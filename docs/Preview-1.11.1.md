# 1.11.1 — unified compressor plot and typography

Approved and installed in both formats. This document preserves the preview
methodology; final delivery evidence is in
[Release-1.11.1-Validation.md](Release-1.11.1-Validation.md).

The release also improves small graph labels and Morph axis layout, and adds a
10-second real-time Motion 0/3/100 comparison to the public showcase. Default
animation speed, sound processing, parameters and latency are unchanged.

## One plot, independent axes

Both the actual Routing inspector and Stack main-bus FX rack use the same
compressor component and **one plot rectangle**:

- Thin solid **Curve**: parameter-derived static input/output transfer including
  threshold, ratio, knee, makeup, effective modulation targets, enable and Mix.
  Primary axes: **Input dBFS** below, **Output dBFS** at left, fixed **−72..0**.
  This model does not simulate attack/release, detector filtering or DSP smoothing.
- Faint **In** and stronger **Wet**: actual stereo RMS from completed 20 ms PCM
  bins, shaded behind the curve; same left dBFS amplitude scale, but the separate
  bottom **History −4 s..now** time scale. Wet is measured before slot Mix, not
  reconstructed from the static curve or GR. The existing producer stores 128
  bins / **2.56 s**, right-aligned in four seconds; older space is deliberately
  empty, never stretched or fabricated.
- Thin dashed **GR**: actual DSP peak attenuation per completed 20 ms bin, oldest
  left, newest right, four seconds. It uses History time and its own labelled
  right **GR dB** scale: **0 at top, 36 at bottom**. The paths do not imply that
  time and input level are the same coordinate.
- Numeric actual GR and its 0–36 dB slim meter stay below/outside the plot. The
  panel's existing compression readout is retained. Plot excursions clip at the
  fixed scales; numeric GR is not clipped to 36 dB.

A small legend and axis rows replace the two-lane headings. Full interpretation
is in the plain tooltip. All signal layers share one clipping rectangle, not
nested panels. Dashed GR preserves the solid transfer at coincident paths.
Stopped/stale, bypassed and reset telemetry clear without inventing motion.

## Telemetry-only bridge

The compressor already publishes measured GR and full-rate input/final-output
PCM. Its pre-Mix wet samples were computed but not forwarded into the existing
RMS-history collector. `Routing.h` adds only the compressor to that collector's
existing monitoring condition. No compressor algorithm, audio routing, parameters,
state format, gain, latency or Mix processing changes. Other FX visuals are unchanged.

## Review artifacts and reproduction

Local ignored artifacts: `source/build/approval-1.11.1-compressor/`.
For each theme (`device`, `neon`, `spectral`) and production view (`inspector`,
`rack`), `compressor-<view>-<theme>` has:

- `.gif`: 100 frames, four seconds at 25 fps;
- `-plot.png`: native-size close-up of the single plot and slim meter;
- `-panel.png`: actual production panel with controls;
- `-context.png`: full editor, showing which production view is active.

`frames/` retains original PNG sequences, PCM RMS/telemetry measurement CSVs and
fixture metadata. `manifest.json` records dimensions, frame counts and sizes.
No third-party package or dependency is installed by the capture script.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python source/tests/capture-compressor-preview.py
```

Native fixtures enumerate connected displays at capture-session start and place
1280×900 editor windows on the non-primary display. Observed layout: primary
3440×1440 at (0,0), work 3440×1392; secondary 1920×1080 at (3440,357), work
1920×1032; both 96 DPI / scale 1. Captured native editor bounds:
(3760,423,1280,900), within the secondary work area. The small telemetry-lifecycle
fixture is also placed there. PNGs/GIFs are **offscreen component snapshots of
those production components**, not desktop recordings or measured desktop FPS.
Generated PCM goes through the actual processor; visual telemetry is not injected.

## Validation

- Release CLAP and VST3 preview binaries built locally, with auto-install disabled.
- Focused compressor history/model/layout/legend/pixel-overlap checks; actual
  processor telemetry sequence updates, stale timeout, bypass and reset checks.
- Routed compressor input/wet energy and GR, including bit-identical monitored
  versus unmonitored audio; existing monitor and creative DSP regressions.
- Relevant creative editor layout/modulation checks and built CLAP/VST3 host smoke
  tests. The large unrelated DSP suite is not required for this presentation change.
- All compiler/test/render/encode commands use dedicated x64 PowerShell through
  `Invoke-HalfCpu.ps1`: **6/12 eligible logical CPUs, affinity 0x3F**. No build agents.
- Six actual-view/theme GIFs verified: 100 distinct frames each, four seconds,
  measured nonzero PCM and wet telemetry; below 8 MiB each.

This is automated validation, not a listening check in the user's DAW.
