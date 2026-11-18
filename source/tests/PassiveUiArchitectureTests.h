#pragma once

#include "ModulationPanel.h"
#include "GuiTestDisplay.h"
#include <cmath>
#include <memory>
#include <stdexcept>

// Include at file scope; run these hooks on the message thread after GUI init.
namespace vocalstack::passiveUiArchitectureTests {
inline void require(bool condition, const juce::String& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

template <class T, class Predicate>
T& find(juce::Component& root, Predicate predicate) {
    const auto search = [&](auto&& self, juce::Component& component) -> T* {
        if (auto* candidate = dynamic_cast<T*>(&component); candidate && predicate(*candidate)) return candidate;
        for (auto* child : component.getChildren())
            if (auto* found = self(self, *child)) return found;
        return nullptr;
    };
    auto* found = search(search, root);
    require(found != nullptr, "Missing passive UI fixture control");
    return *found;
}

inline void expectPassive(juce::Component& component) {
    if (dynamic_cast<juce::TextEditor*>(&component)) return;
    require(!component.getWantsKeyboardFocus() && !component.getMouseClickGrabsKeyboardFocus()
                && !component.isFocusContainer() && !component.isKeyboardFocusContainer(),
            "Idle control regained focus: " + component.getName());
    for (auto* child : component.getChildren()) expectPassive(*child);
}

inline void wheelOver(juce::Component& target, float y, float x = 0, int modifiers = 0) {
    const auto point = target.getLocalBounds().getCentre().toFloat();
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, &target, &target, now, point, now, 0, false};
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = y; wheel.deltaX = x;
    target.mouseWheelMove(event, wheel);
}

struct Fixture {
    std::unique_ptr<Processor> owner = std::make_unique<Processor>();
    juce::Component root, outerContent;
    PassiveViewport outer;
    ModulationPanel panel{*owner};
    explicit Fixture(bool neon) {
        owner->setFlash(neon);
        ModulationState state;
        state.modulators.push_back({1});
        for (const auto* target : {"voice1Gain", "voice1Pan", "voice1Pitch"})
            state.assignments.push_back({1, target, .25f, true});
        juce::String error;
        require(owner->setModulationState(state, error), "Passive UI fixture state: " + error);
        root.setSize(700, 350);
        root.addAndMakeVisible(outer); outer.setBounds(root.getLocalBounds());
        outerContent.setSize(680, 1000);
        outer.setViewedComponent(&outerContent, false);
        outerContent.addAndMakeVisible(panel); panel.setBounds(10, 10, 640, 150);
        panel.select(1); panel.setNeon(neon);
        applyPassiveFocusPolicy(root);
    }
    ~Fixture() { outer.setViewedComponent(nullptr, false); }
    juce::Slider& slider(const juce::String& name) {
        return find<juce::Slider>(panel, [&](auto& s) { return s.getName() == name; });
    }
    juce::Button& button(const juce::String& text) {
        return find<juce::Button>(panel, [&](auto& b) { return b.getButtonText() == text; });
    }
    PassiveViewport& contentViewport() {
        for (auto* child : panel.getChildren())
            if (auto* viewport = dynamic_cast<PassiveViewport*>(child)) return *viewport;
        throw std::runtime_error("ModulationPanel content viewport is not passive");
    }
    void toggleSync() {
        auto& sync = button("Sync");
        sync.setToggleState(!sync.getToggleState(), juce::dontSendNotification);
        sync.onClick();
    }
    void expectTransport() {
        expectPassive(root);
        auto* peer = root.getPeer();
        require(peer != nullptr, "Passive UI native peer missing");
        require(!peer->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey)), "Idle controls consumed host Space");
        require(!peer->handleKeyPress(juce::KeyPress(juce::KeyPress::returnKey)), "Idle controls consumed Return");
    }
};

inline void focusAfterSync() {
    guiTestDisplay::enumerate();
    for (bool neon : {false, true}) {
        Fixture f(neon);
        guiTestDisplay::attach(f.root);
        auto& rate = f.slider("Rate / Hz");
        auto& division = f.slider("Beats / cycle");
        require(dynamic_cast<PassiveNumericSlider*>(&rate) && !dynamic_cast<ModulationSlider*>(&rate),
                "Local numeric inherited assignment behavior");
        for (int repeat = 0; repeat < 4; ++repeat) {
            f.toggleSync();
            require(rate.isEnabled() != division.isEnabled(), "Sync did not switch enabled numerics");
            auto& enabled = rate.isEnabled() ? rate : division;
            require(enabled.isTextBoxEditable(), "Sync disabled deliberate entry");
            auto& label = find<juce::Label>(enabled, [](auto&) { return true; });
            require(label.isEditable(), "Sync recreated a read-only numeric label");
            // Exercise JUCE's native API too, not only our hidden nonvirtual setters.
            juce::Slider& native = enabled;
            native.setEnabled(false); native.setEnabled(true);
            native.lookAndFeelChanged();
            const auto focus = juce::Component::getCurrentlyFocusedComponent();
            const auto point = enabled.getLocalBounds().getCentre().toFloat();
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent down{juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                &enabled, &enabled, now, point, now, 1, false};
            const juce::MouseEvent up{juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys(), 1, 0, 0, 0, 0, &enabled, &enabled, now, point, now, 1, false};
            enabled.mouseDown(down);
            enabled.mouseUp(up);
            require(juce::Component::getCurrentlyFocusedComponent() == focus, "Re-enabled numeric click took focus");
            f.expectTransport();
        }
    }
}

inline void deliberateEditorCommitCancel() {
    guiTestDisplay::enumerate();
    for (bool neon : {false, true}) {
        Fixture f(neon);
        guiTestDisplay::attach(f.root);
        f.toggleSync(); f.toggleSync();
        auto& rate = f.slider("Rate / Hz");
        const auto enter = [&](const juce::String& text) {
            f.contentViewport().setViewPosition(0, 0); f.outer.setViewPosition(0, 0);
            rate.showTextBox();
            auto& label = find<juce::Label>(rate, [](auto& l) { return l.getCurrentTextEditor() != nullptr; });
            auto* editor = label.getCurrentTextEditor();
            require(editor && editor->getWantsKeyboardFocus(), "Deliberate numeric editor cannot receive typing");
            editor->grabKeyboardFocus();
            editor->setText(text, false);
            const auto container = editor->isFocusContainer();
            const auto keyboardContainer = editor->isKeyboardFocusContainer();
            applyPassiveFocusPolicy(f.root);
            require(editor->getWantsKeyboardFocus() && editor->hasKeyboardFocus(false)
                        && editor->isFocusContainer() == container
                        && editor->isKeyboardFocusContainer() == keyboardContainer,
                    "Shared policy modified deliberate editor focus");
            const auto focus = juce::Component::getCurrentlyFocusedComponent();
            wheelOver(f.contentViewport(), -.2f);
            require(label.getCurrentTextEditor() == editor && editor->getText() == text
                        && juce::Component::getCurrentlyFocusedComponent() == focus,
                    "Background wheel disturbed deliberate numeric entry");
        };
        enter("2.5"); rate.hideTextBox(false);
        require(std::abs(rate.getValue() - 2.5) < .001
                    && std::abs(f.owner->getModulationState().modulators.front().rateHz - 2.5f) < .001f,
                "Numeric commit did not update the modulator");
        f.expectTransport();
        enter("7.5"); rate.hideTextBox(true);
        require(std::abs(rate.getValue() - 2.5) < .001
                    && std::abs(f.owner->getModulationState().modulators.front().rateHz - 2.5f) < .001f,
                "Numeric cancel changed the modulator");
        f.expectTransport();
    }
}

inline void modulationPanelNestedWheel() {
    // Real panel hierarchy, but no peer or physical-monitor interaction needed.
    for (bool neon : {false, true}) {
        Fixture f(neon);
        auto& content = f.contentViewport();
        auto& rate = f.slider("Rate / Hz");
        auto& rateLabel = find<juce::Label>(rate, [](auto&) { return true; });
        const auto initialRate = rate.getValue();
        const auto focus = juce::Component::getCurrentlyFocusedComponent();
        for (auto* target : {static_cast<juce::Component*>(&rate), static_cast<juce::Component*>(&rateLabel)}) {
            content.setViewPosition(0, 0); f.outer.setViewPosition(0, 0);
            wheelOver(*target, -.25f);
            require(content.getViewPositionY() > 0 && content.getViewPositionX() == 0
                        && f.outer.getViewPositionY() == 0, "Local numeric wheel missed nearest vertical viewport");
        }
        require(rate.getValue() == initialRate, "Numeric wheel changed rate");
        f.button("Assignments").onClick();
        auto& amount = f.slider("Signed assignment depth");
        auto& valueLabel = find<juce::Label>(amount, [](auto&) { return true; });
        auto* rows = amount.findParentComponentOfClass<PassiveViewport>();
        require(rows && rows != &content && rows->getViewedComponent()->getWidth() > rows->getViewArea().getWidth(),
                "Actual assignment row has no passive horizontal overflow");
        const auto initialDepth = amount.getValue();
        for (auto* target : {static_cast<juce::Component*>(&amount), static_cast<juce::Component*>(&valueLabel),
                             static_cast<juce::Component*>(rows)}) {
            rows->setViewPosition(0, 0); content.setViewPosition(0, 0); f.outer.setViewPosition(0, 0);
            wheelOver(*target, -.25f);
            require(content.getViewPositionY() > 0 && rows->getViewPositionX() == 0
                        && f.outer.getViewPositionY() == 0, "Horizontal assignment row stole vertical wheel");
            content.setViewPosition(0, 10000);
            wheelOver(*target, -.25f);
            require(f.outer.getViewPositionY() > 0 && rows->getViewPositionX() == 0,
                    "Clamped ModulationPanel did not bubble vertical wheel");
            f.outer.setViewPosition(0, 10000);
            wheelOver(*target, -.25f);
            require(rows->getViewPositionX() == 0, "Vertical edge gesture became horizontal scrolling");
        }
        rows->setViewPosition(0, 0); content.setViewPosition(0, 0); f.outer.setViewPosition(0, 0);
        wheelOver(valueLabel, -.25f, 0, juce::ModifierKeys::shiftModifier);
        require(rows->getViewPositionX() > 0 && content.getViewPositionY() == 0
                    && f.outer.getViewPositionY() == 0, "Shift-wheel did not retain horizontal assignment navigation");
        require(amount.getValue() == initialDepth && juce::Component::getCurrentlyFocusedComponent() == focus,
                "Assignment wheel changed depth or focus");
        for (const auto& assignment : f.owner->getModulationState().assignments)
            require(assignment.depth == .25f, "Assignment wheel wrote modulation state");
        expectPassive(f.panel);
    }
}

inline void run() {
    focusAfterSync();
    deliberateEditorCommitCancel();
    modulationPanelNestedWheel();
}
} // namespace vocalstack::passiveUiArchitectureTests
