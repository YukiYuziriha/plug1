"""Headless bed/hit superposition probe; renders and fits stay in memory.

From the repository root (dependencies as in Measure-Spatial-EQ.py):
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-BedInteraction.py
"""

import hashlib
from pathlib import Path
from runpy import run_path


CONTEXT = run_path(str(Path(__file__).with_name("Probe-Spatial-EQ-Context.py")))
np, pb, capture, ms, amp, db_ratio, scalar_fit, samples = (
    CONTEXT[k] for k in ("np", "pb", "capture", "ms", "amp", "db_ratio", "scalar_fit", "ms_samples")
)
RATE, LEAD, BLOCK = (CONTEXT[k] for k in ("RATE", "LEAD", "BLOCK"))
DEPTHS = (0.0, 0.5, 1.0)
PHASES = (0, 90, 180, 270)
LEVELS = (-12, 0, 12)
FREQ = 2400
HIT = samples(8)
TIMES = (("onset", 0), ("ramp", 137), ("steady", samples(350)),
         ("steady+", samples(350) + 137))
WINDOWS = ((0, 1), (1, 8), (8, 30), (30, 100))


def bed_signal():
    end = samples(550)
    t = np.arange(end) / RATE
    fade = np.sin(np.pi / 2 * np.arange(samples(10)) / samples(10)) ** 2
    envelope = np.ones(end)
    envelope[:fade.size], envelope[-fade.size:] = fade, fade[::-1]
    tone = 0.7 * np.cos(2 * np.pi * FREQ * t) + 0.3 * np.cos(2 * np.pi * 1740 * t)
    mono = np.pad((amp(-24) * envelope * tone).astype(np.float32), (0, samples(250)))
    return np.stack((mono, mono))


def hit_signal(bed, on, phase, level):
    # Bed-clock carrier: phase and envelope timing are independent factors.
    # Construct negative polarities exactly, not by evaluating cos(theta + pi).
    # Snap analytical zeros so the shared host onset check sees the true arrival.
    ticks = (FREQ * (on + np.arange(HIT))) % RATE
    theta = 2 * np.pi * ticks / RATE
    if phase % 180 == 0:
        carrier = np.where(ticks % (RATE // 2) == RATE // 4, 0, np.cos(theta))
    else:
        carrier = np.where(ticks % (RATE // 2) == 0, 0, -np.sin(theta))
    sign = 1 if phase < 180 else -1
    x = np.zeros_like(bed)
    x[0, on:on + HIT] = sign * amp(-24 + level) * np.hanning(HIT + 2)[1:-1] * carrier
    return x


def rms(x):
    return np.sqrt(np.mean(x * x, axis=-1))


def dbfs(x):
    return db_ratio(rms(x), 1)


def fmt(values, spec=".1f"):
    return "/".join("n/a" if np.isnan(v) else format(v, spec) for v in values)


def window(on, lo, hi):
    return slice(on + samples(lo), on + samples(hi))


def local_fit(y, x, *, shared=False):
    """One scalar per M/S per 1 ms, optionally shared across the four phases."""
    pred = np.empty_like(y)
    for start in range(0, y.shape[-1], samples(1)):
        w = slice(start, start + samples(1))
        yw, xw = y[..., w], x[..., w]
        if shared:
            # Concatenate phase traces in time, keeping M and S independent.
            yw = yw.transpose(1, 0, 2).reshape(2, -1)
            xw = xw.transpose(1, 0, 2).reshape(2, -1)
        fitted = scalar_fit(yw, xw)[1]
        if shared:
            fitted = fitted.reshape(2, y.shape[0], -1).transpose(1, 0, 2)
        pred[..., w] = fitted
    return pred


def phase_rms(x):
    return np.sqrt(np.mean(x * x, axis=(0, 2)))


def pooled_fit_error(y, x):
    return db_ratio(phase_rms(y - local_fit(y, x)), phase_rms(y))


def report_windows(errors, old, new, coherence, on):
    # Pair 0/180 and 90/270. Squared norms of even and odd sum to total energy
    # after averaging each polarity pair, regardless of their mutual alignment.
    even = (errors[:2] + errors[2:]) / 2
    odd = (errors[:2] - errors[2:]) / 2
    print("    ms       E dBFS       BDelta dBFS      odd/E %     old common       old+cos     old local      hit local")
    for lo, hi in WINDOWS:
        w = window(on, lo, hi)
        e, b = errors[..., w], new[..., w]
        a = np.broadcast_to(old[:, w], e.shape)
        common = local_fit(e, a, shared=True)
        # Since every polarity has its opposite, the common and cosine-weighted
        # bed templates are orthogonal across phases in each fit window.
        signed = local_fit(e - common, a * coherence[:, None, None], shared=True)
        erms = phase_rms(e)
        odd2, even2 = phase_rms(odd[..., w]) ** 2, phase_rms(even[..., w]) ** 2
        odd_pct = np.divide(100 * odd2, odd2 + even2, out=np.full(2, np.nan),
                            where=(odd2 + even2) > 0)
        print(f"    {lo:>2}-{hi:<3} {fmt(db_ratio(erms, 1)):>13} {fmt(db_ratio(phase_rms(b), 1)):>13}"
              f" {fmt(odd_pct):>12} {fmt(db_ratio(phase_rms(e - common), erms)):>14}"
              f" {fmt(db_ratio(phase_rms(e - common - signed), erms)):>14}"
              f" {fmt(pooled_fit_error(e, a)):>14} {fmt(pooled_fit_error(e, b)):>14}")


def main():
    with CONTEXT["HOST"]["MODULE"].open("rb") as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {pb.__version__}, NumPy {np.__version__}")
    print(f"+6 dB / 2.4 kHz / Q=1, stereo, dynamics off, {RATE} Hz float32, "
          f"{BLOCK}-sample blocks, {LEAD}-sample lead, no alignment shift.")
    print("A: dual-mono -24 dBFS nominal peak, 0.7*cos(2400 Hz)+0.3*cos(1740 Hz), "
          "10 ms sine-squared fade in/out, ends at 550 ms.")
    print("B: left-only 8 ms Hann 2.4 kHz hit, nominal per-active-channel level relative to A; "
          "phase against the bed's 2.4 kHz carrier (not against the two-tone sum).")
    print("D=Depth; Delta(X)=Y_D(X)-Y_0(X); E0=Y_0(AB)-Y_0(A)-Y_0(B); "
          "E=Delta(AB)-Delta(A)-Delta(B). All subtraction float64 after hosting float32.")
    print("All pairs are Mid/Side, (L+-R)/2. RMS dBFS uses FS=1. Fits report leftover/E dBr; "
          "lower is better. Old common shares 1 ms coefficients across phases; old/hit local "
          "fit each phase independently (descriptive, not predictions).")
    print("Old+cos adds a slope times the signed, whole-hit input cosine to the common bed gain.")
    print("Odd/E is the percent interaction energy that reverses when B flips, pooled over two polarity pairs. "
          "Near the E0 floor, parity and fits are not interpretable.")

    bed = bed_signal()
    a = {d: ms(capture(bed, d)) for d in DEPTHS}
    check_hit = hit_signal(bed, TIMES[2][1], 0, 0)
    repeat_error = max(np.max(np.abs(capture(x, d) - capture(x, d, fresh=True)))
                       for d in DEPTHS for x in (bed, check_hit, bed + check_hit))
    print(f"Fresh vs reused/reset A/B/AB at all depths (steady, 0 dB, 0 deg): {repeat_error:.3g} FS max")
    if repeat_error > 1e-7:
        raise RuntimeError("Reused/reset instance disagrees with fresh control")

    worst_pre = worst_off = 0.0
    for label, on in TIMES:
        print(f"\n{label}: B at sample {on} ({1000 * on / RATE:.3f} ms from A ramp start)")
        for level in LEVELS:
            traces = {d: [] for d in DEPTHS[1:]}
            for phase in PHASES:
                hit = hit_signal(bed, on, phase, level)
                both = bed + hit
                b0, ab0 = (ms(capture(x, 0.0)) for x in (hit, both))
                off = ab0 - a[0.0] - b0
                worst_off = max(worst_off, np.max(np.abs(off)))
                av, bv = bed[0, on:on + HIT].astype(float), hit[0, on:on + HIT].astype(float)
                coherence = np.dot(av, bv) / (np.linalg.norm(av) * np.linalg.norm(bv))
                for d in DEPTHS[1:]:
                    b, ab = (ms(capture(x, d)) - ref for x, ref in ((hit, b0), (both, ab0)))
                    e = ab - (a[d] - a[0.0]) - b
                    worst_pre = max(worst_pre, np.max(np.abs(e[:, :on]), initial=0))
                    traces[d].append((coherence, off, b, ab, e))
            for d, rows in traces.items():
                old = a[d] - a[0.0]
                w = window(on, 0, 8)
                print(f"  D={d:.0%}, B rel={level:+} dB, 0-8 ms ADelta RMS {fmt(dbfs(old[:, w]))} dBFS")
                print("  phase  input cos       BDelta dBFS      ABDelta dBFS        E dBFS      E0 dBFS   old gain chg %")
                for phase, (rho, off, b, ab, e) in zip(PHASES, rows):
                    gain, _ = scalar_fit(e[:, w], old[:, w])
                    print(f"  {phase:>5} {rho:>+10.3f} {fmt(dbfs(b[:, w])):>13} {fmt(dbfs(ab[:, w])):>13}"
                          f" {fmt(dbfs(e[:, w])):>13} {fmt(dbfs(off[:, w])):>13} {fmt(100 * gain, '+.1f'):>14}")
                report_windows(np.stack([r[4] for r in rows]), old, np.stack([r[2] for r in rows]),
                               np.array([r[0] for r in rows]), on)
    print(f"\nControls across sweep: max |E| before hit = {worst_pre:.3g} FS; "
          f"max |E0| across all samples = {worst_off:.3g} FS (M/S).")
    if max(worst_pre, worst_off) > 1e-6:
        raise RuntimeError("Pre-hit or Depth-0 superposition control exceeded 1e-6 FS")


if __name__ == "__main__":
    main()
