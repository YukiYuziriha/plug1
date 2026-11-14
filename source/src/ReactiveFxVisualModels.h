#pragma once

#include "FxMonitor.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace vocalstack::reactiveVisual {

// Presentation-only mapping of all 128 measured windows. No new measurements,
// envelope follower, clock, normalization or DSP access.
struct WaveformField {
    static constexpr size_t columns = FxMonitorFrame::historyCount;
    struct Band { float negative = 0, positive = 0, rms = 0; };
    struct Column { Band input, wet; };
    std::array<Column, columns> history{};

    static float amplitude(float value) noexcept {
        if (!std::isfinite(value) || std::abs(value) <= .001f) return 0;
        return std::clamp((60.f + 20.f * std::log10(std::abs(value))) / 60.f, 0.f, 1.f);
    }
    static WaveformField from(const FxMonitorFrame& frame, bool active) noexcept {
        WaveformField result;
        if (!active || !frame.hasWet) return result;
        const size_t used = std::min(frame.historyUsed, FxMonitorFrame::historyCount);
        for (size_t x = 0; x < columns; ++x) {
            const size_t begin = x * FxMonitorFrame::historyCount / columns;
            const size_t end = (x + 1) * FxMonitorFrame::historyCount / columns;
            float inLo = 0, inHi = 0, wetLo = 0, wetHi = 0;
            double inPower = 0, wetPower = 0;
            for (size_t i = begin; i < end; ++i) {
                if (i < FxMonitorFrame::historyCount - used) continue;
                const auto& bin = frame.history[i]; // Published oldest-left, newest-right.
                const auto finite = [](float v) { return std::isfinite(v) ? v : 0.f; };
                inLo = std::min(inLo, finite(bin.inputMin)); inHi = std::max(inHi, finite(bin.inputMax));
                wetLo = std::min(wetLo, finite(bin.wetMin)); wetHi = std::max(wetHi, finite(bin.wetMax));
                const double in = finite(bin.inputRms), wet = finite(bin.wetRms);
                inPower += in * in; wetPower += wet * wet;
            }
            result.history[x] = {{amplitude(inLo), amplitude(inHi), amplitude(float(std::sqrt(inPower / double(end - begin))))},
                                 {amplitude(wetLo), amplitude(wetHi), amplitude(float(std::sqrt(wetPower / double(end - begin))))}};
        }
        return result;
    }
};

// Snapshot-only attenuation. -1 marks missing/invalid measurements; zero is
// genuine measured unity gain. No PCM inference or GUI-clock accumulation.
struct GainReductionField {
    static constexpr size_t columns = 200;
    static constexpr float seconds = 4.f, maximumDb = 36.f;
    std::array<float, columns> db{};
    uint64_t sequence = 0, epoch = 0;
    uint32_t nodeId = 0;
    float currentDb = 0;
    bool available = false;
    GainReductionField() noexcept { db.fill(-1.f); }

    template<class Frame, class = void> struct HasHistory : std::false_type {};
    template<class Frame> struct HasHistory<Frame, std::void_t<
        decltype(std::declval<Frame>().gainReductionHistory),
        decltype(std::declval<Frame>().gainReductionHistoryUsed),
        decltype(std::declval<Frame>().gainReductionHistoryBinSamples),
        decltype(std::declval<Frame>().gainReductionDb),
        decltype(std::declval<Frame>().hasGainReduction)>> : std::true_type {};

    template<class Frame>
    static GainReductionField from(const Frame& frame, bool active) noexcept {
        GainReductionField result;
        if constexpr (HasHistory<Frame>::value) {
            static_assert(std::tuple_size<decltype(frame.gainReductionHistory)>::value == columns,
                          "GR history requires 200 measured 20 ms bins");
            if (!active || !frame.hasGainReduction || !std::isfinite(frame.sampleRate)
                || frame.sampleRate <= 0 || frame.gainReductionHistoryBinSamples == 0) return result;
            const double duration = double(frame.gainReductionHistoryBinSamples) / frame.sampleRate;
            if (std::abs(duration - .020) > 1.0 / frame.sampleRate) return result;
            result.available = true;
            result.sequence = frame.sequence; result.epoch = frame.epoch; result.nodeId = frame.nodeId;
            result.currentDb = std::isfinite(frame.gainReductionDb) ? std::max(0.f, frame.gainReductionDb) : 0.f;
            const auto used = std::min(size_t(frame.gainReductionHistoryUsed), columns);
            for (size_t i = columns - used; i < columns; ++i) {
                const float value = frame.gainReductionHistory[i];
                if (std::isfinite(value) && value >= 0) result.db[i] = value;
            }
        }
        return result;
    }
};

// Optional generic seam for a future three-spectrum visual. Caller supplies actual
// measured magnitudes already mapped to display columns; this helper does no FFT.
struct SpectrumField {
    static constexpr size_t columns = WaveformField::columns;
    std::array<std::array<float, columns>, 3> levels{};
    void set(size_t spectrum, size_t column, float linearMagnitude) noexcept {
        if (spectrum < levels.size() && column < columns)
            levels[spectrum][column] = WaveformField::amplitude(linearMagnitude);
    }
};

} // namespace vocalstack::reactiveVisual
