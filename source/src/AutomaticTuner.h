#pragma once

/*
 * Detector adapted from MXTune pitch_detector_talent.cpp:
 * Copyright (C) 2021-2023 Liu An Lin <liuanlin-mx@qq.com>
 * Based on Autotalent 0.2 by Thomas A. Baran (March 20, 2010).
 * This adaptation is distributed under GPL-3.0-or-later, without warranty.
 * See ThirdParty/MXTune for original sources, full licenses and audit notes.
 * Changes: Signalsmith FFT instead of FFTW, bounded RAII storage, linked stereo,
 * finite/silence guards, quadratic peak interpolation and independent controls.
 */

#include "TuningSettings.h"
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <signalsmith-linear/fft.h>
#include <array>
#include <complex>
#include <stdexcept>
#include <vector>

namespace vocalstack {

struct TuningTelemetry {
    float detectedHz = 0;
    float detectedMidi = 0;             // A4=69 at settings.referenceHz
    float confidence = 0;
    float targetMidi = 0;               // Quantized destination, not the gliding target
    float correctionSemitones = 0;      // Current applied shifter command
    bool voiced = false;
    bool hasTarget = false;
};

// One wet-only tuner BEFORE voice fan-out, not one detector per harmony voice.
// prepare() allocates; reset/process/setters/telemetry do not. All access is
// single audio-owner; caller publishes telemetry to other threads separately.
// Input/output may alias channel-for-channel. Mono is duplicated internally;
// stereo uses one pitch decision and a single linked two-channel shifter.
class AutomaticTuner {
public:
    using Settings = TuningSettings;
    static constexpr int defaultLatency = 1344;
    AutomaticTuner() = default;
    AutomaticTuner(const AutomaticTuner&) = delete;
    AutomaticTuner& operator=(const AutomaticTuner&) = delete;
    AutomaticTuner(AutomaticTuner&&) = delete;
    AutomaticTuner& operator=(AutomaticTuner&&) = delete;

    void prepare(double sampleRate, int maxBlock) {
        if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 192000 || maxBlock < 1)
            throw std::invalid_argument("invalid automatic tuner sample rate/block size");
        prepared = false;
        rate = sampleRate;
        // Match VocalEngine's legacy sample latency, including at 44.1 kHz.
        const int window = sampleRate == 44100 || sampleRate == 48000 ? defaultLatency
            : int(std::lround(sampleRate * defaultLatency / 48000));
        // A quarter-window hop gives substantial pitch error at this short
        // window. High overlap trades CPU for hard-tuning accuracy, not latency.
        stretch.configure(2, window, std::max(1, window / 32), false);
        latencySamples = stretch.inputLatency() + stretch.outputLatency();
        detectorSize = 1;
        while (detectorSize < rate * 0.064) detectorSize *= 2;
        detectorHop = std::max(1, int(std::lround(rate * 0.005)));
        fft.resize(detectorSize);
        for (auto& channel : history) channel.assign(detectorSize, 0);
        time.resize(detectorSize);
        spectrum.resize(detectorSize);
        windowFunction.assign(detectorSize, 0);
        inverseWindow.resize(detectorSize);
        // Donor's single raised cosine supported on the middle half-window.
        for (int i = 0; i < detectorSize / 2; ++i)
            windowFunction[i + detectorSize / 4] = float(0.5 - 0.5 * std::cos(4 * pi * i / (detectorSize - 1)));
        for (int i = 0; i < detectorSize; ++i) time[i] = {windowFunction[i], 0};
        // Keep window DC: removing it biases the compensation slope and hence
        // fractional-lag estimates, especially at low sample rates.
        autocorrelate(false);
        const float energy = time[0].real();
        for (int i = 0; i < detectorSize; ++i) {
            const float correlation = time[i].real() / energy;
            inverseWindow[i] = correlation > 0.000001f ? 1 / correlation : 0;
        }
        // std::function construction is confined to prepare (even on a
        // standard library that doesn't small-buffer-optimize this capture).
        stretch.setFreqMap([this](float frequency) { return mapFrequency(frequency); });
        prepared = true;
        reset();
    }

    void reset() noexcept {
        for (auto& channel : history) std::fill(channel.begin(), channel.end(), 0.f);
        if (!prepared) { state = {}; return; }
        stretch.reset();
        mappingRatio = 1;
        mappingFundamental = 0;
        stretch.setFormantSemitones(0, true);
        stretch.setFormantBase(float(150 / rate));
        position = filled = detectorCountdown = controlCountdown = 0;
        targetNote = pendingNote = -1;
        pendingSamples = unvoicedSamples = 0;
        targetPitch = correction = 0;
        state = {};
        scratchIn = {};
        scratchOut = {};
    }

    int latency() const noexcept { return latencySamples; }

    // Observation support, NOT additional audio latency. Complete history is
    // required after reset; steady-state estimate is centered ~half a window ago.
    int detectorWindowSamples() const noexcept { return detectorSize; }
    int detectorHopSamples() const noexcept { return detectorHop; }
    TuningTelemetry telemetry() const noexcept { return state; }

    // maxBlock is a validated host contract, not a processing limit:
    // arbitrarily large/irregular blocks are internally bounded to 32 samples.
    // Null input channels are silence. Invalid layout/unprepared calls clear
    // valid outputs rather than allocating, throwing or touching missing inputs.
    void process(const float* const* input, float* const* output, int count,
                 int channels, const Settings& settings) noexcept {
        if (count <= 0 || !output) return;
        if (!prepared || channels < 1 || channels > 2) {
            for (int c = 0; c < std::clamp(channels, 0, 2); ++c)
                if (output[c]) std::fill_n(output[c], count, 0.f);
            return;
        }
        const Settings s = sanitiseTuningSettings(settings);
        int offset = 0;
        while (offset < count) {
            if (controlCountdown == 0) {
                updateCorrection(s);
                controlCountdown = quantum;
            }
            const int n = std::min({count - offset, controlCountdown, quantum});
            for (int i = 0; i < n; ++i) {
                for (int c = 0; c < 2; ++c) {
                    const float sample = input && input[channels == 1 ? 0 : c]
                        ? input[channels == 1 ? 0 : c][offset + i] : 0;
                    // Bound pathological host values before FFT energy squaring.
                    scratchIn[c][i] = std::isfinite(sample) ? std::clamp(sample, -32.f, 32.f) : 0;
                    history[c][position] = scratchIn[c][i];
                }
                position = (position + 1) % detectorSize;
                filled = std::min(filled + 1, detectorSize);
                if (--detectorCountdown <= 0) {
                    detectorCountdown = detectorHop;
                    if (filled == detectorSize) detect(s);
                }
            }
            const float* ins[] = {scratchIn[0].data(), scratchIn[1].data()};
            float* outs[] = {scratchOut[0].data(), scratchOut[1].data()};
            stretch.process(ins, n, outs, n);
            for (int c = 0; c < channels; ++c)
                if (output[c]) for (int i = 0; i < n; ++i) {
                    const float value = scratchOut[c][i];
                    output[c][offset + i] = std::isfinite(value) ? value : 0;
                }
            offset += n;
            controlCountdown -= n;
        }
    }

    void process(const float* input, float* output, int count, const Settings& settings) noexcept {
        const float* ins[] = {input};
        float* outs[] = {output};
        process(ins, outs, count, 1, settings);
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    static constexpr int quantum = 32;
    using Complex = std::complex<float>;

    void autocorrelate(bool removeDC = true) noexcept {
        fft.fft(time.data(), spectrum.data());
        if (removeDC) spectrum[0] = {};
        for (int i = 0; i < detectorSize; ++i)
            spectrum[i] = {std::norm(spectrum[i]), 0};
        fft.ifft(spectrum.data(), time.data());
    }

    void detect(const Settings& s) noexcept {
        // Stronger channel, rather than L+R: anti-phase vocals must not cancel.
        double powers[2]{};
        for (int c = 0; c < 2; ++c)
            for (float value : history[c]) powers[c] += double(value) * value;
        const int channel = powers[1] > powers[0] ? 1 : 0;
        state.confidence = 0;
        state.voiced = false;
        if (powers[channel] / detectorSize > 1.e-8) {
            for (int i = 0; i < detectorSize; ++i)
                time[i] = {history[channel][(position + i) % detectorSize] * windowFunction[i], 0};
            autocorrelate();
            const float energy = time[0].real();
            if (energy > 1.e-12f && std::isfinite(energy)) {
                float minHz = 55, maxHz = 1200;
                switch (s.vocalRange) {
                    case TuningVocalRange::Low: maxHz = 400; break;
                    case TuningVocalRange::Mid: minHz = 80; maxHz = 800; break;
                    case TuningVocalRange::High: minHz = 150; break;
                    default: break;
                }
                const int minLag = std::max(2, int(std::floor(rate / maxHz)));
                const int maxLag = std::min(detectorSize / 2 - 2, int(std::ceil(rate / minHz)));
                float peak = 0;
                int index = 0;
                for (int lag = minLag; lag <= maxLag; ++lag) {
                    const float value = time[lag].real();
                    if (value > time[lag - 1].real() && value >= time[lag + 1].real() && value > peak) {
                        peak = value;
                        index = lag;
                    }
                }
                if (index) {
                    // The biased lobe locates the fundamental, but its maximum
                    // can lie multiple samples before the unbiased maximum.
                    const int radius = int(std::ceil(index * 0.05)) + 2;
                    const int start = std::max(minLag, index - radius);
                    const int end = std::min(maxLag, index + radius);
                    float unbiasedPeak = 0;
                    for (int lag = start; lag <= end; ++lag) {
                        const float value = time[lag].real() * inverseWindow[lag];
                        if (value > unbiasedPeak) { unbiasedPeak = value; index = lag; }
                    }
                    peak = time[index].real();
                    state.confidence = std::clamp(unbiasedPeak / energy, 0.f, 1.f);
                    const float a = time[index - 1].real() * inverseWindow[index - 1];
                    const float b = peak * inverseWindow[index];
                    const float c = time[index + 1].real() * inverseWindow[index + 1];
                    const float denominator = a - 2 * b + c;
                    const float delta = denominator < -1.e-12f ? std::clamp(0.5f * (a - c) / denominator, -0.5f, 0.5f) : 0;
                    const float frequency = float(rate / (index + delta));
                    if (state.confidence >= 0.7f && frequency >= minHz && frequency <= maxHz) {
                        state.voiced = true;
                        state.detectedHz = frequency;
                        state.detectedMidi = 69 + 12 * std::log2(frequency / s.referenceHz);
                    }
                }
            }
        }
        if (!state.voiced) {
            state.detectedHz = state.detectedMidi = 0;
            state.hasTarget = false;
            pendingNote = -1;
            pendingSamples = 0;
            unvoicedSamples = std::min(unvoicedSamples + detectorHop, detectorSize * 2);
            if (unvoicedSamples >= rate * 0.03) targetNote = -1;
            return;
        }
        unvoicedSamples = 0;
        selectTarget(s);
    }

    void selectTarget(const Settings& s) noexcept {
        const auto mask = tuningNoteMask(s);
        if (!mask) {
            state.hasTarget = false;
            targetNote = pendingNote = -1;
            pendingSamples = 0;
            return;
        }
        int candidate = nearestTuningNote(state.detectedMidi, mask);
        const bool oldAllowed = targetNote >= 0 && (mask & (1u << (targetNote % 12)));
        if (oldAllowed && candidate != targetNote) {
            const float oldDistance = std::abs(state.detectedMidi - targetNote);
            const float newDistance = std::abs(state.detectedMidi - candidate);
            if (oldDistance - newDistance < 2 * s.stability * 0.4f) candidate = targetNote;
        }
        if (oldAllowed && candidate != targetNote) {
            if (candidate != pendingNote) { pendingNote = candidate; pendingSamples = 0; }
            pendingSamples += detectorHop;
            if (pendingSamples < rate * 0.03 * s.stability) candidate = targetNote;
        } else {
            pendingNote = -1;
            pendingSamples = 0;
        }
        if (targetNote < 0 || !oldAllowed) targetPitch = float(candidate);
        targetNote = candidate;
        state.hasTarget = true;
        state.targetMidi = float(targetNote);
    }

    float approach(float current, float destination, float milliseconds) const noexcept {
        if (milliseconds <= 0) return destination;
        return destination + (current - destination) * float(std::exp(-quantum / (rate * milliseconds * 0.001)));
    }

    void updateCorrection(const Settings& s) noexcept {
        // Revalidate immediately on automation, without waiting for the next FFT.
        if (state.voiced) selectTargetForAutomation(s);
        const bool active = state.voiced && state.hasTarget && tuningNoteMask(s);
        float desired = 0;
        if (active) {
            targetPitch = approach(targetPitch, float(targetNote), s.transitionMs);
            desired = std::clamp((targetPitch - state.detectedMidi) * s.amount, -12.f, 12.f);
        }
        // Unvoiced consonants release to unity over 5 ms, even in hard mode.
        correction = approach(correction, desired, active ? s.retuneMs : 5.f);
        if (std::abs(correction) < 1.e-6f) correction = 0;
        mappingRatio = std::exp2(correction / 12);
        mappingFundamental = state.voiced ? float(state.detectedHz / rate) : 0;
        stretch.setFormantSemitones(0, s.preserveFormants);
        stretch.setFormantBase(float((state.voiced ? state.detectedHz : 150) / rate));
        state.correctionSemitones = correction;
    }

    float mapFrequency(float frequency) const noexcept {
        if (mappingFundamental <= 0 || mappingRatio == 1) return frequency * mappingRatio;
        // Short-window spectral peak centroids are not exact frequencies.
        // Apply an additive shift around each known harmonic, not a scaled
        // centroid error. Linear ramps between plateaus keep this monotonic
        // for the bounded [0.5, 2] correction ratio.
        const float harmonic = frequency / mappingFundamental;
        const float base = std::floor(harmonic);
        const float ordinal = base + std::clamp((harmonic - base - .25f) * 2, 0.f, 1.f);
        return frequency + ordinal * mappingFundamental * (mappingRatio - 1);
    }

    void selectTargetForAutomation(const Settings& s) noexcept {
        const auto mask = tuningNoteMask(s);
        // Dwell is advanced only by detect(), never by host/control block size.
        if (!mask) { state.hasTarget = false; targetNote = -1; }
        else if (targetNote < 0 || !(mask & (1u << (targetNote % 12)))) selectTarget(s);
    }

    signalsmith::stretch::SignalsmithStretch<float> stretch{0x74554e45};
    signalsmith::linear::SimpleFFT<float> fft;
    std::array<std::vector<float>, 2> history;
    std::vector<float> windowFunction, inverseWindow;
    std::vector<Complex> time, spectrum;
    std::array<std::array<float, quantum>, 2> scratchIn{}, scratchOut{};
    double rate = 48000;
    int latencySamples = 0, detectorSize = 0, detectorHop = 1;
    int position = 0, filled = 0, detectorCountdown = 0, controlCountdown = 0;
    int targetNote = -1, pendingNote = -1, pendingSamples = 0, unvoicedSamples = 0;
    float targetPitch = 0, correction = 0;
    float mappingRatio = 1, mappingFundamental = 0;
    bool prepared = false;
    TuningTelemetry state{};
};

} // namespace vocalstack
