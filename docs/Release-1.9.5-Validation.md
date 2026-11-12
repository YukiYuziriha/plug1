# Release 1.9.5 — built, tested, packaged and installed

## Status and evidence boundary

The UI changes have a successful **full Release 1.9.5
build** and **34/34 focused CTests passed in 225.47 s**, including **68 editor
groups**. Initial editor validation passed **68 groups in 90 s**. Independent
static review found **no blockers**.

These results and the screenshot observations below are the supplied release
validation evidence; this documentation update did not rerun builds or tests.
**Package refresh, packaged CLAP smoke and installation are complete.** On retry,
`source/build/install-release.ps1` reported **Installed and SHA256 verified for
both per-user CLAP and VST3**, resolving the initial installed-CLAP file lock.
Release source and matching packages were committed as **`e9f2b48`** and
successfully pushed to **`origin/main`**. Installation success is not actual DAW
listening or performance approval.

## Implemented UI scope

### Long FX Mix bars and spacing

- Long FX Mix controls use flat **25 px textured bars** with theme-colored,
  coherent mineral noise at a coarse **3 px** scale, animated through a **15 Hz
  cache**. An exact value marker remains distinct from the separate upper
  modulation lane.
- Compact voice sliders and knobs are unaffected by the long-bar treatment.
- FX-only layout uses **68 px control cells plus 2 px gaps**, reduced group
  gutters, and tighter switch/dropdown gaps.
- **94 px Mix rows** reserve space for the **25 px bar**, **28 px caption**, value
  and upper modulation lane.

### Whole-plugin fluid backgrounds

- Root, canvas, routing and FX surfaces use a coarse fluid background with a
  **maximum 40-cell grid dimension**. Grid proportions follow the frame; features
  grow with the frame rather than becoming denser as it grows.
- A slow **10 Hz cache** supplies the animation. Tint is only slightly themed,
  with saturation capped at **0.18** and brightness matched, then blended at
  **12%**. The original base remains dominant; this is not a vivid gradient.
- Animation reuses the **existing 30 Hz editor timer**, with **no new timer**.
  The animation clock pauses while hidden, and root repaint runs at **10 Hz**.
  These are implementation cadences, not measured native FPS or CPU results.

This is **UI-only**: no DSP or host parameter changes.

## Observed validation

| Check | Result |
| --- | --- |
| Initial editor validation | 68 groups passed in 90 s |
| Full Release 1.9.5 build | Passed |
| Focused CTest suite | 34/34 passed in 225.47 s, including 68 editor groups |
| Independent static review | No blockers |
| Screenshot review | Neon Reverb rack, Spectral Granular, and Neon/Device full editors reviewed; granular presentation stable |

Screenshot review establishes only the inspected visual presentation. It is not
an actual DAW listening test, host approval, or a native FPS measurement. The
**long legacy DSP suite was not rerun** for this UI-only change.

## Packages, smoke and installation

Release CLAP/VST3 artifacts were copied to tracked `Windows-x64`, and
`VocalStack-FX-Tabs.png` was refreshed. Package binary SHA256:

| Package binary | SHA256 |
| --- | --- |
| `Windows-x64/CLAP/Vocal Stack.clap` | `4397e0302fce34e55443668e1d550b62a0013cf1b777f23fb0a556e065a98dd3` |
| `Windows-x64/VST3/Vocal Stack.vst3/Contents/x86_64-win/Vocal Stack.vst3` | `664aa27f36647549a2e9b0440d3b162d1b14d711359a53b10874297f369899a5` |

**Packaged CLAP smoke passed:**

- **2087 automatable parameters at original indices**.
- Stereo at **48 kHz**, mono-to-stereo at **44.1 kHz**, and **1344-sample latency**.
- Mono at **96 kHz** with **2688-sample latency**.
- State, latency, bypass, automation and offline-layout checks.

**Installation retry succeeded:** `source/build/install-release.ps1` reported
**Installed and SHA256 verified for both per-user CLAP and VST3**. **1.9.5 is now
installed.** The initial preflight block is **resolved**: the installed CLAP had
been in use, and that first attempt changed no installed files in either format.

## Delivery checklist

- [x] Full Release build and all 34 focused CTests.
- [x] Independent static review and screenshot review.
- [x] Refresh tracked Release CLAP/VST3 packages and record SHA256 identities.
- [x] Refresh `VocalStack-FX-Tabs.png`.
- [x] Run packaged CLAP smoke against the refreshed artifact.
- [x] Install both per-user formats and verify their SHA256 identities: retry
  reported **Installed and SHA256 verified** for CLAP and VST3; initial lock resolved.
- [x] Commit and push the release with matching packaged binaries: `e9f2b48` on
  `origin/main`.
- [ ] Actual DAW visual/listening, transport, automation/session recall and
  native FPS checks.

Packaging, smoke, both SHA256-verified per-user installations and commit/push are
complete (`e9f2b48` on `origin/main`). The initial installation lock is resolved;
1.9.5 is installed. Actual DAW checks remain unverified. Do not terminate Bitwig
or overwrite a loaded plugin to force installation.
