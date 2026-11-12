#pragma once

#include "MainBusFxRack.h"
#include "ToneEffectPanel.h"
#include "CreativeEffectPanel.h"
#include "UtilityEffectPanel.h"

namespace vocalstack {

// Host-bound cards over the processor's graph. No private graph/FX-slot pool.
class MainBusFxRackPanel final : public juce::Component, private juce::Timer {
public:
    explicit MainBusFxRackPanel(Processor& processor) : owner(processor) {
        setName("Main Bus FX rack"); setComponentID("mainBusFxRack");
        add.setButtonText("+ FX"); add.setComponentID("mainBusAddFx");
        add.onClick = [this] { showAddMenu(); };
        addAndMakeVisible(add); addAndMakeVisible(status); addAndMakeVisible(viewport);
        viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(true, false);
        status.setFont(juce::Font(juce::FontOptions(11.f)));
        detail::passiveControl(*this); sync(); startTimerHz(12);
    }
    // The editor supplies RoutingPanel's existing validated, one-step graph undo path.
    std::function<bool(const Graph&, juce::String&)> commitGraph;
    void setNeon(bool value) {
        neon = value;
        const auto p = themePalette(*this, neon);
        status.setColour(juce::Label::textColourId, p.secondary);
        add.setColour(juce::TextButton::buttonColourId, p.inset);
        add.setColour(juce::TextButton::textColourOffId, p.text);
        for (auto& card : cards) card->setNeon(value);
        repaint();
    }
    void sync() {
        const auto graph = owner.getRoutingGraph(); const auto segment = mainBusFx::discover(graph);
        juce::String signature;
        for (auto id : segment.effects) if (const auto* node = mainBusFx::find(graph, id))
            signature += juce::String(id) + ":" + juce::String(int(node->type)) + ";";
        if (signature != cardSignature) {
            auto previous = std::move(cards); cards.clear();
            for (auto id : segment.effects) if (const auto* node = mainBusFx::find(graph, id)) {
                std::unique_ptr<Card> card;
                for (auto& old : previous) if (old && old->model.id == id && old->model.type == node->type) { card = std::move(old); break; }
                if (!card) card = std::make_unique<Card>(*this, *node);
                content.addAndMakeVisible(*card); cards.push_back(std::move(card));
            }
            cardSignature = signature; resized(); setNeon(neon);
        }
        const auto capacity = mainBusFx::capacityProblem(graph);
        const auto reason = !segment.editable() ? segment.unavailable : !commitGraph
            ? std::string("Shared Routing Undo bridge unavailable; parameter editing remains active.") : capacity;
        add.setEnabled(segment.editable() && capacity.empty() && bool(commitGraph));
        add.setTooltip(juce::String(reason));
        for (auto& card : cards) {
            card->remove.setEnabled(segment.editable() && bool(commitGraph));
            card->handle.setEnabled(segment.editable() && bool(commitGraph));
            card->remove.setTooltip(segment.editable() ? "Remove this routing node (Undo in Routing)" : juce::String(segment.unavailable));
            card->handle.setTooltip(segment.editable() ? "Drag vertically to reorder this FX before Output trim" : juce::String(segment.unavailable));
        }
        status.setText(lastError.isNotEmpty() ? lastError : reason.empty()
            ? "MAIN BUS FX  /  same nodes as Routing  /  before Output trim"
            : juce::String(reason), juce::dontSendNotification);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(6);
        auto header = area.removeFromTop(28); add.setBounds(header.removeFromRight(64)); status.setBounds(header);
        viewport.setBounds(area);
        const int width = juce::jmax(0, area.getWidth() - viewport.getScrollBarThickness());
        int total = 0; for (auto& card : cards) total += card->height(width);
        content.setSize(width, juce::jmax(area.getHeight(), total));
        int y = 0; for (auto& card : cards) { card->setBounds(0, y, width, card->height(width) - 6); y += card->height(width); }
    }
    void paint(juce::Graphics& g) override {
        const auto p = themePalette(*this, neon); g.setColour(p.panel); g.fillRect(getLocalBounds());
        g.setColour(p.border); g.drawRect(getLocalBounds());
    }
private:
    static juce::String name(NodeType type) {
        if (isCreativeEffect(type)) return CreativeEffectPanel::effectName(creativeKind(type));
        if (isUtilityEffect(type)) return UtilityEffectPanel::effectName(utilityKind(type));
        if (type == NodeType::chorale) return "Chorale";
        return type == NodeType::equalizer ? "EQ8" : "Waveshaper";
    }
    class Card final : public juce::Component {
        class Handle final : public juce::TextButton {
        public:
            explicit Handle(Card& c) : card(c) { setButtonText("::"); }
            void mouseDown(const juce::MouseEvent& e) override { startY = e.getEventRelativeTo(&card.rack.content).y; }
            void mouseDrag(const juce::MouseEvent& e) override {
                const int y = e.getEventRelativeTo(&card.rack.content).y;
                card.rack.dragTarget = 0;
                for (size_t i = 0; i < card.rack.cards.size(); ++i)
                    if (y >= card.rack.cards[i]->getY()) card.rack.dragTarget = int(i);
            }
            void mouseUp(const juce::MouseEvent& e) override {
                if (std::abs(e.getEventRelativeTo(&card.rack.content).y - startY) < 5) return;
                const auto id = card.model.id; auto* rack = &card.rack;
                const auto index = rack->dragTarget;
                juce::Component::SafePointer<MainBusFxRackPanel> safe(rack);
                juce::MessageManager::callAsync([safe, id, index] {
                    if (safe) safe->apply(mainBusFx::move(safe->owner.getRoutingGraph(), id, size_t(index)));
                });
            }
        private:
            Card& card; int startY = 0;
        };
    public:
        Card(MainBusFxRackPanel& panel, const RoutingNode& node) : model(node), handle(*this), rack(panel) {
            setName(name(model.type) + " node " + juce::String(model.id));
            setComponentID("mainBusFx" + juce::String(model.id));
            addAndMakeVisible(handle); addAndMakeVisible(title); addAndMakeVisible(enabled);
            addAndMakeVisible(remove); addAndMakeVisible(details); addAndMakeVisible(controls);
            title.setText(name(model.type) + "  #" + juce::String(model.id), juce::dontSendNotification);
            enabled.setButtonText("On"); enabled.setClickingTogglesState(true);
            remove.setButtonText("Remove"); details.setButtonText("Details");
            controls.setComponentID("mainBusPrimaryControls");
            details.setComponentID("mainBusFxDetails");
            const int slot = routingFxSlot(model.id);
            if (slot >= 0) {
                enabled.setModulationOwner(rack.owner); enabled.setComponentID(fxParameterId(size_t(slot), "On"));
                enabledLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(rack.owner.parameters, fxParameterId(size_t(slot), "On"), enabled);
                addKnob(fxParameterId(size_t(slot), "Mix"), "Mix", "%");
                if (model.type == NodeType::chorale) addKnob(fxParameterId(size_t(slot), "Motion"), "Motion", "%");
                else if (slot > 0 && isCreativeEffect(model.type)) {
                    const auto primary = [&](CreativeParameter parameter, const char* label) {
                        const auto p = size_t(parameter); const auto& d = creativeParameters[p];
                        addKnob(creativeParameterId(size_t(slot), p), label, d.unit);
                        knobs.back()->slider.setTooltip(juce::String(d.label) + " — " + name(model.type));
                    };
                    using P = CreativeParameter;
                    switch (creativeKind(model.type)) {
                        case CreativeKind::delay:
                            primary(P::delayTime, "Time"); primary(P::delaySync, "Sync");
                            primary(P::delayBeats, "Beats"); primary(P::delayFeedback, "Feedback");
                            primary(P::delayPingPong, "Ping pong"); primary(P::delayDuck, "Ducking"); break;
                        case CreativeKind::reverb:
                            primary(P::reverbPredelay, "Predelay"); primary(P::reverbSize, "Size");
                            primary(P::reverbDecay, "RT60"); primary(P::reverbDamping, "Damping");
                            primary(P::reverbWidth, "Width"); primary(P::reverbDuck, "Ducking"); break;
                        case CreativeKind::compressor:
                            primary(P::compressorThreshold, "Threshold"); primary(P::compressorRatio, "Ratio");
                            primary(P::compressorAttack, "Attack"); primary(P::compressorRelease, "Release");
                            primary(P::compressorMakeup, "Makeup"); break;
                        case CreativeKind::bitcrusher:
                            primary(P::crusherBits, "Bits"); primary(P::crusherRate, "Rate");
                            primary(P::crusherLowpass, "Low pass"); primary(P::crusherDrive, "Drive");
                            primary(P::crusherDither, "Dither"); break;
                        case CreativeKind::granular:
                            primary(P::granularPitch, "Pitch"); primary(P::granularSize, "Length");
                            primary(P::granularDensity, "Density"); primary(P::granularSpread, "Spread");
                            primary(P::granularPosition, "Lookback"); primary(P::granularFreeze, "Freeze"); break;
                    }
                } else if (slot > 0 && isUtilityEffect(model.type)) {
                    for (size_t p = 0; p < utilityParameterCount; ++p) {
                        const auto& d = utilityParameters[p];
                        if (d.kind == utilityKind(model.type)) addKnob(utilityParameterId(size_t(slot), p), d.label, d.unit);
                    }
                } else if (slot > 0 && model.type == NodeType::waveshaper) {
                    addKnob(toneParameterId(size_t(slot), "WaveDrive"), "Drive", "dB");
                    addKnob(toneParameterId(size_t(slot), "WaveOutput"), "Output", "dB");
                    addKnob(toneParameterId(size_t(slot), "WaveAlgorithm"), "Algorithm", "");
                    addKnob(toneParameterId(size_t(slot), "WaveShapeAmount"), "Shape", "%");
                } else if (slot > 0 && model.type == NodeType::equalizer) {
                    addKnob(toneParameterId(size_t(slot), "EqShift"), "Shift", "st");
                    addKnob(toneParameterId(size_t(slot), "EqGain"), "Gain", "dB");
                }
            } else {
                enabled.setEnabled(false); details.setEnabled(false);
                title.setTooltip("Unmapped routing instance: edit its settings in Routing");
            }
            remove.onClick = [this] {
                const auto id = model.id; juce::Component::SafePointer<MainBusFxRackPanel> safe(&rack);
                juce::MessageManager::callAsync([safe, id] { if (safe) safe->apply(mainBusFx::remove(safe->owner.getRoutingGraph(), id)); });
            };
            details.onClick = [this, slot] {
                if (!fullPanel) {
                    if (slot > 0 && isCreativeEffect(model.type)) fullPanel = std::make_unique<CreativeEffectPanel>(rack.owner, size_t(slot), creativeKind(model.type));
                    else if (slot > 0 && isUtilityEffect(model.type)) fullPanel = std::make_unique<UtilityEffectPanel>(rack.owner, size_t(slot), utilityKind(model.type));
                    else if (slot > 0 && model.type != NodeType::chorale) fullPanel = std::make_unique<ToneEffectPanel>(rack.owner, size_t(slot), model.type);
                    if (fullPanel) addChildComponent(*fullPanel);
                }
                expanded = fullPanel && !expanded;
                if (fullPanel) {
                    if (!expanded) cancelGestures(*fullPanel);
                    fullPanel->setVisible(expanded);
                }
                details.setButtonText(expanded ? "Less" : "Details");
                setNeon(neon); rack.resized();
            };
            details.setEnabled(slot > 0 && model.type != NodeType::chorale);
            detail::passiveControl(*this);
        }
        ~Card() override { cancelGestures(*this); }
        int primaryHeight(int width) const {
            const int columns = juce::jmax(1, (width - 16) / knobPitch);
            return ((int(knobs.size()) + columns - 1) / columns) * knobHeight;
        }
        int height(int width) const { return 34 + primaryHeight(width) + 14 + (expanded ? 360 : 0); }
        void resized() override {
            auto header = getLocalBounds().reduced(6).removeFromTop(24);
            handle.setBounds(header.removeFromLeft(28)); remove.setBounds(header.removeFromRight(66));
            details.setBounds(header.removeFromRight(62)); enabled.setBounds(header.removeFromRight(48)); title.setBounds(header);
            const int width = juce::jmax(0, getWidth() - 16);
            const int columns = juce::jmax(1, width / knobPitch);
            controls.setBounds(8, 34, width, primaryHeight(getWidth()));
            for (size_t i = 0; i < knobs.size(); ++i)
                knobs[i]->setBounds(int(i) % columns * knobPitch, int(i) / columns * knobHeight,
                    juce::jmin(knobPitch - 4, width), knobHeight);
            if (fullPanel) fullPanel->setBounds(8, controls.getBottom() + 8, width, 346);
        }
        void setNeon(bool value) {
            neon = value; const auto p = themePalette(*this, neon);
            title.setColour(juce::Label::textColourId, p.text);
            for (auto* button : std::initializer_list<juce::TextButton*>{&handle, &remove, &details, &enabled}) {
                button->setColour(juce::TextButton::buttonColourId, p.inset);
                button->setColour(juce::TextButton::textColourOffId, p.text);
                button->setColour(juce::TextButton::buttonOnColourId, p.accent);
            }
            for (auto& knob : knobs) knob->setNeon(neon);
            if (auto* panel = dynamic_cast<CreativeEffectPanel*>(fullPanel.get())) panel->setNeon(neon);
            if (auto* panel = dynamic_cast<UtilityEffectPanel*>(fullPanel.get())) panel->setNeon(neon);
            if (auto* panel = dynamic_cast<ToneEffectPanel*>(fullPanel.get())) panel->setNeon(neon);
            repaint();
        }
        void paint(juce::Graphics& g) override {
            const auto p = themePalette(*this, neon); g.setColour(p.inset); g.fillRoundedRectangle(getLocalBounds().toFloat(), 4);
            g.setColour(p.border); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(.5f), 4, 1);
        }
        RoutingNode model;
        Handle handle;
        juce::TextButton remove, details;
    private:
        static void cancelGestures(juce::Component& component) {
            for (auto* child : component.getChildren()) cancelGestures(*child);
            if (auto* slider = dynamic_cast<juce::Slider*>(&component)) slider->hideTextBox(true);
            else if (!dynamic_cast<toneui::EqGraph*>(&component) && component.getComponentID() != "utilityVisual") return;
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent release{juce::Desktop::getInstance().getMainMouseSource(),
                {-1, -1}, juce::ModifierKeys{}, 1, 0, 0, 0, 0, &component, &component,
                now, {-1, -1}, now, 0, true};
            component.mouseUp(release);
        }
        void addKnob(const juce::String& id, const juce::String& label, const char* unit) {
            if (!rack.owner.parameters.getParameter(id)) return;
            auto knob = std::make_unique<ParameterKnob>(rack.owner, id, label, unit);
            knob->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 19);
            controls.addAndMakeVisible(*knob); knobs.push_back(std::move(knob));
        }
        MainBusFxRackPanel& rack;
        bool neon = false, expanded = false;
        static constexpr int knobPitch = 88, knobHeight = 100;
        juce::Label title;
        modulationui::Button enabled;
        juce::Component controls;

        std::vector<std::unique_ptr<ParameterKnob>> knobs;
        std::unique_ptr<juce::Component> fullPanel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledLink;
    };
    void showAddMenu() {
        juce::PopupMenu menu;
        for (auto type : types) menu.addItem(int(type) + 1, name(type));
        juce::Component::SafePointer<MainBusFxRackPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(add), [safe](int choice) {
            if (safe && choice) safe->apply(mainBusFx::add(safe->owner.getRoutingGraph(), NodeType(choice - 1)));
        });
    }
    void apply(const mainBusFx::Edit& edit) {
        lastError = juce::String(edit.error);
        if (edit.changed) {
            if (!commitGraph) lastError = "Shared Routing undo bridge is unavailable; routing was not changed.";
            else if (!commitGraph(edit.graph, lastError) && lastError.isEmpty())
                lastError = "Routing rejected the rack edit; routing was not changed.";
        }
        sync();
    }
    void timerCallback() override { sync(); }
    inline static constexpr std::array<NodeType, 13> types{NodeType::chorale, NodeType::waveshaper,
        NodeType::bitcrusher, NodeType::compressor, NodeType::delay, NodeType::reverb, NodeType::granular,
        NodeType::equalizer, NodeType::gain, NodeType::stereo, NodeType::gate, NodeType::xpFilter, NodeType::synthFilter};

    Processor& owner;
    bool neon = false;
    int dragTarget = 0;
    juce::TextButton add;
    juce::Label status;
    juce::Component content;
    juce::Viewport viewport;
    std::vector<std::unique_ptr<Card>> cards;
    juce::String cardSignature, lastError;
};
} // namespace vocalstack
