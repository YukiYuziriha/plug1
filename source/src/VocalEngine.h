#pragma once

#include <signalsmith-stretch/signalsmith-stretch.h>
#include <array>
#include <cstdint>
#include <vector>

namespace vocalstack {

struct Settings {
    float level = 0.50f;
    float doubles = 0.35f;
    float human = 0.35f;
    float width = 0.65f;
    float formant = 0.0f;
    bool levelOn = true;
    bool doubleOn = true;
    bool formantOn = true;
    bool bypass = false;
};

struct Meters {
    float input = 0.0f;
    float output = 0.0f;
    float correctionDb = 0.0f;
};

class VocalEngine {
public:
    static constexpr int defaultLatency = 1344;
    void prepare(double sampleRate, int inputChannels, int outputChannels, const Settings&);
    void reset(const Settings&);
    void process(const float* const* input, float* const* output, int count,
                 const Settings&, bool hostBypass = false);
    int latency() const noexcept { return latencySamples; }
    double tailSeconds() const noexcept { return (latencySamples + maxDoubleDelay) / rate; }
    Meters meters() const noexcept { return lastMeters; }

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

    struct Voice {
        explicit Voice(unsigned seed) : stretch(seed), random(seed), initialSeed(seed) {}
        void advance(float seconds, float human);
        float uniform();
        Stretch stretch;
        DelayLine delay;
        Buffer rendered{};
        std::uint32_t random, initialSeed;
        float targetTime = 0, targetPitch = 0, targetTone = 0;
        float time = 0, pitch = 0, tone = 0, remaining = 0;
    };

    struct Motion {
        float level, doubles, human, width, formant, formantMix, bypass;
    };

    void updateControls(const Settings&, bool hostBypass);
    float leveler(float power);
    float consonantWeight(float sample);
    void processChunk(const float* const* input, float* const* output, int offset, int count);
    float coefficient(double seconds) const;

    double rate = 48000.0;
    int inChannels = 1, outChannels = 2, latencySamples = 0, maxDoubleDelay = 0, untilUpdate = 0;
    Settings targets{};
    Motion controls{};
    float parameterStep = 0, rmsStep = 0, gainDownStep = 0, gainUpStep = 0;
    float activityStep = 0, meterDecay = 0, energyStep = 0, matchDownStep = 0, matchUpStep = 0;
    float lowPassStep = 0, edgeStep = 0, bodyStep = 0, articulationStep = 0;
    float rms = 0, activity = 0, gainDb = 0, gain = 1, match = 1;
    float leadEnergy = 0, mixEnergy = 0, lowPass = 0, edgeEnergy = 0, bodyEnergy = 0, articulation = 1;
    bool active = false;
    Stretch lead{421};
    std::array<Voice, 2> voices{Voice{1471}, Voice{5417}};
    std::array<DelayLine, 2> dryDelay, levelDelay;
    std::array<Buffer, 2> leadInput{}, leadRendered{};
    Buffer monoInput{}, articulationInput{};
    DelayLine articulationDelay;
    Meters lastMeters{};
};

} // namespace vocalstack
