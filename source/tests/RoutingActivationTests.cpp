// Standalone regression test; compile as C++20 with source/src on the include path.
#include "Routing.h"
#include <iostream>
#include <stdexcept>

using namespace vocalstack;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Graph morphGraph() {
    return {{{1, NodeType::lead}, {14, NodeType::sidechain}, {100, NodeType::spectralMorph},
             {13, NodeType::output}}, {{1, 100, 0}, {14, 100, 1}, {100, 13, 0}}};
}

void graphContract() {
    constexpr int latency = SpectralMorph::latencySamples;
    auto g = morphGraph();
    g.nodes[2].mix = 0;
    check(RoutingExecutor::graphLatency(g) == latency, "enabled Morph at zero Mix still requires latency");
    g.links.pop_back();
    check(RoutingExecutor::graphLatency(g) == 0, "disconnected Morph contributed latency");
    RoutingExecutor r;
    r.prepare(48000);
    check(bool(r.requestGraph(g)), "disconnected Morph required a reserve");

    // A disabled latency-bearing node doesn't depend on DONOR even when
    // validated alongside an enabled upstream Morph. The old structural
    // serial budget still rejects this topology regardless of the bypass.
    g.nodes.push_back({101, NodeType::spectralMorph});
    g.nodes.back().enabled = false;
    g.links.insert(g.links.end(), {{1, 101, 0}, {100, 101, 1}, {101, 13, 0}});
    check(!RoutingExecutor::validate(g) && RoutingExecutor::graphLatency(g) == latency + 1,
          "bypass weakened the structural serial budget");
    g = morphGraph();
    g.nodes[2].enabled = false;
    check(RoutingExecutor::graphLatency(g) == 0, "disabled Morph reported latency");
    g.nodes[2].latencySamples = 1;
    check(!RoutingExecutor::validate(g), "authored latency accepted on a disabled node");
}

void morphTargets() {
    constexpr int latency = SpectralMorph::latencySamples;
    for (const double rate : {44100., 48000., 96000.}) {
        auto g = morphGraph();
        g.nodes[2].enabled = false;
        RoutingExecutor zero;
        zero.prepare(rate);
        check(bool(zero.requestGraph(g)), "disabled Morph requested a reserve");
        RoutingControls controls;
        controls.fxAutomation = true;
        auto& fx = controls.fxSettings[1];
        fx.enabled = true; fx.mix = 1.f;
        fx.spectralMorph.morph = 1.f;
        RoutingSources s;
        s.sidechainAvailable = true;
        s.sidechain = {-.2f, .1f};
        for (int i = 0; i < latency + 300; ++i) {
            s.lead = {i == 0 ? .3f : 0.f, i == 9 ? -.125f : 0.f};
            const auto out = zero.process(s, controls);
            check(out.left == s.lead.left && out.right == s.lead.right, "live on woke a prepared-disabled Morph");
        }
        check(zero.spectralSnapshots()[1].nodeId == 0, "disabled Morph published an FFT snapshot");

        g.nodes[2].enabled = true;
        RoutingExecutor reserved;
        reserved.prepare(rate, latency);
        g.links.push_back({1, 13, 0}); // Also test delay alignment at a merge.
        check(bool(reserved.requestGraph(g)), "enabled Morph rejected with the correct reserve");
        for (int i = 0; i < 2 * latency + 100; ++i) {
            // Build wet history first, then bypass without changing the plan.
            fx.enabled = i < latency;
            s.lead = {i == latency - 1 ? .3f : 0.f, i == latency + 11 ? -.125f : 0.f};
            const auto out = reserved.process(s, controls);
            if (i >= latency)
                check(out.left == (i == 2 * latency - 1 ? .3f : 0.f)
                    && out.right == (i == 2 * latency + 11 ? -.125f : 0.f),
                    "live off did not preserve exact aligned dry in a prepared-enabled plan");
        }
    }
}

void granularSleep() {
    Graph g{{{1, NodeType::lead}, {100, NodeType::granular}, {13, NodeType::output}},
            {{1, 100, 0}, {100, 13, 0}}};
    g.nodes[1].mix = 0;
    RoutingExecutor r;
    r.prepare(48000);
    check(bool(r.requestGraph(g)), "granular graph rejected");
    RoutingSources sources;
    sources.lead = {.2f, -.1f};
    RoutingControls c;
    c.fxAutomation = true;
    auto& fx = c.fxSettings[1];
    fx.mix = 0;
    for (int i = 0; i < 256; ++i) r.process(sources, c);
    check(r.grainSnapshot().nodes[1].grains.sample == 256, "enabled zero-Mix Granular did not run");
    fx.mix = 1;
    for (int i = 0; i < 4096; ++i) r.process(sources, c);
    const auto before = r.grainSnapshot().nodes[1].grains.sample;
    fx.enabled = false;
    for (int i = 0; i < 10000; ++i) r.process(sources, c);
    const auto sleeping = r.grainSnapshot().nodes[1].grains.sample;
    check(sleeping > before && sleeping < before + 10000, "Granular did not fade then sleep");
    for (int i = 0; i < 1024; ++i) {
        const auto out = r.process(sources, c);
        check(out.left == sources.lead.left && out.right == sources.lead.right, "settled Granular bypass was not dry");
    }
    check(r.grainSnapshot().nodes[1].grains.sample == sleeping, "sleeping Granular kept advancing");
    fx.enabled = true;
    r.process(sources, c);
    check(r.grainSnapshot().nodes[1].grains.sample == sleeping + 1, "Granular did not resume retained state");
}
} // namespace

int main() {
    try {
        graphContract();
        morphTargets();
        granularSleep();
        std::cout << "Routing activation tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
