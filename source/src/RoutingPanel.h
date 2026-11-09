#pragma once

#include "DoublerPanel.h"
#include <map>

namespace vocalstack {

// Header-only presentation. The processor owns validation, drafts and publication.
class RoutingPanel final : public juce::Component, private juce::Timer {
    struct EditState {
        Graph graph;
        juce::ValueTree layout;
        std::array<std::array<float, 3>, 16> fxValues{};
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
            if (e.mods.isCtrlDown() || e.mods.isCommandDown()) panel.zoomAt(e.position, wheel.deltaY > 0 ? 1.12f : 1.0f / 1.12f);
            else { panel.pan += juce::Point<float>(wheel.deltaX * 90, wheel.deltaY * 90); panel.saveLayout(); repaint(); }
        }
    private:
        RoutingPanel& panel;
    } canvas;
public:
    explicit RoutingPanel(Processor& processor) : canvas(*this), owner(processor) {
        setSize(1120, 700);
        addAndMakeVisible(canvas);
        addAndMakeVisible(inspector);
        addAndMakeVisible(status);
        status.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        status.setFont(juce::Font(juce::FontOptions(12.0f)));
        for (auto* b : {&add, &undo, &redo, &disconnect, &remove, &bypass, &fit, &zoomOut, &zoomIn}) addAndMakeVisible(*b);
        add.setButtonText("+ Node"); undo.setButtonText("Undo"); redo.setButtonText("Redo");
        disconnect.setButtonText("Disconnect"); remove.setButtonText("Delete node"); bypass.setButtonText("Bypass FX");
        fit.setButtonText("Fit"); zoomOut.setButtonText("-"); zoomIn.setButtonText("+");
        add.onClick = [this] { showAddMenu(); };
        undo.onClick = [this] { stepHistory(undoStates, redoStates); };
        redo.onClick = [this] { stepHistory(redoStates, undoStates); };
        disconnect.onClick = [this] {
            if (selectedLink < 0 || selectedLink >= int(graph.links.size())) return;
            remember(); graph.links.erase(graph.links.begin() + selectedLink); selectedLink = -1; publish();
        };
        remove.onClick = [this] { deleteSelection(); };
        bypass.onClick = [this] { toggleBypass(); };
        fit.onClick = [this] { fitGraph(); };
        zoomOut.onClick = [this] { zoomAt(canvas.getLocalBounds().getCentre().toFloat(), 1.0f / 1.2f); };
        zoomIn.onClick = [this] { zoomAt(canvas.getLocalBounds().getCentre().toFloat(), 1.2f); };
        disconnect.setTooltip("Select a cable, then disconnect. Incomplete drafts remain visible; the previous valid graph keeps running");
        remove.setTooltip("Delete the selected node and its cables. Output is retained");
        bypass.setTooltip("Bypass the selected Chorale without changing its wiring");
        graph = owner.getRoutingGraph();
        loadLayout(owner.getRoutingLayout());
        ensurePositions();
        validateDraft();
        rebuildInspector();
        lastSignature = signature(graph);
        lastLayoutSignature = owner.getRoutingLayout().toXmlString();
        setNeon(owner.isFlash());
        detail::passiveControl(*this);
        startTimerHz(5);
    }
    ~RoutingPanel() override { nodeValue.setLookAndFeel(nullptr); }
    void setNeon(bool value) {
        neon = value;
        for (auto* b : {&add, &undo, &redo, &disconnect, &remove, &bypass, &fit, &zoomOut, &zoomIn, &fxOn}) {
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(neon ? 0xff202e47 : 0xff3b413a));
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(neon ? 0xff64f5e3 : 0xfff5a24b));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e9ee));
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171c));
        }
        for (auto& field : fields) field->setNeon(neon);
        detail::styleSlider(nodeValue, neon);
        repaint(); canvas.repaint();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(neon ? 0xff090d17 : 0xff292b2a));
        g.setColour(juce::Colour(neon ? 0xff2b3a53 : 0xff515850));
        g.drawVerticalLine(inspector.getX() - 8, 46.0f, float(getHeight() - 36));
        g.setColour(juce::Colour(0xffaebaca));
        g.setFont(juce::Font(juce::FontOptions(11.5f)));
        g.drawText("Drag output pin to input  |  Drag blank space to pan  |  Ctrl+wheel to zoom  |  Select cable to disconnect", 12, 43, getWidth() - 24, 22, juce::Justification::centredLeft);
    }
    void resized() override {
        auto toolbar = getLocalBounds().reduced(12).removeFromTop(28);
        for (auto* b : {&add, &undo, &redo, &disconnect, &remove, &bypass, &fit, &zoomOut, &zoomIn}) {
            const int width = b == &zoomIn || b == &zoomOut ? 30 : b == &disconnect || b == &remove ? 102 : b == &bypass ? 90 : 66;
            b->setBounds(toolbar.removeFromLeft(width)); toolbar.removeFromLeft(6);
        }
        canvas.setBounds(12, 70, juce::jmax(100, getWidth() - 278), juce::jmax(100, getHeight() - 112));
        inspector.setBounds(getWidth() - 250, 76, 238, getHeight() - 118);
        status.setBounds(8, getHeight() - 33, getWidth() - 16, 28);
        layoutInspector();
    }
private:
    static juce::String signature(const Graph& g) {
        juce::String s;
        for (const auto& n : g.nodes)
            s += juce::String(n.id) + ":" + juce::String(int(n.type)) + ":" + juce::String(n.voiceSlot) + ":"
                + juce::String(int(n.enabled)) + ":" + juce::String(n.mix, 6) + ":" + juce::String(n.motion, 6)
                + ":" + juce::String(int(n.mainControls)) + ":" + juce::String(n.referencePower, 6) + ":" + juce::String(n.latencySamples) + ";";
        s += "|";
        for (const auto& l : g.links) s += juce::String(l.source) + ">" + juce::String(l.destination) + ":" + juce::String(l.inputPin) + ";";
        return s;
    }
    static juce::String title(const RoutingNode& n) {
        switch (n.type) {
        case NodeType::lead: return "LEAD";
        case NodeType::voice: return "VOICE " + juce::String(n.voiceSlot + 1);
        case NodeType::chorale: return n.mainControls ? "CHORALE / MAIN" : "CHORALE " + juce::String(int(n.id) - 98);
        case NodeType::sum: return "COMPENSATED SUM";
        case NodeType::mix: return n.mainControls ? "MAIN DRY / WET" : "DRY / WET MIX";
        case NodeType::output: return "OUTPUT";
        }
        return {};
    }
    static int inputs(const RoutingNode& n) {
        return n.type == NodeType::sum ? 16 : n.type == NodeType::mix ? 2
            : n.type == NodeType::chorale || n.type == NodeType::output ? 1 : 0;
    }
    RoutingNode* node(std::uint32_t id) {
        for (auto& n : graph.nodes) if (n.id == id) return &n;
        return nullptr;
    }
    int fxSlot(const RoutingNode& n) const {
        return n.type == NodeType::chorale ? routingFxSlot(n.id) : -1;
    }
    juce::Rectangle<float> nodeBounds(const RoutingNode& n) const {
        const auto it = positions.find(n.id);
        const auto p = it == positions.end() ? juce::Point<float>() : it->second;
        return {p.x, p.y, 156.0f, float(juce::jmax(62, 42 + inputs(n) * 13))};
    }
    juce::Point<float> pinPoint(const RoutingNode& n, int pin, bool output) const {
        const auto b = nodeBounds(n);
        return output ? juce::Point<float>(b.getRight(), b.getY() + 40) : juce::Point<float>(b.getX(), b.getY() + 40 + float(pin * 13));
    }
    juce::Point<float> toScreen(juce::Point<float> p) const { return p * zoom + pan; }
    juce::Point<float> toWorld(juce::Point<float> p) const { return (p - pan) / zoom; }
    juce::Path cable(juce::Point<float> a, juce::Point<float> b) const {
        a = toScreen(a); b = toScreen(b);
        const float bend = juce::jmax(35.0f, std::abs(b.x - a.x) * 0.45f);
        juce::Path p; p.startNewSubPath(a); p.cubicTo(a.x + bend, a.y, b.x - bend, b.y, b.x, b.y); return p;
    }
    bool connected(std::uint32_t id, int pin) const {
        for (const auto& l : graph.links) if (l.destination == id && l.inputPin == pin) return true;
        return false;
    }
    void paintCanvas(juce::Graphics& g) {
        g.fillAll(juce::Colour(neon ? 0xff0c1220 : 0xff1c201e));
        const auto accent = juce::Colour(neon ? 0xff64f5e3 : 0xfff5a24b);
        g.setColour(juce::Colour(0xff35414b));
        const float step = 32 * zoom;
        for (float x = std::fmod(pan.x, step); x < float(canvas.getWidth()); x += step)
            for (float y = std::fmod(pan.y, step); y < float(canvas.getHeight()); y += step) g.fillRect(x, y, 1.0f, 1.0f);
        for (size_t i = 0; i < graph.links.size(); ++i) {
            const auto& l = graph.links[i];
            auto* a = node(l.source); auto* b = node(l.destination);
            if (!a || !b) continue;
            g.setColour(int(i) == selectedLink ? juce::Colour(0xffffd477) : valid ? accent.withAlpha(0.7f) : juce::Colour(0xfff58680));
            g.strokePath(cable(pinPoint(*a, 0, true), pinPoint(*b, l.inputPin, false)), juce::PathStrokeType(int(i) == selectedLink ? 3.5f : 2.0f));
        }
        if (connecting) if (auto* n = node(connecting)) {
            g.setColour(accent); g.strokePath(cable(pinPoint(*n, 0, true), toWorld(pointer)), juce::PathStrokeType(2.0f));
        }
        for (const auto& n : graph.nodes) {
            const auto b = nodeBounds(n);
            const auto screen = juce::Rectangle<float>(toScreen(b.getTopLeft()), toScreen(b.getBottomRight()));
            g.setColour(juce::Colour(neon ? 0xff172239 : 0xff303730));
            g.fillRoundedRectangle(screen, 4 * zoom);
            g.setColour(n.id == selectedNode ? accent : juce::Colour(neon ? 0xff465c7c : 0xff65705e));
            g.drawRoundedRectangle(screen, 4 * zoom, n.id == selectedNode ? 2.0f : 1.0f);
            g.setFont(juce::Font(juce::FontOptions(juce::jmax(9.0f, 11.5f * zoom))));
            g.setColour(juce::Colour(0xffe4e9ee));
            auto heading = screen.toNearestInt().reduced(8, 0).withHeight(juce::roundToInt(28 * zoom));
            g.drawText(title(n), heading, juce::Justification::centredLeft);
            for (int pin = 0; pin < inputs(n); ++pin) {
                const auto p = toScreen(pinPoint(n, pin, false));
                const bool wired = connected(n.id, pin);
                const bool required = n.type != NodeType::sum;
                g.setColour(wired ? accent : required ? juce::Colour(0xfff58680) : juce::Colour(0xff68788b));
                g.fillEllipse(p.x - 4, p.y - 4, 8, 8);
                g.setColour(juce::Colour(0xffbcc8d8));
                const auto name = n.type == NodeType::mix ? (pin == 0 ? "Dry" : "Wet") : n.type == NodeType::sum ? juce::String(pin + 1) : "In";
                g.drawText(name, int(p.x + 9), int(p.y - 7), 66, 14, juce::Justification::centredLeft);
            }
            if (n.type != NodeType::output) {
                const auto p = toScreen(pinPoint(n, 0, true));
                g.setColour(accent); g.fillEllipse(p.x - 4, p.y - 4, 8, 8);
            }
            if (n.type == NodeType::chorale) {
                const int slot = fxSlot(n);
                const auto* parameter = slot < 0 ? nullptr : owner.parameters.getRawParameterValue(fxParameterId(size_t(slot), 0));
                const bool on = parameter ? parameter->load() > 0.5f : n.enabled;
                g.setColour(on ? accent : juce::Colour(0xffa6aeb9));
                g.drawText(on ? "ON" : "BYPASSED", screen.toNearestInt().reduced(8, 0).withTrimmedTop(juce::roundToInt(30 * zoom)), juce::Justification::centredRight);
            }
        }
    }
    void canvasDown(const juce::MouseEvent& e) {
        pointer = e.position; dragStart = e.position; startPan = pan;
        draggingNode = 0; connecting = 0; panning = false;
        const auto world = toWorld(e.position);
        if (e.mods.isMiddleButtonDown() || e.mods.isAltDown()) { panning = true; return; }
        for (auto it = graph.nodes.rbegin(); it != graph.nodes.rend(); ++it) {
            if (it->type != NodeType::output && toScreen(pinPoint(*it, 0, true)).getDistanceFrom(e.position) < 10) {
                connecting = it->id; selectedNode = it->id; selectedLink = -1; rebuildInspector(); canvas.repaint(); return;
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
            const auto& l = graph.links[size_t(i)]; auto* a = node(l.source); auto* b = node(l.destination);
            if (!a || !b) continue;
            juce::Path hit;
            juce::PathStrokeType(12.0f).createStrokedPath(hit, cable(pinPoint(*a, 0, true), pinPoint(*b, l.inputPin, false)));
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
        if (connecting) {
            bool found = false;
            for (const auto& n : graph.nodes) {
                for (int pin = 0; pin < inputs(n); ++pin) {
                    if (toScreen(pinPoint(n, pin, false)).getDistanceFrom(e.position) < 12) {
                        remember(); graph.links.push_back({connecting, n.id, pin}); found = true; break;
                    }
                }
                if (found) break;
            }
            if (found) publish();
        }
        if (draggingNode && e.position.getDistanceFrom(dragStart) > 2) {
            undoStates.push_back(dragBefore); limitHistory(); redoStates.clear(); saveLayout(); updateButtons();
        }
        if (panning) saveLayout();
        connecting = 0; draggingNode = 0; panning = false; canvas.repaint();
    }
    void zoomAt(juce::Point<float> point, float factor) {
        const auto world = toWorld(point);
        zoom = juce::jlimit(0.35f, 1.8f, zoom * factor);
        pan = point - world * zoom; saveLayout(); canvas.repaint();
    }
    void fitGraph() {
        juce::Rectangle<float> bounds;
        for (const auto& n : graph.nodes) bounds = bounds.getUnion(nodeBounds(n));
        if (bounds.isEmpty()) return;
        zoom = juce::jlimit(0.35f, 1.5f, juce::jmin(float(canvas.getWidth() - 40) / bounds.getWidth(), float(canvas.getHeight() - 40) / bounds.getHeight()));
        pan = canvas.getLocalBounds().getCentre().toFloat() - bounds.getCentre() * zoom;
        saveLayout(); canvas.repaint();
    }
    juce::ValueTree layout() const {
        juce::ValueTree tree("RoutingLayout");
        tree.setProperty("zoom", zoom, nullptr); tree.setProperty("panX", pan.x, nullptr); tree.setProperty("panY", pan.y, nullptr);
        for (const auto& p : positions) {
            juce::ValueTree child("NODE"); child.setProperty("id", int(p.first), nullptr);
            child.setProperty("x", p.second.x, nullptr); child.setProperty("y", p.second.y, nullptr); tree.addChild(child, -1, nullptr);
        }
        return tree;
    }
    void loadLayout(const juce::ValueTree& tree) {
        positions.clear();
        zoom = juce::jlimit(0.35f, 1.8f, float(tree.getProperty("zoom", 0.72f)));
        pan = {float(tree.getProperty("panX", 20.0f)), float(tree.getProperty("panY", 18.0f))};
        if (!std::isfinite(zoom)) zoom = 0.72f;
        if (!std::isfinite(pan.x) || !std::isfinite(pan.y)) pan = {20, 18};
        for (const auto& c : tree) {
            const int id = int(c.getProperty("id"));
            const float x = float(c.getProperty("x")), y = float(c.getProperty("y"));
            if (id > 0 && std::isfinite(x) && std::isfinite(y)) positions[std::uint32_t(id)] = {x, y};
        }
    }
    void ensurePositions() {
        int extra = 0;
        for (const auto& n : graph.nodes) if (positions.count(n.id) == 0) {
            juce::Point<float> p;
            switch (n.type) {
            case NodeType::lead: p = {24, 20}; break;
            case NodeType::voice: p = {24, 100.0f + float(n.voiceSlot * 70)}; break;
            case NodeType::sum: p = {260, 180}; break;
            case NodeType::mix: p = {490, 30}; break;
            case NodeType::chorale: p = {710, 30}; break;
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
        EditState result{graph, layout(), {}};
        for (size_t slot = 0; slot < 16; ++slot)
            for (size_t field = 0; field < 3; ++field)
                if (const auto* p = owner.parameters.getRawParameterValue(fxParameterId(slot, field))) result.fxValues[slot][field] = p->load();
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
        graph = s.graph; loadLayout(s.layout); selectedNode = 0; selectedLink = -1;
        saveLayout(); publish();
        for (size_t slot = 0; slot < 16; ++slot) {
            const auto id = slot == 0 ? 12u : unsigned(slot + 99);
            const auto findFx = [id](const Graph& g) -> const RoutingNode* {
                for (const auto& n : g.nodes) if (n.id == id && n.type == NodeType::chorale) return &n;
                return nullptr;
            };
            const auto* before = findFx(current.graph); const auto* after = findFx(s.graph);
            const bool lifecycleChange = (!before != !after);
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
        }
        rebuildInspector();
    }
    void validateDraft() {
        const auto result = RoutingExecutor::validate(graph);
        valid = result.valid; error = juce::String(result.message);
        showStatus();
    }
    void showStatus() {
        status.setText(valid ? "VALID ROUTING  /  Audio follows this graph.  Sources remain stable even when a voice card is removed."
                             : "DRAFT NOT RUNNING  /  " + error + "  /  Last valid audio graph continues.", juce::dontSendNotification);
        status.setTooltip(status.getText());
        status.setColour(juce::Label::textColourId, juce::Colour(valid ? 0xffa9dec2 : 0xffff9f91));
        updateButtons();
    }
    void publish() {
        ensurePositions();
        juce::String problem;
        valid = owner.setRoutingGraph(graph, problem); error = problem;
        lastSignature = signature(graph);
        saveLayout(); showStatus(); canvas.repaint();
    }
    void updateButtons() {
        undo.setEnabled(!undoStates.empty()); redo.setEnabled(!redoStates.empty());
        disconnect.setEnabled(selectedLink >= 0 && selectedLink < int(graph.links.size()));
        const auto* n = node(selectedNode);
        remove.setEnabled(n && n->type != NodeType::output);
        bypass.setEnabled(n && n->type == NodeType::chorale);
    }
    void deleteSelection() {
        const auto* n = node(selectedNode); if (!n || n->type == NodeType::output) return;
        remember(); const auto id = selectedNode;
        graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [id](const auto& l) { return l.source == id || l.destination == id; }), graph.links.end());
        graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(), [id](const auto& item) { return item.id == id; }), graph.nodes.end());
        positions.erase(id); selectedNode = 0; selectedLink = -1; publish(); rebuildInspector();
    }
    void setParameter(const juce::String& id, float value) {
        if (auto* p = owner.parameters.getParameter(id)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(value)); p->endChangeGesture();
        }
    }
    void toggleBypass() {
        auto* n = node(selectedNode); if (!n || n->type != NodeType::chorale) return;
        const int slot = fxSlot(*n);
        if (slot < 0) return;
        const auto id = fxParameterId(size_t(slot), size_t(0));
        const auto* p = owner.parameters.getRawParameterValue(id);
        const bool enable = !(p ? p->load() > 0.5f : n->enabled);
        remember(1u << unsigned(slot)); n->enabled = enable;
        publish(); setParameter(id, enable ? 1.0f : 0.0f); rebuildInspector();
    }
    void showNodeMenu() {
        juce::PopupMenu menu;
        const auto* n = node(selectedNode);
        menu.addItem(1, "Delete node", n && n->type != NodeType::output);
        menu.addItem(2, "Toggle node bypass", n && n->type == NodeType::chorale);
        juce::Component::SafePointer<RoutingPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&canvas), [safe](int result) {
            if (!safe) return;
            if (result == 1) safe->deleteSelection(); else if (result == 2) safe->toggleBypass();
        });
    }
    void showAddMenu() {
        juce::PopupMenu menu;
        int effects = 0; for (const auto& n : graph.nodes) if (n.type == NodeType::chorale) ++effects;
        const bool room = graph.nodes.size() < size_t(RoutingExecutor::maxNodes);
        bool freeFxSlot = false;
        for (std::uint32_t id = 100; id <= 114; ++id) if (!node(id)) freeFxSlot = true;
        menu.addItem(1, "Chorale (independent FX slot)", room && effects < 16 && freeFxSlot);
        menu.addItem(2, "Compensated Sum", room); menu.addItem(3, "Dry / Wet Mix", room);
        menu.addSeparator();
        menu.addItem(10, "Restore Lead source", room && !hasSource(-1));
        for (int i = 0; i < 8; ++i) menu.addItem(20 + i, "Restore Voice " + juce::String(i + 1) + " source", room && !hasSource(i));
        menu.addItem(40, "Restore main Chorale", room && !node(12) && effects < 16);
        juce::Component::SafePointer<RoutingPanel> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&add), [safe](int result) { if (safe && result) safe->addNode(result); });
    }
    bool hasSource(int slot) const {
        for (const auto& n : graph.nodes)
            if ((slot < 0 && n.type == NodeType::lead) || (slot >= 0 && n.type == NodeType::voice && n.voiceSlot == slot)) return true;
        return false;
    }
    void addNode(int kind) {
        if (graph.nodes.size() >= size_t(RoutingExecutor::maxNodes)) return;
        RoutingNode n;
        if (kind == 1) {
            n.type = NodeType::chorale;
            for (std::uint32_t id = 100; id <= 114; ++id) if (!node(id)) { n.id = id; break; }
            if (!n.id) return;
            int effects = 0;
            for (const auto& existing : graph.nodes) if (existing.type == NodeType::chorale) ++effects;
            if (effects >= 16) return;
        } else if (kind == 2 || kind == 3) {
            n.type = kind == 2 ? NodeType::sum : NodeType::mix;
            n.id = 200; while (node(n.id)) ++n.id;
        } else if (kind == 10) { n.id = 1; n.type = NodeType::lead; }
        else if (kind >= 20 && kind < 28) { n.id = std::uint32_t(kind - 18); n.type = NodeType::voice; n.voiceSlot = kind - 20; }
        else if (kind == 40) { n.id = 12; n.type = NodeType::chorale; n.mainControls = true; }
        else return;
        remember();
        if (kind == 1) {
            // Explicitly initialise reused stable automation slots, not a hidden shared machine.
            const auto slot = size_t(int(n.id) - 99);
            setParameter(fxParameterId(slot, 0), 1); setParameter(fxParameterId(slot, 1), 25); setParameter(fxParameterId(slot, 2), 35);
        }
        positions[n.id] = toWorld(canvas.getLocalBounds().getCentre().toFloat());
        graph.nodes.push_back(n); selectedNode = n.id; selectedLink = -1; publish(); rebuildInspector();
    }
    void rebuildInspector() {
        fields.clear(); nodeValueLink.reset(); inspector.removeAllChildren();
        inspector.addAndMakeVisible(inspectorTitle); inspector.addAndMakeVisible(inspectorInfo);
        inspectorTitle.setColour(juce::Label::textColourId, juce::Colour(0xffe4e9ee));
        inspectorInfo.setColour(juce::Label::textColourId, juce::Colour(0xffaebaca));
        inspectorInfo.setFont(juce::Font(juce::FontOptions(12.0f)));
        nodeValue.onValueChange = nullptr; nodeValue.onDragStart = nullptr; nodeValue.onDragEnd = nullptr;
        nodeValue.setLookAndFeel(&nodeStyle);
        const auto* n = node(selectedNode);
        inspectorTitle.setText(n ? title(*n) : selectedLink >= 0 ? "AUDIO CONNECTION" : "ROUTING INSPECTOR", juce::dontSendNotification);
        juce::String info = "Select a node to inspect it.\n\nSources expose lead / generated takes. Add an explicit Sum before merging signals.\n\nEmpty inputs and cycles are invalid drafts, not silent audio replacements.";
        if (n) {
            info = "Node ID " + juce::String(n->id);
            if (n->type == NodeType::chorale) {
                const int slot = fxSlot(*n);
                info += "\nIndependent FX slot " + juce::String(slot);
                if (slot >= 0) {
                    fxOn.setButtonText("FX enabled"); fxOn.setClickingTogglesState(true); inspector.addAndMakeVisible(fxOn);
                    fxOn.setComponentID(fxParameterId(size_t(slot), size_t(0)));
                    const auto* on = owner.parameters.getRawParameterValue(fxParameterId(size_t(slot), size_t(0)));
                    fxOn.setToggleState(on ? on->load() > 0.5f : n->enabled, juce::dontSendNotification);
                    fxOn.onClick = [this] { toggleBypass(); };
                    for (int f = 1; f <= 2; ++f) {
                        fields.push_back(std::make_unique<detail::ParameterSlider>(owner, fxParameterId(size_t(slot), f), f == 1 ? "Mix" : "Motion", "%"));
                        inspector.addAndMakeVisible(*fields.back());
                    }
                    info += "\nRouting decides what this effect processes.\nNew added FX deliberately reset the reused slot to On / 25% Mix / 35% Motion.";
                } else info += "\nNo matching host automation slot.";
            } else if (n->type == NodeType::sum || n->type == NodeType::mix) {
                inspector.addAndMakeVisible(nodeValue);
                nodeValue.setSliderStyle(juce::Slider::LinearHorizontal);
                nodeValue.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 100, 24);
                nodeValue.setScrollWheelEnabled(false);
                if (n->type == NodeType::mix && n->mainControls) {
                    nodeValueLink = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, "double", nodeValue);
                    info += "\nMain Mix (%)\nDry pin 0 / Wet pin 1";
                } else {
                    const bool sum = n->type == NodeType::sum;
                    nodeValue.setRange(sum ? 1 : 0, sum ? 16 : 100, sum ? 0.01 : 0.1);
                    nodeValue.setValue(sum ? n->referencePower : n->mix * 100, juce::dontSendNotification);
                    info += sum ? "\nReference power budget (1..16).\nCompensation preserves relative voice gains; not an auto-leveler.\nInput pins 1..16 map to backend 0..15." : "\nMix (%)\nDry pin 0 / Wet pin 1";
                    nodeValue.onDragStart = [this] { remember(); inspectorDragging = true; };
                    nodeValue.onDragEnd = [this] { inspectorDragging = false; };
                    nodeValue.onValueChange = [this, sum] {
                        if (!inspectorDragging) remember();
                        if (auto* current = node(selectedNode)) {
                            if (sum) current->referencePower = float(nodeValue.getValue()); else current->mix = float(nodeValue.getValue() / 100);
                            publish();
                        }
                    };
                }
            } else if (n->type == NodeType::voice) info += "\nStable voice " + juce::String(n->voiceSlot + 1) + ".\nUse Advanced for Pitch, gain, pan and motion. Removed/muted slots retain their source identity and wiring.";
            else if (n->type == NodeType::output) info += "\nFinal Output trim and meters remain in the main editor. Exactly one Output is required.";
            else info += "\nProcessed lead source. Fan out this source rather than adding duplicates.";
        } else if (selectedLink >= 0 && selectedLink < int(graph.links.size())) {
            const auto& l = graph.links[size_t(selectedLink)];
            info = "Source " + juce::String(l.source) + " -> Node " + juce::String(l.destination) + "\nInput pin " + juce::String(l.inputPin) + " (zero-based)\n\nUse Disconnect to remove this cable.";
        }
        inspectorInfo.setText(info, juce::dontSendNotification);
        inspectorInfo.setJustificationType(juce::Justification::topLeft);
        setNeon(neon); layoutInspector(); updateButtons(); detail::passiveControl(inspector);
    }
    void layoutInspector() {
        inspectorTitle.setBounds(0, 0, inspector.getWidth(), 30);
        fxOn.setBounds(4, 40, 220, 26);
        int y = 76;
        for (auto& field : fields) { field->setBounds(0, y, inspector.getWidth(), 34); y += 40; }
        nodeValue.setBounds(0, 40, inspector.getWidth(), 64);
        const int infoY = fields.empty() ? (nodeValue.getParentComponent() == &inspector ? 118 : 40) : y + 12;
        inspectorInfo.setBounds(0, infoY, inspector.getWidth(), juce::jmax(20, inspector.getHeight() - infoY));
    }
    void timerCallback() override {
        if (draggingNode || connecting || inspectorDragging) return;
        auto latest = owner.getRoutingGraph();
        const auto next = signature(latest);
        const auto savedLayout = owner.getRoutingLayout();
        const auto layoutSignature = savedLayout.toXmlString();
        if (next != lastSignature || layoutSignature != lastLayoutSignature) {
            graph = std::move(latest); lastSignature = next;
            undoStates.clear(); redoStates.clear(); selectedLink = -1;
            loadLayout(savedLayout); lastLayoutSignature = layoutSignature;
            ensurePositions(); validateDraft(); rebuildInspector();
        }
        if (const auto* n = node(selectedNode); n && n->type == NodeType::chorale && fxSlot(*n) >= 0) {
            const auto* on = owner.parameters.getRawParameterValue(fxParameterId(size_t(fxSlot(*n)), size_t(0)));
            fxOn.setToggleState(on ? on->load() > 0.5f : n->enabled, juce::dontSendNotification);
        }
        canvas.repaint();
    }
    Processor& owner;
    Graph graph;
    std::map<std::uint32_t, juce::Point<float>> positions;
    float zoom = 0.72f;
    juce::Point<float> pan{20, 18}, pointer, dragStart, startPan, startNode;
    std::uint32_t selectedNode = 0, draggingNode = 0, connecting = 0;
    int selectedLink = -1;
    bool neon = false, valid = true, panning = false, inspectorDragging = false;
    juce::String error, lastSignature, lastLayoutSignature;
    std::vector<EditState> undoStates, redoStates;
    EditState dragBefore;
    juce::TextButton add, undo, redo, disconnect, remove, bypass, fit, zoomOut, zoomIn, fxOn;
    juce::Component inspector;
    juce::Label inspectorTitle, inspectorInfo, status;
    detail::ParameterLookAndFeel nodeStyle;
    juce::Slider nodeValue;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> nodeValueLink;

    std::vector<std::unique_ptr<detail::ParameterSlider>> fields;
};

class RoutingWindow final : public juce::DocumentWindow {
public:
    explicit RoutingWindow(Processor& owner)
        : DocumentWindow("Vocal Stack / Routing", juce::Colour(0xff171d25), juce::DocumentWindow::closeButton), panel(owner) {
        setUsingNativeTitleBar(true);
        setContentNonOwned(&panel, true);
        setResizable(true, false);
        setResizeLimits(900, 540, 1800, 1200);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        centreWithSize(1120, 700);
    }
    ~RoutingWindow() override { clearContentComponent(); }
    void closeButtonPressed() override { setVisible(false); }
    void setNeon(bool value) { panel.setNeon(value); }
private:
    RoutingPanel panel;
    juce::TooltipWindow tips{this, 530};
};
} // namespace vocalstack
