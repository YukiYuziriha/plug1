#include "VocalEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
thread_local bool audio = false;
thread_local int heapCalls = 0;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

void* operator new(std::size_t size) {
    if (audio) ++heapCalls;
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (audio && p) ++heapCalls; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace {
using namespace vocalstack;
Graph direct(NodeType type, int slot = 0) {
    return {{{1, type, slot}, {13, NodeType::output}}, {{1, 13, 0}}};
}

struct Signal {
    static constexpr int size = 32768;
    std::vector<float> left = std::vector<float>(size), right = std::vector<float>(size);
    std::vector<float> outL = std::vector<float>(size), outR = std::vector<float>(size);
    Signal() {
        for (int i = 0; i < size; ++i) {
            left[i] = 0.12f * std::sin(0.041f * i + 0.2f);
            right[i] = -0.08f * std::cos(0.049f * i);
        }
    }
    void run(VocalEngine& engine, const Settings& settings, int begin, int end, bool bypass = false) {
        constexpr int sizes[] = {1, 137, 29, 511, 7, 64};
        int block = 0;
        while (begin < end) {
            const int count = std::min(sizes[block++ % 6], end - begin);
            const float* input[]{left.data() + begin, right.data() + begin};
            const float* side[]{right.data() + begin};
            float* output[]{outL.data() + begin, outR.data() + begin};
            audio = true;
            engine.process(input, output, count, settings, bypass, side, 1);
            audio = false;
            begin += count;
        }
    }
};

void alignment() {
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (const int channels : {1, 2})
            for (const int reserve : {0, SpectralMorph::latencySamples}) {
                Settings s;
                s.levelOn = s.doubleOn = false;
                VocalEngine engine;
                engine.prepare(rate, channels, 2, s, reserve, false, 0);
                check(engine.vocalLatency() == 0 && engine.latency() == reserve, "incorrect zero-budget latency");
                Signal signal;
                signal.run(engine, s, 0, 8192);
                for (int i = 0; i < 8192; ++i) {
                    check(signal.outL[i] == (i < reserve ? 0.0f : signal.left[i - reserve]), "clean Lead alignment");
                    const auto& expectedR = channels == 1 ? signal.left : signal.right;
                    check(signal.outR[i] == (i < reserve ? 0.0f : expectedR[i - reserve]), "clean right alignment");
                }
                auto automated = s;
                automated.formant = 12;
                automated.leadSettings.pitchSemitones = 19;
                automated.leadSettings.detuneCents = 20;
                automated.doubleOn = true;
                automated.doubles = 1;
                check(bool(engine.requestGraph(direct(NodeType::lead))), "Lead graph rejected");
                engine.reset(s);
                signal.run(engine, automated, 0, 8192);
                for (int i = 0; i < 8192; ++i)
                    check(signal.outL[i] == (i < reserve ? 0.0f : signal.left[i - reserve]), "unexpected spectral Lead activation");
                check(engine.voiceSnapshots()[0].activity == 0, "unexpected generated voice activation");
                check(bool(engine.requestGraph(direct(NodeType::sidechain))), "sidechain graph rejected");
                engine.reset(s);
                signal.run(engine, automated, 0, 8192);
                for (int i = 0; i < 8192; ++i)
                    check(signal.outL[i] == (i < reserve ? 0.0f : signal.right[i - reserve]), "aux alignment");
                automated.bypass = true;
                engine.reset(automated);
                signal.run(engine, automated, 0, 8192);
                for (int i = 0; i < 8192; ++i)
                    check(signal.outL[i] == (i < reserve ? 0.0f : signal.left[i - reserve]), "bypass alignment");
            }
}

void levelerWithoutVocalBudget() {
    Settings controls;
    controls.doubleOn = false;
    controls.level = 1;
    VocalEngine clean, delayed;
    clean.prepare(48000, 1, 2, controls, 0, false, 0);
    delayed.prepare(48000, 1, 2, controls, 0, true, 0);
    Signal immediate, aligned;
    for (int i = 0; i < 8192; ++i) immediate.left[i] *= 0.05f;
    aligned.left = immediate.left;
    immediate.run(clean, controls, 0, 16384);
    aligned.run(delayed, controls, 0, 16384);
    const int latency = delayed.vocalLatency();
    bool ridden = false;
    for (int i = 0; i < 16384 - latency; ++i) {
        check(immediate.outL[i] == aligned.outL[i + latency], "Leveler moved relative to the vocal budget");
        ridden |= std::abs(immediate.outL[i] - immediate.left[i]) > 0.001f;
    }
    check(ridden, "Leveler was bypassed with the vocal budget off");
    controls.levelOn = false;
    immediate.run(clean, controls, 16384, Signal::size);
    for (int i = 30000; i < Signal::size; ++i)
        check(immediate.outL[i] == immediate.left[i], "disabled Leveler did not settle to unity");
}

void wakeAndSleep() {
    Settings controls;
    controls.levelOn = controls.doubleOn = false;
    VocalEngine engine;
    engine.prepare(48000, 1, 2, controls, 0, true, 0x02);
    const int latency = engine.vocalLatency();
    check(latency == VocalEngine::defaultLatency, "legacy vocal latency changed");
    Signal signal;
    signal.run(engine, controls, 0, 8192);
    auto voices = engine.voiceSnapshots();
    check(voices[0].activity == 0 && voices[1].activity > 0, "independent voice mask ignored");
    controls.doubleOn = true;
    controls.leadSettings.pitchSemitones = 12;
    signal.run(engine, controls, 8192, 8192 + latency);
    check(engine.voiceSnapshots()[0].activity == 0, "new voice exposed unprimed samples");
    check(engine.leadSnapshot().pitchSemitones == 0, "new Lead exposed unprimed samples");
    signal.run(engine, controls, 8192 + latency, 16384);
    check(engine.voiceSnapshots()[0].activity > 0, "main Mix voice did not wake");
    check(engine.leadSnapshot().pitchSemitones > 0, "Lead spectral renderer did not wake");
    controls.doubleOn = false;
    controls.leadSettings.pitchSemitones = 0;
    signal.run(engine, controls, 16384, Signal::size);
    check(engine.voiceSnapshots()[0].activity == 0, "main Mix voice did not sleep");
    check(engine.voiceSnapshots()[1].activity > 0, "independent voice slept with main Mix off");
    check(engine.leadSnapshot().pitchSemitones == 0, "Lead spectral path did not fade out");
    for (float x : signal.outL) check(std::isfinite(x) && std::abs(x) < 2, "invalid transition output");
}
}

int main() {
    try {
        alignment();
        levelerWithoutVocalBudget();
        wakeAndSleep();
        check(heapCalls == 0, "audio-thread allocation or destruction");
        std::cout << "Vocal latency tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
