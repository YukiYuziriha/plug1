# Invoke through tools/Invoke-HalfCpu.ps1; does not edit shared CMake/build files.
param(
    [string]$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    [switch]$IntegrationOnly
)
$ErrorActionPreference = 'Stop'
if (-not $env:CMAKE_BUILD_PARALLEL_LEVEL -or -not $env:CTEST_PARALLEL_LEVEL) {
    throw 'Run this script through tools/Invoke-HalfCpu.ps1.'
}
if (-not (Test-Path -LiteralPath $VcVars)) { throw "Missing x64 MSVC environment: $VcVars" }
$lines = & cmd.exe /d /s /c "`"$VcVars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed.' }
foreach ($line in $lines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$source = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = Join-Path $source 'build/engine-tuning-isolated'
New-Item -ItemType Directory -Force $build | Out-Null
$includes = @("/I$source/src", "/I$source/build/_deps/signalsmith-stretch-src/include", "/I$source/build/_deps/signalsmith-linear-src/include")
Push-Location $build
try {
    & cl.exe /nologo /std:c++20 /EHsc /O2 /MT /c @includes "$source/src/VocalEngine.cpp" /FoVocalEngine.obj
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $suites = @('tuning/EngineTuning')
        if (-not $IntegrationOnly) { $suites += @('LeadMixBackend', 'SidechainSource', 'VoiceBackend', 'Dsp') }
        foreach ($suite in $suites) {
        $name = Split-Path $suite -Leaf
        & cl.exe /nologo /std:c++20 /EHsc /O2 /MT @includes "$source/tests/${suite}Tests.cpp" VocalEngine.obj "/Fe${name}Tests.exe"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        & (Join-Path $build "${name}Tests.exe")
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
} finally { Pop-Location }
