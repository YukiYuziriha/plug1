#include "Routing.h"
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
Graph pocket(float mix, bool nested = true) {
    Graph graph{{{1, NodeType::lead}, {100, NodeType::granular}, {101, NodeType::delay}, {13, NodeType::output}},
                {{1, 100, 0}, {100, 13, 0}}};
    graph.nodes[1].mix = mix;
    graph.nodes[1].creative[CreativeParameter::granularSize] = 40;
    graph.nodes[1].creative[CreativeParameter::granularPosition] = 25;
    graph.nodes[1].creative[CreativeParameter::granularDensity] = 35;
    graph.nodes[2].mix = 1;
    graph.nodes[2].creative[CreativeParameter::delayTime] = 31;
    graph.nodes[2].creative[CreativeParameter::delaySync] = 0;
    graph.nodes[2].creative[CreativeParameter::delayFeedback] = 35;
    if (nested) graph.nodes[2].wetOwner = 100;
    else graph.links = {{1, 100, 0}, {100, 101, 0}, {101, 13, 0}};
    return graph;
}
void contracts() {
    const auto graph = pocket(1);
    check(bool(RoutingExecutor::validate(graph)), "Granular wet Delay rejected");
    auto bad = graph; bad.nodes[2].wetOwner = 999;
    check(!RoutingExecutor::validate(bad), "orphan wet insert accepted");
    bad = graph; bad.nodes[1].type = NodeType::reverb;
    check(!RoutingExecutor::validate(bad), "unsupported parent accepted");
    bad = graph; bad.nodes[2].type = NodeType::spectralMorph;
    check(!RoutingExecutor::validate(bad), "uncompensated latency insert accepted");
    bad = graph; bad.links.push_back({1, 101, 0});
    check(!RoutingExecutor::validate(bad), "canvas input into owned insert accepted");
    bad = graph; bad.links.push_back({101, 13, 0});
    check(!RoutingExecutor::validate(bad), "wet insert can leak into main output");
    bad = graph; auto child = bad.nodes[2]; child.id = 102; bad.nodes.push_back(child);
    check(!RoutingExecutor::validate(bad), "ambiguous two-insert pocket accepted");
}
void render(double rate, bool mono) {
    auto nested = std::make_unique<RoutingExecutor>();
    auto serial = std::make_unique<RoutingExecutor>();
    auto dry = std::make_unique<RoutingExecutor>();
    auto partial = std::make_unique<RoutingExecutor>();
    auto bypass = std::make_unique<RoutingExecutor>();
    auto noInsert = std::make_unique<RoutingExecutor>();
    auto silent = std::make_unique<RoutingExecutor>();
    for (auto* processor : {nested.get(), serial.get(), dry.get(), partial.get(), bypass.get(), noInsert.get(), silent.get()}) processor->prepare(rate);
    check(bool(nested->requestGraph(pocket(1))), "nested compile");
    check(bool(serial->requestGraph(pocket(1, false))), "serial reference compile");
    check(bool(dry->requestGraph(pocket(0))), "dry compile");
    check(bool(partial->requestGraph(pocket(.3f))), "partial compile");
    auto bypassGraph = pocket(1); bypassGraph.nodes[2].enabled = false;
    check(bool(bypass->requestGraph(bypassGraph)), "bypassed insert compile");
    auto plain = pocket(1); plain.nodes.erase(plain.nodes.begin() + 2);
    check(bool(noInsert->requestGraph(plain)), "plain granular compile");
    check(bool(silent->requestGraph(pocket(1))), "silence compile");
    RoutingSources sources; RoutingControls controls;
    double wetEnergy = 0, difference = 0;
    // The sample API is block-independent; these chunks model irregular host boundaries.
    constexpr int blocks[]{1, 137, 3, 511, 17, 63, 257};
    int position = 0, block = 0;
    while (position < int(rate * .7)) {
        const int count = blocks[block++ % 7];
        audio = true;
        for (int j = 0; j < count; ++j, ++position) {
            const float left = position < int(rate * .3) ? .2f * std::sin(float(position * 123. * 6.283185307 / rate)) : 0.f;
            const float right = mono ? left : left * -.7f;
            sources.lead = {left, right};
            const auto a = nested->process(sources, controls), b = serial->process(sources, controls);
            const auto d = dry->process(sources, controls), p = partial->process(sources, controls);
            const auto x = bypass->process(sources, controls), y = noInsert->process(sources, controls);
            const auto z = silent->process(RoutingSources{}, controls);
            check(std::isfinite(a.left) && std::isfinite(a.right), "nonfinite pocket output");
            check(std::abs(a.left - b.left) < 2.e-6f && std::abs(a.right - b.right) < 2.e-6f, "wet pocket differs from serial fully-wet reference");
            check(d.left == left && d.right == right, "Delay escaped Granular dry path");
            check(std::abs(p.left - (left + .3f * (a.left - left))) < 2.e-6f, "Granular Mix did not control entire wet pocket");
            check(std::abs(x.left - y.left) < 2.e-6f, "insert bypass altered Granular");
            check(z.left == 0 && z.right == 0, "silence generated output");
            wetEnergy += double(a.left) * a.left;
            difference += std::abs(a.left - y.left);
        }
        audio = false;
    }
    check(wetEnergy > 1.e-5 && difference > .01, "Delay reference was vacuous");
    for (auto* processor : {nested.get(), serial.get(), dry.get(), partial.get()}) processor->reset();
    check(!allocations && !destructions, "audio-thread allocation or reclamation");
}
int main() {
    try { contracts(); for (double rate : {44100., 48000., 96000.}) for (bool mono : {false, true}) render(rate, mono);
        std::cout << "Granular wet Delay: serial reference, dry isolation, Mix, bypass, silence, stereo/rates/irregular blocks and RT allocation checks passed\n"; return 0;
    } catch (const std::exception& e) { audio = false; std::cerr << e.what() << '\n'; return 1; }
}
