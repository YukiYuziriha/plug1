#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <clap-juce-extensions/clap-juce-extensions.h>
#include "VocalEngine.h"
#include "MeterBridge.h"
#include "VoiceBridge.h"
#include "FxMonitor.h"
#include "Modulation.h"
#include <mutex>
#include <string_view>
#include <string>

namespace vocalstack {

// Append host parameters: existing IDs and indices are part of session compatibility.
inline constexpr std::array<const char*, 139> legacyParameterIds{
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
inline constexpr size_t toneParametersPerSlot = 49;
inline constexpr std::array<const char*, 9> toneParameterFields{
    "WaveDrive", "WaveOutput", "WaveShape", "EqShift", "EqGain", "EqAdaptiveQ",
    "BandCount", "CrossoverLow", "CrossoverHigh"
};
inline constexpr std::array<const char*, 5> eqBandParameterFields{"On", "Frequency", "Gain", "Q", "Mode"};
inline constexpr size_t toneParameterIndex(size_t slot, size_t field) noexcept {
    return legacyParameterIds.size() + (slot - 1) * toneParametersPerSlot + field;
}
// Storage is never mutated: every const char* remains valid for the plugin lifetime.
inline const auto toneParameterIdStorage = [] {
    std::array<std::string, 15 * toneParametersPerSlot> ids;
    for (size_t slot = 1; slot < 16; ++slot) {
        const auto prefix = "fx" + std::to_string(slot);
        const auto base = (slot - 1) * toneParametersPerSlot;
        for (size_t f = 0; f < toneParameterFields.size(); ++f)
            ids[base + f] = prefix + toneParameterFields[f];
        for (size_t band = 0; band < 8; ++band)
            for (size_t f = 0; f < eqBandParameterFields.size(); ++f)
                ids[base + 9 + band * 5 + f] = prefix + "EqBand" + std::to_string(band + 1) + eqBandParameterFields[f];
    }
    return ids;
}();
inline constexpr size_t originalParameterCount = 874;
inline constexpr size_t globalMixParameterIndex = originalParameterCount;
inline constexpr size_t leadParameterStart = globalMixParameterIndex + 1;
inline constexpr std::array<const char*, 9> leadParameterIds{
    "leadOn", "leadGain", "leadPan", "leadDelay", "leadDetune", "leadFormant", "leadDepth", "leadRate", "leadPitch"
};
inline const auto waveExtraParameterIdStorage = [] {
    std::array<std::string, 30> ids;
    for (size_t slot = 1; slot < 16; ++slot) {
        ids[(slot - 1) * 2] = "fx" + std::to_string(slot) + "WaveRaw";
        ids[(slot - 1) * 2 + 1] = "fx" + std::to_string(slot) + "WaveShapeAmount";
    }
    return ids;
}();
inline constexpr size_t waveExtraParameterStart = leadParameterStart + leadParameterIds.size();
inline constexpr size_t waveQualityParameterStart = waveExtraParameterStart + 30;
static_assert(waveQualityParameterStart == 914);
inline const auto waveQualityParameterIdStorage = [] {
    std::array<std::string, 15> ids;
    for (size_t slot = 1; slot < 16; ++slot)
        ids[slot - 1] = "fx" + std::to_string(slot) + "WaveQuality";
    return ids;
}();
inline constexpr size_t waveLevelMatchParameterStart = waveQualityParameterStart + 15;
inline const auto waveLevelMatchParameterIdStorage = [] {
    std::array<std::string, 15> ids;
    for (size_t slot = 1; slot < 16; ++slot)
        ids[slot - 1] = "fx" + std::to_string(slot) + "WaveLevelMatch";
    return ids;
}();
inline constexpr size_t localMixParameterStart = waveLevelMatchParameterStart + 15;
inline const auto localMixParameterIdStorage = [] {
    std::array<std::string, 48> ids;
    for (size_t slot = 0; slot < ids.size(); ++slot)
        ids[slot] = "mix" + std::to_string(slot + 1) + "Balance";
    return ids;
}();
inline juce::String localMixParameterId(uint32_t nodeId) {
    return nodeId >= 200 && nodeId < 248 ? juce::String(localMixParameterIdStorage[nodeId - 200]) : juce::String();
}
inline constexpr size_t waveAlgorithmParameterStart = localMixParameterStart + 48;
inline const auto waveAlgorithmParameterIdStorage = [] {
    std::array<std::string, 15> ids;
    for (size_t slot = 1; slot < 16; ++slot)
        ids[slot - 1] = "fx" + std::to_string(slot) + "WaveAlgorithm";
    return ids;
}();
inline constexpr size_t creativeParameterStart = waveAlgorithmParameterStart + 15;
inline const auto creativeParameterIdStorage = [] {
    std::array<std::string, 15 * creativeParameterCount> ids;
    for (size_t slot = 1; slot < 16; ++slot)
        for (size_t p = 0; p < creativeParameterCount; ++p)
            ids[(slot - 1) * creativeParameterCount + p] = "fx" + std::to_string(slot) + creativeParameters[p].id;
    return ids;
}();
inline juce::String creativeParameterId(size_t slot, size_t p) {
    return slot > 0 && slot < 16 && p < creativeParameterCount
        ? juce::String(creativeParameterIdStorage[(slot - 1) * creativeParameterCount + p]) : juce::String();
}
inline const auto parameterIds = [] {
    std::array<const char*, creativeParameterStart + 15 * creativeParameterCount> ids{};
    for (size_t i = 0; i < legacyParameterIds.size(); ++i) ids[i] = legacyParameterIds[i];
    for (size_t i = 0; i < toneParameterIdStorage.size(); ++i)
        ids[legacyParameterIds.size() + i] = toneParameterIdStorage[i].c_str();
    ids[globalMixParameterIndex] = "globalMix";
    for (size_t i = 0; i < leadParameterIds.size(); ++i) ids[leadParameterStart + i] = leadParameterIds[i];
    for (size_t i = 0; i < waveExtraParameterIdStorage.size(); ++i)
        ids[waveExtraParameterStart + i] = waveExtraParameterIdStorage[i].c_str();
    for (size_t i = 0; i < waveQualityParameterIdStorage.size(); ++i)
        ids[waveQualityParameterStart + i] = waveQualityParameterIdStorage[i].c_str();
    for (size_t i = 0; i < waveLevelMatchParameterIdStorage.size(); ++i)
        ids[waveLevelMatchParameterStart + i] = waveLevelMatchParameterIdStorage[i].c_str();
    for (size_t i = 0; i < localMixParameterIdStorage.size(); ++i)
        ids[localMixParameterStart + i] = localMixParameterIdStorage[i].c_str();
    for (size_t i = 0; i < waveAlgorithmParameterIdStorage.size(); ++i)
        ids[waveAlgorithmParameterStart + i] = waveAlgorithmParameterIdStorage[i].c_str();
    for (size_t i = 0; i < creativeParameterIdStorage.size(); ++i)
        ids[creativeParameterStart + i] = creativeParameterIdStorage[i].c_str();
    return ids;
}();
static_assert(parameterIds.size() <= ModulationBackend::maxTargets);
juce::String toneParameterId(size_t slot, const char* field);
juce::String eqBandParameterId(size_t slot, size_t band, const char* field);

// All slots are zero-based. Voice fields: On, Gain, Pan, Delay, Detune,
// Formant, Depth, Rate. FX slot 0 is legacy node 12; 1..15 are nodes 100..114.
inline constexpr size_t voiceParameterIndex(size_t slot, size_t field) {
    return slot < 4 ? 11 + slot * 8 + field : 46 + (slot - 4) * 8 + field;
}
inline constexpr const char* voiceParameterId(size_t slot, size_t field) {
    return slot < 8 && field < 8 ? legacyParameterIds[voiceParameterIndex(slot, field)] : nullptr;
}
inline constexpr const char* voicePitchParameterId(size_t slot) {
    return slot < 8 ? legacyParameterIds[78 + slot] : nullptr;
}
inline constexpr const char* voicePresentParameterId(size_t slot) {
    return slot < 8 ? legacyParameterIds[86 + slot] : nullptr;
}
inline constexpr const char* fxParameterId(size_t slot, size_t field) {
    return slot < 16 && field < 3 ? legacyParameterIds[slot == 0 ? 43 + field : 94 + (slot - 1) * 3 + field] : nullptr;
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
    bool acceptsMidi() const override { return true; }
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

    // Control-thread API. Setters validate the entire bounded state before publication.
    ModulationState getModulationState() const;
    bool setModulationState(const ModulationState& state, juce::String& error);
    void beginModulationAssignment(uint32_t id);
    void endModulationAssignment();
    uint32_t modulationAssignmentId() const noexcept;
    bool setModulationDepth(uint32_t modulatorId, const juce::String& parameterId, float normalizedDepth);
    ModulationSnapshot getModulationSnapshot() const;

    Graph getRoutingGraph() const;
    bool setRoutingGraph(const Graph&, juce::String& error);
    juce::String getRoutingError() const;
    juce::ValueTree getRoutingLayout() const;
    void setRoutingLayout(const juce::ValueTree&);
    bool addVoice();
    void removeVoice(size_t slot);
    std::array<VoiceSnapshot, 8> getVoiceSnapshots() const;
    VoiceSnapshot getLeadSnapshot() const;
    void retainFxMonitor(size_t slot);
    void releaseFxMonitor(size_t slot);
    bool readFxMonitor(size_t slot, FxMonitorFrame& out) const;
    float getCreativeGainReduction(size_t slot) const noexcept { return fxMonitor.gainReduction(slot); }


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
    void render(juce::AudioBuffer<float>&, juce::MidiBuffer&, bool hostBypass);
    Settings settingsFromValues(const std::array<float, parameterIds.size()>&) const;
    Graph qualitySnapshot(Graph) const;
    void updateModulationValues();
    void timerCallback() override;
    bool setRoutingGraphLocked(const Graph&, juce::String& error);
    void reconnectVoiceLocked(size_t slot);
    void disconnectVoiceLocked(size_t slot);


    std::array<std::atomic<float>*, parameterIds.size()> values{};
    std::array<juce::RangedAudioParameter*, parameterIds.size()> rangedParameters{};
    std::array<bool, parameterIds.size()> steppedParameters{};
    ModulationState modulationState;
    ModulationBackend modulation;
    std::atomic<uint32_t> assignmentId{0};
    std::array<float, ModulationBackend::maxTargets> modulationContributions{};
    std::array<float, parameterIds.size()> effectiveValues{};
    std::array<uint64_t, parameterIds.size()> discreteHoldUntil{};
    uint64_t processedSamples = 0;
    double processingSampleRate = 48000;
    std::array<bool, parameterIds.size()> activeTargetMask{};
    // Assigned target values are latched in real units on audio; normalization
    // for display and all unassigned-target reads happen on the UI thread.
    std::atomic<uint64_t> modulationSnapshotSequence{0};
    std::array<std::atomic<bool>, parameterIds.size()> snapshotTargeted{};
    std::array<std::atomic<float>, parameterIds.size()> snapshotBase{}, snapshotEffective{};
    bool transportPlaying = false;
    std::atomic<double> hostTempo{120};
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
    // FxMonitor's large windows live on its heap, not the Processor/test stack.
    // Declared before engine so the non-owning executor pointer outlives it.
    FxMonitor fxMonitor;
    VocalEngine engine;
    bool prepared = false;
};

} // namespace vocalstack
