# Vocal Stack 1.11.3 — Routing Output dial sizing

The 1.11.2 control rollback restored the Output rotary but retained 60-pixel-high slider bounds in two Routing layouts. This squeezed the actual dial. Both are corrected:

- Expanded Output node: 110-pixel-wide control, 138-pixel body height, explicit cell rotary geometry.
- Right inspector Output: 110 × 138 control, explicit inspector rotary geometry.
- Numeric and caption lanes remain separate from the dial.
- Stack main controls, normal unsigned Mix fills, parameter ranges, automation, DSP and state are unchanged.

## Validation

- Rebuilt Release CLAP, VST3 and editor test target through the half-CPU wrapper.
- Focused control-layout suite passed across Device, Neon and Spectral themes.
- Added regression coverage of the actual expanded Output card and inspector, requiring rotary style, at least 80 × 80 dial layout space, and separate numeric bounds. The preceding test only verified the rotary style and missed the two production-layout regressions.
- Native fixtures placed on the enumerated non-primary monitor: work area `(3440,357,1920,1032)`, 96 DPI; fixture bounds `(3800,473,1200,800)`.
- Remaining focused checks cover main Stack knob styles/value lanes, normal Mix presentation, inward ring geometry and responsive Morph layout.
- CLAP and VST3 installed; installer verified SHA256 equality with packaged files.
- No DAW listening or full unrelated regression-suite run. No native screenshot or FPS claim.
