# HEAVEN by lil grass block v1.12.1: licenses and corresponding source

These CLAP/VST3 binaries combine GPLv3-covered project/Vital/MXTune-derived code
with JUCE 8.0.12 under **AGPLv3**, using section 13 of both licenses. GPL-covered
parts retain GPLv3 (the tuner adaptation is GPL-3.0-or-later); third-party
components retain their own terms. This is not a GPLv3-only binary or a claim of
a commercial JUCE license.

Keep `LICENSE.txt`, `COPYING.AGPLv3`, `PixelifySans-OFL.txt`, this file and the
complete `ThirdParty` folder with redistributed copies. The software is supplied
without warranty, as detailed in those licenses.

## Current source and provenance

Project source, patches and build scripts:
<https://github.com/YukiYuziriha/plug1>

The current binaries and corresponding source are co-delivered in the repository.
`BUILD-PROVENANCE.json` records the actual source-input hashes and binary hashes
from the successful Release build. It explicitly identifies a working-tree
build: its base commit is **not** claimed to be the binary-producing revision.
The later delivery commit contains those source files and packaged binaries
together. See [the validation record](../docs/Release-1.12.1-Validation.md).

The complete local release assets are named:
- `HEAVEN-1.12.1-Windows-x64.zip`
- `HEAVEN-1.12.1-corresponding-source.zip`
- `SHA256SUMS.txt`

The corresponding-source archive includes the full pinned JUCE source, CLAP
extensions and enabled submodules, Signalsmith Linear/Stretch, retained Vital
and MXTune-derived sources and notices, the font, build scripts and the
reproducible VST3 latency patch. Build and licensing details:
[Building](../docs/Building.md) and [Licensing](../docs/Licensing.md).

Local archive creation and a repository push do not mean a GitHub Release has
been published. Do not substitute an older release's project source for this
version's changed source and wrapper patch. Historical release downloads remain
listed in the repository README; they identify historical bytes only.
