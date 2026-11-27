"""Compare automated multiband boosts with installed Spatial EQ 0.7.0 (offline).

Run in the research worktree with Measure-Spatial-EQ.py's dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'tools/Invoke-HalfCpu.ps1' -Executable python -CommandArguments @('-B','tools/Test-Spatial-EQ-MultibandAutomation.py')"
Add --reference-only to run without a model. Parameter writes are between
Pedalboard calls, not in-block VST3 queue events. No captures are saved.
"""

import argparse
from pathlib import Path
from runpy import run_path


TOOLS = Path(__file__).resolve().parent
AUTO = run_path(str(TOOLS / "Probe-Spatial-EQ-Automation.py"))
SWEEP, HOST = AUTO["SWEEP"], AUTO["HOST"]
np, pb, MODULE = (HOST[k] for k in ("np", "pb", "MODULE"))
SLOTS = (2, 7)
BANDS = ((2, 900, 8, 1.0, 2.4), (7, 4200, 5, 0.64, 0.7))
BLOCK = 512
SHAPES = {"bell": (0.0, "Bell"), "low_shelf": (1 / 6, "Low Shelf"), "high_shelf": (2 / 6, "High Shelf")}


def fixture(rate):
    # Fixed continuous, asymmetric tones near both paths of the crossing bells.
    lead = rate // 10
    t = np.arange(rate - lead) / rate
    left = sum(0.025 * np.cos(2 * np.pi * f * t + p) for f, p in ((900, 0), (1740, 0.3), (2850, 1.2), (4200, 0.7)))
    right = sum(0.019 * np.cos(2 * np.pi * f * t + p) for f, p in ((900, 1.3), (1510, 0.1), (2410, 2), (4200, 1.8)))
    x = np.pad(np.array((left, right), dtype=np.float32), ((0, 0), (lead, 0)))
    move, drop, restore = (int(rate * ms / 1000) + 11 for ms in (350, 550, 720))
    steps = ((move, {2: {"frequency": 3900, "q": 0.85}, 7: {"frequency": 950, "q": 2.0}}),
             (drop, {7: {"depth": 0.0}}), (restore, {7: {"depth": 0.64}}))
    return x, steps


def reverse(bands, steps):
    swap = dict(zip(SLOTS, reversed(SLOTS)))
    return (tuple((swap[slot], *args) for slot, *args in bands),
            tuple((n, {swap[slot]: values for slot, values in changes.items()}) for n, changes in steps))


def direct_schedule(bands, steps):
    direct = tuple((slot, hz, gain, 0.0, *rest) for slot, hz, gain, _, *rest in bands)
    # Keep the same host-call boundary even where removing Depth leaves no writes.
    events = tuple((n, {slot: {field: value for field, value in changes.items() if field != "depth"}
                        for slot, changes in update.items()}) for n, update in steps)
    return direct, events


def capture(x, rate, bands, steps, *, block=BLOCK, irregular=False):
    plugin = pb.load_plugin(str(MODULE))
    params = plugin.parameters
    raw_controls = {}

    def public_field(field):
        return "gain" if field == "gain_db" else field

    def write(slot, field, value):
        field = public_field(field)
        name = f"band_{slot}_{field}"
        if field in ("active", "enabled"):
            SWEEP["set_raw"](params[name], float(value))
            return
        if field == "depth":
            raw, target = value, round(value * 100)
        else:
            raw, target = raw_controls[name, value], value
        text = SWEEP["set_raw"](params[name], raw)
        if SWEEP["display_number"](text, field) != target:
            raise RuntimeError(f"{name}: expected {target}, got {text!r}")

    settings = {f"band_{i}_active": 0.0 for i in range(1, 9)}
    settings.update(processing=0.0, output=0.5, bypass=0.0, spatial_bypass=0.0,
                    mono_audition=0.0, spatial_bass_protection=SWEEP["BASE"]["spatial_bass_protection"])
    for slot, hz, gain, depth, q, *shape in bands:
        shape_raw, display = SHAPES[shape[0] if shape else "bell"]
        if SWEEP["set_raw"](params[f"band_{slot}_shape"], shape_raw) != display:
            raise RuntimeError(f"Slot {slot}: expected shape {display}")
        settings.update({f"band_{slot}_{k}": v for k, v in dict(active=1.0, enabled=1.0, stereo=0.0,
                        dynamic=0.0, width=0.5, spread=0.0, sidechain=0.0).items()})
        targets = {("frequency", hz), ("gain", gain), ("q", q)}
        targets.update((public_field(field), value) for _, update in steps
                       for field, value in update.get(slot, {}).items()
                       if field in ("frequency", "q", "gain_db"))
        for field, value in sorted(targets):
            name = f"band_{slot}_{field}"
            raw_controls[name, value] = SWEEP["raw_for_display"](params[name], field, value)
        for field, value in (("frequency", hz), ("gain", gain), ("q", q), ("depth", depth)):
            write(slot, field, value)
    for name, raw in settings.items():
        SWEEP["set_raw"](params[name], raw)

    boundaries = dict(steps)
    if irregular:
        n = 0
        for offset in np.resize((3001, 4069, 2221, 5147), x.shape[1] // 2200):
            n += int(offset)
            if n >= x.shape[1]:
                break
            boundaries.setdefault(n, {})
    boundaries[x.shape[1]] = {}
    out = []
    start, reset = 0, True
    for stop, update in sorted(boundaries.items()):
        part = x[:, start:stop].copy()
        y = plugin.process(part, rate, buffer_size=block, reset=reset)
        if y.shape != part.shape or y.dtype != np.float32 or not np.isfinite(y).all():
            raise RuntimeError(f"Invalid reference block at {start}: {y.shape}, {y.dtype}")
        out.append(y.astype(np.float64))
        reset = any(changes.get("reset", False) for changes in update.values())
        for slot, changes in update.items():
            for field, value in changes.items():
                if field != "reset":
                    write(slot, field, value)
        start = stop
    return np.concatenate(out, axis=1)


def metrics(x):
    mid_side = np.array(HOST["mid_side"](x))
    return np.sqrt(np.mean(mid_side ** 2, axis=1)), np.max(np.abs(mid_side), axis=1)


def report(label, reference, prediction=None, *, relative=0.003):
    rms, peak = metrics(reference)
    fmt = lambda pair: "/".join(f"{v:.3e}" for v in pair)
    text = f"  {label:34} ref RMS {fmt(rms)} peak {fmt(peak)}"
    if prediction is None:
        print(text)
        return True
    error_rms, error_peak = metrics(prediction - reference)
    passed = np.all(error_rms <= 2e-8 + relative * rms) and np.all(error_peak <= 3e-7 + 3 * relative * peak)
    print(f"{text} | err RMS {fmt(error_rms)} peak {fmt(error_peak)} {'ok' if passed else 'FAIL'}")
    return passed


def model_from_tools():
    for name in ("Spatial-EQ-MultibandAutomationModel.py", "Spatial-EQ-AutomationModel.py"):
        path = TOOLS / name
        if path.is_file():
            process = run_path(str(path)).get("process_bands_components")
            if callable(process):
                print(f"Model: {name}")
                return process
    return None


def measure(rate, process):
    x, steps = fixture(rate)
    move, drop, restore = (n for n, _ in steps)
    specs = ((BANDS, steps), reverse(BANDS, steps))
    windows = {name: slice(n, n + round(rate * 0.07)) for name, n in
               (("move", move), ("drop", drop), ("restore", restore))}
    print(f"\n{rate} Hz | moves/drop/restore at {move}/{drop}/{restore} (sample phases "
          f"{move % 32}/{drop % 32}/{restore % 32}); Mid/Side, full-scale units")
    ref, model = [], []
    for bands, events in specs:
        full = capture(x, rate, bands, events)
        direct = capture(x, rate, *direct_schedule(bands, events))
        ref.append((direct, full - direct, full))
        if process is not None:
            d, w = process(x, bands, steps=events, sample_rate=rate)
            if any(not isinstance(a, np.ndarray) or a.shape != x.shape or a.dtype != np.float64
                   or not np.isfinite(a).all() for a in (d, w)):
                raise RuntimeError("Expected two finite stereo float64 model components")
            model.append((d, w, d + w))

    alt_full = capture(x, rate, *specs[0], block=257, irregular=True)
    alt_direct = capture(x, rate, *direct_schedule(*specs[0]), block=257, irregular=True)
    alt_wet = alt_full - alt_direct
    report("257 + extra splits, direct delta", alt_direct - ref[0][0])
    report("257 + extra splits, wet delta", alt_wet - ref[0][1])
    direct_order, wet_order, _ = (b - a for a, b in zip(*ref))
    report("settled wet order before move", wet_order[:, move - rate // 20:move])
    w = windows["move"]
    noise = max(np.max(np.abs((alt_wet - ref[0][1])[:, w])), 1e-8)
    wet_peak = np.max(np.abs(wet_order[:, w]))
    if wet_peak <= 100 * noise:
        raise RuntimeError(f"Wet slot-order contrast {wet_peak:.3e} not separated from floor {noise:.3e}")
    passed = []
    for name, w in windows.items():
        for order, rendered in enumerate(ref):
            for ch, (label, signal) in enumerate(zip(("direct", "wet", "full"), rendered)):
                pred = model[order][ch][:, w] if model else None
                passed.append(report(f"{name} order {order + 1} {label}", signal[:, w], pred))
        report(f"{name} direct slot contrast", direct_order[:, w])
        pred_order = (model[1][1] - model[0][1])[:, w] if model else None
        passed.append(report(f"{name} wet slot contrast", wet_order[:, w], pred_order, relative=0.10))
    # Negative control for the order-contrast check: a slot-invariant wet output
    # predicts zero contrast, even if it reproduces the fixed-order audio.
    if report("negative control (want FAIL)", wet_order[:, windows["move"]],
              np.zeros_like(wet_order[:, windows["move"]]), relative=0.10):
        raise RuntimeError("Order-contrast gate accepted the slot-invariant negative control")
    return all(passed)


HOLDOUT_BELLS = ((1, 400, 4, 0.58, 1.0), (3, 950, 5, 0.80, 1.4),
                 (6, 3100, 4, 0.73, 1.1), (8, 6800, 4, 0.46, 1.6))
EXTRA_BELLS = ((2, 570, 3, 0.33, 1.3), (4, 1600, 3, 0.60, 0.9),
               (5, 2400, 2.5, 0.39, 1.7), (7, 4400, 3.5, 0.90, 1.0))


def holdout(rate, process, *, shelves=False):
    def at(ms):
        return rate * ms // 1000 + 19

    bands = HOLDOUT_BELLS + (EXTRA_BELLS if rate == 96_000 else ())
    if shelves:
        shapes = {3: "low_shelf", 6: "high_shelf"}
        bands = tuple((*band, shapes.get(band[0], "bell")) for band in bands)
    t = np.arange(rate * 850 // 1000) / rate
    left = sum(0.014 * np.cos(2 * np.pi * f * t + i / 3)
               for i, f in enumerate((400, 930, 1610, 2400, 4200, 6800)))
    right = sum(0.011 * np.sin(2 * np.pi * f * t + i / 2)
                for i, f in enumerate((460, 950, 1700, 2850, 3700, 6850)))
    lead = rate // 10
    x = np.pad(np.array((left, right), dtype=np.float32), ((0, 0), (lead, 0)))
    ramp = tuple((at(280 + 13 * i), {3: {"frequency": 950 + 150 * i, "q": round(1.4 - 0.1 * i, 2)},
                                    6: {"frequency": 3100 - 150 * i, "q": round(1.1 + 0.1 * i, 2),
                                                                            "depth": (73 - 5 * i) / 100}})
                 for i in range(1, 8))
    reset = at(680)
    steps = (*ramp, (at(395), {1: {"active": False}, 8: {"enabled": False}}),
             (at(540), {1: {"active": True}}), (at(590), {8: {"enabled": True}}),
             (reset, {6: {"reset": True}}), (at(760), {3: {"depth": 0.30}}))
    kind = "mixed shelf/Bell" if shelves else "Bell"
    print(f"\n{len(bands)}-band {kind} holdout at {rate} Hz | stepped ramp, active/enable, reset at n={reset}")
    full = capture(x, rate, bands, steps)
    direct = capture(x, rate, *direct_schedule(bands, steps))
    wet = full - direct

    # Verify the reset against a new instance with the exact same suffix and
    # post-reset partition. Do not change buffer size around reset: Pedalboard
    # may reprepare when its effective block size changes on a short call.
    targets = {slot: dict(frequency=hz, gain_db=gain, depth=depth, q=q,
                          shape=shape[0] if shape else "bell")
               for slot, hz, gain, depth, q, *shape in bands}
    for n, changes in steps:
        if n >= reset:
            break
        for slot, update in changes.items():
            targets[slot].update(update)
    fresh_bands = tuple((slot, *(fields[k] for k in ("frequency", "gain_db", "depth", "q", "shape")))
                        for slot, fields in targets.items())
    fresh_steps = tuple((n - reset, changes) for n, changes in steps if n > reset)
    fresh_full = capture(x[:, reset:], rate, fresh_bands, fresh_steps)
    fresh_direct = capture(x[:, reset:], rate, *direct_schedule(fresh_bands, fresh_steps))
    for label, difference in (("direct", direct[:, reset:] - fresh_direct),
                              ("wet", wet[:, reset:] - (fresh_full - fresh_direct))):
        report(f"reset minus fresh {label}", difference)
        if np.any(difference):
            raise RuntimeError(f"Reset {label} did not match fresh with identical host partition")
    no_reset = tuple((n, changes if n != reset else {}) for n, changes in steps)
    continued = capture(x, rate, bands, no_reset)
    report("reset minus matched no-op split", (full - continued)[:, reset:at(760)])
    if np.max(np.abs((full - continued)[:, reset:at(760)])) < 1e-4:
        raise RuntimeError("Reset did not produce a measurable contrast against the matched no-op")

    predicted = (None, None, None)
    if process is not None:
        d, w = process(x, bands, steps=steps, sample_rate=rate)
        if any(not isinstance(a, np.ndarray) or a.shape != x.shape or a.dtype != np.float64
               or not np.isfinite(a).all() for a in (d, w)):
            raise RuntimeError("Expected two finite stereo float64 model components")
        predicted = (d, w, d + w)
    scores = []
    for label, window in (("ramp", slice(ramp[0][0], at(395))), ("flags off", slice(at(395), at(540))),
                          ("flags restored", slice(at(540), reset)), ("reset", slice(reset, at(760))),
                          ("post-reset step", slice(at(760), None))):
        for name, ref, pred in zip(("direct", "wet", "full"), (direct, wet, full), predicted):
            scores.append(report(f"{label} {name}", ref[:, window], None if pred is None else pred[:, window]))
    return all(scores)


def moving_cut(rate, process):
    x, _ = fixture(rate)
    move, deepen, depth = (rate * ms // 1000 + 17 for ms in (280, 455, 660))
    bands = ((2, 2400, 6.5, 0.76, 1.2), (7, 4200, -5.5, 0.64, 1.6))
    steps = ((move, {7: {"frequency": 1450, "q": 0.75}}),
             (deepen, {7: {"gain_db": -8.0}}),
             (depth, {7: {"depth": 0.27}}))
    windows = (("freq/Q move", slice(move, deepen)), ("cut gain", slice(deepen, depth)),
               ("cut Depth", slice(depth, None)))
    print(f"\n{rate} Hz | moving negative Bell with stationary boost, both slot orders")
    print("  Total Depth contrast subtracts all-Depth-zero with identical EQ moves. "
          "It includes the cut's direct Depth response; no wet-only score.")
    scores = []
    for order, (spec, events) in enumerate(((bands, steps), reverse(bands, steps)), start=1):
        off_spec, off_events = direct_schedule(spec, events)
        full = capture(x, rate, spec, events)
        baseline = capture(x, rate, off_spec, off_events)
        pred_full = pred_baseline = None
        if process is not None:
            # Keep no-op boundaries on the reference, but omit writes removed by
            # the all-zero-Depth model schedule.
            off_events = tuple((n, changes) for n, updates in off_events
                               if (changes := {slot: change for slot, change in updates.items() if change}))
            pred_full = model_full(process, x, rate, spec, events)
            pred_baseline = model_full(process, x, rate, off_spec, off_events)
        for label, window in windows:
            scores.append(report(f"{label} order {order} full", full[:, window],
                                 None if pred_full is None else pred_full[:, window]))
            scores.append(report(f"{label} order {order} total Depth", (full - baseline)[:, window],
                                 None if pred_full is None else (pred_full - pred_baseline)[:, window]))
    return all(scores)


def model_full(process, x, rate, bands, steps):
    direct, wet = process(x, bands, steps=steps, sample_rate=rate)
    if any(not isinstance(a, np.ndarray) or a.shape != x.shape or a.dtype != np.float64
           or not np.isfinite(a).all() for a in (direct, wet)):
        raise RuntimeError("Expected two finite stereo float64 model components")
    return direct + wet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-only", action="store_true")
    parser.add_argument("--rate", type=int, choices=(48_000, 96_000), help="run one rate only")
    args = parser.parse_args()
    AUTO["identify"]()
    process = None if args.reference_only else model_from_tools()
    if process is None and not args.reference_only:
        parser.error("process_bands_components not available yet; use --reference-only")
    print("Two positive Stereo Bells, asymmetric continuous tones, zero latency. Writes are at host-call boundaries.")
    print("Boost-only direct anchor: same frequency/Q steps with all Depths zero; wet = full minus anchor.")
    print("Slot contrast = swapped minus original; it includes order effects of the moving EQ, not just overlap weights.")
    print("Two-bell slot-order windows are 70 ms. Model gates: relative RMS 0.3% / peak 0.9%; wet order contrast 10% / 30%,"
          " plus RMS/peak floors 2e-8/3e-7 FS.")
    rates = (args.rate,) if args.rate else (48_000, 96_000)
    results = [measure(rate, process) for rate in rates]
    results.extend(holdout(rate, process, shelves=kind) for kind in (False, True) for rate in rates)
    results.extend(moving_cut(rate, process) for rate in rates)
    if not all(results):
        raise SystemExit("Model comparison failed (see FAIL rows other than the expected negative control).")
    print("Reference only; no model scored." if process is None else "Model comparison passed.")


if __name__ == "__main__":
    main()
