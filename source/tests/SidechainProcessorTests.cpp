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
    check(spectralMorphParameterStart==2252 && owner->getParameters().size()==2342, "append-only Morph parameter count");
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
    try { busMatrix(); activationLatencyPolicy(); spectralContract(); check(!allocations && !destructions, "processor callback allocated/reclaimed"); std::cout << "Sidechain JUCE processor bus/state tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
