# HEAVEN 1.13.1 validation

Vocal Glitch now uses compact, top-labeled Pattern and Launch Grid dropdowns.
They sit side by side when space permits and stack at narrow widths. Layer and
Auto are stacked above Threshold. Other FX controls, DSP and host parameter
mappings are unchanged.

Windows x64 Release CLAP and VST3 built with `Invoke-HalfCpu.ps1`. The
`VocalStackCLAP` and `VocalStackFactoryPresetValidation` checks passed. A focused
native panel check verified the dropdown and button positions at 840 px and
248 px in Device and Neon on the non-primary display (96 DPI, scale 1).
No Bitwig listening pass or full suite was run.

Packaged binary hashes and build input evidence are recorded in
[`BUILD-PROVENANCE.json`](../Windows-x64/BUILD-PROVENANCE.json).
