#include "VocalEngine.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

static std::atomic<bool> watchAllocations{false};
static std::atomic<int> allocations{0};
void* operator new(std::size_t n) {
    if (watchAllocations.load()) ++allocations;
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
void check(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
Settings neutral(bool enabled) {
    Settings s;
    s.tuningEnabled = enabled; s.levelOn = false; s.formantOn = false;
    s.doubleOn = false; s.human = 0; s.advanced = true;
    for (auto& v : s.voiceSettings) v = {false, 0, 0, 0, 0, 0, 0, 0, 0, false};
    return s;
}
Graph sourceGraph(NodeType type) {
    return {{{1, type}, {2, NodeType::output}}, {{1, 2, 0}}};
}
struct Render { std::vector<float> left, right; TuningTelemetry telemetry; int latency; };
Render render(Settings s, double rate = 48000, int channels = 2, int reserve = 0,
              bool hostBypass = false, NodeType source = NodeType::lead,
              bool resetFirst = false, bool irregular = true) {
    VocalEngine engine;
    engine.prepare(rate, channels, 2, s, reserve);
    check(bool(engine.requestGraph(sourceGraph(source))), "source graph rejected");
    const int n = int(rate * 0.65);
    std::vector<float> input(n), other(n);
    Render r{std::vector<float>(n), std::vector<float>(n), {}, engine.latency()};
    for (int i = 0; i < n; ++i) {
        input[i] = 0.15f * std::sin(float(2 * 3.141592653589793 * 452 * i / rate));
        other[i] = -0.7f * input[i];
    }
    if (resetFirst) {
        const float* in[]{input.data(), other.data()};
        float* out[]{r.left.data(), r.right.data()};
        engine.process(in, out, 4096, s);
        engine.reset(s);
    }
    constexpr std::array<int, 7> blocks{1, 137, 7, 511, 32, 3, 64};
    allocations = 0;
    for (int offset = 0, block = 0; offset < n;) {
        int count = std::min(irregular ? blocks[size_t(block++ % blocks.size())] : 64, n - offset);
        const float* in[]{input.data() + offset, other.data() + offset};
        float* out[]{r.left.data() + offset, r.right.data() + offset};
        watchAllocations = true;
        engine.process(in, out, count, s, hostBypass, in, channels);
        watchAllocations = false;
        offset += count;
    }
    check(allocations == 0, "callback allocated");
    r.telemetry = engine.getTuningTelemetry();
    for (int i = 0; i < n; ++i) {
        check(std::isfinite(r.left[i]) && std::isfinite(r.right[i]), "nonfinite output");
        if (s.bypass || s.globalMix == 0 || (hostBypass && i > int(rate * 0.3))
            || source == NodeType::sidechain
                        || (source == NodeType::lead && s.leadSettings.tuneMix == 0)) {
            const float expected = i < r.latency ? 0 : input[size_t(i - r.latency)];
            check(std::abs(r.left[i] - expected) < 1e-6f, "clean/dry latency alignment");
            check(std::abs(r.right[i] - (channels == 1 ? expected : -0.7f * expected)) < 1e-6f,
                  "stereo clean/dry alignment");
        }
    }
    return r;
}
float frequency(const std::vector<float>& v, double rate) {
    int crossings = 0, first = -1, last = 0;
    for (int i = int(rate * 0.3); i < int(v.size()); ++i)
        if (v[size_t(i - 1)] <= 0 && v[size_t(i)] > 0) {
            if (first < 0) first = i;
            last = i; ++crossings;
        }
    return float((crossings - 1) * rate / (last - first));
}
void latencyAndPolicy() {
    auto s = neutral(false);
    VocalEngine e; e.prepare(48000, 2, 2, s);
    check(e.latency() == 1344 && e.tuningLatency() == 0, "legacy latency changed");
    s.tuningEnabled = true;
    check(e.tuningRequiresReprepare(s), "enable must request reprepare");
    std::array<float, 4096> in{}, out{};
    const float* inputs[]{in.data(), in.data()}; float* outputs[]{out.data(), out.data()};
    e.process(inputs, outputs, 4096, s);
    check(e.latency() == 1344 && !e.getTuningTelemetry().voiced, "live enable changed latency");
    e.reset(s);
    check(e.latency() == 1344 && e.tuningRequiresReprepare(s), "reset changed reservation");
    e.prepare(48000, 2, 2, s, 256);
    check(e.latency() == 2944 && e.tuningLatency() == 1344, "enabled serial latency");
    check(!e.tuningRequiresReprepare(s), "prepared enable mismatch");
    s.tuningEnabled = false; e.reset(s);
    check(e.latency() == 2944 && e.tuningRequiresReprepare(s), "disable lost host reservation");
    check(!e.getTuningTelemetry().hasTarget, "disabled telemetry is stale");
}
void paths() {
    for (double rate : {44100., 48000., 96000.}) for (int channels : {1, 2}) {
        auto s = neutral(true); s.leadSettings.tuneMix = 0;
        const auto clean = render(s, rate, channels);
        const int expected = rate == 96000 ? 5376 : 2688;
        check(clean.latency == expected, "sample-rate latency");
        s.globalMix = 0; render(s, rate, channels, 256);
        s.globalMix = 1; s.bypass = true; render(s, rate, channels, 256);
    }
    auto s = neutral(true);
    render(s, 48000, 2, 256, true);
        render(s, 48000, 2, 256, false, NodeType::sidechain);
    s.leadSettings.tuneMix = 0;
    const auto fresh = render(s), reset = render(s, 48000, 2, 0, false, NodeType::lead, true);
    check(fresh.left == reset.left && fresh.right == reset.right, "reset not deterministic");
    s = neutral(false);
    auto dormant = s; dormant.tuningMix = 0.37f; dormant.leadSettings.tuneMix = 0.17f;
    dormant.tuning.referenceHz = 420;
    check(render(s).left == render(dormant).left, "OFF tuning controls changed legacy output");
}
void automationAndInPlace() {
    auto s = neutral(true);
    VocalEngine inPlace, separate;
    inPlace.prepare(48000, 2, 2, s);
    separate.prepare(48000, 2, 2, s);
    check(bool(inPlace.requestGraph(sourceGraph(NodeType::lead))), "in-place graph rejected");
    check(bool(separate.requestGraph(sourceGraph(NodeType::lead))), "separate graph rejected");
    std::array<float, 137> left{}, right{}, originalLeft{}, originalRight{}, outLeft{}, outRight{};
    for (int block = 0; block < 160; ++block) {
        if (block == 35) { s.tuningEnabled = false; s.leadSettings.tuneMix = 0.3f; }
        if (block == 70) { s.tuningEnabled = true; s.tuningMix = 0.4f; }
        if (block == 95) {
            s.tuningMix = std::numeric_limits<float>::quiet_NaN();
            s.leadSettings.tuneMix = std::numeric_limits<float>::infinity();
            s.tuning.amount = std::numeric_limits<float>::quiet_NaN();
        }
        for (int i = 0; i < 137; ++i) {
            left[i] = originalLeft[i] = 0.1f * std::sin(float(block * 137 + i) * 0.059f);
            right[i] = originalRight[i] = -0.7f * left[i];
        }
        const float* aliasedIn[]{left.data(), right.data()};
        float* aliasedOut[]{left.data(), right.data()};
        const float* input[]{originalLeft.data(), originalRight.data()};
        float* output[]{outLeft.data(), outRight.data()};
        allocations = 0; watchAllocations = true;
        inPlace.process(aliasedIn, aliasedOut, 137, s);
        separate.process(input, output, 137, s);
        watchAllocations = false;
        check(allocations == 0, "automated callback allocated");
        check(inPlace.latency() == 2688, "automation changed reserved latency");
        for (int i = 0; i < 137; ++i)
            check(std::isfinite(left[i]) && left[i] == outLeft[i] && right[i] == outRight[i],
                  "in-place automation differs from separate buffers");
        if (!s.tuningEnabled) check(!inPlace.getTuningTelemetry().voiced, "disabled telemetry stale");
    }
    s = neutral(true);
    inPlace.reset(s);
    left.fill(0); right.fill(0);
    const float* input[]{left.data(), right.data()}; float* output[]{outLeft.data(), outRight.data()};
    allocations = 0; watchAllocations = true;
    for (int i = 0; i < 80; ++i) inPlace.process(input, output, 137, s);
    watchAllocations = false;
    check(allocations == 0, "silence callback allocated");
    check(!inPlace.getTuningTelemetry().voiced, "silence voiced after reset");
    for (int i = 0; i < 137; ++i) check(outLeft[i] == 0 && outRight[i] == 0, "reset leaked audio");
}
void blends() {
    auto s = neutral(true);
    auto clean = s; clean.leadSettings.tuneMix = 0;
    const auto tuned = render(s), dry = render(clean);
    check(tuned.telemetry.voiced && tuned.telemetry.hasTarget, "missing shared tuner telemetry");
    check(std::abs(tuned.telemetry.detectedHz - 452) < 2, "stereo detector input incorrect");
    check(std::abs(frequency(tuned.left, 48000) - 440) < 2, "Lead not tuned before neutral path");
    check(std::abs(frequency(dry.left, 48000) - 452) < 1, "Lead clean blend ignored");
    auto partial = s; partial.leadSettings.tuneMix = 0.5f;
    const auto half = render(partial);
    for (size_t i = 0; i < half.left.size(); ++i)
        check(std::abs(half.left[i] - 0.5f * (dry.left[i] + tuned.left[i])) < 1e-6f, "Lead blend is not linear");
    auto zero = s; zero.tuningMix = 0;
    check(render(zero).left == dry.left, "global tuning mix zero is not clean");
    const auto regular = render(s, 48000, 2, 0, false, NodeType::lead, false, false);
    for (size_t i = 0; i < regular.left.size(); ++i)
        check(std::abs(regular.left[i] - tuned.left[i]) < 1e-6f, "host block partition changed tuning");
    s.voiceSettings[0] = {true, 0, -1, 0, 0, 0, 0, 0, 0, true};
    // A voice-only graph makes Lead tuneMix unable to mask voice source errors.
    for (float pitch : {0.f, 12.f}) {
        s.voiceSettings[0].pitchSemitones = pitch;
        s.voiceSettings[0].tuneMix = 1;
        const auto voiceTuned = render(s, 48000, 1, 0, false, NodeType::voice);
        s.voiceSettings[0].tuneMix = 0;
        const auto voiceClean = render(s, 48000, 1, 0, false, NodeType::voice);
        const float factor = pitch == 0 ? 1.f : 2.f;
        check(std::abs(frequency(voiceTuned.left, 48000) - 440 * factor) < 4, "voice tuned source missing");
        check(std::abs(frequency(voiceClean.left, 48000) - 452 * factor) < 4, "voice clean source missing");
    }
}
}
int main() {
    try { latencyAndPolicy(); paths(); automationAndInPlace(); blends(); std::cout << "Engine tuning integration passed\n"; }
    catch (const std::exception& e) { watchAllocations = false; std::cerr << e.what() << '\n'; return 1; }
}
