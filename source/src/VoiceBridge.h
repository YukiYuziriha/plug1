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
            const auto& s = snapshots[i];
            auto& v = voices[i];
            v.flags.store((s.present ? 1 : 0) | (s.enabled ? 2 : 0), std::memory_order_relaxed);
            const std::array<float, 8> fields{s.pan, s.pitchSemitones, s.detuneCents, s.delayMs,
                s.formantSemitones, s.gainDb, s.activity, 0};
            for (size_t f = 0; f < fields.size(); ++f)
                v.fields[f].store(fields[f], std::memory_order_relaxed);
        }
    }
    std::array<VoiceSnapshot, 8> snapshot() const noexcept {
        std::array<VoiceSnapshot, 8> result{};
        for (size_t i = 0; i < voices.size(); ++i) {
            const auto& v = voices[i];
            const auto flags = v.flags.load(std::memory_order_relaxed);
            std::array<float, 8> f{};
            for (size_t j = 0; j < f.size(); ++j)
                f[j] = v.fields[j].load(std::memory_order_relaxed);
            result[i] = {(flags & 1) != 0, (flags & 2) != 0, f[0], f[1], f[2], f[3], f[4], f[5], f[6]};
        }
        return result;
    }
private:
    static_assert(std::atomic<float>::is_always_lock_free && std::atomic<unsigned>::is_always_lock_free,
                  "Voice publication must be lock-free");
    struct Voice {
        std::atomic<unsigned> flags{0};
        std::array<std::atomic<float>, 8> fields{};
    };
    std::array<Voice, 8> voices{};
};

} // namespace vocalstack
