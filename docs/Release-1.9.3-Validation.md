# Release 1.9.3 — built, validated, packaged and installed

## Status and evidence boundary

**Release build, focused tests, package refresh, packaged CLAP smoke and both
per-user installations are complete; commit/push remain pending.** After the final
rack-owned `PanelStyle` fix, the coordinator reports a successful full Release
build and all **34 focused CTests passed in 176.42 s**, including **67 editor groups
and rack UI**. Independent static review found no blockers. Image review confirmed
that compact tab **On** text is now readable.

**1.9.3 is installed**;
the [1.9.2 record](Release-1.9.2-Validation.md) remains historical evidence for that
release, not validation of 1.9.3. Actual host checks remain unverified, and the long
legacy DSP suite was not rerun.

## Implemented scope

- Main Bus FX tabs replace simultaneous compact cards. Each tab represents the
  real main-bus Routing node, not a duplicate processor. Clicking selects one
  full-width editor.
- Drag tabs to reorder using Routing's shared one-step Undo/Redo path. Per-tab
  On/bypass and remove controls and the + FX add workflow remain available.
- Surviving node/editor instances and scroll state are retained across selection
  and reorder. Overflow automatically reveals the selected tab, including after
  selection or resizing.
- Fixed compact knobs reflow responsively rather than stretching to fill wide
  panels. Numeric values sit above knobs and names below. Mix controls use
  horizontal sliders; two-state controls stay compact.
- Wide Creative, Tone and Utility editors place the graph on the left and controls
  on the right; narrower layouts stack the graph and controls.
- The rack owns its `PanelStyle` for standalone tab rendering, fixing the compact
  tab On ellipsis/flat-theme issue; the final build, tests and image review cover it.

This is **UI-only**: no audio DSP or host parameter IDs/counts, ranges or defaults
change. The existing granular parameter illustration remains a parameter guide,
not live grain telemetry or a recorded waveform.

## Observed validation sequence

1. Release build succeeded.
2. Initial editor tests passed **61/67 groups**. The Utility layout mistook hidden
   placeholder sliders for visible Mix controls, stretching two-state buttons.
   Existing tests also expected the previous larger buttons and selector geometry.
3. Fixed layout classification and updated outdated geometry assertions; the
   editor rerun passed **67/67**.
4. The full focused CTest run passed **34/34 in 177.04 s**, including the editor
   groups and rack UI.
5. Independent static review found **no blockers**.
6. Image inspection prompted the final rack-owned `PanelStyle` change for
   standalone **On** ellipsis/flat-theme rendering. The subsequent full Release
   build succeeded and all **34 focused CTests passed in 176.42 s**, including
   **67 editor groups and rack UI**. This is the final reported test pass.
7. Reviewed rack Reverb Neon, EQ Neon, Granular Spectral and Routing Reverb images;
   compact tab **On** is now readable. Added tracked `VocalStack-FX-Tabs.png`, linked
   near the top of the README. Image review is not actual DAW approval.

## Packages and installation

Release CLAP/VST3 were copied to tracked `Windows-x64`. Package binary SHA256:

| Package binary | SHA256 |
| --- | --- |
| `Windows-x64/CLAP/Vocal Stack.clap` | `463ee0d3ee36c03df085ef24ecd47f1ec76baa6dfb862dcca00b1329644b5d83` |
| `Windows-x64/VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3` | `29f52f8d632dd39f53cf14d51dd070c65f264abd221e1ae0b6627dd08b1f474c` |

**Packaged CLAP smoke passed:** 2087 automatable parameters at their original
indices, stereo at 48 kHz, mono-to-stereo at 44.1 kHz and 1344-sample latency.
The coordinator ran `source/build/install-release.ps1`; **both per-user CLAP and
VST3 reported Installed and SHA256 verified**.

## Delivery checklist and remaining host checks

- [x] Full Release build and all 34 focused CTests after the final style fix.
- [x] Review final images, including readable compact tab On text.
- [x] Refresh tracked Release CLAP/VST3 packages and record SHA256 identities.
- [x] Run packaged CLAP smoke.
- [x] Install both per-user formats and verify SHA256.
- [ ] Commit and push the validated release with current packages.
- [ ] Actual Bitwig visual/listening, transport, automation/session recall and
  native FPS checks; automated passes are not host approval.
