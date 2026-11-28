"""Offline static Stereo EQ and spatial Depth model.

Supports up to eight Bell or shelf boosts and cuts in total. Controls
are settled from sample zero, with empty histories on every call. No
automation, dynamics, other channel modes or host processing is modeled.
"""

from math import cos, cosh, exp, expm1, floor, isfinite, pi, sin, sqrt

import numpy as np


# Nominal reflection times in microseconds. Setup assigns tap 7 as
# L=73.3 ms / R=71.9 ms in bank A, then contracts each lane by 18% in bank B.
_REFLECTION_US = (
    (7300, 13700, 21100, 29900, 42100, 57700, 73300),
    (9100, 15100, 23300, 32300, 44900, 59300, 71900),
)
_REFLECTION_GAIN = (0.28, -0.18, 0.16, 0.12, -0.10, 0.09, 0.07)
_DIFFUSION_MS = ((1.7, 5.3, 11.9), (2.9, 7.1, 13.7))
_FEEDBACK_MS = (31.1, 37.3, 43.7, 53.3)
_RETURN_Q = 0.70710678
_SHELF_FEEDS = {"low_shelf": "lowpass", "high_shelf": "highpass"}
_SHELF_FEED_Q = 0.70710678
_WET_MIN_GAIN_DB = 1e-5
_OVERLAP_MIN_DEPTH_PERCENT = np.float32(0.001)
_OVERLAP_MIN_GAIN_DB = np.float32(1e-5)


def _matched_pole(sample_rate, hz, q, response, gain_db=0.0):
    """Fit numerator magnitudes at DC, center and Nyquist to analog targets."""
    w = 2.0 * pi * min(0.475 * sample_rate, max(10.0, hz)) / sample_rate
    shelf = response in _SHELF_FEEDS
    bell_cut = response == "bell" and gain_db < 0.0
    inverse = bell_cut
    if bell_cut:
        if gain_db > -1e-9:
            return 1.0, 0.0, 0.0, 0.0, 0.0
        q = min(30.0, max(0.1, q))
        gain_db = min(-gain_db, 60.0)
    if shelf:
        if abs(gain_db) < 1e-9:
            return 1.0, 0.0, 0.0, 0.0, 0.0
        q = min(30.0, max(0.1, q))
        low = response == "low_shelf"
        inverse = (gain_db > 0.0) != low
        # Fit a Low Shelf boost or High Shelf cut first, then invert as needed.
        # Only the pole frequency moves; the fit still uses w.
        signed_gain = min(abs(gain_db), 36.0 if gain_db > 0.0 else 60.0) * (1 if low else -1)
        boost = 10.0 ** (signed_gain / 40.0)
        pole_w = w / sqrt(boost) if low else w * sqrt(boost)
        damping = 0.5 / q
    else:
        boost = 10.0 ** (gain_db / 40.0)
        pole_w = w
        damping = 0.5 / (q * boost)
    radius = exp(-damping * pole_w)
    if damping > 1.0:
        a1 = -2.0 * radius * cosh(pole_w * sqrt(damping * damping - 1.0))
    else:
        a1 = -2.0 * radius * cos(pole_w * sqrt(1.0 - damping * damping))
    a2 = radius * radius

    # The analog Nyquist target is evaluated at the ratio Nyquist / center.
    x = pi / w
    detune = (1.0 - x * x) ** 2
    width_sq = (x / q) ** 2
    if response == "bell":
        dc, center = 1.0, boost * boost
        nyquist = sqrt((detune + width_sq * boost**2) / (detune + width_sq / boost**2))
    elif shelf:
        # Analog shelf targets at the unshifted DC, center and Nyquist.
        base = (boost - x * x) ** 2 + boost * width_sq
        shifted = (1.0 - boost * x * x) ** 2 + boost * width_sq
        center = boost
        if response == "low_shelf":
            dc, nyquist = boost * boost, boost * sqrt(base / shifted)
        else:
            dc, nyquist = 1.0, boost * sqrt(shifted / base)
    else:
        analog_den = sqrt(detune + width_sq)
        if response == "bandpass":
            dc, center, nyquist = 0.0, 1.0, x / (q * analog_den)
        elif response == "lowpass":
            dc, center, nyquist = 1.0, q, 1.0 / analog_den
        else:  # highpass
            dc, center, nyquist = 0.0, q, x * x / analog_den

    at_dc = dc * abs(1.0 + a1 + a2)
    at_nyquist = nyquist * abs(1.0 - a1 + a2)
    outer_sum = (at_dc + at_nyquist) * 0.5
    inner = (at_dc - at_nyquist) * 0.5

    # Rotate numerator and denominator by one sample at center. Their real
    # parts are fixed by the endpoint constraints; solve for b0 - b2.
    cw, sw = cos(w), sin(w)
    den_real = (1.0 + a2) * cw + a1
    den_imag = (1.0 - a2) * sw
    num_real = outer_sum * cw + inner
    imag_sq = center**2 * (den_real**2 + den_imag**2) - num_real**2
    outer_diff = sqrt(max(0.0, imag_sq)) / sw
    b0, b2 = (outer_sum + outer_diff) * 0.5, (outer_sum - outer_diff) * 0.5
    if inverse:
        scale = b0 if abs(b0) > 1e-30 else 1e-30
        return 1.0 / scale, a1 / scale, a2 / scale, inner / scale, b2 / scale
    return b0, inner, b2, a1, a2


def _filtered(signal, coeffs):
    b0, b1, b2, a1, a2 = coeffs

    def samples():
        state1 = state2 = 0.0
        for x in signal.tolist():
            y = b0 * x + state1
            state1 = b1 * x - a1 * y + state2
            state2 = b2 * x - a2 * y
            yield y

    return np.fromiter(samples(), dtype=np.float64, count=signal.size)


def _sample_delay(sample_rate, ms):
    return (sample_rate * ms) * 0.001


def _allpass(signal, delay):
    # Use zero-padded write history instead of a circular buffer. Blocks no
    # longer than the delay never read from their own writes.
    g = 0.55
    history = np.zeros(delay + signal.size, dtype=np.float64)
    output = -g * signal
    for start in range(0, signal.size, delay):
        stop = min(signal.size, start + delay)
        block = slice(start, stop)
        output[block] += history[block]
        history[start + delay : stop + delay] = signal[block] + g * output[block]
    return output


def _diffused_side(mid, sample_rate):
    chains = []
    for times in _DIFFUSION_MS:
        lane = mid
        for ms in times:
            delay = max(1, floor(_sample_delay(sample_rate, ms) + 0.5))
            lane = _allpass(lane, delay)
        chains.append(lane)
    return chains[0] - chains[1]


def _tapped_reflections(extracted, sample_rate, amount):
    taps = np.zeros_like(extracted)
    for channel, times in enumerate(_REFLECTION_US):
        for us, gain in zip(times, _REFLECTION_GAIN):
            contracted = us * 82 // 100
            for tap_us, mix in ((us, 1.0 - amount), (contracted, amount)):
                # The tapped histories write and advance before reading.
                lag = int(_sample_delay(sample_rate, tap_us / 1000)) - 1
                if lag < extracted.shape[1]:
                    taps[channel, lag:] += gain * mix * extracted[channel, : extracted.shape[1] - lag]
    left, right = taps
    return np.array((0.85 * left + 0.15 * right, 0.15 * left + 0.85 * right))


def _add_feedback(wet, mid, sample_rate, amount):
    delays = tuple(int(_sample_delay(sample_rate, ms)) for ms in _FEEDBACK_MS)
    decay_time = 0.09 + 0.25 * amount
    gains = tuple(0.001 ** (delay / sample_rate / decay_time) for delay in delays)
    pad = max(delays)
    writes = np.zeros((len(delays), pad + mid.size), dtype=np.float64)
    step = min(delays)
    for start in range(0, mid.size, step):
        stop = min(mid.size, start + step)
        read0, read1, read2, read3 = (
            gain * line[pad + start - delay : pad + stop - delay]
            for line, gain, delay in zip(writes, gains, delays)
        )
        wet[0, start:stop] += 0.12 * (read0 + read2)
        wet[1, start:stop] += 0.12 * (read1 + read3)

        drive = 0.25 * mid[start:stop]
        sum01, sum23 = read0 + read1, read2 + read3
        diff01, diff23 = read0 - read1, read2 - read3
        dest = writes[:, pad + start : pad + stop]
        dest[0] = 0.5 * (sum01 + sum23) + drive
        dest[1] = 0.5 * (diff01 + diff23) - drive
        dest[2] = 0.5 * (sum01 - sum23) + drive
        dest[3] = 0.5 * (diff01 - diff23) - drive


def _return_duck(extracted, sample_rate):
    fast_step = -expm1(-1.0 / (0.002 * sample_rate))
    slow_step = -expm1(-1.0 / (0.040 * sample_rate))
    energy = 0.5 * (extracted[0] ** 2 + extracted[1] ** 2)

    def samples():
        fast = slow = 0.0
        for power in energy.tolist():
            fast += fast_step * (power - fast)
            slow += slow_step * (power - slow)
            yield min(1.0, max(0.2, sqrt((slow + 1e-15) / (fast + 1e-15))))

    return np.fromiter(samples(), dtype=np.float64, count=energy.size)


def _validate_input(stereo, sample_rate):
    stereo = np.asarray(stereo, dtype=np.float64)
    if stereo.ndim != 2 or stereo.shape[0] != 2:
        raise ValueError("stereo must have shape (2, N)")
    if not isfinite(sample_rate) or sample_rate < 1000:
        raise ValueError("sample_rate must be finite and at least 1000 Hz")
    return stereo


def _validate_band(frequency, gain_db, q, shape):
    if shape not in ("bell", "low_shelf", "high_shelf"):
        raise ValueError("shape must be 'bell', 'low_shelf', or 'high_shelf'")
    if not isfinite(frequency) or frequency <= 0.0:
        raise ValueError("frequency must be finite and positive")
    if not isfinite(gain_db) or gain_db == 0.0 or not isfinite(q) or q <= 0.0:
        raise ValueError("gain_db must be finite and nonzero; q must be finite and positive")

    if shape == "bell" and gain_db > 0.0 and q < 0.5:
        raise ValueError("the static positive Bell model requires q >= 0.5")


def _validate_depth(depth):
    if not isfinite(depth) or not 0.0 <= depth <= 1.0:
        raise ValueError("depth must be a finite fraction in [0, 1]")


def _filter_stereo(stereo, coeffs):
    return np.array([_filtered(lane, coeffs) for lane in stereo])


def _cut_mid_restore(stereo, eq, depth):
    left, right = stereo - eq
    return (0.5 * (left + right)) * depth


def process_direct_eq(stereo, *, sample_rate=48000, frequency=2400.0, gain_db=6.0,
                      q=1.0, shape="bell"):
    """Return the settled Depth-off EQ output as a (2, N) float64 array.

    Shape is 'bell', 'low_shelf', or 'high_shelf'; all accept either gain sign.
    A positive Bell requires Q >= 0.5. The shelves and negative Bell limit
    design Q to [0.1, 30]. Shelf gain is clamped to [-60, 36] dB; Bell cuts
    are limited to -60 dB.
    Design frequency is limited to 10 Hz through 0.475 * sample_rate.
    Controls are physical values, not host parameters.
    """
    stereo = _validate_input(stereo, sample_rate)
    _validate_band(frequency, gain_db, q, shape)
    return _filter_stereo(stereo, _matched_pole(sample_rate, frequency, q, shape, gain_db))


def _wet_boost_gain_eligible(gain_db):
    # The wet path promotes the float32 target gain before comparing it
    # with a double threshold. Overlap eligibility is a separate test.
    return np.float32(gain_db).item() > _WET_MIN_GAIN_DB


def process_depth_delta(stereo, depth, *, sample_rate=48000, frequency=2400.0,
                        gain_db=6.0, q=1.0, shape="bell"):
    """Return Depth on minus off as a channel-first (2, N) float64 array.

    Depth is a fraction in [0, 1]. Band controls are as in process_direct_eq.
    The delta is spatial wet for a boost, or a Mid-only direct correction
    for an isolated cut. Add it to process_direct_eq for the full output.
    """
    stereo = _validate_input(stereo, sample_rate)
    _validate_band(frequency, gain_db, q, shape)
    _validate_depth(depth)
    if depth == 0.0 or stereo.shape[1] == 0 or (gain_db > 0.0 and not _wet_boost_gain_eligible(gain_db)):
        return np.zeros_like(stereo)

    eq = _filter_stereo(stereo, _matched_pole(sample_rate, frequency, q, shape, gain_db))
    if gain_db < 0.0:
        mid = _cut_mid_restore(stereo, eq, depth)
        return np.array((mid, mid))
    return _wet_highpass(_boost_wet(eq, depth, sample_rate, frequency, gain_db, q, shape), sample_rate)


def _extraction_coeffs(sample_rate, frequency, q, shape):
    feed_shape = _SHELF_FEEDS.get(shape, "bandpass")
    feed_q = q if shape == "bell" else _SHELF_FEED_Q
    return _matched_pole(sample_rate, frequency, feed_q, feed_shape)


def _boost_wet(eq, depth, sample_rate, frequency, gain_db, q, shape, *, overlap=1.0):
    amount = depth * -expm1(-gain_db / 3.0)
    feed = _extraction_coeffs(sample_rate, frequency, q, shape)
    eq_left, eq_right = eq
    extracted = np.array((_filtered(eq_left, feed), _filtered(eq_right, feed)))
    mid = 0.5 * (extracted[0] + extracted[1])
    existing_side = _filtered(0.5 * (eq_left - eq_right), feed)
    side = (amount * overlap) * (0.55 * existing_side + 0.625 * _diffused_side(mid, sample_rate))

    wet = _tapped_reflections(extracted, sample_rate, amount)
    _add_feedback(wet, mid, sample_rate, amount)
    lowpass = _matched_pole(sample_rate, 16000.0 - 9500.0 * amount, _RETURN_Q, "lowpass")
    return_gain = (0.65 * amount * overlap) * _return_duck(extracted, sample_rate)
    wet[0] = _filtered(wet[0], lowpass) * return_gain + side
    wet[1] = _filtered(wet[1], lowpass) * return_gain - side
    return wet


def _wet_highpass(wet, sample_rate):
    return _filter_stereo(wet, _matched_pole(sample_rate, 120.0, _RETURN_Q, "highpass"))


def _band_spec(slot, frequency, gain_db, depth, q=1.0, shape="bell"):
    if not isinstance(slot, (int, np.integer)) or not 1 <= slot <= 8:
        raise ValueError("band slot must be an integer in [1, 8]")
    _validate_band(frequency, gain_db, q, shape)
    _validate_depth(depth)
    return slot, frequency, gain_db, depth, q, shape


def _overlap_weights(sample_rate, boosts):
    # The reference compares the float32 target fields. The Depth control
    # is stored in percentage points, whereas the model API uses a fraction.
    contributors = [
        _extraction_coeffs(sample_rate, frequency, q, shape)
        for frequency, gain_db, depth, q, shape in boosts
        if (np.float32(100.0 * float(np.float32(depth))) > _OVERLAP_MIN_DEPTH_PERCENT
            and np.float32(gain_db) > _OVERLAP_MIN_GAIN_DB)
    ]
    for frequency, _, _, _, _ in boosts:
        angle = 2.0 * pi * frequency / sample_rate
        z1 = complex(cos(angle), -sin(angle))
        z2 = z1 * z1
        # Include the receiving band when eligible, and sum magnitudes rather
        # than powers or complex responses. The reset path seeds this weight
        # directly, without starting an overlap slew from unity.
        overlap = sum(abs((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2))
                      for b0, b1, b2, a1, a2 in contributors)
        yield 1.0 / max(1.0, overlap)


def process_bands(stereo, bands, *, sample_rate=48000):
    """Return the full (direct + wet) settled output as a (2, N) float64 array.

    Each band is (slot, frequency, gain_db, depth[, q[, shape]]), with Q=1
    and shape='bell' by default. Supplied slots are active and enabled;
    omit inactive slots. Slots 1–8 are unique and run in ascending order,
    independent of the input sequence. Depth zero leaves conventional EQ
    enabled but contributes neither spatial audio nor overlap weight.
    """
    stereo = _validate_input(stereo, sample_rate)
    specs = sorted(_band_spec(*band) for band in bands)
    slots = {band[0] for band in specs}
    if len(slots) != len(specs):
        raise ValueError("band slots must be unique")
    direct = stereo.copy()
    boosts, wet_cuts = [], []
    for _, frequency, gain_db, depth, q, shape in specs:
        coeffs = _matched_pole(sample_rate, frequency, q, shape, gain_db)
        eq = _filter_stereo(direct, coeffs)
        if gain_db < 0.0 and depth > 0.0:
            eq += _cut_mid_restore(direct, eq, depth)
            wet_cuts.append((coeffs, depth))
        elif depth > 0.0 and _wet_boost_gain_eligible(gain_db):
            boosts.append((frequency, gain_db, depth, q, shape))
        direct = eq

    if not boosts or stereo.shape[1] == 0:
        return direct

    # Every extractor sees the same final post-EQ pair. Wet cuts run in
    # slot order after accumulation and before the shared bass high-pass.
    weights = _overlap_weights(sample_rate, boosts) if len(boosts) > 1 else (1.0,)
    returns = (_boost_wet(direct, depth, sample_rate, frequency, gain_db, q, shape, overlap=weight)
               for (frequency, gain_db, depth, q, shape), weight in zip(boosts, weights))
    wet = next(returns)
    for contribution in returns:
        wet += contribution
    for coeffs, depth in wet_cuts:
        wet += depth * (_filter_stereo(wet, coeffs) - wet)
    return direct + _wet_highpass(wet, sample_rate)
