#pragma once

// Included inside EditorTests.cpp's test namespace after RoutingPolishTests.h.
namespace stackModulationUi {
namespace ui = vocalstack::toneUiTests;
using routingPolish::pumpTimers;
using routingPolish::action;

template <class Predicate>
void waitFor(Predicate ready, const juce::String& reason) {
    for (int attempt = 0; attempt < 15; ++attempt) {
        if (ready()) return;
        pumpTimers();
    }
    expect(ready(), reason);
}

template <class T>
T& component(juce::Component& root) {
    auto* found = ui::find<T>(root, [](auto&) { return true; });
    expect(found != nullptr, "Missing shared UI component");
    return *found;
}

void localMixerInspector() {
    Processor owner;
    vocalstack::RoutingNode mixer{200, vocalstack::NodeType::mix}; mixer.mix = .2f;
    vocalstack::Graph graph{{{1, vocalstack::NodeType::lead}, mixer, {13, vocalstack::NodeType::output}},
        {{1, 200, 0}, {200, 13, 0}}};
    juce::String error;
    expect(owner.setRoutingGraph(graph, error), "Local mixer UI graph rejected");
    auto layout = owner.getRoutingLayout(); layout.setProperty("selectedNode", 200, nullptr); owner.setRoutingLayout(layout);
    vocalstack::ModulationState state; state.modulators.push_back({1});
    expect(owner.setModulationState(state, error), "Local mixer UI source rejected");
    std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
    editor->addToDesktop(0); editor->setVisible(true); action(*editor, "Routing");
    auto& routing = component<vocalstack::RoutingPanel>(*editor);
    auto& knob = ui::slider(routing, "mix1Balance");
    expectNear(knob.getValue(), 20, "Local mixer did not bind saved value");
    juce::Component::SafePointer<juce::Slider> safe(&knob);
    knob.setValue(65, juce::sendNotificationSync); pumpTimers();
    expect(safe && std::abs(parameterValue(owner, "mix1Balance") - 65) < 1e-5,
        "Mixer automation destroyed its inspector or lost the value");
    auto* title = ui::find<juce::Label>(routing, [](auto& label) { return label.getText() == "DRY / WET MIX"; });
    expect(title && title->isEditable(), "Mixer title is not renameable");
    title->setText("Parallel texture", juce::sendNotificationSync);
    bool savedName = false;
    for (const auto& child : owner.getRoutingLayout())
        if (int(child.getProperty("id")) == 200) savedName = child.getProperty("name").toString() == "Parallel texture";
    expect(savedName, "Machine alias was not saved separately from DSP topology");
    owner.beginModulationAssignment(1); pumpTimers();
    ui::drag(*safe, {30, 30}, {30, -6});
    expect(owner.getModulationState().assignments.size() == 1 && parameterValue(owner, "mix1Balance") == 65,
        "Local mixer assignment failed or wrote base");
    expectPassiveControls(*editor);
}

void eqModulationGestures() {
    for (const bool neon : {false, true}) {
        Processor owner;
        setParameter(owner, "fx1EqBand1On", 1);
        setParameter(owner, "fx1EqBand1Frequency", 1000);
        setParameter(owner, "fx1EqBand1Gain", 0);
        vocalstack::ModulationState state; state.modulators.push_back({1});
        juce::String error;
        expect(owner.setModulationState(state, error), "EQ modulation setup failed");
        owner.beginModulationAssignment(1);
        vocalstack::ToneEffectPanel panel(owner, 1, vocalstack::NodeType::equalizer);
        panel.setNeon(neon); panel.setSize(400, 650); panel.addToDesktop(0); panel.setVisible(true);
        auto& graph = component<vocalstack::toneui::EqGraph>(panel);
        ui::GestureLog gestures(owner);
        const float baseFrequency = parameterValue(owner, "fx1EqBand1Frequency");
        const float baseGain = parameterValue(owner, "fx1EqBand1Gain");
        const auto from = ui::eqPoint(graph, baseFrequency, baseGain);
        ui::drag(graph, from, from.translated(25, -18));
        auto assigned = owner.getModulationState();
        expect(assigned.assignments.size() == 2, "EQ drag did not assign frequency and gain");
        for (const auto& a : assigned.assignments) expect(a.depth > 0, "EQ drag produced wrong depth direction");
        expect(parameterValue(owner, "fx1EqBand1Frequency") == baseFrequency, "EQ Assign changed base frequency");
        expect(parameterValue(owner, "fx1EqBand1Gain") == baseGain, "EQ Assign changed base gain");
        juce::MouseWheelDetails wheel; wheel.deltaY = .2f;
        graph.mouseWheelMove(ui::mouse(graph, from, from, false, 0), wheel);
        expect(owner.getModulationState().assignments.size() == 3, "EQ wheel did not assign Q");
        ui::drag(graph, ui::eqPoint(graph, 3000, -6), ui::eqPoint(graph, 5000, -8));
        expect(ui::enabledBands(owner) == 1, "Assign created an EQ band");
        for (auto* parameter : owner.getParameters()) {
            const auto count = gestures.count(*dynamic_cast<juce::RangedAudioParameter*>(parameter));
            expect(count.values == 0 && count.begins == 0 && count.ends == 0, "EQ assignment wrote host base/gestures");
        }
        owner.endModulationAssignment(); graph.refresh(true);
        expectPassiveControls(panel);
    }
}

void waveQualityRaw() {
    for (const bool neon : {false, true}) {
        Processor owner;
        owner.setFlash(neon);
        vocalstack::ToneEffectPanel panel(owner, 1, vocalstack::NodeType::waveshaper);
        panel.setNeon(neon);
        panel.setSize(400, 720);
        panel.addToDesktop(0);
        panel.setVisible(true);
        expect(panel.isShowing() && panel.getPeer(), "Quality fixture has no showing native peer");
        auto& quality = ui::combo(panel, "fx1WaveQuality");
        expect(quality.isEnabled() && quality.getNumItems() == 3, "Waveshaper quality choices unavailable");
        quality.setSelectedId(3, juce::sendNotificationSync);
        expectNear(parameterValue(owner, "fx1WaveQuality"), 2, "Quality menu did not bind 4x mode");
        setParameter(owner, "fx1WaveRaw", 1);
        waitFor([&] { return !quality.isEnabled(); }, "Raw did not disable Quality menu");
        expect(quality.getSelectedId() == 3, "Raw erased Quality menu selection");
        expectNear(parameterValue(owner, "fx1WaveQuality"), 2, "Raw erased stored quality");
        setParameter(owner, "fx1WaveQuality", 1);
        expect(quality.getSelectedId() == 2 && !quality.isEnabled(), "Disabled Quality did not follow host automation");
        setParameter(owner, "fx1WaveRaw", 0);
        waitFor([&] { return quality.isEnabled(); }, "Leaving Raw did not re-enable Quality");
        expect(quality.getSelectedId() == 2, "Leaving Raw did not restore retained Quality");
        expectNear(parameterValue(owner, "fx1WaveQuality"), 1, "Leaving Raw changed quality parameter");
        expectPassiveControls(panel);
    }
}

void globalMixBypass() {
    for (const bool neon : {false, true}) {
        Processor owner;
        owner.setFlash(neon);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
        editor->addToDesktop(0);
        editor->setVisible(true);
        expect(editor->isShowing() && editor->getPeer(), "Mix fixture has no showing native peer");
        auto& mix = ui::slider(*editor, "globalMix");
        auto& balance = ui::slider(*editor, "double");
        expect(mix.getName() == "Mix" && balance.getName() == "Lead / Stack", "Mix and Lead / Stack labels conflated");
        expect(parameter(owner, "globalMix").getParameterIndex() == 874, "Global Mix must append after the original 874 parameters");
        expectNear(mix.getMinimum(), 0, "Global Mix minimum");
        expectNear(mix.getMaximum(), 100, "Global Mix maximum");
        expectNear(mix.getValue(), 100, "Global Mix default");
        mix.setValue(37, juce::sendNotificationSync);
        expectNear(parameterValue(owner, "globalMix"), 37, "Global Mix UI binding");
        expectNear(parameterValue(owner, "double"), 35, "Global Mix changed Lead / Stack");
        setParameter(owner, "bypass", 1);
        waitFor([&] { return !mix.isEnabled() && mix.getParentComponent()->getAlpha() < 1; },
                "Bypass did not disable and dim Mix");
        expectNear(parameterValue(owner, "globalMix"), 37, "Bypass erased Mix");
        setParameter(owner, "globalMix", 62);
        expectNear(mix.getValue(), 62, "Disabled Mix did not follow host automation");
        setParameter(owner, "bypass", 0);
        waitFor([&] { return mix.isEnabled() && mix.getParentComponent()->getAlpha() == 1; },
                "Leaving bypass did not re-enable and undim Mix");
        expectNear(mix.getParentComponent()->getAlpha(), 1, "Leaving bypass left Mix dimmed");
        expectNear(mix.getValue(), 62, "Leaving bypass reset Mix");
        expectPassiveControls(*editor);
    }
}

void resizeTabs() {
    for (const bool neon : {false, true}) {
        Processor owner;
        owner.setFlash(neon);
        owner.setAdvancedVisible(true);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
        editor->addToDesktop(0);
        editor->setVisible(true);
        expect(editor->isShowing() && editor->getPeer(), "Resize fixture has no showing native peer");
        auto& canvas = *ui::slider(*editor, "outputGain").getParentComponent();
        auto& routing = component<vocalstack::RoutingPanel>(*editor);
        for (const auto size : {juce::Point<int>(940, 560), juce::Point<int>(1480, 1040)}) {
            editor->setSize(size.x, size.y);
            expect(canvas.getWidth() == 940 && canvas.getHeight() == 872 && !canvas.getTransform().isIdentity(),
                   "Expanded Stack must scale its fixed design canvas");
            auto& output = ui::slider(*editor, "outputGain");
            expect(editor->getLocalBounds().contains(editor->getLocalArea(&output, output.getLocalBounds())),
                   "Scaled Stack output is outside the window");
            action(*editor, "Routing");
            expect(canvas.getTransform().isIdentity() && canvas.getBounds() == editor->getLocalBounds(),
                   "Routing header must use unscaled window coordinates");
            expect(routing.getBounds() == juce::Rectangle<int>(18, 100, size.x - 36, size.y - 118),
                   "Routing did not grow with available space");
            action(*editor, "Stack");
            action(*editor, "Hide Advanced");
            expect(canvas.getHeight() == 430 && editor->getWidth() == size.x && editor->getHeight() == size.y,
                   "Drawer visibility changed window dimensions rather than Stack scale");
            action(*editor, "Show Advanced");
            expect(editor->getWidth() == size.x && editor->getHeight() == size.y, "Restoring drawer resized window");
        }
        editor.reset();
        editor.reset(owner.createEditor());
        expect(editor->getWidth() == 1480 && editor->getHeight() == 1040 && owner.isAdvancedVisible(),
               "Editor reopen lost resized dimensions or drawer state");
    }
}

void sharedInspectors() {
    for (const bool neon : {false, true}) {
        Processor owner;
        owner.setFlash(neon);
        owner.setAdvancedEnabled(true);
        owner.setAdvancedVisible(true);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
        editor->addToDesktop(0);
        editor->setVisible(true);
        expect(editor->isShowing() && editor->getPeer(), "Shared-controls fixture has no showing native peer");
        auto& stack = component<vocalstack::DoublerPanel>(*editor);
        auto& routing = component<vocalstack::RoutingPanel>(*editor);
        const auto originalGraph = graphText(owner.getRoutingGraph());
        // Lead is already selected on construction; start with a real transition.
        for (const int slot : {0, -1, 1}) {
            const juce::String prefix = slot < 0 ? "lead" : "voice" + juce::String(slot + 1);
            auto* card = ui::find<vocalstack::VoiceControls>(stack, [&](auto& c) { return c.getComponentID() == prefix + "Card"; });
            expect(card && bool(card->onSelect), "Missing selectable Lead/voice card");
            card->onSelect();
            expect(int(owner.getRoutingLayout().getProperty("selectedVoiceSlot", -99)) == slot,
                   "Stack selection did not reach shared inspector state");
            action(*editor, "Routing");
            expect(routingPolish::labelIs(routing, slot < 0 ? "LEAD" : "VOICE " + juce::String(slot + 1)),
                   "Routing did not reveal selected Stack card");
            for (const auto* field : {"Pitch", "Detune", "Formant", "Gain", "Pan", "Delay", "Depth", "Rate"}) {
                const auto id = prefix + field;
                auto& stackKnob = ui::slider(*card, id);
                auto& inspectorKnob = ui::slider(routing, id);
                expect(stackKnob.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
                           && inspectorKnob.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag,
                       "Shared voice control stopped being rotary: " + id);
                const auto& range = parameter(owner, id).getNormalisableRange();
                const float fromHost = range.convertFrom0to1(0.3f);
                setParameter(owner, id, fromHost);
                expectNear(stackKnob.getValue(), parameterValue(owner, id), "Stack did not follow host: " + id);
                expectNear(inspectorKnob.getValue(), stackKnob.getValue(), "Inspector did not follow host: " + id);
                inspectorKnob.setValue(range.convertFrom0to1(0.6f), juce::sendNotificationSync);
                expectNear(stackKnob.getValue(), inspectorKnob.getValue(), "Inspector edit did not reach Stack: " + id);
                stackKnob.setValue(range.convertFrom0to1(0.4f), juce::sendNotificationSync);
                expectNear(inspectorKnob.getValue(), stackKnob.getValue(), "Stack edit did not reach inspector: " + id);
            }
            auto* stackOn = ui::find<juce::Button>(*card, [&](auto& b) { return b.getComponentID() == prefix + "On"; });
            auto* inspectorOn = ui::find<juce::Button>(routing, [&](auto& b) { return b.getComponentID() == prefix + "On"; });
            expect(stackOn && inspectorOn, "Missing shared voice enable switches");
            setParameter(owner, prefix + "On", 0);
            expect(!stackOn->getToggleState() && !inspectorOn->getToggleState(), "Shared voice switches did not follow host mute");
            inspectorOn->setToggleState(true, juce::sendNotificationSync);
            expect(stackOn->getToggleState() && parameterValue(owner, prefix + "On") == 1,
                   "Inspector enable did not reach Stack and host parameter");
            const int selected = slot < 0 ? 0 : slot == 0 ? 1 : -1;
            routing.selectVoice(selected);
            expect(int(owner.getRoutingLayout().getProperty("selectedVoiceSlot", -99)) == selected,
                   "Routing selection did not update shared voice state");
            action(*editor, "Stack");
            expect(graphText(owner.getRoutingGraph()) == originalGraph, "Shared control edits changed wiring");
            expectPassiveControls(*editor);
        }
    }
}

void modulationInspector() {
    for (const bool neon : {false, true}) {
        Processor owner;
        owner.setFlash(neon);
        owner.setAdvancedVisible(true);
        vocalstack::ModulationState state;
        state.modulators.push_back({1});
        state.modulators.push_back({2, vocalstack::ModulatorType::random});
        state.assignments.push_back({2, "voice1Gain", -0.15f, true});
        juce::String error;
        expect(owner.setModulationState(state, error), "Modulation UI fixture rejected: " + error);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
        editor->addToDesktop(0);
        editor->setVisible(true);
        expect(editor->isShowing() && editor->getPeer(), "Modulation fixture has no showing native peer");
        auto& routing = component<vocalstack::RoutingPanel>(*editor);
        action(*editor, "Routing");
        action(routing, "LFO 1");
        auto& inspector = component<vocalstack::ModulationPanel>(routing);
        expect(findButton(inspector, "Controls")->getToggleState(), "Modulator must open Controls first");
        auto* rate = ui::find<juce::Slider>(inspector, [](auto& s) { return s.getName() == "Rate / Hz"; });
        expect(rate && rate->isShowing(), "Controls has no rate rotary");
        rate->setValue(2.5, juce::sendNotificationSync);
        expectNear(owner.getModulationState().modulators.front().rateHz, 2.5, "Controls did not update modulator rate");
        expectNear(owner.getModulationState().modulators.back().rateHz, 1, "Controls changed another modulator");
        saveSnapshot(*editor, neon ? "editor-lfo-neon.png" : "editor-lfo-device.png");
        action(inspector, "Assign");
        expect(owner.modulationAssignmentId() == 1, "Assign did not activate selected LFO");
        action(*editor, "Stack");
        pumpTimers();
        ui::GestureLog log(owner);
        std::vector<float> bases;
        for (auto* p : owner.getParameters()) bases.push_back(p->getValue());
        for (const auto* id : {"double", "leadPitch", "voice1Gain", "globalMix"}) {
            auto& target = ui::slider(*editor, id);
            waitFor([&] { return !target.isTextBoxEditable(); }, "Assign mode left base numeric entry enabled: "
                + juce::String(id) + " / source " + juce::String(owner.modulationAssignmentId()));
            ui::drag(target, {30, 30}, {30, -15});
            auto current = owner.getModulationState();
            auto it = std::find_if(current.assignments.begin(), current.assignments.end(), [&](const auto& a) {
                return a.modulatorId == 1 && a.targetParameterId == id;
            });
            expect(it != current.assignments.end(), "Assignment drag did not create target: " + juce::String(id));
            expectNear(it->depth, 0.25, "Assignment drag signed depth");
            ui::drag(target, {30, 30}, {30, 48}, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
            current = owner.getModulationState();
            it = std::find_if(current.assignments.begin(), current.assignments.end(), [&](const auto& a) {
                return a.modulatorId == 1 && a.targetParameterId == id;
            });
            expect(it != current.assignments.end(), "Fine assignment drag lost target");
            expectNear(it->depth, 0.24, "Fine negative assignment adjustment");
            ui::doubleClick(target, {30, 30});
        }
        for (int i = 0; i < owner.getParameters().size(); ++i) {
            auto* p = owner.getParameters()[i];
            expectNear(p->getValue(), bases[size_t(i)], "Assignment gesture changed a base parameter");
            const auto count = log.count(*dynamic_cast<juce::RangedAudioParameter*>(p));
            expect(count.values == 0 && count.begins == 0 && count.ends == 0, "Assignment emitted host base writes/gestures");
        }
        action(*editor, "Routing");
        routing.selectVoice(-1);
        expect(owner.modulationAssignmentId() == 1, "Audio node selection cancelled Assign");
        action(routing, "LFO 1");
        // Selection may rebuild the inspector: reacquire it and its children.
        auto& assignmentInspector = component<vocalstack::ModulationPanel>(routing);
        action(assignmentInspector, "Assignments");
        expect(findButton(assignmentInspector, "Assignments")->getToggleState() && !findButton(assignmentInspector, "Controls")->getToggleState(),
               "Assignments tab selection not reflected");
        auto* amount = ui::find<juce::Slider>(assignmentInspector, [](auto& s) { return s.getName() == "Signed assignment depth" && s.isShowing(); });
        expect(amount != nullptr, "Assignments has no signed-depth row");
        auto* assignmentRate = ui::find<juce::Slider>(assignmentInspector, [](auto& s) { return s.getName() == "Rate / Hz"; });
        expect(assignmentRate && !assignmentRate->isShowing(), "Assignments left Controls visible");
        // Rows retain the processor's assignment order, filtered by selected modulator.
        const auto beforeRemoval = owner.getModulationState();
        const auto first = std::find_if(beforeRemoval.assignments.begin(), beforeRemoval.assignments.end(),
                                       [](const auto& a) { return a.modulatorId == 1; });
        expect(first != beforeRemoval.assignments.end(), "Missing first assignment row model");
        const auto target = first->targetParameterId;
        saveSnapshot(*editor, neon ? "editor-assignments-neon.png" : "editor-assignments-device.png");
        amount->setValue(-0.125, juce::sendNotificationSync);
        const auto edited = owner.getModulationState();
        const auto editedRow = std::find_if(edited.assignments.begin(), edited.assignments.end(), [&](const auto& a) {
            return a.modulatorId == 1 && a.targetParameterId == target;
        });
        expect(editedRow != edited.assignments.end(), "Depth edit removed its assignment");
        expectNear(editedRow->depth, -0.125, "Assignment row did not store signed depth");
        action(assignmentInspector, "Remove");
        pumpTimers();
        auto after = owner.getModulationState();
        expect(after.assignments.size() == 4 && after.modulators.size() == 2, "Remove deleted more than one assignment or a modulator");
        expect(std::none_of(after.assignments.begin(), after.assignments.end(), [&](const auto& a) {
            return a.modulatorId == 1 && a.targetParameterId == target;
        }), "Remove left its target assigned");
        expectNear(after.assignments.front().depth, -0.15, "Remove altered another modulator's assignment");
        for (const auto& assignment : edited.assignments) {
            if (assignment.modulatorId == 1 && assignment.targetParameterId == target) continue;
            const auto survivor = std::find_if(after.assignments.begin(), after.assignments.end(), [&](const auto& a) {
                return a.modulatorId == assignment.modulatorId && a.targetParameterId == assignment.targetParameterId;
            });
            expect(survivor != after.assignments.end() && survivor->enabled == assignment.enabled,
                   "Remove lost or disabled an unrelated assignment");
            expectNear(survivor->depth, assignment.depth, "Remove changed an unrelated signed depth");
        }
        action(assignmentInspector, "Controls");
        auto* restoredRate = ui::find<juce::Slider>(assignmentInspector, [](auto& s) { return s.getName() == "Rate / Hz"; });
        expect(findButton(assignmentInspector, "Controls")->getToggleState() && restoredRate && restoredRate->isShowing(),
               "Controls tab did not restore");
        expectNear(restoredRate->getValue(), 2.5, "Controls/Assignments switch lost rate");
        action(*editor, "Done");
        pumpTimers();
        expect(owner.modulationAssignmentId() == 0, "Done did not end Assign");

        action(*editor, "Stack");
        pumpTimers();
        expect(ui::slider(*editor, "double").isTextBoxEditable(), "Leaving Assign did not restore numeric entry");
        for (int i = 0; i < owner.getParameters().size(); ++i) {
            auto& p = *dynamic_cast<juce::RangedAudioParameter*>(owner.getParameters()[i]);
            expectNear(p.getValue(), bases[size_t(i)], "Inspector assignment edit changed a base parameter");
            expect(log.count(p).values == 0, "Inspector assignment edit notified a host base parameter");
        }
        log.balanced("Modulation inspector");
        expectPassiveControls(*editor);
    }
}
} // namespace stackModulationUi
