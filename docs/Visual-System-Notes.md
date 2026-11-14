# Reactive material — original source handoff

**Readability revision:** the 48-column composition described below is historical.
Current presentation retains all 128 telemetry windows/bands and draws connected
waveform geometry at approximately 1.5 px horizontally / 1 px vertically (bounded
at 1024 display columns), with 1 px three-spectrum traces. The decorative raster
stays 48 × 24; no global material texel size or audio analysis changed. Display
interpolation is not additional measured detail. See the final readability section
of [Preview-1.11.0.md](Preview-1.11.0.md) for current tests, timings and artifacts.

**Integration update:** the handoff below was originally unvalidated. The
coordinator has now integrated and Release-built it, registered the tests and
captured all-theme actual-PCM GIFs. See [the 1.11.0 preview record](Preview-1.11.0.md)
for observed results and the unskipped Windows native-click blocker. The original
agent's scope/provenance below is retained; its statements about not building
refer to that agent's handoff, not the current integration status.

This note supplements (does not replace) `Preview-1.10.1.md`. The older preview
records the previous render and validation results, **not validation of this
renderer revision**. No build, test execution, screenshot/render, install,
packaging, version bump, commit or push was performed by the visualization agent.
The coordinating agent owns all compilation, test registration and visual review.

## Source ownership and behavior

Only `source/src/CreativeEffectVisual.h`, the new
`source/src/ReactiveFxVisualModels.h`, `source/src/ReactiveMaterial.h`, the new
`source/tests/ReactiveMaterialTests.h`, and this note were written for this task.
Existing uncommitted 1.10.1 telemetry, routing, tests and documentation were
preserved. No processor/DSP/parameter/telemetry API changes are required.

Reverb, Convolution and Saturator now use one composition path. The existing
convolution and saturation switch bodies became small guide methods; unrelated
models, EQ and the stable granular graph are unchanged. Reverb retains its
pitch/onset/RT60 guides; Convolution retains its generated IR-envelope guide;
Saturator retains its technical transfer curve. These remain parameter models,
not measured responses. Guides and text are drawn after the material.

`reactiveVisual::WaveformField::from(frame, active)` consumes the existing
`FxMonitorFrame::history` only. It reduces 128 consecutive 20 ms bins to **48
fixed columns**, retaining signed extrema and averaging linear RMS power within
each group before converting to the fixed -60..0 dBFS scale. No random source,
automatic normalization, oscillator, UI envelope follower, fake decay or sample
interpolation is introduced. Unused bins remain at the left; new audio enters
at the right and advances toward the left. NaN/infinity and amplitudes at/below
-60 dBFS are empty; amplitudes above 0 dBFS clamp to the field edge.

`reactiveVisual::ReactiveMaterial` owns a **48 x 24 RGB composition image** and
an existing `FluidBackground` instance whose raster is also fixed at 48 x 24.
Fluid pixels are drawn into the composition image first; occupied waveform
cells tint those pixels rather than covering them with an opaque rectangle.
Both channels share a stable central zero baseline: faint palette accent input,
stronger palette secondary processed-wet output. Stepped peak rims and RMS cores
remain connected through the zero line, with no particles or detached heads.
When the plot covers fewer raster columns than the model, overlapping columns
are conservatively max-pooled (including RMS-core height) so narrow peaks are
not skipped. The background and audio are scaled once with nearest-neighbour
sampling, while foreground guides remain crisp vector strokes.

Existing subscription/visibility handling, bounded 30 Hz refresh, 250 ms stale
publication expiry, slot bypass/Mix suppression and reset behavior are retained.
The renderer has no timer, telemetry subscription or processor pointer. It does
not retain previous audio. Clearing the model clears the audio immediately on
next paint; continuous real silence scrolls the measured history away. The
existing decorative phase controls only the underlying fluid material: Motion 0
still permits fresh measured history to change the image.

## Reuse for SpectralMorph

Include `ReactiveMaterial.h`. No dependency on the SpectralMorph implementation
or processor additions is introduced here.

- `reactiveVisual::SpectrumField` contains three arrays of 48 normalized levels.
- `field.set(spectrumIndex, column, linearMagnitude)` maps actual caller-provided
  magnitude to the same fixed -60..0 dB scale. Out-of-range indices are no-ops.
- Call `material.paintSpectrum(g, bounds, field, baseColour,
  std::array<juce::Colour, 3>{inputColour, targetColour, outputColour}, phase)`.
- One material instance per view; clear/rebuild `SpectrumField` when telemetry is
  invalid, silent or expired. Supply actual spectra only, no animation fallback.
- Caller owns FFT/band reduction, validity/expiry, axis labels and crisp guide
  overlays. The helper does not imply that bins are linear/log frequency; caller
  defines the frequency mapping and labels it. The compositor sanitizes/clamps
  directly supplied normalized levels as well.

The waveform entry point is `paint(g, fullBounds, fieldPlotBounds, waveform,
{base, input, output, baseline}, decorativePhase)`. Header/footer pixels remain
fluid-only, and guide overlays are caller-owned.

## Coordinator integration and validation

New headers are header-only and require no extra compilation unit or dependency.
Register tests in the existing editor harness (visualization agent deliberately
did not edit `EditorTests.cpp`, CMake or any existing tests):

```cpp
#include "ReactiveMaterialTests.h"
// Register a group using the harness's existing mechanism:
vocalstack::reactiveMaterialTests::run();
```

Optional separate groups are `reactiveMaterialTests::model()` and
`reactiveMaterialTests::composition()`. The latter makes pure offscreen JUCE
images only, not native display captures. Tests cover fixed dB scaling, newest
right alignment, signed/input/wet separation, unused history, inactive/no-wet
clearing, oldest transient retention, nonfinite guards, bounded raster dimensions,
repeatable frozen material, audio changes with frozen decoration, wet prominence,
no header coverage, clear-after-active, and three-spectrum composition/clearing.
These tests use synthetic model fixtures; the existing `MeasuredFieldUiTests.h`
remains responsible for **actual Processor PCM**, wet tails, Freeze, Motion 0,
expiry, bypass and reset. Do not replace its real-audio assertions with these
model fixtures.

Compile risks to check centrally: JUCE image/BitmapData overload compatibility,
header inclusion in every plugin/editor target, and integration of any concurrent
new effect enum/switch branches. No successful compilation is claimed. Review
all three effects in all three themes, active/silent/late-tail, narrow and expanded
layouts. Check field contrast against the thin guides and newest-right direction.
Run the existing measured-field and granular regressions as well as the new test
groups. Use the prescribed HalfCpu wrapper for all builds/tests/renders; native
review requires freshly enumerated display placement per `AGENTS.md`.
