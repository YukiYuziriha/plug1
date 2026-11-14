# Preview 1.11.0 — complete Neon node visual audit

**Audit first, not a redesign.** No production UI/DSP changes, dependency install,
plugin install, package refresh, version bump, commit or push in this task.
The pending readability work is preserved.

## Open the gallery

- [Complete animated gallery and individual clip index](../source/build/approval-1.11.0-node-gallery/index.md)
- [Machine-readable inventory, model choices, dimensions and image/audio activity](../source/build/approval-1.11.0-node-gallery/gif-manifest.json)
- [Read-only delivered/installed binary verification](../source/build/approval-1.11.0-node-gallery/binary-hashes.json)

Artifacts are local ignored build outputs. Keep this directory when sharing the
index. There are **32 node/role/slot entries**, **49 individual GIFs** and **14
contact-sheet GIFs**. Each is **100 frames, 4 seconds, 25 fps**, verified with
ffprobe. Native-sized close-ups are 412 × 730 (right inspector, including audit
label) and 904 × 432 (complete Stack Main Bus FX row, including audit label).
The largest individual is **0.304 MiB**, largest sheet **0.842 MiB**, below the
8 MiB per-file budget. Contact sheets use reduced previews; individual GIFs
preserve the original component dimensions. Each clip also has a full 1280 × 900
editor context PNG in `frames/<node>-<view>-context.png`.

These are **production-component snapshots**, not desktop recordings or measured
host presentation FPS. Native fixture windows were freshly enumerated and placed
on the non-primary monitor, without moving unrelated windows or requesting
foreground focus. No popup interaction was required. Snapshots do not establish
that the desktop windows were unoccluded; the previous Chrome/native-hit-test
blocker was not bypassed and no native click pass is claimed.

Observed primary work area `(0,0,3440,1392)`; secondary work area
`(3440,357,1920,1032)`; both 96 DPI, scale 1. Each native editor was bounded at
`(3760,423,1280,900)`. Labels/contact-sheet composition and encoding are offscreen
operations, not second-monitor screen grabs.

## Source-of-truth coverage

Inventory was traced from `RoutingPanel::showAddMenu` / `addNode`, `NodeType`, and
`mainBusFx::eligible`. All **23 non-legacy NodeType values** are represented.
`sum` is deliberately absent: it is a migrated legacy value, not spawnable UI.
All eight stable Voice source slots and the restored main Chorale are included.
The required existing Output and main Lead/Stack mix are included even though they
are not independent Add Node menu entries. Arbitrary duplicate IDs and 15 shared
FX automation slots are instances, not additional visualization types.

| Effect | Right inspector | Stack Main Bus FX | Actual visual meaning |
| --- | --- | --- | --- |
| Chorale | Yes | Yes | Controls only; static at fixed settings |
| Waveshaper | Yes | Yes | Parameter transfer plus measured stereo PRE/POST waveforms |
| EQ8 | Yes | Yes | Requested EQ response plus measured stereo-power PRE/POST FFT |
| Multiband | Yes | **NOT SUPPORTED** | Requested crossover curves, measured input FFT/band levels; routing-only branches |
| Delay | Yes | Yes | Parameter-timed phase illustration, not measured echoes |
| Reverb | Yes | Yes | Actual input/wet history and tail; foreground decay/swell guide is a model |
| Compressor | Yes | Yes | Static transfer model plus measured DSP gain reduction |
| Bitcrusher | Yes | Yes | Static quantization schematic; no invented audio reaction |
| Granular | Yes | Yes | Stable parameter-envelope/history guide, not live grains or captured waveform |
| Spectral Morph | Yes | Yes | Actual MAIN/DONOR/result spectra; actual donor capture/hold/release |
| Convolution | Yes | Yes | Actual input/wet history and tail plus synthetic IR envelope guide |
| Saturator | Yes | Yes | Actual input/wet history plus nominal static transfer guide |
| Gain | Yes | Yes | Measured PRE/POST meters |
| Stereo | Yes | Yes | Measured PRE/POST meters; width/balance/polarity controls |
| Gate | Yes | Yes | Measured PRE/POST meters, production gate information |
| XP Filter | Yes | Yes | Frozen parameter response plus measured PRE/POST FFT |
| Synth Filter | Yes | Yes | Approximate cached small-signal response plus measured PRE/POST FFT |
| Restored main Chorale | Yes | Yes | Legacy host-bound controls, static at fixed settings |

Each remaining entry has its own **actual right-inspector GIF**, and **NOT
SUPPORTED** is recorded for its Stack FX view (no substitute/fabricated rack):

- Lead;
- Voice 1, Voice 2, Voice 3, Voice 4, Voice 5, Voice 6, Voice 7, Voice 8;
- SIDECHAIN;
- independent Dry/Wet mix;
- Splitter;
- main Lead/Stack mix;
- Output.

These native inspectors contain controls/information, not waveform displays.
Lead/Voice monitoring elsewhere in Stack does not imply their right inspectors
have that display. Output meters remain in the main editor. SIDECHAIN is shown
with a stereo bus configured, **not** a claim that a DAW host cable is detected;
its audit graph remains unwired, matching Add Node semantics. Morph's separate
fixture explicitly wires the generated auxiliary PCM to DONOR.

Classic LFO and Random belong to `+ Mod`, not Add Node/NodeType. They are not
missing audio-node types: their editor is the separate bottom modulation panel,
not a Stack FX or right audio-node inspector. This gallery does not claim to
capture that separate modulation surface or all parameter-scroll positions.

## Complete model/subtype inventory and chosen representatives

One representative per node is captured; this is **not** a clip of every
configuration/permutation. All production selector options are also recorded in
the JSON/index. Fixed settings let static vs measured behavior be distinguished.

- **Waveshaper:** 19 curves: Soft, Hard, Asymmetric, Warm Asymmetric, Sine
  Saturation, Arctangent, Rounded Clip, Asymmetric Clip, Triangle Fold, Sine Fold,
  Single Fold, Multi Fold, Half-Wave, Full-Wave, Staircase, Dead Zone, Binary
  Threshold, Exponential, S-Curve. The additional `Legacy: Soft/Hard/Asymmetric`
  selector entry delegates to the preserved legacy shape parameter; it is not a
  twentieth curve. Quality: Off/project-rate ADAA, 2x IIR, 4x IIR; Raw overrides
  quality, and Level match is independent. Captured: Soft, Off, Raw off.
- **EQ8:** eight stable band slots, each with 14 modes: Bell; low/high shelf
  12 dB/oct; low/high shelf 24 dB/oct; low cut 6/12/24/48 dB/oct; high cut
  6/12/24/48 dB/oct; Notch. Captured: first band selected, Bell, factory defaults.
- **Multiband:** two bands (Low/High) or three (Low/Mid/High). Captured: three
  bands; low output routed to Output. Inactive branch settings/wires are not
  different node types. No rack view exists.
- **XP Filter:** 15 configurations: Low pass 12, High pass 12, Band pass 12,
  Notch 12, All pass 2, Low pass 24, High pass 24, Band pass x2, Notch x2,
  All pass 4, Low pass 36, High pass 36, Band pass x3, Notch x3, All pass 6.
  Captured: Low pass 12, 1 kHz, default resonance/drive.
- **Synth Filter:** 13 configurations: Digital LP12, Digital HP12, Digital BP12,
  Digital Notch24, Ladder LP24, Comb, Formant A/I/U/O, Dirty LP12, Diode LP24,
  Analog LP12, Digital LP24, Ladder HP24, Analog HP12. Captured: Digital LP12,
  1 kHz, default resonance/drive/feedback. Formant vowels are controlled within
  that model, not additional spawnable node types.
- **Convolution:** Church, Hall, Room, Plate, all synthetic IRs. Captured: Church,
  default brightness/predelay/tail. Actual wet tail is asserted in both views.
- **Morph:** Magnitude or Envelope; FFT 1024/2048/4096; Freeze capture/hold/release;
  internal graph donor or explicitly wired external source. Captured: Magnitude,
  FFT 2048, Morph 80%, external stereo donor; Freeze on at 1.2 s, donor changes at
  1.8 s while held, Freeze off at 3.0 s. Held donor bins are asserted unchanged
  while MAIN continues changing. The actual result spectrum is displayed.
- **Delay:** free milliseconds or tempo-synced beats; continuous ping-pong,
  feedback, ducking, drive and repeat pitch rather than separate algorithms.
  Captured: free 330 ms, factory ping-pong/feedback/pitch. Moving phase is a timing
  model and must not be called measured audio.
- **Reverb:** one parameterized space algorithm, Freeze and pitched-feedback/swell
  options, not a hidden model selector. Captured: 3 s RT60, ducking 0, no freeze,
  default pitched-feedback amount 0; 0.7 s burst then actual wet decay.
- **Compressor:** one stereo dynamics algorithm with ratio/knee/attack/release,
  detector high-pass and makeup controls. Captured threshold -30 dB; remaining
  factory settings, with varying input amplitude to exercise actual GR.
- **Bitcrusher:** one bit-depth/sample-rate/dither/drive/low-pass model. Captured
  factory settings (10 bits, 16 kHz), with fixed parameters; deliberately static.
- **Granular:** forward/reverse and freeze/live, continuous pitch/length/density/
  lookback/scatter/spread/filter/drive. Captured factory forward/live configuration;
  graph remains a stable parameter model. No live cloud was added.
- **Saturator:** one warm/2x ADAA character model with continuous character/tone,
  output and optional Level match. Captured drive 12 dB, other defaults.
- **Gain:** level including exact mute sentinel, not a model selector. Captured
  unity, with genuinely changing PRE/POST meters.
- **Stereo:** continuous width/balance plus swap and independent L/R polarity;
  captured default width 100%, centered, no swap/inversion.
- **Gate:** one downward-expansion model, captured default threshold -40 dB,
  range 60 dB, ratio 20, default timings.
- **Chorale/main Chorale:** Mix/Motion and enable controls, no model selector.
  Independent capture is enabled at 100% wet; restored main capture uses its
  production legacy controls.
- **Lead/Voices:** eight stable Voice slots and one Lead, each with pitch/detune/
  formant/gain/pan/delay/depth/rate and enable controls. All are present/enabled in
  the source fixture, with production default values; not nine distinct synthesis
  algorithms.
- **Dry/Wet:** independent dry/wet inputs and linear blend. **Main mix:** the
  existing Lead/Stack balance, not an extra utility effect. **Splitter:** identical
  full-level A/B outputs, not frequency splitting. **Output:** final trim only.
  **Sidechain:** disabled/mono/stereo host bus configurations, connectivity
  unknown; captured stereo-configured, intentionally unwired in its own fixture.

## Validation and interpretation

- Targeted Release `VocalStackEditorTests` build succeeded; no full plugin/package
  rebuild or installation was needed for a capture-only change.
- All 49 fixture views completed with actual generated PCM through
  `Processor::processBlock`, finite nonzero output and per-frame RMS CSVs.
- **27 clips change pixels; 22 are exactly static.** Static: independent/main
  Chorale, Bitcrusher, Granular (both views), and all 14 source/routing inspectors.
  These are retained as 100-frame GIFs and honestly labelled. No fake audio,
  injected telemetry, parameter animation or randomized visualization was used.
- Reverb/Convolution actual wet-tail assertions and Morph measured
  capture/hold/release assertions passed independently in both surfaces.
- Every encoded file passed duration/frame-count/dimension/file-size and
  nonblank decoded-image checks. PNG frame hashes and unlabelled component
  pixel-change counts are recorded. An initial unflushed label image/append-on-
  rerender issue and singleton-sheet encoder error were corrected before final
  regeneration/verification; initial captures are not claimed as valid finals.
- All capture/build/render/encode/verification children ran sequentially through
  `Invoke-HalfCpu.ps1`: 64-bit PowerShell, **6/12 eligible logical processors,
  affinity 0x3F**; ffmpeg filter concurrency is explicitly bounded. No other
  compiler/test/render jobs were launched concurrently by this task.
- All four delivered/installed binaries retain the recorded **1.10.0.0** hashes;
  the ignored CLAP/VST3 previews remain **1.11.0.0**. No version/package changes.
- Python syntax and inventory coverage checks passed; `git diff --check` passed.
  The broad editor/DAW/native-click suite was not rerun. No claim about listening,
  Expand All desktop FPS, foreground occlusion, CPU reduction or redesign success.

## Reproduce

Build only the existing `VocalStackEditorTests` target using the documented
64-bit half-CPU wrapper and your existing Visual Studio environment, then:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python source/tests/capture-node-gallery.py capture
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python source/tests/capture-node-gallery.py encode
```

The capture script uses the existing ignored `source/build` tree and creates
its `approval-1.11.0-node-gallery/frames` output directory if needed. Existing ffmpeg and
ffprobe are required; the script refuses rather than installing packages.
Optional `--start`/`--end` bounds split sequential work into manageable batches.
All production changes remain outside this audit's scope: uniform pixel geometry,
Morph's left rectangle and Expand All performance still require a separate,
reviewed implementation after this visual inventory.
