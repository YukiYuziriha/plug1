# Vocal Stack 1.11.0 — approved delivery validation

Date: 2026-11-15. The user approved installing the entire pending preview. This
release includes the earlier pending UI work, SIDECHAIN/Spectral Morph, thick
inward rotary rings, Output slider, centered unsigned Dry/Wet presentation,
Lead / Stack labeling, wide left-hand rack Morph spectrum, measured four-second
Compressor/Gate histories, Stereo L/R activity/correlation and persisted final
post-Mix smoothed Mono Audition. Gain retains its +24 dB range and legacy Wet
state/automation. The performance/telemetry integration is included.

There are **2357 host parameters**: indices 0–2341 are preserved, and fifteen
Mono Audition booleans append at 2342–2356. No additional feature, dependency or
test coverage was added during final delivery. Airwindows remains research only.

## Source/artifact correspondence and resource limits

The existing Release build had completed successfully. Final delivery ran the
existing incremental build helper through a dedicated 64-bit PowerShell process
and `tools/Invoke-HalfCpu.ps1`: **6/12 logical processors, affinity 0x3F, jobs 6**.
Ninja reported **no work to do**, confirming current source dependencies were
already built. `COPY_PLUGIN_AFTER_BUILD` remains false. No full suite or GUI
capture rerun was needed. Log: `source/build/final-release-incremental.log`.

All final packaged/installed module smoke children also ran through that wrapper
and reported the same restricted CPU subset. Local helpers, logs, captures and
the machine-local capture/encode script remain ignored rather than packaged.

## Existing validation evidence reviewed at delivery

| Check | Observed result | Log under `source/build/` |
| --- | --- | --- |
| Release CLAP/VST3 and affected tests | Built | `coordinated-build-delivery.log` |
| Core DSP/processor/state/host regressions | **40/40 passed**, 201.77 s | `coordinated-core-delivery.log` |
| Focused control/history/utility/monitor/processor/module suites | **7/7 passed**, 36.44 s | `coordinated-focused-delivery.log` |
| VST3 FFT controller queues, state/PCM and latency | **1/1 passed**, 108 matrix cases, 38.34 s | `coordinated-vst3-automation-2.log` |
| Separate rack and granular UI | **2/2 passed**, 17.73 s | `coordinated-native-delivery.log` |
| Full editor | **80/81 groups passed**, native desktop-hit precondition failed | `coordinated-editor-delivery.log` |
| Native routing cost probe | **Blocked by desktop occlusion**, not a measured native FPS result | `coordinated-native-cost-delivery.log` |
| Existing review artifacts | **39/39 GIFs verified**, all three themes, actual PCM/control changes | `approval-1.11.0-coordinated/index.md`, `manifest.json` |

The full-editor/native-cost blocker identified `Chrome_RenderWidgetHostHWND`
covering the intended fixture hit; earlier attempts identified `SHELLDLL_DefView`.
No native input precondition was weakened. The existing component-cost probe
is explicitly **not native FPS**. Review GIFs are production-component snapshots,
not desktop recordings; their display placement and limitations are documented
in [Preview 1.11.0](Preview-1.11.0.md). No GUI recapture or lock-screen workaround
was attempted during delivery. Approval did not convert these limitations into
passes or require another endless full rerun.

## Packaging, installation and final smoke

Refreshed tracked `Windows-x64` CLAP/VST3 binaries directly from the verified
Release build. Refreshed LICENSE, Pixelify Sans OFL and ThirdParty notices from
the repository; the legal notice contents were unchanged. Notices were also
copied beside installed CLAP in `CLAP/Vocal Stack Licenses` and inside the VST3
bundle at `Contents/Resources/Licenses`.

Packaged **CLAP and VST3 smoke passed before installation**. The existing
`source/build/install-release.ps1` preflighted exclusive read/write access to
both installed binaries before replacing either; it reported **Installed and
SHA256 verified** for both. No file lock blocked installation. Bitwig was not
terminated and no lock was forced. Installed **CLAP and VST3 smoke then passed**:

- CLAP: version 1.11.0, 2357 automatable parameters, preserved original indices,
  state, latency, bypass, automation and main/auxiliary offline layout matrix.
- VST3: native component host **108 bus/rate/Morph/dry/bypass cases passed**,
  including the existing FFT parameter-queue assertions.
- Logs: `source/build/final-package-smoke.log` and
  `source/build/final-installed-smoke.log`.

Installed binary paths:

- `C:\Users\tkach\AppData\Local\Programs\Common\CLAP\Vocal Stack.clap`
- `C:\Users\tkach\AppData\Local\Programs\Common\VST3\Vocal Stack.vst3\Contents\x86_64-win\Vocal Stack.vst3`

Independent checks confirmed **build = tracked package = installed**, with file
and product version **1.11.0** on all six binaries. Full records are retained in
`source/build/final-release-hashes.json`.

| Format | Build = package = installed SHA256 |
| --- | --- |
| CLAP | `54C417087AB17CE615856542E2ED5DBA824BEC1329DC7CBD813B2054EF6F95A9` |
| VST3 | `2A031830AD574FD997F2F176F73EF15441ECAD219FF37F246CDC1CDF23383147` |

## Latency and remaining real-host checks

The existing prepared-graph policy is unchanged: without Morph, total latency is
1344 samples at 44.1/48 kHz and 2688 at 96 kHz; declaring Morph adds 4096 samples
(5440/6784 total). Latency-changing graph edits remain **RESTART PENDING** until
host deactivate/reactivate. Transport stop/reset does not activate them. There
is no restored always-on reserve or newly invented live PDC policy.

Automated module hosts are not actual DAW listening/transport validation. Actual
Bitwig audio/listening, session workflow and keyboard transport remain
**unperformed**. Reload/rescan the plugin in the host to use the installed version;
use deactivate/reactivate to apply a pending latency-changing topology.
