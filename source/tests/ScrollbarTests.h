#pragma once
#include "InspectorControlLayoutTests.h"
#include "GuiTestDisplay.h"

namespace vocalstack::scrollbarTests {
namespace ui = toneUiTests;

inline void visit(juce::Component& root, const std::function<void(juce::Viewport&)>& fn) {
    if (auto* viewport = dynamic_cast<juce::Viewport*>(&root)) fn(*viewport);
    for (auto* child : root.getChildren()) visit(*child, fn);
}

// Native production editors are placed before interaction. PNGs use JUCE's
// component renderer (not a physical desktop framebuffer capture).
inline void screenshots() {
    guiTestDisplay::enumerate();
    std::cout << "Native fixture; PNGs are offscreen component renders, not physical framebuffer captures\n";
    for (int theme = 0; theme < 3; ++theme) {
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        updateUiTests::install(*owner, NodeType::convolution);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->setSize(940, 760); guiTestDisplay::attach(*editor);
        editor->getProperties().set(vocalstackAnimationTimeOverrideProperty, 33.3);
        ui::action(*editor, "Routing");
        auto* panel = ui::find<CreativeEffectPanel>(*editor, [](auto& c) { return c.isShowing(); });
        ui::require(panel != nullptr, "Dense inspector missing");
        auto* viewport = ui::find<juce::Viewport>(*panel, [](auto&) { return true; });
        viewport->setViewPosition(0, 280);
        const auto stem = updateUiTests::themeName(theme);
        ui::screenshot(*editor, stem + "-routing.png");
        ui::screenshot(*panel, stem + "-dense-inspector.png");
        std::cout << stem << ": inspector track=" << viewport->getScrollBarThickness() << '\n';
        ui::action(*editor, "Stack");
        for (int i = 0; i < 6; ++i) ui::action(*editor, "+ Add voice");
        auto* outer = ui::find<juce::Viewport>(*editor, [](auto& v) { return v.getComponentID() == "stackViewport"; });
        outer->setViewPosition(0, 100000);
        ui::screenshot(*editor, stem + "-fullstack.png");
        auto* stack = ui::find<DoublerPanel>(*editor, [](auto&) { return true; });
        ui::screenshot(*stack, stem + "-voice-strip.png");
        visit(*stack, [&](auto& v) { std::cout << stem << ": voice track=" << v.getScrollBarThickness() << '\n'; });
    }
}

inline void checkViewport(juce::Viewport& viewport) {
    auto* content = viewport.getViewedComponent();
    if (!content || viewport.getWidth() <= 0 || viewport.getHeight() <= 0) return;
    auto& vertical = viewport.getVerticalScrollBar();
    auto& horizontal = viewport.getHorizontalScrollBar();
    if (vertical.isVisible()) {
        ui::require(vertical.getWidth() == viewport.getScrollBarThickness(), "Vertical track thickness mismatch");
        ui::require(viewport.getLocalBounds().contains(vertical.getBounds()), "Vertical track escaped viewport");
    }
    if (horizontal.isVisible()) {
        ui::require(horizontal.getHeight() == viewport.getScrollBarThickness(), "Horizontal track thickness mismatch");
        ui::require(viewport.getLocalBounds().contains(horizontal.getBounds()), "Horizontal track escaped viewport");
    }
    if (vertical.isVisible() && horizontal.isVisible())
        ui::require(!vertical.getBounds().intersects(horizontal.getBounds()), "Scrollbar corner overlap");
    const auto visible = viewport.getLocalArea(content, viewport.getViewArea());
    if (vertical.isVisible()) ui::require(!visible.intersects(vertical.getBounds()), "Vertical track overlays content");
    if (horizontal.isVisible()) ui::require(!visible.intersects(horizontal.getBounds()), "Horizontal track overlays content");
    const auto before = viewport.getViewPosition();
    viewport.setViewPosition(100000, 100000);
    const auto area = viewport.getViewArea();
    if (vertical.isVisible()) ui::require(area.getBottom() == content->getHeight(), "Content bottom unreachable");
    if (horizontal.isVisible()) ui::require(area.getRight() == content->getWidth(), "Content right unreachable");
    viewport.setViewPosition(before);
}

inline void run() {
    guiTestDisplay::enumerate();
    std::cout << "Nested viewport and standalone dense-inspector checks: OFFSCREEN, no physical monitor\n";
    ui::require(juce::LookAndFeel_V4().getDefaultScrollbarWidth() == 8, "Pinned JUCE V4 baseline changed");
    PixelLookAndFeel style;
    ui::require(style.getDefaultScrollbarWidth() == 14, "Default scrollbar is not round(8 * 1.8)");
    for (int theme = 0; theme < 3; ++theme) {
        juce::Component root, content, nestedContent;
        PassiveViewport outer, inner;
        root.setLookAndFeel(&style); root.getProperties().set(vocalstackThemeProperty, theme);
        root.addAndMakeVisible(outer); content.addAndMakeVisible(inner);
        outer.setViewedComponent(&content, false); inner.setViewedComponent(&nestedContent, false);
        outer.setScrollBarsShown(true, true); inner.setScrollBarsShown(true, true);
        content.setSize(600, 700); nestedContent.setSize(400, 500);
        for (int width : {174, 190, 220, 320, 480}) for (int repeat = 0; repeat < 3; ++repeat) {
            root.setSize(width, 360); outer.setBounds(root.getLocalBounds()); inner.setBounds(8, 8, 150, 180);
            ui::require(outer.getScrollBarThickness() == 14 && inner.getScrollBarThickness() == 14,
                        "Nested/default scrollbar changed after layout");
            checkViewport(outer); checkViewport(inner);
        }
        outer.setViewedComponent(nullptr, false); inner.setViewedComponent(nullptr, false); root.setLookAndFeel(nullptr);
        auto owner = std::make_unique<Processor>(); owner->setSkin(theme);
        updateUiTests::install(*owner, NodeType::convolution);
        ModulationState modulation; modulation.modulators = {{1}};
        juce::String error;
        ui::require(owner->setModulationState(modulation, error), error);
        ModulationPanel motion(*owner); motion.select(1);
        motion.getProperties().set(vocalstackThemeProperty, theme); motion.setNeon(theme == 1);
        for (int width : {174, 320, 700}) {
            motion.setSize(width, 240);
            visit(motion, [](auto& v) {
                ui::require(v.getScrollBarThickness() == 14, "Motion viewport thickness mismatch");
                checkViewport(v);
            });
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        editor->setSize(940, 760); guiTestDisplay::attach(*editor);
        for (int width : {940, 972, 1200}) {
            editor->setSize(width, 760); ui::action(*editor, "Routing");
            visit(*editor, [](auto& v) { checkViewport(v); });
            ui::action(*editor, "Stack");
            visit(*editor, [](auto& v) { checkViewport(v); });
            auto* stack = ui::find<DoublerPanel>(*editor, [](auto&) { return true; });
            visit(*stack, [](auto& v) {
                ui::require(v.getScrollBarThickness() == 22, "Stack scrollbar is not round(12 * 1.8)");
                ui::require(v.getMaximumVisibleHeight() >= v.getViewedComponent()->getHeight(), "Voice card bottom clips behind horizontal bar");
            });
        }
    }
    inspectorControlLayoutTests::run();
}
} // namespace vocalstack::scrollbarTests
