# Vocal Stack 1.9.8 validation

## Presentation changes

- Embedded unmodified Pixelify Sans for pixel-inspired headings, labels, buttons,
  inspectors and plot captions. This is inspiration from readable pixel-game UI,
  not Minecraft artwork or a Minecraft font. Existing plain numeric readouts stay
  plain for small-size readability and stable value boxes; no moving layouts.
- Shared ParameterKnob unit policy: whole percentages, Hz and ms; one decimal for
  other units, preserving explicit pan, balance and panel-specific formatters.
  This is a focused common-control cleanup, not a rewrite of every plot readout.
- Progress arcs are 1.6x their prior thickness: shared knobs 3 -> 4.8 px,
  main Stack knobs 3.2 -> 5.12 px. Existing track geometry and alignment stay put.
  Arc material is generated through the existing cached pixel surface and clipped
  to the stroke. Secondary effective dots move outside the base arc and disappear
  when effective and base values coincide; the base pointer remains authoritative.
- Buttons retain 1.9.7 cached material, state colours, theme-specific silhouettes
  and borders, with 55% background contrast. Arc contrast is 45%; bar noise is
  quieter too. Surrounding panels retain their stronger texture.
- Extremely rare fixed pixel seeds receive accent-colour glints within generated
  image pixels, never an overlay/component. Slow smooth envelopes avoid random
  frame-to-frame flicker. The ordinary texture remains neutral; tests bound the
  saturated-highlight area below 0.2% and channel spread to 25.
- Simpler primary-control tooltips and a shared drag/reset/type hint for parameter
  knobs. Baselines, value box sizes, existing radii and borders are preserved,
  avoiding a broad redesign. Mix caption remains passive.
- No changes to DSP, processor implementation, host parameter definitions,
  state/preset format, audio routing or keyboard-focus policy.

## Font provenance and license

- Pixelify Sans, copyright 2021 The Pixelify Sans Project Authors.
  Upstream: https://github.com/eifetx/Pixelify-Sans
- Retrieved unmodified from Google Fonts on 2026-11-13:
  https://raw.githubusercontent.com/google/fonts/main/ofl/pixelifysans/PixelifySans%5Bwght%5D.ttf
- Font SHA256: `9ba86cd010a4de309d263ceff8e8044092c9db7efda869620cb9ff1c4389e8a5`.
- SIL Open Font License 1.1 permits embedding and redistribution. Full copyright
  and license: `ThirdParty/PixelifySans-OFL.txt`, also distributed as
  `Windows-x64/PixelifySans-OFL.txt`. Font source: `source/assets/PixelifySans.ttf`.
  No font installation or runtime network request is needed.

## Observed validation

- Full Release build succeeded for affected CLAP, VST3 and test targets. Initial
  200-second commands timed out during JUCE rebuilding; bounded incremental
  resumes completed successfully. Final build log: source/build/release-198-final3.log.
- Final parallel CTest run excluding only legacy VocalStackDSP: 33/34 passed,
  178.07 seconds. The editor suite failed `Host bypass left folded node selected`.
  Standalone final editor rerun passed all groups in 91.89 seconds. An earlier
  run also encountered a cursor-menu anchor timing failure; these are reported
  rather than hidden. A previous full 34/34 run passed before the final tooltip
  and coincident rotary-dot refinements (225.23 seconds).
- Focused pixel tests verify embedded font identity, calm contrast, deterministic
  phase/cache behavior, bounded coarse pixels, sparse muted glints, animated
  buttons, active state and passive focus across all three themes.
- Device, Neon and Spectral Stack captures visually reviewed after direct font
  binding: pixel text visible, value columns readable, no obvious clipping,
  thicker arcs and existing plots retained. Final captures are machine-local in
  source/build/stack-material-198-{0,1,2}.png. Not every inspector was manually
  reviewed; broader layout coverage comes from the editor regression groups.
- Packaged and installed CLAP smoke passed: 2087 parameters at original indices;
  stereo 48 kHz and mono-to-stereo 44.1 kHz latency 1344 samples; mono 96 kHz
  latency 2688 samples; state, bypass, automation and offline layouts passed.
- Both tracked Windows-x64 binaries copied from successful Release artifacts.
  Existing install-release.ps1 reported Installed and SHA256 verified for both
  per-user formats. No host was terminated.
- Legacy DSP suite, actual Bitwig listening/transport and native FPS not rerun.

## Packaged SHA256

- CLAP: `7046d99a066a9e26f12141234d15345ea1a4d016e227ddc45296df90589bd527`
- VST3: `58c4dbc20e3c5ddf629388e66e70df16bb18657030e7d89bd14840a384f1aae0`
