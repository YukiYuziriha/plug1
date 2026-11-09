#pragma once

// Included by EditorTests.cpp inside its test namespace; uses its assertion and UI helpers.
juce::String graphText(const vocalstack::Graph& graph) {
    return vocalstack::processorRouting::encode(graph, "Graph").toXmlString();
}

vocalstack::Graph serialFxGraph(std::uint32_t id = 100) {
    return {{{1, vocalstack::NodeType::lead}, {id, vocalstack::NodeType::chorale},
             {13, vocalstack::NodeType::output}}, {{1, id, 0}, {id, 13, 0}}};
}

juce::ValueTree savedProcessorState(Processor& owner) {
    auto xml = juce::parseXML(owner.settingsText());
    expect(xml != nullptr, "Processor did not serialize XML");
    return juce::ValueTree::fromXml(*xml);
}

void expectSavedGraph(const juce::ValueTree& state, const char* name, const vocalstack::Graph& expected) {
    vocalstack::Graph decoded;
    expect(vocalstack::processorRouting::decode(state.getChildWithName(name), decoded),
           "Cannot decode saved graph: " + juce::String(name));
    expect(graphText(decoded) == graphText(expected), "Saved graph differs: " + juce::String(name));
}

void voiceLifecycle() {
    Processor owner;
    expectBasicVoices(owner, 0.35f);
    owner.setAdvancedEnabled(true);
    for (size_t slot = 2; slot < 8; ++slot) {
        expect(owner.addVoice(), "Could not add an available voice");
        const auto voices = owner.settings().voiceSettings;
        expect(voices[slot].present && voices[slot].enabled, "Add did not choose first absent stable slot");
        setParameter(owner, vocalstack::voicePitchParameterId(slot), float(slot) + 0.25f);
        setParameter(owner, vocalstack::voiceParameterId(slot, "Gain"), -float(slot));
        if (slot == 2) setParameter(owner, "voice3On", 0);
    }
    expect(!owner.addVoice(), "Ninth voice was accepted, or a muted voice was reused");
    const auto before = owner.settings().voiceSettings;
    const auto graph = owner.getRoutingGraph();
    std::uint32_t removedSource = 0;
    for (const auto& node : graph.nodes)
        if (node.type == vocalstack::NodeType::voice && node.voiceSlot == 3) removedSource = node.id;
    expect(removedSource != 0, "Added voice has no stable routing source");
    owner.removeVoice(3);
    expect(!owner.settings().voiceSettings[3].present, "Remove did not clear presence");
    expect(owner.settings().voiceSettings[3].enabled, "Remove incorrectly changed the stored mute parameter");
    for (const auto& link : owner.getRoutingGraph().links)
        expect(link.source != removedSource, "Removed voice retained an outgoing cable");
    for (size_t slot = 0; slot < 8; ++slot) {
        const auto voice = owner.settings().voiceSettings[slot];
        expectNear(voice.pitchSemitones, before[slot].pitchSemitones, "Removal renumbered or erased pitch");
        expectNear(voice.gainDb, before[slot].gainDb, "Removal renumbered or erased gain");
        if (slot != 3) expect(voice.present == before[slot].present && voice.enabled == before[slot].enabled,
                              "Removal changed a surviving voice");
    }
    expect(owner.addVoice(), "Removed slot could not be restored");
    expectNear(owner.settings().voiceSettings[3].pitchSemitones, 3.25f, "Re-add erased dormant pitch");
    bool reconnected = false;
    for (const auto& link : owner.getRoutingGraph().links) reconnected |= link.source == removedSource;
    expect(reconnected, "Re-add did not reconnect the same routing identity");
    const auto stableGraph = graphText(owner.getRoutingGraph());
    owner.removeVoice(99);
    expect(stableGraph == graphText(owner.getRoutingGraph()), "Out-of-range remove changed routing");
    setParameter(owner, "human", 81);
    setParameter(owner, "outputGain", -7);
    setParameter(owner, "fx15Mix", 87);
    owner.resetVoicesToBasic();
    expectBasicVoices(owner, 0.81f);
    expect(owner.settings().advanced, "Basic voice reset disabled Advanced");
    expectNear(parameterValue(owner, "outputGain"), -7, "Voice reset changed output");
    expectNear(parameterValue(owner, "fx15Mix"), 87, "Voice reset changed FX automation");
    for (const auto& node : graph.nodes) {
        if (node.type != vocalstack::NodeType::voice) continue;
        bool retained = false;
        for (const auto& current : owner.getRoutingGraph().nodes)
            retained |= current.id == node.id && current.type == node.type && current.voiceSlot == node.voiceSlot;
        expect(retained, "Basic reset destroyed stable source identity");
    }
}

void legacyFourVoiceMigration() {
    Processor source;
    setParameter(source, "voice1On", 0);
    setParameter(source, "voice3On", 0);
    setParameter(source, "voice4On", 1);
    setParameter(source, "voice3Gain", -17.5f);
    setParameter(source, "voice4Delay", 71.2f);
    auto legacy = savedProcessorState(source);
    for (size_t i = 46; i < vocalstack::parameterIds.size(); ++i)
        legacy.removeChild(legacy.getChildWithProperty("id", vocalstack::parameterIds[i]), nullptr);
    for (const auto* type : {"RoutingGraph", "ActiveRoutingGraph", "RoutingLayout"})
        legacy.removeChild(legacy.getChildWithName(type), nullptr);
    legacy.removeProperty("advancedInitialized", nullptr);
    Processor restored;
    expect(restored.pasteSettings(legacy.toXmlString()), "Legacy 46-parameter state rejected");
    for (size_t i = 0; i < 46; ++i)
        expectNear(parameterValue(restored, vocalstack::parameterIds[i]), parameterValue(source, vocalstack::parameterIds[i]),
                   "Migration changed an original parameter");
    for (size_t slot = 0; slot < 8; ++slot) {
        expect(restored.settings().voiceSettings[slot].present == (slot < 4),
               "Legacy migration must retain four cards including muted voices, not eight or two");
        expectNear(restored.settings().voiceSettings[slot].pitchSemitones, 0, "Legacy pitch must default to unison");
    }
    expect(!restored.settings().voiceSettings[0].enabled && !restored.settings().voiceSettings[2].enabled &&
           restored.settings().voiceSettings[3].enabled, "Migration lost legacy mute state");
    restored.setAdvancedEnabled(true);
    expectNear(parameterValue(restored, "voice3Gain"), -17.5f, "First activation overwrote migrated muted edits");
    expect(restored.settings().voiceSettings[3].present, "First activation removed migrated fourth voice");
    Processor roundTrip;
    juce::MemoryBlock state;
    restored.getStateInformation(state);
    roundTrip.setStateInformation(state.getData(), int(state.getSize()));
    for (size_t slot = 0; slot < 8; ++slot)
        expect(roundTrip.settings().voiceSettings[slot].present == (slot < 4), "Migrated presence failed binary roundtrip");
}

void musicalPitchParameters() {
    Processor owner;
    const std::array<float, 8> intervals{-36, -12, -0.25f, 0, 0.25f, 7.5f, 12, 36};
    for (size_t slot = 0; slot < 8; ++slot) {
        const auto* id = vocalstack::voicePitchParameterId(slot);
        auto& p = parameter(owner, id);
        expectNear(p.getNormalisableRange().start, -36, "Pitch lower bound");
        expectNear(p.getNormalisableRange().end, 36, "Pitch upper bound");
        expectNear(p.convertFrom0to1(p.getDefaultValue()), 0, "Pitch default");
        setParameter(owner, vocalstack::voiceParameterId(slot, "Detune"), -13.25f);
        setParameter(owner, vocalstack::voiceParameterId(slot, "Formant"), 4.5f);
        setParameter(owner, id, intervals[slot]);
        const auto voice = owner.settings().voiceSettings[slot];
        expectNear(voice.pitchSemitones, intervals[slot], "Fractional musical pitch bridge");
        expectNear(voice.detuneCents, -13.25f, "Pitch overwrote independent fine detune");
        expectNear(voice.formantSemitones, 4.5f, "Pitch overwrote independent formant");
        expect(voice.present == (slot < 2), "Pitch automation added a removed voice");
    }
    Processor restored;
    expect(restored.pasteSettings(owner.settingsText()), "Musical pitch state rejected");
    for (size_t slot = 0; slot < 8; ++slot)
        expectNear(restored.settings().voiceSettings[slot].pitchSemitones, intervals[slot], "Pitch state quantized or lost");
    restored.resetVoicesToBasic();
    expectBasicVoices(restored, 0.35f);
}

void routingStateRegression() {
    Processor owner;
    auto active = serialFxGraph();
    active.nodes[1].mix = 0.61f;
    active.nodes[1].motion = 0.27f;
    juce::String error;
    expect(owner.setRoutingGraph(active, error) && error.isEmpty(), "Valid serial graph rejected");
    juce::ValueTree layout("RoutingLayout"), position("NODE");
    layout.setProperty("zoom", 1.2, nullptr);
    layout.setProperty("panX", -31, nullptr);
    position.setProperty("id", 100, nullptr);
    position.setProperty("x", 321, nullptr);
    position.setProperty("y", 123, nullptr);
    layout.addChild(position, -1, nullptr);
    owner.setRoutingLayout(layout);
    const auto expectedLayout = owner.getRoutingLayout().toXmlString();
    position.setProperty("x", 999, nullptr);
    expect(owner.getRoutingLayout().toXmlString() == expectedLayout, "Layout retained mutable caller-owned data");
    auto returned = owner.getRoutingLayout();
    returned.setProperty("zoom", 0.4, nullptr);
    expect(owner.getRoutingLayout().toXmlString() == expectedLayout, "Layout getter leaked mutable state");
    Processor validRestore;
    expect(validRestore.pasteSettings(owner.settingsText()), "Valid routing restore failed");
    expect(graphText(validRestore.getRoutingGraph()) == graphText(active), "Valid graph changed on restore");
    expect(validRestore.getRoutingError().isEmpty(), "Valid restore reported draft error");
    auto draft = active;
    draft.links.pop_back();
    expect(!owner.setRoutingGraph(draft, error) && error.isNotEmpty(), "Incomplete output draft was published");
    expect(graphText(owner.getRoutingGraph()) == graphText(draft), "Invalid draft was not retained for editing");
    const auto saved = savedProcessorState(owner);
    expectSavedGraph(saved, "RoutingGraph", draft);
    expectSavedGraph(saved, "ActiveRoutingGraph", active);
    Processor restored;
    juce::MemoryBlock binary;
    owner.getStateInformation(binary);
    restored.setStateInformation(binary.getData(), int(binary.getSize()));
    expect(graphText(restored.getRoutingGraph()) == graphText(draft), "Binary state discarded incomplete draft");
    expect(restored.getRoutingError().isNotEmpty(), "Invalid restored draft has no diagnostic");
    expect(restored.getRoutingLayout().toXmlString() == expectedLayout, "Draft restore lost layout");
    expectSavedGraph(savedProcessorState(restored), "ActiveRoutingGraph", active);
    auto broken = saved.createCopy();
    auto malformed = broken.getChildWithName("ActiveRoutingGraph");
    malformed.removeAllChildren(nullptr);
    expect(restored.pasteSettings(broken.toXmlString()), "Invalid active graph should safely fall back");
    expectSavedGraph(savedProcessorState(restored), "ActiveRoutingGraph", vocalstack::defaultGraph());
    expect(graphText(restored.getRoutingGraph()) == graphText(draft), "Active fallback erased editable draft");
    auto malformedDraft = saved.createCopy();
    malformedDraft.getChildWithName("RoutingGraph").addChild(juce::ValueTree("Unknown"), -1, nullptr);
    expect(restored.pasteSettings(malformedDraft.toXmlString()), "Malformed draft restore failed");
    expect(restored.getRoutingError().isNotEmpty(), "Malformed draft did not report an error");
    expectSavedGraph(savedProcessorState(restored), "ActiveRoutingGraph", active);
    expect(graphText(restored.getRoutingGraph()) == graphText(vocalstack::defaultGraph()), "Malformed draft fallback is not safe");
    owner.resetParameters();
    expect(owner.getRoutingLayout().getNumChildren() == 0, "Full reset retained layout positions");
    expect(graphText(owner.getRoutingGraph()) == graphText(vocalstack::defaultGraph()), "Full reset retained custom routing");
}

std::vector<float> renderProcessorRegression(Processor& owner) {
    constexpr int rate = 48000, block = 137, length = rate;
    owner.prepareToPlay(rate, block);
    juce::MidiBuffer midi;
    std::vector<float> rendered;
    rendered.reserve(length);
    for (int position = 0; position < length; position += block) {
        const int count = std::min(block, length - position);
        juce::AudioBuffer<float> audio(2, count);
        for (int i = 0; i < count; ++i) {
            const float value = 0.1f * std::sin(float(position + i) * 0.037f);
            audio.setSample(0, i, value);
            audio.setSample(1, i, value);
        }
        owner.processBlock(audio, midi);
        for (int i = 0; i < count; ++i) {
            expect(std::isfinite(audio.getSample(0, i)) && std::isfinite(audio.getSample(1, i)),
                   "Processor graph emitted non-finite audio");
            rendered.push_back(audio.getSample(0, i));
        }
    }
    return rendered;
}

void fxAutomationRegression() {
    Processor owner;
    expect(juce::String(vocalstack::fxParameterId(0, "On")) == "choraleOn" &&
           juce::String(vocalstack::fxParameterId(0, "Mix")) == "choraleMix" &&
           juce::String(vocalstack::fxParameterId(0, "Motion")) == "choraleMotion", "Legacy FX slot mapping changed");
    for (size_t slot = 1; slot < 16; ++slot) {
        for (size_t field = 0; field < 3; ++field) {
            auto& p = parameter(owner, vocalstack::fxParameterId(slot, field));
            expect(owner.getParameters()[int(94 + (slot - 1) * 3 + field)] == &p, "FX automation index collision");
            expectNear(p.getNormalisableRange().start, 0, "FX parameter minimum");
            expectNear(p.getNormalisableRange().end, field == 0 ? 1 : 100, "FX parameter maximum");
            setParameter(owner, p.paramID, field == 0 ? float(slot % 2) : float(slot * 3 + field));
        }
    }
    const auto graphBefore = graphText(owner.getRoutingGraph());
    setParameter(owner, "fx15Mix", 91);
    expect(graphText(owner.getRoutingGraph()) == graphBefore, "FX automation rewrote routing topology");
    Processor restored;
    expect(restored.pasteSettings(owner.settingsText()), "FX automation state rejected");
    for (size_t slot = 0; slot < 16; ++slot)
        for (size_t field = 0; field < 3; ++field)
            expectNear(parameterValue(restored, vocalstack::fxParameterId(slot, field)),
                       parameterValue(owner, vocalstack::fxParameterId(slot, field)), "FX slots aliased or failed roundtrip");
    juce::String error;
    auto graph = serialFxGraph(114);
    graph.nodes[1].mix = 0.42f;
    graph.nodes[1].motion = 0.63f;
    expect(owner.setRoutingGraph(graph, error), "Highest fixed FX identity rejected");
    expectNear(parameterValue(owner, "fx15Mix"), 42, "New FX node did not seed its own slot");
    expectNear(parameterValue(owner, "fx15Motion"), 63, "New FX motion did not seed its own slot");
    setParameter(owner, "fx15Mix", 89);
    setParameter(owner, "fx15On", 0);
    expect(owner.setRoutingGraph(graph, error), "Unchanged topology rejected");
    expectNear(parameterValue(owner, "fx15Mix"), 89, "Topology publication erased host Mix automation");
    expectNear(parameterValue(owner, "fx15On"), 0, "Topology publication erased host On automation");
    auto legacyGraphState = savedProcessorState(owner);
    for (size_t field = 0; field < 3; ++field)
        legacyGraphState.removeChild(legacyGraphState.getChildWithProperty("id", vocalstack::fxParameterId(15, field)), nullptr);
    expect(restored.pasteSettings(legacyGraphState.toXmlString()), "Graph-only FX migration rejected");
    expectNear(parameterValue(restored, "fx15Mix"), 42, "Graph-only FX migration lost authored Mix");
    expectNear(parameterValue(restored, "fx15Motion"), 63, "Graph-only FX migration lost authored Motion");
    expect(owner.settings().fxAutomation, "Processor did not opt into fixed-slot FX automation");
    for (size_t slot = 0; slot < 16; ++slot) {
        const auto fx = owner.settings().fxSettings[slot];
        expect(fx.enabled == (parameterValue(owner, vocalstack::fxParameterId(slot, size_t(0))) >= 0.5f),
               "FX enable settings bridge uses the wrong slot");
        expectNear(fx.mix, parameterValue(owner, vocalstack::fxParameterId(slot, size_t(1))) * 0.01f,
                   "FX Mix settings bridge uses the wrong scale or slot");
        expectNear(fx.motion, parameterValue(owner, vocalstack::fxParameterId(slot, size_t(2))) * 0.01f,
                   "FX Motion settings bridge uses the wrong scale or slot");
    }
    for (const size_t slot : {size_t(1), size_t(15)}) {
        Processor audible;
        setParameter(audible, "levelOn", 0);
        setParameter(audible, "formantOn", 0);
        expect(audible.setRoutingGraph(serialFxGraph(std::uint32_t(slot + 99)), error), "FX audio fixture rejected");
        const auto unchanged = graphText(audible.getRoutingGraph());
        setParameter(audible, vocalstack::fxParameterId(slot, "On"), 0);
        const auto dry = renderProcessorRegression(audible);
        setParameter(audible, vocalstack::fxParameterId(slot, "On"), 1);
        setParameter(audible, vocalstack::fxParameterId(slot, "Mix"), 100);
        setParameter(audible, vocalstack::fxParameterId(slot, "Motion"), 83);
        const auto wet = renderProcessorRegression(audible);
        double difference = 0;
        for (size_t i = 24000; i < wet.size(); ++i) difference += std::abs(wet[i] - dry[i]);
        expect(difference / 24000 > 0.001, "Independent FX automation changed parameters but not rendered audio");
        expect(graphText(audible.getRoutingGraph()) == unchanged, "FX render automation changed topology");
    }
    // A serial lead graph sounds even when the main Mix is fully wet and every voice is muted.
    // Therefore falling back to defaultGraph instead of the retained active graph is observable.
    Processor fallback;
    setParameter(fallback, "levelOn", 0);
    setParameter(fallback, "formantOn", 0);
    setParameter(fallback, "advancedOn", 1);
    setParameter(fallback, "double", 100);
    for (size_t slot = 0; slot < 8; ++slot) setParameter(fallback, vocalstack::voiceParameterId(slot, "On"), 0);
    auto retained = serialFxGraph();
    retained.nodes[1].enabled = false;
    expect(fallback.setRoutingGraph(retained, error), "Retained audio fixture rejected");
    auto incomplete = retained;
    incomplete.links.pop_back();
    expect(!fallback.setRoutingGraph(incomplete, error), "Invalid audio draft accepted");
    Processor recalled;
    expect(recalled.pasteSettings(fallback.settingsText()), "Active fallback audio recall failed");
    const auto output = renderProcessorRegression(recalled);
    const int latency = recalled.getLatencySamples();
    for (int i = 24000; i < int(output.size()); ++i)
        expectNear(output[size_t(i)], 0.1f * std::sin(float(i - latency) * 0.037f),
                   "Invalid saved draft did not retain last-valid latency-aligned audio");
}

juce::MouseEvent routingMouse(juce::Component& target, juce::Point<float> point, bool dragged = false) {
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(), point,
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            &target, &target, now, point, now, 1, dragged};
}

void routingUiRegression() {
    for (const bool neon : {false, true}) {
        Processor owner;
        owner.setFlash(neon);
        const auto graph = serialFxGraph();
        juce::String error;
        expect(owner.setRoutingGraph(graph, error), "Routing UI fixture rejected");
        juce::ValueTree layout("RoutingLayout");
        layout.setProperty("zoom", 1, nullptr);
        layout.setProperty("panX", 0, nullptr);
        layout.setProperty("panY", 0, nullptr);
        const std::array<int, 3> ids{1, 100, 13};
        for (size_t i = 0; i < ids.size(); ++i) {
            juce::ValueTree node("NODE");
            node.setProperty("id", ids[i], nullptr);
            node.setProperty("x", int(20 + i * 220), nullptr);
            node.setProperty("y", 20, nullptr);
            layout.addChild(node, -1, nullptr);
        }
        owner.setRoutingLayout(layout);
        vocalstack::RoutingPanel panel(owner);
        panel.addToDesktop(0);
        auto* canvas = panel.getChildComponent(0);
        expect(canvas != nullptr && canvas->getBounds() == juce::Rectangle<int>(12, 70, 842, 588),
               "Routing canvas dimensions changed");
        const auto click = [&](const char* text) {
            auto* button = findButton(panel, text);
            expect(button != nullptr && button->isEnabled(), "Unavailable routing action: " + juce::String(text));
            button->onClick();
        };
        const auto transport = [&] {
            expectPassiveControls(panel);
            expect(panel.getPeer() != nullptr, "Routing panel has no native peer");
            expect(!panel.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey)), "Graph consumed host transport");
            expect(!panel.getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::returnKey)), "Graph consumed host Return");
        };
        transport();
        canvas->mouseDown(routingMouse(*canvas, {260, 35}));
        canvas->mouseDrag(routingMouse(*canvas, {260, 135}, true));
        canvas->mouseUp(routingMouse(*canvas, {260, 135}, true));
        expectNear(double(owner.getRoutingLayout().getChildWithProperty("id", 100).getProperty("y")), 120,
                   "Mouse drag did not persist node position");
        setParameter(owner, "fx1Mix", 79);
        click("Undo");
        expectNear(double(owner.getRoutingLayout().getChildWithProperty("id", 100).getProperty("y")), 20,
                   "Undo did not restore node position");
        expectNear(parameterValue(owner, "fx1Mix"), 79, "Position undo rewound host automation");
        click("Redo");
        expectNear(double(owner.getRoutingLayout().getChildWithProperty("id", 100).getProperty("y")), 120,
                   "Redo did not restore node position");
        click("Undo");
        // Drag an output onto an occupied input: keep an invalid draft, not a new audio plan.
        canvas->mouseDown(routingMouse(*canvas, {176, 60}));
        canvas->mouseDrag(routingMouse(*canvas, {240, 60}, true));
        canvas->mouseUp(routingMouse(*canvas, {240, 60}, true));
        expect(owner.getRoutingGraph().links.size() == 3 && owner.getRoutingError().isNotEmpty(),
               "Pin gesture did not retain duplicate-input draft");
        expectSavedGraph(savedProcessorState(owner), "ActiveRoutingGraph", graph);
        click("Undo");
        expect(graphText(owner.getRoutingGraph()) == graphText(graph) && owner.getRoutingError().isEmpty(),
               "Cable undo did not restore valid routing");
        click("Redo");
        expect(owner.getRoutingError().isNotEmpty(), "Cable redo did not restore invalid draft");
        click("Undo");
        canvas->mouseDown(routingMouse(*canvas, {260, 35}));
        canvas->mouseUp(routingMouse(*canvas, {260, 35}));
        click("Bypass FX");
        expectNear(parameterValue(owner, "fx1On"), 0, "FX bypass did not update host slot");
        click("Undo");
        expectNear(parameterValue(owner, "fx1On"), 1, "FX bypass undo lost enabled state");
        click("Redo");
        expectNear(parameterValue(owner, "fx1On"), 0, "FX bypass redo lost bypass state");
        expectNear(parameterValue(owner, "fx1Mix"), 79, "FX bypass history rewound unrelated Mix automation");
        // Host automation is authoritative even if the saved node flag is off.
        setParameter(owner, "fx1On", 1);
        canvas->mouseDown(routingMouse(*canvas, {260, 35}));
        canvas->mouseUp(routingMouse(*canvas, {260, 35}));
        auto* fxEnabled = findButton(panel, "FX enabled");
        expect(fxEnabled && fxEnabled->getToggleState(), "Inspector ignored host-enabled FX behind saved bypass flag");
        click("Bypass FX");
        expectNear(parameterValue(owner, "fx1On"), 0, "Bypass did not toggle the actual automated FX state");
        transport();
        canvas->mouseDown(routingMouse(*canvas, {260, 35}));
        canvas->mouseUp(routingMouse(*canvas, {260, 35}));
        click("Delete node");
        expect(owner.getRoutingGraph().nodes.size() == 2 && owner.getRoutingError().isNotEmpty(),
               "Delete did not retain incomplete draft");
        click("Undo");
        expect(owner.getRoutingGraph().nodes.size() == 3 && owner.getRoutingError().isEmpty(),
               "Delete undo did not restore FX node and cables");
        expectNear(parameterValue(owner, "fx1Mix"), 79, "Delete undo lost the independent FX slot");
        transport();
        saveSnapshot(panel, neon ? "routing-neon.png" : "routing-device.png");
    }
}
