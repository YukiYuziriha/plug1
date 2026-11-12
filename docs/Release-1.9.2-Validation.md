# Release 1.9.2 — built, validated and packaged; installation blocked

## Status and evidence boundary

**1.9.2 Release CLAP/VST3 builds and automated validation succeeded; packages
were refreshed and hash-verified. Installation is blocked.** Results below are
coordinator-reported in the task handoff, not newly executed by this
documentation-only pass. No build, tests, packaging, installation or commit was
performed by this pass. Coordinator commit/push is pending, not claimed complete.

**The installation attempt was blocked by a locked CLAP; no installed files
changed and 1.9.1 remains installed until a successful retry.**
The [1.9.1 record](Release-1.9.1-Validation.md) remains historical evidence for that
release; its test results, hashes and installation do not validate 1.9.2.

## Implemented scope

The original granular graph was good; the request was visual polish, not the
flickering live cloud. `CreativeEffectVisual` restores original-style **fixed,
overlapping parameter-envelope curves**, grid gradients and **three-second
history/read-window guides** from coherent parameters. The graph stays visible
before preparation, with stopped transport and without audio. Audio telemetry
does not drive geometry; there is no active-grain auto-zoom or cloud scatter.

This is a **parameter illustration**, not an actual recorded waveform or measured
grain telemetry. **DSP is unchanged**; the snapshot bridge remains available in
the backend but is not the source of this graph's geometry.

## Validation and visual evidence

- Release CLAP/VST3 builds succeeded. The combined build/test command timed out
  at **600 s**, after building completed and while tests were running. That
  interrupted test run is not a pass.
- Separate `ctest`, without rebuilding, passed **all 34 focused tests in
  182.97 s**, including **66 editor groups**.
- Grain UI passed in **11.37 s**, verifying the parameter graph unprepared and
  stopped, snapshot-count changes, model retention on failed coherent reads and
  all-five-effects coherence across Device/Neon/Spectral themes.
- The coordinator manually reviewed all six screenshots in
  `.cache/update-ui-review/`:
  `grain-parameter-{device,neon,spectral}-{174,440}.png` (all combinations).
  Root `VocalStack-Granular.png` and `VocalStack-Stack-FX.png` were updated from
  the current Release. Actual DAW visual behavior remains unverified.
- No audio DSP changes; the separate legacy DSP suite was **not rerun**.

## Package identities

Copied package binaries SHA256-match the successful Release artifacts:

| Package binary | SHA256 |
| --- | --- |
| `Windows-x64/CLAP/Vocal Stack.clap` | `3b17ab1289d2e9815ba200ca91d8ab418884f7d37ee26cf5620bd6bb25860aae` |
| `Windows-x64/VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3` | `943dde6ca0547753c24e5105eaa28af099049bea16eb8bec32ed98ee92342a50` |

**Packaged CLAP smoke passed:** 2087 parameters, latency, bypass and automation.
These are package identities, not installed 1.9.2 identities.

## Remaining delivery and host checks

- [ ] Retry installation after the CLAP lock is released and verify both installed
  formats. No installed files changed in the blocked attempt; 1.9.1 remains
  installed. Never terminate Bitwig or overwrite a loaded plugin to force it.
- [ ] Coordinator commit/push; pending, not yet reported complete.
- [ ] Actual Bitwig visual/listening, transport, automation/session recall and
  native FPS checks; automated passes and screenshot review are not host approval.
