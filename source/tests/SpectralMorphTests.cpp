#include "SpectralMorph.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
thread_local bool rendering = false;
std::atomic<unsigned> allocations{0}, destructions{0};
void check(bool x, const char* reason) { if (!x) throw std::runtime_error(reason); }
}
void* operator new(std::size_t n) {
    if (rendering) ++allocations;
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (p && rendering) ++destructions; std::free(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
using namespace vocalstack;

void reconstruction() {
    constexpr int length = 13001;
    constexpr std::array<int, 8> blocks{1, 137, 7, 512, 33, 3, 1024, 63};
    for (double rate : {44100., 48000., 96000.}) for (bool mono : {false, true}) for (int size = 0; size < 3; ++size) {
        SpectralMorph fx; fx.prepare(rate);
        SpectralMorphSettings settings; settings.fftSize = size;
        std::array<std::vector<float>, 2> input, output;
        for (auto* pair : {&input, &output}) for (auto& c : *pair) c.resize(length);
        for (int i = 0; i < length; ++i) {
            input[0][size_t(i)] = i == 0 ? .3f : float(.1 * std::sin(i * .067) + .03 * std::cos(i * .139));
            input[1][size_t(i)] = mono ? input[0][size_t(i)] : float(-.07 * std::cos(i * .091));
        }
        int offset = 0, block = 0;
        while (offset < length) {
            const int count = std::min(blocks[size_t(block++ % blocks.size())], length - offset);
            rendering = true;
            for (int j = 0; j < count; ++j) {
                const auto i = size_t(offset + j);
                const auto y = fx.process({input[0][i], input[1][i]}, {.4f, -.2f}, true, settings);
                output[0][i] = y[0]; output[1][i] = y[1];
            }
            rendering = false;
            offset += count;
        }
        for (int c = 0; c < 2; ++c) for (int i = 0; i < length; ++i) {
            const float expected = i < SpectralMorph::latencySamples ? 0 : input[size_t(c)][size_t(i - SpectralMorph::latencySamples)];
            check(std::abs(output[size_t(c)][size_t(i)] - expected) < 3.e-6f,
                "WOLA reconstruction / fixed latency / stereo coherence mismatch");
        }
        fx.reset(); settings.mix = 0; settings.morph = 1;
        rendering = true;
        float error = 0;
        for (int i = 0; i < length; ++i) {
            const auto y = fx.process({input[0][size_t(i)], input[1][size_t(i)]}, {}, false, settings);
            for (int c = 0; c < 2; ++c) {
                const float expected = i < SpectralMorph::latencySamples ? 0 : input[size_t(c)][size_t(i - SpectralMorph::latencySamples)];
                error = std::max(error, std::abs(y[size_t(c)] - expected));
            }
        }
        rendering = false;
        check(error == 0, "Mix zero is not exact delayed main");
    }
}
void transitionsAndFreeze() {
    SpectralMorph fx; fx.prepare(48000);
    SpectralMorphSettings settings; settings.morph = 1; settings.smoothingMs = 0;
    settings.fftSize = 0;
    bool finite = true;
    rendering = true;
    for (int i = 0; i < 14000; ++i) {
        const auto y = fx.process({.1f, -.1f}, {.04f, -.07f}, true, settings);
        finite &= std::isfinite(y[0]) && std::isfinite(y[1]);
    }
    rendering = false;
    settings.freeze = true;
    for (int i = 0; i < 6000; ++i) fx.process({.1f, -.1f}, {.04f, -.07f}, true, settings);
    const auto held = fx.audioSnapshot();
    check(held.frozen && !held.capturePending, "Freeze did not capture available donor");
    for (int i = 0; i < 12000; ++i) fx.process({.1f, -.1f}, {}, false, settings);
    const auto disconnected = fx.audioSnapshot();
    check(disconnected.frozen && !disconnected.donorConnected, "disconnect discarded held donor");
    check(disconnected.donor[0] > .01f, "Freeze retained a flag but no donor spectrum");
    settings.freeze = false;
    for (int i = 0; i < 12000; ++i) fx.process({.1f, -.1f}, {}, false, settings);
    check(!fx.audioSnapshot().frozen && fx.audioSnapshot().donor[0] < 1.e-5f,
        "Freeze release failed to return to absent live donor");
    settings.freeze = true;
    for (int i = 0; i < 5000; ++i) fx.process({.1f, -.1f}, {}, false, settings);
    check(fx.audioSnapshot().capturePending && !fx.audioSnapshot().frozen,
        "rearming Freeze resurrected a previous capture without a donor");
    fx.reset(); settings.freeze = true;
    for (int i = 0; i < 5000; ++i) fx.process({.1f, -.1f}, {}, false, settings);
    check(fx.audioSnapshot().capturePending && !fx.audioSnapshot().frozen,
        "Freeze before availability captured invented donor");
    for (int i = 0; i < 5000; ++i) fx.process({.1f, -.1f}, {.02f, -.02f}, true, settings);
    check(fx.audioSnapshot().frozen, "pending capture did not capture when donor arrived");
    rendering = true;
    for (int i = 0; i < 18000; ++i) {
        settings.fftSize = (i / 127) % 3;
        settings.mode = (i / 193) % 2;
        settings.morph = float(i % 500) / 499;
        settings.mix = float(i % 401) / 400;
        settings.smoothingMs = float(i % 1000);
        settings.freeze = (i / 311) % 2 != 0;
        const auto y = fx.process({.2f * std::sin(i * .07f), -.13f * std::cos(i * .11f)},
            {.3f * std::cos(i * .03f), .1f * std::sin(i * .17f)}, i % 701 < 600, settings);
        finite &= std::isfinite(y[0]) && std::isfinite(y[1]) && std::abs(y[0]) < 16 && std::abs(y[1]) < 16;
    }
    fx.reset();
    for (int i = 0; i < 6000; ++i) {
        const auto y = fx.process({}, {}, false, settings);
        finite &= y[0] == 0 && y[1] == 0;
    }
    rendering = false;
    check(finite, "transition/reset processing was nonfinite, unbounded or leaked old audio");
}
int main() {
    try {
        reconstruction(); transitionsAndFreeze();
        check(allocations == 0 && destructions == 0, "STFT processing/reset allocated or destroyed heap storage");
        std::cout << "Spectral Morph standalone DSP tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        rendering = false; std::cerr << error.what() << '\n'; return 1;
    }
}
