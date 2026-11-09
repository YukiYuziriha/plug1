#pragma once

#include "Routing.h"
#include <juce_data_structures/juce_data_structures.h>

namespace vocalstack::processorRouting {

inline juce::ValueTree encode(const Graph& graph, const char* type) {
    juce::ValueTree tree(type);
    for (const auto& n : graph.nodes) {
        juce::ValueTree node("Node");
        node.setProperty("id", juce::int64(n.id), nullptr);
        node.setProperty("type", int(n.type), nullptr);
        node.setProperty("voiceSlot", n.voiceSlot, nullptr);
        node.setProperty("enabled", n.enabled, nullptr);
        node.setProperty("mix", n.mix, nullptr);
        node.setProperty("motion", n.motion, nullptr);
        node.setProperty("mainControls", n.mainControls, nullptr);
        node.setProperty("referencePower", n.referencePower, nullptr);
        node.setProperty("latencySamples", n.latencySamples, nullptr);
        tree.addChild(node, -1, nullptr);
    }
    for (const auto& l : graph.links) {
        juce::ValueTree link("Link");
        link.setProperty("source", juce::int64(l.source), nullptr);
        link.setProperty("destination", juce::int64(l.destination), nullptr);
        link.setProperty("inputPin", l.inputPin, nullptr);
        tree.addChild(link, -1, nullptr);
    }
    return tree;
}

inline bool decode(const juce::ValueTree& tree, Graph& graph) {
    if (!tree.isValid() || tree.getNumChildren() > RoutingExecutor::maxNodes + RoutingExecutor::maxLinks)
        return false;
    graph = {};
    for (const auto& child : tree) {
        if (child.hasType("Node")) {
            RoutingNode n;
            n.id = static_cast<std::uint32_t>(static_cast<juce::int64>(child.getProperty("id", 0)));
            const int type = child.getProperty("type", -1);
            if (type < 0 || type > int(NodeType::output)) return false;
            n.type = static_cast<NodeType>(type);
            n.voiceSlot = child.getProperty("voiceSlot", 0);
            n.enabled = child.getProperty("enabled", true);
            n.mix = child.getProperty("mix", 0.25f);
            n.motion = child.getProperty("motion", 0.35f);
            n.mainControls = child.getProperty("mainControls", false);
            n.referencePower = child.getProperty("referencePower", 1.0f);
            n.latencySamples = child.getProperty("latencySamples", 0);
            graph.nodes.push_back(n);
        } else if (child.hasType("Link")) {
            graph.links.push_back({
                static_cast<std::uint32_t>(static_cast<juce::int64>(child.getProperty("source", 0))),
                static_cast<std::uint32_t>(static_cast<juce::int64>(child.getProperty("destination", 0))),
                int(child.getProperty("inputPin", 0))});
        } else return false;
    }
    return graph.nodes.size() <= RoutingExecutor::maxNodes && graph.links.size() <= RoutingExecutor::maxLinks;
}

inline int fxSlot(std::uint32_t nodeId) noexcept {
    if (nodeId == 12) return 0;
    return nodeId >= 100 && nodeId <= 114 ? int(nodeId - 99) : -1;
}

inline RoutingValidation validate(const Graph& graph) {
    for (const auto& n : graph.nodes) {
        if (n.type == NodeType::chorale && fxSlot(n.id) < 0)
            return {false, "Chorale IDs must be 12 (legacy) or 100..114 (FX slots 1..15)"};
    }
    return RoutingExecutor::validate(graph);
}

// Backend integration seam: Settings::fxSettings is a fixed 16-element array
// with enabled/mix/motion, plus bool fxAutomation=false. The opt-in preserves
// node-authored controls for standalone engine callers. Slot 0 mirrors legacy
// controls; 1..15 map to 100..114. The processor opts in every block.
// Routing reads these values per block/quantum, never rebuilding a graph.
template <typename T, typename Read>
void applyFxSettings(T& settings, Read read) {
    settings.fxAutomation = true;
    for (size_t slot = 0; slot < 16; ++slot) {
        auto& fx = settings.fxSettings[slot];
        fx.enabled = read(slot, 0) >= 0.5f;
        fx.mix = read(slot, 1) * 0.01f;
        fx.motion = read(slot, 2) * 0.01f;
    }
}


} // namespace vocalstack::processorRouting
