# Vocal Stack: Advanced voices and modular FX routing

## Status and scope

This document records the staged implementation plan and the subsequent control-design decisions.

### Current implementation checkpoint: 1.4.0

The user authorized the core through nodes and Voice Motion, excluding follow-up effects. This checkpoint implements eight stable voice slots, musical Pitch/Fine Detune, stacked formants, neutral-origin controls, routing/execution, independent Chorale slots, draft/active graph persistence, native node editing and snapshots.

**1.3.1 clarification:** ordinary disconnections are valid live edits. Unconnected inputs are silence; disconnected Output is silent. Unused disconnected FX cannot block other valid routing. Last-valid fallback applies only to structural errors such as cycles, duplicate/illegal links and unsupported latency—not missing cables. This supersedes the earlier incomplete-draft behavior below.

Implementation choices and limits:

- Bounded JUCE-independent executor rather than AudioProcessorGraph; explicit pins/mixers and off-audio reclamation are tested directly. A separate AudioProcessorGraph prototype was not added.
- All current routing FX add zero dry-path latency. Nonzero additional branch latency is rejected; general latency compensation awaits a module that needs it.
- Topology switching uses a 10 ms output-offset de-click ramp, not simultaneous old/new graph rendering or a spectral crossfade. Retained FX histories are advanced only once.
- Original 46 host IDs/indices remain, with 139 total parameters including fixed FX slots. Wider Formant ranges change normalized host automation mapping; raw saved semitone values are retained.
- Voice Motion plots fine detune vertically and shows musical Pitch separately in the readout.
- Nine core suites plus an optional pinned-history baseline comparison cover automated validation. Listening and transport behavior in the user's Bitwig session remain manual validation.

### Subsequent user amendments implemented in 1.4.0

These decisions supersede conflicting earlier design text below:

- Visible cables are the accepted running topology. No separately visible non-running draft; invalid cycles/duplicate/illegal links are rejected atomically and removed from view.
- Gain compensation is automatic at multi-connection inputs. No visible Sum node or manual compensation box; existing Sum boxes migrate to direct links.
- Cable input ends can be dragged to rewire or dropped on blank canvas to disconnect. Selected cable source ends and Ctrl-output dragging are supported. No Disconnect button.
- Added independent Waveshaper, EQ8 and 2-/3-band crossover modules sharing the 15 additional stable FX slots. Original 139 host parameters stay ordered; appended tone automation brings the total to 874.
- EQ targets EQ+-style graph editing, with Pro-Q selected-band readability as an additional UI reference. Original code/assets and Device/Neon colors remain. There is no spectrum/reference analyzer yet and no claim of matching proprietary coefficients.
- Multiband outputs route through ordinary independent FX and recombine with band-aware unity gains, not voice-count attenuation. Two-band ports #0 Low/#1 High; three-band #0 Low/#1 Mid/#2 High. Stored port #2 cables are dashed/silent in two-band mode, so assignments must be reviewed when switching modes.
- Advanced header groups Voice stack, Add voice, Open Routing and Reset to Basic; routing inspectors have a wider native panel and expanded EQ view.
- XP-style character Filter, delay and reverb remain future work.

The remaining sections retain the original staged design and longer-term requirements; amendments above take precedence where the design evolved.

Keep Basic mode quick to use. Advanced adds **up to eight generated voices, a live voice monitor, and an optional FX-routing editor**, backed by separate processing modules.

Implement in stages, with a working, tested plugin after each stage. The first routing release includes Chorale, not a collection of new effects. Follow-up effects include a waveshaper, an XP-style Filter, and an EQ targeting Bitwig EQ+'s UI and editing workflow, as specified below.

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

## 8. Follow-up effects after nodes

The user requested two additional independent FX modules: a **Filter modeled on Bitwig's XP** and an **EQ targeting Bitwig EQ+'s UI and editing workflow**, not merely a generic filter and basic tone control. For EQ, "behavior" means interaction/UI, not identical DSP or sound. These follow the working routing release; they do not block its initial delivery.

Each module processes incoming audio without knowing whether it belongs to one voice, a group, or the whole stack. Each node owns independent settings/state and uses the same module interface, automation-slot scheme, inspector, and real-time requirements as Chorale. No special voice/master implementations.

### Filter: Bitwig XP as the reference

- Target the XP filter's controls, configurations, response, resonance, drive behavior, and interaction as closely as an independently developed implementation can support.
- Bitwig's official Grid documentation describes XP as inspired by Oberheim, related to Ladder, and offering **15 filter configurations**. XP is also available in Filter+ and Sweep. The target is XP itself, not the entire Filter+ device, its shaper, or its modulation environment.
- Before DSP implementation, inventory all 15 configurations, parameter ranges, defaults, and interactions in the user's Bitwig version. Documentation alone does not specify the complete algorithm or mode list.
- Characterize frequency/phase response, cutoff mapping, resonance, input-level dependence, saturation, and mode switching against reference renders. Test parameter movement as well as static settings.
- Preserve resonant/drive character rather than adding a hidden loudness rider that cancels it. Provide intentional output trim where needed; graph stacking compensation remains the mixer's responsibility.
- Verify numerical stability, silence/reset, extreme cutoff/resonance/drive, sample-rate behavior, and aliasing/oversampling needs.

### EQ: Bitwig EQ+ UI and editing workflow as the target

- The user specifically means **EQ+'s UI and interaction behavior**, not an exact sonic clone. Use independent, well-tested EQ DSP; matching Bitwig's proprietary coefficients or nulling against EQ+ is not a requirement.
- Preserve the EQ+ graph-editing workflow rather than substituting a simplified two- or three-band UI.
- Verified documented feature baseline: up to **eight bands**, **14 modes per band**, global frequency **Shift** and **Gain**, **Adaptive-Q**, and a spectrum display with a **Reference** track option.
- Match the documented editing gestures:
  - Double-click to add a peak at the cursor.
  - Drag the left/right ends of the curve to add low/high shelves.
  - Drag the left/right graph edges away from the curve to add low/high cuts.
  - Drag the lower graph edge to add a notch.
  - Give clear cursor/interaction feedback for each operation.
- Inventory the complete mode list, slopes, ranges, defaults, Q/bandwidth conventions, global control semantics, band enable/delete behavior, and remaining interaction gestures in the reference version before implementing. Do not invent undocumented details.
- Validate our EQ's magnitude/phase response across modes and sample rates, Q/gain interaction including Adaptive-Q, global Shift/Gain behavior, high-frequency response, automation smoothing, and bypass behavior. These are correctness tests of our DSP, not requirements to match EQ+'s sound.
- Draw the response of the actual implemented filters, not a decorative curve. Publish analyzer snapshots without GUI access to DSP state or audio-thread allocation.
- Band gain and global EQ Gain use neutral-origin bipolar rendering around 0 dB. Frequency is logarithmic; Q and other positive-only controls remain unipolar. Confirm the reference meaning/units of Shift before choosing its mapping.
- EQ+'s Reference spectrum feature needs an explicit reference-audio source. Resolve whether the plugin will use a host sidechain/reference input before promising full parity; graph audio cables alone do not provide an external Bitwig track automatically.
- Keep the native plugin skins and keyboard-focus policy. Reproduce useful interactions with original UI code/artwork rather than copying Bitwig assets.

### Reference fidelity and implementation boundary

- **EQ target: UI/workflow parity, not sonic equivalence. Filter target: XP-style controls and sonic character.** Keep these requirements distinct. Use independent DSP/UI implementations; do not copy proprietary code, assets, or presets.
- Record the Bitwig reference version and interaction checklist for UI comparisons. Record reference renders, settings, and comparison tolerances where testing XP's sonic character. Report differences explicitly; do not label an unmeasured approximation an exact clone.
- Public documentation does not reveal proprietary coefficients, nonlinear models, or all interaction details. Resolve missing UI details through observation and user confirmation; use reference measurements where needed for the XP filter. EQ does not require reconstructing proprietary coefficients.
- Reference recording/comparison for XP requires access to Bitwig and suitable test renders; automated tests of our implementation alone cannot establish sonic equivalence.

References:

- EQ+: https://www.bitwig.com/userguide/latest/eq/
- XP: https://www.bitwig.com/userguide/latest/grid_modules#tdq_5cf_t4b

## Scope boundary

The first routing release includes **Chorale**. Follow-up modules are **waveshaper, XP-style Filter, and EQ+-style EQ**; their exact delivery order can be chosen after the routing milestone. A switchable **2-/3-band routing split and recombine** is also requested, as specified below. Adding an effect should require its DSP module, parameter definition, and controls, not changes throughout the voice engine.

Delay, reverb, and a full Thermal-like multiband distortion/macro system remain later possibilities, not committed scope in this plan. The 2-/3-band audio routing feature below is committed planning scope, not merely a future possibility.

## 9. Two-/three-band processing

- Add a multiband routing facility with selectable **2-band** and **3-band** modes.
- Two-band mode exposes **Low / High**, with one crossover frequency.
- Three-band mode exposes **Low / Mid / High**, with two ordered crossover frequencies.
- These are actual audio branches, not merely EQ bands: each band can feed its own independent FX chain, then return to a matching recombine node.
- Example: clean lows, saturated mids, and Chorale on highs. FX modules remain unaware of which band or voice feeds them.
- Permit the split on an individual voice, a grouped stack, or the whole signal through ordinary routing; no special per-location DSP implementations.
- This is a focused band splitter/recombiner, not a full Thermal-style distortion environment or arbitrary modulation system.

### Crossover and recombination contract

- Choose and document a complementary crossover design, including slopes, phase response, and latency, before implementation. Do not assume arbitrary low/high-pass filters will recombine correctly.
- Test two- and three-band recombination with all band chains neutral: document the expected magnitude, phase, and latency response. Flat magnitude does not automatically imply sample-identical dry audio.
- **Band recombination is not voice stacking.** Do not apply count-based voice attenuation when recombining complementary frequency bands; that would incorrectly reduce the reconstructed signal.
- Use a distinct, explicit band-recombine operation with the appropriate unity reconstruction gains. Preserve intentional per-band level changes.
- Account for different FX latency on each band using the executor's latency handling; reject unsupported arrangements rather than silently misaligning branches.
- Smooth crossover movement, handle frequency ordering and sample-rate limits, and switch 2-/3-band modes without unstable coefficients or abrupt clicks.
- Define how inactive Mid-band wiring/settings are preserved when changing modes. Do not silently delete FX chains or repurpose stable automation identities.
- Cover neutral reconstruction, isolated bands, crossover extremes/movement, mode changes, stereo/mono, irregular blocks, bypass/reset, silence, and independent per-band processing.
- Validate the added CPU cost with eight voices and the existing bounded FX instance budget. The splitter/recombiner adds no permission for graph cycles or audio-thread rebuilding.

**Scope note:** EQ covers ordinary tonal filtering. The separate XP-style Filter remains a requested character-filter follow-up, not a prerequisite for multiband splitting; prioritize EQ and useful routing before duplicating basic filtering functionality.
