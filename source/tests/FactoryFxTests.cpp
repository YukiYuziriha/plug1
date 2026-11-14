#include "Routing.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
thread_local bool audioThread = false;
std::atomic<unsigned> allocations{0}, destructions{0};
}
void* operator new(std::size_t n) {
    if (audioThread) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (p && audioThread) ++destructions; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

using namespace vocalstack;
using P = CreativeParameter;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void finite(std::array<float, 2> x) {
    check(std::isfinite(x[0]) && std::isfinite(x[1]), "nonfinite factory FX output");
    check(std::abs(x[0]) < 64 && std::abs(x[1]) < 64, "factory FX headroom bound");
}
void impulseOracle() {
    for (double sr : {8000., 44100., 48000., 96000., 192000., 384000.}) {
        FactoryConvolution fx; fx.prepare(sr);
        for (int space = 0; space < 4; ++space) {
            const auto ir = FactoryConvolution::makeImpulse(sr, space);
            fx.reset(); double error = 0, power = 0;
            audioThread = true;
            for (size_t i = 0; i < ir[0].size() + 8192; ++i) {
                const auto y = fx.process(i == 0 ? 1.f : 0.f, i == 0 ? .5f : 0.f, space, 0, 0, 100, 0, 0);
                for (int c = 0; c < 2; ++c) {
                    const float expected = i < ir[c].size() ? ir[c][i] * (c ? .5f : 1.f) : 0;
                    error = std::max(error, std::abs(double(y[c] - expected)));
                    power += double(y[c]) * y[c];
                }
            }
            audioThread = false;
            check(error < 2.e-6, "partitioned impulse did not match original FIR / alignment / tail");
            check(std::abs(power - .6125) < 1.e-5, "factory impulse energy calibration");
        }
        std::cout << "FIR oracle: " << sr << " Hz, four spaces, stereo, exact zero-delay alignment\n";
    }
    std::array<std::array<std::vector<float>, 2>, 4> ir;
    for (int s = 0; s < 4; ++s) ir[s] = FactoryConvolution::makeImpulse(48000, s);
    for (int a = 0; a < 4; ++a) for (int b = a + 1; b < 4; ++b) {
        double dot = 0, ea = 0, eb = 0;
        for (size_t i = 0; i < ir[a][0].size(); ++i) {
            const double x = ir[a][0][i], y = i < ir[b][0].size() ? ir[b][0][i] : 0;
            dot += x * y; ea += x * x; eb += y * y;
        }
        check(std::abs(dot / std::sqrt(ea * eb)) < .2, "factory IR responses not genuinely distinct");
    }
    // Arbitrary input verifies convolution linearity, not just impulse playback.
    FactoryConvolution fx; fx.prepare(48000);
    const auto h = FactoryConvolution::makeImpulse(48000, 2);
    std::array<float, 173> input{};
    for (size_t i = 0; i < input.size(); ++i) input[i] = float(.07 * std::sin(i * .37));
    for (size_t i = 0; i < 12000; ++i) {
        const auto y = fx.process(i < input.size() ? input[i] : 0, 0, 2, 0, 0, 100, 0, 0);
        double expected = 0;
        for (size_t j = 0; j < input.size() && j <= i; ++j)
            if (i - j < h[0].size()) expected += input[j] * h[0][i - j];
        check(std::abs(y[0] - expected) < 2.e-6 && y[1] == 0, "arbitrary-input FIR oracle / channel isolation");
    }
}
void shaping() {
    FactoryConvolution fx; fx.prepare(48000);
    const auto energy = [&](float tail, float duck, float output, float tilt, int start) {
        fx.reset(); double sum = 0;
        for (int i = 0; i < 180000; ++i) {
            const auto y = fx.process(i == 0 ? 1.f : 0.f, 0, 0, tilt, 0, tail, duck, output);
            if (i >= start) sum += double(y[0]) * y[0];
        }
        return sum;
    };
    const auto full = energy(100, 0, 0, 0, 20000);
    check(energy(25, 0, 0, 0, 20000) < full * .05, "Tail did not shorten late response");
    const auto all = energy(100, 0, 0, 0, 0);
    check(std::abs(energy(100, 0, -6, 0, 0) / all - std::pow(10., -.6)) < 1.e-5, "wet Output gain");
    check(energy(100, 30, 0, 0, 0) < all, "wet duck detector inactive");
    check(std::abs(energy(100, 0, 0, 12, 0) - energy(100, 0, 0, -12, 0)) > .01, "wet brightness tilt inactive");
    FactoryConvolution a, b; a.prepare(48000); b.prepare(48000);
    for (int i = 0; i < 5000; ++i) {
        const auto x = a.process(i == 0 ? 1.f : 0.f, 0, 2, 0, 10, 100, 0, 0);
        const auto y = b.process(i == 480 ? 1.f : 0.f, 0, 2, 0, 0, 100, 0, 0);
        check(std::abs(x[0] - y[0]) < 2.e-6, "PreDelay not sample aligned");
    }
}
void saturatorContract() {
    for (double rate : {44100., 48000., 96000.}) for (float character : {0.f, 35.f, 100.f}) {
        CreativeEffect fx; fx.prepare(rate, CreativeKind::saturator); CreativeSettings s;
        s[P::saturatorCharacter] = character; s[P::saturatorOutput] = 0;
        Waveshaper reference; reference.prepare(rate); WaveshaperSettings target;
        target.shape = 3; target.shapeAmount = character * .01f; target.driveDb = 6; target.quality = 1;
        double dc = 0;
        for (int i = 0; i < 24000; ++i) {
            float l = float(.4 * std::sin(i * .071)), r = -l;
            const auto y = fx.process(l, r, s); reference.process(l, r, target);
            check(std::abs(y[0] - l) < 2.e-6 && std::abs(y[1] - r) < 2.e-6, "Saturator does not use existing 2x ADAA warm curve");
            if (i > 12000) dc += y[0];
        }
        check(std::abs(dc / 12000) < .01, "Saturator asymmetric DC not removed");
    }
    for (auto kind : {CreativeKind::convolution, CreativeKind::saturator}) {
        CreativeEffect fx; fx.prepare(48000, kind); CreativeSettings s;
        std::array<float, 2> previous{}; float jump = 0;
        for (int i = 0; i < 48000; ++i) {
            if (i == 12000) { s[P::convolutionSpace] = 3; s[P::saturatorCharacter] = 100; }
            if (i == 24000) { s[P::convolutionBrightness] = 12; s[P::convolutionTail] = 25; s[P::saturatorTone] = -12; }
            const auto y = fx.process(float(.1 * std::sin(i * .015)), float(.1 * std::sin(i * .019)), s);
            for (int c = 0; c < 2; ++c) jump = std::max(jump, std::abs(y[c] - previous[c]));
            previous = y;
        }
        check(jump < .15f, "Factory FX automation produced excessive discontinuity");
    }
}
void automationAndReset() {
    for (double sr : {8000., 44100., 48000., 96000., 192000., 384000.})
        for (auto kind : {CreativeKind::convolution, CreativeKind::saturator}) {
            CreativeEffect fx; fx.prepare(sr, kind); CreativeSettings s;
            audioThread = true;
            for (int i = 0; i < 15000; ++i) {
                if (i % 127 == 0) for (size_t p = 51; p < creativeParameterCount; ++p) {
                    const auto& d = creativeParameters[p];
                    s.values[p] = d.min + (d.max - d.min) * float((i / 127) % 11) / 10;
                }
                if (i % 997 == 0) s.values[51 + size_t(i) % 11] = std::numeric_limits<float>::quiet_NaN();
                const float x = i % 777 == 0 ? std::numeric_limits<float>::infinity() : float(.3 * std::sin(i * .071));
                finite(fx.process(x, -x * .7f, s));
            }
            fx.reset(); s = {};
            for (int i = 0; i < 12000; ++i) check(fx.process(0, 0, s) == std::array<float, 2>{}, "reset silence leaked");
            audioThread = false;
        }
    for (auto kind : {CreativeKind::convolution, CreativeKind::saturator}) {
        CreativeEffect a, b; a.prepare(48000, kind); b.prepare(48000, kind); CreativeSettings s;
        std::array<std::array<float, 2>, 12000> reference{};
        for (int i = 0; i < int(reference.size()); ++i) reference[size_t(i)] = a.process(float(.2 * std::sin(i * .09)), 0, s);
        int pos = 0, block = 0;
        constexpr int sizes[]{1, 7, 511, 3, 64, 127, 19};
        while (pos < int(reference.size())) {
            const int n = std::min(sizes[block++ % 7], int(reference.size()) - pos);
            for (int i = 0; i < n; ++i, ++pos)
                check(b.process(float(.2 * std::sin(pos * .09)), 0, s) == reference[size_t(pos)], "irregular block dependence");
        }
        b.reset();
        for (int i = 0; i < int(reference.size()); ++i)
            check(b.process(float(.2 * std::sin(i * .09)), 0, s) == reference[size_t(i)], "reset nondeterminism");
    }
    check(allocations == 0 && destructions == 0, "audio allocation or destruction");
}
Graph graph(NodeType type) {
    return {{{1, NodeType::lead}, {100, type}, {13, NodeType::output}}, {{1, 100, 0}, {100, 13, 0}}};
}
void routing() {
    for (auto type : {NodeType::convolution, NodeType::saturator}) {
        RoutingExecutor fx; fx.prepare(48000);
        auto g = graph(type); check(bool(fx.requestGraph(g)), "new FX routing rejected");
        RoutingSources in; RoutingControls controls; controls.fxAutomation = true;
        controls.fxSettings[1].enabled = false;
        audioThread = true;
        for (int i = 0; i < 12000; ++i) {
            in.lead = {float(.2 * std::sin(i * .04)), float(.1 * std::cos(i * .07))};
            const auto y = fx.process(in, controls);
            check(y.left == in.lead.left && y.right == in.lead.right, "new FX bypass must be exact zero-latency dry");
        }
        audioThread = false;
        check(fx.additionalLatencySamples() == 0 && fx.tailSeconds() >= 1, "new FX latency/tail metadata");
        controls.fxSettings[1].enabled = true;
        controls.fxSettings[1].mix = 1;
        for (int i = 0; i < 6000; ++i) { const auto y = fx.process(in, controls); finite({y.left, y.right}); }
        auto branch = g; branch.nodes.insert(branch.nodes.end() - 1, {101, type});
        branch.links.push_back({1, 101, 0}); branch.links.push_back({101, 13, 0});
        check(bool(fx.requestGraph(branch)), "parallel new FX graph rejected");
        audioThread = true;
        for (int i = 0; i < 6000; ++i) { const auto y = fx.process(in, controls); finite({y.left, y.right}); }
        audioThread = false;
        fx.collectRetired();
    }
    check(allocations == 0 && destructions == 0, "graph audio allocation/destruction");
}
void monitoredFactoryAudio() {
    for (auto type : {NodeType::reverb, NodeType::convolution, NodeType::saturator}) {
        FxMonitor monitor;
        RoutingExecutor fx; fx.prepare(48000); fx.setFxMonitor(&monitor);
        Graph g{{{1, NodeType::lead}, {100, type}, {101, type}, {13, NodeType::output}},
                {{1, 100, 0}, {100, 101, 0}, {101, 13, 0}}};
        check(bool(fx.requestGraph(g)), "Factory telemetry graph rejected");
        monitor.retain(1); monitor.retain(2);
        RoutingControls controls; controls.fxAutomation = true; controls.fxSettings[1].mix = .5f; controls.fxSettings[2].mix = 1;
        RoutingSources input;
        FxMonitorFrame a, b;

        uint64_t sequence = 0;
        audioThread = true;
        for (int i = 0; i < 18000; ++i) {
            input.lead = {float(.2 * std::sin(i * .071)), float(.13 * std::cos(i * .043))};
            fx.process(input, controls);
            if (monitor.read(1, a) && a.sequence != sequence) {
                check(a.hasWet && a.nodeId == 100 && std::isfinite(a.wetRms) && a.wetRms > 0, "Missing measured factory wet energy");
                check(a.sequence > sequence, "Nonmonotonic factory publication");
                check(monitor.read(2, b) && b.nodeId == 101 && b.hasWet, "Duplicate type instance telemetry missing");
                // With Mix .5, recover actual pre-blend wet from the coherent
                // input/output window; dry leakage would fail this RMS oracle.
                double power = 0;
                for (size_t j = 0; j < 2048; ++j) for (size_t c = 0; c < 2; ++c) {
                    const double wet = 2 * a.output[c][j] - a.input[c][j];
                    power += wet * wet * .5;
                }
                check(std::abs(a.wetRms - std::sqrt(power / 2048)) < 2.e-5, "Wet RMS is not actual pre-blend stereo energy");
                for (size_t c = 0; c < 2; ++c) for (size_t j = 0; j < 2048; ++j)
                    check(a.output[c][j] == b.input[c][j], "Duplicate instance input/history crossed slots");
                const int first = int(a.historySamples - a.historyBinSamples) - (i + 1 - 2048);
                if (first >= 0 && first + int(a.historyBinSamples) <= 2048) {
                    float lo = 0, hi = 0; double binPower = 0;
                    for (int j = first; j < first + int(a.historyBinSamples); ++j) for (size_t c = 0; c < 2; ++c) {
                        const float wet = 2 * a.output[c][size_t(j)] - a.input[c][size_t(j)];
                        lo = std::min(lo, wet); hi = std::max(hi, wet); binPower += double(wet) * wet * .5;
                    }
                    const auto& bin = a.history.back();
                    check(std::abs(bin.wetMin - lo) < 2.e-5 && std::abs(bin.wetMax - hi) < 2.e-5
                        && std::abs(bin.wetRms - std::sqrt(binPower / a.historyBinSamples)) < 2.e-5,
                        "History measured blended output, not actual pre-Mix wet PCM");
                }
                sequence = a.sequence;
            }
        }
        controls.fxSettings[1].enabled = false;
        for (int i = 0; i < 5000; ++i) fx.process(input, controls);
        check(monitor.read(1, a) && a.wetRms == 0, "Bypass reports false wet activity");
        fx.reset(); check(!monitor.read(1, a), "Reset retained factory activity");
        input.lead = {}; controls.fxSettings[1].enabled = true;
        for (int i = 0; i < 5000; ++i) fx.process(input, controls);
        check(monitor.read(1, a) && a.wetRms == 0 && a.sequence > sequence, "Reset silence / monotonic sequence failed");
        audioThread = false;
    }
    check(allocations == 0 && destructions == 0, "Factory telemetry audio allocation/destruction");
}
// Compare every published history bin to independently accumulated actual PCM,
// not injected monitor frames or a parameter-derived waveform.
void waveformHistoryOracle() {
    for (auto type : {NodeType::reverb, NodeType::convolution, NodeType::saturator})
    for (double rate : {48000., 96000.}) for (int channels : {1, 2}) {
        FxMonitor monitor;
        RoutingExecutor observed, plain;
        observed.prepare(rate); plain.prepare(rate); observed.setFxMonitor(&monitor);
        Graph graph{{{1, NodeType::lead}, {100, type}, {13, NodeType::output}}, {{1, 100, 0}, {100, 13, 0}}};
        check(bool(observed.requestGraph(graph)) && bool(plain.requestGraph(graph)), "History oracle graph");
        monitor.retain(1);
        RoutingControls controls; controls.fxAutomation = true; controls.fxSettings[1].mix = 1;
        const size_t binSize = size_t(rate * .020), total = size_t(rate * 3.2);
        std::vector<StereoSample> input(total), output(total);
        for (size_t i = 0; i < total; ++i) {
            const float x = i < size_t(rate * .35) ? float(.23 * std::sin(i * .071) + .1 * std::cos(i * .039)) : 0;
            input[i] = {x, channels == 1 ? x : -.6f * x};
        }
        RoutingSources source;
        FxMonitorFrame frame;
        uint64_t previous = 0; bool tail = false, wrapped = false;
        constexpr size_t blocks[]{1, 7, 511, 3, 64, 127};
        size_t position = 0, block = 0;
        audioThread = true;
        while (position < total) {
            const auto end = std::min(total, position + blocks[block++ % 6]);
            for (; position < end; ++position) {
                if (type == NodeType::saturator)
                    controls.fxSettings[1].creative[CreativeParameter::saturatorDrive] = position < size_t(rate * .2) ? 0.f : 24.f;
                source.lead = input[position];
                output[position] = observed.process(source, controls);
                const auto reference = plain.process(source, controls);
                check(reference.left == output[position].left && reference.right == output[position].right, "History changed DSP numerics");
            }
            if (!monitor.read(1, frame) || frame.sequence == previous) continue;
            previous = frame.sequence;
            check(frame.hasWet && frame.historyBinSamples == binSize, "History metadata/rate");
            const auto bins = size_t(frame.historySamples / binSize);
            check(frame.historyUsed == std::min(bins, FxMonitorFrame::historyCount), "History occupancy/wrap");
            wrapped |= bins > FxMonitorFrame::historyCount;
            for (size_t j = 0; j < frame.historyUsed; ++j) {
                const size_t start = (bins - frame.historyUsed + j) * binSize;
                float inMin = 0, inMax = 0, wetMin = 0, wetMax = 0;
                double inPower = 0, wetPower = 0;
                for (size_t k = start; k < start + binSize; ++k) {
                    inMin = std::min({inMin, input[k].left, input[k].right}); inMax = std::max({inMax, input[k].left, input[k].right});
                    wetMin = std::min({wetMin, output[k].left, output[k].right}); wetMax = std::max({wetMax, output[k].left, output[k].right});
                    inPower += (double(input[k].left) * input[k].left + double(input[k].right) * input[k].right) * .5;
                    wetPower += (double(output[k].left) * output[k].left + double(output[k].right) * output[k].right) * .5;
                }
                const auto& b = frame.history[FxMonitorFrame::historyCount - frame.historyUsed + j];
                check(std::abs(b.inputMin - inMin) < 1.e-6 && std::abs(b.inputMax - inMax) < 1.e-6
                      && std::abs(b.wetMin - wetMin) < 1.e-6 && std::abs(b.wetMax - wetMax) < 1.e-6, "History extrema differ from actual PCM");
                check(std::abs(b.inputRms - std::sqrt(inPower / binSize)) < 1.e-6
                      && std::abs(b.wetRms - std::sqrt(wetPower / binSize)) < 1.e-6, "History RMS differs from actual PCM");
                if (start > size_t(rate * .6) && b.inputRms == 0 && b.wetRms > 1.e-5) tail = true;
            }
        }
        audioThread = false;
        check(wrapped && previous > 30, "History did not wrap/publish");
        if (type != NodeType::saturator) check(tail, "Reverb history lost actual wet tail after input stopped");
        controls.fxSettings[1].enabled = false;
        audioThread = true;
        for (int i = 0; i < 5000; ++i) observed.process(source, controls);
        check(monitor.read(1, frame) && !frame.hasWet && frame.historyUsed == 0, "Bypass did not clear history");
        observed.reset();
        check(!monitor.read(1, frame), "Reset retained measured history");
        controls.fxSettings[1].enabled = true; source.lead = {};
        for (int i = 0; i < 5000; ++i) observed.process(source, controls);
        check(monitor.read(1, frame) && frame.hasWet, "Silence snapshot missing");
        for (const auto& b : frame.history) check(b.wetMin == 0 && b.wetMax == 0 && b.wetRms == 0, "Reset silence invented waveform");
        audioThread = false;
    }
    check(allocations == 0 && destructions == 0, "Waveform history allocated/destroyed on audio thread");
    std::cout << "PASS: measured PCM history oracle, 48/96 kHz mono/stereo irregular blocks, tails, drive, wrap, bypass, reset, bit-exact DSP, RT guards\n";
}
void monitorBenchmark() {
    for (auto type : {NodeType::reverb, NodeType::convolution, NodeType::saturator}) for (int mode = 0; mode < 3; ++mode) {
        FxMonitor monitor; RoutingExecutor fx; fx.prepare(48000);
        if (mode) fx.setFxMonitor(&monitor);
        if (mode == 2) monitor.retain(1);
        Graph graph{{{1, NodeType::lead}, {100, type}, {13, NodeType::output}}, {{1, 100, 0}, {100, 13, 0}}};
        check(bool(fx.requestGraph(graph)), "Monitor benchmark graph");
        RoutingSources source; RoutingControls controls;
        std::vector<float> input(96000);
        for (size_t i = 0; i < input.size(); ++i) input[i] = float(.2 * std::sin(i * .071));
        double checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (auto x : input) { source.lead = {x, -.7f * x}; const auto y = fx.process(source, controls); checksum += y.left; }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "monitor," << (type == NodeType::reverb ? "Reverb" : type == NodeType::convolution ? "Convolution" : "Saturator") << ','
            << (mode == 0 ? "no_bridge" : mode == 1 ? "unsubscribed" : "subscribed") << ",48000,2," << seconds << ',' << seconds * 50 << ',' << checksum << '\n';
    }
}
void benchmark() {
    monitorBenchmark();
    std::cout << "kind,mode,rate,seconds_audio,seconds_wall,realtime_percent,p99_64_sample_us,max_64_sample_us,prepare_ms,checksum\n";
    for (double sr : {44100., 48000., 96000., 192000., 384000.})
        for (auto kind : {CreativeKind::convolution, CreativeKind::saturator}) for (int mode = 0; mode < 2; ++mode) {
            const auto preparing = std::chrono::steady_clock::now();
            CreativeEffect fx; fx.prepare(sr, kind); CreativeSettings s;
            const auto prepareMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - preparing).count();
            const int blocks = int(sr * 2 / 64);
            std::vector<double> timings(size_t(blocks), 0.0);
            std::vector<float> input(size_t(blocks) * 64);
            for (size_t i = 0; i < input.size(); ++i) input[i] = float(.1 * std::sin(i * .071));
            float checksum = 0;
            const auto start = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b) {
                s[P::convolutionSpace] = mode ? float((b / 97) % 4) : 0;
                s[P::saturatorCharacter] = mode ? float(b % 101) : 35;
                const auto tick = std::chrono::steady_clock::now();
                for (int i = 0; i < 64; ++i) {
                    const auto y = fx.process(input[size_t(b) * 64 + i], .07f, s);
                    checksum += y[0];
                }
                timings[size_t(b)] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - tick).count();
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::sort(timings.begin(), timings.end());
            std::cout << (kind == CreativeKind::convolution ? "Convolution" : "Saturator") << ','
                      << (mode ? "rapid" : "stable") << ',' << sr << ",2," << seconds << ',' << seconds * 50 << ','
                      << timings[size_t(timings.size() * .99)] << ',' << timings.back() << ',' << prepareMs << ',' << checksum << '\n';
        }
}
}
int main(int argc, char**) {
    try {
        if (argc > 1) { benchmark(); return 0; }
        impulseOracle(); shaping(); saturatorContract(); automationAndReset(); routing(); monitoredFactoryAudio(); waveformHistoryOracle();
        std::cout << "PASS: original distinct FIRs, exact convolution oracle, shaping, automation, reset, finite headroom, irregular blocks, routing/bypass, zero RT allocations/destructions\n";
        return 0;
    } catch (const std::exception& e) { audioThread = false; std::cerr << e.what() << '\n'; return 1; }
}
