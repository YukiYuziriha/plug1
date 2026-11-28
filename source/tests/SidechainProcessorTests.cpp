#include "PluginProcessor.h"
#include "ProcessorRoutingState.h"
#include "MainBusFxRack.h"
#include "SidechainWorkflowUiTests.h"
#include <atomic>
#include <cstdlib>
#include <new>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace { thread_local bool audioThread = false; unsigned allocations=0, destructions=0; }
void* operator new(std::size_t n) { if(audioThread) ++allocations; if(auto* p=std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { if(audioThread && p) ++destructions; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
namespace {
using namespace vocalstack;
void check(bool x, const char* reason) { if (!x) throw std::runtime_error(reason); }
struct ActivationListener : juce::AudioProcessorListener {
    explicit ActivationListener(Processor& owner) : processor(owner) { processor.addListener(this); }
    ~ActivationListener() override { processor.removeListener(this); }
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor* owner, const ChangeDetails& details) override {
        if (details.latencyChanged) {
            ++requests;
            latencyAtRequest = owner->getLatencySamples();
            allOnMessageThread &= juce::MessageManager::getInstance()->isThisTheMessageThread();
        }
    }
    Processor& processor;
    int requests = 0;
    int latencyAtRequest = -1;
    bool allOnMessageThread = true;
};

void awaitLatencyRequest(const ActivationListener& listener, int previousRequests, int activeLatency) {
    // Pump the real JUCE timer on the message thread, not Processor internals.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1500.0;
    while (listener.requests == previousRequests && juce::Time::getMillisecondCounterHiRes() < deadline) {
        juce::Timer::callPendingTimersSynchronously();
        if (listener.requests == previousRequests) juce::Thread::sleep(10);
    }
    check(listener.requests == previousRequests + 1 && listener.allOnMessageThread
        && listener.latencyAtRequest == activeLatency, "expected one message-thread latency request at the active latency");
}

void checkLatencyImpulse(Processor& owner, int channels, int latency, bool hostBypass = true) {
    auto* bypass = owner.parameters.getParameter("bypass");
    check(bypass != nullptr, "bypass parameter fixture");
    const auto savedBypass = bypass->getValue();
    if (hostBypass) bypass->setValueNotifyingHost(1.f); // Settle the bypass blend at reset.
    owner.reset();
    check(owner.getLatencySamples() == latency, "transport reset changed advertised latency");
    constexpr std::array<int, 5> blocks{1, 137, 3, 511, 17};
    for (int position = 0, block = 0; position < latency + 600; ++block) {
        const int count = std::min(blocks[size_t(block) % blocks.size()], latency + 600 - position);
        juce::AudioBuffer<float> audio(channels, count); audio.clear();
        if (position == 0) for (int c = 0; c < channels; ++c) audio.setSample(c, 0, c ? -.25f : .4f);
        juce::MidiBuffer midi;
        audioThread = true;
        if (hostBypass) owner.processBlockBypassed(audio, midi);
        else owner.processBlock(audio, midi);
        audioThread = false;
        for (int c = 0; c < channels; ++c) for (int i = 0; i < count; ++i) {
            const float expected = position + i == latency ? (c ? -.25f : .4f) : 0.f;
            check(std::abs(audio.getSample(c, i) - expected) < 1.e-6f, "audio delay differs from active host latency");
        }
        position += count;
    }
    if (hostBypass) bypass->setValueNotifyingHost(savedBypass);
}

void requestParameterLatency(Processor& owner, const ActivationListener& listener, const char* id,
                             float value, int activeLatency) {
    auto* parameter = owner.parameters.getParameter(id);
    check(parameter != nullptr && owner.getLatencySamples() == activeLatency, "latency parameter fixture");
    const int previousRequests = listener.requests;
    juce::AudioBuffer<float> silence(2, 137); silence.clear();
    juce::MidiBuffer midi;
    // Host automation can arrive off the message thread, immediately before render.
    std::thread automated([&] {
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        audioThread = true; owner.processBlock(silence, midi); audioThread = false;
    });
    automated.join();
    check(listener.requests == previousRequests && owner.getLatencySamples() == activeLatency,
          "automation or render synchronously notified the host or changed active latency");
    awaitLatencyRequest(listener, previousRequests, activeLatency);
    check(owner.getLatencySamples() == activeLatency, "timer applied latency before prepare");
}
float donorSample(int channel, int sample) {
    return channel == 0 ? .13f * std::sin(sample * .063f) : -.21f * std::cos(sample * .087f);
}
void busMatrix() {
    for (double rate : {44100., 48000., 96000.}) for (int mainChannels : {1, 2})
        for (int outputChannels : {1, 2}) for (int auxiliaryChannels : {0, 1, 2}) {
        auto owner = std::make_unique<Processor>();
        auto layout = owner->getBusesLayout();
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(mainChannels));
        layout.inputBuses.set(1, juce::AudioChannelSet::canonicalChannelSet(auxiliaryChannels));
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(outputChannels));
        check(owner->setBusesLayout(layout), "supported optional sidechain layout rejected");
        owner->prepareToPlay(rate, 137);
        Graph graph{{{14, NodeType::sidechain}, {13, NodeType::output}}, {{14, 13, 0}}};
        juce::String error;
        check(owner->setRoutingGraph(graph, error), "processor rejected external source graph");
        const int latency = owner->getLatencySamples();
        auto pendingMorph = graph;
        pendingMorph.nodes.push_back({100, NodeType::spectralMorph});
        pendingMorph.links = {{14, 100, 0}, {100, 13, 0}};
        check(owner->setRoutingGraph(pendingMorph, error) && owner->routingRestartPending(), "external matrix pending Morph rejected");
        check(owner->getLatencySamples() == latency, "pending graph altered active host compensation");
        const int length = latency + 1500;
        constexpr std::array<int, 7> blocks{1, 137, 3, 511, 17, 63, 257};
        float maximumError = 0;
        for (int position = 0, block = 0; position < length; ++block) {
            const int count = std::min(blocks[size_t(block % blocks.size())], length - position);
            juce::AudioBuffer<float> audio(std::max(mainChannels + auxiliaryChannels, outputChannels), count);
            audio.clear();
            for (int c = 0; c < mainChannels; ++c)
                for (int i = 0; i < count; ++i) audio.setSample(c, i, c ? -.33f : .41f);
            for (int c = 0; c < auxiliaryChannels; ++c)
                for (int i = 0; i < count; ++i) audio.setSample(mainChannels + c, i, donorSample(c, position + i));
            juce::MidiBuffer midi;
            audioThread=true; owner->processBlock(audio, midi); audioThread=false;
            for (int c = 0; c < outputChannels; ++c) for (int i = 0; i < count; ++i) {
                const int t = position + i - latency;
                float expected = 0;
                if (auxiliaryChannels && t >= 0) {
                    if (outputChannels == 1 && auxiliaryChannels == 2)
                        expected = .5f * (donorSample(0, t) + donorSample(1, t));
                    else expected = donorSample(auxiliaryChannels == 1 ? 0 : c, t);
                }
                maximumError = std::max(maximumError, std::abs(audio.getSample(c, i) - expected));
            }
            position += count;
        }
        check(maximumError < 2.e-5f, "host buffer base channels, stereo identity or absence mismatch");
        // This was real non-bypassed source audio: the pending Morph must not
        // install or silence/re-delay it before host compensation can change.
        check(owner->setRoutingGraph(graph, error) && !owner->routingRestartPending(), "pending Morph cancellation failed");
        // Bus availability is not preset state. The source and wires are.
        juce::MemoryBlock state;
        owner->getStateInformation(state);
        auto restored = std::make_unique<Processor>();
        restored->setStateInformation(state.getData(), int(state.getSize()));
        const auto recalled = restored->getRoutingGraph();
        check(recalled.nodes.size() == 2 && recalled.links.size() == 1
            && recalled.nodes[0].type == NodeType::sidechain
            && recalled.links[0].source == 14, "source / wire recall mismatch");
        check(restored->getChannelCountOfBus(true, 1) == 0, "preset invented host bus availability");
        owner->releaseResources();
        owner->prepareToPlay(rate, 1);
        juce::AudioBuffer<float> silence(std::max(mainChannels + auxiliaryChannels, outputChannels), 1);
        silence.clear(); juce::MidiBuffer midi;
        owner->processBlock(silence, midi);
        for (int c = 0; c < outputChannels; ++c) check(silence.getSample(c, 0) == 0, "reprepare leaked auxiliary history");
    }
}

void sidechainBlendContract() {
    static_assert(sidechainBlendParameterIndex == 2447 && parameterIds.size() == 3177);
    static_assert(int(NodeType::sidechainBlend) == 25 && sidechainBlendNodeId == 15);
    const auto setPercent = [](Processor& owner, const char* id, float percent) {
        auto* p = owner.parameters.getParameter(id);
        check(p != nullptr, "Sidechain Blend fixture parameter missing");
        p->setValueNotifyingHost(p->convertTo0to1(percent));
    };
    const auto topology = [](const Graph& graph) {
        return processorRouting::encode(graph, "RoutingGraph");
    };
    const auto canonical = defaultGraph(false);
    auto owner = std::make_unique<Processor>();
    auto* blend = owner->parameters.getParameter("sidechainBlend");
    check(blend && owner->getParameters().size() == 3177 && owner->getParameters()[2447] == blend
        && blend->isAutomatable() && blend->getDefaultValue() == 0.f, "Sidechain Blend host contract changed");
    juce::String error;
    check(topology(owner->getRoutingGraph()).isEquivalentTo(topology(defaultGraph(false, true))),
        "Fresh processor missing canonical Sidechain Blend routing");
    check(owner->setRoutingGraph(canonical, error) && owner->ensureSidechainBlend(error),
        "Sidechain Blend insertion failed on canonical Lead wiring");
    const auto inserted = owner->getRoutingGraph();
    check(inserted.nodes.size() == canonical.nodes.size() + 2 && inserted.links.size() == canonical.links.size() + 2,
        "Sidechain Blend insertion changed unrelated topology");
    const auto hasLink = [&](uint32_t src, uint32_t dst, int pin) {
        return std::any_of(inserted.links.begin(), inserted.links.end(), [&](const auto& link) {
            return link.source == src && link.destination == dst && link.inputPin == pin && link.sourcePin == 0;
        });
    };
    check(hasLink(1, sidechainBlendNodeId, 0) && hasLink(sidechainNodeId, sidechainBlendNodeId, 1)
        && hasLink(sidechainBlendNodeId, 11, 0) && !hasLink(1, 11, 0), "Sidechain Blend pin wiring changed");
    check(owner->ensureSidechainBlend(error) && topology(owner->getRoutingGraph()).isEquivalentTo(topology(inserted)),
        "Sidechain Blend insertion is not idempotent");

    const auto roundTrip = [&] {
        juce::MemoryBlock state;
        owner->getStateInformation(state);
        auto copy = std::make_unique<Processor>();
        copy->setStateInformation(state.getData(), int(state.getSize()));
        check(copy->getRoutingError().isEmpty()
            && topology(copy->getRoutingGraph()).isEquivalentTo(topology(owner->getRoutingGraph())),
            "Sidechain Blend state recall changed routing");
        return copy;
    };
    setPercent(*owner, "sidechainBlend", 63.f);
    check(std::abs(roundTrip()->settings().sidechainBlend - .63f) < 1.e-6f, "Sidechain Blend automation did not round-trip");
    // A hand-wired Lead path must not be replaced on bus activation or recall.
    auto custom = canonical;
    custom.nodes.push_back({300, NodeType::splitter});
    for (auto& link : custom.links) if (link.source == 1 && link.destination == 11) link.source = 300;
    custom.links.push_back({1, 300, 0});
    check(owner->setRoutingGraph(custom, error), "Custom Lead wiring rejected");
    const auto before = topology(owner->getRoutingGraph());
    check(!owner->ensureSidechainBlend(error) && error.isNotEmpty()
        && topology(owner->getRoutingGraph()).isEquivalentTo(before), "Sidechain Blend disturbed custom Lead wiring");
    roundTrip();
    check(owner->setRoutingGraph(canonical, error), "Could not remove Sidechain Blend for recall fixture");
    roundTrip(); // An intentional removal must not opt into legacy insertion.
    const auto recallLegacy = [&](const Graph& graph) {
        juce::ValueTree legacy("VocalStack");
        legacy.addChild(topology(graph), -1, nullptr);
        auto copy = std::make_unique<Processor>();
        check(copy->pasteSettings(legacy.toXmlString()) && copy->getRoutingError().isEmpty()
            && copy->settings().sidechainBlend == 0.f, "Pre-blend state did not restore with a safe default");
        return topology(copy->getRoutingGraph());
    };
    check(recallLegacy(canonical).isEquivalentTo(topology(inserted)), "Pre-blend canonical routing was not migrated");
    check(recallLegacy(custom).isEquivalentTo(before), "Pre-blend custom routing was modified on migration");

    for (const int channels : {1, 2}) {
        const auto render = [&](bool withBlend, int auxChannels, bool silent, float percent,
                                bool cardOn = false, float cardGain = 0.f) {
            auto proc = std::make_unique<Processor>();
            auto layout = proc->getBusesLayout();
            layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
            layout.inputBuses.set(1, juce::AudioChannelSet::canonicalChannelSet(auxChannels));
            layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
            check(proc->setBusesLayout(layout), "Sidechain Blend bus layout rejected");
            setPercent(*proc, "double", 0.f);
            setPercent(*proc, "levelOn", 0.f);
            setPercent(*proc, "formantOn", 0.f);
            setPercent(*proc, "sidechainBlend", percent);
            setPercent(*proc, "sidechainOn", cardOn ? 1.f : 0.f);
            setPercent(*proc, "sidechainGain", cardGain);
            check(proc->setRoutingGraph(canonical, error), "Sidechain Blend baseline routing rejected");
            proc->prepareToPlay(channels == 1 ? 44100. : 48000., 137);
            const int latency = proc->getLatencySamples();
            if (withBlend) check(proc->ensureSidechainBlend(error), "Sidechain Blend live insertion rejected");
            check(latency == 0 && !proc->routingRestartPending() && proc->getLatencySamples() == latency,
                "Sidechain Blend added latency or requested a restart");
            constexpr std::array<int, 5> blocks{1, 137, 3, 71, 17};
            std::vector<float> output;
            juce::MidiBuffer midi;
            int position = 0;
            for (const int count : blocks) {
                juce::AudioBuffer<float> audio(channels + auxChannels, count);
                audio.clear();
                for (int c = 0; c < channels; ++c)
                    for (int i = 0; i < count; ++i)
                        audio.setSample(c, i, c == 0 ? .25f : -.2f);
                if (!silent) for (int c = 0; c < auxChannels; ++c)
                    for (int i = 0; i < count; ++i)
                        audio.setSample(channels + c, i, donorSample(c, position + i));
                audioThread = true; proc->processBlock(audio, midi); audioThread = false;
                for (int i = 0; i < count; ++i) for (int c = 0; c < channels; ++c)
                    output.push_back(audio.getSample(c, i));
                position += count;
            }
            proc->releaseResources();
            return output;
        };
        const auto lead = render(false, 0, true, 0.f);
        check(std::abs(lead.back()) > .05f, "Sidechain Blend dry reference was silent");
        check(render(true, 0, true, 100.f) == lead, "Absent sidechain altered Lead at 100% blend");
        check(render(true, channels, false, 0.f) == lead, "0% Sidechain Blend was not exact Lead");
        const auto silent = render(true, channels, true, 100.f);
        check(std::all_of(silent.begin(), silent.end(), [](float sample) { return sample == 0.f; }),
            "Configured silent sidechain did not produce wet silence");
        const auto half = render(true, channels, false, 50.f);
        const auto wet = render(true, channels, false, 100.f);
        check(render(true, channels, false, 100.f, true) == wet, "Neutral Sidechain card did not preserve the wet signal");
        check(render(true, channels, false, 100.f, false, -6.f) == wet, "Bypassed Sidechain card changed the wet signal");
        check(render(true, channels, false, 0.f, true, -6.f) == lead, "Sidechain card touched Lead at 0% blend");
        const auto trimmed = render(true, channels, false, 100.f, true, -6.f);
        for (size_t i = 0; i < wet.size(); ++i) {
            const float donor = donorSample(int(i % channels), int(i / channels));
            check(std::abs(trimmed[i] - donor * std::pow(10.f, -.3f)) < 2.e-5f, "Sidechain gain did not trim only the wet signal");
            check(std::abs(wet[i] - donor) < 2.e-5f, "100% Sidechain Blend altered or delayed the donor");
            check(std::abs(half[i] - .5f * (lead[i] + donor)) < 2.e-5f, "50% Sidechain Blend was not linear");
        }
    }
    auto layout = owner->getBusesLayout();
    layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    check(owner->setBusesLayout(layout) && owner->setRoutingGraph(inserted, error), "Sidechain card latency fixture");
    setPercent(*owner, "sidechainPitch", 12.f);
    setPercent(*owner, "sidechainBlend", 100.f);
    const auto preparedLatency = [&] {
        owner->prepareToPlay(48000., 137);
        const auto latency = owner->getLatencySamples();
        owner->releaseResources();
        return latency;
    };
    check(preparedLatency() == 0, "Bypassed Sidechain Pitch reserved latency");
    setPercent(*owner, "sidechainOn", 1.f);
    check(preparedLatency() > 0, "Audible Sidechain Pitch didn't request vocal latency");
    check(roundTrip()->settings().sidechainSettings.pitchSemitones == 12.f, "Sidechain Pitch didn't round-trip");
    setPercent(*owner, "sidechainBlend", 0.f);
    check(preparedLatency() == 0, "Sidechain Pitch at 0% blend reserved latency");
}

void monoAuditionAndGateBridge() {
    auto owner = std::make_unique<Processor>();
    check(stereoMonoParameterStart == 2342 && vocalGlitchParameterStart == 2357, "Mono Audition must append after every released index");
    for (size_t slot = 1; slot < 16; ++slot) {
        auto* p = dynamic_cast<juce::RangedAudioParameter*>(owner->getParameters()[int(2342 + slot - 1)]);
        check(p && p->paramID == stereoMonoParameterId(slot) && p->getDefaultValue() == 0, "Mono Audition index/ID/default mismatch");
    }
    auto layout = owner->getBusesLayout(); layout.inputBuses.set(1, juce::AudioChannelSet::stereo());
    check(owner->setBusesLayout(layout), "Mono Audition donor fixture");
    Graph graph{{{14, NodeType::sidechain}, {100, NodeType::stereo}, {13, NodeType::output}}, {{14, 100, 0}, {100, 13, 0}}};
    graph.nodes[1].mix = .37f;
    graph.nodes[1].utility[UtilityParameter::stereoWidth] = 200;
    graph.nodes[1].monoAudition = true;
    juce::String error; check(owner->setRoutingGraph(graph, error), "Mono Audition graph rejected");
    owner->prepareToPlay(48000, 137); owner->retainFxMonitor(1);
    const auto feed = [&] {
        for (int block = 0; block < 500; ++block) {
            juce::AudioBuffer<float> audio(4, 137); audio.clear();
            for (int i = 0; i < 137; ++i) { audio.setSample(2, i, .6f); audio.setSample(3, i, -.2f); }
            juce::MidiBuffer midi;
            audioThread = true; owner->processBlock(audio, midi); audioThread = false;
        }
    };
    feed(); FxMonitorFrame frame; check(owner->readFxMonitor(1, frame), "Mono Audition output telemetry missing");
    for (size_t i = 0; i < frame.sampleCount; ++i) {
        check(std::abs(frame.output[0][i] - .2f) < 2.e-5f && std::abs(frame.output[1][i] - .2f) < 2.e-5f, "Mono Audition did not fold final post-Mix signal");
    }
    owner->parameters.getParameter("fx1On")->setValueNotifyingHost(0);
    { juce::AudioBuffer<float> audio(4, 1); audio.clear(); audio.setSample(2, 0, .6f); audio.setSample(3, 0, -.2f);
      juce::MidiBuffer midi; audioThread = true; owner->processBlock(audio, midi); audioThread = false;
      check(std::abs(audio.getSample(0, 0) - .2f) < .01f && std::abs(audio.getSample(1, 0) - .2f) < .01f,
            "Stereo slot bypass jumped around the smoothed Mono Audition release"); }
    owner->parameters.getParameter("fx1On")->setValueNotifyingHost(1); feed();
    juce::MemoryBlock state; owner->getStateInformation(state);
    auto restored = std::make_unique<Processor>(); restored->setStateInformation(state.getData(), int(state.getSize()));
    check(restored->parameters.getRawParameterValue(stereoMonoParameterId(1))->load() == 1 && restored->getRoutingGraph().nodes[1].monoAudition, "Mono Audition did not persist in host and graph state");
    owner->parameters.getParameter(stereoMonoParameterId(1))->setValueNotifyingHost(0); feed();
    check(owner->readFxMonitor(1, frame) && std::abs(frame.output[0].back() - frame.output[1].back()) > .5f, "Mono release failed to restore unchanged stereo controls");
    check(std::abs(owner->parameters.getRawParameterValue("fx1Mix")->load() - 37) < .001f, "Mono changed legacy Wet");
    auto legacy = juce::parseXML(restored->settingsText());
    auto tree = juce::ValueTree::fromXml(*legacy);
    for (int i = tree.getNumChildren(); --i >= 0;) if (tree.getChild(i).getProperty("id").toString() == stereoMonoParameterId(1)) tree.removeChild(i, nullptr);
    auto legacyGraph = tree.getChildWithName("RoutingGraph");
    for (auto node : legacyGraph) node.removeProperty("monoAudition", nullptr);
    check(restored->pasteSettings(tree.toXmlString()), "Legacy Mono-default recall failed");
    check(restored->parameters.getRawParameterValue(stereoMonoParameterId(1))->load() == 0, "Missing legacy Mono value did not default off");
    owner->releaseFxMonitor(1); owner->releaseResources();
    graph.nodes[1] = {100, NodeType::gate};
    graph.nodes[1].utility[UtilityParameter::gateThreshold] = 0;
    check(owner->setRoutingGraph(graph, error), "Gate bridge graph");
    owner->prepareToPlay(48000, 137); owner->retainFxMonitor(1); feed();
    check(owner->readFxMonitor(1, frame) && frame.hasGainReduction && frame.utilityHistory.used > 0, "Gate real dynamics/levels did not reach coherent snapshot");
    check(frame.gainReductionDb > 1 && frame.utilityHistory.bins.back().gainReductionDb > 1, "Gate snapshot contains no actual GR");
    check(std::abs(frame.utilityHistory.bins.back().thresholdDb) < .001f, "Gate recorded threshold differs from smoothed DSP threshold");
    owner->parameters.getParameter("fx1Mix")->setValueNotifyingHost(0);
    { juce::AudioBuffer<float> audio(4, 1); audio.clear(); audio.setSample(2, 0, .6f); audio.setSample(3, 0, -.2f);
      juce::MidiBuffer midi; audioThread = true; owner->processBlock(audio, midi); audioThread = false; }
    check(owner->getCreativeGainReduction(1) == 0, "Gate Mix zero retained old scalar GR during the blend ramp");
    feed();
    check(owner->readFxMonitor(1, frame) && frame.gainReductionDb == 0 && frame.utilityHistory.bins.back().gainReductionDb == 0,
          "Gate Mix zero snapshot did not publish zero applied GR");
    owner->parameters.getParameter("fx1On")->setValueNotifyingHost(0); feed();
    check(owner->readFxMonitor(1, frame) && !frame.hasGainReduction && frame.utilityHistory.bins.back().gainReductionDb == 0, "Gate bypass retained measured GR history");
}

void activationLatencyPolicy() {
    for (double rate : {44100., 48000., 96000.}) for (int channels : {1, 2}) {
        auto owner = std::make_unique<Processor>();
        auto layout = owner->getBusesLayout();
        layout.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        layout.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(channels));
        check(owner->setBusesLayout(layout), "latency policy layout");
        owner->prepareToPlay(rate, 137);
        constexpr int base = 0;
        check(owner->getLatencySamples() == base, "fresh graph retained a latency reserve");
        ActivationListener listener(*owner);
        const auto baseSettings = owner->settingsText();
        auto cableEdit = owner->getRoutingGraph();
        check(!cableEdit.links.empty(), "ordinary cable edit fixture");
        cableEdit.links.pop_back();
        juce::String error;
        check(owner->setRoutingGraph(cableEdit, error) && !owner->routingRestartPending(), "ordinary cable edit rejected");
        check(listener.requests == 0 && owner->getLatencySamples() == base, "ordinary cable edit requested activation");
        auto morph = mainBusFx::add(defaultGraph(), NodeType::spectralMorph);
        check(morph.changed, "latency Morph fixture");
        check(owner->setRoutingGraph(morph.graph, error) && error.isEmpty(), "live Morph draft rejected");
        check(owner->routingRestartPending(), "latency change was not labeled pending");
        awaitLatencyRequest(listener, 0, base);
        auto rejected = morph.graph; rejected.nodes.push_back({101, NodeType::spectralMorph});
        for (auto& link : rejected.links) if (link.source == 100 && link.destination == 13) link.source = 101;
        rejected.links.push_back({100, 101, 0});
        check(!owner->setRoutingGraph(rejected, error) && error.isNotEmpty() && owner->routingRestartPending(),
              "rejected serial Morph destroyed the explicit pending activation state");
        check(owner->getRoutingGraph().nodes.size() == morph.graph.nodes.size(), "rejected pending edit changed saved topology");
        check(listener.requests == 1, "rejected pending edit requested activation");
        check(owner->getLatencySamples() == base, "latency changed before host reprepare");
        checkLatencyImpulse(*owner, channels, base);
        check(owner->routingRestartPending(), "reset/transport-style reset applied pending topology");
        juce::MemoryBlock pendingState; owner->getStateInformation(pendingState);
        auto recalled = std::make_unique<Processor>();
        recalled->prepareToPlay(rate, 137);
        ActivationListener recalledListener(*recalled);
        recalled->setStateInformation(pendingState.getData(), int(pendingState.getSize()));
        check(recalled->routingRestartPending(), "live state load did not stage Morph activation");
        awaitLatencyRequest(recalledListener, 0, base);
        check(recalled->getLatencySamples() == base, "state load changed active latency before prepare");
        recalled->releaseResources(); recalled->prepareToPlay(rate, 137);
        check(recalled->getLatencySamples() == base + 4096, "pending topology was lost on state recall");
        recalledListener.requests = 0;
        check(recalled->pasteSettings(baseSettings) && recalled->routingRestartPending(), "pasted Morph removal not deferred");
        awaitLatencyRequest(recalledListener, 0, base + 4096);
        check(recalled->getLatencySamples() == base + 4096, "paste did not preserve active latency");
        recalled->releaseResources(); recalled->prepareToPlay(rate, 137);
        check(!recalled->routingRestartPending() && recalled->getLatencySamples() == base, "pasted removal failed to apply at prepare");
        owner->releaseResources(); owner->prepareToPlay(rate, 137);
        check(!owner->routingRestartPending() && owner->getLatencySamples() == base + 4096, "host activation failed to apply Morph latency");
        checkLatencyImpulse(*owner, channels, base + 4096);
        listener.requests = 0;
        auto donorEdit = owner->getRoutingGraph();
        donorEdit.links.erase(std::remove_if(donorEdit.links.begin(), donorEdit.links.end(), [](const auto& link) {
            return link.destination == 100 && link.inputPin == 1;
        }), donorEdit.links.end());
        check(owner->setRoutingGraph(donorEdit, error) && !owner->routingRestartPending(), "live donor cable edit rejected");
        check(listener.requests == 0 && owner->getLatencySamples() == base + 4096, "same-latency Morph edit requested activation");
        check(owner->setRoutingGraph(defaultGraph(), error) && owner->routingRestartPending(), "Morph removal not deferred");
        awaitLatencyRequest(listener, 0, base + 4096);
        check(owner->getLatencySamples() == base + 4096, "Morph removal changed latency before activation");
        checkLatencyImpulse(*owner, channels, base + 4096);
        owner->releaseResources(); owner->prepareToPlay(rate, 137);
        check(owner->getLatencySamples() == base && !owner->routingRestartPending(), "Morph removal failed to restore fresh latency");
        checkLatencyImpulse(*owner, channels, base);
    }
}

void delayOnlyAndVocalLatency() {
    auto owner = std::make_unique<Processor>();
    auto delay = mainBusFx::add(owner->getRoutingGraph(), NodeType::delay);
    juce::String error;
    check(delay.changed && owner->setRoutingGraph(delay.graph, error), "fresh Delay-only rack fixture");
    owner->prepareToPlay(48000, 137);
    checkLatencyImpulse(*owner, 2, 0);
    ActivationListener listener(*owner);
    constexpr int vocalLatency = 1344;
    const auto transition = [&](const char* id, float value, int before, int after) {
        requestParameterLatency(*owner, listener, id, value, before);
        checkLatencyImpulse(*owner, 2, before);
        owner->releaseResources();
        check(owner->getLatencySamples() == before, "release changed vocal latency before prepare");
        owner->prepareToPlay(48000, 137);
        checkLatencyImpulse(*owner, 2, after);
    };
    transition("doubleOn", 1, 0, vocalLatency);
    transition("doubleOn", 0, vocalLatency, 0);

    auto* formant = owner->parameters.getParameter("formant");
    check(formant != nullptr, "shared Formant amount fixture");
    formant->setValueNotifyingHost(formant->convertTo0to1(5));
    owner->releaseResources(); owner->prepareToPlay(48000, 137);
    checkLatencyImpulse(*owner, 2, 0); // A dormant shared Formant amount is not a shift.
    transition("formantOn", 1, 0, vocalLatency);
    transition("formantOn", 0, vocalLatency, 0);
    transition("leadPitch", 7, 0, vocalLatency);
    transition("leadPitch", 0, vocalLatency, 0);
}

void morphAutomationLatency() {
    auto owner = std::make_unique<Processor>();
    Graph graph{{{1, NodeType::lead}, {14, NodeType::sidechain}, {100, NodeType::spectralMorph},
                 {13, NodeType::output}}, {{1, 100, 0}, {14, 100, 1}, {100, 13, 0}}};
    graph.nodes[2].enabled = false;
    graph.nodes[2].mix = 0; // An enabled Morph still reserves latency at zero Wet.
    juce::String error;
    check(owner->setRoutingGraph(graph, error), "disabled Morph fixture");
    owner->prepareToPlay(48000, 137);
    checkLatencyImpulse(*owner, 2, 0, false);
    ActivationListener listener(*owner);
    int activeLatency = 0;
    constexpr int morphLatency = 4096;
    for (const bool enabled : {true, false, true, false}) {
        requestParameterLatency(*owner, listener, "fx1On", enabled ? 1.f : 0.f, activeLatency);
        // The running route must stay aligned too, not just host bypass.
        checkLatencyImpulse(*owner, 2, activeLatency, false);
        owner->releaseResources();
        check(owner->getLatencySamples() == activeLatency, "release changed Morph latency before prepare");
        owner->prepareToPlay(48000, 137);
        activeLatency = enabled ? morphLatency : 0;
        checkLatencyImpulse(*owner, 2, activeLatency, false);
    }
}

void legacyLatencyStateRecall() {
    auto owner = std::make_unique<Processor>();
    Graph graph{{{1, NodeType::lead}, {100, NodeType::spectralMorph}, {13, NodeType::output}},
                {{1, 100, 0}, {100, 13, 0}}};
    graph.nodes[1].enabled = false;
    juce::ValueTree legacy(owner->parameters.state.getType());
    legacy.addChild(processorRouting::encode(graph, "RoutingGraph"), -1, nullptr);
    const auto field = [&](const char* id, float value) {
        juce::ValueTree child("PARAM");
        child.setProperty("id", id, nullptr);
        child.setProperty("value", value, nullptr);
        legacy.addChild(child, -1, nullptr);
    };
    field("double", 61);
    field("formant", 5);
    field("doubleOn", 0);
    field("formantOn", 0);
    field("fx1Mix", 0);
    const auto load = [&] {
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), state);
        owner->setStateInformation(state.getData(), int(state.getSize()));
        check(owner->getRoutingError().isEmpty(), "legacy routing recall failed");
        owner->prepareToPlay(48000, 137);
    };
    const auto valueAt = [&](int index, const char* id) {
        auto* p = dynamic_cast<juce::RangedAudioParameter*>(owner->getParameters()[index]);
        check(p && p->paramID == id && owner->parameters.getParameter(id) == p, "released latency parameter index or ID moved");
        return p->convertFrom0to1(p->getValue());
    };
    load(); // With no fx1On field, the legacy node's disabled flag supplies it.
    check(valueAt(94, "fx1On") == 0 && valueAt(95, "fx1Mix") == 0, "legacy Morph slot fallback or Wet recall failed");
    check(std::abs(valueAt(1, "double") - 61) < .001f && std::abs(valueAt(4, "formant") - 5) < .001f
        && valueAt(5, "levelOn") == 1 && valueAt(6, "doubleOn") == 0 && valueAt(7, "formantOn") == 0
        && valueAt(883, "leadPitch") == 0, "legacy vocal controls or missing parameter defaults failed");
    check(owner->getLatencySamples() == 0, "legacy disabled graph-only Morph reserved latency");

    owner->releaseResources();
    field("fx1On", 1); // Explicit host state must win over the same disabled graph node.
    load();
    const auto recalled = owner->getRoutingGraph();
    const auto* morph = mainBusFx::find(recalled, 100);
    check(morph && morph->type == NodeType::spectralMorph && valueAt(94, "fx1On") == 1,
          "legacy recall changed Morph identity or overwrote explicit host enable");
    check(owner->getLatencySamples() == 4096, "stale legacy graph disabled flag overrode fx1On");
}

void spectralContract() {
    auto owner=std::make_unique<Processor>();
    check(spectralMorphParameterStart==2252 && owner->getParameters().size()==3177, "append-only Morph parameter count");
    for(size_t i=0;i<parameterIds.size();++i) {
        auto* p=dynamic_cast<juce::RangedAudioParameter*>(owner->getParameters()[int(i)]);
        check(p && p->paramID==parameterIds[i], "host index differs from explicit parameter map");
    }
    for(size_t slot=1;slot<16;++slot) for(size_t field=0;field<62;++field) {
        const size_t expected=field<48 ? 1007+(slot-1)*48+field : field<51 ? 1727+(slot-1)*3+field-48 : 2087+(slot-1)*11+field-51;
        check(creativeParameterIndex(slot,field)==expected, "released creative slot indices moved");
    }
    auto rack=mainBusFx::add(defaultGraph(),NodeType::spectralMorph);
    check(rack.changed, "rack cannot add independent Morph");
    uint32_t id=0; for(const auto& n:rack.graph.nodes) if(n.type==NodeType::spectralMorph) id=n.id;
    int inputs=0, donorInputs=0;
    for(const auto& l:rack.graph.links) if(l.destination==id) {
        ++inputs;
        if(l.inputPin==1) {
            ++donorInputs;
            const auto* donor=mainBusFx::find(rack.graph,l.source);
            check(donor && donor->type==NodeType::sidechain,"rack donor is not SIDECHAIN");
        } else check(l.inputPin==0,"rack invented Morph input");
    }
    check(inputs==2 && donorInputs==1,"rack did not create MAIN and SIDECHAIN inputs");
    check(mainBusFx::discover(rack.graph).editable(),"automatic SIDECHAIN made the Morph rack read-only");
    const auto removed=mainBusFx::remove(rack.graph,id);
    check(removed.changed && RoutingExecutor::validate(removed.graph),"automatic donor Morph could not be safely removed");
    check(mainBusFx::discover(removed.graph).editable() && mainBusFx::find(removed.graph,14),"Morph removal damaged the rack or SIDECHAIN source");
    check(std::none_of(removed.graph.links.begin(),removed.graph.links.end(),[=](const auto& link) {
        return link.source==id || link.destination==id;
    }),"Morph removal left dangling cables");
    auto branch = mainBusFx::add(defaultGraph(),NodeType::spectralMorph);
    branch.graph.links.erase(std::remove_if(branch.graph.links.begin(),branch.graph.links.end(),[=](const auto& link) {
        return link.destination==id && link.inputPin==1;
    }),branch.graph.links.end());
    branch.graph.nodes.push_back({101,NodeType::gain});
    branch.graph.links.push_back({12,101,0}); branch.graph.links.push_back({101,id,1});
    check(bool(RoutingExecutor::validate(branch.graph)),"branched donor restriction fixture is invalid");
    const auto discovered = mainBusFx::discover(branch.graph);
    check(!discovered.editable() && std::find(discovered.effects.begin(),discovered.effects.end(),101)==discovered.effects.end(),
        "analysis-only donor FX was treated as an audible main-bus rack stage");
    auto pocket = mainBusFx::add(defaultGraph(), NodeType::granular).graph;
    RoutingNode wetDelay; wetDelay.id = 101; wetDelay.type = NodeType::delay; wetDelay.mix = 1; wetDelay.wetOwner = 100;
    pocket.nodes.push_back(wetDelay);
    Graph decoded;
    check(processorRouting::decode(processorRouting::encode(pocket, "RoutingGraph"), decoded), "wet pocket state decode failed");
    check(mainBusFx::find(decoded, 101) && mainBusFx::find(decoded, 101)->wetOwner == 100, "wet ownership lost on recall");
    const auto withoutParent = mainBusFx::remove(decoded, 100);
    check(withoutParent.changed && !mainBusFx::find(withoutParent.graph, 101)
        && bool(RoutingExecutor::validate(withoutParent.graph)), "removing Granular orphaned its wet insert");
    Graph graph{{{1,NodeType::lead},{14,NodeType::sidechain},{100,NodeType::spectralMorph},{13,NodeType::output}},{{1,100,0},{14,100,1},{100,13,0}}};
    graph.nodes[2].mix=1; graph.nodes[2].spectralMorph.morph=.5f;
    juce::String error; check(owner->setRoutingGraph(graph,error),"Morph processor graph rejected");
    ModulationState state; Modulator lfo; lfo.id=101; lfo.phase=.25f; lfo.retrigger=ModulationRetrigger::note;
    state.modulators.push_back(lfo); state.assignments.push_back({101,"fx1MorphAmount",.3f,true});
    check(owner->setModulationState(state,error),"continuous Morph modulation rejected");
    for(const char* id : {"fx1MorphMode","fx1MorphFFT","fx1MorphFreeze"}) {
        auto bad=state; bad.assignments[0].targetParameterId=id;
        check(!owner->setModulationState(bad,error),"discrete Morph control accepted as continuous modulation");
    }
    owner->prepareToPlay(48000,137); const int latency=owner->getLatencySamples();
    juce::AudioBuffer<float> buffer(2,137); juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1,60,juce::uint8(100)),17);
    for(int i=0;i<100;++i) {
        for(int c=0;c<2;++c) for(int j=0;j<137;++j) buffer.setSample(c,j,.1f);
        auto* fft=owner->parameters.getParameter("fx1MorphFFT"); fft->setValueNotifyingHost(fft->convertTo0to1(float(i%3)));
        auto* freeze=owner->parameters.getParameter("fx1MorphFreeze"); freeze->setValueNotifyingHost(i<90 ? 1.f : 0.f);
        audioThread=true; owner->processBlock(buffer,midi); audioThread=false; midi.clear();
        check(owner->getLatencySamples()==latency,"live FFT changed fixed host latency");
    }
    check(owner->parameters.getRawParameterValue("fx1MorphAmount")->load()==50,"modulation mutated host base value");
    SpectralMorphFrame frame; check(owner->readSpectralMorph(1,frame) && !frame.spectrum.frozen,"absent donor captured invented data");
    auto editedGraph=owner->getRoutingGraph();
    editedGraph.links.erase(std::remove_if(editedGraph.links.begin(),editedGraph.links.end(),[](const auto& l) {
        return l.destination==100 && l.inputPin==1;
    }),editedGraph.links.end());
    check(owner->setRoutingGraph(editedGraph,error),"telemetry graph-epoch edit failed");
    check(!owner->readSpectralMorph(1,frame),"stopped audio exposed a stale capture from the previous graph epoch");
    auto* freeze=owner->parameters.getParameter("fx1MorphFreeze"); freeze->setValueNotifyingHost(1.f);
    juce::MemoryBlock saved; owner->getStateInformation(saved);
    auto restored=std::make_unique<Processor>(); restored->setStateInformation(saved.getData(),int(saved.getSize())); restored->prepareToPlay(48000,137);
    for(int i=0;i<40;++i) { buffer.clear(); restored->processBlock(buffer,midi); }
    check(restored->readSpectralMorph(1,frame) && frame.spectrum.capturePending && !frame.spectrum.frozen,
        "recall pretended to restore captured donor data");
}
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;
    try {
        if (argc == 2 && juce::String(argv[1]) == "--gain-rider-check") {
            workflowUiTests::gainRiderReadout();
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--sidechain-blend-check") {
            sidechainBlendContract();
            check(!allocations && !destructions, "Sidechain Blend callback allocated/reclaimed");
            workflowUiTests::sidechainBlendControls();
            return 0;
        }
        if (argc == 2 && juce::String(argv[1]) == "--workflow-check") {
            activationLatencyPolicy(); delayOnlyAndVocalLatency(); morphAutomationLatency(); legacyLatencyStateRecall();
            spectralContract(); sidechainBlendContract(); workflowUiTests::run();
            workflowUiTests::sidechainBlendControls();
            check(!allocations && !destructions, "workflow callback allocated/reclaimed");
            std::cout << "Sidechain activation and wet-pocket workflow checks passed\n"; return 0;
        }
        busMatrix(); monoAuditionAndGateBridge(); activationLatencyPolicy(); delayOnlyAndVocalLatency();
        morphAutomationLatency(); legacyLatencyStateRecall(); spectralContract(); sidechainBlendContract();
        check(!allocations && !destructions, "processor callback allocated/reclaimed");
        std::cout << "Sidechain JUCE processor bus/state tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
