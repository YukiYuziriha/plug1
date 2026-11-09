#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    for (size_t i = 10; i < 43; ++i)
        parameters.addParameterListener(parameterIds[i], this);
}

Processor::~Processor() {
    for (size_t i = 10; i < 43; ++i)
        parameters.removeParameterListener(parameterIds[i], this);
}

void Processor::parameterChanged(const juce::String& id, float value) {
    // May run on the audio thread: record intent only, never mutate other params.
    if (id != "advancedOn" || value >= 0.5f)
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
        juce::NormalisableRange<float>{-7.0f, 7.0f, 0.1f}, 0.0f,
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
    const std::array<float, 7> minimum{-60, -1, 0, -100, -12, 0, 0};
    const std::array<float, 7> maximum{12, 1, 100, 100, 12, 2, 8};
    const std::array<float, 7> interval{0.1f, 0.01f, 0.1f, 0.01f, 0.0001f, 0.01f, 0.01f};
    for (size_t v = 0; v < baseline.size(); ++v) {
        const auto& voice = baseline[v];
        const auto name = "Voice " + juce::String(static_cast<int>(v + 1)) + " ";
        const size_t index = 11 + v * 8;
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{parameterIds[index], 1}, name + "Active", voice.enabled));
        const std::array<float, 7> initial{voice.gainDb, voice.pan, voice.delayMs, voice.detuneCents,
                                          voice.formantSemitones, voice.depth, voice.rateHz};
        for (size_t f = 0; f < fields.size(); ++f)
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{parameterIds[index + 1 + f], 1}, name + fields[f],
                juce::NormalisableRange<float>{minimum[f], maximum[f], interval[f]}, initial[f],
                juce::AudioParameterFloatAttributes{}.withLabel(units[f])));
    }
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{parameterIds[43], 1}, "Chorale Active", false));
    percent(44, "Chorale Mix", 25);
    percent(45, "Chorale Motion", 35);
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
        const size_t i = 11 + v * 8;
        result.voiceSettings[v] = {value(i) >= 0.5f, value(i + 1), value(i + 2), value(i + 3),
                                   value(i + 4), value(i + 5), value(i + 6), value(i + 7)};
    }
    result.choraleOn = value(43) >= 0.5f;
    result.choraleMix = value(44) * 0.01f;
    result.choraleMotion = value(45) * 0.01f;
    return result;
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto valid = [](juce::AudioChannelSet channels) {
        return channels == juce::AudioChannelSet::mono() || channels == juce::AudioChannelSet::stereo();
    };
    return valid(layout.getMainInputChannelSet()) && valid(layout.getMainOutputChannelSet());
}

void Processor::prepareToPlay(double sampleRate, int) {
    engine.prepare(sampleRate, getTotalNumInputChannels(), getTotalNumOutputChannels(), settings());
    setLatencySamples(engine.latency());
    metering.publish(engine.meters());
    prepared = true;
}

void Processor::reset() {
    if (prepared) {
        engine.reset(settings());
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
    auto state = parameters.copyState();
    state.setProperty(themeProperty, isFlash(), nullptr);
    state.setProperty("advancedVisible", isAdvancedVisible(), nullptr);
    state.setProperty("advancedInitialized", advancedInitialized.load(std::memory_order_relaxed), nullptr);
    return state;
}

void Processor::getStateInformation(juce::MemoryBlock& data) {
    if (auto xml = copyState().createXml())
        copyXmlToBinary(*xml, data);
}

bool Processor::restoreState(const juce::XmlElement& xml) {
    if (!xml.hasTagName(stateType))
        return false;
    auto restored = juce::ValueTree::fromXml(xml);
    // Missing appended params must not inherit this instance's previous settings.
    bool hasAdvancedState = false;
    for (size_t i = 10; i < 43; ++i)
        hasAdvancedState = hasAdvancedState || restored.getChildWithProperty("id", parameterIds[i]).isValid();
    for (size_t i = 0; i < parameterIds.size(); ++i) {
        const auto* id = parameterIds[i];
        if (!restored.getChildWithProperty("id", id).isValid()) {
            const auto* parameter = parameters.getParameter(id);
            juce::ValueTree missing("PARAM");
            missing.setProperty("id", id, nullptr);
            missing.setProperty("value", parameter->convertFrom0to1(parameter->getDefaultValue()), nullptr);
            restored.addChild(missing, -1, nullptr);
        }
    }
    const bool initialized = static_cast<bool>(restored.getProperty("advancedInitialized", hasAdvancedState));
    flash.store(static_cast<bool>(restored.getProperty(themeProperty, false)), std::memory_order_relaxed);
    advancedVisible.store(static_cast<bool>(restored.getProperty("advancedVisible", false)), std::memory_order_relaxed);
    parameters.replaceState(restored);
    advancedInitialized.store(initialized, std::memory_order_relaxed);
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
    const auto basic = basicVoiceSettings(values[2]->load(std::memory_order_relaxed) * 0.01f);
    for (size_t v = 0; v < basic.size(); ++v) {
        const auto& voice = basic[v];
        const std::array<float, 8> fields{voice.enabled ? 1.0f : 0.0f, voice.gainDb, voice.pan,
            voice.delayMs, voice.detuneCents, voice.formantSemitones, voice.depth, voice.rateHz};
        for (size_t f = 0; f < fields.size(); ++f)
            setParameterValue(parameterIds[11 + v * 8 + f], fields[f]);
    }
    advancedInitialized.store(true, std::memory_order_relaxed);
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
    for (const auto* id : parameterIds) {
        auto* parameter = parameters.getParameter(id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->getDefaultValue());
        parameter->endChangeGesture();
    }
    advancedInitialized.store(false, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

} // namespace vocalstack

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vocalstack::Processor(); }
