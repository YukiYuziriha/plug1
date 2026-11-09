#include "VocalEngine.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
thread_local bool audio = false;
thread_local int allocations = 0, destructions = 0;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(float a, float b, const char* message) { check(std::abs(a - b) < 2e-5f, message); }
}
void* operator new(std::size_t n) {
    if (audio) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (audio && p) ++destructions; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
using namespace vocalstack;

void allSlotsLiveAndIndependent() {
    Graph g; g.nodes.push_back({1, NodeType::lead});
    std::array<Chorale, 16> reference;
    for (int slot = 0; slot < 16; ++slot) {
        const auto id = std::uint32_t(slot ? slot + 99 : 12);
        RoutingNode node{id, NodeType::chorale};
        node.enabled = slot % 2 != 0; node.mix = 0.13f; node.motion = 0.42f;
        node.mainControls = slot == 0 || slot == 1;
        g.nodes.push_back(node);
        g.links.push_back({1, id, 0}); g.links.push_back({id, 200, slot});
        reference[slot].prepare(48000);
    }
    g.nodes.push_back({200, NodeType::sum}); g.nodes.push_back({201, NodeType::output});
    g.links.push_back({200, 201, 0});
    RoutingExecutor executor; executor.prepare(48000);
    check(bool(executor.requestGraph(g)), "automation graph rejected");
    RoutingControls c;
    RoutingSources s;
    // Exactly ONE graph request. Every subsequent change is live value data.
    for (int i = 0; i < 32000; ++i) {
        const int event = i / 137;
        c.fxAutomation = !(i >= 12000 && i < 20000);
        c.choraleOn = event % 3 != 0;
        c.choraleMix = float(event % 11) / 10;
        c.choraleMotion = float(event % 7) / 6;
        for (int slot = 0; slot < 16; ++slot)
            c.fxSettings[slot] = {(event + slot) % 4 != 0,
                float((event + slot * 3) % 13) / 12, float((event * 2 + slot) % 17) / 16};
        // Prove slot zero follows legacy fields, not the array alias.
        c.fxSettings[0] = {!c.choraleOn, 1 - c.choraleMix, 1 - c.choraleMotion};
        s.lead = {0.1f * std::sin(i * 0.021f), 0.07f * std::cos(i * 0.033f)};
        StereoSample expected;
        for (int slot = 0; slot < 16; ++slot) {
            const auto& node = g.nodes[slot + 1];
            FxSettings fx{node.enabled && (!node.mainControls || c.choraleOn),
                node.mainControls ? c.choraleMix : node.mix,
                node.mainControls ? c.choraleMotion : node.motion};
            if (c.fxAutomation) fx = slot ? c.fxSettings[slot] : FxSettings{c.choraleOn, c.choraleMix, c.choraleMotion};
            auto sample = s.lead;
            reference[slot].process(sample.left, sample.right, fx.enabled, fx.mix, fx.motion);
            expected.left += sample.left; expected.right += sample.right;
        }
        audio = true; const auto actual = executor.process(s, c); audio = false;
        // All paths derive from lead, so lineage compensation averages them.
        near(actual.left, expected.left / 16, "FX automation/state left mismatch");
        near(actual.right, expected.right / 16, "FX automation/state right mismatch");
    }
    check(allocations == 0 && destructions == 0, "FX automation changed audio ownership");
}

void unmappedStandalone() {
    Graph g{{{1, NodeType::lead}, {999, NodeType::chorale}, {2, NodeType::output}},
            {{1, 999, 0}, {999, 2, 0}}};
    g.nodes[1].mix = 0.8f; g.nodes[1].motion = 0.7f;
    RoutingExecutor executor; executor.prepare(48000); check(bool(executor.requestGraph(g)), "unmapped graph");
    Chorale reference; reference.prepare(48000);
    RoutingControls c; c.fxAutomation = true;
    for (auto& fx : c.fxSettings) fx = {false, 0, 0};
    for (int i = 0; i < 8000; ++i) {
        RoutingSources s; s.lead = {std::sin(i * 0.04f) * 0.1f, 0};
        auto expected = s.lead; reference.process(expected.left, expected.right, true, 0.8f, 0.7f);
        const auto actual = executor.process(s, c);
        near(actual.left, expected.left, "unmapped node controls overridden");
    }
}

void engineForwarding() {
    Settings settings; settings.levelOn = false; settings.doubles = 0; settings.fxAutomation = true;
    check(settings.fxSettings.size() == 16 && !Settings{}.fxAutomation, "Settings adapter contract");
    for (int slot : {0, 1, 15}) {
        const auto id = std::uint32_t(slot ? slot + 99 : 12);
        Graph fxGraph{{{1, NodeType::lead}, {id, NodeType::chorale}, {2, NodeType::output}},
                      {{1, id, 0}, {id, 2, 0}}};
        fxGraph.nodes[1].enabled = false; fxGraph.nodes[1].mix = 0; // automation must override both
        Graph dryGraph{{{1, NodeType::lead}, {2, NodeType::output}}, {{1, 2, 0}}};
        VocalEngine engine, dryEngine;
        engine.prepare(48000, 1, 2, settings); dryEngine.prepare(48000, 1, 2, settings);
        check(bool(engine.requestGraph(fxGraph)) && bool(dryEngine.requestGraph(dryGraph)), "engine graph request");
        Chorale reference; reference.prepare(48000);
        std::array<float, 137> input{}, left{}, right{}, dryLeft{}, dryRight{};
        for (int block = 0; block < 150; ++block) {
            settings.fxSettings[slot] = {block % 4 != 0, float(block % 11) / 10, float(block % 7) / 6};
            settings.choraleOn = block % 3 != 0;
            settings.choraleMix = float(block % 9) / 8; settings.choraleMotion = 0.77f;
            if (block == 80) { // invalid live controls must not poison FX history
                settings.fxSettings[slot].mix = std::numeric_limits<float>::quiet_NaN();
                settings.fxSettings[slot].motion = std::numeric_limits<float>::infinity();
            }
            for (int i = 0; i < 137; ++i) input[i] = std::sin((block * 137 + i) * 0.033f) * 0.1f;
            const float* in[] = {input.data()};
            float* out[] = {left.data(), right.data()}, *dryOut[] = {dryLeft.data(), dryRight.data()};
            audio = true;
            engine.process(in, out, 137, settings); dryEngine.process(in, dryOut, 137, settings);
            audio = false;
            const auto fx = slot ? settings.fxSettings[slot]
                : FxSettings{settings.choraleOn, settings.choraleMix, settings.choraleMotion};
            for (int i = 0; i < 137; ++i) {
                reference.process(dryLeft[i], dryRight[i], fx.enabled, fx.mix, fx.motion);
                near(left[i], dryLeft[i], "engine did not forward live FX left");
                near(right[i], dryRight[i], "engine did not forward live FX right");
            }
        }
    }
    check(allocations == 0 && destructions == 0, "engine automation allocated/destroyed on audio");
}

int main() {
    try {
        allSlotsLiveAndIndependent(); unmappedStandalone(); engineForwarding();
        std::cout << "FX automation: all 16 slots, legacy precedence, node fallback, independent warm history, engine forwarding and RT ownership passed\n";
    } catch (const std::exception& e) { audio = false; std::cerr << e.what() << '\n'; return 1; }
}
