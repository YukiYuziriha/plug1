#include "Routing.h"
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
    check(!RoutingExecutor::validate(bad), "missing Mix pin accepted");
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
        }
        audio = false;
        if (allocations || destructions) ++errors;
    });
    for (int i = 0; i < 1500; ++i) {
        auto g = i % 2 ? defaultGraph() : independent();
        check(bool(r.requestGraph(g)), "concurrent request");
        if (i % 7 == 0) std::this_thread::yield();
    }
    done.store(true, std::memory_order_release); render.join(); r.collectRetired();
    check(errors == 0, "concurrent render failed");
}
int main() {
    try {
        validation(); defaultAndCompensation(); machinesAndPublication(); smoothSplice(); pendingMachineResetRace(); invalidControlValues(); concurrentEdits();
        std::cout << "Routing backend: validation, endpoints, lineage compensation, independent FX, history, RT ownership and concurrent edits passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
