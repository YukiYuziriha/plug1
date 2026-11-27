"""Compare the input-only delay model to the installed reference, entirely in memory.

From the research worktree root, with the Measure-Spatial-EQ.py dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'tools/Invoke-HalfCpu.ps1' -Executable python -CommandArguments @('-B','tools/Test-Spatial-EQ-DelayModel.py')"
"""

from pathlib import Path
from runpy import run_path
from time import perf_counter
from types import FunctionType, SimpleNamespace


TOOLS = Path(__file__).resolve().parent
# Loads MidOverlap via run_path and sets up the reference's local NumPy path.
REFLECTION = run_path(str(TOOLS / "Fit-Spatial-EQ-ReflectionEnvelope.py"))
OVERLAP, BASE, ARRIVAL = (REFLECTION[k] for k in ("OVERLAP", "BASE", "ARRIVAL"))
np, capture, ms, fixtures, DEPTHS = (OVERLAP[k] for k in ("np", "capture", "ms", "fixtures", "DEPTHS"))
RATE, LEAD = BASE["RATE"], BASE["LEAD"]
BELL = dict(frequency=2400.0, gain_db=6.0, q=1.0)

# Published held-out Mid values, not a rerun or a Side baseline.
# docs/Spatial-EQ-Reference-Measurements.md, "Mid reconstruction: gain on the reflection return".
PUBLISHED_FIXED = {
    "burst total": -4.45,
    "burst increment": -1.87,
    "early increment": 0.08,
    "late increment": -18.69,
    "isolated packets": -16.46,
    "isolated clicks": -33.72,
}
SMALL_REF_RMS = 1e-7  # Diagnostic count only; never exclude a nonzero case from dBr.


def model_delta(process, x, depth, rate, lead, label, bell=BELL):
    padded = np.pad(x, ((0, 0), (lead, 0)))
    y = process(padded, depth, sample_rate=rate, **bell)
    if (not isinstance(y, np.ndarray) or y.shape != padded.shape
            or y.dtype != np.float64 or not np.isfinite(y).all()):
        raise RuntimeError(f"{label}: expected finite stereo float64 of shape {padded.shape}; "
                           f"got {type(y).__name__} {getattr(y, 'shape', None)} {getattr(y, 'dtype', None)}")
    return ms(y[:, lead:])


def stats(reference, prediction):
    error = prediction - reference
    return (np.mean(reference ** 2, axis=-1), np.mean(error ** 2, axis=-1),
            np.max(np.abs(error), axis=-1))


def report(rows, published=None):
    print(f"  {'view':28} {'out':4} {'N':>3} {'dBr':>9} {'ref RMS':>10} {'min ref':>10} "
          f"{'err RMS':>10} {'err peak':>10} {'small':>5} {'fixed dBr':>10}")
    for label, cases in rows.items():
        ref, err, peak = np.stack(cases, axis=1)
        for ch, mode in enumerate(("Mid", "Side")):
            nonzero = ref[:, ch] > 0
            ratio = np.mean(err[nonzero, ch] / ref[nonzero, ch]) if nonzero.any() else None
            db = "n/a" if ratio is None else f"{10 * np.log10(ratio):.2f}" if ratio > 0 else "-inf"
            small = np.count_nonzero(ref[:, ch] < SMALL_REF_RMS ** 2)
            fixed = published.get(label) if published is not None and ch == 0 else None
            baseline = "—" if fixed is None else f"{fixed:.2f}"
            print(f"  {label:28} {mode:4} {len(cases):3} {db:>9} "
                  f"{np.sqrt(ref[:, ch].mean()):10.2e} {np.sqrt(ref[:, ch].min()):10.2e} "
                  f"{np.sqrt(err[:, ch].mean()):10.2e} {peak[:, ch].max():10.2e} {small:5} {baseline:>10}")


def overlap_suite(process, holdout):
    rows = {}
    for f in fixtures(holdout):
        signals = (f.hit,) if f.bed is None else (f.bed + f.hit, f.bed, f.hit)
        off = [capture(x, 0.0) for x in signals]
        for percent in DEPTHS[holdout]:
            depth = percent / 100
            actual = np.stack([ms(capture(x, depth) - zero) for x, zero in zip(signals, off)])
            predicted = np.stack([model_delta(process, x, depth, RATE, LEAD, f"{f.name} {percent}%")
                                  for x in signals])
            if holdout and f.bed is not None:
                rows.setdefault("full AB total", []).append(stats(actual[0], predicted[0]))
            case = SimpleNamespace(name=f.name, on=f.on, end=f.end, overlap=f.bed is not None)
            for label, mix, window in REFLECTION["views"](case):
                weights = np.asarray(mix)
                y = weights @ actual.transpose(1, 0, 2)[..., window]
                p = weights @ predicted.transpose(1, 0, 2)[..., window]
                rows.setdefault(label, []).append(stats(y, p))
    report(rows, PUBLISHED_FIXED if holdout else None)


def click_capture(x, depth, rate, lead):
    if rate == RATE:
        return capture(x, depth)
    # As in Probe-Spatial-EQ-Rate.py, change only the host rate and lead.
    render = BASE["HOST"]["render"]
    configured = FunctionType(render.__code__, dict(render.__globals__, RATE=rate, LEAD=lead),
                              argdefs=render.__defaults__)
    configured.__kwdefaults__ = render.__kwdefaults__
    return configured(np.pad(x, ((0, 0), (lead, 0))), depth, start=lead)


def click_controls(process):
    print("Isolated -24 dBFS peak clicks in the stereo host, 80 ms from click onset:")
    depth_zero_peak = 0.0
    for rate in (RATE, 44_100):
        print(f"  {rate} Hz, {rate // 10} samples silent lead")
        rows = {}
        for route, levels in (("dual mono", (1, 1)), ("asym stereo", (1, -0.5))):
            lead = rate // 10
            x = np.zeros((2, round(0.08 * rate)), dtype=np.float32)
            x[:, 0] = np.asarray(levels) * 10 ** (-24 / 20)
            off = click_capture(x, 0.0, rate, lead)
            for depth in (0.0, 0.5, 1.0):
                label = f"{route} {depth:.0%}"
                y = ms(click_capture(x, depth, rate, lead) - off)
                p = model_delta(process, x, depth, rate, lead, f"{rate} Hz {label}")
                rows[label] = [stats(y, p)]
                if depth == 0.0:
                    depth_zero_peak = max(depth_zero_peak, np.max(np.abs(p)))
        report(rows)
    if depth_zero_peak != 0:
        raise RuntimeError(f"Depth 0 model delta must be zero; observed {depth_zero_peak:.3e} FS peak")


def mapped_controls(sweep, targets):
    host = sweep["HOST"]
    params = host["pb"].load_plugin(str(host["MODULE"])).parameters
    raw, displayed, readback = {}, {}, []
    for field, target in targets.items():
        param = params[sweep["key"](field)]
        raw[field] = sweep["raw_for_display"](param, field, target)
        text = sweep["set_raw"](param, raw[field])
        displayed[field] = sweep["display_number"](text, field)
        if displayed[field] != target:
            raise RuntimeError(f"{field}: expected display {target}, got {text!r}")
        raw[field] = param.raw_value
        readback.append(f"{field}={text} (raw {raw[field]:.9g})")
    print("  " + "; ".join(readback))
    return {**sweep["BASE"], **raw}, dict(frequency=displayed["frequency"],
                                           gain_db=displayed["gain"], q=displayed["q"])


def matrix_capture(sweep, x, depth, controls, rate, lead):
    host, key = sweep["HOST"], sweep["key"]
    plugin = host["pb"].load_plugin(str(host["MODULE"]))
    params = plugin.parameters
    settings = {f"band_{i}_active": 0.0 for i in range(1, 9)}
    settings.update({key(k): v for k, v in dict(active=1.0, enabled=1.0, shape=0.0,
                    stereo=0.0, dynamic=0.0, sidechain=0.0, depth=depth, **controls).items()})
    settings.update(processing=0.0, output=0.5, bypass=0.0, spatial_bypass=0.0, mono_audition=0.0)
    for k, v in settings.items():
        text = sweep["set_raw"](params[k], v)
        if k == key("depth") and sweep["display_number"](text, "depth") != round(100 * depth):
            raise RuntimeError(f"Expected Depth {depth:.0%}, got {text!r}")
    padded = np.pad(x, ((0, 0), (lead, 0)))
    out = plugin.process(padded, rate, buffer_size=host["BLOCK"])
    if out.shape != padded.shape or out.dtype != np.float32 or not np.isfinite(out).all():
        raise RuntimeError(f"Expected finite stereo float32 of shape {padded.shape}; got {out.shape} {out.dtype}")
    arrivals = np.flatnonzero(np.max(np.abs(out), axis=0) > 1e-7)
    if not arrivals.size or arrivals[0] != lead:
        raise RuntimeError(f"Expected direct arrival at {lead}, got {arrivals[:1]}")
    return out[:, lead:]


def long_stereo(rate):
    t = np.arange(rate) / rate
    body = (0.6 + 0.4 * np.cos(2 * np.pi * 3 * t)) * np.minimum(1, (1 - t) / 0.05)
    mid = body * (0.055 * np.cos(2 * np.pi * 6400 * t) + 0.03 * np.cos(2 * np.pi * 800 * t))
    side = 0.028 * body * np.sin(2 * np.pi * 5137 * t + 0.4)
    return np.pad(np.array((mid + side, mid - side), dtype=np.float32), ((0, 0), (0, rate // 4)))


def bell_matrix(process):
    sweep = run_path(str(TOOLS / "Sweep-Spatial-EQ-Controls.py"))
    print("Off-anchor static positive Stereo Bell matrix (48/96 kHz, 100 ms silent lead):")
    print("  Raw values center the controller's display intervals. Display strings show units and resolution;"
          " the model uses the displayed values, not presumed exact internal coefficients.")
    print("  L/R Depth subtraction and M/S fold are float64. 'round RMS' is the M/S RMS bound from"
          " half a float32 ULP per L/R sample per render (final-output rounding only).")
    for hz, gain, q in ((800, 3, 0.7), (6400, 12, 2)):
        controls, bell = mapped_controls(sweep, dict(frequency=hz, gain=gain, q=q))
        print(f"  {'rate':>3} {'stimulus':17} {'depth':>5} {'out':4} {'dBr':>9} {'ref RMS':>10} "
              f"{'err RMS':>10} {'err peak':>10} {'round RMS':>10}")
        for rate in (48_000, 96_000):
            lead = rate // 10
            dual = np.zeros((2, rate // 10), dtype=np.float32)
            dual[:, 0] = 10 ** (-24 / 20)
            asym = dual.copy()
            asym[1, 0] *= -0.5
            cases = [("dual click", dual, (0.33, 1.0)), ("asym click", asym, (0.33, 1.0))]
            if hz == 6400 and rate == 96_000:
                cases.append(("1.25 s asym tones", long_stereo(rate), (0.71,)))
            for label, x, depths in cases:
                off = matrix_capture(sweep, x, 0.0, controls, rate, lead)
                for depth in depths:
                    on = matrix_capture(sweep, x, depth, controls, rate, lead)
                    prediction = model_delta(process, x, depth, rate, lead, label, bell)
                    ref, err, peak = stats(ms(on.astype(np.float64) - off.astype(np.float64)), prediction)
                    ulps = np.spacing(np.abs(on)).astype(np.float64) + np.spacing(np.abs(off)).astype(np.float64)
                    rounding_rms = np.sqrt(np.mean((ulps.sum(axis=0) / 4) ** 2))
                    for ch, mode in enumerate(("Mid", "Side")):
                        db = f"{sweep['db'](np.sqrt(err[ch] / ref[ch])):.2f}" if ref[ch] else "n/a"
                        print(f"  {rate // 1000:3} {label:17} {depth:5.0%} {mode:4} {db:>9} "
                              f"{np.sqrt(ref[ch]):10.2e} {np.sqrt(err[ch]):10.2e} {peak[ch]:10.2e} {rounding_rms:10.2e}")


def main():
    start = perf_counter()
    process = run_path(str(TOOLS / "Spatial-EQ-DelayModel.py"))["process_depth_delta"]
    BASE["identify"]()
    print("Target: reference L/R at Depth minus its Depth-0 render, folded to M=(L+R)/2 and S=(L-R)/2.")
    print("No fitting, rescaling, or alignment shift. AB/A/B renders are separate; increment = AB−A; E = AB−A−B.")
    print(f"Early: first {ARRIVAL} samples ({ARRIVAL / RATE * 1000:.3f} ms) from hit onset; "
          "late: thereafter through hit end +25 ms.")
    print("dBr = 10 log10(mean per-case squared error/reference power), excluding exactly zero references. "
          "Absolute RMS (equal case weight) and peak are in full-scale units and include all cases.")
    print(f"'small' counts cases with ref RMS < {SMALL_REF_RMS:g} FS; near-zero dBr can be dominated by "
          "float32 subtraction/cancellation. Read it alongside absolute error and min ref RMS.")
    print("The fixed dBr column is published held-out Mid only, not recalibrated or a pass/fail threshold.")
    for holdout, title in enumerate(("Development", "Holdout")):
        print(f"{title}: Depth {DEPTHS[holdout]}%")
        overlap_suite(process, holdout)
    click_controls(process)
    bell_matrix(process)
    print(f"Elapsed {perf_counter() - start:.1f} s. No captures or coefficients saved; no listening test.")


if __name__ == "__main__":
    main()
