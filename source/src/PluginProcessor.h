#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <clap-juce-extensions/clap-juce-extensions.h>
#include "VocalEngine.h"
#include "MeterBridge.h"

namespace vocalstack {

// Append host parameters: existing IDs and indices are part of session compatibility.
inline constexpr std::array<const char*, 46> parameterIds{
    "level", "double", "human", "width", "formant",
    "levelOn", "doubleOn", "formantOn", "bypass", "outputGain",
    "advancedOn",
    "voice1On", "voice1Gain", "voice1Pan", "voice1Delay", "voice1Detune", "voice1Formant", "voice1Depth", "voice1Rate",
    "voice2On", "voice2Gain", "voice2Pan", "voice2Delay", "voice2Detune", "voice2Formant", "voice2Depth", "voice2Rate",
    "voice3On", "voice3Gain", "voice3Pan", "voice3Delay", "voice3Detune", "voice3Formant", "voice3Depth", "voice3Rate",
    "voice4On", "voice4Gain", "voice4Pan", "voice4Delay", "voice4Detune", "voice4Formant", "voice4Depth", "voice4Rate",
    "choraleOn", "choraleMix", "choraleMotion"
};
inline constexpr std::array<const char*, 6> knobParameterIds{
    "level", "double", "human", "width", "formant", "outputGain"
};
inline constexpr std::array<const char*, 4> switchParameterIds{
    "levelOn", "doubleOn", "formantOn", "bypass"
};

class Processor final : public juce::AudioProcessor,
                        public clap_juce_extensions::clap_juce_audio_processor_capabilities,
                        private juce::AudioProcessorValueTreeState::Listener {
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

    std::array<std::atomic<float>*, parameterIds.size()> values{};
    std::atomic<bool> flash{false}, advancedVisible{false}, advancedInitialized{false};
    MeterBridge metering;
    std::atomic<bool> meterResetRequested{false};
    VocalEngine engine;
    bool prepared = false;
};

} // namespace vocalstack
