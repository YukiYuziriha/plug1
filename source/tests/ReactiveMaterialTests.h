#pragma once

#include "../src/ReactiveMaterial.h"
#include <limits>
#include <stdexcept>
#include <iostream>

namespace vocalstack::reactiveMaterialTests {

inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
inline bool samePixels(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
        if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}

inline void model() {
    using reactiveVisual::WaveformField;
    require(WaveformField::amplitude(0) == 0 && WaveformField::amplitude(.001f) == 0, "Field silence floor");
    require(std::abs(WaveformField::amplitude(.01f) - 1.f / 3) < .00001f, "Field fixed dB scale");
    require(WaveformField::amplitude(-1) == 1 && WaveformField::amplitude(2) == 1, "Field signed magnitude/headroom clamp");
    require(WaveformField::amplitude(std::numeric_limits<float>::infinity()) == 0, "Field finite guard");
    FxMonitorFrame frame;
    frame.hasWet = true; frame.historyUsed = 1;
    frame.history.back() = {-.01f, .1f, -.1f, 1.f, .02f, .2f};
    const auto field = WaveformField::from(frame, true);
    for (size_t x = 0; x + 1 < WaveformField::columns; ++x)
        require(field.history[x].input.positive == 0 && field.history[x].wet.positive == 0, "Field newest must be right, unused history empty");
    require(field.history.back().wet.positive == 1, "Field lost newest peak");
    require(field.history.back().wet.negative > field.history.back().input.negative, "Field lost signed/input-wet distinction");
    require(field.history.back().wet.rms > 0 && field.history.back().wet.rms <= field.history.back().wet.positive, "Field RMS core");
    const auto cleared = WaveformField::from(frame, false);
    require(cleared.history.back().wet.positive == 0, "Inactive field retained audio");
    frame.historyUsed = FxMonitorFrame::historyCount;
    frame.history = {}; frame.history.front().wetMin = -1;
    require(WaveformField::from(frame, true).history.front().wet.negative == 1, "Oldest transient lost in reduction");
    frame.history = {};
    frame.history.back().wetMax = std::numeric_limits<float>::quiet_NaN();
    require(WaveformField::from(frame, true).history.back().wet.positive == 0, "Nonfinite history leaked");
    frame.hasWet = false; frame.history.back().wetMax = 1;
    require(WaveformField::from(frame, true).history.back().wet.positive == 0, "Invalid wet payload accepted");
}

// Pure offscreen images. No native window, timer, processor or physical-display claim.
inline void composition() {
    using namespace reactiveVisual;
    static_assert(ReactiveMaterial::width == 48 && ReactiveMaterial::height == 24);
        static_assert(WaveformField::columns == 128 && SpectrumField::columns == 128);
        require(ReactiveMaterial::displayColumns(480) == 320, "Display geometry must target 1.5px, independent of decorative raster");
        require(ReactiveMaterial::displayColumns(100000) == 1024, "Display work must remain bounded");
    ReactiveMaterial material;
    const ReactiveMaterial::Colours colours{juce::Colour(0xff141a20), juce::Colour(0xffefbd68),
                                            juce::Colour(0xff54d6c1), juce::Colour(0xff81909a)};
    const juce::Rectangle<float> bounds(0, 0, 480, 240), plot(30, 40, 420, 160);
    const auto render = [&](const WaveformField& field) {
        juce::Image image(juce::Image::RGB, 480, 240, true);
        juce::Graphics g(image);
        material.paint(g, bounds, plot, field, colours, 0); // Frozen decoration.
        return image;
    };
    const auto silent = render({});
    WaveformField input, wet;
    for (auto& c : input.history) c.input = {.3f, .6f, .2f};
    for (auto& c : wet.history) c.wet = {.3f, .6f, .2f};
    const auto inputImage = render(input), wetImage = render(wet);
    require(!samePixels(silent, inputImage) && !samePixels(inputImage, wetImage), "Measured input/wet material distinction");
    require(samePixels(silent, render({})), "Material retained/invented audio after clearing");
    require(samePixels(wetImage, render(wet)), "Frozen material has forced movement");
    require(wetImage.getPixelAt(5, 5) == silent.getPixelAt(5, 5), "Audio field covered header/background outside plot");
    double inputChange = 0, wetChange = 0;
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 480; ++x) {
        const auto distance = [](juce::Colour a, juce::Colour b) {
            return std::abs(int(a.getRed()) - int(b.getRed())) + std::abs(int(a.getGreen()) - int(b.getGreen()))
                 + std::abs(int(a.getBlue()) - int(b.getBlue()));
        };
        inputChange += distance(inputImage.getPixelAt(x, y), silent.getPixelAt(x, y));
        wetChange += distance(wetImage.getPixelAt(x, y), silent.getPixelAt(x, y));
    }
    require(wetChange > inputChange, "Wet field must be stronger than faint input");
    SpectrumField spectrum;
    spectrum.set(0, 127, 1); spectrum.set(1, 127, .1f); spectrum.set(2, 127, .01f);
    require(spectrum.levels[0][127] > spectrum.levels[1][127] && spectrum.levels[1][127] > spectrum.levels[2][127], "Three-spectrum fixed scale");
    spectrum.set(3, 999, 1); // Out-of-range inputs must be bounded no-ops.
    const std::array<juce::Colour, 3> spectrumColours{colours.input, colours.baseline, colours.output};
    const auto renderSpectrum = [&](const SpectrumField& value) {
        juce::Image image(juce::Image::RGB, 480, 240, true); juce::Graphics g(image);
        material.paintSpectrum(g, bounds, value, colours.base, spectrumColours, 0);
        return image;
    };
    const auto emptySpectrum = renderSpectrum({});
    require(!samePixels(emptySpectrum, renderSpectrum(spectrum)), "Spectrum composition missing measured levels");
    require(samePixels(emptySpectrum, renderSpectrum({})), "Spectrum did not clear");
        const double waveStart = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 100; ++i) render(wet);
        const double waveMs = (juce::Time::getMillisecondCounterHiRes() - waveStart) / 100;
        const double spectrumStart = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 100; ++i) renderSpectrum(spectrum);
        const double spectrumMs = (juce::Time::getMillisecondCounterHiRes() - spectrumStart) / 100;
        std::cout << "REACTIVE offscreen 480x240 average ms: waveform=" << waveMs << " spectra=" << spectrumMs
            << "; includes image allocation/compositing, not audio or desktop FPS\n";
}
inline void run() { model(); composition(); }

} // namespace vocalstack::reactiveMaterialTests
