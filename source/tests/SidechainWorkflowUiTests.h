#pragma once
#include "RoutingPanel.h"
#include "GuiTestDisplay.h"
#include <stdexcept>
namespace vocalstack::workflowUiTests {
inline void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
inline juce::Component* find(juce::Component& component, const juce::String& id) {
    if (component.getComponentID() == id) return &component;
    for (auto* child : component.getChildren()) if (auto* result = find(*child, id)) return result;
    return nullptr;
}
inline juce::MouseEvent mouse(juce::Component& target, juce::Point<float> point, juce::Point<float> down, bool dragged = false) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
        &target, &target, now, down, now, 1, dragged};
}
inline void run() {
    guiTestDisplay::enumerate();
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        auto graph = mainBusFx::add(defaultGraph(), NodeType::granular).graph;
        juce::String error;
        require(owner->setRoutingGraph(graph, error), "Granular UI graph rejected");
        auto layout = owner->getRoutingLayout();
        layout.removeAllChildren(nullptr); layout.removeProperty("selectedVoiceSlot", nullptr);
        layout.setProperty("selectedNode", 100, nullptr); layout.setProperty("zoom", .8f, nullptr);
        layout.setProperty("inspectorVisible", true, nullptr);
        juce::ValueTree position("NODE"); position.setProperty("id", 100, nullptr);
        position.setProperty("x", 40, nullptr); position.setProperty("y", 40, nullptr);
        position.setProperty("expanded", true, nullptr); layout.addChild(position, -1, nullptr);
        owner->setRoutingLayout(layout);
        RoutingPanel panel(*owner); panel.setSize(1200, 950); panel.setNeon(theme == 1);
        guiTestDisplay::attach(panel);
        auto* pocket = dynamic_cast<juce::Button*>(find(panel, "granularWetPocket"));
        require(pocket && pocket->isShowing(), "Granular node has no visible wet-pocket action");
        pocket->onClick();
        const auto updated = owner->getRoutingGraph();
        const auto* child = mainBusFx::find(updated, 101);
        require(child && child->type == NodeType::delay && child->wetOwner == 100, "Node action did not create owned wet Delay");
        auto* delayMix = dynamic_cast<juce::Slider*>(find(panel, "fx2Mix"));
        require(delayMix && delayMix->isShowing(), "Pocket action did not reveal Delay controls");
        require(delayMix->getValue() == 100, "New wet insert must default fully processed");
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile("source/build/wet-pocket-" + juce::String(theme) + ".png");
        if (auto stream = file.createOutputStream()) {
            stream->setPosition(0); stream->truncate();
            juce::PNGImageFormat{}.writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds()), *stream);
        }
        ParameterKnob mix(*owner, "fx1Mix", "Mix", "%"); mix.setLinear(true); mix.setSize(320, 64);
        auto& slider = mix.slider; slider.setValue(25, juce::dontSendNotification);
        const auto lanes = slider.getLookAndFeel().getSliderLayout(slider);
        const auto from = lanes.sliderBounds.toFloat().getCentre();
        const auto to = from.translated(60, 0);
        const auto originalSensitivity = slider.getMouseDragSensitivity();
        slider.mouseDown(mouse(slider, from, from));
        require(slider.getMouseDragSensitivity() == lanes.sliderBounds.getWidth(), "Fat Mix rail does not use 1:1 pointer travel");
        slider.mouseDrag(mouse(slider, to, from, true));
        const double expected = .25 + 60. / lanes.sliderBounds.getWidth();
        require(std::abs(slider.valueToProportionOfLength(slider.getValue()) - expected) < .015, "Fat Mix pointer and value movement differ");
        slider.mouseUp(mouse(slider, to, from, true));
        require(slider.getMouseDragSensitivity() == originalSensitivity, "Drag sensitivity leaked after gesture");
    }
    std::cout << "Node wet-pocket creation/navigation and 1:1 fat Mix drag passed in three themes\n";
}
}
