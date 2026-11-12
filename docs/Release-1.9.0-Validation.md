# Release 1.9.0 — validation record

## Release status

Probe implemented in `source/tests/ReleasePerformanceProbe.cpp`. Isolated MSVC
x64 Release compilation and static linking succeeded. After explicit coordinator
quiet-lane confirmation, the full **32-case matrix completed on 2026-11-12 in
25.94 seconds, exit 0, empty stderr**. Active eight-voice/four-LFO/grain-bridge
fixture assertions and per-sample finite-output checks completed successfully.

**Performance is not certified:** the observed heavy workload exceeds nominal
block durations in many cases. The 96 kHz serial pair averages approximately
1.85–2.09 times the block deadline; even one instance shows substantial p95/max
spikes. See actual measurements and limitations below. Successful execution is
not a CPU acceptance pass.

Final coordinator-reported integration results:

- **Release CLAP/VST3 build passed. All 34 focused CTests passed in 146.69 s**,
  including **eight factory-audio shards and 63 editor groups**. Separate legacy
  DSP passed in **81.8347 s**. The earlier 26-suite baseline is superseded by this
  complete final run, not extrapolated from scratch validation.
- **33 screenshots plus 12 rack PNGs**, across Device/Neon/Spectral, were rendered
  and visually inspected. A subsequent test-only screenshot-output addition passed
  the **GrainSnapshotUi rerun in 0.60 s**, producing **three actual-grain PNGs**.
  Root `VocalStack-Granular.png` now shows the actual DSP snapshot;
  `VocalStack-Stack-FX.png` shows the same-node rack. This is not DAW audition.
- Shared Undo restores lifecycle parameter/modulation state while merging live
  unrelated mappings/sources. All removed effect cache IDs are purged to prevent
  old tails on immediate re-add; surviving identities retain history.
- Release artifacts were copied to tracked packages and **SHA256-matched**.
  **Packaged CLAP smoke passed:** 2087 parameters, mono/stereo latency, bypass and
  automation. The release is **built, packaged and validated**, not awaiting a
  build, full-suite run, package copy or packaged smoke result.

### Package identities and installation outcome

| Tracked package binary | SHA256 (matches Release artifact) |
| --- | --- |
| `Windows-x64/CLAP/Vocal Stack.clap` | `f84f10ddb689ffe30322a04894875514ab77b4996ab2e27de0107730ca4aba1a` |
| `Windows-x64/VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3` | `37d9f1e13eda36b1ff1fa24a26a09993c4e420f3d42964b231fa1c54c25d08ac` |

**Initial install attempt (historical):** the installed CLAP was locked by
another process. That attempt changed no installed files and left 1.8.0 installed.

**Installation retry COMPLETE:** after the user closed Bitwig, the coordinator
reran `source/build/install-release.ps1` and observed both per-user CLAP and VST3
report **Installed** and **SHA256 verified**. Version 1.9.0 is now installed in
both per-user formats; the package/Release hashes above are unchanged. Never
terminate Bitwig or overwrite a loaded plugin to force an update.

Release commit `531045b` is pushed to `origin/main`.

**Still unchecked:** actual Bitwig listening/transport, host automation/session
recall and sustained playback/dropouts. Packaged smoke and successful installation
are not an actual Bitwig session.

## Factory preset headroom and reproducibility

Evidence: `source/build/factory-preset-audio/REPORT.md`, `measurements.csv` and
`preset-0.csv` through `preset-7.csv` in the same ignored scratch directory.
The report's old registration-pending note is historical: production CMake now
registers `VocalStackFactoryPresetAudio0` through `VocalStackFactoryPresetAudio7`
using `VocalStackFactoryPresetAudioTests` with factory indices 0..7.

**48 cases / 96 fresh renders passed**: eight real factory presets loaded through
PresetManager/Processor, 44.1/48/96 kHz, mono/stereo, two renders per case. Scratch
8/8 shard rerun passed in **109.14 s** (initial complete measurement run 109.257 s).
These scratch results preceded the final production run; all eight registered
shards also passed within the completed 34-suite run (146.69 s total).

Fixture: 0.2-amplitude 173/1091 Hz voiced transient train, 370 ms recurrence,
exponential envelope/smooth attack; stereo right 0.8 scale and 0.23 rad upper-
partial offset. No peak/RMS normalization. Five seconds excitation, 0.25 s silent
preroll, 15 s silent tail; irregular blocks 1/17/64/137/257/31/113, prepared max
257. Latency-aligned windows use 1344 samples at 44.1/48 kHz and 2688 at 96 kHz.
Input sample peak: -12.881 dBFS; pooled RMS: -30.196 to -29.334 dBFS.

Every sample was finite, with **zero samples at/above 0 dBFS**. Active audio and
delay/reverb tails were non-silent; final tail-second RMS was lower than first
in every case. Fresh runs repeated exact sample hashes, accumulated output energy
and peaks. Across the six rate/layout combinations per preset (rounded dBFS):

| Preset | Max output sample peak | Active RMS range | Max tail sample peak |
| --- | ---: | ---: | ---: |
| Dark Lead | -16.313 | -34.058 to -33.093 | -38.857 |
| Grit Adlibs | -16.327 | -33.956 to -32.773 | -38.881 |
| Deep Double | -19.931 | -37.162 to -36.321 | -41.204 |
| Night Echo | -19.066 | -36.475 to -35.467 | -31.445 |
| Cloud Choir | -24.479 | -41.296 to -40.718 | -35.926 |
| Clean Vocal Utility | -18.181 | -35.773 to -34.828 | -40.086 |
| Vocal Filter Motion | -24.090 | -42.861 to -41.897 | -43.888 |
| Shimmer Bloom | -24.170 | -41.765 to -40.548 | -44.254 |

Final-second RMS was at most -201.309 dBFS; most cases ended at digital zero.
CSV -600 dBFS is the display floor (1e-30 amplitude), not analog silence.
**No preset trim changes are evidenced:** retain -3 dB trims and Shimmer Bloom's
-6 dB. Reverb's 4x calibration affects only its wet branch, not whole-preset gain;
do not divide whole renders by four or normalize away the evidence. This modest
synthetic input does not certify hot/arbitrary-input or true-peak headroom,
perceptual loudness or subjective musical quality. Actual listening remains open.

## Editor/UI profile

`VocalStackEditorTests.exe --profile-routing`: median/p95/max **7.990 / 12.903 /
41.000 ms compact**, **13.448 / 20.044 / 44.231 ms expanded**, at most **12 mounted
cards**. These message-thread measurements are not audio callback ratios, native
presentation FPS or a glitch-free guarantee.

## Why this probe

Existing helpers inspected:

- `EffectsBenchmark.cpp`: isolated harmony and ten-FX routing timings, best of
  three; its routing sources are synthetic rather than eight real pitch engines.
- `DspTests.cpp`: default stereo engine average at 48 kHz / 256 samples.
- `DspAuditTests.cpp`: per-effect mean/max, including a grain snapshot copy;
  not a full processor or multi-instance deadline distribution.
- `ModulationProcessorTests.cpp`: actual processor modulation API/recall usage.
- `GrainSnapshotTests.cpp` / `GrainSnapshotUiTests.cpp`: real grains, processor
  forwarding and bridge ownership, not a full-processor timing distribution.

The remaining question is observed expensive-block cost through the full
Release processor at small and irregular host blocks, with all eight pitch
paths, ten FX, and four active LFOs, including actual snapshot publication.
This deliberately heavy fixture is **not proof of the global worst preset**.

## Workload and matrix

32 cases: 48/96 kHz × one/two instances × four block patterns × two reader modes.
All cases are stereo, with continuously generated deterministic non-silent
multi-tone input and no host bypass.

- Regular blocks: **32, 64, 137** samples, independently measured.
- Irregular repeating sequence: **32, 137, 1, 64, 257, 7, 31, 128, 33, 63**.
  Includes aggregate statistics and separate rows for each actual block size;
  large blocks must not hide one-sample deadline pressure.
- Advanced voices: all eight present/enabled, pitches
  **−12, −9, −7, −4, +3, +5, +7, +12 st**, each −12 dB, alternating ±3 st
  formants. Doubler on, wet mix 75%, global mix 100%, formants on.
- Ten serial FX after the main voice mix: delay, reverb, compressor, bitcrusher,
  granular, XP filter, synth filter, gate, stereo, gain. Mix 60%; reverb pitch
  amount 65% and swell 250 ms; granular pitch −12 st; synth resonance 35%,
  drive 6 dB. Other settings retain module defaults. No legacy Chorale node.
- Four free-running LFOs at 0.37, 0.98, 1.59, 2.20 Hz, with distinct initial
  phases, eight voice-pan assignments and ten FX-mix assignments (18 total).
- Two instances are independent processors and independent buffers, processed
  serially in one timed section against **one** block deadline. Their outputs
  are not cascaded. This is conservative serial scheduling, not a measurement
  of DAW parallel instance scheduling or a second audio device.
- No editor window is created and no message loop is pumped during measurements.
  This isolates processor and explicit snapshot API work, not GUI rendering.

### Actual grain bridge and snapshot overhead

The probe links the existing Release processor library and calls
`Processor::processBlock`; it does not substitute a JUCE-independent engine,
compile out the bridge, or disable publication for an apparent speedup.
`Processor::render` unconditionally publishes `engine.grainSnapshot()` through
`GrainSnapshotBridge` after each callback, alongside voice/lead/meter publication.
These costs are included in every callback result. The fixture requires an
actual active grain cloud from processor slot 5 / node 104 after warm-up, four
published LFOs, and eight active pitched voice snapshots. A stale or wrong
binary must not silently yield a claimed successful workload.

Reader modes:

1. `noReaders`: full production publication, no explicit display reads during
   the measured loop.
2. `read30Hz`: additionally read the actual granular frame, eight voice frames,
   lead frame and modulation snapshot at approximately 30 Hz of processed audio
   time. Record this API batch separately as `snapshot_read`.

Readers are synchronous **outside** the timed callbacks. Thus the snapshot-read
row quantifies incremental UI-side copy/materialization API cost (including
modulation snapshot allocations), not callback cost or contention. No claim is
made that subtracting the two callback modes isolates publisher overhead: both
include the release publisher, and scheduling noise can dominate the difference.
There is no disabled-publisher baseline. Simultaneous GUI contention, FX monitor
FFT subscriptions, rendering and host automation storms are not covered.

## Sampling, budgets, and interpretation

Each case warms 0.15 audio seconds at 257 samples before timing. Construction,
parameter/graph/LFO setup, input generation, buffer resizing, output scanning,
readers, sorting and console output are outside the callback timing interval.
Warm-up exercises pitch-engine history; this is not a cold-start/graph-swap test.

A case targets 0.75 audio seconds and at least 256 callbacks, stopping sooner
at a 0.7-second measured-loop wall budget, or at 8192 callbacks. `capped=1`
identifies the wall-budget stop; always retain actual `count`, especially for
irregular per-size and reader rows. There is no statistically meaningful
universal p95 sample-count guarantee. A 55-second soft whole-matrix budget is
checked between cases; exit 3 explicitly marks an incomplete matrix. Setup,
warm-up, destruction and one non-interruptible callback can overrun soft budgets.
Use a roughly 60-second external timeout if required, recording truncation
rather than presenting a partial matrix as complete. The authorized run took
25.94 seconds including process startup, fixture setup, warm-up and teardown.

CSV rows report arithmetic mean, nearest-rank p95 and maximum wall microseconds,
and the same statistics for each observation's deadline ratio:

`deadline_ratio = callback_seconds / (actual_block_samples / sample_rate)`

For the irregular aggregate, ratios are normalized **before** calculating the
statistics; it is not a ratio of average durations. For two instances, the
numerator is the total serial pair callback time. `snapshot_read` uses the
coincident block deadline for scale only; it is **not** audio-thread utilization.
The checksum consumes output and snapshots; every output sample must be finite.
Clock calls and timer granularity are included, not subtracted.

Ratios are informational observations, not a guarantee, schedulability proof,
or an arbitrary pass/fail threshold. A ratio above 1 means that timed section
exceeded that block's nominal duration in this run. A ratio below 1 does not
prove reliable real-time operation. Maxima are sensitive to preemption, power
management, CPU temperature, background work and OS scheduling. Short offline
runs do not replace real-host sustained playback/listening and dropout checks.
Nonzero exit codes indicate fixture/output errors, bad arguments, or an
incomplete matrix, not a machine-dependent CPU acceptance threshold.

## Registered performance target and reproduction

`source/CMakeLists.txt` now defines **`VocalStackReleasePerformanceProbe`** as an
informational executable, not a correctness CTest or CPU pass/fail gate. With
plugin/tests enabled, build that target from a completed matching Release tree;
run `VocalStackReleasePerformanceProbe.exe --quiet-lane-confirmed` only in an
explicitly coordinated quiet timing lane. No argument performs readiness only.
Record source/archive identities, machine/power state, raw CSV, caps and counts.
Do not silently relabel old archive measurements as a run of a new rebuild.

The authorized results below used a fresh isolated `/MT /O2 /Ob2 /DNDEBUG` MSVC
C++20 probe linked to matching existing Release shared-code, DSP and CLAP support
libraries, with artifacts in the recorded OS temporary directory. The registered
target replaces the ad hoc Ninja-command extraction/temporary-link recipe as the
maintained build route; its existence does not imply an additional timing run.

## Authorized run: actual results

### Environment and evidence identity

- Coordinator explicitly confirmed that other agents had finished builds/timings
  and that no shared build or tests would run during this lane. No shared build
  or test suite was invoked by this task.
- Run start: **2026-11-12 12:25:47.953**, machine-local time.
- CPU: **AMD Ryzen 5 5600G with Radeon Graphics**, 6 cores / 12 logical processors.
- OS: **Windows 11 Pro, 10.0.26300, build 26300**.
- Active power scheme: **High performance**
  (`8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c`), queried immediately after the run.
- Compiler/toolchain: MSVC x64, Visual Studio Build Tools **18.9.1**,
  toolset **14.51.36231**; fresh C++20 probe compiled `/MT /O2 /Ob2 /DNDEBUG`.
- Repository HEAD: `6833ac60718bc3cf30d626edfd47f6e373707cc0`, branch `main`,
  **dirty multi-agent working tree**. This is not an unmodified-HEAD binary
  measurement; archive hashes below identify the linked Release inputs.
- Command: `ReleasePerformanceProbe.exe --quiet-lane-confirmed`.
- Result: **exit 0**, **25.93956 s** external elapsed time, **empty stderr**.
- CSV contains **32 callback aggregate rows, 80 irregular per-size rows, and
  16 snapshot-reader rows**. All checksums are finite. No case was skipped by
  the whole-matrix budget; **23/32 cases hit the per-case 0.7 s wall cap**.

Artifacts remain outside the worktree at:
`C:/Users/tkach/AppData/Local/Temp/vocalstack-release-perf-c1toemag/`:
`ReleasePerformanceProbe.exe`, `results.csv`, `results.stderr.txt`, and
`run-metadata.json`. This temporary location is not durable release storage;
archive it before temporary-file cleanup if raw evidence is needed later.
The summary tables below preserve all 32 case aggregates in this document.

SHA-256 identities:

| Input/evidence | Last modification (local, where recorded) | SHA-256 |
| --- | --- | --- |
| `source/build/VocalStack_artefacts/Release/Vocal Stack_SharedCode.lib` | 2026-11-12 12:04:28.299938 | `9adf25c830a21dff0ab8f58e88022714ce68f58ac5b7bbce2c0396b1dafaefd1` |
| `source/build/vocalstack_dsp.lib` | 2026-11-12 11:38:26.000621 | `fe1998d93f947ba6139b271d2291505a8211af9b86dc8bb130fc7346ae27915d` |
| `source/build/_deps/clap_juce-build/clap_juce_extensions.lib` | 2026-11-09 14:11:21.453871 | `879b2538d8a28ab5b25509a1e18737c027afd1d6c7034640a4f00c08362e1424` |
| Isolated executable | — | `4baf324f69d67cf4dc1726daafe820501a54770789cc371f87c7577dd79dda9b` |
| Probe source | — | `b11e6f5cbc7b27d8225b69c3644bd8caba4507bdecbf07632c6d52cc2a00ebd8` |
| Raw `results.csv` | — | `06717765b2b8289d7907ae32da29d0f840ddad1ccc82563fee427ac6caaea43e` |

### Full-processor callback deadline ratios

Dimensionless ratios, rounded to three decimals; **1.000 means the nominal
block duration**, not an arbitrary acceptance threshold. Both reader modes
include actual Release grain publication in the callback. Each two-instance
row is the total serial pair against one deadline. `N` counts callback groups,
not individual processor calls. Irregular aggregate statistics normalize each
observation by its own actual block size.

| Hz | Instances | Block pattern | Reader mode | N | Mean ratio | p95 ratio | Max ratio |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 48000 | 1 | 32 | noReaders | 1125 | 0.467 | 1.290 | 2.283 |
| 48000 | 1 | 32 | read30Hz | 1125 | 0.461 | 1.208 | 3.334 |
| 48000 | 1 | 64 | noReaders | 563 | 0.451 | 1.009 | 1.405 |
| 48000 | 1 | 64 | read30Hz | 563 | 0.480 | 1.031 | 2.105 |
| 48000 | 1 | 137 | noReaders | 263 | 0.469 | 0.746 | 1.107 |
| 48000 | 1 | 137 | read30Hz | 263 | 0.469 | 0.737 | 0.943 |
| 48000 | 1 | irregular | noReaders | 478 | 0.487 | 1.032 | 5.321 |
| 48000 | 1 | irregular | read30Hz | 478 | 0.488 | 1.056 | 3.718 |
| 48000 | 2 | 32 | noReaders | 1072 | 0.977 | 2.591 | 5.144 |
| 48000 | 2 | 32 | read30Hz | 1072 | 0.955 | 2.574 | 5.634 |
| 48000 | 2 | 64 | noReaders | 539 | 0.972 | 2.119 | 3.331 |
| 48000 | 2 | 64 | read30Hz | 528 | 0.966 | 2.090 | 2.667 |
| 48000 | 2 | 137 | noReaders | 257 | 0.954 | 1.613 | 1.898 |
| 48000 | 2 | 137 | read30Hz | 256 | 0.935 | 1.479 | 1.781 |
| 48000 | 2 | irregular | noReaders | 478 | 1.034 | 2.306 | 11.120 |
| 48000 | 2 | irregular | read30Hz | 455 | 1.085 | 2.491 | 8.379 |
| 96000 | 1 | 32 | noReaders | 2138 | 0.979 | 4.156 | 10.698 |
| 96000 | 1 | 32 | read30Hz | 2106 | 0.979 | 4.198 | 9.250 |
| 96000 | 1 | 64 | noReaders | 1095 | 0.956 | 2.563 | 5.979 |
| 96000 | 1 | 64 | read30Hz | 1072 | 0.963 | 2.577 | 6.472 |
| 96000 | 1 | 137 | noReaders | 500 | 0.978 | 2.079 | 2.703 |
| 96000 | 1 | 137 | read30Hz | 512 | 0.942 | 2.018 | 2.714 |
| 96000 | 1 | irregular | noReaders | 938 | 1.009 | 3.436 | 15.430 |
| 96000 | 1 | irregular | read30Hz | 945 | 0.997 | 3.469 | 20.290 |
| 96000 | 2 | 32 | noReaders | 1043 | 2.013 | 8.586 | 22.124 |
| 96000 | 2 | 32 | read30Hz | 1034 | 1.994 | 8.518 | 18.063 |
| 96000 | 2 | 64 | noReaders | 537 | 1.959 | 5.358 | 9.503 |
| 96000 | 2 | 64 | read30Hz | 524 | 1.974 | 5.383 | 10.232 |
| 96000 | 2 | 137 | noReaders | 254 | 1.931 | 4.110 | 5.919 |
| 96000 | 2 | 137 | read30Hz | 261 | 1.852 | 4.095 | 5.055 |
| 96000 | 2 | irregular | noReaders | 468 | 2.090 | 7.013 | 38.216 |
| 96000 | 2 | irregular | read30Hz | 468 | 2.069 | 6.976 | 29.099 |

Key observations:

- At 48 kHz one instance averages 0.451–0.488× deadline, but 32/64-sample
  p95 values and most maxima exceed 1×. Average headroom does not eliminate
  expensive blocks.
- At 48 kHz the serial pair averages 0.935–1.085×, leaving little or negative
  average budget; regular-block p95 is 1.479–2.591×.
- At 96 kHz one instance averages 0.942–1.009×; regular-block p95 is
  2.018–4.198×. The serial pair averages 1.852–2.090×, so the observed problem
  is not limited to rare maximum outliers.
- The largest observed ratio is **38.2162286×**, at **96 kHz, two instances,
  irregular, noReaders, actual block 7 samples**: **2786.6 µs** against a
  **72.9167 µs** deadline. That size bucket has only **47 observations**;
  its mean/p95/max are **2.062 / 2.445 / 38.216×**. All irregular per-size
  buckets have only 45–95 observations; treat their tails cautiously.
- The largest raw callback duration is a different case: **8446.7 µs**, at
  **96 kHz, two instances, regular 137, noReaders**, ratio **5.91885547×**.
  Ranking raw durations alone would miss the worst normalized small block.

### Incremental snapshot-reader API cost

These are **milliseconds per read batch outside the callback**, covering all
instances in that row. Modes always have full publisher cost inside callbacks;
this is not a measured publisher-only delta. `N` is deliberately exposed because
only 11–23 batches were sampled; with 11/12 observations nearest-rank p95 is the
maximum. These short-run reader tails must not be treated as stable UI budgets.

| Hz | Instances | Block pattern | N | Mean ms | p95 ms | Max ms |
| --- | --- | --- | --- | --- | --- | --- |
| 48000 | 1 | 32 | 23 | 0.391 | 0.495 | 0.644 |
| 48000 | 1 | 64 | 23 | 0.462 | 0.783 | 0.789 |
| 48000 | 1 | 137 | 23 | 0.415 | 0.561 | 0.851 |
| 48000 | 1 | irregular | 23 | 0.412 | 0.447 | 0.734 |
| 48000 | 2 | 32 | 22 | 0.703 | 0.859 | 0.987 |
| 48000 | 2 | 64 | 22 | 0.834 | 1.112 | 1.557 |
| 48000 | 2 | 137 | 22 | 0.748 | 0.854 | 1.046 |
| 48000 | 2 | irregular | 22 | 0.865 | 1.214 | 1.312 |
| 96000 | 1 | 32 | 22 | 0.464 | 0.680 | 0.719 |
| 96000 | 1 | 64 | 22 | 0.425 | 0.574 | 0.676 |
| 96000 | 1 | 137 | 22 | 0.432 | 0.548 | 0.799 |
| 96000 | 1 | irregular | 23 | 0.398 | 0.431 | 0.739 |
| 96000 | 2 | 32 | 11 | 0.937 | 1.611 | 1.611 |
| 96000 | 2 | 64 | 11 | 0.857 | 1.322 | 1.322 |
| 96000 | 2 | 137 | 12 | 0.743 | 0.873 | 0.873 |
| 96000 | 2 | irregular | 11 | 0.696 | 0.809 | 0.809 |

One-instance mean reader cost is **0.391–0.464 ms/batch**; serial two-instance
mean is **0.696–0.937 ms/batch**, with a maximum of **1.611 ms**. These batches
include `getModulationSnapshot()` materialization, not just the granular bridge
copy. Reader/no-reader callback differences cannot be interpreted causally from
one sequential run. Reader cadence is 30 Hz of simulated audio time, not paced
wall time; overloaded cases process less audio before the wall cap.

### Limitations and remaining release checks

- **Single short run, fixed case order**, no repeats or confidence intervals;
  later cases may have different thermal/cache/scheduler conditions. Quiet-lane
  coordination avoids known concurrent work, not OS preemption. No affinity,
  real-time thread priority, hardware counters, or scheduler trace was used.
  The measurement cannot attribute the spikes to a particular DSP stage or OS
  event. Do not dismiss the average overload as mere scheduler noise either.
- All matrix cases executed, but **23 were time-capped**: coverage completion
  is not completion of the target audio duration for each case. The 96 kHz
  serial pair / 137 / noReaders case has only 254 callbacks, below the intended
  256 minimum, because the wall cap takes precedence. Reader and irregular
  bucket sample counts are particularly small.
- Actual eight pitched voices, four LFOs and a nonempty grain cloud were checked
  after warm-up; output was checked for finiteness, **not sonic correctness,
  distortion, musical quality, or sustained audibility**. No new correctness
  suite was rerun in this lane.
- This is full static-linked processor DSP/bridge execution, **not a CLAP/VST3
  host callback**, GUI paint, simultaneous snapshot-reader contention, concurrent
  DAW scheduling, or a global worst-preset search. No disabled-publication
  baseline or separate publisher-only overhead was measured.
- These measurements do **not support a blanket heavy-workload small-buffer
  real-time claim** on this machine. In particular the serial pair at 96 kHz
  is over budget on average. This is a reported observation, not a new arbitrary
  release threshold or an assertion about all hosts/CPUs.
- Before claiming real-time readiness, separately validate sustained playback
  and actual dropouts/listening in the target host, with the release grain UI
  visible, recording sample rate, host buffer, instance scheduling and workload.
  Further profiling/optimization and longer targeted quiet-lane measurements
  are warranted for this exact heavy fixture. They are not implemented here.
- Raw artifacts are in an OS temporary directory; preserve them externally if
  needed. During this isolated probe, no packaged binary was changed and no shared
  build was mutated. Release commit `531045b` is pushed to `origin/main`.

### Release evidence boundary

The final 34-suite run (146.69 s), separate legacy DSP pass (81.8347 s),
render review, three additional actual-grain captures/0.60 s UI rerun, factory
fixture, package hashes and packaged CLAP smoke are complete as recorded above.
Performance tables retain their original evidence identities and caveats; they
are not silently relabeled as a fresh benchmark of the packaged artifacts.
Installation is complete: after the user closed Bitwig, the coordinator reran
`source/build/install-release.ps1`; both per-user CLAP and VST3 reported Installed
and SHA256 verified. Release commit `531045b` is pushed to `origin/main`.
Actual Bitwig listening/transport and other host checks remain unperformed.
