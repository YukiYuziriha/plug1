#include "MainBusFxRack.h"
#include <iostream>
#include <stdexcept>

using namespace vocalstack;
namespace rack = vocalstack::mainBusFx;
namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool sameLinks(const Graph& a, const Graph& b) {
    if (a.links.size() != b.links.size()) return false;
    for (size_t i = 0; i < a.links.size(); ++i) {
        const auto& x = a.links[i]; const auto& y = b.links[i];
        if (x.source != y.source || x.destination != y.destination || x.sourcePin != y.sourcePin || x.inputPin != y.inputPin) return false;
    }
    return true;
}
void testSerialEdits() {
    auto graph = defaultGraph();
    expect(rack::discover(graph).effects == std::vector<uint32_t>{12}, "default main Chorale");
    auto first = rack::add(graph, NodeType::granular);
    expect(first.changed && first.error.empty(), "add Granular");
    auto second = rack::add(first.graph, NodeType::granular);
    expect(second.changed, "duplicate Granular"); graph = second.graph;
    expect(rack::discover(graph).effects == std::vector<uint32_t>({12, 100, 101}), "shared stable slot IDs");
    graph.nodes.back().creative[CreativeParameter::granularPitch] = 7;
    const auto moved = rack::move(graph, 101, 0);
    expect(moved.changed && bool(RoutingExecutor::validate(moved.graph)), "valid reorder");
    expect(rack::discover(moved.graph).effects == std::vector<uint32_t>({101, 12, 100}), "signal order follows drag");
    expect(rack::find(moved.graph, 101)->creative[CreativeParameter::granularPitch] == 7, "reorder retains node settings");
    expect(rack::find(moved.graph, 100)->id == 100, "surviving automation identity unchanged");
    expect(!rack::move(moved.graph, 101, 0).changed, "no-op reorder does not publish");
    const auto removed = rack::remove(moved.graph, 12);
    expect(removed.changed && !rack::find(removed.graph, 12), "remove legacy card");
    expect(rack::discover(removed.graph).effects == std::vector<uint32_t>({101, 100}), "remove bridges only serial neighbours");
    const auto empty = rack::remove(rack::remove(removed.graph, 101).graph, 100);
    expect(rack::discover(empty.graph).editable() && rack::discover(empty.graph).effects.empty(), "empty main bus remains editable");
    expect(rack::add(empty.graph, NodeType::reverb).changed, "insert into direct Mix Output");
    expect(!rack::add(graph, NodeType::splitter).changed && !rack::add(graph, NodeType::crossover).changed, "no structural rack nodes");
}
void testCustomGraphSafety() {
    auto graph = rack::add(defaultGraph(), NodeType::delay).graph;
    graph.links.push_back({11, 13, 0, 0}); // deliberate dry parallel branch
    expect(bool(RoutingExecutor::validate(graph)), "parallel graph is legal Routing");
    const auto segment = rack::discover(graph);
    expect(!segment.editable() && !segment.unavailable.empty(), "parallel path explained");
    expect(segment.effects == std::vector<uint32_t>({12, 100}), "identifiable branch FX remain visible");
    for (const auto& edit : {rack::add(graph, NodeType::reverb), rack::remove(graph, 12), rack::move(graph, 100, 0)})
        expect(!edit.changed && sameLinks(edit.graph, graph) && edit.graph.nodes.size() == graph.nodes.size(), "custom graph never flattened");
    auto perVoice = defaultGraph();
    perVoice.nodes.push_back({100, NodeType::delay});
    perVoice.links.erase(perVoice.links.begin());
    perVoice.links.push_back({2, 100, 0, 0}); perVoice.links.push_back({100, 11, 1, 0});
    expect(rack::discover(perVoice).effects == std::vector<uint32_t>{12}, "per-voice FX are not rack FX");
    auto sideTap = defaultGraph(); sideTap.nodes.push_back({100, NodeType::reverb}); sideTap.links.push_back({12, 100, 0, 0});
    expect(!rack::discover(sideTap).editable(), "even a dead-end side tap blocks destructive rewrite");
    auto merged = defaultGraph(); merged.links.push_back({1, 12, 0, 0});
    expect(!rack::discover(merged).editable(), "side input into rack blocks rewriting");
    auto structural = defaultGraph(); structural.nodes[10].type = NodeType::splitter; structural.nodes[10].mainControls = false;
    expect(!rack::discover(structural).editable(), "splitter interrupts serial segment");
    auto ambiguous = defaultGraph(); RoutingNode mix{200, NodeType::mix}; mix.mainControls = true; ambiguous.nodes.push_back(mix);
    expect(!rack::discover(ambiguous).editable(), "multiple main Mix anchors rejected");
    auto missing = defaultGraph(); missing.nodes[9].mainControls = false;
    expect(!rack::discover(missing).editable(), "missing main Mix rejected");
}
void testCapacityAndReuse() {
    auto graph = defaultGraph();
    for (int i = 0; i < 15; ++i) {
        const auto edit = rack::add(graph, NodeType::gain); expect(edit.changed, "shared pool has free slot"); graph = edit.graph;
    }
    expect(!rack::capacityProblem(graph).empty(), "shared capacity explained");
    expect(!rack::add(graph, NodeType::granular).changed, "rack cannot exceed FX allowance");
    graph = rack::remove(graph, 105).graph;
    const auto reused = rack::add(graph, NodeType::granular);
    expect(reused.changed && rack::find(reused.graph, 105)->type == NodeType::granular, "reuse vacant shared slot only");
    expect(rack::find(reused.graph, 105)->creative[CreativeParameter::granularPitch] == -7, "new slot settings start at defaults");
}
}
int main() {
    try { testSerialEdits(); testCustomGraphSafety(); testCapacityAndReuse(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Main Bus FX rack topology tests passed\n";
}
