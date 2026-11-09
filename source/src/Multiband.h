#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace vocalstack {

// LR4: two cascaded Butterworth biquads per branch. In three-band mode
// split at Low, split its high branch at High, and all-pass the low branch
// through the High split. Thus L+M+H = AP(Low)*AP(High), unity magnitude,
// NOT a dry null. Parallel unfiltered/differently processed paths can comb;
// this frequency-dependent phase is not compensatable by integer latency.
// No explicit delay, no allocation. IIR tails decay asymptotically; the routing
// tail bound uses 8 seconds per filter (10 Hz minimum cutoff). Cutoffs smooth
// logarithmically over 25 ms, coefficients update every 32 samples, retaining
// history. Changing band count resets history and is an intentional topology edit.
class Multiband {
public:
    static constexpr int latencySamples = 0;
    static constexpr double tailSeconds = 8;
    using Bands = std::array<std::array<float, 2>, 3>;
    void prepare(double sampleRate) noexcept {
        rate = std::isfinite(sampleRate) && sampleRate >= 8000 && sampleRate <= 768000 ? sampleRate : 48000;
        step = -std::expm1(-32.0 / (rate * .025));
        reset();
    }
    void reset() noexcept {
        low = {}; upper = {}; alignment = {};
        initialized = false; position = 0; count = 3;
    }
    static double cutoff(double hz, double sr, double fallback) noexcept {
        return std::clamp(std::isfinite(hz) ? hz : fallback, 10.0, sr * .45);
    }
    Bands process(float left, float right, int bands = 3, float lowHz = 250, float highHz = 4000) noexcept {
        const int targetCount = bands == 2 ? 2 : 3;
        if (initialized && targetCount != count) reset();
        count = targetCount;
        if (position == 0) {
            const double lo = std::log(cutoff(lowHz, rate, 250));
            const double hi = std::log(std::max(cutoff(highHz, rate, 4000), std::exp(lo)));
            if (!initialized) { logLow = lo; logHigh = hi; }
            else { logLow += step * (lo - logLow); logHigh += step * (hi - logHigh); }
            low.configure(std::exp(logLow), rate);
            upper.configure(std::exp(logHigh), rate);
            alignment.configure(std::exp(logHigh), rate);
            initialized = true;
        }
        position = (position + 1) % 32;
        Bands out{};
        const std::array<float, 2> input{left, right};
        for (size_t ch = 0; ch < 2; ++ch) {
            const double x = std::isfinite(input[ch]) ? input[ch] : 0;
            const auto a = low.process(x, ch);
            if (count == 2) { out[0][ch] = float(a[0]); out[1][ch] = float(a[1]); }
            else {
                const auto b = upper.process(a[1], ch);
                const auto ap = alignment.process(a[0], ch);
                out[0][ch] = float(ap[0] + ap[1]);
                out[1][ch] = float(b[0]); out[2][ch] = float(b[1]);
            }
        }
        return out;
    }
private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> z1{}, z2{};
        double process(double x, size_t ch) noexcept {
            const double y = b0*x + z1[ch];
            z1[ch] = b1*x - a1*y + z2[ch]; z2[ch] = b2*x - a2*y;
            if (!std::isfinite(y) || !std::isfinite(z1[ch]) || !std::isfinite(z2[ch])) {
                z1[ch] = z2[ch] = 0; return 0;
            }
            if (std::abs(z1[ch]) < 1e-30) z1[ch] = 0;
            if (std::abs(z2[ch]) < 1e-30) z2[ch] = 0;
            return y;
        }
    };
    struct Split {
        std::array<Biquad, 2> lp{}, hp{};
        void configure(double hz, double sr) noexcept {
            const double w = 6.2831853071795864769 * hz / sr;
            const double c = std::cos(w), alpha = std::sin(w) / std::sqrt(2.0), a0 = 1 + alpha;
            for (auto* stages : {&lp, &hp}) for (auto& s : *stages) {
                const bool high = stages == &hp;
                s.b0 = (1 + (high ? c : -c)) / (2*a0);
                s.b1 = (high ? -2 : 2)*s.b0; s.b2 = s.b0;
                s.a1 = -2*c/a0; s.a2 = (1-alpha)/a0;
            }
        }
        std::array<double, 2> process(double x, size_t ch) noexcept {
            double l = x, h = x;
            for (auto& s : lp) l = s.process(l, ch);
            for (auto& s : hp) h = s.process(h, ch);
            return {l,h};
        }
    };
    Split low, upper, alignment;
    double rate = 48000, step = .026, logLow = 0, logHigh = 0;
    int position = 0, count = 3;
    bool initialized = false;
};

} // namespace vocalstack
