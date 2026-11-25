# HEAVEN by lil grass block

**The plugin might have critical bugs leading to severe performance losses.**

**Build a vocal stack. Split it into parallel effects. Or run just the delay.**

HEAVEN is an open-source audio effect for **Windows x64 · CLAP and VST3**. Turn one take into **up to eight generated voices plus an independent Lead**, shape each branch, and bring it all back together in a modular effects graph. Stack and Routing are two views of the same patch.

[Download the latest release](https://github.com/YukiYuziriha/plug1/releases/latest) · [Install](#download--install) · [Getting Started](docs/Getting-Started.md) · [Showcase](#showcase)

## v1.15.0 — Effects inside effects

In Routing, put an FX inside another FX’s wet pocket. Echo only the grains, add reverb to repeats, or EQ the reverb without touching its dry signal. The parent’s Mix controls the whole wet path. [How wet pockets work](docs/Getting-Started.md#shape-only-the-wet-signal).

### Bring your own double

Send a recorded double to HEAVEN’s host sidechain and blend it with your Lead right from Stack. **Sidechain Blend** comes wired in the standard patch, and the independent **Sidechain card** lets you tune, pan and shape the double without touching the Lead. Leave **Process** off to blend the raw take instead. The card follows the host-enabled bus, not whether a track is routed to it or making sound.

The Lead card now houses **Gain Rider**, with a live dB readout so you can see what it adds or cuts. **Neon** is the default for fresh instances, and **Grass Block** adds a pixel-style skin. v1.14.1 also fixes latency-change requests and shows the actually loaded plugin version. [Set up a sidechain double](docs/Getting-Started.md#blend-a-recorded-double-with-sidechain).

## What you can make

- **Doubles and layered harmonies.** Shape pitch, fine detune, formant, gain, pan, delay and motion per generated voice, with independent Lead edits. Start clean or pick from [30 factory presets](docs/Factory-Presets.md).
- **Parallel and multiband effects.** Give voices their own FX, split signals into two or three bands, and recombine branches with Dry/Wet mixers. Main Bus FX tabs edit the same nodes as Routing.
- **Spectral crossbreeds.** Blend a donor’s spectrum into your vocal with Spectral Morph’s **MAIN** and **DONOR** inputs, Magnitude and Envelope modes, and donor Freeze. Feed it an internal branch or the raw mono/stereo host sidechain, independent of the Sidechain card.
- **Glitch fills and grain echoes.** Grab a phrase and fire reverses or ratchets with Vocal Glitch, including MIDI triggering. Give Granular its own Delay on the wet path so the echoes follow Granular’s Mix.
- **Tone, space and movement.** EQ8, Waveshaper, Chorale, Delay, Reverb, synthetic-IR Convolution, Saturator, Compressor, Bitcrusher, Granular, Gain, Stereo, Gate/Expander, XP Filter and Synth Filter. Animate patches with LFO or Random modulation, save full-patch presets, and switch between Device, Neon, Spectral and Grass Block themes. Routing has Undo/Redo, and input/output meters show RMS and sample peaks.

## Showcase

*Earlier Neon captures showing effect controls and signal displays.*

### Spectral Morph — borrow and freeze a spectrum

![Spectral Morph showing MAIN, DONOR and result spectra with donor Freeze](docs/showcase/spectral-morph.gif)

### Reverb — see the wet tail

![Reverb showing measured input and wet history alongside space controls](docs/showcase/reverb.gif)

### Compressor — see the gain reduction actually applied

![Compressor showing transfer curve and gain-reduction history](docs/showcase/compressor.gif)

### Stereo — check width and mono compatibility

![Stereo showing left and right activity, correlation and Mono Audition](docs/showcase/stereo.gif)

## Download & install

Get the **Windows x64 ZIP**, **SHA256 checksums**, and **corresponding source** from the [latest GitHub release](https://github.com/YukiYuziriha/plug1/releases/latest). Extract the ZIP and install just the format your host supports:

| Format | What to copy | Install location |
| --- | --- | --- |
| CLAP | `HEAVEN by lil grass block.clap` | `%LOCALAPPDATA%\Programs\Common\CLAP\` or `C:\Program Files\Common Files\CLAP\` |
| VST3 | The **entire** `HEAVEN by lil grass block.vst3` folder, including `Contents` | `%LOCALAPPDATA%\Programs\Common\VST3\` or `C:\Program Files\Common Files\VST3\` |

Close hosts that have HEAVEN loaded before replacing it. Add the install location to your host’s scan paths if needed, rescan, and insert **HEAVEN by lil grass block** as an audio effect. Keep the included license files, source notice, font licenses and third-party notices with your copy. No macOS, Linux or standalone binaries are supplied.

## First patch

1. Insert HEAVEN on a vocal track. Turn on **Doubler**, dial in **Human** and **Width**, then balance the Lead against the generated voices with **Lead / Stack**.
2. Select a voice card to edit it, or use **+ Add voice** to expand the stack.
3. Use **+ FX** for main-bus effects, or open **Routing** to build per-voice, multiband or sidechain paths.
4. Set **Global Mix** to blend the original input with the whole patch, trim **Output**, and save a preset.

[Getting Started](docs/Getting-Started.md) · [Build from source](docs/Building.md) · [Report an issue](https://github.com/YukiYuziriha/plug1/issues)

## License & credits

Open source under **GPLv3 + AGPLv3**: project-owned code retains [GPLv3](LICENSE), with JUCE used under its [AGPLv3](COPYING.AGPLv3) option. See [licensing and corresponding source](docs/Licensing.md) for combined-work and redistribution terms, and [historical source downloads](docs/Historical-Source.md) for older releases.

[Third-party notices](ThirdParty) cover JUCE, CLAP/CLAP JUCE Extensions, the VST3 SDK, Signalsmith Stretch/Linear, the GPL-covered [Vital filter subset](ThirdParty/Vital/README.md), and [Pixelify Sans](ThirdParty/PixelifySans-OFL.txt) and [Oxanium](ThirdParty/Oxanium-OFL.txt) under the SIL Open Font License 1.1. Embedded dependency notices also apply. Convolution uses original synthetic Church, Hall, Room and Plate IRs.
