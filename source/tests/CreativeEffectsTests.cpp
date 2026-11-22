// Standalone, no JUCE or project libraries. Example: c++ -std=c++17 -O2
// -Isource/src source/tests/CreativeEffectsTests.cpp -o CreativeEffectsTests
#include "../src/CreativeEffects.h"
#include "../src/VocalGlitch.h"
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
    check(creativeParameterCount == 68, "Host parameter count changed");
    constexpr std::array<const char*, 48> legacyIds {{
        "delay_time", "delay_sync", "delay_beats", "delay_feedback", "delay_pingpong",
        "delay_lowcut", "delay_highcut", "delay_drive", "delay_duck", "delay_duck_release", "delay_pitch",
        "reverb_predelay", "reverb_size", "reverb_decay", "reverb_diffusion", "reverb_early",
        "reverb_lowcut", "reverb_highcut", "reverb_damping", "reverb_width", "reverb_modulation",
        "reverb_duck", "reverb_duck_release", "reverb_saturation", "reverb_freeze",
        "compressor_threshold", "compressor_ratio", "compressor_knee", "compressor_attack",
        "compressor_release", "compressor_makeup", "compressor_sidechain_hp",
        "crusher_bits", "crusher_rate", "crusher_lowpass", "crusher_drive", "crusher_dither",
        "granular_pitch", "granular_size", "granular_density", "granular_spread", "granular_position",
        "granular_freeze", "granular_reverse", "granular_lowcut", "granular_highcut", "granular_jitter", "granular_drive"
    }};
    for (std::size_t i = 0; i < legacyIds.size(); ++i)
        check(std::strcmp(creativeParameters[i].id, legacyIds[i]) == 0, "Legacy host ID/index moved");
    check(static_cast<std::size_t>(P::reverbPitch) == 48
        && static_cast<std::size_t>(P::reverbPitchAmount) == 49
        && static_cast<std::size_t>(P::reverbSwell) == 50, "Reverb controls must be appended");
    CreativeSettings s;
    std::array<int, static_cast<std::size_t>(CreativeKind::vocalGlitch) + 1> counts{};
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
// A sustained sine has no energy at either octave until it passes through
// the feedback pitch path. Measure both stereo channels to avoid FDN notches.
void reverbPitchAndSwell() {
    for (double sr : {8000., 48000., 192000.}) for (float pitch : {-12.f, 12.f}) {
        CreativeEffect plain, shifted; CreativeSettings a, b;
        a[P::reverbPredelay] = 0; a[P::reverbDuck] = 0; a[P::reverbModulation] = 0;
        a[P::reverbDamping] = 20000; a[P::reverbLowCut] = 20; a[P::reverbHighCut] = 20000;
        a[P::reverbSaturation] = 0; a[P::reverbEarly] = 0; a[P::reverbDecay] = 5;
        b = a; b[P::reverbPitch] = pitch; b[P::reverbPitchAmount] = 100;
        plain.prepare(sr, CreativeKind::reverb); shifted.prepare(sr, CreativeKind::reverb);
        std::array<double, 2> refSin{}, refCos{}, outSin{}, outCos{};
        const double hz = pitch < 0 ? 400 : 200;
        const double target = hz * std::pow(2., pitch / 12.);
        for (int i = 0; i < static_cast<int>(sr * 3); ++i) {
            const float x = sine(i, sr, hz) * .15f;
            const auto u = plain.process(x, x, a), v = shifted.process(x, x, b);
            finite(v);
            if (i >= static_cast<int>(sr)) for (int c = 0; c < 2; ++c) {
                const double phase = 2 * pi * target * i / sr;
                refSin[c] += u[c] * std::sin(phase); refCos[c] += u[c] * std::cos(phase);
                outSin[c] += v[c] * std::sin(phase); outCos[c] += v[c] * std::cos(phase);
            }
        }
        double reference = 0, octave = 0;
        for (int c = 0; c < 2; ++c) {
            reference += refSin[c] * refSin[c] + refCos[c] * refCos[c];
            octave += outSin[c] * outSin[c] + outCos[c] * outCos[c];
        }
        check(octave > 100 * (1 + reference), "Reverb feedback missing audible up/down octave");
    }
    for (float pitch : {-12.f, 0.f, 12.f}) {
        CreativeEffect plain, disabled; CreativeSettings a, b;
        b = a; b[P::reverbPitch] = pitch;
        // Zero amount, or unity pitch even at full amount, retains the legacy sound.
        b[P::reverbPitchAmount] = pitch == 0 ? 100.f : 0.f;
        plain.prepare(48000, CreativeKind::reverb); disabled.prepare(48000, CreativeKind::reverb);
        for (int i = 0; i < 24000; ++i) {
            const float x = sine(i, 48000) * .2f;
            check(plain.process(x, -.5f * x, a) == disabled.process(x, -.5f * x, b), "Disabled pitch changed reverb");
        }
    }
    for (double sr : {8000., 48000., 384000.}) {
        CreativeEffect plain, swell; CreativeSettings a, b;
        a[P::reverbPredelay] = 0; a[P::reverbDuck] = 0; a[P::reverbEarly] = 100;
        a[P::reverbModulation] = 0; a[P::reverbWidth] = 0;
        b = a; b[P::reverbSwell] = 600;
        plain.prepare(sr, CreativeKind::reverb); swell.prepare(sr, CreativeKind::reverb);
        double earlyPlain = 0, earlySwell = 0, latePlain = 0, lateSwell = 0;
        for (int i = 0; i < static_cast<int>(sr * 3); ++i) {
            const float x = .2f * sine(i, sr, 311);
            const auto u = plain.process(x, x, a), v = swell.process(x, x, b);
            finite(v); check(v[0] == v[1], "Swell changed mono width");
            if (i < sr * .1) { earlyPlain += u[0] * u[0]; earlySwell += v[0] * v[0]; }
            if (i > sr * 2) { latePlain += u[0] * u[0]; lateSwell += v[0] * v[0]; }
        }
        check(earlyPlain > 1.e-5 && earlySwell < earlyPlain * .08, "Wet attack did not soften onset");
        check(lateSwell > latePlain * .5 && lateSwell <= latePlain * 1.001, "Swell did not recover / boosted tail");
        // An isolated transient must also swell, rather than just gating the dry input.
        plain.reset(); swell.reset(); earlyPlain = earlySwell = latePlain = lateSwell = 0;
        for (int i = 0; i < static_cast<int>(sr); ++i) {
            const auto u = plain.process(i == 0 ? .5f : 0, 0, a);
            const auto v = swell.process(i == 0 ? .5f : 0, 0, b);
            if (i < sr * .05) { earlyPlain += u[0] * u[0]; earlySwell += v[0] * v[0]; }
            if (i > sr * .4) { latePlain += u[0] * u[0]; lateSwell += v[0] * v[0]; }
        }
        check(earlySwell < earlyPlain * .08, "Impulse onset was not softened");
        check(lateSwell > latePlain * .01, "Swell gated away the impulse tail");
    }
}
void reverbPitchFreezeAndReset() {
    for (double sr : {8000., 44100., 48000., 96000., 192000., 384000.}) for (float pitch : {-12.f, 12.f}) {
        CreativeEffect fx; CreativeSettings s;
        s[P::reverbPitch] = pitch; s[P::reverbPitchAmount] = 100; s[P::reverbSwell] = 200;
        s[P::reverbDuck] = 0; s[P::reverbDecay] = 20;
        fx.prepare(sr, CreativeKind::reverb);
        for (int i = 0; i < static_cast<int>(sr * .4); ++i)
            fx.process(.15f * sine(i, sr, 400), .1f * sine(i, sr, 311), s);
        s[P::reverbFreeze] = 1;
        double first = 0, last = 0;
        for (int i = 0; i < static_cast<int>(sr * 4); ++i) {
            const auto v = fx.process(0, 0, s); finite(v);
            const double energy = v[0] * v[0] + v[1] * v[1];
            if (i >= sr && i < sr * 2) first += energy;
            if (i >= sr * 3) last += energy;
        }
        check(last > 1.e-12 && last < first * 2, "Pitched freeze died or grew unstable");
        s[P::reverbFreeze] = 0;
        CreativeEffect fresh; fresh.prepare(sr, CreativeKind::reverb);
        fx.reset();
        double replayEnergy = 0;
        for (int i = 0; i < static_cast<int>(sr * .4); ++i) {
            const auto v = fx.process(i == 0 ? .5f : 0, 0, s);
            check(v == fresh.process(i == 0 ? .5f : 0, 0, s), "Pitched/swell reset not deterministic");
            replayEnergy += v[0] * v[0] + v[1] * v[1];
        }
        check(replayEnergy > 1.e-12, "Pitched reset replay did not exercise the tail");
        fx.reset();
        for (int i = 0; i < static_cast<int>(sr * .2); ++i)
            check(fx.process(0, 0, s) == std::array<float, 2>{}, "Reset retained pitched/swell tail");
    }
}
void reverbPitchedDecay() {
    for (float pitch : {-12.f, 12.f}) for (float amount : {25.f, 100.f}) {
        CreativeEffect fx; CreativeSettings s;
        s[P::reverbPitch] = pitch; s[P::reverbPitchAmount] = amount;
        s[P::reverbSwell] = 2000; s[P::reverbDecay] = .3f; s[P::reverbDuck] = 0;
        fx.prepare(48000, CreativeKind::reverb);
        double onset = 0, tail = 0;
        for (int i = 0; i < 480000; ++i) {
            const auto v = fx.process(i == 0 ? 1.f : 0, 0, s); finite(v);
            const double energy = v[0] * v[0] + v[1] * v[1];
            if (i < 48000) onset += energy;
            if (i >= 432000) tail += energy;
        }
        check(onset > 1.e-12 && tail < 1.e-12, "Pitched/swell tail missing or failed to decay");
    }
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
    for (double sr : {8000., 44100., 48000., 96000., 192000., 384000.}) for (auto kind : kinds) {
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
constexpr double glitchRate = 1000.0, glitchTempo = 120.0;
constexpr double glitchPpqPerSample = glitchTempo / (60.0 * glitchRate);
constexpr std::array<float, 8> glitchFade {0, 1.f / 3, 2.f / 3, 1, 1, 2.f / 3, 1.f / 3, 0};
using GlitchFrame = std::array<float, 2>;
GlitchFrame glitchSample(VocalGlitch& fx, GlitchFrame in = {}) {
    GlitchFrame out{};
    fx.process(in[0], in[1], out[0], out[1]);
    return out;
}
void checkGlitchFrame(GlitchFrame actual, GlitchFrame expected, const char* message) {
    check(std::abs(actual[0] - expected[0]) < 1.e-6f && std::abs(actual[1] - expected[1]) < 1.e-6f, message);
}
void glitchTimedGrab() {
    constexpr int grabOffset = 13, sliceLength = 8, beatSamples = 500;
    const auto source = [](int i) -> GlitchFrame { return { .02f * i, -.01f * (i + 4) }; };
    constexpr GlitchFrame live {-.25f, .375f};
    for (bool layer : {false, true}) {
        VocalGlitch fx;
        fx.prepare(glitchRate);
        VocalGlitch::Settings s;
        s.lengthMs = sliceLength; s.pattern = 3; s.layer = layer;
        fx.setSettings(s);
        // Dispatch the grab before this sample, as for an in-block MIDI event.
        // Keep feeding distinct live audio long enough to overwrite the input ring.
        for (int i = 0; i < 1300; ++i) {
            if (i == grabOffset) fx.grab();
            const auto in = i < grabOffset ? source(i) : live;
            check(glitchSample(fx, in) == in, "VocalGlitch grab changed dry audio before fire");
        }
        fx.fire();
        // Half-speed interpolation exposes both endpoints despite the edge fades.
        for (int i = 0; i < 2 * sliceLength; ++i) {
            if (i == 2) fx.grab(); // Repin while the original slice is playing.
            const int index = grabOffset - sliceLength + i / 2;
            const auto a = source(index);
            const auto b = (i % 2 != 0 && index + 1 < grabOffset) ? source(index + 1) : a;
            const float wet = std::min({i, 2 * sliceLength - 1 - i, 3}) / 3.f;
            const float dry = layer ? 1.f : 1.f - std::min(i, 3) / 3.f;
            checkGlitchFrame(glitchSample(fx, live),
                {dry * live[0] + wet * .5f * (a[0] + b[0]), dry * live[1] + wet * .5f * (a[1] + b[1])},
                "VocalGlitch changed the grabbed stereo window or Replace/Layer mix");
        }
        for (int i = 2 * sliceLength; i < beatSamples; ++i) glitchSample(fx, live);
        check(glitchSample(fx, live) == live, "VocalGlitch did not restore dry audio after playback");
        fx.fire();
        for (int i = 0; i < 3; ++i) glitchSample(fx);
        checkGlitchFrame(glitchSample(fx), live, "VocalGlitch did not pin the new grab during playback");
    }
}
void glitchQuantizedFire() {
    constexpr int boundary = 125; // A sixteenth note at 120 BPM and 1 kHz.
    constexpr int jumpSample = 109;
    constexpr std::array<int, 6> blocks {0, 17, 64, jumpSample, 144, 250};
    constexpr GlitchFrame captured {.375f, -.25f};
    for (bool jump : {false, true}) {
        VocalGlitch fx;
        fx.prepare(glitchRate);
        VocalGlitch::Settings s;
        s.lengthMs = 8; s.pattern = 1; s.grid = 1; s.layer = true;
        fx.setSettings(s);
        for (std::size_t block = 0; block + 1 < blocks.size(); ++block) {
            const int start = blocks[block];
            const double ppq = jump && start >= jumpSample
                ? 2.0 + (start - jumpSample) * glitchPpqPerSample : start * glitchPpqPerSample;
            fx.setClock(ppq, glitchTempo, true, jump && start == jumpSample);
            for (int i = start; i < blocks[block + 1]; ++i) {
                if (i == 20) { fx.grab(); fx.fire(); }
                if (i == 60) fx.grab(); // A new grab must not change the queued slice.
                const GlitchFrame in = i < 20 ? captured : GlitchFrame{};
                const int offset = i - boundary;
                const float wet = !jump && offset >= 0 && offset < 8 ? glitchFade[static_cast<std::size_t>(offset)] : 0;
                checkGlitchFrame(glitchSample(fx, in), {in[0] + wet * captured[0], in[1] + wet * captured[1]},
                    jump ? "VocalGlitch launched a queued fire after a transport jump"
                         : "VocalGlitch quantized fire missed the cross-block grid or lost its slice");
            }
        }
    }
}
void glitchAutomaticTail() {
    constexpr int phraseEnd = 64, boundary = 250;
    const auto voice = [](int i) -> GlitchFrame { return {0, -.25f - .004f * i}; };
    for (bool onset : {false, true}) {
        VocalGlitch fx;
        fx.prepare(glitchRate);
        VocalGlitch::Settings s;
        s.lengthMs = 8; s.thresholdDb = -20; s.pattern = 1; s.grid = 1;
        s.layer = true; s.automatic = true;
        fx.setSettings(s);
        fx.setClock(0, glitchTempo, true, false);
        for (int i = 0; i < (onset ? 500 : 375); ++i) {
            if (i == 200) fx.setClock(i * glitchPpqPerSample, glitchTempo, true, false);
            // One right-channel sample above threshold, too short to reopen the
            // smoothed detector, must cancel even on the scheduled grid sample.
            const auto in = i < phraseEnd ? voice(i) : (onset && i == boundary ? GlitchFrame{0, .125f} : GlitchFrame{});
            auto expected = in;
            const int offset = i - boundary;
            if (!onset && offset >= 0 && offset < 8)
                expected[1] += glitchFade[static_cast<std::size_t>(offset)] * voice(phraseEnd - 8 + offset)[1];
            checkGlitchFrame(glitchSample(fx, in), expected,
                onset ? "VocalGlitch did not cancel its automatic launch on onset"
                      : "VocalGlitch automatic launch did not replay the phrase tail without debounce silence");
        }
    }
}
} // namespace
int main() {
    try {
        descriptors(); silenceAndReset(); delayTiming(); compression(); reverbTests();
        reverbPitchAndSwell(); reverbPitchFreezeAndReset(); reverbPitchedDecay();
        crusherTests(); granularTests(); granularOverlapAutomation(); duckingAndCrusherRate(); pitchedDelayAndTempoSlew();
        automationAndRealtime(); blockIndependenceAndPreparation(); decayingTails();
        glitchTimedGrab(); glitchQuantizedFire(); glitchAutomaticTail();
        std::cout << "CreativeEffects: all tests passed (68 parameters, legacy 5 effects; zero audio allocations/frees)\n";
        return 0;
    } catch (const std::exception& e) {
        audioThread = false;
        std::cerr << "CreativeEffects: " << e.what() << '\n';
        return 1;
    }
}
