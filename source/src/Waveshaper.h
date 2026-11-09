#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace vocalstack {

struct WaveshaperSettings {
    bool enabled = true;
    float driveDb = 0, outputDb = 0, mix = 1;
    int shape = 0;
};

// Independent curves: soft tanh, hard clip, and biased tanh (zero at zero,
// unity small-signal slope). Drive is real pre-gain, not normalized away.
// First-order antiderivative antialiasing (ADAA), NOT oversampling or alias-free
// processing. It averages the curve along successive driven samples: it has
// frequency-dependent phase/attenuation, approximately half a sample at low
// levels, but no explicit delay line or integer sample latency. Dry stays direct.
// Controls use 10 ms exponential smoothing, including curve crossfades and bypass;
// first process after prepare/reset adopts settings immediately. Drive [-24,48] dB,
// output [-60,24] dB, mix [0,1]; invalid values use defaults. Bypass settles to exact
// dry while keeping driven-sample history warm. No allocations, locks, or I/O.
class Waveshaper {
public:
    static constexpr int latencySamples = 0;
    static constexpr std::array<const char*, 3> shapeNames{"Soft", "Hard", "Asymmetric"};

    void prepare(double sampleRate) noexcept {
        const double rate = std::isfinite(sampleRate) && sampleRate >= 8000 && sampleRate <= 768000
                    ? sampleRate : 48000;
        smoothing = -std::expm1(-1.0 / (rate * 0.010));
        reset();
    }

    void reset() noexcept {
        previous = {};
        weights = {1, 0, 0};
        drive = output = 0;
        blend = 0;
        initialized = false;
    }

    void process(float& left, float& right, const WaveshaperSettings& settings) noexcept {
        const double driveTarget = clean(settings.driveDb, 0, -24, 48);
        const double outputTarget = clean(settings.outputDb, 0, -60, 24);
        const double blendTarget = settings.enabled ? clean(settings.mix, 1, 0, 1) : 0;
        const int shape = std::clamp(settings.shape, 0, 2);
        if (!initialized) {
            drive = driveTarget;
            output = outputTarget;
            blend = blendTarget;
            weights = {};
            weights[static_cast<size_t>(shape)] = 1;
            initialized = true;
        }
        approach(drive, driveTarget);
        approach(output, outputTarget);
        approach(blend, blendTarget);
        for (size_t i = 0; i < weights.size(); ++i)
            approach(weights[i], i == static_cast<size_t>(shape) ? 1 : 0);
        const double pre = std::pow(10.0, drive / 20.0);
        const double post = std::pow(10.0, output / 20.0);
        const std::array<float*, 2> channels{&left, &right};
        for (size_t channel = 0; channel < channels.size(); ++channel) {
            const double dry = std::isfinite(*channels[channel]) ? *channels[channel] : 0;
            const double x = dry * pre;
            if (blend == 0) {
                previous[channel] = x;
                *channels[channel] = static_cast<float>(dry);
                continue;
            }
            const double old = previous[channel];
            double wet = 0;
            for (int curve = 0; curve < 3; ++curve) {
                if (weights[static_cast<size_t>(curve)] == 0) continue;
                const double delta = x - old;
                const double value = std::abs(delta) < 1.0e-5 * (1 + std::abs(x) + std::abs(old))
                    ? transfer((x + old) * 0.5, curve)
                    : (primitive(x, curve) - primitive(old, curve)) / delta;
                wet += weights[static_cast<size_t>(curve)] * value;
            }
            previous[channel] = x;
            *channels[channel] = static_cast<float>(blend == 0 ? dry
                : dry * (1 - blend) + wet * post * blend);
        }
    }

private:
    static double clean(double x, double fallback, double low, double high) noexcept {
        return std::isfinite(x) ? std::clamp(x, low, high) : fallback;
    }
    void approach(double& value, double target) const noexcept {
        value += smoothing * (target - value);
        if (std::abs(value - target) < 1.0e-9) value = target;
    }
    static double logCosh(double x) noexcept {
        const double a = std::abs(x);
        return a + std::log1p(std::exp(-2 * a)) - 0.6931471805599453094;
    }
    static double transfer(double x, int curve) noexcept {
        if (curve == 1) return std::clamp(x, -1.0, 1.0);
        if (curve == 2) {
            const double bias = std::tanh(0.5);
            return (std::tanh(x + 0.5) - bias) / (1 - bias * bias);
        }
        return std::tanh(x);
    }
    static double primitive(double x, int curve) noexcept {
        if (curve == 1) {
            const double a = std::abs(x);
            return a <= 1 ? 0.5 * x * x : a - 0.5;
        }
        if (curve == 2) {
            const double bias = std::tanh(0.5);
            return (logCosh(x + 0.5) - bias * x) / (1 - bias * bias);
        }
        return logCosh(x);
    }

    double smoothing = -std::expm1(-1.0 / 480.0);
    double drive = 0, output = 0, blend = 0;
    std::array<double, 3> weights{1, 0, 0};
    std::array<double, 2> previous{};
    bool initialized = false;
};

} // namespace vocalstack
