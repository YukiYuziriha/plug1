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

// FxMonitor-style exclusive payload ownership: bounded producer/readers, no
// plain-field seqlock races. Contention drops a frame rather than waiting.
class GrainSnapshotBridge {
public:
    void publish(const RoutingGrainSnapshot& frame) noexcept {
        const auto token = latest.load(std::memory_order_relaxed);
        for (size_t i = 0; i < buffers->size(); ++i) {
            if ((token & 3) == i + 1) continue;
            auto& b = (*buffers)[i];
            int free = 0;
            if (!b.owner.compare_exchange_strong(free, -1, std::memory_order_acquire,
                                                  std::memory_order_relaxed)) continue;
            b.frame = frame;
            b.owner.store(0, std::memory_order_release);
            latest.store((++sequence << 2) | (i + 1), std::memory_order_release);
            return;
        }
    }
    bool read(size_t slot, RoutingGrainFrame& out, uint64_t revision, uint64_t epoch) const noexcept {
        if (slot >= 16) return false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const auto token = latest.load(std::memory_order_acquire);
            if (!token) return false;
            auto& b = (*buffers)[(token & 3) - 1];
            int free = 0;
            if (!b.owner.compare_exchange_strong(free, 1, std::memory_order_acquire,
                                                  std::memory_order_relaxed)) continue;
            bool valid = latest.load(std::memory_order_acquire) == token
                && b.frame.revision == revision && b.frame.resetEpoch == epoch;
            if (valid) out = b.frame.nodes[slot];
            valid = valid && latest.load(std::memory_order_acquire) == token;
            b.owner.store(0, std::memory_order_release);
            if (valid) return out.nodeId != 0 && out.generation != 0;
        }
        return false;
    }
private:
    struct Buffer {
        std::atomic<int> owner{0};
        RoutingGrainSnapshot frame{};
    };
    std::unique_ptr<std::array<Buffer, 3>> buffers = std::make_unique<std::array<Buffer, 3>>();
    std::atomic<uint64_t> latest{0};
    uint64_t sequence = 0;
    static_assert(std::atomic<int>::is_always_lock_free && std::atomic<uint64_t>::is_always_lock_free,
                  "Grain publication requires lock-free ownership");
};

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
// Keep all original 48-field slot blocks at their historical host indices.
// Expanded Reverb fields append after those blocks, before Utility parameters.
inline constexpr size_t legacyCreativeParameterCount = 48;
inline constexpr size_t releasedCreativeParameterCount = 51;
inline constexpr size_t newCreativeParameterStart = 2087;
inline constexpr size_t creativeParameterIndex(size_t slot, size_t p) noexcept {
    if (p >= releasedCreativeParameterCount)
        return newCreativeParameterStart + (slot - 1) * (creativeParameterCount - releasedCreativeParameterCount) + p - releasedCreativeParameterCount;
    return p < legacyCreativeParameterCount
        ? creativeParameterStart + (slot - 1) * legacyCreativeParameterCount + p
        : creativeParameterStart + 15 * legacyCreativeParameterCount
            + (slot - 1) * (releasedCreativeParameterCount - legacyCreativeParameterCount) + p - legacyCreativeParameterCount;
}
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
inline constexpr size_t utilityParameterStart = creativeParameterStart + 15 * releasedCreativeParameterCount;
inline const auto utilityParameterIdStorage = [] {
    std::array<std::string, 15 * utilityParameterCount> ids;
    for (size_t slot = 1; slot < 16; ++slot)
        for (size_t p = 0; p < utilityParameterCount; ++p)
            ids[(slot - 1) * utilityParameterCount + p] = "fx" + std::to_string(slot) + utilityParameters[p].id;
    return ids;
}();
inline juce::String utilityParameterId(size_t slot, size_t p) {
    return slot > 0 && slot < 16 && p < utilityParameterCount
        ? juce::String(utilityParameterIdStorage[(slot - 1) * utilityParameterCount + p]) : juce::String();
}
inline constexpr size_t spectralMorphParameterStart = 2252;
static_assert(newCreativeParameterStart + 15 * (creativeParameterCount - releasedCreativeParameterCount) == spectralMorphParameterStart);
inline constexpr size_t spectralMorphParameterIndex(size_t slot, size_t field) noexcept {
    return spectralMorphParameterStart + (slot - 1) * spectralMorphParameterCount + field;
}
inline const auto spectralMorphIdStorage = [] {
    std::array<std::string, 15 * spectralMorphParameterCount> ids;
    for (size_t slot = 1; slot < 16; ++slot) for (size_t p = 0; p < spectralMorphParameterCount; ++p)
        ids[(slot - 1) * spectralMorphParameterCount + p] = "fx" + std::to_string(slot) + spectralMorphParameters[p].id;
    return ids;
}();
inline juce::String spectralMorphParameterId(size_t slot, size_t field) {
    return slot > 0 && slot < 16 && field < spectralMorphParameterCount
        ? juce::String(spectralMorphIdStorage[(slot - 1) * spectralMorphParameterCount + field]) : juce::String();
}
inline constexpr size_t stereoMonoParameterStart = spectralMorphParameterStart + 15 * spectralMorphParameterCount;
static_assert(stereoMonoParameterStart == 2342);
inline const auto stereoMonoIdStorage = [] {
    std::array<std::string, 15> ids;
    for (size_t slot = 1; slot < 16; ++slot) ids[slot - 1] = "fx" + std::to_string(slot) + "stereo_mono_audition";
    return ids;
}();
inline juce::String stereoMonoParameterId(size_t slot) {
    return slot > 0 && slot < 16 ? juce::String(stereoMonoIdStorage[slot - 1]) : juce::String();
}
inline const auto parameterIds = [] {
    std::array<const char*, stereoMonoParameterStart + 15> ids{};
        static_assert(utilityParameterStart + 15 * utilityParameterCount == newCreativeParameterStart);
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
    for (size_t slot = 1; slot < 16; ++slot)
        for (size_t p = 0; p < creativeParameterCount; ++p)
            ids[creativeParameterIndex(slot, p)] = creativeParameterIdStorage[(slot - 1) * creativeParameterCount + p].c_str();
    for (size_t i = 0; i < utilityParameterIdStorage.size(); ++i)
        ids[utilityParameterStart + i] = utilityParameterIdStorage[i].c_str();
    for (size_t i = 0; i < spectralMorphIdStorage.size(); ++i) ids[spectralMorphParameterStart + i] = spectralMorphIdStorage[i].c_str();
    for (size_t i = 0; i < stereoMonoIdStorage.size(); ++i) ids[stereoMonoParameterStart + i] = stereoMonoIdStorage[i].c_str();
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
    void releaseResources() override;
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
    bool routingRestartPending() const;
    juce::ValueTree getRoutingLayout() const;
    void setRoutingLayout(const juce::ValueTree&);
    bool addVoice();
    void removeVoice(size_t slot);
    std::array<VoiceSnapshot, 8> getVoiceSnapshots() const;
    VoiceSnapshot getLeadSnapshot() const;
    void retainFxMonitor(size_t slot);
    void releaseFxMonitor(size_t slot);
    bool readFxMonitor(size_t slot, FxMonitorFrame& out) const;
    bool readSpectralMorph(size_t slot, SpectralMorphFrame& out) const noexcept {
        const auto revision = engine.requestedGrainRevision();
        const auto epoch = engine.grainEpoch();
        const bool valid = spectralBridge.read(slot, out)
            && out.graphRevision == revision && out.resetEpoch == epoch
            && revision == engine.requestedGrainRevision() && epoch == engine.grainEpoch();
        if (!valid) out = {};
        return valid;
    }
    int externalSidechainChannels() const noexcept { return sidechainChannels.load(std::memory_order_relaxed); }
    float getCreativeGainReduction(size_t slot) const noexcept { return fxMonitor.gainReduction(slot); }
        bool readGrainSnapshot(size_t slot, RoutingGrainFrame& out) const noexcept;
            bool isGrainSnapshotCurrent(const RoutingGrainFrame& frame) const noexcept {
                return frame.nodeId != 0 && frame.generation != 0
                    && frame.revision == engine.requestedGrainRevision() && frame.resetEpoch == engine.grainEpoch();
            }


    int getSkin() const noexcept { return skin.load(std::memory_order_relaxed); }
    void setSkin(int);
    double getMotionPercent() const noexcept { return motionPercent.load(std::memory_order_relaxed); }
    void setMotionPercent(double);
    bool isFlash() const noexcept { return getSkin() == 1; }
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
    std::atomic<int> skin{0};
    std::atomic<bool> advancedVisible{false}, advancedInitialized{false};
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
    std::atomic<double> motionPercent{3.0}; // Presentation state, never a host parameter.
    FxMonitor fxMonitor;
    GrainSnapshotBridge grainBridge;
    SpectralMorphBridge spectralBridge;
    VocalEngine engine;
    std::atomic<int> sidechainChannels{0};
    int preparedRoutingLatency = 0;
    bool restartPending = false;
    bool prepared = false;
};

} // namespace vocalstack
