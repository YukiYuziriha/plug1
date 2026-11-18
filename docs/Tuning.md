# HEAVEN by lil grass block — Tuning — v1.12.0

[Overview](../README.md) · [Getting Started](Getting-Started.md) ·
[Licenses and source](Licensing.md)

HEAVEN by lil grass block was formerly named VocalStack (displayed as Vocal
Stack). This rename is not a new plugin ID: internal IDs, preset folders and
state extensions are preserved for compatibility.

Tuning is automatic correction of a **single vocal melody**, not polyphonic
chord separation. The Tuning tab contains real parameter-linked controls styled
for Device, Neon and Spectral; the images in `docs/design` remain design
prototypes, not release screenshots or evidence of native GUI validation.

## First use

1. Open **Tuning**, turn **Tuning On** on, and watch for **Host audio restart
   pending**. Enabling from an OFF-prepared instance requires the host to
   **deactivate/reactivate its audio processing for the plugin**, or reload it.
   Stopping/starting transport is not sufficient. Until re-preparation, the old
   prepared audio path and reported latency remain in use.
2. Choose **Key**, **Scale** and **Vocal Range** for the singer. Chromatic allows
   all twelve pitch classes. Major, Natural Minor, Harmonic Minor, Major
   Pentatonic and Minor Pentatonic constrain the targets. Clicking a note button
   edits the allowed pitch classes and switches to Custom. No allowed notes
   means no correction, not an automatic chromatic fallback.
3. Set **Correction** (pitch-error strength), **Retune Speed** (correction slew),
   **Note Transition** (target glide) and **Note Stability** (boundary hysteresis
   and dwell). A lower stability can switch targets more readily near boundaries.
   Reference sets A4, normally 440 Hz. Range restricts detection, not harmony
   transposition.

   **Key**, **Scale** and **Vocal Range** use themed dropdowns: one vertical
   column, with the selected item highlighted. Lists of up to seven choices
   show every item; longer lists scroll. Browsing does not change the setting.
   After 2.5 seconds without scroll activity, the list returns to the selected
   item. Scrollbar/touch drags wait for release; wheel inertia restarts the wait.

4. Use **Module Mix** for the shared clean/tuned audio blend, then each Lead or
   generated voice's **Tune Mix** to choose its contribution from that blend.
   At 0% Tune Mix the voice takes the aligned clean source; at 100% it takes the
   module blend. Effective tuned contribution is Module Mix × voice Tune Mix.
   This is separate from Correction, Lead / Stack, FX Mix and Global Mix.
5. **Preserve Formants** enables spectral-envelope compensation during tuning.
   It is separate from creative per-voice formant offsets, and is not perfect
   preservation for every voice or setting. **Reset Tuning** resets the shared
   module only, retaining per-voice Tune Mix settings.

The pitch monitor reports detected pitch/frequency, target, applied correction
and confidence. Silence/unvoiced input has no reliable note. A displayed pitch
is a causal estimate from recent history, not an instantaneous measurement.

## One shared tuner, not a tuner per voice

`source/src/AutomaticTuner.h` embeds the full detector adaptation and correction
algorithm. One shared MXTune/Autotalent-derived detector drives one linked
stereo Signalsmith Stretch shifter before voice fan-out. The engine keeps
latency-aligned clean and tuned sources, blends them per voice, and renders each
existing voice once. It does not run an independent detector or duplicate the
whole voice renderer for every clean/wet blend. Mono input is handled internally;
stereo uses one shared pitch decision rather than independently retuning L/R.

The detector uses windowed FFT autocovariance with inverse window correlation
compensation. New target selection, stability, transition and correction controls
feed a harmonic-anchored frequency map in Signalsmith. Formant compensation uses
Signalsmith's spectral-envelope mechanism. No proprietary tuning binary, online
service or separately installed tuner is required. See the
[integration notice](../ThirdParty/Tuning.md) for provenance and retained licenses.

## Latency and tracking time are different

At **48 kHz**, Tuning adds **1344 samples / 28 ms** of audio-path latency when
reserved at host preparation. The basic patch is **2688 samples / 56 ms** with
Tuning ON-prepared, versus the legacy **1344 samples / 28 ms** OFF-prepared.
A declared Spectral Morph still adds its separate **4096-sample** routing budget.
Global dry, bypass and clean/tuned blends must remain aligned with the prepared
path. Turning tuning off in an ON-prepared instance can blend back to clean
without immediately removing its reserved latency; re-prepare OFF to reclaim it.
Module Mix or Tune Mix at zero does not itself reclaim that reservation.

**Retune Speed = 0 is not zero detector time.** After reset, the 48 kHz detector
requires 4096 samples of history (about **85.33 ms**), then analyzes on roughly
5 ms ticks. Its steady-state estimate represents earlier audio (approximately
half a detector window ago). This observation history is not an additional
4096-sample audio look-ahead buffer or an extra host-reported latency term.
Onsets, large pitch jumps, low notes and unvoiced consonants can lag or produce
uncertain estimates even with zero Retune Speed and Note Transition.

## Validation boundaries

Real-vocal listening and actual DAW tuning workflow remain **unverified**.
Synthetic pitch/formant checks are not a vocal-quality guarantee. This is not a
claim of Antares/Auto-Tune equivalence, matching proprietary tracking behavior,
transparent correction in all conditions, or sample-accurate onset tuning.
Short-window formant quality, artifacts and CPU cost need listening and host
validation. The high-overlap shifter is more expensive than the legacy doubler.

See the [1.12.0 delivery record](Release-1.12.0-Validation.md) for packaging status.
