#pragma once

// Included inside EditorTests.cpp's anonymous namespace, after RoutingPolishTests.h.
namespace routingExpansion {
namespace ui = vocalstack::toneUiTests;
using routingPolish::Fixture;
using vocalstack::Graph;
using vocalstack::NodeType;

Graph graph() {
    Graph result{{{1, NodeType::lead}, {11, NodeType::mix},
                  {100, NodeType::chorale}, {13, NodeType::output}},
                 {{1, 100, 0}, {100, 11, 0}, {11, 13, 0}}};
    result.nodes[1].mainControls = true;
    return result;
}

juce::ValueTree nodeLayout(Processor& owner, uint32_t id) {
    auto result = owner.getRoutingLayout().getChildWithProperty("id", int(id));
    expect(result.isValid(), "Expansion layout missing node " + juce::String(int(id)));
    return result;
}
juce::Point<float> position(Processor& owner, uint32_t id, const char* x = "x", const char* y = "y") {
    const auto n = nodeLayout(owner, id);
    expect(n.hasProperty(x) && n.hasProperty(y), "Missing independent position properties");
    return {float(n.getProperty(x)), float(n.getProperty(y))};
}
void expectPoint(juce::Point<float> actual, juce::Point<float> wanted, const char* reason) {
    ui::near(actual.x, wanted.x, juce::String(reason) + " X");
    ui::near(actual.y, wanted.y, juce::String(reason) + " Y");
}
bool expanded(Processor& owner, uint32_t id) { return bool(nodeLayout(owner, id).getProperty("expanded")); }
bool inspectorVisible(Processor& owner) { return bool(owner.getRoutingLayout().getProperty("inspectorVisible", true)); }
void theme(Fixture& f, int selected) {
    f.owner.setSkin(selected);
    vocalstack::updateUiTests::inheritTheme(*f.panel, selected);
    f.panel->setNeon(selected == 1);
    expect(vocalstack::componentTheme(*f.panel) == vocalstack::themeFromInt(selected), "Expansion fixture did not inherit requested theme");
}
void inspector(Fixture& f, bool visible) {
    if (inspectorVisible(f.owner) != visible) routingPolish::action(*f.panel, visible ? "<" : ">");
    expect(inspectorVisible(f.owner) == visible, "Inspector toggle did not persist");
    auto* divider = ui::find<juce::Component>(*f.panel, [](auto& c) { return c.getName() == "Resize node inspector"; });
    expect(divider != nullptr && divider->isShowing() == visible, "Inspector divider visibility differs from sticky preference");
}

// Geometry of the four fixture node types, from RoutingPanel's public canvas
// drawing contract: 22px port rows, 6px body gap and 8px bottom padding.
juce::Rectangle<float> bounds(Processor& owner, uint32_t id) {
    const auto p = position(owner, id);
    const bool open = expanded(owner, id);
    const float top = id == 11 ? 90.0f : 70.0f;
    const float width = id == 1 ? 416.0f : id == 100 ? 296.0f : 206.0f;
    const float height = id == 1 ? 258.0f : id == 100 ? 146.0f : 114.0f;
    return {p.x, p.y, open ? width : 190.0f, open ? top + height + 8 : top - 6};
}
void pan(Fixture& f, juce::Point<float> delta) {
    const auto start = f.canvas().getLocalBounds().getCentre().toFloat();
    ui::drag(f.canvas(), start, start + delta, juce::ModifierKeys::middleButtonModifier);
}
void focus(Fixture& f, uint32_t id, float bodyHeight = 114) {
    // Centre the body rather than fitting the entire graph to an illegible scale.
    const auto target = f.point(id, id == 1 ? 208.0f : id == 100 ? 258.0f : 103.0f, 70 + bodyHeight * .5f);
    pan(f, f.canvas().getLocalBounds().getCentre().toFloat() - target);
}
void toggle(Fixture& f, uint32_t id) {
    const float width = expanded(f.owner, id) ? bounds(f.owner, id).getWidth() : 190;
    ui::click(f.canvas(), f.point(id, width - 15, 14));
}
void move(Fixture& f, uint32_t id, juce::Point<float> destination) {
    const auto z = float(f.owner.getRoutingLayout().getProperty("zoom"));
    const auto from = f.point(id, 24, 14);
    ui::drag(f.canvas(), from, from + (destination - position(f.owner, id)) * z);
    expectPoint(position(f.owner, id), destination, "Header drag did not persist position");
}
void nonoverlap(Fixture& f) {
    const auto g = graph();
    for (size_t i = 0; i < g.nodes.size(); ++i)
        for (size_t j = i + 1; j < g.nodes.size(); ++j)
            expect(!bounds(f.owner, g.nodes[i].id).intersects(bounds(f.owner, g.nodes[j].id)),
                   "First expansion packing overlaps nodes " + juce::String(int(g.nodes[i].id)) + "/" + juce::String(int(g.nodes[j].id)));
}

void stickyAndGlobal() {
    for (int theme = 0; theme < 3; ++theme) {
        Fixture f(graph(), false); routingExpansion::theme(f, theme);
        const int narrow = f.canvas().getWidth();
        inspector(f, false);
        expect(f.canvas().getWidth() > narrow, "Hidden inspector did not release canvas width");
        for (auto id : {1u, 11u, 100u, 13u}) {
            ui::click(f.canvas(), f.point(id, 24, 14));
            expect(!inspectorVisible(f.owner), "Selecting a node reopened sticky-hidden inspector");
        }
        ui::click(f.canvas(), (f.pin(1, true) + f.pin(100, false)) * .5f);
        expect(!inspectorVisible(f.owner), "Cable selection reopened sticky inspector");
        f.panel->selectVoice(-1);
        routingPolish::pumpTimers();
        expect(!inspectorVisible(f.owner), "Voice selection/timer reopened sticky inspector");
        toggle(f, 13);
        expect(expanded(f.owner, 13) && !expanded(f.owner, 1), "Individual expansion affected another node");
        expect(!inspectorVisible(f.owner), "Individual expansion reopened inspector");
        toggle(f, 13);
        expect(!expanded(f.owner, 13) && !inspectorVisible(f.owner), "Individual collapse changed sticky inspector");
        routingPolish::action(*f.panel, "Expand all");
        for (const auto& n : graph().nodes) expect(expanded(f.owner, n.id), "Expand all missed a supported node");
        expect(!inspectorVisible(f.owner), "Expand all did not hide inspector");
        routingPolish::action(*f.panel, "Expand all"); // Idempotent: one Undo must undo the real bulk edit.
        routingPolish::action(*f.panel, "Undo");
        for (const auto& n : graph().nodes) expect(!expanded(f.owner, n.id), "Repeated Expand all created redundant history");
        routingPolish::action(*f.panel, "Redo");
        routingPolish::action(*f.panel, "Collapse all");
        expect(inspectorVisible(f.owner) && f.canvas().getWidth() == narrow, "Collapse all did not restore inspector geometry");
        for (const auto& n : graph().nodes) expect(!expanded(f.owner, n.id), "Collapse all missed a node");
        toggle(f, 13);
        expect(inspectorVisible(f.owner), "Individual expansion hid explicitly visible inspector");
        f.expectGraph(graph(), "Expansion controls changed routing");
    }
}

void packingAndPositions() {
    Fixture f(graph(), false);
    std::array<juce::Point<float>, 4> compact{};
    const auto g = graph();
    for (size_t i = 0; i < g.nodes.size(); ++i) compact[i] = position(f.owner, g.nodes[i].id);
    routingPolish::action(*f.panel, "Expand all");
    nonoverlap(f);
    const auto packed = f.owner.getRoutingLayout().createCopy();
    routingPolish::action(*f.panel, "Collapse all");
    routingPolish::action(*f.panel, "Expand all");
    nonoverlap(f);
    for (const auto& n : g.nodes) {
        const auto saved = packed.getChildWithProperty("id", int(n.id));
        expectPoint(position(f.owner, n.id), {float(saved.getProperty("x")), float(saved.getProperty("y"))}, "Restored packing changed position");
    }
    const auto lead = position(f.owner, 1);
    const auto output = position(f.owner, 13);
    focus(f, 13);
    const auto manual = lead + juce::Point<float>(30, 30);
    move(f, 13, manual); // Deliberate overlap is a user decision, not a reason to repack on restore.
    expect(bounds(f.owner, 13).intersects(bounds(f.owner, 1)), "Manual-overlap fixture failed");
    toggle(f, 13);
    expectPoint(position(f.owner, 13), compact[3], "Collapse lost original compact position");
    move(f, 13, compact[3] + juce::Point<float>(90, 80));
    const auto compactManual = position(f.owner, 13);
    toggle(f, 13);
    expectPoint(position(f.owner, 13), manual, "Re-expansion repacked a saved manual expanded position");
    expectPoint(position(f.owner, 1), lead, "Individual expansion moved an existing neighbour");
    routingPolish::action(*f.panel, "Collapse all");
    expectPoint(position(f.owner, 13), compactManual, "Bulk collapse lost independent compact movement");
    for (size_t i = 0; i < 3; ++i) expectPoint(position(f.owner, g.nodes[i].id), compact[i], "Bulk collapse rearranged compact graph");
    routingPolish::action(*f.panel, "Expand all");
    expectPoint(position(f.owner, 13), manual, "Bulk re-expansion repacked restored manual movement");
    expectPoint(position(f.owner, 1), lead, "Bulk restore moved neighbour");
    expect(output != manual, "Manual position did not differ from automatic packing");

    // Initial packing is stable-ID based, not graph-vector based.
    auto reversed = graph(); std::reverse(reversed.nodes.begin(), reversed.nodes.end());
    Fixture other(reversed, false);
    routingPolish::action(*other.panel, "Expand all");
    Fixture original(graph(), false);
    routingPolish::action(*original.panel, "Expand all");
    nonoverlap(other);
    for (const auto& n : g.nodes) expectPoint(position(other.owner, n.id), position(original.owner, n.id), "Packing depends on node vector order");
}

void horizontalPacking() {
    for (int skin = 0; skin < 3; ++skin) for (int width : {904, 1280, 1800}) {
        Fixture f(graph(), false); theme(f, skin);
        f.panel->setSize(width, 800);
        routingPolish::action(*f.panel, "Expand all");
        nonoverlap(f);
        const auto lead = bounds(f.owner, 1);
        bool beside = false, wrapped = false;
        for (auto id : {11u, 13u, 100u}) {
            const auto card = bounds(f.owner, id);
            beside |= card.getX() >= lead.getRight() + 11;
            wrapped |= card.getY() > lead.getY();
        }
        expect(beside, "Bulk expansion failed to use horizontal viewport space");
        if (width == 904) expect(wrapped, "Narrow bulk expansion did not wrap final card sizes");
        const auto saved = f.owner.getRoutingLayout().createCopy();
        const float zoom = float(saved.getProperty("zoom"));
        expect(zoom >= .35f, "Bulk expansion shrank controls below the readable zoom floor");
        const auto screen = juce::Rectangle<float>(f.point(1, 0, 0),
                                                   f.point(1, lead.getWidth(), lead.getHeight()));
        expect(f.canvas().getLocalBounds().toFloat().contains(screen), "Bulk expansion did not reveal its first useful card");
        expect(f.canvas().getNumChildComponents() > 0, "Bulk expansion left every useful control virtualized away");
        f.panel->setSize(width + 170, 870);
        routingPolish::action(*f.panel, "Expand all");
        routingPolish::action(*f.panel, "Collapse all");
        routingPolish::action(*f.panel, "Expand all");
        for (const auto& n : graph().nodes) {
            const auto previous = saved.getChildWithProperty("id", int(n.id));
            expectPoint(position(f.owner, n.id), {float(previous.getProperty("x")), float(previous.getProperty("y"))},
                        "Resize/repeated bulk expansion destroyed an existing arrangement");
        }
    }
    {
        Fixture single(graph(), false); inspector(single, false); single.panel->setSize(1800, 800);
        const auto neighbour = position(single.owner, 100);
        toggle(single, 13);
        nonoverlap(single);
        expect(position(single.owner, 13).x >= 642 && position(single.owner, 13).y == 20,
               "Single expansion pushed downward instead of using adjacent horizontal space");
        expectPoint(position(single.owner, 100), neighbour, "Single expansion rearranged its neighbour");
        const auto card = bounds(single.owner, 13);
        expect(single.canvas().getLocalBounds().toFloat().contains(juce::Rectangle<float>(
                   single.point(13, 0, 0), single.point(13, card.getWidth(), card.getHeight()))),
               "Single expansion failed to reveal its target card");
    }
    // Old files cannot distinguish automatic from manual coordinates. Keep even
    // a downward-looking or overlapping saved layout rather than infer intent.
    Fixture f(graph(), false);
    auto saved = f.owner.getRoutingLayout().createCopy();
    auto output = saved.getChildWithProperty("id", 13);
    output.setProperty("expanded", true, nullptr);
    output.setProperty("x", 20.0f, nullptr);
    output.setProperty("y", 6012.0f, nullptr);
    output.setProperty("expandedX", 20.0f, nullptr);
    output.setProperty("expandedY", 6012.0f, nullptr);
    output.setProperty("compactX", 640.0f, nullptr);
    output.setProperty("compactY", 20.0f, nullptr);
    f.panel.reset(); f.owner.setRoutingLayout(saved);
    f.panel = std::make_unique<vocalstack::RoutingPanel>(f.owner);
    f.panel->addToDesktop(0); f.panel->setVisible(true);
    expectPoint(position(f.owner, 13), {20, 6012}, "Opening an unclassified saved position guessed migration intent");
    routingPolish::action(*f.panel, "Collapse all");
    expectPoint(position(f.owner, 13), {640, 20}, "Unclassified layout lost its compact bank");
    routingPolish::action(*f.panel, "Expand all");
    expectPoint(position(f.owner, 13), {20, 6012}, "Bulk expansion overwrote an unclassified expanded position");
}

void layoutHistory() {
    Fixture f(graph(), false);
    const auto compact = position(f.owner, 13);
    toggle(f, 13);
    const auto automatic = position(f.owner, 13);
    move(f, 13, automatic + juce::Point<float>(70, 50));
    const auto manual = position(f.owner, 13);
    inspector(f, false);
    // Do not pump the routing timer: an external topology edit normally clears
    // history. Layout Undo must neither republish its old graph nor replay APVTS.
    auto newer = graph(); newer.links = {{1, 13, 0}};
    juce::String error;
    expect(f.owner.setRoutingGraph(newer, error), "Newer processor graph rejected: " + error);
    setParameter(f.owner, "outputGain", -9);
    setParameter(f.owner, "fx1Mix", 73);
    const auto unchanged = [&] {
        f.expectGraph(newer, "Layout history republished stale routing graph");
        expectNear(parameterValue(f.owner, "outputGain"), -9, "Layout history replayed output automation");
        expectNear(parameterValue(f.owner, "fx1Mix"), 73, "Layout history replayed FX automation");
    };
    routingPolish::action(*f.panel, "Undo");
    expect(inspectorVisible(f.owner), "Inspector Undo did not restore visibility"); unchanged();
    routingPolish::action(*f.panel, "Undo");
    expectPoint(position(f.owner, 13), automatic, "Movement Undo lost automatic expanded position"); unchanged();
    routingPolish::action(*f.panel, "Undo");
    expect(!expanded(f.owner, 13), "Expansion Undo did not collapse");
    expectPoint(position(f.owner, 13), compact, "Expansion Undo lost compact position"); unchanged();
    for (int i = 0; i < 3; ++i) { routingPolish::action(*f.panel, "Redo"); unchanged(); }
    expect(expanded(f.owner, 13) && !inspectorVisible(f.owner), "Redo lost expansion/inspector visibility");
    expectPoint(position(f.owner, 13), manual, "Redo lost manual expanded position");
}

void reopen() {
    Fixture f(graph(), false);
    move(f, 13, {670, 90});
    const auto compact = position(f.owner, 13);
    toggle(f, 13); move(f, 13, {750, 240}); inspector(f, false);
    const auto saved = f.owner.getRoutingLayout().createCopy();
    juce::MemoryBlock state; f.owner.getStateInformation(state);
    const auto check = [&](Processor& owner, juce::Component& panel, const juce::String& stage) {
        expect(expanded(owner, 13) && !inspectorVisible(owner), "Reopen lost expanded/sticky state");
        expectPoint(position(owner, 13), {750, 240}, "Reopen lost expanded movement");
        expectPoint(position(owner, 13, "compactX", "compactY"), compact, "Reopen lost compact movement");
        for (const auto* key : {"zoom", "panX", "panY"}) expectNear(float(owner.getRoutingLayout().getProperty(key)), float(saved.getProperty(key)), "Reopen changed viewport");
        expect(findButton(panel, "<") != nullptr, "Reopened hidden inspector lacks Show action");
        auto* canvas = ui::find<juce::Component>(panel, [](auto& c) { return c.getName() == "Routing node canvas"; });
        expect(canvas != nullptr && canvas->isShowing(), stage + ": reopened routing canvas is not showing");
        const auto layout = owner.getRoutingLayout();
        const float zoom = float(layout.getProperty("zoom"));
        const auto top = (position(owner, 13) + juce::Point<float>(8, 70)) * zoom
                       + juce::Point<float>(float(layout.getProperty("panX")), float(layout.getProperty("panY")));
        expect(canvas->getLocalBounds().toFloat().contains(juce::Rectangle<float>(top.x, top.y, 190 * zoom, 114 * zoom)),
               stage + ": saved output body is not fully onscreen; fixture cannot require a mounted attachment");
        const auto mounted = [&] {
            auto* gain = ui::find<juce::Slider>(*canvas, [](auto& s) { return s.getComponentID() == "outputGain"; });
            expect(gain != nullptr && gain->isShowing(), stage + ": onscreen expanded output did not mount its outputGain attachment");
        };
        mounted(); // Showing/reparenting must mount synchronously, not wait for a timer.
        routingPolish::pumpTimers();
        mounted();
    };
    f.panel.reset();
    f.panel = std::make_unique<vocalstack::RoutingPanel>(f.owner);
    f.panel->addToDesktop(0); f.panel->setVisible(true);
    check(f.owner, *f.panel, "Same-processor panel reopen");
    toggle(f, 13); expectPoint(position(f.owner, 13), compact, "Reopened collapse lost compact bank");
    toggle(f, 13); expectPoint(position(f.owner, 13), {750, 240}, "Reopened expand lost expanded bank");
    auto restored = std::make_unique<Processor>();
    restored->setStateInformation(state.getData(), int(state.getSize()));
    auto panel = std::make_unique<vocalstack::RoutingPanel>(*restored);
    panel->addToDesktop(0); panel->setVisible(true);
    check(*restored, *panel, "Binary-state processor reopen");
}

void binding(Fixture& f, const juce::String& id) {
    auto& slider = ui::slider(f.canvas(), id); // Never find an inspector duplicate.
    if (!slider.isShowing() && id.endsWith("Mix")) {
        // Gain deliberately hides default 100% Wet, but legacy/automated values
        // must reveal the same live attachment before gesture coverage.
        setParameter(f.owner, id, 75);
        for (int tick = 0; tick < 4 && !slider.isShowing(); ++tick) routingPolish::pumpTimers();
    }
    if (!slider.isShowing()) {
        std::cout << "Expanded binding hidden: " << id << " host=" << parameterValue(f.owner, id) << " slider=" << slider.getValue() << '\n';
        for (auto* c = static_cast<juce::Component*>(&slider); c; c = c->getParentComponent())
            std::cout << "  ancestor " << c->getName() << " id=" << c->getComponentID() << " visible=" << c->isVisible() << " showing=" << c->isShowing() << '\n';
    }
    expect(slider.isShowing() && slider.isEnabled(), "Expanded binding control is hidden or disabled: " + id);
    // Effect bodies intentionally scroll at native control size. Bring the
    // actual control into view before testing it, not an offscreen attachment.
    for (auto* parent = slider.getParentComponent(); parent && parent != &f.canvas(); parent = parent->getParentComponent())
        if (auto* viewport = dynamic_cast<juce::Viewport*>(parent)) {
            auto* content = viewport->getViewedComponent();
            const auto target = content->getLocalArea(&slider, slider.getLocalBounds());
            viewport->setViewPosition(0, juce::jmax(0, target.getY() - 12));
        }
    for (auto* parent = slider.getParentComponent(); parent && parent != &f.canvas(); parent = parent->getParentComponent())
        if (auto* viewport = dynamic_cast<juce::Viewport*>(parent))
            expect(viewport->getLocalBounds().contains(viewport->getLocalArea(&slider, slider.getLocalBounds())),
                   "Expanded binding control is clipped by its viewport: " + id);
    const auto area = f.canvas().getLocalArea(&slider, slider.getLocalBounds());
    expect(f.canvas().getLocalBounds().contains(area), "Expanded binding control is clipped: " + id);
    auto& p = parameter(f.owner, id);
    slider.setValue(p.convertFrom0to1(.37f), juce::sendNotificationSync);
    ui::near(parameterValue(f.owner, id), float(slider.getValue()), "Expanded UI-to-host binding: " + id);
    setParameter(f.owner, id, p.convertFrom0to1(.63f));
    ui::near(float(slider.getValue()), parameterValue(f.owner, id), "Expanded host-to-UI binding: " + id);
}

void parameterBindings() {
    for (int theme = 0; theme < 3; ++theme) {
        Fixture f(graph(), false); routingExpansion::theme(f, theme); inspector(f, false);
        for (auto id : {1u, 11u, 100u, 13u}) {
            toggle(f, id); focus(f, id, id == 1 ? 258 : id == 100 ? 146 : 114);
            binding(f, id == 1 ? "leadGain" : id == 11 ? "double" : id == 100 ? "fx1Mix" : "outputGain");
            if (id == 100) {
                auto& on = ui::button(f.canvas(), "FX enabled");
                on.setToggleState(false, juce::sendNotificationSync);
                expectNear(parameterValue(f.owner, "fx1On"), 0, "Expanded FX enabled UI binding");
                setParameter(f.owner, "fx1On", 1);
                expect(on.getToggleState(), "Expanded FX enabled host binding");
            }
            expectPassiveControls(*f.panel);
        }
        for (auto type : {NodeType::waveshaper, NodeType::equalizer, NodeType::delay,
                          NodeType::reverb, NodeType::gain, NodeType::xpFilter, NodeType::synthFilter}) {
            Fixture effect(vocalstack::updateUiTests::effectGraph(type), false);
            routingExpansion::theme(effect, theme); inspector(effect, false);
            // All compact headers use the same chevron even when body sizes differ.
            ui::click(effect.canvas(), effect.point(100, 175, 14));
            focus(effect, 100, 470);
            binding(effect, type == NodeType::equalizer ? vocalstack::toneParameterId(1, "EqShift") : juce::String("fx1Mix"));
            auto* slider = ui::find<juce::Slider>(effect.canvas(), [](auto& s) {
                return s.isShowing() && s.getComponentID().isNotEmpty() && s.getComponentID() != "fx1Mix";
            });
            expect(slider != nullptr, "Expanded effect missing non-Mix parameter control");
            binding(effect, slider->getComponentID());
            expectPassiveControls(*effect.panel);
            // Parent changes recreate ComboBox labels. A fresh virtualized mount
            // must remain passive too; never exempt those internal labels.
            pan(effect, {5000, 5000});
            expect(effect.canvas().getNumChildComponents() == 0, "Offscreen effect body was not released");
            pan(effect, {-5000, -5000});
            expect(effect.canvas().getNumChildComponents() == 1, "Effect body was not remounted");
            expectPassiveControls(*effect.panel);
        }
        Graph voices{{{1, NodeType::lead}, {2, NodeType::voice, 0}, {13, NodeType::output}}, {{1, 13, 0}, {2, 13, 0}}};
        Fixture voice(voices, false); routingExpansion::theme(voice, theme); inspector(voice, false);
        ui::click(voice.canvas(), voice.point(2, 175, 14)); focus(voice, 2, 258);
        binding(voice, vocalstack::voiceParameterId(0, "Gain"));
        Graph local{{{1, NodeType::lead}, {200, NodeType::mix}, {13, NodeType::output}}, {{1, 200, 0}, {200, 13, 0}}};
        Fixture mixer(local, false); routingExpansion::theme(mixer, theme); inspector(mixer, false);
        ui::click(mixer.canvas(), mixer.point(200, 175, 14)); focus(mixer, 200);
        binding(mixer, vocalstack::localMixParameterId(200));
    }
}

void virtualization() {
    Fixture f(graph(), false); inspector(f, false);
    toggle(f, 13); focus(f, 13);
    binding(f, "outputGain");
    expect(f.canvas().getNumChildComponents() == 1, "Only one expanded body should be mounted");
    const auto location = position(f.owner, 13);
    const auto view = f.owner.getRoutingLayout().createCopy();
    pan(f, {5000, 5000});
    expect(f.canvas().getNumChildComponents() == 0, "Offscreen expanded body retained controls/attachments");
    setParameter(f.owner, "outputGain", -6);
    pan(f, {-5000, -5000});
    expect(f.canvas().getNumChildComponents() == 1, "Returning pan failed to remount expanded body");
    expectNear(ui::slider(f.canvas(), "outputGain").getValue(), -6, "Remounted attachment lost host automation");
    auto* body = f.canvas().getChildComponent(0);
    const auto before = f.owner.getRoutingLayout().createCopy();
    juce::MouseWheelDetails wheel{}; wheel.deltaY = .5f;
    const auto centre = body->getLocalBounds().getCentre().toFloat();
    const auto cursor = f.canvas().getLocalPoint(body, centre);
    body->mouseWheelMove(ui::mouse(*body, centre, centre, false, 0), wheel);
    const auto after = f.owner.getRoutingLayout();
    const float z0 = float(before.getProperty("zoom")), z1 = float(after.getProperty("zoom"));
    expect(z1 > z0, "Wheel over expanded body did not forward canvas zoom");
    for (const auto* axis : {"panX", "panY"}) {
        const float coordinate = juce::String(axis) == "panX" ? cursor.x : cursor.y;
        ui::near((coordinate - float(after.getProperty(axis))) / z1,
                 (coordinate - float(before.getProperty(axis))) / z0, "Body wheel moved cursor world coordinate");
    }
    body = f.canvas().getChildComponent(0);
    ui::near(body->getTransform().getScaleFactor(), z1, "Expanded controls did not scale with zoom");
    expectNear(parameterValue(f.owner, "outputGain"), -6, "Body wheel changed parameter instead of zooming");
    expectPoint(position(f.owner, 13), location, "Pan/zoom changed world position");
    f.panel->setVisible(false);
    expect(f.canvas().getNumChildComponents() == 0, "Hiding routing did not immediately release expanded body");
    routingPolish::pumpTimers();
    expect(f.canvas().getNumChildComponents() == 0, "Hidden routing retained expanded body");
    f.panel->setVisible(true);
    expect(f.canvas().getNumChildComponents() == 1, "Showing routing did not immediately remount body");
    routingPolish::pumpTimers();
    expect(f.canvas().getNumChildComponents() == 1, "Showing routing failed to remount body");
    expectNear(ui::slider(f.canvas(), "outputGain").getValue(), -6, "Visibility remount lost host automation");
    expectPassiveControls(*f.panel);
    juce::Component host;
    host.setSize(f.panel->getWidth(), f.panel->getHeight()); host.addToDesktop(0); host.setVisible(true);
    f.panel->removeFromDesktop(); host.addAndMakeVisible(*f.panel);
    expect(f.canvas().getNumChildComponents() == 1, "Visible hierarchy change failed to mount body");
    host.setVisible(false);
    expect(f.canvas().getNumChildComponents() == 0, "Hidden ancestor retained expanded body");
    host.setVisible(true);
    expect(f.canvas().getNumChildComponents() == 1, "Showing ancestor failed to remount body");
    expectPassiveControls(*f.panel);
    toggle(f, 13);
    expect(f.canvas().getNumChildComponents() == 0 && !expanded(f.owner, 13), "Collapse retained virtualized controls");
    expect(float(view.getProperty("zoom")) == z0, "Pan unexpectedly changed zoom");
}

enum class InterruptedControl { output, creative, utility, synthHandle, eqHandle };
enum class Unmount { hide, panAway, destroy };

juce::String interruptionName(InterruptedControl control, Unmount removal) {
    const auto name = control == InterruptedControl::output ? "Output knob" : control == InterruptedControl::creative ? "Creative slider"
                    : control == InterruptedControl::utility ? "Utility slider" : control == InterruptedControl::synthHandle ? "Synth handle" : "EQ handle";
    return juce::String(name) + (removal == Unmount::hide ? " / hide" : removal == Unmount::panAway ? " / offscreen pan" : " / destroy");
}
Graph interruptionGraph(InterruptedControl control) {
    return control == InterruptedControl::output ? graph()
         : vocalstack::updateUiTests::effectGraph(control == InterruptedControl::creative ? NodeType::delay
             : control == InterruptedControl::utility ? NodeType::gate
             : control == InterruptedControl::synthHandle ? NodeType::synthFilter : NodeType::equalizer);
}
juce::String interruptionSliderId(InterruptedControl control) {
    if (control == InterruptedControl::output) return "outputGain";
    if (control == InterruptedControl::creative)
        return vocalstack::creativeParameterId(1, size_t(vocalstack::CreativeParameter::delayFeedback));
    return vocalstack::utilityParameterId(1, size_t(vocalstack::UtilityParameter::gateThreshold));
}
void setupInterruptedControl(Fixture& f, InterruptedControl control) {
    inspector(f, false);
    const auto id = control == InterruptedControl::output ? 13u : 100u;
    ui::click(f.canvas(), f.point(id, 175, 14));
    focus(f, id, control == InterruptedControl::output ? 114 : 470);
    if (control == InterruptedControl::eqHandle) {
        setParameter(f.owner, "fx1EqBand1On", 1); setParameter(f.owner, "fx1EqBand1Mode", 0);
        setParameter(f.owner, "fx1EqBand1Frequency", 1000); setParameter(f.owner, "fx1EqBand1Gain", 6);
    }
    expect(f.canvas().getNumChildComponents() == 1, "Interrupted-edit fixture requires exactly one expanded body");
}
void unmount(Fixture& f, Unmount removal) {
    if (removal == Unmount::destroy) f.panel.reset();
    else {
        if (removal == Unmount::hide) f.panel->setVisible(false);
        else pan(f, {5000, 5000});
        expect(f.canvas().getNumChildComponents() == 0, "Active interaction kept an unmounted body alive");
    }
}
void remount(Fixture& f, Unmount removal) {
    if (removal == Unmount::destroy) {
        f.panel = std::make_unique<vocalstack::RoutingPanel>(f.owner);
        f.panel->addToDesktop(0); f.panel->setVisible(true);
    } else if (removal == Unmount::hide) f.panel->setVisible(true);
    else pan(f, {-5000, -5000});
    expect(f.canvas().getNumChildComponents() == 1, "Interrupted-edit body did not remount");
}
std::vector<float> hostValues(Processor& owner) {
    std::vector<float> result;
    for (auto* p : owner.getParameters()) result.push_back(p->getValue());
    return result;
}

// Run before starting any subsequent edit: a new edit's normal mouseUp could
// otherwise clear inspectorDragging and conceal a leaked card-owned edit scope.
void graphSyncAfterInterruption(Fixture& f, const Graph& original, const juce::String& context) {
    auto newer = original;
    newer.nodes.push_back({101, NodeType::gain});
    juce::String error;
    expect(f.owner.setRoutingGraph(newer, error), context + ": sync-probe graph rejected: " + error);
    expect(!inspectorVisible(f.owner), context + ": sync probe must not reopen the inspector or mask a leaked edit scope");
    auto layout = f.owner.getRoutingLayout().createCopy();
    const float zoom = float(layout.getProperty("zoom"));
    expect(std::isfinite(zoom) && zoom > 0, context + ": sync probe requires a valid canvas zoom");
    // A one-pixel external pan change provides a public, non-interactive signal
    // that the routing timer consumed this layout. A fixed sleep can miss a tick
    // after focus/TextEditor teardown; clicking too soon would overwrite the
    // incoming layout with the stale canvas. No new edit may clear a stuck scope.
    layout.setProperty("panX", float(layout.getProperty("panX")) + 1, nullptr);
    juce::Point<float> expectedBody;
    int expandedBodies = 0;
    for (const auto n : layout) if (n.hasType("NODE") && bool(n.getProperty("expanded"))) {
        ++expandedBodies;
        expectedBody = juce::Point<float>(float(n.getProperty("x")) + 8, float(n.getProperty("y")) + 70) * zoom
                     + juce::Point<float>(float(layout.getProperty("panX")), float(layout.getProperty("panY")));
    }
    expect(expandedBodies == 1, context + ": sync probe requires one expanded output/effect body");
    // Put the compact probe in the free upper-right canvas area, translating
    // screen coordinates through the incoming view left by the focused control.
    const juce::Point<float> screen(float(f.canvas().getWidth()) - 190 * zoom - 24, 20);
    expect(f.canvas().getLocalBounds().toFloat().contains(juce::Rectangle<float>(screen.x, screen.y, 190 * zoom, 64 * zoom)),
           context + ": sync probe must be fully onscreen");
    const auto world = (screen - juce::Point<float>(float(layout.getProperty("panX")), float(layout.getProperty("panY")))) / zoom;
    juce::ValueTree probe("NODE");
    probe.setProperty("id", 101, nullptr); probe.setProperty("x", world.x, nullptr); probe.setProperty("y", world.y, nullptr);
    layout.setProperty("selectedNode", 0, nullptr);
    layout.addChild(probe, -1, nullptr); f.owner.setRoutingLayout(layout);
    const auto bodyPosition = [&] {
        auto* body = f.canvas().getChildComponent(0);
        expect(body != nullptr, context + ": sync probe lost its expanded body");
        return juce::Point<float>{}.transformedBy(body->getTransform());
    };
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000;
    do {
        juce::Timer::callPendingTimersSynchronously();
        if (bodyPosition().getDistanceFrom(expectedBody) < .01f) break;
        juce::Thread::sleep(5);
    } while (juce::Time::getMillisecondCounterHiRes() < deadline);
    expect(bodyPosition().getDistanceFrom(expectedBody) < .01f,
           context + ": routing did not consume external layout within one second after cancellation; body at "
               + juce::String(bodyPosition().x) + ", " + juce::String(bodyPosition().y)
               + "; expected " + juce::String(expectedBody.x) + ", " + juce::String(expectedBody.y));
    const auto click = screen + juce::Point<float>(24, 14) * zoom;
    expect(f.canvas().getLocalBounds().toFloat().contains(click), context + ": sync-probe header click is offscreen");
    const auto beforeClick = f.owner.getRoutingLayout().toXmlString();
    auto* hit = f.canvas().getComponentAt(click.roundToInt());
    const auto hitName = hit ? hit->getName() : juce::String("none");
    ui::click(f.canvas(), click);
    expect(int(f.owner.getRoutingLayout().getProperty("selectedNode", 0)) == 101,
           context + ": onscreen probe selection did not synchronize graph and layout after cancellation"
               + "\nClick: " + juce::String(click.x) + ", " + juce::String(click.y)
               + "; hit: " + hitName
               + "\nBefore click: " + beforeClick + "\nAfter click: " + f.owner.getRoutingLayout().toXmlString());
    expect(!inspectorVisible(f.owner), context + ": selecting the sync probe reopened the inspector");
    routingPolish::action(*f.panel, "Delete node");
    f.expectGraph(original, context + ": subsequent routing edit did not publish correctly");
}

struct CapturedDrag {
    juce::Component* target = nullptr;
    juce::Point<float> from, to;
    std::vector<juce::String> ids;
};
CapturedDrag interruptedDrag(Fixture& f, InterruptedControl control) {
    CapturedDrag drag;
    if (control == InterruptedControl::eqHandle) {
        auto* graph = ui::find<vocalstack::toneui::EqGraph>(f.canvas(), [](auto&) { return true; });
        expect(graph != nullptr, "Expanded EQ drag graph missing");
        drag.target = graph;
        drag.ids = {"fx1EqBand1Frequency", "fx1EqBand1Gain"};
        drag.from = ui::eqPoint(*graph, parameterValue(f.owner, drag.ids[0]), parameterValue(f.owner, drag.ids[1]));
        drag.to = drag.from.translated(25, -15);
    } else if (control == InterruptedControl::synthHandle) {
        drag.target = ui::find<juce::Component>(f.canvas(), [](auto& c) { return c.getComponentID() == "utilityVisual"; });
        expect(drag.target != nullptr, "Expanded synth response handle missing");
        // Utility visual's response plot: left/top gutters 29/43, total gutters 61/95.
        const juce::Rectangle<float> plot(29, 43, float(drag.target->getWidth() - 61), float(drag.target->getHeight() - 95));
        drag.ids = {vocalstack::utilityParameterId(1, size_t(vocalstack::UtilityParameter::synthCutoff)),
                    vocalstack::utilityParameterId(1, size_t(vocalstack::UtilityParameter::synthResonance))};
        // This handle writes absolute plot coordinates. Choose the opposite side
        // of each midpoint from the current value, including on the second drag.
        drag.from = plot.getCentre();
        drag.to = drag.from.translated(parameterValue(f.owner, drag.ids[0]) > std::sqrt(20.f * 20000.f) ? -25.f : 25.f,
                                       parameterValue(f.owner, drag.ids[1]) > 50 ? 15.f : -15.f);
    } else {
        const auto id = interruptionSliderId(control);
        binding(f, id); // Scroll the real control onscreen and establish a non-limit value before listening.
        auto& slider = ui::slider(f.canvas(), id);
        drag.target = &slider; drag.ids = {id};
        const auto track = slider.getLookAndFeel().getSliderLayout(slider).sliderBounds.toFloat();
        drag.from = track.getCentre();
        drag.to = drag.from.translated(control == InterruptedControl::output ? 25.f : track.getWidth() * .2f,
                                      control == InterruptedControl::output ? -25.f : 0.f);
    }
    expect(drag.target->isShowing(), "Interrupted drag target is hidden");
    expect(f.canvas().getLocalBounds().contains(f.canvas().getLocalPoint(drag.target, drag.from).roundToInt())
               && f.canvas().getLocalBounds().contains(f.canvas().getLocalPoint(drag.target, drag.to).roundToInt()),
           "Interrupted drag endpoints are outside the visible canvas");
    return drag;
}
void startCapturedDrag(const CapturedDrag& drag, Processor& owner, ui::GestureLog& log, const juce::String& context) {
    std::vector<float> before;
    for (const auto& id : drag.ids) before.push_back(parameterValue(owner, id));
    drag.target->mouseDown(ui::mouse(*drag.target, drag.from, drag.from));
    drag.target->mouseDrag(ui::mouse(*drag.target, drag.to, drag.from, true));
    // Intentionally NO mouseUp: the lifecycle transition must close these gestures.
    for (size_t i = 0; i < drag.ids.size(); ++i) {
        const auto& id = drag.ids[i];
        const auto count = log.count(parameter(owner, id));
        expect(count.begins == 1 && count.ends == 0 && count.depth == 1 && count.values > 0,
               context + ": test did not establish exactly one active host gesture for " + id);
        expect(parameterValue(owner, id) != before[i], context + ": drag did not change " + id);
    }
}
void interruptedDrags() {
    for (auto control : {InterruptedControl::output, InterruptedControl::creative, InterruptedControl::utility,
                         InterruptedControl::synthHandle, InterruptedControl::eqHandle})
        for (auto removal : {Unmount::hide, Unmount::panAway, Unmount::destroy}) {
            const auto context = interruptionName(control, removal);
            const auto original = interruptionGraph(control);
            Fixture f(original, false); setupInterruptedControl(f, control);
            const auto drag = interruptedDrag(f, control);
            auto log = std::make_unique<ui::GestureLog>(f.owner);
            startCapturedDrag(drag, f.owner, *log, context);
            const auto lastValues = hostValues(f.owner);
            juce::Component::SafePointer<juce::Component> oldBody(f.canvas().getChildComponent(0));
            juce::Component::SafePointer<juce::Component> oldTarget(drag.target);
            unmount(f, removal); // Do not send a release to the active control, including after deletion.
            expect(oldBody == nullptr && oldTarget == nullptr, context + ": unmount retained active controls");
            expect(hostValues(f.owner) == lastValues, context + ": cancellation changed the last drag value or another parameter");
            for (const auto& id : drag.ids) {
                const auto count = log->count(parameter(f.owner, id));
                expect(count.begins == 1 && count.ends == 1 && count.depth == 0, context + ": cancellation did not close exactly one gesture for " + id);
            }
            log->balanced(context + " cancellation");
            remount(f, removal);
            routingPolish::pumpTimers();
            log->balanced(context + " remount did not double-end gestures");
            expect(hostValues(f.owner) == lastValues, context + ": remount replayed or changed parameter values");
            log.reset(); // Graph lifecycle parameter initialization is not a control gesture.
            graphSyncAfterInterruption(f, original, context);
            const auto next = interruptedDrag(f, control);
            ui::GestureLog subsequent(f.owner);
            startCapturedDrag(next, f.owner, subsequent, context + " subsequent edit");
            next.target->mouseUp(ui::mouse(*next.target, next.to, next.from, true, 0));
            subsequent.balanced(context + " subsequent edit");
            for (const auto& id : next.ids) {
                const auto count = subsequent.count(parameter(f.owner, id));
                expect(count.begins == 1 && count.ends == 1 && count.depth == 0, context + ": subsequent gesture was not independently balanced for " + id);
            }
        }
}

void unfinishedTextEntry() {
    for (auto control : {InterruptedControl::output, InterruptedControl::creative, InterruptedControl::utility})
        for (auto removal : {Unmount::hide, Unmount::panAway, Unmount::destroy}) {
            const auto context = interruptionName(control, removal) + " / unfinished numeric entry";
            const auto original = interruptionGraph(control);
            Fixture f(original, false); setupInterruptedControl(f, control);
            const auto id = interruptionSliderId(control); binding(f, id);
            auto& slider = ui::slider(f.canvas(), id);
            auto log = std::make_unique<ui::GestureLog>(f.owner);
            slider.showTextBox();
            auto* entry = ui::find<juce::TextEditor>(slider, [](auto&) { return true; });
            expect(entry != nullptr && entry->getWantsKeyboardFocus(), context + ": numeric entry did not open");
            if (control != InterruptedControl::output) {
                // Deliver the child event to the public Row mouse-listener entry
                // point, as JUCE does for a Label/TextEditor click. This opens the
                // owned edit scope without a Slider::mouseDown host drag.
                auto* row = slider.getParentComponent();
                row->mouseDown(ui::mouse(*entry, entry->getLocalBounds().getCentre().toFloat(), entry->getLocalBounds().getCentre().toFloat()));
            }
            entry->setText("1.234", false); // Neither Return nor mouseUp/commit.
            expect(log->begins() == 0, context + ": typing unexpectedly opened a host drag");
            const auto before = hostValues(f.owner);
            juce::Component::SafePointer<juce::TextEditor> pending(entry);
            unmount(f, removal);
            expect(pending == nullptr, context + ": unmount retained the focused editor");
            expect(hostValues(f.owner) == before, context + ": unmount committed unfinished numeric entry");
            expect(log->begins() == 0, context + ": cancelling unfinished text created a host gesture");
            log->balanced(context);
            remount(f, removal);
            expect(ui::find<juce::TextEditor>(f.canvas(), [](auto&) { return true; }) == nullptr, context + ": remount restored stale numeric entry");
            expectPassiveControls(*f.panel);
            log->balanced(context + " remount");
            log.reset();
            graphSyncAfterInterruption(f, original, context);
            auto& reopened = ui::slider(f.canvas(), id);
            const auto oldValue = parameterValue(f.owner, id);
            const auto changed = parameter(f.owner, id).convertFrom0to1(.4f);
            ui::GestureLog subsequent(f.owner);
            ui::enterValue(reopened, changed);
            ui::near(parameterValue(f.owner, id), float(reopened.getValue()), context + ": subsequent numeric commit lost binding");
            expect(parameterValue(f.owner, id) != oldValue, context + ": subsequent numeric edit did not take effect");
            expect(subsequent.begins() > 0, context + ": subsequent numeric commit did not notify the host");
            subsequent.balanced(context + " subsequent numeric commit");
        }
}

void multiSynthScheduler() {
    using Panel = vocalstack::UtilityEffectPanel;
    using P = vocalstack::UtilityParameter;
    Graph g{{{1, NodeType::lead}, {100, NodeType::synthFilter}, {101, NodeType::synthFilter},
             {102, NodeType::synthFilter}, {103, NodeType::synthFilter}, {13, NodeType::output}},
            {{1, 100, 0}, {100, 101, 0}, {101, 102, 0}, {102, 103, 0}, {103, 13, 0}}};
    Fixture f(g, false);
    f.panel.reset();
    // Fixture setup through public layout state. Four full-size expanded bodies
    // and their plots fit in the canvas, rather than merely existing offscreen.
    auto layout = f.owner.getRoutingLayout().createCopy();
    layout.setProperty("inspectorVisible", false, nullptr);
    for (auto n : layout) {
        const int index = int(n.getProperty("id")) - 100;
        const bool synth = index >= 0 && index < 4;
        n.setProperty("expanded", synth, nullptr);
        n.setProperty("x", synth ? 20 + (index % 2) * 560 : -600, nullptr);
        n.setProperty("y", synth ? 20 + (index / 2) * 580 : -600, nullptr);
    }
    f.owner.setRoutingLayout(layout);
    const auto mount = [&] {
        f.panel = std::make_unique<vocalstack::RoutingPanel>(f.owner);
        f.panel->setSize(1600, 1400); f.panel->addToDesktop(0); f.panel->setVisible(true);
    };
    mount();
    // A second processor/window exercises global scheduling, not just sharing
    // between cards belonging to one routing panel.
    auto secondOwner = std::make_unique<Processor>();
    vocalstack::updateUiTests::install(*secondOwner, NodeType::synthFilter);
    auto second = std::make_unique<Panel>(*secondOwner, 1, vocalstack::UtilityKind::synthFilter);
    second->setSize(500, 470); second->addToDesktop(0); second->setVisible(true);

    const auto responsive = [&](auto&& action, const juce::String& reason) {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        action();
        // This is a generous message-thread watchdog, not a DSP benchmark.
        // Queued/in-flight response rendering must never be joined by UI work.
        expect(juce::Time::getMillisecondCounterHiRes() - start < 1000, reason + ": message thread blocked for >= 1 second");
    };
    const auto dispatch = [&] {
        responsive([] { juce::Timer::callPendingTimersSynchronously(); }, "Synth response polling");
    };
    const auto refresh = [&] { juce::Thread::sleep(80); dispatch(); };
    const auto panels = [&] {
        std::array<Panel*, 4> result{};
        expect(f.canvas().getNumChildComponents() == 4, "Multi-synth fixture did not mount exactly four bodies");
        for (size_t i = 0; i < result.size(); ++i) {
            const juce::String id = vocalstack::fxParameterId(i + 1, "Mix");
            result[i] = ui::find<Panel>(f.canvas(), [&](auto& panel) {
                return ui::find<juce::Slider>(panel, [&](auto& slider) { return slider.getComponentID() == id; }) != nullptr;
            });
            expect(result[i] != nullptr && result[i]->isShowing(), "Multi-synth visible panel missing: " + id);
            auto* visual = ui::find<juce::Component>(*result[i], [](auto& c) { return c.getComponentID() == "utilityVisual"; });
            expect(visual != nullptr && visual->isShowing()
                       && f.canvas().getLocalBounds().contains(f.canvas().getLocalArea(visual, visual->getLocalBounds())),
                   "Multi-synth response plot is clipped/offscreen: " + id);
        }
        expect(second->isShowing(), "Second-processor synth response is not visible");
        return result;
    };
    const auto waitReady = [&](bool includeBusy, const juce::String& phase, const std::function<void()>& busy = {}) {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 15000;
        double nextBusy = 0;
        juce::String pending;
        do {
            if (busy && juce::Time::getMillisecondCounterHiRes() >= nextBusy) {
                responsive(busy, "Updating busy synth client");
                nextBusy = juce::Time::getMillisecondCounterHiRes() + 120;
            }
            dispatch();
            const auto visible = panels();
            pending.clear();
            for (size_t i = includeBusy ? 0 : 1; i < visible.size(); ++i)
                if (!visible[i]->responseReady()) pending += " fx" + juce::String(int(i + 1));
            if (!second->responseReady()) pending += " second-processor";
            if (pending.isEmpty()) return;
            juce::Thread::sleep(10);
        } while (juce::Time::getMillisecondCounterHiRes() < deadline);
        expect(false, phase + ": latest synth response did not complete within 15 seconds:" + pending);
    };
    const auto targets = [&](Processor& owner, size_t slot, int revision) {
        const auto write = [&](P field, float value) { setParameter(owner, vocalstack::utilityParameterId(slot, size_t(field)), value); };
        write(P::synthMode, float((revision + int(slot)) % 13));
        write(P::synthCutoff, float(250 + revision * 110 + int(slot) * 170));
        write(P::synthResonance, float(10 + slot * 8));
        write(P::synthDrive, float(2 + revision % 7));
        write(P::synthFeedback, float(-30 + revision));
    };
    refresh(); waitReady(true, "Initial simultaneous visible responses");
    std::array<juce::Image, 4> before{};
    {
        const auto visible = panels();
        for (size_t i = 0; i < visible.size(); ++i) {
            auto* visual = ui::find<juce::Component>(*visible[i], [](auto& c) { return c.getComponentID() == "utilityVisual"; });
            before[i] = visual->createComponentSnapshot(visual->getLocalBounds());
        }
    }
    // More replacements than clients: each visual must eventually display the
    // newest request, not a completed but obsolete intermediate result.
    for (int revision = 1; revision <= 6; ++revision) {
        responsive([&] {
            for (size_t slot = 1; slot <= 4; ++slot) targets(f.owner, slot, revision);
            targets(*secondOwner, 1, revision + 1);
        }, "Replacing queued synth targets");
        refresh();
    }
    // Keep one client busy while the other cards and another editor settle.
    // A last-writer-wins queue must not let that client starve its neighbours.
    for (size_t slot = 1; slot <= 4; ++slot) targets(f.owner, slot, 7);
    targets(*secondOwner, 1, 7);
    int busyRevision = 8;
    refresh();
    waitReady(false, "Non-busy clients while another client keeps submitting", [&] {
        targets(f.owner, 1, 8 + (busyRevision++ % 20));
    });
    waitReady(true, "Coalesced latest targets");

    for (int cycle = 0; cycle < 3; ++cycle) {
        for (size_t slot = 1; slot <= 4; ++slot) targets(f.owner, slot, 13 + cycle * 2);
        refresh(); // Submit work before destroying the visible clients.
        responsive([&] { pan(f, {5000, 5000}); }, "Cancelling virtualized synth clients");
        expect(f.canvas().getNumChildComponents() == 0, "Offscreen synth clients retained response jobs/components");
        for (size_t slot = 1; slot <= 4; ++slot) targets(f.owner, slot, 14 + cycle * 2);
        targets(*secondOwner, 1, 14 + cycle * 2);
        responsive([&] { pan(f, {-5000, -5000}); }, "Remounting synth clients while worker is busy");
        panels();
        refresh(); waitReady(true, "Cancelled/remounted synth clients");
        expectPassiveControls(*f.panel);
    }
    {
        const auto visible = panels();
        for (size_t i = 0; i < visible.size(); ++i) {
            const auto id = vocalstack::utilityParameterId(i + 1, size_t(P::synthCutoff));
            expectNear(ui::slider(*visible[i], id).getValue(), parameterValue(f.owner, id), "Remounted synth lost latest cutoff binding");
            auto* visual = ui::find<juce::Component>(*visible[i], [](auto& c) { return c.getComponentID() == "utilityVisual"; });
            const auto after = visual->createComponentSnapshot(visual->getLocalBounds());
            expect(before[i].isValid() && after.isValid() && !vocalstack::updateUiTests::samePixels(before[i], after),
                   "Settled synth visual retained its initial response after target churn");
        }
    }
    for (size_t slot = 1; slot <= 4; ++slot) targets(f.owner, slot, 20);
    refresh();
    responsive([&] { f.panel.reset(); }, "Closing routing with queued synth responses");
    targets(*secondOwner, 1, 21);
    responsive(mount, "Reopening routing while cancelled jobs drain");
    refresh(); waitReady(true, "New clients after routing destruction");
    f.expectGraph(g, "Synth response scheduling changed audio routing");
}

void profileRouting() {
    constexpr int frames = 120, columns = 5;
    constexpr std::array<NodeType, 15> effects{
        NodeType::chorale, NodeType::waveshaper, NodeType::equalizer, NodeType::crossover,
        NodeType::delay, NodeType::reverb, NodeType::compressor, NodeType::bitcrusher,
        NodeType::granular, NodeType::gain, NodeType::stereo, NodeType::gate,
        NodeType::xpFilter, NodeType::synthFilter, NodeType::synthFilter};
    Graph patch;
    patch.nodes.push_back({1, NodeType::lead});
    patch.links.push_back({1, 11, 0});
    for (int voice = 0; voice < 8; ++voice) {
        patch.nodes.push_back({uint32_t(voice + 2), NodeType::voice, voice});
        patch.links.push_back({uint32_t(voice + 2), 11, 1});
    }
    vocalstack::RoutingNode mixer{11, NodeType::mix}; mixer.mainControls = true;
    patch.nodes.push_back(mixer);
    uint32_t previous = 11;
    for (size_t slot = 0; slot < effects.size(); ++slot) {
        const auto id = uint32_t(100 + slot);
        patch.nodes.push_back({id, effects[slot]});
        patch.links.push_back({previous, id, 0}); previous = id;
    }
    patch.nodes.push_back({13, NodeType::output}); patch.links.push_back({previous, 13, 0});
    std::cout << "Routing UI profile: Device, 1600x1000 panel, 100% zoom, " << patch.nodes.size()
              << " nodes (15 FX including two Synth filters, 8 voices, Lead, mixer, Output).\n"
              << "Each pass: " << frames << " serial pan + pending-timers + full-panel snapshot frames; "
              << "16 ms pacing sleep excluded. Includes virtualization/mounting and snapshot allocation; "
              << "background curve work may overlap. Informational message-thread timings only; "
              << "not audio/DSP performance, native presentation FPS, or a pass/fail budget.\n";
    for (const bool open : {false, true}) {
        Fixture f(patch, false);
        f.panel.reset();
        f.owner.setAdvancedEnabled(true);
        for (size_t voice = 0; voice < 8; ++voice) setParameter(f.owner, vocalstack::voicePresentParameterId(voice), 1);
        juce::ValueTree layout("RoutingLayout");
        layout.setProperty("zoom", 1.f, nullptr);
        layout.setProperty("panX", 0.f, nullptr); layout.setProperty("panY", 0.f, nullptr);
        layout.setProperty("inspectorVisible", false, nullptr); layout.setProperty("selectedNode", 0, nullptr);
        std::vector<juce::Point<float>> route;
        for (size_t i = 0; i < patch.nodes.size(); ++i) {
            const int row = int(i) / columns, offset = int(i) % columns;
            const int column = row % 2 == 0 ? offset : columns - 1 - offset;
            const juce::Point<float> position(20.f + column * 580.f, 20.f + row * 580.f);
            juce::ValueTree node("NODE");
            node.setProperty("id", int(patch.nodes[i].id), nullptr);
            node.setProperty("x", position.x, nullptr); node.setProperty("y", position.y, nullptr);
            node.setProperty("expanded", open, nullptr); layout.addChild(node, -1, nullptr);
            route.push_back(position + juce::Point<float>(258, 280));
        }
        f.owner.setRoutingLayout(layout);
        f.panel = std::make_unique<vocalstack::RoutingPanel>(f.owner);
        f.panel->setSize(1600, 1000); f.panel->addToDesktop(0); f.panel->setVisible(true);
        std::vector<double> samples;
        samples.reserve(frames);
        int maxMounted = f.canvas().getNumChildComponents();
        for (int frame = 0; frame < frames; ++frame) {
            // Sweep every node in serpentine order, with the same view trajectory
            // and world positions in both passes. Do not Fit the expanded patch.
            const float progress = float(frame) * float(route.size() - 1) / float(frames - 1);
            const auto index = juce::jmin(size_t(progress), route.size() - 2);
            const auto centre = route[index] + (route[index + 1] - route[index]) * (progress - float(index));
            const auto desiredPan = f.canvas().getLocalBounds().getCentre().toFloat() - centre;
            const auto saved = f.owner.getRoutingLayout();
            const juce::Point<float> currentPan(float(saved.getProperty("panX")), float(saved.getProperty("panY")));
            juce::Thread::sleep(16);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            pan(f, desiredPan - currentPan);
            maxMounted = juce::jmax(maxMounted, f.canvas().getNumChildComponents());
            juce::Timer::callPendingTimersSynchronously();
            {
                const auto image = f.panel->createComponentSnapshot(f.panel->getLocalBounds(), true, 1.f);
                expect(image.isValid(), "Routing profile snapshot failed");
            }
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;
            maxMounted = juce::jmax(maxMounted, f.canvas().getNumChildComponents());
            samples.push_back(elapsed);
        }
        std::sort(samples.begin(), samples.end());
        const double median = (samples[frames / 2 - 1] + samples[frames / 2]) * .5;
        const auto p95 = size_t(std::ceil(samples.size() * .95)) - 1;
        std::cout << (open ? "Expanded" : "Compact") << ": frames=" << samples.size()
                  << " median=" << juce::String(median, 3) << " ms p95=" << juce::String(samples[p95], 3)
                  << " ms max=" << juce::String(samples.back(), 3) << " ms max-mounted-cards=" << maxMounted << std::endl;
    }
}

void screenshots() {
    for (int theme = 0; theme < 3; ++theme) {
        const auto suffix = "-" + vocalstack::updateUiTests::themeName(theme) + ".png";
        Graph pair{{{1, NodeType::lead}, {13, NodeType::output}}, {{1, 13, 0}}};
        Fixture overview(pair, false); routingExpansion::theme(overview, theme); overview.panel->setSize(1600, 1000);
        move(overview, 1, {20, 20}); move(overview, 13, {510, 20});
        routingPolish::action(*overview.panel, "Expand all");
        expectNear(float(overview.owner.getRoutingLayout().getProperty("zoom")), 1, "Expanded overview must use legible 100% zoom");
        binding(overview, "leadGain"); binding(overview, "outputGain");
        saveSnapshot(*overview.panel, "routing-expanded-overview" + suffix);
        for (int width : {904, 1800}) {
            Fixture packed(graph(), false); routingExpansion::theme(packed, theme); packed.panel->setSize(width, 800);
            routingPolish::action(*packed.panel, "Expand all"); nonoverlap(packed);
            saveSnapshot(*packed.panel, "routing-packed-" + juce::String(width) + suffix);
        }
        for (auto type : {NodeType::xpFilter, NodeType::reverb, NodeType::waveshaper}) {
            Fixture f(vocalstack::updateUiTests::effectGraph(type), false);
            routingExpansion::theme(f, theme); f.panel->setSize(1600, 1000);
            inspector(f, false); ui::click(f.canvas(), f.point(100, 175, 14)); focus(f, 100, 470);
            binding(f, "fx1Mix");
            if (type == NodeType::xpFilter) {
                auto* filter = ui::find<vocalstack::UtilityEffectPanel>(f.canvas(), [](auto&) { return true; });
                expect(filter != nullptr, "Expanded filter render missing body");
                vocalstack::updateUiTests::waitFor([&] { return filter->responseReady(); }, "Expanded routing filter response not ready");
            }
            if (auto* viewport = ui::find<juce::Viewport>(f.canvas(), [](auto&) { return true; })) viewport->setViewPosition(0, 0);
            const juce::String name = type == NodeType::xpFilter ? "xp-filter" : type == NodeType::reverb ? "reverb" : "waveshaper";
            saveSnapshot(*f.panel, "routing-expanded-" + name + suffix);
        }
    }
}
} // namespace routingExpansion
