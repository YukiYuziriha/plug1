#include "Routing.h"
#include "VocalEngine.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>

namespace {
thread_local bool audio = false;
thread_local int allocations = 0, destructions = 0;
thread_local int pauseAfterAllocations = 0;
std::atomic<bool> allocationPaused{false}, allocationContinue{false};
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void near(float actual, float expected, const char* message) { check(std::abs(actual - expected) < 2e-5f, message); }
}
void* operator new(std::size_t n) {
    if (audio) ++allocations;
    if (pauseAfterAllocations && --pauseAfterAllocations == 0) {
        allocationPaused.store(true, std::memory_order_release);
        while (!allocationContinue.load(std::memory_order_acquire)) std::this_thread::yield();
    }
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (audio && p) ++destructions; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

using namespace vocalstack;
Graph direct() { return {{{1, NodeType::lead}, {2, NodeType::output}}, {{1, 2, 0}}}; }
Graph independent() {
    Graph g{{{1, NodeType::lead}, {2, NodeType::voice, 0},
             {3, NodeType::chorale}, {4, NodeType::chorale}, {5, NodeType::sum}, {6, NodeType::output}},
            {{1, 3, 0}, {2, 4, 0}, {3, 5, 0}, {4, 5, 1}, {5, 6, 0}}};
    g.nodes[2].mix = g.nodes[3].mix = 1;
    g.nodes[2].motion = 0.1f; g.nodes[3].motion = 0.9f;
    return g;
}
void validation() {
    check(bool(RoutingExecutor::validate(defaultGraph())), "default validation");
    auto bad = direct(); bad.links.push_back(bad.links[0]);
    check(!RoutingExecutor::validate(bad), "duplicate accepted");
    bad = direct(); bad.links[0].inputPin = 1;
    check(!RoutingExecutor::validate(bad), "invalid pin accepted");
    bad = direct(); bad.nodes[0].latencySamples = 1;
    check(!RoutingExecutor::validate(bad), "latency accepted");
    bad = independent(); bad.links[0].source = 5;
    check(!RoutingExecutor::validate(bad), "cycle accepted");
    bad = direct(); bad.nodes.push_back({1, NodeType::voice});
    check(!RoutingExecutor::validate(bad), "duplicate ID accepted");
    bad = direct(); bad.nodes[0].voiceSlot = 8; bad.nodes[0].type = NodeType::voice;
    check(!RoutingExecutor::validate(bad), "invalid voice accepted");
    bad = defaultGraph(); bad.links.erase(bad.links.begin() + 8);
    check(bool(RoutingExecutor::validate(bad)), "unconnected Mix pin must receive silence");
    bad = direct();
    for (int i = 0; i < 17; ++i) {
        bad.nodes.push_back({std::uint32_t(100 + i), NodeType::chorale});
        bad.links.push_back({1, std::uint32_t(100 + i), 0});
    }
    check(!RoutingExecutor::validate(bad), "17 effects accepted");
    bad.nodes.pop_back(); bad.links.pop_back();
    check(bool(RoutingExecutor::validate(bad)), "16 effects rejected");
}
void defaultAndCompensation() {
    RoutingExecutor r; r.prepare(48000);
    RoutingSources s; s.lead = {0.4f, -0.2f};
    for (int i = 0; i < 8; ++i) { s.voices[i] = {0.01f * (i + 1), -0.02f}; s.activity[i] = 1; }
    RoutingControls c;
    c.mix = 0; auto out = r.process(s, c);
    near(out.left, s.lead.left, "dry endpoint");
    c.mix = 1; out = r.process(s, c);
    near(out.left, 0.36f * 0.5f, "legacy eight-voice compensation");
    near(out.right, -0.16f * 0.5f, "wet endpoint");
    c.mix = 0.3f; out = r.process(s, c);
    near(out.left, 0.4f * 0.7f + 0.18f * 0.3f, "linear main mix");

    Graph g{{{1, NodeType::lead}, {2, NodeType::chorale}, {3, NodeType::chorale},
             {4, NodeType::sum}, {5, NodeType::output}},
            {{1, 2, 0}, {1, 3, 0}, {2, 4, 0}, {3, 4, 1}, {4, 5, 0}}};
    g.nodes[1].enabled = g.nodes[2].enabled = false;
    check(bool(r.requestGraph(g)), "duplicate branches graph");
    for (int i = 0; i < 600; ++i) out = r.process(s, c);
    near(out.left, s.lead.left, "fan-out reconvergence must average duplicate source");
    g.nodes[2].type = NodeType::sum; // nested identity sum preserves lineage
    check(bool(r.requestGraph(g)), "grouped graph");
    for (int i = 0; i < 600; ++i) out = r.process(s, c);
    near(out.left, s.lead.left, "grouped duplicate compensation");

    Graph distinct{{{1, NodeType::voice, 0}, {2, NodeType::voice, 1}, {3, NodeType::sum}, {4, NodeType::output}},
                   {{1, 3, 0}, {2, 3, 1}, {3, 4, 0}}};
    check(bool(r.requestGraph(distinct)), "distinct graph");
    s.voices[0] = {0.2f, 0}; s.voices[1] = {0, 0.1f};
    for (int i = 0; i < 600; ++i) out = r.process(s, c);
    near(out.left, 0.2f / std::sqrt(2.0f), "independent source budget");
    near(out.right, 0.1f / std::sqrt(2.0f), "relative gain preserved");
    auto bad = direct(); bad.links[0].source = 999;
    check(!r.requestGraph(bad), "bad request accepted");
    near(r.process(s, c).right, out.right, "bad edit changed current graph");
}
void machinesAndPublication() {
    RoutingExecutor r; r.prepare(48000); auto g = independent();
    check(bool(r.requestGraph(g)), "independent graph rejected");
    Chorale a, b; a.prepare(48000); b.prepare(48000);
    RoutingSources s; s.activity[0] = 1;
    for (int i = 0; i < 12000; ++i) {
        s.lead = {std::sin(i * 0.021f) * 0.1f, i == 10 ? 0.3f : 0};
        s.voices[0] = {i == 20 ? 0.1f : 0, std::cos(i * 0.033f) * 0.05f};
        auto x = s.lead, y = s.voices[0];
        a.process(x.left, x.right, true, 1, 0.1f);
        b.process(y.left, y.right, true, 1, 0.9f);
        if (i == 4000) { // same machines, changed node ordering and an unrelated branch
            std::reverse(g.nodes.begin(), g.nodes.end());
            g.nodes.push_back({99, NodeType::voice, 7});
            check(bool(r.requestGraph(g)), "live reorder rejected");
        }
        audio = true; const auto out = r.process(s, {}); audio = false;
        if (i < 4000 || i > 4500) {
            near(out.left, (x.left + y.left) / std::sqrt(2.0f), "machine state shared/reset/double-clocked");
            near(out.right, (x.right + y.right) / std::sqrt(2.0f), "independent stereo machine state");
        }
    }
    check(allocations == 0 && destructions == 0, "audio allocation/destruction");
    r.collectRetired();
    // Queue replacement before consumption; newest request must win.
    check(bool(r.requestGraph(direct())), "queued direct graph");
    Graph latest{{{1, NodeType::voice, 0}, {2, NodeType::output}}, {{1, 2, 0}}};
    check(bool(r.requestGraph(latest)), "replace pending graph");
    for (int i = 0; i < 600; ++i) r.process(s, {0});
    near(r.process(s, {0}).left, s.voices[0].left, "latest request lost");
}
void smoothSplice() {
    RoutingExecutor r; r.prepare(48000); check(bool(r.requestGraph(direct())), "direct graph");
    RoutingSources s; s.lead = {0.4f, 0.4f}; s.voices[0] = {-0.8f, -0.8f}; s.activity[0] = 1;
    auto last = r.process(s, {});
    Graph changed{{{1, NodeType::voice, 0}, {2, NodeType::output}}, {{1, 2, 0}}};
    check(bool(r.requestGraph(changed)), "splice graph");
    near(r.process(s, {}).left, last.left, "splice discontinuity");
    for (int i = 0; i < 600; ++i) {
        const auto out = r.process(s, {});
        check(std::abs(out.left - last.left) < 0.01f, "unsmoothed routing splice");
        last = out;
    }
    near(last.left, -0.8f, "splice failed to settle");
}
void pendingMachineResetRace() {
    const auto graph = [](unsigned id) {
        Graph g{{{1, NodeType::lead}, {id, NodeType::chorale}, {2, NodeType::output}},
                {{1, id, 0}, {id, 2, 0}}};
        g.nodes[1].mix = 1; return g;
    };
    RoutingExecutor r; r.prepare(48000);
    check(bool(r.requestGraph(graph(100))), "reset setup A");
    RoutingSources s; s.lead = {0.2f, 0.1f};
    for (int i = 0; i < 5000; ++i) r.process(s, {});
    check(bool(r.requestGraph(graph(101))), "reset setup B");
    auto pending = graph(100);
    pending.nodes.insert(pending.nodes.end() - 1, {102, NodeType::chorale});
    pending.links.back() = {100, 102, 0}; pending.links.push_back({102, 2, 0});
    RoutingValidation accepted;
    // Allocation 1 builds Plan. Allocation 2 inserts the NEW machine's map
    // entry, after the retained machine 100 has been acquired by that plan.
    std::thread control([&] { pauseAfterAllocations = 2; accepted = r.requestGraph(pending); });
    while (!allocationPaused.load(std::memory_order_acquire)) std::this_thread::yield();
    audio = true; r.process(s, {}); audio = false; // consume B while C retains warmed A
    allocationContinue.store(true, std::memory_order_release); control.join();
    check(bool(accepted), "reset setup C");
    r.reset(); // current B AND queued A must lose history, without losing C
    s = {};
    for (int i = 0; i < 2500; ++i) {
        audio = true; const auto out = r.process(s, {}); audio = false;
        check(out.left == 0 && out.right == 0, "pending graph resurrected pre-reset FX history");
    }
    check(allocations == 0 && destructions == 0, "reset publication audio ownership");
}
void invalidControlValues() {
    RoutingExecutor r; r.prepare(48000);
    RoutingSources s; s.lead = {0.1f, -0.2f};
    s.activity.fill(std::numeric_limits<float>::infinity());
    RoutingControls c; c.mix = std::numeric_limits<float>::quiet_NaN();
    const auto out = r.process(s, c);
    near(out.left, 0.065f, "nonfinite main Mix was not sanitized");
    near(out.right, -0.13f, "nonfinite main Mix right");
    for (double rate : {0.0, -48000.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        bool threw = false;
        try { r.prepare(rate); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "invalid routing rate accepted");
    }
    near(r.process(s, c).left, out.left, "invalid prepare destroyed valid plan");
}
void concurrentEdits() {
    RoutingExecutor r; r.prepare(48000);
    std::atomic<bool> done{false}; std::atomic<int> errors{0};
    std::thread render([&] {
        RoutingSources s; s.lead = {0.1f, 0.2f}; s.voices[0] = {0.03f, 0}; s.activity[0] = 1;
        audio = true;
        while (!done.load(std::memory_order_acquire)) {
            const auto out = r.process(s, {});
            if (!std::isfinite(out.left) || !std::isfinite(out.right)) ++errors;
            if (!std::isfinite(r.tailSeconds()) || r.tailSeconds() < 0) ++errors;
        }
        audio = false;
        if (allocations || destructions) ++errors;
    });
    for (int i = 0; i < 1500; ++i) {
        auto g = i % 2 ? defaultGraph() : independent();
        check(bool(r.requestGraph(g)), "concurrent request");
        check(std::isfinite(r.tailSeconds()) && r.tailSeconds() >= 0, "concurrent tail publication");
        if (i % 7 == 0) std::this_thread::yield();
    }
    done.store(true, std::memory_order_release); render.join(); r.collectRetired();
    check(errors == 0, "concurrent render failed");
}
void automaticFanIn() {
    RoutingExecutor r; r.prepare(48000);
    Graph graph{{{1, NodeType::voice, 0}, {2, NodeType::voice, 1}, {100, NodeType::chorale}, {3, NodeType::output}},
        {{1, 100, 0}, {2, 100, 0}, {100, 3, 0}}};
    graph.nodes[2].enabled = false;
    check(bool(r.requestGraph(graph)), "direct voice fan-in rejected");
    RoutingSources sources; sources.activity[0] = sources.activity[1] = 1;
    sources.voices[0] = {0.2f, 0}; sources.voices[1] = {0, 0.1f};
    RoutingControls controls;
    auto out = r.process(sources, controls);
    near(out.left, 0.2f / std::sqrt(2.0f), "automatic merge compensation left");
    near(out.right, 0.1f / std::sqrt(2.0f), "automatic merge erased relative gain");
    graph.links = {{1, 100, 0}, {100, 3, 0}, {1, 3, 0}};
    check(bool(r.requestGraph(graph)), "duplicated-branch fan-in rejected");
    for (int i = 0; i < 1000; ++i) out = r.process(sources, controls);
    near(out.left, sources.voices[0].left, "duplicate branches increased amplitude");
    auto legacy = defaultGraph();
    legacy.nodes.push_back({10, NodeType::sum});
    legacy.links = {{2, 10, 0}, {3, 10, 1}, {10, 11, 1}, {1, 11, 0}, {11, 12, 0}, {12, 13, 0}};
    const auto migrated = removeLegacySumNodes(legacy);
    check(bool(RoutingExecutor::validate(migrated)), "legacy Sum migration invalid");
    for (const auto& node : migrated.nodes) check(node.type != NodeType::sum, "legacy Sum remains visible");
}
void disconnectionsAreLive() {
    RoutingExecutor r; r.prepare(48000);
    auto graph = direct();
    graph.nodes.push_back({100, NodeType::chorale});
    check(bool(r.requestGraph(graph)), "unused disconnected Chorale rejected");
    RoutingSources sources; sources.lead = {0.2f, -0.1f};
    RoutingControls controls;
    auto out = r.process(sources, controls);
    near(out.left, 0.2f, "disconnected FX changed direct audio");
    graph.links = {{1, 100, 0}, {100, 2, 0}};
    graph.nodes.back().mix = 1;
    check(bool(r.requestGraph(graph)), "connected Chorale rejected");
    for (int i = 0; i < 4800; ++i) out = r.process(sources, controls);
    graph.links.clear();
    check(bool(r.requestGraph(graph)), "disconnect-all retained old graph");
    for (int i = 0; i < 1000; ++i) out = r.process(sources, controls);
    near(out.left, 0, "disconnected Output still plays old Chorale left");
    near(out.right, 0, "disconnected Output still plays old Chorale right");
    graph.links = {{100, 2, 0}};
    check(bool(r.requestGraph(graph)), "unconnected FX input rejected");
    for (int i = 0; i < 4800; ++i) out = r.process(sources, controls);
    near(out.left, 0, "unconnected FX input still receives old audio");
    graph.links = {{1, 2, 0}};
    check(bool(r.requestGraph(graph)), "routing around disconnected FX rejected");
    for (int i = 0; i < 1000; ++i) out = r.process(sources, controls);
    near(out.left, sources.lead.left, "disconnected FX still applied after direct reconnection");
    auto empty = defaultGraph(); empty.links.clear();
    check(bool(r.requestGraph(empty)), "all-disconnected sources/mix/sum rejected");
    for (int i = 0; i < 1000; ++i) out = r.process(sources, controls);
    near(out.left, 0, "empty graph not silent");
}
void toneEffectsAndTypeIdentity() {
    static_assert(int(NodeType::lead) == 0 && int(NodeType::output) == 5
        && int(NodeType::waveshaper) == 6 && int(NodeType::equalizer) == 7 && int(NodeType::crossover) == 8);
    auto defaults = defaultGraph();
    for (const auto& n : defaults.nodes) check(n.type != NodeType::sum,"default exposes Sum");
    RoutingExecutor executor; executor.prepare(48000);
    Graph g{{{1,NodeType::voice,0},{2,NodeType::voice,1},
        {100,NodeType::waveshaper},{101,NodeType::equalizer},{3,NodeType::output}},
        {{1,100,0},{2,100,0},{100,101,0},{101,3,0}}};
    g.nodes[2].waveshaper = {true,12,-3,.8f,2};
    g.nodes[3].eq.bands[0] = {true,1200,6,1.1f,0};
    check(bool(executor.requestGraph(g)),"direct voice tone FX graph");
    Waveshaper shaper; shaper.prepare(48000);
    Equalizer eq; eq.prepare(48000);
    RoutingControls c; c.choraleOn = false;
    for (int i = 0; i < 12000; ++i) {
        RoutingSources s; s.activity[0] = s.activity[1] = 1;
        s.voices[0] = {float(.2*std::sin(i*.03)),0};
        s.voices[1] = {0,float(.1*std::cos(i*.07))};
        auto expected = StereoSample{s.voices[0].left/std::sqrt(2.f),s.voices[1].right/std::sqrt(2.f)};
        shaper.process(expected.left,expected.right,g.nodes[2].waveshaper);
        eq.process(expected.left,expected.right,g.nodes[3].eq);
        if (i == 4000) {
            auto reordered = g; std::reverse(reordered.nodes.begin(),reordered.nodes.end());
            reordered.nodes.push_back({99,NodeType::voice,7});
            check(bool(executor.requestGraph(reordered)),"new FX reorder");
        }
        audio = true; const auto actual = executor.process(s,c); audio = false;
        if (i < 4000 || i > 4500) {
            near(actual.left,expected.left,"new FX history/voice fan-in left");
            near(actual.right,expected.right,"new FX history/voice fan-in right");
        }
    }
    // Same ID, different type must not use an old module (or resurrect its state).
    Graph changed{{{1,NodeType::lead},{100,NodeType::equalizer},{3,NodeType::output}},
        {{1,100,0},{100,3,0}}};
    check(bool(executor.requestGraph(changed)),"type change rejected");
    RoutingSources zero;
    for (int i = 0; i < 1000; ++i) executor.process(zero,c);
    changed.nodes[1].type = NodeType::waveshaper;
    changed.nodes[1].waveshaper = g.nodes[2].waveshaper;
    check(bool(executor.requestGraph(changed)),"type changed back rejected");
    Waveshaper fresh; fresh.prepare(48000);
    for (int i = 0; i < 1000; ++i) {
        auto expected = StereoSample{};
        fresh.process(expected.left,expected.right,changed.nodes[1].waveshaper);
        const auto actual = executor.process(zero,c);
        near(actual.left,expected.left,"type change resurrected old shaper history");
    }
    // Crossovers count against the same sixteen-machine ceiling.
    Graph bounded{{{1,NodeType::lead},{2,NodeType::output}},{{1,2,0}}};
    for (int i = 0; i < 17; ++i)
        bounded.nodes.push_back({std::uint32_t(100+i), i%3 == 0 ? NodeType::crossover : i%3 == 1 ? NodeType::waveshaper : NodeType::equalizer});
    check(!RoutingExecutor::validate(bounded),"17 mixed effects accepted");
    bounded.nodes.pop_back(); check(bool(RoutingExecutor::validate(bounded)),"16 mixed effects rejected");
    check(allocations == 0 && destructions == 0,"new effects audio ownership");
}
void activePlanTails() {
    const auto tailNear = [](double actual, double expected, const char* message) {
        check(std::abs(actual - expected) < 1e-9, message);
    };
    RoutingExecutor r;
    tailNear(r.tailSeconds(), 0, "unprepared tail");
    r.prepare(48000);
    tailNear(r.tailSeconds(), .040, "default routing advertises excessive tail");
    const auto install = [&](const Graph& graph, double expected) {
        const double before = r.tailSeconds();
        check(bool(r.requestGraph(graph)), "tail graph rejected");
        tailNear(r.tailSeconds(), before, "pending graph changed active tail early");
        audio = true;
        r.process({}, {});
        const double actual = r.tailSeconds();
        audio = false;
        tailNear(actual, expected, "active graph tail incorrect");
    };
    install(direct(), 0);
    auto disconnected = direct();
    disconnected.nodes.push_back({100,NodeType::equalizer});
    disconnected.nodes.push_back({101,NodeType::crossover});
    disconnected.links.push_back({1,100,0}); // driven but no path to Output
    disconnected.links.push_back({100,101,0});
    install(disconnected, 0);
    Graph serial{{{1,NodeType::lead},{100,NodeType::chorale},{101,NodeType::waveshaper},
        {102,NodeType::equalizer},{103,NodeType::crossover},{2,NodeType::output}},
        {{1,100,0},{100,101,0},{101,102,0},{102,103,0},{103,2,0}}};
    install(serial, .040+.020+256+8);
    auto parallel = serial;
    parallel.links = {{1,100,0},{1,101,0},{1,102,0},{1,103,0},
        {100,2,0},{101,2,0},{102,2,0},{103,2,0}};
    install(parallel,256); // max, not sum of parallel effect bounds
    parallel.nodes.push_back({104,NodeType::equalizer});
    parallel.links.push_back({1,104,0}); parallel.links.push_back({104,2,0});
    install(parallel,256); // two parallel EQs are still one EQ tail
    parallel.links.push_back({102,104,0});
    install(parallel,512); // longest of multiple incoming paths includes both EQs
    // A silent-input machine connected to Output may retain an audible tail.
    Graph residual{{{100,NodeType::equalizer},{2,NodeType::output}},{{100,2,0}}};
    install(residual,256);
    residual.links.clear(); install(residual,0);
    r.reset(); tailNear(r.tailSeconds(),0,"reset changed graph tail");
    r.prepare(96000); tailNear(r.tailSeconds(),.040,"reprepare tail not default");
    auto bad = direct(); bad.links.push_back(bad.links[0]);
    check(!r.requestGraph(bad),"invalid tail graph accepted");
    tailNear(r.tailSeconds(),.040,"rejected graph changed tail");

    VocalEngine engine; Settings settings;
    engine.prepare(48000,1,2,settings);
    const double defaultTail = engine.tailSeconds();
    check(defaultTail >= .040 && defaultTail < 1, "default engine still advertises worst-case tail");
    check(bool(engine.requestGraph(direct())),"engine tail direct request");
    tailNear(engine.tailSeconds(),defaultTail,"engine tail changed before graph activation");
    std::array<float,64> input{},left{},right{};
    const float* in[]{input.data()}; float* out[]{left.data(),right.data()};
    audio = true; engine.process(in,out,64,settings); audio = false;
    tailNear(engine.tailSeconds(),defaultTail-.040,"engine did not use active routing tail");
    check(bool(engine.requestGraph(serial)),"engine tail serial request");
    audio = true; engine.process(in,out,64,settings); audio = false;
    tailNear(engine.tailSeconds(),defaultTail-.040+.040+.020+256+8,"engine serial tail forwarding");
    check(allocations == 0 && destructions == 0,"tail publication allocated/destroyed on audio");
}
int main() {
    try {
        activePlanTails(); toneEffectsAndTypeIdentity(); automaticFanIn(); disconnectionsAreLive(); validation(); defaultAndCompensation(); machinesAndPublication(); smoothSplice(); pendingMachineResetRace(); invalidControlValues(); concurrentEdits();
        std::cout << "Routing backend: active longest-path tails, engine tail forwarding, validation, endpoints, lineage compensation, independent FX, history, RT ownership and concurrent edits passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
