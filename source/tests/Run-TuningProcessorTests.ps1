# Invoke only through tools/Invoke-HalfCpu.ps1. Builds an isolated headless
# processor-test target without changing the production CMakeLists.txt.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source = (Join-Path $repo 'source').Replace('\', '/')
$harness = Join-Path $repo 'source/build/tuning-processor-harness'
$build = Join-Path $harness 'build'
New-Item -ItemType Directory -Force $harness | Out-Null
@"
cmake_minimum_required(VERSION 3.24)
project(VocalStackTuningProcessorValidation VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded`$<`$<CONFIG:Debug>:Debug>")
set(VOCALSTACK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(VOCALSTACK_BUILD_PLUGIN ON CACHE BOOL "" FORCE)
set(FETCHCONTENT_BASE_DIR "$source/build/_deps" CACHE PATH "" FORCE)
set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)
add_subdirectory("$source" production)
juce_add_console_app(VocalStackTuningProcessorTests PRODUCT_NAME "Vocal Stack Tuning Processor Tests")
target_sources(VocalStackTuningProcessorTests PRIVATE "$source/tests/TuningProcessorTests.cpp")
target_link_libraries(VocalStackTuningProcessorTests PRIVATE VocalStack vocalstack_dsp
    juce::juce_audio_processors clap_juce_extensions juce::juce_recommended_config_flags)
enable_testing()
add_test(NAME VocalStackTuningProcessor COMMAND VocalStackTuningProcessorTests)
"@ | Set-Content -Encoding UTF8 (Join-Path $harness 'CMakeLists.txt')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ build tools not found.' }
$devcmd = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$ninja = Join-Path $env:USERPROFILE '.cmake-deps/ninja/win/x64/ninja.exe'
if (!(Test-Path $ninja)) { throw 'Expected existing Ninja installation not found.' }
$command = "call `"$devcmd`" -arch=x64 -host_arch=x64 && cmake -S `"$harness`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=`"$ninja`" && cmake --build `"$build`" --target VocalStackTuningProcessorTests && ctest --test-dir `"$build`" --output-on-failure"
$batch = Join-Path $harness 'validate.cmd'
$command | Set-Content -Encoding ASCII $batch
& $env:ComSpec /d /c $batch
if ($LASTEXITCODE -ne 0) { throw "Tuning processor validation failed ($LASTEXITCODE)." }
