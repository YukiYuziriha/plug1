#include "PluginProcessor.h"
#include "MeterPanel.h"

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
        if (child->hasTagName("PARAM") && child->getStringAttribute("id") == "outputGain") {
            xml->removeChildElement(child, true);
            ++removed;
        }
        child = next;
    }
    expect(removed == 1, "Expected exactly one PARAM child with id outputGain in saved settings");

    setParameter(owner, "level", 87.0f);
    setParameter(owner, "outputGain", 6.0f);
    expectNear(parameterValue(owner, "outputGain"), 6.0, "Legacy-state test precondition");
    expect(owner.pasteSettings(xml->toString()), "Legacy settings without Output were rejected");
    expectNear(parameterValue(owner, "outputGain"), 0.0, "Legacy settings retained the previous Output gain");
    expectNear(parameterValue(owner, "level"), 23.0, "Legacy settings did not restore an existing parameter");
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

void editorTransportAndTextEntry() {
    Processor owner;
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

juce::String componentName(juce::Component& component) {
    if (auto* button = dynamic_cast<juce::Button*>(&component))
        return button->getButtonText();
    if (auto* label = dynamic_cast<juce::Label*>(&component))
        return "Label " + label->getText();
    return component.getName().isNotEmpty() ? component.getName() : juce::String("unnamed child");
}

void saveSnapshot(juce::AudioProcessorEditor& editor, const juce::String& filename) {
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
        if (dynamic_cast<juce::TooltipWindow*>(child) != nullptr)
            continue; // JUCE's transient tooltip has no layout bounds until shown.
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
}

void deviceEditor() { editorSkin(false); }
void neonEditor() { editorSkin(true); }
}

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    struct Test {
        const char* name;
        void (*run)();
    };
    const std::array<Test, 9> tests{{
        {"Output parameter contract", outputParameterContract},
        {"Output save/copy/paste/reset", outputStateRoundTrips},
        {"Legacy state Output fallback", legacyStateDefaults},
        {"Named bypass binding", bypassBinding},
        {"Audio/UI meter publication and clear", meterPublicationAndReset},
        {"Meter click reset and host shortcuts", meterTransportKeys},
        {"Editor host shortcuts and numeric entry", editorTransportAndTextEntry},
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
