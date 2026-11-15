#include "VisualTheme.h"
#pragma once

#include "MainBusFxRack.h"
#include "ToneEffectPanel.h"
#include "CreativeEffectPanel.h"
#include "UtilityEffectPanel.h"
#include "SpectralMorphVisual.h"

namespace vocalstack {

// Host-bound tabs over the processor's graph. No private graph/FX-slot pool.
class MainBusFxRackPanel final : public juce::Component, private juce::Timer {
public:
    explicit MainBusFxRackPanel(Processor& processor) : owner(processor) {
        setName("Main Bus FX rack"); setComponentID("mainBusFxRack");
        setLookAndFeel(&panelStyle);
        add.setButtonText("+ FX"); add.setComponentID("mainBusAddFx");
        add.onClick = [this] { showAddMenu(); };
        addAndMakeVisible(add); addAndMakeVisible(status); addAndMakeVisible(viewport);
        viewport.setComponentID("mainBusFxRowViewport");
        content.setComponentID("mainBusFxRow");
        viewport.setViewedComponent(&content, false); viewport.setScrollBarsShown(false, true);
        status.setFont(juce::Font(pixelFontOptions(11.f)));
        detail::passiveControl(*this); sync(); startTimerHz(12);
    }
    ~MainBusFxRackPanel() override {
        stopTimer(); viewport.setViewedComponent(nullptr, false); setLookAndFeel(nullptr);
    }
    static constexpr int tabGap = 2, preferredHeight = 388;
    // The editor supplies RoutingPanel's validated, one-step graph undo path.
    std::function<bool(const Graph&, juce::String&)> commitGraph;
    std::function<void()> onOpenRouting;
    void setNeon(bool value) {
        neon = value;
        const auto p = themePalette(*this, neon);
        panelStyle.setPalette(p);
        status.setColour(juce::Label::textColourId, p.secondary);
        add.setColour(juce::TextButton::buttonColourId, p.inset);
        add.setColour(juce::TextButton::textColourOffId, p.text);
        for (auto& card : cards) card->setNeon(value);
        resized(); repaint();
    }
    void sync() {
        const auto graph = owner.getRoutingGraph(); const auto segment = mainBusFx::discover(graph);
        juce::String signature;
        for (auto id : segment.effects) if (const auto* node = mainBusFx::find(graph, id))
            signature += juce::String(id) + ":" + juce::String(int(node->type)) + ";";
        if (signature != cardSignature) {
            const auto oldSelection = selectedId;
            std::vector<uint32_t> previousIds;
            for (auto& card : cards) previousIds.push_back(card->model.id);
            size_t oldIndex = 0;
            for (size_t i = 0; i < cards.size(); ++i) if (cards[i]->model.id == selectedId) oldIndex = i;
            auto previous = std::move(cards); cards.clear();
            for (auto id : segment.effects) if (const auto* node = mainBusFx::find(graph, id)) {
                std::unique_ptr<Card> card;
                for (auto& old : previous) if (old && old->model.id == id && old->model.type == node->type) { card = std::move(old); break; }
                if (!card) card = std::make_unique<Card>(*this, *node);
                content.addAndMakeVisible(card->tab);
                addChildComponent(*card);
                cards.push_back(std::move(card));
            }
            // Keep selection by node identity, including on external reorder/Undo.
            selectedId = 0;
            for (auto& card : cards) if (card->model.id == oldSelection) selectedId = oldSelection;
            // An insertion (including one made in Routing) opens the new instance.
            // Initial discovery opens the first tab rather than jumping to the far end.
            if (previousIds.empty()) {
                if (!cards.empty()) selectedId = cards.front()->model.id;
            } else for (auto& card : cards)
                if (std::find(previousIds.begin(), previousIds.end(), card->model.id) == previousIds.end())
                    selectedId = card->model.id;
            if (selectedId == 0 && !cards.empty()) selectedId = cards[juce::jmin(oldIndex, cards.size() - 1)]->model.id;
            for (auto& old : previous) if (old) old->cancelEditing();
            cardSignature = signature; updateSelection(); resized(); setNeon(neon);
        }
        const auto capacity = mainBusFx::capacityProblem(graph);
        const auto reason = !segment.editable() ? segment.unavailable : !commitGraph
            ? std::string("Shared Routing Undo bridge unavailable; parameter editing remains active.") : capacity;
        add.setEnabled(segment.editable() && capacity.empty() && bool(commitGraph));
        add.setTooltip(juce::String(reason));
        for (auto& card : cards) {
            card->remove.setEnabled(segment.editable() && bool(commitGraph));
            card->canReorder = segment.editable() && bool(commitGraph);
            card->syncTabState();
            card->remove.setTooltip(segment.editable() ? "Remove this routing node (Undo in Routing)" : juce::String(segment.unavailable));
            card->handle.setTooltip(card->canReorder ? "Select FX; drag horizontally to reorder (Undo in Routing)" : "Select FX / " + juce::String(reason));
        }
        status.setText(lastError.isNotEmpty()
                    ? (owner.routingRestartPending() ? "RESTART PENDING / Saved topology is not running. " : juce::String()) + lastError
                    : owner.routingRestartPending()
            ? "RESTART PENDING / Saved topology is not running. Host deactivate/reactivate required; transport stop is not enough."
            : reason.empty() ? "MAIN BUS FX  /  same nodes as Routing  /  before Output trim"
            : juce::String(reason), juce::dontSendNotification);
        status.setTooltip(status.getText());
    }
    void resized() override {
        auto area = getLocalBounds().reduced(6);
        status.setBounds(area.removeFromBottom(18));
        auto header = area.removeFromTop(42);
        add.setBounds(header.removeFromRight(64).reduced(2, 4));
        header.removeFromRight(4);
        viewport.setBounds(header);
        int total = 0;
                for (auto& card : cards) total += card->preferredTabWidth() + tabGap;
                total = juce::jmax(0, total - tabGap);
        const int height = juce::jmax(0, header.getHeight() - viewport.getScrollBarThickness());
        content.setSize(juce::jmax(header.getWidth(), total), height);
        int x = 0;
        for (auto& card : cards) {
            const int width = card->preferredTabWidth();
            card->tab.setBounds(x, 0, width, height);
            card->setBounds(area);
            card->resized(); // Tab width can change without changing the inspector body bounds.
            x += width + tabGap;
        }
        revealSelection();
    }
    void paint(juce::Graphics& g) override {
        const auto p = themePalette(*this, neon);
                background.paint(g, getLocalBounds(), p.panel, p.accent, fluidAnimationSeconds(*this));
        g.setColour(p.border); g.drawRect(getLocalBounds());
    }
private:
    static juce::String name(NodeType type) {
        if (type == NodeType::spectralMorph) return "Spectral Morph";
        if (isCreativeEffect(type)) return CreativeEffectPanel::effectName(creativeKind(type));
        if (isUtilityEffect(type)) return UtilityEffectPanel::effectName(utilityKind(type));
        if (type == NodeType::chorale) return "Chorale";
        return type == NodeType::equalizer ? "EQ8" : "Waveshaper";
    }
    class Card final : public juce::Component {
        class Handle final : public juce::TextButton {
        public:
            explicit Handle(Card& c) : card(c) {
                setComponentID("mainBusFxDrag");
                setTriggeredOnMouseDown(false);
                onClick = [this] { card.rack.select(card.model.id); };
            }
            void paintButton(juce::Graphics& g, bool hover, bool down) override {
                auto& style = getLookAndFeel();
                style.drawButtonBackground(g, *this, findColour(juce::TextButton::buttonColourId), hover, down);
                g.setFont(style.getTextButtonFont(*this, getHeight()));
                g.setColour(findColour(getToggleState() ? juce::TextButton::textColourOnId
                                                       : juce::TextButton::textColourOffId));
                g.drawText(getButtonText(), getLocalBounds().reduced(4, 0), juce::Justification::centredLeft);
            }
            void mouseDown(const juce::MouseEvent& e) override {
                if (!e.mods.isLeftButtonDown()) return;
                card.rack.select(card.model.id);
                startX = e.getEventRelativeTo(&card.rack.content).x;
                started = true;
                signature = card.rack.cardSignature;
                order = mainBusFx::discover(card.rack.owner.getRoutingGraph()).effects;
                card.rack.dragTarget = int(std::find_if(card.rack.cards.begin(), card.rack.cards.end(),
                    [this](const auto& c) { return c.get() == &card; }) - card.rack.cards.begin());
            }
            void mouseDrag(const juce::MouseEvent& e) override {
                auto& rack = card.rack;
                if (!started || !card.canReorder || signature != rack.cardSignature) return;
                const auto point = e.getEventRelativeTo(&rack.viewport).position;
                rack.viewport.autoScroll(int(point.x), int(point.y), 24, 16);
                const int x = e.getEventRelativeTo(&rack.content).x;
                rack.dragTarget = int(rack.cards.size()) - 1;
                                for (size_t i = 0; i < rack.cards.size(); ++i)
                                    if (x < rack.cards[i]->tab.getRight() + tabGap / 2) {
                                        rack.dragTarget = int(i); break;
                                    }
            }
            void mouseUp(const juce::MouseEvent& e) override {
                const bool valid = started && card.canReorder && signature == card.rack.cardSignature
                    && order == mainBusFx::discover(card.rack.owner.getRoutingGraph()).effects;
                started = false;
                if (!valid || std::abs(e.getEventRelativeTo(&card.rack.content).x - startX) < 5) return;
                const auto id = card.model.id; auto* rack = &card.rack;
                const auto index = rack->dragTarget;
                juce::Component::SafePointer<MainBusFxRackPanel> safe(rack);
                const auto expectedOrder = order;
                juce::MessageManager::callAsync([safe, id, index, expectedOrder] {
                    if (safe && mainBusFx::discover(safe->owner.getRoutingGraph()).effects == expectedOrder)
                        safe->apply(mainBusFx::move(safe->owner.getRoutingGraph(), id, size_t(index)));
                });
            }
        private:
            Card& card; int startX = 0;
            bool started = false;
            juce::String signature;
            std::vector<uint32_t> order;
        };
    public:
        Card(MainBusFxRackPanel& panel, const RoutingNode& node) : model(node), handle(*this), rack(panel) {
            setName(name(model.type) + " node " + juce::String(model.id));
            setComponentID("mainBusFx" + juce::String(model.id));
            tab.setComponentID("mainBusFxTab" + juce::String(model.id));
            tab.setName(getName());
            tab.addAndMakeVisible(handle); tab.addAndMakeVisible(enabled);
            tab.addAndMakeVisible(remove);
            remove.setComponentID("mainBusFxRemove");
            handle.setButtonText(name(model.type) + " #" + juce::String(model.id));
            enabled.setButtonText("On"); enabled.setClickingTogglesState(true);
            enabled.setTooltip("Enable/bypass this instance (does not select the tab)");
            remove.setButtonText("x");
            const int slot = routingFxSlot(model.id);
            if (slot >= 0) {
                enabled.setModulationOwner(rack.owner); enabled.setComponentID(fxParameterId(size_t(slot), "On"));
                enabledValue = rack.owner.parameters.getRawParameterValue(enabled.getComponentID());
                enabledLink = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(rack.owner.parameters, fxParameterId(size_t(slot), "On"), enabled);
                if (model.type == NodeType::chorale) {
                    controls.setComponentID("mainBusChoraleControls");
                    bodyViewport.setComponentID("mainBusFxBodyViewport");
                    addAndMakeVisible(bodyViewport);
                    bodyViewport.setViewedComponent(&controls, false);
                    bodyViewport.setScrollBarsShown(true, false);
                    addKnob(fxParameterId(size_t(slot), "Mix"), "Mix", "%", true);
                    addKnob(fxParameterId(size_t(slot), "Motion"), "Motion", "%", true);
                } else if (slot > 0) {
                    if (model.type == NodeType::spectralMorph) {
                        auto spectral = std::make_unique<SpectralMorphPanel>(rack.owner, size_t(slot));
                        spectral->setInspectorLayout(false);
                        spectral->onOpenRouting = [this] { if (rack.onOpenRouting) rack.onOpenRouting(); };
                        fullPanel = std::move(spectral);
                    }
                    else if (isCreativeEffect(model.type)) fullPanel = std::make_unique<CreativeEffectPanel>(rack.owner, size_t(slot), creativeKind(model.type));
                    else if (isUtilityEffect(model.type)) {
                        auto panel = std::make_unique<UtilityEffectPanel>(rack.owner, size_t(slot), utilityKind(model.type));
                        if (model.type == NodeType::stereo) panel->bindMonoAuditionParameter(stereoMonoParameterId(size_t(slot)));
                        fullPanel = std::move(panel);
                    }
                    else {
                        fullPanel = std::make_unique<ToneEffectPanel>(rack.owner, size_t(slot), model.type);
                        bodyViewport.setComponentID("mainBusFxBodyViewport");
                        addAndMakeVisible(bodyViewport);
                        bodyViewport.setViewedComponent(&controls, false);
                        bodyViewport.setScrollBarsShown(true, false);
                        if (model.type != NodeType::waveshaper)
                            addKnob(fxParameterId(size_t(slot), "Mix"), "Mix", "%", true);
                    }
                    fullPanel->setComponentID("mainBusFxFullPanel");
                    if (dynamic_cast<ToneEffectPanel*>(fullPanel.get())) controls.addAndMakeVisible(*fullPanel);
                    else addAndMakeVisible(*fullPanel);
                }
            } else {
                enabled.setEnabled(false);
                enabled.setTooltip("Unmapped routing instance: edit its settings in Routing");
            }
            remove.onClick = [this] {
                const auto id = model.id; juce::Component::SafePointer<MainBusFxRackPanel> safe(&rack);
                juce::MessageManager::callAsync([safe, id] { if (safe) safe->apply(mainBusFx::remove(safe->owner.getRoutingGraph(), id)); });
            };
            detail::passiveControl(*this);
            detail::passiveControl(tab);
        }
        ~Card() override { cancelEditing(); bodyViewport.setViewedComponent(nullptr, false); }
                int preferredTabWidth() {
                    const auto font = handle.getLookAndFeel().getTextButtonFont(handle, 28);
                    return int(std::ceil(juce::GlyphArrangement::getStringWidth(font, handle.getButtonText()))) + 62;
                }
        void resized() override {
            auto area = getLocalBounds().reduced(4);
            auto header = tab.getLocalBounds().reduced(1);
            remove.setBounds(header.removeFromRight(22));
            enabled.setBounds(header.removeFromRight(30));
            handle.setBounds(header);
            if (fullPanel && !dynamic_cast<ToneEffectPanel*>(fullPanel.get())) fullPanel->setBounds(area);
            else {
                bodyViewport.setBounds(area);
                const int width = juce::jmax(1, area.getWidth() - bodyViewport.getScrollBarThickness());
                if (auto* tonePanel = dynamic_cast<ToneEffectPanel*>(fullPanel.get())) {
                    const int panelHeight = tonePanel->preferredContentHeight(width);
                    const int extraHeight = knobs.empty() ? 0 : mixHeight + controlGap;
                    controls.setSize(width, juce::jmax(area.getHeight(), panelHeight + extraHeight));
                    // Keep Tone's responsive graph-left/controls-right layout at the top.
                    // The rack-owned Mix is a compact footer with in-track text.
                    fullPanel->setBounds(0, 0, width, panelHeight);
                    if (!knobs.empty()) {
                        const int mixWidth = juce::jmin(240, juce::jmax(1, width - 16));
                        knobs.front()->setBounds(juce::jmax(0, width - mixWidth - 8), panelHeight + controlGap,
                                                 mixWidth, mixHeight);
                    }
                } else if (model.type == NodeType::chorale && knobs.size() == 2) {
                    const int barWidth = juce::jmax(1, width - 16);
                    controls.setSize(width, juce::jmax(area.getHeight(), 16 + 2 * mixHeight + controlGap));
                    knobs[1]->setBounds(8, 8, barWidth, mixHeight);
                    knobs[0]->setBounds(8, 8 + mixHeight + controlGap, barWidth, mixHeight);
                } else controls.setSize(width, area.getHeight());
            }
        }
        void setNeon(bool value) {
            neon = value; const auto p = themePalette(*this, neon);
            for (auto* button : std::initializer_list<juce::TextButton*>{&handle, &remove, &enabled}) {
                button->setColour(juce::TextButton::buttonColourId, p.inset);
                button->setColour(juce::TextButton::textColourOffId, p.text);
                button->setColour(juce::TextButton::buttonOnColourId, p.accent);
                button->setColour(juce::TextButton::textColourOnId, p.accent.contrasting());
            }
            syncTabState();
            for (auto& knob : knobs) knob->setNeon(neon);
            if (auto* spectral = dynamic_cast<SpectralMorphPanel*>(fullPanel.get())) spectral->setNeon(value);
            if (auto* panel = dynamic_cast<CreativeEffectPanel*>(fullPanel.get())) panel->setNeon(neon);
            if (auto* panel = dynamic_cast<UtilityEffectPanel*>(fullPanel.get())) panel->setNeon(neon);
            if (auto* panel = dynamic_cast<ToneEffectPanel*>(fullPanel.get())) panel->setNeon(neon);
            repaint();
        }
        void paint(juce::Graphics& g) override {
            const auto p = themePalette(*this, neon);
                        background.paint(g, getLocalBounds(), p.inset, p.accent, fluidAnimationSeconds(*this));
            g.setColour(p.border); g.drawRect(getLocalBounds().toFloat().reduced(.5f), 1);
        }
        void syncTabState() {
            const auto p = themePalette(*this, neon);
            const bool active = enabled.isEnabled() && enabledValue && enabledValue->load() > .5f;
            const auto selectedText = p.accent.contrasting();
            handle.setColour(juce::TextButton::textColourOffId,
                             active ? p.text : p.text.interpolatedWith(p.inset, .45f));
            handle.setColour(juce::TextButton::textColourOnId,
                             active ? selectedText : selectedText.interpolatedWith(p.accent, .25f));
        }
        void cancelEditing() { cancelGestures(*this); }
        void showSelected(bool selected) {
            if (!selected && isVisible()) cancelEditing();
            setVisible(selected);
            handle.setToggleState(selected, juce::dontSendNotification);
            tab.repaint();
        }
        RoutingNode model;
        bool canReorder = false;
        juce::Component tab;
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
        void addKnob(const juce::String& id, const juce::String& label, const char* unit, bool linear = false) {
            if (!rack.owner.parameters.getParameter(id)) return;
            auto knob = std::make_unique<ParameterKnob>(rack.owner, id, label, unit);
            knob->setLinear(linear);
            controls.addAndMakeVisible(*knob); knobs.push_back(std::move(knob));
        }
        MainBusFxRackPanel& rack;
        FluidBackground background;
        bool neon = false;
        // Mix overlays its name and editable value inside the textured track.
        static constexpr int mixHeight = 44, controlGap = 2;
        modulationui::Button enabled;
        std::atomic<float>* enabledValue = nullptr;
        juce::Component controls;
        PassiveViewport bodyViewport;

        std::vector<std::unique_ptr<ParameterKnob>> knobs;
        std::unique_ptr<juce::Component> fullPanel;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledLink;
    };
    void select(uint32_t id) {
        if (selectedId != id) {
            selectedId = id;
            updateSelection();
        }
        revealSelection();
    }
    void updateSelection() {
        for (auto& card : cards) card->showSelected(card->model.id == selectedId);
    }
    void revealSelection() {
        for (auto& card : cards) if (card->model.id == selectedId) {
            const auto bounds = card->tab.getBounds();
            const int left = viewport.getViewPositionX(), width = viewport.getViewWidth();
            if (bounds.getX() < left) viewport.setViewPosition(bounds.getX(), 0);
            else if (bounds.getRight() > left + width)
                viewport.setViewPosition(juce::jmax(0, bounds.getRight() - width), 0);
        }
    }
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
    inline static constexpr std::array<NodeType, 16> types{NodeType::chorale, NodeType::waveshaper,
        NodeType::bitcrusher, NodeType::compressor, NodeType::delay, NodeType::reverb, NodeType::granular,
        NodeType::equalizer, NodeType::gain, NodeType::stereo, NodeType::gate, NodeType::xpFilter, NodeType::synthFilter, NodeType::convolution, NodeType::saturator, NodeType::spectralMorph};

    modulationui::PanelStyle panelStyle;
    FluidBackground background;
    Processor& owner;
    bool neon = false;
    uint32_t selectedId = 0;
    int dragTarget = 0;
    juce::TextButton add;
    juce::Label status;
    juce::Component content;
    PassiveViewport viewport;
    std::vector<std::unique_ptr<Card>> cards;
    juce::String cardSignature, lastError;
};
} // namespace vocalstack
