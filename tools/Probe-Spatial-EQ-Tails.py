"""Offline single-band tail lifecycle probe for installed Spatial EQ 0.7.0.

From the research worktree root, with Measure-Spatial-EQ.py's local dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'tools/Invoke-HalfCpu.ps1' -Executable python -CommandArguments @('-B','tools/Probe-Spatial-EQ-Tails.py')"
"""

from pathlib import Path
from runpy import run_path


AUTOMATION = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-Automation.py")))
HOST, SWEEP = AUTOMATION["HOST"], AUTOMATION["SWEEP"]
np, pb, MODULE = (HOST[k] for k in ("np", "pb", "MODULE"))
BLOCK = 512
FLOOR = 1e-8  # Timing threshold in full-scale sample units, not a tolerance.


def capture(x, rate, *, depth=1.0, events=(), block=BLOCK):
    """events: (sample, band field or None, new normalized value, reset next call)."""
    plugin = pb.load_plugin(str(MODULE))
    params = plugin.parameters
    settings = {f"band_{i}_active": 0.0 for i in range(1, 9)}
    band = dict(SWEEP["BASE"], active=1.0, enabled=1.0, shape=0.0, stereo=0.0,
                dynamic=0.0, sidechain=0.0, depth=depth)
    settings.update({SWEEP["key"](field): raw for field, raw in band.items()})
    settings.update(processing=0.0, output=0.5, bypass=0.0, spatial_bypass=0.0, mono_audition=0.0)
    for key, raw in settings.items():
        SWEEP["set_raw"](params[key], raw)

    chunks = []
    previous, reset = 0, True
    for stop, field, raw, next_reset in (*events, (x.shape[1], None, None, False)):
        part = x[:, previous:stop].copy()
        out = plugin.process(part, rate, buffer_size=block, reset=reset)
        if out.shape != part.shape or out.dtype != np.float32 or not np.isfinite(out).all():
            raise RuntimeError(f"Invalid stereo render: {out.shape}, {out.dtype}")
        chunks.append(out.astype(np.float64))
        if field is not None:
            SWEEP["set_raw"](params[SWEEP["key"](field)], raw)
            band[field] = raw
            for check in ("active", "enabled", "depth"):
                if abs(params[SWEEP["key"](check)].raw_value - band[check]) > 1e-6:
                    raise RuntimeError(f"{field} write unexpectedly changed {check}")
        previous, reset = stop, next_reset
    return np.concatenate(chunks, axis=1)


def levels(x):
    lanes = (*HOST["mid_side"](x), x)
    return " / ".join(f"{np.sqrt(np.mean(v ** 2)):.2e},{np.max(np.abs(v)):.2e}" for v in lanes)


def report(label, x):
    print(f"  {label:39} {levels(x)}")


def timing(x, rate, anchor):
    hit = np.flatnonzero(np.max(np.abs(x), axis=0) > FLOOR)
    return "none" if not hit.size else f"{(hit[0]-anchor)*1000/rate:+.3f}..{(hit[-1]-anchor)*1000/rate:+.3f} ms"


def noop(off, on):
    return ((off, None, None, False), (on, None, None, False))


def toggle(field, off, on):
    return ((off, field, 0.0, False), (on, field, 1.0, False))


def measure(rate):
    def samples(ms):
        return round(rate * ms / 1000)

    lead = samples(100)
    burst = lead + samples(16)
    off = burst + samples(8) + 7  # Keep boundary off the nominal 32-sample tick.
    short, long = (off + samples(wait) for wait in (4, 60))
    x = np.zeros((2, long + samples(140)), dtype=np.float32)
    t = np.arange(burst - lead) / rate
    x[:, lead:burst] = 10 ** (-18 / 20) * np.hanning(t.size) * np.cos(2 * np.pi * 2400 * t)
    print(f"\n{rate} Hz / host buffer {BLOCK} | 16 ms dual-mono burst, then digital silence.")
    print(f"  Off boundary {off-burst} samples after burst (absolute index {off}); waits 4/60 ms.")
    print("  Each lane shows RMS,peak in FS: Mid / Side / stereo (pooled RMS, max L/R peak).")

    full = capture(x, rate)
    direct = capture(x, rate, depth=0.0)
    base = full - direct
    split = capture(x, rate, events=noop(off, short))
    repeated = capture(x, rate, events=noop(off, short))
    report("no-op splits minus unsplit", split - full)
    report("fresh-instance no-op repeat residual", repeated - split)
    report("direct filter, 0–2 ms after burst", direct[:, burst:burst + samples(2)])
    report("direct filter, off boundary onward", direct[:, off:])
    report("uninterrupted wet proxy, off onward", base[:, off:])

    for on in (short, long):
        held, restored = slice(off, on), slice(on, on + samples(25))
        print(f"\n  Wait {(on-off)*1000/rate:g} ms | restore at sample {on}:")
        report("uninterrupted wet proxy while held", base[:, held])
        report("uninterrupted wet proxy after restore", base[:, restored])
        rendered = {}
        for field in ("enabled", "active", "depth"):
            events = toggle(field, off, on)
            wet_on = capture(x, rate, events=events)
            rendered[field] = wet_on
            # At Depth 0 a Depth schedule is redundant; enabled/active must be matched.
            eq = direct if field == "depth" else capture(x, rate, depth=0.0, events=events)
            wet = wet_on - eq
            print(f"  {field} = 0 then 1 (same instance, no reset):")
            report("  Depth-0 anchor while held", eq[:, held])
            report("  Depth-0 anchor after restore", eq[:, restored])
            report("  wet proxy while held", wet[:, held])
            report("  wet proxy after restore (25 ms)", wet[:, restored])
            report("  post-restore wet minus uninterrupted", (wet - base)[:, restored])
            print(f"    Wet above {FLOOR:g} FS: held vs off {timing(wet[:, held], rate, 0)}; "
                  f"post vs restore {timing(wet[:, on:], rate, 0)}")
            if on == long:
                kept_off = capture(x, rate, events=(events[0], noop(off, on)[1]))
                restore_effect = wet_on - kept_off
                report("  restore minus keep-off (25 ms)", restore_effect[:, restored])
                print(f"    Restore contrast above {FLOOR:g} FS, relative to restore: "
                      f"{timing(restore_effect[:, on:], rate, 0)}")
            if field == "enabled" and on == short:
                again = capture(x, rate, events=events)
                report("  enabled repeat residual", again - wet_on)
                alternate = capture(x, rate, events=events, block=257)
                alternate_eq = capture(x, rate, depth=0.0, events=events, block=257)
                report("  buffer 257 minus 512, full output", alternate - wet_on)
                report("  buffer 257 minus 512, wet proxy", (alternate - alternate_eq) - wet)
        report("active minus enabled, full waveform", rendered["active"] - rendered["enabled"])

    reset_events = ((off, None, None, True),)
    reset = capture(x, rate, events=reset_events)
    reset_eq = capture(x, rate, depth=0.0, events=reset_events)
    fresh = capture(x[:, off:], rate)
    print("\n  Reset while ringing; no parameter changes:")
    report("fresh instance, silence only", fresh)
    report("reset minus fresh silence", reset[:, off:] - fresh)
    report("Depth-0 after reset", reset_eq[:, off:])
    report("wet proxy after reset", (reset - reset_eq)[:, off:])

    # A separate early Depth-0 click checks whether direct EQ can still ring after a toggle.
    click = np.zeros((2, lead + samples(30)), dtype=np.float32)
    click[:, lead] = 10 ** (-18 / 20)
    early = lead + samples(0.15)
    resume = early + samples(4)
    uninterrupted = capture(click, rate, depth=0.0)
    print(f"\n  Direct-history control: Depth 0 click, toggle +{(early-lead)*1000/rate:.3f} ms, wait 4 ms.")
    report("uninterrupted EQ at early toggle", uninterrupted[:, early:early + samples(0.25)])
    report("uninterrupted EQ after restore time", uninterrupted[:, resume:])
    reset_early = capture(click, rate, depth=0.0, events=((early, None, None, True),))
    report("direct EQ after early reset", reset_early[:, early:])
    for field in ("enabled", "active"):
        eq = capture(click, rate, depth=0.0, events=toggle(field, early, resume))
        report(f"{field}: direct EQ while held", eq[:, early:resume])
        report(f"{field}: direct EQ after restore", eq[:, resume:])


def dormant(rate):
    def samples(ms):
        return round(rate * ms / 1000)

    off = samples(124) + 7
    on = off + samples(3000)
    check = on + samples(40)
    plain = np.zeros((2, check + samples(70)), dtype=np.float32)

    def burst(x, start, length):
        t = np.arange(length) / rate
        x[:, start:start + length] = 10 ** (-18 / 20) * np.hanning(length) * np.cos(2 * np.pi * 2400 * t)

    burst(plain, samples(100), samples(16))
    burst(plain, check, samples(8))
    marked = plain.copy()
    # Long enough to separate direct ring-down from wet history, short enough for
    # the marker to be in the delay network if that network still accepts input.
    burst(marked, on - samples(12), samples(8))
    prefix = slice(on - samples(0.5), on)
    silent = slice(on, check)
    recovery = slice(check, None)
    direct_plain = capture(plain, rate, depth=0.0)
    direct_marked = capture(marked, rate, depth=0.0)
    direct_tag = direct_marked - direct_plain
    print(f"\n{rate} Hz | Long-zero re-entry: 3 s hold, optional marker ends 4 ms before restore.")
    print("  Old and never-high histories receive identical input. Marked and unmarked inputs"
          " are identical from 4 ms before restore onward; a common test burst follows at +40 ms.")
    report("direct marker residual, final 0.5 ms", direct_tag[:, prefix])
    report("direct marker residual, +0..40 ms", direct_tag[:, silent])
    results = {}
    for label, initial, held in (("old 100→0", 1.0, 0.0), ("never high, 0", 0.0, 0.0),
                                 ("positive control, 100→1%", 1.0, 0.01)):
        events = ((off, "depth", held, False), (on, "depth", 1.0, False))
        a = capture(plain, rate, depth=initial, events=events) - direct_plain
        b = capture(marked, rate, depth=initial, events=events) - direct_marked
        print(f"  {label}:")
        report("    wet marker delta, final 0.5 ms", (b - a)[:, prefix])
        report("    unmarked wet proxy, +0..40 ms", a[:, silent])
        report("    wet marker delta, +0..40 ms", (b - a)[:, silent])
        report("    common burst wet proxy", a[:, recovery])
        results[label] = b
    difference = results["old 100→0"] - results["never high, 0"]
    report("old minus never-high, final 0.5 ms", difference[:, prefix])
    report("old minus never-high, +0..40 ms", difference[:, silent])
    report("old minus never-high, common burst", difference[:, recovery])
    print("  A missing marker bounds observable carry-over. It cannot by itself distinguish"
          " buffers being cleared on re-entry from the dormant path never writing the marker.")


def main():
    AUTOMATION["identify"]()
    print("Static +6 dB Stereo Bell 2.4 kHz, Q=1, Width 100%, Spread 0%, zero latency.")
    print("Wet proxy = Depth 100 history minus matching Depth-0 history; the Depth switch uses static Depth 0.")
    print("This is an output contrast, not a tap into wet state. Silence below the floor cannot distinguish"
          " clearing from gating or natural decay; post-restore output on silent input can show retained history.")
    for rate in (48_000, 96_000):
        measure(rate)
        dormant(rate)
    print("Offline Pedalboard host-call boundaries only. No audio saved and no GUI used.")


if __name__ == "__main__":
    main()
