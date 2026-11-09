#pragma once

#include <algorithm>
#include <array>

namespace vocalstack {

// DSP bounds: gain -60..+12 dB, pan -1..+1, delay 0..100 ms,
// detune +/-100 cents, formant +/-12 semitones, depth 0..2, rate 0..8 Hz.
// Depth zero removes motion, not static detune/formant/delay offsets.
struct VoiceSettings {
    bool enabled;
    float gainDb, pan, delayMs, detuneCents, formantSemitones, depth, rateHz;
};

// Stored offsets already include the chosen Human value. In advanced mode Human
// scales only additional motion; it must not be added to these offsets again.
inline std::array<VoiceSettings, 4> basicVoiceSettings(float human) {
    human = std::clamp(human, 0.0f, 1.0f);
    const float cents = 4.5f + 8.0f * human;
    const float formant = 0.30f + 0.55f * human;
    return {{{true, 0, -1, 13, -cents, -formant, 1, 1},
             {true, 0,  1, 24,  cents,  formant, 1, 1},
             {false, 0, -0.4f, 19, -cents, -formant, 1, 1},
             {false, 0,  0.4f, 31,  cents,  formant, 1, 1}}};
}

} // namespace vocalstack
