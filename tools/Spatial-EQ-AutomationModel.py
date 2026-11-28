"""Offline automation for up to eight static-shape Stereo EQ bands.

Initial controls are settled, with empty histories and control tick phase zero.
Events specify when targets become visible to the DSP. Host-specific resets
or reprepare behavior and VST3 queue semantics are not modeled.
"""

from collections.abc import Mapping
from math import cos, exp, expm1, isfinite, log, pi, sin
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


def _controls(initial, steps, size, sample_rate, moving_eq, *, overlap_state=None):
    step32 = 1.0 - exp(-_TICK / (0.018 * sample_rate))
    step1 = 1.0 - exp(-1.0 / (0.012 * sample_rate))
    target = initial.copy()
    depth, frequency, gain, q = (initial[k] for k in ("depth", "frequency", "gain_db", "q"))
    def boost_target():
        # The host target can start fading the boost before working gain crosses zero.
        if overlap_state is not None and not _STATIC["_wet_boost_gain_eligible"](target["gain_db"]):
            return 0.0
        return float(depth) * 0.01 * -expm1(-max(0.0, float(gain)) / 3.0)

    current = amount_target = boost_target()
    cut_current = cut_target = float(depth) * 0.01 if gain < 0.0 else 0.0
    enable = float(initial["active"] and initial["enabled"])
    enable_target = enable
    targets = np.empty((size + _TICK - 1) // _TICK, dtype=np.float64)
    amounts = np.empty(size, dtype=np.float64)
    enables = np.empty(size, dtype=np.float64)
    running = np.empty(size, dtype=bool)
    wet_running = np.empty(size, dtype=bool)
    cut_amounts = np.empty(size, dtype=np.float64)
    cut_running = np.empty(size, dtype=bool)
    designs = []
    design = (float(frequency), float(gain), float(q))
    event = 0
    for i in range(size):
        if event < len(steps) and steps[event][0] == i:
            target.update(steps[event][1])
            enable_target = float(target["active"] and target["enabled"])
            event += 1
        if i % _TICK == 0:
            updates = i == 0 or target["active"] or enable >= _SLEEP_THRESHOLD
            if updates:
                depth = _linear_step(depth, target["depth"], step32)
                # The callback enables flush-to-zero for float32 subnormals.
                if depth < _FLOAT_TINY:
                    depth = np.float32(0.0)
                if moving_eq:
                    frequency = _log_step(frequency, target["frequency"], step32)
                    q = _log_step(q, target["q"], step32)
                    gain = _linear_step(gain, target["gain_db"], step32)
                    design = (float(frequency), float(gain), float(q))
                amount_target = boost_target()
                cut_target = float(depth) * 0.01 if gain < 0.0 else 0.0
            else:
                # A fully inactive slot stores controls but suspends design updates.
                depth, frequency, gain, q = (target[k] for k in ("depth", "frequency", "gain_db", "q"))
            if overlap_state is not None:
                eligible = (target["active"] and target["enabled"]
                            and target["depth"] > _STATIC["_OVERLAP_MIN_DEPTH_PERCENT"]
                            and target["gain_db"] > _STATIC["_OVERLAP_MIN_GAIN_DB"])
                overlap_state.append((float(frequency), float(q), eligible, updates))

            designs.append(design)
            targets[i // _TICK] = amount_target
        enable += (enable_target - enable) * step1
        enables[i] = enable
        running[i] = run = target["active"] or enable >= _SLEEP_THRESHOLD
        if run:
            current += (amount_target - current) * step1
            if amount_target == 0.0 and current < _SLEEP_THRESHOLD:
                current = 0.0
            cut_current += (cut_target - cut_current) * step1
            if cut_target == 0.0 and cut_current < _SLEEP_THRESHOLD:
                cut_current = 0.0
        amounts[i] = current
        wet_running[i] = current != 0.0 or amount_target != 0.0
        cut_amounts[i] = cut_current
        cut_running[i] = cut_current != 0.0 or cut_target != 0.0
    return targets, amounts, enables, designs, running, wet_running, cut_amounts, cut_running


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


def _eq_target(frequency, gain_db, q, sample_rate, *, band_kind=None):
    if not isfinite(frequency) or not 20.0 <= frequency <= min(20000.0, 0.475 * sample_rate):
        raise ValueError("automated frequency must be in [20, min(20000, 0.475 * sample_rate)] Hz")
    if (not isfinite(gain_db) or abs(gain_db) > 30.0
            or (band_kind is None and np.float32(gain_db).item() <= 1e-5)):
        raise ValueError("automated gain_db is outside the supported range")
    min_q = 0.5 if band_kind is None or (band_kind == "bell" and gain_db > 0.0) else 0.1
    if not isfinite(q) or not min_q <= q <= 30.0:
        raise ValueError(f"automated Q must be in [{min_q}, 30]")
    return dict(frequency=np.float32(frequency), gain_db=np.float32(gain_db), q=np.float32(q))


def _prepare_events(steps, size, initial, sample_rate, *, band_kind=None):
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
            eq = _eq_target(targets["frequency"], targets["gain_db"], targets["q"], sample_rate,
                            band_kind=band_kind)
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
    controls = _controls(initial, steps, stereo.shape[1], sample_rate, moving_eq)
    direct = _band_direct(stereo, initial, sample_rate, moving_eq, controls)
    wet = _band_wet(direct, initial, sample_rate, moving_eq, controls)
    return direct, _STATIC["_wet_highpass"](wet, sample_rate)


def _direct_designs(sample_rate, designs, shape, *, gate_tiny_eq):
    matched = _STATIC["_matched_pole"]
    return [(1.0, 0.0, 0.0, 0.0, 0.0) if gate_tiny_eq and abs(gain) < 1e-9
            else matched(sample_rate, f, width, shape, gain) for f, gain, width in designs]


def _band_direct(stereo, initial, sample_rate, moving_eq, controls, *, coeffs=None):
    targets, _, enables, designs, running, _, cut_amounts, _ = controls
    direct = stereo.copy()
    frames = np.flatnonzero(running)
    if not frames.size:
        return direct
    source = stereo[:, frames]
    if moving_eq:
        if coeffs is None:
            coeffs = _direct_designs(sample_rate, designs, "bell", gate_tiny_eq=False)
        counts = np.bincount(frames // _TICK, minlength=len(targets)).tolist()
        eq = _tick_filter(source, coeffs, counts)
    else:
        eq = _STATIC["process_direct_eq"](source, sample_rate=sample_rate, frequency=initial["frequency"],
                                           gain_db=initial["gain_db"], q=initial["q"])
    blend = enables[frames]
    transition = blend != 1.0
    eq[:, transition] = source[:, transition] + (eq[:, transition] - source[:, transition]) * blend[transition]
    # The slewed cut amount can outlive the negative working gain.
    eq += _STATIC["_cut_mid_restore"](source, eq, cut_amounts[frames])
    direct[:, frames] = eq
    return direct


def _wet_epochs(running, wet_running):
    frames = np.flatnonzero(running)
    edges = np.flatnonzero(np.diff(wet_running[frames], prepend=False, append=False))
    for begin, end in zip(edges[::2], edges[1::2]):
        positions = frames[begin:end]
        ticks = positions // _TICK
        first, last = ticks[0], ticks[-1] + 1
        yield positions, ticks, slice(first, last), np.bincount(ticks - first, minlength=last - first).tolist()


def _band_wet(direct, initial, sample_rate, moving_eq, controls, *, overlap=None, shape="bell"):
    targets, amounts, enables, designs, running, wet_running, _, _ = controls
    wet = np.zeros_like(direct)
    # Missing frames pause the band; Depth dormancy starts a fresh wet epoch.
    if np.any(running & wet_running):
        matched = _STATIC["_matched_pole"]
        build_feed = _STATIC["_extraction_coeffs"]
        feed = ([build_feed(sample_rate, f, width, shape) for f, _, width in designs] if moving_eq else
                [build_feed(sample_rate, initial["frequency"], initial["q"], shape)] * len(targets))
        return_coeffs = [matched(sample_rate, 16000.0 - 9500.0 * k, _STATIC["_RETURN_Q"], "lowpass")
                         for k in targets.tolist()]
        for positions, wet_ticks, span, lengths in _wet_epochs(running, wet_running):
            contribution = _wet_epoch(direct[:, positions], feed[span], return_coeffs[span],
                                      lengths, amounts[positions], enables[positions],
                                      targets[wet_ticks], sample_rate)
            # Weight only the completed per-band wet sample, not its histories.
            if overlap is not None:
                contribution *= overlap[wet_ticks]
            wet[:, positions] = contribution
    return wet


def _apply_wet_cut(wet, coeffs, controls):
    _, _, enables, _, running, _, amounts, cut_running = controls
    for positions, _, span, counts in _wet_epochs(running, cut_running):
        source = wet[:, positions]
        # The wet-only copy has its own history and only runs while the cut amount is live.
        filtered = _tick_filter(source, coeffs[span], counts)
        wet[:, positions] = source + (filtered - source) * enables[positions] * amounts[positions]


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


def _ordered_overlap(sample_rate, starts, shapes, states):
    alpha = 1.0 - exp(-_TICK / (0.018 * sample_rate))
    build = _STATIC["_extraction_coeffs"]
    working = [build(sample_rate, band["frequency"], band["q"], shape)
               for band, shape in zip(starts, shapes)]
    previous_designs = [(float(band["frequency"]), float(band["q"])) for band in starts]
    weights = np.ones((len(starts), len(states[0])), dtype=np.float64)
    weight = [1.0] * len(starts)
    for tick, row in enumerate(zip(*states)):
        # Eligibility uses the new host snapshot for all slots; design
        # fields advance one slot at a time. Later slots still have the
        # previous tick's extraction coefficients during this receiver's scan.
        eligible = [j for j, (_, _, contributes, _) in enumerate(row) if contributes]
        for i, (frequency, q, _, updates) in enumerate(row):
            design = (frequency, q)
            if design != previous_designs[i]:
                working[i] = build(sample_rate, frequency, q, shapes[i])
                previous_designs[i] = design
            if updates:
                angle = (frequency * (2.0 * pi)) / sample_rate
                z1 = complex(cos(angle), -sin(angle))
                z2 = z1 * z1
                total = 0.0
                for j in eligible:
                    b0, b1, b2, a1, a2 = working[j]
                    total += abs((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2))
                target = 1.0 / max(1.0, total)
                weight[i] = target if tick == 0 else weight[i] + (target - weight[i]) * alpha
            weights[i, tick] = weight[i]
    return weights


def _render_multiband_segment(stereo, starts, shapes, steps, sample_rate):
    controls, states = [], []
    direct = stereo
    for slot, initial in starts.items():
        state = []
        control = _controls(initial, steps[slot], stereo.shape[1], sample_rate, True, overlap_state=state)
        shape = shapes[slot]
        coeffs = _direct_designs(sample_rate, control[3], shape, gate_tiny_eq=True)
        direct = _band_direct(direct, initial, sample_rate, True, control, coeffs=coeffs)
        controls.append(control)
        states.append(state)
    wet = np.zeros_like(stereo)
    if any(np.any(control[4] & control[5]) for control in controls):
        weights = _ordered_overlap(sample_rate, list(starts.values()), list(shapes.values()), states)
        for (slot, initial), control, weight in zip(starts.items(), controls, weights):
            wet += _band_wet(direct, initial, sample_rate, True, control, overlap=weight, shape=shapes[slot])
        for shape, control in zip(shapes.values(), controls):
            if np.any(control[4] & control[7]):
                designs = [(f, min(g, 0.0), q) for f, g, q in control[3]]
                coeffs = _direct_designs(sample_rate, designs, shape, gate_tiny_eq=True)
                _apply_wet_cut(wet, coeffs, control)
    return direct, _STATIC["_wet_highpass"](wet, sample_rate)


def process_bands_components(stereo, bands, *, steps=(), sample_rate=48000):
    """Return (direct EQ, wet) as channel-first (2, N) float64 arrays.

    Bands use (slot, frequency_hz, gain_db, depth[, q[, shape]]); Q defaults
    to 1. Supports Stereo Bell and Low/High Shelf boosts and cuts, including
    gain sign changes. Shapes are fixed. Listed slots start active and
    enabled with nonzero gain; unlisted slots cannot receive events. Use strictly
    increasing (sample, {slot: {field: target}}) events. Fields are frequency,
    q, gain_db, depth, active, enabled and reset (as in process_components).
    A reset in any slot resets the entire renderer, including the tick phase
    and overlap weights, with all targets in that event applied first.
    """
    stereo = _STATIC["_validate_input"](stereo, sample_rate)
    specs = sorted(_STATIC["_band_spec"](*band) for band in bands)
    starts, shapes = {}, {}
    for slot, frequency, gain, depth, q, shape in specs:
        if slot in starts:
            raise ValueError("band slots must be unique")
        shapes[slot] = shape
        initial = _eq_target(frequency, gain, q, sample_rate, band_kind=shape)
        starts[slot] = dict(initial, depth=_depth_percent(depth), active=True, enabled=True)

    size = stereo.shape[1]
    per_slot = {slot: [] for slot in starts}
    previous = -1
    for sample, changes in steps:
        sample = index(sample)
        if not previous < sample < size:
            raise ValueError("step samples must be strictly increasing in [0, N)")
        if not isinstance(changes, Mapping) or not changes or changes.keys() - starts.keys():
            raise ValueError("step must map listed slots to control updates")
        for slot, change in changes.items():
            if not isinstance(change, Mapping):
                raise ValueError("slot update must be a field-to-target mapping")
            per_slot[slot].append((sample, change))
        previous = sample

    events = {}
    for slot, initial in starts.items():
        prepared, _ = _prepare_events(per_slot[slot], size, initial, sample_rate, band_kind=shapes[slot])
        for sample, update in prepared:
            events.setdefault(sample, {})[slot] = update

    direct, wet = stereo.copy(), np.zeros_like(stereo)
    if not starts or not size:
        return direct, wet
    target = {slot: band.copy() for slot, band in starts.items()}
    local_steps = {slot: [] for slot in starts}
    start = 0
    for sample, changes in sorted(events.items()):
        reset = any(update.get("reset", False) for update in changes.values())
        if reset and sample > start:
            direct[:, start:sample], wet[:, start:sample] = _render_multiband_segment(
                stereo[:, start:sample], starts, shapes, local_steps, sample_rate)
        for slot, update in changes.items():
            target[slot].update((key, value) for key, value in update.items() if key != "reset")
            if not reset:
                local_steps[slot].append((sample - start, update))
        if reset:
            start = sample
            starts = {slot: band.copy() for slot, band in target.items()}
            local_steps = {slot: [] for slot in starts}
    direct[:, start:], wet[:, start:] = _render_multiband_segment(
        stereo[:, start:], starts, shapes, local_steps, sample_rate)
    return direct, wet
