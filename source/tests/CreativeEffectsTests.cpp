// Standalone, no JUCE or project libraries. Example: c++ -std=c++17 -O2
// -Isource/src source/tests/CreativeEffectsTests.cpp -o CreativeEffectsTests
#include "../src/CreativeEffects.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
thread_local bool audioThread = false;
std::atomic<unsigned> audioAllocations{0}, audioDestructions{0};
}
void* operator new(std::size_t n) {
    if (audioThread) ++audioAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (p && audioThread) ++audioDestructions;
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#if defined(__cpp_aligned_new)
void* operator new(std::size_t n, std::align_val_t a) {
    if (audioThread) ++audioAllocations;
#if defined(_MSC_VER)
    if (void* p = _aligned_malloc(n ? n : 1, static_cast<std::size_t>(a))) return p;
#else
    const auto alignment = static_cast<std::size_t>(a);
    if (void* p = std::aligned_alloc(alignment, ((n ? n : 1) + alignment - 1) / alignment * alignment)) return p;
#endif
    throw std::bad_alloc();
}
void operator delete(void* p, std::align_val_t) noexcept {
    if (p && audioThread) ++audioDestructions;
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete[](void* p, std::align_val_t a) noexcept { ::operator delete(p, a); }
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept { ::operator delete(p, a); }
#endif

using namespace vocalstack;
namespace {
using P = CreativeParameter;
constexpr double pi = 3.14159265358979323846;
constexpr std::array<CreativeKind, 5> kinds {CreativeKind::delay, CreativeKind::reverb,
    CreativeKind::compressor, CreativeKind::bitcrusher, CreativeKind::granular};
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
float sine(int i, double sr, double hz = 220) { return static_cast<float>(std::sin(2 * pi * hz * i / sr)); }
void finite(const std::array<float, 2>& v) {
    check(std::isfinite(v[0]) && std::isfinite(v[1]), "Nonfinite output");
    check(std::abs(v[0]) <= 64 && std::abs(v[1]) <= 64, "Unbounded output");
}
void descriptors() {
    check(creativeParameterCount == 48, "Host parameter count changed");
    CreativeSettings s;
    std::array<int, 5> counts{};
    for (std::size_t i = 0; i < creativeParameterCount; ++i) {
        const auto& d = creativeParameters[i];
        check(d.id && *d.id && d.label && *d.label && d.unit, "Incomplete descriptor");
        check(d.min < d.max && d.initial >= d.min && d.initial <= d.max, "Invalid descriptor range");
        check(s.values[i] == d.initial, "Incorrect default initialization");
        ++counts[static_cast<std::size_t>(d.kind)];
        for (std::size_t j = 0; j < i; ++j) check(std::strcmp(d.id, creativeParameters[j].id) != 0, "Duplicate host ID");
    }
    for (int count : counts) check(count > 0, "Missing kind descriptors");
    CreativeEffect cold;
    check(cold.process(1, 1, s) == std::array<float, 2>{}, "Unprepared effect must be safe");
}
void silenceAndReset() {
    for (double sr : {8000., 44100., 48000., 96000., 192000., 384000.}) for (auto kind : kinds) {
        CreativeEffect fx; CreativeSettings s;
        fx.prepare(sr, kind);
        for (int i = 0; i < 4096; ++i) check(fx.process(0, 0, s) == std::array<float, 2>{}, "Nonzero initial silence");
        fx.reset();
        std::array<std::array<float, 2>, 8192> reference{};
        for (int i = 0; i < static_cast<int>(reference.size()); ++i) reference[static_cast<std::size_t>(i)] = fx.process(i == 0 ? .5f : 0, i == 1 ? -.3f : 0, s);
        fx.reset();
        for (int i = 0; i < static_cast<int>(reference.size()); ++i)
            check(fx.process(i == 0 ? .5f : 0, i == 1 ? -.3f : 0, s) == reference[static_cast<std::size_t>(i)], "Reset not deterministic");
        fx.reset();
        for (int i = 0; i < 4096; ++i) check(fx.process(0, 0, s) == std::array<float, 2>{}, "Reset left a tail");
        check(fx.gainReductionDb() == 0, "Reset left gain reduction");
    }
}
void delayTiming() {
    CreativeSettings s;
    s[P::delayTime] = 10; s[P::delayFeedback] = 0; s[P::delayDuck] = 0; s[P::delayPingPong] = 0;
    s[P::delayLowCut] = 20; s[P::delayHighCut] = 20000;
    for (bool sync : {false, true}) for (double bpm : {120., 60., 0., std::numeric_limits<double>::quiet_NaN()}) {
        s[P::delaySync] = sync ? 1.f : 0.f; s[P::delayBeats] = .0625f;
        const double tempo = std::isfinite(bpm) && bpm > 0 ? bpm : 120;
        const int expected = sync ? static_cast<int>(48000 * 60 * .0625 / tempo) : 480;
        CreativeEffect fx; fx.prepare(48000, CreativeKind::delay);
        float atTap = 0;
        for (int i = 0; i <= expected + 100; ++i) {
            auto v = fx.process(i == 0 ? 1.f : 0.f, 0, s, bpm);
            if (i < expected) check(v[0] == 0 && v[1] == 0, "Delay leaked dry / wrong tempo timing");
            if (i == expected) atTap = v[0];
            check(v[1] == 0, "Delay crosstalk at zero feedback");
        }
        check(atTap > .7f, "Missing delay tap");
    }
    s[P::delaySync] = 0; s[P::delayFeedback] = 70; s[P::delayPingPong] = 100; s[P::delayDrive] = 0;
    CreativeEffect ping; ping.prepare(48000, CreativeKind::delay);
    for (int i = 0; i <= 1920; ++i) {
        // A centered mono source must alternate L/R, not remain centered.
        const float impulse = i == 0 ? 1.f : 0.f;
        const auto v = ping.process(impulse, impulse, s);
        if (i > 0 && i % 480 == 0) {
            const auto side = static_cast<std::size_t>((i / 480 - 1) & 1);
            check(v[side] > .1f, "Centered mono ping pong missing alternating repeat");
            check(std::abs(v[1 - side]) < .02f, "Centered mono ping pong remained centered");
        }
    }
    // At zero ping-pong, stereo excitation must remain independent and unscaled.
    s[P::delayPingPong] = 0; s[P::delayFeedback] = 0;
    ping.reset();
    for (int i = 0; i <= 480; ++i) {
        const auto v = ping.process(i == 0 ? 1.f : 0.f, i == 0 ? -.25f : 0.f, s);
        if (i == 480) {
            check(v[0] > .7f, "Normal stereo delay excitation was attenuated");
            check(std::abs(v[1] + v[0] * .25f) < 1.e-6f, "Zero ping-pong changed stereo excitation");
        }
    }
    s[P::delaySync] = 1; s[P::delayBeats] = 8; s[P::delayFeedback] = 0;
    CreativeEffect longest; longest.prepare(8000, CreativeKind::delay);
    for (int i = 0; i <= 192000; ++i) {
        const auto v = longest.process(i == 0 ? 1.f : 0.f, 0, s, 20);
        if (i < 192000) check(v == std::array<float, 2>{}, "Long synced delay was truncated");
        else check(v[0] > .7f, "Eight beats at 20 BPM missing");
    }
}
void compression() {
    CreativeSettings s;
    s[P::compressorThreshold] = -18; s[P::compressorRatio] = 4;
    s[P::compressorKnee] = 0; s[P::compressorSidechainHP] = 20;
    CreativeEffect fx; fx.prepare(48000, CreativeKind::compressor);
    double inEnergy = 0, outEnergy = 0;
    for (int i = 0; i < 96000; ++i) {
        const float x = sine(i, 48000, 1000) * .5f;
        auto v = fx.process(x, x * .5f, s);
        check(std::abs(v[0] * .5f - v[1]) < 1.e-6f, "Compressor not stereo linked");
        if (i > 48000) { inEnergy += x * x; outEnergy += v[0] * v[0]; }
    }
    const double attenuation = 10 * std::log10(inEnergy / outEnergy);
    check(attenuation > 7 && attenuation < 10, "Incorrect compressor static ratio");
    check(std::abs(fx.gainReductionDb() - attenuation) < .3, "Gain reduction meter inaccurate");
    for (int i = 0; i < 240000; ++i) fx.process(0, 0, s);
    check(fx.gainReductionDb() < .01f, "Compressor did not release");
    s[P::compressorRatio] = 1; s[P::compressorMakeup] = 6;
    fx.reset();
    auto v = fx.process(.1f, -.1f, s);
    check(std::abs(v[0] - .1f * std::pow(10.f, .3f)) < 1.e-6f, "Unity ratio / makeup failed");
    check(fx.gainReductionDb() == 0, "Unity ratio has attenuation");
}
double reverbEnergy(float decay, float width) {
    CreativeEffect fx; CreativeSettings s;
    s[P::reverbDecay] = decay; s[P::reverbDuck] = 0; s[P::reverbModulation] = 0;
    s[P::reverbWidth] = width; s[P::reverbSaturation] = 0; s[P::reverbEarly] = 0;
    fx.prepare(48000, CreativeKind::reverb);
    double tail = 0, leftEnergy = 0, rightEnergy = 0, difference = 0;
    for (int i = 0; i < 96000; ++i) {
        auto v = fx.process(i == 0 ? 1.f : 0.f, 0, s);
        finite(v);
        if (i < 1344) check(v == std::array<float, 2>{}, "Reverb leaked dry before predelay");
        if (width == 0) check(v[0] == v[1], "Zero width not mono");
        leftEnergy += v[0] * v[0]; rightEnergy += v[1] * v[1];
        difference += (v[0] - v[1]) * (v[0] - v[1]);
        if (i > 24000) tail += v[0] * v[0] + v[1] * v[1];
    }
    check(leftEnergy > 1.e-5 && rightEnergy > 1.e-5, "FDN missing stereo tail");
    if (width > 0) check(difference > 1.e-5, "FDN has no stereo diffusion");
    return tail;
}
void reverbTests() {
    const double shortTail = reverbEnergy(.3f, 100), longTail = reverbEnergy(3, 100);
    check(longTail > shortTail * 20, "RT60 does not control tail decay");
    reverbEnergy(1, 0);
    CreativeEffect fx; CreativeSettings s;
    s[P::reverbDuck] = 0; s[P::reverbModulation] = 0;
    fx.prepare(48000, CreativeKind::reverb);
    for (int i = 0; i < 24000; ++i) fx.process(i == 0 ? 1.f : 0.f, 0, s);
    s[P::reverbFreeze] = 1;
    double frozenEnergy = 0;
    for (int i = 0; i < 192000; ++i) {
        auto v = fx.process(0, 0, s); finite(v);
        if (i > 144000) frozenEnergy += v[0] * v[0] + v[1] * v[1];
    }
    check(frozenEnergy > 1.e-9, "Freeze failed to hold tail");
}
void crusherTests() {
    CreativeEffect fx; CreativeSettings s;
    s[P::crusherDrive] = 0; s[P::crusherRate] = 96000; s[P::crusherLowpass] = 20000;
    s[P::crusherBits] = 3;
    fx.prepare(48000, CreativeKind::bitcrusher);
    auto v = fx.process(.1f, -.1f, s);
    check(v == std::array<float, 2>{}, "Low bit depth did not quantize small input");
    fx.reset(); s[P::crusherBits] = 24;
    v = fx.process(.1f, -.1f, s);
    check(v[0] > .09f && v[1] < -.09f, "High bit depth not transparent within output filter");
    fx.reset(); s[P::crusherDither] = 100;
    for (int i = 0; i < 48000; ++i) check(fx.process(0, 0, s) == std::array<float, 2>{}, "Dither creates idle noise");
}
void granularTests() {
    CreativeEffect fx; CreativeSettings s;
    s[P::granularPitch] = -12; s[P::granularSize] = 200; s[P::granularDensity] = 50;
    s[P::granularJitter] = 0; s[P::granularSpread] = 0;
    fx.prepare(48000, CreativeKind::granular);
    double targetSin = 0, targetCos = 0, drySin = 0, dryCos = 0, energy = 0;
    for (int i = 0; i < 144000; ++i) {
        auto v = fx.process(sine(i, 48000, 200) * .3f, sine(i, 48000, 200) * .3f, s);
        check(v[0] == v[1], "Granular zero spread does not preserve mono"); finite(v);
        if (i > 48000) {
            targetSin += v[0] * std::sin(2 * pi * 100 * i / 48000);
            targetCos += v[0] * std::cos(2 * pi * 100 * i / 48000);
            drySin += v[0] * std::sin(2 * pi * 200 * i / 48000);
            dryCos += v[0] * std::cos(2 * pi * 200 * i / 48000);
            energy += v[0] * v[0];
        }
    }
    check(energy > 1, "Granular output missing");
    check(targetSin * targetSin + targetCos * targetCos > 100 * (drySin * drySin + dryCos * dryCos), "Granular octave pitch incorrect");
    s[P::granularFreeze] = 1;
    double frozenEnergy = 0;
    for (int i = 0; i < 144000; ++i) {
        auto v = fx.process(0, 0, s); finite(v);
        if (i > 96000) frozenEnergy += v[0] * v[0] + v[1] * v[1];
    }
    check(frozenEnergy > .1, "Granular freeze did not retain capture");
}
void granularOverlapAutomation() {
    // At unity pitch and zero scatter every surviving grain reads the same
    // waveform. This exposes coherent gain spikes hidden by random grain phase.
    for (double sr : {8000., 44100., 48000., 96000.}) for (bool scatter : {false, true}) {
        CreativeEffect fx; CreativeSettings s;
        s[P::granularPitch] = 0; s[P::granularSpread] = scatter ? 100.f : 0.f;
        s[P::granularJitter] = scatter ? 100.f : 0.f; s[P::granularDrive] = 0;
        s[P::granularPosition] = 0; s[P::granularLowCut] = 20; s[P::granularHighCut] = 20000;
        s[P::granularSize] = 250; s[P::granularDensity] = 80;
        fx.prepare(sr, CreativeKind::granular);
        float maximum = 0, maximumStep = 0, previous = 0;
        const int segment = static_cast<int>(sr * .5);
        for (int i = 0; i < segment * 8; ++i) {
            if (i == segment * 2) s[P::granularDensity] = 4;
            if (i == segment * 3) s[P::granularSize] = 20;
            if (i == segment * 4) s[P::granularDensity] = 80;
            if (i == segment * 5) s[P::granularSize] = 250;
            if (i == segment * 6) { s[P::granularDensity] = 4; s[P::granularSize] = 20; }
            const float x = sine(i, sr, 200) * .2f;
            const auto v = fx.process(x, x, s);
            finite(v);
            maximum = std::max(maximum, std::max(std::abs(v[0]), std::abs(v[1])));
            if (!scatter) check(v[0] == v[1], "Overlap normalization changed mono image");
            maximumStep = std::max(maximumStep, std::abs(v[0] - previous)); previous = v[0];
        }
        // Small filter/envelope transients are allowed, not the old ~10x peak.
        check(maximum > .1f && maximum < .22f, "Granular density/size automation caused a coherent gain spike");
        check(maximumStep < .06f, "Granular normalization introduced a discontinuity");
    }
    // An isolated, unpanned grain must retain its polynomial window, not be
    // divided by its own instantaneous weight into a rectangular pulse.
    CreativeEffect fx; CreativeSettings s;
    s[P::granularPitch] = 0; s[P::granularSpread] = s[P::granularJitter] = 0;
    s[P::granularSize] = 20; s[P::granularDensity] = 4; s[P::granularPosition] = 0;
    s[P::granularLowCut] = 20; s[P::granularHighCut] = 20000;
    fx.prepare(48000, CreativeKind::granular);
    creative_detail::Band expected;
    expected.hp = 1.f - std::exp(-2.f * creative_detail::pi * 20.f / 48000.f);
    expected.lp = 1.f - std::exp(-2.f * creative_detail::pi * 20000.f / 48000.f);
    float phase = 0;
    for (int i = 0; i < 1600; ++i) {
        const float t = phase * (1 - phase);
        const float window = phase < 1 ? 16 * t * t : 0;
        const float reference = expected.process(i >= 3 ? .2f * sine(i - 3, 48000, 1000) * window : 0);
        const auto v = fx.process(.2f * sine(i, 48000, 1000), .2f * sine(i, 48000, 1000), s);
        check(std::abs(v[0] - reference) < 2.e-6f, "Overlap normalization flattened an isolated grain window");
        phase += 1000.f / (20.f * 48000.f);
    }
}
void duckingAndCrusherRate() {
    for (auto kind : {CreativeKind::delay, CreativeKind::reverb}) {
        CreativeEffect plain, ducked; CreativeSettings a, b;
        a[P::delayDuck] = a[P::reverbDuck] = 0;
        b = a; b[P::delayDuck] = b[P::reverbDuck] = 24;
        plain.prepare(48000, kind); ducked.prepare(48000, kind);
        double reference = 0, attenuated = 0;
        for (int i = 0; i < 96000; ++i) {
            const float x = sine(i, 48000, 311) * .4f;
            const auto u = plain.process(x, x, a), v = ducked.process(x, x, b);
            if (i > 48000) { reference += u[0] * u[0]; attenuated += v[0] * v[0]; }
        }
        check(reference > 1.e-6 && attenuated < reference * .02, "Wet ducking did not attenuate during vocal input");
    }
    CreativeEffect fast, slow; CreativeSettings a, b;
    a[P::crusherRate] = 96000; a[P::crusherLowpass] = 20000; a[P::crusherDrive] = 0;
    b = a; b[P::crusherRate] = 1000;
    fast.prepare(48000, CreativeKind::bitcrusher); slow.prepare(48000, CreativeKind::bitcrusher);
    double fastMotion = 0, slowMotion = 0; float previousFast = 0, previousSlow = 0;
    for (int i = 0; i < 24000; ++i) {
        const float x = (i & 1) ? -.4f : .4f;
        const float u = fast.process(x, x, a)[0], v = slow.process(x, x, b)[0];
        fastMotion += std::abs(u - previousFast); slowMotion += std::abs(v - previousSlow);
        previousFast = u; previousSlow = v;
    }
    check(slowMotion < fastMotion * .1, "Crusher rate did not introduce sample holding");
}
void pitchedDelayAndTempoSlew() {
    CreativeEffect fx; CreativeSettings s;
    s[P::delayPitch] = -12; s[P::delayFeedback] = 0; s[P::delayDuck] = 0;
    s[P::delayDrive] = 0; s[P::delayLowCut] = 20; s[P::delayHighCut] = 20000;
    fx.prepare(48000, CreativeKind::delay);
    double targetSin = 0, targetCos = 0, drySin = 0, dryCos = 0;
    for (int i = 0; i < 96000; ++i) {
        const float x = sine(i, 48000, 200) * .3f;
        const float y = fx.process(x, x, s)[0];
        if (i >= 48000) {
            targetSin += y * std::sin(2 * pi * 100 * i / 48000);
            targetCos += y * std::cos(2 * pi * 100 * i / 48000);
            drySin += y * std::sin(2 * pi * 200 * i / 48000);
            dryCos += y * std::cos(2 * pi * 200 * i / 48000);
        }
    }
    check(targetSin * targetSin + targetCos * targetCos > 100 * (1 + drySin * drySin + dryCos * dryCos), "Delay pitch shift incorrect");
    s[P::delayPitch] = 0; s[P::delaySync] = 1;
    float previous = 0, maximumStep = 0;
    for (int i = 0; i < 96000; ++i) {
        if (i == 24000) s[P::delayBeats] = .25f;
        const float x = sine(i, 48000, 200) * .3f;
        const float y = fx.process(x, x, s, i < 48000 ? 120 : 75)[0];
        if (i > 0) maximumStep = std::max(maximumStep, std::abs(y - previous));
        previous = y;
    }
    check(maximumStep < .25f, "Pitch/tempo automation jumped delay taps");
}
void automationAndRealtime() {
    for (double sr : {8000., 44100., 96000., 192000.}) for (auto kind : kinds) {
        CreativeEffect fx; CreativeSettings s;
        fx.prepare(sr, kind);
        bool allFinite = true;
        audioThread = true;
        for (int i = 0; i < 48000; ++i) {
            if (i % 113 == 0) for (std::size_t p = 0; p < creativeParameterCount; ++p) {
                const auto& d = creativeParameters[p];
                s.values[p] = ((i / 113 + static_cast<int>(p)) & 1) ? d.min : d.max;
            }
            if (i % 997 == 0) s.values[static_cast<std::size_t>(i) % creativeParameterCount] = std::numeric_limits<float>::quiet_NaN();
            const float x = i % 777 == 0 ? std::numeric_limits<float>::infinity() : sine(i, sr) * .5f;
            const auto v = fx.process(x, -.5f * x, s, i % 401 == 0 ? -1 : 85 + i % 200);
            allFinite = allFinite && std::isfinite(v[0]) && std::isfinite(v[1]) && std::abs(v[0]) <= 64 && std::abs(v[1]) <= 64;
        }
        fx.reset();
        audioThread = false;
        check(allFinite, "Automation destabilized DSP");
    }
    check(audioAllocations == 0 && audioDestructions == 0, "Audio processing/reset allocated or freed heap storage");
}
void blockIndependenceAndPreparation() {
    for (auto kind : kinds) {
        CreativeEffect a, b; CreativeSettings s;
        a.prepare(48000, kind); b.prepare(48000, kind);
        constexpr std::array<int, 7> blocks {1, 17, 64, 3, 257, 511, 31};
        int position = 0;
        for (int block = 0; position < 24000; ++block) {
            const int count = std::min(blocks[static_cast<std::size_t>(block) % blocks.size()], 24000 - position);
            for (int j = 0; j < count; ++j, ++position) {
                const float x = sine(position, 48000) * .25f;
                check(a.process(x, -x, s) == b.process(x, -x, s), "External grouping changed output");
            }
        }
        for (auto next : kinds) {
            a.prepare(44100, next);
            for (int i = 0; i < 2048; ++i) check(a.process(0, 0, s) == std::array<float, 2>{}, "Reprepare retained previous kind state");
        }
    }
    for (double invalid : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        CreativeEffect fx; fx.prepare(invalid, CreativeKind::delay);
        finite(fx.process(.5f, -.5f, CreativeSettings{}));
    }
}
void decayingTails() {
    for (auto kind : {CreativeKind::delay, CreativeKind::reverb, CreativeKind::granular}) {
        CreativeEffect fx; CreativeSettings s;
        s[P::delayFeedback] = 50; s[P::delayTime] = 40; s[P::delayDuck] = 0;
        s[P::reverbDecay] = .3f; s[P::reverbDuck] = 0;
        fx.prepare(48000, kind);
        double lastSecond = 0;
        for (int i = 0; i < 480000; ++i) {
            const auto v = fx.process(i == 0 ? 1.f : 0.f, 0, s); finite(v);
            if (i >= 432000) lastSecond += v[0] * v[0] + v[1] * v[1];
        }
        check(lastSecond < 1.e-12, "Unfrozen tail does not decay to silence");
    }
}
} // namespace
int main() {
    try {
        descriptors(); silenceAndReset(); delayTiming(); compression(); reverbTests();
        crusherTests(); granularTests(); granularOverlapAutomation(); duckingAndCrusherRate(); pitchedDelayAndTempoSlew();
        automationAndRealtime(); blockIndependenceAndPreparation(); decayingTails();
        std::cout << "CreativeEffects: all tests passed (48 parameters, 5 effects; zero audio allocations/frees)\n";
        return 0;
    } catch (const std::exception& e) {
        audioThread = false;
        std::cerr << "CreativeEffects: " << e.what() << '\n';
        return 1;
    }
}
