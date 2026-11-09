#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <clap-juce-extensions/clap-juce-extensions.h>
#include "VocalEngine.h"

namespace vocalstack {

inline constexpr std::array<const char*, 9> parameterIds{
    "level", "double", "human", "width", "formant",
    "levelOn", "doubleOn", "formantOn", "bypass"
};

class Processor final : public juce::AudioProcessor,
                        public clap_juce_extensions::clap_juce_audio_processor_capabilities {
public:
    Processor();

    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    double getTailLengthSeconds() const override { return engine.tailSeconds(); }

    bool supportsRemoteControls() const noexcept override { return true; }
    uint32_t remoteControlsPageCount() noexcept override { return 1; }
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

    bool isFlash() const { return flash.load(std::memory_order_relaxed); }
    void setFlash(bool enabled);
    Meters getMeters() const;
    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    Settings settings() const;
    juce::ValueTree copyState();
    bool restoreState(const juce::XmlElement&);
    void render(juce::AudioBuffer<float>&, bool hostBypass);

    std::array<std::atomic<float>*, parameterIds.size()> values{};
    std::atomic<bool> flash{false};
    std::array<std::atomic<float>, 3> metering{};
    VocalEngine engine;
    bool prepared = false;
};

} // namespace vocalstack
