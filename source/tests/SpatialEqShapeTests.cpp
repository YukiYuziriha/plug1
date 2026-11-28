#include "SpatialEq.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Standalone (link with src/SpatialEq.cpp); no JUCE or installed VST3 needed.
namespace {
using namespace vocalstack;
using Shape = SpatialEqShape;
constexpr std::size_t onset = 1024;
constexpr std::array slopes{6, 12, 18, 24, 36, 48, 72, 96};
constexpr std::array shapes{Shape::LowCut, Shape::HighCut, Shape::Notch, Shape::BandPass};
using Stereo = std::array<std::array<float, 4096>, 2>;

void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

void close(float actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}

Stereo impulse(bool second = false) {
    Stereo x{};
    x[0][onset] = 0.125f;
    x[1][onset] = -0.0625f;
    if (second) {
        x[0][1391] = -0.03125f;
        x[1][1391] = 0.0625f;
    }
    return x;
}

Stereo render(SpatialEqBandSettings band, double rate = 48000, bool withBoost = false,
              bool chunked = false) {
    SpatialEqSettings settings;
    settings.bands[3] = band;
    if (withBoost) settings.bands[1] = {true, true, Shape::Bell, 2400, 6, 1, 1};
    SpatialEq eq;
    eq.prepare(rate);
    eq.setSettings(settings);
    auto x = impulse(withBoost);
    constexpr std::array<std::size_t, 6> blocks{1, 31, 257, 17, 64, 3};
    for (std::size_t pos = 0, i = 0; pos < x[0].size(); ++i) {
        const auto remaining = x[0].size() - pos;
        const auto count = chunked ? std::min(remaining, blocks[i % blocks.size()]) : remaining;
        eq.process(x[0].data() + pos, x[1].data() + pos, count);
        pos += count;
    }
    return x;
}

void nativeDirectFixtures() {
    // First two normalized impulse samples captured from Stereo/Zero Latency
    // Spatial EQ VST3 0.7.0 at 48 kHz, 2.4 kHz, Q=1, Depth=0.
    constexpr double taps[][8][2]{
        {{0.86090750, -0.23209837}, {0.80084109, -0.35057288}, {0.73964596, -0.44246000},
         {0.67336357, -0.52899277}, {0.55417144, -0.65035087}, {0.45978042, -0.71043205},
         {0.31289893, -0.72307795}, {0.21277061, -0.65493864}},
        {{0.22088942, 0.21004613}, {0.05672397, 0.11211804}, {0.01330427, 0.04008494},
         {0.00332817, 0.01335688}, {0.00019370, 0.00116925}, {1.12528205e-5, 9.06555506e-5},
         {3.79109473e-8, 4.58440240e-7}, {1.27611699e-10, 2.05802131e-9}},
        {{0.86092728, -0.22042115}},
        {{0.21897295, 0.19083476}}
    };
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        SpatialEqBandSettings band{true, true, shapes[i], 2400, 0, 1, 0};
        for (std::size_t j = 0; j < (i < 2 ? slopes.size() : 1); ++j) {
            band.slopeDbPerOctave = slopes[j];
            const auto y = render(band);
            for (std::size_t k = 0; k < 2; ++k)
                close(y[0][onset + k] * 8, taps[i][j][k],
                      5e-5 * std::abs(taps[i][j][k]) + 2e-14, "native direct fixture");
        }
    }
    const auto fallback = render({true, true, Shape::HighCut, 2400, 0, 0.1f, 0, 12});
    close(fallback[0][onset] * 8, 0.009615295567, 2e-8, "native High Cut bilinear fallback");
    for (auto shape : {Shape::LowCut, Shape::HighCut}) {
        SpatialEqBandSettings band{true, true, shape, 2400, 0, 1, 0, 6};
        const auto firstOrder = render(band);
        band.q = 30;
        require(render(band) == firstOrder, "6 dB/oct must ignore Q");
        band.slopeDbPerOctave = 24;
        const auto resonant = render(band);
        band.q = 1;
        require(render(band) != resonant, "cut pole pairs must respond to Q");
        const auto defaultSlope = render(band);
        band.slopeDbPerOctave = 30;
        require(render(band) == defaultSlope, "unsupported slope must default to 24 dB/oct");
    }
}

void spatialBehavior() {
    const auto dry = impulse();
    for (auto shape : shapes) {
        for (double rate : {44100., 48000., 96000.}) {
            SpatialEqBandSettings band{true, true, shape, 2400, 0, 2, 0};
            const auto off = render(band, rate);
            band.gainDb = 30;
            if (shape == Shape::Notch || shape == Shape::BandPass) band.slopeDbPerOctave = 96;
            require(render(band, rate, false, true) == off, "gain or unused slope changed the filter");
            band.depth = 1;
            const auto full = render(band, rate, false, true);
            band.enabled = false;
            require(render(band, rate) == dry, "disabled filter changed dry stereo");
            for (std::size_t i = 0; i < dry[0].size(); ++i) {
                close(full[0][i] + full[1][i], dry[0][i] + dry[1][i], 2e-8, "Depth must restore Mid");
                close(full[0][i] - full[1][i], off[0][i] - off[1][i], 2e-8, "Depth must preserve Side");
            }
        }
    }

    struct Fixture {
        SpatialEqBandSettings band;
        double rate;
        std::size_t sample;
        double left, right;
    };
    // Native mixed-band renders, with a +6 dB / 100% Depth bell in slot 2.
    constexpr Fixture wet[]{
        {{true, true, Shape::LowCut, 800, 18, 0.5f, 0.5f, 18}, 44100, 1292, -0.002121356316, -0.0002497122041},
        {{true, true, Shape::HighCut, 2400, 18, 2, 1, 72}, 96000, 1166, 0.006252000574, -0.006253981497},
        {{true, true, Shape::Notch, 2400, 18, 1, 1}, 48000, 1311, 0.001554131042, 0.0002077542595},
        {{true, true, Shape::BandPass, 2400, 18, 4, 0.5f}, 48000, 1360, -0.0009339607204, 0.0002839553636},
    };
    for (const auto& f : wet) {
        const auto y = render(f.band, f.rate, true, true);
        close(y[0][f.sample], f.left, 2e-8, "native mixed-band left return");
        close(y[1][f.sample], f.right, 2e-8, "native mixed-band right return");
    }
}

void topologyReset() {
    SpatialEqSettings settings;
    auto& band = settings.bands[3];
    band = {true, true, Shape::LowCut, 2400, 0, 4, 0.5f, 96};
    SpatialEq eq;
    eq.setSettings(settings);
    eq.prepare(48000);
    auto excite = impulse();
    eq.process(excite[0].data() + onset, excite[1].data() + onset, 64);

    for (int i = 0; i < 2; ++i) {
        if (i == 0) band.slopeDbPerOctave = 6;
        else band.shape = Shape::BandPass;
        eq.setSettings(settings);
        float l = 0, r = 0;
        eq.process(l, r);
        require(l == 0 && r == 0, "topology change must clear the old filter history at the tick");
        l = 0.125f;
        r = -0.0625f;
        eq.process(l, r);
        require(std::isfinite(l) && std::isfinite(r) && l != 0 && r != 0, "new filter topology must respond");
        for (int j = 0; j < SpatialEq::coefficientInterval - 2; ++j) {
            l = r = 0;
            eq.process(l, r);
        }
    }
    eq.reset();
    float l = 0, r = 0;
    eq.process(l, r);
    require(l == 0 && r == 0, "reset must clear a previously live shape");
}
} // namespace

int main() {
    try {
        nativeDirectFixtures();
        spatialBehavior();
        topologyReset();
        std::cout << "Spatial EQ shape checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
