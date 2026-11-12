#include "PluginProcessor.h"
#include "CreativeEffectPanel.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace vocalstack;
namespace {
void expect(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Graph graph(NodeType type = NodeType::granular) {
    return {{{1, NodeType::lead}, {100, type}, {13, NodeType::output}},
            {{1, 100, 0}, {100, 13, 0}}};
}
void bridgeOwnership() {
    GrainSnapshotBridge bridge;
    RoutingGrainFrame out;
    expect(!bridge.read(1, out, 1, 1), "unpublished bridge is empty");
    std::atomic<bool> done{false}, failed{false};
    std::atomic<unsigned> reads{0};
    const auto reader = [&] {
        while (!done.load()) {
            RoutingGrainFrame frame;
            if (!bridge.read(1, frame, 1, 1)) continue;
            reads.fetch_add(1, std::memory_order_relaxed);
            if (frame.nodeId != 100 || frame.grains.sample != frame.generation || frame.grains.count != 32) failed.store(true);
            for (const auto& grain : frame.grains.grains)
                if (grain.position != float(frame.generation) || grain.step != -float(frame.generation)) failed.store(true);
        }
    };
    std::thread first(reader), second(reader);
    for (uint64_t i = 1; i < 20000; ++i) {
        RoutingGrainSnapshot frame; frame.revision = frame.resetEpoch = 1;
        auto& node = frame.nodes[1]; node.nodeId = 100; node.generation = node.grains.sample = i; node.grains.count = 32;
        for (auto& grain : node.grains.grains) { grain.position = float(i); grain.step = -float(i); }
        bridge.publish(frame);
    }
    done.store(true); first.join(); second.join();
    expect(reads.load() > 0 && !failed.load(), "concurrent readers never observe torn plain payloads");
    expect(!bridge.read(1, out, 2, 1) && !bridge.read(1, out, 1, 2), "revision and reset invalidate stale data");
    expect(!bridge.read(16, out, 1, 1) && !bridge.read(2, out, 1, 1), "invalid and absent slots rejected");
}
uint64_t hash(const juce::Image& image) {
    uint64_t result = 0;
    for (int y = 0; y < image.getHeight(); ++y) for (int x = 0; x < image.getWidth(); ++x)
        result = result * 31 + image.getPixelAt(x, y).getARGB();
    return result;
}
void integration() {
    auto storage = std::make_unique<Processor>(); auto& owner = *storage;
    owner.prepareToPlay(48000, 257);
    juce::String error; expect(owner.setRoutingGraph(graph(), error), "install granular");
    RoutingGrainFrame frame;
    expect(!owner.readGrainSnapshot(1, frame), "pending plan cannot display old data");
    juce::AudioBuffer<float> audio(2, 257); juce::MidiBuffer midi;
    const auto render = [&] {
        for (int i = 0; i < audio.getNumSamples(); ++i) { audio.setSample(0, i, .2f); audio.setSample(1, i, -.1f); }
        owner.processBlock(audio, midi);
    };
    for (int i = 0; i < 40; ++i) render();
    expect(owner.readGrainSnapshot(1, frame) && frame.grains.count > 0, "Processor forwards real audio grains");
    const auto generation = frame.generation;
    juce::Component root; root.setBounds(0, 0, 900, 540); root.setVisible(true); root.addToDesktop(0);
    CreativeEffectVisual routing(owner, 1, CreativeKind::granular), stack(owner, 1, CreativeKind::granular);
    root.addAndMakeVisible(routing); root.addAndMakeVisible(stack);
    routing.setBounds(0, 0, 440, 238); stack.setBounds(450, 0, 440, 238);
    std::array<uint64_t, 3> themeHashes{};
    for (int skin = 0; skin < 3; ++skin) {
        root.getProperties().set(vocalstackThemeProperty, skin);
        owner.setSkin(skin); routing.setNeon(skin == 1); stack.setNeon(skin == 1);
        routing.refresh(); stack.refresh();
        const auto a = routing.createComponentSnapshot(routing.getLocalBounds());
        const auto b = stack.createComponentSnapshot(stack.getLocalBounds());
        expect(a.isValid() && hash(a) == hash(b), "Routing and Stack render the identical snapshot in every theme");
        themeHashes[size_t(skin)] = hash(a);
        const std::array<const char*, 3> names{"device", "neon", "spectral"};
        auto file = juce::File::getCurrentWorkingDirectory().getChildFile(
            juce::String("grain-live-") + names[size_t(skin)] + ".png");
        auto stream = file.createOutputStream();
        expect(stream && stream->openedOk(), "open live grain screenshot");
        stream->setPosition(0); stream->truncate();
        expect(juce::PNGImageFormat().writeImageToStream(a, *stream), "save actual live grain visual");
    }
    expect(themeHashes[0] != themeHashes[1] && themeHashes[1] != themeHashes[2]
        && themeHashes[0] != themeHashes[2], "Device/Neon/Spectral palettes actually differ");
    const auto last = hash(routing.createComponentSnapshot(routing.getLocalBounds()));
    routing.setBounds(0, 600, 440, 238);
    expect(!routing.visibleInHierarchy(), "viewport clipping detected");
    render(); routing.refresh();
    expect(hash(routing.createComponentSnapshot(routing.getLocalBounds())) == last, "offscreen refresh performs no snapshot/model work");
    routing.setBounds(0, 0, 440, 238); routing.refresh();
    const auto currentImage = hash(routing.createComponentSnapshot(routing.getLocalBounds()));
    expect(owner.setRoutingGraph(graph(NodeType::delay), error), "replace granular");
    expect(!owner.readGrainSnapshot(1, frame), "old granular data hidden immediately on request");
    expect(hash(routing.createComponentSnapshot(routing.getLocalBounds())) != currentImage,
           "paint rejects invalidated cache even before timer refresh");
    render(); expect(!owner.readGrainSnapshot(1, frame), "replacement has no grains");
    expect(owner.setRoutingGraph(graph(), error), "reuse granular slot"); render();
    expect(owner.readGrainSnapshot(1, frame) && frame.generation != generation, "slot reuse has fresh generation");
    owner.reset(); expect(owner.readGrainSnapshot(1, frame) && frame.grains.count == 0 && frame.grains.capturedFrames == 0, "reset publishes empty capture");
    owner.releaseResources(); root.removeFromDesktop();
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI init;
    try { bridgeOwnership(); integration(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Grain publication concurrency/Processor/UI/theme/offscreen tests passed\n";
}
