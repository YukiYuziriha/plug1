#include "SpatialEq.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

namespace vocalstack {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double returnQ = 0.70710678;
constexpr double sleepThreshold = 1e-9;
constexpr double wetMinGain = 1e-5;
constexpr float overlapMinDepth = 0.001f; // percentage points
constexpr float overlapMinGain = 1e-5f;
using Stereo = std::array<double, 2>;

enum class Response { bell, lowShelf, highShelf, bandpass, lowpass, highpass };

struct Coeffs {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    double magnitude(std::complex<double> z, std::complex<double> z2) const noexcept {
        return std::abs((b0 + b1 * z + b2 * z2) / (1.0 + a1 * z + a2 * z2));
    }
};

double square(double x) noexcept { return x * x; }

// Match the analog magnitudes at DC, center and Nyquist for the digital poles.
Coeffs matchedPole(double rate, double frequency, double q, Response type, double gain = 0) noexcept {
    const double w = 2 * pi * std::clamp(frequency, 10.0, 0.475 * rate) / rate;
    const bool high = type == Response::highShelf;
    const bool shelf = high || type == Response::lowShelf;
    const bool bellCut = type == Response::bell && gain < 0;
    bool invert = bellCut;
    if (bellCut) {
        q = std::clamp(q, 0.1, 30.0);
        gain = std::min(-gain, 60.0);
    } else if (shelf) {
        // Fit a Low Shelf boost or High Shelf cut; the opposite signs use
        // the reciprocal of the corresponding prototype.
        q = std::clamp(q, 0.1, 30.0);
        invert = (gain > 0) == high;
        gain = std::min(std::abs(gain), gain > 0 ? 36.0 : 60.0) * (high ? -1 : 1);
    }
    const double boost = gain == 0 ? 1.0 : std::pow(10.0, gain / 40.0);
    const double pole = !shelf ? w : (high ? w * std::sqrt(boost) : w / std::sqrt(boost));
    const double damping = 0.5 / (shelf ? q : q * boost);
    const double radius = std::exp(-damping * pole);
    const double a1 = -2 * radius * (damping > 1 ? std::cosh(pole * std::sqrt(square(damping) - 1))
                                                 : std::cos(pole * std::sqrt(1 - square(damping))));
    const double a2 = square(radius);
    const double x = pi / w, detune = square(1 - square(x)), width = square(x / q);
    double dc = 0, center = q, nyquist = 0;
    if (type == Response::bell) {
        dc = 1;
        center = square(boost);
        nyquist = std::sqrt((detune + width * square(boost)) / (detune + width / square(boost)));
    } else if (shelf) {
        const double base = square(boost - square(x)) + boost * width;
        const double shifted = square(1 - boost * square(x)) + boost * width;
        dc = high ? 1.0 : square(boost);
        center = boost;
        nyquist = boost * std::sqrt(high ? shifted / base : base / shifted);
    } else {
        const double den = std::sqrt(detune + width);
        if (type == Response::bandpass) {
            center = 1;
            nyquist = x / (q * den);
        } else if (type == Response::lowpass) {
            dc = 1;
            nyquist = 1 / den;
        } else {
            nyquist = square(x) / den;
        }
    }
    const double dcNum = dc * std::abs(1 + a1 + a2);
    const double nyNum = nyquist * std::abs(1 - a1 + a2);
    const double sum = 0.5 * (dcNum + nyNum), b1 = 0.5 * (dcNum - nyNum);
    const double cw = std::cos(w), sw = std::sin(w);
    const double denReal = (1 + a2) * cw + a1, denImag = (1 - a2) * sw;
    const double diff = std::sqrt(std::max(0.0, square(center) * (square(denReal) + square(denImag))
                                                   - square(sum * cw + b1))) / sw;
    const double b0 = 0.5 * (sum + diff), b2 = 0.5 * (sum - diff);
    if (invert) {
        const double scale = std::abs(b0) > 1e-30 ? b0 : 1e-30;
        return {1 / scale, a1 / scale, a2 / scale, b1 / scale, b2 / scale};
    }
    return {b0, b1, b2, a1, a2};
}

// Far below the float output's useful range, but early enough to prevent
// subnormal arithmetic and let the shared high-pass settle into a true skip.
double quiet(double x) noexcept { return std::abs(x) < 1e-30 ? 0.0 : x; }

struct Filter {
    double s1 = 0, s2 = 0;

    double process(double x, const Coeffs& c) noexcept {
        const double y = c.b0 * x + s1;
        s1 = quiet(c.b1 * x - c.a1 * y + s2);
        s2 = quiet(c.b2 * x - c.a2 * y);
        return y;
    }
};

using StereoFilter = std::array<Filter, 2>;

Stereo filter(Stereo x, StereoFilter& states, const Coeffs& c) noexcept {
    return {states[0].process(x[0], c), states[1].process(x[1], c)};
}

bool hasSignal(const StereoFilter& states) noexcept {
    return states[0].s1 != 0 || states[0].s2 != 0 || states[1].s1 != 0 || states[1].s2 != 0;
}

bool hasSignal(Stereo x) noexcept { return x[0] != 0 || x[1] != 0; }

struct History {
    std::vector<double> samples;
    std::size_t head = 0, valid = 0;

    void prepare(std::size_t maxDistance) { samples.resize(maxDistance + 1); }

    // A logical clear is independent of sample rate and cannot expose old writes.
    void clear() noexcept { head = valid = 0; }

    double read(std::size_t distance) const noexcept {
        if (distance > valid) return 0;
        const auto index = head >= distance ? head - distance : head + samples.size() - distance;
        return samples[index];
    }

    void push(double value) noexcept {
        samples[head] = quiet(value);
        if (++head == samples.size()) head = 0;
        if (valid < samples.size()) ++valid;
    }
};

struct Delays {
    static constexpr std::array<int, 7> leftUs{7300, 13700, 21100, 29900, 42100, 57700, 73300};
    static constexpr std::array<int, 7> rightUs{9100, 15100, 23300, 32300, 44900, 59300, 71900};
    static constexpr std::array<double, 7> tapGain{0.28, -0.18, 0.16, 0.12, -0.10, 0.09, 0.07};
    static constexpr std::array<double, 6> diffusionMs{1.7, 5.3, 11.9, 2.9, 7.1, 13.7};
    static constexpr std::array<double, 4> feedbackMs{31.1, 37.3, 43.7, 53.3};

    std::array<std::array<std::array<std::size_t, 7>, 2>, 2> taps{}; // lane, bank, tap
    std::array<std::size_t, 6> diffusion{};
    std::array<std::size_t, 4> feedback{};
    std::array<double, 4> feedbackSeconds{};
    double fastStep = 0, slowStep = 0;

    explicit Delays(double rate) noexcept {
        const auto samples = [rate](double ms) { return (rate * ms) * 0.001; };
        for (std::size_t lane = 0; lane < taps.size(); ++lane) {
            const auto& us = lane == 0 ? leftUs : rightUs;
            for (std::size_t j = 0; j < us.size(); ++j) {
                taps[lane][0][j] = static_cast<std::size_t>(samples(us[j] / 1000.0));
                taps[lane][1][j] = static_cast<std::size_t>(samples((us[j] * 82 / 100) / 1000.0));
            }
        }
        for (std::size_t j = 0; j < diffusion.size(); ++j)
            diffusion[j] = std::max<std::size_t>(1, static_cast<std::size_t>(samples(diffusionMs[j]) + 0.5));
        for (std::size_t j = 0; j < feedback.size(); ++j) {
            feedback[j] = static_cast<std::size_t>(samples(feedbackMs[j]));
            feedbackSeconds[j] = static_cast<double>(feedback[j]) / rate;
        }
        fastStep = -std::expm1(-1 / (0.002 * rate));
        slowStep = -std::expm1(-1 / (0.040 * rate));
    }
};

struct Space {
    std::array<History, 2> reflections;
    std::array<History, 6> diffusion;
    std::array<History, 4> feedback;
    std::array<double, 4> feedbackGain{};
    StereoFilter extract{}, damping{};
    Filter extractSide{};
    Coeffs lowpass{};
    double fast = 0, slow = 0, designedAmount = -1;

    void prepare(const Delays& d) {
        for (std::size_t i = 0; i < reflections.size(); ++i)
            reflections[i].prepare(d.taps[i][0].back());
        for (std::size_t i = 0; i < diffusion.size(); ++i) diffusion[i].prepare(d.diffusion[i]);
        for (std::size_t i = 0; i < feedback.size(); ++i) feedback[i].prepare(d.feedback[i]);
    }

    void clear() noexcept {
        extract = damping = {};
        extractSide = {};
        fast = slow = 0;
        for (auto& ring : reflections) ring.clear();
        for (auto& ring : diffusion) ring.clear();
        for (auto& ring : feedback) ring.clear();
    }

    void design(const Delays& d, double rate, double targetAmount) noexcept {
        if (designedAmount == targetAmount) return;
        const double decay = 0.09 + 0.25 * targetAmount;
        for (std::size_t i = 0; i < feedback.size(); ++i)
            feedbackGain[i] = std::pow(0.001, d.feedbackSeconds[i] / decay);
        lowpass = matchedPole(rate, 16000 - 9500 * targetAmount, returnQ, Response::lowpass);
        designedAmount = targetAmount;
    }

    Stereo process(Stereo x, const Coeffs& feed, const Delays& d, double amount, double enable) noexcept {
        const auto u = filter(x, extract, feed);
        const double mid = 0.5 * (u[0] + u[1]);
        const double existingSide = extractSide.process(0.5 * (x[0] - x[1]), feed);
        Stereo diffused{mid, mid};
        for (std::size_t i = 0; i < diffusion.size(); ++i) {
            double& lane = diffused[i / 3];
            const double y = diffusion[i].read(d.diffusion[i]) - 0.55 * lane;
            diffusion[i].push(lane + 0.55 * y);
            lane = y;
        }
        const double side = amount * (0.55 * existingSide + 0.625 * (diffused[0] - diffused[1])) * enable;

        Stereo taps{};
        for (std::size_t lane = 0; lane < taps.size(); ++lane) {
            auto& ring = reflections[lane];
            // Write first: distance N now gives an input lag of N - 1.
            ring.push(u[lane] * enable);
            double a = 0, b = 0;
            for (std::size_t j = 0; j < Delays::tapGain.size(); ++j) {
                a += Delays::tapGain[j] * ring.read(d.taps[lane][0][j]);
                b += Delays::tapGain[j] * ring.read(d.taps[lane][1][j]);
            }
            taps[lane] = (1 - amount) * a + amount * b;
        }
        Stereo wet{0.85 * taps[0] + 0.15 * taps[1], 0.15 * taps[0] + 0.85 * taps[1]};
        std::array<double, 4> v{};
        for (std::size_t i = 0; i < feedback.size(); ++i)
            v[i] = feedbackGain[i] * feedback[i].read(d.feedback[i]);
        wet[0] += 0.12 * (v[0] + v[2]);
        wet[1] += 0.12 * (v[1] + v[3]);
        const double drive = 0.25 * (mid * enable);
        const double sum01 = v[0] + v[1], sum23 = v[2] + v[3];
        const double diff01 = v[0] - v[1], diff23 = v[2] - v[3];
        feedback[0].push(0.5 * (sum01 + sum23) + drive);
        feedback[1].push(0.5 * (diff01 + diff23) - drive);
        feedback[2].push(0.5 * (sum01 - sum23) + drive);
        feedback[3].push(0.5 * (diff01 - diff23) - drive);

        const double power = 0.5 * (square(u[0]) + square(u[1]));
        fast = quiet(fast + d.fastStep * (power - fast));
        slow = quiet(slow + d.slowStep * (power - slow));
        const double duck = std::clamp(std::sqrt((slow + 1e-15) / (fast + 1e-15)), 0.2, 1.0);
        const double level = 0.65 * amount * duck * enable;
        wet = filter(wet, damping, lowpass);
        return {wet[0] * level + side, wet[1] * level - side};
    }
};

struct Controls {
    float frequency = 1000, gain = 0, q = 1, depth = 0; // Depth in percentage points
    SpatialEqShape shape = SpatialEqShape::Bell;
    bool active = false, enabled = true;


    bool on() const noexcept { return active && enabled; }
    bool eligible() const noexcept { return on() && gain > overlapMinGain && depth > overlapMinDepth; }
};

float bounded(float x, float fallback, float lo, float hi) noexcept {
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}

Controls sanitize(SpatialEqBandSettings b, double rate) noexcept {
    if (b.shape != SpatialEqShape::LowShelf && b.shape != SpatialEqShape::HighShelf)
        b.shape = SpatialEqShape::Bell;
    const float high = static_cast<float>(std::min(20000.0, 0.475 * rate));
    const float gain = bounded(b.gainDb, 0, -30, 30);
    const float minQ = b.shape == SpatialEqShape::Bell && gain > 0 ? 0.5f : 0.1f;
    const float depth = bounded(b.depth, 0, 0, 1);
    return {bounded(b.frequency, std::min(1000.0f, high), 20, high), gain,
            bounded(b.q, 1, minQ, 30), depth < std::numeric_limits<float>::min() ? 0.0f
                 : static_cast<float>(100.0 * depth), b.shape, b.active, b.enabled};
}

float flushDepth(float depth) noexcept {
    return depth < std::numeric_limits<float>::min() ? 0.0f : depth;
}

float stepLinear(float current, float target, double alpha) noexcept {
    return static_cast<float>(static_cast<double>(target - current) * alpha + current);
}

float stepLog(float current, float target, double alpha) noexcept {
    const float oldLog = static_cast<float>(std::log(static_cast<double>(current)));
    const float newLog = static_cast<float>(std::log(static_cast<double>(target)));
    return static_cast<float>(std::exp(static_cast<double>(newLog - oldLog) * alpha + oldLog));
}

Response directResponse(SpatialEqShape shape) noexcept {
    if (shape == SpatialEqShape::LowShelf) return Response::lowShelf;
    if (shape == SpatialEqShape::HighShelf) return Response::highShelf;
    return Response::bell;
}

struct Amount {
    double value = 0, target = 0;
    bool running = false;

    bool live() const noexcept { return value != 0 || target != 0; }

    // A paused band retains its epoch. A dormant path restarts with empty
    // history, independently of the other path's gain sign or tail state.
    bool advance(double step) noexcept {
        if (live()) {
            value += (target - value) * step;
            if (target == 0 && value < sleepThreshold) value = 0;
        }
        const bool wasRunning = running;
        running = live();
        return running && !wasRunning;
    }
};

struct Band {
    Controls target{}, current{};
    Coeffs eq{}, feed{}, overlapFeed{};
    StereoFilter direct{}, wetCut{};
    Space space;
    float eqFrequency = 0, eqGain = 0, eqQ = 0, feedFrequency = 0, feedQ = 0;
    float overlapFrequency = 0, overlapQ = 0;
    Amount boost{}, cut{};
    double enable = 0, weight = 1;
    bool running = false;

    void updateAmounts() noexcept {
        const double depth = static_cast<double>(current.depth) * 0.01;
        // The host gain gates the boost immediately at the tick, even while
        // the working gain is still positive. Cut Depth follows working gain.
        boost.target = depth != 0 && current.gain > 0 && target.gain > wetMinGain
            ? depth * -std::expm1(-static_cast<double>(current.gain) / 3) : 0;
        cut.target = current.gain < 0 ? depth : 0;
    }

    void settle() noexcept {
        current = target;
        updateAmounts();
        boost = {boost.target, boost.target, false};
        cut = {cut.target, cut.target, false};
        eqFrequency = feedFrequency = overlapFrequency = 0;
        direct = wetCut = {};
        space.clear();
    }

    void updateDesign(double rate, double alpha) noexcept {
        current.depth = flushDepth(stepLinear(current.depth, target.depth, alpha));
        current.frequency = stepLog(current.frequency, target.frequency, alpha);
        current.q = stepLog(current.q, target.q, alpha);
        current.gain = stepLinear(current.gain, target.gain, alpha);
        updateAmounts();
        if (eqFrequency != current.frequency || eqQ != current.q || eqGain != current.gain) {
            eq = std::abs(current.gain) < 1e-9 ? Coeffs{} : matchedPole(
                rate, current.frequency, current.q, directResponse(current.shape), current.gain);
            eqFrequency = current.frequency;
            eqQ = current.q;
            eqGain = current.gain;
        }
    }

    Coeffs makeExtraction(double rate, float frequency, float q) const noexcept {
        if (current.shape == SpatialEqShape::Bell) return matchedPole(rate, frequency, q, Response::bandpass);
        const auto response = current.shape == SpatialEqShape::LowShelf ? Response::lowpass : Response::highpass;
        return matchedPole(rate, frequency, returnQ, response);
    }

    const Coeffs& overlapExtraction(double rate) noexcept {
        if (current.frequency == feedFrequency && current.q == feedQ) return feed;
        if (overlapFrequency != current.frequency || overlapQ != current.q) {
            overlapFeed = makeExtraction(rate, current.frequency, current.q);
            overlapFrequency = current.frequency;
            overlapQ = current.q;
        }
        return overlapFeed;
    }

    void updateSpace(const Delays& delays, double rate) noexcept {
        if (feedFrequency != current.frequency || feedQ != current.q) {
            feed = overlapExtraction(rate);
            feedFrequency = current.frequency;
            feedQ = current.q;
        }
        space.design(delays, rate, boost.target);
    }

    Stereo processDirect(Stereo x, double step) noexcept {
        enable += ((target.on() ? 1.0 : 0.0) - enable) * step;
        running = target.active || enable >= sleepThreshold;
        if (!running) return x;

        if (boost.advance(step)) space.clear();
        if (cut.advance(step)) wetCut = {};
        if (std::abs(eqGain) < 1e-9 && !hasSignal(direct)) return x;
        auto y = filter(x, direct, eq);
        if (enable != 1) {
            y[0] = x[0] + (y[0] - x[0]) * enable;
            y[1] = x[1] + (y[1] - x[1]) * enable;
        }
        if (cut.value != 0) {
            const double mid = 0.5 * ((x[0] - y[0]) + (x[1] - y[1])) * cut.value;
            y[0] += mid;
            y[1] += mid;
        }
        return y;
    }
};

} // namespace

struct SpatialEq::State {
    const double rate;
    const Delays delays;
    const double tickStep, sampleStep;
    const Coeffs highpass;
    StereoFilter wetHighpass{};
    std::array<Band, SpatialEqSettings::bandCount> bands{};
    int phase = 0;
    bool firstTick = true, initialized = false;

    explicit State(double sampleRate)
        : rate(sampleRate), delays(rate), tickStep(1 - std::exp(-coefficientInterval / (0.018 * rate))),
          sampleStep(1 - std::exp(-1 / (0.012 * rate))),
          highpass(matchedPole(rate, 120, returnQ, Response::highpass)) {
        for (auto& band : bands) band.space.prepare(delays);
    }

    void setTargets(const SpatialEqSettings& settings) noexcept {
        for (std::size_t i = 0; i < bands.size(); ++i) bands[i].target = sanitize(settings.bands[i], rate);
    }

    void reset() noexcept {
        phase = 0;
        firstTick = true;
        wetHighpass = {};
        for (auto& b : bands) {
            b.settle();
            b.enable = b.target.on() ? 1 : 0;
            b.weight = 1;
            b.running = false;
        }
        initialized = true;
    }

    void tick() noexcept {
        std::array<bool, SpatialEqSettings::bandCount> eligible{};
        bool anyEligible = false;
        for (std::size_t i = 0; i < bands.size(); ++i) {
            eligible[i] = bands[i].target.eligible();
            anyEligible |= eligible[i];
        }
        for (auto& b : bands) {
            // Receivers see all slots' new eligibility but later slots' previous
            // extraction designs. Don't move this scan to a separate second pass.
            const bool topologyChanged = b.current.shape != b.target.shape;
            if (topologyChanged) b.settle();
            if (!firstTick && !topologyChanged && !b.target.active && b.enable < sleepThreshold) {
                b.current = b.target;
                continue;
            }
            b.updateDesign(rate, tickStep);
            if (b.boost.live()) b.updateSpace(delays, rate);
            double total = 0;
            if (anyEligible) {
                const double angle = (b.current.frequency * (2 * pi)) / rate;
                const std::complex<double> z{std::cos(angle), -std::sin(angle)};
                const auto z2 = z * z;
                for (std::size_t i = 0; i < bands.size(); ++i)
                    if (eligible[i]) total += bands[i].overlapExtraction(rate).magnitude(z, z2);
            }
            const double weight = 1 / std::max(1.0, total);
            b.weight = firstTick ? weight : b.weight + (weight - b.weight) * tickStep;
        }
        firstTick = false;
    }

    Stereo process(Stereo x) noexcept {
        if (!initialized) reset();
        if (phase == 0) tick();
        if (++phase == coefficientInterval) phase = 0;
        bool boostLive = false, cutLive = false;
        for (auto& b : bands) {
            x = b.processDirect(x, sampleStep);
            if (b.running) {
                cutLive |= b.cut.running;
                boostLive |= b.boost.running;
            }
        }
        if (!boostLive && !cutLive && !hasSignal(wetHighpass)) return x;

        Stereo wet{};
        // All boosts extract from the same output of the serial direct pass.
        if (boostLive) for (auto& b : bands) {
            if (!b.running || !b.boost.running) continue;
            const auto y = b.space.process(x, b.feed, delays, b.boost.value, b.enable);
            wet[0] += y[0] * b.weight; // Fixed unity Strength, after the complete return.
            wet[1] += y[1] * b.weight;
        }
        if (cutLive) for (auto& b : bands) {
            if (!b.running || !b.cut.running || (!hasSignal(wet) && !hasSignal(b.wetCut))) continue;
            // Design with min(working gain, 0). Even at identity, run the
            // remaining cut history while its blend is slewing out.
            const auto y = filter(wet, b.wetCut, b.eqGain < 0 ? b.eq : Coeffs{});
            wet[0] += (y[0] - wet[0]) * b.enable * b.cut.value;
            wet[1] += (y[1] - wet[1]) * b.enable * b.cut.value;
        }
        if (hasSignal(wet) || hasSignal(wetHighpass)) {
            wet = filter(wet, wetHighpass, highpass);
            x[0] += wet[0];
            x[1] += wet[1];
        }
        return x;
    }
};

SpatialEq::SpatialEq() noexcept = default;
SpatialEq::~SpatialEq() = default;

void SpatialEq::prepare(double sampleRate) {
    if (!std::isfinite(sampleRate) || sampleRate < 1000 || sampleRate > 768000) sampleRate = 48000;
    state = std::make_unique<State>(sampleRate);
    state->setTargets(targets);
}

void SpatialEq::reset() noexcept {
    if (state) state->reset();
}

void SpatialEq::setSettings(const SpatialEqSettings& settings) noexcept {
    targets = settings;
    if (state) state->setTargets(targets);
}

void SpatialEq::process(float& left, float& right) noexcept {
    if (!state) return;
    const auto y = state->process({std::isfinite(left) ? left : 0.0, std::isfinite(right) ? right : 0.0});
    const float l = static_cast<float>(y[0]), r = static_cast<float>(y[1]);
    left = std::isfinite(l) ? l : 0.0f;
    right = std::isfinite(r) ? r : 0.0f;
}

void SpatialEq::process(float* left, float* right, std::size_t frames) noexcept {
    if (!state) return;
    for (std::size_t i = 0; i < frames; ++i) process(left[i], right[i]);
}

} // namespace vocalstack
