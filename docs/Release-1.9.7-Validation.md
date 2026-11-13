# Vocal Stack 1.9.7 validation

## Scope

- Procedural phase speed is 1.2 times the previous speed for FluidBackground
  and ParameterKnob slider noise. Grid size, 10/15 Hz cache refresh and editor
  30 Hz timer are unchanged; no new timers.
- The four top Stack controlPlates (Leveler, generated takes, Formant, Output)
  use individual cached FluidBackground surfaces in Device, Neon and Spectral.
- Editor and inspector button styles use the same muted material with a cache
  per live button. Safe component references discard caches for destroyed buttons.
  Existing active/hover/pressed colours, disabled treatment, borders, text and
  rounded Device clipping are preserved.
- No DSP, host parameter, preset/state-format or keyboard-focus changes.

## Observed validation

- Full Release build succeeded. The initial 200-second build command timed out;
  a bounded incremental resume completed, followed by a successful final build.
- CTest Release, excluding only VocalStackDSP: **34/34 passed, 521.33 seconds**.
  This includes all 68 editor groups, rack UI, baseline compatibility and the
  eight factory-audio shards. The long legacy DSP suite was not rerun.
- Focused Fluid pixel group passed again after adding screenshot capture.
  Coverage includes deterministic inherited/frozen phases, invalid-time handling,
  muted contrast, bounded grid, all four actual Stack card strips, animated
  buttons, distinct active state and passive keyboard focus in all three themes.
- Device, Neon and Spectral actual Stack editor captures were visually reviewed:
  card silhouettes, text, knob values/arcs and selected buttons remain readable.
  Captures are machine-local in source/build/stack-material-197-{0,1,2}.png.
- Broad --render runner failed with **Tone UI: Missing FX tab** before producing
  the requested Stack captures. No unrelated screenshot-helper changes were made;
  focused Stack captures above succeeded instead.
- Packaged CLAP smoke passed: 2087 parameters at original indices; stereo 48 kHz
  and mono-to-stereo 44.1 kHz latency 1344 samples; mono 96 kHz latency 2688 samples;
  state, latency, bypass, automation and offline layouts passed.
- Tracked Windows-x64 CLAP and VST3 binaries refreshed from Release artifacts.
- Existing source/build/install-release.ps1 succeeded for both per-user formats,
  reporting Installed and SHA256 verified. Bitwig was not terminated.
- Actual Bitwig listening/transport and native FPS checks remain unperformed.

## Packaged SHA256

- CLAP: `601ce3a0358c75b16396bba8bd743494cccbbfa19d45156fc5506b206bd79f02`
- VST3: `aae4cc9bc42dbe0a2156847b75b2798ed622bd3a33feaada7528173b9181afcb`
