#include "Routing.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <chrono>
namespace { thread_local bool audio = false; unsigned allocations = 0, destructions = 0;
void check(bool x, const char* reason) { if (!x) throw std::runtime_error(reason); }
}
void* operator new(std::size_t n) { if (audio) ++allocations; if (auto* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { if (audio && p) ++destructions; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
using namespace vocalstack;
Graph morphGraph() {
    Graph g{{{1, NodeType::lead}, {14, NodeType::sidechain}, {100, NodeType::spectralMorph}, {13, NodeType::output}},
        {{1,100,0}, {14,100,1}, {100,13,0}}};
    g.nodes[2].mix = 1; return g;
}
void contracts() {
    auto g = morphGraph(); check(bool(RoutingExecutor::validate(g)), "valid MAIN/DONOR graph rejected");
    check(routingInputCount(NodeType::spectralMorph) == 2 && routingInput(NodeType::spectralMorph, 1).auxiliary,
        "generic auxiliary declaration missing");
    auto bad = g; bad.links.push_back(bad.links[1]); check(!RoutingExecutor::validate(bad), "duplicate donor accepted");
    bad = g; bad.links[1].inputPin = 2; check(!RoutingExecutor::validate(bad), "invalid donor pin accepted");
    bad = g; bad.nodes.push_back({101, NodeType::gain}); bad.links[1].source = 101; bad.links.push_back({100,101,0});
    check(!RoutingExecutor::validate(bad), "donor dependency cycle accepted");
    bad = g; bad.nodes.push_back({101, NodeType::spectralMorph}); bad.links.back() = {100,101,0}; bad.links.push_back({101,13,0});
    check(!RoutingExecutor::validate(bad), "unsupported serial latency accepted");
    RoutingExecutor noReserve; noReserve.prepare(48000); check(!noReserve.requestGraph(g), "unprepared latency accepted");
}
void compensation() {
    for (double rate : {44100.,48000.,96000.}) for (int mode : {0,1}) {
        RoutingExecutor executor; executor.prepare(rate, SpectralMorph::latencySamples);
        auto g = morphGraph(); g.nodes[2].spectralMorph.mode = mode;
        // Merge the dry main and the identity Morph result. Donor is deliberately
        // the same provenance as main: it must not enter result lineage twice.
        g.links.push_back({1,13,0});
        check(bool(executor.requestGraph(g)), "parallel compensated graph rejected");
        RoutingSources s; s.sidechainAvailable = true;
        float error = 0;
        audio = true;
        for (int i = 0; i < 14000; ++i) {
            s.lead = {i == 0 ? .3f : 0.f, i == 11 ? -.2f : 0.f}; s.sidechain = {.17f,-.13f};
            const auto y = executor.process(s, {});
            const float left = i == SpectralMorph::latencySamples ? .3f : 0;
            const float right = i == SpectralMorph::latencySamples + 11 ? -.2f : 0;
            error = std::max({error,std::abs(y.left-left),std::abs(y.right-right)});
        }
        audio = false;
        check(error < 3.e-6f, "parallel main/donor/output delay compensation or provenance mismatch");
        const auto invalid = executor.requestGraph(Graph{}); check(!invalid, "invalid edit accepted");
        executor.reset();
        for (int i = 0; i < 5000; ++i) { s = {}; const auto y = executor.process(s, {}); check(y.left == 0 && y.right == 0, "reset leaked delay history"); }
    }
}
void endpointsAndInstances() {
    for (int mode : {0,1}) {
        RoutingExecutor executor; executor.prepare(48000, SpectralMorph::latencySamples);
        auto g = morphGraph(); g.nodes[2].spectralMorph.morph = 1; g.nodes[2].spectralMorph.smoothingMs = 0;
        g.nodes[2].spectralMorph.mode = mode;
        // An ordinary internal Gain is the donor; no external-host special case.
        g.nodes[1].type = NodeType::voice; g.nodes[1].voiceSlot = 0;
        g.nodes.push_back({101,NodeType::gain});
        g.links[1].source = 101; g.links.push_back({14,101,0});
        check(bool(executor.requestGraph(g)), "internal donor rejected");
        RoutingSources s; s.lead = {.1f,-.1f}; s.voices[0] = {-.04f,.07f}; s.activity[0] = 1;
        StereoSample y;
        for (int i = 0; i < 18000; ++i) y = executor.process(s, {});
        check(std::abs(y.left-.04f) < 2.e-4f && std::abs(y.right+.07f) < 2.e-4f,
            "magnitude/envelope endpoint did not retain main phase and donor level");
        // Distinct instances freeze independent donor spectra, not shared state.
        g.nodes.push_back({102,NodeType::spectralMorph}); g.nodes.back().mix = 1;
        g.nodes.back().spectralMorph.morph = 1; g.nodes.back().spectralMorph.freeze = true;
        g.nodes[2].spectralMorph.freeze = true;
        g.links.push_back({14,102,0}); g.links.push_back({1,102,1});
        g.links.push_back({102,13,0}); // Both Morph instances contribute to Output.
        check(bool(executor.requestGraph(g)), "independent parallel instances rejected");
        for (int i = 0; i < 6000; ++i) executor.process(s, {});
        auto frames = executor.spectralSnapshots();
        check(frames[1].spectrum.frozen && frames[3].spectrum.frozen, "independent captures not held");
        check(frames[1].spectrum.donor[0] != frames[3].spectrum.donor[0], "instances shared donor history");
    }
}
void bridgeConcurrency() {
    SpectralMorphBridge bridge; SpectralMorphFrames frames{};
    std::atomic<bool> done{false}, failed{false}, midpointRead{false}; std::atomic<unsigned> reads{0};
    const auto read = [&] {
        while (!done.load(std::memory_order_acquire)) {
            SpectralMorphFrame frame;
            if (!bridge.read(1, frame)) continue;
            const float expected = float(frame.spectrum.revision);
            for (size_t i = 0; i < 128; ++i)
                if (frame.spectrum.main[i] != expected || frame.spectrum.donor[i] != expected + 1 || frame.spectrum.result[i] != expected + 2) failed.store(true);
            ++reads;
            if (frame.spectrum.revision >= 5000 && frame.spectrum.revision < 10000)
                midpointRead.store(true, std::memory_order_release);
        }
    };
    std::thread a(read), b(read);
    audio = true;
    for (uint64_t i = 1; i <= 10000; ++i) {
        auto& f = frames[1]; f.nodeId = 100; f.spectrum.revision = i;
        f.spectrum.main.fill(float(i)); f.spectrum.donor.fill(float(i + 1)); f.spectrum.result.fill(float(i + 2));
        bridge.publish(frames);
        if (i == 5000) {
            // Scheduling handshake belongs to the test driver, never the audio
            // publication method. Under parallel CTest the producer can finish
            // before Windows gives either reader its first timeslice.
            audio = false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!midpointRead.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            audio = true;
        }
    }
    audio = false; done.store(true, std::memory_order_release); a.join(); b.join();
    std::cout << "Spectrum bridge concurrent reads=" << reads.load() << " incoherent=" << failed.load() << '\n';
    check(!failed && reads > 0 && midpointRead, "concurrent spectrum reader observed incoherent payload or was never scheduled");
}
int main() {
    try { contracts(); compensation(); endpointsAndInstances(); bridgeConcurrency(); check(!allocations && !destructions, "graph callback allocated or reclaimed");
        std::cout << "Spectral routing/latency/auxiliary tests passed\n"; return 0; }
    catch (const std::exception& e) { audio=false; std::cerr << e.what() << '\n'; return 1; }
}
