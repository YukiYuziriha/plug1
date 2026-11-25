#include "VisualTheme.h"
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
    class Choice final : public modulationui::ComboBox {
    public:
        void resized() override {
            modulationui::ComboBox::resized();
            for (auto* child : getChildren()) if (auto* text = dynamic_cast<juce::Label*>(child)) {
                text->setFont(juce::Font(pixelFontOptions(12.f))); text->setMinimumHorizontalScale(1.f);
            }
        }
        void lookAndFeelChanged() override {
            modulationui::ComboBox::lookAndFeelChanged();
            detail::passiveControl(*this);
        }
    };
    struct Row final : juce::Component {
        Row(Processor& owner, const juce::String& id, const juce::String& name,
            const juce::String& tooltip, const char* unit, bool discrete,
            const juce::StringArray& choices) {
            setComponentID(id + "Control");
            slider.addMouseListener(this, true);
            label.setText(name, juce::dontSendNotification);
            label.setFont(juce::Font(pixelFontOptions(ParameterKnob::controlFontSize)));
            label.setMinimumHorizontalScale(1.f);
            label.setInterceptsMouseClicks(false, false);
            addAndMakeVisible(label);
            if (!choices.isEmpty()) {
                choice.addItemList(choices, 1);
                choice.setComponentID(id); choice.setName(name); choice.setTitle(name); choice.setTooltip(tooltip);
                choice.setModulationOwner(owner); choice.setScrollWheelEnabled(false);
                addAndMakeVisible(choice);
                if (auto* parameter = owner.parameters.getParameter(id)) {
                    choiceLink = std::make_unique<juce::ParameterAttachment>(*parameter, [this](float value) {
                        choice.setSelectedId(1 + juce::roundToInt(value), juce::dontSendNotification);
                    }, nullptr);
                    choiceLink->sendInitialUpdate();
                }
            } else if (discrete) {
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
            if (compactChoiceWidth > 0) {
                label.setJustificationType(juce::Justification::centredLeft);
                label.setBounds(area.removeFromTop(18));
                choice.setBounds(area.removeFromTop(28));
                return;
            }
            label.setJustificationType(juce::Justification::centred);
            const bool textured = slider.getSliderStyle() == juce::Slider::LinearHorizontal
                && bool(slider.getProperties()["vocalstackTexturedBar"]);
            label.setVisible(!buttonLink && !textured);
            const bool inspectorRotary = inspectorLayout && sliderLink && !textured;
            slider.getProperties().set("vocalstackInspectorRotary", inspectorRotary);
            slider.getProperties().set("vocalstackCellRotary", sliderLink && !textured);
            if (!textured) label.setBounds(area.removeFromBottom(sliderLink ? captionHeight(getWidth()) : 28));
            slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, juce::jmin(82, area.getWidth()), 20);
            slider.setBounds(area);
            if (inspectorRotary) for (auto* child : slider.getChildren())
                if (auto* value = dynamic_cast<juce::Label*>(child)) value->setMinimumHorizontalScale(1.f);
            choice.setBounds(area.withSizeKeepingCentre(area.getWidth(), 28));
            // Keep two-state controls button-sized, rather than filling a knob cell.
            toggle.setBounds(getLocalBounds().withSizeKeepingCentre(juce::jmin(toggle.getComponentID().endsWith("saturator_level_match") ? 90 : 58, getWidth()), 28));
        }
        int captionHeight(int width) const {
            juce::AttributedString text; text.append(label.getText(), themeFont(label, ParameterKnob::controlFontSize));
            juce::TextLayout layout;
            layout.createLayout(text, float(juce::jmax(1, width - 10)));
            return juce::jmax(16, int(std::ceil(layout.getHeight())) + 2);
        }
        int inspectorHeight(int width) const {
            if (choiceLink) return 60;
            if (buttonLink) return 44;
            return 22 + juce::jmin(96, juce::jmax(1, width - 2)) + captionHeight(width);
        }
        int preferredWidth() const {
            if (choiceLink) return 200;
            if (buttonLink) return toggle.getComponentID().endsWith("saturator_level_match") ? 92 : 60;
            return slider.getComponentID().endsWith("convolution_brightness") ? 100 : 82;
        }
        bool textEditing = false, inspectorLayout = false;
        int compactChoiceWidth = 0;
        void setNeon(bool neon) {
            const auto palette = themePalette(*this, neon);
            const auto accent = palette.accent;
            modulationui::styleCombo(choice, palette);
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
            if (!getLocalBounds().isEmpty()) resized();
        }
        ParameterKnob::Style style;
        juce::Label label;
        Slider slider;
        Toggle toggle;
        Choice choice;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderLink;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonLink;
        std::unique_ptr<juce::ParameterAttachment> choiceLink;
    };
    struct Group {
        enum class Layout { standard, compactChoices, glitchAuto };
        Layout layout = Layout::standard;
        std::unique_ptr<juce::Label> heading;
        std::vector<std::unique_ptr<Row>> rows;
    };
public:
    CreativeEffectPanel(Processor& processor, size_t fxSlot, CreativeKind effectKind)
        : owner(processor), slot(fxSlot), kind(effectKind), visual(processor, fxSlot, effectKind) {
        setName(effectName(kind) + " inspector");
        overview.setText(description(kind), juce::dontSendNotification);
        overview.setFont(juce::Font(pixelFontOptions(12.0f)));
        overview.setMinimumHorizontalScale(1.f);
        overview.setBorderSize(juce::BorderSize<int>(0));
        overview.setJustificationType(juce::Justification::topLeft);
        overview.setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(overview);
        content.addAndMakeVisible(visual);
        if (kind == CreativeKind::vocalGlitch) {
            const int grabNote = 36 + 2 * (int(slot) - 1);
            grab.setTooltip("Capture recent audio without playing it. MIDI note " + juce::String(grabNote) + " (any channel).");
            fire.setTooltip("Play the last manual Grab using Pattern and Grid. MIDI note " + juce::String(grabNote + 1) + " (any channel).");
            grab.onClick = [this] { owner.requestVocalGlitch(slot, true); };
            fire.onClick = [this] { owner.requestVocalGlitch(slot, false); };
            for (auto* button : {&grab, &fire}) {
                button->setWantsKeyboardFocus(false);
                button->setMouseClickGrabsKeyboardFocus(false);
                content.addAndMakeVisible(*button);
            }
            midiMapping.setFont(juce::Font(pixelFontOptions(12.f)));
            midiMapping.setMinimumHorizontalScale(1.f);
            midiMapping.setJustificationType(juce::Justification::centredLeft);
            midiMapping.setInterceptsMouseClicks(false, false);
            midiMapping.setText("MIDI (any channel): " + juce::String(grabNote) + " Grab / " + juce::String(grabNote + 1) + " Fire",
                                juce::dontSendNotification);
            content.addAndMakeVisible(midiMapping);
        }
        addGroup("BLEND");
        addRow(fxParameterId(slot, "Mix"), "Mix", "True dry/wet blend for this FX slot: 0% dry, 100% wet.", "%", false);
        configureCrossfadeDisplay(groups.back().rows.back()->slider);
        groups.back().rows.back()->slider.getProperties().set("vocalstackTexturedBar", true);
        groups.back().rows.back()->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        const char* previous = "";
        for (size_t i = 0; i < creativeParameterCount; ++i) {
            const auto& d = creativeParameters[i];
            if (d.kind != kind) continue;
            const auto field = static_cast<CreativeParameter>(i);
            const auto* section = groupFor(field);
            if (juce::String(section) != previous) {
                using Layout = Group::Layout;
                using P = CreativeParameter;
                addGroup(section, field == P::glitchPattern ? Layout::compactChoices
                    : field == P::glitchLayer ? Layout::glitchAuto : Layout::standard);
                previous = section;
            }
            const auto id = creativeParameterId(slot, i);
            auto tip = parameterTip(field);
            juce::StringArray choices;
            if (field == CreativeParameter::convolutionSpace) {
                for (const auto* name : FactoryConvolution::names) choices.add(juce::String(name) + " (synthetic)");
            } else if (field == CreativeParameter::glitchPattern || field == CreativeParameter::glitchGrid) {
                if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(owner.parameters.getParameter(id))) {
                    choices = parameter->choices;
                    for (int index = 0; index < choices.size(); ++index)
                        tip += "\n" + choices[index] + ": " + CreativeEffectVisual::glitchChoiceDescription(field, index);
                }
            }
            addRow(id, field == CreativeParameter::glitchGrid ? "Launch Grid" : d.label, tip, d.unit, d.discrete, choices);
            if (groups.back().layout == Group::Layout::compactChoices)
                groups.back().rows.back()->compactChoiceWidth = field == CreativeParameter::glitchPattern ? 152 : 124;
            if (i == size_t(CreativeParameter::delayTime)) timeRow = groups.back().rows.back().get();
            if (i == size_t(CreativeParameter::delayBeats)) beatsRow = groups.back().rows.back().get();
        }
        meter.setFont(juce::Font(pixelFontOptions(12.0f, juce::Font::bold)));
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
            case CreativeKind::convolution: return "Convolution";
            case CreativeKind::saturator: return "Saturator";
            case CreativeKind::vocalGlitch: return "Vocal Glitch";
        }
        return {};
    }
    static juce::String description(CreativeKind k) {
        switch (k) {
            case CreativeKind::delay: return "Stereo repeats with tempo sync, coloured feedback and pitch. Ducking keeps repeats behind the vocal.\nBeats use quarter-note multiples: 0.5 = eighth, 1 = quarter.";
            case CreativeKind::reverb: return "Shape early reflections, tail length and tone in a diffuse stereo space. Ducking keeps the tail behind the vocal.\nFreeze holds the tail; Mix blends dry and wet.";
            case CreativeKind::compressor: return "Stereo-linked compression with a soft knee and filtered level detector.\nMakeup is manual output gain. Mix blends dry and compressed audio for parallel compression.";
            case CreativeKind::bitcrusher: return "Reduce bit depth and sample rate for digital texture. Drive colours the input; dither decorrelates quantization distortion.\nFilter the output, then blend with Mix.";
            case CreativeKind::convolution: return "Original synthetic stereo IR spaces, not measured venues. Church 3.2 s, Hall 2.1 s, Room 0.65 s, Plate 1.5 s.\nBrightness tilts wet tone; Tail tapers decay (100% = full IR). Zero added integer latency. Factory IRs only.";
            case CreativeKind::saturator: return "Focused warm saturation. Character shapes asymmetry; Tone tilts the wet output. Fixed 2x IIR oversampling + ADAA reduces, not eliminates, aliasing.\nLevel match is a bounded RMS audition aid before Tone, not a limiter or LUFS match. Parallel paths can change phase.";
            case CreativeKind::vocalGlitch: return "Grab captures recent audio; Fire plays it. Auto independently captures and plays phrase tails.";
            case CreativeKind::granular: return "Layer grains from recent audio. Shape grain length, density, pitch and stereo spread; add timing scatter.\nFreeze holds captured audio, not grain playback. The graph is a stable parameter guide, not live audio.";
        }
        return {};
    }
    void setNeon(bool enabled) {
        neon = enabled;
        overview.setColour(juce::Label::textColourId, themePalette(*this, neon).muted);
        visual.setNeon(neon);
        meter.setColour(juce::Label::textColourId, accent());
        if (kind == CreativeKind::vocalGlitch) {
            const auto palette = themePalette(*this, neon);
            midiMapping.setColour(juce::Label::textColourId, palette.muted);
            modulationui::styleButton(grab, palette);
            modulationui::styleButton(fire, palette);
        }
        for (auto& group : groups) {
            group.heading->setColour(juce::Label::textColourId, accent());
            for (auto& row : group.rows) row->setNeon(neon);
        }
        if (!getLocalBounds().isEmpty()) resized();
        repaint();
    }
    void setInspectorLayout(bool enabled) {
        inspectorLayout = enabled;
        for (auto& group : groups) for (auto& row : group.rows) row->inspectorLayout = enabled;
        resized();
    }
    void revealParameter(const juce::String& id) {
        for (auto& group : groups) for (auto& row : group.rows)
            if (row->slider.getComponentID() == id || row->toggle.getComponentID() == id || row->choice.getComponentID() == id)
                viewport.setViewPosition(0, juce::jmax(0, row->getY() - 26));
    }
    void paint(juce::Graphics& g) override {
        const auto palette = themePalette(*this, neon);
        background.paint(g, {0, -viewport.getViewPositionY(), content.getWidth(), content.getHeight()},
                         palette.panel, palette.accent, fluidAnimationSeconds(*this));
    }
    void resized() override {
        const juce::ScopedValueSetter<bool> guard(layingOut, true);
        viewport.setBounds(getLocalBounds());
        // Always reserve the scrollbar, avoiding threshold-dependent reflow.
        const int width = juce::jmax(1, getWidth() - viewport.getScrollBarThickness());
        const int innerWidth = juce::jmax(1, width - 16);
        const bool split = innerWidth >= 680;
        const int visualWidth = split ? juce::jlimit(270, 440, innerWidth * 2 / 5) : innerWidth;
        const bool glitch = kind == CreativeKind::vocalGlitch;
        int visualY = 8;
        if (glitch) {
            auto buttons = juce::Rectangle<int>(8, 8, juce::jmin(visualWidth, 184), 28);
            grab.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).withTrimmedRight(3));
            fire.setBounds(buttons.withTrimmedLeft(3));
            midiMapping.setBounds(8, 39, visualWidth, 22);
            visualY = midiMapping.getBottom() + 6;
        }
        const int visualHeight = glitch ? 86 : split ? 254 : (width < 280 ? 216 : 238);
        visual.setBounds(8, visualY, visualWidth, visualHeight);
        juce::AttributedString descriptionText;
        descriptionText.append(overview.getText(), themeFont(overview, 12.f));
        juce::TextLayout descriptionLayout;
        descriptionLayout.createLayout(descriptionText, float(visualWidth));
        const int overviewHeight = juce::jmax(32, int(std::ceil(descriptionLayout.getHeight())) + 6);
        overview.setBounds(8, visual.getBottom() + 6, visualWidth, overviewHeight);
        int visualBottom = overview.getBottom() + 6;
        if (kind == CreativeKind::compressor) {
            meter.setBounds(8, visualBottom, visualWidth, 28);
            visualBottom = meter.getBottom() + 6;
        }
        const int controlsX = split ? visual.getRight() + 6 : 8;
        const int controlsWidth = split ? juce::jmax(1, width - controlsX - 8) : innerWidth;
        int y = split ? 8 : visualBottom;
        for (auto& group : groups) {
            const auto visibleCount = std::count_if(group.rows.begin(), group.rows.end(), [](const auto& row) { return row->isVisible(); });
            const bool sparse = visibleCount > 0 && visibleCount <= 2;
            for (auto& row : group.rows) if (row->sliderLink) {
                row->slider.getProperties().set("vocalstackTexturedBar", sparse);
                row->slider.setSliderStyle(sparse ? juce::Slider::LinearHorizontal : juce::Slider::RotaryHorizontalVerticalDrag);
            }
            group.heading->setBounds(controlsX, y, controlsWidth, 20);
            y += 22;
            if (group.layout == Group::Layout::compactChoices) {
                constexpr int rowHeight = 46, gap = 8;
                int x = 0;
                for (auto& row : group.rows) if (row->isVisible()) {
                    const int w = juce::jmin(controlsWidth, row->compactChoiceWidth);
                    if (x != 0 && x + w > controlsWidth) { x = 0; y += rowHeight + gap; }
                    row->setBounds(controlsX + x, y, w, rowHeight);
                    x += w + gap;
                }
                if (x != 0) y += rowHeight + gap;
                continue;
            }
            if (group.layout == Group::Layout::glitchAuto) {
                auto& layer = *group.rows[0];
                auto& automatic = *group.rows[1];
                auto& threshold = *group.rows[2];
                constexpr int buttonWidth = 60, buttonHeight = 28, buttonStep = 34, knobX = buttonWidth + 12;
                const bool beside = controlsWidth >= knobX + 64;
                const int knobWidth = juce::jmin(76, controlsWidth - (beside ? knobX : 0));
                const int knobHeight = threshold.inspectorHeight(knobWidth);
                const int dialCentre = (knobHeight - threshold.captionHeight(knobWidth) + 20) / 2;
                const int autoY = y + (beside ? juce::jmax(buttonStep, dialCentre - buttonHeight / 2) : buttonStep);
                layer.setBounds(controlsX, autoY - buttonStep, juce::jmin(buttonWidth, controlsWidth), buttonHeight);
                automatic.setBounds(controlsX, autoY, layer.getWidth(), buttonHeight);
                threshold.setBounds(controlsX + (beside ? knobX : 0), beside ? y : automatic.getBottom() + 6,
                                    knobWidth, knobHeight);
                y = juce::jmax(automatic.getBottom(), threshold.getBottom()) + 4;
                continue;
            }
            if (sparse) {
                // The style overlays the editable value and name inside the track.
                constexpr int mixHeight = 44;
                for (auto& row : group.rows) if (row->isVisible()) {
                    row->setBounds(controlsX, y, controlsWidth, row->choiceLink ? 60 : mixHeight);
                    y += row->getHeight() + 6;
                }
                continue;
            }
            {
                // Share existing row width with the dial faces rather than leaving
                // blank cells. Dedicated value/caption lanes stay outside the face.
                std::vector<Row*> line;
                int used = 0;
                const auto flush = [&] {
                    if (line.empty()) return;
                    int extra = juce::jmax(0, controlsWidth - used);
                    int x = 0, height = 0;
                    int rotaryCount = int(std::count_if(line.begin(), line.end(), [](auto* row) { return row->sliderLink != nullptr; }));
                    for (auto* row : line) {
                        int cellWidth = juce::jmin(controlsWidth, row->sliderLink && inspectorLayout ? 88 : row->preferredWidth());
                        if (row->sliderLink) {
                            const int growth = juce::jmin(110 - cellWidth, extra / juce::jmax(1, rotaryCount--));
                            cellWidth += growth; extra -= growth;
                        }
                        const int cellHeight = row->inspectorHeight(cellWidth);
                        row->setBounds(controlsX + x, y, cellWidth, cellHeight);
                        x += cellWidth + 2; height = juce::jmax(height, cellHeight);
                    }
                    y += height + 2; line.clear(); used = 0;
                };
                for (auto& row : group.rows) if (row->isVisible()) {
                    const int minimum = juce::jmin(controlsWidth, row->sliderLink && inspectorLayout ? 88 : row->preferredWidth());
                    if (!line.empty() && used + 2 + minimum > controlsWidth) flush();
                    used += (line.empty() ? 0 : 2) + minimum;
                    line.push_back(row.get());
                }
                flush(); y += 4;
                continue;
            }

        }
        content.setSize(width, juce::jmax(getHeight(), juce::jmax(visualBottom, y)));
        // Retain the preferred anchor even if a wide/tall view temporarily has
        // no scroll range. Returning to a sidebar restores this instance's place.
        viewport.setViewPosition(0, scrollAnchor ? scrollAnchor->getY() + scrollOffset : scrollY);
    }
private:
    bool inspectorLayout = false;
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
    void addGroup(const char* name, Group::Layout layout = Group::Layout::standard) {
        Group group;
        group.layout = layout;
        group.heading = std::make_unique<juce::Label>();
        group.heading->setText(name, juce::dontSendNotification);
        group.heading->setFont(juce::Font(pixelFontOptions(11.0f, juce::Font::bold)));
        group.heading->setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(*group.heading); groups.push_back(std::move(group));
    }
    void addRow(const juce::String& id, const juce::String& name, const juce::String& tip, const char* unit,
                bool discrete, const juce::StringArray& choices = {}) {
        auto row = std::make_unique<Row>(owner, id, name, tip, unit, discrete, choices);
        const auto begin = [this, id] { if (onBeginEdit) onBeginEdit(id); };
        const auto end = [this] { if (onEndEdit) onEndEdit(); updateState(); };
        row->slider.beginEdit = begin; row->slider.endEdit = end;
        row->slider.onValueChange = [this] { if (onValueChanged) onValueChanged(); };
        row->toggle.beginEdit = begin; row->toggle.endEdit = end;
        if (row->choiceLink) {
            auto* control = row.get();
            control->choice.onChange = [this, control, begin, end] {
                const int selected = control->choice.getSelectedId();
                if (selected <= 0) return;
                begin();
                control->choiceLink->setValueAsCompleteGesture(float(selected - 1));
                if (onValueChanged) onValueChanged();
                end();
            };
        }
        content.addAndMakeVisible(*row); groups.back().rows.push_back(std::move(row));
    }
    static const char* groupFor(CreativeParameter p) {
        using P = CreativeParameter;
        if (p == P::convolutionSpace) return "FACTORY IR";
        if (p >= P::convolutionBrightness && p <= P::convolutionOutput) return "WET SHAPING";
        if (p == P::glitchLength) return "CAPTURE";
        if (p == P::glitchPattern || p == P::glitchGrid) return "PLAYBACK";
        if (p == P::glitchLayer || p == P::glitchAuto || p == P::glitchThreshold) return "LAYER & AUTO";
        if (p >= P::saturatorDrive) return "SATURATION | 2x + ADAA";
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
            case P::convolutionSpace: return "Original synthetic stereo IRs, not measured venues. Selection crossfades preprepared convolution kernels.";
            case P::glitchLength: return "How much recent audio to capture, in milliseconds.";
            case P::glitchPattern: return "How Fire or Auto plays the captured slice.";
            case P::glitchGrid: return "Quantizes playback, not Grab. Use the immediate setting when the host transport is stopped.";
            case P::glitchLayer: return "On: add the slice over the live vocal. Off: replace the live vocal in the wet signal during playback. Mix still blends dry and wet.";
            case P::glitchAuto: return "Capture and play phrase tails automatically. Manual Grab and Fire still work.";
            case P::glitchThreshold: return "Input level used to detect phrases for Auto; does not affect manual Grab or Fire.";
            case P::convolutionBrightness: return "Smoothed wet-only tilt around 1 kHz: opposite low/high gains, not feedback damping.";
            case P::convolutionTail: return "Additional exponential decay taper, approximated at FFT partition centres. 100% retains the full factory IR; does not stretch time or extend the IR.";
            case P::saturatorCharacter: return "Continuous warm asymmetric transfer shape using the existing Waveshaper antiderivative anti-aliasing at fixed 2x oversampling.";
            case P::saturatorLevelMatch: return "Bounded smoothed RMS matching before Tone and manual Output; not perceptual loudness matching or peak protection.";
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
    FluidBackground background;
    CreativeEffectVisual visual;
    juce::Component content;
    InspectorViewport viewport;
    bool layingOut = false;
    Row* scrollAnchor = nullptr;
    int scrollY = 0, scrollOffset = 0;
    juce::Label overview, meter, midiMapping;
    juce::TextButton grab{"Grab"}, fire{"Fire"};
    std::vector<Group> groups;
    Row* timeRow = nullptr;
    Row* beatsRow = nullptr;
};

} // namespace vocalstack
