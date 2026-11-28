#pragma once

#include "PluginProcessor.h"
#include "ProcessorRoutingState.h"
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <vector>

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
    auto processor = std::make_unique<Processor>();
    auto& p = *processor;
    require(p.getParameters().size() == 3897 && parameterIds.size() == 3897 && creativeParameterStart == 1007
        && legacyCreativeParameterCount == 48 && creativeParameterCount == 68
        && utilityParameterStart == 1772 && utilityParameterCount == 21,
            "Incorrect host count or appended parameter layout");
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
    for (size_t slot = 1; slot < 16; ++slot) {
        for (size_t f = 0; f < creativeParameterCount; ++f) {
            const auto index = f < 48 ? 1007 + (slot - 1) * 48 + f
                : f < 51 ? 1727 + (slot - 1) * 3 + f - 48
                : f < 62 ? 2087 + (slot - 1) * 11 + f - 51
                         : 2357 + (slot - 1) * 6 + f - 62;
            require(creativeParameterIndex(slot, f) == index, "Creative host index changed");
            require(creativeParameterId(slot, f) == parameterIds[index], "Creative helper offset");
        }
        for (size_t f = 0; f < utilityParameterCount; ++f)
            require(utilityParameterId(slot, f) == parameterIds[1772 + (slot - 1) * 21 + f], "Utility helper offset");
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
    auto sourceProcessor = std::make_unique<Processor>();
    auto& source = *sourceProcessor;
    // This fixture dirties the original tone block, not later Lead/global controls.
    for (size_t i = 139; i < originalParameterCount; ++i) {
        auto* parameter = source.parameters.getParameter(parameterIds[i]);
        parameter->setValueNotifyingHost((i % 3 == 0) ? 0.19f : 0.81f);
    }
    const auto saved = state(source);
    juce::MemoryBlock binary;
    source.getStateInformation(binary);
    auto restoredProcessor = std::make_unique<Processor>();
    auto& restored = *restoredProcessor;
    restored.setStateInformation(binary.getData(), int(binary.getSize()));
    auto pastedProcessor = std::make_unique<Processor>();
    auto& pasted = *pastedProcessor;
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
    // Dirty every appended control so missing legacy values cannot pass by
    // retaining defaults left untouched by the original tone-only fixture.
    for (size_t i = 139; i < parameterIds.size(); ++i) {
        auto* parameter = restored.parameters.getParameter(parameterIds[i]);
        parameter->setValueNotifyingHost(parameter->getDefaultValue() < 0.5f ? 1.0f : 0.0f);
        require(read(restored, parameterIds[i]) != parameter->convertFrom0to1(parameter->getDefaultValue()),
                "Legacy migration fixture did not dirty parameter: " + juce::String(parameterIds[i]));
    }
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
    auto processor = std::make_unique<Processor>();
    auto& p = *processor;
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
        auto processor = std::make_unique<Processor>();
        auto& p = *processor;
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

inline void spatialEq() {
    using namespace processorRouting;
    static_assert(int(NodeType::equalizer) == 7 && int(NodeType::spatialEq) == 28);
    static_assert(spatialEqParameterStart == 2457 && spatialEqParameterCount == 720);
    auto owner = std::make_unique<Processor>();
    auto& p = *owner;
    auto g = graph(NodeType::spatialEq);
    auto& band = g.nodes[1].spatialEq.bands.back();
    band = {true, true, SpatialEqShape::Bell, 1000.f, 12.f, 1.f, .25f};
    const auto id = [](size_t b, SpatialEqField field) { return spatialEqBandParameterId(1, b, field); };
    for (size_t b = 0; b < 8; ++b) {
        const auto* depth = p.parameters.getParameter(id(b, spatialDepth));
        require(depth && depth->isAutomatable(), "Missing Spatial EQ Depth automation");
        near(read(p, id(b, spatialDepth)), 0, "Spatial EQ Depth default must be zero");
    }
    juce::String error;
    write(p, eqBandParameterId(1, 7, "Gain"), -9.f);
    require(p.setRoutingGraph(g, error), error);
    near(read(p, id(7, spatialDepth)), 25, "Spatial EQ creation did not seed Depth");
    near(read(p, eqBandParameterId(1, 7, "Gain")), -9, "Spatial EQ overwrote EQ8 parameters");
    for (size_t b = 0; b < 8; ++b) write(p, id(b, spatialDepth), 10.f * float(b + 1));
    auto cableOnly = g;
    cableOnly.links.clear();
    require(p.setRoutingGraph(cableOnly, error), error);
    require(p.setRoutingGraph(g, error), error);
    const auto settings = p.settings().fxSettings[1].spatialEq;
    for (size_t b = 0; b < settings.bands.size(); ++b)
        near(settings.bands[b].depth, .1f * float(b + 1), "Depth did not reach the correct DSP band");
    auto copy = std::make_unique<Processor>();
    require(copy->pasteSettings(p.settingsText()), "Spatial EQ state round-trip failed");
    require(copy->getRoutingGraph().nodes[1].type == NodeType::spatialEq, "Spatial EQ type not recalled");
    for (size_t b = 0; b < 8; ++b)
        near(read(*copy, id(b, spatialDepth)), 10.f * float(b + 1), "Spatial EQ state lost Depth");
    auto partial = state(p);
    partial.removeChild(partial.getChildWithProperty("id", id(7, spatialDepth)), nullptr);
    require(copy->pasteSettings(partial.toXmlString()), "Spatial EQ graph fallback failed");
    near(read(*copy, id(7, spatialDepth)), 25, "Missing Depth did not use graph-authored value");

    copy.reset();
    write(p, "levelOn", 0);
    write(p, "doubleOn", 0);
    write(p, "formantOn", 0);
    write(p, id(7, spatialDepth), 0);
    p.prepareToPlay(48000, 256);
    const auto renderSide = [&](int& position) {
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        double peak = 0;
        for (int block = 0; block < 256; ++block) {
            const int frames = 1 + (block * 53) % 256;
            buffer.setSize(2, frames, false, false, true);
            for (int i = 0; i < frames; ++i, ++position) {
                const float sample = float(.05 * std::sin(6.283185307179586 * 1000 * position / 48000));
                buffer.setSample(0, i, sample);
                buffer.setSample(1, i, sample);
            }
            p.processBlock(buffer, midi);
            for (int i = 0; i < frames; ++i) {
                const auto l = buffer.getSample(0, i), r = buffer.getSample(1, i);
                require(std::isfinite(l) && std::isfinite(r), "Spatial EQ rendered non-finite audio");
                if (block >= 216) peak = std::max(peak, double(std::abs(l - r)));
            }
        }
        return peak;
    };
    int position = 0;
    require(renderSide(position) < 1.e-7, "Depth 0 colored the stereo difference on dual mono");
    write(p, id(7, spatialDepth), 100);
    require(renderSide(position) > 1.e-5, "Automating Depth did not generate stereo from dual mono");
    require(p.getLatencySamples() == 0 && !p.routingRestartPending(), "Spatial EQ changed latency or requested reactivation");
    write(p, fxParameterId(1, "On"), 0);
    require(renderSide(position) < 1.e-7, "Settled Spatial EQ bypass leaked the reflection return");
    write(p, fxParameterId(1, "On"), 1);
    require(renderSide(position) > 1.e-5, "Spatial EQ did not resume after bypass");
    p.releaseResources();

    auto legacy = std::make_unique<Processor>();
    auto eq8 = graph(NodeType::equalizer);
    eq8.nodes[1].eq.bands[7] = {true, 1000, -9, .707f, 13};
    require(legacy->setRoutingGraph(eq8, error), error);
    auto legacyState = state(*legacy);
    for (size_t i = spatialEqParameterStart; i < parameterIds.size(); ++i)
        legacyState.removeChild(legacyState.getChildWithProperty("id", parameterIds[i]), nullptr);
    require(legacy->pasteSettings(legacyState.toXmlString()), "Legacy EQ8 state without Spatial EQ controls was rejected");
    const auto recalled = legacy->getRoutingGraph().nodes[1];
    require(recalled.type == NodeType::equalizer && recalled.eq.bands[7].mode == 13, "Recall migrated or modified EQ8");
    near(read(*legacy, eqBandParameterId(1, 7, "Gain")), -9, "Legacy EQ8 gain changed on recall");
    near(read(*legacy, id(7, spatialDepth)), 0, "Legacy state did not default Spatial EQ Depth to zero");
}

inline void spatialEqSignedAudio() {
    using namespace processorRouting;
    constexpr double rate = 48000;
    constexpr int firstCrossing = 4093, secondCrossing = 8191, totalFrames = 12289;
    constexpr std::array<float, 3> depths{0, 50, 100};
    constexpr std::array<const char*, 3> shapeNames{"Bell", "Low Shelf", "High Shelf"};
    constexpr std::array shapes{SpatialEqShape::Bell, SpatialEqShape::LowShelf, SpatialEqShape::HighShelf};
    constexpr std::array<float, 3> frequencies{900, 360, 4100};
    constexpr size_t cutSlot = 6, boostSlot = 1;
    auto owner = std::make_unique<Processor>();
    auto& p = *owner;
    juce::String error;
    require(p.setRoutingGraph(graph(NodeType::spatialEq), error), error);
    for (const auto* id : {"levelOn", "doubleOn", "formantOn", "bypass", "outputGain"}) write(p, id, 0);
    write(p, "globalMix", 100);
    const auto setBand = [&](size_t band, int shape, float hz, float gain, float q, float depth) {
        const std::array<float, 6> raw{1, float(shape), hz, gain, q, depth};
        for (size_t field = 0; field < raw.size(); ++field)
            write(p, spatialEqBandParameterId(1, band, field), raw[field]);
    };
    const auto render = [&](size_t shape, float depth, bool crossing) {
        const float hz = frequencies[shape];
        // Construct the reference from the fixture, not from the processor's mapped settings.
        SpatialEqSettings target;
        target.bands[cutSlot] = {true, true, shapes[shape], hz, -9, 1.2f, depth * .01f};
        setBand(cutSlot, int(shape), hz, -9, 1.2f, depth);
        if (crossing) {
            target.bands[boostSlot] = {true, true, SpatialEqShape::Bell, hz * 1.1f, 7, .8f, .8f};
            setBand(boostSlot, 0, hz * 1.1f, 7, .8f, 80);
        } else write(p, spatialEqBandParameterId(1, boostSlot, spatialOn), 0);
        p.prepareToPlay(rate, 256);
        require(p.getLatencySamples() == 0, "Spatial EQ parity needs a zero-latency graph");
        SpatialEq direct;
        direct.setSettings(target);
        direct.prepare(rate);
        juce::AudioBuffer<float> inOut(2, 256);
        juce::MidiBuffer midi;
        std::array<float, 256> referenceLeft{}, referenceRight{};
        std::vector<float> rendered;
        rendered.reserve(2 * totalFrames);
        float peakError = 0;
        for (int position = 0, block = 0; position < totalFrames; ++block) {
            if (crossing && (position == firstCrossing || position == secondCrossing)) {
                const float gain = position == firstCrossing ? 9.f : -9.f;
                write(p, spatialEqBandParameterId(1, cutSlot, spatialGain), gain);
                target.bands[cutSlot].gainDb = gain;
                direct.setSettings(target);
            }
            int end = totalFrames;
            if (crossing && position < secondCrossing)
                end = position < firstCrossing ? firstCrossing : secondCrossing;
            const int frames = std::min(end - position, 1 + (31 + block * 53) % 256);
            inOut.setSize(2, frames, false, false, true);
            for (int i = 0; i < frames; ++i) {
                const double t = (position + i) / rate;
                const double phase = 6.283185307179586 * hz * t;
                const auto left = float(.036 * std::sin(phase * .63) + .024 * std::cos(phase * 1.13));
                const auto right = float(.031 * std::cos(phase * .82) - .021 * std::sin(phase * 1.37));
                inOut.setSample(0, i, referenceLeft[size_t(i)] = left);
                inOut.setSample(1, i, referenceRight[size_t(i)] = right);
            }
            direct.process(referenceLeft.data(), referenceRight.data(), size_t(frames));
            p.processBlock(inOut, midi);
            for (int i = 0; i < frames; ++i) {
                const auto left = inOut.getSample(0, i), right = inOut.getSample(1, i);
                require(std::isfinite(left) && std::isfinite(right), "Signed Spatial EQ produced non-finite audio");
                peakError = std::max({peakError, std::abs(left - referenceLeft[size_t(i)]), std::abs(right - referenceRight[size_t(i)])});
                rendered.push_back(left);
                rendered.push_back(right);
            }
            position += frames;
        }
        p.releaseResources();
        require(peakError < 2.e-6f, juce::String(shapeNames[shape])
            + (crossing ? " crossing with boost" : " cut at Depth " + juce::String(depth))
            + " differs from direct SpatialEq; peak error " + juce::String(peakError, 9));
        return rendered;
    };
    const auto distance = [](const auto& a, const auto& b) {
        float peak = 0;
        for (size_t i = 0; i < a.size(); ++i) peak = std::max(peak, std::abs(a[i] - b[i]));
        return peak;
    };
    for (size_t shape = 0; shape < shapes.size(); ++shape) {
        std::array<std::vector<float>, depths.size()> cuts;
        for (size_t depth = 0; depth < depths.size(); ++depth) cuts[depth] = render(shape, depths[depth], false);
        require(distance(cuts[0], cuts[1]) > 1.e-4f && distance(cuts[1], cuts[2]) > 1.e-4f,
            juce::String(shapeNames[shape]) + " cut fixture did not distinguish 0%, 50%, and 100% Depth");
        render(shape, 100, true);
    }
}

inline void spatialEqShapesAndSlopes() {
    using namespace processorRouting;
    constexpr double rate = 48000.;
    constexpr std::array shapes{SpatialEqShape::LowCut, SpatialEqShape::HighCut, SpatialEqShape::Notch, SpatialEqShape::BandPass};
    auto owner = std::make_unique<Processor>();
    auto& p = *owner;
    const auto shapeId = spatialEqBandParameterId(1, 0, spatialShape);
    const auto slopeId = spatialEqSlopeParameterId(1, 0);
    auto* shapeParam = p.parameters.getParameter(shapeId);
    auto* slopeParam = p.parameters.getParameter(slopeId);
    require(shapeParam && slopeParam && slopeParam == p.getParameters()[3177], "Slope was not appended after the existing bank");
    near(read(p, slopeId), 3, "Slope default is not 24 dB/oct");
    for (int i = 0; i < 3; ++i)
        near(shapeParam->convertFrom0to1(.5f * float(i)), float(i), "Released normalized shape mapping changed");
    for (int i = 0; i < 7; ++i)
        near(shapeParam->convertFrom0to1(shapeParam->convertTo0to1(float(i))), float(i), "Shape automation cannot reach the ordinal");

    auto graphModel = graph(NodeType::spatialEq);
    auto& band = graphModel.nodes[1].spatialEq.bands[0];
    band = {true, true, SpatialEqShape::LowCut, 900, 15, .81f, .5f, 48};
    juce::String error;
    require(p.setRoutingGraph(graphModel, error), error);
    near(read(p, slopeId), 5, "Graph did not seed the slope parameter");
    auto tree = state(p);
    tree.removeChild(tree.getChildWithProperty("id", slopeId), nullptr);
    require(p.pasteSettings(tree.toXmlString()), "Graph slope fallback failed to load");
    near(read(p, slopeId), 5, "Missing host slope did not fall back to graph-authored slope");
    auto legacyTree = tree.createCopy();
    for (const auto* graphKey : {"RoutingGraph", "ActiveRoutingGraph"}) {
        auto graphTree = legacyTree.getChildWithName(graphKey);
        for (const auto& node : graphTree)
            if (auto spatial = node.getChildWithName("SpatialEq"); spatial.getNumChildren() > 0)
                spatial.getChild(0).removeProperty("Slope", nullptr);
    }
    require(p.pasteSettings(legacyTree.toXmlString()), "Old Spatial EQ state without slope failed to load");
    near(read(p, slopeId), 3, "Old Spatial EQ state did not default to 24 dB/oct");
    for (size_t i = 0; i < spatialEqSlopes.size(); ++i) {
        write(p, slopeId, float(i));
        require(p.settings().fxSettings[1].spatialEq.bands[0].slopeDbPerOctave == spatialEqSlopes[i], "Slope choice did not map to dB/oct");
    }
    auto copy = std::make_unique<Processor>();
    require(copy->pasteSettings(p.settingsText()), "Automated slope failed state recall");
    near(read(*copy, slopeId), 7, "Saved host slope lost to graph default");
    copy.reset();

    for (const auto* id : {"levelOn", "doubleOn", "formantOn", "bypass", "outputGain"}) write(p, id, 0);
    write(p, "globalMix", 100);
    for (size_t shape = 0; shape < shapes.size(); ++shape) {
        band.shape = shapes[shape];
        write(p, shapeId, float(shape + 3));
        write(p, slopeId, float(spatialEqSlopeIndex(float(band.slopeDbPerOctave))));
        p.prepareToPlay(rate, 137);
        SpatialEqSettings expected = graphModel.nodes[1].spatialEq;
        expected.bands[0].gainDb = 0; // The four filter-only shapes must ignore the stored gain.
        SpatialEq reference;
        reference.setSettings(expected);
        reference.prepare(rate);
        auto routing = std::make_unique<RoutingExecutor>();
        routing->prepare(rate);
        require(bool(routing->requestGraph(graphModel)), "Standalone graph rejected the new shape");
        juce::AudioBuffer<float> audio(2, 137);
        juce::MidiBuffer midi;
        float peakError = 0;
        for (int block = 0; block < 24; ++block) {
            if (block == 12 && shape < 2) {
                band.slopeDbPerOctave = 6;
                expected.bands[0].slopeDbPerOctave = 6;
                reference.setSettings(expected);
                write(p, slopeId, 0);
            }
            std::array<StereoSample, 137> oracle{};
            for (int i = 0; i < audio.getNumSamples(); ++i) {
                const auto sample = double(block * audio.getNumSamples() + i);
                const StereoSample input{float(.05 * std::sin(sample * .07)), float(.04 * std::cos(sample * .18))};
                audio.setSample(0, i, input.left);
                audio.setSample(1, i, input.right);
                oracle[size_t(i)] = input;
                reference.process(oracle[size_t(i)].left, oracle[size_t(i)].right);
                if (block < 12) {
                    RoutingSources sources{};
                    sources.lead = input;
                    const auto actual = routing->process(sources, RoutingControls{});
                    require(std::abs(actual.left - oracle[size_t(i)].left) < 2.e-6f
                        && std::abs(actual.right - oracle[size_t(i)].right) < 2.e-6f, "Node-authored graph render differs from direct DSP");
                }
            }
            p.processBlock(audio, midi);
            for (int i = 0; i < audio.getNumSamples(); ++i) {
                const float l = audio.getSample(0, i), r = audio.getSample(1, i);
                require(std::isfinite(l) && std::isfinite(r), "Spatial EQ filter output is not finite");
                peakError = std::max({peakError, std::abs(l - oracle[size_t(i)].left), std::abs(r - oracle[size_t(i)].right)});
            }
        }
        require(peakError < 2.e-6f, "HEAVEN filter or live slope differs from direct SpatialEq: " + juce::String(peakError, 8));
        require(p.getLatencySamples() == 0 && !p.routingRestartPending(), "Filter/slope added latency or requested reactivation");
        p.releaseResources();
        band.slopeDbPerOctave = 48;
    }
}

inline void spatialEqInternalDynamics() {
    using namespace processorRouting;
    constexpr size_t slot = 1, bandIndex = 7;
    const auto id = [](size_t field) { return spatialEqDynamicParameterId(slot, bandIndex, field); };
    auto owner = std::make_unique<Processor>();
    auto& p = *owner;
    static_assert(spatialEqDynamicParameterStart == 3297 && spatialEqDynamicParameterCount == 600);
    constexpr std::array defaults{0.f, -24.f, 4.f, 15.f, 150.f};
    constexpr std::array lower{0.f, -72.f, 0.f, .1f, 10.f}, upper{2.f, 0.f, 18.f, 200.f, 2000.f};
    for (size_t f = 0; f < defaults.size(); ++f) {
        auto* parameter = p.parameters.getParameter(id(f));
        require(parameter && parameter->isAutomatable() && parameter == p.getParameters()[int(3297 + bandIndex * 5 + f)],
            "Dynamics bank index or automation changed");
        near(read(p, id(f)), defaults[f], "Wrong dynamics default");
        near(parameter->convertFrom0to1(0), lower[f], "Wrong dynamics minimum");
        near(parameter->convertFrom0to1(1), upper[f], "Wrong dynamics maximum");
    }

    auto model = graph(NodeType::spatialEq);
    auto& authored = model.nodes[1].spatialEq.bands[bandIndex];
    authored = {true, true, SpatialEqShape::Bell, 900, 3, .9f, .35f, 24,
        SpatialEqDynamicMode::Upward, -40, 10, 5, 80};
    juce::String error;
    require(p.setRoutingGraph(model, error), error);
    near(read(p, id(dynamicThreshold)), -40, "New graph did not seed dynamics");
    write(p, id(dynamicThreshold), -37);
    auto copy = std::make_unique<Processor>();
    require(copy->pasteSettings(p.settingsText()), "Dynamics failed state recall");
    near(read(*copy, id(dynamicThreshold)), -37, "Host-authored dynamics lost at recall");
    auto partial = state(p);
    for (size_t f = 0; f < defaults.size(); ++f) partial.removeChild(partial.getChildWithProperty("id", id(f)), nullptr);
    require(copy->pasteSettings(partial.toXmlString()), "Graph-authored dynamics failed state recall");
    near(read(*copy, id(dynamicThreshold)), -40, "Graph-authored dynamics lost at recall");
    for (const auto* key : {"RoutingGraph", "ActiveRoutingGraph"})
        for (const auto& node : partial.getChildWithName(key))
            if (auto bands = node.getChildWithName("SpatialEq"); bands.getNumChildren() > int(bandIndex))
                for (const auto& d : spatialEqDynamicFields) bands.getChild(int(bandIndex)).removeProperty(d.id, nullptr);
    require(copy->pasteSettings(partial.toXmlString()), "Old graph without dynamics failed state recall");
    for (size_t f = 0; f < defaults.size(); ++f) near(read(*copy, id(f)), defaults[f], "Old graph inherited non-default dynamics");
    copy.reset();

    for (const auto* key : {"levelOn", "doubleOn", "formantOn", "bypass", "outputGain"}) write(p, key, 0);
    write(p, "globalMix", 100);
    constexpr double rate = 48000;
    constexpr int frames = 137, blocks = 100;
    const auto render = [&](SpatialEqShape shape, SpatialEqDynamicMode mode) {
        write(p, spatialEqBandParameterId(slot, bandIndex, spatialShape), float(shape));
        const std::array<float, 5> values{float(mode), -40, 10, 5, 80};
        for (size_t f = 0; f < values.size(); ++f) write(p, id(f), values[f]);
        p.prepareToPlay(rate, frames);
        SpatialEqSettings expected = model.nodes[1].spatialEq;
        expected.bands[bandIndex].shape = shape;
        expected.bands[bandIndex].dynamicMode = mode;
        SpatialEq direct;
        direct.setSettings(expected);
        direct.prepare(rate);
        juce::AudioBuffer<float> audio(2, frames);
        juce::MidiBuffer midi;
        std::vector<float> rendered;
        rendered.reserve(2 * frames * blocks);
        float errorPeak = 0;
        for (int block = 0; block < blocks; ++block) {
            if (block == 53) {
                const std::array<float, 4> next{-30, 7, 13, 42};
                for (size_t f = 0; f < next.size(); ++f) write(p, id(f + 1), next[f]);
                auto& b = expected.bands[bandIndex];
                b.thresholdDb = -30; b.rangeDb = 7; b.attackMs = 13; b.releaseMs = 42;
                direct.setSettings(expected);
            }
            std::array<float, frames> left{}, right{};
            for (int i = 0; i < frames; ++i) {
                const double sample = block * frames + i;
                const double amplitude = block >= 12 && block < 76 ? .19 : .002;
                left[size_t(i)] = float(amplitude * std::sin(sample * .118));
                right[size_t(i)] = float(.75 * amplitude * std::cos(sample * .132));
                audio.setSample(0, i, left[size_t(i)]);
                audio.setSample(1, i, right[size_t(i)]);
            }
            direct.process(left.data(), right.data(), frames);
            p.processBlock(audio, midi);
            for (int i = 0; i < frames; ++i) {
                const auto l = audio.getSample(0, i), r = audio.getSample(1, i);
                require(std::isfinite(l) && std::isfinite(r), "Non-finite dynamics output");
                errorPeak = std::max({errorPeak, std::abs(l - left[size_t(i)]), std::abs(r - right[size_t(i)])});
                rendered.push_back(l);
                rendered.push_back(r);
            }
        }
        require(errorPeak < 2.e-6f, "HEAVEN internal dynamics differ from direct DSP: " + juce::String(errorPeak, 8));
        require(p.getLatencySamples() == 0 && !p.routingRestartPending(), "Dynamics changed compensation");
        p.releaseResources();
        return rendered;
    };
    for (const auto shape : {SpatialEqShape::Bell, SpatialEqShape::LowShelf, SpatialEqShape::HighShelf, SpatialEqShape::Notch}) {
        const auto off = render(shape, SpatialEqDynamicMode::Off);
        for (const auto mode : {SpatialEqDynamicMode::Downward, SpatialEqDynamicMode::Upward}) {
            const auto on = render(shape, mode);
            float delta = 0;
            for (size_t i = 0; i < on.size(); ++i) delta = std::max(delta, std::abs(on[i] - off[i]));
            require(shape == SpatialEqShape::Notch ? delta < 2.e-6f : delta > 1.e-4f,
                "Eligible dynamics did not engage or inactive Notch dynamics leaked");
        }
    }
}

inline void run() {
    parameterContract();
    stateAndLegacyMigration();
    modelAndCreation();
    audioBridge();
}

} // namespace vocalstack::toneProcessorTests
