# Run in a dedicated PowerShell process: affinity/environment changes last until exit.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)][string] $Executable,
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]] $CommandArguments
)
$ErrorActionPreference = 'Stop'
try {
    if ($env:OS -ne 'Windows_NT' -or [IntPtr]::Size -ne 8) {
        throw 'Requires 64-bit PowerShell on Windows.'
    }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HalfCpuNative {
    [DllImport("kernel32.dll")] public static extern ushort GetActiveProcessorGroupCount();
    [DllImport("kernel32.dll")] public static extern uint GetActiveProcessorCount(ushort group);
}
'@
    if ([HalfCpuNative]::GetActiveProcessorGroupCount() -ne 1) {
        throw 'Multiple Windows processor groups are unsupported; refusing to run without a reliable limit.'
    }
    $total = [int][HalfCpuNative]::GetActiveProcessorCount(0)
    if ($total -lt 2) { throw 'Cannot restrict a single logical processor to half capacity using affinity.' }
    $budget = [int][Math]::Floor($total / 2)
    $process = [Diagnostics.Process]::GetCurrentProcess()
    $available = $process.ProcessorAffinity.ToInt64()
    [long] $mask = 0
    $jobs = 0
    for ($bit = 0; $bit -lt $total -and $jobs -lt $budget; $bit++) {
        [long] $flag = [long]1 -shl $bit
        if (($available -band $flag) -ne 0) { $mask = $mask -bor $flag; $jobs++ }
    }
    if ($jobs -eq 0) { throw 'No eligible processors in the inherited affinity mask.' }
    $process.ProcessorAffinity = [IntPtr]$mask
    if ($process.ProcessorAffinity.ToInt64() -ne $mask) { throw 'Affinity verification failed.' }
    $env:CMAKE_BUILD_PARALLEL_LEVEL = "$jobs"
    $env:CTEST_PARALLEL_LEVEL = "$jobs"

    # Explicit flags override environment defaults; reject them instead of silently exceeding the job budget.
    foreach ($argument in $CommandArguments) {
        if ($argument -match '^(--parallel(?:=|$)|-j|/m(?:[:0-9]|$)|-maxcpucount)') {
            throw 'Omit explicit parallel/job flags; this wrapper supplies the concurrency budget.'
        }
    }
    $leaf = [IO.Path]::GetFileNameWithoutExtension($Executable)
    if ($leaf -eq 'cmake' -and $CommandArguments -contains '--build') {
        $CommandArguments += @('--parallel', "$jobs")
    } elseif ($leaf -eq 'ctest') {
        $CommandArguments += @('--parallel', "$jobs")
    }
    Write-Host "HalfCpu: $jobs/$total logical processors; affinity 0x$($mask.ToString('X')); parallel jobs $jobs"
    & $Executable @CommandArguments
    if ($null -eq $LASTEXITCODE) { exit 0 }
    exit $LASTEXITCODE
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
