#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vocalstack {

class VocalGlitch {
public:
    struct Settings {
        float lengthMs = 150, thresholdDb = -36;
        int pattern = 0, grid = 0;
        bool layer = false, automatic = false;
    };

    // After prepare, drive all methods from the audio thread. Call grab/fire
    // before process at the desired offset; a grab excludes that input sample.
    void prepare(double sampleRate) {
        rate = std::isfinite(sampleRate) && sampleRate > 0.0
            ? std::clamp(sampleRate, 1.0, 768000.0) : 48000.0;
        fadeSamples = samplesForMs(3.0);
        onsetSamples = samplesForMs(12.0);
        tailDebounce = samplesForMs(75.0);
        attack = static_cast<float>(-std::expm1(-1000.0 / (2.0 * rate)));
        release = static_cast<float>(-std::expm1(-1000.0 / (8.0 * rate)));
        const auto maxSlice = samplesForMs(maxLengthMs);
        // Leave room behind the detector's release and silence debounce.
        history.resize(maxSlice + samplesForMs(200.0));
        for (auto& slice : slices)
            slice.samples.resize(maxSlice);
        reset();
    }

    void reset() noexcept {
        writePosition = historyCount = 0;
        pinned = -1;
        pending = {};
        playback = {};
        lastMix = retriggerMix = {};
        retriggerLeft = 0;
        transportPlaying = clockRunning = false;
        blockPpq = ppqPerSample = blockOffset = 0.0;
        tempo = 120.0;
        resetDetector();
    }

    void setSettings(Settings next) noexcept {
        next.lengthMs = std::isfinite(next.lengthMs)
            ? std::clamp(next.lengthMs, 1.0f, maxLengthMs) : 150.0f;
        next.thresholdDb = std::isfinite(next.thresholdDb)
            ? std::clamp(next.thresholdDb, -90.0f, 0.0f) : -36.0f;
        next.pattern = std::clamp(next.pattern, 0, 3);
        next.grid = std::clamp(next.grid, 0, 3);
        if (next.thresholdDb != settings.thresholdDb) {
            openLevel = std::pow(10.0f, next.thresholdDb * 0.05f);
            closeLevel = openLevel * 0.5011872f; // 6 dB of hysteresis.
        }
        if (next.automatic != settings.automatic) {
            resetDetector();
            if (pending.automatic)
                pending = {};
        }
        settings = next;
    }

    void grab() noexcept {
        if (historyCount != 0)
            pinned = capture(0, pinned);
    }

    void fire() noexcept {
        if (pinned >= 0 && canSchedule())
            schedule(pinned, false);
    }

    void setClock(double ppq, double bpm, bool playing, bool discontinuity) noexcept {
        const bool validTempo = std::isfinite(bpm) && bpm >= 20.0 && bpm <= 10000.0;
        const bool running = playing && std::isfinite(ppq) && validTempo;
        if (discontinuity || (transportPlaying && !playing) || (clockRunning && !running)) {
            pending = {};
            resetDetector();
        }
        transportPlaying = playing;
        clockRunning = running;
        tempo = validTempo ? bpm : 120.0;
        blockPpq = running ? ppq : 0.0;
        ppqPerSample = running ? tempo / (60.0 * rate) : 0.0;
        blockOffset = 0.0;
    }

    void process(float inL, float inR, float& outL, float& outR) noexcept {
        if (!history.empty()) {
            history[writePosition] = {inL, inR};
            if (++writePosition == history.size())
                writePosition = 0;
            if (historyCount < history.size())
                ++historyCount;
            if (settings.automatic)
                detectTail(std::max(std::abs(inL), std::abs(inR)));
        }

        if (pending.slot >= 0 && (pending.immediate ||
                (clockRunning && currentPpq() + ppqTolerance >= pending.ppq))) {
            begin(pending);
            pending = {};
        }

        if (playback.slot >= 0 || retriggerLeft != 0) {
            auto mix = render();
            if (retriggerLeft != 0) {
                const auto old = static_cast<float>(retriggerLeft--) / static_cast<float>(fadeSamples);
                mix.dry += old * (retriggerMix.dry - mix.dry);
                mix.wet.l += old * (retriggerMix.wet.l - mix.wet.l);
                mix.wet.r += old * (retriggerMix.wet.r - mix.wet.r);
            }
            lastMix = mix;
            outL = mix.dry * inL + mix.wet.l;
            outR = mix.dry * inR + mix.wet.r;
        } else {
            lastMix = {};
            outL = inL;
            outR = inR;
        }
        blockOffset += 1.0;
    }

private:
    static constexpr float maxLengthMs = 1000.0f;
    static constexpr double ppqTolerance = 1.0e-9;
    static constexpr std::array<double, 4> gridBeats {{0.0, 0.25, 1.0 / 3.0, 1.0}};

    struct Frame { float l = 0.0f, r = 0.0f; };
    struct Slice { std::vector<Frame> samples; std::size_t length = 0; };
    struct Launch {
        int slot = -1, pattern = 0;
        bool layer = false, automatic = false, immediate = false;
        double ppq = 0.0;
    };
    struct Hit {
        std::size_t start = 0, length = 0;
        bool reverse = false, halfSpeed = false;
    };
    struct Playback {
        int slot = -1;
        bool layer = false;
        std::size_t position = 0, length = 0, replaceStart = 0;
        int hit = 0, hitCount = 0;
        std::array<Hit, 4> hits {};
    };
    struct Mix { Frame wet {}; float dry = 1.0f; };

    std::size_t samplesForMs(double ms) const noexcept {
        return std::max(std::size_t{1}, static_cast<std::size_t>(std::round(ms * rate / 1000.0)));
    }

    int capture(std::size_t skip, int replace) noexcept {
        // A new grab can reuse its old slot unless a launch or playback still
        // owns it. Four slots cover the pinned, scheduled, playing and new slices.
        int slot = 0;
        for (; slot < static_cast<int>(slices.size()); ++slot) {
            if (slot != playback.slot && slot != pending.slot && (slot != pinned || slot == replace))
                break;
        }
        auto& slice = slices[static_cast<std::size_t>(slot)];
        const auto capacity = history.size();
        slice.length = std::min({samplesForMs(settings.lengthMs), historyCount - skip, slice.samples.size()});
        const auto start = (writePosition + capacity - (skip + slice.length)) % capacity;
        const auto first = std::min(slice.length, capacity - start);
        std::copy_n(history.data() + start, first, slice.samples.data());
        std::copy_n(history.data(), slice.length - first, slice.samples.data() + first);
        return slot;
    }

    double currentPpq() const noexcept { return blockPpq + blockOffset * ppqPerSample; }
    // A dead clock must not turn a quantized request into an off-grid launch.
    bool canSchedule() const noexcept { return settings.grid == 0 || clockRunning; }

    void schedule(int slot, bool automatic) noexcept {
        pending = {slot, settings.pattern, settings.layer, automatic, settings.grid == 0, 0.0};
        if (!pending.immediate) {
            const auto step = gridBeats[static_cast<std::size_t>(settings.grid)];
            // An event already on the grid uses that boundary, including on a block edge.
            pending.ppq = std::ceil((currentPpq() - ppqTolerance) / step) * step;
        }
    }

    void resetDetector() noexcept {
        envelope = 0.0f;
        sinceVoice = aboveCount = belowCount = 0;
        phrase = false;
    }

    void detectTail(float peak) noexcept {
        envelope += (peak - envelope) * (peak > envelope ? attack : release);
        if (envelope < 1.0e-12f)
            envelope = 0.0f;
        if (peak >= closeLevel && envelope >= closeLevel)
            sinceVoice = 0;
        else if (sinceVoice < history.size())
            ++sinceVoice;

        // Check the raw onset too: voice can return on the very grid sample
        // where the launch is due, before the smoothed detector opens again.
        if (pending.automatic && (peak >= openLevel || envelope >= openLevel))
            pending = {};

        if (!phrase) {
            if (envelope >= openLevel) {
                if (++aboveCount >= onsetSamples) {
                    phrase = true;
                    aboveCount = belowCount = 0;
                }
            } else {
                aboveCount = 0;
            }
            return;
        }

        if (envelope >= closeLevel) {
            belowCount = 0;
        } else if (++belowCount >= tailDebounce) {
            phrase = false;
            belowCount = 0;
            // The envelope and debounce confirm the end, but neither belongs
            // in the captured audio. A queued manual launch takes priority.
            if (canSchedule() && sinceVoice < historyCount && (pending.slot < 0 || pending.automatic)) {
                pending = {};
                schedule(capture(sinceVoice, -1), true);
            }
        }
    }

    void begin(const Launch& launch) noexcept {
        if (playback.slot >= 0 || retriggerLeft != 0) {
            retriggerMix = lastMix;
            retriggerLeft = fadeSamples;
        }
        playback = {};
        playback.slot = launch.slot;
        playback.layer = launch.layer;
        const auto sourceLength = slices[static_cast<std::size_t>(launch.slot)].length;
        const auto samplesPerBeat = 60.0 * rate / tempo;
        const auto boundary = [samplesPerBeat](double beats) {
            return static_cast<std::size_t>(std::round(beats * samplesPerBeat));
        };
        const auto addHit = [&](std::size_t start, std::size_t end, bool reverse, bool halfSpeed) {
            const auto count = std::min(end - start, sourceLength * (halfSpeed ? 2 : 1));
            if (count != 0)
                playback.hits[static_cast<std::size_t>(playback.hitCount++)] = {start, count, reverse, halfSpeed};
        };

        playback.length = std::max(std::size_t{1}, boundary(1.0));
        if (launch.pattern == 2) {
            // End-align the reverse pickup instead of following a short slice with silence.
            playback.length = std::max(std::size_t{1}, boundary(0.5));
            playback.replaceStart = playback.length - std::min(playback.length, sourceLength);
            addHit(playback.replaceStart, playback.length, true, false);
        } else if (launch.pattern == 3) {
            addHit(0, std::max(std::size_t{1}, boundary(0.5)), false, true);
        } else {
            const int count = launch.pattern == 0 ? 3 : 4;
            for (int i = 0; i < count; ++i) {
                addHit(boundary(static_cast<double>(i) / count),
                       i + 1 == count ? playback.length : boundary(static_cast<double>(i + 1) / count),
                       launch.pattern == 0 && i == count - 1, false);
            }
        }
    }

    float edgeGain(std::size_t position, std::size_t length) const noexcept {
        if (length <= 2)
            return 1.0f;
        const auto ramp = std::min(fadeSamples, (length - 1) / 2);
        const auto edge = std::min({position, length - 1 - position, ramp});
        return static_cast<float>(edge) / static_cast<float>(ramp);
    }

    Mix render() noexcept {
        Mix mix;
        if (playback.slot < 0)
            return mix;
        const auto position = playback.position++;
        if (!playback.layer && position >= playback.replaceStart) {
            mix.dry -= edgeGain(position - playback.replaceStart, playback.length - playback.replaceStart);
        }
        if (playback.hit < playback.hitCount) {
            const auto& hit = playback.hits[static_cast<std::size_t>(playback.hit)];
            if (position >= hit.start) {
                const auto offset = position - hit.start;
                const auto& slice = slices[static_cast<std::size_t>(playback.slot)];
                const auto index = hit.reverse ? slice.length - 1 - offset : (hit.halfSpeed ? offset / 2 : offset);
                auto sample = slice.samples[index];
                if (hit.halfSpeed && (offset & 1u) != 0 && index + 1 < slice.length) {
                    sample.l = 0.5f * (sample.l + slice.samples[index + 1].l);
                    sample.r = 0.5f * (sample.r + slice.samples[index + 1].r);
                }
                const auto gain = edgeGain(offset, hit.length);
                mix.wet = {gain * sample.l, gain * sample.r};
                if (offset + 1 == hit.length)
                    ++playback.hit;
            }
        }
        if (playback.position == playback.length)
            playback.slot = -1;
        return mix;
    }

    Settings settings;
    double rate = 48000.0, tempo = 120.0;
    double blockPpq = 0.0, ppqPerSample = 0.0, blockOffset = 0.0;
    bool transportPlaying = false, clockRunning = false;
    std::vector<Frame> history;
    std::array<Slice, 4> slices;
    std::size_t writePosition = 0, historyCount = 0;
    int pinned = -1;
    Launch pending;
    Playback playback;
    Mix lastMix, retriggerMix;
    std::size_t fadeSamples = 1, retriggerLeft = 0;
    float attack = 1.0f, release = 1.0f, envelope = 0.0f;
    float openLevel = 0.015848932f, closeLevel = 0.007943282f;
    std::size_t onsetSamples = 1, tailDebounce = 1;
    std::size_t sinceVoice = 0, aboveCount = 0, belowCount = 0;
    bool phrase = false;
};

} // namespace vocalstack
