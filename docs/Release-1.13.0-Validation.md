# HEAVEN 1.13.0 validation

Windows x64 Release CLAP and VST3 were built using `tools/Invoke-HalfCpu.ps1`,
packaged under `Windows-x64`, and installed in the per-user Common plugin
folders. Both installed binaries matched the packaged SHA-256 hashes in
[`BUILD-PROVENANCE.json`](../Windows-x64/BUILD-PROVENANCE.json).

Focused CTest checks passed (3/3):

- `VocalStackCreative`: verifies the exact stereo grab window, pinned-slice
  reuse, Replace/Layer, quantized launch/cancellation, and Auto tail capture.
- `VocalStackFactoryPresetValidation`: verifies fresh dry startup, 30 factory
  presets, sample-offset MIDI with modulation idle, PPQ-aligned replay, seek
  cancellation, and unchanged reported latency with the glitch node.
- `VocalStackCLAP`: exercises the built wrapper and verifies the appended host
  parameters and state. The original 2357 parameter indices stay in place.

No Bitwig listening, native GUI layout check, or full regression suite was run.
The host-timing integration check uses a test playhead, not a live DAW.

See [Getting Started](Getting-Started.md#vocal-glitch) for controls and MIDI
mapping. Playback patterns are one-shots set to the tempo at launch. Auto is
opt-in and level-triggered, not phoneme recognition.
