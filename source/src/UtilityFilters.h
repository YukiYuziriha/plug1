#pragma once
// Synth filter DSP is GPL-3.0-or-later Vital-derived code; see VitalFilterDSP.h
// and ../../ThirdParty/Vital/README.md. XP remains an original design.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <vector>
#include "VitalFilterDSP.h"
#include "UtilityTelemetry.h"

namespace vocalstack {
enum class UtilityKind { gain, stereo, gate, xpFilter, synthFilter };
// Append, never reorder, once persisted by the host.
enum class UtilityParameter : std::size_t {
    gainDb, stereoWidth, stereoBalance, stereoSwap, stereoInvertLeft, stereoInvertRight,
    gateThreshold, gateRange, gateRatio, gateAttack, gateHold, gateRelease,
    xpMode, xpCutoff, xpResonance, xpDrive,
    synthMode, synthCutoff, synthResonance, synthDrive, synthFeedback, count
};
inline constexpr std::size_t utilityParameterCount = static_cast<std::size_t>(UtilityParameter::count);
struct UtilityParameterDescriptor {
    const char* id; const char* label; const char* unit;
    float min, max, initial;
    UtilityKind kind;
    bool discrete = false;
};
inline constexpr std::array<UtilityParameterDescriptor, utilityParameterCount> utilityParameters {{
    // -120 is the exact mute sentinel (display as -infinity), not -120 dB gain.
    {"gain_db", "Gain", "dB", -120, 24, 0, UtilityKind::gain},
    {"stereo_width", "Width", "%", 0, 200, 100, UtilityKind::stereo},
    {"stereo_balance", "Balance", "%", -100, 100, 0, UtilityKind::stereo},
    {"stereo_swap", "Swap channels", "", 0, 1, 0, UtilityKind::stereo, true},
    {"stereo_invert_left", "Invert left", "", 0, 1, 0, UtilityKind::stereo, true},
    {"stereo_invert_right", "Invert right", "", 0, 1, 0, UtilityKind::stereo, true},
    {"gate_threshold", "Threshold", "dB", -96, 0, -40, UtilityKind::gate},
    {"gate_range", "Range", "dB", 0, 96, 60, UtilityKind::gate},
    {"gate_ratio", "Expansion ratio", ":1", 1, 20, 20, UtilityKind::gate},
    {"gate_attack", "Opening attack", "ms", .01f, 200, 1, UtilityKind::gate},
    {"gate_hold", "Hold", "ms", 0, 1000, 30, UtilityKind::gate},
    {"gate_release", "Closing release", "ms", 1, 3000, 100, UtilityKind::gate},
    {"xp_mode", "Configuration", "", 0, 14, 0, UtilityKind::xpFilter, true},
    {"xp_cutoff", "Cutoff", "Hz", 20, 20000, 1000, UtilityKind::xpFilter},
    {"xp_resonance", "Resonance", "%", 0, 100, 0, UtilityKind::xpFilter},
    {"xp_drive", "Character drive", "dB", 0, 24, 0, UtilityKind::xpFilter},
    {"synth_mode", "Filter model", "", 0, 12, 0, UtilityKind::synthFilter, true},
    {"synth_cutoff", "Cutoff / fundamental", "Hz", 20, 20000, 1000, UtilityKind::synthFilter},
    {"synth_resonance", "Resonance", "%", 0, 100, 0, UtilityKind::synthFilter},
    {"synth_drive", "Drive", "dB", 0, 24, 0, UtilityKind::synthFilter},
    {"synth_feedback", "Comb feedback / vowel X", "%", -95, 95, 50, UtilityKind::synthFilter}
}};
struct UtilitySettings {
    std::array<float, utilityParameterCount> values{};
    UtilitySettings() noexcept {
        for (std::size_t i = 0; i < values.size(); ++i) values[i] = utilityParameters[i].initial;
    }
    float& operator[](UtilityParameter p) noexcept { return values[static_cast<std::size_t>(p)]; }
    float operator[](UtilityParameter p) const noexcept { return values[static_cast<std::size_t>(p)]; }
};

// Original XP-inspired workflow, NOT a model of Bitwig's algorithms. Each stage
// is a prewarped TPT SVF with optional unity-small-signal-slope soft saturation.
// BP is constant-peak (k*band); notch = low+high; allpass = low+high-k*band.
// Cascades share cutoff/Q: resonance and notch depths compound with stage count.
inline constexpr std::array<const char*, 15> xpFilterConfigurations {{
    "Low pass 12", "High pass 12", "Band pass 12", "Notch 12", "All pass 2",
    "Low pass 24", "High pass 24", "Band pass x2", "Notch x2", "All pass 4",
    "Low pass 36", "High pass 36", "Band pass x3", "Notch x3", "All pass 6"
}};
// GPL-covered adaptation of pinned Vital code, not category stand-ins.
// Modes 0..6 retain family/pass mappings; modes 7..12 append more Vital models.
inline constexpr auto synthFilterConfigurations = vital_filter::names;
struct UtilityResponse {
    double magnitude = 1;
    // False: exact frozen, settled linear response. True: local small-signal
    // approximation, including finite impulse rendering of nonlinear Vital DSP.
    // Self-oscillation and moving delays have no universal LTI transfer.
    bool approximate = false;
};

namespace utility_detail {
inline constexpr double pi = 3.14159265358979323846;
inline double rate(double sr) noexcept {
    return std::isfinite(sr) && sr >= 8000 && sr <= 384000 ? sr : 48000;
}
inline double db(double x) noexcept { return std::pow(10., x / 20.); }
inline double safe(double x) noexcept {
    return !std::isfinite(x) || std::abs(x) < 1.e-30 ? 0 : x;
}
inline float output(double x) noexcept {
    if (!std::isfinite(x)) return 0;
    constexpr double limit = static_cast<double>(std::numeric_limits<float>::max());
    return static_cast<float>(std::clamp(x, -limit, limit));
}
inline double parameter(const UtilitySettings& s, UtilityParameter p) noexcept {
    const auto i = static_cast<std::size_t>(p);
    const auto& d = utilityParameters[i];
    const double x = std::isfinite(s.values[i]) ? std::clamp(s.values[i], d.min, d.max) : d.initial;
    return d.discrete ? std::round(x) : x;
}
inline double cutoff(double hz, double sr) noexcept { return std::clamp(hz, 20., sr * .45); }
inline double damping(double resonance) noexcept { return 2. - 1.9 * resonance / 100.; }
// Zero drive is exactly linear; otherwise slope at zero stays one. No gain
// compensation pretending to preserve large-signal loudness. Not oversampled:
// strong drive aliases, and the graph is not a large-signal spectral prediction.
inline double shape(double x, double drive) noexcept {
    if (drive <= 0) return x;
    const double a = db(drive) - 1.;
    return std::tanh(a * x) / a;
}
struct SVF {
    double s1 = 0, s2 = 0;
    double run(double x, double g, double k, int mode) noexcept {
        const double a = 1. / (1. + g * (g + k));
        const double v1 = a * (s1 + g * (x - s2));
        const double v2 = s2 + g * v1;
        s1 = safe(2 * v1 - s1); s2 = safe(2 * v2 - s2);
        const double high = x - k * v1 - v2;
        switch (mode) {
            case 0: return v2;
            case 1: return high;
            case 2: return k * v1;
            case 3: return high + v2;
            default: return high + v2 - k * v1;
        }
    }
};
using Complex = std::complex<double>;
inline Complex svfResponse(double frequency, double sr, double hz, double k, int mode) noexcept {
    // Bilinear analog prototype, evaluated without tan(w/2) singularities.
    const Complex z = std::polar(1., -2 * pi * frequency / sr);
    const Complex a = 1. - z, b = std::tan(pi * cutoff(hz, sr) / sr) * (1. + z);
    const Complex den = a*a + k*a*b + b*b;
    switch (mode) {
        case 0: return b*b / den;
        case 1: return a*a / den;
        case 2: return k*a*b / den;
        case 3: return (a*a + b*b) / den;
        default: return (a*a - k*a*b + b*b) / den;
    }
}
} // namespace utility_detail

// Audio-thread owned; settings must be a snapshot. prepare() alone allocates
// (two roughly 50 ms Vital comb buffers). reset/process allocate nothing,
// take no locks. Synth response rendering is separately UI/worker-only.
// Supports 8--384 kHz; invalid rates use 48 kHz. No lookahead or explicit host
// latency. Comb delay is an intentional effect, with a direct path.
// Continuous controls are smoothed over 10 ms (comb delay 30 ms). Discrete
// filter changes reset filter state and bridge from the last output over 5 ms.
// First sample after prepare/reset uses settings immediately. Silence does not
// discard legitimate tails; tiny recursive values are flushed and nonfinite
// input becomes zero. Gain/stereo/gate have no arbitrary audio level ceiling.
class UtilityEffect {
public:
    void prepare(double sr, UtilityKind kind) {
        sr_ = utility_detail::rate(sr); kind_ = kind;
        if (kind == UtilityKind::synthFilter) vital_.prepare(sr_);
        step_ = 1. - std::exp(-1. / (.01 * sr_));

        bridgeStep_ = std::exp(-1. / (.005 * sr_));
        ready_ = true; reset();
    }
    void reset() noexcept {
        clearFilters();
        initialized_ = false; reduction_ = 0; hold_ = 0;
        gain_ = 1; monoAmount_ = 0; last_ = {}; bridge_ = {}; bridgeAmount_ = 0; mode_ = -1;
    }
    std::array<float, 2> process(float left, float right, const UtilitySettings& settings) noexcept {
        using namespace utility_detail;
        if (!ready_) return {};
        for (std::size_t i = 0; i < utilityParameterCount; ++i) {
            if (utilityParameters[i].kind != kind_) continue;
            const double target = parameter(settings, static_cast<UtilityParameter>(i));
            if (!initialized_ || utilityParameters[i].discrete) v_[i] = target;
            else v_[i] += step_ * (target - v_[i]);
        }
        std::array<double, 2> y {{std::isfinite(left) ? left : 0., std::isfinite(right) ? right : 0.}};
        if (kind_ == UtilityKind::gain) {
            // Smooth linear gain to reach true mute, not an asymptotic dB floor.
            const double targetDb = parameter(settings, P::gainDb);
            const double target = targetDb <= -120 ? 0 : db(targetDb);
            gain_ = initialized_ ? gain_ + step_ * (target - gain_) : target;
            if (target == 0 && gain_ < 1.e-12) gain_ = 0;
            for (auto& x : y) x *= gain_;
        } else if (kind_ == UtilityKind::stereo) {
            // Swap first, M/S width, unity-centre balance, then output polarity.
            // Smooth switches as continuous matrices too, avoiding hard clicks.
            for (std::size_t j = 0; j < switches_.size(); ++j) {
                const double t = get(static_cast<P>(static_cast<std::size_t>(P::stereoSwap) + j));
                switches_[j] = initialized_ ? switches_[j] + step_ * (t - switches_[j]) : t;
            }
            const double l = y[0] * (1-switches_[0]) + y[1] * switches_[0];
            const double r = y[1] * (1-switches_[0]) + y[0] * switches_[0];
            const double mid = (l+r)*.5, side = (l-r)*.5*get(P::stereoWidth)*.01;
            const double balance = get(P::stereoBalance)*.01;
            y = {{(mid+side)*(1-std::max(0., balance))*(1-2*switches_[1]),
                  (mid-side)*(1+std::min(0., balance))*(1-2*switches_[2])}};
        } else if (kind_ == UtilityKind::gate) {
            // Linked instantaneous peak detector, no lookahead. Above threshold
            // retriggers hold; attack opens, release closes. Ratio 1 is unity.
            const double peak = std::max(std::abs(y[0]), std::abs(y[1]));
            const double level = 20*std::log10(std::max(1.e-30, peak));
            double target = std::min(get(P::gateRange), std::max(0., (get(P::gateThreshold)-level)*(get(P::gateRatio)-1)));
            if (level >= get(P::gateThreshold)) hold_ = get(P::gateHold)*sr_*.001;
            else if (hold_ > 0) { hold_ -= 1; target = 0; }
            const double ms = target < reduction_ ? get(P::gateAttack) : get(P::gateRelease);
            reduction_ += (1-std::exp(-1./(ms*.001*sr_)))*(target-reduction_);
            for (auto& x : y) x *= db(-reduction_);
        } else {
            const bool xp = kind_ == UtilityKind::xpFilter;
            const int mode = static_cast<int>(get(xp ? P::xpMode : P::synthMode));
            if (mode != mode_) {
                if (initialized_) { bridge_ = last_; bridgeAmount_ = 1; clearFilters(); }
                mode_ = mode;
            }
            if (xp) {
                const double g = std::tan(pi * cutoff(get(P::xpCutoff), sr_)/sr_);
                const double k = damping(get(P::xpResonance)), drive = get(P::xpDrive);
                for (std::size_t c = 0; c < 2; ++c)
                    for (int stage = 0; stage <= mode/5; ++stage)
                        y[c] = svf_[c][static_cast<std::size_t>(stage)].run(shape(y[c], drive), g, k, mode%5);
            } else y = vital_.process(y[0], y[1], {mode, get(P::synthCutoff), get(P::synthResonance),
                                                  get(P::synthDrive), get(P::synthFeedback)});
            for (std::size_t c = 0; c < 2; ++c)
                y[c] = (1-bridgeAmount_)*y[c] + bridgeAmount_*bridge_[c];
            bridgeAmount_ *= bridgeStep_;
            if (bridgeAmount_ < 1.e-12) bridgeAmount_ = 0;
        }

        initialized_ = true;
        last_ = {{safe(y[0]), safe(y[1])}};
        return {{output(last_[0]), output(last_[1])}};
    }
    float gainReductionDb() const noexcept { return kind_ == UtilityKind::gate ? static_cast<float>(reduction_) : 0.f; }
    // AUDIO thread only: small bounded value copy, never a GUI getter.
    UtilityDynamicsSnapshot dynamicsSnapshot() const noexcept {
        return {initialized_ && kind_ == UtilityKind::gate
                    ? static_cast<float>(get(P::gateThreshold)) : -40.f, gainReductionDb()};
    }
    // AUDIO thread integration hook: call once per sample AFTER slot Mix,
    // before final output telemetry. Persist enabled per slot; bypass passes false.
    // Width, balance, polarity and Mix remain untouched; no hidden restore state.
    std::array<float, 2> applyMonoAudition(std::array<float, 2> postMix, bool enabled) noexcept {
        if (!ready_ || kind_ != UtilityKind::stereo) return postMix;
        monoAmount_ += step_ * ((enabled ? 1. : 0.) - monoAmount_);
        const double mid = (double(postMix[0]) + double(postMix[1])) * .5;
        for (auto& channel : postMix)
            channel = utility_detail::output(channel + monoAmount_ * (mid - channel));
        return postMix;
    }

    // UI/worker ONLY, allocating O(N) storage and doing O(N log N) work. Returns
    // N/2+1 linear magnitudes at bin * sampleRate/N (DC through Nyquist).
    // Renders the ACTUAL Vital-adapted tick path with a 1e-5 impulse, frozen
    // controls and zero initial state. No obsolete linear prototype equations.
    // Always approximate: finite impulse truncation, nonlinear operating point,
    // possible self-oscillation, and moving comb delays invalidate an exact LTI
    // interpretation. Higher N improves low-frequency/high-Q tail resolution.
    static std::vector<UtilityResponse> synthResponseCurve(double sampleRate, const UtilitySettings& settings,
                                                         std::size_t requestedSize = 32768) {
        using namespace utility_detail;
        const double sr = rate(sampleRate);
        std::size_t n = 512;
        while (n < std::min<std::size_t>(requestedSize,262144)) n *= 2;
        const vital_filter::Parameters p {static_cast<int>(parameter(settings,P::synthMode)),
            parameter(settings,P::synthCutoff),parameter(settings,P::synthResonance),
            parameter(settings,P::synthDrive),parameter(settings,P::synthFeedback)};
        const auto frame = vital_filter::makeFrame(sr,p);
        vital_filter::Channel render; render.prepare(sr);
        std::vector<Complex> bins(n);
        for (std::size_t i=0;i<n;++i) bins[i]=safe(render.tick(i==0?1.e-5:0,frame))/1.e-5;
        for (std::size_t i=1,j=0;i<n;++i) {
            std::size_t bit=n>>1;
            for (;j&bit;bit>>=1) j^=bit;
            j^=bit; if(i<j)std::swap(bins[i],bins[j]);
        }
        for (std::size_t length=2;length<=n;length*=2) {
            const Complex root=std::polar(1.,-2*pi/static_cast<double>(length));
            for (std::size_t start=0;start<n;start+=length) {
                Complex w=1;
                for (std::size_t j=0;j<length/2;++j) {
                    const Complex a=bins[start+j],b=w*bins[start+j+length/2];
                    bins[start+j]=a+b; bins[start+j+length/2]=a-b; w*=root;
                }
            }
        }
        std::vector<UtilityResponse> result(n/2+1);
        for(std::size_t i=0;i<result.size();++i)result[i]={std::abs(bins[i]),true};
        return result;
    }

    // Target settings, not a concurrent read of audio-thread state. SYNTH:
    // UI/worker ONLY: response() allocates and renders a whole curve per call!
    // For plotting, call synthResponseCurve ONCE and interpolate its FFT bins.
    // Never call synth response() per pixel, in paint(), or on the audio thread.
    // Gain is exact; stereo uses the LEFT->LEFT matrix entry (not a stereo
    // scalar response). Gate uses unity/open response and is marked approximate.
    static UtilityResponse response(double sampleRate, UtilityKind kind, double frequency,
                                    const UtilitySettings& settings) {
        using namespace utility_detail;
        const double sr = rate(sampleRate);
        const double f = std::isfinite(frequency) ? std::clamp(frequency, 0., sr*.5) : 0.;
        const auto p = [&settings](P id) { return parameter(settings, id); };
        if (kind == UtilityKind::gain) return {p(P::gainDb) <= -120 ? 0 : db(p(P::gainDb)), false};
        if (kind == UtilityKind::gate) return {1, true};
        if (kind == UtilityKind::stereo) {
            const double w = p(P::stereoWidth)*.01;
            return {std::abs(.5*(1+(p(P::stereoSwap) ? -w : w))*(1-std::max(0., p(P::stereoBalance)*.01))), false};
        }
        if (kind == UtilityKind::synthFilter) {
            const auto curve = synthResponseCurve(sr, settings);
            const double bin = f/sr*static_cast<double>((curve.size()-1)*2);
            const auto lo = static_cast<std::size_t>(bin);
            const auto hi = std::min(lo+1, curve.size()-1);
            return {curve[lo].magnitude+(bin-static_cast<double>(lo))*(curve[hi].magnitude-curve[lo].magnitude),true};
        }
        const int mode = static_cast<int>(p(P::xpMode));
        return {std::abs(std::pow(svfResponse(f,sr,p(P::xpCutoff),damping(p(P::xpResonance)),mode%5), mode/5+1)),p(P::xpDrive)>0};
    }
    UtilityResponse response(double frequency, const UtilitySettings& settings) const {
        return response(sr_, kind_, frequency, settings);
    }
private:
    using P = UtilityParameter;
    double get(P p) const noexcept { return v_[static_cast<std::size_t>(p)]; }
    void clearFilters() noexcept {
        svf_ = {}; vital_.reset();
    }
    double sr_ = 48000, step_ = 0, bridgeStep_ = 0;
    UtilityKind kind_ = UtilityKind::gain;
    bool ready_ = false, initialized_ = false;
    int mode_ = -1;
    std::array<double, utilityParameterCount> v_{};
    std::array<double, 3> switches_{};
    std::array<std::array<utility_detail::SVF, 3>, 2> svf_{};
    vital_filter::Filter vital_;
    double reduction_ = 0, hold_ = 0, gain_ = 1, bridgeAmount_ = 0, monoAmount_ = 0;
    std::array<double, 2> last_{}, bridge_{};
};
} // namespace vocalstack
