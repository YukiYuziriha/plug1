# HEAVEN autotune: artifact research and detection reuse

Status: implementation preparation / research plan, 2026-11-25.
No donor is selected or sonically validated yet. This document records the user's
current requirements; it is not a claim that tests or audio comparisons passed.

## 1. What the user actually wants

- A very good **hard-autotune sound**, judged against the user's installed Antares
  Auto-Tune Pro. The snaps, stepped scoops/slides, warbles and transition texture
  are desirable parts of the effect. Do not optimize them away in pursuit of
  transparent correction or choose an engine solely for sustained-note accuracy.
- Reuse someone else's **complete working open-source tuner**. Embed it through a
  thin adapter, preserving its detector, target selection, correction dynamics,
  formant behavior and shifter. Do not invent another detector/shifter hybrid.
- **Always retain detected pitch and notes for valid analyzed material.** Reuse
  that analysis on subsequent playback rather than repeatedly detecting it.
- The motivation is lower CPU with **five parallel autotunes**. Determine whether
  this means five internal branches, separate plugin instances, or separate vocal
  takes before choosing the sharing scope. Do not silently assume one of these.
- Correction and shifting remain live, with independent tuning settings where
  needed. Downstream HEAVEN processing and automation remain live too.
- This is **not a corrected-audio render cache**, freeze feature or Melodyne-style
  audio editor project. Do not turn it into Live/Edit/Cached audio modes or add
  offline resynthesis/piano-roll scope without a separate request.

## 2. Research the desired artifacts using Antares as a black box

The installed files were found at:

- `C:\Program Files\Common Files\VST3\Antares\Auto-Tune Pro.vst3`
- `C:\Program Files\Common Files\VST3\Antares\Auto-Key.vst3`

Existence is verified; exact version, successful loading, authorization, parameter
access and audio rendering are **not** verified. Auto-Key is not the correction
reference. Do not infer Auto-Tune's implementation from an artifact alone.

Use normal authorized plugin hosting to record input/output behavior. No license
bypass, binary extraction, decompilation or proprietary code reuse is part of this
plan. Check applicable usage terms before automated comparative work. Black-box
observations guide donor selection, not a newly invented Antares clone.

### Reference setup

- [ ] Load Auto-Tune Pro in an isolated host process; use Bitwig exports if the
  offline harness cannot host it correctly. Do not alter the user's projects.
- [ ] Record exact version, algorithm/mode (including Classic if available), input
  type/range, key, scale, reference tuning, retune speed, tracking, humanize,
  flex/transition-related options, vibrato, formant and mix settings actually
  exposed by this version. Do not assume similarly named controls are equivalent.
- [ ] Establish the user's preferred hard-tune reference settings by supplying
  short labeled renders for listening. Keep distinct Antares modes distinct.
- [ ] Save plugin state plus a readable parameter manifest, input/output hashes,
  sample rate, channels, block size, reported latency, preroll and reset policy.
- [ ] Render repeats to measure determinism; distinguish realtime and offline
  processing if the plugin behaves differently. Keep original raw renders.

### Material and artifact probes

Use real isolated singing as the main evidence. Synthetic probes help isolate
behavior but cannot substitute for listening to vocals.

| Material/probe | Behavior to compare |
| --- | --- |
| Slow rising/falling scoops across scale notes | Staircase rhythm, switching thresholds, direction-dependent hysteresis |
| Fast runs, short passing notes and onset scoops | Which intermediate notes survive and the texture of each snap |
| Sustained vowels with narrow/wide vibrato | Flattening, boundary flutter, warble and retained modulation |
| Abrupt semitone/large interval changes | Attack, settling, glide/overshoot and transition coloration |
| Breathy/quiet entries, consonants and brief dropouts | Lock/release behavior and pitched/unpitched handover |
| Low/high registers and different vowels | Period-dependent roughness, octave errors and timbral change |
| Repeated phrases with different entry/preroll points | State dependence and sensitivity to transport resets |

Measure output pitch trajectories with an independent analyzer and confidence
mask, switch timing/dwell, settling behavior, modulation spectra, harmonic and
sideband changes, envelope discontinuities and formant movement. Inspect short
waveform/spectrogram excerpts around events. Measurements of uncertain/noisy F0
must not be presented as certain pitch errors.

Compare unmodified renders and separately labeled loudness-matched audition
copies using only a documented constant gain. Account for fixed delay without
warping time or hiding transition differences. An Antares/donor waveform null is
not expected and is not the sonic acceptance criterion: user listening approval
of the artifact character is required. Separate wanted character from accidental
clicks, dropouts, unstable output and unrelated defects.

## 3. Obtain free isolated singing first

User authorizes finding/downloading free vocal samples for comparison. Prefer
original dry, untuned single-singer recordings: no backing, doubles, reverb,
delay, previous pitch correction or source-separation artifacts. Do not use
commercial-song rips or assume that a free download grants redistribution.

Verified source lead:

- **VocalSet: A Singing Voice Dataset**, Julia Wilkins, Prem Seetharaman, Alison
  Wahl and Bryan Pardo, Northwestern University.
  <https://zenodo.org/records/1193957>, version 1.0,
  DOI `10.5281/zenodo.1193957`. The inspected page describes monophonic singing
  from 20 singers, vowels, scales, arpeggios, long tones and excerpts, and lists
  **CC BY 4.0**. Its `VocalSet.zip` is approximately 2.1 GB; the page lists archive
  MD5 `c44f60d34b8724b9a6f6d15e0a3158a9`. A newer version is advertised; pin the
  version actually acquired and do not silently substitute it.
- [ ] Obtain a small representative subset if possible; otherwise use a bounded
  archive download, keeping it outside tracked release/source assets.
- [ ] Verify file integrity, audio format, isolation and suitability. Record exact
  archive/member names, source URL, authors, license, acquisition date, hashes,
  excerpt boundaries and any resampling or other transformations.
- [ ] Include both lower/higher voices, ordinary phrases, slides and vibrato; do
  not rely exclusively on perfectly sustained professional vowels.
- [ ] If download/access fails, files are unsuitable, or they do not cover the
  desired contemporary hard-tune phrasing, **ask the user for dry singing WAVs**.
  Request original untreated vocals, not only already-tuned references. A dry/tuned
  pair plus Antares settings is useful if available.
- [ ] Keep user recordings private/local unless the user explicitly approves
  publication. Keep dataset license/attribution separate from plugin code terms.

No vocal audio has been downloaded, auditioned or processed as of this note.

## 4. Compare intact donors before committing to an algorithm

Candidate repositories, not endorsements:

| Candidate | Relevant inspected evidence | Unresolved |
| --- | --- | --- |
| [SnapTune](https://github.com/NicoFerni/snaptune) | `Source/dsp/AutoTuneEngine.h`: complete engine, retune/transition settings, onset/note-change handling, vibrato, Live delay-line and Studio TD-PSOLA paths | Actual artifact similarity, CPU, source pin/build and external-analysis seam |
| [Flayr Tune](https://github.com/FlayrLabs/flayr-tune) | `Source/TuneEngine.h`: framework-free engine, YIN, correction, PSOLA and Live delay-line/LPC paths | Sound/CPU; both synthesis paths and LPC run in the inspected process loop; measure rather than assuming cheap mode switching |
| [NoteForge](https://github.com/bighandfoot/NoteForge) | `Source/dsp/LiveTuner.cpp`: complete live detection/correction/shifting chain; `Engine.cpp` retains offline analysis | Hard-tune sound; offline stored analysis is not connected to Live; editor/audio-render cache is not this feature |
| [MXTune](https://github.com/liuanlin-mx/MXTune) | Existing archived HEAVEN plan explicitly calls for inspecting the complete donor | Full source/build/sonic comparison still required; the rejected detector-only hybrid is not intact MXTune |

Source inspection is not proof of sonic quality. SnapTune is a useful first
hard-tune audition candidate, not a selected winner. PSOLA versus a delay line
alone does not determine whether an engine sounds like the preferred Antares mode.

- [ ] Pin source commits, inspect complete DSP and licenses/dependencies, and
  build each shortlisted ORIGINAL plugin without sound-producing modifications.
- [ ] Render the same input corpus through each at documented hard settings,
  including distinct quality/latency modes where applicable.
- [ ] Produce a compact comparison with short labeled/blind audition clips,
  event-level observations, failures, latency and measured CPU. Reject an engine
  whose sound misses the goal; do not silently rewrite its internals to rescue it.
- [ ] Obtain user approval of the preferred artifact character before integration.

Important corrections to previous advice:

- Flayr's Graph track is **not detection reuse** in the inspected source:
  `analyse()` still calls `detector.detect()`, and `correct()` reads graph targets.
- SnapTune's inspected public interface does not expose external pitch analysis.
- NoteForge Live runs `analyseFrames()` on incoming audio. Its persisted offline
  `rawHz`/aperiodicity/notes demonstrate useful data storage, not a ready-made
  shared-analysis Live API.
- No inspected candidate is yet proven to meet the complete cache requirement
  without an adapter change. No Antares-equivalence or CPU-saving claim is earned.

## 5. Always-retained analysis: preserve sound while removing duplicate work

Conceptual path, not permission to redesign the donor's algorithm:

```text
Unknown audio -> original donor analysis -> retain timestamped results
Known audio   -> validated analysis lookup
                         |
                         +-> original target/correction + shifter A
                         +-> original target/correction + shifter B
                         +-> original target/correction + shifter C ...
```

Retain the original source pitch/period at the donor's native precision and
cadence, voiced/unvoiced decisions, timing/availability and other required
analysis outputs/state. Rounded MIDI notes or UI plot points are insufficient.
Keep editable target notes and per-tuner key/retune/transition settings separate.
Determine the correct boundary from the chosen donor's actual dependencies; do
not assume its detection, octave guards, pitch marks or adaptive windows are
independent of mode and settings.

- Share analysis only for identical source audio and compatible analysis settings.
  Different vocal takes need their own retained analysis. Different upstream FX,
  mode/range/tracking settings or analysis history may need separate cache entries.
- Record analysis configuration, engine revision, sample rate, channel policy,
  source identity/content validation, timestamps and context. Host position alone
  is not a clip identity and cannot establish cache validity.
- Unknown or changed material uses live detection and populates valid entries.
  Never use stale pitch just because playback is at a previously visited time.
  Define recapture/invalidation, seeks, loops, rerecording and tempo/stretch rules.
- Preserve causal availability: do not feed a first-pass estimate earlier on cached
  playback unless a separately approved behavior explicitly changes the sound.
- Prove how detector/filter history resumes across cached/uncached boundaries and
  how correction state warms up on seeks; frames alone may not be sufficient.
- Bound memory and make persistence explicit. Always-retained analysis is the
  normal behavior, not a manual capture/cache mode; limits must be visible rather
  than silently discarding valid material. Analysis belongs to session data, not
  automatically to shareable factory presets.
- No callback allocation, locking, disk I/O or GUI access. Publish immutable data
  through bounded lock-free ownership and retire it off the audio thread.
- Separate instances, particularly Bitwig-sandboxed processes, do not automatically
  share memory. Do not promise cross-instance reuse before choosing and validating
  its identity, lifetime, synchronization and isolation model.

## 6. Prove five-tuner CPU benefit

Measure both identical-source and different-take scenarios. Report analysis,
correction/shifting, lookup/validation and total costs separately, with memory
and worst-case callback time as well as average CPU.

Compare: one uncached tuner; five independent tuners; five tuners sharing first-
pass analysis where valid; and five tuners on retained-analysis playback. If five
outputs truly have identical settings/state/input, processing once and fanning out
may suffice; do not mistake that trivial case for five independently tunable paths.

Expected cost structure (not measured savings):

- Independent: `5 * detection + 5 * correction/shifting`.
- Shared first pass: `detection + 5 * correction/shifting + sharing overhead`.
- Valid retained playback: `lookup/validation + 5 * correction/shifting`.

Caching detection does not remove resynthesis or all waveform-dependent analysis.
Do not predict an arbitrary percentage before profiling. Use supported rates,
mono/stereo, regular/irregular small blocks, automation, silence and seeks.

## 7. HEAVEN integration and acceptance

Reuse the archived donor-output-equivalence principle, **not the old hybrid or
its whole branch**. Current `plan.md` adaptive processing/latency policy takes
precedence over the archived inactive-latency reservation policy.

- [ ] Original donor versus thin adapter: waveform/null equivalence under the
  same input, controls, initialization, timing, automation and block delivery.
- [ ] Detector-driven versus retained/shared-analysis path: equivalent output,
  including cache misses, resets, repeated playback and boundary transitions.
  Investigate any mismatch rather than normalizing or time-warping it away.
- [ ] Neutral HEAVEN/tuning-only output matches the selected original donor after
  documented fixed-delay alignment. Preserve source notices and exact pins.
- [ ] Preserve aligned clean/tuned sources, shared Module Mix and individual
  Lead/generated-voice Tune Mix where integrating that design. Do not replace the
  whole input with tuned audio and lose those independent choices. Five distinct
  tuner settings require distinct correction/shifting paths, not just Tune Mix.
- [ ] Incorporate actual requirements into adaptive CLAP/VST3 activation. No
  inactive latency reserve by default; commit path/latency together when the host
  completes reactivation. Keep bypass, dry/wet, branches and sidechain aligned.
- [ ] Preserve parameter IDs/order, state compatibility, routing and downstream
  automation. Review old tuning-state migration explicitly.
- [ ] Run actual CLAP/VST3 and Bitwig workflow tests; distinguish offline harness
  evidence from Bitwig verification and user listening approval.

All Windows configure/build/test/benchmark commands must run through
`tools/Invoke-HalfCpu.ps1` in a dedicated 64-bit PowerShell process, per
[resource limits](docs/Windows-Resource-Limits.md). Native GUI work must follow
`AGENTS.md` display enumeration and non-primary-monitor placement requirements.
Do not terminate Bitwig, overwrite loaded plugins or disturb unrelated work.

## 8. Evidence status and immediate next work

Completed: requirements clarified; selected donor source paths inspected;
installed Antares paths located; VocalSet dataset description/license inspected;
research plan recorded.

Not completed: sample acquisition/audio inspection; Antares hosting/rendering;
original donor builds; comparative listening/measurements; donor selection;
retained-analysis adapter; five-way benchmarks; implementation or release.

Next: obtain suitable dry vocals, establish the exact Antares reference and render
intact donors. Produce evidence and let artifact character decide the donor.
Do not implement a new sonic algorithm or a corrected-audio cache in the meantime.
