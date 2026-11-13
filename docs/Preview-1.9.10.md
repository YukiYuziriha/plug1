# 1.9.10 — approved and installed

The user approved the preview and explicitly authorized installation. Tracked
`Windows-x64` CLAP and VST3 binaries now match the current 1.9.10 Release build.
The original preview validation and screenshot record is preserved below.

## Targeted rendering changes

- Compact linear-slider base markers are rectangular. Long textured Mix fills
  and markers were already rectangular and remain so, with passive captions.
- Main Stack knob tracks and progress arcs now have explicit butt end caps.
  Compact knob strokes also explicitly request butt caps. Circular arc paths,
  radii, hit areas, value mappings and modulation lanes remain unchanged.
- Main Stack numeric text boxes explicitly use the embedded Pixelify Sans at
  14 px without horizontal shrinking. Previously they inherited JUCE's default
  font despite the editor's pixel look-and-feel. Stack titles use the embedded
  face at 14 px, without synthetic bold. Compact readouts retain their existing
  embedded face and 12.5 px sizing; regression checks verify the actual face.
- Generated background texels blend toward the theme accent by at most 28%,
  rather than 10%. A fixed seed on each grid ensures that coarse Stack card
  grids actually contain a glint: those grids contained no old hash-selected
  seeds. Existing rare hash seeds remain. The smooth envelope now has a roughly
  52-second cycle, rather than roughly 18 seconds; no frame-random sparkle,
  overlay, full-surface accent gradient or new timer was added. Contrast scaling
  on control textures and the card-specific 24-texel grid are preserved.
- No DSP, parameter definitions, state, automation, focus or gesture logic changed.

## Validation observed

All configure/build/test commands ran in dedicated 64-bit PowerShell processes
through `tools/Invoke-HalfCpu.ps1`, reporting 6/12 CPUs and affinity `0x3F`.
The local build helper initializes the x64 Visual Studio toolchain and invokes
that wrapper. Initial build invocations timed out at 200 seconds; incremental
continuations completed successfully. Both CLAP and VST3 preview targets built
under `source/build/VocalStack_artefacts/Release`; automatic installation is disabled.

- All 34 non-editor CTests passed, including legacy DSP, parameter/state and
  effect coverage.
- The final editor-only CTest passed: all 68 editor checks, including square
  marker corners at compact/long widths, embedded main/compact readout fonts,
  deterministic material phases, sparse accent bounds, actual coarse card
  pixels, passive Mix captions, focus, state and control-mode regression checks.
- One concurrent full-suite run had an intermittent existing routing assertion:
  `Canonical Chorale folding edits the real graph and follows host: Host bypass
  left folded node selected`. It passed in the preceding full run, an isolated
  rerun and the final complete editor-only run. No routing workaround was added.
- The first run found a new test-harness issue: reading a JUCE image before its
  Graphics context had flushed. Scoping the context before pixel assertions fixed
  the test; the square marker implementation did not need alteration.
- `git diff --check` passed. No tracked packaged binary was modified.
- No actual DAW, listening, transport or installed-plugin check was performed.

## Actual approval screenshots

All files are local, ignored preview artifacts under:

`C:/Projects/vibe-coded-shit/plug1/source/build/approval-1.9.10/`

Theme mapping: `0=device`, `1=neon`, `2=spectral`.

| Theme | Full Stack | Knobs | Compact sliders | Background |
| --- | --- | --- | --- | --- |
| Device | `device-full-stack.png` | `device-knobs-phases.png` | `device-sliders-phases.png` | `device-background-phases.png` |
| Neon | `neon-full-stack.png` | `neon-knobs-phases.png` | `neon-sliders-phases.png` | `neon-background-phases.png` |
| Spectral | `spectral-full-stack.png` | `spectral-knobs-phases.png` | `spectral-sliders-phases.png` | `spectral-background-phases.png` |

These are actual JUCE component snapshots, not mockups. Full Stack captures show
all of the current editor window; lower voice controls extend into its scrollable
viewport. Phase strips contain unscaled, unenhanced crops, top to bottom at
**0.0, 6.0, 33.3, 46.0 seconds**. They are not equally spaced animation frames.
The later phases deliberately expose rare glints; they are not representative
of their peak intensity at every instant. Full Stack aliases use 33.3 seconds.
Original full snapshots for every phase are named
`approval-1910-stack-{theme}-phase-{phase}.png`. Individual native crops are also
available as `{theme-name}-{knobs|sliders|background}-phase-{phase}.png`.

Additional real control snapshots cover all themes:

- `approval-1910-compact-knob-{theme}.png`
- `approval-1910-slider-{theme}-{110|340}.png`
- `approval-1910-mix-{theme}-{110|340}.png`

The Mix snapshots show rectangular textured progress fills and passive captions
at compact and long widths. Images contain no added highlight overlays, contrast
boost or retouching. The crop assembly script is an ignored local helper at
`source/build/approval-crops-1910.ps1`; source snapshots are generated by the editor
regression tests.

## Approved delivery validation

- Incremental configure/build completed through `tools/Invoke-HalfCpu.ps1`.
  A subsequent explicit build of `VocalStack_CLAP`, `VocalStack_VST3` and
  `vocalstack_clap_smoke` reported `ninja: no work to do`, confirming current
  artifacts without a full rebuild. Generated JUCE metadata and embedded version
  strings in both packaged binaries identify 1.9.10. The VST3 Windows PE
  FileVersion field still reports 1.0.2; it is not the plugin release version.
- The local helper's CTest argument binding failed before launching tests.
  CTest was then invoked through the wrapper with an explicit argument array.
  The complete run passed 34/35 tests, including the editor suite. The only
  failure was `VocalStackGrainSnapshotUi`: `concurrent readers never observe torn
  plain payloads`. Its isolated rerun passed (1/1). No source workaround was made;
  the concurrent failure remains a validation caveat, not a clean full-suite pass.
- Packaged CLAP smoke passed: 2087 automatable parameters, original indices,
  two remote pages, state, latency, bypass, automation and offline layouts
  (stereo 48 kHz, mono-to-stereo 44.1 kHz, mono 96 kHz).
- Installation used the existing ignored `source/build/install-release.ps1`,
  including its exclusive-access checks. Both installed formats were SHA256
  verified against the packages. No Bitwig process was stopped and no locked
  file was forced. Installed CLAP smoke passed the same checks.
- Every build and test invocation used a dedicated 64-bit PowerShell process
  through the wrapper, reporting 6/12 logical CPUs, affinity `0x3F`.
- No actual DAW, listening or transport check was performed.

### SHA256 (build = tracked package = installed)

CLAP: `8B27FAAB4D921F122701972DD4A01045D7FDE0BF8C67A57863F67A73316897C2`

VST3: `F35F03599B9F4C4B5407657AB98D165A2FA8F631EC4B85952F4DF589255CFC2A`

Installed files:

- `C:/Users/tkach/AppData/Local/Programs/Common/CLAP/Vocal Stack.clap`
- `C:/Users/tkach/AppData/Local/Programs/Common/VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3`
