"""Offline comparison of positive shelves against installed Spatial EQ 0.7.0.

From the research worktree root, with the Measure-Spatial-EQ.py dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'tools/Invoke-HalfCpu.ps1' -Executable python -CommandArguments @('-B','tools/Test-Spatial-EQ-Shelves.py','--reference-only')"
Remove --reference-only to compare the model. Add --details for per-fixture scores.
"""

import argparse
import hashlib
from pathlib import Path
import re
from runpy import run_path


TOOLS = Path(__file__).resolve().parent
SWEEP = run_path(str(TOOLS / "Sweep-Spatial-EQ-Controls.py"))
HOST = SWEEP["HOST"]
np, pb, MODULE, BLOCK = (HOST[k] for k in ("np", "pb", "MODULE", "BLOCK"))
key, set_raw, raw_for_display, display_number = (SWEEP[k] for k in
                                                ("key", "set_raw", "raw_for_display", "display_number"))
DEPTHS = (0.0, 0.5, 1.0)
RATES = (44_100, 48_000, 96_000)
SHELVES = {
    "low_shelf": ("Low Shelf", 1 / 6, ((240, 6, 1.0), (800, 3, 0.7))),
    "high_shelf": ("High Shelf", 2 / 6, ((6400, 6, 1.0), (3200, 12, 2.0))),
}


def identify():
    manifest = (HOST["PLUGIN"] / "Contents" / "Resources" / "moduleinfo.json").read_text(encoding="utf-8")
    match = re.search(r'"Version"\s*:\s*"([^"]+)"', manifest)
    version = match[1] if match else "unknown"
    if version != "0.7.0":
        raise RuntimeError(f"Expected Spatial EQ 0.7.0, found {version}: {MODULE}")
    with MODULE.open("rb") as binary:
        digest = hashlib.file_digest(binary, "sha256").hexdigest()
    print(f"{MODULE}\nVersion {version} | SHA-256 {digest} | Pedalboard {pb.__version__} | NumPy {np.__version__}")


def configure(params, shape_name, shape_raw, targets):
    shape = params[key("shape")]
    if shape_name not in shape.valid_values or set_raw(shape, shape_raw) != shape_name:
        raise RuntimeError(f"Expected {shape_name!r} at raw {shape_raw}, got {shape.string_value!r}")
    controls = {**SWEEP["BASE"], "shape": shape.raw_value}
    text = [f"{shape_name} (raw {shape.raw_value:.9g})"]
    for field, target in zip(("frequency", "gain", "q"), targets):
        param = params[key(field)]
        actual = set_raw(param, raw_for_display(param, field, target))
        if display_number(actual, field) != target:
            raise RuntimeError(f"Expected {field} {target}, got {actual!r}")
        controls[field] = param.raw_value
        text.append(f"{actual} (raw {param.raw_value:.9g})")
    print("  " + "; ".join(text))
    return controls


def fixtures(rate):
    amplitude = 10 ** (-24 / 20)
    click = np.zeros(rate // 5, dtype=np.float32)
    click[0] = amplitude
    yield "impulse", "dual", np.stack((click, click))
    right = np.zeros_like(click)
    right[round(0.00073 * rate)] = -amplitude / 2
    yield "impulse", "asym", np.stack((click, right))

    t = np.arange(3 * rate // 4) / rate
    start, end = 25.0, min(19_000.0, 0.44 * rate)
    k = np.log(end / start) / 0.75
    # Nonzero first sample makes the leading-silence/onset check unambiguous.
    fade = np.clip((0.75 - t) / 0.02, 0, 1)
    left = amplitude * fade * np.cos(2 * np.pi * start * np.expm1(k * t) / k)
    right = 0.55 * amplitude * fade * np.sin(2 * np.pi * end * np.expm1(-k * t) / -k + 0.4)
    for route, r in (("dual", left), ("asym", right)):
        yield "sweep", route, np.pad(np.array((left, r), dtype=np.float32), ((0, 0), (0, rate // 5)))


def validated(audio, x, dtype, label):
    if (not isinstance(audio, np.ndarray) or audio.shape != x.shape
            or audio.dtype != dtype or not np.isfinite(audio).all()):
        raise RuntimeError(f"{label}: expected finite {dtype.__name__} of shape {x.shape}, got "
                           f"{getattr(audio, 'shape', None)} {getattr(audio, 'dtype', None)}")
    return audio


def capture(x, rate, lead, depth, controls, *, active=True, spatial_bypass=False):
    params_settings = {f"band_{i}_active": 0.0 for i in range(1, 9)}
    params_settings.update({key(k): v for k, v in dict(active=float(active), enabled=1.0,
                           stereo=0.0, dynamic=0.0, sidechain=0.0, depth=depth, **controls).items()})
    params_settings.update(processing=0.0, output=0.5, bypass=0.0,
                           spatial_bypass=float(spatial_bypass), mono_audition=0.0)
    plugin = pb.load_plugin(str(MODULE))
    for name, raw in params_settings.items():
        text = set_raw(plugin.parameters[name], raw)
        if name == key("depth") and display_number(text, "depth") != depth * 100:
            raise RuntimeError(f"Expected Depth {depth:.0%}, got {text!r}")
    out = validated(plugin.process(x.copy(), rate, buffer_size=BLOCK), x, np.float32, "reference")
    arrivals = np.flatnonzero(np.max(np.abs(out), axis=0) > 1e-7)
    if not arrivals.size or arrivals[0] != lead:
        raise RuntimeError(f"Expected direct arrival at {lead}, got {arrivals[:1]}")
    return out.astype(np.float64)


def direct_gain(audio, dry, rate, frequency):
    # Evaluate the impulse transfer at specified frequencies without FFT-bin rounding.
    f = np.array((frequency / 4, frequency, min(4 * frequency, 0.45 * rate)))
    kernel = np.exp((-2j * np.pi / rate) * f[:, None] * np.arange(audio.shape[-1]))
    gain = 20 * np.log10(np.abs(kernel @ audio[0] / dry[0, 0]))
    return ", ".join(f"{hz:g} Hz {db:+.2f} dB" for hz, db in zip(f, gain))


def score(reference, prediction):
    ref = np.array(HOST["mid_side"](reference))
    power = np.mean(ref ** 2, axis=1)
    peak = np.max(np.abs(ref), axis=1)
    if prediction is None:
        return power, peak, None, None
    err = np.array(HOST["mid_side"](prediction - reference))
    return power, peak, np.mean(err ** 2, axis=1), np.max(np.abs(err), axis=1)


def report(label, scores):
    ref, ref_peak, err, err_peak = zip(*scores)
    has_model = err[0] is not None
    ref = np.stack(ref)
    for ch, lane in enumerate(("M", "S")):
        power = ref[:, ch]
        text = f"  {label:55} {lane} {len(scores):2} {np.sqrt(power.mean()):11.3e} {max(p[ch] for p in ref_peak):11.3e}"
        if has_model:
            residual = np.stack(err)[:, ch]
            nonzero = power > 0
            db = f"{HOST['db'](np.sqrt(np.mean(residual[nonzero] / power[nonzero]))):.2f}" if nonzero.any() else "n/a"
            text += f" {np.sqrt(residual.mean()):11.3e} {max(p[ch] for p in err_peak):11.3e} {db:>9}"
        print(text)


def compare(args, model):
    inactive_anchors = {}
    for shape, (shape_name, shape_raw, settings) in SHELVES.items():
        print(f"\n{shape}:")
        rows, details = {}, []
        controls_peaks = np.zeros(3)
        for targets in settings:
            frequency, gain_db, q = targets
            plugin = pb.load_plugin(str(MODULE))
            controls = configure(plugin.parameters, shape_name, shape_raw, targets)
            for rate in args.rates:
                lead = rate // 10
                kwargs = dict(sample_rate=rate, frequency=frequency, gain_db=gain_db, q=q, shape=shape)
                for stimulus, route, dry in fixtures(rate):
                    label = f"{rate} {frequency}/{gain_db}/{q:g} {stimulus} {route}"
                    x = np.pad(dry, ((0, 0), (lead, 0)))
                    off = capture(x, rate, lead, 0.0, controls)
                    # With all eight bands inactive, the shelf settings cannot affect dry.
                    anchor = (rate, stimulus, route)
                    if anchor not in inactive_anchors:
                        inactive_anchors[anchor] = capture(x, rate, lead, 0.0, controls, active=False)
                    inactive = inactive_anchors[anchor]
                    controls_peaks[0] = max(controls_peaks[0], np.max(np.abs(inactive - x)))
                    if stimulus == "impulse" and route == "dual":
                        bypass = capture(x, rate, lead, 1.0, controls, spatial_bypass=True)
                        controls_peaks[1] = max(controls_peaks[1], np.max(np.abs(bypass - off)))
                        print(f"    {rate} Hz direct transfer: {direct_gain(off[:, lead:], dry, rate, frequency)}")
                    direct = None
                    if model is not None:
                        direct = validated(model["process_direct_eq"](x, **kwargs), x, np.float64, label)
                    # Score the EQ contribution, not the mostly-dry output. The raw
                    # sample residual is unchanged when the inactive anchor equals dry.
                    views = [("direct", off[:, lead:] - inactive[:, lead:],
                              None if direct is None else direct[:, lead:] - dry)]
                    for depth in DEPTHS:
                        on = capture(x, rate, lead, depth, controls)
                        delta = None
                        if model is not None:
                            delta = validated(model["process_depth_delta"](x, depth, **kwargs),
                                              x, np.float64, label)
                            if depth == 0 and np.any(delta):
                                raise RuntimeError(f"{label}: model Depth 0 delta must be exactly zero")
                        if depth == 0:
                            controls_peaks[2] = max(controls_peaks[2], np.max(np.abs(on - off)))
                        views.append((f"delta {depth:.0%}", on[:, lead:] - off[:, lead:],
                                      None if delta is None else delta[:, lead:]))
                    for view, ref, prediction in views:
                        stats = score(ref, prediction)
                        rows.setdefault(f"{stimulus} {route} / {view}", []).append(stats)
                        if args.details:
                            details.append((f"{label} / {view}", stats))
        print("  Worst L/R control peak residuals (FS): inactive minus dry %.3e; "
              "spatial bypass at 100%% minus Depth 0 (dual impulse) %.3e; fresh Depth 0 repeat %.3e" % tuple(controls_peaks))
        header = f"  {'view':55} {'MS':>1} {'N':>2} {'ref RMS':>11} {'ref peak':>11}"
        if model is not None:
            header += f" {'err RMS':>11} {'err peak':>11} {'err dBr':>9}"
        print(header)
        for label, scores in rows.items():
            report(label, scores)
        if details:
            print("\n" + header)
            for label, stats in details:
                report(label, [stats])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-only", action="store_true", help="do not load or score the model")
    parser.add_argument("--rates", type=int, nargs="+", choices=RATES, default=RATES)
    parser.add_argument("--details", action="store_true", help="include individual fixture rows")
    args = parser.parse_args()
    identify()
    model = None
    if not args.reference_only:
        model = run_path(str(TOOLS / "Spatial-EQ-DelayModel.py"))
        if not all(callable(model.get(name)) for name in ("process_direct_eq", "process_depth_delta")):
            parser.error("shelf comparison needs process_direct_eq and process_depth_delta; use --reference-only for capture")
    print(f"Stereo float32, {BLOCK}-sample host blocks, 100 ms silent lead, no fitting or time alignment.")
    print("Static band 3, Stereo, Width 100%, Spread 0%, bass protection 120 Hz, zero latency, trim 0 dB.")
    print("Direct = Depth 0 minus inactive (model: direct EQ minus dry). Depth delta = Depth on minus local Depth 0.")
    print("Impulse: 200 ms window; sweep: 750 ms plus 200 ms tail; L peak -24 dBFS.")
    print("Subtract and fold M=(L+R)/2, S=(L-R)/2 in float64. RMS/peak in FS, equal fixture weight.")
    if model is not None:
        print("dBr = 10 log10(mean per-fixture error/reference power), omitting only exact zero refs; "
              "absolute residuals always include them. Scores are diagnostic, not tolerance gates.")
    compare(args, model)
    print("All audio stayed in memory; no editor or listening test.")


if __name__ == "__main__":
    main()
