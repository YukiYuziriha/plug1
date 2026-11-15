# Vocal Stack 1.11.1 release validation

## Changes

- One shared Compressor plot combines its transfer curve, measured input/wet history and measured gain-reduction history; its numeric compression meter remains.
- Larger small graph/technical labels, pixel-font consistency and reserved Morph axis rows improve narrow-panel readability without shrinking knobs.
- The README showcases the real-time Motion 0%, default 3%, and 100% comparison.
- No new host parameters, sound algorithms, state-format or latency-policy changes. Compressor wet telemetry now forwards already-computed samples; monitored and unmonitored output was tested sample-for-sample identical.

## Observed validation

The preceding preview passed the Release CLAP/VST3 build; focused compressor history, telemetry, creative DSP and wrapper tests; related layout/modulation checks; typography-fit and coordinated Motion checks. Actual generated PCM drove the reviewed all-theme inspector/rack captures. See [preview details](Preview-1.11.1.md).

Final delivery checks:

- Incremental Release plugin/test-target check: **Ninja reported no work to do**.
- Packaged CLAP smoke: **1.11.1**, **2357 parameters**, original indices preserved; nine main/aux layouts at 44.1/48/96 kHz, state, latency, bypass and automation passed.
- Packaged VST3 native component host: **108 bus/rate/Morph/dry/bypass cases passed**.
- Installer: both per-user formats **Installed and SHA256 verified**, with no host killed or locked files forced.
- All build/test commands used `Invoke-HalfCpu.ps1`: **6/12 logical CPUs**, affinity **0x3F**.

The full unrelated suites were not rerun for delivery. The earlier native desktop-occlusion blocker is not claimed resolved. Actual DAW listening, transport and native FPS remain unverified. Captures are production-component snapshots, not desktop recordings.

## Build/package/installed hashes

| Format | SHA256 |
| --- | --- |
| CLAP | `9eaed440b10366d5ca26e521e82129b72a427378be83c87fdd4d1cd7030e97ba` |
| VST3 | `10bc50b3faec11d3c85f61eba63ab56be15a14095c6747faf84b697aacc9b31f` |

Matching binaries are tracked under `Windows-x64` and installed in the per-user Common CLAP/VST3 folders. Licensing and complete corresponding-source access accompany the [v1.11.1 release](https://github.com/YukiYuziriha/plug1/releases/tag/v1.11.1).
