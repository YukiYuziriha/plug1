#pragma once

#include "PluginProcessor.h"
#include "Equalizer.h"
#include "FxVisuals.h"
#include "ModulationUI.h"
#include "ParameterKnob.h"
#include "VisualTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace vocalstack {

// Defined by the processor parameter layer; no fallback IDs or DSP access here.
juce::String toneParameterId(size_t slot, const char* field);
juce::String eqBandParameterId(size_t slot, size_t band, const char* field);

namespace toneui {
inline float value(Processor& owner, const juce::String& id, float fallback = 0) {
    if (const auto* p = owner.parameters.getRawParameterValue(id)) {
        const auto v = p->load(std::memory_order_relaxed);
        if (std::isfinite(v)) return v;
    }
    return fallback;
}
inline void write(Processor& owner, const juce::String& id, float actual) {
    if (auto* p = owner.parameters.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(actual));
}
inline void edit(Processor& owner, const juce::String& id, float actual) {
    if (auto* p = owner.parameters.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(actual));
        p->endChangeGesture();
    }
}
inline juce::Colour bandColour(size_t band) {
    constexpr std::array<juce::uint32, 8> colours{
        0xfff5a24b, 0xfff0cf65, 0xff9ed872, 0xff64d5ba,
        0xff6eb9ef, 0xffa99afa, 0xffed8ec6, 0xffee8b82};
    return juce::Colour(colours[band % colours.size()]);
}
inline juce::Colour bandColour(size_t band, const juce::Component& component, bool legacyNeon = false) {
    if (componentTheme(component, legacyNeon) == Theme::device) return bandColour(band);
    const auto p = themePalette(component, legacyNeon);
    return p.accent.interpolatedWith(p.secondary, float(band % 8) / 7.0f);
}

inline const std::array<const char*, 14> modeLabels{
    "Bell", "Low shelf 12 dB/oct", "High shelf 12 dB/oct",
    "Low shelf 24 dB/oct", "High shelf 24 dB/oct",
    "Low cut 6 dB/oct", "Low cut 12 dB/oct", "Low cut 24 dB/oct", "Low cut 48 dB/oct",
    "High cut 6 dB/oct", "High cut 12 dB/oct", "High cut 24 dB/oct", "High cut 48 dB/oct", "Notch"};

// Requested response and interactive bands are independent of the captured FFT.
// Left axis: EQ gain dB. Right axis: stereo-power spectrum peak amplitude dBFS.
class EqGraph final : public FxMonitorView, public juce::SettableTooltipClient {
public:
    EqGraph(Processor& processor, size_t fxSlot) : FxMonitorView(processor, fxSlot, true), owner(processor), slot(fxSlot) {
        setName("EQ response graph");
        setTitle("EQ response and live stereo spectrum: click to add, drag bands, Alt-wheel for Q");
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTooltipText();
        refresh(true);
    }
    ~EqGraph() override { finishGesture(); }
    std::function<void(size_t)> onSelect;
    void select(size_t band) { selected = juce::jmin(size_t(7), band); rebuildPaths(); repaint(); }
    void resized() override { rebuildPaths(); }
    void visibilityChanged() override {
        FxMonitorView::visibilityChanged();
        if (!isShowing()) { finishGesture(); pendingClick = false; pendingMode = -1; }
    }
    void parentHierarchyChanged() override {
        FxMonitorView::parentHierarchyChanged();
        if (!isShowing()) { finishGesture(); pendingClick = false; pendingMode = -1; }
    }
    void refresh(bool force = false) {
        EqualizerSettings next;
        next.enabled = value(owner, fxParameterId(slot, "On"), 1) >= 0.5f;
        next.shiftSemitones = value(owner, toneParameterId(slot, "EqShift"));
        next.gainDb = value(owner, toneParameterId(slot, "EqGain"));
        next.adaptiveQ = value(owner, toneParameterId(slot, "EqAdaptiveQ")) >= 0.5f;
        bool changed = next.enabled != settings.enabled || next.shiftSemitones != settings.shiftSemitones
            || next.gainDb != settings.gainDb || next.adaptiveQ != settings.adaptiveQ;
        for (size_t b = 0; b < next.bands.size(); ++b) {
            auto& band = next.bands[b];
            band.enabled = value(owner, eqBandParameterId(slot, b, "On")) >= 0.5f;
            band.frequency = value(owner, eqBandParameterId(slot, b, "Frequency"), 1000);
            band.gainDb = value(owner, eqBandParameterId(slot, b, "Gain"));
            band.q = value(owner, eqBandParameterId(slot, b, "Q"), 0.707f);
            band.mode = juce::jlimit(0, 13, int(std::lround(value(owner, eqBandParameterId(slot, b, "Mode")))));
            const auto& old = settings.bands[b];
            changed |= band.enabled != old.enabled || band.frequency != old.frequency
                || band.gainDb != old.gainDb || band.q != old.q || band.mode != old.mode;
        }
        double nextRate = owner.getSampleRate();
        if (!std::isfinite(nextRate) || nextRate < 8000 || nextRate > 768000) nextRate = 48000;
        changed |= nextRate != sampleRate;
        settings = next;
        sampleRate = nextRate;
        if (changed || force) rebuildPaths();
        // The panel's UI timer owns feedback reads; paint never requests audio state.
        auto snapshot = owner.getModulationSnapshot();
        if (snapshot.consistent) {
            modulationSnapshot = std::move(snapshot);
            modulationState = owner.getModulationState();
            const auto assigning = owner.modulationAssignmentId();
            feedbackSource = assigning ? assigning : modulationui::selectedModulator(owner);
            repaint();
        } else if (changed || force) repaint();
    }
    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        const auto palette = themePalette(*this, neon);
        const bool device = componentTheme(*this, neon) == Theme::device;
        g.setColour(device ? juce::Colour(0xff1c211f) : palette.inset);
        g.fillRoundedRectangle(bounds, device ? 5.0f : 0.0f);
        const auto area = plot();
        if (area.getWidth() < 10 || area.getHeight() < 10) return;
        g.setFont(juce::Font(pixelFontOptions(10.0f)));
        for (float hz : {20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f}) {
            const auto x = xFor(hz);
            g.setColour(device ? juce::Colour(0xff809398).withAlpha(0.13f) : palette.border.withAlpha(0.65f));
            g.drawVerticalLine(int(x), area.getY(), area.getBottom());
            const bool narrow = getWidth() < 260;
            if (narrow ? (hz == 100 || hz == 10000)
                       : (hz == 20 || hz == 100 || hz == 1000 || hz == 10000)) {
                g.setColour(device ? juce::Colour(0xffa8b3bb) : palette.muted);
                g.drawText(hz < 1000 ? juce::String(int(hz)) : juce::String(int(hz / 1000)) + "k",
                           int(x) - 12, int(area.getBottom()) + 3, 30, 14, juce::Justification::centred);
            }
        }
        g.setColour(device ? juce::Colour(0xffa8b3bb) : palette.muted);
        g.drawText("Hz", 1, int(area.getBottom()) + 3, 27, 14, juce::Justification::centredRight);
        paintSpectrum(g, area, true);
        paintSpectrumScale(g, area);
        for (int db : {-24, -12, 0, 12, 24}) {
            const auto y = yFor(float(db));
            g.setColour((device ? juce::Colour(0xffa8b3bb) : palette.muted).withAlpha(db == 0 ? 0.45f : 0.14f));
            g.drawHorizontalLine(int(y), area.getX(), area.getRight());
            g.setColour(device ? juce::Colour(0xffa8b3bb) : palette.muted);
            g.drawText((db > 0 ? "+" : "") + juce::String(db), 1, int(y) - 6, 27, 13, juce::Justification::centredRight);
        }
        {
            juce::Graphics::ScopedSaveState clip(g);
            g.reduceClipRegion(area.toNearestInt());
            g.setColour(bandColour(selected, *this, neon).withAlpha(0.65f));
            g.strokePath(bandPath, juce::PathStrokeType(1.1f));
            g.setColour((device ? palette.accent : palette.secondary).withAlpha(settings.enabled ? 1.0f : 0.4f));
            g.strokePath(totalPath, juce::PathStrokeType(2.0f));
        }
        for (size_t b = 0; b < settings.bands.size(); ++b) {
            if (!settings.bands[b].enabled) continue;
            const auto p = bandPoint(b);
            const auto colour = bandColour(b, *this, neon);
            paintModulation(g, b, p);
            g.setColour(colour.withAlpha(b == selected ? 0.22f : 0.1f));
            g.fillEllipse(p.x - 12, p.y - 12, 24, 24);
            g.setColour(colour);
            g.fillEllipse(p.x - 7, p.y - 7, 14, 14);
            if (b == selected) {
                g.setColour(device ? juce::Colour(0xffeef2ee) : palette.text);
                g.drawEllipse(p.x - 9, p.y - 9, 18, 18, 1.3f);
            }
            g.setColour(juce::Colour(0xff151b20));
            g.drawText(juce::String(int(b + 1)), int(p.x) - 7, int(p.y) - 7, 14, 14, juce::Justification::centred);
        }
        g.setColour(device ? juce::Colour(0xff7e9297).withAlpha(0.5f) : palette.border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), device ? 5.0f : 0.0f, 1);
        g.setColour(device ? juce::Colour(0xffa8b3bb) : palette.muted);
        auto title = getLocalBounds().removeFromTop(18).reduced(7, 0);
        g.drawText("FFT dBFS", title.removeFromRight(62), juce::Justification::centredRight);
        const juce::String status = getWidth() < 260 ? (!settings.enabled ? "BYPASS" : "EQ dB")
            : !settings.enabled ? "EQ dB | BYPASS | IN / OUT FFT"
            : isStreamStalled() ? "EQ dB | fading last FFT"
            : hasLiveFrame() ? "EQ dB | IN dim / OUT bright FFT" : "EQ dB | no live audio";
        g.drawText(status, title, juce::Justification::centredLeft);
    }
    void mouseMove(const juce::MouseEvent& e) override {
        const bool onBand = hit(e.position) >= 0;
        const int mode = edgeMode(e.position);
        const juce::String next = owner.modulationAssignmentId() != 0
            ? (onBand ? "Assign: drag X = frequency depth, Y = gain depth (bell/shelves); Alt-wheel = Q depth. Signed normalized range; base unchanged."
                      : "Assign: drag an existing band or Alt-wheel over it for Q depth. Adding bands is disabled.")
            : onBand ? "Drag: frequency / gain | Alt-wheel: Q"
            : mode == 13 ? "Click: bell | drag inward: notch"
            : mode == 1 || mode == 2 ? "Click: bell | drag curve end: shelf"
            : mode >= 0 ? "Click: bell | drag edge: cut" : "Click empty space: add bell";
        if (hoverHint != next) { hoverHint = next; setTooltip(next); repaint(); }
        setMouseCursor(onBand ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    }
    void mouseExit(const juce::MouseEvent&) override { hoverHint.clear(); repaint(); }
    void mouseDown(const juce::MouseEvent& e) override {
        finishGesture();
        pendingMode = -1;
        pendingClick = false;
        if (!e.mods.isLeftButtonDown() || e.getNumberOfClicks() > 1) return;
        refresh();
        const int found = hit(e.position);
        dragAssignment = owner.modulationAssignmentId();
        if (found >= 0) {
            choose(size_t(found)); dragBand = found;
            if (dragAssignment) {
                const auto state = owner.getModulationState();
                dragFrequencyDepth = depthFor(state, dragAssignment, eqBandParameterId(slot, size_t(found), "Frequency"));
                dragGainDepth = depthFor(state, dragAssignment, eqBandParameterId(slot, size_t(found), "Gain"));
                dragStart = e.position;
            }
            return;
        }
        if (dragAssignment) return;
        pendingMode = edgeMode(e.position);
        pendingClick = plot().contains(e.position);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!e.mods.isLeftButtonDown() || e.getDistanceFromDragStart() < 3) return;
        pendingClick = false;
        if (dragAssignment) {
            if (owner.modulationAssignmentId() != dragAssignment || dragBand < 0) return;
            const auto b = size_t(dragBand);
            const auto frequencyId = eqBandParameterId(slot, b, "Frequency");
            const auto gainId = eqBandParameterId(slot, b, "Gain");
            assignDepth(dragAssignment, frequencyId, dragFrequencyDepth
                + normalized(frequencyId, unshiftedFrequency(e.position.x))
                - normalized(frequencyId, unshiftedFrequency(dragStart.x)));
            if (hasGain(settings.bands[b].mode))
                assignDepth(dragAssignment, gainId, dragGainDepth
                    + normalized(gainId, gainFor(e.position.y)) - normalized(gainId, gainFor(dragStart.y)));
            refresh();
            return;
        }
        // Entering Assign during an ordinary drag must never continue base edits.
        if (owner.modulationAssignmentId()) { finishGesture(); pendingMode = -1; return; }
        if (dragBand < 0 && pendingMode >= 0) {
            dragBand = create(pendingMode, e.position);
            pendingMode = -1;
        }
        if (dragBand < 0) return;
        if (gestures.empty()) {
            begin(eqBandParameterId(slot, size_t(dragBand), "Frequency"));
            if (hasGain(settings.bands[size_t(dragBand)].mode))
                begin(eqBandParameterId(slot, size_t(dragBand), "Gain"));
        }
        moveBand(size_t(dragBand), e.position);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        const bool add = !dragAssignment && !owner.modulationAssignmentId()
            && pendingClick && e.getNumberOfClicks() == 1
            && e.getDistanceFromDragStart() < 3 && plot().contains(e.position);
        finishGesture(); pendingMode = -1; pendingClick = false;
        if (add) {
            refresh();
            const int found = hit(e.position);
            if (found >= 0) choose(size_t(found)); else create(0, e.position);
        }
    }
    // The first mouseUp already added/selected a band. Never duplicate it on
    // the second click, even when the first click filled the last free slot.
    void mouseDoubleClick(const juce::MouseEvent&) override { pendingClick = false; }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        // In cards/inspectors an unmodified wheel is navigation, even over a
        // handle or at a scroll boundary. Keep standalone graph Q compatibility.
        if (!e.mods.isAltDown()) {
            for (auto* parent = getParentComponent(); parent != nullptr; parent = parent->getParentComponent()) {
                if (dynamic_cast<juce::Viewport*>(parent) != nullptr) {
                    forwardPassiveWheel(*this, e, wheel);
                    return;
                }
            }
        }
        const int found = hit(e.position);
        if (found < 0 || wheel.deltaY == 0 || !gestures.empty()) return;
        choose(size_t(found));
        const auto id = eqBandParameterId(slot, selected, "Q");
        if (const auto source = owner.modulationAssignmentId()) {
            assignDepth(source, id, depthFor(owner.getModulationState(), source, id) + wheel.deltaY * 0.1f);
            refresh();
            return;
        }
        // Do not turn a cancelled assignment drag into a base-value wheel edit.
        if (dragAssignment) return;
        const auto q = value(owner, id, 0.707f);
        // A wheel event is a complete, balanced host gesture, including trackpads.
        edit(owner, id, juce::jlimit(0.1f, 18.0f, q * std::exp(wheel.deltaY * 2.0f)));
        refresh();
    }
private:
    void setTooltipText() {
        // Component is deliberately not a keyboard command target.
        const juce::String instructions = "Single-click empty space: bell. Click a band: select. Drag a curve end: shelf. "
            "Drag a side edge: cut. Drag bottom edge: notch. Wheel: scroll enclosing card. Alt-wheel on a band: Q (plain wheel also edits Q in standalone graphs). "
            "Assign mode: drag an existing band horizontally for frequency depth, vertically for gain depth (bell/shelves); "
            "Alt-wheel over a band for Q depth. Depth is signed normalized target range; base values stay unchanged. "
            "No bands are added in Assign mode. Solid numbered handle: requested; outlined dot: effective; purple/pink lines: selected source range. "
            "Left scale: requested EQ gain dB. Right: live FFT peak dBFS, Hann window, stereo power. "
            "Input dim, output bright; FFT is independent of the +/-24 dB response scale.";
        setDescription(instructions);
        setTooltip(instructions);
    }
    static float depthFor(const ModulationState& state, uint32_t source, const juce::String& id) {
        for (const auto& a : state.assignments)
            if (a.modulatorId == source && a.targetParameterId == id) return a.depth;
        return 0;
    }
    float normalized(const juce::String& id, float actual) const {
        if (const auto* parameter = owner.parameters.getParameter(id)) return parameter->convertTo0to1(actual);
        return 0;
    }
    void assignDepth(uint32_t source, const juce::String& id, float depth) {
        owner.setModulationDepth(source, id, juce::jlimit(-1.0f, 1.0f, depth));
    }
    float effectiveActual(const juce::String& id, float fallback) const {
        if (const auto* parameter = owner.parameters.getParameter(id))
            for (const auto& target : modulationSnapshot.targets)
                if (target.parameterId == id) return parameter->convertFrom0to1(target.effectiveNormalized);
        return fallback;
    }
    float shiftedFrequency(float frequency, float shift) const {
        return float(juce::jlimit(10.0, sampleRate * 0.45, double(frequency) * std::exp2(double(shift) / 12.0)));
    }
    void paintModulation(juce::Graphics& g, size_t b, juce::Point<float> base) const {
        const auto frequencyId = eqBandParameterId(slot, b, "Frequency");
        const auto gainId = eqBandParameterId(slot, b, "Gain");
        bool assigned = false;
        for (const auto& a : modulationState.assignments)
            if (a.enabled && (a.targetParameterId == frequencyId || a.targetParameterId == gainId
                || a.targetParameterId == eqBandParameterId(slot, b, "Q")
                || a.targetParameterId == eqBandParameterId(slot, b, "Mode")
                || a.targetParameterId == toneParameterId(slot, "EqShift"))) assigned = true;
        bool bipolar = true;
        float sourceDepth = 1;
        for (const auto& m : modulationState.modulators)
            if (m.id == feedbackSource) { bipolar = m.bipolar; sourceDepth = m.depth; break; }
        const auto drawRange = [&](const juce::String& id, float actual, bool horizontal) {
            for (const auto& a : modulationState.assignments) {
                if (!a.enabled || a.modulatorId != feedbackSource || a.targetParameterId != id) continue;
                const auto* parameter = owner.parameters.getParameter(id);
                if (!parameter) return;
                const float centre = parameter->convertTo0to1(actual);
                const float depth = a.depth * sourceDepth;
                const float lo = bipolar ? centre - std::abs(depth) : centre + juce::jmin(0.0f, depth);
                const float hi = bipolar ? centre + std::abs(depth) : centre + juce::jmax(0.0f, depth);
                // Frequency depth is parameter-normalized, NOT log-plot distance.
                const auto coordinate = [&](float n) {
                    const float v = parameter->convertFrom0to1(juce::jlimit(0.0f, 1.0f, n));
                    return horizontal ? xFor(shiftedFrequency(v, settings.shiftSemitones)) : yFor(v);
                };
                const float start = coordinate(lo), end = coordinate(hi);
                const auto palette = themePalette(*this, neon);
                g.setColour(componentTheme(*this, neon) == Theme::device
                    ? juce::Colour(a.depth < 0 ? 0xffff96c8 : 0xffb798ff)
                    : a.depth < 0 ? palette.secondary : palette.accent);
                if (horizontal) {
                    g.drawLine(start, base.y, end, base.y, 2.5f);
                    g.drawLine(start, base.y - 4, start, base.y + 4, 1);
                    g.drawLine(end, base.y - 4, end, base.y + 4, 1);
                } else {
                    g.drawLine(base.x, start, base.x, end, 2.5f);
                    g.drawLine(base.x - 4, start, base.x + 4, start, 1);
                    g.drawLine(base.x - 4, end, base.x + 4, end, 1);
                }
                return;
            }
        };
        drawRange(frequencyId, settings.bands[b].frequency, true);
        if (hasGain(settings.bands[b].mode)) drawRange(gainId, settings.bands[b].gainDb, false);
        if (!assigned) return;
        const auto frequency = effectiveActual(frequencyId, settings.bands[b].frequency);
        const auto gain = effectiveActual(gainId, settings.bands[b].gainDb);
        const auto shift = effectiveActual(toneParameterId(slot, "EqShift"), settings.shiftSemitones);
        const int mode = int(std::lround(effectiveActual(eqBandParameterId(slot, b, "Mode"), float(settings.bands[b].mode))));
        const juce::Point<float> effective{xFor(shiftedFrequency(frequency, shift)), yFor(hasGain(mode) ? gain : 0.0f)};
        g.setColour(bandColour(b, *this, neon).withAlpha(0.65f));
        g.drawLine(base.x, base.y, effective.x, effective.y, 1);
        g.setColour(bandColour(b, *this, neon));
        g.drawEllipse(effective.x - 4, effective.y - 4, 8, 8, 1.5f);
    }
    int edgeMode(juce::Point<float> p) const {
        const auto area = plot();
        if (!area.expanded(8).contains(p)) return -1;
        if (p.y >= area.getBottom() - 10) return 13;
        if (p.x <= area.getX() + 12 || p.x >= area.getRight() - 12) {
            const bool low = p.x < area.getCentreX();
            const float edgeDb = responseDb(low ? 20.0 : 20000.0, settings);
            return std::abs(p.y - yFor(edgeDb)) < 14 ? (low ? 1 : 2) : (low ? 6 : 10);
        }
        return -1;
    }
    juce::Rectangle<float> plot() const {
        return getLocalBounds().toFloat().withTrimmedLeft(32).withTrimmedRight(34)
            .withTrimmedTop(24).withTrimmedBottom(23);
    }
    float xFor(float hz) const {
        return plot().getX() + plot().getWidth() * std::log(juce::jlimit(20.0f, 20000.0f, hz) / 20.0f) / std::log(1000.0f);
    }
    float frequencyFor(float x) const {
        const auto a = plot();
        return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, (x - a.getX()) / juce::jmax(1.0f, a.getWidth())));
    }
    float yFor(float db) const {
        return plot().getCentreY() - juce::jlimit(-24.0f, 24.0f, db) * plot().getHeight() / 48.0f;
    }
    float gainFor(float y) const {
        return juce::jlimit(-24.0f, 24.0f, (plot().getCentreY() - y) * 48.0f / juce::jmax(1.0f, plot().getHeight()));
    }
    static bool hasGain(int mode) { return mode >= 0 && mode <= 4; }
    juce::Point<float> bandPoint(size_t b) const {
        const auto& band = settings.bands[b];
        const auto shifted = juce::jlimit(10.0, sampleRate * 0.45,
            double(band.frequency) * std::exp2(double(settings.shiftSemitones) / 12.0));
        return {xFor(float(shifted)), yFor(hasGain(band.mode) ? band.gainDb : 0.0f)};
    }
    int hit(juce::Point<float> p) const {
        if (settings.bands[selected].enabled && p.getDistanceFrom(bandPoint(selected)) < 13) return int(selected);
        for (size_t b = 0; b < settings.bands.size(); ++b)
            if (settings.bands[b].enabled && p.getDistanceFrom(bandPoint(b)) < 11) return int(b);
        return -1;
    }
    void choose(size_t b) { selected = b; if (onSelect) onSelect(b); rebuildPaths(); repaint(); }
    int create(int mode, juce::Point<float> position) {
        if (owner.modulationAssignmentId()) return -1;
        refresh();
        for (size_t b = 0; b < settings.bands.size(); ++b) {
            if (settings.bands[b].enabled) continue;
            edit(owner, eqBandParameterId(slot, b, "Mode"), float(mode));
            edit(owner, eqBandParameterId(slot, b, "Q"), 0.707f);
            edit(owner, eqBandParameterId(slot, b, "Frequency"), unshiftedFrequency(position.x));
            edit(owner, eqBandParameterId(slot, b, "Gain"), hasGain(mode) ? gainFor(position.y) : 0);
            edit(owner, eqBandParameterId(slot, b, "On"), 1);
            refresh();
            choose(b);
            return int(b);
        }
        return -1;
    }
    float unshiftedFrequency(float x) const {
        return juce::jlimit(20.0f, 20000.0f, frequencyFor(x) / std::exp2(settings.shiftSemitones / 12.0f));
    }
    void moveBand(size_t b, juce::Point<float> p) {
        write(owner, eqBandParameterId(slot, b, "Frequency"), unshiftedFrequency(p.x));
        if (hasGain(settings.bands[b].mode)) write(owner, eqBandParameterId(slot, b, "Gain"), gainFor(p.y));
        refresh();
    }
    void begin(const juce::String& id) {
        if (auto* p = owner.parameters.getParameter(id)) { p->beginChangeGesture(); gestures.push_back(p); }
    }
    void finishGesture() {
        for (auto* p : gestures) p->endChangeGesture();
        gestures.clear();
        dragBand = -1;
        dragAssignment = 0;
    }
    float responseDb(double frequency, const EqualizerSettings& s) const {
        const auto magnitude = Equalizer::frequencyResponse(frequency, sampleRate, s);
        return float(20.0 * std::log10(juce::jmax(1.0e-12, magnitude)));
    }
    void rebuildPaths() {
        totalPath.clear(); bandPath.clear();
        const auto a = plot();
        if (a.getWidth() < 1 || a.getHeight() < 1) return;
        EqualizerSettings single;
        single.shiftSemitones = settings.shiftSemitones;
        single.adaptiveQ = settings.adaptiveQ;
        single.bands[selected] = settings.bands[selected];
        const int steps = juce::jlimit(64, 900, int(a.getWidth()));
        for (int i = 0; i <= steps; ++i) {
            const auto x = a.getX() + a.getWidth() * float(i) / float(steps);
            const auto f = frequencyFor(x);
            const auto y = yFor(responseDb(f, settings));
            const auto bandY = yFor(responseDb(f, single));
            if (i == 0) { totalPath.startNewSubPath(x, y); bandPath.startNewSubPath(x, bandY); }
            else { totalPath.lineTo(x, y); bandPath.lineTo(x, bandY); }
        }
    }
    Processor& owner;
    size_t slot, selected = 0;
    EqualizerSettings settings;
    ModulationSnapshot modulationSnapshot;
    ModulationState modulationState;
    uint32_t feedbackSource = 0, dragAssignment = 0;
    float dragFrequencyDepth = 0, dragGainDepth = 0;
    juce::Point<float> dragStart;
    double sampleRate = 48000;
    bool pendingClick = false;
    int dragBand = -1, pendingMode = -1;
    std::vector<juce::RangedAudioParameter*> gestures;
    juce::String hoverHint;
    juce::Path totalPath, bandPath;
};
} // namespace toneui
} // namespace vocalstack
