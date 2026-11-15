#pragma once

#include "Chorale.h"
#include "Waveshaper.h"
#include "Equalizer.h"
#include "Multiband.h"
#include "FxMonitor.h"
#include "CreativeEffects.h"
#include "UtilityFilters.h"
#include "SpectralMorph.h"
#include "SpectralMorphBridge.h"
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
// an effect ID or type creates fresh state; retaining both retains DSP history.
enum class NodeType : std::uint8_t { lead = 0, voice = 1, chorale = 2, sum = 3, mix = 4, output = 5, waveshaper = 6, equalizer = 7, crossover = 8, delay = 9, reverb = 10, compressor = 11, bitcrusher = 12, granular = 13, gain = 14, stereo = 15, gate = 16, xpFilter = 17, synthFilter = 18, splitter = 19, convolution = 20, saturator = 21, sidechain = 22, spectralMorph = 23 };

// Named signal inputs are shared by validation, prepared plans and presentation.
// Additional multi-input effects extend this declaration, not host-buffer access.
struct EffectInputDeclaration {
    const char* name;
    bool auxiliary;
};
inline constexpr int routingInputCount(NodeType type) noexcept {
    if (type == NodeType::lead || type == NodeType::voice || type == NodeType::sidechain) return 0;
    if (type == NodeType::sum) return 16;
    return type == NodeType::mix || type == NodeType::spectralMorph ? 2 : 1;
}
inline constexpr EffectInputDeclaration routingInput(NodeType type, int pin) noexcept {
    if (type == NodeType::spectralMorph) return {pin == 0 ? "MAIN" : "DONOR", pin == 1};
    return type == NodeType::mix ? EffectInputDeclaration{pin == 0 ? "DRY" : "WET", false}
                                 : EffectInputDeclaration{"MAIN", false};
}
inline constexpr bool isCreativeEffect(NodeType type) noexcept {
    return (type >= NodeType::delay && type <= NodeType::granular)
        || type == NodeType::convolution || type == NodeType::saturator;
}
inline constexpr CreativeKind creativeKind(NodeType type) noexcept {
    if (type == NodeType::convolution) return CreativeKind::convolution;
    if (type == NodeType::saturator) return CreativeKind::saturator;
    return static_cast<CreativeKind>(int(type) - int(NodeType::delay));
}
inline constexpr bool isUtilityEffect(NodeType type) noexcept {
    return type >= NodeType::gain && type <= NodeType::synthFilter;
}
inline constexpr UtilityKind utilityKind(NodeType type) noexcept {
    return static_cast<UtilityKind>(int(type) - int(NodeType::gain));
}
struct RoutingNode {
    std::uint32_t id = 0;
    NodeType type = NodeType::lead;
    int voiceSlot = 0; // zero-based, stable 0..7
    bool enabled = true; // effect bypass; crossover bypass routes full-band to port zero
    float mix = isUtilityEffect(type) ? 1.0f : 0.25f, motion = 0.35f;
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
};
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

inline Graph defaultGraph() {
    Graph g;
    g.nodes.push_back({1, NodeType::lead});
    for (int i = 0; i < 8; ++i) g.nodes.push_back({std::uint32_t(i + 2), NodeType::voice, i});
    RoutingNode mix{11, NodeType::mix}; mix.mainControls = true;
    RoutingNode fx{12, NodeType::chorale}; fx.mainControls = true;
    g.nodes.insert(g.nodes.end(), {mix, fx, {13, NodeType::output}});
    for (int i = 0; i < 8; ++i) g.links.push_back({std::uint32_t(i + 2), 11, 1});
    g.links.insert(g.links.end(), {{1, 11, 0}, {11, 12, 0}, {12, 13, 0}});
    return g;
}

// Control-thread migration: old explicit Sum boxes become ordinary fan-in
// cables. Runtime merges now own compensation, so no Sum is exposed in the UI.
inline Graph removeLegacySumNodes(const Graph& old) {
    Graph graph;
    const auto isSum = [&](std::uint32_t id) {
        for (const auto& n : old.nodes) if (n.id == id) return n.type == NodeType::sum;
        return false;
    };
    for (const auto& n : old.nodes) if (n.type != NodeType::sum) graph.nodes.push_back(n);
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
// reclamation. Spectral Morph contributes a fixed 4096-sample integer delay.
// Prepared per-wire delays align every dependency (including auxiliary pins),
// and the output is padded to the caller-provided fixed reserve. Paths exceeding
// that reserve are rejected, including serial Morph. Donor provenance never
// contributes to the audible MAIN lineage. EQ/LR4 and ADAA/IIR phase are not
// estimated or removed by this integer-delay compensation.
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
    RoutingValidation requestGraph(const Graph& g) {
        std::array<int, maxNodes> order{};
        auto result = orderGraph(g, order);
        if (!result) return result;
        if (!(rate > 0)) return {false, "prepare must precede requestGraph"};
        if (graphLatency(g) > reserve) return {false, "Routing exceeds the prepared latency reserve; serial Spectral Morph is unsupported"};
        collectRetired();
        auto plan = std::make_unique<Plan>();
        plan->count = int(g.nodes.size());
        std::array<int, maxNodes> remap{};

        for (int i = 0; i < plan->count; ++i) remap[order[i]] = i;
        for (int i = 0; i < plan->count; ++i) {
            auto& step = plan->steps[i];
            step.node = g.nodes[order[i]];
            for (const auto& link : g.links) if (link.destination == step.node.id) {
                const int source = find(g, link.source);
                step.inputs[step.inputCount] = remap[source];
                step.sourcePins[step.inputCount] = link.sourcePin;
                step.inputPins[step.inputCount++] = link.inputPin;
            }
            int arrival = 0;
            for (int wire = 0; wire < step.inputCount; ++wire)
                arrival = std::max(arrival, plan->steps[step.inputs[wire]].pathLatency);
            step.pathLatency = arrival + (step.node.type == NodeType::spectralMorph ? SpectralMorph::latencySamples : 0);
            for (int wire = 0; wire < step.inputCount; ++wire) {
                step.delays[wire].data.resize(size_t(arrival - plan->steps[step.inputs[wire]].pathLatency));
                step.donorWired |= routingInput(step.node.type, step.inputPins[wire]).auxiliary;
            }
            if (isEffect(step.node.type)) {
                auto& weak = machines[step.node.id];
                step.fx = weak.lock();
                if (!step.fx || step.fx->type != step.node.type) {
                    step.fx = std::make_shared<Machine>(step.node.type);
                    step.fx->prepare(rate);
                    step.fx->grainGeneration = ++nextGrainGeneration;
                    weak = step.fx;
                }
            }

            plan->hasCrossover |= step.node.type == NodeType::crossover;
            if (step.node.type == NodeType::output) plan->output = i;
        }

        plan->outputDelay.data.resize(size_t(reserve - plan->steps[plan->output].pathLatency));
        plan->revision = grainRevision.fetch_add(1, std::memory_order_acq_rel) + 1;
        delete pending.exchange(plan.release(), std::memory_order_acq_rel);
        // Do not accumulate expired machine identities after repeated editing.
        for (auto it = machines.begin(); it != machines.end();) {
            const auto machine = it->second.lock();
            // Retired/active plans may still own deleted effects. Only ID/type
            // survivors may reuse history, even when pending edits coalesce.
            const int index = find(g, it->first);
            if (!machine || index < 0 || g.nodes[size_t(index)].type != machine->type)
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
    StereoSample process(const RoutingSources& sources, const RoutingControls& controls) noexcept {
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
        if (!active) return {};
        if (tailSamplesUntilUpdate == 0) {
            updateTail(*active, controls);
            tailSamplesUntilUpdate = 2048;
        }
        --tailSamplesUntilUpdate;
        if (fxMonitor) fxMonitor->beginSample();
        updateRegions(controls);
        return active->hasCrossover ? processPlan(sources, controls, active->values)
                                    : processPlan(sources, controls, active->compactValues);
    }

private:
    template <class Values>
    StereoSample processPlan(const RoutingSources& sources, const RoutingControls& controls, Values& values) noexcept {
        using Value = typename Values::value_type::value_type;
        constexpr size_t lineageStride = std::tuple_size<decltype(Value{}.lineage)>::value / 10;
        for (int i = 0; i < active->count; ++i) {
            auto& step = active->steps[i];
            auto& ports = values[i];
            // Only crossover needs to clear every port (bypassed/unused bands).
            // Splitter overwrites both outputs with complete routed values.
            if (step.node.type == NodeType::crossover) ports = {};
            else ports[0] = {};
            auto& value = ports[0];
            const auto& n = step.node;
            const int monitorSlot = fxMonitor && isEffect(n.type) ? routingFxSlot(n.id) : -1;
            const bool monitoring = monitorSlot >= 0 && fxMonitor->capturing(size_t(monitorSlot));
            StereoSample monitorInput{}, monitorWet{};
            bool monitorHasWet = false, monitorHasGainReduction = false, monitorHasUtilityDynamics = false;
            UtilityDynamicsSnapshot monitorDynamics{};
            int monitorBands = 0;
            for (int wire = 0; wire < step.inputCount; ++wire)
                step.wireAudio[wire] = step.delays[wire].process(values[step.inputs[wire]][step.sourcePins[wire]].audio);
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
            case NodeType::splitter:
                // Copy audio AND provenance: each branch is full-level, but
                // coherent reconvergence still receives automatic compensation.
                value = input(0);
                ports[1] = value;
                break;
            case NodeType::chorale: {
                value = input(0);
                if (monitoring) monitorInput = value.audio;
                const int slot = routingFxSlot(n.id);
                FxSettings fx{n.enabled && (!n.mainControls || controls.choraleOn),
                              n.mainControls ? controls.choraleMix : n.mix,
                              n.mainControls ? controls.choraleMotion : n.motion};
                if (controls.fxAutomation && slot >= 0) {
                    // Slot zero aliases existing parameters, not a second source
                    // of truth. Live automation replaces node bypass as well.
                    fx = slot == 0 ? FxSettings{controls.choraleOn, controls.choraleMix, controls.choraleMotion}
                                   : controls.fxSettings[slot];
                }
                step.fx->chorale.process(value.audio.left, value.audio.right, fx.enabled, fx.mix, fx.motion);
                break;
            }
            case NodeType::waveshaper: {
                value = input(0);
                if (monitoring) monitorInput = value.audio;
                const auto fx = effectSettings(n, controls);
                auto settings = fx.waveshaper; settings.enabled = settings.enabled && fx.enabled;
                step.fx->waveshaper.process(value.audio.left, value.audio.right, settings);
                break;
            }
            case NodeType::equalizer: {
                value = input(0);
                if (monitoring) monitorInput = value.audio;
                const auto fx = effectSettings(n, controls);
                auto settings = fx.eq; settings.enabled = settings.enabled && fx.enabled;
                step.fx->eq.process(value.audio.left, value.audio.right, settings);
                break;
            }
            case NodeType::crossover: {
                const auto in = input(0);
                if (monitoring) monitorInput = in.audio;
                const auto fx = effectSettings(n, controls);
                monitorBands = fx.enabled ? (fx.bandCount == 2 ? 2 : 3) : 1;
                // Disabled crossover is full-band on port zero; other ports silent.
                // Keep all filter histories warm even while bypassed.
                const auto bands = step.fx->crossover.process(in.audio.left, in.audio.right,
                    fx.bandCount, fx.crossoverLow, fx.crossoverHigh);
                if (!fx.enabled) { value = in; break; }
                const double lo = Multiband::cutoff(fx.crossoverLow, rate, 250);
                const double hi = std::max(lo, Multiband::cutoff(fx.crossoverHigh, rate, 4000));
                for (int port = 0; port < 3; ++port) {
                    if (fx.bandCount == 2 && port == 2) continue;
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
            case NodeType::delay: case NodeType::reverb: case NodeType::compressor:
            case NodeType::bitcrusher: case NodeType::granular:
            case NodeType::convolution: case NodeType::saturator: {
                value = input(0);
                if (monitoring) monitorInput = value.audio;
                const auto fx = effectSettings(n, controls);
                const auto wet = step.fx->creative.process(value.audio.left, value.audio.right, fx.creative, controls.tempoBpm);
                if (monitoring && (n.type == NodeType::reverb || n.type == NodeType::convolution || n.type == NodeType::saturator || n.type == NodeType::compressor)) {
                    // Actual processed wet samples, not dry input or a parameter model.
                    // Bypass/zero Mix invalidates measured history, not DSP state.
                    monitorHasWet = controls.monitorWetEnabled && fx.enabled && fx.mix > 0;
                    if (monitorHasWet) monitorWet = {wet[0], wet[1]};
                }
                const float target = fx.enabled ? unit(fx.mix, .25f) : 0.f;
                if (!step.mixInitialized) { step.smoothedMix = target; step.mixInitialized = true; }
                else step.smoothedMix += mixSmoothing * (target - step.smoothedMix);
                if ((n.type == NodeType::convolution || n.type == NodeType::saturator)
                    && std::abs(step.smoothedMix - target) < 1.e-7) step.smoothedMix = target;
                const float m = float(step.smoothedMix);
                value.audio = {blend(value.audio.left, wet[0], m), blend(value.audio.right, wet[1], m)};
                if (fxMonitor && monitorSlot >= 0)
                    fxMonitor->setGainReduction(size_t(monitorSlot), fx.enabled && controls.monitorWetEnabled ? step.fx->creative.gainReductionDb() : 0.f);
                monitorHasGainReduction = n.type == NodeType::compressor && fx.enabled && controls.monitorWetEnabled;
                break;
            }
            case NodeType::spectralMorph: {
                value = input(0); // Only MAIN contributes audible provenance.
                const auto donor = input(1);
                const auto fx = effectSettings(n, controls);
                auto settings = fx.spectralMorph; settings.enabled = fx.enabled; settings.mix = fx.mix;
                const auto wet = step.fx->spectral->process({value.audio.left, value.audio.right},
                    {donor.audio.left, donor.audio.right}, step.donorWired && donor.available, settings);
                value.audio = {wet[0], wet[1]};
                break;
            }
            case NodeType::gain: case NodeType::stereo: case NodeType::gate:
            case NodeType::xpFilter: case NodeType::synthFilter: {
                value = input(0);
                if (monitoring) monitorInput = value.audio;
                const auto fx = effectSettings(n, controls);
                // Clock the wet machine even while dry so bypass preserves history.
                const auto wet = step.fx->utility.process(value.audio.left, value.audio.right, fx.utility);
                if (monitoring && n.type == NodeType::gate) {
                    monitorHasUtilityDynamics = true;
                    monitorDynamics = step.fx->utility.dynamicsSnapshot();
                    if (!fx.enabled || fx.mix <= 0 || !controls.monitorWetEnabled) monitorDynamics.gainReductionDb = 0;
                    monitorHasGainReduction = fx.enabled && controls.monitorWetEnabled;
                }
                const float target = fx.enabled ? unit(fx.mix, 1.f) : 0.f;
                if (!step.mixInitialized) { step.smoothedMix = target; step.mixInitialized = true; }
                else step.smoothedMix += mixSmoothing * (target - step.smoothedMix);
                if (std::abs(step.smoothedMix - target) < 1.e-7) step.smoothedMix = target;
                const float m = float(step.smoothedMix);
                value.audio = {blend(value.audio.left, wet[0], m), blend(value.audio.right, wet[1], m)};
                if (n.type == NodeType::stereo) {
                    const auto audition = step.fx->utility.applyMonoAudition({value.audio.left, value.audio.right}, fx.enabled && fx.monoAudition);
                    // Slot bypass already fades Mix; release audition smoothly
                    // with it. Full/global bypass selects aligned dry in the engine.
                    value.audio = {audition[0], audition[1]};
                }
                if (fxMonitor && monitorSlot >= 0)
                    fxMonitor->setGainReduction(size_t(monitorSlot), fx.enabled && fx.mix > 0 && controls.monitorWetEnabled ? step.fx->utility.gainReductionDb() : 0.f);
                break;
            }
            case NodeType::output: value = input(0); break;
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
                                        monitorHasWet, monitorWet.left, monitorWet.right, int(n.type), monitorHasGainReduction,
                                                                                monitorDynamics, monitorHasUtilityDynamics);
            }
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
    struct Machine {
        uint64_t grainGeneration = 0;
        explicit Machine(NodeType t) : type(t) {}
        void prepare(double sr) {
            switch (type) {
            case NodeType::chorale: chorale.prepare(sr); break;
            case NodeType::waveshaper: waveshaper.prepare(sr); break;
            case NodeType::equalizer: eq.prepare(sr); break;
            case NodeType::crossover: crossover.prepare(sr); break;
            case NodeType::spectralMorph: spectral = std::make_unique<SpectralMorph>(); spectral->prepare(sr); break;
            default:
                if (isCreativeEffect(type)) creative.prepare(sr, creativeKind(type));
                else if (isUtilityEffect(type)) utility.prepare(sr, utilityKind(type));
                break;
            }
        }
        void reset() { chorale.reset(); waveshaper.reset(); eq.reset(); crossover.reset(); creative.reset(); utility.reset(); if (spectral) spectral->reset(); }
        NodeType type;
        Chorale chorale;
        Waveshaper waveshaper;
        Equalizer eq;
        Multiband crossover;
        CreativeEffect creative;
        UtilityEffect utility;
        std::unique_ptr<SpectralMorph> spectral;
    };
    static constexpr double moduleTailSeconds(NodeType type) noexcept {
        switch (type) {
        case NodeType::chorale: return .040;
        case NodeType::waveshaper: return Waveshaper::tailSeconds; // DC / IIR resampling / smoothing
        case NodeType::equalizer: return 256; // conservative extreme-Q/low-cutoff IIR decay
        case NodeType::crossover: return 8;

        case NodeType::compressor: case NodeType::bitcrusher: return 2;
        case NodeType::convolution: return 3.6;
        case NodeType::saturator: return Waveshaper::tailSeconds;
        // Cover ringing and old comb contents across mode/cutoff/feedback edits,
        // even when today's mode is non-comb or the machine is bypassed.
        case NodeType::xpFilter: case NodeType::synthFilter: return 256;
        default: return 0;
        }
    }
    static bool isEffect(NodeType t) noexcept {
        return t == NodeType::chorale || t == NodeType::waveshaper
            || t == NodeType::equalizer || t == NodeType::crossover || isCreativeEffect(t) || isUtilityEffect(t) || t == NodeType::spectralMorph;
    }
    struct SignalDelay {
        std::vector<StereoSample> data; size_t next = 0;
        StereoSample process(StereoSample in) noexcept {
            if (data.empty()) return in;
            const auto out = data[next]; data[next] = in; next = (next + 1) % data.size(); return out;
        }
        void reset() noexcept { std::fill(data.begin(), data.end(), StereoSample{}); next = 0; }
    };
    struct Step {
        RoutingNode node;
        std::array<int, maxLinks> inputs{}, inputPins{}, sourcePins{};
        std::array<SignalDelay, maxLinks> delays;
        std::array<StereoSample, maxLinks> wireAudio{};
        int pathLatency = 0; bool donorWired = false;
        int inputCount = 0;
        double smoothedMix = 0;
        bool mixInitialized = false;
        std::shared_ptr<Machine> fx;
    };
    struct Plan {
        SignalDelay outputDelay;
        uint64_t revision = 0;
        std::array<Step, maxNodes> steps;
        std::array<std::array<Value, 3>, maxNodes> values{};
        // Full-band graphs need ten source weights, not all 33 frequency regions.
        // Keep the established multiband path whenever a crossover is present.
        std::array<std::array<RoutedValue<1>, 3>, maxNodes> compactValues{};
        std::array<double, regionCount> boundaries{};
        int regions = 1, count = 0, output = 0;
        std::array<double, maxNodes> pathTails{};
        bool hasCrossover = false;
    };
    static FxSettings effectSettings(const RoutingNode& n, const RoutingControls& controls) noexcept {
        const int slot = routingFxSlot(n.id);
        // Slot zero belongs exclusively to legacy Chorale, not new effect types.
        if (controls.fxAutomation && slot > 0) return controls.fxSettings[slot];
        FxSettings fx{n.enabled, n.mix, n.motion};
        fx.waveshaper = n.waveshaper; fx.eq = n.eq; fx.creative = n.creative; fx.utility = n.utility; fx.spectralMorph = n.spectralMorph; fx.monoAudition = n.monoAudition;
        fx.bandCount = n.bandCount; fx.crossoverLow = n.crossoverLow; fx.crossoverHigh = n.crossoverHigh;
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
            double upstream = 0;
            for (int wire = 0; wire < step.inputCount; ++wire)
                upstream = std::max(upstream, plan.pathTails[step.inputs[wire]]);
            const auto& n = step.node;
            const int slot = routingFxSlot(n.id);
            const auto& settings = controls.fxAutomation && slot > 0
                ? controls.fxSettings[size_t(slot)].creative : n.creative;
            plan.pathTails[i] = upstream + (isCreativeEffect(n.type)
                ? creativeTailSeconds(n.type, settings, controls.tempoBpm) : moduleTailSeconds(n.type));
        }
        reportedTail.store(plan.pathTails[plan.output], std::memory_order_release);
    }
    void updateRegions(const RoutingControls& controls) noexcept {
        if (!active->hasCrossover) return;
        auto& b = active->boundaries;
        int used = 1; b[0] = 0;
        for (int i = 0; i < active->count; ++i) {
            const auto& n = active->steps[i].node;
            if (n.type != NodeType::crossover) continue;
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
    static RoutingValidation orderGraph(const Graph& g, std::array<int, maxNodes>& order) {
        if (g.nodes.empty() || g.nodes.size() > maxNodes || g.links.size() > maxLinks)
            return {false, "graph exceeds bounded node/link limits or is empty"};
        std::array<int, maxNodes> degree{};
        std::array<std::array<int, maxInputs>, maxNodes> pins{};
        std::array<bool, 10> sources{};
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
            case NodeType::convolution: case NodeType::saturator:
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
            case NodeType::sum: case NodeType::mix: case NodeType::splitter: break;
            default: return {false, "unknown node type"};
            }
        }
        if (effects > maxEffects || outputs != 1) return {false, "require one Output and at most 16 effects"};
        for (const auto& l : g.links) {
            const int s = find(g, l.source), d = find(g, l.destination);
            if (s < 0 || d < 0 || s == d) return {false, "invalid link endpoint"};
            const auto type = g.nodes[d].type;
            const int count = routingInputCount(type);
            if (g.nodes[s].type == NodeType::output || l.inputPin < 0 || l.inputPin >= count)
                return {false, "invalid input pin or Output used as source"};
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
            for (const auto& l : g.links) if (l.source == g.nodes[next].id) --degree[find(g, l.destination)];
        }
        if (graphLatencyOrdered(g, order) > SpectralMorph::latencySamples)
            return {false, "Serial Spectral Morph exceeds the fixed 4096-sample graph budget"};
        return {true, {}};
    }
    static int graphLatencyOrdered(const Graph& g, const std::array<int, maxNodes>& order) {
        std::array<int, maxNodes> latency{}; int maximum = 0;
        for (size_t step = 0; step < g.nodes.size(); ++step) {
            const int i = order[step];
            for (const auto& link : g.links) if (link.destination == g.nodes[i].id)
                latency[i] = std::max(latency[i], latency[find(g, link.source)]);
            if (g.nodes[i].type == NodeType::spectralMorph) latency[i] += SpectralMorph::latencySamples;
            maximum = std::max(maximum, latency[i]);
        }
        return maximum;
    }
public:
    // Includes dormant/disconnected Morph nodes: bypass, automation and wiring
    // edits cannot change the host contract within a prepared activation.
    static int graphLatency(const Graph& g) {
        std::array<int, maxNodes> order{};
        return orderGraph(g, order) ? graphLatencyOrdered(g, order) : SpectralMorph::latencySamples + 1;
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
