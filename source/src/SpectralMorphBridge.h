#pragma once
#include "SpectralMorph.h"
#include <atomic>
#include <memory>

namespace vocalstack {
struct SpectralMorphFrame {
    uint32_t nodeId = 0;
    SpectralMorphSnapshot spectrum{};
    uint64_t graphRevision = 0, resetEpoch = 0;
};
using SpectralMorphFrames = std::array<SpectralMorphFrame, 16>;
// Exclusive ownership, not a float seqlock. Bounded tries; the audio writer
// drops a publication rather than waiting for a reader or reclaiming memory.
class SpectralMorphBridge {
public:
    void publish(const SpectralMorphFrames& frames) noexcept {
        const auto token = latest.load(std::memory_order_relaxed);
        for (size_t i = 0; i < buffers->size(); ++i) {
            if ((token & 3) == i + 1) continue;
            auto& b = (*buffers)[i]; int free = 0;
            if (!b.owner.compare_exchange_strong(free, -1, std::memory_order_acquire)) continue;
            b.frames = frames;
            b.owner.store(0, std::memory_order_release);
            latest.store((++sequence << 2) | (i + 1), std::memory_order_release);
            return;
        }
    }
    bool read(size_t slot, SpectralMorphFrame& frame) const noexcept {
        if (slot >= 16) return false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto token = latest.load(std::memory_order_acquire);
            if (!token) return false;
            auto& b = (*buffers)[(token & 3) - 1]; int free = 0;
            if (!b.owner.compare_exchange_strong(free, 1, std::memory_order_acquire)) continue;
            const bool valid = latest.load(std::memory_order_acquire) == token;
            if (valid) frame = b.frames[slot];
            b.owner.store(0, std::memory_order_release);
            if (valid) return frame.nodeId != 0;
        }
        return false;
    }
private:
    struct Buffer { std::atomic<int> owner{0}; SpectralMorphFrames frames{}; };
    std::unique_ptr<std::array<Buffer, 3>> buffers = std::make_unique<std::array<Buffer, 3>>();
    std::atomic<uint64_t> latest{0}; uint64_t sequence = 0;
    static_assert(std::atomic<int>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free);
};
}
