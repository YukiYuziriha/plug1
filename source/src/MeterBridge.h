#pragma once

#include "Metering.h"
#include <atomic>
#include <cstddef>

namespace vocalstack {

// Meter fields need race-free, best-effort display snapshots, not audio-frame coherence.
// Only the audio thread publishes; the UI never reads mutable detector state.
class MeterBridge {
public:
    void publish(const Meters& meters) noexcept {
        for (std::size_t channel = 0; channel < input.size(); ++channel) {
            input[channel].store(meters.input[channel]);
            output[channel].store(meters.output[channel]);
        }
        correctionDb.store(meters.correctionDb, std::memory_order_relaxed);
        inputChannels.store(meters.inputChannels, std::memory_order_relaxed);
        outputChannels.store(meters.outputChannels, std::memory_order_relaxed);
    }

    Meters snapshot() const noexcept {
        Meters meters;
        for (std::size_t channel = 0; channel < input.size(); ++channel) {
            meters.input[channel] = input[channel].load();
            meters.output[channel] = output[channel].load();
        }
        meters.correctionDb = correctionDb.load(std::memory_order_relaxed);
        meters.inputChannels = inputChannels.load(std::memory_order_relaxed);
        meters.outputChannels = outputChannels.load(std::memory_order_relaxed);
        return meters;
    }

private:
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free
                  && std::atomic<bool>::is_always_lock_free, "Meter publication must be lock-free");

    struct Channel {
        void store(const MeterReading& reading) noexcept {
            rms.store(reading.rms, std::memory_order_relaxed);
            peak.store(reading.peak, std::memory_order_relaxed);
            heldPeak.store(reading.heldPeak, std::memory_order_relaxed);
            maxPeak.store(reading.maxPeak, std::memory_order_relaxed);
            clipped.store(reading.clipped, std::memory_order_relaxed);
        }
        MeterReading load() const noexcept {
            return {rms.load(std::memory_order_relaxed), peak.load(std::memory_order_relaxed),
                    heldPeak.load(std::memory_order_relaxed), maxPeak.load(std::memory_order_relaxed),
                    clipped.load(std::memory_order_relaxed)};
        }
        std::atomic<float> rms{0}, peak{0}, heldPeak{0}, maxPeak{0};
        std::atomic<bool> clipped{false};
    };

    std::array<Channel, 2> input{}, output{};
    std::atomic<float> correctionDb{0};
    std::atomic<int> inputChannels{1}, outputChannels{2};
};

} // namespace vocalstack
