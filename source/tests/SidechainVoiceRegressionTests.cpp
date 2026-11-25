// Explicit offline regression/benchmark; run separately from concurrent tests for meaningful timings.
#include "VocalEngine.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using namespace vocalstack;
constexpr int rate = 48000, seconds = 12, warmup = 2 * rate, length = seconds * rate;
using Audio = std::array<std::vector<float>, 2>;
using Clock = std::chrono::steady_clock;
struct Mode { const char* name; bool process; float pitch, detune, formant; };
constexpr Mode modes[]{{"raw", false, 0, 0, 0}, {"neutral", true, 0, 0, 0},
    {"Pitch12", true, 12, 0, 0}, {"Detune10", true, 0, 10, 0}, {"Formant3", true, 0, 0, 3}};
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
double threadSeconds() {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    check(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user) != 0, "GetThreadTimes failed");
    const auto ticks = [](FILETIME t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return double(ticks(kernel) + ticks(user)) * 1.e-7;
#else
    return 0; // Wall timings remain available; CPU metric is Windows-specific.
#endif
}
double amplitude(const std::vector<float>& x, double hz) {
    double re = 0, im = 0;
    for (int i = length - rate; i < length; ++i) {
        const double phase = 2 * std::numbers::pi * hz * i / rate;
        re += x[size_t(i)] * std::cos(phase); im += x[size_t(i)] * std::sin(phase);
    }
    return 2 * std::hypot(re, im) / rate;
}
struct Result { Audio output; double wall = 0; };
Result run(const Mode& mode, int channels, int block, const Audio& main, const Audio& donor,
           const Audio* raw) {
    Settings s;
    s.levelOn = false; s.doubleOn = false; s.doubles = 0; s.human = 0;
    s.sidechainBlendAutomation = true; s.sidechainBlend = 1;
    s.sidechainSettings.enabled = mode.process;
    s.sidechainSettings.pitchSemitones = mode.pitch;
    s.sidechainSettings.detuneCents = mode.detune;
    s.sidechainSettings.formantSemitones = mode.formant;
    for (auto& v : s.voiceSettings) { v.enabled = false; v.present = false; }
    auto engine = std::make_unique<VocalEngine>();
    engine->prepare(rate, channels, channels, s);
    check(bool(engine->requestGraph(defaultGraph(false, true))), "sidechain graph rejected");
    Result result;
    for (auto& x : result.output) x.resize(length);
    std::vector<double> callbacks;
    callbacks.reserve(length / block + 1);
    std::array<double, seconds> windows{};
    double cpu = 0;
    // Preserve host block size, including across measurement-window boundaries.
    // CPU accounting starts at the first complete callback after warm-up.
    double cpuStart = 0;
    for (int offset = 0; offset < length;) {
            const int second = offset / rate;
            if (offset >= warmup && cpuStart == 0) cpuStart = threadSeconds();
            const int count = std::min(block, length - offset);
            const float* in[]{main[0].data() + offset, main[1].data() + offset};
            const float* aux[]{donor[0].data() + offset, donor[1].data() + offset};
            float* out[]{result.output[0].data() + offset, result.output[1].data() + offset};
            const auto start = Clock::now();
            engine->process(in, out, count, s, false, aux, channels);
            const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            if (offset >= warmup) { windows[size_t(second)] += elapsed; callbacks.push_back(elapsed / count * block * 1.e6); }
            offset += count;
    }
    cpu = threadSeconds() - cpuStart;
    double energy = 0, peak = 0, jump = 0, boundaryJump = 0, minRms64 = 1.e9;
    double rawError = 0, donorError = 0, targetAmplitude = 0;
    size_t zeros = 0, isolatedZeros = 0, longestZeroRun = 0, nonfinite = 0;
    for (int c = 0; c < channels; ++c) {
        size_t zeroRun = 0;
        const auto& x = result.output[size_t(c)];
        for (int i = warmup; i < length; ++i) {
            const double value = x[size_t(i)];
            nonfinite += !std::isfinite(value);
            energy += value * value; peak = std::max(peak, std::abs(value));
            const double delta = std::abs(value - x[size_t(i - 1)]);
            jump = std::max(jump, delta);
            if (i % block == 0) boundaryJump = std::max(boundaryJump, delta);
            if (value == 0) {
                ++zeros; longestZeroRun = std::max(longestZeroRun, ++zeroRun);
                if (i + 1 < length && std::abs(x[size_t(i - 1)]) > .001f && std::abs(x[size_t(i + 1)]) > .001f) ++isolatedZeros;
            } else zeroRun = 0;
            if (raw) rawError = std::max(rawError, std::abs(value - (*raw)[size_t(c)][size_t(i)]));
            donorError = std::max(donorError, std::abs(value - donor[size_t(c)][size_t(i - engine->latency())]));
        }
        for (int i = warmup; i + 64 <= length; i += 64) {
            double e = 0; for (int j = 0; j < 64; ++j) e += double(x[size_t(i + j)]) * x[size_t(i + j)];
            minRms64 = std::min(minRms64, std::sqrt(e / 64));
        }
        targetAmplitude = std::max(targetAmplitude, amplitude(x, 220 * std::exp2((mode.pitch + mode.detune * .01) / 12)));
    }
    std::sort(callbacks.begin(), callbacks.end());
    for (int i = 2; i < seconds; ++i) result.wall += windows[size_t(i)];
    const double early = (windows[2] + windows[3] + windows[4]) / 3;
    const double late = (windows[9] + windows[10] + windows[11]) / 3;
    const double rms = std::sqrt(energy / ((length - warmup) * channels));
    std::cout << mode.name << ',' << channels << ',' << block << ',' << engine->latency()
        << ',' << result.wall * 100 << ',' << cpu / 10 * 100 << ',' << late / early
        << ',' << callbacks[size_t(.99 * (callbacks.size() - 1))] << ',' << callbacks.back()
        << ',' << rms << ',' << peak << ',' << minRms64 << ',' << jump << ',' << boundaryJump
        << ',' << zeros << ',' << isolatedZeros << ',' << longestZeroRun << ',' << nonfinite
        << ',' << donorError << ',' << (raw ? rawError : 0) << ',' << targetAmplitude << '\n';
    check(nonfinite == 0 && rms > .001 && peak < 4, "nonfinite/silent/runaway sidechain output");
    check(longestZeroRun < 16 && isolatedZeros == 0 && minRms64 > .0001, "sustained dropout or isolated silent sample");
    check(jump < .25, "severe sample discontinuity");
    check(targetAmplitude > .0001, "missing spectral fundamental");
    if (!mode.process) check(donorError == 0, "raw sidechain not exact latency-aligned donor");
    if (raw && mode.pitch == 0 && mode.detune == 0 && mode.formant == 0)
        check(rawError == 0, "Process neutral differs from raw");
    // Timing is diagnostic, not a portable pass/fail limit on shared machines.
    return result;
}
}
int main() {
    try {
        Audio main, donor;
        for (auto* a : {&main, &donor}) for (auto& c : *a) c.resize(length);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < length; ++i) {
            const double phase = 2 * std::numbers::pi * 220 * i / rate + c * .37;
            float sample = .003f;
            for (int h = 1; h <= 12; ++h) sample += float(.09 / h * std::sin(phase * h + .13 * h));
            donor[size_t(c)][size_t(i)] = sample * (c ? .73f : 1.f);
            main[size_t(c)][size_t(i)] = float(.04 * std::sin(2 * std::numbers::pi * 317 * i / rate));
        }
        std::cout << std::setprecision(9)
            << "mode,channels,block,latency,wall_ms_per_audio_second,thread_cpu_pct,late_early_wall_ratio,p99_block_us,max_block_us,rms,peak,min_rms64,max_jump,max_host_boundary_jump,zero_samples,isolated_zeros,longest_zero_run,nonfinite,max_donor_error,max_raw_error,target_amplitude\n";
        // wall sum covers ten audio seconds: seconds * 100 = ms/audio second.
        for (int channels : {1, 2}) for (int block : {64, 137, 512}) {
            auto raw = run(modes[0], channels, block, main, donor, nullptr);
            for (size_t m = 1; m < std::size(modes); ++m) run(modes[m], channels, block, main, donor, &raw.output);
        }
        std::cout << "PASS: 30 sustained offline Sidechain cases (12 s each, 2 s warm-up)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
