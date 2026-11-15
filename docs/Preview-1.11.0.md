# 1.11.0 — SIDECHAIN, Spectral Morph and shared reactive material

**Current coordinated revision: approved, Release-built, packaged and installed
as 1.11.0.** Both packaged and installed CLAP/VST3 module smoke checks pass;
build/package/installed SHA256 hashes and PE versions match. See
[final delivery validation](Release-1.11.0-Validation.md).
Core regressions pass **40/40**; current full editor result **80/81** retains the
native desktop-occlusion click precondition failure. Separate rack and granular
UI suites and the focused handoff suites pass. Approval authorizes delivery with
that limitation, not a claim of native input/FPS or DAW listening success.
The earlier 1.10.1 work is included; `Preview-1.10.1.md` remains its historical
record. Earlier preview-only status statements below are historical.

## Using the new effect

1. Add **Spectral Morph** in the main-bus rack or Routing. It is an independent
   FX instance using the existing stable slots 1–15. The external source uses no
   FX slot. Adding Morph never invents a donor cable.
2. In Routing choose independent **MAIN** and **DONOR** inputs. Any ordinary
   internal source/effect is a valid donor where the DAG and latency budget allow.
   To use a host track, enable the optional mono/stereo sidechain bus in the host,
   add the unwired **SIDECHAIN** source, and connect it to DONOR.
3. Choose Magnitude or Envelope, Morph, FFT Size, Smoothing, Freeze, Mix and Output.
   Mix is delayed main versus processed audio, not donor versus main.

An unwired/disabled donor supplies silence, never the main mix or main input.
SIDECHAIN is not RMS-normalized; ordinary graph fan-in retains its existing
provenance-based merge law. Mono auxiliary input duplicates to L/R; stereo retains
channel identity. Mono output averages the independently processed L/R channels.

**Configured is not connected.** JUCE/VST3 cannot reliably report whether the host
has attached a cable. Source labels say MONO/STEREO CONFIG or DISABLED, not that a
nonzero cable has been detected. A configured silent bus can supply valid zero
analysis. Zero audio is not used as a disconnect detector or a reason to borrow
main audio. Host availability is runtime metadata, not preset audio.

## Latency policy — fixed per host preparation

Graphs without Morph prepare **zero additional routing latency**. Graphs declaring
Morph prepare its actual fixed **4096-sample** routing latency, including dormant,
bypassed and disconnected Morph nodes so their activation/wiring cannot change
the contract. The processor and actual CLAP/VST3 wrapper activation tests verify these totals:

| Sample rate | No Morph total | With Morph total |
| --- | ---: | ---: |
| 44.1 kHz | **1344 samples** (30.48 ms) | **5440 samples** (123.36 ms) |
| 48 kHz | **1344 samples** (28.00 ms) | **5440 samples** (113.33 ms) |
| 96 kHz | **2688 samples** (28.00 ms) | **6784 samples** (70.67 ms) |

**Adding/removing Morph while prepared saves a pending topology, not a new audio
plan.** Routing labels it **RESTART PENDING** and states that visible saved cables
are not running. The current graph and host latency remain unchanged until the
host deactivates/reactivates (re-prepares) the plugin. Transport stop and reset do
not apply it. Hosts that suspend processing without re-preparation are not enough;
use their plugin deactivate/reactivate facility or reload the plugin. Pending
routing survives state save/recall and can be undone before activation. Ordinary
same-latency routing edits remain live. Slot controls/automation still address the
current shared host parameter bank; this is topology deferral, not transactional
preset/parameter deferral. State recall can therefore change parameters while
leaving its latency-changing topology pending. A pending latency edit that reuses
an occupied effect slot for a different machine is rejected explicitly; deactivate
before that replacement. Modulation-assignment retirement for a deferred graph
waits until preparation. No automatic host restart or unverified live PDC switch
is claimed.

The 1024/2048/4096-point STFTs have 1024/2048/4096-sample streaming latency and
3072/2048/0 samples of padding. All stay warm; FFT selection crossfades without
reallocating or changing the host report. Prepared per-wire delays align MAIN,
DONOR and ordinary reconvergence; final output is padded to the fixed reserve.
Slot bypass, slot Mix, global Mix and whole-plugin/host bypass retain delayed
**main** alignment. The source engine also delays external input to the existing
internal-source algorithmic latency before graph processing.

Any dependency chain exceeding 4096 routing samples is rejected, including serial
Morph through MAIN **or DONOR**, even in a dead-end branch. Multiple independent
parallel instances are supported within the existing shared FX capacity. Arbitrary
user-authored nonzero node latency remains rejected. Musical delays/predelays and
IIR/EQ/oversampling phase are not automatically removed or called integer latency
compensation. Standalone legacy `VocalEngine` callers retain a zero reserve unless
one is explicitly prepared; the plugin derives that reserve from declared graph
latency at preparation, not a global always-on penalty.

With auxiliary wiring, rack topology edits that are not provably serial are
explicitly read-only: use Routing to add/remove/reorder wires/nodes. Parameter
editing remains available. Rack discovery follows audible MAIN paths, not
analysis-only donor effects; it never deletes donor/fan-out wires to force a
serial rack.

## DSP and state semantics

- Four-overlap WOLA uses compatible sine analysis/synthesis windows and 1/2
  overlap normalization. Arbitrary host block boundaries do not restart frames.
- Magnitude: `R = (1-M)*|main| + M*|donor|`, retaining main complex phase. Spectral
  zeros/near-zero carrier bins do not receive invented phase. A silent main is
  not excited by donor-only audio. This is not a vocoder/phase-transfer mode.
- Envelope: blend toward `|main| * clamp(donorEnvelope/mainEnvelope, 0, 16)`.
  Frequency envelopes use a box average with approximately ±200 Hz radius,
  quantized to bins and bounded at 32 bins each side. Smoothing is donor temporal
  magnitude smoothing: `alpha = 1-exp(-hop/(sampleRate*smoothingSeconds))`.
- Continuous Morph, Mix, Output and transition weights are smoothed. Morph,
  Smoothing, Output and existing Mix support continuous modulation. Mode, FFT and
  Freeze are discrete host automation, deliberately rejected as continuous LFO
  targets. No audio-thread graph construction, allocation, reclamation, locks,
  I/O or GUI access is introduced in processing. Prepare/reset require the
  existing quiescent/control-thread lifecycle.
- Freeze captures the next available analysis frame per warm resolution, keeps
  held donor data separate from live smoothing, and transitions over 20 ms.
  Main analysis/phase continues. Disconnect while held retains the capture.
  Release returns to live donor; a fresh unavailable capture request stays pending
  rather than resurrecting an older capture (only an existing release may fade).
  Reset/reprepare clear captures. Presets retain the Freeze request, **not captured
  audio/spectra**; recall can therefore show Capture Waiting until input exists.
- No automatic donor level matching. Envelope division/gain and exceptional
  values are bounded; ±120 dBFS arithmetic containment is not a musical limiter.
- **2357 parameters:** every existing index **0–2341** remains fixed. Morph's six
  fields per independent slot stay at **2252–2341**. Fifteen separate persisted
  booleans `fx1stereo_mono_audition` through `fx15stereo_mono_audition` append at
  **2342–2356**, default false and host-automation-only, not LFO targets. No
  released utility/creative stride or mode changes. Mono Audition folds the final
  post-Mix Stereo slot output as `(L+R)/2` using the existing smoothed 10 ms hook;
  Width/Balance/Swap/Polarity/Mix remain untouched. Slot bypass releases audition
  smoothly alongside the existing Mix fade; global/host bypass still selects the
  latency-aligned original input. Binary, text,
  graph-state and missing-legacy-value recall are covered.

## Measured presentation

Reverb, Convolution and Saturator share the reactive material, now separating
its unchanged **48 × 24 decorative background** from fine measured geometry.
All **128** existing signed extrema/RMS history windows are retained around one fixed zero
baseline. Connected stepped waveform paths target 1.5 px horizontally and 1 px
vertical rims at scale 1, bounded to 1024 display columns. Interpolated display
geometry is not additional audio samples; covered measured extrema are unioned
so narrow displays do not skip peaks. These are not drifting blobs, particles or
invented decay. Thin foreground guides remain parameter models. Existing history
sample counts, stale clearing and Motion 0 measured updates are preserved.

Separate `SpectralMorphVisual.h` uses the same material for coherent MAIN/DONOR/
RESULT spectra, actual frozen/pending/unrouted status, and Open Routing. DSP
already needs the FFT; no extra UI FFT or live DSP read is used. Snapshots use
bounded exclusive ownership, not a float seqlock. Analysis-window coherent gain
calibrates peak magnitudes; all 128 logarithmic 20 Hz–Nyquist telemetry bands
are retained in thin 1 px connected traces, fixed -60..0 dBFS. Lines between
measurements are display interpolation, not new FFT detail. DC occupies the
first column. Spectra are analysis/pre-Mix and pre-Output-gain, not final output
RMS or a phase/power-summed stereo spectrum. They describe the selected-resolution
analysis; final audio crossfades warm resolutions during FFT changes. Stopped
analysis expires after 250 ms. Graph/reset epoch checks reject old captures after
removal, reuse, graph edits or reset before publication can be mistaken for the
current instance.

## Coordinated performance/telemetry changes

- Routing synchronizes node cards once per polling path rather than twice. Its
  timer repaints the canvas for material-frame advances or actual graph/port
  changes, not every unchanged tick at Motion 0. Existing interactions still
  explicitly invalidate their affected canvas.
- Node chrome and cable stroking are culled against the actual paint clip;
  procedural surfaces reject wholly clipped paints before raster generation.
  No trace/history/material resolution was reduced.
- FX monitor subscriptions intersect every ancestor viewport, not merely
  `isShowing()`. GUI FFT Hann coefficients are precomputed; simultaneous spectrum
  views share one analysis per processor/slot/sequence/epoch/identity/rate;
  restarting a stalled stream clears shared FFT peaks instead of resurrecting them.
- Procedural rasters also share a bounded **128-entry message-thread cache**
  keyed by exact material, dimensions, palette, contrast and animation frame.
  Tiles are immutable so another surface/phase cannot corrupt a retained image.
  Original material fingerprints and deterministic-phase tests are retained.
- Compressor and Gate retain the existing bounded atomic GR interface, with
  finite nonnegative containment. Coherent snapshots now also carry node kind,
  current pre-makeup attenuation and four-second 20 ms peak-GR history. These
  values come from the DSP, not a curve/threshold model. Display range remains a
  UI concern; telemetry does not clamp actual attenuation to a 36 dB display.
  The original 128-bin PCM history is unchanged. A separate 200-bin peak-GR
  history is valid independently of PCM `hasWet`, including Compressor Mix 0.
  Gate additionally publishes actual input/final-slot-output RMS, actual smoothed
  threshold and mean wet-GR per 20 ms bin through `UtilityHistoryAccumulator`.
  Gate Mix 0/bypass reports zero applied GR, not stale attenuation, including
    while the slot blend is still releasing. Completed
  histories are reordered/copied only during the existing exclusive snapshot
  publication, never read live by the GUI.
- `VocalStackEditorTests --benchmark-routing` is an optional bounded native
  cost probe: 15 FX, actual PCM, collapsed/expanded-exposed/expanded-offscreen,
  Motion 0/3/100. It enumerates displays, places the window on the non-primary
  display when available, and rejects an occluded native window. Output reports
  actual canvas paint time/counts, timer/card synchronization, clip culls and
  material raster/cache counters. It is **not delivered FPS**, an offscreen FPS
  estimate, or a realtime audio scheduling benchmark. Run only through
  `tools/Invoke-HalfCpu.ps1`, after the explicit UI-agent handoff.

### Coordinated validation and preview artifacts

After the explicit all-three-UI-agents completion handoff, the sole coordinator
ran configure/compiler/test/render/encode children through dedicated 64-bit
PowerShell and `tools/Invoke-HalfCpu.ps1`: **6/12 logical processors, 0x3F**.
`COPY_PLUGIN_AFTER_BUILD` remains false. Source version stays **1.11.0**.
These preview checks preceded the subsequently approved delivery; the final
incremental check reported no work to do before packaging and installation.

| Check | Observed result | Log under `source/build/` |
| --- | --- | --- |
| Release CLAP/VST3 and all affected test targets | Built successfully | `coordinated-build-delivery.log`; test-only VST3 queue extension: `coordinated-vst3-automation-build-2.log` |
| Full non-editor DSP/processor/state/host regressions | **40/40 passed**, 201.77 s | `coordinated-core-delivery.log` |
| Focused Control Layout, Compressor History, Utility Visual, monitor, processor and module hosts | **7/7 passed**, 36.44 s | `coordinated-focused-delivery.log` |
| Additional native VST3 FFT parameter queues and saved-state/PCM oracle | **1/1 passed**, 38.34 s, all 108 matrix cases | `coordinated-vst3-automation-2.log` |
| Full editor | **80/81 groups passed**, 161.24 s | `coordinated-editor-delivery.log` |
| Separate Main Bus FX and granular UI, serialized | **2/2 passed**, 17.73 s | `coordinated-native-delivery.log` |
| Actual-PCM/control capture/encode | **39/39 GIFs verified**, 100 frames / 4 s / 25 presentation fps; largest **339773 bytes** | `coordinated-capture-a-final.log`, `coordinated-capture-b-final.log`, `coordinated-encode-final.log` |

The remaining full-editor failure is the unchanged strict native click
precondition, not skipped or replaced by direct component input: the intended
window was visible/enabled at `(3760,473)-(5040,1273)`, but the hit at the intended
card belonged to **`Chrome_RenderWidgetHostHWND`** and `peerContains=0`. The latest
native cost probe is independently blocked by the same class
(`coordinated-native-cost-delivery.log`). Earlier attempts during this integration
hit `SHELLDLL_DefView`; both conditions are recorded rather than weakened or
reported as a clean desktop/native-input pass. No unrelated
window or host was moved/killed, and no native-exposed FPS/DAW transport/listening
success is claimed. Main Bus FX's separate component hit-tested wheel regressions
passed **98 cases per theme**; these are not proof that the external desktop
occlusion disappeared.

The standalone processor matrix additionally compares **non-bypassed actual
external-source audio** against its exact original latency while a Morph draft
is pending, then cancels it before state/availability checks. Activation tests
cover no-Morph → dormant Morph → enable → removal, reset versus preparation,
state recall and irregular blocks. The existing allocation/destruction guards
also cover the new Mono/Gate processor path. Actual CLAP/VST3 module hosts check
baseline and Morph totals, active-state deferral, deactivation/reactivation,
mono/stereo main/auxiliary layouts and FFT automation. The VST3 extension connects
the actual component and controller through `IConnectionPoint`, discovers the
real FFT parameter ID, submits normalized 0 / 0.5 / 1 parameter queues at offset
17, and checks saved component values 0 / 1 / 2, finite/unchanged steady-state PCM
and the unchanged latency. Its first unconnected-controller metadata attempt
failed; proper component/controller pairing fixed the test fixture without a
production DSP change. No stronger sample-accurate FFT-switch timing claim is
made. This verifies the stated fixed-activation policy, **not arbitrary live DAW PDC switching**.

### Cost measurement — not FPS

`VocalStackEditorTests --benchmark-routing-components` forces 60 complete editor
component snapshots per case, processing real PCM between them. It retains a
native peer on the enumerated second monitor but does **not** require/exploit a
false claim of native desktop exposure. Nine cases cover Motion 0/3/100 and
collapsed / expanded inside the viewport / expanded outside it. The initial
60-second attempt timed out; the subsequent bounded 180-second run completed.

One before/after run of this same harness around the immutable shared-raster
cache change observed:

| Expanded inside viewport | Before forced component ms / 60 | After ms / 60 | Approx. ms per forced snapshot, before → after |
| --- | ---: | ---: | ---: |
| Motion 0 | 1607.34 | 1587.48 | 26.8 → 26.5 |
| Motion 3 | 3066.61 | 1774.95 | 51.1 → 29.6 |
| Motion 100 | 4902.47 | 2949.93 | 81.7 → 49.2 |

Logs: `coordinated-component-cost.log` and
`coordinated-component-cost-shared.log`. The after-run Motion-100 expanded case
recorded 119205 shared tile hits; expanded-off-viewport cases drew **zero node
chrome and zero cables**. Card synchronizations equaled routing timer ticks,
not twice their count. Warm Motion-0 cases generated **zero material rasters**.
These are single-run component-work measurements with timer/native-message
catch-up, not controlled statistical benchmarks, delivered FPS or realtime audio
scheduling. Expanded Motion 100 **remains costly**; no universal FPS target or
performance at every DAW/window/scale is claimed.

### Current review clips

[All 39 clips, paired surfaces and three-theme index](../source/build/approval-1.11.0-coordinated/index.md)
with [verified manifest](../source/build/approval-1.11.0-coordinated/manifest.json).
The directory is a local ignored build artifact, not a packaged delivery.

- Gate, Compressor, Stereo, Gain and Morph: **right inspector and Stack rack**,
  each in Device / Neon / Spectral.
- Output, Dry/Wet and main Mix: inspector, each in all three themes.
- Stereo switches persisted Mono Audition on at frame 30, off at 75 and asserts
  actual final L/R equality. Gain recalls/automates 37% Wet to reveal **Wet
  (legacy)**, then returns to hidden default 100%. Output sweeps **-24..+12 dB**;
  Dry/Wet and main Mix sweep unsigned **0..100**. Morph captures/holds/releases
  actual donor spectra, with the wide Stack spectrum at left.
- Captures assert actual DSP GR/history and Gate measured levels/threshold. The
  history is real audio-rate production telemetry, not PCM-ratio inference or
  GUI-generated fake history. Frame CSVs and native-window placement logs are
  alongside the PNGs.
- All captures are **production-component snapshots**, not OS framebuffer
  recordings. Windows were placed on the actual non-primary display, work area
  `(3440,357,1920,1032)`, DPI 96 / scale 1; the primary was `(0,0,3440,1392)`,
  DPI 96 / scale 1. Deterministic material fingerprint tests protect pixel parity
  of the later shared-raster cache.

Additional integration guards verify that non-default **effective LFO Wet** reveals
Gain's compatibility row even at a 100% host base, without changing the base or
allocating a second modulation snapshot. A one-sample Stereo slot-bypass check
catches skipping the smoothed Mono release. A rejected serial-Morph edit preserves
the existing pending draft, its old running latency and the visible RESTART
PENDING warning. Those branches are covered by the final regression tests, not
claimed as successful physical mouse/DAW interaction.

Before delivery approval, read-only SHA256 rechecks of both tracked packages
and both per-user installed binaries matched the original 1.10.0 hashes in
`Release-1.10.0-Validation.md`:
CLAP `41DA7CFA95B68B2B6D3ED1289B797999056C84637882F9E85B71ED00F871658A`,
VST3 `FD71879DA9D21215C201885A26B6A77309EEE69BE29EA1E775F327904657FEA1`.
Those historical preservation checks are superseded by the approved 1.11.0
package refresh and installation recorded in `Release-1.11.0-Validation.md`.
Airwindows work remains research only.

## Initial integration validation (historical)

The final readability revision has its own validation record below. These initial
integration results and baseline artifacts are preserved, not overwritten.

All configure/compiler/build/test/benchmark/render/encode children ran through
64-bit `tools/Invoke-HalfCpu.ps1`: **6/12 logical processors, affinity 0x3F**.
The Release CLAP and VST3 builds succeeded; `COPY_PLUGIN_AFTER_BUILD` stays off.
An initial 200-second build timed out and was resumed. Later builds succeeded.

| Validation | Observed result | Local log under `source/build/approval-1.11.0/` |
| --- | --- | --- |
| Full non-editor/non-grain-UI CTest run | **41/41 passed, 171.77 s** | `noneditor-final.log` |
| Isolated full editor run | **77/78 groups passed** | `editor-final.log` |
| Isolated granular UI | **1/1 passed, 9.50 s** | `grain-ui-final.log` |
| Focused run after final source/rack/placement review | **9/9 passed, 20.03 s** | `focused-after-review.log` |
| Focused actual-PCM Morph UI | **1/1 passed** | `spectral-ui-after-review.log` |
| Native VST3 component host | **108 matrix cases passed**, plus disable/96k/reconnect/44.1k lifecycle phases | `vst3-host-final.log` |
| Reactive three-effects, all themes | Passed actual-PCM render assertions | `reactive-render.log` |
| Morph internal/external donors, all themes | Passed held-spectrum, release, placement, expanded/rack render assertions | `morph-render.log` |
| Encoded GIF verification | **24/24**, 25 fps, each below 8 MiB | `gif-manifest.json` |

The full 41/41 and 77/78 runs followed the final production changes, including
source placement, rack discovery and graph/reset epoch rejection. The focused
9/9 and all-theme compact/expanded/rack captures were then rerun successfully.
No clean full editor pass is claimed.

DSP/source tests cover mono/stereo, 44.1/48/96 kHz, irregular blocks, all FFT sizes,
reconstruction (<3e-6 maximum error oracle), fixed latency, exact prepared Mix-zero
dry, main-phase/DC magnitude and envelope endpoints, freeze capture/hold/rearm/
release/reset, absence, independent instances, finite bounds and transitions.
Routing covers auxiliary cycles/duplicates/pins, compensation, provenance and
unsupported serial latency. Processor tests verify released index formulas,
state/defaults, no captured-audio recall, modulation, actual aliased host layouts,
reprepare, and zero counted callback allocations/destructions. Concurrent bridge
readers check coherent arrays. An early combined read-count/coherence assertion
failed under full load; an isolated diagnostic observed 52653 coherent reads.
A test-driver midpoint scheduling handshake outside the measured publication
section now guarantees reader execution; the repeated full run passed. No wait
was added to production publication and no payload-integrity assertion was removed.

CLAP wrapper coverage selects all nine configurations at all three rates: legacy
main configuration IDs 0=stereo, 1=mono-to-stereo, 2=mono remain unchanged; IDs 3–8
add mono/stereo auxiliary groups. Tests verify metadata, nonzero donor exclusion
from main bypass, actual routed donor absence/channel/phase endpoints, state and
sample-offset FFT/Freeze event sub-blocks with unchanged latency. VST3 uses the
actual DLL factory, IComponent/IAudioProcessor and IBStream, not a Processor
substitute: all four mono/stereo main/output combinations × three auxiliary
layouts × three rates × Morph/global-dry/bypass, plus representative same-instance
rate-change/disconnect/reconnect. Wrapper coverage is not actual DAW validation;
VST3 controller automation queues/transport and comprehensive DAW recall remain
unverified. Envelope/internal-donor coverage is in DSP/processor/UI tests, not
every wrapper-matrix permutation.

### Remaining native input blocker

The sole full-editor failure is the existing Compact Stack native-click
precondition. Earlier runs returned **`LockScreenInputOcclusionFrame`** at the
click point; the final run returned **`Chrome_RenderWidgetHostHWND`**, still not
the editor HWND. Fixture `(3760,473,1280,800)`, target `(3961,924)`, scale 1,
`contains=0`, and null mouse capture are logged. The existing fixture already
raises its own window; an external desktop window still wins native hit-testing.
No assertion was skipped, click callback substituted to satisfy this test,
automatic unlock attempted, unrelated user window moved, or input-filter/Windows
occlusion workaround installed. An unlocked, unoccluded interactive desktop is
required to finish this native click check. Automated render setup uses normal
UI/state APIs, not proof of physical mouse interaction through desktop occlusion.

Actual Bitwig/other-DAW listening, recording/transport, musical quality, host-native
FPS and cross-platform validation are **not claimed**.

### Informational performance, not a deadline guarantee

A separate two-second stereo standalone Morph render, with all three resolutions
warm, measured ~**8.62–8.81% of one core at 48 kHz**, ~7.76–8.01% at 44.1 kHz and
~14.25–14.56% at 96 kHz. Maximum observed irregular-block times were ~1.67–2.02 ms.
See `spectral-benchmark.csv`. This is a short local run, not the entire vocal
engine/rack, not a tiny-block deadline guarantee, and not a maximum-instance stress
claim. Keeping all resolutions warm deliberately costs CPU to make FFT changes
stable and allocation-free.

## Initial review artifacts (preserved baseline)

Directory: `C:/Projects/vibe-coded-shit/plug1/source/build/approval-1.11.0/`.

| Actual-motion view | Device | Neon | Spectral |
| --- | --- | --- | --- |
| Reverb graph | [GIF](../source/build/approval-1.11.0/reverb-device-graph.gif) | [GIF](../source/build/approval-1.11.0/reverb-neon-graph.gif) | [GIF](../source/build/approval-1.11.0/reverb-spectral-graph.gif) |
| Convolution graph | [GIF](../source/build/approval-1.11.0/convolution-device-graph.gif) | [GIF](../source/build/approval-1.11.0/convolution-neon-graph.gif) | [GIF](../source/build/approval-1.11.0/convolution-spectral-graph.gif) |
| Saturator graph | [GIF](../source/build/approval-1.11.0/saturator-device-graph.gif) | [GIF](../source/build/approval-1.11.0/saturator-neon-graph.gif) | [GIF](../source/build/approval-1.11.0/saturator-spectral-graph.gif) |
| Morph external donor / Freeze | [GIF](../source/build/approval-1.11.0/spectral-morph-external-device-panel.gif) | [GIF](../source/build/approval-1.11.0/spectral-morph-external-neon-panel.gif) | [GIF](../source/build/approval-1.11.0/spectral-morph-external-spectral-panel.gif) |
| Morph internal donor / Freeze | [GIF](../source/build/approval-1.11.0/spectral-morph-internal-device-panel.gif) | [GIF](../source/build/approval-1.11.0/spectral-morph-internal-neon-panel.gif) | [GIF](../source/build/approval-1.11.0/spectral-morph-internal-spectral-panel.gif) |

Each reactive graph also has a full `-panel.gif`: **148 frames / 5.92 s**. Morph
panels have **120 frames / 4.8 s**, with changing donor, actual capture/hold/release,
FFT and mode changes. Inputs are deterministic generated PCM through the actual
processor, not microphone/DAW recordings or injected UI telemetry. Frame cadence
is 25 frames per rendered audio second, not measured desktop presentation FPS.

For each Morph `<internal|external>-<device|neon|spectral>`, `after/` contains
`spectral-morph-<source>-<theme>-editor-native-{20,70,105}.png`,
`-expanded-native.png`, `-rack-unrouted-native.png`, `-unrouted.png`, raw panel
sequences and measurement CSVs. Reactive effects have `-editor-native-{20,75}.png`,
graph/panel sequences and CSVs. Final compact/expanded/rack Neon captures and
standalone spectra in all three themes were visually reviewed.

Sessions freshly enumerated primary work `(0,0,3440,1392)` and non-primary work
`(3440,357,1920,1032)`, both 96 DPI / scale 1. New editors were placed at
`(3760,473,1280,800)`, reactive panels `(3980,523,840,700)`, Morph panels
`(3980,663,840,420)`. These are **JUCE component snapshots of native fixtures on
the verified second monitor**, not screen/framebuffer recordings or proof those
windows were unoccluded on the physical desktop. Broader legacy `editor-*.png` tests explicitly render
offscreen and carry no physical monitor claim. No user host/unrelated window was
moved. Capture/encoding installs no dependency; it uses existing ffmpeg/ffprobe.

## Historical pre-approval binary preservation

Read-only SHA256 and PE checks confirmed both packages and both per-user installed
binaries still report **1.10.0.0** and match the original delivery hashes:

| Format | Packaged = installed SHA256 |
| --- | --- |
| CLAP | `41DA7CFA95B68B2B6D3ED1289B797999056C84637882F9E85B71ED00F871658A` |
| VST3 | `FD71879DA9D21215C201885A26B6A77309EEE69BE29EA1E775F327904657FEA1` |

Only ignored `source/build/VocalStack_artefacts/Release/` binaries report 1.11.0.0.
`binary-hashes.json` records all six paths/versions/hashes. No `Windows-x64` file,
installed plugin, package, commit or remote branch was replaced or updated.

## Historical readability revision — preview, still 1.11.0

**Built and rendered; pending review. No install, package refresh, dependency
installation, commit or push.** All prior pending SIDECHAIN/Morph/shared-reactive
work remains. This revision changes presentation and its tests only; it does not
change stereo FFT work, DSP, host parameters, callback allocation policy or the
then-current always-reserved latency policy. The later coordinated revision
supersedes that policy with the prepared-graph contract documented above; the
4096-sample cost applies only to graphs declaring Morph.

### Geometry before / after

Measurements below are local pixels at the freshly observed scale 1. The original
`approval-1.11.0` images/GIFs remain untouched.

| Surface | Before | After |
| --- | --- | --- |
| Standalone Morph spectra, 828 px wide | 48 × 24 composition, approximately **17.25 × 10.25 px** blocks | All **128** actual bands; thin **1 px** connected traces and faint fills |
| Reverb 340 × 254 graph surface | 48 × 24 composition, approximately **7.08 × 10.58 px** blocks | Approximately **1.5 px** horizontal geometry / **1 px** rims, same 128 history windows |
| Actual Morph knob row | **74 px**; native slider dial area 207 × 27 | **138 px**; dial area 203 × 90 |
| Morph face diameter | **14.229 px** | **57.35 px**, **4.03×** larger |
| Stack Main Bus FX allocation | **904 × 340** | **904 × 388**, +48 px / +14.1% height |
| Fresh editor default | **940 × 1104** | **Unchanged**; saved sizes and Stack/Routing switching preserved |

The goal is finer **visual geometry**, not 8× more measured data. Morph vertices
remain 128 telemetry bands; lines between them do not imply extra FFT bins.
Waveform interpolation likewise does not invent finer sample counts. An 8×
per-axis increase of a full material raster would imply up to 64× raster work;
this implementation deliberately does **not** do that. Fluid/background/global
texture pixels remain unchanged. Waveform work is bounded to 1024 columns and
batched into connected paths, not a growing grid of per-pixel draw calls. Existing
30 Hz visibility-aware refresh and audio-thread behavior are unchanged.

Morph wraps to fewer knob columns on narrow panels and scrolls its control area;
wide panels retain one complete four-knob row. Value and caption lanes are
separate from dial hit bounds. Dense shared Creative FX rows distribute existing
spare width into actual dial cells (bounded at 110 px), with at most 96 px dial
space plus measured caption height. Sparse controls retain compact fat bars and
switches. Inspector knobs retain their larger layout and gain a little more face
area inside the same cells. Reverb's first complete control row fits the taller
rack; later groups remain deliberately scrollable rather than growing the whole
window or overlapping the footer. Outer Stack scroll extent grows by 48 px;
existing scroll anchoring and saved/default dimensions are retained.

### Final validation actually observed

All compiler/test/render/encode children used 64-bit `Invoke-HalfCpu.ps1`,
**6/12 logical processors / affinity 0x3F**. Final production changes were built
before the final full CTest and isolated editor runs. Subsequent rebuild changes
were test-assertion-only (fixed-cell expectations updated to bounded responsive
geometry). An initial shell invocation did not build; a later compile caught a
stale static rack-height assertion. Both were corrected. An early combined
build/editor command exceeded 200 s; the isolated final editor run below completed
with persistent logs. No interrupted run is counted as a pass.

Logs/artifacts: `source/build/approval-1.11.0-readability/`, except the build log at
`source/build/readability-build.log`.

| Check | Final observed result | Log |
| --- | --- | --- |
| Release CLAP + VST3 | Built successfully, preview 1.11.0.0 | `readability-build.log` |
| Full non-editor/non-granular-UI CTest | **41/41**, **175.38 s**; includes actual CLAP/VST3 hosts, sidechain and main-rack UI | `noneditor.log` |
| Isolated full editor | **77/78**, **193.23 s**; no clean pass claimed | `editor.log` |
| Isolated granular UI | **1/1**, **9.98 s** | `grain-ui.log` |
| Focused material/Morph/Creative/inspector/resize | **5/5 groups** | individual named logs |
| Actual-PCM reactive render, three effects × three themes | Passed activity, tails, Motion 0, expiry, bypass/reset assertions | `reactive-render.log` |
| Actual-PCM Morph render, internal/external × three themes | Passed actual capture/hold/release and responsive geometry assertions | `morph-render.log` |
| Encoded GIFs | **33/33**, 25 fps, native dimensions, each below 8 MiB | `gif-manifest.json`, `encode.log` |
| Before/after crops | **18** unscaled crops, original frames retained | `crop-manifest.json` |
| Delivered binary preservation | Both packages and both installed plugins retain original 1.10.0 hashes; only ignored builds are 1.11.0 | `binary-hashes.json`, `binary-verification.log` |
| Whitespace | `git diff --check` passed | terminal |

New/updated geometry assertions cover all themes, narrow/wide Creative cells,
Morph widths 190/220/320/480/840/1200, useful face minima, separate native numeric
and dial hit areas, nonoverlapping cells, scroll reach, retained inspector size,
viewport wheel/focus semantics, Stack no-overlap geometry and unchanged native
window defaults. Material tests assert 128-bin retention, 1.5 px target geometry,
the 1024-column work bound, fixed scale, silent/invalid clearing and composition.

The **sole editor blocker** remains the native Compact Stack click precondition:
`contains=0`, `peerContains=0`, target `(3961,924)`, native fixture
`(3760,473,1280,800)`, and `Chrome_RenderWidgetHostHWND` winning the Win32 hit test.
The final editor run was isolated from other test/render jobs. Nothing was skipped,
no click substituted, no unrelated window moved, and no unlock/occlusion workaround
installed. Complete native click validation still needs an unoccluded interactive
desktop. DAW listening/transport and host-native presentation FPS remain untested.

### UI timing and resource scope

Final isolated editor measurements: offscreen 480 × 240 material image creation
and composition averaged **4.34 ms waveform / 3.52 ms spectra**, 100 iterations.
All-theme actual-PCM render sessions measured **12.25–17.87 ms** per standalone
840 × 420 Morph component snapshot, including the larger controls/compositing.
These are informational local snapshots, not a real-time deadline/FPS guarantee
or audio callback cost. There is no comparable recorded timing for the original
coarse renderer, so **no measured before/after speedup or slowdown is claimed**.
An intermediate fine per-cell painter was replaced with a handful of connected
paths to avoid thousands of draw calls. Its isolated waveform snapshot measured
6.38 ms; run-to-run capture variance prevents treating that as a controlled
production-baseline comparison. No larger telemetry payload, UI FFT, audio
allocation or background grid was introduced.

### Final all-theme motion previews

These use deterministic PCM through the actual processor, not injected visual
telemetry. Reverb: 148 frames / 5.92 s. Morph: 120 frames / 4.8 s with actual
Freeze, changing donor, release and FFT/mode changes. Encodes retain native image
size and 25 fps; a 64-colour palette keeps full panels below the existing 8 MiB
review cap without shrinking the UI. They are component snapshots, not screen
recordings or proof that physical windows were unoccluded.

| View | Device | Neon | Spectral |
| --- | --- | --- | --- |
| Morph external full Stack FX row | [GIF](../source/build/approval-1.11.0-readability/spectral-morph-external-device-stack-row.gif) | [GIF](../source/build/approval-1.11.0-readability/spectral-morph-external-neon-stack-row.gif) | [GIF](../source/build/approval-1.11.0-readability/spectral-morph-external-spectral-stack-row.gif) |
| Morph external full panel | [GIF](../source/build/approval-1.11.0-readability/spectral-morph-external-device-panel.gif) | [GIF](../source/build/approval-1.11.0-readability/spectral-morph-external-neon-panel.gif) | [GIF](../source/build/approval-1.11.0-readability/spectral-morph-external-spectral-panel.gif) |
| Morph internal full Stack FX row | [GIF](../source/build/approval-1.11.0-readability/spectral-morph-internal-device-stack-row.gif) | [GIF](../source/build/approval-1.11.0-readability/spectral-morph-internal-neon-stack-row.gif) | [GIF](../source/build/approval-1.11.0-readability/spectral-morph-internal-spectral-stack-row.gif) |
| Reverb full Stack FX row | [GIF](../source/build/approval-1.11.0-readability/reverb-device-stack-row.gif) | [GIF](../source/build/approval-1.11.0-readability/reverb-neon-stack-row.gif) | [GIF](../source/build/approval-1.11.0-readability/reverb-spectral-stack-row.gif) |
| Reverb measured graph | [GIF](../source/build/approval-1.11.0-readability/reverb-device-graph.gif) | [GIF](../source/build/approval-1.11.0-readability/reverb-neon-graph.gif) | [GIF](../source/build/approval-1.11.0-readability/reverb-spectral-graph.gif) |

Concrete one-to-one Neon knob crops:
[before](../source/build/approval-1.11.0-readability/morph-knob-neon-before.png) /
[after](../source/build/approval-1.11.0-readability/morph-knob-neon-after.png).
Reverb waveform crops:
[before](../source/build/approval-1.11.0-readability/reverb-field-neon-before.png) /
[after](../source/build/approval-1.11.0-readability/reverb-field-neon-after.png).
All three themes also have `morph-spectra-<theme>-{before,after}.png` and native
full-editor, expanded, rack and unrouted snapshots under `after/`.

Capture sessions freshly enumerated primary work `(0,0,3440,1392)` and secondary
work `(3440,357,1920,1032)`, both 96 DPI / scale 1. Native editors were placed at
`(3760,473,1280,800)`; reactive panels `(3980,523,840,700)` and Morph panels
`(3980,663,840,420)`. Pure material/model snapshots and oversized legacy layout
checks are explicitly offscreen, with no second-monitor claim.

## Complete node visual audit — no redesign

The subsequent [Neon node visual audit](Node-Visual-Audit-1.11.0.md) covers all 23
non-legacy node types as 32 node/role/slot entries: **32 right-inspector clips +
17 supported Stack FX clips**, including all eight Voices, sources, routing
utilities and restored main Chorale. Multiband and non-FX routing/source nodes
are explicitly marked **NOT SUPPORTED** in Stack FX rather than omitted.

[Open all 49 individual GIFs and 14 grouped sheets](../source/build/approval-1.11.0-node-gallery/index.md).
All are 4 s / 100 frames / 25 fps, generated PCM through the real processor;
27 clips change and 22 are intentionally static. Actual Reverb/Convolution tails
and Morph external donor freeze/hold/release are verified in both surfaces.
The inventory enumerates available model choices and clearly identifies each
chosen representative; it does not claim an animation of every permutation.

Only capture fixture/tests, encoder and documentation changed in this audit.
No production UI redesign/pixelation, Morph rectangle change, performance fix,
installation, package refresh, version bump, commit or push. The read-only hash
check again preserved delivered/installed 1.10.0 and built previews 1.11.0.
Second-monitor native fixture bounds and offscreen-snapshot limitations are
recorded in the audit; no new desktop FPS or native-click pass is claimed.
