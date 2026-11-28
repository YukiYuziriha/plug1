#include "SpatialEq.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

// Standalone: link with src/SpatialEq.cpp. No JUCE, host or reference VST3.
namespace {
using namespace vocalstack;
using Mode = SpatialEqDynamicMode;
using Stereo = std::array<std::vector<float>, 2>;
constexpr double pi = 3.14159265358979323846;
constexpr int onsetMs = 20;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

std::size_t sample(int rate, int ms) {
    return static_cast<std::size_t>(rate * ms / 1000);
}

Stereo fixture(int rate) {
    Stereo x;
    for (auto& lane : x) lane.resize(sample(rate, 1600));
    for (auto i = sample(rate, onsetMs); i < x[0].size(); ++i) {
        const bool loud = i >= sample(rate, 200) && i < sample(rate, 700);
        const double amplitude = loud ? 0.25 : 0.004;
        x[0][i] = static_cast<float>(amplitude * std::cos(2 * pi * (i % 20) / 20));
        // Anti-phase cancels the Mid, but must not cancel a linked power detector.
        x[1][i] = -x[0][i];
    }
    return x;
}

Stereo render(Stereo x, int rate, Mode mode, float gain = 0, float depth = 0,
              float attack = 15, float release = 150, bool irregular = false) {
    SpatialEqSettings settings;
    auto& band = settings.bands[2];
    band.active = true;
    band.frequency = static_cast<float>(rate) / 20;
    band.gainDb = gain;
    band.depth = depth;
    band.dynamicMode = mode;
    band.thresholdDb = -30;
    band.rangeDb = 6;
    band.attackMs = attack;
    band.releaseMs = release;
    SpatialEq eq;
    eq.prepare(rate);
    eq.setSettings(settings);
    constexpr std::array<std::size_t, 7> blocks{1, 17, 257, 31, 5, 512, 63};
    for (std::size_t pos = 0, block = 0; pos < x[0].size();) {
        const auto remaining = x[0].size() - pos;
        const auto count = irregular ? std::min(remaining, blocks[block++ % blocks.size()]) : remaining;
        eq.process(x[0].data() + pos, x[1].data() + pos, count);
        pos += count;
    }
    for (const auto& lane : x)
        require(std::all_of(lane.begin(), lane.end(), [](float v) { return std::isfinite(v); }),
                "non-finite dynamics output");
    return x;
}

double gainDb(const Stereo& y, const Stereo& dry, int rate, int startMs, int stopMs) {
    double input = 0, output = 0;
    for (std::size_t lane = 0; lane < y.size(); ++lane) {
        for (auto i = sample(rate, startMs); i < sample(rate, stopMs); ++i) {
            input += static_cast<double>(dry[lane][i]) * dry[lane][i];
            output += static_cast<double>(y[lane][i]) * y[lane][i];
        }
    }
    return 10 * std::log10(output / input);
}

void checkModesAndTiming(const Stereo& x, int rate) {
    const auto off = render(x, rate, Mode::Off);
    const auto down = render(x, rate, Mode::Downward);
    const auto up = render(x, rate, Mode::Upward);
    require(off == x, "Off with flat static gain must be exact dry");
    require(std::abs(gainDb(down, off, rate, 500, 680) + 6) < 0.1,
            "Downward must reach the -6 dB range at center above threshold");
    require(std::abs(gainDb(up, off, rate, 500, 680) - 6) < 0.1,
            "Upward must reach the +6 dB range at center above threshold");
    for (const auto* y : {&down, &up}) {
        require(std::abs(gainDb(*y, off, rate, 60, 180)) < 0.01,
                "quiet input must not trigger either mode");
        require(std::abs(gainDb(*y, off, rate, 1450, 1550)) < 0.01,
                "both modes must recover to unity on quiet input");
    }

    const auto fast = render(x, rate, Mode::Downward, 0, 0, 0.1f, 10);
    const auto slowAttack = render(x, rate, Mode::Downward, 0, 0, 200, 10);
    const auto slowRelease = render(x, rate, Mode::Downward, 0, 0, 0.1f, 500);
    require(gainDb(fast, off, rate, 220, 250) < gainDb(slowAttack, off, rate, 220, 250) - 1,
            "faster Attack must produce more early attenuation");
    require(std::abs(gainDb(fast, off, rate, 800, 860)) < 0.1,
            "fast Release did not recover after the loud-to-quiet step");
    require(gainDb(slowRelease, off, rate, 800, 860) < -2,
            "slow Release did not retain attenuation after the loud-to-quiet step");
}

void checkDepthAndPartition(const Stereo& x, int rate) {
    const auto dryOff = render(x, rate, Mode::Off, 6);
    const auto wetOff = render(x, rate, Mode::Off, 6, 0.75f);
    const auto dryDown = render(x, rate, Mode::Downward, 6);
    const auto wetDown = render(x, rate, Mode::Downward, 6, 0.75f);
    require(wetDown == render(x, rate, Mode::Downward, 6, 0.75f, 15, 150, true),
            "dynamic Depth output depends on block partition");

    const auto onset = sample(rate, onsetMs);
    for (const auto& lane : wetDown) {
        for (std::size_t i = 0; i < onset; ++i)
            require(lane[i] == 0, "dynamic Depth produced output before the input onset");
        require(std::abs(lane[onset]) > 1e-4, "dynamic Depth delayed the first output sample");
    }

    double wetEnergy = 0, changeEnergy = 0;
    const auto start = sample(rate, 500), stop = sample(rate, 680);
    for (std::size_t lane = 0; lane < wetDown.size(); ++lane) {
        for (auto i = start; i < stop; ++i) {
            const double wet = static_cast<double>(wetDown[lane][i]) - dryDown[lane][i];
            const double change = wet - (static_cast<double>(wetOff[lane][i]) - dryOff[lane][i]);
            wetEnergy += wet * wet;
            changeEnergy += change * change;
        }
    }
    const auto count = 2 * (stop - start);
    require(std::sqrt(wetEnergy / count) > 0.005, "Depth must remain audible with dynamics engaged");
    require(std::sqrt(changeEnergy / count) > 0.005,
            "dynamics must change the isolated Depth return, not just the direct output");
}
} // namespace

int main() {
    try {
        static_assert(SpatialEq::latencySamples == 0);
        for (const int rate : {44100, 48000, 96000}) {
            const auto x = fixture(rate);
            checkModesAndTiming(x, rate);
            checkDepthAndPartition(x, rate);
            std::cout << "Spatial EQ dynamics checks passed at " << rate << " Hz\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "Spatial EQ dynamics: " << error.what() << '\n';
        return 1;
    }
}
