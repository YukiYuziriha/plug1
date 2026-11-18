#include "AutomaticTuner.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#ifdef _MSC_VER
#include <malloc.h>
#endif

namespace {
std::atomic<bool> watching{false};
std::atomic<unsigned> allocations{0};
void recordAllocation() { if (watching.load(std::memory_order_relaxed)) ++allocations; }
}
void* operator new(std::size_t n) {
    recordAllocation();
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t alignment) {
    recordAllocation();
#ifdef _MSC_VER
    if (auto* p = _aligned_malloc(n ? n : 1, std::size_t(alignment))) return p;
#else
    if (auto* p = std::aligned_alloc(std::size_t(alignment), ((n + std::size_t(alignment) - 1) / std::size_t(alignment)) * std::size_t(alignment))) return p;
#endif
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept {
#ifdef _MSC_VER
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }

namespace {
using namespace vocalstack;
constexpr double pi = 3.14159265358979323846;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Audio { std::vector<float> left, right; explicit Audio(int n) : left(n), right(n) {} };
Audio tone(double rate, double hz, double seconds = 1.2, float stereo = 1) {
    Audio a{int(rate * seconds)};
    for (int i = 0; i < int(a.left.size()); ++i) {
        a.left[i] = float(0.2 * std::sin(2 * pi * hz * i / rate));
        a.right[i] = stereo * a.left[i];
    }
    return a;
}
void render(AutomaticTuner& tuner, const Audio& in, Audio& out, const TuningSettings& s, int channels = 2, bool irregular = true) {
    const int sizes[] = {1, 7, 31, 64, 257, 3, 1024, 19};
    int offset = 0, index = 0;
    const unsigned before = allocations.load();
    watching = true;
    while (offset < int(in.left.size())) {
        const int n = std::min(int(in.left.size()) - offset, irregular ? sizes[index++ % 8] : 256);
        const float* ins[] = {in.left.data() + offset, in.right.data() + offset};
        float* outs[] = {out.left.data() + offset, out.right.data() + offset};
        tuner.process(ins, outs, n, channels, s);
        offset += n;
    }
    watching = false;
    require(allocations == before, "audio callback allocated");
    for (float value : out.left) require(std::isfinite(value), "nonfinite output");
    for (float value : out.right) require(std::isfinite(value), "nonfinite stereo output");
}
double frequency(const std::vector<float>& samples, double rate) {
    double first = 0, last = 0;
    int crossings = 0;
    for (int i = int(samples.size() / 2) + 1; i < int(samples.size()); ++i) {
        if (samples[i - 1] <= 0 && samples[i] > 0) {
            const double position = i - 1 - samples[i - 1] / double(samples[i] - samples[i - 1]);
            if (crossings++ == 0) first = position;
            last = position;
        }
    }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0;
}
double rms(const std::vector<float>& samples) {
    double power = 0;
    for (size_t i = samples.size() / 2; i < samples.size(); ++i) power += samples[i] * double(samples[i]);
    return std::sqrt(power / (samples.size() - samples.size() / 2));
}
void settingsTests() {
    TuningSettings s;
    s.scale = TuningScale::Major;
    require(tuningNoteMask(s) == 0xab5, "C major mask");
    s.root = 2;
    require(tuningNoteMask(s) == 0xad6, "D major mask");
    s.scale = TuningScale::NaturalMinor; s.root = 0;
    require(tuningNoteMask(s) == 0x5ad, "minor mask");
    require(nearestTuningNote(61.f, 0xab5) == 60, "lower-note tie break");
    require(nearestTuningNote(61.f, 0) == -1, "empty mask");
    s.root = -13; s.amount = std::numeric_limits<float>::quiet_NaN(); s.referenceHz = 1;
    s = sanitiseTuningSettings(s);
    require(s.root == 11 && s.amount == 1 && s.referenceHz == 400, "settings sanitization");
}
void pitchAndLatencyTests() {
    for (double rate : {8000., 22050., 44100., 48000., 88200., 96000., 192000.}) {
        AutomaticTuner tuner;
        tuner.prepare(rate, 257);
        const int expected = rate == 44100 || rate == 48000 ? 1344 : int(std::lround(rate * 1344 / 48000));
        require(tuner.latency() == expected, "legacy latency mismatch");
        TuningSettings s; s.stability = 0;
        auto in = tone(rate, 230., 1.1, -0.7f);
        Audio out{int(in.left.size())};
        render(tuner, in, out, s);
        const auto t = tuner.telemetry();
        std::cout << "rate=" << rate << " latency=" << tuner.latency() << " window=" << tuner.detectorWindowSamples()
                  << " detected=" << t.detectedHz << " confidence=" << t.confidence << " output=" << frequency(out.left, rate) << '\n';
        require(t.voiced && std::abs(t.detectedHz - 230) < 1, "detector frequency/voicing");
        require(std::abs(t.targetMidi - 58) < .01, "chromatic target");
        require(std::abs(frequency(out.left, rate) - 233.08188) < 1.5, "output not pitch corrected");
        require(rms(out.left) > .06 && rms(out.left) < .3, "corrected level/headroom");
        double stereoError = 0;
        for (size_t i = out.left.size() / 2; i < out.left.size(); ++i)
            stereoError = std::max(stereoError, std::abs(double(out.right[i] + .7f * out.left[i])));
        require(stereoError < .003, "linked stereo image drift");
        tuner.reset();
        in = Audio{expected + 4096}; out = Audio{expected + 4096};
        in.left[0] = in.right[0] = 1;
        s.amount = 0;
        render(tuner, in, out, s);
        const int peak = int(std::max_element(out.left.begin(), out.left.end()) - out.left.begin());
        require(peak == expected && std::abs(out.left[peak] - 1) < .01, "impulse audio latency");
    }
}
void rangeReferenceAndScaleTests() {
    AutomaticTuner tuner;
    tuner.prepare(48000, 64);
    for (double hz : {65., 110., 440., 900.}) {
        auto in = tone(48000, hz); Audio out{int(in.left.size())};
        tuner.reset(); render(tuner, in, out, {});
        require(tuner.telemetry().voiced && std::abs(tuner.telemetry().detectedHz - hz) < 1, "vocal range detector");
    }
    TuningSettings s; s.referenceHz = 442;
    auto in = tone(48000, 442); Audio out{int(in.left.size())};
    tuner.reset(); render(tuner, in, out, s, 1);
    require(std::abs(tuner.telemetry().detectedMidi - 69) < .02, "reference A calibration");
    s.referenceHz = 440; s.scale = TuningScale::Major; s.root = 0; s.stability = 0;
    in = tone(48000, 277.18263); out = Audio{int(in.left.size())};
    tuner.reset(); render(tuner, in, out, s);
    require(tuner.telemetry().targetMidi == 60 || tuner.telemetry().targetMidi == 62, "scale target must be allowed");
    s.scale = TuningScale::Custom; s.allowedNotesMask = 1u << 2;
    tuner.reset(); render(tuner, in, out, s);
    require(tuner.telemetry().targetMidi == 62, "custom notes mask");
    std::cout << "custom detected=" << tuner.telemetry().detectedHz << " correction=" << tuner.telemetry().correctionSemitones << " output=" << frequency(out.left, 48000) << '\n';
    require(std::abs(frequency(out.left, 48000) - 293.6648) < 2, "custom correction audio");
    s.allowedNotesMask = 0;
    render(tuner, in, out, s);
    require(!tuner.telemetry().hasTarget && std::abs(tuner.telemetry().correctionSemitones) < .001, "empty notes mask must release");
}
void continuityAndResetTests() {
    AutomaticTuner a, b;
    a.prepare(48000, 1024); b.prepare(48000, 1);
    TuningSettings s;
    auto in = tone(48000, 227, 1.2, -1);
    Audio x{int(in.left.size())}, y{int(in.left.size())};
    render(a, in, x, s, 2, true); render(b, in, y, s, 2, false);
    float error = 0;
    for (size_t i = 0; i < x.left.size(); ++i) error = std::max(error, std::abs(x.left[i] - y.left[i]));
    std::cout << "block partition max difference=" << error << '\n';
    require(error < .002, "block partition changes sound");
    require(a.telemetry().voiced, "anti-phase input detection");
    watching = true; const auto n = allocations.load(); a.reset(); watching = false;
    require(allocations == n, "reset allocated");
    render(a, in, y, s);
    for (size_t i = 0; i < x.left.size(); ++i) require(std::abs(x.left[i] - y.left[i]) < .00001, "reset not reproducible");
    Audio silence{48000}, silentOut{48000}; render(a, silence, silentOut, s);
    require(!a.telemetry().voiced && !a.telemetry().hasTarget && a.telemetry().detectedHz == 0, "stale silence pitch");
    require(rms(silentOut.left) < 1.e-8, "silence tail not cleared");
    a.reset(); render(a, silence, silentOut, s);
    require(rms(silentOut.left) == 0, "reset silence not silent");
    in.left[0] = std::numeric_limits<float>::infinity();
    in.right[3] = std::numeric_limits<float>::quiet_NaN();
    s.amount = std::numeric_limits<float>::quiet_NaN(); s.retuneMs = -100;
    render(a, in, y, s);
}
void automationAndHardRetuneTests() {
    AutomaticTuner tuner; tuner.prepare(48000, 257);
    TuningSettings s; s.stability = 0;
    auto in = tone(48000, 225); Audio out{int(in.left.size())};
    render(tuner, in, out, s);
    const float error = tuner.telemetry().targetMidi - tuner.telemetry().detectedMidi;
    require(std::abs(tuner.telemetry().correctionSemitones - error) < .002, "zero retune not hard");
    // Already voiced, change to a different destination at a control boundary.
    s.scale = TuningScale::Custom; s.allowedNotesMask = 1u << 0;
    tuner.process(in.left.data(), out.left.data(), 64, s);
    require(tuner.telemetry().targetMidi == 60, "mask automation retained disallowed target");
    require(tuner.telemetry().correctionSemitones > 2, "hard mask retune deferred");
    s.amount = 0;
    tuner.process(in.left.data(), out.left.data(), 64, s);
    require(std::abs(tuner.telemetry().correctionSemitones) < .0001, "amount zero is not unity");
    s.amount = .5; render(tuner, in, out, s);
    require(std::abs(tuner.telemetry().correctionSemitones - .5f * (60 - tuner.telemetry().detectedMidi)) < .002, "partial amount");
    tuner.reset(); s.amount = 1; s.allowedNotesMask = 1u << 9; s.retuneMs = 1000;
    render(tuner, in, out, s);
    require(std::abs(tuner.telemetry().correctionSemitones) < .36, "slow retune ignored");
    tuner.reset(); s = {}; s.stability = 1;
    in = tone(48000, 220); out = Audio{int(in.left.size())}; render(tuner, in, out, s);
    in = tone(48000, 227); render(tuner, in, out, s);
    require(tuner.telemetry().targetMidi == 57, "boundary hysteresis failed");
    s.stability = 0; render(tuner, in, out, s);
    require(tuner.telemetry().targetMidi == 58, "zero stability failed to release boundary");
    s.transitionMs = 1000; s.scale = TuningScale::Custom; s.allowedNotesMask = 1u << 0;
    // A disallowed old note switches immediately for safety; test a musical
    // transition with the old destination still allowed instead.
    s.allowedNotesMask = 0xfff;
    in = tone(48000, 260); render(tuner, in, out, s);
    require(tuner.telemetry().targetMidi == 60 && tuner.telemetry().correctionSemitones < -.1, "transition glide ignored");
}
void sweepAndSafetyTests() {
    AutomaticTuner tuner; tuner.prepare(48000, 64);
    TuningSettings s; s.stability = 0;
    double worstCents = 0;
    for (int note = 36; note <= 84; note += 4) {
        for (float offset : {-.35f, .35f}) {
            const double targetHz = 440 * std::exp2((note - 69) / 12.);
            auto in = tone(48000, targetHz * std::exp2(offset / 12), .8);
            Audio out{int(in.left.size())};
            tuner.reset(); render(tuner, in, out, s);
            require(tuner.telemetry().voiced, "sweep unvoiced");
            require(tuner.telemetry().targetMidi == note, "sweep wrong quantization");
            const double cents = std::abs(1200 * std::log2(frequency(out.left, 48000) / targetHz));
            worstCents = std::max(worstCents, cents);
            if (cents >= 12) std::cout << "sweep note=" << note << " offset=" << offset << " error cents=" << cents << '\n';
            require(cents < 12, "pitch sweep exceeds 12 cents");
        }
    }
    std::cout << "26-tone sweep worst output error cents=" << worstCents << '\n';
    Audio noise{48000}, out{48000};
    unsigned random = 1234;
    for (size_t i = 0; i < noise.left.size(); ++i) {
        random = random * 1664525u + 1013904223u;
        noise.left[i] = noise.right[i] = .2f * (float(random >> 8) / 8388608.f - 1);
    }
    tuner.reset(); render(tuner, noise, out, s);
    require(!tuner.telemetry().voiced, "white noise is voiced");
    std::fill(noise.left.begin(), noise.left.end(), .2f);
    std::fill(noise.right.begin(), noise.right.end(), .2f);
    tuner.reset(); render(tuner, noise, out, s);
    require(!tuner.telemetry().voiced, "DC is voiced");
    auto in = tone(48000, 225); Audio separate{int(in.left.size())};
    tuner.reset(); render(tuner, in, separate, s);
    AutomaticTuner inplace; inplace.prepare(48000, 64);
    const unsigned before = allocations.load(); watching = true;
    float* buffers[] = {in.left.data(), in.right.data()};
    inplace.process(buffers, buffers, int(in.left.size()), 2, s);
    watching = false;
    require(allocations == before, "in-place large block allocated");
    for (size_t i = 0; i < in.left.size(); ++i) require(std::abs(in.left[i] - separate.left[i]) < .00001, "in-place processing mismatch");
    AutomaticTuner unprepared; unprepared.reset();
    unprepared.process(in.left.data(), out.left.data(), 16, s);
    for (int i = 0; i < 16; ++i) require(out.left[i] == 0, "unprepared output not clear");
    bool rejected = false;
    try { unprepared.prepare(std::numeric_limits<double>::quiet_NaN(), 64); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "invalid prepare not rejected");
    // Informational single-thread processing cost; not a machine-specific gate.
    auto benchmarkIn = tone(48000, 225, 3); Audio benchmarkOut{int(benchmarkIn.left.size())};
    tuner.reset(); const auto start = std::chrono::steady_clock::now();
    render(tuner, benchmarkIn, benchmarkOut, s);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "48k stereo processing real-time factor=" << elapsed / 3 << " (lower is better; includes test checks)\n";
}

void formantTests() {
    constexpr double rate = 48000, fundamental = 220;
    auto in = tone(rate, fundamental, 1.5);
    auto envelope = [](double hz) { return std::exp(-.5 * std::pow((hz - 850) / 180, 2)) + .6 * std::exp(-.5 * std::pow((hz - 2200) / 260, 2)) + .02; };
    for (size_t i = 0; i < in.left.size(); ++i) {
        double v = 0;
        for (int h = 1; h <= 20; ++h) v += .06 * envelope(h * fundamental) * std::sin(2 * pi * h * fundamental * i / rate);
        in.left[i] = in.right[i] = float(v);
    }
    TuningSettings s; s.scale = TuningScale::Custom; s.allowedNotesMask = 1u; s.stability = 0;
    AutomaticTuner a, b; a.prepare(rate, 256); b.prepare(rate, 256);
    Audio preserved{int(in.left.size())}, shifted{int(in.left.size())};
    render(a, in, preserved, s); s.preserveFormants = false; render(b, in, shifted, s);
    require(a.telemetry().voiced && a.telemetry().targetMidi == 60, "harmonic vowel detection");
    auto distance = [&](const Audio& audio) {
        std::array<double, 16> measured{}, expected{};
        double sumM = 0, sumE = 0;
        for (int h = 1; h <= 16; ++h) {
            const double hz = 261.625565 * h;
            double re = 0, im = 0;
            for (size_t i = audio.left.size() / 2; i < audio.left.size(); ++i) {
                re += audio.left[i] * std::cos(2 * pi * hz * i / rate);
                im += audio.left[i] * std::sin(2 * pi * hz * i / rate);
            }
            measured[h - 1] = std::hypot(re, im); expected[h - 1] = envelope(hz);
            sumM += measured[h - 1]; sumE += expected[h - 1];
        }
        double d = 0;
        for (int h = 0; h < 16; ++h) d += std::pow(measured[h] / sumM - expected[h] / sumE, 2);
        return d;
    };
    const auto p = distance(preserved), u = distance(shifted);
    std::cout << "vowel envelope error preserved=" << p << " unpreserved=" << u << '\n';
    require(p < u, "formant compensation did not preserve synthetic vowel envelope");
}
void pitchStepTests() {
    constexpr double rate = 48000;
    constexpr int change = 24000;
    Audio in{48000}, out{48000};
    double phase = 0;
    for (int i = 0; i < 48000; ++i) {
        phase += 2 * pi * (i < change ? 430. : 450.) / rate;
        in.left[i] = in.right[i] = float(.2 * std::sin(phase));
    }
    AutomaticTuner tuner; tuner.prepare(rate, 257);
    TuningSettings s; s.scale = TuningScale::Custom; s.allowedNotesMask = 1u << 9;
    s.retuneMs = s.transitionMs = s.stability = 0;
    render(tuner, in, out, s);
    auto measured = [&](int start, int count) {
        std::vector<float> window(out.left.begin() + start, out.left.begin() + start + count);
        return frequency(window, rate);
    };
    const double before = measured(change - 9600, 4800);
    const double after = measured(change + tuner.latency() + 9600, 4800);
    require(std::abs(1200 * std::log2(before / 440.)) < 10, "pitch-step pre-change tuning error");
    require(std::abs(1200 * std::log2(after / 440.)) < 10, "pitch-step failed to settle within 200 ms");
    double worst = 0;
    for (int start = change + tuner.latency(); start < change + tuner.latency() + 7200; start += 240) {
        const double hz = measured(start, 960);
        require(hz > 0 && std::isfinite(hz), "pitch-step lost periodic output");
        worst = std::max(worst, std::abs(1200 * std::log2(hz / 440.)));
    }
    std::cout << "Phase-continuous 430->450 Hz step: settled=" << after
              << " Hz, first150ms worst=" << worst << " cents (detector tracking is not instantaneous)\n";
    require(worst < 200, "pitch-step transient exceeded two semitones");
}
} // namespace

int main() {
    try {
        settingsTests(); pitchAndLatencyTests(); rangeReferenceAndScaleTests();
        continuityAndResetTests(); automationAndHardRetuneTests(); sweepAndSafetyTests(); formantTests(); pitchStepTests();
        std::cout << "Automatic tuner DSP tests passed; zero callback/reset allocations.\n";
        return 0;
    } catch (const std::exception& e) {
        watching = false;
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
