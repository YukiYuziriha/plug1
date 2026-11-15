# Utility visual update — integrated delivery

**Delivered in the approved installed 1.11.0 release.** See
[final package/install validation](Release-1.11.0-Validation.md). Owner-only
handoff instructions below are preserved as historical integration context.

**Coordinator integration completed:** `FxMonitorFrame::utilityHistory` now
publishes the audio-owned accumulator under the existing exclusive buffer
ownership. Gate receives actual smoothed threshold/wet GR and final post-Mix
output, with zero applied GR on bypass/Mix zero. Fifteen persisted Mono Audition
booleans append at indices 2342–2356; all 0–2341 IDs/indices and utility strides
remain unchanged. Routing inspector, expanded card and Stack Stereo paths bind
them, and the smoothed fold runs once after Mix, before telemetry.
Gain's shared crossfade formatter is integrated; default Wet is hidden, while
recalled/base-automated **or effectively modulated** non-unity Wet is revealed.
The visibility check reuses the visual's coherent target snapshot, not another
full modulation read, and waits for active gestures to finish.
New helper, actual processor/LFO, Mono/state and GUI bindings tests pass; current
full editor is 80/81 because its native click precondition remains desktop-blocked.
See [consolidated validation and actual three-theme captures](Preview-1.11.0.md).
The source owner's original handoff/status below remains historical.

Read `AGENTS.md`, working-tree status, `Preview-1.11.0.md` readability notes and
`Node-Visual-Audit-1.11.0.md`. Existing shared changes are preserved. This work
owns only `UtilityEffectPanel.h`, `UtilityFilters.h`, new `UtilityTelemetry.h`,
new `UtilityVisualUpdateTests.h`, and this document. No version bump, builds,
tests, GUI capture, install, package refresh, commit or push was performed.
Coordinator must merge/integrate before validation. No rendered result is claimed.

## Implemented

- Gain: existing range verified as exact mute (`-120` sentinel) through **+24 dB**.
  Description now states that range. Global Output is untouched. Slot Mix remains
  attached and unchanged; it is hidden only at default 100% Wet and revealed as
  **Wet (legacy)** when the recalled/automated base or effective value is lower.
  Tooltip explains the compatibility blend. Visibility changes wait until an
  active user gesture ends; no value is reset or normalized. Width/state recall
  and existing IDs/indices remain unchanged.
- Stereo: final-output **L/R RMS activity and normalized L/R cross-power
  correlation** now lead the visual. Combined PRE/POST RMS remains secondary.
  Silence or a silent channel gives undefined correlation, not a false +1 mono
  reading. The bounded O(2048) analysis runs only on copied monitor arrays on the
  GUI thread; no FFT, live DSP access, or new DSP correlation work.
- Gate: keeps existing PRE/POST RMS and actual GR meter. Adds rendering for 200
  measured 20 ms bins, oldest left/newest right, fixed **4 seconds**,
  **-72..0 dBFS** input/final-output RMS and threshold, and a separate **0..36 dB**
  downward GR guide. Guide clipping never limits audio or the existing 0..96 dB
  meter. Threshold is recorded per bin, not painted retroactively from current
  settings. Thin connected paths reuse the same compositor primitives as
  `ReactiveMaterial`, with the existing coarse `FxMonitorView::paintBackground`
  surface; no enlarged fluid raster or invented waveform. Input is secondary
  tint, output text tint, threshold muted, GR accent.
- `UtilityEffect::dynamicsSnapshot()` exposes a small AUDIO-thread value copy of
  actual smoothed gate threshold and actual wet gate attenuation. It is not a
  thread-safe UI accessor. Existing `getCreativeGainReduction()` ultimately reads
  `FxMonitor`'s atomic per-slot reduction; that safe path remains for the current
  numerical meter, never used to fake historical GR or infer it from RMS ratios.
- `UtilityHistoryAccumulator` is implemented: per-sample O(1), fixed storage,
  no locks/allocations. `snapshot()` copies/reorders at most 200 completed bins.
  Stereo RMS is sqrt(mean((L²+R²)/2)); GR and threshold are sample-mean dB per bin.
- Mono Audition: one-click host-button binding and the actual smoothed DSP fold
  are implemented, but disabled until the coordinator supplies persisted state.
  There is no ephemeral per-panel latch or remembered-width restore hack.

## Required FxMonitor / Routing integration (latency owner)

**Gate graph intentionally shows “Waiting for measured history” until this is
connected.** It never substitutes decorative or UI-clock-generated samples.

1. Include `UtilityTelemetry.h` in `FxMonitor.h`; add the plain payload
   `UtilityHistorySnapshot utilityHistory{};` to `FxMonitorFrame`.
   The panel already detects this exact optional field with a SFINAE adapter;
   no panel changes are needed after the field is added.
2. Add one `UtilityHistoryAccumulator` to each AUDIO-owned monitor Slot (not
   shared global state). Reset with the actual sample rate wherever existing
   history is cleared: retain generation, reset, node identity/type, rate/plan
   changes. Use the existing frame epoch/invalidation protocol.
3. Routing must obtain `step.fx->utility.dynamicsSnapshot()` on the AUDIO thread
   after gate processing. Pass it to monitor capture alongside slot input and
   **final output after slot Mix**, not pre-Mix wet output. An example compatible
   extension is a trailing `UtilityDynamicsSnapshot utilityDynamics = {}` and
   `bool hasUtilityDynamics = false` on capture; accumulate only for Gate.
   Bypass/Mix-zero must publish zero GR (not stale reduction), retaining actual
   measured input/output. For partial Mix, GR still means the internal wet gate
   attenuation, explicitly not an inferred attenuation of the final blend.
4. While capturing Gate, call
   `s.utilityHistory.push(inL, inR, outL, outR, utilityDynamics)` once per sample.
   At the existing ~30 Hz publication, copy
   `frame.utilityHistory = s.utilityHistory.snapshot()` under the **same exclusive
   buffer ownership** as all other frame payload fields. Non-Gate frames must
   clear `utilityHistory`. Never call `snapshot()` from the GUI on a live instance.
   The large payload adds ~3.2 KiB per snapshot buffer / slot, plus accumulator.
5. Keep history capture subscription-aware. Do not run another FFT, allocate in
   the callback, publish each sample, or read the UtilityEffect from the GUI.

No existing FxMonitor, Routing or processor files were edited by this owner.

## Required persisted Mono Audition integration (processor / state owner)

There is **no existing `stereoMono` parameter**. Existing Width=0 folds before
balance/polarity/Mix, so it cannot honestly be used as final-output Mono Audition.
Also, appending to `UtilityParameter` would change the released per-slot stride
and shift `newCreativeParameterStart`; this patch deliberately does not do that.

- Append **15 separate boolean host parameters at the end of the current complete
  parameter table**, without moving any released indices. Proposed IDs:
  `fx1stereo_mono_audition` through `fx15stereo_mono_audition`; name
  `FX N Mono Audition`; range 0/1, default **false**, discrete, preferably not
  modulation-assignable. Store per-slot in processor/routing snapshots and preset
  and graph state. Missing legacy value defaults false. Preserve it on graph
  rebuilds, inspector close/reopen and editor recreation; reset only under the
  same rules as other per-slot controls. It has no latency.
- At every UtilityEffectPanel construction path for Stereo (Routing inspector
  **and** Stack Main Bus FX), call
  `panel.bindMonoAuditionParameter(thePersistedId)` once. This creates the
  existing APVTS `ButtonAttachment`; it returns false for a missing parameter.
  Button uses existing passive-control setup, does not request keyboard focus,
  and has no keyboard shortcut implementation. Native host-shortcut validation
  remains outstanding, not claimed.
- After the Stereo slot's existing dry/wet blend, once per sample, use:
  `result = utility.applyMonoAudition({result.left, result.right}, enabled && monoParameter);`
  Adapt the assignment to the routing sample type. Call even when false, so the
  10 ms transition releases. Call **before output telemetry**. Never process the
  fold twice or apply it before Mix. Keep the existing full-bypass policy: if
  bypass must be bit-exact immediately, bypass the audition result while still
  advancing its false release state.
- The hook returns `(L+R)/2` in both channels when settled, uses a 10 ms smoothed
  linear matrix, and preserves every Width/Balance/Swap/Polarity/Mix value.
  Opposite-polarity content can cancel; it is not a limiter or loudness matcher.
  `prepare/reset` clears audition smoothing. No saved hidden previous width.

## Validation handoff (not executed)

Register the new header functions in the coordinator's existing test harness:

- `UtilityVisualUpdateTests::telemetry()` — six rates, four-second bounded history,
  ordering, exact RMS values, reset/silence and correlation/nonfinite samples.
- `UtilityVisualUpdateTests::dynamicsAndMono()` — actual gate snapshot and reset,
  mono fold/release and unchanged Gain range.
- `UtilityVisualUpdateTests::controls(processor)` — default-hidden / recalled
  nondefault legacy Wet, both palette layouts, unbound audition disabled and
  keyboard-focus flags. Run on the message thread with an unmodulated fixture.

Coordinator must update old UtilityPanelTests layout assumptions: hidden Gain
Mix rows should be skipped; Stereo's first row is now below the Mono button.
Do not delete the old layout/gesture coverage. Add integration coverage for the
new appended IDs/legacy state, enabled audition with non-100% Mix and unequal
polarity, monitor epoch/reset/bypass, multiple simultaneous Gate instances,
threshold automation and registered persisted Mono across inspector recreation.
Capture actual generated signal in both themes and both inspector/rack surfaces;
verify guide units/legend fit at narrow widths, scroll reachability, silent
correlation and old-left/new-right motion. No fake sample fixture for approval.
Before native GUI testing enumerate displays and place fixtures on the actual
non-primary monitor per AGENTS. All Windows build/test commands must use
`tools/Invoke-HalfCpu.ps1` in dedicated 64-bit PowerShell after coordinator merge.
