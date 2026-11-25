# HEAVEN 1.15.0

Each single-wet-path FX in Routing can own a scoped canvas with Wet In, Wet Out, and explicitly wired nodes. Drop a disconnected node on a cable to splice it in, inside a pocket or in the main graph. Legacy Granular → Delay graphs, FX slot IDs and host parameter IDs retain compatibility. Multiband retains separate outputs.

## Validation

- Windows x64 CLAP and VST3 Release builds passed.
- Ten focused CTest suites passed: DSP, Routing Backend, Routing Activation, Tone Effects, Lead Mix Backend, Main Bus FX Rack, Wet Pocket, CLAP, VST3 Sidechain, and Routing Activation Host.
- Wet-pocket tests cover dry isolation, bypass, legacy Granular → Delay, manual multi-node wiring, unwired nodes, invalid cross-scope cables and cycles, and Morph latency. Live owner or insert bypass correctly requests removal of latency when the wet Morph is the only latency-bearing effect.
- Native Routing and Sidechain workflow checks passed on the non-primary display in all four skins, including scope navigation, main and pocket cable drop-in, deletion, undo, and preset recall.

Bitwig was not used. This work does not claim to resolve the reported sidechain playback stutter.
