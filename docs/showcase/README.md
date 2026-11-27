# HEAVEN showcase

[Product overview](../../README.md) · [Getting Started](../Getting-Started.md)

## v1.15.1 — Routing and Grass Block

- [Neon Routing](neon-routing.gif): Lead passes through Granular to Output. The cyan wet path adds Delay and Reverb between Granular’s **Wet Out** and **Wet Return**.
- [Grass Block Stack](grass-block-stack.gif): Doubler, voice cards and live input/output meters in the pixel-style skin.

Both loops are six-second offscreen captures of the production editor at **Motion 100%**, rendered at 20 frames per second with generated PCM. They show the interface, not native frame rate or DAW audio performance. The machine-local preset-path footer is cropped out.

## Earlier effect demos (v1.11)

These are existing production-component captures from the v1.11 work included in
the delivered release. The GIFs are copied without resizing, re-encoding or
retiming. They process real **generated PCM**, not a singer recording; this is a
visual workflow showcase, not an audio-quality demonstration. Component-frame
sequences are not desktop screen recordings, native FPS measurements or evidence
of DAW listening/transport compatibility.

## What each view shows

| File | Surface | Interpretation |
| --- | --- | --- |
| [stack-1.11.0.png](stack-1.11.0.png) | Full Stack editor context, Neon | Current coordinated main controls, meters and selected Morph rack. The voice cards are below the visible area; this is not an eight-card stack screenshot. |
| [routing-1.11.0.png](routing-1.11.0.png) | Full Routing editor context, Neon | Actual MAIN/DONOR wiring: Lead through the main mix into MAIN, configured SIDECHAIN into DONOR. The host configuration label does not prove a connected DAW track. |
| [spectral-morph.gif](spectral-morph.gif) | Stack Main Bus FX, Neon | Measured MAIN/DONOR/result analysis spectra with actual donor Freeze capture/hold/release. Spectra are pre-Mix and pre-Output, not final output metering. |
| [reverb.gif](reverb.gif) | Stack Main Bus FX, Neon | Measured input/wet history and tail over the decorative material. Foreground decay/swell guides are parameter models, not a measured IR. |
| [compressor.gif](compressor.gif) | Stack Main Bus FX, Neon | Actual four-second DSP gain-reduction history plus a separate static transfer model. |
| [stereo.gif](stereo.gif) | Stack Main Bus FX, Neon | L/R activity/correlation and deliberate Mono Audition changes; this is not a DAW interaction recording. |

The stills retain the existing 1280-pixel editor width. Only the bottom 20-pixel
machine-local preset-folder footer was cropped for publication; no controls,
wiring, meters or audio data were composited or invented. The selected Morph
fixture uses custom auxiliary routing, so its rack correctly says add/remove/
reorder is unavailable. Use Routing for those topology edits.

## Capture provenance

The following are **local capture identifiers**, not public download links. Public
media live here in `docs/showcase`, independent of ignored build folders.

| Public file | Original local capture |
| --- | --- |
| `stack-1.11.0.png` | `approval-1.11.0-coordinated/frames/spectral-morph-rack-neon-context.png` |
| `routing-1.11.0.png` | `approval-1.11.0-coordinated/frames/spectral-morph-inspector-neon-context.png` |
| `spectral-morph.gif` | `approval-1.11.0-coordinated/spectral-morph-rack-neon.gif` |
| `compressor.gif` | `approval-1.11.0-coordinated/compressor-rack-neon.gif` |
| `stereo.gif` | `approval-1.11.0-coordinated/stereo-rack-neon.gif` |
| `reverb.gif` | `approval-1.11.0-readability/reverb-neon-stack-row.gif` |

The Reverb readability capture is the updated fine measured-field presentation,
not the older node-gallery view. The coordinated captures supply the later Morph,
Compressor and Stereo revisions. See [v1.11 technical notes](../Preview-1.11.0.md)
and the [release record](../Release-1.11.0-Validation.md) for the evidence boundaries.
The known native desktop-hit/occlusion blocker remains a limitation; inclusion in
this showcase does not turn it into a passing native-input test.
