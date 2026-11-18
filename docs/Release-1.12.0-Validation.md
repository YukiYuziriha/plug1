# HEAVEN by lil grass block — v1.12.0 delivery record

## Scope

One shared automatic tuner before voice fan-out, aligned clean/tuned sources,
per-voice Tune Mix and a real themed Tuning tab. The detector and correction
implementation is embedded in `source/src/AutomaticTuner.h`; see [Tuning](Tuning.md)
and the [MXTune/Autotalent integration notice](../ThirdParty/Tuning.md).

At 48 kHz: OFF-prepared basic latency remains 1344 samples; ON-prepared is
2688 samples (1344 added). Enabling from OFF-prepared needs host audio
reactivation, not transport stop/start. The UI reports pending reactivation.
Zero retune does not remove the approximately 85 ms detector history after reset.

## Observed build and package status

- Successful Windows x64 Release build, version **1.12.0**, through
  `tools/Invoke-HalfCpu.ps1`, with verified 6/12 logical processor affinity.
- Build evidence: local `source/build/heaven-build.log` and its zero exit-code
  record. Build directories/logs are machine-local, not committed source.
- Repository `Windows-x64` CLAP/VST3 delivery files refreshed from the Release
  outputs. No installed plugin was replaced and no host was closed or terminated.
- CLAP SHA-256: `066f6c4516a2e98d8de947f27c2cda1638089d79f546382a64f761c7acc91f94`.
- VST3 module SHA-256: `e884258314428112760de173f480335bb4ebd1ffbb6479a526b098096cb1f92d`.

This is an **uncommitted working-tree build**, not a publication. Do not identify
the base HEAD as its binary-producing commit. `Windows-x64/BUILD-PROVENANCE.json`
records binary hashes, build evidence and source-input hashes. Local release ZIPs
are produced in `source/build/publication-1.12.0-heaven`; corresponding source includes
MXTune notices and pinned dependency sources. No commit, push or upload was made.

## Observed validation

All native builds and DSP/UI test executions used the resource wrapper.

- Full CTest run completed: **49/50 suites passed** (346 seconds). The editor suite
  failed; see remaining checks below. The optional legacy audio baseline passed.
- Final focused rerun: **AutomaticTuner, EngineTuning, TuningProcessor and TuningUi
  all passed**. Coverage includes pitch/scale selection, formant compensation,
  8–192 kHz DSP rates, mono/stereo, irregular blocks, allocation checks, source
  blending, latency-aligned bypass, parameter ordering, legacy state migration,
  telemetry and modulation.
- A phase-continuous 430→450 Hz input step with A-only hard correction settled
  to 440.63 Hz in the tested post-transition interval. Worst measured error in
  the first 150 ms was about **78 cents**. This explicitly demonstrates tracking
  lag; sustained-tone accuracy must not be interpreted as onset accuracy.
- Final editor rerun: **80/82 internal checks passed**, including Device, Neon and
  Spectral editor snapshots and updated nine-control voice-card counts.
- Tuning native GUI tests enumerated both connected displays and placed their
  windows on the non-primary display (1920×1080, work area 1920×1032, 96 DPI).
  Three-theme PNG snapshots are **offscreen renders**, not native screenshots.
- Packaging tooling: **18 tests passed**, covering source inclusion, stale
  source/binary refusal, wrong versions, incomplete builds and ZIP safeguards.
- `git diff --check` passed.

### Choice-dropdown UI revision

Key, Scale and Vocal Range now use a shared parameter-attached `ChoiceDropdown`.
Options use one vertical column with selected-row highlighting, up to seven
visible rows, and a scrollbar for longer lists. The list recentres on the latest
selected value after 2500 ms without scroll activity; held pointer sources block
return until release. Scrolling, hover and recentring do not write parameters.

- Release CLAP/VST3 and all affected test targets rebuilt successfully.
- Final focused CTest run: **5/5 passed** (TuningUi, TuningProcessor, CLAP,
  ModulationProcessor and MainBusFxRackUi).
- Expanded TuningUi checks pass for exact timeout boundaries, repeated/inertial
  wheel activity, multiple held pointers, release timing, one-column geometry,
  short/long lists, selected state, reverse automation and popup destruction.
- Native fixtures enumerate displays and use the non-primary monitor; popup
  placement is checked against its work area. Three-theme screenshots remain
  explicitly offscreen renders.
- Read-only independent review found no high-priority mapping/lifetime/timing
  defects. Outside clicks delivered through JUCE close the popup, but dismissal
  on another application's/non-JUCE host window's activation is not guaranteed;
  Escape dismissal is intentionally not claimed under passive keyboard handling.
- The full editor regression was not rerun for this UI revision. The two failures
  from the prior full run remain unresolved rather than being declared fixed.

### Plugin-wide UI architecture corrections

Subagents implemented focused changes following the plugin-wide static review;
see [UI architecture](UiArchitecture.md) for ownership rules and deferred work.

- Routing distinguishes document changes from presentation preferences, so
  resizing and feedback selection no longer discard graph Undo/Redo. Explicit
  layout undo remains supported without restoring stale graph/automation state.
- Inspector replacement cancels active gestures before destroying controls;
  feedback-source selection has one authoritative persisted identity.
- A shared passive-focus policy and non-assignable numeric slider lifecycle
  preserve deliberate text entry while preventing idle controls from reclaiming
  host keyboard focus. Modulation viewports follow the existing wheel policy.
- Dropdowns, Utility configuration and Tuning Formants use the shared assignment
  interaction. Tuning Enabled remains intentionally non-modulatable.
- Crossover and Tuning operational displays use coherent effective-state
  snapshots, separately from base-value editing.
- Editor parameter reveal activates Tuning or Stack as appropriate, selects
  matching rack tabs, and reveals targets through nested viewport ancestors.

Observed validation for this revision:

- Release build completed successfully through the HalfCpu wrapper. Packaged
  binaries and hashes above correspond to this architecture revision.
- Added normal CTest target `VocalStackUiArchitecture`: **10/10 internal cases
  passed**, including a notification-driven Chorale bypass case without waiting
  for the polling timer.
- Broad CTest run: **50/51 suites passed**. The editor suite exposed two routing
  regressions during integration; both were fixed and their original checks
  rerun rather than weakened.
- Final targeted rerun: **7/8 suites passed**; the editor suite is still marked
  failed, now back to **80/82 internal checks**, with only the two longstanding
  failures listed below. The new architecture, Tuning UI/state, CLAP,
  modulation/state and rack suites passed. Evidence is in local
  `source/build/ui-architecture-final-tests.log`.
- Native fixtures enumerated displays and used the non-primary monitor. Theme
  screenshots remain offscreen renders. No DAW listening/transport test is claimed.
- `git diff --check` passed. No commits, pushes, or installed-plugin replacements.

### Product rename: HEAVEN by lil grass block

- User-visible product name is **HEAVEN by lil grass block**, manufacturer
  **lil grass block**. UI headers show HEAVEN with the author credit underneath;
  clipboard/preset/routing-window labels follow the new name.
- Internal CMake target/namespace, CLAP ID `audio.vocalstack.effect`, VST3
  manufacturer/plugin codes, parameter IDs, preset extension and existing
  preferences/preset locations are retained for compatibility. This is a rename,
  not a new plugin identity.
- Current delivery names are `HEAVEN by lil grass block.clap` and the matching
  `.vst3` bundle/module. Known legacy repository delivery names are removed by
  validated package refresh; installed plugin paths are never modified.
- Built the renamed CLAP/VST3 modules, UI architecture and Tuning UI harnesses,
  plus CLAP/VST3 host smoke targets through the HalfCpu wrapper.
- Final rename CTest run: **4/4 passed** — UiArchitecture, TuningUi, CLAP and
  VST3Sidechain. CLAP checks assert both the new display name and unchanged ID;
  UI tests assert the processor name. Evidence: `source/build/heaven-validation.log`.
- Packaging tools: **23 tests passed**, including rename migration, notices,
  version/source checks and ZIP contents. `git diff --check` passed.
- The full Editor suite was last run before the branding-only change: 80/82,
  with the two existing failures below. Its new header was inspected in the
  three-theme offscreen renders, not claimed as a DAW screenshot.
- Final archives are `HEAVEN-1.12.0-Windows-x64.zip` and
  `HEAVEN-1.12.0-corresponding-source.zip` in the publication directory above.

### Remaining editor checks

The full editor suite is **not green**. Two checks still fail and are not hidden
or skipped:

1. `Slider selection, feedback, alignment, sensitivity and knob modes`: the
   existing FX texture test finds one tone in its small sampled region at Device
   theme / width 340, where it expects at least two.
2. `Compact Stack cards, fixed inspector rows and click activation`: native peer
   hit-testing reports the card point outside the visible native peer. Geometry
   assertions for the new Tune Mix pass before this native-click check fails.

The focused tuning UI suite passes, but it does not substitute for these checks.
No unrelated renderer or global native-window behavior was changed to silence
these failures.

## Validation boundaries

No real-vocal listening evaluation or DAW workflow test has been performed.
Detector history, short-window formant quality, onset tracking and CPU cost remain
practical limitations. Synthetic checks do not certify vocal quality, and no
Antares/Auto-Tune equivalence is claimed. Keep complete corresponding source and
all donor notices available alongside any binary distribution.
