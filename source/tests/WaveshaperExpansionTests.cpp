#include "Routing.h"
#include <array>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
bool countAudioAllocations = false;
size_t audioAllocations = 0;
}
void* operator new(std::size_t n) {
    if (countAudioAllocations) ++audioAllocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace vocalstack;
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr std::array rates{8000, 44100, 48000, 96000, 192000};
void expect(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a, double b, double tolerance, const char* message) {
    expect(std::isfinite(a) && std::abs(a - b) <= tolerance, message);
}
bool dc(int c) { return c == 3 || c == 7 || c == 12 || c == 13; }
double logCosh(double x) {
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2 * a)) - std::log(2.0);
}
double oldPrimitive(double x, int shape) {
    if (shape == 1) return std::abs(x) <= 1 ? x * x / 2 : std::abs(x) - 0.5;
    if (shape == 2) { const double b = std::tanh(0.5); return (logCosh(x + 0.5) - b * x) / (1 - b * b); }
    return logCosh(x);
}
void legacy(int rate) {
    for (int c = 0; c < 3; ++c) {
        Waveshaper shaper; shaper.prepare(rate);
        WaveshaperSettings s; s.shape = c;
        double previous = 0;
        for (int i = 0; i < 4096; ++i) {
            float l = float(2.3 * std::sin(i * 0.071)), r = -l;
            const double x = l, delta = x - previous;
            const double expected = std::abs(delta) < 1e-5 * (1 + std::abs(x) + std::abs(previous))
                ? Waveshaper::transfer((x + previous) / 2, c)
                : (oldPrimitive(x, c) - oldPrimitive(previous, c)) / delta;
            shaper.process(l, r, s); previous = x;
            near(l, expected, 2e-7, "legacy ADAA changed");
        }
    }
}
// Independent dense midpoint integration checks analytic ADAA, including folds
// spanning several periods, clipping knees and destruction discontinuities.
void integration(int rate) {
    constexpr std::array<std::array<float, 2>, 5> intervals{{{-4.1f, 3.7f}, {0.3f, 2.2f},
        {-2.7f, -0.1f}, {1.1f, -3.2f}, {0.25f, 0.250001f}}};
    for (int c = 0; c < 19; ++c) for (float amount : {0.0f, 0.37f, 0.5f, 1.0f}) {
        WaveshaperSettings s; s.shape = c; s.shapeAmount = amount;
        for (const auto& interval : intervals) {
            Waveshaper shaper; shaper.prepare(rate);
            float l = interval[0], r = l; shaper.process(l, r, s);
            const double first = l;
            l = interval[1]; r = l; shaper.process(l, r, s);
            double measured = l;
            if (dc(c)) measured += (1 - std::exp(-2 * pi * 5 / rate)) * first;
            double integral = 0;
            constexpr int steps = 16384;
            for (int j = 0; j < steps; ++j) {
                const double x = interval[0] + (double(interval[1]) - interval[0]) * (j + 0.5) / steps;
                integral += Waveshaper::transfer(x, c, amount) / steps;
            }
            if (std::abs(measured - integral) > 0.001)
                std::cerr << "Integration shape=" << c << " amount=" << amount << " interval="
                          << interval[0] << ',' << interval[1] << " measured=" << measured
                          << " reference=" << integral << '\n';
            near(measured, integral, 0.001, "analytic ADAA disagrees with numerical integration");
        }
        s.raw = true;
        Waveshaper raw; raw.prepare(rate);
        float l = 1.75f, r = -0.83f;
        raw.process(l, r, s);
        near(l, Waveshaper::transfer(1.75, c, amount), 2e-7, "Raw left is not direct transfer");
        near(r, Waveshaper::transfer(-0.83f, c, amount), 2e-7, "Raw right is not direct transfer");
    }
}
void automation(int rate) {
    Waveshaper a, b; a.prepare(rate); b.prepare(rate);
    WaveshaperSettings s;
    constexpr std::array blocks{1, 7, 64, 3, 257, 19};
    int sample = 0;
    // The scalar reference and irregular caller blocks must be bit-identical.
    for (int block = 0; block < 180; ++block) for (int j = 0; j < blocks[block % blocks.size()]; ++j, ++sample) {
        s.shape = (sample / 83) % 19; s.raw = (sample / 127) % 2 != 0;
        s.shapeAmount = float((sample / 17) % 101) / 100;
        s.quality = (sample / 293) % 3;
        s.driveDb = float((sample / 211) % 73 - 24);
        s.outputDb = -6; s.mix = sample % 401 < 200 ? 1.0f : 0.5f;
        float l = float(0.4 * std::sin(sample * 0.023)), r = 0;
        float x = l, y = r;
        a.process(l, r, s); b.process(x, y, s);
        expect(l == x && r == y && std::isfinite(l), "automation/block partition is not deterministic");
        near(r, 0, 1e-6, "stereo history leaked into silent channel");
    }
    s.enabled = false;
    for (int i = 0; i < rate; ++i) { float l = 0.2f, r = -0.1f; a.process(l, r, s); }
    float l = 0.2f, r = -0.1f; a.process(l, r, s);
    expect(l == 0.2f && r == -0.1f, "bypass did not settle to exact dry");
    a.reset(); b.reset(); s.enabled = true;
    l = 0.3f; r = -0.2f; float x = l, y = r;
    a.process(l, r, s); b.process(x, y, s);
    expect(l == x && r == y, "reset retained history");
    s.shape = 999; s.quality = 999; s.shapeAmount = std::numeric_limits<float>::quiet_NaN();
    s.driveDb = std::numeric_limits<float>::infinity();
    l = std::numeric_limits<float>::infinity(); r = std::numeric_limits<float>::quiet_NaN();
    a.process(l, r, s);
    expect(std::isfinite(l) && std::isfinite(r), "invalid controls/input produced nonfinite output");
}
void dcAndSilence(int rate) {
    for (int c = 0; c < 19; ++c) for (bool raw : {false, true}) {
        Waveshaper shaper; shaper.prepare(rate);
        WaveshaperSettings s; s.shape = c; s.raw = raw;
        float l = 0, r = 0;
        for (int i = 0; i < 100; ++i) { l = r = 0; shaper.process(l, r, s); }
        near(l, 0, 1e-7, "silence generated signal");
        if (!dc(c)) continue;
        double sum = 0;
        for (int i = 0; i < 2 * rate; ++i) {
            l = float(0.8 * std::sin(2 * pi * 200 * i / rate)); r = l;
            shaper.process(l, r, s);
            if (i >= rate) sum += l;
        }
        near(sum / rate, 0, 2e-5, "new asymmetric/rectifier output has DC");
    }
}
void aliasMeasurement(int rate) {
    // Third harmonic of 3/16 Fs folds to 7/16 Fs. This is one measured
    // high-drive case, not a broadband guarantee for every curve or input.
    std::array<double, 2> magnitude{};
    for (int raw = 0; raw < 2; ++raw) {
        Waveshaper shaper; shaper.prepare(rate);
        WaveshaperSettings s; s.shape = 9; s.driveDb = 12; s.raw = raw != 0;
        double real = 0, imaginary = 0;
        constexpr int length = 4096;
        for (int i = 0; i < length + 1024; ++i) {
            float l = float(0.8 * std::sin(2 * pi * 3 * i / 16)), r = l;
            shaper.process(l, r, s);
            if (i >= 1024) {
                real += l * std::cos(2 * pi * 7 * i / 16);
                imaginary += l * std::sin(2 * pi * 7 * i / 16);
            }
        }
        magnitude[raw] = 2 * std::hypot(real, imaginary) / length;
    }
    expect(magnitude[0] < magnitude[1] * 0.8, "ADAA failed measured sine-fold alias reduction case");
    std::cout << "Sine Fold alias bin ADAA/Raw: " << magnitude[0] << '/' << magnitude[1] << '\n';
}
void qualityContracts(int rate) {
    expect(WaveshaperSettings{}.quality == 0, "legacy quality default changed");
    for (int quality = 0; quality < 3; ++quality) {
        WaveshaperSettings s; s.quality = quality; s.shape = 1;
        const auto metadata = Waveshaper::processingMetadata(s);
        expect(metadata.integerLatencySamples == 0 && metadata.oversamplingFactor == (1 << quality)
               && metadata.frequencyDependentPhase, "quality latency/phase metadata wrong");
        Waveshaper shaper, fresh; shaper.prepare(rate); fresh.prepare(rate);
        float l = 0.1f, r = 0;
        shaper.process(l, r, s);
        expect(l != 0, "mode introduced an explicit whole-sample impulse delay");
        // Linear hard-clip region: passband gain should equal ADAA's theoretical
        // averaging loss at the actual processing rate, not interpolation images.
        double re = 0, im = 0;
        constexpr int length = 4096, warmup = 4096;
        for (int i = 0; i < length + warmup; ++i) {
            l = float(0.1 * std::sin(2 * pi * i / 8)); r = 0;
            shaper.process(l, r, s);
            if (i >= warmup) { re += l * std::cos(2 * pi * i / 8); im += l * std::sin(2 * pi * i / 8); }
            near(r, 0, 1e-12, "resampler stereo crosstalk");
        }
        const double magnitude = 2 * std::hypot(re, im) / (0.1 * length);
        near(magnitude, std::cos(pi / (8 * (1 << quality))), 1e-4, "resampler passband/ADAA gain wrong");
        if (quality != 0) expect(std::abs(re) > 1, "IIR phase metadata falsely describes phase-neutral output");
        shaper.reset();
        for (int i = 0; i < 1000; ++i) {
            l = float(0.3 * std::sin(i * 0.03)); r = -l; float x = l, y = r;
            shaper.process(l, r, s); fresh.process(x, y, s);
            expect(l == x && r == y, "quality reset did not clear resampling state");
        }
        s.enabled = false;
        for (int i = 0; i < rate; ++i) { l = 0.23f; r = -0.13f; shaper.process(l, r, s); }
        l = 0.23f; r = -0.13f; shaper.process(l, r, s);
        expect(l == 0.23f && r == -0.13f, "quality bypass delayed or filtered dry signal");
    }
    // Raw's entire time-domain sequence, including DC removal, is independent
    // of the selected oversampling factor, not merely its static transfer.
    for (int c : {0, 3, 7, 12, 13, 16, 18}) {
        std::array<Waveshaper, 3> raw;
        for (auto& effect : raw) effect.prepare(rate);
        WaveshaperSettings s; s.raw = true; s.shape = c; s.driveDb = 9;
        for (int i = 0; i < 2000; ++i) {
            float expectedL = 0, expectedR = 0;
            for (int q = 0; q < 3; ++q) {
                s.quality = q;
                float l = float(0.8 * std::sin(i * 0.093)), r = -l;
                raw[q].process(l, r, s);
                if (q == 0) { expectedL = l; expectedR = r; }
                else expect(l == expectedL && r == expectedR, "Raw was secretly oversampled/filtered by quality");
            }
        }
    }
    // Live selection requires no prepare or graph rebuild; path activation and
    // arbitrary rapid reversals are bounded and allocation-free.
    Waveshaper changing; changing.prepare(rate);
    WaveshaperSettings s; s.shape = 1;
    float previous = 0;
    audioAllocations = 0; countAudioAllocations = true;
    bool finite = true, smooth = true;
    double maximumStep = 0; int maximumStepSample = 0;
    for (int i = 0; i < rate; ++i) {
        if (i >= rate / 8) s.quality = (i / (rate / 8)) % 3;
        s.raw = i >= rate / 2 && i < 3 * rate / 4;
        float l = 0.3f, r = 0;
        changing.process(l, r, s);
        finite = finite && std::isfinite(l) && std::isfinite(r);
        if (i > 10) {
            const double step = std::abs(l - previous);
            if (step > maximumStep) { maximumStep = step; maximumStepSample = i; }
            smooth = smooth && step < 0.01;
        }
        previous = l;
    }
    countAudioAllocations = false;
    if (!finite || !smooth) std::cerr << "Quality switch max step=" << maximumStep << " at " << maximumStepSample << '\n';
    expect(finite && smooth, "live quality/Raw switching clicked or produced nonfinite output");
    expect(audioAllocations == 0, "quality selection allocated on audio thread");
    // Decimator/DC histories must drain with silence rather than retaining a
    // stale offset or unbounded IIR ringing. Exercise every curve in both modes.
    for (int q = 1; q <= 2; ++q) for (int c = 0; c < 19; ++c) {
        Waveshaper effect; effect.prepare(rate);
        s = {}; s.quality = q; s.shape = c; s.driveDb = 36;
        float l = 0, r = 0;
        for (int i = 0; i < 100; ++i) {
            l = float(std::sin(i * 0.3)); r = -l; effect.process(l, r, s);
            expect(std::isfinite(l) && std::isfinite(r), "quality/curve produced nonfinite audio");
        }
        for (int i = 0; i < rate; ++i) { l = r = 0; effect.process(l, r, s); }
        near(l, 0, 1e-6, "quality/curve silence tail did not drain");
        near(r, 0, 1e-6, "right quality/curve silence tail did not drain");
    }
}
void qualityDcAndGuards(int rate) {
    for (int q = 1; q < 3; ++q) for (int c : {3, 7, 12, 13}) {
        Waveshaper effect; effect.prepare(rate);
        WaveshaperSettings s; s.quality = q; s.shape = c;
        double mean = 0;
        for (int i = 0; i < 2 * rate; ++i) {
            float l = float(0.8 * std::sin(2 * pi * 200 * i / rate)), r = -l;
            effect.process(l, r, s);
            if (i >= rate) mean += l;
        }
        near(mean / rate, 0, 3e-5, "oversampled asymmetric/rectifier output has DC");
    }
    for (int q = 0; q < 3; ++q) for (int c = 0; c < 19; ++c) {
        Waveshaper effect; effect.prepare(rate);
        WaveshaperSettings s; s.quality = q; s.shape = c; s.driveDb = 48; s.outputDb = 24;
        for (int i = 0; i < 100; ++i) {
            float l = i % 2 ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max();
            float r = i % 3 ? std::numeric_limits<float>::quiet_NaN() : 0;
            effect.process(l, r, s);
            expect(std::isfinite(l) && std::isfinite(r), "extreme input escaped quality finite guards");
        }
    }
    for (int c : {8, 11}) for (double x : {1e20, -1e20, 1e40, -1e40})
        expect(std::abs(Waveshaper::transfer(x, c)) <= 1, "large-input periodic fold lost its bounds");
}
void qualityAliasMeasurement(int rate) {
    std::array<double, 3> magnitude{};
    for (int q = 0; q < 3; ++q) {
        Waveshaper effect; effect.prepare(rate);
        WaveshaperSettings s; s.shape = 9; s.driveDb = 12; s.quality = q;
        double re = 0, im = 0;
        constexpr int length = 8192, warmup = 4096;
        for (int i = 0; i < length + warmup; ++i) {
            float l = float(0.8 * std::sin(2 * pi * 3 * i / 16)), r = l;
            effect.process(l, r, s);
            if (i >= warmup) { re += l * std::cos(2 * pi * 7 * i / 16); im += l * std::sin(2 * pi * 7 * i / 16); }
        }
        magnitude[q] = 2 * std::hypot(re, im) / length;
    }
    expect(magnitude[1] < magnitude[0] * 0.1 && magnitude[2] < magnitude[0] * 0.1,
           "oversampling failed measured sine-fold alias reduction case");
    std::cout << "Sine Fold alias Off/2x/4x: " << magnitude[0] << '/' << magnitude[1] << '/' << magnitude[2] << '\n';
}
void levelMatchedAudition(int rate) {
    Waveshaper effect;
    effect.prepare(rate);
    WaveshaperSettings s;
    s.shape = 1; s.driveDb = 24; s.levelMatch = true;
    double inputPower = 0, outputPower = 0;
    for (int i = 0; i < rate * 3; ++i) {
        const float input = float(0.2 * std::sin(2 * pi * 220 * i / rate));
        float l = input, r = -input;
        effect.process(l, r, s);
        expect(std::isfinite(l) && std::isfinite(r), "level match produced nonfinite output");
        if (i >= rate * 2) { inputPower += input * input; outputPower += 0.5 * (l * l + r * r); }
    }
    expect(std::abs(10 * std::log10(outputPower / inputPower)) < 0.6, "audition RMS did not settle near input");
    s.enabled = false;
    for (int i = 0; i < rate; ++i) { float l = 0.25f, r = -0.5f; effect.process(l, r, s); }
    float l = 0.25f, r = -0.5f;
    effect.process(l, r, s);
    expect(l == 0.25f && r == -0.5f, "level match changed settled bypass");
    effect.reset();
    s.enabled = true;
    for (int i = 0; i < 1024; ++i) { l = r = 0; effect.process(l, r, s); expect(l == 0 && r == 0, "level match lifted silence"); }
}
void qualityCpu() {
    // One stereo instance at 48 kHz; wall time is informational, no machine-
    // dependent performance threshold. All modes use the same driven fold.
    double checksum = 0;
    for (int q = 0; q < 3; ++q) {
        Waveshaper effect; effect.prepare(48000);
        WaveshaperSettings s; s.quality = q; s.shape = 11; s.driveDb = 18;
        constexpr int length = 48000;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < length; ++i) {
            float l = float(0.7 * std::sin(i * 0.08)), r = -l;
            effect.process(l, r, s); checksum += l;
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << "Quality " << Waveshaper::qualityNames[q] << " one stereo second: " << ms << " ms\n";
    }
    expect(std::isfinite(checksum), "CPU measurement nonfinite output");
}
void switching(int rate) {
    Waveshaper shaper; shaper.prepare(rate);
    WaveshaperSettings s; s.shape = 1;
    float previous = 0;
    for (int i = 0; i < rate; ++i) {
        if (i == rate / 4) s.raw = true;
        if (i == rate / 2) { s.raw = false; s.shape = 9; }
        float l = 0.7f, r = l; shaper.process(l, r, s);
        if (i > 10) expect(std::abs(l - previous) < 0.01, "curve/Raw switch jumped on constant input");
        previous = l;
    }
    // Shared sample history must remain warm through exact bypass, too.
    s = {}; s.enabled = false; s.shape = 1;
    shaper.reset();
    for (int i = 0; i < 10; ++i) { float l = 2, r = -2; shaper.process(l, r, s); }
    s.enabled = true;
    float l = -2, r = 2; shaper.process(l, r, s);
    const double blend = -std::expm1(-1.0 / (rate * 0.010));
    near(l, -2 * (1 - blend), 2e-7, "bypass did not preserve ADAA sample history");
}
Graph graph() {
    return {{{1, NodeType::lead}, {101, NodeType::waveshaper}, {2, NodeType::output}},
            {{1, 101, 0}, {101, 2, 0}}};
}
void routing() {
    auto g = graph();
    for (int q = 0; q < 3; ++q) { g.nodes[1].waveshaper.quality = q; expect(bool(RoutingExecutor::validate(g)), "valid quality rejected"); }
    g.nodes[1].waveshaper.quality = 3;
    expect(!RoutingExecutor::validate(g), "invalid quality accepted");
    g.nodes[1].waveshaper.quality = -1;
    expect(!RoutingExecutor::validate(g), "negative quality accepted");
    g.nodes[1].waveshaper.quality = 0;
    expect(Waveshaper::shapeNames.size() == 19 && !WaveshaperSettings{}.raw
           && WaveshaperSettings{}.shapeAmount == 0.5f, "expanded settings defaults");
    expect(std::string_view(Waveshaper::shapeNames[0]) == "Soft"
        && std::string_view(Waveshaper::shapeNames[1]) == "Hard"
        && std::string_view(Waveshaper::shapeNames[2]) == "Asymmetric", "legacy indices changed");
    for (int c = 0; c < 19; ++c) { g.nodes[1].waveshaper.shape = c; expect(bool(RoutingExecutor::validate(g)), "valid shape rejected"); }
    g.nodes[1].waveshaper.shape = 19;
    expect(!RoutingExecutor::validate(g), "invalid shape accepted");
    g.nodes[1].waveshaper.shape = 18; g.nodes[1].waveshaper.shapeAmount = std::numeric_limits<float>::quiet_NaN();
    expect(!RoutingExecutor::validate(g), "invalid shape amount accepted");
    g = graph(); g.nodes[1].waveshaper = {true, 0, 0, 1, 9, true, 0.23f};
    RoutingExecutor executor; executor.prepare(48000); expect(bool(executor.requestGraph(g)), "graph request failed");
    RoutingSources sources; sources.lead = {0.4f, -0.2f};
    auto out = executor.process(sources, {});
    near(out.left, Waveshaper::transfer(0.4f, 9, 0.23f), 2e-6, "independent node settings lost Raw/amount");
    executor.reset();
    RoutingControls controls; controls.fxAutomation = true;
    const int slot = routingFxSlot(101); expect(slot > 0, "test FX ID is not automatable");
    controls.fxSettings[slot].waveshaper = {true, 0, 0, 1, 18, true, 0.8f};
    out = executor.process(sources, controls);
    near(out.left, Waveshaper::transfer(0.4f, 18, 0.8f), 2e-6, "automated settings lost Raw/amount");
}
void parallelQualityRouting(int rate) {
    // Correlated fan-out is compensated by 1/2 on reunion. No integer delay is
    // needed in either branch. This verifies timing, not IIR phase cancellation.
    auto g = graph(); g.links.push_back({1, 2, 0});
    g.nodes[1].waveshaper.shape = 1;
    RoutingExecutor routing; routing.prepare(rate);
    expect(bool(routing.requestGraph(g)), "parallel quality graph rejected");
    expect(routing.additionalLatencySamples() == 0, "unexpected routing integer latency");
    RoutingControls controls; controls.fxAutomation = true;
    auto& s = controls.fxSettings[routingFxSlot(101)].waveshaper; s.shape = 1;
    Waveshaper reference; reference.prepare(rate);
    RoutingSources sources;
    for (int i = 0; i < rate; ++i) {
        s.quality = (i / (rate / 5)) % 3;
        s.raw = i > rate / 2;
        sources.lead = {float(0.1 * std::sin(i * 0.08)), 0};
        float l = sources.lead.left, r = 0; reference.process(l, r, s);
        const auto out = routing.process(sources, controls);
        near(out.left, 0.5 * (sources.lead.left + l), 2e-7, "parallel branch has an unreported integer delay");
        near(out.right, 0, 1e-12, "parallel branch stereo leakage");
    }
    near(routing.tailSeconds(), Waveshaper::tailSeconds, 1e-12, "resampler tail not propagated to Output");
    auto bad = g; bad.nodes[1].latencySamples = 1;
    expect(!RoutingExecutor::validate(bad), "unsupported branch delay falsely accepted as aligned");
    // Local dry must stay exact across all qualities; parallel reunion is unity.
    routing.reset(); controls.fxSettings[routingFxSlot(101)].waveshaper.mix = 0;
    for (int i = 0; i < 100; ++i) {
        s.quality = i % 3; sources.lead = {0.2f, -0.3f};
        const auto out = routing.process(sources, controls);
        expect(out.left == sources.lead.left && out.right == sources.lead.right,
               "parallel local-dry reconstruction changed with quality");
    }
}
}
int main() {
    try {
        routing();
        for (int rate : rates) {
            legacy(rate); integration(rate); automation(rate); dcAndSilence(rate); switching(rate); aliasMeasurement(rate);
                        qualityContracts(rate); qualityDcAndGuards(rate); qualityAliasMeasurement(rate); parallelQualityRouting(rate); levelMatchedAudition(rate);
            std::cout << "Waveshaper expansion passed at " << rate << " Hz\n";
        }
        qualityCpu();
        return 0;
    } catch (const std::exception& e) {
        countAudioAllocations = false;
        std::cerr << "Waveshaper expansion failure: " << e.what() << '\n'; return 1;
    }
}
