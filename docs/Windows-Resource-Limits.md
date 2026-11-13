# Windows build/test resource limits

Run from the repository root in a Visual Studio Developer shell (64-bit Windows
PowerShell 5.1 or newer). The wrapper preserves the working directory and toolchain
environment, streams output, and returns the command's exit code:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake -S source -B source/build -G Ninja -DCMAKE_BUILD_TYPE=Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 cmake --build source/build --config Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 ctest --test-dir source/build -C Release --output-on-failure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 source/build/vocalstack_effects_benchmark.exe
```

An absolute executable path is also accepted (quote paths containing spaces).
Always use a dedicated PowerShell process, not dot-sourcing or an in-process
invocation: affinity and environment changes intentionally last until exit.
No administrator privilege or permanent machine settings are required.

## Contract and limitations

- Before starting the command, the wrapper selects at most `floor(N / 2)` logical
  processors from its inherited affinity, lowest eligible processor numbers first.
  It verifies its own mask before launch. A narrower inherited mask stays narrower.
- Normal Windows children inherit this affinity: CMake, Ninja/MSBuild, compilers,
  linkers, CTest, tests, and their ordinary descendants. Configure-time tools are
  covered too. All agents/subagents must use the wrapper; parallel invocations
  normally share the same low-numbered half, rather than different halves.
- `CMAKE_BUILD_PARALLEL_LEVEL` and `CTEST_PARALLEL_LEVEL` equal the selected CPU
  count. Direct CMake builds and CTest invocations additionally receive explicit
  `--parallel N`. Caller parallel flags are rejected. Other tools must honor the
  environment or receive their own suitably limited concurrency settings.
- This is **half logical processor capacity, not an exact instantaneous 50% Task
  Manager quota**. SMT, heterogeneous cores, scheduling and sampling affect the
  displayed percentage. It is not half physical cores or guaranteed half throughput.
- Affinity is not a security sandbox/job-object quota: programs can deliberately
  change their masks; external build services, distributed workers, already-running
  processes and separately launched commands are not covered. Multiple commands
  have separate job budgets, not an aggregate concurrency semaphore.
- Multi-processor-group machines are refused, even on Windows versions that span
  groups by default: a pointer-sized process mask cannot safely represent them.
  32-bit PowerShell, non-Windows systems and single-logical-CPU machines are also
  refused. No silent unrestricted fallback. Such systems need a separately
  reviewed group-aware/job-object policy before building.
- Local ignored `source/build/*.cmd` helpers on this checkout route CMake, CTest
  and smoke executables through this wrapper; their toolchain paths remain local.
  New checkouts should use the tracked commands above. Do not commit local helpers.

## Lightweight validation

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Test-HalfCpu.ps1
```

This launches no compile: it checks child/grandchild affinity, matching build/test
concurrency environment, exit-code propagation and rejection of explicit job
flags. The rejection check prints an expected error. It does not benchmark CPU
usage or run the plugin suite. Tooling/docs-only changes need no plugin version,
package, build or installation refresh.
