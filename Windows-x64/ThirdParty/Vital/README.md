# Vital filter subset and scalar integration

Upstream: https://github.com/mtytel/vital

**Pinned commit:** `636ca0ef517a4db087a6a08a6a8a5e704e21f836`

Raw source base:
`https://raw.githubusercontent.com/mtytel/vital/636ca0ef517a4db087a6a08a6a8a5e704e21f836/`

Copyright 2013–2019 Matt Tytel (individual retained files carry their notices).
**License: GPL-3.0-or-later.** See `LICENSE` (unmodified upstream license).
`UPSTREAM_README.md` is the unmodified upstream README. `SHA256SUMS` records the
SHA-256 of every unmodified upstream file retained here. The original synthesis
engine, UI, presets, assets, authentication, JUCE, and platform frameworks are
NOT imported. `src/` is reference source, not an additional build target.

## What is actually integrated

`../../source/src/VitalFilterDSP.h` is **modified, GPL-covered Vital code**, not a
renamed independent/category-inspired filter. It adapts selected `setupFilter`
and sample `tick`/`process` equations from:

- `digital_svf.{h,cpp}`: TPT state, hardTanh, cubed resonance mapping, drive
  compensation, post gain, 12/24 dB paths, notch-pass-swap style.
- `ladder_filter.{h,cpp}`: tuned state-feedback combination, input rational tanh,
  four algebraic-saturated one-poles, 24 dB LP/HP stage weights and mappings.
- `dirty_filter.{h,cpp}`: two linear input poles, nonlinear feedback loop, tuned
  resonance, quickTanh state saturation, drive/normalization, 12 dB LP path.
- `diode_filter.{h,cpp}`: diode-stage cross feedback, feedback high-pass, distinct
  stage saturation functions and cubic resonance mapping.
- `sallen_key_filter.{h,cpp}`: Vital's Analog family, tuned feedback solve,
  rational input tanh, 12 dB LP/HP outputs and compensation.
- `comb_filter.{h,cpp}`, `lookups/memory.h`, `framework/poly_utils.h`: low-pass
  comb feedback, input scale, two loop-filter poles, hardTanh memory write and
  four-point Catmull interpolation. **Not the previous linear-interpolation comb.**
- `formant_filter.{h,cpp}`, `formant_manager.{h,cpp}`: four parallel DigitalSvf
  bandpasses, original A/I/U/O vowel table, bilinear interpolation, gain table,
  formant-manager Q bounds 4..30, resonance multiplication and transposition.
- `one_pole_filter.h`, `synth_filter.{h,cpp}`, `framework/futils.h`: original
  state ordering, model coefficient function, drive percentage conversion,
  rational tanh/hardTanh/quickTanh/algebraicSat equations.

XP is still separately and explicitly an original XP-inspired design. There is
no proprietary Bitwig source or assertion of Bitwig fidelity.

## Host mapping (21 parameter IDs unchanged)

`synth_mode` range is now **0..12**, preserving the original family/pass choices:

| Index | Vital configuration | Upstream style / blend |
|---|---|---|
| 0 | Digital LP12 | k12Db / 0 |
| 1 | Digital HP12 | k12Db / 2 |
| 2 | Digital BP12 | k12Db / 1 |
| 3 | Digital Notch24 | kNotchPassSwap / 1 |
| 4 | Ladder LP24 | k24Db / 0 |
| 5 | Comb | kComb + kLowHighBlend / 0 |
| 6 | Formant A/I/U/O | formant style 0, spread 0 |
| 7 | Dirty LP12 | k12Db / 0 |
| 8 | Diode LP24 | k12Db / 0 (the diode topology itself has four stages) |
| 9 | Analog/Sallen–Key LP12 | k12Db / 0 |
| 10 | Digital LP24 | k24Db / 0 |
| 11 | Ladder HP24 | k24Db / 2 |
| 12 | Analog/Sallen–Key HP12 | k12Db / 2 |

- `synth_cutoff`: Hz, capped at 0.45 * sample rate. For formant it transposes the
  whole vowel table by `12*log2(cutoff/1000)` semitones (1000 Hz = no transpose).
- `synth_resonance`: 0..100 maps to Vital resonance_percent 0..1. Each model then
  uses its own upstream mapping; it is NOT a shared homemade Q law. In comb,
  feedback uses the separate `synth_feedback` control instead.
- `synth_drive`: 0..24 dB -> drive amplitude through Vital's original polynomial
  `futils::exp2(dB/6.02059991329)` (approximately `10^(dB/20)`); drive_percent is
  **dB/36**, as in upstream FilterState::loadSettings, not dB/24. Formant upstream
  ignores filter drive; this adapter explicitly exposes drive as a pre-bank
  input trim. Comb upstream has no drive control; this control is inactive there.
- `synth_feedback`: -95..95 in comb maps to upstream resonance_percent
  `(feedback/100+1)/2`, then upstream's signed square-root feedback law. The
  descriptor percentage is the pre-law value, not the final loop coefficient.
  In formant it maps -95..95 to vowel X 0..1, with vowel Y fixed at 0.5. It is
  inactive in other families. Label is `Comb feedback / vowel X`.

## Explicit modifications / fidelity limits

1. Header-only C++17, independent mono scalar state per stereo channel; double
   arithmetic replaces SIMD/polyphonic float lanes. Upstream float constants
   and float coefficient calculations are retained where numerically important.
2. Exact evaluation of the upstream coefficient-generating functions replaces
   the 2048-entry interpolated LUT, avoiding global initialization and framework
   dependencies. As of Vocal Stack 1.7.1, unchanged coefficient frames are cached;
   formant cutoff sweeps reuse unchanged vowel/Q/gain results while cutoff
   coefficients still update every sample. The comb smoothing coefficient is
   prepared once. These caches retain the original calculations, not a new LUT or
   reduced control rate; tests compare cached/uncached output sample-for-sample.
   This is not bit-exact LUT/SIMD reproduction. Standard-library
   MIDI/pitch math replaces SIMD lane mapping; the upstream polynomial drive
   conversion and all audible model saturation functions are retained.
3. Only the styles listed above are exposed; this is not every Vital filter
   variant or a complete Vital synthesizer/preset reproduction.
4. The surrounding UtilityEffect owns finite parameter validation, 10 ms
   continuous-control smoothing, mode-state resets/5 ms transition bridging,
   stereo independence, and the 8–384 kHz sample-rate contract. Comb read delay
   has an additional 30 ms smoother. Upstream block-ramp/polyphonic reset logic
   is replaced by these standalone sample controls.
5. No oversampling/decimation engine is imported. At drive/resonance extremes,
   aliasing and self-oscillation are possible, just as nonlinear equations allow;
   this does not claim full-plugin Vital fidelity. There is zero added explicit
   host latency. Upstream comb memory offset/read/write timing is retained,
   including its one-sample offset relative to period when read before push.
6. Delay storage is a bounded ~50 ms vector per channel, allocated only during
   prepare; interpolation is upstream Catmull, using modulo indexing instead of
   duplicated power-of-two memory. All filter states reset on explicit reset or
   topology change. This also clears the diode feedback high-pass state (which
   upstream DiodeFilter::reset omits). Tiny states flush; exceptional nonfinite
   results reset the affected channel, without adding a normal-signal limiter.
7. The diode input HP path has zero amount in the exposed style/blend, so its
   inaudible states are omitted. The actual feedback HP and all audible diode
   stage equations are retained.

## Response plotting: changed contract for synth filters

`UtilityEffect::synthResponseCurve(sr, settings, fftSize=32768)` renders a `1e-5`
impulse through the **actual adapted sample equations**, then computes a radix-2
FFT. Result has N/2+1 linear-magnitude bins at `bin*sr/N`; requested FFT size is
rounded up to a power of two in 512..262144. All entries are marked approximate:
finite tail truncation, nonlinear operating point, self-oscillation and moving
comb delays preclude a universal exact LTI transfer function.

**Allocating, UI/worker-only, O(N log N), potentially expensive.** Cache a curve
per settings/sample-rate change and interpolate its bins. Do not call from the
audio callback, per pixel, or synchronously every repaint. Higher FFT sizes are
needed at low cutoffs/high Q/high rates to avoid truncating long tails.

The existing single-frequency `response()` convenience method delegates to a
whole rendered curve for synth mode and therefore also allocates/is expensive
(and is no longer noexcept). XP/gain/stereo/gate response queries remain cheap.
Old closed-form synth response equations have been removed entirely.

## Independent upstream reference tests

`reference/generate.py` produces `reference/Upstream.h` from the retained original
class declarations and .cpp bodies. It removes includes, renames the namespace,
and adds inline qualifiers; it does **not** replace the setup/process/tick bodies
with the production adaptation. Generated code is retained for standalone builds.

`reference/Framework.h` supplies a test-only single-lane poly_float wrapper,
minimal Processor I/O, math functions, exact analytic coefficient lookup and
Catmull memory shim. That reference uses the original filter class implementations,
including original constants and setup mappings. Its scalar-double SIMD shim,
exact coefficient evaluation and memory replacement are explicit reference
limitations: these are **not tests against the complete SIMD Vital plugin**.

UtilityFiltersTests compares adaptation vs those upstream class bodies for all
13 configurations at 8/48/192 kHz, multiple cutoff/resonance/drive settings and
small/large mixed-tone inputs. Tolerance is 0.3% relative RMS or 1e-5 peak absolute
error, allowing float table/constant precision differences. Four-formant expected
output uses upstream tables/interpolateFormants and four upstream DigitalSvf
classes. Comb test spans enough samples for low-frequency echoes. Separate DFT
checks compare the public response curve to actual UtilityEffect impulse output.
Realtime tests cover rates to 384 kHz, mode/continuous automation, stereo, reset,
nonfinite input/settings, and intercept allocations/frees during process/reset.
The expensive synth response renderer is intentionally outside realtime checks.

Regenerate (no network needed):

```text
python ThirdParty/Vital/reference/generate.py
```

Build/run standalone from plug1, in an MSVC developer prompt:

```bat
cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /permissive- source/tests/UtilityFiltersTests.cpp /Fo:"%TEMP%\UtilityFiltersTests.obj" /Fe:"%TEMP%\UtilityFiltersTests.exe"
"%TEMP%\UtilityFiltersTests.exe"
```

**Production build changes required: none.** Include `UtilityFilters.h` normally;
`VitalFilterDSP.h` is header-only. No .cpp additions, include paths, dependencies,
SIMD/architecture flags or GPL engine libraries are needed. C++17 is required.
No CMake/build configuration was edited by this integration.
