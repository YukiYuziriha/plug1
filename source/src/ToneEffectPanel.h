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
        setName(isWave() ? "Waveshaper inspector" : isEq() ? "EQ8 inspector" : "Band routing inspector");
                mode.setModulationOwner(owner); quality.setModulationOwner(owner);
                bandOn.setModulationOwner(owner); adaptive.setModulationOwner(owner); raw.setModulationOwner(owner); levelMatch.setModulationOwner(owner);
        heading.setText(isWave() ? "WAVESHAPER" : isEq() ? "EQ8" : "BAND ROUTING", juce::dontSendNotification);
        heading.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        heading.setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(heading);
        hint.setFont(juce::Font(juce::FontOptions(11.0f)));
        hint.setJustificationType(juce::Justification::topLeft);
        hint.setInterceptsMouseClicks(false, false);
        content.addAndMakeVisible(hint);
        if (slot < 1 || slot > 15) {
            hint.setText("Select an effect in automation slots 1-15.", juce::dontSendNotification);
        } else if (isWave()) setupWave();
        else if (isEq()) setupEq();
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
        contentViewport.setViewedComponent(nullptr, false);
        expanded.reset();
        // Attachments must be detached before their controls are destroyed.
        modeAttachment.reset();
        qualityAttachment.reset();
        enabledAttachment.reset();
        adaptiveAttachment.reset();
        levelMatchAttachment.reset();
    }
    static constexpr int preferredWidth = 400;
    static constexpr int preferredHeight = 650;
    void setNeon(bool enabled) {
        neon = enabled;
        const auto accent = juce::Colour(neon ? 0xff64f5e3 : 0xfff5a24b);
        heading.setColour(juce::Label::textColourId, accent);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xffaebbc2));
        for (auto& c : controls) c->setNeon(neon);
        for (auto& c : bandControls) c->setNeon(neon);
        for (auto* b : std::initializer_list<juce::TextButton*>{&expand, &bandOn, &remove, &adaptive, &raw, &levelMatch}) {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(neon ? 0xff202b40 : 0xff303932));
            b->setColour(juce::TextButton::buttonOnColourId, accent);
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff151b20));
        }
        for (auto* combo : {&mode, &quality}) {
            combo->setColour(juce::ComboBox::backgroundColourId, juce::Colour(neon ? 0xff202b40 : 0xff303932));
            combo->setColour(juce::ComboBox::textColourId, juce::Colour(0xffe4e9ee));
            combo->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff526071));
        }
        qualityLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb8c4ca));
        if (graph) graph->setNeon(neon);
        if (visual) visual->setNeon(neon);
        if (expanded) expanded->setNeon(neon);
        updateBandButtons();
        repaint();
    }
    void revealParameter(const juce::String& id) {
        if (isEq()) for (size_t band = 0; band < 8; ++band)
            if (id.startsWith("fx" + juce::String(int(slot)) + "EqBand" + juce::String(int(band + 1)))) { selectBand(band); break; }
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(neon ? 0xff111827 : 0xff252826));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5);
    }
    void resized() override {
        contentViewport.setBounds(getLocalBounds());
        const int height = juce::jmax(preferredHeight, getHeight());
        const int scrollbar = height > getHeight() ? contentViewport.getScrollBarThickness() : 0;
        content.setSize(juce::jmax(1, getWidth() - scrollbar), height);
        layoutContent();
    }
private:
    void layoutContent() {
        auto area = content.getLocalBounds().reduced(8);
        auto top = area.removeFromTop(23);
        if (isEq()) expand.setBounds(top.removeFromRight(91));
        heading.setBounds(top);
        area.removeFromTop(5);
        if (graph) {
            // Keep the response plot generous; short panels scroll the rotary
            // controls rather than taking their height from the plot.
            const int graphHeight = juce::jlimit(260, 480, content.getHeight() - 365);
            graph->setBounds(area.removeFromTop(graphHeight));
            area.removeFromTop(5);
            auto selectors = area.removeFromTop(23);
            for (size_t b = 0; b < bandButtons.size(); ++b) {
                const int remaining = int(bandButtons.size() - b);
                bandButtons[b].setBounds(selectors.removeFromLeft(selectors.getWidth() / remaining).reduced(1, 0));
            }
            area.removeFromTop(4);
            auto bandRow = area.removeFromTop(24);
            bandOn.setBounds(bandRow.removeFromLeft(65));
            remove.setBounds(bandRow.removeFromRight(58));
            mode.setBounds(bandRow.reduced(3, 0));
            area.removeFromTop(3);
            if (bandControls.size() == 3) {
                auto row = area.removeFromTop(104);
                const int width = row.getWidth() / 3;
                for (auto& control : bandControls) control->setBounds(row.removeFromLeft(width).reduced(2, 0));
            }
            area.removeFromTop(3);
            if (controls.size() >= 2) {
                auto pair = area.removeFromTop(100);
                controls[0]->setBounds(pair.removeFromLeft(pair.getWidth() / 2).withTrimmedRight(4));
                controls[1]->setBounds(pair.withTrimmedLeft(4));
            }
            adaptive.setBounds(area.removeFromTop(23));
            hint.setBounds(area);
        } else {
            if (mode.getNumItems() > 0) {
                mode.setBounds(area.removeFromTop(29));
                area.removeFromTop(10);
            }
            if (visual) {
                visual->setBounds(area.removeFromTop(isWave() ? 240 : 260));
                area.removeFromTop(6);
            }
            if (isWave()) {
                auto toggles = area.removeFromTop(25);
                raw.setBounds(toggles.removeFromLeft(toggles.getWidth() * 3 / 5));
                levelMatch.setBounds(toggles);
                area.removeFromTop(4);
                auto qualityRow = area.removeFromTop(29);
                qualityLabel.setBounds(qualityRow.removeFromLeft(62)); quality.setBounds(qualityRow);
                area.removeFromTop(4);
            }
            const int columns = 2;
            const int width = area.getWidth() / columns;
            const int topY = area.getY();
            for (size_t i = 0; i < controls.size(); ++i)
                controls[i]->setBounds(area.getX() + int(i % size_t(columns)) * width, topY + int(i / size_t(columns)) * 104, width, 100);
            area.removeFromTop(int((controls.size() + size_t(columns) - 1) / size_t(columns)) * 104);
            area.removeFromTop(8);
            hint.setBounds(area);
        }
    }
private:
    class ExpandedWindow final : public juce::DocumentWindow {
    public:
        ExpandedWindow(Processor& owner, size_t slot, NodeType type)
            : juce::DocumentWindow("EQ8 | expanded editor", juce::Colour(0xff20262d), juce::DocumentWindow::closeButton) {
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
        void setNeon(bool enabled) { panel->setNeon(enabled); }
    private:
        ToneEffectPanel* panel = nullptr; // owned by DocumentWindow
    };
    // These values are the appended Routing.h contract; keeping this header
    // independent of enum spellings permits parallel routing/backend work.
    bool isWave() const { return type == static_cast<NodeType>(6); }
    bool isEq() const { return type == static_cast<NodeType>(7); }
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
            if (!expanded) expanded = std::make_unique<ExpandedWindow>(owner, slot, type);
            expanded->setNeon(neon);
            expanded->setVisible(true);
            expanded->toFront(false);
        };
        content.addAndMakeVisible(expand);
        for (size_t b = 0; b < bandButtons.size(); ++b) {
            auto& button = bandButtons[b];
            button.setButtonText(juce::String(int(b + 1)));
            button.setName("Select EQ band " + juce::String(int(b + 1)));
            button.setTooltip("Select band " + juce::String(int(b + 1)) + "; selection does not alter audio");
            button.onClick = [this, b] { selectBand(b); };
            content.addAndMakeVisible(button);
        }
        for (size_t m = 0; m < toneui::modeLabels.size(); ++m) mode.addItem(toneui::modeLabels[m], int(m + 1));
        mode.setName("Band filter mode");
        bandOn.setButtonText("On");
        bandOn.setClickingTogglesState(true);
        bandOn.setTooltip("Enable this band, retaining its frequency, gain, Q and mode");
        content.addAndMakeVisible(bandOn);
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
        hint.setText("Click: bell | drag ends: shelf / sides: cut\nBottom: notch | wheel: Q | FFT: right dBFS scale", juce::dontSendNotification);
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
    void selectBand(size_t band) {
        if (band > 7) return;
        // Re-selecting the current chip must not interrupt a live text/slider edit.
        if (selectedBand == band && !bandControls.empty()) return;
        selectedBand = band;
        modeAttachment.reset();
        enabledAttachment.reset();
        bandControls.clear();
        attachMode(eqBandParameterId(slot, band, "Mode"));
        const auto onId = eqBandParameterId(slot, band, "On");
        bandOn.setComponentID(onId);
        if (owner.parameters.getParameter(onId)) {
            bandOn.setEnabled(true);
            enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, onId, bandOn);
        } else bandOn.setEnabled(false);
        const std::array<const char*, 3> fields{"Frequency", "Gain", "Q"};
        const std::array<const char*, 3> titles{"Frequency", "Gain", "Q"};
        const std::array<const char*, 3> units{"Hz", "dB", "Q"};
        for (size_t i = 0; i < fields.size(); ++i) {
            auto c = std::make_unique<toneui::Control>(owner, eqBandParameterId(slot, band, fields[i]), titles[i], units[i]);
            c->setNeon(neon);
            content.addAndMakeVisible(*c);
            bandControls.push_back(std::move(c));
        }
        graph->select(band);
        updateBandButtons();
        detail::passiveControl(*this);
        resized();
    }
    void updateBandButtons() {
        if (!isEq()) return;
        for (size_t b = 0; b < bandButtons.size(); ++b) {
            auto& button = bandButtons[b];
            const bool enabled = toneui::value(owner, eqBandParameterId(slot, b, "On")) >= 0.5f;
            const auto colour = toneui::bandColour(b);
            button.setToggleState(b == selectedBand, juce::dontSendNotification);
            button.setColour(juce::TextButton::buttonColourId, colour.withAlpha(enabled ? 0.30f : 0.08f));
            button.setColour(juce::TextButton::buttonOnColourId, colour.withAlpha(enabled ? 0.85f : 0.4f));
            button.setColour(juce::TextButton::textColourOffId, enabled ? colour.brighter(0.25f) : juce::Colour(0xff89959d));
            button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffeef2ee));
        }
        const auto bandMode = int(std::lround(toneui::value(owner, eqBandParameterId(slot, selectedBand, "Mode"))));
        if (bandControls.size() == 3) {
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
    Processor& owner;
    size_t slot, selectedBand = 0;
    NodeType type;
    bool neon = false, expandedView = false;
    juce::Component content;
    juce::Viewport contentViewport;
    juce::Label heading, hint, qualityLabel;
    toneui::PassiveComboBox mode, quality;
    juce::TextButton expand, remove;
        modulationui::Button bandOn, adaptive, raw, levelMatch;
    std::array<juce::TextButton, 8> bandButtons;
    std::vector<std::unique_ptr<toneui::Control>> controls, bandControls;
    std::unique_ptr<toneui::EqGraph> graph;
    std::unique_ptr<toneui::FxMonitorView> visual;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, qualityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment, adaptiveAttachment, rawAttachment, levelMatchAttachment;
    std::unique_ptr<ExpandedWindow> expanded;
};
} // namespace vocalstack
