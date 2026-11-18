# Invoke ONLY through tools/Invoke-HalfCpu.ps1 in a dedicated 64-bit PowerShell.
param([string]$VcVars = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
$ErrorActionPreference = 'Stop'
if (-not $env:CMAKE_BUILD_PARALLEL_LEVEL -or -not $env:CTEST_PARALLEL_LEVEL) {
    throw 'Run this script through tools/Invoke-HalfCpu.ps1.'
}
if (-not (Test-Path -LiteralPath $VcVars)) { throw "Supply -VcVars with the installed x64 MSVC environment script: $VcVars" }
# Import the toolchain environment without printing the caller environment.
$lines = & cmd.exe /d /s /c "`"$VcVars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'MSVC environment setup failed.' }
foreach ($line in $lines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$build = Join-Path $PSScriptRoot '../../build/tuning-isolated'
& cmake -S $PSScriptRoot -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ctest --test-dir $build --output-on-failure
exit $LASTEXITCODE
