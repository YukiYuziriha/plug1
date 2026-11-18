#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vocalstack {

enum class TuningScale { Chromatic, Major, NaturalMinor, HarmonicMinor, MajorPentatonic, MinorPentatonic, Custom };
enum class TuningVocalRange { Wide, Low, Mid, High };

// Value snapshot owned/published by the caller. Enable/bypass and latency-aligned
// dry/wet mixing deliberately belong to the engine, not this wet-only DSP.
struct TuningSettings {
    int root = 0;                         // C=0 ... B=11
    TuningScale scale = TuningScale::Chromatic;
    std::uint16_t allowedNotesMask = 0x0fff; // Absolute pitch classes: bit 0=C.
    float referenceHz = 440;
    TuningVocalRange vocalRange = TuningVocalRange::Wide;
    float amount = 1;                     // 0..1 of correction in semitones
    float retuneMs = 0;                   // Error smoothing time constant; 0=hard.
    float transitionMs = 0;               // Target-note glide time constant; 0=hard.
    float stability = 0.25f;              // 0..1: 0..40 cents hysteresis, 0..30 ms dwell
    bool preserveFormants = true;
};

inline float tuningFiniteClamp(float value, float lo, float hi, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}

inline TuningSettings sanitiseTuningSettings(TuningSettings s) noexcept {
    s.root = (s.root % 12 + 12) % 12;
    s.allowedNotesMask &= 0x0fff;
    s.referenceHz = tuningFiniteClamp(s.referenceHz, 400, 480, 440);
    s.amount = tuningFiniteClamp(s.amount, 0, 1, 1);
    s.retuneMs = tuningFiniteClamp(s.retuneMs, 0, 2000, 0);
    s.transitionMs = tuningFiniteClamp(s.transitionMs, 0, 2000, 0);
    s.stability = tuningFiniteClamp(s.stability, 0, 1, 0.25f);
    return s;
}

// Non-custom scales intersect the absolute allowed-note mask. An empty result
// means no correction (never silently substitutes a chromatic scale).
inline std::uint16_t tuningNoteMask(const TuningSettings& s) noexcept {
    unsigned relative = 0x0fff;
    switch (s.scale) {
        case TuningScale::Major: relative = 0x0ab5; break;
        case TuningScale::NaturalMinor: relative = 0x05ad; break;
        case TuningScale::HarmonicMinor: relative = 0x09ad; break;
        case TuningScale::MajorPentatonic: relative = 0x0295; break;
        case TuningScale::MinorPentatonic: relative = 0x04a9; break;
        default: break;
    }
    const unsigned root = unsigned((s.root % 12 + 12) % 12);
    return std::uint16_t(((relative << root) | (relative >> (12 - root))) & s.allowedNotesMask & 0x0fff);
}

// Deterministic lower-note tie break. MIDI-valued input is relative to referenceHz.
inline int nearestTuningNote(float midi, std::uint16_t mask) noexcept {
    if (!std::isfinite(midi) || !(mask & 0x0fff)) return -1;
    const int base = int(std::floor(std::clamp(midi, -100.f, 200.f)));
    int best = base;
    float distance = 1000;
    for (int note = base - 12; note <= base + 12; ++note) {
        if (!(mask & (1u << ((note % 12 + 12) % 12)))) continue;
        const float d = std::abs(midi - float(note));
        if (d < distance) { distance = d; best = note; }
    }
    return best;
}

} // namespace vocalstack
