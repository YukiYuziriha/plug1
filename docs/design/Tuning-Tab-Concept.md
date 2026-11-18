# Tuning tab — UI proposal only

These images are offscreen design renders, not screenshots of implemented plugin
controls. No native windows were opened or placed on a physical monitor. Pitch
curves and readings are illustrative. No DSP, host parameters, routing, preset
schema, or plugin binaries were changed.

## Proposed control contract

| Control | Intended behaviour |
| --- | --- |
| Tuning On | Enable the single shared correction engine. |
| Key / Scale | Choose target pitch classes; individual note buttons permit custom scales. |
| Reference | Editable concert A frequency, initially 440 Hz. The pictured selector is a numeric field concept, not a fixed list. |
| Vocal Range | Constrain detection to the singer's range; the full-range label is an intended design target, not a verified backend specification. |
| Correction | 0–100% pitch correction strength; distinct from an audio dry/wet mix. |
| Retune Speed | Intended correction response time within a target note; zero means no deliberately added correction slew, not zero algorithmic latency. |
| Note Transition | Intended glide time when the selected target note changes; zero gives stepped target changes. |
| Note Stability | Target-note hysteresis to reduce unwanted switching near note boundaries. |
| Preserve Formants | Preserve vocal spectral character while tuning, separate from each voice's creative formant shift. |
| Reset Tuning | Reset this module, not the rest of the patch. |
| Per-voice Tune Mix | True blend of shared clean Lead and tuned Lead, before that voice's existing processing. Include Lead and all eight generated slots; four example cards are shown. |

Retune Speed, Note Transition, and Note Stability describe proposed integration
behaviour. They are not a claim that every donor has those exact independent
parameters or calibrated millisecond mappings. The donor is not selected by this
mockup; final parameter ranges and backend support require DSP validation.

## Visual design

- Third tab alongside Stack and Routing.
- Existing `VisualTheme.h` Device, Neon, and Spectral palette values.
- Existing embedded Pixelify Sans font.
- Square buttons, thin panel borders, rotary arcs, numeric readouts, and horizontal
  mix bars echo the current plugin controls. These are drawn mock controls, not
  interactive JUCE widgets or pixel-exact captures of its look-and-feel.
- Upper area: target notes and detector range.
- Middle: four primary knobs and formant preservation.
- Lower area: input/target pitch monitor and voice Tune Mix controls.

## Regenerate

With Pillow installed:

```text
python tools/Render-Tuning-Concept.py
```

Or use an isolated temporary dependency environment without modifying project dependencies:

```text
uv run --no-project --with pillow python tools/Render-Tuning-Concept.py
```

Outputs: `tuning-tab-device.png`, `tuning-tab-neon.png`, and
`tuning-tab-spectral.png` beside this document.
