#pragma once

#include "Routing.h"
#include <algorithm>

namespace vocalstack::mainBusFx {

inline bool eligible(NodeType type) noexcept {
    return type == NodeType::chorale || type == NodeType::waveshaper
        || type == NodeType::equalizer || isCreativeEffect(type) || isUtilityEffect(type);
}
inline const RoutingNode* find(const Graph& graph, uint32_t id) {
    for (const auto& node : graph.nodes) if (node.id == id) return &node;
    return nullptr;
}
struct Segment {
    uint32_t mix = 0, output = 0;
    std::vector<uint32_t> effects;
    std::string unavailable;
    bool editable() const noexcept { return unavailable.empty(); }
};

// Discovery never treats an arbitrary topological sort as a serial signal path.
// In a custom graph the reachable intersection is display-only, not a rewrite rule.
inline Segment discover(const Graph& graph) {
    Segment result;
    if (const auto validation = RoutingExecutor::validate(graph); !validation) {
        result.unavailable = "Invalid routing: " + validation.message; return result;
    }
    int mixes = 0, outputs = 0;
    for (const auto& node : graph.nodes) {
        if (node.type == NodeType::mix && node.mainControls) { result.mix = node.id; ++mixes; }
        if (node.type == NodeType::output) { result.output = node.id; ++outputs; }
    }
    if (mixes != 1 || outputs != 1) {
        result.unavailable = "Rack needs one main Mix and one Output. Edit routing to choose a serial path.";
        return result;
    }
    std::vector<uint32_t> downstream{result.mix}, upstream{result.output};
    const auto reachable = [&](std::vector<uint32_t>& ids, bool forward) {
        for (size_t i = 0; i < ids.size(); ++i)
            for (const auto& link : graph.links) {
                if ((forward ? link.source : link.destination) != ids[i]) continue;
                const auto id = forward ? link.destination : link.source;
                if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
            }
    };
    reachable(downstream, true); reachable(upstream, false);
    for (const auto id : downstream) {
        const auto* node = find(graph, id);
        if (node && eligible(node->type) && std::find(upstream.begin(), upstream.end(), id) != upstream.end())
            result.effects.push_back(id);
    }
    const auto fail = [&]() {
        result.unavailable = "Custom/branched main bus: add, remove and reorder unavailable. Edit FX normally or make a serial path in Routing.";
        return result;
    };
    std::vector<uint32_t> ordered;
    auto current = result.mix;
    for (size_t step = 0; step <= graph.nodes.size(); ++step) {
        const RoutingLink* next = nullptr;
        for (const auto& link : graph.links) if (link.source == current) {
            if (next || link.sourcePin != 0 || link.inputPin != 0) return fail();
            next = &link;
        }
        if (!next) return fail();
        int inputs = 0;
        for (const auto& link : graph.links) if (link.destination == next->destination) ++inputs;
        if (inputs != 1) return fail();
        current = next->destination;
        if (current == result.output) {
            for (const auto& link : graph.links) if (link.source == current) return fail();
            result.effects = std::move(ordered); return result;
        }
        const auto* node = find(graph, current);
        if (!node || !eligible(node->type)) return fail();
        ordered.push_back(current);
    }
    return fail();
}
inline std::string capacityProblem(const Graph& graph) {
    if (graph.nodes.size() >= size_t(RoutingExecutor::maxNodes)) return "Routing node capacity reached (48).";
    int effects = 0;
    for (const auto& node : graph.nodes) if (eligible(node.type) || node.type == NodeType::crossover) ++effects;
    if (effects >= RoutingExecutor::maxEffects) return "Shared routing FX capacity reached (16).";
    for (uint32_t id = 100; id <= 114; ++id) if (!find(graph, id)) return {};
    return "All 15 independent routing FX slots are occupied.";
}

inline void wire(Graph& graph, const Segment& segment, const std::vector<uint32_t>& ordered) {
    auto sources = segment.effects; sources.push_back(segment.mix);
    graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [&](const auto& link) {
        return std::find(sources.begin(), sources.end(), link.source) != sources.end();
    }), graph.links.end());
    auto previous = segment.mix;
    for (const auto id : ordered) { graph.links.push_back({previous, id, 0, 0}); previous = id; }
    graph.links.push_back({previous, segment.output, 0, 0});
}
struct Edit {
    Graph graph;
    std::string error;
    bool changed = false;
};
inline Edit add(const Graph& original, NodeType type) {
    Edit edit{original}; const auto segment = discover(original);
    if (!segment.editable()) { edit.error = segment.unavailable; return edit; }
    if (!eligible(type)) { edit.error = "Structural nodes are only available in Routing."; return edit; }
    if (auto problem = capacityProblem(original); !problem.empty()) { edit.error = problem; return edit; }
    RoutingNode node;
    node.type = type; node.mix = type == NodeType::chorale ? .25f : 1.0f;
    for (uint32_t id = 100; id <= 114; ++id) if (!find(original, id)) { node.id = id; break; }
    edit.graph.nodes.push_back(node);
    auto ordered = segment.effects; ordered.push_back(node.id); wire(edit.graph, segment, ordered);
    const auto valid = RoutingExecutor::validate(edit.graph);
    if (!valid) { edit.graph = original; edit.error = valid.message; return edit; }
    edit.changed = true; return edit;
}
inline Edit remove(const Graph& original, uint32_t id) {
    Edit edit{original}; const auto segment = discover(original);
    if (!segment.editable()) { edit.error = segment.unavailable; return edit; }
    auto ordered = segment.effects;
    const auto it = std::find(ordered.begin(), ordered.end(), id);
    if (it == ordered.end()) { edit.error = "FX is no longer in the main bus."; return edit; }
    ordered.erase(it); wire(edit.graph, segment, ordered);
    edit.graph.nodes.erase(std::remove_if(edit.graph.nodes.begin(), edit.graph.nodes.end(), [=](const auto& node) { return node.id == id; }), edit.graph.nodes.end());
    edit.changed = true; return edit;
}
inline Edit move(const Graph& original, uint32_t id, size_t index) {
    Edit edit{original}; const auto segment = discover(original);
    if (!segment.editable()) { edit.error = segment.unavailable; return edit; }
    auto ordered = segment.effects;
    const auto it = std::find(ordered.begin(), ordered.end(), id);
    if (it == ordered.end() || index >= ordered.size()) { edit.error = "FX destination is no longer available."; return edit; }
    if (size_t(it - ordered.begin()) == index) return edit;
    ordered.erase(it); ordered.insert(ordered.begin() + std::ptrdiff_t(index), id);
    wire(edit.graph, segment, ordered); edit.changed = true; return edit;
}
} // namespace vocalstack::mainBusFx
