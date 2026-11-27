"""Offline, in-memory Depth-delta prototype for the installed RFA Spatial EQ 0.7.0.

From the worktree root, with the existing Measure-Spatial-EQ.py host dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Fit-Spatial-EQ-Candidate.py
Append --develop to stop before any holdout renders. Use --dense-diagnostic
for the separate dense-tail experiment (combine with --develop for fit only).

This tests one static bell at 48 kHz, not a product DSP or a listening test.
No reference audio, fitted kernels, or context coefficients are written to disk.
"""

import argparse
import hashlib
from pathlib import Path
import re
from runpy import run_path


TOOLS = Path(__file__).resolve().parent
LTI = run_path(str(TOOLS / "Probe-Spatial-EQ-LTI.py"))
CONTEXT = run_path(str(TOOLS / "Probe-Spatial-EQ-Context.py"))
SWEEP = run_path(str(TOOLS / "Sweep-Spatial-EQ-Controls.py"))
HOST = LTI["HOST"]
np, predict, ms, energy, error_db, fmt = (
    LTI[k] for k in ("np", "predict", "ms", "energy", "error_db", "pair")
)
capture, pair_impulses = CONTEXT["capture"], CONTEXT["pair"]
RATE, LEAD, BLOCK = (HOST[k] for k in ("RATE", "LEAD", "BLOCK"))
ANCHORS = SWEEP["DEPTHS"]
DEV_DEPTHS = (4, 17, 61, 86)
TEST_DEPTHS = (7, 37, 68, 94)
LEVELS = (-36, -12)
CONTEXT_DEPTHS = (0, 25, 50, 75, 100)
GAPS = (64, 240, 512, 960, 1920, 3840)
WINDOWS = (0, RATE // 1000, RATE // 500, RATE // 200)
TAIL = LTI["IR_LENGTH"]
BASE, LINEAR, SHAPED, QUADRATIC = "scaled full", "linear", "shape normalized", "local quadratic"
INTERPOLATORS = (LINEAR, SHAPED, QUADRATIC)
BURST_ON, BURST_LENGTH = round(0.273 * RATE), round(0.011 * RATE)
DENSE_TEST_DEPTHS = (13, 43, 79, 97)
RECENT, BED = RATE // 200, RATE // 50


def identify():
    module = HOST["MODULE"]
    manifest = HOST["PLUGIN"] / "Contents" / "Resources" / "moduleinfo.json"
    text = manifest.read_text(encoding="utf-8")
    version = re.search(r'"Version"\s*:\s*"([^"]+)"', text)
    if not version or version[1] != "0.7.0":
        raise RuntimeError(f"Expected installed RFA Spatial EQ 0.7.0, got {version[1] if version else 'unknown'}")
    with module.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    print(f"RFA Spatial EQ {version[1]} | SHA-256 {digest}\n{module}")
    print(f"Pedalboard {HOST['pb'].__version__}, NumPy {np.__version__}; "
          f"stereo float32, {RATE} Hz, buffer {BLOCK}, lead {LEAD}, no alignment shift")
    print("One band: +6 dB, 2.4 kHz, Q 1, Stereo Bell, Width 100%, Spread 0%, dynamics off; "
          "Zero Latency, 0 dB trim, 120 Hz bass protection.")


def matrix(depth, levels):
    """H[Mid/Side output, L/R input, sample]. Pool only normalized isolated impulses."""
    columns = []
    for channel in range(2):
        responses = []
        for level in levels:
            x = np.zeros((2, TAIL), dtype=np.float32)
            x[channel, 0] = 10 ** (level / 20)
            responses.append(ms(capture(x, depth / 100)) / float(x[channel, 0]))
        columns.append(np.mean(responses, axis=0))
    return np.stack(columns, axis=1)


def bracket(knots, value):
    hi = min(max(int(np.searchsorted(knots, value)), 1), len(knots) - 1)
    lo = hi - 1
    return lo, hi, (value - knots[lo]) / (knots[hi] - knots[lo])


def blend(a, b, fraction):
    return a + (b - a) * fraction


def kernel(bank, depth, method):
    if method == BASE:
        return bank[-1] * (depth / 100)
    lo, hi, weight = bracket(ANCHORS, depth)
    if method == LINEAR:
        return blend(bank[lo], bank[hi], weight)
    if method == QUADRATIC:
        third = min((i for i in (lo - 1, hi + 1) if 0 <= i < len(ANCHORS)),
                    key=lambda i: abs(ANCHORS[i] - depth))
        nodes = (lo, hi, third)
        return sum(bank[i] * np.prod([(depth - ANCHORS[j]) / (ANCHORS[i] - ANCHORS[j])
                                      for j in nodes if i != j]) for i in nodes)
    # The sweep suggests near-linear Side and a much steeper Mid growth law.
    # Divide out approximate orders before interpolating the shape, not the log magnitude.
    order = np.array([2, 1])[:, None, None]
    a, b = max(ANCHORS[lo], ANCHORS[1]), ANCHORS[hi]
    return blend(bank[max(lo, 1)] * (depth / a) ** order,
                 bank[hi] * (depth / b) ** order, weight)


def selected_kernel(bank, depth, methods):
    mid, side = methods
    h = kernel(bank, depth, mid)
    if side != mid:
        h[1] = kernel(bank, depth, side)[1]
    return h


def fit_context(bank):
    """Fit a clipped Mid-only change to A's tail, in three short windows after B."""
    routes = [(CONTEXT["L"], CONTEXT["R"]), (CONTEXT["MONO"], CONTEXT["MONO"])]
    gains = np.zeros((len(CONTEXT_DEPTHS), len(GAPS), len(WINDOWS) - 1))
    for j, gap in enumerate(GAPS):
        pairs = [pair_impulses(gap, a, b, sign=sign) for a, b in routes for sign in (-1, 1)]
        off = [capture(a + b, 0) for a, b in pairs]
        for i, depth in enumerate(CONTEXT_DEPTHS[1:], 1):
            h = bank[ANCHORS.index(depth), 0]
            prior, residual = [], []
            for (a, b), zero in zip(pairs, off):
                actual = ms(capture(a + b, depth / 100) - zero)[0]
                reference = a[:, 0] @ h[:, gap:gap + WINDOWS[-1]]
                second = b[:, gap] @ h[:, :WINDOWS[-1]]
                prior.append(reference)
                residual.append(actual[gap:gap + WINDOWS[-1]] - reference - second)
            for k, (lo, hi) in enumerate(zip(WINDOWS, WINDOWS[1:])):
                p, r = np.asarray(prior)[:, lo:hi], np.asarray(residual)[:, lo:hi]
                power = np.sum(p * p)
                if power:
                    gains[i, j, k] = np.clip(np.sum(p * r) / power, -1, 0)
    return gains


def context_correction(x, depth, h, gains, windows):
    """A scale-invariant isolated-attack heuristic, not a general envelope model."""
    result = np.zeros_like(x, dtype=np.float64)
    if windows == 0:
        return result
    amplitude = np.max(np.abs(x), axis=0)
    quiet = WINDOWS[1]
    history = np.lib.stride_tricks.sliding_window_view(np.pad(amplitude, (quiet, 0)), quiet + 1)
    # Prefilter so computing the prior peak is cheap, even on dense audio.
    candidates = np.flatnonzero((amplitude > 0) & (amplitude > 4 * np.r_[0, amplitude[:-1]]))
    events = candidates[np.max(history[candidates, :-1], axis=1) < 0.25 * amplitude[candidates]]
    lo, hi, weight = bracket(CONTEXT_DEPTHS, depth)
    by_gap = blend(gains[lo], gains[hi], weight)
    for a, b in zip(events, events[1:]):
        gap = b - a
        if not GAPS[0] <= gap <= GAPS[-1]:
            continue
        lo, hi, weight = bracket(GAPS, gap)
        # Fit at equal levels only: fade the notch for a weaker B, do not extrapolate above unity.
        gate = min(float(amplitude[b] / amplitude[a]), 1)
        attenuation = gate * blend(by_gap[lo], by_gap[hi], weight)
        for g, lo, hi in zip(attenuation[:windows], WINDOWS, WINDOWS[1:]):
            hi = min(hi, x.shape[-1] - b)
            if hi > lo:
                result[0, b + lo:b + hi] += g * (x[:, a] @ h[0, :, gap + lo:gap + hi])
    return result


def clicks(groups):
    """Each tuple is (first sample, gap, first L/R, second L/R)."""
    out = np.zeros((2, max(start + gap for start, gap, _, _ in groups) + 1))
    for start, gap, a, b in groups:
        out[:, start], out[:, start + gap] = a, b
    return out


def final_audio():
    """New deterministic waveforms; no reused probe audio in the final split."""
    t = np.arange(RATE) / RATE
    phase = 2 * np.pi * (146 * t + 2.4 * t ** 2 + 0.08 * np.sin(10 * np.pi * t))
    harmonics = np.arange(1, 43)
    weights = (0.15 + np.exp(-0.5 * ((harmonics * 146 - 2130) / 430) ** 2)) / np.sqrt(harmonics)
    voice = weights @ np.cos(harmonics[:, None] * phase)
    gate = np.maximum(0, np.cos(2 * np.pi * (3.15 * t - 0.21))) ** 1.5
    mono = voice * gate * np.minimum((t + 1 / RATE) / 0.012, 1)

    rng = np.random.default_rng(44371)
    f = np.fft.rfftfreq(t.size, 1 / RATE)
    stereo = []
    for rate, center in ((2.1, 1900), (3.2, 3100)):
        spectrum = np.fft.rfft(rng.standard_normal(t.size))
        body = np.fft.irfft(spectrum / (1 + ((f - center) / 1200) ** 2), t.size)
        stereo.append(body * (0.15 + 0.85 * np.sin(2 * np.pi * rate * t) ** 4))

    bed = np.cos(2 * np.pi * 2180 * t) + 0.28 * np.cos(2 * np.pi * 1530 * t)
    bed *= 0.24 * np.minimum((t + 1 / RATE) / 0.01, 1) * np.minimum((1 - t) / 0.03, 1)
    burst = np.zeros_like(t)
    on, n = BURST_ON, BURST_LENGTH
    burst[on:on + n] = np.hanning(n) * np.cos(2 * np.pi * 2520 * t[:n] + 0.4)
    return (
        ("harmonic mono", np.stack((mono, mono)), -20),
        ("stereo texture", np.stack(stereo), -14),
        ("bed + burst", np.stack((bed + burst, bed)), -17),
        ("bed - burst", np.stack((bed - burst, bed)), -17),
        ("unseen click pairs", clicks(((0, 173, (0, 1), (0.7, 0)),
            (RATE // 3, 407, (0.8, 0.8), (-0.8, -0.8)),
            (2 * RATE // 3, 689, (1, 0), (0, 1.2)),
            (RATE, 1331, (1, 0), (-0.5, 0)))), -27),
    )


def ir_scores(bank, off, depths, level, selected=None):
    print("  Depth | " + " | ".join((BASE, *INTERPOLATORS))
          + (" | selected" if selected else "") + " (M/S, pooled over L/R bases)")
    losses = {name: [] for name in INTERPOLATORS}
    for depth in depths:
        ref = (matrix(depth, (level,)) - off).reshape(2, -1)
        scores = {name: error_db(ref, kernel(bank, depth, name).reshape(2, -1))
                  for name in (BASE, *INTERPOLATORS)}
        if selected:
            scores["selected"] = error_db(ref, selected_kernel(bank, depth, selected).reshape(2, -1))
        else:
            for name in losses:
                losses[name].append(10 ** (scores[name] / 10))
        print(f"  {depth:5}% | " + " | ".join(fmt(v) for v in scores.values()))
    if not selected:
        return tuple(min(losses, key=lambda name: np.mean(np.array(losses[name])[:, i])) for i in (0, 1))
    return selected


def measurements(audio, depths):
    for name, dry, level in audio:
        x = (dry * (10 ** (level / 20) / np.max(np.abs(dry)))).astype(np.float32)
        x = np.pad(x, ((0, 0), (0, TAIL)))
        off = capture(x, 0)
        for depth in depths:
            yield name, level, depth, x, ms(capture(x, depth / 100) - off)


def develop_context(bank, methods, gains):
    audio = (("probe syllables", LTI["speech_like"](), -18),
             ("probe stereo", LTI["decorrelated"](), -18),
             ("development pairs", clicks(((0, 320, (1, 0), (0, -0.8)),
                 (RATE // 3, 780, (1, 1), (0.55, 0.55)),
                 (2 * RATE // 3, 1440, (0, 1), (1, 0)))), -24))
    losses = {w: [] for w in (0, 1, 3)}
    print("Development audio: interpolator only / 1 ms notch / 5 ms stepped notch (M/S dBr)")
    for name, _, depth, x, ref in measurements(audio, DEV_DEPTHS):
        h = selected_kernel(bank, depth, methods)
        raw = predict(h, x)
        scores = {w: error_db(ref, raw + context_correction(x, depth, h, gains, w)) for w in losses}
        print(f"  {name:<18} {depth:2}% | " + " | ".join(fmt(v) for v in scores.values()))
        for w, score in scores.items():
            losses[w].append(10 ** (score[0] / 10))
    means = {w: np.mean(loss) for w, loss in losses.items()}
    print("  Mean normalized Mid error power (dBr): " +
          ", ".join(f"{w} windows = {10 * np.log10(v):.3f}" for w, v in means.items()))
    return min(means, key=means.get)


def evaluate_audio(bank, methods, gains, windows):
    print("Final audio: matching-input D0 subtraction, full signal plus 1 s tail.")
    print("  Audio (peak dBFS)          D | Delta RMS M/S dBFS | scaled-full | linear | "
          "selected | selected+context (errors M/S dBr) | burst-only selected+context")
    comparisons = {"vs scaled full": [], "vs selected interpolation": []}
    for name, level, depth, x, ref in measurements(final_audio(), TEST_DEPTHS):
        h = selected_kernel(bank, depth, methods)
        raw = predict(h, x)
        corrected = raw + context_correction(x, depth, h, gains, windows)
        baseline, linear = (error_db(ref, predict(kernel(bank, depth, m), x)) for m in (BASE, LINEAR))
        selected, final = error_db(ref, raw), error_db(ref, corrected)
        comparisons["vs scaled full"].append(final - baseline)
        comparisons["vs selected interpolation"].append(final - selected)
        w = slice(BURST_ON, BURST_ON + BURST_LENGTH)
        burst = fmt(error_db(ref[:, w], corrected[:, w])) if name.startswith("bed") else "--"
        print(f"  {name:<19} {level:3} {depth:4}% | {fmt(LTI['db_power'](energy(ref) / x.shape[-1])):>16} | "
              + " | ".join([*(fmt(v) for v in (baseline, linear, selected, final)), burst]))
    for label, differences in comparisons.items():
        d = np.array(differences)
        print(f"  {label}: median change {fmt(np.median(d, axis=0))} dB M/S; "
              f"worst change {fmt(np.max(d, axis=0))}; "
              f"regressions >0.1 dB {np.sum(d > 0.1, axis=0).tolist()} of {len(d)} (M/S)")


def dense_burst(t, center, burst_hz, start, duration, polarity):
    bed = 0.24 * (np.cos(2 * np.pi * center * t) + 0.35 * np.sin(2 * np.pi * 0.71 * center * t))
    bed *= np.minimum((t + 1 / RATE) / 0.014, 1)
    lo, n = round(start * RATE), round(duration * RATE)
    lr = np.stack((bed, bed))
    lr[0, lo:lo + n] += polarity * np.hanning(n) * np.cos(2 * np.pi * burst_hz * t[:n] + 0.7)
    return lr, slice(lo, lo + n)


def dense_audio(held_out):
    t = np.arange(RATE) / RATE
    if not held_out:
        bed, w = dense_burst(t, 1970, 2710, 0.36, 0.018, 1)
        return (("probe syllables", LTI["speech_like"](), -18, None),
                ("probe stereo", LTI["decorrelated"](), -18, None),
                ("development bed", bed, -19, w))
    phase = 2 * np.pi * (179 * t + 4 * t ** 2)
    harmonics = np.arange(1, 33)
    weights = np.exp(-((179 * harmonics - 2520) / 790) ** 2) + 0.1
    voice = (weights / np.sqrt(harmonics)) @ np.sin(harmonics[:, None] * phase + 0.9)
    voice *= 0.04 + np.maximum(np.sin(2 * np.pi * 3.7 * t), 0) ** 2
    rng = np.random.default_rng(68131)
    f = np.fft.rfftfreq(t.size, 1 / RATE)
    noise = np.fft.rfft(rng.standard_normal((2, t.size)))
    stereo = np.fft.irfft(noise / (1 + ((f - np.array([[1650], [2860]])) / 860) ** 4), t.size)
    stereo *= 0.22 + 0.78 * np.cos(2 * np.pi * np.array([[2.8], [4.3]]) * t) ** 2
    plus, w = dense_burst(t, 2385, 2190, 0.427, 0.015, 1)
    minus, _ = dense_burst(t, 2385, 2190, 0.427, 0.015, -1)
    return (("held-out harmonic", np.stack((voice, voice)), -22, None),
            ("held-out texture", stereo, -15, None),
            ("held-out bed + burst", plus, -16, w),
            ("held-out bed - burst", minus, -16, w))


def dense_feature(x, h):
    # Two adjacent causal windows; zero-pad unavailable history. The sign
    # distinguishes an increase in input power from a drop against the bed.
    power = np.mean(np.square(x.astype(np.float64)), axis=0)
    sums = np.cumsum(np.pad(power, (BED + RECENT, 0)))
    recent = (sums[BED + RECENT:] - sums[BED:-RECENT]) / RECENT
    bed = (sums[BED:-RECENT] - sums[:-BED - RECENT]) / BED
    contrast = (recent - bed) / (recent + bed + 1e-12)
    tail = h[:1].copy()
    tail[:, :, :RECENT] = 0
    return contrast * predict(tail, x)[0]


def dense_cases(audio, depths, bank, methods, gains, windows):
    regions = {name: w for name, _, _, w in audio}
    for name, _, depth, x, ref in measurements((case[:3] for case in audio), depths):
        h = selected_kernel(bank, depth, methods)
        baseline = predict(h, x)[0] + context_correction(x, depth, h, gains, windows)[0]
        yield name, depth, ref[0], baseline, dense_feature(x, h), regions[name]


def dense_diagnostic(bank, methods, gains, windows, develop_only):
    # Fit one global gain by equal-case normalized Mid error, not raw power.
    assert not set(DENSE_TEST_DEPTHS) & (set(ANCHORS) | set(DEV_DEPTHS) | set(TEST_DEPTHS))
    dev = list(dense_cases(dense_audio(False), DEV_DEPTHS, bank, methods, gains, windows))
    numerator, denominator = 0.0, 0.0
    for _, _, ref, baseline, feature, _ in dev:
        ref_power = energy(ref)
        numerator += np.dot(ref - baseline, feature) / ref_power
        denominator += energy(feature) / ref_power
    gain = float(np.clip(numerator / denominator, -0.5, 0.5)) if denominator else 0.0
    print("\nDense diagnostic only: 5 ms vs preceding 20 ms power contrast, Mid IR lag >=5 ms.")
    print(f"  Locked development gain {gain:+.8f} (bounded to +/-0.5); Side unchanged.")

    def report(label, cases):
        totals = []
        print(f"{label}: baseline -> with dense feature (Mid dBr; negative change is better)")
        for name, depth, ref, baseline, feature, w in cases:
            adjusted = baseline + gain * feature
            before, after = error_db(ref, baseline), error_db(ref, adjusted)
            totals.append((10 ** (before / 10), 10 ** (after / 10)))
            burst = f"; burst {error_db(ref[w], baseline[w]):+.3f} -> {error_db(ref[w], adjusted[w]):+.3f}" if w else ""
            print(f"  {name:<22} {depth:2}%: {before:+.3f} -> {after:+.3f} ({after - before:+.3f} dB){burst}")
        first, second = np.array(totals).T
        differences = 10 * np.log10(second / first)
        mean_before, mean_after = 10 * np.log10(np.mean(totals, axis=0))
        print(f"  Mean normalized error: {mean_before:+.4f} -> {mean_after:+.4f} dBr; "
              f"median change {np.median(differences):+.4f} dB, worst {max(differences):+.4f} dB; "
              f"regressions >0.1 dB: {np.sum(differences > 0.1)}/{len(totals)}")

    report("Development (fit)", dev)
    if not develop_only:
        report("Fresh dense holdout (no fitting)",
               dense_cases(dense_audio(True), DENSE_TEST_DEPTHS, bank, methods, gains, windows))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--develop", action="store_true", help="do not render final holdouts")
    parser.add_argument("--dense-diagnostic", action="store_true", help="evaluate one experimental Mid-tail feature only")
    args = parser.parse_args()
    identify()
    print(f"Fit depths {ANCHORS}%; dev {DEV_DEPTHS}%; final {TEST_DEPTHS}%. "
          f"Train IR levels {LEVELS} dBFS; dev -24, final -19 dBFS.")
    assert not (set(ANCHORS) & set(DEV_DEPTHS) or set(ANCHORS) & set(TEST_DEPTHS)
                or set(DEV_DEPTHS) & set(TEST_DEPTHS))
    print("Errors: 20 log10(RMS prediction error / RMS measured Depth-delta), lower is better. M/S=(L +/- R)/2.")
    x = pair_impulses(512, CONTEXT["L"], CONTEXT["R"])[0]
    for depth in (0, 1):
        if np.max(np.abs(capture(x, depth) - capture(x, depth, fresh=True))) > 1e-7:
            raise RuntimeError("Reset/reuse failed the fresh-instance impulse control")
    off = matrix(0, LEVELS)
    bank = np.stack([matrix(d, LEVELS) - off if d else np.zeros_like(off) for d in ANCHORS])
    print("\nDevelopment isolated L/R impulse matrix:")
    methods = ir_scores(bank, off, DEV_DEPTHS, -24)
    print(f"Selected independently by mean normalized error power on dev impulses: "
          f"Mid = {methods[0]}, Side = {methods[1]}")
    print(f"Fit context at Depths {CONTEXT_DEPTHS[1:]}%, gaps {GAPS} samples, "
          "equal -24 dBFS hits, L>R and mono>mono, both second-hit polarities.")
    gains = fit_context(bank)
    windows = develop_context(bank, methods, gains)
    print(f"Selected on dev audio only: {windows} notch windows, {WINDOWS[windows] / RATE * 1000:g} ms, Mid only.")
    if args.dense_diagnostic:
        dense_diagnostic(bank, methods, gains, windows, args.develop)
        return
    if not args.develop:
        print("\nFinal isolated L/R impulse matrix (not used to select the model):")
        ir_scores(bank, off, TEST_DEPTHS, -19, selected=methods)
        evaluate_audio(bank, methods, gains, windows)
    print("\nScope: no dense envelope/state model, no Side context correction, no automation or other controls, "
          "no natural-vocal listening. All renders and coefficients are ephemeral.")


if __name__ == "__main__":
    main()
