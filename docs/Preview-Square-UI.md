# Square UI — historical source preview

Based on 1.9.10 (`7ae6c68`). This document preserves the original preview passes;
its unreleased/no-install statements below are historical, not current gates.
The complete pending preview was subsequently approved and delivered in 1.10.0.
See [release validation and installation](Release-1.10.0-Validation.md).

## Changes

- Lead card badge displays `0`; voice badges retain `1`–`8`. All are square.
- Voice-motion numbered markers and their selection frames are square; Lead also
  displays `0` there. Left/right axis labels still use L/R as stereo directions.
- Removed decorative corner radii from Device header/control plates, voice and
  Chorale cards, meters/clip badges, modulation frames, assignment outlines,
  creative/Tone visual frames, granular history regions and main-bus FX cards.
- Shared pixel look-and-feel draws square fallback buttons, dropdowns, scrollbar
  thumbs, tooltips and reveal bubbles. Existing custom rectangular panel buttons,
  tabs, dropdowns, linear slider bars/readouts and text-editor outlines remain
  rectangular. Main editor textured buttons and plate clips are now rectangular.
- Routing input sockets retain their taller fan-in shape but have square corners;
  output sockets are square. Connection locations, dimensions and cable paths
  are unchanged.
- No DSP, IDs, parameter definitions, automation/state, focus/gesture handlers,
  embedded fonts or texture generation changed.

Deliberately curved functional geometry remains: rotary faces/tracks/arcs and
rotary modulation markers; EQ/filter graph handles/effective-response markers;
pitch/delay visual event markers and pulses; response/waveform curves; and
routing cables. These are not decorative panel corners or numbered badges.

## Observed validation

All builds, CTests and screenshot runs used a dedicated x64 PowerShell process
through `tools/Invoke-HalfCpu.ps1`: 6/12 logical processors, affinity `0x3F`.

- Release build completed, including CLAP, VST3 and UI test executables. Outputs
  remain in ignored `source/build`; no `Windows-x64` package was replaced.
- All 35 CTests passed (191.56 seconds), including CLAP smoke, DSP, state,
  automation, focus and editor coverage.
- Final editor-only CTest passed (98.77 seconds) after snapshot-harness changes.
  The editor suite now contains 69 checks, including a new all-theme pixel
  comparison of square Lead `0`, Voice `1` and Voice `8` badges and shared
  dropdown corners.
- The first compile exposed an ambiguous integer/float rectangle overload in
  the converted routing input paint call; explicit float dimensions fixed it.
- Initial `--render` failed in the old `rackTabs()` preflight with `Missing FX
  tab`, before writing screenshots. The screenshot-only entry point no longer
  invokes that unrelated preflight. It now resets the horizontal card viewport
  to the start after adding voices, so approval captures actually show Lead `0`.
  No production routing or tab behavior was changed to work around it.
- Final `--render` completed successfully. Images are real JUCE component
  snapshots, not mockups or retouched images.
- `git diff --check` passed. No DAW/listening/installed-plugin test was performed.

## Approval screenshots

Local ignored directory:
`C:/Projects/vibe-coded-shit/plug1/source/build/approval-square-ui/`

These full-editor images inherit the actual plugin look-and-feel:

| Theme | Stack / Lead 0 | Routing + Reverb FX | Routing + Waveshaper FX |
| --- | --- | --- | --- |
| Device | `update-stack-full-device.png` | `update-reverb-device.png` | `update-waveshaper-device.png` |
| Neon | `update-stack-full-neon.png` | `update-reverb-neon.png` | `update-waveshaper-neon.png` |
| Spectral | `update-stack-full-spectral.png` | `update-reverb-spectral.png` | `update-waveshaper-spectral.png` |

Additional actual-editor filter captures are `update-xp-filter-{theme}.png` and
`update-synth-filter-{theme}.png` for `device`, `neon`, and `spectral`.
Stack captures show Lead and the first four generated voices; remaining cards
are reachable by the unchanged horizontal scrolling, and lower controls by the
unchanged vertical scrolling. Meter values are idle, not a listening test.

The render command also emits `routing-*.png` standalone test-fixture snapshots.
Those fixtures do not inherit the editor's pixel look-and-feel and therefore
still show JUCE default rounded buttons. They are not plugin approval images;
use the full-editor `update-*.png` captures above to review this change.

## Background-subtlety follow-up (unreleased)

- `FluidBackground` now interpolates each completed background texel 20% toward
  its surface base: `base + 0.8 * (original - base)` (8-bit JUCE interpolation).
  This includes rare glints, rather than changing their seeds, envelope or motion.
  Shared defaults cover editor/canvas, Stack plates and Routing/FX surfaces.
- Explicit control-material instances preserve the original button and rotary-arc
  rendering. Slider/knob face materials are unchanged. Texel grids, animation
  speed `1.2`, phase quantization, DSP and focus handling are unchanged.
- Deterministic tests check every pixel against the 0.8 reference for all three
  themes, background/panel/inset bases, 24/80 grids and four animation phases;
  button output is checked against the unattenuated control reference.
- Release build succeeded. Full CTest rerun: **35/35 passed**, 219.71 seconds.
  First test invocation timed out after 200 seconds (34 passes observed); the
  complete rerun used a longer timeout. An initial compile rejected an explicit
  constructor in aggregate initialization; using the entry's control-material
  default initializer fixed it. The local helper's CTest argument forwarding
  failed before tests launched; explicit PowerShell array forwarding worked.
- Build, tests and screenshot rendering used `Invoke-HalfCpu.ps1`, 6/12 logical
  processors / affinity `0x3F`. `--render` succeeded for all themes.
- Before/after approval images are in ignored
  `source/build/approval-background-subtle/before/` and `after/`. Before images
  are preserved copies of the prior square-preview captures. Compare matching
  `update-stack-full-{theme}.png`, `update-reverb-{theme}.png`, and
  `update-waveshaper-{theme}.png` for `device`, `neon`, and `spectral`.
  Additional matching filter captures are included. Standalone `routing-*`
  fixtures are not full-editor approval captures, as explained above.
- No installation, tracked package replacement, version bump, commit or push.
  Existing square-preview edits remain intact; visual approval is still pending.

## Voice rails, preset chooser and Routing materials (unreleased)

- Inspected `ParameterKnob::Style::drawLinearSlider`: the existing white pointer
  was **4 × 10 px**, with a **2 px** rail. The ordinary linear rail now matches
  the pointer's **10 px** height, preserving its centre and width/value mapping.
  Rail and filled range use cached `FluidBackground::Material::control` noise.
  The separate **25 px FX Mix** geometry and passive caption are unchanged;
  ordinary voice numeric labels retain their existing editable layout/handlers.
- The actual footer `PresetBar::PassiveBrowser` ComboBox now opts into a cached
  muted inset material. `Editor::applyTheme` updates its text, arrow and border
  along with the toolbar/status colours. The empty selector caption also uses
  its actual theme foreground instead of the global JUCE default.
- Shared pixel look-and-feel paints JUCE popup backgrounds, rows, headers,
  separators, disabled items, submenu indicators and selection/tick marks.
  Theme resolution uses each popup's **originating target component**, since
  popup peers are outside the editor hierarchy. Routing's node, Add and modulator
  menus explicitly use the originating look-and-feel. No global desktop/default
  look-and-feel is changed. Popup textures are cached at a fixed phase, no timer.
- Routing nodes now have per-node cached panel material. Device retains stable
  voice identity colours; Neon/Spectral use their palette secondary for voice
  headers/outputs. Selected borders, enabled/bypassed text, ports, cabling,
  hit-testing, zoom/layout and all graph actions retain their existing semantics.
- New selector/menu/node surfaces use the existing **0.8 background attenuation**;
  controls retain unattenuated control material. No changes to `FluidBackground`
  generation, existing surfaces, DSP, parameters, IDs, state or automation.

### Validation observed for this follow-up

- Final Release build succeeded, including CLAP/VST3 and UI executables.
- Final full CTest run: **35/35 passed**, **259.12 seconds**. Earlier full run also
  passed 35/35 (257.57 seconds). The editor suite remains 69 named tests, with
  all-theme material/entry assertions added to the square-chrome test.
- New regression checks exercise the actual editor's preset selector colours,
  pixel look-and-feel and idle focus; popup texture diversity and square border;
  voice numeric-editor availability/focus policy; exact 10 px rail/pointer
  geometry and readable foreground. Existing slider modulation, gestures,
  host focus, Routing, bypass, state and background-attenuation tests pass.
- Final `--render` succeeded for Device, Neon and Spectral. Live-popup assertions
  verify the originating theme's border and unchanged focus after dismissal.
  Node-menu captures reject a tall Add menu to prevent mislabelling.
- Build, tests and rendering all used dedicated x64 PowerShell processes through
  `tools/Invoke-HalfCpu.ps1`: 6/12 logical processors, affinity `0x3F`. Initial
  shell quoting/PowerShell argument-binding attempts failed before build/tests;
  local ignored build helper and explicit `-CommandArguments` array resolved it.
- Capture inspection caught a deferred graphics flush (black composite) and an
  initial synthetic right-click using unpersisted geometry (Add menu). The final
  harness flushes before saving, persists Fit geometry and briefly positions then
  restores the system pointer for JUCE's mouse-position popup placement. These
  are harness-only corrections; no production geometry was altered for captures.
- `git diff --check` passed. No DAW/listening or installed-plugin test performed.

### Approval captures

Ignored local directory: `source/build/approval-control-materials/`.
Replace `{theme}` below with `device`, `neon`, or `spectral`:

| Capture | Filename |
| --- | --- |
| Actual full Stack, chooser closed | `update-stack-full-{theme}.png` |
| Actual full Stack + expanded preset menu | `stack-presets-expanded-{theme}.png` |
| Preset menu only | `stack-presets-expanded-{theme}-popup-crop.png` |
| Actual Voice 1 card/sliders | `voice-sliders-{theme}-crop.png` |
| Actual Routing + node right-click menu | `routing-context-{theme}.png` |
| Node menu only (Delete / Toggle FX bypass) | `routing-context-{theme}-popup-crop.png` |
| Routing canvas/nodes only | `routing-nodes-{theme}-crop.png` |

Expanded-menu images combine **real JUCE component snapshots** of the editor and
its separate popup peer at their real screen-relative coordinates. They are not
OS desktop screenshots, mock menus or retouched designs; native window shadows
are not represented. The preset list includes locally discovered user filenames.
Routing uses a real Lead → Reverb → Output graph with idle metering, not audio.
Standalone `routing-expanded-*` fixtures also emitted by `--render` remain
non-approval fixtures as described above; use `routing-context-*` for this review.

Native **Folder... / Save... file dialogs remain OS-owned and unchanged**. Their
platform chrome/font/material cannot reliably be overridden with JUCE painting;
this change themes the actual in-plugin preset selector/menu, not OS dialogs.
No installation, tracked `Windows-x64` replacement, version bump, commit or push.
Installed and packaged 1.9.10 remain untouched; all prior uncommitted square and
subtler-background work is preserved.

## Coherent clustered-material audit (unreleased)

This follow-up preserves the preceding uncommitted preview; it is not an
installation or release. All writes were confined to `plug1`. Installed and
tracked packaged 1.9.10 are untouched. No version bump, commit or push.

### Material

- `FluidBackground.h` now combines broad low-frequency structure (55%), stepped
  mineral-like cells (30%) and slow strata (15%), quantized to five shade levels.
  Independent fine grain is removed. This is original procedural artwork inspired
  by block-material construction, not copied Minecraft textures or assets.
- Narrower brightness endpoints (`0.78 * base` to `1.22 * base + .012`) keep the
  palette restrained. Background attenuation remains exactly **0.8**. Existing
  nearest-neighbour grids/physical chunky texel sizes remain; adjacent texels now
  intentionally form larger patches rather than all changing independently.
- Rare, fixed-seed accent glints have a reduced `.12` blend (previously `.28`),
  with continuous slow envelopes, not frame-wise sparkle. `1.2` animation speed
  and inherited deterministic animation phases remain. No timers were added.
- Thick FX bars now use the same cached material rather than a second grain
  generator, retaining their 128x26 tile / two-pixel texels. Ordinary voice rails
  remain **10 px**, FX Mix remains **25 px**, with its passive caption unchanged.
- Surface images cache dimensions, phase and palette. Shared LAF control caches
  use safe component pointers and discard dead entries. Static popup/tooltip
  material does not start independent animation. Controls use reduced contrast;
  plot backplates are especially restrained (`.25` before `.8` attenuation).

### Audited custom UI / look-and-feel coverage

| Surface | Outcome |
| --- | --- |
| Root/canvas, main control plates, Routing/FX cards and nodes | Shared clustered material replaces previous noise |
| Main headers/surrounds, Voice/Lead and Chorale card backgrounds | Added restrained cached material; square Lead 0 and voice colours retained |
| Toolbar, Stack/Routing tabs, bypass, FX tabs/handles and panel buttons | Existing custom button LAFs retain state treatments and inherit new material; fallback button surface now textured too |
| Preset chooser, routing/inspector/modulation dropdowns, including empty/inactive states | Existing preset surface retained; general and private PanelStyle dropdowns now use cached material; empty text uses component foreground |
| Actual preset/context menus, headers, disabled rows, separators, submenu/tick marks | Existing originating-target theme resolution retained; new material underneath crisp row text/marks; discrete modulation menu now explicitly uses originating LAF |
| ToggleButton fallback | Square textured field with flat readable state mark; production TextButton toggles retain their custom styles |
| Editable TextEditor fields | Cached subtle background, respecting transparent numeric editors and existing entry/focus handlers; editor defaults explicitly themed |
| Scrollbars | Textured trough and thumb; preserve translucent thumb alpha and hover treatment |
| Tooltips/reveal bubbles | Cached subtle surface; explicit editor tooltip palette; bubble tail and outlines retain functional shape |
| Rotary faces/arcs; normal and thick linear rails | Shared material; existing geometry/value mapping retained; Device main-face gradient preserved under a faint material layer |
| Meter, EQ, creative/Tone monitor and voice-motion backplates | Added very subtle material underneath existing plots/readouts |

**Intentionally untextured:** glyphs, numeric readouts, plot/response curves,
waveforms, EQ handles, routing sockets/cables, selection/bypass indicators,
slider pointers/zero ticks, voice number badges, meter fills/peak lines and small
functional marks. Transparent labels/numeric fields inherit the textured parent
rather than acquiring opaque boxes. Selected menu rows use translucent theme
highlight over the textured menu rather than an independent busy texture.

**Native exclusions:** Folder/Save file-dialog chrome, native detached Routing
window title bar, OS shadows, host-owned parameter/automation menus and host
window chrome. No global desktop LAF is installed.

**Honest limits:** this is a source/LAF audit plus idle component-render review,
not an exhaustive screenshot of every hover/disabled combination or every FX
algorithm at every size. Error/confirmation AlertWindows, file-dialog content,
and OS accessibility/high-contrast behaviour were not visually exercised. The
small bubble pointer retains its flat base fill. Standalone Routing fixtures
without editor LAF inheritance remain non-approval fixtures. No DAW listening,
installed-plugin or live-audio visual check was performed.

### Validation and capture provenance

- Every compiler, test and renderer ran through `tools/Invoke-HalfCpu.ps1` in a
  dedicated x64 PowerShell process: **6/12 logical processors, affinity 0x3F**.
- Final Release build passed, including CLAP, VST3 and UI executables in ignored
  `source/build`. Final full CTest: **35/35 passed, 170.44 s**; editor suite
  **69 checks**, 97.66 s. DSP/state/automation/host-focus coverage is unchanged.
- New deterministic all-theme checks measure adjacent-vs-distant differences
  (`adjacent < .35 * distant`), contiguous quantized patches (>65% identical
  horizontal neighbours), muted average channel deviation (<7) and subtler
  control contrast, at three phases. Existing `.8` pixel-reference attenuation,
  grid scaling, time determinism/motion, theme tint and real UI checks also pass.
- Old tiny-strip assumptions were updated because coherent patches intentionally
  contain flat regions; actual card pixels still match the coarse material
  exactly, and grid diversity is checked over the whole reference. Dropdown
  square-corner tests check opacity rather than obsolete exact flat-red fill.
- Earlier full runs: 34/35 with two obsolete rendering assertions; subsequently
  34/35 with intermittent `Canonical Chorale folding ... Host bypass left folded
  node selected`. Immediate editor-only rerun passed 69/69; final full run passed.
  No graph/host-bypass semantics were changed to mask this result.
- One compile caught a const paint helper requiring a mutable image cache; fixed.
  Initial shell/wrapper argument attempts failed before compilation; corrected
  calls used the existing x64 toolchain helper. Python crops could not run due
  to missing Pillow; no dependency was installed. System.Drawing crop helper
  succeeded through the wrapper instead.
- Both **before** (existing preview executable, before edits) and **after**
  (final rebuilt executable) `--render` runs passed for all themes, including
  actual preset popup and node context menu assertions. Images are real JUCE
  component snapshots, not mockups; separate popup peers are composited at their
  real screen-relative coordinates by the existing harness. Native shadows are
  not captured. Crops do not alter pixels; `-2x` uses nearest-neighbour only.

### Current approval paths

Absolute root:
`C:/Projects/vibe-coded-shit/plug1/source/build/approval-clustered-material/`

Both `before/` and `after/` contain each filename below, with `{theme}` equal to
`device`, `neon` or `spectral`:

| View | Filename |
| --- | --- |
| Full Stack / Lead 0 | `update-stack-full-{theme}.png` |
| Full Routing + Reverb FX | `update-reverb-{theme}.png` |
| Full Routing + Waveshaper FX | `update-waveshaper-{theme}.png` |
| Other full FX views | `update-xp-filter-{theme}.png`, `update-synth-filter-{theme}.png` |
| Close material crop, actual main plate | `material-close-{theme}.png` |
| Same crop enlarged 2x, nearest-neighbour | `material-close-{theme}-2x.png` |
| Stack + actual open preset menu | `stack-presets-expanded-{theme}.png` |
| Actual preset menu crop | `stack-presets-expanded-{theme}-popup-crop.png` |
| Routing + actual node menu | `routing-context-{theme}.png` |
| Actual node menu crop | `routing-context-{theme}-popup-crop.png` |
| Voice rail and Routing-node close views | `voice-sliders-{theme}-crop.png`, `routing-nodes-{theme}-crop.png` |

Stack images show Lead and first four voices with unchanged scrolling for the
remaining cards. These paths supersede prior approval paths for this follow-up.

## Approval resolved

The entire pending preview was approved for 1.10.0 delivery. Tracked packages and
both per-user installations are now 1.10.0 with independently verified hashes;
see [the release record](Release-1.10.0-Validation.md). Earlier preview gates and
version statements above remain historical observations only.
