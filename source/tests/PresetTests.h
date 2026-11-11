#pragma once

#include "PresetManager.h"
#include <stdexcept>

// Integration: include this header in the existing JUCE editor/processor test
// executable and call vocalstack::presetTests::run() after ScopedJuceInitialiser_GUI.
// No static registration, main(), build-system edits, or production preference I/O.
// Uses a unique temp directory and cleans it up. Must run on the message thread.
namespace vocalstack::presetTests {

inline void require(bool ok, const juce::String& message) {
    if (!ok) throw std::runtime_error(message.toStdString());
}
inline void success(const juce::Result& result) { require(result.wasOk(), result.getErrorMessage()); }
inline void write(Processor& p, const juce::String& id, float value) {
    auto* parameter = p.parameters.getParameter(id);
    require(parameter != nullptr, "Missing test parameter: " + id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
inline float read(Processor& p, const juce::String& id) {
    const auto* value = p.parameters.getRawParameterValue(id);
    require(value != nullptr, "Missing raw test parameter: " + id);
    return value->load();
}
inline juce::ValueTree document(const juce::String& text) {
    const auto xml = juce::parseXML(text);
    require(xml != nullptr, "Test XML did not parse");
    return juce::ValueTree::fromXml(*xml);
}
struct TemporaryFolder {
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("VocalStackPresetTests-" + juce::Uuid().toString());
    TemporaryFolder() { success(root.createDirectory()); }
    ~TemporaryFolder() { root.deleteRecursively(); }
    juce::File preferences() const { return root.getChildFile("local.settings"); }
};

inline void roundTripAndUndo() {
    TemporaryFolder files;
    std::unique_ptr<Processor> pStorage = std::make_unique<Processor>();
    Processor& p = *pStorage;
    PresetManager manager(p, files.preferences());
    success(manager.setFolder(files.root));
    p.setFlash(false);
    auto layout = p.getRoutingLayout();
    layout.setProperty("editorWidth", 1111, nullptr);
    layout.setProperty("editorHeight", 777, nullptr);
    p.setRoutingLayout(layout);
    write(p, "voice1Pitch", -12); write(p, "outputGain", -7);
    Graph graph{{{1, NodeType::lead}, {100, NodeType::waveshaper}, {13, NodeType::output}},
                {{1, 100, 0, 0}, {100, 13, 0, 0}}};
    juce::String error;
    require(p.setRoutingGraph(graph, error), error);
    write(p, "fx1WaveDrive", 11);
    ModulationState mods;
    Modulator lfo; lfo.id = 17; lfo.rateHz = .4f;
    mods.modulators.push_back(lfo);
    mods.assignments.push_back({17, "outputGain", .15f, true});
    require(p.setModulationState(mods, error), error);
    const auto saved = manager.captureText("Round trip");
    const auto savedDocument = document(saved);
    const auto savedPatch = savedDocument.getChild(0);
    require(!savedPatch.hasProperty("skin") && !savedPatch.hasProperty("flash") && !savedPatch.hasProperty("advancedVisible"), "Preset leaked skin/drawer preferences");
    require(!savedPatch.getChildWithName("RoutingLayout").hasProperty("editorWidth"), "Preset leaked window size");
    require(!saved.contains(files.root.getFullPathName()), "Preset leaked local folder");

    p.resetParameters();
    write(p, "outputGain", -2); write(p, "voice1Pitch", 5);
    p.setSkin(2);
    p.setAdvancedVisible(true);
    layout = p.getRoutingLayout();
    layout.setProperty("editorWidth", 1333, nullptr); layout.setProperty("editorHeight", 888, nullptr);
    p.setRoutingLayout(layout);
    const auto before = manager.captureText("Before");
    success(manager.loadText(saved));
    require(manager.canUndoLoad(), "Successful load did not arm Undo load");
    require(std::abs(read(p, "outputGain") + 7) < .001f && std::abs(read(p, "voice1Pitch") + 12) < .001f, "Preset lost parameter/voice state");
    require(std::abs(read(p, "fx1WaveDrive") - 11) < .001f, "Preset lost FX state");
    require(p.getRoutingGraph().nodes.size() == 3 && p.getRoutingGraph().nodes[1].id == 100, "Preset lost routing graph");
    require(p.getModulationState().assignments.size() == 1 && p.getModulationState().modulators[0].id == 17, "Preset lost modulation");
    require(p.getSkin() == 2 && p.isAdvancedVisible(), "Preset changed current presentation preferences");
    require(int(p.getRoutingLayout()["editorWidth"]) == 1333 && int(p.getRoutingLayout()["editorHeight"]) == 888, "Preset changed window size");
    success(manager.undoLoad());
    require(!manager.canUndoLoad(), "One-step undo was not consumed");
    require(manager.captureText("Before") == before, "Undo did not restore the entire prior patch");
    require(manager.undoLoad().failed(), "Empty undo unexpectedly succeeded");
}

inline void rejectWithoutMutation() {
    TemporaryFolder files;
    std::unique_ptr<Processor> pStorage = std::make_unique<Processor>();
    Processor& p = *pStorage;
    PresetManager manager(p, files.preferences());
    const auto text = manager.captureText("Valid");
    success(manager.loadText(text));
    write(p, "outputGain", -9);
    const auto before = p.settingsText();
    const auto reject = [&](const juce::String& invalid) {
        require(manager.loadText(invalid).failed(), "Invalid preset was accepted");
        require(p.settingsText() == before, "Rejected preset changed live state");
        require(manager.canUndoLoad(), "Rejected preset destroyed previous undo checkpoint");
    };
    reject("<VocalStack/>"); reject("not XML");
    reject(text + "<extra/>"); reject(text + "trailing junk");
    reject("<!DOCTYPE VocalStackPreset><VocalStackPreset/>");
    reject(juce::String::repeatedString("x", PresetManager::maxPresetBytes + 1));
    reject(juce::String::repeatedString("<a>", 20) + juce::String::repeatedString("</a>", 20));
    auto invalid = document(text);
    invalid.setProperty("schemaVersion", 2, nullptr); reject(invalid.toXmlString());
    invalid.setProperty("schemaVersion", "1junk", nullptr); reject(invalid.toXmlString());
    invalid = document(text);
    auto patch = invalid.getChild(0);
    auto value = patch.getChildWithProperty("id", "outputGain");
    value.setProperty("value", "nan", nullptr); reject(invalid.toXmlString());
    value.setProperty("value", "1junk", nullptr); reject(invalid.toXmlString());
    value.setProperty("value", 10000, nullptr); reject(invalid.toXmlString());
    value.setProperty("value", -3, nullptr);
    patch.addChild(value.createCopy(), -1, nullptr); reject(invalid.toXmlString());
    patch.removeChild(patch.getNumChildren() - 1, nullptr);
    patch.removeChild(value, nullptr); reject(invalid.toXmlString());
    invalid = document(text); patch = invalid.getChild(0);
    patch.getChildWithName("RoutingGraph").getChildWithProperty("id", 1).setProperty("type", 99, nullptr);
    reject(invalid.toXmlString());
    invalid = document(text); patch = invalid.getChild(0);
    // Cycle in a connected graph must not trigger processor's default fallback.
    auto graph = patch.getChildWithName("RoutingGraph");
    juce::ValueTree link("Link"); link.setProperty("source", 13, nullptr); link.setProperty("destination", 1, nullptr);
    graph.addChild(link, -1, nullptr); reject(invalid.toXmlString());
    invalid = document(text); patch = invalid.getChild(0);
    juce::ValueTree mod("Modulator"); mod.setProperty("id", 1, nullptr); mod.setProperty("rateHz", -1, nullptr);
    patch.getChildWithName("Modulation").addChild(mod, -1, nullptr); reject(invalid.toXmlString());
    success(manager.undoLoad());
    require(std::abs(read(p, "outputGain") - float(document(text).getChild(0).getChildWithProperty("id", "outputGain")["value"])) < .001f,
            "Rejected load changed undo target");
}

inline void filesAndLocalPreferences() {
    TemporaryFolder files;
    const auto userFolder = files.root.getChildFile("presets");
    success(userFolder.createDirectory());
    std::unique_ptr<Processor> pStorage = std::make_unique<Processor>();
    Processor& p = *pStorage;
    PresetManager manager(p, files.preferences());
    std::vector<PresetManager::Entry> entries;
    require(manager.refresh(entries).failed() && entries.size() == 8, "Missing folder should surface choice while keeping factories");
    const auto file = userFolder.getChildFile("Saved.vocalstackpreset");
    require(manager.saveNew(file).failed(), "Saved without an explicit folder choice");
    success(manager.setFolder(userFolder));
    write(p, "outputGain", -6);
    success(manager.saveNew(file));
    const auto bytes = file.loadFileAsString();
    write(p, "outputGain", -1);
    require(manager.saveNew(file).failed() && file.loadFileAsString() == bytes, "Existing file was overwritten");
    require(manager.saveNew(userFolder.getChildFile("wrong.xml")).failed(), "Wrong extension accepted");
    require(manager.saveNew(files.root.getChildFile("outside.vocalstackpreset")).failed(), "Saved outside selected folder");
    success(manager.refresh(entries));
    require(entries.size() == 9 && entries.back().name == "Saved", "User browser refresh failed");
    success(manager.load(entries.back()));
    require(std::abs(read(p, "outputGain") + 6) < .001f, "File load failed");
    {
        PresetManager reopened(p, files.preferences());
        require(reopened.getFolder() == userFolder, "Chosen folder was not persisted immediately");
    }
    const auto hostState = p.settingsText();
    require(!hostState.contains(userFolder.getFullPathName()), "Folder entered host state");
    const auto bad = userFolder.getChildFile("Bad.vocalstackpreset");
    require(bad.replaceWithText("garbage"), "Could not write invalid test fixture");
    require(manager.load({"Bad", bad, -1}).failed(), "Invalid user file accepted");
    require(manager.load({"Missing", userFolder.getChildFile("Missing.vocalstackpreset"), -1}).failed(), "Missing file accepted");
    require(manager.setFolder(userFolder.getChildFile("not-a-folder")).failed(), "Invalid folder accepted");
    require(manager.getFolder() == userFolder, "Failed folder choice changed current folder");
}

inline void factories() {
    TemporaryFolder files;
    std::unique_ptr<Processor> pStorage = std::make_unique<Processor>();
    Processor& p = *pStorage;
    PresetManager manager(p, files.preferences());
    require(PresetManager::factoryNames().size() == 8, "Expected eight factory patches");
    require(p.parameters.getParameter("fx1delay_time") != nullptr, "Creative factory parameters are not integrated");
    for (int i = 0; i < PresetManager::factoryNames().size(); ++i) {
        juce::ValueTree clean, dirty;
        success(manager.factoryPatch(i, clean));
        for (auto* base : p.getParameters())
            if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(base)) param->setValueNotifyingHost(.81f);
        success(manager.factoryPatch(i, dirty));
        require(clean.toXmlString() == dirty.toXmlString(), "Factory inherited live patch values");
        for (auto* base : p.getParameters()) if (auto* param = dynamic_cast<juce::RangedAudioParameter*>(base))
            require(clean.getChildWithProperty("id", param->paramID).isValid(), "Factory is incomplete: " + param->paramID);
        const auto mods = clean.getChildWithName("Modulation");
        require(mods.isValid() && mods.getNumChildren() == 0, "Factory inherited modulation");
        success(manager.load({PresetManager::factoryNames()[i], {}, i}));
    }
    juce::ValueTree ignored;
    require(manager.factoryPatch(-1, ignored).failed() && manager.factoryPatch(8, ignored).failed(), "Invalid factory index accepted");
}

inline void toolbarFocusPolicy() {
    TemporaryFolder files;
    std::unique_ptr<Processor> pStorage = std::make_unique<Processor>();
    Processor& p = *pStorage;
    PresetBar bar(p, files.preferences());
    bar.setSize(904, 48);
    require(!bar.getWantsKeyboardFocus(), "Preset bar grabs keyboard focus");
    for (int i = 0; i < bar.getNumChildComponents(); ++i) {
        const auto* child = bar.getChildComponent(i);
        if (dynamic_cast<const juce::Button*>(child) || dynamic_cast<const juce::ComboBox*>(child)) {
            require(!child->getWantsKeyboardFocus() && !child->getMouseClickGrabsKeyboardFocus(), "Preset toolbar control grabs host shortcuts");
            require(child->getWidth() > 0 && child->getHeight() > 0, "Preset toolbar control has empty bounds");
        }
    }
}

inline void run() {
    roundTripAndUndo();
    rejectWithoutMutation();
    filesAndLocalPreferences();
    factories();
    toolbarFocusPolicy();
}

} // namespace vocalstack::presetTests
