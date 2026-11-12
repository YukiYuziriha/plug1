#pragma once

#include "CreativeEffects.h"
#include "DoublerPanel.h"
#include "ParameterKnob.h"
#include "ModulationUI.h"
#include "CreativeEffectVisual.h"
#include <functional>
#include <memory>
#include <vector>

namespace vocalstack {

// Slot identities and parameter ranges belong to the processor, never the view.
class CreativeEffectPanel final : public juce::Component, private juce::Timer {
    class Slider final : public ModulationSlider {
    public:
        std::function<void()> beginEdit, endEdit;
        void mouseDown(const juce::MouseEvent& e) override {
            editing = !assigning();
            if (editing && beginEdit) beginEdit();
            ModulationSlider::mouseDown(e);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            ModulationSlider::mouseUp(e);
            if (editing && endEdit) endEdit();
            editing = false;
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override {
            if (!assigning() && beginEdit) beginEdit();
            ModulationSlider::mouseDoubleClick(e);
            if (!assigning() && endEdit) endEdit();
        }
        Processor* owner = nullptr;
    private:
        bool assigning() const { return owner && owner->modulationAssignmentId() != 0; }
        bool editing = false;
    };
    class Toggle final : public modulationui::Button {
    public:
        std::function<void()> beginEdit, endEdit;
        void mouseDown(const juce::MouseEvent& e) override {
            editing = owner && owner->modulationAssignmentId() == 0 && !e.mods.isPopupMenu();
            if (editing && beginEdit) beginEdit();
            modulationui::Button::mouseDown(e);
        }
        void mouseUp(const juce::MouseEvent& e) override {
            modulationui::Button::mouseUp(e);
            if (editing && endEdit) endEdit();
            editing = false;
        }
        Processor* owner = nullptr;
    private:
        bool editing = false;
    };
    struct Row final : juce::Component {
        Row(Processor& owner, const juce::String& id, const juce::String& name,
            const juce::String& tooltip, const char* unit, bool discrete) {
            setComponentID(id + "Control");
                        slider.addMouseListener(this, true);
            label.setText(name, juce::dontSendNotification);
            label.setFont(juce::Font(juce::FontOptions(11.5f)));
            label.setMinimumHorizontalScale(1.f);
            label.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(label);
            if (discrete) {
                toggle.owner = &owner; toggle.setModulationOwner(owner);
                toggle.setComponentID(id); toggle.setButtonText(name);
                toggle.setClickingTogglesState(true); toggle.setTooltip(tooltip);
                addAndMakeVisible(toggle);
                buttonLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, id, toggle);
                label.setVisible(false);
            } else {
                slider.owner = &owner; slider.bindModulation(owner, id);
                slider.setName(name); slider.setTitle(name); slider.setTooltip(tooltip);
                slider.setLookAndFeel(&style);
                slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 82, 21);
                addAndMakeVisible(slider);
                sliderLink = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, id, slider);
                if (auto* p = owner.parameters.getParameter(id))
                    slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
                const juce::String suffix(unit);
                slider.textFromValueFunction = [suffix](double v) {
                    if (suffix == "Hz" && v >= 1000) return juce::String(v / 1000, 2) + " kHz";
                    if (suffix == "beats")
                        return juce::String(v, 4).trimCharactersAtEnd("0").trimCharactersAtEnd(".") + " beats";
                    const int decimals = suffix == "%" || suffix == "Hz" ? 0 : 2;
                    return juce::String(v, decimals) + (suffix == ":1" ? suffix : " " + suffix);
                };
                slider.valueFromTextFunction = [suffix](const juce::String& text) {
                    return text.getDoubleValue() * (suffix == "Hz" && text.containsIgnoreCase("k") ? 1000.0 : 1.0);
                };
                slider.updateText();
            }
        }
        ~Row() override { slider.removeMouseListener(this); slider.setLookAndFeel(nullptr); }
                void mouseDown(const juce::MouseEvent& e) override {
                    // Numeric entry lives in the slider's child Label/TextEditor, not
                    // Slider::mouseDown. Capture its pre-edit value without taking keys.
                    if (e.eventComponent != &slider && slider.owner && slider.owner->modulationAssignmentId() == 0)
                        if (slider.beginEdit) slider.beginEdit();
                }
                void mouseUp(const juce::MouseEvent& e) override {
                    if (e.eventComponent != &slider && slider.endEdit) slider.endEdit();
                }
        void resized() override {
            auto area = getLocalBounds().reduced(2, 1);
            label.setJustificationType(juce::Justification::centred);
            label.setFont(juce::Font(juce::FontOptions(getWidth() < 100 ? 11.f : 11.5f)));
            label.setBounds(area.removeFromTop(26));
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, juce::jmin(82, area.getWidth()), 20);
            slider.setBounds(area);
            toggle.setBounds(getLocalBounds().reduced(2, 8));
        }
        void setNeon(bool neon) {
            const auto palette = themePalette(*this, neon);
            const auto accent = palette.accent;
            label.setColour(juce::Label::textColourId, palette.text);
            slider.setColour(juce::Slider::trackColourId, accent);
            slider.setColour(juce::Slider::thumbColourId, palette.text);
            slider.setColour(juce::Slider::textBoxTextColourId, palette.text);
            slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            toggle.setColour(juce::TextButton::buttonColourId, palette.inset);
            toggle.setColour(juce::TextButton::buttonOnColourId, accent);
            toggle.setColour(juce::TextButton::textColourOffId, palette.text);
            toggle.setColour(juce::TextButton::textColourOnId, palette.background);
        }
        ParameterKnob::Style style;
        juce::Label label;
        Slider slider;
        Toggle toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderLink;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonLink;
    };
    struct Group {
        std::unique_ptr<juce::Label> heading;
        std::vector<std::unique_ptr<Row>> rows;
    };
public:
    CreativeEffectPanel(Processor& processor, size_t fxSlot, CreativeKind effectKind)
        : owner(processor), slot(fxSlot), kind(effectKind), visual(processor, fxSlot, effectKind) {
        setName(effectName(kind) + " inspector");
        overview.setText(description(kind), juce::dontSendNotification);
        overview.setFont(juce::Font(juce::FontOptions(11.5f)));
        overview.setJustificationType(juce::Justification::topLeft);
        overview.setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(overview);
        content.addAndMakeVisible(visual);
        addGroup("BLEND");
        addRow(fxParameterId(slot, "Mix"), "Dry / Wet", "True dry/wet blend for this FX slot.", "%", false);
        const char* previous = "";
        for (size_t i = 0; i < creativeParameterCount; ++i) {
            const auto& d = creativeParameters[i];
            if (d.kind != kind) continue;
            const auto* section = groupFor(static_cast<CreativeParameter>(i));
            if (juce::String(section) != previous) { addGroup(section); previous = section; }
            const auto id = creativeParameterId(slot, i);
            addRow(id, d.label, parameterTip(static_cast<CreativeParameter>(i)), d.unit, d.discrete);
            if (i == size_t(CreativeParameter::delayTime)) timeRow = groups.back().rows.back().get();
            if (i == size_t(CreativeParameter::delayBeats)) beatsRow = groups.back().rows.back().get();
        }
        meter.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        meter.setTooltip("Actual gain reduction reported by the compressor DSP; not an estimated transfer curve.");
        content.addChildComponent(meter); meter.setVisible(kind == CreativeKind::compressor);
        viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        setNeon(false); detail::passiveControl(*this); updateState(); startTimerHz(20);
    }
    ~CreativeEffectPanel() override { stopTimer(); viewport.setViewedComponent(nullptr, false); }
    // Called before base-value edits; assignment gestures are not base edits.
    std::function<void(const juce::String&)> onBeginEdit;
    std::function<void()> onEndEdit;
        std::function<void()> onValueChanged;
    static juce::String effectName(CreativeKind k) {
        switch (k) {
            case CreativeKind::delay: return "Delay";
            case CreativeKind::reverb: return "Reverb";
            case CreativeKind::compressor: return "Compressor";
            case CreativeKind::bitcrusher: return "Bitcrusher";
            case CreativeKind::granular: return "Granular";
        }
        return {};
    }
    static juce::String description(CreativeKind k) {
        switch (k) {
            case CreativeKind::delay: return "Stereo repeats with tempo sync, feedback colour, pitch and vocal ducking. Beats are quarter-note multiples.";
            case CreativeKind::reverb: return "Diffuse stereo space: shape early reflections, the decay and its colour; duck the tail behind the vocal.";
            case CreativeKind::compressor: return "Stereo-linked dynamics with a soft knee and filtered detector. Mix blends parallel compression; makeup is manual.";
            case CreativeKind::bitcrusher: return "Bit-depth and sample-rate reduction with input drive, dither and output filtering.";
            case CreativeKind::granular: return "Grains from recent audio, with pitch, reverse, timing scatter and stereo spread. Freeze holds the capture buffer.";
        }
        return {};
    }
    void setNeon(bool enabled) {
        neon = enabled;
        overview.setColour(juce::Label::textColourId, themePalette(*this, neon).muted);
        visual.setNeon(neon);
        meter.setColour(juce::Label::textColourId, accent());
        for (auto& group : groups) {
            group.heading->setColour(juce::Label::textColourId, accent());
            for (auto& row : group.rows) row->setNeon(neon);
        }
        repaint();
    }
    void revealParameter(const juce::String& id) {
        for (auto& group : groups) for (auto& row : group.rows)
            if (row->slider.getComponentID() == id || row->toggle.getComponentID() == id)
                viewport.setViewPosition(0, juce::jmax(0, row->getY() - 26));
    }
    void paint(juce::Graphics& g) override {
        g.setColour(themePalette(*this, neon).panel);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5);
    }
    void resized() override {
        viewport.setBounds(getLocalBounds());
        // Reserve scrollbar width consistently so crossing the height threshold
        // cannot reflow the grid back and forth.
        const int width = juce::jmax(1, getWidth() - viewport.getScrollBarThickness());
        const int columns = juce::jlimit(1, 6, (width - 16) / 76);
        const int cell = juce::jmax(1, (width - 16) / columns);
        int y = 8;
        const bool compact = width < 280;
        const int visualHeight = compact ? 216 : 238;
        visual.setBounds(8, y, juce::jmax(1, width - 16), visualHeight); y += visualHeight + 8;
        const int overviewHeight = compact ? 76 : 44;
        overview.setBounds(8, y, juce::jmax(1, width - 16), overviewHeight); y += overviewHeight + 6;
        if (kind == CreativeKind::compressor) { meter.setBounds(8, y, width - 16, 28); y += 34; }
        for (auto& group : groups) {
            group.heading->setBounds(8, y, width - 16, 23); y += 25;
            for (size_t i = 0; i < group.rows.size(); ++i)
                group.rows[i]->setBounds(8 + int(i % size_t(columns)) * cell,
                    y + int(i / size_t(columns)) * 100, cell, 98);
            y += int((group.rows.size() + size_t(columns) - 1) / size_t(columns)) * 100 + 8;
        }
        content.setSize(width, juce::jmax(getHeight(), y));
    }
private:
    juce::Colour accent() const { return themePalette(*this, neon).accent; }
    void addGroup(const char* name) {
        Group group;
        group.heading = std::make_unique<juce::Label>();
        group.heading->setText(name, juce::dontSendNotification);
        group.heading->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        group.heading->setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(*group.heading); groups.push_back(std::move(group));
    }
    void addRow(const juce::String& id, const juce::String& name, const juce::String& tip, const char* unit, bool discrete) {
        auto row = std::make_unique<Row>(owner, id, name, tip, unit, discrete);
        const auto begin = [this, id] { if (onBeginEdit) onBeginEdit(id); };
        const auto end = [this] { if (onEndEdit) onEndEdit(); updateState(); };
        row->slider.beginEdit = begin; row->slider.endEdit = end;
                row->slider.onValueChange = [this] { if (onValueChanged) onValueChanged(); };
        row->toggle.beginEdit = begin; row->toggle.endEdit = end;
        content.addAndMakeVisible(*row); groups.back().rows.push_back(std::move(row));
    }
    static const char* groupFor(CreativeParameter p) {
        using P = CreativeParameter;
        if (p == P::reverbPitch || p == P::reverbPitchAmount || p == P::reverbSwell) return "PITCH & SWELL";
        if (p <= P::delayPingPong) return "TIMING & REPEATS";
        if (p <= P::delayPitch) return "COLOUR & DUCKING";
        if (p <= P::reverbEarly) return "SPACE & DECAY";
        if (p <= P::reverbModulation) return "TONE & WIDTH";
        if (p <= P::reverbFreeze) return "TAIL & DUCKING";
        if (p <= P::compressorKnee) return "DYNAMICS";
        if (p <= P::compressorSidechainHP) return "ENVELOPE & LEVEL";
        if (p <= P::crusherDither) return "DIGITAL TEXTURE";
        if (p <= P::granularReverse) return "GRAINS & CAPTURE";
        return "COLOUR & SCATTER";
    }
    static juce::String parameterTip(CreativeParameter p) {
        using P = CreativeParameter;
        switch (p) {
            case P::delaySync: return "Use host tempo and the Beats control instead of milliseconds.";
            case P::delayBeats: return "Quarter-note multiples: 0.25 = sixteenth, 0.5 = eighth, 0.75 = dotted eighth, 1 = quarter. Long times are limited to the delay buffer.";
            case P::delayTime: return "Free-running repeat interval in milliseconds; inactive when Tempo sync is on.";
            case P::delayFeedback: return "Amount returned to the delay input. Higher values sustain more repeats.";
            case P::delayPingPong: return "Cross-feedback between left and right repeat channels.";
            case P::delayPitch: return "Pitch shift applied to repeats, in semitones.";
            case P::reverbDecay: return "Nominal midband RT60 in seconds. Damping changes high-frequency decay.";
            case P::reverbPitch: return "Pitch shift inside reverb feedback. The parameter schematic is not a measured impulse response.";
            case P::reverbPitchAmount: return "Blend of pitched feedback, not an additional wet voice. Freeze disables the pitched branch.";
            case P::reverbSwell: return "Signal-dependent wet attack softens onset without moving predelay taps. The graphic is an onset guide, not an exact envelope prediction.";
            case P::reverbFreeze: return "Hold the reverb tail; disable to return to normal decay.";
            case P::reverbModulation: return "Tail delay modulation depth in milliseconds.";
            case P::compressorSidechainHP: return "High-pass filter in the level detector, not the audible signal.";
            case P::compressorMakeup: return "Manual output gain after compression. No automatic level matching.";
            case P::crusherDither: return "TPDF noise added before quantization to decorrelate distortion.";
            case P::granularFreeze: return "Hold captured audio while grains continue playing.";
            case P::granularReverse: return "Play grains backwards through the captured audio.";
            case P::granularPosition: return "How far behind the live input grains read from captured audio.";
            default: break;
        }
        const auto& d = creativeParameters[size_t(p)];
        return juce::String(d.label) + " | " + juce::String(d.min) + " to " + juce::String(d.max)
            + " " + d.unit + ". Drag to adjust; double-click resets. In Assign mode, drag to set modulation depth.";
    }
    void updateState() {
        visual.refresh();
        if (kind == CreativeKind::delay) {
            // Control dependencies must update even when the visual is scrolled
            // out of view. Keep its heavy model/animation refresh visibility-gated.
            const auto snapshot = owner.getModulationSnapshot();
            if (snapshot.consistent) {
                const auto index = size_t(CreativeParameter::delaySync);
                const bool synced = toneui::visualParameter(owner, creativeParameterId(slot, index),
                    creativeParameters[index].initial, &snapshot) >= .5f;
                if (timeRow) timeRow->setEnabled(!synced);
                if (beatsRow) beatsRow->setEnabled(synced);
            }
        }
        if (kind == CreativeKind::compressor) {
            const float reduction = owner.getCreativeGainReduction(slot);
            meter.setText("GAIN REDUCTION   " + juce::String(std::isfinite(reduction) ? juce::jmax(0.0f, reduction) : 0.0f, 1) + " dB", juce::dontSendNotification);
        }
    }
    void timerCallback() override {
        // refresh() also resets the schematic clock while viewport-clipped or
        // hidden; resuming must not integrate time spent outside the view.
        if (isShowing()) updateState();
        else visual.refresh();
    }
    Processor& owner;
    size_t slot;
    CreativeKind kind;
    bool neon = false;
    CreativeEffectVisual visual;
    juce::Component content;
    PassiveViewport viewport;
    juce::Label overview, meter;
    std::vector<Group> groups;
    Row* timeRow = nullptr;
    Row* beatsRow = nullptr;
};

} // namespace vocalstack
