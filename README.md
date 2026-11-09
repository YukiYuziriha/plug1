# Vocal Stack 1.1.2

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
| Leveler | 50% | Automatic gain riding toward −20 dBFS RMS; 0% off, 100% full correction |
| Mix | 35% | Generated doubles added to the lead; 0% lead only, 100% maximum doubles, not wet-only |
| Human | 35% | Variation in the doubles' timing, pitch, and tone |
| Width | 65% | Stereo spread of the doubles |
| Formant | 0.0 st | Moves vocal resonances at the original lead pitch |
| Output | 0.0 dB | Final output trim, −24 to +12 dB; double-click for unity |

Bitwig's **Plug-in Parameters** button exposes all six knobs, the three module
switches, and bypass for automation. The original **Vocal Stack** CLAP Remote
Controls page retains its five knobs and three module switches. A second
**Output** page supplies output trim and bypass. Existing parameter IDs and
indices are unchanged; older sessions restore Output to 0 dB.

Click **SKIN / DEVICE** or **SKIN / NEON** to switch the editor. Neon uses a
flat Shibuya-at-night palette. The project
state stores the skin, controls, and bypass switches. **Reset** restores the
audio controls. **Copy settings** and **Paste** transfer settings between
instances. Full bypass uses the latency-aligned dry signal, ignoring output trim.
Output trim is smoothed over 18 ms and is not a limiter.

### Metering

The dedicated meter panel displays actual input and final output in **dBFS**:

- Independent L/R lanes for stereo, one M lane for mono.
- **RMS** fill: unweighted power with a 300 ms averaging time constant. A
  full-scale sine reads approximately −3.01 dBFS RMS, not 0 VU or LUFS.
- White **PK** markers: fast sample-peak envelope, falling at 24 dB/s.
- Amber **HOLD** markers: 1.5-second peak hold, then 12 dB/s decay.
- **MAX**: highest sample peak across the active channels since clear.
- Red **CLIP**: latched when a sample reaches or exceeds 0 dBFS. This indicates
  sample overload, not true/intersample peak; it does not clip or limit the audio.
- **LVL**: separately labeled leveler gain correction, not output trim.

Click the panel to clear held/max peaks and clip flags without changing the
audio, RMS, or live peak envelope. Space/Return are not meter shortcuts. Audio must be
processing for the reset request to take effect; ongoing overload relatches CLIP.
Meters keep their history independently of the editor's 30 Hz refresh.

![Neon editor with illustrative meter readings](VocalStack-Neon-Shibuya.png)

The preview uses synthetic readings to demonstrate peak hold and clip indication.

Mix is an additive doubles amount with smoothed stack-level compensation, not a
conventional dry/wet crossfade. The lead remains present at 100%. Leveler is not
output gain: above its −20 dBFS RMS target it attenuates, and below it boosts active
phrases. At 0% it applies unity gain; other modules can still change output level.
Use Output for manual trim. Version 1.1.2 clarifies these names without changing DSP
or parameter IDs, so existing automation and saved sessions retain their sound.

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

## 1.1.1 transport shortcut fix

Normal control clicks no longer request keyboard focus, and the meter panel no
longer consumes Space/Return. Idle slider value labels follow the same policy
in both skins, including after a skin change. Deliberate numeric entry still
accepts typing; commit or cancel the edit to return shortcuts to the host.

## Included 1.0.3 fixes

- Human timing drift now moves the double voices' delay read positions every
  sample, removing the 64-sample jumps that caused crackle at higher settings.
  The intended pitch, timing, and tone variation remains.
- Formant processing now matches each processed voice's average power to its
  latency-aligned, post-leveler input. The lead uses stereo-linked gain. Matching
  is smoothed, bounded to approximately ±12 dB, and applied before double-voice
  delay and consonant suppression. This preserves settled RMS level, not peaks
  or exact perceived loudness; spectral changes can still sound louder/quieter.

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

## Architecture

- `VocalEngine`: JUCE-independent signal path and smoothed controls.
- `Metering.h`: reusable per-channel RMS, sample-peak and hold detectors.
- `MeterBridge.h`: lock-free, best-effort audio-to-UI display snapshots.
- `PluginProcessor`: host parameters, state compatibility and audio-thread meter
  reset requests. Stable host parameter order is separate from UI mappings.
- `PluginEditor` / `MeterPanel`: controls, skins and presentation only.

Keep future effects as small DSP modules called by the engine, rather than
putting signal processing in the editor. See `AGENTS.md` for project conventions.

## Verification

- Version 1.1.1 Windows x64 binaries were built natively with MSVC 2026 and
  Ninja. All three CTest suites (DSP, CLAP host, editor/state) passed.
- DSP checks cover Human update boundaries at 44.1–192 kHz, formant RMS matching
  at 44.1–96 kHz, stereo/mono behavior, leveler/doubler balance, gain accuracy and
  smoothing, block-size independence, automation, silence/reset, calibrated
  meters, peak timing and latched overloads. Meter-only resets do not alter audio.
- Editor tests cover both skins' bounds and control bindings, state copy/paste,
  legacy sessions without Output, unity reset, mouse-only meter clear, and PNG
  rendering. Hidden native-peer tests check Space/Return are unconsumed outside
  numeric editing, and that numeric commit/cancel restores this behavior. Focus
  policy is checked again after switching skins. Actual DAW transport behavior
  still requires a host check. CLAP tests cover ten parameters and both remote pages.
- Earlier Linux CLAP builds passed 34 validator checks with zero failures and
  warnings. Ten inapplicable checks were skipped. The earlier Linux VST3 build
  passed pluginval at strictness level 5. These external validators have not
  been rerun for 1.1.1.
- The DSP and CLAP host tests exercised bypass alignment, three bus layouts,
  state recall, parameter automation, offline processing, and operation at
  several sample rates up to 192 kHz. The formant tests included steady 72 Hz
  and 260 Hz fundamentals with shifts in both directions. The CLAP host test
  also verified all ten automation parameters while preserving the original
  eight Remote Controls mappings.
- During version 1.0.0 testing, both editor skins rendered in a Linux CLAP
  host and Bitwig Studio 6.1.4 on Linux scanned and inserted the CLAP.
  The sandbox's Bitwig audio engine stopped during startup. Playback in
  that Linux Bitwig sandbox remained unverified. The user subsequently confirmed
  an earlier Windows build works in Bitwig. Version 1.1.1 has passed the offline
  checks above; listening to this updated version in Bitwig remains a user check.
