"""Offline spatial Depth delta for one static, positive-gain Stereo Bell.

Controls are settled from sample zero, with empty histories on every call.
The direct bell is identical at Depth on and off, so only the wet sum is
returned. No automation, dynamics, cuts, other band modes, or host processing
is modeled. The traced anchor is 48 kHz / 2.4 kHz / +6 dB / Q=1.
"""

from math import cos, exp, expm1, floor, isfinite, pi, sin, sqrt

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


def _matched_pole(sample_rate, hz, q, response, gain_db=0.0):
    """Fit numerator magnitudes at DC, center and Nyquist to analog targets."""
    w = 2.0 * pi * min(0.475 * sample_rate, max(10.0, hz)) / sample_rate
    boost = 10.0 ** (gain_db / 40.0)
    damping = 0.5 / (q * boost)
    radius = exp(-damping * w)
    a1 = -2.0 * radius * cos(w * sqrt(1.0 - damping * damping))
    a2 = radius * radius

    # The analog Nyquist target is evaluated at the ratio Nyquist / center.
    x = pi / w
    detune = (1.0 - x * x) ** 2
    width_sq = (x / q) ** 2
    if response == "bell":
        dc, center = 1.0, boost * boost
        nyquist = sqrt((detune + width_sq * boost**2) / (detune + width_sq / boost**2))
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
    return (outer_sum + outer_diff) * 0.5, inner, (outer_sum - outer_diff) * 0.5, a1, a2


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


def process_depth_delta(stereo, depth, *, sample_rate=48000, frequency=2400.0, gain_db=6.0, q=1.0):
    """Return Depth on minus off as a channel-first (2, N) float64 array.

    Depth is a fraction in [0, 1]; gain must be positive. Frequency is
    internally limited to 10 Hz through 0.475 * sample_rate for filter design.
    This uses the traced underdamped/critically damped setup (Q >= 0.5);
    alternate rates and Bell settings use the same static design, not a host
    parameter or smoothing emulation.
    """
    stereo = np.asarray(stereo, dtype=np.float64)
    if stereo.ndim != 2 or stereo.shape[0] != 2:
        raise ValueError("stereo must have shape (2, N)")
    if not isfinite(depth) or not 0.0 <= depth <= 1.0:
        raise ValueError("depth must be a finite fraction in [0, 1]")
    if not isfinite(sample_rate) or sample_rate < 1000:
        raise ValueError("sample_rate must be finite and at least 1000 Hz")
    if not isfinite(frequency) or frequency <= 0.0:
        raise ValueError("frequency must be finite and positive")
    if not isfinite(gain_db) or gain_db <= 0.0 or not isfinite(q) or q < 0.5:
        raise ValueError("the static model requires finite gain_db > 0 and q >= 0.5")
    if depth == 0.0 or stereo.shape[1] == 0:
        return np.zeros_like(stereo)

    amount = depth * -expm1(-gain_db / 3.0)
    bell = _matched_pole(sample_rate, frequency, q, "bell", gain_db)
    bandpass = _matched_pole(sample_rate, frequency, q, "bandpass")
    eq_left, eq_right = (_filtered(lane, bell) for lane in stereo)
    extracted = np.array((_filtered(eq_left, bandpass), _filtered(eq_right, bandpass)))
    mid = 0.5 * (extracted[0] + extracted[1])
    existing_side = _filtered(0.5 * (eq_left - eq_right), bandpass)
    side = amount * (0.55 * existing_side + 0.625 * _diffused_side(mid, sample_rate))

    wet = _tapped_reflections(extracted, sample_rate, amount)
    _add_feedback(wet, mid, sample_rate, amount)
    lowpass = _matched_pole(sample_rate, 16000.0 - 9500.0 * amount, _RETURN_Q, "lowpass")
    return_gain = 0.65 * amount * _return_duck(extracted, sample_rate)
    wet[0] = _filtered(wet[0], lowpass) * return_gain + side
    wet[1] = _filtered(wet[1], lowpass) * return_gain - side

    highpass = _matched_pole(sample_rate, 120.0, _RETURN_Q, "highpass")
    return np.array([_filtered(lane, highpass) for lane in wet])
