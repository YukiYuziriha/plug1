# Historical binaries and corresponding source

The v1.11.0 release supplies two complementary source assets:

- [VocalStack-source-history.zip](https://github.com/YukiYuziriha/plug1/releases/download/v1.11.0/VocalStack-source-history.zip)
  contains `VocalStack.gitbundle`, the complete reachable repository history at
  packaging time, plus the per-commit source/binary/dependency matrix.
- [VocalStack-1.11.0-corresponding-source.zip](https://github.com/YukiYuziriha/plug1/releases/download/v1.11.0/VocalStack-1.11.0-corresponding-source.zip)
  supplies the complete editable external dependency sources used by **every
  audited historical revision**, plus current source, notices and build tools.

Download **both** for historical builds. The Git bundle preserves original
commits, tracked source, assets, patches, notices, build files and historical
binary bytes without rewriting history. It does not contain local configuration,
credentials from Git config, reflogs, or ignored build caches. New legal/build
documentation is in the source ZIP; it is not retroactively inserted in old
commits. The release's `SHA256SUMS.txt` checks these ZIP assets.

## Audited scope and meaning

`Historical-Source-Manifest.json` records all **44 reachable commits** at history
tip `35ab94c13c920e759e8efa82f7a9c8a80fc955c5`, including the documentation/showcase
commit, and **56 unique plugin binary blobs**. Each row identifies the commit,
version from CMake, complete `source` tree object, CMake blob, all dependency
declarations, and the Git blob/SHA256 of each co-delivered CLAP/VST3 binary.

Every audited CMake file has **identical FetchContent dependency declarations**:
JUCE 8.0.12 with archive SHA256
`dec1a8baee5aaec4502b717a421b850e257ab7e9c2efd03766185ec1957cf102`,
CLAP JUCE Extensions `6bde108329ff9fc24404b79b8b397261a75cf6a8`,
Signalsmith Stretch `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da`, and
Signalsmith Linear `7bb1c8f36794d6980080ce817c91f8da8fe56a66`.
The pinned extension's gitlinks supply CLAP
`29ffcc273be7c7c651f6c9953b99e69700e2387a` and CLAP Helpers
`a61bcdf0ecc2c8db1e80bfe8bf9cb7e8d9fd2bbc`. Both are included in the source ZIP.
JUCE includes the matching vendored VST3 SDK and embedded dependency sources.
Vital code, its adaptation, font assets and project-specific patches are tracked
at the corresponding historical commits and therefore preserved in the bundle.
No second version of an external dependency is needed for the audited history.
Optional Linear backends such as IPP, PFFFT, CMSIS-DSP and xsimd are not enabled
by the project's ordinary Windows configuration and are not bundled separately.

**Co-delivered is not independently reproduced:** the matrix proves which binary
bytes and source tree occur together in each commit. It does not establish that
every historical binary was compiled from that exact commit or that a rebuild
will be byte-identical. No historical build or runtime test was performed for
this packaging update. The complete history also preserves earlier source when
an old binary was carried forward unchanged in a later commit.

## Restore without GitHub or dependency downloads

Extract both ZIPs side by side, producing `VocalStack-history` and
`VocalStack-source`. In 64-bit Windows PowerShell, from their parent directory:

```powershell
git clone ./VocalStack-history/VocalStack.gitbundle VocalStack-restored
Set-Location VocalStack-restored
# Example: v1.11.0. Select another full commit from the matrix for an older binary.
git checkout --detach 12534e87440ca33a41f12bde53956319bd06ebab
Copy-Item -Recurse ../VocalStack-source/external external
$deps = (Resolve-Path external).Path
$portsPatch = (Resolve-Path source/cmake/clap-ports.patch).Path
$wrapper = (Resolve-Path ../VocalStack-source/tools/Invoke-HalfCpu.ps1).Path
git -C external/clap_juce init --quiet
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $wrapper cmake "-DSOURCE_DIRECTORY=$deps/clap_juce" "-DPATCH_FILE=$portsPatch" -P source/cmake/PatchClap.cmake
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $wrapper cmake -S source -B source/build-offline -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_FULLY_DISCONNECTED=ON "-DFETCHCONTENT_SOURCE_DIR_JUCE=$deps/JUCE" "-DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE=$deps/clap_juce" "-DFETCHCONTENT_SOURCE_DIR_SIGNALSMITH-LINEAR=$deps/signalsmith-linear" "-DFETCHCONTENT_SOURCE_DIR_SIGNALSMITH-STRETCH=$deps/signalsmith-stretch"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $wrapper cmake --build source/build-offline --config Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& '$wrapper' -Executable ctest -CommandArguments @('--test-dir', 'source/build-offline', '-C', 'Release', '--output-on-failure')"
```

Use a **fresh clone and fresh copy of `external` for each historical revision**;
patches changed during development and must come from that revision, not the
latest checkout. The dependency-local `git init` keeps `git apply` isolated from
the restored parent repository; it does not download anything. Compiler/Windows SDK/CMake prerequisites and installation are
described in `VocalStack-source/docs/Building.md`; test availability follows the
selected historical CMake file. The current half-CPU wrapper is used even for
commits that predate it. Git metadata is present in this restored checkout for
optional history-based tests, but these remain opt-in. To inspect exact binary
bytes instead of rebuilding, take the tracked `Windows-x64` files at the selected
commit and compare SHA256 with its matrix row.

## Recreate the release assets

From a full repository checkout with populated dependency caches:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python tools/Bundle-Release-Assets.py --output-directory source/build/publication
```

The tool audits all reachable commits, fails if historical dependency declarations
differ, creates and verifies the Git bundle, clones it into a temporary directory,
and compares every restored commit/source tree against the matrix. It prepares
the current source ZIP and complete Windows package, verifies ZIP integrity and
unchanged current plugin bytes, and writes individual `.sha256` files and
`SHA256SUMS.txt`. Existing asset files are not overwritten; select an empty output
directory for a rerun. `--audit-only` updates the tracked JSON matrix without
creating release archives. All archive work runs under the half-CPU wrapper;
none of these steps compiles, installs, commits, uploads, or changes visibility.

Keep these source assets available alongside the historical binaries. Providing
source and notices now does not certify past licensing compliance or alter any
third-party rights. This packaging does not add plugin UI features or claim to
have verified an About/license menu at runtime; it preserves the existing
binaries and supplies the full accompanying legal documents and source.
