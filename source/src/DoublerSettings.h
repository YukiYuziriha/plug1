#pragma once

#include <algorithm>
#include <array>

namespace vocalstack {

// DSP bounds: gain -60..+12 dB, pan -1..+1, delay 0..100 ms,
// detune +/-100 cents, formant +/-24 st (shared + voice can reach +/-48),
// musical pitch +/-36 st, depth 0..2, rate 0..8 Hz.
// Depth zero removes motion, not static detune/formant/delay offsets.
struct VoiceSettings {
    bool enabled;
    float gainDb, pan, delayMs, detuneCents, formantSemitones, depth, rateHz;
    float pitchSemitones = 0.0f;
    bool present = true; // Removal is distinct from enabled/mute; stable slot identity.
    float tuneMix = 1.0f; // Appended for aggregate compatibility; clean/tuned source blend.
};

// Stored offsets already include the chosen Human value. In advanced mode Human
// scales only additional motion; it must not be added to these offsets again.
inline std::array<VoiceSettings, 8> basicVoiceSettings(float human) {
    human = std::clamp(human, 0.0f, 1.0f);
    const float cents = 4.5f + 8.0f * human;
    const float formant = 0.30f + 0.55f * human;
    return {{{true, 0, -1, 13, -cents, -formant, 1, 1},
             {true, 0,  1, 24,  cents,  formant, 1, 1},
             {false, 0, -0.4f, 19, -cents, -formant, 1, 1, 0, false},
             {false, 0,  0.4f, 31,  cents,  formant, 1, 1, 0, false},
             {false, 0, -0.7f, 37, -cents, -formant, 1, 1, 0, false},
             {false, 0,  0.7f, 43,  cents,  formant, 1, 1, 0, false},
             {false, 0, -0.2f, 49, -cents, -formant, 1, 1, 0, false},
             {false, 0,  0.2f, 55,  cents,  formant, 1, 1, 0, false}}};
}

} // namespace vocalstack
