#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "MeterPanel.h"
#include "DoublerPanel.h"

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
    void syncDrawer();
    void drawDevice(juce::Graphics&);
    void drawFlash(juce::Graphics&);

    Processor& owner;
    Style style;
    std::array<juce::Slider, knobParameterIds.size()> knobs;
    std::array<juce::Label, knobParameterIds.size()> labels;
    std::array<juce::TextButton, switchParameterIds.size()> switches;
    juce::TextButton theme, reset, copy, paste, drawer;
    DoublerPanel advancedPanel;
    ChoralePanel choralePanel;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, knobParameterIds.size()> knobLinks;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, switchParameterIds.size()> switchLinks;
    juce::TooltipWindow tips{this, 530};
    MeterPanel meterPanel;
    int feedbackTicks = 0;
};

} // namespace vocalstack
