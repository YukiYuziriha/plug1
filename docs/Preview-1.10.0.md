# 1.10.0 — Historical Convolution and Saturator preview

**Delivery update:** the user approved the entire pending preview for installation
and delivery. Both tracked formats and per-user installations are now 1.10.0;
see [release validation, caveats and hashes](Release-1.10.0-Validation.md).
The preview observations and no-install gates below describe their historical
passes only; they are superseded by that approval and successful installation.
The existing uncommitted square / Lead 0 / 0.8-background / clustered mineral
material / textured menus and nodes / 10 px voice-rail preview is retained.
Its historical record remains in `Preview-Square-UI.md`.

At the original preview checkpoint, CMake source/build was 1.10.0 while installed
and tracked packages were **1.9.10**. Their historical read-only SHA256 hashes were:

- CLAP: `8B27FAAB4D921F122701972DD4A01045D7FDE0BF8C67A57863F67A73316897C2`
- VST3: `F35F03599B9F4C4B5407657AB98D165A2FA8F631EC4B85952F4DF589255CFC2A`

## Convolution

A separate Routing / Main Bus FX effect, not a renamed algorithmic reverb.
`FactoryConvolution.h` performs actual stereo FIR convolution, verified against
both the original impulse samples and a direct arbitrary-input FIR oracle.

| Factory choice | Finite IR length | Original synthetic construction |
| --- | ---: | --- |
| Church | 3.20 s | Later onset, slower density buildup, darker diffuse tail, widely spaced early reflections |
| Hall | 2.10 s | Faster buildup, brighter tail, intermediate reflection spacing |
| Room | 0.65 s | Early onset, short bright response, closely spaced reflections |
| Plate | 1.50 s | Immediate dense response with a decaying modal component and fewer discrete early reflections |

These are **original procedural stereo IRs**, not measured venues, physical
plate recordings, licensed IR downloads or copied assets. Names describe
synthetic space characters. The selector, host choice strings and overview say
synthetic explicitly. Different geometry, density, filtering, modal content and
random seeds are used; pairwise response correlation tests guard distinctness.
No assertion of authentic venue modelling or a listening evaluation is made.
Each channel's factory response is energy-normalized to 0.7; this is not peak
normalization or a limiter. Mono excitation produces a decorrelated stereo wet
response in stereo layouts; the two FIR channels have no crossfeed matrix.

Controls:

- **Synthetic space:** Church / Hall / Room / Plate. All four kernels are prepared
  before processing; selection blends their prepared spectra with smoothed
  weights. Late partitions use a coherent weight snapshot for their scheduled
  overlap-add block, so the late response changes more slowly than early energy.
- **Brightness:** -12..+12 dB wet-only complementary low/high tilt around 1 kHz.
  Opposite band gains are +/- half the indicated tilt. Actual smoothed EQ, not
  feedback damping and not just a relabelled low-pass cutoff.
- **PreDelay:** 0..250 ms, fractional interpolated read with smoothed time.
- **Tail:** 25..100%, additional exponential decay taper. 100% retains the original
  finite IR. Taper gains are evaluated at FFT partition centres; this is not
  time stretching, an exact RT60 control, or a way to extend the finite IR.
  The direct first 64 taps are not tapered.
- **Duck:** 0..30 dB wet attenuation, stereo-linked peak follower; fixed 5 ms attack
  and 300 ms release. No detector/release placeholder controls are exposed.
- **Output:** -24..+12 dB manual wet trim, before the dry/wet blend.
- **Mix:** existing slot's smoothed true dry/wet blend; default 25% when added.

The plot shows normalized **48 kHz left-channel peak-envelope bins** derived
from the actual original factory IR, plus an illustrative Tail taper. It does
not pretend to show live audio, the current-rate exact waveform, a measured room,
or the effects of wet tilt, pre-delay and ducking.

### Processing/lifecycle constraints

- JUCE-independent DSP. Direct 64-tap head plus non-uniform partitions:
  64-sample partitions from tap 64, 512 from 1024, 4096 from 8192,
  and 16384 from 32768 to the finite IR end.
- Delayed partitions have sufficient pre-output slack to distribute MAC and
  long-transform stages over processing ticks. Preparation builds all spectra,
  FFT tables and storage on the serialized control/preparation caller, never in
  `processBlock`. This is **synchronous off-audio preparation**, not a background
  file-loading worker. Measured preparation was about 37 ms at 48 kHz and
  294–306 ms at 384 kHz on this machine.
- Zero added **integer algorithmic latency**: graph-parallel and local dry paths
  need no new delay compensation. IR onset and user PreDelay are musical wet
  timing, not host latency. Existing whole-plugin latency remains unchanged.
- IR selection, automation, reset and bypass do not allocate, free, lock or do
  I/O. Tests instrument audio allocations and deallocations. The existing graph
  handoff installs prepared machines; retired graph destruction remains on the
  control caller. No shared-pointer replacement/destruction is introduced in
  sample processing.
- Graph/type/slot lifecycle uses existing stable node IDs and machine reuse.
  Serial and parallel paths, arbitrary sample-block subdivision, mono/stereo,
  and existing crossover lineage remain supported. Existing 48-node / 16-FX
  graph bounds and 15 independent automatable FX slots are unchanged; no new
  pool or index-by-effect-type assumption was added.
- Memory and work are finite but increase with sample rate and instance count.
  No claim that 15 simultaneous convolvers, especially at high rates, will fit
  every host's real-time CPU/memory budget.

**Deliberately omitted:** external/user IR file loading, drag/drop, file browser,
external-file state, trimming, normalization options and arbitrary-length IRs.
Factory state is portable and self-contained: a stable selector plus parameters;
IRs regenerate deterministically for the processing sample rate, without paths.
There is no half-implemented missing-file recovery workflow.

## Saturator

A separate focused effect using the existing `Waveshaper` DSP, without changing
`Waveshaper.h` or the Delay/Reverb feedback saturation functions.

- **Drive:** 0..36 dB.
- **Character:** 0..100%, continuous amount of the existing Warm Asymmetric curve.
- **Tone:** -12..+12 dB smoothed wet tilt around 1 kHz.
- **Output:** -24..+12 dB manual wet trim; default -6 dB with 6 dB Drive.
- **Mix:** existing true dry/wet blend, default 100% when added.
- **Level match:** optional existing bounded, smoothed RMS audition aid. Off by
  default. Estimate is before Tone and manual Output; it does not match LUFS,
  guarantee equal peaks or protect against clipping.

Fixed **2x IIR oversampling + first-order ADAA**, including the existing DC
blocker for the asymmetric curve. No misleading alias-free label or duplicate
Raw/Quality/Shape panel. Existing Waveshaper's integer latency is zero, but its
ADAA/IIR processing has frequency-dependent phase/attenuation: dry/wet and
parallel branches can comb. The static transfer plot deliberately excludes
Tone, Output, RMS matching, smoothing, resampling and Mix.

## Host/state/UI integration

- Append-only NodeType values: Convolution **20**, Saturator **21**; Splitter stays
  19 and all earlier types retain their values. Explicit CreativeKind mapping is
  used for the non-contiguous new node types.
- All **2087** original host IDs and indices remain fixed. Eleven new fields per
  independent FX slot append at **2087..2251**, giving **2252** parameters.
  Original Creative 48-field blocks, three expanded Reverb fields per slot and
  Utility fields remain at their historical indices. New controls use explicit
  index/ID helpers, not effect-type arithmetic.
- Missing legacy fields get descriptor defaults; graph-only state can seed new
  fields from its nested node values. New migration ranges cannot accidentally
  interpret appended controls as Utility fields.
- Existing slot On and Mix are reused; there is no duplicate new Mix parameter.
  Existing automation/modulation capacity accommodates the new last-slot IDs.
- Both Add menus, Main Bus FX tabs, Routing inspectors, expanded cards, graph
  validation, state codecs, reset and factory patches understand both effects.
- Two complete factory patches append without changing the first eight:
  **Synthetic Church** and **Warm Plate**.
- The space selector has named choices, normal host gestures/history scope,
  discrete-modulation assignment support, disabled wheel editing, passive host
  keyboard focus and themed textured popups. A recreated ComboBox label also
  restores the passive focus policy. Legacy compact switches keep their original
  58 px bound; only the new Level Match switch is wider.

## Validation observed

All configure/build/compiler/test/benchmark/render commands ran in dedicated x64
PowerShell processes through `tools/Invoke-HalfCpu.ps1`: **6/12 logical CPUs,
affinity 0x3F**. Initial long builds timed out and were resumed incrementally;
the completed Release build includes CLAP, VST3 and test executables. Automatic
installation is disabled. An initial CTest shell argument-binding error occurred
before tests launched; the corrected call passed an explicit argument array.

- New standalone `VocalStackFactoryFx` passes: full-response FIR agreement for all
  four spaces at **8, 44.1, 48, 96, 192 and 384 kHz**, exact zero-delay alignment,
  arbitrary-input direct FIR oracle, stereo channel isolation, distinct response
  correlation, tail decay taper, pre-delay alignment, wet output gain, duck and
  brightness action. Silence/reset, repeated determinism, irregular subdivisions,
  NaN/Inf automation/input sanitization, bounded finite output, smooth switch
  excursions, serial/parallel routing and exact local bypass also pass.
- Saturator is sample-compared to the actual existing 2x ADAA warm curve at three
  rates and three Character values, including DC removal checks. Existing
  Waveshaper expansion / quality and legacy creative DSP suites pass.
- Audio-thread allocation and destruction counters remain **zero**, including
  automated factory selection and graph installation/retirement handoff.
- New processor/UI coverage checks append-only indices, all-slot settings,
  binary/text state, graph-only migration, reset, final-slot modulation without
  host-base writes, real processor mono/stereo renders at 44.1/48/96 kHz,
  all-theme layouts at 220/400/760 px and passive focus. Whole-plugin host bypass
  and actual new main-bus tabs are covered by the final editor validation.
- Full parallel CTest: **37/38 passed** in the last run (167.40 s). Only failure:
  previously documented intermittent `Canonical Chorale folding ... Host bypass
  left folded node selected`. All 37 other tests passed, including legacy DSP,
  pinned baseline, CLAP smoke, state/modulation, rack and all ten factory audio
  tests. No routing workaround was added to hide that unrelated assertion.
- Final Release rebuild passed after the last source review. To avoid the known
  load-sensitive editor assertion without changing its implementation or test,
  the final validation ran the suites separately: **37/37 non-editor CTests
  passed (154.78 s)**, then the **editor-only CTest passed (80.16 s)** with all
  **70/70** checks, including the final explicit whole-plugin host-bypass and
  real main-bus tab assertions. Thus every current CTest suite passed in the
  final split runs; this is not a claim that the full concurrent run was clean.
- New factory audio tests cover 12 factory/rate/layout cases, each rendered twice:
  deterministic repeats, finite unclipped output and decaying tails. This is
  synthetic test audio, **not a listening or actual DAW check**.
- `git diff --check` passed. Source review restricted exact Mix endpoint snapping
  to the two new effects, leaving legacy Creative routing blends unchanged.
  `Waveshaper.h` and all tracked `Windows-x64` files have no diff. The final
  dedicated new-FX renderer passed for all three themes; the existing broad
  Stack/FX/popup/expanded-routing renderer also passed in the new approval folder.

### Informational CPU benchmark

Raw local output: `source/build/approval-1.10.0/factory-fx-benchmark.csv`.
Each row measures two seconds of one stereo DSP instance with 64-sample timing
windows; input is generated before timing. Stable and rapid selector/Character
changes are separate cases. Wall/audio percentages are **not Task Manager CPU**,
not host callback measurements and not a cross-machine guarantee. Timed windows
include timer/loop overhead and possible OS scheduling pauses.

| Rate | Convolution stable / rapid | Saturator stable / rapid |
| --- | ---: | ---: |
| 44.1 kHz | 2.65% / 3.76% | 2.32% / 2.41% |
| 48 kHz | 2.74% / 4.22% | 3.40% / 2.63% |
| 96 kHz | 5.98% / 8.44% | 5.06% / 5.20% |
| 192 kHz | 14.72% / 19.53% | 10.39% / 11.15% |
| 384 kHz | 33.38% / 49.84% | 20.92% / 22.27% |

At 48 kHz, Convolution's stable/rapid 99th-percentile 64-sample windows were
238/343 microseconds; maxima 517/698 microseconds. At 384 kHz, rapid Convolution
was 481 microseconds p99 and 1063 microseconds maximum, exceeding a 64-sample
166.7-microsecond deadline. **High-rate small-buffer real-time operation is not
validated and must not be represented as safe.** Use conservative rates/buffer
sizes/instance counts and measure in the intended host; correctness tests at
384 kHz do not establish real-time suitability. No actual DAW CPU measurement
or maximum-slot stress guarantee is claimed.

The first implementation failed the performance review: monolithic long-block
MACs produced millisecond spikes and excessive high-rate cost. The final
implementation distributes long-tier work, uses SIMD complex MACs on x64 and
skips zero/unneeded factory partitions. The oracle was rerun after this change.

## Actual approval screenshots

Absolute folder:
`C:/Projects/vibe-coded-shit/plug1/source/build/approval-1.10.0/`

These are actual JUCE component snapshots, not mockups. Each `*-controls.png` is
an **840x460 detached test fixture using the actual production panel and editor
look-and-feel**, not a new floating plugin window. Each matching `*-routing.png`
is the full actual editor with the Routing inspector. Material time is frozen at
33.3 seconds for consistent inspection; no image enhancement/retouching is used.

| Theme | Church Convolution | Plate Convolution | Saturator |
| --- | --- | --- | --- |
| Device | `convolution-church-device-controls.png` | `convolution-plate-device-controls.png` | `saturator-device-controls.png` |
| Neon | `convolution-church-neon-controls.png` | `convolution-plate-neon-controls.png` | `saturator-neon-controls.png` |
| Spectral | `convolution-church-spectral-controls.png` | `convolution-plate-spectral-controls.png` | `saturator-spectral-controls.png` |

Full editor alternatives replace `-controls.png` with `-routing.png`.
Actual open IR menu composites are `convolution-church-{theme}-chooser.png`,
with unscaled popup crops `convolution-church-{theme}-chooser-popup-crop.png`.
The separate JUCE popup peer is composited at its actual position; native window
shadows/chrome are not captured. Additional existing-UI captures in this same
folder include `update-stack-full-{theme}.png`, `voice-sliders-{theme}-crop.png`,
`routing-nodes-{theme}-crop.png`, actual preset/context menus and expanded Routing
fixtures. These exercise the preserved UI preview, not a replacement design. All nine controls views and three popup crops
were visually inspected. An initially truncated Brightness caption was widened
without changing existing control geometry; final captures show the full label.

## Fat FX Mix material refinement — historical pass (superseded below)

The thick textured FX Mix bars alone now use **3x3 physical texels at 1x UI
scale**, previously **2x2**: exactly **1.5x larger per axis**. The same normalized
64x13 clustered field is drawn into a **192x39** tile instead of 128x26. The
visible bar stays **25 px high**; the larger tile is clipped, not used to enlarge
any control. Origin remains fixed at the track's top-left; Mix changes only the
fill clip, never the material dimensions, seed or phase.

A separate `FluidBackground::Material::fatBar` lifts only the quantized upper
mineral tones (blend >= 0.75), before the existing 0.35 control contrast. The
lower tones and median remain unchanged. Rare integrated glint seeds/envelopes,
10 Hz phase quantization and **1.2 animation speed** are retained. This is not a
change to high numerical Mix values, text, marker, geometry, backgrounds,
10 px voice sliders, menus, buttons or knobs. Existing convolution/saturator DSP
and earlier UI edits are preserved.

Validation for this refinement:

- Release rebuild completed, including local CLAP/VST3 and editor-test artifacts;
  no installation or tracked package replacement. Final **70/70 editor tests
  passed**, including the existing new-FX state/processor/rack/layout/focus tests.
- New deterministic tests compare exact old 2x2 and new 3x3 texels in all themes
  at 0/6/33.3/46 seconds, check increased upper-quarter tone visibility with the
  original palette dominant, unchanged median, a bounded nonnegative RGB lift,
  actual production tiling at a fixed origin, and unchanged overlapping material
  pixels when Mix changes. Shared background/control pixel fingerprints pin
  their palettes, quantization, animation phase and rare glints independently.
- Actual before/after render comparison: **zero changed pixels outside the fat
  bar in all nine 840x460 new-FX controls snapshots**. Full Stack snapshots also
  change only within their existing Mix bar. **27 snapshots are pixel-identical**,
  including all-theme voice-slider crops, popup crops and routing-node crops.
- All builds/tests/renders/crop composition ran through `Invoke-HalfCpu.ps1` in
  dedicated x64 PowerShell: **6/12 CPUs, affinity 0x3F**. No large DSP-suite rerun
  was needed for this material-only change. `git diff --check` passed.
- Installed and tracked 1.9.10 CLAP/VST3 SHA256 hashes still match those above.
  No commit, push, install or package refresh; screenshot approval remains open.

Actual screenshots: `source/build/approval-fat-texels/` (ignored local artifacts).
All comparisons are **before on the left, after on the right**, without scaling
or image enhancement; phase is frozen at 33.3 seconds.

- `fat-bars-before-after.png`: **950x594** sheet; nine pairs of **474x25** actual
  bar crops, Church / Plate / Saturator in Device / Neon / Spectral.
- `full-controls-{device,neon,spectral}-before-after.png`: **1680x1440** sheets,
  each containing three pairs of unscaled **840x460** production-panel fixtures.
- `before/` and `after/`: individual `convolution-church-{theme}-controls.png`,
  `convolution-plate-{theme}-controls.png`, `saturator-{theme}-controls.png`,
  corresponding `-fat-bar-crop.png` files and full actual `-routing.png` editors.
- `pixel-comparison.txt`: changed bounds and the full pixel-identical file list;
  `compare.ps1`: repeatable local crop/comparison script using System.Drawing.

## Coordinated Motion / material / reactive preview

### Presentation and storage

- The header has a square fat **Motion** slider between the name and Stack/Routing.
  Range **0..100%, step 0.01%, default/double-click 3%**, using the existing relative
  half-speed drag / Shift-fine gesture and passive in-bar caption. Secondary
  theme accent is used (Neon's Formant cyan/blue-green, not pink).
- Speed is exactly **percent / 3**: 0 freezes, 3 keeps the prior **1.2** procedural
  baseline, 100 is **33.333...x** that baseline. `DecorativeMotion` accumulates
  elapsed visible time; editing speed never multiplies an absolute wall clock
  or jumps the existing phase. Hiding pauses and reopening resumes without
  integrating hidden time. No timer was added; the existing 30 Hz editor timer
  publishes the inherited phase. Audio, meters, modulation and reactive FX
  measurements are not speed-scaled.
- `Processor::motionPercent` is non-parameter presentation state. It persists as
  the root **`motionPercent`** attribute in binary host state and full copy/paste
  state, survives editor recreation on that processor, and defaults to 3 for
  legacy state. Sound presets strip it and retain the current instance's value,
  like skin. It is **not a new automatable host parameter**, not a machine-global
  preference, and does not change the existing **2252 IDs/indices**.
- Buttons, bars, backgrounds, glints and rotary materials inherit the same phase.
  Popup backgrounds resolve their originating component; tooltip backgrounds use
  their editor look-and-feel's published phase. Separate peers sharing that
  look-and-feel repaint on the existing background-frame tick.
- Glints retain the fixed rare seeds / coherent envelopes, with stronger isolated
  accent texels rather than more glitter. Fat material retains exact **3x3 texels**,
  **192x39 tile**, and **25 px track**. The previous upper-white lift is removed;
  shadows and middle tones are deeper instead. Bipolar fill and caption contrast
  now share the true zero origin, including an empty fill at 0 dB.
- Creative inspector backgrounds, utility backgrounds and Tone backgrounds are
  anchored to their full scrolling content bounds. Routing canvas and voice
  cards already use local/content coordinates; fixed headers retain separate
  surfaces. Pixel tests compare the same content after a 37 px scroll in all
  themes. Scroll tracks/thumbs remain square under the shared look-and-feel.
- Sparse creative groups use actual visible row counts (one or two controls,
  including Mix); dense wet-shaping/saturation groups remain knobs. Gain + Dry/Wet
  and Chorale Motion + Mix are two fat bars, including rack, Routing inspector
  and expanded Chorale bodies. The crossover selector plus two boundaries is
  three controls and remains rotary. Voice numeric entry is retained; fat FX
  captions remain passive. Voice-card and Voice Motion materials are retained
  and verified. Small signal/parameter markers are square; normal rotary faces
  are intentionally still round.
- `vocalstackSquarePrototype` is an **opt-in review prototype only**, rendered
  beside an unchanged rotary and slider on the same real Drive parameter. Its
  open square perimeter has no circular face/arc underneath. It retains the
  underlying rotary drag interaction. No production rotary is globally replaced.

### Actual reactive factory FX

- Both new views reuse the existing **per-slot `FxMonitor` bridge**, rather than
  adding a timer, global effect-type telemetry, or reading live DSP from the GUI.
  A coherent frame now includes actual **stereo wet RMS before slot blending**.
  Convolution retains its original generated **48 kHz left-channel normalized
  peak-envelope bins** and explicitly labelled illustrative Tail taper. The live
  meter has its own header row, so it cannot mask the quiet end of the IR plot. It is not
  is not an exact current-rate/partition-centre/tilt/predelay/duck response model.
- Saturator uses the actual `Waveshaper::transfer` Warm Asymmetric function at
  effective Drive/Character; the secondary curve segment spans actual measured
  stereo PRE input extrema. This is an operating-span overlay, **not** a fake
  waveform or sample-wise nonlinear scatter. Tone, Output, matching, Mix,
  smoothing, ADAA/DC removal and oversampling are not part of the static curve;
  the tooltip/footer label those limitations. The wet bar includes real output
  processing, before the slot blend.
- Without processed audio, the parameter model stays meaningful and stable;
  there is no invented audio animation. Hidden/clipped views release subscriptions.
  Reset invalidates immediately; an unchanged stream clears after 250 ms without
  new publication and does not reanimate from the same stale frame. Bypassed/dry
  factory slots report zero wet activity; the DSP may still clock its history.
- Audio publication remains bounded/full-rate 2048-sample windows, approximately
  30 Hz, with three ownership buffers, at most three read attempts, and existing
  lock-free assertions. Wet energy reuses the already-bounded power storage.
  No audio allocation, destruction, locks, I/O or GUI access was introduced.
  Granular's stable parameter/history graph remains intact.

### Validation observed for this pass

All compiles, tests, benchmarks, JUCE renders and FFmpeg encodes ran through
`tools/Invoke-HalfCpu.ps1` in dedicated x64 PowerShell processes:
**6/12 logical CPUs, affinity 0x3F**. No dependency or external asset was added.

- Final Release build succeeded for CLAP, VST3 and test artifacts.
- **71/71 editor tests passed in an isolated run**:
  `source/build/coordinated-editor-delivery.log`.
- **37/37 remaining CTest tests passed**, including the updated Main Bus FX rack
  UI, CLAP smoke, all ten factory audio presets, factory DSP, monitor concurrency,
  granular UI, state/automation and allocation regressions:
  `source/build/coordinated-other-37.log`. Thus every registered test was exercised
  successfully, with the editor suite scheduled separately. After the last
  meter-header-only layout correction, all 71 editor tests and both CLAP smoke /
  Main Bus FX rack UI tests were rerun and passed; the DSP was unchanged.
- A fully parallel full-suite run is **not claimed clean**: the editor's existing
  `Canonical Chorale folding ... Host bypass left folded node selected` timing
  check failed under the concurrent heavy DSP/preset run. It passed when the full
  editor suite ran without that concurrent load. No timing assertion was weakened.
- New checks cover exact linear/freeze/hidden phase math; Motion state/legacy
  recall and preset isolation; unchanged parameter count; visible sparse layouts;
  bipolar caption contrast; content-anchored scroll pixels; no invented idle
  activity; monotonically advancing real DSP frames; reset and persistent stall
  clearing. The factory DSP test reconstructs pre-blend wet from coherent input /
  output at 50% Mix and checks the exact stereo RMS oracle, duplicate instances,
  bypass, reset silence, and zero audio allocations/destructions. The concurrent
  snapshot stress test additionally checks coherent wet-energy fields.
- Informational benchmark: `source/build/coordinated-benchmark.csv`. At 48 kHz,
  two seconds of routed audio took 62.81 / 63.19 ms for Convolution without /
  with an active bridge, and 78.80 / 80.46 ms for Saturator. Checksums agree.
  The unsubscribed Convolution sample was noisier/slower (85.88 ms), so these
  single-run figures are **not a reliable overhead bound or real-time guarantee**.
  Work/storage bounds and allocation tests are stronger evidence than timing noise.
- `git diff --check` passed. Tracked 1.9.10 CLAP/VST3 hashes remain exactly those
  recorded at the top. No tracked package replacement, install, commit or push.

Final local build artifacts (not tracked packages):

- `source/build/VocalStack_artefacts/Release/CLAP/Vocal Stack.clap`
  SHA256 `2E85E5AB73B29E9F7BC1140E630FDE3A82442784E6766A7CBF9E61BA7C849876`
- `source/build/VocalStack_artefacts/Release/VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3`
  SHA256 `767C4405F1DF1A4F64BB1300EC248030F7109F26D8D3EAF8714F97215036FAA8`

### In-action previews and review images

All current artifacts are in the ignored local folder:
**`source/build/approval-coordinated/`**.

For each `{theme}` = `device`, `neon`, `spectral`:

- **`convolution-live-{theme}-production-ui.gif`** and
  **`saturator-live-{theme}-production-ui.gif`**: actual full production editor,
  real offline two-tone burst through `Processor::processBlock`, with the normal
  routing/DSP/telemetry path. Twelve snapshots, **7680 real samples / 160 ms**
  per step at 48 kHz; the burst stops at sample 24000 and subsequent frames show
  measured silence/IR tail. No telemetry is manually injected. Decorative Motion
  is **0%** during these captures, demonstrating that measured FX stay reactive.
- Matching `*-controls.gif` files use the actual 840x460 production-panel fixture;
  `*-contact.png` is an unscaled six-frame sheet. `*-frame-00.png` through `11`
  and `*-full-frame-00.png` through `11` retain original component snapshots.
- **`motion-100-{theme}.gif`** and `motion-100-{theme}-phase-{0..3}.png`:
  actual Stack editor, one-second phase steps using the linear 100% accumulator.
  Header, two Chorale bars, Voice Motion, voice cards and square scrollbar are
  visible together. These are explicitly decorative phase-series captures,
  not claims of live audio in the Stack monitor.
- `header-motion-{theme}.png`, `two-sliders-scrollbar-{theme}.png`, and
  `square-prototype-{theme}.png` give compact review details. Prototype order is
  **square perimeter / existing rotary / existing slider**, all at 24 dB Drive.
- `convolution-before-after-{theme}.png`: unscaled old lifted material on the
  **left**, current deeper material + honest live-status model on the **right**.
- Existing `update-stack-full-*`, `voice-sliders-*-crop`, `routing-nodes-*-crop`,
  Church/Plate/Saturator controls/routing and popup captures are refreshed here.
  Images were inspected across all three themes; no image enhancement is applied.
- `render.log`, `encode.log`, and ignored `encode.py` record local rendering /
  encoding. Reproduce JUCE outputs with the rebuilt editor tests' new
  **`--render-coordinated`** command from the desired output directory, using
  the resource-limit wrapper. FFmpeg was already available on this machine;
  it was not downloaded or added to the project.

These GIFs are **offline production-UI renders driven by real DSP**, not a DAW
screen recording, listening test, or evidence of host automation playback.

## Repeated Dry / Wet utility — source preview

The existing `NodeType::mix` already supplies a genuine two-input utility, not
an effect's internal Mix knob, Splitter or compensated Sum. It remains in
Routing **+ Node → Dry / Wet Mix**, with independent **Dry pin 0**, **Wet pin 1**
and one output. No replacement DSP, node type, parameter or dependency was added.

- The blend remains linear: **0% = Dry**, **100% = Wet**, **50% = 0.5 Dry +
  0.5 Wet**. Identical inputs remain unity, rather than receiving an equal-power
  boost. Unconnected inputs receive silence; one connected input is attenuated
  by its blend gain. Multiple cables on a pin retain existing automatic fan-in
  compensation **before** the two pins are blended. New Add retains its existing
  25% node default; host-parameter double-click default remains 50%.
- Right-click a local Dry/Wet node → **Duplicate Dry / Wet**. This copies the
  current host **base** balance into a new stable node/parameter identity, not
  the instantaneous modulated value. The copy is **unwired** and receives no
  copied modulation assignments or alias. Existing cables, including source
  fanout, are untouched, so duplication cannot silently alter the running mix.
  Connect its Dry/Wet inputs and output explicitly. The main Lead/Stack mixer
  is not duplicated by this action.
- The inspector and expanded local mixer now use the shared **square textured
  fat slider**, percentage caption, passive keyboard policy and a short endpoint
  tooltip. FX bypass is deliberately unavailable on this routing utility; select
  0% to hear its Dry input. Whole-plugin bypass still uses latency-aligned dry.
- The bounded RT limits are unchanged: **48 total graph nodes, 16 FX, 128
  connections**, up to 16 incoming branches per input. Dry/Wet consumes **one
  total node and zero FX slots**. The unchanged 12-node default graph has room
  for **36 additional** Dry/Wet nodes. Lead + Output leaves room for **46**;
  an Output-only graph can hold 47 silent mixers, but has no audio source.
  Add and Duplicate disable at the total-node cap. There is no singleton limit
  and no promise of an unbounded number of nodes.
- Existing automation is reused without appending or renumbering anything:
  node IDs **200..247** map explicitly to **`mix1Balance`..`mix48Balance`** at
  zero-based host indices **944..991**. All **2252 parameter IDs/indices** remain
  unchanged. Spawning/duplication now allocates only this mapped ID pool, rather
  than leaving a misleading fallback to unmapped IDs 248..299. Each base balance
  remains independently automatable/modulatable, smoothed, saved and recalled.
- Routes declaring nonzero algorithmic latency remain **rejected**, not silently
  accepted without compensation. The graph assumes common algorithmic latency;
  intentional voice delay and IIR/ADAA/filter phase differences are not corrected.
  This is not a phase-alignment or arbitrary external-latency compensation tool.

### Dry/Wet validation and actual review images

All compiles/tests/renders ran through `tools/Invoke-HalfCpu.ps1` in dedicated
x64 PowerShell processes: **6/12 logical CPUs, affinity 0x3F**.

- Release CLAP/VST3 and affected test binaries rebuilt successfully:
  `source/build/drywet-build.log`.
- **72/72 editor checks passed** in isolation:
  `source/build/drywet-editor-final.log`. **37/37 other CTest tests passed**,
  including CLAP smoke, factory presets, DSP, state and RT ownership regressions:
  `source/build/drywet-regression.log`. Editor and heavy DSP tests were scheduled
  separately, preserving the earlier preview's timing-test precautions.
- `source/tests/DryWetUiTests.h`: actual asynchronous Add and Duplicate menu
  actions in all three themes; live-base value copying, three independent
  balances, repeated Add through all 48 total nodes, disabled capacity actions,
  fat inspector/expanded controls, no utility FX bypass, duplicate/delete
  undo/redo, and wired Dry/Wet pin restoration without changing surviving routes.
  Popup completion is pumped with the Windows message queue in this test only;
  this new popup-action harness is currently Windows-specific.
- `source/tests/DryWetRoutingTests.h` (in RoutingBackendTests): mono/stereo,
  32/44.1/48/96/192 kHz, exact dry/wet/midpoint gains, identical-input unity,
  missing-input silence, nonfinite automation containment, reset silence,
  invalid pins/cycles/nonzero-latency rejection, a fully audible **45-mixer +
  two-source + Output = 48-node** DAG with distinct balances, rejection of node
  49, and zero audio-thread allocation/destruction.
- `source/tests/RoutingSplitterStateTests.cpp`: independent first/second/last
  mapped mixer balances, unchanged host indices, binary host recall, copy/paste,
  sound preset recall, graph-only legacy parameter seeding, and whole-plugin
  latency-aligned bypass with irregular 7/127-sample blocks.
- Actual **1500x1000 production-editor** component snapshots are in the ignored
  `source/build/approval-drywet/` folder. For `{theme}` = `device`, `neon`,
  `spectral`: **`drywet-{theme}-nodes-inspector.png`** shows three separate mixers
  and the selected inspector; **`drywet-{theme}-add.png`** and
  **`drywet-{theme}-duplicate.png`** composite the actual popup peers at their
  real screen positions. Matching `*-popup-crop.png` files retain the menus.
  No mock menus or image enhancement. All three selected-inspector images and
  representative Add/Duplicate menu crops were visually inspected. Reproduce
  with the editor test binary's **`--render-drywet`** mode through the wrapper.
- Final local, uninstalled binaries (not `Windows-x64` packages):
  CLAP SHA256 `525970AAEC873A28E92AA0C3219D2D1A348B9DF93527E9C3BCEBF90E4BA5438A`;
  VST3 SHA256 `252CB58C3167CAE2E95315FE658CBB3F35AB3626443FA0876E941BAFFCDAB325`.
  These supersede the earlier local artifact hashes above, not tracked packages.

No DSP rewrite, parameter-count change, package replacement, installation,
commit or push. Earlier coordinated all-DSP/UI/Motion pending work is preserved.
No DAW automation recording, listening test or installed-plugin verification is
claimed; visual approval remains pending.

## Routing right-inspector dial density — source preview

This pass is scoped to the **Routing canvas right Creative FX inspector** via
`CreativeEffectPanel::setInspectorLayout(true)`. Expanded node cards, main-bus
rack tabs, voice controls, Tone/Utility panels and other ParameterKnob users
retain their existing layout/rendering. No panel-width limits, DSP, parameter,
state, version, Sidechain roadmap or square-prototype deployment changed.

- Dense rows pack at an **88 px readable minimum**, share available line width,
  and cap short final-line cells at **110 px** rather than stretching a lone dial
  across the panel. Existing **2 px horizontal/row gaps** and 6 px group separation
  remain. The 320..480 px inspector limits and scrollbar reservation are unchanged.
- Inspector-only rotary layout explicitly reserves a **20 px native numeric-entry
  lane**, a dial lane up to 96 px high, and a measured **16 px or wrapped caption
  lane**. Caption height uses the actual pixel-font TextLayout, not character
  counts. Numeric labels keep the 12.5 px pixel font with no horizontal shrinking.
- The dial fills its remaining square with **10 px radial clearance** for the
  assignment arc and effective-value marker. This is not a blind global radius
  multiplier: both lanes and modulation extent remain inside the control cell.
- One-/two-control sections retain their existing full-width **25 px fat bars**,
  passive captions/focus policy and compact buttons. Voice pointers remain 10 px.
  More vertical scrolling is expected for dense inspectors; controls reflow rather
  than overlapping, and the existing per-instance scroll anchor is retained.

Observed production geometry (same in Device, Neon and Spectral):

| Reverb first dense control | Before | After, 320 px inspector | After, 480 px inspector |
| --- | --- | --- | --- |
| Cell | 68 x 100 px | 97 x 131 px | 89 x 123 px |
| Dial lane | 66 x 52 px | 95 x 95 px | 87 x 87 px |
| Painted inner face diameter, from renderer geometry | 27.4 px | 58.1 px | 51.9 px |

The face is approximately twice the old diameter without widening the inspector.
The smaller cell at 480 px is intentional: five readable controls fit in a line
rather than three, while the dial still uses its bounded lane. Per-effect geometry
is printed in each capture folder's `render.log`.

### Validation and actual before/after screenshots

All compiles, tests, JUCE renders and contact-sheet generation used
`tools/Invoke-HalfCpu.ps1` in dedicated x64 PowerShell processes:
**6/12 logical CPUs, affinity 0x3F**. No packages or dependencies were installed.

- Release CLAP, VST3, Editor, Main Bus FX rack UI and Granular UI targets built:
  `source/build/inspector-release-build.log`. A final test-only rebuild is recorded
  in `source/build/inspector-final-test-build.log`.
- **73/73 editor checks passed** in isolation:
  `source/build/inspector-editor-tests.log`. This includes existing dense rack/card,
  sparse/fat slider, voice, square prototype, scrolling, focus and pending-preview
  checks; their assertions were not relaxed.
- New `source/tests/InspectorControlLayoutTests.h` exercises all seven Creative
  effects in all three themes at panel widths **174, 190, 220, 280, 320, 400,
  480 px**. It checks row/hitbox disjointness, visible-child confinement, measured
  caption height, numeric min/max/current text widths, unshrunk fonts, actual
  numeric-label and dial hit targets, separate value/dial/name lanes, full scroll
  reachability, unchanged fat Mix and inspector-only opt-in.
- **3/3 adjacent CTest checks passed**, scheduled separately: `VocalStackCLAP`,
  `VocalStackMainBusFxRackUi`, `VocalStackGrainSnapshotUi`:
  `source/build/inspector-adjacent-tests.log`. No separate DSP regression run was
  needed for this UI-only change; the editor suite includes its existing DSP/state
  integration checks. No DAW listening or installed-plugin test is claimed.
- Actual production-editor renders are in the ignored local folders
  **`source/build/approval-inspector-before/`** and
  **`source/build/approval-inspector-after/`**. Baseline was rendered before the
  production layout edit, using the same new render fixture.
  Each contains **84 production-capture PNGs**: seven effects x three themes x two widths x full
  context/inspector crop. Filenames are
  `{delay,reverb,compressor,bitcrusher,granular,convolution,saturator}-`
  `{device,neon,spectral}-{narrow,wide}-{full,inspector}.png`.
  Narrow is an actual **904 x 600 editor / 320 px inspector**; wide is
  **1500 x 1000 / 480 px inspector**. Captures scroll to the first dense group
  where possible; short viewports intentionally show scrolling content at the edge,
  not clipped layout cells. Full-context images retain the graph and panel boundary.
- Six unscaled before/after contact sheets in the after folder are
  **`review-{device,neon,spectral}-{narrow,wide}.png`**, before above / after below.
  All six were visually inspected, including Convolution/Saturator and the older
  dense effects. Individual Reverb, Convolution and Saturator crops/full contexts
  were also inspected. No image enhancement, mocked controls or square replacement.
  Reproduce production captures using **`--render-inspector-controls`** through
  the wrapper from a desired output directory.
- `git diff --check` passed. Tracked `Windows-x64` packages remain untouched.
  Final local, uninstalled CLAP SHA256:
  `6B4F79CA3B0E11827ACC964453D0BC2E746E650F28DC6ADDA6E3CBBF24B036E2`;
  VST3 SHA256:
  `C69B96E3709ADCC416917618E1142CCF387B82BA151DF8BE4FEDBAAAFFEB4156`.

All prior pending previews and the **planned/unimplemented SIDECHAIN / Spectral
Morph roadmap** are preserved. No package replacement, installation, commit or push;
this presentation remains subject to visual approval.

## Thicker scrollbars — source preview

This request changes **scrollbars**, not audio parameter sliders. The current
JUCE 8.0.12 **LookAndFeel_V4** default and actual before renders both establish
an **8 px** baseline (V2's older 18 px default is not the active baseline).
`scrollbarMetrics` in `VisualTheme.h` computes nearest-integer logical pixels
once, using `(baseline * 18 + 5) / 10`:

| Use | Before | After |
| --- | ---: | ---: |
| Default vertical track width / horizontal track height | 8 px | 14 px |
| Stack voice-strip horizontal track height | 12 px | 22 px |
| Default painted thumb cross-axis extent, retaining 1 px inset per side | 6 px | 12 px |
| Voice-strip painted thumb cross-axis extent, same inset | 10 px | 20 px |

Both track and thumb grow together. Square ends, inherited theme/material
textures, hover response and the existing 1 px thumb inset remain unchanged;
there is no new scrollbar painting style. PixelLookAndFeel supplies the shared
default, including inherited native-control scrollbar defaults. PassiveViewport
and the ordinary Routing/Motion viewports use stable shared metrics as well, so
standalone panels and device-theme fallback do not retain a narrower default.
The Stack voice strip keeps its separately scaled baseline. No resize or theme
callback multiplies a current width.

Audit covered the editor Stack viewport, Routing modulator rail and right
inspectors, Creative/Tone/Utility inner viewports, main FX rack tabs/body, and
Motion Controls/Assignments viewports. Preset browsing uses JUCE PopupMenu scroll
zones/arrows rather than a separate configurable ScrollBar; those menu arrows
were not mistaken for parameter sliders or redesigned. Popup/control look-and-feel
inheritance remains intact. This pass does not claim native popup interaction
coverage.

Existing layouts already subtract `getScrollBarThickness()` from available
content space. The voice strip was the exception: its 278 px cards exactly filled
the previous 290 px viewport including the 12 px bar. Its enclosing scrollable
body now gains **10 px**: viewport **290 → 300 px**, drawer **430 → 440 px**,
whole Stack canvas **940x1148 → 940x1158**. Cards remain 278 px high. The fixed
preset footer, saved/default **940x1104** window, minimum width, and all audio
sliders/Motion geometry and behavior are unchanged. At the 940 px minimum editor
width, the rightmost fixed control ends at x=922 and the 14 px vertical track
begins at x=926, so no extra outer horizontal bar is needed.

### Scrollbar validation and review images

All builds, tests and render generation used `tools/Invoke-HalfCpu.ps1` in dedicated
x64 PowerShell processes: **6/12 logical processors, affinity 0x3F**.

- Release CLAP, VST3, Editor, Main Bus FX rack UI and Granular UI targets rebuilt:
  `source/build/scrollbars-release-build.log`. Binaries remain local/uninstalled.
- **8/8 focused editor groups passed**: `source/build/scrollbars-editor-checks.log`.
  The new `ScrollbarTests.h` covers all three themes, horizontal/vertical and
  nested viewports, repeated layout, track confinement, non-overlapping scrollbar
  corners/content, complete end-of-scroll reachability, narrow Motion viewports,
  actual Routing/Stack editors at 940/972/1200 px widths, and unclipped voice cards.
  It also runs the pending dense-inspector regression: all seven Creative effects
  at 174/190/220/280/320/400/480 px, including numeric text/hitbox/caption confinement.
  Existing slider sensitivity/rendering, passive wheel, Tone-card, fluid material
  and responsive Creative checks pass unchanged. This is focused validation,
  **not a claim that all 74 editor groups were rerun**.
- **3/3 adjacent CTest suites passed**: CLAP, Main Bus FX Rack UI and Granular UI,
  including 294 hit-tested rack/editor wheel cases. Log:
  `source/build/scrollbars-adjacent-tests.log`.
- Actual production-editor before/after renders are in
  `source/build/approval-scrollbars-before/` and
  `source/build/approval-scrollbars-after/`, **12 PNGs per folder**. Each contains
  `{device,neon,spectral}-{routing,dense-inspector,fullstack,voice-strip}.png`.
  The dense effect is Convolution; fullstack captures scroll to the eight-voice
  strip at the bottom of the real **940x760** editor. `capture.log` records actual
  8/12 px before and 14/22 px after metrics. Reproduce with
  `VocalStackEditorTests.exe --render-scrollbars` through the CPU wrapper from
  the desired output directory. Device before/after and Neon/Spectral routing,
  inspector and voice-strip output were visually reviewed.
- **Physical placement versus rendering:** native sessions enumerate connected
  displays before interaction. Observed primary work area `(0,0,3440,1392)` and
  non-primary work area **`(3440,357,1920,1032)`**, both **96 DPI / scale 1**.
  The capture fixture was placed on the non-primary second monitor at
  **`(3930,493,940,760)`** before interaction; capture logs retain the evidence.
  `GuiTestDisplay.h` selects a non-primary display dynamically using JUCE's
  DPI-aware display/work-area coordinates, including negative origins, checks
  confinement and reports a primary fallback if necessary. It refuses oversized
  native fixtures rather than silently placing them outside the work area.
  Rack and Granular native harnesses now use it too.
- **Every PNG here is a JUCE offscreen component render, not a physical desktop
  framebuffer screenshot**, even when its fixture has a native peer on monitor 2.
  Standalone nested/dense inspector and drawing regressions are pure offscreen
  tests with **no physical monitor placement**. The rack's oversized all-tabs
  overview is explicitly detached for offscreen rendering, then restored to its
  confined second-monitor window. No user host/unrelated window was moved.

All earlier pending source previews, preference work, and the planned/unimplemented
SIDECHAIN / Spectral Morph roadmap remain preserved. No dependency/package install,
tracked `Windows-x64` refresh, commit or push. Version remains **1.10.0 source
preview**, pending approval. No full DSP suite, DAW listening/transport test,
installed-plugin check or physical framebuffer capture is claimed for this pass.

## Chunkier square Routing nodes and double-weight connections — source preview

This follow-up changes Routing presentation only. **Real rotary knobs remain
circular**; the review-only square-perimeter prototype is not enabled anywhere
new. Node frames, expand/collapse affordances, pins, status badges, toolbar and
inspector controls are square. Cable trajectories remain naturally curved.
No DSP, routing policy, parameter identity, graph limits, package, version or
SIDECHAIN / Spectral Morph implementation changed. There is no implemented donor
port in the current Routing UI to restyle; this pass does not invent one.

Actual logical-pixel geometry at 100% zoom:

| Routing element | Before this pass | After |
| --- | --- | --- |
| Ordinary / selected frame stroke | 1 / 2 px | 2 / 3 px |
| Header height / left title inset | 28 / 8 px | 30 / 6 px |
| Header / port font size | 11.5 / 11.5 px | 14 / 12.5 px |
| Output pin | 8 x 8 px | 10 x 10 px |
| Input pin | 8 x 12 px | 10 x 10 px, square |
| Expand/collapse | Bare glyph in 22 x 22 target | Outlined square 22 x 22 face and target |
| Expanded Output or main Mix control cell | 140 x 110 px | Full 190 x 114 px body |
| Normal and drag-preview cable width | 2 px | **4 px** |
| Selected cable width | 3.5 px | **7 px** |
| Cable body selection lane | 12 px | 12 px, still wider than every painted state |

- Compact widths remain **190 px**. Active one-port nodes remain 64 px high,
  two-row Dry/Wet/Splitter nodes 84 px, three-row Multiband nodes 106 px.
  First-port offset **42 px** and row pitch **22 px** are unchanged, as are
  expanded body dimensions, inspector width limits and separate compact/expanded
  position banks. The new header and port labels paint in a consistent node-local
  coordinate system, avoiding fixed-screen text overlaps as zoom changes.
- Bypassed effects now reserve a separate **18 px** status lane below the last
  port. The square `Bypassed` badge no longer competes with long effect names in
  the header or with embedded controls. Only bypassed effect heights/body offsets
  gain those 18 px; port positions do not move. User-authored overlapping layouts
  are not silently rearranged when bypass changes.
- Expanded Creative FX cards opt into the existing bounded dense layout: minimum
  **88 px** cells, capped short rows, separate numeric/dial/caption lanes, and
  scroll-reachable controls. This explicitly extends the earlier right-inspector
  density preview to expanded Routing cards, not to main-rack or global knob users.
  Existing fat textured bars and **14 / 22 px** scrollbar metrics remain intact.
- `RoutingPanel` owns a scoped `PixelLookAndFeel`, so standalone/native Routing
  contexts cannot fall back to JUCE's rounded toolbar buttons, FX enable controls,
  combo boxes or scrollbar thumbs. Embedded Routing uses the same square material
  renderer. The local decorative phase follows the existing animation clock.
- All cable states use absolute shared widths, not repeated multiplication:
  normal, inactive/dashed and destination/source drag previews use **4 px**;
  selected active/dashed cables use **7 px**. There is no separate hover-width
  state in the current implementation. Explicit **butt caps / miter joins** add no
  decorative round endpoints. Curves, colors, inactivity dashes and single-output
  rewiring semantics remain unchanged. Cable width stays in screen pixels at zoom.
- Pin pickup/drop/rewire targets are square and consistent: **20 x 20 screen px**
  at 100% and above; below 100% they scale with zoom, remain twice the painted pin
  size, and do not overlap adjacent port rows. Cable-body selection retains its
  existing 12 px tolerance. No graph-count or viewport clipping workaround added.

### Completed validation and actual review images

All builds, tests and render generation used `tools/Invoke-HalfCpu.ps1` in dedicated
x64 PowerShell processes: **6/12 logical processors, affinity 0x3F**.

- Final Release CLAP, VST3, Editor, Main Bus FX Rack UI and Granular UI targets
  built: `source/build/validation-routing-scrollbars/release-build.log`.
- Final focused `--filter Routing`: **18/18 editor groups passed**:
  `source/build/validation-routing-scrollbars/routing-tests.log`.
- Final full editor run: **75/75 groups passed**, including the Stack resize and
  Device/Neon/Spectral editor groups:
  `source/build/validation-routing-scrollbars/editor-tests.log`.
- The first run's four failures (71/75) were incomplete test updates for the
  **related, ongoing scrollbar preview**, not unrelated failures. Production
  layout and those assertions now share `editorLayout` constants in
  `PluginEditor.h`. The scrollable canvas derives from the fixed Stack origin,
  panel height and bottom margin; only **22 - 12 = 10 px** of additional voice
  scrollbar reservation is added. Explicit independent assertions pin
  **1148 + 10 = 1158 px**, and the default window remains **940 x 1104**, not
  940 x 1114. Stack panel height is **430 + 10 = 440 px**. Existing containment,
  panel separation, scrolling, footer exclusion, native-size/no-transform checks,
  activation, and resized-window recall assertions remain enabled. The editor
  groups additionally verify saved default width/height after closing and both
  dimensions on reopening. No layout regression or failing check was hidden.
- The final added group passes independently after strengthening its checks:
  `source/build/chunky-final-focused-tests.log`. `RoutingChunkyTests.h` exercises
  all **16 effect node types x three themes**, compact/expanded restoration,
  square pin-corner pickup/drop at multiple zooms, bypass renders, expanded dense
  row disjointness, numeric/dial confinement and scroll reachability, absence of
  the square-knob prototype, exact 4/7 px strokes, butt endpoints and hit-lane
  width. Existing packing, independent positions, virtualization, multiband,
  splitter, repeated Dry/Wet, control binding, wheel and history groups also pass
  in the full run. The menu-anchor probe now explicitly installs a square-style
  probe on Routing rather than assuming the global default look-and-feel.
- **3/3 adjacent CTest suites passed**: `VocalStackCLAP`,
  `VocalStackMainBusFxRackUi`, `VocalStackGrainSnapshotUi`:
  `source/build/validation-routing-scrollbars/adjacent-tests.log`. No broad DSP rerun was required for
  this UI-only change. No DAW listening or installed-plugin test is claimed.
- Before production-panel renders were captured before the presentation edits,
  using the same cluster fixture:
  **`source/build/approval-routing-chunky-before/`**, 12 PNGs. This standalone
  baseline honestly exposes the old rounded JUCE fallback as well as thin frames.
  Existing before captures were not recreated or substituted.
- Final production-panel renders:
  **`source/build/approval-routing-chunky-after/`**, 24 PNGs. Both directories have
  `{device,neon,spectral}-{compact,expanded}-{cluster,crop}.png`. The after folder
  additionally has `{device,neon,spectral}-compact-{selected-cable,drag-preview,zoom-out}.png`
  and `{device,neon,spectral}-expanded-dense-controls.png`. The 1:1 crops show
  real compact-node geometry and the expanded Convolution face. Full contexts
  show expanded Dry/Wet alongside compact neighbors and the retained inspector;
  dense-control views show circular dials. Device compact, Neon expanded and
  selected/drag states, and Spectral expanded dense/zoom views were visually
  reviewed. Reproduce with `--render-routing-chunky` through the wrapper from an
  output folder. PNGs are direct JUCE component renders/crops, not mockups.
- Each native Routing fixture enumerates actual connected display bounds,
  primary flags, work areas and DPI/scale, then places itself on the selected
  non-primary display. Capture logs record primary work area
  **(0,0,3440,1392)**; non-primary work area **(3440,357,1920,1032)**; both
  **96 DPI / scale 1**. The 1600 x 900 capture fixture is placed at
  **(3600,423,1600,900)**, fully within the second monitor's work area.
  **All PNGs are offscreen JUCE renders, not physical framebuffer screenshots.**
  No user's host/unrelated window was moved. Native popup interaction/placement
  beyond the existing programmatic menu-anchor checks is not claimed.
- The final Stack resize fixture uses a bounded native host on the second monitor
  at **(3660,373,1480,1000)**. Deliberate 1040/1400 px-tall editor cases are
  programmatically tested as **clipped/offscreen child layouts**, not oversized
  desktop windows. Device/Neon/Spectral editor PNG tests have no native peer and
  are explicitly logged as **pure offscreen, no physical monitor placement**.
  Their new outputs are isolated in `source/build/validation-routing-scrollbars/`
  (`editor-{device,neon,spectral}{,-advanced,-eight-voices}.png`), leaving earlier
  review assets intact. No before captures were rerendered for this validation fix.

`git diff --check` passed; Git only reported its existing LF-to-CRLF advisory for
`PluginProcessor.cpp`. Tracked `Windows-x64` packages remain unchanged.

All earlier uncommitted source previews remain in place. No dependency install,
tracked `Windows-x64` replacement, package installation, commit or push occurred.
Version remains **1.10.0 source preview**, pending user approval.

## Approval resolved; remaining honest limits

The entire preview was explicitly approved and delivered as 1.10.0; package
replacement, installation and installed CLAP smoke are now complete. The final
[release record](Release-1.10.0-Validation.md) supersedes historical gates above.
No Bitwig process was stopped. Listening, DAW transport, host automation recording,
VST3 host validation, native accessibility/high-contrast, Linux/macOS builds and
external IR workflows remain unverified or outside this release.
