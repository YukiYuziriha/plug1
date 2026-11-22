# HEAVEN: licenses, corresponding source and build provenance

Current delivery: **HEAVEN by lil grass block 1.13.0**, with the Vocal Glitch
slicer and opt-in phrase-tail triggering. Successful Release-build evidence and
exact binary/source-input hashes are in `BUILD-PROVENANCE.json`; see
[the release record](../docs/Release-1.13.0-Validation.md).
The historical restored `Vocal Stack` deliveries are not the current binaries.

These CLAP/VST3 binaries combine GPLv3-covered Vocal Stack/Vital code with JUCE
8.0.12 under **AGPLv3**, using the combination permission in section 13 of both
licenses. GPL-covered parts retain GPLv3; third-party components retain their
own terms. This is not a GPLv3-only binary or a claim of a commercial JUCE license.

Keep `LICENSE.txt`, `COPYING.AGPLv3`, `PixelifySans-OFL.txt`, this file and the
complete `ThirdParty` folder with redistributed copies. The software is supplied
without warranty, as detailed in those licenses.

Project source: <https://github.com/YukiYuziriha/plug1>. See
[Building](../docs/Building.md) and [Licensing](../docs/Licensing.md) in the full
repository and corresponding-source ZIP for build and license details.

## Release source and evidence

Publish `HEAVEN-1.13.0-corresponding-source.zip` alongside
`HEAVEN-1.13.0-Windows-x64.zip` and `SHA256SUMS.txt`, providing source access at no
extra charge under GPLv3/AGPLv3 section 6(d), not a future source-on-request offer.
These are expected asset names, not a claim that the assets have been published.
The archive root is `HEAVEN-1.13.0/`; other versions use their selected version.

The source ZIP includes the editable project, build/patch scripts, retained
Vital subset, font, licenses/notices, and complete pinned external sources for
JUCE, CLAP JUCE extensions (including enabled CLAP submodules), Signalsmith
Linear and Signalsmith Stretch. Dependencies are exported from pinned Git caches
or hash-verified upstream archives, not from potentially patched cache contents.
The included CMake patch scripts reproduce project dependency modifications.
No compiled plugin, build cache or old automatic-tuner source is required.

`SOURCE-MANIFEST.json` hashes every packaged source/dependency file. In
`--working-tree` mode it explicitly records `working-tree-snapshot`, a null
`binary_source_commit`, and a base commit for context only. It must not represent
the base commit as the binary-producing revision. `BUILD-PROVENANCE.json` in the
Windows ZIP records the observed successful resource-wrapped build, build-log
hash, source/build-input hashes and delivered binary hashes; the source ZIP
includes the identical provenance record when packaging that build. Verification checks
those hashes against the actual ZIP contents, dependency pins, notice inventories
and license texts. This evidence is not an independent reproducible-build proof
or a claim that the binaries equal an arbitrary Git revision.

## Maintainer commands

Run from the repository root using **dedicated 64-bit PowerShell processes**.
Python 3.12 or later is required. Every command below uses the resource wrapper;
none compiles or installs plugins. Refresh requires an already completed,
successful Release build, its captured wrapper log and an exit-code file
containing `0`. Use the actual build/log locations for your build.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python tools/Test-Release-Bundles.py
```

After the release owner has completed and validated the real 1.13.0 HEAVEN build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python tools/Refresh-Windows-Package.py --build-directory source/build --build-log source/build/release-build.log --build-exit-file source/build/release-build.exit --version 1.13.0
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python tools/Bundle-Release-Assets.py --working-tree --version 1.13.0 --deps-cache source/build/_deps --output-directory source/build/publication-1.13.0
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python tools/Verify-Release-Assets.py --directory source/build/publication-1.13.0 --version 1.13.0
```

Refresh changes repository delivery files only, never installed plugins. It
checks PE version, Release configuration, source freshness and source/product
version before replacing deliveries. It replaces, rather than merges, the
`ThirdParty` tree to remove retired notices, and retires only the known legacy
`Vocal Stack` deliveries after staging the replacements. Other binaries are
preserved by refresh but rejected as unverified extras during release bundling.

Release bundling defaults to **no history archive** and requires an explicit
`--working-tree` or `--revision` selection. It does not require removed tuning/UI
documents; it includes the selected release validation document if present.
Use a new output directory; existing ZIPs are not overwritten. Source-only export:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python tools/Bundle-Corresponding-Source.py --working-tree --deps-cache source/build/_deps --output source/build/HEAVEN-1.13.0-corresponding-source.zip
```

`--revision COMMIT` is available for an exact, independently established
binary-producing revision; it refuses source/dependency differences rather than
silently overlaying current code. The release verifier currently checks the
working-tree/provenance workflow above. `--include-history` explicitly opts into
the separate all-ref historical audit/archive; that archive can include saved
work and is not needed for the current release's corresponding source.

## Historical pre-tuning delivery

The historical v1.11.3 binary-producing revision is
<https://github.com/YukiYuziriha/plug1/tree/ae4ee4f9f7f69c2d03aceb625843e718541617e7>.
Its dependency pins are supplied with the
[v1.11.1 corresponding source](https://github.com/YukiYuziriha/plug1/releases/download/v1.11.1/VocalStack-1.11.1-corresponding-source.zip).
The [historical source archive](https://github.com/YukiYuziriha/plug1/releases/download/v1.11.0/VocalStack-source-history.zip)
records co-delivered binary bytes, not independently reproduced historical builds.
Those historical assets do not certify a new HEAVEN build.
