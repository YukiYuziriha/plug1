# Building Vocal Stack

[Overview](../README.md) · [Getting Started](Getting-Started.md) ·
[Windows resource-limit policy](Windows-Resource-Limits.md)

The current source project is **v1.11.0**, C++20, with CLAP/VST3 plugin targets and
DSP, processor, state, module-host and UI tests. These are reproducible commands
for a fresh Windows checkout, not references to machine-local delivery scripts.

## Prerequisites

- Windows x64 and **64-bit Windows PowerShell 5.1 or newer**.
- Visual Studio 2022 with **Desktop development with C++** and a Windows SDK.
- Git and CMake **3.24 or newer**, available in the developer shell.
- Network access for CMake's first dependency fetch. No API keys are required.

Clone the repository, then open its root in an **x64 Visual Studio 2022 Developer
PowerShell**. If the repo is already present, use that checkout instead:

```powershell
git clone https://github.com/YukiYuziriha/plug1.git
Set-Location plug1
```

## Configure, build and test

Run from the repository root. All Windows configure/build/test/benchmark children
must use the tracked half-CPU wrapper in a dedicated 64-bit PowerShell process:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake -S source -B source/build-vs2022 -G "Visual Studio 17 2022" -A x64
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake --build source/build-vs2022 --config Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& './tools/Invoke-HalfCpu.ps1' -Executable ctest -CommandArguments @('--test-dir', 'source/build-vs2022', '-C', 'Release', '--output-on-failure')"
```

The CTest invocation passes an explicit argument array so PowerShell does not
consume `-C` as an abbreviation of the wrapper's `CommandArguments` parameter.
The separate `source/build-vs2022` directory avoids colliding with an existing
local Ninja cache. Do not reuse a build tree configured with another generator.
Do not add explicit job/parallel flags: the wrapper selects at most half the
eligible logical processors and supplies concurrency. It refuses unsupported
processor-group topologies, 32-bit PowerShell and single-logical-CPU machines;
stop rather than falling back to an unrestricted build. This is an affinity and
job-budget policy, not a guaranteed instantaneous CPU percentage cap.

Artifacts appear under:

```text
source/build-vs2022/VocalStack_artefacts/Release/CLAP/Vocal Stack.clap
source/build-vs2022/VocalStack_artefacts/Release/VST3/Vocal Stack.vst3/
```

`COPY_PLUGIN_AFTER_BUILD` is false: a successful build does not install plugins or
replace tracked `Windows-x64` packages. For local use, follow the
[installation guide](Getting-Started.md#install-on-windows-x64) with your new
artifact, preserving the complete VST3 bundle and legal notices. Do not overwrite
a plugin loaded by a running host.

CTest includes native UI fixtures. An unlocked, unoccluded desktop is needed for
native interaction checks; the project prefers an available non-primary monitor.
Offscreen/component snapshots are not proof of native input or host rendering
performance. The [v1.11 release record](Release-1.11.0-Validation.md) documents the
remaining occlusion blocker and distinguishes automated tests from listening.
These commands have not been rerun as part of this documentation-only update.

## DSP-only configuration

To build the JUCE-independent DSP library and its tests without plugin/UI targets,
use a separate tree:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake -S source -B source/build-dsp -G "Visual Studio 17 2022" -A x64 -DVOCALSTACK_BUILD_PLUGIN=OFF
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake --build source/build-dsp --config Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& './tools/Invoke-HalfCpu.ps1' -Executable ctest -CommandArguments @('--test-dir', 'source/build-dsp', '-C', 'Release', '--output-on-failure')"
```

`VOCALSTACK_BUILD_TESTS` defaults to ON. Optional
`-DVOCALSTACK_TEST_BASELINE=ON` compares against a pinned historical Git revision;
that option additionally requires Python and the expected revision in local Git
history. Ordinary builds and source archives do not require that baseline option.

## Dependencies and licensing

[CMakeLists.txt](../source/CMakeLists.txt) is the source of truth for pins:

| Dependency | Pin |
| --- | --- |
| JUCE | 8.0.12, archive SHA256 verified by CMake |
| CLAP JUCE Extensions | `6bde108329ff9fc24404b79b8b397261a75cf6a8` |
| Signalsmith Stretch | `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` |
| Signalsmith Linear | `7bb1c8f36794d6980080ce817c91f8da8fe56a66` |

The configure step fetches these sources and applies the repository's CLAP
patches for host integration, including bus layouts, state, tails and sidechain.
The DSP also contains an attributed GPL-covered scalar
[Vital filter integration](../ThirdParty/Vital/README.md). Pixelify Sans and
Oxanium Medium and SemiBold are embedded from `source/assets` under their
[Pixelify Sans](../ThirdParty/PixelifySans-OFL.txt) and
[Oxanium](../ThirdParty/Oxanium-OFL.txt) OFL notices.

This distribution selects **JUCE's open-source AGPLv3 route**. The combined
plugin uses the GPLv3/AGPLv3 section 13 combination permission, retaining GPLv3
for project/Vital code and AGPLv3 for JUCE modules. Read [GPLv3](../LICENSE),
[AGPLv3](../COPYING.AGPLv3), [Licensing and corresponding source](Licensing.md)
and all [third-party notices](../ThirdParty) before redistributing. No commercial
JUCE entitlement is asserted.

## Build from the complete corresponding-source ZIP

Extract the ZIP, enter `VocalStack-source`, and use the same prerequisites and
half-CPU wrapper as above. Python is needed only to create/verify the source
package, not to build the ordinary targets. The ZIP contains pristine dependency
sources in `external`; no dependency fetch is needed. Apply the initial CLAP
patch explicitly because a FetchContent source override bypasses `PATCH_COMMAND`.
The remaining tail/sidechain patches are applied by the normal configure step:

```powershell
$deps = (Resolve-Path external).Path
$portsPatch = (Resolve-Path source/cmake/clap-ports.patch).Path
git -C external/clap_juce init --quiet
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake "-DSOURCE_DIRECTORY=$deps/clap_juce" "-DPATCH_FILE=$portsPatch" -P source/cmake/PatchClap.cmake
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake -S source -B source/build-offline -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_FULLY_DISCONNECTED=ON "-DFETCHCONTENT_SOURCE_DIR_JUCE=$deps/JUCE" "-DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE=$deps/clap_juce" "-DFETCHCONTENT_SOURCE_DIR_SIGNALSMITH-LINEAR=$deps/signalsmith-linear" "-DFETCHCONTENT_SOURCE_DIR_SIGNALSMITH-STRETCH=$deps/signalsmith-stretch"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake --build source/build-offline --config Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& './tools/Invoke-HalfCpu.ps1' -Executable ctest -CommandArguments @('--test-dir', 'source/build-offline', '-C', 'Release', '--output-on-failure')"
```

The paths are resolved from the extracted package, not machine-specific locations.
The local `git init` isolates patch application from any enclosing Git checkout;
it does not download dependencies or change their source files.
Keep `VOCALSTACK_TEST_BASELINE` OFF in an extracted ZIP: it deliberately has no
Git history. See `SOURCE-MANIFEST.json` for the binary-producing commit, packaging
overlays, dependency pins and hashes. Offline configure/build has not been run
as part of this documentation-only packaging update; do not treat these
instructions as an observed rebuild result.

The [archived architecture reference](Manual-and-History.md#architecture) explains
the DSP/processor/UI boundaries. Keep downloaded dependencies, generated build
trees, local presets and machine-specific helper scripts out of source changes.
