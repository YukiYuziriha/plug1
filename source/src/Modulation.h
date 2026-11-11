#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>
#include <span>

namespace vocalstack {

// Control-thread value model. Depth is signed normalized target-range displacement.
// division is quarter notes/cycle (1 = quarter, 4 = bar in 4/4).
enum class ModulatorType { lfo, random };
enum class ModulationWaveform { sine, triangle, saw, square };
enum class ModulationRetrigger { free, transport, note };
struct Modulator {
    uint32_t id = 0;
    ModulatorType type = ModulatorType::lfo;
    float rateHz = 1, division = 1, phase = 0, depth = 1;
    bool sync = false, bipolar = true;
    ModulationRetrigger retrigger = ModulationRetrigger::free;
    ModulationWaveform waveform = ModulationWaveform::sine;
    float shape = 0.5f, smoothing = 0;
};
struct Assignment {
    uint32_t modulatorId = 0;
    juce::String targetParameterId;
    float depth = 0;
    bool enabled = true;
};
struct ModulationState {
    std::vector<Modulator> modulators;
    std::vector<Assignment> assignments;
};
struct ModulatorSnapshot { uint32_t id = 0; float output = 0, phase = 0; };
struct ModulationTargetSnapshot {
    juce::String parameterId;
    float baseNormalized = 0, effectiveNormalized = 0, contribution = 0;
};
struct ModulationSnapshot {
    std::vector<ModulatorSnapshot> modulators;
    std::vector<ModulationTargetSnapshot> targets;
    // False means a bounded display read overlapped audio publication; keep the
    // previous display until the next UI timer. No partial frame is returned.
    // Assigned values and source outputs share a tick; unassigned bases are live
    // host reads, not a transactional snapshot of concurrent host automation.
    bool consistent = true;
};

// Original monophonic implementation inspired by the Classic LFO / Random
// descriptions in https://www.bitwig.com/userguide/latest/modulator/ . Shape is
// phase skew; Random uses uniform xorshift targets with optional cubic slew.
// Transport retrigger means restart-on-play, not timeline/PPQ phase locking.
// Fixed storage, single audio consumer, externally serialized control producer.
// No shared_ptr destruction, strings, allocation, or locks on audio. Pending plans
// are latest-wins; a slot is returned only after the audio consumer releases it.
class ModulationBackend {
public:
    static constexpr size_t maxModulators = 16, maxAssignments = 256, maxTargets = 2048;
    static constexpr int quantum = 32;
    struct ResolvedAssignment { uint16_t modulator = 0, target = 0; float depth = 0; };
    struct Plan {
        std::array<Modulator, maxModulators> modulators{};
        std::array<ResolvedAssignment, maxAssignments> assignments{};
        size_t modulatorCount = 0, assignmentCount = 0;
        // Rebuilt by publish(), never trusted from callers.
        std::array<uint16_t, maxAssignments> targetIndices{};
        size_t targetCount = 0;
    };
    bool publish(const Plan& plan) noexcept {
        if (plan.modulatorCount > maxModulators || plan.assignmentCount > maxAssignments) return false;
        for (size_t i = 0; i < plan.modulatorCount; ++i) {
            const auto& m = plan.modulators[i];
            if (!m.id || !std::isfinite(m.rateHz) || m.rateHz < 0.001f || m.rateHz > 100
                || !std::isfinite(m.division) || m.division < 0.015625f || m.division > 256
                || !std::isfinite(m.phase) || m.phase < 0 || m.phase >= 1
                || !std::isfinite(m.depth) || m.depth < 0 || m.depth > 1
                || !std::isfinite(m.shape) || m.shape < 0 || m.shape > 1
                || !std::isfinite(m.smoothing) || m.smoothing < 0 || m.smoothing > 1
                || int(m.type) < 0 || int(m.type) > 1 || int(m.waveform) < 0 || int(m.waveform) > 3
                || int(m.retrigger) < 0 || int(m.retrigger) > 2) return false;
            for (size_t previous = 0; previous < i; ++previous)
                if (plan.modulators[previous].id == m.id) return false;
        }
        for (size_t i = 0; i < plan.assignmentCount; ++i) {
            const auto& a = plan.assignments[i];
            if (a.modulator >= plan.modulatorCount || a.target >= maxTargets
                || !std::isfinite(a.depth) || std::abs(a.depth) > 1) return false;
        }
        for (int i = 0; i < 4; ++i) {
            int expected = 0;
            if (!slots[size_t(i)].owned.compare_exchange_strong(expected, 1)) continue;
            auto& resolved = slots[size_t(i)].plan;
            resolved = plan;
            resolved.targetCount = 0;
            for (size_t a = 0; a < resolved.assignmentCount; ++a) {
                const auto target = resolved.assignments[a].target;
                const auto end = resolved.targetIndices.begin() + resolved.targetCount;
                if (std::find(resolved.targetIndices.begin(), end, target) == end)
                    resolved.targetIndices[resolved.targetCount++] = target;
            }
            const int old = pending.exchange(i, std::memory_order_acq_rel);
            if (old >= 0) slots[size_t(old)].owned.store(0, std::memory_order_release);
            return true;
        }
        return false;
    }
    void prepare(double sampleRate) noexcept {
        rate = std::isfinite(sampleRate) && sampleRate >= 4000 ? sampleRate : 48000;
        // Quiescent audio only. Install the latest control plan before resetting
        // so reset never resurrects a pre-edit runtime or leaves stale RNG state.
        install();
        remaining = 0; eventTickPending = false; runtime = {};
        for (size_t i = 0; i < activePlan().modulatorCount; ++i) resetRuntime(i, activePlan().modulators[i]);
        snapshotSequence.fetch_add(1);
        snapshotCount.store(0);
        snapshotSequence.fetch_add(1);
    }
    bool isIdle() const noexcept {
        return activePlan().modulatorCount == 0 && !eventTickPending
            && pending.load(std::memory_order_acquire) < 0;
    }
    std::span<const uint16_t> targets() const noexcept {
        return {activePlan().targetIndices.data(), activePlan().targetCount};
    }
    int samplesUntilTick() const noexcept { return eventTickPending ? 0 : remaining; }
    void advance(int samples) noexcept {
        for (size_t i = 0; i < activePlan().modulatorCount; ++i) runtime[i].elapsed += samples;
        remaining -= samples;
    }
    void retrigger(ModulationRetrigger mode) noexcept {
        install();
        for (size_t i = 0; i < activePlan().modulatorCount; ++i)
            if (activePlan().modulators[i].retrigger == mode) {
                resetRuntime(i, activePlan().modulators[i]);
                eventTickPending = true;
            }
    }
    void setTempo(double bpm) noexcept { if (std::isfinite(bpm) && bpm > 0) tempo = std::clamp(bpm, 1.0, 1000.0); }
    void tick(std::array<float, maxTargets>& contributions) noexcept {
        install();
        contributions.fill(0);
        const bool eventOnly = remaining > 0;
        auto& p = activePlan();
        for (size_t i = 0; i < p.modulatorCount; ++i) {
            auto& r = runtime[i]; const auto& m = p.modulators[i];
            // An off-grid note resets only its own sources. Other oscillators
            // keep both their phase integration interval and held output.
            if (eventOnly && !r.needsOutput) continue;
            r.phase += r.elapsed * r.hz / rate;
            r.elapsed = 0;
            while (r.phase >= 1) { r.phase -= 1; r.previous = r.next; r.next = random(r.seed); }
            r.hz = m.sync ? tempo / (60.0 * m.division) : m.rateHz;
            const float t = unitPhase(r.phase);
            float value = 0;
            if (m.type == ModulatorType::random) {
                const float blend = m.smoothing == 0 ? 1.0f
                    : std::clamp(t / m.smoothing, 0.0f, 1.0f);
                value = r.previous + (r.next - r.previous) * blend * blend * (3 - 2 * blend);
            } else {
                // Shape bends phase around an adjustable midpoint without changing frequency.
                const float pivot = 0.01f + 0.98f * m.shape;
                const float x = t < pivot ? 0.5f * t / pivot : 0.5f + 0.5f * (t - pivot) / (1 - pivot);
                switch (m.waveform) {
                    case ModulationWaveform::sine: value = std::sin(6.28318530718f * x); break;
                    case ModulationWaveform::triangle: value = 1 - 4 * std::abs(x - 0.5f); break;
                    case ModulationWaveform::saw: value = 2 * x - 1; break;
                    case ModulationWaveform::square: value = x < 0.5f ? 1.0f : -1.0f; break;
                }
            }
            if (!m.bipolar) value = 0.5f * (value + 1);
            outputs[i] = value * m.depth;
            r.needsOutput = false;
        }
        snapshotSequence.fetch_add(1);
        for (size_t i = 0; i < p.modulatorCount; ++i) {
            snapshotId[i].store(p.modulators[i].id);
            snapshotOutput[i].store(outputs[i]);
            snapshotPhase[i].store(unitPhase(runtime[i].phase));
        }
        snapshotCount.store(p.modulatorCount);
        snapshotSequence.fetch_add(1);
        for (size_t i = 0; i < p.assignmentCount; ++i) {
            const auto& a = p.assignments[i];
            contributions[a.target] += outputs[a.modulator] * a.depth;
        }
        if (!eventOnly) remaining = quantum;
        eventTickPending = false;
    }
    std::vector<ModulatorSnapshot> snapshots(bool* consistent = nullptr) const {
        std::vector<ModulatorSnapshot> result;
        result.reserve(maxModulators);
        // Sequentially consistent payload atomics make this a race-free seqlock,
        // including on weakly ordered CPUs. Only the UI reader retries (bounded).
        for (int attempt = 0; attempt < 4; ++attempt) {
            result.clear();
            const auto before = snapshotSequence.load();
            if (before & 1) continue;
            const auto n = snapshotCount.load();
            for (size_t i = 0; i < n; ++i)
                result.push_back({snapshotId[i].load(), snapshotOutput[i].load(), snapshotPhase[i].load()});
            if (before == snapshotSequence.load()) {
                if (consistent) *consistent = true;
                return result;
            }
        }
        if (consistent) *consistent = false;
        result.clear();
        return result;
    }
private:
    struct Slot { Plan plan; std::atomic<int> owned{0}; };
    struct Runtime {
        uint32_t id = 0, seed = 1;
        int elapsed = 0;
        bool needsOutput = true;
        double phase = 0, hz = 1;
        float previous = 0, next = 0, configuredPhase = 0;
    };
    static float unitPhase(double phase) noexcept {
        // A double just below one can round to 1.0f; keep the public [0,1)
        // phase contract without resetting a random source one sample early.
        return std::min(float(phase), std::nextafter(1.0f, 0.0f));
    }
    static float random(uint32_t& seed) noexcept {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        return float(double(seed) / 4294967295.0 * 2 - 1);
    }
    void resetRuntime(size_t i, const Modulator& m) noexcept {
        auto& r = runtime[i]; r = {}; r.id = m.id; r.seed = m.id ^ 0x9e3779b9u;
        if (!r.seed) r.seed = 1;
        r.phase = r.configuredPhase = m.phase;
        r.hz = m.sync ? tempo / (60.0 * m.division) : m.rateHz;
        r.previous = random(r.seed); r.next = random(r.seed);
    }
    void install() noexcept {
        const int next = pending.exchange(-1, std::memory_order_acq_rel);
        if (next < 0) return;
        eventTickPending = true;
        const auto previousRuntime = runtime;
        const auto previousOutputs = outputs;
        const auto previousCount = activePlan().modulatorCount;
        const auto* previousPlan = &activePlan();
        const int old = active; active = next;
        runtime = {};
        outputs = {};
        const auto& p = activePlan();
        for (size_t i = 0; i < p.modulatorCount; ++i) {
            resetRuntime(i, p.modulators[i]);
            for (size_t previousIndex = 0; previousIndex < previousCount; ++previousIndex) {
                const auto& previous = previousRuntime[previousIndex];
                if (previous.id == p.modulators[i].id && previousPlan->modulators[previousIndex].type == p.modulators[i].type) {
                    runtime[i] = previous;
                    outputs[i] = previousOutputs[previousIndex];
                    runtime[i].needsOutput = previous.needsOutput || p.modulators[i].phase != previous.configuredPhase;
                    runtime[i].phase += p.modulators[i].phase - previous.configuredPhase;
                    runtime[i].phase -= std::floor(runtime[i].phase);
                    runtime[i].configuredPhase = p.modulators[i].phase;
                    break;
                }
            }
        }
        if (old >= 0) slots[size_t(old)].owned.store(0, std::memory_order_release);
    }
    const Plan& activePlan() const noexcept { return active < 0 ? empty : slots[size_t(active)].plan; }
    static_assert(std::atomic<int>::is_always_lock_free && std::atomic<float>::is_always_lock_free
                  && std::atomic<uint64_t>::is_always_lock_free);
    std::array<Slot, 4> slots{};
    Plan empty{};
    std::atomic<int> pending{-1};
    int active = -1, remaining = 0;
    bool eventTickPending = false;
    double rate = 48000, tempo = 120;
    std::array<Runtime, maxModulators> runtime{};
    std::array<float, maxModulators> outputs{};
    std::atomic<uint64_t> snapshotSequence{0};
    std::atomic<size_t> snapshotCount{0};
    std::array<std::atomic<uint32_t>, maxModulators> snapshotId{};
    std::array<std::atomic<float>, maxModulators> snapshotOutput{}, snapshotPhase{};
};

} // namespace vocalstack
