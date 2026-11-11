// Informational timings, not pass/fail CPU deadlines. Run without concurrent builds/tests.
#include "CreativeEffects.h"
#include "UtilityFilters.h"
#include "HarmonyShifter.h"
#include "Routing.h"
#include <fstream>
#include <memory>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace vocalstack;
namespace {
using Clock = std::chrono::steady_clock;
std::ofstream capture;
template <class Process>
void measure(const char* name, double rate, bool automate, Process process) {
    const int count = int(rate);
    std::vector<float> signal(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) signal[size_t(i)] = float(.07 * std::sin(i * 1382.3 / rate));
    double best = 1.e30, checksum = 0;
    for (int repeat = 0; repeat < 3; ++repeat) {
        double sum = 0;
        const auto start = Clock::now();
        for (int i = 0; i < count; ++i) {
            const auto y = process(signal[size_t(i)], automate && (i / 64) % 2 != 0);
            sum += std::abs(y[0]) + std::abs(y[1]);
        }
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        best = std::min(best, ms); checksum += sum;
    }
    if (capture.is_open()) for (int i = 0; i < count; ++i) {
        const auto y = process(signal[size_t(i)], automate && (i / 64) % 2 != 0);
        const std::array<float, 2> samples{{float(y[0]), float(y[1])}};
        capture.write(reinterpret_cast<const char*>(samples.data()), sizeof(samples));
    }
    std::cout << name << ',' << rate << ',' << (automate ? "automated" : "static")
              << ',' << best << ',' << checksum << '\n';
}
}
int main(int argc, char** argv) {
    if (argc == 2) { capture.open(argv[1], std::ios::binary); if (!capture) return 1; }
    std::cout << std::setprecision(10) << "module,rate,controls,best_ms_per_audio_second,checksum\n";
    for (double rate : {48000., 96000.}) for (bool automated : {false, true}) {
        {
            HarmonyShifter fx; fx.prepare(rate, rate == 48000 ? 1344 : 2688); fx.setSemitones(-7, 3);
            measure("Harmony -7st", rate, automated, [&](float x, bool move) {
                fx.setSemitones(move ? -5.f : -7.f, 3);
                const auto y = fx.process(x); return std::array<float, 2>{{y, y}};
            });
        }
        for (int effects : {0, 10}) {
            auto fx = std::make_unique<RoutingExecutor>(); fx->prepare(rate);
            auto graph = defaultGraph();
            if (effects) {
                graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [](const auto& l) { return l.destination == 13; }), graph.links.end());
                constexpr NodeType types[] = {NodeType::delay, NodeType::reverb, NodeType::compressor,
                    NodeType::bitcrusher, NodeType::granular, NodeType::xpFilter, NodeType::synthFilter,
                    NodeType::gate, NodeType::stereo, NodeType::gain};
                uint32_t previous = 12;
                for (size_t i = 0; i < std::size(types); ++i) {
                    RoutingNode node{uint32_t(100 + i), types[i]}; node.mix = .25f;
                    graph.nodes.push_back(node); graph.links.push_back({previous, node.id, 0}); previous = node.id;
                }
                graph.links.push_back({previous, 13, 0});
            }
            if (!fx->requestGraph(graph)) return 1;
            RoutingControls controls; controls.mix = .35f;
            measure(effects ? "Routing 10FX" : "Routing 0FX", rate, automated, [&](float x, bool) {
                RoutingSources sources; sources.lead = {x, x};
                for (size_t i = 0; i < 8; ++i) { sources.voices[i] = {x * .5f, x * .3f}; sources.activity[i] = 1; }
                const auto y = fx->process(sources, controls); return std::array<float, 2>{{y.left, y.right}};
            });
        }
        for (int mode = 0; mode < 13; ++mode) {
            UtilityEffect fx; UtilitySettings base, moving;
            base[UtilityParameter::synthMode] = float(mode);
            base[UtilityParameter::synthResonance] = 35;
            base[UtilityParameter::synthDrive] = 6;
            moving = base; moving[UtilityParameter::synthCutoff] = 1800;
            fx.prepare(rate, UtilityKind::synthFilter);
            measure(synthFilterConfigurations[size_t(mode)], rate, automated,
                [&](float x, bool move) { return fx.process(x, -x * .7f, move ? moving : base); });
        }
        for (auto kind : {UtilityKind::gain, UtilityKind::stereo, UtilityKind::gate, UtilityKind::xpFilter}) {
            UtilityEffect fx; UtilitySettings base, moving;
            base[UtilityParameter::xpResonance] = 35;
            base[UtilityParameter::xpDrive] = 6;
            moving = base; moving[UtilityParameter::gainDb] = -6;
            moving[UtilityParameter::stereoWidth] = 140; moving[UtilityParameter::gateThreshold] = -24;
            moving[UtilityParameter::xpCutoff] = 1800;
            fx.prepare(rate, kind);
            const char* name = kind == UtilityKind::gain ? "Gain" : kind == UtilityKind::stereo ? "Stereo"
                : kind == UtilityKind::gate ? "Gate" : "XP";
            measure(name, rate, automated,
                [&](float x, bool move) { return fx.process(x, -x * .7f, move ? moving : base); });
        }
        for (auto kind : {CreativeKind::delay, CreativeKind::reverb, CreativeKind::compressor, CreativeKind::bitcrusher, CreativeKind::granular}) {
            CreativeEffect fx; CreativeSettings base, moving;
            base[CreativeParameter::reverbPitchAmount] = 45;
            base[CreativeParameter::reverbSwell] = 250;
            moving = base; moving[CreativeParameter::delayTime] = 420;
            moving[CreativeParameter::reverbPitchAmount] = 65; moving[CreativeParameter::compressorThreshold] = -24;
            moving[CreativeParameter::crusherBits] = 10; moving[CreativeParameter::granularPitch] = -12;
            fx.prepare(rate, kind);
            const char* name = kind == CreativeKind::delay ? "Delay" : kind == CreativeKind::reverb ? "Shimmer"
                : kind == CreativeKind::compressor ? "Compressor" : kind == CreativeKind::bitcrusher ? "Crusher" : "Granular";
            measure(name, rate, automated,
                [&](float x, bool move) { return fx.process(x, -x * .7f, move ? moving : base); });
        }
    }
}
