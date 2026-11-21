#include "PluginProcessor.h"
#include "ProcessorRoutingState.h"
#include "MainBusFxRack.h"
#include <atomic>
#include <cstdlib>
#include <new>
#include <array>
#include <iostream>
#include <stdexcept>

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

void monoAuditionAndGateBridge() {
    auto owner = std::make_unique<Processor>();
    check(stereoMonoParameterStart == 2342 && parameterIds.size() == 2357, "Mono Audition must append after every released index");
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
        const int base = rate == 96000 ? 2688 : 1344;
        check(owner->getLatencySamples() == base, "no-Morph graph retained the 4096 penalty");
        auto morph = mainBusFx::add(defaultGraph(), NodeType::spectralMorph);
        check(morph.changed, "latency Morph fixture");
        // Even dormant or disconnected Morph must reserve its eventual active
        // delay, so enabling/wiring it later is a same-contract operation.
        for (auto& node : morph.graph.nodes) if (node.type == NodeType::spectralMorph) node.enabled = false;
        juce::String error;
        check(owner->setRoutingGraph(morph.graph, error) && error.isEmpty(), "live Morph draft rejected");
        check(owner->routingRestartPending(), "latency change was not labeled pending");
        auto rejected = morph.graph; rejected.nodes.push_back({101, NodeType::spectralMorph});
        for (auto& link : rejected.links) if (link.source == 100 && link.destination == 13) link.source = 101;
        rejected.links.push_back({100, 101, 0});
        check(!owner->setRoutingGraph(rejected, error) && error.isNotEmpty() && owner->routingRestartPending(),
              "rejected serial Morph destroyed the explicit pending activation state");
        check(owner->getRoutingGraph().nodes.size() == morph.graph.nodes.size(), "rejected pending edit changed saved topology");
        check(owner->getLatencySamples() == base, "latency changed before host reprepare");
        const auto checkBypass = [&](int expectedLatency) {
            auto* bypass = owner->parameters.getParameter("bypass");
            check(bypass != nullptr, "bypass parameter fixture");
            bypass->setValueNotifyingHost(1.f);
            owner->reset();
            constexpr std::array<int, 5> blocks{1, 137, 3, 511, 17};
            for (int position = 0, block = 0; position < expectedLatency + 600; ++block) {
                const int count = std::min(blocks[size_t(block) % blocks.size()], expectedLatency + 600 - position);
                juce::AudioBuffer<float> audio(channels, count); audio.clear();
                if (position == 0) for (int c = 0; c < channels; ++c) audio.setSample(c, 0, c ? -.25f : .4f);
                juce::MidiBuffer midi;
                audioThread = true; owner->processBlockBypassed(audio, midi); audioThread = false;
                for (int c = 0; c < channels; ++c) for (int i = 0; i < count; ++i) {
                    const float expected = position + i == expectedLatency ? (c ? -.25f : .4f) : 0.f;
                    check(std::abs(audio.getSample(c, i) - expected) < 1.e-6f, "audio delay moved before/away from host latency");
                }
                position += count;
            }
        };
        checkBypass(base);
        check(owner->routingRestartPending(), "reset/transport-style reset applied pending topology");
        juce::MemoryBlock pendingState; owner->getStateInformation(pendingState);
        auto recalled = std::make_unique<Processor>();
        recalled->setStateInformation(pendingState.getData(), int(pendingState.getSize()));
        recalled->prepareToPlay(rate, 137);
        check(recalled->getLatencySamples() == base + 4096, "pending topology was lost on state recall");
        owner->releaseResources(); owner->prepareToPlay(rate, 137);
        check(!owner->routingRestartPending() && owner->getLatencySamples() == base + 4096, "host activation failed to apply Morph latency");
        checkBypass(base + 4096);
        auto enabled = owner->getRoutingGraph();
        for (auto& node : enabled.nodes) if (node.type == NodeType::spectralMorph) node.enabled = true;
        check(owner->setRoutingGraph(enabled, error) && !owner->routingRestartPending(), "Morph enable unexpectedly changed latency contract");
        check(owner->setRoutingGraph(defaultGraph(), error) && owner->routingRestartPending(), "Morph removal not deferred");
        check(owner->getLatencySamples() == base + 4096, "Morph removal changed latency before activation");
        checkBypass(base + 4096);
        owner->releaseResources(); owner->prepareToPlay(rate, 137);
        check(owner->getLatencySamples() == base && !owner->routingRestartPending(), "Morph removal failed to restore legacy latency");
        checkBypass(base);
    }
}

void spectralContract() {
    auto owner=std::make_unique<Processor>();
    check(spectralMorphParameterStart==2252 && owner->getParameters().size()==2357, "append-only Morph parameter count");
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
    int inputs=0; for(const auto& l:rack.graph.links) if(l.destination==id) { ++inputs; check(l.inputPin==0,"rack invented donor wire"); }
    check(inputs==1,"rack did not create exactly one MAIN wire");
    rack.graph.nodes.push_back({14,NodeType::sidechain}); rack.graph.links.push_back({14,id,1});
    check(!mainBusFx::discover(rack.graph).editable(),"wired auxiliary rack edits must reject rather than erase donor cables");
    check(!mainBusFx::remove(rack.graph,id).changed,"read-only donor graph was rewritten");
    auto branch = mainBusFx::add(defaultGraph(),NodeType::spectralMorph);
    branch.graph.nodes.push_back({101,NodeType::gain});
    branch.graph.links.push_back({12,101,0}); branch.graph.links.push_back({101,id,1});
    const auto discovered = mainBusFx::discover(branch.graph);
    check(!discovered.editable() && std::find(discovered.effects.begin(),discovered.effects.end(),101)==discovered.effects.end(),
        "analysis-only donor FX was treated as an audible main-bus rack stage");
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
int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    try { busMatrix(); monoAuditionAndGateBridge(); activationLatencyPolicy(); spectralContract(); check(!allocations && !destructions, "processor callback allocated/reclaimed"); std::cout << "Sidechain JUCE processor bus/state tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
