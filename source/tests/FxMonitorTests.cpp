#include "Routing.h"
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace vocalstack;
constexpr double pi = 3.14159265358979323846;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double actual, double expected, const char* message, double tolerance = 1e-6) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
Graph serial() {
    RoutingNode wave{100, NodeType::waveshaper};
    wave.waveshaper.driveDb = 12;
    RoutingNode eq{114, NodeType::equalizer};
    eq.eq.gainDb = -6;
    return {{{1, NodeType::lead}, wave, eq, {13, NodeType::output}},
            {{1, 100, 0}, {100, 114, 0}, {114, 13, 0}}};
}
StereoSample signal(size_t i, double rate) {
    return {float(.37 * std::sin(2 * pi * 997 * double(i) / rate)),
            float(.21 * std::cos(2 * pi * 3521 * double(i) / rate))};
}
void install(RoutingExecutor& executor, const Graph& graph) {
    check(executor.requestGraph(graph).valid, "test graph rejected");
}
void subscriptions() {
    FxMonitor monitor;
    FxMonitorFrame frame;
    monitor.reset(48000);
    monitor.release(1); monitor.release(FxMonitor::slotCount);
    monitor.retain(FxMonitor::slotCount);
    check(!monitor.read(FxMonitor::slotCount, frame), "out-of-range read succeeded");
    monitor.retain(1); monitor.retain(1);
    const auto feed = [&](int count) {
        for (int i = 0; i < count; ++i) {
            monitor.beginSample();
            monitor.capture(1, 100, .25f, -.5f, .125f, -.25f, 0, {});
        }
    };
    feed(2047);
    check(!monitor.read(1, frame), "published partial window");
    feed(1);
    check(monitor.read(1, frame), "full subscribed window missing");
    const auto sequence = frame.sequence;
    monitor.release(1);
    check(monitor.read(1, frame), "one view releasing removed another subscription");
    monitor.release(1);
    check(!monitor.read(1, frame), "last release did not invalidate immediately");
    monitor.release(1); monitor.retain(1);
    check(!monitor.read(1, frame), "new subscription reused old generation");
    feed(2047);
    check(!monitor.read(1, frame), "resubscription published partial window");
    feed(1);
    check(monitor.read(1, frame) && frame.sequence > sequence, "resubscription sequence not monotonic");
    check(!monitor.read(2, frame), "unsubscribed slot returned data");
}
void routedStereo() {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        FxMonitor monitor;
        RoutingExecutor observed, plain;
        observed.prepare(rate); plain.prepare(rate);
        observed.setFxMonitor(&monitor);
        install(observed, serial()); install(plain, serial());
        monitor.retain(1); monitor.retain(15);
        Waveshaper wave; wave.prepare(rate);
        Equalizer eq; eq.prepare(rate);
        const auto graph = serial();
        std::vector<StereoSample> input, middle, output;
        RoutingSources sources; RoutingControls controls;
        FxMonitorFrame a, b;
        for (size_t i = 0; i < 12000; ++i) {
            sources.lead = signal(i, rate);
            auto wet = sources.lead;
            wave.process(wet.left, wet.right, graph.nodes[1].waveshaper);
            input.push_back(sources.lead); middle.push_back(wet);
            eq.process(wet.left, wet.right, graph.nodes[2].eq);
            output.push_back(wet);
            const auto x = observed.process(sources, controls), y = plain.process(sources, controls);
            check(x.left == y.left && x.right == y.right, "monitor changed rendered audio");
            if (monitor.read(1, a) && a.sequence != b.sequence) {
                // Publication occurs on this sample. Check every consecutive sample,
                // not just endpoints: decimated, swapped or summed channels fail.
                check(a.nodeId == 100 && a.sampleRate == rate && a.bandCount == 0, "slot 1 metadata");
                FxMonitorFrame last;
                check(monitor.read(15, last) && last.nodeId == 114, "independent final slot missing");
                for (size_t j = 0; j < FxMonitorFrame::sampleCount; ++j) {
                    const size_t k = i + 1 - FxMonitorFrame::sampleCount + j;
                    near(a.input[0][j], input[k].left, "wave pre L");
                    near(a.input[1][j], input[k].right, "wave pre R");
                    near(a.output[0][j], middle[k].left, "wave post L");
                    near(a.output[1][j], middle[k].right, "wave post R");
                    near(last.input[0][j], middle[k].left, "EQ pre L");
                    near(last.input[1][j], middle[k].right, "EQ pre R");
                    near(last.output[0][j], output[k].left, "EQ post L");
                    near(last.output[1][j], output[k].right, "EQ post R");
                }
                b = a;
            }
        }
        check(b.sequence > 1, "no overlapping full-rate publications");
    }
}
void lifecycle() {
    FxMonitor monitor;
    RoutingExecutor executor;
    executor.prepare(48000); executor.setFxMonitor(&monitor); monitor.retain(1);
    auto graph = serial(); install(executor, graph);
    RoutingSources sources; sources.lead = {.25f, -.125f}; RoutingControls controls;
    const auto feed = [&](int n) { for (int i = 0; i < n; ++i) executor.process(sources, controls); };
    FxMonitorFrame frame;
    feed(4096); check(monitor.read(1, frame), "initial frame missing");
    auto sequence = frame.sequence;
    executor.reset(); check(!monitor.read(1, frame), "reset left stale frame");
    sources.lead = {}; feed(2047); check(!monitor.read(1, frame), "reset skipped warmup");
    feed(1); check(monitor.read(1, frame) && frame.sequence > sequence, "reset lost subscription/sequence");
    for (const auto& channel : frame.output) for (float x : channel) near(x, 0, "reset silence not finite/zero");
    sequence = frame.sequence;
    Graph direct{{{1, NodeType::lead}, {13, NodeType::output}}, {{1, 13, 0}}};
    install(executor, direct); feed(1);
    check(!monitor.read(1, frame), "removed graph retained frame");
    feed(4096); check(!monitor.read(1, frame), "removed slot republished");
    executor.collectRetired();
    graph.nodes[1] = RoutingNode{100, NodeType::equalizer};
    install(executor, graph); feed(2047);
    check(!monitor.read(1, frame), "reused identity skipped warmup");
    feed(1); check(monitor.read(1, frame) && frame.sequence > sequence, "type reuse lost subscription");
    executor.prepare(96000); install(executor, graph);
    check(!monitor.read(1, frame), "rate change retained frame");
    feed(3199); check(!monitor.read(1, frame), "rate publication interval ignored");
    feed(1); check(monitor.read(1, frame) && frame.sampleRate == 96000, "rate/subscription not refreshed");
    for (const auto& channel : frame.input) for (float x : channel) near(x, 0, "new rate input stale");
}
void bandPorts() {
    for (int count : {2, 3}) for (bool enabled : {false, true}) {
        FxMonitor monitor;
        RoutingExecutor executor;
        executor.prepare(48000); executor.setFxMonitor(&monitor); monitor.retain(1);
        RoutingNode split{100, NodeType::crossover};
        split.enabled = enabled; split.bandCount = count;
        Graph graph{{{1, NodeType::lead}, split, {13, NodeType::output}}, {{1, 100, 0}, {100, 13, 0}}};
        install(executor, graph);
        Multiband reference; reference.prepare(48000);
        std::vector<Multiband::Bands> ports;
        RoutingSources sources; RoutingControls controls;
        FxMonitorFrame frame;
        uint64_t previous = 0;
        for (size_t i = 0; i < 8000; ++i) {
            sources.lead = signal(i, 48000);
            auto bands = reference.process(sources.lead.left, sources.lead.right, count);
            if (!enabled) { bands = {}; bands[0] = {sources.lead.left, sources.lead.right}; }
            ports.push_back(bands);
            executor.process(sources, controls);
            if (!monitor.read(1, frame) || frame.sequence == previous) continue;
            previous = frame.sequence;
            check(frame.bandCount == (enabled ? count : 1), "wrong crossover port count");
            std::array<double, 3> power{};
            for (size_t j = 0; j < FxMonitorFrame::sampleCount; ++j) {
                const auto& expected = ports[i + 1 - FxMonitorFrame::sampleCount + j];
                for (size_t ch = 0; ch < 2; ++ch) {
                    double sum = 0;
                    for (size_t band = 0; band < 3; ++band) {
                        sum += expected[band][ch];
                        power[band] += double(expected[band][ch]) * expected[band][ch] * .5;
                    }
                    near(frame.output[ch][j], sum, "crossover post is not actual port sum");
                }
            }
            for (size_t band = 0; band < 3; ++band)
                near(frame.bandRms[band], std::sqrt(power[band] / FxMonitorFrame::sampleCount), "band RMS is not actual stereo port power");
        }
        check(previous > 1, "crossover never published");
    }
}
void concurrentSnapshots() {
    FxMonitor monitor;
    monitor.reset(48000); monitor.retain(1);
    for (size_t i = 0; i < 2048; ++i) {
        monitor.beginSample(); monitor.capture(1, 100, float(i), -float(i), 2 * float(i), -2 * float(i), 0, {});
    }
    std::atomic<bool> start{false}, done{false};
    std::atomic<int> errors{0}, reads{0};
    const auto reader = [&] {
        while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
        uint64_t sequence = 0;
        do {
            FxMonitorFrame frame;
            if (!monitor.read(1, frame)) continue; // Bounded contention failures are allowed.
            ++reads;
            if (frame.sequence < sequence || frame.nodeId != 100 || frame.sampleRate != 48000) ++errors;
            sequence = frame.sequence;
            for (size_t j = 0; j < FxMonitorFrame::sampleCount; ++j) {
                const float x = frame.input[0][j];
                if (!std::isfinite(x) || frame.input[1][j] != -x || frame.output[0][j] != 2*x
                    || frame.output[1][j] != -2*x || (j && x != frame.input[0][j-1] + 1)) ++errors;
            }
        } while (!done.load(std::memory_order_acquire));
    };
    std::thread a(reader), b(reader);
    start.store(true, std::memory_order_release);
    for (size_t i = 2048; i < 160000; ++i) {
        monitor.beginSample(); monitor.capture(1, 100, float(i), -float(i), 2 * float(i), -2 * float(i), 0, {});
        if (i % 4096 == 0) { monitor.retain(1); monitor.release(1); std::this_thread::yield(); }
    }
    done.store(true, std::memory_order_release); a.join(); b.join();
    check(reads > 0 && errors == 0, "concurrent snapshots torn, out of order or incoherent");
}
} // namespace
int main() {
    struct Test { const char* name; void (*run)(); };
    const Test tests[]{{"subscription/refcount/generation", subscriptions}, {"routed full-rate stereo and transparent monitoring", routedStereo},
        {"reset/rate/removal/type reuse", lifecycle}, {"actual crossover port RMS and bypass", bandPorts},
        {"concurrent bounded snapshots", concurrentSnapshots}};
    int failures = 0;
    for (const auto& test : tests) {
        try { test.run(); std::cout << "[PASS] " << test.name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "[FAIL] " << test.name << ": " << e.what() << '\n'; }
    }
    return failures == 0 ? 0 : 1;
}
