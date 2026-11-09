#include "VocalEngine.h"

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
    for (size_t i = 1; i < left.size(); ++i)
        maxStep = std::max(maxStep, std::abs(left[i] - left[i - 1]));
    std::cout << "Largest sample step during abrupt automation: " << maxStep << '\n';
    expect(maxStep < 0.045f, "Automation produced an abrupt sample discontinuity");
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
        benchmark();
        const auto duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::cout << "DSP checks passed in " << duration << " seconds\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
