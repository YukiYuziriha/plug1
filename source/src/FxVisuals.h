#pragma once

#include "PluginProcessor.h"
#include "Multiband.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <utility>

namespace vocalstack::toneui {

inline float visualParameter(Processor& owner, const juce::String& id, float fallback = 0,
                             const ModulationSnapshot* snapshot = nullptr) {
    if (snapshot && snapshot->consistent) {
        for (const auto& target : snapshot->targets) if (target.parameterId == id) {
            if (std::isfinite(target.effectiveNormalized)) {
                if (const auto* parameter = owner.parameters.getParameter(id)) {
                    const float value = parameter->convertFrom0to1(std::clamp(target.effectiveNormalized, 0.0f, 1.0f));
                    if (std::isfinite(value)) return value;
                }
            }
            break;
        }
    }
    if (const auto* p = owner.parameters.getRawParameterValue(id)) {
        const auto v = p->load(std::memory_order_relaxed);
        if (std::isfinite(v)) return v;
    }
    return fallback;
}
inline int effectiveWaveShape(Processor& owner, size_t slot, const ModulationSnapshot* snapshot = nullptr) {
    const int algorithm = std::clamp(int(visualParameter(owner, toneParameterId(slot, "WaveAlgorithm"), 0, snapshot)), 0, 19);
    return algorithm > 0 ? algorithm - 1 : std::clamp(int(visualParameter(owner, toneParameterId(slot, "WaveShape"), 0, snapshot)), 0, 2);
}
inline juce::Colour visualAccent(bool neon) { return juce::Colour(neon ? 0xff64f5e3 : 0xfff5a24b); }
inline juce::Colour crossoverColour(int band) {
    constexpr std::array<juce::uint32, 3> colours{0xfff5a24b, 0xff64d5ba, 0xffa99afa};
    return juce::Colour(colours[size_t(juce::jlimit(0, 2, band))]);
}
inline double visualSampleRate(double rate) {
    return std::isfinite(rate) && rate >= 8000 && rate <= 768000 ? rate : 48000;
}

// GUI-only radix-2 FFT. Hann coherent-gain normalization: a bin-centred,
// full-scale sine in both channels reads 0 dBFS (peak amplitude, not PSD).
// Average channel POWERS, never samples: opposite-polarity stereo cannot cancel.
class FxSpectrum {
public:
    static constexpr size_t size = FxMonitorFrame::sampleCount, bins = size / 2 + 1;
    static_assert((size & (size - 1)) == 0, "FFT requires a power-of-two monitor window");
    FxSpectrum() { clear(); }
    void clear() { db.fill(-96); }
    void decay(float amount) { for (auto& v : db) v = std::max(-96.0f, v - amount); }
    void update(const std::array<std::array<float, size>, 2>& samples) {
        std::array<double, bins> power{};
        for (size_t ch = 0; ch < 2; ++ch) {
            double windowSum = 0;
            for (size_t i = 0; i < size; ++i) {
                const double w = 0.5 - 0.5 * std::cos(6.283185307179586 * double(i) / double(size - 1));
                windowSum += w;
                work[i] = std::complex<double>(std::isfinite(samples[ch][i]) ? samples[ch][i] * w : 0, 0);
            }
            for (size_t i = 1, j = 0; i < size; ++i) {
                size_t bit = size >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap(work[i], work[j]);
            }
            for (size_t length = 2; length <= size; length <<= 1) {
                const auto step = std::polar(1.0, -6.283185307179586 / double(length));
                for (size_t start = 0; start < size; start += length) {
                    std::complex<double> w(1, 0);
                    for (size_t j = 0; j < length / 2; ++j) {
                        const auto a = work[start + j], b = work[start + j + length / 2] * w;
                        work[start + j] = a + b; work[start + j + length / 2] = a - b;
                        w *= step;
                    }
                }
            }
            for (size_t i = 0; i < bins; ++i) {
                const double scale = (i == 0 || i == size / 2 ? 1.0 : 2.0) / windowSum;
                power[i] += 0.5 * std::norm(work[i]) * scale * scale;
            }
        }
        for (size_t i = 0; i < bins; ++i) {
            const float next = float(10 * std::log10(std::max(1.0e-12, power[i])));
            db[i] = std::max(-96.0f, std::max(next, db[i] - 2.0f));
        }
    }
    juce::Path path(juce::Rectangle<float> area, double rate) const {
        juce::Path p;
        const int steps = juce::jlimit(32, 1000, int(area.getWidth()));
        for (int i = 0; i <= steps; ++i) {
            const double hz = 20 * std::pow(1000.0, double(i) / steps);
            // Stop at Nyquist, rather than drawing fictitious high-frequency bins.
            if (hz > rate * 0.5) break;
            const double bin = hz * size / rate;
            const size_t a = std::min(bins - 1, size_t(bin)), b = std::min(bins - 1, a + 1);
            const float level = db[a] + float(bin - double(a)) * (db[b] - db[a]);
            const float x = area.getX() + area.getWidth() * float(i) / float(steps);
            const float y = area.getBottom() - area.getHeight() * (juce::jlimit(-96.0f, 0.0f, level) + 96) / 96;
            if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        return p;
    }
    const std::array<float, bins>& levelsDbFS() const { return db; }
private:
    std::array<float, bins> db{};
    std::array<std::complex<double>, size> work{};
};

// Each visible view retains independently (including the expanded EQ window).
// Polling is message-thread only; no GUI or FFT work reaches the audio callback.
class FxMonitorView : public juce::Component, private juce::Timer {
public:
    FxMonitorView(Processor& processor, size_t fxSlot, bool analyze)
        : monitorOwner(processor), monitorSlot(fxSlot), analyzeSpectrum(analyze) {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        startTimerHz(30);
    }
    ~FxMonitorView() override { stopTimer(); if (retained) monitorOwner.releaseFxMonitor(monitorSlot); }
    void visibilityChanged() override { syncSubscription(); }
    void parentHierarchyChanged() override { syncSubscription(); }
    bool hasLiveFrame() const { return valid && opacity > 0; }
    bool isStreamStalled() const { return valid && opacity < 1; }
    const FxMonitorFrame& monitorFrame() const { return frame; }
    const FxSpectrum& inputSpectrum() const { return input; }
    const FxSpectrum& outputSpectrum() const { return output; }
    void setNeon(bool enabled) { neon = enabled; repaint(); }
protected:
    void paintBackground(juce::Graphics& g) const {
        g.setColour(juce::Colour(neon ? 0xff0d1420 : 0xff1c211f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.setColour(juce::Colour(0xffa8b3bb));
    }
    void paintSpectrum(juce::Graphics& g, juce::Rectangle<float> area, bool withOutput) const {
        if (!hasLiveFrame()) return;
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(area.toNearestInt());
        auto draw = [&](const FxSpectrum& spectrum, juce::Colour colour, float alpha) {
            auto line = spectrum.path(area, visualSampleRate(frame.sampleRate));
            if (line.isEmpty()) return;
            auto fill = line;
            fill.lineTo(line.getCurrentPosition().x, area.getBottom());
            fill.lineTo(area.getX(), area.getBottom()); fill.closeSubPath();
            g.setColour(colour.withAlpha(alpha * opacity * 0.16f)); g.fillPath(fill);
            g.setColour(colour.withAlpha(alpha * opacity)); g.strokePath(line, juce::PathStrokeType(1.0f));
        };
        draw(input, juce::Colour(0xffa8b3bb), 0.45f);
        if (withOutput) draw(output, visualAccent(neon), 0.8f);
    }
    void paintSpectrumScale(juce::Graphics& g, juce::Rectangle<float> area) const {
        g.setColour(juce::Colour(0xff86979e));
        for (int db : {0, -24, -48, -72, -96}) {
            const int y = int(area.getBottom() - area.getHeight() * float(db + 96) / 96);
            g.drawText(juce::String(db), int(area.getRight()) + 3, y - 6, 27, 12, juce::Justification::centredLeft);
        }
    }
    Processor& monitorOwner;
    size_t monitorSlot;
    FxMonitorFrame frame{};
    bool neon = false, valid = false;
    float opacity = 0;
private:
    void clear() { valid = false; opacity = 0; frame = {}; input.clear(); output.clear(); }
    void syncSubscription() {
        const bool wanted = isShowing() && monitorSlot >= 1 && monitorSlot <= 15;
        if (wanted == retained) return;
        if (wanted) monitorOwner.retainFxMonitor(monitorSlot);
        else monitorOwner.releaseFxMonitor(monitorSlot);
        retained = wanted; clear(); repaint();
    }
    void timerCallback() override {
        syncSubscription();
        if (!retained) return;
        const double now = juce::Time::getMillisecondCounterHiRes();
        FxMonitorFrame next;
        if (!monitorOwner.readFxMonitor(monitorSlot, next)) { clear(); repaint(); return; }
        if (!valid || next.sequence != frame.sequence || next.nodeId != frame.nodeId || next.sampleRate != frame.sampleRate) {
            if (!valid || next.epoch != frame.epoch || next.nodeId != frame.nodeId || next.sampleRate != frame.sampleRate) { input.clear(); output.clear(); }
            frame = next; valid = true; opacity = 1; lastFrameMs = now;
            if (analyzeSpectrum) { input.update(frame.input); output.update(frame.output); }
        } else if (now - lastFrameMs > 200) {
            const float elapsed = float(std::min(250.0, now - lastTickMs));
            opacity = std::max(0.0f, opacity - elapsed / 650);
            input.decay(elapsed * 0.06f); output.decay(elapsed * 0.06f);
        }
        lastTickMs = now; repaint();
    }
    FxSpectrum input, output;
    bool retained = false, analyzeSpectrum;
    double lastFrameMs = 0, lastTickMs = 0;
};

class WaveshaperVisual final : public FxMonitorView, public juce::SettableTooltipClient {
public:
    WaveshaperVisual(Processor& owner, size_t slot) : FxMonitorView(owner, slot, false) {
        setName("Waveshaper transfer and stereo waveforms");
        setInterceptsMouseClicks(true, false);
        setTooltip("Static memoryless transfer uses effective modulated parameter snapshots for drive, shape, shape amount, output, mix and bypass. Raw uses this curve at project rate regardless of Quality. Off/2x/4x use ADAA, with IIR oversampling at 2x/4x. All have zero explicit integer delay, not zero phase; parallel paths may comb. ADAA, resampling, DC removal and optional RMS level matching are dynamic and are not simulated here. Below: actual pre/post L and R, fixed amplitude scale; no stereo summing.");
    }
    // Snapshot memoryless curve, not an ADAA/DC simulation or live DSP state.
    static double settledTransfer(double x, double driveDb, double outputDb, double mix, int shape, bool enabled,
                                  double shapeAmount = 0.5) {
        if (!enabled) return x;
        const double driven = x * std::pow(10.0, driveDb / 20);
        const double wet = Waveshaper::transfer(driven, shape, shapeAmount);
        return x * (1 - mix) + wet * std::pow(10.0, outputDb / 20) * mix;
    }
    void paint(juce::Graphics& g) override {
        paintBackground(g);
        auto next = monitorOwner.getModulationSnapshot();
        // Match knob feedback: an overlapping publication must not flash base values.
        if (next.consistent) modulationSnapshot = std::move(next);
        const auto parameter = [&](const juce::String& id, float fallback = 0) {
            return visualParameter(monitorOwner, id, fallback, &modulationSnapshot);
        };
        const bool enabled = parameter(fxParameterId(monitorSlot, "On"), 1) >= 0.5f;
        auto bounds = getLocalBounds().reduced(7);
        const bool raw = parameter(toneParameterId(monitorSlot, "WaveRaw")) >= 0.5f;
        const int quality = std::clamp(int(parameter(toneParameterId(monitorSlot, "WaveQuality"))), 0, 2);
        const bool matched = parameter(toneParameterId(monitorSlot, "WaveLevelMatch")) >= 0.5f;
                const juce::String mode = (raw ? juce::String("Raw @ 1x") : juce::String("ADAA / ") + Waveshaper::qualityNames[size_t(quality)])
                    + (matched ? " | pre-match curve" : "");
        g.drawText(enabled ? "STATIC | " + mode
                           : juce::String("BYPASS | unity transfer"), bounds.removeFromTop(16), juce::Justification::centredLeft);
        auto curve = bounds.removeFromTop(91).toFloat().withTrimmedLeft(24).withTrimmedRight(7);
        const double drive = parameter(toneParameterId(monitorSlot, "WaveDrive"));
        const double outputDb = parameter(toneParameterId(monitorSlot, "WaveOutput"));
        const double mix = juce::jlimit(0.0, 1.0, double(parameter(fxParameterId(monitorSlot, "Mix"), 100)) / 100);
        const int shape = effectiveWaveShape(monitorOwner, monitorSlot, &modulationSnapshot);
        const double shapeAmount = parameter(toneParameterId(monitorSlot, "WaveShapeAmount"), 50) * 0.01;
        auto yFor = [&](double y) { return curve.getCentreY() - float(juce::jlimit(-1.0, 1.0, y)) * curve.getHeight() * 0.5f; };
        g.setColour(juce::Colour(0xff819398).withAlpha(0.25f));
        g.drawHorizontalLine(int(curve.getCentreY()), curve.getX(), curve.getRight());
        g.drawVerticalLine(int(curve.getCentreX()), curve.getY(), curve.getBottom());
        g.drawLine(curve.getX(), curve.getBottom(), curve.getRight(), curve.getY(), 1);
        juce::Path p;
        bool outside = false;
        for (int i = 0; i <= 256; ++i) {
            const double x = double(i) / 128 - 1;
            const double y = settledTransfer(x, drive, outputDb, mix, shape, enabled, shapeAmount);
            outside |= std::abs(y) > 1;
            const float px = curve.getX() + curve.getWidth() * float(i) / 256;
            if (i == 0) p.startNewSubPath(px, yFor(y)); else p.lineTo(px, yFor(y));
        }
        g.setColour(visualAccent(neon)); g.strokePath(p, juce::PathStrokeType(1.8f));
        g.setColour(juce::Colour(0xffa8b3bb));
        g.drawText("+1", 2, int(curve.getY()), 24, 12, juce::Justification::centredLeft);
        g.drawText("-1", 2, int(curve.getBottom()) - 12, 24, 12, juce::Justification::centredLeft);
        g.drawText(outside ? "Input -1 ... +1 | output outside +/-1 off scale" : "Input -1 ... +1 | Output -1 ... +1", bounds.removeFromTop(16), juce::Justification::centred);
        const auto ms = 1000.0 * FxMonitorFrame::sampleCount
                    / visualSampleRate(hasLiveFrame() ? frame.sampleRate : monitorOwner.getSampleRate());
        g.drawText("PRE dim / POST bright | L / R | " + juce::String(ms, 1) + " ms", bounds.removeFromTop(16), juce::Justification::centredLeft);
        for (size_t ch = 0; ch < 2; ++ch) {
            auto lane = bounds.removeFromTop(29).toFloat().withTrimmedLeft(17);
            g.setColour(juce::Colour(0xffa8b3bb));
            g.drawText(ch == 0 ? "L" : "R", 7, int(lane.getY()), 14, 26, juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff819398).withAlpha(0.2f));
            g.drawHorizontalLine(int(lane.getCentreY()), lane.getX(), lane.getRight());
            if (!hasLiveFrame()) continue;
            auto draw = [&](const auto& samples, juce::Colour colour) {
                // Pixel min/max envelopes preserve captured sample peaks, not fake motion.
                g.setColour(colour.withAlpha(colour.getFloatAlpha() * opacity));
                const int pixels = std::max(1, int(lane.getWidth()));
                for (int x = 0; x < pixels; ++x) {
                    const size_t begin = size_t(x) * FxMonitorFrame::sampleCount / size_t(pixels);
                    const size_t end = std::min(FxMonitorFrame::sampleCount, std::max(begin + 1, size_t(x + 1) * FxMonitorFrame::sampleCount / size_t(pixels)));
                    float lo = std::isfinite(samples[begin]) ? samples[begin] : 0, hi = lo;
                    for (size_t i = begin; i < end; ++i) if (std::isfinite(samples[i])) { lo = std::min(lo, samples[i]); hi = std::max(hi, samples[i]); }
                    const float a = lane.getCentreY() - juce::jlimit(-1.0f, 1.0f, hi) * 12;
                    const float b = lane.getCentreY() - juce::jlimit(-1.0f, 1.0f, lo) * 12;
                    g.drawVerticalLine(int(lane.getX()) + x, a, std::max(a + 0.7f, b));
                }
            };
            draw(frame.input[ch], juce::Colour(0xffa8b3bb).withAlpha(0.4f));
            draw(frame.output[ch], visualAccent(neon).withAlpha(0.85f));
        }
        g.setColour(juce::Colour(0xffa8b3bb));
        g.drawText(isStreamStalled() ? "No new frames | fading last L/R capture"
                               : hasLiveFrame() ? "Waveforms: +/-1 FS (over-range off scale)" : "No live audio | waveform scale +/-1 FS", bounds.removeFromTop(16), juce::Justification::centredLeft);
    }
private:
    ModulationSnapshot modulationSnapshot;
};

class CrossoverVisual final : public FxMonitorView, public juce::SettableTooltipClient {
public:
    CrossoverVisual(Processor& owner, size_t slot) : FxMonitorView(owner, slot, true) {
        setName("LR4 crossover response spectrum and band RMS");
        setInterceptsMouseClicks(true, false);
        setTooltip("Settled LR4 branch responses; three-band Low includes high-split all-pass alignment. Input FFT uses stereo power, right dBFS scale. Meters are captured stereo RMS per output, not estimated from the spectrum. Bypass exposes one full-band output.");
    }
    static std::complex<double> splitResponse(double hz, double cutoff, double rate, bool high) {
        const double w = 6.283185307179586 * cutoff / rate;
        const double c = std::cos(w), alpha = std::sin(w) / std::sqrt(2.0), a0 = 1 + alpha;
        const double b0 = (1 + (high ? c : -c)) / (2 * a0);
        const auto z = std::polar(1.0, -6.283185307179586 * hz / rate);
        const auto h = (b0 + (high ? -2 : 2) * b0 * z + b0 * z * z)
            / (1.0 - 2 * c / a0 * z + (1 - alpha) / a0 * z * z);
        return h * h;
    }
    static std::array<double, 3> responseMagnitudes(double hz, double rate, double low, double high, int count) {
        rate = visualSampleRate(rate);
        low = Multiband::cutoff(low, rate, 250);
        high = std::max(low, Multiband::cutoff(high, rate, 4000));
        const auto l = splitResponse(hz, low, rate, false), h = splitResponse(hz, low, rate, true);
        if (count == 2) return {std::abs(l), std::abs(h), 0};
        const auto ul = splitResponse(hz, high, rate, false), uh = splitResponse(hz, high, rate, true);
        return {std::abs(l * (ul + uh)), std::abs(h * ul), std::abs(h * uh)};
    }
    void paint(juce::Graphics& g) override {
        paintBackground(g);
        auto bounds = getLocalBounds().reduced(7);
        const bool enabled = visualParameter(monitorOwner, fxParameterId(monitorSlot, "On"), 1) >= 0.5f;
        const int count = visualParameter(monitorOwner, toneParameterId(monitorSlot, "BandCount"), 3) < 2.5f ? 2 : 3;
        const double rate = visualSampleRate(hasLiveFrame() ? frame.sampleRate : monitorOwner.getSampleRate());
        const double low = visualParameter(monitorOwner, toneParameterId(monitorSlot, "CrossoverLow"), 250);
        const double high = visualParameter(monitorOwner, toneParameterId(monitorSlot, "CrossoverHigh"), 4000);
        g.drawText(enabled ? "LR4 | settled branch response" : "BYPASS | one full-band output", bounds.removeFromTop(16), juce::Justification::centredLeft);
        auto axes = bounds.removeFromTop(14);
        g.drawText("Response dB", axes.removeFromLeft(100), juce::Justification::centredLeft);
        g.drawText("Input FFT dBFS", axes, juce::Justification::centredRight);
        auto area = bounds.removeFromTop(106).toFloat().withTrimmedLeft(27).withTrimmedRight(30);
        paintSpectrum(g, area, false); paintSpectrumScale(g, area);
        for (int db : {0, -12, -24, -36, -48}) {
            const auto y = area.getY() - float(db) * area.getHeight() / 48;
            g.setColour(juce::Colour(0xff819398).withAlpha(0.17f)); g.drawHorizontalLine(int(y), area.getX(), area.getRight());
            g.setColour(juce::Colour(0xffa8b3bb)); g.drawText(juce::String(db), 0, int(y) - 6, 30, 12, juce::Justification::centredRight);
        }
        for (float hz : {100.0f, 1000.0f, 10000.0f}) {
            const float x = area.getX() + area.getWidth() * std::log(hz / 20) / std::log(1000.0f);
            g.setColour(juce::Colour(0xff819398).withAlpha(0.17f)); g.drawVerticalLine(int(x), area.getY(), area.getBottom());
            g.setColour(juce::Colour(0xffa8b3bb)); g.drawText(hz < 1000 ? "100" : hz < 10000 ? "1k" : "10k", int(x) - 15, int(area.getBottom()), 30, 13, juce::Justification::centred);
        }
        for (int band = 0; band < (enabled ? count : 1); ++band) {
            juce::Path p;
            for (int i = 0; i <= 300; ++i) {
                const double hz = 20 * std::pow(1000.0, double(i) / 300);
                if (hz >= rate * 0.5) break;
                const auto magnitudes = responseMagnitudes(hz, rate, low, high, count);
                const double db = enabled ? 20 * std::log10(std::max(1.0e-12, magnitudes[size_t(band)])) : 0;
                const float x = area.getX() + area.getWidth() * float(i) / 300;
                const float y = area.getY() - float(juce::jlimit(-48.0, 0.0, db)) * area.getHeight() / 48;
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            g.setColour(enabled ? crossoverColour(band) : juce::Colour(0xffa8b3bb));
            g.strokePath(p, juce::PathStrokeType(1.8f));
        }
        bounds.removeFromTop(17);
        g.setColour(juce::Colour(0xffa8b3bb));
        g.drawText("Actual output stereo RMS | -60 ... 0 dBFS", bounds.removeFromTop(16), juce::Justification::centredLeft);
        const int meterCount = enabled ? count : 1;
        for (int band = 0; band < meterCount; ++band) {
            auto row = bounds.removeFromTop(20);
            const auto label = !enabled ? "Full" : band == 0 ? "Low" : band == count - 1 ? "High" : "Mid";
            g.setColour(enabled ? crossoverColour(band) : juce::Colour(0xffa8b3bb));
            g.drawText(label, row.removeFromLeft(35), juce::Justification::centredLeft);
            const bool available = hasLiveFrame() && frame.bandCount == meterCount;
            const float rms = available ? frame.bandRms[size_t(band)] * opacity : 0;
            const float db = std::isfinite(rms) && rms > 0 ? 20 * std::log10(rms) : -100;
            const float measuredDb = available && frame.bandRms[size_t(band)] > 0
                ? 20 * std::log10(frame.bandRms[size_t(band)]) : -100;
            g.drawText(available && !isStreamStalled() ? (measuredDb < -99 ? "-inf" : juce::String(measuredDb, 1)) : "--", row.removeFromRight(42), juce::Justification::centredRight);
            auto bar = row.reduced(2, 6).toFloat();
            g.setColour(juce::Colour(0xff819398).withAlpha(0.15f)); g.fillRect(bar);
            g.setColour(db > 0 ? juce::Colour(0xffee8b82) : enabled ? crossoverColour(band) : juce::Colour(0xffa8b3bb));
            g.fillRect(bar.withWidth(bar.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60) / 60)));
        }
        g.setColour(juce::Colour(0xffa8b3bb));
        g.drawText(isStreamStalled() ? "No new frames | fading last capture"
                               : hasLiveFrame() ? "Input FFT: Hann / stereo power / peak dBFS" : "No live audio | requested response only", bounds.removeFromTop(16), juce::Justification::centredLeft);
    }
};

} // namespace vocalstack::toneui
