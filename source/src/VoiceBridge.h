#pragma once

#include "VocalEngine.h"
#include <atomic>

namespace vocalstack {

// Best-effort display values, like MeterBridge: race-free individual fields,
// not a promise that every field belongs to the same audio block.
class VoiceBridge {
public:
    void publish(const std::array<VoiceSnapshot, 8>& snapshots) noexcept {
        for (size_t i = 0; i < voices.size(); ++i) {
            voices[i].publish(snapshots[i]);
        }
    }
    std::array<VoiceSnapshot, 8> snapshot() const noexcept {
        std::array<VoiceSnapshot, 8> result{};
        for (size_t i = 0; i < voices.size(); ++i) {
            result[i] = voices[i].snapshot();
        }
        return result;
    }
    void publishLead(const VoiceSnapshot& snapshot) noexcept { lead.publish(snapshot); }
    VoiceSnapshot leadSnapshot() const noexcept { return lead.snapshot(); }
private:
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free,
                  "Voice publication must be lock-free");
    struct Voice {
        std::atomic<unsigned> flags{0};
        std::array<std::atomic<float>, 8> fields{};
        void publish(const VoiceSnapshot& s) noexcept {
            flags.store((s.present ? 1 : 0) | (s.enabled ? 2 : 0), std::memory_order_relaxed);
            const std::array<float, 8> values{s.pan, s.pitchSemitones, s.detuneCents, s.delayMs,
                s.formantSemitones, s.gainDb, s.activity, 0};
            for (size_t i = 0; i < values.size(); ++i)
                fields[i].store(values[i], std::memory_order_relaxed);
        }
        VoiceSnapshot snapshot() const noexcept {
            const auto state = flags.load(std::memory_order_relaxed);
            std::array<float, 8> values{};
            for (size_t i = 0; i < values.size(); ++i)
                values[i] = fields[i].load(std::memory_order_relaxed);
            return {(state & 1) != 0, (state & 2) != 0, values[0], values[1], values[2],
                values[3], values[4], values[5], values[6]};
        }
    };
    std::array<Voice, 8> voices{};
    Voice lead;
};

} // namespace vocalstack
