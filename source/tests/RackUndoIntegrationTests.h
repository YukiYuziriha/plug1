#pragma once

#include "MainBusFxRackPanel.h"
#include "ProcessorRoutingState.h"
#include "RoutingPanel.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

// Coordinator registration: include at file scope in EditorTests.cpp and add
// {"Shared rack/Routing Undo integration", [] {
//     expect(vocalstack::rackUndoIntegrationTests::run(), "Rack Undo integration failed");
// }} to its test table (and increase the table size). No separate CMake target.
// Run on the message thread after ScopedJuceInitialiser_GUI. Public APIs only.
namespace vocalstack::rackUndoIntegrationTests {
inline void require(bool ok, const juce::String& message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
inline juce::String encoded(const Graph& graph) {
    return processorRouting::encode(graph, "Graph").toXmlString();
}
inline juce::Component* find(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren()) if (auto* result = find(*child, id)) return result;
    return nullptr;
}
inline juce::Button& button(juce::Component& root, const juce::String& text) {
    for (auto* child : root.getChildren())
        if (auto* result = dynamic_cast<juce::Button*>(child); result && result->getButtonText() == text) return *result;
    throw std::runtime_error(("Missing Routing action: " + text).toStdString());
}
inline void write(Processor& owner, const juce::String& id, float value) {
    auto* parameter = owner.parameters.getParameter(id);
    require(parameter != nullptr, "Missing parameter: " + id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
using Parameters = std::vector<std::pair<juce::String, float>>;
inline Parameters snapshot(Processor& owner, size_t slot) {
    Parameters values;
    const auto add = [&](const juce::String& id) {
        const auto* value = owner.parameters.getRawParameterValue(id);
        require(value != nullptr, "Missing snapshot parameter: " + id);
        values.emplace_back(id, value->load());
    };
    for (size_t field = 0; field < 3; ++field) add(fxParameterId(slot, field));
    for (size_t field = 0; field < toneParametersPerSlot; ++field) add(parameterIds[toneParameterIndex(slot, field)]);
    for (size_t field = 0; field < creativeParameterCount; ++field) add(creativeParameterId(slot, field));
    for (size_t field = 0; field < utilityParameterCount; ++field) add(utilityParameterId(slot, field));
    for (size_t field = 0; field < 2; ++field) add(parameterIds[waveExtraParameterStart + (slot - 1) * 2 + field]);
    add(toneParameterId(slot, "WaveQuality")); add(toneParameterId(slot, "WaveLevelMatch"));
    return values;
}
inline void unchanged(Processor& owner, const Parameters& expected, const juce::String& context) {
    for (const auto& [id, value] : expected) {
        const auto* actual = owner.parameters.getRawParameterValue(id);
        require(actual && std::isfinite(actual->load()) && std::abs(actual->load() - value) < .0001f,
                context + ": parameter changed: " + id);
    }
}
struct Fixture {
    std::unique_ptr<Processor> storage = std::make_unique<Processor>();
    Processor& owner = *storage;
    RoutingPanel routing{owner};
    MainBusFxRackPanel rack{owner};
    Fixture() {
        routing.setVisible(false);
        rack.commitGraph = [this](const Graph& graph, juce::String& error) {
            return routing.commitExternalGraphEdit(graph, error);
        };
        rack.setBounds(0, 0, 904, 600); rack.sync();
    }
    Graph graph() const { return owner.getRoutingGraph(); }
    void install(const Graph& graph) {
        juce::String error;
        require(owner.setRoutingGraph(graph, error), "External graph rejected: " + error);
    }
    void commit(const mainBusFx::Edit& edit) {
        require(edit.changed && edit.error.empty(), "Rack helper refused edit: " + juce::String(edit.error));
        juce::String error = "stale rejection";
        require(rack.commitGraph(edit.graph, error), "Rack bridge rejected edit: " + error);
        require(error.isEmpty(), "Successful bridge left a rejection reason");
        rack.sync();
    }
    void action(const juce::String& text) {
        auto& target = button(routing, text);
        require(target.isEnabled() && bool(target.onClick), "Unavailable history/action: " + text);
        target.onClick(); rack.sync();
    }
    void expectGraph(const Graph& expected, const juce::String& context) {
        require(encoded(graph()) == encoded(expected), context);
    }
    void card(uint32_t id, bool present) {
        require((find(rack, "mainBusFx" + juce::String(id)) != nullptr) == present,
                "Rack view disagrees with shared node " + juce::String(id));
    }
    void select(uint32_t id) {
        routing.setVisible(true);
        const auto layout = owner.getRoutingLayout();
        const auto node = layout.getChildWithProperty("id", int(id));
        require(node.isValid(), "Missing Routing node position");
        const float zoom = float(layout.getProperty("zoom"));
        const juce::Point<float> point{(float(node.getProperty("x")) + 60) * zoom + float(layout.getProperty("panX")),
                                       (float(node.getProperty("y")) + 15) * zoom + float(layout.getProperty("panY"))};
        auto* canvas = routing.getChildComponent(0);
        require(canvas != nullptr, "Missing Routing canvas");
        const auto event = [&](int modifiers) {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(), point,
                juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, canvas, canvas, now, point, now, 1, false};
        };
        canvas->mouseDown(event(juce::ModifierKeys::leftButtonModifier));
        canvas->mouseUp(event(0));
    }
};

inline void addRemoveReorder() {
    Fixture f;
    const auto initial = f.graph();
    f.commit(mainBusFx::add(f.graph(), NodeType::granular));
    const auto added = f.graph(); f.card(100, true);
    f.action("Undo"); f.expectGraph(initial, "Add must undo in one step"); f.card(100, false);
    require((f.owner.spentFxSlots() & (1u << 1)) != 0, "Undoing an add released an exposed slot");
    require(!button(f.routing, "Undo").isEnabled(), "Add recorded multiple undo steps");
    f.action("Redo"); f.expectGraph(added, "Add must redo in one step"); f.card(100, true);
    require(!button(f.routing, "Redo").isEnabled(), "Add recorded multiple redo steps");

    f.commit(mainBusFx::add(f.graph(), NodeType::granular));
    const auto beforeMove = f.graph();
    auto* firstCard = find(f.rack, "mainBusFx100");
    auto* secondCard = find(f.rack, "mainBusFx101");
    const auto pitch1 = creativeParameterId(1, size_t(CreativeParameter::granularPitch));
    const auto pitch2 = creativeParameterId(2, size_t(CreativeParameter::granularPitch));
    auto staleMove = mainBusFx::move(beforeMove, 101, 0);
    write(f.owner, pitch1, 5); write(f.owner, pitch2, -12);
    write(f.owner, fxParameterId(1, "Mix"), 37);
    const auto first = snapshot(f.owner, 1), second = snapshot(f.owner, 2);
    f.commit(staleMove);
    const auto moved = f.graph();
    auto expectedOrder = mainBusFx::discover(beforeMove).effects;
    expectedOrder.pop_back(); expectedOrder.insert(expectedOrder.begin(), 101);
    require(mainBusFx::discover(moved).effects == expectedOrder, "Reorder changed IDs or order");
    unchanged(f.owner, first, "Stale candidate replayed surviving bases");
    unchanged(f.owner, second, "Reorder changed duplicate bases");
    require(find(f.rack, "mainBusFx100") == firstCard && find(f.rack, "mainBusFx101") == secondCard,
            "Reorder replaced surviving rack cards");
    f.action("Undo");
    require(mainBusFx::discover(f.graph()).effects == mainBusFx::discover(beforeMove).effects, "Reorder undo is not one step");
    unchanged(f.owner, first, "Reorder undo rewound parameters"); unchanged(f.owner, second, "Reorder undo rewound duplicate");
    // Automation after Undo must also survive Redo, not replay its historical base.
    write(f.owner, pitch1, 8); const auto automated = snapshot(f.owner, 1);
    f.action("Redo");
    require(mainBusFx::discover(f.graph()).effects == mainBusFx::discover(moved).effects, "Reorder redo is not one step");
    unchanged(f.owner, automated, "Reorder redo rewound automation");

    const auto beforeRemove = f.graph();
    f.commit(mainBusFx::remove(f.graph(), 100)); const auto removed = f.graph(); f.card(100, false);
    f.action("Undo"); f.expectGraph(beforeRemove, "Remove must undo in one step"); f.card(100, true);
    unchanged(f.owner, automated, "Remove undo lost parameter snapshot");
    f.action("Redo"); f.expectGraph(removed, "Remove must redo in one step"); f.card(100, false);
    unchanged(f.owner, second, "Remove touched surviving duplicate");
}

inline void crossViewHistory() {
    Fixture f; const auto initial = f.graph();
    f.commit(mainBusFx::add(f.graph(), NodeType::granular));
    write(f.owner, creativeParameterId(1, size_t(CreativeParameter::granularPitch)), 11);
    const auto values = snapshot(f.owner, 1);
    const auto withFx = f.graph();
    f.select(100); f.action("Delete node"); f.card(100, false);
    require(!mainBusFx::find(f.graph(), 100), "Routing Delete did not remove rack node");
    const auto deleted = f.graph();
    f.action("Undo"); f.expectGraph(withFx, "Routing Delete undo did not restore rack addition"); f.card(100, true);
    unchanged(f.owner, values, "Cross-view delete undo lost parameters");
    f.action("Undo"); f.expectGraph(initial, "Second Undo did not reach preceding rack edit"); f.card(100, false);
    require(!button(f.routing, "Undo").isEnabled(), "Cross-view edits did not share exactly two history entries");
    f.action("Redo"); f.expectGraph(withFx, "Shared Redo did not restore rack edit"); f.card(100, true);
    unchanged(f.owner, values, "Rack redo lost edited lifecycle values");
    f.action("Redo"); f.expectGraph(deleted, "Shared Redo did not restore Routing Delete"); f.card(100, false);
    require(!button(f.routing, "Redo").isEnabled(), "Cross-view redo left extra steps");
}

inline void duplicateSlotsStaySpent() {
    Fixture f;
    f.commit(mainBusFx::add(f.graph(), NodeType::granular));
    f.commit(mainBusFx::add(f.graph(), NodeType::granular));
    write(f.owner, creativeParameterId(1, size_t(CreativeParameter::granularPitch)), 9);
    write(f.owner, creativeParameterId(2, size_t(CreativeParameter::granularPitch)), -4);
    write(f.owner, fxParameterId(1, "On"), 0); write(f.owner, fxParameterId(1, "Mix"), 43);
    // Dormant banks are part of the same lifecycle snapshot, too.
    write(f.owner, toneParameterId(1, "WaveDrive"), 7);
    write(f.owner, toneParameterId(1, "WaveShapeAmount"), 64);
    write(f.owner, toneParameterId(1, "WaveQuality"), 2);
    write(f.owner, toneParameterId(1, "WaveLevelMatch"), 0);
    const auto old = snapshot(f.owner, 1), survivor = snapshot(f.owner, 2);
    const auto original = f.graph();
    f.commit(mainBusFx::remove(f.graph(), 100));
    require((f.owner.spentFxSlots() & (1u << 1)) != 0, "Deleting FX released its automation slot");
    juce::String error;
    require(!f.owner.setRoutingGraph(original, error), "Normal graph edit restored a retired slot");
    f.action("Undo"); f.expectGraph(original, "Undo removal failed to restore original duplicate ID/type");
    unchanged(f.owner, old, "Undo lost original parameter banks");
    unchanged(f.owner, survivor, "Undo touched surviving duplicate");
    f.action("Redo"); require(!mainBusFx::find(f.graph(), 100), "Redo removal must be one step");
    require((f.owner.spentFxSlots() & (1u << 1)) != 0, "Redo released the original slot");
    unchanged(f.owner, survivor, "Redo touched surviving duplicate");
}

inline void hiddenRefreshValidationAndCustomGraph() {
    Fixture f;
    f.commit(mainBusFx::add(f.graph(), NodeType::delay)); // History invalidated by the external graph below.
    auto custom = f.graph();
    custom.nodes.push_back({114, NodeType::gain});
    for (auto& link : custom.links) if (link.source == 1 && link.destination == 11) link.source = 114;
    custom.links.push_back({1, 114, 0, 0});
    f.install(custom);
    write(f.owner, fxParameterId(15, "Mix"), 61);
    custom = f.graph(); const auto upstream = snapshot(f.owner, 15);
    auto layout = f.owner.getRoutingLayout().createCopy();
    layout.setProperty("inspectorWidth", 437, nullptr);
    juce::ValueTree position("NODE"); position.setProperty("id", 114, nullptr);
    position.setProperty("x", 123, nullptr); position.setProperty("y", 456, nullptr);
    position.setProperty("name", "Custom upstream gain", nullptr); layout.addChild(position, -1, nullptr);
    f.owner.setRoutingLayout(layout);
    require(!f.routing.isVisible(), "Fixture must exercise hidden Routing without timer refresh");
    f.commit(mainBusFx::add(f.graph(), NodeType::granular));
    const auto edited = f.graph();
    const auto saved = f.owner.getRoutingLayout().getChildWithProperty("id", 114);
    require(saved.getProperty("name").toString() == "Custom upstream gain" && float(saved.getProperty("x")) == 123,
            "Hidden bridge snapshotted stale layout");
    unchanged(f.owner, upstream, "Rack edit rewrote custom upstream FX");
    f.action("Undo"); f.expectGraph(custom, "Hidden bridge snapshotted stale graph instead of custom graph");
    require(!button(f.routing, "Undo").isEnabled(), "External graph failed to invalidate old history");
    require(int(f.owner.getRoutingLayout().getProperty("inspectorWidth")) == 437, "Undo lost refreshed panel sizing");

    auto invalid = custom; invalid.links.push_back({13, 11, 0, 0});
    juce::String error;
    require(!f.rack.commitGraph(invalid, error) && error.isNotEmpty(), "Invalid topology must return a rejection reason");
    f.expectGraph(custom, "Rejected candidate changed custom graph");
    require(!button(f.routing, "Undo").isEnabled() && button(f.routing, "Redo").isEnabled(),
            "Validation recorded history or destroyed redo");
    // Backend topology validation accepts this, but processor slot mapping must reject it.
    auto unmapped = custom; unmapped.nodes.push_back({199, NodeType::gain});
    require(bool(RoutingExecutor::validate(unmapped)), "Rollback fixture must pass topology validation");
    require(!f.rack.commitGraph(unmapped, error) && error.isNotEmpty(), "Processor rejection was not propagated");
    f.expectGraph(custom, "publish rollback changed the live custom graph");
    require(!button(f.routing, "Undo").isEnabled() && button(f.routing, "Redo").isEnabled(),
            "publish rollback left phantom history or destroyed redo");
    f.action("Redo"); f.expectGraph(edited, "Rejected candidate corrupted pending redo");
    unchanged(f.owner, upstream, "Undo/Redo rewrote custom upstream parameters");

    auto branched = f.graph(); branched.links.push_back({11, 13, 0, 0});
    f.install(branched); f.rack.sync();
    const auto before = encoded(f.graph());
    const auto refused = mainBusFx::remove(f.graph(), 100);
    require(!refused.changed && !refused.error.empty(), "Rack must refuse ambiguous main-bus rewrite");
    require(encoded(f.graph()) == before && !find(f.rack, "mainBusAddFx")->isEnabled(), "Custom branch was flattened by rack refresh");
}

inline void modulationState(Processor& owner, const ModulationState& state) {
    juce::String error;
    require(owner.setModulationState(state, error), "Modulation fixture rejected: " + error);
}
inline void assignmentsEqual(Processor& owner, const std::vector<Assignment>& expected, const juce::String& context) {
    const auto actual = owner.getModulationState().assignments;
    require(actual.size() == expected.size(), context + ": assignment count changed");
    for (const auto& wanted : expected) {
        const auto found = std::find_if(actual.begin(), actual.end(), [&](const Assignment& a) {
            return a.modulatorId == wanted.modulatorId && a.targetParameterId == wanted.targetParameterId;
        });
        require(found != actual.end() && found->enabled == wanted.enabled && std::abs(found->depth - wanted.depth) < .0001f,
                context + ": lost/rewound assignment " + wanted.targetParameterId);
    }
}
inline void modulationRemovalHistory() {
    for (const bool routingDelete : {false, true}) {
        Fixture f;
        auto initial = mainBusFx::add(defaultGraph(), NodeType::granular).graph;
        initial.nodes.push_back({109, NodeType::gain}); // fx10 must not be captured by fx1's prefix.
        f.install(initial);
        f.commit(mainBusFx::add(f.graph(), NodeType::granular));
        const auto removedTarget = creativeParameterId(1, size_t(CreativeParameter::granularPitch));
        const auto survivorTarget = creativeParameterId(2, size_t(CreativeParameter::granularPitch));
        ModulationState state; Modulator first; first.id = 1; Modulator second; second.id = 2;
        state.modulators = {first, second};
        const std::vector<Assignment> removed{{1, removedTarget, .32f, true}, {2, "fx1Mix", -.21f, false}};
        state.assignments = removed;
        state.assignments.push_back({1, survivorTarget, .14f, true});
        state.assignments.push_back({2, "leadPan", .12f, true});
        state.assignments.push_back({1, "fx10Mix", .13f, true});
        modulationState(f.owner, state);
        if (routingDelete) { f.select(100); f.action("Delete node"); }
        else f.commit(mainBusFx::remove(f.graph(), 100));
        auto live = f.owner.getModulationState();
        require(live.assignments.size() == 3, "Node removal did not retire exactly its own maps");
        // Delete a surviving mapping, edit an unrelated mapping and source controls,
        // and add another mapping while the removed node is absent.
        live.assignments = {{2, "leadPan", .67f, false}, {1, "fx10Mix", -.48f, true}, {2, "double", .42f, true}};
        live.modulators[0].rateHz = .73f; live.modulators[1].depth = .27f;
        modulationState(f.owner, live);
        auto restored = live.assignments; restored.insert(restored.end(), removed.begin(), removed.end());
        f.action("Undo"); f.card(100, true);
        assignmentsEqual(f.owner, restored, "Removal Undo must merge only removed-node mappings");
        const auto sources = f.owner.getModulationState().modulators;
        require(sources.size() == 2 && sources[0].rateHz == .73f && sources[1].depth == .27f,
                "Removal Undo rewound modulator edits");
        // An edit to the restored map becomes the next lifecycle snapshot, while
        // unrelated changes made after Undo must also survive subsequent Redo.
        auto edited = f.owner.getModulationState();
        for (auto& a : edited.assignments) {
            if (a.targetParameterId == removedTarget) a.depth = -.56f;
            if (a.targetParameterId == "leadPan") a.depth = -.19f;
        }
        modulationState(f.owner, edited);
        auto latestUnrelated = live.assignments; latestUnrelated[0].depth = -.19f;
        f.action("Redo"); f.card(100, false);
        assignmentsEqual(f.owner, latestUnrelated, "Removal Redo must retain unrelated edits");
        auto latestRestored = latestUnrelated;
        auto updatedRemoved = removed; updatedRemoved[0].depth = -.56f;
        latestRestored.insert(latestRestored.end(), updatedRemoved.begin(), updatedRemoved.end());
        f.action("Undo");
        assignmentsEqual(f.owner, latestRestored, "Repeated Undo lost latest removed-node snapshot");
    }
}
inline void modulationRejectsReplacement() {
    Fixture f;
    f.commit(mainBusFx::add(f.graph(), NodeType::granular));
    const auto pitch = creativeParameterId(1, size_t(CreativeParameter::granularPitch));
    ModulationState state; Modulator source; source.id = 1; state.modulators.push_back(source);
    state.assignments = {{1, pitch, -.31f, true}, {1, "leadPan", .18f, true}};
    modulationState(f.owner, state);
    auto replacement = f.graph();
    for (auto& n : replacement.nodes) if (n.id == 100) n.type = NodeType::waveshaper;
    juce::String error;
    require(!f.rack.commitGraph(replacement, error) && error.isNotEmpty(), "In-place replacement retargeted FX automation");
    assignmentsEqual(f.owner, state.assignments, "Rejected edit mutated mappings");
    f.commit(mainBusFx::remove(f.graph(), 100));
    assignmentsEqual(f.owner, {{1, "leadPan", .18f, true}}, "Delete did not retire the outgoing map");
    f.action("Undo");
    assignmentsEqual(f.owner, state.assignments, "Undo did not restore the original owner's mappings");
}
inline void modulationNonLifecycleAndDeletedSource() {
    Fixture f;
    f.commit(mainBusFx::add(f.graph(), NodeType::granular));
    const auto pitch = creativeParameterId(1, size_t(CreativeParameter::granularPitch));
    ModulationState state; Modulator source; source.id = 1; state.modulators.push_back(source);
    state.assignments = {{1, pitch, .22f, true}}; modulationState(f.owner, state);
    f.commit(mainBusFx::move(f.graph(), 100, 0));
    auto changed = state; changed.assignments = {{1, pitch, -.63f, false}, {1, "leadPan", .39f, true}};
    modulationState(f.owner, changed);
    f.action("Undo"); assignmentsEqual(f.owner, changed.assignments, "Reorder Undo rewound mapping edits");
    f.action("Redo"); assignmentsEqual(f.owner, changed.assignments, "Reorder Redo rewound mapping edits");
    f.commit(mainBusFx::remove(f.graph(), 100));
    modulationState(f.owner, {}); // Independent source deletion must not be undone by graph history.
    f.action("Undo"); f.card(100, true);
    require(f.owner.getModulationState().modulators.empty(), "Graph Undo resurrected a deleted modulator");
    assignmentsEqual(f.owner, {}, "Graph Undo restored a dangling mapping to a deleted source");
}

inline bool run() {
    struct Test { const char* name; void (*call)(); };
    const Test tests[]{{"add/remove/reorder one-step history and live parameters", addRemoveReorder},
                       {"Routing/rack cross-view shared history", crossViewHistory},
                       {"retired slots and original lifecycle parameter banks", duplicateSlotsStaySpent},
                       {"hidden refresh, validation, redo and custom graph", hiddenRefreshValidationAndCustomGraph},
                       {"assigned rack/Routing removal merges lifecycle mappings", modulationRemovalHistory},
                       {"reject in-place replacement without changing modulation", modulationRejectsReplacement},
                       {"non-lifecycle maps and deleted sources stay live", modulationNonLifecycleAndDeletedSource}};
    bool passed = true;
    for (const auto& test : tests) {
        try { test.call(); std::cout << "PASS RackUndo: " << test.name << '\n'; }
        catch (const std::exception& error) { passed = false; std::cerr << "FAIL RackUndo: " << test.name << ": " << error.what() << '\n'; }
    }
    return passed;
}
} // namespace vocalstack::rackUndoIntegrationTests
