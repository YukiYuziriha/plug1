#pragma once

#include "ToneUiTests.h"
#include "HoverWheelRegressionTests.h"

// Register run() in the editor test runner, after ScopedJuceInitialiser_GUI.
namespace vocalstack::toneCardLayoutTests {
namespace ui = toneUiTests;

inline juce::Viewport& viewport(ToneEffectPanel& panel) {
    auto* result = ui::find<juce::Viewport>(panel, [](auto&) { return true; });
    ui::require(result != nullptr, "Tone card has no internal viewport");
    return *result;
}

inline void checkContent(ToneEffectPanel& panel) {
    auto& view = viewport(panel);
    auto* content = view.getViewedComponent();
    ui::require(content != nullptr, "Tone card has no content");
    ui::require(content->getWidth() <= view.getViewArea().getWidth(), "Tone card needs horizontal scrolling");
    const bool wide = panel.getWidth() >= 620;
    const int inner = content->getWidth() - 16;
    const int controlWidth = wide ? inner - inner * 44 / 100 - 12 : inner;
    const int controlX = wide ? 8 + inner * 44 / 100 + 12 : 8;
    std::vector<juce::Component*> visible;
    for (auto* child : content->getChildren()) {
        if (!child->isVisible()) continue;
        ui::require(!child->getBounds().isEmpty() && content->getLocalBounds().contains(child->getBounds()),
                    "Tone card clips content: " + child->getName());
        for (auto* previous : visible)
            ui::require(!previous->getBounds().intersects(child->getBounds()), "Tone card controls overlap");
        visible.push_back(child);
        const auto bounds = child->getBounds();
        const int viewHeight = view.getViewArea().getHeight();
        ui::require(viewHeight > 0, "Tone card viewport has no usable height");
        // A hint or response plot can be taller than the viewport. Every slice
        // must be reachable, not simultaneously visible. Parameter controls still
        // have to fit in one view so their labels and values can be used together.
        if (dynamic_cast<toneui::Control*>(child) || dynamic_cast<juce::ComboBox*>(child)
            || dynamic_cast<juce::Button*>(child))
            ui::require(bounds.getHeight() <= viewHeight,
                        "Tone parameter control is taller than its viewport: " + ui::componentPath(*child));
        for (int y = bounds.getY(); y < bounds.getBottom(); y += viewHeight) {
            const auto slice = bounds.withY(y).withHeight(juce::jmin(viewHeight, bounds.getBottom() - y));
            view.setViewPosition(0, y);
            ui::require(view.getViewArea().contains(slice),
                        "Tone card content slice cannot be reached: " + ui::componentPath(*child)
                            + " child=" + bounds.toString() + " slice=" + slice.toString()
                            + " view=" + view.getViewArea().toString()
                            + " content=" + content->getLocalBounds().toString()
                            + " panel=" + panel.getLocalBounds().toString());
        }
        if (auto* button = dynamic_cast<juce::Button*>(child); button && button->getClickingTogglesState())
            ui::require(button->getWidth() >= 48 && button->getWidth() <= 64,
                        "Two-state control expanded into a full row");
        if (auto* choice = dynamic_cast<juce::ComboBox*>(child))
            ui::require(choice->getWidth() >= 140, "Compact mode is not a readable full-width row");
        if (auto* knob = dynamic_cast<toneui::Control*>(child)) {
            ui::require(knob->getX() >= controlX, "Wide Tone control escaped its right-hand group");
            const bool linear = knob->slider.getSliderStyle() == juce::Slider::LinearHorizontal;
            if (linear) {
                ui::require(knob->getWidth() == controlWidth && knob->getHeight() == 62,
                            "Mix must be a standalone horizontal slider row");
            } else {
                ui::require(knob->getWidth() == 78 && knob->getHeight() == 100,
                            "Responsive knob must retain its compact fixed size");
            }
            bool valueFound = false;
            for (auto* part : knob->slider.getChildren()) {
                if (auto* label = dynamic_cast<juce::Label*>(part); label && label->isVisible()) {
                    valueFound = true;
                    ui::require(label->getWidth() >= 64 && label->getHeight() >= 18,
                                "Compact knob value is unreadable");
                    ui::require(knob->slider.getLocalBounds().contains(label->getBounds()), "Knob clips its value");
                }
            }
            ui::require(valueFound, "Compact knob lost its value label");
        }
    }
    int knobCount = 0, firstY = content->getHeight(), lastY = 0;
    for (auto* child : visible) {
        auto* knob = dynamic_cast<toneui::Control*>(child);
        if (!knob || knob->slider.getSliderStyle() == juce::Slider::LinearHorizontal) continue;
        ++knobCount;
        firstY = juce::jmin(firstY, knob->getY());
        lastY = juce::jmax(lastY, knob->getY());
    }
    const int columns = juce::jmax(1, (controlWidth + 4) / 82);
    if (knobCount > 0)
        ui::require(lastY - firstY == ((knobCount - 1) / columns) * 104,
                    "Tone knobs did not rewrap to use the available inspector width");
    ui::passive(panel);
}

inline void narrowLayouts() {
    auto owner = std::make_unique<Processor>();
    for (auto theme : {Theme::device, Theme::neon, Theme::spectral}) {
        for (int width : {190, 204, 220, 280, 320, 400, 560, 619, 620, 640, 800, 860}) {
            for (int kind : {6, 7, 8}) {
                ToneEffectPanel panel(*owner, 1, static_cast<NodeType>(kind));
                panel.getProperties().set(vocalstackThemeProperty, int(theme));
                panel.setNeon(theme == Theme::neon);
                panel.setSize(width, 280);
                checkContent(panel);
                if (width >= 620) {
                    auto* plot = ui::find<toneui::FxMonitorView>(panel, [](auto&) { return true; });
                    auto* eq = ui::find<toneui::EqGraph>(panel, [](auto&) { return true; });
                    auto* left = eq ? static_cast<juce::Component*>(eq) : static_cast<juce::Component*>(plot);
                    ui::require(left != nullptr && left->getX() == 8
                                && left->getWidth() < viewport(panel).getViewedComponent()->getWidth() / 2,
                                "Wide Tone plot still spans a full-width column");
                    if (width == 860)
                        ui::require(viewport(panel).getViewedComponent()->getHeight() <= 280,
                                    "860px Tone inspector wastes its short wide layout on vertical scrolling");
                }
                viewport(panel).setViewPosition(0, 0);
                const auto image = ui::render(panel);
                ui::require(image.isValid(), "Compact Tone card failed to render");
                if (kind == 7) {
                    for (int band = 1; band <= 8; ++band) {
                        ui::action(panel, juce::String(band));
                        ui::slider(panel, eqBandParameterId(1, size_t(band - 1), "Frequency"));
                        ui::slider(panel, eqBandParameterId(1, size_t(band - 1), "Gain"));
                        ui::slider(panel, eqBandParameterId(1, size_t(band - 1), "Q"));
                        checkContent(panel);
                    }
                }
                const auto snapshotName = "tone-card-" + juce::String(kind) + "-" + juce::String(int(theme));
                if (width == 204) {
                    viewport(panel).setViewPosition(0, 0);
                    ui::screenshot(panel, snapshotName + "-top.png");
                    auto* firstKnob = ui::find<toneui::Control>(panel, [](auto&) { return true; });
                    ui::require(firstKnob != nullptr, "Tone card has no parameter controls");
                    viewport(panel).setViewPosition(0, firstKnob->getY());
                    ui::screenshot(panel, snapshotName + "-controls.png");
                }
                panel.setSize(width, panel.preferredContentHeight(width));
                checkContent(panel);
                if (width == 204) ui::screenshot(panel, snapshotName + "-full.png");
                ui::require(viewport(panel).getViewedComponent()->getHeight() <= viewport(panel).getViewArea().getHeight(),
                            "Full-height Tone card still has internal vertical overflow");
                // Broad plots exceed a short viewport too; do not regress the
                // original inspector geometry just to satisfy a card assertion.
                panel.setSize(400, 240);
                checkContent(panel);
                panel.setSize(400, ToneEffectPanel::preferredHeight);
                checkContent(panel);
                ui::require(panel.preferredContentHeight(400) >= ToneEffectPanel::preferredHeight,
                            "Broad inspector lost its useful plot/control height");
                auto& view = viewport(panel);
                view.setViewPosition(0, 120);
                const int oldY = view.getViewPositionY();
                panel.setSize(860, 240);
                ui::require(view.getViewPositionY() == juce::jmin(oldY,
                                view.getViewedComponent()->getHeight() - view.getViewArea().getHeight()),
                            "Widening Tone inspector reset its scroll position");
                checkContent(panel);
            }
        }
    }
}

inline void wheel(toneui::EqGraph& graph, juce::Point<float> point, int modifiers = 0) {
    juce::MouseWheelDetails details{};
    details.deltaY = -.3f;
    graph.mouseWheelMove(ui::mouse(graph, point, point, false, modifiers), details);
}

inline void passiveGraphWheel() {
    auto owner = std::make_unique<Processor>();
    ui::write(*owner, eqBandParameterId(1, 0, "On"), 1);
    ui::write(*owner, eqBandParameterId(1, 0, "Frequency"), 1000);
    ui::write(*owner, eqBandParameterId(1, 0, "Gain"), 0);
    ToneEffectPanel panel(*owner, 1, static_cast<NodeType>(7));
    panel.setSize(204, 240);
    auto* graph = ui::find<toneui::EqGraph>(panel, [](auto&) { return true; });
    ui::require(graph != nullptr, "Missing compact EQ graph");
    const auto handle = ui::eqPoint(*graph, 1000, 0);
    auto& inner = viewport(panel);
    const auto focus = juce::Component::getCurrentlyFocusedComponent();
    {
        hoverWheelRegressionTests::ParameterChanges changes(*owner);
        for (auto point : {handle, juce::Point<float>(8, 8)}) {
            inner.setViewPosition(0, 0);
            wheel(*graph, point);
            ui::require(inner.getViewPositionY() > 0, "Graph hover wheel failed to scroll internal viewport");
        }
        inner.setViewPosition(0, 100000);
        const auto edge = inner.getViewPositionY();
        wheel(*graph, handle);
        ui::require(inner.getViewPositionY() == edge, "Graph wheel moved past bottom edge");
        changes.check();
    }
    inner.setViewPosition(0, 0);
    const auto qId = eqBandParameterId(1, 0, "Q");
    const auto beforeQ = ui::read(*owner, qId);
    {
        ui::GestureLog gestures(*owner);
        wheel(*graph, handle, juce::ModifierKeys::altModifier);
        ui::require(ui::read(*owner, qId) != beforeQ && inner.getViewPositionY() == 0,
                    "Explicit Alt-wheel must edit Q without scrolling");
        ui::require(gestures.count(ui::parameter(*owner, qId)).begins == 1,
                    "Explicit Q wheel lost its host gesture");
        gestures.balanced("compact Alt-wheel Q");
    }

    // Model a rack body containing Mix + a full-height Tone panel. The panel's
    // viewport has no range, so navigation goes to the outer body, not Q.
    juce::Component body;
    juce::Viewport outer;
    outer.setBounds(0, 0, 220, 240);
    outer.setScrollBarsShown(true, false);
    body.setSize(204, panel.preferredContentHeight(204) + 104);
    body.addAndMakeVisible(panel);
    panel.setBounds(0, 104, 204, panel.preferredContentHeight(204));
    outer.setViewedComponent(&body, false);
    {
        hoverWheelRegressionTests::ParameterChanges changes(*owner);
        wheel(*graph, handle);
        ui::require(outer.getViewPositionY() > 0 && inner.getViewPositionY() == 0,
                    "Full-height graph failed to forward to rack body viewport");
        panel.setSize(204, 240);
        outer.setViewPosition(0, 0);
        inner.setViewPosition(0, 100000);
        wheel(*graph, handle);
        ui::require(outer.getViewPositionY() > 0, "Nested graph wheel did not chain at inner bottom");
        changes.check();
    }
    outer.setViewedComponent(nullptr, false);
    ui::require(juce::Component::getCurrentlyFocusedComponent() == focus, "Graph wheel stole host focus");

    toneui::EqGraph standalone(*owner, 1);
    standalone.setSize(204, 200);
    const auto standaloneQ = ui::read(*owner, qId);
    wheel(standalone, ui::eqPoint(standalone, 1000, 0));
    ui::require(ui::read(*owner, qId) != standaloneQ, "Standalone graph lost plain-wheel Q editing");
}

inline void run() {
    narrowLayouts();
    passiveGraphWheel();
}
} // namespace vocalstack::toneCardLayoutTests
