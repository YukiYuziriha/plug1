#include "VocalEngine.h"
#include "Vst3SpatialEqAutomation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vocalstack {

void Vst3SpatialEqAutomation::add(Target target, void* queue, ReadPoint read, int count,
                                  int samples, double previous, Point last) noexcept {
    const auto normalized = [](double v) { return std::isfinite(v) && v >= 0. && v <= 1.; };
    if (count <= 0 || count > samples || last.sample < 0 || last.sample >= samples
        || !normalized(last.value) || !normalized(previous)) return;

    // One point per sample is the maximum valid ordered queue. Check it once
    // before installing a lane so a malformed curve stays on the legacy path.
    Point first = last;
    int offset = -1;
    for (int i = 0; i < count - 1; ++i) {
        Point point{};
        if (!read(queue, i, point) || point.sample <= offset || point.sample >= last.sample
            || !normalized(point.value)) return;
        if (i == 0) first = point;
        offset = point.sample;
    }
    auto* const end = lanes.data() + size;
    auto* lane = std::find_if(lanes.data(), end, [&](const Lane& active) {
        return active.target.slot == target.slot && active.target.band == target.band
            && active.target.depth == target.depth;
    });
    if (lane == end) {
        if (size == lanes.size()) return;
        ++size;
    }
    *lane = {target, queue, read, count, 0, {-1, previous}, first, last, 0.};
    lane->updateSlope();
}

void Vst3SpatialEqAutomation::Lane::updateSlope() noexcept {
    slope = (to.value - from.value) / double(to.sample - from.sample);
}

float Vst3SpatialEqAutomation::Lane::valueAt(int sample) noexcept {
    while (sample >= to.sample && index + 1 < count) {
        from = to;
        ++index;
        if (index == count - 1) to = last;
        else if (!read(queue, index, to)) {
            // A conforming host keeps the validated queue stable for process().
            index = count - 1;
            to = last;
        }
        updateSlope();
    }
    const float normalized = float(sample >= to.sample ? to.value : from.value + slope * (sample - from.sample));
    return target.minimum + normalized * (target.maximum - target.minimum);
}

void Vst3SpatialEqAutomation::apply(int sample, std::array<FxSettings, 16>& fx) noexcept {
    for (size_t i = 0; i < size; ++i) {
        auto& lane = lanes[i];
        const auto value = lane.valueAt(sample);
        auto& band = fx[lane.target.slot].spatialEq.bands[lane.target.band];
        if (lane.target.depth) band.depth = value * .01f;
        else band.gainDb = value;
    }
}

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
    if (s.sidechainSettings.enabled) {
        auto& sidechain = s.sidechainSettings;
        sidechain = bounded(sidechain);
        // Stopping the clock should fade out motion, not hold a random offset.
        if (sidechain.rateHz == 0.0f) sidechain.depth = 0.0f;
    }
    return s;
}

constexpr float spectralEpsilon = 0.00001f;

bool localSpectralRequested(const VoiceSettings& v, float human) {
    return v.pitchSemitones != 0.0f || v.detuneCents != 0.0f || v.formantSemitones != 0.0f
        || (v.depth > 0.0f && v.rateHz > 0.0f && human > 0.0f);
}

bool leadSpectralRequested(const Settings& settings) {
    return settings.leadSettings.enabled && ((settings.formantOn && settings.formant != 0.0f)
        || localSpectralRequested(settings.leadSettings, settings.human));
}

float localSpectralBlend(float formant, float fine, float pitch) {
    return std::min(1.0f, 5.0f * (std::abs(formant) + std::abs(fine) + std::abs(pitch)));
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
    const float fraction = delay - static_cast<float>(whole);
    if (fraction == 0.0f) return data[static_cast<size_t>(newer)];
    const auto older = newer == 0 ? length - 1 : newer - 1;
    return mix(data[static_cast<size_t>(newer)], data[static_cast<size_t>(older)], fraction);
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

void VocalEngine::prepare(double sampleRate, int inputChannels, int outputChannels, const Settings& settings,
                          int routingLatencyReserve, bool needsVocalLatency, std::uint8_t independentVoices) {
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
    defaultRoutedVoices = routedVoices = 0xffu | (unsigned(independentVoices) << 8);
    latencySamples = 0;
    // 100 ms user delay plus up to 14 ms of Human motion.
    maxDoubleDelay = static_cast<int>(std::ceil(rate * 0.115));
    if (needsVocalLatency) {
        leadSpectral.stretch.configure(inChannels, window, hop);
        latencySamples = leadSpectral.stretch.inputLatency() + leadSpectral.stretch.outputLatency();
        sidechainSpectral.stretch.configure(2, window, hop);
        for (auto& voice : voices) {
            voice.stretch.configure(1, window, hop);
            voice.delay.prepare(maxDoubleDelay + 2);
            voice.harmony.prepare(rate, latencySamples);
        }
        for (auto* spectral : {&leadSpectral, &sidechainSpectral})
            for (auto& harmony : spectral->harmony) harmony.prepare(rate, latencySamples);
        articulationDelay.prepare(latencySamples + maxDoubleDelay + 2);
    }
    for (auto* delays : {&dryDelay, &levelDelay, &auxiliaryDelay})
        for (auto& delay : *delays)
            delay.prepare(latencySamples + 2);
    for (auto* delays : {&leadLocalDelay, &sidechainLocalDelay})
        for (auto& delay : *delays) delay.prepare(maxDoubleDelay + 2);

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
    if (latencySamples != 0) {
        leadSpectral.stretch.reset();
        leadSpectral.stretch.setFormantBase(static_cast<float>(150.0 / rate));
    }
    sidechainRunning = false; // Start fresh when a donor next reaches the Blend.
    leadMotion.controls = settings.leadSettings;
    leadMotion.amplitude = settings.leadSettings.enabled ? dbToGain(settings.leadSettings.gainDb) : 0;
    leadMotion.delaySamples = float(rate * 0.001) * settings.leadSettings.delayMs;
    leadMotion.random = leadMotion.initialSeed;
    leadMotion.time = leadMotion.pitch = leadMotion.tone = leadMotion.remaining = 0;
    leadMotion.targetTime = leadMotion.targetPitch = leadMotion.targetTone = 0;
    leadMotion.appliedPitch = leadMotion.appliedDetune = leadMotion.appliedFormant = 0;
    leadMotion.measuredEnergy = 0;
    leadLocalBlend = 0;
    leadSpectral.running = latencySamples != 0 && leadSpectralRequested(settings);
    leadSpectral.wakeBlend = leadSpectral.running ? 1.0f : 0.0f;
    leadSpectral.wakeRemaining = 0;
    leadSpectral.harmonyRunning = leadSpectral.running && settings.leadSettings.pitchSemitones != 0;
    leadSpectral.pitchBlend = leadSpectral.harmonyRunning ? 1.0f : 0.0f;
    leadSpectral.harmonyWarmup = 0;
    globalMix = settings.globalMix;
    for (size_t channel = 0; channel < 2; ++channel) {
        if (latencySamples != 0) leadSpectral.harmony[channel].reset();
        leadLocalDelay[channel].clear();
    }
    const auto initialVoices = settings.advanced ? settings.voiceSettings : basicVoiceSettings(settings.human);
    const bool mainMixActive = settings.doubleOn && settings.doubles > 0.0f;
    articulationRunning = false;
    for (size_t index = 0; index < voices.size(); ++index) {
        auto& voice = voices[index];
        voice.controls = bounded(initialVoices[index]);
        voice.running = voiceRequested(index, voice.controls, mainMixActive);
        articulationRunning |= voice.running;
        voice.presence = voice.running ? 1.0f : 0.0f;
        voice.harmonyRunning = voice.running && voice.controls.pitchSemitones != 0;
        voice.harmonyBlend = voice.harmonyRunning ? 1.0f : 0.0f;
        voice.harmonyWarmup = voice.wakeRemaining = 0;
        voice.measuredEnergy = 0;
        voice.appliedPitch = voice.appliedDetune = voice.appliedFormant = 0;
        voice.amplitude = voice.presence * dbToGain(voice.controls.gainDb);
        voice.delaySamples = static_cast<float>(rate * 0.001) * voice.controls.delayMs;
        if (latencySamples != 0) {
            voice.stretch.reset();
            voice.stretch.setFormantBase(static_cast<float>(150.0 / rate));
            voice.harmony.reset();
            voice.delay.clear();
        }
        voice.levelMatch = {};
        voice.random = voice.initialSeed;
        voice.time = voice.pitch = voice.tone = voice.remaining = 0;
    }
    for (auto* delays : {&dryDelay, &levelDelay, &auxiliaryDelay, &rackDryDelay})
        for (auto& delay : *delays)
            delay.clear();
    if (latencySamples != 0) articulationDelay.clear();
    routing.reset();
    snapshots = {};
    leadState = {true, settings.leadSettings.enabled, leadMotion.controls.pan, 0, 0,
        leadMotion.controls.delayMs, 0, leadMotion.controls.gainDb, 0};
    resetLeveler();
    leadSpectral.match = {};
    lowPass = edgeEnergy = bodyEnergy = 0;
    articulation = 1;
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

bool VocalEngine::voiceRequested(size_t index, const VoiceSettings& voice, bool mainMixActive) const noexcept {
    return latencySamples != 0 && voice.present && voice.enabled
        && (routedVoices & ((mainMixActive ? 0x101u : 0x100u) << index)) != 0;
}

void VocalEngine::resetLeveler() noexcept {
    rms = activity = gainDb = normalizationDb = 0;
    longInputEnergy = longOutputEnergy = longWeight = 0;
    gain = 1;
    active = levelerRunning = false;
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

void VocalEngine::advanceLocalVoice(Voice& voice, float seconds) {
    const auto& v = voice.controls;
    voice.advance(seconds * v.rateHz, controls.human);
    voice.finePitch = v.detuneCents * 0.01f + voice.pitch * 0.245f * controls.human * v.depth;
    voice.appliedPitch = v.pitchSemitones;
    voice.appliedDetune = voice.finePitch * 100.0f;
    voice.appliedFormant = v.formantSemitones + voice.tone * 0.36f * controls.human * v.depth;
}

void VocalEngine::smoothLocalVoice(VoiceSettings& v, const VoiceSettings& target, bool snap) const {
    for (auto member : {&VoiceSettings::gainDb, &VoiceSettings::pan, &VoiceSettings::delayMs,
                        &VoiceSettings::detuneCents, &VoiceSettings::formantSemitones,
                        &VoiceSettings::depth, &VoiceSettings::rateHz, &VoiceSettings::pitchSemitones}) {
        v.*member = mix(v.*member, target.*member, parameterStep);
        if (snap && std::abs(v.*member - target.*member) < spectralEpsilon) v.*member = target.*member;
    }
}

void VocalEngine::renderStereoSpectral(StereoSpectral& state, const Voice& voice, bool wanted,
                                       bool independentFormant, bool update, int channels, int count) {
    if (!wanted) {
        state.running = state.harmonyRunning = false;
        state.pitchBlend = 0;
        return;
    }
    const bool waking = !state.running;
    if (waking) {
        state.stretch.reset();
        state.stretch.setFormantBase(static_cast<float>(150.0 / rate));
        state.match = {};
        state.wakeRemaining = latencySamples;
        state.wakeBlend = 0;
    }
    state.running = true;
    if (waking || update) {
        state.stretch.setTransposeSemitones(voice.finePitch);
        state.stretch.setFormantSemitones(voice.appliedFormant, independentFormant);
    }
    const float* in[] = {state.input[0].data(), state.input[1].data()};
    float* out[] = {state.rendered[0].data(), state.rendered[1].data()};
    state.stretch.process(in, count, out, count);

    const bool musical = std::abs(voice.appliedPitch) > spectralEpsilon;
    if (musical && !state.harmonyRunning) {
        for (auto& harmony : state.harmony) harmony.reset();
        state.harmonyRunning = true;
        state.harmonyWarmup = latencySamples;
    }
    if (state.harmonyRunning) {
        // Reset clears ratios as well as history. Restore controls before processing.
        for (auto& harmony : state.harmony)
            harmony.setSemitones(voice.appliedPitch + voice.appliedDetune * 0.01f, voice.appliedFormant);
        const float target = musical ? 1.0f : 0.0f;
        for (int i = 0; i < count; ++i) {
            if (state.harmonyWarmup) --state.harmonyWarmup;
            else state.pitchBlend = mix(state.pitchBlend, target, parameterStep);
            if (std::abs(state.pitchBlend - target) < spectralEpsilon) state.pitchBlend = target;
            for (int channel = 0; channel < channels; ++channel)
                state.rendered[channel][i] = mix(state.rendered[channel][i],
                    state.harmony[channel].process(state.input[channel][i]), state.pitchBlend);
        }
        if (!musical && state.pitchBlend == 0) state.harmonyRunning = false;
    }
}

StereoSample VocalEngine::blendStereoSpectral(StereoSpectral& state, StereoSample reference,
                                              int sample, int rightChannel, float& amount) {
    if (!state.running) {
        amount = 0;
        return reference;
    }
    // Keep aligned clean audio audible until the spectral history is primed.
    if (state.wakeRemaining > 0) --state.wakeRemaining;
    else {
        state.wakeBlend = mix(state.wakeBlend, 1.0f, parameterStep);
        if (1.0f - state.wakeBlend < spectralEpsilon) state.wakeBlend = 1.0f;
    }
    amount *= state.wakeBlend;
    const auto left = state.rendered[0][sample], right = state.rendered[rightChannel][sample];
    const float spectralGain = matchedGain(state.match,
        0.5f * (reference.left * reference.left + reference.right * reference.right),
        0.5f * (left * left + right * right));
    if (amount <= 0.0f) return reference;
    return {mix(reference.left, left * spectralGain, amount), mix(reference.right, right * spectralGain, amount)};
}

StereoSample VocalEngine::applyLocalVoice(Voice& voice, std::array<DelayLine, 2>& delay,
                                          StereoSample sample, float timeStep) {
    const auto& v = voice.controls;
    voice.time = mix(voice.time, voice.targetTime, std::min(1.0f, timeStep * v.rateHz));
    const float delayTarget = std::clamp(float(rate * 0.001) *
        (v.delayMs + voice.time * 7.0f * controls.human * v.depth), 0.0f, float(maxDoubleDelay));
    voice.delaySamples += std::clamp(delayTarget - voice.delaySamples, -0.125f, 0.125f);
    delay[0].push(sample.left);
    delay[1].push(sample.right);
    return {
        delay[0].read(voice.delaySamples) * voice.amplitude * std::min(1.0f, 1.0f - v.pan),
        delay[1].read(voice.delaySamples) * voice.amplitude * std::min(1.0f, 1.0f + v.pan)};
}

void VocalEngine::process(const float* const* input, float* const* output, int count,
                          const Settings& settings, bool hostBypass,
                          const float* const* auxiliary, int auxiliaryChannels,
                          Vst3SpatialEqAutomation* spatialAutomation, int hostOffset) {
    updateControls(settings, hostBypass);
    if (!auxiliary || auxiliaryChannels <= 0 || !auxiliary[0] || (auxiliaryChannels > 1 && !auxiliary[1]))
        auxiliaryChannels = 0;
    for (int offset = 0; offset < count;) {
        if (untilUpdate == 0) {
            const float seconds = static_cast<float>(quantum / rate);
            advanceLocalVoice(leadMotion, seconds);
            const float fine = leadMotion.finePitch, localFormant = leadMotion.appliedFormant;
            const float pitch = leadMotion.appliedPitch;
            const bool local = std::abs(localFormant) > spectralEpsilon || std::abs(pitch + fine) > spectralEpsilon;
            leadIndependentFormant = local;
            leadMotion.appliedFormant = (local ? controls.formant * controls.formantMix : controls.formant) + localFormant;
            leadLocalBlend = localSpectralBlend(localFormant, fine, pitch);
            for (size_t index = 0; latencySamples != 0 && index < voices.size(); ++index) {
                auto& voice = voices[index];
                const auto& v = voice.controls;
                voice.advance(seconds * v.rateHz, controls.human);
                const float finePitch = v.detuneCents * 0.01f
                    + voice.pitch * 0.245f * controls.human * v.depth;
                const float detune = finePitch * 100.0f;
                const float voiceColor = v.formantSemitones
                    + voice.tone * 0.36f * controls.human * v.depth;
                // Preserve the original doubler at musical zero. Its predictive
                // peak mapping is not accurate enough for three-octave harmonies.
                const float independent = controls.formantMix + controls.advanced * (1.0f - controls.formantMix);
                voice.finePitch = finePitch;
                voice.appliedPitch = v.pitchSemitones;
                voice.appliedDetune = detune;
                voice.appliedFormant = controls.formantMix * controls.formant + independent * voiceColor;
            }
            untilUpdate = quantum;
        }
        const int chunk = std::min(untilUpdate, count - offset);
        processChunk(input, output, offset, chunk, auxiliary, auxiliaryChannels, spatialAutomation, hostOffset);
        untilUpdate -= chunk;
        offset += chunk;
    }
}

void VocalEngine::processChunk(const float* const* input, float* const* output, int offset, int count,
                               const float* const* auxiliary, int auxiliaryChannels,
                               Vst3SpatialEqAutomation* spatialAutomation, int hostOffset) {
    routedVoices = routing.beginSpan(defaultRoutedVoices);
    const bool updateSpectral = untilUpdate == quantum;
    const auto& sidechainTarget = targets.sidechainSettings;
    const bool renderSidechain = sidechainTarget.enabled && auxiliaryChannels > 0
        && routing.usesSidechainVoice(targets.sidechainBlendAutomation, targets.sidechainBlend);
    const bool sidechainWaking = renderSidechain && !sidechainRunning;
    sidechainRunning = renderSidechain;
    if (sidechainWaking) {
        sidechainMotion.controls = sidechainTarget;
        sidechainMotion.amplitude = dbToGain(sidechainTarget.gainDb);
        sidechainMotion.delaySamples = float(rate * 0.001) * sidechainTarget.delayMs;
        sidechainMotion.random = sidechainMotion.initialSeed;
        sidechainMotion.time = sidechainMotion.pitch = sidechainMotion.tone = sidechainMotion.remaining = 0;
        sidechainMotion.targetTime = sidechainMotion.targetPitch = sidechainMotion.targetTone = 0;
        sidechainSpectral.running = sidechainSpectral.harmonyRunning = false;
        sidechainSpectral.pitchBlend = 0;
        for (auto& delay : sidechainLocalDelay) delay.clear();
    }
    if (renderSidechain && (sidechainWaking || updateSpectral)) {
        advanceLocalVoice(sidechainMotion, static_cast<float>(quantum / rate));
        sidechainLocalBlend = localSpectralBlend(sidechainMotion.appliedFormant,
            sidechainMotion.finePitch, sidechainMotion.appliedPitch);
    }
    const bool sidechainWanted = renderSidechain && latencySamples != 0
        && (localSpectralRequested(sidechainTarget, targets.human) || sidechainLocalBlend > spectralEpsilon);
    const float mixTarget = targets.doubleOn ? targets.doubles : 0.0f;
    const bool mainMixActive = mixTarget > 0.0f || controls.doubles > 0.0f;
    const auto chunkTargets = targets.advanced ? targets.voiceSettings : basicVoiceSettings(controls.human);
    bool renderVoices = false;
    for (size_t index = 0; index < voices.size(); ++index) {
        auto& voice = voices[index];
        const auto& target = chunkTargets[index];
        const bool wanted = voiceRequested(index, target, mainMixActive);
        // Mute and removal keep their own fades. An enabled slot gated solely
        // by main Mix has already faded out on that pin when we get here.
        const bool fading = (!target.present || !target.enabled) && voice.running
            && (voice.amplitude != 0.0f || voice.presence != 0.0f);
        if (!wanted && !fading) {
            voice.running = false;
            voice.amplitude = voice.presence = voice.measuredEnergy = 0;
            continue;
        }
        renderVoices = true;
        const bool waking = !voice.running;
        if (waking) {
            // Reset preallocated histories so sleeping time cannot replay old audio.
            voice.stretch.reset();
            voice.stretch.setFormantBase(static_cast<float>(150.0 / rate));
            voice.delay.clear();
            voice.levelMatch = {};
            voice.harmonyRunning = false;
            voice.harmonyBlend = 0;
            voice.wakeRemaining = latencySamples + maxDoubleDelay;
            voice.running = true;
        }
        if (waking || updateSpectral) {
            voice.stretch.setTransposeSemitones(voice.finePitch);
            voice.stretch.setFormantSemitones(voice.appliedFormant, true);
        }
    }
    if (renderVoices && !articulationRunning) {
        articulationDelay.clear();
        lowPass = edgeEnergy = bodyEnergy = 0;
        articulation = 1;
    }
    articulationRunning = renderVoices;

    // Capture the whole chunk first because a host may use the same buffer for input and output.
    std::array<Buffer, 2> alignedDry{}, alignedLevel{}, alignedAuxiliary{};
    const float levelTarget = targets.levelOn ? targets.level : 0.0f;
    for (int i = 0; i < count; ++i) {
        controls.level = mix(controls.level, levelTarget, parameterStep);
        if (std::abs(controls.level - levelTarget) < 0.00001f)
            controls.level = levelTarget;
        const float left = sampleAt(input[0], offset + i);
        const float right = inChannels == 2 ? sampleAt(input[1], offset + i) : left;
        float multiplier = 1.0f;
        if (controls.level != 0.0f) {
            levelerRunning = true;
            multiplier = leveler(0.5f * (left * left + right * right));
        } else if (levelerRunning) {
            resetLeveler();
        }
        for (int channel = 0; channel < inChannels; ++channel) {
            const float sample = channel == 0 ? left : right;
            inputMeters[channel].process(sample);
            leadSpectral.input[channel][i] = sample * multiplier;
            if (latencySamples != 0) {
                dryDelay[channel].push(sample);
                levelDelay[channel].push(leadSpectral.input[channel][i]);
                alignedDry[channel][i] = dryDelay[channel].read(static_cast<float>(latencySamples));
                alignedLevel[channel][i] = levelDelay[channel].read(static_cast<float>(latencySamples));
            } else {
                alignedDry[channel][i] = sample;
                alignedLevel[channel][i] = leadSpectral.input[channel][i];
            }
        }
        for (int channel = 0; channel < 2; ++channel) {
            const float sample = auxiliary && auxiliaryChannels > 0
                ? sampleAt(auxiliary[auxiliaryChannels == 1 ? 0 : channel], offset + i) : 0.f;
            if (sidechainWanted) sidechainSpectral.input[channel][i] = sample;
            alignedAuxiliary[channel][i] = sample;
            if (latencySamples != 0) {
                auxiliaryDelay[channel].push(sample);
                alignedAuxiliary[channel][i] = auxiliaryDelay[channel].read(float(latencySamples));
            }
        }
        if (renderVoices) {
            const float mid = 0.5f * (left + right);
            monoInput[i] = mid * multiplier;
            articulationInput[i] = consonantWeight(mid);
        }
    }

    const bool leadWanted = latencySamples != 0 && (leadSpectralRequested(targets)
        || (leadMotion.amplitude > 0.0f && (leadLocalBlend > spectralEpsilon
            || controls.formantMix * std::min(1.0f, std::abs(controls.formant) * 5.0f) > spectralEpsilon)));
    renderStereoSpectral(leadSpectral, leadMotion, leadWanted, leadIndependentFormant,
                         updateSpectral, inChannels, count);
    if (renderSidechain)
        renderStereoSpectral(sidechainSpectral, sidechainMotion, sidechainWanted, true,
                             updateSpectral, 2, count);
    const float* voiceIn[] = {monoInput.data()};
    for (auto& voice : voices) {
        if (!voice.running) continue;
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
    routingControls.sidechainBlend = targets.sidechainBlend;
    routingControls.sidechainBlendAutomation = targets.sidechainBlendAutomation;
    for (int i = 0; i < count; ++i) {
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
        if (renderVoices) articulationDelay.push(articulationInput[i]);

        auto& lv = leadMotion.controls;
        const auto lt = targets.leadSettings;
        smoothLocalVoice(lv, lt);
        const float leadAmplitudeTarget = lt.enabled ? dbToGain(lv.gainDb) : 0;
        leadMotion.amplitude = mix(leadMotion.amplitude, leadAmplitudeTarget, parameterStep);
        if (std::abs(leadMotion.amplitude - leadAmplitudeTarget) < 0.000001f) leadMotion.amplitude = leadAmplitudeTarget;
        const int rightChannel = inChannels == 2 ? 1 : 0;
        const float referenceLeft = alignedLevel[0][i];
        const float referenceRight = alignedLevel[rightChannel][i];
        float formantBlend = std::max(leadLocalBlend,
            controls.formantMix * std::min(1.0f, std::abs(controls.formant) * 5.0f));
        const auto leadSample = blendStereoSpectral(leadSpectral, {referenceLeft, referenceRight},
                                                    i, rightChannel, formantBlend);
        const float referenceMono = 0.5f * (referenceLeft + referenceRight);
        const float timeStep = (6.0f + 5.0f * controls.human) / static_cast<float>(rate);
        const auto voiceTargets = targets.advanced ? targets.voiceSettings : basicVoiceSettings(controls.human);
        RoutingSources sources;
        sources.sidechainAvailable = auxiliary && auxiliaryChannels > 0;
        sources.sidechain = {alignedAuxiliary[0][i], alignedAuxiliary[1][i]};
        sources.lead = applyLocalVoice(leadMotion, leadLocalDelay, leadSample, timeStep);
        if (renderSidechain) {
            auto& sv = sidechainMotion.controls;
            smoothLocalVoice(sv, sidechainTarget, true);
            const float amplitude = dbToGain(sv.gainDb);
            sidechainMotion.amplitude = mix(sidechainMotion.amplitude, amplitude, parameterStep);
            if (std::abs(sidechainMotion.amplitude - amplitude) < 0.000001f)
                sidechainMotion.amplitude = amplitude;
            float spectralBlend = sidechainLocalBlend;
            const auto donor = blendStereoSpectral(sidechainSpectral, sources.sidechain, i, 1, spectralBlend);
            sources.sidechainVoice = applyLocalVoice(sidechainMotion, sidechainLocalDelay, donor, timeStep);
            // Keep the delay history ready for automation, but route the raw tap at unity.
            sources.sidechainVoiceAvailable = spectralBlend > 0.0f || sidechainMotion.delaySamples != 0.0f
                || sidechainMotion.amplitude != 1.0f || sv.pan != 0.0f;
        }
        // Meter the actual stereo lead source after its delay, envelope and pan,
        // before graph routing (like generated voices, independent of final Mix).
        const float leadPower = 0.5f * (sources.lead.left * sources.lead.left
            + sources.lead.right * sources.lead.right);
        leadMotion.measuredEnergy = mix(leadMotion.measuredEnergy, leadPower, activityStep);
        leadState = {true, lt.enabled, lv.pan,
            formantBlend > 0 && leadSpectral.pitchBlend > 0 ? leadMotion.appliedPitch : 0,
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
            v.present = target.present; v.enabled = target.enabled;
            // Slew the actual read head by at most 1/8 sample per sample. Even
            // extreme delay/mode automation cannot jump the read position.
            voice.time = mix(voice.time, voice.targetTime,
                             std::min(1.0f, timeStep * v.rateHz));
            const float requestedDelay = std::clamp(static_cast<float>(rate * 0.001) *
                (v.delayMs + voice.time * 7.0f * controls.human * v.depth),
                0.0f, static_cast<float>(maxDoubleDelay));
            voice.delaySamples += std::clamp(requestedDelay - voice.delaySamples, -0.125f, 0.125f);
            const float delaySamples = voice.delaySamples;
            const float pan = v.pan * controls.width;
            const float delayMs = delaySamples * static_cast<float>(1000.0 / rate);
            if (!voice.running) {
                snapshots[index] = {target.present, target.enabled, pan, voice.appliedPitch,
                    voice.appliedDetune, delayMs, voice.appliedFormant, v.gainDb, 0};
                continue;
            }
            // Hold the envelope shut until both the transform and the current
            // fractional read position contain post-wake samples. The read
            // head can only move by 1/8 sample per sample, so readiness holds.
            const bool primed = float(voice.wakeRemaining) <= float(maxDoubleDelay) - delaySamples;
            if (voice.wakeRemaining > 0) --voice.wakeRemaining;
            const bool sounding = target.present && target.enabled && primed;
            voice.presence = mix(voice.presence, sounding ? 1.0f : 0.0f, parameterStep);
            if (!sounding && voice.presence < 0.000001f) voice.presence = 0;
            const float amplitudeTarget = sounding ? dbToGain(v.gainDb) : 0.0f;
            voice.amplitude = mix(voice.amplitude, amplitudeTarget, parameterStep);
            if (std::abs(voice.amplitude - amplitudeTarget) < 0.000001f)
                voice.amplitude = amplitudeTarget;
            const float voiceGain = matchedGain(voice.levelMatch, referenceMono * referenceMono,
                                                voice.rendered[i] * voice.rendered[i]);
            voice.delay.push(voice.rendered[i] * voiceGain);
            const float consonants = articulationDelay.read(static_cast<float>(latencySamples) + delaySamples);
            const float sample = voice.delay.read(delaySamples) * consonants * voice.amplitude;
            // Linear pan preserves each voice's contribution in mono. The
            // fixed scale gives two hard-panned decorrelated voices unity
            // stereo power; user voice gains are never normalized away.
            sources.voices[index] = {sample * (1.0f - pan) * 0.5f, sample * (1.0f + pan) * 0.5f};
            sources.activity[index] = voice.presence;
            voice.measuredEnergy = mix(voice.measuredEnergy, sample * sample, activityStep);
            snapshots[index] = {target.present, target.enabled, pan, voice.appliedPitch,
                voice.appliedDetune, delayMs,
                voice.appliedFormant, v.gainDb, std::sqrt(std::max(0.0f, voice.measuredEnergy))};
        }

        routingControls.mix = controls.doubles;
        if (spatialAutomation != nullptr)
            spatialAutomation->apply(hostOffset + offset + i, routingControls.fxSettings);
        const auto routed = routing.process(sources, routingControls, false);
        std::array<float, 2> finalDry{alignedDry[0][i], alignedDry[rightChannel][i]};
        if (routingReserve != 0) {
            for (int c = 0; c < 2; ++c) {
                rackDryDelay[c].push(finalDry[c]);
                finalDry[c] = rackDryDelay[c].read(float(routingReserve));
            }
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
    correctionDb = levelerRunning ? 20.0f * std::log10(gain) * (1.0f - controls.bypass) : 0.0f;
}

} // namespace vocalstack
