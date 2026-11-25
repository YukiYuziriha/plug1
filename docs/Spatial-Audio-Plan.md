# Spatial audio DSP and product notes

Status: design notes only; no DSP or UI implemented by this document.

## Shared direction

Build an efficient, clean-room spatial DSP engine based on the described behavior, not a claim to reproduce a proprietary implementation exactly. Keep the engine independent of EQ bands and UI so multiple tools can compose it.

Core building blocks: decorrelation/width, short early reflections, damping of generated space, transient-aware ducking, and low-frequency protection. Use explicit source positioning/panning where required; side-only spatial generation is not a substitute for moving the direct source.

## Product A: Spatial EQ

- Conventional EQ with a Depth knob per band; target eight bands after proving one band.
- Generate frequency-local stereo space, including from mono sources.
- Explore distinct boost and cut behavior as described in the reference transcript.
- Preserve the conventional EQ mono response by adding generated space in the side signal; validate with null tests.
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
- Do not automatically impose the EQ's exact mono null target on the placer: panning/distance gain policies need their own explicit mono behavior and tests.
- Top-down visuals describe a stereo perceptual scene, not a promise of physically accurate surround or binaural localization.

## Implementation sequence

1. Prove one spatial EQ band with basic controls.
2. Measure mono compatibility, transient behavior, frequency localization, stability, and CPU usage.
3. Expand to multiple bands while retaining reusable DSP modules.
4. Reuse those modules in the separate mono-in/stereo-out Signal Placer.
5. Build advanced visuals afterward, including optional top-down EQ view.

## Performance and validation

Start with minimum-phase biquads and short preallocated delay/all-pass networks. No allocations or locks in the audio callback; smooth automated controls and bypass inactive processing. Benchmark rather than assuming the reference plugin's slowdown is DSP-related.

Validate impulse responses, mono fold-down, bass leakage, mono vocals, percussion, automation, and CPU at multiple sample rates. No implementation or benchmark results are claimed yet.
