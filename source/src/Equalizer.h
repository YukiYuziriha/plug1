#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace vocalstack {

struct EqBandSettings {
    bool enabled = false;
    float frequency = 1000, gainDb = 0, q = 0.707f;
    int mode = 0;
};

struct EqualizerSettings {
    bool enabled = true;
    std::array<EqBandSettings, 8> bands{};
    float shiftSemitones = 0, gainDb = 0;
    bool adaptiveQ = false;
};

// Independent RBJ-style bell/shelf/notch and bilinear cut filters, not Bitwig
// coefficients. Shelf24 = two Shelf12 stages with half the requested dB each.
// Higher cuts use Butterworth stage Qs scaled by q/0.707; 6 dB cuts ignore Q.
// Adaptive-Q narrows bells by q * 10^(abs(band gain)/80), bounded to [0.1,20].
// Shift is explicitly semitones, global Gain is output trim. No explicit delay;
// IIR phase is frequency-dependent. Settings/response reads need external thread
// ownership: the instance response is NOT an audio/UI synchronization bridge.
// Ranges: shifted frequency [10,0.45*sampleRate] Hz, band gain [-24,24] dB,
// Q [0.1,20], shift [-48,48] semitones, output [-60,24] dB. Controls smooth in
// log-frequency/log-Q/dB with a 25 ms time constant. At most once per 64 samples,
// changed controls rebuild fixed-size banks and crossfade for 64 samples; mode
// switches start the new bank with cleared history. Trim/bypass smooth per sample.
// First process after prepare/reset adopts settings immediately. Settled bypass
// is exact dry, with filters kept warm. No allocations, locks, or I/O.
class Equalizer {
public:
    static constexpr int latencySamples = 0;
    static constexpr int coefficientInterval = 64;
    static constexpr std::array<const char*, 14> modeNames{
        "Bell", "LowShelf12", "HighShelf12", "LowShelf24", "HighShelf24",
        "LowCut6", "LowCut12", "LowCut24", "LowCut48",
        "HighCut6", "HighCut12", "HighCut24", "HighCut48", "Notch"};

    void prepare(double sampleRate) noexcept {
        rate = validRate(sampleRate);
        smoothing = -std::expm1(-1.0 / (rate * 0.025));
        controlStep = -std::expm1(-coefficientInterval / (rate * 0.025));
        reset();
    }
    void reset() noexcept {
        bands = {};
        position = 0;
        gain = 0;
        blend = 1;
        initialized = false;
    }

    void process(float& left, float& right, const EqualizerSettings& settings) noexcept {
        const double gainTarget = clean(settings.gainDb, 0, -60, 24);
        const double blendTarget = settings.enabled ? 1 : 0;
        if (!initialized) {
            gain = gainTarget;
            blend = blendTarget;
        }
        if (position == 0) update(settings);
        initialized = true;
        approach(gain, gainTarget, smoothing);
        approach(blend, blendTarget, smoothing);
        const double amount = static_cast<double>(position + 1) / coefficientInterval;
        const double outputGain = std::pow(10.0, gain / 20.0);
        const std::array<float*, 2> channels{&left, &right};
        for (size_t channel = 0; channel < channels.size(); ++channel) {
            const double dry = std::isfinite(*channels[channel]) ? *channels[channel] : 0;
            double wet = dry;
            for (auto& band : bands) {
                const double a = band.current.process(wet, channel);
                if (band.transition) {
                    const double b = band.next.process(wet, channel);
                    wet = a + (b - a) * amount;
                } else {
                    wet = a;
                }
            }
            const double value = blend == 0 ? dry
                : dry * (1 - blend) + wet * outputGain * blend;
            const float result = static_cast<float>(value);
            *channels[channel] = std::isfinite(result) ? result : 0.0f;
        }
        if (++position == coefficientInterval) {
            position = 0;
            for (auto& band : bands)
                if (band.transition) band.current = band.next;
        }
    }

    // Settled, requested response: same sanitization and coefficient builder as
    // audio. Magnitude is linear, NOT dB; includes global trim and bypass.
    static std::complex<double> responseComplex(double frequency, double sampleRate,
                                                const EqualizerSettings& settings) noexcept {
        const double sr = validRate(sampleRate);
        if (!settings.enabled) return {1, 0};
        const auto z = unitDelay(frequency, sr);
        std::complex<double> result{std::pow(10.0, clean(settings.gainDb, 0, -60, 24) / 20), 0};
        for (const auto& band : settings.bands)
            result *= makeBank(controls(band, settings, sr), sr).response(z);
        return result;
    }
    static double frequencyResponse(double frequency, double sampleRate,
                                    const EqualizerSettings& settings) noexcept {
        return std::abs(responseComplex(frequency, sampleRate, settings));
    }

    // Frozen instantaneous response, including the current bank crossfades and
    // smoothed bypass/trim. A time-varying filter has no single LTI response.
    std::complex<double> responseComplex(double frequency) const noexcept {
        const auto z = unitDelay(frequency, rate);
        std::complex<double> result{std::pow(10.0, gain / 20), 0};
        const double amount = static_cast<double>(position) / coefficientInterval;
        for (const auto& band : bands) {
            const auto a = band.current.response(z);
            result *= band.transition ? a + (band.next.response(z) - a) * amount : a;
        }
        return (1 - blend) + blend * result;
    }
    double frequencyResponse(double frequency) const noexcept {
        return std::abs(responseComplex(frequency));
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    struct Coefficients {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::complex<double> response(std::complex<double> z) const noexcept {
            return (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
        }
    };
    struct Stage {
        Coefficients c{};
        std::array<double, 2> z1{}, z2{};
        double process(double x, size_t channel) noexcept {
            const double y = c.b0 * x + z1[channel];
            z1[channel] = c.b1 * x - c.a1 * y + z2[channel];
            z2[channel] = c.b2 * x - c.a2 * y;
            if (!std::isfinite(y) || !std::isfinite(z1[channel]) || !std::isfinite(z2[channel])) {
                z1[channel] = z2[channel] = 0;
                return 0;
            }
            if (std::abs(z1[channel]) < 1.0e-30) z1[channel] = 0;
            if (std::abs(z2[channel]) < 1.0e-30) z2[channel] = 0;
            return y;
        }
    };
    struct Bank {
        std::array<Stage, 4> stages{};
        int count = 0;
        double process(double x, size_t channel) noexcept {
            for (int i = 0; i < count; ++i) x = stages[static_cast<size_t>(i)].process(x, channel);
            return x;
        }
        std::complex<double> response(std::complex<double> z) const noexcept {
            std::complex<double> result{1, 0};
            for (int i = 0; i < count; ++i) result *= stages[static_cast<size_t>(i)].c.response(z);
            return result;
        }
    };
    struct Controls {
        bool enabled = false;
        int mode = 0;
        double logFrequency = std::log(1000.0), gain = 0, logQ = std::log(0.707);
        bool operator==(const Controls&) const = default;
    };
    struct Band {
        Bank current{}, next{};
        Controls value{};
        bool transition = false;
    };
    static double clean(double value, double fallback, double low, double high) noexcept {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }
    static double validRate(double sr) noexcept {
        return std::isfinite(sr) && sr >= 8000 && sr <= 768000 ? sr : 48000;
    }
    static void approach(double& value, double target, double step) noexcept {
        value += step * (target - value);
        if (std::abs(value - target) < 1.0e-8) value = target;
    }
    static std::complex<double> unitDelay(double frequency, double sr) noexcept {
        return std::polar(1.0, -2 * pi * clean(frequency, 1000, 0, sr * 0.5) / sr);
    }
    static Controls controls(const EqBandSettings& b, const EqualizerSettings& s, double sr) noexcept {
        Controls c;
        c.enabled = b.enabled;
        c.mode = std::clamp(b.mode, 0, 13);
        const double frequency = clean(b.frequency, 1000, 10, sr * 0.45)
            * std::exp2(clean(s.shiftSemitones, 0, -48, 48) / 12);
        c.logFrequency = std::log(std::clamp(frequency, 10.0, sr * 0.45));
        c.gain = clean(b.gainDb, 0, -24, 24);
        double q = clean(b.q, 0.707, 0.1, 20);
        if (s.adaptiveQ && c.mode == 0) q *= std::pow(10.0, std::abs(c.gain) / 80);
        c.logQ = std::log(std::clamp(q, 0.1, 20.0));
        return c;
    }
    static Coefficients normalize(double b0, double b1, double b2,
                                  double a0, double a1, double a2) noexcept {
        return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
    }
    static Coefficients biquad(int type, double f, double gainDb, double q, double sr) noexcept {
        const double w = 2 * pi * f / sr, cs = std::cos(w), sn = std::sin(w);
        const double alpha = sn / (2 * q), a = std::pow(10.0, gainDb / 40);
        if (type == 0)
            return normalize(1 + alpha * a, -2 * cs, 1 - alpha * a,
                             1 + alpha / a, -2 * cs, 1 - alpha / a);
        if (type == 13)
            return normalize(1, -2 * cs, 1, 1 + alpha, -2 * cs, 1 - alpha);
        if (type == 1 || type == 2) {
            const double beta = 2 * std::sqrt(a) * alpha;
            if (type == 1)
                return normalize(a * ((a + 1) - (a - 1) * cs + beta),
                    2 * a * ((a - 1) - (a + 1) * cs), a * ((a + 1) - (a - 1) * cs - beta),
                    (a + 1) + (a - 1) * cs + beta, -2 * ((a - 1) + (a + 1) * cs),
                    (a + 1) + (a - 1) * cs - beta);
            return normalize(a * ((a + 1) + (a - 1) * cs + beta),
                -2 * a * ((a - 1) + (a + 1) * cs), a * ((a + 1) + (a - 1) * cs - beta),
                (a + 1) - (a - 1) * cs + beta, 2 * ((a - 1) - (a + 1) * cs),
                (a + 1) - (a - 1) * cs - beta);
        }
        const bool highpass = type == 6;
        const double b0 = (1 + (highpass ? cs : -cs)) * 0.5;
        return normalize(b0, highpass ? -2 * b0 : 2 * b0, b0,
                         1 + alpha, -2 * cs, 1 - alpha);
    }
    static Bank makeBank(const Controls& c, double sr) noexcept {
        Bank bank;
        if (!c.enabled) return bank;
        const double f = std::exp(c.logFrequency), q = std::exp(c.logQ);
        if (c.mode == 5 || c.mode == 9) {
            const double k = std::tan(pi * f / sr), divisor = 1 + k;
            const double b0 = c.mode == 5 ? 1 / divisor : k / divisor;
            bank.count = 1;
            bank.stages[0].c = {b0, c.mode == 5 ? -b0 : b0, 0, (k - 1) / divisor, 0};
        } else if (c.mode >= 6 && c.mode <= 12 && c.mode != 9) {
            const int order = c.mode <= 8 ? c.mode - 6 : c.mode - 10;
            bank.count = 1 << order;
            constexpr std::array<double, 4> q8{0.5097955791, 0.6013448869, 0.8999762231, 2.5629154477};
            for (int i = 0; i < bank.count; ++i) {
                const double baseQ = bank.count == 1 ? 0.707 : bank.count == 2
                    ? (i == 0 ? 0.5411961001 : 1.3065629649) : q8[static_cast<size_t>(i)];
                bank.stages[static_cast<size_t>(i)].c = biquad(c.mode <= 8 ? 6 : 10,
                    f, 0, baseQ * q / 0.707, sr);
            }
        } else {
            bank.count = c.mode == 3 || c.mode == 4 ? 2 : 1;
            const int type = c.mode == 3 ? 1 : c.mode == 4 ? 2 : c.mode;
            for (int i = 0; i < bank.count; ++i)
                bank.stages[static_cast<size_t>(i)].c = biquad(type, f, c.gain / bank.count, q, sr);
        }
        return bank;
    }
    void update(const EqualizerSettings& settings) noexcept {
        for (size_t i = 0; i < bands.size(); ++i) {
            auto& band = bands[i];
            const auto target = controls(settings.bands[i], settings, rate);
            auto value = target;
            if (initialized) {
                value = band.value;
                value.enabled = target.enabled;
                value.mode = target.mode;
                approach(value.logFrequency, target.logFrequency, controlStep);
                approach(value.logQ, target.logQ, controlStep);
                approach(value.gain, target.gain, controlStep);
            }
            band.transition = false;
            if (!initialized || !(value == band.value)) {
                band.next = makeBank(value, rate);
                if (initialized && value.mode == band.value.mode && value.enabled == band.value.enabled) {
                    for (size_t stage = 0; stage < band.next.stages.size(); ++stage) {
                        band.next.stages[stage].z1 = band.current.stages[stage].z1;
                        band.next.stages[stage].z2 = band.current.stages[stage].z2;
                    }
                }
                if (!initialized) band.current = band.next;
                else band.transition = true;
                band.value = value;
            }
        }
    }

    double rate = 48000, smoothing = -std::expm1(-1.0 / 1200.0);
    double controlStep = -std::expm1(-64.0 / 1200.0), gain = 0, blend = 1;
    std::array<Band, 8> bands{};
    int position = 0;
    bool initialized = false;
};

} // namespace vocalstack
