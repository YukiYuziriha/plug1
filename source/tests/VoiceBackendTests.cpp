#include "VocalEngine.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
thread_local bool audio = false;
thread_local int allocations = 0, destructions = 0;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void near(float a, float b, const char* message) { check(std::abs(a - b) < 0.005f, message); }
}
void* operator new(std::size_t n) {
    if (audio) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (audio && p) ++destructions; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
using namespace vocalstack;

Settings solo(int slot = 0) {
    Settings s; s.advanced = true; s.levelOn = false; s.human = 0;
    s.width = 0; s.doubles = 1; s.formant = 0;
    for (auto& v : s.voiceSettings) { v.enabled = false; v.present = false; }
    s.voiceSettings[slot] = {true, 0, 0, 0, 0, 0, 0, 1, 0, true};
    return s;
}
Graph voiceGraph(int slot = 0) {
    return {{{1, NodeType::voice, slot}, {2, NodeType::output}}, {{1, 2, 0}}};
}
std::vector<float> render(VocalEngine& engine, const Settings& s, int rate, int channels,
                          int outputs, int seconds, bool vowel = false, double fundamental = 220) {
    const int size = seconds * rate;
    std::vector<float> input(size), right(size), leftOut(size), rightOut(size);
    for (int i = 0; i < size; ++i) {
        const double phase = 2 * std::numbers::pi * fundamental * i / rate;
        input[i] = 0.08f * float(std::sin(phase));
        if (vowel) for (int h = 2; h < 20 && h * fundamental < rate * 0.45; ++h)
            input[i] += 0.04f / h * float(std::sin(phase * h));
        right[i] = input[i] * 0.73f;
    }
    constexpr int blocks[] = {1, 137, 64, 29, 511, 7};
    int position = 0, block = 0;
    while (position < size) {
        const auto count = std::min(blocks[block++ % 6], size - position);
        const float* in[] = {input.data() + position, right.data() + position};
        float* out[] = {leftOut.data() + position, rightOut.data() + position};
        audio = true; engine.process(in, out, count, s); audio = false;
        position += count;
    }
    for (int i = 0; i < size; ++i) {
        check(std::isfinite(leftOut[i]) && std::isfinite(rightOut[i]), "nonfinite voice output");
        check(std::abs(leftOut[i]) < 16 && std::abs(rightOut[i]) < 16, "extreme voice runaway");
    }
    check(channels == 1 || channels == 2, "test channels");
    check(outputs == 1 || outputs == 2, "test outputs");
    return leftOut;
}
double amplitude(const std::vector<float>& data, int rate, double hz) {
    double real = 0, imaginary = 0, weight = 0;
    const int count = rate;
    const int start = int(data.size()) - count;
    for (int i = 0; i < count; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / count);
        const double p = 2 * std::numbers::pi * hz * i / rate;
        real += data[start + i] * w * std::cos(p);
        imaginary += data[start + i] * w * std::sin(p); weight += w;
    }
    return 2 * std::hypot(real, imaginary) / weight;
}
void pitchAndDefaults() {
    const VoiceSettings oldAggregate{true, -6, 0.2f, 10, 7, 2, 1, 1};
    check(oldAggregate.present && oldAggregate.pitchSemitones == 0, "aggregate compatibility");
    const auto basic = basicVoiceSettings(0.35f);
    for (int i = 0; i < 8; ++i) check(basic[i].present == (i < 2), "fresh presence defaults");
    for (float pitch : {-36.0f, -24.0f, -12.0f, 0.0f, 7.5f, 12.0f, 24.0f, 36.0f}) {
        auto s = solo(); s.voiceSettings[0].pitchSemitones = pitch;
        s.voiceSettings[0].detuneCents = 17;
        // A co-moving envelope isolates pitch accuracy from formant preservation
        // (a fixed high envelope can nearly erase a low shifted test sinusoid).
        s.formant = std::clamp(pitch, -24.0f, 24.0f);
        s.voiceSettings[0].formantSemitones = pitch - s.formant;
        VocalEngine e; e.prepare(48000, 1, 2, s);
        check(bool(e.requestGraph(voiceGraph())), "voice graph request");
        const double fundamental = 220;
        auto out = render(e, s, 48000, 1, 2, 2, true, fundamental);
        const double frequency = fundamental * std::exp2((pitch + 0.17) / 12);
        const double atTarget = amplitude(out, 48000, frequency);
        const double wrong = amplitude(out, 48000, frequency * std::exp2(1.0 / 12));
        std::cout << "Pitch " << pitch << " st +17 ct: " << frequency << " Hz amplitude " << atTarget << ", adjacent semitone " << wrong << '\n';

        check(atTarget > 1e-4 && atTarget > wrong * 4, "musical pitch/fine detune frequency mismatch");
        const auto snap = e.voiceSnapshots()[0];
        near(snap.pitchSemitones, pitch, "pitch snapshot"); near(snap.detuneCents, 17, "fine detune snapshot");
        check(snap.activity > 0, "activity is not measured");
        near(snap.delayMs, 0, "snapshot must exclude algorithm latency");
    }
}
void formantExtremes() {
    for (int rate : {44100, 48000, 88200, 96000, 192000}) {
        for (float sign : {-1.0f, 1.0f}) {
            auto s = solo(7); s.formant = 24 * sign;
            s.voiceSettings[7].formantSemitones = 24 * sign;
            s.voiceSettings[7].pitchSemitones = sign * 36;
            s.voiceSettings[7].detuneCents = sign * 100;
            VocalEngine e; e.prepare(rate, 2, 1, s);
            check(bool(e.requestGraph(voiceGraph(7))), "slot eight graph");
            render(e, s, rate, 2, 1, 1, true);
            auto snap = e.voiceSnapshots();
            near(snap[7].formantSemitones, 48 * sign, "stacked formant was clamped");
            near(snap[7].pitchSemitones, 36 * sign, "extreme pitch snapshot");
            near(snap[7].detuneCents, 100 * sign, "extreme detune snapshot");
            s.formantOn = false;
            render(e, s, rate, 2, 1, 1, true);
            near(e.voiceSnapshots()[7].formantSemitones, 24 * sign, "independent formant disabled with shared");
            e.reset(s);
            render(e, s, rate, 2, 1, 1);
        }
    }
}
void removalReactivationAndBypass() {
    auto s = solo(7); s.voiceSettings[7].delayMs = 80;
    s.voiceSettings[7].pan = 0.5f; s.width = 0.7f;
    VocalEngine e; e.prepare(48000, 1, 2, s);
    check(bool(e.requestGraph(voiceGraph(7))), "slot eight routing");
    render(e, s, 48000, 1, 2, 1);
    near(e.voiceSnapshots()[7].pan, 0.35f, "effective width/pan snapshot");
    near(e.voiceSnapshots()[7].delayMs, 80, "extra delay snapshot");
    s.voiceSettings[7].present = false;
    render(e, s, 48000, 1, 2, 1);
    check(!e.voiceSnapshots()[7].present, "removed slot reported present");
    s.voiceSettings[7].present = true;
    render(e, s, 48000, 1, 2, 1);
    check(e.voiceSnapshots()[7].activity > 0, "reactivated slot silent");
    s.bypass = true; e.reset(s);
    const int n = 4096;
    std::vector<float> input(n), output(n), right(n);
    for (int i = 0; i < n; ++i) input[i] = float(i % 113) / 113.0f;
    const float* in[] = {input.data()}; float* out[] = {output.data(), right.data()};
    audio = true; e.process(in, out, n, s); audio = false;
    for (int i = 0; i < n; ++i) check(output[i] == (i < e.latency() ? 0 : input[i - e.latency()]), "bypass identity/latency");
    check(allocations == 0 && destructions == 0, "voice audio allocated or destroyed storage");
}
void allSlotsAndEffects() {
    auto s = solo(); s.human = 1; s.formant = 24;
    constexpr float pitches[] = {-36, -24, -12, 7.5f, 12, 24, 36, 0};
    for (int i = 0; i < 8; ++i)
        s.voiceSettings[i] = {true, -12, i % 2 ? 1.0f : -1.0f, float(i * 9),
                              i % 2 ? 100.0f : -100.0f, i % 2 ? 24.0f : -24.0f,
                              2, 8, pitches[i], true};
    VocalEngine e; e.prepare(48000, 2, 2, s);
    auto graph = defaultGraph();
    graph.links.pop_back(); // replace legacy Chorale -> Output with 15 more machines
    for (int i = 0; i < 15; ++i) {
        RoutingNode fx{std::uint32_t(100 + i), NodeType::chorale};
        fx.mix = 0.1f; fx.motion = float(i) / 14;
        graph.nodes.push_back(fx);
        graph.links.push_back({std::uint32_t(i ? 99 + i : 12), std::uint32_t(100 + i), 0});
    }
    graph.links.push_back({114, 13, 0});
    check(bool(e.requestGraph(graph)), "16 machine graph rejected");
    const auto start = std::chrono::steady_clock::now();
    render(e, s, 48000, 2, 2, 1, true);
    std::cout << "Eight voices / 16 FX / one second offline wall time: "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    for (const auto& snap : e.voiceSnapshots()) check(snap.present && snap.enabled, "stable slot missing");
    // Exercise warm-up and smooth crossovers in both directions, not just static
    // settings. Also exercise new pitch with Human motion and independent formants.
    for (int i = 0; i < 8; ++i) s.voiceSettings[i].pitchSemitones = -pitches[i];
    s.formant = -24;
    render(e, s, 48000, 2, 2, 1, true);
    s.voiceSettings[3].present = false;
    render(e, s, 48000, 2, 2, 1, true);
    check(!e.voiceSnapshots()[3].present && e.voiceSnapshots()[4].present, "removal renumbered slots");
    for (auto& v : s.voiceSettings) v.pitchSemitones = 0;
    render(e, s, 48000, 2, 2, 1, true);
    e.reset(s);
    float silence[511]{}, left[511]{}, right[511]{};
    const float* input[] = {silence, silence}; float* output[] = {left, right};
    audio = true; e.process(input, output, 511, s); audio = false;
    for (int i = 0; i < 511; ++i) check(left[i] == 0 && right[i] == 0, "reset left a tail on silence");
    check(allocations == 0 && destructions == 0, "eight-voice automation allocated/destroyed on audio");
}
int main() {
    try {
        pitchAndDefaults(); formantExtremes(); removalReactivationAndBypass(); allSlotsAndEffects();
        std::cout << "Voice backend: pitch spectrum, stacked formant requests, rates, irregular blocks, snapshots, slot lifecycle, bypass and RT ownership passed\n";
    } catch (const std::exception& e) { audio = false; std::cerr << e.what() << '\n'; return 1; }
}
