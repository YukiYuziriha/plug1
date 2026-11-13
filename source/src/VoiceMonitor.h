#pragma once

#include "PluginProcessor.h"
#include "VisualTheme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>
#include <optional>

namespace vocalstack {

// Snapshot-only view: never reads the engine or synthesizes modulation.
class VoiceMonitor final : public juce::Component {
public:
    static constexpr size_t leadSlot = 8;
    std::function<void(size_t)> onSelect;
    void setNeon(bool value) { neon = value; repaint(); }
    void select(size_t slot) { if (slot <= leadSlot) selected = slot; repaint(); }
    void update(const std::array<VoiceSnapshot, 8>& values,
                std::optional<VoiceSnapshot> lead = std::nullopt) {
        for (size_t i = 0; i < values.size(); ++i) snapshots[i] = values[i];
        snapshots[leadSlot] = lead.value_or(VoiceSnapshot{});
        for (size_t i = 0; i < snapshots.size(); ++i) {
            auto& trail = trails[i];
            if (!snapshots[i].present || !snapshots[i].enabled || snapshots[i].activity < 0.00001f) {
                trail.clear();
                continue;
            }
            trail.push_back({snapshots[i].pan, snapshots[i].detuneCents});
            if (trail.size() > 24) trail.pop_front();
        }
        repaint();
    }
    void paint(juce::Graphics& g) override {
        const auto palette = themePalette(*this, neon);
        const bool device = componentTheme(*this, neon) == Theme::device;
        g.fillAll(device ? juce::Colour(0xff202421) : palette.panel);
        auto plot = plotBounds();
        g.setFont(juce::Font(pixelFontOptions(11.0f)));
        g.setColour(device ? juce::Colour(0xffaebaca) : palette.muted);
        g.drawText("VOICE MOTION   /   fine detune +/-150 ct", 10, 2, 330, 18, juce::Justification::centredLeft);
        const auto axis = plot.toNearestInt().withHeight(14).translated(0, -15);
                g.drawText("L", axis, juce::Justification::centredLeft);
                g.drawText("C", axis, juce::Justification::centred);
                g.drawText("R", axis, juce::Justification::centredRight);
        g.setColour(device ? juce::Colour(0xff46515b) : palette.border);
        g.drawRect(plot, 1.0f);
        g.drawHorizontalLine(juce::roundToInt(plot.getCentreY()), plot.getX(), plot.getRight());
        g.drawVerticalLine(juce::roundToInt(plot.getCentreX()), plot.getY(), plot.getBottom());
        for (size_t i = 0; i < snapshots.size(); ++i) {
            const auto& s = snapshots[i];
            if (!s.present) continue;
            const auto colour = device ? voiceColour(i) : i == leadSlot ? palette.secondary : palette.accent;
            const auto& trail = trails[i];
            for (size_t j = 1; j < trail.size(); ++j) {
                g.setColour(colour.withAlpha(0.04f + 0.28f * float(j) / float(trail.size())));
                g.drawLine({point(trail[j - 1]), point(trail[j])}, 1.4f);
            }
            const auto p = point({s.pan, s.detuneCents});
            const float activity = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(s.activity, -72.0f) + 60.0f) / 48.0f);
            g.setColour(colour.withAlpha(s.enabled ? 0.28f + 0.72f * activity : 0.18f));
            if (i == leadSlot) g.fillRect(p.x - 7, p.y - 7, 14.0f, 14.0f);
            else g.fillEllipse(p.x - 7, p.y - 7, 14, 14);
            if (i == selected) {
                g.setColour(device ? juce::Colours::white : palette.text);
                g.drawEllipse(p.x - 9, p.y - 9, 18, 18, 1.0f);
            }
            g.setColour(device ? juce::Colours::white
                : s.enabled && activity > 0.5f ? palette.background : palette.text);
            g.drawText(i == leadSlot ? juce::String("L") : juce::String(int(i + 1)),
                int(p.x - 7), int(p.y - 7), 14, 14, juce::Justification::centred);
        }
        const auto& s = snapshots[selected];
        g.setColour(device ? juce::Colour(0xffe4e9ee) : palette.text);
        const auto info = (selected == leadSlot ? juce::String("Lead") : "V" + juce::String(int(selected + 1)))
            + (s.present ? (s.enabled ? "   LIVE" : "   OFF") : "   ABSENT")
            + "   Pitch " + juce::String(s.pitchSemitones, 2) + " st   |   Fine " + juce::String(s.detuneCents, 1)
            + " ct   |   Extra delay " + juce::String(s.delayMs, 1) + " ms   |   Formant "
            + juce::String(s.formantSemitones, 2) + " st   |   Gain " + juce::String(s.gainDb, 1) + " dB";
        g.drawText(info, 10, getHeight() - 21, getWidth() - 20, 19, juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        float closest = 18.0f;
        // Prefer the topmost marker when Lead and a generated voice overlap.
        for (size_t i = snapshots.size(); i-- > 0;) {
            if (!snapshots[i].present) continue;
            const auto distance = point({snapshots[i].pan, snapshots[i].detuneCents}).getDistanceFrom(e.position);
            if (distance < closest) { closest = distance; selected = i; }
        }
        if (closest < 18 && onSelect) onSelect(selected);
        repaint();
    }
    static juce::Colour voiceColour(size_t i) {
        if (i == leadSlot) return juce::Colour(0xffffd580);
        return juce::Colour::fromHSV(float(i) / 8.0f + 0.04f, 0.55f, 0.95f, 1.0f);
    }
private:
    juce::Rectangle<float> plotBounds() const { return {44.0f, 32.0f, float(getWidth() - 88), float(getHeight() - 60)}; }
    juce::Point<float> point(juce::Point<float> value) const {
        const auto p = plotBounds().reduced(10.0f, 8.0f);
        return {p.getCentreX() + juce::jlimit(-1.0f, 1.0f, value.x) * p.getWidth() * 0.5f,
                p.getCentreY() - juce::jlimit(-150.0f, 150.0f, value.y) / 150.0f * p.getHeight() * 0.5f};
    }
    bool neon = false;
    size_t selected = 0;
    std::array<VoiceSnapshot, 9> snapshots{};
    std::array<std::deque<juce::Point<float>>, 9> trails;
};
} // namespace vocalstack
