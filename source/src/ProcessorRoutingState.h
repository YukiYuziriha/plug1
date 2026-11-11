#pragma once

#include "Routing.h"
#include <juce_data_structures/juce_data_structures.h>

namespace vocalstack::processorRouting {

inline bool isEffect(NodeType type) noexcept {
    return type == NodeType::chorale || type == NodeType::waveshaper
        || type == NodeType::equalizer || type == NodeType::crossover || isCreativeEffect(type) || isUtilityEffect(type);
}

inline float bounded(float value, float low, float high, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

inline void boundToneSettings(RoutingNode& n) noexcept {
    n.waveshaper.driveDb = bounded(n.waveshaper.driveDb, 0, 36, 0);
    n.waveshaper.outputDb = bounded(n.waveshaper.outputDb, -24, 12, 0);
    n.waveshaper.shape = std::clamp(n.waveshaper.shape, 0, 18);
        n.waveshaper.quality = std::clamp(n.waveshaper.quality, 0, 2);
        n.waveshaper.shapeAmount = bounded(n.waveshaper.shapeAmount, 0, 1, 0.5f);
    n.waveshaper.mix = bounded(n.waveshaper.mix, 0, 1, 1);
    n.eq.shiftSemitones = bounded(n.eq.shiftSemitones, -24, 24, 0);
    n.eq.gainDb = bounded(n.eq.gainDb, -24, 24, 0);
    for (auto& band : n.eq.bands) {
        band.frequency = bounded(band.frequency, 20, 20000, 1000);
        band.gainDb = bounded(band.gainDb, -24, 24, 0);
        band.q = bounded(band.q, 0.1f, 18, 0.707f);
        band.mode = std::clamp(band.mode, 0, 13);
    }
    n.bandCount = std::clamp(n.bandCount, 2, 3);
    n.crossoverLow = bounded(n.crossoverLow, 20, 20000, 250);
    n.crossoverHigh = bounded(n.crossoverHigh, 20, 20000, 4000);
}

// Same 9 + 8*5 layout as the appended host parameter block. No ID lookup is
// involved in the audio-thread bridge; Read loads only cached atomics.
template <typename Read>
void applyToneSettings(FxSettings& fx, Read read) {
    fx.waveshaper.enabled = fx.enabled;
    fx.waveshaper.mix = fx.mix;
    fx.waveshaper.driveDb = read(0);
    fx.waveshaper.outputDb = read(1);
    fx.waveshaper.shape = int(read(2));
    fx.eq.enabled = fx.enabled;
    fx.eq.shiftSemitones = read(3);
    fx.eq.gainDb = read(4);
    fx.eq.adaptiveQ = read(5) >= 0.5f;
    fx.bandCount = int(read(6));
    // Keep raw requested cutoffs independent; the DSP orders/bounds them.
    fx.crossoverLow = read(7);
    fx.crossoverHigh = read(8);
    for (size_t b = 0; b < fx.eq.bands.size(); ++b) {
        auto& band = fx.eq.bands[b];
        const auto i = 9 + b * 5;
        band.enabled = read(i) >= 0.5f;
        band.frequency = read(i + 1);
        band.gainDb = read(i + 2);
        band.q = read(i + 3);
        band.mode = int(read(i + 4));
    }
}

inline std::array<float, 49> toneValues(const RoutingNode& node) noexcept {
    auto n = node;
    boundToneSettings(n);
    std::array<float, 49> values{n.waveshaper.driveDb, n.waveshaper.outputDb,
        float(n.waveshaper.shape), n.eq.shiftSemitones, n.eq.gainDb,
        n.eq.adaptiveQ ? 1.0f : 0.0f, float(n.bandCount), n.crossoverLow, n.crossoverHigh};
    for (size_t b = 0; b < n.eq.bands.size(); ++b) {
        const auto& band = n.eq.bands[b];
        const auto i = 9 + b * 5;
        values[i] = band.enabled ? 1.0f : 0.0f;
        values[i + 1] = band.frequency;
        values[i + 2] = band.gainDb;
        values[i + 3] = band.q;
        values[i + 4] = float(band.mode);
    }
    return values;
}

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
        node.setProperty("bandCount", n.bandCount, nullptr);
        node.setProperty("crossoverLow", n.crossoverLow, nullptr);
        node.setProperty("crossoverHigh", n.crossoverHigh, nullptr);
        juce::ValueTree wave("Waveshaper");
        wave.setProperty("enabled", n.waveshaper.enabled, nullptr);
        wave.setProperty("driveDb", n.waveshaper.driveDb, nullptr);
        wave.setProperty("outputDb", n.waveshaper.outputDb, nullptr);
        wave.setProperty("mix", n.waveshaper.mix, nullptr);
        wave.setProperty("shape", n.waveshaper.shape, nullptr);
        wave.setProperty("raw", n.waveshaper.raw, nullptr);
        wave.setProperty("shapeAmount", n.waveshaper.shapeAmount, nullptr);
                wave.setProperty("quality", n.waveshaper.quality, nullptr);
                wave.setProperty("levelMatch", n.waveshaper.levelMatch, nullptr);
        node.addChild(wave, -1, nullptr);
        juce::ValueTree eq("Equalizer");
        eq.setProperty("enabled", n.eq.enabled, nullptr);
        eq.setProperty("shiftSemitones", n.eq.shiftSemitones, nullptr);
        eq.setProperty("gainDb", n.eq.gainDb, nullptr);
        eq.setProperty("adaptiveQ", n.eq.adaptiveQ, nullptr);
        for (const auto& b : n.eq.bands) {
            juce::ValueTree band("Band");
            band.setProperty("enabled", b.enabled, nullptr);
            band.setProperty("frequency", b.frequency, nullptr);
            band.setProperty("gainDb", b.gainDb, nullptr);
            band.setProperty("q", b.q, nullptr);
            band.setProperty("mode", b.mode, nullptr);
            eq.addChild(band, -1, nullptr);
        }
        node.addChild(eq, -1, nullptr);
        juce::ValueTree creative("Creative");
        for (size_t p = 0; p < creativeParameterCount; ++p)
            creative.setProperty(creativeParameters[p].id, n.creative.values[p], nullptr);
        node.addChild(creative, -1, nullptr);
        juce::ValueTree utility("Utility");
        for (size_t p = 0; p < utilityParameterCount; ++p)
            utility.setProperty(utilityParameters[p].id, n.utility.values[p], nullptr);
        node.addChild(utility, -1, nullptr);
        tree.addChild(node, -1, nullptr);
    }
    for (const auto& l : graph.links) {
        juce::ValueTree link("Link");
        link.setProperty("source", juce::int64(l.source), nullptr);
        link.setProperty("destination", juce::int64(l.destination), nullptr);
        link.setProperty("inputPin", l.inputPin, nullptr);
        link.setProperty("sourcePin", l.sourcePin, nullptr);
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
            if (type < 0 || type > int(NodeType::synthFilter)) return false;
            n.type = static_cast<NodeType>(type);
            n.voiceSlot = child.getProperty("voiceSlot", 0);
            n.enabled = child.getProperty("enabled", true);
            n.mix = child.getProperty("mix", isUtilityEffect(n.type) ? 1.0f : 0.25f);
            n.motion = child.getProperty("motion", 0.35f);
            n.mainControls = child.getProperty("mainControls", false);
            n.referencePower = child.getProperty("referencePower", 1.0f);
            n.latencySamples = child.getProperty("latencySamples", 0);
            n.bandCount = child.getProperty("bandCount", n.bandCount);
            n.crossoverLow = child.getProperty("crossoverLow", n.crossoverLow);
            n.crossoverHigh = child.getProperty("crossoverHigh", n.crossoverHigh);
            const auto wave = child.getChildWithName("Waveshaper");
            n.waveshaper.enabled = wave.getProperty("enabled", n.waveshaper.enabled);
            n.waveshaper.driveDb = wave.getProperty("driveDb", n.waveshaper.driveDb);
            n.waveshaper.outputDb = wave.getProperty("outputDb", n.waveshaper.outputDb);
            n.waveshaper.mix = wave.getProperty("mix", n.waveshaper.mix);
            n.waveshaper.shape = wave.getProperty("shape", n.waveshaper.shape);
            n.waveshaper.raw = wave.getProperty("raw", n.waveshaper.raw);
            n.waveshaper.shapeAmount = wave.getProperty("shapeAmount", n.waveshaper.shapeAmount);
                        n.waveshaper.quality = wave.getProperty("quality", 0);
                        n.waveshaper.levelMatch = wave.getProperty("levelMatch", false);
            const auto eq = child.getChildWithName("Equalizer");
            n.eq.enabled = eq.getProperty("enabled", n.eq.enabled);
            n.eq.shiftSemitones = eq.getProperty("shiftSemitones", n.eq.shiftSemitones);
            n.eq.gainDb = eq.getProperty("gainDb", n.eq.gainDb);
            n.eq.adaptiveQ = eq.getProperty("adaptiveQ", n.eq.adaptiveQ);
            if (eq.getNumChildren() > int(n.eq.bands.size())) return false;
            for (int b = 0; b < eq.getNumChildren(); ++b) {
                const auto saved = eq.getChild(b);
                if (!saved.hasType("Band")) return false;
                auto& band = n.eq.bands[size_t(b)];
                band.enabled = saved.getProperty("enabled", band.enabled);
                band.frequency = saved.getProperty("frequency", band.frequency);
                band.gainDb = saved.getProperty("gainDb", band.gainDb);
                band.q = saved.getProperty("q", band.q);
                band.mode = saved.getProperty("mode", band.mode);
            }
            const auto creative = child.getChildWithName("Creative");
            for (size_t p = 0; p < creativeParameterCount; ++p) {
                const auto& d = creativeParameters[p];
                n.creative.values[p] = bounded(float(creative.getProperty(d.id, d.initial)), d.min, d.max, d.initial);
            }
            const auto utility = child.getChildWithName("Utility");
            for (size_t p = 0; p < utilityParameterCount; ++p) {
                const auto& d = utilityParameters[p];
                const float value = bounded(float(utility.getProperty(d.id, d.initial)), d.min, d.max, d.initial);
                n.utility.values[p] = d.discrete ? std::round(value) : value;
            }
            boundToneSettings(n);
            graph.nodes.push_back(n);
        } else if (child.hasType("Link")) {
            graph.links.push_back({
                static_cast<std::uint32_t>(static_cast<juce::int64>(child.getProperty("source", 0))),
                static_cast<std::uint32_t>(static_cast<juce::int64>(child.getProperty("destination", 0))),
                int(child.getProperty("inputPin", 0)), int(child.getProperty("sourcePin", 0))});
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
        if (isEffect(n.type) && (fxSlot(n.id) < 0 || (n.id == 12 && n.type != NodeType::chorale)))
            return {false, "Effect IDs must be 100..114 (FX slots 1..15); ID 12 is legacy Chorale only"};
    }
    return RoutingExecutor::validate(graph);
}

// Fixed legacy controls are shared by all effect types. Slot zero remains
// exclusively the original Chorale, preserving its historical host indices.
template <typename Read>
void applyFxSettings(std::array<FxSettings, 16>& settings, Read read) {
    for (size_t slot = 0; slot < 16; ++slot) {
        auto& fx = settings[slot];
        fx.enabled = read(slot, 0) >= 0.5f;
        fx.mix = read(slot, 1) * 0.01f;
        fx.motion = read(slot, 2) * 0.01f;
    }
}

} // namespace vocalstack::processorRouting
