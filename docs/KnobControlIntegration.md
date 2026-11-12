# Knob controls / Stack pointer integration — 1.9.0

## Finalized implementation

- `source/src/CreativeEffectPanel.h`: continuous parameters use rotary controls,
  below-knob numeric entry, 130 px minimum cells and 100 px row pitch. Reverb,
  Delay, Compressor, Bitcrusher and Granular retain attachments, reset, assignment
  and edit callbacks.
- `source/src/UtilityEffectPanel.h`: the same grid for Gain, Stereo, Gate, XP and
  Synth Filter. Boolean switches/configuration menus remain discrete controls.
- `source/src/DoublerPanel.h`: `VoiceControls::ControlMode { knobs, stackSliders }`
  defaults to knobs. Stack explicitly selects sliders for Lead and every voice;
  `setCompact()` changes layout, not control style. Routing remains rotary.
- `source/src/ParameterKnob.h`: one light base thumb drawn after the modulation
  range; effective feedback is a separate upper-lane triangle. Both use JUCE's
  actual bounds/value coordinate. Stack's 54 px cells retain below-control values
  and half-speed relative dragging. No duplicate eligibility rail is drawn.
- EQ's `toneui::Control`, modulation numeric/depth controls, Routing and rack
  controls use rotary knobs. Embedded creative/utility panels retain scrolling.
  Parameter IDs, DSP/state and keyboard-entry semantics are unchanged.

## Registered coverage and observed validation

`source/tests/SliderRenderingTests.h` is included and dispatched by
`source/tests/EditorTests.cpp`; it is no longer an unregistered standalone handoff.
Coverage includes Creative/Utility families, EQ, both voice modes, polarity/depth,
inconsistent snapshots, relative dragging and coincident/near-coincident pointer
pixels at 75/100/150/200% scale in Device, Neon and Spectral.

The initial isolated `.cache/knob-validation/build` Release executable passed;
Stack, Routing voice, Reverb and Gain captures were generated and selected images
visually inspected. Final Release CLAP/VST3 builds and **all 34 focused CTests
passed in 146.69 s**, including eight factory shards and 63 editor groups. Separate
legacy DSP passed in 81.8347 s. The prior 33 screenshots plus 12 rack PNGs across
three themes were visually inspected; a subsequent screenshot-output-only
GrainSnapshotUi rerun passed in 0.60 s and added three actual-grain PNGs. Root
Granular and the new Stack FX preview are refreshed. This is not DAW audition.

Release artifacts were copied to tracked packages and SHA256-matched; packaged
CLAP smoke passed 2087 parameters, mono/stereo latency, bypass and automation.
**Installation complete:** after the user closed Bitwig, the coordinator reran
`source/build/install-release.ps1`; both per-user CLAP and VST3 reported
**Installed** and **SHA256 verified**. Release commit `531045b` is pushed to
`origin/main`. Actual Bitwig listening/transport, automation and session recall
remain unperformed. See [release validation](Release-1.9.0-Validation.md) for
package hashes and performance caveats.
