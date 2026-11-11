#pragma once

#include "ToneEffectPanel.h"
#include "ModulationPanel.h"
#include <map>

namespace vocalstack {

// Header-only presentation. The processor owns the accepted graph and publication.
class RoutingPanel final : public juce::Component, private juce::Timer {
    struct EditState {
        Graph graph;
        juce::ValueTree layout;
        std::array<std::array<float, 3>, 16> fxValues{};
        std::array<std::array<float, toneParametersPerSlot>, 15> toneValues{};
                std::array<std::array<float, 2>, 15> waveValues{};
                        std::array<float, 15> qualityValues{}, levelMatchValues{};
        unsigned fxOnUndoMask = 0;
    };
    class Canvas final : public juce::Component {
    public:
        explicit Canvas(RoutingPanel& p) : panel(p) { detail::passiveControl(*this); }
        void paint(juce::Graphics& g) override { panel.paintCanvas(g); }
        void mouseDown(const juce::MouseEvent& e) override { panel.canvasDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override { panel.canvasDrag(e); }
        void mouseUp(const juce::MouseEvent& e) override { panel.canvasUp(e); }
        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
            if (wheel.deltaY != 0.0f)
                panel.zoomAt(e.position, wheel.deltaY > 0 ? 1.12f : 1.0f / 1.12f);
        }
    private:
        RoutingPanel& panel;
    } canvas;
    class InspectorDivider final : public juce::Component {
    public:
        explicit InspectorDivider(RoutingPanel& p) : panel(p) {
            setName("Resize node inspector");
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            detail::passiveControl(*this);
        }
        void mouseDown(const juce::MouseEvent&) override {
            startWidth = panel.inspector.getWidth();
            panel.resizingInspector = true;
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            const int width = panel.constrainedInspectorWidth(startWidth - e.getDistanceFromDragStartX());
            if (width == panel.inspector.getWidth()) return;
            panel.desiredInspectorWidth = juce::jlimit(320, 480, width);
            panel.resized();
            panel.repaint();
        }
        void mouseUp(const juce::MouseEvent&) override {
            panel.resizingInspector = false;
            panel.saveInspectorWidth();
        }
    private:
        RoutingPanel& panel;
        int startWidth = 320;
    };
    class ModulationDivider final : public juce::Component {
    public:
        explicit ModulationDivider(RoutingPanel& p) : panel(p) {
            setName("Resize modulation panel");
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            detail::passiveControl(*this);
        }
        void mouseDown(const juce::MouseEvent&) override {
            startHeight = panel.modulationPanel.getHeight();
            panel.resizingModulation = true;
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            const int height = panel.constrainedModulationHeight(startHeight - e.getDistanceFromDragStartY());
            if (height == panel.modulationPanel.getHeight()) return;
            panel.desiredModulationHeight = juce::jlimit(160, 480, height);
            panel.resized();
            panel.repaint();
        }
        void mouseUp(const juce::MouseEvent&) override {
            panel.resizingModulation = false;
            panel.saveModulationHeight();
        }
    private:
        RoutingPanel& panel;
        int startHeight = ModulationPanel::preferredHeight;
    };
    class ModulatorTile final : public juce::TextButton {
    public:
        explicit ModulatorTile(const Modulator& m) : TextButton(modulatorName(m)), model(m) {}
        Modulator model;
        float phase = 0;
        std::vector<float> history;
        void paintButton(juce::Graphics& g, bool hovered, bool down) override {
            auto bounds = getLocalBounds().toFloat().reduced(1);
            const auto accent = findColour(juce::TextButton::buttonOnColourId);
            g.setColour(findColour(juce::TextButton::buttonColourId).brighter(hovered || down ? .15f : 0));
            g.fillRoundedRectangle(bounds, 5);
            g.setColour(getToggleState() ? accent : accent.withAlpha(.25f));
            g.drawRoundedRectangle(bounds, 5, getToggleState() ? 2.0f : 1.0f);
            g.setColour(findColour(juce::TextButton::textColourOffId));
            g.setFont(juce::Font(juce::FontOptions(12.0f)));
            g.drawText(getButtonText(), bounds.removeFromTop(22), juce::Justification::centred);
            const auto plot = bounds.reduced(7, 6);
            g.setColour(accent.withAlpha(.2f));
            g.drawHorizontalLine(int(plot.getCentreY()), plot.getX(), plot.getRight());
            juce::Path path;
            const bool random = model.type == ModulatorType::random;
            const size_t count = random ? history.size() : 65;
            for (size_t i = 0; i < count; ++i) {
                const float t = float(i) / float(random ? 63 : 64);
                float value = random ? history[i] : 0;
                if (!random) {
                    const float pivot = .01f + .98f * model.shape;
                    const float x = t < pivot ? .5f * t / pivot : .5f + .5f * (t - pivot) / (1 - pivot);
                    switch (model.waveform) {
                        case ModulationWaveform::sine: value = std::sin(juce::MathConstants<float>::twoPi * x); break;
                        case ModulationWaveform::triangle: value = 1 - 4 * std::abs(x - .5f); break;
                        case ModulationWaveform::saw: value = 2 * x - 1; break;
                        case ModulationWaveform::square: value = x < .5f ? 1.0f : -1.0f; break;
                    }
                    if (!model.bipolar) value = .5f * (value + 1);
                    value *= model.depth;
                }
                const float x = plot.getX() + t * plot.getWidth();
                const float y = plot.getCentreY() - value * plot.getHeight() * .45f;
                if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
            }
            g.setColour(accent); g.strokePath(path, juce::PathStrokeType(1.5f));
            if (!random) {
                g.setColour(accent.withAlpha(.6f));
                const float x = plot.getX() + juce::jlimit(0.0f, 1.0f, phase) * plot.getWidth();
                g.drawLine(x, plot.getY(), x, plot.getBottom(), 1);
            }
        }
    };
public:
    explicit RoutingPanel(Processor& processor) : canvas(*this), owner(processor), inspectorDivider(*this), modulationPanel(processor), modulationDivider(*this) {
        setName("Audio routing canvas and inspector");
        addAndMakeVisible(canvas);
        addAndMakeVisible(inspector);
        addAndMakeVisible(inspectorDivider);
        addChildComponent(modulationPanel);
        addChildComponent(modulationDivider);
        addAndMakeVisible(status);
                addAndMakeVisible(modulatorViewport); modulatorViewport.setViewedComponent(&modulatorStrip, false);
                modulatorViewport.setScrollBarsShown(true, false);
                addAndMakeVisible(addModulator); addModulator.setButtonText("+ Modulator");
                addModulator.onClick = [this] { showModulatorMenu(); };
                addChildComponent(assignmentBanner);
                assignmentBanner.onClick = [this] { owner.endModulationAssignment(); refreshAssignmentBanner(); };
                modulationPanel.onDeleted = [this] {
                            selectedModulator = 0;
                            if (!modulationui::selectedModulator(owner)) modulationui::selectModulator(owner, 0);
                            rebuildModulatorStrip(); updateModulationPanel(); saveLayout();
                        };
                modulationPanel.onReveal = [this](const juce::String& id) { revealTarget(id); };
        status.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        status.setFont(juce::Font(juce::FontOptions(12.0f)));
        inspectorTitle.setTooltip("Double-click a machine title to rename it. Stable voice/parameter identities do not change.");
        inspectorTitle.onTextChange = [this] {
            const auto* selected = node(selectedNode);
            if (!selected) return;
            const auto name = inspectorTitle.getText().trim().substring(0, 48);
            remember();
            if (name.isEmpty()) names.erase(selectedNode); else names[selectedNode] = name;
            inspectorTitle.setText(title(*selected), juce::dontSendNotification);
            saveLayout(); updateButtons(); canvas.repaint();
        };
        for (auto* b : {&add, &undo, &redo, &remove, &bypass, &fit, &zoomOut, &zoomIn}) addAndMakeVisible(*b);
        add.setButtonText("+ Node"); undo.setButtonText("Undo"); redo.setButtonText("Redo");
        remove.setButtonText("Delete node"); bypass.setButtonText("Bypass FX");
        fit.setButtonText("Fit"); zoomOut.setButtonText("-"); zoomIn.setButtonText("+");
        add.onClick = [this] { showAddMenu(); };
        undo.onClick = [this] { stepHistory(undoStates, redoStates); };
        redo.onClick = [this] { stepHistory(redoStates, undoStates); };

        remove.onClick = [this] { deleteSelection(); };
        disconnect.setButtonText("Disconnect");
        disconnect.setName("Disconnect selected audio connection");
        disconnect.onClick = [this] { disconnectSelection(); };
        bypass.onClick = [this] { toggleBypass(); };
        fit.onClick = [this] { fitGraph(); };
        zoomOut.onClick = [this] { zoomAt(canvas.getLocalBounds().getCentre().toFloat(), 1.0f / 1.2f); };
        zoomIn.onClick = [this] { zoomAt(canvas.getLocalBounds().getCentre().toFloat(), 1.2f); };
        canvas.setName("Routing node canvas");
        add.setTooltip("Add an independent effect, a dry/wet mixer, or a missing source");
        remove.setTooltip("Delete the selected node and its cables. Output is retained");
        bypass.setTooltip("Bypass the selected FX through its host parameter. A bypassed splitter passes full-band audio on port 0 only.");
        graph = owner.getRoutingGraph();
        loadLayout(owner.getRoutingLayout());
        ensurePositions();
        rebuildModulatorStrip();
        showStatus();
        rebuildInspector();
        lastSignature = signature(graph);
        lastPortSignature = portSignature();
        lastLayoutSignature = owner.getRoutingLayout().toXmlString();
        setNeon(owner.isFlash());
        detail::passiveControl(*this);
        refreshAssignmentBanner();
        // setSize invokes resized(); wait until the viewports and saved layout are ready.
        setSize(1280, 800);
        startTimerHz(15);
    }
    ~RoutingPanel() override { toneViewport.setViewedComponent(nullptr, false); modulatorViewport.setViewedComponent(nullptr, false); nodeValue.setLookAndFeel(nullptr); }
    // Call only after the embedded canvas has its final bounds.
    void fitIfUnpositioned() {
        const auto savedLayout = owner.getRoutingLayout();
        for (const auto& child : savedLayout)
            if (child.hasType("NODE")) return;
        fitGraph();
    }
    void setNeon(bool value) {
        neon = value;
        for (auto* b : std::initializer_list<juce::TextButton*>{&add, &undo, &redo, &remove, &bypass, &fit, &zoomOut, &zoomIn, &fxOn, &disconnect, &addModulator, &assignmentBanner}) {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(neon ? 0xff202e47 : 0xff3b413a));
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(neon ? 0xff64f5e3 : 0xfff5a24b));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
        }
        for (auto& button : modulatorButtons) {
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(neon ? 0xff202e47 : 0xff3b413a));
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(neon ? 0xff64f5e3 : 0xfff5a24b));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
            button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
        }
        for (auto& field : fields) field->setNeon(neon);
        if (tonePanel) tonePanel->setNeon(neon);
                if (voiceControls) voiceControls->setNeon(neon);
                if (mixControl) mixControl->setNeon(neon);
                modulationPanel.setNeon(neon);
        modulationui::styleRotary(nodeValue, neon);
        repaint(); canvas.repaint();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(neon ? 0xff090d17 : 0xff292b2a));
        g.setColour(juce::Colour(neon ? 0xff2b3a53 : 0xff515850));
        g.drawVerticalLine(inspectorDivider.getBounds().getCentreX(), 46.0f, float(inspector.getBottom()));
        if (modulationDivider.isVisible())
            g.drawHorizontalLine(modulationDivider.getBounds().getCentreY(), float(canvas.getX()), float(canvas.getRight()));
        g.setColour(juce::Colour(0xffaebaca));
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText("Output drag: move / empty drop disconnects  |  Ctrl/Cmd+drag output: source  |  Wheel: zoom  |  Alt/middle drag: pan", 12, 43, getWidth() - 24, 22, juce::Justification::centredLeft);
    }
    void resized() override {
        auto toolbar = getLocalBounds().reduced(12).removeFromTop(28);
        for (auto* b : {&add, &undo, &redo, &remove, &bypass, &fit, &zoomOut, &zoomIn}) {
            const int width = b == &zoomIn || b == &zoomOut ? 30 : b == &remove ? 102 : b == &bypass ? 90 : 66;
            b->setBounds(toolbar.removeFromLeft(width)); toolbar.removeFromLeft(6);
        }
        const int inspectorWidth = constrainedInspectorWidth(desiredInspectorWidth);
        const int railWidth = 132;
        const int bottom = getHeight() - 40;
        const int modulationHeight = constrainedModulationHeight(desiredModulationHeight);
        const int stripTop = selectedModulator ? bottom - modulationHeight : bottom;
        const int x = getWidth() - inspectorWidth - 12;
        canvas.setBounds(railWidth + 24, 70, juce::jmax(100, x - railWidth - 44), juce::jmax(0, stripTop - 78));
        assignmentBanner.setBounds(x, 44, inspectorWidth, 27);
        inspector.setBounds(x, 76, inspectorWidth, juce::jmax(0, bottom - 84));
        inspectorDivider.setBounds(x - 13, 44, 10, juce::jmax(0, bottom - 52));
        addModulator.setBounds(12, 72, railWidth, 28);
        modulatorViewport.setBounds(12, 106, railWidth, juce::jmax(0, bottom - 106));
        modulatorStrip.setSize(railWidth - modulatorViewport.getScrollBarThickness(), int(modulatorButtons.size()) * 88);
        for (size_t i = 0; i < modulatorButtons.size(); ++i)
            modulatorButtons[i]->setBounds(0, int(i) * 88, modulatorStrip.getWidth(), 82);
        modulationDivider.setVisible(selectedModulator != 0);
        modulationDivider.setBounds(canvas.getX(), stripTop - 8, canvas.getWidth(), 8);
        modulationPanel.setBounds(canvas.getX(), stripTop, canvas.getWidth(), modulationHeight);
        status.setBounds(8, getHeight() - 33, getWidth() - 16, 28);
        layoutInspector();
    }
public:
    // Editor bridge: -1 = Lead, 0..7 = generated voice. Does not end Assign.
    void selectVoice(int slot) {
        for (const auto& n : graph.nodes)
            if ((slot < 0 && n.type == NodeType::lead) || (n.type == NodeType::voice && int(n.voiceSlot) == slot)) {
                selectedNode = n.id; selectedLink = -1;
                rebuildInspector(); saveLayout(); canvas.repaint(); return;
            }
    }
    uint32_t selectedModulationId() const { return modulationui::selectedModulator(owner); }
        std::function<void(int)> onSelectVoice;
    std::function<void(const juce::String&)> onRevealParameter;
private:
    int constrainedInspectorWidth(int desired) const {
        // Match resized(): rail 132 + margins/gap 56 + canvas minimum 100.
        const int available = juce::jmax(0, getWidth() - 288);
        return juce::jmin(juce::jlimit(320, 480, desired), available);
    }
    void saveInspectorWidth() {
        auto saved = owner.getRoutingLayout();
        const bool currentLayout = saved.toXmlString() == lastLayoutSignature;
        if (!saved.isValid()) saved = juce::ValueTree("RoutingLayout");
        saved.setProperty("inspectorWidth", desiredInspectorWidth, nullptr);
        owner.setRoutingLayout(saved);
        // Do not hide a pending external layout change from the timer.
        if (currentLayout) lastLayoutSignature = saved.toXmlString();
    }
    int constrainedModulationHeight(int desired) const {
        // Canvas starts at 70; reserve the 8px divider and 40px status area.
        const int available = juce::jmax(0, getHeight() - 118);
        const int canvasMinimum = juce::jlimit(0, 100, available - 160);
        return juce::jmin(juce::jlimit(160, 480, desired), available - canvasMinimum);
    }
    void saveModulationHeight() {
        auto saved = owner.getRoutingLayout();
        const bool currentLayout = saved.toXmlString() == lastLayoutSignature;
        if (!saved.isValid()) saved = juce::ValueTree("RoutingLayout");
        saved.setProperty("modulationHeight", desiredModulationHeight, nullptr);
        owner.setRoutingLayout(saved);
        // Do not hide a pending external layout change from the timer.
        if (currentLayout) lastLayoutSignature = saved.toXmlString();
    }
    void refreshAssignmentBanner() {
        if (isShowing() && juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::escapeKey)) owner.endModulationAssignment();
        juce::String text;
        for (const auto& m : owner.getModulationState().modulators)
            if (m.id == owner.modulationAssignmentId()) text = "Assigning: " + modulatorName(m) + "  /  Escape or click to cancel";
        assignmentBanner.setButtonText(text); assignmentBanner.setVisible(text.isNotEmpty());
    }
    void showModulatorMenu() {
        juce::PopupMenu menu; menu.addItem(1, "Classic LFO"); menu.addItem(2, "Random");
        juce::Component::SafePointer<RoutingPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addModulator), [safe](int result) {
            if (!safe || result == 0) return;
            auto state = safe->owner.getModulationState(); Modulator m;
            m.id = 1; for (const auto& existing : state.modulators) m.id = juce::jmax(m.id, existing.id + 1);
            m.type = result == 1 ? ModulatorType::lfo : ModulatorType::random;
            state.modulators.push_back(m); juce::String error;
            if (!safe->owner.setModulationState(state, error)) { safe->error = error; safe->showStatus(); return; }
            safe->selectedModulator = m.id;
            safe->rebuildModulatorStrip(); safe->updateModulationPanel(); safe->saveLayout();
        });
    }
    void rebuildModulatorStripIfChanged() {
        juce::String signature;
        for (const auto& m : owner.getModulationState().modulators) signature += juce::String(m.id) + ":" + juce::String(int(m.type)) + ";";
        if (signature == modulatorSignature) return;
        modulatorSignature = signature; rebuildModulatorStrip();
        if (selectedModulator) {
            bool found = false; for (const auto& m : owner.getModulationState().modulators) found |= m.id == selectedModulator;
            if (!found) { selectedModulator = 0; updateModulationPanel(); saveLayout(); }
        }
    }
    void rebuildModulatorStrip() {
        modulatorButtons.clear();
        for (const auto& m : owner.getModulationState().modulators) {
            auto button = std::make_unique<ModulatorTile>(m);
            button->setTooltip("Select modulator controls / assignments; audio routing selection never cancels Assign");

            button->onClick = [this, id = m.id] {
                selectedModulator = id;
                updateModulationPanel(); saveLayout();
            };
            detail::passiveControl(*button); modulatorStrip.addAndMakeVisible(*button); modulatorButtons.push_back(std::move(button));
        }
        resized(); setNeon(neon);
    }
    void updateModulationPanel() {
        const auto state = owner.getModulationState();
        bool found = false;
        for (const auto& m : state.modulators) found |= m.id == selectedModulator;
        if (!found) selectedModulator = 0;
        if (selectedModulator) modulationui::selectModulator(owner, selectedModulator);
        if (modulationPanel.selectedId() != selectedModulator) modulationPanel.select(selectedModulator);
        modulationPanel.setVisible(selectedModulator != 0);
        for (auto& tile : modulatorButtons)
            tile->setToggleState(tile->model.id == selectedModulationId(), juce::dontSendNotification);
        resized(); repaint();
    }
    void revealTarget(const juce::String& id) {
        if (id.startsWith("voice")) { selectVoice(id.substring(5).getIntValue() - 1); }
        else if (id.startsWith("lead")) selectVoice(-1);
        else {
            for (const auto& n : graph.nodes) {
                const bool fx = id.startsWith("fx") && isEffect(n.type) && fxSlot(n) == id.substring(2).getIntValue();
                const bool legacyFx = id.startsWith("chorale") && n.type == NodeType::chorale && n.mainControls;
                const bool mix = n.type == NodeType::mix && (n.mainControls ? id == "double" : id == localMixParameterId(n.id));
                const bool output = id == "outputGain" && n.type == NodeType::output;
                if (fx || legacyFx || mix || output) { selectedNode = n.id; selectedLink = -1; rebuildInspector(); break; }
            }
        }
        if (tonePanel) tonePanel->revealParameter(id);
        std::function<juce::Component*(juce::Component&)> find = [&](juce::Component& c) -> juce::Component* {
            if (c.getComponentID() == id) return &c;
            for (auto* child : c.getChildren()) if (auto* match = find(*child)) return match;
            return nullptr;
        };
        if (auto* target = find(inspector)) {
            if (auto* content = toneViewport.getViewedComponent(); content && content->isParentOf(target)) {
                const auto bounds = content->getLocalArea(target, target->getLocalBounds());
                toneViewport.setViewPosition(0, juce::jmax(0, bounds.getY() - 48));
            }
            addChildComponent(revealBubble);
            juce::AttributedString text; text.append("Target: " + id, juce::Font(juce::FontOptions(12.0f)), juce::Colours::white);
            revealBubble.showAt(target, text, 1600, true, false);
        } else if (onRevealParameter) onRevealParameter(id);
        saveLayout(); canvas.repaint();
    }
    static juce::String signature(const Graph& g) {
        juce::String s;
        for (const auto& n : g.nodes)
            s += juce::String(n.id) + ":" + juce::String(int(n.type)) + ":" + juce::String(n.voiceSlot) + ":"
                + juce::String(int(n.enabled)) + ":" + juce::String(n.type == NodeType::mix && !localMixParameterId(n.id).isEmpty() ? 0.0f : n.mix, 6) + ":" + juce::String(n.motion, 6)
                + ":" + juce::String(int(n.mainControls)) + ":" + juce::String(n.referencePower, 6) + ":" + juce::String(n.latencySamples) + ";";
        s += "|";
        for (const auto& l : g.links) s += juce::String(l.source) + ":" + juce::String(l.sourcePin) + ">" + juce::String(l.destination) + ":" + juce::String(l.inputPin) + ";";
        return s;
    }
    juce::String title(const RoutingNode& n) const {
        if (const auto alias = names.find(n.id); alias != names.end()) return alias->second;
        switch (n.type) {
        case NodeType::lead: return "LEAD";
        case NodeType::voice: return "VOICE " + juce::String(n.voiceSlot + 1);
        case NodeType::chorale: return n.mainControls ? "CHORALE / MAIN" : "CHORALE " + juce::String(int(n.id) - 98);
        case NodeType::sum: return {}; // Legacy nodes are stripped by the processor.
        case NodeType::mix: return n.mainControls ? "LEAD / STACK" : "DRY / WET MIX";
        case NodeType::output: return "OUTPUT";
        case NodeType::waveshaper: return "WAVESHAPER " + juce::String(routingFxSlot(n.id));
        case NodeType::equalizer: return "EQ8 " + juce::String(routingFxSlot(n.id));
        case NodeType::crossover: return "MULTIBAND " + juce::String(routingFxSlot(n.id));
        }
        return {};
    }
    static bool isEffect(NodeType type) {
        return type == NodeType::chorale || type == NodeType::waveshaper
            || type == NodeType::equalizer || type == NodeType::crossover;
    }
    static int inputs(const RoutingNode& n) {
        return n.type == NodeType::mix ? 2 : isEffect(n.type) || n.type == NodeType::output ? 1 : 0;
    }
    int outputPins(const RoutingNode& n) const {
        if (n.type == NodeType::output || n.type == NodeType::sum) return 0;
        if (n.type != NodeType::crossover) return 1;
        const int slot = fxSlot(n);
        return (slot > 0 ? toneui::value(owner, toneParameterId(size_t(slot), "BandCount"), float(n.bandCount))
                         : float(n.bandCount)) >= 2.5f ? 3 : 2;
    }
    bool effectEnabled(const RoutingNode& n) const {
        const int slot = fxSlot(n);
        return slot >= 0 ? toneui::value(owner, fxParameterId(size_t(slot), "On"), n.enabled ? 1.0f : 0.0f) >= 0.5f : n.enabled;
    }
    bool inactiveSource(const RoutingLink& link) const {
        for (const auto& n : graph.nodes) if (n.id == link.source)
            return link.sourcePin >= outputPins(n) || (n.type == NodeType::crossover && !effectEnabled(n) && link.sourcePin != 0);
        return false;
    }
    bool hasStoredPort(const RoutingNode& n) const {
        return n.type == NodeType::crossover && outputPins(n) == 2
            && std::any_of(graph.links.begin(), graph.links.end(), [&n](const auto& l) { return l.source == n.id && l.sourcePin == 2; });
    }
    juce::String outputName(const RoutingNode& n, int pin) const {
        if (n.type != NodeType::crossover) return "Out";
        if (pin >= outputPins(n)) return "Stored #2 | silent";
        if (!effectEnabled(n)) return pin == 0 ? "Dry / full-band" : "Silent / bypass";
        return pin == 0 ? "Low" : outputPins(n) == 3 && pin == 1 ? "Mid" : "High";
    }
    RoutingNode* node(std::uint32_t id) {
        for (auto& n : graph.nodes) if (n.id == id) return &n;
        return nullptr;
    }
    const RoutingNode* node(std::uint32_t id) const {
        for (const auto& n : graph.nodes) if (n.id == id) return &n;
        return nullptr;
    }
    bool voicePresent(int slot) const {
        if (slot < 0 || slot >= 8) return false;
        if (toneui::value(owner, "advancedOn", 0) < 0.5f)
            return basicVoiceSettings(0).at(size_t(slot)).present;
        return toneui::value(owner, voicePresentParameterId(size_t(slot)), slot < 2 ? 1.0f : 0.0f) >= 0.5f;
    }
    // Only the untouched canonical main chain may be represented as a direct
    // bypass cable. No alternate graph is published or used for processing.
    std::pair<int, int> foldedChain() const {
        const auto* mix = node(11); const auto* fx = node(12); const auto* out = node(13);
        if (!mix || mix->type != NodeType::mix || !mix->mainControls
            || !fx || fx->type != NodeType::chorale || !fx->mainControls
            || effectEnabled(*fx) || !out || out->type != NodeType::output) return {-1, -1};
        int incoming = -1, outgoing = -1, touching = 0;
        for (size_t i = 0; i < graph.links.size(); ++i) {
            const auto& l = graph.links[i];
            if (l.source == 12 || l.destination == 12) ++touching;
            if (l.source == 11 && l.sourcePin == 0 && l.destination == 12 && l.inputPin == 0) incoming = int(i);
            if (l.source == 12 && l.sourcePin == 0 && l.destination == 13 && l.inputPin == 0) outgoing = int(i);
        }
        return touching == 2 && incoming >= 0 && outgoing >= 0
            ? std::make_pair(incoming, outgoing) : std::make_pair(-1, -1);
    }
    bool nodeVisible(const RoutingNode& n) const {
        if (n.type == NodeType::sum) return false;
        if (n.type == NodeType::voice) return voicePresent(n.voiceSlot);
        if (n.id == 12 && n.type == NodeType::chorale && n.mainControls && !effectEnabled(n)) {
            if (foldedChain().first >= 0) return false;
            return std::any_of(graph.links.begin(), graph.links.end(), [](const auto& l) { return l.source == 12 || l.destination == 12; });
        }
        return true;
    }
    struct DisplayLink { RoutingLink link; int index; int foldedEnd = -1; };
    std::vector<DisplayLink> displayLinks() const {
        std::vector<DisplayLink> result;
        const auto fold = foldedChain();
        for (size_t i = 0; i < graph.links.size(); ++i) {
            if (int(i) == fold.second) continue;
            auto l = graph.links[i];
            if (int(i) == fold.first) { l.destination = 13; l.inputPin = 0; }
            const auto* a = node(l.source); const auto* b = node(l.destination);
            if (a && b && nodeVisible(*a) && nodeVisible(*b))
                result.push_back({l, int(i), int(i) == fold.first ? fold.second : -1});
        }
        return result;
    }
    bool displayLink(int index, DisplayLink& result) const {
        for (const auto& item : displayLinks()) if (item.index == index) { result = item; return true; }
        return false;
    }
    void eraseDisplayedLink(const DisplayLink& item) {
        if (item.foldedEnd >= 0) {
            graph.links.erase(graph.links.begin() + juce::jmax(item.index, item.foldedEnd));
            graph.links.erase(graph.links.begin() + juce::jmin(item.index, item.foldedEnd));
        } else graph.links.erase(graph.links.begin() + item.index);
    }
    void clearOutput(std::uint32_t source, int pin) {
        const auto fold = foldedChain();
        if (source == 11 && pin == 0 && fold.first >= 0)
            eraseDisplayedLink({{}, fold.first, fold.second});
        graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [source, pin](const auto& l) {
            return l.source == source && l.sourcePin == pin;
        }), graph.links.end());
    }
    void disconnectSelection() {
        DisplayLink item;
        if (!displayLink(selectedLink, item)) return;
        remember(); eraseDisplayedLink(item); selectedLink = -1;
        publish(); rebuildInspector();
    }
    void validateSelection() {
        if (const auto* n = node(selectedNode); !n || !nodeVisible(*n)) selectedNode = 0;
        DisplayLink item;
        if (!displayLink(selectedLink, item)) selectedLink = -1;
    }
    int fxSlot(const RoutingNode& n) const {
        return isEffect(n.type) ? routingFxSlot(n.id) : -1;
    }
    juce::Rectangle<float> nodeBounds(const RoutingNode& n) const {
        const auto it = positions.find(n.id);
        const auto p = it == positions.end() ? juce::Point<float>() : it->second;
        const int rows = juce::jmax(inputs(n), outputPins(n) + (hasStoredPort(n) ? 1 : 0));
        return {p.x, p.y, 190.0f, float(juce::jmax(64, 40 + rows * 22))};
    }
    juce::Point<float> pinPoint(const RoutingNode& n, int pin, bool output) const {
        const auto b = nodeBounds(n);
        return {output ? b.getRight() : b.getX(), b.getY() + 42 + float(pin * 22)};
    }
    juce::Point<float> toScreen(juce::Point<float> p) const { return p * zoom + pan; }
    juce::Point<float> toWorld(juce::Point<float> p) const { return (p - pan) / zoom; }
    juce::Path cable(juce::Point<float> a, juce::Point<float> b) const {
        a = toScreen(a); b = toScreen(b);
        const float bend = juce::jmax(35.0f, std::abs(b.x - a.x) * 0.45f);
        juce::Path p; p.startNewSubPath(a); p.cubicTo(a.x + bend, a.y, b.x - bend, b.y, b.x, b.y); return p;
    }
    int connectionCount(std::uint32_t id, int pin) const {
        int count = 0;
        for (const auto& item : displayLinks())
            if (item.link.destination == id && item.link.inputPin == pin) ++count;
        return count;
    }
    void paintCanvas(juce::Graphics& g) {
        g.fillAll(juce::Colour(neon ? 0xff0c1220 : 0xff1c201e));
        const auto accent = juce::Colour(neon ? 0xff64f5e3 : 0xfff5a24b);
        g.setColour(juce::Colour(0xff35414b));
        const float step = 32 * zoom;
        for (float x = std::fmod(pan.x, step); x < float(canvas.getWidth()); x += step)
            for (float y = std::fmod(pan.y, step); y < float(canvas.getHeight()); y += step) g.fillRect(x, y, 1.0f, 1.0f);
        for (const auto& item : displayLinks()) {
            const int i = item.index;
            if (i == rewiringLink) continue;
            const auto& l = item.link;
            auto* a = node(l.source); auto* b = node(l.destination);
            if (!a || !b) continue;
            const bool inactive = inactiveSource(l);
            g.setColour(int(i) == selectedLink ? juce::Colour(0xffffd477) : inactive ? juce::Colour(0xff83909b).withAlpha(0.5f) : accent.withAlpha(0.7f));
            const auto path = cable(pinPoint(*a, l.sourcePin, true), pinPoint(*b, l.inputPin, false));
            const juce::PathStrokeType stroke(int(i) == selectedLink ? 3.5f : 2.0f);
            if (inactive) {
                juce::Path dashed; const float lengths[]{5.0f, 4.0f};
                stroke.createDashedStroke(dashed, path, lengths, 2);
                g.fillPath(dashed);
            } else g.strokePath(path, stroke);
        }
        if (rewiringLink >= 0 && rewiringSource) {
            DisplayLink item;
            if (displayLink(rewiringLink, item)) if (auto* target = node(item.link.destination)) {
                const auto& link = item.link;
                g.setColour(accent);
                g.strokePath(cable(toWorld(pointer), pinPoint(*target, link.inputPin, false)), juce::PathStrokeType(2.0f));
            }
        } else if (connecting) if (auto* n = node(connecting); n && nodeVisible(*n) && connectingPin < outputPins(*n)) {
            g.setColour(accent); g.strokePath(cable(pinPoint(*n, connectingPin, true), toWorld(pointer)), juce::PathStrokeType(2.0f));
        }
        for (const auto& n : graph.nodes) {
            if (!nodeVisible(n)) continue;
            const auto b = nodeBounds(n);
            const auto screen = juce::Rectangle<float>(toScreen(b.getTopLeft()), toScreen(b.getBottomRight()));
            const auto nodeAccent = n.type == NodeType::voice ? VoiceMonitor::voiceColour(size_t(juce::jlimit(0, 7, n.voiceSlot))) : accent;
            g.setColour(juce::Colour(neon ? 0xff172239 : 0xff303730));
            g.fillRoundedRectangle(screen, 4 * zoom);
            g.setColour(nodeAccent.withAlpha(0.12f));
            g.fillRect(screen.withHeight(28 * zoom));
            g.setColour(n.id == selectedNode ? accent : juce::Colour(neon ? 0xff465c7c : 0xff65705e));
            g.drawRoundedRectangle(screen, 4 * zoom, n.id == selectedNode ? 2.0f : 1.0f);
            g.setFont(juce::Font(juce::FontOptions(juce::jmax(9.0f, 11.5f * zoom))));
            g.setColour(juce::Colour(0xffe4e9ee));
            auto heading = screen.toNearestInt().reduced(8, 0).withHeight(juce::roundToInt(28 * zoom));
            g.drawText(title(n), isEffect(n.type) && !effectEnabled(n) ? heading.withTrimmedRight(65) : heading,
                       juce::Justification::centredLeft);
            for (int pin = 0; pin < inputs(n); ++pin) {
                const auto p = toScreen(pinPoint(n, pin, false));
                const int count = connectionCount(n.id, pin);
                g.setColour(count > 0 ? accent : juce::Colour(0xff68788b));
                // Pill-shaped inputs advertise fan-in, rather than a single-link socket.
                g.fillRoundedRectangle(p.x - 4, p.y - 6, 8, 12, 4);
                g.setColour(juce::Colour(0xffbcc8d8));
                const juce::String name = n.type == NodeType::mix ? (pin == 0 ? "Dry" : "Wet") : "In";
                g.drawText(name + (count > 1 ? " x" + juce::String(count) : juce::String()), int(p.x + 9), int(p.y - 7), 70, 14, juce::Justification::centredLeft);
            }
            for (int pin = 0; pin < outputPins(n); ++pin) {
                const auto p = toScreen(pinPoint(n, pin, true));
                const bool silent = n.type == NodeType::crossover && !effectEnabled(n) && pin > 0;
                g.setColour(silent ? juce::Colour(0xff68788b) : nodeAccent);
                g.fillEllipse(p.x - 4, p.y - 4, 8, 8);
                g.setColour(juce::Colour(0xffbcc8d8));
                g.drawText(outputName(n, pin), int(p.x - 105 * zoom), int(p.y - 7), int(96 * zoom), 14, juce::Justification::centredRight);
            }
            if (hasStoredPort(n)) {
                const auto p = toScreen(pinPoint(n, 2, true));
                g.setColour(juce::Colour(0xff929da6));
                g.drawText("Stored #2 | silent", int(p.x - 140 * zoom), int(p.y - 7), int(132 * zoom), 14, juce::Justification::centredRight);
            }
            if (isEffect(n.type) && !effectEnabled(n)) {
                g.setColour(juce::Colour(0xffa6aeb9));
                g.drawText("Bypassed", heading.removeFromRight(65), juce::Justification::centredRight);
            }
        }
    }
    void canvasDown(const juce::MouseEvent& e) {
        pointer = e.position; dragStart = e.position; startPan = pan;
        draggingNode = 0; connecting = 0; connectingPin = 0; panning = false; rewiringLink = -1; rewiringSource = false;
        const auto world = toWorld(e.position);
        if (e.mods.isMiddleButtonDown() || e.mods.isAltDown()) { panning = true; return; }
        const auto pickEnd = [&](int index, bool sourceEnd) {
            if (index < 0 || index >= int(graph.links.size())) return false;
            DisplayLink item;
            if (!displayLink(index, item)) return false;
            const auto& link = item.link;
            const auto* target = node(sourceEnd ? link.source : link.destination);
            if (!target || toScreen(pinPoint(*target, sourceEnd ? link.sourcePin : link.inputPin, sourceEnd)).getDistanceFrom(e.position) >= 10) return false;
            rewiringLink = selectedLink = index; rewiringSource = sourceEnd; selectedNode = 0;
            if (!sourceEnd) { connecting = link.source; connectingPin = link.sourcePin; }
            rebuildInspector(); canvas.repaint(); return true;
        };
        // Custom single-output policy, not Blender's fanout behavior: ordinary
        // output drags move destinations, even when a cable is selected.
        // Only Ctrl/Command opts into moving the source of a selected/last link.
        if (!e.mods.isPopupMenu()) {
            if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
                if (pickEnd(selectedLink, true)) return;
                for (int i = int(graph.links.size()) - 1; i >= 0; --i) if (pickEnd(i, true)) return;
            }
            if (pickEnd(selectedLink, false)) return;
            for (int i = int(graph.links.size()) - 1; i >= 0; --i) if (pickEnd(i, false)) return;
        }
        for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it) {
            if (!nodeVisible(*it)) continue;
            if (!e.mods.isPopupMenu()) for (int pin = 0; pin < outputPins(*it); ++pin) {
                if (toScreen(pinPoint(*it, pin, true)).getDistanceFrom(e.position) < 10) {
                    connecting = it->id; connectingPin = pin; selectedNode = it->id; selectedLink = -1;
                    rebuildInspector(); canvas.repaint(); return;
                }
            }
            if (nodeBounds(*it).contains(world)) {
                selectedNode = it->id; selectedLink = -1;
                if (!e.mods.isPopupMenu()) {
                    draggingNode = it->id; startNode = positions[it->id]; dragBefore = state();
                }
                rebuildInspector(); canvas.repaint();
                if (e.mods.isPopupMenu()) showNodeMenu();
                return;
            }
        }
        selectedNode = 0; selectedLink = -1;
        for (int i = int(graph.links.size()) - 1; i >= 0; --i) {
            DisplayLink item;
            if (!displayLink(i, item)) continue;
            const auto& l = item.link; auto* a = node(l.source); auto* b = node(l.destination);
            if (!a || !b) continue;
            juce::Path hit;
            juce::PathStrokeType(12.0f).createStrokedPath(hit, cable(pinPoint(*a, l.sourcePin, true), pinPoint(*b, l.inputPin, false)));
            if (hit.contains(e.position)) { selectedLink = i; break; }
        }
        panning = selectedLink < 0;
        rebuildInspector(); canvas.repaint();
        if (e.mods.isPopupMenu()) { panning = false; showAddMenu(); }
    }
    void canvasDrag(const juce::MouseEvent& e) {
        pointer = e.position;
        if (draggingNode) positions[draggingNode] = startNode + (e.position - dragStart) / zoom;
        else if (panning) pan = startPan + e.position - dragStart;
        canvas.repaint();
    }
    void canvasUp(const juce::MouseEvent& e) {
        const bool cableEdit = connecting != 0 || rewiringLink >= 0;
        if (rewiringLink >= 0) {
            if (e.position.getDistanceFrom(dragStart) > 2) {
                DisplayLink item;
                if (!displayLink(rewiringLink, item)) { connecting = 0; rewiringLink = -1; rebuildInspector(); return; }
                auto moved = item.link;
                bool found = false;
                for (const auto& n : graph.nodes) {
                    if (!nodeVisible(n)) continue;
                    if (rewiringSource) {
                        for (int pin = 0; pin < outputPins(n); ++pin)
                            if (toScreen(pinPoint(n, pin, true)).getDistanceFrom(e.position) < 12) {
                                moved.source = n.id; moved.sourcePin = pin; found = true; break;
                            }
                    } else {
                        for (int pin = 0; pin < inputs(n); ++pin)
                            if (toScreen(pinPoint(n, pin, false)).getDistanceFrom(e.position) < 12) {
                                moved.destination = n.id; moved.inputPin = pin; found = true; break;
                            }
                    }
                    if (found) break;
                }
                remember();
                // Erase both canonical links for a folded cable before making
                // a real direct replacement. Other saved fanout is untouched
                // until its source output is explicitly edited.
                eraseDisplayedLink(item);
                if (found) {
                    clearOutput(item.link.source, item.link.sourcePin);
                    clearOutput(moved.source, moved.sourcePin);
                    graph.links.push_back(moved);
                }
                selectedLink = -1; rewiringLink = -1; publish();
            }
        } else if (connecting && e.position.getDistanceFrom(dragStart) > 2) {
            // A click is selection only. A real output drag replaces all old
            // destinations (including dormant saved fanout), or clears them on
            // an empty drop. clearOutput also removes both folded chain links.
            const auto* source = node(connecting);
            if (source && nodeVisible(*source) && connectingPin < outputPins(*source)) {
                RoutingLink replacement{connecting, 0, 0, connectingPin};
                bool found = false;
                for (const auto& n : graph.nodes) {
                    if (!nodeVisible(n)) continue;
                    for (int pin = 0; pin < inputs(n); ++pin) {
                        if (toScreen(pinPoint(n, pin, false)).getDistanceFrom(e.position) < 12) {
                            replacement.destination = n.id; replacement.inputPin = pin;
                            found = true; break;
                        }
                    }
                    if (found) break;
                }
                const bool occupied = std::any_of(graph.links.begin(), graph.links.end(), [this](const auto& l) {
                    return l.source == connecting && l.sourcePin == connectingPin;
                });
                if (found || occupied) {
                    remember(); clearOutput(connecting, connectingPin);
                    if (found) graph.links.push_back(replacement);
                    selectedLink = -1; publish();
                }
            }
        }
        if (draggingNode && e.position.getDistanceFrom(dragStart) > 2) {
            undoStates.push_back(dragBefore); limitHistory(); redoStates.clear(); saveLayout(); updateButtons();
        }
        if (!cableEdit) saveLayout();
        connecting = 0; connectingPin = 0; draggingNode = 0; rewiringLink = -1; rewiringSource = false; panning = false;
        if (cableEdit) rebuildInspector();
        canvas.repaint();
    }
    void zoomAt(juce::Point<float> point, float factor) {
        const auto world = toWorld(point);
        zoom = juce::jlimit(0.35f, 1.8f, zoom * factor);
        pan = point - world * zoom; saveLayout(); canvas.repaint();
    }
    void fitGraph() {
        juce::Rectangle<float> bounds;
        for (const auto& n : graph.nodes) if (nodeVisible(n)) bounds = bounds.getUnion(nodeBounds(n));
        if (bounds.isEmpty()) return;
        zoom = juce::jlimit(0.35f, 1.5f, juce::jmin(float(canvas.getWidth() - 40) / bounds.getWidth(), float(canvas.getHeight() - 40) / bounds.getHeight()));
        pan = canvas.getLocalBounds().getCentre().toFloat() - bounds.getCentre() * zoom;
        saveLayout(); canvas.repaint();
    }
    juce::ValueTree layout() const {
        auto tree = owner.getRoutingLayout().createCopy();
        if (!tree.isValid()) tree = juce::ValueTree("RoutingLayout");
        for (int i = tree.getNumChildren(); --i >= 0;) if (tree.getChild(i).hasType("NODE")) tree.removeChild(i, nullptr);
        tree.setProperty("inspectorWidth", desiredInspectorWidth, nullptr);
        tree.setProperty("modulationHeight", desiredModulationHeight, nullptr);
        tree.setProperty("selectedNode", int(selectedNode), nullptr);
        tree.setProperty("selectedModulator", int(selectedModulator), nullptr);
        if (const auto* n = node(selectedNode); n && (n->type == NodeType::voice || n->type == NodeType::lead))
            tree.setProperty("selectedVoiceSlot", n->type == NodeType::lead ? -1 : int(n->voiceSlot), nullptr);
        tree.setProperty("zoom", zoom, nullptr); tree.setProperty("panX", pan.x, nullptr); tree.setProperty("panY", pan.y, nullptr);
        for (const auto& p : positions) {
            juce::ValueTree child("NODE"); child.setProperty("id", int(p.first), nullptr);
            child.setProperty("x", p.second.x, nullptr); child.setProperty("y", p.second.y, nullptr);
                        if (const auto alias = names.find(p.first); alias != names.end()) child.setProperty("name", alias->second, nullptr);
                        tree.addChild(child, -1, nullptr);
        }
        return tree;
    }
    void loadLayout(const juce::ValueTree& tree, bool restorePanelSizes = true) {
        // Divider preferences are independent of graph undo/redo snapshots.
        if (restorePanelSizes) {
            desiredInspectorWidth = juce::jlimit(320, 480, int(tree.getProperty("inspectorWidth", 320)));
            desiredModulationHeight = juce::jlimit(160, 480, int(tree.getProperty("modulationHeight", ModulationPanel::preferredHeight)));
        }
        positions.clear(); names.clear();
        selectedNode = uint32_t(int(tree.getProperty("selectedNode", 0)));
        selectedModulator = uint32_t(int(tree.getProperty("selectedModulator", 0)));
        const int voice = int(tree.getProperty("selectedVoiceSlot", -2));
        if (voice != lastSelectedVoiceSlot && voice >= -1 && voice < 8) {
            // On reopen retain a saved FX/mixer selection alongside the modulator.
            // A subsequent Stack-card selection still reveals that voice here.
            const bool revealVoice = lastSelectedVoiceSlot != -2 || !node(selectedNode);
            lastSelectedVoiceSlot = voice;
            if (revealVoice) for (const auto& n : graph.nodes)
                if ((voice == -1 && n.type == NodeType::lead) || (n.type == NodeType::voice && int(n.voiceSlot) == voice)) selectedNode = n.id;
        }
        zoom = juce::jlimit(0.35f, 1.8f, float(tree.getProperty("zoom", 0.72f)));
        pan = {float(tree.getProperty("panX", 20.0f)), float(tree.getProperty("panY", 18.0f))};
        if (!std::isfinite(zoom)) zoom = 0.72f;
        if (!std::isfinite(pan.x) || !std::isfinite(pan.y)) pan = {20, 18};
        for (const auto& c : tree) {
            const int id = int(c.getProperty("id"));
            const float x = float(c.getProperty("x")), y = float(c.getProperty("y"));
            if (id > 0 && std::isfinite(x) && std::isfinite(y)) {
                positions[std::uint32_t(id)] = {x, y};
                const auto alias = c.getProperty("name").toString().trim().substring(0, 48);
                if (alias.isNotEmpty()) names[std::uint32_t(id)] = alias;
            }
        }
        resized();
    }
    void ensurePositions() {
        int extra = 0;
        for (const auto& n : graph.nodes) if (positions.count(n.id) == 0) {
            juce::Point<float> p;
            switch (n.type) {
            case NodeType::lead: p = {24, 20}; break;
            case NodeType::voice: p = {24, 100.0f + float(n.voiceSlot * 70)}; break;
            case NodeType::sum: p = {}; break;
            case NodeType::mix: p = {490, 30}; break;
            case NodeType::chorale:
            case NodeType::waveshaper:
            case NodeType::equalizer:
            case NodeType::crossover: p = {710, 30}; break;
            case NodeType::output: p = {930, 30}; break;
            }
            if (n.id >= 100) p = {270.0f + float(extra % 3) * 200, 460.0f + float(extra / 3) * 100};
            positions[n.id] = p; ++extra;
        }
    }
    void saveLayout() {
        const auto saved = layout();
        owner.setRoutingLayout(saved);
        lastLayoutSignature = saved.toXmlString();
    }
    EditState state() const {
        EditState result{owner.getRoutingGraph(), layout(), {}};
        for (size_t slot = 0; slot < 16; ++slot)
            for (size_t field = 0; field < 3; ++field)
                if (const auto* p = owner.parameters.getRawParameterValue(fxParameterId(slot, field))) result.fxValues[slot][field] = p->load();
        for (size_t slot = 1; slot < 16; ++slot)
            for (size_t field = 0; field < toneParametersPerSlot; ++field)
                if (const auto* p = owner.parameters.getRawParameterValue(parameterIds[toneParameterIndex(slot, field)]))
                    result.toneValues[slot - 1][field] = p->load();
        for (size_t slot = 1; slot < 16; ++slot)
            for (size_t field = 0; field < 2; ++field)
                if (const auto* p = owner.parameters.getRawParameterValue(parameterIds[waveExtraParameterStart + (slot - 1) * 2 + field]))
                    result.waveValues[slot - 1][field] = p->load();
        for (size_t slot = 1; slot < 16; ++slot)
            if (const auto* p = owner.parameters.getRawParameterValue(toneParameterId(slot, "WaveQuality")))
                result.qualityValues[slot - 1] = p->load();
        for (size_t slot = 1; slot < 16; ++slot)
            if (const auto* p = owner.parameters.getRawParameterValue(toneParameterId(slot, "WaveLevelMatch")))
                result.levelMatchValues[slot - 1] = p->load();
        return result;
    }
    void limitHistory() { if (undoStates.size() > 80) undoStates.erase(undoStates.begin()); }
    void remember(unsigned fxOnMask = 0) {
        auto saved = state(); saved.fxOnUndoMask = fxOnMask;
        undoStates.push_back(std::move(saved)); limitHistory(); redoStates.clear();
    }
    void stepHistory(std::vector<EditState>& from, std::vector<EditState>& to) {
        if (from.empty()) return;
        auto current = state();
        const auto s = from.back(); from.pop_back();
        current.fxOnUndoMask = s.fxOnUndoMask;
        to.push_back(current);
        graph = s.graph; loadLayout(s.layout, false); selectedNode = 0; selectedLink = -1;
        if (!publish(false)) { from.push_back(s); to.pop_back(); rebuildInspector(); return; }
        for (size_t slot = 0; slot < 16; ++slot) {
            const auto id = slot == 0 ? 12u : unsigned(slot + 99);
            const auto findFx = [id](const Graph& g) -> const RoutingNode* {
                for (const auto& n : g.nodes) if (n.id == id && isEffect(n.type)) return &n;
                return nullptr;
            };
            const auto* before = findFx(current.graph); const auto* after = findFx(s.graph);
            const bool lifecycleChange = (!before != !after) || (before && after && before->type != after->type);
            // Cable/position undo must not rewind unrelated host automation.
            for (size_t field = 0; field < 3; ++field) {
                const bool changed = lifecycleChange || (field == 0 && (s.fxOnUndoMask & (1u << slot)))
                    || (before && after && (field == 0 ? before->enabled != after->enabled
                        : field == 1 ? before->mix != after->mix : before->motion != after->motion));
                if (!changed) continue;
                const auto parameter = fxParameterId(slot, field);
                if (const auto* p = owner.parameters.getRawParameterValue(parameter); p && p->load() != s.fxValues[slot][field])
                    setParameter(parameter, s.fxValues[slot][field]);
            }
            if (slot > 0 && lifecycleChange) {
                for (size_t field = 0; field < toneParametersPerSlot; ++field)
                    setParameter(parameterIds[toneParameterIndex(slot, field)], s.toneValues[slot - 1][field]);
                for (size_t field = 0; field < 2; ++field)
                    setParameter(parameterIds[waveExtraParameterStart + (slot - 1) * 2 + field], s.waveValues[slot - 1][field]);
                setParameter(toneParameterId(slot, "WaveQuality"), s.qualityValues[slot - 1]);
                setParameter(toneParameterId(slot, "WaveLevelMatch"), s.levelMatchValues[slot - 1]);
            }
        }
        rebuildInspector();
    }

    void showStatus() {
        status.setText(error.isNotEmpty() ? "EDIT REJECTED  /  " + error + "  /  Visible cables remain the running routing."
            : "LIVE ROUTING  /  Automatic gain compensation at merges. Unconnected inputs and Output are silent.", juce::dontSendNotification);
        status.setTooltip(status.getText());
        status.setColour(juce::Label::textColourId, juce::Colour(error.isEmpty() ? 0xffa9dec2 : 0xffff9f91));
        updateButtons();
    }
    bool publish(bool discardUndoOnReject = true) {
        ensurePositions();
        // Parameter-bound mixers follow current base automation on cable edits;
        // stale node copies must never rewind a knob or rebuild while dragging.
        const auto current = owner.getRoutingGraph();
        for (auto& n : graph.nodes) if (n.type == NodeType::mix && !localMixParameterId(n.id).isEmpty())
            for (const auto& old : current.nodes) if (old.id == n.id && old.type == n.type) { n.mix = old.mix; break; }
        juce::String problem;
        const bool accepted = owner.setRoutingGraph(graph, problem);
        error = problem;
        graph = owner.getRoutingGraph();
        if (!accepted) {
            selectedLink = -1;
            if (!node(selectedNode)) selectedNode = 0;
            if (discardUndoOnReject && !undoStates.empty()) undoStates.pop_back();
        }
        lastSignature = signature(graph);
        lastPortSignature = portSignature();
        saveLayout(); showStatus(); canvas.repaint();
        return accepted;
    }
    void updateButtons() {
        undo.setEnabled(!undoStates.empty()); redo.setEnabled(!redoStates.empty());

        DisplayLink item;
        disconnect.setEnabled(displayLink(selectedLink, item));
        const auto* n = node(selectedNode);
        remove.setEnabled(n && n->type != NodeType::output);
        bypass.setEnabled(n && isEffect(n->type));
        bypass.setButtonText(n && isEffect(n->type) && !effectEnabled(*n) ? "Enable FX" : "Bypass FX");
    }
    void deleteSelection() {
        const auto* n = node(selectedNode); if (!n || !nodeVisible(*n) || n->type == NodeType::output) return;
        remember(); const auto id = selectedNode;
        graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [id](const auto& l) { return l.source == id || l.destination == id; }), graph.links.end());
        graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(), [id](const auto& item) { return item.id == id; }), graph.nodes.end());
        positions.erase(id); names.erase(id); selectedNode = 0; selectedLink = -1; publish(); rebuildInspector();
    }
    void setParameter(const juce::String& id, float value) {
        if (auto* p = owner.parameters.getParameter(id)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture();
        }
    }
    void toggleBypass() {
        auto* n = node(selectedNode); if (!n || !nodeVisible(*n) || !isEffect(n->type)) return;
        const int slot = fxSlot(*n);
        if (slot < 0) return;
        const auto id = fxParameterId(size_t(slot), size_t(0));
        const auto* p = owner.parameters.getRawParameterValue(id);
        const bool enable = !(p ? p->load() > 0.5f : n->enabled);
        remember(1u << unsigned(slot)); n->enabled = enable;
        if (publish()) setParameter(id, enable ? 1.0f : 0.0f);
        rebuildInspector();
    }
    void showNodeMenu() {
        juce::PopupMenu menu;
        const auto* n = node(selectedNode);
        menu.addItem(1, "Delete node", n && n->type != NodeType::output);
        menu.addItem(2, "Toggle FX bypass", n && isEffect(n->type));
        juce::Component::SafePointer<RoutingPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&canvas).withMousePosition(), [safe](int result) {
            if (!safe) return;
            if (result == 1) safe->deleteSelection(); else if (result == 2) safe->toggleBypass();
        });
    }
    void showAddMenu() {
        juce::PopupMenu menu;
        int effects = 0; for (const auto& n : graph.nodes) if (isEffect(n.type)) ++effects;
        const bool room = graph.nodes.size() < size_t(RoutingExecutor::maxNodes);
        bool freeFxSlot = false;
        for (std::uint32_t id = 100; id <= 114; ++id) if (!node(id)) freeFxSlot = true;
        const bool effectRoom = room && effects < 16 && freeFxSlot;
        menu.addSectionHeader("Independent FX | shared automation slots");
        menu.addItem(1, "Chorale", effectRoom);
        menu.addItem(4, "Waveshaper", effectRoom);
        menu.addItem(5, "EQ8", effectRoom);
        menu.addItem(6, "Multiband | 2 / 3 bands", effectRoom);
        menu.addSeparator();
        menu.addItem(3, "Dry / Wet Mix", room);
        menu.addSeparator();
        menu.addItem(10, "Restore Lead source", room && !hasSource(-1));
        for (int i = 0; i < 8; ++i) if (voicePresent(i))
            menu.addItem(20 + i, "Restore Voice " + juce::String(i + 1) + " source", room && !hasSource(i));
        menu.addItem(40, "Restore main Chorale", room && !node(12) && effects < 16);
        juce::Component::SafePointer<RoutingPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&add).withMousePosition(), [safe](int result) { if (safe && result) safe->addNode(result); });
    }
    bool hasSource(int slot) const {
        for (const auto& n : graph.nodes)
            if ((slot < 0 && n.type == NodeType::lead) || (slot >= 0 && n.type == NodeType::voice && n.voiceSlot == slot)) return true;
        return false;
    }
    void addNode(int kind) {
        if (graph.nodes.size() >= size_t(RoutingExecutor::maxNodes)) return;
        RoutingNode n;
        if (kind == 1 || kind == 4 || kind == 5 || kind == 6) {
            n.type = kind == 1 ? NodeType::chorale : kind == 4 ? NodeType::waveshaper : kind == 5 ? NodeType::equalizer : NodeType::crossover;
            for (std::uint32_t id = 100; id <= 114; ++id) if (!node(id)) { n.id = id; break; }
            if (!n.id) return;
            int effects = 0;
            for (const auto& existing : graph.nodes) if (isEffect(existing.type)) ++effects;
            if (effects >= 16) return;
        } else if (kind == 3) {
            n.type = NodeType::mix;
            n.id = 200; while (node(n.id)) ++n.id;
        } else if (kind == 10) { n.id = 1; n.type = NodeType::lead; }
        else if (kind >= 20 && kind < 28) {
            if (!voicePresent(kind - 20)) return;
            n.id = std::uint32_t(kind - 18); n.type = NodeType::voice; n.voiceSlot = kind - 20;
        }
        else if (kind == 40) {
            if (node(12) || std::count_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& item) { return isEffect(item.type); }) >= 16) return;
            n.id = 12; n.type = NodeType::chorale; n.mainControls = true;
        }
        else return;
        if (node(n.id)) return;
        remember();
        positions[n.id] = toWorld(canvas.getLocalBounds().getCentre().toFloat());
        graph.nodes.push_back(n); selectedNode = n.id; selectedLink = -1;
        if (publish() && isEffect(n.type) && n.id >= 100 && n.id <= 114) {
            // Reset a newly allocated/reused machine only after its graph is accepted.
            const auto slot = size_t(routingFxSlot(n.id));
            setParameter(fxParameterId(slot, "On"), 1);
            setParameter(fxParameterId(slot, "Mix"), n.type == NodeType::chorale ? 25.0f : 100.0f);
            setParameter(fxParameterId(slot, "Motion"), 35);
            for (size_t field = 0; field < toneParametersPerSlot; ++field) {
                const auto id = parameterIds[toneParameterIndex(slot, field)];
                if (const auto* parameter = owner.parameters.getParameter(id))
                    setParameter(id, parameter->convertFrom0to1(parameter->getDefaultValue()));
            }
        }
        rebuildInspector();
    }
    void rebuildInspector() {
        validateSelection();
        updateModulationPanel();
        inspectorTitle.setEditable(false, node(selectedNode) != nullptr, false);
        toneViewport.setViewedComponent(nullptr, false);
        tonePanel.reset(); voiceControls.reset(); mixControl.reset(); fields.clear(); nodeValueLink.reset(); inspector.removeAllChildren();

        inspector.addAndMakeVisible(inspectorTitle); inspector.addAndMakeVisible(inspectorInfo);
        inspectorTitle.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        inspectorInfo.setColour(juce::Label::textColourId, juce::Colour(0xffaebaca));
        inspectorInfo.setFont(juce::Font(juce::FontOptions(12.0f)));
        nodeValue.onValueChange = nullptr; nodeValue.onDragStart = nullptr; nodeValue.onDragEnd = nullptr;
        nodeValue.setLookAndFeel(&nodeStyle);
        const auto* n = node(selectedNode);
        if (n && (n->type == NodeType::voice || n->type == NodeType::lead)) {
            const int slot = n->type == NodeType::lead ? -1 : int(n->voiceSlot);
            if (slot != lastSelectedVoiceSlot) {
                lastSelectedVoiceSlot = slot;
                auto saved = owner.getRoutingLayout().createCopy(); saved.setProperty("selectedVoiceSlot", slot, nullptr);
                owner.setRoutingLayout(saved); lastLayoutSignature = saved.toXmlString();
                if (onSelectVoice) onSelectVoice(slot);
            }
        }
        inspectorTitle.setText(n ? title(*n) : selectedLink >= 0 ? "AUDIO CONNECTION" : "ROUTING INSPECTOR", juce::dontSendNotification);
        juce::String info = "Select a machine to edit its controls.\n\nInputs accept multiple voices with automatic merge compensation. Each output (each Multiband band) has one outgoing cable. Dragging it to another input replaces its existing links; dropping on empty space disconnects them. Saved fanout remains until that output is edited. A click without dragging leaves wiring unchanged.\n\nDrag an occupied input end to reconnect; drop it on empty space to disconnect that cable. Ctrl/Command-drag an output to move a cable's source. Wheel zooms at the cursor; Alt/middle-drag pans.\n\nAbsent voices and the untouched bypassed main Chorale are hidden without changing routing. Muted voices stay visible. Unconnected inputs receive silence; rejected edits retain accepted routing.";
        if (n) {
            info = "Node ID " + juce::String(n->id);
            if (isEffect(n->type)) {
                const int slot = fxSlot(*n);
                info += "\nIndependent FX slot " + juce::String(slot);
                if (slot >= 0) {
                    fxOn.setModulationOwner(owner);
                                        fxOn.setButtonText("FX enabled"); fxOn.setClickingTogglesState(true); inspector.addAndMakeVisible(fxOn);
                    fxOn.setComponentID(fxParameterId(size_t(slot), size_t(0)));
                    const auto* on = owner.parameters.getRawParameterValue(fxParameterId(size_t(slot), size_t(0)));
                    fxOn.setToggleState(on ? on->load() > 0.5f : n->enabled, juce::dontSendNotification);
                    fxOn.onClick = [this] { toggleBypass(); };
                    if (n->type == NodeType::chorale) {
                        for (int f = 1; f <= 2; ++f) {
                            fields.push_back(std::make_unique<ParameterKnob>(owner, fxParameterId(size_t(slot), f), f == 1 ? "Mix" : "Motion", "%"));
                            inspector.addAndMakeVisible(*fields.back());
                        }
                        info += "\nOne independent machine; routing chooses its input.\nNew Chorale slots start On / 25% Mix / 35% Motion.";
                    } else if (slot > 0) {
                        tonePanel = std::make_unique<ToneEffectPanel>(owner, size_t(slot), n->type);
                        toneViewport.setScrollBarsShown(true, false);
                        toneViewport.setViewedComponent(tonePanel.get(), false);
                        inspector.addAndMakeVisible(toneViewport);
                        info = "Slot " + juce::String(slot) + " | independent host automation";
                        if (n->type == NodeType::crossover)
                            info += "\n2 bands: #0 Low / #1 High. 3 bands: #0 Low / #1 Mid / #2 High.\nStored #2 cables are silent in 2-band mode; #1 becomes High.\nBypass: full-band on #0 only; all other outputs silent.";
                    }
                } else info += "\nNo matching host automation slot.";
            } else if (n->type == NodeType::mix) {
                inspector.addAndMakeVisible(nodeValue);
                nodeValue.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                nodeValue.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 24);
                nodeValue.setScrollWheelEnabled(false);
                if (n->type == NodeType::mix && n->mainControls) {
                    inspector.removeChildComponent(&nodeValue);
                    mixControl = std::make_unique<ParameterKnob>(owner, "double", "Lead / Stack", "%");
                    inspector.addAndMakeVisible(*mixControl);
                    info += "\nLead / Stack balance\nLead pin 0 / Stack pin 1";
                } else if (localMixParameterId(n->id).isNotEmpty()) {
                    inspector.removeChildComponent(&nodeValue);
                    mixControl = std::make_unique<ParameterKnob>(owner, localMixParameterId(n->id), "Dry / Wet", "%");
                    inspector.addAndMakeVisible(*mixControl);
                    info += "\nAutomatable / modulatable Dry pin 0 / Wet pin 1";
                } else {
                    nodeValue.setDoubleClickReturnValue(true, 50);
                    nodeValue.setVelocityModeParameters(1.0, 1, 0.0, true,
                        static_cast<juce::ModifierKeys::Flags>(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::commandModifier));
                    nodeValue.setRange(0, 100, 0.1);
                    nodeValue.setTooltip("Graph-local mix; not a host parameter or modulation target. Main Lead / Stack is modulatable.");
                    nodeValue.setValue(n->mix * 100, juce::dontSendNotification);
                    info += "\nMix (%)\nDry pin 0 / Wet pin 1. Each pin accepts multiple sources.";
                    nodeValue.onDragStart = [this] { remember(); inspectorDragging = true; };
                    nodeValue.onDragEnd = [this] { inspectorDragging = false; };
                    nodeValue.onValueChange = [this] {
                        if (!inspectorDragging) remember();
                        if (auto* current = node(selectedNode)) {
                            current->mix = float(nodeValue.getValue() / 100);
                            publish();
                        }
                    };
                }
            } else if (n->type == NodeType::voice || n->type == NodeType::lead) {
                            voiceControls = std::make_unique<VoiceControls>(owner, n->type == NodeType::lead ? -1 : int(n->voiceSlot));
                            toneViewport.setViewedComponent(voiceControls.get(), false); toneViewport.setScrollBarsShown(true, false);
                            inspector.addAndMakeVisible(toneViewport);
                            info += "\nShared Stack controls / stable host parameters";
                        }
            else if (n->type == NodeType::output) {
                            mixControl = std::make_unique<ParameterKnob>(owner, "outputGain", "Output trim", "dB");
                            inspector.addAndMakeVisible(*mixControl);
                            info += "\nFinal output trim; meters remain in the main editor. Exactly one Output is required.";
                        }
            else info += "\nProcessed lead source. Connecting this output elsewhere moves its cable; inputs accept multiple sources.";
        } else if (selectedLink >= 0 && selectedLink < int(graph.links.size())) {
            DisplayLink item;
            if (!displayLink(selectedLink, item)) return;
            inspector.addAndMakeVisible(disconnect);
            const auto& l = item.link;
            const auto* source = node(l.source);
            const auto* destination = node(l.destination);
            info = (source ? title(*source) : juce::String(l.source)) + " / "
                + (source ? outputName(*source, l.sourcePin) : juce::String(l.sourcePin))
                + " -> " + (destination ? title(*destination) : juce::String(l.destination))
                + "\nOutput #" + juce::String(l.sourcePin) + " -> Input #" + juce::String(l.inputPin)
                + "\n\nDrag the input end to reconnect or disconnect this cable. Drag the output to move its destination or drop on empty space to disconnect all its links. Ctrl/Command-drag the output to move this cable's source. Clicks leave wiring unchanged.";
            if (item.foldedEnd >= 0)
                info += "\n\nTransparent bypass: this cable represents stored Mix 11 -> main Chorale 12 -> Output 13. Chorale is Off via its host parameter. Disconnect removes BOTH chain links; rewiring replaces both with one real direct cable. Enabling Chorale reveals the stored chain if unchanged.";
            if (inactiveSource(l)) info += "\n\nINACTIVE / SILENT: this cable remains stored. Re-enable the splitter or return to 3 bands to restore its source port.";
        }
        inspectorInfo.setText(info, juce::dontSendNotification);
        inspectorInfo.setJustificationType(juce::Justification::topLeft);
        setNeon(neon); layoutInspector(); updateButtons(); detail::passiveControl(inspector);
    }
    void layoutInspector() {
        inspectorTitle.setBounds(0, 0, inspector.getWidth(), 30);
                if (voiceControls) {
                    const int top = 36;
                    toneViewport.setBounds(0, top, inspector.getWidth(), juce::jmax(0, inspector.getHeight() - top));
                    auto* content = voiceControls.get();
                    content->setSize(juce::jmax(1, inspector.getWidth() - toneViewport.getScrollBarThickness()),
                                     390);
                    return;
                }
        fxOn.setBounds(4, 34, 160, 26);
        disconnect.setBounds(4, 34, 130, 28);
        if (tonePanel) {
            const bool crossover = node(selectedNode) && node(selectedNode)->type == NodeType::crossover;
            const int footerHeight = crossover ? 79 : 28;
            const int available = juce::jmax(0, inspector.getHeight() - 68 - footerHeight);
            toneViewport.setBounds(0, 68, inspector.getWidth(), available);
            tonePanel->setBounds(0, 0, juce::jmax(1, inspector.getWidth() - toneViewport.getScrollBarThickness()), ToneEffectPanel::preferredHeight);
            inspectorInfo.setFont(juce::Font(juce::FontOptions(11.0f)));
            inspectorInfo.setBounds(0, 68 + available + 4, inspector.getWidth(), footerHeight - 4);
            return;
        }
        int y = 76;
        const int columns = juce::jmax(1, juce::jmin(3, int(fields.size())));
        const int fieldWidth = inspector.getWidth() / columns;
        for (size_t i = 0; i < fields.size(); ++i)
            fields[i]->setBounds(int(i % size_t(columns)) * fieldWidth, y + int(i / size_t(columns)) * 104, juce::jmax(0, fieldWidth - 4), 100);
        if (!fields.empty()) y += int((fields.size() + 2) / 3) * 104;
        nodeValue.setBounds(0, 40, 140, 110);
        if (mixControl) mixControl->setBounds(0, 40, 140, 110);
        const int infoY = fields.empty() ? (nodeValue.getParentComponent() == &inspector || mixControl ? 160 : selectedLink >= 0 ? 76 : 40) : y + 12;
        inspectorInfo.setBounds(0, infoY, inspector.getWidth(), juce::jmax(20, inspector.getHeight() - infoY));
    }
    juce::String portSignature() const {
        juce::String result;
        for (const auto& n : graph.nodes)
            result += juce::String(n.id) + ":" + juce::String(outputPins(n)) + ":"
                + juce::String(int(nodeVisible(n))) + ":" + juce::String(int(isEffect(n.type) && effectEnabled(n))) + ";";
        result += "fold:" + juce::String(foldedChain().first);
        return result;
    }
    void timerCallback() override {
        refreshAssignmentBanner();
        // A divider gesture changes bounds only; defer external rebuilds until release.
        if (resizingInspector || resizingModulation) return;
        rebuildModulatorStripIfChanged();
        const auto modulators = owner.getModulationState().modulators;
        const auto snapshot = owner.getModulationSnapshot();
        for (size_t i = 0; i < modulatorButtons.size() && i < modulators.size(); ++i) {
            auto& tile = *modulatorButtons[i];
            tile.model = modulators[i];
            tile.setToggleState(tile.model.id == selectedModulationId(), juce::dontSendNotification);
            if (snapshot.consistent) for (const auto& m : snapshot.modulators) if (m.id == tile.model.id) {
                tile.phase = m.phase;
                tile.history.push_back(m.output);
                if (tile.history.size() > 64) tile.history.erase(tile.history.begin());
            }
            tile.repaint();
        }
        modulationui::refreshWithSelection(inspector, owner, owner.getModulationSnapshot(), owner.getModulationState(), owner.modulationAssignmentId());
        // Host presence, bank, band-count and On changes apply even during a
        // cable gesture. Cancel it rather than dropping onto a now-hidden pin.
        const auto ports = portSignature();
        if (ports != lastPortSignature) {
            lastPortSignature = ports;
            connecting = 0; rewiringLink = -1; rewiringSource = false;
            const auto previousNode = selectedNode;
            const int previousLink = selectedLink;
            validateSelection();
            if (draggingNode && (!node(draggingNode) || !nodeVisible(*node(draggingNode)))) draggingNode = 0;
            if (previousNode != selectedNode || previousLink != selectedLink || selectedLink >= 0)
                rebuildInspector();
            updateButtons(); canvas.repaint();
        }
        if (draggingNode || connecting || rewiringLink >= 0 || inspectorDragging) return;
        auto latest = owner.getRoutingGraph();
        const auto next = signature(latest);
        const auto savedLayout = owner.getRoutingLayout();
        const auto layoutSignature = savedLayout.toXmlString();
        if (next != lastSignature || layoutSignature != lastLayoutSignature) {
            graph = std::move(latest); lastSignature = next;
            undoStates.clear(); redoStates.clear(); selectedLink = -1;
            loadLayout(savedLayout); lastLayoutSignature = layoutSignature;
            ensurePositions(); error.clear(); showStatus(); rebuildInspector();
            lastPortSignature = portSignature();
        }
        if (const auto* n = node(selectedNode); n && isEffect(n->type) && fxSlot(*n) >= 0) {
            const auto* on = owner.parameters.getRawParameterValue(fxParameterId(size_t(fxSlot(*n)), size_t(0)));
            fxOn.setToggleState(on ? on->load() > 0.5f : n->enabled, juce::dontSendNotification);
        }
        validateSelection();
        updateButtons();
        // Port counts and bypass come from host parameters, not serialized nodes.
        canvas.repaint();
    }
    Processor& owner;
    Graph graph;
    std::map<std::uint32_t, juce::Point<float>> positions;
    std::map<std::uint32_t, juce::String> names;
    float zoom = 0.72f;
    juce::Point<float> pan{20, 18}, pointer, dragStart, startPan, startNode;
    std::uint32_t selectedNode = 0, draggingNode = 0, connecting = 0;
    int selectedLink = -1, rewiringLink = -1, connectingPin = 0;
    bool rewiringSource = false;
    bool neon = false, panning = false, inspectorDragging = false;
    juce::String error, lastSignature, lastLayoutSignature, lastPortSignature;
    std::vector<EditState> undoStates, redoStates;
    EditState dragBefore;
    juce::TextButton add, undo, redo, remove, bypass, fit, zoomOut, zoomIn, disconnect;
        modulationui::Button fxOn;
    juce::Component inspector;
    InspectorDivider inspectorDivider;
    int desiredInspectorWidth = 320;
    bool resizingInspector = false;
    juce::Label inspectorTitle, inspectorInfo, status;
    modulationui::RotaryStyle nodeStyle;
    juce::Slider nodeValue;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> nodeValueLink;

    std::vector<std::unique_ptr<ParameterKnob>> fields;
        std::unique_ptr<ParameterKnob> mixControl;
        std::unique_ptr<VoiceControls> voiceControls;
        ModulationPanel modulationPanel;
        ModulationDivider modulationDivider;
        int desiredModulationHeight = ModulationPanel::preferredHeight;
        bool resizingModulation = false;
        uint32_t selectedModulator = 0;
            int lastSelectedVoiceSlot = -2;
        juce::String modulatorSignature;
        juce::Component modulatorStrip;
        juce::Viewport modulatorViewport;
        juce::TextButton addModulator, assignmentBanner;
            juce::BubbleMessageComponent revealBubble;
        std::vector<std::unique_ptr<ModulatorTile>> modulatorButtons;
    std::unique_ptr<ToneEffectPanel> tonePanel;
    juce::Viewport toneViewport;
};

class RoutingWindow final : public juce::DocumentWindow {
public:
    explicit RoutingWindow(Processor& owner)
        : DocumentWindow("Vocal Stack / Routing", juce::Colour(0xff171d25), juce::DocumentWindow::closeButton), panel(owner) {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&panel, true);
        setResizable(true, false);
        setResizeLimits(1000, 740, 1800, 1200);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        centreWithSize(1280, 800);
    }
    ~RoutingWindow() override { clearContentComponent(); }
    void closeButtonPressed() override { setVisible(false); }
    void setNeon(bool value) { panel.setNeon(value); }
private:
    RoutingPanel panel;
    juce::TooltipWindow tips{this, 530};
};
} // namespace vocalstack
