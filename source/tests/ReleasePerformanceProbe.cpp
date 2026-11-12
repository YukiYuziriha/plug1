// Isolated Release TU; build recipe and measurement limits: docs/Release-1.9.0-Validation.md.
#include "PluginProcessor.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace vocalstack;
namespace {
using Clock = std::chrono::steady_clock;
constexpr double pi = 3.14159265358979323846;
constexpr int maxBlock = 257;
constexpr int irregular[] = {32, 137, 1, 64, 257, 7, 31, 128, 33, 63};
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
double seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
void set(Processor& p, const juce::String& id, float value) {
    auto* parameter = p.parameters.getParameter(id);
    require(parameter != nullptr, "missing fixture parameter");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
Graph heavyGraph() {
    auto g = defaultGraph();
    g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(), [](const auto& n) { return n.id == 12; }), g.nodes.end());
    g.links.erase(std::remove_if(g.links.begin(), g.links.end(), [](const auto& l) {
        return l.source == 12 || l.destination == 12;
    }), g.links.end());
    // Same ten-effect family mix as EffectsBenchmark, but fed by real pitched voices.
    constexpr NodeType types[] = {NodeType::delay, NodeType::reverb, NodeType::compressor,
        NodeType::bitcrusher, NodeType::granular, NodeType::xpFilter, NodeType::synthFilter,
        NodeType::gate, NodeType::stereo, NodeType::gain};
    uint32_t previous = 11;
    for (size_t i = 0; i < std::size(types); ++i) {
        RoutingNode node{uint32_t(100 + i), types[i]};
        node.mix = .6f;
        node.creative[CreativeParameter::reverbPitchAmount] = 65;
        node.creative[CreativeParameter::reverbSwell] = 250;
        node.creative[CreativeParameter::granularPitch] = -12;
        node.utility[UtilityParameter::synthResonance] = 35;
        node.utility[UtilityParameter::synthDrive] = 6;
        g.nodes.push_back(node);
        g.links.push_back({previous, node.id, 0}); previous = node.id;
    }
    g.links.push_back({previous, 13, 0});
    return g;
}
struct Instance {
    std::unique_ptr<Processor> processor = std::make_unique<Processor>();
    juce::AudioBuffer<float> audio{2, maxBlock};
    juce::MidiBuffer midi;
    uint64_t cursor = 0;
    Instance(double rate) {
        auto& p = *processor;
        p.setAdvancedEnabled(true);
        set(p, "bypass", 0); set(p, "doubleOn", 1); set(p, "globalMix", 100);
        set(p, "double", 75); set(p, "formantOn", 1);
        constexpr float pitches[] = {-12, -9, -7, -4, 3, 5, 7, 12};
        for (size_t v = 0; v < 8; ++v) {
            set(p, voicePresentParameterId(v), 1); set(p, voiceParameterId(v, "On"), 1);
            set(p, voicePitchParameterId(v), pitches[v]);
            set(p, voiceParameterId(v, "Gain"), -12);
            set(p, voiceParameterId(v, "Formant"), v % 2 ? 3.f : -3.f);
        }
        juce::String error;
        require(p.setRoutingGraph(heavyGraph(), error), "heavy graph rejected");
        ModulationState mods;
        for (uint32_t i = 0; i < 4; ++i) {
            Modulator lfo; lfo.id = i + 1; lfo.rateHz = .37f + float(i) * .61f; lfo.phase = float(i) * .19f;
            mods.modulators.push_back(lfo);
            for (size_t v = i; v < 8; v += 4)
                mods.assignments.push_back({i + 1, voiceParameterId(v, "Pan"), .2f, true});
            for (size_t slot = i + 1; slot <= 10; slot += 4)
                mods.assignments.push_back({i + 1, fxParameterId(slot, "Mix"), .12f, true});
        }
        require(p.setModulationState(mods, error), "four LFO fixture rejected");
        p.prepareToPlay(rate, maxBlock);
        const auto settings = p.settings();
        for (size_t v = 0; v < 8; ++v)
            require(settings.voiceSettings[v].enabled && settings.voiceSettings[v].present && settings.voiceSettings[v].pitchSemitones == pitches[v], "pitched voice fixture inactive");
    }
    ~Instance() { processor->releaseResources(); }
    void fill(int n, double rate) {
        audio.setSize(2, n, false, false, true);
        for (int j = 0; j < n; ++j) {
            const double t = double(cursor + uint64_t(j)) / rate;
            audio.setSample(0, j, float(.08 * std::sin(2*pi*173*t) + .025 * std::sin(2*pi*1091*t)));
            audio.setSample(1, j, float(.07 * std::sin(2*pi*181*t) + .02 * std::cos(2*pi*997*t)));
        }
        cursor += uint64_t(n);
    }
    double checkOutput() const {
        double sum = 0;
        for (int ch = 0; ch < 2; ++ch) for (int j = 0; j < audio.getNumSamples(); ++j) {
            const auto x = audio.getSample(ch, j);
            require(std::isfinite(x), "nonfinite processor output"); sum += std::abs(x);
        }
        return sum;
    }
};
struct Observation { int block; double us, ratio; };
void report(const char* metric, double rate, int instances, int regular, bool readers,
            const std::vector<Observation>& all, int filter, double checksum, bool capped) {
    std::vector<double> us, ratios;
    for (const auto& x : all) if (!filter || x.block == filter) { us.push_back(x.us); ratios.push_back(x.ratio); }
    if (us.empty()) return;
    std::sort(us.begin(), us.end()); std::sort(ratios.begin(), ratios.end());
    const auto p95 = size_t(std::ceil(.95 * double(us.size()))) - 1;
    std::cout << metric << ',' << rate << ',' << instances << ',';
    if (regular) std::cout << regular; else std::cout << "irregular";
    std::cout << ',' << (readers ? "read30Hz" : "noReaders") << ',' << filter << ',' << us.size()
        << ',' << std::accumulate(us.begin(), us.end(), 0.) / double(us.size()) << ',' << us[p95] << ',' << us.back()
        << ',' << std::accumulate(ratios.begin(), ratios.end(), 0.) / double(ratios.size()) << ',' << ratios[p95] << ',' << ratios.back()
        << ',' << checksum << ',' << capped << '\n';
}
void run(double rate, int count, int regular, bool readers) {
    std::vector<std::unique_ptr<Instance>> instances;
    for (int i = 0; i < count; ++i) instances.push_back(std::make_unique<Instance>(rate));
    const auto blockAt = [&](size_t i) { return regular ? regular : irregular[i % std::size(irregular)]; };
    // Warm input/history and pitch engines; excluded from every timing distribution.
    for (uint64_t samples = 0; samples < uint64_t(rate * .15);) {
        for (auto& p : instances) { p->fill(maxBlock, rate); p->processor->processBlock(p->audio, p->midi); }
        samples += maxBlock;
    }
    for (auto& p : instances) {
        RoutingGrainFrame frame;
        require(p->processor->readGrainSnapshot(5, frame) && frame.nodeId == 104 && frame.grains.count > 0,
                "release grain bridge must publish actual active grains");
        require(p->processor->getModulationSnapshot().modulators.size() == 4, "four active LFOs required");
                for (const auto& voice : p->processor->getVoiceSnapshots())
                    require(voice.present && voice.enabled && voice.activity > 0 && voice.pitchSemitones != 0,
                            "all eight pitched voice paths must be active");
    }
    std::vector<Observation> callbacks, snapshots;
    callbacks.reserve(8192); snapshots.reserve(8192);
    uint64_t samples = 0, nextRead = 0;
    double checksum = 0;
    const auto wallStart = Clock::now();
    bool capped = false;
    for (size_t b = 0; b < 8192; ++b) {
        const int n = blockAt(b);
        for (auto& p : instances) p->fill(n, rate);
        const auto begin = Clock::now();
        for (auto& p : instances) p->processor->processBlock(p->audio, p->midi);
        const double elapsed = seconds(begin);
        callbacks.push_back({n, elapsed * 1.e6, elapsed * rate / n});
        // This is UI-side work, NOT part of the audio callback deadline.
        if (readers && samples >= nextRead) {
            const auto readBegin = Clock::now();
            for (auto& p : instances) {
                RoutingGrainFrame grain;
                require(p->processor->readGrainSnapshot(5, grain), "grain read failed");
                const auto voices = p->processor->getVoiceSnapshots();
                const auto lead = p->processor->getLeadSnapshot();
                const auto mods = p->processor->getModulationSnapshot();
                checksum += double(grain.grains.count) + double(voices.size()) + double(mods.targets.size());
                (void)lead;
            }
            const double readTime = seconds(readBegin);
            snapshots.push_back({n, readTime * 1.e6, readTime * rate / n});
            nextRead += uint64_t(rate / 30);
        }
        for (auto& p : instances) checksum += p->checkOutput();
        samples += uint64_t(n);
        if (samples >= uint64_t(rate * .75) && callbacks.size() >= 256) break;
        if (seconds(wallStart) >= .7) { capped = true; break; }
    }
    report("callback", rate, count, regular, readers, callbacks, 0, checksum, capped);
    if (!regular) for (int n : irregular) report("callback_by_size", rate, count, regular, readers, callbacks, n, checksum, capped);
    report("snapshot_read", rate, count, regular, readers, snapshots, 0, checksum, capped);
}
}
int main(int argc, char** argv) {
    // No processor construction, audio processing, or timings without explicit lane acknowledgement.
    if (argc != 2 || std::string_view(argv[1]) != "--quiet-lane-confirmed") {
        std::cout << "Ready. Coordinator must confirm quiet lane before --quiet-lane-confirmed.\n";
        return argc == 1 ? 0 : 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
    try {
        std::cout << std::setprecision(9)
            << "# Release full processor; grain publication ALWAYS enabled; 8 pitched voices, 10FX, 4LFO/18 assignments.\n"
            << "# Serial independent instances share ONE deadline; ratios are observations, not guarantees or pass thresholds.\n"
            << "# snapshot_read ratios use the coincident block deadline for scale only, not audio callback demand.\n"
            << "metric,rate,instances,pattern,readers,block_filter,count,mean_us,p95_us,max_us,mean_deadline_ratio,p95_deadline_ratio,max_deadline_ratio,checksum,capped\n";
        const auto start = Clock::now();
        for (double rate : {48000., 96000.}) for (int count : {1, 2})
            for (int block : {32, 64, 137, 0}) for (bool readers : {false, true}) {
                if (seconds(start) > 55) {
                    std::cerr << "INCOMPLETE: 55s soft budget exhausted between cases; remaining matrix not measured\n";
                    return 3;
                }
                run(rate, count, block, readers);
            }
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
