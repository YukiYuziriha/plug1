#pragma once

// Included by RoutingBackendTests.cpp; shares the RT allocation/destruction guard.
namespace dryWetRouting {
void run() {
    static_assert(RoutingExecutor::maxNodes == 48 && RoutingExecutor::maxEffects == 16 && RoutingExecutor::maxLinks == 128);
    for (double rate : {32000., 44100., 48000., 96000., 192000.}) for (bool stereo : {false, true}) {
        RoutingExecutor executor; executor.prepare(rate);
        Graph g{{{1, NodeType::lead}, {2, NodeType::voice, 0}, {200, NodeType::mix}, {13, NodeType::output}},
                {{1, 200, 0}, {2, 200, 1}, {200, 13, 0}}};
        RoutingSources sources; sources.lead = {.4f, stereo ? -.2f : .4f};
        sources.voices[0] = {-.1f, stereo ? .3f : -.1f}; sources.activity[0] = 1;
        RoutingControls controls; controls.localMixAutomation = true;
        for (float balance : {0.f, .5f, 1.f}) {
            controls.localMix[0] = balance;
            check(bool(executor.requestGraph(g)), "Dry/Wet two-input graph rejected");
            StereoSample out;
            for (int i = 0; i < int(rate * .15); ++i) out = routingSplitter::render(executor, sources, controls);
            routingSplitter::same(out, {sources.lead.left * (1-balance) + sources.voices[0].left * balance,
                                       sources.lead.right * (1-balance) + sources.voices[0].right * balance}, "Dry/Wet linear endpoints/midpoint incorrect");
        }
        // Control automation is bounded/smoothed, finite, and independent.
        controls.localMix[0] = std::numeric_limits<float>::quiet_NaN();
        auto out = routingSplitter::render(executor, sources, controls);
        check(std::isfinite(out.left) && std::isfinite(out.right), "Dry/Wet nonfinite automation escaped");
        controls.localMix[0] = .5f;
        g.links.erase(g.links.begin() + 1); // Missing Wet must be silence, not a copy of Dry.
        check(bool(executor.requestGraph(g)), "Unconnected Dry/Wet input rejected");
        for (int i = 0; i < int(rate * .15); ++i) out = routingSplitter::render(executor, sources, controls);
        routingSplitter::same(out, {sources.lead.left * .5f, sources.lead.right * .5f}, "Unconnected Wet is not silence");
        g.links.insert(g.links.begin() + 1, {1, 200, 1}); // Correlated inputs stay unity, not equal-power +3dB.
        check(bool(executor.requestGraph(g)), "Correlated Dry/Wet inputs rejected");
        for (int i = 0; i < int(rate * .15); ++i) out = routingSplitter::render(executor, sources, controls);
        routingSplitter::same(out, sources.lead, "Identical Dry/Wet midpoint changed unity gain");
        executor.reset(); routingSplitter::same(routingSplitter::render(executor, {}, controls), {}, "Dry/Wet reset retained audio");
        auto invalid = g; invalid.nodes[2].latencySamples = 1;
        check(!executor.requestGraph(invalid), "Unsupported Dry/Wet latency accepted");
        invalid = g; invalid.links[0].inputPin = 2;
        check(!executor.requestGraph(invalid), "Third Dry/Wet input accepted");
        invalid = g; invalid.links[0].sourcePin = 1;
        check(!executor.requestGraph(invalid), "Nonexistent source output accepted");
        invalid = g; invalid.links.push_back({200, 200, 0});
        check(!executor.requestGraph(invalid), "Dry/Wet feedback accepted");
    }
    // A full 48-node DAG: 45 distinct Dry/Wet balances plus two sources and Output.
    // Every mixer is audible in the chain and none consumes one of the 16 FX slots.
    Graph full{{{1, NodeType::lead}, {2, NodeType::voice, 0}, {13, NodeType::output}}, {}};
    RoutingControls controls; controls.localMixAutomation = true;
    for (uint32_t id = 200; id < 245; ++id) {
        full.nodes.push_back({id, NodeType::mix});
        full.links.push_back({id == 200 ? 1 : id - 1, id, 0}); full.links.push_back({2, id, 1});
        controls.localMix[id - 200] = id % 3 == 0 ? .02f : id % 3 == 1 ? .03f : .04f;
    }
    full.links.push_back({244, 13, 0});
    check(bool(RoutingExecutor::validate(full)), "Full-capacity repeated Dry/Wet graph rejected");
    RoutingExecutor executor; executor.prepare(48000); check(bool(executor.requestGraph(full)), "Full-capacity Dry/Wet install failed");
    RoutingSources sources; sources.lead = {.4f, -.2f}; sources.voices[0] = {-.1f, .3f}; sources.activity[0] = 1;
    auto expected = sources.lead;
    for (int slot = 0; slot < 45; ++slot) {
        const float m = controls.localMix[size_t(slot)];
        expected = {expected.left * (1-m) + sources.voices[0].left * m, expected.right * (1-m) + sources.voices[0].right * m};
    }
    StereoSample out;
    for (int i = 0; i < 1000; ++i) out = routingSplitter::render(executor, sources, controls);
    routingSplitter::same(out, expected, "Full-capacity independent balances lost their signal/settings");
    full.nodes.push_back({245, NodeType::mix});
    check(!executor.requestGraph(full), "49 total Dry/Wet graph nodes accepted");
    check(allocations == 0 && destructions == 0, "Dry/Wet audio allocated or destroyed memory");
}
}
