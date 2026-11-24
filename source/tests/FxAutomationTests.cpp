#include "VocalEngine.h"
#include <algorithm>
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

namespace {
constexpr double sampleRate = 48000;

// Use the closed-form fade on each constant-target segment to schedule
// standalone DSP ticks. The actual audio and retained history come from the
// standalone kernels, not a second routing executor.
class FadeClock {
public:
    bool tick(bool enabled, double target, double seconds, double floor, bool adoptFirst = false) {
        if (!started && adoptFirst) {
            started = true;
            blend = segmentStart = segmentTarget = target;
            return true;
        }
        if (target != segmentTarget) {
            segmentStart = blend;
            segmentTarget = target;
            samples = 0;
        }
        const bool run = enabled || blend != 0;
        if (blend != target) {
            blend = target + (segmentStart - target) * std::exp(-++samples / (sampleRate * seconds));
            if (std::abs(blend - target) < floor) blend = target;
        }
        return run;
    }
private:
    int samples = 0;
    double blend = 0, segmentStart = 0, segmentTarget = 0;
    bool started = false;
};

struct ReferenceFx {
    explicit ReferenceFx(NodeType type = NodeType::chorale) : type(type) {
        switch (type) {
        case NodeType::chorale: chorale.prepare(sampleRate); break;
        case NodeType::waveshaper: shaper.prepare(sampleRate); break;
        case NodeType::equalizer: eq.prepare(sampleRate); break;
        case NodeType::crossover: crossover.prepare(sampleRate); break;
        default: break;
        }
    }

    StereoSample render(StereoSample sample, const FxSettings& fx, int port = 0) {
        switch (type) {
        case NodeType::chorale: {
            const auto target = fx.enabled && std::isfinite(fx.mix) ? std::clamp(fx.mix, 0.f, 1.f) : 0.f;
            if (fade.tick(fx.enabled, target, .025, 1e-6))
                chorale.process(sample.left, sample.right, fx.enabled, fx.mix, fx.motion);
            break;
        }
        case NodeType::waveshaper: {
            auto settings = fx.waveshaper;
            settings.enabled = settings.enabled && fx.enabled;
            const auto mix = std::isfinite(settings.mix) ? std::clamp(settings.mix, 0.f, 1.f) : 1.f;
            if (fade.tick(settings.enabled, settings.enabled ? mix : 0.f, .010, 1e-9, true))
                shaper.process(sample.left, sample.right, settings);
            break;
        }
        case NodeType::equalizer: {
            auto settings = fx.eq;
            settings.enabled = settings.enabled && fx.enabled;
            if (fade.tick(settings.enabled, settings.enabled ? 1. : 0., .025, 1e-8, true))
                eq.process(sample.left, sample.right, settings);
            break;
        }
        case NodeType::crossover: {
            if (!fx.enabled) return port == 0 ? sample : StereoSample{};
            const auto bands = crossover.process(sample.left, sample.right, fx.bandCount, fx.crossoverLow, fx.crossoverHigh);
            return {bands[port][0], bands[port][1]};
        }
        default: break;
        }
        return sample;
    }

    NodeType type;
    FadeClock fade;
    Chorale chorale;
    Waveshaper shaper;
    Equalizer eq;
    Multiband crossover;
};
}

void allSlotsLiveAndIndependent() {
    Graph g; g.nodes.push_back({1, NodeType::lead});
    std::array<ReferenceFx, 16> reference;
    for (int slot = 0; slot < 16; ++slot) {
        const auto id = std::uint32_t(slot ? slot + 99 : 12);
        RoutingNode node{id, NodeType::chorale};
        node.enabled = slot % 2 != 0; node.mix = 0.13f; node.motion = 0.42f;
        node.mainControls = slot == 0 || slot == 1;
        g.nodes.push_back(node);
        g.links.push_back({1, id, 0}); g.links.push_back({id, 200, slot});

    }
    g.nodes.push_back({200, NodeType::sum}); g.nodes.push_back({201, NodeType::output});
    g.links.push_back({200, 201, 0});
    RoutingExecutor executor; executor.prepare(48000);
    check(bool(executor.requestGraph(g)), "automation graph rejected");
    RoutingControls c;
    RoutingSources s;
    // Exactly ONE graph request. Every subsequent change is live value data.
    // A long explicit bypass lets every slot finish fading before it wakes.
    for (int i = 0; i < 54000; ++i) {
        const bool sleepingSpan = i >= 32000 && i < 50000;
        const int event = i / 137;
        c.fxAutomation = !(i >= 12000 && i < 20000);
        c.choraleOn = !sleepingSpan && event % 3 != 0;
        c.choraleMix = float(event % 11) / 10;
        c.choraleMotion = float(event % 7) / 6;
        for (int slot = 0; slot < 16; ++slot)
            c.fxSettings[slot] = {!sleepingSpan && (event + slot) % 4 != 0,
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
            const auto sample = reference[slot].render(s.lead, fx);
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
        audio = true; const auto actual = executor.process(s, c); audio = false;
        near(actual.left, expected.left, "unmapped node controls overridden");
        near(actual.right, expected.right, "unmapped node controls overridden");
    }
    check(allocations == 0 && destructions == 0, "unmapped node changed audio ownership");
}

void engineForwarding() {
    Settings settings; settings.levelOn = false; settings.doubles = 0; settings.fxAutomation = true;
    check(settings.fxSettings.size() == 16 && !Settings{}.fxAutomation, "Settings adapter contract");
    for (auto type : {NodeType::chorale, NodeType::waveshaper, NodeType::equalizer, NodeType::crossover})
    for (int slot : {0, 1, 15}) {
        if (slot == 0 && type != NodeType::chorale) continue;
        const auto id = std::uint32_t(slot ? slot + 99 : 12);
        Graph fxGraph{{{1, NodeType::lead}, {id, type}, {2, NodeType::output}},
                      {{1, id, 0}, {id, 2, 0}}};
        fxGraph.nodes[1].enabled = false; fxGraph.nodes[1].mix = 0; // automation must override both
        Graph dryGraph{{{1, NodeType::lead}, {2, NodeType::output}}, {{1, 2, 0}}};
        VocalEngine engine, dryEngine;
        engine.prepare(48000, 1, 2, settings); dryEngine.prepare(48000, 1, 2, settings);
        check(bool(engine.requestGraph(fxGraph)) && bool(dryEngine.requestGraph(dryGraph)), "engine graph request");
        ReferenceFx reference(type);
        std::array<float, 137> input{}, left{}, right{}, dryLeft{}, dryRight{};
        for (int block = 0; block < 150; ++block) {
            settings.fxSettings[slot] = {block % 4 != 0, float(block % 11) / 10, float(block % 7) / 6};
            settings.fxSettings[slot].waveshaper = {true, float(block%13), -3, .8f, block%3};
            auto& e = settings.fxSettings[slot].eq;
            e.bands[0] = {true, float(200+block*10), float(block%12-6), .8f, 0};
            settings.fxSettings[slot].bandCount = block < 70 ? 3 : 2;
            settings.fxSettings[slot].crossoverLow = float(100+block*4);
            settings.fxSettings[slot].crossoverHigh = 3000;
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
                const auto expected = reference.render({dryLeft[i], dryRight[i]}, fx);
                near(left[i], expected.left, "engine did not forward live FX left");
                near(right[i], expected.right, "engine did not forward live FX right");
            }
        }
    }
    check(allocations == 0 && destructions == 0, "engine automation allocated/destroyed on audio");
}

void newEffectSlots() {
    for (auto type : {NodeType::waveshaper,NodeType::equalizer,NodeType::crossover}) {
        for (int slot = 1; slot < 16; ++slot) {
            const auto id = std::uint32_t(slot+99);
            const int port = type == NodeType::crossover ? slot % 3 : 0;
            Graph g{{{1,NodeType::lead},{id,type},{2,NodeType::output}},{{1,id,0},{id,2,0,port}}};
            auto& n = g.nodes[1];
            n.waveshaper = {true, 9,-2,.7f,2};
            n.eq.bands[0] = {true,700,5,.9f,0};
            n.crossoverLow = 450; n.crossoverHigh = 2500; n.bandCount = 3;
            RoutingExecutor executor; executor.prepare(48000);
            check(bool(executor.requestGraph(g)),"new effect slot graph");
            ReferenceFx reference(type);
            RoutingControls c; c.choraleOn = false; // must never gate added FX
            for (int i = 0; i < 6000; ++i) {
                c.fxAutomation = i >= 1000 && i < 4000;
                auto& live = c.fxSettings[slot];
                live.enabled = (i/173)%3 != 0;
                live.waveshaper = {true,float((i/137)%20),-4,.9f,(i/211)%3};
                live.eq.bands[0] = {true,float(200+(i/137)*33),float((i/173)%12-6),1.2f,0};
                live.bandCount = i < 3000 ? 2 : 3;
                live.crossoverLow = float(200+(i/137)*11); live.crossoverHigh = 3300;
                FxSettings fx{n.enabled,n.mix,n.motion};
                fx.waveshaper = n.waveshaper; fx.eq = n.eq;
                fx.bandCount = n.bandCount; fx.crossoverLow = n.crossoverLow; fx.crossoverHigh = n.crossoverHigh;
                if (c.fxAutomation) fx = live;
                RoutingSources s; s.lead = {float(.2*std::sin(i*.07)),float(.13*std::cos(i*.031))};
                const auto expected = reference.render(s.lead, fx, port);
                audio = true; const auto actual = executor.process(s,c); audio = false;
                if (type == NodeType::crossover && (!fx.enabled || (port == 2 && fx.bandCount == 2)))
                    check(actual.left == expected.left && actual.right == expected.right, "crossover bypass/unused port was not exact");
                near(actual.left,expected.left,"new effect slot/fallback mismatch left");
                near(actual.right,expected.right,"new effect slot/fallback mismatch right");
            }
        }
    }
    check(allocations == 0 && destructions == 0,"new effects allocated on audio");
}

int main() {
    try {
        allSlotsLiveAndIndependent(); unmappedStandalone(); newEffectSlots(); engineForwarding();
        std::cout << "FX automation: all 16 slots, tone/crossover slots 1..15, legacy precedence, node fallback, bypass sleep/resume, engine forwarding and RT ownership passed\n";
    } catch (const std::exception& e) { audio = false; std::cerr << e.what() << '\n'; return 1; }
}
