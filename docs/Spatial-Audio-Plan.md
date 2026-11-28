# Spatial audio DSP and product notes

**Status:** The research branch has an unreleased HEAVEN Spatial EQ node in CLAP and VST3 test builds. The independent C++ engine matches the tested Bell, Low Shelf, and High Shelf boosts, cuts, and live gain-sign crossings without captured impulse responses or fitted coefficients. The legacy EQ8 is unchanged. Strength is fixed at unity, with no control or parameter. Native VST3 and CLAP tests verify timed Spatial EQ Gain/Depth delivery. DAW validation and human listening remain open.

Reference: [RFA Spatial EQ transcript notes](Spatial-EQ-RFA-Transcript-Notes.md) separate the speaker’s claims from open questions. The [reference measurements and offline tests](Spatial-EQ-Reference-Measurements.md) document the measured behavior and match limits.

## Shared direction

Build an independently written, efficient spatial DSP engine informed by the behavioral trace and verified with offline comparisons. Keep reference binary analysis and decompiler output out of the product. Keep the engine independent of EQ bands and UI so multiple tools can reuse it.

Core building blocks: decorrelation/width, short early reflections, damping of generated space, transient-aware ducking, and low-frequency protection. Use explicit source positioning/panning where required; side-only spatial generation is not a substitute for moving the direct source.

## Product A: Spatial EQ

- One eight-band minimum-phase EQ node with per-band Depth, not a separate spatial node or enable switch. Depth at zero behaves like conventional EQ; skip spatial processing once all bands are at zero and any tails have faded. The direct path does not wait for reflections. Verify zero added host-reported latency in the HEAVEN build, including while moving Depth live.
- Add a per-band **Strength** knob alongside Depth for supported boosts. Depth controls the character of the space; Strength is a linear amplitude trim on that band's generated space, after its reflection and Side processing, before summing with other bands and applying wet cuts. **0%** mutes that contribution, **50%** is unity and the default reference-match point, and **100%** is 2× amplitude (+6.02 dB on generated space only), not a promise of twice the perceived effect.
- Strength must not change the direct EQ, Depth-dependent cuts, decay, detector, or overlap weighting. Smooth Strength changes without resetting histories. Verify that 50% preserves the port's reference match, 0% and 100% scale only the intended contribution, and overlapping boosted bands retain usable headroom. A shallower Depth with more Strength may share a similar width level, but it must not change into the higher-Depth reflection pattern.
- Generate frequency-local stereo space, including from mono sources. Keep the direct EQ and the filter feeding the spatial engine distinct; a shelf does not necessarily spatialize the entire raised range equally.
- Preserve the distinction between boost-generated space and Depth-dependent cuts. Measure what happens to existing input Side and space from other bands.
- Compare the mono fold-down and the stereo difference against the reference. Do not force an exact mono null: the verified boost adds some Mid and a full-Depth cut can change the conventional Mid cut.
- First deliverable: DSP and basic controls, not 3D graphics.
- Later: a nicer primary EQ UI; top-down placement may be an optional secondary view, not the main EQ view.
- Dynamic EQ, dedicated mid/side editing, and linear phase are later stages.

## Product B: Signal Placer (working name)

A separate tool, not merely another EQ view.

Requested signal flow:

1. Collapse incoming audio to 100% mono.
2. Position that mono source in a stereo spatial scene using the shared spatial engine plus direct-source positioning.
3. Output stereo with controllable perceived position, width, and distance.

Planned UI: a top-down space with a draggable dot representing the source position. The dot controls spatial placement; exact coordinate ranges and parameter mappings remain to be designed. Width/spread can remain a separate control rather than being conflated with source position.

Design cautions:

- Stereo-to-mono summing can cancel anti-phase source material; define and document the downmix policy.
- Signal Placer intentionally discards the input stereo image before synthesizing a new one.
- Preserve the distinction between direct-source panning, generated width, and perceived distance.
- Define the placer's mono behavior separately from the EQ's measured fold-down; panning and distance gain policies need their own tests.
- Top-down visuals describe a stereo perceptual scene, not a promise of physically accurate surround or binaural localization.

## Implementation sequence

1. **Verified offline — static Stereo Bell boost.** The no-calibration model includes the delay and feedback paths, both Side injections, and the filtered reflection-return gain. Validate with dual-mono and asymmetric input and retained overlap fixtures, not just isolated clicks.
2. **Verified offline — positive low and high shelves.** Both reuse the reflection engine with their own post-EQ extraction filter. Impulses and sweeps cover 0%, 50%, and 100% Depth with separate direct EQ and Depth-added Mid/Side scores.
3. **Verified offline — Stereo Bell cut and one boost/cut pair.** The cut changes direct Mid and separately filters accumulated wet. Both band orders and a separated-band control are covered. Static overlap weights are verified on overlapping bells, mixed shelves and bells, and eight-band holdouts with cuts.
4. **Verified offline — automation and tails.** The model reproduces tested Depth/frequency/Q/gain changes, slot-ordered overlap updates, switch and eligibility changes, Depth-zero re-entry, and explicit resets. Tests include four/eight-band holdouts with positive bells and shelves and a moving negative Bell fixture. Shape and gain sign stay fixed; host-call boundaries do not establish VST3 in-block delivery.
5. **Integrated in a test build.** The C++ engine is wired into Routing and the Stack rack with eight Depth controls. Native VST3 and CLAP timing tests passed for Gain and Depth; DAW playback and transport checks remain. Static and gain-crossing comparisons on real narration and stereo choir passed, and RMS-matched A/B files were exported but not auditioned. Shape swaps, remaining channel modes, and other spatial controls are outside the verified scope; dynamic EQ and linear phase remain later work.
6. Reuse the engine in the separate mono-in/stereo-out Signal Placer. Build advanced visuals afterward, including an optional top-down EQ view.

## Native VST3 automation finding

The native `vocalstack_vst3_sidechain_smoke` runner loads the built HEAVEN VST3 module and sends `ProcessData::inputParameterChanges` for Spatial EQ Gain and Depth. The focused `--spatial-eq-offsets` mode places jumps at samples 73 and 297 of a 512-sample block (48 kHz stereo), with adjacent old/new queue points for each jump. The reference is the same module with explicit `process()` splits at those offsets. A no-op queue matches an unqueued baseline.

Before the fix, the pinned JUCE wrapper read only the final point and rendered exactly as if that value was sent at sample 0. The regression now requires the queued waveform to match an explicit offset-zero per-sample oracle. At 48 kHz with 512-frame blocks, Gain and Depth jumps, separate and simultaneous ramps, implicit continuation across a block boundary, and a zero-sample flush all matched sample-for-sample. The same runner failed all five timed fixtures on the pre-fix binary. No-op queues and partition-only controls were exact; latency stayed unchanged.

The native CLAP smoke test also offers `--spatial-eq-offsets`. At 48 kHz stereo, timestamped Gain and Depth events at 73, 297, and 511 of a 512-sample block matched explicit host splits exactly, including the one-sample final span. A no-op event list matched the unqueued baseline. Moving the final event to the next block changed the audio, confirming that the comparison detects a one-sample boundary error. This is native CLAP step-event coverage, not VST3 interpolation coverage.

The scoped VST3 fix borrows validated host queues for one callback and uses fixed-capacity cursors. The existing sample loop evaluates normalized linear segments and drives the existing Spatial EQ control smoothing. There is no new processor split or change to MIDI/transport handling, and JUCE still commits final parameter values and processes zero-sample flushes normally. Timing support in this patch is restricted to Spatial EQ Gain and Depth; other parameters keep their existing delivery. Native smoke coverage is not a DAW playback or transport audition.

## Performance and validation

Start with minimum-phase biquads and short preallocated delay/all-pass networks. No allocations or locks in the audio callback; smooth automated controls and bypass inactive processing. Benchmark rather than assuming the reference plugin's slowdown is DSP-related.

Validate impulse responses, mono fold-down, bass leakage, mono vocals, percussion, automation, and CPU at multiple sample rates. Report both full-output and Depth-delta error; use absolute errors alongside relative scores near a null. Offline DSP match and standalone processing cost are documented. No end-to-end DAW CPU benchmark or human listening result is claimed.
