#include "Routing.h"
#include <complex>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
bool audio = false;
int allocations = 0, destructions = 0;
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
void near(double a, double b, double tolerance, const char* text) { check(std::abs(a-b) < tolerance, text); }
constexpr double pi = 3.14159265358979323846;
}
void* operator new(size_t n) {
    if (audio) ++allocations;
    if (auto p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { if (audio && p) ++destructions; std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }
using namespace vocalstack;

void responses() {
    for (double rate : {32000., 44100., 48000., 96000., 192000.}) for (int count : {2,3})
        for (double hz : {30., 100., 250., 600., 2000., 4000., 9000.}) {
            Multiband split; split.prepare(rate);
            std::complex<double> input{}, output{};
            std::array<std::complex<double>,3> spectrum{};
            const int warm = int(rate*.6), length = int(rate*.25);
            for (int i = 0; i < warm+length; ++i) {
                const double phase = 2*pi*hz*i/rate;
                const float x = float(.2*std::cos(phase));
                audio = true; const auto b = split.process(x, -x, count, 250, 4000); audio = false;
                for (int j = 0; j < 3; ++j) {
                    check(std::isfinite(b[j][0]), "nonfinite crossover");
                    near(b[j][0], -b[j][1], 1e-8, "stereo crossover mismatch");
                    if (i >= warm) spectrum[j] += double(b[j][0])*std::polar(1.,-phase);
                }
                if (count == 2) check(b[2][0] == 0 && b[2][1] == 0, "two-band third port not silent");
                if (i >= warm) {
                    const auto z = std::polar(1.,-phase);
                    input += double(x)*z;
                    output += double(b[0][0]+b[1][0]+b[2][0])*z;
                }
            }
            near(std::abs(output/input), 1., .003, "LR4 recombination magnitude not flat");
            if (hz == 250) near(std::abs(spectrum[0]/input), .5, .003, "LR4 low cutoff not -6 dB");
            if (count == 2 && hz == 250) {
                near(std::abs(spectrum[1]/input), .5, .003, "LR4 high cutoff not -6 dB");
                check(std::abs(output/input-std::complex<double>{1,0}) > 1, "incorrect dry-null claim");
            }
            split.reset(); Multiband fresh; fresh.prepare(rate);
            for (int i = 0; i < 500; ++i) {
                const float x = float(std::sin(i*.07)*.1);
                check(split.process(x,0,count) == fresh.process(x,0,count), "reset retained crossover history");
            }
        }
}
Graph bandGraph(int count = 3, bool duplicate = false) {
    Graph g{{{1,NodeType::lead},{100,NodeType::crossover},{2,NodeType::output}},{{1,100,0}}};
    g.nodes[1].bandCount = count;
    for (int p = 0; p < count; ++p) g.links.push_back({100,2,0,p});
    if (duplicate) {
        g.nodes.push_back({101,NodeType::equalizer});
        g.links.push_back({100,101,0,0}); g.links.push_back({101,2,0});
    }
    return g;
}
void routing() {
    for (int count : {2,3}) for (bool duplicate : {false,true}) {
        RoutingExecutor executor; executor.prepare(48000);
        auto g = bandGraph(count,duplicate);
        check(bool(executor.requestGraph(g)), "band routing rejected");
        Multiband reference; reference.prepare(48000);
        for (int i = 0; i < 10000; ++i) {
            RoutingSources s; s.lead = {float(.2*std::sin(i*.03)),float(.1*std::cos(i*.13))};
            const auto b = reference.process(s.lead.left,s.lead.right,count);
            audio = true; const auto out = executor.process(s,{}); audio = false;
            if (i <= 3000 || i > 3500) {
                near(out.left,b[0][0]+b[1][0]+b[2][0],2e-6,"complementary/duplicated low merge attenuated");
                near(out.right,b[0][1]+b[1][1]+b[2][1],2e-6,"band merge right mismatch");
            }
            if (i == 3000) {
                std::reverse(g.nodes.begin(),g.nodes.end());
                check(bool(executor.requestGraph(g)), "reordered crossover rejected");
            }
        }
    }
    auto g = bandGraph();
    g.links.push_back(g.links.back()); check(!RoutingExecutor::validate(g), "duplicate source port accepted");
    g = bandGraph(); g.links.back().sourcePin = 3;
    check(!RoutingExecutor::validate(g), "fourth port accepted");
    g = bandGraph(); g.links[0].sourcePin = 1;
    check(!RoutingExecutor::validate(g), "non-crossover multiple outputs accepted");
    g = bandGraph(); g.nodes[1].bandCount = 4;
    check(!RoutingExecutor::validate(g), "invalid band count accepted");
    g = bandGraph(); g.nodes[1].crossoverHigh = 20;
    check(!RoutingExecutor::validate(g), "crossed cutoffs accepted");
    g = bandGraph(); g.links.clear(); check(bool(RoutingExecutor::validate(g)), "disconnected crossover rejected");
    // Legacy Sum migration preserves each band's source port, not only node ID.
    g = bandGraph(); g.nodes.push_back({3,NodeType::sum});
    for (auto& l : g.links) if (l.destination == 2) l.destination = 3;
    g.links.push_back({3,2,0});
    g = removeLegacySumNodes(g);
    check(bool(RoutingExecutor::validate(g)), "band ports lost in Sum migration");
    for (int p = 0; p < 3; ++p) {
        bool found = false;
        for (const auto& l : g.links) if (l.source == 100 && l.sourcePin == p && l.destination == 2) found = true;
        check(found,"migrated band missing");
    }
}
void automationAndSilence() {
    Multiband split; split.prepare(48000);
    for (int i = 0; i < 20000; ++i) {
        const int bands = (i/137)%2+2;
        const float lo = i%13 == 0 ? std::numeric_limits<float>::quiet_NaN() : float(10+i%2000);
        const float hi = i%17 == 0 ? std::numeric_limits<float>::infinity() : float(3000+i%10000);
        audio = true; const auto b = split.process(.1f,-.2f,bands,lo,hi); audio = false;
        for (const auto& channel : b) for (float x : channel) check(std::isfinite(x),"automation poisoned crossover");
    }
    split.reset();
    for (int i = 0; i < 5000; ++i)
        for (const auto& band : split.process(0,0)) for (float x : band) check(x == 0,"reset silence failed");
    check(Multiband::latencySamples == 0 && Waveshaper::latencySamples == 0 && Equalizer::latencySamples == 0,"latency changed");
}
int main() {
    try {
        responses(); routing(); automationAndSilence();
        check(allocations == 0 && destructions == 0,"audio allocation/destruction");
        std::cout << "Multiband: LR4 magnitude/phase, rates, stereo, ports, complementary and duplicate-band provenance, state, automation and RT ownership passed\n";
    } catch (const std::exception& e) { audio = false; std::cerr << e.what() << '\n'; return 1; }
}
