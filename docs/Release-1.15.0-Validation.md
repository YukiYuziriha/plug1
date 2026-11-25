# HEAVEN 1.15.0

FX wet pockets in Routing can now host a selectable insert on a single-wet-path FX. The parent Mix blends the dry path with the complete wet path. Legacy Granular → Delay graphs, FX slot IDs and host parameter IDs are unchanged. Multiband retains its separate outputs.

## Validation

- Windows x64 CLAP and VST3 Release builds passed.
- Ten focused CTest suites passed: DSP, Routing Backend, Routing Activation, Tone Effects, Lead Mix Backend, Main Bus FX Rack, Wet Pocket, CLAP, VST3 Sidechain, and Routing Activation Host.
- Wet-pocket tests cover dry isolation, bypass, legacy Granular → Delay, other FX, and Morph latency. Live owner or insert bypass correctly requests removal of latency when the wet Morph is the only latency-bearing effect.
- Native Routing and Sidechain workflow checks passed on the non-primary display in all four skins, including Delay → Reverb selection, navigation, deletion, undo, and preset recall.

Bitwig was not used. This work does not claim to resolve the reported sidechain playback stutter.
