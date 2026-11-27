"""Bounded headless weak-Mid test; no audio or fits are saved.

From the research worktree root, with Measure-Spatial-EQ.py's local dependencies:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-HalfCpu.ps1 python -B tools/Probe-Spatial-EQ-MidWeakLaw.py
"""

from dataclasses import dataclass
import hashlib
from pathlib import Path
from runpy import run_path
from time import perf_counter


CTX = run_path(str(Path(__file__).with_name('Probe-Spatial-EQ-Context.py')))
np, capture, amp, samples, db = (CTX[k] for k in ('np', 'capture', 'amp', 'ms_samples', 'db_ratio'))
RATE, LEAD, BLOCK = (CTX[k] for k in ('RATE', 'LEAD', 'BLOCK'))
ON = samples(60) + 37
END = ON + samples(12)
EARLY = slice(ON, ON + samples(1.25))
LATE = slice(EARLY.stop, END)
DEV = (-60, -54, -48, -42)
LEVEL_TEST = (-63, -57, -51, -45, -39)
TRANSFER = (-57, -51, -45)
ANCHOR = -51
POWERS = (1, 2, 3)
LIMIT_RENDERS = 420
LIMIT_SECONDS = 60
norm = np.linalg.norm


def stereo(packet, start):
    x = np.zeros((2, END), dtype=np.float32)
    x[:, start:start + len(packet)] = packet
    return x


def history(shift):
    # Match MidTrigger's far/off history, including its four energy-matched slices.
    n = samples(4)
    env = np.hanning(n + 2)[1:-1].reshape(4, -1)
    carrier = np.cos(np.arange(n) * (2 * np.pi * 6000 / RATE)).reshape(4, -1)
    packet = env * carrier
    packet *= (norm(env, axis=1) / norm(packet, axis=1))[:, None]
    a = stereo(amp(-30) * packet.ravel(), ON - samples(3.5) - n)
    a[:, ON - samples(11)] = amp(-24)
    return a * np.float32(amp(shift))


def pulse(ms, hz, *, quadrature=False):
    n = samples(ms)
    phase = np.arange(n) * (2 * np.pi * hz / RATE)
    carrier = -np.sin(phase) if quadrature else np.cos(phase)
    y = np.hanning(n + 2)[1:-1] * carrier
    return y / np.max(np.abs(y))


def ulps(y):
    return np.abs(np.spacing(y.astype(np.float32))).astype(float)


@dataclass
class Trace:
    old: np.ndarray
    even: np.ndarray
    odd: np.ndarray
    isolated: np.ndarray
    rounding: np.ndarray
    off: np.ndarray


@dataclass
class Case:
    name: str
    level: int
    shift: int
    trace: Trace
    floor: float
    late_floor: float

    @property
    def drive(self):
        return amp(self.level + self.shift - ANCHOR)

    @property
    def even(self):
        return self.trace.even[EARLY]

    @property
    def usable(self):
        return norm(self.even) > 10 * self.floor


class Host:
    def __init__(self):
        self.start = perf_counter()
        self.count = 0
        self.histories = {}

    def render(self, x, schedule):
        kw = dict(lead=LEAD + 49, block=127, fresh=schedule == 'fresh') if schedule else {}
        pair = []
        for depth in (0., 1.):
            if self.count >= LIMIT_RENDERS or perf_counter() - self.start > LIMIT_SECONDS:
                raise RuntimeError('Probe budget exhausted')
            pair.append(capture(x, depth, **kw).mean(axis=0))
            self.count += 1
        return np.stack(pair)

    def interaction(self, shape, level, shift, schedule):
        a = history(shift)
        key = shift, schedule
        if key not in self.histories:
            self.histories[key] = self.render(a, schedule)
        prior = self.histories[key]
        b = stereo(amp(level + shift) * shape, ON)
        hit = np.stack([self.render(s * b, schedule) for s in (1, -1)])
        joined = np.stack([self.render(a + s * b, schedule) for s in (1, -1)])
        e = joined - prior - hit
        delta = e[:, 1] - e[:, 0]
        # One-ULP sum bounds host output rounding, not internal DSP arithmetic.
        rounding = (ulps(prior) + 0.5 * (ulps(joined) + ulps(hit)).sum(axis=0)).sum(axis=0)
        return Trace(prior[1] - prior[0], delta.mean(axis=0), (delta[0] - delta[1]) / 2,
                     hit[:, 1] - hit[:, 0], rounding, e[:, 0])

    def measure(self, name, shape, level, shift=0, *, fresh=False):
        a = self.interaction(shape, level, shift, '')
        b = self.interaction(shape, level, shift, 'fresh' if fresh else 'reblocked')

        def floor(w):
            return max(1e-15, norm(a.rounding[w]), norm(b.rounding[w]),
                       norm(a.even[w] - b.even[w]), norm(a.odd[w] - b.odd[w]),
                       *(norm(off[w]) for off in (*a.off, *b.off)))

        pre = slice(None, ON)
        if max(norm(a.even[pre]), norm(a.odd[pre])) > 10 * floor(pre):
            raise RuntimeError('Pre-hit additivity control failed')
        return Case(name, level, shift, a, floor(EARLY), floor(LATE))


def scalar_error(y, template):
    return db(norm(y - template * (y @ template) / (template @ template)), norm(y))


def report_levels(groups, coefficients, template):
    print('\nEarly [0,1.25) ms. Norm columns p=1/2/3 are measured/predicted dB; free slope is diagnostic.')
    print('Wave is frozen p=2 template error; oracle and old fit a scalar to template and old tail (dBr).')
    print('Old gain is the signed projection of even onto old; negative means a loss along the old tail.')
    print('shape         B nominal    S/floor    norm FS    p=1    p=2    p=3    wave   oracle     old  old gain %')
    for group in groups:
        errors = []
        for c in group:
            size = norm(c.even)
            label = f'{c.name:14} {c.level:8} {db(size, c.floor):10.1f} {size:10.2g}'
            if not c.usable:
                print(label + '  floor-limited')
                continue
            row = [db(size, k * c.drive ** p) for p, k in zip(POWERS, coefficients)]
            errors.append(row)
            pred = template * c.drive ** 2
            old = c.trace.old[EARLY]
            gain = (c.even @ old) / (old @ old)
            print(label + ''.join(f' {e:6.1f}' for e in row)
                  + f' {db(norm(c.even - pred), size):7.1f}'
                  + f' {scalar_error(c.even, template):8.1f} {scalar_error(c.even, old):7.1f} {100 * gain:11.4f}')
        usable = [c for c in group if c.usable]
        if len(usable) >= 3:
            slope = np.polyfit(np.log([c.drive for c in usable]), np.log([norm(c.even) for c in usable]), 1)[0]
            rmse = np.sqrt(np.mean(np.square(errors), axis=0))
            print(f'  norm slope {slope:.3f}; fixed-p norm RMSE (dB): ' + ' / '.join(f'{r:.2f}' for r in rmse))


def report_scale(cases, shifted):
    print('\nJoint scale: same relative drive, early traces divided by their own old-tail norms.')
    print('Old shape and waveform errors in dBr; norm shift is shifted/base in dB.')
    print('base B    shift   norm shift   old shape  relative law  absolute B²   pair floor')
    for c in shifted:
        base = cases[c.level]
        if not (c.usable and base.usable):
            print(f'{c.level:6} {c.shift:8}  floor-limited')
            continue
        old, old0 = c.trace.old[EARLY], base.trace.old[EARLY]
        y, y0 = c.even / norm(old), base.even / norm(old0)
        floor = c.floor / norm(old) + base.floor / norm(old0)
        print(f'{c.level:6} {c.shift:8} {db(norm(y), norm(y0)):12.2f}'
              f' {db(norm(old / norm(old) - old0 / norm(old0)), 1):11.1f}'
              f' {db(norm(y - y0), norm(y)):13.1f} {db(norm(y - y0 * amp(c.shift)**2), norm(y)):12.1f}'
              f' {db(floor, norm(y)):12.1f}')


def main():
    host = Host()
    with CTX['HOST']['MODULE'].open('rb') as module:
        print(f"Reference SHA-256 {hashlib.file_digest(module, 'sha256').hexdigest()}; "
              f"Pedalboard {CTX['pb'].__version__}, NumPy {np.__version__}")
    print(f'{RATE} Hz, +6 dB / 2.4 kHz / Q=1, dynamics off, dual mono, lead/block {LEAD}/{BLOCK}, hit sample {ON}.')
    print('A: -24 dBFS click at -11 ms plus -30 dBFS scale on a 4 ms / 6 kHz packet ending -3.5 ms.')
    print('E±=Delta(A±B)-Delta(A)-Delta(±B), Delta=Depth100-Depth0; even/odd from ± sum/difference.')
    print('Floor proxy: max of output ULP sum, Depth-0 residual and lead+49/block127 disagreement (fresh at weakest holdout).')
    shape = pulse(.25, 6000)
    dev = [host.measure('dev Hann', shape, level) for level in DEV]
    fit = [c for c in dev if c.usable]
    if len(fit) < 3:
        raise RuntimeError('Fewer than three development levels clear the floor; no fit justified')
    # Equal log-norm and equal normalized-waveform weights, not loudest-case LS.
    coefficients = [np.exp(np.mean([np.log(norm(c.even) / c.drive ** p) for c in fit])) for p in POWERS]
    template = np.mean([c.even / c.drive ** 2 for c in fit], axis=0)
    levels = [host.measure('held Hann', shape, level, fresh=level == min(LEVEL_TEST)) for level in LEVEL_TEST]
    groups = [dev, levels]
    print('Development hit: 0.25 ms Hann / 6 kHz. Levels in peak dBFS/channel; unseen pulses match input L2.')
    for name, wave in (('click anchor', np.ones(1)), ('0.5ms/6k', pulse(.5, 6000)),
                       ('0.25ms/2.4k', pulse(.25, 2400)), ('0.25ms/6k +90', pulse(.25, 6000, quadrature=True))):
        scale = norm(shape) / norm(wave)
        print(f'  {name}: actual peak = nominal B {db(scale, 1):+.2f} dB')
        groups.append([host.measure(name, scale * wave, level) for level in TRANSFER])
    report_levels(groups, coefficients, template)

    shifted = [host.measure('joint', shape, level, shift) for level in (-57, -45) for shift in (-6, 6)]
    report_scale({c.level: c for c in levels}, shifted)
    print('\nLate [1.25,12) ms measured separately, never fitted; ratio requires both >10x floor.')
    print('shape         B nominal   late odd/floor   late even/floor    odd/even   early isolated/old')
    for group in groups[1:]:
        c = group[3] if group is levels else group[1]  # -45 level holdout, -51 shape transfer
        odd, even = norm(c.trace.odd[LATE]), norm(c.trace.even[LATE])
        ratio = f'{db(odd, even):.1f}' if min(odd, even) > 10 * c.late_floor else 'n/a'
        isolated = max(norm(y[EARLY]) for y in c.trace.isolated)
        print(f'{c.name:14} {c.level:8} {db(odd, c.late_floor):16.1f} {db(even, c.late_floor):17.1f}'
              f' {ratio:>11} {db(isolated, norm(c.trace.old[EARLY])):20.1f}')
    print(f'{host.count} headless renders in {perf_counter() - host.start:.1f}s; no audio or fits written.')


if __name__ == '__main__':
    main()
