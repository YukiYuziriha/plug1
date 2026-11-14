#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vocalstack {
// Value copy on the AUDIO thread only; publish through the existing monitor bridge.
struct UtilityDynamicsSnapshot {
    float thresholdDb = -40, gainReductionDb = 0;
};
struct UtilityHistorySnapshot {
    static constexpr size_t capacity = 200;
    struct Bin {
        float inputDb = -72, outputDb = -72, thresholdDb = -40, gainReductionDb = 0;
    };
    std::array<Bin, capacity> bins{}; // oldest first, valid bins right-aligned
    size_t used = 0;
    uint64_t samples = 0;
    double binSeconds = .020;
};
// Audio-thread owned. No allocation, locks, GUI access or unbounded work.
// Push slot input and FINAL post-Mix output, not the wet signal.
class UtilityHistoryAccumulator {
public:
    void reset(double sampleRate) noexcept {
        rate = std::isfinite(sampleRate) && sampleRate >= 8000 && sampleRate <= 384000 ? sampleRate : 48000;
        interval = size_t(std::max(1., std::round(rate * .020)));
        history = {}; position = partial = 0; inputPower = outputPower = reduction = threshold = 0;
    }
    void push(float inL, float inR, float outL, float outR, UtilityDynamicsSnapshot dynamics) noexcept {
        const auto safe = [](float v) { return std::isfinite(v) ? double(v) : 0.; };
        const double l = safe(inL), r = safe(inR), ol = safe(outL), oright = safe(outR);
        inputPower += (l*l + r*r)*.5; outputPower += (ol*ol + oright*oright)*.5;
        reduction += std::isfinite(dynamics.gainReductionDb) ? std::max(0.f, dynamics.gainReductionDb) : 0;
        threshold += std::isfinite(dynamics.thresholdDb) ? dynamics.thresholdDb : -40;
        if (++partial != interval) return;
        history.bins[position] = {powerDb(inputPower / double(interval)), powerDb(outputPower / double(interval)),
            float(threshold / double(interval)), float(reduction / double(interval))};
        position = (position + 1) % UtilityHistorySnapshot::capacity;
        history.used = std::min(history.used + 1, UtilityHistorySnapshot::capacity);
        history.samples += interval; partial = 0; inputPower = outputPower = reduction = threshold = 0;
    }
    UtilityHistorySnapshot snapshot() const noexcept {
        UtilityHistorySnapshot result;
        result.used = history.used; result.samples = history.samples; result.binSeconds = double(interval) / rate;
        for (size_t i = 0; i < result.used; ++i)
            result.bins[result.capacity - result.used + i] = history.bins[(position + result.capacity - result.used + i) % result.capacity];
        return result;
    }
    static float powerDb(double power) noexcept {
        return float(10 * std::log10(std::max(1.e-30, power)));
    }
private:
    UtilityHistorySnapshot history;
    size_t interval = 960, position = 0, partial = 0;
    double rate = 48000, inputPower = 0, outputPower = 0, reduction = 0, threshold = 0;
};
struct UtilityStereoActivity {
    std::array<float, 2> rms{};
    float correlation = 0;
    bool correlationValid = false;
    template <class Channels> static UtilityStereoActivity measure(const Channels& channels) noexcept {
        UtilityStereoActivity result;
        double left = 0, right = 0, cross = 0;
        for (size_t i = 0; i < channels[0].size(); ++i) {
            const double l = std::isfinite(channels[0][i]) ? channels[0][i] : 0;
            const double r = std::isfinite(channels[1][i]) ? channels[1][i] : 0;
            left += l*l; right += r*r; cross += l*r;
        }
        if (!channels[0].empty()) result.rms = {{float(std::sqrt(left / channels[0].size())), float(std::sqrt(right / channels[0].size()))}};
        result.correlationValid = left > 1.e-20 && right > 1.e-20;
        if (result.correlationValid) result.correlation = float(std::clamp(cross / std::sqrt(left * right), -1., 1.));
        return result;
    }
};
namespace utility_visual_detail {
// Compiles before the coordinator adds the optional coherent frame payload.
template <class Frame> const UtilityHistorySnapshot* history(const Frame&, long) noexcept { return nullptr; }
template <class Frame> auto history(const Frame& frame, int) noexcept -> decltype(static_cast<const UtilityHistorySnapshot*>(&frame.utilityHistory)) {
    return &frame.utilityHistory;
}
template <class Frame> const UtilityHistorySnapshot* history(const Frame& frame) noexcept { return history(frame, 0); }
}
} // namespace vocalstack
