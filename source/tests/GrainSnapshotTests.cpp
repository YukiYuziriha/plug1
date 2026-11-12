#include "Routing.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <cstdlib>
#include <new>

namespace { thread_local bool trackAllocations = false; thread_local size_t allocationCount = 0; }
void* operator new(std::size_t bytes) {
    if (trackAllocations) ++allocationCount;
    if (auto* result = std::malloc(bytes == 0 ? 1 : bytes)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace vocalstack;
namespace {
void expect(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Graph graph(NodeType type = NodeType::granular) {
    return {{{1, NodeType::lead}, {100, type}, {13, NodeType::output}},
            {{1, 100, 0}, {100, 13, 0}}};
}
void effectHistoryLifecycle(NodeType type, double rate) {
    auto patch = graph(type);
    patch.nodes[1].mix = 1;
    patch.nodes[1].creative.values[size_t(CreativeParameter::delayTime)] = 80;
    patch.nodes[1].creative.values[size_t(CreativeParameter::delayFeedback)] = 75;
    patch.nodes[1].creative.values[size_t(CreativeParameter::reverbDecay)] = 3;
    // A dry utility provides an exactly transparent neighbour to reorder across.
    RoutingNode neighbour{101, NodeType::gain}; neighbour.mix = 0;
    patch.nodes.push_back(neighbour);
    patch.links = {{1, 100, 0}, {100, 101, 0}, {101, 13, 0}};
    auto baseline = std::make_unique<RoutingExecutor>();
    auto reordered = std::make_unique<RoutingExecutor>();
    auto recreated = std::make_unique<RoutingExecutor>();
    for (auto* engine : {baseline.get(), reordered.get(), recreated.get()}) {
        engine->prepare(rate);
        expect(engine->requestGraph(patch).valid, "history graph accepted");
    }
    RoutingSources input; RoutingControls controls;
    for (int i = 0; i < int(rate * .3); ++i) {
        input.lead = {float(.2 * std::sin(i * .037)), float(.13 * std::cos(i * .051))};
        for (auto* engine : {baseline.get(), reordered.get(), recreated.get()}) engine->process(input, controls);
    }
    auto moved = patch;
    moved.links = {{1, 101, 0}, {101, 100, 0}, {100, 13, 0}};
    expect(reordered->requestGraph(moved).valid, "surviving ID/type reorder accepted");
    auto removed = patch;
    removed.nodes.erase(removed.nodes.begin() + 1);
    removed.links = {{1, 101, 0}, {101, 13, 0}};
    expect(recreated->requestGraph(removed).valid, "effect removal accepted");
    // Intentionally no process callback between removal and same-ID/type re-add.
    expect(recreated->requestGraph(patch).valid, "same-type immediate re-add accepted");
    input.lead = {};
    double retainedEnergy = 0, recreatedEnergy = 0;
    const int transitionSamples = std::max(1, int(rate * .010));
    for (int i = 0; i < int(rate * .3); ++i) {
        const auto original = baseline->process(input, controls);
        const auto movedOutput = reordered->process(input, controls);
        const auto freshOutput = recreated->process(input, controls);
        if (i < transitionSamples) continue; // Exclude the intentional output splice ramp.
        expect(std::abs(original.left - movedOutput.left) < 1.e-7f
            && std::abs(original.right - movedOutput.right) < 1.e-7f,
            "surviving delay/reverb reorder retains exact DSP history");
        retainedEnergy += double(original.left) * original.left + double(original.right) * original.right;
        recreatedEnergy += double(freshOutput.left) * freshOutput.left + double(freshOutput.right) * freshOutput.right;
    }
    expect(retainedEnergy > 1.e-8, "fixture has a measurable retained delay/reverb tail");
    expect(recreatedEnergy < 1.e-16, "removed delay/reverb machine history must not resurrect on immediate re-add");
}
void run(double rate) {
    auto storage = std::make_unique<RoutingExecutor>(); auto& engine = *storage;
    engine.prepare(rate);
    auto patch = graph();
    patch.nodes[1].creative.values[size_t(CreativeParameter::granularReverse)] = 1;
    expect(engine.requestGraph(patch).valid, "granular graph accepted");
    RoutingSources input; input.lead = {.2f, -.1f}; RoutingControls controls;
    CreativeEffect reference; reference.prepare(rate, CreativeKind::granular);
    uint64_t sample = 0;
    for (int block : {1, 7, 63, 128, 511, 8192}) {
        allocationCount = 0; trackAllocations = true;
        for (int i = 0; i < block; ++i) {
            engine.process(input, controls);
            reference.process(input.lead.left, input.lead.right, patch.nodes[1].creative, controls.tempoBpm);
        }
        const auto frame = engine.grainSnapshot();
        trackAllocations = false;
        expect(allocationCount == 0, "audio processing and grain copy allocate nothing");
        sample += uint64_t(block);
        const auto& node = frame.nodes[1];
        const auto actual = reference.grainSnapshot();
        expect(node.grains.sample == actual.sample && node.grains.count == actual.count
            && node.grains.captureHead == actual.captureHead && node.grains.validFraction == actual.validFraction,
            "routing copy matches same-input DSP snapshot exactly");
        expect(frame.revision == engine.requestedGrainRevision(), "active revision matches request");
        expect(node.nodeId == 100 && node.generation != 0, "stable identity and machine generation");
        expect(node.grains.sample == sample, "snapshot clocks actual processed samples");
        expect(node.grains.count <= 32, "bounded cloud");
        expect(node.grains.captureHead >= 0 && node.grains.captureHead < 1, "normalized capture head");
        expect(node.grains.validFraction >= 0 && node.grains.validFraction <= 1, "valid capture range");
        for (uint32_t i = 0; i < node.grains.count; ++i) {
            const auto& grain = node.grains.grains[i];
            const auto& expected = actual.grains[i];
            expect(grain.position == expected.position && grain.progress == expected.progress
                && grain.envelope == expected.envelope && grain.pan == expected.pan
                && grain.step == expected.step && grain.slot == expected.slot
                && grain.holdingBoundary == expected.holdingBoundary, "all grain fields copied from actual DSP");
            expect(grain.slot < 32 && grain.position >= 0 && grain.position < 1, "actual stable grain coordinates");
            expect(grain.progress >= 0 && grain.progress <= 1 && grain.envelope >= 0 && grain.envelope <= 1.00001f, "bounded progress/envelope");
            expect(grain.step < 0, "actual reverse playback");
        }
    }
    auto before = engine.grainSnapshot();
    expect(before.nodes[1].grains.count > 0, "actual cloud has grains");
    expect(engine.requestGraph(patch).valid, "republish same identity"); engine.process(input, controls);
    expect(engine.grainSnapshot().nodes[1].generation == before.nodes[1].generation, "topology republish retains machine");
    const auto epoch = engine.grainEpoch(); engine.reset();
    auto reset = engine.grainSnapshot();
    expect(reset.resetEpoch > epoch && reset.nodes[1].grains.count == 0 && reset.nodes[1].grains.capturedFrames == 0, "reset invalidates history");
    patch.nodes[1].creative.values[size_t(CreativeParameter::granularFreeze)] = 1;
    expect(engine.requestGraph(patch).valid, "freeze graph");
    for (int i = 0; i < 10000; ++i) engine.process(input, controls);
    const auto frozen = engine.grainSnapshot().nodes[1].grains;
    expect(frozen.frozen && !frozen.capturing, "thresholded DSP freeze, not parameter guess");
    for (int i = 0; i < 257; ++i) engine.process(input, controls);
    expect(engine.grainSnapshot().nodes[1].grains.capturedFrames == frozen.capturedFrames, "freeze holds capture");
    expect(engine.requestGraph(graph(NodeType::delay)).valid, "replace with delay"); engine.process(input, controls);
    expect(engine.grainSnapshot().nodes[1].nodeId == 0, "non-granular slot is empty");
    expect(engine.requestGraph(graph()).valid, "reuse slot"); engine.process(input, controls);
    expect(engine.grainSnapshot().nodes[1].generation != before.nodes[1].generation, "reused slot changes instance generation");
    const auto reused = engine.grainSnapshot().nodes[1].generation;
    expect(engine.requestGraph(defaultGraph()).valid, "remove graph");
    // Re-add before audio retires the old plan: must still be a new instance.
    expect(engine.requestGraph(graph()).valid, "immediate re-add"); engine.process(input, controls);
    expect(engine.grainSnapshot().nodes[1].generation != reused, "pending removal cannot resurrect a machine");
}
}
int main() {
    try {
            for (double rate : {44100., 48000., 96000.}) {
                effectHistoryLifecycle(NodeType::delay, rate);
                effectHistoryLifecycle(NodeType::reverb, rate);
                run(rate);
            }
        }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Grain snapshot and delay/reverb identity/history lifecycle tests passed\n";
}
