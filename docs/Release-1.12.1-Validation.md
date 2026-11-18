# HEAVEN by lil grass block — v1.12.1 validation

## Implemented

First enable from an OFF-prepared instance automatically requests host audio
reactivation from the processor's 10 Hz message-thread timer. The explicit JUCE
`latencyChanged` notification does not change the current reported latency or
DSP path. Requests coalesce until host reactivation. The existing CLAP wrapper
requests restart; a reproducible, target-scoped patch to pinned JUCE 8.0.12
forwards same-latency VST3 notifications as `kLatencyChanged`. Its activation
guard updates the latency cache without requesting another restart.

`prepareToPlay()` commits the new reservation while audio is quiescent. Off
starts a smooth return to aligned clean audio and retains the reservation,
without another restart request. Re-enabling uses that reserve immediately.
The next normal prepare while Off reclaims the tuning latency. At 48 kHz the
basic path is 1344 samples OFF-prepared and 2688 samples ON-prepared; Spectral
Morph's separate routing reserve is unchanged.

Tuner storage is preallocated even on OFF prepares within its existing 8–192 kHz
range. OFF processing outside that range preserves the previous engine behavior;
this release does not expand the tuner's supported rates. Per-voice Tune Mix now
also applies in Basic mode, independently of Basic/Advanced voice geometry.

The Tuning monitor distinguishes ACTIVATING from ACTIVE/unvoiced and OFF.
Routing-only pending changes do not appear as tuning activation failures.

## Observed validation

Release CLAP and VST3 builds passed through dedicated 64-bit PowerShell with
`tools/Invoke-HalfCpu.ps1`: 6/12 logical processors, affinity `0x3F`.
Local logs: `source/build/activation-build.log`, `.exit` (`0`), and
`source/build/activation-tests.log`.

Final CTest run: **12/12 suites passed**, 104.90 seconds:

- `VocalStackCLAP`, `VocalStackVST3Sidechain`: actual built format wrappers;
  first-enable restart notification with old latency unchanged, host-emulated
  deactivate/reactivate, committed latency, no repeated restart, retained reserve
  across Off/On, and corrected output (452 Hz toward 440 Hz), clean Off output,
  and correction after re-enable.
- `VocalStackTuningProcessor`: parameter/state compatibility, timer notification
  on the message thread, delayed-host coalescing, rapid toggles, binary/text state
  restore, synchronous reentrant processor-listener prepare, mono/stereo at
  44.1/48/96 kHz, and irregular-block latency-aligned bypass.
- `VocalStackAutomaticTuner`, `VocalStackEngineTuning`: detection/correction,
  clean/tuned blends, finite output, reset, no observed callback allocations,
  Basic-mode per-voice blends and legacy OFF output at 384 kHz.
- `VocalStackTuningUi`, `VocalStackUiArchitecture`: activation/readout states,
  routing-warning isolation, existing focus/history/selection/navigation checks.
- `VocalStackDSP`, `VocalStackBaseline`, `VocalStackLeadMixBackend`,
  `VocalStackSidechainSource`, `VocalStackSpectralRouting`.

An initial new Basic-mode blend fixture failed because Module Mix also changed
the Lead-derived gain-matching reference. Holding Lead Tune Mix at zero makes
that reference identical for the comparisons. Exact endpoint/partial-blend
assertions were retained; the final rerun above passed.

The packaging tooling's **23 regression tests passed** through the same CPU
wrapper. These are packaging tests, not additional plugin/DAW listening checks.

The JUCE patch was separately checked on temporary copies for clean application,
idempotence (including timestamps), unchanged macro-disabled code, and refusal
of changed/duplicate anchors without writing the input.

Native GUI fixtures enumerated actual displays and placed their windows/popups
on the non-primary display: work area `(3440,357,1920,1032)`, DPI 96, scale 1.
Primary work area was `(0,0,3440,1392)`, DPI 96, scale 1. No user host or unrelated
windows were moved. Device, Neon and Spectral panel images were also inspected;
these `tuning-ui-offscreen-*.png` images are **offscreen renders**, not DAW or
native screen captures.

## Delivery and installation

`Windows-x64` was refreshed from the successful version-verified Release build.
Both installed formats were replaced only after exclusive-file preflight and
backing up the previous installed CLAP and complete VST3 bundle. No host was
terminated. Installed files were SHA-256 verified against the release bytes:

- CLAP: `%LOCALAPPDATA%\Programs\Common\CLAP\HEAVEN by lil grass block.clap`
  — `6d25fe1aa9b3957737a036bde349002eca6aff6711436f939e5f56a6321e225c`
- Complete VST3 bundle:
  `%LOCALAPPDATA%\Programs\Common\VST3\HEAVEN by lil grass block.vst3`
  — inner module `2efe9d9abc57381896e6fdd62f051f2e5c1231abc58aeac112221e8555d43416`
- Backup: `.cache/installed-before-1.12.1-20261118-114205`.

Local archives use the `HEAVEN-1.12.1-Windows-x64.zip` and
`HEAVEN-1.12.1-corresponding-source.zip` naming scheme under
`source/build/publication-1.12.1`. Build provenance describes an explicitly
uncommitted working-tree build, not a rebuild of the subsequent Git commit.
Corresponding source includes pinned dependencies, wrapper patches and notices.
Local packaging and a Git push do not constitute a published GitHub Release.

## Remaining limits

- Actual Bitwig/other DAW automatic restart completion and real-vocal listening
  have **not** been tested. The format harnesses emulate a compliant host; they
  verify notification and DSP behavior, not every DAW's response. A host may delay
  reactivation, and the UI stays ACTIVATING until it completes. No universal
  instantaneous/glitch-free activation claim is made.
- Wrapper-level synchronous host reentry is not exercised by the format smoke
  tests; synchronous reentry is covered at the processor-listener level.
- Detector acquisition/tracking time and vocal-quality limits remain as described
  in [Tuning](Tuning.md); zero retune is not zero detection latency.
- The full Editor suite was not rerun. The two unresolved 1.12.0 Editor checks
  (sampled FX texture and native card hit-testing) are not claimed fixed or green.
