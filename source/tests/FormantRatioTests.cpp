#include "VocalEngine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
constexpr int rate = 48000;
std::vector<float> excitation(double centre) {
    std::vector<float> input(rate * 2);
    const double width = centre / 5;
    // Dense voiced excitation with a nonzero broadband floor lets a spectral
    // envelope move across FOUR octaves without mistaking missing harmonics
    // or Nyquist truncation for an artificial +/-24 st implementation cap.
    for (int h = 1; h <= 200; ++h) {
        const double frequency = h * 100;
        const double magnitude = 0.0005 + 0.008 * std::exp(-0.5 * std::pow((frequency - centre) / width, 2));
        for (int i = 0; i < int(input.size()); ++i)
            input[i] += float(magnitude * std::sin(2 * std::numbers::pi * frequency * i / rate + h * 1.73));
    }
    return input;
}
double envelopePeak(const std::vector<float>& data) {
    constexpr int n = 32768;
    std::vector<std::complex<double>> spectrum(n);
    for (int i = 0; i < n; ++i) spectrum[i] = data[data.size() - n + i]
        * (0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / n));
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(spectrum[i], spectrum[j]);
    }
    for (int length = 2; length <= n; length *= 2) {
        const auto step = std::polar(1.0, -2 * std::numbers::pi / length);
        for (int start = 0; start < n; start += length) {
            std::complex<double> rotation{1, 0};
            for (int i = 0; i < length / 2; ++i) {
                const auto a = spectrum[start + i], b = spectrum[start + i + length / 2] * rotation;
                spectrum[start + i] = a + b; spectrum[start + i + length / 2] = a - b;
                rotation *= step;
            }
        }
    }
    // Integrate bands, not just ideal harmonic frequencies: pitch/transient
    // sidebands belong to the measured spectral envelope too.
    std::vector<double> energy(n / 2 + 2);
    for (int i = 0; i <= n / 2; ++i) energy[i + 1] = energy[i] + std::norm(spectrum[i]);
    constexpr int radius = 75 * n / rate;
    double best = 0; int peak = 0;
    for (int i = radius + 1; i < int(21000.0 * n / rate); ++i) {
        const auto power = energy[i + radius + 1] - energy[i - radius];
        if (power > best) { best = power; peak = i; }
    }
    if (!(best > 1e-12)) throw std::runtime_error("formant test rendered silence");
    return double(peak) * rate / n;
}
void verify(float sign, float musicalPitch) {
    const double centre = sign > 0 ? 500 : 8000;
    const double expectedRatio = sign > 0 ? 16 : 1.0 / 16;
    const auto input = excitation(centre);
    std::vector<float> output(input.size()), right(input.size());
    vocalstack::Settings s; s.advanced = true; s.levelOn = false; s.human = 0;
    s.width = 0; s.doubles = 1; s.formant = sign * 24;
    for (auto& voice : s.voiceSettings) voice.present = false;
    s.voiceSettings[0] = {true, 0, 0, 0, 0, sign * 24, 0, 1, musicalPitch, true};
    vocalstack::VocalEngine engine; engine.prepare(rate, 1, 2, s);
    vocalstack::Graph g{{{1, vocalstack::NodeType::voice, 0}, {2, vocalstack::NodeType::output}}, {{1, 2, 0}}};
    if (!engine.requestGraph(g)) throw std::runtime_error("formant graph rejected");
    for (int offset = 0; offset < int(input.size());) {
        const int count = std::min(offset % 2 ? 137 : 511, int(input.size()) - offset);
        const float* in[] = {input.data() + offset};
        float* out[] = {output.data() + offset, right.data() + offset};
        engine.process(in, out, count, s); offset += count;
    }
    for (float sample : output) if (!std::isfinite(sample)) throw std::runtime_error("nonfinite stacked formant audio");
    // Search the complete spectrum, including the incorrect x4-capped centre.
    // A target/snapshot-only implementation cannot pass this test.
    const double peak = envelopePeak(output);
    const double measuredRatio = peak / centre;
    std::cout << "Stacked formant " << sign * 48 << " st, pitch " << musicalPitch
              << " st: envelope " << centre << " -> " << peak << " Hz, ratio " << measuredRatio << '\n';
    // Harmonic-grid resolution and envelope smoothing prevent a promise of an
    // exact peak bin. This tolerance still decisively excludes a x4 / 1/4 cap.
    if (!(measuredRatio > expectedRatio * 0.7 && measuredRatio < expectedRatio * 1.3))
        throw std::runtime_error("rendered spectral envelope did not move by the stacked +/-48 st ratio");
}
}
int main() {
    try {
        for (float sign : {-1.0f, 1.0f}) for (float pitch : {0.0f, 7.5f}) verify(sign, pitch);
        std::cout << "Actual stacked formant audio ratios passed on legacy and harmony paths\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
