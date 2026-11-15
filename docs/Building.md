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
[Vital filter integration](../ThirdParty/Vital/README.md). Pixelify Sans is embedded
from `source/assets` under its [OFL license](../ThirdParty/PixelifySans-OFL.txt).

Read the repository [GPL v3 text](../LICENSE) and the retained
[third-party notices](../ThirdParty), including JUCE's separate AGPLv3/commercial
terms, before redistributing builds. This guide does not substitute for those
license obligations.

The [archived architecture reference](Manual-and-History.md#architecture) explains
the DSP/processor/UI boundaries. Keep downloaded dependencies, generated build
trees, local presets and machine-specific helper scripts out of source changes.
