#pragma once

#include "PluginProcessor.h"
#include "ProcessorRoutingState.h"
#include <cmath>
#include <stdexcept>
#include <unordered_set>

// MAIN can call run() from the editor test runner without changing its existing
// UI regression header. These tests do not construct an editor or reconfigure DSP
// in response to automation.
namespace vocalstack::toneProcessorTests {

inline void require(bool condition, const juce::String& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}
inline void near(float actual, float expected, const juce::String& message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= 0.0001f * std::max(1.0f, std::abs(expected)), message);
}
inline void write(Processor& p, const juce::String& id, float raw) {
    auto* parameter = p.parameters.getParameter(id);
    require(parameter != nullptr, "Missing tone parameter: " + id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(raw));
}
inline float read(Processor& p, const juce::String& id) {
    const auto* value = p.parameters.getRawParameterValue(id);
    require(value != nullptr, "Missing raw tone parameter: " + id);
    return value->load(std::memory_order_relaxed);
}
inline juce::ValueTree state(Processor& p) {
    const auto xml = juce::parseXML(p.settingsText());
    require(xml != nullptr, "Missing processor XML");
    return juce::ValueTree::fromXml(*xml);
}
inline Graph graph(NodeType type) {
    return {{{1, NodeType::lead}, {100, type}, {13, NodeType::output}},
            {{1, 100, 0, 0}, {100, 13, 0, 0}}};
}

inline void parameterContract() {
    Processor p;
    require(p.getParameters().size() == 1727 && parameterIds.size() == 1727 && creativeParameterStart == 1007,
            "Incorrect host count or creative append offset");
    std::unordered_set<std::string> unique;
    for (size_t i = 0; i < parameterIds.size(); ++i) {
        require(unique.insert(parameterIds[i]).second, "Duplicate host ID");
        require(p.getParameters()[int(i)] == p.parameters.getParameter(parameterIds[i]), "Host index changed");
        if (i < 139) require(std::string_view(parameterIds[i]) == legacyParameterIds[i], "Original 139 IDs changed");
    }
    for (size_t slot = 1; slot < 16; ++slot) {
        for (size_t f = 0; f < 9; ++f)
            require(toneParameterId(slot, toneParameterFields[f]) == parameterIds[toneParameterIndex(slot, f)], "Tone helper offset");
        for (size_t b = 0; b < 8; ++b)
            for (size_t f = 0; f < 5; ++f)
                require(eqBandParameterId(slot, b, eqBandParameterFields[f]) == parameterIds[toneParameterIndex(slot, 9 + b * 5 + f)], "Band helper offset");
    }
    constexpr std::array<float, 9> low{0, -24, 0, -24, -24, 0, 2, 20, 20};
    constexpr std::array<float, 9> high{36, 12, 2, 24, 24, 1, 3, 20000, 20000};
    for (size_t f = 0; f < low.size(); ++f) {
        const auto* parameter = p.parameters.getParameter(toneParameterId(15, toneParameterFields[f]));
        near(parameter->getNormalisableRange().start, low[f], "Tone range minimum");
        near(parameter->getNormalisableRange().end, high[f], "Tone range maximum");
    }
    write(p, toneParameterId(1, "WaveDrive"), 12);
    require(!bool(state(p).getProperty("advancedInitialized")), "Tone automation initialized voices");
    const auto fx = p.settings().fxSettings[1];
    near(fx.waveshaper.driveDb, 12, "Cached wave bridge");
    write(p, eqBandParameterId(15, 7, "On"), 1);
    write(p, eqBandParameterId(15, 7, "Mode"), 13);
    write(p, eqBandParameterId(15, 7, "Q"), 18);
    const auto band = p.settings().fxSettings[15].eq.bands[7];
    require(band.enabled && band.mode == 13, "Cached final EQ band bridge");
    near(band.q, 18, "Cached Q bridge");
    require(toneParameterId(0, "WaveDrive").isEmpty() && toneParameterId(16, "EqGain").isEmpty()
        && toneParameterId(1, "eqgain").isEmpty() && toneParameterId(1, nullptr).isEmpty()
        && eqBandParameterId(1, 8, "On").isEmpty(), "Invalid helper arguments accepted");
}

inline void stateAndLegacyMigration() {
    Processor source;
    // This fixture dirties the original tone block, not later Lead/global controls.
    for (size_t i = 139; i < originalParameterCount; ++i) {
        auto* parameter = source.parameters.getParameter(parameterIds[i]);
        parameter->setValueNotifyingHost((i % 3 == 0) ? 0.19f : 0.81f);
    }
    const auto saved = state(source);
    juce::MemoryBlock binary;
    source.getStateInformation(binary);
    Processor restored;
    restored.setStateInformation(binary.getData(), int(binary.getSize()));
    Processor pasted;
    require(pasted.pasteSettings(source.settingsText()), "Tone paste failed");
    for (size_t i = 139; i < originalParameterCount; ++i) {
        const auto* id = parameterIds[i];
        const auto raw = read(source, id);
        near(float(saved.getChildWithProperty("id", id).getProperty("value")), raw, "Tone state is not raw");
        near(read(restored, id), raw, "Binary tone restore failed");
        near(read(pasted, id), raw, "Tone paste failed");
    }
    auto old = saved.createCopy();
    for (size_t i = 139; i < parameterIds.size(); ++i)
        old.removeChild(old.getChildWithProperty("id", parameterIds[i]), nullptr);
    require(restored.pasteSettings(old.toXmlString()), "Old 139 state rejected");
    constexpr std::array<float, 8> frequencies{60, 120, 250, 500, 1000, 2500, 6000, 12000};
    for (size_t i = 139; i < parameterIds.size(); ++i) {
        auto* parameter = restored.parameters.getParameter(parameterIds[i]);
        near(read(restored, parameterIds[i]), parameter->convertFrom0to1(parameter->getDefaultValue()), "Old state inherited dirty tone values");
    }
    for (size_t b = 0; b < 8; ++b) {
        near(read(restored, eqBandParameterId(15, b, "Frequency")), frequencies[b], "Legacy frequency spread");
        near(read(restored, eqBandParameterId(15, b, "On")), 0, "Legacy band enabled");
    }
    source.resetParameters();
    for (const auto* id : parameterIds) {
        auto* parameter = source.parameters.getParameter(id);
        near(read(source, id), parameter->convertFrom0to1(parameter->getDefaultValue()), "Reset missed host parameter");
    }
}

inline void modelAndCreation() {
    Processor p;
    auto g = graph(NodeType::equalizer);
    auto& n = g.nodes[1];
    n.waveshaper = {false, 13, -7, 0.6f, 2};
    n.eq.enabled = false;
    n.eq.shiftSemitones = 5;
    n.eq.gainDb = -3;
    n.eq.adaptiveQ = true;
    n.eq.bands[7] = {true, 12345, 7, 3, 13};
    n.bandCount = 2;
    n.crossoverLow = 350;
    n.crossoverHigh = 6500;
    juce::String error;
    require(p.setRoutingGraph(g, error), error);
    near(read(p, toneParameterId(1, "EqShift")), 5, "Creation did not seed EQ shift");
    near(read(p, eqBandParameterId(1, 7, "Frequency")), 12345, "Creation did not seed band");
    const auto authored = processorRouting::encode(p.getRoutingGraph(), "Graph");
    Graph decoded;
    require(processorRouting::decode(authored, decoded), "Tone model decode failed");
    require(processorRouting::encode(decoded, "Graph").toXmlString() == authored.toXmlString(), "Nested tone settings lost");
    write(p, toneParameterId(1, "EqShift"), 17);
    write(p, eqBandParameterId(1, 7, "Gain"), -9);
    g.links.clear();
    require(p.setRoutingGraph(g, error), error);
    near(read(p, toneParameterId(1, "EqShift")), 17, "Cable edit reset automation");
    near(read(p, eqBandParameterId(1, 7, "Gain")), -9, "Cable edit reset band automation");
    // Missing parameters use node-authored values, but present raw automation wins.
    auto saved = state(p);
    const auto shift = toneParameterId(1, "EqShift");
    saved.removeChild(saved.getChildWithProperty("id", shift), nullptr);
    require(p.pasteSettings(saved.toXmlString()), "Partial tone state rejected");
    near(read(p, shift), 5, "Missing tone control did not use model fallback");
    near(read(p, eqBandParameterId(1, 7, "Gain")), -9, "Model overwrote saved automation");
    g.nodes[1].type = NodeType::waveshaper;
    require(p.setRoutingGraph(g, error), error);
    near(read(p, toneParameterId(1, "WaveDrive")), 13, "Type change did not seed wave drive");
    auto invalid = g;
    invalid.nodes[1].id = 12;
    require(!p.setRoutingGraph(invalid, error), "Non-Chorale accepted legacy FX ID");
    invalid.nodes[1].id = 99;
    require(!p.setRoutingGraph(invalid, error), "Tone accepted unstable FX ID");
    auto boundedNode = g.nodes[1];
    boundedNode.waveshaper.driveDb = 100;
    boundedNode.waveshaper.outputDb = -100;
    boundedNode.eq.bands[0].q = 100;
    boundedNode.eq.bands[0].mode = 99;
    boundedNode.eq.shiftSemitones = 100;
    processorRouting::boundToneSettings(boundedNode);
    near(boundedNode.waveshaper.driveDb, 36, "Model drive bound");
    near(boundedNode.waveshaper.outputDb, -24, "Model output bound");
    near(boundedNode.eq.bands[0].q, 18, "Model Q bound");
    near(boundedNode.eq.shiftSemitones, 24, "Model shift bound");
    require(boundedNode.eq.bands[0].mode == 13, "Model mode bound");
    auto cross = graph(NodeType::crossover);
    cross.links.back().sourcePin = 2;
    require(processorRouting::decode(processorRouting::encode(cross, "Graph"), decoded), "Crossover decode failed");
    require(decoded.nodes[1].type == NodeType::crossover && decoded.links.back().sourcePin == 2, "Crossover source pin lost");
    cross.links.back().sourcePin = 3;
    require(!processorRouting::validate(cross), "Invalid crossover source pin accepted");
}

inline double renderTone(Processor& p, int& position) {
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    double power = 0;
    for (int block = 0; block < 120; ++block) {
        for (int i = 0; i < buffer.getNumSamples(); ++i, ++position) {
            const auto sample = float(0.05 * std::sin(6.283185307179586 * 1000.0 * position / 48000.0));
            buffer.setSample(0, i, sample);
            buffer.setSample(1, i, sample);
        }
        p.processBlock(buffer, midi);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const auto sample = buffer.getSample(0, i);
            require(std::isfinite(sample), "Non-finite tone audio");
            if (block >= 104) power += double(sample) * sample;
        }
    }
    return std::sqrt(power / (16 * 256));
}

inline void audioBridge() {
    for (auto type : {NodeType::waveshaper, NodeType::equalizer, NodeType::crossover}) {
        Processor p;
        write(p, "levelOn", 0);
        write(p, "doubleOn", 0);
        write(p, "formantOn", 0);
        auto g = graph(type);
        g.nodes[1].mix = 1;
        juce::String error;
        require(p.setRoutingGraph(g, error), error);
        p.prepareToPlay(48000, 256);
        const auto beforeGraph = processorRouting::encode(p.getRoutingGraph(), "Graph").toXmlString();
        int position = 0;
        const auto before = renderTone(p, position);
        if (type == NodeType::waveshaper) write(p, toneParameterId(1, "WaveDrive"), 12);
        if (type == NodeType::equalizer) write(p, toneParameterId(1, "EqGain"), 6);
        if (type == NodeType::crossover) write(p, toneParameterId(1, "CrossoverLow"), 4000);
        const auto after = renderTone(p, position);
        require(before > 1.0e-7 && after > before * 1.6, "Tone automation did not reach live audio");
        require(processorRouting::encode(p.getRoutingGraph(), "Graph").toXmlString() == beforeGraph, "Automation mutated authored graph");
        if (type == NodeType::crossover) {
            write(p, toneParameterId(1, "CrossoverLow"), 18000);
            write(p, toneParameterId(1, "CrossoverHigh"), 40);
            const auto settings = p.settings().fxSettings[1];
            near(settings.crossoverLow, 18000, "Low parameter reordered");
            near(settings.crossoverHigh, 40, "High parameter reordered");
            require(std::isfinite(renderTone(p, position)), "Crossed cutoffs broke DSP");
        }
        p.releaseResources();
    }
}

inline void run() {
    parameterContract();
    stateAndLegacyMigration();
    modelAndCreation();
    audioBridge();
}

} // namespace vocalstack::toneProcessorTests
