#pragma once

#include "PluginProcessor.h"
#include "ProcessorRoutingState.h"
#include "PresetManager.h"
#include "UtilityPanelTests.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

// Call utilityIntegrationTests::run() on the message thread after JUCE init.
// Exercises the real processor parameter/audio bridge and isolated utility controls.
namespace vocalstack::utilityIntegrationTests {
inline void require(bool ok, const juce::String& message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
inline void near(double a, double b, const juce::String& message) {
    require(std::isfinite(a) && std::abs(a - b) < 1.e-4 * std::max(1., std::abs(b)), message);
}
inline auto& parameter(Processor& p, const juce::String& id) {
    auto* result = p.parameters.getParameter(id);
    require(result != nullptr, "Missing utility parameter: " + id);
    return *result;
}
inline void write(Processor& p, const juce::String& id, float raw) {
    auto& target = parameter(p, id);
    target.setValueNotifyingHost(target.convertTo0to1(raw));
}
inline float read(Processor& p, const juce::String& id) {
    return parameter(p, id).convertFrom0to1(parameter(p, id).getValue());
}
inline Graph graph(NodeType type, uint32_t id = 100) {
    return {{{1, NodeType::lead}, {id, type}, {13, NodeType::output}}, {{1, id, 0}, {id, 13, 0}}};
}
inline void install(Processor& p, const Graph& g) {
    juce::String error;
    const bool accepted = p.setRoutingGraph(g, error);
    require(accepted, error);
}
inline juce::ValueTree state(Processor& p) {
    const auto xml = juce::parseXML(p.settingsText());
    require(xml != nullptr, "Utility state XML did not parse");
    return juce::ValueTree::fromXml(*xml);
}
inline void prepare(Processor& p, double rate = 48000, int channels = 2) {
    auto buses = p.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    buses.inputBuses.set(0, set); buses.outputBuses.set(0, set);
    require(p.setBusesLayout(buses), "Utility bus layout rejected");
    write(p, "levelOn", 0); write(p, "doubleOn", 0); write(p, "formantOn", 0);
    write(p, "outputGain", 0); write(p, "globalMix", 100);
    p.prepareToPlay(rate, 511);
}
inline void fill(juce::AudioBuffer<float>& audio, int position, double rate, bool silence = false) {
    for (int c = 0; c < audio.getNumChannels(); ++c) for (int i = 0; i < audio.getNumSamples(); ++i)
        audio.setSample(c, i, silence ? 0.f : float(.08 * std::sin(6.283185307179586 * (c ? 337 : 223) * (position + i) / rate)));
}
inline double render(Processor& p, int samples, double rate = 48000, int channels = 2, bool silence = false) {
    constexpr std::array<int, 7> blocks{1, 31, 7, 511, 127, 3, 64};
    juce::MidiBuffer midi;
    double energy = 0;
    for (int position = 0, block = 0; position < samples; ++block) {
        const int n = std::min(blocks[size_t(block) % blocks.size()], samples - position);
        juce::AudioBuffer<float> audio(channels, n); fill(audio, position, rate, silence);
        p.processBlock(audio, midi);
        for (int c = 0; c < channels; ++c) for (int i = 0; i < n; ++i) {
            const float value = audio.getSample(c, i);
            require(std::isfinite(value), "Nonfinite routed utility output");
            energy += double(value) * value;
        }
        position += n;
    }
    return energy;
}
inline constexpr std::array<NodeType, 5> types{NodeType::gain, NodeType::stereo, NodeType::gate, NodeType::xpFilter, NodeType::synthFilter};

inline void parameterContract() {
    auto p = std::make_unique<Processor>();
    require(creativeParameterCount == 68 && utilityParameterCount == 21 && utilityParameterStart == 1772
        && parameterIds.size() == 3177 && ModulationBackend::maxTargets == 4096, "Expanded host capacity contract");
    require(p->getParameters().size() == 3177, "Wrong utility host layout count");
    for (size_t i = 0; i < parameterIds.size(); ++i)
        require(p->getParameters()[int(i)] == p->parameters.getParameter(parameterIds[i]), "Host layout/index mismatch");
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < utilityParameterCount; ++i) {
        const auto id = utilityParameterId(slot, i);
        const auto& d = utilityParameters[i]; auto& target = parameter(*p, id);
        require(id == parameterIds[utilityParameterStart + (slot - 1) * utilityParameterCount + i], "Utility ID offset mismatch");
        near(target.getNormalisableRange().start, d.min, id + " minimum");
        near(target.getNormalisableRange().end, d.max, id + " maximum");
        near(read(*p, id), d.initial, id + " initial");
        if (d.discrete) {
            require(target.isDiscrete(), id + " must be discrete host automation");
            require(target.getNumSteps() == int(d.max - d.min + 1), id + " step count");
            if (d.max > 1) require(dynamic_cast<juce::AudioParameterInt*>(&target) != nullptr, id + " must not be Boolean");
            for (int mode = int(d.min); mode <= int(d.max); ++mode) {
                write(*p, id, float(mode)); near(read(*p, id), mode, id + " lost mode");
            }
        } else write(*p, id, d.min + .37f * (d.max - d.min));
        near(p->settings().fxSettings[slot].utility.values[i], read(*p, id), id + " cached settings bridge");
    }
    for (size_t i = 0; i < types.size(); ++i)
        require(int(types[i]) == 14 + int(i) && int(utilityKind(types[i])) == int(i), "Utility node enum/kind mapping");
    p->resetParameters();
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < utilityParameterCount; ++i)
        near(read(*p, utilityParameterId(slot, i)), utilityParameters[i].initial, "Reset missed utility parameter");
}

inline void roundTrips() {
    auto source = std::make_unique<Processor>();
    Graph g; g.nodes.push_back({1, NodeType::lead}); uint32_t previous = 1;
    for (size_t slot = 1; slot <= types.size(); ++slot) {
        RoutingNode n{uint32_t(99 + slot), types[slot - 1]};
        for (size_t i = 0; i < utilityParameterCount; ++i) {
            const auto& d = utilityParameters[i];
            n.utility.values[i] = d.discrete ? d.max : d.min + .43f * (d.max - d.min);
        }
        g.nodes.push_back(n); g.links.push_back({previous, n.id, 0}); previous = n.id;
    }
    g.nodes.push_back({13, NodeType::output}); g.links.push_back({previous, 13, 0});
    install(*source, g);
    Graph decoded;
    const auto tree = processorRouting::encode(g, "Graph");
    require(processorRouting::decode(tree, decoded), "Utility graph decode failed");
    require(processorRouting::encode(decoded, "Graph").toXmlString() == tree.toXmlString(), "Utility nested settings lost");
    for (size_t slot = 1; slot <= types.size(); ++slot) for (size_t i = 0; i < utilityParameterCount; ++i)
        near(read(*source, utilityParameterId(slot, i)), g.nodes[slot].utility.values[i], "New utility node failed to seed host");
    // Saved host automation and dormant slots are independent of authored graph controls.
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < utilityParameterCount; ++i)
        parameter(*source, utilityParameterId(slot, i)).setValueNotifyingHost(.71f);
    source->setSkin(2);
    ModulationState mods; Modulator lfo; lfo.id = 71; mods.modulators.push_back(lfo);
    mods.assignments.push_back({71, utilityParameterId(15, size_t(UtilityParameter::synthFeedback)), .2f, true});
    juce::String error; const bool accepted = source->setModulationState(mods, error); require(accepted, error);
    juce::MemoryBlock binary; source->getStateInformation(binary);
    auto restored = std::make_unique<Processor>(); auto pasted = std::make_unique<Processor>();
    restored->setStateInformation(binary.getData(), int(binary.getSize()));
    require(pasted->pasteSettings(source->settingsText()), "Utility paste rejected");
    for (auto* copy : {restored.get(), pasted.get()}) {
        require(copy->getSkin() == 2, "Third skin not recalled");
        for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < utilityParameterCount; ++i)
            near(read(*copy, utilityParameterId(slot, i)), read(*source, utilityParameterId(slot, i)), "Utility host state lost");
        for (const auto* name : {"RoutingGraph", "ActiveRoutingGraph", "Modulation"})
            require(state(*copy).getChildWithName(name).toXmlString() == state(*source).getChildWithName(name).toXmlString(), "Utility state section lost");
    }
    auto graphOnly = state(*source);
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < utilityParameterCount; ++i)
        graphOnly.removeChild(graphOnly.getChildWithProperty("id", utilityParameterId(slot, i)), nullptr);
    require(pasted->pasteSettings(graphOnly.toXmlString()), "Graph-only utility state rejected");
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < utilityParameterCount; ++i)
        near(read(*pasted, utilityParameterId(slot, i)), slot <= types.size() ? g.nodes[slot].utility.values[i] : utilityParameters[i].initial,
            "Missing utility parameter not seeded/defaulted");
    auto legacy = state(*source);
    legacy.removeProperty("skin", nullptr); legacy.setProperty("flash", true, nullptr);
    require(pasted->pasteSettings(legacy.toXmlString()) && pasted->getSkin() == 1 && pasted->isFlash(), "Legacy flash fallback");
    legacy.setProperty("skin", 99, nullptr);
    require(pasted->pasteSettings(legacy.toXmlString()) && pasted->getSkin() == 2, "Skin not bounded");
    pasted->setSkin(-1); require(pasted->getSkin() == 0, "Skin setter not bounded");
    pasted->setFlash(true); require(pasted->getSkin() == 1, "setFlash compatibility");
    pasted->setFlash(false); require(pasted->getSkin() == 0, "setFlash false compatibility");
}

inline void automationAndModulation() {
    using U = UtilityParameter;
    auto control = std::make_unique<Processor>(); auto automated = std::make_unique<Processor>();
    auto g = graph(NodeType::synthFilter, 114);
    g.nodes[1].utility[U::synthMode] = 5; g.nodes[1].utility[U::synthCutoff] = 100;
    g.nodes[1].utility[U::synthFeedback] = 90;
    for (auto* p : {control.get(), automated.get()}) { install(*p, g); prepare(*p); }
    automated->retainFxMonitor(15);
    juce::MidiBuffer midi;
    double tailEnergy = 0;
    for (int block = 0; block < 120; ++block) {
        const int size = block % 2 ? 511 : 127;
        juce::AudioBuffer<float> a(2, size), b(2, size); fill(a, block * 511, 48000, block > 70); b.makeCopyOf(a);
        if (block > 50) for (size_t i = 0; i < utilityParameterCount; ++i) {
            const auto id = utilityParameterId(15, i);
            if (utilityParameters[i].kind == UtilityKind::synthFilter) write(*automated, id, read(*automated, id));
            else parameter(*automated, id).setValueNotifyingHost(block % 2 ? .2f : .8f);
        }
        control->processBlock(a, midi); automated->processBlock(b, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < size; ++i) {
            require(std::isfinite(b.getSample(c, i)) && std::abs(a.getSample(c, i) - b.getSample(c, i)) < 1.e-6,
                "Repeated/dormant utility automation reset filter history");
            if (block > 85) tailEnergy += double(b.getSample(c, i)) * b.getSample(c, i);
        }
    }
    require(tailEnergy > 1.e-9, "History comparison did not exercise a comb tail");
    FxMonitorFrame before, after;
    require(automated->readFxMonitor(15, before), "Utility monitor did not publish");
    const auto authored = processorRouting::encode(automated->getRoutingGraph(), "Graph").toXmlString();
    const auto id = utilityParameterId(15, size_t(U::synthFeedback));
    write(*automated, id, 55); render(*automated, 4096, 48000, 2, true);
    require(automated->readFxMonitor(15, after) && after.epoch == before.epoch && after.sequence > before.sequence,
        "Utility automation republished graph/reset monitor epoch");
    require(processorRouting::encode(automated->getRoutingGraph(), "Graph").toXmlString() == authored, "Utility automation rewrote authored graph");
    install(*automated, automated->getRoutingGraph());
    near(read(*automated, id), 55, "Cable-only graph publication overwrote host automation");
    ModulationState mods; Modulator lfo; lfo.id = 81; lfo.waveform = ModulationWaveform::square;
    lfo.bipolar = false; lfo.rateHz = .1f; mods.modulators.push_back(lfo);
    mods.assignments.push_back({81, id, .3f, true});
    const auto mode = utilityParameterId(15, size_t(U::synthMode));
    mods.assignments.push_back({81, mode, -.3f, true});
    juce::String error; const bool accepted = automated->setModulationState(mods, error); require(accepted, error);
    render(*automated, 4096);
    const auto snapshot = automated->getModulationSnapshot();
    require(snapshot.consistent, "Inconsistent utility modulation snapshot");
    bool found = false, modeFound = false;
    for (const auto& target : snapshot.targets) {
        if (target.parameterId == id) {
            found = true; require(target.effectiveNormalized > target.baseNormalized, "High-index utility modulation did not reach audio snapshot");
        }
        if (target.parameterId == mode) {
            modeFound = true;
            const auto raw = parameter(*automated, mode).convertFrom0to1(target.effectiveNormalized);
            require(raw == std::round(raw), "Multiway utility modulation produced fractional mode");
        }
    }
    require(found && modeFound, "Utility modulation targets above 2048 were lost");
    near(read(*automated, id), 55, "Modulation wrote host base");
    g.nodes[1].type = NodeType::gain; install(*automated, g);
    require(automated->getModulationState().assignments.empty(), "Replacement utility inherited machine modulation map");
    automated->releaseFxMonitor(15);
}

inline void routedAudio() {
    for (auto type : types) for (double rate : {44100., 48000., 96000.}) for (int channels : {1, 2}) {
        auto p = std::make_unique<Processor>(); install(*p, graph(type)); prepare(*p, rate, channels);
        require(render(*p, 8192, rate, channels) > 1.e-9, "Routed utility produced only silence");
        write(*p, "fx1Mix", 0); render(*p, 8192, rate, channels);
        write(*p, "fx1On", 0); render(*p, 8192, rate, channels);
        p->reset(); near(render(*p, 4096, rate, channels, true), 0, "Reset utility pipeline not silent");
    }
    // Gate reduction is delivered through the same per-slot meter bridge.
    auto p = std::make_unique<Processor>(); install(*p, graph(NodeType::gate));
    write(*p, utilityParameterId(1, size_t(UtilityParameter::gateThreshold)), 0);
    prepare(*p); p->retainFxMonitor(1); render(*p, 24000);
    require(p->getCreativeGainReduction(1) > 1, "Gate gain-reduction bridge missing");
    write(*p, "fx1On", 0); render(*p, 12000);
    near(p->getCreativeGainReduction(1), 0, "Bypassed gate meter retained reduction");
    p->releaseFxMonitor(1);
}

inline void presetSkin() {
    auto p = std::make_unique<Processor>();
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("vocalstack-utility-presets", {}, false);
    require(folder.createDirectory().wasOk(), "Could not create utility preset fixture");
    {
        PresetManager manager(*p, folder.getChildFile("preferences.settings"));
        p->setSkin(2);
        require(PresetManager::factoryNames().size() >= 8, "Missing utility/filter/shimmer factories");
        for (int index = 0; index < PresetManager::factoryNames().size(); ++index) {
            const auto result = manager.load({PresetManager::factoryNames()[index], {}, index});
            require(result.wasOk(), result.getErrorMessage());
            require(p->getSkin() == 2, "Factory replaced presentation skin");
        }
        const auto undone = manager.undoLoad(); require(undone.wasOk(), undone.getErrorMessage());
        require(p->getSkin() == 2, "Preset undo replaced presentation skin");
    }
    folder.deleteRecursively();
}
inline bool run() {
    bool passed = true;
    const auto test = [&](const char* name, const auto& body) {
        try { body(); std::cout << "PASS utility integration / " << name << '\n'; }
        catch (const std::exception& error) { passed = false; std::cerr << "FAIL utility integration / " << name << ": " << error.what() << '\n'; }
    };
    test("host descriptor/index/discrete contract", [] { parameterContract(); });
    test("graph, host state, migration and skins", [] { roundTrips(); });
    test("comb automation history and high-index modulation", [] { automationAndModulation(); });
    test("routed mono/stereo/sample rates and gate metering", [] { routedAudio(); });
    test("factory and undo skin retention", [] { presetSkin(); });
    test("panel scheduling, immediate redraw and narrow cards", [] { UtilityPanelTests::run(); });
    return passed;
}
} // namespace vocalstack::utilityIntegrationTests
