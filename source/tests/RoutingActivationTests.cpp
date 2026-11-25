// Standalone DSP tests; compile as C++20 with source/src on the include path.
// With a JUCE processor target's definitions/link libraries, also run host tests.
#include "Routing.h"
#if JUCE_MODULE_AVAILABLE_juce_audio_processors
#include "PluginProcessor.h"
#include <future>
#include <thread>
#endif
#include <iostream>
#include <stdexcept>

using namespace vocalstack;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Graph morphGraph() {
    return {{{1, NodeType::lead}, {14, NodeType::sidechain}, {100, NodeType::spectralMorph},
             {13, NodeType::output}}, {{1, 100, 0}, {14, 100, 1}, {100, 13, 0}}};
}

void graphContract() {
    constexpr int latency = SpectralMorph::latencySamples;
    auto g = morphGraph();
    g.nodes[2].mix = 0;
    check(RoutingExecutor::graphLatency(g) == latency, "enabled Morph at zero Mix still requires latency");
    g.links.pop_back();
    check(RoutingExecutor::graphLatency(g) == 0, "disconnected Morph contributed latency");
    RoutingExecutor r;
    r.prepare(48000);
    check(bool(r.requestGraph(g)), "disconnected Morph required a reserve");

    // A disabled latency-bearing node doesn't depend on DONOR even when
    // validated alongside an enabled upstream Morph. The old structural
    // serial budget still rejects this topology regardless of the bypass.
    g.nodes.push_back({101, NodeType::spectralMorph});
    g.nodes.back().enabled = false;
    g.links.insert(g.links.end(), {{1, 101, 0}, {100, 101, 1}, {101, 13, 0}});
    check(!RoutingExecutor::validate(g) && RoutingExecutor::graphLatency(g) == latency + 1,
          "bypass weakened the structural serial budget");
    g = morphGraph();
    g.nodes[2].enabled = false;
    check(RoutingExecutor::graphLatency(g) == 0, "disabled Morph reported latency");
    g.nodes[2].latencySamples = 1;
    check(!RoutingExecutor::validate(g), "authored latency accepted on a disabled node");
}

void morphTargets() {
    constexpr int latency = SpectralMorph::latencySamples;
    for (const double rate : {44100., 48000., 96000.}) {
        auto g = morphGraph();
        g.nodes[2].enabled = false;
        RoutingExecutor zero;
        zero.prepare(rate);
        check(bool(zero.requestGraph(g)), "disabled Morph requested a reserve");
        RoutingControls controls;
        controls.fxAutomation = true;
        auto& fx = controls.fxSettings[1];
        fx.enabled = true; fx.mix = 1.f;
        fx.spectralMorph.morph = 1.f;
        RoutingSources s;
        s.sidechainAvailable = true;
        s.sidechain = {-.2f, .1f};
        for (int i = 0; i < latency + 300; ++i) {
            s.lead = {i == 0 ? .3f : 0.f, i == 9 ? -.125f : 0.f};
            const auto out = zero.process(s, controls);
            check(out.left == s.lead.left && out.right == s.lead.right, "live on woke a prepared-disabled Morph");
        }
        check(zero.spectralSnapshots()[1].nodeId == 0, "disabled Morph published an FFT snapshot");

        g.nodes[2].enabled = true;
        RoutingExecutor reserved;
        reserved.prepare(rate, latency);
        g.links.push_back({1, 13, 0}); // Also test delay alignment at a merge.
        check(bool(reserved.requestGraph(g)), "enabled Morph rejected with the correct reserve");
        for (int i = 0; i < 2 * latency + 100; ++i) {
            // Build wet history first, then bypass without changing the plan.
            fx.enabled = i < latency;
            s.lead = {i == latency - 1 ? .3f : 0.f, i == latency + 11 ? -.125f : 0.f};
            const auto out = reserved.process(s, controls);
            if (i >= latency)
                check(out.left == (i == 2 * latency - 1 ? .3f : 0.f)
                    && out.right == (i == 2 * latency + 11 ? -.125f : 0.f),
                    "live off did not preserve exact aligned dry in a prepared-enabled plan");
        }
    }
}

void granularSleep() {
    Graph g{{{1, NodeType::lead}, {100, NodeType::granular}, {13, NodeType::output}},
            {{1, 100, 0}, {100, 13, 0}}};
    g.nodes[1].mix = 0;
    RoutingExecutor r;
    r.prepare(48000);
    check(bool(r.requestGraph(g)), "granular graph rejected");
    RoutingSources sources;
    sources.lead = {.2f, -.1f};
    RoutingControls c;
    c.fxAutomation = true;
    auto& fx = c.fxSettings[1];
    fx.mix = 0;
    for (int i = 0; i < 256; ++i) r.process(sources, c);
    check(r.grainSnapshot().nodes[1].grains.sample == 256, "enabled zero-Mix Granular did not run");
    fx.mix = 1;
    for (int i = 0; i < 4096; ++i) r.process(sources, c);
    const auto before = r.grainSnapshot().nodes[1].grains.sample;
    fx.enabled = false;
    for (int i = 0; i < 10000; ++i) r.process(sources, c);
    const auto sleeping = r.grainSnapshot().nodes[1].grains.sample;
    check(sleeping > before && sleeping < before + 10000, "Granular did not fade then sleep");
    for (int i = 0; i < 1024; ++i) {
        const auto out = r.process(sources, c);
        check(out.left == sources.lead.left && out.right == sources.lead.right, "settled Granular bypass was not dry");
    }
    check(r.grainSnapshot().nodes[1].grains.sample == sleeping, "sleeping Granular kept advancing");
    fx.enabled = true;
    r.process(sources, c);
    check(r.grainSnapshot().nodes[1].grains.sample == sleeping + 1, "Granular did not resume retained state");
}
#if JUCE_MODULE_AVAILABLE_juce_audio_processors
struct ActivationListener : juce::AudioProcessorListener {
    explicit ActivationListener(Processor& p) : processor(p) { processor.addListener(this); }
    ~ActivationListener() override {
        if (reader.joinable()) reader.join();
        processor.removeListener(this);
    }
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor* owner, const ChangeDetails& details) override {
        if (!details.latencyChanged) return;
        ++requests;
        latencyAtRequest = owner->getLatencySamples();
        pendingAtRequest = processor.routingRestartPending();
        onMessageThread &= juce::MessageManager::getInstance()->isThisTheMessageThread();
        if (reader.joinable()) reader.join();
        std::promise<void> read;
        auto completed = read.get_future();
        reader = std::thread([this, read = std::move(read)]() mutable {
            processor.getRoutingGraph(); // Requires controlMutex on a different thread.
            read.set_value();
        });
        outsideLock &= completed.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
    }
    Processor& processor;
    std::thread reader;
    int requests = 0, latencyAtRequest = -1;
    bool pendingAtRequest = false, onMessageThread = true, outsideLock = true;
};

void parameter(Processor& processor, const char* id, float value) {
    auto* p = processor.parameters.getParameter(id);
    check(p != nullptr, "missing activation fixture parameter");
    p->setValueNotifyingHost(p->convertTo0to1(value));
}

void pumpActivationTimers() {
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 120.;
    do {
        juce::Thread::sleep(10);
        juce::Timer::callPendingTimersSynchronously();
    } while (juce::Time::getMillisecondCounterHiRes() < deadline);
}

enum class Edit { reset, add, remove };

void activationNotification(Edit edit, bool prepared, bool latencyChange) {
    auto processor = std::make_unique<Processor>();
    parameter(*processor, "advancedOn", 1);
    parameter(*processor, "doubleOn", latencyChange && edit != Edit::reset ? 1.f : 0.f);
    parameter(*processor, "formantOn", latencyChange && edit == Edit::reset ? 1.f : 0.f);
    for (size_t slot = 0; slot < 8; ++slot)
        parameter(*processor, voicePresentParameterId(slot),
                  edit == Edit::remove && slot == 0 ? 1.f : 0.f);
    auto graph = defaultGraph(false, true);
    graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(), [&](const RoutingLink& link) {
        return link.source >= 2 && link.source <= 9
            && !(edit == Edit::remove && link.source == 2);
    }), graph.links.end());
    juce::String error;
    check(processor->setRoutingGraph(graph, error), "activation fixture graph rejected");
    if (prepared) processor->prepareToPlay(48000, 137);
    check(!processor->routingRestartPending(), "activation fixture already pending");
    const int activeLatency = processor->getLatencySamples();
    if (prepared && latencyChange)
        check((activeLatency > 0) == (edit != Edit::add), "activation fixture latency mismatch");
    ActivationListener listener(*processor);
    if (edit == Edit::reset) processor->resetParameters();
    else if (edit == Edit::add) check(processor->addVoice(), "activation fixture could not add voice");
    else processor->removeVoice(0);
    const bool expectedPending = prepared && latencyChange;
    check(processor->routingRestartPending() == expectedPending, "edit did not make the expected pending transition");
    check(listener.requests == int(expectedPending), "edit must synchronously send exactly one latency notification iff pending");
    check(processor->getLatencySamples() == activeLatency, "edit changed active latency before host activation");
    if (expectedPending)
        check(listener.pendingAtRequest && listener.latencyAtRequest == activeLatency
            && listener.onMessageThread && listener.outsideLock,
            "latency request must expose pending state and old latency outside controlMutex on the message thread");
    pumpActivationTimers();
    check(listener.requests == int(expectedPending), "timer duplicated or belatedly supplied the edit's latency notification");
    if (expectedPending) {
        processor->releaseResources();
        processor->prepareToPlay(48000, 137);
        check(!processor->routingRestartPending(), "host activation did not consume pending edit");
        check((processor->getLatencySamples() > 0) == (edit == Edit::add), "host activation applied wrong latency");
    }
    processor->releaseResources();
}

void pocketLatency() {
    auto processor = std::make_unique<Processor>();
    Graph graph{{{1, NodeType::lead}, {100, NodeType::delay}, {101, NodeType::spectralMorph}, {13, NodeType::output}},
                {{1, 100, 0}, {100, 13, 0}}};
    graph.nodes[2].wetOwner = 100;
    juce::String error;
    check(processor->setRoutingGraph(graph, error), "wet Morph activation fixture rejected");
    processor->prepareToPlay(48000., 137);
    const int wet = processor->getLatencySamples();
    check(wet == SpectralMorph::latencySamples, "wet Morph did not contribute latency");
    const auto toggle = [&](const char* control, float value, int before, int after) {
        {
            ActivationListener listener(*processor);
            parameter(*processor, control, value);
            pumpActivationTimers();
            check(listener.requests == 1 && listener.pendingAtRequest && listener.latencyAtRequest == before,
                "wet Morph toggle didn't request automatic latency reactivation exactly once");
        }
        processor->releaseResources();
        processor->prepareToPlay(48000., 137);
        check(processor->getLatencySamples() == after && !processor->routingRestartPending(),
            "wet Morph reactivation applied wrong latency");
    };
    toggle("fx1On", 0.f, wet, 0);
    toggle("fx1On", 1.f, 0, wet);
    toggle("fx2On", 0.f, wet, 0);
    toggle("fx2On", 1.f, 0, wet);
    processor->releaseResources();
}

void hostActivationNotifications() {
    int failures = 0;
    for (auto edit : {Edit::reset, Edit::add, Edit::remove}) {
        const auto* name = edit == Edit::reset ? "resetParameters" : edit == Edit::add ? "addVoice" : "removeVoice";
        for (int scenario = 0; scenario < 3; ++scenario) {
            try {
                activationNotification(edit, scenario != 2, scenario != 1);
            } catch (const std::exception& e) {
                ++failures;
                std::cerr << name << " scenario " << scenario << ": " << e.what() << '\n';
            }
        }
    }
    check(failures == 0, "host activation notification regressions failed");
}
#endif
} // namespace

int main() {
#if JUCE_MODULE_AVAILABLE_juce_audio_processors
    juce::ScopedJuceInitialiser_GUI juce;
#endif
    try {
        graphContract();
        morphTargets();
        granularSleep();
#if JUCE_MODULE_AVAILABLE_juce_audio_processors
        hostActivationNotifications();
        pocketLatency();
        std::cout << "Host activation notification tests passed\n";
#endif
        std::cout << "Routing activation tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
