# Vocal Stack 1.11.2 — control presentation correction

- Restored circular Lead / Stack and Output knobs in the main Stack controls, with the same bounds as neighboring knobs.
- Restored Output trim knobs in Routing cards and inspector.
- Removed midpoint/bipolar presentation from all Mix controls. Mix remains an unsigned 0–100% dry/wet blend, filling from the dry endpoint.
- Preserved parameter IDs, ranges, automation, state and audio processing.

## Validation

- Release CLAP, VST3 and editor test targets rebuilt through `Invoke-HalfCpu.ps1` (6 of 12 logical processors).
- Focused offscreen control-layout test passed: rotary Output/Lead-Stack, ordinary Mix presentation, value lanes, inward ring geometry and responsive Morph layout.
- Packaged CLAP state, latency, bypass, automation and mono/stereo sidechain layout smoke checks passed at 44.1, 48 and 96 kHz.
- Installation attempted but blocked by the installed CLAP being in use. The installer preflight left both installed files unchanged; close the plugin host before retrying.
- No native desktop screenshot or DAW listening check was performed for this UI-only correction. Offscreen checks have no physical monitor placement.
