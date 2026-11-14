#pragma once
#include "UtilityEffectPanel.h"
#include <limits>
#include <stdexcept>

namespace vocalstack {
// Ready for coordinator registration; deliberately no static test registration.
struct UtilityVisualUpdateTests {
    static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
    static void telemetry() {
        UtilityHistoryAccumulator accumulator;
        for (double rate : {8000., 44100., 48000., 96000., 192000., 384000.}) {
            accumulator.reset(rate);
            const size_t interval = size_t(std::round(rate * .020));
            for (size_t bin = 0; bin < 205; ++bin)
                for (size_t i = 0; i < interval; ++i)
                    accumulator.push(.5f, .5f, .25f, .25f, {-40.f + float(bin) * .01f, float(bin) * .1f});
            const auto snapshot = accumulator.snapshot();
            require(snapshot.used == 200 && snapshot.samples == interval * 205, "History is not bounded to 200 measured bins");
            require(std::abs(snapshot.binSeconds * 200 - 4) < 1.e-9, "History is not four seconds");
            require(std::abs(snapshot.bins.front().gainReductionDb - .5f) < 1.e-4f, "Oldest retained GR misplaced");
            require(std::abs(snapshot.bins.back().gainReductionDb - 20.4f) < 1.e-4f, "Newest GR not on right");
            require(std::abs(snapshot.bins.back().inputDb + 6.0206f) < .001f, "Input RMS dBFS wrong");
            require(std::abs(snapshot.bins.back().outputDb + 12.0412f) < .001f, "Final output RMS dBFS wrong");
        }
        accumulator.reset(48000);
        for (int i = 0; i < 960; ++i) accumulator.push(0, 0, 0, 0, {-50, 0});
        auto silent = accumulator.snapshot();
        require(silent.used == 1 && silent.bins.back().inputDb <= -72 && silent.bins.back().gainReductionDb == 0, "Silence fabricated activity");
        accumulator.reset(48000); require(accumulator.snapshot().used == 0, "Reset retained history");
        UtilityHistoryAccumulator other;
        other.reset(48000);
        for (int i = 0; i < 959; ++i) other.push(1, 1, 1, 1, {-20, 8});
        require(other.snapshot().used == 0, "Partial bin published as a complete measurement");
        other.push(1, 1, 1, 1, {-20, 8});
        require(other.snapshot().bins.back().thresholdDb == -20 && accumulator.snapshot().used == 0, "Histories crossed instances or threshold was not measured");
        struct WithHistory { UtilityHistorySnapshot utilityHistory; } with;
        struct WithoutHistory {} without;
        require(utility_visual_detail::history(with) == &with.utilityHistory, "Optional frame payload was not detected");
        require(utility_visual_detail::history(without) == nullptr, "Missing payload invented history");
        std::array<std::array<float, 8>, 2> channels{};
        require(!UtilityStereoActivity::measure(channels).correlationValid, "Silence reported mono correlation");
        channels[0].fill(.25f); channels[1].fill(.25f);
        require(UtilityStereoActivity::measure(channels).correlation > .999f, "Mono correlation wrong");
        channels[1].fill(-.25f);
        require(UtilityStereoActivity::measure(channels).correlation < -.999f, "Anti-phase correlation wrong");
        channels[1].fill(0);
        require(!UtilityStereoActivity::measure(channels).correlationValid, "One silent channel reported correlation");
        channels[0][0] = std::numeric_limits<float>::quiet_NaN();
        require(std::isfinite(UtilityStereoActivity::measure(channels).rms[0]), "Nonfinite telemetry escaped");
    }
    static void dynamicsAndMono() {
        UtilityEffect gate, stereo;
        UtilitySettings settings;
        gate.prepare(48000, UtilityKind::gate);
        settings[UtilityParameter::gateHold] = 0;
        for (int i = 0; i < 48000; ++i) gate.process(.0001f, .0001f, settings);
        require(gate.dynamicsSnapshot().gainReductionDb > 30, "Snapshot is not actual gate attenuation");
        require(gate.dynamicsSnapshot().thresholdDb == -40, "Snapshot threshold is not DSP threshold");
        gate.reset(); require(gate.dynamicsSnapshot().gainReductionDb == 0, "Gate reset retained GR");
        stereo.prepare(48000, UtilityKind::stereo);
        std::array<float, 2> mono{};
        for (int i = 0; i < 24000; ++i) mono = stereo.applyMonoAudition({.75f, -.25f}, true);
        require(std::abs(mono[0] - .25f) < 1.e-6f && std::abs(mono[1] - .25f) < 1.e-6f, "Audition is not unity-mid fold");
        for (int i = 0; i < 24000; ++i) mono = stereo.applyMonoAudition({.75f, -.25f}, false);
        require(std::abs(mono[0] - .75f) < 1.e-6f && std::abs(mono[1] + .25f) < 1.e-6f, "Audition did not restore stereo");
        require(settings[UtilityParameter::stereoWidth] == 100, "Audition changed stored width");
        require(utilityParameters[size_t(UtilityParameter::gainDb)].max == 24, "Utility Gain range changed");
    }
    // Message-thread fixture; no desktop/capture claim.
    static void controls(Processor& owner) {
        const auto id = fxParameterId(1, "Mix");
        auto* parameter = owner.parameters.getParameter(id);
        require(parameter != nullptr, "Missing Mix parameter");
        const float saved = parameter->getValue();
        struct Restore { juce::RangedAudioParameter& parameter; float value; ~Restore() { parameter.setValueNotifyingHost(value); } } restore{*parameter, saved};
        parameter->setValueNotifyingHost(parameter->convertTo0to1(100));
        UtilityEffectPanel gain(owner, 1, UtilityKind::gain);
        gain.setSize(400, 730); gain.timerCallback();
        require(!gain.mixRow->isVisible(), "Default Gain Mix is not hidden");
        gain.mixRow->slider.setValue(37, juce::sendNotificationSync); gain.timerCallback();
        require(gain.mixRow->isVisible(), "Legacy Gain Wet was not revealed");
        require(std::abs(parameter->convertFrom0to1(parameter->getValue()) - 37) < .01f, "Legacy blend silently reset");
        for (bool neon : {false, true}) {
            gain.setNeon(neon);
            for (int width : {220, 400, 620, 860}) {
                gain.setSize(width, 730);
                require(gain.content.getLocalBounds().contains(gain.mixRow->getBounds()), "Legacy Wet escaped content");
            }
        }
        UtilityEffectPanel stereo(owner, 1, UtilityKind::stereo);
        require(!stereo.monoAudition.isEnabled(), "Unbound audition changed ephemeral UI state");
        require(!stereo.bindMonoAuditionParameter("missing_mono_parameter"), "Missing audition binding accepted");
        require(!stereo.monoAudition.getWantsKeyboardFocus() && !stereo.monoAudition.getMouseClickGrabsKeyboardFocus(), "Audition claims keyboard focus");
    }
};
} // namespace vocalstack
