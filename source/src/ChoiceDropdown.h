#pragma once

#include "PluginProcessor.h"
#include "ModulationUI.h"
#include "VisualTheme.h"
#include "UiControlPolicy.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace vocalstack {

// Message-thread UI. Processor (and its parameters) must outlive this control.
class ChoiceDropdown final : public juce::Component {
public:
    // Pure timing model: milliseconds from any monotonic clock, no JUCE/message
    // loop required. Each pointer source has its own hold; inertia is activity,
    // even when a wheel event cannot move the viewport any further.
    struct ScrollReturnState {
        static constexpr double delayMs = 2500.0;

        void inputPressed(int source, double now) {
            if (std::find(heldSources.begin(), heldSources.end(), source) == heldSources.end())
                heldSources.push_back(source);
            scrolled(now);
        }
        void inputReleased(int source, double now) {
            const auto it = std::find(heldSources.begin(), heldSources.end(), source);
            if (it == heldSources.end()) return;
            heldSources.erase(it);
            scrolled(now);
        }
        void scrolled(double now) noexcept { lastActivityMs = now; pending = true; }
        bool consumeReturn(double now) noexcept {
            if (!pending || !heldSources.empty() || now - lastActivityMs < delayMs) return false;
            pending = false;
            return true;
        }
        void cancel() noexcept { pending = false; heldSources.clear(); }
        bool isHeld() const noexcept { return !heldSources.empty(); }
        bool isPending() const noexcept { return pending; }
        double lastActivityMs = 0;
        bool pending = false;
        std::vector<int> heldSources;
    };

    ChoiceDropdown(Processor& processor, const juce::String& parameterId,
                   const juce::String& label, juce::StringArray names)
        : owner(processor), options(std::move(names)), caption(label), id(parameterId),
          parameter(processor.parameters.getParameter(parameterId)) {
        setComponentID(id + "Control");
        setName(label);
        title.setText(label, juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centred);
        title.setFont(juce::Font(pixelFontOptions(14.f)));
        title.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(title);
        trigger.setModulationOwner(processor);
        trigger.setComponentID(id);
        trigger.setTitle(label);
        trigger.setTooltip(label + ": choose an option. Scroll the open list to browse.");
        trigger.onClick = [safe = juce::Component::SafePointer<ChoiceDropdown>(this)] {
            if (safe) safe->togglePopup();
        };
        addAndMakeVisible(trigger);
        setLookAndFeel(&style);
        makePassive(*this);
        if (parameter != nullptr && !options.isEmpty()) {
            // ParameterAttachment delivers unnormalised values on the message thread.
            attachment = std::make_unique<juce::ParameterAttachment>(*parameter,
                [safe = juce::Component::SafePointer<ChoiceDropdown>(this)](float value) {
                    if (safe) safe->parameterUpdated(value);
                }, nullptr);
            attachment->sendInitialUpdate();
        } else {
            jassertfalse; // A missing ID/empty list must not write another parameter.
            trigger.setEnabled(false);
            trigger.setButtonText("--");
        }
        setNeon(false);
    }

    ~ChoiceDropdown() override {
        onSelection = nullptr;
        trigger.onClick = nullptr;
        popup.reset();
        attachment.reset();
        setLookAndFeel(nullptr);
    }

    void setNeon(bool neon) {
        legacyNeon = neon;
        const auto palette = themePalette(*this, neon);
        title.setColour(juce::Label::textColourId, palette.text);
        styleButton(trigger, palette);
        if (popup) popup->applyTheme(componentTheme(*this, neon));
        makePassive(*this);
        repaint();
    }

    int getSelectedIndex() const noexcept { return selected; }

    // A complete host gesture; invalid indices are ignored, never clamped into
    // a different user choice. Like clicking a row, this notifies onSelection
    // only for a changed selection. Automation updates do not invoke it.
    void selectIndex(int index) {
        // A list opened before Assign must not commit a late base-value edit.
        if (owner.modulationAssignmentId() != 0 || !attachment
            || index < 0 || index >= options.size() || index == selected) return;
        const auto safe = juce::Component::SafePointer<ChoiceDropdown>(this);
        const float value = valueForIndex(index);
        attachment->setValueAsCompleteGesture(value);
        if (!safe) return;
        parameterUpdated(parameter->convertFrom0to1(parameter->getValue()));
        auto callback = onSelection;
        if (callback) callback(selected); // Last operation: clients may destroy us.
    }

    std::function<void(int)> onSelection;

    void resized() override {
        auto area = getLocalBounds();
        title.setBounds(area.removeFromTop(20));
        trigger.setBounds(area.reduced(2, 3));
        closePopup();
    }
    void visibilityChanged() override { if (!isShowing()) closePopup(); }
    void parentHierarchyChanged() override { closePopup(); setNeon(legacyNeon); }
    void enablementChanged() override { if (!isEnabled()) closePopup(); }

    // Optional deterministic UI hooks, in addition to the pure state above.
    // showPopup requires a showing native owner; advancePopupTimeForTesting
    // uses the same clock domain as Time::getMillisecondCounterHiRes().
    void showPopup() {
        if (owner.modulationAssignmentId() != 0 || popup || !attachment
            || !isEnabled() || !isShowing() || getPeer() == nullptr) return;
        const auto anchor = trigger.getScreenBounds();
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(anchor);
        if (display == nullptr) return;
        const auto work = display->userArea.reduced(4);
        if (work.getWidth() < 40 || work.getHeight() < 40) return;
        popup = std::make_unique<Popup>(*this);
        popup->applyTheme(componentTheme(*this, legacyNeon));
        const int width = juce::jmin(work.getWidth(), juce::jmax(190, trigger.getWidth()));
        const int height = juce::jmin(work.getHeight(), 8 + Popup::rowHeight * juce::jmin(7, options.size()));
        int y = anchor.getBottom();
        if (y + height > work.getBottom()) y = anchor.getY() - height;
        const juce::Rectangle<int> bounds(
            juce::jlimit(work.getX(), work.getRight() - width, anchor.getX()),
            juce::jlimit(work.getY(), work.getBottom() - height, y), width, height);
        popup->setBounds(bounds);
        // Lifetime-owned by this control, but a top-level peer: passing the
        // plugin HWND to addToDesktop creates a clipped WS_CHILD on Windows,
        // not an owned popup. No modal loop, focus grab, or always-on-top flag.
        popup->addToDesktop(juce::ComponentPeer::windowIsTemporary
                           | juce::ComponentPeer::windowIgnoresKeyPresses);
        popup->setVisible(true);
        popup->centreSelected();
    }
    void closePopup() { popup.reset(); }
    bool isPopupOpen() const noexcept { return popup != nullptr && popup->isVisible(); }
    juce::Viewport* getPopupViewportForTesting() noexcept { return popup ? &popup->viewport : nullptr; }
    ScrollReturnState* getPopupScrollStateForTesting() noexcept { return popup ? &popup->scrollReturn : nullptr; }
    void advancePopupTimeForTesting(double now) { if (popup) popup->advanceTime(now); }

private:
    static void makePassive(juce::Component& component) {
        applyPassiveFocusPolicy(component);
    }
    static void styleButton(juce::TextButton& button, const ThemePalette& p) {
        button.setColour(juce::TextButton::buttonColourId, p.inset);
        button.setColour(juce::TextButton::buttonOnColourId, p.accent);
        button.setColour(juce::TextButton::textColourOffId, p.text);
        button.setColour(juce::TextButton::textColourOnId, p.background);
        button.setColour(juce::ComboBox::outlineColourId, p.border);
    }
    float valueForIndex(int index) const {
        return parameter->convertFrom0to1(options.size() <= 1 ? 0.f : float(index) / float(options.size() - 1));
    }
    void parameterUpdated(float value) {
        if (!std::isfinite(value)) return;
        selected = juce::jlimit(0, options.size() - 1,
            juce::roundToInt(parameter->convertTo0to1(value) * float(options.size() - 1)));
        trigger.setButtonText(options[selected] + "  v");
        trigger.setDescription(caption + ": " + options[selected]);
        if (popup) popup->updateSelection(); // Never moves an actively browsed list.
    }
    void togglePopup() { if (popup) closePopup(); else showPopup(); }

    class Popup final : public juce::Component, private juce::Timer {
    public:
        static constexpr int rowHeight = 32;
        explicit Popup(ChoiceDropdown& dropdown)
            : owner(dropdown), input(*this), viewport(*this), anchor(dropdown.trigger.getScreenBounds()) {
            setOpaque(true);
            setName(dropdown.caption + " options");
            setComponentID(dropdown.id + "Options");
            setLookAndFeel(&style);
            viewport.setComponentID(dropdown.id + "Viewport");
            viewport.setScrollBarsShown(true, false);
            viewport.setScrollBarThickness(scrollbarMetrics::standard);
            viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);
            viewport.setViewedComponent(&content, false);
            addAndMakeVisible(viewport);
            for (int i = 0; i < owner.options.size(); ++i) {
                auto button = std::make_unique<juce::TextButton>(owner.options[i]);
                button->setComponentID(owner.id + "Option" + juce::String(i));
                button->setTitle(owner.caption + ": " + owner.options[i]);
                // JUCE's button accessibility handler exposes the radio group
                // and checked state; no painted pseudo-controls or hover selection.
                button->setClickingTogglesState(true);
                button->setRadioGroupId(1);
                button->onClick = [safe = juce::Component::SafePointer<Popup>(this), i] {
                    if (!safe) return;
                    auto dropdown = juce::Component::SafePointer<ChoiceDropdown>(&safe->owner);
                    safe->requestClose();
                    if (dropdown) dropdown->selectIndex(i);
                };
                content.addAndMakeVisible(*button);
                rows.push_back(std::move(button));
            }
            addMouseListener(&input, true);
            juce::Desktop::getInstance().addGlobalMouseListener(&input);
            makePassive(*this);
            updateSelection();
            startTimerHz(60);
        }
        ~Popup() override {
            stopTimer();
            scrollReturn.cancel();
            juce::Desktop::getInstance().removeGlobalMouseListener(&input);
            removeMouseListener(&input);
            for (auto& row : rows) row->onClick = nullptr;
            viewport.setViewedComponent(nullptr, false);
            setLookAndFeel(nullptr);
        }
        void applyTheme(Theme theme) {
            getProperties().set(vocalstackThemeProperty, int(theme));
            palette = themePalette(theme);
            for (auto& row : rows) styleButton(*row, palette);
            viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, palette.accent);
            viewport.getVerticalScrollBar().setColour(juce::ScrollBar::backgroundColourId, palette.inset);
            repaint();
        }
        void updateSelection() {
            for (int i = 0; i < int(rows.size()); ++i)
                rows[size_t(i)]->setToggleState(i == owner.selected, juce::dontSendNotification);
        }
        void resized() override {
            programmaticScroll = true;
            viewport.setBounds(getLocalBounds().reduced(4));
            const int contentHeight = int(rows.size()) * rowHeight;
            const int width = juce::jmax(1, viewport.getWidth()
                - (contentHeight > viewport.getHeight() ? viewport.getScrollBarThickness() : 0));
            content.setSize(width, contentHeight);
            for (int i = 0; i < int(rows.size()); ++i)
                rows[size_t(i)]->setBounds(0, i * rowHeight, width, rowHeight);
            programmaticScroll = false;
        }
        void paint(juce::Graphics& g) override {
            g.fillAll(palette.panel);
            g.setColour(palette.border);
            g.drawRect(getLocalBounds());
        }
        void centreSelected() {
            programmaticScroll = true;
            const int maxY = juce::jmax(0, content.getHeight() - viewport.getViewHeight());
            viewport.setViewPosition(0, juce::jlimit(0, maxY,
                owner.selected * rowHeight + rowHeight / 2 - viewport.getViewHeight() / 2));
            programmaticScroll = false;
        }
        void advanceTime(double now) {
            if (!closing && scrollReturn.consumeReturn(now)) centreSelected();
        }
        void requestClose() {
            if (closing) return;
            closing = true;
            stopTimer();
            scrollReturn.cancel();
            setVisible(false);
            // Never delete a button/viewport while JUCE is dispatching its event.
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Popup>(this)] {
                if (safe) safe->owner.closePopup();
            });
        }

        ScrollReturnState scrollReturn;
        class ListViewport final : public juce::Viewport {
        public:
            explicit ListViewport(Popup& p) : popup(p) {}
            void visibleAreaChanged(const juce::Rectangle<int>&) override {
                if (!popup.programmaticScroll && !popup.closing)
                    popup.scrollReturn.scrolled(juce::Time::getMillisecondCounterHiRes());
            }
        private:
            Popup& popup;
        };

    private:
        struct Input final : juce::MouseListener {
            explicit Input(Popup& p) : popup(p) {}
            bool inside(const juce::MouseEvent& e) const {
                return e.eventComponent == &popup || popup.isParentOf(e.eventComponent);
            }
            void mouseDown(const juce::MouseEvent& e) override {
                if (!inside(e)) { popup.requestClose(); return; }
                if (!popup.closing) popup.scrollReturn.inputPressed(e.source.getIndex(), now());
            }
            void mouseUp(const juce::MouseEvent& e) override {
                if (!popup.closing) popup.scrollReturn.inputReleased(e.source.getIndex(), now());
            }
            void mouseDrag(const juce::MouseEvent& e) override {
                if (!popup.closing && inside(e)) popup.scrollReturn.scrolled(now());
            }
            void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails&) override {
                if (!popup.closing && inside(e)) popup.scrollReturn.scrolled(now());
            }
            static double now() { return juce::Time::getMillisecondCounterHiRes(); }
            Popup& popup;
        };
        void timerCallback() override {
            // A sandboxed plugin can have a foreground DAW in another process;
            // Process::isForegroundProcess() would wrongly close its passive UI.
            if (owner.owner.modulationAssignmentId() != 0 || !owner.isShowing()
                || !owner.isEnabled() || owner.trigger.getScreenBounds() != anchor) {
                requestClose();
                return;
            }
            advanceTime(juce::Time::getMillisecondCounterHiRes());
        }
        ChoiceDropdown& owner;
        PixelLookAndFeel style;
        Input input;
    public:
        ListViewport viewport;
    private:
        juce::Component content;
        std::vector<std::unique_ptr<juce::TextButton>> rows;
        juce::Rectangle<int> anchor;
        ThemePalette palette = themePalette(Theme::device);
        bool programmaticScroll = false, closing = false;
    };

    Processor& owner;
    juce::StringArray options;
    juce::String caption, id;
    juce::RangedAudioParameter* parameter = nullptr;
    int selected = -1;
    bool legacyNeon = false;
    PixelLookAndFeel style;
    juce::Label title;
    modulationui::Button trigger;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    std::unique_ptr<Popup> popup;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoiceDropdown)
};

} // namespace vocalstack
