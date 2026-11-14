#pragma once

#include "../src/ReactiveMaterial.h"
#include "../src/CreativeEffects.h"
#include <limits>
#include <stdexcept>

namespace vocalstack::compressorHistoryTests {
inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// Fixture deliberately does not claim production telemetry exists.
struct Frame {
    std::array<float, 200> gainReductionHistory{};
    size_t gainReductionHistoryUsed = 0, gainReductionHistoryBinSamples = 960;
    float gainReductionDb = 0;
    bool hasGainReduction = true;
    double sampleRate = 48000;
    uint64_t sequence = 1, epoch = 1;
    uint32_t nodeId = 7;
};
inline void measuredMappingAndScale() {
    using reactiveVisual::GainReductionField;
    using reactiveVisual::HistoryField;
    Frame frame;
    frame.gainReductionHistoryUsed = 2;
    frame.gainReductionHistory[198] = 12;
    frame.gainReductionHistory[199] = 48;
    frame.gainReductionDb = 48;
    const auto field = GainReductionField::from(frame, true);
    require(field.available && field.db[197] < 0 && field.db[198] == 12 && field.db[199] == 48,
            "GR history must preserve right alignment, dB units and unclipped measurements");
    require(field.currentDb == 48, "Numeric GR must not clip at the graph ceiling");
    const juce::Rectangle<float> plot(0, 10, 200, 90);
    require(HistoryField::yFor(0, plot) == 10 && HistoryField::yFor(36, plot) == 100
            && HistoryField::yFor(12, plot) == 40 && HistoryField::yFor(48, plot) == 100,
            "GR must grow downward on a fixed 0..36 dB scale");
    require(!GainReductionField::from(frame, false).available, "Bypass/stale data retained GR");
    frame.hasGainReduction = false;
    require(!GainReductionField::from(frame, true).available, "Invalid producer accepted");
    frame.hasGainReduction = true;
    frame.gainReductionHistory[199] = std::numeric_limits<float>::quiet_NaN();
    require(GainReductionField::from(frame, true).db[199] < 0, "Nonfinite GR became measured unity");
    frame.gainReductionHistoryBinSamples = 1920;
    require(!GainReductionField::from(frame, true).available, "Wrong timebase labelled as four seconds");
    for (double rate : {44100., 48000., 96000.}) {
        frame.sampleRate = rate;
        frame.gainReductionHistoryBinSamples = size_t(std::round(rate * .020));
        require(GainReductionField::from(frame, true).available, "Supported sample-rate timebase rejected");
    }
}
inline bool samePixels(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds()) return false;
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
        if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}
// Pure offscreen composition; no native-window/second-monitor validation claim.
inline void sharedPixelsAndCache() {
    using namespace reactiveVisual;
    HistoryField compressor, gate;
    Frame frame; frame.gainReductionHistoryUsed = 200;
    for (size_t i = 0; i < 200; ++i) frame.gainReductionHistory[i] = float(i % 37);
    auto field = GainReductionField::from(frame, true);
    const HistoryField::Colours colours{juce::Colour(0xff141a20), juce::Colour(0xff384650),
                                        juce::Colour(0xff54d6c1), juce::Colour(0xff81909a)};
    const auto render = [&](HistoryField& renderer, const GainReductionField& data, int width = 420) {
        juce::Image image(juce::Image::RGB, width, 100, true); juce::Graphics g(image);
        renderer.paint(g, {0, 0, float(width), 100}, data, colours); return image;
    };
    const auto first = render(compressor, field);
    require(samePixels(first, render(gate, field)), "Shared Compressor/Gate renderer differs in pixels");
    require(samePixels(first, render(compressor, field)) && compressor.geometryBuildCount() == 1,
            "Repeated paint rebuilt geometry or invented history motion");
    field.sequence++;
    render(compressor, field);
    require(compressor.geometryBuildCount() == 2, "Telemetry revision did not invalidate cache");
    render(compressor, field, 300);
    require(compressor.geometryBuildCount() == 3, "Resize did not invalidate cache");
    field.epoch++; render(compressor, field);
    require(compressor.geometryBuildCount() == 4, "Reset epoch did not invalidate cache");
    const auto empty = render(compressor, {});
    require(!samePixels(first, empty), "Cleared GR lane retained measured attenuation");
}
inline void actualDspSnapshotBridge() {
    using reactiveVisual::GainReductionField;
    require(GainReductionField::HasHistory<FxMonitorFrame>::value,
            "INTEGRATION REQUIRED: FxMonitorFrame has no measured GR history contract");
    CreativeSettings settings;
    using P = CreativeParameter;
    settings[P::compressorThreshold] = -24;
    settings[P::compressorRatio] = 4;
    settings[P::compressorMakeup] = 0;
    CreativeEffect fx; fx.prepare(48000, CreativeKind::compressor);
    FxMonitor monitor; monitor.reset(48000); monitor.retain(0);
    float peak = 0;
    for (int i = 0; i < 48000; ++i) {
        const float input = .5f * std::sin(float(i) * .1308996939f);
        const auto output = fx.process(input, -input, settings);
        const float measured = fx.gainReductionDb();
        peak = std::max(peak, measured);
        monitor.beginSample(); monitor.setGainReduction(0, measured);
        monitor.capture(0, 7, input, -input, output[0], output[1], 0, {}, true, output[0], output[1]);
    }
    FxMonitorFrame snapshot;
    require(monitor.read(0, snapshot), "No actual DSP monitor snapshot");
    const auto field = GainReductionField::from(snapshot, true);
    require(field.available && field.currentDb > 1, "Actual DSP GR did not reach snapshot consumer");
    float historyPeak = 0;
    for (float db : field.db) historyPeak = std::max(historyPeak, db);
    require(historyPeak > 1 && historyPeak <= peak + .001f,
            "Snapshot history missing actual gain reduction or exceeds measured DSP peak");
    monitor.reset(48000);
    require(!monitor.read(0, snapshot), "Reset exposed old measured GR");
    monitor.release(0);
}
inline void run() {
    measuredMappingAndScale(); sharedPixelsAndCache(); actualDspSnapshotBridge();
}
} // namespace vocalstack::compressorHistoryTests
