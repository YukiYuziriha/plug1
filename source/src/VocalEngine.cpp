#include "VocalEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vocalstack {
namespace {
float mix(float a, float b, float amount) { return a + (b - a) * amount; }
float powerToDb(float power) { return 10.0f * std::log10(std::max(power, 1.0e-12f)); }
float dbToGain(float db) { return std::exp(db * 0.115129255f); }

VoiceSettings bounded(VoiceSettings v) {
    const auto limit = [](float value, float low, float high, float fallback) {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    v.gainDb = limit(v.gainDb, -60, 12, 0);
    v.pan = limit(v.pan, -1, 1, 0);
    v.delayMs = limit(v.delayMs, 0, 100, 20);
    v.detuneCents = limit(v.detuneCents, -100, 100, 0);
    v.formantSemitones = limit(v.formantSemitones, -24, 24, 0);
    v.pitchSemitones = limit(v.pitchSemitones, -36, 36, 0);
    v.depth = limit(v.depth, 0, 2, 1);
    v.rateHz = limit(v.rateHz, 0, 8, 1);
    return v;
}

Settings boundedSettings(Settings s) {
    const auto unit = [](float x, float fallback) {
        return std::isfinite(x) ? std::clamp(x, 0.0f, 1.0f) : fallback;
    };
    s.level = unit(s.level, 0.5f); s.doubles = unit(s.doubles, 0.35f);
    s.human = unit(s.human, 0.35f); s.width = unit(s.width, 0.65f);
    s.formant = std::isfinite(s.formant) ? std::clamp(s.formant, -24.0f, 24.0f) : 0;
    s.outputGainDb = std::isfinite(s.outputGainDb) ? s.outputGainDb : 0;
    s.globalMix = unit(s.globalMix, 1);
    s.leadSettings = bounded(s.leadSettings);
    return s;
}

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
        remaining = 0.65f + uniform() * 0.70f;
        targetTime = 2.0f * uniform() - 1.0f;
        targetPitch = 2.0f * uniform() - 1.0f;
        targetTone = 2.0f * uniform() - 1.0f;
    }
    const float step = std::min(1.0f, seconds * (6.0f + human * 5.0f));
    pitch = mix(pitch, targetPitch, step);
    tone = mix(tone, targetTone, step);
}

float VocalEngine::coefficient(double seconds) const {
    return static_cast<float>(-std::expm1(-1.0 / (rate * seconds)));
}

void VocalEngine::prepare(double sampleRate, int inputChannels, int outputChannels, const Settings& settings, int routingLatencyReserve) {
    if (!std::isfinite(sampleRate) || sampleRate <= 0 || sampleRate > std::numeric_limits<int>::max()
        || inputChannels < 1 || inputChannels > 2 || outputChannels < 1 || outputChannels > 2)
        throw std::invalid_argument("invalid engine sample rate or channel layout");
    // Use the same sample latency at both standard session rates.
    const auto window = sampleRate == 44100.0 || sampleRate == 48000.0 ? defaultLatency
        : static_cast<int>(std::lround(sampleRate * defaultLatency / 48000.0));
    if (window < 127) throw std::invalid_argument("sample rate is too low for the harmony window");
    rate = sampleRate;
    inChannels = inputChannels;
    outChannels = outputChannels;
    const auto hop = static_cast<int>(std::lround(rate * 0.010));
    lead.configure(inChannels, window, hop);
    for (auto& voice : voices)
        voice.stretch.configure(1, window, hop);
    latencySamples = lead.inputLatency() + lead.outputLatency();
    // 100 ms user delay plus up to 14 ms of Human motion.
    maxDoubleDelay = static_cast<int>(std::ceil(rate * 0.115));
    for (auto& voice : voices) {
        voice.delay.prepare(maxDoubleDelay + 2);
        voice.harmony.prepare(rate, latencySamples);
    }
    for (auto* delays : {&dryDelay, &levelDelay, &auxiliaryDelay})
        for (auto& delay : *delays)
            delay.prepare(latencySamples + 2);
    for (size_t channel = 0; channel < 2; ++channel) {
        leadHarmony[channel].prepare(rate, latencySamples);
        leadLocalDelay[channel].prepare(maxDoubleDelay + 2);
    }
    articulationDelay.prepare(latencySamples + maxDoubleDelay + 2);

    parameterStep = coefficient(0.018);
    rmsStep = coefficient(0.043);
    gainDownStep = coefficient(0.100);
    gainUpStep = coefficient(0.300);
    activityStep = coefficient(0.045);
    for (auto* detectors : {&inputMeters, &outputMeters})
        for (auto& detector : *detectors)
            detector.prepare(rate);
    energyStep = coefficient(0.110);
    matchDownStep = coefficient(0.050);
    matchUpStep = coefficient(0.220);
    lowPassStep = coefficient(1.0 / (2.0 * 3.141592653589793 * 2600.0));
    edgeStep = coefficient(0.010);
    bodyStep = coefficient(0.045);
    articulationStep = coefficient(0.009);
    longStep = coefficient(3.0);
    routingReserve = std::clamp(routingLatencyReserve, 0, SpectralMorph::latencySamples);
    for (auto& delay : rackDryDelay) delay.prepare(routingReserve + 2);
    routing.prepare(rate, routingReserve);
    reset(settings);
}

void VocalEngine::reset(const Settings& requested) {
    const auto settings = boundedSettings(requested);
    lead.reset();
    lead.setFormantBase(static_cast<float>(150.0 / rate));
    leadMotion.controls = settings.leadSettings;
    leadMotion.amplitude = settings.leadSettings.enabled ? dbToGain(settings.leadSettings.gainDb) : 0;
    leadMotion.delaySamples = float(rate * 0.001) * settings.leadSettings.delayMs;
    leadMotion.random = leadMotion.initialSeed;
    leadMotion.time = leadMotion.pitch = leadMotion.tone = leadMotion.remaining = 0;
    leadMotion.targetTime = leadMotion.targetPitch = leadMotion.targetTone = 0;
    leadMotion.appliedPitch = leadMotion.appliedDetune = leadMotion.appliedFormant = 0;
    leadMotion.measuredEnergy = 0;
    leadLocalBlend = 0;
    leadHarmonyRunning = settings.leadSettings.pitchSemitones != 0;
    leadPitchBlend = leadHarmonyRunning ? 1.0f : 0.0f;
    leadHarmonyWarmup = 0;
    globalMix = settings.globalMix;
    for (size_t channel = 0; channel < 2; ++channel) {
        leadHarmony[channel].reset();
        leadLocalDelay[channel].clear();
    }
    const auto initialVoices = settings.advanced ? settings.voiceSettings : basicVoiceSettings(settings.human);
    for (size_t index = 0; index < voices.size(); ++index) {
        auto& voice = voices[index];
        voice.controls = bounded(initialVoices[index]);
        voice.presence = voice.controls.present && voice.controls.enabled ? 1.0f : 0.0f;
        voice.running = voice.controls.present;
        voice.harmony.reset();
        voice.harmonyRunning = voice.controls.pitchSemitones != 0;
        voice.harmonyBlend = voice.harmonyRunning ? 1.0f : 0.0f;
        voice.harmonyWarmup = 0;
        voice.measuredEnergy = 0;
        voice.appliedPitch = voice.appliedDetune = voice.appliedFormant = 0;
        voice.amplitude = voice.presence * dbToGain(voice.controls.gainDb);
        voice.delaySamples = static_cast<float>(rate * 0.001) * voice.controls.delayMs;
        voice.stretch.reset();
        voice.stretch.setFormantBase(static_cast<float>(150.0 / rate));
        voice.delay.clear();
        voice.levelMatch = {};
        voice.random = voice.initialSeed;
        voice.time = voice.pitch = voice.tone = voice.remaining = 0;
    }
    for (auto* delays : {&dryDelay, &levelDelay, &auxiliaryDelay, &rackDryDelay})
        for (auto& delay : *delays)
            delay.clear();
    articulationDelay.clear();
    routing.reset();
    snapshots = {};
    leadState = {true, settings.leadSettings.enabled, leadMotion.controls.pan, 0, 0,
        leadMotion.controls.delayMs, 0, leadMotion.controls.gainDb, 0};
    longInputEnergy = longOutputEnergy = longWeight = 0;
    normalizationDb = 0;
    leadMatch = {};
    rms = activity = gainDb = 0;
    lowPass = edgeEnergy = bodyEnergy = 0;
    gain = articulation = 1;
    active = false;
    untilUpdate = 0;
    for (auto* detectors : {&inputMeters, &outputMeters})
        for (auto& detector : *detectors)
            detector.reset();
    correctionDb = 0;
    targets = settings;
    controls = {settings.levelOn ? settings.level : 0.0f,
                settings.doubleOn ? settings.doubles : 0.0f,
                settings.human, settings.width, settings.formant,
                settings.formantOn ? 1.0f : 0.0f, settings.bypass ? 1.0f : 0.0f,
                settings.advanced ? 1.0f : 0.0f, settings.outputGainDb};
}

RoutingGrainSnapshot VocalEngine::grainSnapshot() const noexcept {
    return routing.grainSnapshot();
}

Meters VocalEngine::meters() const noexcept {
    Meters result;
    result.inputChannels = inChannels;
    result.outputChannels = outChannels;
    result.correctionDb = correctionDb;
    for (int channel = 0; channel < inChannels; ++channel)
        result.input[channel] = inputMeters[channel].reading();
    for (int channel = 0; channel < outChannels; ++channel)
        result.output[channel] = outputMeters[channel].reading();
    return result;
}

void VocalEngine::resetMeters() noexcept {
    for (auto* detectors : {&inputMeters, &outputMeters})
        for (auto& detector : *detectors)
            detector.resetPeaks();
}

void VocalEngine::updateControls(const Settings& settings, bool hostBypass) {
    targets = boundedSettings(settings);
    targets.bypass = targets.bypass || hostBypass;
}

float VocalEngine::leveler(float power) {
    rms = mix(rms, power, rmsStep);
    constexpr float gateOpen = 0.0000631f;   // -42 dBFS RMS
    constexpr float gateClose = 0.0000100f;  // -50 dBFS RMS
    active = rms >= (active ? gateClose : gateOpen);
    activity = mix(activity, active ? 1.0f : 0.0f, activityStep);
    longInputEnergy += longStep * (power - longInputEnergy);
    longWeight += longStep * (1.0 - longWeight);
    const float rmsDb = powerToDb(rms);
    const float referenceDb = powerToDb(static_cast<float>(longInputEnergy / std::max(longWeight, 1.0e-12)));
    const float correction = std::clamp(0.75f * (referenceDb - rmsDb), -10.0f, 8.0f);
    // Taper positive gain near the activity threshold to avoid pulling up room tone.
    const float boostGate = std::clamp((rmsDb + 43.0f) / 13.0f, 0.0f, 1.0f);
    const float targetDb = correction * activity * (correction > 0.0f ? boostGate : 1.0f);
    gainDb = mix(gainDb, targetDb, targetDb < gainDb ? gainDownStep : gainUpStep);
    const float ridingGain = dbToGain(gainDb * controls.level);
    longOutputEnergy += longStep * (power * ridingGain * ridingGain - longOutputEnergy);
    // Normalize the ridden signal against the same long-term input window, not
    // a fixed dBFS target. Bounds/gating deliberately take priority over exact
    // RMS equality, and no peak or perceptual-loudness guarantee is implied.
    const float targetNormalization = longInputEnergy > 1.0e-9 && longOutputEnergy > 1.0e-9
        ? std::clamp(powerToDb(static_cast<float>(longInputEnergy / longOutputEnergy)), -6.0f, 6.0f) : 0.0f;
    // Store normalization per unit of riding strength, so the entire gain
    // correction fades together when Leveler is automated off. Its long energy
    // history must never leave a residual gain that is dropped at zero.
    const float perUnitNormalization = std::clamp(targetNormalization /
        std::max(controls.level, 0.05f), -12.0f, 12.0f);
    normalizationDb = mix(normalizationDb, perUnitNormalization, gainUpStep);
    gain = dbToGain(std::clamp(controls.level * (gainDb + normalizationDb * activity), -10.0f, 8.0f));
    // Zero strength must remain bit-exact unity, including after automation.
    if (controls.level == 0.0f)
        gain = 1.0f;
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

float VocalEngine::matchedGain(LevelMatch& state, float referencePower, float renderedPower) {
    state.referenceEnergy = mix(state.referenceEnergy, referencePower, energyStep);
    state.renderedEnergy = mix(state.renderedEnergy, renderedPower, energyStep);
    constexpr float silenceFloor = 1.0e-9f;
    float targetGain = state.gain;
    if (state.referenceEnergy > silenceFloor && state.renderedEnergy > silenceFloor) {
        targetGain = std::clamp(std::sqrt(state.referenceEnergy / state.renderedEnergy), 0.25f, 4.0f);
    } else if (state.referenceEnergy <= silenceFloor && state.renderedEnergy <= silenceFloor) {
        targetGain = 1.0f;
    }
    // Hold through unmatched attacks/tails; never boost a missing or silent render.
    state.gain = mix(state.gain, targetGain, targetGain < state.gain ? matchDownStep : matchUpStep);
    return state.gain;
}

void VocalEngine::process(const float* const* input, float* const* output, int count,
                          const Settings& settings, bool hostBypass,
                          const float* const* auxiliary, int auxiliaryChannels) {
    updateControls(settings, hostBypass);
    for (int offset = 0; offset < count;) {
        if (untilUpdate == 0) {
            const float seconds = static_cast<float>(quantum / rate);
            auto& lv = leadMotion.controls;
            leadMotion.advance(seconds * lv.rateHz, controls.human);
            const float fine = lv.detuneCents * 0.01f + leadMotion.pitch * 0.245f * controls.human * lv.depth;
            const float localFormant = lv.formantSemitones + leadMotion.tone * 0.36f * controls.human * lv.depth;
            const bool local = std::abs(localFormant) > 0.00001f || std::abs(lv.pitchSemitones + fine) > 0.00001f;
            const float color = (local ? controls.formant * controls.formantMix : controls.formant) + localFormant;
            leadMotion.appliedPitch = lv.pitchSemitones;
            leadMotion.appliedDetune = fine * 100.0f;
            leadMotion.appliedFormant = color;
            leadLocalBlend = std::min(1.0f, 5.0f * (std::abs(localFormant) + std::abs(fine) + std::abs(lv.pitchSemitones)));
            lead.setTransposeSemitones(fine);
            lead.setFormantSemitones(color, local);
            for (auto& harmony : leadHarmony) harmony.setSemitones(lv.pitchSemitones + fine, color);
            for (size_t index = 0; index < voices.size(); ++index) {
                auto& voice = voices[index];
                const auto& v = voice.controls;
                voice.advance(seconds * v.rateHz, controls.human);
                const float finePitch = v.detuneCents * 0.01f
                    + voice.pitch * 0.245f * controls.human * v.depth;
                const float detune = finePitch * 100.0f;
                const float pitch = v.pitchSemitones + finePitch;
                const float color = v.formantSemitones
                    + voice.tone * 0.36f * controls.human * v.depth;
                // Preserve the original doubler at musical zero. Its predictive
                // peak mapping is not accurate enough for three-octave harmonies.
                voice.stretch.setTransposeSemitones(finePitch);
                const float independent = controls.formantMix + controls.advanced * (1.0f - controls.formantMix);
                voice.appliedPitch = v.pitchSemitones;
                voice.appliedDetune = detune;
                voice.appliedFormant = controls.formantMix * controls.formant + independent * color;
                voice.stretch.setFormantSemitones(voice.appliedFormant, true);
                voice.harmony.setSemitones(pitch, voice.appliedFormant);
            }
            untilUpdate = quantum;
        }
        const int chunk = std::min(untilUpdate, count - offset);
        processChunk(input, output, offset, chunk, auxiliary, auxiliaryChannels);
        untilUpdate -= chunk;
        offset += chunk;
    }
}

void VocalEngine::processChunk(const float* const* input, float* const* output, int offset, int count,
                               const float* const* auxiliary, int auxiliaryChannels) {
    // Capture the whole chunk first because a host may use the same buffer for input and output.
    std::array<Buffer, 2> alignedDry{}, alignedLevel{}, alignedAuxiliary{};
    for (int i = 0; i < count; ++i) {
        const float levelTarget = targets.levelOn ? targets.level : 0.0f;
        controls.level = mix(controls.level, levelTarget, parameterStep);
        if (std::abs(controls.level - levelTarget) < 0.00001f)
            controls.level = levelTarget;
        const float left = sampleAt(input[0], offset + i);
        const float right = inChannels == 2 ? sampleAt(input[1], offset + i) : left;
        const float multiplier = leveler(0.5f * (left * left + right * right));
        for (int channel = 0; channel < inChannels; ++channel) {
            const float sample = channel == 0 ? left : right;
            inputMeters[channel].process(sample);
            leadInput[channel][i] = sample * multiplier;
            dryDelay[channel].push(sample);
            levelDelay[channel].push(leadInput[channel][i]);
            alignedDry[channel][i] = dryDelay[channel].read(static_cast<float>(latencySamples));
            alignedLevel[channel][i] = levelDelay[channel].read(static_cast<float>(latencySamples));
        }
        for (int channel = 0; channel < 2; ++channel) {
            const float sample = auxiliary && auxiliaryChannels > 0
                ? sampleAt(auxiliary[auxiliaryChannels == 1 ? 0 : channel], offset + i) : 0.f;
            auxiliaryDelay[channel].push(sample);
            alignedAuxiliary[channel][i] = auxiliaryDelay[channel].read(float(latencySamples));
        }
        const float mid = 0.5f * (left + right);
        monoInput[i] = mid * multiplier;
        articulationInput[i] = consonantWeight(mid);
    }

    const float* leadIn[] = {leadInput[0].data(), leadInput[1].data()};
    float* leadOut[] = {leadRendered[0].data(), leadRendered[1].data()};
    lead.process(leadIn, count, leadOut, count);
    const bool leadMusical = std::abs(leadMotion.appliedPitch) > 0.00001f;
    if (leadMusical && !leadHarmonyRunning) {
        for (auto& harmony : leadHarmony) harmony.reset();
        leadHarmonyRunning = true;
        leadHarmonyWarmup = latencySamples;
    }
    if (leadHarmonyRunning) {
        // reset() clears ratios as well as history; restore the cached controls
        // immediately, not up to one control quantum later.
        for (auto& harmony : leadHarmony)
            harmony.setSemitones(leadMotion.appliedPitch + leadMotion.appliedDetune * 0.01f,
                                  leadMotion.appliedFormant);
        for (int i = 0; i < count; ++i) {
            if (leadHarmonyWarmup) --leadHarmonyWarmup;
            else leadPitchBlend = mix(leadPitchBlend, leadMusical ? 1.0f : 0.0f, parameterStep);
            if (std::abs(leadPitchBlend - (leadMusical ? 1.0f : 0.0f)) < 0.00001f)
                leadPitchBlend = leadMusical ? 1.0f : 0.0f;
            for (int channel = 0; channel < inChannels; ++channel)
                leadRendered[channel][i] = mix(leadRendered[channel][i], leadHarmony[size_t(channel)].process(leadInput[channel][i]), leadPitchBlend);
        }
        if (!leadMusical && leadPitchBlend == 0) leadHarmonyRunning = false;
    }
    const float* voiceIn[] = {monoInput.data()};
    const auto chunkTargets = targets.advanced ? targets.voiceSettings : basicVoiceSettings(controls.human);
    for (size_t index = 0; index < voices.size(); ++index) {
        auto& voice = voices[index];
        if (!chunkTargets[index].present && voice.amplitude == 0 && voice.presence < 0.000001f) {
            voice.running = false;
            voice.rendered.fill(0);
            continue;
        }
        if (!voice.running) {
            // reset() clears preallocated storage only; never reconfigure on audio.
            voice.stretch.reset();
            voice.stretch.setFormantBase(static_cast<float>(150.0 / rate));
            voice.delay.clear();
            voice.levelMatch = {};
            voice.harmony.reset();
            voice.harmonyRunning = false;
            voice.harmonyBlend = 0;
            voice.running = true;
        }
        float* voiceOut[] = {voice.rendered.data()};
        voice.stretch.process(voiceIn, count, voiceOut, count);
        const bool musical = std::abs(voice.controls.pitchSemitones) > 0.00001f;
        if (musical && !voice.harmonyRunning) {
            voice.harmony.reset();
            voice.harmonyRunning = true;
            voice.harmonyWarmup = latencySamples;
        }
        if (voice.harmonyRunning) {
            voice.harmony.setSemitones(voice.appliedPitch + voice.appliedDetune * 0.01f,
                                      voice.appliedFormant);
            for (int i = 0; i < count; ++i) {
                const float shifted = voice.harmony.process(monoInput[i]);
                if (voice.harmonyWarmup) --voice.harmonyWarmup;
                else voice.harmonyBlend = mix(voice.harmonyBlend, musical ? 1.0f : 0.0f, parameterStep);
                if (std::abs(voice.harmonyBlend - (musical ? 1.0f : 0.0f)) < 0.00001f)
                    voice.harmonyBlend = musical ? 1.0f : 0.0f;
                voice.rendered[i] = mix(voice.rendered[i], shifted, voice.harmonyBlend);
            }
            if (!musical && voice.harmonyBlend == 0) voice.harmonyRunning = false;
        }
    }

    RoutingControls routingControls;
    routingControls.choraleOn = targets.choraleOn;
    routingControls.choraleMix = targets.choraleMix;
    routingControls.choraleMotion = targets.choraleMotion;
    // Forward tone/creative/utility settings as fixed value data; no graph
    // rebuild or module reset is needed for live FX automation.
    routingControls.fxSettings = targets.fxSettings;
    routingControls.fxAutomation = targets.fxAutomation;
    routingControls.monitorWetEnabled = !targets.bypass;
    routingControls.localMix = targets.localMix;
    routingControls.localMixAutomation = targets.localMixAutomation;
    routingControls.tempoBpm = targets.tempoBpm;
    for (int i = 0; i < count; ++i) {
        const float mixTarget = targets.doubleOn ? std::clamp(targets.doubles, 0.0f, 1.0f) : 0.0f;
        controls.doubles = mix(controls.doubles, mixTarget, parameterStep);
        if (std::abs(controls.doubles - mixTarget) < 0.00001f)
            controls.doubles = mixTarget;
        controls.advanced = mix(controls.advanced, targets.advanced ? 1.0f : 0.0f, parameterStep);
        controls.human = mix(controls.human, targets.human, parameterStep);
        controls.width = mix(controls.width, targets.width, parameterStep);
        controls.formant = mix(controls.formant, targets.formant, parameterStep);
        controls.formantMix = mix(controls.formantMix, targets.formantOn ? 1.0f : 0.0f, parameterStep);
        const float bypassTarget = targets.bypass ? 1.0f : 0.0f;
        controls.bypass = mix(controls.bypass, bypassTarget, parameterStep);
        // Float one-poles otherwise stop just short of an exact bypass endpoint.
        if (std::abs(controls.bypass - bypassTarget) < 0.0001f)
            controls.bypass = bypassTarget;
        controls.outputGainDb += (targets.outputGainDb - controls.outputGainDb) * parameterStep;
        const float outputGain = dbToGain(static_cast<float>(controls.outputGainDb));
        articulationDelay.push(articulationInput[i]);

        auto& lv = leadMotion.controls;
        const auto lt = targets.leadSettings;
        for (auto member : {&VoiceSettings::gainDb, &VoiceSettings::pan, &VoiceSettings::delayMs,
                            &VoiceSettings::detuneCents, &VoiceSettings::formantSemitones,
                            &VoiceSettings::depth, &VoiceSettings::rateHz, &VoiceSettings::pitchSemitones})
            lv.*member = mix(lv.*member, lt.*member, parameterStep);
        const float leadAmplitudeTarget = lt.enabled ? dbToGain(lv.gainDb) : 0;
        leadMotion.amplitude = mix(leadMotion.amplitude, leadAmplitudeTarget, parameterStep);
        if (std::abs(leadMotion.amplitude - leadAmplitudeTarget) < 0.000001f) leadMotion.amplitude = leadAmplitudeTarget;
        // Only actual local shifts select the spectral path. Depth with a
        // stopped motion clock must not leak a disabled shared Formant effect.
        const float formantBlend = std::max(controls.formantMix * std::min(1.0f, std::abs(controls.formant) * 5.0f),
            leadLocalBlend);
        const int rightChannel = inChannels == 2 ? 1 : 0;
        const float referenceLeft = alignedLevel[0][i];
        const float referenceRight = alignedLevel[rightChannel][i];
        const float referencePower = 0.5f * (referenceLeft * referenceLeft + referenceRight * referenceRight);
        const float renderedLeft = leadRendered[0][i];
        const float renderedRight = leadRendered[rightChannel][i];
        const float renderedPower = 0.5f * (renderedLeft * renderedLeft + renderedRight * renderedRight);
        const float leadGain = matchedGain(leadMatch, referencePower, renderedPower);
        const float leadLeft = mix(referenceLeft, renderedLeft * leadGain, formantBlend);
        const float leadRight = mix(referenceRight, renderedRight * leadGain, formantBlend);
        const float referenceMono = 0.5f * (referenceLeft + referenceRight);
        const float timeStep = (6.0f + 5.0f * controls.human) / static_cast<float>(rate);
        const auto voiceTargets = targets.advanced ? targets.voiceSettings : basicVoiceSettings(controls.human);
        RoutingSources sources;
        sources.sidechainAvailable = auxiliary && auxiliaryChannels > 0;
        sources.sidechain = {alignedAuxiliary[0][i], alignedAuxiliary[1][i]};
        const float timeStepLead = (6.0f + 5.0f * controls.human) / float(rate);
        leadMotion.time = mix(leadMotion.time, leadMotion.targetTime, std::min(1.0f, timeStepLead * lv.rateHz));
        const float leadDelayTarget = std::clamp(float(rate * 0.001) *
            (lv.delayMs + leadMotion.time * 7.0f * controls.human * lv.depth), 0.0f, float(maxDoubleDelay));
        leadMotion.delaySamples += std::clamp(leadDelayTarget - leadMotion.delaySamples, -0.125f, 0.125f);
        leadLocalDelay[0].push(leadLeft);
        leadLocalDelay[1].push(leadRight);
        sources.lead = {
            leadLocalDelay[0].read(leadMotion.delaySamples) * leadMotion.amplitude * std::min(1.0f, 1.0f - lv.pan),
            leadLocalDelay[1].read(leadMotion.delaySamples) * leadMotion.amplitude * std::min(1.0f, 1.0f + lv.pan)};
        // Meter the actual stereo lead source after its delay, envelope and pan,
        // before graph routing (like generated voices, independent of final Mix).
        const float leadPower = 0.5f * (sources.lead.left * sources.lead.left
            + sources.lead.right * sources.lead.right);
        leadMotion.measuredEnergy = mix(leadMotion.measuredEnergy, leadPower, activityStep);
        leadState = {true, lt.enabled, lv.pan,
            formantBlend > 0 && leadPitchBlend > 0 ? leadMotion.appliedPitch : 0,
            formantBlend > 0 ? leadMotion.appliedDetune : 0,
            leadMotion.delaySamples * static_cast<float>(1000.0 / rate),
            formantBlend > 0 ? leadMotion.appliedFormant : 0, lv.gainDb,
            std::sqrt(std::max(0.0f, leadMotion.measuredEnergy))};
        for (size_t index = 0; index < voices.size(); ++index) {
            auto& voice = voices[index];
            const auto target = bounded(voiceTargets[index]);
            auto& v = voice.controls;
            for (auto member : {&VoiceSettings::gainDb, &VoiceSettings::pan,
                                &VoiceSettings::delayMs, &VoiceSettings::detuneCents,
                                &VoiceSettings::formantSemitones, &VoiceSettings::depth,
                                &VoiceSettings::rateHz, &VoiceSettings::pitchSemitones})
                v.*member = mix(v.*member, target.*member, parameterStep);
            const bool sounding = target.present && target.enabled;
            v.present = target.present; v.enabled = target.enabled;
            voice.presence = mix(voice.presence, sounding ? 1.0f : 0.0f, parameterStep);
            const float amplitudeTarget = sounding ? dbToGain(v.gainDb) : 0.0f;
            voice.amplitude = mix(voice.amplitude, amplitudeTarget, parameterStep);
            if (std::abs(voice.amplitude - amplitudeTarget) < 0.000001f)
                voice.amplitude = amplitudeTarget;
            // Slew the actual read head by at most 1/8 sample per sample. Even
            // extreme delay/mode automation cannot jump the read position.
            voice.time = mix(voice.time, voice.targetTime,
                             std::min(1.0f, timeStep * v.rateHz));
            const float requestedDelay = std::clamp(static_cast<float>(rate * 0.001) *
                (v.delayMs + voice.time * 7.0f * controls.human * v.depth),
                0.0f, static_cast<float>(maxDoubleDelay));
            voice.delaySamples += std::clamp(requestedDelay - voice.delaySamples, -0.125f, 0.125f);
            const float delaySamples = voice.delaySamples;
            const float voiceGain = matchedGain(voice.levelMatch, referenceMono * referenceMono,
                                                voice.rendered[i] * voice.rendered[i]);
            voice.delay.push(voice.rendered[i] * voiceGain);
            const float consonants = articulationDelay.read(static_cast<float>(latencySamples) + delaySamples);
            const float sample = voice.delay.read(delaySamples) * consonants * voice.amplitude;
            const float pan = v.pan * controls.width;
            // Linear pan preserves each voice's contribution in mono. The
            // fixed scale gives two hard-panned decorrelated voices unity
            // stereo power; user voice gains are never normalized away.
            sources.voices[index] = {sample * (1.0f - pan) * 0.5f, sample * (1.0f + pan) * 0.5f};
            sources.activity[index] = voice.presence;
            voice.measuredEnergy = mix(voice.measuredEnergy, sample * sample, activityStep);
            snapshots[index] = {target.present, target.enabled, pan, voice.appliedPitch,
                voice.appliedDetune, delaySamples * static_cast<float>(1000.0 / rate),
                voice.appliedFormant, v.gainDb, std::sqrt(std::max(0.0f, voice.measuredEnergy))};
        }

        routingControls.mix = controls.doubles;
        const auto routed = routing.process(sources, routingControls);
        std::array<float, 2> finalDry{};
        for (int c = 0; c < 2; ++c) {
            rackDryDelay[c].push(alignedDry[c == 0 ? 0 : rightChannel][i]);
            finalDry[c] = rackDryDelay[c].read(float(routingReserve));
        }
        const float left = routed.left, right = routed.right;
        globalMix = mix(globalMix, targets.globalMix, parameterStep);
        if (std::abs(globalMix - targets.globalMix) < 0.00001f) globalMix = targets.globalMix;
        const float wetLeft = globalMix == 0 ? finalDry[0]
            : globalMix == 1 ? left * outputGain : mix(finalDry[0], left * outputGain, globalMix);
        const float wetRight = globalMix == 0 ? finalDry[1]
            : globalMix == 1 ? right * outputGain : mix(finalDry[1], right * outputGain, globalMix);
        // Whole-plugin bypass remains untouched, independent of saved global Mix.
        const float finalLeft = controls.bypass == 1.0f ? finalDry[0]
            : mix(wetLeft, finalDry[0], controls.bypass);
        const float finalRight = controls.bypass == 1.0f ? finalDry[1]
            : mix(wetRight, finalDry[1], controls.bypass);
        if (outChannels == 1) {
            const float result = 0.5f * (finalLeft + finalRight);
            output[0][offset + i] = result;
            outputMeters[0].process(result);
        } else {
            output[0][offset + i] = finalLeft;
            output[1][offset + i] = finalRight;
            outputMeters[0].process(finalLeft);
            outputMeters[1].process(finalRight);
        }
    }
    correctionDb = 20.0f * std::log10(gain) * (1.0f - controls.bypass);
}

} // namespace vocalstack
