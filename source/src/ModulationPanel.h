#pragma once

#include "DoublerPanel.h"
#include "ModulationUI.h"
#include <functional>

namespace vocalstack {

inline juce::String modulatorName(const Modulator& m) {
    return (m.type == ModulatorType::lfo ? "LFO " : "Random ") + juce::String(m.id);
}

inline juce::Font modulationFont(float size) {
    static const auto family = juce::Font::findAllTypefaceNames().contains("Cascadia Mono")
        ? juce::String("Cascadia Mono") : juce::Font::getDefaultMonospacedFontName();
    return juce::Font(juce::FontOptions(family, size, juce::Font::plain));
}

// Control-thread editor. Neither selection nor refresh resets the audio clock.
class ModulationPanel final : public juce::Component, private juce::Timer {
    class OutputView final : public juce::Component {
    public:
        float output = 0, phase = 0;
        std::vector<float> history;
        void paint(juce::Graphics& g) override {
            auto a = getLocalBounds().toFloat().reduced(5);
            const auto palette = themePalette(*this);
            const bool device = componentTheme(*this) == Theme::device;
            g.setColour(device ? juce::Colour(0xff171f2b) : palette.inset);
            g.fillRoundedRectangle(a, device ? 4.0f : 0.0f);
            g.setColour(device ? juce::Colour(0xff46515b) : palette.border);
            g.drawHorizontalLine(int(a.getCentreY()), a.getX(), a.getRight());
            juce::Path p;
            for (size_t i = 0; i < history.size(); ++i) {
                const float x = a.getX() + a.getWidth() * float(i) / 127.0f;
                const float y = a.getCentreY() - history[i] * a.getHeight() * 0.45f;
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            g.setColour(device ? juce::Colour(0xff64f5e3) : palette.secondary);
            g.strokePath(p, juce::PathStrokeType(1.5f));
            g.setFont(modulationFont(10.0f));
            g.drawText("OUTPUT  " + juce::String(output, 3) + "   phase " + juce::String(phase, 2),
                       a.toNearestInt().removeFromTop(18), juce::Justification::centredLeft);
        }
    } output;
    class NumericStyle final : public juce::LookAndFeel_V4 {
    public:
        void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float value,
                              float start, float end, juce::Slider& slider) override {
            rotary.drawRotarySlider(g, x, y, w, h, value, start, end, slider);
        }
        juce::Label* createSliderTextBox(juce::Slider& slider) override {
            auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
            label->setFont(modulationFont(10.0f));
            return label;
        }
    private:
        modulationui::RotaryStyle rotary;
    };
    struct Numeric final : juce::Component {
        juce::Label label;
        NumericStyle style;
        juce::Slider slider;
        Numeric(const juce::String& name, double lo, double hi, double step) {
            label.setText(name, juce::dontSendNotification); label.setJustificationType(juce::Justification::centred);
            label.setFont(modulationFont(10.0f));
            label.setInterceptsMouseClicks(false, false); addAndMakeVisible(label);
            slider.setLookAndFeel(&style);
            label.setTooltip(name);
            if (name == "Rate / Hz") label.setText("Rate Hz", juce::dontSendNotification);
            if (name == "Beats / cycle") label.setText("Beats", juce::dontSendNotification);
            if (name == "Phase / turns") label.setText("Phase", juce::dontSendNotification);
            if (name == "Output amount") label.setText("Amount", juce::dontSendNotification);
            if (name == "Phase skew") label.setText("Skew", juce::dontSendNotification);
            if (name == "Random smoothing") label.setText("Smooth", juce::dontSendNotification);
                        slider.setName(name); slider.setRange(lo, hi, step);
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxAbove, false, 46, 16);
            slider.setScrollWheelEnabled(false);
            slider.textFromValueFunction = [](double value) { return juce::String(value, 3); };
            slider.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
            slider.setVelocityModeParameters(1.0, 1, 0.0, true, static_cast<juce::ModifierKeys::Flags>(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::commandModifier));
            addAndMakeVisible(slider);
            detail::passiveControl(*this);
        }
        ~Numeric() override { slider.setLookAndFeel(nullptr); }
                void resized() override { auto a = getLocalBounds(); label.setBounds(a.removeFromBottom(16)); slider.setBounds(a); }
    } rate{"Rate / Hz", .001, 100, .001}, division{"Beats / cycle", .015625, 256, 0},
      phase{"Phase / turns", 0, .999, .001}, depth{"Output amount", 0, 1, .001},
      shape{"Phase skew", 0, 1, .001}, smooth{"Random smoothing", 0, 1, .001};
    class AssignmentRow final : public juce::Component {
    public:
        AssignmentRow(ModulationPanel& panel, const Assignment& a) : parent(panel), target(a.targetParameterId) {
            auto* p = parent.owner.parameters.getParameter(target);
            name.setText(parent.targetName(target), juce::dontSendNotification);
            name.setTooltip(target); name.setInterceptsMouseClicks(false, false); addAndMakeVisible(name);
            amount.setLookAndFeel(&style);
                        modulationui::styleRotary(amount, parent.neon);
                        amount.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            amount.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 106, 20);
            amount.setRange(-1, 1, .001); amount.setValue(a.depth, juce::dontSendNotification);
            amount.setDoubleClickReturnValue(true, 0); amount.setScrollWheelEnabled(false);
            amount.setName("Signed assignment depth");
            amount.setVelocityModeParameters(1.0, 1, 0.0, true, static_cast<juce::ModifierKeys::Flags>(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::commandModifier));
            amount.textFromValueFunction = [p](double v) {
                if (p && p->getNormalisableRange().skew == 1) {
                    const auto& range = p->getNormalisableRange();
                    const double delta = v * (range.end - range.start);
                    return (delta >= 0 ? "+" : "") + juce::String(delta, 2) + " " + p->getLabel();
                }
                return (v >= 0 ? "+" : "") + juce::String(v * 100, 1) + "% span";
            };
            amount.valueFromTextFunction = [p](const juce::String& text) {
                if (!p || p->getNormalisableRange().skew != 1) return text.getDoubleValue() / 100;
                const auto& range = p->getNormalisableRange();
                return text.getDoubleValue() / double(range.end - range.start);
            };
            amount.updateText();
            amount.setTooltip("Signed range displacement: linear targets use their units; logarithmic targets use percent of normalized span. Base values are unchanged; effective results are bounded.");
            amount.onValueChange = [this] {
                if (!parent.owner.setModulationDepth(parent.selected, target, float(amount.getValue())))
                    parent.showError("Assignment depth was rejected");
            };
            addAndMakeVisible(amount);
            enabled.setButtonText("On"); enabled.setClickingTogglesState(true);
            enabled.setToggleState(a.enabled, juce::dontSendNotification); addAndMakeVisible(enabled);
            enabled.onClick = [this] { parent.editAssignment(target, false, enabled.getToggleState()); };
            remove.setButtonText("Remove"); addAndMakeVisible(remove);
            remove.onClick = [this] { parent.editAssignment(target, true, false); };
            reveal.setButtonText("Reveal"); addAndMakeVisible(reveal);
            reveal.onClick = [this] { if (parent.onReveal) parent.onReveal(target); };
            detail::passiveControl(*this);
        }
        ~AssignmentRow() override { amount.setLookAndFeel(nullptr); }
        void setNeon(bool neon) {
            modulationui::styleRotary(amount, neon);
            const auto p = themePalette(*this, neon);
            name.setColour(juce::Label::textColourId, p.text);
            for (auto* button : {&enabled, &remove, &reveal}) modulationui::styleButton(*button, p);
        }
        void paint(juce::Graphics& g) override {
            if (componentTheme(*this, parent.neon) == Theme::device) return;
            const auto p = themePalette(*this, parent.neon);
            g.setColour(p.panel);
            g.fillRect(getLocalBounds());
            g.setColour(p.border);
            g.drawRect(getLocalBounds());
        }
        void resized() override {
            auto a = getLocalBounds().reduced(3); name.setBounds(a.removeFromTop(22));
            amount.setBounds(a.removeFromLeft(120)); auto buttons = a.removeFromTop(28);
            enabled.setBounds(buttons.removeFromLeft(42)); reveal.setBounds(buttons.removeFromLeft(62)); remove.setBounds(buttons);
        }
    private:
        ModulationPanel& parent;
        juce::String target;
        juce::Label name;
        modulationui::RotaryStyle style;
        juce::Slider amount;
        juce::TextButton enabled, remove, reveal;
    };
public:
    explicit ModulationPanel(Processor& processor) : owner(processor) {
        for (auto* b : {&controlsTab, &assignmentsTab, &assign, &remove, &sync, &bipolar}) content.addAndMakeVisible(*b);
        controlsTab.setButtonText("Controls"); assignmentsTab.setButtonText("Assignments");
        controlsTab.onClick = [this] { assignments = false; refresh(); };
        assignmentsTab.onClick = [this] { assignments = true; refresh(); };
        assign.setButtonText("Assign"); assign.onClick = [this] {
            if (owner.modulationAssignmentId() == selected) owner.endModulationAssignment();
            else owner.beginModulationAssignment(selected);
            refresh();
        };
        remove.setButtonText("Delete modulator"); remove.onClick = [this] {
            auto state = owner.getModulationState();
            state.modulators.erase(std::remove_if(state.modulators.begin(), state.modulators.end(), [this](const auto& m) { return m.id == selected; }), state.modulators.end());
            state.assignments.erase(std::remove_if(state.assignments.begin(), state.assignments.end(), [this](const auto& a) { return a.modulatorId == selected; }), state.assignments.end());
            if (commit(state)) { selected = 0; if (onDeleted) onDeleted(); }
        };
        sync.setButtonText("Host sync"); bipolar.setButtonText("Bipolar");
        for (auto* b : {&sync, &bipolar}) { b->setClickingTogglesState(true); b->onClick = [this] { saveControls(); }; }
        waveform.addItemList({"Sine", "Triangle", "Saw", "Square"}, 1);
                divisions.addItemList({"1/64", "1/32", "1/16", "1/8 triplet", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2", "1 bar", "2 bars", "4 bars"}, 1);
                divisions.setTextWhenNothingSelected("Sync division preset");
                divisions.onChange = [this] {
                    constexpr std::array<double, 12> beats{.0625, .125, .25, 1.0 / 3, .5, .75, 1, 1.5, 2, 4, 8, 16};
                    const int index = divisions.getSelectedId() - 1;
                    if (index >= 0 && index < int(beats.size())) division.slider.setValue(beats[size_t(index)]);
                };
        retrigger.addItemList({"Free running", "Restart on play", "MIDI note retrigger"}, 1);
        waveform.onChange = [this] { saveControls(); }; retrigger.onChange = [this] { saveControls(); };
        for (auto* c : {&rate, &division, &phase, &depth, &shape, &smooth}) {
            content.addAndMakeVisible(*c); c->slider.onValueChange = [this] { saveControls(); };
        }
        rate.slider.setSkewFactorFromMidPoint(1); division.slider.setSkewFactorFromMidPoint(1);
        for (auto* c : {&rate, &division, &phase, &depth, &shape, &smooth})
            c->slider.setDoubleClickReturnValue(true, c == &rate || c == &division || c == &depth ? 1 : c == &shape ? .5 : 0);
        content.addAndMakeVisible(waveform); content.addAndMakeVisible(retrigger); content.addAndMakeVisible(divisions); content.addAndMakeVisible(output);
        title.setFont(modulationFont(11.0f));
        message.setFont(modulationFont(10.0f));
        content.addAndMakeVisible(title);
        content.addAndMakeVisible(message); message.setJustificationType(juce::Justification::topLeft);
        rowsViewport.setViewedComponent(&rowsArea, false); rowsViewport.setScrollBarsShown(false, true); content.addAndMakeVisible(rowsViewport);
        contentViewport.setViewedComponent(&content, false);
        contentViewport.setScrollBarsShown(true, true);
        juce::Component::addAndMakeVisible(contentViewport);
        detail::passiveControl(*this); startTimerHz(20);
    }
    ~ModulationPanel() override {
        setLookAndFeel(nullptr);
        contentViewport.setViewedComponent(nullptr, false);
        rowsViewport.setViewedComponent(nullptr, false);
    }
    static constexpr int preferredHeight = 240;
    void select(uint32_t id) {
        if (selected != id) {
            output.history.clear(); output.output = output.phase = 0;
            contentViewport.setViewPosition(0, 0); rowsViewport.setViewPosition(0, 0);
        }
        selected = id; refresh();
    }
    uint32_t selectedId() const { return selected; }
    void setNeon(bool neon) {
        this->neon = neon;
        const auto p = themePalette(*this, neon);
        const bool device = componentTheme(*this, neon) == Theme::device;
        panelStyle.setPalette(p);
        setLookAndFeel(device ? nullptr : &panelStyle);
        title.setColour(juce::Label::textColourId, p.text);
        message.setColour(juce::Label::textColourId, p.muted);
        for (auto* c : {&rate, &division, &phase, &depth, &shape, &smooth}) {
            c->label.setColour(juce::Label::textColourId, p.text);
            modulationui::styleRotary(c->slider, neon);
            detail::passiveControl(c->slider);
        }
        for (auto& row : rows) row->setNeon(neon);
        for (auto* b : {&controlsTab, &assignmentsTab, &assign, &remove, &sync, &bipolar})
            modulationui::styleButton(*b, p);
        for (auto* combo : {&waveform, &retrigger, &divisions}) modulationui::styleCombo(*combo, p);
        modulationui::styleViewport(contentViewport, p);
        modulationui::styleViewport(rowsViewport, p);
        repaint();
    }
    void paint(juce::Graphics& g) override {
        if (componentTheme(*this, neon) != Theme::device) g.fillAll(themePalette(*this, neon).panel);
    }
    std::function<void(const juce::String&)> onReveal;
    std::function<void()> onDeleted;
    void resized() override {
        contentViewport.setBounds(getLocalBounds());
        // Keep control geometry stable; small windows scroll instead of shrinking knobs.
        const int scrollbar = contentViewport.getScrollBarThickness();
        content.setSize(juce::jmax(660, getWidth() - scrollbar), juce::jmax(preferredHeight, getHeight()) - scrollbar);
        contentViewport.setSingleStepSizes(32, 32);
        layoutContent();
    }
private:
    void layoutContent() {
        auto a = content.getLocalBounds().reduced(6);
        auto header = a.removeFromTop(28);
        title.setBounds(header.removeFromLeft(140));
        controlsTab.setBounds(header.removeFromLeft(100)); header.removeFromLeft(4);
        assignmentsTab.setBounds(header.removeFromLeft(110)); header.removeFromLeft(12);
        assign.setBounds(header.removeFromLeft(100)); header.removeFromLeft(8);
        remove.setBounds(header.removeFromLeft(145));
        a.removeFromTop(6);
        message.setBounds(a.removeFromBottom(24));
        if (assignments) {
            rowsViewport.setBounds(a);
            rowsArea.setSize(juce::jmax(a.getWidth(), int(rows.size()) * 320), 106);
            int x = 0; for (auto& row : rows) { row->setBounds(x, 0, 312, 106); x += 320; }
            return;
        }
        output.setBounds(a.removeFromLeft(220)); a.removeFromLeft(8);
        auto options = a.removeFromLeft(96);
        waveform.setBounds(options.removeFromTop(26)); options.removeFromTop(4);
        retrigger.setBounds(options.removeFromTop(26)); options.removeFromTop(4);
        divisions.setBounds(options.removeFromTop(26)); options.removeFromTop(4);
        auto switches = options.removeFromTop(26);
        sync.setBounds(switches.removeFromLeft(48)); bipolar.setBounds(switches);
                sync.setButtonText("Sync");
                sync.setTooltip("Host tempo sync");
        a.removeFromLeft(8);
        int index = 0;
        for (auto* c : {&rate, &division, &phase, &depth, &shape, &smooth}) {
            c->setBounds(a.getX() + (index % 3) * 52, a.getY() + (index / 3) * 76, 52, 76);
            ++index;
        }
    }
private:
    juce::String targetName(const juce::String& id) const {
        juce::String machine = "Main";
        if (id.startsWith("lead")) machine = "Lead";
        else if (id.startsWith("voice")) machine = "Voice " + id.substring(5).retainCharacters("0123456789");
        else if (id.startsWith("fx")) {
            const int slot = id.substring(2).getIntValue(); machine = "FX " + juce::String(slot);
            for (const auto& n : owner.getRoutingGraph().nodes) if (routingFxSlot(n.id) == slot) {
                machine = (n.type == NodeType::waveshaper ? "Waveshaper " : n.type == NodeType::equalizer ? "EQ8 " : n.type == NodeType::crossover ? "Multiband " : "Chorale ") + juce::String(slot); break;
            }
        }
        auto* p = owner.parameters.getParameter(id);
        return machine + " / " + (p ? p->getName(80) : id);
    }
    bool commit(const ModulationState& state) {
        juce::String error; const bool ok = owner.setModulationState(state, error);
        if (!ok) showError(error);
                return ok;
    }
    void showError(const juce::String& error) { message.setText(error, juce::dontSendNotification); }
    void editAssignment(const juce::String& target, bool erase, bool enabled) {
        auto state = owner.getModulationState();
        for (auto it = state.assignments.begin(); it != state.assignments.end(); ++it)
            if (it->modulatorId == selected && it->targetParameterId == target) {
                if (erase) state.assignments.erase(it); else it->enabled = enabled;
                break;
            }
        if (commit(state)) refreshRows = true;
    }
    void saveControls() {
        if (loading) return;
        auto state = owner.getModulationState();
        for (auto& m : state.modulators) if (m.id == selected) {
            m.rateHz = float(rate.slider.getValue()); m.division = float(division.slider.getValue());
            m.phase = float(phase.slider.getValue()); m.depth = float(depth.slider.getValue());
            m.shape = float(shape.slider.getValue()); m.smoothing = float(smooth.slider.getValue());
            m.sync = sync.getToggleState(); m.bipolar = bipolar.getToggleState();
            m.waveform = static_cast<ModulationWaveform>(waveform.getSelectedId() - 1);
            m.retrigger = static_cast<ModulationRetrigger>(retrigger.getSelectedId() - 1);
        }
        commit(state); rate.setEnabled(!sync.getToggleState()); division.setEnabled(sync.getToggleState());
    }
    void refresh() {
        const auto state = owner.getModulationState(); loading = true;
        for (const auto& m : state.modulators) if (m.id == selected) {
            title.setText(modulatorName(m), juce::dontSendNotification);
            rate.slider.setValue(m.rateHz, juce::dontSendNotification); division.slider.setValue(m.division, juce::dontSendNotification);
            phase.slider.setValue(m.phase, juce::dontSendNotification); depth.slider.setValue(m.depth, juce::dontSendNotification);
            shape.slider.setValue(m.shape, juce::dontSendNotification); smooth.slider.setValue(m.smoothing, juce::dontSendNotification);
            sync.setToggleState(m.sync, juce::dontSendNotification); bipolar.setToggleState(m.bipolar, juce::dontSendNotification);
            waveform.setSelectedId(int(m.waveform) + 1, juce::dontSendNotification); retrigger.setSelectedId(int(m.retrigger) + 1, juce::dontSendNotification);
            waveform.setEnabled(m.type == ModulatorType::lfo); shape.setEnabled(m.type == ModulatorType::lfo); smooth.setEnabled(m.type == ModulatorType::random);
        }
        loading = false;
        for (auto* c : {&rate, &division, &phase, &depth, &shape, &smooth}) c->setVisible(!assignments);
        for (auto* c : std::array<juce::Component*, 6>{&waveform, &retrigger, &divisions, &sync, &bipolar, &output}) c->setVisible(!assignments);
        rate.setEnabled(!sync.getToggleState()); division.setEnabled(sync.getToggleState());
        controlsTab.setToggleState(!assignments, juce::dontSendNotification); assignmentsTab.setToggleState(assignments, juce::dontSendNotification);
        assign.setToggleState(owner.modulationAssignmentId() == selected, juce::dontSendNotification);
        rowsViewport.setVisible(assignments); rows.clear();
        for (const auto& a : state.assignments) if (a.modulatorId == selected) {
            auto row = std::make_unique<AssignmentRow>(*this, a);
            rowsArea.addAndMakeVisible(*row);
            row->setNeon(neon);
            rows.push_back(std::move(row));
        }
        message.setText(assignments ? "Signed depth uses target units for linear controls, percent span for logarithmic controls. Base values stay unchanged; effective values are bounded. Reveal opens the target."
            : "Assign: drag a target to set depth. Esc cancels. Sync: quarter notes / cycle.", juce::dontSendNotification);
        resized();
    }
    void timerCallback() override {
        if (refreshRows) { refreshRows = false; refresh(); }
        if (!isShowing()) return;
        juce::String key;
        for (const auto& a : owner.getModulationState().assignments) if (a.modulatorId == selected) key += a.targetParameterId + (a.enabled ? "+" : "-");
        if (key != rowSignature) { rowSignature = key; refresh(); }
        assign.setToggleState(owner.modulationAssignmentId() == selected, juce::dontSendNotification);
        const auto snapshot = owner.getModulationSnapshot();
                if (!snapshot.consistent) return;
                for (const auto& m : snapshot.modulators) if (m.id == selected) {
            output.output = m.output; output.phase = m.phase;
            output.history.push_back(m.output); if (output.history.size() > 128) output.history.erase(output.history.begin()); output.repaint();
        }
    }
    modulationui::PanelStyle panelStyle;
    Processor& owner;
    uint32_t selected = 0;
    bool loading = false, assignments = false, refreshRows = false, neon = false;
    juce::String rowSignature;
    juce::TextButton controlsTab, assignmentsTab, assign, remove, sync, bipolar;
    class PassiveCombo final : public juce::ComboBox {
            void lookAndFeelChanged() override {
                            juce::ComboBox::lookAndFeelChanged();
                            for (auto* child : getChildren())
                                if (auto* label = dynamic_cast<juce::Label*>(child)) label->setFont(modulationFont(10.0f));
                            detail::passiveControl(*this);
                        }
        };
        PassiveCombo waveform, retrigger, divisions;
    juce::Component content;
    juce::Viewport contentViewport;
    juce::Label title, message;
    juce::Component rowsArea;
    juce::Viewport rowsViewport;
    std::vector<std::unique_ptr<AssignmentRow>> rows;
};
} // namespace vocalstack
