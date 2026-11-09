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
    return layout;
}

Settings Processor::settings() const {
    const auto value = [&](size_t i) { return values[i]->load(std::memory_order_relaxed); };
    return {value(0) * 0.01f, value(1) * 0.01f, value(2) * 0.01f, value(3) * 0.01f,
            value(4), value(5) >= 0.5f, value(6) >= 0.5f, value(7) >= 0.5f, value(8) >= 0.5f};
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
    prepared = true;
}

void Processor::reset() {
    if (prepared)
        engine.reset(settings());
}

void Processor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) { render(audio, false); }

void Processor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer&) { render(audio, true); }

void Processor::render(juce::AudioBuffer<float>& audio, bool hostBypass) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared)
        return;
    engine.process(audio.getArrayOfReadPointers(), audio.getArrayOfWritePointers(),
                   audio.getNumSamples(), settings(), hostBypass);
    const auto meters = engine.meters();
    metering[0].store(meters.input, std::memory_order_relaxed);
    metering[1].store(meters.output, std::memory_order_relaxed);
    metering[2].store(meters.correctionDb, std::memory_order_relaxed);
}

Meters Processor::getMeters() const {
    return {metering[0].load(std::memory_order_relaxed), metering[1].load(std::memory_order_relaxed),
            metering[2].load(std::memory_order_relaxed)};
}

juce::AudioProcessorParameter* Processor::getBypassParameter() const {
    return parameters.getParameter(parameterIds.back());
}

bool Processor::remoteControlsPageFill(uint32_t index, juce::String& section, uint32_t& id,
                                       juce::String& name,
                                       std::array<juce::AudioProcessorParameter*, CLAP_REMOTE_CONTROLS_COUNT>& slots) noexcept {
    if (index != 0)
        return false;

    section = "Main";
    name = "Vocal Stack";
    id = 1;
    static_assert(CLAP_REMOTE_CONTROLS_COUNT + 1 == parameterIds.size());
    for (size_t i = 0; i < slots.size(); ++i)
        slots[i] = parameters.getParameter(parameterIds[i]);
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
