#pragma once

#include <array>
#include <cstddef>
#include <memory>

namespace vocalstack {

enum class SpatialEqShape { Bell, LowShelf, HighShelf };

struct SpatialEqBandSettings {
    bool active = false;
    bool enabled = true;
    SpatialEqShape shape = SpatialEqShape::Bell;
    float frequency = 1000.0f; // Hz
    float gainDb = 0.0f;
    float q = 1.0f;
    float depth = 0.0f; // [0, 1]; Strength is fixed at unity
};

struct SpatialEqSettings {
    static constexpr std::size_t bandCount = 8;
    std::array<SpatialEqBandSettings, bandCount> bands{}; // index 0 is slot 1
};

// Stereo matched-pole EQ with a parallel spatial return. Use from one audio
// thread; publish settings snapshots externally. No allocations after prepare.
class SpatialEq {
public:
    static constexpr int latencySamples = 0;
    static constexpr int coefficientInterval = 32;

    SpatialEq() noexcept;
    ~SpatialEq();
    SpatialEq(const SpatialEq&) = delete;
    SpatialEq& operator=(const SpatialEq&) = delete;

    // Off the audio thread. Rates outside [1 kHz, 768 kHz] fall back to 48 kHz.
    // The first sample after prepare starts with the latest settings settled.
    void prepare(double sampleRate);
    // Clear histories, settle the current targets and restart the 32-sample clock.
    void reset() noexcept;
    // Set the snapshot at the desired sample boundary (split blocks as needed).
    // Signed gains automate through zero without a band reset. Shape changes
    // settle EQ/Depth and clear that slot's histories at the next control tick.
    // Frequency: 20 Hz to min(20 kHz, 0.475 * rate). Gain: [-30, 30] dB for
    // all shapes. Q: [0.5, 30] for positive Bells, [0.1, 30] otherwise.
    void setSettings(const SpatialEqSettings& settings) noexcept;

    // In-place stereo; distinct lanes required. For mono-to-stereo, pass dual
    // mono in separate lanes. Before prepare, these functions leave audio alone.
    void process(float& left, float& right) noexcept;
    void process(float* left, float* right, std::size_t frames) noexcept;

private:
    struct State;
    std::unique_ptr<State> state;
    SpatialEqSettings targets{};
};

} // namespace vocalstack
