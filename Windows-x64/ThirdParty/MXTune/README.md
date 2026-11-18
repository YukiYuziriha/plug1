# Automatic tuner donor audit

## Provenance and license

Selected detector: MXTune `pitch_detector_talent.cpp` and `.h`, pinned to
`ea73804baff9e483d70b0056ebc91523402210eb` from
https://github.com/liuanlin-mx/MXTune . These two original files are preserved
verbatim here for attribution/audit and are **not compiled**. Both carry
Copyright (C) 2021-2023 Liu An Lin <liuanlin-mx@qq.com> and GPL-3.0-or-later
notices. `LICENSE` is the donor's complete GPLv3 license; `COPYING-autotalent`
is its upstream Autotalent notice and GPLv2 license.

The MXTune README identifies Thomas A. Baran's Autotalent as the detector's
source. The original Autotalent 0.2 `autotalent.c` explicitly grants
GPL-2.0-or-later (not GPL-2.0-only), so selecting GPLv3 is permitted.
`autotalent-0.2-notice.txt` preserves that original source header, including
its no-warranty clause. Verified against:
https://web.mit.edu/tbaran/www/autotalent-0.2.tar.gz
SHA256: `105e98a0d118c0ea78432d055b9afa478a7d541e5e3fe369498104674cc504d8`.
GPLv3 permits combination with AGPLv3 under section 13. No proprietary code
or binary donor dependencies were used.

## Actual adaptation

`source/src/AutomaticTuner.h` ports the donor's middle-half raised-cosine
window, FFT autocovariance, inverse window-autocorrelation compensation,
maximum biased local peak selection, and unbiased voicing confidence.
Changes include RAII/preallocated storage; the existing MIT Signalsmith Linear
FFT in place of FFTW; explicit silence/finite/index guards; quadratic peak
interpolation of the locally refined unbiased peak instead of a three-bin
center of mass; keeping DC in the window autocorrelation (not the signal
spectrum) to avoid biased fractional-lag estimates; a wider configurable vocal
range; linked stereo selection; and new quantization/hysteresis/control code. Donor FFTW, ring buffer, plugin wrappers and pitch shifter are not linked.
No PureData/mayer FFT source is copied or used.

MXTune's pitch-synchronous shifter was investigated but not adopted: its
mono fragment engine would need additional stereo-linking and formant work.
Instead the implementation uses the project's already pinned MIT Signalsmith
Stretch `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` (1.3.2), with actual
`setFormantSemitones(0, true)` spectral-envelope compensation. This is not a
claim of perfect preservation at the short legacy window. Signalsmith's
existing license notices remain in `ThirdParty/Signalsmith-Stretch.txt` and
`ThirdParty/Signalsmith-Linear.txt`. The existing Linear dependency is pinned
at `7bb1c8f36794d6980080ce817c91f8da8fe56a66`.

The shifter uses a 1/32-window hop and a monotonic harmonic-anchored frequency
map: additive shifts around detected harmonics, with linear ramps between
harmonics. This avoids scaling spectral-centroid errors. The much higher
overlap is deliberate: the legacy 1/4-window hop produced roughly 25 cents
of error in a tested one-semitone correction. No changes to Signalsmith's
source were needed. Correction is bounded to +/-12 semitones for numeric and
mapping safety; normal nearest-note quantization requires at most 6 semitones.

## Integration/latency limits

This is wet-only, one shared tuner before fan-out. Do not place it serially in
front of an already delayed lead shifter without accounting for the sum.
It **does not** integrate itself into the engine or alter host latency.
Audio latency is queried from Stretch: configured to the existing 1344 sample
window at 44.1/48 kHz (30.48/28 ms), otherwise rounded proportionally to 48 kHz.
Confirm with `latency()` after prepare. The detector adds no audio delay, but
needs a full power-of-two window >=64 ms after reset. At 48 kHz this is 4096
samples (85.33 ms), with 240-sample (5 ms) updates. At 44.1 kHz: 4096 samples
(92.88 ms), 221-sample updates. The raised cosine's useful support is the
middle half; steady-state decisions describe audio ~half a detector window
in the past. Hard-retune zero removes control smoothing, NOT detector history
or spectral-window smearing. Low-register/onset accuracy is constrained by
this causal latency compromise. Offline synthetic tests are not a DAW vocal
listening evaluation.

Initial lock occurs on the next 5 ms analysis tick after the complete history
is available; control commands update every 32 samples independently of host
block partitioning. The detector observes the incoming signal rather than
adding a detector-length look-ahead delay. Fast pitch jumps/onsets therefore
have a causal tracking lag and may initially retain the previous correction.
This is not zero-latency or sample-accurate onset tuning.

The isolated MSVC Release suite measured a worst error of 6.48 cents across
26 pure tones from C2 to C6, each +/-35 cents detuned. A synthetic vowel
spectral-envelope test improved from squared normalized envelope error 0.292
without compensation to 0.095 with compensation. These are limited synthetic
checks, not universal vocal-quality guarantees. An informational 48 kHz stereo
run measured ~0.217 processing-seconds/audio-second on the development machine
(including allocation guards and output checks), about 22% of one logical
core. Host load and machine differences matter; the high-overlap path is
substantially more expensive than the existing subtle doubler.

See `source/tests/tuning/README.md` for the complete public interface and test
invocation. No existing engine, processor, UI, build configuration or package
binary is changed by this standalone module.
