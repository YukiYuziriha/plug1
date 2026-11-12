#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace vocalstack {

enum class CreativeKind { delay, reverb, compressor, bitcrusher, granular };

// Append, never reorder, when these indices/IDs become part of host state.
enum class CreativeParameter : std::size_t {
    delayTime, delaySync, delayBeats, delayFeedback, delayPingPong,
    delayLowCut, delayHighCut, delayDrive, delayDuck, delayDuckRelease, delayPitch,
    reverbPredelay, reverbSize, reverbDecay, reverbDiffusion, reverbEarly,
    reverbLowCut, reverbHighCut, reverbDamping, reverbWidth, reverbModulation,
    reverbDuck, reverbDuckRelease, reverbSaturation, reverbFreeze,
    compressorThreshold, compressorRatio, compressorKnee, compressorAttack,
    compressorRelease, compressorMakeup, compressorSidechainHP,
    crusherBits, crusherRate, crusherLowpass, crusherDrive, crusherDither,
    granularPitch, granularSize, granularDensity, granularSpread, granularPosition,
    granularFreeze, granularReverse, granularLowCut, granularHighCut, granularJitter,
    granularDrive, reverbPitch, reverbPitchAmount, reverbSwell, count
};
inline constexpr std::size_t creativeParameterCount = static_cast<std::size_t>(CreativeParameter::count);
struct CreativeParameterDescriptor {
    const char* id;
    const char* label;
    const char* unit;
    float min, max, initial;
    CreativeKind kind;
    bool discrete = false;
};
inline constexpr std::array<CreativeParameterDescriptor, creativeParameterCount> creativeParameters {{
    {"delay_time", "Time", "ms", 1, 2000, 330, CreativeKind::delay},
    {"delay_sync", "Tempo sync", "", 0, 1, 0, CreativeKind::delay, true},
    {"delay_beats", "Quarter-note beats", "beats", .0625f, 8, .75f, CreativeKind::delay},
    {"delay_feedback", "Feedback", "%", 0, 95, 38, CreativeKind::delay},
    {"delay_pingpong", "Ping pong", "%", 0, 100, 60, CreativeKind::delay},
    {"delay_lowcut", "Low cut", "Hz", 20, 2000, 160, CreativeKind::delay},
    {"delay_highcut", "High cut", "Hz", 300, 20000, 5500, CreativeKind::delay},
    {"delay_drive", "Feedback drive", "dB", 0, 24, 3, CreativeKind::delay},
    {"delay_duck", "Ducking", "dB", 0, 30, 6, CreativeKind::delay},
    {"delay_duck_release", "Duck release", "ms", 20, 2000, 280, CreativeKind::delay},
    {"delay_pitch", "Repeat pitch", "st", -12, 12, 0, CreativeKind::delay},
    {"reverb_predelay", "Predelay", "ms", 0, 250, 28, CreativeKind::reverb},
    {"reverb_size", "Room size", "%", 0, 100, 55, CreativeKind::reverb},
    {"reverb_decay", "Midband RT60", "s", .15f, 20, 2.8f, CreativeKind::reverb},
    {"reverb_diffusion", "Diffusion", "%", 0, 100, 80, CreativeKind::reverb},
    {"reverb_early", "Early reflections", "%", 0, 100, 25, CreativeKind::reverb},
    {"reverb_lowcut", "Low cut", "Hz", 20, 2000, 180, CreativeKind::reverb},
    {"reverb_highcut", "High cut", "Hz", 300, 20000, 7500, CreativeKind::reverb},
    {"reverb_damping", "HF damping", "Hz", 300, 20000, 4200, CreativeKind::reverb},
    {"reverb_width", "Stereo width", "%", 0, 150, 100, CreativeKind::reverb},
    {"reverb_modulation", "Tail modulation", "ms", 0, 2, .35f, CreativeKind::reverb},
    {"reverb_duck", "Ducking", "dB", 0, 30, 6, CreativeKind::reverb},
    {"reverb_duck_release", "Duck release", "ms", 20, 2000, 400, CreativeKind::reverb},
    {"reverb_saturation", "Tail saturation", "dB", 0, 18, 0, CreativeKind::reverb},
    {"reverb_freeze", "Freeze", "", 0, 1, 0, CreativeKind::reverb, true},
    {"compressor_threshold", "Threshold", "dB", -60, 0, -20, CreativeKind::compressor},
    {"compressor_ratio", "Ratio", ":1", 1, 20, 4, CreativeKind::compressor},
    {"compressor_knee", "Knee", "dB", 0, 24, 6, CreativeKind::compressor},
    {"compressor_attack", "Attack", "ms", .1f, 100, 8, CreativeKind::compressor},
    {"compressor_release", "Release", "ms", 10, 2000, 120, CreativeKind::compressor},
    {"compressor_makeup", "Makeup", "dB", -12, 24, 0, CreativeKind::compressor},
    {"compressor_sidechain_hp", "Detector high pass", "Hz", 20, 1000, 100, CreativeKind::compressor},
    {"crusher_bits", "Bit depth", "bits", 3, 24, 10, CreativeKind::bitcrusher},
    {"crusher_rate", "Sample rate", "Hz", 1000, 96000, 16000, CreativeKind::bitcrusher},
    {"crusher_lowpass", "Output low pass", "Hz", 300, 20000, 6500, CreativeKind::bitcrusher},
    {"crusher_drive", "Input drive", "dB", 0, 30, 6, CreativeKind::bitcrusher},
    {"crusher_dither", "TPDF dither", "%", 0, 100, 0, CreativeKind::bitcrusher},
    {"granular_pitch", "Pitch", "st", -24, 12, -7, CreativeKind::granular},
    {"granular_size", "Grain length", "ms", 20, 250, 100, CreativeKind::granular},
    {"granular_density", "Grain density", "Hz", 4, 80, 30, CreativeKind::granular},
    {"granular_spread", "Stereo spread", "%", 0, 100, 45, CreativeKind::granular},
    {"granular_position", "Lookback", "ms", 0, 1500, 90, CreativeKind::granular},
    {"granular_freeze", "Freeze", "", 0, 1, 0, CreativeKind::granular, true},
    {"granular_reverse", "Reverse", "", 0, 1, 0, CreativeKind::granular, true},
    {"granular_lowcut", "Low cut", "Hz", 20, 2000, 160, CreativeKind::granular},
    {"granular_highcut", "High cut", "Hz", 300, 20000, 6000, CreativeKind::granular},
    {"granular_jitter", "Timing scatter", "%", 0, 100, 12, CreativeKind::granular},
    {"granular_drive", "Capture drive", "dB", 0, 24, 0, CreativeKind::granular},
    {"reverb_pitch", "Feedback pitch", "st", -12, 12, 12, CreativeKind::reverb},
    {"reverb_pitch_amount", "Pitched feedback", "%", 0, 100, 0, CreativeKind::reverb},
    {"reverb_swell", "Wet attack", "ms", 0, 2000, 0, CreativeKind::reverb}
}};
struct CreativeSettings {
    std::array<float, creativeParameterCount> values{};
    CreativeSettings() noexcept {
        for (std::size_t i = 0; i < values.size(); ++i) values[i] = creativeParameters[i].initial;
    }
    float& operator[](CreativeParameter p) noexcept { return values[static_cast<std::size_t>(p)]; }
    float operator[](CreativeParameter p) const noexcept { return values[static_cast<std::size_t>(p)]; }
};

namespace creative_detail {
inline constexpr float pi = 3.14159265358979323846f;
inline float clean(float x) noexcept {
    return !std::isfinite(x) || std::abs(x) < 1.e-20f ? 0.f : std::clamp(x, -64.f, 64.f);
}
inline float dbGain(float db) noexcept { return std::pow(10.f, db * .05f); }
// Passive nonlinearity in feedback: never increases energy, even while automated.
inline float saturate(float x, float amount) noexcept { return x / (1.f + amount * std::abs(x)); }
struct Ring {
    std::vector<float> data;
    std::size_t head = 0;
    void allocate(std::size_t n) { data.assign(n, 0.f); head = 0; }
    void release() { std::vector<float>().swap(data); head = 0; }
    void reset() noexcept { std::fill(data.begin(), data.end(), 0.f); head = 0; }
    void push(float x) noexcept { if (data.empty()) return; data[head] = clean(x); if (++head == data.size()) head = 0; }
    float at(double position) const noexcept {
        const double n = static_cast<double>(data.size());
        if (data.empty() || !std::isfinite(position)) return 0;
        // Normal DSP reads are within two revolutions. The fallback also bounds
        // work for absolute granular capture coordinates after long sessions.
        if (position < -n || position >= 2 * n) position = std::fmod(position, n);
        if (position < 0) position += n;
        if (position >= n) position -= n;
        const auto i = static_cast<std::size_t>(position);
        const auto j = i + 1 == data.size() ? 0 : i + 1;
        return data[i] + static_cast<float>(position - static_cast<double>(i)) * (data[j] - data[i]);
    }
    float read(double delay) const noexcept {
        if (data.size() < 3 || !std::isfinite(delay)) return 0;
        return at(static_cast<double>(head) - std::clamp(delay, 1.0, static_cast<double>(data.size() - 2)));
    }
};
struct Band {
    float low = 0, high = 0, lp = 0, hp = 0;
    void reset() noexcept { low = high = 0; }
    float process(float x) noexcept {
        high = clean(high + hp * (x - high));
        low = clean(low + lp * (x - high - low));
        return low;
    }
};
struct Grain {
    double position = 0, lastPosition = 0, step = 1;
    float phase = 0, increment = 0, pan = 0, boundaryGain = 1;
    std::array<float, 2> last{};
    bool active = false, hasRead = false, retiring = false;
};
} // namespace creative_detail

// Single audio-thread owner. prepare() is non-realtime and resets state; reset()
// is allocation-free but clears potentially large buffers. Settings must be an
// audio-thread snapshot, not concurrently mutated host storage. No lookahead or
// reported host latency; wet time effects have their intentional acoustic delay.
// Rates 8--384 kHz are supported; invalid rates fall back to 48 kHz. Tempo is
// quarter-note BPM, 20--400 (otherwise 120). Delay storage covers all 8 beats at
// 20 BPM (24 seconds). Continuous controls use 25 ms smoothing; expensive
// coefficients update every 32 samples, compressor gain targets every 8 samples.
// The granular cloud has 32 fixed grain slots; new launches are skipped if full.
// Granular pitch is not formant-preserving; zero-scatter periodic grain streams
// can interfere on sustained tones. Some scatter is useful on vocal material.
// Reverb RT60 is a midband target: damping, filters and interpolation shorten it.
// Reverb pitch is a dual-head, 60 ms granular feedback transposer (not formant-
// preserving or alias-free). Amount replaces feedback with a .45-gain pitched
// branch; zero amount / zero semitones bypass it exactly. Freeze fades it out.
// Wet attack is a stereo-linked envelope swell, not added predelay; 0 bypasses.
// Freeze is bounded/near-lossless, not an infinite or bit-exact loop. Crusher is
// intentionally aliased, with a postfilter, not an antialiased sample-rate converter.
class CreativeEffect {
public:
    struct GrainSnapshot {
        struct Grain {
            float position = 0, progress = 0, envelope = 0, pan = 0, step = 0;
            std::uint32_t slot = 0;
            bool holdingBoundary = false;
        };
        std::array<Grain, 32> grains{};
        std::uint64_t sample = 0, capturedFrames = 0;
        std::uint32_t count = 0;
        float captureHead = 0, validFraction = 0;
        bool frozen = false, capturing = false;
    };
    // AUDIO THREAD ONLY, e.g. once at the end of a host block. Fixed value data,
    // no allocations/locks. The coordinator must publish through its own bounded
    // lock-free bridge; calling this concurrently from the UI is a data race.
    // Positions/progress describe the next render sample, not decorative grains.
    GrainSnapshot grainSnapshot() const noexcept {
        GrainSnapshot result;
        if (!ready_ || kind_ != CreativeKind::granular) return result;
        const double n = static_cast<double>(rings_[0].data.size());
        result.sample = tick_; result.capturedFrames = capturedFrames_;
        result.captureHead = static_cast<float>(rings_[0].head / n);
        result.validFraction = static_cast<float>(std::min<double>(static_cast<double>(capturedFrames_), n) / n);
        result.frozen = granularFrozen_; result.capturing = !granularFrozen_;
        for (std::size_t i = 0; i < grains_.size(); ++i) {
            const auto& g = grains_[i]; if (!g.active) continue;
            double p = std::fmod(g.retiring ? g.lastPosition : g.position, n); if (p < 0) p += n;
            const float t = g.phase * (1 - g.phase);
            result.grains[result.count++] = {static_cast<float>(p / n), g.phase,
                g.hasRead ? 16 * t * t * g.boundaryGain : 0.f, g.pan, static_cast<float>(g.step), static_cast<std::uint32_t>(i), g.retiring};
        }
        return result;
    }
    static constexpr float reverbWetCalibration = 4.f;
    void prepare(double sampleRate, CreativeKind kind) {
        sr_ = std::isfinite(sampleRate) && sampleRate >= 8000 && sampleRate <= 384000 ? sampleRate : 48000;
        kind_ = kind;
        parameterCount_ = 0;
        for (std::size_t i = 0; i < creativeParameterCount; ++i)
            if (creativeParameters[i].kind == kind) parameterIndices_[parameterCount_++] = i;
        for (auto& b : rings_) b.release();
        for (auto& b : allpass_) b.release();
        for (auto& b : reverbPitchRings_) b.release();
        if (kind == CreativeKind::delay) {
            for (int i = 0; i < 2; ++i) rings_[i].allocate(static_cast<std::size_t>(sr_ * 24.1) + 8);
        } else if (kind == CreativeKind::reverb) {
            for (int i = 0; i < 8; ++i) {
                rings_[i].allocate(static_cast<std::size_t>(sr_ * .24) + 8);
                reverbPitchRings_[i].allocate(static_cast<std::size_t>(sr_ * .06) + 8);
            }
            for (int i = 8; i < 10; ++i) rings_[i].allocate(static_cast<std::size_t>(sr_ * .55) + 8);
            constexpr double ap[] = {.0047, .0079, .0127, .0173, .0053, .0089, .0137, .0191};
            for (int i = 0; i < 8; ++i) allpass_[i].allocate(static_cast<std::size_t>(sr_ * ap[i]) + 3);
        } else if (kind == CreativeKind::granular) {
            for (int i = 0; i < 2; ++i) rings_[i].allocate(static_cast<std::size_t>(sr_ * 3) + 8);
        }
        smoothStep_ = 1.f - std::exp(-1.f / static_cast<float>(sr_ * .025));
        duckAttack_ = std::exp(-1.f / static_cast<float>(sr_ * .005));
        ready_ = true;
        reset();
    }
    void reset() noexcept {
        for (auto& b : rings_) b.reset();
        for (auto& b : allpass_) b.reset();
        for (auto& b : reverbPitchRings_) b.reset();
        swellPeak_ = swellEnvelope_ = 0;
        capturedFrames_ = 0; granularFrozen_ = false;
        for (auto& b : bands_) b.reset();
        damp_.fill(0); scLow_.fill(0); held_.fill(0); grains_ = {};
        initialized_ = false; tick_ = 0; duckEnv_ = compEnv_ = reduction_ = targetReduction_ = 0;
        compGain_ = targetCompGain_ = 1; grainNormalization_ = 1; crusherPhase_ = 1; spawn_ = 0;
        pitchPhase_ = modPhase_ = 0; pitchBlend_ = 0; rng_ = 0x6d2b79f5u;
    }
    std::array<float, 2> process(float left, float right, const CreativeSettings& settings, double bpm = 120) noexcept {
        if (!ready_) return {};
        left = creative_detail::clean(left); right = creative_detail::clean(right);
        // Only the selected kind's controls are touched. Invalid automation falls
        // back to descriptor defaults before it can poison recursive state.
        for (std::size_t p = 0; p < parameterCount_; ++p) {
            const auto i = parameterIndices_[p];
            const auto& d = creativeParameters[i];
            const float v = std::isfinite(settings.values[i]) ? std::clamp(settings.values[i], d.min, d.max) : d.initial;
            if (!initialized_) smoothed_[i] = v;
            else smoothed_[i] += smoothStep_ * (v - smoothed_[i]);
        }
        if (!initialized_ || (tick_ & 31u) == 0) update(bpm);
        initialized_ = true;
        std::array<float, 2> out{};
        switch (kind_) {
            case CreativeKind::delay: out = delay(left, right); break;
            case CreativeKind::reverb: out = reverb(left, right); break;
            case CreativeKind::compressor: out = compressor(left, right); break;
            case CreativeKind::bitcrusher: out = crusher(left, right); break;
            case CreativeKind::granular: out = granular(left, right); break;
        }
        ++tick_;
        return {creative_detail::clean(out[0]), creative_detail::clean(out[1])};
    }
    // Positive attenuation, before makeup, audio-thread read (publish externally).
    float gainReductionDb() const noexcept {
            return kind_ == CreativeKind::compressor ? -20.f * std::log10(std::max(1.e-12f, compGain_)) : 0.f;
        }

private:
    using P = CreativeParameter;
    using Ring = creative_detail::Ring;
    double sr_ = 48000;
    CreativeKind kind_ = CreativeKind::delay;
    bool ready_ = false, initialized_ = false;
    std::array<std::size_t, creativeParameterCount> parameterIndices_{};
    std::size_t parameterCount_ = 0;
    std::uint64_t tick_ = 0;
    std::uint32_t rng_ = 0x6d2b79f5u;
    std::array<float, creativeParameterCount> smoothed_{};
    std::array<Ring, 10> rings_;
    std::array<Ring, 8> allpass_, reverbPitchRings_;
    std::array<creative_detail::Band, 2> bands_{};
    std::array<float, 8> damp_{}, lineGain_{};
    std::array<double, 8> lineTime_{}, currentLineTime_{};
    std::array<float, 2> scLow_{}, held_{};
    std::array<creative_detail::Grain, 32> grains_{};
    std::uint64_t capturedFrames_ = 0;
    bool granularFrozen_ = false;
    float smoothStep_ = 0, duckAttack_ = 0, duckRelease_ = 0, duckEnv_ = 0, duckAmount_ = 0;
    float compEnv_ = 0, reduction_ = 0, targetReduction_ = 0, compGain_ = 1, targetCompGain_ = 1;
    float attack_ = 0, release_ = 0, makeup_ = 1, scHP_ = 0;
    float feedback_ = 0, ping_ = 0, drive_ = 0, pitchRatio_ = 1, pitchBlend_ = 0;
    float damping_ = 0, diffusion_ = 0, early_ = 0, width_ = 1, modulation_ = 0, freeze_ = 0;
    float quantStep_ = 0, rateStep_ = 0, dither_ = 0, grainIncrement_ = 0, density_ = 0;
    float grainNormalization_ = 1;
    float reverbPitchAmount_ = 0, swellAttack_ = 0, swellRelease_ = 0;
    float swellPeak_ = 0, swellEnvelope_ = 0;
    double delayTime_ = 1, predelay_ = 1, pitchPhase_ = 0, modPhase_ = 0, crusherPhase_ = 1, spawn_ = 0;
    float value(P p) const noexcept { return smoothed_[static_cast<std::size_t>(p)]; }
    float pole(float ms) const noexcept { return std::exp(-1000.f / (ms * static_cast<float>(sr_))); }
    float cutoff(float hz) const noexcept {
        return 1.f - std::exp(-2.f * creative_detail::pi * std::min(hz, static_cast<float>(sr_ * .45)) / static_cast<float>(sr_));
    }
    float random() noexcept {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ >> 8) * (1.f / 16777216.f);
    }
    void setBand(float low, float high) noexcept {
        for (auto& b : bands_) { b.hp = cutoff(low); b.lp = cutoff(high); }
    }
    void update(double bpm) noexcept {
        using creative_detail::dbGain;
        // Expensive conversions are shared by the next 32 audio samples.
        switch (kind_) {
            case CreativeKind::delay: {
                const double tempo = std::isfinite(bpm) && bpm >= 20 && bpm <= 400 ? bpm : 120;
                delayTime_ = value(P::delaySync) >= .5f ? sr_ * 60 * value(P::delayBeats) / tempo : sr_ * .001 * value(P::delayTime);
                delayTime_ = std::clamp(delayTime_, 1.0, sr_ * 24.0);
                feedback_ = value(P::delayFeedback) * .01f; ping_ = value(P::delayPingPong) * .01f;
                drive_ = dbGain(value(P::delayDrive)) - 1;
                duckAmount_ = dbGain(value(P::delayDuck)) - 1.f; duckRelease_ = pole(value(P::delayDuckRelease));
                pitchRatio_ = std::pow(2.f, value(P::delayPitch) / 12.f);
                setBand(value(P::delayLowCut), value(P::delayHighCut)); break;
            }
            case CreativeKind::reverb: {
                predelay_ = std::max(1.0, sr_ * .001 * value(P::reverbPredelay));
                constexpr double times[] = {.0297, .0371, .0411, .0437, .0531, .0617, .0719, .0793};
                const double size = .45 + 1.55 * value(P::reverbSize) * .01;
                for (int i = 0; i < 8; ++i) {
                    lineTime_[i] = sr_ * times[i] * size;
                    lineGain_[i] = std::pow(10.f, static_cast<float>(-3 * times[i] * size / value(P::reverbDecay)));
                }
                damping_ = cutoff(value(P::reverbDamping)); diffusion_ = value(P::reverbDiffusion) * .007f;
                early_ = value(P::reverbEarly) * .01f; width_ = value(P::reverbWidth) * .01f;
                modulation_ = value(P::reverbModulation) * .001f * static_cast<float>(sr_);
                duckAmount_ = dbGain(value(P::reverbDuck)) - 1.f; duckRelease_ = pole(value(P::reverbDuckRelease));
                drive_ = dbGain(value(P::reverbSaturation)) - 1; freeze_ = value(P::reverbFreeze);
                pitchRatio_ = std::pow(2.f, value(P::reverbPitch) / 12.f);
                reverbPitchAmount_ = value(P::reverbPitchAmount) * .01f;
                swellAttack_ = value(P::reverbSwell) > 0 ? pole(value(P::reverbSwell)) : 0;
                swellRelease_ = pole(100);
                setBand(value(P::reverbLowCut), value(P::reverbHighCut)); break;
            }
            case CreativeKind::compressor:
                attack_ = pole(value(P::compressorAttack)); release_ = pole(value(P::compressorRelease));
                makeup_ = dbGain(value(P::compressorMakeup)); scHP_ = cutoff(value(P::compressorSidechainHP)); break;
            case CreativeKind::bitcrusher:
                quantStep_ = std::pow(2.f, 1.f - value(P::crusherBits));
                rateStep_ = std::min(1.f, value(P::crusherRate) / static_cast<float>(sr_));
                drive_ = dbGain(value(P::crusherDrive)); dither_ = value(P::crusherDither) * .01f;
                for (auto& b : bands_) { b.hp = 0; b.lp = cutoff(value(P::crusherLowpass)); } break;
            case CreativeKind::granular:
                pitchRatio_ = std::pow(2.f, value(P::granularPitch) / 12.f);
                grainIncrement_ = 1000.f / (value(P::granularSize) * static_cast<float>(sr_));
                density_ = value(P::granularDensity) / static_cast<float>(sr_);
                drive_ = dbGain(value(P::granularDrive)); makeup_ = 1.f / std::sqrt(drive_);
                setBand(value(P::granularLowCut), value(P::granularHighCut)); break;
        }
    }
    float duck(float l, float r) noexcept {
        const float x = std::max(std::abs(l), std::abs(r));
        const float a = x > duckEnv_ ? duckAttack_ : duckRelease_;
        duckEnv_ = creative_detail::clean(x + a * (duckEnv_ - x));
        // Rational amplitude law: depth is exact at an envelope of 0.25 (-12 dBFS).
        return 1.f / (1.f + duckAmount_ * std::min(1.f, duckEnv_ * 4.f));
    }
    std::array<float, 2> delay(float l, float r) noexcept {
        // Time slewing is continuous (tape-style pitch change), never a tap jump.
        // The control-rate target is followed at audio rate.
        if (tick_ == 0) currentTime_ = delayTime_;
        currentTime_ += smoothStep_ * (delayTime_ - currentTime_);
        std::array<float, 2> wet{};
        const double window = sr_ * .06;
        const float pitchTarget = std::min(1.f, std::abs(value(P::delayPitch)) * 10.f);
        if (tick_ == 0) pitchBlend_ = pitchTarget;
        pitchBlend_ += smoothStep_ * (pitchTarget - pitchBlend_);
        for (int c = 0; c < 2; ++c) {
            float x = rings_[c].read(currentTime_);
            if (pitchBlend_ > 1.e-6f) {
                const double p = pitchPhase_, q = p < .5 ? p + .5 : p - .5;
                const float w = static_cast<float>(1.0 - std::abs(2 * p - 1));
                const float shifted = w * rings_[c].read(currentTime_ + p * window)
                    + (1 - w) * rings_[c].read(currentTime_ + q * window);
                x += pitchBlend_ * (shifted - x);
            }
            wet[c] = bands_[c].process(x);
        }
        pitchPhase_ += (1.0 - pitchRatio_) / window;
        if (pitchPhase_ < 0) pitchPhase_ += 1; else if (pitchPhase_ >= 1) pitchPhase_ -= 1;
        // A centered source must seed only one side at full ping-pong, otherwise
        // cross-feedback simply repeats two identical signals down the center.
        const float mono = (l + r) * .5f;
        const float excitationL = l * (1 - ping_) + mono * ping_;
        const float excitationR = r * (1 - ping_);
        rings_[0].push(excitationL + feedback_ * creative_detail::saturate(wet[0] * (1 - ping_) + wet[1] * ping_, drive_));
        rings_[1].push(excitationR + feedback_ * creative_detail::saturate(wet[1] * (1 - ping_) + wet[0] * ping_, drive_));
        const float d = duck(l, r); return {wet[0] * d, wet[1] * d};
    }
    double currentTime_ = 1, currentPre_ = 1;
    std::array<float, 2> reverb(float l, float r) noexcept {
        if (tick_ == 0) currentPre_ = predelay_;
        currentPre_ += smoothStep_ * (predelay_ - currentPre_);
        const float preL = rings_[8].read(currentPre_), preR = rings_[9].read(currentPre_);
        rings_[8].push(l * (1 - freeze_)); rings_[9].push(r * (1 - freeze_));
        std::array<float, 2> input {preL, preR};
        for (int c = 0; c < 2; ++c) for (int j = 0; j < 4; ++j) {
            auto& ap = allpass_[c * 4 + j];
            const float z = ap.read(static_cast<double>(ap.data.size() - 2));
            const float y = z - diffusion_ * input[c];
            ap.push(input[c] + diffusion_ * y); input[c] = y;
        }
        std::array<float, 8> taps{};
        float sum = 0;
        modPhase_ += .19 / sr_; if (modPhase_ >= 1) modPhase_ -= 1;
        for (int i = 0; i < 8; ++i) {
            // Triangle modulation avoids transcendental functions in the sample loop.
            double phase = modPhase_ + i * .123; if (phase >= 1) phase -= 1;
            const double mod = (1 - 4 * std::abs(phase - .5)) * modulation_ * (1 - freeze_);
            // Freeze removes damping/modulation and leaves a near-lossless orthogonal FDN.
            if (tick_ == 0) currentLineTime_[i] = lineTime_[i];
            currentLineTime_[i] += smoothStep_ * (lineTime_[i] - currentLineTime_[i]);
            const float x = rings_[i].read(currentLineTime_[i] + mod);
            damp_[i] = creative_detail::clean(damp_[i] + damping_ * (x - damp_[i]));
            taps[i] = damp_[i] * (1 - freeze_) + x * freeze_; sum += taps[i];
        }
        for (int i = 0; i < 8; ++i) {
            // Householder reflection I - 2 vv^T / (v^T v), norm-preserving.
            float mixed = taps[i] - sum * .25f;
            auto& pitch = reverbPitchRings_[i];
            pitch.push(mixed);
            const float amount = reverbPitchAmount_ * (1 - freeze_)
                * std::min(1.f, std::abs(value(P::reverbPitch)) * 10.f);
            if (amount > 1.e-6f) {
                const double p = pitchPhase_, q = p < .5 ? p + .5 : p - .5;
                const float w = static_cast<float>(1 - std::abs(2 * p - 1));
                const float shifted = w * pitch.read(2 + p * sr_ * .06)
                    + (1 - w) * pitch.read(2 + q * sr_ * .06);
                // Convex tap crossfade, with headroom for time compression and
                // overlapping read heads. Replace, never add, feedback energy.
                // Freeze fades this lossy branch out of the orthogonal network.
                mixed += amount * (.45f * shifted - mixed);
            }
            const float g = lineGain_[i] * (1 - freeze_) + .99998f * freeze_;
            const float excitation = ((i & 1) ? input[1] : input[0]) * .22f * (1 - freeze_);
            rings_[i].push(excitation + g * creative_detail::saturate(mixed, drive_ * (1 - freeze_)));
        }
        pitchPhase_ += (1.0 - pitchRatio_) / (sr_ * .06);
        if (pitchPhase_ < 0) pitchPhase_ += 1; else if (pitchPhase_ >= 1) pitchPhase_ -= 1;
        float a = (taps[0] + taps[2] - taps[4] - taps[6]) * .5f;
        float b = (taps[1] + taps[3] - taps[5] - taps[7]) * .5f;
        // Sparse, alternating-side early taps precede the dense tail.
        a += early_ * (1 - freeze_) * (.5f * preL + .3f * rings_[9].read(currentPre_ + sr_ * .019) - .2f * rings_[8].read(currentPre_ + sr_ * .043));
        b += early_ * (1 - freeze_) * (.5f * preR + .3f * rings_[8].read(currentPre_ + sr_ * .023) - .2f * rings_[9].read(currentPre_ + sr_ * .047));
        a = bands_[0].process(a); b = bands_[1].process(b);
        // Stereo-linked wet transient shaping: a slow attack catches up with a
        // fast peak follower. The common release preserves decaying tails;
        // unlike predelay this softens onset without moving any read taps.
        const float peak = std::max(std::abs(a), std::abs(b));
        swellPeak_ = creative_detail::clean(std::max(peak, swellPeak_ * swellRelease_));
        const float coefficient = swellPeak_ > swellEnvelope_ ? swellAttack_ : swellRelease_;
        swellEnvelope_ = creative_detail::clean(swellPeak_ + coefficient * (swellEnvelope_ - swellPeak_));
        const float swell = swellAttack_ == 0 ? 1.f
            : std::min(1.f, swellEnvelope_ / std::max(1.e-20f, swellPeak_));
        const float mid = (a + b) * .5f, side = (a - b) * .5f * width_, d = duck(l, r) * swell;
        // Fixed output-only calibration, measured in docs/DSP-Audit.md. Never
        // normalizes the tail, changes loop stability, or adds a hidden dry path.
        return {(mid + side) * d * reverbWetCalibration, (mid - side) * d * reverbWetCalibration};
    }
    std::array<float, 2> compressor(float l, float r) noexcept {
        const std::array<float, 2> in {l, r}; float peak = 0;
        for (int c = 0; c < 2; ++c) {
            scLow_[c] = creative_detail::clean(scLow_[c] + scHP_ * (in[c] - scLow_[c]));
            peak = std::max(peak, std::abs(in[c] - scLow_[c]));
        }
        const float a = peak > compEnv_ ? attack_ : release_;
        compEnv_ = creative_detail::clean(peak + a * (compEnv_ - peak));
        if ((tick_ & 7u) == 0) {
            const float over = 20.f * std::log10(std::max(1.e-12f, compEnv_)) - value(P::compressorThreshold);
            const float knee = value(P::compressorKnee), slope = 1.f - 1.f / value(P::compressorRatio);
            targetReduction_ = knee > 0 && over > -knee * .5f && over < knee * .5f
                ? slope * (over + knee * .5f) * (over + knee * .5f) / (2 * knee)
                : slope * std::max(0.f, over);
            targetCompGain_ = creative_detail::dbGain(-targetReduction_);
        }
        // Detector provides attack/release; a short gain smoother removes 8-sample steps.
        compGain_ += .25f * (targetCompGain_ - compGain_);
        reduction_ += .25f * (targetReduction_ - reduction_);
        return {l * compGain_ * makeup_, r * compGain_ * makeup_};
    }
    std::array<float, 2> crusher(float l, float r) noexcept {
        crusherPhase_ += rateStep_;
        if (crusherPhase_ >= 1) {
            crusherPhase_ -= std::floor(crusherPhase_);
            const std::array<float, 2> in {l, r};
            for (int c = 0; c < 2; ++c) {
                // Dither is input-gated: exact digital silence remains silent.
                const float noise = in[c] == 0 ? 0 : (random() - random()) * quantStep_ * dither_;
                const float x = std::clamp(in[c] * drive_ + noise, -1.f, 1.f);
                held_[c] = std::round(x / quantStep_) * quantStep_ / drive_;
            }
        }
        return {bands_[0].process(held_[0]), bands_[1].process(held_[1])};
    }
    std::array<float, 2> granular(float l, float r) noexcept {
        const bool freeze = value(P::granularFreeze) >= .5f;
        granularFrozen_ = freeze;
        // Existing grains finish normally; new grains address the same frozen
        // history. Freeze stops capture, not grain/window progression.
        if (!freeze) {
            rings_[0].push(creative_detail::saturate(l * drive_, (drive_ - 1) / drive_) * makeup_);
            rings_[1].push(creative_detail::saturate(r * drive_, (drive_ - 1) / drive_) * makeup_);
            ++capturedFrames_;
        }
        const double captured = static_cast<double>(capturedFrames_);
        const double valid = std::min(captured, static_cast<double>(rings_[0].data.size()));
        const double oldest = captured - valid;
        spawn_ -= density_;
        if (spawn_ <= 0) {
            spawn_ += std::max(.1f, 1.f + (random() * 2 - 1) * value(P::granularJitter) * .008f);
            for (auto& g : grains_) if (!g.active) {
                g = {}; g.active = true; g.phase = 0; g.increment = grainIncrement_;
                g.step = pitchRatio_ * (value(P::granularReverse) >= .5f ? -1 : 1);
                const double length = 1.0 / grainIncrement_;
                // Keep even a +12 st forward grain behind the write head for its
                // entire lifetime, with interpolation guard samples on both ends.
                const double guard = 4 + length * (freeze ? std::abs(g.step) : std::max(0.0, g.step - 1));
                const double scatter = random() * value(P::granularJitter) * .001 * sr_;
                const double lookback = guard + value(P::granularPosition) * .001 * sr_ + scatter;
                // Clamp requested history to recorded data, not zero-filled
                // storage. At startup a forward grain may wait for its first
                // valid sample; reverse/frozen grains never wrap into unwritten
                // history. Interpolation always requires two captured samples.
                const double backwardsTravel = length * std::max(0.0, (freeze ? 0.0 : 1.0) - g.step);
                const double maxLookback = std::max(4.0, valid - 2 - backwardsTravel);
                g.position = captured - std::min(lookback, maxLookback);
                g.pan = (random() * 2 - 1) * value(P::granularSpread) * .01f;
                break;
            }
        }
        float a = 0, b = 0, overlapL = 0, overlapR = 0;
        for (auto& g : grains_) if (g.active) {
            // Hann-like polynomial window: zero value/slope at both boundaries.
            const float t = g.phase * (1 - g.phase), w = 16 * t * t;
            const float weightL = w * (1 - g.pan * .65f), weightR = w * (1 + g.pan * .65f);
            const bool readable = g.position >= oldest && g.position < captured - 1;
            if (!readable && g.hasRead) g.retiring = true;
            if (readable && !g.retiring) {
                const double ringPosition = static_cast<double>(rings_[0].head) - (captured - g.position);
                g.last = {rings_[0].at(ringPosition), rings_[1].at(ringPosition)};
                g.lastPosition = g.position; g.hasRead = true;
            }
            // A freeze transition can stop the write head in front of an
            // existing forward grain; unfreeze can overwrite an old reverse
            // grain. Hold the last valid sample and finish a 2ms linear fade,
            // rather than reading unrelated history or jumping to digital zero.
            if (g.retiring) g.boundaryGain = std::max(0.f, g.boundaryGain - static_cast<float>(1 / (.002 * sr_)));
            a += g.last[0] * weightL * g.boundaryGain;
            b += g.last[1] * weightR * g.boundaryGain;
            overlapL += weightL * g.boundaryGain; overlapR += weightR * g.boundaryGain;
            g.position += g.step;
            if (g.boundaryGain == 0) g.active = false;
            g.phase += g.increment; if (g.phase >= 1) g.active = false;
        }
        // Use surviving grains' actual windows/lifetimes, not the new spawn
        // settings. Floor at one to retain isolated windows and gaps. A common
        // stereo envelope preserves the image and bounds even coherent grains:
        // follow rising (already smooth) window sums immediately, release over
        // 25 ms. A symmetric smoother could lag a rising overlap and overshoot.
        const float overlap = std::max(1.f, std::max(overlapL, overlapR));
        grainNormalization_ = std::max(overlap, grainNormalization_ + smoothStep_ * (overlap - grainNormalization_));
        const float gain = 1.f / grainNormalization_;
        return {bands_[0].process(a * gain), bands_[1].process(b * gain)};
    }
};

} // namespace vocalstack
