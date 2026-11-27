"""Offline, one-band reflection-envelope experiment; no audio or kernels saved.

From the research worktree root, with the existing measurement dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Fit-Spatial-EQ-ReflectionEnvelope.py

The target is the boosted band's Mid Depth delta, not the whole plugin output.
See docs/Spatial-EQ-Reference-Measurements.md for the fixture and reference hash.
"""

from pathlib import Path
from runpy import run_path
from time import perf_counter


OVERLAP = run_path(str(Path(__file__).resolve().with_name("Fit-Spatial-EQ-MidOverlap.py")))
BASE = OVERLAP["BASE"]
np, capture, ms, predict, energy, samples = (
    OVERLAP[k] for k in ("np", "capture", "ms", "predict", "energy", "samples")
)
RATE, ANCHORS, LEVELS, TAIL = (BASE[k] for k in ("RATE", "ANCHORS", "LEVELS", "TAIL"))
DEPTHS = OVERLAP["DEPTHS"]
EPSILON = 1e-15
ARRIVAL = samples(6) - 2  # 286 samples at 48 kHz: the documented -100 dBr Mid onset.


def detector(off=None):
    """Build a fixed, input-only detector; an impulse-derived direct EQ is offline calibration."""
    w = 2 * np.pi * 2400 / RATE
    direct = None
    if off is None:
        alpha = np.sin(w) / 4  # Previous development-selected constant-peak BP, Q=2.
        a0, a1, a2 = 1 + alpha, -2 * np.cos(w), 1 - alpha
        k = alpha
    else:
        q = 1.0
        r = np.exp(-w / (2 * q))
        angle = w * np.sqrt(1 - 1 / (4 * q * q))
        a0, a1, a2 = 1, -2 * r * np.cos(angle), r * r
        z = np.exp(-1j * w)
        k = abs((1 + a1 * z + a2 * z * z) / (1 - z * z))
        np.testing.assert_allclose([k, a1, a2], [0.1348032593, -1.6463993738, 0.7304026910],
                                   rtol=0, atol=1e-9)
        print(f"Matched-pole BP: numerator [{k:.10f}, 0, {-k:.10f}], "
              f"denominator [1, {a1:.10f}, {a2:.10f}]; unity at 2400 Hz.")
        direct = np.stack((off[0] + off[1], off[0] - off[1]))  # Fixed MS-to-LR impulse matrix.
    b = (k, 0, -k)
    h = np.zeros(samples(32), dtype=np.float64)
    for i in range(h.size):
        h[i] = (b[i] if i < len(b) else 0) / a0
        if i >= 1:
            h[i] -= a1 * h[i - 1] / a0
        if i >= 2:
            h[i] -= a2 * h[i - 2] / a0

    def gain(x):
        source = x if direct is None else predict(direct, x)
        band = np.stack([np.convolve(channel, h)[:x.shape[-1]] for channel in source])
        power = np.mean(band ** 2, axis=0)
        return np.clip(np.sqrt((pole(power, 40) + EPSILON) / (pole(power, 2) + EPSILON)), 0.2, 1)

    return gain


def pole(power, time_ms):
    """Causal one-pole scan in chunks; no FFT roundoff near the energy floor."""
    step = -1000 / (RATE * time_ms)
    decay = np.exp(step * np.arange(1, 257))
    feed = -np.expm1(step) / decay
    y = np.empty_like(power)
    state = 0.0
    for i in range(0, power.size, decay.size):
        block = power[i:i + decay.size]
        n = block.size
        y[i:i + n] = decay[:n] * (state + np.cumsum(feed[:n] * block))
        state = y[i + n - 1]
    return y



def impulse(level, channel):
    x = np.zeros((2, TAIL), dtype=np.float32)
    x[channel, 0] = 10 ** (level / 20)
    return x


def impulse_bank():
    def matrix(depth, level):
        columns = []
        for ch in range(2):
            x = impulse(level, ch)
            columns.append(ms(capture(x, depth / 100)) / float(x[ch, 0]))
        return np.stack(columns, axis=1)

    # Layout: anchor, calibration level, M/S output, L/R input, sample.
    measured = np.stack([[matrix(d, level) for level in LEVELS] for d in ANCHORS])
    off = measured[0].mean(axis=0)
    return off, measured.mean(axis=1) - off, (measured[1:, :, 0] - measured[0, :, 0])


def return_bank(mid, gain):
    gains = np.stack([[gain(impulse(level, ch)) for ch in range(2)] for level in LEVELS])
    ungated = (mid / gains).mean(axis=1)
    return np.concatenate([np.zeros_like(ungated[:1]), ungated])[:, None]


def split(holdout, bank, method, context, models):
    fixtures = OVERLAP["fixtures"](holdout)
    measured = OVERLAP["measure"](fixtures, DEPTHS[holdout], bank, method, context)
    preds = {key: [] for key in models}
    cases, return_check = [], {key: [] for key in models}
    for f in fixtures:
        signals = (f.hit,) if f.bed is None else (f.bed + f.hit, f.bed, f.hit)
        gains = {key: [gain(x) for x in signals] for key, (gain, _) in models.items()}
        for depth in DEPTHS[holdout]:
            case = next(measured)
            assert case.name == f.name and case.depth == depth
            cases.append(case)
            for key, (_, kernels) in models.items():
                h = BASE["kernel"](kernels, depth, method)
                p = np.stack([predict(h, x)[0] * g for x, g in zip(signals, gains[key])])
                preds[key].append(p)
                if holdout and not case.overlap:
                    # Independently measured packets/clicks (not calibration impulses):
                    # compare the return itself after removing the hypothesized gain.
                    g = gains[key][0][f.on:]
                    target = case.ref[0, f.on:] / g
                    return_check[key].append((f.name, normalized_error(target, p[0, f.on:] / g)))
    # Exhaust the generator so the baseline's render-control checks run.
    if next(measured, None) is not None:
        raise RuntimeError("Unexpected extra overlap render")
    return cases, preds, return_check


def fit_baseline(cases):
    loss, fixed = OVERLAP["loss"], OVERLAP["fixed"]
    notch = min((0, 1, 3), key=lambda w: loss(cases, [fixed(c, w) for c in cases]))
    weights = {loc: OVERLAP["fit"](cases, loc, notch) for loc in OVERLAP["LOCATIONS"]}

    def corrected(cases, loc):
        return [fixed(c, notch) + c.columns[loc] @ weights[loc] for c in cases]

    chosen = min(weights, key=lambda loc: loss(cases, corrected(cases, loc)))
    print(f"Existing baseline locked: {notch} click-notch windows; previous challenger: {chosen}.")
    return lambda cases: {"LTI": [c.lti for c in cases],
                          "fixed": [fixed(c, notch) for c in cases],
                          f"old {chosen}": corrected(cases, chosen)}


def normalized_error(y, p):
    power = energy(y)
    if not power or not np.all(np.isfinite(p)):
        raise RuntimeError("Zero-power reference or non-finite prediction in scoring window")
    return energy(y - p) / power


def views(c):
    if not c.overlap:
        yield ("isolated clicks" if "click" in c.name else "isolated packets"), (1,), slice(c.on, None)
        return
    early = slice(c.on, c.on + ARRIVAL)
    late = slice(c.on + ARRIVAL, c.end + samples(25))
    burst = slice(c.on, c.end)
    yield "burst total", (1, 0, 0), burst
    yield "burst increment", (1, -1, 0), burst
    yield "burst E only", (1, -1, -1), burst
    yield "pre-hit bed", (1, 0, 0), slice(c.on - samples(25), c.on)
    yield "post-hit 25 ms", (1, 0, 0), slice(c.end, c.end + samples(25))
    for name, window in (("early", early), ("late", late)):
        yield f"{name} total", (1, 0, 0), window
        yield f"{name} increment", (1, -1, 0), window
        yield f"{name} E only", (1, -1, -1), window
    stratum = {"steady": "sustained", "chopped": "gated", "release": "decaying"}[c.name.split("-")[0]]
    yield f"{stratum} burst total", (1, 0, 0), burst


def db(power):
    return 10 * np.log10(power)


def report(cases, preds):
    rows = {}
    for case, tracks in zip(cases, zip(*preds.values())):
        for name, mix, window in views(case):
            y = np.asarray(mix) @ case.ref[:, window]
            errors = [normalized_error(y, np.asarray(mix) @ p[:, window]) for p in tracks]
            rows.setdefault(name, []).append(errors)
    print(f"  {'Mid dBr (equal case power)':25}" + "".join(f"{p:>10}" for p in preds)
          + "  exact vs fixed / approx: worst / N >0.1 dB")
    changes, worst = {}, {}
    fixed, approx, exact = (list(preds).index(key) for key in ("fixed", "approx", "exact"))
    for name, values in rows.items():
        power = np.asarray(values)
        score = db(power.mean(axis=0))
        deltas = [db(power[:, exact] / power[:, j]) for j in (fixed, approx)]
        changes[name], worst[name] = score[exact] - score[fixed], max(deltas[0])
        print(f"  {name:25}" + "".join(f"{v:10.2f}" for v in score)
              + "".join(f"  {max(d):+6.2f} / {np.sum(d > 0.1):2}/{len(d):2}" for d in deltas))
    guarded = ("burst increment", "pre-hit bed", "post-hit 25 ms", "isolated packets", "isolated clicks",
               "early total", "early increment", "late total", "late increment")
    passes = (changes["burst total"] <= -0.5 and all(changes[k] <= 0.1 for k in guarded)
              and all(v <= 0.5 for k, v in worst.items() if not k.endswith("E only")))
    print(f"Original relative guard vs fixed (including early and late): burst improvement >=0.5 dB, "
          f"guarded means regress <=0.1 dB, no non-E case regresses >0.5 dB: {'PASS' if passes else 'FAIL'}.")


def early_floor_control(cases, preds):
    """Output-quantization sensitivity of the early hit increment, not a new benchmark."""
    keys = ("fixed", "approx", "exact")
    tracks = {(c.name, c.depth): (c, *(preds[k][i] for k in keys))
              for i, c in enumerate(cases) if c.overlap}
    errors, bounds, worst_repeat = [], [], 0.0
    comparisons = {key: [] for key in keys[:-1]}
    print("Early increment floor control (FS RMS, exact vs fixed and approx):")
    print("  Margin = 2 x RMS of the samplewise half-ULP sum of four float32 renders folded to Mid.")
    print("  It bounds sensitivity of the difference of two RMS errors to final output rounding only.")
    print(f"  {'case':17} {'ref RMS':>10} {'fixed err':>10} {'approx err':>10} {'exact err':>10} {'margin':>10}"
          "  exact minus fixed / approx (dB); cases with either >0.1 dB")
    for f in OVERLAP["fixtures"](True):
        if f.bed is None:
            continue
        window = slice(f.on, f.on + ARRIVAL)
        signals = f.bed + f.hit, f.bed
        off = [capture(x, 0)[:, window] for x in signals]
        for depth in DEPTHS[1]:
            on = [capture(x, depth / 100)[:, window] for x in signals]
            raw = np.stack(off + on)
            raw32 = raw.astype(np.float32)
            if not np.array_equal(raw, raw32):
                raise RuntimeError("Expected float32 renders for the output rounding control")
            # 1/2 ULP per render and 1/2 per channel from the Mid fold-down.
            sample_bound = 0.25 * np.spacing(np.abs(raw32)).sum(axis=(0, 1), dtype=np.float64)
            bound = rms(sample_bound)
            bounds.append(bound)
            c, *models = tracks[f.name, depth]
            y = OVERLAP["incremental"](c.ref)[window]
            repeat = ms(on[0] - off[0] - (on[1] - off[1]))[0]
            worst_repeat = max(worst_repeat, rms(y - repeat))
            err = [rms(y - OVERLAP["incremental"](p)[window]) for p in models]
            errors.append(err)
            relative = db((err[-1] / np.asarray(err[:-1])) ** 2)
            for key, delta, prior in zip(keys[:-1], relative, err):
                comparisons[key].append((delta, err[-1] - prior > 2 * bound))
            if max(relative) > 0.1:
                print(f"  {f.name + f' {depth}%':17} {rms(y):10.2e} "
                      + " ".join(f"{v:10.2e}" for v in (*err, 2 * bound))
                      + f"  {relative[0]:+.2f} / {relative[1]:+.2f}")
    print("  Pooled absolute error: " + ", ".join(f"{k} {rms(e):.2e} FS RMS"
                                               for k, e in zip(keys, zip(*errors))))
    print(f"  Per-case margins {2 * min(bounds):.2e}–{2 * max(bounds):.2e} FS RMS; "
          f"max same-host rerender difference {worst_repeat:.2e} FS RMS.")
    for key, values in comparisons.items():
        print(f"  Exact vs {key}: {sum(d > 0.5 for d, _ in values)}/{len(values)} exceed the original +0.5 dB cap; "
              f"{sum(d > 0.5 and over for d, over in values)}/{len(values)} also exceed the absolute margin.")


def rms(x):
    return np.sqrt(np.mean(np.square(x)))


def steady_return_control(models, method):
    """Independent steady drives: score unmodulated return without dividing the capture."""
    t = np.arange(samples(320)) / RATE
    window = slice(samples(220), None)
    print("Independent 220–320 ms steady-tone return control: ungated kernel vs measured Mid delta.")
    for hz, route in ((1500, (1, 0)), (2400, (1, 1)), (3500, (0, 1))):
        x = (np.asarray(route)[:, None] * 0.05 * np.cos(2 * np.pi * hz * t)).astype(np.float32)
        off = capture(x, 0)
        targets = [ms(capture(x, d / 100) - off)[0, window] for d in DEPTHS[1]]
        results = []
        for key, (gain, kernels) in models.items():
            errors = [normalized_error(y, predict(BASE["kernel"](kernels, depth, method), x)[0, window])
                      for depth, y in zip(DEPTHS[1], targets)]
            results.append(f"{key} {db(np.mean(errors)):.2f} dBr (min gain {gain(x)[window].min():.5f})")
        print(f"  {hz} Hz {route}: " + ", ".join(results))


def main():
    start = perf_counter()
    BASE["identify"]()
    assert not set(DEPTHS[0]) & set(DEPTHS[1])
    assert not set(ANCHORS) & set(sum(DEPTHS, ()))
    print(f"Reflection-only Mid proxy: per-render Depth 0 subtraction, calibration impulses normalized "
          f"by input peak at {LEVELS} dBFS. No Side or delay-network reconstruction.")
    print(f"Development Depth {DEPTHS[0]}%; holdout {DEPTHS[1]}%. E = Delta(AB)-Delta(A)-Delta(B).")
    off, bank, mid = impulse_bank()
    early_power = energy(mid[..., :ARRIVAL].ravel()) / energy(mid.ravel())
    print(f"Calibration Mid energy before sample {ARRIVAL}: {db(early_power):.1f} dBr.")
    method, _ = BASE["ir_scores"](bank, off, BASE["DEV_DEPTHS"], -24)
    context = BASE["fit_context"](bank)
    detectors = {"approx": detector(), "exact": detector(off)}
    models = {key: (gain, return_bank(mid, gain)) for key, gain in detectors.items()}
    print(f"Fixed architecture: {method} kernel interpolation, gain after convolution in both models.")
    print("Approx: prior Q=2 bandpass on input. Exact-steady frontend: Q=1 matched-pole BP after "
          "the measured Depth-0 EQ impulse matrix (offline calibration only).")
    print("Development renders:")
    dev, preds, _ = split(False, bank, method, context, models)
    baseline = fit_baseline(dev)
    loss = OVERLAP["loss"]
    costs = {key: loss(dev, p) for key, p in preds.items()}
    print("Development objective (60% burst total / 30% increment / 10% isolated; no detector selection): "
          + ", ".join(f"{key} {db(value):.2f} dBr" for key, value in costs.items()))
    del dev, preds

    print(f"Holdout renders (no fitting): early [0, {ARRIVAL / RATE * 1000:.3f}) ms from hit onset, "
          "late from that boundary through hit end +25 ms.")
    held, new, returns = split(True, bank, method, context, models)
    preds = baseline(held) | new
    report(held, preds)
    early_floor_control(held, preds)
    print("Return control: held-out isolated Mid, divided by each detector gain; "
          "compare the de-enveloped waveform to the convolved kernel (equal case power).")
    for label, click in (("packets", False), ("clicks", True)):
        results = [f"{key} {db(np.mean([e for name, e in group if ('click' in name) == click])):.2f} dBr"
                   for key, group in returns.items()]
        print(f"  {label}: " + ", ".join(results))
    steady_return_control(models, method)
    print(f"Elapsed {perf_counter() - start:.1f} s. No audio or coefficients saved; no listening test.")


if __name__ == "__main__":
    main()
