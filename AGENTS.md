# Vocal Stack development guidelines

## Product and design

- Build a useful audio tool, not a feature demo. Favor controls and metering a
  working sound engineer needs: clear units, sensible ranges, predictable level,
  headroom, readable peaks, and trustworthy bypass.
- Keep the interface focused. Prefer a few well-chosen controls and useful
  defaults over exposing every DSP variable or adding flashy complexity.
- Preserve DAW keyboard shortcuts, especially Space for transport. Ordinary
  control clicks must not claim keyboard focus or consume host shortcuts. Only
  deliberate text entry should own keys, and commit/cancel must release it.
- When an important design choice is unclear, ask the user. Do not guess about
  sonic goals, destructive workflows, or major UI/architecture changes.
- Look at respected plugins and their official documentation for inspiration
  when useful. Borrow principles and workflows, not proprietary code, artwork,
  branding, or presets. Explain which parts are inspiration versus verified facts.
- Propose options before implementing speculative effects. A request for a plan
  is not permission to implement the effect.
- Mix means a true dry/wet blend, not an additive voice amount. Leveler should
  even phrase dynamics without imposing a fixed output RMS target; distinguish
  long-term RMS preservation from identical peaks or perceived loudness.
- Keep Advanced activation separate from panel visibility. Preserve dormant
  per-voice edits across toggles; Reset to Basic derives from current basic
  controls without resetting unrelated modules. Independent Advanced formants
  remain active when the shared Formant switch is off.

## Shared UI layout and controls

- Neon is the geometry reference. Device, Neon, Spectral, and Grass share the
  same component structure, spacing, control sizes, stroke widths, and corner
  radii. Skins change only colors and fonts. Fit text using actual font width
  and line height; do not fork the layout or add padding for individual skins.
- Reuse shared components and sizing tokens before adding local UI code. Define
  Small/Medium/Large knob sizes and Stack/Routing density presets once; select
  from those presets rather than hardcoding dimensions in individual FX panels.
- Every FX uses the shared thick Mix slider in the top row, with 1:1 mouse drag
  and consistent zero, partial, and full fill in Stack and Routing.

## Architecture

- Keep processing, host integration/state, and presentation separate. Small,
  reusable DSP modules are preferred; avoid both a growing monolith and a generic
  framework designed for hypothetical features.
- `VocalEngine` orchestrates the signal path. JUCE-independent helpers belong in
  the DSP layer; `PluginProcessor` owns parameters, state and the audio/UI bridge;
  UI components display snapshots and send requests, not mutate live DSP state.
- Preserve existing parameter IDs, indices, state recall, bus layouts, latency,
  and automation compatibility. Append new host parameters and give missing
  legacy-state values safe defaults. Use explicit parameter mappings in the UI.
- Audio callbacks must not allocate, lock, perform I/O, or access GUI objects.
  Use bounded processing and lock-free communication with the message thread.
- Smooth gain and time-varying DSP controls appropriately. Delay read positions
  must move continuously, not jump at control or host-block boundaries.
- Meter actual input and final output. Label RMS, sample peak, peak hold, maximum
  peak, and clipping honestly; do not call sample peaks true peaks or RMS LUFS.
- Full bypass must preserve the latency-aligned dry signal. Output trim is not a
  limiter, and automatic level matching is not exact perceptual loudness matching.

## Validation and delivery

- All Windows configure/build/test/benchmark commands, including those run by
  subagents, MUST run through `tools/Invoke-HalfCpu.ps1` in a dedicated 64-bit
  PowerShell process. See `docs/Windows-Resource-Limits.md` for usage. Do not
  bypass it with direct cmake/compiler/ctest invocations or explicit job flags.
  Parallel agents must use the same wrapper (the same restricted CPU subset).
  This limits eligible logical processors, not instantaneous Task Manager CPU.
  If it refuses the machine topology, stop and report rather than run unrestricted.
- Tooling/docs-only changes require relevant lightweight validation, not plugin
  rebuilds, version bumps, packaging, or installation. Commit and push them too.
- After changes affecting plugin binaries, always rebuild the affected binaries, run relevant tests, and
  commit and push the completed work to the existing GitHub repository.
  Do not leave source changes on GitHub with stale packaged binaries.
- Update the version and documentation for user-visible releases. Refresh the
  tracked CLAP and VST3 files in `Windows-x64` from the successful Release build.
  Keep build directories, downloaded dependencies, and machine-local scripts out
  of Git. Never commit credentials or force-push without explicit permission.
- Cover DSP changes with offline tests: mono/stereo, supported sample rates,
  irregular block sizes, automation, silence/reset, finite output, level/headroom,
  and bypass/latency as relevant. Cover parameter/state and UI changes too.
- Verify GUI layout and text fit in all four skins at narrow and wide window
  sizes when changing controls or meters. Distinguish automated checks from a
  listening check in the user's actual DAW.
- Permanent user preference: always do GUI testing and screenshotting on the
  non-primary second monitor when available. At the start of every GUI test or
  capture session, enumerate the actual connected displays, their primary flags,
  work areas, and DPI/scaling; do not assume a display number or cached layout.
  Place native plugin/test windows on the selected non-primary display before
  interaction or capture, within its usable work area, respecting per-monitor DPI
  and negative desktop coordinates. Ensure transient owned windows (popups,
  menus, and dialogs) follow their owner onto that display and remain in its work
  area. Prefer an existing harness placement facility when available.
  Never move the user's host or unrelated windows, or change the primary display,
  display resolution, or scaling. If no second monitor is available, report the
  primary-monitor fallback explicitly. Pure offscreen snapshots and DSP-only
  tests have no physical monitor placement: label them accordingly and never
  claim they ran on the second monitor. Do not retroactively claim earlier
  screenshots used the second monitor without verified placement evidence.
- Do not terminate Bitwig or overwrite a loaded plugin to force installation.
  Ask the user to close the host when files are locked.
- If rebuilding, tests, installation, or pushing is blocked, state exactly what
  succeeded and what remains. Never claim a build, push, or listening test passed
  without observing it.
- Finish with a concise summary of changes, validation, version/commit, and any
  action the user needs to take.
