# Main Bus FX rack integration and tests — 1.9.0

## Finalized shared Routing bridge

`PluginEditor.cpp` connects the rack to:

```cpp
bool RoutingPanel::commitExternalGraphEdit(const Graph&, juce::String& error);
```

The bridge is implemented; there is no processor-only fallback or second undo
history. It refreshes graph/layout from the processor before recording edits,
including while Routing is hidden, validates candidates, retains surviving
parameter bases, records one existing `EditState`, and uses the existing
publication/rejection path. Inspector/cards/selection/buttons refresh together.

Undo/Redo restores lifecycle parameter banks and modulation assignments for
removed/replaced identities while merging live unrelated state: surviving
assignments and current modulation sources are not overwritten by an old full
snapshot. Freshly allocated/reused slots do not inherit retired mappings.
All removed effect cache IDs are purged, not only granular IDs, preventing old
tails/history from resurrecting on immediate re-add before audio installs removal.
Surviving ID/type pairs retain DSP history during reorder; restoring a deleted
machine's controls is not restoration of its old audio tail.

Rack topology edits affect only the discovered unambiguous main Mix → serial FX
→ Output segment. Branched/custom paths refuse destructive edits; identifiable
FX remain editable. Duplicates share the existing node/FX capacity. Structural
Splitter and Multiband are excluded. Parameter/layout refresh does not republish
topology.

## Registered coverage

`source/CMakeLists.txt` registers `VocalStackMainBusFxRack` and
`VocalStackMainBusFxRackUi`. `RackUndoIntegrationTests.h` is dispatched through
the editor runner, not left as a proposed integration step.

- Backend: default/empty chains, duplicate Granular, stable IDs/settings, reorder
  and no-op reorder, remove/reuse, shared capacity, per-voice exclusion, side-input,
  side-output, parallel/custom refusal, ambiguous anchors and structural exclusion.
- UI: real host-bound controls, independent duplicates, no topology writes from
  refresh/layout, external reorder preserving cards/values, editable ambiguous
  graphs, session recall, three-theme captures and replacement of fixed Chorale.
- Shared editor integration: cross-view one-step Undo/Redo, lifecycle parameter
  banks, modulation removal/replacement/reuse and merging unrelated live mappings.

## Observed status

Final Release CLAP/VST3 builds and **all 34 focused CTests passed in 146.69 s**,
including eight factory shards and 63 editor groups. Separate legacy DSP passed
in 81.8347 s. The prior 33 screenshots plus 12 rack PNGs across three themes were
visually inspected; a subsequent test-only screenshot-output addition passed the
GrainSnapshotUi rerun in 0.60 s, producing three actual-grain PNGs. The new root
`VocalStack-Stack-FX.png` shows the same-node rack; Granular imagery is refreshed.

Tracked packages were copied from Release artifacts and SHA256-matched. Packaged
CLAP smoke passed 2087 parameters, mono/stereo latency, bypass and automation.
**Installation blocked:** installed CLAP locked by another process; no installed
files changed and 1.8.0 remains installed. Actual Bitwig listening/transport,
automation/recall and install retry remain open. Commit/push is coordinator-owned;
no completion or hash is claimed. See `docs/Release-1.9.0-Validation.md` for package
identities and performance limitations. This documentation update ran no
builds/tests and made no commit.
