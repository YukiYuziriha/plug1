#include "PluginProcessor.h"
#include "MeterPanel.h"
#include "DoublerPanel.h"
#include "RoutingPanel.h"
#include "ProcessorRoutingState.h"
#include "ToneProcessorTests.h"
#include "ToneUiTests.h"

#include <array>
#include <cmath>
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
    Processor owner;
    const std::array<const char*, 10> originalNames{"Leveler", "Mix", "Human", "Width", "Formant",
        "Leveler Active", "Doubler Active", "Formant Active", "Bypass", "Output"};
    expect(owner.getParameters().size() == 874, "Expected 139 original + 735 appended tone host parameters");
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
    Processor owner;
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

    Processor automated;
    setParameter(automated, "human", 90);
    setParameter(automated, "advancedOn", 1);
    expectBasicVoices(automated, 0.35f); // Host activation uses safe stored baseline, not message-thread rewrites.
    setParameter(automated, "advancedOn", 0);
    automated.setAdvancedEnabled(true);
    expectBasicVoices(automated, 0.35f);
}

void advancedStateRoundTrips() {
    Processor source;
    source.setAdvancedEnabled(true);
    source.setAdvancedVisible(true);
    source.setFlash(true);
    for (size_t i = 11; i < vocalstack::parameterIds.size(); ++i)
        parameter(source, vocalstack::parameterIds[i]).setValueNotifyingHost(i % 2 == 0 ? 0.0f : 1.0f);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    Processor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    for (size_t i = 10; i < vocalstack::parameterIds.size(); ++i)
        expectNear(parameterValue(restored, vocalstack::parameterIds[i]), parameterValue(source, vocalstack::parameterIds[i]),
                   "Appended parameter did not roundtrip");
    expect(restored.isFlash() && restored.isAdvancedVisible(), "Editor properties did not roundtrip");
    restored.setAdvancedEnabled(false);
    setParameter(restored, "human", 100);
    restored.setAdvancedEnabled(true);
    expectNear(parameterValue(restored, "voice1Gain"), -60, "Restored activation overwrote custom voice");
    Processor pasted;
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

    Processor partial;
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
    Processor owner;
    auto& output = parameter(owner, "outputGain");
    expect(output.paramID == "outputGain", "Output parameter ID changed");
    expect(output.getName(64) == "Output", "Output parameter must be named Output");
    expectNear(output.getNormalisableRange().start, -24.0, "Output minimum dB");
    expectNear(output.getNormalisableRange().end, 12.0, "Output maximum dB");
    expectNear(output.convertFrom0to1(output.getDefaultValue()), 0.0, "Output default dB");
    expectNear(parameterValue(owner, "outputGain"), 0.0, "New instance output dB");
}

void outputStateRoundTrips() {
    Processor source;
    setParameter(source, "outputGain", -9.5f);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    expect(saved.getSize() > 0, "Saved processor state is empty");

    Processor restored;
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
    Processor owner;
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
    Processor owner;
    auto& bypass = parameter(owner, "bypass");
    auto& output = parameter(owner, "outputGain");
    expect(owner.getBypassParameter() == &bypass, "Host bypass does not point to the named bypass parameter");
    expect(owner.getBypassParameter() != &output, "Appending Output changed the host bypass parameter");
    setParameter(owner, "bypass", 1.0f);
    expectNear(owner.getBypassParameter()->getValue(), 1.0, "Host bypass did not follow the bypass parameter");
    expectNear(parameterValue(owner, "outputGain"), 0.0, "Changing bypass changed Output");
}

void meterPublicationAndReset() {
    Processor owner;
    setParameter(owner, "level", 0.0f);
    setParameter(owner, "double", 0.0f);
    setParameter(owner, "outputGain", 6.0f);
    owner.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 20; ++block) {
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
    for (int block = 0; block < 20; ++block) {
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
           "An idle plugin control can steal host keyboard focus: " + component.getName());
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        expectPassiveControls(*component.getChildComponent(i));
}

void editorTransportAndTextEntryForSkin(bool neon) {
    Processor owner;
    owner.setFlash(neon);
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
    juce::Slider* output = nullptr;
    for (int i = 0; i < editor->getNumChildComponents(); ++i)
        if (auto* slider = dynamic_cast<juce::Slider*>(editor->getChildComponent(i));
            slider != nullptr && slider->getName() == "Output")
            output = slider;
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
    editorTransportAndTextEntryForSkin(false);
    editorTransportAndTextEntryForSkin(true);
}

juce::Button* findButton(juce::Component& parent, const juce::String& text) {
    for (auto* child : parent.getChildren()) {
        if (auto* button = dynamic_cast<juce::Button*>(child); button && button->getButtonText() == text)
            return button;
        if (auto* found = findButton(*child, text)) return found;
    }
    return nullptr;
}

void checkDrawerControls(Processor& owner, juce::Component& parent, int& sliders, int& buttons) {
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
            auto& p = parameter(owner, id);
            slider->setValue(p.getNormalisableRange().end, juce::sendNotificationSync);
            expectNear(parameterValue(owner, id), p.getNormalisableRange().end, "Drawer slider binding: " + id);
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
        if (auto* button = dynamic_cast<juce::Button*>(child); button && button->getComponentID().isNotEmpty()) {
            ++buttons;
            const auto id = button->getComponentID();
            setParameter(owner, id, 1);
            if (id != "advancedOn") expect(button->getToggleState(), "Voice/Chorale switch did not follow host");
        }
        checkDrawerControls(owner, *child, sliders, buttons);
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

void editorSkin(bool neon) {
    Processor owner;
    owner.setFlash(neon);
    const juce::String skin = neon ? "Neon" : "Device";
    std::unique_ptr<juce::AudioProcessorEditor> editor(owner.createEditor());
    expect(editor != nullptr, skin + " editor creation failed");
    editor->setVisible(true);
    expectPassiveControls(*editor);

    constexpr std::array<const char*, 6> names{"Leveler", "Mix", "Human", "Width", "Formant", "Output"};
    std::array<juce::Slider*, names.size()> knobs{};
    std::vector<juce::Component*> controls;
    MeterPanel* meters = nullptr;
    int sliderCount = 0;
    for (int i = 0; i < editor->getNumChildComponents(); ++i) {
        auto* child = editor->getChildComponent(i);
        if (!child->isVisible() || dynamic_cast<juce::TooltipWindow*>(child) != nullptr)
            continue; // Hidden drawer and transient tooltip do not participate in collapsed layout.
        const auto description = skin + " / " + componentName(*child);
        expect(!child->getBounds().isEmpty(), description + " has empty bounds");
        expect(editor->getLocalBounds().contains(child->getBounds()), description + " lies outside the editor");
        if (auto* slider = dynamic_cast<juce::Slider*>(child)) {
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
            controls.push_back(slider);
        } else if (dynamic_cast<juce::Button*>(child) != nullptr) {
            controls.push_back(child);
        } else if (auto* panel = dynamic_cast<MeterPanel*>(child)) {
            expect(meters == nullptr, skin + " has multiple meter panels");
            meters = panel;
            controls.push_back(panel);
        }
    }
    expect(sliderCount == 6, skin + " must have exactly six direct sliders");
    expect(meters != nullptr, skin + " is missing its MeterPanel child");
    for (size_t i = 0; i < knobs.size(); ++i) {
        expect(knobs[i] != nullptr, skin + " is missing knob " + names[i]);
        expect(knobs[i]->isDoubleClickReturnEnabled(), skin + " knob does not reset on double-click: " + names[i]);
        const auto& target = parameter(owner, vocalstack::knobParameterIds[i]);
        const auto expected = i == 4 || i == 5 ? 0.0f : target.convertFrom0to1(target.getDefaultValue());
        expectNear(knobs[i]->getDoubleClickReturnValue(), expected, skin + " knob double-click default: " + names[i]);
    }
    // Only direct interactive controls participate; slider textboxes and labels are not siblings to test.
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
            button->onClick();
            expectPassiveControls(*editor);
            button->onClick();
            expectPassiveControls(*editor);
        }

    // Keep injection and capture synchronous so the editor's timer cannot replace the synthetic snapshot.
    meters->setMeters(syntheticMeters());
    saveSnapshot(*editor, neon ? "editor-neon.png" : "editor-device.png");
    const int collapsedHeight = editor->getHeight();
    auto* show = findButton(*editor, "Show Advanced");
    expect(show != nullptr, "Missing drawer visibility control");
    show->onClick();
    expect(owner.isAdvancedVisible() && !owner.settings().advanced, "Showing drawer changed activation");
    expect(collapsedHeight == 430 && editor->getWidth() == 940 && editor->getHeight() == 872,
               "Drawer must expand 940x430 to 940x872");
    int advancedSliders = 0, advancedButtons = 0, choraleSliders = 0, choraleButtons = 0;
    for (auto* child : editor->getChildren()) {
        if (dynamic_cast<vocalstack::DoublerPanel*>(child))
            checkDrawerControls(owner, *child, advancedSliders, advancedButtons);
        if (dynamic_cast<vocalstack::ChoralePanel*>(child))
            checkDrawerControls(owner, *child, choraleSliders, choraleButtons);
    }
    expect(advancedSliders == 16 && advancedButtons == 3, "Fresh drawer must contain two complete eight-slider voice cards");
    expect(choraleSliders == 2 && choraleButtons == 1, "Chorale controls missing");
    auto* addVoice = findButton(*editor, "+ Add voice");
    expect(addVoice != nullptr, "Missing Add voice action");
    for (int i = 0; i < 6; ++i) addVoice->onClick();
    expect(!addVoice->isEnabled(), "Add voice action remained enabled at capacity");
    advancedSliders = advancedButtons = 0;
    for (auto* child : editor->getChildren())
        if (dynamic_cast<vocalstack::DoublerPanel*>(child))
            checkDrawerControls(owner, *child, advancedSliders, advancedButtons);
    expect(advancedSliders == 64 && advancedButtons == 9, "Eight complete stable voice cards missing");
    expectPassiveControls(*editor);
    auto* advanced = findButton(*editor, "Voice stack");
    expect(advanced != nullptr, "Missing Advanced activation switch");
    advanced->setToggleState(false, juce::dontSendNotification);
    advanced->onClick();
    advanced->setToggleState(true, juce::dontSendNotification);
    advanced->onClick();
    expect(owner.settings().advanced, "Advanced UI activation failed");
    auto* basic = findButton(*editor, "Reset to Basic");
    expect(basic != nullptr, "Missing Reset to Basic");
    basic->onClick();
    expectBasicVoices(owner, owner.settings().human);
    expect(owner.settings().advanced, "UI Reset to Basic disabled Advanced");
    expectPassiveControls(*editor);
    saveSnapshot(*editor, neon ? "editor-neon-advanced.png" : "editor-device-advanced.png");
    for (int i = 0; i < 6; ++i) addVoice->onClick();
    expectPassiveControls(*editor);
    saveSnapshot(*editor, neon ? "editor-neon-eight-voices.png" : "editor-device-eight-voices.png");
    basic->onClick();
    expectBasicVoices(owner, owner.settings().human);
    show->onClick();
    expect(!owner.isAdvancedVisible() && owner.settings().advanced, "Hiding drawer changed activation");
    expect(editor->getHeight() == collapsedHeight, "Drawer did not collapse editor");
    show->onClick();
    editor.reset();
    editor.reset(owner.createEditor());
    expect(editor->getHeight() == collapsedHeight + 442, "Editor reopen lost drawer visibility");
    expectPassiveControls(*editor);
}

#include "ProcessorUiRegressionTests.h"

void deviceEditor() { editorSkin(false); }
void neonEditor() { editorSkin(true); }
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    struct Test {
        const char* name;
        void (*run)();
    };
    const std::array<Test, 21> tests{{
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
        {"Routing drafts, active fallback and layout state", routingStateRegression},
        {"Fixed FX automation slots", fxAutomationRegression},
        {"Tone FX parameters, recall and rendered automation", vocalstack::toneProcessorTests::run},
        {"Tone FX UI, EQ gestures and multiband cable endpoints", vocalstack::toneUiTests::run},
        {"Voice 1 FX alters only Voice 1 with automatic merging", perVoiceFxSignalPath},
        {"Routing mouse edits, history, focus and both skins", routingUiRegression},
        {"Device editor and PNG", deviceEditor},
        {"Neon editor and PNG", neonEditor}
    }};
    int failures = 0;
    for (const auto& test : tests) {
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
    std::cout << tests.size() - static_cast<size_t>(failures) << '/' << tests.size() << " editor tests passed\n";
    return failures == 0 ? 0 : 1;
}
