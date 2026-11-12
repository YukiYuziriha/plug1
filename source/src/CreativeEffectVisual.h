#pragma once

#include "CreativeEffects.h"
#include "FxVisuals.h"

namespace vocalstack {

// Message-thread models; granular reads only the Processor's owned audio snapshot.
class CreativeEffectVisual final : public juce::Component, public juce::SettableTooltipClient {
public:
    CreativeEffectVisual(Processor& processor, size_t fxSlot, CreativeKind effectKind)
        : owner(processor), slot(fxSlot), kind(effectKind) {
        setName(kind == CreativeKind::granular ? "Live grain snapshot" : "Effect parameter model");
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTooltip(kind == CreativeKind::granular
                    ? grainDescription()
                    : "Parameter-derived schematic using coherent effective modulation targets. Unassigned parameters are host base reads; DSP smoothing and signal-dependent behaviour are not simulated. Reverb is not a measured impulse response. Compressor GR is measured by the DSP.");
        refresh();
    }
    void setNeon(bool enabled) { neon = enabled; repaint(); }
    bool delaySynced() const { return value(P::delaySync) >= 0.5f; }
    double delayIntervalSeconds() const {
        return std::clamp(delaySynced() ? 60.0 * value(P::delayBeats) / tempo
                                      : value(P::delayTime) * .001,
                          1.0 / toneui::visualSampleRate(owner.getSampleRate()), 24.0);
    }
    double timingPhase() const { return delayPhase; }
    void visibilityChanged() override { lastAnimationMs = -1; }
    void parentHierarchyChanged() override { lastAnimationMs = -1; }
    void mouseMove(const juce::MouseEvent& event) override {
        if (kind != CreativeKind::granular || !grainReady || !owner.isGrainSnapshotCurrent(grainFrame)) return;
        int next = -1;
        float distance = 18.f;
        for (uint32_t i = 0; i < grainFrame.grains.count; ++i) {
            const auto& grain = grainFrame.grains.grains[i];
            const float candidate = grainPoints[grain.slot].getDistanceFrom(event.position);
            if (candidate < distance) { distance = candidate; next = int(grain.slot); }
        }
        if (next != hoveredSlot) { hoveredSlot = next; repaint(); }
        if (next < 0) setTooltip(grainDescription());
        for (uint32_t i = 0; i < grainFrame.grains.count; ++i) {
            const auto& grain = grainFrame.grains.grains[i];
            if (int(grain.slot) == next)
                setTooltip("DSP slot " + juce::String(next + 1) + " / ring " + number(grain.position * 100, 2)
                    + "% / lifetime " + number(grain.progress * 100, 1) + "% / envelope " + number(grain.envelope, 3)
                    + " / pan " + number(grain.pan, 2) + (grain.step < 0 ? " / reverse" : " / forward")
                    + (grain.holdingBoundary ? " / boundary held" : ""));
        }
    }
    void mouseExit(const juce::MouseEvent&) override {
        hoveredSlot = -1;
        if (kind == CreativeKind::granular) setTooltip(grainDescription());
        repaint();
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
        if (!visibleInHierarchy()) { lastAnimationMs = -1; return; }
        if (kind == CreativeKind::granular) {
            const auto generation = grainFrame.generation;
            grainReady = owner.readGrainSnapshot(slot, grainFrame);
            if (!grainReady || generation != grainFrame.generation) {
                hoveredSlot = -1; setTooltip(grainDescription());
            }
            repaint();
            return;
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
        const float gr = owner.getCreativeGainReduction(slot);
        reduction = std::isfinite(gr) ? std::max(0.f, gr) : 0.f;
        repaint();
    }
    void paint(juce::Graphics& g) override {
        // A hidden view may become visible before its next timer refresh. Never
        // paint its cached cloud after a reset/removal/reuse in the meantime.
        if (kind == CreativeKind::granular && grainReady && !owner.isGrainSnapshotCurrent(grainFrame)) {
            grainReady = false; hoveredSlot = -1; setTooltip(grainDescription());
        }
        const auto c = themePalette(*this, neon);
        auto b = getLocalBounds().toFloat();
        g.setColour(c.inset); g.fillRoundedRectangle(b, 5);
        g.setColour(c.border); g.drawRoundedRectangle(b.reduced(0.5f), 5, 1);
        b.reduce(8, 8);
        if (kind == CreativeKind::granular) { granular(g, b, {}, c); return; }
        if (kind == CreativeKind::delay) { delay(g, b, {}, c); return; }
        text(g, b.removeFromTop(16), kind == CreativeKind::granular ? "ACTUAL GRAIN SNAPSHOT" : (enabled ? "PARAMETER MODEL" : "BYPASS | stored parameter model"), c.text);
        text(g, b.removeFromTop(15), kind == CreativeKind::granular
            ? (grainReady ? "Audio-thread state / no extrapolation" : "Waiting for current audio snapshot")
            : (ready ? "Effective targets / base if unassigned" : "Waiting for coherent parameter snapshot"), c.muted);
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
    static juce::String grainDescription() {
        return "Actual DSP grains, never extrapolated. Read zoom: percent of valid capture from oldest (0%) to newest (100%). "
               "Each envelope is a stable DSP slot; hover for its ID and exact values. Cursor: lifetime; brightness/height: envelope; "
               "arrow: playback direction; colour: stereo pan. Stable slot separation avoids fixed empty rows. "
               "The upper ring rail marks valid audio and capture head. Freeze holds capture, not grain playback.";
    }
    float value(P p) const { return values[size_t(p)]; }
    static juce::String number(double v, int decimals = 1) {
        return decimals == 0 ? juce::String(juce::roundToInt(v)) : juce::String(v, decimals);
    }
    static juce::String pitch(float v) { return (v > 0 ? "+" : "") + number(v) + " st"; }
    static void text(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, juce::Colour c) {
        g.setColour(c); g.setFont(juce::Font(juce::FontOptions(11.f)));
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
                g.fillEllipse(x - radius * 2, y - radius * 2, radius * 4, radius * 4);
                g.setColour(colour.withAlpha(.3f + .7f * weight));
                g.fillEllipse(x - radius, y - radius, radius * 2, radius * 2);
            };
            pulse(top, left, c.secondary); pulse(bottom, right, c.accent);
            const float nextLeft = feedback * (left * (1 - ping) + right * ping);
            right = feedback * (right * (1 - ping) + left * ping); left = nextLeft;
        }
        auto progress = juce::Rectangle<float>(r.getX(), r.getBottom() + 3, r.getWidth(), 2);
        g.setColour(c.border); g.fillRect(progress);
        g.setColour(c.text); g.fillRect(progress.withWidth(progress.getWidth() * float(delayPhase)));
    }
    void granular(juce::Graphics& g, juce::Rectangle<float> r, juce::Rectangle<float>, const ThemePalette& c) {
        text(g, r.removeFromTop(17), "GRAIN CLOUD / SNAPSHOT", c.text);
        if (!grainReady) {
            text(g, r.removeFromTop(18), "Awaiting current audio", c.muted);
            return;
        }
        const auto& frame = grainFrame.grains;
        const auto state = frame.frozen ? "FROZEN" : frame.capturing ? "CAPTURING" : "IDLE";
        text(g, r.removeFromTop(17), juce::String(state) + " / " + juce::String(frame.count) + " grains", frame.frozen ? c.accent : c.secondary);
        auto footer = r.removeFromBottom(49);
        auto rail = r.removeFromTop(6).reduced(0, 1);
        g.setColour(c.border); g.fillRoundedRectangle(rail, 2);
        const auto captureColour = frame.frozen ? c.accent : c.secondary;
        const float start = frame.captureHead - frame.validFraction;
        const auto segment = [&](float left, float right) {
            if (right <= left) return;
            g.setColour(captureColour);
            g.fillRect(rail.withX(rail.getX() + left * rail.getWidth()).withWidth((right - left) * rail.getWidth()));
        };
        if (start < 0) { segment(0, frame.captureHead); segment(1 + start, 1); }
        else segment(start, frame.captureHead);
        const float head = rail.getX() + frame.captureHead * rail.getWidth();
        g.setColour(c.text); g.drawLine(head, rail.getY() - 2, head, rail.getBottom() + 2, 1.5f);
        r.removeFromTop(8); r.removeFromBottom(4);
        g.setColour(c.border.withAlpha(.5f));
        g.drawHorizontalLine(int(r.getCentreY()), r.getX(), r.getRight());
        // Zoom only valid captured audio: ring coordinates unwrap at its oldest
        // sample. No grain is invented, culled or moved forward between frames.
        const float span = std::max(frame.validFraction, .000001f);
        const float oldest = frame.captureHead - frame.validFraction;
        const auto capturePosition = [&](const auto& grain) {
            float offset = grain.position - oldest;
            offset -= std::floor(offset);
            return std::clamp(offset / span, 0.f, 1.f);
        };
        float viewStart = 1, viewEnd = 0;
        for (uint32_t i = 0; i < frame.count; ++i) {
            const float position = capturePosition(frame.grains[i]);
            viewStart = std::min(viewStart, position); viewEnd = std::max(viewEnd, position);
        }
        // Frame the occupied read window, not eight seconds of mostly unused
        // ring. Axis labels remain coordinates within the valid capture span.
        const float padding = std::max(.025f, (viewEnd - viewStart) * .15f);
        viewStart = std::max(0.f, viewStart - padding);
        viewEnd = std::min(1.f, std::max(viewStart + .05f, viewEnd + padding));
        if (frame.count == 0) { viewStart = 0; viewEnd = 1; }
        uint32_t reverse = 0, holding = 0;
        const float halfWidth = r.getWidth() < 240 ? 8.f : 14.f;
        for (uint32_t i = 0; i < std::min<uint32_t>(frame.count, uint32_t(frame.grains.size())); ++i) {
            const auto& grain = frame.grains[i];
            const float position = (capturePosition(grain) - viewStart) / std::max(.000001f, viewEnd - viewStart);
            const float x = r.getX() + halfWidth + position * std::max(1.f, r.getWidth() - halfWidth * 2);
            // Stable slot separation keeps coincident positions readable, with
            // a small pan offset. Colour, not y, is the primary pan encoding.
            const float stagger = (float((grain.slot * 13) % 32) / 31.f - .5f) * .76f;
            const float y = r.getY() + 19.f + std::max(1.f, r.getHeight() - 38.f) * (.5f + grain.pan * .08f + stagger);
            grainPoints[grain.slot] = {x, y};
            const auto colour = grain.pan < 0 ? c.secondary : c.accent;
            const float envelope = std::clamp(grain.envelope, 0.f, 1.f);
            const float height = 6.f + 9.f * envelope;
            juce::Path window;
            window.startNewSubPath(x - halfWidth, y);
            for (int step = 1; step <= 20; ++step) {
                const float t = float(step) / 20;
                const float hann = .5f - .5f * std::cos(t * juce::MathConstants<float>::twoPi);
                window.lineTo(x - halfWidth + 2 * halfWidth * t, y - hann * height);
            }
            g.setColour(colour.withAlpha(.12f + .18f * envelope));
            g.fillPath(window);
            g.setColour(colour.withAlpha(.5f + .5f * envelope));
            g.strokePath(window, juce::PathStrokeType(1.2f));
            const float cursor = x - halfWidth + 2 * halfWidth * std::clamp(grain.progress, 0.f, 1.f);
            g.drawLine(cursor, y - height, cursor, y + 2, 1.f);
            const float direction = grain.step < 0 ? -1.f : 1.f;
            g.drawLine(x, y, x + direction * 5, y, 1);
            g.drawLine(x + direction * 5, y, x + direction * 3, y - 2, 1);
            g.drawLine(x + direction * 5, y, x + direction * 3, y + 2, 1);
            if (frame.count <= 8 || hoveredSlot == int(grain.slot))
                text(g, {x - halfWidth, y + 3, 2 * halfWidth + 3, 13}, juce::String(grain.slot + 1), colour);
            if (hoveredSlot == int(grain.slot)) {
                g.setColour(c.text); g.drawEllipse(x - 4, y - 4, 8, 8, 1);
            }
            if (grain.holdingBoundary) {
                g.setColour(c.text); g.drawEllipse(x - 3, y - 3, 6, 6, 1);
                ++holding;
            }
            if (grain.step < 0) ++reverse;
        }
        auto axisRow = footer.removeFromTop(16);
        text(g, axisRow, number(viewStart * 100, 0) + "%", c.muted);
        g.setColour(c.muted); g.drawText(number(viewEnd * 100, 0) + "%", axisRow, juce::Justification::centredRight);
        g.drawText("Read zoom", axisRow, juce::Justification::centred);
        text(g, footer.removeFromTop(16), juce::String(reverse) + " reverse / " + juce::String(holding) + " held", c.secondary);
        text(g, footer, "Valid " + number(frame.validFraction * 100, 1) + "% / pan colour", c.muted);
        if (frame.count == 0) text(g, r, frame.capturedFrames == 0 ? "Capture empty" : "No active grains", c.muted);
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
    RoutingGrainFrame grainFrame{};
    std::array<juce::Point<float>, 32> grainPoints{};
    int hoveredSlot = -1;
    bool grainReady = false;
    size_t slot;
    CreativeKind kind;
    std::array<float, creativeParameterCount> values = CreativeSettings{}.values;
    double tempo = 120, delayPhase = 0, lastAnimationMs = -1;
    float reduction = 0, mix = 100;
    bool enabled = true, neon = false, ready = false;
};

} // namespace vocalstack
