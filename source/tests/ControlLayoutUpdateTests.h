#pragma once

#include "PluginEditor.h"
#include "SpectralMorphVisual.h"
#include "GuiTestDisplay.h"
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

// Include at translation-unit scope. Invoke on the JUCE message thread after
// ScopedJuceInitialiser_GUI. Routing card geometry uses a native fixture on the
// selected secondary display because offscreen cards are intentionally virtualized.
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
        require(!hasMidpointCrossfade(slider), "Mix must fill from dry to wet, not from its midpoint");
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
    require(output.slider.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag,
            "Output trim must remain a knob");
    require(output.slider.getMinimum() == -24 && output.slider.getMaximum() == 12,
            "Global output range changed (Gain FX +24 is a different parameter)");
    ParameterKnob mix(*owner, "double", "Lead / Stack", "%");
    require(!hasMidpointCrossfade(mix.slider), "Lead / Stack must not use midpoint styling");
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
                    require(knob->getHeight() >= 142 && lanes.sliderBounds.getHeight() >= 90,
                            "Morph must retain a 90px dial after enlarged value/caption lanes");
                    require(lanes.textBoxBounds.getHeight() == 22
                        && knob->getHeight() == lanes.sliderBounds.getHeight() + lanes.textBoxBounds.getHeight()
                            + knob->captionHeightForWidth(knob->getWidth()),
                        "Morph cell height must account for its actual caption and value lanes");
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
    auto* rack = dynamic_cast<MainBusFxRackPanel*>(find(*editor, "mainBusFxRack"));
    require(rack && rack->getY() == editorLayout::stackRackY
        && rack->getHeight() == rack->preferredHeightForWidth(rack->getWidth()),
        "Stack FX rack did not use its width-dependent allocation");
    for (const auto* id : {"outputGain", "double", "globalMix"}) {
        auto* slider = dynamic_cast<juce::Slider*>(find(*editor, id));
        const auto expected = juce::String(id) == "globalMix" ? juce::Slider::LinearHorizontal
            : juce::Slider::RotaryHorizontalVerticalDrag;
        require(slider && slider->getSliderStyle() == expected, "Stack control has incorrect knob/slider style");
        require(!hasMidpointCrossfade(*slider), "Stack Mix must not use bipolar presentation");
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
inline void routingOutputGeometry() {
    guiTestDisplay::enumerate();
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>();
        owner->setSkin(theme);
        juce::String error;
        require(owner->setRoutingGraph(Graph{{{1, NodeType::lead}, {13, NodeType::output}}, {{1, 13, 0, 0}}}, error), "Output fixture graph rejected");
        auto layout = owner->getRoutingLayout();
        layout.setProperty("selectedNode", 13, nullptr);
        layout.removeProperty("selectedVoiceSlot", nullptr);
        layout.setProperty("inspectorVisible", true, nullptr);
        layout.setProperty("zoom", 1.0f, nullptr);
        layout.removeAllChildren(nullptr);
        juce::ValueTree node("NODE");
        node.setProperty("id", 13, nullptr);
        node.setProperty("x", 100, nullptr);
        node.setProperty("y", 100, nullptr);
        node.setProperty("expanded", true, nullptr);
        layout.addChild(node, -1, nullptr);
        owner->setRoutingLayout(layout);
        RoutingPanel routing(*owner);
        routing.setSize(1200, 800);
        guiTestDisplay::attach(routing);
        int count = 0;
        const auto inspect = [&](auto&& self, juce::Component& component) -> void {
            if (auto* slider = dynamic_cast<juce::Slider*>(&component);
                slider && slider->getComponentID() == "outputGain") {
                ++count;
                const auto lanes = slider->getLookAndFeel().getSliderLayout(*slider);
                require(slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag,
                        "Routing Output must be rotary");
                require(lanes.sliderBounds.getWidth() >= 80 && lanes.sliderBounds.getHeight() >= 80,
                        "Routing Output squeezed into an old slider-sized layout");
                require(!lanes.sliderBounds.intersects(lanes.textBoxBounds), "Output value overlaps dial");
            }
            for (auto* child : component.getChildren()) self(self, *child);
        };
        inspect(inspect, routing);
        require(count == 2, "Must check Output in both expanded card and inspector");
    }
}
inline void run() {
    crossfadePresentation();
    routingOutputGeometry();
    outputAndRingGeometry();
    morphResponsiveLayout();
    editorControls();
}
} // namespace vocalstack::controlLayoutUpdateTests
