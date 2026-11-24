# HEAVEN 1.13.4 — validation

## Final 1.13.4 build and CLAP checks

- The final binaries built at `1.13.4`; the harness confirmed `1.13.4` in the
  PE version metadata and the CLAP descriptor.
- The stereo Delay CLAP benchmark measured **53.5–59.4% less audio-thread
  CPU time** than packaged `1.13.3`: five alternating paired runs for each
  combination of 44.1, 48 and 96 kHz and 64- or 512-frame blocks. Each reported
  reduction is the median paired reduction for that configuration.
- The final CLAP restored the same 137,957-byte full state saved by packaged
  `1.13.3`: the active Delay recalled exactly 123 ms, and the second wired
  Delay remained disabled. Reported latency dropped from 1,344/2,688 samples
  to zero, and the stereo impulse responses matched exactly after alignment.

## Final regression run

- All 14 selected CTest groups passed: RoutingActivation, VocalLatency, CLAP,
  VST3Sidechain, FxAutomation, WetPocket, DSP, RoutingBackend, SpectralRouting,
  LeadMixBackend, VoiceBackend, MainBusFxRack, GrainSnapshot and FxMonitor.
- The processor `--workflow-check` passed, including Morph toggles at
  0 ↔ 4,096 samples, legacy recall, and native wet-pocket and Mix-slider UI
  checks on the enumerated second monitor.
- All 26 release-tool tests passed. The host controls when reactivation occurs;
  Bitwig’s scheduling was not tested.

## Packaging and verification

See the maintainer commands in
[`Windows-x64/SOURCE.md`](../Windows-x64/SOURCE.md): run
`Refresh-Windows-Package.py` with the successful Release build’s log and exit
file, bundle with `Bundle-Release-Assets.py` using an explicit `--working-tree`
or `--revision`, then run `Verify-Release-Assets.py` on the output directory
with the matching version. Use `tools/Invoke-HalfCpu.ps1` from dedicated 64-bit
PowerShell processes, as documented there.
