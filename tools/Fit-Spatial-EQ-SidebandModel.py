"""Diagnostic fit of sparse, steady-state complex sideband lines; writes no data.

From the worktree root (dependencies as in Measure-Spatial-EQ.py):
  powershell.exe -NoProfile -Command "& ./tools/Invoke-HalfCpu.ps1 python '-B' 'tools/Fit-Spatial-EQ-SidebandModel.py'"

This is not a time-domain model for dense audio, and is not a product default.
"""

import hashlib
from concurrent.futures import ProcessPoolExecutor, as_completed
from itertools import product
from math import gcd
from pathlib import Path
from runpy import run_path


TOOLS = Path(__file__).parent
TRACK = run_path(str(TOOLS / "Probe-Spatial-EQ-SidebandTracking.py"))
PHASE = run_path(str(TOOLS / "Probe-Spatial-EQ-SidebandPhase.py"))
np, pb, centered, spectrum, tone, amp, db_ratio = (
    TRACK[k] for k in ("np", "pb", "centered", "spectrum", "tone", "amp", "db_ratio")
)
RATE, BLOCK, LEAD, ON, N, START, END, MODULE, PROBES, ROUTES, LEVELS, STEP = (
    TRACK[k] for k in ("RATE", "BLOCK", "LEAD", "ON", "N", "START", "END", "MODULE", "PROBES",
                       "ROUTES", "LEVELS", "STEP")
)
FIT, TEST, error, rms = (PHASE[k] for k in ("FIT", "TEST", "error", "rms"))
PHASES = PHASE["PHASES"][[0, 1, 2, 3, 6, 7]]
ORDERS = (-2, 2, -4, 4)
ROT = np.exp(1j * PHASES[:, None] * np.asarray(ORDERS))[:, None, :]
BED_LEVEL = -24
DEV_HZ = (1500, 1700, 1900, 2200, 2400, 2600, 2900, 3200, 3500)
TEST_HZ = (1600, 2000, 2500, 3000, 3400)
# Fixed M/S anchors from docs/Spatial-EQ-Reference-Measurements.md, not tuned here.
ANCHORS_MS = (5.958, 1.708)
# On this 100 Hz lattice and for even orders, tau and tau + 5 ms are identical.
GRID_MS = np.arange(500) * 0.01


def target_bins(hz):
    signed = np.asarray(PROBES)[:, None] + hz * np.asarray(ORDERS)
    targets = np.abs(signed)
    guards = targets[..., None] + STEP * np.asarray([-1, 1])
    # Apply the tracking probe's residue checks to the requested lines only.
    # Its unrelated fixed-offset controls need not be collision-free here.
    period = gcd(hz, RATE)
    fold = TRACK["fold"]
    teeth = (0, *PROBES, *(-p for p in PROBES))
    imd = {fold(sum(mix)) for mix in product(teeth, repeat=3)}
    seen = set()
    for p, row in zip(PROBES, targets):
        for f in row:
            for at in (f, f - STEP, f + STEP):
                images = {q for q in PROBES if (at - q) % period == 0 or (at + q) % period == 0}
                if (not 0 < at < RATE // 2 or at % STEP or at in seen or at in imd
                        or at % period == 0 or images != ({p} if at == f else set())):
                    raise ValueError(f"F={hz}, probe={p}: contaminated bin at {at} Hz")
                seen.add(at)
    return targets // STEP, guards // STEP, signed < 0


def capture_group(r, l, beds, indices, plans):
    comb = sum(map(tone, PROBES))
    comb[:ON] = 0
    probe = np.asarray(ROUTES[r][1])[:, None] * comb
    quiet = np.zeros((2, END))
    eps = amp(LEVELS[l])
    _, delta = centered(quiet, probe, eps)
    q, q_repeat = spectrum(delta), spectrum(delta, START + N)
    controls = np.zeros(5)
    lines = {}
    for hz in beds:
        targets, guards, negative = plans[hz]
        angle = 2 * np.pi * ((hz * np.arange(END)) % RATE) / RATE
        phases = {}

        for p in indices:
            phase = PHASES[p]
            bed = np.broadcast_to(amp(BED_LEVEL) * np.cos(angle + phase), quiet.shape)
            off, delta = centered(bed, probe, eps)
            e = spectrum(delta) - q
            repeat = spectrum(delta, START + N) - q_repeat
            z = e[r, targets]
            # Undo positive-frequency folding before demodulating by n*phase.
            phases[p] = np.where(negative, z.conj(), z)
            checks = (spectrum(off)[r, targets], q[r, targets], e[r, guards],
                      e[1 - r, targets], repeat[r, targets] - z)
            controls = np.maximum(controls, [np.max(np.abs(v)) for v in checks])
        lines[hz] = np.stack([phases[p] for p in indices])
    return lines, controls


def measure(beds, levels, phase_split, plans):
    indices = tuple(range(len(PHASES)))[phase_split]
    lines = np.empty((len(beds), len(ROUTES), len(levels), len(indices),
                      len(PROBES), len(ORDERS)), dtype=complex)
    controls = np.zeros((len(ROUTES), 5))
    batches = (beds[::2], beds[1::2])
    with ProcessPoolExecutor(max_workers=6) as pool:
        jobs = {pool.submit(capture_group, r, l, batch, indices, plans): (r, l, batch)
                for r in range(len(ROUTES)) for l in levels for batch in batches}
        for job in as_completed(jobs):
            r, l, batch = jobs[job]
            measured, checks = job.result()
            for hz, z in measured.items():
                lines[beds.index(hz), r, levels.index(l)] = z
            controls[r] = np.maximum(controls[r], checks)
            route = ROUTES[r][0]
            print(f"Captured {route}>{route} at {LEVELS[l]} dBFS/tooth, beds {batch} Hz", flush=True)
    return lines, controls


def interpolate(hz, local, query, lo, hi, delay_ms):
    # A delay at the signed output frequency adds -2*pi*(probe+n*bed)*tau.
    # The constant probe term cancels in derotation/reapplication for each tooth.
    slope = 2j * np.pi * np.asarray(delay_ms)[..., None, None] * np.asarray(ORDERS) / 1000
    t = ((query - hz[lo]) / (hz[hi] - hz[lo]))[:, None, None, None]
    left = local[lo] * np.exp((hz[lo] - query)[:, None, None, None] * slope)
    right = local[hi] * np.exp((hz[hi] - query)[:, None, None, None] * slope)
    return (1 - t) * left + t * right


def predict(development):
    hz = np.asarray(DEV_HZ)
    # Bed x route x tooth x order. Development has only the stronger probe level.
    local = np.mean(development[:, :, 0] * ROT[FIT].conj(), axis=2)
    middle = np.arange(1, len(hz) - 1)
    losses = []
    for delay in GRID_MS:
        # One slope per route, shared across teeth and signed orders. Select by
        # leave-one-interior-bed-out complex prediction, not the final test split.
        dev_pred = interpolate(hz, local, hz[middle], middle - 1, middle + 1, delay)
        losses.append(rms(dev_pred - local[middle], axis=(0, 2, 3)))
    best = np.argmin(losses, axis=0)
    fitted = GRID_MS[best]
    for r, (route, _) in enumerate(ROUTES):
        print(f"{route}>{route}: dev grid {fitted[r]:.2f} ms, onset anchor {ANCHORS_MS[r]:.3f} ms; "
              f"dev grid vs raw residual change "
              f"{db_ratio(losses[best[r]][r], losses[0][r]):+.2f} dB", flush=True)
    query = np.asarray(TEST_HZ)
    hi = np.searchsorted(hz, query)
    return tuple(interpolate(hz, local, query, hi - 1, hi, delay)[:, :, None, None] * ROT[TEST]
                 for delay in (0, ANCHORS_MS, fitted))


def report(observed, predictions):
    models = (0, *predictions)
    labels = ("signal", "zero", "raw interp", "onset anchor", "dev grid")

    def scores(y, *pred, axis=None):
        return (db_ratio(rms(y, axis), 1), *(error(y, p, axis) for p in pred))

    def row(label, values):
        print(f"{label:>19} " + " ".join(f"{value:>12.2f}" for value in values))

    print("\nSignal is RMS dB re tooth; errors are complex residual RMS / measured RMS in dB (lower better).")
    print("All interpolation variants use the same neighboring development knots and held phase rotation.")
    print(f"{'held bed / level':>19} " + " ".join(f"{label:>12}" for label in labels))
    row("all held-out", scores(observed, *models))
    for b, hz in enumerate(TEST_HZ):
        for l, level in enumerate(LEVELS):
            row(f"{hz} Hz / {level}", scores(observed[b, :, l], 0,
                                            *(p[b, :, 0] for p in predictions)))

    # Pool beds, probe levels, held phases and teeth, not routes or orders.
    by_order = scores(observed, *models, axis=(0, 2, 3, 4))
    print(f"\n{'route / order':>19} " + " ".join(f"{label:>12}" for label in labels))
    for r, (route, _) in enumerate(ROUTES):
        for k, order in enumerate(ORDERS):
            row(f"{route}>{route} / {order:+d}", [v[r, k] for v in by_order])

    # Keep the prior -10 dB floor and require a material win over raw interpolation.
    _, zero, raw, anchor, fitted = by_order
    print("\nScreen per pooled route/order: <= -10 dB, beats zero, and >= 1 dB better than raw.")
    for name, score in (("Onset anchor", anchor), ("Dev grid", fitted)):
        passes = (score <= -10) & (score < zero) & (score <= raw - 1)
        print(f"{name}: {np.count_nonzero(passes)}/{passes.size} passed. "
              f"{'Screen met' if passes.all() else 'No transfer rescue'}. Diagnostic only.")


def main():
    if set(DEV_HZ) & set(TEST_HZ) or not all(DEV_HZ[0] < f < DEV_HZ[-1] for f in TEST_HZ):
        raise ValueError("Test beds must be disjoint and bracketed by development beds")
    plans = {hz: target_bins(hz) for hz in (*DEV_HZ, *TEST_HZ)}
    with MODULE.open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}", flush=True)
    print(f"Zero Latency, +6 dB bell at 2400 Hz, Q=1, stereo, dynamics off; {RATE} Hz, "
          f"{BLOCK}-sample blocks, {LEAD}-sample lead, no time shift.")
    print(f"Mid bed {BED_LEVEL} dBFS peak; comb {PROBES} Hz at {LEVELS} dBFS peak/tooth; "
          f"onset {ON / RATE:g} s; FFT from {START / RATE:g} s for {N / RATE:g} s plus adjacent repeat.")
    print("E = (J100-J0) with bed minus its quiet counterpart; J is the centered probe derivative.")
    print("Model: C(f_bed) * exp(i*n*bed_phase); interpolate C with and without slope compensation.")
    print(f"Develop at {LEVELS[0]} dBFS/tooth: {DEV_HZ} Hz; test both levels: {TEST_HZ} Hz.")
    print("Grid: 0-4.99 ms in 0.01 ms steps (one alias period on this 100 Hz bed lattice). "
          "The slope does not identify a physical delay.")
    print(f"Fit phases {np.rint(np.rad2deg(PHASES[FIT])).astype(int).tolist()} deg; "
          f"score phases {np.rint(np.rad2deg(PHASES[TEST])).astype(int).tolist()} deg.", flush=True)
    development, dev_controls = measure(DEV_HZ, (0,), FIT, plans)
    predictions = predict(development)
    observed, test_controls = measure(TEST_HZ, tuple(range(len(LEVELS))), TEST, plans)
    print("Peak checks (D0 / quiet / guard / cross-route / window change), dB re tooth:")
    for (route, _), checks in zip(ROUTES, np.maximum(dev_controls, test_controls)):
        print(f"  {route}: " + " / ".join(f"{v:.2f}" for v in db_ratio(checks, 1)))
    report(observed, predictions)


if __name__ == "__main__":
    main()
