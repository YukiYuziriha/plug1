# Control layout update — source handoff

## Scope and status

Presentation-only edits layered on the existing uncommitted Preview 1.11.0
work. No DSP, host parameter, range, state, latency or audio-routing changes.
No DLL/plugin builds, tests, GUI captures, package/dependency installation,
commit or push were performed for this handoff. The coordinator must validate
and register the new tests before calling this complete validation.

Files touched by this task, exactly:

- `source/src/ParameterKnob.h`
- `source/src/CreativeEffectPanel.h`
- `source/src/PluginEditor.h`
- `source/src/PluginEditor.cpp`
- `source/src/MainBusFxRackPanel.h`
- `source/src/SpectralMorphVisual.h`
- `source/tests/ControlLayoutUpdateTests.h` (new)
- `docs/Control-Layout-Update.md` (new)

Other existing modified/untracked files belong to the prior work or concurrent
agents and were not edited by this task. `RoutingPanel.h`,
`UtilityEffectPanel.h`, existing test headers, `EditorTests.cpp` and CMake are
outside this task's write ownership.

## Controls

- Stack Output and Lead / Stack use the shared thick horizontal slider renderer.
  Global Mix also uses a horizontal bar. Their compact values have a separate
  top lane, not in-track text squeezed into a 90 px control. Short in-track
  captions identify Output / Blend / Mix; the existing Lead / Stack title stays.
- `ParameterKnob` automatically presents `outputGain` as a thick slider, including
  Routing's existing Output-node controls. This is the actual host parameter for
  global output: **-24 to +12 dB**, unchanged. `globalOutput` is accepted as a
  presentation identifier only; it is not a new parameter. Gain FX's existing
  +24 dB upper limit is unrelated and was not changed.
- `configureCrossfadeDisplay(juce::Slider&, bool leadStack = false)` opts into
  midpoint-anchored presentation. The fill travels left/right from 50%, with
  the base marker and effective modulation marker still using the actual
  unsigned 0..100 coordinates. Nothing becomes a negative mix amount.
- Generic text is `50% Wet`; Stack-specific text is `50% Stack` under the existing
  Lead / Stack caption. The parser accepts plain numbers, the displayed value,
  and explicit `Dry50/Wet50` or `50 / 50` text. Endpoint clamping is 0..100.
  Lead / Stack keeps its existing linear crossfade; no equal-power mapping or
  backend formula changed.
- Generic shared `ParameterKnob` Mix controls (FX, Chorale, global and local
  `mix*Balance`) and Creative FX Mix opt in. Routing's independent `nodeValue`
  and Utility's independent rows require their owners' integration below.
- Actual circular rotaries stay circular. Shared progress/track strokes grow
  inward to up to 8 px (bounded on small dials), keeping the previous progress
  ring's outer edge. The face is constrained inside the thicker ring. Stack's
  separate rotary renderer follows the same inward rule. No square-prototype
  deployment, cell-size reduction, or FX knob-to-slider conversion was made
  except the requested output/blend controls.

## Morph layout

`SpectralMorphPanel::usesWideLayout(int width, bool inspector)` is true for
non-inspector panels at width >= 680 px. The Stack rack explicitly chooses the
non-inspector mode. Wide layout reserves 40% of the inner width, bounded to
260..420 px, for the rectangular spectrum at left, a 12 px gap, then the mode,
FFT, freeze and knob controls at right. Large 138 px cells and their separate
value/caption lanes remain. The four knobs remain in one row at the breakpoint.

Narrow panels and inspector mode retain the spectrum above controls, responsive
1..4 columns and vertical scrolling. The mode/FFT/freeze row, legend, axis,
donor status and Open Routing action keep separate reserved lanes.

The existing Main Bus FX height remains **388 px**. Default/saved editor sizes
and the existing outer Stack scrolling constants were not changed.

Read-only geometry APIs for regression/capture tooling:

- `SpectralMorphPanel::usesSideBySideLayout() const`
- `SpectralMorphPanel::spectrumBounds() const`
- Component IDs: `spectralMorphControlsViewport`, `spectralMorphControlsContent`
- `ParameterKnob::Style::rotaryRingWidth(float outerRadius)`

## Cache / repaint changes for performance review

`ParameterKnob::Style` has separate track/value `FluidBackground` instances:
painting differently coloured arcs no longer invalidates a single material
cache twice per dial paint. Each uses stable full-dial texture bounds rather
than changing the material raster size with the value arc's bounding box.
Track and value stroked paths have independent local geometry caches keyed by
centre, radius, angles and thickness. Colour, phase and size invalidation remain
with the existing material cache; no global cache or DSP work was introduced.

`ModulationSlider::refreshModulation` still updates effective/depth/assignment
state and the Assign numeric-entry/focus policy for hidden duplicate controls.
It requests a repaint only while showing and when display state or the 10 Hz
material frame changes. Dirty state survives hidden refreshes, so the next
visible refresh paints the latest state. Inconsistent snapshots still retain
previous consistent effective feedback. JUCE attachment/base-value repaints are
not suppressed, and this is not a claim that every hidden-component operation
is eliminated.

The editor marks its fully background-painted layout canvas opaque and subtracts
its visible, viewport-clipped rectangle from root damage. This retains root
refresh below short content in tall windows and avoids requesting a full-editor
repaint every decorative frame. Canvas, editor-owned popup/tooltip peers,
modulation feedback and toggle feedback retain refresh. Broader canvas-panel
damage subtraction was deliberately not retained: voice/rack surrounds still
rely on the parent clock, so excluding them could freeze their materials. The
30 Hz toggle traversal is also intentionally unchanged to avoid losing
assignment/telemetry feedback. No measured performance improvement is claimed.

## Required owner handoffs

### Routing owner (do not edit this file from the control-layout task)

1. Use `configureCrossfadeDisplay(nodeValue)` after Routing installs its own
   formatter for main-controls `double`. Use generic Dry/Wet wording in Routing;
   **only Stack** uses the Lead / Stack-specific formatter. The independent
   nodeValue renderer must consume `hasMidpointCrossfade()` or switch to the
   shared `ParameterKnob::Style`; the helper property alone cannot change an
   unrelated look-and-feel renderer.
2. Audit expanded/compact mix cards and inspector labels/tooltips for generic
   Dry/Wet language and the true 0..100 endpoints. Do not rewrite the mix DSP.
3. Output-node `ParameterKnob` now selects a horizontal thick bar automatically.
   Compact card and inspector layout should allocate at least 54 px total height
   for its separate 20 px value, 8 px modulation lane and 25 px track. Use the
   full available inspector width where practical rather than the former fixed
   140 px rotary slot. Preserve `outputGain` attachment, reset/automation and
   -24..+12 dB. Do not substitute Gain FX's +24 range.
4. Test expanded/collapsed Output and Mix nodes and inspector switching, undo,
   native numeric-entry policy, passive wheel and Assign gestures.

### Utility / Gain owner

1. Call `configureCrossfadeDisplay` on the utility panel's own Mix row after
   assigning its text functions. Do not call it on Gain dB or non-mix percentages.
2. Shared `ParameterKnob::Style` now recognizes `vocalstackMidpointCrossfade` in
   linear, textured-bar and rotary fill origins, including midpoint ticks and
   the in-track value contrast mask. Reuse that style for Gain's slider if useful;
   there is no separate new Gain styling dependency or range edit.
3. `vocalstackSeparateBarValue` reserves a separate 20 px value lane and uses a
   short in-track caption. Only opt in when at least 54 px height is available.
   Generic Gain/Mix layout changes remain owned by that agent.

### Performance owner

Read-only review of these files: check arc geometry/material cache invalidation,
Motion=0 repaint suppression, hidden-to-visible state, opaque canvas/root damage
and tall-window uncovered root regions. Do not concurrently edit this scope;
report findings to the coordinator. Routing changes remain with its owner.

## Test registration API (coordinator)

New `source/tests/ControlLayoutUpdateTests.h` is self-contained and must be
included **at translation-unit scope**, not inside an anonymous/test namespace:

```cpp
#include "ControlLayoutUpdateTests.h"
```

After `juce::ScopedJuceInitialiser_GUI`, on the message thread, register a focused
runner dispatch to:

```cpp
vocalstack::controlLayoutUpdateTests::run();
```

Individual entry points are `crossfadePresentation()`, `outputAndRingGeometry()`,
`morphResponsiveLayout()` and `editorControls()` in that namespace. Failure uses
`std::runtime_error`; no pre-existing runner `expect`, timers, native desktop
attachment or audio processing is required. The coordinator owns the command-line
dispatch name and CTest registration; this task did not invent or edit those.

Coverage: unsigned/linear mix mapping and semantic text round trips; Output bar
and exact -24..+12 range; inward ring geometry; three themes; Morph widths
190/220/320/480/679/680/840/896/1200 in Stack and inspector mode; plot/viewport and
cell non-overlap; 138 px rotary cells and separated value lanes; scroll reach;
no square deployment; default editor dimensions, 388 px rack and compact Stack
value glyph width. Existing suites remain unchanged; the older Morph suite's
one-row expectation at width >=480 still matches this layout.

Run the focused group, existing Morph/Creative/Utility/slider/Stack modulation,
DryWet UI, hover-wheel/focus and routing regression groups after all owners have
integrated, then the consolidated suite. **All** Windows configure/build/test/
benchmark commands must use `tools/Invoke-HalfCpu.ps1` in dedicated 64-bit
PowerShell, including coordinator validation. No such commands ran here.

## Before / after capture recommendations

Preserve the existing `approval-1.11.0` and readability artifacts as the baseline;
do not overwrite or imply a new baseline was captured before this edit. Compare
matching theme, viewport size, UI scale, base values, modulation and explicit
animation phase. Capture Device, Neon and Spectral at 100/150/200% scale:

- Fresh default Stack: header Mix, Lead / Stack and Output; minimum / midpoint /
  maximum values (especially +12 dB), all FX tabs and footer/scroll boundaries.
- Morph in the actual 904 x 388 rack allocation, plus standalone widths 679 and
  680 and inspector widths 190/220/320; log plot and viewport bounds and knob
  cells to show left/right vs above/below, not merely a screenshot crop.
- Routing Output/Mix compact, expanded and inspector views after the owner
  handoff; include selected, bypassed, Assign and effective-modulation states.
- Circular rotaries at identical values, comparing outer diameter and inward
  stroke thickness; include longest numeric text and multi-line captions.
- Motion 0/3/100, static and active modulation, tab hide/show, frozen/live Morph,
  and popup/tooltip refresh. Use deterministic offscreen before/after frames for
  pixel geometry; report them as offscreen, not second-monitor captures.
- For native captures, first enumerate actual displays, primary flags, work
  areas and DPI. Place only test/plugin windows and their transient peers on the
  available non-primary monitor. Report primary fallback if none exists.

For timing compare the same Release harness before/after integration: visible
and hidden panel counts, cache warmup, Motion phases, snapshot CPU, and timer/
paint counts. Separate offscreen compositing time from native FPS and from DSP
cost. No screenshots, timings, test pass or listening result is asserted by
this source handoff.
