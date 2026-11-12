# Actual granular snapshots — integration and validation

## Implemented

The parameter-only granular schematic is replaced by actual DSP snapshots in
`CreativeEffectVisual.h`. Routing's inspector and Stack's expanded creative FX
panel instantiate this same visual through `CreativeEffectPanel`. The bridge,
rendering and test integration are finalized for 1.9.0.

- `RoutingExecutor::grainSnapshot()` is audio-owner-only. It traverses at most
  48 active-plan steps and copies at most 15 granular nodes, each with the DSP's
  fixed 32-grain payload. It never reads pending/retired DSP instances.
- Each node carries its stable routing ID and monotonic machine generation.
  Accepted plans have monotonic revisions; reset/prepare changes reset epochs.
  Requested revision and reset epoch are atomically readable without exposing
  mutable DSP. All removed effect IDs, not only granular identities, are erased
  from the control-side weak machine cache, including removal/re-add before audio
  installs removal; old tails cannot resurrect through that cache.
  Surviving granular machines retain history/generation through topology edits.
- VocalEngine exposes only the owner copy and atomic validity metadata.
  Processor publishes once at the end of each host block (also on its modulated
  chunking path), plus quiescent prepare/reset. There is no per-sample grain copy.
- `GrainSnapshotBridge` follows FxMonitor's ownership pattern: three preallocated
  payload buffers, exclusive bounded CAS claims, release/acquire handoff and a
  sequence-bearing latest token. Readers and writer never concurrently access a
  plain payload. No mutex, retry-until-success loop, allocation or UI call occurs
  in publication. A busy buffer drops the frame; reads attempt at most three
  times. Multiple views/readers are supported, not just a single SPSC consumer.
- Processor rejects frames from older requested plans or reset epochs. Empty
  non-granular slots are rejected and failed UI reads clear the cached display.
  Paint rechecks cached validity using atomic metadata: reopening a hidden view
  before the timer refresh cannot paint an old removed/reused instance.
- Display: actual circular capture positions, stable grain-slot rows, lifetime
  progress rails, envelope-scaled marker heights/opacity, signed playback arrows,
  pan colours and boundary-held rings. Metadata includes active/reverse/held
  counts, actual frozen/capturing state, capture head, valid circular capture
  range and processed sample counter. No parameter-derived or animated fake
  grains, extrapolation or fabricated waveform. DSP wet history continues to
  clock while bypassed, as before; this is an engine snapshot, not an audibility
  meter. With transport stopped, the last completed block remains a snapshot.
- Device, Neon and Spectral use the same data/model and inherited theme palette.
  Hidden panels skip timer updates. Visual refresh checks visibility and ancestor
  clipping before reading any snapshots or requesting repaint (including viewport
  scrolling). Timers still make their inexpensive visibility check at 20 Hz.

Publication is bounded but not subscription-gated: all active granular nodes are
copied once per completed host block, even with no editor. The UI does no hidden
snapshot/model refresh. No waveform/buffer copying is implemented.

## Registered tests

`source/CMakeLists.txt` registers `VocalStackGrainSnapshot` and
`VocalStackGrainSnapshotUi`. No manual registration step remains.

`GrainSnapshotTests.cpp` covers 44.1/48/96 kHz, irregular block sizes,
exact same-input direct DSP-vs-routing grain fields, bounded coordinates,
reverse, actual freeze/capture hold, reset epoch/empty history, surviving machine
identity, type replacement, reused slots and immediate pending removal/re-add.
A thread-local allocation guard checks ordinary C++ new/new[] during processing
and snapshot copies; aligned/custom allocators are not intercepted.

`GrainSnapshotUiTests.cpp` stresses 19,999 publications against two concurrent
readers and checks complete correlated payloads, revision/reset rejection,
Processor-to-UI actual grains, pending-plan invalidation, slot reuse, reset,
identical cross-view images in each theme, distinct Device/Neon/Spectral renders,
clipped refresh doing no model work and invalidated-cache paint before timer.

## Observed validation

2026-11-12: private Release/Ninja/MSVC build under `.cache/grain-validation/build`.
Existing dependency source trees were read from `source/build/_deps`; no shared
build outputs were written. Current Processor, Engine, Editor and JUCE code were
compiled in the private tree. No packaged CLAP/VST3 targets were built/installed.

Passed:

- `GrainSnapshotTests.exe`
- `GrainSnapshotUiTests_artefacts/Release/GrainSnapshotUiTests.exe`
- Private CTest `VocalStackCreative`
- Private CTest `VocalStackFxMonitor`
- Private CTest `VocalStackRoutingBackend`

The two new suites passed strengthened allocation/theme/cache assertions. Final
Release CLAP/VST3 builds and **all 34 focused CTests passed in 146.69 s**, including
eight factory shards and 63 editor groups; separate legacy DSP passed in 81.8347 s.
The prior 33 screenshots plus 12 rack PNGs across Device/Neon/Spectral were visually
inspected. A subsequent test-only screenshot-output addition passed the
**GrainSnapshotUi rerun in 0.60 s**, producing **three actual-grain PNGs**. The root
`VocalStack-Granular.png` is refreshed with the actual DSP snapshot. Concurrency
stress is not a ThreadSanitizer proof; rendered review is not DAW listening.

Tracked packages were copied from Release artifacts and SHA256-matched; packaged
CLAP smoke passed 2087 parameters, mono/stereo latency, bypass and automation.
**Installation blocked:** installed CLAP locked by another process; no installed
files changed and 1.8.0 remains installed. Actual Bitwig checks and installation
retry remain open. Commit/push is coordinator-owned; no completion or hash is
claimed. See [release validation](Release-1.9.0-Validation.md) for package identities
and performance caveats. This documentation update ran no builds/tests or commit.
