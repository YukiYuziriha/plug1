# Shared automatic tuner DSP

Header-only implementation: `source/src/AutomaticTuner.h`.
Settings: `source/src/TuningSettings.h`.
Donor/license audit: `ThirdParty/MXTune/README.md`.
No existing engine, processor, UI or CMake file was edited. This directory's
CMake project builds only the standalone tuner test against the existing
pinned Signalsmith checkouts in `source/build/_deps`; it downloads nothing.

## Interface / integration contract

```cpp
#include "AutomaticTuner.h"

vocalstack::AutomaticTuner tuner; // Non-copyable/non-movable; stable address.
vocalstack::TuningSettings settings;
// Off audio, with processing quiescent:
tuner.prepare(sampleRate, maxBlock);
int delaySamples = tuner.latency();

// Single audio-owner. Mono/stereo channel counts 1 or 2. Channel-for-channel
// in-place supported; input and output arrays contain at least channelCount pointers.
tuner.process(inputPointers, outputPointers, count, channelCount, settings);
// Mono convenience overload:
tuner.process(monoInput, monoOutput, count, settings);
// Same audio-owner snapshot, NOT a concurrent UI getter:
auto pitch = tuner.telemetry();
// No allocation; preserves preparation and latency, clears all signal history:
tuner.reset();
```

- `prepare(double sampleRate, int maxBlock)` accepts 8--192 kHz and positive
  block sizes, throws `std::invalid_argument` for invalid arguments. It is the
  only allocating method. Larger-than-prepared blocks are supported with
  bounded internal chunks. Do not call prepare concurrently with processing.
- `process` and `reset` perform no allocation, locking, I/O or GUI access.
  Nonfinite inputs become silence; pathological inputs are bounded to +/-32
  before squaring. Null input/channel pointers are silence; null output channel
  pointers are skipped. Non-positive lengths do nothing.
- Enable/bypass and true dry/wet mix belong **outside** this wet-only DSP. Keep
  feeding the tuner while bypassed if tracking must remain warm. Feed it the
  common lead signal before voice fan-out. Delay dry by `latency()` when mixing.
- Its 1344-sample delay matches the legacy engine at 44.1/48 kHz. **Serially
  inserting it before the existing lead shifter still adds their delays.**
  Integration must replace/share a latency stage or explicitly account for the
  sum; do not assume standalone latency compatibility makes serial insertion
  free. No engine integration is performed here.
- Detection does not add audio delay but needs >=64 ms power-of-two history:
  4096 samples at 48 kHz (85.33 ms), 4096 at 44.1 kHz (92.88 ms). First lock is
  on the next ~5 ms analysis tick. `detectorWindowSamples()` and
  `detectorHopSamples()` expose the exact prepared values. Steady-state pitch
  estimates refer roughly to the middle of that history; sudden note changes
  have tracking lag. Hard-retune zero does not eliminate this or STFT smearing.
- Telemetry fields: `detectedHz`, `detectedMidi` (A4=69 at referenceHz),
  `confidence` [0,1], `targetMidi`, `correctionSemitones`, `voiced`, `hasTarget`.
  Read `targetMidi` only when `hasTarget`; correction is the applied command,
  not an independent measurement of rendered audio. Copy/publish telemetry
  through the processor's own thread-safe bridge.

## Controls

`AutomaticTuner::Settings` aliases `TuningSettings`. Pass a value snapshot on
each process call; controls are sampled on the internal 32-sample clock.

| Field | Meaning |
|---|---|
| `root` | C=0 to B=11; normalized modulo 12 |
| `scale` | `TuningScale::{Chromatic, Major, NaturalMinor, HarmonicMinor, MajorPentatonic, MinorPentatonic, Custom}` |
| `allowedNotesMask` | Absolute pitch classes, bit 0=C, bit 11=B; intersected with rooted scale; Custom uses mask alone |
| `referenceHz` | A4 calibration, 400--480 Hz; default 440 |
| `vocalRange` | `TuningVocalRange::{Wide, Low, Mid, High}`: 55--1200, 55--400, 80--800, 150--1200 Hz |
| `amount` | 0--1 fraction of semitone correction; default 1 |
| `retuneMs` | Error smoothing time constant, 0--2000 ms; 0=hard |
| `transitionMs` | Target-note glide time constant, 0--2000 ms; 0=hard |
| `stability` | 0--1, up to 40-cent hysteresis and 30-ms candidate dwell; default 0.25 |
| `preserveFormants` | Real Signalsmith spectral-envelope compensation, default true; approximate at this short window |

Empty effective masks release correction to unity; they do not silently select
chromatic. Exact quantization ties select the lower allowed note. Disallowing
the currently held target switches immediately rather than dwelling on a
forbidden note. Unvoiced input releases correction over 5 ms. Gain/mix, enable,
UI/state/host parameter IDs are deliberately absent. Settings sanitization and
quantization helpers are available without constructing a tuner.

## Validation

From the repository root, using a dedicated 64-bit PowerShell process:

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 powershell.exe -NoProfile -ExecutionPolicy Bypass -File source/tests/tuning/Run-Tests.ps1
```

The runner imports the MSVC x64 environment, configures the **new isolated**
CMake project under `source/build/tuning-isolated`, builds Release, and runs
CTest. `-VcVars` overrides the installed MSVC environment batch path. Every
child inherits the wrapper's CPU affinity and parallelism limits. The existing
plugin CMake project and packaged binaries are untouched.

Tests cover scale/root/mask sanitization, A4 calibration, all four smoothing/
stability controls, hard retune, empty masks, a 26-tone C2--C6 accuracy sweep,
sample rates 8/22.05/44.1/48/88.2/96/192 kHz, impulse latency, mono/stereo,
anti-phase channels, stereo-image coherence, irregular and oversized/in-place
blocks, partition invariance, reset determinism, silence/DC/noise, finite
output, level/headroom, and a synthetic harmonic-vowel envelope comparison.
Global ordinary/array/aligned allocation guards cover processing and reset,
including initial signal processing, formant toggles and automated settings.

Observed MSVC Release result: all checks passed; zero processing/reset
allocations; identical fixed-setting output across tested block partitions;
worst sweep output error 6.48 cents. The current high-overlap implementation
cost about 0.217x real time at 48 kHz stereo on the development machine, roughly
22% of one logical core (informational, includes test checks). This is an
accuracy/latency vs CPU compromise, not a lightweight transparent substitute
for the engine's subtle doubler. Signalsmith dependency headers emit existing
MSVC conversion/shadow warnings under /W4; no donor/dependency code is patched.
No plugin build, host/PDC integration, real-vocal listening check or GUI test
is claimed. These are purely offline DSP tests with no display placement.
