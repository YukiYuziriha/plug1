# Vocal Stack 1.9.6 validation

## Scope

UI-only release; no DSP or host parameter changes.

- Background grid longest edge: **80 cells, up from 40**, giving approximately
  half-sized pixels at the same surface size. Denser multiscale fluid noise and
  grain produce stronger brightness variation while retaining muted theme tint:
  saturation cap **0.18**, tint blend **0.12**, not a vivid gradient.
- Long FX Mix bars: denser noise with **2 px texels, down from 3 px**.
- A normal-font **`Mix NN%`** caption sits inside the **25 px bar**, with reverse
  contrast over the filled portion. Mix rows shrink from **94 px to 44 px**.
- The caption is a nonclickable label with mouse passthrough. Clicking or dragging
  over the text adjusts Mix; it does not open numeric entry or take keyboard
  focus. Numeric entry for all other knobs and voice controls is unchanged.

## Observed validation

- Full Release build succeeded.
- **All 34 focused CTests passed in 161.06 s**, including **68 editor groups and
  rack UI**.
- Reviewed Neon Reverb and Spectral Granular rack images plus Neon and Device
  full-editor images. These are image checks, not actual DAW validation.
- Release CLAP and VST3 artifacts were copied into tracked `Windows-x64` packages.
- `VocalStack-FX-Tabs.png` was refreshed.
- Packaged CLAP smoke passed:
  - **2087 parameters at original indices**.
  - Stereo at **48 kHz**.
  - Mono-to-stereo at **44.1 kHz**, **1344-sample latency**.
  - Mono at **96 kHz**, **2688-sample latency**.
- `source/build/install-release.ps1` reported **Installed and SHA256 verified**
  for **both per-user CLAP and VST3**.

## Packaged SHA256 identities

Hashes were also checked directly against the tracked package files during this
documentation update.

| Format | Tracked file | SHA256 |
| --- | --- | --- |
| CLAP | `Windows-x64/CLAP/Vocal Stack.clap` | `d4b9f87ebba2857ff5e01e60be17512760d1103bad1e20b4f7d0dc6dc3e3a3f7` |
| VST3 | `Windows-x64/VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3` | `1d11b2e29e795a3a32e620e1b894122eda562ec9a4e6447a21e60aab0d9d5a9c` |

## Remaining checks and delivery

- Actual DAW visual, listening, transport, automation, recall and native FPS checks
  remain **unverified**.
- The long legacy DSP suite was **not rerun**.
- Release commit and push with matching packages are **pending next**.
