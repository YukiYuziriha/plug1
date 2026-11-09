#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vocalstack {

// Three coordinated modulated delay taps per channel. No feedback, allocations
// only in prepare(), and no added dry-path latency. Not a model of proprietary DSP.
class Chorale {
public:
    void prepare(double sampleRate) {
        rate = sampleRate;
        for (auto& channel : delay)
            channel.assign(static_cast<size_t>(std::ceil(rate * 0.040)) + 4, 0.0f);
        smoothing = -std::expm1(-1.0 / (rate * 0.025));
        reset();
    }

    void reset() {
        for (auto& channel : delay)
            std::fill(channel.begin(), channel.end(), 0.0f);
        next = 0;
        phase = 0;
        blend = 0;
        motion = 0.35f;
        lowpass = {};
    }

    void process(float& left, float& right, bool enabled, float mix, float movement) {
        const float target = enabled && std::isfinite(mix) ? std::clamp(mix, 0.0f, 1.0f) : 0.0f;
        blend += smoothing * (target - blend);
        if (std::abs(blend - target) < 1.0e-6f)
            blend = target;
        const float motionTarget = std::isfinite(movement) ? std::clamp(movement, 0.0f, 1.0f) : 0.35f;
        motion += smoothing * (motionTarget - motion);
        const std::array<float, 2> input{left, right};
        std::array<float, 2> wet{};
        constexpr double tau = 6.2831853071795864769;
        const double speed = 0.12 + 1.08 * motion * motion;
        phase += speed / rate;
        if (phase >= 1.0)
            phase -= 1.0;
        const float toneStep = static_cast<float>(-std::expm1(-tau * 7000.0 / rate));
        for (size_t channel = 0; channel < delay.size(); ++channel) {
            // Gentle wet-only bandwidth restriction softens interpolation images.
            lowpass[channel] += toneStep * (input[channel] - lowpass[channel]);
            delay[channel][next] = lowpass[channel];
            for (int tap = 0; tap < 3; ++tap) {
                const double angle = tau * (phase + tap / 3.0 + channel * 0.18);
                const double swing = std::sin(angle) + 0.15 * std::sin(angle * 2.0);
                const float milliseconds = static_cast<float>(14.0 + tap * 3.2
                    + (0.4 + 3.6 * motion) * swing);
                wet[channel] += read(channel, milliseconds * static_cast<float>(rate * 0.001));
            }
            // RMS-oriented three-tap scaling, with conservative extra headroom.
            wet[channel] *= 0.5f;
        }
        if (++next == delay[0].size())
            next = 0;
        if (blend == 0.0f)
            return; // Exact transparency while disabled; history stays warm.
        const float amount = static_cast<float>(blend);
        left = input[0] * (1.0f - amount) + wet[0] * amount;
        right = input[1] * (1.0f - amount) + wet[1] * amount;
    }

private:
    float read(size_t channel, float samples) const {
        const auto size = static_cast<int>(delay[channel].size());
        const int whole = static_cast<int>(samples);
        int first = static_cast<int>(next) - whole;
        if (first < 0) first += size;
        const int second = first == 0 ? size - 1 : first - 1;
        const float fraction = samples - static_cast<float>(whole);
        return delay[channel][static_cast<size_t>(first)] * (1.0f - fraction)
            + delay[channel][static_cast<size_t>(second)] * fraction;
    }

    double rate = 48000, phase = 0;
    double smoothing = 0, blend = 0, motion = 0.35;
    std::array<float, 2> lowpass{};
    size_t next = 0;
    std::array<std::vector<float>, 2> delay;
};

} // namespace vocalstack
