"""Headless complex sideband phase and frequency-transfer probe; writes no data.

From the repository root, with Measure-Spatial-EQ.py's local dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-SidebandPhase.py
"""

import hashlib
from itertools import product
from pathlib import Path
from runpy import run_path


BASE = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-Sidebands.py")))
np, pb, amp, centered, spectrum, tone, fold, db_ratio = (
    BASE[k] for k in ("np", "pb", "amp", "centered", "spectrum", "tone", "fold", "db_ratio")
)
RATE, BED_HZ, LEVELS, ORDERS, ON, N, START, END, MODULE, BLOCK, LEAD = (
    BASE[k] for k in ("RATE", "BED_HZ", "LEVELS", "ORDERS", "ON", "N", "START", "END",
                      "MODULE", "BLOCK", "LEAD")
)
PROBES = (1668, BASE["PROBES"][0], 1820)
CENTER = 1
OFFSETS = tuple(n for order in ORDERS for n in (-order, order))
PHASES = np.deg2rad(np.arange(0, 360, 36))
FIT, TEST = slice(None, None, 2), slice(1, None, 2)
ROT = np.exp(1j * PHASES[:, None] * np.asarray(OFFSETS))


def bins():
    signed = np.asarray([[hz + n * BED_HZ for n in OFFSETS] for hz in PROBES])
    targets = np.abs(signed)  # Nyquist wrapping is rejected below.
    step = RATE // N
    guards = targets[..., None] + step * np.asarray([-1, 1])
    all_hz = np.concatenate((targets.ravel(), guards.ravel()))
    teeth = {f for hz in PROBES for f in (-hz, hz)}
    # Reject distinct cubic products, including their bed translations. Products
    # landing on a probe itself cannot be separated; the level check tests them.
    cubic = {sum(terms) for terms in product(teeth, repeat=3)} - teeth
    period = RATE // BED_HZ
    unwanted = {fold(f + k * BED_HZ) for f in {0, *cubic} for k in range(period)}
    if (len(set(all_hz)) != len(all_hz) or set(all_hz) & (unwanted | set(PROBES))
            or np.any((all_hz <= 0) | (all_hz >= RATE // 2) | (all_hz % step != 0))):
        raise RuntimeError("Invalid or colliding sideband/guard bins")
    return targets // step, guards // step, signed < 0


def measure(beds, route, eps, target_bins):
    targets, guards, negative = target_bins
    probe = np.asarray(route)[:, None] * sum(map(tone, PROBES))
    probe[:, :ON] = 0
    lines, guard_peak, drift_peak = [], [], []
    for bed in beds:
        _, delta = centered(bed, probe, eps)
        first, second = spectrum(delta), spectrum(delta, START + N)
        z = first[:, targets]
        # Represent f_probe+n*f_bed, not its positive-frequency image.
        lines.append(np.where(negative, z.conj(), z))
        guard_peak.append(np.max(np.abs(first[:, guards]), axis=(-2, -1)))
        drift_peak.append(np.max(np.abs((second - first)[:, targets]), axis=-1))
    # Line shape: frequency, phase, output, signed offset.
    return np.moveaxis(lines, 2, 0), np.max(guard_peak, axis=0), np.max(drift_peak, axis=0)


def rms(z, axis=0):
    return np.sqrt(np.mean(np.abs(z) ** 2, axis=axis))


def error(observed, predicted, axis=0):
    return db_ratio(rms(observed - predicted, axis), rms(observed, axis))


def pair(values):
    return "/".join(f"{v:.1f}" for v in values)


def main():
    target_bins = bins()
    with MODULE.open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}", flush=True)
    print(f"+6 dB bell, {BED_HZ} Hz, Q=1, stereo, dynamics off; {RATE} Hz, "
          f"{BLOCK}-sample blocks, {LEAD}-sample lead; Mid bed -24 dBFS peak.")
    print(f"Fixed cosine probe comb: {PROBES} Hz; onset {ON / RATE:g} s; "
          f"FFT at {START / RATE:g} s for {N / RATE:g} s, then adjacent repeat.")
    print("Delta J = centered weak-probe derivative at Depth 100% minus Depth 0%.")
    print("Fit phases 0/72/144/216/288 deg; score 36/108/180/252/324 deg.")
    print("All table entries are dB pairs for M/S outputs. Signal is held-out RMS re input; "
          "errors are held-out complex RMS ratios (lower is better).")
    print("Local fits use five phases at their own frequency. Transfer uses only the "
          f"{PROBES[CENTER]} Hz fit. Flat uses the training mean (no phase rotation).", flush=True)

    bed_angle = 2 * np.pi * ((BED_HZ * np.arange(END)) % RATE) / RATE
    beds = [np.broadcast_to(amp(-24) * np.cos(bed_angle + phase), (2, END)) for phase in PHASES]
    for label, route in (("M", (1, 1)), ("S", (1, -1))):
        previous = None
        for level in LEVELS:
            lines, guards, windows = measure(beds, route, amp(level), target_bins)
            rot = ROT[None, :, None, :]
            local = np.mean(lines[:, FIT] * rot[:, FIT].conj(), axis=1, keepdims=True)
            flat = np.mean(lines[:, FIT], axis=1, keepdims=True)
            observed = lines[:, TEST]
            center_fit = local[CENTER] * rot[0, TEST]
            local_err = error(observed, local * rot[:, TEST], axis=1)
            flat_err = error(observed[CENTER], flat[CENTER])
            transfer_err = error(observed, center_fit, axis=1)
            signals = db_ratio(rms(observed, axis=1), 1)
            drift = None if previous is None else error(observed[CENTER], previous)
            print(f"\n{label} input, {level} dBFS peak/tooth; center {PROBES[CENTER]} Hz. "
                  "Guard and window drift are peak dB re input across all measured phases:")
            for hz, guard, window in zip(PROBES, guards.T, windows.T):
                print(f"  {hz} Hz: guard {pair(db_ratio(guard, 1))}, drift {pair(db_ratio(window, 1))}")
            columns = ("center signal", "center local", "center flat", "prev level fit",
                       f"{PROBES[0]} local", f"{PROBES[0]} transfer",
                       f"{PROBES[2]} local", f"{PROBES[2]} transfer")
            print(f"{'n':>3} " + " | ".join(" ".join(f"{v:>16}" for v in group)
                                             for group in (columns[:4], columns[4:])))
            for k, n in enumerate(OFFSETS):
                report = (signals[CENTER, :, k], local_err[CENTER, :, k], flat_err[:, k],
                          None if drift is None else drift[:, k], local_err[0, :, k],
                          transfer_err[0, :, k], local_err[2, :, k], transfer_err[2, :, k])
                cells = ["--" if v is None else pair(v) for v in report]
                print(f"{n:+3d} " + " ".join(f"{v:>16}" for v in cells[:4])
                      + " | " + " ".join(f"{v:>16}" for v in cells[4:]))
            previous = center_fit
            print("", flush=True)


if __name__ == "__main__":
    main()
