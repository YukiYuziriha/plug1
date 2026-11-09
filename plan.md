# Vocal Stack: Advanced voices and modular FX routing

## Status and scope

This document records the staged implementation plan and the subsequent control-design decisions. Creating this plan does not begin implementation of the plugin changes.

Keep Basic mode quick to use. Advanced adds **up to eight generated voices, a live voice monitor, and an optional FX-routing editor**, backed by separate processing modules.

Implement in stages, with a working, tested plugin after each stage. This release includes routing with Chorale, not a collection of new effects. A waveshaper comes afterward as the first proof of extensibility.

**Architecture goal:** adding an effect must not require teaching it what a voice is.

## 1. Separate the existing engine's responsibilities

Refactor without intentionally changing the current sound:

- **Voice bank:** generates the lead and individual doubled signals.
- **FX modules:** independent processors with prepare, reset, process, settings, and latency/tail information.
- **Routing model:** stores nodes, connections, and stable identities.
- **Routing executor:** runs those connections in the correct order.
- **Mixers:** handle compensated summing and the main dry/wet blend.
- Keep host parameters/state, UI, and metering outside these components.

**Checkpoint:** the default routing matches the current plugin's output. Chorale becomes an ordinary FX instance, not a special hardcoded processing step.

## 2. Expand Advanced to eight stable voice slots

- Fresh Advanced configuration shows **two voices and + Add voice**.
- Add complementary voices up to eight generated voices; the lead does not count toward this limit.
- Separate **Remove** from **On/off**:
  - Off preserves the card and settings for auditioning.
  - Remove disconnects and hides that slot.
- Do not renumber surviving voices or transfer their automation.
- Preserve the stack across panel closure and Advanced toggling.
- **Reset to Basic** returns to two Basic-derived voices.
- Restore existing four-voice sessions without losing their settings.
- Avoid unnecessary processing for unused slots, with clean initialization/fades when added.
- Use a bounded, scrollable card area rather than making the window eight cards tall.

### Actual musical Pitch and separate Fine Detune

- Add a prominent per-voice **Pitch** control in **semitones (st)**.
- Range: **-36 to +36 st**, with **0 st** as the default and centered neutral point.
- This must perform actual musical pitch shifting, including octave and harmony intervals, not merely relabel the existing subtle detuning control.
- Support fractional semitones for fine adjustment.
- Retain **Fine Detune** separately in secondary voice controls, in cents. Its existing range is -100 to +100 ct.
- Preserve complementary small detune defaults for the doubler's character; do not make the main Pitch control nonzero to represent them.
- **Human** retains its separate role: time-varying pitch/timing/formant movement around the chosen settings. Human at zero stops random movement, not musical Pitch or fixed Fine Detune.
- These are fixed pitch intervals, not automatic scale-aware harmonization or pitch correction.
- Verify DSP quality, latency, and behavior across the full requested pitch range; do not assume extending a parameter range alone implements it.

### Formant ranges and stacking

- Expand the **main large Formant knob** to **-24 to +24 st**.
- Expand each **per-voice Formant** control to **-24 to +24 st**.
- Both are bipolar, with **0 st** as the neutral point.
- Shared and per-voice shifts **add together**. Do not cap their combined shift at +/-24 st.
- Example: shared +24 st plus voice +24 st requests an effective **+48 st**; likewise -24 plus -24 requests **-48 st**.
- Validate the full combined range in the DSP. Do not silently clamp away the requested stacking behavior.
- Preserve the existing distinction: disabling shared Formant does not disable independent Advanced voice formants.
- Retain fine adjustment near zero despite the wider ranges.
- A neutral point is not necessarily a preset default: complementary existing per-voice formant defaults may remain nonzero.

### Slider and knob behavior

Signed controls must show displacement from their neutral point, not look half-applied at neutral. Apply consistently in both skins.

| Control | Fill behavior | Neutral/reference |
|---|---|---|
| Voice Pitch | Bipolar, outward from center | 0 st |
| Fine Detune | Bipolar, outward from center | 0 ct |
| Pan | Bipolar, outward from center | Center / 0 |
| Per-voice Formant | Bipolar, outward from center | 0 st |
| Main Formant | Bipolar, outward from center | 0 st |
| Voice Gain | From the unity marker toward cut or boost | 0 dB |
| Output Gain | From the unity marker toward cut or boost | 0 dB |

- Pan must clearly distinguish left, center, and right; L/C/R formatting is a useful presentation option.
- Gain ranges are asymmetric. The fill origin must be the actual **0 dB position**, not an incorrectly assumed geometric midpoint.
- Keep **Leveler, main Mix, Human, Width, Delay, Depth, Rate, Chorale Mix, and Chorale Motion** unipolar, filling from minimum.
- Width at zero collapses the spread; it is not a signed pan control.
- Depth at 0x stops motion. A 1x reference tick is useful, but does not make Depth bipolar.
- Neutral and preset default are different concepts. Do not reset intentional voice pan, delay, detune, or formant defaults merely to center every slider.
- Existing main Formant and Output knobs already draw from zero; Advanced sliders currently need the corresponding neutral-origin rendering.

## 3. Implement and test routing before drawing cables

Initially support:

| Node | Job |
|---|---|
| Lead / Voice sources | Expose generated audio |
| Chorale | Process any connected audio |
| Compensated Sum | Combine branches without an automatic stacking-volume boost |
| Dry/Wet Mix | Blend two inputs using the main Mix knob |
| Output | Final trim and output metering |

Connections determine whether an effect processes one voice, a group, or the whole result. **An effect has no voice or master mode.** Each FX is its own machine that processes incoming audio; routing decides what feeds it.

Rules:

- One effect node is one independent processor instance with its own state.
- Two voices feeding one effect through a mixer are processed as a sum; two effect nodes process independently.
- Require an explicit mixer to combine multiple signals, making compensation visible and predictable.
- Preserve relative voice gains; do not boost a quieter voice back up.
- Reject loops, duplicate connections, and invalid wiring.
- Start with a limit of **16 FX instances**, separate from source/mixer nodes.
- No arbitrary modulation cables or topology automation initially.
- The main Mix is a dedicated dry/wet node: the lead is its dry branch and generated voices form its wet branch. Effects can be placed before or after it through routing.

**Default graph:** lead -> dry input; voices -> compensated wet sum -> wet input; main Mix -> Chorale -> Output.

### Compensation contract

- Extend the Basic doubler's stacking-compensation intent to explicit routing mixers: combining voices should add thickness, not an automatic volume boost.
- Preserve intentional relative gain changes, such as one voice being -6 dB.
- Do not add an automatic output loudness rider, Auto Level feature, or limiter.
- Compensation belongs in mixers, not in each FX module.
- Define and test compensation for grouped merges, fan-out, and reconvergence; do not assume a simple voice count covers duplicated branches and independent voices equally.
- Do not promise identical sample peaks, RMS, or perceived loudness for arbitrary correlated signals and effect outputs.

**Checkpoint:** offline tests prove routing, ordering, compensation, and independent effect state before the graph UI exists.

## 4. Make live routing changes safe

- Validate and prepare routing changes **off the audio thread**.
- Publish a prepared execution plan at a safe processing boundary.
- Smooth or crossfade changes without advancing shared effect state twice.
- Preserve unaffected effect instances and their history.
- Handle branch latency explicitly; reject unsupported latency cases rather than silently misaligning audio.
- No allocation, locks, graph rebuilding, or object destruction in the audio callback.
- A bad edit must leave the last valid graph running, not silence or crash the plugin.

## 5. Add the native JUCE node editor

Use a small native JUCE canvas, keeping the processing backend independent of the UI choice.

- Drag nodes and connect audio pins.
- Pan/zoom, select, disconnect, delete, and bypass.
- Add effects from a menu.
- Select an FX node to edit its controls in an inspector.
- Show invalid connections clearly.
- Save node positions separately from audio connections.
- Add undo/redo for graph edits.
- Preserve DAW shortcuts, especially Space. Only deliberate numeric text entry should temporarily own keyboard input.

Before committing to the backend, prototype `AudioProcessorGraph` against the real-time requirements. Use it if it fits; otherwise use a small bounded executor, not two competing graph systems.

## 6. Add the voice-motion monitor

Above the voice cards:

- Horizontal position: effective pan, including Width.
- Vertical position: current detune/motion. With the new wide musical Pitch range, explicitly distinguish base musical Pitch from fine detune/motion in the display; finalize a readable vertical scale before implementation.
- Number/color: stable voice identity.
- Brightness: measured voice activity.
- Subtle trails: actual modulation history.
- Selected-voice readout: musical Pitch, current detune, extra delay, effective formant, and gain.
- Exclude shared algorithm latency from the displayed per-voice extra delay.
- Clicking a dot reveals/highlights its card; selecting a card highlights its dot.
- Initially no draggable dots.
- Label it **Voice Motion**, not a stereo/correlation analyzer.

Publish lightweight audio-thread snapshots. **No invented animation and no GUI access to live DSP objects.**

## 7. Compatibility, testing, and delivery

Cover:

- Existing session migration and unchanged existing parameter IDs/order.
- Append new voice/Pitch/FX automation parameters; stable slot identities must survive removal and restoration.
- Preserve existing Fine Detune values and sessions rather than reinterpreting cents as semitones.
- Expand Formant ranges with explicit attention to saved-state recall and host automation mapping. Document any unavoidable compatibility impact.
- New voice/FX automation slots with stable identities.
- Mix endpoints, relative gains, compensated merges, and bypass.
- Full Pitch range, fractional pitch, Fine Detune, and Human combinations.
- Shared and per-voice Formant at both individual extremes and stacked +/-48 st, including independent voice formants with the shared switch off.
- Add/remove/reconnect during playback.
- Mono/stereo, supported sample rates, irregular blocks, silence, reset, and tails.
- Both skins, scrolling, graph interaction, keyboard focus, and neutral-origin fills.
- Worst-case eight-voice/FX CPU usage, including extreme pitch/formant settings.

At useful implementation milestones:

1. Run relevant automated tests and build CLAP/VST3.
2. Update release version/documentation when appropriate and refresh tracked packages.
3. Install only if both installed plugin binaries are unlocked; never terminate Bitwig or force an overwrite.
4. Verify packaged/installed artifacts and run smoke tests.
5. Commit and push completed work.
6. Clearly distinguish automated validation from actual listening/transport checks in Bitwig.

Documentation-only changes do not require rebuilding or reinstalling unchanged plugin binaries.

## Scope boundary

This release includes the routing system with **Chorale**, not a collection of new effects. A waveshaper follows afterward as the first demonstration that adding an effect requires only its DSP module, parameter definition, and controls, not changes throughout the voice engine.
