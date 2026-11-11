#pragma once

#include "CreativeEffects.h"
#include "FxVisuals.h"

namespace vocalstack {

// Message-thread parameter models only. No reads of live delay/grain DSP state.
class CreativeEffectVisual final : public juce::Component, public juce::SettableTooltipClient {
public:
    CreativeEffectVisual(Processor& processor, size_t fxSlot, CreativeKind effectKind)
        : owner(processor), slot(fxSlot), kind(effectKind) {
        setName("Effect parameter model");
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTooltip("Parameter-derived schematic using coherent effective modulation targets. Unassigned parameters are host base reads; DSP smoothing and signal-dependent behaviour are not simulated. Reverb is not a measured impulse response; grains are not live grain telemetry. Only compressor GR is measured by the DSP.");
        refresh();
    }
    void setNeon(bool enabled) { neon = enabled; repaint(); }
    bool delaySynced() const { return value(P::delaySync) >= 0.5f; }
    void refresh() {
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
        }
        const float gr = owner.getCreativeGainReduction(slot);
        reduction = std::isfinite(gr) ? std::max(0.f, gr) : 0.f;
        repaint();
    }
    void paint(juce::Graphics& g) override {
        const auto c = themePalette(*this, neon);
        auto b = getLocalBounds().toFloat();
        g.setColour(c.inset); g.fillRoundedRectangle(b, 5);
        g.setColour(c.border); g.drawRoundedRectangle(b.reduced(0.5f), 5, 1);
        b.reduce(10, 8);
        text(g, b.removeFromTop(16), enabled ? "PARAMETER MODEL" : "BYPASS | stored parameter model", c.text);
        text(g, b.removeFromTop(15), ready ? "Effective targets / base if unassigned" : "Waiting for coherent parameter snapshot", c.muted);
        auto footer = b.removeFromBottom(43);
        b.removeFromBottom(15); // Dedicated axis-label row.
        auto plot = b.withTrimmedLeft(23).withTrimmedRight(5).reduced(0, 6);
        grid(g, plot, c.border);
        switch (kind) {
            case CreativeKind::reverb: reverb(g, plot, footer, c); break;
            case CreativeKind::delay: delay(g, plot, footer, c); break;
            case CreativeKind::granular: granular(g, plot, footer, c); break;
            case CreativeKind::compressor: compressor(g, plot, footer, c); break;
            case CreativeKind::bitcrusher: crusher(g, plot, footer, c); break;
        }
    }
private:
    using P = CreativeParameter;
    float value(P p) const { return values[size_t(p)]; }
    static juce::String number(double v, int decimals = 1) { return juce::String(v, decimals); }
    static juce::String pitch(float v) { return (v > 0 ? "+" : "") + number(v) + " st"; }
    static void text(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, juce::Colour c) {
        g.setColour(c); g.setFont(juce::Font(juce::FontOptions(10.f)));
        g.drawFittedText(s, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
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
    static void axis(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& left,
                     const juce::String& right, const ThemePalette& c) {
        auto row = juce::Rectangle<float>(r.getX(), r.getBottom() + 4, r.getWidth(), 14);
        text(g, row.removeFromLeft(row.getWidth() * 0.5f), left, c.muted);
        g.setColour(c.muted); g.drawText(right, row, juce::Justification::centredRight);
    }
    static void pitchRail(juce::Graphics& g, juce::Rectangle<float>& r, float semitones, float amount, const ThemePalette& c) {
        auto lane = r.removeFromTop(21);
        text(g, lane.removeFromLeft(35), "Pitch", c.secondary);
        const float centre = lane.getCentreY(), end = centre - semitones / 12 * 8;
        g.setColour(c.border); g.drawLine(lane.getX(), centre, lane.getRight(), centre, .75f);
        g.setColour(c.secondary.withAlpha(.15f + .85f * amount));
        g.drawLine(lane.getX(), centre, lane.getRight() - 3, end, 1.1f);
        g.fillEllipse(lane.getRight() - 5, end - 2, 4, 4);
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
        curve(g, swell, r, c.accent);
        g.setColour(c.secondary); g.drawVerticalLine(int(xFor(pre)), r.getY(), r.getBottom());
        text(g, {r.getX() - 25, r.getY(), 24, 12}, "0 dB", c.muted);
        text(g, {r.getX() - 25, r.getBottom() - 12, 24, 12}, "-60", c.muted);
        axis(g, r, "0 s", number(span, 2) + " s", c);
        text(g, f.removeFromTop(14), "RT60 " + number(rt, 2) + " s  /  Pre " + number(pre * 1000, 0) + " ms  /  Swell " + number(attack * 1000, 0) + " ms", c.text);
        text(g, f.removeFromTop(14), "Pitch " + pitch(value(P::reverbPitch)) + "  /  Feedback " + number(value(P::reverbPitchAmount), 0) + "%" + (freeze ? " (off in Freeze)" : ""), c.secondary);
        text(g, f, freeze ? "FREEZE | held-tail guide; not measured IR" : "Nominal decay + onset guide; not measured IR", c.muted);
    }
    void delay(juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float> f, const ThemePalette& c) const {
        const bool sync = delaySynced();
        const double seconds = std::clamp(sync ? 60 * value(P::delayBeats) / tempo : value(P::delayTime) * .001, .000001, 24.0);
        const float feedback = value(P::delayFeedback) * .01f, ping = value(P::delayPingPong) * .01f;
        pitchRail(g, r, value(P::delayPitch), std::min(1.f, std::abs(value(P::delayPitch)) * 10), c);
        const float laneHeight = r.getHeight() * .40f;
        text(g, {r.getX() - 20, r.getY(), 18, 14}, "L", c.secondary);
        text(g, {r.getX() - 20, r.getCentreY(), 18, 14}, "R", c.accent);
        // Illustrative L-seeded cross-feedback, not a prediction of stereo input.
        float l = 1, right = 0;
        for (int i = 1; i <= 8; ++i) {
            const float x = r.getX() + r.getWidth() * float(i) / 8.5f;
            const float bottomL = r.getY() + laneHeight, bottomR = r.getBottom();
            g.setColour(c.secondary.withAlpha(.85f));
            g.fillRect(x - 2, bottomL - l * laneHeight, 4.f, std::max(1.f, l * laneHeight));
            g.setColour(c.accent.withAlpha(.85f));
            g.fillRect(x - 2, bottomR - right * laneHeight, 4.f, std::max(1.f, right * laneHeight));
            const float nextL = feedback * (l * (1 - ping) + right * ping);
            right = feedback * (right * (1 - ping) + l * ping); l = nextL;
        }
        axis(g, r, "0 s", number(seconds * 8.5, 2) + " s", c);
        text(g, f.removeFromTop(14), sync ? number(value(P::delayBeats), 4) + " beats @ " + number(tempo, 1) + " BPM / " + number(seconds * 1000, 0) + " ms"
                                       : "Free time " + number(seconds * 1000, 1) + " ms", c.text);
        text(g, f.removeFromTop(14), "FB " + number(feedback * 100, 0) + "%  /  Ping pong " + number(ping * 100, 0) + "%  /  " + pitch(value(P::delayPitch)), c.secondary);
        text(g, f, "L-seeded repeat schematic; colour / ducking omitted", c.muted);
    }
    void granular(juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float> f, const ThemePalette& c) const {
        const float length = value(P::granularSize), density = value(P::granularDensity);
        auto history = r.removeFromTop(22);
        text(g, history.removeFromLeft(42), "History", c.muted);
        history.reduce(0, 7);
        g.setColour(c.border); g.fillRoundedRectangle(history, 2);
        const float ratio = std::pow(2.f, value(P::granularPitch) / 12);
        const bool frozen = value(P::granularFreeze) >= .5f;
        const float guard = float(4000 / toneui::visualSampleRate(owner.getSampleRate()))
            + length * (frozen ? ratio : std::max(0.f, ratio - 1));
        const float age = value(P::granularPosition) + guard;
        const float zone = std::min(3000.f - age, length * ratio + value(P::granularJitter));
        // Age runs oldest (left, 3 s) to capture head (right, 0 s).
        auto region = history.withX(history.getRight() - (age + zone) / 3000 * history.getWidth())
            .withWidth(std::max(2.f, zone / 3000 * history.getWidth()));
        g.setColour(c.secondary.withAlpha(.35f)); g.fillRoundedRectangle(region, 2);
        g.setColour(c.accent); g.drawVerticalLine(int(history.getRight() - age / 3000 * history.getWidth()), history.getY() - 2, history.getBottom() + 2);
        const float span = std::max(500.f, length * 3), spacing = 1000 / density;
        const bool reverse = value(P::granularReverse) >= .5f;
        // Fixed evenly-spaced example windows: never animate fictitious grains.
        const int count = std::min(40, int(span / spacing) + 1);
        for (int grain = 0; grain < count; ++grain) {
            juce::Path window;
            const float start = grain * spacing;
            for (int i = 0; i <= 48; ++i) {
                const float phase = float(i) / 48, t = start + phase * length;
                if (t > span) break;
                const float w = 16 * phase * phase * (1 - phase) * (1 - phase);
                const float x = r.getX() + t / span * r.getWidth();
                const float y = r.getBottom() - w * r.getHeight() * .72f;
                if (i == 0) window.startNewSubPath(x, y); else window.lineTo(x, y);
            }
            if (!window.isEmpty()) {
                auto fill = window;
                fill.lineTo(window.getCurrentPosition().x, r.getBottom());
                fill.lineTo(r.getX() + start / span * r.getWidth(), r.getBottom()); fill.closeSubPath();
                const auto colour = grain % 2 ? c.accent : c.secondary;
                g.setColour(colour.withAlpha(.055f)); g.fillPath(fill);
                g.setColour(colour.withAlpha(.65f)); g.strokePath(window, juce::PathStrokeType(.9f));
            }
        }
        axis(g, r, reverse ? "< reverse windows" : "forward windows >", number(span, 0) + " ms", c);
        text(g, f.removeFromTop(14), number(length, 0) + " ms  /  " + number(density, 1) + " Hz  /  Overlap ~" + number(length * density * .001) + "x  /  " + pitch(value(P::granularPitch)), c.text);
        text(g, f.removeFromTop(14), "3 s history / lookback " + number(value(P::granularPosition), 0) + " ms + guard / scatter " + number(value(P::granularJitter), 0) + "%", c.secondary);
        text(g, f, value(P::granularFreeze) >= .5f ? "SCHEMATIC | capture frozen; windows continue" : "SCHEMATIC | nominal windows, not live grains", c.muted);
    }
    void compressor(juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float> f, const ThemePalette& c) const {
        const float threshold = value(P::compressorThreshold), knee = value(P::compressorKnee);
        const float slope = 1 - 1 / value(P::compressorRatio), makeup = value(P::compressorMakeup);
        const auto yFor = [&](float db) { return r.getBottom() - std::clamp((db + 60) / 84, 0.f, 1.f) * r.getHeight(); };
        g.setColour(c.muted.withAlpha(.35f)); g.drawLine(r.getX(), yFor(-60), r.getRight(), yFor(24), .8f);
        g.setColour(c.secondary.withAlpha(.5f));
        g.drawVerticalLine(int(r.getX() + (threshold + 60) / 84 * r.getWidth()), r.getY(), r.getBottom());
        juce::Path line;
        for (int i = 0; i <= 256; ++i) {
            const float in = -60 + 84 * float(i) / 256, over = in - threshold;
            const float gr = knee > 0 && over > -knee * .5f && over < knee * .5f
                ? slope * (over + knee * .5f) * (over + knee * .5f) / (2 * knee) : slope * std::max(0.f, over);
            const float wet = enabled ? in - gr + makeup : in;
            const float out = in + 20 * std::log10((1 - mix * .01f) + mix * .01f * std::pow(10.f, (wet - in) / 20));
            const float x = r.getX() + r.getWidth() * float(i) / 256;
            if (i == 0) line.startNewSubPath(x, yFor(out)); else line.lineTo(x, yFor(out));
        }
        curve(g, line, r, c.accent);
        text(g, {r.getX() - 24, r.getY(), 24, 12}, "+24", c.muted);
        text(g, {r.getX() - 24, r.getBottom() - 12, 24, 12}, "-60", c.muted);
        axis(g, r, "Input -60 dB", "+24 dB", c);
        text(g, f.removeFromTop(14), "Threshold " + number(threshold) + " dB / " + number(value(P::compressorRatio)) + ":1 / Knee " + number(knee) + " dB", c.text);
        auto bar = f.removeFromTop(14);
        text(g, bar.removeFromLeft(110), "Actual GR " + number(reduction) + " dB", c.secondary);
        bar.reduce(0, 4); g.setColour(c.border); g.fillRect(bar);
        g.setColour(c.secondary); g.fillRect(bar.withWidth(bar.getWidth() * std::clamp(reduction / 36, 0.f, 1.f)));
        text(g, f, "Output -60..+24 dB (off-scale clipped); GR bar 0-36", c.muted);
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
    double tempo = 120;
    float reduction = 0, mix = 100;
    bool enabled = true, neon = false, ready = false;
};

} // namespace vocalstack
