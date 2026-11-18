// Link with the plugin/shared processor target, not just vocalstack_dsp.
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace vocalstack;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void processorApiAndRecall() {
    Processor owner;
    check(parameterIds.size() == 2378 && creativeParameterStart == 1007, "append-only creative parameter count/offset changed");
    for (size_t i = 0; i < parameterIds.size(); ++i)
        check(owner.getParameters()[int(i)] == owner.parameters.getParameter(parameterIds[i]), "parameter index changed");
    check(owner.settings().globalMix == 1 && owner.settings().leadSettings.enabled
        && owner.settings().leadSettings.depth == 0, "unsafe neutral defaults");
    auto* shapeParameter = owner.parameters.getParameter(toneParameterId(1, "WaveAlgorithm"));
    shapeParameter->setValueNotifyingHost(shapeParameter->convertTo0to1(19));
    check(owner.settings().fxSettings[1].waveshaper.shape == 18, "settings bridge truncated appended wave shape");
    shapeParameter->setValueNotifyingHost(shapeParameter->getDefaultValue());
    ModulationState state;
    Modulator lfo; lfo.id = 101; lfo.phase = 0.25f; lfo.retrigger = ModulationRetrigger::note;
    state.modulators.push_back(lfo);
    state.assignments.push_back({101, "leadPan", 0.25f, true});
    juce::String error;
    check(owner.setModulationState(state, error), "valid state rejected");
    owner.beginModulationAssignment(101);
    check(owner.modulationAssignmentId() == 101, "assignment mode not persistent");
    owner.prepareToPlay(48000, 137);
    juce::AudioBuffer<float> audio(2, 137); audio.clear(); juce::MidiBuffer midi;
    owner.processBlock(audio, midi);
    check(owner.parameters.getRawParameterValue("leadPan")->load() == 0, "modulation wrote host base value");
    bool found = false;
    for (const auto& target : owner.getModulationSnapshot().targets)
        if (target.parameterId == "leadPan") {
            found = target.effectiveNormalized > target.baseNormalized;
            check(target.contribution > 0, "missing effective contribution");
        }
    check(found, "no effective Lead target snapshot");
    const auto frame = owner.getModulationSnapshot();
    check(frame.consistent && frame.modulators.size() == 1, "missing coherent processor frame");
    for (const auto& target : frame.targets) if (target.parameterId == "leadPan")
        check(std::abs(target.effectiveNormalized - std::clamp(target.baseNormalized
            + frame.modulators[0].output * 0.25f, 0.0f, 1.0f)) < 0.003f,
            "target and source snapshots belong to different ticks");
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 17);
    owner.processBlock(audio, midi);
    const auto afterNote = owner.getModulationSnapshot();
    check(afterNote.consistent && afterNote.modulators.size() == 1 && std::abs(afterNote.modulators[0].phase - 0.252125f) < 1e-6f,
        "processor ignored sample-position MIDI note retrigger");
    check(owner.setModulationDepth(101, "leadPan", -0.3f), "signed depth edit failed");
    juce::MemoryBlock saved; owner.getStateInformation(saved);
    Processor recalled;
    recalled.setStateInformation(saved.getData(), int(saved.getSize()));
    const auto recalledState = recalled.getModulationState();
    check(recalledState.modulators.size() == 1 && recalledState.assignments.size() == 1
        && recalledState.assignments[0].depth == -0.3f, "modulation recall failed");
    for (const auto* id : {"leadOn", "doubleOn", "formantOn", "fx1WaveShape", "fx1WaveAlgorithm", "fx1WaveRaw", "fx1WaveQuality",
                           "fx1EqAdaptiveQ", "fx1BandCount", "fx1EqBand1Mode"}) {
        auto probe = state;
        probe.assignments.push_back({101, id, -0.75f, true});
        check(owner.setModulationState(probe, error), "discrete sound parameter rejected by modulation API");
    }
    check(owner.setModulationState(state, error), "restore test state after discrete coverage");
    auto invalid = state; invalid.assignments[0].targetParameterId = "bypass";
    check(!owner.setModulationState(invalid, error), "whole bypass accepted as a target");
    invalid = state; invalid.modulators[0].rateHz = std::numeric_limits<float>::quiet_NaN();
    check(!owner.setModulationState(invalid, error), "nonfinite controls accepted");
    invalid = state; invalid.modulators.resize(ModulationBackend::maxModulators + 1);
    check(!owner.setModulationState(invalid, error), "unbounded modulator list accepted");
    auto xml = juce::parseXML(owner.settingsText());
    check(xml != nullptr, "invalid settings XML");
    if (auto* mods = xml->getChildByName("Modulation")) xml->removeChildElement(mods, true);
    for (size_t i = originalParameterCount; i < parameterIds.size(); ++i) {
        for (auto* child = xml->getFirstChildElement(); child;) {
            auto* next = child->getNextElement();
            if (child->getStringAttribute("id") == parameterIds[i]) xml->removeChildElement(child, true);
            child = next;
        }
    }
    check(recalled.pasteSettings(xml->toString()), "legacy state rejected");
    check(recalled.getModulationState().modulators.empty() && recalled.settings().globalMix == 1
        && recalled.settings().leadSettings.pitchSemitones == 0, "old state inherited new controls");
    owner.endModulationAssignment();
    check(owner.modulationAssignmentId() == 0, "assignment mode did not end");
}
void localMixTargets() {
    Processor owner;
    check(localMixParameterStart == 944 && localMixParameterId(200) == "mix1Balance"
        && localMixParameterId(247) == "mix48Balance" && localMixParameterId(248).isEmpty(), "local mix stable IDs changed");
    RoutingNode mixer{200, NodeType::mix}; mixer.mix = .23f;
    Graph graph{{{1, NodeType::lead}, mixer, {13, NodeType::output}}, {{1, 200, 0}, {200, 13, 0}}};
    juce::String error;
    check(owner.setRoutingGraph(graph, error), "local mix graph rejected");
    auto* parameter = owner.parameters.getParameter("mix1Balance");
    check(std::abs(parameter->convertFrom0to1(parameter->getValue()) - 23) < 1e-5f, "node did not initialize mixer parameter");
    ModulationState state; Modulator source; source.id = 1; source.phase = .25f;
    state.modulators.push_back(source); state.assignments.push_back({1, "mix1Balance", 1, true});
    check(owner.setModulationState(state, error), "local mixer modulation rejected");
    owner.prepareToPlay(48000, 137);
    juce::AudioBuffer<float> buffer(2, 137); buffer.clear(); juce::MidiBuffer midi;
    owner.processBlock(buffer, midi);
    bool moved = false;
    for (const auto& target : owner.getModulationSnapshot().targets)
        if (target.parameterId == "mix1Balance") moved = target.effectiveNormalized > .9f;
    check(moved && owner.parameters.getRawParameterValue("mix1Balance")->load() == 23, "mixer modulation failed or wrote base");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(60));
    const auto cableOnly = owner.getRoutingGraph();
    check(owner.setRoutingGraph(cableOnly, error) && owner.parameters.getRawParameterValue("mix1Balance")->load() == 60,
        "cable edit rewound mixer automation");
    Processor recalled;
    check(recalled.pasteSettings(owner.settingsText()) && std::abs(recalled.settings().localMix[0] - .6f) < 1e-6f,
        "mixer parameter recall failed");
    Graph direct{{{1, NodeType::lead}, {13, NodeType::output}}, {{1, 13, 0}}};
    check(owner.setRoutingGraph(direct, error) && owner.getModulationState().assignments.empty(),
        "removed mixer retained assignments for a replacement");
    RoutingExecutor executor; executor.prepare(48000);
    check(bool(executor.requestGraph(graph)), "mixer executor graph rejected");
    RoutingSources sources; sources.lead = {.5f, -.5f};
    RoutingControls controls; controls.localMixAutomation = true; controls.localMix[0] = 0;
    auto audio = executor.process(sources, controls);
    check(audio.left == .5f && audio.right == -.5f, "local mixer dry automation not consumed");
    controls.localMix[0] = 1;
    for (int i = 0; i < 24000; ++i) audio = executor.process(sources, controls);
    check(audio.left == 0 && audio.right == 0, "local mixer wet endpoint not smoothed/consumed");
}

void qualityParametersAndRecall() {
    Processor owner;
    check(waveQualityParameterStart == 914 && parameterIds.size() == 2378, "quality index or expanded parameter count changed");
    check(owner.getParameters()[913] == owner.parameters.getParameter("fx15WaveShapeAmount"),
        "quality moved an existing wave parameter");
    for (size_t slot = 1; slot < 16; ++slot) {
        const auto id = toneParameterId(slot, "WaveQuality");
        check(id == "fx" + juce::String(int(slot)) + "WaveQuality", "quality resolver returned wrong ID");
        auto* parameter = owner.parameters.getParameter(id);
        check(parameter != nullptr, "quality parameter missing");
        const auto& range = parameter->getNormalisableRange();
        if (parameter != owner.getParameters()[int(914 + slot - 1)] || !parameter->isDiscrete()
            || parameter->getNumSteps() != 3 || range.start != 0 || range.end != 2
            || parameter->convertFrom0to1(parameter->getDefaultValue()) != 0)
            std::cerr << id << ": index=" << owner.getParameters().indexOf(parameter)
                << " discrete=" << parameter->isDiscrete() << " steps=" << parameter->getNumSteps()
                << " range=" << range.start << ".." << range.end
                << " interval=" << range.interval << " default="
                << parameter->convertFrom0to1(parameter->getDefaultValue()) << char(10);
        check(parameter == owner.getParameters()[int(914 + slot - 1)], "quality parameter index changed");
        check(parameter->isDiscrete(), "quality must explicitly advertise discrete host automation");
        check(parameter->getNumSteps() == 3 && range.start == 0 && range.end == 2 && range.interval == 1,
            "quality parameter range/step count changed");
        check(parameter->convertFrom0to1(parameter->getDefaultValue()) == 0, "quality parameter default changed");
        check(owner.settings().fxSettings[slot].waveshaper.quality == 0, "quality did not default Off");
    }
    check(waveLevelMatchParameterStart == 929, "level match moved prior parameters");
    for (size_t slot = 1; slot < 16; ++slot) {
        const auto id = toneParameterId(slot, "WaveLevelMatch");
        auto* p = owner.parameters.getParameter(id);
        check(p && owner.getParameters()[int(929 + slot - 1)] == p && p->isBoolean()
            && !owner.settings().fxSettings[slot].waveshaper.levelMatch, "level-match contract/default changed");
    }
    check(waveAlgorithmParameterStart == 992, "algorithm parameters moved prior IDs");
    auto* legacyShape = owner.parameters.getParameter("fx1WaveShape");
    auto* algorithm = owner.parameters.getParameter("fx1WaveAlgorithm");
    check(legacyShape->getNormalisableRange().end == 2 && algorithm->getNumSteps() == 20,
        "legacy shape automation range was remapped");
    legacyShape->setValueNotifyingHost(.5f);
    check(owner.settings().fxSettings[1].waveshaper.shape == 1, "legacy normalized Hard automation changed");
    algorithm->setValueNotifyingHost(algorithm->convertTo0to1(1));
    check(owner.settings().fxSettings[1].waveshaper.shape == 0, "expanded explicit Soft did not override legacy");
    algorithm->setValueNotifyingHost(0);
    check(owner.settings().fxSettings[1].waveshaper.shape == 1, "Follow Legacy did not restore old automation");
    RoutingNode wave{100, NodeType::waveshaper};
    wave.mix = 1; wave.waveshaper.raw = true; wave.waveshaper.shape = 18;
    wave.waveshaper.shapeAmount = 0.77f; wave.waveshaper.quality = 2; wave.waveshaper.levelMatch = true;
    Graph graph{{{1, NodeType::lead}, wave, {13, NodeType::output}}, {{1, 100, 0}, {100, 13, 0}}};
    juce::String error;
    check(owner.setRoutingGraph(graph, error), "quality graph rejected");
    const auto qualityId = toneParameterId(1, "WaveQuality");
    auto* quality = owner.parameters.getParameter(qualityId);
    const auto readQuality = [&] { return owner.parameters.getRawParameterValue(qualityId)->load(); };
    const auto graphQuality = [](const Processor& processor) {
        for (const auto& node : processor.getRoutingGraph().nodes)
            if (node.id == 100) return node.waveshaper.quality;
        throw std::runtime_error("quality test waveshaper missing");
    };
    check(owner.settings().fxSettings[1].waveshaper.levelMatch, "graph edit lost level match");
    check(readQuality() == 2 && owner.settings().fxSettings[1].waveshaper.raw
        && owner.settings().fxSettings[1].waveshaper.quality == 2 && graphQuality(owner) == 2,
        "graph edit lost quality or Raw's dormant setting");
    quality->setValueNotifyingHost(quality->convertTo0to1(1));
    check(graphQuality(owner) == 1, "graph snapshot did not follow automated base quality");
    auto edited = owner.getRoutingGraph();
    for (auto& node : edited.nodes) if (node.id == 100) node.waveshaper.quality = 0;
    check(owner.setRoutingGraph(edited, error) && readQuality() == 0, "graph quality edit lost against automation");
    quality->setValueNotifyingHost(quality->convertTo0to1(2));
    const auto cableOnly = owner.getRoutingGraph();
    check(owner.setRoutingGraph(cableOnly, error) && readQuality() == 2, "cable-only graph edit reset quality");
    juce::MemoryBlock saved;
    owner.getStateInformation(saved);
    Processor recalled;
    recalled.setStateInformation(saved.getData(), int(saved.getSize()));
    check(recalled.settings().fxSettings[1].waveshaper.levelMatch, "level match state recall failed");
    check(recalled.settings().fxSettings[1].waveshaper.quality == 2 && graphQuality(recalled) == 2,
        "quality binary state recall failed");
    auto xml = juce::parseXML(owner.settingsText());
    check(xml != nullptr, "quality settings XML missing");
    for (auto* child = xml->getFirstChildElement(); child;) {
        auto* next = child->getNextElement();
        if (child->hasTagName("PARAM") && child->getStringAttribute("id").endsWith("WaveQuality"))
            xml->removeChildElement(child, true);
        child = next;
    }
    check(recalled.pasteSettings(xml->toString()) && graphQuality(recalled) == 2,
        "missing quality PARAM did not migrate graph quality");
    for (const auto* graphType : {"RoutingGraph", "ActiveRoutingGraph"}) {
        auto* graphXml = xml->getChildByName(graphType);
        check(graphXml != nullptr, "quality graph XML missing");
        for (auto* node = graphXml->getFirstChildElement(); node; node = node->getNextElement())
            if (auto* waveXml = node->getChildByName("Waveshaper")) waveXml->removeAttribute("quality");
    }
    check(recalled.pasteSettings(xml->toString()), "pre-quality state rejected");
    for (size_t slot = 1; slot < 16; ++slot)
        check(recalled.settings().fxSettings[slot].waveshaper.quality == 0, "old state inherited previous quality");
    check(graphQuality(recalled) == 0, "legacy graph snapshot did not default quality Off");
    // PARAM is authoritative when an older graph snapshot disagrees.
    auto conflicting = juce::parseXML(owner.settingsText());
    check(conflicting != nullptr, "quality conflict XML missing");
    for (auto* child = conflicting->getFirstChildElement(); child; child = child->getNextElement())
        if (child->hasTagName("PARAM") && child->getStringAttribute("id") == qualityId)
            child->setAttribute("value", 1);
    check(recalled.pasteSettings(conflicting->toString()) && graphQuality(recalled) == 1
        && recalled.settings().fxSettings[1].waveshaper.quality == 1, "graph quality overrode host PARAM");
    quality->setValueNotifyingHost(quality->convertTo0to1(0));
    ModulationState state;
    Modulator lfo; lfo.id = 211; lfo.phase = 0.25f;
    state.modulators.push_back(lfo);
    state.assignments.push_back({211, qualityId, 1, true});
    check(owner.setModulationState(state, error), "quality rejected as a discrete target");
    owner.prepareToPlay(48000, 32);
    juce::AudioBuffer<float> audio(2, 32); audio.clear(); juce::MidiBuffer midi;
    owner.processBlock(audio, midi);
    const auto frame = owner.getModulationSnapshot();
    check(frame.consistent, "quality snapshot inconsistent");
    bool found = false;
    for (const auto& target : frame.targets) if (target.parameterId == qualityId) {
        found = true;
        check(target.baseNormalized == 0 && target.effectiveNormalized == 1, "quality did not modulate to 4x");
    }
    check(found && readQuality() == 0 && graphQuality(owner) == 0
        && owner.settings().fxSettings[1].waveshaper.raw,
        "quality modulation wrote host/graph base values or changed Raw");
}

void populatedGraphStress() {
    Processor owner;
    const auto set = [&](const juce::String& id, float value) {
        auto* p = owner.parameters.getParameter(id);
        check(p != nullptr, "stress parameter missing");
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    set("advancedOn", 1);
    for (size_t v = 0; v < 8; ++v) {
        set(voicePresentParameterId(v), 1); set(voiceParameterId(v, "On"), 1);
        set(voicePitchParameterId(v), v % 2 ? 7 : -5);
    }
    Graph graph = defaultGraph();
    graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [](auto& link) { return link.destination == 13; }), graph.links.end());
    uint32_t previous = 12;
    for (uint32_t id = 100; id <= 114; ++id) {
        RoutingNode wave{id, NodeType::waveshaper}; wave.mix = 1;
        wave.waveshaper.quality = 2; wave.waveshaper.shape = 11; wave.waveshaper.driveDb = 18;
        graph.nodes.push_back(wave); graph.links.push_back({previous, id, 0}); previous = id;
    }
    graph.links.push_back({previous, 13, 0});
    juce::String error;
    check(owner.setRoutingGraph(graph, error), "stress graph rejected");
    set("choraleOn", 1);
    ModulationState state;
    for (uint32_t id = 1; id <= 16; ++id) {
        Modulator source; source.id = id; source.rateHz = .1f + id * .13f;
        source.type = id % 2 ? ModulatorType::lfo : ModulatorType::random;
        source.smoothing = .7f; state.modulators.push_back(source);
        for (size_t v = 0; v < 8; ++v) {
            state.assignments.push_back({id, voiceParameterId(v, "Pan"), .01f, true});
            state.assignments.push_back({id, toneParameterId(v + 1, "WaveShapeAmount"), .01f, true});
        }
    }
    check(state.assignments.size() == 256 && owner.setModulationState(state, error), "stress assignments rejected");
    owner.prepareToPlay(48000, 137);
    juce::AudioBuffer<float> audio(2, 137); juce::MidiBuffer midi;
    const auto start = std::chrono::steady_clock::now();
    double checksum = 0;
    for (int offset = 0; offset < 48000; offset += audio.getNumSamples()) {
        for (int i = 0; i < audio.getNumSamples(); ++i) {
            const float x = .08f * std::sin(float(offset + i) * .03f);
            audio.setSample(0, i, x); audio.setSample(1, i, -x * .7f);
        }
        owner.processBlock(audio, midi);
        for (int channel = 0; channel < 2; ++channel) for (int i = 0; i < audio.getNumSamples(); ++i) {
            const float x = audio.getSample(channel, i);
            check(std::isfinite(x), "stress graph produced nonfinite audio"); checksum += std::abs(x);
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    check(checksum > 0, "stress graph went silent");
    std::cout << "Stress: 8 pitched voices, 16 FX (15 x 4x multi-fold), 16 modulators / 256 assignments: "
        << ms << " ms per audio second (informational, machine-dependent)" << char(10);
}

void creativeFilterStress() {
    for (double rate : {48000., 96000.}) {
        auto owner = std::make_unique<Processor>();
        const auto set = [&](const juce::String& id, float value) {
            auto* p = owner->parameters.getParameter(id);
            check(p != nullptr, "creative stress parameter missing");
            p->setValueNotifyingHost(p->convertTo0to1(value));
        };
        set("advancedOn", 1);
        for (size_t v = 0; v < 8; ++v) {
            set(voicePresentParameterId(v), 1); set(voiceParameterId(v, "On"), 1);
            set(voicePitchParameterId(v), v % 2 ? 7 : -5);
        }
        Graph graph = defaultGraph();
        graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [](auto& link) { return link.destination == 13; }), graph.links.end());
        constexpr NodeType types[] = {NodeType::delay, NodeType::reverb, NodeType::compressor,
            NodeType::bitcrusher, NodeType::granular, NodeType::xpFilter, NodeType::synthFilter,
            NodeType::gate, NodeType::stereo, NodeType::gain};
        uint32_t previous = 12;
        for (size_t i = 0; i < std::size(types); ++i) {
            RoutingNode node{uint32_t(100 + i), types[i]};
            node.mix = i < 5 ? .25f : 1.f;
            if (types[i] == NodeType::reverb) {
                node.creative[CreativeParameter::reverbPitchAmount] = 45;
                node.creative[CreativeParameter::reverbSwell] = 250;
            }
            if (types[i] == NodeType::synthFilter) {
                node.utility[UtilityParameter::synthMode] = 6;
                node.utility[UtilityParameter::synthResonance] = 35;
            }
            graph.nodes.push_back(node); graph.links.push_back({previous, node.id, 0}); previous = node.id;
        }
        graph.links.push_back({previous, 13, 0});
        juce::String error;
        check(owner->setRoutingGraph(graph, error), "creative stress graph rejected");
        ModulationState state;
        for (uint32_t id = 1; id <= 4; ++id) {
            Modulator source; source.id = id; source.rateHz = .15f * id;
            state.modulators.push_back(source);
            state.assignments.push_back({id, utilityParameterId(7, size_t(UtilityParameter::synthCutoff)), .03f, true});
            state.assignments.push_back({id, creativeParameterId(2, size_t(CreativeParameter::reverbPitchAmount)), .03f, true});
        }
        check(owner->setModulationState(state, error), "creative stress modulation rejected");
        owner->prepareToPlay(rate, 137);
        juce::AudioBuffer<float> audio(2, 137); juce::MidiBuffer midi;
        double checksum = 0, maxBlockMs = 0;
        const auto start = std::chrono::steady_clock::now();
        int samples = 0;
        for (; samples < int(rate * 2); samples += audio.getNumSamples()) {
            for (int i = 0; i < audio.getNumSamples(); ++i) {
                const float x = .08f * std::sin(float((samples + i) / rate * 1382.3));
                audio.setSample(0, i, x); audio.setSample(1, i, x * -.7f);
            }
            const auto blockStart = std::chrono::steady_clock::now();
            owner->processBlock(audio, midi);
            maxBlockMs = std::max(maxBlockMs, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - blockStart).count());
            for (int channel = 0; channel < 2; ++channel) for (int i = 0; i < audio.getNumSamples(); ++i) {
                const float x = audio.getSample(channel, i);
                check(std::isfinite(x), "creative stress nonfinite audio"); checksum += std::abs(x);
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        check(checksum > 0, "creative stress went silent");
        std::cout << "Creative/filter stress: 8 pitched voices, 10 serial FX (shimmer/swell/formant), 4 modulators at "
            << rate << " Hz: " << ms * rate / samples << " ms/audio second, max 137-sample block "
            << maxBlockMs << " ms (informational, machine-dependent)" << char(10);
        owner->releaseResources();
    }
}

void finalCreativeTarget() {
    Processor owner;
    const auto id = creativeParameterId(15, size_t(CreativeParameter::reverbSwell));
    auto* target = owner.parameters.getParameter(id);
    check(target && target->getParameterIndex() == 1771, "final creative target moved or is missing");
    target->setValueNotifyingHost(.25f);
    const float base = owner.parameters.getRawParameterValue(id)->load();
    ModulationState state;
    Modulator source; source.id = 71; source.phase = .25f;
    state.modulators.push_back(source);
    state.assignments.push_back({71, id, .25f, true});
    juce::String error;
    check(owner.setModulationState(state, error), "final creative target rejected by modulation capacity");
    juce::MemoryBlock saved; owner.getStateInformation(saved);
    Processor recalled;
    recalled.setStateInformation(saved.getData(), int(saved.getSize()));
    check(recalled.getModulationState().assignments.size() == 1
        && recalled.getModulationState().assignments[0].targetParameterId == id, "final creative assignment did not roundtrip");
    recalled.prepareToPlay(48000, 137);
    juce::AudioBuffer<float> audio(2, 137); audio.clear(); juce::MidiBuffer midi;
    recalled.processBlock(audio, midi);
    const auto frame = recalled.getModulationSnapshot();
    check(frame.consistent, "final creative snapshot is inconsistent");
    bool moved = false;
    for (const auto& value : frame.targets) if (value.parameterId == id)
        moved = value.effectiveNormalized > value.baseNormalized + .1f;
    check(moved, "final creative target did not receive live modulation");
    check(recalled.parameters.getRawParameterValue(id)->load() == base, "creative modulation changed host base value");
}

void discreteZeroCrossingAndIdle(double sampleRate, const char* parameterId = "leadOn") {
    const bool legacyInteger = juce::String(parameterId) == "fx1WaveShape";
    const float initialEffective = legacyInteger ? 1.0f : 0.0f;
    const float baseEffective = legacyInteger ? 0.0f : 1.0f;
    Processor owner;
    auto* parameter = owner.parameters.getParameter(parameterId);
    check(parameter != nullptr, "missing discrete dwell parameter");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(baseEffective));
    if (legacyInteger)
        check(!parameter->isDiscrete(),
            "legacy integer host metadata changed; test no longer exercises inherited false isDiscrete");
    ModulationState state;
    Modulator source;
    source.id = 17; source.phase = legacyInteger ? 0.0f : 0.5f;
    source.retrigger = ModulationRetrigger::note;
    source.waveform = ModulationWaveform::square;
    state.modulators.push_back(source);
    state.assignments.push_back({17, parameterId, 1, true});
    juce::String error;
    check(owner.setModulationState(state, error), "discrete dwell plan rejected");
    owner.prepareToPlay(sampleRate, 32);
    juce::AudioBuffer<float> audio(2, 32); audio.clear(); juce::MidiBuffer midi;
    const auto effectiveOn = [&] {
        const auto frame = owner.getModulationSnapshot();
        check(frame.consistent, "discrete snapshot was not coherent");
        for (const auto& target : frame.targets)
            if (target.parameterId == parameterId) return target.effectiveNormalized;
        throw std::runtime_error("missing discrete target");
    };
    owner.processBlock(audio, midi);
    check(effectiveOn() == initialEffective, "discrete target did not step to initial modulated value");
    state.modulators[0].depth = 0;
    check(owner.setModulationState(state, error), "zero-contribution plan rejected");
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 17);
    const int dwellSamples = int(sampleRate * 0.020);
    for (int sample = 32; sample <= dwellSamples + 64; sample += 32) {
        owner.processBlock(audio, midi);
        check(effectiveOn() == (sample < dwellSamples ? initialEffective : baseEffective),
            "zero contribution bypassed discrete dwell or dwell used an unset host sample rate");
    }
    check(owner.parameters.getRawParameterValue(parameterId)->load() == baseEffective, "discrete modulation wrote host value");
    check(owner.setModulationState({}, error), "clear modulation state rejected");
    owner.processBlock(audio, midi);
    owner.processBlock(audio, midi);
    const auto idle = owner.getModulationSnapshot();
    check(idle.consistent && idle.modulators.empty() && effectiveOn() == baseEffective,
        "idle fast path left stale source or effective target snapshots");
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI initialiser;
    try {
        processorApiAndRecall();
        qualityParametersAndRecall();
        localMixTargets();
        finalCreativeTarget();
        populatedGraphStress();
        creativeFilterStress();
        for (double rate : {48000.0, 96000.0}) {
            discreteZeroCrossingAndIdle(rate);
            discreteZeroCrossingAndIdle(rate, "fx1WaveShape");
            discreteZeroCrossingAndIdle(rate, "fx1delay_sync");
            discreteZeroCrossingAndIdle(rate, "fx15granular_freeze");
        }
        std::cout << "Modulation processor tests passed" << char(10);
    }
    catch (const std::exception& e) { std::cerr << e.what() << char(10); return 1; }
}
