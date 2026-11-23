# HEAVEN 1.13.3 — automatic Morph activation and Granular wet Delay

## Changes

- Accepted latency-changing routing edits and live state recall now send a host latency/activation notification outside the processor control lock. Running audio and reported latency stay unchanged until host preparation applies the new graph.
- CLAP forwards this to the host's restart request. The VST3 wrapper forwards same-latency activation requests and suppresses recursive notifications during activation. The host still owns safe scheduling; no plugin kills/relaunches or forced audio-thread graph rebuilds.
- Newly spawned Morph nodes in Routing and the Stack rack create/reuse SIDECHAIN and connect DONOR automatically. Explicit later donor changes/disconnections are preserved. An already occupied Sidechain output is not stolen; use an explicit Splitter to share it.
- External donor wiring no longer disables safe serial rack removal/reorder. Removing Morph removes its donor wire, preserves the donor source, and bridges its MAIN neighbours. Ambiguous main-chain branches remain protected.
- Routing-only Granular wet pocket: its expanded node has **Wet pocket / Delay**, also available in its context menu. This creates an independently automated Delay node labelled **WET DELAY / GRANULAR N**, internally processed before Granular Mix. It needs no canvas cables. The pocket starts fully processed; its own Mix and bypass remain independent. Granular at fully dry silences all pocket output, including Delay tails.
- The first pocket deliberately supports one Delay in Granular. Other parent/insert types, longer chains and nesting remain planned, not silently accepted. Unsupported ownership/cables/latency are rejected. Deleting Granular deletes its owned insert; deleting the insert leaves Granular intact.
- Pocket ownership persists in graph/preset state, defaults absent for older sessions, consumes ordinary FX/node capacity, and preserves existing automation identities. No host parameters added.
- Fat textured Mix rails use 1:1 relative horizontal mouse travel. Compact sliders retain half-speed travel, deliberate fine modifiers and passive host-keyboard behavior remain.

## Observed validation

- Release CLAP/VST3 and focused processor/rack/wet-pocket test targets built through `Invoke-HalfCpu.ps1`, restricted to six of twelve logical processors.
- `vocalstack_wetpocket_tests`: passed deterministic stereo/dual-mono tests at 44.1/48/96 kHz and irregular chunk boundaries. Wet pocket matches a fully-wet Granular→Delay serial reference; partial Granular Mix blends the complete pocket; zero Mix has no Delay leak; insert bypass matches plain Granular; silence remains zero; finite output; no audio-thread allocation/reclamation observed.
- `vocalstack_mainbusfxrack_tests`: passed automatic donor, safe external donor edits/removal, capacity, identity and ambiguous-branch restrictions.
- `VocalStackSidechainProcessorTests --workflow-check`: passed host-notification addition/removal/state-recall tests while preserving prepared latency; ownership round-trip/cascade deletion; node pocket creation/navigation and actual 1:1 Mix dragging across three themes. GUI fixtures used the enumerated non-primary 1920×1080 monitor at 96 DPI, work area `(3440,357,1920,1032)`.
- CLAP smoke passed bus/rate/state/latency/bypass/automation and donor checks, now explicitly asserting a host restart request for pending Morph and no restart loop during activation.
- VST3 native component host: 108 bus/rate/Morph/dry/bypass cases passed. This validates component processing/latency, not Bitwig's actual controller-driven restart scheduling.

## Local delivery

Both HEAVEN CLAP and VST3 installed successfully; installer preflight and SHA256 checks passed, with prior files backed up. Packaged/installed hashes:

- CLAP: `bff0a932f9938800c4103f3cbd44682311f7f2577b500cbba96d7da33865b2ad`
- VST3: `a31c2318a610026e459ab1721d1260dd913b92ffa1664a3fc7829b5cf9fc0b6b`

## Existing validation blockers / scope

The broad editor target currently fails to compile because two older tests reference the removed static `MainBusFxRackPanel::preferredHeight` after the rack changed to width-dependent height. The default processor suite also stops at an older Gate fixture that tries to replace a Stereo slot identity. These were not weakened or reported as passing. Focused workflow tests run independently of those failures.

No musical-quality, native FPS or completed Bitwig-host reactivation claim. Actual host scheduling must be checked in the user's DAW. Native-mounted component snapshots are not desktop video recordings. Public release archives/publication are separate from the requested repository push and local installation.
