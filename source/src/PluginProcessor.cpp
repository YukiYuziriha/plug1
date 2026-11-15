#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ProcessorRoutingState.h"

namespace vocalstack {
namespace {
using State = juce::AudioProcessorValueTreeState;
constexpr auto stateType = "VocalStack";
constexpr auto themeProperty = "flash";

// JUCE AudioParameterInt supplies integer snapping/step counts, but inherits
// isDiscrete() == false. Quality is explicitly stepped host automation.
class DiscreteAudioParameterInt final : public juce::AudioParameterInt {
public:
    using juce::AudioParameterInt::AudioParameterInt;
    bool isDiscrete() const override { return true; }
};

// Append-only wave parameters are bridged here rather than changing the legacy
// 49-field tone layout (its indices are part of host/session compatibility).
void boundProcessorTone(RoutingNode& node) {
    const auto shape = node.waveshaper.shape;
    processorRouting::boundToneSettings(node);
    node.waveshaper.shape = std::clamp(shape, 0, 18);
    node.waveshaper.shapeAmount = processorRouting::bounded(node.waveshaper.shapeAmount, 0, 1, 0.5f);
    node.waveshaper.quality = std::clamp(node.waveshaper.quality, 0, 2);
}
auto processorToneValues(const RoutingNode& node) {
    auto result = processorRouting::toneValues(node);
    result[2] = float(std::clamp(node.waveshaper.shape, 0, 2));
    return result;
}
juce::ValueTree encodeProcessorGraph(const Graph& graph, const char* type) {
    auto result = processorRouting::encode(graph, type);
    for (auto child : result) {
        if (!child.hasType("Node")) continue;
        for (const auto& node : graph.nodes) if (juce::int64(node.id) == juce::int64(child["id"])) {
            auto wave = child.getChildWithName("Waveshaper");
            wave.setProperty("raw", node.waveshaper.raw, nullptr);
            wave.setProperty("shapeAmount", node.waveshaper.shapeAmount, nullptr);
            wave.setProperty("quality", node.waveshaper.quality, nullptr);
        }
    }
    return result;
}
bool decodeProcessorGraph(const juce::ValueTree& tree, Graph& graph) {
    if (!processorRouting::decode(tree, graph)) return false;
    for (auto& node : graph.nodes) for (const auto child : tree) {
        if (!child.hasType("Node") || juce::int64(node.id) != juce::int64(child["id"])) continue;
        const auto wave = child.getChildWithName("Waveshaper");
        node.waveshaper.shape = int(wave.getProperty("shape", 0));
        node.waveshaper.raw = wave.getProperty("raw", false);
        node.waveshaper.shapeAmount = wave.getProperty("shapeAmount", 0.5f);
        node.waveshaper.quality = int(wave.getProperty("quality", 0));
        boundProcessorTone(node);
    }
    return true;
}
}

juce::String toneParameterId(size_t slot, const char* field) {
    if (slot == 0 || slot > 15 || field == nullptr) return {};
    if (std::string_view(field) == "WaveRaw") return parameterIds[waveExtraParameterStart + (slot - 1) * 2];
    if (std::string_view(field) == "WaveShapeAmount") return parameterIds[waveExtraParameterStart + (slot - 1) * 2 + 1];
    if (std::string_view(field) == "WaveQuality") return parameterIds[waveQualityParameterStart + slot - 1];
    if (std::string_view(field) == "WaveLevelMatch") return parameterIds[waveLevelMatchParameterStart + slot - 1];
    if (std::string_view(field) == "WaveAlgorithm") return parameterIds[waveAlgorithmParameterStart + slot - 1];
    for (size_t f = 0; f < toneParameterFields.size(); ++f)
        if (std::string_view(field) == toneParameterFields[f])
            return parameterIds[toneParameterIndex(slot, f)];
    return {};
}

juce::String eqBandParameterId(size_t slot, size_t band, const char* field) {
    if (slot == 0 || slot > 15 || band >= 8 || field == nullptr) return {};
    for (size_t f = 0; f < eqBandParameterFields.size(); ++f)
        if (std::string_view(field) == eqBandParameterFields[f])
            return parameterIds[toneParameterIndex(slot, 9 + band * 5 + f)];
    return {};
}

Processor::Processor()
    : AudioProcessor(BusesProperties()
        .withInput("Vocal In", juce::AudioChannelSet::stereo(), true)
                .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
        .withOutput("Stack Out", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, stateType, makeParameters()) {
    engine.setFxMonitor(&fxMonitor);
    setLatencySamples(VocalEngine::defaultLatency);
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = parameters.getRawParameterValue(parameterIds[i]);
        rangedParameters[i] = parameters.getParameter(parameterIds[i]);
        // Cache off audio. Legacy integer parameters retain their original host
        // metadata, but still need Schmitt/dwell protection in modulation.
        steppedParameters[i] = rangedParameters[i]->isDiscrete()
            || dynamic_cast<juce::AudioParameterInt*>(rangedParameters[i]) != nullptr;
        const auto initial = values[i]->load();
        snapshotBase[i].store(initial);
        snapshotEffective[i].store(initial);
    }
    for (size_t i = 10; i < 94; ++i)
        if (i < 43 || i >= 46) parameters.addParameterListener(parameterIds[i], this);
    startTimerHz(10);
}

Processor::~Processor() {
    stopTimer();
    for (size_t i = 10; i < 94; ++i)
        if (i < 43 || i >= 46) parameters.removeParameterListener(parameterIds[i], this);
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
    percent(1, "Mix", 35); // UI label is Lead / Stack; preserve host metadata.
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
    constexpr std::array<float, 8> frequencies{60, 120, 250, 500, 1000, 2500, 6000, 12000};
    for (size_t slot = 1; slot < 16; ++slot) {
        const auto name = "FX " + juce::String(int(slot)) + " ";
        const auto base = toneParameterIndex(slot, 0);
        const auto addFloat = [&](size_t field, const juce::String& title, float low, float high,
                                  float initial, const char* unit, bool logarithmic = false) {
            juce::NormalisableRange<float> range{low, high};
            if (logarithmic) range.setSkewForCentre(std::sqrt(low * high));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{parameterIds[base + field], 1}, name + title, range, initial,
                juce::AudioParameterFloatAttributes{}.withLabel(unit)));
        };
        const auto addInt = [&](size_t field, const juce::String& title, int low, int high, int initial) {
            layout.add(std::make_unique<juce::AudioParameterInt>(
                juce::ParameterID{parameterIds[base + field], 1}, name + title, low, high, initial));
        };
        const auto addBool = [&](size_t field, const juce::String& title) {
            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{parameterIds[base + field], 1}, name + title, false));
        };
        addFloat(0, "Wave Drive", 0, 36, 0, "dB");
        addFloat(1, "Wave Output", -24, 12, 0, "dB");
        addInt(2, "Wave Shape", 0, 2, 0); // Preserve legacy normalized automation.
        addFloat(3, "EQ Shift", -24, 24, 0, "st");
        addFloat(4, "EQ Gain", -24, 24, 0, "dB");
        addBool(5, "EQ Adaptive Q");
        addInt(6, "Band Count", 2, 3, 3);
        addFloat(7, "Crossover Low", 20, 20000, 250, "Hz", true);
        addFloat(8, "Crossover High", 20, 20000, 4000, "Hz", true);
        for (size_t b = 0; b < 8; ++b) {
            const auto bandName = "EQ Band " + juce::String(int(b + 1)) + " ";
            const auto field = 9 + b * 5;
            addBool(field, bandName + "Active");
            addFloat(field + 1, bandName + "Frequency", 20, 20000, frequencies[b], "Hz", true);
            addFloat(field + 2, bandName + "Gain", -24, 24, 0, "dB");
            addFloat(field + 3, bandName + "Q", 0.1f, 18, 0.707f, "");
            addInt(field + 4, bandName + "Mode", 0, 13, 0);
        }
    }
    percent(int(globalMixParameterIndex), "Global Mix", 100);
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"leadOn", 1}, "Lead Active", true));
    for (size_t f = 0; f < fields.size(); ++f)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{leadParameterIds[f + 1], 1}, "Lead " + juce::String(fields[f]),
            juce::NormalisableRange<float>{minimum[f], maximum[f], interval[f]}, 0.0f,
            juce::AudioParameterFloatAttributes{}.withLabel(units[f])));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"leadPitch", 1}, "Lead Pitch",
        juce::NormalisableRange<float>{-36, 36, 0.01f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("st")));
    for (size_t slot = 1; slot < 16; ++slot) {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{toneParameterId(slot, "WaveRaw"), 1}, "FX " + juce::String(int(slot)) + " Wave Raw", false));
        percent(int(waveExtraParameterStart + (slot - 1) * 2 + 1),
            ("FX " + juce::String(int(slot)) + " Wave Shape Amount").toRawUTF8(), 50);
    }
    for (size_t slot = 1; slot < 16; ++slot)
        layout.add(std::make_unique<DiscreteAudioParameterInt>(
            juce::ParameterID{toneParameterId(slot, "WaveQuality"), 1},
            "FX " + juce::String(int(slot)) + " Wave Quality", 0, 2, 0,
            juce::AudioParameterIntAttributes{}.withStringFromValueFunction([](int value, int) {
                return juce::String(Waveshaper::qualityNames[size_t(std::clamp(value, 0, 2))]);
            }).withValueFromStringFunction([](const juce::String& text) {
                if (text.trim().equalsIgnoreCase("2x")) return 1;
                if (text.trim().equalsIgnoreCase("4x")) return 2;
                return std::clamp(text.getIntValue(), 0, 2);
            })));
    for (size_t slot = 1; slot < 16; ++slot)
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{toneParameterId(slot, "WaveLevelMatch"), 1},
            "FX " + juce::String(int(slot)) + " Wave Level Match", false));
    for (size_t slot = 0; slot < localMixParameterIdStorage.size(); ++slot)
        percent(int(localMixParameterStart + slot), ("Mix " + juce::String(int(slot + 1)) + " Balance").toRawUTF8(), 50);
    for (size_t slot = 1; slot < 16; ++slot)
        layout.add(std::make_unique<DiscreteAudioParameterInt>(
            juce::ParameterID{toneParameterId(slot, "WaveAlgorithm"), 1},
            "FX " + juce::String(int(slot)) + " Wave Algorithm", 0, 19, 0,
            juce::AudioParameterIntAttributes{}.withStringFromValueFunction([](int value, int) {
                return value == 0 ? juce::String("Follow legacy Shape") : juce::String(Waveshaper::shapeNames[size_t(std::clamp(value - 1, 0, 18))]);
            }).withValueFromStringFunction([](const juce::String& text) {
                for (size_t i = 0; i < Waveshaper::shapeNames.size(); ++i)
                    if (text.trim().equalsIgnoreCase(Waveshaper::shapeNames[i])) return int(i + 1);
                return 0;
            })));
    const auto appendDescriptor = [&](const juce::String& id, const juce::String& name, const auto& d) {
        if (id.endsWith("convolution_space")) {
            juce::StringArray choices;
            for (const auto* space : FactoryConvolution::names) choices.add(juce::String(space) + " (synthetic)");
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, choices, int(d.initial)));
        } else if (d.discrete && d.min == 0 && d.max == 1) {
            layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, d.initial >= .5f));
        } else if (d.discrete) {
            layout.add(std::make_unique<DiscreteAudioParameterInt>(juce::ParameterID{id, 1}, name,
                int(d.min), int(d.max), int(d.initial)));
        } else {
            juce::NormalisableRange<float> range(d.min, d.max);
            if (d.min > 0 && d.max / d.min >= 20) range.setSkewForCentre(std::sqrt(d.min * d.max));
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{id, 1}, name, range, d.initial,
                juce::AudioParameterFloatAttributes{}.withLabel(d.unit)));
        }
    };
    // Append new Reverb fields after every historical Creative slot block.
    for (int pass = 0; pass < 2; ++pass)
        for (size_t slot = 1; slot < 16; ++slot)
            for (size_t p = pass == 0 ? 0 : legacyCreativeParameterCount;
                 p < (pass == 0 ? legacyCreativeParameterCount : releasedCreativeParameterCount); ++p) {
                const auto& d = creativeParameters[p];
                appendDescriptor(creativeParameterId(slot, p), "FX " + juce::String(int(slot)) + " " + d.label, d);
            }
    for (size_t slot = 1; slot < 16; ++slot)
        for (size_t p = 0; p < utilityParameterCount; ++p) {
            const auto& d = utilityParameters[p];
            appendDescriptor(utilityParameterId(slot, p), "FX " + juce::String(int(slot)) + " " + d.label, d);
        }
    for (size_t slot = 1; slot < 16; ++slot)
        for (size_t p = releasedCreativeParameterCount; p < creativeParameterCount; ++p) {
            const auto& d = creativeParameters[p];
            appendDescriptor(creativeParameterId(slot, p), "FX " + juce::String(int(slot)) + " " + d.label, d);
        }
    for (size_t slot = 1; slot < 16; ++slot)
        for (size_t p = 0; p < spectralMorphParameterCount; ++p) {
            const auto id = spectralMorphParameterId(slot, p);
            const auto name = "FX " + juce::String(int(slot)) + " Spectral " + spectralMorphParameters[p].label;
            if (p == 1 || p == 2) {
                const auto choices = p == 1 ? juce::StringArray{"Magnitude", "Envelope"}
                                           : juce::StringArray{"1024", "2048", "4096"};
                layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, choices,
                    int(spectralMorphParameters[p].initial)));
            } else appendDescriptor(id, name, spectralMorphParameters[p]);
        }
    for (size_t slot = 1; slot < 16; ++slot)
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{stereoMonoParameterId(slot), 1},
            "FX " + juce::String(int(slot)) + " Mono Audition", false));
    return layout;
}

Settings Processor::settings() const {
    std::array<float, parameterIds.size()> raw{};
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = values[i]->load(std::memory_order_relaxed);
    return settingsFromValues(raw);
}

Settings Processor::settingsFromValues(const std::array<float, parameterIds.size()>& raw) const {
    const auto value = [&](size_t i) { return raw[i]; };
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
    result.fxAutomation = true;
    processorRouting::applyFxSettings(result.fxSettings, [&](size_t slot, size_t field) {
        return value(slot == 0 ? 43 + field : 94 + (slot - 1) * 3 + field);
    });
    for (size_t slot = 1; slot < 16; ++slot) {
        processorRouting::applyToneSettings(result.fxSettings[slot], [&](size_t field) {
            return value(toneParameterIndex(slot, field));
        });
        std::array<float, spectralMorphParameterCount> spectralValues{};
        for (size_t p = 0; p < spectralValues.size(); ++p) spectralValues[p] = value(spectralMorphParameterIndex(slot, p));
        result.fxSettings[slot].spectralMorph = spectralMorphFromValues(spectralValues);
        result.fxSettings[slot].monoAudition = value(stereoMonoParameterStart + slot - 1) >= .5f;
        result.fxSettings[slot].waveshaper.raw = value(waveExtraParameterStart + (slot - 1) * 2) >= 0.5f;
        result.fxSettings[slot].waveshaper.shapeAmount = value(waveExtraParameterStart + (slot - 1) * 2 + 1) * 0.01f;
        // Raw overrides quality in Waveshaper; keep the requested quality saved.
        result.fxSettings[slot].waveshaper.quality = std::clamp(int(value(waveQualityParameterStart + slot - 1)), 0, 2);
        result.fxSettings[slot].waveshaper.levelMatch = value(waveLevelMatchParameterStart + slot - 1) >= 0.5f;
        const int algorithm = std::clamp(int(value(waveAlgorithmParameterStart + slot - 1)), 0, 19);
        if (algorithm > 0) result.fxSettings[slot].waveshaper.shape = algorithm - 1;
        for (size_t p = 0; p < creativeParameterCount; ++p)
            result.fxSettings[slot].creative.values[p] = value(creativeParameterIndex(slot, p));
                    for (size_t p = 0; p < utilityParameterCount; ++p)
                        result.fxSettings[slot].utility.values[p] = value(utilityParameterStart + (slot - 1) * utilityParameterCount + p);
    }
    result.tempoBpm = hostTempo.load(std::memory_order_relaxed);
    result.localMixAutomation = true;
    for (size_t slot = 0; slot < result.localMix.size(); ++slot)
        result.localMix[slot] = value(localMixParameterStart + slot) * 0.01f;
    result.globalMix = value(globalMixParameterIndex) * 0.01f;
    const auto i = leadParameterStart;
    result.leadSettings = {value(i) >= 0.5f, value(i + 1), value(i + 2), value(i + 3), value(i + 4),
        value(i + 5), value(i + 6), value(i + 7), value(i + 8), true};
    return result;
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto valid = [](juce::AudioChannelSet channels) {
        return channels == juce::AudioChannelSet::mono() || channels == juce::AudioChannelSet::stereo();
    };
    return layout.inputBuses.size() == 2 && layout.outputBuses.size() == 1
            && valid(layout.getMainInputChannelSet()) && valid(layout.getMainOutputChannelSet())
            && (layout.inputBuses[1].isDisabled() || valid(layout.inputBuses[1]));
}

void Processor::prepareToPlay(double sampleRate, int) {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    processingSampleRate = sampleRate;
    sidechainChannels.store(getChannelCountOfBus(true, 1), std::memory_order_relaxed);
    modulationSnapshotSequence.fetch_add(1);
    modulation.prepare(sampleRate);
    discreteHoldUntil.fill(0);
    processedSamples = 0;
    activeTargetMask.fill(false);
    for (auto& target : snapshotTargeted) target.store(false);
    modulationSnapshotSequence.fetch_add(1);
    transportPlaying = false;
    // Host activation is the only boundary at which integer graph latency may
    // change. A transport stop/reset is not a host deactivation.
    prepared = false;
    juce::String activationError;
    // Commit deferred lifecycle cleanup only now, never against running audio.
    if (!setRoutingGraphLocked(routingDraft, activationError)) routingError = activationError;
    preparedRoutingLatency = RoutingExecutor::graphLatency(routingActive);
    engine.prepare(sampleRate, getMainBusNumInputChannels(), getMainBusNumOutputChannels(), settings(), preparedRoutingLatency);
    const auto restored = engine.requestGraph(routingActive);
    restartPending = false;
    if (!restored) routingError = juce::String(restored.message);
    grainBridge.publish(engine.grainSnapshot());
        spectralBridge.publish(engine.spectralSnapshots());
    voiceBridge.publish(engine.voiceSnapshots());
    voiceBridge.publishLead(engine.leadSnapshot());
    setLatencySamples(engine.latency());
    metering.publish(engine.meters());
    prepared = true;
}

void Processor::releaseResources() {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    prepared = false; // Called by the host with processing quiescent.
}

void Processor::reset() {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (prepared) {
        modulationSnapshotSequence.fetch_add(1);
        modulation.prepare(processingSampleRate);
        discreteHoldUntil.fill(0);
        processedSamples = 0;
        activeTargetMask.fill(false);
        for (auto& target : snapshotTargeted) target.store(false);
        modulationSnapshotSequence.fetch_add(1);
        engine.reset(settings());
        grainBridge.publish(engine.grainSnapshot());
        spectralBridge.publish(engine.spectralSnapshots());
        voiceBridge.publish(engine.voiceSnapshots());
        voiceBridge.publishLead(engine.leadSnapshot());
        metering.publish(engine.meters());
    }
}

void Processor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) { render(audio, midi, false); }

void Processor::processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) { render(audio, midi, true); }

void Processor::render(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, bool hostBypass) {
    juce::ScopedNoDenormals noDenormals;
    if (!prepared)
        return;
    if (meterResetRequested.exchange(false, std::memory_order_relaxed))
        engine.resetMeters();
    if (auto* playHead = getPlayHead()) {
        if (auto position = playHead->getPosition()) {
            if (auto bpm = position->getBpm()) {
                modulation.setTempo(*bpm);
                if (std::isfinite(*bpm) && *bpm > 0) hostTempo.store(*bpm, std::memory_order_relaxed);
            }
            const bool playing = position->getIsPlaying();
            if (playing && !transportPlaying) modulation.retrigger(ModulationRetrigger::transport);
            transportPlaying = playing;
        }
    }
    auto mainInput = getBusBuffer(audio, true, 0);
    auto mainOutput = getBusBuffer(audio, false, 0);
    const int auxiliaryChannels = getChannelCountOfBus(true, 1);
    auto auxiliary = getBusBuffer(audio, true, 1);
    sidechainChannels.store(auxiliaryChannels, std::memory_order_relaxed);
    // With no modulators there is no control work or re-chunking of the legacy
    // engine path. A newly published plan is picked up on the next host block.
    if (modulation.isIdle()) {
        engine.process(mainInput.getArrayOfReadPointers(), mainOutput.getArrayOfWritePointers(),
            audio.getNumSamples(), settings(), hostBypass,
            auxiliaryChannels > 0 ? auxiliary.getArrayOfReadPointers() : nullptr, auxiliaryChannels);
        processedSamples += uint64_t(audio.getNumSamples());
        grainBridge.publish(engine.grainSnapshot());
        spectralBridge.publish(engine.spectralSnapshots());
        voiceBridge.publish(engine.voiceSnapshots());
        voiceBridge.publishLead(engine.leadSnapshot());
        metering.publish(engine.meters());
        return;
    }
    const auto blockBaseSettings = settings();
    auto event = midi.begin();
    for (int offset = 0; offset < audio.getNumSamples();) {
        while (event != midi.end() && (*event).samplePosition <= offset) {
            const auto metadata = *event;
            if (metadata.numBytes >= 3 && (metadata.data[0] & 0xf0) == 0x90 && metadata.data[2] != 0)
                modulation.retrigger(ModulationRetrigger::note);
            ++event;
        }
        if (modulation.samplesUntilTick() <= 0) updateModulationValues();
        int count = std::min(modulation.samplesUntilTick(), audio.getNumSamples() - offset);
        if (event != midi.end()) count = std::min(count, std::max(1, (*event).samplePosition - offset));
        const float* input[2] = {mainInput.getReadPointer(0, offset), mainInput.getReadPointer(std::min(1, mainInput.getNumChannels() - 1), offset)};
                const float* donor[2] = {auxiliaryChannels > 0 ? auxiliary.getReadPointer(0, offset) : nullptr,
                    auxiliaryChannels > 0 ? auxiliary.getReadPointer(std::min(1, auxiliaryChannels - 1), offset) : nullptr};
        float* output[2] = {mainOutput.getWritePointer(0, offset), mainOutput.getWritePointer(std::min(1, mainOutput.getNumChannels() - 1), offset)};
        engine.process(input, output, count, modulation.targets().empty()
            ? blockBaseSettings : settingsFromValues(effectiveValues), hostBypass, donor, auxiliaryChannels);
        modulation.advance(count);
        processedSamples += uint64_t(count);
        offset += count;
    }
    grainBridge.publish(engine.grainSnapshot());
        spectralBridge.publish(engine.spectralSnapshots());
    voiceBridge.publish(engine.voiceSnapshots());
    voiceBridge.publishLead(engine.leadSnapshot());
    metering.publish(engine.meters());
}

bool Processor::readGrainSnapshot(size_t slot, RoutingGrainFrame& out) const noexcept {
    const auto revision = engine.requestedGrainRevision();
    const auto epoch = engine.grainEpoch();
    const bool valid = grainBridge.read(slot, out, revision, epoch)
        && revision == engine.requestedGrainRevision() && epoch == engine.grainEpoch();
    if (!valid) out = {};
    return valid;
}

void Processor::retainFxMonitor(size_t slot) { fxMonitor.retain(slot); }
void Processor::releaseFxMonitor(size_t slot) { fxMonitor.release(slot); }
bool Processor::readFxMonitor(size_t slot, FxMonitorFrame& out) const {
    return fxMonitor.read(slot, out);
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
    state.setProperty("skin", getSkin(), nullptr);
    state.setProperty("motionPercent", getMotionPercent(), nullptr);
    state.setProperty(themeProperty, isFlash(), nullptr);
    state.setProperty("advancedVisible", isAdvancedVisible(), nullptr);
    state.setProperty("advancedInitialized", advancedInitialized.load(std::memory_order_relaxed), nullptr);
    state.removeChild(state.getChildWithName("RoutingGraph"), nullptr);
    state.removeChild(state.getChildWithName("ActiveRoutingGraph"), nullptr);
    state.removeChild(state.getChildWithName("RoutingLayout"), nullptr);
    state.addChild(encodeProcessorGraph(qualitySnapshot(routingDraft), "RoutingGraph"), -1, nullptr);
    state.addChild(encodeProcessorGraph(qualitySnapshot(routingActive), "ActiveRoutingGraph"), -1, nullptr);
    state.addChild(routingLayout.createCopy(), -1, nullptr);
    state.removeChild(state.getChildWithName("Modulation"), nullptr);
    juce::ValueTree mods("Modulation");
    for (const auto& m : modulationState.modulators) {
        juce::ValueTree node("Modulator");
        node.setProperty("id", juce::int64(m.id), nullptr);
        node.setProperty("type", int(m.type), nullptr);
        node.setProperty("rateHz", m.rateHz, nullptr);
        node.setProperty("sync", m.sync, nullptr);
        node.setProperty("division", m.division, nullptr);
        node.setProperty("phase", m.phase, nullptr);
        node.setProperty("depth", m.depth, nullptr);
        node.setProperty("bipolar", m.bipolar, nullptr);
        node.setProperty("retrigger", int(m.retrigger), nullptr);
        node.setProperty("waveform", int(m.waveform), nullptr);
        node.setProperty("shape", m.shape, nullptr);
        node.setProperty("smoothing", m.smoothing, nullptr);
        mods.addChild(node, -1, nullptr);
    }
    for (const auto& a : modulationState.assignments) {
        juce::ValueTree node("Assignment");
        node.setProperty("modulatorId", juce::int64(a.modulatorId), nullptr);
        node.setProperty("targetParameterId", a.targetParameterId, nullptr);
        node.setProperty("depth", a.depth, nullptr);
        node.setProperty("enabled", a.enabled, nullptr);
        mods.addChild(node, -1, nullptr);
    }
    state.addChild(mods, -1, nullptr);
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
    ModulationState savedModulation;
    const auto mods = restored.getChildWithName("Modulation");
    if (mods.getNumChildren() > int(ModulationBackend::maxModulators + ModulationBackend::maxAssignments)) return false;
    for (const auto& node : mods) {
        if (node.hasType("Modulator")) {
            Modulator m;
            m.id = uint32_t(juce::int64(node.getProperty("id", 0)));
            m.type = ModulatorType(int(node.getProperty("type", 0)));
            m.rateHz = node.getProperty("rateHz", 1.0f);
            m.sync = node.getProperty("sync", false);
            m.division = node.getProperty("division", 1.0f);
            m.phase = node.getProperty("phase", 0.0f);
            m.depth = node.getProperty("depth", 1.0f);
            m.bipolar = node.getProperty("bipolar", true);
            m.retrigger = ModulationRetrigger(int(node.getProperty("retrigger", 0)));
            m.waveform = ModulationWaveform(int(node.getProperty("waveform", 0)));
            m.shape = node.getProperty("shape", 0.5f);
            m.smoothing = node.getProperty("smoothing", 0.0f);
            savedModulation.modulators.push_back(m);
        } else if (node.hasType("Assignment")) {
            savedModulation.assignments.push_back({uint32_t(juce::int64(node.getProperty("modulatorId", 0))),
                node.getProperty("targetParameterId").toString(), float(node.getProperty("depth", 0.0f)),
                bool(node.getProperty("enabled", true))});
        } else return false;
    }
    juce::String modulationError;
    if (!setModulationState(savedModulation, modulationError)) return false;
    restored.removeChild(mods, nullptr);
    Graph draft = defaultGraph(), active = draft;
    const auto graphTree = restored.getChildWithName("RoutingGraph");
    const auto activeTree = restored.getChildWithName("ActiveRoutingGraph");
    juce::String graphError;
    if (graphTree.isValid() && !decodeProcessorGraph(graphTree, draft)) {
        draft = defaultGraph();
        graphError = "Malformed saved routing draft";
    }
    if (!decodeProcessorGraph(activeTree, active) || !processorRouting::validate(active))
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
            if (graphTree.isValid() && ((i >= 43 && i < 46) || (i >= 94 && i < 139))) {
                const size_t slot = i < 46 ? 0 : 1 + (i - 94) / 3;
                const size_t field = i < 46 ? i - 43 : (i - 94) % 3;
                for (const auto& node : active.nodes)
                    if (processorRouting::isEffect(node.type) && processorRouting::fxSlot(node.id) == int(slot))
                        initial = field == 0 ? (node.enabled ? 1.0f : 0.0f)
                            : 100 * (field == 1 ? node.mix : node.motion);
            }

            if (graphTree.isValid() && i >= 139 && i < originalParameterCount) {
                const size_t slot = 1 + (i - 139) / toneParametersPerSlot;
                const size_t field = (i - 139) % toneParametersPerSlot;
                for (const auto& node : active.nodes)
                    if (processorRouting::isEffect(node.type) && node.type != NodeType::chorale
                        && processorRouting::fxSlot(node.id) == int(slot))
                        initial = processorToneValues(node)[field];
            }
            if (graphTree.isValid() && i >= waveExtraParameterStart && i < waveQualityParameterStart) {
                const size_t slot = 1 + (i - waveExtraParameterStart) / 2;
                for (const auto& node : active.nodes)
                    if (node.type == NodeType::waveshaper && processorRouting::fxSlot(node.id) == int(slot))
                        initial = (i - waveExtraParameterStart) % 2 == 0
                            ? (node.waveshaper.raw ? 1.0f : 0.0f) : node.waveshaper.shapeAmount * 100;
            }
            if (graphTree.isValid() && i >= waveQualityParameterStart && i < waveLevelMatchParameterStart) {
                const size_t slot = 1 + i - waveQualityParameterStart;
                for (const auto& node : active.nodes)
                    if (node.type == NodeType::waveshaper && processorRouting::fxSlot(node.id) == int(slot))
                        initial = float(node.waveshaper.quality);
            }
            if (graphTree.isValid() && i >= waveLevelMatchParameterStart && i < localMixParameterStart) {
                const size_t slot = 1 + i - waveLevelMatchParameterStart;
                for (const auto& node : active.nodes)
                    if (node.type == NodeType::waveshaper && processorRouting::fxSlot(node.id) == int(slot))
                        initial = node.waveshaper.levelMatch ? 1.0f : 0.0f;
            }
            if (graphTree.isValid() && i >= localMixParameterStart && i < waveAlgorithmParameterStart) {
                const auto id = uint32_t(200 + i - localMixParameterStart);
                for (const auto& node : active.nodes)
                    if (node.type == NodeType::mix && !node.mainControls && node.id == id)
                        initial = node.mix * 100;
            }
            // Older three-shape sessions stay in Follow Legacy mode. A graph-only
            // expanded curve can seed the new selector without remapping old automation.
            if (graphTree.isValid() && i >= waveAlgorithmParameterStart && i < creativeParameterStart) {
                const size_t slot = 1 + i - waveAlgorithmParameterStart;
                for (const auto& node : active.nodes)
                    if (node.type == NodeType::waveshaper && processorRouting::fxSlot(node.id) == int(slot)
                        && node.waveshaper.shape > 2)
                        initial = float(node.waveshaper.shape + 1);
            }
            if (graphTree.isValid() && i >= creativeParameterStart && i < utilityParameterStart) {
                const size_t offset = i - creativeParameterStart;
                const bool legacy = offset < 15 * legacyCreativeParameterCount;
                const size_t width = legacy ? legacyCreativeParameterCount : releasedCreativeParameterCount - legacyCreativeParameterCount;
                const size_t relative = legacy ? offset : offset - 15 * legacyCreativeParameterCount;
                const size_t slot = 1 + relative / width;
                const size_t field = relative % width + (legacy ? 0 : legacyCreativeParameterCount);
                for (const auto& node : active.nodes)
                    if (isCreativeEffect(node.type) && processorRouting::fxSlot(node.id) == int(slot))
                        initial = node.creative.values[field];
            }
            if (graphTree.isValid() && i >= newCreativeParameterStart && i < spectralMorphParameterStart) {
                const size_t width = creativeParameterCount - releasedCreativeParameterCount;
                const size_t slot = 1 + (i - newCreativeParameterStart) / width;
                const size_t field = releasedCreativeParameterCount + (i - newCreativeParameterStart) % width;
                for (const auto& node : active.nodes)
                    if (isCreativeEffect(node.type) && processorRouting::fxSlot(node.id) == int(slot))
                        initial = node.creative.values[field];
            }
            if (i >= spectralMorphParameterStart && i < stereoMonoParameterStart) {
                const size_t slot = 1 + (i - spectralMorphParameterStart) / spectralMorphParameterCount;
                const size_t field = (i - spectralMorphParameterStart) % spectralMorphParameterCount;
                initial = spectralMorphParameters[field].initial;
                for (const auto& node : active.nodes) if (node.type == NodeType::spectralMorph && routingFxSlot(node.id) == int(slot))
                    initial = spectralMorphValues(node.spectralMorph)[field];
            }
            if (i >= stereoMonoParameterStart) {
                initial = 0;
                for (const auto& node : draft.nodes)
                    if (node.type == NodeType::stereo && processorRouting::fxSlot(node.id) == int(i - stereoMonoParameterStart + 1))
                        initial = node.monoAudition ? 1.f : 0.f;
            }
            if (graphTree.isValid() && i >= utilityParameterStart && i < newCreativeParameterStart) {
                const size_t slot = 1 + (i - utilityParameterStart) / utilityParameterCount;
                const size_t field = (i - utilityParameterStart) % utilityParameterCount;
                for (const auto& node : active.nodes)
                    if (isUtilityEffect(node.type) && processorRouting::fxSlot(node.id) == int(slot))
                        initial = node.utility.values[field];
            }
            initial = parameter->convertFrom0to1(parameter->convertTo0to1(initial));
            missing.setProperty("value", initial, nullptr);
            restored.addChild(missing, -1, nullptr);
        }
    }
    const bool initialized = legacyVoices
        || static_cast<bool>(restored.getProperty("advancedInitialized", hasAdvancedState));
    skin.store(std::clamp(int(restored.getProperty("skin",
        static_cast<bool>(restored.getProperty(themeProperty, false)) ? 1 : 0)), 0, 2), std::memory_order_relaxed);
    const double motion = double(restored.getProperty("motionPercent", 3.0));
    motionPercent.store(std::isfinite(motion) ? std::clamp(motion, 0.0, 100.0) : 3.0, std::memory_order_relaxed);
    advancedVisible.store(static_cast<bool>(restored.getProperty("advancedVisible", false)), std::memory_order_relaxed);
    parameters.replaceState(restored);
    advancedInitialized.store(initialized, std::memory_order_relaxed);
    active = removeLegacySumNodes(active);
    if (!processorRouting::validate(active)) active = defaultGraph();
    routingDraft = active;
    routingLayout = std::move(layout);
    routingError = graphError;
    restartPending = prepared && RoutingExecutor::graphLatency(active) != preparedRoutingLatency;
    if (!restartPending) {
        if (prepared) {
            const auto requested = engine.requestGraph(active);
            if (!requested) { routingError = juce::String(requested.message); return false; }
        }
        routingActive = std::move(active);
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

void Processor::setSkin(int requested) {
    const int next = std::clamp(requested, 0, 2);
    if (skin.exchange(next, std::memory_order_relaxed) != next)
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void Processor::setMotionPercent(double requested) {
    const double next = std::isfinite(requested) ? std::clamp(requested, 0.0, 100.0) : 3.0;
    if (motionPercent.exchange(next, std::memory_order_relaxed) != next)
        updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
}

void Processor::setFlash(bool enabled) {
    setSkin(enabled ? 1 : 0);
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
    setModulationState({}, error);
    endModulationAssignment();
    setRoutingGraphLocked(defaultGraph(), error);
    advancedInitialized.store(false, std::memory_order_relaxed);
}

void Processor::timerCallback() {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    if (prepared) engine.collectRetiredGraphs();
}

Graph Processor::qualitySnapshot(Graph graph) const {
    // Graph views/state show requested host/base quality, never a live modulated
    // value. Keep Raw's dormant quality visible and persistent as well.
    for (auto& node : graph.nodes) {
        if (node.type == NodeType::stereo) {
            const int slot = routingFxSlot(node.id);
            if (slot > 0) node.monoAudition = values[stereoMonoParameterStart + size_t(slot - 1)]->load(std::memory_order_relaxed) >= .5f;
        }
        if (node.type == NodeType::mix && !node.mainControls && node.id >= 200 && node.id < 248)
            node.mix = values[localMixParameterStart + node.id - 200]->load(std::memory_order_relaxed) * .01f;
        if (node.type == NodeType::spectralMorph) {
            const int spectralSlot = routingFxSlot(node.id);
            if (spectralSlot > 0) {
                std::array<float, spectralMorphParameterCount> v{};
                for (size_t p = 0; p < v.size(); ++p) v[p] = values[spectralMorphParameterIndex(size_t(spectralSlot), p)]->load(std::memory_order_relaxed);
                node.spectralMorph = spectralMorphFromValues(v);
            }
        }
        if (node.type != NodeType::waveshaper) continue;
        const int slot = processorRouting::fxSlot(node.id);
        if (slot > 0 && slot < 16) {
            node.waveshaper.quality = std::clamp(int(values[waveQualityParameterStart + size_t(slot - 1)]->load(
                std::memory_order_relaxed)), 0, 2);
            node.waveshaper.levelMatch = values[waveLevelMatchParameterStart + size_t(slot - 1)]->load(std::memory_order_relaxed) >= 0.5f;
        }
    }
    return graph;
}

Graph Processor::getRoutingGraph() const {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    return qualitySnapshot(routingDraft);
}

juce::String Processor::getRoutingError() const {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    return routingError;
}

bool Processor::routingRestartPending() const {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    return restartPending;
}

bool Processor::setRoutingGraphLocked(const Graph& graph, juce::String& error) {
    // Accepted latency-changing edits are explicit drafts, never audio plans.
    // Rejected edits leave both the running graph and draft untouched.
    Graph boundedGraph = graph;
    for (auto& node : boundedGraph.nodes) boundProcessorTone(node);
    const auto sourceValidation = processorRouting::validate(boundedGraph);
    if (!sourceValidation) {
        error = routingError = juce::String(sourceValidation.message);
        return false;
    }
    Graph candidate = removeLegacySumNodes(boundedGraph);
    const auto validation = processorRouting::validate(candidate);
    if (!validation) {
        error = routingError = juce::String(validation.message);
        return false;
    }
    const bool defer = prepared && RoutingExecutor::graphLatency(candidate) != preparedRoutingLatency;
    if (defer) {
        // Host parameters share effect slots. A pending replacement must not
        // initialize controls belonging to a different, still-running machine.
        for (const auto& old : routingActive.nodes) {
            if (!processorRouting::isEffect(old.type)) continue;
            const auto replacement = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
                [&](const RoutingNode& n) { return n.id == old.id; });
            if (replacement != candidate.nodes.end() && replacement->type != old.type) {
                error = routingError = "Deactivate the plugin before replacing an occupied FX slot in a latency-changing graph";
                return false;
            }
        }
    }
    if (prepared && !defer) {
        const auto requested = engine.requestGraph(candidate);
        if (!requested) {
            error = routingError = juce::String(requested.message);
            return false;
        }
    }
    // Retire assignments when an FX machine disappears or changes type; stable
    // host slot IDs must never make a replacement inherit another machine's map.
    auto retainedModulation = modulationState;
    for (const auto& old : routingActive.nodes) {
        if (defer) break; // Do not remove modulation from the still-running graph.
        if (!processorRouting::isEffect(old.type) && old.type != NodeType::mix) continue;
        const auto same = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [&](const RoutingNode& n) { return n.id == old.id && n.type == old.type; });
        if (same != candidate.nodes.end()) continue;
        if (old.type == NodeType::mix) {
            const auto id = old.mainControls ? juce::String("double") : localMixParameterId(old.id);
            auto& assignments = retainedModulation.assignments;
            assignments.erase(std::remove_if(assignments.begin(), assignments.end(), [&](const Assignment& a) {
                return a.targetParameterId == id;
            }), assignments.end());
            continue;
        }
        const int slot = processorRouting::fxSlot(old.id);
        const auto prefix = slot == 0 ? juce::String("chorale") : "fx" + juce::String(slot);
        auto& assignments = retainedModulation.assignments;
        assignments.erase(std::remove_if(assignments.begin(), assignments.end(), [&](const Assignment& a) {
            if (!a.targetParameterId.startsWith(prefix)) return false;
            if (slot == 0) return true;
            const auto suffix = a.targetParameterId.substring(prefix.length());
            return !suffix.isEmpty() && !juce::CharacterFunctions::isDigit(suffix[0]);
        }), assignments.end());
    }
    if (retainedModulation.assignments.size() != modulationState.assignments.size()) {
        juce::String ignored;
        setModulationState(retainedModulation, ignored);
    }
    routingDraft = candidate;
    restartPending = defer;
    if (!defer) routingActive = std::move(candidate);
    routingError.clear();
    error.clear();
    return true;
}

bool Processor::setRoutingGraph(const Graph& graph, juce::String& error) {
    bool accepted;
    {
        const std::lock_guard<std::recursive_mutex> lock(controlMutex);
        const auto previous = qualitySnapshot(routingDraft);
        accepted = setRoutingGraphLocked(graph, error);
        if (accepted) {
            // Only intentional inspector edits/new nodes update automation.
            // A cable-only edit must not overwrite host-automated FX values.
            for (const auto& node : routingDraft.nodes) {
                if (node.type == NodeType::mix && !node.mainControls && node.id >= 200 && node.id < 248) {
                    const auto oldMix = std::find_if(previous.nodes.begin(), previous.nodes.end(), [&](const RoutingNode& n) {
                        return n.id == node.id && n.type == NodeType::mix && !n.mainControls;
                    });
                    if (oldMix == previous.nodes.end() || oldMix->mix != node.mix)
                        setParameterValue(parameterIds[localMixParameterStart + node.id - 200], node.mix * 100);
                }
                if (!processorRouting::isEffect(node.type)) continue;
                const auto old = std::find_if(previous.nodes.begin(), previous.nodes.end(),
                    [&](const RoutingNode& n) { return n.id == node.id && n.type == node.type; });
                const auto slot = size_t(processorRouting::fxSlot(node.id));
                if (old == previous.nodes.end() || old->enabled != node.enabled)
                    setParameterValue(fxParameterId(slot, size_t(0)), node.enabled ? 1.0f : 0.0f);
                if (old == previous.nodes.end() || old->mix != node.mix)
                    setParameterValue(fxParameterId(slot, size_t(1)), node.mix * 100);
                if (old == previous.nodes.end() || old->motion != node.motion)
                    setParameterValue(fxParameterId(slot, size_t(2)), node.motion * 100);
                if (slot > 0 && node.type == NodeType::spectralMorph) {
                    const auto next = spectralMorphValues(node.spectralMorph);
                    const auto before = old == previous.nodes.end() ? next : spectralMorphValues(old->spectralMorph);
                    for (size_t p = 0; p < next.size(); ++p)
                        if (old == previous.nodes.end() || next[p] != before[p])
                            setParameterValue(parameterIds[spectralMorphParameterIndex(slot, p)], next[p]);
                }
                if (slot > 0 && isCreativeEffect(node.type)) {
                    for (size_t p = 0; p < creativeParameterCount; ++p)
                        if (old == previous.nodes.end() || node.creative.values[p] != old->creative.values[p])
                            setParameterValue(parameterIds[creativeParameterIndex(slot, p)], node.creative.values[p]);
                }
                if (slot > 0 && node.type == NodeType::stereo && (old == previous.nodes.end() || node.monoAudition != old->monoAudition))
                    setParameterValue(parameterIds[stereoMonoParameterStart + slot - 1], node.monoAudition ? 1.f : 0.f);
                if (slot > 0 && isUtilityEffect(node.type)) {
                    for (size_t p = 0; p < utilityParameterCount; ++p)
                        if (old == previous.nodes.end() || node.utility.values[p] != old->utility.values[p])
                            setParameterValue(parameterIds[utilityParameterStart + (slot - 1) * utilityParameterCount + p], node.utility.values[p]);
                }
                if (slot > 0 && node.type != NodeType::chorale && node.type != NodeType::spectralMorph && !isCreativeEffect(node.type) && !isUtilityEffect(node.type)) {
                    const auto next = processorToneValues(node);
                    const auto before = old == previous.nodes.end() ? next : processorToneValues(*old);
                    if (node.type == NodeType::waveshaper && (old == previous.nodes.end() || node.waveshaper.shape != old->waveshaper.shape))
                        setParameterValue(parameterIds[waveAlgorithmParameterStart + slot - 1], float(node.waveshaper.shape + 1));
                    if (old == previous.nodes.end() || node.waveshaper.raw != old->waveshaper.raw)
                        setParameterValue(parameterIds[waveExtraParameterStart + (slot - 1) * 2], node.waveshaper.raw ? 1 : 0);
                    if (old == previous.nodes.end() || node.waveshaper.shapeAmount != old->waveshaper.shapeAmount)
                        setParameterValue(parameterIds[waveExtraParameterStart + (slot - 1) * 2 + 1], node.waveshaper.shapeAmount * 100);
                    if (old == previous.nodes.end() || node.waveshaper.quality != old->waveshaper.quality)
                        setParameterValue(parameterIds[waveQualityParameterStart + slot - 1], float(node.waveshaper.quality));
                    if (old == previous.nodes.end() || node.waveshaper.levelMatch != old->waveshaper.levelMatch)
                        setParameterValue(parameterIds[waveLevelMatchParameterStart + slot - 1], node.waveshaper.levelMatch ? 1 : 0);
                    for (size_t f = 0; f < next.size(); ++f)
                        if (old == previous.nodes.end() || next[f] != before[f])
                            setParameterValue(parameterIds[toneParameterIndex(slot, f)], next[f]);
                }
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
    // New voices join the main wet input directly; compensation is automatic.
    // Do not invent a destination when a custom graph omits the main Mix.
    for (const auto& mix : routingDraft.nodes) {
        if (mix.type != NodeType::mix || !mix.mainControls) continue;
        if (routingDraft.links.size() < RoutingExecutor::maxLinks)
            routingDraft.links.push_back({sourceId, mix.id, 1});
        return;
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
        auto state = modulationState;
        const auto prefix = "voice" + juce::String(int(slot + 1));
        state.assignments.erase(std::remove_if(state.assignments.begin(), state.assignments.end(),
            [&](const Assignment& a) { return a.targetParameterId.startsWith(prefix); }), state.assignments.end());
        juce::String modulationError;
        setModulationState(state, modulationError);
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

VoiceSnapshot Processor::getLeadSnapshot() const {
    return voiceBridge.leadSnapshot();
}

ModulationState Processor::getModulationState() const {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    return modulationState;
}

bool Processor::setModulationState(const ModulationState& state, juce::String& error) {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    error.clear();
    const auto fail = [&](const char* text) { error = text; return false; };
    if (state.modulators.size() > ModulationBackend::maxModulators
        || state.assignments.size() > ModulationBackend::maxAssignments)
        return fail("Modulation capacity exceeded (16 modulators / 256 assignments)");
    ModulationBackend::Plan plan;
    for (const auto& m : state.modulators) {
        if (!m.id || !std::isfinite(m.rateHz) || m.rateHz < 0.001f || m.rateHz > 100
            || !std::isfinite(m.division) || m.division < 0.015625f || m.division > 256
            || !std::isfinite(m.phase) || m.phase < 0 || m.phase >= 1
            || !std::isfinite(m.depth) || m.depth < 0 || m.depth > 1
            || !std::isfinite(m.shape) || m.shape < 0 || m.shape > 1
            || !std::isfinite(m.smoothing) || m.smoothing < 0 || m.smoothing > 1
            || int(m.type) < 0 || int(m.type) > 1 || int(m.waveform) < 0 || int(m.waveform) > 3
            || int(m.retrigger) < 0 || int(m.retrigger) > 2)
            return fail("Invalid modulator controls or identity");
        for (size_t i = 0; i < plan.modulatorCount; ++i)
            if (plan.modulators[i].id == m.id) return fail("Duplicate modulator ID");
        plan.modulators[plan.modulatorCount++] = m;
    }
    for (size_t aIndex = 0; aIndex < state.assignments.size(); ++aIndex) {
        const auto& a = state.assignments[aIndex];
        size_t source = plan.modulatorCount, target = parameterIds.size();
        for (size_t i = 0; i < plan.modulatorCount; ++i) if (plan.modulators[i].id == a.modulatorId) source = i;
        for (size_t i = 0; i < parameterIds.size(); ++i) if (a.targetParameterId == parameterIds[i]) target = i;
        if (source == plan.modulatorCount || target == parameterIds.size()) return fail("Unknown modulation source or parameter ID");
        if (target >= stereoMonoParameterStart) return fail("Mono Audition is host automation only, not a continuous modulation target");
        if (target >= spectralMorphParameterStart) {
            const auto field = (target - spectralMorphParameterStart) % spectralMorphParameterCount;
            if (spectralMorphParameters[field].discrete) return fail("Spectral Mode, FFT and Freeze are host automation only, not continuous modulation targets");
        }
        if (target == 8 || target == 10 || (target >= 86 && target < 94))
            return fail("Bypass and structural controls cannot be modulation targets");
        if (!std::isfinite(a.depth) || std::abs(a.depth) > 1) return fail("Assignment depth must be -1..1");
        for (size_t previous = 0; previous < aIndex; ++previous)
            if (state.assignments[previous].modulatorId == a.modulatorId
                && state.assignments[previous].targetParameterId == a.targetParameterId)
                return fail("Duplicate modulation assignment");
        if (a.enabled) plan.assignments[plan.assignmentCount++] = {
            uint16_t(source), uint16_t(target), a.depth};
    }
    if (!modulation.publish(plan)) return fail("Modulation publication busy; retry the edit");
    modulationState = state;
    const auto assigning = assignmentId.load();
    if (assigning && std::none_of(state.modulators.begin(), state.modulators.end(),
        [=](const Modulator& m) { return m.id == assigning; })) assignmentId.store(0);
    updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true));
    return true;
}

void Processor::beginModulationAssignment(uint32_t id) {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    for (const auto& m : modulationState.modulators)
        if (m.id == id) { assignmentId.store(id); return; }
    assignmentId.store(0);
}
void Processor::endModulationAssignment() { assignmentId.store(0); }
uint32_t Processor::modulationAssignmentId() const noexcept { return assignmentId.load(); }
bool Processor::setModulationDepth(uint32_t id, const juce::String& parameterId, float depth) {
    const std::lock_guard<std::recursive_mutex> lock(controlMutex);
    auto state = modulationState;
    auto found = std::find_if(state.assignments.begin(), state.assignments.end(),
        [&](const Assignment& a) { return a.modulatorId == id && a.targetParameterId == parameterId; });
    if (found != state.assignments.end()) found->depth = depth;
    else state.assignments.push_back({id, parameterId, depth, true});
    juce::String error;
    return setModulationState(state, error);
}

void Processor::updateModulationValues() {
    modulationSnapshotSequence.fetch_add(1);
    modulation.tick(modulationContributions);
    const auto targets = modulation.targets();
    if (!targets.empty())
        for (size_t i = 0; i < values.size(); ++i)
            effectiveValues[i] = values[i]->load(std::memory_order_relaxed);
    std::array<bool, parameterIds.size()> nextTargetMask{};
    for (const size_t i : targets) {
        nextTargetMask[i] = true;
        const float raw = effectiveValues[i];
        auto* parameter = rangedParameters[i];
        const float base = parameter->convertTo0to1(raw);
        const float normalized = std::clamp(base + modulationContributions[i], 0.0f, 1.0f);
        float effective = parameter->convertFrom0to1(normalized);
        // Keep the Schmitt state even when summed contributions cross zero.
        // Dwell is measured in samples, not ticks: extra note ticks cannot
        // shorten the switching interval or make it depend on host block size.
        if (steppedParameters[i]) {
            const float previousRaw = activeTargetMask[i] ? snapshotEffective[i].load() : raw;
            const float previous = parameter->convertTo0to1(previousRaw);
            const float proposed = parameter->convertTo0to1(effective);
            const float step = 1.0f / float(std::max(1, parameter->getNumSteps() - 1));
            if (proposed != previous) {
                if (processedSamples < discreteHoldUntil[i] || std::abs(normalized - previous) < step * 0.6f)
                    effective = previousRaw;
                else discreteHoldUntil[i] = processedSamples + uint64_t(processingSampleRate * 0.020);
            }
        }
        effectiveValues[i] = effective;
        snapshotBase[i].store(raw);
        snapshotEffective[i].store(effective);
        snapshotTargeted[i].store(true);
    }
    for (size_t i = 0; i < values.size(); ++i)
        if (activeTargetMask[i] && !nextTargetMask[i]) {
            snapshotTargeted[i].store(false);
            discreteHoldUntil[i] = 0;
        }
    activeTargetMask = nextTargetMask;
    modulationSnapshotSequence.fetch_add(1);
}

ModulationSnapshot Processor::getModulationSnapshot() const {
    ModulationSnapshot result;
    result.targets.reserve(parameterIds.size());
    std::array<float, parameterIds.size()> baseRaw{}, effectiveRaw{};
    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto before = modulationSnapshotSequence.load();
        if (before & 1) continue;
        bool sourcesConsistent = false;
        result.modulators = modulation.snapshots(&sourcesConsistent);
        result.targets.clear();
        if (!sourcesConsistent) continue;
        for (size_t i = 0; i < values.size(); ++i) {
            if (i == 8 || i == 10 || (i >= 86 && i < 94)) continue;
            if (i >= stereoMonoParameterStart) continue;
            if (i >= spectralMorphParameterStart && spectralMorphParameters[(i - spectralMorphParameterStart) % spectralMorphParameterCount].discrete) continue;
            const bool assigned = snapshotTargeted[i].load();
            const float raw = assigned ? snapshotBase[i].load() : values[i]->load(std::memory_order_relaxed);
            baseRaw[i] = raw;
            effectiveRaw[i] = assigned ? snapshotEffective[i].load() : raw;
        }
        if (before != modulationSnapshotSequence.load()) continue;
        // Finish expensive normalization/String construction only after copying
        // a stable frame; a UI read need not fit those operations between ticks.
        for (size_t i = 0; i < values.size(); ++i) {
            if (i == 8 || i == 10 || (i >= 86 && i < 94)) continue;
            if (i >= stereoMonoParameterStart) continue;
            if (i >= spectralMorphParameterStart && spectralMorphParameters[(i - spectralMorphParameterStart) % spectralMorphParameterCount].discrete) continue;
            const float base = rangedParameters[i]->convertTo0to1(baseRaw[i]);
            const float effective = rangedParameters[i]->convertTo0to1(effectiveRaw[i]);
            result.targets.push_back({parameterIds[i], base, effective, effective - base});
        }
        return result;
    }
    result.consistent = false;
    result.modulators.clear();
    result.targets.clear();
    return result;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }

} // namespace vocalstack

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vocalstack::Processor(); }
