#include "VisualTheme.h"
#pragma once

#include "DoublerPanel.h"
#include "ParameterKnob.h"
#include "ModulationUI.h"
#include "ToneEqGraph.h"
#include "Waveshaper.h"
#include <memory>
#include <vector>

namespace vocalstack {
namespace toneui {
// JUCE replaces a ComboBox's internal label on hierarchy/skin changes. Apply
// the host-focus policy when the replacement is made, not on a polling timer.
class PassiveComboBox final : public modulationui::ComboBox {
public:
    PassiveComboBox() { detail::passiveControl(*this); }
    void lookAndFeelChanged() override {
        modulationui::ComboBox::lookAndFeelChanged();
        detail::passiveControl(*this);
    }
};

class Control final : public juce::Component {
    ParameterKnob knob;
public:
    Control(Processor& owner, const juce::String& id, const juce::String& name, const char* unit)
        : knob(owner, id, name, unit), slider(knob.slider) {
        addAndMakeVisible(knob);
        if (juce::String(unit) == "Hz") {
            slider.textFromValueFunction = [](double v) {
                return v >= 1000 ? juce::String(v / 1000, 2) + " kHz" : juce::String(v, 1) + " Hz";
            };
            slider.valueFromTextFunction = [](const juce::String& text) {
                const double value = text.getDoubleValue(); return text.containsIgnoreCase("k") ? value * 1000 : value;
            };
        }
        slider.updateText();
    }
    void setLinear(bool linear) { knob.setLinear(linear); }
    void resized() override { knob.setBounds(getLocalBounds()); }
    void setNeon(bool neon) { knob.setNeon(neon); }
    ModulationSlider& slider;
};
} // namespace toneui

// Native RoutingPanel inspector content. The parent supplies the machine's
// fxOn/bypass control. slot 1..15 is a stable automation identity, not a node ID.
class ToneEffectPanel final : public juce::Component, private juce::Timer {
public:
    ToneEffectPanel(Processor& processor, size_t fxSlot, NodeType nodeType)
        : owner(processor), slot(fxSlot), type(nodeType) {
        setName(isWave() ? "Waveshaper inspector" : isEq() ? "EQ8 inspector"
                    : isSpatialEq() ? "Spatial EQ inspector" : "Band routing inspector");
                mode.setModulationOwner(owner); quality.setModulationOwner(owner);
                bandOn.setModulationOwner(owner); adaptive.setModulationOwner(owner); raw.setModulationOwner(owner); levelMatch.setModulationOwner(owner);
        heading.setText(isWave() ? "WAVESHAPER" : isEq() ? "EQ8" : isSpatialEq() ? "SPATIAL EQ" : "BAND ROUTING",
                    juce::dontSendNotification);
        heading.setFont(juce::Font(pixelFontOptions(14.0f, juce::Font::bold)));
        heading.setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(heading);
        hint.setFont(juce::Font(pixelFontOptions(11.0f)));
        hint.setJustificationType(juce::Justification::topLeft);
        hint.setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(hint);
        if (slot < 1 || slot > 15) {
            hint.setText("Select an effect in automation slots 1-15.", juce::dontSendNotification);
        } else if (isWave()) setupWave();
        else if (isEq()) setupEq();
        else if (isSpatialEq()) setupSpatialEq();
        else setupBands();
        contentViewport.setViewedComponent(&content, false);
        contentViewport.setScrollBarsShown(true, false);
        juce::Component::addAndMakeVisible(contentViewport);
        setNeon(false);
        detail::passiveControl(*this);
        startTimerHz(30);
    }
    ~ToneEffectPanel() override {
        stopTimer();
        setLookAndFeel(nullptr);
        contentViewport.setViewedComponent(nullptr, false);
        expanded.reset();
        // Attachments must be detached before their controls are destroyed.
        modeAttachment.reset();
        spatialShapeAttachment.reset();
        spatialSlopeAttachment.reset();
        spatialDynamicModeAttachment.reset();
        qualityAttachment.reset();
        enabledAttachment.reset();
        adaptiveAttachment.reset();
        levelMatchAttachment.reset();
    }
    static constexpr int preferredWidth = 400;
    static constexpr int preferredHeight = 650;
    // Outer card viewports can size us to this height (including their Mix strip
    // separately) to keep a single vertical scroll owner. Width excludes their bar.
    int preferredContentHeight(int width) const {
        if (isSpatialEq()) {
            const int inner = juce::jmax(1, width - 16);
            const int columns = juce::jmax(1, (inner + knobGap) / (knobWidth + knobGap));
            const int rowHeight = ((4 + columns - 1) / columns) * (knobHeight + knobGap);
            return (width < compactWidth ? 268 : 244) + rowHeight * (spatialDynamicsVisible ? 2 : 1);
        }
        if (width >= 620) {
            // Reserve the scrollbar before measuring so the breakpoint cannot oscillate.
            const int inner = juce::jmax(1, width - contentViewport.getScrollBarThickness() - 16);
            const int controlWidth = inner - inner * 44 / 100 - 6;
            const int columns = juce::jmax(1, (controlWidth + knobGap) / (knobWidth + knobGap));
            const int count = isEq() ? 5 : isWave() ? 3 : 2;
            const int gridHeight = ((count + columns - 1) / columns) * (knobHeight + knobGap);
            return juce::jmax(280, (isEq() ? 158 : isWave() ? 148 : 83) + gridHeight);
        }
        const int inner = juce::jmax(1, width - 16);
        const int columns = juce::jmax(1, (inner + knobGap) / (knobWidth + knobGap));
        const int count = isEq() ? 5 : isWave() ? 3 : 2;
        const int gridHeight = ((count + columns - 1) / columns) * (knobHeight + knobGap);
        // Match the fixed rows in layoutContent; short inspectors scroll without shrinking plots.
        const bool compact = width < compactWidth;
        const int fixedHeight = isEq() ? (compact ? 445 : 485)
                                      : isWave() ? (compact ? 450 : 510) : (compact ? 314 : 394);
        return juce::jmax(preferredHeight, fixedHeight + gridHeight);
    }
    void setNeon(bool enabled) {
        neon = enabled;
        const auto selectedTheme = componentTheme(*this, neon);
        const auto p = themePalette(*this, neon);
        const bool device = selectedTheme == Theme::device;
        panelStyle.setPalette(p);
        setLookAndFeel(device ? nullptr : &panelStyle);
        const auto accent = device ? p.accent : p.secondary;
        heading.setColour(juce::Label::textColourId, accent);
        heading.setFont(juce::Font(pixelFontOptions(14.0f,
            selectedTheme == Theme::spectral ? juce::Font::plain : juce::Font::bold)));
        hint.setColour(juce::Label::textColourId, device ? juce::Colour(0xffaebbc2) : p.muted);
        for (auto& c : controls) c->setNeon(neon);
        for (auto& c : bandControls) c->setNeon(neon);
        for (auto& c : dynamicControls) c->setNeon(neon);
        for (auto* b : std::initializer_list<juce::TextButton*>{&expand, &bandOn, &remove, &adaptive, &raw, &levelMatch}) {
            b->setColour(juce::TextButton::buttonColourId, device ? juce::Colour(0xff303932) : p.inset);
            b->setColour(juce::TextButton::buttonOnColourId, device ? accent : p.accent);
            b->setColour(juce::TextButton::textColourOffId, device ? juce::Colour(0xffe4e9ee) : p.text);
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff151b20));
        }
        for (auto* combo : {&mode, &quality, &slope, &dynamicsMode}) {
            modulationui::styleCombo(*combo, p);
            if (device) {
                combo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff303932));
                combo->setColour(juce::ComboBox::textColourId, juce::Colour(0xffe4e9ee));
                combo->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff526071));
            }
        }
        qualityLabel.setColour(juce::Label::textColourId, device ? juce::Colour(0xffb8c4ca) : p.muted);
        dynamicsLabel.setColour(juce::Label::textColourId, device ? juce::Colour(0xffb8c4ca) : p.muted);
        modulationui::styleViewport(contentViewport, p);
        if (graph) graph->setNeon(neon);
        if (visual) visual->setNeon(neon);
        if (expanded) expanded->setTheme(selectedTheme);
        updateBandButtons();
        repaint();
    }
    void revealParameter(const juce::String& id) {
        if (isEq()) for (size_t band = 0; band < 8; ++band)
            if (id.startsWith("fx" + juce::String(int(slot)) + "EqBand" + juce::String(int(band + 1)))) { selectBand(band); break; }
    }
    void paint(juce::Graphics& g) override {
        const auto palette = themePalette(*this, neon);
        background.paint(g, {0, -contentViewport.getViewPositionY(), content.getWidth(), content.getHeight()}, palette.panel, palette.accent, fluidAnimationSeconds(*this));
    }
    void resized() override {
        const auto position = contentViewport.getViewPosition();
        contentViewport.setBounds(getLocalBounds());
        int height = juce::jmax(preferredContentHeight(getWidth()), getHeight());
        const int scrollbar = height > getHeight() ? contentViewport.getScrollBarThickness() : 0;
        const int width = juce::jmax(1, getWidth() - scrollbar);
        height = juce::jmax(preferredContentHeight(getWidth() >= 620 ? getWidth() : width), getHeight());
        content.setSize(width, height);
        layoutContent();
        contentViewport.setViewPosition(position);
    }
private:
    static constexpr int compactWidth = 320;
    static constexpr int knobWidth = 68, knobHeight = 100, knobGap = 2;
    // The textured track contains its name and editable value.
    static constexpr int mixHeight = 44;
    void layoutContent() {
        const bool wide = !isSpatialEq() && getWidth() >= 620;
        const bool compact = !wide && content.getWidth() < compactWidth;
        auto area = content.getLocalBounds().reduced(8);
        auto top = area.removeFromTop(23);
        if (isEq()) expand.setBounds(top.removeFromRight(91));
        heading.setBounds(top);
        area.removeFromTop(5);
        juce::Rectangle<int> plotArea;
        if (wide) {
            plotArea = area.removeFromLeft(area.getWidth() * 44 / 100);
            area.removeFromLeft(6);
        }
        const auto knobGrid = [&](bool dynamicsOnly = false) {
            const int columns = juce::jmax(1, (area.getWidth() + knobGap) / (knobWidth + knobGap));
            const int y = area.getY();
            int index = 0;
            const auto place = [&](auto& knobs) {
                for (auto& control : knobs) {
                    if (control->slider.getSliderStyle() == juce::Slider::LinearHorizontal) continue;
                    control->setBounds(area.getX() + (index % columns) * (knobWidth + knobGap),
                                       y + (index / columns) * (knobHeight + knobGap),
                                       juce::jmin(knobWidth, area.getWidth()), knobHeight);
                    ++index;
                }
            };
            if (dynamicsOnly) place(dynamicControls);
            else {
                place(bandControls);
                place(controls);
            }
            area.removeFromTop(((index + columns - 1) / columns) * (knobHeight + knobGap));
        };
        if (isSpatialEq()) {
            layoutBandChips(area, compact);
            bandOn.setBounds(area.removeFromTop(24).withWidth(56));
            area.removeFromTop(2);
            mode.setBounds(area.removeFromTop(29));
            area.removeFromTop(2);
            slope.setBounds(area.removeFromTop(29));
            area.removeFromTop(2);
            knobGrid();
            auto dynamicsRow = area.removeFromTop(29);
            dynamicsLabel.setBounds(dynamicsRow.removeFromLeft(juce::jmin(128, dynamicsRow.getWidth() / 2)));
            dynamicsMode.setBounds(dynamicsRow);
            area.removeFromTop(4);
            if (spatialDynamicsVisible) knobGrid(true);
        } else if (graph) {
            const int columns = juce::jmax(1, (area.getWidth() + knobGap) / (knobWidth + knobGap));
            const int gridHeight = ((int(bandControls.size() + controls.size()) + columns - 1) / columns)
                                   * (knobHeight + knobGap);
            const int graphHeight = wide ? juce::jlimit(200, 480, plotArea.getHeight() - 36)
                : compact ? 200 : juce::jlimit(260, 480, content.getHeight() - gridHeight - 209);
            if (wide) {
                graph->setBounds(plotArea.removeFromTop(graphHeight));
                plotArea.removeFromTop(6);
            } else {
                graph->setBounds(area.removeFromTop(graphHeight));
                area.removeFromTop(5);
            }
            layoutBandChips(area, compact);
            auto actions = area.removeFromTop(24);
            bandOn.setBounds(actions.removeFromLeft(56));
            actions.removeFromLeft(2);
            remove.setBounds(actions.removeFromLeft(64));
            area.removeFromTop(2);
            mode.setBounds(area.removeFromTop(29));
            area.removeFromTop(2);
            knobGrid();
            adaptive.setButtonText("Adapt Q");
            adaptive.setBounds(area.removeFromTop(25).withWidth(64));
        } else {
            if (mode.getNumItems() > 0) {
                if (wide) {
                    mode.setBounds(plotArea.removeFromTop(29));
                    plotArea.removeFromTop(4);
                } else {
                    mode.setBounds(area.removeFromTop(29));
                    area.removeFromTop(6);
                }
            }
            if (visual) {
                if (wide) {
                    visual->setBounds(plotArea.removeFromTop(juce::jlimit(160, 480, plotArea.getHeight() - 36)));
                    plotArea.removeFromTop(6);
                } else {
                    visual->setBounds(area.removeFromTop(compact ? 180 : isWave() ? 240 : 260));
                    area.removeFromTop(6);
                }
            }
            if (isWave()) {
                auto toggles = area.removeFromTop(25);
                raw.setButtonText("Raw");
                raw.setBounds(toggles.removeFromLeft(56));
                toggles.removeFromLeft(2);
                levelMatch.setButtonText("Match");
                levelMatch.setBounds(toggles.removeFromLeft(64));
                area.removeFromTop(2);
                if (wide) {
                    auto qualityRow = area.removeFromTop(29);
                    qualityLabel.setBounds(qualityRow.removeFromLeft(52));
                    quality.setBounds(qualityRow);
                } else {
                    qualityLabel.setBounds(area.removeFromTop(18));
                    quality.setBounds(area.removeFromTop(29));
                }
                area.removeFromTop(2);
            }
            knobGrid();
            for (auto& control : controls) {
                if (control->slider.getSliderStyle() != juce::Slider::LinearHorizontal) continue;
                control->setBounds(area.removeFromTop(mixHeight));
                area.removeFromTop(2);
            }
        }
        area.removeFromTop(6);
        hint.setBounds(wide ? plotArea : area);
    }
private:
    class ExpandedWindow final : public juce::DocumentWindow {
    public:
        ExpandedWindow(Processor& owner, size_t slot, NodeType type, Theme selectedTheme)
            : juce::DocumentWindow("EQ8 | expanded editor", themePalette(selectedTheme).background, juce::DocumentWindow::closeButton) {
            // Desktop windows do not inherit the plugin editor's component tree.
            getProperties().set(vocalstackThemeProperty, int(selectedTheme));
            setUsingNativeTitleBar(true);
            panel = new ToneEffectPanel(owner, slot, type);
            panel->expand.setVisible(false);
            panel->expandedView = true;
            setContentOwned(panel, false);
            setResizable(true, false);
            setResizeLimits(400, 650, 1400, 1000);
            setWantsKeyboardFocus(false);
            setMouseClickGrabsKeyboardFocus(false);
            centreWithSize(780, 700);
        }
        void closeButtonPressed() override { setVisible(false); }
        void setTheme(Theme selectedTheme) {
            getProperties().set(vocalstackThemeProperty, int(selectedTheme));
            setBackgroundColour(themePalette(selectedTheme).background);
            panel->setNeon(selectedTheme == Theme::neon);
            repaint();
        }
    private:
        ToneEffectPanel* panel = nullptr; // owned by DocumentWindow
    };
    bool isWave() const { return type == NodeType::waveshaper; }
    bool isEq() const { return type == NodeType::equalizer; }
    bool isSpatialEq() const { return type == NodeType::spatialEq; }
    void layoutBandChips(juce::Rectangle<int>& area, bool compact) {
        const int columns = compact ? 4 : 8;
        for (int row = 0; row < 8 / columns; ++row) {
            auto chips = area.removeFromTop(24);
            for (int col = 0; col < columns; ++col)
                bandButtons[size_t(row * columns + col)].setBounds(
                    chips.removeFromLeft(chips.getWidth() / (columns - col)).reduced(1));
        }
        area.removeFromTop(4);
    }
    void addControl(const char* field, const char* title, const char* unit) {
        auto c = std::make_unique<toneui::Control>(owner, toneParameterId(slot, field), title, unit);
        content.addAndMakeVisible(*c);
        controls.push_back(std::move(c));
    }
    void attachMode(const juce::String& id) {
        modeAttachment.reset();
        mode.setComponentID(id);
        mode.setWantsKeyboardFocus(false);
        mode.setMouseClickGrabsKeyboardFocus(false);
        content.addAndMakeVisible(mode);
        if (owner.parameters.getParameter(id)) {
            mode.setEnabled(true);
            modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.parameters, id, mode);
        } else mode.setEnabled(false);
    }
    void setupWave() {
        visual = std::make_unique<toneui::WaveshaperVisual>(owner, slot);
        content.addAndMakeVisible(*visual);
        mode.addItem("Follow legacy Shape automation", 1);
        for (size_t i = 0; i < Waveshaper::shapeNames.size(); ++i) {
            if (i == 0) mode.addSectionHeading("Classic");
            else if (i == 3) mode.addSectionHeading("Saturation");
            else if (i == 6) mode.addSectionHeading("Clipping");
            else if (i == 8) mode.addSectionHeading("Folding");
            else if (i == 12) mode.addSectionHeading("Rectification");
            else if (i == 14) mode.addSectionHeading("Destruction");
            else if (i == 17) mode.addSectionHeading("Curve shaping");
            juce::Image icon(juce::Image::ARGB, 32, 22, true);
            juce::Graphics glyph(icon);
            juce::Path curve;
            for (int step = 0; step <= 31; ++step) {
                const double x = -2.0 + 4.0 * step / 31;
                const float y = 11.0f - 8.0f * float(juce::jlimit(-1.0, 1.0, Waveshaper::transfer(x, int(i))));
                if (step == 0) curve.startNewSubPath(0, y); else curve.lineTo(float(step), y);
            }
            glyph.setColour(juce::Colour(0xffc4d6ce));
            glyph.strokePath(curve, juce::PathStrokeType(1.5f));
            mode.getRootMenu()->addItem(int(i + 2), Waveshaper::shapeNames[i], true, false, icon);
        }
        mode.setName("Waveshaper shape");
        mode.setTooltip("Original transfer curves grouped by family; the first three preserve legacy identities");
        attachMode(toneParameterId(slot, "WaveAlgorithm"));
        qualityLabel.setText("Quality", juce::dontSendNotification);
        qualityLabel.setInterceptsMouseClicks(false, false); content.addAndMakeVisible(qualityLabel);
        quality.setName("Waveshaper quality");
        quality.addItem("Off / project-rate ADAA", 1); quality.addItem("2x IIR", 2); quality.addItem("4x IIR", 3);
        quality.setComponentID(toneParameterId(slot, "WaveQuality"));
        quality.setTooltip("2x / 4x IIR oversampling reduces aliasing but is not phase-neutral. IIR filters have frequency-dependent phase; parallel paths may comb. Raw always runs at project rate; the quality value is retained while Raw is on.");
        content.addAndMakeVisible(quality);
        if (owner.parameters.getParameter(quality.getComponentID()))
            qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.parameters, quality.getComponentID(), quality);
        addControl("WaveDrive", "Drive", "dB");
        addControl("WaveOutput", "Output trim", "dB");
                addControl("WaveShapeAmount", "Shape", "%");
                raw.setButtonText("Raw / project-rate nonlinearity"); raw.setClickingTogglesState(true);
                raw.setComponentID(toneParameterId(slot, "WaveRaw"));
                raw.setTooltip("Bypass alias reduction at project sample rate. This also changes ADAA attenuation and phase; not a fixed grit amount.");
                content.addAndMakeVisible(raw);
        raw.onStateChange = [this] { updateWaveQuality(); };
        rawAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, toneParameterId(slot, "WaveRaw"), raw);
        updateWaveQuality();
        levelMatch.setButtonText("Level match");
        levelMatch.setClickingTogglesState(true);
        levelMatch.setComponentID(toneParameterId(slot, "WaveLevelMatch"));
        levelMatch.setTooltip("Bounded stereo RMS matching for audition; settles over time, not a limiter or LUFS match. Manual Output trim remains active.");
        content.addAndMakeVisible(levelMatch);
        levelMatchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, levelMatch.getComponentID(), levelMatch);
        auto mix = std::make_unique<toneui::Control>(owner, fxParameterId(slot, "Mix"), "Dry / wet", "%");
        mix->setLinear(true);
        mix->slider.setTooltip("True dry/wet blend; drive can increase level. Output trim is not a limiter.");
        content.addAndMakeVisible(*mix);
        controls.push_back(std::move(mix));
        if (controls.size() > 2) controls[2]->setEnabled(Waveshaper::hasShapeAmount(toneui::effectiveWaveShape(owner, slot)));
        hint.setText("Raw adds aliasing; Quality reduces it.\nLevel match aids RMS comparison, not peak limiting.", juce::dontSendNotification);
    }
    void setupEq() {
        graph = std::make_unique<toneui::EqGraph>(owner, slot);
        graph->onSelect = [this](size_t band) { selectBand(band); };
        content.addAndMakeVisible(*graph);
        expand.setButtonText("Expand EQ");
        expand.setTooltip("Open a larger, resizable EQ editor. Both views follow host automation.");
        expand.onClick = [this] {
            const auto selectedTheme = componentTheme(*this, neon);
            if (!expanded) expanded = std::make_unique<ExpandedWindow>(owner, slot, type, selectedTheme);
            expanded->setTheme(selectedTheme);
            expanded->setVisible(true);
            expanded->toFront(false);
        };
        content.addAndMakeVisible(expand);
        setupBandChips();
        for (size_t m = 0; m < toneui::modeLabels.size(); ++m) mode.addItem(toneui::modeLabels[m], int(m + 1));
        mode.setName("Band filter mode");
        remove.setButtonText("Remove");
        remove.setTooltip("Disable the selected band; its stable automation slot and values are retained");
        remove.onClick = [this] {
            toneui::edit(owner, eqBandParameterId(slot, selectedBand, "On"), 0);
            if (graph) graph->refresh();
            updateBandButtons();
        };
        content.addAndMakeVisible(remove);
        addControl("EqShift", "Frequency shift", "st");
        controls.back()->slider.setTooltip("Shift all band frequencies in semitones; graph handles show shifted frequency");
        addControl("EqGain", "Global output", "dB");
        adaptive.setButtonText("Adaptive Q");
        adaptive.setClickingTogglesState(true);
        adaptive.setTooltip("Narrow bell bandwidth with increasing absolute band gain");
        content.addAndMakeVisible(adaptive);
        const auto id = toneParameterId(slot, "EqAdaptiveQ");
                adaptive.setComponentID(id);
        if (owner.parameters.getParameter(id))
            adaptiveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, id, adaptive);
        else adaptive.setEnabled(false);
        hint.setText("Click: bell | drag ends: shelf / sides: cut\nBottom: notch | Alt-wheel: Q | FFT: right dBFS scale", juce::dontSendNotification);
        selectBand(0);
    }
    void setupBandChips() {
        for (size_t b = 0; b < bandButtons.size(); ++b) {
            auto& button = bandButtons[b];
            button.setButtonText(juce::String(int(b + 1)));
            button.setName("Select EQ band " + juce::String(int(b + 1)));
            button.setTooltip("Select band " + juce::String(int(b + 1)) + "; selection does not alter audio");
            button.onClick = [this, b] { selectBand(b); };
            content.addAndMakeVisible(button);
        }
        bandOn.setButtonText("On");
        bandOn.setClickingTogglesState(true);
        bandOn.setTooltip("Enable this band without resetting its controls");
        content.addAndMakeVisible(bandOn);
    }
    void setupSpatialEq() {
        setupBandChips();
        mode.addItemList(processorRouting::spatialEqShapeNames, 1);
        mode.setName("Spatial EQ band shape");
        slope.addItemList(processorRouting::spatialEqSlopeNames, 1);
        slope.setName("Spatial EQ cut slope");
        slope.setTooltip("Slope applies only to Low Cut and High Cut. A 6 dB/oct cut ignores Q.");
        dynamicsLabel.setText("Internal dynamics", juce::dontSendNotification);
        dynamicsLabel.setFont(hint.getFont());
        dynamicsLabel.setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(dynamicsLabel);
        dynamicsMode.addItemList(processorRouting::spatialEqDynamicModeNames, 1);
        dynamicsMode.setName("Spatial EQ internal dynamics");
        dynamicsMode.setTooltip("Bell and shelves only. Downward cuts and Upward boosts above threshold using the internal detector.");
        hint.setText("Depth is per band. At 0%, only the direct EQ is heard.", juce::dontSendNotification);
        selectBand(0);
    }
    void setupBands() {
        visual = std::make_unique<toneui::CrossoverVisual>(owner, slot);
        content.addAndMakeVisible(*visual);
        mode.addItem("2 bands | Low / High", 1);
        mode.addItem("3 bands | Low / Mid / High", 2);
        mode.setName("Number of crossover bands");
        mode.setTooltip("Select two or three audio branches; inactive Mid wiring/settings are retained");
        attachMode(toneParameterId(slot, "BandCount"));
        addControl("CrossoverLow", "Low crossover", "Hz");
        addControl("CrossoverHigh", "High crossover", "Hz");
        controls[0]->slider.setTooltip("Low / High boundary in 2-band mode; Low / Mid boundary in 3-band mode");
        controls[1]->slider.setTooltip("Mid / High boundary in 3-band mode. Keep above the low crossover.");
        updateBands();
    }
    void bindSpatialChoice(toneui::PassiveComboBox& combo, const juce::String& id,
                           std::unique_ptr<juce::ParameterAttachment>& attachment) {
        combo.onChange = nullptr;
        attachment.reset();
        combo.setComponentID(id);
        content.addAndMakeVisible(combo);
        if (auto* parameter = owner.parameters.getParameter(id)) {
            attachment = std::make_unique<juce::ParameterAttachment>(*parameter, [this, &combo](float raw) {
                combo.setSelectedId(int(std::lround(raw)) + 1, juce::dontSendNotification);
                updateSpatialBandControls();
            });
            combo.onChange = [&combo, &attachment] {
                if (attachment && combo.getSelectedId() > 0)
                    attachment->setValueAsCompleteGesture(float(combo.getSelectedId() - 1));
            };
            attachment->sendInitialUpdate();
        }
    }
    void updateSpatialBandControls() {
        const int shape = mode.getSelectedId() - 1;
        const bool cut = shape == int(SpatialEqShape::LowCut) || shape == int(SpatialEqShape::HighCut);
        const bool tonal = shape >= int(SpatialEqShape::Bell) && shape <= int(SpatialEqShape::HighShelf);
        slope.setEnabled(cut);
        dynamicsMode.setEnabled(tonal);
        const bool showDynamics = tonal && dynamicsMode.getSelectedId() > 1;
        for (auto& control : dynamicControls) control->setVisible(showDynamics);
        if (bandControls.size() == 4) {
            bandControls[1]->setEnabled(tonal);
            bandControls[2]->setEnabled(!cut || processorRouting::spatialEqSlopeFromIndex(float(slope.getSelectedId() - 1)) != 6);
        }
        if (spatialDynamicsVisible != showDynamics) {
            spatialDynamicsVisible = showDynamics;
            if (!dynamicControls.empty()) resized();
        }
    }
    void selectBand(size_t band) {
        if (band > 7) return;
        // Re-selecting the current chip must not interrupt a live text/slider edit.
        if (selectedBand == band && !bandControls.empty()) return;
        selectedBand = band;
        modeAttachment.reset();
        enabledAttachment.reset();
        bandControls.clear();
        dynamicControls.clear();
        if (isSpatialEq()) {
            bindSpatialChoice(mode, spatialEqBandParameterId(slot, band, processorRouting::spatialShape), spatialShapeAttachment);
            bindSpatialChoice(slope, spatialEqSlopeParameterId(slot, band), spatialSlopeAttachment);
            bindSpatialChoice(dynamicsMode, spatialEqDynamicParameterId(slot, band, processorRouting::dynamicMode), spatialDynamicModeAttachment);
        } else attachMode(eqBandParameterId(slot, band, "Mode"));
        const auto onId = isSpatialEq() ? spatialEqBandParameterId(slot, band, processorRouting::spatialOn)
                                       : eqBandParameterId(slot, band, "On");
        bandOn.setComponentID(onId);
        if (owner.parameters.getParameter(onId)) {
            bandOn.setEnabled(true);
            enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, onId, bandOn);
        } else bandOn.setEnabled(false);
        const std::array<const char*, 3> fields{"Frequency", "Gain", "Q"};
        const std::array<const char*, 3> titles{"Frequency", "Gain", "Q"};
        const std::array<const char*, 3> units{"Hz", "dB", "Q"};
        const auto add = [&](const juce::String& id, const char* title, const char* unit) {
            auto c = std::make_unique<toneui::Control>(owner, id, title, unit);
            content.addAndMakeVisible(*c);
            c->setNeon(neon);
            bandControls.push_back(std::move(c));
        };
        if (isSpatialEq()) {
            for (size_t p = processorRouting::spatialFrequency; p <= processorRouting::spatialDepth; ++p) {
                const auto& d = processorRouting::spatialEqFields[p];
                add(spatialEqBandParameterId(slot, band, p), d.label, d.unit);
            }
            constexpr std::array<const char*, 4> tips{"Detector level above which the band engages.",
                "Maximum dynamic cut or boost, in dB.", "How quickly the band engages.", "How quickly the band returns to its static gain."};
            for (size_t field = processorRouting::dynamicThreshold; field < spatialEqDynamicFieldsPerBand; ++field) {
                const auto& d = processorRouting::spatialEqDynamicFields[field];
                auto c = std::make_unique<toneui::Control>(owner, spatialEqDynamicParameterId(slot, band, field), d.label, d.unit);
                c->slider.setTooltip(tips[field - processorRouting::dynamicThreshold]);
                content.addChildComponent(*c);
                c->setNeon(neon);
                dynamicControls.push_back(std::move(c));
            }
            updateSpatialBandControls();
        } else {
            for (size_t i = 0; i < fields.size(); ++i) add(eqBandParameterId(slot, band, fields[i]), titles[i], units[i]);
            graph->select(band);
        }
        updateBandButtons();
        detail::passiveControl(*this);
        resized();
    }
    void updateBandButtons() {
        if (!isEq() && !isSpatialEq()) return;
        for (size_t b = 0; b < bandButtons.size(); ++b) {
            auto& button = bandButtons[b];
            const auto id = isSpatialEq() ? spatialEqBandParameterId(slot, b, processorRouting::spatialOn)
                                          : eqBandParameterId(slot, b, "On");
            const bool enabled = toneui::value(owner, id) >= 0.5f;
            const auto colour = toneui::bandColour(b, *this, neon);
            button.setToggleState(b == selectedBand, juce::dontSendNotification);
            button.setColour(juce::TextButton::buttonColourId, colour.withAlpha(enabled ? 0.30f : 0.08f));
            button.setColour(juce::TextButton::buttonOnColourId, colour.withAlpha(enabled ? 0.85f : 0.4f));
            const auto p = themePalette(*this, neon);
            const bool device = componentTheme(*this, neon) == Theme::device;
            button.setColour(juce::TextButton::textColourOffId, enabled ? colour.brighter(0.25f)
                : device ? juce::Colour(0xff89959d) : p.muted);
            button.setColour(juce::TextButton::textColourOnId, device ? juce::Colour(0xffeef2ee)
                : enabled ? p.background : p.text);
        }
        if (isEq() && bandControls.size() == 3) {
            const auto bandMode = int(std::lround(toneui::value(owner, eqBandParameterId(slot, selectedBand, "Mode"))));
            bandControls[1]->setEnabled(bandMode <= 4);
            bandControls[2]->setEnabled(bandMode != 5 && bandMode != 9);
        }
    }
    void updateBands() {
        const auto* count = owner.parameters.getParameter(toneParameterId(slot, "BandCount"));
        const bool three = count != nullptr && count->getValue() >= 0.5f;
        if (controls.size() >= 2) controls[1]->setEnabled(three);
        const auto low = toneui::value(owner, toneParameterId(slot, "CrossoverLow"), 250);
        const auto high = toneui::value(owner, toneParameterId(slot, "CrossoverHigh"), 4000);
        hint.setText(three
            ? "Low / Mid / High: route branches to FX, then recombine. LR4 sum has unity magnitude, not dry phase. Curves show settled requests."
            : "Low / High: high crossover and Mid wiring are dormant, not erased. LR4 sum has unity magnitude, not dry phase.", juce::dontSendNotification);
        if (toneui::value(owner, fxParameterId(slot, "On"), 1) < 0.5f) {
            hint.setText("BYPASSED: one full-band output, not a split. Dormant crossover settings and branch wiring are retained.", juce::dontSendNotification);
        } else if (three && low >= high)
            hint.setText("CROSSOVERS OVERLAP: High is clamped to Low in the response and DSP. Separate the cutoffs to restore a useful Mid band.", juce::dontSendNotification);
    }
    void updateWaveQuality() {
        const bool rawActive = raw.getToggleState();
        quality.setEnabled(qualityAttachment != nullptr && !rawActive);
        qualityLabel.setAlpha(rawActive ? .45f : 1.0f);
    }
    void timerCallback() override {
        if (!isShowing()) return;
        if (graph) { graph->refresh(); updateBandButtons(); }
        else if (isSpatialEq()) updateBandButtons();
        else if (!isWave() && slot >= 1 && slot <= 15) updateBands();
        if (isWave()) {
            updateWaveQuality();
            const int shape = toneui::effectiveWaveShape(owner, slot);
            mode.changeItemText(1, "Legacy: " + juce::String(Waveshaper::shapeNames[size_t(std::clamp(int(toneui::value(owner, toneParameterId(slot, "WaveShape"))), 0, 2))]));
            if (controls.size() > 2) controls[2]->setEnabled(Waveshaper::hasShapeAmount(shape));
        }
        modulationui::refreshWithSelection(*this, owner, owner.getModulationSnapshot(), owner.getModulationState(), owner.modulationAssignmentId());
        mode.repaint(); quality.repaint(); bandOn.repaint(); adaptive.repaint(); raw.repaint(); levelMatch.repaint();
                if (expandedView) expand.setVisible(false);
    }
    modulationui::PanelStyle panelStyle;
    Processor& owner;
    size_t slot, selectedBand = 0;
    NodeType type;
    bool neon = false, expandedView = false, spatialDynamicsVisible = false;
    FluidBackground background;
    juce::Component content;
    PassiveViewport contentViewport;
    juce::Label heading, hint, qualityLabel, dynamicsLabel;
    toneui::PassiveComboBox mode, quality, slope, dynamicsMode;
    juce::TextButton expand, remove;
        modulationui::Button bandOn, adaptive, raw, levelMatch;
    std::array<juce::TextButton, 8> bandButtons;
    std::vector<std::unique_ptr<toneui::Control>> controls, bandControls, dynamicControls;
    std::unique_ptr<toneui::EqGraph> graph;
    std::unique_ptr<toneui::FxMonitorView> visual;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, qualityAttachment;
    std::unique_ptr<juce::ParameterAttachment> spatialShapeAttachment, spatialSlopeAttachment, spatialDynamicModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment, adaptiveAttachment, rawAttachment, levelMatchAttachment;
    std::unique_ptr<ExpandedWindow> expanded;
};
} // namespace vocalstack
