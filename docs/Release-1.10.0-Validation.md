# Vocal Stack 1.10.0 — approved delivery validation

The user explicitly approved installation and delivery of the entire pending
1.10.0 preview. Both tracked Windows x64 formats were refreshed from the final
successful Release build, and both per-user installations succeeded. The older
preview documents retain historical observations; their approval gates are no
longer current. SIDECHAIN / Spectral Morph remains **roadmap only, unimplemented**.

## Delivered scope

- Independent Convolution with original synthetic Church/Hall/Room/Plate spaces,
  Brightness, PreDelay, Tail, Duck, Output and Mix; focused 2x IIR + ADAA Saturator.
  Existing algorithmic Reverb and Delay feedback saturation are retained.
- Actual audio-reactive factory-FX telemetry/visuals, with honest inactive/model
  labels; no claim of musical quality from synthetic test signals.
- Coordinated linear global Motion with 3% base, clustered textures and glints,
  0.8 background strength, square chrome with circular knobs, fat 3 px slider
  material, 10 px voice rails, sparse FX controls and context-themed presets.
- Repeated/duplicated Dry/Wet nodes, shared inspector layout, 14/22 px scrollbars,
  chunky square Routing nodes with double-weight connections, and shared Stack
  layout constants. Approved source work and tests were preserved.
- **2252 host parameters:** original 2087 indices retained; 165 controls appended
  at 2087–2251. Motion remains presentation state, not a new host parameter.
- Both PE file/product versions now report **1.10.0**. JUCE 8.0.12's generated
  Windows resource rule lacked its Info.txt input dependency, retaining old
  1.0.2 metadata. Project CMake now adds that dependency and links the existing
  resource into CLAP too. CLAP smoke asserts descriptor version against CMake's
  release version. No third-party dependency source was patched for this repair.

## Build and tests actually observed

All configure/build/compiler/test/smoke/render processes ran through
`tools/Invoke-HalfCpu.ps1` in dedicated x64 PowerShell processes: **6/12 logical
processors, affinity 0x3F**. Incremental Release builds regenerated metadata and
completed successfully; existing compiled source objects were reused where
current. Final build log: `source/build/delivery-1100-build-complete.log`.

Every one of the 38 registered suites passed across the following split runs:

| Validation | Observed result | Local ignored log |
| --- | --- | --- |
| Final non-editor/non-grain-UI CTests | **36/36 passed, 130.24 s** | `source/build/delivery-1100-36-final.log` |
| Isolated full editor CTest | **Passed, 116.89 s; 75 registered groups** | `source/build/delivery-1100-editor.log` |
| Isolated grain UI, repeat until-fail:3 | **3/3 passed, 27.05 s total** | `source/build/delivery-1100-grain-isolated.log` |

**Not a clean combined run:** two earlier parallel non-editor runs each passed
36/37 and failed only `VocalStackGrainSnapshotUi` at `concurrent readers never
observe torn plain payloads` (132.10 s and 130.41 s). The assertion combines a
positive read-count requirement with payload integrity, so this output alone does
not establish which condition failed. Isolated passes do not prove the concurrency
issue fixed. Its implementation/assertion was not weakened or changed for delivery.
Logs: `delivery-1100-noneditor{,-final}.log` under `source/build/`. The separately
recorded older Chorale editor timing assertion is not claimed fixed either.

Coverage includes legacy DSP and pinned baseline, Creative/utility/waveshaper
DSP, FX automation and routing, state/modulation, rack UI, all ten factory-audio
shards, and the new Factory FX tests. New-effect checks cover FIR-oracle agreement
for four synthetic spaces at 8/44.1/48/96/192/384 kHz, saturator comparison to the
existing warm curve, finite output, silence/reset, irregular processing, graph
handoff, telemetry and **zero counted audio-thread allocations/destructions**.
Editor coverage includes append-only parameter mapping, binary/text state,
mono/stereo processing, latency-aligned bypass, all-theme controls and the approved
Routing/scrollbar/layout work. These are automated checks, not actual DAW listening.

## Render validation

All seven modes passed: `--render-factoryfx` (24 PNGs), `--render-coordinated`
(231), `--render-drywet` (15), `--render-inspector-controls` (84),
`--render-scrollbars` (12), `--render-routing-chunky` (24), and broad `--render`
(51): **441 offscreen JUCE PNGs**, not physical framebuffer screenshots.
Outputs/logs: `source/build/delivery-1100-renders/<mode>/`.
Neon Saturator controls and Spectral expanded dense Routing were visually reviewed.
Historical before captures were not replaced.

The Routing fixture freshly enumerated primary work area `(0,0,3440,1392)` and
non-primary work area `(3440,357,1920,1032)`, both 96 DPI / scale 1, and logged
native placement on the non-primary display. This is fixture-specific evidence,
not a claim that every legacy peer or popup was verified on that display. Pure
offscreen renders have no physical monitor placement. No user's host/unrelated
window was moved; no actual DAW/native-FPS or comprehensive popup-placement result
is claimed.

## Packaging, installation and smoke

The tracked `Windows-x64/CLAP/Vocal Stack.clap` and VST3 bundle binary match the
final Release artifacts. `Windows-x64` also carries the project license and
third-party notices/source provenance, including Pixelify Sans SIL OFL 1.1; the
font remains embedded. Original root legal records remain intact.

Packaged CLAP smoke passed **before installation**. The existing ignored
`source/build/install-release.ps1` then preflighted both installed binary paths
with exclusive read/write access before either replacement. It reported
**Installed and SHA256 verified** for both. No host was killed, no lock was
forced, and no installed binary lock blocked the attempt. Legal notices were
also delivered beside CLAP and inside VST3's Resources/Licenses directory.

Installed CLAP smoke subsequently passed. Both smoke runs verified descriptor
**1.10.0**, **2252 automatable parameters**, remote controls, state, automation,
bypass and layouts: stereo 48 kHz / 1344 samples, mono-to-stereo 44.1 kHz / 1344
samples, mono 96 kHz / 2688 samples. Logs:
`source/build/delivery-1100-{package,installed}-smoke.log`.

Independent SHA256 and PE checks confirmed **build = tracked package = installed**
for each format, file/product version **1.10.0** on all six binaries:

| Format | SHA256 |
| --- | --- |
| CLAP | `41DA7CFA95B68B2B6D3ED1289B797999056C84637882F9E85B71ED00F871658A` |
| VST3 | `FD71879DA9D21215C201885A26B6A77309EEE69BE29EA1E775F327904657FEA1` |

Installed binary paths:

- `C:\Users\tkach\AppData\Local\Programs\Common\CLAP\Vocal Stack.clap`
- `C:\Users\tkach\AppData\Local\Programs\Common\VST3\Vocal Stack.vst3\Contents\x86_64-win\Vocal Stack.vst3`

Local verification details: `source/build/delivery-1100-hashes.json`. Machine-local
build/install/verification helpers and generated captures/logs are excluded from
Git; generated root review images are retained locally and ignored, not deleted.

## Remaining limits

- The parallel grain UI assertion remains a reproducible validation caveat;
  every suite passed separately, not together in a clean concurrent run.
- No independent VST3 host/validator smoke was available/run. VST3 was built,
  installed and binary-hash/PE-version verified; CLAP smoke is not VST3 validation.
- No actual Bitwig listening, transport, automation recording, DAW recall,
  native FPS, comprehensive accessibility or cross-platform validation is claimed.
- No external IR workflow or Sidechain Spectral Morph implementation was added.

`git diff --check` passed (only the existing PluginProcessor.cpp line-ending
advisory). Delivery uses the existing `main` branch, with no force-push; the
release commit hash and final push/status are reported in the delivery response.
