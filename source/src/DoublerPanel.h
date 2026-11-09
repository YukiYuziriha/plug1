#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace vocalstack {
namespace detail {
inline void passiveControl(juce::Component& component) {
    if (dynamic_cast<juce::TextEditor*>(&component) != nullptr)
        return; // Deliberate numeric entry is the only keyboard owner.
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    for (auto* child : component.getChildren())
        passiveControl(*child);
}

inline void styleSlider(juce::Slider& slider, bool neon) {
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe4e9ee));
    slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff20262d));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff526071));
    slider.setColour(juce::Slider::trackColourId, juce::Colour(neon ? 0xffff4ca0 : 0xfff5a24b));
    slider.setColour(juce::Slider::thumbColourId, juce::Colour(neon ? 0xff64f5e3 : 0xffe4e4df));
    passiveControl(slider);
}

// Header-only UI keeps this presentation component independent of build targets.
class ParameterSlider final : public juce::Component {
public:
    ParameterSlider(Processor& owner, const char* id, const juce::String& title, const char* unit) {
        label.setText(title, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
        slider.setComponentID(id);
        slider.setName(title);
        slider.setTitle(title);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 23);
        slider.setScrollWheelEnabled(false);
        addAndMakeVisible(slider);
        link = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, id, slider);
        const auto* parameter = owner.parameters.getParameter(id);
        slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
        const auto suffix = juce::String(unit).isEmpty() ? juce::String() : juce::String(" ") + unit;
        const int decimals = juce::String(unit) == "%" ? 0 : 2;
        slider.textFromValueFunction = [suffix, decimals](double value) { return juce::String(value, decimals) + suffix; };
        slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
        slider.updateText();
        passiveControl(*this);
    }
    void resized() override {
        auto area = getLocalBounds();
        label.setBounds(area.removeFromLeft(64));
        slider.setBounds(area);
    }
    void setNeon(bool neon) { styleSlider(slider, neon); }
    juce::Slider slider;
private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> link;
};
} // namespace detail

class DoublerPanel final : public juce::Component {
    class VoiceCard final : public juce::Component {
    public:
        VoiceCard(Processor& owner, size_t index) {
            const auto base = 11 + index * 8;
            enabled.setComponentID(parameterIds[base]);
            enabled.setButtonText("VOICE " + juce::String(static_cast<int>(index + 1)));
            enabled.setClickingTogglesState(true);
            enabled.setTooltip("Enable this generated voice");
            addAndMakeVisible(enabled);
            link = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                owner.parameters, parameterIds[base], enabled);
            const std::array<const char*, 7> names{"Gain", "Pan", "Delay", "Detune", "Formant", "Depth", "Rate"};
            const std::array<const char*, 7> units{"dB", "", "ms", "ct", "st", "x", "Hz"};
            for (size_t f = 0; f < fields.size(); ++f) {
                fields[f] = std::make_unique<detail::ParameterSlider>(owner, parameterIds[base + 1 + f], names[f], units[f]);
                addAndMakeVisible(*fields[f]);
            }
            fields[1]->slider.setTooltip("Pan: -1 left, 0 centre, +1 right; Width scales the spread");
            fields[4]->slider.setTooltip("Voice formant offset in semitones; independent of global Formant Active");
            fields[5]->slider.setTooltip("Motion depth: zero stops motion but retains static offsets");
            fields[6]->slider.setTooltip("Motion rate in Hz (0 to 8)");
            detail::passiveControl(*this);
        }
        void resized() override {
            auto area = getLocalBounds().reduced(8);
            enabled.setBounds(area.removeFromTop(25));
            area.removeFromTop(6);
            for (auto& field : fields)
                field->setBounds(area.removeFromTop(33));
        }
        void paint(juce::Graphics& g) override {
            g.setColour(juce::Colour(neon ? 0xff111827 : 0xff252826));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
            g.setColour(juce::Colour(neon ? 0xff2b3a53 : 0xff515850));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
        }
        void setNeon(bool value) {
            neon = value;
            enabled.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
            enabled.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
            for (auto& field : fields) field->setNeon(neon);
            repaint();
        }
    private:
        bool neon = false;
        juce::TextButton enabled;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> link;
        std::array<std::unique_ptr<detail::ParameterSlider>, 7> fields;
    };
public:
    explicit DoublerPanel(Processor& processor) : owner(processor) {
        setName("Advanced Doubler drawer");
        enabled.setComponentID("advancedOn");
        enabled.setButtonText("ADVANCED DOUBLER");
        enabled.setClickingTogglesState(true);
        enabled.setToggleState(owner.settings().advanced, juce::dontSendNotification);
        enabled.onClick = [this] { owner.setAdvancedEnabled(enabled.getToggleState()); };
        enabled.setTooltip("Use the stored four-voice settings instead of Basic. Disabling preserves edits");
        reset.setButtonText("Reset to Basic");
        reset.setTooltip("Restore voices from current Human: two enabled, extras off. Advanced stays enabled; other controls unchanged");
        reset.onClick = [this] { owner.resetVoicesToBasic(); };
        addAndMakeVisible(enabled);
        addAndMakeVisible(reset);
        for (size_t v = 0; v < cards.size(); ++v) {
            cards[v] = std::make_unique<VoiceCard>(owner, v);
            addAndMakeVisible(*cards[v]);
        }
        detail::passiveControl(*this);
    }
    void sync() { enabled.setToggleState(owner.settings().advanced, juce::dontSendNotification); }
    void setNeon(bool neon) {
        for (auto* button : {&enabled, &reset}) {
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
            button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
        }
        for (auto& card : cards) card->setNeon(neon);
    }
    void resized() override {
        enabled.setBounds(0, 0, 178, 26);
        reset.setBounds(getWidth() - 132, 0, 132, 26);
        const int width = (getWidth() - 24) / 4;
        for (size_t v = 0; v < cards.size(); ++v)
            cards[v]->setBounds(static_cast<int>(v) * (width + 8), 36, width, getHeight() - 36);
    }
private:
    Processor& owner;
    juce::TextButton enabled, reset;
    std::array<std::unique_ptr<VoiceCard>, 4> cards;
};

class ChoralePanel final : public juce::Component {
public:
    explicit ChoralePanel(Processor& owner)
        : mix(owner, "choraleMix", "Mix", "%"), motion(owner, "choraleMotion", "Motion", "%") {
        setName("Chorale section");
        enabled.setComponentID("choraleOn");
        enabled.setButtonText("CHORALE");
        enabled.setClickingTogglesState(true);
        enabled.setTooltip("Enable the separate Chorale effect (off by default)");
        addAndMakeVisible(enabled);
        addAndMakeVisible(mix);
        addAndMakeVisible(motion);
        link = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, "choraleOn", enabled);
        detail::passiveControl(*this);
    }
    void resized() override {
        enabled.setBounds(12, 16, 112, 26);
        mix.setBounds(148, 12, 328, 34);
        motion.setBounds(500, 12, getWidth() - 512, 34);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(neon ? 0xff111827 : 0xff252826));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
        g.setColour(juce::Colour(neon ? 0xff2b3a53 : 0xff515850));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
    }
    void setNeon(bool value) {
        neon = value;
        enabled.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
        enabled.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
        mix.setNeon(neon);
        motion.setNeon(neon);
        repaint();
    }
private:
    bool neon = false;
    juce::TextButton enabled;
    detail::ParameterSlider mix, motion;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> link;
};
} // namespace vocalstack
