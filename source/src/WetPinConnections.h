#pragma once
#include "Routing.h"

namespace vocalstack {
inline constexpr uint32_t wetBoundaryId(uint32_t owner, bool returning) noexcept {
    return 0x10000u + 2u * owner + unsigned(returning);
}

// The visible send/return pins use the same graph representation as saved wet
// pockets. Only a complete, detached main-canvas component can be claimed.
inline RoutingValidation prepareWetPinConnection(Graph& graph, const RoutingLink& link) {
    const auto find = [&](uint32_t id) {
        return std::find_if(graph.nodes.begin(), graph.nodes.end(), [=](const auto& n) { return n.id == id; });
    };
    const auto source = find(link.source), dest = find(link.destination);
    if (source == graph.nodes.end() || dest == graph.nodes.end()) return {false, "Missing cable endpoint"};
    if (source->wetOwner == dest->wetOwner) return {true, {}};
    if (source->wetOwner && dest->wetOwner) return {false, "Cannot connect wet paths owned by different FX"};
    const auto owner = source->wetOwner ? source->wetOwner : dest->wetOwner;
    const auto detached = source->wetOwner ? dest : source;
    std::array<uint32_t, RoutingExecutor::maxNodes> component{};
    size_t count = 1;
    component[0] = detached->id;
    for (size_t i = 0; i < count; ++i) {
        const auto member = find(component[i]);
        if (member == graph.nodes.end() || member->id == owner || member->wetOwner || member->mainControls
            || !routingSupportsWetInsert(member->type)
            || std::any_of(graph.nodes.begin(), graph.nodes.end(), [&](const auto& n) { return n.wetOwner == member->id; }))
            return {false, "Disconnect this chain from main routing or its existing wet path first"};
        for (const auto& cable : graph.links) {
            const auto adjacent = cable.source == member->id ? cable.destination
                : cable.destination == member->id ? cable.source : 0;
            if (!adjacent || std::find(component.begin(), component.begin() + count, adjacent) != component.begin() + count) continue;
            if (count == component.size()) return {false, "Wet routing exceeds node capacity"};
            component[count++] = adjacent;
        }
    }
    for (size_t i = 0; i < count; ++i) find(component[i])->wetOwner = owner;
    return {true, {}};
}

inline void expandLegacyWetPockets(Graph& graph) {
    const size_t originalSize = graph.nodes.size();
    for (size_t i = 0; i < originalSize; ++i) {
        const auto child = graph.nodes[i];
        if (!child.wetOwner || child.type == NodeType::wetInput || child.type == NodeType::wetOutput
            || graph.nodes.size() + 2 > RoutingExecutor::maxNodes || graph.links.size() + 2 > RoutingExecutor::maxLinks
            || std::count_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto& n) { return n.wetOwner == child.wetOwner; }) != 1)
            continue;
        const auto send = wetBoundaryId(child.wetOwner, false), receive = wetBoundaryId(child.wetOwner, true);
        if (std::any_of(graph.nodes.begin(), graph.nodes.end(), [&](const auto& n) { return n.id == send || n.id == receive; })) continue;
        RoutingNode a{send, NodeType::wetInput}, b{receive, NodeType::wetOutput};
        a.wetOwner = b.wetOwner = child.wetOwner;
        graph.nodes.push_back(a); graph.nodes.push_back(b);
        graph.links.push_back({send, child.id, 0}); graph.links.push_back({child.id, receive, 0});
    }
}

// A detached chain is free to patch back into the main graph or another FX.
inline void releaseDetachedWetNodes(Graph& graph) {
    std::array<bool, RoutingExecutor::maxNodes> attached{};
    if (graph.nodes.size() > attached.size()) return;
    for (size_t i = 0; i < graph.nodes.size(); ++i)
        attached[i] = graph.nodes[i].type == NodeType::wetInput || graph.nodes[i].type == NodeType::wetOutput;
    bool changed;
    do {
        changed = false;
        for (const auto& link : graph.links) {
            const auto index = [&](uint32_t id) {
                return size_t(std::find_if(graph.nodes.begin(), graph.nodes.end(), [=](const auto& n) { return n.id == id; }) - graph.nodes.begin());
            };
            const auto a = index(link.source), b = index(link.destination);
            if (a < graph.nodes.size() && b < graph.nodes.size() && attached[a] != attached[b])
                attached[a] = attached[b] = changed = true;
        }
    } while (changed);
    for (size_t i = 0; i < graph.nodes.size(); ++i) {
        auto& n = graph.nodes[i];
        if (!attached[i] && n.wetOwner && std::any_of(graph.nodes.begin(), graph.nodes.end(), [&](const auto& b) {
            return b.wetOwner == n.wetOwner && b.type == NodeType::wetInput;
        })) n.wetOwner = 0;
    }
}
} // namespace vocalstack
