#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace vocalstack {

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Style final : public juce::LookAndFeel_V4 {
    public:
        bool flash = false;
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        juce::Font getTextButtonFont(juce::TextButton&, int) override;
    };

    void timerCallback() override;
    void applyTheme();
    void drawMeters(juce::Graphics&);
    void drawDevice(juce::Graphics&);
    void drawFlash(juce::Graphics&);

    Processor& owner;
    Style style;
    std::array<juce::Slider, 5> knobs;
    std::array<juce::Label, 5> labels;
    std::array<juce::TextButton, 4> switches;
    juce::TextButton theme, reset, copy, paste;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> knobLinks;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 4> switchLinks;
    juce::TooltipWindow tips{this, 530};
    Meters meters{};
    int feedbackTicks = 0;
};

} // namespace vocalstack
