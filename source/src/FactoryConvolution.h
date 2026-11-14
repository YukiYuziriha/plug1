#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace vocalstack {

// Original synthetic spaces, not recordings of named venues or physical plates.
// IR transforms, synthesis and storage allocation happen in prepare().
// A direct 64-tap head and delayed non-uniform partitions give zero integer
// latency. No resources are replaced or destroyed while processing automation.
class FactoryConvolution {
    using Complex = std::complex<float>;
    struct FFT {
        size_t n = 0;
        std::vector<size_t> reverse;
        std::vector<Complex> roots;
        void prepare(size_t size) {
            n = size; reverse.resize(n); roots.resize(n / 2);
            for (size_t i = 0; i < n; ++i) {
                size_t x = i, r = 0;
                for (size_t j = n; j > 1; j >>= 1) { r = (r << 1) | (x & 1); x >>= 1; }
                reverse[i] = r;
            }
            for (size_t i = 0; i < roots.size(); ++i) {
                const double angle = -6.283185307179586 * double(i) / double(n);
                roots[i] = {float(std::cos(angle)), float(std::sin(angle))};
            }
        }
        void reorder(Complex* x) const noexcept {
            for (size_t i = 0; i < n; ++i) if (i < reverse[i]) std::swap(x[i], x[reverse[i]]);
        }
        void pass(Complex* x, size_t width, bool inverse) const noexcept {
            const size_t half = width / 2, stride = n / width;
            for (size_t base = 0; base < n; base += width)
                for (size_t j = 0; j < half; ++j) {
                    const auto w = inverse ? std::conj(roots[j * stride]) : roots[j * stride];
                    const auto a = x[base + j], z = x[base + j + half];
                    const Complex b{z.real() * w.real() - z.imag() * w.imag(), z.real() * w.imag() + z.imag() * w.real()};
                    x[base + j] = a + b; x[base + j + half] = a - b;
                }
        }
        void run(Complex* x, bool inverse = false) const noexcept {
            reorder(x);
            for (size_t width = 2; width <= n; width <<= 1) pass(x, width, inverse);
            if (inverse) for (size_t i = 0; i < n; ++i) x[i] /= float(n);
        }
    };
    struct Tier {
        size_t block = 0, offset = 0, parts = 0, fill = 0, cursor = 0;
        FFT fft;
        std::array<size_t, 4> usedParts{};
        std::array<float, 4> taskWeights{};
        size_t taskPart = 0;
        uint64_t taskStart = 0;
        float taskTail = 1;
        double taskRate = 48000;
        bool taskActive = false;
        int phase = 0;
        size_t fftWidth = 2;
        std::array<std::vector<Complex>, 2> input, work, sum;
        std::array<std::array<std::vector<Complex>, 2>, 4> spectra;
        void prepare(size_t p, size_t start, size_t end,
                     const std::array<std::array<std::vector<float>, 2>, 4>& ir) {
            block = p; offset = start; parts = end <= start ? 0 : (end - start + p - 1) / p;
            fft.prepare(p * 2);
            for (int s = 0; s < 4; ++s)
                usedParts[s] = ir[s][0].size() <= start ? 0 : std::min(parts, (ir[s][0].size() - start + p - 1) / p);
            for (int c = 0; c < 2; ++c) {
                input[c].assign(parts * p * 2, {}); work[c].assign(p * 2, {}); sum[c].assign(p * 2, {});
                for (int s = 0; s < 4; ++s) {
                    auto& h = spectra[s][c]; h.assign(usedParts[s] * p * 2, {});
                    for (size_t k = 0; k < usedParts[s]; ++k) {
                        auto* chunk = h.data() + k * p * 2;
                        for (size_t i = 0; i < p; ++i) {
                            const size_t t = start + k * p + i;
                            if (t < end && t < ir[s][c].size()) chunk[i] = ir[s][c][t];
                        }
                        fft.run(chunk);
                    }
                }
            }
            reset();
        }
        void reset() noexcept {
            fill = cursor = 0; taskActive = false; taskPart = 0;
            for (int c = 0; c < 2; ++c) {
                std::fill(input[c].begin(), input[c].end(), Complex{});
                std::fill(work[c].begin(), work[c].end(), Complex{});
            }
        }
        static void multiplyAdd(Complex* dst, const Complex* a, const Complex* b, float gain, size_t size) noexcept {
#if defined(_M_X64) || defined(__SSE2__)
            auto* d = reinterpret_cast<float*>(dst);
            const auto* x = reinterpret_cast<const float*>(a);
            const auto* h = reinterpret_cast<const float*>(b);
            const auto g = _mm_set1_ps(gain);
            const auto signs = _mm_set_ps(1, -1, 1, -1);
            for (size_t i = 0; i < size * 2; i += 4) {
                const auto av = _mm_loadu_ps(x + i), bv = _mm_loadu_ps(h + i);
                const auto real = _mm_shuffle_ps(av, av, _MM_SHUFFLE(2, 2, 0, 0));
                const auto imag = _mm_shuffle_ps(av, av, _MM_SHUFFLE(3, 3, 1, 1));
                const auto swap = _mm_shuffle_ps(bv, bv, _MM_SHUFFLE(2, 3, 0, 1));
                const auto product = _mm_add_ps(_mm_mul_ps(real, bv), _mm_mul_ps(_mm_mul_ps(imag, swap), signs));
                _mm_storeu_ps(d + i, _mm_add_ps(_mm_loadu_ps(d + i), _mm_mul_ps(product, g)));
            }
#else
            for (size_t i = 0; i < size; ++i) {
                const float ar = a[i].real(), ai = a[i].imag(), br = b[i].real(), bi = b[i].imag();
                dst[i] = {dst[i].real() + (ar * br - ai * bi) * gain,
                          dst[i].imag() + (ar * bi + ai * br) * gain};
            }
#endif
        }
        void accumulate(size_t begin, size_t end) noexcept {
            for (size_t k = begin; k < end; ++k) {
                const float taper = std::exp(-float((offset + k * block + block * .5) / taskRate) * (1.f / taskTail - 1.f) * 4.f);
                for (int s = 0; s < 4; ++s) {
                    const float gain = taskWeights[s] * taper;
                    if (gain < 1.e-7f || k >= usedParts[s]) continue;
                    for (int c = 0; c < 2; ++c) {
                        const auto* history = input[c].data() + ((cursor + parts - k) % parts) * block * 2;
                        const auto* h = spectra[s][c].data() + k * block * 2;
                        multiplyAdd(sum[c].data(), history, h, gain, block * 2);
                    }
                }
            }
        }
        void finish(std::array<std::vector<float>, 2>& pending) noexcept {
            for (int c = 0; c < 2; ++c) {
                auto& y = sum[c];
                if (block < 4096) fft.run(y.data(), true);
                else for (auto& v : y) v /= float(block * 2);
                for (size_t i = 0; i < block * 2; ++i)
                    pending[c][size_t((taskStart + i) % pending[c].size())] += y[i].real();
            }
            cursor = (cursor + 1) % parts; taskActive = false;
        }
        void process(const std::array<float, 2>& x, uint64_t tick,
                     const std::array<float, 4>& weights, float tail, double rate,
                     std::array<std::vector<float>, 2>& pending) noexcept {
            if (!parts) return;
            // Long-tier work is distributed over its guaranteed pre-output slack,
            // rather than doing every partition MAC at a large block boundary.
            if (taskActive && (fill & 31u) == 0) {
                if (phase == 1) {
                    for (int c = 0; c < 2; ++c) fft.pass(input[c].data() + cursor * block * 2, fftWidth, false);
                    fftWidth <<= 1;
                    if (fftWidth > block * 2) phase = 2;
                } else if (phase == 3) {
                    for (int c = 0; c < 2; ++c) fft.reorder(sum[c].data());
                    fftWidth = 2; phase = 4;
                } else if (phase == 4) {
                    for (int c = 0; c < 2; ++c) fft.pass(sum[c].data(), fftWidth, true);
                    fftWidth <<= 1;
                    if (fftWidth > block * 2) phase = 5;
                } else if (phase == 5 || taskPart == parts) {
                    if (block >= 4096 && phase != 5) phase = 3;
                    else finish(pending);
                } else {
                    size_t stages = 0; for (size_t n = block * 2; n > 1; n >>= 1) ++stages;
                    const size_t groups = block / 32 - (block >= 4096 ? 2 * stages + 5 : 2);
                    const size_t budget = (parts + groups - 1) / groups;
                    const size_t end = std::min(parts, taskPart + budget);
                    accumulate(taskPart, end); taskPart = end;
                }
            }
            for (int c = 0; c < 2; ++c) work[c][fill] = x[c];
            if (++fill != block) return;
            taskWeights = weights; taskTail = tail; taskRate = rate;
            taskStart = tick + 1 - block + offset; taskPart = 0;
            for (int c = 0; c < 2; ++c) {
                auto& w = work[c]; std::fill(w.begin() + block, w.end(), Complex{});
                auto* destination = input[c].data() + cursor * block * 2;
                if (block >= 4096) {
                    std::copy(w.begin(), w.end(), destination);
                    fft.reorder(destination);
                } else {
                    fft.run(w.data());
                    std::copy(w.begin(), w.end(), destination);
                }
                std::fill(sum[c].begin(), sum[c].end(), Complex{});
                std::fill(w.begin(), w.end(), Complex{});
            }
            fill = 0;
            if (offset >= 2 * block) { taskActive = true; phase = block >= 4096 ? 1 : 2; fftWidth = 2; }
            else { accumulate(0, parts); finish(pending); }
        }
    };
public:
    static constexpr std::array<const char*, 4> names{"Church", "Hall", "Room", "Plate"};
    static constexpr std::array<double, 4> durations{3.2, 2.1, .65, 1.5};
    static constexpr int latencySamples = 0;
    // Deterministic synthesis seam shared by offline tests. No borrowed assets.
    static std::array<std::vector<float>, 2> makeImpulse(double rate, int space) {
        space = std::clamp(space, 0, 3);
        const size_t length = size_t(std::ceil(rate * durations[size_t(space)]));
        std::array<std::vector<float>, 2> ir;
        for (int c = 0; c < 2; ++c) {
            auto& h = ir[c]; h.resize(length);
            uint32_t seed = 0x9e3779b9u + uint32_t(space * 7187 + c * 10937);
            double low = 0, energy = 0;
            const double cutoff = space == 0 ? 3400 : space == 1 ? 5100 : space == 2 ? 6900 : 9500;
            const double pole = std::exp(-6.283185307179586 * std::min(cutoff, rate * .4) / rate);
            const double onset = space == 0 ? .024 : space == 1 ? .012 : space == 2 ? .002 : .001;
            for (size_t i = 0; i < length; ++i) {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                const double noise = double(seed) / 2147483648.0 - 1;
                low = (1 - pole) * noise + pole * low;
                const double t = double(i) / rate;
                const double rise = std::clamp((t - onset) / (space == 0 ? .12 : space == 1 ? .06 : .015), 0.0, 1.0);
                const double envelope = std::exp(-8.0 * t / durations[size_t(space)]);
                const double modal = space == 3 ? .2 * std::sin(6.283185307179586 * (347 + c * 29) * t) * std::exp(-t * 8) : 0;
                h[i] = float((low + modal) * rise * envelope);
            }
            // Different geometries, not merely different decay constants/seeds.
            for (int r = 0; r < (space == 3 ? 2 : 9); ++r) {
                const double time = onset + (r + 1) * (space == 0 ? .0191 : space == 1 ? .0113 : .0047)
                    * (1 + c * .031 + r * .017);
                const size_t at = size_t(time * rate);
                if (at < length) h[at] += float((r % 3 == 0 ? -.8 : .7) * std::exp(-r * .25));
            }
            for (float v : h) energy += double(v) * v;
            const float gain = float(.7 / std::sqrt(std::max(energy, 1.e-20)));
            for (auto& v : h) v *= gain;
        }
        return ir;
    }
    void prepare(double sampleRate) {
        rate = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000.0, 384000.0) : 48000;
        std::array<std::array<std::vector<float>, 2>, 4> ir;
        for (int s = 0; s < 4; ++s) {
            ir[s] = makeImpulse(rate, s);
            for (int c = 0; c < 2; ++c) {
                            std::copy_n(ir[s][c].begin(), 64, head[s][c].begin());
                            headActive[s][c] = std::any_of(head[s][c].begin(), head[s][c].end(), [](float x) { return x != 0; });
                        }
        }
        const size_t end = ir[0][0].size();
        tiers[0].prepare(64, 64, 1024, ir);
        tiers[1].prepare(512, 1024, 8192, ir);
        tiers[2].prepare(4096, 8192, std::min(size_t(32768), end), ir);
        tiers[3].prepare(16384, 32768, end, ir);
        for (auto& p : pending) p.assign(131072, 0);
        for (auto& p : pre) p.assign(size_t(rate * .251) + 4, 0);
        step = float(-std::expm1(-1.0 / (rate * .04)));
        toneStep = float(1 - std::exp(-6.283185307179586 * 1000 / rate));
        attack = float(-std::expm1(-1.0 / (rate * .005)));
        release = float(-std::expm1(-1.0 / (rate * .3)));
        reset();
    }
    void reset() noexcept {
        for (auto& t : tiers) t.reset();
        for (auto& p : pending) std::fill(p.begin(), p.end(), 0.f);
        for (auto& p : pre) std::fill(p.begin(), p.end(), 0.f);
        history = {}; low = {}; weights = {}; tick = preHead = 0;
        initialized = false; envelope = 0; brightness = predelay = output = 0; tail = 1;
    }
    std::array<float, 2> process(float l, float r, int space, float tiltDb,
                               float preMs, float tailPercent, float duckDb, float outDb) noexcept {
        if (pending[0].empty()) return {};
        space = std::clamp(space, 0, 3);
        if (!initialized) { weights[size_t(space)] = 1; brightness = tiltDb; predelay = preMs; tail = tailPercent * .01f; output = outDb; initialized = true; }
        for (int s = 0; s < 4; ++s) {
            weights[s] += step * ((s == space ? 1.f : 0.f) - weights[s]);
            if (s != space && weights[s] < 1.e-7f) weights[s] = 0;
        }
        brightness += step * (tiltDb - brightness); predelay += step * (preMs - predelay);
        tail += step * (tailPercent * .01f - tail); output += step * (outDb - output);
        const std::array<float, 2> in{l, r};
        std::array<float, 2> delayed{}, wet{};
        for (int c = 0; c < 2; ++c) {
            pre[c][preHead] = in[c];
            double p = double(preHead) - predelay * .001 * rate;
            if (p < 0) p += pre[c].size();
            const size_t a = size_t(p) % pre[c].size(), b = (a + 1) % pre[c].size();
            delayed[c] = pre[c][a] + float(p - std::floor(p)) * (pre[c][b] - pre[c][a]);
            history[c][size_t(tick % 64)] = delayed[c];
        }
        preHead = (preHead + 1) % pre[0].size();
        for (auto& t : tiers) t.process(delayed, tick, weights, std::clamp(tail, .25f, 1.f), rate, pending);
        const float peak = std::max(std::abs(l), std::abs(r));
        const float envStep = peak > envelope ? attack : release;
        envelope += envStep * (peak - envelope);
        const float gain = std::pow(10.f, (output - duckDb * std::clamp(envelope * 4.f, 0.f, 1.f)) / 20.f);
        const float bass = std::pow(10.f, -brightness / 40.f), treble = 1.f / bass;
        for (int c = 0; c < 2; ++c) {
            const size_t at = size_t(tick % pending[c].size());
            float x = pending[c][at]; pending[c][at] = 0;
            for (int s = 0; s < 4; ++s) if (weights[s] > 0 && headActive[s][c])
                for (size_t i = 0; i < 64; ++i)
                    x += history[c][size_t((tick + 64 - i) % 64)] * head[s][c][i] * weights[s];
            low[c] += toneStep * (x - low[c]);
            wet[c] = (bass * low[c] + treble * (x - low[c])) * gain;
        }
        ++tick; return wet;
    }
private:
    double rate = 48000;
    std::array<Tier, 4> tiers;
    std::array<std::array<std::array<float, 64>, 2>, 4> head{};
        std::array<std::array<bool, 2>, 4> headActive{};
    std::array<std::array<float, 64>, 2> history{};
    std::array<std::vector<float>, 2> pending, pre;
    std::array<float, 2> low{};
    std::array<float, 4> weights{};
    uint64_t tick = 0;
    size_t preHead = 0;
    float attack = 0, release = 0;
        float step = 0, toneStep = 0, brightness = 0, predelay = 0, tail = 1, output = 0, envelope = 0;
    bool initialized = false;
};
} // namespace vocalstack
