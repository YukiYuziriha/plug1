// Standalone: compile as C++20 with source/src on the include path. No JUCE.
#include "Equalizer.h"
#include "Waveshaper.h"

#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
bool countAllocations = false;
size_t allocations = 0;
}
void* operator new(std::size_t size) {
    if (countAllocations) ++allocations;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using namespace vocalstack;
constexpr double pi = 3.14159265358979323846;
constexpr std::array rates{44100, 48000, 96000, 192000};

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    expect(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
float signal(int sample, int rate) {
    return static_cast<float>(0.18 * std::sin(2 * pi * 237 * sample / rate)
        + 0.07 * std::cos(2 * pi * 1379 * sample / rate));
}

template<class Effect, class Settings>
void contracts(int rate, const Settings& settings) {
    Effect effect, fresh;
    effect.prepare(rate);
    fresh.prepare(rate);
    for (int i = 0; i < rate / 10; ++i) {
        float l = 0, r = 0;
        effect.process(l, r, settings);
        expect(l == 0 && r == 0, "Prepared silence must remain silent");
    }
    effect.reset();
    for (int i = 0; i < rate / 10; ++i) {
        float l = signal(i, rate), r = l;
        effect.process(l, r, settings);
        expect(l == r, "Mono channels diverged");
    }
    effect.reset();
    for (int i = 0; i < rate / 10; ++i) {
        float l = signal(i, rate), r = 0, a = l, b = r;
        effect.process(l, r, settings);
        fresh.process(a, b, settings);
        expect(l == a && r == b, "Reset does not match fresh state");
        expect(r == 0, "Stereo channels share history");
    }
    effect.prepare(96000);
    effect.prepare(rate);
    fresh.reset();
    for (int i = 0; i < 500; ++i) {
        float l = signal(i, rate), r = -l, a = l, b = r;
        effect.process(l, r, settings);
        fresh.process(a, b, settings);
        expect(l == a && r == b, "Reprepare retained state");
    }
    effect.reset();
    for (int i = 0; i < 500; ++i) {
        float l = 0, r = 0;
        effect.process(l, r, settings);
        expect(l == 0 && r == 0, "Reset retained a tail");
    }
    auto bypass = settings;
    bypass.enabled = false;
    for (int i = 0; i < rate; ++i) {
        float l = signal(i, rate), r = -l;
        const float dry = l;
        effect.process(l, r, bypass);
        if (i > rate * 3 / 4) expect(l == dry && r == -dry, "Settled bypass is not exact");
    }
    effect.reset();
    float l = 0.25f, r = -0.125f;
    effect.process(l, r, bypass);
    expect(l == 0.25f && r == -0.125f, "Initial bypass is not direct");
    expect(Effect::latencySamples == 0, "Unexpected explicit latency");
}

void eqResponses(int rate) {
    EqualizerSettings s;
    for (int i = 0; i < 100; ++i) {
        float l = signal(i, rate), r = -l;
        const float dry = l;
        Equalizer eq;
        eq.prepare(rate);
        eq.process(l, r, s);
        expect(l == dry && r == -dry, "Default EQ must be unity");
    }
    s.bands[0] = {true, 1000, 12, 0.707f, 0};
    near(Equalizer::frequencyResponse(1000, rate, s), std::pow(10.0, 12.0 / 20), 1.0e-7,
         "Bell center boost wrong");
    s.bands[0].gainDb = -12;
    near(Equalizer::frequencyResponse(1000, rate, s), std::pow(10.0, -12.0 / 20), 1.0e-7,
         "Bell center cut wrong");
    s.shiftSemitones = 12;
    near(Equalizer::frequencyResponse(2000, rate, s), std::pow(10.0, -12.0 / 20), 1.0e-7,
         "Semitone shift wrong");
    s.shiftSemitones = 0;
    const double ordinary = Equalizer::frequencyResponse(1500, rate, s);
    s.adaptiveQ = true;
    expect(Equalizer::frequencyResponse(1500, rate, s) > ordinary, "Adaptive Q should narrow bell");
    s.adaptiveQ = false;
    for (int mode = 1; mode <= 4; ++mode) {
        s.bands[0].mode = mode;
        s.bands[0].gainDb = 12;
        const double low = Equalizer::frequencyResponse(0, rate, s);
        const double high = Equalizer::frequencyResponse(rate * 0.5, rate, s);
        near(mode == 1 || mode == 3 ? low : high, std::pow(10.0, 0.6), 1.0e-7,
             "Shelf endpoint gain wrong");
        near(mode == 1 || mode == 3 ? high : low, 1, 1.0e-7, "Shelf passband wrong");
        for (double f : {100.0, 700.0, 1000.0, 4000.0}) {
            const auto boost = Equalizer::responseComplex(f, rate, s);
            s.bands[0].gainDb = -12;
            const auto cut = Equalizer::responseComplex(f, rate, s);
            near(std::abs(boost * cut - 1.0), 0, 1.0e-7, "Shelf boost/cut not reciprocal");
            s.bands[0].gainDb = 12;
        }
    }
    for (int mode = 5; mode <= 12; ++mode) {
        s.bands[0].mode = mode;
        const bool lowCut = mode <= 8;
        near(Equalizer::frequencyResponse(lowCut ? 0 : rate * 0.5, rate, s), 0, 1.0e-8,
             "Cut stopband endpoint wrong");
        near(Equalizer::frequencyResponse(lowCut ? rate * 0.5 : 0, rate, s), 1, 1.0e-7,
             "Cut passband endpoint wrong");
        near(Equalizer::frequencyResponse(1000, rate, s), std::sqrt(0.5), 0.0002,
             "Butterworth cutoff wrong");
    }
    s.bands[0].mode = 13;
    near(Equalizer::frequencyResponse(1000, rate, s), 0, 1.0e-8, "Notch misses center");
    s.bands[0].enabled = false;
    s.gainDb = 6;
    near(Equalizer::frequencyResponse(1000, rate, s), std::pow(10.0, 0.3), 1.0e-8,
         "Global gain wrong");
    s.enabled = false;
    near(Equalizer::frequencyResponse(1000, rate, s), 1, 0, "Bypass response wrong");
}

// Complex lock-in on an integer-cycle sine verifies magnitude AND phase of the
// actual sample processor against the public coefficient-based response API.
void eqMeasured(int rate) {
    constexpr int length = 8192;
    for (int mode = 0; mode < 14; ++mode) {
        for (int bin : {32, 128, 512}) {
            const double frequency = static_cast<double>(rate) * bin / length;
            EqualizerSettings s;
            s.bands[0] = {true, 1100, 9, 0.9f, mode};
            s.bands[1] = {true, 3000, -3, 1.2f, 0};
            s.gainDb = -2;
            Equalizer eq;
            eq.prepare(rate);
            std::complex<double> input{}, output{};
            for (int i = -rate / 2; i < length; ++i) {
                const double phase = 2 * pi * frequency * i / rate;
                float l = static_cast<float>(0.1 * std::sin(phase)), r = 0;
                const float dry = l;
                eq.process(l, r, s);
                expect(r == 0, "EQ stereo crosstalk");
                if (i >= 0) {
                    const auto z = std::polar(1.0, -phase);
                    input += static_cast<double>(dry) * z;
                    output += static_cast<double>(l) * z;
                }
            }
            const auto predicted = Equalizer::responseComplex(frequency, rate, s);
            near(std::abs(output / input - predicted), 0, 2.0e-5,
                 "Measured EQ complex response disagrees with API");
            near(std::abs(eq.responseComplex(frequency) - predicted), 0, 1.0e-10,
                 "Instance response disagrees with settled response");
        }
    }
}

// Harmonics of an exact-bin sine; no window or spectral leakage required.
std::array<double, 4> shaperSpectrum(int shape, float drive, int rate) {
    Waveshaper effect;
    effect.prepare(rate);
    WaveshaperSettings s;
    s.shape = shape;
    s.driveDb = drive;
    constexpr int length = 8192, bin = 37;
    std::array<std::complex<double>, 4> spectrum{};
    for (int i = -length; i < length; ++i) {
        const double phase = 2 * pi * bin * i / length;
        float l = static_cast<float>(0.4 * std::sin(phase)), r = l;
        effect.process(l, r, s);
        expect(l == r, "Waveshaper mono mismatch");
        if (i >= 0)
            for (size_t harmonic = 0; harmonic < spectrum.size(); ++harmonic)
                spectrum[harmonic] += static_cast<double>(l) * std::polar(1.0, -phase * harmonic);
    }
    std::array<double, 4> result{};
    for (size_t i = 0; i < result.size(); ++i) result[i] = std::abs(spectrum[i]) / length;
    return result;
}

void shaperCurves(int rate) {
    Waveshaper antialias;
    antialias.prepare(rate);
    WaveshaperSettings hardSettings;
    hardSettings.shape = 1;
    float stepL = 2, stepR = -2;
    antialias.process(stepL, stepR, hardSettings);
    near(stepL, 0.75, 1.0e-7, "Hard ADAA must integrate across the clip knee");
    near(stepR, -0.75, 1.0e-7, "Negative ADAA clip knee wrong");
    stepL = -2; stepR = 2;
    antialias.process(stepL, stepR, hardSettings);
    near(stepL, 0, 1.0e-7, "ADAA opposite endpoints should average to zero");
    near(stepR, 0, 1.0e-7, "ADAA stereo histories incorrect");
    for (int shape = 0; shape < 3; ++shape) {
        Waveshaper effect;
        effect.prepare(rate);
        WaveshaperSettings s;
        s.shape = shape;
        float l = 0.25f, r = -0.25f;
        for (int i = 0; i < 10; ++i) {
            l = 0.25f; r = -0.25f;
            effect.process(l, r, s);
        }
        const double bias = std::tanh(0.5);
        const double expected = shape == 0 ? std::tanh(0.25) : shape == 1 ? 0.25
            : (std::tanh(0.75) - bias) / (1 - bias * bias);
        near(l, expected, 1.0e-7, "Static shaping curve wrong");
        const auto clean = shaperSpectrum(shape, 0, rate);
        const auto driven = shaperSpectrum(shape, 18, rate);
        expect(driven[1] > clean[1] * 1.5, "Drive is normalized away");
        expect(driven[3] > clean[3] + 0.01, "Drive did not add harmonics");
        if (shape == 2) expect(driven[2] > 0.01, "Asymmetric curve lacks even harmonics");
        else near(driven[2], 0, 1.0e-7, "Symmetric curve generates even harmonics");
    }
    const auto soft = shaperSpectrum(0, 18, rate), hard = shaperSpectrum(1, 18, rate);
    expect(hard[3] > soft[3], "Hard clipping should generate stronger third harmonic");

    Waveshaper dry, wet, half;
    dry.prepare(rate); wet.prepare(rate); half.prepare(rate);
    WaveshaperSettings s;
    s.driveDb = 12;
    s.outputDb = -6;
    for (int i = 0; i < 1000; ++i) {
        float a = signal(i, rate), b = a, c = a, ar = -a, br = -a, cr = -a;
        const float original = a;
        s.mix = 0; dry.process(a, ar, s);
        s.mix = 1; wet.process(b, br, s);
        s.mix = 0.5f; half.process(c, cr, s);
        expect(a == original && ar == -original, "Zero mix is not exact dry");
        near(c, 0.5 * (a + b), 1.0e-7, "Mix is not true dry/wet blend");
        near(cr, 0.5 * (ar + br), 1.0e-7, "Stereo mix is not true dry/wet blend");
    }
    Waveshaper trimA, trimB;
    trimA.prepare(rate); trimB.prepare(rate);
    for (int i = 0; i < 500; ++i) {
        float l = signal(i, rate), r = l, a = l, b = r;
        s.mix = 1; s.outputDb = 0; trimA.process(l, r, s);
        s.outputDb = -6; trimB.process(a, b, s);
        near(a, l * std::pow(10.0, -0.3), 1.0e-7, "Output trim incorrect");
    }
}

void automationAndGuards(int rate) {
    Equalizer eq;
    Waveshaper shaper;
    eq.prepare(rate); shaper.prepare(rate);
    EqualizerSettings e;
    WaveshaperSettings s;
    // Irregular call groups are just caller block boundaries; neither effect
    // observes them, so compare against an uninterrupted sample sequence.
    Equalizer referenceEq;
    Waveshaper referenceShaper;
    referenceEq.prepare(rate); referenceShaper.prepare(rate);
    constexpr std::array blocks{1, 17, 64, 3, 251, 32, 511};
    int sample = 0;
    allocations = 0;
    countAllocations = true;
    bool valid = true, identical = true;
    for (int block = 0; block < 300; ++block) {
        for (int j = 0; j < blocks[static_cast<size_t>(block) % blocks.size()]; ++j, ++sample) {
            if (sample % 127 == 0) {
                for (size_t b = 0; b < e.bands.size(); ++b) {
                    e.bands[b] = {true, sample % 254 == 0 ? 10.0f : static_cast<float>(rate),
                        sample % 254 == 0 ? -24.0f : 24.0f, sample % 254 == 0 ? 0.1f : 20.0f,
                        (sample / 127 + static_cast<int>(b)) % 14};
                }
                e.enabled = sample % 381 != 0;
                e.shiftSemitones = sample % 254 == 0 ? -48.0f : 48.0f;
                e.adaptiveQ = sample % 254 != 0;
                s.enabled = e.enabled;
                s.shape = (sample / 127) % 3;
                s.driveDb = sample % 254 == 0 ? -24.0f : 48.0f;
                s.outputDb = sample % 254 == 0 ? -60.0f : 24.0f;
                s.mix = sample % 254 == 0 ? 0.0f : 1.0f;
            }
            float l = signal(sample, rate), r = -l, a = l, b = r;
            eq.process(l, r, e); referenceEq.process(a, b, e);
            valid = valid && std::isfinite(l) && std::isfinite(r);
            identical = identical && l == a && r == b;
            l = signal(sample, rate); r = -l; a = l; b = r;
            shaper.process(l, r, s); referenceShaper.process(a, b, s);
            valid = valid && std::isfinite(l) && std::isfinite(r);
            identical = identical && l == a && r == b;
        }
    }
    countAllocations = false;
    expect(allocations == 0, "Process allocated memory");
    expect(valid, "Extreme automation produced nonfinite audio");
    expect(identical, "Sample processing depends on caller grouping");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    s = {true, nan, inf, nan, 999};
    e.gainDb = inf; e.shiftSemitones = nan;
    for (auto& band : e.bands) band = {true, nan, inf, nan, -999};
    for (int i = 0; i < 5000; ++i) {
        float l = i % 2 ? nan : 0.1f, r = inf;
        shaper.process(l, r, s);
        eq.process(l, r, e);
        expect(std::isfinite(l) && std::isfinite(r), "Nonfinite settings/input escaped guards");
    }
    expect(std::isfinite(Equalizer::frequencyResponse(nan, nan, e)), "Nonfinite response API");
    eq.prepare(nan); shaper.prepare(inf);
    float l = 0.1f, r = l;
    eq.process(l, r, e); shaper.process(l, r, s);
    expect(std::isfinite(l) && std::isfinite(r), "Invalid prepare rate escaped guards");
}

void smoothing(int rate) {
    Equalizer eq;
    Waveshaper shaper;
    eq.prepare(rate); shaper.prepare(rate);
    EqualizerSettings e;
    WaveshaperSettings s;
    s.shape = 1;
    float lastEq = 0, lastShaper = 0;
    for (int i = 0; i < rate; ++i) {
        if (i == rate / 4) { e.gainDb = 18; s.driveDb = 18; s.outputDb = -6; s.shape = 2; }
        if (i == rate / 2) { e.enabled = false; s.enabled = false; }
        float l = 0.1f, r = l, a = l, b = r;
        eq.process(l, r, e); shaper.process(a, b, s);
        if (i > 100) {
            expect(std::abs(l - lastEq) < 0.002f, "EQ trim/bypass automation clicked");
            expect(std::abs(a - lastShaper) < 0.002f, "Shaper automation clicked");
        }
        lastEq = l; lastShaper = a;
    }
    // Frequency sweeps through a resonant filter, including changes in mode,
    // are bounded and finite under the dual-bank transition scheme.
    eq.reset();
    e = {};
    double peak = 0;
    for (int i = 0; i < rate; ++i) {
        e.bands[0] = {true, static_cast<float>(20 * std::pow(500.0, double(i) / rate)),
                     12, 4, i < rate / 2 ? 0 : 13};
        float l = signal(i, rate), r = -l;
        eq.process(l, r, e);
        peak = std::max(peak, std::abs(static_cast<double>(l)));
        expect(std::isfinite(l) && std::isfinite(r), "Sweep became nonfinite");
    }
    expect(peak < 2, "Sweep produced an excessive transient");
}
} // namespace

int main() {
    try {
        for (int rate : rates) {
            WaveshaperSettings shaper;
            shaper.driveDb = 12;
            contracts<Waveshaper>(rate, shaper);
            EqualizerSettings eq;
            eq.bands[0] = {true, 900, 6, 1.2f, 0};
            contracts<Equalizer>(rate, eq);
            eqResponses(rate);
            eqMeasured(rate);
            shaperCurves(rate);
            automationAndGuards(rate);
            smoothing(rate);
            std::cout << "Tone effects passed at " << rate << " Hz\n";
        }
        return 0;
    } catch (const std::exception& error) {
        countAllocations = false;
        std::cerr << "Tone effects failure: " << error.what() << '\n';
        return 1;
    }
}
