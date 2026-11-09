#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ProcessorRoutingState.h"

namespace vocalstack {
namespace {
using State = juce::AudioProcessorValueTreeState;
constexpr auto stateType = "VocalStack";
constexpr auto themeProperty = "flash";
}

Processor::Processor()
    : AudioProcessor(BusesProperties()
        .withInput("Vocal In", juce::AudioChannelSet::stereo(), true)
        .withOutput("Stack Out", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, stateType, makeParameters()) {
    setLatencySamples(VocalEngine::defaultLatency);
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = parameters.getRawParameterValue(parameterIds[i]);
    for (size_t i = 10; i < parameterIds.size(); ++i)
        parameters.addParameterListener(parameterIds[i], this);
    startTimerHz(10);
}

Processor::~Processor() {
    stopTimer();
    for (size_t i = 10; i < parameterIds.size(); ++i)
        parameters.removeParameterListener(parameterIds[i], this);
}

void Processor::parameterChanged(const juce::String& id, float value) {
    // May run on the audio thread: record intent only, never mutate other params.
    if (id.startsWith("voice") || (id == "advancedOn" && value >= 0.5f))
        advancedInitialized.store(true, std::memory_order_relaxed);
}

State::ParameterLayout Processor::makeParameters() {
    State::ParameterLayout layout;
    const auto percentText = [](float value, int) { return juce::String(juce::roundToInt(value)) + " %"; };
    const auto formantText = [](float value, int) {
        return (value > 0 ? "+" : "") + juce::String(value, 1) + " st";
    };
    auto percent = [&](int index, const char* name, float initial) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{parameterIds[index], 1}, name,
            juce::NormalisableRange<float>{0.0f, 100.0f, 1.0f}, initial,
            juce::AudioParameterFloatAttributes{}.withLabel("%").withStringFromValueFunction(percentText)));
    };
    percent(0, "Leveler", 50);
    percent(1, "Mix", 35);
    percent(2, "Human", 35);
    percent(3, "Width", 65);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{parameterIds[4], 1}, "Formant",
        juce::NormalisableRange<float>{-24.0f, 24.0f, 0.1f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("st").withStringFromValueFunction(formantText)));
    const std::array<const char*, 4> names{"Leveler Active", "Doubler Active", "Formant Active", "Bypass"};
    for (size_t i = 0; i < names.size(); ++i)
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{parameterIds[i + 5], 1}, names[i], i != 3));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outputGain", 1}, "Output",
        juce::NormalisableRange<float>{-24.0f, 12.0f, 0.1f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("dB").withStringFromValueFunction(
            [](float value, int) { return (value > 0 ? "+" : "") + juce::String(value, 1) + " dB"; })));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{parameterIds[10], 1}, "Advanced Doubler", false));
    const auto baseline = basicVoiceSettings(0.35f);
    const std::array<const char*, 7> fields{"Gain", "Pan", "Delay", "Detune", "Formant", "Depth", "Rate"};
    const std::array<const char*, 7> units{"dB", "", "ms", "ct", "st", "x", "Hz"};
    const std::array<float, 7> minimum{-60, -1, 0, -100, -24, 0, 0};
    const std::array<float, 7> maximum{12, 1, 100, 100, 24, 2, 8};
    const std::array<float, 7> interval{0.1f, 0.01f, 0.1f, 0.01f, 0.0001f, 0.01f, 0.01f};
    const auto addVoiceParameters = [&](size_t v) {
        const auto& voice = baseline[v];
        const auto name = "Voice " + juce::String(static_cast<int>(v + 1)) + " ";
        const size_t index = voiceParameterIndex(v, 0);
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{parameterIds[index], 1}, name + "Active", voice.enabled));
        const std::array<float, 7> initial{voice.gainDb, voice.pan, voice.delayMs, voice.detuneCents,
                                          voice.formantSemitones, voice.depth, voice.rateHz};
        for (size_t f = 0; f < fields.size(); ++f)
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{parameterIds[index + 1 + f], 1}, name + fields[f],
                juce::NormalisableRange<float>{minimum[f], maximum[f], interval[f]}, initial[f],
                juce::AudioParameterFloatAttributes{}.withLabel(units[f])));
    };
    for (size_t v = 0; v < 4; ++v) addVoiceParameters(v);
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{parameterIds[43], 1}, "Chorale Active", false));
    percent(44, "Chorale Mix", 25);
    percent(45, "Chorale Motion", 35);
    for (size_t v = 4; v < 8; ++v) addVoiceParameters(v);
    for (size_t v = 0; v < 8; ++v)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{voicePitchParameterId(v), 1}, "Voice " + juce::String(int(v + 1)) + " Pitch",
            juce::NormalisableRange<float>{-36.0f, 36.0f, 0.01f}, 0.0f,
            juce::AudioParameterFloatAttributes{}.withLabel("st")));
    for (size_t v = 0; v < 8; ++v)
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{voicePresentParameterId(v), 1}, "Voice " + juce::String(int(v + 1)) + " Present", v < 2));
    for (size_t slot = 1; slot < 16; ++slot) {
        const auto name = "FX " + juce::String(int(slot)) + " Chorale ";
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{fxParameterId(slot, size_t(0)), 1}, name + "Active", true));
        percent(int(94 + (slot - 1) * 3 + 1), (name + "Mix").toRawUTF8(), 25);
        percent(int(94 + (slot - 1) * 3 + 2), (name + "Motion").toRawUTF8(), 35);
    }
    return layout;
}

Settings Processor::settings() const {
    const auto value = [&](size_t i) { return values[i]->load(std::memory_order_relaxed); };
    Settings result;
    result.level = value(0) * 0.01f;
    result.doubles = value(1) * 0.01f;
    result.human = value(2) * 0.01f;
    result.width = value(3) * 0.01f;
    result.formant = value(4);
    result.levelOn = value(5) >= 0.5f;
    result.doubleOn = value(6) >= 0.5f;
    result.formantOn = value(7) >= 0.5f;
    result.bypass = value(8) >= 0.5f;
    result.outputGainDb = value(9);
    result.advanced = value(10) >= 0.5f;
    for (size_t v = 0; v < result.voiceSettings.size(); ++v) {
        const size_t i = voiceParameterIndex(v, 0);
        result.voiceSettings[v] = {value(i) >= 0.5f, value(i + 1), value(i + 2), value(i + 3),
                                   value(i + 4), value(i + 5), value(i + 6), value(i + 7),
                                   value(78 + v), value(86 + v) >= 0.5f};
    }
    result.choraleOn = value(43) >= 0.5f;
    result.choraleMix = value(44) * 0.01f;
    result.choraleMotion = value(45) * 0.01f;
    processorRouting::applyFxSettings(result, [&](size_t slot, size_t field) {
        return value(slot == 0 ? 43 + field : 94 + (slot - 1) * 3 + field);
    });
    return result;
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto valid = [](juce::AudioChannelSet channels) {
        return channels == juce::AudioChannelSet::mono() || channels == juce::AudioChannelSet::stereo();
    };
    return valid(layout.getMainInputChannelSet()) && valid(layout.getMainOutputChannelSet());
}

void Processor::prepareToPlay(double sampleRate, int) {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    engine.prepare(sampleRate, getTotalNumInputChannels(), getTotalNumOutputChannels(), settings());
    const auto restored = engine.requestGraph(routingActive);
    if (!restored) routingError = juce::String(restored.message);
    voiceBridge.publish(engine.voiceSnapshots());
    setLatencySamples(engine.latency());
    metering.publish(engine.meters());
    prepared = true;
}

void Processor::reset() {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (prepared) {
        engine.reset(settings());
        voiceBridge.publish(engine.voiceSnapshots());
        metering.publish(engine.meters());
    }
}

void Processor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) { render(audio, false); }

void Processor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) { render(audio, true); }

void Processor::render(juce::AudioBuffer<float>& audio, bool hostBypass) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared)
        return;
    if (meterResetRequested.exchange(false, std::memory_order_relaxed))
        engine.resetMeters();
    engine.process(audio.getArrayOfReadPointers(), audio.getArrayOfWritePointers(),
                   audio.getNumSamples(), settings(), hostBypass);
    voiceBridge.publish(engine.voiceSnapshots());
    metering.publish(engine.meters());
}

Meters Processor::getMeters() const {
    return metering.snapshot();
}

juce::AudioProcessorParameter* Processor::getBypassParameter() const {
    return parameters.getParameter("bypass");
}

bool Processor::remoteControlsPageFill(uint32_t index, juce::String& section, uint32_t& id,
                                       juce::String& name,
                                       std::array<juce::AudioProcessorParameter*, CLAP_REMOTE_CONTROLS_COUNT>& slots) noexcept {
    slots.fill(nullptr);
    if (index == 0) {
        section = "Main";
        name = "Vocal Stack";
        id = 1;
        // Keep the original eight controller assignments unchanged.
        for (size_t i = 0; i < slots.size(); ++i)
            slots[i] = parameters.getParameter(parameterIds[i]);
    } else if (index == 1) {
        section = "Output";
        name = "Output";
        id = 2;
        slots[0] = parameters.getParameter("outputGain");
        slots[1] = parameters.getParameter("bypass");
    } else {
        return false;
    }
    return true;
}

juce::ValueTree Processor::copyState() {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    auto state = parameters.copyState();
    state.setProperty(themeProperty, isFlash(), nullptr);
    state.setProperty("advancedVisible", isAdvancedVisible(), nullptr);
    state.setProperty("advancedInitialized", advancedInitialized.load(std::memory_order_relaxed), nullptr);
    state.removeChild(state.getChildWithName("RoutingGraph"), nullptr);
    state.removeChild(state.getChildWithName("ActiveRoutingGraph"), nullptr);
    state.removeChild(state.getChildWithName("RoutingLayout"), nullptr);
    state.addChild(processorRouting::encode(routingDraft, "RoutingGraph"), -1, nullptr);
    state.addChild(processorRouting::encode(routingActive, "ActiveRoutingGraph"), -1, nullptr);
    state.addChild(routingLayout.createCopy(), -1, nullptr);
    return state;
}

void Processor::getStateInformation(juce::MemoryBlock& data) {
    if (auto xml = copyState().createXml())
        copyXmlToBinary(*xml, data);
}

bool Processor::restoreState(const juce::XmlElement& xml) {
    if (!xml.hasTagName(stateType))
        return false;
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    auto restored = juce::ValueTree::fromXml(xml);
    Graph draft = defaultGraph(), active = draft;
    const auto graphTree = restored.getChildWithName("RoutingGraph");
    const auto activeTree = restored.getChildWithName("ActiveRoutingGraph");
    juce::String graphError;
    if (graphTree.isValid() && !processorRouting::decode(graphTree, draft)) {
        draft = defaultGraph();
        graphError = "Malformed saved routing draft";
    }
    if (!processorRouting::decode(activeTree, active) || !processorRouting::validate(active))
        active = defaultGraph();
    const auto validation = processorRouting::validate(draft);
    if (validation && graphError.isEmpty()) active = draft;
    else if (graphError.isEmpty()) graphError = juce::String(validation.message);
    auto layout = restored.getChildWithName("RoutingLayout").createCopy();
    if (!layout.isValid()) layout = juce::ValueTree("RoutingLayout");
    restored.removeChild(graphTree, nullptr);
    restored.removeChild(activeTree, nullptr);
    restored.removeChild(restored.getChildWithName("RoutingLayout"), nullptr);
    bool hasLegacyVoices = false;
    for (size_t i = 11; i < 43; ++i)
        hasLegacyVoices = hasLegacyVoices || restored.getChildWithProperty("id", parameterIds[i]).isValid();
    bool hasPresenceState = false;
    for (size_t slot = 0; slot < 8; ++slot)
        hasPresenceState = hasPresenceState || restored.getChildWithProperty("id", voicePresentParameterId(slot)).isValid();
    const bool legacyVoices = hasLegacyVoices && !hasPresenceState;
    // Missing appended params must not inherit this instance's previous settings.
    bool hasAdvancedState = false;
    for (size_t i = 10; i < 94; ++i)
        if (i < 43 || i >= 46)
            hasAdvancedState = hasAdvancedState || restored.getChildWithProperty("id", parameterIds[i]).isValid();
    for (size_t i = 0; i < parameterIds.size(); ++i) {
        const auto* id = parameterIds[i];
        if (!restored.getChildWithProperty("id", id).isValid()) {
            const auto* parameter = parameters.getParameter(id);
            juce::ValueTree missing("PARAM");
            missing.setProperty("id", id, nullptr);
            float initial = i >= 86 && i < 94 && legacyVoices
                ? (i < 90 ? 1.0f : 0.0f)
                : parameter->convertFrom0to1(parameter->getDefaultValue());
            // A graph-only state can predate fixed FX automation. Seed missing
            // controls from its runnable nodes rather than losing their edits.
            if (graphTree.isValid() && ((i >= 43 && i < 46) || i >= 94)) {
                const size_t slot = i < 46 ? 0 : 1 + (i - 94) / 3;
                const size_t field = i < 46 ? i - 43 : (i - 94) % 3;
                for (const auto& node : active.nodes)
                    if (node.type == NodeType::chorale && processorRouting::fxSlot(node.id) == int(slot))
                        initial = field == 0 ? (node.enabled ? 1.0f : 0.0f)
                            : 100 * (field == 1 ? node.mix : node.motion);
            }

            missing.setProperty("value", initial, nullptr);
            restored.addChild(missing, -1, nullptr);
        }
    }
    const bool initialized = legacyVoices
        || static_cast<bool>(restored.getProperty("advancedInitialized", hasAdvancedState));
    flash.store(static_cast<bool>(restored.getProperty(themeProperty, false)), std::memory_order_relaxed);
    advancedVisible.store(static_cast<bool>(restored.getProperty("advancedVisible", false)), std::memory_order_relaxed);
    parameters.replaceState(restored);
    advancedInitialized.store(initialized, std::memory_order_relaxed);
    routingDraft = std::move(draft);
    routingActive = std::move(active);
    routingLayout = std::move(layout);
    routingError = graphError;
    if (prepared) {
        const auto requested = engine.requestGraph(routingActive);
        if (!requested) routingError = juce::String(requested.message);
    }
    return true;
}

void Processor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        restoreState(*xml);
}

juce::String Processor::settingsText() {
    return copyState().createXml()->toString();
}

bool Processor::pasteSettings(const juce::String& text) {
    const auto xml = juce::parseXML(text);
    if (!xml || !restoreState(*xml))
        return false;
    updateHostDisplay(ChangeDetails{}.withProgramChanged(true).withNonParameterStateChanged(true));
    return true;
}

void Processor::setFlash(bool enabled) {
    if (flash.exchange(enabled, std::memory_order_relaxed) != enabled)
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void Processor::setParameterValue(const char* id, float value) {
    auto* parameter = parameters.getParameter(id);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    parameter->endChangeGesture();
}

void Processor::resetVoicesToBasic() {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    const auto basic = basicVoiceSettings(values[2]->load(std::memory_order_relaxed) * 0.01f);
    for (size_t v = 0; v < basic.size(); ++v) {
        const auto& voice = basic[v];
        const std::array<float, 8> fields{voice.enabled ? 1.0f : 0.0f, voice.gainDb, voice.pan,
            voice.delayMs, voice.detuneCents, voice.formantSemitones, voice.depth, voice.rateHz};
        for (size_t f = 0; f < fields.size(); ++f)
            setParameterValue(voiceParameterId(v, f), fields[f]);
        setParameterValue(voicePitchParameterId(v), 0);
        setParameterValue(voicePresentParameterId(v), v < 2 ? 1.0f : 0.0f);
        if (v < 2) reconnectVoiceLocked(v); else disconnectVoiceLocked(v);
    }
    juce::String error;
    setRoutingGraphLocked(routingDraft, error);
    advancedInitialized.store(true, std::memory_order_relaxed);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void Processor::setAdvancedEnabled(bool enabled) {
    if (enabled && !advancedInitialized.load(std::memory_order_relaxed))
        resetVoicesToBasic();
    setParameterValue("advancedOn", enabled ? 1.0f : 0.0f);
}

void Processor::setAdvancedVisible(bool visible) {
    if (advancedVisible.exchange(visible, std::memory_order_relaxed) != visible)
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void Processor::resetParameters() {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    for (const auto* id : parameterIds) {
        auto* parameter = parameters.getParameter(id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->getDefaultValue());
        parameter->endChangeGesture();
    }
    routingLayout = juce::ValueTree("RoutingLayout");
    juce::String error;
    setRoutingGraphLocked(defaultGraph(), error);
    advancedInitialized.store(false, std::memory_order_relaxed);
}

void Processor::timerCallback() {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (prepared) engine.collectRetiredGraphs();
}

Graph Processor::getRoutingGraph() const {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    return routingDraft;
}

juce::String Processor::getRoutingError() const {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    return routingError;
}

bool Processor::setRoutingGraphLocked(const Graph& graph, juce::String& error) {
    // Copy first: callers may pass routingDraft itself. Invalid sketches are
    // saved for editing, but never replace the last runnable execution plan.
    Graph candidate = graph;
    const auto validation = processorRouting::validate(candidate);
    routingDraft = candidate;
    if (!validation) {
        error = routingError = juce::String(validation.message);
        return false;
    }
    if (prepared) {
        const auto requested = engine.requestGraph(candidate);
        if (!requested) {
            error = routingError = juce::String(requested.message);
            return false;
        }
    }
    routingActive = std::move(candidate);
    routingError.clear();
    error.clear();
    return true;
}

bool Processor::setRoutingGraph(const Graph& graph, juce::String& error) {
    bool accepted;
    {
        const std::lock_guard<std::recursive_mutex> lock(controlMutex);
        const auto previous = routingActive;
        accepted = setRoutingGraphLocked(graph, error);
        if (accepted) {
            // Only intentional inspector edits/new nodes update automation.
            // A cable-only edit must not overwrite host-automated FX values.
            for (const auto& node : graph.nodes) {
                if (node.type != NodeType::chorale) continue;
                const auto old = std::find_if(previous.nodes.begin(), previous.nodes.end(),
                    [&](const RoutingNode& n) { return n.id == node.id && n.type == node.type; });
                const auto slot = size_t(processorRouting::fxSlot(node.id));
                if (old == previous.nodes.end() || old->enabled != node.enabled)
                    setParameterValue(fxParameterId(slot, size_t(0)), node.enabled ? 1.0f : 0.0f);
                if (old == previous.nodes.end() || old->mix != node.mix)
                    setParameterValue(fxParameterId(slot, size_t(1)), node.mix * 100);
                if (old == previous.nodes.end() || old->motion != node.motion)
                    setParameterValue(fxParameterId(slot, size_t(2)), node.motion * 100);
            }
        }
    }
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    return accepted;
}

juce::ValueTree Processor::getRoutingLayout() const {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    return routingLayout.createCopy();
}

void Processor::setRoutingLayout(const juce::ValueTree& layout) {
    {
        const std::lock_guard<std::recursive_mutex> lock(controlMutex);
        // Keep a fixed wrapper so arbitrary UI layout state cannot collide with
        // the graph or APVTS state. Never retain a mutable caller-owned tree.
        if (layout.hasType("RoutingLayout")) routingLayout = layout.createCopy();
        else {
            routingLayout = juce::ValueTree("RoutingLayout");
            if (layout.isValid()) routingLayout.addChild(layout.createCopy(), -1, nullptr);
        }
    }
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void Processor::disconnectVoiceLocked(size_t slot) {
    auto& links = routingDraft.links;
    links.erase(std::remove_if(links.begin(), links.end(), [&](const RoutingLink& link) {
        return std::any_of(routingDraft.nodes.begin(), routingDraft.nodes.end(), [&](const RoutingNode& node) {
            return node.type == NodeType::voice && node.voiceSlot == int(slot) && node.id == link.source;
        });
    }), links.end());
}

void Processor::reconnectVoiceLocked(size_t slot) {
    auto source = std::find_if(routingDraft.nodes.begin(), routingDraft.nodes.end(), [&](const RoutingNode& node) {
        return node.type == NodeType::voice && node.voiceSlot == int(slot);
    });
    if (source == routingDraft.nodes.end()) {
        if (routingDraft.nodes.size() >= RoutingExecutor::maxNodes) return;
        std::uint32_t id = std::uint32_t(slot + 2);
        const auto occupied = [&](std::uint32_t candidate) {
            return std::any_of(routingDraft.nodes.begin(), routingDraft.nodes.end(),
                [&](const RoutingNode& node) { return node.id == candidate; });
        };
        if (occupied(id)) {
            id = 200;
            while (occupied(id)) ++id;
        }
        routingDraft.nodes.push_back({id, NodeType::voice, int(slot)});
        source = std::prev(routingDraft.nodes.end());
    }
    const auto sourceId = source->id;
    if (std::any_of(routingDraft.links.begin(), routingDraft.links.end(),
        [&](const RoutingLink& link) { return link.source == sourceId; })) return;
    // Reconnect only when the main Mix has an explicit wet Sum. Do not guess
    // how to rewrite a custom serial FX branch or replace a user's connection.
    for (const auto& mix : routingDraft.nodes) {
        if (mix.type != NodeType::mix || !mix.mainControls) continue;
        for (const auto& wet : routingDraft.links) {
            if (wet.destination != mix.id || wet.inputPin != 1) continue;
            const auto sum = std::find_if(routingDraft.nodes.begin(), routingDraft.nodes.end(),
                [&](const RoutingNode& n) { return n.id == wet.source && n.type == NodeType::sum; });
            if (sum == routingDraft.nodes.end()) continue;
            for (int attempt = 0; attempt < RoutingExecutor::maxInputs; ++attempt) {
                const int pin = (int(slot) + attempt) % RoutingExecutor::maxInputs;
                const bool used = std::any_of(routingDraft.links.begin(), routingDraft.links.end(),
                    [&](const RoutingLink& link) { return link.destination == sum->id && link.inputPin == pin; });
                if (!used && routingDraft.links.size() < RoutingExecutor::maxLinks) {
                    const RoutingLink link{sourceId, sum->id, pin};
                    routingDraft.links.push_back(link);
                    return;
                }
            }
        }
    }
}

bool Processor::addVoice() {
    {
        const std::lock_guard<std::recursive_mutex> lock(controlMutex);
        size_t slot = 0;
        while (slot < 8 && values[86 + slot]->load(std::memory_order_relaxed) >= 0.5f) ++slot;
        if (slot == 8) return false;
        reconnectVoiceLocked(slot);
        setParameterValue(voiceParameterId(slot, size_t(0)), 1);
        setParameterValue(voicePresentParameterId(slot), 1);
        juce::String error;
        setRoutingGraphLocked(routingDraft, error);
        advancedInitialized.store(true, std::memory_order_relaxed);
    }
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    return true;
}

void Processor::removeVoice(size_t slot) {
    if (slot >= 8) return;
    {
        const std::lock_guard<std::recursive_mutex> lock(controlMutex);
        setParameterValue(voicePresentParameterId(slot), 0);
        disconnectVoiceLocked(slot);
        juce::String error;
        setRoutingGraphLocked(routingDraft, error);
        advancedInitialized.store(true, std::memory_order_relaxed);
    }
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

std::array<VoiceSnapshot, 8> Processor::getVoiceSnapshots() const {
    return voiceBridge.snapshot();
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

} // namespace vocalstack

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vocalstack::Processor(); }
