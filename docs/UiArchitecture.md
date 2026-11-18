# HEAVEN by lil grass block — UI architecture: focused consolidation

HEAVEN by lil grass block is the product formerly named VocalStack (displayed
as Vocal Stack). This is not a new plugin ID: internal IDs, preset folders and
state extensions remain unchanged for compatibility. Internal CMake targets,
including `VocalStack` and the test targets below, retain their names.

This revision preserves the existing visual design, host parameters and DSP.
It corrects shared behavior rather than introducing a new component framework.

## Ownership rules

- `Processor` owns parameters, validated state changes and audio/UI publication.
  Controls edit base values with host gestures; operational visuals consume
  coherent effective-state snapshots. UI components do not own live DSP.
- `UiControlPolicy.h` owns passive idle-focus and nested viewport reveal policy.
  Deliberate transient `TextEditor` subtrees are exempt from the idle sweep.
- `PassiveNumericSlider` reapplies that policy when enablement, text-box style or
  look-and-feel recreates numeric labels. `ModulationSlider` adds assignment
  behavior; modulation-source controls use the non-assignable numeric base.
- `modulationui` owns assignment interception and discrete assignment menus.
  `ChoiceDropdown`, Utility configuration and Tuning Formants reuse that policy.
  Latency-affecting Tuning Enabled remains prohibited as an assignment target.
- Editor coordinates page-aware reveal; panels retain their local tab/band
  knowledge. `revealThroughViewports` makes the final target reachable through
  inner and outer scroll containers without moving already-visible targets.

## Routing document versus preferences

Routing history must not be invalidated merely because the window, divider,
feedback source or another presentation preference changes. Routing now compares
its document-relevant state separately from the entire persisted layout tree.
Actual external graph/node-layout replacement still invalidates incompatible
history. Explicit layout-only history can follow a newer graph where its node
layout remains compatible, without restoring unrelated parameter automation.

`selectedModulationId` is the authoritative feedback-source selection, with
legacy migration. It is distinct from active Assign mode. Rebuilding a view must
not overwrite a newer shared selection from an obsolete local copy.

Inspector replacement cancels edits before destroying controls/attachments and
clears its dragging latch. A topology-dependent Chorale/connection inspector
subscribes to the relevant host parameter and validates selection on notification,
without relying on a timer tick. That callback reads the parameter's current
value because the APVTS raw mirror may not yet reflect the notification.

## Feedback versus editing

Crossover response/enable/band-count feedback uses one retained coherent
modulation snapshot. Tuning base-note editing remains independent of the
operational allowed-note mask. Invalid/inconsistent snapshots retain the last
coherent display state rather than combining values from different revisions.
The Tuning history is labeled recent snapshots, not a measured six-second trace.

## Tests

`VocalStackUiArchitectureTests` is registered as `VocalStackUiArchitecture` in
CTest and shares the native-GUI resource lock. Its ten cases exercise routing
history, inspector gesture teardown, notification-driven selection, passive focus
across Sync changes, deliberate numeric commit/cancel, nested wheel routing,
discrete assignment/no-base-write behavior, effective-state displays and reveal.
Existing editor, rack, tuning, modulation and host tests remain in place. See
[the delivery record](Release-1.12.0-Validation.md) for observed results and the
remaining two editor-suite failures. Native tests enumerate displays and use the
non-primary monitor where available; offscreen snapshots are labeled separately.

## Deliberately deferred

- A broader shared effect-panel factory/edit-history coordinator: existing
  surfaces still have some construction and edit-hook duplication.
- Visibility-aware telemetry scheduling throughout all panel families. Hidden
  refresh cost needs measurement before changing scheduling contracts.
- A single top-level page enum, independent per-page scroll anchors and pinned
  navigation chrome. These affect workflow/layout and are not needed for the
  concrete reveal bug fix.
- Full native transient-owner activation/dismissal policy across dropdowns and
  expanded windows, including non-JUCE host windows and other applications.
- Changing named-scale/custom-mask semantics. That is a separate musical/state
  policy decision, not an architecture refactor.

No new visual redesign, installed-plugin replacement, commit or push is implied.
