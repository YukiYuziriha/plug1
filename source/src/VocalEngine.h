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
    // slots 1..15 drive Chorale/Waveshaper/EQ/Crossover nodes 100..114 without
    // touching graph/state publication. Without opt-in, node-authored nested
    // settings remain authoritative. Added effects never depend on choraleOn.
    bool fxAutomation = false;
    std::array<float, 48> localMix{};
    bool localMixAutomation = false;
    float globalMix = 1.0f; // Untouched latency-aligned input vs complete graph + trim.
    VoiceSettings leadSettings{true, 0, 0, 0, 0, 0, 0, 0, 0, true};
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
    void prepare(double sampleRate, int inputChannels, int outputChannels, const Settings&);
    void reset(const Settings&);
    void process(const float* const* input, float* const* output, int count,
                 const Settings&, bool hostBypass = false);
    int latency() const noexcept { return latencySamples; }
    // Active routing plan's longest connected tail, plus source delay/latency.
    // Queued graph edits update the routing bound when audio installs them.
    // Tone EQ/LR4 phase and ADAA phase are not integer sample latency.
    double tailSeconds() const noexcept {
        return (latencySamples + maxDoubleDelay) / rate + routing.tailSeconds();
    }
    Meters meters() const noexcept;
    void resetMeters() noexcept;
    // Off audio, one serialized control caller. prepare must have completed.
    RoutingValidation requestGraph(const Graph& graph) { return routing.requestGraph(graph); }
    void collectRetiredGraphs() { routing.collectRetired(); }
    // Optional non-owning bridge; caller owns lifetime, audio must be quiescent.
    void setFxMonitor(FxMonitor* monitor) noexcept { routing.setFxMonitor(monitor); }
    std::array<VoiceSnapshot, 8> voiceSnapshots() const noexcept { return snapshots; }
    VoiceSnapshot leadSnapshot() const noexcept { return leadState; }

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
        float appliedPitch = 0, appliedDetune = 0, appliedFormant = 0, measuredEnergy = 0;
        bool running = false, harmonyRunning = false;
        float harmonyBlend = 0;
        int harmonyWarmup = 0;
    };

    struct Motion {
        float level, doubles, human, width, formant, formantMix, bypass;
        float advanced;
        double outputGainDb;
    };

    void updateControls(const Settings&, bool hostBypass);
    float leveler(float power);
    float consonantWeight(float sample);
    float matchedGain(LevelMatch&, float referencePower, float renderedPower);
    void processChunk(const float* const* input, float* const* output, int offset, int count);
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
    bool active = false;
    LevelMatch leadMatch;
    Stretch lead{421};
    Voice leadMotion{613};
    std::array<HarmonyShifter, 2> leadHarmony;
    std::array<DelayLine, 2> leadLocalDelay;
    float leadPitchBlend = 0, leadLocalBlend = 0, globalMix = 1;
    bool leadHarmonyRunning = false;
    int leadHarmonyWarmup = 0;
    std::array<Voice, 8> voices{Voice{1471}, Voice{5417}, Voice{7919}, Voice{104729},
                              Voice{130363}, Voice{155921}, Voice{196613}, Voice{262147}};
    RoutingExecutor routing;
    std::array<VoiceSnapshot, 8> snapshots{};
    VoiceSnapshot leadState{};
    std::array<DelayLine, 2> dryDelay, levelDelay;
    std::array<Buffer, 2> leadInput{}, leadRendered{};
    Buffer monoInput{}, articulationInput{};
    DelayLine articulationDelay;
    std::array<MeterDetector, 2> inputMeters{}, outputMeters{};
    // Bounded RMS normalization is not a limiter: individual peaks can change.
    double longInputEnergy = 0, longOutputEnergy = 0, longWeight = 0;
    float longStep = 0, normalizationDb = 0;
    float correctionDb = 0;
};

} // namespace vocalstack
