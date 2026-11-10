#include "VocalEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace vocalstack;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Graph leadGraph() { return {{{1, NodeType::lead}, {2, NodeType::output}}, {{1, 2, 0}}}; }
std::vector<float> render(Settings settings, double rate, int channels, int outputChannels,
                          bool hostBypass = false, bool dirtyFirst = false) {
    VocalEngine engine;
    engine.prepare(rate, channels, outputChannels, settings);
    if (dirtyFirst) {
        auto dirty = settings;
        dirty.leadSettings = {true, -9, -0.7f, 48, 67, -14, 2, 6, 19, true};
        dirty.globalMix = 0.2f; dirty.formant = 17; dirty.human = 1;
        std::array<float, 137> signal{}, left{}, right{};
        signal.fill(0.2f);
        const float* in[]{signal.data(), signal.data()};
        float* out[]{left.data(), right.data()};
        for (int i = 0; i < 30; ++i) engine.process(in, out, 137, dirty);
        engine.reset(settings);
    }
    check(bool(engine.requestGraph(leadGraph())), "lead graph rejected");
    const int samples = int(rate * 0.35);
    std::vector<float> input(samples), right(samples), leftOut(samples), rightOut(samples);
    for (int i = 0; i < samples; ++i) {
        input[i] = 0.12f * std::sin(float(i) * 0.071f);
        right[i] = input[i] * 0.73f;
    }
    constexpr std::array<int, 7> blocks{1, 137, 7, 511, 32, 3, 64};
    int offset = 0, block = 0;
    while (offset < samples) {
        const int count = std::min(blocks[size_t(block++ % blocks.size())], samples - offset);
        const float* in[]{input.data() + offset, right.data() + offset};
        float* out[]{leftOut.data() + offset, rightOut.data() + offset};
        engine.process(in, out, count, settings, hostBypass);
        offset += count;
    }
    for (int i = 0; i < samples; ++i) {
        check(std::isfinite(leftOut[i]) && std::isfinite(rightOut[i]), "nonfinite Lead output");
        if (settings.globalMix == 0 || settings.bypass || (hostBypass && i > int(rate * 0.25))) {
            float expected = i < engine.latency() ? 0 : input[size_t(i - engine.latency())];
            if (outputChannels == 1 && channels == 2) expected *= 0.865f;
            check(std::abs(leftOut[i] - expected) < 1e-7f, "dry/bypass is not latency-aligned untouched input");
        }
    }
    return leftOut;
}
void endpoints() {
    for (double rate : {44100.0, 48000.0, 96000.0})
        for (int channels : {1, 2}) for (int outputs : {1, 2}) {
            Settings s;
            s.globalMix = 0; s.level = 1; s.formant = -12; s.outputGainDb = 12;
            s.leadSettings = {false, -60, 1, 83, 71, 14, 2, 5, 24, true};
            render(s, rate, channels, outputs);
            s.globalMix = 0.63f; s.bypass = true;
            render(s, rate, channels, outputs);
            s.bypass = false;
            render(s, rate, channels, outputs, true);
        }
}
void sameAfterWarmup(const std::vector<float>& a, const std::vector<float>& b, const char* message) {
    check(a.size() == b.size(), "comparison size mismatch");
    for (size_t i = 5000; i < a.size(); ++i) check(std::abs(a[i] - b[i]) < 1e-6f, message);
}
void formantAndReset() {
    for (float pitch : {0.0f, 12.0f}) {
        Settings shared;
        shared.levelOn = false; shared.human = 0; shared.doubles = 0;
        shared.formant = 12; shared.leadSettings.pitchSemitones = pitch;
        auto split = shared; split.formant = 6; split.leadSettings.formantSemitones = 6;
        sameAfterWarmup(render(shared, 48000, 2, 2), render(split, 48000, 2, 2),
            "Lead shared/local formants were applied twice or did not add");
        auto independent = split; independent.formantOn = false; independent.formant = -24;
        auto localOnly = independent; localOnly.formant = 0;
        sameAfterWarmup(render(independent, 48000, 2, 2), render(localOnly, 48000, 2, 2),
            "disabled shared Formant contaminated the independent local shift");
    }
    Settings neutral;
    neutral.levelOn = false; neutral.doubles = 0;
    const auto fresh = render(neutral, 48000, 2, 2);
    const auto reset = render(neutral, 48000, 2, 2, false, true);
    check(fresh == reset, "reset to neutral did not reproduce a fresh engine");
    auto stopped = neutral;
    stopped.formantOn = false; stopped.formant = -24;
    stopped.leadSettings.depth = 2; stopped.leadSettings.rateHz = 0;
    auto off = stopped; off.formant = 0; off.leadSettings.depth = 0;
    sameAfterWarmup(render(stopped, 48000, 2, 2), render(off, 48000, 2, 2),
        "stopped Lead motion leaked disabled shared Formant");
}
void leadControls() {
    Settings s; s.levelOn = false; s.human = 0; s.doubles = 0; s.formantOn = false;
    const auto neutral = render(s, 48000, 1, 2);
    auto gain = s; gain.leadSettings.gainDb = -6;
    const auto quieter = render(gain, 48000, 1, 2);
    auto pan = s; pan.leadSettings.pan = 1;
    const auto panned = render(pan, 48000, 1, 2);
    auto mute = s; mute.leadSettings.enabled = false;
    const auto muted = render(mute, 48000, 1, 2);
    auto delay = s; delay.leadSettings.delayMs = 20;
    const auto delayed = render(delay, 48000, 1, 2);
    double energy = 0;
    for (size_t i = 5000; i < neutral.size(); ++i) {
        energy += neutral[i] * neutral[i];
        check(std::abs(quieter[i] - neutral[i] * std::pow(10.0f, -6.0f / 20)) < 1e-6f, "Lead gain normalized away");
        check(panned[i] == 0 && muted[i] == 0, "Lead pan/mute failed");
        check(std::abs(delayed[i] - neutral[i - 960]) < 1e-6f, "Lead delay mismatch");
    }
    check(energy > 1, "neutral Lead unexpectedly silent");
    for (float pitch : {-36.0f, 36.0f}) {
        auto shifted = s;
        shifted.leadSettings.pitchSemitones = pitch;
        shifted.leadSettings.detuneCents = 100;
        shifted.leadSettings.formantSemitones = -24;
        render(shifted, 48000, 2, 2);
    }
    check(s.voiceSettings.size() == 8 && s.leadSettings.enabled && s.globalMix == 1,
        "Lead consumed a generated voice or defaults changed");
}
}
int main() {
    try { endpoints(); leadControls(); formantAndReset(); std::cout << "Lead/global Mix backend tests passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
