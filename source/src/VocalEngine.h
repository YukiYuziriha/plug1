#pragma once

#include "Metering.h"
#include "DoublerSettings.h"
#include "Routing.h"
#include "HarmonyShifter.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <array>

#include <cstdint>
#include <vector>

namespace vocalstack {

class Vst3SpatialEqAutomation;

struct Settings {
    float level = 0.50f;
    float doubles = 0.35f; // True Mix: 0 = processed lead, 1 = doubles only.
    float human = 0.35f;
    float width = 0.65f;
    float formant = 0.0f;
    bool levelOn = true;
    bool doubleOn = true;
    bool formantOn = true;
    bool bypass = false;
    float outputGainDb = 0.0f;
    bool advanced = false;
    std::array<VoiceSettings, 8> voiceSettings = basicVoiceSettings(0.35f);
    bool choraleOn = false;
    float choraleMix = 0.25f, choraleMotion = 0.35f;
    std::array<FxSettings, 16> fxSettings{};
    // Opt-in processor bridge. Slot 0 always uses the legacy Chorale fields;
    // slots 1..15 drive tone, creative and utility nodes 100..114 without
    // touching graph/state publication. Without opt-in, node-authored nested
    // settings remain authoritative. Added effects never depend on choraleOn.
    bool fxAutomation = false;
    std::array<float, 48> localMix{};
    bool localMixAutomation = false;
    double tempoBpm = 120;
    float globalMix = 1.0f; // Untouched latency-aligned input vs complete graph + trim.
    VoiceSettings leadSettings{true, 0, 0, 0, 0, 0, 0, 0, 0, true};
    float sidechainBlend = 0.0f;
    bool sidechainBlendAutomation = false;
    VoiceSettings sidechainSettings{false, 0, 0, 0, 0, 0, 0, 0, 0, true};
};

// Audio-thread value snapshot. The processor must publish copies via its own
// atomic bridge; UI/control threads must not read a running engine directly.
// Pan includes Width; detune includes Human motion; delay excludes algorithmic
// latency. gainDb is the smoothed user gain before internal level matching;
// activity is measured post-envelope linear RMS, not a normalized UI brightness.
struct VoiceSnapshot {
    bool present = false, enabled = false;
    float pan = 0, pitchSemitones = 0, detuneCents = 0, delayMs = 0;
    float formantSemitones = 0, gainDb = 0, activity = 0;
};

class VocalEngine {
public:
    static constexpr int defaultLatency = 1344;
    // Quiescent audio required. prepare installs defaultGraph(); restore a saved
    // graph with requestGraph AFTER prepare. reset preserves the active graph.
    // With no vocal budget, spectral Lead/Sidechain and all generated voices
    // stay bypassed until reprepare. A set bit in independentVoices keeps that enabled slot
    // eligible even at main Mix zero (use a conservative union across graph edits).
    void prepare(double sampleRate, int inputChannels, int outputChannels, const Settings&,
                 int routingLatencyReserve = 0, bool needsVocalLatency = true,
                 std::uint8_t independentVoices = 0xff);
    void reset(const Settings&);
    void process(const float* const* input, float* const* output, int count,
                 const Settings&, bool hostBypass = false,
                 const float* const* auxiliary = nullptr, int auxiliaryChannels = 0,
                 Vst3SpatialEqAutomation* spatialAutomation = nullptr, int hostOffset = 0);
    int vocalLatency() const noexcept { return latencySamples; }
    int latency() const noexcept { return vocalLatency() + routingReserve; }
    // Active routing plan's longest connected tail, plus source delay/latency.
    // Queued graph edits update the routing bound when audio installs them.
    // Tone EQ/LR4 phase and ADAA phase are not integer sample latency.
    double tailSeconds() const noexcept {
        return (latencySamples + routingReserve + maxDoubleDelay) / rate + routing.tailSeconds();
    }
    Meters meters() const noexcept;
    void resetMeters() noexcept;
    // Off audio, one serialized control caller. prepare must have completed.
    RoutingValidation requestGraph(const Graph& graph) { return routing.requestGraph(graph); }
    RoutingValidation requestGraph(const Graph& graph, std::uint8_t connected, std::uint8_t independent) {
        return routing.requestGraph(graph, unsigned(connected) | (unsigned(independent) << 8));
    }
    void collectRetiredGraphs() { routing.collectRetired(); }
    // Optional non-owning bridge; caller owns lifetime, audio must be quiescent.
    void setFxMonitor(FxMonitor* monitor) noexcept { routing.setFxMonitor(monitor); }
    void setGlitchSpan(double ppq, double bpm, bool playing, bool discontinuity, uint32_t triggers) noexcept {
        routing.setGlitchSpan(ppq, bpm, playing, discontinuity, triggers);
    }
    std::array<VoiceSnapshot, 8> voiceSnapshots() const noexcept { return snapshots; }
    VoiceSnapshot leadSnapshot() const noexcept { return leadState; }
        // Same-owner audio copies; Processor owns concurrent publication.
        RoutingGrainSnapshot grainSnapshot() const noexcept;
    SpectralMorphFrames spectralSnapshots() const noexcept { return routing.spectralSnapshots(); }
        uint64_t requestedGrainRevision() const noexcept { return routing.requestedGrainRevision(); }
        uint64_t grainEpoch() const noexcept { return routing.grainEpoch(); }

private:
    static constexpr int quantum = 64;
    using Buffer = std::array<float, quantum>;
    using Stretch = signalsmith::stretch::SignalsmithStretch<float>;

    struct DelayLine {
        void prepare(int capacity);
        void clear();
        void push(float sample);
        float read(float delay) const;
        std::vector<float> data;
        int next = 0;
    };

    struct LevelMatch {
        float referenceEnergy = 0, renderedEnergy = 0, gain = 1;
    };

    struct Voice {
        explicit Voice(unsigned seed) : stretch(seed), random(seed), initialSeed(seed) {}
        void advance(float seconds, float human);
        float uniform();
        Stretch stretch;
        HarmonyShifter harmony;
        DelayLine delay;
        LevelMatch levelMatch;
        Buffer rendered{};
        std::uint32_t random, initialSeed;
        float targetTime = 0, targetPitch = 0, targetTone = 0;
        float time = 0, pitch = 0, tone = 0, remaining = 0;
        VoiceSettings controls{};
        float amplitude = 0, presence = 0, delaySamples = 0;
        float finePitch = 0, appliedPitch = 0, appliedDetune = 0, appliedFormant = 0, measuredEnergy = 0;
        bool running = false, harmonyRunning = false;
        float harmonyBlend = 0;
        int harmonyWarmup = 0, wakeRemaining = 0;
    };

    struct StereoSpectral {
        explicit StereoSpectral(unsigned seed) : stretch(seed) {}
        Stretch stretch;
        std::array<HarmonyShifter, 2> harmony;
        std::array<Buffer, 2> input{}, rendered{};
        LevelMatch match;
        float pitchBlend = 0, wakeBlend = 0;
        int wakeRemaining = 0, harmonyWarmup = 0;
        bool running = false, harmonyRunning = false;
    };

    struct Motion {
        float level, doubles, human, width, formant, formantMix, bypass;
        float advanced;
        double outputGainDb;
    };

    void updateControls(const Settings&, bool hostBypass);
    void resetLeveler() noexcept;
    float leveler(float power);
    bool voiceRequested(size_t index, const VoiceSettings&, bool mainMixActive) const noexcept;
    float consonantWeight(float sample);
    float matchedGain(LevelMatch&, float referencePower, float renderedPower);
    void advanceLocalVoice(Voice&, float seconds);
    void smoothLocalVoice(VoiceSettings&, const VoiceSettings&, bool snap = false) const;
    void renderStereoSpectral(StereoSpectral&, const Voice&, bool wanted, bool independentFormant,
                              bool update, int channels, int count);
    StereoSample blendStereoSpectral(StereoSpectral&, StereoSample reference, int sample,
                                     int rightChannel, float& blend);
    StereoSample applyLocalVoice(Voice&, std::array<DelayLine, 2>&, StereoSample, float timeStep);
    void processChunk(const float* const* input, float* const* output, int offset, int count,
                      const float* const* auxiliary, int auxiliaryChannels,
                      Vst3SpatialEqAutomation*, int hostOffset);
    float coefficient(double seconds) const;

    double rate = 48000.0;
    int inChannels = 1, outChannels = 2, latencySamples = 0, maxDoubleDelay = 0, untilUpdate = 0;
    Settings targets{};
    Motion controls{};
    float parameterStep = 0, rmsStep = 0, gainDownStep = 0, gainUpStep = 0;
    float activityStep = 0, energyStep = 0, matchDownStep = 0, matchUpStep = 0;
    float lowPassStep = 0, edgeStep = 0, bodyStep = 0, articulationStep = 0;
    float rms = 0, activity = 0, gainDb = 0, gain = 1;
    float lowPass = 0, edgeEnergy = 0, bodyEnergy = 0, articulation = 1;
    bool active = false, levelerRunning = false, articulationRunning = false;
    unsigned routedVoices = 0xffffu, defaultRoutedVoices = 0xffffu;
    StereoSpectral leadSpectral{421}, sidechainSpectral{829};
    Voice leadMotion{613}, sidechainMotion{947};
    std::array<DelayLine, 2> leadLocalDelay, sidechainLocalDelay;
    float leadLocalBlend = 0, sidechainLocalBlend = 0, globalMix = 1;
    bool leadIndependentFormant = false, sidechainRunning = false;
    std::array<Voice, 8> voices{Voice{1471}, Voice{5417}, Voice{7919}, Voice{104729},
                              Voice{130363}, Voice{155921}, Voice{196613}, Voice{262147}};
    RoutingExecutor routing;
    std::array<VoiceSnapshot, 8> snapshots{};
    VoiceSnapshot leadState{};
    int routingReserve = 0;
    std::array<DelayLine, 2> rackDryDelay;
    std::array<DelayLine, 2> dryDelay, levelDelay, auxiliaryDelay;

    Buffer monoInput{}, articulationInput{};
    DelayLine articulationDelay;
    std::array<MeterDetector, 2> inputMeters{}, outputMeters{};
    // Bounded RMS normalization is not a limiter: individual peaks can change.
    double longInputEnergy = 0, longOutputEnergy = 0, longWeight = 0;
    float longStep = 0, normalizationDb = 0;
    float correctionDb = 0;
};

} // namespace vocalstack
