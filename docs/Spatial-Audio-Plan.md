# Spatial audio DSP and product notes

**Status:** The offline model matches the tested static, positive-gain Stereo Bell without captured impulse responses or fitted coefficients. No HEAVEN DSP or UI integration yet. Shelves, cuts, overlapping bands, and automation are not yet matched.

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
2. **Next — low and high shelves.** Trace the shape-specific spatial feed and confirm whether both boosts use the existing reflection engine. For each shelf, run a sweep and an impulse at 0%, 50%, and 100% Depth. Score the direct EQ and the Depth-added Mid and Side separately. Add only behavior that differs from the bell.
3. **Cuts and overlapping bands.** Verify one negative-gain band at the same Depth settings first. Then trace direct EQ, spatial feed, overlap weighting, and cut order for one overlapping boost/cut pair. Swap band order and compare with a separated-band control, including what happens to another band's generated Side. Expand to eight only after these cases match.
4. **Automation and tail lifecycle.** Trace and test Depth, frequency, Q and gain steps and ramps. Move or disable a band while its tail is ringing, then re-enable it. Verify parameter smoothing, coefficient updates and reset behavior across sample rates and block sizes.
5. **Integration gate.** Validate held-out multiband presets and level-matched A/B on real vocals and stereo material. Then integrate basic per-band controls and benchmark the realtime engine before visual polish. Validate remaining shapes/channel modes and spatial controls as separate cases; dynamic EQ and linear phase remain later scope.
6. Reuse the engine in the separate mono-in/stereo-out Signal Placer. Build advanced visuals afterward, including an optional top-down EQ view.

## Performance and validation

Start with minimum-phase biquads and short preallocated delay/all-pass networks. No allocations or locks in the audio callback; smooth automated controls and bypass inactive processing. Benchmark rather than assuming the reference plugin's slowdown is DSP-related.

Validate impulse responses, mono fold-down, bass leakage, mono vocals, percussion, automation, and CPU at multiple sample rates. Report both full-output and Depth-delta error; use absolute errors alongside relative scores near a null. Offline DSP match results are documented; no realtime CPU benchmark or listening result is claimed yet.
