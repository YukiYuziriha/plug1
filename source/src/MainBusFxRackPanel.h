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
        viewport.setComponentID("mainBusFxRowViewport");
        content.setComponentID("mainBusFxRow");
        viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(false, true);
        status.setFont(juce::Font(juce::FontOptions(11.f)));
        detail::passiveControl(*this); sync(); startTimerHz(12);
    }
    // Voice-sized cards with fixed headers and independently scrolling full inspectors.
    static constexpr int cardWidth = 204, cardGap = 8, preferredHeight = 340;
    // The editor supplies RoutingPanel's validated, one-step graph undo path.
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
            card->handle.setTooltip(segment.editable() ? "Drag horizontally to reorder this FX before Output trim" : juce::String(segment.unavailable));
        }
        status.setText(lastError.isNotEmpty() ? lastError : reason.empty()
            ? "MAIN BUS FX  /  same nodes as Routing  /  before Output trim"
            : juce::String(reason), juce::dontSendNotification);
    }
    void resized() override {
        auto area = getLocalBounds().reduced(6);
        auto header = area.removeFromTop(28); add.setBounds(header.removeFromRight(64)); status.setBounds(header);
        viewport.setBounds(area);
        const int total = juce::jmax(0, int(cards.size()) * (cardWidth + cardGap) - cardGap);
        const int height = juce::jmax(0, area.getHeight() - viewport.getScrollBarThickness());
        content.setSize(juce::jmax(area.getWidth(), total), height);
        int x = 0;
        for (auto& card : cards) { card->setBounds(x, 0, cardWidth, height); x += cardWidth + cardGap; }
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
            explicit Handle(Card& c) : card(c) { setButtonText("::"); setComponentID("mainBusFxDrag"); }
            void mouseDown(const juce::MouseEvent& e) override {
                startX = e.getEventRelativeTo(&card.rack.content).x;
                card.rack.dragTarget = int(std::find_if(card.rack.cards.begin(), card.rack.cards.end(),
                    [this](const auto& c) { return c.get() == &card; }) - card.rack.cards.begin());
            }
            void mouseDrag(const juce::MouseEvent& e) override {
                auto& rack = card.rack;
                const auto point = e.getEventRelativeTo(&rack.viewport).position;
                rack.viewport.autoScroll(int(point.x), int(point.y), 24, 16);
                const int x = e.getEventRelativeTo(&rack.content).x;
                rack.dragTarget = juce::jlimit(0, int(rack.cards.size()) - 1, x / (cardWidth + cardGap));
            }
            void mouseUp(const juce::MouseEvent& e) override {
                if (!isEnabled() || std::abs(e.getEventRelativeTo(&card.rack.content).x - startX) < 5) return;
                const auto id = card.model.id; auto* rack = &card.rack;
                const auto index = rack->dragTarget;
                juce::Component::SafePointer<MainBusFxRackPanel> safe(rack);
                juce::MessageManager::callAsync([safe, id, index] {
                    if (safe) safe->apply(mainBusFx::move(safe->owner.getRoutingGraph(), id, size_t(index)));
                });
            }
        private:
            Card& card; int startX = 0;
        };
    public:
        Card(MainBusFxRackPanel& panel, const RoutingNode& node) : model(node), handle(*this), rack(panel) {
            setName(name(model.type) + " node " + juce::String(model.id));
            setComponentID("mainBusFx" + juce::String(model.id));
            addAndMakeVisible(handle); addAndMakeVisible(title); addAndMakeVisible(enabled);
            addAndMakeVisible(remove);
            remove.setComponentID("mainBusFxRemove");
            title.setText(name(model.type) + "  #" + juce::String(model.id), juce::dontSendNotification);
            enabled.setButtonText("On"); enabled.setClickingTogglesState(true);
            remove.setButtonText("x");
            const int slot = routingFxSlot(model.id);
            if (slot >= 0) {
                enabled.setModulationOwner(rack.owner); enabled.setComponentID(fxParameterId(size_t(slot), "On"));
                enabledLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(rack.owner.parameters, fxParameterId(size_t(slot), "On"), enabled);
                if (model.type == NodeType::chorale) {
                    controls.setComponentID("mainBusChoraleControls");
                    bodyViewport.setComponentID("mainBusFxBodyViewport");
                    addAndMakeVisible(bodyViewport);
                    bodyViewport.setViewedComponent(&controls, false);
                    bodyViewport.setScrollBarsShown(true, false);
                    addKnob(fxParameterId(size_t(slot), "Mix"), "Mix", "%");
                    addKnob(fxParameterId(size_t(slot), "Motion"), "Motion", "%");
                } else if (slot > 0) {
                    if (isCreativeEffect(model.type)) fullPanel = std::make_unique<CreativeEffectPanel>(rack.owner, size_t(slot), creativeKind(model.type));
                    else if (isUtilityEffect(model.type)) fullPanel = std::make_unique<UtilityEffectPanel>(rack.owner, size_t(slot), utilityKind(model.type));
                    else {
                        fullPanel = std::make_unique<ToneEffectPanel>(rack.owner, size_t(slot), model.type);
                        bodyViewport.setComponentID("mainBusFxBodyViewport");
                        addAndMakeVisible(bodyViewport);
                        bodyViewport.setViewedComponent(&controls, false);
                        bodyViewport.setScrollBarsShown(true, false);
                        if (model.type != NodeType::waveshaper)
                            addKnob(fxParameterId(size_t(slot), "Mix"), "Mix", "%");
                    }
                    fullPanel->setComponentID("mainBusFxFullPanel");
                    if (dynamic_cast<ToneEffectPanel*>(fullPanel.get())) controls.addAndMakeVisible(*fullPanel);
                    else addAndMakeVisible(*fullPanel);
                }
            } else {
                enabled.setEnabled(false);
                title.setTooltip("Unmapped routing instance: edit its settings in Routing");
            }
            remove.onClick = [this] {
                const auto id = model.id; juce::Component::SafePointer<MainBusFxRackPanel> safe(&rack);
                juce::MessageManager::callAsync([safe, id] { if (safe) safe->apply(mainBusFx::remove(safe->owner.getRoutingGraph(), id)); });
            };
            detail::passiveControl(*this);
        }
        ~Card() override { cancelGestures(*this); }
        void resized() override {
            auto area = getLocalBounds().reduced(4);
            auto header = area.removeFromTop(24);
            handle.setBounds(header.removeFromLeft(22));
            remove.setBounds(header.removeFromRight(24));
            enabled.setBounds(header.removeFromRight(36));
            title.setBounds(header);
            area.removeFromTop(4);
            if (fullPanel && !dynamic_cast<ToneEffectPanel*>(fullPanel.get())) fullPanel->setBounds(area);
            else {
                bodyViewport.setBounds(area);
                const int width = juce::jmax(1, area.getWidth() - bodyViewport.getScrollBarThickness());
                const int knobAreaHeight = int(knobs.size()) * knobHeight;
                auto* tonePanel = dynamic_cast<ToneEffectPanel*>(fullPanel.get());
                const int panelHeight = tonePanel ? tonePanel->preferredContentHeight(width) : 0;
                controls.setSize(width, juce::jmax(area.getHeight(), knobAreaHeight + panelHeight));
                if (fullPanel) fullPanel->setBounds(0, knobAreaHeight, width, panelHeight);
                for (size_t i = 0; i < knobs.size(); ++i)
                    knobs[i]->setBounds(4, int(i) * knobHeight, width - 8, knobHeight);
            }
        }
        void setNeon(bool value) {
            neon = value; const auto p = themePalette(*this, neon);
            title.setColour(juce::Label::textColourId, p.text);
            for (auto* button : std::initializer_list<juce::TextButton*>{&handle, &remove, &enabled}) {
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
        juce::TextButton remove;
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
        bool neon = false;
        static constexpr int knobHeight = 100;
        juce::Label title;
        modulationui::Button enabled;
        juce::Component controls;
        PassiveViewport bodyViewport;

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
    PassiveViewport viewport;
    std::vector<std::unique_ptr<Card>> cards;
    juce::String cardSignature, lastError;
};
} // namespace vocalstack
