#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace vocalstack {

// Linear amplitudes relative to digital full scale; peak is sample peak, not true peak.
struct MeterReading {
    float rms = 0, peak = 0, heldPeak = 0, maxPeak = 0;
    bool clipped = false;
};

struct Meters {
    std::array<MeterReading, 2> input{}, output{};
    float correctionDb = 0;
    int inputChannels = 1, outputChannels = 2;
};

// Single-channel, allocation-free detector. Call process() once per actual sample.
// prepare() changes the clock without discarding history or the remaining hold time.
class MeterDetector {
public:
    void prepare(double sampleRate) noexcept {
        sampleSeconds = 1.0 / std::max(1.0, sampleRate);
        powerStep = -std::expm1(-sampleSeconds / 0.300);
        peakDecay = std::exp(-sampleSeconds * 24.0 * log10Over20);
        holdDecay = std::exp(-sampleSeconds * 12.0 * log10Over20);
    }

    void reset() noexcept {
        power = peak = 0;
        resetPeaks();
    }

    // Display clear deliberately preserves the moving RMS and fast peak envelope.
    void resetPeaks() noexcept {
        heldPeak = maxPeak = holdRemaining = 0;
        clipped = false;
    }

    void process(float sample) noexcept {
        const double magnitude = std::isfinite(sample) ? std::abs(static_cast<double>(sample)) : 0.0;
        power += (magnitude * magnitude - power) * powerStep;
        peak = std::max(magnitude, peak * peakDecay);
        if (magnitude >= heldPeak) {
            heldPeak = magnitude;
            holdRemaining = magnitude > 0 ? 1.5 : 0;
        } else if (holdRemaining > 0) {
            holdRemaining = std::max(0.0, holdRemaining - sampleSeconds);
        } else {
            heldPeak = std::max(magnitude, heldPeak * holdDecay);
        }
        maxPeak = std::max(maxPeak, magnitude);
        clipped = clipped || magnitude >= 1.0;
        // Avoid denormal tails during prolonged silence, including in non-JUCE users.
        if (power < 1.0e-30) power = 0;
        if (peak < 1.0e-15) peak = 0;
        if (heldPeak < 1.0e-15) heldPeak = 0;
    }

    MeterReading reading() const noexcept {
        return {static_cast<float>(std::sqrt(power)), static_cast<float>(peak),
                static_cast<float>(heldPeak), static_cast<float>(maxPeak), clipped};
    }

private:
    static constexpr double log10Over20 = 0.1151292546497022842;
    double sampleSeconds = 1.0 / 48000.0;
    double powerStep = -std::expm1(-sampleSeconds / 0.300);
    double peakDecay = std::exp(-sampleSeconds * 24.0 * log10Over20);
    double holdDecay = std::exp(-sampleSeconds * 12.0 * log10Over20);
    double power = 0, peak = 0, heldPeak = 0, maxPeak = 0, holdRemaining = 0;
    bool clipped = false;
};

} // namespace vocalstack
