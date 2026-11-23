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
bool hasLink(const Graph& graph, uint32_t source, uint32_t destination, int inputPin = 0, int sourcePin = 0) {
    return std::any_of(graph.links.begin(), graph.links.end(), [&](const auto& link) {
        return link.source == source && link.destination == destination
            && link.inputPin == inputPin && link.sourcePin == sourcePin;
    });
}
void expectUnchanged(const rack::Edit& edit, const Graph& original, const char* message) {
    expect(!edit.changed && !edit.error.empty() && sameLinks(edit.graph, original)
        && edit.graph.nodes.size() == original.nodes.size(), message);
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
void testMorphDonorEdits() {
    const auto added = rack::add(defaultGraph(), NodeType::spectralMorph);
    expect(added.changed && bool(RoutingExecutor::validate(added.graph)), "add valid Morph with default donor");
    auto graph = added.graph;
    expect(rack::find(graph, 14) && rack::find(graph, 14)->type == NodeType::sidechain
        && hasLink(graph, 14, 100, 1), "new Morph creates SIDECHAIN 14 feeding DONOR 1");
    expect(rack::discover(graph).editable()
        && rack::discover(graph).effects == std::vector<uint32_t>({12, 100}), "external donor does not disable rack");
    graph = rack::add(graph, NodeType::delay).graph;
    expect(hasLink(graph, 14, 100, 1) && hasLink(graph, 100, 101), "insert after donor-fed Morph preserves donor");
    const auto moved = rack::move(graph, 100, 0);
    expect(moved.changed && bool(RoutingExecutor::validate(moved.graph))
        && rack::discover(moved.graph).effects == std::vector<uint32_t>({100, 12, 101})
        && hasLink(moved.graph, 14, 100, 1), "reorder preserves incoming donor");
    const auto removed = rack::remove(graph, 100);
    expect(removed.changed && bool(RoutingExecutor::validate(removed.graph))
        && !rack::find(removed.graph, 100) && rack::find(removed.graph, 14)
        && !hasLink(removed.graph, 14, 100, 1) && hasLink(removed.graph, 12, 101), "remove Morph deletes donor cable and bridges main neighbours");
    const auto readded = rack::add(removed.graph, NodeType::spectralMorph);
    expect(readded.changed && readded.graph.nodes.size() == graph.nodes.size()
        && hasLink(readded.graph, 14, 100, 1), "reuse retained unconnected SIDECHAIN");

    // Existing explicit donors and saved source fanout are not defaulted or flattened.
    graph.nodes.push_back({200, NodeType::gain});
    graph.nodes.push_back({201, NodeType::sum});
    for (auto& link : graph.links)
        if (link.source == 14 && link.destination == 100) link.source = 200;
    graph.links.push_back({14, 200, 0, 0});
    graph.links.push_back({14, 201, 0, 0});
    graph.links.push_back({1, 100, 1, 0});
    expect(bool(RoutingExecutor::validate(graph)) && rack::discover(graph).editable(), "external processed and merged donors are safe");
    const auto customMoved = rack::move(graph, 100, 0);
    expect(customMoved.changed && bool(RoutingExecutor::validate(customMoved.graph))
        && hasLink(customMoved.graph, 200, 100, 1) && hasLink(customMoved.graph, 1, 100, 1)
        && hasLink(customMoved.graph, 14, 201), "reorder retains explicit donors and unrelated wiring");
    const auto customRemoved = rack::remove(graph, 100);
    expect(customRemoved.changed && bool(RoutingExecutor::validate(customRemoved.graph))
        && rack::find(customRemoved.graph, 200) && rack::find(customRemoved.graph, 14)
        && hasLink(customRemoved.graph, 14, 200) && hasLink(customRemoved.graph, 14, 201)
        && hasLink(customRemoved.graph, 12, 101), "remove preserves donor processing source and unrelated fanout");
    const auto neighbourRemoved = rack::remove(graph, 12);
    expect(neighbourRemoved.changed && hasLink(neighbourRemoved.graph, 200, 100, 1)
        && hasLink(neighbourRemoved.graph, 11, 100), "remove neighbour retains explicit donor");

    auto unwired = added.graph;
    unwired.links.erase(std::remove_if(unwired.links.begin(), unwired.links.end(),
        [](const auto& link) { return link.inputPin == 1 && link.destination == 100; }), unwired.links.end());
    const auto unchangedDonor = rack::add(unwired, NodeType::gain);
    expect(unchangedDonor.changed && !hasLink(unchangedDonor.graph, 14, 100, 1), "edits never default existing unwired Morph");
    auto tapped = added.graph; tapped.links.push_back({12, 100, 1, 0});
    expect(bool(RoutingExecutor::validate(tapped)) && !rack::discover(tapped).editable(), "rack output tapped as donor remains ambiguous");
    expectUnchanged(rack::remove(tapped, 100), tapped, "rack donor tap cannot be silently removed");
}
void testMorphDefaultDonorSafety() {
    auto graph = defaultGraph(); graph.nodes.push_back({14, NodeType::sidechain});
    const auto reused = rack::add(graph, NodeType::spectralMorph);
    expect(reused.changed && reused.graph.nodes.size() == graph.nodes.size() + 1
        && hasLink(reused.graph, 14, 100, 1), "reuse existing SIDECHAIN without duplication");
    graph.nodes.back().id = 42;
    const auto customId = rack::add(graph, NodeType::spectralMorph);
    expect(customId.changed && hasLink(customId.graph, 42, 100, 1)
        && !rack::find(customId.graph, 14), "reuse existing source identity");
    graph.nodes.push_back({200, NodeType::sum}); graph.links.push_back({42, 200, 0, 0});
    expectUnchanged(rack::add(graph, NodeType::spectralMorph), graph, "occupied SIDECHAIN refuses implicit fanout or cable theft");
    auto collision = defaultGraph(); collision.nodes.push_back({14, NodeType::sum});
    expectUnchanged(rack::add(collision, NodeType::spectralMorph), collision, "reserved source ID collision is non-destructive");

    auto nodes = defaultGraph();
    while (nodes.nodes.size() < size_t(RoutingExecutor::maxNodes - 2))
        nodes.nodes.push_back({uint32_t(200 + nodes.nodes.size()), NodeType::sum});
    const auto full = rack::add(nodes, NodeType::spectralMorph);
    expect(full.changed && full.graph.nodes.size() == size_t(RoutingExecutor::maxNodes), "Morph and new source fit exact node capacity");
    nodes.nodes.push_back({300, NodeType::sum});
    expectUnchanged(rack::add(nodes, NodeType::spectralMorph), nodes, "reserve capacity for source as well as Morph");
    nodes.nodes.back() = {14, NodeType::sidechain};
    expect(rack::add(nodes, NodeType::spectralMorph).changed, "one remaining node is enough with existing source");

    auto links = defaultGraph();
    for (uint32_t id = 200; links.links.size() < size_t(RoutingExecutor::maxLinks - 2); ++id) {
        links.nodes.push_back({id, NodeType::sum});
        for (int pin = 0; pin < 16 && links.links.size() < size_t(RoutingExecutor::maxLinks - 2); ++pin)
            links.links.push_back({1, id, pin, 0});
    }
    expect(bool(RoutingExecutor::validate(links)), "link capacity fixture valid");
    const auto atLimit = rack::add(links, NodeType::spectralMorph);
    expect(atLimit.changed && atLimit.graph.links.size() == size_t(RoutingExecutor::maxLinks), "reserve main and donor links at exact capacity");
    links.nodes.push_back({300, NodeType::sum}); links.links.push_back({1, 300, 0, 0});
    expectUnchanged(rack::add(links, NodeType::spectralMorph), links, "insufficient donor link capacity leaves graph untouched");
    expect(rack::add(links, NodeType::gain).changed, "ordinary effect only needs one extra link");
    expectUnchanged(rack::add(reused.graph, NodeType::spectralMorph), reused.graph, "second serial Morph rejected without altering first donor");
    auto serial = reused.graph;
    serial.links.erase(std::remove_if(serial.links.begin(), serial.links.end(),
        [](const auto& link) { return link.destination == 100 && link.inputPin == 1; }), serial.links.end());
    const auto overBudget = rack::add(serial, NodeType::spectralMorph);
    expectUnchanged(overBudget, serial, "latency rejection rolls back new Morph and default donor cable");
    expect(overBudget.error.find("4096") != std::string::npos, "serial Morph retains executor latency restriction");
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
    try { testSerialEdits(); testCustomGraphSafety(); testMorphDonorEdits(); testMorphDefaultDonorSafety(); testCapacityAndReuse(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Main Bus FX rack topology tests passed\n";
}
