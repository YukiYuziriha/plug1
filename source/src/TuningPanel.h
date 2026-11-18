#pragma once

#include "DoublerPanel.h"
#include "ChoiceDropdown.h"
#include "TuningSettings.h"
#include "FxVisuals.h"
#include "ModulationUI.h"
#include <cmath>

namespace vocalstack {

// A view of processor parameters and telemetry; no detector or tuning state lives here.
class TuningPanel final : public juce::Component {
public:
    explicit TuningPanel(Processor& processor)
        : owner(processor), mix(owner, "tuningMix", "Module Mix"),
          root(owner, "tuningRoot", "Key", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}),
          scale(owner, "tuningScale", "Scale", {"Chromatic", "Major", "Natural Minor", "Harmonic Minor", "Major Pentatonic", "Minor Pentatonic", "Custom"}),
          reference(owner, "tuningReference", "Reference", "Hz"),
          range(owner, "tuningRange", "Vocal Range", {"Wide", "Low", "Mid", "High"}),
          amount(owner, "tuningAmount", "Correction"),
          retune(owner, "tuningRetune", "Retune Speed", "ms"),
          transition(owner, "tuningTransition", "Note Transition", "ms"),
          stability(owner, "tuningStability", "Note Stability") {
        setComponentID("tuningPanel");
        enabled.setButtonText("Tuning On");
        enabled.setComponentID("tuningEnabled");
        enabled.setClickingTogglesState(true);
        enabled.setTooltip("Enable shared pitch correction. Host audio reactivation may be required to reserve tuning latency. Module Mix blends clean and tuned Lead before the per-voice Tune Mix.");
        formants.setButtonText("Preserve Formants");
        formants.setComponentID("tuningFormants");
        formants.setModulationOwner(owner);
        formants.setClickingTogglesState(true);
        formants.setTooltip("Preserve vocal spectral character during correction; separate from creative voice formants.");
        enabledLink = std::make_unique<ButtonLink>(owner.parameters, "tuningEnabled", enabled);
        formantsLink = std::make_unique<ButtonLink>(owner.parameters, "tuningFormants", formants);
        reset.setButtonText("Reset Tuning");
        reset.setComponentID("resetTuning");
        reset.setTooltip("Reset only the shared tuning module. Per-voice Tune Mix settings are retained.");
        reset.onClick = [this] {
            for (const auto* id : {"tuningEnabled", "tuningMix", "tuningRoot", "tuningScale", "tuningNotes",
                                  "tuningReference", "tuningRange", "tuningAmount", "tuningRetune",
                                  "tuningTransition", "tuningStability", "tuningFormants"}) {
                if (auto* p = owner.parameters.getParameter(id)) {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->getDefaultValue());
                    p->endChangeGesture();
                }
            }
            sync(false);
        };
        for (auto* button : std::array<juce::TextButton*, 3>{&enabled, &formants, &reset}) addAndMakeVisible(*button);
        for (auto* knob : controls()) addAndMakeVisible(*knob);
        for (auto* dropdown : choices()) addAndMakeVisible(*dropdown);
        percent(mix); percent(amount); percent(stability);
        mix.setLinear(true);
        mix.slider.setTooltip("Module dry/wet: clean Lead to shared tuned Lead. Separate from correction strength and each voice's Tune Mix.");
        amount.slider.setTooltip("Pitch correction strength, not an audio dry/wet mix.");
        retune.slider.setTooltip("Correction response in ms. Zero adds no correction slew, not zero algorithmic latency.");
        transition.slider.setTooltip("Target-note glide in ms. Zero gives stepped target changes.");
        stability.slider.setTooltip("Target-note hysteresis and dwell: reduce unwanted switching near note boundaries.");
        reference.slider.textFromValueFunction = [](double v) { return juce::String(v, 1) + " Hz"; };
        reference.slider.updateText();

        for (size_t i = 0; i < notes.size(); ++i) {
            auto& button = notes[i];
            button.setButtonText(noteNames[i]);
            button.setComponentID("tuningNote" + juce::String(int(i)));
            button.setName("Allow " + juce::String(noteNames[i]));
            button.setTooltip("Edit the base pitch-class mask and switch to Custom. Toggles show base notes; LIVE ALLOWED reports the effective modulated note count. No live allowed notes means no correction.");
            button.onClick = [this, i] {
                const auto mask = baseMask() ^ (1u << i);
                setParameter("tuningNotes", float(mask));
                setParameter("tuningScale", float(int(TuningScale::Custom)));
                sync(false);
            };
            addAndMakeVisible(button);
        }
        status.setComponentID("tuningPitchReadout");
        status.setFont(juce::Font(pixelFontOptions(16.f)));
        status.setJustificationType(juce::Justification::centredLeft);
        status.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(status);
        detail::passiveControl(*this);
        sync(false);
    }

    void sync(bool recordHistory = true) {
        syncFromSnapshot(owner.getModulationSnapshot(), recordHistory);
    }

private:
    friend struct EffectiveUiArchitectureTests;
    void syncFromSnapshot(ModulationSnapshot next, bool recordHistory) {
        // Match the visuals: failed bounded reads retain the last coherent state.
        if (next.consistent) modulationSnapshot = std::move(next);
        const bool on = value("tuningEnabled") >= .5f;
        mix.setEnabled(on);
        mix.setAlpha(on ? 1.f : .35f);
        const auto mask = baseMask();
        for (size_t i = 0; i < notes.size(); ++i)
            notes[i].setToggleState((mask & (1u << i)) != 0, juce::dontSendNotification);
        const bool liveOn = operationalValue("tuningEnabled") >= .5f;
        const auto liveMask = operationalMask();
        const auto telemetry = owner.getTuningTelemetry();
        const bool voiced = liveOn && telemetry.voiced && std::isfinite(telemetry.detectedMidi)
            && std::isfinite(telemetry.detectedHz) && telemetry.detectedHz > 0;
        const bool target = voiced && telemetry.hasTarget && std::isfinite(telemetry.targetMidi);
        juce::String text = !liveOn ? "TUNING OFF" : !voiced ? "INPUT  --   /   Waiting for a voiced signal" :
            "INPUT  " + pitchName(telemetry.detectedMidi) + "  /  " + juce::String(telemetry.detectedHz, 1) + " Hz";
        text += "\nTARGET  " + (target ? pitchName(telemetry.targetMidi) : juce::String("--"));
        text += "     CORRECTION  " + (target && std::isfinite(telemetry.correctionSemitones)
            ? juce::String(int(std::round(telemetry.correctionSemitones * 100.f))) + " ct" : juce::String("--"));
        text += "\nCONFIDENCE  " + (voiced && std::isfinite(telemetry.confidence)
            ? juce::String(int(std::round(juce::jlimit(0.f, 1.f, telemetry.confidence) * 100.f))) + "%" : juce::String("--"));
        int allowedCount = 0;
        for (size_t i = 0; i < notes.size(); ++i) if (liveMask & (1u << i)) ++allowedCount;
        text += "\nLIVE ALLOWED  " + juce::String(allowedCount) + " / 12";
        if (liveOn && liveMask == 0) text += "\nNo allowed notes: no correction";
        if (liveOn && owner.routingRestartPending()) text += "\nHost audio restart pending";
        status.setText(text, juce::dontSendNotification);
        if (recordHistory) {
            history[historyPosition] = {telemetry.detectedMidi, telemetry.targetMidi, voiced, target};
            historyPosition = (historyPosition + 1) % history.size();
        }
        repaint(12, 558, 520, 104);
    }

public:
    void setNeon(bool neon) {
        const auto p = themePalette(*this, neon);
        for (auto* knob : controls()) knob->setNeon(neon);
        for (auto* dropdown : choices()) dropdown->setNeon(neon);
        for (auto* button : std::array<juce::TextButton*, 3>{&enabled, &formants, &reset}) styleButton(*button, p);
        for (auto& button : notes) styleButton(button, p);
        status.setColour(juce::Label::textColourId, p.text);
        detail::passiveControl(*this);
        repaint();
    }

    void resized() override {
        enabled.setBounds(460, 4, 100, 30);
        mix.setBounds(570, 0, 180, 58);
        reset.setBounds(770, 4, 120, 30);
        root.setBounds(12, 126, 106, 64);
        scale.setBounds(130, 126, 220, 64);
        reference.setBounds(368, 104, 140, 124);
        range.setBounds(640, 126, 230, 64);
        for (size_t i = 0; i < notes.size(); ++i) notes[i].setBounds(14 + int(i) * 47, 238, 42, 30);
        amount.setBounds(14, 330, 140, 154);
        retune.setBounds(160, 330, 140, 154);
        transition.setBounds(306, 330, 140, 154);
        stability.setBounds(452, 330, 140, 154);
        formants.setBounds(632, 340, 250, 34);
        status.setBounds(548, 558, 344, 102);
    }

    void paint(juce::Graphics& g) override {
        const auto p = themePalette(*this);
        auto text = [&](juce::String s, juce::Rectangle<int> r, float size, juce::Colour colour) {
            g.setColour(colour); g.setFont(juce::Font(pixelFontOptions(size)));
            g.drawText(s, r, juce::Justification::centredLeft);
        };
        text("PITCH CORRECTION", {4, 0, 420, 30}, 23.f, p.text);
        text("One shared tuner. Clean + tuned Lead feed every voice.", {4, 34, 450, 24}, 13.f, p.muted);
        const auto section = [&](juce::Rectangle<int> r, const char* title) {
            g.setColour(p.panel); g.fillRect(r);
            g.setColour(p.border); g.drawRect(r);
            g.setColour(p.accent); g.fillRect(r.getX(), r.getY(), r.getWidth(), 2);
            text(title, r.withHeight(30).reduced(12, 0), 14.f, p.accent);
        };
        section({0, 70, 606, 214}, "TARGET NOTES / BASE");
        section({618, 70, 286, 214}, "DETECTION");
        section({0, 302, 606, 202}, "TUNING CHARACTER");
        section({618, 302, 286, 202}, "VOICE CHARACTER");
        section({0, 522, 904, 152}, "LIVE PITCH MONITOR");
        text("Single vocal input", {636, 238, 250, 24}, 14.f, p.muted);
        text("Separate from creative formant shifts.", {636, 390, 260, 28}, 13.f, p.muted);
        text("Zero ms = no added slew, not zero latency.", {632, 434, 268, 48}, 12.f, p.muted);
        const juce::Rectangle<float> plot(16.f, 560.f, 510.f, 98.f);
        g.setColour(p.inset); g.fillRect(plot);
        const auto& latest = history[(historyPosition + history.size() - 1) % history.size()];
        const float centre = latest.voiced ? std::round(latest.input) : 60.f;
        for (int offset = -2; offset <= 2; ++offset) {
            const float y = plot.getCentreY() - float(offset) * plot.getHeight() / 6.f;
            g.setColour(p.border); g.drawHorizontalLine(int(y), plot.getX(), plot.getRight());
        }
        // Gaps are genuine unvoiced snapshots, never interpolated detector data.
        for (int trace = 0; trace < 2; ++trace) {
            juce::Path path;
            bool connected = false;
            for (size_t i = 0; i < history.size(); ++i) {
                const auto& sample = history[(historyPosition + i) % history.size()];
                if (!(trace == 0 ? sample.voiced : sample.targeted)) { connected = false; continue; }
                const float midi = trace == 0 ? sample.input : sample.target;
                const float x = plot.getX() + float(i) * plot.getWidth() / float(history.size() - 1);
                const float y = juce::jlimit(plot.getY(), plot.getBottom(), plot.getCentreY() - (midi - centre) * plot.getHeight() / 6.f);
                if (connected) path.lineTo(x, y); else path.startNewSubPath(x, y);
                connected = true;
            }
            g.setColour(trace == 0 ? p.muted : p.secondary);
            g.strokePath(path, juce::PathStrokeType(1.5f));
        }
        text("Input / Target   |   recent pitch snapshots, +/-3 semitones", {16, 660, 510, 14}, 11.f, p.muted);
        text("Per-voice Tune Mix is in each Stack / Routing voice panel, including Lead.",
             {4, 692, 896, 28}, 14.f, p.muted);
    }

private:
    using ButtonLink = juce::AudioProcessorValueTreeState::ButtonAttachment;
    static constexpr std::array<const char*, 12> noteNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    std::array<ParameterKnob*, 6> controls() { return {&mix, &reference, &amount, &retune, &transition, &stability}; }
    std::array<ChoiceDropdown*, 3> choices() { return {&root, &scale, &range}; }
    static void percent(ParameterKnob& knob) {
        knob.slider.textFromValueFunction = [](double v) { return juce::String(v * 100, 0) + " %"; };
        knob.slider.valueFromTextFunction = [](const juce::String& s) { return s.getDoubleValue() / 100.; };
        knob.slider.updateText();
    }

    template <typename Palette> static void styleButton(juce::TextButton& button, const Palette& p) {
        button.setColour(juce::TextButton::textColourOffId, p.text);
        button.setColour(juce::TextButton::textColourOnId, p.background);
    }
    float value(const char* id) const {
        if (const auto* p = owner.parameters.getRawParameterValue(id)) return p->load();
        return 0;
    }
    void setParameter(const char* id, float v) {
        if (auto* p = owner.parameters.getParameter(id)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture();
        }
    }
    float operationalValue(const char* id) const {
        return toneui::visualParameter(owner, id, 0, &modulationSnapshot);
    }
    unsigned operationalMask() const {
        TuningSettings settings;
        settings.root = int(operationalValue("tuningRoot"));
        settings.scale = TuningScale(int(operationalValue("tuningScale")));
        settings.allowedNotesMask = std::uint16_t(operationalValue("tuningNotes"));
        return tuningNoteMask(settings);
    }
    unsigned baseMask() const {
        TuningSettings settings;
        settings.root = int(value("tuningRoot"));
        settings.scale = TuningScale(int(value("tuningScale")));
        settings.allowedNotesMask = std::uint16_t(value("tuningNotes"));
        return tuningNoteMask(settings);
    }
    static juce::String pitchName(float midi) {
        const int note = int(std::round(juce::jlimit(-100.f, 200.f, midi)));
        return juce::String(noteNames[size_t((note % 12 + 12) % 12)]) + juce::String(int(std::floor(note / 12.0)) - 1)
            + "  " + juce::String(int(std::round((midi - float(note)) * 100.f))) + " ct";
    }
    struct PitchSample { float input = 0, target = 0; bool voiced = false, targeted = false; };
    std::array<PitchSample, 180> history{};
    size_t historyPosition = 0;
    Processor& owner;
    ParameterKnob mix;
    ChoiceDropdown root, scale;
    ParameterKnob reference;
    ChoiceDropdown range;
    ParameterKnob amount, retune, transition, stability;
    juce::TextButton enabled, reset;
    modulationui::Button formants;
    ModulationSnapshot modulationSnapshot;
    std::array<juce::TextButton, 12> notes;
    juce::Label status;
    std::unique_ptr<ButtonLink> enabledLink, formantsLink;
};

} // namespace vocalstack
