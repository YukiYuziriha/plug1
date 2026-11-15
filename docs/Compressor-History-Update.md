# Compressor / Gate measured history — integrated delivery

**Delivered in the approved installed 1.11.0 release.** See
[final package/install validation](Release-1.11.0-Validation.md). Owner-only
handoff instructions below are preserved as historical integration context.

**Coordinator integration completed:** all exact producer fields below are now
present. Per-sample actual DSP GR feeds a dedicated 200-bin peak ring, independent
of the unchanged 128-bin PCM history and `hasWet`. Publication uses the same
exclusive snapshot ownership; bypass/generation/kind/reset/rate invalidation
clears stale history. `actualDspSnapshotBridge()` and the registered Compressor
History group pass, including actual routed compressor attenuation checks.
Gate's separate measured RMS/threshold/mean-GR payload is also integrated.
See [current validation and all-theme review GIFs](Preview-1.11.0.md). The original
owner-only handoff below is historical; no full native desktop/FPS pass is claimed.

## Required telemetry integration — FxMonitor owner

Consumer work is confined to CreativeEffectVisual.h, ReactiveFxVisualModels.h,
ReactiveMaterial.h and a new test header. Do not change existing 128-bin PCM
history or the three existing reactive effects' scale/appearance.

Please add these fields to `FxMonitorFrame` (names are the consumer contract):

```cpp
static constexpr size_t gainReductionHistoryCount = 200;
std::array<float, gainReductionHistoryCount> gainReductionHistory{};
size_t gainReductionHistoryUsed = 0;
size_t gainReductionHistoryBinSamples = 960;
float gainReductionDb = 0;
bool hasGainReduction = false;
```

- Publish actual `CreativeEffect::gainReductionDb()` / Gate
  `UtilityEffect::gainReductionDb()` measurements, positive attenuation in dB,
  before makeup and slot Mix. Never estimate from input/output PCM.
- Each completed bin holds the **maximum applied GR** over 20 ms, with bin length
  `round(sampleRate * .020)`. 200 bins are nominally four seconds. Publish ordered
  oldest-left/newest-right and right-align partial history, like existing PCM bins.
- `gainReductionDb` is the current measured attenuation in the same snapshot.
  `hasGainReduction` identifies a valid compressor/Gate capture, including silence
  and unity attenuation; it is independent of `hasWet`.
- Keep capturing GR even where PCM `hasWet` is false (Gate). Set validity false
  for unrelated effects. Clear GR history on subscription generation, node/kind,
  bypass, reset, sample-rate/plan changes; use existing sequence/epoch coherence.
- Accumulate on the audio thread, bounded and allocation/lock-free. Read the
  actual per-sample DSP gain in Routing's existing measurement location; do not
  merely append the latest atomic scalar at GUI refresh rate. Existing
  `setGainReduction` may feed accumulation if it is called once per sample before
  `capture` and validity/reset are explicit.

Current consumer uses a compile-safe optional-field adapter: until **all** fields
above exist, it shows unavailable history, never fabricated history. Completion
requires the telemetry owner to provide these fields and the coordinator to run
DSP-to-snapshot integration checks. This is not a passing placeholder assertion.

## Shared rendering API — Utility/Gate owner

`reactiveVisual::GainReductionField::from(frame, active)` maps the snapshot;
`reactiveVisual::HistoryField::paint(g, bounds, field, colours)` is the shared
renderer in ReactiveMaterial.h. Own one HistoryField per component. Colours is
`{background, grid, trace, text}`. Pass the complete lane bounds: this primitive
owns the border, plot inset, ticks, time labels and clipped stepped trace. Call
with the same bounds/colours/data for pixel-identical Compressor/Gate graphs.

Fixed semantics: nominal **4 seconds**, old left / now right, **0..36 dB GR**,
positive attenuation drawn downward from the top. The trace is peak attenuation
per completed 20 ms bin, not an envelope estimate or sample-accurate waveform.
Values above 36 dB clip visually, but the numeric current meter remains unclipped.
Unused/invalid bins are blank, not invented unity history. Nonfinite bins break
paths. No global flashing, auto-scaling, animation clock or random waves.

## Validation ownership

No builds/tests run by this agent, per coordinator request. Coordinator must
integrate `source/tests/CompressorHistoryTests.h` in the existing test runner;
entry point `vocalstack::compressorHistoryTests::run()`.

Exact test functions:
- `measuredMappingAndScale()` — dB/downward scale, right alignment, invalid/stale
  clearing, nonfinite gaps and 44.1/48/96 kHz timebases.
- `sharedPixelsAndCache()` — identical Compressor/Gate offscreen pixels, stable
  repeated paints, sequence/epoch/size invalidation and clearing.
- `actualDspSnapshotBridge()` — real compressor processing through existing
  `FxMonitor::setGainReduction` + `capture`, requires measured GR in the published
  snapshot and reset invalidation. This deliberately **fails** while the producer
  contract is absent: do not skip it or report consumers alone as completion.

The existing latest-value DSP meter is retained as a compatibility fallback while
snapshot history is unavailable. It is never appended to GUI-generated history.
History itself uses coherent snapshot measurements only. The producer can retain
its existing public `setGainReduction`/`capture` call signatures for this test;
if it needs a new explicit validity argument, update this new test header during
coordinator integration (no existing test runner was edited here).

## Consumer implementation / preserved behaviour

- Compressor owns a top measured history lane and lower technical transfer curve;
  numeric GR and 0..36 dB meter remain, with unclipped numeric values. History is
  pre-Mix, so Mix 0 does not falsely erase measured internal compression. Bypass,
  hidden subscriptions and stale snapshots do clear it.
- Shared HistoryField caches trace/fill geometry by snapshot sequence, epoch,
  node, actual content, validity and bounds. Parameter guide paths cache coherent
  parameter values, enabled state, Mix and bounds. Measured waveform mapping
  caches telemetry revision/epoch/node and active state. Theme changes recolour
  cached geometry without touching measurements.
- Existing Reverb/Convolution/Saturator decorative raster, measured waveform
  scales and guide shapes are unchanged. No global brightness pulse added.
  Granular's stable parameter-envelope model and hierarchy/clipping visibility
  checks are unchanged.

Files changed by this task only:
`source/src/CreativeEffectVisual.h`, `source/src/ReactiveFxVisualModels.h`,
`source/src/ReactiveMaterial.h`, `source/tests/CompressorHistoryTests.h`,
`docs/Compressor-History-Update.md`.

Lightweight validation: scoped `git diff --check` passed. Builds, tests, native
GUI review, packaging, installs, commits and pushes were not run. No version bump
or edits to EditorTests, Processor, Routing, FxMonitor, CreativeEffects or Gate.
