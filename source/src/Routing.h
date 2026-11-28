#pragma once

#include "Chorale.h"
#include "Waveshaper.h"
#include "Equalizer.h"
#include "SpatialEq.h"
#include "Multiband.h"
#include "FxMonitor.h"
#include "CreativeEffects.h"
#include "VocalGlitch.h"
#include "UtilityFilters.h"
#include "SpectralMorph.h"
#include "SpectralMorphBridge.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace vocalstack {

// Serializable model. IDs identify machines, not positions in nodes[]. Changing
// an effect ID, type or wet owner creates fresh state. FX
// with an internal Mix also restart when switching between native Mix and a
// separately rendered wet endpoint.
enum class NodeType : std::uint8_t { lead = 0, voice = 1, chorale = 2, sum = 3, mix = 4, output = 5, waveshaper = 6, equalizer = 7, crossover = 8, delay = 9, reverb = 10, compressor = 11, bitcrusher = 12, granular = 13, gain = 14, stereo = 15, gate = 16, xpFilter = 17, synthFilter = 18, splitter = 19, convolution = 20, saturator = 21, sidechain = 22, spectralMorph = 23, vocalGlitch = 24, sidechainBlend = 25, wetInput = 26, wetOutput = 27, spatialEq = 28 };
inline constexpr std::uint32_t sidechainNodeId = 14;
inline constexpr std::uint32_t sidechainBlendNodeId = 15;

// Named signal inputs are shared by validation, prepared plans and presentation.
// Additional multi-input effects extend this declaration, not host-buffer access.
struct EffectInputDeclaration {
    const char* name;
    bool auxiliary;
};
inline constexpr int routingInputCount(NodeType type) noexcept {
    if (type == NodeType::lead || type == NodeType::voice || type == NodeType::sidechain || type == NodeType::wetInput) return 0;
    if (type == NodeType::sum) return 16;
    return type == NodeType::mix || type == NodeType::spectralMorph || type == NodeType::sidechainBlend ? 2 : 1;
}
inline constexpr EffectInputDeclaration routingInput(NodeType type, int pin) noexcept {
    if (type == NodeType::spectralMorph) return {pin == 0 ? "MAIN" : "DONOR", pin == 1};
    if (type == NodeType::sidechainBlend) return {pin == 0 ? "LEAD" : "SIDECHAIN", pin == 1};
    return type == NodeType::mix ? EffectInputDeclaration{pin == 0 ? "DRY" : "WET", false}
                                 : EffectInputDeclaration{"MAIN", false};
}
// Mandatory whole-sample DSP delay only, not creative timing or filter phase.
inline constexpr int routingDeclaredLatency(NodeType type) noexcept {
    static_assert(SpatialEq::latencySamples == 0);
    return type == NodeType::spectralMorph ? SpectralMorph::latencySamples : 0;
}
inline constexpr bool isCreativeEffect(NodeType type) noexcept {
    return (type >= NodeType::delay && type <= NodeType::granular)
        || type == NodeType::convolution || type == NodeType::saturator || type == NodeType::vocalGlitch;
}
inline constexpr CreativeKind creativeKind(NodeType type) noexcept {
    if (type == NodeType::convolution) return CreativeKind::convolution;
    if (type == NodeType::saturator) return CreativeKind::saturator;
    if (type == NodeType::vocalGlitch) return CreativeKind::vocalGlitch;
    return static_cast<CreativeKind>(int(type) - int(NodeType::delay));
}
inline constexpr bool isUtilityEffect(NodeType type) noexcept {
    return type >= NodeType::gain && type <= NodeType::synthFilter;
}
inline constexpr UtilityKind utilityKind(NodeType type) noexcept {
    return static_cast<UtilityKind>(int(type) - int(NodeType::gain));
}
// Crossover has separate band outputs, not a single wet bus. Canvas
// infrastructure (including Sidechain Blend) is not a slot FX.
inline constexpr bool routingSupportsWetPocket(NodeType type) noexcept {
    return type == NodeType::chorale || type == NodeType::waveshaper || type == NodeType::equalizer
        || type == NodeType::spectralMorph || isCreativeEffect(type) || isUtilityEffect(type);
}
inline constexpr bool routingSupportsWetInsert(NodeType type) noexcept {
    return routingSupportsWetPocket(type) || type == NodeType::sum || type == NodeType::mix
        || type == NodeType::splitter || type == NodeType::crossover;
}
struct RoutingNode {
    std::uint32_t id = 0;
    NodeType type = NodeType::lead;
    int voiceSlot = 0; // zero-based, stable 0..7
    bool enabled = true; // effect bypass; crossover bypass routes full-band to port zero
    float mix = isUtilityEffect(type) ? 1.0f : type == NodeType::sidechainBlend ? 0.0f : 0.25f, motion = 0.35f;
    bool mainControls = false; // Mix: main Mix; Chorale: legacy Chorale controls
    float referencePower = 1.0f; // legacy Sum only; main Mix wet pin has a 2-voice budget
    int latencySamples = 0; // nonzero is unsupported, rejected rather than misaligned
    WaveshaperSettings waveshaper{};
    EqualizerSettings eq{};
    int bandCount = 3;
    float crossoverLow = 250, crossoverHigh = 4000;
    CreativeSettings creative;
    UtilitySettings utility;
    SpectralMorphSettings spectralMorph;
    bool monoAudition = false;
    std::uint32_t wetOwner = 0; // Pocket scope (0 = main); processed before the owner's local Mix.
    SpatialEqSettings spatialEq;
};
inline constexpr bool routingUsesInput(const RoutingNode& node, int pin) noexcept {
    return node.enabled || !routingInput(node.type, pin).auxiliary;
}
struct RoutingLink {
    std::uint32_t source = 0, destination = 0;
    int inputPin = 0; // Mix dry=0/wet=1, Sum=0..15, other processors=0
    int sourcePin = 0; // splitter: identical full-level outputs 0/1
                           // crossover: Low=0, Mid=1, High=2; two-band: Low=0, High=1
};
struct Graph { std::vector<RoutingNode> nodes; std::vector<RoutingLink> links; };
struct RoutingValidation {
    bool valid = false;
    std::string message;
    explicit operator bool() const noexcept { return valid; }
};

inline Graph defaultGraph(bool legacyChorale = true, bool withSidechainBlend = false) {
    Graph g;
    g.nodes.push_back({1, NodeType::lead});
    for (int i = 0; i < 8; ++i) g.nodes.push_back({std::uint32_t(i + 2), NodeType::voice, i});
    RoutingNode mix{11, NodeType::mix}; mix.mainControls = true;
    RoutingNode fx{12, NodeType::chorale}; fx.mainControls = true;
    g.nodes.push_back(mix);
    if (legacyChorale) g.nodes.push_back(fx);
    g.nodes.push_back({13, NodeType::output});
    for (int i = 0; i < 8; ++i) g.links.push_back({std::uint32_t(i + 2), 11, 1});
    if (withSidechainBlend) {
        g.nodes.push_back({sidechainNodeId, NodeType::sidechain});
        g.nodes.push_back({sidechainBlendNodeId, NodeType::sidechainBlend});
        g.links.insert(g.links.end(), {{1, sidechainBlendNodeId, 0},
            {sidechainNodeId, sidechainBlendNodeId, 1}, {sidechainBlendNodeId, 11, 0}});
    } else g.links.push_back({1, 11, 0});
    if (legacyChorale) g.links.insert(g.links.end(), {{11, 12, 0}, {12, 13, 0}});
    else g.links.push_back({11, 13, 0});
    return g;
}

// Control-thread migration: old explicit Sum boxes become ordinary fan-in
// cables. Runtime merges now own compensation, so no Sum is exposed in the UI.
inline Graph removeLegacySumNodes(const Graph& old) {
    Graph graph;
    const auto isLegacySum = [](const RoutingNode& n) { return n.type == NodeType::sum && n.wetOwner == 0; };
    const auto isSum = [&](std::uint32_t id) {
        for (const auto& n : old.nodes) if (n.id == id) return isLegacySum(n);
        return false;
    };
    for (const auto& n : old.nodes) if (!isLegacySum(n)) graph.nodes.push_back(n);
    const auto expand = [&](auto&& self, std::uint32_t source, std::uint32_t destination, int pin, int sourcePin, int depth) -> void {
        if (depth > 48) return; // Callers validate the bounded DAG before migration.
        if (isSum(source)) {
            for (const auto& l : old.links) if (l.destination == source) self(self, l.source, destination, pin, l.sourcePin, depth + 1);
        } else {
            for (const auto& l : graph.links)
                if (l.source == source && l.destination == destination && l.inputPin == pin && l.sourcePin == sourcePin) return;
            graph.links.push_back({source, destination, pin, sourcePin});
        }
    };
    for (const auto& l : old.links) if (!isSum(l.destination)) expand(expand, l.source, l.destination, l.inputPin, l.sourcePin, 0);
    return graph;
}

struct StereoSample { float left = 0, right = 0; };
// Sources must share algorithmic latency. Per-voice musical delay is intentional
// timing and is NOT automatically removed by the routing layer.
struct RoutingSources {
    StereoSample lead;
    std::array<StereoSample, 8> voices{};
    bool sidechainAvailable = false;
    StereoSample sidechain; // Already aligned to source-engine latency; absent is silence.
    // Smoothed enabled/present weights, independent of intentional voice gain.
    std::array<float, 8> activity{};
    bool sidechainVoiceAvailable = false;
    StereoSample sidechainVoice; // Same algorithmic latency as the raw sidechain tap.
};
struct FxSettings {
    bool enabled = true;
    float mix = 0.25f, motion = 0.35f;
    WaveshaperSettings waveshaper{};
    EqualizerSettings eq{};
    int bandCount = 3;
    float crossoverLow = 250, crossoverHigh = 4000;
    CreativeSettings creative;
    UtilitySettings utility;
    SpectralMorphSettings spectralMorph;
    bool monoAudition = false;
    SpatialEqSettings spatialEq;
};

// Stable processor automation identities. Unmapped standalone machines keep
// their node-authored controls even when another machine opts into automation.
inline constexpr int routingFxSlot(std::uint32_t id) noexcept {
    return id == 12 ? 0 : id >= 100 && id <= 114 ? int(id - 99) : -1;
}

struct RoutingControls {
    float mix = 0.35f;
    bool choraleOn = false;
    float choraleMix = 0.25f, choraleMotion = 0.35f;
    std::array<FxSettings, 16> fxSettings{};
    bool fxAutomation = false;
    bool monitorWetEnabled = true; // Presentation only; does not gate processing.
    std::array<float, 48> localMix{};
    bool localMixAutomation = false;
    double tempoBpm = 120;
    float sidechainBlend = 0.0f;
    bool sidechainBlendAutomation = false;
};

// Fan-in contract: distinct source voices are orthogonal, duplicate paths are
// correlated, and complementary crossover bands partition nominal frequency
// provenance. Merge at unity for disjoint bands; compensate overlapping paths.
// A broadband path overlapping several bands takes the most conservative gain.
// Voice gain/pan and measured audio power are deliberately excluded. Processing
// can change actual correlation; provenance is not a loudness/correlation meter.
// Legacy explicit Sum remains executable for migration only.
//
// Bounded executor chosen instead of AudioProcessorGraph: no JUCE dependency,
// explicit mixer/pin semantics, and a raw-pointer audio handoff with off-thread
// reclamation. Prepared per-wire delays align contributing inputs, and the
// output is padded to the caller-provided fixed reserve. A prepared-disabled
// latency-bearing machine is a true MAIN bypass; its auxiliary pins are absent
// from the plan. Donor provenance never contributes to the audible MAIN lineage.
// EQ/LR4 and ADAA/IIR phase are not removed by integer-delay compensation.
//
// Thread contract: prepare/reset/destruction require audio quiescence AND
// serialization with all control calls. requestGraph
// and collectRetired run on ONE serialized control thread, never audio. process
// runs on ONE audio thread. No processor-owned publication mechanism is needed.
// Request success means queued, applied on the next process call. Multiple queued
// edits collapse to the latest. Stable effect IDs AND types share history, but
// only the current plan renders: state is never advanced twice for a transition.
struct RoutingGrainFrame {
    uint32_t nodeId = 0;
    uint64_t generation = 0;
    CreativeEffect::GrainSnapshot grains{};
    uint64_t revision = 0, resetEpoch = 0;
};
struct RoutingGrainSnapshot {
    uint64_t revision = 0, resetEpoch = 0;
    std::array<RoutingGrainFrame, 16> nodes{};
};

class RoutingExecutor {
public:
    // Audio owner only; call once per host block, never from the GUI.
    RoutingGrainSnapshot grainSnapshot() const noexcept {
        RoutingGrainSnapshot result;
        result.resetEpoch = grainResetEpoch;
        if (!active) return result;
        result.revision = active->revision;
        for (int i = 0; i < active->count; ++i) {
            const auto& step = active->steps[i];
            const int slot = routingFxSlot(step.node.id);
            if (step.node.type == NodeType::granular && step.fx && slot > 0) {
                result.nodes[size_t(slot)] = {step.node.id, step.fx->grainGeneration,
                    step.fx->creative.grainSnapshot(), result.revision, result.resetEpoch};
            }
        }
        return result;
    }
    SpectralMorphFrames spectralSnapshots() const noexcept {
        SpectralMorphFrames frames{};
        if (active) for (int i = 0; i < active->count; ++i) {
            const auto& s = active->steps[i]; const int slot = routingFxSlot(s.node.id);
            if (s.node.type == NodeType::spectralMorph && s.fx && s.fx->spectral && slot > 0)
                frames[size_t(slot)] = {s.node.id, s.fx->spectral->audioSnapshot(), active->revision, grainResetEpoch};
        }
        return frames;
    }
    // Concurrent readers may reject frames from a not-yet-installed plan/reset.
    uint64_t requestedGrainRevision() const noexcept { return grainRevision.load(std::memory_order_acquire); }
    uint64_t grainEpoch() const noexcept { return publishedGrainEpoch.load(std::memory_order_acquire); }
    static constexpr int maxNodes = 48, maxLinks = 128, maxEffects = 16, maxInputs = 16;
    // Prepared fixed budget, added to the source engine latency. Arbitrary
    // user-authored latencySamples remains unsupported; known module latency
    // is compiled from effect declarations and cannot be overridden by state.
    static constexpr int integerLatencySamples = Waveshaper::latencySamples;
    int additionalLatencySamples() const noexcept { return reserve; }
    // Legacy finite graph allowance, NOT a cap on creative tails (Freeze can
    // report infinity). IIR allowances do not truncate audio.
    static constexpr double maximumTailSeconds = maxEffects * 256.0;
    // Reports the active plan only. Queued edits become visible when audio
    // installs them; prepare publishes the default immediately. Creative controls
    // refresh every 2048 samples; bypass/mix do not remove retained tails.
    double tailSeconds() const noexcept { return reportedTail.load(std::memory_order_acquire); }
    // Non-owning, opt-in instrumentation. Set only with audio quiescent; the
    // monitor must outlive the executor. Standalone DSP remains uninstrumented.
    void setFxMonitor(FxMonitor* monitor) noexcept {
        fxMonitor = monitor;
        if (fxMonitor) fxMonitor->reset(rate > 0 ? rate : 48000);
    }
    RoutingExecutor() = default;
    RoutingExecutor(const RoutingExecutor&) = delete;
    RoutingExecutor& operator=(const RoutingExecutor&) = delete;
    ~RoutingExecutor() { clear(); }

    static RoutingValidation validate(const Graph& g) {
        std::array<int, maxNodes> order{};
        return orderGraph(g, order);
    }
    void prepare(double sampleRate, int latencyReserve = 0) {
        if (!std::isfinite(sampleRate) || sampleRate <= 0 || sampleRate > std::numeric_limits<int>::max())
            throw std::invalid_argument("invalid routing sample rate");
        clear();
        reserve = std::clamp(latencyReserve, 0, SpectralMorph::latencySamples);
        rate = sampleRate;
        mixSmoothing = float(-std::expm1(-1.0 / (rate * .010)));
        transitionLength = std::max(1, int(rate * 0.010));
        requestGraph(defaultGraph());
        active = pending.exchange(nullptr);
        updateTail(*active, RoutingControls{});
        reset();
    }
    RoutingValidation requestGraph(const Graph& g, unsigned routedVoices = 0x10000u) {
        std::array<int, maxNodes> order{};
        WetPockets pockets;
        auto result = orderGraph(g, order, &pockets);
        if (!result) return result;
        if (!(rate > 0)) return {false, "prepare must precede requestGraph"};
        const auto latency = measureLatency(g, order, pockets, false);
        if (latency.output > reserve) return {false, "Routing exceeds the prepared latency reserve"};
        collectRetired();
        auto plan = std::make_unique<Plan>();
        plan->count = int(g.nodes.size());
        plan->routedVoices = routedVoices;
        std::array<int, maxNodes> remap{};

        for (int i = 0; i < plan->count; ++i) remap[order[i]] = i;
        for (int i = 0; i < plan->count; ++i) {
            auto& step = plan->steps[i];
            const int index = order[i];
            step.node = g.nodes[index];
            const int declared = routingDeclaredLatency(step.node.type);
            const int arrival = latency.paths[index] - latency.local[index];
            const int wetResult = pockets.result[index];
            if (wetResult >= 0) {
                step.wetResult = remap[wetResult];
                if (pockets.input[index] >= 0) step.wetInput = remap[pockets.input[index]];
                step.pocketDry.data.resize(size_t(latency.paths[wetResult]));
            }
            for (const auto& link : g.links) if (link.destination == step.node.id && routingUsesInput(step.node, link.inputPin)) {
                const int source = find(g, link.source);
                const int wire = step.inputCount++;
                step.inputs[wire] = remap[source];
                step.sourcePins[wire] = link.sourcePin;
                step.inputPins[wire] = link.inputPin;
                step.delays[wire].data.resize(size_t(arrival - latency.paths[source]));
                step.donorWired |= routingInput(step.node.type, link.inputPin).auxiliary;
                if (step.node.type == NodeType::sidechainBlend && link.inputPin == 1
                    && g.nodes[source].type == NodeType::sidechain)
                    step.sidechainVoiceWire = wire;
            }
            if (isEffect(step.node.type) && (declared == 0 || step.node.enabled)) {
                auto& weak = machines[step.node.id];
                step.fx = weak.lock();
                const bool splitsWet = wetResult >= 0 && usesInternalWetMix(step.node.type);
                if (!step.fx || step.fx->type != step.node.type || step.fx->splitsWet != splitsWet
                    || step.fx->wetOwner != step.node.wetOwner) {
                    step.fx = std::make_shared<Machine>(step.node, splitsWet);
                    step.fx->prepare(rate);
                    step.fx->grainGeneration = ++nextGrainGeneration;
                    weak = step.fx;
                }
            }

            if (step.node.type == NodeType::output) plan->output = i;
        }

        plan->steps[plan->output].connected = true;

        for (int index = plan->output; index >= 0; --index) {
            auto& step = plan->steps[index];
            if (!step.connected) continue;
            if (step.wetResult >= 0) plan->steps[step.wetResult].connected = true;
            if (step.node.wetOwner && step.node.type != NodeType::wetInput) {
                auto& owner = plan->steps[remap[find(g, step.node.wetOwner)]];
                if (owner.wetInput >= 0) {
                    step.nextWetStep = owner.firstWetStep;
                    owner.firstWetStep = index;
                }
            }
            plan->hasCrossover |= step.node.type == NodeType::crossover;
            if (step.sidechainVoiceWire >= 0) plan->sidechainVoiceStep = index;
            for (int wire = 0; wire < step.inputCount; ++wire)
                plan->steps[step.inputs[wire]].connected = true;
        }

        plan->outputDelay.data.resize(size_t(reserve - latency.output));
        plan->revision = grainRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
        delete pending.exchange(plan.release(), std::memory_order_acq_rel);
        // Do not accumulate expired machine identities after repeated editing.
        for (auto it = machines.begin(); it != machines.end();) {
            const auto machine = it->second.lock();
            // Retired/active plans may still own deleted effects. Only ID/type
            // survivors may reuse history, even when pending edits coalesce.
            const int index = find(g, it->first);
            // A true latency bypass stops the timeline. Start fresh on the
            // next activation rather than replaying pre-bypass FFT/delay data.
            if (!machine || index < 0 || g.nodes[size_t(index)].type != machine->type
                || (routingDeclaredLatency(machine->type) != 0 && !g.nodes[size_t(index)].enabled))
                it = machines.erase(it);
            else ++it;
        }
        return {true, {}};
    }
    void collectRetired() {
        auto read = retireRead.load(std::memory_order_relaxed);
        const auto write = retireWrite.load(std::memory_order_acquire);
        while (read != write) {
            delete retired[read % retired.size()];
            ++read;
        }
        retireRead.store(read, std::memory_order_release);
    }
    void reset() {
        publishedGrainEpoch.store(++grainResetEpoch, std::memory_order_release);
        // Include old-type machines still owned by active/retired plans, not
        // only the latest identity map. Quiescent audio/control is required.
        std::array<Machine*, maxNodes * 6> seen{};
        size_t used = 0;
        const auto resetPlan = [&](Plan* plan) {
            if (!plan) return;
            plan->outputDelay.reset();
            for (int i = 0; i < plan->count; ++i) {
                plan->steps[i].mixInitialized = false;
                plan->steps[i].glitchStamp = 0;
                plan->steps[i].pocketDry.reset();
                for (auto& delay : plan->steps[i].delays) delay.reset();
                if (auto* machine = plan->steps[i].fx.get()) {
                    if (std::find(seen.begin(), seen.begin() + used, machine) == seen.begin() + used) {
                        seen[used++] = machine; machine->reset();
                    }
                }
            }
        };
        resetPlan(active); resetPlan(pending.load());
        for (auto r = retireRead.load(); r != retireWrite.load(); ++r) resetPlan(retired[r % retired.size()]);
        last = correction = {};
        transition = 0;
        rendered = false;
        tailSamplesUntilUpdate = 0;
        if (fxMonitor) fxMonitor->reset(rate > 0 ? rate : 48000);
    }
    void setGlitchSpan(double ppq, double bpm, bool playing, bool discontinuity, uint32_t triggers) noexcept {
        glitchPpq = ppq;
        glitchBpm = bpm;
        glitchPlaying = playing;
        glitchDiscontinuity = discontinuity;
        glitchTriggers = triggers;
        glitchOffset = 0;
        ++glitchStamp;
    }
    unsigned beginSpan(unsigned fallbackVoices = 0xffffu) noexcept {
        const auto write = retireWrite.load(std::memory_order_relaxed);
        // SPSC retirement queue: bounded work, no audio CAS retry loop. If the
        // control thread stops collecting, retain the current plan safely.
        if (write - retireRead.load(std::memory_order_acquire) < retired.size()) {
            if (auto* next = pending.exchange(nullptr, std::memory_order_acq_rel)) {
                auto* old = active;
                active = next;
                if (fxMonitor) fxMonitor->reset(rate);
                tailSamplesUntilUpdate = 0;
                if (old) {
                    retired[write % retired.size()] = old;
                    retireWrite.store(write + 1, std::memory_order_release);
                }
                transition = rendered ? transitionLength : 0;
            }
        }
        return active && active->routedVoices != 0x10000u ? active->routedVoices : fallbackVoices;
    }
    // Audio thread, after beginSpan. Use the target, not the smoothing tail,
    // so an automated zero stops the voice immediately.
    bool usesSidechainVoice(bool automation, float blendTarget) const noexcept {
        if (!active || active->sidechainVoiceStep < 0) return false;
        return unit(automation ? blendTarget : active->steps[active->sidechainVoiceStep].node.mix, 0) > 0;
    }

    StereoSample process(const RoutingSources& sources, const RoutingControls& controls, bool begin = true) noexcept {
        if (begin) beginSpan();
        if (!active) return {};
        if (tailSamplesUntilUpdate == 0) {
            updateTail(*active, controls);
            tailSamplesUntilUpdate = 2048;
        }
        --tailSamplesUntilUpdate;
        if (fxMonitor) fxMonitor->beginSample();
        updateRegions(controls);
        const auto out = active->hasCrossover ? processPlan(sources, controls, active->values)
                                              : processPlan(sources, controls, active->compactValues);
        ++glitchOffset;
        return out;
    }

private:
    template <class Values>
    StereoSample processPlan(const RoutingSources& sources, const RoutingControls& controls, Values& values) noexcept {
        using Value = typename Values::value_type::value_type;
        constexpr size_t lineageStride = std::tuple_size<decltype(Value{}.lineage)>::value / 10;
        const auto processStep = [&](int i, const auto& renderFx) {
            auto& step = active->steps[i];
            auto& ports = values[i];
            // Only crossover needs to clear every port (bypassed/unused bands).
            // Splitter overwrites both outputs with complete routed values.
            if (step.node.type == NodeType::crossover) ports = {};
            else ports[0] = {};
            auto& value = ports[0];
            const auto& n = step.node;

            const int monitorSlot = fxMonitor && n.type == NodeType::crossover ? routingFxSlot(n.id) : -1;
            const bool monitoring = monitorSlot >= 0 && fxMonitor->capturing(size_t(monitorSlot));
            StereoSample monitorInput{};
            int monitorBands = 0;
            for (int wire = 0; wire < step.inputCount; ++wire) {
                // Substitute only this edge; the source value and lineage stay raw.
                const auto audio = wire == step.sidechainVoiceWire && sources.sidechainVoiceAvailable
                    ? sources.sidechainVoice : values[step.inputs[wire]][step.sourcePins[wire]].audio;
                step.wireAudio[wire] = step.delays[wire].process(audio);
            }
            const auto incomingValue = [&](int wire) {
                auto incoming = values[step.inputs[wire]][step.sourcePins[wire]];
                incoming.audio = step.wireAudio[wire];
                return incoming;
            };
            const auto input = [&](int pin) -> Value {
                int contributors = 0, single = 0;
                for (int wire = 0; wire < step.inputCount; ++wire)
                    if (step.inputPins[wire] == pin) { ++contributors; single = wire; }
                if (contributors == 1)
                    return incomingValue(single);
                if (contributors == 0) return {};
                Value merged{};
                for (int wire = 0; wire < step.inputCount; ++wire) {
                    if (step.inputPins[wire] != pin) continue;
                    const auto incoming = incomingValue(wire);
                    for (size_t s = 0; s < merged.lineage.size(); ++s) merged.lineage[s] += incoming.lineage[s];
                }
                const float budget = n.type == NodeType::mix && n.mainControls && pin == 1 ? 2.0f : 1.0f;
                std::array<float, regionCount> scales{};
                for (int region = 0; region < active->regions; ++region) {
                    float power = 0;
                    for (int s = 0; s < 10; ++s) {
                        const float x = merged.lineage[s * lineageStride + region]; power += x*x;
                    }
                    scales[region] = contributors > 1 ? std::sqrt(budget / std::max(budget, power)) : 1;
                }
                merged = {};
                for (int wire = 0; wire < step.inputCount; ++wire) {
                    if (step.inputPins[wire] != pin) continue;
                    const auto incoming = incomingValue(wire);
                    float scale = 1;
                    for (int region = 0; region < active->regions; ++region)
                        for (int s = 0; s < 10; ++s)
                            if (incoming.lineage[s * lineageStride + region] != 0)
                                scale = std::min(scale, scales[region]);
                    merged.available |= incoming.available;
                    merged.audio.left += incoming.audio.left * scale;
                    merged.audio.right += incoming.audio.right * scale;
                    for (size_t s = 0; s < merged.lineage.size(); ++s) merged.lineage[s] += incoming.lineage[s] * scale;
                }
                return merged;
            };
            switch (n.type) {
            case NodeType::lead:
                value.available = true;
                value.audio = sources.lead;
                for (int r = 0; r < active->regions; ++r) value.lineage[r] = 1;
                break;
            case NodeType::sidechain:
                value.available = sources.sidechainAvailable;
                value.audio = sources.sidechain;
                for (int r = 0; r < active->regions; ++r) value.lineage[9 * lineageStride + r] = 1;
                break;
            case NodeType::voice:
                value.available = true;
                value.audio = sources.voices[n.voiceSlot];
                for (int r = 0; r < active->regions; ++r)
                    value.lineage[(n.voiceSlot + 1) * lineageStride + r] = std::sqrt(unit(sources.activity[n.voiceSlot], 0));
                break;
            case NodeType::sum: {
                for (int pin = 0; pin < maxInputs; ++pin) {
                    const auto in = input(pin);
                    value.available |= in.available;
                    value.audio.left += in.audio.left; value.audio.right += in.audio.right;
                    for (size_t s = 0; s < value.lineage.size(); ++s) value.lineage[s] += in.lineage[s];
                }
                float power = 0;
                for (int r = 0; r < active->regions; ++r) {
                    float p = 0;
                    for (int s = 0; s < 10; ++s) { const auto x = value.lineage[s * lineageStride + r]; p += x*x; }
                    power = std::max(power, p);
                }
                const float scale = std::sqrt(n.referencePower / std::max(n.referencePower, power));
                value.audio.left *= scale; value.audio.right *= scale;
                for (auto& x : value.lineage) x *= scale;
                break;
            }
            case NodeType::mix: {
                float m = unit(n.mainControls ? controls.mix : n.mix, 0.35f);
                if (!n.mainControls && controls.localMixAutomation && n.id >= 200 && n.id < 248) {
                    const float target = unit(controls.localMix[n.id - 200], n.mix);
                    if (!step.mixInitialized) { step.smoothedMix = target; step.mixInitialized = true; }
                    else step.smoothedMix += mixSmoothing * (target - step.smoothedMix);
                    if (std::abs(step.smoothedMix - target) < 1e-6f) step.smoothedMix = target;
                    m = float(step.smoothedMix);
                }
                const auto& dry = input(0); const auto& wet = input(1);
                value.available = dry.available || wet.available;
                value.audio = {blend(dry.audio.left, wet.audio.left, m), blend(dry.audio.right, wet.audio.right, m)};
                for (size_t s = 0; s < value.lineage.size(); ++s) value.lineage[s] = blend(dry.lineage[s], wet.lineage[s], m);
                break;
            }
            case NodeType::sidechainBlend: {
                value = input(0);
                if (!n.enabled || !sources.sidechainAvailable || !step.donorWired) {
                    step.mixInitialized = false;
                    break;
                }
                const float m = updateWetMix(step, unit(controls.sidechainBlendAutomation ? controls.sidechainBlend : n.mix, 0), true);
                if (m == 0.0f) break;
                const auto wet = input(1);
                if (!wet.available) {
                    step.mixInitialized = false;
                    break;
                }
                if (m == 1.0f) {
                    value = wet;
                    break;
                }
                value.available = true;
                value.audio = {blend(value.audio.left, wet.audio.left, m), blend(value.audio.right, wet.audio.right, m)};
                for (size_t s = 0; s < value.lineage.size(); ++s) value.lineage[s] = blend(value.lineage[s], wet.lineage[s], m);
                break;
            }
            case NodeType::splitter:
                // Copy audio AND provenance: each branch is full-level, but
                // coherent reconvergence still receives automatic compensation.
                value = input(0);
                ports[1] = value;
                break;
            case NodeType::crossover: {
                const auto in = input(0);
                if (monitoring) monitorInput = in.audio;
                const auto fx = effectSettings(n, controls);
                monitorBands = fx.enabled ? (fx.bandCount == 2 ? 2 : 3) : 1;
                // Crossover bypass has no fade: full-band on port zero, other ports silent.
                if (!fx.enabled) { value = in; break; }
                const auto bands = step.fx->crossover.process(in.audio.left, in.audio.right,
                    fx.bandCount, fx.crossoverLow, fx.crossoverHigh);
                const double lo = Multiband::cutoff(fx.crossoverLow, rate, 250);
                const double hi = std::max(lo, Multiband::cutoff(fx.crossoverHigh, rate, 4000));
                for (int port = 0; port < 3; ++port) {
                    if (fx.bandCount == 2 && port == 2) continue;
                    ports[port].available = in.available;
                    ports[port].audio = {bands[port][0], bands[port][1]};
                    for (int r = 0; r < active->regions; ++r) {
                        const double f = active->boundaries[r];
                        const int band = f < lo ? 0 : fx.bandCount == 2 ? 1 : f < hi ? 1 : 2;
                        if (band == port) for (int s = 0; s < 10; ++s)
                            ports[port].lineage[s * lineageStride + r] = in.lineage[s * lineageStride + r];
                    }
                }
                break;
            }
            case NodeType::chorale: case NodeType::waveshaper: case NodeType::equalizer: case NodeType::spatialEq:
            case NodeType::delay: case NodeType::reverb: case NodeType::compressor:
            case NodeType::bitcrusher: case NodeType::granular: case NodeType::convolution:
            case NodeType::saturator: case NodeType::vocalGlitch: case NodeType::spectralMorph:
            case NodeType::gain: case NodeType::stereo: case NodeType::gate:
            case NodeType::xpFilter: case NodeType::synthFilter: {
                value = input(0); // Only MAIN contributes audible provenance.
                if (step.donorWired) {
                    const auto donor = input(1);
                    value.audio = renderFx(step, value, donor.audio, donor.available);
                } else value.audio = renderFx(step, value, {}, false);
                break;
            }
            case NodeType::output: case NodeType::wetOutput: value = input(0); break;
            case NodeType::wetInput: break; // The owner seeds it before running the pocket.
            }
            if (monitoring) {
                auto monitorOutput = value.audio;
                std::array<std::array<float, 2>, 3> bands{};
                if (monitorBands != 0) {
                    monitorOutput = {};
                    for (int band = 0; band < monitorBands; ++band) {
                        const auto audio = ports[band].audio;
                        bands[size_t(band)] = {audio.left, audio.right};
                        monitorOutput.left += audio.left;
                        monitorOutput.right += audio.right;
                    }
                }
                fxMonitor->capture(size_t(monitorSlot), n.id,
                    monitorInput.left, monitorInput.right,
                    monitorOutput.left, monitorOutput.right, monitorBands, bands,
                    false, 0, 0, int(n.type), false);
            }
        };
        const auto renderOrdinaryFx = [&](Step& step, const Value& in, StereoSample donor, bool donorAvailable) {
            const auto fx = effectSettings(step.node, controls);
            const auto frame = renderEffect(step, in.audio, donor, donorAvailable, fx, controls);
            return finishEffect(step, in.audio, frame, fx, controls);
        };
        const auto renderTopLevelFx = [&](Step& step, const Value& in, StereoSample donor, bool donorAvailable) {
            if (step.wetResult < 0) return renderOrdinaryFx(step, in, donor, donorAvailable);
            const auto fx = effectSettings(step.node, controls);
            auto frame = renderEffect(step, in.audio, donor, donorAvailable, fx, controls);
            // Always clock the dry signal and the matching owner envelope. Live
            // bypass must not reveal an empty wet path early on re-enable.
            const auto aligned = step.pocketDry.process({frame.dry, frame.mix});
            frame.dry = aligned.audio;
            frame.mix = aligned.mix;
            const auto audio = frame.hasWet ? frame.wet : StereoSample{};
            if (step.wetInput >= 0) {
                auto& seed = values[step.wetInput][0];
                seed = {};
                seed.audio = audio;
                seed.available = in.available;
                // A pocket is one source, regardless of its owner's upstream
                // voice mix. Preserve main-graph lineage at the owner's output.
                for (int r = 0; r < active->regions; ++r) seed.lineage[r] = 1;
                // Clock only Wet Out's active ancestors, even with zero input
                // during bypass, so downstream filters and delays don't stall.
                for (int i = step.firstWetStep; i >= 0; i = active->steps[i].nextWetStep)
                    processStep(i, renderOrdinaryFx);
                frame.wet = values[step.wetResult][0].audio;
            } else {
                auto& insert = active->steps[step.wetResult];
                if (frame.hasWet || !step.pocketDry.data.empty()) {
                    Value input{};
                    input.audio = audio;
                    frame.wet = renderOrdinaryFx(insert, input, {}, false);
                } else insert.glitchStamp = 0;
            }
            return finishEffect(step, in.audio, frame, fx, controls);
        };
        for (int i = 0; i < active->count; ++i) {
            const auto& step = active->steps[i];
            if (step.connected && !step.node.wetOwner) processStep(i, renderTopLevelFx);
        }
        auto out = active->outputDelay.process(values[active->output][0].audio);
        // A short output-offset ramp removes the splice discontinuity without
        // rendering the old graph (which would double-clock shared machines).
        // This is de-clicking, not an old/new spectral crossfade.
        if (transition) {
            if (transition == transitionLength) correction = {last.left - out.left, last.right - out.right};
            const float weight = float(transition--) / float(transitionLength);
            out.left += correction.left * weight; out.right += correction.right * weight;
        }
        last = out; rendered = true;
        return out;
    }

private:
    // Nominal frequency regions are provenance, NOT ideal brick-wall audio.
    // At most two boundaries per crossover. Complementary bands partition the
    // source's lineage; duplicated same-band paths overlap and are compensated.
    static constexpr int regionCount = maxEffects * 2 + 1;
    template <int Regions>
    struct RoutedValue { bool available = false; StereoSample audio; std::array<float, 10 * Regions> lineage{}; };
    using Value = RoutedValue<regionCount>;
    template <class Sample>
    struct DelayLine {
        std::vector<Sample> data; size_t next = 0;
        Sample process(Sample in) noexcept {
            if (data.empty()) return in;
            const auto out = data[next]; data[next] = in; next = (next + 1) % data.size(); return out;
        }
        void reset() noexcept { std::fill(data.begin(), data.end(), Sample{}); next = 0; }
    };
    using SignalDelay = DelayLine<StereoSample>;
    struct Machine {
        uint64_t grainGeneration = 0;
        explicit Machine(const RoutingNode& n, bool splitWet)
            : type(n.type), wetOwner(n.wetOwner), splitsWet(splitWet) {}
        // Track the bypass envelopes owned by DSP classes that don't expose
        // their settled state. EQ/Shaper receive an initial tick so a machine
        // starting bypassed still fades in on enable.
        bool clockBypass(bool enabled, double target, double epsilon, bool adoptFirst = false) noexcept {
            if (!bypassInitialized && adoptFirst) {
                bypassInitialized = true;
                bypassBlend = target;
                return true;
            }
            const bool run = enabled || bypassBlend != 0.;
            bypassBlend += bypassStep * (target - bypassBlend);
            if (std::abs(bypassBlend - target) < epsilon) bypassBlend = target;
            return run;
        }
        void prepare(double sr) {
            if (type == NodeType::chorale || type == NodeType::equalizer || type == NodeType::waveshaper) {
                const double smoothRate = type == NodeType::chorale || (sr >= 8000 && sr <= 768000) ? sr : 48000;
                bypassStep = -std::expm1(-1.0 / (smoothRate * (type == NodeType::waveshaper ? .010 : .025)));
            } else if (type == NodeType::stereo) {
                bypassStep = 1. - std::exp(-1. / (.01 * utility_detail::rate(sr)));
            }
            switch (type) {
            case NodeType::chorale: chorale.prepare(sr); break;
            case NodeType::waveshaper: waveshaper.prepare(sr); break;
            case NodeType::equalizer: eq.prepare(sr); break;
            case NodeType::spatialEq:
                spatialEq = std::make_unique<SpatialEq>(); spatialEq->prepare(sr);
                bypassStep = -std::expm1(-1.0 / (sr * .025));
                break;
            case NodeType::crossover: crossover.prepare(sr); break;
            case NodeType::vocalGlitch: glitch = std::make_unique<VocalGlitch>(); glitch->prepare(sr); break;
            case NodeType::spectralMorph:
                spectral = std::make_unique<SpectralMorph>(); spectral->prepare(sr);
                alignedDry.data.resize(size_t(routingDeclaredLatency(type)));
                spectralMixStep = float(-std::expm1(-1.0 / (.020 * sr)));
                break;
            default:
                if (isCreativeEffect(type)) creative.prepare(sr, creativeKind(type));
                else if (isUtilityEffect(type)) utility.prepare(sr, utilityKind(type));
                break;
            }
        }
        void reset() {
            bypassInitialized = false; bypassBlend = choraleWetBlend = 0.;
            chorale.reset(); waveshaper.reset(); eq.reset(); crossover.reset(); creative.reset(); utility.reset();
            if (spectral) { spectral->reset(); alignedDry.reset(); spectralMix = 0; }
            if (glitch) glitch->reset();
            if (spatialEq) spatialEq->reset();
        }
        double bypassBlend = 0., bypassStep = 0., choraleWetBlend = 0.;
        bool bypassInitialized = false;
        const NodeType type;
        const std::uint32_t wetOwner;
        const bool splitsWet;
        Chorale chorale;
        Waveshaper waveshaper;
        Equalizer eq;
        std::unique_ptr<SpatialEq> spatialEq;
        SpatialEqSettings spatialEqTarget;
        Multiband crossover;
        CreativeEffect creative;
        UtilityEffect utility;
        std::unique_ptr<SpectralMorph> spectral;
        SignalDelay alignedDry;
        float spectralMix = 0, spectralMixStep = 0;
        std::unique_ptr<VocalGlitch> glitch;
    };
    static constexpr double moduleTailSeconds(NodeType type) noexcept {
        switch (type) {
        case NodeType::chorale: return .040;
        case NodeType::waveshaper: return Waveshaper::tailSeconds; // DC / IIR resampling / smoothing
        case NodeType::equalizer: return 256; // conservative extreme-Q/low-cutoff IIR decay
        case NodeType::spatialEq: return 8; // Low-frequency IIR ringing plus the reflection return.
        case NodeType::crossover: return 8;

        case NodeType::compressor: case NodeType::bitcrusher: return 2;
        case NodeType::vocalGlitch: return 6.25; // Up to one beat waiting plus one beat of playback at 20 BPM.
        case NodeType::convolution: return 3.6;
        case NodeType::saturator: return Waveshaper::tailSeconds;
        // Cover ringing and old comb contents across mode/cutoff/feedback edits,
        // even when today's mode is non-comb or the machine is bypassed.
        case NodeType::xpFilter: case NodeType::synthFilter: return 256;
        default: return 0;
        }
    }
    static bool isEffect(NodeType t) noexcept {
        return t == NodeType::crossover || t == NodeType::spatialEq || routingSupportsWetPocket(t);
    }
    static constexpr bool usesInternalWetMix(NodeType t) noexcept {
        return t == NodeType::chorale || t == NodeType::waveshaper || t == NodeType::equalizer
            || t == NodeType::spectralMorph;
    }
    struct Step {
        RoutingNode node;
        std::array<int, maxLinks> inputs{}, inputPins{}, sourcePins{};
        std::array<SignalDelay, maxLinks> delays;
        struct PocketDry { StereoSample audio; double mix = 0; };
        DelayLine<PocketDry> pocketDry;
        std::array<StereoSample, maxLinks> wireAudio{};
        bool donorWired = false, connected = false;
        int wetResult = -1, wetInput = -1, firstWetStep = -1, nextWetStep = -1;
        int sidechainVoiceWire = -1;
        int inputCount = 0;
        double smoothedMix = 0;
        bool mixInitialized = false;
        uint64_t glitchStamp = 0;
        std::shared_ptr<Machine> fx;
    };
    float updateWetMix(Step& step, float target, bool snap) const noexcept {
        if (!step.mixInitialized) { step.smoothedMix = target; step.mixInitialized = true; }
        else step.smoothedMix += mixSmoothing * (target - step.smoothedMix);
        if (snap && std::abs(step.smoothedMix - target) < 1.e-7) step.smoothedMix = target;
        return float(step.smoothedMix);
    }
    struct EffectFrame {
        StereoSample dry, wet;
        double mix = 0;
        bool hasWet = false;
    };

    EffectFrame renderEffect(Step& step, StereoSample dry, StereoSample donor, bool donorAvailable,
                             const FxSettings& fx, const RoutingControls& controls) noexcept {
        EffectFrame frame{dry, {}};
        if (!step.fx) return frame; // Prepared latency bypass wins over live automation.
        auto& machine = *step.fx;
        const auto type = step.node.type;
        const auto finiteDry = [&] {
            return StereoSample{std::isfinite(dry.left) ? dry.left : 0.f, std::isfinite(dry.right) ? dry.right : 0.f};
        };
        // Use native Mix unless this FX owns an insert. Only the owner needs
        // the full wet endpoint and a separately applied Mix envelope.
        const bool splitWet = machine.splitsWet;
        switch (type) {
        case NodeType::chorale:
            if (machine.clockBypass(fx.enabled, fx.enabled ? unit(fx.mix, 0.f) : 0.f, 1.e-6)) {
                if (!splitWet) {
                    machine.chorale.process(frame.dry.left, frame.dry.right, fx.enabled, fx.mix, fx.motion);
                    return frame;
                }
                frame.wet = dry;
                machine.chorale.process(frame.wet.left, frame.wet.right, true, 1.f, fx.motion);
                // Unlike EQ and Shaper, Chorale fades up from zero after
                // reset, even at Mix=1. Remove that internal dry contribution
                // before handing its delayed taps to the insert.
                machine.choraleWetBlend += machine.bypassStep * (1. - machine.choraleWetBlend);
                if (1. - machine.choraleWetBlend < 1.e-6) machine.choraleWetBlend = 1.;
                const float amount = float(machine.choraleWetBlend);
                if (amount == 0.f) frame.wet = {};
                else if (amount != 1.f) {
                    frame.wet.left = (frame.wet.left - dry.left * (1.f - amount)) / amount;
                    frame.wet.right = (frame.wet.right - dry.right * (1.f - amount)) / amount;
                }
                frame.mix = machine.bypassBlend;
                frame.hasWet = fx.enabled || frame.mix != 0.;
            }
            return frame;
        case NodeType::waveshaper: {
            auto settings = fx.waveshaper;
            settings.enabled = settings.enabled && fx.enabled;
            if (machine.clockBypass(settings.enabled, settings.enabled ? unit(settings.mix, 1.f) : 0.f, 1.e-9, true)) {
                auto& output = splitWet ? frame.wet : frame.dry;
                if (splitWet) {
                    frame.dry = frame.wet = finiteDry();
                    frame.mix = machine.bypassBlend;
                    frame.hasWet = settings.enabled || frame.mix != 0.;
                    settings.enabled = true;
                    settings.mix = 1.f;
                }
                machine.waveshaper.process(output.left, output.right, settings);
            }
            return frame;
        }
        case NodeType::equalizer: {
            auto settings = fx.eq;
            settings.enabled = settings.enabled && fx.enabled;
            if (machine.clockBypass(settings.enabled, settings.enabled ? 1. : 0., 1.e-8, true)) {
                auto& output = splitWet ? frame.wet : frame.dry;
                if (splitWet) {
                    frame.dry = frame.wet = finiteDry();
                    frame.mix = machine.bypassBlend;
                    frame.hasWet = settings.enabled || frame.mix != 0.;
                    settings.enabled = true;
                }
                machine.eq.process(output.left, output.right, settings);
            }
            return frame;
        }
        case NodeType::spatialEq: {
            const bool waking = !machine.bypassInitialized || machine.bypassBlend == 0.;
            if (!machine.clockBypass(fx.enabled, fx.enabled ? 1. : 0., 1.e-8, true)
                || (!fx.enabled && machine.bypassBlend == 0.)) return frame;
            const auto& target = fx.spatialEq;
            const auto sameBand = [](const auto& a, const auto& b) {
                return a.active == b.active && a.enabled == b.enabled && a.shape == b.shape
                    && a.frequency == b.frequency && a.gainDb == b.gainDb && a.q == b.q && a.depth == b.depth;
            };
            if (!std::equal(target.bands.begin(), target.bands.end(), machine.spatialEqTarget.bands.begin(), sameBand)) {
                machine.spatialEqTarget = target;
                machine.spatialEq->setSettings(target);
            }
            // Do not replay a frozen reflection tail after a settled node bypass.
            if (waking && fx.enabled) machine.spatialEq->reset();
            frame.wet = finiteDry();
            machine.spatialEq->process(frame.wet.left, frame.wet.right);
            frame.mix = machine.bypassBlend;
            frame.hasWet = true;
            return frame;
        }
        case NodeType::spectralMorph: {
            auto settings = fx.spectralMorph;
            settings.enabled = splitWet || fx.enabled;
            settings.mix = splitWet ? 1.f : fx.mix;
            frame.dry = machine.alignedDry.process(finiteDry());
            const auto wet = machine.spectral->process({dry.left, dry.right}, {donor.left, donor.right}, donorAvailable, settings);
            if (!splitWet) {
                if (fx.enabled) frame.dry = {wet[0], wet[1]};
                return frame;
            }
            frame.wet = {wet[0], wet[1]};
            const float target = fx.enabled ? unit(fx.mix, 1.f) : 0.f;
            if (!machine.bypassInitialized) {
                machine.bypassInitialized = true;
                machine.spectralMix = target;
            } else machine.spectralMix += machine.spectralMixStep * (target - machine.spectralMix);
            frame.hasWet = fx.enabled;
            frame.mix = fx.enabled ? machine.spectralMix : 0.f;
            return frame;
        }
        case NodeType::vocalGlitch: {
            using P = CreativeParameter;
            auto& glitch = *machine.glitch;
            const bool enabled = fx.enabled && controls.monitorWetEnabled;
            frame.mix = updateWetMix(step, enabled ? unit(fx.mix, .25f) : 0.f, true);
            if (!fx.enabled && frame.mix == 0.) { step.glitchStamp = 0; return frame; }
            if (step.glitchStamp != glitchStamp) {
                const bool waking = step.glitchStamp == 0;
                step.glitchStamp = glitchStamp;
                const double ppq = glitchOffset == 0 ? glitchPpq : glitchPpq + glitchOffset * glitchBpm / (60.0 * rate);
                glitch.setClock(ppq, glitchBpm, glitchPlaying, glitchDiscontinuity || !enabled || waking);
            }
            const auto choice = [&](P p) {
                const auto v = fx.creative[p];
                return std::isfinite(v) ? int(std::clamp(v, 0.f, 3.f)) : 0;
            };
            glitch.setSettings({fx.creative[P::glitchLength], fx.creative[P::glitchThreshold],
                choice(P::glitchPattern), choice(P::glitchGrid), fx.creative[P::glitchLayer] >= .5f,
                enabled && fx.creative[P::glitchAuto] >= .5f});
            if (enabled && glitchOffset == 0 && glitchTriggers != 0) {
                const auto slot = routingFxSlot(step.node.id);
                if (slot > 0 && slot < 16) {
                    const auto bits = glitchTriggers >> (2 * (slot - 1));
                    if (bits & 1u) glitch.grab();
                    if (bits & 2u) glitch.fire();
                }
            }
            glitch.process(dry.left, dry.right, frame.wet.left, frame.wet.right);
            frame.hasWet = step.wetResult < 0 || enabled || frame.mix != 0.;
            return frame;
        }
        default: {
            const bool utility = isUtilityEffect(type);
            const bool snap = utility || !fx.enabled || type == NodeType::convolution || type == NodeType::saturator;
            frame.mix = updateWetMix(step, fx.enabled ? unit(fx.mix, utility ? 1.f : .25f) : 0.f, snap);
            if (!fx.enabled && frame.mix == 0.) return frame;
            const auto wet = utility ? machine.utility.process(dry.left, dry.right, fx.utility)
                : machine.creative.process(dry.left, dry.right, fx.creative, controls.tempoBpm);
            frame.wet = {wet[0], wet[1]};
            frame.hasWet = true;
            return frame;
        }
        }
    }

    StereoSample finishEffect(Step& step, StereoSample in, const EffectFrame& frame,
                              const FxSettings& fx, const RoutingControls& controls) noexcept {
        const auto type = step.node.type;
        auto out = frame.dry;
        if (frame.mix != 0. || (step.wetResult < 0 && frame.hasWet)) {
            const auto mixSample = [&](float dry, float wet) {
                if (type == NodeType::equalizer || type == NodeType::waveshaper)
                    return float(dry * (1. - frame.mix) + wet * frame.mix);
                const float m = float(frame.mix);
                if (type == NodeType::chorale) return dry * (1.f - m) + wet * m;
                if (type == NodeType::spectralMorph) {
                    const float sample = blend(dry, wet, m);
                    return std::isfinite(sample) ? sample : 0.f;
                }
                return blend(dry, wet, m);
            };
            out = {mixSample(out.left, frame.wet.left), mixSample(out.right, frame.wet.right)};
        }
        if (type == NodeType::stereo && step.fx->clockBypass(fx.enabled, fx.enabled && fx.monoAudition ? 1. : 0., 1.e-7)) {
            const auto audition = step.fx->utility.applyMonoAudition({out.left, out.right}, fx.enabled && fx.monoAudition);
            out = {audition[0], audition[1]};
        }
        const int slot = fxMonitor ? routingFxSlot(step.node.id) : -1;
        if (slot < 0) return out;
        const bool showDynamics = fx.enabled && controls.monitorWetEnabled;
        if (isCreativeEffect(type) && type != NodeType::vocalGlitch)
            fxMonitor->setGainReduction(size_t(slot), showDynamics ? step.fx->creative.gainReductionDb() : 0.f);
        if (isUtilityEffect(type))
            fxMonitor->setGainReduction(size_t(slot), showDynamics && fx.mix > 0 ? step.fx->utility.gainReductionDb() : 0.f);
        if (fxMonitor->capturing(size_t(slot))) {
            const bool wetMeter = (type == NodeType::reverb || type == NodeType::convolution
                || type == NodeType::saturator || type == NodeType::compressor) && showDynamics && fx.mix > 0;
            UtilityDynamicsSnapshot dynamics{};
            if (type == NodeType::gate) {
                dynamics = step.fx->utility.dynamicsSnapshot();
                if (!showDynamics || fx.mix <= 0) dynamics.gainReductionDb = 0;
            }
            fxMonitor->capture(size_t(slot), step.node.id, in.left, in.right, out.left, out.right, 0, {},
                wetMeter, frame.wet.left, frame.wet.right, int(type),
                showDynamics && (type == NodeType::compressor || type == NodeType::gate), dynamics, type == NodeType::gate);
        }
        return out;
    }


    struct Plan {
        SignalDelay outputDelay;
        uint64_t revision = 0;
        unsigned routedVoices = 0x10000u;
        std::array<Step, maxNodes> steps;
        std::array<std::array<Value, 3>, maxNodes> values{};
        // Full-band graphs need ten source weights, not all 33 frequency regions.
        // Keep the established multiband path whenever a crossover is present.
        std::array<std::array<RoutedValue<1>, 3>, maxNodes> compactValues{};
        std::array<double, regionCount> boundaries{};
        int regions = 1, count = 0, output = 0;
        std::array<double, maxNodes> pathTails{};
        int sidechainVoiceStep = -1;
        bool hasCrossover = false;
    };
    static FxSettings effectSettings(const RoutingNode& n, const RoutingControls& controls) noexcept {
        const int slot = routingFxSlot(n.id);
        if (n.type == NodeType::chorale) {
            if (controls.fxAutomation && slot == 0)
                return FxSettings{controls.choraleOn, controls.choraleMix, controls.choraleMotion};
            if (controls.fxAutomation && slot > 0) return controls.fxSettings[size_t(slot)];
            return FxSettings{n.enabled && (!n.mainControls || controls.choraleOn),
                              n.mainControls ? controls.choraleMix : n.mix,
                              n.mainControls ? controls.choraleMotion : n.motion};
        }
        // Slot zero belongs exclusively to legacy Chorale, not new effect types.
        if (controls.fxAutomation && slot > 0) return controls.fxSettings[slot];
        FxSettings fx{n.enabled, n.mix, n.motion};
        fx.waveshaper = n.waveshaper; fx.eq = n.eq; fx.creative = n.creative; fx.utility = n.utility; fx.spectralMorph = n.spectralMorph; fx.monoAudition = n.monoAudition;
        fx.bandCount = n.bandCount; fx.crossoverLow = n.crossoverLow; fx.crossoverHigh = n.crossoverHigh;
        fx.spatialEq = n.spatialEq;
        return fx;
    }
    double creativeTailSeconds(NodeType type, const CreativeSettings& settings, double bpm) const noexcept {
        using P = CreativeParameter;
        const auto value = [&](P p) {
            const auto i = static_cast<size_t>(p);
            const auto& d = creativeParameters[i];
            const float x = settings.values[i];
            return double(std::isfinite(x) ? std::clamp(x, d.min, d.max) : d.initial);
        };
        // Practical decay allowances, not sample-exact silence. 250 ms covers
        // the creative one-pole filters and control slews, including 20 Hz HP.
        constexpr double residual = .25;
        switch (type) {
        case NodeType::delay: {
            const double tempo = std::isfinite(bpm) && bpm >= 20 && bpm <= 400 ? bpm : 120;
            const double time = value(P::delaySync) >= .5
                ? 60 * value(P::delayBeats) / tempo : .001 * value(P::delayTime);
            const double feedback = .01 * value(P::delayFeedback);
            // Include the first echo, then enough repeats to fall below -80 dB.
            const double repeats = feedback > 0 ? std::ceil(std::log(1.e-4) / std::log(feedback)) : 0;
            const double pitchWindow = std::abs(value(P::delayPitch)) > 1.e-6 ? .06 : 0;
            return (time + pitchWindow) * (1 + repeats) + residual;
        }
        case NodeType::reverb:
            if (value(P::reverbFreeze) >= .5) return std::numeric_limits<double>::infinity();
            // Pitched feedback adds up to a 60 ms read window per traversal.
            // Keep the old unpitched bound while allowing extra shimmer decay.
            return .001 * value(P::reverbPredelay) + residual
                + (value(P::reverbPitchAmount) > 0 && std::abs(value(P::reverbPitch)) > 1.e-6
                    ? 4 * value(P::reverbDecay) + .75 : 2 * value(P::reverbDecay));
        case NodeType::granular: {
            if (value(P::granularFreeze) >= .5) return std::numeric_limits<double>::infinity();
            // Old grains retain size/pitch across automation. Cover the maximum
            // lifetime plus reverse traversal (2x), not just today's grain size.
            const double maxGrain = .001 * creativeParameters[static_cast<size_t>(P::granularSize)].max;
            return .001 * (value(P::granularPosition) + value(P::granularJitter))
                + 3 * maxGrain + 4 / rate + residual;
        }
        default: return moduleTailSeconds(type);
        }
    }
    void updateTail(Plan& plan, const RoutingControls& controls) noexcept {
        // Bounded topological DP over cached arrays: serial adds, parallel max.
        // Only Output's result is published; dead ends (even frozen) cannot
        // contribute, but connected silent-input machines may retain history.
        for (int i = 0; i < plan.count; ++i) {
            const auto& step = plan.steps[i];
            if (!step.connected) continue;
            double upstream = 0;
            for (int wire = 0; wire < step.inputCount; ++wire)
                upstream = std::max(upstream, plan.pathTails[step.inputs[wire]]);
            const auto& n = step.node;
            const int slot = routingFxSlot(n.id);
            const auto& settings = controls.fxAutomation && slot > 0
                ? controls.fxSettings[size_t(slot)].creative : n.creative;
            plan.pathTails[i] = upstream + (isCreativeEffect(n.type)
                ? creativeTailSeconds(n.type, settings, controls.tempoBpm) : moduleTailSeconds(n.type));
            if (step.wetResult >= 0) plan.pathTails[i] += plan.pathTails[step.wetResult];
        }
        reportedTail.store(plan.pathTails[plan.output], std::memory_order_release);
    }
    void updateRegions(const RoutingControls& controls) noexcept {
        if (!active->hasCrossover) return;
        auto& b = active->boundaries;
        int used = 1; b[0] = 0;
        for (int i = 0; i < active->count; ++i) {
            const auto& step = active->steps[i];
            const auto& n = step.node;
            if (!step.connected || n.type != NodeType::crossover) continue;
            const auto fx = effectSettings(n, controls);
            if (!fx.enabled) continue;
            const double lo = Multiband::cutoff(fx.crossoverLow, rate, 250);
            b[used++] = lo;
            if (fx.bandCount != 2) b[used++] = std::max(lo, Multiband::cutoff(fx.crossoverHigh, rate, 4000));
        }
        std::sort(b.begin(), b.begin() + used);
        active->regions = int(std::unique(b.begin(), b.begin() + used) - b.begin());
    }
    static_assert(std::atomic<Plan*>::is_always_lock_free, "routing publication requires lock-free pointers");
    static float unit(float x, float fallback) noexcept {
        return std::isfinite(x) ? std::clamp(x, 0.0f, 1.0f) : fallback;
    }
    static float blend(float a, float b, float x) noexcept { return x == 1 ? b : a + (b - a) * x; }
    static int find(const Graph& g, std::uint32_t id) {
        for (int i = 0; i < int(g.nodes.size()); ++i) if (g.nodes[i].id == id) return i;
        return -1;
    }
    struct WetPockets {
        std::array<int, maxNodes> input, result; // result is Wet Out or the legacy insert
        WetPockets() { input.fill(-1); result.fill(-1); }
    };
    static RoutingValidation orderGraph(const Graph& g, std::array<int, maxNodes>& order,
                                        WetPockets* compiledPockets = nullptr) {
        if (g.nodes.empty() || g.nodes.size() > maxNodes || g.links.size() > maxLinks)
            return {false, "graph exceeds bounded node/link limits or is empty"};
        std::array<int, maxNodes> degree{};
        std::array<std::array<int, maxInputs>, maxNodes> pins{};
        std::array<bool, 10> sources{};
        WetPockets pockets;
        std::array<int, maxNodes> ownedCount{}, lastOwned{}, resultOwner{};
        resultOwner.fill(-1);
        int outputs = 0, effects = 0;
        for (int i = 0; i < int(g.nodes.size()); ++i) {
            const auto& n = g.nodes[i];
            if (!n.id || find(g, n.id) != i) return {false, "node IDs must be nonzero and unique"};
            if (n.latencySamples != 0) return {false, "additional branch latency is unsupported"};
            if (!std::isfinite(n.mix) || n.mix < 0 || n.mix > 1 || !std::isfinite(n.motion)
                || n.motion < 0 || n.motion > 1 || !std::isfinite(n.referencePower)
                || n.referencePower < 1 || n.referencePower > 16) return {false, "invalid node controls"};
            switch (n.type) {
            case NodeType::lead:
            case NodeType::sidechain:
            case NodeType::voice: {
                if (n.type == NodeType::voice && (n.voiceSlot < 0 || n.voiceSlot >= 8))
                    return {false, "invalid voice slot"};
                const int s = n.type == NodeType::sidechain ? 9 : n.type == NodeType::lead ? 0 : n.voiceSlot + 1;
                if (sources[s]) return {false, "duplicate source: fan out the existing source instead"};
                sources[s] = true; break;
            }
            case NodeType::crossover:
                if ((n.bandCount != 2 && n.bandCount != 3) || !std::isfinite(n.crossoverLow)
                    || !std::isfinite(n.crossoverHigh) || n.crossoverLow < 10
                    || n.crossoverHigh < n.crossoverLow) return {false, "invalid crossover controls"};
                [[fallthrough]];
            case NodeType::waveshaper:
                if (n.waveshaper.shape < 0 || n.waveshaper.shape >= int(Waveshaper::shapeNames.size())
                    || !std::isfinite(n.waveshaper.shapeAmount) || n.waveshaper.shapeAmount < 0
                    || n.waveshaper.shapeAmount > 1 || n.waveshaper.quality < 0
                    || n.waveshaper.quality >= int(Waveshaper::qualityNames.size()))
                    return {false, "invalid waveshaper shape/quality controls"};
                [[fallthrough]];
            case NodeType::equalizer:
            case NodeType::spatialEq:
            case NodeType::chorale: ++effects; break;
            case NodeType::spectralMorph: {
                const auto values = spectralMorphValues(n.spectralMorph);
                for (size_t p = 0; p < values.size(); ++p) {
                    const auto& d = spectralMorphParameters[p]; const float x = values[p];
                    if (!std::isfinite(x) || x < d.min || x > d.max || (d.discrete && x != std::round(x)))
                        return {false, "invalid Spectral Morph controls"};
                }
                ++effects; break;
            }
            case NodeType::delay: case NodeType::reverb: case NodeType::compressor:
            case NodeType::bitcrusher: case NodeType::granular:
            case NodeType::convolution: case NodeType::saturator: case NodeType::vocalGlitch:
                for (size_t p = 0; p < creativeParameterCount; ++p) {
                    const auto& d = creativeParameters[p]; const float x = n.creative.values[p];
                    if (!std::isfinite(x) || x < d.min || x > d.max) return {false, "invalid creative effect controls"};
                }
                ++effects; break;
            case NodeType::gain: case NodeType::stereo: case NodeType::gate:
            case NodeType::xpFilter: case NodeType::synthFilter:
                for (size_t p = 0; p < utilityParameterCount; ++p) {
                    const auto& d = utilityParameters[p]; const float x = n.utility.values[p];
                    if (!std::isfinite(x) || x < d.min || x > d.max || (d.discrete && x != std::round(x)))
                        return {false, "invalid utility effect controls"};
                }
                ++effects; break;
            case NodeType::output: ++outputs; break;
            case NodeType::wetInput: case NodeType::wetOutput:
                if (!n.wetOwner) return {false, "Wet In and Wet Out require a pocket owner"};
                break;
            case NodeType::sidechainBlend:
                if (n.id != sidechainBlendNodeId) return {false, "Sidechain Blend requires its dedicated node ID"};
                break;
            case NodeType::sum: case NodeType::mix: case NodeType::splitter: break;
            default: return {false, "unknown node type"};
            }
        }
        for (int i = 0; i < int(g.nodes.size()); ++i) {
            const auto& child = g.nodes[i];
            if (!child.wetOwner) continue;
            const int parent = find(g, child.wetOwner);
            if (parent < 0 || g.nodes[parent].wetOwner || !routingSupportsWetPocket(g.nodes[parent].type))
                return {false, "A wet pocket requires a compatible top-level FX owner"};
            if (child.type == NodeType::wetInput || child.type == NodeType::wetOutput) {
                auto& endpoint = child.type == NodeType::wetInput ? pockets.input[parent] : pockets.result[parent];
                if (endpoint >= 0) return {false, "Duplicate wet pocket endpoint"};
                endpoint = i;
            } else if (!routingSupportsWetInsert(child.type)) {
                return {false, "Node type is not allowed in a wet pocket"};
            }
            ++ownedCount[parent];
            lastOwned[parent] = i;
        }
        for (int i = 0; i < int(g.nodes.size()); ++i) if (ownedCount[i]) {
            if (pockets.input[i] < 0 || pockets.result[i] < 0) {
                // Legacy: exactly one single-wet FX, with no endpoints or
                // cables (scope and self-link checks below rule out any cable).
                if (ownedCount[i] != 1 || !routingSupportsWetPocket(g.nodes[lastOwned[i]].type))
                    return {false, "A wet pocket needs one Wet In and one Wet Out"};
                pockets.result[i] = lastOwned[i];
            }
            // The pocket's result precedes its owner for latency/tail analysis
            // and reverse reachability. Audio runs the pocket at the owner.
            resultOwner[pockets.result[i]] = i;
            ++degree[i];
        }
        if (effects > maxEffects || outputs != 1) return {false, "require one Output and at most 16 effects"};
        for (const auto& l : g.links) {
            const int s = find(g, l.source), d = find(g, l.destination);
            if (s < 0 || d < 0 || s == d) return {false, "invalid link endpoint"};
            if (g.nodes[s].wetOwner != g.nodes[d].wetOwner)
                return {false, "A cable cannot cross wet pocket scopes"};
            const auto type = g.nodes[d].type;
            const int count = routingInputCount(type);
            if (g.nodes[s].type == NodeType::output || g.nodes[s].type == NodeType::wetOutput
                || l.inputPin < 0 || l.inputPin >= count)
                return {false, "invalid input pin or output endpoint used as source"};
            const int outputPins = g.nodes[s].type == NodeType::crossover ? 3
                            : g.nodes[s].type == NodeType::splitter ? 2 : 1;
            if (l.sourcePin < 0 || l.sourcePin >= outputPins) return {false, "invalid source pin"};
            if (++pins[d][l.inputPin] > maxInputs) return {false, "input exceeds 16 incoming branches"};
            int duplicates = 0;
            for (const auto& other : g.links)
                if (other.source == l.source && other.destination == l.destination && other.inputPin == l.inputPin && other.sourcePin == l.sourcePin) ++duplicates;
            if (duplicates > 1) return {false, "duplicate connection"};
            ++degree[d];
        }
        // Unconnected pins are silence, not invalid edits. Reject structural
        // faults below, but never retain audible old wiring after a disconnect.
        int used = 0;
        std::array<bool, maxNodes> done{};
        while (used < int(g.nodes.size())) {
            int next = -1;
            for (int i = 0; i < int(g.nodes.size()); ++i) if (!done[i] && degree[i] == 0) { next = i; break; }
            if (next < 0) return {false, "routing cycle"};
            done[next] = true; order[used++] = next;
            if (resultOwner[next] >= 0) --degree[resultOwner[next]];
            for (const auto& l : g.links) if (l.source == g.nodes[next].id) --degree[find(g, l.destination)];
        }
        if (measureLatency(g, order, pockets, true).maximum > SpectralMorph::latencySamples)
            return {false, "Serial Spectral Morph exceeds the fixed 4096-sample graph budget"};
        if (compiledPockets) *compiledPockets = pockets;
        return {true, {}};
    }
    static int preparedLatency(const RoutingNode& node) noexcept {
        return node.enabled ? routingDeclaredLatency(node.type) : 0;
    }

    struct LatencyPaths {
        std::array<int, maxNodes> paths{}, local{};
        int output = 0, maximum = 0;
    };
    // Structural validation still budgets every declared machine and every
    // cable, irrespective of live bypass and whether a branch reaches Output.
    static LatencyPaths measureLatency(const Graph& g, const std::array<int, maxNodes>& order,
                                       const WetPockets& pockets, bool structural) {
        LatencyPaths latency;
        const auto effectLatency = [structural](const RoutingNode& node) {
            return structural ? routingDeclaredLatency(node.type) : preparedLatency(node);
        };
        for (size_t step = 0; step < g.nodes.size(); ++step) {
            const int i = order[step];
            const auto& node = g.nodes[i];

            auto& path = latency.paths[i];
            for (const auto& link : g.links)
                if (link.destination == node.id && (structural || routingUsesInput(node, link.inputPin)))
                    path = std::max(path, latency.paths[find(g, link.source)]);
            const int wetResult = pockets.result[i];
            latency.local[i] = effectLatency(node) + (wetResult >= 0 ? latency.paths[wetResult] : 0);
            path += latency.local[i];
            latency.maximum = std::max(latency.maximum, path);
            if (node.type == NodeType::output) latency.output = path;
        }
        return latency;
    }
public:
    // Longest prepared path into Output, before padding to the fixed reserve.
    // Use the same effective graph for this query and requestGraph(): enabled
    // must reflect the host target for latency-bearing nodes. Mix, source/pin
    // silence and live automation do not change a prepared plan's latency.
    // The path into Wet Out adds latency even if its owner is locally bypassed.
    // Invalid graphs continue to return the out-of-budget sentinel (4097).
    static int graphLatency(const Graph& g) {
        std::array<int, maxNodes> order{};
        WetPockets pockets;
        return orderGraph(g, order, &pockets) ? measureLatency(g, order, pockets, false).output
                                             : SpectralMorph::latencySamples + 1;
    }
private:
    void clear() {
        delete pending.exchange(nullptr); delete active; active = nullptr;
        collectRetired(); machines.clear();
        reportedTail.store(0, std::memory_order_release);
    }
    static_assert(std::atomic<double>::is_always_lock_free, "tail publication requires lock-free doubles");
    uint64_t nextGrainGeneration = 0, grainResetEpoch = 0;
        std::atomic<uint64_t> grainRevision{0}, publishedGrainEpoch{0};
        FxMonitor* fxMonitor = nullptr;
    std::atomic<double> reportedTail{0};
    int reserve = 0;
    double rate = 0;
    double glitchPpq = 0, glitchBpm = 120;
    uint64_t glitchStamp = 1, glitchOffset = 0;
    uint32_t glitchTriggers = 0;
    bool glitchPlaying = false, glitchDiscontinuity = false;
    float mixSmoothing = 0;
    Plan* active = nullptr;
    std::atomic<Plan*> pending{nullptr};
    std::array<Plan*, 4> retired{};
    std::atomic<unsigned> retireRead{0}, retireWrite{0};
    static_assert(std::atomic<unsigned>::is_always_lock_free, "retirement requires lock-free indices");
    std::unordered_map<std::uint32_t, std::weak_ptr<Machine>> machines;
    StereoSample last{}, correction{};
    int tailSamplesUntilUpdate = 0;
    int transition = 0, transitionLength = 480;
    bool rendered = false;
};

} // namespace vocalstack
