#include "HarmonyShifter.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

// Actual output frequencies, not reported settings. Co-moving formants isolate
// pitch from envelope correction; separate FormantRatioTests cover the envelope.
int main() {
    try {
        for (int rate : {44100, 48000, 88200, 96000, 192000}) {
            const int latency = rate == 44100 || rate == 48000 ? 1344 : int(std::lround(rate * 1344.0 / 48000));
            const int n = rate * 2;
            std::vector<float> out(n);
            for (float pitch : {-36.0f, -24.0f, -12.0f, 0.0f, 7.5f, 12.0f, 24.0f, 36.0f}) {
                vocalstack::HarmonyShifter shifter; shifter.prepare(rate, latency);
                const float total = pitch + 0.17f;
                shifter.setSemitones(total, total);
                const double target = 440 * std::exp2(total / 12);
                for (int i = 0; i < n; ++i)
                    out[i] = shifter.process(0.1f * float(std::sin(2 * std::numbers::pi * 440 * i / rate)));
                const auto amplitude = [&](double hz) {
                    double real = 0, imaginary = 0;
                    for (int i = 0; i < rate; ++i) {
                        const double window = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / rate);
                        const double phase = 2 * std::numbers::pi * hz * i / rate;
                        real += out[rate + i] * window * std::cos(phase);
                        imaginary += out[rate + i] * window * std::sin(phase);
                    }
                    return 4 * std::hypot(real, imaginary) / rate;
                };
                const auto correct = amplitude(target);
                const auto wrong = amplitude(target * std::exp2(1.0 / 12));
                if (!(correct > 1e-4 && correct > wrong * 4))
                    throw std::runtime_error("harmony musical interval/fine detune mismatch");
                std::cout << "Harmony " << rate << " Hz, " << pitch << " st +17 ct: " << target << " Hz, amplitude " << correct << '\n';
            }
            vocalstack::HarmonyShifter shifter; shifter.prepare(rate, latency);
            int peakIndex = -1; float peak = 0;
            for (int i = 0; i < latency * 3; ++i) {
                const auto x = shifter.process(i == 0 ? 1.0f : 0.0f);
                if (std::abs(x) > peak) { peak = std::abs(x); peakIndex = i; }
            }
            if (peakIndex != latency) throw std::runtime_error("harmony source latency mismatch");
            for (float semitones : {-48.0f, 48.0f}) {
                shifter.setSemitones(0, semitones);
                const float expected = semitones > 0 ? 16.0f : 1.0f / 16;
                if (shifter.formantFactor() != expected)
                    throw std::runtime_error("actual envelope factor is not the full stacked formant ratio");
            }
        }
        vocalstack::HarmonyShifter shifter;
        if (shifter.process(1) != 0) throw std::runtime_error("unprepared harmony is unsafe");
        for (double rate : {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected = false;
            try { shifter.prepare(rate, 1344); } catch (const std::invalid_argument&) { rejected = true; }
            if (!rejected) throw std::runtime_error("invalid harmony rate accepted");
        }
        bool rejected = false;
        try { shifter.prepare(48000, 126); } catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("unalignable harmony latency accepted");
        shifter.prepare(48000, 1344);
        shifter.setSemitones(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity());
        for (int i = 0; i < 5000; ++i)
            if (!std::isfinite(shifter.process(std::numeric_limits<float>::infinity())))
                throw std::runtime_error("nonfinite harmony controls/input poisoned synthesis");
        std::cout << "Harmony spectral ratios, high rates, fixed latency and invalid-input guards passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
