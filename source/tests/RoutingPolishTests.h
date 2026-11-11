#pragma once

// Included after ProcessorUiRegressionTests.h inside EditorTests.cpp's test namespace.
namespace routingPolish {
using vocalstack::Graph;
using vocalstack::NodeType;
using vocalstack::toneUiTests::click;
using vocalstack::toneUiTests::drag;

void pumpTimers() {
    juce::Thread::sleep(80);
    juce::Timer::callPendingTimersSynchronously();
}

void action(juce::Component& root, const char* text) {
    auto* button = findButton(root, text);
    expect(button && button->isShowing() && button->isEnabled() && bool(button->onClick),
           "Unavailable polishing action: " + juce::String(text));
    button->onClick();
}

bool labelIs(juce::Component& root, const juce::String& text) {
    return vocalstack::toneUiTests::find<juce::Label>(root, [&](auto& label) {
        return label.isVisible() && label.getText() == text;
    }) != nullptr;
}

struct Fixture {
    Processor owner;
    std::unique_ptr<vocalstack::RoutingPanel> panel;
    explicit Fixture(const Graph& graph, bool neon) {
        owner.setFlash(neon);
        juce::String error;
        expect(owner.setRoutingGraph(graph, error), "Polishing fixture rejected: " + error);
        juce::ValueTree layout("RoutingLayout");
        layout.setProperty("zoom", 1.0f, nullptr);
        layout.setProperty("panX", 0.0f, nullptr);
        layout.setProperty("panY", 0.0f, nullptr);
        for (const auto& n : graph.nodes) {
            juce::Point<float> p;
            if (n.type == NodeType::voice) p = {20, 20.0f + n.voiceSlot * 70.0f};
            else if (n.id == 1) p = {20, 600};
            else if (n.id == 11) p = {240, 20};
            else if (n.id == 13) p = {640, 20};
            else p = {440, 180};
            juce::ValueTree child("NODE");
            child.setProperty("id", int(n.id), nullptr);
            child.setProperty("x", p.x, nullptr);
            child.setProperty("y", p.y, nullptr);
            layout.addChild(child, -1, nullptr);
        }
        owner.setRoutingLayout(layout);
        panel = std::make_unique<vocalstack::RoutingPanel>(owner);
        panel->addToDesktop(0);
        panel->setVisible(true);
    }
    juce::Component& canvas() { return *panel->getChildComponent(0); }
    juce::Point<float> point(std::uint32_t id, float dx, float dy) {
        const auto layout = owner.getRoutingLayout();
        const auto n = layout.getChildWithProperty("id", int(id));
        expect(n.isValid(), "Missing fixture node position");
        const float zoom = float(layout.getProperty("zoom"));
        return {(float(n.getProperty("x")) + dx) * zoom + float(layout.getProperty("panX")),
                (float(n.getProperty("y")) + dy) * zoom + float(layout.getProperty("panY"))};
    }
    juce::Point<float> pin(std::uint32_t id, bool output, int port = 0) {
        // RoutingPanel: 190-wide nodes, first port at y+42, 22 pixels per port.
        return point(id, output ? 190.0f : 0.0f, 42.0f + port * 22.0f);
    }
    void selected(std::uint32_t id, const juce::String& title, bool visible) {
        click(canvas(), point(id, 20, 15));
        expect(labelIs(*panel, title) == visible, "Unexpected routing visibility: " + title);
    }
    void expectGraph(const Graph& expected, const juce::String& reason) {
        expect(owner.getRoutingError().isEmpty(), reason + ": rejected edit");
        expect(graphText(owner.getRoutingGraph()) == graphText(expected), reason);
        expectSavedGraph(savedProcessorState(owner), "ActiveRoutingGraph", expected);
    }
};

void outputGestures() {
    for (const bool neon : {false, true}) {
        Graph graph{{{1, NodeType::lead}, {100, NodeType::chorale},
                     {11, NodeType::mix}, {13, NodeType::output}},
                    {{1, 100, 0}, {1, 11, 0}, {100, 13, 0}, {11, 13, 0}}};
        Fixture f(graph, neon);
        click(f.canvas(), f.pin(1, true));
        f.expectGraph(graph, "Output click changed saved fanout");
        auto* undo = findButton(*f.panel, "Undo");
        expect(undo && !undo->isEnabled(), "Output selection created undo history");

        auto disconnected = graph;
        disconnected.links.erase(disconnected.links.begin(), disconnected.links.begin() + 2);
        drag(f.canvas(), f.pin(1, true), {760, 500});
        f.expectGraph(disconnected, "Empty output drop did not clear ALL previous links");
        action(*f.panel, "Undo");
        f.expectGraph(graph, "Output disconnect undo lost saved fanout");
        action(*f.panel, "Redo");
        f.expectGraph(disconnected, "Output disconnect redo lost edit");
        action(*f.panel, "Undo");

        auto replaced = disconnected;
        replaced.links.push_back({1, 11, 1});
        drag(f.canvas(), f.pin(1, true), f.pin(11, false, 1));
        f.expectGraph(replaced, "Output drag did not replace every old destination");
        action(*f.panel, "Undo");
        f.expectGraph(graph, "Replacement undo did not restore original fanout");

        // The equal-height cubic's midpoint selects the wire body, not a node or pin.
        click(f.canvas(), (f.pin(11, true) + f.pin(13, false)) * 0.5f);
        expect(labelIs(*f.panel, "AUDIO CONNECTION"), "Cable selection did not show inspector");
        action(*f.panel, "Disconnect");
        auto oneRemoved = graph;
        oneRemoved.links.pop_back();
        f.expectGraph(oneRemoved, "Inspector Disconnect changed unrelated fanout");
        action(*f.panel, "Undo");
        f.expectGraph(graph, "Inspector disconnect undo lost cable");

        const juce::Point<float> cursor{315, 315};
        const auto before = f.owner.getRoutingLayout();
        juce::MouseWheelDetails wheel{};
        wheel.deltaY = 0.5f;
        const auto event = vocalstack::toneUiTests::mouse(f.canvas(), cursor, cursor, false, 0);
        f.canvas().mouseWheelMove(event, wheel);
        const auto after = f.owner.getRoutingLayout();
        const float z0 = float(before.getProperty("zoom")), z1 = float(after.getProperty("zoom"));
        expect(z1 > z0, "Wheel without modifiers did not zoom");
        expectNear((cursor.x - float(after.getProperty("panX"))) / z1,
                   (cursor.x - float(before.getProperty("panX"))) / z0, "Zoom moved cursor world X");
        expectNear((cursor.y - float(after.getProperty("panY"))) / z1,
                   (cursor.y - float(before.getProperty("panY"))) / z0, "Zoom moved cursor world Y");
        f.expectGraph(graph, "Wheel zoom changed routing");
    }
}

void multibandOutputs() {
    for (const bool neon : {false, true}) {
        Graph graph{{{1, NodeType::lead}, {100, NodeType::crossover},
                     {11, NodeType::mix}, {13, NodeType::output}},
                    {{1, 100, 0}, {100, 11, 0, 0}, {100, 11, 1, 1},
                     {100, 13, 0, 1}, {100, 13, 0, 2}, {11, 13, 0}}};
        Fixture f(graph, neon);
        setParameter(f.owner, vocalstack::toneParameterId(1, "BandCount"), 3);
        pumpTimers();
        click(f.canvas(), f.pin(100, true, 1));
        f.expectGraph(graph, "Multiband output click changed cables");
        auto expected = graph;
        expected.links.erase(expected.links.begin() + 2, expected.links.begin() + 4);
        drag(f.canvas(), f.pin(100, true, 1), {760, 500});
        f.expectGraph(expected, "Clearing Mid changed Low/High or retained Mid fanout");
        action(*f.panel, "Undo");
        f.expectGraph(graph, "Multiband clear undo lost independent ports");
        expected.links.push_back({100, 11, 1, 1});
        drag(f.canvas(), f.pin(100, true, 1), f.pin(11, false, 1));
        f.expectGraph(expected, "Mid replacement changed another band's links");
        action(*f.panel, "Undo");
        f.expectGraph(graph, "Multiband replacement undo lost independent ports");
    }
}

void voiceVisibility() {
    for (const bool neon : {false, true}) {
        const auto graph = vocalstack::defaultGraph();
        Fixture f(graph, neon);
        // Basic ignores dormant Advanced presence and always exposes exactly two voices.
        for (size_t slot = 0; slot < 8; ++slot)
            setParameter(f.owner, vocalstack::voicePresentParameterId(slot), slot >= 2 ? 1 : 0);
        setParameter(f.owner, "advancedOn", 0);
        pumpTimers();
        for (int slot = 0; slot < 8; ++slot)
            f.selected(std::uint32_t(slot + 2), "VOICE " + juce::String(slot + 1), slot < 2);
        f.expectGraph(graph, "Basic visibility changed stored voice routing");

        setParameter(f.owner, "advancedOn", 1);
        setParameter(f.owner, "voice3On", 0);
        pumpTimers();
        for (int slot = 0; slot < 8; ++slot)
            f.selected(std::uint32_t(slot + 2), "VOICE " + juce::String(slot + 1), slot >= 2);
        f.selected(4, "VOICE 3", true);
        setParameter(f.owner, vocalstack::voicePresentParameterId(2), 0);
        pumpTimers();
        expect(!labelIs(*f.panel, "VOICE 3"), "Host absence left a hidden voice selected");
        f.selected(4, "VOICE 3", false);
        setParameter(f.owner, vocalstack::voicePresentParameterId(2), 1);
        pumpTimers();
        f.selected(4, "VOICE 3", true);
        expectNear(parameterValue(f.owner, "voice3On"), 0, "Showing muted voice unmuted it");
        f.expectGraph(graph, "Presence/mute display edits changed dormant routing");
    }
}

void foldedMainChorale() {
    for (const bool neon : {false, true}) {
        const auto graph = vocalstack::defaultGraph();
        Fixture f(graph, neon);
        setParameter(f.owner, vocalstack::fxParameterId(0, "On"), 0);
        pumpTimers();
        f.selected(12, "CHORALE / MAIN", false);
        f.expectGraph(graph, "Folding bypass published a different graph");
        const auto midpoint = (f.pin(11, true) + f.pin(13, false)) * 0.5f;
        click(f.canvas(), midpoint);
        expect(labelIs(*f.panel, "AUDIO CONNECTION"), "Folded wire could not be selected");
        action(*f.panel, "Disconnect");
        auto disconnected = graph;
        disconnected.links.erase(disconnected.links.end() - 2, disconnected.links.end());
        f.expectGraph(disconnected, "Folded Disconnect did not remove BOTH real chain links");
        action(*f.panel, "Undo");
        f.expectGraph(graph, "Folded Disconnect undo did not restore real chain");
        drag(f.canvas(), f.pin(13, false), {760, 500});
        f.expectGraph(disconnected, "Folded input-end empty drop retained hidden chain");
        action(*f.panel, "Undo");
        auto direct = disconnected;
        direct.links.push_back({11, 13, 0});
        drag(f.canvas(), f.pin(11, true), f.pin(13, false));
        f.expectGraph(direct, "Folded output rewire did not create one real direct cable");
        action(*f.panel, "Undo");
        drag(f.canvas(), f.pin(11, true), {760, 500});
        f.expectGraph(disconnected, "Folded output empty drop retained hidden chain");
        action(*f.panel, "Undo");

        click(f.canvas(), midpoint);
        setParameter(f.owner, vocalstack::fxParameterId(0, "On"), 1);
        pumpTimers();
        expect(vocalstack::toneUiTests::find<juce::Label>(*f.panel, [](auto& label) {
                   return label.getText().contains("Transparent bypass:");
               }) == nullptr, "Host enabling Chorale left a stale folded-wire inspector");
        f.selected(12, "CHORALE / MAIN", true);
        auto* enabled = findButton(*f.panel, "FX enabled");
        expect(enabled && enabled->getToggleState(), "Host mainChorale On did not reveal enabled inspector");
        setParameter(f.owner, vocalstack::fxParameterId(0, "On"), 0);
        pumpTimers();
        expect(!labelIs(*f.panel, "CHORALE / MAIN"), "Host bypass left folded node selected");
        f.selected(12, "CHORALE / MAIN", false);
        f.expectGraph(graph, "Host bypass changed stored canonical chain");

        // A custom bypassed effect remains a machine, never a transparent cable.
        Fixture custom(serialFxGraph(), neon);
        setParameter(custom.owner, "fx1On", 0);
        pumpTimers();
        custom.selected(100, "CHORALE 2", true);
        auto* customEnabled = findButton(*custom.panel, "FX enabled");
        expect(customEnabled && !customEnabled->getToggleState(), "Custom bypass was hidden or ignored");
        custom.expectGraph(serialFxGraph(), "Custom bypass changed saved routing");

        auto altered = graph;

        altered.links[altered.links.size() - 2] = {1, 12, 0};
        Fixture noncanonical(altered, neon);
        setParameter(noncanonical.owner, vocalstack::fxParameterId(0, "On"), 0);
        pumpTimers();
        noncanonical.selected(12, "CHORALE / MAIN", true);
        noncanonical.expectGraph(altered, "Noncanonical bypass was folded or changed");
    }
}

void embeddedTabs() {
    for (const bool neon : {false, true}) {
        Processor owner;
        owner.setFlash(neon);
        owner.setAdvancedEnabled(true);
        setParameter(owner, vocalstack::voicePitchParameterId(0), 7.25f);
        setParameter(owner, "voice1Gain", -9);
        setParameter(owner, "voice1On", 0);
        for (const bool expanded : {false, true}) {
            for (const bool active : {false, true}) {
                owner.setAdvancedVisible(expanded);
                setParameter(owner, "advancedOn", active ? 1 : 0);
                std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
                editor->addToDesktop(0);
                editor->setVisible(true);
                auto* routing = vocalstack::toneUiTests::find<vocalstack::RoutingPanel>(*editor, [](auto&) { return true; });
                auto* advanced = vocalstack::toneUiTests::find<vocalstack::DoublerPanel>(*editor, [](auto&) { return true; });
                expect(routing && advanced, "Editor must own its embedded routing and stack panels");
                const auto before = graphText(owner.getRoutingGraph());
                const auto windowBounds = editor->getLocalBounds();
                const auto floatingWindows = [] {
                    int count = 0;
                    auto& desktop = juce::Desktop::getInstance();
                    for (int i = 0; i < desktop.getNumComponents(); ++i)
                        if (dynamic_cast<juce::DocumentWindow*>(desktop.getComponent(i))) ++count;
                    return count;
                };
                const int windows = floatingWindows();
                juce::String firstLayout;
                for (int repeat = 0; repeat < 2; ++repeat) {
                    action(*editor, "Routing");
                    pumpTimers();
                    expect(routing->isShowing() && routing->getParentComponent() == editor.get()
                               && !routing->isOnDesktop() && routing->getPeer() == editor->getPeer(),
                           "Routing opened outside its editor");
                    expect(editor->getLocalBounds() == windowBounds
                               && routing->getBounds() == editor->getLocalBounds().withTrimmedBottom(52).reduced(18).withTrimmedTop(82),
                           "Embedded routing must use available space above the preset footer without resizing the window");
                    if (repeat == 0) firstLayout = owner.getRoutingLayout().toXmlString();
                    else expect(firstLayout == owner.getRoutingLayout().toXmlString(), "Tab switch refitted saved routing layout");
                    if (!expanded && !active && repeat == 0)
                        saveSnapshot(*editor, neon ? "editor-routing-neon.png" : "editor-routing-device.png");
                    expect(!advanced->isVisible(), "Routing tab left Advanced panel visible");
                    expect(floatingWindows() == windows, "Routing spawned a floating window");
                    expect(findButton(*editor, "Routing")->getToggleState()
                               && !findButton(*editor, "Stack")->getToggleState(), "Routing tab selection not reflected");
                    action(*editor, "Stack");
                    pumpTimers();
                    expect(!routing->isVisible() && advanced->isVisible(), "Stack must stay visible regardless of legacy drawer state");
                    expect(editor->getLocalBounds() == windowBounds, "Stack tab changed host window dimensions");
                    expect(findButton(*editor, "Stack")->getToggleState()
                               && !findButton(*editor, "Routing")->getToggleState(), "Stack tab selection not reflected");
                    expect(owner.isAdvancedVisible() == expanded && owner.settings().advanced == active,
                           "Tab switch changed Advanced visibility or activation");
                    expectNear(parameterValue(owner, vocalstack::voicePitchParameterId(0)), 7.25f, "Tabs erased dormant pitch");
                    expectNear(parameterValue(owner, "voice1Gain"), -9, "Tabs erased dormant gain");
                    expectNear(parameterValue(owner, "voice1On"), 0, "Tabs unmuted dormant voice");
                    expect(graphText(owner.getRoutingGraph()) == before, "Tabs changed routing graph");
                    expectPassiveControls(*editor);
                }
            }
        }
    }
}
} // namespace routingPolish
