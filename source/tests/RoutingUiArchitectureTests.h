#pragma once

#include "RoutingPanel.h"
#include "MainBusFxRackPanel.h"
#include "ProcessorRoutingState.h"
#include "GuiTestDisplay.h"
#include <memory>
#include <stdexcept>

// Include at file scope in EditorTests.cpp, then register (and grow its table):
// {"Routing UI architecture", [] { vocalstack::routingUiArchitectureTests::run(); }}
// Message thread / ScopedJuceInitialiser_GUI required. Native fixtures use the
// enumerated non-primary display, with an explicit primary-only fallback.
namespace vocalstack::routingUiArchitectureTests {
inline void require(bool ok, const juce::String& message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
inline juce::String encoded(const Graph& graph) {
    return processorRouting::encode(graph, "Graph").toXmlString();
}
inline juce::Button& button(juce::Component& root, const juce::String& text) {
    for (auto* child : root.getChildren())
        if (auto* result = dynamic_cast<juce::Button*>(child); result && result->getButtonText() == text) return *result;
    throw std::runtime_error(("Missing Routing action: " + text).toStdString());
}
inline juce::Slider* slider(juce::Component& root, const juce::String& id) {
    if (auto* result = dynamic_cast<juce::Slider*>(&root); result && result->getComponentID() == id) return result;
    for (auto* child : root.getChildren()) if (auto* result = slider(*child, id)) return result;
    return nullptr;
}
struct Fixture {
    std::unique_ptr<Processor> storage = std::make_unique<Processor>();
    Processor& owner = *storage;
    RoutingPanel panel{owner};
    Fixture() {
        panel.setSize(904, 600);
        guiTestDisplay::attach(panel);
    }
    void tick() {
        const auto before = panel.routingWorkCounters().timerTicks;
        for (int attempt = 0; attempt < 50 && panel.routingWorkCounters().timerTicks == before; ++attempt) {
            juce::Thread::sleep(20);
            juce::Timer::callPendingTimersSynchronously();
        }
        require(panel.routingWorkCounters().timerTicks > before, "Routing timer did not run");
    }
    void add(NodeType type = NodeType::granular) {
        const auto edit = mainBusFx::add(owner.getRoutingGraph(), type);
        require(edit.changed && edit.error.empty(), "Fixture add failed");
        juce::String error;
        require(panel.commitExternalGraphEdit(edit.graph, error), "Routing commit failed: " + error);
    }
    void action(const juce::String& text) {
        auto& target = button(panel, text);
        require(target.isEnabled() && bool(target.onClick), "Unavailable action: " + text);
        target.onClick();
    }
    void install(const Graph& graph) {
        juce::String error;
        require(owner.setRoutingGraph(graph, error), "External replacement failed: " + error);
    }
    void preferences(int width) {
        auto layout = owner.getRoutingLayout();
        layout.setProperty("editorWidth", width, nullptr);
        layout.setProperty("editorHeight", 780, nullptr);
        layout.setProperty("inspectorWidth", 380, nullptr);
        layout.setProperty("modulationHeight", 220, nullptr);
        layout.setProperty("unrelatedPreference", "keep me", nullptr);
        owner.setRoutingLayout(layout);
    }
    void checkPreferences(int width) {
        const auto layout = owner.getRoutingLayout();
        require(int(layout.getProperty("editorWidth")) == width
            && int(layout.getProperty("editorHeight")) == 780
            && int(layout.getProperty("inspectorWidth")) == 380
            && int(layout.getProperty("modulationHeight")) == 220
            && layout.getProperty("unrelatedPreference").toString() == "keep me",
            "History replayed stale window/preferences");
    }
};
inline void presentationHistory() {
    Fixture f;
    auto modulation = f.owner.getModulationState();
    modulation.modulators.clear(); modulation.assignments.clear();
    Modulator first; first.id = 1;
    Modulator second; second.id = 2;
    modulation.modulators.push_back(first); modulation.modulators.push_back(second);
    juce::String error;
    require(f.owner.setModulationState(modulation, error), "Modulation fixture failed: " + error);
    modulationui::selectModulator(f.owner, 1);
    const auto initial = f.owner.getRoutingGraph();
    f.add(); const auto added = f.owner.getRoutingGraph();
    f.preferences(1200);
    auto legacy = f.owner.getRoutingLayout();
    legacy.setProperty("selectedModulator", 1, nullptr);
    f.owner.setRoutingLayout(legacy);
    modulationui::selectModulator(f.owner, 2);
    f.owner.beginModulationAssignment(1);
    f.tick();
    require(f.panel.selectedModulationId() == 2, "Routing did not follow canonical feedback selection");
    f.action("Undo");
    require(encoded(f.owner.getRoutingGraph()) == encoded(initial), "Resize/feedback selection invalidated Undo");
    f.checkPreferences(1200);
    require(f.panel.selectedModulationId() == 2, "Undo rewound feedback selection");
    // Change preferences after Undo and replay before a timer can observe them.
    f.preferences(1300); modulationui::selectModulator(f.owner, 1);
    f.action("Redo");
    require(encoded(f.owner.getRoutingGraph()) == encoded(added), "Presentation update invalidated Redo");
    f.checkPreferences(1300);
    require(f.panel.selectedModulationId() == 1, "Redo rewound feedback selection");
    require(!button(f.panel, "Redo").isEnabled(), "Redo created an extra history step");
    require(f.owner.modulationAssignmentId() == 1, "Feedback/history changed Assign identity");

    // Layout-only snapshots carry the same no-stale-presentation contract.
    f.action(">");
    modulationui::selectModulator(f.owner, 2); f.preferences(1350);
    f.action("Undo");
    require(encoded(f.owner.getRoutingGraph()) == encoded(added) && f.panel.selectedModulationId() == 2,
            "Layout-only Undo rewound graph/feedback");
    f.checkPreferences(1350);
    f.action("Redo");
    require(f.panel.selectedModulationId() == 2 && f.owner.modulationAssignmentId() == 1,
            "Layout-only Redo rewound feedback/Assign");
    f.checkPreferences(1350);
    f.action("Undo"); // Return before the layout-only edit, leaving the graph edit on top.

    // Hidden rack commits use the same document-vs-presentation distinction.
    f.panel.setVisible(false); f.preferences(1400); f.add(NodeType::delay);
    f.action("Undo");
    require(encoded(f.owner.getRoutingGraph()) == encoded(added), "Hidden rack commit lost preceding routing history");
    f.action("Undo");
    require(encoded(f.owner.getRoutingGraph()) == encoded(initial), "Hidden preference update cleared prior Undo");
    f.checkPreferences(1400);
}
inline void externalReplacement() {
    Fixture f;
    const auto initial = f.owner.getRoutingGraph();
    f.add(); f.install(initial); f.tick();
    require(!button(f.panel, "Undo").isEnabled() && !button(f.panel, "Redo").isEnabled(),
            "Timer did not invalidate history after external graph replacement");

    f.add(); f.install(initial);
    // Deliberately invoke the stale enabled action before the timer refreshes.
    f.action("Undo");
    require(encoded(f.owner.getRoutingGraph()) == encoded(initial)
        && !button(f.panel, "Undo").isEnabled() && !button(f.panel, "Redo").isEnabled(),
        "Immediate Undo replayed history from a replaced document");

    f.add();
    auto layout = f.owner.getRoutingLayout();
    require(layout.getNumChildren() > 0, "Fixture did not save routing positions");
    auto node = layout.getChildWithProperty("id", 13);
    require(node.isValid(), "Fixture output position missing");
    node.setProperty("x", float(node.getProperty("x")) + 20.0f, nullptr);
    f.owner.setRoutingLayout(layout); f.tick();
    require(!button(f.panel, "Undo").isEnabled(), "External NODE document replacement retained stale history");

    f.add(NodeType::delay); f.action("Undo");
    require(button(f.panel, "Redo").isEnabled(), "Fixture did not create Redo");
    f.install(initial); f.tick();
    require(!button(f.panel, "Redo").isEnabled(), "External replacement retained stale Redo");
}
struct GestureProbe final : juce::AudioProcessorParameter::Listener {
    explicit GestureProbe(juce::RangedAudioParameter& value) : parameter(value) { parameter.addListener(this); }
    ~GestureProbe() override { parameter.removeListener(this); }
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override { starting ? ++begins : ++ends; }
    juce::RangedAudioParameter& parameter;
    int begins = 0, ends = 0;
};
inline void inspectorTeardown() {
    Fixture f; f.add(); f.panel.selectEffect(100);
    const auto id = creativeParameterId(1, size_t(CreativeParameter::granularPitch));
    auto* control = slider(f.panel, id);
    auto* parameter = f.owner.parameters.getParameter(id);
    require(control && parameter, "Creative inspector pitch control missing");
    GestureProbe probe(*parameter);
    const auto now = juce::Time::getCurrentTime();
    const auto point = control->getLocalBounds().getCentre().toFloat();
    const juce::MouseEvent down{juce::Desktop::getInstance().getMainMouseSource(), point,
        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0,
        control, control, now, point, now, 1, false};
    control->mouseDown(down);
    control->mouseDrag(down.withNewPosition(point.translated(0, -20)));
    require(probe.begins == 1 && probe.ends == 0, "Fixture did not open a host gesture");
    f.panel.selectEffect(12);
    require(probe.ends == 1, "Inspector replacement leaked its active host gesture");
    f.panel.selectEffect(100); f.panel.selectVoice(-1);
    require(probe.ends == 1, "Repeated inspector teardown closed a gesture twice");
    // A leaked inspectorDragging flag would block the timer's external sync.
    const auto replacement = mainBusFx::remove(f.owner.getRoutingGraph(), 100);
    require(replacement.changed, "Fixture remove failed");
    f.install(replacement.graph); f.tick();
    require(!button(f.panel, "Undo").isEnabled(), "Inspector teardown left dragging latched and blocked external sync");
}
inline bool hasLabel(juce::Component& root, const juce::String& text) {
    if (auto* label = dynamic_cast<juce::Label*>(&root); label && label->getText() == text) return true;
    for (auto* child : root.getChildren()) if (hasLabel(*child, text)) return true;
    return false;
}
inline void hostBypassWithoutPolling() {
    Fixture f;
    auto* on = f.owner.parameters.getParameter(fxParameterId(0, "On"));
    require(on != nullptr, "Missing main Chorale On parameter");
    const auto write = [&](float value) {
        on->beginChangeGesture();
        on->setValueNotifyingHost(on->convertTo0to1(value));
        on->endChangeGesture();
    };
    write(1);
    f.panel.selectEffect(12);
    require(hasLabel(f.panel, "CHORALE / MAIN"), "Enabled main Chorale inspector missing");
    const auto ticks = f.panel.routingWorkCounters().timerTicks;
    write(0);
    require(f.panel.routingWorkCounters().timerTicks == ticks, "Fixture unexpectedly polled Routing");
    require(!hasLabel(f.panel, "CHORALE / MAIN"), "Host bypass requires a timer tick to retire hidden selection");
    require(hasLabel(f.panel, "ROUTING INSPECTOR"), "Host bypass did not rebuild the retired inspector");
    write(1); f.panel.selectEffect(12); write(0);
    require(!hasLabel(f.panel, "CHORALE / MAIN"), "Repeated host bypass retained the folded selection");
}
inline void run() {
    guiTestDisplay::enumerate();
    presentationHistory();
    externalReplacement();
    inspectorTeardown();
    hostBypassWithoutPolling();
}
} // namespace vocalstack::routingUiArchitectureTests
