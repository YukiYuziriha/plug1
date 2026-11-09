#pragma once

#include "Chorale.h"
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

// Serializable model. IDs identify machines, not positions in nodes[]. Changing a
// Chorale ID creates a fresh machine; retaining it retains delay/modulation history.
enum class NodeType : std::uint8_t { lead = 0, voice = 1, chorale = 2, sum = 3, mix = 4, output = 5 };
struct RoutingNode {
    std::uint32_t id = 0;
    NodeType type = NodeType::lead;
    int voiceSlot = 0; // zero-based, stable 0..7
    bool enabled = true; // Chorale bypass; other nodes retain their explicit wiring
    float mix = 0.25f, motion = 0.35f;
    bool mainControls = false; // Mix: main Mix; Chorale: legacy Chorale controls
    float referencePower = 1.0f; // Sum only; default wet sum uses legacy 2-voice budget
    int latencySamples = 0; // nonzero is unsupported, rejected rather than misaligned
};
struct RoutingLink {
    std::uint32_t source = 0, destination = 0;
    int inputPin = 0; // Mix dry=0/wet=1, Sum=0..15, other processors=0
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
    RoutingNode sum{10, NodeType::sum}; sum.referencePower = 2;
    RoutingNode mix{11, NodeType::mix}; mix.mainControls = true;
    RoutingNode fx{12, NodeType::chorale}; fx.mainControls = true;
    g.nodes.insert(g.nodes.end(), {sum, mix, fx, {13, NodeType::output}});
    for (int i = 0; i < 8; ++i) g.links.push_back({std::uint32_t(i + 2), 10, i});
    g.links.insert(g.links.end(), {{1, 11, 0}, {10, 11, 1}, {11, 12, 0}, {12, 13, 0}});
    return g;
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

// Sum contract: carry a nine-source lineage vector through the DAG. Distinct
// sources are orthogonal, duplicated paths are correlated, FX retain provenance,
// and Mix linearly blends provenance. Sum divides audio AND lineage by
// max(1, norm(sum(lineage)) / sqrt(referencePower)). Voice gain/pan knobs are
// deliberately excluded. Each explicit Sum establishes its own power budget;
// grouping is hierarchical, not an automatic gain rider or a measured-correlation
// claim. Modified/phase-inverted FX may not remain correlated in actual audio.
//
// Bounded executor chosen instead of AudioProcessorGraph: no JUCE dependency,
// explicit mixer/pin semantics, and a raw-pointer audio handoff with off-thread
// reclamation. All supported modules have zero additional dry-path latency.
//
// Thread contract: prepare/reset/destruction require audio quiescence AND
// serialization with all control calls. requestGraph
// and collectRetired run on ONE serialized control thread, never audio. process
// runs on ONE audio thread. No processor-owned publication mechanism is needed.
// Request success means queued, applied on the next process call. Multiple queued
// edits collapse to the latest. Stable Chorale IDs share state between plans, but
// only the current plan renders: state is never advanced twice for a transition.
class RoutingExecutor {
public:
    static constexpr int maxNodes = 48, maxLinks = 128, maxEffects = 16, maxInputs = 16;
    static constexpr double maximumTailSeconds = maxEffects * 0.040;
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
        for (int i = 0; i < plan->count; ++i) remap[order[i]] = i;
        for (int i = 0; i < plan->count; ++i) {
            auto& step = plan->steps[i];
            step.node = g.nodes[order[i]];
            step.inputs.fill(-1);
            for (const auto& link : g.links) if (link.destination == step.node.id) {
                const int source = find(g, link.source);
                step.inputs[link.inputPin] = remap[source];
            }
            if (step.node.type == NodeType::chorale) {
                auto& weak = machines[step.node.id];
                step.fx = weak.lock();
                if (!step.fx) {
                    step.fx = std::make_shared<Chorale>();
                    step.fx->prepare(rate);
                    weak = step.fx;
                }
            }
            if (step.node.type == NodeType::output) plan->output = i;
        }
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
        // Include pending/retired plans: a queued edit can retain a machine
        // absent from the current plan. Reset each shared machine exactly once.
        for (auto& entry : machines) if (auto machine = entry.second.lock()) machine->reset();
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
                if (old) {
                    retired[write % retired.size()] = old;
                    retireWrite.store(write + 1, std::memory_order_release);
                }
                transition = rendered ? transitionLength : 0;
            }
        }
        if (!active) return {};
        for (int i = 0; i < active->count; ++i) {
            auto& step = active->steps[i];
            auto& value = values[i];
            value = {};
            const auto& n = step.node;
            const auto input = [&](int pin) -> const Value& { return values[step.inputs[pin]]; };
            switch (n.type) {
            case NodeType::lead:
                value.audio = sources.lead; value.lineage[0] = 1; break;
            case NodeType::voice:
                value.audio = sources.voices[n.voiceSlot];
                value.lineage[n.voiceSlot + 1] = std::sqrt(unit(sources.activity[n.voiceSlot], 0));
                break;
            case NodeType::sum: {
                for (int pin = 0; pin < maxInputs; ++pin) if (step.inputs[pin] >= 0) {
                    const auto& in = input(pin);
                    value.audio.left += in.audio.left; value.audio.right += in.audio.right;
                    for (int s = 0; s < 9; ++s) value.lineage[s] += in.lineage[s];
                }
                float power = 0;
                for (auto x : value.lineage) power += x * x;
                const float scale = std::sqrt(n.referencePower / std::max(n.referencePower, power));
                value.audio.left *= scale; value.audio.right *= scale;
                for (auto& x : value.lineage) x *= scale;
                break;
            }
            case NodeType::mix: {
                const float m = unit(n.mainControls ? controls.mix : n.mix, 0.35f);
                const auto& dry = input(0); const auto& wet = input(1);
                value.audio = {blend(dry.audio.left, wet.audio.left, m), blend(dry.audio.right, wet.audio.right, m)};
                for (int s = 0; s < 9; ++s) value.lineage[s] = blend(dry.lineage[s], wet.lineage[s], m);
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
                step.fx->process(value.audio.left, value.audio.right, fx.enabled, fx.mix, fx.motion);
                break;
            }
            case NodeType::output: value = input(0); break;
            }
        }
        auto out = values[active->output].audio;
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
    struct Value { StereoSample audio; std::array<float, 9> lineage{}; };
    struct Step { RoutingNode node; std::array<int, maxInputs> inputs{}; std::shared_ptr<Chorale> fx; };
    struct Plan { std::array<Step, maxNodes> steps; int count = 0, output = 0; };
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
        std::array<std::array<bool, maxInputs>, maxNodes> pins{};
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
                (type == NodeType::output || type == NodeType::chorale) ? 1 : 0;
            if (g.nodes[s].type == NodeType::output || l.inputPin < 0 || l.inputPin >= count)
                return {false, "invalid input pin or Output used as source"};
            if (pins[d][l.inputPin]) return {false, "input already connected: use an explicit Sum"};

            pins[d][l.inputPin] = true; ++degree[d];
        }
        for (int i = 0; i < int(g.nodes.size()); ++i) {
            const auto type = g.nodes[i].type;
            if ((type == NodeType::chorale || type == NodeType::output) && !pins[i][0])
                return {false, "processor input is disconnected"};
            if (type == NodeType::mix && (!pins[i][0] || !pins[i][1])) return {false, "Mix needs dry and wet pins"};
            if (type == NodeType::sum && degree[i] == 0) return {false, "Sum needs at least one input"};
        }
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
    }
    double rate = 0;
    Plan* active = nullptr;
    std::atomic<Plan*> pending{nullptr};
    std::array<Plan*, 4> retired{};
    std::atomic<unsigned> retireRead{0}, retireWrite{0};
    static_assert(std::atomic<unsigned>::is_always_lock_free, "retirement requires lock-free indices");
    std::unordered_map<std::uint32_t, std::weak_ptr<Chorale>> machines;
    std::array<Value, maxNodes> values{};
    StereoSample last{}, correction{};
    int transition = 0, transitionLength = 480;
    bool rendered = false;
};

} // namespace vocalstack
