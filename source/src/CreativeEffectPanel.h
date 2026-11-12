#pragma once

#include "CreativeEffects.h"
#include "DoublerPanel.h"
#include "ParameterKnob.h"
#include "ModulationUI.h"
#include "CreativeEffectVisual.h"
#include <algorithm>
#include <cmath>
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
                slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 82, 20);
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
            textEditing = e.eventComponent != &slider && slider.owner
                && slider.owner->modulationAssignmentId() == 0 && !e.mods.isPopupMenu();
            if (textEditing && slider.beginEdit) slider.beginEdit();
        }
        void mouseUp(const juce::MouseEvent&) override {
            if (textEditing && slider.endEdit) slider.endEdit();
            textEditing = false;
        }
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
            forwardPassiveWheel(*this, e, wheel);
        }
        void resized() override {
            auto area = getLocalBounds().reduced(1, 0);
            label.setJustificationType(juce::Justification::centred);
            label.setBounds(area.removeFromBottom(28));
            slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, juce::jmin(82, area.getWidth()), 20);
            slider.setBounds(area);
            // Keep two-state controls button-sized, rather than filling a knob cell.
            toggle.setBounds(getLocalBounds().withSizeKeepingCentre(juce::jmin(58, getWidth()), 28));
        }
        int preferredWidth() const { return buttonLink ? 60 : 84; }
        bool textEditing = false;
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
        overview.setFont(juce::Font(juce::FontOptions(12.0f)));
        overview.setMinimumHorizontalScale(1.f);
        overview.setBorderSize(juce::BorderSize<int>(0));
        overview.setJustificationType(juce::Justification::topLeft);
        overview.setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(overview);
        content.addAndMakeVisible(visual);
        addGroup("BLEND");
        addRow(fxParameterId(slot, "Mix"), "Mix", "True dry/wet blend for this FX slot: 0% dry, 100% wet.", "%", false);
        groups.back().rows.back()->slider.setSliderStyle(juce::Slider::LinearHorizontal);
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
        viewport.onScroll = [this] { if (!layingOut) rememberScroll(); };
        setNeon(false); detail::passiveControl(*this); updateState(); startTimerHz(20);
    }
    ~CreativeEffectPanel() override {
        stopTimer(); viewport.onScroll = nullptr; viewport.setViewedComponent(nullptr, false);
    }
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
            case CreativeKind::delay: return "Stereo repeats with tempo sync, coloured feedback and pitch. Ducking keeps repeats behind the vocal.\nBeats use quarter-note multiples: 0.5 = eighth, 1 = quarter.";
            case CreativeKind::reverb: return "Shape early reflections, tail length and tone in a diffuse stereo space. Ducking keeps the tail behind the vocal.\nFreeze holds the tail; Mix blends dry and wet.";
            case CreativeKind::compressor: return "Stereo-linked compression with a soft knee and filtered level detector.\nMakeup is manual output gain. Mix blends dry and compressed audio for parallel compression.";
            case CreativeKind::bitcrusher: return "Reduce bit depth and sample rate for digital texture. Drive colours the input; dither decorrelates quantization distortion.\nFilter the output, then blend with Mix.";
            case CreativeKind::granular: return "Layer grains from recent audio. Shape grain length, density, pitch and stereo spread; add timing scatter.\nFreeze holds captured audio, not grain playback. The graph is a stable parameter guide, not live audio.";
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
        const juce::ScopedValueSetter<bool> guard(layingOut, true);
        viewport.setBounds(getLocalBounds());
        // Always reserve the scrollbar, avoiding threshold-dependent reflow.
        const int width = juce::jmax(1, getWidth() - viewport.getScrollBarThickness());
        const int innerWidth = juce::jmax(1, width - 16);
        const bool split = innerWidth >= 680;
        const int visualWidth = split ? juce::jlimit(270, 440, innerWidth * 2 / 5) : innerWidth;
        const int visualHeight = split ? 254 : (width < 280 ? 216 : 238);
        visual.setBounds(8, 8, visualWidth, visualHeight);
        juce::AttributedString descriptionText;
        descriptionText.append(overview.getText(), overview.getFont());
        juce::TextLayout descriptionLayout;
        descriptionLayout.createLayout(descriptionText, float(visualWidth));
        const int overviewHeight = juce::jmax(32, int(std::ceil(descriptionLayout.getHeight())) + 6);
        overview.setBounds(8, visual.getBottom() + 10, visualWidth, overviewHeight);
        int visualBottom = overview.getBottom() + 8;
        if (kind == CreativeKind::compressor) {
            meter.setBounds(8, visualBottom, visualWidth, 28);
            visualBottom = meter.getBottom() + 8;
        }
        const int controlsX = split ? visual.getRight() + 16 : 8;
        const int controlsWidth = split ? juce::jmax(1, width - controlsX - 8) : innerWidth;
        int y = split ? 8 : visualBottom;
        for (auto& group : groups) {
            group.heading->setBounds(controlsX, y, controlsWidth, 20);
            y += 22;
            if (&group == &groups.front()) {
                group.rows.front()->setBounds(controlsX, y, controlsWidth, 64);
                y += 70;
                continue;
            }
            int x = 0, lineHeight = 0;
            for (auto& row : group.rows) {
                const int cellWidth = juce::jmin(controlsWidth, row->preferredWidth());
                const int cellHeight = row->buttonLink ? 44 : 100;
                if (x > 0 && x + cellWidth > controlsWidth) {
                    y += lineHeight + 2; x = 0; lineHeight = 0;
                }
                row->setBounds(controlsX + x, y, cellWidth, cellHeight);
                x += cellWidth + 2;
                lineHeight = juce::jmax(lineHeight, cellHeight);
            }
            y += lineHeight + 10;
        }
        content.setSize(width, juce::jmax(getHeight(), juce::jmax(visualBottom, y)));
        // Retain the preferred anchor even if a wide/tall view temporarily has
        // no scroll range. Returning to a sidebar restores this instance's place.
        viewport.setViewPosition(0, scrollAnchor ? scrollAnchor->getY() + scrollOffset : scrollY);
    }
private:
    class InspectorViewport final : public PassiveViewport {
    public:
        std::function<void()> onScroll;
        void visibleAreaChanged(const juce::Rectangle<int>&) override { if (onScroll) onScroll(); }
    };
    void rememberScroll() {
        scrollY = viewport.getViewPositionY();
        scrollAnchor = nullptr;
        for (auto& group : groups) for (auto& row : group.rows)
            if (row->getY() <= scrollY && (!scrollAnchor || row->getY() > scrollAnchor->getY()))
                scrollAnchor = row.get();
        scrollOffset = scrollAnchor ? scrollY - scrollAnchor->getY() : 0;
    }
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
    InspectorViewport viewport;
    bool layingOut = false;
    Row* scrollAnchor = nullptr;
    int scrollY = 0, scrollOffset = 0;
    juce::Label overview, meter;
    std::vector<Group> groups;
    Row* timeRow = nullptr;
    Row* beatsRow = nullptr;
};

} // namespace vocalstack
