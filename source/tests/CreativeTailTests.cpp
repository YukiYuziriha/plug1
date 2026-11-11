// Standalone C++20 test; no JUCE or project libraries required.
#include "../src/Routing.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
thread_local bool audio = false;
unsigned allocations = 0, frees = 0;
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    check(std::isfinite(actual) && std::abs(actual - expected) < 1.e-8, message);
}
}
void* operator new(std::size_t n) {
    if (audio) ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (audio && p) ++frees;
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

using namespace vocalstack;
using P = CreativeParameter;
namespace {
Graph single(NodeType type, std::uint32_t id = 100) {
    return {{{1, NodeType::lead}, {id, type}, {2, NodeType::output}},
            {{1, id, 0}, {id, 2, 0}}};
}
void render(RoutingExecutor& r, const RoutingControls& controls = {}, int count = 1) {
    audio = true;
    for (int i = 0; i < count; ++i) r.process({}, controls);
    audio = false;
}
void install(RoutingExecutor& r, const Graph& g, const RoutingControls& controls = {}) {
    const double before = r.tailSeconds();
    check(bool(r.requestGraph(g)), "graph rejected");
    check(r.tailSeconds() == before, "pending graph changed published tail");
    render(r, controls);
}
double delayTail(double time, double feedback, double window = 0) {
    return (time + window) * (1 + (feedback > 0 ? std::ceil(std::log(1.e-4) / std::log(feedback)) : 0)) + .25;
}
void delayControls() {
    RoutingExecutor r; r.prepare(48000);
    near(r.tailSeconds(), .040, "legacy default changed");
    auto g = single(NodeType::delay);
    auto& s = g.nodes[1].creative;
    s[P::delayTime] = 100; s[P::delayFeedback] = 0;
    install(r, g); near(r.tailSeconds(), .35, "zero-feedback first echo");
    s[P::delayFeedback] = 50;
    install(r, g); near(r.tailSeconds(), delayTail(.1, .5), "feedback decay");
    s[P::delayPitch] = -12;
    install(r, g); near(r.tailSeconds(), delayTail(.1, .5, .06), "pitched repeat window");
    s[P::delaySync] = 1; s[P::delayBeats] = 2; s[P::delayPitch] = 0;
    RoutingControls c; c.tempoBpm = 60;
    install(r, g, c); near(r.tailSeconds(), delayTail(2, .5), "activation missed live tempo");
    c.tempoBpm = 120;
    render(r, c, 2047); near(r.tailSeconds(), delayTail(2, .5), "refresh occurred early");
    render(r, c); near(r.tailSeconds(), delayTail(1, .5), "tempo refresh missing");
    c.tempoBpm = std::numeric_limits<double>::quiet_NaN();
    render(r, c, 2048); near(r.tailSeconds(), delayTail(1, .5), "invalid tempo fallback");
    s[P::delayBeats] = 8; s[P::delayFeedback] = 95; c.tempoBpm = 20;
    install(r, g, c); near(r.tailSeconds(), delayTail(24, .95), "long synced delay underestimated");
}
void automationAndFreeze() {
    RoutingExecutor r; r.prepare(48000);
    auto g = single(NodeType::reverb);
    RoutingControls c; c.fxAutomation = true;
    auto& s = c.fxSettings[1].creative;
    s[P::reverbDecay] = 3; s[P::reverbPredelay] = 100;
    install(r, g, c); near(r.tailSeconds(), 6.35, "activation missed automated reverb controls");
    s[P::reverbPitchAmount] = 40; s[P::reverbPitch] = 12;
    render(r, c, 2048); near(r.tailSeconds(), 13.1, "pitched feedback tail allowance missing");
    s[P::reverbPitchAmount] = 0;
    render(r, c, 2048); near(r.tailSeconds(), 6.35, "zero shimmer changed historical reverb tail");
    s[P::reverbFreeze] = 1;
    render(r, c, 2048); check(std::isinf(r.tailSeconds()), "reverb freeze not infinite");
    s[P::reverbFreeze] = 0; s[P::reverbDecay] = .5f;
    render(r, c, 2048); near(r.tailSeconds(), 1.35, "reverb did not return to practical tail");
    s[P::reverbDecay] = std::numeric_limits<float>::infinity();
    render(r, c, 2048); near(r.tailSeconds(), 2 * double(2.8f) + .35, "invalid automation not sanitized");
    g = single(NodeType::granular);
    s[P::granularPosition] = 500; s[P::granularJitter] = 50;
    install(r, g, c); near(r.tailSeconds(), .5 + .05 + .75 + 4. / 48000 + .25, "granular history/traversal allowance");
    s[P::granularFreeze] = 1;
    render(r, c, 2048); check(std::isinf(r.tailSeconds()), "granular freeze not infinite");
    c.fxSettings[1].enabled = false; c.fxSettings[1].mix = 0;
    render(r, c, 2048); check(std::isinf(r.tailSeconds()), "bypass discarded frozen history allowance");
    s[P::granularFreeze] = 0;
    render(r, c, 2048); check(std::isfinite(r.tailSeconds()) && r.tailSeconds() < 3, "granular unfreeze retained infinite tail");
    // Unmapped nodes must still use authored settings under FX automation.
    g = single(NodeType::reverb, 500); g.nodes[1].creative[P::reverbDecay] = 1;
    install(r, g, c); near(r.tailSeconds(), 2.278, "unmapped node used automated slot");
    for (auto type : {NodeType::compressor, NodeType::bitcrusher}) {
        install(r, single(type)); near(r.tailSeconds(), 2, "fixed creative allowance changed");
    }
}
void utilityRouting() {
    using U = UtilityParameter;
    auto r = std::make_unique<RoutingExecutor>(); r->prepare(48000);
    const RoutingSources source{{.125f, -.25f}};
    for (const auto type : {NodeType::gain, NodeType::stereo, NodeType::gate, NodeType::xpFilter, NodeType::synthFilter}) {
        auto g = single(type);
        check(g.nodes[1].mix == 1, "utility default is not full wet");
        g.nodes[1].mix = 0;
        install(*r, g); render(*r, {}, 20000);
        audio = true;
        const auto dry = r->process(source, {});
        audio = false;
        check(dry.left == source.lead.left && dry.right == source.lead.right, "utility zero Mix is not exact dry");
        g.nodes[1].enabled = false; g.nodes[1].mix = 1;
        install(*r, g); render(*r, {}, 20000);
        const auto bypass = r->process(source, {});
        check(bypass.left == source.lead.left && bypass.right == source.lead.right, "utility bypass is not exact dry");
        near(r->tailSeconds(), type == NodeType::xpFilter || type == NodeType::synthFilter ? 256 : 0, "utility tail allowance");
    }
    auto g = single(NodeType::gain);
    g.nodes[1].utility[U::gainDb] = -6;
    install(*r, g); render(*r, {}, 20000);
    const auto gain = r->process(source, {});
    check(std::abs(gain.left - .125 * std::pow(10., -.3)) < 1.e-6, "routed Gain ignored authored settings");
    g.nodes.push_back({101, NodeType::gain}); g.nodes.back().utility[U::gainDb] = -6;
    g.links = {{1, 100, 0}, {100, 101, 0}, {101, 2, 0}};
    install(*r, g); render(*r, {}, 20000);
    const auto serial = r->process(source, {});
    check(std::abs(serial.left - .125 * std::pow(10., -.6)) < 1.e-6, "independent serial Gain machines failed");
    RoutingControls c; c.fxAutomation = true;
    c.fxSettings[1].mix = c.fxSettings[2].mix = 1;
    c.fxSettings[1].utility[U::gainDb] = -12;
    c.fxSettings[2].utility[U::gainDb] = 6;
    render(*r, c, 20000);
    check(std::abs(r->process(source, c).left - gain.left) < 1.e-6, "utility slot automation failed");
    g = single(NodeType::synthFilter);
    g.nodes[1].utility[U::synthMode] = 5;
    g.nodes[1].utility[U::synthCutoff] = 20;
    g.nodes[1].utility[U::synthFeedback] = 95;
    install(*r, g);
    audio = true;
    r->process(source, {});
    double energy = 0;
    for (int i = 0; i < 6000; ++i) {
        const auto y = r->process({}, {});
        energy += y.left * y.left + y.right * y.right;
    }
    audio = false;
    check(energy > 1.e-6 && std::isfinite(energy), "routed comb has no retained tail");
    r->reset();
    audio = true;
    for (int i = 0; i < 6000; ++i) {
        const auto y = r->process({}, {});
        check(y.left == 0 && y.right == 0, "utility reset retained comb audio");
    }
    audio = false;
    // Two same-kind machines must not double-clock a shared comb ring.
    g.nodes.push_back({101, NodeType::synthFilter});
    g.nodes.back().utility[U::synthMode] = 5;
    g.nodes.back().utility[U::synthCutoff] = 37;
    g.nodes.back().utility[U::synthFeedback] = -60;
    g.links = {{1, 100, 0}, {100, 101, 0}, {101, 2, 0}};
    install(*r, g); r->reset();
    UtilityEffect first, second;
    first.prepare(48000, UtilityKind::synthFilter); second.prepare(48000, UtilityKind::synthFilter);
    audio = true;
    for (int i = 0; i < 8000; ++i) {
        const RoutingSources in{i == 0 ? source.lead : StereoSample{}};
        const auto a = first.process(in.lead.left, in.lead.right, g.nodes[1].utility);
        const auto b = second.process(a[0], a[1], g.nodes.back().utility);
        const auto actual = r->process(in, {});
        check(std::abs(actual.left - b[0]) < 1.e-6 && std::abs(actual.right - b[1]) < 1.e-6,
            "same-kind routed filters shared DSP history");
    }
    audio = false;
    near(r->tailSeconds(), 512, "serial filter allowances not added");
    g.links = {{1, 100, 0}, {1, 101, 0}, {100, 2, 0}, {101, 2, 0}};
    install(*r, g); near(r->tailSeconds(), 256, "parallel filter allowances not maxed");
    // Retain the conservative bound after comb-to-SVF automation or bypass.
    c.fxSettings[1].utility[U::synthMode] = 0; c.fxSettings[1].enabled = false;
    render(*r, c, 2048); near(r->tailSeconds(), 256, "comb history tail lost after mode edit");
    g.links = {{1, 2, 0}};
    install(*r, g); near(r->tailSeconds(), 0, "dead-end filter contaminated Output tail");
    g.nodes[1].utility[U::synthMode] = utilityParameters[size_t(U::synthMode)].max + 1;
    check(!r->requestGraph(g), "invalid utility mode accepted");
    g.nodes[1].utility[U::synthMode] = .5f;
    check(!r->requestGraph(g), "fractional utility mode accepted");
    g.nodes[1].utility[U::synthMode] = std::numeric_limits<float>::quiet_NaN();
    check(!r->requestGraph(g), "nonfinite utility mode accepted");
}
void topologyAndLegacy() {
    RoutingExecutor r; r.prepare(8000);
    auto g = single(NodeType::reverb);
    g.nodes[1].creative[P::reverbDecay] = 1; // 2.278 s
    g.nodes.push_back({101, NodeType::granular});
    g.nodes.back().creative[P::granularFreeze] = 1;
    g.links.push_back({1, 101, 0}); // Driven dead end must not poison Output.
    install(r, g); near(r.tailSeconds(), 2.278, "disconnected freeze contaminated tail");
    g.links.push_back({101, 2, 0});
    install(r, g); check(std::isinf(r.tailSeconds()), "parallel frozen path ignored");
    g.nodes.back().creative[P::granularFreeze] = 0;
    install(r, g); near(r.tailSeconds(), 2.278, "parallel tails added rather than maxed");
    g.links = {{1, 100, 0}, {100, 101, 0}, {101, 2, 0}};
    install(r, g);
    near(r.tailSeconds(), 2.278 + .09 + .012 + .75 + 4. / 8000 + .25, "serial tails not added");
    g.links = {{101, 2, 0}};
    g.nodes.back().creative[P::granularFreeze] = 1;
    install(r, g); check(std::isinf(r.tailSeconds()), "silent-input retained freeze ignored");
    g.links.clear(); install(r, g); near(r.tailSeconds(), 0, "disconnected Output tail");
    Graph legacy{{{1, NodeType::lead}, {100, NodeType::chorale}, {101, NodeType::waveshaper},
        {102, NodeType::equalizer}, {103, NodeType::crossover}, {2, NodeType::output}},
        {{1, 100, 0}, {100, 101, 0}, {101, 102, 0}, {102, 103, 0}, {103, 2, 0}}};
    install(r, legacy); near(r.tailSeconds(), .040 + 1 + 256 + 8, "legacy exact serial tail changed");
    render(r, {}, 4096); near(r.tailSeconds(), .040 + 1 + 256 + 8, "periodic refresh changed legacy tail");
    r.reset(); near(r.tailSeconds(), .040 + 1 + 256 + 8, "reset changed published tail");
    r.prepare(96000); near(r.tailSeconds(), .040, "reprepare default tail");
    auto invalid = single(NodeType::delay); invalid.links.push_back(invalid.links[0]);
    check(!r.requestGraph(invalid), "invalid graph accepted");
    near(r.tailSeconds(), .040, "rejected graph changed active tail");
}
}
int main() {
    try {
        delayControls(); automationAndFreeze(); topologyAndLegacy(); utilityRouting();
        check(allocations == 0 && frees == 0, "audio tail updates allocated or freed memory");
        std::cout << "Creative/utility routing: tails, tempo, freeze, topology, true dry, Gain, comb/reset and zero audio allocations/frees passed\n";
    } catch (const std::exception& e) {
        audio = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
