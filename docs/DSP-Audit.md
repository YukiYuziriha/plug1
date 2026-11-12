# DSP audit — Vocal Stack 1.9.0

## Scope and delivery status

**1.9.0 is built, packaged and validated: all 34 focused CTests passed in
146.69 s. Installation is blocked; actual Bitwig checks remain open.**
This engineering record does not certify installation or subjective sonic quality. The original DSP audit changed `source/src/CreativeEffects.h` and
added standalone tests/measurements. The current combined implementation also
includes routing/modulation corrections, Splitter, rotary controls with the Stack
slider pointer fix, the same-node Main Bus FX rack and shared Routing undo bridge,
and end-to-end actual granular snapshot publication/rendering.

Standalone results below retain their original scope. Final Release CLAP/VST3
builds and 34 focused CTests passed (eight factory shards, 63 editor groups), plus
separate legacy DSP in 81.8347 s. Tracked packages match Release artifacts by
SHA256; packaged CLAP smoke passed. Installation was blocked by a locked installed
CLAP: no installed files changed, so 1.8.0 remains installed. Actual Bitwig
listening/transport is unperformed; no commit/push completion is claimed.

Decisions: **repair** the existing reverb's output calibration and granular
history boundaries; **keep** their algorithms and existing normalization; repair
the compressor meter's transient gain calculation. No upstream code was copied,
no dependency was added, and no replacement algorithm is claimed equivalent to
Clouds, Freeverb, Dragonfly or Signalsmith.

## 1. Reproducible baseline, captured before DSP edits

Initial standalone measurements and the existing creative-effects regressions
were run against the untouched header first. Baseline revision:
`6833ac60718bc3cf30d626edfd47f6e373707cc0`.

* Git/LF header SHA256:
  `869ed8cf2ea3be816e4d7a52200f1a365598f22102e4030ea236b87107fb486e`.
* Original working-tree/CRLF SHA256:
  `d1d4274e4b1526034a42fc31c136df110ed8a51cb37ead3fd649c876a6b56d7b`.
  Converting the Git blob to CRLF reproduces this hash.
* Repaired header SHA256 at validation:
  `aa1ed76be01f0f101cda2708991fe2ad8519e0a072234a68187ae81673ed91c8`.

`source/tests/DspAuditProbe.cpp` is JUCE-independent. Retained data:

* `DSP-before.csv`, `DSP-after.csv`: 900 rows each, six rates
  (8/44.1/48/96/192/384 kHz), five inputs, six feature configurations,
  five **linear** dry/wet positions (0/25/50/75/100%).
* `DSP-decay-before.csv`, `DSP-decay.csv`: 18 impulse/Schroeder decay fits.
* `DSP-benchmark.csv`: existing informational effects/routing benchmark, rebuilt
  against the current tree, not a before/after CPU comparison.

All these files are in `source/tests/`. Reproduce from the project root:

```sh
python source/tests/run-dsp-audit.py --revision 6833ac60718bc3cf30d626edfd47f6e373707cc0 --measure source/tests/DSP-before.csv
python source/tests/run-dsp-audit.py --measure source/tests/DSP-after.csv
python source/tests/run-dsp-audit.py --revision 6833ac60718bc3cf30d626edfd47f6e373707cc0 --decay source/tests/DSP-decay-before.csv
python source/tests/run-dsp-audit.py --decay source/tests/DSP-decay.csv
python source/tests/run-dsp-audit.py --existing --extended
python source/tests/run-dsp-audit.py --benchmark source/tests/DSP-benchmark.csv
python source/tests/research-dsp-upstream.py
```

The Windows runner discovers MSVC using `vswhere`, builds with C++20 `/O2 /W4`,
and copies the header graph into a unique, automatically removed temporary
directory. It does not invoke CMake, change the shared build tree or install
anything. `--header PATH` can select another retained creative header for the
probe. Network access is **only** used by the optional research script, which
fetches public source at explicit hashes and prints SHA256/code excerpts. No
recorded/private audio, project content or credentials are transmitted.

### Inputs and measurement windows

Signal IDs: 0 = correlated stereo xorshift noise (seed `0x12345678`, amplitude
±0.2); 1 = independent stereo noise; 2 = correlated 997 Hz sine, amplitude 0.2;
3 = deterministic synthetic vocal-like transient (173/1091 Hz, exponentially
decaying every 370 ms); 4 = correlated unit impulse. These are test signals,
not a recording or model of actual vocals. Each render is eight seconds: input
for five seconds, then three seconds of silence. Noise/tone/transient RMS and
sample peak use seconds 2–5, excluding startup; impulse energy uses seconds 0–5.
Tail RMS uses seconds 5–8. Stereo energy is the mean of the two channel energies.
Relative dB is an energy ratio, **not LUFS/perceptual loudness**. Feature IDs:

| ID | Change from neutral/default reverb |
|---|---|
| 0 | Duck = 0; swell/saturation/pitched amount = 0 |
| 1 | Duck = 6 dB (existing default), independently |
| 2 | Swell = 500 ms, independently |
| 3 | Saturation = 12 dB, independently |
| 4 | Pitched feedback = 100%, +12 st, independently |
| 5 | Early reflections = 0, independently |

Other settings remain descriptor defaults, including frequency-dependent filters
and damping. These fixtures do not cover every possible tonal/resonant input.
`--measure ... --audio PATH` additionally writes the eight-second 48 kHz stereo
transient render (signal 3 / feature 0), interleaved float32, no WAV header. Both
before and after audio were rendered in temporary storage, compared and removed;
they are regenerable, not large binary repository additions. Observed hashes:

* Before: `8fcd622bfeb9a533179a3602e3f01b04e6674dbb0f43779c8b4fd75e6cbc6674`
* After: `a9402a63a551de26b765c0897236060728f4bbacdfd8987e0f43a3bad5caaf79`

After dividing the repaired render by four, its maximum absolute difference from
the baseline was **0.0** on this compiler/platform. This demonstrates the intended
output-only calibration, not equivalence to an upstream reverb or a listening test.

## 2. Reverb findings and repair

### R1 — high: undercalibrated wet output, independently of ducking

48 kHz results at 100% wet, relative to the dry input:

| Input | Before, neutral | After, neutral | After, default 6 dB duck |
|---|---:|---:|---:|
| Correlated noise | −15.70 dB | −3.66 dB | −8.35 dB |
| Independent stereo noise | −15.79 dB | −3.75 dB | −8.61 dB |
| 997 Hz tone | −9.33 dB | +2.71 dB | −2.24 dB |
| Synthetic transient | −11.11 dB | +0.93 dB | −3.57 dB |

The collapse is present with ducking disabled. The default duck stage independently
adds approximately 4.5–5 dB of attenuation on these sustained fixtures, consistent
with its level-dependent rational envelope law. Swell, saturation and pitched
feedback also reduce output in their separate cases; those deliberate controls
must not be mistaken for a normalization failure.

**Repair:** fixed `CreativeEffect::reverbWetCalibration = 4` (+12.0412 dB), applied
only to the final wet output. It brings the tonal/transient fixtures into a useful
nominal range, while leaving noise lower because the reverb is intentionally
band-limited. It is a conservative, finite empirical calibration at the existing
defaults, not a universal unity-RMS solution or an inverse filter. There is no
signal-following tail/noise AGC, new limiter, or concealed dry feed at 100% wet.
The pre-existing `clean()` numerical containment (±64) is not a musical limiter;
its existence does not establish headroom. No change to feedback gains, delay
times, predelay, diffusion, damping, width, early taps, duck, swell, pitch or freeze.
No descriptor IDs, ordering, host latency or parameter ranges changed.

**Behavior impact:** existing reverb patches/tails become four times larger at
the same Wet setting, including freeze and shimmer. Default duck depth remains
6 dB, not silently disabled. Output/patch gain may need reducing. The widest
100%-wet sample peak across the retained level matrix was **0.71366 FS** (44.1 kHz
tone, early reflections off); this is only a fixture result, **not** a safe bound
for full-scale input, narrow resonances, long decay, wide stereo, feedback or a
whole graph. The completed factory review below evidences no trim changes for
its modest synthetic fixture. Keep existing balances/trims; subjective real-vocal
audition and arbitrary/hot-input headroom remain separate, unchecked questions.

### Blend law: keep linear, document the dip

`RoutingExecutor::processPlan` already blends dry/wet linearly. Keep it: it has
exact endpoints and avoids adding the equal-power +3 dB midpoint gain for identical
correlated paths (e.g. neutral dynamics). No extra blend occurs inside the reverb.
The new routing test verifies exact 0%-wet dry samples and no immediate dry feed
at 100% wet, at all six tested rates. The DSP itself is wet-only.

With calibrated reverb, the 48 kHz neutral 50% blend is −4.46 dB on correlated
broadband noise, −1.17 dB on the tone and −2.66 dB on the transient. Different
correlation/filtering means neither linear nor equal-power guarantees constant
loudness. Remaining interior dips are disclosed, not covered up by a hidden dry
path, AGC, or claims that Wet now preserves all levels.

### R2 — medium/informational: nominal RT60 is not measured live decay

Code inspection: input passes through four true allpass stages per channel;
eight fractional delay lines use a Householder reflection `I − 2vvᵀ/(vᵀv)`
(`tap[i] − sum/4`). This matrix is orthogonal. Per-line feedback is
`10^(−3 * lineSeconds / targetDecay)`. The input excitation coefficient is 0.22;
output signed four-tap projections are scaled by 0.5. One-pole line damping,
interpolation and output HP/LP change the actual broadband decay. Freeze suppresses
new excitation, fades damping/modulation/pitched feedback and approaches a
0.99998 feedback gain; it is not an infinite or bit-exact sample loop.

The decay probe uses default diffusion/damping/output filters, no early taps,
modulation, duck, swell, saturation or pitch. It integrates the impulse energy
backwards (Schroeder), fits −5 to −25 dB, and extrapolates that slope to 60 dB.
Rendering lasts `max(3, 2*target)` seconds. This is broadband **T20-extrapolated
seconds**, not certified midband RT60, not a measurement of a time-varying preset,
and the short-decay fit can include the diffuser's decay.

At 48 kHz, room sizes 0/100:

| Target | Size 0 measured | Size 100 measured |
|---|---:|---:|
| 0.15 s | 0.257 s | 0.290 s |
| 2.8 s | 2.232 s | 2.325 s |
| 20 s | 14.770 s | 15.714 s |

44.1/96 kHz results are retained too. Before/after fit times were identical at
CSV precision; integrated energy rose by 16× (error under 8e−8 in the printed
ratio). Keep the algorithm, but UI descriptions must say nominal/target decay,
not measured response. Replacing diffusion/feedback to force this broadband
measurement to match one number would change the sound and was not justified.

## 3. Granular findings, repair and unchanged decisions

### G1 — medium: lookback could address only unwritten history

Repro: record 100 ms; select 1500 ms lookback, reverse, 250 ms grains; freeze.
Previously the engine addressed zero-initialized three-second storage rather than
the available recording. It had no captured-history count. New regression requires
usable frozen audio and bounded real snapshots for this partial-fill case at all
six rates. Repair tracks actual captured frames, clamps lookback to recorded data,
and reserves reverse/backward travel relative to the moving capture head.

### G2 — medium: freeze/unfreeze can invalidate an existing grain's read domain

The old launch guard only covered the state at grain birth. A live +12 st forward
grain can catch a now-frozen write head; a reverse grain can meet overwritten old
history after capture resumes. Circular indexing is memory-safe but does not make
those samples the correct captured material.

Repair uses monotonic absolute grain/capture coordinates and checks both samples
required for linear interpolation against `[oldestCaptured, captured−1)`. Only
then is a circular read performed. On leaving the valid domain after a successful
read, a grain holds its last valid stereo sample and retires over **2 ms**, under
its existing window. It never resumes by jumping into unrelated history. A grain
that has not yet read valid material emits zero, including empty frozen capture.
Frozen capture stops writing, while grain time/windows keep running. No global
low-pass/limiter was added to conceal boundary problems. A short held sample can
briefly have DC content; existing wet HP removes it. This is deliberate bounded
edge handling, not seamless looping or a zero-click promise for every waveform.
The forward-freeze regression explicitly observes boundary retirement and bounds
its fixture's consecutive-sample step to 0.08 at 48 kHz.

### Keep scheduling, windows and normalization

* 32 fixed slots, skip new launch when full, no abrupt voice stealing. Nominal
  density × length is at most 20 without scatter; jitter can fill all slots.
  Skipped launches are bounded behavior, not an unbounded queue.
* Size, signed pitch and pan are latched per birth; existing grains keep their
  lifetimes during automation. Density and parameters are smoothed, expensive
  coefficients refreshed every 32 samples; launches remain sample-based.
* Window `16 * (phase * (1−phase))²` has zero endpoint value/slope. Keep its sparse
  gaps and original isolated-grain shape, rather than dividing each isolated
  window into a rectangular pulse.
* Stereo weights remain `window*(1 ∓ 0.65*pan)`. This is stereo balance, **not**
  equal-power panning or Clouds' mono/stereo crossfeed. Keep coherent zero-spread
  mono and do not silently change the instrument's pan semantics.
* Keep the existing common max-channel window-sum normalization: floor at one,
  immediate rising-window tracking, 25 ms release. Include boundary fades in the
  actual weights. This bounds coherent buildup without dividing by a near-zero
  sparse window; it is not RMS/perceptual matching for decorrelated grains.
* Preserve linear pitch interpolation and intentional non-formant-preserving
  behavior. +12 st playback is not alias-free. No upstream resampler was added
  without a measured need/cost comparison.

The original sparse-window exact reference test, overlap/density automation tests,
pitch/reverse/freeze checks and reset determinism still pass. New partial-history
and transition tests supplement them. No startup lookahead or host latency added;
startup/insufficient-history grains can be silent or shortened at a valid edge.

### Bounded actual snapshot API — integrated live display

`CreativeEffect::GrainSnapshot grainSnapshot() const noexcept` is **audio-thread
only**, allocation-free and lock-free as a same-owner value copy. It is not itself
a concurrent publication mechanism. Never call it from the UI on a live instance.

* Fixed `std::array<Grain,32>` plus `count`; each grain carries its stable slot,
  normalized circular position, 0–1 progress, polynomial envelope including the
  boundary fade, pan, signed playback `step`, and `holdingBoundary`.
* Frame metadata: processed sample counter, captured frame count, normalized
  capture head, valid fraction, actual thresholded `frozen` and `capturing` flags.
* Coordinates/progress describe the next render sample; a retiring grain's
  position stays at the actual held read position. An as-yet unread grain's
  envelope is zero. These are DSP state, not generated decorative particles.
* Empty snapshot for non-granular/unprepared instances. Reset clears capture and
  grain history. No waveform/buffer copy, allocation, lock or GUI access.

The end-to-end bridge and rendering are now **implemented**:

1. The audio-owned `RoutingExecutor` copies active granular state through
   `VocalEngine`; `PluginProcessor` publishes once per completed host block,
   including modulated processing, plus quiescent prepare/reset. There is no
   per-sample 32-grain publication or capture-waveform copy.
2. `GrainSnapshotBridge` uses three preallocated payload buffers with exclusive
   bounded atomic ownership. Contention drops a frame instead of waiting; reads
   make at most three attempts. This is not a seqlock over concurrently accessed
   plain fields, and publication performs no allocation, lock, I/O or GUI access.
3. Frames carry stable node ID, machine generation, plan revision and reset epoch.
   Processor/UI validity checks reject removed/reused/reset identities and stale
   plans, including a cached paint before the next timer refresh. Surviving
   machines retain history through topology edits.
4. Routing and Stack reuse `CreativeEffectVisual` via `CreativeEffectPanel` in
   Device, Neon and Spectral. The display shows actual circular grain positions,
   lifetime/envelope, pan/direction, boundary holds and valid/frozen capture state.
   Hidden/clipped views skip snapshot refresh/repaint work. Publication remains
   bounded but is not subscription-gated when no editor is open.

These are sampled DSP frames, not extrapolated motion or an audibility meter;
with transport stopped, the last completed block remains visible. The granular
parameter-only schematic is replaced, not merely relabeled. Reverb and other
creative plots remain parameter models. Focused snapshot/bridge/UI checks are
recorded in `Granular-Snapshot-Integration.md`; the final 34-suite run passed.
A subsequent screenshot-output-only GrainSnapshotUi rerun passed in 0.60 s.

## 4. Remaining FX/shared-engine inventory and decisions

Severity refers to evidence here, not a claim of exhaustive certification.

| Path | Evidence / reproducible constraint | Decision / status |
|---|---|---|
| Delay | Fractional linear ring reads; sample-rate slew for time/BPM; eight beats at 20 BPM = 24 s storage; filtered passive saturated feedback; cross-feedback ping-pong. Original timing, fractional/pitch, tempo slew, maximum-delay, duck/tail tests pass. | **Keep.** Full ping-pong deliberately seeds mono into one side: anti-phase stereo input can cancel. That is the existing mono-fold semantic, not lossless stereo preservation. Dual-head pitch interpolation is not alias-free. |
| Compressor | Linked max-channel HP detector, amplitude envelope, hard/soft dB knee, no lookahead, 8-sample target update plus short gain smoothing. New analytic Nyquist-HP/static-knee transfer check covers ratios 1/4/20 and knees 0/6 at six rates. | **Keep detector/transfer; repair meter (C1 below).** Attack/release are exponential time constants, not guaranteed time to a particular gain-reduction value. No fixed-output-level AGC. |
| Gate | `UtilityFilters.h`: linked instantaneous peak, dB downward expansion, range, hold, attack-to-open/release-to-close; ratio-one unity. Existing utility suite exercises dynamics/hold/stereo/rates. | **Keep.** No detector HP is offered for Gate; do not invent one or label the compressor's sidechain HP as shared. No lookahead means the first transient can escape. |
| Bitcrusher | Symmetric `round(x/step)` quantization; optional input-gated TPDF; phase-accumulator sample/hold; drive/clamp/de-drive; wet postfilter. Silence/dither/rate/extreme and automation tests pass. | **Keep.** Intentionally aliased, not a sample-rate converter. Input-gated dither is not statistically ideal at digital silence; that exact-silence tradeoff is explicit. No arbitrary DC correction added without measured bias. |
| Waveshaper | Analytic primitives/ADAA; Raw project-rate path; optional 2×/4× IIR oversampling; DC blocker for added asymmetric/rectifying shapes; bounded level-match aid. Rebuilt tone/waveshaper tests include transfer, aliases, phase/endpoints/automation. | **Keep.** One Sine Fold alias-bin fixture reports Off/2×/4× ≈ 0.443656 / 6.39e−6 / 3.09e−6; not blanket alias-free certification. Frequency-dependent IIR/ADAA phase can comb parallel dry. |
| Chorale | Three modulated fractional delay taps/channel, 7 kHz wet filter, 0.5 tap-sum scale, warm history while bypassed; rebuilt Chorale suite passed. | **Keep.** Coherent taps can exceed unity. Direct standalone `prepare()` does not sanitize invalid sample rates; callers must supply a valid prepared rate. Source-contract hazard, not a reproduced normal-host defect; outside owned file. |
| EQ | RBJ/bilinear filters, 25 ms log-frequency/log-Q/dB smoothing, fixed 64-sample coefficient/crossfade cadence, explicit exact settled dry. Rebuilt tone suite covers coefficient/audio/response behavior. | **Keep.** No explicit delay does not mean phase neutrality. Live instance response reads still require thread ownership. |
| XP / Synth filters | XP is original TPT SVF cascade. Synth is the retained pinned Vital adaptation; existing utility test exercises its actual path, response and reference comparisons. | **Keep.** Existing Vital comparisons cover mapped paths only, not every nonlinear/modulated operating point. XP is not Bitwig code; nonlinear plots remain approximate. |
| Gain / Stereo | True mute sentinel; smoothed gain and stereo switch matrices; M/S width, balance, swap and polarity order explicit. Utility suite rebuilt. | **Keep.** Width 200% is not energy preserving. Polarity/swap crossfades deliberately pass through attenuation; not an unintended leveler. |
| Voice pitch / formant | Inspected `HarmonyShifter.h` STFT/hop/window, measured-frequency mapping, preallocation, fixed latency and independent spectral-envelope remapping; inspected VocalEngine dry alignment/bypass/merge call sites. Signalsmith reference examined below. | **Keep provisionally.** Full VocalEngine/Signalsmith backend, host latency/state and musical pitch/formant regressions were not rebuilt by this standalone header-only task. No new defect confidently established, no gratuitous rewrite. |
| Multiband / routing | Existing source/band provenance, bounded graph processing, linear effect blend, state/instance reuse and tail paths inspected. Rebuilt multiband/provenance, FX-monitor and creative-tail suites; new exact reverb endpoints. | **Keep.** Does not replace coordinator's splitter, graph Undo/state/host tests. Endpoint success is not full routing certification. |
| Meters / visuals | `Metering.h` uses 300 ms moving power RMS, sample peak, hold/max and ≥1 sample clipping. Granular now renders actual published DSP snapshots; other creative plots remain parameter schematics. | **Keep accurate labels.** Not LUFS or true peak. EQ/static nonlinear plots are not measured live transfer; reverb graphics are not measured IRs. |

### C1 — low/medium: compressor display was not the gain actually applied

Previously `gainReductionDb()` returned a separately smoothed dB target while
audio used a separately smoothed **linear** gain. These are unequal during gain
transitions. Repair reports `−20 log10(compGain_)`, before makeup, from the gain
actually used. New pulse/sustained check compares reported dB against output/input
on every sample with a 1e−4 dB tolerance. This adds a logarithm to reads; current
routing reads this meter every sample, so coordinator can decimate publication
rather than weakening correctness. No detector/audio transfer change.

### Shared safety and limits

Creative continuous controls use 25 ms smoothing and finite/clamped defaults;
coefficient updates occur every 32 samples. Granular birth-latched controls and
freeze thresholding are explicit exceptions to continuously retuning old grains.
`Ring::at/read/push` now also handle unprepared/short rings and nonfinite indices;
absolute addressing has bounded modulo fallback instead of unbounded wrap loops.
The normal granular path maps age to the current circular head before reading,
avoiding repeated long-session modulo work in the sample loop.

Allocations remain in `prepare()`; no processing/snapshot/reset allocation, free,
lock or I/O was added. Reset still clears large buffers and is bounded by prepared
capacity, not a cheap per-block operation. Approximate float buffer memory at
48 kHz, excluding object/vector metadata: delay 9.25 MB (24.1 s × two channels),
granular 1.15 MB (3 s × two), reverb 0.69 MB (line/pitch/predelay/allpass storage).
At 384 kHz these scale roughly eightfold; sixteen delay instances can therefore
be expensive. Snapshot/grain state adds only fixed-size per-instance storage.
No graph-capacity or voice-count increase was made.

## 5. Pinned upstream source inspection and licenses

These are actual source inspections at fixed revisions, not product/popularity
endorsements. `research-dsp-upstream.py` records the exact raw URLs and hashes on
request. No upstream executable was built or benchmarked here, so **no upstream
speed, memory, fidelity or sound-quality comparison is claimed**.

### Clouds — keep original engine; borrow constraints, not a partial port

Repository `pichenettes/eurorack`, revision
`f4cfa7697095a28d292bff758beb51deb49b5a86`.

Inspected `clouds/dsp/grain.h` (`Start`, `RenderEnvelope`, `OverlapAdd`),
`granular_sample_player.h` (`Play`, `ScheduleGrain`) and `audio_buffer.h` reads/writes.
Grain header SHA256:
`08feb5ac5408e2cb30eb037a844f6ee187d8dedd53fee913d1b5ab2c74ebfac3`.

Actual behaviors: 64 fixed maximum grains, sample-offset predelay within bounded
blocks, fixed-point read phases, quality-dependent interpolation/envelopes,
mono sine-table pan or stereo crossfeed, density scheduling from overlap/size,
active-grain-count normalization, and `ScheduleGrain` explicitly reserves material
consumed by both play and record heads (and reduces size for high pitch).
This supports auditing whole trajectories rather than copying just a window
formula. It does **not** demonstrate equivalence to this engine's reverse mode,
absolute-ms lookback, partial capture, stereo balance or window-sum normalization.
Dependencies include stmlib and generated LUT/resources; a coherent port would
bring scheduler/interpolator/buffer/window behavior together. Work is bounded by
voice count and rendered frames; exact desktop memory/CPU was not measured.
The inspected DSP files carry Emilie Gillet's 2014 **MIT** notice. Adoption would
require preserving notices and inspecting the licenses of every imported support
file; hardware/artwork/presets are not part of this selection. No code imported.

### JUCE / Freeverb-derived reference — keep custom feature set

Repository `juce-framework/JUCE`, tag 8.0.6, revision
`51a8a6d7aeae7326956d747737ccf1575e61e209`.

Inspected `modules/juce_audio_basics/utilities/juce_Reverb.h`: eight parallel combs
and four serial allpasses/channel, 44.1 kHz tunings scaled to sample rate, stereo
offset 23 samples, mono-summed excitation with gain 0.015, wet scale 3/dry scale 2,
width crossfeed and smoothed gains. The exposed wet level is not a universal unity
normalization either. Freeze changes excitation/damping/feedback. This does not
provide the current early-reflection, duck/swell or pitched-feedback controls as a
drop-in equivalent. Buffers allocate on rate setup; processing scales with frames
and fixed comb/allpass counts, but no comparative benchmark was run.

Pinned JUCE header is **AGPLv3 or JUCE commercial terms**, not a license grant
inferred from the historical Freeverb ancestry. No JUCE reverb code copied.

### Dragonfly / Freeverb3 — keep FDN for this repair, viable separate evaluation

Repository `michaelwillis/dragonfly-reverb`, revision
`440ec7b3b3db2fec34b9f80cf4890102ca14d1c3`.

Inspected `plugins/dragonfly-hall-reverb/DSP.cpp`,
`common/freeverb/progenitor2.cpp` and licensing files. The wrapper separately
processes early and late reverb in bounded chunks, disables each internal dry
path (`setdryr(0)`), sends adjustable early audio into the late tank, then applies
independent dry/early/late output gains. `progenitor2::processreplace` actually
contains DC removal, LFO/noise-modulated diffusion, LR crossfeed, bass-shaped
cross-loop feedback, multiple delay/allpass sections and signed output taps.
This is not an eight-line Householder substitute with identical parameters.

Freeverb3's inspected Progenitor2 source is **GPL-2.0-or-later**; its `COPYING`
contains GPLv2. Dragonfly's root license is GPLv3; attribution/transitive licenses
must be retained for an actual integration. Dependencies include Freeverb3 filter,
delay, allpass, oscillator/noise support and wrapper infrastructure. Its more
extensive network is a meaningful integration/evaluation task; exact CPU/memory
and feature mapping were not measured. Replacing the engine just to repair a
measured scalar deficit would add unvalidated behavior. No code imported.

### Signalsmith Stretch — relevant pitch reference, not a grain-engine replacement

Repository `signalsmith-audio/signalsmith-stretch`, revision
`88061fd4eda56a2353c5d65c74aa3f4ccf76f9da`.

Inspected `signalsmith-stretch.h` configuration, STFT processing, spectral prediction,
and explicit `inputLatency()`/`outputLatency()` paths, plus `LICENSE.txt`. It uses
STFT/spectral remapping and allocated configuration buffers (including preroll),
not a ready-made position/scatter/reverse/freeze granular voice API. Latency depends
on configured analysis/synthesis blocks and split computation. It is already
relevant to the voice path; it is not a zero-latency drop-in for the creative cloud.
**MIT**, Copyright 2022 Geraint Luff / Signalsmith Audio Ltd.; the project already
retains Signalsmith notices. No new code/support dependency added. No equivalence
or comparative performance claim made.

## 6. Validation observed and remaining release work

Observed successful standalone builds/runs from the current source tree:

* New `DspAuditTests`: six rate boundaries, actual interpolation/wrap and nonfinite
  indices, mono/correlated/anti-correlated/stereo inputs, min/max/invalid automation,
  reference sample stream versus irregular partitions (1/7/31/2/129/511/3),
  reset/reprepare, silence, reverb fixture/true-wet onset, partial/empty frozen
  capture, real snapshot bounds/identity, forward freeze retirement, analytical
  compressor transfer, actual GR and no processing/snapshot/reset allocations/frees.
* New `DspAuditRoutingTests`: exact dry and delayed wet endpoints at six rates.
* Existing `CreativeEffectsTests`, `ChoraleTests`, `ToneEffectsTests`,
  `UtilityFiltersTests`, `WaveshaperExpansionTests`, `CreativeTailTests`,
  `FxMonitorTests`, `MultibandTests` all rebuilt and passed. The original creative
  suite reports 51 parameters, five effects, zero audio allocations/frees; its
  freeze/pitch/swell, sparse-window and overlap tests remain intact.
* Both level matrices, both decay matrices, temporary before/after audio comparison,
  and pinned-source retrieval completed successfully.
* Current-tree benchmark completed at 48/96 kHz. Ten-effect routing used about
  **103.49 / 193.82 ms per audio second** in the static best-of-three runs, versus
  **15.55 / 30.43 ms** for its zero-added-FX graph. These synthetic runs include
  routing, not the full plugin's eight voice/Signalsmith/UI/host cost. The benchmark's
  routing “automated” flag does not automate its ten FX; don't present that as an
  adversarial full-patch automation test.
* New informational 48 kHz small-block timings include meter reads and one snapshot
  per block. One run: reverb 64-sample mean/max 23.31/62.70 µs; dense granular
  17.84/47.50 µs. OS scheduling/timer noise affects maxima. Other runs showed larger
  outliers. Neither average nor observed maximum is a real-time deadline guarantee.

The initial extended runner required C++20 for existing tests, and an early
benchmark invocation revealed duplicate headers from mixed temp/source include
paths; the runner now snapshots the complete header graph. Final runs passed.
Existing unrelated MSVC shadowing warnings in Waveshaper/Routing/test globals
remain; they were not “fixed” outside ownership.

**Implementation now complete:** processor/UI granular bridging and shared
all-theme live rendering are present, as are the Main Bus rack's shared Routing
undo bridge and routing/control corrections. Separate contributor handoffs record
focused snapshot and knob validation; older integration TODOs in those handoffs
are not the current implementation status.

### Integrated validation and factory headroom review

Final coordinator-reported results: **Release CLAP/VST3 build passed; all 34
focused CTests passed in 146.69 s**, including eight factory shards and 63 editor
groups; separate legacy DSP passed in **81.8347 s**. **33 screenshots plus 12 rack
PNGs** across three themes were visually inspected. Subsequent test-only output
added **three actual-grain PNGs**; GrainSnapshotUi rerun passed in **0.60 s**.
Root Granular/Stack FX previews are refreshed with actual snapshot/rack imagery.
Shared Undo now restores lifecycle modulation assignments while merging unrelated
live mappings/sources. All removed effect cache IDs are purged, preventing stale
tails on immediate re-add, while surviving identities retain history.

Factory evidence: `source/build/factory-preset-audio/REPORT.md`. **48 cases / 96
fresh renders passed**, with exact hashes/energy/peaks repeating; scratch 8/8 shards
passed in **109.14 s**. Eight presets × 44.1/48/96 kHz × mono/stereo, irregular
blocks, five seconds of modest synthetic vocal-like excitation and 15 seconds of
silent tail. All samples finite, none at/above 0 dBFS; active/tail audio non-silent
and final tail-second RMS below first in every case. Input peak was about -12.881
dBFS; maximum output sample peaks across presets ranged from -24.479 to -16.313
dBFS. The per-preset peak/RMS table and complete fixture are preserved in
[release validation](Release-1.9.0-Validation.md).

**No preset trim changes are evidenced:** keep -3 dB trims, Shimmer Bloom -6 dB,
and intentional balances. Reverb calibration scales its wet branch, not the whole
preset. This completes reproducible fixture headroom review, not subjective
listening, true-peak analysis or hot/arbitrary-input certification. Production
CMake registers all eight factory shards, which also passed in the final
34-suite run; this is distinct from the earlier scratch pass.

The registered informational performance probe completed its full matrix; heavy
small-block and multiple-instance deadline overruns are disclosed in the release
record, not a CPU acceptance pass. UI median/p95/max was **7.990/12.903/41.000 ms
compact**, **13.448/20.044/44.231 ms expanded**, at most 12 mounted cards. Those
message-thread timings are not audio CPU or a real-time guarantee.

**Packaging complete:** Release artifacts copied to tracked CLAP/VST3 packages
and SHA256-matched; packaged CLAP smoke passed 2087 parameters, mono/stereo
latency, bypass and automation. Exact package hashes are in the release record.
**Install attempt blocked:** installed CLAP locked by another process; no installed
files changed and **1.8.0 remains installed**. Retry when unlocked, never by
terminating Bitwig. **Actual Bitwig listening/transport, automation, recall and
sustained playback/dropout checks remain unperformed.** No commit/push completion
or commit hash is claimed. This documentation update ran no builds/tests, changed
no packages and made no commit or push.
