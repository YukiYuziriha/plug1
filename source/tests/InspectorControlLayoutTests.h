#pragma once
#include "UpdateUiTests.h"
#include <iostream>

namespace vocalstack::inspectorControlLayoutTests {
namespace ui = toneUiTests;
constexpr std::array types{NodeType::delay, NodeType::reverb, NodeType::compressor,
    NodeType::bitcrusher, NodeType::granular, NodeType::convolution, NodeType::saturator};

inline void visitRotaries(juce::Component& root, const std::function<void(juce::Slider&)>& visit) {
    if (auto* slider = dynamic_cast<juce::Slider*>(&root); slider && slider->isVisible()
        && slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag) visit(*slider);
    for (auto* child : root.getChildren()) if (child->isVisible()) visitRotaries(*child, visit);
}

inline void run() {
    for (int theme = 0; theme < 3; ++theme) for (auto type : types) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        updateUiTests::install(*owner, type);
        RoutingPanel routing(*owner);
        routing.getProperties().set(vocalstackThemeProperty, theme); routing.setNeon(theme == 1);
        auto* actual = ui::find<CreativeEffectPanel>(routing, [](auto&) { return true; });
        ui::require(actual != nullptr, "Routing inspector hierarchy missing");
        CreativeEffectPanel panel(*owner, 1, creativeKind(type));
        panel.getProperties().set(vocalstackThemeProperty, theme); panel.setNeon(theme == 1);
        panel.setInspectorLayout(true);
        for (int width : {174, 190, 220, 280, 320, 400, 480}) {
            panel.setSize(width, 360);
            auto* viewport = ui::find<juce::Viewport>(panel, [](auto&) { return true; });
            auto* content = viewport->getViewedComponent();
            std::vector<juce::Rectangle<int>> cells;
            for (auto* row : content->getChildren()) if (row->isVisible() && row->getComponentID().endsWith("Control")) {
                ui::require(content->getLocalBounds().contains(row->getBounds()), "Inspector row escaped scroll content");
                for (const auto& previous : cells) ui::require(!previous.intersects(row->getBounds()), "Inspector hitboxes overlap");
                cells.push_back(row->getBounds());
                for (auto* child : row->getChildren()) if (child->isVisible()) {
                    ui::require(row->getLocalBounds().contains(child->getBounds()), "Visible control/caption clips its cell");
                    if (auto* caption = dynamic_cast<juce::Label*>(child)) {
                        juce::AttributedString text; text.append(caption->getText(), caption->getFont());
                        juce::TextLayout layout; layout.createLayout(text, float(caption->getWidth() - 8));
                        ui::require(layout.getHeight() <= float(caption->getHeight()), "Inspector caption clips vertically: " + caption->getText() + " width=" + juce::String(width) + " textHeight=" + juce::String(layout.getHeight()) + " lane=" + juce::String(caption->getHeight()));
                    }
                }
            }
            visitRotaries(panel, [&](auto& slider) {
                auto* row = slider.getParentComponent();
                const auto layout = slider.getLookAndFeel().getSliderLayout(slider);
                ui::require(bool(slider.getProperties()["vocalstackInspectorRotary"]), "Inspector rotary did not opt into bounded layout");
                ui::require(slider.getLocalBounds().contains(layout.textBoxBounds) && slider.getLocalBounds().contains(layout.sliderBounds)
                    && !layout.textBoxBounds.intersects(layout.sliderBounds), "Numeric hitbox overlaps dial or clips slider");
                ui::require(dynamic_cast<juce::Label*>(slider.getComponentAt(layout.textBoxBounds.getCentre())) != nullptr
                    && slider.getComponentAt(layout.sliderBounds.getCentre()) == &slider,
                    "Numeric entry or dial hit target is obstructed");
                ui::require(layout.textBoxBounds.getHeight() == 22 && layout.sliderBounds.getHeight() >= 80,
                    "Inspector lost readable value lane or useful dial size");
                for (auto* child : slider.getChildren()) if (auto* value = dynamic_cast<juce::Label*>(child); value && value->isVisible()) {
                    ui::require(slider.getLocalBounds().contains(value->getBounds()) && !value->getBounds().intersects(layout.sliderBounds),
                        "Actual numeric label clips or overlaps dial hitbox");
                    ui::require(value->getMinimumHorizontalScale() == 1.f, "Inspector value shrinks its pixel font");
                    ui::require(std::abs(value->getFont().getHeight() - (ParameterKnob::controlFontSize + 2.f)) < .001f,
                        "Inspector value did not receive exactly +2 font height");
                    ui::require(value->getFont().getAscent() + value->getFont().getDescent() <= float(value->getHeight()),
                        "Inspector value font clips its actual label lane");
                }
                ui::require(row->getWidth() >= 88 && row->getWidth() <= 110, "Inspector allocated arbitrary oversized/undersized knob cells");
                for (auto* child : row->getChildren()) if (auto* caption = dynamic_cast<juce::Label*>(child); caption && caption->isVisible())
                    ui::require(!caption->getBounds().intersects(slider.getBounds()), "Inspector name and value/dial overlap");
                for (auto value : {slider.getMinimum(), slider.getMaximum(), slider.getValue()}) {
                    const auto text = slider.getTextFromValue(value);
                    const auto font = juce::Font(pixelFontOptions(ParameterKnob::controlFontSize));
                    ui::require(juce::GlyphArrangement::getStringWidth(font, text) <= float(layout.textBoxBounds.getWidth() - 10),
                        "Inspector numeric value too wide: " + slider.getComponentID() + " " + text);
                }
                panel.revealParameter(slider.getComponentID());
                const auto visible = viewport->getLocalArea(row, row->getLocalBounds());
                ui::require(viewport->getLocalBounds().contains(visible), "Inspector knob not fully scroll reachable");
            });
            auto& mix = ui::slider(panel, "fx1Mix");
            ui::require(mix.getSliderStyle() == juce::Slider::LinearHorizontal
                && ParameterKnob::Style::texturedBarBounds(mix.getLookAndFeel().getSliderLayout(mix).sliderBounds.toFloat()).getHeight() == 25,
                "Inspector changed sparse fat slider behavior");
            ui::passive(panel);
        }
        // Production right inspector opts in; ordinary rack/card panels do not.
        visitRotaries(*actual, [&](auto& slider) { ui::require(bool(slider.getProperties()["vocalstackInspectorRotary"]), "Actual inspector not opted in"); });
        CreativeEffectPanel unchanged(*owner, 1, creativeKind(type)); unchanged.setSize(320, 360);
        visitRotaries(unchanged, [&](auto& slider) { ui::require(!bool(slider.getProperties()["vocalstackInspectorRotary"]), "Inspector policy leaked to rack/cards"); });
    }
}

inline void screenshots() {
    for (int theme = 0; theme < 3; ++theme) for (auto type : types) for (bool wide : {false, true}) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        updateUiTests::install(*owner, type);
        auto layout = owner->getRoutingLayout();
        layout.setProperty("inspectorWidth", wide ? 480 : 320, nullptr);
        layout.setProperty("inspectorVisible", true, nullptr); owner->setRoutingLayout(layout);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->setSize(wide ? 1500 : 904, wide ? 1000 : 600);
        editor->addToDesktop(0); editor->setVisible(true); ui::action(*editor, "Routing");
        editor->getProperties().set(vocalstackAnimationTimeOverrideProperty, 33.3);
        juce::Timer::callPendingTimersSynchronously();
        auto* panel = ui::find<CreativeEffectPanel>(*editor, [](auto& c) { return c.isShowing(); });
        ui::require(panel != nullptr, "Actual right inspector missing");
        auto* viewport = ui::find<juce::Viewport>(*panel, [](auto&) { return true; });
        juce::Slider* first = nullptr;
        visitRotaries(*panel, [&](auto& s) { if (!first) first = &s; });
        ui::require(first != nullptr, "Dense inspector fixture has no rotary");
        viewport->setViewPosition(0, juce::jmax(0, first->getParentComponent()->getY() - 22));
        const auto stem = CreativeEffectPanel::effectName(creativeKind(type)).toLowerCase() + "-"
            + updateUiTests::themeName(theme) + (wide ? "-wide" : "-narrow");
        ui::screenshot(*editor, stem + "-full.png");
        ui::screenshot(*panel, stem + "-inspector.png");
        const auto bounds = first->getLookAndFeel().getSliderLayout(*first).sliderBounds;
        std::cout << stem << ",panel=" << panel->getWidth() << ",cell="
            << first->getParentComponent()->getWidth() << "x" << first->getParentComponent()->getHeight()
            << ",dialLane=" << bounds.getWidth() << "x" << bounds.getHeight() << '\n';
    }
}
} // namespace vocalstack::inspectorControlLayoutTests
