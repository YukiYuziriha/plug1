# Vocal Stack 1.0.2

Windows x64 audio effect for Bitwig Studio. The archive contains a CLAP plugin,
a VST3 fallback, and C++ source with CMake and tests.

## Install

1. Extract the ZIP.
2. Copy `Windows-x64/CLAP/Vocal Stack.clap` to
   `C:\Program Files\Common Files\CLAP\`. The per-user location is
   `%LOCALAPPDATA%\Programs\Common\CLAP\`.
3. Open Bitwig and search for **Vocal Stack** in the audio effects browser.
   Check **Settings → Locations → Plug-in Locations** if your chosen location is
   missing.
4. To use VST3, copy the entire `Vocal Stack.vst3` folder from
   `Windows-x64/VST3/` into `C:\Program Files\Common Files\VST3\`.

Installation paths are defined by [CLAP](https://github.com/free-audio/clap/blob/main/include/clap/entry.h)
and [VST3](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Locations%2BFormat/Plugin%2BLocations.html).

## Controls

| Control | Default | Effect |
| --- | --- | --- |
| Level | 50% | Strength of phrase-level gain correction |
| Double | 35% | Amount of the two generated layers |
| Human | 35% | Variation in the doubles' timing, pitch, and tone |
| Width | 65% | Stereo spread of the doubles |
| Formant | 0.0 st | Moves vocal resonances at the original lead pitch |

Bitwig's **Plug-in Parameters** button on the device exposes all five knobs,
the three module switches, and bypass for automation. CLAP also supplies a
**Vocal Stack** Remote Controls page with the five knobs and three module
switches.

Click **SKIN / DEVICE** or **SKIN / NEON** to switch the editor. Neon uses a
flat Shibuya-at-night palette. The project
state stores the skin, controls, and bypass switches. **Reset** restores the
audio controls. **Copy settings** and **Paste** transfer settings between
instances. The meter readout under IN and OUT shows the current leveler gain.

The leveler detects active phrases from smoothed input RMS and limits correction
to −10 to +8 dB at full strength. The doubles use separate Signalsmith
processors, drifting timing and pitch, subtle formant offsets, and reduced
level on consonants. The mix estimates loudness from the actual combined
signal. The Formant module switch controls formant processing for all three
voices. Width affects the doubles. The lead retains the stereo input image.

The plugin reports 1,344 samples of latency at 44.1 and 48 kHz, about 30.5 and
28 ms respectively. Version 1.0.0 reported 2,496 samples at those rates. Bitwig can compensate during playback. This latency is
also present when monitoring through the effect. Bypass uses the aligned dry
path. CLAP offers stereo, mono-to-stereo, and mono layouts.

## Build from source

From a Visual Studio 2022 Developer PowerShell with C++ desktop tools, Git,
and CMake 3.24 or newer installed:

```powershell
cd source
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The first configure fetches pinned dependencies. The plugins appear under
`build/VocalStack_artefacts/Release/`. To build just the portable DSP tests,
configure with `-DVOCALSTACK_BUILD_PLUGIN=OFF`.

The code uses [JUCE](https://github.com/juce-framework/JUCE),
[CLAP JUCE Extensions](https://github.com/free-audio/clap-juce-extensions), and
[Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch).
The local CLAP wrapper patch adds mono layouts, marks the bypass parameter,
and rejects invalid binary XML state.

## Verification

- Both Windows x64 binaries were cross-compiled with Clang and the Microsoft
  SDK. The PE exports and imports were inspected.
- The Linux CLAP build passed 34 validator checks with zero failures and
  warnings. Ten inapplicable checks were skipped. The Linux VST3 build passed
  pluginval at strictness level 5.
- The DSP and CLAP host tests exercised bypass alignment, three bus layouts,
  state recall, parameter automation, offline processing, and operation at
  several sample rates up to 192 kHz. The formant tests included steady 72 Hz
  and 260 Hz fundamentals with shifts in both directions. The CLAP host test
  also verified all nine automation parameters and the eight Remote Controls
  mappings.
- During version 1.0.0 testing, both editor skins rendered in a Linux CLAP
  host and Bitwig Studio 6.1.4 on Linux scanned and inserted the CLAP.
  The sandbox's Bitwig audio engine stopped during startup. Playback in
  Bitwig and loading the Windows build in Bitwig remain unverified.
  The sound on a recorded performance remains a listening check.
