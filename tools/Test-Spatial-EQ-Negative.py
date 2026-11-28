"""Offline negative shelves and gain-sign transitions against Spatial EQ 0.7.0.

Run from the worktree with the existing Measure-Spatial-EQ.py dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'tools/Invoke-HalfCpu.ps1' -Executable python -CommandArguments @('-B','tools/Test-Spatial-EQ-Negative.py')"
"""

import argparse
from math import ceil, log
from pathlib import Path
from runpy import run_path


TOOLS = Path(__file__).resolve().parent
SHELVES = run_path(str(TOOLS / "Test-Spatial-EQ-Shelves.py"))
HOST = run_path(str(TOOLS / "Test-Spatial-EQ-MultibandAutomation.py"))
STATIC = run_path(str(TOOLS / "Spatial-EQ-DelayModel.py"))
np = HOST["np"]
CAPTURE = HOST["capture"]
SETTINGS = ((240, -6, 1.0, "low_shelf"), (800, -18, 0.7, "low_shelf"),
            (6400, -6, 1.0, "high_shelf"), (3200, -12, 2.0, "high_shelf"))


def mid_side(x):
    left, right = x
    return np.array(((left + right) * 0.5, (left - right) * 0.5))


def compare(label, ref, got, *, peak_limit, relative_limit=-75):
    ref_ms, err = mid_side(ref), mid_side(got - ref)
    rms = np.sqrt(np.mean(err**2, axis=1))
    peak = np.max(np.abs(err), axis=1)
    ref_rms = np.sqrt(np.mean(ref_ms**2, axis=1))
    db = np.full(2, -np.inf)
    score = (rms > 1e-8) & (ref_rms > 1e-8)
    db[score] = 20 * np.log10(rms[score] / ref_rms[score])
    if not np.isfinite(got).all() or np.any(peak > peak_limit) or np.any(db > relative_limit):
        raise AssertionError(f"{label}: M/S RMS={rms}, peak={peak}, dBr={db}")
    return rms.max(), peak.max(), db.max()


def summary(label, rows):
    rms, peak, db = np.max(rows, axis=0)
    relative = f"{db:.2f} dBr" if np.isfinite(db) else "below scoring floor"
    print(f"{label}: {len(rows)} stereo comparisons; worst M/S RMS {rms:.3e} FS, "
          f"peak {peak:.3e} FS, relative {relative} (above 1e-8 FS RMS)")


def fixtures(rate):
    amplitude = 10 ** (-24 / 20)
    impulse = np.zeros((2, rate // 10), dtype=np.float32)
    impulse[0, 0] = amplitude
    impulse[1, round(0.00073 * rate)] = -amplitude / 2
    yield "impulse", impulse

    duration = 0.12
    t = np.arange(round(duration * rate)) / rate
    low, high = 25.0, min(19_000.0, 0.44 * rate)
    k = np.log(high / low) / duration
    fade = np.clip((duration - t) / 0.01, 0, 1)
    left = amplitude * fade * np.cos(2 * np.pi * low * np.expm1(k * t) / k)
    right = 0.55 * amplitude * fade * np.sin(2 * np.pi * high * np.expm1(-k * t) / -k + 0.4)
    yield "sweep", np.pad(np.array((left, right), dtype=np.float32), ((0, 0), (0, rate // 12)))


def static_cases(rate):
    rows = {key: [] for key in ("direct", "isolated full", "isolated delta", "blend law", "pair full", "pair wet")}
    lead = rate // 10
    for frequency, gain, q, shape in SETTINGS:
        kwargs = dict(sample_rate=rate, frequency=frequency, gain_db=gain, q=q, shape=shape)
        for stimulus, dry in fixtures(rate):
            x = np.pad(dry, ((0, 0), (lead, 0)))
            pred_off = STATIC["process_direct_eq"](x, **kwargs)
            cut = (3, frequency, gain, 0.0, q, shape)
            off = CAPTURE(x, rate, (cut,), ())
            label = f"{rate} {shape} {frequency}/{gain}/{q} {stimulus}"

            def check(view, expected, actual, suffix="", limit=2e-6):
                rows[view].append(compare(f"{label} {view} {suffix}", expected[:, lead:], actual[:, lead:],
                                          peak_limit=limit))

            x64 = x.astype(np.float64)
            check("direct", off - x64, pred_off - x64)
            for depth in (0.5, 1.0):
                on = CAPTURE(x, rate, ((3, frequency, gain, depth, q, shape),), ())
                delta = STATIC["process_depth_delta"](x, depth, **kwargs)
                check("isolated full", on, pred_off + delta, depth)
                check("isolated delta", on - off, delta, depth)
                restore = depth * mid_side(x64 - off)[0]
                check("blend law", on - off, np.stack((restore, restore)), depth, limit=3e-8)

            # Isolate the wet pass with the asymmetric impulse and a fixed cut Depth.
            if stimulus != "impulse":
                continue
            for boost_slot, cut_slot in ((3, 4), (4, 3)):
                for depth in (0.5, 1.0):
                    def bands(boost_depth):
                        return ((boost_slot, frequency, 6, boost_depth, 1.0),
                                (cut_slot, frequency, gain, depth, q, shape))

                    on, off = (CAPTURE(x, rate, bands(d), ()) for d in (1.0, 0.0))
                    pred_on, pred_off = (STATIC["process_bands"](x, bands(d), sample_rate=rate) for d in (1.0, 0.0))
                    check("pair full", on, pred_on, (boost_slot, depth))
                    check("pair wet", on - off, pred_on - pred_off, (boost_slot, depth))
    return rows


def crossing_cases(rate, process):
    x, _ = HOST["fixture"](rate)
    write = int(rate * 0.180) + 11
    next_tick = (write + 31) // 32 * 32
    # A symmetric ±6 dB step reaches zero after ln(2) time constants.
    sign_tick = next_tick + 32 * (ceil(log(2.0) * 0.018 * rate / 32) - 1)
    x = x[:, :next_tick + rate // 6]
    rows = {key: [] for key in ("cross direct", "cross Depth delta", "cross full")}
    for shape, frequency in (("bell", 1740), ("low_shelf", 900), ("high_shelf", 4200)):
        for start in (6, -6):
            steps = ((write, {3: {"gain_db": -start}}),)
            for companion in ((), ((7, 2850, 6, 0.64, 1.0),)):
                bands = ((3, frequency, start, 0.8, 1.0, shape),) + companion
                off = tuple((*band[:3], 0.0, *band[4:]) for band in bands)
                label = f"{rate} {shape} {start:+} to {-start:+} companion={bool(companion)}"
                ref_on = CAPTURE(x, rate, bands, steps)
                ref_off = CAPTURE(x, rate, off, steps)
                for config, ref in ((bands, ref_on), (off, ref_off)):
                    split = CAPTURE(x, rate, config, steps, block=257, irregular=True)
                    if not np.array_equal(ref, split):
                        raise AssertionError(f"{label}: irregular/no-op splits changed output")
                unchanged = CAPTURE(x, rate, off, ())
                changed = np.flatnonzero(np.max(np.abs(ref_off - unchanged), axis=0) > 1e-8)
                if not changed.size or changed[0] != next_tick:
                    raise AssertionError(f"{label}: expected first EQ change at tick {next_tick}, got {changed[:1]}")
                if not companion:
                    side = mid_side(ref_on - ref_off)[1]
                    if start < 0:
                        changed = np.flatnonzero(np.abs(side[write:]) > 1e-7)
                        if not changed.size or not sign_tick <= write + changed[0] < sign_tick + 32:
                            raise AssertionError(f"{label}: expected boost Side at crossing tick {sign_tick}, got {changed[:1]} relative to write")
                        print(f"  {label}: first Depth Side above 1e-7 FS at write + {changed[0]} samples")
                    else:
                        old = CAPTURE(x, rate, bands, ()) - unchanged
                        first = np.flatnonzero(np.abs(side - mid_side(old)[1]) > 1e-7)
                        if not first.size or not next_tick <= first[0] < sign_tick:
                            raise AssertionError(f"{label}: expected boost Side to change before tick {sign_tick}, got {first[:1]}")
                        print(f"  {label}: first Depth Side change above 1e-7 FS at write + {first[0] - write} samples")
                on = sum(process(x, bands, steps=steps, sample_rate=rate))
                dry = sum(process(x, off, steps=steps, sample_rate=rate))
                # Score the transition independently of the settled pre-roll.
                window = slice(next_tick, next_tick + rate // 6)
                for view, ref, got in (("cross direct", ref_off, dry), ("cross Depth delta", ref_on - ref_off, on - dry),
                                       ("cross full", ref_on, on)):
                    rows[view].append(compare(f"{label} {view}", ref[:, window], got[:, window], peak_limit=2e-6))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("static", "crossing"), help="run only the selected suite")
    args = parser.parse_args()
    SHELVES["identify"]()
    suites = []
    if args.suite != "crossing":
        suites.append((static_cases, (44_100, 48_000, 96_000)))
    if args.suite != "static":
        process = run_path(str(TOOLS / "Spatial-EQ-AutomationModel.py"))["process_bands_components"]
        suites.append((lambda rate: crossing_cases(rate, process), (48_000, 96_000)))
    for suite, rates in suites:
        totals = {}
        for rate in rates:
            for view, rows in suite(rate).items():
                totals.setdefault(view, []).extend(rows)
        for view, rows in totals.items():
            summary(view, rows)


if __name__ == "__main__":
    main()
