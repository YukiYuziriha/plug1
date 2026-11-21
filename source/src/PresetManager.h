#include "VisualTheme.h"
#pragma once

#include "PluginProcessor.h"
#include "ProcessorRoutingState.h"
#include "CreativeEffects.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <filesystem>
#include <cmath>
#include <set>
#include <cstdlib>
#include <string_view>
#include <cstring>
#include <cctype>

namespace vocalstack {

// Message/control thread only; never call from processBlock. Header-only integration:
// Editor owns PresetBar presets{owner}; addAndMakeVisible(presets), allocate a NEW
// preferredHeight row (recommended width >= 620), and optionally handle onPatchLoaded to sync
// routing/voice panels. Existing 940px editor has no unused full-width toolbar row.
// Nothing here changes programs, host-state format, or machine preferences in APVTS.
class PresetManager {
public:
    static constexpr int maxPresetBytes = 2 * 1024 * 1024;
    static constexpr int maxUserEntries = 512;
    static constexpr const char* extension = ".vocalstackpreset";
    struct Entry {
        juce::String name;
        juce::File file;
        int factory = -1;
        bool isFactory() const noexcept { return factory >= 0; }
    };

    // Explicit preferences file permits isolated tests; empty uses user app data.
    explicit PresetManager(Processor& p, juce::File preferencesFile = {}) : processor(p) {
        if (preferencesFile == juce::File())
            preferencesFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("Vocal Stack").getChildFile("PresetBrowser.settings");
        juce::PropertiesFile::Options options;
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.millisecondsBeforeSaving = -1;
        if (!preferencesFile.existsAsFile() || preferencesFile.getSize() <= 65536) {
            preferences = std::make_unique<juce::PropertiesFile>(preferencesFile, options);
            const auto path = preferences->getValue("presetFolder");
            if (path.isNotEmpty() && juce::File::isAbsolutePath(path)) folder = juce::File(path);
        }
    }

    juce::File getFolder() const { return folder; }
    bool canUndoLoad() const noexcept { return undoPatch.isValid(); }
    static juce::StringArray factoryNames() {
        return {"Dark Lead", "Grit Adlibs", "Deep Double", "Night Echo", "Cloud Choir",
                    "Clean Vocal Utility", "Vocal Filter Motion", "Shimmer Bloom", "Synthetic Church", "Warm Plate"};
    }

    juce::Result setFolder(const juce::File& chosen) {
        if (!chosen.isDirectory()) return fail("Choose an existing preset folder.");
        if (!preferences) return fail("Preset preferences file is too large; folder was not saved.");
        const auto old = preferences->getValue("presetFolder");
        preferences->setValue("presetFolder", chosen.getFullPathName());
        if (!preferences->saveIfNeeded()) {
            preferences->setValue("presetFolder", old);
            return fail("Could not persist the preset folder in user app data. Check permissions.");
        }
        folder = chosen;
        return juce::Result::ok();
    }

    juce::Result refresh(std::vector<Entry>& entries) const {
        entries.clear();
        const auto names = factoryNames();
        for (int i = 0; i < names.size(); ++i) entries.push_back({names[i], {}, i});
        if (folder == juce::File()) return fail("Choose Folder to enable user presets and Save.");
        if (!folder.isDirectory()) return fail("Preset folder is unavailable. Choose Folder to reconnect it.");
        std::vector<Entry> users;
        for (const auto& item : juce::RangedDirectoryIterator(folder, false, "*" + juce::String(extension), juce::File::findFiles)) {
            if (users.size() == maxUserEntries) return fail("More than 512 user presets; use a smaller folder.");
            const auto file = item.getFile();
            users.push_back({file.getFileNameWithoutExtension(), file, -1});
        }
        std::sort(users.begin(), users.end(), [](const Entry& a, const Entry& b) {
            return a.name.compareNatural(b.name) < 0;
        });
        entries.insert(entries.end(), users.begin(), users.end());
        return juce::Result::ok();
    }

    juce::Result load(const Entry& entry) {
        if (entry.isFactory()) {
            juce::ValueTree patch;
            auto result = factoryPatch(entry.factory, patch);
            return result.failed() ? result : loadPatch(patch);
        }
        if (!entry.file.existsAsFile()) return fail("Preset file no longer exists. Refresh the browser.");
        const auto size = entry.file.getSize();
        if (size <= 0 || size > maxPresetBytes) return fail("Preset must be between 1 byte and 2 MiB.");
        // Bounded read even if another process grows the file after getSize().
        juce::FileInputStream stream(entry.file);
        if (!stream.openedOk()) return fail("Could not open preset for reading.");
        juce::MemoryBlock bytes;
        stream.readIntoMemoryBlock(bytes, maxPresetBytes + 1);
        if (bytes.getSize() > maxPresetBytes) return fail("Preset exceeds 2 MiB.");
        if (std::memchr(bytes.getData(), 0, bytes.getSize()) != nullptr
            || !juce::CharPointer_UTF8::isValidString(static_cast<const char*>(bytes.getData()), int(bytes.getSize())))
            return fail("Preset must be UTF-8 XML without embedded NUL bytes.");
        return loadText(juce::String::fromUTF8(static_cast<const char*>(bytes.getData()), int(bytes.getSize())));
    }

    juce::String captureText(const juce::String& name) {
        auto patch = currentPatch();
        stripPreferences(patch);
        juce::ValueTree document("VocalStackPreset");
        document.setProperty("schemaVersion", 1, nullptr);
        document.setProperty("name", name.substring(0, 128), nullptr);
        document.addChild(patch, -1, nullptr);
        return document.toXmlString();
    }

    juce::Result loadText(const juce::String& text) {
        auto result = boundedXml(text);
        if (result.failed()) return result;
        const auto xml = juce::parseXML(text);
        if (!xml) return fail("Preset is not valid XML.");
        const auto document = juce::ValueTree::fromXml(*xml);
        if (!document.hasType("VocalStackPreset") || document.getProperty("schemaVersion").toString() != "1"
            || document.getNumChildren() != 1 || document.getProperty("name").toString().trim().isEmpty()
            || document.getProperty("name").toString().length() > 128)
            return fail("Unsupported preset schema (expected VocalStackPreset version 1).");
        return loadPatch(document.getChild(0));
    }

    // Deliberately never replaces an existing file, even after a save-dialog
    // overwrite confirmation. A same-directory hard-link commit is atomic and
    // fails if the destination appeared concurrently. Unsupported filesystems
    // report an error instead of falling back to a potentially destructive copy.
    juce::Result saveNew(const juce::File& file) {
        if (!folder.isDirectory()) return fail("Choose an available preset folder before saving.");
        if (file.getParentDirectory() != folder) return fail("Save inside the selected preset folder (or choose a new Folder first).");
        if (!file.hasFileExtension(extension)) return fail("Use the .vocalstackpreset extension.");
        if (file.exists()) return fail("A preset with that name already exists. Choose a different name; nothing was overwritten.");
        const auto name = file.getFileNameWithoutExtension().trim();
        if (name.isEmpty() || name.length() > 128) return fail("Use a preset name of 1–128 characters.");
        const auto text = captureText(name);
        auto patch = currentPatch();
        auto result = validatePatch(patch);
        if (result.failed()) return result;
        result = boundedXml(text);
        if (result.failed()) return result;
        const auto temporary = folder.getNonexistentChildFile(".vocalstack-writing-" + juce::Uuid().toString(), ".tmp", false);
        struct Cleanup { juce::File file; ~Cleanup() { file.deleteFile(); } } cleanup{temporary};
        {
            juce::FileOutputStream out(temporary);
            if (!out.openedOk() || !out.writeText(text, false, false, "\n")) return fail("Could not write preset. Check folder permissions and free space.");
            out.flush();
            if (out.getStatus().failed()) return fail("Could not flush preset to disk.");
        }
        std::error_code error;
        std::filesystem::create_hard_link(nativePath(temporary), nativePath(file), error);
        if (error) return fail("Preset not saved (existing name, permissions, or filesystem lacks atomic hard links): " + juce::String(error.message()));
        return juce::Result::ok();
    }

    juce::Result undoLoad() {
        if (!undoPatch.isValid()) return fail("No preset load to undo.");
        auto patch = undoPatch.createCopy();
        retainPreferences(patch, currentPatch());
        const auto result = validatePatch(patch);
        if (result.failed()) return result;
        const auto current = currentPatch();
        if (!processor.pasteSettings(patch.toXmlString()) || processor.getRoutingError().isNotEmpty()) {
            if (!processor.pasteSettings(current.toXmlString()) || processor.getRoutingError().isNotEmpty())
                return fail("Undo load and rollback failed. Restore the host session state.");
            return fail("Could not restore the patch from before the last preset load; current patch was retained.");
        }
        undoPatch = {};
        return juce::Result::ok();
    }

    // Complete deterministic patches: reset EVERY installed host parameter to its
    // declared default, replace graph/layout/modulation, then author overrides.
    // Descriptor IDs and raw units are shared with the host parameter bridge.
    juce::Result factoryPatch(int index, juce::ValueTree& patch) {
        if (index < 0 || index >= factoryNames().size()) return fail("Unknown factory preset.");
        patch = currentPatch();
        for (int i = patch.getNumChildren(); --i >= 0;)
            patch.removeChild(i, nullptr);
        for (auto* base : processor.getParameters()) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(base);
            if (p) setRaw(patch, p->paramID, p->convertFrom0to1(p->getDefaultValue()));
        }
        stripPreferences(patch);
        patch.removeProperty("fxSlotLineage", nullptr);
        for (int slot = 1; slot <= 15; ++slot)
            patch.removeProperty(juce::Identifier("fxSlotOwner" + juce::String(slot)), nullptr);
        patch.setProperty("advancedInitialized", true, nullptr);
        patch.addChild(juce::ValueTree("Modulation"), -1, nullptr);
        patch.addChild(juce::ValueTree("RoutingLayout"), -1, nullptr);
        for (int slot = 1; slot <= 15; ++slot)
            for (const auto& d : creativeParameters)
                setRaw(patch, "fx" + juce::String(slot) + d.id, d.initial);
        setRaw(patch, "bypass", 0); setRaw(patch, "outputGain", -3);
        setRaw(patch, "levelOn", 1); setRaw(patch, "level", 35);
        setRaw(patch, "doubleOn", 1); setRaw(patch, "advancedOn", 1);
        setRaw(patch, "globalMix", 100); setRaw(patch, "choraleOn", 0);
        for (int v = 1; v <= 8; ++v) {
            setRaw(patch, "voice" + juce::String(v) + "Present", v <= 2 ? 1 : 0);
            setRaw(patch, "voice" + juce::String(v) + "On", v <= 2 ? 1 : 0);
        }
        const float blends[]{12, 30, 42, 18, 46, 10, 18, 35, 12, 8};
        setRaw(patch, "double", blends[index]);
        for (int v = 1; v <= 2; ++v) {
            const auto prefix = "voice" + juce::String(v);
            setRaw(patch, prefix + "Gain", index == 2 ? -7 : -12);
            setRaw(patch, prefix + "Pan", v == 1 ? -.65f : .65f);
            setRaw(patch, prefix + "Delay", v == 1 ? 17 : 29);
            setRaw(patch, prefix + "Detune", v == 1 ? -6 : 7);
            setRaw(patch, prefix + "Pitch", index == 2 ? -12 : (index == 4 ? (v == 1 ? 7 : 12) : 0));
            setRaw(patch, prefix + "Formant", index == 2 ? -3 : (index == 4 ? -1 : 0));
        }
        // Start with the serialized default graph, remove legacy Chorale, and
        // insert a post-stack chain. Graph uses normalized mix, PARAM uses percent.
        auto graph = processorRouting::encode(defaultGraph(), "RoutingGraph");
        graph.removeChild(graph.getChildWithProperty("id", 12), nullptr);
        for (int i = graph.getNumChildren(); --i >= 0;) {
            auto child = graph.getChild(i);
            if (child.hasType("Link") && (int(child["source"]) == 12 || int(child["destination"]) == 12)) graph.removeChild(i, nullptr);
        }
        int previous = 11, slot = 0;
        const auto effect = [&](int type, float mix) {
            ++slot;
            const int id = 99 + slot;
            juce::ValueTree node("Node");
            node.setProperty("id", id, nullptr); node.setProperty("type", type, nullptr);
            node.setProperty("enabled", true, nullptr); node.setProperty("mix", mix * .01f, nullptr);
            graph.addChild(node, -1, nullptr);
            addLink(graph, previous, id); previous = id;
            setRaw(patch, "fx" + juce::String(slot) + "On", 1);
            setRaw(patch, "fx" + juce::String(slot) + "Mix", mix);
            return "fx" + juce::String(slot);
        };
        if (index == 5) {
            const auto gate = effect(int(NodeType::gate), 100);
            setRaw(patch, gate + "gate_threshold", -48);
            setRaw(patch, gate + "gate_range", 18);
            setRaw(patch, gate + "gate_ratio", 3);
            setRaw(patch, gate + "gate_release", 180);
            const auto stereo = effect(int(NodeType::stereo), 100);
            setRaw(patch, stereo + "stereo_width", 115);
            const auto trim = effect(int(NodeType::gain), 100);
            setRaw(patch, trim + "gain_db", -2);
        }
        if (index == 6) {
            const auto xp = effect(int(NodeType::xpFilter), 100);
            setRaw(patch, xp + "xp_mode", 6);
            setRaw(patch, xp + "xp_cutoff", 180);
            setRaw(patch, xp + "xp_resonance", 15);
            const auto synth = effect(int(NodeType::synthFilter), 22);
            setRaw(patch, synth + "synth_mode", 6);
            setRaw(patch, synth + "synth_cutoff", 950);
            setRaw(patch, synth + "synth_resonance", 25);
        }
        if (index >= 8) {
            setRaw(patch, "outputGain", -6);
            if (index == 9) {
                const auto sat = effect(int(NodeType::saturator), 100);
                setRaw(patch, sat + "saturator_drive", 9);
                setRaw(patch, sat + "saturator_character", 55);
                setRaw(patch, sat + "saturator_output", -9);
                setRaw(patch, sat + "saturator_tone", -2);
            }
            const auto conv = effect(int(NodeType::convolution), index == 8 ? 18 : 15);
            setRaw(patch, conv + "convolution_space", index == 8 ? 0 : 3);
            setRaw(patch, conv + "convolution_duck", 6);
            setRaw(patch, conv + "convolution_brightness", index == 8 ? -2 : 1);
        } else {
        auto compressor = effect(11, 100);
        setRaw(patch, compressor + "compressor_threshold", -22);
        setRaw(patch, compressor + "compressor_ratio", index == 1 ? 6 : 3);
        setRaw(patch, compressor + "compressor_makeup", index == 1 ? 5 : 3);
        if (index == 1) {
            const auto crusher = effect(12, 22);
            setRaw(patch, crusher + "crusher_bits", 9);
            setRaw(patch, crusher + "crusher_rate", 12000);
        }
        if (index == 4) {
            const auto grain = effect(13, 20);
            setRaw(patch, grain + "granular_pitch", 7);
            setRaw(patch, grain + "granular_spread", 85);
            setRaw(patch, grain + "granular_highcut", 4400);
        }
        const auto delay = effect(9, index == 3 ? 32 : 13);
        setRaw(patch, delay + "delay_time", index == 3 ? 440 : 270);
        setRaw(patch, delay + "delay_feedback", index == 3 ? 48 : 24);
        setRaw(patch, delay + "delay_highcut", 3600);
        setRaw(patch, delay + "delay_duck", 10);
        const auto reverb = effect(10, index == 4 ? 30 : 12);
        setRaw(patch, reverb + "reverb_decay", index == 4 ? 5.5f : 1.8f);
        setRaw(patch, reverb + "reverb_highcut", 4800);
        setRaw(patch, reverb + "reverb_duck", 9);
        if (index == 7) {
            setRaw(patch, "outputGain", -6);
            setRaw(patch, reverb + "Mix", 32);
            graph.getChildWithProperty("id", 99 + slot).setProperty("mix", .32f, nullptr);
            setRaw(patch, reverb + "reverb_decay", 5);
            setRaw(patch, reverb + "reverb_pitch", 12);
            setRaw(patch, reverb + "reverb_pitch_amount", 38);
            setRaw(patch, reverb + "reverb_swell", 450);
        }
        }
        addLink(graph, previous, 13);
        patch.addChild(graph, -1, nullptr);
        auto active = graph.createCopy();
        // ValueTree type is immutable: construct the alternate wrapper explicitly.
        juce::ValueTree activeGraph("ActiveRoutingGraph");
        for (auto child : active) activeGraph.addChild(child.createCopy(), -1, nullptr);
        patch.addChild(activeGraph, -1, nullptr);
        auto layout = patch.getChildWithName("RoutingLayout");
        const auto position = [&](int id, int x, int y) {
            juce::ValueTree saved("NODE");
            saved.setProperty("id", id, nullptr); saved.setProperty("x", x, nullptr); saved.setProperty("y", y, nullptr);
            layout.addChild(saved, -1, nullptr);
        };
        position(1, 20, 20);
        for (int v = 0; v < 8; ++v) position(v + 2, 20, 110 + v * 85);
        position(11, 230, 110);
        for (int s = 1; s <= slot; ++s) position(99 + s, 440, 20 + (s - 1) * 95);
        position(13, 650, 20 + (slot - 1) * 95);
        return juce::Result::ok();
    }

private:
    Processor& processor;
    std::unique_ptr<juce::PropertiesFile> preferences;
    juce::File folder;
    juce::ValueTree undoPatch;
    static juce::Result fail(const juce::String& text) { return juce::Result::fail(text); }
    static std::filesystem::path nativePath(const juce::File& file) {
        return std::filesystem::u8path(file.getFullPathName().toStdString());
    }
    juce::ValueTree currentPatch() {
        const auto xml = juce::parseXML(processor.settingsText());
        return xml ? juce::ValueTree::fromXml(*xml) : juce::ValueTree();
    }
    static void setRaw(juce::ValueTree& patch, const juce::String& id, float value) {
        auto param = patch.getChildWithProperty("id", id);
        if (!param.isValid()) {
            param = juce::ValueTree("PARAM"); param.setProperty("id", id, nullptr);
            patch.addChild(param, -1, nullptr);
        }
        param.setProperty("value", value, nullptr);
    }
    static void addLink(juce::ValueTree& graph, int source, int destination) {
        juce::ValueTree link("Link");
        link.setProperty("source", source, nullptr); link.setProperty("destination", destination, nullptr);
        link.setProperty("inputPin", 0, nullptr); link.setProperty("sourcePin", 0, nullptr);
        graph.addChild(link, -1, nullptr);
    }
    static void stripPreferences(juce::ValueTree& patch) {
        patch.removeProperty("skin", nullptr);
        patch.removeProperty("motionPercent", nullptr);
        patch.removeProperty("flash", nullptr); patch.removeProperty("advancedVisible", nullptr);
        auto layout = patch.getChildWithName("RoutingLayout");
        // Root layout properties are presentation preferences; node positions live
        // in children and remain part of the patch.
        for (int i = layout.getNumProperties(); --i >= 0;) layout.removeProperty(layout.getPropertyName(i), nullptr);
    }
    static void retainPreferences(juce::ValueTree& patch, const juce::ValueTree& live) {
        stripPreferences(patch);
        for (const auto* key : {"skin", "flash", "advancedVisible", "motionPercent"})
            if (live.hasProperty(key)) patch.setProperty(key, live[key], nullptr);
        auto layout = patch.getChildWithName("RoutingLayout");
        if (!layout.isValid()) { layout = juce::ValueTree("RoutingLayout"); patch.addChild(layout, -1, nullptr); }
        const auto current = live.getChildWithName("RoutingLayout");
        for (int i = 0; i < current.getNumProperties(); ++i) {
            const auto key = current.getPropertyName(i); layout.setProperty(key, current[key], nullptr);
        }
    }
    static juce::Result boundedXml(const juce::String& text) {
        if (text.isEmpty() || text.getNumBytesAsUTF8() > maxPresetBytes) return fail("Preset XML is empty or exceeds 2 MiB.");
        // Bound nesting BEFORE JUCE's recursive parser; reject DTDs/entities and
        // comments rather than trying to interpret untrusted declarations.
        const std::string_view bytes(text.toRawUTF8(), text.getNumBytesAsUTF8());
        int depth = 0, tags = 0, roots = 0;
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] != '<') {
                if (depth == 0 && !std::isspace(static_cast<unsigned char>(bytes[i]))) return fail("Unexpected text outside preset XML root.");
                continue;
            }
            if (i + 1 == bytes.size()) return fail("Unterminated preset XML tag.");
            if (++tags > 12000 || bytes[i + 1] == '!') return fail("Preset XML declarations or excessive elements are not supported.");
            const auto end = bytes.find('>', i);
            if (end == std::string_view::npos) return fail("Unterminated preset XML tag.");
            if (bytes[i + 1] == '/') --depth;
            else if (bytes[i + 1] != '?') {
                if (depth == 0 && ++roots != 1) return fail("Preset XML must have exactly one root.");
                if (bytes[end - 1] != '/') ++depth;
            }
            if (depth < 0 || depth > 16) return fail("Preset XML nesting exceeds schema limits.");
            i = end;
        }
        return depth == 0 && roots == 1 ? juce::Result::ok() : fail("Unbalanced preset XML.");
    }
    juce::Result validatePatch(const juce::ValueTree& patch) {
        if (!patch.hasType("VocalStack")) return fail("Preset does not contain a VocalStack patch.");
        auto bounded = boundedXml(patch.toXmlString());
        if (bounded.failed()) return bounded;
        std::set<juce::String> ids, sections;
        for (const auto& child : patch) {
            if (child.hasType("PARAM")) {
                const auto id = child.getProperty("id").toString();
                auto* p = processor.parameters.getParameter(id);
                if (!p) return fail("Preset requires an unavailable host parameter: " + id + ". Integrate the matching effects first.");
                if (!ids.insert(id).second || !child.hasProperty("value") || child.getNumChildren() != 0) return fail("Duplicate or malformed PARAM: " + id);
                const auto number = child["value"].toString().trim().toStdString();
                char* end = nullptr;
                const double value = std::strtod(number.c_str(), &end);
                const auto& range = p->getNormalisableRange();
                if (number.empty() || end != number.c_str() + number.size() || !std::isfinite(value)
                    || value < double(range.start) - 0.0001 || value > double(range.end) + 0.0001)
                    return fail("Non-finite or out-of-range preset parameter: " + id);
            } else {
                const auto type = child.getType().toString();
                if (type != "RoutingGraph" && type != "ActiveRoutingGraph" && type != "RoutingLayout" && type != "Modulation") return fail("Unknown preset section: " + type);
                if (!sections.insert(type).second) return fail("Duplicate preset section: " + type);
            }
        }
        for (auto* base : processor.getParameters())
            if (const auto* p = dynamic_cast<juce::RangedAudioParameter*>(base))
                if (ids.count(p->paramID) == 0) return fail("Incomplete preset: missing parameter " + p->paramID);
        if (sections.size() != 4) return fail("Incomplete preset: both graphs, layout and modulation are required.");
        for (const auto* type : {"RoutingGraph", "ActiveRoutingGraph"}) {
            Graph graph;
            if (!processorRouting::decode(patch.getChildWithName(type), graph)) return fail("Malformed or unsupported routing graph. Integrate the matching effect types first.");
            const auto valid = processorRouting::validate(graph);
            if (!valid) return fail("Invalid preset routing: " + juce::String(valid.message));
        }
        // The processor owns modulation validation. Preflight on an unprepared,
        // isolated processor because pasteSettings mutates modulation BEFORE graph
        // restore. Never test an untrusted patch against the live instance.
        auto probe = std::make_unique<Processor>();
        if (!probe->pasteSettings(patch.toXmlString())) return fail("Preset modulation or state was rejected by the processor.");
        if (probe->getRoutingError().isNotEmpty()) return fail("Preset routing was rejected: " + probe->getRoutingError());
        return juce::Result::ok();
    }
    juce::Result loadPatch(const juce::ValueTree& source) {
        auto patch = source.createCopy();
        auto before = currentPatch();
        retainPreferences(patch, before);
        const auto result = validatePatch(patch);
        if (result.failed()) return result;
        if (!processor.pasteSettings(patch.toXmlString()) || processor.getRoutingError().isNotEmpty()) {
            if (!processor.pasteSettings(before.toXmlString()) || processor.getRoutingError().isNotEmpty()) return fail("Preset load and rollback failed. Restore the host session state.");
            return fail("Preset load failed; the previous patch was restored.");
        }
        undoPatch = before;
        return juce::Result::ok();
    }
};

// Optional standalone UI. Own it in Editor, not on the stack. All async callbacks
// use SafePointer; chooser destruction cancels outstanding work. Normal controls
// never request keyboard focus; only the native save dialog owns text entry.
class PresetBar final : public juce::Component {
    class PassiveBrowser final : public juce::ComboBox {
        void lookAndFeelChanged() override {
            juce::ComboBox::lookAndFeelChanged();
            for (auto* child : getChildren()) {
                child->setWantsKeyboardFocus(false);
                child->setMouseClickGrabsKeyboardFocus(false);
            }
        }
    };
public:
    static constexpr int controlRowHeight = 30, statusRowHeight = 22;
    static constexpr int preferredHeight = controlRowHeight + 2 + statusRowHeight;
    explicit PresetBar(Processor& processor, juce::File preferencesFile = {}) : manager(processor, preferencesFile) {
        setWantsKeyboardFocus(false);
        for (auto* button : {&refreshButton, &folderButton, &saveButton, &undoButton}) {
            addAndMakeVisible(*button); button->setWantsKeyboardFocus(false); button->setMouseClickGrabsKeyboardFocus(false);
        }
        addAndMakeVisible(browser); addAndMakeVisible(status);
        browser.getProperties().set("vocalstackPresetSelector", true);
        browser.setEditableText(false); browser.setWantsKeyboardFocus(false); browser.setMouseClickGrabsKeyboardFocus(false);
        status.setInterceptsMouseClicks(false, false);
        status.setMinimumHorizontalScale(1.f);
        status.setFont(pixelFontOptions(11.0f));
        browser.setTooltip("Factory and user presets. Loading replaces the whole patch; Undo load restores the previous patch.");
        undoButton.setTooltip("Restore the entire patch from before the last successful preset load (one step, not parameter undo).");
        folderButton.setTooltip("Choose the user preset folder; remembered locally on this machine, never in host state.");
        saveButton.setTooltip("Save the complete patch under a new name. Existing files are never overwritten.");
        refreshButton.onClick = [this] { refresh(); };
        folderButton.onClick = [this] { choose(false); };
        saveButton.onClick = [this] { choose(true); };
        undoButton.onClick = [this] {
            const auto result = manager.undoLoad();
            if (result.wasOk()) {
                lastLoaded = 0;
                browser.setSelectedId(0, juce::dontSendNotification);
            }
            report(result, "Restored previous patch", true);
        };
        browser.onChange = [this] {
            const int index = browser.getSelectedId() - 1;
            if (index >= 0 && index < int(entries.size())) {
                const auto result = manager.load(entries[size_t(index)]);
                report(result, "Loaded " + entries[size_t(index)].name, true);
                if (result.failed()) browser.setSelectedId(lastLoaded, juce::dontSendNotification);
                else lastLoaded = index + 1;
            }
        };
        refresh();
    }
    std::function<void()> onPatchLoaded;
    PresetManager& getManager() noexcept { return manager; }
    void refresh() {
        const auto result = manager.refresh(entries);
        browser.clear(juce::dontSendNotification);
        browser.addSectionHeading("Factory / original vocal patches");
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == size_t(PresetManager::factoryNames().size())) { browser.addSeparator(); browser.addSectionHeading("User presets"); }
            browser.addItem(entries[i].name, int(i + 1));
        }
        lastLoaded = 0;
        browser.setTextWhenNothingSelected("Choose preset...");
        report(result, "Folder: " + manager.getFolder().getFullPathName());
    }
    void applyTheme() {
        const auto p = themePalette(*this);
        browser.setColour(juce::ComboBox::backgroundColourId, p.inset);
        browser.setColour(juce::ComboBox::outlineColourId, p.border);
        browser.setColour(juce::ComboBox::textColourId, p.text);
        browser.setColour(juce::ComboBox::arrowColourId, p.accent);
        status.setColour(juce::Label::textColourId, p.muted);
        for (auto* button : {&refreshButton, &folderButton, &saveButton, &undoButton}) {
            button->setColour(juce::TextButton::buttonColourId, p.panel);
            button->setColour(juce::TextButton::textColourOffId, p.text);
        }
        repaint();
    }
    void resized() override {
        auto area = getLocalBounds();
        auto row = area.removeFromTop(controlRowHeight);
        const auto placeButton = [&](juce::TextButton& button, int minimumWidth) {
            const auto font = getLookAndFeel().getTextButtonFont(button, controlRowHeight);
            const int width = juce::jmax(minimumWidth,
                int(std::ceil(juce::GlyphArrangement::getStringWidth(font, button.getButtonText()))) + 16);
            button.setBounds(row.removeFromRight(width + 4).reduced(2, 0));
        };
        placeButton(undoButton, 90);
        placeButton(saveButton, 56);
        placeButton(folderButton, 66);
        placeButton(refreshButton, 68);
        browser.setBounds(row.reduced(2, 0));
        area.removeFromTop(2);
        status.setBounds(area.reduced(3, 0));
    }
private:
    PresetManager manager;
    std::vector<PresetManager::Entry> entries;
    PassiveBrowser browser;
    juce::Label status;
    juce::TextButton refreshButton{"Refresh"}, folderButton{"Folder..."}, saveButton{"Save..."}, undoButton{"Undo load"};
    std::unique_ptr<juce::FileChooser> chooser;
    bool choosing = false;
    int lastLoaded = 0;
    void report(const juce::Result& result, const juce::String& success, bool loaded = false) {
        const auto message = result.failed() ? result.getErrorMessage() : success;
        status.setText(message, juce::dontSendNotification); status.setTooltip(message);
        const bool available = manager.getFolder().isDirectory();
        saveButton.setEnabled(available && !choosing);
        folderButton.setEnabled(!choosing);
        undoButton.setEnabled(manager.canUndoLoad());
        if (loaded && result.wasOk() && onPatchLoaded) onPatchLoaded();
    }
    void choose(bool saving) {
        if (choosing) return;
        choosing = true;
        report(juce::Result::ok(), saving ? "Choose a new preset filename..." : "Choose the user preset folder...");
        chooser = std::make_unique<juce::FileChooser>(saving ? "Save new Vocal Stack preset (no overwrite)" : "Choose Vocal Stack preset folder",
            saving ? manager.getFolder().getChildFile("Untitled" + juce::String(PresetManager::extension)) : manager.getFolder(),
            saving ? "*" + juce::String(PresetManager::extension) : juce::String());
        const int flags = saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;
        chooser->launchAsync(flags, [safe = juce::Component::SafePointer<PresetBar>(this), saving](const juce::FileChooser& dialog) {
            if (!safe) return;
            safe->choosing = false;
            auto file = dialog.getResult();
            if (file == juce::File()) { safe->report(juce::Result::ok(), "Cancelled"); return; }
            if (saving && file.getFileExtension().isEmpty()) file = file.withFileExtension(PresetManager::extension);
            const auto result = saving ? safe->manager.saveNew(file) : safe->manager.setFolder(file);
            if (result.wasOk()) safe->refresh();
            safe->report(result, saving ? "Saved " + file.getFileNameWithoutExtension() : "Folder: " + file.getFullPathName());
        });
    }
};

} // namespace vocalstack
