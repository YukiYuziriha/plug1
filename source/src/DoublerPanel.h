#include "VisualTheme.h"
#pragma once

#include "PluginProcessor.h"
#include "VoiceMonitor.h"
#include "ParameterKnob.h"
#include "ModulationUI.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace vocalstack {
namespace detail {
inline void passiveControl(juce::Component& component) {
    if (dynamic_cast<juce::TextEditor*>(&component) != nullptr) return;
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    for (auto* child : component.getChildren()) passiveControl(*child);
}

// Signed tracks start at the actual zero value (gain is not symmetric).
using ParameterLookAndFeel = ParameterKnob::Style;

inline void styleSlider(juce::Slider& slider, bool neon) {
    const auto p = themePalette(slider, neon);
    const bool device = componentTheme(slider, neon) == Theme::device;
    slider.setColour(juce::Slider::textBoxTextColourId, p.text);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, device ? juce::Colour(0xff20262d) : p.inset);
    slider.setColour(juce::Slider::textBoxOutlineColourId, device ? juce::Colour(0xff526071) : p.border);
    slider.setColour(juce::Slider::trackColourId, p.accent);
    slider.setColour(juce::Slider::thumbColourId, device ? p.text : p.secondary);
    passiveControl(slider);
}

class ParameterSlider final : public juce::Component {
public:
    ParameterSlider(Processor& owner, const juce::String& id, const juce::String& title, const char* unit) {
        label.setText(title, juce::dontSendNotification);
        label.setFont(juce::Font(pixelFontOptions(title == "PITCH" ? 13.0f : 11.5f,
                                                        title == "PITCH" ? juce::Font::bold : juce::Font::plain)));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
        slider.setLookAndFeel(&style);
        slider.bindModulation(owner, id);
        slider.setName(title);
        slider.setTitle(title);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 23);
        slider.setScrollWheelEnabled(false);
        addAndMakeVisible(slider);
        link = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, id, slider);
        if (const auto* parameter = owner.parameters.getParameter(id))
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
        const auto suffix = juce::String(unit).isEmpty() ? juce::String() : juce::String(" ") + unit;
        const int decimals = juce::String(unit) == "%" ? 0 : 2;
        slider.textFromValueFunction = [suffix, decimals](double value) { return juce::String(value, decimals) + suffix; };
        slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
        if (title == "Pan") {
            slider.textFromValueFunction = [](double v) {
                return std::abs(v) < 0.005 ? juce::String("C") : juce::String(std::abs(v) * 100, 0) + (v < 0 ? " L" : " R");
            };
            slider.valueFromTextFunction = [](const juce::String& value) {
                if (value.containsIgnoreCase("C")) return 0.0;
                const double v = value.getDoubleValue();
                if (value.containsIgnoreCase("L")) return -std::abs(v) / 100;
                if (value.containsIgnoreCase("R")) return std::abs(v) / 100;
                return v;
            };
        }
        slider.updateText();
        passiveControl(*this);
    }
    ~ParameterSlider() override { slider.setLookAndFeel(nullptr); }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        forwardPassiveWheel(*this, event, wheel);
    }
    void resized() override {
        auto area = getLocalBounds();
        label.setBounds(area.removeFromLeft(70));
        slider.setBounds(area);
    }
    void setNeon(bool neon) {
        label.setColour(juce::Label::textColourId, themePalette(*this, neon).text);
        styleSlider(slider, neon);
    }
    ModulationSlider slider;
private:
    ParameterLookAndFeel style;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> link;
};
} // namespace detail

// slot -1 is Lead; slots 0..7 retain their existing host parameter IDs.
class VoiceControls final : public juce::Component {
    class SelectionClicks final : public juce::MouseListener {
    public:
        std::function<void()> onClick;
        void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
    } selectionClicks;
    public:
        enum class ControlMode { knobs, stackSliders };
        VoiceControls(Processor& owner, int index, ControlMode mode = ControlMode::knobs)
            : voiceSlot(index), controlMode(mode), pitch(owner, index < 0 ? "leadPitch" : voicePitchParameterId(size_t(index)), "PITCH", "st") {
            jassert(index >= -1 && index < 8);
            setComponentID(index < 0 ? "leadCard" : "voice" + juce::String(index + 1) + "Card");
            const auto parameterId = [index](size_t field) -> juce::String {
                return index < 0 ? leadParameterIds[field] : voiceParameterId(size_t(index), field);
            };
            enabled.setModulationOwner(owner);
            enabled.setComponentID(parameterId(0));
            enabled.setButtonText("On");
            enabled.setName(index < 0 ? "Enable Lead" : "Enable voice " + juce::String(index + 1));
            enabled.setClickingTogglesState(true);
            enabled.setTooltip("Mute this voice without removing its card or settings");
            addAndMakeVisible(enabled);
            link = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, parameterId(0), enabled);
            addAndMakeVisible(pitch);
            pitch.slider.setTooltip("Musical interval: -36 to +36 semitones; independent of Fine Detune. Fractional intervals supported");
            const std::array<const char*, 7> names{"Gain", "Pan", "Delay", "Fine detune", "Formant", "Depth", "Rate"};
            const std::array<const char*, 7> units{"dB", "", "ms", "ct", "st", "x", "Hz"};
            for (size_t f = 0; f < fields.size(); ++f) {
                fields[f] = std::make_unique<ParameterKnob>(owner, parameterId(f + 1), names[f], units[f]);
                addAndMakeVisible(*fields[f]);
            }
            pitch.setLinear(controlMode == ControlMode::stackSliders);
            for (auto& field : fields) field->setLinear(controlMode == ControlMode::stackSliders);
            fields[1]->slider.setTooltip("L / C / R; effective spread also includes main Width");
            fields[3]->slider.setTooltip("Fine Detune in cents, separate from musical Pitch");
            fields[4]->slider.setTooltip("Voice formant +/-24 st; adds to shared formant and remains active when shared Formant is off");
            fields[5]->slider.setTooltip("Motion depth: zero stops motion, not static offsets");
            remove.setButtonText("Remove");
            remove.setTooltip("Remove this stable voice slot; surviving voices are not renumbered");
            remove.onClick = [&owner, index] { owner.removeVoice(size_t(index)); };
            if (index >= 2) addAndMakeVisible(remove);
            selectionClicks.onClick = [this, &owner, index] {
                            if (index >= 0 && !owner.settings().advanced) owner.setAdvancedEnabled(true);
                            if (onSelect) onSelect();
            };
            addMouseListener(&selectionClicks, true);
            detail::passiveControl(*this);
        }
        ~VoiceControls() override { removeMouseListener(&selectionClicks); }
        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
            forwardPassiveWheel(*this, event, wheel);
        }
        void resized() override {
            auto area = getLocalBounds().reduced(8);
            auto header = area.removeFromTop(25);
            remove.setBounds(0, 0, 0, 0);
                        if (!compact) remove.setBounds(header.removeFromRight(64));
                        else if (voiceSlot >= 2) remove.setBounds(getWidth() - 58, 35, 50, 18);
            header.removeFromLeft(compact ? 70 : 112);
            enabled.setBounds(header.removeFromLeft(44));
            area.removeFromTop(5);
            std::array<ParameterKnob*, 8> controls{&pitch, fields[3].get(), fields[4].get(), fields[0].get(),
                fields[1].get(), fields[2].get(), fields[5].get(), fields[6].get()};
            const int columns = compact ? 2 : 4;
            if (compact) area.removeFromTop(14);
            const int width = area.getWidth() / columns;
            const int height = compact && controlMode == ControlMode::stackSliders ? 54 : 104;
            for (size_t i = 0; i < controls.size(); ++i)
                controls[i]->setBounds(area.getX() + int(i % size_t(columns)) * width,
                    area.getY() + int(i / size_t(columns)) * height, width, height);
        }

        void paint(juce::Graphics& g) override {
            const auto p = themePalette(*this, neon);
            const auto selectedTheme = componentTheme(*this, neon);
            const bool device = selectedTheme == Theme::device;
            const float radius = device ? 4.0f : 0.0f;
            g.setColour(p.panel);
            g.fillRoundedRectangle(getLocalBounds().toFloat(), radius);
            const auto colour = device
                ? (voiceSlot < 0 ? juce::Colour(0xfff8de6b) : VoiceMonitor::voiceColour(size_t(voiceSlot)))
                : (voiceSlot < 0 ? p.secondary : p.accent);
            g.setColour(selected ? colour : device ? juce::Colour(0xff515850) : p.border);
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.7f), radius, selected ? 1.6f : 1.0f);
            g.setColour(colour.withAlpha(device || selected ? 0.12f : 0.05f));
            g.fillRoundedRectangle(5.0f, 5.0f, float(getWidth() - 10), 32.0f, device ? 3.0f : 0.0f);
            g.setColour(colour);
            g.fillEllipse(12, 10, 22, 22);
            g.setFont(juce::Font(pixelFontOptions(12.0f, juce::Font::bold)));
            g.setColour(juce::Colour(0xff151b20));
            g.drawText(voiceSlot < 0 ? "L" : juce::String(voiceSlot + 1), 12, 10, 22, 22, juce::Justification::centred);
            g.setColour(p.text);
            g.drawText(voiceSlot < 0 ? "LEAD" : "VOICE", 42, 10, 66, 22, juce::Justification::centredLeft);
        }
        void setCompact(bool value) {
                    compact = value;

                    resized();
                }
                void setSelected(bool value) { selected = value; repaint(); }
        void setNeon(bool value) {
            neon = value;
            for (auto* button : std::array<juce::TextButton*, 2>{&enabled, &remove}) {
                button->setColour(juce::TextButton::textColourOffId, themePalette(*this, neon).text);
                button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
            }
            pitch.setNeon(neon);
            for (auto& field : fields) field->setNeon(neon);
            repaint();
        }
        std::function<void()> onSelect;
    private:
        const int voiceSlot;
        const ControlMode controlMode;
        bool neon = false, selected = false, compact = false;
        modulationui::Button enabled;
        juce::TextButton remove;
        ParameterKnob pitch;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> link;
        std::array<std::unique_ptr<ParameterKnob>, 7> fields;
};

class DoublerPanel final : public juce::Component {
    // Observes clicks without taking focus or stealing the slider gesture.
    class CardClicks final : public juce::MouseListener {
    public:
        std::function<void(juce::Component*)> onClick;
        void mouseDown(const juce::MouseEvent& e) override { if (onClick) onClick(e.eventComponent); }
    } clicks;
public:
    explicit DoublerPanel(Processor& processor) : owner(processor), lead(processor, -1, VoiceControls::ControlMode::stackSliders) {
        setName("Advanced Doubler drawer");

        reset.setButtonText("Reset to Basic");

        reset.setTooltip("Restore two Basic-derived voices; unrelated modules stay unchanged");
        reset.onClick = [this] { owner.resetVoicesToBasic(); sync(); };
        add.setButtonText("+ Add voice");
        add.onClick = [this] {
                    const auto before = owner.settings();
                    if (!owner.addVoice()) return;
                    sync();
                    for (size_t i = 0; i < cards.size(); ++i)
                        if (!before.voiceSettings[i].present) { select(i, true); break; }
                };
        for (auto* button : {&reset, &add}) addAndMakeVisible(*button);
        addAndMakeVisible(monitor);
        monitor.onSelect = [this](size_t slot) {
                    if (slot == VoiceMonitor::leadSlot) selectLead(true); else select(slot, true);
                };
        viewport.setViewedComponent(&cardArea, false);
        viewport.setScrollBarsShown(false, true);
        viewport.setScrollBarThickness(12);
        viewport.setScrollOnDragEnabled(false);
        addAndMakeVisible(viewport);
        clicks.onClick = [this](juce::Component* c) {
            if (c == &lead || lead.isParentOf(c)) { selectLead(false); return; }
            for (size_t i = 0; i < cards.size(); ++i)
                if (c == cards[i].get() || cards[i]->isParentOf(c)) { select(i, false); break; }
        };
        for (size_t v = 0; v < cards.size(); ++v) {
            cards[v] = std::make_unique<VoiceControls>(owner, int(v), VoiceControls::ControlMode::stackSliders);
            cards[v]->setCompact(true);
                        cards[v]->onSelect = [this, v] { select(v, false); };
            cards[v]->addMouseListener(&clicks, true);
            cardArea.addAndMakeVisible(*cards[v]);
        }
        lead.setCompact(true);
                lead.onSelect = [this] { selectLead(false); };
        lead.addMouseListener(&clicks, true);
        cardArea.addAndMakeVisible(lead);
        sync();
        selectLead(false);
        detail::passiveControl(*this);
    }
    ~DoublerPanel() override {
        lead.removeMouseListener(&clicks);
        for (auto& card : cards) card->removeMouseListener(&clicks);
        viewport.setViewedComponent(nullptr, false);
    }
    void sync() {
        const auto settings = owner.settings();

        bool changed = false;
        int count = 0;
        for (size_t i = 0; i < cards.size(); ++i) {
            const bool present = settings.voiceSettings[i].present;
            changed |= cards[i]->isVisible() != present;
            cards[i]->setVisible(present);
            if (present) ++count;
        }
        add.setEnabled(count < 8);
        if (changed) resized();
        if (selectedSlot < 8 && !settings.voiceSettings[selectedSlot].present)
            for (size_t i = 0; i < cards.size(); ++i)
                if (settings.voiceSettings[i].present) { select(i, false); break; }
        monitor.update(owner.getVoiceSnapshots(), owner.getLeadSnapshot());
    }
    void setNeon(bool neon) {
        for (auto* button : {&reset, &add}) {
            button->setColour(juce::TextButton::textColourOffId, themePalette(*this, neon).text);
            button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
        }

        const auto p = themePalette(*this, neon);
        viewport.getHorizontalScrollBar().setColour(juce::ScrollBar::thumbColourId, p.muted.withAlpha(0.45f));
        viewport.getHorizontalScrollBar().setColour(juce::ScrollBar::backgroundColourId, p.inset);
        monitor.setNeon(neon);
        lead.setNeon(neon);
        for (auto& card : cards) card->setNeon(neon);
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        forwardPassiveWheel(*this, event, wheel);
    }
    std::function<void(int)> onVoiceSelected;
    // Mirroring Routing selection must not emit another selection request.
    void selectVoice(int slot, bool reveal = true, bool notify = true) {
        if (slot < 0) selectLead(reveal, notify);
        else if (slot < 8) select(size_t(slot), reveal, notify);
    }
    void resized() override {

        auto actions = getLocalBounds().removeFromTop(26);
        reset.setBounds(actions.removeFromRight(116));
        actions.removeFromRight(12);
        add.setBounds(actions.removeFromRight(116));
        monitor.setBounds(0, 32, getWidth(), 100);
        viewport.setBounds(0, 140, getWidth(), juce::jmax(0, getHeight() - 140));
        lead.setBounds(0, 0, 172, 278);
        int x = 180;
        for (auto& card : cards) if (card->isVisible()) {
            card->setBounds(x, 0, 172, 278);
            x += 180;
        }
        cardArea.setSize(juce::jmax(getWidth(), x - 8), 278);
    }
private:
    void select(size_t slot, bool reveal, bool notify = true) {
        selectedSlot = slot;
        lead.setSelected(false);
        monitor.select(slot);
        for (size_t i = 0; i < cards.size(); ++i) cards[i]->setSelected(i == slot);
        if (reveal && cards[slot]->isVisible())
            viewport.setViewPosition(juce::jlimit(0, juce::jmax(0, cardArea.getWidth() - viewport.getWidth()), cards[slot]->getX()), 0);
        if (notify && onVoiceSelected) onVoiceSelected(int(slot));
    }
    void selectLead(bool reveal, bool notify = true) {
        selectedSlot = VoiceMonitor::leadSlot;
        monitor.select(VoiceMonitor::leadSlot);
        lead.setSelected(true);
        for (auto& card : cards) card->setSelected(false);
        if (reveal) viewport.setViewPosition(0, 0);
        if (notify && onVoiceSelected) onVoiceSelected(-1);
    }
    Processor& owner;
    size_t selectedSlot = 0;
    juce::TextButton reset, add;
    VoiceMonitor monitor;
    juce::Component cardArea;
    PassiveViewport viewport;
    VoiceControls lead;
    std::array<std::unique_ptr<VoiceControls>, 8> cards;
};

class ChoralePanel final : public juce::Component {
public:
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        forwardPassiveWheel(*this, event, wheel);
    }
    explicit ChoralePanel(Processor& owner)
        : mix(owner, "choraleMix", "Mix", "%"), motion(owner, "choraleMotion", "Motion", "%") {
        setName("Chorale section");
        enabled.setModulationOwner(owner);
        enabled.setComponentID("choraleOn");
        enabled.setButtonText("CHORALE");
        enabled.setClickingTogglesState(true);
        enabled.setTooltip("Enable the separate Chorale effect (off by default)");
        addAndMakeVisible(enabled);
        addAndMakeVisible(mix);
        addAndMakeVisible(motion);
        for (auto* control : {&mix, &motion})
            control->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 19);
        link = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, "choraleOn", enabled);
        detail::passiveControl(*this);
    }
    void resized() override {
        enabled.setBounds(12, 16, 112, 26);
        mix.setBounds(148, 2, 140, 60);
        motion.setBounds(312, 2, 140, 60);
    }
    void paint(juce::Graphics& g) override {
        const auto p = themePalette(*this, neon);
        const bool device = componentTheme(*this, neon) == Theme::device;
        const float radius = device ? 4.0f : 0.0f;
        g.setColour(p.panel);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), radius);
        g.setColour(device ? juce::Colour(0xff515850) : p.border);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), radius, 1.0f);
    }
    void setNeon(bool value) {
        neon = value;
        enabled.setColour(juce::TextButton::textColourOffId, themePalette(*this, neon).text);
        enabled.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
        mix.setNeon(neon);
        motion.setNeon(neon);
        repaint();
    }
private:
    bool neon = false;
    modulationui::Button enabled;
    ParameterKnob mix, motion;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> link;
};
} // namespace vocalstack
