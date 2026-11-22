#pragma once

#include <array>
#include <cstddef>
#include <initializer_list>

#include <juce_core/juce_core.h>
#include "Routing.h"

namespace vocalstack::expandedFactoryPresets {

inline constexpr std::array<const char*, 20> names {{
    // Lead-ready
    "Glass Center", "Silk Rebound", "Basement Anchor", "Chorus Armor",
    "Prism Hook", "Dry Spark", "Pocket Relay", "Close Quarters",
    "Warm Voltage", "Gloss Overdrive",
    // Wild
    "Backspun Confetti", "Octave Dominoes", "Subterranean Twin", "Pixel Megaphone",
    "Vowel Reactor", "Trapdoor Room", "Comet Choir", "Wire Resonator",
    "Split Personality", "Slow Orbit"
}};

template <typename Parameter>
struct Setting { Parameter parameter; float value; };

// Applies a zero-based recipe to a fresh factory context. Effects are appended in signal order.
template <typename Context>
void apply(int index, const Context& ctx) {
    if (index < 0 || static_cast<std::size_t>(index) >= names.size()) return;

    using C = CreativeParameter;
    using U = UtilityParameter;
    using N = NodeType;
    const auto creative = [&](N type, float mix, std::initializer_list<Setting<C>> settings) {
        const auto prefix = ctx.effect(type, mix);
        for (const auto& [parameter, value] : settings)
            ctx.raw(prefix + creativeParameters[static_cast<std::size_t>(parameter)].id, value);
    };
    const auto utility = [&](N type, float mix, std::initializer_list<Setting<U>> settings) {
        const auto prefix = ctx.effect(type, mix);
        for (const auto& [parameter, value] : settings)
            ctx.raw(prefix + utilityParameters[static_cast<std::size_t>(parameter)].id, value);
    };
    const auto stack = [&](float mix, float human, float width) {
        ctx.raw("double", mix);
        ctx.raw("human", human);
        ctx.raw("width", width);
    };
    const auto level = [&](float amount) {
        ctx.raw("levelOn", 1);
        ctx.raw("level", amount);
    };
    const auto trim = [&](float db) { ctx.raw("outputGain", db); };

    ctx.raw("leadOn", 1);
    switch (index) {
        // Lead-ready: keep the original vocal in front of the signature.
        case 0: { // Glass Center: tight stereo doubles into a bright, short plate.
            stack(32, 18, 82);
            ctx.voice(1, -6, -.74f, 13, -8, 0, -.4f, .18f, .62f);
            ctx.voice(2, -7, .78f, 26, 9, 0, .6f, .21f, .81f);
            const auto eq = ctx.effect(N::equalizer, 100);
            ctx.raw(eq + "EqBand1On", 1);
            ctx.raw(eq + "EqBand1Frequency", 320);
            ctx.raw(eq + "EqBand1Gain", -3);
            ctx.raw(eq + "EqBand1Q", 1.1f);
            ctx.raw(eq + "EqBand2On", 1);
            ctx.raw(eq + "EqBand2Frequency", 6500);
            ctx.raw(eq + "EqBand2Mode", 2); // High shelf
            ctx.raw(eq + "EqBand2Gain", 2);
            creative(N::convolution, 23, {{C::convolutionSpace, 3}, {C::convolutionBrightness, 3},
                {C::convolutionPredelay, 31}, {C::convolutionTail, 57}, {C::convolutionOutput, -3}});
            trim(-4);
            break;
        }
        case 1: // Silk Rebound: one dark slap, tucked behind an intimate pop lead.
            stack(0, 0, 65);
            level(23);
            creative(N::delay, 27, {{C::delayTime, 103}, {C::delayFeedback, 9}, {C::delayPingPong, 27},
                {C::delayLowCut, 250}, {C::delayHighCut, 3400}, {C::delayDrive, 0},
                {C::delayDuck, 3}, {C::delayDuckRelease, 140}});
            trim(-5);
            break;
        case 2: // Basement Anchor: centered low octave under a close trap lead.
            stack(38, 6, 24);
            ctx.voice(1, -4, 0, 4, 0, -12, -2.5f, .06f, .4f);
            utility(N::xpFilter, 100, {{U::xpMode, 1}, {U::xpCutoff, 72}});
            creative(N::compressor, 82, {{C::compressorThreshold, -23}, {C::compressorRatio, 3.8f},
                {C::compressorKnee, 7}, {C::compressorAttack, 16}, {C::compressorRelease, 105},
                {C::compressorSidechainHP, 125}});
            trim(-4);
            break;
        case 3: { // Chorus Armor: pop-punk gang edges with fast, compressed bite.
            stack(42, 36, 88);
            ctx.voice(1, -6, -.84f, 19, -12, 0, -1, .27f, .82f);
            ctx.voice(2, -6, .86f, 32, 14, 0, .7f, .30f, 1.13f);
            ctx.voice(3, -11, -.28f, 45, 3, 0, -1.8f, .34f, .67f);
            creative(N::compressor, 68, {{C::compressorThreshold, -26}, {C::compressorRatio, 5},
                {C::compressorKnee, 5}, {C::compressorAttack, 3}, {C::compressorRelease, 72}});
            const auto wave = ctx.effect(N::waveshaper, 46);
            ctx.raw(wave + "WaveShape", 2);
            ctx.raw(wave + "WaveDrive", 18);
            ctx.raw(wave + "WaveOutput", -4);
            ctx.raw(wave + "WaveLevelMatch", 1);
            trim(-5);
            break;
        }
        case 4: { // Prism Hook: upper-octave sparkle behind an animated K-pop lead.
            stack(33, 15, 78);
            ctx.voice(1, -6, -.48f, 16, -5, 12, 1.2f, .16f, .9f);
            ctx.voice(2, -7, .58f, 21, 8, 0, .5f, .24f, 1.23f);
            const auto prefix = ctx.effect(N::chorale, 33);
            ctx.raw(prefix + "Motion", 57);
            trim(-5);
            break;
        }
        case 5: // Dry Spark: focused hip-hop consonants with dry, grainy edge.
            stack(0, 0, 65);
            utility(N::gate, 100, {{U::gateThreshold, -48}, {U::gateRange, 12}, {U::gateRatio, 3},
                {U::gateAttack, .7f}, {U::gateHold, 38}, {U::gateRelease, 135}});
            creative(N::saturator, 52, {{C::saturatorDrive, 18}, {C::saturatorCharacter, 66},
                {C::saturatorTone, 4}, {C::saturatorOutput, -5}, {C::saturatorLevelMatch, 1}});
            trim(-5);
            break;
        case 6: // Pocket Relay: dotted-eighth ping-pong that opens between phrases.
            stack(0, 0, 65);
            creative(N::delay, 32, {{C::delaySync, 1}, {C::delayBeats, .75f}, {C::delayFeedback, 34},
                {C::delayPingPong, 90}, {C::delayLowCut, 290}, {C::delayHighCut, 4600},
                {C::delayDrive, 0}, {C::delayDuck, 8}, {C::delayDuckRelease, 155}});
            trim(-4);
            break;
        case 7: // Close Quarters: audible early room reflections without a long wash.
            stack(0, 0, 65);
            level(28);
            creative(N::compressor, 65, {{C::compressorThreshold, -24}, {C::compressorRatio, 3},
                {C::compressorAttack, 10}, {C::compressorRelease, 85}});
            creative(N::reverb, 18, {{C::reverbPredelay, 11}, {C::reverbSize, 23}, {C::reverbDecay, .48f},
                {C::reverbEarly, 73}, {C::reverbDiffusion, 55}, {C::reverbWidth, 76},
                {C::reverbLowCut, 290}, {C::reverbHighCut, 6200}, {C::reverbDuck, 3}});
            trim(-6);
            break;
        case 8: // Warm Voltage: off-center double and a short, dark hall for melodic rap.
            stack(35, 29, 58);
            ctx.voice(1, -5, -.36f, 35, -11, 0, -1.6f, .26f, .67f);
            creative(N::saturator, 44, {{C::saturatorDrive, 14}, {C::saturatorCharacter, 46},
                {C::saturatorTone, -3}, {C::saturatorOutput, -3}, {C::saturatorLevelMatch, 1}});
            creative(N::convolution, 25, {{C::convolutionSpace, 1}, {C::convolutionBrightness, -5},
                {C::convolutionPredelay, 44}, {C::convolutionTail, 51}, {C::convolutionOutput, -3}});
            trim(-5);
            break;
        case 9: // Gloss Overdrive: bright hyperpop double with parallel digital grit.
            stack(36, 9, 82);
            ctx.voice(1, -7, -.66f, 10, -13, 0, 2.4f, .12f, 1.7f);
            ctx.voice(2, -7, .68f, 18, 15, 0, 2.9f, .19f, 2.1f);
            creative(N::bitcrusher, 28, {{C::crusherBits, 7}, {C::crusherRate, 11700},
                {C::crusherLowpass, 5500}, {C::crusherDrive, 2}});
            trim(-5);
            break;

        // Wild: wet-dominant inserts and larger pitch/formant moves are intentional.
        case 10: // Backspun Confetti: bright, scattered reverse grains from live capture.
            stack(0, 0, 65);
            creative(N::granular, 79, {{C::granularPitch, 7}, {C::granularSize, 72},
                {C::granularDensity, 43}, {C::granularSpread, 94}, {C::granularPosition, 225},
                {C::granularReverse, 1}, {C::granularJitter, 64}, {C::granularLowCut, 320},
                {C::granularHighCut, 7100}});
            creative(N::delay, 28, {{C::delayTime, 127}, {C::delayFeedback, 18}, {C::delayPingPong, 76},
                {C::delayDrive, 0}, {C::delayDuck, 0}});
            trim(-7);
            break;
        case 11: // Octave Dominoes: climbing ping-pong echoes on a straight eighth grid.
            stack(0, 0, 65);
            creative(N::delay, 69, {{C::delaySync, 1}, {C::delayBeats, .5f}, {C::delayFeedback, 46},
                {C::delayPingPong, 95}, {C::delayPitch, 12}, {C::delayLowCut, 420},
                {C::delayHighCut, 5000}, {C::delayDrive, 0}, {C::delayDuck, 2}});
            trim(-7);
            break;
        case 12: // Subterranean Twin: deep octave/formant divergence with a pinched top end.
            stack(81, 9, 62);
            ctx.voice(1, -3, 0, 2, 0, -12, -7, .14f, .46f);
            ctx.voice(2, -9, -.54f, 31, -17, -12, 5, .33f, 1.2f);
            ctx.voice(3, -12, .61f, 47, 8, -24, -3, .22f, .67f);
            utility(N::xpFilter, 100, {{U::xpMode, 5}, {U::xpCutoff, 2500}, {U::xpResonance, 17}});
            creative(N::saturator, 72, {{C::saturatorDrive, 21}, {C::saturatorCharacter, 72},
                {C::saturatorTone, 1}, {C::saturatorOutput, -5}, {C::saturatorLevelMatch, 1}});
            trim(-7);
            break;
        case 13: // Pixel Megaphone: narrow-band shout into a low-rate, four-bit radio.
            stack(0, 0, 65);
            utility(N::xpFilter, 100, {{U::xpMode, 2}, {U::xpCutoff, 1480},
                {U::xpResonance, 27}, {U::xpDrive, 4}});
            creative(N::bitcrusher, 88, {{C::crusherBits, 4}, {C::crusherRate, 4200},
                {C::crusherLowpass, 2850}, {C::crusherDrive, 8}, {C::crusherDither, 0}});
            utility(N::stereo, 100, {{U::stereoWidth, 0}});
            trim(-10);
            break;
        case 14: // Vowel Reactor: electronic vowel filter on a high, detuned duet.
            stack(70, 16, 91);
            ctx.voice(1, -5, -.75f, 12, -23, 12, 6, .41f, 1.62f);
            ctx.voice(2, -5, .73f, 27, 21, 12, 3, .44f, 2.17f);
            utility(N::synthFilter, 84, {{U::synthMode, 6}, {U::synthCutoff, 1250},
                {U::synthResonance, 62}, {U::synthDrive, 3}, {U::synthFeedback, -63}});
            creative(N::delay, 28, {{C::delaySync, 1}, {C::delayBeats, .25f}, {C::delayFeedback, 29},
                {C::delayPingPong, 76}, {C::delayDrive, 0}, {C::delayDuck, 0}});
            trim(-8);
            break;
        case 15: // Trapdoor Room: a large verb burst chopped by a post-reverb gate.
            stack(0, 0, 65);
            creative(N::reverb, 45, {{C::reverbPredelay, 8}, {C::reverbSize, 81}, {C::reverbDecay, 1.45f},
                {C::reverbDiffusion, 91}, {C::reverbEarly, 56}, {C::reverbWidth, 125},
                {C::reverbLowCut, 280}, {C::reverbHighCut, 5500}, {C::reverbDuck, 0}});
            utility(N::gate, 100, {{U::gateThreshold, -32}, {U::gateRange, 62}, {U::gateRatio, 14},
                {U::gateAttack, .35f}, {U::gateHold, 92}, {U::gateRelease, 36}});
            trim(-9);
            break;
        case 16: // Comet Choir: an octave halo dissolving into a long upward shimmer.
            stack(54, 13, 90);
            ctx.voice(1, -6, -.71f, 24, -7, 12, 2, .23f, .59f);
            ctx.voice(2, -7, .72f, 43, 9, 12, -2, .27f, .84f);
            creative(N::reverb, 48, {{C::reverbPredelay, 58}, {C::reverbSize, 88}, {C::reverbDecay, 6.3f},
                {C::reverbEarly, 12}, {C::reverbDiffusion, 93}, {C::reverbModulation, .87f},
                {C::reverbLowCut, 390}, {C::reverbHighCut, 8300}, {C::reverbDamping, 5500},
                {C::reverbPitch, 12}, {C::reverbPitchAmount, 64}, {C::reverbSwell, 290}, {C::reverbDuck, 0}});
            trim(-9);
            break;
        case 17: // Wire Resonator: a fixed comb rings like a plucked metal wire on vowels.
            stack(0, 0, 65);
            utility(N::xpFilter, 100, {{U::xpMode, 6}, {U::xpCutoff, 185}});
            utility(N::synthFilter, 87, {{U::synthMode, 5}, {U::synthCutoff, 247},
                {U::synthResonance, 36}, {U::synthDrive, 0}, {U::synthFeedback, 58}});
            creative(N::convolution, 29, {{C::convolutionSpace, 2}, {C::convolutionPredelay, 6},
                {C::convolutionTail, 55}, {C::convolutionBrightness, -3}, {C::convolutionOutput, -4}});
            trim(-9);
            break;
        case 18: { // Split Personality: high and low alter egos, pulled hard to opposite sides.
            stack(88, 25, 100);
            ctx.voice(1, -4, -.97f, 10, -19, 12, 8, .56f, 1.31f);
            ctx.voice(2, -4, .97f, 39, 16, -12, -6, .47f, .93f);
            const auto prefix = ctx.effect(N::chorale, 43);
            ctx.raw(prefix + "Motion", 78);
            utility(N::stereo, 100, {{U::stereoWidth, 126}});
            trim(-8);
            break;
        }
        case 19: // Slow Orbit: long, downward grains sent into a shadowy church.
            stack(0, 0, 65);
            creative(N::granular, 84, {{C::granularPitch, -12}, {C::granularSize, 214},
                {C::granularDensity, 17}, {C::granularSpread, 76}, {C::granularPosition, 540},
                {C::granularJitter, 47}, {C::granularLowCut, 190}, {C::granularHighCut, 3900}});
            creative(N::convolution, 52, {{C::convolutionSpace, 0}, {C::convolutionBrightness, -6},
                {C::convolutionPredelay, 39}, {C::convolutionTail, 91}, {C::convolutionOutput, -5}});
            trim(-8);
            break;
    }
}

} // namespace vocalstack::expandedFactoryPresets
