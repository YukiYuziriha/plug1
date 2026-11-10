#pragma once

#include "DoublerPanel.h"
#include "ToneEqGraph.h"
#include "Waveshaper.h"
#include <memory>
#include <vector>

namespace vocalstack {
namespace toneui {
// JUCE replaces a ComboBox's internal label on hierarchy/skin changes. Apply
// the host-focus policy when the replacement is made, not on a polling timer.
class PassiveComboBox final : public juce::ComboBox {
public:
    PassiveComboBox() { detail::passiveControl(*this); }
    void lookAndFeelChanged() override {
        juce::ComboBox::lookAndFeelChanged();
        detail::passiveControl(*this);
    }
};

// Compact label-above control for paired rows. Attachments own automation
// gestures; graph edits never assign slider values or rebuild live DSP objects.
class Control final : public juce::Component {
public:
    Control(Processor& owner, const juce::String& id, const juce::String& name, const char* unit) {
        label.setText(name, juce::dontSendNotification);
        label.setFont(juce::Font(juce::FontOptions(11.0f)));
        label.setColour(juce::Label::textColourId, juce::Colour(0xffb8c4ca));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
        slider.setLookAndFeel(&look);
        slider.setName(name);
        slider.setTitle(name);
        slider.setComponentID(id);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
        slider.setScrollWheelEnabled(false);
        addAndMakeVisible(slider);
        if (auto* p = owner.parameters.getParameter(id)) {
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, id, slider);
            slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        } else setEnabled(false);
        const auto suffix = juce::String(unit);
        slider.textFromValueFunction = [suffix](double v) {
            if (suffix == "Hz") return v >= 1000 ? juce::String(v / 1000, 2) + " kHz" : juce::String(v, 0) + " Hz";
            return juce::String(v, suffix == "Q" ? 2 : 1) + (suffix == "Q" ? "" : " " + suffix);
        };
        slider.valueFromTextFunction = [suffix](const juce::String& text) {
            const auto v = text.getDoubleValue();
            return suffix == "Hz" && text.containsIgnoreCase("k") ? v * 1000 : v;
        };
        slider.updateText();
        detail::passiveControl(*this);
    }
    ~Control() override { slider.setLookAndFeel(nullptr); }
    void resized() override {
        auto a = getLocalBounds();
        label.setBounds(a.removeFromTop(13));
        slider.setBounds(a);
    }
    void setNeon(bool neon) { detail::styleSlider(slider, neon); }
    juce::Slider slider;
private:
    detail::ParameterLookAndFeel look;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
} // namespace toneui

// Native RoutingPanel inspector content. The parent supplies the machine's
// fxOn/bypass control. slot 1..15 is a stable automation identity, not a node ID.
class ToneEffectPanel final : public juce::Component, private juce::Timer {
public:
    ToneEffectPanel(Processor& processor, size_t fxSlot, NodeType nodeType)
        : owner(processor), slot(fxSlot), type(nodeType) {
        setName(isWave() ? "Waveshaper inspector" : isEq() ? "EQ8 inspector" : "Band routing inspector");
        heading.setText(isWave() ? "WAVESHAPER" : isEq() ? "EQ8" : "BAND ROUTING", juce::dontSendNotification);
        heading.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        heading.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(heading);
        hint.setFont(juce::Font(juce::FontOptions(11.0f)));
        hint.setJustificationType(juce::Justification::topLeft);
        hint.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(hint);
        if (slot < 1 || slot > 15) {
            hint.setText("Select an effect in automation slots 1-15.", juce::dontSendNotification);
        } else if (isWave()) setupWave();
        else if (isEq()) setupEq();
        else setupBands();
        setNeon(false);
        detail::passiveControl(*this);
        startTimerHz(30);
    }
    ~ToneEffectPanel() override {
        stopTimer();
        expanded.reset();
        // Attachments must be detached before their controls are destroyed.
        modeAttachment.reset();
        enabledAttachment.reset();
        adaptiveAttachment.reset();
    }
    static constexpr int preferredWidth = 400;
    static constexpr int preferredHeight = 550;
    void setNeon(bool enabled) {
        neon = enabled;
        const auto accent = juce::Colour(neon ? 0xff64f5e3 : 0xfff5a24b);
        heading.setColour(juce::Label::textColourId, accent);
        hint.setColour(juce::Label::textColourId, juce::Colour(0xffaebbc2));
        for (auto& c : controls) c->setNeon(neon);
        for (auto& c : bandControls) c->setNeon(neon);
        for (auto* b : {&expand, &bandOn, &remove, &adaptive}) {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(neon ? 0xff202b40 : 0xff303932));
            b->setColour(juce::TextButton::buttonOnColourId, accent);
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff151b20));
        }
        mode.setColour(juce::ComboBox::backgroundColourId, juce::Colour(neon ? 0xff202b40 : 0xff303932));
        mode.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe4e9ee));
        mode.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff526071));
        if (graph) graph->setNeon(neon);
        if (visual) visual->setNeon(neon);
        if (expanded) expanded->setNeon(neon);
        updateBandButtons();
        repaint();
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(neon ? 0xff111827 : 0xff252826));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        auto top = area.removeFromTop(23);
        if (isEq()) expand.setBounds(top.removeFromRight(91));
        heading.setBounds(top);
        area.removeFromTop(5);
        if (graph) {
            // At 400 x 550 the plot is ~339 x 220 including axis gutters.
            // A narrow inspector stays usable; Expand provides a larger canvas.
            const int graphHeight = juce::jlimit(130, 480, getHeight() - 285);
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
                bandControls[0]->setBounds(area.removeFromTop(36));
                auto pair = area.removeFromTop(36);
                bandControls[1]->setBounds(pair.removeFromLeft(pair.getWidth() / 2).withTrimmedRight(4));
                bandControls[2]->setBounds(pair.withTrimmedLeft(4));
            }
            area.removeFromTop(3);
            if (controls.size() >= 2) {
                auto pair = area.removeFromTop(36);
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
            for (auto& control : controls) {
                control->setBounds(area.removeFromTop(48));
                area.removeFromTop(6);
            }
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
            setResizeLimits(400, 550, 1400, 1000);
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
        addAndMakeVisible(*c);
        controls.push_back(std::move(c));
    }
    void attachMode(const juce::String& id) {
        modeAttachment.reset();
        mode.setComponentID(id);
        mode.setWantsKeyboardFocus(false);
        mode.setMouseClickGrabsKeyboardFocus(false);
        addAndMakeVisible(mode);
        if (owner.parameters.getParameter(id)) {
            mode.setEnabled(true);
            modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.parameters, id, mode);
        } else mode.setEnabled(false);
    }
    void setupWave() {
        visual = std::make_unique<toneui::WaveshaperVisual>(owner, slot);
        addAndMakeVisible(*visual);
        for (size_t i = 0; i < Waveshaper::shapeNames.size(); ++i)
            mode.addItem(Waveshaper::shapeNames[i], int(i + 1));
        mode.setName("Waveshaper shape");
        mode.setTooltip("Soft tanh, hard clip, or asymmetric saturation");
        attachMode(toneParameterId(slot, "WaveShape"));
        addControl("WaveDrive", "Drive", "dB");
        addControl("WaveOutput", "Output trim", "dB");
        auto mix = std::make_unique<toneui::Control>(owner, fxParameterId(slot, "Mix"), "Dry / wet", "%");
        mix->slider.setTooltip("True dry/wet blend; drive can increase level. Output trim is not a limiter.");
        addAndMakeVisible(*mix);
        controls.push_back(std::move(mix));
        hint.setText("Drive adds real gain. Trim for headroom.\nADAA is not oversampling or alias-free.", juce::dontSendNotification);
    }
    void setupEq() {
        graph = std::make_unique<toneui::EqGraph>(owner, slot);
        graph->onSelect = [this](size_t band) { selectBand(band); };
        addAndMakeVisible(*graph);
        expand.setButtonText("Expand EQ");
        expand.setTooltip("Open a larger, resizable EQ editor. Both views follow host automation.");
        expand.onClick = [this] {
            if (!expanded) expanded = std::make_unique<ExpandedWindow>(owner, slot, type);
            expanded->setNeon(neon);
            expanded->setVisible(true);
            expanded->toFront(false);
        };
        addAndMakeVisible(expand);
        for (size_t b = 0; b < bandButtons.size(); ++b) {
            auto& button = bandButtons[b];
            button.setButtonText(juce::String(int(b + 1)));
            button.setName("Select EQ band " + juce::String(int(b + 1)));
            button.setTooltip("Select band " + juce::String(int(b + 1)) + "; selection does not alter audio");
            button.onClick = [this, b] { selectBand(b); };
            addAndMakeVisible(button);
        }
        for (size_t m = 0; m < toneui::modeLabels.size(); ++m) mode.addItem(toneui::modeLabels[m], int(m + 1));
        mode.setName("Band filter mode");
        bandOn.setButtonText("On");
        bandOn.setClickingTogglesState(true);
        bandOn.setTooltip("Enable this band, retaining its frequency, gain, Q and mode");
        addAndMakeVisible(bandOn);
        remove.setButtonText("Remove");
        remove.setTooltip("Disable the selected band; its stable automation slot and values are retained");
        remove.onClick = [this] {
            toneui::edit(owner, eqBandParameterId(slot, selectedBand, "On"), 0);
            if (graph) graph->refresh();
            updateBandButtons();
        };
        addAndMakeVisible(remove);
        addControl("EqShift", "Frequency shift", "st");
        controls.back()->slider.setTooltip("Shift all band frequencies in semitones; graph handles show shifted frequency");
        addControl("EqGain", "Global output", "dB");
        adaptive.setButtonText("Adaptive Q");
        adaptive.setClickingTogglesState(true);
        adaptive.setTooltip("Narrow bell bandwidth with increasing absolute band gain");
        addAndMakeVisible(adaptive);
        const auto id = toneParameterId(slot, "EqAdaptiveQ");
        if (owner.parameters.getParameter(id))
            adaptiveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, id, adaptive);
        else adaptive.setEnabled(false);
        hint.setText("Click: bell | drag ends: shelf / sides: cut\nBottom: notch | wheel: Q | FFT: right dBFS scale", juce::dontSendNotification);
        selectBand(0);
    }
    void setupBands() {
        visual = std::make_unique<toneui::CrossoverVisual>(owner, slot);
        addAndMakeVisible(*visual);
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
        const std::array<const char*, 3> titles{"Band frequency (before shift)", "Band gain", "Q"};
        const std::array<const char*, 3> units{"Hz", "dB", "Q"};
        for (size_t i = 0; i < fields.size(); ++i) {
            auto c = std::make_unique<toneui::Control>(owner, eqBandParameterId(slot, band, fields[i]), titles[i], units[i]);
            c->setNeon(neon);
            addAndMakeVisible(*c);
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
    void timerCallback() override {
        if (!isShowing()) return;
        if (graph) { graph->refresh(); updateBandButtons(); }
        else if (!isWave() && slot >= 1 && slot <= 15) updateBands();
        if (expandedView) expand.setVisible(false);
    }
    Processor& owner;
    size_t slot, selectedBand = 0;
    NodeType type;
    bool neon = false, expandedView = false;
    juce::Label heading, hint;
    toneui::PassiveComboBox mode;
    juce::TextButton expand, bandOn, remove, adaptive;
    std::array<juce::TextButton, 8> bandButtons;
    std::vector<std::unique_ptr<toneui::Control>> controls, bandControls;
    std::unique_ptr<toneui::EqGraph> graph;
    std::unique_ptr<toneui::FxMonitorView> visual;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment, adaptiveAttachment;
    std::unique_ptr<ExpandedWindow> expanded;
};
} // namespace vocalstack
