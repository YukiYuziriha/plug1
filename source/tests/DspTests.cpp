#include "VocalEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

using vocalstack::Settings;
using vocalstack::VocalEngine;
using Track = std::vector<float>;

void expect(bool result, const char* reason) {
    if (!result)
        throw std::runtime_error(reason);
}

Settings neutral() {
    Settings settings;
    settings.level = settings.doubles = 0;
    return settings;
}

struct Render {
    Track left, right;
    int latency;
    vocalstack::Meters meters;
};

Render render(const Track& left, const Track* right, int rate, int outputChannels,
              const Settings& settings, int block = 137) {
    VocalEngine engine;
    engine.prepare(rate, right ? 2 : 1, outputChannels, settings);
    Render result{Track(left.size()), Track(left.size()), engine.latency(), {}};
    for (int position = 0; position < static_cast<int>(left.size()); position += block) {
        const float* inputs[] = {left.data() + position, right ? right->data() + position : nullptr};
        float* outputs[] = {result.left.data() + position, result.right.data() + position};
        engine.process(inputs, outputs, std::min(block, static_cast<int>(left.size()) - position), settings);
    }
    result.meters = engine.meters();
    return result;
}

float rms(const Track& track, int start, int finish) {
    double power = 0;
    for (int i = start; i < finish; ++i)
        power += track[i] * track[i];
    return static_cast<float>(std::sqrt(power / (finish - start)));
}

float ratioDb(float a, float b) { return 20.0f * std::log10(a / b); }

Track tone(int rate, float level, float frequency, int seconds) {
    Track input(static_cast<size_t>(rate * seconds));
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = level * static_cast<float>(std::sin(2 * std::numbers::pi * frequency * i / rate));
    return input;
}

Track vowel(int rate, int seconds, double fundamental = 125) {
    Track input(static_cast<size_t>(rate * seconds));
    for (int harmonic = 1; harmonic <= 36; ++harmonic) {
        const double frequency = harmonic * fundamental;
        const double low = std::exp(-std::pow((frequency - 625) / 240, 2));
        const double high = std::exp(-std::pow((frequency - 1450) / 310, 2));
        const double amplitude = (0.31 + 5.0 * low + 2.2 * high) * 0.085 / harmonic;
        for (size_t i = 0; i < input.size(); ++i)
            input[i] += static_cast<float>(amplitude *
                std::sin(2 * std::numbers::pi * frequency * i / rate + 1.73 * harmonic));
    }
    return input;
}

double amplitudeAt(const Track& signal, int start, int count, int rate, float hz) {
    const auto radians = 2 * std::numbers::pi * hz / rate;
    double real = 0, imaginary = 0;
    for (int i = 0; i < count; ++i) {
        const auto sample = signal[start + i];
        real += sample * std::cos(i * radians);
        imaginary += sample * std::sin(i * radians);
    }
    return std::hypot(real, imaginary) * 2 / count;
}

void alignment() {
    for (const auto rate : {44100, 48000, 96000, 192000}) {
        Track left(20000), right(20000);
        left[193] = 0.51f;
        right[491] = -0.38f;
        auto settings = Settings{};
        settings.bypass = true;
        const auto wet = render(left, &right, rate, 2, settings);
        const auto expectedLatency = rate == 44100 ? 1344 : rate * 28 / 1000;
        expect(wet.latency == expectedLatency, "Reported latency does not match the shorter window");
        for (size_t i = 0; i < left.size(); ++i) {
            expect(wet.left[i] == (i == static_cast<size_t>(193 + wet.latency) ? 0.51f : 0.0f),
                   "Bypassed left is not an exact latency-aligned copy");
            expect(wet.right[i] == (i == static_cast<size_t>(491 + wet.latency) ? -0.38f : 0.0f),
                   "Bypassed right is not an exact latency-aligned copy");
        }
        const auto mono = render(left, nullptr, rate, 2, neutral());
        expect(mono.left == wet.left && mono.left == mono.right, "Neutral mono-to-stereo latency mismatch");
    }
}

void leveler() {
    constexpr int rate = 48000;
    const auto quiet = tone(rate, 0.035f, 125, 2);
    const auto loud = tone(rate, 0.50f, 125, 2);
    const auto noise = tone(rate, 0.0008f, 125, 2);
    auto settings = neutral();
    settings.level = 1;
    const auto raised = render(quiet, nullptr, rate, 1, settings);
    const auto lowered = render(loud, nullptr, rate, 1, settings);
    const auto silent = render(noise, nullptr, rate, 1, settings);
    const int start = rate * 3 / 2;
    const auto compare = [start, rate](const Track& source, const Render& out) {
        return ratioDb(rms(out.left, start, 2 * rate),
                       rms(source, start - out.latency, 2 * rate - out.latency));
    };
    const float raise = compare(quiet, raised);
    const float lower = compare(loud, lowered);
    const float floor = compare(noise, silent);
    std::cout << "Leveler dB change, quiet/loud/below gate: " << raise << " / " << lower << " / " << floor << '\n';
    expect(raise > 6.0f && raise <= 8.01f, "Quiet phrase gain out of bounds");
    expect(lower < -8.0f && lower >= -10.01f, "Loud phrase gain out of bounds");
    expect(std::abs(floor) < 0.005f, "Below-gate input was amplified");
}

void formant() {
    constexpr int rate = 48000;
    const auto source = vowel(rate, 2);
    auto settings = neutral();
    settings.formant = 5.0f;
    const auto shifted = render(source, nullptr, rate, 1, settings);
    constexpr int start = rate, count = rate / 2;
    const auto fundamental = amplitudeAt(shifted.left, start, count, rate, 125);
    const auto wrongPitch = amplitudeAt(shifted.left, start, count, rate, 125 * std::exp2(5.0f / 12));
    const double before = amplitudeAt(source, start, count, rate, 875) /
                          amplitudeAt(source, start, count, rate, 625);
    const double after = amplitudeAt(shifted.left, start, count, rate, 875) /
                         amplitudeAt(shifted.left, start, count, rate, 625);
    std::cout << "Formant envelope ratio at 875/625 Hz: " << before << " -> " << after
              << ", original/off-grid fundamental " << fundamental << " / " << wrongPitch << '\n';
    expect(fundamental > 0.006 && fundamental > 12 * wrongPitch, "Formant transposed the test fundamental");
    expect(after > before * 1.45, "Formant control did not move the tested spectral envelope");

    for (const int pitch : {72, 260}) {
        const auto input = vowel(rate, 2, pitch);
        for (const float shift : {-5.0f, 5.0f}) {
            settings.formant = shift;
            const auto result = render(input, nullptr, rate, 1, settings);
            double harmonicPower = 0;
            for (int harmonic = 1; harmonic * pitch < 16000; ++harmonic) {
                const auto amplitude = amplitudeAt(result.left, start, count, rate, harmonic * pitch);
                harmonicPower += 0.5 * amplitude * amplitude;
            }
            const double totalPower = std::pow(rms(result.left, start, start + count), 2);
            expect(totalPower > 1.0e-6 && harmonicPower > 0.995 * totalPower,
                   "Short-window formant shift introduced off-pitch energy");
        }
    }
}

void doubles() {
    constexpr int rate = 48000;
    auto input = vowel(rate, 3);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] *= static_cast<float>(0.75 + 0.25 * std::sin(i * 2.0 * std::numbers::pi / (rate * 0.43)));
    auto settings = neutral();
    settings.doubles = 0.70f;
    settings.width = 1.0f;
    settings.human = 0.75f;
    const auto a = render(input, nullptr, rate, 2, settings);
    const auto b = render(input, nullptr, rate, 2, settings, 509);
    const auto mono = render(input, nullptr, rate, 1, settings);
    Track sides(input.size()), difference(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        expect(std::isfinite(a.left[i]) && std::isfinite(a.right[i]), "Doubler emitted non-finite audio");
        sides[i] = a.left[i] - a.right[i];
        difference[i] = a.left[i] - b.left[i];
    }
    const auto original = rms(input, rate, 3 * rate);
    const auto sideRatio = rms(sides, rate, 3 * rate) / original;
    const auto stereo = ratioDb(std::hypot(rms(a.left, rate, 3 * rate), rms(a.right, rate, 3 * rate)) *
                                std::sqrt(0.5f), original);
    const auto folded = ratioDb(rms(mono.left, rate, 3 * rate), original);
    std::cout << "Double stereo/mono RMS change: " << stereo << " / " << folded
              << " dB; side/lead ratio " << sideRatio << '\n';
    expect(sideRatio > 0.20f, "Doubles lack independent stereo content");
    expect(std::abs(stereo) < 1.3f && std::abs(folded) < 1.3f, "Excessive double loudness change");
    expect(rms(difference, rate, 3 * rate) < 1.0e-5f, "Render depends on host block size");
}

bool humanContinuity() {
    bool clean = true;
    for (const int rate : {44100, 48000, 96000, 192000}) {
        const auto input = tone(rate, 0.15f, 1000, 3);
        for (const float human : {0.0f, 0.35f, 1.0f}) {
            auto settings = neutral();
            settings.doubles = settings.width = 1.0f;
            settings.human = human;
            const auto output = render(input, nullptr, rate, 2, settings);
            double boundaryPower = 0, interiorPower = 0;
            int boundaries = 0, interiors = 0;
            for (int i = rate; i < 3 * rate; ++i) {
                const auto side = [&](int position) {
                    return output.left[position] - output.right[position];
                };
                const double acceleration = side(i) - 2.0 * side(i - 1) + side(i - 2);
                // A stepped read position creates impulses at both samples around an update.
                if (i % 64 <= 1) {
                    boundaryPower += acceleration * acceleration;
                    ++boundaries;
                } else {
                    interiorPower += acceleration * acceleration;
                    ++interiors;
                }
            }
            const double ratio = (boundaryPower / boundaries) / (interiorPower / interiors);
            std::cout << "Human " << human << ", " << rate
                      << " Hz: update-boundary/interior acceleration power " << ratio << '\n';
            clean = clean && std::isfinite(ratio) && ratio < 4.0;
        }
    }
    return clean;
}

bool formantLevel() {
    bool matched = true;
    for (const int rate : {44100, 48000, 96000}) {
        for (const int pitch : {72, 125, 260}) {
            const auto input = vowel(rate, 3, pitch);
            for (const float shift : {-7.0f, -4.0f, 4.0f, 7.0f}) {
                auto settings = neutral();
                settings.formant = shift;
                const auto output = render(input, nullptr, rate, 1, settings);
                const float change = ratioDb(rms(output.left, 2 * rate, 3 * rate),
                    rms(input, 2 * rate - output.latency, 3 * rate - output.latency));
                std::cout << "Formant " << shift << " st, " << pitch << " Hz, " << rate
                          << " Hz sample rate: RMS change " << change << " dB\n";
                matched = matched && std::isfinite(change) && std::abs(change) < 0.2f;
            }
        }
    }
    return matched;
}

void formantStereo() {
    constexpr int rate = 48000;
    const auto left = vowel(rate, 3);
    for (const float rightGain : {0.0f, 0.37f, -1.0f}) {
        auto right = left;
        for (auto& sample : right)
            sample *= rightGain;
        auto settings = neutral();
        settings.formant = -7.0f;
        const auto output = render(left, &right, rate, 2, settings);
        const float inputRms = std::hypot(rms(left, 2 * rate, 3 * rate), rms(right, 2 * rate, 3 * rate));
        const float outputRms = std::hypot(rms(output.left, 2 * rate, 3 * rate),
                                          rms(output.right, 2 * rate, 3 * rate));
        expect(std::abs(ratioDb(outputRms, inputRms)) < 0.2f, "Stereo formant level is not matched");
        for (size_t i = 0; i < left.size(); ++i)
            expect(std::abs(output.right[i] - rightGain * output.left[i]) < 1.0e-5f,
                   "Formant level matching changed the stereo balance");

        settings.level = 0.5f;
        settings.doubles = 0.7f;
        settings.human = 1.0f;
        const auto combined = render(left, &right, rate, 2, settings, 1);
        const auto blocked = render(left, &right, rate, 2, settings, 509);
        settings.formant = 0;
        const auto reference = render(left, &right, rate, 2, settings);
        const float combinedRms = std::hypot(rms(combined.left, 2 * rate, 3 * rate),
                                            rms(combined.right, 2 * rate, 3 * rate));
        const float referenceRms = std::hypot(rms(reference.left, 2 * rate, 3 * rate),
                                             rms(reference.right, 2 * rate, 3 * rate));
        expect(std::abs(ratioDb(combinedRms, referenceRms)) < 1.3f,
               "Formant normalization changed the leveler/doubler balance");
        for (size_t i = 0; i < left.size(); ++i) {
            expect(std::abs(combined.left[i] - blocked.left[i]) < 1.0e-5f &&
                   std::abs(combined.right[i] - blocked.right[i]) < 1.0e-5f,
                   "Shifted formant render depends on host block size");
            if (rightGain == -1.0f)
                expect(std::abs(combined.left[i] + combined.right[i]) < 1.0e-5f,
                       "A cancelling stereo mid produced spurious double voices");
        }
    }
}

void formantTransitions() {
    constexpr int rate = 48000, block = 64, seconds = 8;
    auto input = vowel(rate, seconds);
    for (int i = 0; i < seconds * rate; ++i) {
        if (i < rate / 2 || (i >= 2 * rate && i < 3 * rate) || i >= 6 * rate)
            input[i] = 0;
        else if (i >= 3 * rate && i < 4 * rate)
            input[i] *= 0.001f;
    }
    auto settings = neutral();
    settings.formant = -7;
    settings.doubles = 0.7f;
    VocalEngine engine;
    engine.prepare(rate, 1, 2, settings);
    Track left(input.size()), right(input.size());
    float peak = 0, maxStep = 0;
    for (int i = 0; i < seconds * rate; i += block) {
        if (i == 2 * rate || i == 4 * rate)
            settings.formant = 7;
        if (i == 3 * rate || i == 5 * rate)
            settings.formant = -7;
        if (i == 4 * rate)
            settings.formantOn = false;
        if (i == 5 * rate)
            settings.formantOn = true;
        if (i % rate == 0)
            settings.human = settings.human < 0.5f ? 1.0f : 0.0f;
        const float* in[] = {input.data() + i};
        float* out[] = {left.data() + i, right.data() + i};
        engine.process(in, out, block, settings);
    }
    for (size_t i = 0; i < input.size(); ++i) {
        expect(std::isfinite(left[i]) && std::isfinite(right[i]), "Formant/Human transition emitted non-finite audio");
        peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
        if (i > 0)
            maxStep = std::max(maxStep, std::max(std::abs(left[i] - left[i - 1]),
                                               std::abs(right[i] - right[i - 1])));
    }
    std::cout << "Formant/Human transitions: peak " << peak << ", largest step " << maxStep << '\n';
    expect(peak < 0.6f && maxStep < 0.12f, "Formant/Human transitions produced an excessive peak or discontinuity");
    expect(rms(left, 7 * rate, seconds * rate) < 1.0e-7f &&
           rms(right, 7 * rate, seconds * rate) < 1.0e-7f, "Normalization amplified a silent tail");

    settings = neutral();
    settings.formant = -7;
    settings.doubles = 0.7f;
    // Dirty the gain state immediately before reset, then exercise the next attack.
    // Start voiced: Signalsmith retains a silence-fast-path flag across reset, so
    // a silence-first samplewise replay would also test dependency scheduler state.
    const auto resetInput = vowel(rate, 3);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1)
            engine.reset(settings);
        for (int i = 0; i < 3 * rate; i += block) {
            const float* in[] = {resetInput.data() + i};
            float* out[] = {left.data() + i, right.data() + i};
            engine.process(in, out, block, settings);
        }
    }
    const auto fresh = render(resetInput, nullptr, rate, 2, settings, block);
    for (size_t i = 0; i < resetInput.size(); ++i)
        expect(std::abs(left[i] - fresh.left[i]) < 1.0e-6f &&
               std::abs(right[i] - fresh.right[i]) < 1.0e-6f,
               "Reset retained stale formant normalization or Human state");
}

void automation() {
    constexpr int rate = 48000, block = 64;
    auto settings = neutral();
    VocalEngine engine;
    engine.prepare(rate, 1, 2, settings);
    const auto signal = tone(rate, 0.18f, 125, 2);
    Track left(signal.size()), right(signal.size());
    for (int i = 0; i < 2 * rate; i += block) {
        if (i % 4096 == 0) {
            settings.bypass = !settings.bypass;
            settings.formant = settings.bypass ? -6.0f : 6.0f;
            settings.doubles = settings.bypass ? 0.0f : 1.0f;
            settings.width = settings.bypass ? 0.0f : 1.0f;
        }
        const float* in[] = {signal.data() + i};
        float* out[] = {left.data() + i, right.data() + i};
        engine.process(in, out, std::min(block, 2 * rate - i), settings);
    }
    float maxStep = 0;
    for (size_t i = 1; i < left.size(); ++i) {
        expect(std::isfinite(left[i]) && std::isfinite(right[i]), "Automation emitted non-finite audio");
        maxStep = std::max(maxStep, std::abs(left[i] - left[i - 1]));
    }
    std::cout << "Largest sample step during abrupt automation: " << maxStep << '\n';
    expect(maxStep < 0.045f, "Automation produced an abrupt sample discontinuity");
}

bool sameReading(const vocalstack::MeterReading& a, const vocalstack::MeterReading& b) {
    return a.rms == b.rms && a.peak == b.peak && a.heldPeak == b.heldPeak &&
           a.maxPeak == b.maxPeak && a.clipped == b.clipped;
}

vocalstack::MeterReading measure(const Track& track, int rate) {
    vocalstack::MeterDetector detector;
    detector.prepare(rate);
    for (const float sample : track)
        detector.process(sample);
    return detector.reading();
}

void outputTrim() {
    const Settings legacy{0.5f, 0.35f, 0.35f, 0.65f, 0.0f, true, true, true, false};
    expect(legacy.outputGainDb == 0, "Appending output trim broke legacy Settings aggregates");
    constexpr int rate = 48000;
    const auto left = tone(rate, 0.35f, 1000, 1);
    const auto right = tone(rate, 0.10f, 750, 1);
    for (const float db : {-24.0f, -6.0f, 0.0f, 6.0f, 12.0f}) {
        auto settings = neutral();
        settings.outputGainDb = db;
        const auto output = render(left, &right, rate, 2, settings);
        const float gain = std::pow(10.0f, db / 20.0f);
        expect(output.latency == VocalEngine::defaultLatency, "Output trim added latency");
        for (int i = 0; i < rate; ++i) {
            const float expectedLeft = i < output.latency ? 0 : left[i - output.latency] * gain;
            const float expectedRight = i < output.latency ? 0 : right[i - output.latency] * gain;
            expect(std::abs(output.left[i] - expectedLeft) < 5.0e-7f &&
                   std::abs(output.right[i] - expectedRight) < 5.0e-7f,
                   "Output trim is not the requested linear gain (or was limited)");
            if (db == 0)
                expect(output.left[i] == expectedLeft && output.right[i] == expectedRight,
                       "Unity output trim is not an exact neutral render");
        }
        expect(output.meters.inputChannels == 2 && output.meters.outputChannels == 2,
               "Stereo meters report incorrect channel counts");
        expect(sameReading(output.meters.input[0], measure(left, rate)) &&
               sameReading(output.meters.input[1], measure(right, rate)),
               "Input meters do not measure independent pre-DSP channels");
        expect(sameReading(output.meters.output[0], measure(output.left, rate)) &&
               sameReading(output.meters.output[1], measure(output.right, rate)),
               "Output meters do not measure actual post-trim audio");
        if (db == 12)
            expect(output.meters.output[0].clipped && !output.meters.output[1].clipped &&
                   output.meters.output[0].maxPeak > 1.0f && !output.meters.input[0].clipped,
                   "Post-trim clipping or independent stereo clip detection is incorrect");
    }
}

void outputTrimAutomation() {
    for (const int rate : {44100, 48000}) {
        const Track input(rate, 0.2f);
        const std::array<int, 3> events{rate / 5 + 13, rate / 2 + 7, 3 * rate / 4 + 19};
        const std::array<float, 3> values{12, -24, 6};
        const auto automate = [&](int block) {
            auto settings = neutral();
            VocalEngine engine;
            engine.prepare(rate, 1, 1, settings);
            Track output(rate);
            int event = 0;
            for (int position = 0; position < rate;) {
                if (event < 3 && position == events[event])
                    settings.outputGainDb = values[event++];
                const int end = event < 3 ? events[event] : rate;
                const int count = std::min(block, end - position);
                const float* in[] = {input.data() + position};
                float* out[] = {output.data() + position};
                engine.process(in, out, count, settings);
                position += count;
            }
            return output;
        };
        const auto single = automate(1);
        const auto irregular = automate(137);
        const auto large = automate(512);
        const int latency = rate == 44100 ? 1344 : VocalEngine::defaultLatency;
        const float step = static_cast<float>(-std::expm1(-1.0 / (rate * 0.018)));
        double db = 0;
        float target = 0;
        int event = 0;
        for (int i = 0; i < rate; ++i) {
            if (event < 3 && i == events[event])
                target = values[event++];
            db += (target - db) * step;
            const float expected = i < latency ? 0 : 0.2f * static_cast<float>(std::exp(db * 0.1151292546497022842));
            expect(std::abs(single[i] - expected) < 2.0e-7f,
                   "Output automation does not follow an 18 ms sample-smoothed dB gain");
            expect(single[i] == irregular[i] && single[i] == large[i],
                   "Output trim automation depends on host block size");
            if (i > latency)
                expect(std::abs(single[i] - single[i - 1]) < 0.005f,
                       "Output trim automation produced a sample discontinuity");
        }
    }
}

void outputTrimBypass() {
    constexpr int rate = 48000;
    const auto left = tone(rate, 0.24f, 431, 1);
    const auto right = tone(rate, 0.11f, 797, 1);
    for (const bool host : {false, true}) {
        auto settings = Settings{};
        settings.formant = -4;
        settings.outputGainDb = 12;
        settings.bypass = !host;
        VocalEngine engine;
        engine.prepare(rate, 2, 2, settings);
        Track outLeft(rate), outRight(rate);
        for (int position = 0; position < rate;) {
            const int event = rate / 2;
            if (position == event)
                settings.outputGainDb = -24;
            const int end = position < event ? event : rate;
            const int count = std::min(137, end - position);
            const float* in[] = {left.data() + position, right.data() + position};
            float* out[] = {outLeft.data() + position, outRight.data() + position};
            engine.process(in, out, count, settings, host);
            position += count;
        }
        for (int i = host ? rate / 4 : 0; i < rate; ++i) {
            const float expectedLeft = i < engine.latency() ? 0 : left[i - engine.latency()];
            const float expectedRight = i < engine.latency() ? 0 : right[i - engine.latency()];
            expect(outLeft[i] == expectedLeft && outRight[i] == expectedRight,
                   "Full parameter/host bypass is not an exact dry copy independent of output trim");
        }
        expect(sameReading(engine.meters().output[0], measure(outLeft, rate)) &&
               sameReading(engine.meters().output[1], measure(outRight, rate)),
               "Bypassed output metering does not follow the actual final output");
    }
}

void meterBallistics() {
    for (const int rate : {44100, 48000, 96000, 192000}) {
        vocalstack::MeterDetector detector;
        detector.prepare(rate);
        for (int i = 0; i < 256; ++i)
            detector.process(0);
        expect(sameReading(detector.reading(), {}), "Silence created a meter reading");
        for (int i = 0; i < 3 * rate / 10; ++i)
            detector.process(0.5f);
        expect(std::abs(detector.reading().rms - 0.5f * std::sqrt(1.0f - std::exp(-1.0f))) < 1.0e-6f &&
               !detector.reading().clipped,
               "RMS detector does not integrate power with a 300 ms time constant or falsely clipped");
        detector.reset();
        for (int i = 0; i < 3 * rate; ++i)
            detector.process(static_cast<float>(std::sin(2 * std::numbers::pi * 1000 * i / rate)));
        const auto sine = detector.reading();
        expect(std::abs(ratioDb(sine.rms, 1.0f) + 3.0103f) < 0.015f &&
               std::abs(ratioDb(sine.peak, 1.0f)) < 0.04f,
               "Full-scale sine RMS is not -3.01 dBFS with a near-0 dBFS sample peak");
        detector.reset();
        detector.process(-1.0f);
        for (int i = 0; i < 3 * rate / 2; ++i)
            detector.process(0);
        auto reading = detector.reading();
        expect(std::abs(reading.heldPeak - 1.0f) < 1.0e-5f && reading.maxPeak == 1 && reading.clipped,
               "Peak hold/max/clip latch did not survive 1.5 seconds of silent samples");
        expect(std::abs(ratioDb(reading.peak, 1.0f) + 36.0f) < 0.002f,
               "Fast sample peak release is not 24 dB/second");
        for (int i = 0; i < rate; ++i)
            detector.process(0);
        reading = detector.reading();
        expect(std::abs(ratioDb(reading.heldPeak, 1.0f) + 12.0f) < 0.002f &&
               reading.maxPeak == 1 && reading.clipped,
               "Held peak release is not 12 dB/second or clip/max was not latched");
        detector.process(-1.25f);
        expect(detector.reading().maxPeak == 1.25f && detector.reading().heldPeak == 1.25f,
               "A new sample peak did not re-arm the hold and maximum");
        const auto before = detector.reading();
        detector.resetPeaks();
        reading = detector.reading();
        expect(reading.rms == before.rms && reading.peak == before.peak &&
               reading.heldPeak == 0 && reading.maxPeak == 0 && !reading.clipped,
               "Display clear altered moving metering or retained peak/clip history");
        detector.reset();
        expect(sameReading(detector.reading(), {}), "Full detector reset retained meter history");
    }
    vocalstack::MeterDetector changingRate;
    changingRate.prepare(48000);
    changingRate.process(0.8f);
    for (int i = 0; i < 36000; ++i)
        changingRate.process(0);
    changingRate.prepare(96000);
    for (int i = 0; i < 72000; ++i)
        changingRate.process(0);
    expect(std::abs(changingRate.reading().heldPeak - 0.8f) < 1.0e-5f,
           "Sample-rate change shortened the remaining peak hold");
    for (int i = 0; i < 96000; ++i)
        changingRate.process(0);
    expect(std::abs(ratioDb(changingRate.reading().heldPeak, 0.8f) + 12.0f) < 0.002f,
           "Meter timing is not monotonic in seconds across sample-rate changes");
}

void meterRoutingAndReset() {
    constexpr int rate = 48000;
    auto input = tone(rate, 0.35f, 311, 1);
    input[37] = -1.2f;
    auto settings = neutral();
    const auto mono = render(input, nullptr, rate, 1, settings);
    const auto stereo = render(input, nullptr, rate, 2, settings);
    const auto blocked = render(input, nullptr, rate, 1, settings, 512);
    expect(sameReading(mono.meters.input[0], blocked.meters.input[0]) &&
           sameReading(mono.meters.output[0], blocked.meters.output[0]),
           "Meter timing depends on host block size");
    expect(mono.meters.inputChannels == 1 && mono.meters.outputChannels == 1 &&
           sameReading(mono.meters.input[1], {}) && sameReading(mono.meters.output[1], {}),
           "Mono meters report phantom channels or nonzero unused readings");
    expect(stereo.meters.inputChannels == 1 && stereo.meters.outputChannels == 2 &&
           sameReading(stereo.meters.input[1], {}) &&
           sameReading(stereo.meters.output[0], stereo.meters.output[1]),
           "Mono-to-stereo meter channel counts or routing is incorrect");
    expect(mono.meters.input[0].clipped && mono.meters.output[0].clipped,
           "Clip latches were lost at a later host block");
    Track inPlace = input;
    VocalEngine aliased;
    aliased.prepare(rate, 1, 1, settings);
    for (int i = 0; i < rate; i += 137) {
        const float* in[] = {inPlace.data() + i};
        float* out[] = {inPlace.data() + i};
        aliased.process(in, out, std::min(137, rate - i), settings);
    }
    expect(inPlace == mono.left && sameReading(aliased.meters().input[0], mono.meters.input[0]) &&
           sameReading(aliased.meters().output[0], mono.meters.output[0]),
           "In-place buffers changed pre-DSP metering or audio");

    settings.doubles = 0.6f;
    settings.human = 0.8f;
    settings.formant = -4;
    settings.outputGainDb = -6;
    VocalEngine cleared, reference;
    cleared.prepare(rate, 1, 2, settings);
    reference.prepare(rate, 1, 2, settings);
    Track left(rate), right(rate), referenceLeft(rate), referenceRight(rate);
    for (int position = 0; position < rate;) {
        if (position == rate / 2) {
            const auto before = cleared.meters();
            expect(before.input[0].clipped && before.input[0].maxPeak == 1.2f,
                   "Input clip/max history vanished before display reset");
            cleared.resetMeters();
            const auto after = cleared.meters();
            expect(after.inputChannels == before.inputChannels && after.outputChannels == before.outputChannels &&
                   after.correctionDb == before.correctionDb, "Display reset changed meter metadata/correction");
            for (int channel = 0; channel < 2; ++channel) {
                expect(after.input[channel].rms == before.input[channel].rms &&
                       after.input[channel].peak == before.input[channel].peak &&
                       after.output[channel].rms == before.output[channel].rms &&
                       after.output[channel].peak == before.output[channel].peak,
                       "Display reset cleared moving RMS/fast peak");
                expect(after.input[channel].heldPeak == 0 && after.input[channel].maxPeak == 0 &&
                       !after.input[channel].clipped && after.output[channel].heldPeak == 0 &&
                       after.output[channel].maxPeak == 0 && !after.output[channel].clipped,
                       "Display reset retained held/max peaks or clipping");
            }
        }
        const int end = position < rate / 2 ? rate / 2 : rate;
        const int count = std::min(137, end - position);
        const float* in[] = {input.data() + position};
        float* out[] = {left.data() + position, right.data() + position};
        float* ref[] = {referenceLeft.data() + position, referenceRight.data() + position};
        cleared.process(in, out, count, settings);
        reference.process(in, ref, count, settings);
        position += count;
    }
    expect(left == referenceLeft && right == referenceRight,
           "Display reset changed audio, delay, smoothing, or Human/formant state");
    expect(!cleared.meters().input[0].clipped && reference.meters().input[0].clipped,
           "Display reset did not independently clear the clip latch");
    expect(cleared.meters().input[0].rms == reference.meters().input[0].rms &&
           cleared.meters().output[0].rms == reference.meters().output[0].rms &&
           cleared.meters().input[0].maxPeak <= 0.35f,
           "Display reset restarted the RMS integration or retained the old maximum");

    settings = neutral();
    settings.outputGainDb = 6;
    cleared.reset(settings);
    const auto reset = cleared.meters();
    expect(reset.correctionDb == 0, "Engine reset retained correction metering");
    for (int channel = 0; channel < 2; ++channel)
        expect(sameReading(reset.input[channel], {}) && sameReading(reset.output[channel], {}),
               "Engine reset did not clear all meter history");
    const auto fresh = render(input, nullptr, rate, 2, settings);
    for (int i = 0; i < rate; i += 137) {
        const float* in[] = {input.data() + i};
        float* out[] = {left.data() + i, right.data() + i};
        cleared.process(in, out, std::min(137, rate - i), settings);
    }
    expect(left == fresh.left && right == fresh.right,
           "Engine reset retained output trim smoothing or stale delay audio");
    const Track silence(1024);
    const auto silent = render(silence, nullptr, rate, 2, neutral());
    expect(sameReading(silent.meters.input[0], {}) && sameReading(silent.meters.output[0], {}) &&
           sameReading(silent.meters.output[1], {}), "Silent engine meters were nonzero");
}

void benchmark() {
    constexpr int rate = 48000, duration = 6;
    const auto input = vowel(rate, duration);
    auto settings = Settings{};
    settings.formant = -3.0f;
    const auto started = std::chrono::steady_clock::now();
    const auto output = render(input, &input, rate, 2, settings, 256);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    expect(rms(output.left, rate, duration * rate) > 0.005f, "Benchmark output was silent");
    std::cout << "Stereo DSP, 48 kHz / 256 frames: " << elapsed / duration * 100.0
              << "% of realtime on this CPU\n";
}

int main() {
    try {
        const auto started = std::chrono::steady_clock::now();
        alignment();
        leveler();
        formant();
        doubles();
        automation();
        outputTrim();
        outputTrimAutomation();
        outputTrimBypass();
        meterBallistics();
        meterRoutingAndReset();
        const bool humanClean = humanContinuity();
        const bool formantMatched = formantLevel();
        expect(humanClean, "Human delay modulation creates discontinuities at control updates");
        expect(formantMatched, "Formant shifting changes the settled output level");
        formantStereo();
        formantTransitions();
        benchmark();
        const auto duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::cout << "DSP checks passed in " << duration << " seconds\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
