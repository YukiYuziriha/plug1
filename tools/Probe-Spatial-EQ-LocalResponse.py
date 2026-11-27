"""Measure finite-step local Depth responses of the installed VST3, in memory.

From the repository root (dependencies as in Measure-Spatial-EQ.py):
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-LocalResponse.py
"""

import hashlib
from pathlib import Path
from runpy import run_path
from types import SimpleNamespace


CONTEXT = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-Context.py")))
np, pb, capture, ms, amp = (CONTEXT[k] for k in ("np", "pb", "capture", "ms", "amp"))
RATE, LEAD, BLOCK = (CONTEXT[k] for k in ("RATE", "LEAD", "BLOCK"))
MODULE = CONTEXT["HOST"]["MODULE"]
ON = RATE
PERIOD = RATE // 600
HIT = RATE * 8 // 1000
END = ON + RATE // 5
WINDOW = slice(ON, END)
PHASES = (0, 90, 180, 270)
LEVELS = (-36, -48, -60, -72, -84, -96, -108)
ROUTES = ((1, 1), (1, -1))


def rms(x):
    return np.sqrt(np.mean(x ** 2, axis=-1))


def db(num, den=1):
    with np.errstate(divide="ignore", invalid="ignore"):
        return 20 * np.log10(np.divide(num, den))


def fmt(x):
    return "/".join("n/a" if np.isnan(v) else f"{v:.2f}" for v in np.atleast_1d(x))


def error(actual, predicted):
    return db(rms(actual - predicted), rms(actual))


def peak(x):
    return np.max(np.abs(x), initial=0)


def bed_signal(phase):
    phase_samples = phase * RATE // (360 * 2400)
    ticks = (np.arange(END) - ON + phase_samples) % PERIOD
    theta = 2 * np.pi * ticks / PERIOD
    mid = amp(-24) * (0.75 * np.cos(4 * theta) + 0.25 * np.cos(3 * theta + 0.4))
    side = amp(-30) * (0.65 * np.sin(4 * theta + 0.3) + 0.35 * np.cos(3 * theta - 0.5))
    return np.stack((mid + side, mid - side)).astype(np.float32)


def probes():
    t = np.arange(HIT) / RATE
    result = []
    for route, freq, shift in zip(ROUTES, (2400, 1800), (0.2, 0.7)):
        wave = np.hanning(HIT + 2)[1:-1] * np.cos(2 * np.pi * freq * t + shift)
        p = np.zeros((2, END))
        p[:, ON:ON + HIT] = np.asarray(route)[:, None] * wave / peak(wave)
        result.append(p)
    return result


def rendered(x, **host):
    off, full = (capture(x, depth, **host) for depth in (0.0, 1.0))
    return SimpleNamespace(off=off, full=full, delta=ms(full - off))


def steps(bed, p, eps, base, **host):
    # Quantize each input exactly once; cancel the depth baselines in float64.
    xp, xm = ((bed + sign * eps * p).astype(np.float32) for sign in (1, -1))
    plus, minus = (rendered(x, **host) for x in (xp, xm))
    odd = (plus.delta - minus.delta) / 2
    even = ((plus.delta - base.delta) + (minus.delta - base.delta)) / 2
    sent = xp.astype(np.float64) - xm
    off_j = (plus.off - minus.off)[:, WINDOW] / (2 * eps)
    # At most half a local float32 ULP per rounded output (no bound on DSP
    # arithmetic). Propagate absolute bounds through both subtractions and M/S.
    ulps = sum(np.spacing(np.abs(y).astype(np.float32)).astype(np.float64)
               for y in (plus.full, minus.full, plus.off, minus.off)) / 4
    floor = np.broadcast_to(ulps.mean(axis=0), odd.shape)
    return SimpleNamespace(j=odd[:, WINDOW] / eps, even=even[:, WINDOW],
                           floor=db(rms(floor[:, WINDOW]), rms(odd[:, WINDOW])),
                           quant=error(p[:, WINDOW], sent[:, WINDOW] / (2 * eps)),
                           off_j=off_j, pre=max(peak(odd[:, :ON]), peak(even[:, :ON])))


def quiet_ir(level):
    eps = amp(level)
    irs = []
    for route in ROUTES:
        x = np.zeros((2, END - ON), dtype=np.float32)
        x[:, 0] = np.asarray(route) * eps
        plus, minus = (rendered(sign * x).delta for sign in (1, -1))
        irs.append((plus - minus) / (2 * float(x[0, 0])))
    return irs


def convolve(ir, p):
    n = 2 * ir.shape[-1]
    return np.fft.irfft(np.fft.rfft(ir, n) * np.fft.rfft(p, n), n)[:, :ir.shape[-1]]


def controls(bed, p, references):
    print("\nControls: phase 0, M probe, -60 dBFS; differences vs unmodified fixture.")
    ref, base = references
    repeat = rendered(bed, fresh=True)
    fresh = steps(bed, p, amp(-60), repeat, fresh=True)
    print(f"  Fresh vs reset/reused: Depth 0/100 bed max {peak(repeat.off - base.off):.3g}/"
          f"{peak(repeat.full - base.full):.3g} FS; J max {peak(fresh.j - ref.j):.3g} FS/eps")
    for name, host in (("silence lead +137", {"lead": LEAD + 137}), ("block 257", {"block": 257})):
        changed = rendered(bed, **host)
        step = steps(bed, p, amp(-60), changed, **host)
        print(f"  {name}: bed Delta {fmt(error(base.delta[:, WINDOW], changed.delta[:, WINDOW]))},"
              f" J {fmt(error(ref.j, step.j))} dBr; pre {step.pre:.3g} FS, "
              f"D0 J change {peak(ref.off_j - step.off_j):.3g} FS/eps")

    print("  History B: -12 dB lower bed before a 10 ms ramp to the exact shared suffix.")
    for shared_ms in (10, 100):
        suffix = ON - RATE * shared_ms // 1000
        ramp_start = suffix - RATE // 100
        history = bed.copy()
        history[:, :ramp_start] *= amp(-12)
        history[:, ramp_start:suffix] *= np.linspace(amp(-12), 1, suffix - ramp_start, endpoint=False)
        assert np.array_equal(history[:, suffix:], bed[:, suffix:])
        changed = rendered(history)
        step = steps(history, p, amp(-60), changed)
        print(f"  Identical input suffix from -{shared_ms} ms: bed Delta "
              f"{fmt(error(base.delta[:, WINDOW], changed.delta[:, WINDOW]))}, "
              f"J {fmt(error(ref.j, step.j))} dBr; pre {step.pre:.3g} FS")
        print(f"    D0 max: shared suffix {peak(base.off[:, suffix:] - changed.off[:, suffix:]):.9g} FS, "
              f"scored window {peak(base.off[:, WINDOW] - changed.off[:, WINDOW]):.9g} FS, "
              f"J change {peak(ref.off_j - step.off_j):.9g} FS/eps")


def main():
    with MODULE.open("rb") as module:
        identity = hashlib.file_digest(module, "sha256").hexdigest()
    print(f"Reference SHA-256 {identity}; Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB bell, 2.4 kHz, Q=1, stereo, dynamics off; {RATE} Hz float32, "
          f"{BLOCK}-sample blocks, {LEAD}-sample silent lead, no time shift.")
    print(f"Bed: periodic 2.4/1.8 kHz, nominal M/S peaks -24/-30 dBFS, "
          f"1 s running before probe (render sample {LEAD + ON}, block offset {(LEAD + ON) % BLOCK}).")
    print("Probes: simultaneous-onset, 8 ms Hann packets, M at 2.4 kHz and S at 1.8 kHz,"
          " each unit peak L/R. Sweep eps in per-probe peak dBFS (sum not renormalized).")
    print("Window: 0–200 ms after probe onset, bed continues. Pairs are M/S except L/R input quant.")
    print("D=Y100-Y0; J=(D(A+eps*p)-D(A-eps*p))/(2*eps); E=(D(+)+D(-))/2-D(A).")
    print("J RMS is dB re FS/eps. Other columns are dBr: E/(eps*J), change vs previous larger step,"
          " error vs quiet IR prediction, output-rounding estimate / odd, L/R input quantization error.")
    print("dBr is 20 log10(RMS ratio); -inf is exact zero with nonzero reference; n/a = 0/0.")

    packets = probes()
    irs = quiet_ir(-60)
    alt_irs = quiet_ir(-48)

    predictions, quiet_off = [], []
    for label, ir, alt, p in zip("MS", irs, alt_irs, packets):
        pred = convolve(ir, p[0, WINDOW])
        predictions.append(pred)
        # At -48 dBFS the first Hann sample clears the shared onset threshold.
        eps = amp(-48)
        plus, minus = (rendered((sign * eps * p).astype(np.float32)) for sign in (1, -1))
        isolated = (plus.delta - minus.delta)[:, WINDOW] / (2 * eps)
        quiet_off.append((plus.off - minus.off)[:, WINDOW] / (2 * eps))
        print(f"Quiet {label}: -60 vs -48 dBFS impulse IR error {fmt(error(ir, alt))}; "
              f"isolated -48 dBFS packet vs -60 IR error {fmt(error(isolated, pred))} dBr")

    quiet_off.append(quiet_off[0] + quiet_off[1])
    worst_pre = worst_period = 0.0
    results = {level: [] for level in LEVELS}
    off_errors = {level: [] for level in LEVELS}
    phase_refs = []
    refs = None
    for phase in PHASES:
        bed = bed_signal(phase)
        base = rendered(bed)
        previous = [None, None]
        last = slice(ON - 6 * PERIOD, ON)
        earlier = slice(ON - 7 * PERIOD, ON - PERIOD)
        worst_period = max(worst_period, peak(base.delta[:, last] - base.delta[:, earlier]))
        print(f"\nPhase {phase}: period-to-period pre-hit bed Delta difference "
              f"{fmt(error(base.delta[:, last], base.delta[:, earlier]))} dBr.")
        print("  Probe  eps         J RMS          E/odd          vs prev         vs qIR       round/odd        quant")
        print("  Sum rows: prediction error J(M+S) vs J(M)+J(S), then E/odd and round/odd (all dBr).")
        for level in LEVELS:
            eps = amp(level)
            estimates = [steps(bed, p, eps, base) for p in (*packets, packets[0] + packets[1])]
            for step, off in zip(estimates, quiet_off):
                worst_pre = max(worst_pre, step.pre)
                off_errors[level].extend(error(step.off_j, off))
            pair = estimates[2]
            add = error(pair.j, estimates[0].j + estimates[1].j)
            for i, (label, step) in enumerate(zip("MS", estimates)):
                drift = np.full(2, np.nan) if previous[i] is None else error(step.j, previous[i])
                previous[i] = step.j
                even = db(rms(step.even), eps * rms(step.j))
                ir_error = error(step.j, predictions[i])
                results[level].append((drift, even, ir_error, add, step.floor, step.quant))
                print(f"  {label:>5} {level:4} {fmt(db(rms(step.j))):>14} "
                      f"{fmt(even):>14} {fmt(drift):>14} {fmt(ir_error):>14} "
                      f"{fmt(step.floor):>14} {fmt(step.quant):>14}")
            print(f"  {'sum':>5} {level:4} {fmt(add):>14} "
                  f"{fmt(db(rms(pair.even), eps * rms(pair.j))):>14} {fmt(pair.floor):>14}")
            if level == -60:
                phase_refs.append(previous.copy())
                if phase == 0:
                    refs = estimates[0], base

    print("\nExtrema across phases, directions and output channels (L/R for quant and D0), dBr:")
    print("D0 error compares bed-centered Depth-0 J to isolated Depth-0 J (sum uses isolated M+S).")
    print("   eps           vs prev              E/odd              vs qIR           sum error          round/odd              quant           D0 error  worst drift")
    for level, values in results.items():
        columns = np.asarray(values).transpose(1, 0, 2).reshape(6, -1)
        spans = [fmt((row.min(), row.max())) if not np.isnan(row).all() else "--" for row in columns]
        off = off_errors[level]
        spans.append(fmt((min(off), max(off))))
        route = "--"
        if level != LEVELS[0]:
            phase_i, path = divmod(int(np.argmax(columns[0])), 4)
            route = f"{PHASES[phase_i]} {'MS'[path // 2]}->{'MS'[path % 2]}"
        print(f"  {level:4}" + "".join(f" {cell:>18}" for cell in spans) + f"  {route}")
    print("Phase check at -60 dBFS, phase 90/180/270 vs 0 respectively, error in J (dBr M/S):")
    for i, label in enumerate("MS"):
        print(f"  {label}: " + "; ".join(fmt(error(row[i], phase_refs[0][i])) for row in phase_refs[1:]))

    print(f"\nSweep checks: max |odd/even| before probe {worst_pre:.9g} FS M/S; "
          f"max settled-bed period difference {worst_period:.9g} FS M/S.")
    if worst_pre > 1e-6:
        raise RuntimeError("Pre-probe control exceeded 1e-6 FS")
    controls(bed_signal(0), packets[0], refs)


if __name__ == "__main__":
    main()
