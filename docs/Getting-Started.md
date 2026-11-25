# Getting Started

[Overview](../README.md) · [Build from source](Building.md) ·
[Detailed manual/history](Manual-and-History.md)

## Install on Windows x64

This guide covers **HEAVEN by lil grass block v1.14.0**. Choose the format your
host supports; installing both is optional. No installer or source build is
required to use the supplied binaries.

1. Download and extract the **Windows x64 ZIP** for the version you want from
   [GitHub Releases](https://github.com/YukiYuziriha/plug1/releases). Open the
   extracted `HEAVEN-<version>` directory to find the paths below.
2. Close hosts that currently have HEAVEN (formerly Vocal Stack) loaded. Do not force replacement
   of a locked plugin or terminate the host to install it.
3. Copy the appropriate item:

   | Format | From the extracted ZIP | Per-user destination |
   | --- | --- | --- |
   | CLAP | `Windows-x64/CLAP/HEAVEN by lil grass block.clap` | `%LOCALAPPDATA%\Programs\Common\CLAP\` |
   | VST3 | **Entire** `Windows-x64/VST3/HEAVEN by lil grass block.vst3/` folder, including `Contents` | `%LOCALAPPDATA%\Programs\Common\VST3\` |

   System-wide alternatives are `C:\Program Files\Common Files\CLAP\` and
   `C:\Program Files\Common Files\VST3\`; writing there may require administrator
   permission. The VST3 inner module alone is not a complete bundle.
4. Keep the included license texts, `SOURCE.md`, font licenses and complete
   `ThirdParty/` notices with your copy.
5. Open the host, check its plugin search folders, rescan, and insert **HEAVEN by
   lil grass block** on an audio track.

The [factory preset bank](Factory-Presets.md) includes 30 patches.
There is no Tuning tab or automatic pitch correction on main. The previous
implementation and replacement plan are preserved on
[`work/autotune-donor-integration`](https://github.com/YukiYuziriha/plug1/tree/work/autotune-donor-integration).
Legacy plugin identities and preset/preferences locations remain unchanged.

## Start with Stack

A fresh instance starts with Gain Rider, Doubler, shared Formant and rack effects
inactive. The lead is audible and global Bypass is off. Enable the controls you
want or load a factory preset; opening a saved session restores its own patch.

Play a vocal through the plugin and start with the main controls:

| Control | Use it for |
| --- | --- |
| Sidechain | Crossfade the Lead branch with the host sidechain before Lead / Stack and Main Bus FX. At 0% it is all Lead; at 100% it is all Sidechain on that branch. A disabled `Sidechain?` means the blend is unavailable in the current bus or routing configuration. |
| Lead / Stack | Balancing the edited Lead against the generated voices. At 100% Stack, no Lead remains. |
| Human | Scaling irregular timing, pitch and tonal motion without overwriting stored voice offsets. |
| Width | Scaling the generated voices' stereo spread. |
| Formant | Shifting vocal resonance independently of musical pitch. |
| Global Mix | Blending latency-aligned untouched original input with the complete processed patch. |
| Output | Final level trim; double-click for unity. It is not a limiter. |

Select the **Lead** card to find **Gain Rider** (formerly Leveler). Turn on
**RIDE** and use the percentage to set the strength. The signed dB readout
shows the live cut or boost before the rack. At 0% correction is off. Gain Rider
evens out phrases rather than setting a target RMS/LUFS; it is not a limiter.
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

### Vocal Glitch

Add **Vocal Glitch** from **+ FX** or Routing. With the vocal playing, hit **Grab**
just after the sound you want. **Length** sets how much recent audio it saves;
**Fire** replays that same audio until you grab again. For exact trigger timing,
route MIDI to HEAVEN: slot 1 uses notes **36** (Grab) and **37** (Fire), on any
channel. Each subsequent slot uses the next pair, as shown in the controls.

- **Pattern:** Triplet Reverse plays three hits with the last one backward;
  Straight Ratchet plays four forwards; Reverse Pickup plays one backward;
  Octave Cut plays at half speed, then cuts in Replace mode.
- **Grid:** Immediate works without transport. The other choices wait for the
  next sixteenth, eighth-note triplet, or quarter-note boundary and require
  running host transport with beat position and tempo. Capture is not quantized.
- **Layer:** On keeps the live vocal beneath the fill. Off replaces it; set the
  slot's Mix to 100% if you want gaps to go silent.
- **Auto:** Opt in to catch phrase tails as the input falls below Threshold.
  Auto uses the same Pattern and Grid and cancels a queued fill if the vocal
  comes back before launch. It detects level changes, not syllables or phonemes,
  and doesn't overwrite your manual Grab.

Place it before long delay or reverb tails if you want Auto to follow the words.
Captured audio isn't saved in presets or sessions. Vocal Glitch adds no lookahead.

### Blend a recorded double with Sidechain

1. In your host, enable HEAVEN’s mono or stereo sidechain input and send the
   recorded double to it. The **Sidechain card** appears beside **Lead** when
   the host enables the bus. This is not cable or signal detection: a host can
   leave the bus enabled without a send or while it carries silence.
2. Turn up the **Sidechain** knob in Stack. It controls **Sidechain Blend** in
   Routing, already wired on the standard patch: Lead and SIDECHAIN feed the
   blend, then its output feeds the Lead side of **Lead / Stack**. Compatible
   older patches can be wired automatically when there is a single direct
   Lead cable to the main Mix’s DRY input. Inspect Routing for custom patches;
   automatic insertion does not replace custom Lead wiring.
3. Leave **Process** off (the default) to pass the raw sidechain through to the
   blend. Turn it on to use the card’s independent gain, pan, delay, pitch,
   fine detune, formant, and motion depth/rate. Turning **Process** off does
   not mute the sidechain or reset its controls. These edits apply only to the
   direct SIDECHAIN feed into **Sidechain Blend**, not to Lead, the generated
   voices, or the raw SIDECHAIN output used elsewhere in Routing.

With no enabled host sidechain bus, the blend passes Lead through unchanged.
Silence on an enabled bus does not trigger a fallback to Lead. **Lead / Stack**
still balances the resulting branch against the generated voices separately.

### Spectral Morph and sidechain

Morph needs **two deliberately chosen sources**:

```text
Lead / Stack or another internal branch ──> Morph MAIN  ──> Output
Internal donor OR host SIDECHAIN       ──> Morph DONOR
```

1. Add **Spectral Morph** through + FX or Routing. A new Morph connects to the
   **SIDECHAIN** source by default when the graph can accommodate the connection.
2. Route your audio to **MAIN**. For **DONOR**, feed the host's optional mono or
   stereo sidechain, or replace the default connection with another internal branch.
3. Route Morph's output to the next effect or Output. Cycles and unsupported
   serial Morph paths are rejected.
4. Start with Magnitude or Envelope mode, adjust Morph and Smoothing, then try
   **Freeze donor**. Freeze holds captured donor spectra while MAIN keeps changing;
   it is not an audio-loop recorder. Mix blends delayed MAIN and processed audio,
   not MAIN and DONOR.

A direct SIDECHAIN-to-DONOR cable still feeds raw host sidechain audio;
Sidechain card settings do not change it. An unwired/disabled donor supplies
silence, never an automatic copy of MAIN. There is no automatic donor level matching. The SIDECHAIN labels `MONO CONFIG`,
`STEREO CONFIG` and `DISABLED` describe host bus configuration, **not detected host
cables**. A configured but silent bus may legitimately analyze zero audio. Mono
auxiliary input is duplicated to L/R; stereo preserves the two channels.

Freeze requests persist in presets, but captured audio/spectra do not. A recalled
patch may show capture waiting until donor analysis exists. Reset/reprepare clears
the capture. Morph retains MAIN phase; donor-only audio does not excite a silent
MAIN. It is not a vocoder/phase-transfer mode.

### Latency and applying routing

Sidechain Blend itself adds no host latency. Neither raw blending nor the card’s
basic gain, pan or delay controls require extra compensation. Sidechain spectral
settings (pitch, detune, formant, or motion with nonzero depth, rate and Human)
require vocal-processing latency only when **Process** is on and the direct
sidechain feed can reach Output through an enabled blend set above 0%. If another
path already needs that latency, the card does not add it a second time.

An enabled Morph on a path to Output adds 4,096 samples of routing latency;
bypassed or disconnected Morph does not reserve it. Changing the Morph FFT size
does not change latency. Serial Morph dependencies that exceed the routing budget
are rejected.

HEAVEN requests host reactivation only when the total reported latency needs to
change. The host decides when to apply it; until then, the running path and its
reported latency stay aligned. Same-latency routing edits apply without reactivation.

## Modulation, presets and displays

Use **+ Mod** in Routing for LFO and Random sources. Assign supported continuous
controls using the modulation UI; discrete Morph Mode, FFT and Freeze are host
automation controls, not continuous LFO targets. The footer preset browser saves
full patches. Copy settings/Paste transfers settings between instances. Skin
cycles **Device → Neon → Spectral → Grass Block** without changing the audio
engine. Fresh instances start in **Neon** (Oxanium); **Grass Block** uses
Pixelify Sans. Saved patches keep their selected skin.

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
- **Visible wiring seems inactive:** a latency-changing edit waits for the host
  to apply the requested reactivation. If it remains pending, re-prepare the plugin;
  transport stop alone does not necessarily apply it.
- **Unexpected levels/artifacts:** reduce extreme pitch/formant/drive values,
  inspect sample peaks and trim Output. Neither Gain Rider nor merge compensation
  prevents clipping or guarantees perceived loudness.
- **Host-specific problems:** report the host and plugin format. Older release
  validation records apply only to their named builds, not to v1.14.0 or to every host.

[Open an issue](https://github.com/YukiYuziriha/plug1/issues) with version, format,
host/Windows versions, sample rate, block size and minimal reproduction steps.
Include a small preset if useful; do not upload private recordings or personal
paths. Audio examples should be yours to share.
