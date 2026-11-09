#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>
#include <stdexcept>

namespace vocalstack {

// Fixed-latency, phase-continuous STFT harmony shifter. Unlike the subtle
// doubler path, this remaps instantaneous frequencies explicitly, so large
// intervals do not depend on peak-bin prediction. No FFT library/JUCE or
// allocation after prepare. Formants are an independently remapped, smoothed
// spectral envelope, not an extra pitch shift. This is not pitch correction.
class HarmonyShifter {
public:
    void prepare(double sampleRate, int latencySamples) {
        if (!std::isfinite(sampleRate) || sampleRate <= 0 || latencySamples < 127)
            throw std::invalid_argument("harmony needs a finite positive rate and at least 127 samples latency");
        size = 128;
        while (size * 2 <= latencySamples && size < 4096) size *= 2;
        hop = size / 8;
        extraDelay = std::max(0, latencySamples - (size - 1));
        input.assign(size, 0); overlap.assign(size, 0);
        delay.assign(extraDelay + 1, 0);
        lowDelay.assign(size / 2 + 1, 0);
        spectrum.resize(size); output.resize(size);
        previousPhase.assign(size / 2 + 1, 0); phase.assign(size / 2 + 1, 0);
        magnitude.resize(size / 2 + 1); frequency.resize(size / 2 + 1);
        envelope.resize(size / 2 + 1); mappedMagnitude.resize(size / 2 + 1);
        mappedFrequency.resize(size / 2 + 1); window.resize(size);
        for (int i = 0; i < size; ++i) window[i] = 0.5f - 0.5f * std::cos(tau * i / size);
        envelopeRadius = std::max(2, int(std::min(double(size / 2), 350.0 * size / sampleRate)));
        reset();
    }
    void reset() noexcept {
        for (auto* v : {&input, &overlap, &delay, &previousPhase, &phase}) std::fill(v->begin(), v->end(), 0.0f);
        std::fill(lowDelay.begin(), lowDelay.end(), 0.0f);
        oscillators = {};
        next = outputNext = delayNext = lowNext = sinceFrame = 0;
        pitchRatio = formantRatio = 1;
    }
    void setSemitones(float pitch, float formant) noexcept {
        // Numeric safety bounds, well outside the engine's musical/motion
        // range. In particular, +/-48 st formants are never capped to +/-24.
        const auto finiteSemitones = [](float x) { return std::isfinite(x) ? std::clamp(x, -96.0f, 96.0f) : 0.0f; };
        pitchRatio = std::exp2(finiteSemitones(pitch) / 12);
        formantRatio = std::exp2(finiteSemitones(formant) / 12);
    }
    // Actual factor consumed by envelope remapping, useful for DSP inspection.
    float formantFactor() const noexcept { return formantRatio; }
    float process(float sample) noexcept {
        if (!size) return 0;
        input[next] = std::isfinite(sample) ? std::clamp(sample, -1e6f, 1e6f) : 0.0f;
        if (++next == size) next = 0;
        if (++sinceFrame == hop) { frame(); sinceFrame = 0; }
        float low = 0;
        for (auto& oscillator : oscillators) {
            oscillator.amplitude += (oscillator.target - oscillator.amplitude) * (2.0f / hop);
            if (oscillator.amplitude < 1e-9f && oscillator.target == 0) continue;
            oscillator.phase += oscillator.frequency;
            if (oscillator.phase > tau) oscillator.phase -= tau;
            low += oscillator.amplitude * std::sin(oscillator.phase);
        }
        // Sinusoidal estimates refer to the analysis-window centre. Delay that
        // bank by half a window to align it with the STFT's frame-start output.
        lowDelay[lowNext] = low;
        if (++lowNext == int(lowDelay.size())) lowNext = 0;
        const float rendered = overlap[outputNext] + lowDelay[lowNext];
        overlap[outputNext] = 0;
        if (++outputNext == size) outputNext = 0;
        delay[delayNext] = rendered;
        if (++delayNext == int(delay.size())) delayNext = 0;
        return delay[delayNext];
    }
private:
    static constexpr float tau = 6.2831853071795864769f;
    void fft(std::vector<std::complex<float>>& x, bool inverse) noexcept {
        for (int i = 1, j = 0; i < size; ++i) {
            int bit = size >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(x[i], x[j]);
        }
        for (int length = 2; length <= size; length *= 2) {
            const auto step = std::polar(1.0f, (inverse ? tau : -tau) / length);
            for (int start = 0; start < size; start += length) {
                std::complex<float> rotation{1, 0};
                for (int j = 0; j < length / 2; ++j) {
                    const auto a = x[start + j], b = x[start + j + length / 2] * rotation;
                    x[start + j] = a + b; x[start + j + length / 2] = a - b;
                    rotation *= step;
                }
            }
        }
        if (inverse) for (auto& xk : x) xk /= float(size);
    }
    float envelopeAt(float bin) const noexcept {
        if (bin < 0 || bin >= size / 2) return 0;
        const int i = int(bin);
        return envelope[i] + (envelope[i + 1] - envelope[i]) * (bin - i);
    }
    void frame() noexcept {
        for (int i = 0; i < size; ++i) spectrum[i] = input[(next + i) % size] * window[i];
        fft(spectrum, false);
        const int bins = size / 2;
        for (int k = 0; k <= bins; ++k) {
            const float p = std::arg(spectrum[k]);
            const float expected = tau * k * hop / size;
            const float delta = std::remainder(p - previousPhase[k] - expected, tau);
            previousPhase[k] = p;
            magnitude[k] = std::abs(spectrum[k]);
            frequency[k] = tau * k / size + delta / hop;
        }
        // Bounded linear-time moving RMS envelope. Ratios operate on the
        // envelope only; phase/frequency always follow the musical interval.
        double energy = 0;
        int left = 0, right = -1;
        for (int k = 0; k <= bins; ++k) {
            const int end = std::min(bins, k + envelopeRadius);
            while (right < end) { ++right; energy += double(magnitude[right]) * magnitude[right]; }
            const int start = std::max(0, k - envelopeRadius);
            while (left < start) { energy -= double(magnitude[left]) * magnitude[left]; ++left; }
            envelope[k] = float(std::sqrt(std::max(0.0, energy) / (right - left + 1)));
        }
        for (auto& oscillator : oscillators) oscillator.target = 0;
        std::fill(mappedMagnitude.begin(), mappedMagnitude.end(), 0.0f);
        std::fill(mappedFrequency.begin(), mappedFrequency.end(), 0.0f);
        for (int k = 1; k < bins; ++k) {
            // Map the measured frequency, not the integer analysis-bin centre.
            // At +36 st a one-bin analysis error becomes eight output bins;
            // centre-bin mapping would smear/cancel the intended harmonic.
            const float bin = frequency[k] * pitchRatio * size / tau;
            if (bin <= 0 || bin >= bins - 1) continue; // never alias out-of-band energy
            const float correction = std::min(16.0f,
                envelopeAt(bin / formantRatio) / std::max(1e-9f, envelope[k]));
            // At large downward intervals, distinct input harmonics collapse
            // into the same low FFT bin. Preserve those resolved input peaks
            // with independent phase-continuous oscillators instead of averaging
            // their frequencies. Crossfade into STFT synthesis over bins 2..3.
            const float lowWeight = std::clamp(3.0f - bin, 0.0f, 1.0f);
            if (k < int(oscillators.size()) && lowWeight > 0 && magnitude[k] > magnitude[k - 1]
                && magnitude[k] >= magnitude[k + 1]) {
                auto& oscillator = oscillators[k];
                oscillator.target = 4.0f * magnitude[k] * correction * lowWeight / size;
                oscillator.frequency = frequency[k] * pitchRatio;
            }
            const float m = magnitude[k] * correction * (1 - lowWeight);
            const int low = int(bin);
            for (int i = 0; i < 2; ++i) {
                const float weight = i ? bin - low : 1 - (bin - low);
                const float amplitude = m * weight;
                mappedMagnitude[low + i] += amplitude;
                mappedFrequency[low + i] += amplitude * frequency[k] * pitchRatio;
            }
        }
        std::fill(output.begin(), output.end(), std::complex<float>{});
        for (int k = 1; k < bins; ++k) {
            const float m = mappedMagnitude[k];
            const float f = m > 1e-12f ? mappedFrequency[k] / m : tau * k / size;
            phase[k] = std::remainder(phase[k] + f * hop, tau);
            output[k] = std::polar(m, phase[k]);
            output[size - k] = std::conj(output[k]);
        }
        fft(output, true);
        const float normalization = 8.0f * hop / (3.0f * size);
        for (int i = 0; i < size; ++i)
            overlap[(outputNext + i) % size] += output[i].real() * window[i] * normalization;
    }
    int size = 0, hop = 0, extraDelay = 0, envelopeRadius = 0;
    int next = 0, outputNext = 0, delayNext = 0, lowNext = 0, sinceFrame = 0;
    struct Oscillator { float phase = 0, frequency = 0, amplitude = 0, target = 0; };
    std::array<Oscillator, 64> oscillators{};
    float pitchRatio = 1, formantRatio = 1;
    std::vector<float> input, overlap, delay, lowDelay, window, previousPhase, phase;
    std::vector<float> magnitude, frequency, envelope, mappedMagnitude, mappedFrequency;
    std::vector<std::complex<float>> spectrum, output;
};
} // namespace vocalstack
