#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace vocalstack {

struct SpectralMorphSettings {
    float morph = 0, smoothingMs = 40, mix = 1, outputDb = 0;
    int mode = 0, fftSize = 1; // Magnitude / Envelope; 1024 / 2048 / 4096.
    bool freeze = false, enabled = true;
};

struct SpectralMorphParameterDescriptor {
    const char* id; const char* label; const char* unit; float min, max, initial; bool discrete;
};
inline constexpr size_t spectralMorphParameterCount = 6;
inline constexpr std::array<SpectralMorphParameterDescriptor, 6> spectralMorphParameters{{
    {"MorphAmount", "Morph", "%", 0, 100, 0, false},
    {"MorphMode", "Mode", "", 0, 1, 0, true},
    {"MorphFFT", "FFT Size", "", 0, 2, 1, true},
    {"MorphSmoothing", "Smoothing", "ms", 0, 1000, 40, false},
    {"MorphFreeze", "Freeze", "", 0, 1, 0, true},
    {"MorphOutput", "Output", "dB", -24, 12, 0, false}
}};
inline std::array<float, 6> spectralMorphValues(const SpectralMorphSettings& s) noexcept {
    return {s.morph * 100, float(s.mode), float(s.fftSize), s.smoothingMs, s.freeze ? 1.f : 0.f, s.outputDb};
}
inline SpectralMorphSettings spectralMorphFromValues(const std::array<float, 6>& v) noexcept {
    SpectralMorphSettings s; s.morph = v[0] * .01f; s.mode = int(v[1]); s.fftSize = int(v[2]);
    s.smoothingMs = v[3]; s.freeze = v[4] >= .5f; s.outputDb = v[5]; return s;
}

// Audio-owner snapshot. Concurrent consumers must receive an exclusively owned
// copy through a bridge, never read a running processor's plain arrays.
struct SpectralMorphSnapshot {
    static constexpr int bins = 128;
    std::array<float, bins> main{}, donor{}, result{};
    std::uint64_t revision = 0;
    bool donorConnected = false, capturePending = false, frozen = false;
    int fftSize = 2048;
    double sampleRate = 0;
};

// Streaming 4-overlap WOLA. All three resolutions stay warm and are padded to
// 4096 samples, making FFT-size automation a crossfade, not a latency change.
// No donor normalization, phase synthesis, channel summing or callback allocation.
class SpectralMorph {
public:
    static constexpr int latencySamples = 4096;
    static constexpr std::array<int, 3> fftSizes{1024, 2048, 4096};
    SpectralMorph() : storage(std::make_unique<Storage>()) {}
    SpectralMorph(const SpectralMorph&) = delete;
    SpectralMorph& operator=(const SpectralMorph&) = delete;

    void prepare(double sampleRate) {
        if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000)
            throw std::invalid_argument("invalid Spectral Morph sample rate");
        rate = sampleRate;
        slew = float(-std::expm1(-1.0 / (.020 * rate)));
        for (int lane = 0; lane < 3; ++lane) {
            auto& s = storage->lanes[size_t(lane)];
            s.n = fftSizes[size_t(lane)];
            double windowSum = 0;
            for (int i = 0; i < s.n; ++i)
                s.window[size_t(i)] = std::sin(std::numbers::pi_v<float> * (float(i) + .5f) / float(s.n));
            for (int i = 0; i < s.n; ++i) windowSum += s.window[size_t(i)];
            s.amplitudeScale = float(2.0 / windowSum);
            const double logRange = std::log(rate * .5 / 20.0);
            for (int k = 0; k <= s.n / 2; ++k)
                s.displayBucket[size_t(k)] = k == 0 ? 0 : std::clamp(int(std::log((double(k) * rate / s.n) / 20.0) / logRange * (SpectralMorphSnapshot::bins - 1)), 0, SpectralMorphSnapshot::bins - 1);
            for (int i = 0; i < s.n / 2; ++i)
                s.roots[size_t(i)] = std::polar(1.f, -2.f * std::numbers::pi_v<float> * float(i) / float(s.n));
        }
        reset();
    }
    void reset() noexcept {
        for (auto& s : storage->lanes) {
            for (auto& c : s.channels) c = {};
            s.write = s.hop = s.outputRead = s.paddingWrite = 0;
            s.pending = s.held = s.requestedFreeze = false;
            s.release = 0;
        }
        for (auto& d : storage->dry) d.fill(0);
        storage->dryWrite = 0;
        weights = {0, 1, 0};
        initialized = false;
        snapshot = {}; snapshot.sampleRate = rate;
        ++epoch;
    }
    std::array<float, 2> process(std::array<float, 2> main,
                                 std::array<float, 2> donor,
                                 bool donorConnected,
                                 const SpectralMorphSettings& requested) noexcept {
        if (!(rate > 0)) return {};
        const int selected = std::clamp(requested.fftSize, 0, 2);
        const auto safe = [](float x, float low, float high, float fallback) {
            return std::isfinite(x) ? std::clamp(x, low, high) : fallback;
        };
        const float morphTarget = safe(requested.morph, 0, 1, 0);
        const float mixTarget = requested.enabled ? safe(requested.mix, 0, 1, 1) : 0;
        const float gainTarget = std::pow(10.f, safe(requested.outputDb, -24, 12, 0) / 20.f);
        const float smoothing = safe(requested.smoothingMs, 0, 1000, 40);
        if (!initialized) {
            morph = morphTarget; mix = mixTarget; gain = gainTarget;
            envelopeWeight = requested.mode == 1 ? 1.f : 0.f;
            weights = {}; weights[size_t(selected)] = 1;
            initialized = true;
        }
        morph += slew * (morphTarget - morph);
        mix += slew * (mixTarget - mix);
        gain += slew * (gainTarget - gain);
        envelopeWeight += slew * ((requested.mode == 1 ? 1.f : 0.f) - envelopeWeight);
        std::array<float, 2> wet{}, dry{};
        for (int c = 0; c < 2; ++c) {
            main[size_t(c)] = finite(main[size_t(c)]);
            donor[size_t(c)] = donorConnected ? finite(donor[size_t(c)]) : 0.f;
            auto& d = storage->dry[size_t(c)];
            dry[size_t(c)] = d[size_t(storage->dryWrite)];
            d[size_t(storage->dryWrite)] = main[size_t(c)];
        }
        storage->dryWrite = (storage->dryWrite + 1) % latencySamples;
        for (int lane = 0; lane < 3; ++lane) {
            auto& s = storage->lanes[size_t(lane)];
            weights[size_t(lane)] += slew * ((lane == selected ? 1.f : 0.f) - weights[size_t(lane)]);
            if (requested.freeze != s.requestedFreeze) {
                s.requestedFreeze = requested.freeze;
                s.pending = requested.freeze;
                // A new capture request must not silently reactivate an older
                // capture when the donor is unavailable. Retained samples may
                // only finish their existing 20 ms release transition.
                if (requested.freeze) s.held = false;
            }
            for (int c = 0; c < 2; ++c) {
                auto& ch = s.channels[size_t(c)];
                ch.input[size_t(s.write)] = main[size_t(c)];
                ch.donor[size_t(s.write)] = donor[size_t(c)];
                float x = ch.output[size_t(s.outputRead)];
                ch.output[size_t(s.outputRead)] = 0;
                const int padding = latencySamples - s.n;
                if (padding > 0) {
                    const float previous = ch.padding[size_t(s.paddingWrite)];
                    ch.padding[size_t(s.paddingWrite)] = x;
                    x = previous;
                }
                wet[size_t(c)] += weights[size_t(lane)] * x;
            }
            s.write = (s.write + 1) % s.n;
            s.outputRead = (s.outputRead + 1) % s.n;
            const int padding = latencySamples - s.n;
            if (padding > 0) s.paddingWrite = (s.paddingWrite + 1) % padding;
            if (++s.hop == s.n / 4) {
                s.hop = 0;
                analyze(s, smoothing, donorConnected, lane == selected);
            }
        }
        snapshot.donorConnected = donorConnected;
        snapshot.capturePending = storage->lanes[size_t(selected)].pending;
        snapshot.frozen = storage->lanes[size_t(selected)].held
            && storage->lanes[size_t(selected)].requestedFreeze;
        snapshot.fftSize = fftSizes[size_t(selected)];
        for (int c = 0; c < 2; ++c)
            wet[size_t(c)] = finite(dry[size_t(c)] + mix * (wet[size_t(c)] * gain - dry[size_t(c)]));
        return wet;
    }
    SpectralMorphSnapshot audioSnapshot() const noexcept { return snapshot; }

private:
    static constexpr int maxBins = latencySamples / 2 + 1;
    using Samples = std::array<float, latencySamples>;
    using Spectrum = std::array<float, maxBins>;
    struct Channel {
        Samples input{}, donor{}, output{}, padding{};
        Spectrum live{}, held{}, mainEnvelope{}, donorEnvelope{};
        std::array<std::complex<float>, latencySamples> a{}, b{};
    };
    struct Lane {
        int n = 0, write = 0, hop = 0, outputRead = 0, paddingWrite = 0;
        std::array<Channel, 2> channels{};
        Samples window{};
        std::array<int, maxBins> displayBucket{};
        std::array<std::complex<float>, latencySamples / 2> roots{};
        bool pending = false, held = false, requestedFreeze = false;
        float release = 0, amplitudeScale = 0;
    };
    struct Storage { std::array<Lane, 3> lanes{}; std::array<Samples, 2> dry{}; int dryWrite = 0; };
    static float finite(float x) noexcept {
        // Exceptional-value containment, not musical level normalization. The
        // ±120 dBFS bound keeps finite hostile inputs out of FFT overflow.
        return std::isfinite(x) ? std::clamp(x, -1.e6f, 1.e6f) : 0.f;
    }
    static void fft(std::array<std::complex<float>, latencySamples>& data, const Lane& s, bool inverse) noexcept {
        const int n = s.n;
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(data[size_t(i)], data[size_t(j)]);
        }
        for (int length = 2; length <= n; length *= 2) {
            for (int start = 0; start < n; start += length) {
                for (int k = 0; k < length / 2; ++k) {
                    auto root = s.roots[size_t(k * (n / length))];
                    if (inverse) root = std::conj(root);
                    const auto a = data[size_t(start + k)];
                    const auto b = data[size_t(start + k + length / 2)] * root;
                    data[size_t(start + k)] = a + b;
                    data[size_t(start + k + length / 2)] = a - b;
                }
            }
        }
        if (inverse) for (int k = 0; k < n; ++k) data[size_t(k)] /= float(n);
    }
    void analyze(Lane& s, float smoothing, bool donorConnected, bool publish) noexcept {
        const int bins = s.n / 2 + 1;
        const float alpha = smoothing > 0
            ? float(-std::expm1(-double(s.n / 4) / (rate * .001 * smoothing))) : 1.f;
        const bool capture = s.pending && donorConnected;
        if (capture) { s.held = true; s.pending = false; }
        const float holdTarget = s.requestedFreeze && s.held ? 1.f : 0.f;
        s.release += float(-std::expm1(-double(s.n / 4) / (.020 * rate))) * (holdTarget - s.release);
        // ~200 Hz box-smoothed envelopes, bounded at 32 bins either side.
        const int radius = std::clamp(int(std::ceil(200.0 * s.n / rate)), 1, 32);
        if (publish) { snapshot.main.fill(0); snapshot.donor.fill(0); snapshot.result.fill(0); }
        for (auto& ch : s.channels) {
            for (int j = 0; j < s.n; ++j) {
                const int index = (s.write + j) % s.n;
                ch.a[size_t(j)] = ch.input[size_t(index)] * s.window[size_t(j)];
                ch.b[size_t(j)] = ch.donor[size_t(index)] * s.window[size_t(j)];
            }
            fft(ch.a, s, false); fft(ch.b, s, false);
            for (int k = 0; k < bins; ++k) {
                const float magnitude = std::abs(ch.b[size_t(k)]);
                ch.live[size_t(k)] += alpha * (magnitude - ch.live[size_t(k)]);
                if (capture) ch.held[size_t(k)] = ch.live[size_t(k)];
            }
            for (int k = 0; k < bins; ++k) {
                float a = 0, b = 0;
                const int lo = std::max(0, k - radius), hi = std::min(bins - 1, k + radius);
                for (int j = lo; j <= hi; ++j) {
                    a += std::abs(ch.a[size_t(j)]);
                    b += ch.live[size_t(j)] + s.release * (ch.held[size_t(j)] - ch.live[size_t(j)]);
                }
                ch.mainEnvelope[size_t(k)] = a / float(hi - lo + 1);
                ch.donorEnvelope[size_t(k)] = b / float(hi - lo + 1);
            }
            for (int k = 0; k < bins; ++k) {
                const float a = std::abs(ch.a[size_t(k)]);
                const float b = ch.live[size_t(k)] + s.release * (ch.held[size_t(k)] - ch.live[size_t(k)]);
                // Main phase always wins. Main spectral zeros remain zero; the
                // donor cannot synthesize a phase or excite a silent carrier.
                const float envelopeGain = std::clamp(ch.donorEnvelope[size_t(k)]
                    / std::max(1.e-7f, ch.mainEnvelope[size_t(k)]), 0.f, 16.f);
                const float target = b + envelopeWeight * (a * envelopeGain - b);
                const float result = a + morph * (target - a);
                ch.a[size_t(k)] *= a > 1.e-7f ? result / a : 0.f;
                if (k > 0 && k < s.n / 2) ch.a[size_t(s.n - k)] = std::conj(ch.a[size_t(k)]);
                if (publish) {
                    const size_t bucket = size_t(s.displayBucket[size_t(k)]);
                    const float scale = s.amplitudeScale * ((k == 0 || k == s.n / 2) ? .5f : 1.f); // Analysis-window coherent gain; no signal normalization.
                    snapshot.main[bucket] = std::max(snapshot.main[bucket], a * scale);
                    snapshot.donor[bucket] = std::max(snapshot.donor[bucket], b * scale);
                    snapshot.result[bucket] = std::max(snapshot.result[bucket], std::abs(ch.a[size_t(k)]) * scale);
                }
            }
            fft(ch.a, s, true);
            for (int j = 0; j < s.n; ++j)
                ch.output[size_t((s.outputRead + j) % s.n)] += finite(ch.a[size_t(j)].real()) * s.window[size_t(j)] * .5f;
        }
        if (publish) snapshot.revision = ++epoch;
    }
    std::unique_ptr<Storage> storage;
    double rate = 0;
    float slew = 0, morph = 0, mix = 1, gain = 1, envelopeWeight = 0;
    std::array<float, 3> weights{0, 1, 0};
    bool initialized = false;
    std::uint64_t epoch = 0;
    SpectralMorphSnapshot snapshot;
};
} // namespace vocalstack
