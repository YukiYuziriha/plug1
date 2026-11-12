#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "VisualTheme.h"
#include "MeterPanel.h"
#include "DoublerPanel.h"
#include "RoutingPanel.h"
#include "MainBusFxRackPanel.h"
#include "PresetManager.h"

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
        juce::Label* createSliderTextBox(juce::Slider& slider) override {
            return createPassiveSliderTextBox(juce::LookAndFeel_V4::createSliderTextBox(slider));
        }
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        juce::Font getTextButtonFont(juce::TextButton&, int) override;
    };

    void timerCallback() override;
    void applyTheme();
    void updateBypassControls(float bypassValue);
    void syncDrawer();
    void openRouting();
    void drawDevice(juce::Graphics&);
    void drawFlash(juce::Graphics&);
        void drawSpectral(juce::Graphics&);
    void rememberWindow();
    void revealParameter(const juce::String&);
    bool keyPressed(const juce::KeyPress&) override;

    Processor& owner;
    PresetBar presets;
    Style style;
    class LayoutCanvas final : public juce::Component {
    public:
        std::function<void(juce::Graphics&)> draw;
        void paint(juce::Graphics& g) override { if (draw) draw(g); }
    };
    LayoutCanvas layoutCanvas;
    PassiveViewport stackViewport;

    bool restoringWindow = true;
    int selectedVoiceSlot = -1;
    std::array<ModulationSlider, knobParameterIds.size()> knobs;
    ParameterKnob globalMix;
    juce::Label assignmentBanner;
    juce::TextButton endAssignment;
    std::array<juce::Label, knobParameterIds.size()> labels;
    std::array<modulationui::Button, switchParameterIds.size()> switches;
    juce::TextButton theme, reset, copy, paste, stackTab, routingTab;
    RoutingPanel routingPanel;
    bool routingVisible = false;
    bool routingOpened = false;
    DoublerPanel advancedPanel;
    MainBusFxRackPanel mainBusFxRack;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, knobParameterIds.size()> knobLinks;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, switchParameterIds.size()> switchLinks;
    std::unique_ptr<juce::ParameterAttachment> bypassUiLink;
    juce::TooltipWindow tips{this, 530};
    juce::BubbleMessageComponent revealBubble;
    MeterPanel meterPanel;
    int feedbackTicks = 0;
};

} // namespace vocalstack
