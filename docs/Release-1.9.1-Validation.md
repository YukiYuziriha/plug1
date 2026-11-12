# Release 1.9.1 — built, validated and packaged; installation pending

## Status and evidence boundary

CMake already declares **1.9.1**. Current source/test diffs were reviewed for this
record; no build, test, packaging, installation, commit or push was performed by
this documentation task. Execution results below are coordinator-reported in the
task handoff, not a newly observed run.

- **All 34 focused CTests passed in 154.15 s**, including **66 editor groups and
  eight factory-audio shards**. This supersedes the first 33/34, 63/66 run.
- Three test assumptions were corrected: oversized Tone text is checked through
  scroll slices; Delay uses `refreshAt` for deterministic animation and exact
  snapshot comparisons; EQ assignment uses Alt-wheel.
- A final minor fix removed the duplicate WaveMix control. The subsequent **full
  Release build succeeded**, followed by **4/4 targeted Editor/rack/grain UI/CLAP
  passes in 81.50 s**. Editor passed **66/66 groups in 81.27 s**. The 34-suite run
  preceded this final fix; the targeted run validates the rebuilt revision.
- **Packaged CLAP smoke passed:** 2087 parameters, latency, bypass and automation.
  Tracked CLAP/VST3 packages match Release artifacts by SHA256 (below).
- **Installation is blocked**, not complete: the installed CLAP is in use by a
  process. No installed files changed; **1.9.0 remains installed**.
- Commit/push is pending with the coordinator; no completed push is claimed.

## Implemented UI scope

- **Rack/Stack:** horizontal 204 px FX cards, near the 172 px voice-card width,
  expose all parameters through full shared UIs with internal vertical scroll and
  fixed headers; no Details step. Reordering is horizontal. Stack keeps a native
  940x1148 body in a vertical viewport, a fixed preset footer and the 940x1104
  default window instead of shrinking the complete UI to fit.
- **Hover wheel:** shared `PassiveViewport` forwarding chooses the nearest
  vertical ancestor able to move, propagates outward at edges, and uses horizontal
  fallback only when no vertical range exists. Shift-wheel requests horizontal
  motion. Main value labels also forward navigation without parameter edits or
  focus capture. Embedded EQ plain wheel scrolls; Alt-wheel edits Q or assignment
  depth with the existing gesture contract. Standalone graphs retain plain-wheel Q.
- **Granular:** the actual DSP snapshot becomes a valid-capture/read-region cloud,
  not 32 fixed empty rows. Sparse/dense views retain stable slot identities,
  envelope/lifetime and direction cues; no extrapolated grains are invented.
- **Delay:** parameter-timed animation illustrates timing/feedback, not measured
  echoes or host transport. Its clock pauses when hidden/clipped or bypassed;
  offscreen Sync changes still update Time/Beats control enablement.
- **Filters:** XP cutoff/resonance knob edits refresh immediately from one coherent
  target snapshot. The utility timer is 60 Hz, formerly 20 Hz. Synth removes the
  100 ms request throttle while preserving the actual 32768-sample DSP-derived
  response and shared bounded worker; there is no alternate fake preview.

This is a presentation/test patch: **no audio DSP changes**. Same-node routing,
parameter bindings and host parameter compatibility are retained.

## Coverage and visual/performance checks

`EditorTests.cpp` now registers **66 editor groups**, adding hover-wheel,
voice-sized Tone-card/EQ scrolling and immediate-filter/bounded-scheduler groups.
`MainBusFxRackUiTests.cpp` includes **354 real-hit-tested wheel cases**, checking
actual rendered surfaces and navigation boundaries without parameter, gesture,
focus or routing/layout mutations. Granular/Delay coverage also exercises real
snapshots and deterministic animation. Passing run boundaries are recorded above.

**Screenshots:** 33 rendered, plus rack-row views in Device/Neon/Spectral. Row and
grain/Delay views were inspected; root images were updated. This is not a DAW
listening check.

**Informational UI profile:** 26 nodes, 120 frames; at most 12 mounted cards.

| View | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: |
| Compact | 9.081 | 13.008 | 49.161 |
| Expanded | 14.755 | 22.607 | 37.176 |

These timings are not a native presentation FPS or glitch-free guarantee. Actual
Bitwig listening and native FPS remain unchecked. No audio DSP changed, so the
separate legacy DSP suite was **not rerun**; its 1.9.0 pass is historical only.

## Package identities and installation

Both tracked package binaries SHA256-match the final Release artifacts:

| Package binary | SHA256 |
| --- | --- |
| `Windows-x64/CLAP/Vocal Stack.clap` | `b4f2d6d9387aefae4ee5338e3da41b2d33ca4c1c980ee177114841e18daaa1ce` |
| `Windows-x64/VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3` | `3aebefa834be8f9d9fa919239ff35d17ab628cd543e45036c5b4589ad66a91a4` |

**Installation attempt blocked:** the installed CLAP is being used by a process.
No installed files changed; **1.9.0 remains installed**, despite 1.9.1 packages
being ready. Never terminate Bitwig or overwrite a loaded plugin to force an update.

Remaining work:

- [ ] Retry installation after the host closes and verify installed hashes.
- [ ] Coordinator commit/push; not yet complete in this record.
- [ ] Actual Bitwig listening, transport, automation/session recall, sustained
  playback and native FPS checks; automated coverage is not host validation.

The [1.9.0 validation record](Release-1.9.0-Validation.md) remains unchanged history;
its package hashes, installation and push are not 1.9.1 delivery results.
