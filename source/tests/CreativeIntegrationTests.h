#pragma once

#include "CreativeEffects.h"
#include "PluginProcessor.h"
#include "ProcessorRoutingState.h"
#include "PresetManager.h"
#include "RoutingPanel.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

// Include in EditorTests.cpp and call run() after ScopedJuceInitialiser_GUI.
// Returns false (with named failures on stderr) rather than throwing through main.
// Message-thread only; PresetManager fixtures use unique temporary preferences.
// PNGs are real 1200x920 editor renders, written to source/build on execution.
namespace vocalstack::creativeIntegrationTests {

inline void require(bool condition, const juce::String& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}
inline void near(double actual, double expected, const juce::String& message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= 0.0001 * std::max(1.0, std::abs(expected)), message);
}
inline void success(const juce::Result& result) { require(result.wasOk(), result.getErrorMessage()); }
inline juce::RangedAudioParameter& parameter(Processor& p, const juce::String& id) {
    auto* result = p.parameters.getParameter(id);
    require(result != nullptr, "Missing creative integration parameter: " + id);
    return *result;
}
inline void write(Processor& p, const juce::String& id, float value) {
    auto& target = parameter(p, id);
    target.setValueNotifyingHost(target.convertTo0to1(value));
}
inline float read(Processor& p, const juce::String& id) {
    const auto* result = p.parameters.getRawParameterValue(id);
    require(result != nullptr, "Missing creative raw parameter: " + id);
    return result->load(std::memory_order_relaxed);
}
inline juce::ValueTree state(Processor& p) {
    const auto xml = juce::parseXML(p.settingsText());
    require(xml != nullptr, "Processor settings XML did not parse");
    return juce::ValueTree::fromXml(*xml);
}
struct EffectCase { NodeType type; CreativeKind kind; const char* name; };
inline constexpr std::array<EffectCase, 5> effects{{
    {NodeType::delay, CreativeKind::delay, "delay"},
    {NodeType::reverb, CreativeKind::reverb, "reverb"},
    {NodeType::compressor, CreativeKind::compressor, "compressor"},
    {NodeType::bitcrusher, CreativeKind::bitcrusher, "bitcrusher"},
    {NodeType::granular, CreativeKind::granular, "granular"}
}};
inline Graph graph(NodeType type) {
    Graph result{{{1, NodeType::lead}, {100, type}, {13, NodeType::output}},
                 {{1, 100, 0, 0}, {100, 13, 0, 0}}};
    result.nodes[1].mix = 1;
    return result;
}
inline juce::String encoded(const Graph& g) { return processorRouting::encode(g, "Graph").toXmlString(); }
inline void install(Processor& p, const Graph& g) {
    juce::String error;
    const bool accepted = p.setRoutingGraph(g, error);
    require(accepted, "Creative routing rejected: " + error);
}
inline void prepare(Processor& p, double rate, int channels) {
    auto layout = p.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.set(0, set); layout.outputBuses.set(0, set);
    require(p.setBusesLayout(layout), "Creative test bus layout rejected");
    write(p, "levelOn", 0); write(p, "doubleOn", 0); write(p, "formantOn", 0);
    write(p, "globalMix", 100); write(p, "bypass", 0);
    p.prepareToPlay(rate, 511);
}
inline constexpr std::array<int, 9> blockSizes{1, 7, 31, 127, 3, 511, 64, 255, 19};
inline void fill(juce::AudioBuffer<float>& audio, double rate, int position, bool silence = false) {
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int i = 0; i < audio.getNumSamples(); ++i) {
            const double t = (position + i) / rate;
            audio.setSample(ch, i, silence ? 0.0f : float(.12 * std::sin(6.283185307179586 * (ch ? 337 : 223) * t)
                                                       + .035 * std::sin(6.283185307179586 * 997 * t)));
        }
}
inline double finite(const juce::AudioBuffer<float>& audio) {
    double energy = 0;
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        for (int i = 0; i < audio.getNumSamples(); ++i) {
            const float value = audio.getSample(ch, i);
            require(std::isfinite(value), "Creative processing produced NaN/Inf");
            energy += double(value) * value;
        }
    return energy;
}
inline double render(Processor& p, double rate, int channels, int samples, bool silence = false) {
    juce::MidiBuffer midi;
    int position = 0;
    size_t block = 0;
    double energy = 0;
    while (position < samples) {
        const int size = std::min(blockSizes[block++ % blockSizes.size()], samples - position);
        juce::AudioBuffer<float> audio(channels, size);
        fill(audio, rate, position, silence);
        p.processBlock(audio, midi);
        energy += finite(audio);
        position += size;
    }
    return energy;
}

inline void parameterContract() {
    Processor p;
    require(creativeParameterStart == 1007 && creativeParameterCount == 68, "Creative append contract changed");
    require(parameterIds.size() == 3177 && p.getParameters().size() == int(parameterIds.size()), "Wrong expanded host parameter count");
    std::set<std::string> unique;
    for (size_t i = 0; i < parameterIds.size(); ++i) {
        require(unique.insert(parameterIds[i]).second, "Duplicate parameter ID");
        require(p.getParameters()[int(i)] == p.parameters.getParameter(parameterIds[i]), "Host parameter index changed");
        if (i < legacyParameterIds.size()) require(std::string(parameterIds[i]) == legacyParameterIds[i], "Legacy host ID changed");
    }
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < creativeParameterCount; ++i) {
        const auto id = creativeParameterId(slot, i);
        const auto& descriptor = creativeParameters[i];
        require(id == parameterIds[creativeParameterIndex(slot, i)], "Creative helper host offset: " + id);
                if (i < 48) require(creativeParameterIndex(slot, i) == 1007 + (slot - 1) * 48 + i, "Historical creative host index changed");
        auto& target = parameter(p, id);
        near(target.getNormalisableRange().start, descriptor.min, "Creative range minimum: " + id);
        near(target.getNormalisableRange().end, descriptor.max, "Creative range maximum: " + id);
        near(read(p, id), descriptor.initial, "Creative default: " + id);
        write(p, id, descriptor.discrete ? 1.0f : descriptor.min + .37f * (descriptor.max - descriptor.min));
        near(p.settings().fxSettings[slot].creative.values[i], read(p, id), "Creative cached settings bridge: " + id);
    }
    for (size_t i = 0; i < effects.size(); ++i)
        require(int(effects[i].type) == int(i) + 9, "Creative node serialization enum changed");
    p.resetParameters();
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < creativeParameterCount; ++i)
        near(read(p, creativeParameterId(slot, i)), creativeParameters[i].initial, "Reset missed creative parameter");
}

inline void addConnectAndProcess(const EffectCase& effect, double rate, int channels) {
    Processor p;
    auto g = graph(effect.type);
    g.links.clear();
    install(p, g); // Exercise new-node registration independently of cabling.
    require(p.getRoutingGraph().nodes[1].type == effect.type, "Creative node did not survive add");
    g.links.push_back({1, 100, 0, 0}); install(p, g);
    g.links.push_back({100, 13, 0, 0}); install(p, g);
    prepare(p, rate, channels);
    const auto before = encoded(p.getRoutingGraph());
    require(render(p, rate, channels, int(rate * .65)) > 1.0e-8, "Connected creative effect produced only silence");
    render(p, rate, channels, int(rate * .1), true);
    require(encoded(p.getRoutingGraph()) == before, "Audio processing mutated authored routing");
    p.reset();
    near(render(p, rate, channels, 4096, true), 0, "Creative reset retained audible history");
    p.releaseResources();
}

inline Graph fullGraph() {
    Graph g;
    g.nodes.push_back({1, NodeType::lead});
    uint32_t previous = 1;
    for (size_t i = 0; i < effects.size(); ++i) {
        RoutingNode n{uint32_t(100 + i), effects[i].type};
        n.mix = .4f;
        for (size_t field = 0; field < creativeParameterCount; ++field) {
            const auto& d = creativeParameters[field];
            n.creative.values[field] = d.discrete ? 0.0f : d.min + .23f * (d.max - d.min);
        }
        g.nodes.push_back(n); g.links.push_back({previous, n.id, 0, 0}); previous = n.id;
    }
    g.nodes.push_back({13, NodeType::output}); g.links.push_back({previous, 13, 0, 0});
    return g;
}
inline void roundTrips() {
    Processor source;
    const auto g = fullGraph();
    install(source, g);
    Graph decoded;
    require(processorRouting::decode(processorRouting::encode(g, "Graph"), decoded), "Creative graph decode rejected");
    require(encoded(decoded) == encoded(g), "Creative nested model roundtrip lost settings");
    for (size_t slot = 1; slot <= effects.size(); ++slot) for (size_t i = 0; i < creativeParameterCount; ++i)
        near(read(source, creativeParameterId(slot, i)), g.nodes[slot].creative.values[i], "New creative node did not seed host value");
    // Distinguish saved automation from node-authored values, including dormant slots.
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < creativeParameterCount; ++i) {
        auto& target = parameter(source, creativeParameterId(slot, i));
        target.setValueNotifyingHost((i + slot) % 2 ? .31f : .69f);
    }
    ModulationState mods;
    Modulator lfo; lfo.id = 17; lfo.rateHz = .73f; lfo.phase = .2f; lfo.shape = .4f;
    mods.modulators.push_back(lfo);
    for (size_t slot = 1; slot <= effects.size(); ++slot) {
        const auto kind = effects[slot - 1].kind;
        for (size_t i = 0; i < creativeParameterCount; ++i)
            if (creativeParameters[i].kind == kind && !creativeParameters[i].discrete) {
                mods.assignments.push_back({17, creativeParameterId(slot, i), .17f, true}); break;
            }
    }
    juce::String error;
    const bool accepted = source.setModulationState(mods, error);
    require(accepted, "Creative modulation targets rejected: " + error);
    const auto saved = state(source);
    juce::MemoryBlock binary;
    source.getStateInformation(binary);
    Processor restored, pasted;
    restored.setStateInformation(binary.getData(), int(binary.getSize()));
    require(pasted.pasteSettings(source.settingsText()), "Creative settings paste rejected");
    for (auto* copy : {&restored, &pasted}) {
        for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < creativeParameterCount; ++i) {
            const auto id = creativeParameterId(slot, i);
            near(read(*copy, id), read(source, id), "Creative parameter roundtrip: " + id);
        }
        const auto recalled = state(*copy);
        for (const auto* section : {"RoutingGraph", "ActiveRoutingGraph", "Modulation"}) {
            require(saved.getChildWithName(section).isValid(), "Missing saved patch section");
            require(recalled.getChildWithName(section).toXmlString() == saved.getChildWithName(section).toXmlString(),
                    "Creative roundtrip lost section: " + juce::String(section));
        }
        require(copy->getModulationState().assignments.size() == effects.size(), "Creative modulation assignment count lost");
        prepare(*copy, 48000, 2);
        render(*copy, 48000, 2, 8192);
    }
    // A pre-creative state must not inherit parameters from a previous session.
    auto legacy = saved.createCopy();
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < creativeParameterCount; ++i)
        legacy.removeChild(legacy.getChildWithProperty("id", creativeParameterId(slot, i)), nullptr);
    legacy.removeChild(legacy.getChildWithName("RoutingGraph"), nullptr);
    legacy.removeChild(legacy.getChildWithName("ActiveRoutingGraph"), nullptr);
    legacy.removeChild(legacy.getChildWithName("Modulation"), nullptr);
    legacy.addChild(processorRouting::encode(defaultGraph(), "RoutingGraph"), -1, nullptr);
    legacy.addChild(processorRouting::encode(defaultGraph(), "ActiveRoutingGraph"), -1, nullptr);
    legacy.addChild(juce::ValueTree("Modulation"), -1, nullptr);
    require(pasted.pasteSettings(legacy.toXmlString()), "Pre-creative patch rejected");
    for (size_t slot = 1; slot <= 15; ++slot) for (size_t i = 0; i < creativeParameterCount; ++i)
        near(read(pasted, creativeParameterId(slot, i)), creativeParameters[i].initial, "Legacy patch inherited creative value");
}

struct MonitorSubscription {
    Processor& owner;
    explicit MonitorSubscription(Processor& p) : owner(p) { owner.retainFxMonitor(1); }
    ~MonitorSubscription() { owner.releaseFxMonitor(1); }
};
inline void automation(const EffectCase& effect) {
    // Independent deterministic renders catch history resets even if a broken
    // implementation preserves the authored graph and hides its publication.
    auto control = std::make_unique<Processor>();
    auto automated = std::make_unique<Processor>();
    for (auto* p : {control.get(), automated.get()}) { install(*p, graph(effect.type)); prepare(*p, 48000, 2); }
    MonitorSubscription subscription(*automated);
    juce::MidiBuffer midi;
    double energy = 0;
    int position = 0;
    for (int block = 0; position < 36000; ++block) {
        const int size = blockSizes[size_t(block) % blockSizes.size()];
        juce::AudioBuffer<float> a(2, size), b(2, size);
        fill(a, 48000, position, position >= 24000); b.makeCopyOf(a);
        // Notify even for an unchanged value: hosts may resend automation points.
        if (position >= 18000) for (size_t i = 0; i < creativeParameterCount; ++i) {
            const auto id = creativeParameterId(1, i);
            if (creativeParameters[i].kind == effect.kind) write(*automated, id, read(*automated, id));
            // Real edits to dormant controls must not reset the active machine.
            else parameter(*automated, id).setValueNotifyingHost(block % 2 ? .21f : .79f);
        }
        control->processBlock(a, midi); automated->processBlock(b, midi);
        finite(a); finite(b);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < size; ++i)
            require(std::abs(a.getSample(ch, i) - b.getSample(ch, i)) < 1.0e-6f, "Repeated automation reset creative DSP history");
        if (position >= 24000) energy += finite(b);
        position += size;
    }
    if (effect.kind == CreativeKind::delay || effect.kind == CreativeKind::reverb || effect.kind == CreativeKind::granular)
        require(energy > 1.0e-8, "History comparison did not exercise a creative tail");
    if (effect.kind == CreativeKind::delay || effect.kind == CreativeKind::reverb || effect.kind == CreativeKind::granular) {
        // Edit an active colour control after input and pipeline latency have
        // drained. A reset cannot manufacture the retained delay/capture/tail.
        for (auto* p : {control.get(), automated.get()}) {
            if (effect.kind == CreativeKind::granular)
                write(*p, creativeParameterId(1, size_t(CreativeParameter::granularPosition)), 400);
            p->reset();
            render(*p, 48000, 2, 36000);
            render(*p, 48000, 2, 4096, true);
        }
        const auto colour = effect.kind == CreativeKind::delay ? CreativeParameter::delayDrive
            : effect.kind == CreativeKind::reverb ? CreativeParameter::reverbSaturation : CreativeParameter::granularDrive;
        const auto id = creativeParameterId(1, size_t(colour));
        write(*automated, id, read(*automated, id) + 1);
        const auto referenceTail = render(*control, 48000, 2, 4096, true);
        const auto automatedTail = render(*automated, 48000, 2, 4096, true);
        require(referenceTail > 1.0e-8 && automatedTail > referenceTail * .01,
                "Changing an active creative control erased retained audio history");
    }
    FxMonitorFrame before, after;
    require(automated->readFxMonitor(1, before), "Creative monitor did not publish a warmed frame");
    const auto authored = encoded(automated->getRoutingGraph());
    // Real changing automation must reach cached settings without compiling a new
    // plan. Monitor epochs are invalidated by every actual plan installation.
    for (int step = 0; step < 8; ++step) {
        for (size_t i = 0; i < creativeParameterCount; ++i) if (creativeParameters[i].kind == effect.kind) {
            auto& target = parameter(*automated, creativeParameterId(1, i));
            target.setValueNotifyingHost(step % 2 ? .26f : .74f);
            near(automated->settings().fxSettings[1].creative.values[i], read(*automated, creativeParameterId(1, i)), "Automation did not update creative settings");
        }
        juce::Timer::callPendingTimersSynchronously();
        render(*automated, 48000, 2, 4096);
        require(automated->readFxMonitor(1, after) && after.sequence > before.sequence, "Automation stopped live monitor publication");
        require(after.epoch == before.epoch && after.nodeId == before.nodeId, "Creative automation republished routing/reset monitor history");
        require(encoded(automated->getRoutingGraph()) == authored, "Creative automation rewrote authored graph");
        before = after;
    }
}

struct TemporaryFolder {
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("VocalStackCreativeIntegration-" + juce::Uuid().toString());
    TemporaryFolder() { success(root.createDirectory()); }
    ~TemporaryFolder() { root.deleteRecursively(); }
};
inline void factories() {
    TemporaryFolder files;
    Processor p;
    PresetManager manager(p, files.root.getChildFile("local.settings"));
    std::array<bool, effects.size()> covered{};
    for (int index = 0; index < PresetManager::factoryNames().size(); ++index) {
        juce::ValueTree clean, dirty;
        success(manager.factoryPatch(index, clean));
        for (auto* base : p.getParameters()) if (auto* target = dynamic_cast<juce::RangedAudioParameter*>(base))
            target->setValueNotifyingHost(.81f);
        ModulationState stale;
        Modulator lfo; lfo.id = 99; stale.modulators.push_back(lfo);
        stale.assignments.push_back({99, creativeParameterId(15, 0), .2f, true});
        juce::String error;
        const bool accepted = p.setModulationState(stale, error); require(accepted, error);
        success(manager.factoryPatch(index, dirty));
        require(clean.toXmlString() == dirty.toXmlString(), "Factory inherited dirty creative patch");
        for (const auto* id : parameterIds)
            require(clean.getChildWithProperty("id", id).isValid(), "Factory omitted parameter: " + juce::String(id));
        success(manager.load({PresetManager::factoryNames()[index], {}, index}));
        for (const auto* id : parameterIds)
            near(read(p, id), float(clean.getChildWithProperty("id", id)["value"]), "Factory did not load complete parameter patch: " + juce::String(id));
        require(p.getModulationState().modulators.empty() && p.getModulationState().assignments.empty(), "Factory retained stale modulation");
        Graph expected;
        require(processorRouting::decode(clean.getChildWithName("RoutingGraph"), expected), "Factory creative graph invalid");
        require(encoded(p.getRoutingGraph()) == encoded(expected), "Factory did not load full routing patch");
        for (const auto& n : p.getRoutingGraph().nodes) for (size_t i = 0; i < effects.size(); ++i)
            if (n.type == effects[i].type) covered[i] = true;
        prepare(p, 48000, 2);
        require(render(p, 48000, 2, 30000) > 1.0e-8, "Loaded factory produced only silence");
        p.releaseResources();
    }
    require(std::all_of(covered.begin(), covered.end(), [](bool value) { return value; }), "Factories did not exercise all five creative effects");
}

template <typename T, typename Predicate>
inline T* find(juce::Component& parent, const Predicate& predicate) {
    if (auto* result = dynamic_cast<T*>(&parent); result && predicate(*result)) return result;
    for (auto* child : parent.getChildren()) if (auto* result = find<T>(*child, predicate)) return result;
    return nullptr;
}
inline void passive(juce::Component& component) {
    if (dynamic_cast<juce::TextEditor*>(&component)) return;
    require(!component.getWantsKeyboardFocus() && !component.getMouseClickGrabsKeyboardFocus(), "Creative inspector steals host keyboard focus: " + component.getName());
    for (auto* child : component.getChildren()) passive(*child);
}
inline juce::ValueTree selectionLayout() {
    juce::ValueTree layout("RoutingLayout");
    layout.setProperty("selectedNode", 100, nullptr);
    layout.setProperty("inspectorWidth", 320, nullptr);
    layout.setProperty("editorWidth", 1200, nullptr); layout.setProperty("editorHeight", 920, nullptr);
    layout.setProperty("zoom", .72f, nullptr);
    layout.setProperty("panX", 0, nullptr); layout.setProperty("panY", 30, nullptr);
    const std::array<int, 3> ids{1, 100, 13}, xs{20, 245, 470};
    for (size_t i = 0; i < ids.size(); ++i) {
        juce::ValueTree n("NODE"); n.setProperty("id", ids[i], nullptr);
        n.setProperty("x", xs[i], nullptr); n.setProperty("y", 35, nullptr);
        layout.addChild(n, -1, nullptr);
    }
    return layout;
}
inline juce::File screenshotFolder() {
    const juce::String file(__FILE__);
    if (juce::File::isAbsolutePath(file))
        return juce::File(file).getParentDirectory().getParentDirectory().getChildFile("build");
    // Support runners launched from source/build, the repository root, or IDEs
    // whose compilers emit relative __FILE__ paths.
    auto directory = juce::File::getCurrentWorkingDirectory();
    for (int i = 0; i < 10; ++i) {
        if (directory.getChildFile("tests/CreativeIntegrationTests.h").existsAsFile()) return directory.getChildFile("build");
        if (directory.getChildFile("source/tests/CreativeIntegrationTests.h").existsAsFile()) return directory.getChildFile("source/build");
        const auto parent = directory.getParentDirectory();
        if (parent == directory) break;
        directory = parent;
    }
    throw std::runtime_error("Cannot locate source/build for creative screenshots");
}
inline void snapshot(juce::Component& editor, const juce::String& filename) {
    const auto folder = screenshotFolder(); success(folder.createDirectory());
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
    require(image.isValid() && image.getWidth() == 1200 && image.getHeight() == 920, "Creative editor screenshot must be 1200x920");
    const auto file = folder.getChildFile(filename);
    juce::FileOutputStream stream(file);
    require(stream.openedOk() && stream.setPosition(0), "Cannot open creative screenshot: " + file.getFullPathName());
    success(stream.truncate());
    juce::PNGImageFormat png;
    require(png.writeImageToStream(image, stream), "Cannot encode creative screenshot");
    stream.flush(); success(stream.getStatus());
    require(file.getSize() > 0, "Creative screenshot is empty");
    std::cout << "Snapshot: " << file.getFullPathName() << '\n';
}
inline void inspectorControls(Processor& p, CreativeEffectPanel& panel, CreativeKind kind) {
    passive(panel);
    const auto check = [&](const juce::String& id, bool discrete) {
        auto& host = parameter(p, id);
        const float original = read(p, id);
        panel.revealParameter(id);
        auto* control = find<juce::Component>(panel, [&](const juce::Component& c) {
            return c.getComponentID() == id && (dynamic_cast<const juce::Slider*>(&c) || dynamic_cast<const juce::Button*>(&c));
        });
        require(control != nullptr && control->isShowing(), "Missing visible creative control: " + id);
        require(control->getWidth() >= (discrete ? 48 : 64) && control->getHeight() >= 20,
                        "Creative control clipped at minimum routing width: " + id);
                if (discrete) require(control->getWidth() <= 64 && control->getHeight() >= 24,
                                      "Creative two-state control must stay compact and usable: " + id);
        require(control->getParentComponent()->getLocalBounds().contains(control->getBounds()), "Creative control outside its row: " + id);
        if (discrete) {
            auto* button = dynamic_cast<juce::Button*>(control); require(button != nullptr, "Discrete creative control is not a button");
            button->setToggleState(original < .5f, juce::sendNotificationSync);
            near(read(p, id), original < .5f ? 1 : 0, "Creative toggle did not write host");
            write(p, id, original); require(button->getToggleState() == (original >= .5f), "Creative toggle did not follow host");
        } else {
            auto* slider = dynamic_cast<juce::Slider*>(control); require(slider != nullptr, "Continuous creative control is not a slider");
            const bool texturedBar = bool(slider->getProperties()["vocalstackTexturedBar"]);
            if (!texturedBar) require(slider->isTextBoxEditable(), "Creative numeric entry disabled");
            near(slider->getMinimum(), host.getNormalisableRange().start, "Creative slider minimum");
            near(slider->getMaximum(), host.getNormalisableRange().end, "Creative slider maximum");
            slider->setValue(host.getNormalisableRange().end, juce::sendNotificationSync);
            near(read(p, id), float(slider->getValue()), "Creative slider did not write host");
            write(p, id, original); near(slider->getValue(), original, "Creative slider did not follow host");
            if (texturedBar) {
                auto* caption = find<juce::Label>(*slider, [](const juce::Label&) { return true; });
                require(caption != nullptr && caption->isVisible() && !slider->isTextBoxEditable()
                            && !caption->isEditable(), "Creative Mix native caption must be display-only");
                auto* root = slider->getTopLevelComponent();
                const auto point = root->getLocalPoint(caption, caption->getLocalBounds().getCentre());
                auto* hit = root->getComponentAt(point);
                require(hit == slider, "Creative Mix caption must pass root mouse hits to slider");
                const auto start = hit->getLocalPoint(root, point).toFloat();
                const auto focus = juce::Component::getCurrentlyFocusedComponent();
                if (slider->isEnabled()) {
                    write(p, id, host.getNormalisableRange().convertFrom0to1(.5f));
                    const auto before = read(p, id);
                    const auto end = start.translated(slider->getWidth() * .2f, 0);
                    const auto now = juce::Time::getCurrentTime();
                    const auto mouse = [&](juce::Point<float> point, int modifiers, bool dragged) {
                        return juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(), point,
                            juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, slider, slider, now, start, now, 1, dragged};
                    };
                    slider->mouseDown(mouse(start, juce::ModifierKeys::leftButtonModifier, false));
                    near(read(p, id), before, "Creative Mix caption mouseDown must not jump");
                    slider->mouseUp(mouse(start, 0, false));
                    near(read(p, id), before, "Creative Mix caption click must not jump");
                    require(caption->getCurrentTextEditor() == nullptr && !caption->isEditable()
                                && !slider->isTextBoxEditable(), "Creative Mix caption click must remain display-only");
                    require(juce::Component::getCurrentlyFocusedComponent() == focus, "Creative Mix caption click stole focus");
                    slider->mouseDown(mouse(start, juce::ModifierKeys::leftButtonModifier, false));
                    slider->mouseDrag(mouse(end, juce::ModifierKeys::leftButtonModifier, true));
                    slider->mouseUp(mouse(end, 0, true));
                    require(std::abs(read(p, id) - before) > .01f, "Creative Mix caption drag did not edit host");
                    near(read(p, id), float(slider->getValue()), "Creative Mix drag host binding");
                    require(caption->getCurrentTextEditor() == nullptr, "Creative Mix drag opened numeric entry");
                    require(juce::Component::getCurrentlyFocusedComponent() == focus, "Creative Mix caption drag stole focus");
                    write(p, id, original);
                }
            } else if (slider->isEnabled()) {
                slider->showTextBox();
                auto* entry = find<juce::TextEditor>(*slider, [](const juce::TextEditor&) { return true; });
                require(entry != nullptr && entry->getWantsKeyboardFocus(), "Creative numeric entry cannot own keys");
                entry->setText(juce::String(host.getNormalisableRange().start, 6), false);
                slider->hideTextBox(false);
                near(read(p, id), host.getNormalisableRange().start, "Creative numeric entry did not commit");
                slider->showTextBox();
                entry = find<juce::TextEditor>(*slider, [](const juce::TextEditor&) { return true; });
                require(entry != nullptr, "Creative numeric entry did not reopen");
                entry->setText(juce::String(host.getNormalisableRange().end, 6), false);
                slider->hideTextBox(true);
                near(read(p, id), host.getNormalisableRange().start, "Creative numeric cancel changed host value");
                write(p, id, original);
            }
        }
    };
    check(fxParameterId(1, "Mix"), false);
    for (size_t i = 0; i < creativeParameterCount; ++i)
        if (creativeParameters[i].kind == kind) check(creativeParameterId(1, i), creativeParameters[i].discrete);
    panel.revealParameter(fxParameterId(1, "Mix"));
}
inline void editor(const EffectCase& effect, bool neon) {
    Processor p;
    install(p, graph(effect.type)); p.setFlash(neon); p.setRoutingLayout(selectionLayout());
    std::unique_ptr<juce::AudioProcessorEditor> view(p.createEditor());
    require(view != nullptr, "Creative editor creation failed");
    view->setSize(1200, 920); view->setVisible(true); view->addToDesktop(0);
    auto* routingButton = find<juce::Button>(*view, [](const juce::Button& b) { return b.getButtonText() == "Routing"; });
    require(routingButton != nullptr && bool(routingButton->onClick), "Routing tab unavailable"); routingButton->onClick();
    auto* routing = find<RoutingPanel>(*view, [](const RoutingPanel&) { return true; });
    auto* panel = find<CreativeEffectPanel>(*view, [](const CreativeEffectPanel&) { return true; });
    require(routing != nullptr && routing->isShowing() && panel != nullptr && panel->isShowing(), "Saved creative selection did not open inspector");
    require(panel->getName() == CreativeEffectPanel::effectName(effect.kind) + " inspector", "Wrong creative inspector selected");
    const auto normalBounds = routing->getBounds();
    routing->setSize(608, std::max(600, routing->getHeight())); // 288px canvas/rail budget + 320px inspector.
    require(panel->getParentComponent()->getWidth() >= 320, "Minimum routing width squeezed creative inspector");
    inspectorControls(p, *panel, effect.kind);
    auto* canvas = find<juce::Component>(*routing, [](const juce::Component& c) { return c.getName() == "Routing node canvas"; });
    require(canvas != nullptr && canvas->getWidth() >= 100, "Minimum routing width squeezed node canvas");
    routing->setBounds(normalBounds); view->setSize(1200, 920); view->resized();
    require(view->getLocalArea(panel, panel->getLocalBounds()).getIntersection(view->getLocalBounds()).getWidth() == panel->getWidth(), "Creative inspector outside editor screenshot");
    snapshot(*view, "creative-" + juce::String(effect.name) + (neon ? "-neon.png" : "-device.png"));
    view->setVisible(false);
}

inline bool run() {
    auto* messages = juce::MessageManager::getInstanceWithoutCreating();
    if (messages == nullptr || !messages->isThisTheMessageThread()) {
        std::cerr << "FAIL creative integration: run after ScopedJuceInitialiser_GUI on the message thread\n";
        return false;
    }
    bool passed = true;
    const auto test = [&](const juce::String& name, const auto& body) {
        try { body(); std::cout << "PASS creative integration / " << name << '\n'; }
        catch (const std::exception& error) {
            passed = false; std::cerr << "FAIL creative integration / " << name << ": " << error.what() << '\n';
        }
    };
    test("parameter contract", [] { parameterContract(); });
    test("state, graph and modulation roundtrips", [] { roundTrips(); });
    for (const auto& effect : effects) {
        for (const double rate : {44100.0, 48000.0, 96000.0}) for (const int channels : {1, 2})
            test(juce::String(effect.name) + " / " + juce::String(rate, 0) + " Hz / " + juce::String(channels) + "ch", [&] { addConnectAndProcess(effect, rate, channels); });
        test(juce::String(effect.name) + " automation/history", [&] { automation(effect); });
        for (const bool neon : {false, true})
            test(juce::String(effect.name) + (neon ? " neon inspector" : " device inspector"), [&] { editor(effect, neon); });
    }
    test("complete factory patches and audio", [] { factories(); });
    return passed;
}

} // namespace vocalstack::creativeIntegrationTests
