#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MeterPanel.h"
#include "DoublerPanel.h"
#include "RoutingPanel.h"
#include "ProcessorRoutingState.h"
#include "ToneProcessorTests.h"
#include "ToneUiTests.h"
#include "PresetTests.h"
#include "CreativeIntegrationTests.h"
#include "UtilityIntegrationTests.h"
#include "UpdateUiTests.h"
#include "SliderRenderingTests.h"
#include "MainBusFxRackPanel.h"
#include "HoverWheelRegressionTests.h"
#include "ToneCardLayoutTests.h"
#include "UtilityPanelTests.h"
#include "CreativePanelLayoutTests.h"
#include "FluidBackgroundTests.h"
#include "RackUndoIntegrationTests.h"
#include "FactoryFxUiTests.h"
#include "CoordinatedPreviewTests.h"
#include "ControlLayoutUpdateTests.h"
#include "CompressorHistoryTests.h"
#include "UtilityVisualUpdateTests.h"
#include "InspectorControlLayoutTests.h"
#include "ScrollbarTests.h"
#include "MeasuredFieldUiTests.h"
#include "ReactiveMaterialTests.h"
#include "SpectralMorphUiTests.h"
#include "NodeGalleryTests.h"
#include "TypographyMotionTests.h"
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using vocalstack::MeterPanel;
using vocalstack::Processor;

void expect(bool result, const juce::String& reason) {
    if (!result)
        throw std::runtime_error(reason.toStdString());
}

void expectNear(double actual, double expected, const juce::String& reason) {
    expect(std::isfinite(actual) && std::abs(actual - expected) < 0.0001,
           reason + ": expected " + juce::String(expected, 4) + ", got " + juce::String(actual, 4));
}

juce::RangedAudioParameter& parameter(Processor& owner, const juce::String& id) {
    auto* result = owner.parameters.getParameter(id);
    expect(result != nullptr, "Missing parameter: " + id);
    return *result;
}

void setParameter(Processor& owner, const juce::String& id, float value) {
    auto& target = parameter(owner, id);
    target.beginChangeGesture();
    target.setValueNotifyingHost(target.convertTo0to1(value));
    target.endChangeGesture();
}

float parameterValue(Processor& owner, const juce::String& id) {
    const auto* value = owner.parameters.getRawParameterValue(id);
    expect(value != nullptr, "Missing raw parameter: " + id);
    return value->load();
}

void advancedParameterContract() {
    std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
    Processor& owner = *ownerStorage;
    const std::array<const char*, 10> originalNames{"Leveler", "Mix", "Human", "Width", "Formant",
        "Leveler Active", "Doubler Active", "Formant Active", "Bypass", "Output"};
    expect(owner.getParameters().size() == 3177, "Expected 2457 existing controls and 720 appended Spatial EQ controls");
    for (size_t slot = 1; slot < 16; ++slot) {
        for (size_t field = 0; field < vocalstack::creativeParameterCount; ++field) {
            const auto expectedIndex = field < 48 ? 1007 + (slot - 1) * 48 + field
                : field < 51 ? 1727 + (slot - 1) * 3 + field - 48
                : field < 62 ? 2087 + (slot - 1) * 11 + field - 51
                             : 2357 + (slot - 1) * 6 + field - 62;
            expect(vocalstack::creativeParameterIndex(slot, field) == expectedIndex,
                   "Creative append-only mapping changed");
            expect(parameter(owner, vocalstack::creativeParameterId(slot, field)).getParameterIndex() == int(expectedIndex),
                   "Creative host parameter index changed");
        }
        for (size_t field = 0; field < vocalstack::utilityParameterCount; ++field)
            expect(parameter(owner, vocalstack::utilityParameterId(slot, field)).getParameterIndex() == int(1772 + (slot - 1) * 21 + field),
                   "Utility host parameter index changed");
    }
        for (size_t slot = 1; slot < 16; ++slot)
            expect(parameter(owner, vocalstack::toneParameterId(slot, "WaveQuality")).getParameterIndex() == 914 + int(slot - 1),
                   "WaveQuality append-only host index changed");
    constexpr std::array<const char*, 46> originalIds{
        "level", "double", "human", "width", "formant", "levelOn", "doubleOn", "formantOn", "bypass", "outputGain",
        "advancedOn",
        "voice1On", "voice1Gain", "voice1Pan", "voice1Delay", "voice1Detune", "voice1Formant", "voice1Depth", "voice1Rate",
        "voice2On", "voice2Gain", "voice2Pan", "voice2Delay", "voice2Detune", "voice2Formant", "voice2Depth", "voice2Rate",
        "voice3On", "voice3Gain", "voice3Pan", "voice3Delay", "voice3Detune", "voice3Formant", "voice3Depth", "voice3Rate",
        "voice4On", "voice4Gain", "voice4Pan", "voice4Delay", "voice4Detune", "voice4Formant", "voice4Depth", "voice4Rate",
        "choraleOn", "choraleMix", "choraleMotion"};
    for (size_t i = 0; i < originalIds.size(); ++i)
        expect(owner.getParameters()[static_cast<int>(i)] == &parameter(owner, originalIds[i]),
               "Original 46 parameter ID/index changed: " + juce::String(originalIds[i]));
    for (size_t i = 0; i < vocalstack::parameterIds.size(); ++i) {
        auto& p = parameter(owner, vocalstack::parameterIds[i]);
        expect(owner.getParameters()[static_cast<int>(i)] == &p, "Parameter index changed: " + p.paramID);
        if (i < originalNames.size())
            expect(p.getName(64) == originalNames[i], "Original host name changed");
        expect(p.isAutomatable(), "Parameter is not automatable: " + p.paramID);
    }
    expect(!owner.settings().advanced && !owner.settings().choraleOn, "New modules must default off");
    expectNear(owner.settings().choraleMix, 0.25, "Default Chorale Mix");
    expectNear(owner.settings().choraleMotion, 0.35, "Default Chorale Motion");
    const std::array<float, 7> low{-60, -1, 0, -100, -24, 0, 0};
    const std::array<float, 7> high{12, 1, 100, 100, 24, 2, 8};
    for (size_t v = 0; v < 8; ++v) {
        for (size_t f = 0; f < low.size(); ++f) {
            auto& p = parameter(owner, vocalstack::voiceParameterId(v, f + 1));
            expectNear(p.getNormalisableRange().start, low[f], "Voice field minimum");
            expectNear(p.getNormalisableRange().end, high[f], "Voice field maximum");
            p.setValueNotifyingHost(1.0f);
        }
        const auto voice = owner.settings().voiceSettings[v];
        const std::array<float, 7> fields{voice.gainDb, voice.pan, voice.delayMs, voice.detuneCents,
            voice.formantSemitones, voice.depth, voice.rateHz};
        for (size_t f = 0; f < high.size(); ++f)
            expectNear(fields[f], high[f], "Voice Settings bridge maximum");
    }
    setParameter(owner, "double", 0);
    expectNear(owner.settings().doubles, 0, "Lead-only Mix mapping");
    setParameter(owner, "double", 100);
    expectNear(owner.settings().doubles, 1, "Doubles-only Mix mapping");
    setParameter(owner, "formantOn", 0);
    setParameter(owner, "voice1Formant", 3.5f);
    expectNear(owner.settings().voiceSettings[0].formantSemitones, 3.5, "Global formant switch erased voice offset");
    expect(!owner.settings().formantOn, "Voice offset changed global formant switch");
    setParameter(owner, "choraleOn", 1);
    setParameter(owner, "choraleMix", 100);
    setParameter(owner, "choraleMotion", 0);
    expect(owner.settings().choraleOn, "Chorale activation mapping");
    expectNear(owner.settings().choraleMix, 1, "Chorale Mix endpoint");
    expectNear(owner.settings().choraleMotion, 0, "Chorale Motion endpoint");
}

void expectBasicVoices(const Processor& owner, float human) {
    const auto expected = vocalstack::basicVoiceSettings(human);
    const auto actual = owner.settings().voiceSettings;
    for (size_t v = 0; v < actual.size(); ++v) {
        expect(actual[v].enabled == expected[v].enabled, "Basic voice enable mismatch");
        expect(actual[v].present == (v < 2), "Basic reset must retain exactly two present voices");
        expectNear(actual[v].pitchSemitones, 0, "Basic reset must clear musical pitch");
        const std::array<float, 7> a{actual[v].gainDb, actual[v].pan, actual[v].delayMs, actual[v].detuneCents,
            actual[v].formantSemitones, actual[v].depth, actual[v].rateHz};
        const std::array<float, 7> b{expected[v].gainDb, expected[v].pan, expected[v].delayMs, expected[v].detuneCents,
            expected[v].formantSemitones, expected[v].depth, expected[v].rateHz};
        for (size_t f = 0; f < a.size(); ++f)
            expectNear(a[f], b[f], "Basic voice field mismatch");
    }
}

void advancedActivationAndReset() {
    std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
    Processor& owner = *ownerStorage;
    setParameter(owner, "human", 80);
    owner.setAdvancedEnabled(true);
    expect(owner.settings().advanced, "UI activation failed");
    expectBasicVoices(owner, 0.8f);
    setParameter(owner, "voice1Delay", 67.5f);
    setParameter(owner, "voice3Present", 1);
    setParameter(owner, "voice3On", 1);
    owner.setAdvancedEnabled(false);
    setParameter(owner, "human", 10);
    owner.setAdvancedEnabled(true);
    expectNear(owner.settings().voiceSettings[0].delayMs, 67.5, "UI reactivation lost custom settings");
    expect(owner.settings().voiceSettings[2].enabled, "UI reactivation lost extra voice");
    for (size_t i = 0; i < 10; ++i) {
        if (i == 2) continue;
        auto& p = parameter(owner, vocalstack::parameterIds[i]);
        p.setValueNotifyingHost(0.7f);
    }
    setParameter(owner, "choraleOn", 1);
    setParameter(owner, "choraleMix", 73);
    setParameter(owner, "choraleMotion", 62);
    std::array<float, 10> unrelated{};
    for (size_t i = 0; i < unrelated.size(); ++i)
        unrelated[i] = parameterValue(owner, vocalstack::parameterIds[i]);
    owner.resetVoicesToBasic();
    expectBasicVoices(owner, 0.1f);
    expect(owner.settings().advanced, "Reset to Basic disabled Advanced");
    for (size_t i = 0; i < unrelated.size(); ++i)
        expectNear(parameterValue(owner, vocalstack::parameterIds[i]), unrelated[i], "Voice reset changed unrelated parameter");
    expectNear(owner.settings().choraleMix, 0.73, "Voice reset changed Chorale Mix");
    expectNear(owner.settings().choraleMotion, 0.62, "Voice reset changed Chorale Motion");
    expect(owner.settings().choraleOn, "Voice reset disabled Chorale");

    std::unique_ptr<Processor> automatedStorage = std::make_unique<Processor>();

    Processor& automated = *automatedStorage;
    setParameter(automated, "human", 90);
    setParameter(automated, "advancedOn", 1);
    expectBasicVoices(automated, 0.35f); // Host activation uses safe stored baseline, not message-thread rewrites.
    setParameter(automated, "advancedOn", 0);
    automated.setAdvancedEnabled(true);
    expectBasicVoices(automated, 0.35f);
}

void advancedStateRoundTrips() {
    std::unique_ptr<Processor> sourceStorage = std::make_unique<Processor>();
    Processor& source = *sourceStorage;
    source.setAdvancedEnabled(true);
    source.setAdvancedVisible(true);
    source.setFlash(true);
    for (size_t i = 11; i < vocalstack::parameterIds.size(); ++i)
        parameter(source, vocalstack::parameterIds[i]).setValueNotifyingHost(i % 2 == 0 ? 0.0f : 1.0f);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    std::unique_ptr<Processor> restoredStorage = std::make_unique<Processor>();
    Processor& restored = *restoredStorage;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    for (size_t i = 10; i < vocalstack::parameterIds.size(); ++i)
        expectNear(parameterValue(restored, vocalstack::parameterIds[i]), parameterValue(source, vocalstack::parameterIds[i]),
                   "Appended parameter did not roundtrip");
    expect(restored.isFlash() && restored.isAdvancedVisible(), "Editor properties did not roundtrip");
    restored.setAdvancedEnabled(false);
    setParameter(restored, "human", 100);
    restored.setAdvancedEnabled(true);
    expectNear(parameterValue(restored, "voice1Gain"), -60, "Restored activation overwrote custom voice");
    std::unique_ptr<Processor> pastedStorage = std::make_unique<Processor>();
    Processor& pasted = *pastedStorage;
    expect(pasted.pasteSettings(source.settingsText()), "Appended settings paste failed");
    for (size_t i = 10; i < vocalstack::parameterIds.size(); ++i)
        expectNear(parameterValue(pasted, vocalstack::parameterIds[i]), parameterValue(source, vocalstack::parameterIds[i]),
                   "Appended parameter did not paste");
    source.resetParameters();
    for (const auto* id : vocalstack::parameterIds) {
        const auto& p = parameter(source, id);
        expectNear(parameterValue(source, id), p.convertFrom0to1(p.getDefaultValue()), "Full reset missed parameter");
    }
    setParameter(source, "human", 60);
    source.setAdvancedEnabled(true);
    expectBasicVoices(source, 0.6f);

    std::unique_ptr<Processor> partialStorage = std::make_unique<Processor>();

    Processor& partial = *partialStorage;
    setParameter(partial, "level", 99);
    setParameter(partial, "bypass", 1);
    expect(partial.pasteSettings("<VocalStack><PARAM id=\"voice3Formant\" value=\"-4\"/></VocalStack>"),
           "Partial voice settings were rejected");
    expectNear(parameterValue(partial, "level"), 50, "Partial state inherited stale Leveler");
    expectNear(parameterValue(partial, "bypass"), 0, "Partial state inherited stale bypass");
    partial.setAdvancedEnabled(true);
    expectNear(parameterValue(partial, "voice3Formant"), -4, "First activation discarded restored dormant voice edit");
}

void outputParameterContract() {
    std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
    Processor& owner = *ownerStorage;
    auto& output = parameter(owner, "outputGain");
    expect(output.paramID == "outputGain", "Output parameter ID changed");
    expect(output.getName(64) == "Output", "Output parameter must be named Output");
    expectNear(output.getNormalisableRange().start, -24.0, "Output minimum dB");
    expectNear(output.getNormalisableRange().end, 12.0, "Output maximum dB");
    expectNear(output.convertFrom0to1(output.getDefaultValue()), 0.0, "Output default dB");
    expectNear(parameterValue(owner, "outputGain"), 0.0, "New instance output dB");
}

void outputStateRoundTrips() {
    std::unique_ptr<Processor> sourceStorage = std::make_unique<Processor>();
    Processor& source = *sourceStorage;
    setParameter(source, "outputGain", -9.5f);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    expect(saved.getSize() > 0, "Saved processor state is empty");

    std::unique_ptr<Processor> restoredStorage = std::make_unique<Processor>();

    Processor& restored = *restoredStorage;
    setParameter(restored, "outputGain", 6.0f);
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    expectNear(parameterValue(restored, "outputGain"), -9.5, "Saved Output value was not restored");

    const auto copied = source.settingsText();
    expect(copied.isNotEmpty(), "Copied settings are empty");
    setParameter(restored, "outputGain", 3.0f);
    expect(restored.pasteSettings(copied), "Copied settings were rejected");
    expectNear(parameterValue(restored, "outputGain"), -9.5, "Pasted Output value was not restored");

    setParameter(restored, "outputGain", 12.0f);
    restored.resetParameters();
    expectNear(parameterValue(restored, "outputGain"), 0.0, "Reset did not restore unity Output");
    expectNear(parameterValue(source, "outputGain"), -9.5, "Restoring another instance changed the source");
}

void legacyStateDefaults() {
    std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
    Processor& owner = *ownerStorage;
    setParameter(owner, "level", 23.0f);
    setParameter(owner, "outputGain", -12.0f);
    auto xml = juce::parseXML(owner.settingsText());
    expect(xml != nullptr, "Settings are not valid XML");

    int removed = 0;
    for (auto* child = xml->getFirstChildElement(); child != nullptr;) {
        auto* next = child->getNextElement();
        bool appended = false;
        for (size_t i = 9; i < vocalstack::parameterIds.size(); ++i)
            appended = appended || child->getStringAttribute("id") == vocalstack::parameterIds[i];
        if (child->hasTagName("PARAM") && appended) {
            xml->removeChildElement(child, true);
            ++removed;
        }
        child = next;
    }
    expect(removed == int(vocalstack::parameterIds.size() - 9), "Expected Output plus all appended PARAM children");
    for (const auto* type : {"RoutingGraph", "ActiveRoutingGraph", "RoutingLayout"})
        if (auto* graph = xml->getChildByName(type)) xml->removeChildElement(graph, true);
    xml->removeAttribute("advancedVisible");
    xml->removeAttribute("advancedInitialized");
    for (size_t i = 10; i < vocalstack::parameterIds.size(); ++i)
        parameter(owner, vocalstack::parameterIds[i]).setValueNotifyingHost(0.93f);
    owner.setAdvancedVisible(true);

    setParameter(owner, "level", 87.0f);
    setParameter(owner, "outputGain", 6.0f);
    expectNear(parameterValue(owner, "outputGain"), 6.0, "Legacy-state test precondition");
    expect(owner.pasteSettings(xml->toString()), "Legacy settings without Output were rejected");
    expectNear(parameterValue(owner, "outputGain"), 0.0, "Legacy settings retained the previous Output gain");
    expectNear(parameterValue(owner, "level"), 23.0, "Legacy settings did not restore an existing parameter");
    for (size_t i = 10; i < vocalstack::parameterIds.size(); ++i) {
        const auto* id = vocalstack::parameterIds[i];
        const auto& p = parameter(owner, id);
        expectNear(parameterValue(owner, id), p.convertFrom0to1(p.getDefaultValue()), "Legacy state inherited appended parameter");
    }
    expect(!owner.isAdvancedVisible(), "Legacy state retained drawer visibility");
    owner.setAdvancedEnabled(true);
    expectBasicVoices(owner, 0.35f);
}

void bypassBinding() {
    std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
    Processor& owner = *ownerStorage;
    auto& bypass = parameter(owner, "bypass");
    auto& output = parameter(owner, "outputGain");
    expect(owner.getBypassParameter() == &bypass, "Host bypass does not point to the named bypass parameter");
    expect(owner.getBypassParameter() != &output, "Appending Output changed the host bypass parameter");
    setParameter(owner, "bypass", 1.0f);
    expectNear(owner.getBypassParameter()->getValue(), 1.0, "Host bypass did not follow the bypass parameter");
    expectNear(parameterValue(owner, "outputGain"), 0.0, "Changing bypass changed Output");
}

void meterPublicationAndReset() {
    std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
    Processor& owner = *ownerStorage;
    setParameter(owner, "level", 0.0f);
    setParameter(owner, "double", 0.0f);
    setParameter(owner, "outputGain", 6.0f);
    owner.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    const int settleBlocks = (owner.getLatencySamples() + 255) / 256 + 20;
    for (int block = 0; block < settleBlocks; ++block) {
        for (int i = 0; i < audio.getNumSamples(); ++i) {
            audio.setSample(0, i, 1.25f);
            audio.setSample(1, i, 0.25f);
        }
        owner.processBlock(audio, midi);
    }
    const auto before = owner.getMeters();
    expect(before.inputChannels == 2 && before.outputChannels == 2, "Meter bridge lost channel counts");
    expect(before.input[0].clipped && before.output[0].clipped &&
           !before.input[1].clipped && !before.output[1].clipped, "Meter bridge lost per-channel clip latches");
    expectNear(before.input[0].maxPeak, 1.25, "Published input maximum");
    expect(std::abs(before.output[0].maxPeak - 1.25f * juce::Decibels::decibelsToGain(6.0f)) < 0.001f,
           "Published output maximum is not post-trim");
    for (int block = 0; block < settleBlocks; ++block) {
        audio.clear();
        owner.processBlock(audio, midi);
    }
    owner.requestMeterReset();
    expect(owner.getMeters().output[0].clipped, "UI request mutated audio-thread meter state directly");
    audio.clear();
    owner.processBlock(audio, midi);
    const auto cleared = owner.getMeters();
    expect(!cleared.input[0].clipped && !cleared.output[0].clipped &&
           cleared.input[0].maxPeak == 0 && cleared.output[0].maxPeak == 0,
           "Audio thread did not apply the meter clear request");
    expect(cleared.input[0].rms > 0 && cleared.output[0].rms > 0, "Peak clear erased live RMS history");
    owner.reset();
    expect(owner.getMeters().input[0].rms == 0 && owner.getMeters().output[0].rms == 0,
           "Host reset did not publish cleared meters");
}

vocalstack::Meters syntheticMeters() {
    vocalstack::Meters snapshot{};
    snapshot.inputChannels = 1;
    snapshot.outputChannels = 2;
    const auto reading = [](float rmsDb, float peakDb, float heldDb, float maxDb, bool clipped) {
        vocalstack::MeterReading result;
        result.rms = juce::Decibels::decibelsToGain(rmsDb);
        result.peak = juce::Decibels::decibelsToGain(peakDb);
        result.heldPeak = juce::Decibels::decibelsToGain(heldDb);
        result.maxPeak = juce::Decibels::decibelsToGain(maxDb);
        result.clipped = clipped;
        return result;
    };
    snapshot.input[0] = reading(-24.0f, -10.0f, -6.0f, -3.0f, false);
    // Inactive input data must not make a mono input look stereo or clipped.
    snapshot.input[1] = reading(-18.0f, -8.0f, -4.0f, 0.6f, true);
    snapshot.output[0] = reading(-18.0f, -3.0f, 0.0f, 1.2f, true);
    snapshot.output[1] = reading(-30.0f, -12.0f, -7.0f, -3.0f, false);
    snapshot.correctionDb = -3.5f;
    return snapshot;
}

void meterTransportKeys() {
    MeterPanel panel;
    panel.setSize(244, 246);
    panel.addToDesktop(0); // Hidden native peer: exercise JUCE key routing without taking focus.
    auto* peer = panel.getPeer();
    expect(peer != nullptr, "Meter transport test could not create a native peer");
    int resets = 0;
    panel.onReset = [&resets] { ++resets; };
    for (const bool neon : {false, true}) {
        panel.setNeon(neon);
        panel.setMeters(syntheticMeters());
        const auto before = resets;
        expect(!peer->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey)),
               "Meter consumed the host's Space transport shortcut");
        expect(!peer->handleKeyPress(juce::KeyPress(juce::KeyPress::returnKey)),
               "Meter consumed the host's Return shortcut");
        expect(resets == before, "A host shortcut cleared meter history");
        expect(!panel.getWantsKeyboardFocus() && !panel.getMouseClickGrabsKeyboardFocus(),
               "Clicking the meter can steal keyboard focus");
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent click{juce::Desktop::getInstance().getMainMouseSource(), {10, 10},
            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
            &panel, &panel, now, {10, 10}, now, 1, false};
        panel.mouseDown(click);
        expect(resets == before + 1, "Clicking the meter did not clear peaks");
    }
}

void expectPassiveControls(juce::Component& component) {
    if (dynamic_cast<juce::TooltipWindow*>(&component) != nullptr ||
        dynamic_cast<juce::TextEditor*>(&component) != nullptr)
        return;
    expect(!component.getWantsKeyboardFocus() && !component.getMouseClickGrabsKeyboardFocus(),
           "An idle plugin control can steal host keyboard focus: "
               + vocalstack::toneUiTests::componentPath(component)
               + " [wants=" + juce::String(int(component.getWantsKeyboardFocus()))
               + ", mouse=" + juce::String(int(component.getMouseClickGrabsKeyboardFocus())) + "]");
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        expectPassiveControls(*component.getChildComponent(i));
}

void editorTransportAndTextEntryForSkin(int theme) {
    std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
    Processor& owner = *ownerStorage;
    owner.setSkin(theme);
    owner.setAdvancedVisible(true);
    std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
    editor->addToDesktop(0); // Keep the native peer hidden; do not activate a test window.
    auto* peer = editor->getPeer();
    expect(peer != nullptr, "Editor transport test could not create a native peer");
    const auto expectTransport = [&] {
        expectPassiveControls(*editor);
        expect(!peer->handleKeyPress(juce::KeyPress(juce::KeyPress::spaceKey)),
               "Editor consumed Space outside text entry");
        expect(!peer->handleKeyPress(juce::KeyPress(juce::KeyPress::returnKey)),
               "Editor consumed Return outside text entry");
    };
    expectTransport();
    auto* output = vocalstack::toneUiTests::find<juce::Slider>(*editor, [](auto& slider) {
        return slider.getComponentID() == "outputGain";
    });
    expect(output != nullptr && output->isTextBoxEditable(), "Output numeric entry was disabled");
    const auto enter = [&](const juce::String& value) {
        output->showTextBox();
        juce::TextEditor* entry = nullptr;
        for (int i = 0; i < output->getNumChildComponents(); ++i)
            if (auto* label = dynamic_cast<juce::Label*>(output->getChildComponent(i)))
                if (auto* candidate = label->getCurrentTextEditor())
                    entry = candidate;
        expect(entry != nullptr && entry->getWantsKeyboardFocus(), "Numeric entry cannot receive typing");
        entry->setText(value, false);
    };
    enter("-6.5");
    output->hideTextBox(false);
    expectNear(parameterValue(owner, "outputGain"), -6.5, "Numeric entry did not commit");
    expectTransport();
    enter("-12");
    output->hideTextBox(true);
    expectNear(parameterValue(owner, "outputGain"), -6.5, "Cancelling numeric entry changed the parameter");
    expectTransport();
}

void editorTransportAndTextEntry() {
    for (int theme = 0; theme < 3; ++theme) editorTransportAndTextEntryForSkin(theme);
}

juce::Button* findButton(juce::Component& parent, const juce::String& text) {
    for (auto* child : parent.getChildren()) {
        if (auto* button = dynamic_cast<juce::Button*>(child); button && button->getButtonText() == text)
            return button;
        if (auto* found = findButton(*child, text)) return found;
    }
    return nullptr;
}

void checkDrawerControls(Processor& owner, juce::Component& parent, int& sliders, int& buttons,
                         bool stackSliders = false) {
    for (auto* child : parent.getChildren()) {
        if (!child->isVisible()) continue;
        // JUCE parents the viewed component inside a clipping holder, not directly in the viewport.
        auto* viewport = dynamic_cast<juce::Viewport*>(parent.getParentComponent());
        const bool clippedContent = viewport != nullptr && viewport->getViewedComponent() == child;
        if (!clippedContent && dynamic_cast<juce::Viewport*>(&parent) == nullptr)
            expect(parent.getLocalBounds().contains(child->getBounds()), "Drawer child outside parent: " + child->getName());
        if (auto* slider = dynamic_cast<juce::Slider*>(child)) {
            ++sliders;
            const auto id = slider->getComponentID();
            const bool linear = stackSliders || id.endsWith("Mix") || id == "choraleMotion"
                            || (id.startsWith("fx") && id.endsWith("Motion"));
            expect(slider->getSliderStyle() == (linear ? juce::Slider::LinearHorizontal
                                                      : juce::Slider::RotaryHorizontalVerticalDrag),
                   "Stack voice controls and standalone FX Mix use horizontal sliders");
            auto& p = parameter(owner, id);
            slider->setValue(p.getNormalisableRange().end, juce::sendNotificationSync);
            expectNear(parameterValue(owner, id), p.getNormalisableRange().end, "Drawer slider binding: " + id);
            if (bool(slider->getProperties()["vocalstackTexturedBar"])) {
                juce::Label* caption = nullptr;
                for (auto* nested : slider->getChildren())
                    if (auto* label = dynamic_cast<juce::Label*>(nested)) caption = label;
                expect(caption != nullptr && caption->isVisible() && !slider->isTextBoxEditable()
                           && !caption->isEditable(), "Drawer Mix native caption must be display-only");
                auto* root = slider->getTopLevelComponent();
                const auto point = root->getLocalPoint(caption, caption->getLocalBounds().getCentre());
                auto* hit = root->getComponentAt(point);
                expect(hit == slider, "Drawer Mix caption must pass root mouse hits to slider");
                const auto start = hit->getLocalPoint(root, point).toFloat();
                const auto focus = juce::Component::getCurrentlyFocusedComponent();
                setParameter(owner, id, p.getNormalisableRange().convertFrom0to1(.5f));
                expectNear(slider->getValue(), parameterValue(owner, id), "Drawer Mix follows host: " + id);
                const auto before = parameterValue(owner, id);
                const auto end = start.translated(slider->getWidth() * .2f, 0);
                const auto now = juce::Time::getCurrentTime();
                const auto mouse = [&](juce::Point<float> point, int modifiers, bool dragged) {
                    return juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(), point,
                        juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, slider, slider, now, start, now, 1, dragged};
                };
                slider->mouseDown(mouse(start, juce::ModifierKeys::leftButtonModifier, false));
                expectNear(parameterValue(owner, id), before, "Drawer Mix caption mouseDown must not jump");
                slider->mouseUp(mouse(start, 0, false));
                expectNear(parameterValue(owner, id), before, "Drawer Mix caption click must not jump");
                expect(caption->getCurrentTextEditor() == nullptr && !caption->isEditable()
                            && !slider->isTextBoxEditable(), "Drawer Mix caption click must remain display-only");
                expect(juce::Component::getCurrentlyFocusedComponent() == focus, "Drawer Mix caption click stole focus");
                slider->mouseDown(mouse(start, juce::ModifierKeys::leftButtonModifier, false));
                slider->mouseDrag(mouse(end, juce::ModifierKeys::leftButtonModifier, true));
                slider->mouseUp(mouse(end, 0, true));
                expect(std::abs(parameterValue(owner, id) - before) > .01, "Drawer Mix caption drag did not edit host: " + id);
                expectNear(parameterValue(owner, id), slider->getValue(), "Drawer Mix drag host binding: " + id);
                expect(caption->getCurrentTextEditor() == nullptr, "Drawer Mix drag opened numeric entry");
                expect(juce::Component::getCurrentlyFocusedComponent() == focus, "Drawer Mix caption drag stole focus");
                continue;
            }
            expect(slider->isTextBoxEditable(), "Drawer numeric entry disabled");
            slider->showTextBox();
            juce::TextEditor* entry = nullptr;
            for (auto* nested : slider->getChildren())
                if (auto* label = dynamic_cast<juce::Label*>(nested))
                    if (auto* text = label->getCurrentTextEditor()) entry = text;
            expect(entry != nullptr && entry->getWantsKeyboardFocus(), "Drawer numeric entry cannot own keys");
            entry->setText(juce::String(p.getNormalisableRange().start, 4), false);
            slider->hideTextBox(false);
            expectNear(parameterValue(owner, id), p.getNormalisableRange().start, "Drawer numeric commit: " + id);
            slider->showTextBox();
            slider->hideTextBox(true);
            expectNear(parameterValue(owner, id), p.getNormalisableRange().start, "Drawer numeric cancel: " + id);
            continue; // Slider internals have transient text editors and their own layout.
        }
        if (auto* button = dynamic_cast<juce::Button*>(child);
                    button && owner.parameters.getParameter(button->getComponentID()) != nullptr) {
            ++buttons;
            const auto id = button->getComponentID();
            setParameter(owner, id, 1);
            if (id != "advancedOn") expect(button->getToggleState(), "Voice/Chorale switch did not follow host");
        }
        checkDrawerControls(owner, *child, sliders, buttons, stackSliders);
    }
}

juce::String componentName(juce::Component& component) {
    if (auto* button = dynamic_cast<juce::Button*>(&component))
        return button->getButtonText();
    if (auto* label = dynamic_cast<juce::Label*>(&component))
        return "Label " + label->getText();
    return component.getName().isNotEmpty() ? component.getName() : juce::String("unnamed child");
}

void saveSnapshot(juce::Component& editor, const juce::String& filename) {
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    expect(image.isValid(), "Editor snapshot is invalid: " + filename);
    expect(image.getWidth() == editor.getWidth() && image.getHeight() == editor.getHeight(),
           "Editor snapshot dimensions changed: " + filename);
    const auto destination = juce::File::getCurrentWorkingDirectory().getChildFile(filename);
    juce::FileOutputStream stream(destination);
    expect(stream.openedOk(), "Cannot open snapshot: " + destination.getFullPathName());
    expect(stream.setPosition(0), "Cannot rewind snapshot file: " + filename);
    expect(stream.truncate().wasOk(), "Cannot truncate previous snapshot: " + filename);
    juce::PNGImageFormat png;
    expect(png.writeImageToStream(image, stream), "PNG encoding failed: " + filename);
    stream.flush();
    expect(stream.getStatus().wasOk(), "PNG write failed: " + stream.getStatus().getErrorMessage());
    expect(destination.getSize() > 0, "PNG file is empty: " + filename);
    std::cout << "Snapshot: " << destination.getFullPathName().toStdString() << '\n';
}

void editorSkin(int theme) {
    std::unique_ptr<Processor> ownerStorage = std::make_unique<Processor>();
    Processor& owner = *ownerStorage;
    owner.setSkin(theme);
    const juce::String skin = theme == 2 ? "Spectral" : theme == 1 ? "Neon" : "Device";
    const auto filename = "editor-" + skin.toLowerCase();
    std::cout << skin << " editor snapshots: OFFSCREEN, no native peer or physical monitor placement\n";
    std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
    expect(editor != nullptr, skin + " editor creation failed");
    editor->setVisible(true);
    expectPassiveControls(*editor);

    constexpr std::array<const char*, 7> names{"Sidechain", "Lead / Stack", "Human", "Width", "Formant", "Output", "Mix"};
    std::array<juce::Slider*, names.size()> knobs{};
    std::vector<juce::Component*> controls;
    MeterPanel* meters = nullptr;
    int sliderCount = 0;
    auto& canvas = *vocalstack::toneUiTests::slider(*editor, "outputGain").getParentComponent();
    for (auto* child : canvas.getChildren()) {
        if (!child->isVisible() || dynamic_cast<juce::TooltipWindow*>(child) != nullptr)
            continue; // Hidden panels and transient tooltips do not participate in layout.
        const auto description = skin + " / " + componentName(*child);
        expect(!child->getBounds().isEmpty(), description + " has empty bounds");
        expect(canvas.getLocalBounds().contains(child->getBounds()), description + " lies outside the design canvas");
        auto* slider = vocalstack::toneUiTests::find<juce::Slider>(*child, [](auto& candidate) {
            return candidate.getComponentID() == "globalMix";
        });
        if (!slider) slider = dynamic_cast<juce::Slider*>(child);
        if (slider) {
            if (slider->getComponentID() == "decorativeMotion") {
                expect(slider->getName() == "Motion" && slider->getSliderStyle() == juce::Slider::LinearHorizontal,
                       skin + " Motion presentation slider missing");
                controls.push_back(child);
                continue;
            }
            ++sliderCount;
            bool recognised = false;
            for (size_t index = 0; index < names.size(); ++index) {
                if (slider->getName() == names[index]) {
                    expect(knobs[index] == nullptr, description + " is duplicated");
                    knobs[index] = slider;
                    recognised = true;
                }
            }
            expect(recognised, description + " is an unexpected direct slider");
            controls.push_back(child);
        } else if (dynamic_cast<juce::Button*>(child) != nullptr) {
            controls.push_back(child);
        } else if (auto* panel = dynamic_cast<MeterPanel*>(child)) {
            expect(meters == nullptr, skin + " has multiple meter panels");
            meters = panel;
            controls.push_back(panel);
        }
    }
    expect(sliderCount == 7, skin + " must have six main rotaries and one nested global Mix");
    expect(meters != nullptr, skin + " is missing its MeterPanel child");
    for (size_t i = 0; i < knobs.size(); ++i) {
        expect(knobs[i] != nullptr, skin + " is missing knob " + names[i]);
        expect(knobs[i]->isDoubleClickReturnEnabled(), skin + " knob does not reset on double-click: " + names[i]);
        const auto& target = parameter(owner, i == 0 ? "sidechainBlend" :
            i < vocalstack::knobParameterIds.size() ? vocalstack::knobParameterIds[i] : "globalMix");
        const auto expected = i == 4 || i == 5 ? 0.0f : target.convertFrom0to1(target.getDefaultValue());
        expectNear(knobs[i]->getDoubleClickReturnValue(), expected, skin + " knob double-click default: " + names[i]);
    }
    // Compare sibling control bounds, including the global Mix wrapper, not its nested slider.
    for (size_t i = 0; i < controls.size(); ++i)
        for (size_t j = i + 1; j < controls.size(); ++j)
            expect(!controls[i]->getBounds().intersects(controls[j]->getBounds()),
                   skin + " controls overlap: " + componentName(*controls[i]) + " / " + componentName(*controls[j]));

    auto& output = *knobs[5];
    expectNear(output.getMinimum(), -24.0, skin + " Output knob minimum");
    expectNear(output.getMaximum(), 12.0, skin + " Output knob maximum");
    expectNear(output.getValue(), 0.0, skin + " Output knob initial value");
    output.setValue(-7.0, juce::sendNotificationSync);
    expectNear(parameterValue(owner, "outputGain"), -7.0, skin + " Output attachment did not update the parameter");
    setParameter(owner, "outputGain", 3.0f);
    expectNear(output.getValue(), 3.0, skin + " Output attachment did not follow the parameter");
    output.setValue(0.0, juce::sendNotificationSync);

    for (auto* control : controls)
        if (auto* button = dynamic_cast<juce::Button*>(control);
            button != nullptr && button->getButtonText().startsWith("SKIN")) {
            constexpr int themeCount = int(vocalstack::Theme::grass) + 1;
            for (int step = 1; step <= themeCount; ++step) {
                button->onClick();
                expect(owner.getSkin() == (theme + step) % themeCount, "Skin button must cycle all four themes");
                expect(int(editor->getProperties()[vocalstack::vocalstackThemeProperty]) == owner.getSkin(),
                       "Editor root theme did not follow skin button");
                expectPassiveControls(*editor);
            }
        }

    // Keep injection and capture synchronous so the editor's timer cannot replace the synthetic snapshot.
    meters->setMeters(syntheticMeters());
    saveSnapshot(*editor, filename + ".png");
    const int windowHeight = editor->getHeight();
    const int windowWidth = editor->getWidth();
    for (const auto* obsolete : {"Show Advanced", "Hide Advanced", "Voice stack", "Open Routing"})
        expect(findButton(*editor, obsolete) == nullptr, "Obsolete Stack action remains: " + juce::String(obsolete));
    expect(!owner.settings().advanced, "Always-visible Stack changed legacy activation");
    // Stack preserves native control sizes and scrolls vertically below the FX row.
    static_assert(vocalstack::editorLayout::defaultWindowHeight == 1114);
    static_assert(vocalstack::editorLayout::additionalVoiceScrollbarHeight == 10);
    auto* measuredRack = vocalstack::toneUiTests::find<vocalstack::MainBusFxRackPanel>(*editor, [](auto&) { return true; });
    expect(measuredRack != nullptr, "Stack FX rack missing");
    expect(editor->isResizable() && windowHeight == vocalstack::editorLayout::defaultWindowHeight
               && windowWidth == vocalstack::editorLayout::editorWidth,
           "Fresh editor must remain resizable at 940x1114, including the enlarged 62px preset footer");
    expect(canvas.getBounds() == juce::Rectangle<int>(0, 0, vocalstack::editorLayout::editorWidth,
                                                      (measuredRack->getBottom() + vocalstack::editorLayout::stackBottomPadding))
               && canvas.getTransform().isIdentity(),
           "Fresh Stack must retain its unscaled design plus the voice scrollbar growth");

    int advancedSliders = 0, advancedButtons = 0, choraleSliders = 0, choraleButtons = 0;
    for (auto* child : canvas.getChildren()) {
        if (dynamic_cast<vocalstack::DoublerPanel*>(child))
            checkDrawerControls(owner, *child, advancedSliders, advancedButtons, true);
    }
    auto* rack = vocalstack::toneUiTests::find<vocalstack::MainBusFxRackPanel>(canvas, [](auto&) { return true; });
    expect(rack && rack->isVisible() && rack->getBounds() == juce::Rectangle<int>(
               18, vocalstack::editorLayout::stackRackY, 904, rack->preferredHeightForWidth(904)),
           "Stack must lay out the Main Bus FX row below the voice cards at its preferred height");
    auto* freshStack = vocalstack::toneUiTests::find<vocalstack::DoublerPanel>(canvas, [](auto&) { return true; });
    expect(freshStack && freshStack->getBounds() == juce::Rectangle<int>(18, vocalstack::editorLayout::stackPanelY, 904,
                                                                        vocalstack::editorLayout::stackPanelHeight)
               && canvas.getLocalBounds().contains(freshStack->getBounds())
               && !rack->getBounds().intersects(freshStack->getBounds()),
           "The rack must not overlap or clip the full Stack panel");
    auto* chorale = vocalstack::toneUiTests::find<juce::Component>(*rack, [](auto& child) {
        return child.getComponentID() == "mainBusFx12";
    });
    expect(chorale != nullptr, "Main Bus FX rack is missing its canonical Chorale card");
    checkDrawerControls(owner, *chorale, choraleSliders, choraleButtons);
        auto* choraleTab = vocalstack::toneUiTests::find<juce::Component>(*rack, [](auto& child) {
            return child.getComponentID() == "mainBusFxTab12";
        });
        expect(choraleTab != nullptr, "Main Bus Chorale node tab missing");
        checkDrawerControls(owner, *choraleTab, choraleSliders, choraleButtons);
    expect(advancedSliders == 24 && advancedButtons == 3, "Fresh Stack must contain Lead and two complete eight-slider voice cards");
    expect(choraleSliders == 2 && choraleButtons == 1, "Main Bus Chorale knobs or host-bound switch missing");
    auto* addVoice = findButton(*editor, "+ Add voice");
    expect(addVoice != nullptr, "Missing Add voice action");
    for (int i = 0; i < 6; ++i) addVoice->onClick();
    expect(!addVoice->isEnabled(), "Add voice action remained enabled at capacity");
    advancedSliders = advancedButtons = 0;
    for (auto* child : canvas.getChildren())
        if (dynamic_cast<vocalstack::DoublerPanel*>(child))
            checkDrawerControls(owner, *child, advancedSliders, advancedButtons, true);
    expect(advancedSliders == 72 && advancedButtons == 9, "Lead and eight complete stable voice cards missing");
    expectPassiveControls(*editor);
    setParameter(owner, "advancedOn", 0);
    expect(!owner.settings().advanced, "Legacy host activation no longer disables Advanced");
    owner.setAdvancedEnabled(true);
    expect(owner.settings().advanced, "Legacy Advanced activation API failed");
    auto* basic = findButton(*editor, "Reset to Basic");
    expect(basic != nullptr, "Missing Reset to Basic");
    basic->onClick();
    expectBasicVoices(owner, owner.settings().human);
    expect(owner.settings().advanced, "UI Reset to Basic disabled Advanced");
    expectPassiveControls(*editor);
    for (const auto* id : vocalstack::leadParameterIds) {
        auto& p = parameter(owner, id);
        p.setValueNotifyingHost(p.getDefaultValue());
    }
    editor->setSize(940, 1104);
    saveSnapshot(*editor, filename + "-advanced.png");
    for (int i = 0; i < 6; ++i) addVoice->onClick();
    expectPassiveControls(*editor);
    saveSnapshot(*editor, filename + "-eight-voices.png");
    basic->onClick();
    expectBasicVoices(owner, owner.settings().human);
    editor->setSize(windowWidth, windowHeight);
    owner.setAdvancedVisible(false);
    juce::Timer::callPendingTimersSynchronously();
    auto* stack = vocalstack::toneUiTests::find<vocalstack::DoublerPanel>(*editor, [](auto&) { return true; });
    expect(stack && stack->isVisible() && canvas.getHeight() == (measuredRack->getBottom() + vocalstack::editorLayout::stackBottomPadding) && owner.settings().advanced,
           "Legacy visibility state hid Stack or changed activation");
    editor.reset();
    const auto savedWindow = owner.getRoutingLayout();
    expect(int(savedWindow.getProperty("editorWidth")) == windowWidth &&
           int(savedWindow.getProperty("editorHeight")) == windowHeight,
           "Scrollable canvas growth changed saved/default window dimensions");
    editor.reset(owner.createEditor());
    stack = vocalstack::toneUiTests::find<vocalstack::DoublerPanel>(*editor, [](auto&) { return true; });
    expect(editor->getWidth() == windowWidth && editor->getHeight() == windowHeight && stack && stack->isVisible(),
           "Editor reopen lost dimensions or always-visible Stack");
    expectPassiveControls(*editor);
}

#include "ProcessorUiRegressionTests.h"
#include "RoutingPolishTests.h"
#include "StackModulationUiTests.h"
#include "RoutingExpansionTests.h"
#include "DryWetUiTests.h"
#include "RoutingChunkyTests.h"

void deviceEditor() { editorSkin(0); }
void neonEditor() { editorSkin(1); }
void spectralEditor() { editorSkin(2); }
}

// Optional bounded native paint-cost probe, not a delivered-FPS measurement.
// Run separately from regressions so native interaction/capture is never raced.
void benchmarkRoutingWork(bool componentPaint = false) {
#if JUCE_WINDOWS
    using namespace vocalstack;
    guiTestDisplay::enumerate();
    auto owner = std::make_unique<Processor>();
    auto graph = defaultGraph();
    for (auto type : {NodeType::spectralMorph, NodeType::waveshaper, NodeType::equalizer,
            NodeType::compressor, NodeType::reverb, NodeType::delay, NodeType::bitcrusher,
            NodeType::granular, NodeType::convolution, NodeType::saturator, NodeType::gain,
            NodeType::stereo, NodeType::gate, NodeType::xpFilter, NodeType::synthFilter}) {
        const auto edit = mainBusFx::add(graph, type);
        toneUiTests::require(edit.changed, "Benchmark FX graph capacity"); graph = edit.graph;
    }
    juce::String error;
    toneUiTests::require(owner->setRoutingGraph(graph, error), "Benchmark graph rejected");
    owner->prepareToPlay(48000, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
    const auto work = guiTestDisplay::selected().userArea;
    editor->setSize(std::min(1280, work.getWidth()), std::min(800, work.getHeight()));
    guiTestDisplay::attach(*editor); editor->toFront(true); toneUiTests::action(*editor, "Routing");
    auto* panel = toneUiTests::find<RoutingPanel>(*editor, [](auto& c) { return c.isShowing(); });
    toneUiTests::require(panel != nullptr, "Benchmark Routing panel missing");
    const auto dispatch = [] {
        MSG message;
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessage(&message); }
        juce::Timer::callPendingTimersSynchronously();
    };
    juce::AudioBuffer<float> audio(2, 256); juce::MidiBuffer midi;
    int sample = 0;
    std::cout << (componentPaint ? "COMPONENT_ROUTING_COST: forced component-snapshot cost with a placed native peer; NOT native exposed paint/FPS, valid even when desktop is occluded\n"
                                : "NATIVE_ROUTING_COST: native paint/timer cost only, not delivered FPS; 15 independent FX, synchronous real PCM\n");
    for (double motion : {0., 3., 100.}) for (int state = 0; state < 3; ++state) {
        owner->setMotionPercent(motion);
        toneUiTests::action(*panel, state == 0 ? "Collapse all" : "Expand all");
        if (state == 2) {
            auto layout = owner->getRoutingLayout();
            layout.setProperty("panX", -50000.f, nullptr); layout.setProperty("panY", -50000.f, nullptr);
            owner->setRoutingLayout(layout);
        } else if (state == 0) toneUiTests::action(*panel, "Fit");
        for (int settle = 0; settle < 8; ++settle) { juce::Thread::sleep(16); dispatch(); }
        const auto native = static_cast<HWND>(editor->getPeer()->getNativeHandle());
        RECT nativeBounds{};
        toneUiTests::require(GetWindowRect(native, &nativeBounds) != 0, "Native Routing benchmark window bounds unavailable");
        POINT point{(nativeBounds.left + nativeBounds.right) / 2, (nativeBounds.top + nativeBounds.bottom) / 2};
        const auto hit = WindowFromPoint(point);
        if (!componentPaint && GetAncestor(hit, GA_ROOT) != GetAncestor(native, GA_ROOT)) {
            wchar_t className[256]{}; GetClassNameW(hit, className, 256);
            throw std::runtime_error("Native Routing cost probe is occluded by " + juce::String(className).toStdString());
        }
        panel->resetRoutingWorkCounters(); FluidBackground::resetWorkCounters();
        const auto started = juce::Time::getMillisecondCounterHiRes();
        double dispatchMs = 0, componentPaintMs = 0;
        for (int frame = 0; frame < 60; ++frame) {
            for (int block = 0; block < 3; ++block) {
                for (int i = 0; i < 256; ++i, ++sample) {
                    audio.setSample(0, i, .2f * std::sin(float(sample) * .063f));
                    audio.setSample(1, i, .17f * std::sin(float(sample) * .077f));
                }
                owner->processBlock(audio, midi);
            }
            const auto before = juce::Time::getMillisecondCounterHiRes(); dispatch();
            dispatchMs += juce::Time::getMillisecondCounterHiRes() - before;
            if (componentPaint) {
                const auto paintStart = juce::Time::getMillisecondCounterHiRes();
                const auto snapshot = editor->createComponentSnapshot(editor->getLocalBounds());
                toneUiTests::require(snapshot.isValid(), "Routing cost component snapshot missing");
                componentPaintMs += juce::Time::getMillisecondCounterHiRes() - paintStart;
            }
            juce::Thread::sleep(16);
        }
        const auto routing = panel->routingWorkCounters(); const auto material = FluidBackground::workCounters();
        std::cout << "routing_cost motion=" << motion << " state=" << (state == 0 ? "collapsed" : state == 1 ? "expanded-exposed" : "expanded-offscreen")
            << " wall_ms=" << juce::Time::getMillisecondCounterHiRes() - started << " dispatch_ms=" << dispatchMs
            << " forced_component_ms=" << componentPaintMs
            << " canvas_paints=" << routing.canvasPaints << " canvas_ms=" << routing.canvasPaintMs
            << " syncs=" << routing.cardSynchronizations << " timer_ticks=" << routing.timerTicks
            << " nodes_drawn=" << routing.nodesDrawn << " nodes_culled=" << routing.nodesCulled
            << " cables_drawn=" << routing.cablesDrawn << " cables_culled=" << routing.cablesCulled
            << " material_rasters=" << material.rasterizations << " material_shared_hits=" << material.sharedHits << " material_hits=" << material.cacheHits
            << " material_pixels=" << material.pixelsGenerated << '\n';
    }
    editor.reset(); owner->releaseResources();
#else
    throw std::runtime_error("Native Routing cost probe currently requires Windows message dispatch");
#endif
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc == 2 && juce::String(argv[1]) == "--benchmark-routing-components") {
        try { benchmarkRoutingWork(true); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Component Routing cost probe: " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--benchmark-routing") {
        try { benchmarkRoutingWork(); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Native Routing cost probe: " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render-typography-motion") {
        try { vocalstack::typographyMotionTests::capture(); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Typography/Motion capture: " << error.what() << '\n'; return 1; }
    }
    if (argc == 4 && juce::String(argv[1]) == "--render-update-gallery") {
        try { vocalstack::nodeGalleryTests::run(argv[2], juce::String(argv[3]).getIntValue(), true); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Coordinated update capture: " << error.what() << '\n'; return 1; }
    }
    if (argc >= 2 && juce::String(argv[1]) == "--render-node-gallery") {
        try { vocalstack::nodeGalleryTests::run(argc > 2 ? juce::String(argv[2]) : juce::String()); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Node gallery: " << error.what() << std::endl; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render-spectral-morph") {
        try { vocalstack::spectralMorphUiTests::run(true); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Spectral Morph render: " << error.what() << std::endl; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render-measured-fields") {
        try { vocalstack::measuredFieldUiTests::run(true); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Measured field render: " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render-routing-chunky") {
        try { routingChunky::screenshots(); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Routing render: " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render-scrollbars") {
        try { vocalstack::scrollbarTests::screenshots(); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Scrollbar render: " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render-inspector-controls") {
        try { vocalstack::inspectorControlLayoutTests::screenshots(); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Inspector render: " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render-drywet") {
        try { dryWetUi::screenshots(); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Dry/Wet render: " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render-coordinated") {
        try { vocalstack::coordinatedPreviewTests::screenshots(); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Coordinated preview render: " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render-factoryfx") {
        try { vocalstack::factoryFxUiTests::screenshots(); return 0; }
        catch (const std::exception& error) { std::cerr << "[FAIL] Factory FX render: " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && juce::String(argv[1]) == "--render") {
        try {
            vocalstack::updateUiTests::screenshots();
            routingExpansion::screenshots();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] Render: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && juce::String(argv[1]) == "--profile-routing") {
        try {
            routingExpansion::profileRouting();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] Routing profile: " << error.what() << '\n';
            return 1;
        }
    }
    juce::String filter;
    if (argc == 3 && juce::String(argv[1]) == "--filter" && juce::String(argv[2]).isNotEmpty())
        filter = argv[2];
    else if (argc != 1) {
        std::cerr << "Usage: VocalStackEditorTests [--render | --profile-routing | --filter <name substring>]\n";
        return 2;
    }
    struct Test {
        const char* name;
        void (*run)();
    };
    const std::array<Test, 85> tests{{
                {"Technical typography fits reserved lanes in all themes", vocalstack::typographyMotionTests::run},
            {"Control layout update, midpoint sliders, inward rings and wide Morph", vocalstack::controlLayoutUpdateTests::run},
            {"Compressor measured four-second gain reduction history", vocalstack::compressorHistoryTests::run},
            {"Utility visual telemetry, Gate history and Mono Audition", [] {
                vocalstack::UtilityVisualUpdateTests::telemetry(); vocalstack::UtilityVisualUpdateTests::dynamicsAndMono();
                            vocalstack::UtilityVisualUpdateTests::effectiveGainWet();
                auto owner = std::make_unique<vocalstack::Processor>(); vocalstack::UtilityVisualUpdateTests::controls(*owner);
            }},
            {"Scrollbar thickness, nested axes, confinement and dense inspectors", vocalstack::scrollbarTests::run},
            {"Dry/Wet repeated Add, Duplicate, independent controls and history", dryWetUi::run},
        {"Spectral Morph measured internal/external donor and actual Freeze", [] { vocalstack::spectralMorphUiTests::run(); }},
        {"Reactive material models and three-spectrum composition", vocalstack::reactiveMaterialTests::run},
        {"Measured graph fields, real reverb tails and Motion zero", [] { vocalstack::measuredFieldUiTests::run(); }},
        {"Coordinated Motion, sparse groups and real factory FX telemetry", vocalstack::coordinatedPreviewTests::run},
        {"Advanced and Chorale host contract", advancedParameterContract},
        {"Advanced activation and isolated reset", advancedActivationAndReset},
        {"Advanced and Chorale state", advancedStateRoundTrips},
        {"Output parameter contract", outputParameterContract},
        {"Output save/copy/paste/reset", outputStateRoundTrips},
        {"Legacy state Output fallback", legacyStateDefaults},
        {"Named bypass binding", bypassBinding},
        {"Audio/UI meter publication and clear", meterPublicationAndReset},
        {"Meter click reset and host shortcuts", meterTransportKeys},
        {"Editor host shortcuts and numeric entry", editorTransportAndTextEntry},
        {"Fresh voices, stable identity and reset", voiceLifecycle},
        {"Legacy four-voice migration including mute", legacyFourVoiceMigration},
        {"Musical pitch host bridge", musicalPitchParameters},
        {"Routing square chunky chrome, pin corners and all effect cards", routingChunky::run},
        {"Routing inspector useful dial area, text lanes and narrow reflow", vocalstack::inspectorControlLayoutTests::run},
        {"Routing drafts, active fallback and layout state", routingStateRegression},
        {"Fixed FX automation slots", fxAutomationRegression},
        {"Spatial EQ parameters, recall and rendered automation", vocalstack::toneProcessorTests::spatialEq},
        {"Spatial EQ selected-band controls (offscreen)", vocalstack::toneUiTests::spatialEq},
        {"Spatial EQ signed audio through HEAVEN", vocalstack::toneProcessorTests::spatialEqSignedAudio},
        {"Tone FX parameters, recall and rendered automation", vocalstack::toneProcessorTests::run},
        {"Tone FX UI, EQ gestures and multiband cable endpoints", vocalstack::toneUiTests::run},
        {"Preset validation, full patch recall, files and toolbar", vocalstack::presetTests::run},
        {"Creative FX processor, state, automation, factories and both skins", [] {
            expect(vocalstack::creativeIntegrationTests::run(), "Creative integration group failed; see named failures above");
        }},
        {"Utility processor, append-only parameters, state and audio", [] {
            expect(vocalstack::utilityIntegrationTests::run(), "Utility integration group failed; see named failures above");
        }},
        {"Factory convolution and saturator state, processor, layout and focus", vocalstack::factoryFxUiTests::run},
        {"Three-theme state, legacy migration and preset isolation", vocalstack::updateUiTests::themeState},
        {"All utility inspectors in all three themes", vocalstack::updateUiTests::utilityInspectors},
        {"Square chrome and zero Lead badges in all themes", vocalstack::updateUiTests::squareChromeAndBadges},
                {"Responsive Creative controls, compact switches and scroll anchors", vocalstack::creativePanelLayoutTests::run},
        {"Half-speed relative sliders across sizes, skew and themes", vocalstack::updateUiTests::halfSpeedSliders},
        {"Slider selection, feedback, alignment, sensitivity and knob modes", vocalstack::sliderRenderingTests::run},
                {"Fluid pixel backgrounds, proportional blocks and deterministic phases", vocalstack::fluidBackgroundTests::run},
                {"Hovered controls scroll nearest viewport without parameter edits", vocalstack::hoverWheelRegressionTests::run},
                {"Voice-sized Tone cards and passive EQ wheel scrolling", vocalstack::toneCardLayoutTests::run},
                {"Immediate filter redraw and bounded latest-target synth scheduler", [] {
                    vocalstack::UtilityPanelTests::scheduling(); vocalstack::UtilityPanelTests::controls();
                }},
        {"Routing half-width modulator rail in all three themes", vocalstack::updateUiTests::routingModulatorRail},
        {"Routing waveform previews follow modulator controls", vocalstack::updateUiTests::routingWaveformPreviews},
        {"Coherent creative visual models in all three themes", vocalstack::updateUiTests::creativeVisuals},
        {"Voice 1 FX alters only Voice 1 with automatic merging", perVoiceFxSignalPath},
        {"Routing mouse edits, history, focus and both skins", routingUiRegression},
        {"Shared rack/Routing Undo integration", [] {
            expect(vocalstack::rackUndoIntegrationTests::run(), "Rack Undo integration failed; see named failures above");
        }},
        {"Embedded Stack/Routing tabs retain Advanced in both skins", routingPolish::embeddedTabs},
        {"Occupied output replacement, disconnect inspector and cursor zoom", routingPolish::outputGestures},
        {"Multiband output edits preserve other ports", routingPolish::multibandOutputs},
        {"Splitter output edits preserve independent branches", routingPolish::splitterOutputs},
        {"Routing voice visibility follows presence, not mute", routingPolish::voiceVisibility},
        {"Canonical Chorale folding edits the real graph and follows host", routingPolish::foldedMainChorale},
        {"Waveshaper Raw disables Quality without erasing it", stackModulationUi::waveQualityRaw},
        {"Global Mix binding and bypass disable in both skins", stackModulationUi::globalMixBypass},
        {"Resizable Stack canvas and full-size Routing retain window", stackModulationUi::resizeTabs},
        {"Shared Lead/voice controls and inspector selection", stackModulationUi::sharedInspectors},
        {"Modulation gestures and Controls/Assignments removal", stackModulationUi::modulationInspector},
        {"EQ handles assign modulation without host base writes", stackModulationUi::eqModulationGestures},
        {"Local mixer knob, modulation and machine aliases", stackModulationUi::localMixerInspector},
                {"Compact Stack cards, fixed inspector rows and click activation", stackModulationUi::compactCards},
                {"Left modulator rail and independent fixed-knob bottom strip", stackModulationUi::modulationRailGeometry},
                        {"Inspector divider clamps, persistence and live component identity", stackModulationUi::resizableInspector},
                {"Modulation divider clamps, persistence and full-height inspector identity", stackModulationUi::resizableModulationPanel},
                {"Lead monitor index 8, selection and processor publication", stackModulationUi::leadMonitor},
                {"Waveshaper transfer follows live effective modulation", stackModulationUi::modulatedWaveshaperCurve},
                {"Routing context and Add Node menus anchor to cursor", stackModulationUi::menuCursorPlacement},
        {"Routing sticky inspector and global/individual expansion", routingExpansion::stickyAndGlobal},
        {"Routing first-use packing and independent manual positions", routingExpansion::packingAndPositions},
        {"Routing horizontal packing separates parallel expanded lanes", routingExpansion::horizontalPacking},
        {"Routing layout-only history preserves newer graph and automation", routingExpansion::layoutHistory},
        {"Routing expansion and position banks survive reopen/state recall", routingExpansion::reopen},
        {"Expanded routing controls bind parameters in all three themes", routingExpansion::parameterBindings},
        {"Expanded routing virtualization, pan and cursor-centred zoom", routingExpansion::virtualization},
        {"Visible multi-synth scheduler coalescing, cancellation and UI responsiveness", routingExpansion::multiSynthScheduler},
        {"Interrupted expanded drags close host gestures before unmount", routingExpansion::interruptedDrags},
        {"Unfinished expanded numeric entry cancels and releases edit scope", routingExpansion::unfinishedTextEntry},
        {"Device editor and PNG", deviceEditor},
        {"Neon editor and PNG", neonEditor},
        {"Spectral editor and PNG", spectralEditor}
    }};
    int failures = 0;
    int selected = 0;
    for (const auto& test : tests) {
        if (filter.isNotEmpty() && !juce::String(test.name).containsIgnoreCase(filter)) continue;
        ++selected;
        std::cout << "[RUN] " << test.name << std::endl;
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }
    if (selected == 0) {
        std::cerr << "No editor tests match filter: " << filter << '\n';
        return 2;
    }
    std::cout << selected - failures << '/' << selected << " editor tests passed\n";
    return failures == 0 ? 0 : 1;
}
