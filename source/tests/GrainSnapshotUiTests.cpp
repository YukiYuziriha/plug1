#include "PluginProcessor.h"
#include "CreativeEffectPanel.h"
#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

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
void save(juce::Component& component, const juce::String& name) {
    const auto image = component.createComponentSnapshot(component.getLocalBounds());
    auto file = juce::File::getCurrentWorkingDirectory().getChildFile(name + ".png");
    auto stream = file.createOutputStream();
    expect(stream && stream->openedOk(), "open screenshot");
    stream->setPosition(0); stream->truncate();
    expect(juce::PNGImageFormat().writeImageToStream(image, *stream), "write screenshot");
}
void parameter(Processor& owner, const juce::String& id, float value) {
    auto* p = owner.parameters.getParameter(id);
    expect(p != nullptr, "fixture parameter exists");
    p->setValueNotifyingHost(p->convertTo0to1(value));
}
void creative(Processor& owner, CreativeParameter id, float value) {
    parameter(owner, creativeParameterId(1, size_t(id)), value);
}
juce::Component* find(juce::Component& root, const juce::String& id) {
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren()) if (auto* result = find(*child, id)) return result;
    return nullptr;
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
    const auto still = hash(routing.createComponentSnapshot(routing.getLocalBounds()));
    routing.refresh();
    expect(hash(routing.createComponentSnapshot(routing.getLocalBounds())) == still, "no invented grain motion without audio");
    for (int i = 0; i < 5; ++i) render();
    routing.refresh();
    expect(hash(routing.createComponentSnapshot(routing.getLocalBounds())) != still, "new audio snapshot visibly moves grains");
    for (bool dense : {false, true}) {
        creative(owner, CreativeParameter::granularDensity, dense ? 80.f : 12.f);
        creative(owner, CreativeParameter::granularSize, dense ? 500.f : 120.f);
        creative(owner, CreativeParameter::granularSpread, 100);
        creative(owner, CreativeParameter::granularReverse, dense ? 1.f : 0.f);
        for (int i = 0; i < 150; ++i) render();
        expect(owner.readGrainSnapshot(1, frame), "density fixture publishes snapshot");
        expect(dense ? frame.grains.count >= 20 : frame.grains.count < 8, "real sparse and dense DSP fixtures");
        for (int skin = 0; skin < 3; ++skin) for (int width : {174, 204, 440}) {
            root.getProperties().set(vocalstackThemeProperty, skin); owner.setSkin(skin);
            routing.setBounds(0, 0, width, width < 300 ? 216 : 238);
            routing.setNeon(skin == 1); routing.refresh();
            save(routing, "grain-" + juce::String(dense ? "dense-" : "sparse-") + juce::String(skin) + "-" + juce::String(width));
        }
    }
    for (int i = 0; i < 800; ++i) render();
    expect(owner.readGrainSnapshot(1, frame) && frame.grains.validFraction == 1, "wrapped capture ring fixture is full");
    routing.refresh(); save(routing, "grain-wrapped");
    creative(owner, CreativeParameter::granularFreeze, 1);
    for (int i = 0; i < 30; ++i) render();
    expect(owner.readGrainSnapshot(1, frame) && frame.grains.frozen, "freeze snapshot is actual DSP state");
    const auto frozenHead = frame.grains.captureHead;
    routing.refresh(); const auto frozenImage = hash(routing.createComponentSnapshot(routing.getLocalBounds()));
    for (int i = 0; i < 5; ++i) render();
    routing.refresh();
    expect(owner.readGrainSnapshot(1, frame) && frame.grains.captureHead == frozenHead, "freeze holds capture head");
    expect(hash(routing.createComponentSnapshot(routing.getLocalBounds())) != frozenImage, "frozen capture still has live grain playback");
    save(routing, "grain-frozen");
    routing.setBounds(0, 0, 440, 238); routing.refresh();
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
    save(routing, "grain-replaced");
    render(); expect(!owner.readGrainSnapshot(1, frame), "replacement has no grains");
    expect(owner.setRoutingGraph(graph(), error), "reuse granular slot"); render();
    expect(owner.readGrainSnapshot(1, frame) && frame.generation != generation, "slot reuse has fresh generation");
    routing.refresh(); const auto beforeReset = hash(routing.createComponentSnapshot(routing.getLocalBounds()));
    owner.reset(); expect(owner.readGrainSnapshot(1, frame) && frame.grains.count == 0 && frame.grains.capturedFrames == 0, "reset publishes empty capture");
    expect(hash(routing.createComponentSnapshot(routing.getLocalBounds())) != beforeReset, "paint invalidates reset before next timer");
    routing.refresh(); save(routing, "grain-reset");
    owner.releaseResources(); root.removeFromDesktop();
}
struct Tempo final : juce::AudioPlayHead {
    double bpm = 90;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info; info.setBpm(bpm); return info;
    }
};
void delayTiming() {
    auto storage = std::make_unique<Processor>(); auto& owner = *storage;
    Tempo tempo; owner.setPlayHead(&tempo); owner.prepareToPlay(48000, 257);
    juce::String error; expect(owner.setRoutingGraph(graph(NodeType::delay), error), "install delay fixture");
    juce::AudioBuffer<float> audio(2, 257); audio.clear(); juce::MidiBuffer midi;
    creative(owner, CreativeParameter::delaySync, 0);
    creative(owner, CreativeParameter::delayTime, 500);
    owner.processBlock(audio, midi);
    juce::Component root; root.setBounds(0, 0, 500, 400); root.setVisible(true); root.addToDesktop(0);
    CreativeEffectVisual visual(owner, 1, CreativeKind::delay);
    root.addAndMakeVisible(visual); visual.setBounds(0, 0, 200, 216);
    visual.refreshAt(1000);
    expect(std::abs(visual.delayIntervalSeconds() - .5) < .00001, "free delay timing uses milliseconds");
    const auto start = hash(visual.createComponentSnapshot(visual.getLocalBounds()));
    save(visual, "delay-free-start");
    visual.refreshAt(1125);
    expect(std::abs(visual.timingPhase() - .25) < .00001, "delay moves quarter interval in 125 ms");
    expect(hash(visual.createComponentSnapshot(visual.getLocalBounds())) != start, "delay playhead actually moves");
    save(visual, "delay-free-moving");
    visual.setBounds(0, 500, 200, 216); visual.refreshAt(1200);
    visual.refreshAt(9000);
    expect(std::abs(visual.timingPhase() - .25) < .00001, "offscreen delay clock does not advance");
    visual.setBounds(0, 0, 200, 216); visual.refreshAt(10000);
    expect(std::abs(visual.timingPhase() - .25) < .00001, "returning onscreen does not catch up hidden time");
    visual.setVisible(false); visual.refreshAt(11000); visual.setVisible(true); visual.refreshAt(12000);
    expect(std::abs(visual.timingPhase() - .25) < .00001, "hidden delay clock pauses");
    creative(owner, CreativeParameter::delaySync, 1);
    creative(owner, CreativeParameter::delayBeats, 1.5f);
    owner.processBlock(audio, midi); visual.refreshAt(12000);
    expect(std::abs(visual.delayIntervalSeconds() - 1) < .00001, "sync uses host 90 BPM and quarter-note multiples");
    visual.refreshAt(12250);
    expect(std::abs(visual.timingPhase() - .5) < .00001, "sync phase follows the actual parameter interval");
    save(visual, "delay-sync-moving");
    parameter(owner, fxParameterId(1, "On"), 0); owner.processBlock(audio, midi); visual.refreshAt(12250);
    visual.refreshAt(12750);
    expect(std::abs(visual.timingPhase() - .5) < .00001, "bypassed schematic is stationary");
    parameter(owner, fxParameterId(1, "On"), 1); owner.processBlock(audio, midi);
    for (int skin = 0; skin < 3; ++skin) for (int width : {174, 204, 440}) {
        root.getProperties().set(vocalstackThemeProperty, skin); visual.setNeon(skin == 1);
        visual.setBounds(0, 0, width, 216); visual.refreshAt(12750);
        save(visual, "delay-sync-" + juce::String(skin) + "-" + juce::String(width));
    }
    tempo.bpm = 20; creative(owner, CreativeParameter::delayBeats, 8);
    owner.processBlock(audio, midi); visual.refreshAt(12750);
    expect(visual.delayIntervalSeconds() == 24, "timing schematic uses DSP 24-second buffer limit");
    tempo.bpm = std::numeric_limits<double>::quiet_NaN();
    owner.processBlock(audio, midi); visual.refreshAt(12750);
    expect(visual.delayIntervalSeconds() == 24, "missing host tempo retains Processor's last valid tempo");
    tempo.bpm = 500;
    owner.processBlock(audio, midi); visual.refreshAt(12750);
    expect(std::abs(visual.delayIntervalSeconds() - 4) < .00001, "out-of-range tempo uses DSP 120 BPM fallback");
    owner.releaseResources(); owner.setPlayHead(nullptr); root.removeFromDesktop();
}
void layouts() {
    auto storage = std::make_unique<Processor>(); auto& owner = *storage;
    owner.prepareToPlay(48000, 257);
    juce::Component root; root.setBounds(0, 0, 900, 540); root.setVisible(true); root.addToDesktop(0);
    for (auto kind : {CreativeKind::granular, CreativeKind::delay, CreativeKind::reverb, CreativeKind::compressor, CreativeKind::bitcrusher}) {
        const auto type = kind == CreativeKind::granular ? NodeType::granular : kind == CreativeKind::delay ? NodeType::delay
            : kind == CreativeKind::reverb ? NodeType::reverb : kind == CreativeKind::compressor ? NodeType::compressor : NodeType::bitcrusher;
        juce::String error; expect(owner.setRoutingGraph(graph(type), error), "install panel snapshot fixture");
        juce::AudioBuffer<float> audio(2, 257); juce::MidiBuffer midi;
        for (int i = 0; i < 40; ++i) { audio.clear(); owner.processBlock(audio, midi); }
        CreativeEffectPanel panel(owner, 1, kind); root.addAndMakeVisible(panel);
        for (int skin = 0; skin < 3; ++skin) for (int width : {190, 220, 700}) {
            root.getProperties().set(vocalstackThemeProperty, skin); owner.setSkin(skin);
            panel.setBounds(0, 0, width, 460); panel.setNeon(skin == 1);
            auto* viewport = dynamic_cast<juce::Viewport*>(panel.getChildComponent(0));
            expect(viewport != nullptr, "full creative body has a viewport");
            viewport->setViewPosition(0, 0);
            auto* visual = dynamic_cast<CreativeEffectVisual*>(viewport->getViewedComponent()->getChildComponent(1));
            expect(visual != nullptr, "panel contains shared creative visual");
            visual->refresh();
            if (kind == CreativeKind::delay && skin == 0 && width == 190) {
                const auto phase = visual->timingPhase();
                expect(visual->visibleInHierarchy(), "timer fixture starts in visible viewport");
                // A previous suite may leave a native timer message in flight;
                // service due timers until the new panel receives its first tick.
                for (int attempt = 0; attempt < 50 && visual->timingPhase() == phase; ++attempt) {
                    juce::Thread::sleep(20); juce::Timer::callPendingTimersSynchronously();
                }
                expect(visual->timingPhase() != phase, "panel timer really advances visible delay without parameter edits");
                viewport->setViewPosition(0, viewport->getViewedComponent()->getHeight());
                juce::Timer::callPendingTimersSynchronously();
                const auto paused = visual->timingPhase();
                juce::Thread::sleep(80); juce::Timer::callPendingTimersSynchronously();
                expect(visual->timingPhase() == paused, "panel timer skips viewport-clipped delay");
                viewport->setViewPosition(0, 0); visual->refresh();
            }
            save(panel, "creative-card-" + CreativeEffectPanel::effectName(kind) + "-" + juce::String(skin) + "-" + juce::String(width));
            auto* content = viewport->getViewedComponent();
            std::vector<juce::String> ids{fxParameterId(1, "Mix")};
            for (size_t i = 0; i < creativeParameterCount; ++i)
                if (creativeParameters[i].kind == kind) ids.push_back(creativeParameterId(1, i));
            for (const auto& id : ids) {
                auto* control = find(panel, id);
                expect(control != nullptr, "all creative parameters retain attached controls");
                const auto bounds = content->getLocalArea(control, control->getLocalBounds());
                expect(bounds.getX() >= 0 && bounds.getRight() <= content->getWidth(), "control fits without horizontal scroll");
                panel.revealParameter(id);
                const auto visible = viewport->getLocalArea(control, control->getLocalBounds());
                expect(viewport->getLocalBounds().contains(visible), "every parameter is fully scroll reachable");
                expect(!control->getWantsKeyboardFocus(), "ordinary controls preserve host keyboard focus");
            }
            save(panel, "creative-card-bottom-" + CreativeEffectPanel::effectName(kind) + "-" + juce::String(skin) + "-" + juce::String(width));
            if (kind == CreativeKind::delay) {
                const auto syncId = creativeParameterId(1, size_t(CreativeParameter::delaySync));
                auto* sync = dynamic_cast<juce::Button*>(find(panel, syncId));
                auto* time = find(panel, creativeParameterId(1, size_t(CreativeParameter::delayTime)));
                auto* beats = find(panel, creativeParameterId(1, size_t(CreativeParameter::delayBeats)));
                expect(sync && time && beats, "delay mode controls exist");
                // A wide card needs a shorter viewport to clip the whole visual,
                // not merely its top edge, while keeping Sync reachable.
                panel.setSize(width, 180);
                const auto awaitMode = [&](bool synced) {
                    for (int attempt = 0; attempt < 50
                         && (time->isEnabled() != !synced || beats->isEnabled() != synced); ++attempt) {
                        juce::Thread::sleep(20); juce::Timer::callPendingTimersSynchronously();
                    }
                    expect(time->isEnabled() == !synced && beats->isEnabled() == synced,
                           "Time/Beats enablement follows current Sync");
                };
                for (bool hostEdit : {false, true}) for (bool initial : {false, true}) {
                    creative(owner, CreativeParameter::delaySync, initial ? 1.f : 0.f);
                    audio.clear(); owner.processBlock(audio, midi);
                    viewport->setViewPosition(0, 0);
                    visual->refresh(); awaitMode(initial);
                    expect(visual->visibleInHierarchy() && visual->delaySynced() == initial,
                           "each Sync direction seeds the opposite cached visual mode");
                    panel.revealParameter(syncId);
                    expect(panel.isShowing() && visual->isShowing() && !visual->visibleInHierarchy(),
                           "Sync fixture clips visual by scrolling, not by hiding the panel");
                    const auto paused = visual->timingPhase();
                    const bool synced = !initial;
                    if (hostEdit) creative(owner, CreativeParameter::delaySync, synced ? 1.f : 0.f);
                    else sync->setToggleState(synced, juce::sendNotificationSync);
                    expect(owner.parameters.getParameter(syncId)->getValue() == (synced ? 1.f : 0.f),
                           "Sync edit reaches attached parameter");
                    // No audio block: unassigned Sync must use the current host
                    // base value even before the next DSP publication.
                    awaitMode(synced);
                    expect(!visual->visibleInHierarchy(), "mode update never returns to the visual");
                    expect(visual->delaySynced() == initial,
                           "clipped visual model remains stale while controls update in both directions");
                    expect(visual->timingPhase() == paused, "mode controls update without advancing clipped animation");
                }
            }
        }
    }
    owner.releaseResources(); root.removeFromDesktop();
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI init;
    try { bridgeOwnership(); integration(); delayTiming(); layouts(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Grain publication/live cloud/freeze/reset/themes/compact scrolling/delay timing/clipped Sync UI and host edits tests passed\n";
}
