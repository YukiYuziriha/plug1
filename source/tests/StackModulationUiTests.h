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
    std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
    Processor& owner = *ownerStorage;
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
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage;
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
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage;
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
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage;
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
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage;
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
            const int contentHeight = size.y - 52; // Preset toolbar has its own unscaled footer.
            const float scale = std::min(1.0f, std::min(float(size.x) / 940, float(contentHeight) / 872));
            const auto transform = canvas.getTransform();
            expect(canvas.getWidth() == 940 && canvas.getHeight() == 872,
                   "Stack must retain its fixed design canvas");
            expectNear(transform.mat00, scale, "Stack horizontal scale must never exceed one");
            expectNear(transform.mat11, scale, "Stack vertical scale must never exceed one");
            expectNear(transform.mat02, (size.x - 940 * scale) * .5f, "Stack horizontal centering");
            expectNear(transform.mat12, (contentHeight - 872 * scale) * .5f, "Stack vertical centering above preset toolbar");
            auto& output = ui::slider(*editor, "outputGain");
            expect(editor->getLocalBounds().contains(editor->getLocalArea(&output, output.getLocalBounds())),
                   "Scaled Stack output is outside the window");
            action(*editor, "Routing");
            expect(canvas.getTransform().isIdentity() && canvas.getBounds() == juce::Rectangle<int>(0, 0, size.x, contentHeight),
                   "Routing header must use unscaled content coordinates above preset toolbar");
            expect(routing.getBounds() == juce::Rectangle<int>(18, 100, size.x - 36, contentHeight - 118),
                   "Routing did not grow with available space");
            action(*editor, "Stack");
            owner.setAdvancedVisible(false); pumpTimers();
            expect(canvas.getHeight() == 872 && component<vocalstack::DoublerPanel>(*editor).isShowing()
                       && editor->getWidth() == size.x && editor->getHeight() == size.y,
                   "Legacy drawer state changed fixed Stack layout or window dimensions");
        }
        editor.reset();
        editor.reset(owner.createEditor());
        expect(editor->getWidth() == 1480 && editor->getHeight() == 1040 && component<vocalstack::DoublerPanel>(*editor).isVisible(),
               "Editor reopen lost resized dimensions or always-visible Stack");
    }
}

void sharedInspectors() {
    for (const bool neon : {false, true}) {
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage;
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
                expect(stackKnob.getSliderStyle() == juce::Slider::LinearHorizontal
                           && inspectorKnob.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag,
                       "Stack must use horizontal sliders while inspector retains rotary knobs: " + id);
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
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage;
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
// Send through JUCE's peer so registered card/child mouse listeners run too.
void peerClick(juce::Component& root, juce::Component& target, juce::Point<int> point) {
    auto* peer = root.getPeer();
    expect(peer != nullptr, "Mouse fixture needs a native peer");
    root.setAlwaysOnTop(true);
        root.toFront(true);
        const auto local = root.getLocalPoint(&target, point).toFloat();
        expect(root.getComponentAt(local) == &target, "Card click fixture did not hit intended component");
        const auto p = local * root.getDesktopScaleFactor();
    const auto now = juce::Time::currentTimeMillis();
    peer->handleMouseEvent(juce::MouseInputSource::mouse, p, {}, 1, 0, now);
    peer->handleMouseEvent(juce::MouseInputSource::mouse, p,
                          juce::ModifierKeys::leftButtonModifier, 1, 0, now + 1);
    peer->handleMouseEvent(juce::MouseInputSource::mouse, p, {}, 1, 0, now + 2);
}

void compactCards() {
    for (const bool neon : {false, true}) {
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage; owner.setFlash(neon);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
        editor->addToDesktop(0); editor->setVisible(true);
        auto& stack = component<vocalstack::DoublerPanel>(*editor);
        for (const auto* prefix : {"lead", "voice1", "voice2"}) {
            auto* card = ui::find<vocalstack::VoiceControls>(stack, [&](auto& c) {
                return c.getComponentID() == juce::String(prefix) + "Card";
            });
            expect(card && card->getWidth() == 172 && card->getHeight() == 278,
                   "Stack card must stay compact at 172x278");
            const std::array<const char*, 8> fields{"Pitch", "Detune", "Formant", "Gain", "Pan", "Delay", "Depth", "Rate"};
            for (size_t i = 0; i < fields.size(); ++i) {
                auto& slider = ui::slider(*card, juce::String(prefix) + fields[i]);
                expect(slider.getSliderStyle() == juce::Slider::LinearHorizontal,
                       "Compact Lead/voice cards must use horizontal sliders");
                auto& control = *slider.getParentComponent();
                expect(control.getBounds() == juce::Rectangle<int>(8 + int(i % 2) * 78, 52 + int(i / 2) * 54, 78, 54),
                       "Compact card must use two columns and four fixed-height rows");
            }
        }
        auto& lead = ui::slider(stack, "leadPitch");
        peerClick(*editor, *lead.getParentComponent()->getParentComponent(), {20, 15});
        expect(!owner.settings().advanced, "Selecting Lead unexpectedly enabled generated voices");
        auto& voice = *ui::slider(stack, "voice1Pitch").getParentComponent()->getParentComponent();
        peerClick(*editor, voice, {20, 15});
        expect(owner.settings().advanced && int(owner.getRoutingLayout().getProperty("selectedVoiceSlot", -99)) == 0,
               "Real voice-card click must auto-enable legacy Advanced and select the voice: active="
                                  + juce::String(int(owner.settings().advanced)) + " slot=" + owner.getRoutingLayout().getProperty("selectedVoiceSlot", -99).toString());
        action(*editor, "Routing");
        auto& routing = component<vocalstack::RoutingPanel>(*editor);
        auto& inspector = *ui::slider(routing, "voice1Pitch").getParentComponent()->getParentComponent();
        const auto checkInspector = [&] {
            const std::array<const char*, 8> fields{"Pitch", "Detune", "Formant", "Gain", "Pan", "Delay", "Depth", "Rate"};
            const int width = (inspector.getWidth() - 16) / 4;
            for (size_t i = 0; i < fields.size(); ++i) {
                auto& slider = ui::slider(inspector, "voice1" + juce::String(fields[i]));
                expect(slider.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag,
                       "Voice inspector must retain larger rotary knobs");
                auto& control = *slider.getParentComponent();
                expect(control.getBounds() == juce::Rectangle<int>(8 + int(i % 4) * width, 38 + int(i / 4) * 104, width, 104),
                       "Voice inspector must retain four columns and two fixed 104px rows");
            }
        };
        checkInspector(); editor->setSize(1480, 1600); checkInspector();
        expectPassiveControls(*editor);
    }
}

void modulationRailGeometry() {
    for (const bool neon : {false, true}) {
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage; owner.setFlash(neon);
        vocalstack::ModulationState state;
        state.modulators = {{1}, {2, vocalstack::ModulatorType::random}};
        juce::String error;
        expect(owner.setModulationState(state, error), "Rail fixture rejected");
        vocalstack::RoutingPanel routing(owner);
        routing.addToDesktop(0); routing.setVisible(true); routing.selectVoice(0);
        auto& nodeKnob = ui::slider(routing, "voice1Pitch");
        juce::Component::SafePointer<juce::Slider> retained(&nodeKnob);
        auto* nodeViewport = nodeKnob.findParentComponentOfClass<juce::Viewport>();
        expect(nodeViewport != nullptr, "Missing voice inspector viewport");
        auto& nodeInspector = *nodeViewport->getParentComponent();
        const auto inspectorBounds = nodeInspector.getBounds();
        expect(inspectorBounds == juce::Rectangle<int>(948, 76, 320, 676),
               "Unselected LFO must leave full-height node inspector");
        auto& canvas = *routing.getChildComponent(0);
        auto& strip = component<vocalstack::ModulationPanel>(routing);
        expect(!strip.isVisible(), "Modulator strip should be hidden until selected");
        auto* lfo = findButton(routing, "LFO 1");
        auto* random = findButton(routing, "Random 2");
        expect(lfo && random, "Missing rail modulator tiles");
        auto* rail = lfo->findParentComponentOfClass<juce::Viewport>();
        expect(rail && rail->getBounds() == juce::Rectangle<int>(12, 106, 132, 654)
                   && lfo->getBounds() == juce::Rectangle<int>(0, 0, 132 - rail->getScrollBarThickness(), 82)
                   && random->getY() == 88, "Modulator tiles must form a vertical left rail");
        const auto graph = graphText(owner.getRoutingGraph());
        action(routing, "LFO 1");
        expect(retained && retained->isShowing() && routingPolish::labelIs(routing, "VOICE 1"),
               "Selecting LFO destroyed/replaced the node inspector");
        expect(strip.getParentComponent() == &routing && strip.isShowing()
                   && strip.getBounds() == juce::Rectangle<int>(156, 520, 772, 240)
                   && canvas.getBounds() == juce::Rectangle<int>(156, 70, 772, 442),
               "LFO bottom strip must span only the audio canvas width");
        expect(nodeInspector.getBounds() == inspectorBounds && !strip.getBounds().intersects(inspectorBounds),
               "Selecting LFO must not shorten or overlap the full-height node inspector");
        saveSnapshot(routing, neon ? "routing-lfo-inspector-neon.png" : "routing-lfo-inspector-device.png");
        auto* viewport = ui::find<juce::Viewport>(strip, [](auto& v) { return v.getViewedComponent() != nullptr; });
        expect(viewport != nullptr, "Missing bottom-strip scroll viewport");
        const auto checkControls = [&] {
            const std::array<const char*, 6> fields{
                "Rate / Hz", "Beats / cycle", "Phase / turns", "Output amount", "Phase skew", "Random smoothing"};
            for (size_t i = 0; i < fields.size(); ++i) {
                auto* knob = ui::find<juce::Slider>(strip, [&](auto& s) { return s.getName() == fields[i]; });
                expect(knob && knob->isVisible() && knob->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag,
                       "Missing bottom-strip rotary: " + juce::String(fields[i]));
                expect(knob->getParentComponent()->getBounds()
                           == juce::Rectangle<int>(338 + int(i % 3) * 52, 40 + int(i / 3) * 76, 52, 76),
                       "LFO knobs must retain three columns and two fixed 52x76px rows: " + juce::String(fields[i]));
            }
            int selectors = 0;
            for (auto* child : viewport->getViewedComponent()->getChildren()) {
                if (auto* selector = dynamic_cast<juce::ComboBox*>(child)) {
                    expect(selector->getBounds() == juce::Rectangle<int>(234, 40 + selectors * 30, 96, 26),
                           "LFO selectors must retain half-width 96px geometry");
                    ++selectors;
                }
            }
            expect(selectors == 3, "LFO must retain all three half-width selectors");
            auto* sync = findButton(strip, "Sync");
            auto* bipolar = findButton(strip, "Bipolar");
            expect(sync && bipolar && sync->getBounds() == juce::Rectangle<int>(234, 130, 48, 26)
                       && bipolar->getBounds() == juce::Rectangle<int>(282, 130, 48, 26),
                   "Sync and Bipolar must share the 96px selector width");
            expect(viewport->getViewedComponent()->getWidth() == juce::jmax(660, strip.getWidth() - viewport->getScrollBarThickness()),
                   "LFO content must retain a 660px minimum width instead of shrinking controls");
        };
        checkControls();
        for (const auto size : {juce::Point<int>(904, 442), juce::Point<int>(1600, 1000)}) {
            routing.setSize(size.x, size.y);
            checkControls();
            expect(retained && retained->isShowing(), "Resizing strip lost node inspector");
            expect(nodeInspector.getBounds() == juce::Rectangle<int>(size.x - 332, 76, 320, size.y - 124),
                   "Selected LFO must leave inspector at full available height after resizing");
            const int stripHeight = size.y == 442 ? 224 : 240;
            expect(strip.getBounds() == juce::Rectangle<int>(156, size.y - 40 - stripHeight, size.x - 508, stripHeight)
                       && canvas.getBounds() == juce::Rectangle<int>(156, 70, size.x - 508, size.y - 118 - stripHeight)
                       && strip.getY() == canvas.getBottom() + 8 && !strip.getBounds().intersects(nodeInspector.getBounds()),
                   "Resized LFO strip must stay below canvas and outside inspector");
        }
        routing.setSize(904, 442);
        checkControls();
        viewport->setViewPosition(10000, 0);
        expect(viewport->getViewPositionX() > 0, "Narrow bottom strip must scroll instead of shrinking controls");
        action(routing, "Random 2");
        expect(viewport->getViewPositionX() == 0 && strip.selectedId() == 2 && retained,
               "Switching modulators must reset scroll without replacing node inspector");
        expect(nodeInspector.getBounds() == juce::Rectangle<int>(572, 76, 320, 318),
               "Switching modulators shortened the node inspector");
        routing.selectVoice(-1);
        expect(strip.isShowing() && strip.selectedId() == 2 && routingPolish::labelIs(routing, "LEAD"),
               "Selecting audio node closed the modulator strip");
        expect(graphText(owner.getRoutingGraph()) == graph, "Modulator selection changed audio wiring");
        action(strip, "Delete modulator");
        expect(!strip.isVisible() && routingPolish::labelIs(routing, "LEAD"),
               "Deleting modulator must hide only the bottom strip");
        expect(nodeInspector.getBounds() == juce::Rectangle<int>(572, 76, 320, 318)
                   && canvas.getBottom() == routing.getHeight() - 48,
               "Deleting modulator must restore canvas height without changing inspector height");
        expectPassiveControls(routing);
    }
}

void resizableInspector() {
    for (const bool neon : {false, true}) {
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage; owner.setFlash(neon); owner.setAdvancedEnabled(true);
        vocalstack::ModulationState state; state.modulators = {{1}};
        juce::String error;
        expect(owner.setModulationState(state, error), "Divider modulation fixture rejected");
        auto routing = std::make_unique<vocalstack::RoutingPanel>(owner);
        routing->addToDesktop(0); routing->setVisible(true); routing->selectVoice(0);
        action(*routing, "LFO 1"); pumpTimers();
        const auto graph = graphText(owner.getRoutingGraph());
        const auto modulation = owner.getModulationState();
        std::vector<float> bases;
        for (auto* p : owner.getParameters()) bases.push_back(p->getValue());
        auto originalLayout = owner.getRoutingLayout().createCopy();
        originalLayout.removeProperty("inspectorWidth", nullptr);

        for (const int width : {480, 320, 416}) {
            auto& slider = ui::slider(*routing, "voice1Pitch");
            juce::Component::SafePointer<juce::Slider> retainedSlider(&slider);
            auto& voice = *slider.getParentComponent()->getParentComponent();
            juce::Component::SafePointer<juce::Component> retainedVoice(&voice);
            auto* viewport = slider.findParentComponentOfClass<juce::Viewport>();
            expect(viewport != nullptr, "Divider fixture has no voice viewport");
            auto& inspector = *viewport->getParentComponent();
            auto* divider = ui::find<juce::Component>(*routing, [](auto& c) { return c.getName() == "Resize node inspector"; });
            expect(divider && divider->isShowing() && divider->getWidth() == 10,
                   "Missing visible 10px inspector resize divider");
            auto& canvas = *routing->getChildComponent(0);
            auto& strip = component<vocalstack::ModulationPanel>(*routing);
            const auto check = [&](int actualWidth) {
                expect(inspector.getBounds() == juce::Rectangle<int>(1280 - actualWidth - 12, 76, actualWidth, 676),
                       "Divider must retain full inspector height with LFO selected");
                expect(divider->getBounds() == juce::Rectangle<int>(inspector.getX() - 13, 44, 10, 708),
                       "Divider did not follow inspector edge");
                expect(strip.isShowing() && strip.selectedId() == 1 && strip.getX() == canvas.getX()
                           && strip.getWidth() == canvas.getWidth() && strip.getHeight() == 240
                           && strip.getY() == 520 && canvas.getY() == 70 && canvas.getHeight() == 442
                           && canvas.getWidth() >= 100 && !strip.getBounds().intersects(inspector.getBounds()),
                       "Divider changed selected LFO or overlapped its canvas-only strip");
                expect(retainedSlider && retainedVoice && &ui::slider(*routing, "voice1Pitch") == retainedSlider.getComponent()
                           && slider.getParentComponent()->getParentComponent() == retainedVoice.getComponent(),
                       "Divider rebuilt the live voice inspector instead of resizing it");
            };
            const auto from = juce::Point<float>(5, 40);
            const int delta = width == 480 ? -10000 : width == 320 ? 10000 : inspector.getWidth() - width;
            const auto to = from.translated(float(delta), 0);
            divider->mouseDown(ui::mouse(*divider, from, from));
            divider->mouseDrag(ui::mouse(*divider, to, from, true));
            check(width); pumpTimers(); check(width);
            divider->mouseUp(ui::mouse(*divider, to, from, true, 0));
            pumpTimers(); check(width);
            expect(int(owner.getRoutingLayout().getProperty("inspectorWidth", -1)) == width,
                   "Divider release did not persist constrained desired inspector width");
            if (width == 480) {
                saveSnapshot(*routing, neon ? "routing-lfo-inspector-wide-neon.png" : "routing-lfo-inspector-wide-device.png");
                routing->setSize(600, 800);
                expect(inspector.getWidth() == 312 && canvas.getWidth() == 100 && strip.getWidth() == 100,
                       "Narrow window must constrain inspector to preserve 100px canvas");
                expect(inspector.getHeight() == 676 && strip.isShowing() && retainedSlider && retainedVoice,
                       "Narrow window lost full-height inspector or selected LFO");
                expect(int(owner.getRoutingLayout().getProperty("inspectorWidth", -1)) == 480,
                       "Temporary window constraint overwrote desired inspector width");
                routing->setSize(1280, 800); check(480);
            }
            expect(graphText(owner.getRoutingGraph()) == graph && owner.getRoutingError().isEmpty(),
                   "Resizing inspector changed audio graph");
            auto layout = owner.getRoutingLayout().createCopy(); layout.removeProperty("inspectorWidth", nullptr);
            expect(layout.toXmlString() == originalLayout.toXmlString(),
                   "Divider changed node positions, pan, zoom or selection");
            expect(owner.getModulationState().modulators.size() == modulation.modulators.size()
                       && owner.getModulationState().assignments.size() == modulation.assignments.size(),
                   "Divider changed modulation model");
            for (int i = 0; i < owner.getParameters().size(); ++i)
                expectNear(owner.getParameters()[i]->getValue(), bases[size_t(i)], "Divider wrote a host parameter");
            expectPassiveControls(*routing);

            routing.reset();
            routing = std::make_unique<vocalstack::RoutingPanel>(owner);
            routing->addToDesktop(0); routing->setVisible(true); pumpTimers();
            auto& restoredSlider = ui::slider(*routing, "voice1Pitch");
            auto* restoredViewport = restoredSlider.findParentComponentOfClass<juce::Viewport>();
            expect(restoredViewport && restoredViewport->getParentComponent()->getWidth() == width,
                   "Recreated routing panel lost saved inspector width");
            auto& restoredStrip = component<vocalstack::ModulationPanel>(*routing);
            expect(restoredStrip.isShowing() && restoredStrip.selectedId() == 1
                       && restoredViewport->getParentComponent()->getHeight() == 676,
                   "Recreated routing panel lost simultaneous LFO and full-height voice inspector");
            expect(graphText(owner.getRoutingGraph()) == graph, "Recreating inspector changed audio graph");
        }
    }
}

void resizableModulationPanel() {
    for (const bool neon : {false, true}) {
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage; owner.setFlash(neon); owner.setAdvancedEnabled(true);
        vocalstack::ModulationState state; state.modulators = {{1}};
        state.assignments.push_back({1, "voice1Gain", -0.15f, true});
        juce::String error;
        expect(owner.setModulationState(state, error), "Vertical divider fixture rejected");
        auto routing = std::make_unique<vocalstack::RoutingPanel>(owner);
        routing->addToDesktop(0); routing->setVisible(true); routing->selectVoice(0);
        auto* divider = ui::find<juce::Component>(*routing, [](auto& c) { return c.getName() == "Resize modulation panel"; });
        expect(divider && !divider->isVisible(), "Vertical divider must be hidden without a selected modulator");
        action(*routing, "LFO 1"); pumpTimers();
        expect(component<vocalstack::ModulationPanel>(*routing).getHeight() == 240,
               "Missing modulationHeight must default to the preferred 240px height");
        const auto graph = graphText(owner.getRoutingGraph());
        const auto modulationXml = [&] {
            auto xml = juce::parseXML(owner.settingsText());
            expect(xml && xml->getChildByName("Modulation"), "Missing serialized modulation fixture");
            return xml->getChildByName("Modulation")->toString();
        };
        const auto modulation = modulationXml();
        std::vector<float> bases;
        for (auto* p : owner.getParameters()) bases.push_back(p->getValue());
        auto originalLayout = owner.getRoutingLayout().createCopy();
        originalLayout.removeProperty("modulationHeight", nullptr);
        const auto checkState = [&] {
            expect(graphText(owner.getRoutingGraph()) == graph && owner.getRoutingError().isEmpty(),
                   "Vertical divider changed audio graph");
            auto layout = owner.getRoutingLayout().createCopy(); layout.removeProperty("modulationHeight", nullptr);
            expect(layout.toXmlString() == originalLayout.toXmlString(),
                   "Vertical divider changed inspector width, node positions, pan, zoom or selection");
            expect(modulationXml() == modulation, "Vertical divider changed modulation model");
            for (int i = 0; i < owner.getParameters().size(); ++i)
                expectNear(owner.getParameters()[i]->getValue(), bases[size_t(i)], "Vertical divider wrote a host parameter");
        };

        for (const int height : {480, 160, 336}) {
            auto& slider = ui::slider(*routing, "voice1Pitch");
            juce::Component::SafePointer<juce::Slider> retainedSlider(&slider);
            auto& voice = *slider.getParentComponent()->getParentComponent();
            juce::Component::SafePointer<juce::Component> retainedVoice(&voice);
            auto* viewport = slider.findParentComponentOfClass<juce::Viewport>();
            expect(viewport != nullptr, "Vertical divider fixture has no voice viewport");
            auto& inspector = *viewport->getParentComponent();
            auto& canvas = *routing->getChildComponent(0);
            auto& strip = component<vocalstack::ModulationPanel>(*routing);
            divider = ui::find<juce::Component>(*routing, [](auto& c) { return c.getName() == "Resize modulation panel"; });
            expect(divider && divider->isShowing(), "Missing visible vertical resize divider");
            const auto check = [&](int actualHeight) {
                expect(inspector.getBounds() == juce::Rectangle<int>(948, 76, 320, routing->getHeight() - 124),
                       "Vertical divider shortened or moved the full-height inspector");
                expect(strip.isShowing() && strip.selectedId() == 1
                           && strip.getBounds() == juce::Rectangle<int>(156, routing->getHeight() - 40 - actualHeight, 772, actualHeight)
                           && canvas.getBounds() == juce::Rectangle<int>(156, 70, 772, routing->getHeight() - 118 - actualHeight)
                           && canvas.getHeight() >= 100 && !strip.getBounds().intersects(inspector.getBounds()),
                       "Vertical divider lost selection, canvas minimum or independent strip geometry");
                expect(divider->getBounds() == juce::Rectangle<int>(canvas.getX(), canvas.getBottom(), canvas.getWidth(), 8)
                           && divider->getBottom() == strip.getY(),
                       "Vertical divider must fill the 8px gap above the modulation panel");
                expect(retainedSlider && retainedVoice && retainedSlider->isShowing()
                           && &ui::slider(*routing, "voice1Pitch") == retainedSlider.getComponent()
                           && slider.getParentComponent()->getParentComponent() == retainedVoice.getComponent(),
                       "Vertical divider rebuilt the live voice inspector");
            };
            const auto from = juce::Point<float>(40, 4);
            const int delta = height == 480 ? -10000 : height == 160 ? 10000 : strip.getHeight() - height;
            const auto to = from.translated(0, float(delta));
            divider->mouseDown(ui::mouse(*divider, from, from));
            divider->mouseDrag(ui::mouse(*divider, to, from, true));
            check(height); pumpTimers(); check(height);
            divider->mouseUp(ui::mouse(*divider, to, from, true, 0));
            pumpTimers(); check(height);
            expect(int(owner.getRoutingLayout().getProperty("modulationHeight", -1)) == height,
                   "Vertical divider release did not persist clamped desired height");
            if (height == 480) {
                routing->setSize(1280, 442); pumpTimers(); check(224);
                expect(int(owner.getRoutingLayout().getProperty("modulationHeight", -1)) == 480,
                       "Temporary short window overwrote desired modulation height");
                routing->setSize(1280, 800); pumpTimers(); check(480);
            }
            checkState(); expectPassiveControls(*routing);
            routing.reset();
            routing = std::make_unique<vocalstack::RoutingPanel>(owner);
            routing->addToDesktop(0); routing->setVisible(true); pumpTimers();
            auto& restoredStrip = component<vocalstack::ModulationPanel>(*routing);
            auto* restoredViewport = ui::slider(*routing, "voice1Pitch").findParentComponentOfClass<juce::Viewport>();
            expect(restoredStrip.isShowing() && restoredStrip.selectedId() == 1 && restoredStrip.getHeight() == height
                       && restoredViewport && restoredViewport->getParentComponent()->getBounds() == juce::Rectangle<int>(948, 76, 320, 676),
                   "Recreated panel lost saved modulation height or full-height voice inspector");
            checkState();
        }
    }
}

void leadMonitor() {
    static_assert(vocalstack::VoiceMonitor::leadSlot == 8);
    for (const bool neon : {false, true}) {
        vocalstack::VoiceMonitor monitor; monitor.setNeon(neon); monitor.setSize(904, 100);
        std::array<vocalstack::VoiceSnapshot, 8> voices{};
        voices[0] = {true, true, 0, 0, 0, 0, 0, 0, .2f};
        auto lead = voices[0];
        size_t selected = 99; monitor.onSelect = [&](size_t slot) { selected = slot; };
        monitor.update(voices, lead);
        ui::click(monitor, {452, 52});
        expect(selected == 8, "Overlapping Lead marker must win and emit index 8");
        monitor.select(8); monitor.select(999); ui::render(monitor);
        monitor.update(voices); selected = 99;
        ui::click(monitor, {452, 52});
        expect(selected == 0, "Legacy eight-voice update must clear optional Lead marker");
        voices = {}; monitor.update(voices, lead); selected = 99;
        ui::click(monitor, {452, 52});
        expect(selected == 8, "Lead-only monitor cannot be selected");
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage; owner.setAdvancedEnabled(true);
        setParameter(owner, "leadPan", .4f); setParameter(owner, "leadGain", -6);
        owner.prepareToPlay(48000, 256);
        expect(owner.getLeadSnapshot().present && owner.getLeadSnapshot().activity == 0,
               "Prepare must publish present, silent Lead snapshot");
        juce::AudioBuffer<float> audio(2, 256); juce::MidiBuffer midi;
        for (int block = 0; block < 160; ++block) {
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) audio.setSample(ch, i, .25f);
            owner.processBlock(audio, midi);
        }
        const auto snapshot = owner.getLeadSnapshot();
        expect(snapshot.present && snapshot.enabled && std::isfinite(snapshot.activity) && snapshot.activity > .01f,
               "Processor must publish actual active Lead signal");
        expectNear(snapshot.pan, .4, "Lead snapshot pan");
        expectNear(snapshot.gainDb, -6, "Lead snapshot gain");
        expect(owner.getVoiceSnapshots().size() == 8, "Lead must not replace a generated voice slot");
        vocalstack::DoublerPanel stack(owner); stack.setSize(904, 430); stack.sync();
        auto& stackMonitor = component<vocalstack::VoiceMonitor>(stack);
        int selectedSlot = -99; stack.onVoiceSelected = [&](int slot) { selectedSlot = slot; };
        stackMonitor.onSelect(8);
        expect(selectedSlot == -1, "Stack must map monitor index 8 to Lead inspector slot -1");
        owner.reset();
        expect(owner.getLeadSnapshot().present && owner.getLeadSnapshot().activity == 0,
               "Reset must clear published Lead activity");
        owner.releaseResources();
    }
}
void modulatedWaveshaperCurve() {
    using namespace vocalstack;
    for (const bool neon : {false, true}) {
        std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
        Processor& owner = *ownerStorage;
        setParameter(owner, "fx1WaveDrive", 6);
        setParameter(owner, "fx1WaveOutput", -6);
        setParameter(owner, "fx1Mix", 100);
        setParameter(owner, "fx1WaveAlgorithm", 1);
        setParameter(owner, "fx1WaveShapeAmount", 40);
        ModulationSnapshot synthetic;
        for (const auto* id : {"fx1WaveDrive", "fx1WaveOutput", "fx1Mix", "fx1WaveAlgorithm", "fx1WaveShapeAmount", "fx1On"}) {
            auto& p = parameter(owner, id);
            synthetic.targets = {{id, p.getValue(), .8f, 0}};
            expectNear(toneui::visualParameter(owner, id, -99, &synthetic), p.convertFrom0to1(.8f),
                       "Curve snapshot must convert normalized modulation in parameter units");
            synthetic.targets.front().effectiveNormalized = 2;
            expectNear(toneui::visualParameter(owner, id, -99, &synthetic), p.convertFrom0to1(1), "Curve snapshot upper clamp");
            synthetic.targets.front().effectiveNormalized = -1;
            expectNear(toneui::visualParameter(owner, id, -99, &synthetic), p.convertFrom0to1(0), "Curve snapshot lower clamp");
            synthetic.targets.front().effectiveNormalized = std::numeric_limits<float>::quiet_NaN();
            expectNear(toneui::visualParameter(owner, id, -99, &synthetic), parameterValue(owner, id), "Nonfinite snapshot fallback");
            synthetic.consistent = false;
            expectNear(toneui::visualParameter(owner, id, -99, &synthetic), parameterValue(owner, id), "Inconsistent snapshot fallback");
            synthetic.consistent = true;
        }
        synthetic.targets = {{"fx1WaveAlgorithm", 0, parameter(owner, "fx1WaveAlgorithm").convertTo0to1(5), 0}};
        expect(toneui::effectiveWaveShape(owner, 1, &synthetic) == 4, "Curve ignored effective algorithm");
        synthetic.targets.front().effectiveNormalized = 0;
        setParameter(owner, "fx1WaveShape", 2);
        expect(toneui::effectiveWaveShape(owner, 1, &synthetic) == 2, "Legacy shape fallback lost");

        ModulationState state; state.modulators = {{1}};
        state.modulators[0].waveform = ModulationWaveform::square;
        state.modulators[0].rateHz = .1f;
        for (const auto* id : {"fx1WaveDrive", "fx1WaveOutput", "fx1Mix", "fx1WaveShapeAmount"})
            state.assignments.push_back({1, id, .2f, true});
        juce::String error;
        expect(owner.setModulationState(state, error), "Live curve modulation fixture rejected: " + error);
        owner.prepareToPlay(48000, 64);
        toneui::WaveshaperVisual visual(owner, 1); visual.setNeon(neon); visual.setSize(360, 260);
        const auto verify = [&] {
            juce::AudioBuffer<float> audio(2, 64); audio.clear(); juce::MidiBuffer midi;
            owner.processBlock(audio, midi);
            const auto snapshot = owner.getModulationSnapshot();
            expect(snapshot.consistent && !snapshot.targets.empty(), "Live curve needs published effective targets");
            const auto actual = [&](const char* id) {
                for (const auto& target : snapshot.targets) if (target.parameterId == id)
                    return parameter(owner, id).convertFrom0to1(target.effectiveNormalized);
                return parameterValue(owner, id);
            };
            const auto image = ui::render(visual);
            auto bounds = visual.getLocalBounds().reduced(7); bounds.removeFromTop(16);
            const auto curve = bounds.removeFromTop(91).toFloat().withTrimmedLeft(24).withTrimmedRight(7);
            bool displaced = false;
            for (const double x : {-.5, -.25, .125, .25, .5}) {
                const double y = toneui::WaveshaperVisual::settledTransfer(x, actual("fx1WaveDrive"), actual("fx1WaveOutput"),
                    actual("fx1Mix") / 100, 0, true, actual("fx1WaveShapeAmount") / 100);
                const double base = toneui::WaveshaperVisual::settledTransfer(x, 6, -6, 1, 0, true, .4);
                displaced |= std::abs(y - base) * curve.getHeight() * .5 > 5;
                const auto p = juce::Point<float>(curve.getX() + float(x + 1) * .5f * curve.getWidth(),
                    curve.getCentreY() - float(juce::jlimit(-1.0, 1.0, y)) * curve.getHeight() * .5f).roundToInt();
                const auto accent = toneui::visualPalette(visual, neon).accent;
                // Thin strokes on integer graph edges cover only part of a pixel.
                const auto edgeAccent = toneui::visualPalette(visual, neon).inset.overlaidWith(accent.withAlpha(.55f));
                bool found = false;
                for (int py = p.y - 2; py <= p.y + 2; ++py) for (int px = p.x - 2; px <= p.x + 2; ++px) {
                    if (!image.getBounds().contains(px, py)) continue;
                    const auto pixel = image.getPixelAt(px, py);
                    for (const auto expected : {accent, edgeAccent})
                        found |= std::abs(int(pixel.getRed()) - int(expected.getRed())) < 35
                            && std::abs(int(pixel.getGreen()) - int(expected.getGreen())) < 35
                            && std::abs(int(pixel.getBlue()) - int(expected.getBlue())) < 35;
                }

                expect(found, "Rendered waveshaper curve does not follow effective modulation: x=" + juce::String(x) + " y=" + juce::String(y) + " neon=" + juce::String(int(neon)));
            }
            expect(displaced, "Curve regression fixture must distinguish modulated and base curves");
            for (const auto& target : snapshot.targets)
                expectNear(parameter(owner, target.parameterId).getValue(), target.baseNormalized, "Curve modulation wrote host base");
        };
        verify();
        state.modulators.front().phase = .75f;
        expect(owner.setModulationState(state, error), "Curve phase change rejected");
        owner.reset(); verify();
        expectNear(parameterValue(owner, "fx1WaveDrive"), 6, "Curve painting changed base Drive");
        owner.releaseResources();
    }
}

void menuCursorPlacement() {
    struct MenuProbe final : juce::LookAndFeel_V4 {
        juce::LookAndFeel* previous = &juce::LookAndFeel::getDefaultLookAndFeel();
        std::optional<juce::Rectangle<int>> target;
        MenuProbe() { juce::LookAndFeel::setDefaultLookAndFeel(this); }
        ~MenuProbe() override {
            juce::PopupMenu::dismissAllActiveMenus();
            juce::LookAndFeel::setDefaultLookAndFeel(previous);
        }
        juce::Component* getParentComponentForMenuOptions(const juce::PopupMenu::Options& options) override {
            target = options.getTargetScreenArea();
            return juce::LookAndFeel_V4::getParentComponentForMenuOptions(options);
        }
    };
    for (const bool neon : {false, true}) {
        MenuProbe probe;
        routingPolish::Fixture fixture(serialFxGraph(), neon);
        auto& canvas = fixture.canvas();
        const auto check = [&](juce::Point<float> position) {
            const auto cursor = juce::Desktop::getMousePosition();
            probe.target.reset();
            canvas.mouseDown(ui::mouse(canvas, position, position, false, juce::ModifierKeys::rightButtonModifier));
            expect(probe.target.has_value(), "Routing context menu did not supply popup options");
            expect(probe.target->getWidth() <= 1 && probe.target->getHeight() <= 1
                       && probe.target->getPosition().getDistanceFrom(cursor) <= 2,
                   "Routing menu must anchor to cursor, not canvas or toolbar bounds");
            juce::PopupMenu::dismissAllActiveMenus();
        };
        check(fixture.point(100, 20, 15));
        check({300, 350});
        const auto cursor = juce::Desktop::getMousePosition(); probe.target.reset();
        action(*fixture.panel, "+ Node");
        expect(probe.target && probe.target->getWidth() <= 1 && probe.target->getHeight() <= 1
                   && probe.target->getPosition().getDistanceFrom(cursor) <= 2,
               "Add Node menu must anchor to cursor too");
        juce::PopupMenu::dismissAllActiveMenus();
    }
}
} // namespace stackModulationUi
