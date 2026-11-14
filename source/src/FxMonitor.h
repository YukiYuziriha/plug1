#pragma once

#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

namespace vocalstack {

struct FxMonitorFrame {
    static constexpr size_t sampleCount = 2048;
    static constexpr size_t historyCount = 200; // Four seconds at 20 ms/bin.
    // 20 ms stereo-union extrema and stereo power, before the slot Mix.
    struct HistoryBin {
        float inputMin = 0, inputMax = 0, wetMin = 0, wetMax = 0;
        float inputRms = 0, wetRms = 0;
        float gainReductionDb = 0; // Maximum actual pre-makeup attenuation in this bin.
    };
    std::array<HistoryBin, historyCount> history{};
    size_t historyUsed = 0;
    uint64_t historySamples = 0;
    size_t historyBinSamples = 960;
    std::array<std::array<float, sampleCount>, 2> input{}, output{};
    // Linear amplitude RMS: sqrt(mean((L*L + R*R)/2)), over this window.
    // Crossover ports only; bypass has one full-band port, unused entries zero.
    std::array<float, 3> bandRms{};
    float wetRms = 0; // Measured creative FX: processed wet signal before slot blend.
    bool hasWet = false;
    double sampleRate = 48000;
    uint32_t nodeId = 0;
    int bandCount = 0, nodeKind = -1;
    float gainReductionDb = 0;
    uint64_t sequence = 0, epoch = 0;
};

// One audio producer, concurrent bounded snapshot readers. All large storage is
// heap allocated at construction, never in process/capture/read. Retain/release
// only change requests; no GUI thread reads or destroys live DSP state.
// Requests are polled every 64 samples; publication targets 30 Hz after a full
// 2048-sample warmup (overlapping windows at low rates, never decimated). Reads
// fail immediately when the last retain is released; a new subscription cannot
// reuse an old generation. Plan/reset/rate changes also require a fresh window.
class FxMonitor {
public:
    static constexpr size_t slotCount = 16;
    void setGainReduction(size_t slot, float db) noexcept {
        if (slot < slotCount) reductions[slot].store(std::isfinite(db) ? std::clamp(db, 0.f, 160.f) : 0.f, std::memory_order_relaxed);
    }
    float gainReduction(size_t slot) const noexcept {
        return slot < slotCount ? reductions[slot].load(std::memory_order_relaxed) : 0.f;
    }
    FxMonitor() : slots(std::make_unique<std::array<Slot, slotCount>>()) {}
    FxMonitor(const FxMonitor&) = delete;
    FxMonitor& operator=(const FxMonitor&) = delete;

    void retain(size_t slot) noexcept {
        if (slot >= slotCount) return;
        auto& request = (*slots)[slot].request;
        auto old = request.load(std::memory_order_relaxed);
        for (;;) {
            const auto count = uint32_t(old);
            if (count == std::numeric_limits<uint32_t>::max()) return;
            const auto next = count == 0 ? ((old + (uint64_t{1} << 32)) | 1) : old + 1;
            if (request.compare_exchange_weak(old, next, std::memory_order_release,
                                              std::memory_order_relaxed)) return;
        }
    }
    void release(size_t slot) noexcept {
        if (slot >= slotCount) return;
        auto& request = (*slots)[slot].request;
        auto old = request.load(std::memory_order_relaxed);
        while (uint32_t(old) != 0)
            if (request.compare_exchange_weak(old, old - 1, std::memory_order_release,
                                              std::memory_order_relaxed)) return;
    }
    // On false, callers must clear their display; out is unspecified. Sequence
    // increases per slot across subscriptions/resets; nodeId uses routing IDs.
    bool read(size_t slot, FxMonitorFrame& out) const noexcept {
        if (slot >= slotCount) return false;
        auto& s = (*slots)[slot];
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto request = s.request.load(std::memory_order_acquire);
            if (uint32_t(request) == 0) return false;
            const auto token = s.latest.load(std::memory_order_acquire);
            if (token == 0) return false;
            auto& b = s.buffers[(token & 3) - 1];
            int free = 0;
            if (!b.owner.compare_exchange_strong(free, 1, std::memory_order_acquire,
                                                 std::memory_order_relaxed)) continue;
            // Ownership prevents ALL concurrent plain payload access by writers.
            bool valid = s.latest.load(std::memory_order_acquire) == token
                && b.generation == (request >> 32);
            if (valid) out = b.frame;
            valid = valid && s.latest.load(std::memory_order_acquire) == token
                && s.request.load(std::memory_order_acquire) == request;
            b.owner.store(0, std::memory_order_release);
            if (valid) return true;
        }
        return false;
    }

    // Audio/quiescent thread only, including setter/prepare/reset/plan changes.
    // Invalidate immediately, retaining UI subscriptions and monotonic sequences.
    void reset(double sampleRate) noexcept {
        rate = sampleRate;
        poll = 0;
        for (auto& x : reductions) x.store(0, std::memory_order_relaxed);
        interval = size_t(std::max(1.0, sampleRate / 30.0));
        historyInterval = size_t(std::max(1.0, std::round(sampleRate * .020)));
        for (auto& s : *slots) {
            s.latest.store(0, std::memory_order_release);
            ++s.epoch;
            s.generation = 0;
            s.enabled = false;
            clearHistory(s);
            s.used = s.position = s.elapsed = 0;
        }
    }
    void beginSample() noexcept {
        if (poll++ % 64 != 0) return;
        for (auto& s : *slots) {
            const auto request = s.request.load(std::memory_order_acquire);
            const bool enabled = uint32_t(request) != 0;
            const auto generation = request >> 32;
            if (enabled != s.enabled || generation != s.generation) {
                s.latest.store(0, std::memory_order_release);
                ++s.epoch;
                s.used = s.position = s.elapsed = 0;
                clearHistory(s);
                s.generation = generation;
                s.enabled = enabled;
            }
        }
    }
    bool capturing(size_t slot) const noexcept {
        return slot < slotCount && (*slots)[slot].enabled;
    }
    void capture(size_t slot, uint32_t nodeId, float inL, float inR,
                 float outL, float outR, int bandCount,
                 const std::array<std::array<float, 2>, 3>& bands,
                                  bool hasWet = false, float wetL = 0, float wetR = 0,
                                  int nodeKind = -1) noexcept {
        if (slot >= slotCount) return;
        auto& s = (*slots)[slot];
        if (!s.enabled) return;
        if (s.staging.nodeId != nodeId || s.staging.bandCount != bandCount || s.staging.hasWet != hasWet
                    || s.staging.nodeKind != nodeKind) {
            s.latest.store(0, std::memory_order_release);
            ++s.epoch;
            s.used = s.position = s.elapsed = 0;
            clearHistory(s);
            s.staging.nodeId = nodeId;
            s.staging.bandCount = bandCount;
        }
        s.staging.hasWet = hasWet;
        s.staging.nodeKind = nodeKind;
        s.staging.gainReductionDb = gainReduction(slot);
        if (hasWet) {
            auto& bin = s.partial;
            bin.gainReductionDb = std::max(bin.gainReductionDb, s.staging.gainReductionDb);
            bin.inputMin = std::min({bin.inputMin, inL, inR});
            bin.inputMax = std::max({bin.inputMax, inL, inR});
            bin.wetMin = std::min({bin.wetMin, wetL, wetR});
            bin.wetMax = std::max({bin.wetMax, wetL, wetR});
            s.inputPower += (double(inL) * inL + double(inR) * inR) * .5;
            s.wetPower += (double(wetL) * wetL + double(wetR) * wetR) * .5;
            if (++s.partialSamples == historyInterval) {
                bin.inputRms = float(std::sqrt(s.inputPower / historyInterval));
                bin.wetRms = float(std::sqrt(s.wetPower / historyInterval));
                s.staging.history[s.historyPosition] = bin;
                s.historyPosition = (s.historyPosition + 1) % FxMonitorFrame::historyCount;
                s.staging.historyUsed = std::min(s.staging.historyUsed + 1, FxMonitorFrame::historyCount);
                s.staging.historySamples += historyInterval;
                s.partial = {}; s.partialSamples = 0; s.inputPower = s.wetPower = 0;
            }
        }
        const auto p = s.position;
        s.staging.input[0][p] = inL; s.staging.input[1][p] = inR;
        s.staging.output[0][p] = outL; s.staging.output[1][p] = outR;
        for (size_t b = 0; b < 3; ++b)
            s.powers[b][p] = int(b) < bandCount
                ? (double(bands[b][0]) * bands[b][0] + double(bands[b][1]) * bands[b][1]) * 0.5
                                : (b == 0 && hasWet ? (double(wetL) * wetL + double(wetR) * wetR) * .5 : 0);
        s.position = (p + 1) % FxMonitorFrame::sampleCount;
        s.used = std::min(s.used + 1, FxMonitorFrame::sampleCount);
        ++s.elapsed;
        if (s.used != FxMonitorFrame::sampleCount || s.elapsed < interval) return;
        s.elapsed = 0;
        publish(s);
    }

private:
    std::array<std::atomic<float>, slotCount> reductions{};
    struct Buffer {
        // 0=unowned, -1=writer, 1=reader. Nobody waits for an owner.
        std::atomic<int> owner{0};
        FxMonitorFrame frame;
        uint64_t generation = 0;
    };
    struct Slot {
        // High word is the subscription generation; low word is retain count.
        std::atomic<uint64_t> request{0}, latest{0};
        std::array<Buffer, 3> buffers;
        FxMonitorFrame staging;
        std::array<std::array<double, FxMonitorFrame::sampleCount>, 3> powers{};
        uint64_t generation = 0, sequence = 0, epoch = 0;
        size_t used = 0, position = 0, elapsed = 0;
        bool enabled = false;
        FxMonitorFrame::HistoryBin partial{};
        size_t partialSamples = 0, historyPosition = 0;
        double inputPower = 0, wetPower = 0;
    };
    static void clearHistory(Slot& s) noexcept {
        s.staging.history = {}; s.staging.historyUsed = 0; s.staging.historySamples = 0;
        s.partial = {}; s.partialSamples = s.historyPosition = 0;
        s.inputPower = s.wetPower = 0;
    }
    void publish(Slot& s) noexcept {
        const auto latest = s.latest.load(std::memory_order_relaxed);
        for (size_t i = 0; i < s.buffers.size(); ++i) {
            if ((latest & 3) == i + 1) continue;
            auto& b = s.buffers[i];
            int free = 0;
            if (!b.owner.compare_exchange_strong(free, -1, std::memory_order_acquire,
                                                 std::memory_order_relaxed)) continue;
            auto& frame = b.frame;
            // Ring windows are full-rate, oldest first. FFT belongs to the UI.
            const auto copy = [&](const auto& from, auto& to) {
                std::copy(from.begin() + s.position, from.end(), to.begin());
                std::copy(from.begin(), from.begin() + s.position,
                          to.begin() + (FxMonitorFrame::sampleCount - s.position));
            };
            for (size_t ch = 0; ch < 2; ++ch) {
                copy(s.staging.input[ch], frame.input[ch]);
                copy(s.staging.output[ch], frame.output[ch]);
            }
            for (size_t band = 0; band < 3; ++band) {
                double power = 0;
                for (auto x : s.powers[band]) power += x;
                frame.bandRms[band] = float(std::sqrt(power / FxMonitorFrame::sampleCount));
            }
            // Same exclusive payload ownership as the full-rate windows: no
            // seqlock races, and the producer skips busy buffers rather than waits.
            frame.history = {};
            const auto used = s.staging.historyUsed;
            for (size_t j = 0; j < used; ++j)
                frame.history[FxMonitorFrame::historyCount - used + j] =
                    s.staging.history[(s.historyPosition + FxMonitorFrame::historyCount - used + j) % FxMonitorFrame::historyCount];
            frame.historyUsed = used;
            frame.historySamples = s.staging.historySamples;
            frame.historyBinSamples = historyInterval;
            frame.hasWet = s.staging.hasWet;
            frame.wetRms = frame.hasWet ? frame.bandRms[0] : 0;
            if (frame.hasWet) frame.bandRms[0] = 0;
            frame.nodeId = s.staging.nodeId;
            frame.bandCount = s.staging.bandCount;
            frame.nodeKind = s.staging.nodeKind;
            frame.gainReductionDb = s.staging.gainReductionDb;
            frame.sampleRate = rate;
            frame.sequence = ++s.sequence;
            frame.epoch = s.epoch;
            b.generation = s.generation;
            b.owner.store(0, std::memory_order_release);
            s.latest.store((frame.sequence << 2) | (i + 1), std::memory_order_release);
            return;
        }
    }
    static_assert(std::atomic<uint64_t>::is_always_lock_free
                  && std::atomic<int>::is_always_lock_free && std::atomic<float>::is_always_lock_free,
                  "FX telemetry requires lock-free snapshot ownership");
    std::unique_ptr<std::array<Slot, slotCount>> slots;
    double rate = 48000;
    size_t interval = 1600, poll = 0, historyInterval = 960;
};

} // namespace vocalstack
