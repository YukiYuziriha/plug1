"""Offline static multiband waveform comparison against Spatial EQ 0.7.0.

From the research worktree root, with the Measure-Spatial-EQ.py dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'tools/Invoke-HalfCpu.ps1' -Executable python -CommandArguments @('-B','tools/Test-Spatial-EQ-Multiband.py')"
Use --reference-only to measure without testing a model, or --with-cuts to add both slot orders.
"""

import argparse
from pathlib import Path
from runpy import run_path


TOOLS = Path(__file__).resolve().parent
SHELVES = run_path(str(TOOLS / "Test-Spatial-EQ-Shelves.py"))
SWEEP = SHELVES["SWEEP"]
np, pb, MODULE, BLOCK = (SHELVES[k] for k in ("np", "pb", "MODULE", "BLOCK"))
SHAPES = {"bell": ("Bell", 0.0), **{name: spec[:2] for name, spec in SHELVES["SHELVES"].items()}}
# Error gates apply independently to M/S and each waveform contrast. Floors
# accommodate float32 output/cancellation and display-to-raw control rounding.
RMS_FLOOR, RMS_FRACTION = 1e-8, 1e-3
PEAK_FLOOR, PEAK_FRACTION = 2e-7, 2e-3

CASES = (
    ("overlap", 48_000, ((3, 2400, 6, 1.0), (7, 2400, 3, 0.35), (1, 3000, 4.5, 0.67, 1.3))),
    ("mixed", 48_000, ((6, 3200, 5, 0.80, 0.9, "high_shelf"), (1, 800, 5, 0.60, 1.2),
                       (4, 800, 4, 0.46, 0.8, "low_shelf"), (8, 3200, 3, 0.0))),
    ("separated", 48_000, ((1, 160, 4, 0.50, 1.2), (4, 1600, 5, 0.70, 1.5), (8, 12800, 3, 0.90, 2.0))),
    ("holdout-44k", 44_100, ((1, 180, 3, 0.38, 0.75, "low_shelf"), (2, 400, 3.5, 0.59, 0.8),
                             (3, 720, 3, 0.22, 1.6), (4, 1250, 5, 0.84, 1.1),
                             (5, 1800, 4, 0.48, 0.9), (6, 2500, 2.5, 0.71, 1.3),
                             (7, 4200, 4, 0.94, 0.8, "high_shelf"), (8, 8500, 3, 0.64, 1.6))),
    ("holdout-96k", 96_000, ((8, 260, 2, 0.39, 0.9, "low_shelf"), (7, 650, 4, 0.81, 1.2),
                             (6, 1100, 2.5, 0.60, 2.0), (5, 3100, 3.5, 0.22, 0.7),
                             (4, 4800, 2, 0.73, 0.8, "high_shelf"), (3, 6250, 3, 0.94, 1.7),
                             (2, 8750, 2.5, 0.64, 1.4), (1, 12200, 3.5, 0.48, 1.0, "high_shelf"))),
)
CUT_CASES = tuple((f"cut-{'after' if slot == 3 else 'before'}", 48_000,
                   ((slot, 2400, 6, 0.80), (7 - slot, 2400, -6, 0.50))) for slot in (3, 4))


def settings(bands):
    plugin = pb.load_plugin(str(MODULE))
    params = plugin.parameters
    values = {f"band_{i}_active": 0.0 for i in range(1, 9)}
    values.update(processing=0.0, output=0.5, bypass=0.0, spatial_bypass=0.0, mono_audition=0.0,
                  spatial_bass_protection=SWEEP["BASE"]["spatial_bass_protection"])
    for slot, hz, gain, depth, *options in bands:
        q = options[0] if options else 1.0
        shape = options[1] if len(options) > 1 else "bell"
        prefix = f"band_{slot}_"
        public, raw = SHAPES[shape]
        if SWEEP["set_raw"](params[prefix + "shape"], raw) != public:
            raise RuntimeError(f"Slot {slot}: could not select {public}")
        controls = dict(active=1.0, enabled=1.0, shape=raw, stereo=0.0, dynamic=0.0,
                        sidechain=0.0, width=0.5, spread=0.0, depth=depth)
        for field, target in (("frequency", hz), ("gain", gain), ("q", q)):
            param = params[prefix + field]
            raw = SWEEP["raw_for_display"](param, field, target)
            if SWEEP["display_number"](SWEEP["set_raw"](param, raw), field) != target:
                raise RuntimeError(f"Slot {slot}: could not set {field} to {target}")
            controls[field] = param.raw_value
        values.update({prefix + field: raw for field, raw in controls.items()})
    return values


def capture(x, rate, lead, values, *, off=False):
    plugin = pb.load_plugin(str(MODULE))
    for name, value in values.items():
        is_depth = name.endswith("_depth")
        raw = 0.0 if off and is_depth else value
        text = SWEEP["set_raw"](plugin.parameters[name], raw)
        if is_depth and SWEEP["display_number"](text, "depth") != round(raw * 100):
            raise RuntimeError(f"{name}: expected {raw:.0%}; got {text!r}")
    out = SHELVES["validated"](plugin.process(x.copy(), rate, buffer_size=BLOCK), x, np.float32, "reference")
    arrivals = np.flatnonzero(np.max(np.abs(out), axis=0) > 1e-7)
    if not arrivals.size or arrivals[0] != lead:
        raise RuntimeError(f"Expected zero-latency onset at {lead}, got {arrivals[:1]}")
    return out[:, lead:].astype(np.float64)


def score(label, ref, pred):
    failures = []
    reference = np.array(SHELVES["HOST"]["mid_side"](ref))
    error = None if pred is None else np.array(SHELVES["HOST"]["mid_side"](pred - ref))
    for ch, (lane, signal) in enumerate(zip("MS", reference)):
        rms, peak = np.sqrt(np.mean(signal ** 2)), np.max(np.abs(signal))
        row = f"    {label:11} {lane} {rms:10.3e} {peak:10.3e}"
        if error is not None:
            err_rms, err_peak = np.sqrt(np.mean(error[ch] ** 2)), np.max(np.abs(error[ch]))
            dbr = f"{SHELVES['HOST']['db'](err_rms / rms):.1f}" if rms else "n/a"
            passed = err_rms <= RMS_FLOOR + RMS_FRACTION * rms and err_peak <= PEAK_FLOOR + PEAK_FRACTION * peak
            row += f" {err_rms:10.3e} {err_peak:10.3e} {dbr:>7} {'ok' if passed else 'FAIL'}"
            if not passed:
                failures.append(f"{label} {lane}")
        print(row)
    return failures


def run_case(name, rate, bands, process):
    print(f"\n{name}, {rate} Hz: {bands}")
    values = settings(bands)
    off_bands = tuple((slot, hz, gain, 0.0, *extra) for slot, hz, gain, _, *extra in bands)
    lead = rate // 10
    failures = []
    for stimulus, route, dry in SHELVES["fixtures"](rate):
        if route != "asym" and (name != "overlap" or stimulus != "impulse"):
            continue
        x = np.pad(dry, ((0, 0), (lead, 0)))
        ref_off = capture(x, rate, lead, values, off=True)
        ref_on = capture(x, rate, lead, values)
        eq = full = delta = None
        if process is not None:
            model_off, full = (SHELVES["validated"](process(x, spec, sample_rate=rate), x, np.float64, name)[:, lead:]
                               for spec in (off_bands, bands))
            eq, delta = model_off - dry, full - model_off
        print(f"  {stimulus} / {route}")
        for label, ref, pred in (("direct EQ", ref_off - dry, eq), ("full output", ref_on, full),
                                 ("Depth-added", ref_on - ref_off, delta)):
            failures.extend(f"{name} {stimulus}/{route}: {item}" for item in score(label, ref, pred))
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-only", action="store_true", help="report reference levels without scoring a model")
    parser.add_argument("--with-cuts", action="store_true", help="add one boost and one cut in both slot orders")
    parser.add_argument("--case", choices=tuple(name for name, _, _ in CASES + CUT_CASES), help="run only this fixture")
    args = parser.parse_args()
    cases = CASES + CUT_CASES if args.with_cuts or (args.case or "").startswith("cut-") else CASES
    cases = [spec for spec in cases if args.case is None or spec[0] == args.case]
    SHELVES["identify"]()
    process = None
    if not args.reference_only:
        process = run_path(str(TOOLS / "Spatial-EQ-DelayModel.py"))["process_bands"]
        for name, rate, bands in cases:
            try:
                process(np.zeros((2, 1), dtype=np.float32), bands, sample_rate=rate)
            except (ValueError, NotImplementedError) as exc:
                parser.error(f"process_bands rejected {name}: {exc}; use --reference-only to probe independently")
    print(f"Stereo float32, host buffer {BLOCK}, zero latency, no alignment shift. "
          "Width 100%, Spread 0%, bass protection 120 Hz.")
    print("Direct EQ = all Depths zero minus dry; full = configured output; Depth-added = full minus its own all-zero-Depth anchor.")
    print("Per-case M/S RMS and peak in FS. Subtraction and M/S fold in float64.")
    if process is None:
        print("REFERENCE ONLY (no model verification): ref RMS, ref peak.")
    else:
        print(f"ref RMS, ref peak, err RMS, err peak, err dBr, result. Gates: RMS <= {RMS_FLOOR:g} + "
              f"{RMS_FRACTION:g}*ref RMS; peak <= {PEAK_FLOOR:g} + {PEAK_FRACTION:g}*ref peak.")
    failures = [item for case in cases for item in run_case(*case, process)]
    if failures:
        raise SystemExit(f"{len(failures)} comparison lanes failed:\n  " + "\n  ".join(failures))
    print("Reference capture complete; no model tested." if process is None else "All comparison lanes passed.")


if __name__ == "__main__":
    main()
