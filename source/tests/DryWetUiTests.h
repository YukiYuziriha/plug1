#pragma once

// Included inside EditorTests.cpp's test namespace after RoutingExpansionTests.h.
namespace dryWetUi {
namespace ui = vocalstack::toneUiTests;
using routingPolish::Fixture;
using vocalstack::Graph;
using vocalstack::NodeType;

juce::AccessibilityHandler* menuItem(const juce::String& text) {
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i) {
        auto* root = desktop.getComponent(i);
        if (!root->isVisible() || !root->isCurrentlyModal()) continue;
        auto* item = ui::find<juce::Component>(*root, [&](auto& c) {
            auto* handler = c.getAccessibilityHandler();
            return handler && handler->getTitle() == text;
        });
        if (item) return item->getAccessibilityHandler();
    }
    return nullptr;
}
void choose(const juce::String& text) {
    auto* item = menuItem(text);
    expect(item && item->getActions().invoke(juce::AccessibilityActionType::press), "Missing active menu action: " + text);
#if JUCE_WINDOWS
    // Deliver the real asynchronous PopupMenu completion without enabling
    // production modal loops or substituting a test-only graph mutation.
    for (int tick = 0; tick < 30; ++tick) {
        MSG message;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessage(&message);
        }
        juce::Thread::sleep(1);
    }
#else
    throw std::runtime_error("Dry/Wet popup dispatch test currently requires Windows");
#endif
}
void context(Fixture& f, uint32_t id) {
    const auto p = f.point(id, 24, 14);
    f.canvas().mouseDown(ui::mouse(f.canvas(), p, p, false, juce::ModifierKeys::rightButtonModifier));
}
void add(Fixture& f) {
    routingPolish::action(*f.panel, "+ Node"); choose("Dry / Wet Mix");
}
void run() {
    for (int theme = 0; theme < 3; ++theme) {
        Graph base{{{1, NodeType::lead}, {13, NodeType::output}}, {{1, 13, 0}}};
        Fixture f(base, theme == 1); routingExpansion::theme(f, theme);
        add(f); add(f);
        auto g = f.owner.getRoutingGraph();
        expect(g.nodes.size() == 4 && g.nodes[2].id == 200 && g.nodes[3].id == 201, "Repeated Add did not create independent Dry/Wet nodes");
        setParameter(f.owner, "mix1Balance", 73); setParameter(f.owner, "mix2Balance", 19);
        routingPolish::pumpTimers();
        // Move the two spawned nodes apart through their saved layout, then reopen.
        auto layout = f.owner.getRoutingLayout().createCopy();
        for (int id : {200, 201}) {
            auto n = layout.getChildWithProperty("id", id);
            n.setProperty("x", id == 200 ? 50 : 290, nullptr); n.setProperty("y", 170, nullptr);
                        n.setProperty("compactX", id == 200 ? 50 : 290, nullptr); n.setProperty("compactY", 170, nullptr);
        }
        f.owner.setRoutingLayout(layout); f.panel.reset();
        f.panel = std::make_unique<vocalstack::RoutingPanel>(f.owner);
        f.panel->addToDesktop(0); f.panel->setVisible(true); routingExpansion::theme(f, theme);
        routingExpansion::inspector(f, true);
        ui::click(f.canvas(), f.point(200, 24, 14));
        expect(routingPolish::labelIs(*f.panel, "DRY / WET MIX"), "Dry/Wet selection did not reveal its inspector: " + f.owner.getRoutingLayout().toXmlString());
        auto& slider = ui::slider(*f.panel, "mix1Balance");
        expect(slider.getSliderStyle() == juce::Slider::LinearHorizontal && bool(slider.getProperties()["vocalstackTexturedBar"]), "Dry/Wet inspector must use themed fat slider");
        expect(slider.getTooltip().contains("0% Dry") && slider.getTooltip().contains("100% Wet"), "Dry/Wet endpoint tooltip missing");
        expect(!findButton(*f.panel, "Bypass FX")->isEnabled(), "Dry/Wet incorrectly consumes FX bypass");
        routingExpansion::toggle(f, 200);
        auto* card = ui::find<juce::Slider>(f.canvas(), [](auto& c) { return c.getComponentID() == "mix1Balance"; });
        expect(card && card->getSliderStyle() == juce::Slider::LinearHorizontal && bool(card->getProperties()["vocalstackTexturedBar"]), "Expanded Dry/Wet must use themed fat slider");
        routingExpansion::toggle(f, 200);
        context(f, 200); choose("Duplicate Dry / Wet");
        g = f.owner.getRoutingGraph();
        expect(g.nodes.size() == 5 && g.nodes.back().id == 202 && g.links.size() == 1, "Duplicate changed routes or failed to create independent identity");
        expectNear(parameterValue(f.owner, "mix3Balance"), 73, "Duplicate lost live balance");
        setParameter(f.owner, "mix3Balance", 41);
        expectNear(parameterValue(f.owner, "mix1Balance"), 73, "Duplicate shares source balance");
        expectNear(parameterValue(f.owner, "mix2Balance"), 19, "Duplicate changed another balance");
        routingPolish::action(*f.panel, "Delete node");
        routingPolish::action(*f.panel, "Undo");
        expectNear(parameterValue(f.owner, "mix3Balance"), 41, "Delete undo lost independent balance");
        routingPolish::action(*f.panel, "Redo");
        expect(f.owner.getRoutingGraph().nodes.size() == 4, "Delete redo failed");
        routingPolish::action(*f.panel, "Undo");
        routingPolish::action(*f.panel, "Undo");
        expect(f.owner.getRoutingGraph().nodes.size() == 4, "Duplicate undo failed");
        routingPolish::action(*f.panel, "Redo");
        expectNear(parameterValue(f.owner, "mix3Balance"), 41, "Duplicate redo lost edited balance");
        expect(f.owner.getRoutingGraph().links.size() == 1, "History changed existing routing");
        // All remaining total-node capacity is usable, not limited to FX slots.
        while (f.owner.getRoutingGraph().nodes.size() < 48) add(f);
        routingPolish::action(*f.panel, "+ Node");
        auto* item = menuItem("Dry / Wet Mix");
        expect(item && !item->getActions().invoke(juce::AccessibilityActionType::press), "Dry/Wet Add enabled above 48 total nodes");
        juce::PopupMenu::dismissAllActiveMenus();
        expect(f.owner.getRoutingGraph().nodes.size() == 48, "Total-node cap changed");
        context(f, f.owner.getRoutingGraph().nodes.back().id);
        item = menuItem("Duplicate Dry / Wet");
        expect(item && !item->getActions().invoke(juce::AccessibilityActionType::press), "Duplicate enabled above 48 total nodes");
        juce::PopupMenu::dismissAllActiveMenus();
        expectPassiveControls(*f.panel);
    }
    Graph wired{{{1, NodeType::lead}, {200, NodeType::mix}, {201, NodeType::mix}, {13, NodeType::output}},
                {{1, 200, 0}, {200, 201, 1}, {1, 201, 0}, {201, 13, 0}}};
    Fixture f(wired, false);
    ui::click(f.canvas(), f.point(201, 24, 14));
    routingPolish::action(*f.panel, "Delete node");
    auto removed = wired;
    removed.nodes.erase(removed.nodes.begin() + 2);
    removed.links = {{1, 200, 0}};
    f.expectGraph(removed, "Deleting wired Dry/Wet changed surviving node/input routes");
    routingPolish::action(*f.panel, "Undo"); f.expectGraph(wired, "Wired Dry/Wet delete undo lost Dry/Wet pins");
    routingPolish::action(*f.panel, "Redo"); f.expectGraph(removed, "Wired Dry/Wet delete redo changed surviving routes");
}
void screenshots() {
    for (int theme = 0; theme < 3; ++theme) {
        auto g = vocalstack::defaultGraph();
        g.nodes.push_back({200, NodeType::mix}); g.nodes.back().mix = .73f;
        g.nodes.push_back({201, NodeType::mix}); g.nodes.back().mix = .19f;
        g.nodes.push_back({202, NodeType::mix}); g.nodes.back().mix = .41f;
        Fixture f(g, theme == 1); routingExpansion::theme(f, theme);
        auto layout = f.owner.getRoutingLayout().createCopy();
        layout.setProperty("selectedNode", 200, nullptr);
        for (auto child : layout) {
            const int id = int(child["id"]);
            if (id >= 200) {
                            child.setProperty("x", 40 + (id - 200) * 240, nullptr); child.setProperty("y", 180, nullptr);
                            child.setProperty("compactX", 40 + (id - 200) * 240, nullptr); child.setProperty("compactY", 180, nullptr);
                        }
        }
        f.owner.setRoutingLayout(layout); f.panel.reset();
        std::unique_ptr<juce::AudioProcessorEditor> editor(f.owner.createEditor());
        editor->setSize(1500, 1000);
        editor->addToDesktop(0); editor->setVisible(true); routingPolish::action(*editor, "Routing");
        auto* panel = ui::find<vocalstack::RoutingPanel>(*editor, [](auto& c) { return c.isShowing(); });
        expect(panel != nullptr, "Production routing panel missing");
        routingPolish::pumpTimers();
        const juce::String name = "drywet-" + juce::String(theme == 0 ? "device" : theme == 1 ? "neon" : "spectral");
        vocalstack::updateUiTests::saveImage(editor->createComponentSnapshot(editor->getLocalBounds()), name + "-nodes-inspector.png");
        routingPolish::action(*panel, "+ Node");
        expect(menuItem("Dry / Wet Mix") != nullptr, "Actual Add menu omitted Dry/Wet");
        vocalstack::updateUiTests::captureOpenPopup(*editor, name + "-add");
        const auto point = f.point(200, 24, 14);
        auto& canvas = *panel->getChildComponent(0);
        canvas.mouseDown(ui::mouse(canvas, point, point, false, juce::ModifierKeys::rightButtonModifier));
        expect(menuItem("Duplicate Dry / Wet") != nullptr, "Actual node menu omitted Duplicate");
        vocalstack::updateUiTests::captureOpenPopup(*editor, name + "-duplicate");
    }
}
}
