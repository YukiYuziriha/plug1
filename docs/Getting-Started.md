# Getting Started

[Overview](../README.md) · [Build from source](Building.md) ·
[Detailed manual/history](Manual-and-History.md)

## Install on Windows x64

HEAVEN by lil grass block v1.12.3 is an audio effect supplied as CLAP and VST3. Choose the
format your host supports; installing both is optional. There is no tracked
installer. Building from source is not required to use the supplied binaries.

1. Download the repository using **Code → Download ZIP** on
   [GitHub](https://github.com/YukiYuziriha/plug1), then extract it, or clone it.
   For CLAP alone, the [tracked binary](https://github.com/YukiYuziriha/plug1/raw/refs/heads/main/Windows-x64/CLAP/HEAVEN%20by%20lil%20grass%20block.clap)
   is also a direct download.
2. Close hosts that currently have HEAVEN (formerly Vocal Stack) loaded. Do not force replacement
   of a locked plugin or terminate the host to install it.
3. Copy the appropriate item:

   | Format | From the repository | Per-user destination |
   | --- | --- | --- |
   | CLAP | `Windows-x64/CLAP/HEAVEN by lil grass block.clap` | `%LOCALAPPDATA%\Programs\Common\CLAP\` |
   | VST3 | **Entire** `Windows-x64/VST3/HEAVEN by lil grass block.vst3/` folder | `%LOCALAPPDATA%\Programs\Common\VST3\` |

   System-wide alternatives are `C:\Program Files\Common Files\CLAP\` and
   `C:\Program Files\Common Files\VST3\`; writing there may require administrator
   permission. The VST3 inner module alone is not a complete bundle.
4. Keep `Windows-x64/LICENSE.txt`, `Windows-x64/PixelifySans-OFL.txt` and the
   `Windows-x64/ThirdParty/` notices with your distribution/copy.
5. Open the host, check its plugin search folders, rescan, and insert **HEAVEN by lil grass block** on an audio track. In Bitwig, check **Settings → Locations → Plug-in
   Locations** if it is missing. These are installation pointers, not a claim of
   completed real-DAW validation.

The tracked binaries are v1.12.3. This version starts dry and expands the
[factory preset bank](Factory-Presets.md) to 30 patches.
There is no Tuning tab or automatic pitch correction on main. The previous
implementation and replacement plan are preserved on
[`work/autotune-donor-integration`](https://github.com/YukiYuziriha/plug1/tree/work/autotune-donor-integration).
Legacy plugin identities and preset/preferences locations remain unchanged.

## Start with Stack

A fresh instance starts with Leveler, Doubler, shared Formant and rack effects
inactive. The lead is audible and global Bypass is off. Enable the controls you
want or load a factory preset; opening a saved session restores its own patch.

Play a vocal through the plugin and start with the main controls:

| Control | Use it for |
| --- | --- |
| Leveler | Smoothing phrase dynamics relative to the recording's own energy envelope; 0% disables the correction. |
| Lead / Stack | Balancing the edited Lead against the generated voices. At 100% Stack, no Lead remains. |
| Human | Scaling irregular timing, pitch and tonal motion without overwriting stored voice offsets. |
| Width | Scaling the generated voices' stereo spread. |
| Formant | Shifting vocal resonance independently of musical pitch. |
| Global Mix | Blending latency-aligned untouched original input with the complete processed patch. |
| Output | Final level trim; double-click for unity. It is not a limiter. |

Leveler approximately preserves long-term input RMS after settling, not exact
peaks or perceived loudness. It does not force a fixed output RMS/LUFS target.
Whole-plugin bypass returns the latency-aligned original input, ignoring Output.

### Add and edit voices

Select a generated voice card to activate/edit custom voices. First activation
initializes from the current Basic settings. **+ Add voice** expands the stack to
up to **eight generated voices**, in addition to the independently editable Lead.
Scroll the voice-card area if needed.

Each generated voice has On, gain, pan, delay, pitch, fine detune, formant, motion
depth and rate. Pitch is musical transposition, not automatic pitch correction.
Human scales motion; it does not erase fixed offsets. Independent custom formants
remain active when the shared Formant switch is off.

**On** mutes a voice without removing its settings. Removing an added slot hides
and disconnects it without renumbering other voices; re-adding retains its edits.
The first two generated cards cannot be removed through the UI. **Reset to Basic**
derives a two-voice configuration from current basic controls and removes extra
voice connections without resetting unrelated FX.

## Add effects and routing

In Stack, **+ FX** adds an effect to the main-bus rack. Tabs select actual Routing
nodes, not copies of the processors. Use a tab's On switch for bypass, or drag a
tab to reorder an unambiguous serial chain. There are 15 shared additional FX
slots plus the original main Chorale slot—not 16 instances of every effect type.

Open **Routing** for individual-voice processing, multiple branches, independent
Dry/Wet mixers, full-level A/B Splitters and two-/three-band Multiband processing.
Switching the Stack/Routing view does not itself change sound.

- Drag an output pin to an input to connect it. Dragging an already-connected
  output moves its destination rather than adding another branch. Use Splitter
  for two independently wired full-level copies.
- Multiple sources can enter one input with source-aware merge compensation.
  This is not a limiter or a promise of constant loudness for correlated signals.
- Select a cable to edit either endpoint or disconnect it. Undo/Redo supports
  topology edits. Unwired inputs receive silence; disconnected Output is silent
  after the short de-click ramp. An effect can retain a tail after input removal.
- Expand supported node cards for inline controls; **Fit**, wheel zoom and canvas
  panning help navigate a larger patch.
- A branched or auxiliary path can make rack topology controls read-only. Use
  Routing to edit those wires/nodes; the rack will not flatten your graph.

### Spectral Morph and sidechain

Morph needs **two deliberately chosen sources**:

```text
Lead / Stack or another internal branch ──> Morph MAIN  ──> Output
Internal donor OR host SIDECHAIN       ──> Morph DONOR
```

1. Add **Spectral Morph** through + FX or Routing. If the graph becomes
   **RESTART PENDING**, apply the preparation step below before auditioning it.
2. Connect the audio you want to process to **MAIN**. Connect a different internal
   branch to **DONOR**, or enable the host's optional mono/stereo auxiliary bus,
   add a **SIDECHAIN** source, and wire it to DONOR. Adding either node does not
   invent a donor cable.
3. Route Morph's output to the next effect or Output. Cycles and unsupported
   serial Morph paths are rejected.
4. Start with Magnitude or Envelope mode, adjust Morph and Smoothing, then try
   **Freeze donor**. Freeze holds captured donor spectra while MAIN keeps changing;
   it is not an audio-loop recorder. Mix blends delayed MAIN and processed audio,
   not MAIN and DONOR.

An unwired/disabled donor supplies silence, never an automatic copy of MAIN.
There is no automatic donor level matching. The SIDECHAIN labels `MONO CONFIG`,
`STEREO CONFIG` and `DISABLED` describe host bus configuration, **not detected host
cables**. A configured but silent bus may legitimately analyze zero audio. Mono
auxiliary input is duplicated to L/R; stereo preserves the two channels.

Freeze requests persist in presets, but captured audio/spectra do not. A recalled
patch may show capture waiting until donor analysis exists. Reset/reprepare clears
the capture. Morph retains MAIN phase; donor-only audio does not excite a silent
MAIN. It is not a vocoder/phase-transfer mode.

### Latency and RESTART PENDING

Reported total latency at the rates covered by the current module checks:

| Sample rate | No declared Morph | With declared Morph |
| --- | ---: | ---: |
| 44.1 kHz | 1344 samples / 30.48 ms | 5440 samples / 123.36 ms |
| 48 kHz | 1344 samples / 28.00 ms | 5440 samples / 113.33 ms |
| 96 kHz | 2688 samples / 28.00 ms | 6784 samples / 70.67 ms |

A declared Morph node reserves its additional 4096 samples even when dormant,
bypassed or disconnected. FFT size selection does not change that reported total.

Adding/removing Morph while the host has prepared the plugin saves a pending
routing topology. **RESTART PENDING means the displayed saved wiring is not yet
the running graph.** The current graph and host latency remain in use until the
host **deactivates/reactivates (re-prepares)** the plugin. Use the host's plugin
deactivate/reactivate facility, or reload the plugin; stopping transport or
resetting alone is not enough. Pending wiring survives save/recall and can be
undone. Parameter controls still address the current shared parameter bank while
wiring is pending. Ordinary same-latency routing edits remain live.

Serial Morph dependencies exceeding the fixed routing budget are rejected on
MAIN or DONOR paths. Multiple independent parallel Morph instances are possible
within the shared slot capacity. See the [technical latency policy](Preview-1.11.0.md#latency-policy--fixed-per-host-preparation)
for edge cases.

## Modulation, presets and displays

Use **+ Mod** in Routing for LFO and Random sources. Assign supported continuous
controls using the modulation UI; discrete Morph Mode, FFT and Freeze are host
automation controls, not continuous LFO targets. The footer preset browser saves
full patches. Copy settings/Paste transfers settings between instances. Skin
cycles **Device → Neon → Spectral** without choosing a different audio engine.

Input/output meters show **RMS and sample peak**, not LUFS or true peak.
Compressor/Gate histories show measured attenuation; Morph spectra show analysis
before Mix/Output. Reverb/Convolution/Saturator combine measured histories with
parameter guides. Delay timing and Granular envelope graphics are illustrations,
not recorded echoes or live grain waveforms. See the [showcase notes](showcase/README.md).

## Troubleshooting and support

- **Plugin missing:** confirm a Windows x64 host and supported format; check scan
  paths, complete VST3 bundle contents and the host's rescan/quarantine report.
- **No sound:** check bypass/mutes, Lead / Stack, FX Mix and Output; inspect the
  cables into Output and any required Morph donor. At 100% Stack with no enabled
  generated voices, the voice bus is silent.
- **Visible wiring seems inactive:** look for RESTART PENDING and re-prepare the
  plugin. Do not assume transport stop applies a latency-changing edit.
- **Unexpected levels/artifacts:** reduce extreme pitch/formant/drive values,
  inspect sample peaks and trim Output. Neither Leveler nor merge compensation
  prevents clipping or guarantees perceived loudness.
- **Host-specific problems:** actual Bitwig listening/session/transport validation
  remains unperformed. Automated module tests are not a DAW compatibility list.
  The current release also retains a native desktop-input occlusion blocker;
  [read the validation boundaries](Release-1.11.0-Validation.md).

[Open an issue](https://github.com/YukiYuziriha/plug1/issues) with version, format,
host/Windows versions, sample rate, block size and minimal reproduction steps.
Include a small preset if useful; do not upload private recordings or personal
paths. Audio examples should be yours to share.
