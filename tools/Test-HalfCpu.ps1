param([switch] $Probe, [switch] $Grandchild)
$ErrorActionPreference = 'Stop'
if ($Probe) {
    $mask = [Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity.ToInt64()
    $bits = 0
    for ($i = 0; $i -lt 64; $i++) { if (($mask -band ([long]1 -shl $i)) -ne 0) { $bits++ } }
    if ($bits -ne [int]$env:CMAKE_BUILD_PARALLEL_LEVEL -or
        $bits -ne [int]$env:CTEST_PARALLEL_LEVEL) { throw 'Concurrency environment does not match affinity.' }
    if ($Grandchild) {
        if ($mask.ToString() -ne $env:HALF_CPU_TEST_MASK) { throw 'Grandchild affinity differs.' }
        Write-Host "PASS grandchild affinity and concurrency: $bits"
        exit 0
    }
    $env:HALF_CPU_TEST_MASK = $mask.ToString()
    & powershell.exe -NoProfile -File $PSCommandPath -Probe -Grandchild
    if ($LASTEXITCODE -ne 0) { throw 'Grandchild failed.' }
    Write-Host "PASS child affinity and concurrency: $bits"
    exit 0
}
$wrapper = Join-Path $PSScriptRoot 'Invoke-HalfCpu.ps1'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $wrapper powershell.exe -NoProfile -File $PSCommandPath -Probe
if ($LASTEXITCODE -ne 0) { throw 'Restricted child failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $wrapper cmd.exe /c exit 23
if ($LASTEXITCODE -ne 23) { throw 'Exit code was not preserved.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $wrapper cmake --build unused --parallel 99
if ($LASTEXITCODE -ne 1) { throw 'Explicit concurrency was not rejected.' }
Write-Host 'PASS exit propagation and explicit concurrency rejection (preceding error is expected).'
