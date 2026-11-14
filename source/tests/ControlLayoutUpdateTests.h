#pragma once

#include "PluginEditor.h"
#include "SpectralMorphVisual.h"
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

// Include at translation-unit scope. Invoke on the JUCE message thread after
// ScopedJuceInitialiser_GUI; no native window, audio callback or test-runner globals.
namespace vocalstack::controlLayoutUpdateTests {
inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
inline juce::Component* find(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren()) if (auto* found = find(*child, id)) return found;
    return nullptr;
}
inline void crossfadePresentation() {
    juce::Slider slider;
    slider.setRange(0, 100, .01);
    for (bool stack : {false, true}) {
        configureCrossfadeDisplay(slider, stack);
        require(hasMidpointCrossfade(slider), "Crossfade missing midpoint presentation");
        require(slider.getMinimum() == 0 && slider.getMaximum() == 100, "Presentation changed mix range");
        for (double value : {0., 25., 50., 75., 100.}) {
            const auto text = slider.textFromValueFunction(value);
            require(text.contains(stack ? "% Stack" : "% Wet"), "Crossfade value lost semantic unit");
            require(slider.valueFromTextFunction(text) == value, "Crossfade value does not round trip");
            slider.setValue(value, juce::dontSendNotification);
            require(std::abs(slider.valueToProportionOfLength(value) - value / 100.) < 1.e-6,
                    "Crossfade presentation introduced a nonlinear mapping");
        }
    }
    require(slider.valueFromTextFunction("Dry50/Wet50") == 50, "Explicit Dry/Wet text misparsed");
    require(slider.valueFromTextFunction("-20") == 0 && slider.valueFromTextFunction("120") == 100,
            "Crossfade permits negative mix or values above wet endpoint");
}
inline void outputAndRingGeometry() {
    auto owner = std::make_unique<Processor>();
    ParameterKnob output(*owner, "outputGain", "Output trim", "dB");
    output.setSize(240, 64);
    require(output.slider.getSliderStyle() == juce::Slider::LinearHorizontal,
            "Output node trim regressed to a knob");
    require(output.slider.getMinimum() == -24 && output.slider.getMaximum() == 12,
            "Global output range changed (Gain FX +24 is a different parameter)");
    const auto layout = output.slider.getLookAndFeel().getSliderLayout(output.slider);
    const auto track = ParameterKnob::Style::texturedBarBounds(layout.sliderBounds.toFloat());
    require(track.getHeight() == 25 && track.getY() >= 8, "Output lost thick bar or modulation lane");
    for (float radius : {20.f, 36.f, 48.f}) {
        const float outer = radius + 2.4f;
        const float width = ParameterKnob::Style::rotaryRingWidth(outer);
        require(width > 4.8f, "Progress ring did not thicken");
        require(std::abs((outer - width * .5f) + width * .5f - (radius + 2.4f)) < .0001f,
                "Progress ring grew outward");
    }
}
inline void morphResponsiveLayout() {
    auto owner = std::make_unique<Processor>();
    SpectralMorphPanel panel(*owner, 1);
    for (int theme = 0; theme < 3; ++theme) {
        panel.getProperties().set(vocalstackThemeProperty, theme);
        panel.setNeon(theme == 1);
        for (bool inspector : {false, true}) {
            panel.setInspectorLayout(inspector);
            for (int width : {190, 220, 320, 480, 679, 680, 840, 896, 1200}) {
                panel.setSize(width, 360);
                const bool wide = !inspector && width >= 680;
                require(panel.usesSideBySideLayout() == wide, "Morph breakpoint/inspector fallback changed");
                auto* viewport = dynamic_cast<juce::Viewport*>(find(panel, "spectralMorphControlsViewport"));
                require(viewport && viewport->getViewedComponent(), "Morph controls lost viewport");
                auto* content = viewport->getViewedComponent();
                const auto plot = panel.spectrumBounds();
                require(!plot.isEmpty() && panel.getLocalBounds().contains(plot), "Morph plot is clipped or empty");
                require(!plot.intersects(viewport->getBounds()), "Morph plot overlaps controls");
                require(wide ? plot.getRight() < viewport->getX() : plot.getBottom() < viewport->getY(),
                        "Morph must place controls right when wide, below when narrow");
                std::vector<juce::Rectangle<int>> cells;
                for (auto* child : content->getChildren()) if (auto* knob = dynamic_cast<ParameterKnob*>(child)) {
                    require(content->getLocalBounds().contains(knob->getBounds()), "Morph control outside scroll content");
                    for (auto previous : cells) require(!previous.intersects(knob->getBounds()), "Morph cells overlap");
                    cells.push_back(knob->getBounds());
                    const auto lanes = knob->slider.getLookAndFeel().getSliderLayout(knob->slider);
                    require(knob->getHeight() == 138 && lanes.sliderBounds.getHeight() >= 90,
                            "Morph lost existing large knob/value/caption lanes");
                    require(!lanes.sliderBounds.intersects(lanes.textBoxBounds), "Morph value overlaps rotary");
                    require(!bool(knob->slider.getProperties()["vocalstackSquarePrototype"]), "Square prototype deployed");
                    viewport->setViewPosition(0, knob->getY());
                    require(viewport->getLocalBounds().contains(viewport->getLocalArea(knob, knob->getLocalBounds())),
                            "Morph knob not fully scroll reachable");
                }
                require(cells.size() == 4, "Morph parameter missing");
            }
        }
    }
}
inline void editorControls() {
    auto owner = std::make_unique<Processor>();
    std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
    require(editor->getWidth() == editorLayout::editorWidth
        && editor->getHeight() == editorLayout::defaultWindowHeight, "Fresh default window changed");
    require(MainBusFxRackPanel::preferredHeight == 388, "Stack FX allocation changed");
    for (const auto* id : {"outputGain", "double", "globalMix"}) {
        auto* slider = dynamic_cast<juce::Slider*>(find(*editor, id));
        require(slider && slider->getSliderStyle() == juce::Slider::LinearHorizontal, "Stack control is not horizontal");
        const auto lanes = slider->getLookAndFeel().getSliderLayout(*slider);
        require(!lanes.textBoxBounds.intersects(lanes.sliderBounds), "Compact Stack value overlaps track");
        for (auto value : {slider->getMinimum(), 50. <= slider->getMaximum() ? 50. : 0., slider->getMaximum()}) {
            const auto text = slider->getTextFromValue(value);
            for (auto* child : slider->getChildren()) if (auto* label = dynamic_cast<juce::Label*>(child))
                require(juce::GlyphArrangement::getStringWidth(label->getFont(), text)
                    <= float(lanes.textBoxBounds.getWidth() - 4), "Stack value glyphs do not fit reserved lane");
        }
    }
    auto* lead = dynamic_cast<juce::Slider*>(find(*editor, "double"));
    require(lead->textFromValueFunction(50) == "50% Stack", "Stack lost Lead/Stack-specific language");
}
inline void run() {
    crossfadePresentation();
    outputAndRingGeometry();
    morphResponsiveLayout();
    editorControls();
}
} // namespace vocalstack::controlLayoutUpdateTests
