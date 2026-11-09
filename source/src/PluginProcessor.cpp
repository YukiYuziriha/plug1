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
    percent(0, "Level", 50);
    percent(1, "Double", 35);
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
    if (!restored.getChildWithProperty("id", "outputGain").isValid()) {
        // Old sessions must restore unity, not inherit the current instance's trim.
        juce::ValueTree outputGain("PARAM");
        outputGain.setProperty("id", "outputGain", nullptr);
        outputGain.setProperty("value", 0.0f, nullptr);
        restored.addChild(outputGain, -1, nullptr);
    }
    flash.store(static_cast<bool>(restored.getProperty(themeProperty, false)), std::memory_order_relaxed);
    parameters.replaceState(restored);
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

void Processor::resetParameters() {
    for (const auto* id : parameterIds) {
        auto* parameter = parameters.getParameter(id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->getDefaultValue());
        parameter->endChangeGesture();
    }
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

} // namespace vocalstack

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vocalstack::Processor(); }
