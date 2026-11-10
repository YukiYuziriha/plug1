#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace vocalstack {

struct WaveshaperSettings {
    bool enabled = true;
    float driveDb = 0, outputDb = 0, mix = 1;
    int shape = 0;
    bool raw = false;
    float shapeAmount = 0.5f;
    int quality = 0; // appended: 0 = Off (legacy ADAA), 1 = 2x, 2 = 4x
    bool levelMatch = false;
};

// Off preserves project-rate first-order ADAA. 2x/4x add cascaded half-band IIR
// interpolation/decimation and evaluate ADAA at the increased sample rate.
// Neither strategy is alias-free. Raw always evaluates directly at project rate,
// overriding quality on the audible path. ADAA and IIR resampling change phase
// and attenuation; zero explicit integer latency does NOT mean phase alignment.
// Local dry and graph bypass remain direct, so parallel paths may comb.
// All curves have analytic primitives, including piecewise/discontinuous curves.
// Near-coincident endpoints use the midpoint limit to avoid cancellation.
// Controls/curve/Raw crossfades use 10 ms smoothing; histories run through bypass.
// New asymmetric/rectifying curves have a 5 Hz DC blocker, legacy curves do not.
// Quality crossfades use the same 10 ms smoothing. All paths are preallocated;
// newly activated quality filters are primed to the current sample's steady
// state under their fade-in, and run throughout fade-out/Raw/bypass. No RT graph
// rebuild, allocation, lock or I/O.
class Waveshaper {
public:
    // This is explicit whole-sample delay, not a rounded IIR group-delay claim.
    static constexpr int latencySamples = 0;
    static constexpr bool qualityHasFrequencyDependentPhase = true;
    static constexpr double tailSeconds = 1.0; // includes DC and resampler decay
    static constexpr std::array<const char*, 3> qualityNames{"Off", "2x", "4x"};
    static constexpr std::array<int, 3> qualityFactors{1, 2, 4};
    struct ProcessingMetadata {
        int oversamplingFactor;
        int integerLatencySamples;
        bool frequencyDependentPhase;
    };
    // Settled requested-mode metadata, not instantaneous crossfade state. Raw
    // overrides quality; the optional DC blocker still operates at project rate.
    static ProcessingMetadata processingMetadata(const WaveshaperSettings& s) noexcept {
        const bool audible = s.enabled && clean(s.mix, 1, 0, 1) > 0;
        return {audible && !s.raw ? qualityFactors[static_cast<size_t>(std::clamp(s.quality, 0, 2))] : 1,
                latencySamples, audible && (!s.raw || needsDc(std::clamp(s.shape, 0, 18)))};
    }
    static constexpr std::array<const char*, 19> shapeNames{
        "Soft", "Hard", "Asymmetric", "Warm Asymmetric", "Sine Saturation",
        "Arctangent", "Rounded Clip", "Asymmetric Clip", "Triangle Fold",
        "Sine Fold", "Single Fold", "Multi Fold", "Half-Wave", "Full-Wave",
        "Staircase", "Dead Zone", "Binary Threshold", "Exponential", "S-Curve"};

    static constexpr bool hasShapeAmount(int curve) noexcept {
        return curve >= 3 && curve < int(shapeNames.size());
    }
    // Memoryless requested transfer for static UI plots. Does not simulate ADAA,
    // smoothing or DC removal; Raw does not change this static curve.
    static double transfer(double x, int curve, double amount = 0.5) noexcept {
        if (!std::isfinite(x)) return 0;
        return evaluate(x, std::clamp(curve, 0, 18), clean(amount, 0.5, 0, 1));
    }

    void prepare(double sampleRate) noexcept {
        const double rate = std::isfinite(sampleRate) && sampleRate >= 8000 && sampleRate <= 768000
                    ? sampleRate : 48000;
        smoothing = -std::expm1(-1.0 / (rate * 0.010));
        dcPole = std::exp(-2 * pi * 5 / rate);
        energyStep = -std::expm1(-1.0 / (rate * 0.300));
        reset();
    }
    void reset() noexcept {
        previous = {}; dcInput = {}; dcOutput = {}; weights = {}; weights[0] = 1;
        qualityWeights = {1, 0, 0}; oversampled = {};
        drive = output = blend = rawBlend = 0; amount = 0.5; initialized = false;
        inputEnergy = shapedEnergy = matchDb = matchBlend = 0;
    }
    void process(float& left, float& right, const WaveshaperSettings& settings) noexcept {
        const double driveTarget = clean(settings.driveDb, 0, -24, 48);
        const double outputTarget = clean(settings.outputDb, 0, -60, 24);
        const double blendTarget = settings.enabled ? clean(settings.mix, 1, 0, 1) : 0;
        const double amountTarget = clean(settings.shapeAmount, 0.5, 0, 1);
        const int shape = std::clamp(settings.shape, 0, 18);
        const int quality = std::clamp(settings.quality, 0, 2);
        if (!initialized) {
            drive = driveTarget; output = outputTarget; blend = blendTarget;
            rawBlend = settings.raw ? 1 : 0; amount = amountTarget;
            matchBlend = settings.levelMatch ? 1 : 0;
            weights = {}; weights[static_cast<size_t>(shape)] = 1;
            qualityWeights = {}; qualityWeights[static_cast<size_t>(quality)] = 1;
            initialized = true;
        }
        approach(drive, driveTarget); approach(output, outputTarget); approach(blend, blendTarget);
        approach(rawBlend, settings.raw ? 1 : 0); approach(amount, amountTarget);
        for (size_t i = 0; i < qualityWeights.size(); ++i) {
            if (i > 0 && qualityWeights[i] == 0 && i == static_cast<size_t>(quality)) {
                oversampled[i - 1] = {}; // bounded state clear, no preparation/allocation
                for (auto& channel : oversampled[i - 1]) channel.primeOnFirstSample = true;
            }
            approach(qualityWeights[i], i == static_cast<size_t>(quality) ? 1 : 0);
        }
        for (size_t i = 0; i < weights.size(); ++i)
            approach(weights[i], i == static_cast<size_t>(shape) ? 1 : 0);
        const double pre = std::pow(10.0, drive / 20.0);
        const double post = std::pow(10.0, output / 20.0);
        const std::array<float*, 2> channels{&left, &right};
        std::array<double, 2> drySamples{}, wetSamples{};
        for (size_t channel = 0; channel < channels.size(); ++channel) {
            const double dry = std::isfinite(*channels[channel]) ? *channels[channel] : 0;
            const double x = dry * pre, old = previous[channel], delta = x - old;
            double baseAa = 0, baseRaw = 0;
            for (int curve = 0; curve < int(shapeNames.size()); ++curve) {
                const auto index = static_cast<size_t>(curve);
                // Independent Raw/ADAA DC histories stay warm during switching.
                if (weights[index] == 0 && !needsDc(curve)) continue;
                double aa = antialiased(x, old, delta, curve);
                double raw = evaluate(x, curve, amount);
                if (needsDc(curve)) {
                    aa = removeDc(aa, dcInput[0][channel][index], dcOutput[0][channel][index]);
                    raw = removeDc(raw, dcInput[1][channel][index], dcOutput[1][channel][index]);
                }
                baseAa += weights[index] * aa;
                baseRaw += weights[index] * raw;
            }
            previous[channel] = x;
            double aa = qualityWeights[0] * baseAa;
            for (size_t q = 1; q < qualityWeights.size(); ++q)
                if (qualityWeights[q] != 0)
                    aa += qualityWeights[q] * processOversampled(oversampled[q - 1][channel], x, q == 2);
            // Exact endpoint: quality never filters or delays Raw audio.
            const double wet = rawBlend == 1 ? baseRaw : rawBlend == 0 ? aa : aa + rawBlend * (baseRaw - aa);
            drySamples[channel] = dry;
            wetSamples[channel] = wet;
        }
        const double inPower = 0.5 * (drySamples[0] * drySamples[0] + drySamples[1] * drySamples[1]);
        const double wetPower = 0.5 * (wetSamples[0] * wetSamples[0] + wetSamples[1] * wetSamples[1]);
        inputEnergy += energyStep * (inPower - inputEnergy);
        shapedEnergy += energyStep * (wetPower - shapedEnergy);
        // Audition aid, not a limiter/LUFS matcher. Gate quiet input, bound gain,
        // and keep manual output trim outside the estimate so it remains useful.
        const double targetDb = inputEnergy > 1e-10 && shapedEnergy > 1e-12
            ? std::clamp(10 * std::log10(inputEnergy / shapedEnergy), -24.0, 24.0) : 0;
        matchDb += energyStep * (targetDb - matchDb);
        approach(matchBlend, settings.levelMatch ? 1 : 0);
        const double matchedPost = post * (matchBlend == 0 ? 1 : std::pow(10.0, matchDb * matchBlend / 20));
        for (size_t channel = 0; channel < channels.size(); ++channel) {
            const double result = blend == 0 ? drySamples[channel]
                : drySamples[channel] * (1 - blend) + wetSamples[channel] * matchedPost * blend;
            const double limit = std::numeric_limits<float>::max();
            *channels[channel] = static_cast<float>(std::clamp(result, -limit, limit));
        }
    }
private:
    static constexpr double pi = 3.14159265358979323846;
    // Polyphase half-band low-pass: two six-section allpass branches. Coefficients
    // are fixed at normalized frequency, so no rate-dependent RT filter design.
    // H(z) = (A0(z^2) + z^-1 A1(z^2))/2. Cross-branch decimation at the odd
    // phase gives unity-magnitude allpass reconstruction for a linear transfer,
    // NOT unity phase. Each second stage doubles the intermediate sample rate.
    struct HalfBand {
        static constexpr std::array<double, 6> evenCoefficients{
            .036681502163648017, .2746317593794541, .56109896978791948,
            .769741833862266, .8922608180038789, .962094548378084};
        static constexpr std::array<double, 6> oddCoefficients{
            .13654762463195771, .42313861743656667, .6775400499741616,
            .839889624849638, .9315419599631839, .9878163707328971};
        std::array<double, 6> evenState{}, oddState{};
        static double allpass(double x, const std::array<double, 6>& coefficients,
                              std::array<double, 6>& state) noexcept {
            for (size_t i = 0; i < state.size(); ++i) {
                const double y = coefficients[i] * x + state[i];
                state[i] = x - coefficients[i] * y;
                if (std::abs(state[i]) < 1e-30) state[i] = 0; // suppress denormal tails
                x = y;
            }
            return x;
        }
        void setConstant(double x) noexcept {
            for (size_t i = 0; i < evenState.size(); ++i) {
                evenState[i] = (1 - evenCoefficients[i]) * x;
                oddState[i] = (1 - oddCoefficients[i]) * x;
            }
        }
        std::array<double, 2> up(double x) noexcept {
            return {allpass(x, evenCoefficients, evenState), allpass(x, oddCoefficients, oddState)};
        }
        double down(const std::array<double, 2>& x) noexcept {
            return 0.5 * (allpass(x[0], oddCoefficients, oddState)
                        + allpass(x[1], evenCoefficients, evenState));
        }
    };
    struct OversampledChannel {
        HalfBand upFirst, upSecond;
        // Separate ordinary/DC-prone signal groups keep the legacy curves free
        // of DC filtering. DC removal follows decimation at project rate.
        std::array<HalfBand, 2> downFirst{}, downSecond{};
        double previous = 0, dcInput = 0, dcOutput = 0;
        bool primeOnFirstSample = false;
    };
    double removeDc(double x, double& input, double& output) const noexcept {
        const double y = x - input + dcPole * output;
        input = x; output = std::abs(y) < 1e-30 ? 0 : y;
        return output;
    }
    double antialiased(double x, double old, double delta, int curve) const noexcept {
        return std::abs(delta) < 1e-5 * (1 + std::abs(x) + std::abs(old))
            ? evaluate((x + old) * 0.5, curve, amount)
            : (primitive(x, curve, amount) - primitive(old, curve, amount)) / delta;
    }
    std::array<double, 2> oversampledTransfer(double x, double& old) const noexcept {
        std::array<double, 2> groups{};
        for (int curve = 0; curve < int(shapeNames.size()); ++curve) {
            const auto index = static_cast<size_t>(curve);
            if (weights[index] != 0)
                groups[needsDc(curve) ? 1 : 0] += weights[index] * antialiased(x, old, x - old, curve);
        }
        old = x;
        return groups;
    }
    double processOversampled(OversampledChannel& state, double x, bool fourTimes) const noexcept {
        if (state.primeOnFirstSample) {
            // Constant-signal initial conditions avoid a resampler startup burst
            // on live activation. They do not invent past audio or promise a
            // phase-coherent switch for arbitrary signals; the fade remains.
            std::array<double, 2> groups{};
            for (int c = 0; c < int(shapeNames.size()); ++c)
                if (weights[static_cast<size_t>(c)] != 0)
                    groups[needsDc(c) ? 1 : 0] += weights[static_cast<size_t>(c)] * evaluate(x, c, amount);
            state.upFirst.setConstant(x); state.upSecond.setConstant(x);
            for (size_t group = 0; group < 2; ++group) {
                state.downFirst[group].setConstant(groups[group]);
                state.downSecond[group].setConstant(groups[group]);
            }
            state.previous = x; state.dcInput = groups[1]; state.dcOutput = 0;
            state.primeOnFirstSample = false;
        }
        const auto intermediate = state.upFirst.up(x);
        std::array<std::array<double, 2>, 2> middle{}; // [signal group][phase]
        for (size_t phase = 0; phase < 2; ++phase) {
            if (fourTimes) {
                const auto fine = state.upSecond.up(intermediate[phase]);
                std::array<std::array<double, 2>, 2> shaped{};
                for (size_t j = 0; j < 2; ++j) {
                    const auto groups = oversampledTransfer(fine[j], state.previous);
                    for (size_t group = 0; group < 2; ++group) shaped[group][j] = groups[group];
                }
                for (size_t group = 0; group < 2; ++group)
                    middle[group][phase] = state.downSecond[group].down(shaped[group]);
            } else {
                const auto groups = oversampledTransfer(intermediate[phase], state.previous);
                for (size_t group = 0; group < 2; ++group) middle[group][phase] = groups[group];
            }
        }
        const double ordinary = state.downFirst[0].down(middle[0]);
        const double asymmetric = state.downFirst[1].down(middle[1]);
        return ordinary + removeDc(asymmetric, state.dcInput, state.dcOutput);
    }
    static bool needsDc(int c) noexcept { return c == 3 || c == 7 || c == 12 || c == 13; }
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
    static double triangle(double x) noexcept {
        double r = std::fmod(x, 4.0);
        if (r < -1) r += 4;
        if (r > 3) r -= 4;
        return r <= 1 ? r : 2 - r;
    }
    static double trianglePrimitive(double x) noexcept {
        double r = std::fmod(x, 4.0);
        if (r < 0) r += 4;
        if (r <= 1) return r * r * 0.5;
        if (r <= 3) return 1 - (r - 2) * (r - 2) * 0.5;
        return (r - 4) * (r - 4) * 0.5;
    }
    static double clipPrimitive(double x, double low, double high) noexcept {
        if (x < low) return low * x - low * low * 0.5;
        if (x > high) return high * x - high * high * 0.5;
        return x * x * 0.5;
    }
    static double evaluate(double x, int c, double a) noexcept {
        const double k = 0.5 + 2 * a, t = k * x, v = std::abs(t);
        switch (c) {
        case 1: return std::clamp(x, -1.0, 1.0);
        case 2: { const double b = std::tanh(0.5); return (std::tanh(x + 0.5) - b) / (1 - b * b); }
        case 3: { const double b = 0.15 + 0.7 * a, z = std::tanh(b); return (std::tanh(t + b) - z) / (k * (1 - z * z)); }
        case 4: return std::sin(std::clamp(t, -pi / 2, pi / 2));
        case 5: return 2 / pi * std::atan(t * pi / 2);
        case 6: return t / std::hypot(1.0, t);
        case 7: return std::clamp(x, -(0.2 + 0.8 * a), 1.0);
        case 8: return triangle(t);
        case 9: return std::sin(t);
        case 10: return std::copysign(1.0, t) * (v <= 1 ? v : v < 3 ? 2 - v : -1);
        case 11: return (triangle(t * 3) + 0.25 * triangle(t * 9)) / 1.25;
        case 12: return std::max(0.0, x) + (1 - a * 2) * std::min(0.0, x);
        case 13: return (1 - a) * x + a * std::abs(x);
        case 14: { const double n = 2 + 14 * a; return std::copysign(std::floor(n * std::min(1.0, std::abs(x)) + 0.5) / n, x); }
        case 15: return std::copysign(std::max(0.0, std::abs(x) - a * 0.9), x);
        case 16: return std::abs(x) <= 0.02 + 0.8 * a ? 0 : std::copysign(1.0, x);
        case 17: return std::copysign(-std::expm1(-v), t);
        case 18: { const double u = std::clamp(t, -1.0, 1.0); return u * (1.5 - 0.5 * u * u); }
        default: return std::tanh(x);
        }
    }
    static double primitive(double x, int c, double a) noexcept {
        const double k = 0.5 + 2 * a, t = k * x, v = std::abs(t);
        switch (c) {
        case 1: { const double z = std::abs(x); return z <= 1 ? 0.5 * x * x : z - 0.5; }
        case 2: { const double b = std::tanh(0.5); return (logCosh(x + 0.5) - b * x) / (1 - b * b); }
        case 3: { const double b = 0.15 + 0.7 * a, z = std::tanh(b); return (logCosh(t + b) - z * t) / (k * k * (1 - z * z)); }
        case 4: return (v <= pi / 2 ? 1 - std::cos(t) : 1 + v - pi / 2) / k;
        case 5: { const double z = t * pi / 2; return (z * std::atan(z) - std::log(std::hypot(1.0, z))) * 4 / (k * pi * pi); }
        case 6: return std::hypot(1.0, t) / k;
        case 7: return clipPrimitive(x, -(0.2 + 0.8 * a), 1);
        case 8: return trianglePrimitive(t) / k;
        case 9: return -std::cos(t) / k;
        case 10: return (v <= 1 ? v * v * 0.5 : v < 3 ? 2 * v - v * v * 0.5 - 1 : 3.5 - v) / k;
        case 11: return (trianglePrimitive(t * 3) / 3 + 0.25 * trianglePrimitive(t * 9) / 9) / (1.25 * k);
        case 12: return x * x * 0.5 * (x < 0 ? 1 - a * 2 : 1);
        case 13: return (1 - a) * x * x * 0.5 + a * x * std::abs(x) * 0.5;
        case 14: {
            const double n = 2 + 14 * a, z = std::min(1.0, std::abs(x));
            const double q = std::floor(n * z + 0.5);
            return q * z / n - q * q / (2 * n * n)
                + std::max(0.0, std::abs(x) - 1) * std::floor(n + 0.5) / n;
        }
        case 15: { const double z = std::max(0.0, std::abs(x) - a * 0.9); return z * z * 0.5; }
        case 16: return std::max(0.0, std::abs(x) - (0.02 + 0.8 * a));
        case 17: return (v + std::expm1(-v)) / k;
        case 18: return (v <= 1 ? 0.75 * t * t - 0.125 * t * t * t * t : v - 0.375) / k;
        default: return logCosh(x);
        }
    }
    double smoothing = -std::expm1(-1.0 / 480.0), dcPole = std::exp(-2 * pi * 5 / 48000);
    double drive = 0, output = 0, blend = 0, rawBlend = 0, amount = 0.5;
    double inputEnergy = 0, shapedEnergy = 0, matchDb = 0, matchBlend = 0, energyStep = 0;
    std::array<double, 19> weights{1};
    std::array<double, 2> previous{};
    std::array<std::array<std::array<double, 19>, 2>, 2> dcInput{}, dcOutput{};
    std::array<double, 3> qualityWeights{1, 0, 0};
    std::array<std::array<OversampledChannel, 2>, 2> oversampled{};
    bool initialized = false;
};

} // namespace vocalstack
