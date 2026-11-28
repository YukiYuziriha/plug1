# Vocal Stack: Current roadmap and release history

## Planned — shared spatial DSP, Spatial EQ and Signal Placer

An unreleased Spatial EQ test build is available on the research branch; it has not been installed or merged. See the [spatial audio plan](docs/Spatial-Audio-Plan.md) and [reference tests](docs/Spatial-EQ-Reference-Measurements.md).

- [x] **Static Stereo Bell boost:** verify a standalone offline reflection network with no measured impulse responses. Match both Mid and Side on held-out overlaps, and check additional frequency/Q/gain and sample-rate settings.
- [x] **Static positive low and high shelves:** verify their shape-specific feeds into the shared Depth engine. Impulse and sweep comparisons cover 0%, 50%, and 100% Depth at three sample rates.
- [x] **Static Stereo Bell cut and one boost/cut pair:** match the direct and wet-only cut passes in both orders, with an overlapping and a separated control.
- [x] **Single positive Stereo Bell automation and tails:** match host-call Depth/frequency/Q/gain updates, short band disable/re-enable, Depth-zero dormancy/re-entry, and explicit reset. Verify full output and Depth-added Mid/Side separately at 48 and 96 kHz.
- [x] **Static multiband expansion:** implement and verify overlap normalization for simultaneous positive bells and shelves, including held-out eight-band presets and mixed Bell cuts.
- [x] **Fixed-shape multiband automation:** verify slot-ordered overlap updates for positive bells and shelves and negative Bell cuts, including four/eight-band holdouts, eligibility changes, and reset at host-call boundaries.
- [x] **Recorded voice comparison:** verify one static four-band preset on mono narration and native-stereo choir; export RMS-matched A/B pairs.
- [x] **Negative EQ and live gain crossings:** verify Bell and both shelf cuts, including overlap with a boost and live crossings through zero, against the reference and through HEAVEN's processor path.
- [x] **Research-branch test build:** one eight-band minimum-phase Spatial EQ node, with per-band Depth, no Spatial switch, and no added reported latency. Spatial work stops after zero-Depth tails fade. Legacy EQ8 is unchanged. Build and validate CLAP/VST3; benchmark zero, one, and eight active Depth bands.
- [ ] **Next — listening and delivery:** audition the A/B pairs (including an isolated sung vocal if available) and verify VST3 in-block parameter timestamps independently of host-call steps.
- [ ] **Deferred — fixed at unity in the test build.** Add per-band **Strength** for boosts: 0% mutes that band's generated space, 50% (default) is the reference match, and 100% doubles the generated signal's amplitude. Keep direct EQ and Depth cuts unchanged. Verify unity, smoothing, and headroom.
- [ ] **Signal Placer (working name):** a separate tool using the same spatial engine. Collapse incoming mono/stereo audio to 100% mono, then place that source into a new stereo scene with direct-source positioning, width and distance. Define the downmix policy explicitly because anti-phase input can cancel. Panning, width and perceived distance are distinct controls; the placer's mono behavior needs its own acceptance criteria.
- [ ] **UI later:** Signal Placer uses a top-down space with a draggable source-position dot. Left/right controls source position; near/far coordinates perceived distance cues. Width remains separately controllable. Spatial EQ may reuse this top-down view as an optional secondary view, not its main UI. Do not build the 3D UI before proving the DSP; begin with per-band Depth knobs.
- [ ] Keep reusable DSP modules composable for related and experimental effects without building a speculative general-purpose framework. Use preallocated bounded processing, smoothed controls and inactive-branch bypass; benchmark CPU rather than assuming the reference plugin's slowdown is DSP-related.
- [ ] Validate mono fold-down, frequency localization, reflection timing, transient preservation, bass leakage, automation stability and CPU across sample rates/block sizes before visual polish. Dynamic EQ and linear phase are later scope.

## Adaptive DSP and latency

- Stack and Routing share the same processing plan. An explicitly disabled module keeps its settings and wiring, but stops its DSP once bypass settles. Disconnected paths do not render. A simple chain does not pay for the vocal stretcher when no routed vocal processing needs it.
- Changes that keep latency constant apply during processing. When an enable, disable, or routing change changes the required latency, HEAVEN requests automatic CLAP/VST3 reactivation and applies the new path and reported latency together. No inactive latency is reserved for these modules. Reactivation timing is host-controlled and can briefly interrupt playback.
- The base controls determine the prepared latency; per-sample internal modulation does not schedule reactivation. Whole-plugin host bypass remains aligned to the current reported latency.
- Offline CLAP comparison against 1.13.3: one stereo Delay with the vocal modules off used 53.5–59.4% less audio-thread CPU across 44.1/48/96 kHz and 64/512-frame blocks. The current build reported zero samples, with identical latency-aligned impulse responses. A state saved by the released 1.13.3 binary recalled the custom Delay and disabled second slot. Bitwig was not used.

## Routing-node wet pockets

- First delivery: Granular owns an independent Delay insert in its wet path, before Granular's local Dry/Wet blend. Granular at 0% Wet must mute the entire pocket including Delay tails; parent bypass must return dry. Configure from the Routing node, not the Stack rack.
- [ ] Extend wet pockets to **all FX nodes**, wherever a meaningful wet signal exists: ordered insert chains, add/remove/reorder, clear ownership and navigation, independent persistent FX identities/modulation, undo/redo, preset recall and explicit nested-latency compensation.
- [ ] Permit all supported FX types inside pockets after validating their latency, tails, auxiliary inputs, bypass and recursion rules. Do not silently permit feedback cycles or uncompensated spectral/oversampled paths.
- [ ] Refine the node-only nested-chain editor; do not add pocket configuration to Stack panels.


## Implemented in 1.13.0 — HEAVEN vocal glitch slicer

Borrow [Gross Beat](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Gross%20Beat.htm)'s separation of buffer capture and launch timing, and [Stutter Edit 2](https://www.izotope.com/en/products/stutter-edit-2.html)'s one-trigger gestures. Don't build a time-curve editor or duplicate the granular rack effect. The goal is to grab the end of a word once, play three triplet hits with the last reversed, and be back on the live vocal at the downbeat.

### First version

- [x] **Host clock:** Extend `PluginProcessor`'s BPM and transport handling to read beat position (PPQ) each block. Use PPQ for grid phase and BPM plus sample rate for the in-block launch offset. Supports sixteenth, eighth-note triplet, and quarter-note launch. Clear pending launches on stop, seek, or loop wrap. Keep Immediate usable without a valid running host clock.
- [x] **Grab:** Keep a bounded, preallocated rolling buffer of the rack node's input. Pin the selected duration immediately before the trigger; don't quantize capture. Preserve the exact slice until the next valid grab, even as new audio rolls in. Empty history or no captured slice leaves the vocal unchanged. Captured audio is transient, not preset or session data.
- [x] **Fire:** Launch the pinned slice immediately or on the next selected grid line. Provide separate Grab and Fire controls and MIDI triggers; honor MIDI sample offsets and process these triggers independently of modulation. UI events are not sample-accurate. Keep capture length independent of repeat spacing.
- [x] **Ready-made fills:** Start with straight and triplet ratchets, a reverse pickup, and an octave-down varispeed burst into a cut. Keep fills one-shot and deterministic, with short fades at jumps and a return to live input at the pattern boundary. Use **Replace** for actual cuts and **Layer** to keep the live vocal underneath. The node starts off and must not add look-ahead or change reported plugin latency.
- [x] **Acceptance:** Use marked input to verify the exact slice and launch offset, repeated reuse of the same slice, reverse order, and return to live. Check straight/triplet timing across block sizes, missing PPQ, transport jumps, and both routing modes.
- [ ] **DAW audition:** Verify MIDI and grid behavior in Bitwig CLAP/VST3 and audition the triplet-to-reverse fill on a real vocal.

**After the first version plays well:** Add a compact editable lane (`Live`, `Repeat`, `Reverse`, `Repitch`, `Cut`). Defer independent pitch/time and drawn playback curves.

- [x] **Auto phrase tails:** Opt-in threshold detector grabs the tail before trailing silence, follows the launch grid, and cancels a queued Auto fill on renewed input. Does not identify phonemes or replace the manual capture.

## Historical release — 1.11.1 approved, packaged and installed

Unified Compressor plot, larger small technical text, and the Motion comparison
showcase are delivered. See [1.11.1 validation](docs/Release-1.11.1-Validation.md).
Sound algorithms, host parameter layout and prepared latency policy are unchanged.

## Historical release — 1.11.0 approved, packaged and installed

**All source-owner handoffs are complete. The user approved delivery of the entire
pending preview; matching Release CLAP/VST3 packages are installed.** See
[final validation, paths and hashes](docs/Release-1.11.0-Validation.md).
See [1.11.0 implementation, validation, latency costs and actual review GIFs](docs/Preview-1.11.0.md).
Latest core validation passed 40/40, focused validation 7/7, separate rack/granular
UI 2/2, and the additional VST3 FFT queue check 1/1 (108 matrix cases). Full editor
passed 80/81: the desktop-occlusion native-click precondition remains blocked, not
skipped. The native cost probe is also blocked; no native FPS claim is made.
All 39 real-PCM/control review GIFs are preserved. Final incremental Release
verification reported no work to do; packaged and installed CLAP/VST3 smoke passed
through HalfCpu (6/12 logical processors). Actual DAW listening/transport is unperformed.

At v1.11.0, the always-on 4096-sample no-Morph penalty was removed. Latency
was fixed per host preparation: no Morph 1344/2688 samples, declared Morph
5440/6784. In that release, latency-changing topology edits remained explicitly RESTART PENDING until
host deactivate/reactivate; transport stop/reset cannot apply them. Pending slot
type replacements that could initialize a still-running machine are rejected.
Serial Morph paths exceeding the 4096-sample graph budget remain rejected. Original host indices 0–2251 are preserved;
90 Morph controls append through index 2341; fifteen persisted Mono Audition
booleans append at 2342–2356 for 2357 total. The earlier pending 1.10.1 work is
included; preserve its [historical preview](docs/Preview-1.10.1.md).
Unchecked acceptance items below mean unperformed actual DAW checks, not missing
DSP implementation or a remaining installation gate.

### Coordinated follow-up — integrated and delivered

- [x] Source: prepared graph latency policy, explicit pending-topology UI label,
  no-Morph legacy latency, deferred modulation retirement, occupied-slot guard.
- [x] Source/tests: processor no-Morph/Morph/removal/recall/reset activation cases;
  real CLAP/VST3 wrapper baseline and active-state/reactivation latency assertions.
- [x] Source: clip-aware routing chrome/cables/material, one card synchronization
  per timer path, material-frame/structural rather than unconditional canvas ticks,
  ancestor-clipped monitor subscription, shared slot/sequence FFT and Hann cache.
- [x] Source/tests: bounded Compressor/Gate GR atomics plus coherent identity-tagged
  snapshot/history fields; 200 bins for four-second measured history.
- [x] Instrumentation: optional native `--benchmark-routing` cost probe at Motion
  0/3/100, collapsed/exposed/offscreen; no delivered-FPS claim.
- [x] All control/utility/visual source owners finished; APIs integrated after handoff.
- [x] Sole coordinator built Release and ran regressions and native CLAP/VST3
  latency/automation matrices through HalfCpu. Native GUI click and cost-probe
  occlusion failures are recorded, not represented as passes.
- [x] All-theme actual-PCM Gate/Stereo/Compressor/Morph and Output/Dry-Wet captures;
  four-second histories and 0–36 dB GR presentation recorded in 39 review GIFs.
- [x] Recorded results in Preview 1.11.0 and final release validation. After explicit
  approval, refreshed matching packages/legal notices and installed with the
  existing exclusive-access preflight; no host killed or lock forced.

### 1. Host input and first-class routing source

- [x] Expose an optional external mono/stereo sidechain bus in CLAP and VST3,
  verifying each wrapper's actual auxiliary-bus behavior rather than assuming
  parity. Define mono-to-stereo handling explicitly and preserve channel identity.
- [x] Add a first-class `SIDECHAIN` source to routing. Missing/disconnected host
  input produces silence; never substitute the main input or main mix, normalize
  its level automatically, or consume an FX slot for this source.
- [x] Include source identity, connections and disconnected state in routing
  snapshots, presets and session recall. Host bus availability remains runtime
  state, not audio embedded in a preset. Disconnect/reconnect must not invent wires.

### 2. Generic auxiliary inputs and honest latency

- [x] Extend effect input declarations and prepared routing plans with meaningful,
  named auxiliary inputs. Effects receive routed buffers through this generic
  contract, not a hardcoded direct read from the host sidechain. Internal sources
  and `SIDECHAIN` must be equally valid donors where routing permits.
- [x] Give each Spectral Morph instance independent `MAIN` and `DONOR` pins,
  processor history and freeze state. Validate pin/channel compatibility, cycles,
  duplicate wires and fan-out/reconvergence before publishing a graph. Reject an
  invalid edit without replacing the last valid running graph.
- [x] Specify actual STFT latency for each FFT configuration; align the effect's
  local dry/wet paths and report total plugin latency to the host. Account for
  donor timing and routing-path alignment. Implement branch compensation where
  supported, or explicitly reject unsupported parallel/reconvergent latency
  configurations. Never advertise zero latency to hide alignment work.
- [x] Prepare graph changes, buffers, FFT resources and delay storage off the audio
  thread; publish safely at processing boundaries and reclaim old resources off
  that thread. Define FFT-size/latency-change and reset/reprepare behavior without
  callback allocation, locks, I/O or graph rebuilding.

### 3. Spectral Morph DSP and controls

- [x] Implement an independent two-input Spectral Morph effect, not a sidechain
  switch on an existing effect. Initial modes: **Magnitude**, transferring/blending
  donor magnitudes with an explicit main-phase strategy; and **Envelope**, applying
  a smoothed donor spectral envelope for timbral transfer rather than merely
  copying fine spectral bins. Document equations and audible endpoint behavior.
- [x] Provide **Morph**, **Mode**, **FFT Size**, **Smoothing**, **Freeze**, **Mix**
  and **Output**. Morph controls transfer strength; Mix remains a true,
  latency-aligned dry/wet blend. Smooth continuous controls and make transitions
  bounded and click-resistant. Add continuous modulation only for justified,
  musically useful controls; do not imply discrete mode/FFT/freeze automation is
  continuous modulation or expand the parameter surface speculatively.
- [x] Freeze captures and holds donor analysis, with explicit capture, hold and
  release behavior independent of main-input processing. Define capture before
  donor availability, repeated capture, disconnect while held, reset and recall
  semantics. Keep held donor data separate from live analysis/smoothing state and
  specify whether capture data is persisted; never silently claim it is recalled.
- [x] Use a proper streaming overlap-add STFT with compatible analysis/synthesis
  windows, overlap and reconstruction normalization. Preserve stereo coherence
  without unintended summing or channel swaps; smooth envelopes/magnitudes over
  time and frequency as appropriate. Handle silence and spectral division safely,
  with bounded gains and finite output, not automatic level normalization.
- [x] Preallocate all working storage outside processing. Bound per-block work;
  support arbitrary host block boundaries and 44.1/48/96 kHz, with explicit
  prepare/reset, sample-rate and block-size-change behavior. No audio-thread
  allocation, locks, I/O or UI access, including freeze and control transitions.

### 4. Routing/rack presentation and measured telemetry

- [x] Show clear `MAIN` / `DONOR` pins and host sidechain availability in Routing.
  In the rack, explicitly show a disconnected donor and an **Open Routing** action.
  Do not auto-connect the main mix, create an invented donor wire or suggest that
  an unrouted donor is receiving signal.
- [x] Display main, donor and result spectra from actual bounded, coherent DSP
  telemetry, with useful scales and visible live/frozen/disconnected state.
  Transfer snapshots safely without audio-thread blocking/allocation; render on
  the UI thread at bounded rates. Do not present decorative plots as measurements.
- [ ] Verify controls, spectra and frozen-state legibility in every existing theme,
  rack and routing views, compact/expanded layouts, and preserve host keyboard
  shortcuts. Capture real screenshots for user review before installation.
  **Captures/model/PCM checks passed in all themes; the native Compact Stack
  click precondition remains blocked by external native desktop occlusion
  (earlier LockScreenInputOcclusionFrame, final Chrome_RenderWidgetHostHWND).**

### 5. Acceptance tests and gated delivery

- [x] Offline DSP: FFT/OLA reconstruction and latency alignment; Morph and Mix
  endpoints; magnitude/main-phase behavior; envelope transfer and smoothing;
  freeze capture/hold/release/reset; silence/disconnected donor; finite/bounded
  output; independent instances; modulation and transition continuity. Cover
  mono/stereo, 44.1/48/96 kHz and irregular blocks, including no-allocation checks
  during steady processing and supported transitions.
- [x] Integration: parameter/state/preset round trips and defined frozen-state
  recall, source snapshots, internal versus external donor routing, invalid pins,
  cycles, duplicate wires, graph swaps and supported/rejected latency paths.
  Verify local Mix, global Mix and bypass all retain correct dry alignment.
- [ ] Host verification in both CLAP and VST3: absent, mono and stereo auxiliary
  buses; reconnect; prepare/reprepare; plugin reload and session recall; bypass,
  global Mix, transport start/stop, sample-rate changes and irregular block sizes.
  Separate wrapper smoke tests from actual DAW listening/automation checks and
  record unverified hosts or unsupported layouts explicitly. **Automated CLAP
  nine-layout/rate and native VST3 108-case plus reconnect/rate-change coverage
  passed, including actual VST3 FFT controller parameter queues; actual DAW
    listening/transport checks remain unperformed.**
- [x] Run relevant builds, tests and performance checks through the existing
  `tools/Invoke-HalfCpu.ps1` workflow
  in dedicated 64-bit PowerShell; stop if its topology restriction fails. Update
  usage/routing/latency/state documentation and record observed results honestly.
- [x] Present actual all-theme screenshots/GIFs and results for review **before installing**.
  The entire pending 1.11.0 preview was subsequently approved for installation.
- [x] After separate approval: refreshed matching CLAP/VST3 packages and legal
  notices, verified build/package/installed hashes and PE versions, passed both
  packaged and installed module smoke checks, and installed with files unlocked.
  No DAW was terminated or lock/input protection bypassed.

### Explicitly outside this scope

Envelope followers, keyed compression, vocoders, spectral gates, alternative
phase/harmonic/detail processing and region-selective modes are future ideas,
**not deliverables** of this plan. The donor Freeze above is specifically planned
here; it does not promote the older general spectral freeze/smear ideas below to
implemented or committed broader effects. Follow the staged order: host source ->
generic auxiliary routing and latency -> DSP -> UI/telemetry -> tests and gated
delivery, validating each foundation before building the next.

## Airwindows donor-library research — RESEARCH ONLY; implementation unapproved

**Status: proposed research, not an approved implementation or dependency adoption.**
The user-supplied idea is to research potentially reusable algorithms, then return
findings for a decision before any implementation. This does not authorize source
imports, ports, replacements, new parameters/UI, dependency installation, or changes
to current DSP. Continue the existing approved roadmap independently; this research
neither replaces its priorities nor relaxes its preview/delivery gates.

### Unverified leads and deliberately small first pass

All names/spellings, exact versions, descriptions, reputation and third-party
article claims (including 2026 claims) are **UNVERIFIED leads, not established
facts**. The categories below are proposed research questions, not verified
upstream functionality or suitability. No external sources were fetched for this
planning entry; no upstream links, license conclusions or adopted dependencies
are asserted.

| User-supplied leads | Research question / possible role, subject to verification |
| --- | --- |
| TapeToTape / IronOxide | Tape-style coloration; compare the actual algorithms and versions before choosing one. |
| Spiral / Mackity / BussColors | Alternative coloration; establish meaningful differences and vocal usefulness rather than collecting variants. |
| Console family | Identify the exact proposed console algorithm/version and any encode/decode or gain-staging requirements. |
| ClipOnly | Clipping/headroom treatment; determine actual transfer, state, latency and overshoot behavior. |
| Galactic / Verbity / Cloudcoat / Clearcoat | Space/ambience candidates; verify names, topology, intended behavior and stereo/mono consequences. |
| Unspecified weird/slew algorithms | First identify exact upstream candidates and a useful vocal purpose; no open-ended catalog import. |

- [ ] Start with **two or three nonredundant candidates**, not ten imported effects.
  Provisional research slots: one TapeToTape-or-IronOxide coloration candidate,
  ClipOnly for clipping/headroom, and one Galactic-or-Verbity space candidate.
  Choose exact identities only after upstream verification; drop a slot if its
  role duplicates existing DSP or lacks a concrete vocal-use hypothesis. Keep the
  other leads as backlog research, not promised effects.

### Provenance and legal gate — before any reuse proposal

- [ ] Locate the official upstream source for each selected algorithm. Record its
  exact name, source files, version/tag or immutable commit, and retrieval date;
  distinguish original upstream code from forks, ports, bundles and commentary.
  Add original upstream links only after actually verifying them; until then keep
  this as a research task, not a guessed reference or citation to a third-party article.
- [ ] Inspect the license at that exact revision, including file-level exceptions
  and required notices. Record attribution, modification and source/binary
  redistribution obligations, and compatibility with this project's license and
  distribution model. Do not infer permission from reputation, availability or
  another algorithm's license. Resolve ambiguity before proposing adoption; no
  license or legal compatibility is currently verified.

### Per-algorithm engineering assessment — findings required, not work completed

- [ ] Record equations/topology, useful parameter ranges and input/output gain
  conventions, including any normalization, hidden gain compensation or expected
  drive/headroom. Identify smoothing requirements and automation discontinuities.
- [ ] Audit real-time behavior: allocations/deallocations, locks, I/O, lazy setup,
  bounded worst-case work and CPU cost. Map preparation and resource retirement
  outside the callback; plan regression coverage using the existing callback-malloc
  safeguards, including steady processing, automation and supported transitions.
- [ ] Assess sample-rate scaling, mono/stereo channel coupling and phase, actual
  algorithmic latency and dry/wet/bypass alignment, aliasing and any oversampling
  need/cost. Document block-size dependence and supported reset/reprepare behavior.
- [ ] Inventory state ownership, initialization/reset, denormal handling, dither,
  injected noise and random seeds. Specify deterministic regression conditions
  without silently removing sonic behavior; distinguish noisy output from defects.
- [ ] Describe a minimal fit to existing DSP ownership: JUCE-independent helpers
  in the DSP layer, `VocalEngine` orchestration, `PluginProcessor` parameter/state
  ownership, and snapshot-only UI access. No direct UI mutation of DSP, hidden
  callback setup, or speculative framework. Preserve host IDs, state recall,
  routing and the current prepared-latency contract.

### Evidence and decision gate — no automatic replacement

- [ ] Write a short per-candidate dossier with verified provenance/license facts,
  unresolved questions, engineering risks, expected CPU/latency budget and a
  recommendation: reject, defer, or request approval for a bounded prototype.
  Research approval is not implementation, dependency or release approval.
- [ ] Before any later adoption or replacement, require level-matched A/B against
  current DSP using real vocals, blind where possible. Record matching method and
  remaining perceived-loudness limitations; do not mistake louder for better.
  Include mono compatibility, stereo phase, gain staging/headroom, automation,
  48/96 kHz and irregular blocks, plus other currently supported rates/layouts.
- [ ] A separately approved prototype must demonstrate finite output, silence,
  reset/reprepare, deterministic regressions, no callback malloc/locks and honest
  latency/CPU/aliasing results. Keep automated checks distinct from listening
  evidence. **Do not replace current processing without this A/B evidence and
  explicit approval.** No build, test, package, install, commit or push is part of
  this documentation-only update; later approved execution retains existing
  HalfCpu, compatibility and delivery requirements.

## Historical status — 1.10.0 approved, built, packaged and installed

- [x] Deliver the entire approved preview: synthetic-space Convolution, Saturator,
  real audio-reactive visuals, linear global Motion (3% base), clustered material
  and glints, 0.8 background strength, square chrome/circular knobs, fat 3 px
  slider material, 10 px voice rails, sparse FX controls and themed preset menus.
- [x] Preserve repeated/duplicated Dry/Wet utilities, shared inspector control
  layout, 14/22 px scrollbars, chunky square Routing nodes, double-weight
  connections and shared Stack layout constants.
- [x] Preserve 2087 existing host indices; append 165 controls (2252 total).
- [x] Final incremental Release build; both Windows PE versions corrected to
  1.10.0 with a durable generated-resource dependency and CLAP resource linkage.
- [x] All 38 CTest suites passed in split runs: final 36/36 (130.24 s), isolated
  75-group editor (116.89 s), and three isolated grain UI passes (27.05 s).
  Two parallel 37-suite runs failed the grain UI concurrent-reader assertion;
  this is not a clean combined test pass. No unrelated concurrency fix was made.
- [x] Seven render modes passed (441 offscreen PNGs); packaged/installed CLAP
  descriptor version, state, latency, bypass, automation and layout smoke passed.
- [x] Refresh tracked Windows CLAP/VST3 and legal/font notices; existing installer
  preflight succeeded for both destinations, then both installed successfully.
  Independent build/package/installed SHA256 and PE 1.10.0 checks passed.
- [ ] Actual DAW listening, transport, automation/recall, VST3 host validation and
  native FPS measurements remain open.

See [1.10.0 delivery evidence and hashes](docs/Release-1.10.0-Validation.md).
Preview documents remain historical records, not current installation gates.
SIDECHAIN / Spectral Morph was not included in this 1.10.0 delivery; its implemented 1.11.0 source preview and remaining gates are recorded above.

## Historical status — 1.9.6 built, tested, packaged and installed

UI-only: no DSP or host parameter changes.

- [x] Increase the background grid's longest edge from 40 to 80 cells for
  approximately half-sized pixels; use denser multiscale fluid noise and grain
  with stronger brightness variation. Keep theme tint muted: saturation capped
  at 0.18 and tint blend 0.12, not a vivid gradient.
- [x] Use denser long FX Mix noise with 2 px texels instead of 3 px.
- [x] Place normal-font `Mix NN%` inside the 25 px bar, with reverse contrast
  over the filled portion; reduce Mix rows from 94 to 44 px.
- [x] Make the caption a nonclickable, mouse-passthrough label. Clicking or
  dragging over its text adjusts Mix without numeric entry or keyboard focus.
  Keep numeric entry for all other knobs and voice controls unchanged.
- [x] Full Release build succeeded; all 34 focused CTests passed in 161.06 s,
  including 68 editor groups and rack UI.
- [x] Review Neon Reverb and Spectral Granular racks plus Neon/Device full editors.
- [x] Copy Release CLAP/VST3 packages to tracked `Windows-x64` and refresh
  `VocalStack-FX-Tabs.png`.
- [x] Packaged CLAP smoke: 2087 parameters at original indices; stereo 48 kHz,
  mono-to-stereo 44.1 kHz with 1344-sample latency, and mono 96 kHz with
  2688-sample latency passed.
- [x] `install-release.ps1`: both per-user formats reported **Installed and
  SHA256 verified**.
- [x] Commit and push the release with matching packages: `cd54c85` on `origin/main`.
- [ ] Actual DAW visual/listening/transport/automation/recall and native FPS checks.

The long legacy DSP suite was not rerun. Screenshot review is not actual DAW
validation. See [1.9.6 validation](docs/Release-1.9.6-Validation.md) for package hashes.

### Historical 1.9.5 — built, tested, packaged, installed and pushed

UI-only: no DSP or host parameter changes. Release source and matching packages
are committed as `e9f2b48` and pushed to `origin/main`.

- [x] Flat 25 px textured long FX Mix bars: theme-colored coherent mineral noise
  at coarse 3 px scale, cached at 15 Hz; exact marker and separate upper modulation
  lane. Compact voice sliders and knobs remain unaffected.
- [x] Whole-plugin coarse fluid backgrounds on root/canvas/routing and FX surfaces:
  maximum 40-cell grid dimension, frame-proportioned with features growing with
  the frame; slow 10 Hz cache. Slight theme tint only, saturation capped at 0.18,
  brightness matched and blended at 12% so the base dominates, not a vivid gradient.
- [x] Reuse the existing 30 Hz editor timer, with no new timer; pause the animation
  clock while hidden and repaint the root at 10 Hz.
- [x] FX-only 68 px control cells plus 2 px gaps, reduced group gutters and
  switch/dropdown gaps; 94 px Mix rows accommodate the 25 px bar, 28 px caption,
  value and upper modulation lane.
- [x] Initial editor validation: 68 groups passed in 90 s.
- [x] Full Release 1.9.5 build; all 34 focused CTests passed in 225.47 s, including
  68 editor groups. Independent static review found no blockers.
- [x] Review Neon Reverb rack, Spectral Granular, and Neon/Device full-editor
  screenshots; granular presentation remained stable.
- [x] Copy Release CLAP/VST3 artifacts to tracked `Windows-x64`, record SHA256
  identities in the validation report, and refresh `VocalStack-FX-Tabs.png`.
- [x] Packaged CLAP smoke: 2087 automatable parameters at original indices;
  stereo 48 kHz, mono-to-stereo 44.1 kHz, 1344-sample latency; mono 96 kHz with
  2688-sample latency; state/latency/bypass/automation/offline-layout checks passed.
- [x] Installation retry succeeded: `install-release.ps1` reported **Installed and
  SHA256 verified** for both per-user CLAP and VST3. Initial CLAP file lock resolved.
- [x] Commit and push the release with matching packages: `e9f2b48` on `origin/main`.
- [ ] Actual DAW visual/listening/transport/automation/recall and native FPS checks.

The long legacy DSP suite was not rerun. Screenshot review is not a real-DAW
listening or FPS result. Packaging, smoke and both SHA256-verified per-user
installations are complete. Release commit `e9f2b48` is pushed to `origin/main`.
See [1.9.5 validation](docs/Release-1.9.5-Validation.md).

### Historical 1.9.4 — built, validated, packaged and installed

Title-sized FX tabs (4 px title padding per side, 2 px gaps), variable-width
reorder hit testing, reliable tab relayout and matching 12.5 px knob name/value
text shipped. Release build and 34 focused CTests passed in 209.76 s, including
67 editor groups and rack UI. Final Neon/Spectral images were reviewed; packaged
CLAP smoke passed (2087 parameters, indices intact), and both per-user formats
were installed and SHA256-verified. Actual Bitwig checks remained unverified;
the long legacy DSP suite was not rerun.

Historical package SHA256:
- CLAP: `fb2bd048ca4426e4cbb879f1b5d60aed21d42bce43723eed889eba6292a79c6f`
- VST3: `1f45b74a82fa368a6a83032434660e4baa86d2e8078e2b98a608ac7ae422205c`

### Historical 1.9.3 — built, validated, packaged, installed and pushed

This section is the current roadmap. Release records below describe what shipped
in each version; the original design at the end is historical, not a fresh task
list. When requirements conflict, the latest release record takes precedence.

### 1.9.3 — tabbed Main Bus FX and responsive effect editors

UI-only scope: no audio DSP, host parameter IDs/counts, ranges or defaults change.

- [x] Bump version to 1.9.3.
- [x] Replace simultaneous Main Bus FX cards with tabs for the actual Routing
  nodes; click a tab to show one full-width editor, not a duplicate processor.
- [x] Drag tabs to reorder through Routing's shared one-step Undo/Redo path;
  retain per-instance bypass/remove controls and the + FX add workflow.
- [x] Retain surviving editor instances and scroll state across selection and
  reorder; automatically reveal the selected tab when the strip overflows.
- [x] Reflow fixed compact knobs responsively, with numeric values above and names
  below, horizontal Mix sliders and compact two-state controls.
- [x] Use graph-left/controls-right layouts in wide Creative, Tone and Utility
  editors, with stacked layouts at narrower widths.
- [x] Extend editor and rack UI regression coverage. Initial editor run passed
  61/67: layout classification mistook hidden Utility sliders for Mix rows, and
  old geometry assertions needed updating. Fixed both; rerun passed 67/67,
  then all 34 focused CTests passed in
  177.04 s, including editor groups and rack UI. Release build succeeded.
- [x] Independent static review found no blockers.
- [x] Following image inspection, give the rack its own `PanelStyle` to address
  standalone tab On ellipsis/flat-theme rendering.
- [x] After the final `PanelStyle` fix, full Release build succeeded and all
  34 focused CTests passed in 176.42 s, including 67 editor groups and rack UI.
- [x] Review rack Reverb Neon, EQ Neon, Granular Spectral and Routing Reverb
  images; compact tab On is now readable. Add tracked `VocalStack-FX-Tabs.png`.
- [x] Copy Release CLAP/VST3 to tracked `Windows-x64`; record package SHA256 hashes
  in the validation record. Packaged CLAP smoke passed 2087 automatable parameters
  at original indices, stereo 48 kHz, mono-to-stereo 44.1 kHz and latency 1344 samples.
- [x] Run `source/build/install-release.ps1`: both per-user CLAP and VST3 reported
  **Installed and SHA256 verified**.
- [x] Commit and push source and matching packages: `5468939` on `origin/main`.
- [ ] Actual Bitwig visual/listening/transport/automation/recall and native FPS checks.

See [Release 1.9.3 validation](docs/Release-1.9.3-Validation.md) for evidence
and package hashes. The long legacy DSP suite was not rerun; actual host checks remain
unverified. **1.9.3 is now installed; 1.9.2's record remains historical and does not
validate 1.9.3.** Release commit `5468939` is pushed to `origin/main`.

### Historical 1.9.2 — parameter graph restored; validated and installed

- [x] Version bumped to 1.9.2; presentation-only change, no DSP changes.
- [x] Restore `CreativeEffectVisual`'s original-style fixed overlapping parameter
  envelope curves, grid gradients and three-second history/read-window guides.
  The original graph was wanted with visual polish, not a flickering live cloud.
- [x] Derive geometry from coherent parameters, not audio telemetry. Keep the
  graph visible unprepared, stopped and without audio; no active-grain auto-zoom
  or cloud scatter. Label it a parameter illustration, not a recorded waveform
  or measured grain telemetry. The DSP snapshot bridge remains backend-available.
- [x] Coordinator reports Release CLAP/VST3 builds succeeded. The combined
  build/test command timed out at 600 s after building, during tests; separate
  `ctest` without rebuilding passed all 34 focused tests in 182.97 s, including
  66 editor groups. Grain UI passed in 11.37 s: unprepared/stopped graph,
  snapshot-count changes, retained model on failed coherent read and all-five-FX
  coherence across three themes. No audio DSP changes; legacy DSP not rerun.
- [x] Coordinator manually reviewed all six
  `.cache/update-ui-review/grain-parameter-{device,neon,spectral}-{174,440}.png`
  screenshots; root Granular and Stack FX previews updated from current Release.
- [x] Copy CLAP/VST3 packages and SHA256-match Release artifacts; packaged CLAP
  smoke passed 2087 parameters, latency, bypass and automation.
- [x] Complete installation retry: coordinator reran
  `source/build/install-release.ps1`; both per-user CLAP and VST3 reported
  **Installed and SHA256 verified**. 1.9.2 is installed.
- [x] Release commit `c1ee5c7` pushed to `origin/main`.
- [ ] Actual Bitwig visual/listening/transport/automation/recall and native FPS checks.

See [Release 1.9.2 validation](docs/Release-1.9.2-Validation.md). This documentation
pass runs no build/tests and performs no packaging, installation or commit.

### Historical 1.9.1 — UI fixes validated; installation retry succeeded

- [x] Horizontal 204 px Main Bus FX cards beside the roughly 172 px voice-card
  scale; full parameter UIs with internal vertical scroll, fixed headers and no
  Details step. Stack uses an unscaled 940x1148 body viewport, fixed preset footer
  and unchanged 940x1104 default window.
- [x] Shared `PassiveViewport` hover-wheel routing: nearest movable vertical
  ancestor first, outward edge propagation, horizontal fallback only without a
  vertical range, and Shift-wheel for horizontal navigation. Main numeric labels
  forward wheel events without parameter edits or focus capture. Embedded EQ uses
  plain wheel for scrolling and Alt-wheel for Q/depth with complete gestures.
- [x] Actual granular cloud framed by valid capture/read positions rather than
  32 fixed rows; sparse/dense envelopes retain slot identity and direction. Delay
  is a parameter-timed animated schematic, not measured echoes; hidden/clipped or
  bypassed clocks pause. Offscreen Sync still updates Time/Beats enablement.
- [x] Immediate coherent-snapshot XP cutoff/resonance response; utility timer
  60 Hz, formerly 20 Hz. Synth drops the 100 ms request throttle but retains the
  actual 32768-sample DSP curve on the shared bounded worker, not a fake preview.
- [x] CMake version already 1.9.1; no audio DSP changes in this patch.
- [x] Add three editor groups (66 total): hover-wheel regression, narrow Tone-card
  layout and filter-response/scheduler coverage; rack coverage includes 354
  real-hit-tested wheel cases.
- [x] Correct test assumptions: oversized Tone text checked in scroll slices,
  Delay `refreshAt` deterministic animation/exact snapshot comparison, EQ Alt-wheel
  assignment. Coordinator reports all 34 focused CTests passed in 154.15 s,
  including 66 editor groups and eight factory-audio shards.
- [x] Remove duplicate WaveMix control; full Release rebuild succeeds, followed by
  targeted Editor/rack/grain UI/CLAP 4/4 passes in 81.50 s (Editor 66/66, 81.27 s).
- [x] Refresh tracked CLAP/VST3 packages and SHA256-match Release artifacts;
  packaged CLAP smoke passes 2087 parameters, latency, bypass and automation.
- [x] Render 33 screenshots plus rack-row views in three themes; inspect row and
  grain/Delay views and update root images. Informational 26-node/120-frame UI
  profile median/p95/max: compact 9.081/13.008/49.161 ms, expanded
  14.755/22.607/37.176 ms, at most 12 mounted cards—not a native FPS guarantee.
- [x] Complete installation retry: coordinator reran
  `source/build/install-release.ps1`; both per-user CLAP and VST3 reported
  **Installed and SHA256 verified**.
- [x] Release commit `fa44543` pushed to `origin/main`.
- [ ] Actual Bitwig listening/transport/automation/recall and native FPS checks.
  No audio DSP changes; legacy DSP was not rerun (1.9.0 pass remains historical).

See [Release 1.9.1 validation](docs/Release-1.9.1-Validation.md) for evidence and
pending work. The completed 1.9.0 results below are historical, not 1.9.1 approval.

### Previously delivered

- [x] Editable Lead and up to eight generated voices; independent pitch, formant,
  gain, pan, delay and motion controls, plus the measured Voice Motion monitor.
- [x] Authoritative audio routing, independent FX instances, automatic compensated
  fan-in, band-aware recombination, cable editing and graph undo/redo.
- [x] Chorale, EQ8 with live spectra, 2-/3-band splitter, and 19 waveshapers.
- [x] Classic LFO/Random modulation, assignments and effective-value feedback.
- [x] Compact Stack sliders, independent right node inspector with adjustable
  width, and compact LFO controls in a vertically resizable bottom panel.
- [x] Delay, Reverb, Compressor, Bitcrusher and Granular routing nodes.
- [x] Whole-patch factory/user presets, user-selected local save folder and
  one-step Undo load. Eight original starting points are included.
- [x] Reverb pitched feedback (including octave down), shimmer and wet swell.
- [x] Gain, Stereo Utility and Gate/Expander nodes with measured gate GR.
- [x] XP-inspired original character filter with 15 configurations and graph editing.
- [x] Synth Filter: actual upstream-derived DSP, 13 configurations; not category
  stand-ins or a claim of bit-exact full-synth parity. GPL attribution retained.
- [x] Themed reverb/delay/granular/compressor/crusher models and waveshaper graphics.
- [x] Flattened Neon surfaces (knobs retained), third charcoal/lavender Spectral theme.
- [x] Release CLAP/VST3 packages, 19 focused suites, 48 editor groups, legacy DSP
  suite, actual three-theme screenshots and paired CPU measurements for 1.7.1.
- [x] Half-speed relative horizontal sliders, no initial jump; unchanged rotary
  sensitivity, numeric entry, reset and modulation-depth assignment gestures.
- [x] Compact full-band routing provenance, cached FFT tables and exact filter
  coefficients; no reduced quality settings. 14.4 million captured floats matched
  the pre-optimization DSP bit-for-bit across the benchmark scenarios.

### 1.8.0 — expandable routing delivered

UI-only scope: no audio DSP, host parameter IDs/counts or parameter ranges changed.

- [x] Sticky inspector hide/show arrow stays reachable; selecting nodes or cables
  does not reopen a hidden inspector.
- [x] **Expand all** opens supported cards and hides the inspector; **Collapse all**
  closes them and shows it, including after a manual hide. Individual **+ / −**
  toggles leave inspector visibility unchanged and support mixed card states.
- [x] Reused in-node controls/visuals edit the same underlying values: Lead/Voice
  controls, FX panels and enable, Chorale Mix/Motion, Mix balance and Output trim.
- [x] Separate compact/expanded position banks keyed by stable node IDs. Moves in
  one bank do not affect the other; toggling restores remembered manual positions.
- [x] Deterministic first-use packing moves only the new card downward past
  neighbours. Subsequent expansion restores its saved position without repacking.
- [x] Persist inspector visibility, expansion states and both position banks in
  instance layout. Bulk expansion/collapse is one layout Undo step; layout-only
  history does not republish topology or replay parameters.
- [x] Expanded controls follow pan/zoom; headers, ports and cables retain world-space
  geometry. Controls remain passive for idle DAW keys and reuse parameter bindings.
- [x] Virtualize collapsed, hidden and offscreen control bodies rather than keeping
  their timers/monitors alive. Recreate bodies when they return onscreen.
- [x] Synth plots share one background curve worker across editors: one active
  render, at most 17 pending requests, coalesced latest targets per client and
  queued-work cancellation on unmount. No worker per expanded filter.
- [x] Cancel gestures before control teardown, including interrupted drags and
  unfinished numeric entry. Regression tests cover hide, offscreen pan and close.
- [x] Final Release validation: all 19 focused CTest suites and 57 editor groups pass.
- [x] Profile expanded large patches: 26 nodes (15 FX, eight voices, Lead, mixer,
  Output), Device theme, 1600x1000, 100% zoom, 120 serial pan/timer/snapshot frames
  per pass. Compact median/p95/max: 6.951/8.750/36.264 ms; expanded:
  12.390/16.972/34.411 ms, at most 12 mounted cards. Pacing sleep excluded;
  includes mounting and snapshot allocation, background curve work may overlap.
  Informational UI timings, not native presentation FPS or audio deadline safety.
- [x] Render 12 expanded views across three themes and 15 existing update views;
  publish `VocalStack-Expanded-Routing.png`.

### 1.9.0 — IMPLEMENTED, BUILT, PACKAGED AND VALIDATED

Release CLAP/VST3 builds and all **34 focused CTests passed in 146.69 s**, including
eight factory shards and 63 editor groups. Separate legacy DSP passed in 81.8347 s.
Packages match Release artifacts by SHA256 and packaged CLAP smoke passed.
After the user closed Bitwig, the coordinator reran
`source/build/install-release.ps1`; both per-user CLAP and VST3 reported
Installed and SHA256 verified. Actual Bitwig checks remain open.
Release commit `531045b` is pushed to `origin/main`.
See `docs/Release-1.9.0-Validation.md` for evidence and performance limitations.

#### Routing, modulation and controls — IMPLEMENTED

- [x] Bounded deterministic 2D expansion packing uses horizontal space and wraps;
  bulk expansion plans final sizes together. Separate compact/expanded banks
  preserve remembered/manual moves; single expansion reveals its target.
- [x] Modulator rail is 66 px wide with 44 px stride (41 px visible tiles), down
  from 132/88. LFO previews use the DSP evaluator for waveform, phase, skew,
  polarity and amount; Random retains actual published history on surviving tiles.
- [x] Always-reachable sticky bottom modulation hide/show persists independently
  from the right inspector, retaining selection, Assign and idle DAW shortcuts.
- [x] Continuous parameter controls are knobs except Stack Lead/voice sliders.
  Switches/menus remain discrete. Stack has one light base thumb, a separate
  upper-lane effective triangle and assigned range on the primary track; duplicate
  eligibility rail/pointer misalignment fixed. Numeric entry, reset, half-speed
  relative dragging and assignment gestures retained.
- [x] Splitter: one input, identical full-level A/B outputs, independent cables,
  source/band provenance retained, no FX slot or host parameters. Not a crossover
  or -6 dB split. Backend, rewiring and state/preset coverage added.

Limits stay **48 nodes, 16 effects, 128 links, eight voices plus Lead**.

#### DSP audit and repairs — IMPLEMENTED

- [x] Inventory, reproducible before/after level/decay probes and keep/repair
  decisions in `docs/DSP-Audit.md`. Pinned source/license inspection of Clouds,
  JUCE/Freeverb, Dragonfly/Freeverb3 and Signalsmith; retain existing algorithms.
  No new upstream code/dependency, equivalence or comparative quality claim.
- [x] Reverb: measured fixed **4x wet-output calibration (+12.0412 dB)**, independent
  of ducking. Keep FDN and linear blend; no AGC, new limiter or concealed dry feed.
  Add quantitative level/endpoint/stability tests; distinguish nominal RT60 from
  measured broadband decay. Existing patches/freeze/shimmer are louder at the
  same Wet setting; duck default stays 6 dB. Factory headroom/reproducibility
    passed the documented 48-case fixture; no trim changes are evidenced. This is
    not arbitrary-input headroom certification or subjective audition.
- [x] Granular: track captured history, clamp lookback, validate interpolated and
  reverse reads in partial/frozen buffers. Boundary grains hold their last sample
  and retire over 2 ms. Retain bounded 32-grain scheduling, windows, stereo balance
  and normalization; sparse gaps/aliased pitch remain explicit, not a Clouds port.
- [x] Actual grain snapshots flow through RoutingExecutor, VocalEngine and Processor
  once per host block. Preallocated bounded lock-free ownership bridge drops
  contended frames; identity/generation, revision and reset epochs reject stale
  reset/removal/reuse frames. Routing/Stack share live all-theme rendering of
  positions, progress/envelopes, direction/pan and valid/frozen capture state.
  Hidden/clipped refresh is skipped; no extrapolation or waveform copying.
- [x] Compressor meter uses actual smoothed linear gain before makeup, not a
  separate dB target; analytic transfer/transient regressions added. Audio transfer
  unchanged. Remaining effects/shared engine audited with limitations documented.

#### Stack Main Bus FX rack — IMPLEMENTED

- [x] Replace fixed Chorale with **+ FX**, compact same-node cards, enable/remove
  and drag-handle reorder. Duplicates have independent parameters and share the
  Routing FX pool. Controls/modulation/visuals are reused; structural Splitter and
  Multiband remain Routing-only.
- [x] Discover only the unambiguous main Mix -> serial FX -> Output segment before
  trim. Never flatten custom/parallel graphs; explain disabled topology actions
  while identifiable FX remain editable. Per-voice FX are not main-bus FX.
- [x] Shared `RoutingPanel::commitExternalGraphEdit()` bridge now exists, with the
  existing Undo/Redo and lifecycle parameter/modulation snapshots. Restoration
  merges live unrelated assignments and sources. All removed effect cache IDs
  are purged to prevent old tails on immediate re-add; surviving IDs/types retain
  history. Editing parameters or layout alone does not republish topology.
- [x] Backend/UI and shared-undo integration coverage added for duplicates/capacity,
  reorder/removal, refusal and cross-view state restoration; integrated coverage
  passed in the final 34-suite run.

#### Release validation and installation — COMPLETE; actual host checks open

- [x] Update 1.9.0 README, plan and DSP audit with accurate validation boundaries.
- [x] Release CLAP/VST3 build; all 34 focused CTests pass in 146.69 s, including
  eight factory shards and 63 editor groups. Separate legacy DSP passes in 81.8347 s.
- [x] Render and visually inspect 33 screenshots plus 12 rack PNGs in all themes.
  Subsequent test-only screenshot output adds three actual-grain PNGs;
  GrainSnapshotUi rerun passes in 0.60 s. Refresh root Granular and Stack FX images.
- [x] Record UI median/p95/max: 7.990/12.903/41.000 ms compact,
  13.448/20.044/44.231 ms expanded, at most 12 mounted cards.
- [x] Complete informational full-processor/multiple-instance performance probe;
  register `VocalStackReleasePerformanceProbe`. Heavy small-block deadline overruns
  are documented, not a real-time acceptance pass; further host profiling remains.
- [x] Complete factory synthetic headroom/reproducibility review: 48 cases and 96
  fresh renders, 8/8 scratch shards pass in 109.14 s. No trim changes evidenced;
  retain -3 dB trims and Shimmer Bloom -6 dB. Not a subjective listening comparison.
- [x] Register all eight factory CTest shards in production CMake.
- [x] Final integrated 34-suite outcome recorded: all pass, 146.69 s.
- [ ] Perform subjective preset/real-vocal audition and actual host dropout review;
  reproducible synthetic headroom results do not complete these checks.
- [ ] Perform actual Bitwig listening, transport/tempo, automation and recall.
  **Actual Bitwig listening/transport remains unperformed.**
- [x] Copy Release artifacts to tracked CLAP/VST3 packages and verify SHA256 match;
  packaged CLAP smoke passes 2087 parameters, mono/stereo latency, bypass/automation.
- [x] Complete installation retry after the user closed Bitwig: coordinator reran
  `source/build/install-release.ps1`; both per-user CLAP and VST3 reported
  Installed and SHA256 verified. Never terminate Bitwig to force replacement.

Release commit `531045b` is pushed to `origin/main`.

### Implementation boundaries

The approved 1.7 feature scope is implemented. XP is original cascaded TPT SVF
DSP, not Bitwig source or a measured XP clone. Synth Filter adapts pinned Vital
sample equations, validated against retained upstream class bodies through a
single-lane framework shim; it is not a complete synthesizer/SIMD/oversampling
port. See `ThirdParty/Vital/README.md` for exact mappings and fidelity limits.
UI artwork and presets are original, not copied upstream assets.

Reverb and other non-granular plots remain parameter schematics, not measured
IRs. Granular now shows actual bounded DSP snapshots in Routing and Stack.
Synth small-signal plots render actual adapted DSP on a bounded background job,
cache/interpolate FFT bins, and are approximate for nonlinear/moving-delay models.

### Historical validation and remaining host checks

- [ ] Listen in Bitwig to the five effects and factory patches on real vocals;
  check wet/dry balance, ducking, grit, granular transitions and freeze/unfreeze.
- [ ] Exercise Bitwig transport, tempo changes, automation and session recall.
- [x] Historical 1.7.1 profiling: eight pitched voices, ten serial FX
  including pitched reverb/swell/formant, four modulators, stereo/137 frames.
  Paired Release medians changed from 548/1143 to 420/861 ms per audio second
  at 48/96 kHz on this machine (~23–25% less CPU). Average 96 kHz throughput is now
  below real time, but maximum observed blocks still missed 137-sample deadlines.
  This is not a universal CPU/glitch-free guarantee; see README for methodology.
- [x] Complete 1.8.0 Release/package verification: CLAP/VST3 packages match built
  artifacts by SHA256; packaged CLAP host smoke passes.
- [x] Install 1.8.0 as requested: per-user Common CLAP/VST3 binaries replaced while
  unlocked and SHA256-verified. Bitwig was not terminated.

For 1.8.0, all 19 focused CTest suites and all 57 editor/state groups pass on the
final revision. The separate legacy DSP suite was not rerun for this UI-only
release; DSP sources and host parameter definitions are unchanged.

Historical 1.7.1 validation: all 19 focused suites (48 editor groups), the separate
legacy `VocalStackDSP` suite and packaged CLAP smoke passed. Fifteen actual editor
screenshots cover five views in all three themes. Those package/performance records
are not 1.8.0 results. Actual Bitwig listening/transport checks remain unperformed;
no installation was performed.

### Explicitly deferred / outside current scope

- Negative-formant **Preserve Clarity** remains a listening-evaluation idea, not
  approved implementation scope. Do not silently change formant sound or ranges.
- General nonzero-integer-latency branch compensation is not implemented;
  unsupported latency-producing routing is rejected.
- External reference-track EQ analysis, full proprietary-device parity and a
  complete Thermal-style multiband/macro environment are not promised.
- Spectral freeze/smear, a dedicated stutter buffer and tuned resonators were
  discussed as ideas but are not committed roadmap items. A frequency shifter
  was dropped from the recommended vocal-focused direction.
- Backward compatibility is not a priority for new work per the latest user
  direction. Existing compatibility code need not be removed, but new migration
  infrastructure is not a deliverable unless separately requested.

## Release history

These records retain the implementation and validation details of each release.
They are not pending checklists; later records supersede earlier design choices.

### 1.7.1 — performance and precise slider dragging

- Optimized full-band routing provenance while retaining the existing multiband
  compensation path. Cached harmony FFT tables and unchanged filter coefficients.
- No FFT-size, hop-size, oversampling, control-rate or grain/tail quality reduction.
  14.4 million captured float samples matched pre-optimization output bit-for-bit;
  cache/uncached automation equivalence and realtime safety tests passed.
- Heavy full-patch paired Release medians: 548→420 ms/audio second at 48 kHz,
  1143→861 at 96 kHz. Average throughput improved ~23–25%; small-block deadlines
  remain a concern. Extreme 15×4x waveshaper stress remains over real time.
- Horizontal sliders drag at half cursor speed relative to the initial value;
  no track-click jump, with full-range numeric entry/reset and unchanged knobs.
- 19 focused suites, 48 editor groups and separate legacy DSP passed. Refreshed
  CLAP/VST3 packages; no installation or Bitwig listening/transport test claimed.
- Recorded the expandable-node proposal, subsequently implemented for 1.8.0.

### 1.7.0 — character filters, utilities, shimmer and themed models

- Added Gain, Stereo Utility, Gate/Expander, XP Filter and Synth Filter routing
  nodes with host/state/modulation/preset integration; 2087 total parameters.
- Synth DSP is a GPL-covered scalar adaptation of pinned upstream equations:
  Digital, Ladder, Dirty, Diode, Analog, Comb and four-formant models, 13 modes.
  Retained upstream source/license/hashes and independent class-body comparisons.
- Reverb adds pitch −12…+12 st, pitched feedback 0–100% and wet swell 0–2000 ms.
  Pitch feedback replaces rather than adds the bounded branch; freeze fades it out.
- Added effective-target schematics and background-cached DSP response curves;
  measured gate GR and pre/post spectra remain separate from schematic curves.
- Added flat Spectral skin and unified Neon surfaces while retaining Neon knobs.
  Full-patch factories now include Clean Vocal Utility, Vocal Filter Motion and
  Shimmer Bloom in addition to the five existing original vocal patches.
- Release build and 19 focused suites passed, including 47 editor groups; separate
  legacy DSP passed. CPU stress caveat and upstream fidelity limits are documented
  above and in README. Updated CLAP/VST3 packages and real-editor previews.
- Not installed; no real Bitwig listening or transport verification claimed.

### 1.6.0 — creative vocal effects and whole-patch presets

Implemented and Release-built:

- Five independent routing machines: Delay, Reverb, Compressor, Bitcrusher and
  Granular, with compact grouped sliders, modulation and host automation.
- Delay: tempo/free timing, ping-pong that alternates centered mono vocals,
  filtered/saturated feedback, pitched repeats and ducking.
- Reverb: eight-line diffuse FDN, separate size/RT60, predelay, early reflections,
  tone/damping, modulation, width, ducking, saturation and freeze. Pitch/shimmer
  and swell are not implemented.
- Compressor: stereo-linked peak detection, knee, timing, detector high-pass,
  manual makeup and real gain-reduction readout; no lookahead.
- Crusher: independent bit/rate reduction, drive, gated TPDF dither and postfilter;
  intentional aliasing. Granular: 32 bounded grains, pitch, reverse, freeze,
  lookback/scatter, stereo spread and overlap-aware level normalization.
- Full-patch preset footer: complete parameters, routing, positions and modulation;
  choose a machine-local user folder, save new files, browse factories/user files,
  and undo the last preset load. Skin/window preferences stay unchanged.
- Five original factory starting points: Dark Lead, Grit Adlibs, Deep Double,
  Night Echo and Cloud Choir. Inspired by dark modern vocal production, not exact
  artist recreations or proprietary preset copies.
- Practical connected-path tail reporting; freeze maps safely to infinite tails
  in CLAP/VST3. 1727 host parameters, including 720 new creative controls.

Validation: all 18 focused CTest suites passed, including 42 editor/state groups,
creative processor/audio tests and both skins. Standalone DSP tests also covered
allocation-free processing and sanitizer checks. Tracked CLAP/VST3 packages match
Release artifacts by SHA256; packaged CLAP smoke passed. The long legacy
`VocalStackDSP` suite was excluded. No actual Bitwig listening check or installation
was performed for this build.

Reference principles: Valhalla Delay/VintageVerb (coloured spaces and repeats),
FabFilter Pro-C 3 (dynamics/detector workflow), Decimort 2 (separate degradation
controls), and SILO (granular dimensions). Original DSP and UI, not clones.

### 1.5.2 — compact, vertically resizable modulation panel

Validation: Release CLAP/VST3 builds and all 16 focused CTest suites passed,
including 40 editor/state groups. Both skins reviewed. Long legacy DSP suite
excluded; Bitwig listening remains manual validation.

- Compact 52x76 control cells in two rows of three, reduced value text, shorter
  labels with full-name tooltips, and 96 px selectors.
- Cascadia Mono LFO labels/readouts when installed, with system monospace fallback.
- Graph fills the panel's usable height; controls do not stretch.
- Drag the top divider to resize the panel, with saved 160–480 px desired height
  constrained to retain canvas space. Small panels scroll; right inspector and
  audio topology remain unchanged. Graph undo/redo preserves panel size preferences.

### 1.5.1 — routing layout and compact Stack polish

Validation: Release CLAP/VST3 rebuilt; all 16 focused CTest suites passed,
including 39 editor/state groups. Both skins visually checked. Long legacy DSP
suite excluded; no actual Bitwig listening check.

This section supersedes the historical UI requirements below:

- Left, vertically scrolling LFO/Random waveform tiles, with + Modulator above.
- Selected modulator Controls/Assignments in a canvas-width bottom strip; fixed-size
  controls scroll horizontally rather than stretching. Node inspector remains
  independent and full-height on the right, defaulting to 320 px (20% narrower).
  Its left divider resizes between 320 and 480 px with saved width, without
  topology changes, graph refitting, or inspector object reconstruction.
- Context menus on nodes and empty canvas open at the cursor.
- Waveshaper transfer curves follow coherent effective modulation snapshots,
  without writing modulation into base parameters.
- Always-visible Stack controls; remove Show Advanced, Voice stack and redundant
  Open Routing buttons. Retain legacy Advanced host/state compatibility; generated
  card interaction activates custom processing when necessary, not editor opening.
- Compact 172x278 Stack cards with two columns/four rows of horizontal sliders,
  including zero-centered bipolar fills. Node inspectors retain larger knobs.
- Stack fits down proportionally, never enlarges controls beyond native size.
- Lead motion/activity snapshot, gold L marker, trail, readout and card selection.

### 1.5.0 — resizing, Lead, modulation, and waveshapers

**Status: implemented, validated, packaged and installed as 1.5.0.** This
section takes precedence over conflicting historical requirements below. Existing
sessions, original parameter IDs/order/ranges, and authoritative audio topology
remain compatibility constraints.

Release validation:

- Release CLAP/VST3 builds and **all 16 focused suites passed**, including **33
  editor/state groups** in both skins. This includes neutral/default-path baseline
  compatibility, modulation timing/publication/dwell/recall, extra mixer targets,
  EQ assignment gestures, Raw/quality/DC/RMS matching and legacy normalized shape
  automation. The long legacy `VocalStackDSP` suite was excluded, not passed.
- Both skins' Stack/Routing/FX/LFO/Assignments renders visually reviewed. Packaged
  and installed CLAP smoke tests passed. Both installed formats match packages by
  SHA256. Actual Bitwig listening/transport remains manual user validation.
- Worst-case stress passed finite-output checks, but an isolated 48 kHz run of
  8 pitched voices + 16 FX (15 serial 4x Multi Fold shapers) + 16 modulators/256
  assignments took about **1.4 seconds per audio second** on this machine.
  Capacity limits are not real-time CPU guarantees; extreme patches can overload.

Implementation checklist:

- [x] Proportional resizable Stack; unscaled expanding Routing; saved window size,
  graph view and selection; scrolling inspectors and pinned + Modulator.
- [x] Global Mix beside Bypass, preserved/dimmed while bypassed; existing control
  renamed Lead / Stack without changing its host identity/behavior.
- [x] Neutral editable Lead, separate from eight generated voices; shared controls
  in Stack and source-node inspectors, with bidirectional selection.
- [x] Replace parameter sliders in voice cards, Routing FX/voice inspectors, and
  Stack Chorale with shared rotary controls. Voice cards use **two rows of four**;
  signed arcs use actual zero (Gain uses 0 dB), not the midpoint of its range.
- [x] 16 independent Classic LFO/Random sources, 256 assignments, fixed 32-sample
  clock, tempo/retrigger information, bounded real-time-safe publication and snapshots.
- [x] Controls / Assignments inspector, signed depths, enable/remove/reveal,
  persistent Assign across views, discrete assignment menus, live combined markers
  and selected-source ranges, including EQ handle/wheel assignment gestures.
- [x] Additional Dry/Wet mixers are parameter-bound and modulatable, not exceptions
  to the sound-control rule; IDs 200..247 have fixed slots. Nonstandard imported
  mixer IDs retain legacy graph controls. Machine aliases are stored in UI layout,
  editable by double-clicking inspector title, with graph-history support.
- [x] 19 original waveshapers, curve-icon selector, Shape, per-instance Raw,
  Off/2x/4x quality, DC handling and optional bounded stereo-RMS audition matching.
- [x] Append-only host parameters: **1007 total**, original 874 unchanged. In
  particular, old WaveShape remains 0..2: appended WaveAlgorithm uses 0=Follow
  Legacy, 1..19=explicit curves, preserving old normalized shape automation.
- [x] Negative-formant sound/ranges remain unchanged. Preserve Clarity is deferred.

Release clarifications superseding broader wording below:

- Oversampling uses preallocated half-band **IIR** paths with zero explicit integer
  delay, not a linear-phase/latency-producing mode. ADAA/IIR/DC phase is frequency
  dependent; local/parallel dry stays direct and can comb. General nonzero integer
  latency remains rejected. Do not claim phase-neutral branch compensation.
- Classic LFO/Random are original **monophonic** implementations following verified
  Bitwig workflow descriptions, not exact DSP/full device parity. Sync follows
  tempo/division with restart-on-play, not PPQ seek/loop phase locking. Random is
  uniform stepped/cubic-slew motion. UI changes preserve running histories; a
  prepare/reset initializes them, rather than recalling sample-exact phase/RNG state.
- RMS audition matching is gated, bounded +/-24 dB, and smoothed over 300 ms;
  manual Output trim remains active. It is not peak limiting or a LUFS guarantee.
- Reference: https://www.bitwig.com/userguide/latest/modulator/ . No proprietary
  source, artwork, branding, or presets were copied.

#### Delivery order

1. Resizable UI, global Mix, editable Lead, and shared voice inspector.
2. Shared modulation backend, Classic LFO/Random, assignments, and live feedback.
3. Expanded waveshapers, per-instance Raw mode, quality modes, and required
   latency support. Raw mode itself does not require oversampling or added latency.

Each stage must produce a working, tested plugin. No new Filter, delay, reverb,
or unrelated EQ/multiband redesign is included.

#### Resizable window and selection

- Stack scales its complete layout proportionally, with no stretched text or
  controls; account for the Advanced drawer's different layout aspect ratio.
- Routing keeps controls, nodes, text, and pins at normal size. Additional window
  area expands the canvas; the right inspector retains a sensible width and
  gains usable height. Scroll overflowing inspector content.
- Keep **+ Modulator** pinned at the inspector bottom.
- Use sensible minimum sizes. Remember window dimensions, graph pan/zoom, and
  selection. Switching tabs must not unexpectedly resize the window.
- Resizing never rearranges nodes or repeatedly auto-fits the graph. Retain Fit.
- Selection follows across Stack and Routing: reveal/highlight the corresponding
  voice card when changing views.
- Keep double-click reset and modifier-drag fine adjustment consistent wherever
  compatible with existing deliberate gestures. Preserve DAW keyboard shortcuts.
- Inspector headers clearly identify the selected machine, with rename/bypass
  where applicable. Retain the current color scheme and both skins.

#### Global Mix and Lead / Stack

- Rename the existing doubler Mix to **Lead / Stack**, including its main routing
  mix-node presentation, without changing its behavior or parameter identity.
  Use a clear balance readout rather than an unexplained percentage.
- Add a global **Mix** beside whole-plugin Bypass at the top right: latency-aligned
  untouched input versus the complete processed graph output.
- Default global Mix to 100% processed, including migration of older sessions.
- Bypass passes original input, dims/disables global Mix, and preserves its value.
- Keep processor histories/tails coherent at the Dry endpoint. Smooth transitions.
- Do not add a redundant local dry/wet knob to the Lead card.

#### Editable Lead and shared voice controls

- Show Lead first in Stack, visually distinguished from generated voices. Lead
  does not consume one of the eight generated-voice slots.
- Provide local pitch, fine detune, formant, pan, gain, and enable controls;
  applicable delay/humanization controls start neutral. Preserve existing shared
  processing; avoid applying its formant or other shared controls twice.
- Lead edits affect the Lead side of Lead / Stack; only global Mix accesses
  untouched original input. Existing Lead / Stack behavior is not redefined.
- Extract shared voice controls for Stack cards and the Routing inspector when a
  Lead/Voice source is selected. Both edit the same stable parameters, not copies.
- Preserve musical Pitch +/-36 st, Fine +/-100 ct, local/main Formant +/-24 st
  and existing additive formant behavior. Neutral-origin displays remain correct.

#### Modulation architecture and target coverage

- Build a shared modulation layer, not per-effect LFO hacks or audio-cable nodes.
  Current parameter IDs/settings are foundations, not an existing modulation API.
- Independent modulators and stable assignments combine base settings/host
  automation with contributions into bounded effective DSP values.
- Never write modulation output into host parameter/base values.
- Address targets by stable parameter ID and modulators by stable identity;
  preserve assignments across panel changes, reopening, and session recall.
- Evaluate on a fixed audio/control clock, independent of host block size, with
  parameter-appropriate interpolation/smoothing and safe discrete transitions.
- All continuous sound parameters are targets: voice/Lead controls, main controls,
  FX mix/drive/output, EQ frequency/gain/Q, crossover frequencies, etc.
- Discrete sound controls use explicit stepped modulation and safe switching;
  prevent accidental chattering. Structural/UI actions and whole-plugin Bypass
  are not targets. Removing/replacing targets must never redirect assignments to
  an unrelated machine that reuses an FX slot.
- Initial scope is modulation of sound parameters, not modulator-to-modulator
  feedback. Such an extension requires explicit ordering/cycle rules.
- Prepare edits off audio and publish bounded plans safely. No allocation, locks,
  graph rebuilds, or destruction in the audio callback.
- Publish lightweight effective-value/contribution snapshots for the UI; no GUI
  reads of live DSP state. Serialize settings/assignments with safe old-state defaults.
- Wire host tempo/transport and MIDI into processing where needed for sync and
  triggering; do not pretend these are already consumed by the current engine.

#### Classic LFO and Random

- Use Bitwig Classic LFO and Random as the workflow/behavior references. Verify
  official documentation and map controls before implementation. Use original
  code/assets; record deliberate differences, not unverified exact-parity claims.
- Classic LFO: periodic waveform/shaping controls, free Hz and host-synced timing,
  phase, relevant retrigger modes, bipolar/unipolar behavior, live output display.
- Random: stepped values and smoothly evolving motion, free/synced timing, and
  relevant smoothing/distribution/retrigger controls based on the verified reference.
- Stable running state: opening panels, resizing, selecting targets, and changing
  host block sizes must not restart phases or random sequences unexpectedly.

#### Assignment interaction and inspector

- **+ Modulator** offers Classic LFO / Random. A compact persistent modulator strip
  permits reselecting an LFO after selecting an audio node; no audio cables needed.
- Selecting a modulator shows right-inspector **Controls / Assignments** tabs.
- Assignment rows show machine + parameter, editable signed amount in useful units,
  enable/disable, remove, and a way to reveal/highlight the target control.
- Click **Assign**, then drag an eligible highlighted control to set signed depth
  instead of changing its base setting. Multiple targets have independent amounts.
- Assignment mode survives voice/FX selection and Stack/Routing tab switches.
  Show a persistent **Assigning: LFO 1** banner. Escape or Assign exits.
- Keep base values visible. Draw the selected modulator's range as a colored arc
  or strip and the actual combined effective value as a moving indicator.
- Distinguish the selected LFO's contribution from the result of multiple sources.
  Apply feedback to knobs, sliders, and relevant EQ handles in both views.
- Show truthful activity during assignment and playback, not invented animation.

#### Expanded waveshapers and presentation

- Preserve existing Soft / Hard / Asymmetric algorithm identities and sound by
  default; append algorithms without reordering existing saved choices.
- Proposed collection (19 total), subject to meaningful distinct behavior/tests:
  - Existing: Soft, Hard, Asymmetric.
  - Saturation: Warm Asymmetric, Sine Saturation, Arctangent.
  - Clipping: Rounded Clip, Asymmetric Clip.
  - Folding: Triangle Fold, Sine Fold, Single Fold, Multi Fold.
  - Rectification: Half-Wave, Full-Wave.
  - Destruction: Staircase, Dead Zone, Binary Threshold.
  - Curve shaping: Exponential, S-Curve.
- These are original algorithms, inspired by Thermal's breadth, not proprietary
  curve reproductions or claimed sonic clones.
- Keep Drive / Output / local Mix. Add modulatable Shape where meaningful and
  clearly disable it for algorithms without that dimension.
- Group the selector by family with small curve icons; update the static transfer
  visualization and retain actual pre/post stereo waveform monitoring.
- Add optional level-matched audition, explicitly not limiting or a guarantee of
  perceptual loudness matching. Handle DC in asymmetric/rectifying algorithms.

#### Raw aliasing switch and quality

- Add a saved **Raw** switch per waveshaper instance, default Off to preserve
  existing sessions. Raw On directly evaluates the nonlinear curve at project
  sample rate, bypassing ADAA/other alias-reduction processing for that mode.
- Off retains current ADAA for existing algorithms; new algorithms need suitable
  documented anti-aliasing strategies. Do not claim an alias-free Off mode.
- Smooth Raw switching without clicks, keep required histories warm, and expose
  it consistently in inspector/state/visuals. Define discrete modulation safely.
- Raw is intentionally sample-rate/input/drive dependent, not a fixed grit amount
  or bitcrusher. Explain that bypassing ADAA also changes its attenuation/phase.
- Add selectable oversampling quality separately. Make Raw and quality semantics
  explicit: Raw runs the nonlinearity at project rate, not secretly oversampled.
- Align local dry/wet and parallel graph branches, report host latency, and extend
  current routing latency restrictions before enabling latency-producing modes.
- Quality changes prepare off audio and transition safely. Measure CPU/alias
  reduction; oversampling is not universally preferable and not alias-free.

#### Negative formant: explicitly deferred

- User clarified dullness occurs at strongly negative settings, not small shifts.
- **Do not change negative-formant DSP, ranges, stacking, brightness, or defaults
  in this implementation.** Darkening alone is not established as a bug.
- Optional Preserve Clarity (retaining consonant/sibilant detail while shifting
  vowel/body character) is an idea for later listening evaluation only, not an
  approved feature. No automatic treble boost or hidden unshifted-voice blend.

#### Validation and release

- Test migration, unchanged old IDs/indices, neutral defaults, and repeatable recall.
- Check resizes/minimum sizes/both skins, tab/selection persistence, shared controls,
  global Mix endpoints, Lead / Stack, bypass, and dry/branch latency alignment.
- Test modulation timing across irregular blocks/sample rates, sync/transport/MIDI,
  positive/negative/multiple assignments, safe removal/replacement, and UI snapshots.
- Test waveshaper silence/reset/DC/finite output, drive/shape/Raw automation,
  safe quality transitions, alias behavior, and parallel reconstruction/alignment.
- Measure eight-voice/populated-graph/modulation CPU. Run focused regression suites;
  distinguish automated results from actual Bitwig listening/transport checks.
- Refresh versions/docs/packages and install only if unlocked after successful
  builds. No binary rebuild is needed merely to record this pending plan.

### Historical release scope: 1.4.1 — UI, routing, and FX monitors

These 1.4.1 amendments take precedence over older checkpoints below, but not the
1.5.0 amendments above. That patch retained the
current UI+monitor scope, not a new effect module or a full Pro-Q/EQ+ clone.
Release CLAP/VST3 builds and all 12 focused suites passed, including 26 editor
regression groups. Refreshed packages and installed binaries match by SHA256;
packaged/installed CLAP smoke checks passed. Both skins were visually reviewed.
The long legacy DSP suite was excluded; Bitwig listening/transport checks remain
manual user validation.

- Use embedded **Stack / Routing** top tabs. Changing tabs or panel visibility
  never changes processing activation or audio-graph connections.
- Dragging an output pin moves its connection to a new input, replacing its
  existing destination instead of adding fan-out. Dropping on empty canvas
  disconnects; clicking without dragging leaves the connection unchanged.
  **Ctrl/Cmd+output drag** moves the source. Selected cables support dragging
  either endpoint, with empty-canvas disconnect and undo/redo.
- Shared-input editing uses the selected cable to identify the connection.
  The selected-cable inspector includes **Disconnect**; this supersedes the
  earlier no-button decision. Disconnect and endpoint moves are undoable.
- The canvas mouse wheel zooms around the cursor **without Ctrl**. Retain pan,
  +/- zoom, and Fit without taking ordinary DAW transport shortcuts.
- Present muted voices remain visible; absent voices are hidden. Basic shows
  the first two voices only. Apply this distinction to stack, routing, and
  Voice Motion rather than treating mute as removal.
- A bypassed canonical `mainChorale` in the default Main Mix → Chorale → Output
  chain folds into a transparent chain display. Custom main-Chorale wiring
  remains visible even when bypassed. Visibility and bypass presentation never
  mutate audio-graph topology; bypass changes processing, not connections.
- EQ shows live input/output **2048-point Hann FFTs**, computed on the UI thread
  from snapshots with stereo-power combination, not channel summing. Keep the
  **±24 dB response-gain scale separate from FFT dBFS**. A **single click** on
  empty graph space adds a bell, not a double-click. Monitoring includes bypass.
- Waveshaper shows a **static transfer curve**, explicitly not the dynamic ADAA
  response, plus live stereo pre/post waveforms including bypass.
- Multiband shows the **requested LR4 response**, input FFT, and **actual output
  RMS meters**, including bypass (#0 full-band, other outputs silent). Do not
  portray the requested response as measured band-output spectra.
- Add standalone `vocalstack_fxmonitor_tests` from `tests/FxMonitorTests.cpp`,
  linked to `vocalstack_dsp`, registered as `VocalStackFxMonitor`. With plugin
  suites and optional pinned-history baseline enabled, the configured suite
  count is **13 including long `VocalStackDSP`, or 12 excluding it**; without the
  baseline the counts are 12/11. Counts are not passing-test claims.
- Official references are inspiration, not copied code/assets or full feature
  parity: [FabFilter analyzer help](https://www.fabfilter.com/help/pro-q/using/analyzer)
  documents pre/post spectra; [Blender node editing](https://docs.blender.org/manual/en/latest/interface/controls/nodes/editing.html)
  explicitly allows output fan-out and documents Ctrl-dragging outgoing links.
  Our single-output connection policy is an intentional difference, not a claim
  that Blender forbids fan-out. No exact/full Pro-Q or EQ+ analyzer is claimed.

### Historical implementation checkpoint: 1.4.0

The user authorized the core through nodes and Voice Motion, excluding follow-up effects. This checkpoint implements eight stable voice slots, musical Pitch/Fine Detune, stacked formants, neutral-origin controls, routing/execution, independent Chorale slots, draft/active graph persistence, native node editing and snapshots.

**1.3.1 clarification:** ordinary disconnections are valid live edits. Unconnected inputs are silence; disconnected Output is silent. Unused disconnected FX cannot block other valid routing. Last-valid fallback applies only to structural errors such as cycles, duplicate/illegal links and unsupported latency—not missing cables. This supersedes the earlier incomplete-draft behavior below.

Implementation choices and limits:

- Bounded JUCE-independent executor rather than AudioProcessorGraph; explicit pins/mixers and off-audio reclamation are tested directly. A separate AudioProcessorGraph prototype was not added.
- All current routing FX add zero dry-path latency. Nonzero additional branch latency is rejected; general latency compensation awaits a module that needs it.
- Topology switching uses a 10 ms output-offset de-click ramp, not simultaneous old/new graph rendering or a spectral crossfade. Retained FX histories are advanced only once.
- Original 46 host IDs/indices remain, with 139 total parameters including fixed FX slots. Wider Formant ranges change normalized host automation mapping; raw saved semitone values are retained.
- Voice Motion plots fine detune vertically and shows musical Pitch separately in the readout.
- Nine core suites plus an optional pinned-history baseline comparison cover automated validation. Listening and transport behavior in the user's Bitwig session remain manual validation.

### Historical user amendments implemented in 1.4.0

These superseded the original staged design; the 1.4.1 amendments above now take
precedence wherever they conflict (especially tabs, cable edits, and monitors):

- Visible cables are the accepted running topology. No separately visible non-running draft; invalid cycles/duplicate/illegal links are rejected atomically and removed from view.
- Gain compensation is automatic at multi-connection inputs. No visible Sum node or manual compensation box; existing Sum boxes migrate to direct links.
- Cable input ends can be dragged to rewire or dropped on blank canvas to disconnect. Selected cable source ends and Ctrl-output dragging are supported. No Disconnect button.
- Added independent Waveshaper, EQ8 and 2-/3-band crossover modules sharing the 15 additional stable FX slots. Original 139 host parameters stay ordered; appended tone automation brings the total to 874.
- EQ targets EQ+-style graph editing, with Pro-Q selected-band readability as an additional UI reference. Original code/assets and Device/Neon colors remain. There is no spectrum/reference analyzer yet and no claim of matching proprietary coefficients.
- Multiband outputs route through ordinary independent FX and recombine with band-aware unity gains, not voice-count attenuation. Two-band ports #0 Low/#1 High; three-band #0 Low/#1 Mid/#2 High. Stored port #2 cables are dashed/silent in two-band mode, so assignments must be reviewed when switching modes.
- Advanced header groups Voice stack, Add voice, Open Routing and Reset to Basic; routing inspectors have a wider native panel and expanded EQ view.
- XP-style character Filter, delay and reverb remain future work.

The remaining sections retain the original staged design and reference briefs,
including superseded gestures and analyzer parity goals. They are not the current
implementation contract. Use Current status for remaining work and the latest
applicable release record for implemented behavior.

Keep Basic mode quick to use. Advanced adds **up to eight generated voices, a live voice monitor, and an optional FX-routing editor**, backed by separate processing modules.

Implement in stages, with a working, tested plugin after each stage. The first routing release includes Chorale, not a collection of new effects. Follow-up effects include a waveshaper, an XP-style Filter, and an EQ targeting Bitwig EQ+'s UI and editing workflow, as specified below.

**Architecture goal:** adding an effect must not require teaching it what a voice is.

## Historical design archive — not the current implementation contract

Sections 1–9 below preserve the original staged requirements and reference briefs.
They intentionally include superseded proposals: explicit Sum/recombine nodes,
voice-card knobs, right-inspector modulator controls, and delay/reverb as future
work. Current behavior is automatic compensated fan-in/band recombination, compact
Stack sliders, independent right node and bottom modulator panels, and the five
creative effects delivered in 1.6.0. Use the current roadmap above for outstanding
work; do not reimplement archived requirements simply because they use imperative
wording.

## 1. Separate the existing engine's responsibilities

Refactor without intentionally changing the current sound:

- **Voice bank:** generates the lead and individual doubled signals.
- **FX modules:** independent processors with prepare, reset, process, settings, and latency/tail information.
- **Routing model:** stores nodes, connections, and stable identities.
- **Routing executor:** runs those connections in the correct order.
- **Mixers:** handle compensated summing and the main dry/wet blend.
- Keep host parameters/state, UI, and metering outside these components.

**Checkpoint:** the default routing matches the current plugin's output. Chorale becomes an ordinary FX instance, not a special hardcoded processing step.

## 2. Expand Advanced to eight stable voice slots

- Fresh Advanced configuration shows **two voices and + Add voice**.
- Add complementary voices up to eight generated voices; the lead does not count toward this limit.
- Separate **Remove** from **On/off**:
  - Off preserves the card and settings for auditioning.
  - Remove disconnects and hides that slot.
- Do not renumber surviving voices or transfer their automation.
- Preserve the stack across panel closure and Advanced toggling.
- **Reset to Basic** returns to two Basic-derived voices.
- Restore existing four-voice sessions without losing their settings.
- Avoid unnecessary processing for unused slots, with clean initialization/fades when added.
- Use a bounded, scrollable card area rather than making the window eight cards tall.

### Actual musical Pitch and separate Fine Detune

- Add a prominent per-voice **Pitch** control in **semitones (st)**.
- Range: **-36 to +36 st**, with **0 st** as the default and centered neutral point.
- This must perform actual musical pitch shifting, including octave and harmony intervals, not merely relabel the existing subtle detuning control.
- Support fractional semitones for fine adjustment.
- Retain **Fine Detune** separately in secondary voice controls, in cents. Its existing range is -100 to +100 ct.
- Preserve complementary small detune defaults for the doubler's character; do not make the main Pitch control nonzero to represent them.
- **Human** retains its separate role: time-varying pitch/timing/formant movement around the chosen settings. Human at zero stops random movement, not musical Pitch or fixed Fine Detune.
- These are fixed pitch intervals, not automatic scale-aware harmonization or pitch correction.
- Verify DSP quality, latency, and behavior across the full requested pitch range; do not assume extending a parameter range alone implements it.

### Formant ranges and stacking

- Expand the **main large Formant knob** to **-24 to +24 st**.
- Expand each **per-voice Formant** control to **-24 to +24 st**.
- Both are bipolar, with **0 st** as the neutral point.
- Shared and per-voice shifts **add together**. Do not cap their combined shift at +/-24 st.
- Example: shared +24 st plus voice +24 st requests an effective **+48 st**; likewise -24 plus -24 requests **-48 st**.
- Validate the full combined range in the DSP. Do not silently clamp away the requested stacking behavior.
- Preserve the existing distinction: disabling shared Formant does not disable independent Advanced voice formants.
- Retain fine adjustment near zero despite the wider ranges.
- A neutral point is not necessarily a preset default: complementary existing per-voice formant defaults may remain nonzero.

### Slider and knob behavior

Signed controls must show displacement from their neutral point, not look half-applied at neutral. Apply consistently in both skins.

| Control | Fill behavior | Neutral/reference |
|---|---|---|
| Voice Pitch | Bipolar, outward from center | 0 st |
| Fine Detune | Bipolar, outward from center | 0 ct |
| Pan | Bipolar, outward from center | Center / 0 |
| Per-voice Formant | Bipolar, outward from center | 0 st |
| Main Formant | Bipolar, outward from center | 0 st |
| Voice Gain | From the unity marker toward cut or boost | 0 dB |
| Output Gain | From the unity marker toward cut or boost | 0 dB |

- Pan must clearly distinguish left, center, and right; L/C/R formatting is a useful presentation option.
- Gain ranges are asymmetric. The fill origin must be the actual **0 dB position**, not an incorrectly assumed geometric midpoint.
- Keep **Leveler, main Mix, Human, Width, Delay, Depth, Rate, Chorale Mix, and Chorale Motion** unipolar, filling from minimum.
- Width at zero collapses the spread; it is not a signed pan control.
- Depth at 0x stops motion. A 1x reference tick is useful, but does not make Depth bipolar.
- Neutral and preset default are different concepts. Do not reset intentional voice pan, delay, detune, or formant defaults merely to center every slider.
- Existing main Formant and Output knobs already draw from zero; Advanced sliders currently need the corresponding neutral-origin rendering.

## 3. Implement and test routing before drawing cables

Initially support:

| Node | Job |
|---|---|
| Lead / Voice sources | Expose generated audio |
| Chorale | Process any connected audio |
| Compensated Sum | Combine branches without an automatic stacking-volume boost |
| Dry/Wet Mix | Blend two inputs using the main Mix knob |
| Output | Final trim and output metering |

Connections determine whether an effect processes one voice, a group, or the whole result. **An effect has no voice or master mode.** Each FX is its own machine that processes incoming audio; routing decides what feeds it.

Rules:

- One effect node is one independent processor instance with its own state.
- Two voices feeding one effect through a mixer are processed as a sum; two effect nodes process independently.
- Require an explicit mixer to combine multiple signals, making compensation visible and predictable.
- Preserve relative voice gains; do not boost a quieter voice back up.
- Reject loops, duplicate connections, and invalid wiring.
- Start with a limit of **16 FX instances**, separate from source/mixer nodes.
- No arbitrary modulation cables or topology automation initially.
- The main Mix is a dedicated dry/wet node: the lead is its dry branch and generated voices form its wet branch. Effects can be placed before or after it through routing.

**Default graph:** lead -> dry input; voices -> compensated wet sum -> wet input; main Mix -> Chorale -> Output.

### Compensation contract

- Extend the Basic doubler's stacking-compensation intent to explicit routing mixers: combining voices should add thickness, not an automatic volume boost.
- Preserve intentional relative gain changes, such as one voice being -6 dB.
- Do not add an automatic output loudness rider, Auto Level feature, or limiter.
- Compensation belongs in mixers, not in each FX module.
- Define and test compensation for grouped merges, fan-out, and reconvergence; do not assume a simple voice count covers duplicated branches and independent voices equally.
- Do not promise identical sample peaks, RMS, or perceived loudness for arbitrary correlated signals and effect outputs.

**Checkpoint:** offline tests prove routing, ordering, compensation, and independent effect state before the graph UI exists.

## 4. Make live routing changes safe

- Validate and prepare routing changes **off the audio thread**.
- Publish a prepared execution plan at a safe processing boundary.
- Smooth or crossfade changes without advancing shared effect state twice.
- Preserve unaffected effect instances and their history.
- Handle branch latency explicitly; reject unsupported latency cases rather than silently misaligning audio.
- No allocation, locks, graph rebuilding, or object destruction in the audio callback.
- A bad edit must leave the last valid graph running, not silence or crash the plugin.

## 5. Add the native JUCE node editor

Use a small native JUCE canvas, keeping the processing backend independent of the UI choice.

- Drag nodes and connect audio pins.
- Pan/zoom, select, disconnect, delete, and bypass.
- Add effects from a menu.
- Select an FX node to edit its controls in an inspector.
- Show invalid connections clearly.
- Save node positions separately from audio connections.
- Add undo/redo for graph edits.
- Preserve DAW shortcuts, especially Space. Only deliberate numeric text entry should temporarily own keyboard input.

Before committing to the backend, prototype `AudioProcessorGraph` against the real-time requirements. Use it if it fits; otherwise use a small bounded executor, not two competing graph systems.

## 6. Add the voice-motion monitor

Above the voice cards:

- Horizontal position: effective pan, including Width.
- Vertical position: current detune/motion. With the new wide musical Pitch range, explicitly distinguish base musical Pitch from fine detune/motion in the display; finalize a readable vertical scale before implementation.
- Number/color: stable voice identity.
- Brightness: measured voice activity.
- Subtle trails: actual modulation history.
- Selected-voice readout: musical Pitch, current detune, extra delay, effective formant, and gain.
- Exclude shared algorithm latency from the displayed per-voice extra delay.
- Clicking a dot reveals/highlights its card; selecting a card highlights its dot.
- Initially no draggable dots.
- Label it **Voice Motion**, not a stereo/correlation analyzer.

Publish lightweight audio-thread snapshots. **No invented animation and no GUI access to live DSP objects.**

## 7. Compatibility, testing, and delivery

Cover:

- Existing session migration and unchanged existing parameter IDs/order.
- Append new voice/Pitch/FX automation parameters; stable slot identities must survive removal and restoration.
- Preserve existing Fine Detune values and sessions rather than reinterpreting cents as semitones.
- Expand Formant ranges with explicit attention to saved-state recall and host automation mapping. Document any unavoidable compatibility impact.
- New voice/FX automation slots with stable identities.
- Mix endpoints, relative gains, compensated merges, and bypass.
- Full Pitch range, fractional pitch, Fine Detune, and Human combinations.
- Shared and per-voice Formant at both individual extremes and stacked +/-48 st, including independent voice formants with the shared switch off.
- Add/remove/reconnect during playback.
- Mono/stereo, supported sample rates, irregular blocks, silence, reset, and tails.
- Both skins, scrolling, graph interaction, keyboard focus, and neutral-origin fills.
- Worst-case eight-voice/FX CPU usage, including extreme pitch/formant settings.

At useful implementation milestones:

1. Run relevant automated tests and build CLAP/VST3.
2. Update release version/documentation when appropriate and refresh tracked packages.
3. Install only if both installed plugin binaries are unlocked; never terminate Bitwig or force an overwrite.
4. Verify packaged/installed artifacts and run smoke tests.
5. Commit and push completed work.
6. Clearly distinguish automated validation from actual listening/transport checks in Bitwig.

Documentation-only changes do not require rebuilding or reinstalling unchanged plugin binaries.

## 8. Follow-up effects after nodes

The user requested two additional independent FX modules: a **Filter modeled on Bitwig's XP** and an **EQ targeting Bitwig EQ+'s UI and editing workflow**, not merely a generic filter and basic tone control. For EQ, "behavior" means interaction/UI, not identical DSP or sound. These follow the working routing release; they do not block its initial delivery.

Each module processes incoming audio without knowing whether it belongs to one voice, a group, or the whole stack. Each node owns independent settings/state and uses the same module interface, automation-slot scheme, inspector, and real-time requirements as Chorale. No special voice/master implementations.

### Filter: Bitwig XP as the reference

- Target the XP filter's controls, configurations, response, resonance, drive behavior, and interaction as closely as an independently developed implementation can support.
- Bitwig's official Grid documentation describes XP as inspired by Oberheim, related to Ladder, and offering **15 filter configurations**. XP is also available in Filter+ and Sweep. The target is XP itself, not the entire Filter+ device, its shaper, or its modulation environment.
- Before DSP implementation, inventory all 15 configurations, parameter ranges, defaults, and interactions in the user's Bitwig version. Documentation alone does not specify the complete algorithm or mode list.
- Characterize frequency/phase response, cutoff mapping, resonance, input-level dependence, saturation, and mode switching against reference renders. Test parameter movement as well as static settings.
- Preserve resonant/drive character rather than adding a hidden loudness rider that cancels it. Provide intentional output trim where needed; graph stacking compensation remains the mixer's responsibility.
- Verify numerical stability, silence/reset, extreme cutoff/resonance/drive, sample-rate behavior, and aliasing/oversampling needs.

### EQ: Bitwig EQ+ UI and editing workflow as the target

- The user specifically means **EQ+'s UI and interaction behavior**, not an exact sonic clone. Use independent, well-tested EQ DSP; matching Bitwig's proprietary coefficients or nulling against EQ+ is not a requirement.
- Preserve the EQ+ graph-editing workflow rather than substituting a simplified two- or three-band UI.
- Verified documented feature baseline: up to **eight bands**, **14 modes per band**, global frequency **Shift** and **Gain**, **Adaptive-Q**, and a spectrum display with a **Reference** track option.
- Match the documented editing gestures:
  - Double-click to add a peak at the cursor.
  - Drag the left/right ends of the curve to add low/high shelves.
  - Drag the left/right graph edges away from the curve to add low/high cuts.
  - Drag the lower graph edge to add a notch.
  - Give clear cursor/interaction feedback for each operation.
- Inventory the complete mode list, slopes, ranges, defaults, Q/bandwidth conventions, global control semantics, band enable/delete behavior, and remaining interaction gestures in the reference version before implementing. Do not invent undocumented details.
- Validate our EQ's magnitude/phase response across modes and sample rates, Q/gain interaction including Adaptive-Q, global Shift/Gain behavior, high-frequency response, automation smoothing, and bypass behavior. These are correctness tests of our DSP, not requirements to match EQ+'s sound.
- Draw the response of the actual implemented filters, not a decorative curve. Publish analyzer snapshots without GUI access to DSP state or audio-thread allocation.
- Band gain and global EQ Gain use neutral-origin bipolar rendering around 0 dB. Frequency is logarithmic; Q and other positive-only controls remain unipolar. Confirm the reference meaning/units of Shift before choosing its mapping.
- EQ+'s Reference spectrum feature needs an explicit reference-audio source. Resolve whether the plugin will use a host sidechain/reference input before promising full parity; graph audio cables alone do not provide an external Bitwig track automatically.
- Keep the native plugin skins and keyboard-focus policy. Reproduce useful interactions with original UI code/artwork rather than copying Bitwig assets.

### Reference fidelity and implementation boundary

- **EQ target: UI/workflow parity, not sonic equivalence. Filter target: XP-style controls and sonic character.** Keep these requirements distinct. Use independent DSP/UI implementations; do not copy proprietary code, assets, or presets.
- Record the Bitwig reference version and interaction checklist for UI comparisons. Record reference renders, settings, and comparison tolerances where testing XP's sonic character. Report differences explicitly; do not label an unmeasured approximation an exact clone.
- Public documentation does not reveal proprietary coefficients, nonlinear models, or all interaction details. Resolve missing UI details through observation and user confirmation; use reference measurements where needed for the XP filter. EQ does not require reconstructing proprietary coefficients.
- Reference recording/comparison for XP requires access to Bitwig and suitable test renders; automated tests of our implementation alone cannot establish sonic equivalence.

References:

- EQ+: https://www.bitwig.com/userguide/latest/eq/
- XP: https://www.bitwig.com/userguide/latest/grid_modules#tdq_5cf_t4b

## Scope boundary

The first routing release includes **Chorale**. Follow-up modules are **waveshaper, XP-style Filter, and EQ+-style EQ**; their exact delivery order can be chosen after the routing milestone. A switchable **2-/3-band routing split and recombine** is also requested, as specified below. Adding an effect should require its DSP module, parameter definition, and controls, not changes throughout the voice engine.

Delay, reverb, and a full Thermal-like multiband distortion/macro system remain later possibilities, not committed scope in this plan. The 2-/3-band audio routing feature below is committed planning scope, not merely a future possibility.

## 9. Two-/three-band processing

- Add a multiband routing facility with selectable **2-band** and **3-band** modes.
- Two-band mode exposes **Low / High**, with one crossover frequency.
- Three-band mode exposes **Low / Mid / High**, with two ordered crossover frequencies.
- These are actual audio branches, not merely EQ bands: each band can feed its own independent FX chain, then return to a matching recombine node.
- Example: clean lows, saturated mids, and Chorale on highs. FX modules remain unaware of which band or voice feeds them.
- Permit the split on an individual voice, a grouped stack, or the whole signal through ordinary routing; no special per-location DSP implementations.
- This is a focused band splitter/recombiner, not a full Thermal-style distortion environment or arbitrary modulation system.

### Crossover and recombination contract

- Choose and document a complementary crossover design, including slopes, phase response, and latency, before implementation. Do not assume arbitrary low/high-pass filters will recombine correctly.
- Test two- and three-band recombination with all band chains neutral: document the expected magnitude, phase, and latency response. Flat magnitude does not automatically imply sample-identical dry audio.
- **Band recombination is not voice stacking.** Do not apply count-based voice attenuation when recombining complementary frequency bands; that would incorrectly reduce the reconstructed signal.
- Use a distinct, explicit band-recombine operation with the appropriate unity reconstruction gains. Preserve intentional per-band level changes.
- Account for different FX latency on each band using the executor's latency handling; reject unsupported arrangements rather than silently misaligning branches.
- Smooth crossover movement, handle frequency ordering and sample-rate limits, and switch 2-/3-band modes without unstable coefficients or abrupt clicks.
- Define how inactive Mid-band wiring/settings are preserved when changing modes. Do not silently delete FX chains or repurpose stable automation identities.
- Cover neutral reconstruction, isolated bands, crossover extremes/movement, mode changes, stereo/mono, irregular blocks, bypass/reset, silence, and independent per-band processing.
- Validate the added CPU cost with eight voices and the existing bounded FX instance budget. The splitter/recombiner adds no permission for graph cycles or audio-thread rebuilding.

**Scope note:** EQ covers ordinary tonal filtering. The separate XP-style Filter remains a requested character-filter follow-up, not a prerequisite for multiband splitting; prioritize EQ and useful routing before duplicating basic filtering functionality.
