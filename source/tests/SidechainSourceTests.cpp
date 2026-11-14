#include "Routing.h"
#include "VocalEngine.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
thread_local bool rendering = false;
std::atomic<unsigned> allocations{0}, destructions{0};
void check(bool x, const char* reason) { if (!x) throw std::runtime_error(reason); }
}
void* operator new(std::size_t n) {
    if (rendering) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (p && rendering) ++destructions; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

using namespace vocalstack;
int main() {
    try {
        Graph graph{{{14, NodeType::sidechain}, {13, NodeType::output}}, {{14, 13, 0}}};
        check(bool(RoutingExecutor::validate(graph)), "SIDECHAIN source rejected");
        check(routingInputCount(NodeType::sidechain) == 0, "source accepts an invented input");
        auto invalid = graph;
        invalid.nodes.push_back({15, NodeType::sidechain});
        check(!RoutingExecutor::validate(invalid), "duplicate external source accepted");
        invalid = graph; invalid.links.push_back({13, 14, 0});
        check(!RoutingExecutor::validate(invalid), "wire into SIDECHAIN source accepted");
        RoutingExecutor executor; executor.prepare(48000);
        check(bool(executor.requestGraph(graph)), "cannot prepare source graph");
        RoutingSources sources; sources.lead = {.9f, -.7f};
        auto output = executor.process(sources, {});
        check(output.left == 0 && output.right == 0, "missing donor substituted main input");
        sources.sidechain = {.125f, -.25f};
        for (int i = 0; i < 1000; ++i) output = executor.process(sources, {});
        check(output.left == .125f && output.right == -.25f, "source normalized or swapped channels");
        // Source does not consume an FX slot or reduce the 16-effect allowance.
        auto capacity = graph;
        for (int i = 0; i < 16; ++i) capacity.nodes.push_back({uint32_t(100 + i), NodeType::gain});
        check(bool(RoutingExecutor::validate(capacity)), "external source consumed an FX slot");

        for (double rate : {44100., 48000., 96000.}) for (int mainChannels : {1, 2}) for (int donorChannels : {0, 1, 2}) {
            auto engine = std::make_unique<VocalEngine>();
            Settings settings;
            settings.levelOn = false; settings.doubleOn = false; settings.globalMix = 1;
            engine->prepare(rate, mainChannels, 2, settings);
            check(bool(engine->requestGraph(graph)), "engine source graph rejected");
            const int length = engine->latency() + 2300;
            std::array<std::vector<float>, 2> main, donor, result;
            for (auto* buffers : {&main, &donor, &result}) for (auto& buffer : *buffers) buffer.resize(size_t(length));
            for (int i = 0; i < length; ++i) {
                main[0][size_t(i)] = .19f; main[1][size_t(i)] = -.31f;
                donor[0][size_t(i)] = .1f * std::sin(float(i) * .053f);
                donor[1][size_t(i)] = -.17f * std::cos(float(i) * .079f);
            }
            constexpr std::array<int, 7> blocks{1, 137, 3, 512, 17, 63, 257};
            int offset = 0, block = 0;
            while (offset < length) {
                const int count = std::min(blocks[size_t(block++ % blocks.size())], length - offset);
                const float* in[]{main[0].data() + offset, main[1].data() + offset};
                const float* aux[]{donor[0].data() + offset, donor[1].data() + offset};
                float* out[]{result[0].data() + offset, result[1].data() + offset};
                rendering = true;
                engine->process(in, out, count, settings, false, donorChannels ? aux : nullptr, donorChannels);
                rendering = false;
                offset += count;
            }
            for (int c = 0; c < 2; ++c) for (int i = 0; i < length; ++i) {
                const float expected = donorChannels && i >= engine->latency()
                    ? donor[size_t(donorChannels == 1 ? 0 : c)][size_t(i - engine->latency())] : 0.f;
                check(std::abs(result[size_t(c)][size_t(i)] - expected) < 2.e-5f,
                    "source latency / absence / mono duplication / stereo identity mismatch");
            }
        }
        check(allocations == 0 && destructions == 0, "audio source path allocated or reclaimed memory");
        std::cout << "SIDECHAIN routing/source-engine tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        rendering = false; std::cerr << error.what() << '\n'; return 1;
    }
}
