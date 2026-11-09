#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <clap-juce-extensions/clap-juce-extensions.h>
#include "VocalEngine.h"
#include "MeterBridge.h"
#include "VoiceBridge.h"
#include <mutex>
#include <string_view>

namespace vocalstack {

// Append host parameters: existing IDs and indices are part of session compatibility.
inline constexpr std::array<const char*, 139> parameterIds{
    "level", "double", "human", "width", "formant",
    "levelOn", "doubleOn", "formantOn", "bypass", "outputGain",
    "advancedOn",
    "voice1On", "voice1Gain", "voice1Pan", "voice1Delay", "voice1Detune", "voice1Formant", "voice1Depth", "voice1Rate",
    "voice2On", "voice2Gain", "voice2Pan", "voice2Delay", "voice2Detune", "voice2Formant", "voice2Depth", "voice2Rate",
    "voice3On", "voice3Gain", "voice3Pan", "voice3Delay", "voice3Detune", "voice3Formant", "voice3Depth", "voice3Rate",
    "voice4On", "voice4Gain", "voice4Pan", "voice4Delay", "voice4Detune", "voice4Formant", "voice4Depth", "voice4Rate",
    "choraleOn", "choraleMix", "choraleMotion",
    "voice5On", "voice5Gain", "voice5Pan", "voice5Delay", "voice5Detune", "voice5Formant", "voice5Depth", "voice5Rate",
    "voice6On", "voice6Gain", "voice6Pan", "voice6Delay", "voice6Detune", "voice6Formant", "voice6Depth", "voice6Rate",
    "voice7On", "voice7Gain", "voice7Pan", "voice7Delay", "voice7Detune", "voice7Formant", "voice7Depth", "voice7Rate",
    "voice8On", "voice8Gain", "voice8Pan", "voice8Delay", "voice8Detune", "voice8Formant", "voice8Depth", "voice8Rate",
    "voice1Pitch", "voice2Pitch", "voice3Pitch", "voice4Pitch", "voice5Pitch", "voice6Pitch", "voice7Pitch", "voice8Pitch",
    "voice1Present", "voice2Present", "voice3Present", "voice4Present", "voice5Present", "voice6Present", "voice7Present", "voice8Present",
    "fx1On", "fx1Mix", "fx1Motion", "fx2On", "fx2Mix", "fx2Motion", "fx3On", "fx3Mix",
    "fx3Motion", "fx4On", "fx4Mix", "fx4Motion", "fx5On", "fx5Mix", "fx5Motion", "fx6On",
    "fx6Mix", "fx6Motion", "fx7On", "fx7Mix", "fx7Motion", "fx8On", "fx8Mix", "fx8Motion",
    "fx9On", "fx9Mix", "fx9Motion", "fx10On", "fx10Mix", "fx10Motion", "fx11On", "fx11Mix",
    "fx11Motion", "fx12On", "fx12Mix", "fx12Motion", "fx13On", "fx13Mix", "fx13Motion", "fx14On",
    "fx14Mix", "fx14Motion", "fx15On", "fx15Mix", "fx15Motion"
};
// All slots are zero-based. Voice fields: On, Gain, Pan, Delay, Detune,
// Formant, Depth, Rate. FX slot 0 is legacy node 12; 1..15 are nodes 100..114.
inline constexpr size_t voiceParameterIndex(size_t slot, size_t field) {
    return slot < 4 ? 11 + slot * 8 + field : 46 + (slot - 4) * 8 + field;
}
inline constexpr const char* voiceParameterId(size_t slot, size_t field) {
    return slot < 8 && field < 8 ? parameterIds[voiceParameterIndex(slot, field)] : nullptr;
}
inline constexpr const char* voicePitchParameterId(size_t slot) {
    return slot < 8 ? parameterIds[78 + slot] : nullptr;
}
inline constexpr const char* voicePresentParameterId(size_t slot) {
    return slot < 8 ? parameterIds[86 + slot] : nullptr;
}
inline constexpr const char* fxParameterId(size_t slot, size_t field) {
    return slot < 16 && field < 3 ? parameterIds[slot == 0 ? 43 + field : 94 + (slot - 1) * 3 + field] : nullptr;
}
inline constexpr const char* voiceParameterId(size_t slot, std::string_view field) {
    constexpr std::array<std::string_view, 8> names{"On", "Gain", "Pan", "Delay", "Detune", "Formant", "Depth", "Rate"};
    for (size_t i = 0; i < names.size(); ++i) if (field == names[i]) return voiceParameterId(slot, i);
    return nullptr;
}
inline constexpr const char* fxParameterId(size_t slot, std::string_view field) {
    constexpr std::array<std::string_view, 3> names{"On", "Mix", "Motion"};
    for (size_t i = 0; i < names.size(); ++i) if (field == names[i]) return fxParameterId(slot, i);
    return nullptr;
}
inline constexpr std::array<const char*, 6> knobParameterIds{
    "level", "double", "human", "width", "formant", "outputGain"
};
inline constexpr std::array<const char*, 4> switchParameterIds{
    "levelOn", "doubleOn", "formantOn", "bypass"
};

class Processor final : public juce::AudioProcessor,
                        public clap_juce_extensions::clap_juce_audio_processor_capabilities,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer {
public:
    Processor();
    ~Processor() override;

    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    double getTailLengthSeconds() const override { return engine.tailSeconds(); }

    bool supportsRemoteControls() const noexcept override { return true; }
    uint32_t remoteControlsPageCount() noexcept override { return 2; }
    bool remoteControlsPageFill(uint32_t index, juce::String& section, uint32_t& id,
                                juce::String& name,
                                std::array<juce::AudioProcessorParameter*, CLAP_REMOTE_CONTROLS_COUNT>&) noexcept override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vocal Stack"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::String settingsText();
    bool pasteSettings(const juce::String&);
    void resetParameters();
    // UI requests only: host automation of advancedOn never rewrites voice parameters.
    void setAdvancedEnabled(bool);
    void resetVoicesToBasic();
    bool isAdvancedVisible() const { return advancedVisible.load(std::memory_order_relaxed); }
    void setAdvancedVisible(bool);
    Settings settings() const;

    Graph getRoutingGraph() const;
    bool setRoutingGraph(const Graph&, juce::String& error);
    juce::String getRoutingError() const;
    juce::ValueTree getRoutingLayout() const;
    void setRoutingLayout(const juce::ValueTree&);
    bool addVoice();
    void removeVoice(size_t slot);
    std::array<VoiceSnapshot, 8> getVoiceSnapshots() const;


    bool isFlash() const { return flash.load(std::memory_order_relaxed); }
    void setFlash(bool enabled);
    Meters getMeters() const;
    void requestMeterReset() noexcept { meterResetRequested.store(true, std::memory_order_relaxed); }
    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    void parameterChanged(const juce::String&, float) override;
    void setParameterValue(const char* id, float value);
    juce::ValueTree copyState();
    bool restoreState(const juce::XmlElement&);
    void render(juce::AudioBuffer<float>&, bool hostBypass);
    void timerCallback() override;
    bool setRoutingGraphLocked(const Graph&, juce::String& error);
    void reconnectVoiceLocked(size_t slot);
    void disconnectVoiceLocked(size_t slot);


    std::array<std::atomic<float>*, parameterIds.size()> values{};
    std::atomic<bool> flash{false}, advancedVisible{false}, advancedInitialized{false};
    MeterBridge metering;
    std::atomic<bool> meterResetRequested{false};
    VoiceBridge voiceBridge;
    // Never acquired by render/settings/parameterChanged. Hosts may restore state
    // on a non-message control thread; all engine control calls are serialized.
    mutable std::recursive_mutex controlMutex;
    Graph routingDraft = defaultGraph(), routingActive = routingDraft;
    juce::ValueTree routingLayout{"RoutingLayout"};
    juce::String routingError;
    VocalEngine engine;
    bool prepared = false;
};

} // namespace vocalstack
