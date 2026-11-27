"""Offline automation and lifecycle for one positive Stereo Bell.

Initial controls are settled, with empty histories and control tick phase zero.
Events specify when targets become visible to the DSP. Host-specific resets
or reprepare behavior and VST3 queue semantics are not modeled.
"""

from collections.abc import Mapping
from math import exp, expm1, isfinite, log
from operator import index
from pathlib import Path
from runpy import run_path

import numpy as np


_STATIC = run_path(str(Path(__file__).with_name("Spatial-EQ-DelayModel.py")))
_TICK = 32
_SLEEP_THRESHOLD = 1e-9
_FLOAT_TINY = np.finfo(np.float32).tiny


def _depth_percent(depth):
    if not isfinite(depth) or not 0.0 <= depth <= 1.0:
        raise ValueError("Depth must be a finite fraction in [0, 1]")
    fraction = np.float32(depth)
    return np.float32(100.0 * float(fraction)) if fraction >= _FLOAT_TINY else np.float32(0.0)


def _linear_step(current, target, alpha):
    return np.float32(float(target - current) * alpha + float(current))


def _log_step(current, target, alpha):
    # The log values and their difference round to float32 before the double step.
    old_log = np.float32(log(float(current)))
    new_log = np.float32(log(float(target)))
    return np.float32(exp(float(new_log - old_log) * alpha + float(old_log)))


def _controls(initial, steps, size, sample_rate, moving_eq):
    step32 = 1.0 - exp(-_TICK / (0.018 * sample_rate))
    step1 = 1.0 - exp(-1.0 / (0.012 * sample_rate))
    target = initial.copy()
    depth, frequency, gain, q = (initial[k] for k in ("depth", "frequency", "gain_db", "q"))
    boost_weight = -expm1(-float(gain) / 3.0)
    current = float(depth) * 0.01 * boost_weight
    amount_target = current
    enable = float(initial["active"] and initial["enabled"])
    enable_target = enable
    targets = np.empty((size + _TICK - 1) // _TICK, dtype=np.float64)
    amounts = np.empty(size, dtype=np.float64)
    enables = np.empty(size, dtype=np.float64)
    running = np.empty(size, dtype=bool)
    wet_running = np.empty(size, dtype=bool)
    designs = []
    design = (float(frequency), float(gain), float(q))
    event = 0
    for i in range(size):
        if event < len(steps) and steps[event][0] == i:
            target.update(steps[event][1])
            enable_target = float(target["active"] and target["enabled"])
            event += 1
        if i % _TICK == 0:
            if i == 0 or target["active"] or enable >= _SLEEP_THRESHOLD:
                depth = _linear_step(depth, target["depth"], step32)
                # The callback enables flush-to-zero for float32 subnormals.
                if depth < _FLOAT_TINY:
                    depth = np.float32(0.0)
                if moving_eq:
                    frequency = _log_step(frequency, target["frequency"], step32)
                    q = _log_step(q, target["q"], step32)
                    gain = _linear_step(gain, target["gain_db"], step32)
                    design = (float(frequency), float(gain), float(q))
                    boost_weight = -expm1(-float(gain) / 3.0)
                amount_target = float(depth) * 0.01 * boost_weight
            else:
                # A fully inactive slot stores controls but suspends design updates.
                depth, frequency, gain, q = (target[k] for k in ("depth", "frequency", "gain_db", "q"))
            designs.append(design)
            targets[i // _TICK] = amount_target
        enable += (enable_target - enable) * step1
        enables[i] = enable
        running[i] = run = target["active"] or enable >= _SLEEP_THRESHOLD
        if run:
            current += (amount_target - current) * step1
            if amount_target == 0.0 and current < _SLEEP_THRESHOLD:
                current = 0.0
        amounts[i] = current
        wet_running[i] = current != 0.0 or amount_target != 0.0
    return targets, amounts, enables, designs, running, wet_running


def _add_feedback(wet, drive_mid, sample_rate, targets):
    delays = tuple(int(_STATIC["_sample_delay"](sample_rate, ms)) for ms in _STATIC["_FEEDBACK_MS"])
    decay = 0.09 + 0.25 * targets
    # The gain at the read sample applies even to data already in flight.
    gains = [0.001 ** (delay / sample_rate / decay) for delay in delays]
    pad = max(delays)
    history = np.zeros((len(delays), pad + drive_mid.size), dtype=np.float64)
    step = min(delays)
    for start in range(0, drive_mid.size, step):
        stop = min(drive_mid.size, start + step)
        a, b, c, d = (
            gain[start:stop] * line[pad + start - delay : pad + stop - delay]
            for line, delay, gain in zip(history, delays, gains)
        )
        wet[0, start:stop] += 0.12 * (a + c)
        wet[1, start:stop] += 0.12 * (b + d)
        drive = 0.25 * drive_mid[start:stop]
        dest = history[:, pad + start : pad + stop]
        dest[0] = 0.5 * ((a + b) + (c + d)) + drive
        dest[1] = 0.5 * ((a - b) + (c - d)) - drive
        dest[2] = 0.5 * ((a + b) - (c + d)) + drive
        dest[3] = 0.5 * ((a - b) - (c - d)) - drive


def _tick_filter(signal, coeffs, frames):
    def samples(lane):
        s1 = s2 = 0.0
        values = iter(lane.tolist())
        for (b0, b1, b2, a1, a2), count in zip(coeffs, frames):
            for _ in range(count):
                x = next(values)
                y = b0 * x + s1
                s1, s2 = b1 * x - a1 * y + s2, b2 * x - a2 * y
                yield y

    # Coefficients change at ticks; transposed DF-II state is not rebased.
    return np.array([np.fromiter(samples(lane), dtype=np.float64, count=lane.size) for lane in signal])


def _eq_target(frequency, gain_db, q, sample_rate):
    if not isfinite(frequency) or not 20.0 <= frequency <= min(20000.0, 0.475 * sample_rate):
        raise ValueError("automated frequency must be in [20, min(20000, 0.475 * sample_rate)] Hz")
    if not isfinite(gain_db) or not 1e-5 < gain_db <= 30.0 or np.float32(gain_db).item() <= 1e-5:
        raise ValueError("automated gain_db must exceed 1e-5 dB after rounding and be at most 30 dB")
    if not isfinite(q) or not 0.5 <= q <= 30.0:
        raise ValueError("automated Q must be in [0.5, 30]")
    return dict(frequency=np.float32(frequency), gain_db=np.float32(gain_db), q=np.float32(q))


def _prepare_events(steps, size, initial, sample_rate):
    events = []
    targets = initial.copy()
    previous = -1
    moving_eq = False
    for sample, change in steps:
        sample = index(sample)
        if not previous < sample < size:
            raise ValueError("step samples must be strictly increasing in [0, N)")
        update = dict(change) if isinstance(change, Mapping) else {"depth": change}
        if not update or update.keys() - (initial.keys() | {"reset"}):
            raise ValueError("step must update depth, frequency, gain_db, q, active, enabled or reset")
        for key in update.keys() & {"active", "enabled", "reset"}:
            if update[key] not in (False, True):
                raise ValueError(f"{key} must be boolean")
            update[key] = bool(update[key])
        if "depth" in update:
            update["depth"] = _depth_percent(update["depth"])
        targets.update((key, value) for key, value in update.items() if key != "reset")
        if update.keys() & {"frequency", "gain_db", "q"}:
            moving_eq = True
            eq = _eq_target(targets["frequency"], targets["gain_db"], targets["q"], sample_rate)
            update.update((key, eq[key]) for key in update.keys() & eq.keys())
        events.append((sample, update))
        previous = sample
    return events, moving_eq


def process_depth_delta(stereo, initial_depth, *, steps=(), sample_rate=48000,
                        frequency=2400.0, gain_db=6.0, q=1.0, active=True, enabled=True):
    """Return the wet signal relative to a Depth-off render with the same direct controls.

    See process_components for the event format. For a Depth-step contrast,
    subtract the wet result with the same EQ events and no Depth step.
    """
    return process_components(stereo, initial_depth, steps=steps, sample_rate=sample_rate,
                              frequency=frequency, gain_db=gain_db, q=q, active=active, enabled=enabled)[1]


def process_components(stereo, initial_depth, *, steps=(), sample_rate=48000,
                       frequency=2400.0, gain_db=6.0, q=1.0, active=True, enabled=True):
    """Return (direct EQ, wet), each a channel-first (2, N) float64 array.

    Pass strictly increasing `(sample_index, update)` pairs. `update` may be
    a Depth fraction (the original format) or a mapping with any subset of
    `depth`, `frequency` (Hz), `gain_db`, `q`, `active`, `enabled`, `reset`.
    Omitted targets stay unchanged. EQ and Depth sample the latest target at
    each tick, so a ramp can be a series of updates. Active and enabled take
    effect at the specified sample. `reset=True` clears histories and restarts
    the tick clock at that sample with the current targets settled, including
    any targets in the same event. Indices include any silent lead.
    Automated EQ uses 20 Hz through min(20 kHz, 0.475 * sample_rate),
    Q in [0.5, 30], and gain above 1e-5 dB through +30 dB. Depth is in [0, 1].
    Add the two arrays for full output, or vary one event schedule and subtract
    full outputs for a contrast that includes direct EQ changes.
    """
    stereo = _STATIC["_validate_input"](stereo, sample_rate)
    _STATIC["_validate_band"](frequency, gain_db, q, "bell")
    if gain_db <= 1e-5:
        raise ValueError("gain_db must exceed the boost eligibility threshold (1e-5 dB)")
    size = stereo.shape[1]
    if active not in (False, True) or enabled not in (False, True):
        raise ValueError("active and enabled must be boolean")
    initial = dict(depth=_depth_percent(initial_depth), frequency=frequency, gain_db=gain_db,
                   q=q, active=bool(active), enabled=bool(enabled))
    events, moving_eq = _prepare_events(steps, size, initial, sample_rate)
    if moving_eq:
        initial.update(_eq_target(frequency, gain_db, q, sample_rate))
    direct = stereo.copy()
    wet = np.zeros_like(stereo)
    target = initial.copy()
    start = 0
    local_steps = []
    for sample, update in events:
        if update.get("reset", False):
            if sample > start:
                direct[:, start:sample], wet[:, start:sample] = _render_segment(
                    stereo[:, start:sample], initial, local_steps, sample_rate, moving_eq)
            target.update((k, v) for k, v in update.items() if k != "reset")
            start, local_steps, initial = sample, [], target.copy()
        else:
            local_steps.append((sample - start, update))
            target.update(update)
    if start < size:
        direct[:, start:], wet[:, start:] = _render_segment(
            stereo[:, start:], initial, local_steps, sample_rate, moving_eq)
    return direct, wet


def _render_segment(stereo, initial, steps, sample_rate, moving_eq):
    targets, amounts, enables, designs, running, wet_running = _controls(
        initial, steps, stereo.shape[1], sample_rate, moving_eq)
    frames = np.flatnonzero(running)
    direct = stereo.copy()
    wet = np.zeros_like(stereo)
    if not frames.size:
        return direct, wet

    matched = _STATIC["_matched_pole"]
    ticks = frames // _TICK
    counts = np.bincount(ticks, minlength=len(targets)).tolist()
    source = stereo[:, frames]
    if moving_eq:
        bell = [matched(sample_rate, f, width, "bell", gain) for f, gain, width in designs]
        eq = _tick_filter(source, bell, counts)
    else:
        eq = _STATIC["process_direct_eq"](source, sample_rate=sample_rate, frequency=initial["frequency"],
                                           gain_db=initial["gain_db"], q=initial["q"])
    blend = enables[frames]
    transition = blend != 1.0
    eq[:, transition] = source[:, transition] + (eq[:, transition] - source[:, transition]) * blend[transition]
    direct[:, frames] = eq

    # Missing frames pause the band; Depth dormancy instead starts a fresh
    # per-band wet epoch. The shared high-pass still runs on the full timeline.
    wet_gate = wet_running[frames]
    if wet_gate.any():
        feed = ([matched(sample_rate, f, width, "bandpass") for f, _, width in designs] if moving_eq else
                [matched(sample_rate, initial["frequency"], initial["q"], "bandpass")] * len(targets))
        return_coeffs = [matched(sample_rate, 16000.0 - 9500.0 * k, _STATIC["_RETURN_Q"], "lowpass")
                         for k in targets.tolist()]
        edges = np.flatnonzero(np.diff(wet_gate, prepend=False, append=False))
        for begin, end in zip(edges[::2], edges[1::2]):
            section = slice(begin, end)
            positions = frames[section]
            wet_ticks = ticks[section]
            first, last = wet_ticks[0], wet_ticks[-1] + 1
            lengths = np.bincount(wet_ticks - first, minlength=last - first).tolist()
            wet[:, positions] = _wet_epoch(eq[:, section], feed[first:last], return_coeffs[first:last],
                                           lengths, amounts[positions], enables[positions],
                                           targets[wet_ticks], sample_rate)
    return direct, _STATIC["_wet_highpass"](wet, sample_rate)


def _wet_epoch(eq, feed, return_coeffs, counts, amounts, enables, targets, sample_rate):
    extracted = _tick_filter(eq, feed, counts)
    original_side = _tick_filter((0.5 * (eq[0] - eq[1]))[None, :], feed, counts)[0]
    mid = 0.5 * (extracted[0] + extracted[1])
    side = amounts * (0.55 * original_side + 0.625 * _STATIC["_diffused_side"](mid, sample_rate)) * enables

    # Two fixed integer lag reads of the same history. Their output mix
    # moves with current amount, not with the tick-rate coefficient target.
    writes = extracted * enables
    bank_a = _STATIC["_tapped_reflections"](writes, sample_rate, 0.0)
    bank_b = _STATIC["_tapped_reflections"](writes, sample_rate, 1.0)
    wet = (1.0 - amounts) * bank_a + amounts * bank_b
    _add_feedback(wet, mid * enables, sample_rate, targets)
    wet = _tick_filter(wet, return_coeffs, counts)
    wet *= 0.65 * amounts * _STATIC["_return_duck"](extracted, sample_rate) * enables
    wet[0] += side
    wet[1] -= side
    return wet
