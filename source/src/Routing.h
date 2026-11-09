#pragma once

#include "Chorale.h"
#include "Waveshaper.h"
#include "Equalizer.h"
#include "Multiband.h"
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
enum class NodeType : std::uint8_t { lead = 0, voice = 1, chorale = 2, sum = 3, mix = 4, output = 5, waveshaper = 6, equalizer = 7, crossover = 8 };
struct RoutingNode {
    std::uint32_t id = 0;
    NodeType type = NodeType::lead;
    int voiceSlot = 0; // zero-based, stable 0..7
    bool enabled = true; // effect bypass; crossover bypass routes full-band to port zero
    float mix = 0.25f, motion = 0.35f;
    bool mainControls = false; // Mix: main Mix; Chorale: legacy Chorale controls
    float referencePower = 1.0f; // legacy Sum only; main Mix wet pin has a 2-voice budget
    int latencySamples = 0; // nonzero is unsupported, rejected rather than misaligned
    WaveshaperSettings waveshaper{};
    EqualizerSettings eq{};
    int bandCount = 3;
    float crossoverLow = 250, crossoverHigh = 4000;
};
struct RoutingLink {
    std::uint32_t source = 0, destination = 0;
    int inputPin = 0; // Mix dry=0/wet=1, Sum=0..15, other processors=0
    int sourcePin = 0; // crossover: Low=0, Mid=1, High=2; two-band: Low=0, High=1
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
// reclamation. All modules report zero additional integer sample latency.
// LR4/EQ IIR phase and waveshaper ADAA phase/attenuation are NOT delay alignment;
// parallel dry/processed paths may comb. No dry-null recombination is promised.
//
// Thread contract: prepare/reset/destruction require audio quiescence AND
// serialization with all control calls. requestGraph
// and collectRetired run on ONE serialized control thread, never audio. process
// runs on ONE audio thread. No processor-owned publication mechanism is needed.
// Request success means queued, applied on the next process call. Multiple queued
// edits collapse to the latest. Stable effect IDs AND types share history, but
// only the current plan renders: state is never advanced twice for a transition.
class RoutingExecutor {
public:
    static constexpr int maxNodes = 48, maxLinks = 128, maxEffects = 16, maxInputs = 16;
    // Theoretical graph limit, NOT the tail reported for every graph. IIR tails
    // are asymptotic; their finite reporting allowances do not truncate audio.
    static constexpr double maximumTailSeconds = maxEffects * 256.0;
    // Reports the active plan only. Queued edits become visible when audio
    // installs them; prepare publishes the default immediately. Controls/bypass
    // do not reduce bounds, since automation can reactivate connected effects.
    double tailSeconds() const noexcept { return reportedTail.load(std::memory_order_acquire); }
    RoutingExecutor() = default;
    RoutingExecutor(const RoutingExecutor&) = delete;
    RoutingExecutor& operator=(const RoutingExecutor&) = delete;
    ~RoutingExecutor() { clear(); }

    static RoutingValidation validate(const Graph& g) {
        std::array<int, maxNodes> order{};
        return orderGraph(g, order);
    }
    void prepare(double sampleRate) {
        if (!std::isfinite(sampleRate) || sampleRate <= 0 || sampleRate > std::numeric_limits<int>::max())
            throw std::invalid_argument("invalid routing sample rate");
        clear();
        rate = sampleRate;
        transitionLength = std::max(1, int(rate * 0.010));
        requestGraph(defaultGraph());
        active = pending.exchange(nullptr);
        reportedTail.store(active->tailSeconds, std::memory_order_release);
        reset();
    }
    RoutingValidation requestGraph(const Graph& g) {
        std::array<int, maxNodes> order{};
        auto result = orderGraph(g, order);
        if (!result) return result;
        if (!(rate > 0)) return {false, "prepare must precede requestGraph"};
        collectRetired();
        auto plan = std::make_unique<Plan>();
        plan->count = int(g.nodes.size());
        std::array<int, maxNodes> remap{};
        std::array<double, maxNodes> pathTails{};
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
            if (isEffect(step.node.type)) {
                auto& weak = machines[step.node.id];
                step.fx = weak.lock();
                if (!step.fx || step.fx->type != step.node.type) {
                    step.fx = std::make_shared<Machine>(step.node.type);
                    step.fx->prepare(rate);
                    weak = step.fx;
                }
            }
            // Topological DP: serial effects add, parallel branches take max.
            // Reading only Output's result excludes every disconnected/dead-end
            // branch, but retains tails of connected machines with silent input.
            for (int wire = 0; wire < step.inputCount; ++wire)
                pathTails[i] = std::max(pathTails[i], pathTails[step.inputs[wire]]);
            pathTails[i] += moduleTailSeconds(step.node.type);
            plan->hasCrossover |= step.node.type == NodeType::crossover;
            if (step.node.type == NodeType::output) plan->output = i;
        }
        plan->tailSeconds = pathTails[plan->output];
        delete pending.exchange(plan.release(), std::memory_order_acq_rel);
        // Do not accumulate expired machine identities after repeated editing.
        for (auto it = machines.begin(); it != machines.end();)
            if (it->second.expired()) it = machines.erase(it); else ++it;
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
        // Include old-type machines still owned by active/retired plans, not
        // only the latest identity map. Quiescent audio/control is required.
        std::array<Machine*, maxNodes * 6> seen{};
        size_t used = 0;
        const auto resetPlan = [&](Plan* plan) {
            if (!plan) return;
            for (int i = 0; i < plan->count; ++i) if (auto* machine = plan->steps[i].fx.get()) {
                if (std::find(seen.begin(), seen.begin() + used, machine) == seen.begin() + used) {
                    seen[used++] = machine; machine->reset();
                }
            }
        };
        resetPlan(active); resetPlan(pending.load());
        for (auto r = retireRead.load(); r != retireWrite.load(); ++r) resetPlan(retired[r % retired.size()]);
        last = correction = {};
        transition = 0;
        rendered = false;
    }
    StereoSample process(const RoutingSources& sources, const RoutingControls& controls) noexcept {
        const auto write = retireWrite.load(std::memory_order_relaxed);
        // SPSC retirement queue: bounded work, no audio CAS retry loop. If the
        // control thread stops collecting, retain the current plan safely.
        if (write - retireRead.load(std::memory_order_acquire) < retired.size()) {
            if (auto* next = pending.exchange(nullptr, std::memory_order_acq_rel)) {
                auto* old = active;
                active = next;
                reportedTail.store(next->tailSeconds, std::memory_order_release);
                if (old) {
                    retired[write % retired.size()] = old;
                    retireWrite.store(write + 1, std::memory_order_release);
                }
                transition = rendered ? transitionLength : 0;
            }
        }
        if (!active) return {};
        updateRegions(controls);
        for (int i = 0; i < active->count; ++i) {
            auto& step = active->steps[i];
            auto& ports = active->values[i];
            // Non-crossover plans never write ports 1/2; they stay zero from
            // plan construction. Avoid clearing three full provenance buffers.
            if (step.node.type == NodeType::crossover) ports = {};
            else ports[0] = {};
            auto& value = ports[0];
            const auto& n = step.node;
            const auto input = [&](int pin) -> Value {
                int contributors = 0, single = 0;
                for (int wire = 0; wire < step.inputCount; ++wire)
                    if (step.inputPins[wire] == pin) { ++contributors; single = wire; }
                if (contributors == 1)
                    return active->values[step.inputs[single]][step.sourcePins[single]];
                if (contributors == 0) return {};
                Value merged{};
                for (int wire = 0; wire < step.inputCount; ++wire) {
                    if (step.inputPins[wire] != pin) continue;
                    const auto& incoming = active->values[step.inputs[wire]][step.sourcePins[wire]];
                    for (size_t s = 0; s < merged.lineage.size(); ++s) merged.lineage[s] += incoming.lineage[s];
                }
                const float budget = n.type == NodeType::mix && n.mainControls && pin == 1 ? 2.0f : 1.0f;
                std::array<float, regionCount> scales{};
                for (int region = 0; region < active->regions; ++region) {
                    float power = 0;
                    for (int s = 0; s < 9; ++s) {
                        const float x = merged.lineage[s * regionCount + region]; power += x*x;
                    }
                    scales[region] = contributors > 1 ? std::sqrt(budget / std::max(budget, power)) : 1;
                }
                merged = {};
                for (int wire = 0; wire < step.inputCount; ++wire) {
                    if (step.inputPins[wire] != pin) continue;
                    const auto& incoming = active->values[step.inputs[wire]][step.sourcePins[wire]];
                    float scale = 1;
                    for (int region = 0; region < active->regions; ++region)
                        for (int s = 0; s < 9; ++s)
                            if (incoming.lineage[s * regionCount + region] != 0)
                                scale = std::min(scale, scales[region]);
                    merged.audio.left += incoming.audio.left * scale;
                    merged.audio.right += incoming.audio.right * scale;
                    for (size_t s = 0; s < merged.lineage.size(); ++s) merged.lineage[s] += incoming.lineage[s] * scale;
                }
                return merged;
            };
            switch (n.type) {
            case NodeType::lead:
                value.audio = sources.lead;
                for (int r = 0; r < active->regions; ++r) value.lineage[r] = 1;
                break;
            case NodeType::voice:
                value.audio = sources.voices[n.voiceSlot];
                for (int r = 0; r < active->regions; ++r)
                    value.lineage[(n.voiceSlot + 1) * regionCount + r] = std::sqrt(unit(sources.activity[n.voiceSlot], 0));
                break;
            case NodeType::sum: {
                for (int pin = 0; pin < maxInputs; ++pin) {
                    const auto in = input(pin);
                    value.audio.left += in.audio.left; value.audio.right += in.audio.right;
                    for (size_t s = 0; s < value.lineage.size(); ++s) value.lineage[s] += in.lineage[s];
                }
                float power = 0;
                for (int r = 0; r < active->regions; ++r) {
                    float p = 0;
                    for (int s = 0; s < 9; ++s) { const auto x = value.lineage[s * regionCount + r]; p += x*x; }
                    power = std::max(power, p);
                }
                const float scale = std::sqrt(n.referencePower / std::max(n.referencePower, power));
                value.audio.left *= scale; value.audio.right *= scale;
                for (auto& x : value.lineage) x *= scale;
                break;
            }
            case NodeType::mix: {
                const float m = unit(n.mainControls ? controls.mix : n.mix, 0.35f);
                const auto& dry = input(0); const auto& wet = input(1);
                value.audio = {blend(dry.audio.left, wet.audio.left, m), blend(dry.audio.right, wet.audio.right, m)};
                for (size_t s = 0; s < value.lineage.size(); ++s) value.lineage[s] = blend(dry.lineage[s], wet.lineage[s], m);
                break;
            }
            case NodeType::chorale: {
                value = input(0);
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
                const auto fx = effectSettings(n, controls);
                auto settings = fx.waveshaper; settings.enabled = settings.enabled && fx.enabled;
                step.fx->waveshaper.process(value.audio.left, value.audio.right, settings);
                break;
            }
            case NodeType::equalizer: {
                value = input(0);
                const auto fx = effectSettings(n, controls);
                auto settings = fx.eq; settings.enabled = settings.enabled && fx.enabled;
                step.fx->eq.process(value.audio.left, value.audio.right, settings);
                break;
            }
            case NodeType::crossover: {
                const auto in = input(0);
                const auto fx = effectSettings(n, controls);
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
                        if (band == port) for (int s = 0; s < 9; ++s)
                            ports[port].lineage[s * regionCount + r] = in.lineage[s * regionCount + r];
                    }
                }
                break;
            }
            case NodeType::output: value = input(0); break;
            }
        }
        auto out = active->values[active->output][0].audio;
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
    struct Value { StereoSample audio; std::array<float, 9 * regionCount> lineage{}; };
    struct Machine {
        explicit Machine(NodeType t) : type(t) {}
        void prepare(double sr) {
            switch (type) {
            case NodeType::chorale: chorale.prepare(sr); break;
            case NodeType::waveshaper: waveshaper.prepare(sr); break;
            case NodeType::equalizer: eq.prepare(sr); break;
            case NodeType::crossover: crossover.prepare(sr); break;
            default: break;
            }
        }
        void reset() { chorale.reset(); waveshaper.reset(); eq.reset(); crossover.reset(); }
        NodeType type;
        Chorale chorale;
        Waveshaper waveshaper;
        Equalizer eq;
        Multiband crossover;
    };
    static constexpr double moduleTailSeconds(NodeType type) noexcept {
        switch (type) {
        case NodeType::chorale: return .040;
        case NodeType::waveshaper: return .020; // previous driven sample / smoothed blend
        case NodeType::equalizer: return 256; // conservative extreme-Q/low-cutoff IIR decay
        case NodeType::crossover: return 8;
        default: return 0;
        }
    }
    static bool isEffect(NodeType t) noexcept {
        return t == NodeType::chorale || t == NodeType::waveshaper
            || t == NodeType::equalizer || t == NodeType::crossover;
    }
    struct Step {
        RoutingNode node;
        std::array<int, maxLinks> inputs{}, inputPins{}, sourcePins{};
        int inputCount = 0;
        std::shared_ptr<Machine> fx;
    };
    struct Plan {
        std::array<Step, maxNodes> steps;
        std::array<std::array<Value, 3>, maxNodes> values{};
        std::array<double, regionCount> boundaries{};
        int regions = 1, count = 0, output = 0;
        double tailSeconds = 0;
        bool hasCrossover = false;
    };
    static FxSettings effectSettings(const RoutingNode& n, const RoutingControls& controls) noexcept {
        const int slot = routingFxSlot(n.id);
        // Slot zero belongs exclusively to legacy Chorale, not new effect types.
        if (controls.fxAutomation && slot > 0) return controls.fxSettings[slot];
        FxSettings fx{n.enabled, n.mix, n.motion};
        fx.waveshaper = n.waveshaper; fx.eq = n.eq;
        fx.bandCount = n.bandCount; fx.crossoverLow = n.crossoverLow; fx.crossoverHigh = n.crossoverHigh;
        return fx;
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
        std::array<bool, 9> sources{};
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
            case NodeType::voice: {
                if (n.type == NodeType::voice && (n.voiceSlot < 0 || n.voiceSlot >= 8))
                    return {false, "invalid voice slot"};
                const int s = n.type == NodeType::lead ? 0 : n.voiceSlot + 1;
                if (sources[s]) return {false, "duplicate source: fan out the existing source instead"};
                sources[s] = true; break;
            }
            case NodeType::crossover:
                if ((n.bandCount != 2 && n.bandCount != 3) || !std::isfinite(n.crossoverLow)
                    || !std::isfinite(n.crossoverHigh) || n.crossoverLow < 10
                    || n.crossoverHigh < n.crossoverLow) return {false, "invalid crossover controls"};
                [[fallthrough]];
            case NodeType::waveshaper: case NodeType::equalizer:
            case NodeType::chorale: ++effects; break;
            case NodeType::output: ++outputs; break;
            case NodeType::sum: case NodeType::mix: break;
            default: return {false, "unknown node type"};
            }
        }
        if (effects > maxEffects || outputs != 1) return {false, "require one Output and at most 16 effects"};
        for (const auto& l : g.links) {
            const int s = find(g, l.source), d = find(g, l.destination);
            if (s < 0 || d < 0 || s == d) return {false, "invalid link endpoint"};
            const auto type = g.nodes[d].type;
            const int count = type == NodeType::sum ? maxInputs : type == NodeType::mix ? 2 :
                (type == NodeType::output || isEffect(type)) ? 1 : 0;
            if (g.nodes[s].type == NodeType::output || l.inputPin < 0 || l.inputPin >= count)
                return {false, "invalid input pin or Output used as source"};
            const int outputPins = g.nodes[s].type == NodeType::crossover ? 3 : 1;
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
        return {true, {}};
    }
    void clear() {
        delete pending.exchange(nullptr); delete active; active = nullptr;
        collectRetired(); machines.clear();
        reportedTail.store(0, std::memory_order_release);
    }
    static_assert(std::atomic<double>::is_always_lock_free, "tail publication requires lock-free doubles");
    std::atomic<double> reportedTail{0};
    double rate = 0;
    Plan* active = nullptr;
    std::atomic<Plan*> pending{nullptr};
    std::array<Plan*, 4> retired{};
    std::atomic<unsigned> retireRead{0}, retireWrite{0};
    static_assert(std::atomic<unsigned>::is_always_lock_free, "retirement requires lock-free indices");
    std::unordered_map<std::uint32_t, std::weak_ptr<Machine>> machines;
    StereoSample last{}, correction{};
    int transition = 0, transitionLength = 480;
    bool rendered = false;
};

} // namespace vocalstack
