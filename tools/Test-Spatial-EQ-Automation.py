"""Compare one automated positive Stereo Bell against installed Spatial EQ 0.7.0.

Run through tools/Invoke-HalfCpu.ps1 with python -B tools/Test-Spatial-EQ-Automation.py.
Event times are Pedalboard call boundaries, not VST3 queue offsets.
"""

from pathlib import Path
from runpy import run_path


TOOLS = Path(__file__).parent
TAILS = run_path(str(TOOLS / "Probe-Spatial-EQ-Tails.py"))
MODEL = run_path(str(TOOLS / "Spatial-EQ-AutomationModel.py"))["process_components"]
AUTOMATION, SWEEP, HOST, np = (TAILS[k] for k in ("AUTOMATION", "SWEEP", "HOST", "np"))
CAPTURE = TAILS["capture"]


def compare(label, reference, model):
    if not np.isfinite(model).all():
        raise AssertionError(f"{label}: non-finite model samples")
    errors = []
    for ref, residual in zip(HOST["mid_side"](reference), HOST["mid_side"](model - reference)):
        rms = np.sqrt(np.mean(residual ** 2))
        peak = np.max(np.abs(residual))
        ref_rms = np.sqrt(np.mean(ref ** 2))
        relative = 20 * np.log10(rms / ref_rms) if rms and ref_rms else -np.inf
        if (rms > 1e-8 and (ref_rms == 0 or relative > -85)) or peak > 2e-6:
            raise AssertionError(f"{label}: {relative:.2f} dBr, RMS {rms:.3e}, peak {peak:.3e} FS")
        errors.append(f"{relative:.2f} dBr (peak {peak:.2e} FS)")
    print(f"    {label:24} Mid / Side: {' / '.join(errors)}")


def check(label, x, rate, depth, events, raw, first):
    model_steps = []
    wet_events = []
    direct_events = []
    for sample, control, value, reset in events:
        update = {"reset": True} if reset else {}
        if control is not None:
            update[control] = value
        model_steps.append((sample, update))
        field = "gain" if control == "gain_db" else control
        parameter = raw(field, value) if field is not None else None
        wet_events.append((sample, field, parameter, reset))
        direct_events.append((sample, None, None, reset) if field == "depth" else wet_events[-1])

    full_ref = CAPTURE(x, rate, depth=depth, events=wet_events)
    direct_ref = CAPTURE(x, rate, depth=0.0, events=direct_events)
    direct, wet = MODEL(x, depth, sample_rate=rate, steps=model_steps)
    keep = slice(first, None)
    print(f"  {label}:")
    compare("direct EQ", direct_ref[:, keep], direct[:, keep])
    compare("Depth delta", (full_ref - direct_ref)[:, keep], wet[:, keep])
    compare("full output", full_ref[:, keep], (direct + wet)[:, keep])
    return full_ref, direct + wet


def measure(rate):
    print(f"\n{rate} Hz")
    plugin = HOST["pb"].load_plugin(str(HOST["MODULE"]))
    normalized = {}

    def raw(field, value):
        if field == "gain":
            return 0.5 + value / 60
        if field not in ("frequency", "q"):
            return float(value)
        key = field, value
        if key not in normalized:
            param = plugin.parameters[SWEEP["key"](field)]
            normalized[key] = SWEEP["raw_for_display"](param, field, value)
        return normalized[key]

    x = AUTOMATION["stimulus"](rate)
    split = 3 * rate // 4 + 13
    event = (split, "depth", 0.71, False)
    ref, model = check("Depth 33% to 71%", x, rate, 0.33, (event,), raw, split)
    base_ref = CAPTURE(x, rate, depth=0.33)
    base_model = sum(MODEL(x, 0.33, sample_rate=rate))
    ref_delta, model_delta = ref - base_ref, model - base_model
    compare("Depth-step contrast", ref_delta[:, split:], model_delta[:, split:])
    expected_tick = split + (-split) % 32
    for delta in (ref_delta, model_delta):
        changed = np.flatnonzero(np.max(np.abs(delta), axis=0) > 1e-8)
        if not changed.size or changed[0] != expected_tick:
            raise AssertionError(f"Expected Depth step at sample {expected_tick}, got {changed[:1]}")

    for field, target in (("frequency", 1450), ("q", 1.8), ("gain_db", 10.5)):
        check(f"{field} step", x, rate, 0.57, ((split, field, target, False),), raw, split)
    ramp_targets = (target for j in range(1, 6) for target in (
        ("frequency", 2400 - 210 * j), ("q", 1 + j / 10),
        ("gain_db", 6 + j / 2), ("depth", 0.33 + j * 0.08)))
    ramp = tuple((split + 97 * i, field, value, False) for i, (field, value) in enumerate(ramp_targets))
    check("interleaved 20-point ramp", x, rate, 0.33, ramp, raw, split)

    lead = rate // 10
    end = lead + round(0.016 * rate)
    off = end + round(0.008 * rate) + 7
    on = off + round(0.060 * rate)
    burst = np.zeros((2, on + rate // 10), dtype=np.float32)
    t = np.arange(end - lead) / rate
    burst[:, lead:end] = 0.12 * np.hanning(t.size) * np.cos(2 * np.pi * 2400 * t)
    for field in ("active", "enabled", "depth"):
        events = ((off, field, 0.0, False), (on, field, 1.0, False))
        check(f"60 ms {field} gap", burst, rate, 1.0, events, raw, off)
    ref, model = check("reset while ringing", burst, rate, 1.0, ((off, None, None, True),), raw, off)
    if ref[:, off:].any() or model[:, off:].any():
        raise AssertionError("Reset on a silent suffix did not return exact silence")

    resume = off + 3 * rate
    dormant = np.zeros((2, resume + rate // 10), dtype=np.float32)
    dormant[:, :off] = burst[:, :off]
    for delay in (-rate // 80, rate // 25):
        start = resume + delay
        n = rate // 125
        dormant[:, start:start + n] = burst[:, lead:lead + n]
    events = ((off, "depth", 0.0, False), (resume, "depth", 1.0, False))
    check("3 s Depth-zero re-entry", dormant, rate, 1.0, events, raw, resume)


def main():
    AUTOMATION["identify"]()
    for rate in (48_000, 96_000):
        measure(rate)
    print("Automation and tail lifecycle comparisons passed; no audio saved.")


if __name__ == "__main__":
    main()
