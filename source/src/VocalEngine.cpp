#include "VocalEngine.h"

#include <algorithm>
#include <cmath>

namespace vocalstack {
namespace {
float mix(float a, float b, float amount) { return a + (b - a) * amount; }
float powerToDb(float power) { return 10.0f * std::log10(std::max(power, 1.0e-12f)); }
float dbToGain(float db) { return std::exp(db * 0.115129255f); }

float sampleAt(const float* input, int index) {
    const float sample = input[index];
    return std::isfinite(sample) ? sample : 0.0f;
}
}

void VocalEngine::DelayLine::prepare(int capacity) {
    data.resize(static_cast<size_t>(capacity));
    clear();
}

void VocalEngine::DelayLine::clear() {
    std::fill(data.begin(), data.end(), 0.0f);
    next = 0;
}

void VocalEngine::DelayLine::push(float sample) {
    data[static_cast<size_t>(next)] = sample;
    if (++next == static_cast<int>(data.size()))
        next = 0;
}

float VocalEngine::DelayLine::read(float delay) const {
    const auto length = static_cast<int>(data.size());
    const auto whole = static_cast<int>(delay);
    auto newer = next - 1 - whole;
    if (newer < 0)
        newer += length;
    const auto older = newer == 0 ? length - 1 : newer - 1;
    return mix(data[static_cast<size_t>(newer)], data[static_cast<size_t>(older)],
               delay - static_cast<float>(whole));
}

float VocalEngine::Voice::uniform() {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return static_cast<float>(random >> 8) / 16777216.0f;
}

void VocalEngine::Voice::advance(float seconds, float human) {
    remaining -= seconds;
    if (remaining <= 0.0f) {
        remaining = 0.105f + uniform() * 0.28f;
        targetTime = 2.0f * uniform() - 1.0f;
        targetPitch = 2.0f * uniform() - 1.0f;
        targetTone = 2.0f * uniform() - 1.0f;
    }
    const float step = seconds * (6.0f + human * 5.0f);
    time = mix(time, targetTime, step);
    pitch = mix(pitch, targetPitch, step);
    tone = mix(tone, targetTone, step);
}

float VocalEngine::coefficient(double seconds) const {
    return static_cast<float>(-std::expm1(-1.0 / (rate * seconds)));
}

void VocalEngine::prepare(double sampleRate, int inputChannels, int outputChannels, const Settings& settings) {
    rate = sampleRate;
    inChannels = inputChannels;
    outChannels = outputChannels;
    // Use the same sample latency at both standard session rates.
    const auto window = rate == 44100.0 || rate == 48000.0 ? defaultLatency
        : static_cast<int>(std::lround(rate * defaultLatency / 48000.0));
    const auto hop = static_cast<int>(std::lround(rate * 0.010));
    lead.configure(inChannels, window, hop);
    for (auto& voice : voices)
        voice.stretch.configure(1, window, hop);
    latencySamples = lead.inputLatency() + lead.outputLatency();
    maxDoubleDelay = static_cast<int>(std::ceil(rate * 0.035));
    for (auto& voice : voices)
        voice.delay.prepare(maxDoubleDelay + 2);
    for (auto* delays : {&dryDelay, &levelDelay})
        for (auto& delay : *delays)
            delay.prepare(latencySamples + 2);
    articulationDelay.prepare(latencySamples + maxDoubleDelay + 2);

    parameterStep = coefficient(0.018);
    rmsStep = coefficient(0.043);
    gainDownStep = coefficient(0.100);
    gainUpStep = coefficient(0.300);
    activityStep = coefficient(0.045);
    meterDecay = 1.0f - coefficient(0.280);
    energyStep = coefficient(0.110);
    matchDownStep = coefficient(0.050);
    matchUpStep = coefficient(0.220);
    lowPassStep = coefficient(1.0 / (2.0 * 3.141592653589793 * 2600.0));
    edgeStep = coefficient(0.010);
    bodyStep = coefficient(0.045);
    articulationStep = coefficient(0.009);
    reset(settings);
}

void VocalEngine::reset(const Settings& settings) {
    lead.reset();
    lead.setFormantBase(static_cast<float>(150.0 / rate));
    for (auto& voice : voices) {
        voice.stretch.reset();
        voice.stretch.setFormantBase(static_cast<float>(150.0 / rate));
        voice.delay.clear();
        voice.random = voice.initialSeed;
        voice.time = voice.pitch = voice.tone = voice.remaining = 0;
    }
    for (auto* delays : {&dryDelay, &levelDelay})
        for (auto& delay : *delays)
            delay.clear();
    articulationDelay.clear();
    rms = activity = gainDb = leadEnergy = mixEnergy = 0;
    lowPass = edgeEnergy = bodyEnergy = 0;
    gain = match = articulation = 1;
    active = false;
    untilUpdate = 0;
    lastMeters = {};
    targets = settings;
    controls = {settings.levelOn ? settings.level : 0.0f,
                settings.doubleOn ? settings.doubles : 0.0f,
                settings.human, settings.width, settings.formant,
                settings.formantOn ? 1.0f : 0.0f, settings.bypass ? 1.0f : 0.0f};
}

void VocalEngine::updateControls(const Settings& settings, bool hostBypass) {
    targets = settings;
    targets.bypass = targets.bypass || hostBypass;
}

float VocalEngine::leveler(float power) {
    rms = mix(rms, power, rmsStep);
    constexpr float gateOpen = 0.0000631f;   // -42 dBFS RMS
    constexpr float gateClose = 0.0000100f;  // -50 dBFS RMS
    active = rms >= (active ? gateClose : gateOpen);
    activity = mix(activity, active ? 1.0f : 0.0f, activityStep);
    const float rmsDb = powerToDb(rms);
    const float correction = std::clamp(-20.0f - rmsDb, -10.0f, 8.0f);
    // Taper positive gain near the activity threshold to avoid pulling up room tone.
    const float boostGate = std::clamp((rmsDb + 43.0f) / 13.0f, 0.0f, 1.0f);
    const float targetDb = correction * activity * (correction > 0.0f ? boostGate : 1.0f);
    gainDb = mix(gainDb, targetDb, targetDb < gainDb ? gainDownStep : gainUpStep);
    gain = dbToGain(gainDb * controls.level);
    return gain;
}

float VocalEngine::consonantWeight(float sample) {
    lowPass = mix(lowPass, sample, lowPassStep);
    const float high = sample - lowPass;
    edgeEnergy = mix(edgeEnergy, high * high, edgeStep);
    bodyEnergy = mix(bodyEnergy, sample * sample, bodyStep);
    const float ratio = edgeEnergy / (bodyEnergy + 1.0e-6f);
    const float body = std::clamp((ratio - 0.13f) * 2.5f, 0.0f, 1.0f);
    articulation = mix(articulation, 1.0f - 0.73f * body, articulationStep);
    return articulation;
}

void VocalEngine::process(const float* const* input, float* const* output, int count,
                          const Settings& settings, bool hostBypass) {
    updateControls(settings, hostBypass);
    for (int offset = 0; offset < count;) {
        if (untilUpdate == 0) {
            const float seconds = static_cast<float>(quantum / rate);
            lead.setFormantSemitones(controls.formant);
            for (size_t index = 0; index < voices.size(); ++index) {
                auto& voice = voices[index];
                const float side = index == 0 ? -1.0f : 1.0f;
                voice.advance(seconds, controls.human);
                const float pitch = side * (0.045f + 0.080f * controls.human)
                                    + voice.pitch * (0.035f + 0.21f * controls.human);
                const float color = side * (0.30f + 0.55f * controls.human)
                                    + voice.tone * 0.36f * controls.human;
                voice.stretch.setTransposeSemitones(pitch);
                voice.stretch.setFormantSemitones(controls.formantMix * (controls.formant + color), true);
            }
            untilUpdate = quantum;
        }
        const int chunk = std::min(untilUpdate, count - offset);
        processChunk(input, output, offset, chunk);
        untilUpdate -= chunk;
        offset += chunk;
    }
}

void VocalEngine::processChunk(const float* const* input, float* const* output, int offset, int count) {
    // Capture the whole chunk first because a host may use the same buffer for input and output.
    std::array<Buffer, 2> alignedDry{}, alignedLevel{};
    for (int i = 0; i < count; ++i) {
        controls.level = mix(controls.level, targets.levelOn ? targets.level : 0.0f, parameterStep);
        const float left = sampleAt(input[0], offset + i);
        const float right = inChannels == 2 ? sampleAt(input[1], offset + i) : left;
        const float multiplier = leveler(0.5f * (left * left + right * right));
        for (int channel = 0; channel < inChannels; ++channel) {
            const float sample = channel == 0 ? left : right;
            leadInput[channel][i] = sample * multiplier;
            dryDelay[channel].push(sample);
            levelDelay[channel].push(leadInput[channel][i]);
            alignedDry[channel][i] = dryDelay[channel].read(static_cast<float>(latencySamples));
            alignedLevel[channel][i] = levelDelay[channel].read(static_cast<float>(latencySamples));
        }
        const float mid = 0.5f * (left + right);
        monoInput[i] = mid * multiplier;
        articulationInput[i] = consonantWeight(mid);
        lastMeters.input = std::max(lastMeters.input * meterDecay,
                                   std::max(std::abs(left), std::abs(right)));
    }

    const float* leadIn[] = {leadInput[0].data(), leadInput[1].data()};
    float* leadOut[] = {leadRendered[0].data(), leadRendered[1].data()};
    lead.process(leadIn, count, leadOut, count);
    const float* voiceIn[] = {monoInput.data()};
    for (auto& voice : voices) {
        float* voiceOut[] = {voice.rendered.data()};
        voice.stretch.process(voiceIn, count, voiceOut, count);
    }

    for (int i = 0; i < count; ++i) {
        controls.doubles = mix(controls.doubles, targets.doubleOn ? targets.doubles : 0.0f, parameterStep);
        controls.human = mix(controls.human, targets.human, parameterStep);
        controls.width = mix(controls.width, targets.width, parameterStep);
        controls.formant = mix(controls.formant, targets.formant, parameterStep);
        controls.formantMix = mix(controls.formantMix, targets.formantOn ? 1.0f : 0.0f, parameterStep);
        controls.bypass = mix(controls.bypass, targets.bypass ? 1.0f : 0.0f, parameterStep);
        articulationDelay.push(articulationInput[i]);

        const float formantBlend = controls.formantMix * std::min(1.0f, std::abs(controls.formant) * 5.0f);
        const int rightChannel = inChannels == 2 ? 1 : 0;
        const float leadLeft = mix(alignedLevel[0][i], leadRendered[0][i], formantBlend);
        const float leadRight = mix(alignedLevel[rightChannel][i], leadRendered[rightChannel][i], formantBlend);
        std::array<float, 2> doubleSamples{};
        for (size_t index = 0; index < voices.size(); ++index) {
            auto& voice = voices[index];
            const float offsetMs = index == 0 ? 13.0f : 24.0f;
            const float delaySamples = static_cast<float>(rate * 0.001) *
                (offsetMs + voice.time * (1.0f + 6.0f * controls.human));
            voice.delay.push(voice.rendered[i]);
            const float consonants = articulationDelay.read(static_cast<float>(latencySamples) + delaySamples);
            doubleSamples[index] = voice.delay.read(delaySamples) * consonants;
        }

        // Linear pan preserves the level of each double in the mono fold-down.
        const float near = 0.5f + 0.5f * controls.width;
        const float far = 1.0f - near;
        const float amount = controls.doubles * 0.88f;
        const float left = leadLeft + amount * (near * doubleSamples[0] + far * doubleSamples[1]);
        const float right = leadRight + amount * (far * doubleSamples[0] + near * doubleSamples[1]);

        // Measure the summed signal so correlated voices are included in the compensation.
        const float dryMid = 0.5f * (leadLeft + leadRight);
        const float wetMid = 0.5f * (left + right);
        const float leadPower = outChannels == 1 ? dryMid * dryMid : 0.5f * (leadLeft * leadLeft + leadRight * leadRight);
        const float wetPower = outChannels == 1 ? wetMid * wetMid : 0.5f * (left * left + right * right);
        leadEnergy = mix(leadEnergy, leadPower, energyStep);
        mixEnergy = mix(mixEnergy, wetPower, energyStep);
        constexpr float energyFloor = 1.0e-7f;
        const float targetMatch = std::clamp(std::sqrt((leadEnergy + energyFloor) /
                                                       (mixEnergy + energyFloor)), 0.64f, 1.10f);
        match = mix(match, targetMatch, targetMatch < match ? matchDownStep : matchUpStep);
        const float compensation = mix(1.0f, match, std::min(1.0f, controls.doubles * 8.0f));
        const float finalLeft = mix(left * compensation, alignedDry[0][i], controls.bypass);
        const float finalRight = mix(right * compensation, alignedDry[rightChannel][i], controls.bypass);
        if (outChannels == 1) {
            const float result = 0.5f * (finalLeft + finalRight);
            output[0][offset + i] = result;
            lastMeters.output = std::max(lastMeters.output * meterDecay, std::abs(result));
        } else {
            output[0][offset + i] = finalLeft;
            output[1][offset + i] = finalRight;
            lastMeters.output = std::max(lastMeters.output * meterDecay,
                                        std::max(std::abs(finalLeft), std::abs(finalRight)));
        }
    }
    lastMeters.correctionDb = gainDb * controls.level * (1.0f - controls.bypass);
}

} // namespace vocalstack
