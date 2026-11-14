#pragma once

// Included inside EditorTests.cpp's test namespace after RoutingExpansionTests.h.
namespace routingChunky {
namespace ui = vocalstack::toneUiTests;
using routingPolish::Fixture;
using vocalstack::Graph;
using vocalstack::NodeType;

Graph cluster() {
    return {{{1, NodeType::lead}, {100, NodeType::convolution}, {101, NodeType::crossover},
             {200, NodeType::mix}, {300, NodeType::splitter}, {13, NodeType::output}},
            {{1, 100, 0}, {100, 200, 1}, {1, 200, 0}, {200, 101, 0},
             {101, 300, 0}, {300, 13, 0}, {300, 13, 0, 1}}};
}
void arrange(Fixture& f, int theme, bool open) {
    auto layout = f.owner.getRoutingLayout().createCopy();
    layout.setProperty("inspectorVisible", true, nullptr);
    layout.setProperty("zoom", 1.0f, nullptr);
    layout.setProperty("panX", 12.0f, nullptr); layout.setProperty("panY", 12.0f, nullptr);
    const std::array<juce::Point<float>, 6> compact{{{20, 20}, {260, 20}, {500, 20}, {260, 170}, {500, 170}, {740, 170}}};
    const std::array<juce::Point<float>, 6> expanded{{{20, 20}, {20, 180}, {570, 20}, {570, 180}, {800, 180}, {800, 350}}};
    for (int i = 0; i < layout.getNumChildren(); ++i) {
        auto child = layout.getChild(i); const auto p = (open ? expanded : compact)[size_t(i)];
        child.setProperty("x", p.x, nullptr); child.setProperty("y", p.y, nullptr);
        child.setProperty("expanded", open && (int(child.getProperty("id")) == 100 || int(child.getProperty("id")) == 200), nullptr);
    }
    f.panel.reset(); f.owner.setRoutingLayout(layout);
    f.panel = std::make_unique<vocalstack::RoutingPanel>(f.owner);
    f.panel->setSize(1600, 900); routingExpansion::theme(f, theme);
    vocalstack::guiTestDisplay::attach(*f.panel);
    f.panel->getProperties().set(vocalstack::vocalstackAnimationTimeOverrideProperty, 33.3);
}
void run() {
    vocalstack::guiTestDisplay::enumerate();
    juce::Path line; line.startNewSubPath(20, 20); line.lineTo(100, 20);
    for (bool selected : {false, true}) {
        juce::Path painted; const auto stroke = vocalstack::routingCableStyle::stroke(selected);
        stroke.createStrokedPath(painted, line);
        expectNear(painted.getBounds().getHeight(), selected ? 7.0f : 4.0f, "Cable stroke is not exactly twice baseline");
        expectNear(painted.getBounds().getX(), 20, "Cable cap extends beyond endpoint");
        expectNear(painted.getBounds().getRight(), 100, "Cable cap is not butt-ended");
        expect(vocalstack::routingCableStyle::hit >= stroke.getStrokeThickness(), "Cable hit lane narrower than paint");
    }
    for (int theme = 0; theme < 3; ++theme) {
        Fixture pins(cluster(), false); arrange(pins, theme, false);
        expect(dynamic_cast<vocalstack::PixelLookAndFeel*>(&pins.panel->getLookAndFeel()) != nullptr,
               "Standalone Routing fell back to rounded JUCE chrome");
        for (int step = 0; step < 3; ++step) {
            const float corner = 9 * juce::jmin(1.0f, float(pins.owner.getRoutingLayout().getProperty("zoom")));
            const auto from = pins.pin(1, true) + juce::Point<float>(corner, corner);
            const auto to = pins.pin(100, false) + juce::Point<float>(corner, corner);
            ui::drag(pins.canvas(), from, to);
            const auto edited = pins.owner.getRoutingGraph();
            expect(pins.owner.getRoutingError().isEmpty() &&
                   std::count_if(edited.links.begin(), edited.links.end(), [](const auto& l) {
                       return l.source == 1 && l.destination == 100 && l.inputPin == 0;
                   }) == 1, "Square pin corner did not hit cable source/drop target");
            juce::MouseWheelDetails wheel{}; wheel.deltaY = step == 0 ? -.5f : .5f;
            const auto at = pins.canvas().getLocalBounds().getCentre().toFloat();
            pins.canvas().mouseWheelMove(ui::mouse(pins.canvas(), at, at, false, 0), wheel);
        }
        for (auto type : {NodeType::chorale, NodeType::waveshaper, NodeType::equalizer,
                          NodeType::crossover, NodeType::gain, NodeType::stereo, NodeType::gate,
                          NodeType::xpFilter, NodeType::synthFilter, NodeType::delay, NodeType::reverb,
                          NodeType::compressor, NodeType::bitcrusher, NodeType::granular,
                          NodeType::convolution, NodeType::saturator}) {
            Fixture f(vocalstack::updateUiTests::effectGraph(type), false);
            routingExpansion::theme(f, theme);
            f.panel->setSize(1600, 900); vocalstack::guiTestDisplay::attach(*f.panel);
            const auto compact = routingExpansion::position(f.owner, 100);
            ui::click(f.canvas(), f.point(100, 175, 14));
            expect(routingExpansion::expanded(f.owner, 100), "Square expansion target failed");
            routingExpansion::focus(f, 100, 430);
            if (vocalstack::isCreativeEffect(type)) {
                auto* creative = ui::find<vocalstack::CreativeEffectPanel>(f.canvas(), [](auto&) { return true; });
                expect(creative != nullptr, "Expanded Creative node body missing");
                auto* knob = ui::find<juce::Slider>(*creative, [](auto& c) {
                    return bool(c.getProperties()["vocalstackInspectorRotary"]);
                });
                expect(knob && knob->getParentComponent()->getWidth() >= 88,
                       "Expanded node retained undersized dense cells");
                auto* viewport = ui::find<juce::Viewport>(*creative, [](auto&) { return true; });
                auto* content = viewport->getViewedComponent();
                std::vector<juce::Rectangle<int>> cells;
                for (auto* row : content->getChildren()) if (row->isVisible() && row->getComponentID().endsWith("Control")) {
                    expect(content->getLocalBounds().contains(row->getBounds()), "Expanded control escaped scroll content");
                    for (const auto& previous : cells) expect(!previous.intersects(row->getBounds()), "Expanded node control hitboxes overlap");
                    cells.push_back(row->getBounds());
                }
                vocalstack::inspectorControlLayoutTests::visitRotaries(*creative, [&](auto& rotary) {
                    const auto lanes = rotary.getLookAndFeel().getSliderLayout(rotary);
                    expect(!bool(rotary.getProperties()["vocalstackSquarePrototype"]), "Square prototype leaked into real rotary");
                    expect(!lanes.sliderBounds.intersects(lanes.textBoxBounds) &&
                           rotary.getLocalBounds().contains(lanes.sliderBounds) &&
                           rotary.getLocalBounds().contains(lanes.textBoxBounds), "Expanded numeric/dial lanes overlap or clip");
                    creative->revealParameter(rotary.getComponentID());
                    const auto visible = viewport->getLocalArea(rotary.getParentComponent(), rotary.getParentComponent()->getLocalBounds());
                    expect(viewport->getLocalBounds().contains(visible), "Expanded node dense control is not scroll reachable");
                });
            }
            setParameter(f.owner, "fx1On", 0); routingPolish::pumpTimers();
            const auto image = f.canvas().createComponentSnapshot(f.canvas().getLocalBounds());
            expect(image.isValid(), "Bypassed expanded node failed to render");
            setParameter(f.owner, "fx1On", 1); routingPolish::pumpTimers();
            routingPolish::action(*f.panel, "Collapse all");
            routingExpansion::expectPoint(routingExpansion::position(f.owner, 100), compact,
                                          "Chunky chrome disturbed compact position bank");
        }
    }
}
void screenshots() {
    vocalstack::guiTestDisplay::enumerate();
    std::cout << "Routing PNGs: offscreen JUCE component renders; native fixtures on enumerated non-primary display\n";
    for (int theme = 0; theme < 3; ++theme) for (bool open : {false, true}) {
        Fixture f(cluster(), false); arrange(f, theme, open);
        const auto stem = vocalstack::updateUiTests::themeName(theme) + (open ? "-expanded" : "-compact");
        saveSnapshot(*f.panel, stem + "-cluster.png");
        const auto image = f.canvas().createComponentSnapshot(f.canvas().getLocalBounds(), true, 1.0f);
        const auto crop = image.getClippedImage(open ? juce::Rectangle<int>(22, 182, 530, 570) : juce::Rectangle<int>(22, 22, 710, 250));
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(stem + "-crop.png");
        juce::FileOutputStream stream(file); expect(stream.openedOk(), "Cannot create routing crop");
        stream.setPosition(0); stream.truncate();
        expect(juce::PNGImageFormat().writeImageToStream(crop, stream), "Cannot encode routing crop");
        if (open) {
            auto* creative = ui::find<vocalstack::CreativeEffectPanel>(f.canvas(), [](auto&) { return true; });
            auto* viewport = ui::find<juce::Viewport>(*creative, [](auto&) { return true; });
            viewport->setViewPosition(0, 280);
            saveSnapshot(*f.panel, stem + "-dense-controls.png");
        } else {
            ui::click(f.canvas(), (f.pin(300, true) + f.pin(13, false)) * .5f);
            saveSnapshot(*f.panel, stem + "-selected-cable.png");
            const auto from = f.pin(1, true), to = f.pin(100, false) + juce::Point<float>(70, 90);
            f.canvas().mouseDown(ui::mouse(f.canvas(), from, from, false, juce::ModifierKeys::leftButtonModifier));
            f.canvas().mouseDrag(ui::mouse(f.canvas(), to, from, true, juce::ModifierKeys::leftButtonModifier));
            saveSnapshot(*f.panel, stem + "-drag-preview.png");
            // A release at the original occupied input retains the same destination.
            f.canvas().mouseUp(ui::mouse(f.canvas(), f.pin(100, false), from, true, 0));
            juce::MouseWheelDetails wheel{}; wheel.deltaY = -.5f;
            const auto centre = f.canvas().getLocalBounds().getCentre().toFloat();
            for (int i = 0; i < 3; ++i)
                f.canvas().mouseWheelMove(ui::mouse(f.canvas(), centre, centre, false, 0), wheel);
            saveSnapshot(*f.panel, stem + "-zoom-out.png");
        }
    }
}
} // namespace routingChunky
