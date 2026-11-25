# SIDECHAIN / Spectral Morph integration (v1.11.0 archive)

For current [Sidechain Blend and Spectral Morph setup](Getting-Started.md#blend-a-recorded-double-with-sidechain), use Getting Started. The validation and fixed-reserve design below describe the v1.11.0 preview, not v1.14.0.

The initial source-only checkpoint is superseded by the implemented, Release-built
**1.11.0 source preview**. The parent explicitly completed the visual handoff;
there is no remaining visual-agent ownership/build wait.

See [Preview-1.11.0.md](Preview-1.11.0.md) for the authoritative implementation,
latency policy, validation chronology, native input blocker, host coverage,
performance costs, all-theme GIF/screenshot paths and unchanged installed hashes.

## Integration map

- `PluginProcessor`: optional bus 1, explicit main-bus channel counts/views,
  separate auxiliary processing pointers, append-only parameters/state/modulation,
  coherent telemetry publication and host latency reporting.
- `VocalEngine`: chunk capture before aliased output writes, external source-engine
  alignment, fixed routing reserve and aligned global dry/bypass.
- `Routing.h`: first-class SIDECHAIN, named generic auxiliary pins, independent
  MAIN/DONOR dependencies, prepared per-wire/output compensation, bounded graph
  publication/reclamation and explicit rejection beyond the 4096-sample budget.
- `SpectralMorph.h`: three warm WOLA resolutions, independent donor analysis,
  magnitude/envelope processing, capture/hold/rearm/release and fixed latency.
- `SpectralMorphBridge.h`: exclusive-owned bounded snapshots; no live UI DSP read.
- `SpectralMorphVisual.h`: shared three-spectrum material, host-bound controls,
  real freeze/unrouted state, standalone/rack/expanded Routing presentation.
- `ReactiveMaterial.h`, `ReactiveFxVisualModels.h`, `CreativeEffectVisual.h`:
  visual-agent handoff integrated and tested without discarding the pending
  1.10.1 measured-history work. See `Visual-System-Notes.md` for its provenance.
- `PatchClapSidechain.cmake`: pinned wrapper optional-bus layouts; legacy IDs 0–2
  preserved, mono/stereo sidechain groups appended. VST3 uses JUCE's auxiliary bus.

Full non-editor validation passed 41/41 and isolated granular UI passed. The full
editor run passed 77/78; the native desktop-occlusion click precondition remains
a real, unskipped blocker (earlier LockScreenInputOcclusionFrame; final run
Chrome_RenderWidgetHostHWND). Final affected-source checks
passed 9/9 and all-theme real-PCM compact/expanded/rack rendering passed.

**No installation, package replacement, commit or push.** Existing packaged and
installed 1.10.0 hashes are unchanged. Actual DAW listening/transport remains
unperformed; source preview validation is not a musical-quality claim.
