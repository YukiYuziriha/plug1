#include "Routing.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
namespace { thread_local bool audio = false; unsigned allocations = 0, destructions = 0;
void check(bool x, const char* message) { if (!x) throw std::runtime_error(message); }
}
void* operator new(std::size_t n) { if (audio) ++allocations; if (auto* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { if (audio && p) ++destructions; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
using namespace vocalstack;
RoutingNode effect(std::uint32_t id, NodeType type, float mix) {
    using P = CreativeParameter;
    RoutingNode node{id, type};
    node.mix = node.waveshaper.mix = mix;
    switch (type) {
    case NodeType::granular:
        node.creative[P::granularSize] = 40;
        node.creative[P::granularPosition] = 25;
        node.creative[P::granularDensity] = 35;
        break;
    case NodeType::delay:
        node.creative[P::delayTime] = 31;
        node.creative[P::delaySync] = 0;
        node.creative[P::delayFeedback] = 35;
        break;
    case NodeType::reverb:
        node.creative[P::reverbPredelay] = 11;
        node.creative[P::reverbDecay] = .48f;
        break;
    case NodeType::waveshaper: node.waveshaper.driveDb = 9; break;
    case NodeType::gain: node.utility[UtilityParameter::gainDb] = -6; break;
    case NodeType::equalizer: node.eq.bands[0] = {true, 350, -9, .707f, 0}; break;
    case NodeType::spectralMorph: node.spectralMorph.morph = .65f; break;
    default: break;
    }
    return node;
}
Graph pocket(float mix, NodeType owner = NodeType::granular, NodeType insert = NodeType::delay) {
    auto child = effect(101, insert, 1);
    child.wetOwner = 100;
    return {{{1, NodeType::lead}, effect(100, owner, mix), child, {13, NodeType::output}},
            {{1, 100, 0}, {100, 13, 0}}};
}
StereoSample signal(int position, double rate, bool mono = false) {
    if (position < 0 || position >= int(rate * .3)) return {};
    constexpr double tau = 6.283185307179586;
    const float left = .2f * std::sin(float(position * 123. * tau / rate));
    return {left, mono ? left : .13f * std::cos(float(position * 467. * tau / rate))};
}
void near(StereoSample actual, StereoSample expected, const char* message) {
    check(std::abs(actual.left - expected.left) < 2.e-6f &&
          std::abs(actual.right - expected.right) < 2.e-6f, message);
}
void contracts() {
    const auto graph = pocket(1);
    check(bool(RoutingExecutor::validate(graph)), "Granular wet Delay rejected");
    auto bad = graph; bad.nodes[2].wetOwner = 999;
    check(!RoutingExecutor::validate(bad), "orphan wet insert accepted");
    check(bool(RoutingExecutor::validate(pocket(1, NodeType::reverb, NodeType::delay))), "Reverb owner rejected");
    check(bool(RoutingExecutor::validate(pocket(1, NodeType::delay, NodeType::reverb))), "Reverb insert rejected");
    bad = graph; bad.nodes[1].type = NodeType::crossover;
    check(!RoutingExecutor::validate(bad), "Crossover owner accepted");
    bad = graph; bad.nodes[2].type = NodeType::crossover;
    check(!RoutingExecutor::validate(bad), "Crossover insert accepted");
    bad = pocket(1, NodeType::gain, NodeType::spectralMorph);
    check(bool(RoutingExecutor::validate(bad)), "Morph wet insert rejected");
    bad.nodes.push_back({sidechainNodeId, NodeType::sidechain});
    bad.links.push_back({sidechainNodeId, 101, 1});
    check(!RoutingExecutor::validate(bad), "donor cable into Morph wet insert accepted");
    bad = pocket(1, NodeType::spectralMorph, NodeType::spectralMorph);
    check(!RoutingExecutor::validate(bad), "nested Morphs exceeded fixed latency budget");
    bad = graph; bad.links.push_back({1, 101, 0});
    check(!RoutingExecutor::validate(bad), "canvas input into owned insert accepted");
    bad = graph; bad.links.push_back({101, 13, 0});
    check(!RoutingExecutor::validate(bad), "wet insert can leak into main output");
    bad = graph; auto child = bad.nodes[2]; child.id = 102; bad.nodes.push_back(child);
    check(!RoutingExecutor::validate(bad), "two children in one wet pocket accepted");
    bad.nodes.back().wetOwner = 101;
    check(!RoutingExecutor::validate(bad), "wet insert owning another insert accepted");
}
void render(double rate, bool mono, NodeType owner, NodeType insert) {
    const auto wetGraph = pocket(1, owner, insert);
    const int latency = routingDeclaredLatency(owner) + routingDeclaredLatency(insert);
    check(RoutingExecutor::graphLatency(wetGraph) == latency, "incorrect pocket latency");
    auto nested = std::make_unique<RoutingExecutor>();
    auto serial = std::make_unique<RoutingExecutor>();
    auto dry = std::make_unique<RoutingExecutor>();
    auto partial = std::make_unique<RoutingExecutor>();
    auto bypass = std::make_unique<RoutingExecutor>();
    auto noInsert = std::make_unique<RoutingExecutor>();
    auto silent = std::make_unique<RoutingExecutor>();
    auto ownerBypass = std::make_unique<RoutingExecutor>();
    for (auto* processor : {nested.get(), serial.get(), dry.get(), partial.get(), bypass.get(), noInsert.get(), silent.get(), ownerBypass.get()})
        processor->prepare(rate, latency);
    check(bool(nested->requestGraph(wetGraph)), "nested compile");
    auto serialGraph = wetGraph;
    serialGraph.nodes[2].wetOwner = 0;
    serialGraph.links = {{1, 100, 0}, {100, 101, 0}, {101, 13, 0}};
    check(bool(serial->requestGraph(serialGraph)), "serial reference compile");
    const auto dryGraph = pocket(0, owner, insert);
    check(RoutingExecutor::graphLatency(dryGraph) == latency, "zero Mix discarded prepared latency");
    check(bool(dry->requestGraph(dryGraph)), "dry compile");
    check(bool(partial->requestGraph(pocket(.3f, owner, insert))), "partial compile");
    auto bypassGraph = wetGraph; bypassGraph.nodes[2].enabled = false;
    check(RoutingExecutor::graphLatency(bypassGraph) == routingDeclaredLatency(owner), "disabled insert still contributes prepared latency");
    check(bool(bypass->requestGraph(bypassGraph)), "bypassed insert compile");
    auto plain = wetGraph; plain.nodes.erase(plain.nodes.begin() + 2);
    check(bool(noInsert->requestGraph(plain)), "unowned reference compile");
    check(bool(silent->requestGraph(wetGraph)), "silence compile");
    auto ownerOff = wetGraph; ownerOff.nodes[1].enabled = false;
    check(RoutingExecutor::graphLatency(ownerOff) == routingDeclaredLatency(insert), "owner bypass discarded insert latency");
    check(bool(ownerBypass->requestGraph(ownerOff)), "bypassed owner compile");
    RoutingSources sources; RoutingControls controls;
    double wetEnergy = 0, leftDifference = 0, rightDifference = 0;
    // The sample API is block-independent; these chunks model irregular host boundaries.
    constexpr int blocks[]{1, 137, 3, 511, 17, 63, 257};
    int position = 0, block = 0;
    while (position < int(rate * .7)) {
        const int count = blocks[block++ % 7];
        audio = true;
        for (int j = 0; j < count; ++j, ++position) {
            sources.lead = signal(position, rate, mono);
            const auto aligned = signal(position - latency, rate, mono);
            const auto a = nested->process(sources, controls), b = serial->process(sources, controls);
            const auto d = dry->process(sources, controls), p = partial->process(sources, controls);
            const auto x = bypass->process(sources, controls), y = noInsert->process(sources, controls);
            const auto z = silent->process(RoutingSources{}, controls);
            const auto off = ownerBypass->process(sources, controls);
            check(std::isfinite(a.left) && std::isfinite(a.right), "nonfinite pocket output");
            near(a, b, "wet pocket differs from serial fully-wet reference");
            check(d.left == aligned.left && d.right == aligned.right, "wet insert leaked into or misaligned the dry path");
            near(p, {aligned.left + .3f * (a.left - aligned.left), aligned.right + .3f * (a.right - aligned.right)},
                 "owner Mix did not control entire wet pocket");
            near(x, y, "insert bypass changed the owner output or latency");
            check(off.left == aligned.left && off.right == aligned.right, "owner bypass did not preserve aligned dry");
            check(z.left == 0 && z.right == 0, "silence generated output");
            wetEnergy += double(a.left) * a.left + double(a.right) * a.right;
            leftDifference += std::abs(a.left - y.left);
            rightDifference += std::abs(a.right - y.right);
        }
        audio = false;
    }
    check(wetEnergy > 1.e-5 && leftDifference > .01 && rightDifference > .01, "wet reference was vacuous");
    for (auto* processor : {nested.get(), serial.get(), dry.get(), partial.get()}) processor->reset();
    check(!allocations && !destructions, "audio-thread allocation or reclamation");
}
void donorlessMorph() {
    constexpr double rate = 48000;
    constexpr int latency = SpectralMorph::latencySamples;
    auto graph = pocket(1, NodeType::gain, NodeType::spectralMorph);
    graph.nodes[1].utility[UtilityParameter::gainDb] = 0;
    graph.nodes[2].spectralMorph.morph = 1;
    graph.nodes[2].spectralMorph.freeze = true;
    graph.nodes.push_back({sidechainNodeId, NodeType::sidechain});
    auto processor = std::make_unique<RoutingExecutor>();
    processor->prepare(rate);
    check(!processor->requestGraph(graph), "Morph insert accepted without a latency reserve");
    processor->prepare(rate, latency);
    check(bool(processor->requestGraph(graph)), "donorless Morph pocket compile");

    RoutingSources sources;
    sources.sidechainAvailable = true;
    sources.sidechain = {.7f, -.8f};
    RoutingControls controls;
    controls.fxAutomation = true;
    auto& owner = controls.fxSettings[routingFxSlot(100)];
    owner.mix = 1;
    auto& child = controls.fxSettings[routingFxSlot(101)];
    child.mix = 1;
    child.spectralMorph = graph.nodes[2].spectralMorph;
    constexpr int liveBypassAt = latency * 3;
    audio = true;
    for (int i = 0; i < liveBypassAt + 1024; ++i) {
        sources.lead = signal(i, rate);
        child.enabled = i < liveBypassAt;
        const auto actual = processor->process(sources, controls);
        const auto expected = child.enabled ? StereoSample{} : signal(i - latency, rate);
        near(actual, expected, "Morph acquired a donor or live bypass lost stereo alignment");
    }
    audio = false;
    const auto frame = processor->spectralSnapshots()[routingFxSlot(101)];
    check(frame.nodeId == 101 && frame.spectrum.revision > 0, "owned Morph did not publish analysis");
    check(!frame.spectrum.donorConnected && frame.spectrum.capturePending && !frame.spectrum.frozen,
          "owned Morph fulfilled Freeze without a donor cable");
    check(processor->additionalLatencySamples() == latency, "live bypass changed prepared reserve");
}

void legacyChorale() {
    constexpr double rate = 48000;
    auto processor = std::make_unique<RoutingExecutor>();
    processor->prepare(rate);
    Chorale reference;
    reference.prepare(rate);
    RoutingControls controls;
    controls.mix = 0;
    controls.choraleMix = .42f;
    controls.choraleMotion = .72f;
    RoutingSources sources;
    double difference = 0;
    audio = true;
    for (int i = 0; i < 17000; ++i) {
        controls.choraleOn = i < 8000;
        controls.fxAutomation = i >= 4000;
        controls.fxSettings[0].enabled = !controls.choraleOn;
        controls.fxSettings[0].mix = 1;
        sources.lead = signal(i, rate);
        auto expected = sources.lead;
        reference.process(expected.left, expected.right, controls.choraleOn, controls.choraleMix, controls.choraleMotion);
        near(processor->process(sources, controls), expected, "legacy Chorale controls or bypass changed routed audio");
        difference += std::abs(expected.left - sources.lead.left);
    }
    audio = false;
    check(difference > .01, "legacy Chorale reference was vacuous");
}

int main() {
    const char* test = "graph contracts";
    try {
        contracts();
        test = "Granular -> Delay";
        for (double rate : {44100., 48000., 96000.})
            for (bool mono : {false, true}) render(rate, mono, NodeType::granular, NodeType::delay);
        struct Pair { const char* name; NodeType owner, insert; };
        constexpr Pair pairs[] = {
            {"Delay -> Reverb", NodeType::delay, NodeType::reverb},
            {"Reverb -> Chorale", NodeType::reverb, NodeType::chorale},
            {"Waveshaper -> Gain", NodeType::waveshaper, NodeType::gain},
            {"Gain -> EQ", NodeType::gain, NodeType::equalizer},
            {"Gain -> Morph", NodeType::gain, NodeType::spectralMorph},
            {"Morph -> Gain", NodeType::spectralMorph, NodeType::gain},
        };
        for (const auto& pair : pairs) {
            test = pair.name;
            render(48000, false, pair.owner, pair.insert);
        }
        test = "donorless Morph";
        donorlessMorph();
        test = "legacy Chorale";
        legacyChorale();
        check(!allocations && !destructions, "audio-thread allocation or reclamation");
        std::cout << "Wet-pocket graph, stereo rendering, Morph latency/donor and legacy Chorale checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        audio = false;
        std::cerr << test << ": " << e.what() << '\n';
        return 1;
    }
}
