#include "VisualTheme.h"
#pragma once

#include "CreativeEffects.h"
#include "FxVisuals.h"
#include "ReactiveMaterial.h"
#include "CompressorPlot.h"

namespace vocalstack {

// Message-thread parameter models. Granular geometry is independent of audio state.
class CreativeEffectVisual final : public juce::Component, public juce::SettableTooltipClient {
public:
    CreativeEffectVisual(Processor& processor, size_t fxSlot, CreativeKind effectKind)
        : owner(processor), slot(fxSlot), kind(effectKind) {
        setName(kind == CreativeKind::granular ? "Grain parameter envelope graph" : "Effect parameter model");
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTooltip(kind == CreativeKind::granular
                    ? grainDescription()
                    : "Parameter-derived schematic using coherent effective modulation targets. Unassigned parameters are host base reads; DSP smoothing and signal-dependent behaviour are not simulated. Reverb is not a measured impulse response. Compressor GR is measured by the DSP.");
        if (kind == CreativeKind::vocalGlitch) {
            setName("Vocal Glitch playback guide");
            setTooltip("Effective Pattern and Grid targets, not live capture, playback or transport status.");
        }
        if (kind == CreativeKind::compressor) {
            setName("Compressor shared transfer and measured history plot");
            setTooltip("One plot, independent axes: solid Curve uses Input/Output -72..0 dBFS. In (faint) and Wet (strong) show measured stereo RMS before Mix, using the bottom History time scale and left dBFS scale. PCM retains 2.56 s, right-aligned in 4 s; older space is empty. Dashed GR is actual DSP gain reduction before makeup/Mix, peak per 20 ms bin, four seconds, on the right 0..36 dB downward scale. Curve is the parameter-derived static transfer including Mix/makeup, not measured output; attack/release and detector behaviour are not simulated. Off-scale values are clipped; the numeric GR meter is not. Stopped/bypassed/reset telemetry clears.");
        }
        if (factoryReactive())
            setTooltip("Background: actual stereo min/max and RMS in 20 ms bins, oldest left, newest right, 2.56 s; logarithmic amplitude (-60 to 0 dBFS), not the foreground model axes. Faint amber is measured input; stronger teal is processed wet before slot Mix (including the actual tail), sharing a fixed zero baseline. Motion does not pause audio data. Bypass, reset and stopped processing clear the field. Thin foreground: parameter model only, not measured IR or sample-wise transfer. Convolution uses the generated 48 kHz L IR envelope with illustrative Tail taper; Saturator transfer excludes Tone/Out/Match/Mix and resampling.");

        if (kind == CreativeKind::convolution) {
            for (int s = 0; s < 4; ++s) {
                const auto ir = FactoryConvolution::makeImpulse(48000, s);
                for (size_t i = 0; i < ir[0].size(); ++i) {
                    const size_t bin = std::min(size_t(127), i * 128 / ir[0].size());
                    irPeaks[size_t(s)][bin] = std::max(irPeaks[size_t(s)][bin], std::abs(ir[0][i]));
                }
                const float peak = *std::max_element(irPeaks[size_t(s)].begin(), irPeaks[size_t(s)].end());
                for (auto& v : irPeaks[size_t(s)]) v /= std::max(peak, 1.e-12f);
            }
        }
        refresh();
    }
    static juce::String glitchChoiceDescription(CreativeParameter field, int choice) {
        if (field == P::glitchPattern) {
            switch (choice) {
                case 0: return "Three hits; last one reversed";
                case 1: return "Four forward hits";
                case 2: return "One reverse pickup";
                case 3: return "One half-speed hit";
                default: return {};
            }
        }
        switch (choice) {
            case 0: return "Immediate (no grid)";
            case 1: return "Next sixteenth-note boundary";
            case 2: return "Next eighth-note triplet boundary";
            case 3: return "Next quarter-note boundary";
            default: return {};
        }
    }
    void setNeon(bool enabled) { neon = enabled; repaint(); }
    bool delaySynced() const { return value(P::delaySync) >= 0.5f; }
    double delayIntervalSeconds() const {
        return std::clamp(delaySynced() ? 60.0 * value(P::delayBeats) / tempo
                                      : value(P::delayTime) * .001,
                          1.0 / toneui::visualSampleRate(owner.getSampleRate()), 24.0);
    }
    double timingPhase() const { return delayPhase; }
    ~CreativeEffectVisual() override { if (retained) owner.releaseFxMonitor(slot); }
    void visibilityChanged() override { lastAnimationMs = -1; syncTelemetry(); }
    void parentHierarchyChanged() override { lastAnimationMs = -1; syncTelemetry(); }
    bool hasMeasuredActivity() const noexcept { return liveTelemetry; }
    float measuredWetRms() const noexcept { return liveTelemetry ? telemetry.wetRms : 0.f; }
    uint64_t measuredSequence() const noexcept { return liveTelemetry ? telemetry.sequence : 0; }
    reactiveVisual::GainReductionField measuredCompressionHistory() const noexcept {
        const bool active = liveTelemetry && enabled;
        if (!compressionCached || compressionSequence != telemetry.sequence || compressionEpoch != telemetry.epoch
            || compressionNode != telemetry.nodeId || compressionActive != active) {
            compressionCached = true; compressionSequence = telemetry.sequence; compressionEpoch = telemetry.epoch;
            compressionNode = telemetry.nodeId; compressionActive = active;
            compressionField = reactiveVisual::GainReductionField::from(telemetry, active);
        }
        return compressionField;
    }
    // Read-only rendering seam: units are milliseconds, Hz and nominal overlap.
    // History is a fixed 3 s parameter guide, not available captured audio.
    struct GranularModel {
        float lengthMs, densityHz, spacingMs, spanMs, overlap;
        float pitchSemitones, lookbackMs, scatterMs, guardMs, historyNearMs, historyFarMs;
        bool frozen, reverse;
    };
    bool hasParameterSnapshot() const { return ready; }
    GranularModel granularModel() const {
        const float length = value(P::granularSize), density = value(P::granularDensity);
        const float semitones = value(P::granularPitch), ratio = std::pow(2.f, semitones / 12.f);
        const bool frozen = value(P::granularFreeze) >= .5f, reverse = value(P::granularReverse) >= .5f;
        // Nominal playback guard, excluding the DSP's sample-rate-dependent
        // interpolation padding and startup/recorded-history clamps.
        const float guard = length * (frozen ? ratio : reverse ? 0.f : std::max(0.f, ratio - 1.f));
        const float near = std::clamp(value(P::granularPosition) + guard, 0.f, 3000.f);
        return {length, density, 1000.f / density, std::max(500.f, length * 3.f), length * density * .001f,
                semitones, value(P::granularPosition), value(P::granularJitter), guard, near,
                std::min(3000.f, near + length * ratio + value(P::granularJitter)), frozen, reverse};
    }
    bool visibleInHierarchy() const {
        if (!isShowing()) return false;
        auto visible = getLocalBounds();
        for (auto* parent = getParentComponent(); parent != nullptr; parent = parent->getParentComponent()) {
            visible = visible.getIntersection(getLocalArea(parent, parent->getLocalBounds()));
            if (visible.isEmpty()) return false;
        }
        return !visible.isEmpty();
    }
    void refresh() { refreshAt(juce::Time::getMillisecondCounterHiRes()); }
    // Explicit monotonic time also allows deterministic animation tests. This is
    // a parameter-timed illustration, not host transport or measured audio.
    void refreshAt(double nowMs) {
        syncTelemetry();
        if (!visibleInHierarchy()) { lastAnimationMs = -1; return; }
        if (retained) {
            FxMonitorFrame next;
            if (owner.readFxMonitor(slot, next) && (next.hasWet || kind == CreativeKind::compressor)) {
                if (next.sequence != telemetry.sequence || next.epoch != telemetry.epoch) {
                    telemetry = next; lastTelemetryMs = nowMs; liveTelemetry = true;
                } else if (nowMs - lastTelemetryMs > 250) liveTelemetry = false;
            } else liveTelemetry = false;
        }
        auto next = owner.getModulationSnapshot();
        // Preserve the entire previous model on a failed bounded snapshot read.
        if (next.consistent) {
            for (size_t i = 0; i < values.size(); ++i) {
                const auto& d = creativeParameters[i];
                values[i] = std::clamp(toneui::visualParameter(owner, creativeParameterId(slot, i), d.initial, &next), d.min, d.max);
            }
            enabled = toneui::visualParameter(owner, fxParameterId(slot, "On"), 1, &next) >= 0.5f;
            mix = std::clamp(toneui::visualParameter(owner, fxParameterId(slot, "Mix"), 100, &next), 0.f, 100.f);
            ready = true;
        }
        if (kind == CreativeKind::delay) {
            const auto bpm = owner.settings().tempoBpm;
            tempo = std::isfinite(bpm) && bpm >= 20 && bpm <= 400 ? bpm : 120;
            if (lastAnimationMs >= 0 && nowMs >= lastAnimationMs && enabled)
                delayPhase = std::fmod(delayPhase + (nowMs - lastAnimationMs) * .001 / delayIntervalSeconds(), 1.0);
            lastAnimationMs = nowMs;
        }

        repaint();
    }
    void paint(juce::Graphics& g) override {

        const auto c = themePalette(*this, neon);
        auto b = getLocalBounds().toFloat();
        if (factoryReactive()) { paintReactive(g, b, c); return; }
        if (kind == CreativeKind::compressor) { paintCompressor(g, b, c); return; }
        surfaceMaterial.paint(g, b.toNearestInt(), c.inset, c.accent, fluidAnimationSeconds(*this), .25f);
        g.setColour(c.border); g.drawRect(b.reduced(0.5f), 1);
        b.reduce(8, 8);
        if (kind == CreativeKind::granular) { granular(g, b, {}, c); return; }
        if (kind == CreativeKind::delay) { delay(g, b, {}, c); return; }
        if (kind == CreativeKind::vocalGlitch) {
            text(g, b.removeFromTop(21), enabled ? "PLAYBACK GUIDE" : "BYPASS | PLAYBACK GUIDE", c.text);
            if (!ready) {
                text(g, b.removeFromTop(21), "Waiting for parameter snapshot", c.muted);
                return;
            }
            text(g, b.removeFromTop(21), glitchChoiceDescription(P::glitchPattern, juce::roundToInt(value(P::glitchPattern))), c.text);
            text(g, b.removeFromTop(21), glitchChoiceDescription(P::glitchGrid, juce::roundToInt(value(P::glitchGrid))), c.muted);
            return;
        }
        text(g, b.removeFromTop(16), enabled ? "PARAMETER MODEL" : "BYPASS | stored parameter model", c.text);
        text(g, b.removeFromTop(15), ready ? "Effective targets / base if unassigned" : "Waiting for coherent parameter snapshot", c.muted);
        auto footer = b.removeFromBottom(43);
        b.removeFromBottom(15); // Dedicated axis-label row.
        auto plot = b.withTrimmedLeft(23).withTrimmedRight(5).reduced(0, 6);
        grid(g, plot, c.border);
        switch (kind) {
            case CreativeKind::reverb: break; // Shared reactive path above.
            case CreativeKind::delay: delay(g, plot, footer, c); break;
            case CreativeKind::granular: granular(g, plot, footer, c); break;
            case CreativeKind::compressor: break; // Unified plot above.
            case CreativeKind::bitcrusher: crusher(g, plot, footer, c); break;
            case CreativeKind::convolution: break;
            case CreativeKind::saturator: break;
            case CreativeKind::vocalGlitch: break; // Compact guide above.
        }
    }
private:
    void convolutionGuide(juce::Graphics& g, juce::Rectangle<float> plot,
                          juce::Rectangle<float> footer, const ThemePalette& c) const {
        const size_t space = size_t(std::clamp(int(std::round(value(P::convolutionSpace))), 0, 3));
        auto& path = guideCache.path;
        if (guideCache.update(plot, values, enabled, mix)) {
        for (size_t i = 0; i < 128; ++i) {
            const float t = float(i) / 127;
            const float taper = std::exp(-t * float(FactoryConvolution::durations[space]) * (100.f / value(P::convolutionTail) - 1) * 4);
            const float x = plot.getX() + t * plot.getWidth();
            const float y = plot.getBottom() - irPeaks[space][i] * taper * plot.getHeight();
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
        }
        g.setColour(c.accent); g.strokePath(path, juce::PathStrokeType(1.15f));
        text(g, footer.removeFromTop(15), juce::String(FactoryConvolution::names[space]) + " | SYNTHETIC IR | " + juce::String(FactoryConvolution::durations[space], 2) + " s", c.text);
        text(g, footer.removeFromTop(14), "48 kHz L peak envelope / Tail taper guide", c.muted);
        text(g, footer, "Tilt / pre-delay / duck not plotted", c.muted);
    }
    void saturatorGuide(juce::Graphics& g, juce::Rectangle<float> plot,
                        juce::Rectangle<float> footer, const ThemePalette& c) const {
        auto& path = guideCache.path;
        if (guideCache.update(plot, values, enabled, mix)) {
        const double drive = std::pow(10.0, value(P::saturatorDrive) / 20.0);
        for (int i = 0; i <= 128; ++i) {
            const double x = double(i) / 64 - 1;
            const double y = Waveshaper::transfer(x * drive, 3, value(P::saturatorCharacter) * .01);
            const float px = plot.getX() + float(i) / 128 * plot.getWidth();
            const float py = plot.getCentreY() - float(y) * plot.getHeight() * .42f;
            if (!i) path.startNewSubPath(px, py); else path.lineTo(px, py);
        }
        }
        g.setColour(c.accent); g.strokePath(path, juce::PathStrokeType(1.3f));
        text(g, footer.removeFromTop(15), "STATIC TRANSFER | input -1 to +1", c.text);
        text(g, footer.removeFromTop(14), "2x IIR + ADAA | not alias-free", c.muted);
        text(g, footer, "Field: faint INPUT / strong WET | shared zero", c.secondary);
    }
    bool factoryReactive() const { return kind == CreativeKind::reverb || kind == CreativeKind::convolution || kind == CreativeKind::saturator; }
    void paintReactive(juce::Graphics& g, juce::Rectangle<float> bounds, const ThemePalette& c) {
        auto b = bounds.reduced(8, 8);
        auto header = b.removeFromTop(16);
        auto meter = b.removeFromTop(24);
        auto footer = b.removeFromBottom(43);
        b.removeFromBottom(15);
        auto plot = b.withTrimmedLeft(23).withTrimmedRight(5).reduced(0, 6);
        auto fieldPlot = plot;
        if (kind == CreativeKind::reverb) { fieldPlot.removeFromTop(21); fieldPlot.removeFromTop(14); }
        const bool fieldActive = liveTelemetry && enabled && mix > 0;
        if (!waveformCached || waveformSequence != telemetry.sequence || waveformEpoch != telemetry.epoch
            || waveformNode != telemetry.nodeId || waveformActive != fieldActive) {
            waveformCached = true; waveformSequence = telemetry.sequence; waveformEpoch = telemetry.epoch;
            waveformNode = telemetry.nodeId; waveformActive = fieldActive;
            waveformField = reactiveVisual::WaveformField::from(telemetry, fieldActive);
        }
        reactiveMaterial.paint(g, bounds, fieldPlot, waveformField,
            {c.inset, c.accent, c.secondary, c.muted}, fluidAnimationSeconds(*this));
        g.setColour(c.border); g.drawRect(bounds.reduced(.5f), 1);
        text(g, header, enabled ? "MEASURED MATERIAL / MODEL GUIDE" : "BYPASS | stored parameter model", c.text);
        wetMeter(g, meter, c);
        if (kind == CreativeKind::reverb) reverb(g, plot, footer, c);
        else {
            grid(g, plot, c.border);
            if (kind == CreativeKind::convolution) convolutionGuide(g, plot, footer, c);
            else saturatorGuide(g, plot, footer, c);
        }
    }
    void syncTelemetry() {
        const bool wanted = (factoryReactive() || kind == CreativeKind::compressor) && visibleInHierarchy();
        if (wanted != retained) {
            if (wanted) owner.retainFxMonitor(slot); else owner.releaseFxMonitor(slot);
            retained = wanted; liveTelemetry = false; telemetry = {}; lastTelemetryMs = -1;
        }
    }
    void wetMeter(juce::Graphics& g, juce::Rectangle<float> plot, const ThemePalette& c) {
        const bool audible = liveTelemetry && enabled && mix > 0;
        const float rms = audible ? telemetry.wetRms : 0.f;
        const float db = juce::Decibels::gainToDecibels(rms, -72.f);
        text(g, plot.withHeight(14), audible ? "WET RMS " + juce::String(db, 1) + " dBFS | pre-Mix" : "No live wet activity / bypass or stopped", c.secondary);
        text(g, plot.withTrimmedTop(13), "FIELD: HISTORY < NOW | 2.56 s / -60..0 dBFS", c.muted);
    }
    void paintCompressor(juce::Graphics& g, juce::Rectangle<float> bounds, const ThemePalette& c) {
        g.setColour(c.inset); g.fillRect(bounds);
        g.setColour(c.border); g.drawRect(bounds.reduced(.5f), 1.f);
        auto b = bounds.reduced(8, 8);
        auto meter = b.removeFromBottom(17);
        const auto measured = measuredCompressionHistory();
        compressorPlot.paint(g, b, telemetry, measured, liveTelemetry && enabled,
            {value(P::compressorThreshold), value(P::compressorRatio), value(P::compressorKnee),
             value(P::compressorMakeup), mix * .01f, enabled},
            {c.inset, c.border, c.muted, c.accent, c.text, c.secondary, c.muted}, componentTheme(*this, neon));
        const float current = measured.available ? measured.currentDb : 0.f;
        text(g, meter.removeFromLeft(112), measured.available ? "Actual GR " + number(current) + " dB" : "GR unavailable", c.secondary);
        meter.reduce(0, 5); g.setColour(c.border); g.fillRect(meter);
        g.setColour(c.secondary); g.fillRect(meter.withWidth(meter.getWidth() * std::clamp(current / 36.f, 0.f, 1.f)));
    }
    CompressorPlot compressorPlot;
    struct GuideCache {
        juce::Path path;
        juce::Rectangle<float> bounds;
        std::array<float, creativeParameterCount> parameters{};
        float mix = 0;
        bool enabled = false, valid = false;
        bool update(juce::Rectangle<float> nextBounds,
                    const std::array<float, creativeParameterCount>& nextParameters,
                    bool nextEnabled, float nextMix) {
            if (valid && bounds == nextBounds && parameters == nextParameters
                && enabled == nextEnabled && mix == nextMix) return false;
            valid = true; bounds = nextBounds; parameters = nextParameters;
            enabled = nextEnabled; mix = nextMix; path.clear(); return true;
        }
    };
    mutable GuideCache guideCache;

    mutable reactiveVisual::GainReductionField compressionField;
    mutable uint64_t compressionSequence = 0, compressionEpoch = 0;
    mutable uint32_t compressionNode = 0;
    mutable bool compressionCached = false, compressionActive = false;
    reactiveVisual::WaveformField waveformField;
    uint64_t waveformSequence = 0, waveformEpoch = 0;
    uint32_t waveformNode = 0;
    bool waveformCached = false, waveformActive = false;
    FxMonitorFrame telemetry{};
    double lastTelemetryMs = -1;
    bool retained = false, liveTelemetry = false;
    FluidBackground surfaceMaterial;
    reactiveVisual::ReactiveMaterial reactiveMaterial;
    std::array<std::array<float, 128>, 4> irPeaks{};
    using P = CreativeParameter;
    static juce::String grainDescription() {
        return "Parameter envelope graph, not live audio or measured grains. Fixed example windows show grain length and nominal "
               "density/overlap using coherent effective targets (host base if unassigned). The 3 s history guide shows requested "
               "lookback, nominal pitch guard and scatter in milliseconds, not recorded audio. Interpolation padding, startup clamps, "
               "random timing and the live voice limit are not simulated. Freeze holds capture, not grain playback.";
    }
    float value(P p) const { return values[size_t(p)]; }
    static juce::String number(double v, int decimals = 1) {
        return decimals == 0 ? juce::String(juce::roundToInt(v)) : juce::String(v, decimals);
    }
    static juce::String pitch(float v) { return (v > 0 ? "+" : "") + number(v) + " st"; }
    void text(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, juce::Colour c) const {
        g.setColour(c); g.setFont(themeFont(componentTheme(*this, neon), 12.f));
        g.drawFittedText(s, r.toNearestInt(), juce::Justification::centredLeft, 1, 1.f);
    }
    static void grid(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) {
        g.setColour(c.withAlpha(0.5f));
        for (int i = 0; i <= 4; ++i) {
            const float t = float(i) / 4;
            g.drawHorizontalLine(int(r.getY() + t * r.getHeight()), r.getX(), r.getRight());
            g.drawVerticalLine(int(r.getX() + t * r.getWidth()), r.getY(), r.getBottom());
        }
    }
    static void curve(juce::Graphics& g, const juce::Path& line, juce::Rectangle<float> r, juce::Colour c) {
        auto fill = line;
        fill.lineTo(line.getCurrentPosition().x, r.getBottom());
        fill.lineTo(r.getX(), r.getBottom()); fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(c.withAlpha(0.22f), r.getX(), r.getY(),
            c.withAlpha(0.015f), r.getX(), r.getBottom(), false));
        g.fillPath(fill); g.setColour(c); g.strokePath(line, juce::PathStrokeType(1.15f));
    }
    void axis(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& left,
              const juce::String& right, const ThemePalette& c) const {
        auto row = juce::Rectangle<float>(r.getX(), r.getBottom() + 4, r.getWidth(), 14);
        text(g, row.removeFromLeft(row.getWidth() * 0.5f), left, c.muted);
        g.setColour(c.muted); g.drawText(right, row, juce::Justification::centredRight);
    }
    void pitchRail(juce::Graphics& g, juce::Rectangle<float>& r, float semitones, float amount, const ThemePalette& c) const {
        auto lane = r.removeFromTop(21);
        text(g, lane.removeFromLeft(35), "Pitch", c.secondary);
        const float centre = lane.getCentreY(), end = centre - semitones / 12 * 8;
        g.setColour(c.border); g.drawLine(lane.getX(), centre, lane.getRight(), centre, .75f);
        g.setColour(c.secondary.withAlpha(.15f + .85f * amount));
        g.drawLine(lane.getX(), centre, lane.getRight() - 3, end, 1.1f);
        g.fillRect(lane.getRight() - 5, end - 2, 4.f, 4.f);
    }
    void reverb(juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float> f, const ThemePalette& c) const {
        const float pre = value(P::reverbPredelay) * .001f, rt = value(P::reverbDecay);
        const float attack = value(P::reverbSwell) * .001f;
        const bool freeze = value(P::reverbFreeze) >= .5f;
        pitchRail(g, r, value(P::reverbPitch), freeze ? 0.f : value(P::reverbPitchAmount) * .01f, c);
        auto legend = r.removeFromTop(14);
        text(g, legend.removeFromLeft(legend.getWidth() * .5f), "Decay guide", c.secondary);
        text(g, legend, "Swell guide", c.accent);
        const float span = pre + std::max(rt, 3 * attack);
        const auto xFor = [&](float t) { return r.getX() + t / span * r.getWidth(); };
        grid(g, r, c.border);
        // Log amplitude axis: nominal RT60 guide, deliberately not an FDN IR.
        juce::Path decay, swell;
        for (int i = 0; i <= 240; ++i) {
            const float t = span * float(i) / 240, elapsed = std::max(0.f, t - pre);
            const float db = t < pre ? -60.f : freeze ? 0.f : std::max(-60.f, -60 * elapsed / rt);
            const float onset = t < pre ? 0.f : attack > 0 ? 1 - std::exp(-elapsed / attack) : 1.f;
            const float shaped = std::max(-60.f, db + 20 * std::log10(std::max(0.001f, onset)));
            const float x = xFor(t), y = r.getY() - shaped / 60 * r.getHeight();
            if (i == 0) { decay.startNewSubPath(x, r.getY() - db / 60 * r.getHeight()); swell.startNewSubPath(x, y); }
            else { decay.lineTo(x, r.getY() - db / 60 * r.getHeight()); swell.lineTo(x, y); }
        }
        g.setColour(c.secondary.withAlpha(.55f)); g.strokePath(decay, juce::PathStrokeType(.8f));
        g.setColour(c.accent); g.strokePath(swell, juce::PathStrokeType(1.15f));
        g.setColour(c.secondary); g.drawVerticalLine(int(xFor(pre)), r.getY(), r.getBottom());
        text(g, {r.getX() - 25, r.getY(), 24, 12}, "0 dB", c.muted);
        text(g, {r.getX() - 25, r.getBottom() - 12, 24, 12}, "-60", c.muted);
        axis(g, r, "0 s", number(span, 2) + " s", c);
        text(g, f.removeFromTop(14), "RT60 " + number(rt, 2) + " s  /  Pre " + number(pre * 1000, 0) + " ms  /  Swell " + number(attack * 1000, 0) + " ms", c.text);
        text(g, f.removeFromTop(14), "Pitch " + pitch(value(P::reverbPitch)) + "  /  Feedback " + number(value(P::reverbPitchAmount), 0) + "%" + (freeze ? " (off in Freeze)" : ""), c.secondary);
        text(g, f, freeze ? "FREEZE | held-tail guide; not measured IR" : "Nominal decay + onset guide; not measured IR", c.muted);
    }
    void delay(juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float>, const ThemePalette& c) const {
        const double seconds = delayIntervalSeconds();
        const float feedback = value(P::delayFeedback) * .01f, ping = value(P::delayPingPong) * .01f;
        text(g, r.removeFromTop(17), enabled ? "DELAY / TIMING" : "DELAY / BYPASSED", c.text);
        text(g, r.removeFromTop(16), "Parameter-timed / not audio", c.muted);
        auto footer = r.removeFromBottom(48);
        text(g, footer.removeFromTop(16), number(seconds * 1000, 1) + " ms / " + (delaySynced() ? number(tempo, 1) + " BPM" : "free"), c.text);
        text(g, footer.removeFromTop(16), "FB " + number(feedback * 100, 0) + "% / Cross " + number(ping * 100, 0) + "%", c.secondary);
        text(g, footer, "L-seeded / " + pitch(value(P::delayPitch)), c.muted);
        r.reduce(7, 10);
        const float top = r.getY() + r.getHeight() * .26f;
        const float bottom = r.getY() + r.getHeight() * .76f;
        g.setColour(c.border);
        g.drawLine(r.getX(), top, r.getRight(), top);
        g.drawLine(r.getX(), bottom, r.getRight(), bottom);
        text(g, {r.getX(), top - 19, 18, 15}, "L", c.secondary);
        text(g, {r.getX(), bottom - 19, 18, 15}, "R", c.accent);
        // One travelling cohort, L-seeded, attenuated/cross-fed at each interval.
        // Pulse size is feedback weight, deliberately not an audio-level meter.
        float left = 1, right = 0;
        for (int i = 0; i < 4; ++i) {
            const float x0 = r.getX() + r.getWidth() * float(i) / 4;
            const float x = x0 + r.getWidth() * float(delayPhase) / 4;
            g.setColour(c.border); g.drawVerticalLine(int(x0), r.getY(), r.getBottom());
            const auto pulse = [&](float y, float weight, juce::Colour colour) {
                if (weight < .001f) return;
                const float radius = 2.5f + 3.5f * weight;
                g.setColour(colour.withAlpha(.08f + .12f * weight));
                g.fillRect(x - radius * 2, y - radius * 2, radius * 4, radius * 4);
                g.setColour(colour.withAlpha(.3f + .7f * weight));
                g.fillRect(x - radius, y - radius, radius * 2, radius * 2);
            };
            pulse(top, left, c.secondary); pulse(bottom, right, c.accent);
            const float nextLeft = feedback * (left * (1 - ping) + right * ping);
            right = feedback * (right * (1 - ping) + left * ping); left = nextLeft;
        }
        auto progress = juce::Rectangle<float>(r.getX(), r.getBottom() + 3, r.getWidth(), 2);
        g.setColour(c.border); g.fillRect(progress);
        g.setColour(c.text); g.fillRect(progress.withWidth(progress.getWidth() * float(delayPhase)));
    }
    void granular(juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float>, const ThemePalette& c) const {
        const auto model = granularModel();
        const bool narrow = r.getWidth() < 280;
        text(g, r.removeFromTop(16), narrow ? "PARAMETER ENVELOPES" : "GRAIN PARAMETER ENVELOPES", c.text);
        text(g, r.removeFromTop(15), !enabled ? "Bypass / not live audio" : model.frozen ? "Freeze / not live audio" : "Model / not live audio", c.muted);
        auto footer = r.removeFromBottom(48);
        auto historyLabel = r.removeFromTop(14);
        text(g, historyLabel, "History guide", c.secondary);
        g.setColour(c.muted); g.drawText("3 s", historyLabel, juce::Justification::centredRight);
        auto history = r.removeFromTop(8).reduced(1, 1);
        const auto historyX = [&](float age) { return history.getRight() - age / 3000.f * history.getWidth(); };
        g.setColour(c.border); g.fillRect(history);
        auto region = juce::Rectangle<float>(historyX(model.historyFarMs), history.getY(),
            std::max(2.f, (model.historyFarMs - model.historyNearMs) / 3000.f * history.getWidth()), history.getHeight());
        g.setGradientFill(juce::ColourGradient(c.secondary.withAlpha(.18f), region.getX(), region.getY(),
            c.secondary.withAlpha(.7f), region.getRight(), region.getY(), false));
        g.fillRect(region);
        g.setColour(c.accent);
        g.drawLine(historyX(model.historyNearMs), history.getY() - 2, historyX(model.historyNearMs), history.getBottom() + 2, 1.5f);
        r.removeFromTop(5);
        auto historyAxis = r.removeFromTop(13);
        text(g, historyAxis, "-3000 ms", c.muted);
        g.setColour(c.muted); g.drawText("0", historyAxis, juce::Justification::centredRight);
        r.removeFromTop(4);
        auto timeAxis = r.removeFromBottom(16);
        auto plot = r.reduced(1, 2);
        if (plot.getHeight() > 0 && plot.getWidth() > 0) {
            g.setGradientFill(juce::ColourGradient(c.secondary.withAlpha(.065f), plot.getX(), plot.getY(),
                c.inset.withAlpha(0.f), plot.getX(), plot.getBottom(), false));
            g.fillRect(plot);
            grid(g, plot, c.border.withAlpha(.45f));
            const auto xFor = [&](float ms) { return plot.getX() + ms / model.spanMs * plot.getWidth(); };
            // Original stable overlapping example windows, including the tails
            // of preceding windows. Neither wall clock nor live grains enter.
            juce::Graphics::ScopedSaveState saved(g);
            g.reduceClipRegion(plot.toNearestInt());
            const int preceding = int(std::ceil(model.lengthMs / model.spacingMs));
            const int following = int(std::ceil(model.spanMs / model.spacingMs));
            for (int grain = -preceding; grain <= following; ++grain) {
                const float start = float(grain) * model.spacingMs;
                const float from = std::max(0.f, -start / model.lengthMs);
                const float to = std::min(1.f, (model.spanMs - start) / model.lengthMs);
                if (from >= to) continue;
                juce::Path window;
                for (int i = 0; i <= 64; ++i) {
                    const float phase = from + (to - from) * float(i) / 64.f;
                    const float envelope = 16.f * phase * phase * (1.f - phase) * (1.f - phase);
                    const float x = xFor(start + phase * model.lengthMs);
                    const float y = plot.getBottom() - envelope * plot.getHeight() * .82f;
                    if (i == 0) window.startNewSubPath(x, y); else window.lineTo(x, y);
                }
                auto fill = window;
                fill.lineTo(window.getCurrentPosition().x, plot.getBottom());
                fill.lineTo(xFor(start + from * model.lengthMs), plot.getBottom()); fill.closeSubPath();
                const auto colour = grain % 2 == 0 ? c.secondary : c.accent;
                g.setGradientFill(juce::ColourGradient(colour.withAlpha(.16f), plot.getX(), plot.getY(),
                    colour.withAlpha(.015f), plot.getX(), plot.getBottom(), false));
                g.fillPath(fill);
                g.setColour(colour.withAlpha(grain == 0 ? .95f : .5f));
                g.strokePath(window, juce::PathStrokeType(grain == 0 ? 1.6f : .9f));
            }
            // One complete window's duration; density controls the spacing of
            // every curve, not its width or the plotted time range.
            const float y = plot.getY() + 2, end = xFor(model.lengthMs);
            g.setColour(c.secondary);
            g.drawLine(plot.getX(), y, end, y, 1);
            g.drawLine(plot.getX(), y, plot.getX(), y + 4, 1);
            g.drawLine(end, y, end, y + 4, 1);
        }
        text(g, timeAxis, "0 ms", c.muted);
        g.setColour(c.muted); g.drawText(number(model.spanMs, 0) + " ms", timeAxis, juce::Justification::centredRight);
        text(g, footer.removeFromTop(16), number(model.lengthMs, 0) + " ms length / " + number(model.densityHz, 0) + " Hz", c.text);
        text(g, footer.removeFromTop(16), "Overlap " + number(model.overlap, 1) + "x / " + pitch(model.pitchSemitones), c.secondary);
        text(g, footer, "Lookback " + number(model.lookbackMs, 0) + " ms / " + (model.reverse ? "Rev" : "Fwd"), c.muted);
    }
    void crusher(juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float> f, const ThemePalette& c) const {
        const float bits = value(P::crusherBits), step = std::pow(2.f, 1 - bits);
        const float drive = std::pow(10.f, value(P::crusherDrive) / 20);
        // Zoom to eight quantizer steps at high bit depths; report the real scale.
        const float extent = std::min(1.f, 4 * step / drive);
        const auto yFor = [&](float y) { return r.getCentreY() - std::clamp(y / extent, -1.f, 1.f) * r.getHeight() * .5f; };
        g.setColour(c.muted.withAlpha(.35f)); g.drawLine(r.getX(), r.getBottom(), r.getRight(), r.getY(), .8f);
        juce::Path line;
        float previous = 0, previousInput = 0;
        for (int i = 0; i <= 512; ++i) {
            const float in = (float(i) / 256 - 1) * extent;
            const float quantized = std::round(std::clamp(in * drive, -1.f, 1.f) / step) * step / drive;
            const float out = enabled ? in * (1 - mix * .01f) + quantized * mix * .01f : in;
            const float x = r.getX() + r.getWidth() * float(i) / 512;
            if (i == 0) line.startNewSubPath(x, yFor(out));
            else {
                const float drySlope = enabled ? 1 - mix * .01f : 1;
                line.lineTo(x, yFor(previous + (in - previousInput) * drySlope));
                line.lineTo(x, yFor(out));
            }
            previous = out; previousInput = in;
        }
        curve(g, line, r, c.accent);
        axis(g, r, "Input / output", "+/-" + juce::String(extent, 3, true) + " FS", c);
        text(g, f.removeFromTop(14), number(bits, 1) + " bits / " + number(std::min(double(value(P::crusherRate)), toneui::visualSampleRate(owner.getSampleRate()))) + " Hz hold / Drive " + number(value(P::crusherDrive)) + " dB", c.text);
        text(g, f.removeFromTop(14), "Quantizer + mix / zoom " + number(1 / extent, 0) + "x / Dither " + number(value(P::crusherDither), 0) + "%", c.secondary);
        text(g, f, "Static stair transfer; hold / filter / noise omitted", c.muted);
    }
    Processor& owner;
    size_t slot;
    CreativeKind kind;
    std::array<float, creativeParameterCount> values = CreativeSettings{}.values;
    double tempo = 120, delayPhase = 0, lastAnimationMs = -1;
    float mix = 100;
    bool enabled = true, neon = false, ready = false;
};

} // namespace vocalstack
