# HEAVEN 1.14.0

- Blend recorded doubles with Lead from Stack. The independent Sidechain card follows the host-enabled input bus and includes Pitch, Fine, Formant, Gain, Pan, Delay, Depth and Rate. Process off leaves the raw blend intact, and Spectral Morph still has its raw donor input.
- See the Lead Gain Rider's live adjustment in dB. Neon is the default for new instances, and Grass Block adds animated dirt and grass with pixel typography.

## Validation

Windows x64 Release builds completed for CLAP and VST3. The focused Sidechain checks passed for independent control bindings, host-bus changes with the editor open, saved settings, wet-only gain, and latency requirements. Native Sidechain, Gain Rider and wet-pocket workflows passed on a non-primary monitor. Offline CLAP, VST3 Sidechain, DSP, Routing Activation, Main Bus FX Rack and Wet Pocket CTest groups passed, as did 29 packaging-tool tests.

The full Sidechain Processor suite stops at `Gate bridge graph`, which also reproduces with the previous local test executable. Bitwig was not used for validation. A configured host bus does not prove a physical connection, and latency changes depend on the host honoring its reactivation request.
