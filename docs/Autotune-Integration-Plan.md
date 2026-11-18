# Replace the hybrid tuner with an intact upstream autotune engine

## Non-negotiable acceptance criterion

With every HEAVEN processor except autotune disabled/neutral, HEAVEN must produce
the same audio as the original selected open-source autotune plugin given the
same input, sample rate, channel configuration, settings, automation and initial
state. If it does not, the integration is wrong and must not ship as completed.

This is an upstream-equivalence requirement, not a claim of Antares equivalence
and not something to replace with subjective listening or sine-wave pitch tests.
The original working plugin is the reference. No new detector, quantizer,
transition algorithm, formant correction or resynthesis may replace its internals.

Only the documented fixed delay needed to align HEAVEN's existing signal path
may be removed for comparison. Do not normalize gain, stretch time, compensate
frequency-dependent phase, trim arbitrary transients or otherwise conceal a
processing difference. Neutral HEAVEN must not add level matching, filtering,
creative pitch/formant processing, motion, stereo changes or parallel signals.

## Preserved work; not the accepted tuner

Branch `work/autotune-donor-integration` preserves the complete 1.12.1 implementation
and history (`22cdc1c`, `f00f1c8`), including matching built binaries and notices.
The current MXTune-detector/Signalsmith hybrid is explicitly NOT the requested
complete upstream implementation and is not the reference for correctness.

Potentially reusable integration, subject to review against the chosen donor:
- One shared clean/tuned source before voice fan-out, true Module/voice Mix.
- Themed Tuning tab, routing/reveal hooks and state/parameter plumbing.
- First-enable host latency reactivation, retained reservation on Off, aligned
  global dry and bypass; CLAP and VST3 wrapper tests.
- UI architecture/focus/history improvements and regression fixtures.

Do not merge this branch wholesale back to main. Main is the non-tuning product;
selectively port only useful infrastructure after the replacement passes gates.

## 1. Select and pin the entire working donor

- Inspect complete candidate source, starting with MXTune, not just its detector.
- Verify license compatibility, all transitive licenses, Windows toolchain,
  original wrapper, supported formats/rates/channels, latency and buildability.
- Build the ORIGINAL plugin from a pinned revision without DSP modifications.
- Record exact source pins, build flags and dependencies and preserve notices.
- If a candidate cannot be embedded intact, explain the specific blocker and
  select another working donor rather than silently inventing a substitute.

Gate: reproducible original-plugin build and executable reference render harness.
No donor is claimed selected or proven by the old detector-only audit.

## 2. Establish the reference before adapting anything

Render the original plugin and capture complete settings/state, input/output,
reported latency, reset/warm-up behavior and automation timing. Include silence,
sustained tones, pitch changes, broadband/transient inputs and fixed recorded
fixtures where available. These are deterministic equivalence fixtures, not a
claim of human listening ability or proof of vocal quality.

Use the SAME upstream engine, precision, dependencies and compiler settings in
both builds. Prefer sample-for-sample / bit-identical output. If external-wrapper
rounding prevents bit identity, investigate and document the exact cause and a
strict floating-point error bound before accepting it; do not choose an arbitrary
loose tolerance after seeing failures. Waveform/null comparison, not just detected
pitch or perceived similarity, is the acceptance test.

## 3. Embed through a thin adapter

- Preserve the donor's complete detection -> target/correction -> resynthesis
  chain and original timing/state relationships.
- Adapt buffer ownership, block delivery, lifecycle, parameters and telemetry,
  not its sound-producing mathematics. Keep core source changes minimal/auditable.
- Handle arbitrary host blocks without changing upstream sample timing.
- All required allocation occurs with processing quiescent. No callback locks,
  I/O or GUI access. Preserve donor reset and state behavior.
- Do not claim linked stereo if the donor is mono. Determine its original channel
  policy and preserve it. Any additional mono-to-stereo policy must be explicit
  and approved, not an unreviewed new shifter or pitch-decision scheme.

## 4. Integrate HEAVEN's routing, UI and host lifecycle

- One donor tuner feeds aligned clean and tuned sources; voices crossfade those
  sources before their existing creative processing. No per-voice tuner farm.
- A neutral Lead/tuning-only test path must reproduce the original donor output
  with unity gain, no other processing and no dry leakage.
- Map controls to REAL donor parameters and units. Replace/remove unsupported
  hybrid controls instead of pretending the upstream engine implements them.
- Use actual HEAVEN theme widgets. Maintain host shortcuts and passive focus.
- Derive latency from the donor, never from the discarded hybrid's constants.
- Automatically coordinate first-enable latency with the host, preserve delay
  alignment and retain reserve through subsequent Off/On toggles.
- Preserve non-tuning parameter IDs/order and legacy preset compatibility;
  explicitly review archived 1.12.x tuning-state migration before reintroduction.

## 5. Required release gates

1. Original plugin vs embedded tuner waveform/null equivalence under identical
   input/settings, initialization, automation and resets.
2. Original plugin vs HEAVEN with ONLY tuning active: same waveform after the
   explicitly documented fixed host/HEAVEN latency alignment. This is the user's
   release-blocking requirement. No gain normalization or hidden cleanup.
3. Supported mono/stereo policies and sample rates, regular/irregular blocks,
   silence, reset, state recall, note changes and parameter automation.
4. True Module Mix and per-voice Mix endpoints/intermediates; clean/tuned and
   bypass alignment; no observed audio-callback allocations; finite output.
5. CLAP/VST3 actual format tests for first activation, deferred host response,
   reentrant lifecycle, no restart loops and stable reserved Off/On latency.
6. Styled UI mappings/layout/focus in all themes, with native fixtures on the
   enumerated non-primary monitor; distinguish offscreen renders from captures.
7. Release binaries and complete corresponding source/notices packaged together,
   version verified and installed only when not loaded/locked by a host.

Do not substitute synthetic pitch accuracy, the current hybrid, UI completion,
or an assumed sonic improvement for gates 1 and 2. Do not call the integration
finished if HEAVEN changes the original tuner's sound in the tuning-only setup.

## Main branch separation

Remove tuning from main by reverting the tuning deliveries without rewriting
published history. Preserve HEAVEN branding and compatibility identifiers. Raise
the UI's minimum font size by 2 points on main, including meters and shared
widget paths; verify layout without simply shrinking text back to the old size.
Do not begin the replacement tuner implementation as part of that rollback.
