#pragma once

// Included by RoutingBackendTests.cpp to share its checks and RT allocation guard.
namespace routingSplitter {
Graph graph(int port = 0) {
    return {{{1, NodeType::lead}, {300, NodeType::splitter}, {13, NodeType::output}},
            {{1, 300, 0}, {300, 13, 0, port}}};
}
StereoSample render(RoutingExecutor& executor, const RoutingSources& sources,
                    const RoutingControls& controls = {}) {
    audio = true;
    const auto result = executor.process(sources, controls);
    audio = false;
    return result;
}
void same(StereoSample actual, StereoSample expected, const char* message) {
    near(actual.left, expected.left, message);
    near(actual.right, expected.right, message);
}
void outputsAndMerges() {
    static_assert(int(NodeType::splitter) == 19);
    static_assert(RoutingExecutor::maxNodes == 48 && RoutingExecutor::maxEffects == 16
                  && RoutingExecutor::maxLinks == 128);
    for (double rate : {32000., 44100., 48000., 96000., 192000.}) {
        for (bool stereo : {false, true}) for (int port : {0, 1}) {
            RoutingExecutor executor; executor.prepare(rate);
            auto g = graph(port);
            // These are not effect controls; neither may attenuate a splitter.
            g.nodes[1].enabled = false; g.nodes[1].mix = 0;
            check(bool(executor.requestGraph(g)), "splitter output rejected");
            for (int i = 0; i < 1000; ++i) {
                RoutingSources sources;
                sources.lead = {float(.3 * std::sin(i * .071)),
                    stereo ? float(-.2 * std::cos(i * .043)) : float(.3 * std::sin(i * .071))};
                same(render(executor, sources), sources.lead, "splitter output is not full-level identity");
            }
            check(executor.tailSeconds() == 0, "splitter added a tail");
            executor.reset();
            same(render(executor, {}), {}, "splitter retained audio after reset");
        }
    }
    for (bool legacySum : {false, true}) {
        RoutingExecutor executor; executor.prepare(48000);
        auto g = graph();
        g.links.push_back({300, 13, 0, 1});
        // A nested splitter must not invent a new source identity.
        g.nodes.push_back({301, NodeType::splitter});
        g.links.push_back({300, 301, 0, 1});
        g.links.push_back({301, 13, 0, 0});
        g.links.push_back({301, 13, 0, 1});
        if (legacySum) {
            g.nodes.push_back({20, NodeType::sum});
            int pin = 0;
            for (auto& l : g.links) if (l.destination == 13) {
                l.destination = 20; l.inputPin = pin++;
            }
            g.links.push_back({20, 13, 0});
        }
        check(bool(executor.requestGraph(g)), "splitter reconvergence rejected");
        RoutingSources sources; sources.lead = {.4f, -.2f};
        same(render(executor, sources), sources.lead, "coherent splitter merge changed level");
        // Splitter input itself supports normal automatic fan-in on its one pin.
        g.nodes.push_back({2, NodeType::voice, 0});
        g.links.push_back({2, 300, 0});
        check(bool(executor.requestGraph(g)), "splitter input fan-in rejected");
        sources.voices[0] = {-.1f, .3f}; sources.activity[0] = 1;
        StereoSample out;
        for (int i = 0; i < 600; ++i) out = render(executor, sources);
        same(out, {(.4f - .1f) / std::sqrt(2.f), (-.2f + .3f) / std::sqrt(2.f)},
             "splitter input fan-in lost independent-source provenance");
    }
}
void independentProcessing() {
    RoutingExecutor executor; executor.prepare(48000);
    Graph g{{{1, NodeType::lead}, {300, NodeType::splitter}, {100, NodeType::chorale},
             {101, NodeType::chorale}, {13, NodeType::output}},
            {{1, 300, 0}, {300, 100, 0, 0}, {300, 101, 0, 1}, {100, 13, 0}, {101, 13, 0}}};
    g.nodes[2].mix = g.nodes[3].mix = 1;
    g.nodes[2].motion = .1f; g.nodes[3].motion = .9f;
    check(bool(executor.requestGraph(g)), "splitter effect branches rejected");
    Chorale first, second; first.prepare(48000); second.prepare(48000);
    bool different = false;
    for (int i = 0; i < 5000; ++i) {
        RoutingSources sources; sources.lead = {float(.3 * std::sin(i * .053)), float(.2 * std::cos(i * .029))};
        auto a = sources.lead, b = sources.lead;
        first.process(a.left, a.right, true, 1, .1f);
        second.process(b.left, b.right, true, 1, .9f);
        different |= std::abs(a.left - b.left) > 1e-4f;
        same(render(executor, sources), {(a.left + b.left) * .5f, (a.right + b.right) * .5f},
             "splitter branches share mutable audio/history or lose merge compensation");
    }
    check(different, "independence test did not produce different branch processing");
    // Ignore one processed branch at Output: its DSP must not mutate the other port.
    g.links = {{1, 300, 0}, {300, 100, 0, 0}, {100, 101, 0}, {300, 13, 0, 1}};
    check(bool(executor.requestGraph(g)), "independent dry branch rejected");
    RoutingSources sources; sources.lead = {.25f, -.125f};
    for (int i = 0; i < 600; ++i) render(executor, sources);
    same(render(executor, sources), sources.lead, "processed branch mutated splitter sibling");
}
void crossoverProvenance() {
    for (int bands : {2, 3}) for (bool beforeCrossover : {false, true}) {
        RoutingExecutor executor; executor.prepare(48000);
        Graph g{{{1, NodeType::lead}, {100, NodeType::crossover},
                 {300, NodeType::splitter}, {13, NodeType::output}}, {}};
        g.nodes[1].bandCount = bands;
        if (beforeCrossover) {
            // Identical crossovers on splitter branches partition the same regions.
            g.nodes.push_back({101, NodeType::crossover}); g.nodes.back().bandCount = bands;
            g.links = {{1, 300, 0}, {300, 100, 0, 0}, {300, 101, 0, 1}};
            for (int p = 0; p < bands; ++p) {
                g.links.push_back({100, 13, 0, p}); g.links.push_back({101, 13, 0, p});
            }
        } else {
            // Duplicate just the low band: its provenance must stay band-limited,
            // or the complementary mid/high branches would be attenuated.
            g.links = {{1, 100, 0}, {100, 300, 0, 0}, {300, 13, 0, 0}, {300, 13, 0, 1}};
            for (int p = 1; p < bands; ++p) g.links.push_back({100, 13, 0, p});
        }
        check(bool(executor.requestGraph(g)), "crossover splitter graph rejected");
        Multiband reference; reference.prepare(48000);
        for (int i = 0; i < 4000; ++i) {
            RoutingSources sources; sources.lead = {float(.2 * std::sin(i * .031)), float(.1 * std::cos(i * .139))};
            const auto b = reference.process(sources.lead.left, sources.lead.right, bands);
            same(render(executor, sources), {b[0][0] + b[1][0] + b[2][0], b[0][1] + b[1][1] + b[2][1]},
                 "splitter lost crossover region provenance");
        }
        check(executor.tailSeconds() == 8, "splitter added or doubled crossover tail");
    }
}
void validationAndTails() {
    auto bad = graph(); bad.links.back().sourcePin = 2;
    check(!RoutingExecutor::validate(bad), "third splitter output accepted");
    bad = graph(); bad.links.back().sourcePin = -1;
    check(!RoutingExecutor::validate(bad), "negative splitter output accepted");
    bad = graph(); bad.links.front().inputPin = 1;
    check(!RoutingExecutor::validate(bad), "second splitter input accepted");
    bad = graph(); bad.links.push_back({300, 300, 0, 1});
    check(!RoutingExecutor::validate(bad), "splitter self-cycle accepted");
    bad = graph(); bad.nodes.push_back({301, NodeType::splitter});
    bad.links.push_back({300, 301, 0, 1}); bad.links.push_back({301, 300, 0, 1});
    check(!RoutingExecutor::validate(bad), "splitter cycle accepted");
    bad = graph(); bad.links.push_back(bad.links.back());
    check(!RoutingExecutor::validate(bad), "duplicate splitter connection accepted");
    auto bounded = graph();
    for (int i = 0; i < 16; ++i) bounded.nodes.push_back({std::uint32_t(100 + i), NodeType::chorale});
    for (int i = 1; i < 30; ++i) bounded.nodes.push_back({std::uint32_t(300 + i), NodeType::splitter});
    check(bounded.nodes.size() == 48 && bool(RoutingExecutor::validate(bounded)),
          "splitters consumed effect slots or lowered node limit");
    RoutingExecutor executor; executor.prepare(48000);
    check(bool(executor.requestGraph(bounded)), "48-node splitter plan construction failed");
    RoutingSources boundedSources; boundedSources.lead = {.2f, -.1f};
    same(render(executor, boundedSources), boundedSources.lead, "48-node splitter execution failed");
    bounded.nodes.back().type = NodeType::chorale;
    check(!RoutingExecutor::validate(bounded), "splitters allowed a seventeenth effect");
    bounded.nodes.back().type = NodeType::splitter;
    bounded.nodes.push_back({400, NodeType::splitter});
    check(!RoutingExecutor::validate(bounded), "splitter exceeded node limit");
    auto g = graph(1);
    g.nodes.push_back({100, NodeType::chorale});
    g.nodes.push_back({101, NodeType::equalizer});
    g.links = {{1, 100, 0}, {100, 300, 0}, {300, 101, 0, 0}, {101, 13, 0}, {300, 13, 0, 1}};
    check(bool(executor.requestGraph(g)), "splitter tail graph rejected");
    render(executor, {});
    check(std::abs(executor.tailSeconds() - 256.040) < 1e-9, "splitter longest-path tail incorrect");
    g.links.erase(g.links.begin() + 3); // EQ is now a dead end.
    check(bool(executor.requestGraph(g)), "splitter disconnected tail graph rejected");
    render(executor, {});
    check(std::abs(executor.tailSeconds() - .040) < 1e-9, "dead splitter branch contributed tail");
    g = graph(1); g.links.erase(g.links.begin());
    check(bool(executor.requestGraph(g)), "disconnected splitter rejected");
    for (int i = 0; i < 600; ++i) render(executor, {});
    same(render(executor, {}), {}, "disconnected splitter is not silent");
}
void rewiringAndCapacity() {
    RoutingExecutor executor; executor.prepare(48000);
    auto g = graph();
    RoutingSources sources; sources.lead = {.3f, -.2f};
    sources.voices[0] = {-.15f, .1f}; sources.activity[0] = 1;
    check(bool(executor.requestGraph(g)), "splitter rewiring fixture rejected");
    same(render(executor, sources), sources.lead, "initial splitter route incorrect");
    g.nodes.push_back({2, NodeType::voice, 0});
    g.links.front().source = 2;
    g.links.back().sourcePin = 1;
    check(bool(executor.requestGraph(g)), "splitter input/output rewire rejected");
    for (int i = 0; i < 600; ++i) render(executor, sources);
    same(render(executor, sources), sources.voices[0], "splitter rewire retained old source/audio");
    auto invalid = g; invalid.links.push_back({300, 300, 0});
    check(!executor.requestGraph(invalid), "invalid live splitter edit accepted");
    same(render(executor, sources), sources.voices[0], "invalid edit replaced active splitter plan");
    g.links.pop_back();
    check(bool(executor.requestGraph(g)), "splitter output disconnect rejected");
    for (int i = 0; i < 600; ++i) render(executor, sources);
    same(render(executor, sources), {}, "splitter disconnect retained old audible output");

    Graph capacity{{{1, NodeType::lead}, {13, NodeType::output}}, {}};
    for (int i = 0; i < 16; ++i) {
        capacity.nodes.push_back({std::uint32_t(300 + i), NodeType::splitter});
        capacity.links.push_back({1, std::uint32_t(300 + i), 0});
    }
    for (int target = 0; target < 7; ++target) {
        const auto id = target == 6 ? 13u : std::uint32_t(400 + target);
        if (target != 6) capacity.nodes.push_back({id, NodeType::splitter});
        for (int source = 0; source < 16; ++source)
            capacity.links.push_back({std::uint32_t(300 + source), id, 0, source % 2});
    }
    check(capacity.links.size() == 128 && bool(executor.requestGraph(capacity)),
          "128-link/16-input splitter graph rejected");
    for (int i = 0; i < 600; ++i) render(executor, sources);
    same(render(executor, sources), sources.lead, "maximum fan-in lost coherent provenance");
    capacity.links.push_back({300, 400, 0, 1});
    check(!RoutingExecutor::validate(capacity), "129-link splitter graph accepted");
    capacity.links.erase(capacity.links.begin() + 1); // Now 128 links, but 17 on one pin.
    check(!RoutingExecutor::validate(capacity), "17-input splitter pin accepted");
}
void run() {
    outputsAndMerges(); independentProcessing(); crossoverProvenance(); validationAndTails(); rewiringAndCapacity();
    check(allocations == 0 && destructions == 0, "splitter processing allocated/destroyed on audio");
}
} // namespace routingSplitter
