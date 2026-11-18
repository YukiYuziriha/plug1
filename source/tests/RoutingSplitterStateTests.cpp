// Standalone processor/preset backend suite; link like VocalStackModulationProcessorTests.
// Deliberately separate from EditorTests.cpp and its UI-owned test headers.
#include "PluginProcessor.h"
#include "ProcessorRoutingState.h"
#include "PresetManager.h"
#include <iostream>
#include <stdexcept>

using namespace vocalstack;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Graph graph() {
    return {{{1, NodeType::lead}, {300, NodeType::splitter}, {301, NodeType::splitter}, {13, NodeType::output}},
            {{1, 300, 0}, {300, 301, 0, 1}, {300, 13, 0, 0}, {301, 13, 0, 1}}};
}
void sameTopology(const Graph& actual, const Graph& expected) {
    check(actual.nodes.size() == expected.nodes.size() && actual.links.size() == expected.links.size(),
          "splitter recall changed graph size");
    for (size_t i = 0; i < expected.nodes.size(); ++i)
        check(actual.nodes[i].id == expected.nodes[i].id && actual.nodes[i].type == expected.nodes[i].type,
              "splitter recall changed node identity/type");
    for (size_t i = 0; i < expected.links.size(); ++i) {
        const auto& a = actual.links[i]; const auto& b = expected.links[i];
        check(a.source == b.source && a.destination == b.destination && a.inputPin == b.inputPin && a.sourcePin == b.sourcePin,
              "splitter recall changed link or source pin");
    }
}
void codec() {
    check(!processorRouting::isEffect(NodeType::splitter) && !isCreativeEffect(NodeType::splitter)
          && !isUtilityEffect(NodeType::splitter), "splitter classified as effect");
    check(processorRouting::fxSlot(300) == -1 && routingFxSlot(300) == -1,
          "splitter ID consumes automation slot");
    for (const auto* type : {"RoutingGraph", "ActiveRoutingGraph"}) {
        const auto encoded = processorRouting::encode(graph(), type);
        Graph decoded;
        check(processorRouting::decode(encoded, decoded) && bool(processorRouting::validate(decoded)),
              "splitter graph codec rejected appended type");
        sameTopology(decoded, graph());
        const auto xml = juce::parseXML(encoded.toXmlString());
        check(xml != nullptr && processorRouting::decode(juce::ValueTree::fromXml(*xml), decoded),
              "splitter XML roundtrip failed");
        sameTopology(decoded, graph());
        auto bad = encoded.createCopy();
        bad.getChildWithProperty("id", juce::int64(300)).setProperty("type", 24, nullptr);
        check(!processorRouting::decode(bad, decoded), "unknown appended type accepted");
        for (int pin : {-1, 2}) {
            bad = encoded.createCopy();
            for (auto child : bad) if (child.hasType("Link") && int(child["source"]) == 300)
                child.setProperty("sourcePin", pin, nullptr);
            check(processorRouting::decode(bad, decoded) && !processorRouting::validate(decoded),
                  "saved invalid splitter source pin accepted");
        }
    }
    // Older graphs have no sourcePin property and must continue to default to zero.
    auto legacy = processorRouting::encode(defaultGraph(), "RoutingGraph");
    for (auto child : legacy) if (child.hasType("Link")) child.removeProperty("sourcePin", nullptr);
    Graph decoded;
    check(processorRouting::decode(legacy, decoded) && bool(processorRouting::validate(decoded)),
          "splitter decoding broke legacy state");
}
std::vector<float> parameterValues(Processor& owner) {
    std::vector<float> result;
    for (auto* p : owner.getParameters()) result.push_back(p->getValue());
    return result;
}
void processorRecall() {
    auto owner = std::make_unique<Processor>();
    const auto before = parameterValues(*owner);
    check(before.size() == 2378, "splitter changed the host parameter count");
    juce::String error;
    check(owner->setRoutingGraph(graph(), error), "processor rejected splitter graph");
    check(parameterValues(*owner) == before, "splitter graph edit changed host parameters");
    const auto encodedBeforeFailure = processorRouting::encode(owner->getRoutingGraph(), "RoutingGraph").toXmlString();
    auto overCapacity = graph();
    for (int i = 0; i < 45; ++i) overCapacity.nodes.push_back({std::uint32_t(400 + i), NodeType::splitter});
    check(!owner->setRoutingGraph(overCapacity, error), "processor accepted 49 splitter/source/output nodes");
    check(processorRouting::encode(owner->getRoutingGraph(), "RoutingGraph").toXmlString() == encodedBeforeFailure,
          "capacity rejection changed the saved splitter graph");
    for (size_t i = 0; i < parameterIds.size(); ++i)
        check(owner->getParameters()[int(i)] == owner->parameters.getParameter(parameterIds[i]),
              "splitter changed host parameter IDs/indices");
    juce::MemoryBlock saved; owner->getStateInformation(saved);
    auto recalled = std::make_unique<Processor>();
    recalled->setStateInformation(saved.getData(), int(saved.getSize()));
    sameTopology(recalled->getRoutingGraph(), graph());
    check(parameterValues(*recalled) == before, "splitter host recall changed parameters");
    auto pasted = std::make_unique<Processor>();
    check(pasted->pasteSettings(owner->settingsText()), "splitter settings paste failed");
    sameTopology(pasted->getRoutingGraph(), graph());
    auto invalid = graph(); invalid.links.back().sourcePin = 2;
    check(!owner->setRoutingGraph(invalid, error), "processor accepted invalid splitter output");
    sameTopology(owner->getRoutingGraph(), graph());
    invalid = graph(); invalid.links.push_back({301, 300, 0, 1});
    check(!owner->setRoutingGraph(invalid, error), "processor accepted splitter cycle");
    sameTopology(owner->getRoutingGraph(), graph());
}
struct TemporaryFolder {
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("VocalStackSplitterTests-" + juce::Uuid().toString());
    TemporaryFolder() { check(root.createDirectory().wasOk(), "could not create isolated preset directory"); }
    ~TemporaryFolder() { root.deleteRecursively(); }
};
void presets() {
    TemporaryFolder files;
    auto owner = std::make_unique<Processor>();
    PresetManager manager(*owner, files.root.getChildFile("local.settings"));
    juce::String error;
    check(owner->setRoutingGraph(graph(), error), "preset splitter fixture rejected");
    const auto text = manager.captureText("Splitter roundtrip");
    check(owner->setRoutingGraph(defaultGraph(), error), "preset reset failed");
    check(manager.loadText(text).wasOk(), "preset preflight/load rejected splitter type 19");
    sameTopology(owner->getRoutingGraph(), graph());
    for (const auto* treeType : {"RoutingGraph", "ActiveRoutingGraph"}) for (bool cycle : {false, true}) {
        const auto xml = juce::parseXML(text);
        check(xml != nullptr, "preset XML parse failed");
        auto document = juce::ValueTree::fromXml(*xml);
        auto tree = document.getChild(0).getChildWithName(treeType);
        check(tree.isValid(), "preset missing routing section");
        if (cycle) {
            juce::ValueTree link("Link");
            link.setProperty("source", 301, nullptr); link.setProperty("destination", 300, nullptr);
            link.setProperty("inputPin", 0, nullptr); link.setProperty("sourcePin", 1, nullptr);
            tree.addChild(link, -1, nullptr);
        } else {
            for (auto child : tree) if (child.hasType("Link") && int(child["source"]) == 300)
                child.setProperty("sourcePin", 2, nullptr);
        }
        check(manager.loadText(document.toXmlString()).failed(), "preset accepted invalid splitter pins/cycle");
        sameTopology(owner->getRoutingGraph(), graph());
    }
}
void dryWetRecall() {
    auto owner = std::make_unique<Processor>();
    auto g = graph();
    g.nodes.push_back({200, NodeType::mix}); g.nodes.back().mix = .73f;
    g.nodes.push_back({201, NodeType::mix}); g.nodes.back().mix = .19f;
    g.nodes.push_back({247, NodeType::mix}); g.nodes.back().mix = .41f;
    g.links = {{1, 300, 0}, {300, 200, 0, 0}, {300, 201, 1, 1},
               {200, 247, 0}, {201, 247, 1}, {247, 13, 0}};
    juce::String error;
    check(owner->setRoutingGraph(g, error), "Dry/Wet recall fixture rejected");
    const auto verify = [&](Processor& target) {
        sameTopology(target.getRoutingGraph(), g);
        check(target.getParameters().size() == 2378, "Dry/Wet changed host parameter count");
        for (size_t i = 0; i < parameterIds.size(); ++i)
            check(target.getParameters()[int(i)] == target.parameters.getParameter(parameterIds[i]), "Dry/Wet changed existing host indices");
        for (auto pair : {std::pair<uint32_t, float>{200u, 73.f}, {201u, 19.f}, {247u, 41.f}}) {
            const auto id = localMixParameterId(pair.first);
            const auto* p = target.parameters.getParameter(id);
            check(p && p->getParameterIndex() == 944 + int(pair.first - 200), "Dry/Wet explicit index mapping changed");
            check(std::abs(target.parameters.getRawParameterValue(id)->load() - pair.second) < .001f, "Dry/Wet independent balance recall failed");
        }
    };
    verify(*owner);
    juce::MemoryBlock saved; owner->getStateInformation(saved);
    auto copy = std::make_unique<Processor>(); copy->setStateInformation(saved.getData(), int(saved.getSize())); verify(*copy);
    check(copy->pasteSettings(owner->settingsText()), "Dry/Wet copy/paste failed"); verify(*copy);
    TemporaryFolder files; PresetManager manager(*owner, files.root.getChildFile("drywet.settings"));
    const auto text = manager.captureText("Dry/Wet instances");
    check(owner->setRoutingGraph(defaultGraph(), error), "Dry/Wet preset fixture reset failed");
    check(manager.loadText(text).wasOk(), "Dry/Wet preset recall failed"); verify(*owner);
    // Legacy graph-only state seeds the existing mappings from node-authored balances.
    auto legacy = juce::ValueTree::fromXml(*juce::parseXML(owner->settingsText()));
    for (int i = legacy.getNumChildren(); --i >= 0;) {
        const auto child = legacy.getChild(i);
        if (child.hasType("PARAM") && child["id"].toString().startsWith("mix") && child["id"].toString().endsWith("Balance"))
            legacy.removeChild(i, nullptr);
    }
    check(copy->pasteSettings(legacy.toXmlString()), "Dry/Wet legacy graph-only recall failed"); verify(*copy);
    // Whole-plugin bypass remains latency-aligned dry, regardless of routed blends.
    auto* bypass = owner->parameters.getParameter("bypass");
    bypass->setValueNotifyingHost(1.f);
    owner->prepareToPlay(48000, 127); owner->reset();
    const int latency = owner->getLatencySamples();
    juce::MidiBuffer midi;
    for (int position = 0; position < latency + 800;) {
        const int count = std::min(position % 2 ? 7 : 127, latency + 800 - position);
        juce::AudioBuffer<float> audio(2, count); audio.clear();
        if (!position) { audio.setSample(0, 0, .25f); audio.setSample(1, 0, -.125f); }
        owner->processBlockBypassed(audio, midi);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < count; ++i)
            check(std::abs(audio.getSample(ch, i) - (position + i == latency ? (ch ? -.125f : .25f) : 0.f)) < 1.e-6f, "Dry/Wet graph broke full-plugin aligned bypass");
        position += count;
    }
}
} // namespace
int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    try {
        dryWetRecall(); codec(); processorRecall(); presets();
        std::cout << "Splitter state: codecs, legacy compatibility, host recall, unchanged parameters and preset validation passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
