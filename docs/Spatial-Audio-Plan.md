# Spatial audio DSP and product notes

**Status:** The offline model matches the tested static Stereo Bell boosts and cuts and positive low and high shelves in presets of up to eight bands, without captured impulse responses or fitted coefficients. Single positive Stereo Bell automation and tail lifecycle also match the tested host-call-boundary renders. Multiband automation and VST3 in-block parameter delivery remain unverified. No HEAVEN DSP or UI integration yet.

Reference: [RFA Spatial EQ transcript notes](Spatial-EQ-RFA-Transcript-Notes.md) separate the speaker’s claims from open questions. The [reference measurements and offline tests](Spatial-EQ-Reference-Measurements.md) document the measured behavior and match limits.

## Shared direction

Build an independently written, efficient spatial DSP engine informed by the behavioral trace and verified with offline comparisons. Keep reference binary analysis and decompiler output out of the product. Keep the engine independent of EQ bands and UI so multiple tools can reuse it.

Core building blocks: decorrelation/width, short early reflections, damping of generated space, transient-aware ducking, and low-frequency protection. Use explicit source positioning/panning where required; side-only spatial generation is not a substitute for moving the direct source.

## Product A: Spatial EQ

- Conventional EQ with a Depth knob per band; target eight bands after the static and automated multiband paths are validated.
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
4. **Verified offline — single positive Stereo Bell automation and tails.** The model reproduces the tested Depth/frequency/Q/gain steps and a combined ramp, per-sample enable transitions, Depth-zero re-entry, and explicit resets. Host-call boundaries do not establish VST3 in-block parameter delivery. Next, implement and verify the traced slot-ordered overlap updates for moving bands and changing eligibility.
5. **Integration gate.** Validate automated held-out multiband presets and run level-matched A/B on real vocals and stereo material. Then integrate basic per-band controls and benchmark the realtime engine before visual polish. Validate remaining shapes/channel modes and spatial controls as separate cases; dynamic EQ and linear phase remain later scope.
6. Reuse the engine in the separate mono-in/stereo-out Signal Placer. Build advanced visuals afterward, including an optional top-down EQ view.

## Performance and validation

Start with minimum-phase biquads and short preallocated delay/all-pass networks. No allocations or locks in the audio callback; smooth automated controls and bypass inactive processing. Benchmark rather than assuming the reference plugin's slowdown is DSP-related.

Validate impulse responses, mono fold-down, bass leakage, mono vocals, percussion, automation, and CPU at multiple sample rates. Report both full-output and Depth-delta error; use absolute errors alongside relative scores near a null. Offline DSP match results are documented; no realtime CPU benchmark or listening result is claimed yet.
