# HEAVEN 1.12.2 validation

The pre-tuning processor is restored, with HEAVEN branding and larger UI text. Fresh instances and Reset now have no FX; legacy saved graphs are retained. Stack FX appear under the voices and size to the selected effect. Host labels for numbered Mix and Active controls no longer say Chorale. Legacy parameter identities remain unchanged.

Within a saved patch lineage, deleting an effect retires its automation slot rather than assigning that slot to a different effect. The 15 numbered slots can be exhausted; Undo can restore an original effect. Loading a different patch is outside this guarantee. Legacy saves cannot reconstruct slot ownership for effects deleted before this version.

Focused checks on the final build: Stack sizing fit 15 of 16 effect types without inner vertical scrolling in each of the three skins; processor automation ownership passed, including deletion, recall and older same-lineage snapshots; CLAP and VST3 smoke tests passed. These are not Bitwig session tests. The full Editor suite was not rerun for the final FX changes.

Both Windows Release binaries were refreshed and installed. Installed SHA-256 hashes matched the delivered files. The previous installation was backed up at `.cache/installed-before-1.12.2-20261121-193056`.

