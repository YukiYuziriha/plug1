# Licensing and corresponding source

Vocal Stack uses the **open-source AGPLv3 route for JUCE 8.0.12**, not an
asserted commercial JUCE entitlement. The distributed CLAP/VST3 plugin is a
combined GPLv3/AGPLv3 work. It is not merely an aggregate of independent programs,
and is not offered as GPLv3-only software.

## Which terms apply

- Project-owned source remains under the existing **GNU GPL version 3** in
  [LICENSE](../LICENSE). This update does not replace that license or remove
  existing grants. File-specific notices remain authoritative.
- JUCE framework modules are used under **GNU AGPL version 3**; the full,
  unmodified GNU text is in [COPYING.AGPLv3](../COPYING.AGPLv3). The retained
  [JUCE notice](../ThirdParty/JUCE.md) describes upstream's alternative licensing;
  its commercial alternative is not the route selected for this distribution.
- **Section 13 of GPLv3 and section 13 of AGPLv3 expressly permit this
  combination.** The GPL-covered parts remain GPL-covered, JUCE's AGPL-covered
  parts remain AGPL-covered, and AGPL section 13's network-interaction
  requirements apply to the combination as described in those texts. This is
  not a blanket relicensing of Vital, MIT/BSD libraries, fonts, or JUCE's embedded
  third-party code.
- The adapted [Vital filters](../ThirdParty/Vital/README.md) and retained Vital
  reference code remain **GPL-3.0-or-later**, with their copyright, modification
  notices and upstream license. CLAP, CLAP Helpers, CLAP JUCE Extensions,
  Signalsmith libraries and the bundled VST3 SDK retain their respective notices
  in [ThirdParty](../ThirdParty). Pixelify Sans retains its **OFL 1.1** terms.
  Preserve notices within dependency sources too; the top-level notice folder
  is not a replacement for all embedded JUCE dependency licenses.

Running the plugin does not require publishing your music or DAW sessions.
Redistribution and modification carry obligations, including preserving notices,
providing the applicable license texts and providing Corresponding Source for
binaries. Do not add restrictions incompatible with recipients' rights. For
modified versions that support remote network interaction, follow AGPL section
13's prominent source-access requirement. The license texts, not this summary,
control. This is a project licensing declaration and delivery checklist, not a
legal opinion or a certification of past compliance.

Primary references: [GNU GPLv3, sections 1, 6 and 13](https://www.gnu.org/licenses/gpl-3.0.html),
[GNU AGPLv3, sections 1, 6 and 13](https://www.gnu.org/licenses/agpl-3.0.html), and
[JUCE 8.0.12's own license notice](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md).
The bundled AGPL text was downloaded verbatim from
<https://www.gnu.org/licenses/agpl-3.0.txt>.

## Current binaries and source provenance

The current **v1.11.1** binaries correspond to source commit
[`e6bd0ff28b3d103c4045cf141680e51286e4d0f3`](https://github.com/YukiYuziriha/plug1/tree/e6bd0ff28b3d103c4045cf141680e51286e4d0f3).
Download their [complete corresponding source](https://github.com/YukiYuziriha/plug1/releases/download/v1.11.1/VocalStack-1.11.1-corresponding-source.zip).
Dependency pins below are unchanged. For source bundling, pass this revision and
a version-specific output filename to the bundler command below.

### Historical v1.11.0 provenance

The **v1.11.0** binaries are associated with source commit
[`12534e87440ca33a41f12bde53956319bd06ebab`](https://github.com/YukiYuziriha/plug1/tree/12534e87440ca33a41f12bde53956319bd06ebab).
The subsequent README/showcase commit `35ab94c` did not change `source/src`,
`source/assets`, `source/cmake` or `source/CMakeLists.txt`. This licensing-only
update likewise does not rebuild or change the binary bytes. A source package
may include newer legal/build documentation, but must identify those overlays
separately from the binary-producing source revision.

[Building](Building.md) gives the toolchain and installation instructions.
[Windows-x64/SOURCE.md](../Windows-x64/SOURCE.md) belongs beside downloaded
binaries and identifies this source revision. The exact dependency inputs are:

| Input | Upstream source and exact pin |
| --- | --- |
| JUCE (including its vendored VST3 SDK and embedded libraries) | `https://codeload.github.com/juce-framework/JUCE/tar.gz/refs/tags/8.0.12`; SHA256 `dec1a8baee5aaec4502b717a421b850e257ab7e9c2efd03766185ec1957cf102` |
| CLAP JUCE Extensions | `https://github.com/free-audio/clap-juce-extensions.git` at `6bde108329ff9fc24404b79b8b397261a75cf6a8` |
| CLAP (extension submodule) | `https://github.com/free-audio/clap.git` at `29ffcc273be7c7c651f6c9953b99e69700e2387a` |
| CLAP Helpers (extension submodule) | `https://github.com/free-audio/clap-helpers.git` at `a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc` |
| Signalsmith Stretch | `https://github.com/Signalsmith-Audio/signalsmith-stretch.git` at `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` |
| Signalsmith Linear | `https://github.com/Signalsmith-Audio/linear.git` at `7bb1c8f36794d6980080ce817c91f8da8fe56a66` |
| Vital subset and adapter | Retained editable sources and modifications in this repository; upstream `636ca0ef517a4db087a6a08a6a8a5e704e21f836`, as documented in `ThirdParty/Vital` |
| Embedded font | `source/assets/PixelifySans.ttf`, with `ThirdParty/PixelifySans-OFL.txt` |

Use the **entire JUCE archive**, not just its module headers or notice file.
The extension's two submodule sources must also be included. Stretch's unused
submodule is intentionally disabled in CMake; Linear is fetched separately.
All project CLAP patches and their CMake application scripts must accompany the
editable sources. General-purpose unmodified tools and System Libraries are
excluded only to the extent allowed by section 1; do not omit a required
project-specific dependency on that basis.

## Preparing a complete source bundle (no plugin build)

`tools/Bundle-Corresponding-Source.py` requires Python 3.12+, Git, network access
to the public JUCE archive, and already populated Git dependency caches. It reads
Git objects at the pins, not modified/generated dependency working files, and
recursively exports submodules at their recorded gitlinks. It includes the full
JUCE source archive after verifying CMake's SHA256, project sources, font,
patches, tests, notices and tracked build tools. It excludes compiled plugins,
Git metadata, credentials and ignored machine-local helpers. Current legal/docs
and the bundler are explicitly recorded as packaging overlays. ZIP entries use
fixed timestamps; `SOURCE-MANIFEST.json` records input pins and per-file hashes.
This does not promise bit-identical compiler output across toolchain versions.

From the repository root, after the ordinary dependency-fetching configure if
caches are not already populated (see [Building](Building.md)):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python tools/Bundle-Corresponding-Source.py --revision 12534e87440ca33a41f12bde53956319bd06ebab --output source/build/VocalStack-1.11.0-corresponding-source.zip
```

`--deps-cache` can point to another configure tree's `_deps` directory. Export
fails if tracked/untracked non-ignored project source or `ThirdParty` differs
from the selected revision, a required Git object/submodule is absent, or JUCE's
archive hash is wrong. It refuses to overwrite an existing bundle. A sibling
`.zip.sha256` is produced. Review the manifest and test extraction before upload;
a generated bundle is not automatically published or a legal compliance verdict.

## Release assets and redistribution

The prepared release tag is **`v1.11.0`**. README and the binary package's
`SOURCE.md` link directly to these asset URLs:

- [Windows x64 CLAP/VST3 package](https://github.com/YukiYuziriha/plug1/releases/download/v1.11.0/VocalStack-1.11.0-Windows-x64.zip)
- [Complete current corresponding source and dependency sources](https://github.com/YukiYuziriha/plug1/releases/download/v1.11.0/VocalStack-1.11.0-corresponding-source.zip)
- [Complete repository history and historical source/binary/dependency matrix](https://github.com/YukiYuziriha/plug1/releases/download/v1.11.0/VocalStack-source-history.zip)
- [SHA256 checksums](https://github.com/YukiYuziriha/plug1/releases/download/v1.11.0/SHA256SUMS.txt)

These are publication targets, not a claim that this packaging tool uploaded
anything. The maintainer uploads the local prepared files to this release and
verifies download access when making the release/repository public. This uses
GPLv3/AGPLv3 **section 6(d)** equivalent source access at no extra charge, not a
future source-on-request promise. GitHub's automatic repository ZIP is not the
complete source asset because it omits fetched dependencies.

[Historical source restoration](Historical-Source.md) documents the audit of all
44 reachable commits and 56 unique binary blobs. The external pins are identical
throughout that history, so the history ZIP **together with** the current complete
source ZIP includes every historical project revision and all required pinned
external sources. The matrix records co-delivered binary hashes and source trees,
not a forensic claim about each historical compilation. The Git bundle preserves
the original history, including earlier sources when unchanged binary bytes were
carried forward. Adding notices/source packages now does not certify past
compliance or alter third-party rights.

`tools/Bundle-Release-Assets.py` prepares the three ZIPs, individual checksums and
`SHA256SUMS.txt` under `source/build/publication` by default. See the historical
source guide for its half-CPU-wrapped command. It verifies the history bundle by
restoring it and comparing every audited commit/source tree. The current source
ZIP includes final publication documentation as separately identified overlays.
The Windows ZIP contains the entire `Windows-x64` tree, GPLv3/AGPLv3 texts, font
license, complete retained third-party notices, source URLs and accompanying
licensing/build/installation instructions; the plugin bytes remain unchanged.

Keep the source assets available alongside current and historical binary
downloads, and preserve these notices/licenses on redistribution. Runtime
legal-notice obligations remain governed by the licenses; this docs/package-only
change neither adds an About UI nor certifies runtime legal-notice behavior.
No plugin rebuild, historical rebuild, listening test or offline configure/build
was performed as part of packaging. Bundle restoration, file integrity and
unchanged binary bytes are validation of source delivery, not claims of
byte-identical compiler output.
