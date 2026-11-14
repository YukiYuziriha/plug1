# 1.10.1 — Measured graph background preview

**Pending visual approval. No installation, package replacement, commit or push.**
Based on the clean delivered `df4ba55` / 1.10.0 checkout. CMake/source and rebuilt
Release artifacts are 1.10.1; tracked `Windows-x64` and installed plugins remain
1.10.0. No parent-project edits. No granular or unrelated effect rendering changes.

## What the pixels mean

- **Reverb (the existing algorithmic effect):** filled, scrolling processed-wet
  waveform history behind the thin RT60/onset guides. The field continues with
  the actual tail after dry input stops, including held wet energy in Freeze.
- **Convolution:** the same real wet history behind the original generated-IR
  envelope/Tail-taper guide. The guide is now a thin foreground stroke, not an
  opaque model fill covering the measured field.
- **Saturator:** upper input and lower processed-wet history bands behind the
  thin transfer curve. Actual Drive changes affect the processed band; the old
  thick highlighted transfer segment and jumping RMS bar have been removed.
- Each history contains **128 consecutive 20 ms bins: 2.56 seconds**, oldest
  left/newest right. Rims show zero-inclusive stereo-union signed min/max;
  denser cores show stereo RMS magnitude clipped to the peak band. No channel
  summing/cancellation. Extrema include every sample, not arbitrary decimation.
- Field amplitude uses a fixed **-60 to 0 dBFS logarithmic amplitude scale**, not
  automatic normalization. Pixel quantization is three vertical pixels; no
  random particles, fake waves, synthetic decay, or GUI-rate envelope invention.
  Signals below -60 dBFS are visually empty (telemetry itself is not truncated).
- The background's time/amplitude scales are independent of the foreground
  model axes. The short header and tooltip distinguish these explicitly. The
  convolution guide is not a measured IR; Saturator's model still excludes the
  resampler, ADAA/DC blocker, Tone, Output, RMS matching and Mix.
- Wet samples are measured **after effect processing and before slot Mix**.
  Slot bypass/zero Mix, whole-plugin bypass and host bypass clear measured
  history while DSP continues unchanged. Reset/rate/graph/subscription changes
  invalidate it. Continuous silence scrolls the real history out; a stopped
  stream clears after 250 ms without a new publication. Freeze holds the real
  reverb tail, not the telemetry capture. Hidden views release subscriptions.
- Motion 0 pauses decorative motion only. Measured data uses the existing
  visible-view 30 Hz refresh; no additional audio-dependent GUI timer.

## Implementation and real-time contract

`FxMonitor.h` extends the existing per-slot three-buffer exclusive-ownership
snapshots. Only a producer-owned staging ring and an exclusively claimed payload
are written. Atomic ownership prevents simultaneous plain-payload reader/writer
access; this is **not an unsafe float seqlock**. Publication tries a bounded set
of buffers and skips busy ones; the audio writer never waits for GUI readers.
All large storage is allocated at monitor construction. Capture, publication and
read use bounded fixed arrays with no allocation, lock, I/O or GUI access.

`Routing.h` measures the real `CreativeEffect::process` result for ordinary Reverb
as well as factory Convolution/Saturator. `VocalEngine.cpp` forwards only a
presentation bypass flag; it does not gate or change any audio calculation.
`CreativeEffectVisual.h` renders snapshots, never live DSP state. Host parameter
IDs/count, automation/state format, audio algorithms, latency and blending remain
unchanged.

## Validation observed

All configure/build/test/benchmark/render/encode subprocesses ran through the
64-bit `tools/Invoke-HalfCpu.ps1`: **6/12 logical processors, affinity 0x3F**.
Two initial full-build invocations hit a 200 s tool timeout; incremental resumes
completed successfully with a longer bound. Final Release build passed, including
**CLAP and VST3**; `COPY_PLUGIN_AFTER_BUILD` remains off.

- Final 36/36 non-editor/non-grain-UI CTests passed (178.54 s), including CLAP
  smoke, baseline compatibility, factory FX, monitor concurrency, routing/state,
  presets and DSP regressions. Log: `noneditor-final.log`.
- Final additional factory test passed after extending the pre-Mix and duplicate
  instance oracle: `factory-oracle-final.log`. Every published historical bin is
  compared with independently accumulated actual rendered PCM at **48/96 kHz,
  mono/stereo, irregular blocks**. Tests cover wet tails after input stops,
  changing Drive, ring wrap, reset silence, bypass and bit-exact observed versus
  unmonitored DSP. Audio-thread allocation/destruction guards stayed zero.
- Concurrent monitor readers validate ordered coherent full-rate samples and
  historical extrema/RMS while the writer advances. Subscription generation,
  refcounts, reset/rate/removal and slot reuse tests passed.
- Isolated granular UI CTest passed (12.37 s). Granular rendering was not changed.
- Final isolated editor run: **75/76 groups passed**, including the new measured
  field group, all-theme model checks and existing factory telemetry tests.
  **Not a clean editor suite:** `Compact Stack cards, fixed inspector rows and
  click activation` failed its synthetic voice-card click, reporting
  `active=0 slot=-99`. It also failed an isolated retry. Follow-up investigation
  identified Windows lock-screen native hit-test occlusion; see below. The
  stronger fixture still fails rather than pretending to click through it.
  Original log: `editor-final.log`; latest: `compact-editor-final.log`.
- New measured-field native tests/render pass checks actual `Processor::processBlock`
  PCM, changed graph pixels at Motion 0, sustained input-silent wet tails,
  Freeze, stopped-stream expiry, slot/plugin/host bypass and reset. No injected
  UI telemetry is used. All nine theme/effect render combinations passed.
- `git diff --check` passed (only the repository's LF/CRLF conversion warning).

### Compact Stack click investigation (no production UI changes)

The failure reproduces with **only Compact Stack selected**, without running the
new waveform group. This rules out a required preceding waveform-fixture cleanup
leak. Two focused fixture corrections were attempted: use the freshly enumerated
second monitor with a fitting 1280 x 800 native editor, then scroll ancestor
viewports to expose the card header before native injection. The previous
1480 x 1600 inspector-layout assertion is preserved but now runs after removing
the native peer, explicitly offscreen. All original layout, Advanced activation
and voice-selection assertions remain; a native-hit precondition and exactly-one
registered mouseDown assertion were added, not substituted for them.

Evidence after those corrections, in both isolated and full editor runs:

- Native editor bounds `(3760,473)-(5040,1273)`, visible/enabled.
- Lead header root/peer position `(201,451)`, screen `(3961,924)`; component and
  native-peer transforms agree exactly, scale 1 / 96 DPI. The logical hit is
  `leadCard`, inside the window and exposed viewport.
- No modal components, no dragging, **Win32 GetCapture() is null**.
- **Win32 WindowFromPoint(3961,924) returns class
  `LockScreenInputOcclusionFrame`, not the editor HWND.** Both `root.contains`
  and `peer.contains` therefore return false.
- JUCE's Windows peer `contains` explicitly compares `WindowFromPoint` against
  its own HWND. MouseInputSource's native dispatch calls that containment check
  before selecting an event target. Logical `getComponentAt` alone does not
  check desktop occlusion, explaining why the original fixture could pass its
  logical-hit assertion but never activate/select a card (`active=0 slot=-99`).

This is a locked/occluded native desktop input blocker, **not evidence of a DPI
transform error or a production Stack callback regression**. Actual unlocked
native click behavior is still unverified; no lock-screen bypass, direct callback
substitution, OS input-filter workaround, skipped assertion or automatic unlock
was attempted. Stop after the two focused fixture attempts; rerun on an unlocked
interactive desktop to complete native click validation.

Final observed results through HalfCpu: isolated Compact Stack **0/1**, full
editor **75/76**, failing only the stronger native-hit precondition. The new
measured-field group and all other groups still pass. Logs under the unchanged
review directory: `compact-diagnostic.log`, `compact-win32-diagnostic.log`,
`compact-focused-final.log`, `compact-editor-final.log`, and
`compact-investigation-build.log`. Existing GIF/screenshot paths and review
assets were not regenerated or renamed for this investigation. No plugin
production source changes, version bump, install, package replacement, commit
or push were made in this follow-up.

### Informational timing, not a hard real-time guarantee

`telemetry-benchmark.csv` records a single noisy two-second stereo 48 kHz run per
mode. Unsubscribed/subscribed CPU as percent of one real-time core:

| Effect | Unsubscribed | Subscribed |
| --- | ---: | ---: |
| Reverb | 2.312% | 2.384% |
| Convolution | 3.053% | 4.000% |
| Saturator | 3.796% | 3.941% |

This includes the existing full-rate telemetry as well as new history, not an
isolated history-only cost. Checksums matched within each effect's three modes.
No-bridge Saturator was slower than either bridge mode in this short run, so do
not infer a precise speedup or universal overhead from these timings.

## Actual production UI review artifacts

Absolute review directory:

`C:/Projects/vibe-coded-shit/plug1/source/build/approval-1.10.1/`

For **each** `reverb`, `convolution`, `saturator` and **each** `device`, `neon`,
`spectral`, the directory contains:

- `<effect>-<theme>-graph.gif` — actual production graph animation.
- `<effect>-<theme>-panel.gif` — complete 840 x 700 production controls panel.
- `<effect>-<theme>-graph-before-after.gif` — actual 1.10.0 still at left, new
  measured sequence at right; **not fabricated baseline animation**.
- `<effect>-<theme>-graph-before-after.png` and
  `<effect>-<theme>-editor-before-after.png` — labeled comparisons. Editor
  comparisons preserve aspect ratio with black padding for different fixture sizes.
- `after/<effect>-<theme>-measurements.csv` — actual measured input RMS, wet RMS,
  history sample position and Drive per image; no listening claim.
- `after/<effect>-<theme>-editor-native-20.png` and `-75.png` — full native
  editor component snapshots at excitation and late tail/changed Drive.

**All 27 GIFs were ffprobe-verified: 148 frames, 5.92 s, 25 fps, each under
8 MiB.** Graph GIFs are about 0.15–0.17 MiB; full panel GIFs about 4.35–6.12 MiB.
Reverbs use a 0.55 s shaped two-tone burst followed by digital input silence;
Saturator uses measured two-tone audio with changing amplitude and Drive
0 / 12 / 30 / 3 dB. This is deterministic generated audio through the actual
processor, not a microphone/DAW recording and not a listening test.

Baseline images were captured from the previously built 1.10.0 executable before
rebuilding. Their stimulus, settings and fixture sizes differ (including Reverb
RT60 2.8 s before / 4 s after); these are **visual comparisons, not a controlled
numerical A/B**. Baseline native placement was not verified; do not claim it used
the second monitor. Old `after/*editor-offscreen*.png` are superseded diagnostic
images, not the final full-editor evidence.

### Native display evidence

Final render session freshly enumerated:

- Primary bounds `(0,0,3440,1440)`, work `(0,0,3440,1392)`.
- Non-primary bounds `(3440,357,1920,1080)`, work `(3440,357,1920,1032)`.
- Both **96 DPI, scale 1**.

Every new editor fixture was placed at `(3760,473,1280,800)` and every standalone
panel at `(3980,523,840,700)`, inside the non-primary work area. These are
**JUCE component snapshots of verified native fixtures**, not screen/framebuffer
recordings or host-native FPS measurements. No user host/unrelated window was
moved. Final evidence: `render-final.log`. Earlier offscreen diagnostics are not
retroactively described as second-monitor tests.

### Reproduction

Run in a dedicated 64-bit PowerShell process from the repository root; toolchain
setup follows `Windows-Resource-Limits.md`. Run native editor tests separately
from other native GUI suites to avoid known fixture interference.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake --build source/build --config Release
# Run the renderer with working directory source/build/approval-1.10.1/after:
# <absolute wrapper path> <absolute VocalStackEditorTests.exe path> --render-measured-fields
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python source/tests/encode-measured-fields.py source/build/approval-1.10.1
```

The encoder uses the already installed ffmpeg/ffprobe; no Python package or
network dependency was added. Approval files stay in ignored build storage.
**No install/package replacement/commit/push until visual approval.** Actual DAW
listening/transport, VST3 host validation and host-native FPS remain unverified.
