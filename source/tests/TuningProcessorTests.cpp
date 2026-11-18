// Headless processor integration; run with Run-TuningProcessorTests.ps1.
#include "PluginProcessor.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace vocalstack;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(float actual, float expected, const char* message) { check(std::abs(actual - expected) < .001f, message); }
void set(Processor& p, const char* id, float value) {
    auto* parameter = p.parameters.getParameter(id);
    check(parameter != nullptr, "missing parameter");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
constexpr std::array<float, 21> defaults{0, 1, 0, 0, 4095, 440, 0, 1, 0, 0, .25f, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
constexpr std::array<float, 21> minima{0, 0, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr std::array<float, 21> maxima{1, 1, 11, 6, 4095, 480, 3, 1, 2000, 2000, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
constexpr std::array<float, 21> edits{1, .63f, 9, 6, 0x295, 432, 2, .81f, 123, 456, .72f, 0, .23f, .1f, .2f, .3f, .4f, .5f, .6f, .7f, .8f};
void contractAndState() {
    auto p = std::make_unique<Processor>();
    check(tuningParameterStart == 2357 && parameterIds.size() == 2378, "append-only parameter count");
    check(p->getParameters().size() == int(parameterIds.size()), "host parameter count");
    for (size_t i = 0; i < parameterIds.size(); ++i)
        check(p->getParameters()[int(i)] == p->parameters.getParameter(parameterIds[i]), "host parameter order");
    for (size_t i = 0; i < tuningParameterIds.size(); ++i) {
        auto* parameter = p->parameters.getParameter(tuningParameterIds[i]);
        near(parameter->convertFrom0to1(parameter->getDefaultValue()), defaults[i], "parameter default");
        near(parameter->convertFrom0to1(0), minima[i], "parameter minimum");
        near(parameter->convertFrom0to1(1), maxima[i], "parameter maximum");
        set(*p, tuningParameterIds[i], edits[i]);
    }
    auto* scale = dynamic_cast<juce::AudioParameterChoice*>(p->parameters.getParameter("tuningScale"));
    check(scale && scale->choices == juce::StringArray{"Chromatic", "Major", "NaturalMinor", "HarmonicMinor", "MajorPentatonic", "MinorPentatonic", "Custom"}, "scale enum order");
    auto* range = dynamic_cast<juce::AudioParameterChoice*>(p->parameters.getParameter("tuningRange"));
    check(range && range->choices == juce::StringArray{"Wide", "Low", "Mid", "High"}, "range enum order");
    const auto s = p->settings();
    check(s.tuningEnabled && s.tuning.root == 9 && s.tuning.scale == TuningScale::Custom
        && s.tuning.allowedNotesMask == 0x295 && s.tuning.vocalRange == TuningVocalRange::Mid
        && !s.tuning.preserveFormants, "discrete settings bridge");
    near(s.tuningMix, .63f, "module mix bridge"); near(s.tuning.referenceHz, 432, "reference bridge");
    near(s.tuning.amount, .81f, "amount bridge"); near(s.tuning.retuneMs, 123, "retune bridge");
    near(s.tuning.transitionMs, 456, "transition bridge"); near(s.tuning.stability, .72f, "stability bridge");
    near(s.leadSettings.tuneMix, .23f, "lead mix bridge");
    for (size_t v = 0; v < 8; ++v) near(s.voiceSettings[v].tuneMix, edits[13 + v], "voice mix bridge");
    juce::MemoryBlock state; p->getStateInformation(state);
    auto recalled = std::make_unique<Processor>();
    recalled->setStateInformation(state.getData(), int(state.getSize()));
    for (size_t i = 0; i < edits.size(); ++i)
        near(recalled->parameters.getRawParameterValue(tuningParameterIds[i])->load(), edits[i], "binary state round trip");
    const auto text = p->settingsText();
    p->resetParameters();
    check(!p->settings().tuningEnabled, "reset must disable tuning");
    check(p->pasteSettings(text), "text state rejected");
    for (size_t i = 0; i < edits.size(); ++i)
        near(p->parameters.getRawParameterValue(tuningParameterIds[i])->load(), edits[i], "text state round trip");
    auto xml = juce::parseXML(text); check(xml != nullptr, "settings XML");
    auto legacy = juce::ValueTree::fromXml(*xml);
    for (const auto* id : tuningParameterIds) legacy.removeChild(legacy.getChildWithProperty("id", id), nullptr);
    p->prepareToPlay(48000, 137);
    const int activeLatency = p->getLatencySamples();
    check(p->pasteSettings(legacy.createXml()->toString()), "legacy state rejected");
    check(!p->settings().tuningEnabled && p->routingRestartPending() && p->getLatencySamples() == activeLatency,
        "legacy restore did not disable previously active tuning safely");
    for (size_t i = 0; i < defaults.size(); ++i)
        near(p->parameters.getRawParameterValue(tuningParameterIds[i])->load(), defaults[i], "missing legacy field inherited previous edit");
    p->releaseResources();
    // Partial new-format states use defaults independently, including an empty mask.
    set(*p, "tuningEnabled", 1); set(*p, "tuningNotes", 0);
    xml = juce::parseXML(p->settingsText()); auto partial = juce::ValueTree::fromXml(*xml);
    partial.removeChild(partial.getChildWithProperty("id", "tuningEnabled"), nullptr);
    check(p->pasteSettings(partial.createXml()->toString()), "partial state rejected");
    check(!p->settings().tuningEnabled && p->settings().tuning.allowedNotesMask == 0, "partial recall safety");
    set(*p, "leadTuneMix", .19f); set(*p, "voice8TuneMix", .37f);
    p->resetVoicesToBasic();
    near(p->settings().leadSettings.tuneMix, .19f, "basic reset changed lead tuning");
    near(p->settings().voiceSettings[7].tuneMix, .37f, "basic reset changed dormant voice tuning");
}
void activationAndBypass(int channels, double rate) {
    auto p = std::make_unique<Processor>();
    auto layout = p->getBusesLayout();
    layout.inputBuses.set(0, channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    layout.outputBuses.set(0, layout.inputBuses[0]);
    check(p->setBusesLayout(layout), "bus layout rejected");
    p->prepareToPlay(rate, 137);
    const int base = p->getLatencySamples();
    check(!p->routingRestartPending(), "default restart pending");
    set(*p, "tuningEnabled", 1);
    check(p->routingRestartPending(), "enable must advertise pending reactivation");
    p->reset(); check(p->getLatencySamples() == base, "reset changed host latency");
    juce::AudioBuffer<float> audio(channels, 137); audio.clear(); juce::MidiBuffer midi;
    p->processBlock(audio, midi);
    check(p->getLatencySamples() == base && !p->getTuningTelemetry().voiced, "unprepared tuner enabled live");
    // Saved requested enable must survive while DSP activation remains deferred.
    juce::MemoryBlock pending; p->getStateInformation(pending);
    auto recalled = std::make_unique<Processor>();
    recalled->setStateInformation(pending.getData(), int(pending.getSize()));
    check(recalled->settings().tuningEnabled, "pending enable not saved");
    p->releaseResources(); p->prepareToPlay(rate, 137);
    const int tuned = p->getLatencySamples();
    check(tuned > base && !p->routingRestartPending(), "reactivation did not reserve tuner latency");
    const auto bypassImpulse = [&] {
        // Start at the bypass endpoint rather than measuring its normal ramp.
        set(*p, "bypass", 1);
        p->reset();
        constexpr std::array<int, 7> blocks{1, 7, 137, 31, 256, 3, 97};
        int position = 0, block = 0;
        while (position < tuned + 512) {
            const int count = std::min(blocks[size_t(block++ % blocks.size())], tuned + 512 - position);
            audio.setSize(channels, count); audio.clear();
            if (position == 0) for (int c = 0; c < channels; ++c) audio.setSample(c, 0, .25f);
            p->processBlockBypassed(audio, midi);
            for (int c = 0; c < channels; ++c) for (int i = 0; i < count; ++i)
                near(audio.getSample(c, i), position + i == tuned ? .25f : 0, "bypass not latency-aligned dry");
            position += count;
        }
        check(p->getLatencySamples() == tuned, "render changed host latency");
    };
    bypassImpulse();
    set(*p, "tuningEnabled", 0);
    check(p->routingRestartPending(), "disable must retain reserve until reactivation");
    bypassImpulse();
    p->releaseResources(); p->prepareToPlay(rate, 137);
    check(p->getLatencySamples() == base && !p->routingRestartPending(), "disable reactivation retained reserve");
}
void telemetryAndModulation() {
    auto p = std::make_unique<Processor>();
    check(!p->getTuningTelemetry().voiced && !p->getTuningTelemetry().hasTarget, "initial telemetry");
    set(*p, "tuningEnabled", 1);
    p->prepareToPlay(48000, 137);
    // Exercise chunked render as well as the idle-modulation fast path below.
    ModulationState state; Modulator lfo; lfo.id = 101; state.modulators.push_back(lfo);
    state.assignments.push_back({101, "tuningAmount", -.25f, true});
    juce::String error;
    check(p->setModulationState(state, error), "continuous tuning modulation rejected");
    auto invalid = state; invalid.assignments[0].targetParameterId = "tuningEnabled";
    check(!p->setModulationState(invalid, error), "latency-changing enable accepted as modulation target");
    std::atomic<bool> done{false}, invalidFrame{false};
    std::atomic<unsigned> reads{0};
    std::thread reader([&] {
        while (!done.load()) {
            const auto t = p->getTuningTelemetry();
            if (!std::isfinite(t.detectedHz) || !std::isfinite(t.detectedMidi) || !std::isfinite(t.confidence)
                || !std::isfinite(t.targetMidi) || !std::isfinite(t.correctionSemitones)
                || (t.voiced && (t.detectedHz <= 0 || std::abs(t.detectedMidi - (69 + 12 * std::log2(t.detectedHz / 440))) > .01f)))
                invalidFrame.store(true);
            reads.fetch_add(1);
        }
    });
    juce::AudioBuffer<float> audio(2, 137); juce::MidiBuffer midi;
    bool voiced = false, target = false;
    for (int block = 0; block < 450; ++block) {
        for (int i = 0; i < 137; ++i) {
            const float sample = .15f * std::sin(float(2 * 3.141592653589793 * 225 * (block * 137 + i) / 48000));
            for (int c = 0; c < 2; ++c) audio.setSample(c, i, sample);
        }
        p->processBlock(audio, midi);
        const auto t = p->getTuningTelemetry(); voiced |= t.voiced; target |= t.hasTarget;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 137; ++i)
            if (!std::isfinite(audio.getSample(c, i))) invalidFrame.store(true);
    }
    done.store(true); reader.join();
    check(reads.load() > 0 && !invalidFrame.load(), "concurrent telemetry invalid or incoherent");
    check(voiced && target, "chunked render did not publish voiced target telemetry");
    near(p->parameters.getRawParameterValue("tuningAmount")->load(), 1, "modulation changed host base");
    p->reset(); check(!p->getTuningTelemetry().voiced && !p->getTuningTelemetry().hasTarget, "reset retained stale pitch");
    check(p->setModulationState({}, error), "clear modulation failed");
    for (int block = 0; block < 300; ++block) {
        for (int i = 0; i < 137; ++i) for (int c = 0; c < 2; ++c)
            audio.setSample(c, i, .15f * std::sin(float(2 * 3.141592653589793 * 225 * (block * 137 + i) / 48000)));
        p->processBlock(audio, midi);
    }
    check(p->getTuningTelemetry().voiced, "idle render did not publish telemetry");
}
}
int main() {
    juce::ScopedJuceInitialiser_GUI juce;
    try {
        contractAndState(); std::cout << "PASS parameter contract and state migration\n";
        for (const double rate : {44100., 48000., 96000.}) for (const int channels : {1, 2}) activationAndBypass(channels, rate);
        std::cout << "PASS mono/stereo activation latency and irregular-block bypass at 44.1/48/96 kHz\n";
        telemetryAndModulation(); std::cout << "PASS concurrent telemetry, reset and modulation render paths\n";
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
