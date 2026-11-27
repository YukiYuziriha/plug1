"""Headless irregular-bed sideband interpolation probe; writes no data.

  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-IrregularGrid.py

Uses the installed reference and dependencies of Measure-Spatial-EQ.py.
"""

import hashlib
import os
from concurrent.futures import ProcessPoolExecutor
from functools import partial
from itertools import product
from pathlib import Path
from runpy import run_path
from types import FunctionType


FIT_MODEL = run_path(str(Path(__file__).with_name("Fit-Spatial-EQ-SidebandModel.py")))
np, pb, rms, error, db_ratio, interpolate = (
    FIT_MODEL[k] for k in ("np", "pb", "rms", "error", "db_ratio", "interpolate")
)
RATE, BLOCK, LEAD, ON, MODULE, PROBES, ROUTES, LEVELS, BED_LEVEL, ORDERS, ANCHORS_MS = (
    FIT_MODEL[k] for k in ("RATE", "BLOCK", "LEAD", "ON", "MODULE", "PROBES", "ROUTES",
                           "LEVELS", "BED_LEVEL", "ORDERS", "ANCHORS_MS")
)
PHASES, ROT, FIT, TEST = (FIT_MODEL[k] for k in ("PHASES", "ROT", "FIT", "TEST"))
N = RATE  # One-second coherent windows preserve the original comb at 1 Hz resolution.
START = FIT_MODEL["START"]
END = START + 2 * N
BANDS = {
    "low": (1500, 1512, 1530, 1536, 1550, 1560, 1575, 1584, 1600, 1608),
    "center": (2340, 2352, 2360, 2376, 2400, 2424, 2432, 2440, 2448, 2460),
    "high": (3350, 3360, 3375, 3384, 3400, 3408, 3420, 3432, 3440, 3456),
}
WIDE = (1500, 1608, 2340, 2460, 3350, 3456)
GAPS = (1850, 1920, 2850, 2928)
DELAY_MS = np.arange(1001) * 0.02
MODELS = ("zero", "fixed", "nearest", "raw", "anchor", "delay")
CHECKS = "D0 / quiet / guard / cross-route / window change"


def rebind(fn, **overrides):
    return FunctionType(fn.__code__, dict(fn.__globals__, **overrides),
                        fn.__name__, fn.__defaults__)


def bins(hz):
    return rebind(FIT_MODEL["target_bins"], STEP=RATE // N)(hz)


def capture(job):
    route, level, beds, phases = job
    # Use the existing signed-bin capture, changing only coherent window geometry.
    get = rebind(FIT_MODEL["capture_group"], N=N, END=END,
                 tone=rebind(FIT_MODEL["tone"], END=END),
                 spectrum=rebind(FIT_MODEL["spectrum"], N=N))
    plans = {hz: bins(hz) for hz in beds}
    reset_get = rebind(get, centered=partial(FIT_MODEL["centered"], fresh=False))
    lines, controls = reset_get(route, level, beds, phases, plans)
    if beds[0] == BANDS["low"][0] and level == 0:
        fresh, _ = get(route, level, beds[:1], phases[:1], plans)
        if not np.array_equal(fresh[beds[0]][0], lines[beds[0]][0]):
            raise RuntimeError("Fresh and reset captures differ at the measured bins")
    if not (np.isfinite(list(lines.values())).all() and np.isfinite(controls).all()):
        raise RuntimeError(f"Nonfinite capture for {job}")
    return route, level, lines, controls


def split(band):
    return band[::2] + band[-1:], band[1:-1:2]


def measure():
    all_beds = tuple(sorted({hz for band in BANDS.values() for hz in band} | set(GAPS)))
    held = tuple(sorted({hz for band in BANDS.values() for hz in split(band)[1]} | set(GAPS)))
    if set(WIDE) & set(held) or not all(WIDE[0] < f < WIDE[-1] for f in held):
        raise ValueError("Wide test beds must be disjoint and bracketed")
    for hz in all_beds:
        bins(hz)  # Fail on overlap or incoherence before loading the plugin.
    indices = tuple(range(len(PHASES)))
    jobs = [(r, l, part[i:i + 5], phases)
            for r, (l, part, phases) in product(
                range(len(ROUTES)), ((0, all_beds, indices), (1, held, indices[TEST])))
            for i in range(0, len(part), 5)]
    levels = ({}, {})
    controls = np.full((len(ROUTES), len(LEVELS), 5), -np.inf)
    workers = min(6, int(os.environ.get("CTEST_PARALLEL_LEVEL", "1")))
    with ProcessPoolExecutor(max_workers=workers) as pool:
        for route, level, lines, checks in pool.map(capture, jobs):
            levels[level].update({(hz, route): z for hz, z in lines.items()})
            controls[route, level] = np.maximum(controls[route, level], checks)
    return levels, controls


def stacked(lines, hz):
    return np.asarray([[lines[f, r] for r in range(len(ROUTES))] for f in hz])


def coefficients(lines):
    # [bed, route, phase, tooth, order] -> [bed, route, tooth, order]
    return np.mean(lines[:, :, FIT] * ROT[FIT].conj(), axis=2)


def fit_delay(hz, local):
    middle = np.arange(1, len(hz) - 1)
    losses = [rms(interpolate(hz, local, hz[middle], middle - 1, middle + 1, delay)
                  - local[middle], axis=(0, 2, 3)) for delay in DELAY_MS]
    return DELAY_MS[np.argmin(losses, axis=0)]


def predictions(levels, train, test):
    hz, query = np.asarray(train), np.asarray(test)
    hi = np.searchsorted(hz, query)
    if np.any(hi == 0) or np.any(hi == len(hz)) or np.any(query == hz[hi]):
        raise ValueError("Test beds must fall strictly between training knots")
    lo = hi - 1
    local = coefficients(stacked(levels[0], train))
    delay = fit_delay(hz, local)
    nearest = np.where(query - hz[lo] <= hz[hi] - query, lo, hi)
    fixed = local[np.argmin(abs(hz - (hz[0] + hz[-1]) / 2))]
    print(f"  train {train}; test {test}; LOO delay {pair(delay)} ms (M/S)")
    models = (0, fixed, local[nearest], *(interpolate(hz, local, query, lo, hi, d)
                                          for d in (0, ANCHORS_MS, delay)))
    # Keep a route axis for the fixed prediction and a bed axis for the others.
    return 0, fixed[:, None] * ROT[TEST], *(p[:, :, None] * ROT[TEST] for p in models[2:])


def pair(values):
    return "/".join(f"{v:.2f}" for v in values)


def score(levels, test, models, orders=False):
    strong = stacked(levels[0], test)
    oracle = coefficients(strong)[:, :, None] * ROT[TEST]
    held = (strong[:, :, TEST], stacked(levels[1], test))
    for r, (name, _) in enumerate(ROUTES):
        ref = held[0][:, r]
        preds = [0, models[1][r], *(p[:, r] for p in models[2:]), oracle[:, r]]
        for l, level in enumerate(LEVELS):
            y = held[l][:, r]
            print(f"  {name}>{name} {level:3d} " + " ".join(f"{v:8.2f}" for v in (
                db_ratio(rms(y, None), 1), *(error(y, p, None) for p in preds), error(y, ref, None))))
        if orders:
            # Distinguish radial from angular error for the delay predictor. Each
            # oracle replaces just one component with the held observation.
            p = preds[5]
            radial = error(abs(ref), abs(p), axis=(0, 1, 2))
            angular = error(ref, abs(ref) * np.exp(1j * np.angle(p)), axis=(0, 1, 2))
            for i, n in enumerate(ORDERS):
                y = ref[..., i]
                vals = (db_ratio(rms(y, None), 1),
                        *(error(y, p[..., i] if np.ndim(p) else p, None) for p in preds),
                        radial[i], angular[i])
                print(f"    {name}>{name} {n:+d} " + " ".join(f"{v:8.2f}" for v in vals))


def variation(levels, band):
    # Descriptive only: uses both training and test frequencies, not the predictors.
    c = coefficients(stacked(levels[0], band))
    gap = np.diff(band)
    for r, (name, _) in enumerate(ROUTES):
        a, b = c[:-1, r], c[1:, r]
        # Ignore phase on lines >20 dB down from either endpoint's strongest line.
        def strong(z):
            return abs(z) >= 0.1 * np.max(abs(z), axis=(1, 2), keepdims=True)

        mask = strong(a) & strong(b)
        raw = np.rad2deg(np.angle(b) - np.angle(a))
        circular = np.rad2deg(np.angle(b * a.conj()))
        wraps = mask & (abs(raw) > 180)
        reliable = abs(circular[mask])
        peaks = np.abs(db_ratio(abs(b[mask]), abs(a[mask])))
        # Total variation distance of the normalized line-power profiles ignores
        # both common gain and line phases; 0=same shape, 1=no shared power.
        power = abs(c[:, r]) ** 2
        profile = power / power.sum(axis=(1, 2), keepdims=True)
        shape = 0.5 * np.sum(abs(np.diff(profile, axis=0)), axis=(1, 2))
        idx = np.argmax(np.where(mask, abs(raw), -np.inf))
        j, tooth, order = np.unravel_index(idx, raw.shape)
        print(f"  {name}>{name}: {gap.min()}–{gap.max()} Hz steps; wraps {wraps.sum()}/{mask.sum()}; "
              f"largest raw {raw[j, tooth, order]:+.1f}° -> {circular[j, tooth, order]:+.1f}° "
              f"at {band[j]}->{band[j+1]} Hz ({PROBES[tooth]} Hz, n={ORDERS[order]:+d}); "
              f"circular |step| median/max {np.median(reliable):.1f}/{reliable.max():.1f}°; "
              f"|line gain step| median/max {np.median(peaks):.2f}/{peaks.max():.2f} dB; "
              f"shape TV median/max {np.median(shape):.3f}/{shape.max():.3f}")


def alias_report():
    fold = FIT_MODEL["TRACK"]["fold"]
    beds = sorted({f for band in BANDS.values() for f in band} | set(GAPS))
    targets = list(product(beds, PROBES, ORDERS))
    aliases = [min((abs(m) for m in range(-64, 65) if m != n
                    and fold(p + m * f) == fold(p + n * f)), default=65)
               for f, p, n in targets]
    minimum = min(aliases)
    if minimum <= 64:
        f, p, n = targets[aliases.index(minimum)]
        print(f"Same-tooth folded aliases through |m|=64: smallest |m|={minimum} "
              f"(bed {f}, tooth {p}, target n={n:+d}); "
              f"{sum(m <= 8 for m in aliases)}/{len(aliases)} lines have |m|<=8 collisions.")
    else:
        print("No same-tooth folded aliases through |m|=64.")
    print("Distinct tooth and bed-only image residues, direct cubic products, and adjacent bins "
          "are checked; translated higher-order products and higher aliases aren't excluded.")


def main():
    with MODULE.open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"Zero Latency, +6 dB bell at 2400 Hz, Q=1, stereo, dynamics off; {RATE} Hz, "
          f"{BLOCK}-sample blocks, {LEAD}-sample lead, no time shift.")
    print(f"Mid bed {BED_LEVEL} dBFS peak; comb {PROBES} Hz at {LEVELS} dBFS peak/tooth; "
          f"onset {ON / RATE:g} s; two coherent {N / RATE:g} s FFT windows from {START / RATE:g} s.")
    print("E = bed-centered (J100-J0) minus quiet (J100-J0); undo folding before phase demodulation.")
    for label, phases in (("fit at stronger probe", PHASES[FIT]), ("score at both probe levels", PHASES[TEST])):
        print(f"  {label}: {np.rint(np.rad2deg(phases)).astype(int).tolist()} deg")
    alias_report()
    print("Capturing in-memory renders with reset between stimuli and fresh-instance controls...",
          flush=True)
    levels, controls = measure()
    print(f"Peak controls in dB re tooth ({CHECKS}):")
    for r, (name, _) in enumerate(ROUTES):
        for l, level in enumerate(LEVELS):
            print(f"  {name}>{name} {level}: {pair(db_ratio(controls[r, l], 1))}")
    print("Signal is RMS dB re tooth; errors are complex residual RMS / observed RMS dB (lower is better).")
    print("Fixed uses one training knot nearest the training midpoint; anchor uses onset delays "
          f"({pair(ANCHORS_MS)} ms M/S). Delay fits 0–20 ms per route by training-only leave-one-out; "
          "a phase slope, not a physical delay measurement.")
    print("Phase oracle uses fit phases at the test frequency, not frequency transfer. Level delta "
          "compares the same held phases at -48 and -60 (normalized to the row signal).")
    header = " ".join(f"{m:>8}" for m in ("signal", *MODELS, "phase-or", "level-d"))
    for name, band in BANDS.items():
        train, test = split(band)
        print(f"\n{name} dense local ({band[0]}–{band[-1]} Hz)")
        variation(levels, band)
        models = predictions(levels, train, test)
        print("           " + header)
        if name == "center":
            print("  Signed-order rows at -48 have radial/angular delay-error components "
                  "in place of level-d (two final columns).")
        score(levels, test, models, orders=name == "center")
    wide_test = tuple(sorted({f for band in BANDS.values() for f in split(band)[1]} | set(GAPS)))
    print("\nSparse wide transfer (no extrapolation); one common wide fit, scored separately by region.")
    models = predictions(levels, WIDE, wide_test)
    for name, test in [(name, split(band)[1]) for name, band in BANDS.items()] + [("gaps", GAPS)]:
        print(f"{name} (knots up to {max(np.diff(WIDE))} Hz apart):\n           " + header)
        idx = np.searchsorted(wide_test, test)
        score(levels, test, (models[0], models[1], *(p[idx] for p in models[2:])))


if __name__ == "__main__":
    main()
