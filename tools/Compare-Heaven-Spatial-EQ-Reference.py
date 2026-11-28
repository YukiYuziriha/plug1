"""Compare a built HEAVEN VST3's routed Spatial EQ against installed 0.7.0.

Uses Pedalboard 0.9.26, a local mono/stereo recording, and the same JUCE
VST3 state representation used by Pedalboard. Exports an RMS-matched local
24-bit WAV pair. No standalone DSP, DAW, graphical editor, or install.
"""

import argparse
import hashlib
import math
from pathlib import Path
from runpy import run_path
from struct import pack, unpack_from
import xml.etree.ElementTree as ET


TOOLS = Path(__file__).resolve().parent
RATE = 48_000
# Same cut+boost preset as the standalone ensemble comparison.
BANDS = ((2, 850, -9, 0.75, 1.4, "bell"), (4, 280, -9, 0.50, 0.7, "low_shelf"),
         (7, 4200, -6, 1.00, 1.3, "high_shelf"), (6, 2500, 6, 0.64, 1.1, "bell"))
SHAPES = ("bell", "low_shelf", "high_shelf")
ALPHABET = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
DIGITS = {c: n for n, c in enumerate(ALPHABET)}


def xml_blob(tree):
    body = ET.tostring(tree, encoding="utf-8")
    return b"VC2!" + pack("<I", len(body)) + body + b"\0"


def unpack_xml(blob):
    if blob[:4] != b"VC2!" or len(blob) < 8:
        raise RuntimeError("unsupported JUCE state format (expected VC2!)")
    size, = unpack_from("<I", blob, 4)
    if size > len(blob) - 8:
        raise RuntimeError("truncated JUCE state")
    return ET.fromstring(blob[8:8 + size])


def encode_component(blob):
    # juce::MemoryBlock stores six-bit values in little-endian bit order.
    chars = []
    for i in range(0, len(blob), 3):
        part = blob[i:i + 3]
        word = int.from_bytes(part, "little")
        chars.extend(ALPHABET[(word >> bit) & 63] for bit in range(0, 8 * len(part), 6))
    return f"{len(blob)}." + "".join(chars)


def decode_component(text):
    length, chars = text.split(".", 1)
    out = bytearray()
    for i in range(0, len(chars), 4):
        word = sum(DIGITS[c] << (6 * k) for k, c in enumerate(chars[i:i + 4]))
        out.extend(word.to_bytes(3, "little"))
    length = int(length)
    if len(chars) != (8 * length + 5) // 6:
        raise RuntimeError("invalid JUCE component data size")
    return out[:length]


def state_for(bands):
    values = {"levelOn": 0, "doubleOn": 0, "formantOn": 0, "bypass": 0,
              "outputGain": 0, "globalMix": 100, "fx1On": 1}
    for slot, hz, gain, depth, q, shape in bands:
        prefix = f"fx1SpatialEqBand{slot}"
        for field, value in zip(("On", "Shape", "Frequency", "Gain", "Q", "Depth"),
                                (1, SHAPES.index(shape), hz, gain, q, 100 * depth)):
            values[prefix + field] = value
    tree = ET.Element("VocalStack")
    for key, value in values.items():
        ET.SubElement(tree, "PARAM", id=key, value=str(value))
    # Stable IDs from the processor fixture: Lead -> rack slot 1 -> Output.
    graph = ET.SubElement(tree, "RoutingGraph")
    for node, kind in (("1", "0"), ("100", "28"), ("13", "5")):
        ET.SubElement(graph, "Node", id=node, type=kind)
    for src, dst in (("1", "100"), ("100", "13")):
        ET.SubElement(graph, "Link", source=src, destination=dst, inputPin="0")
    state = ET.Element("VST3PluginState")
    ET.SubElement(state, "IComponent").text = encode_component(xml_blob(tree))
    return xml_blob(state), values


def open_vst3(module):
    from pedalboard._pedalboard import _PythonExternalPluginMixin
    from pedalboard_native import VST3Plugin
    from unittest.mock import patch

    def no_initial_values(_plugin, values):
        if values:
            raise RuntimeError("unexpected Pedalboard initialization parameter values")

    # Pedalboard 0.9.26 calls this hook even with no overrides and eagerly
    # probes every Python parameter range. This single-threaded probe uses
    # native parameters instead; skip the empty hook only during construction.
    with patch.object(_PythonExternalPluginMixin, "__set_initial_parameter_values__", no_initial_values):
        return VST3Plugin(str(module))


def heaven_capture(host, module, x, bands, *, split=None, gain_write=None):
    plugin = open_vst3(module)
    if not plugin.name.startswith("HEAVEN"):
        raise RuntimeError(f"expected built HEAVEN VST3, got {plugin.name!r}")
    state, expected = state_for(bands)
    # The mixin's __setattr__ scans all host parameters even for raw_state.
    # Call the native state descriptor without that name lookup.
    object.__setattr__(plugin, "raw_state", state)
    outer = unpack_xml(plugin.raw_state)
    saved = unpack_xml(decode_component(outer.findtext("IComponent")))
    params = {p.get("id"): float(p.get("value")) for p in saved.findall("PARAM")}
    # JUCE's float normalization round trip can move a frequency by a few ULPs.
    wrong = {key: (value, params.get(key)) for key, value in expected.items()
             if key not in params or not math.isclose(params[key], value, rel_tol=5e-7, abs_tol=1e-4)}
    if wrong:
        raise RuntimeError(f"HEAVEN parameter state did not round-trip: {wrong}")
    graph = saved.find("ActiveRoutingGraph")
    if graph is None or {n.get("id"): n.get("type") for n in graph.findall("Node")} != {
            "1": "0", "100": "28", "13": "5"}:
        raise RuntimeError("HEAVEN did not recall the active Spatial EQ routing graph")
    actual_links = {(n.get("source"), n.get("destination")) for n in graph.findall("Link")}
    if actual_links != {("1", "100"), ("100", "13")}:
        raise RuntimeError(f"unexpected active HEAVEN links: {actual_links}")
    def process(part, reset):
        y = plugin.process(part.copy(), RATE, buffer_size=host["BLOCK"], reset=reset)
        if y.shape != part.shape or not host["np"].isfinite(y).all() or plugin.reported_latency_samples != 0:
            raise RuntimeError("unexpected HEAVEN shape, non-finite output, or nonzero reported latency")
        return y.astype("float64")

    if split is None:
        return process(x, True)
    first = process(x[:, :split], True)
    if gain_write is not None:
        slot, gain = gain_write
        # Pedalboard 0.9.26: look up the exact native name to avoid building
        # Python metadata for all of HEAVEN's host parameters. Never reload state.
        parameter = plugin._get_parameter(f"FX 1 Spatial EQ Band {slot} Gain")
        if parameter is None or not parameter.is_automatable:
            raise RuntimeError(f"HEAVEN band {slot} Gain is missing or not automatable")
        parameter.raw_value = parameter.get_raw_value_for_text(str(gain))
        if float(parameter.get_text_for_raw_value(parameter.raw_value).split()[0]) != gain:
            raise RuntimeError(f"HEAVEN band {slot} Gain write did not stick")
    second = process(x[:, split:], False)
    if gain_write is not None:
        outer = unpack_xml(plugin.raw_state)
        saved = unpack_xml(decode_component(outer.findtext("IComponent")))
        field = f"fx1SpatialEqBand{slot}Gain"
        value = saved.find(f"PARAM[@id='{field}']")
        if value is None or not math.isclose(float(value.get("value")), gain, abs_tol=1e-4):
            raise RuntimeError(f"HEAVEN processor did not retain the {field} write")
    return host["np"].concatenate((first, second), axis=1)


def reference_capture(host, x, bands, *, split=None, gain_write=None):
    from pedalboard._pedalboard import to_python_parameter_name

    plugin = open_vst3(host["MODULE"])
    sweep = host["SWEEP"]
    set_raw, number = (sweep[k] for k in ("set_raw", "display_number"))

    def find_raw(parameter, field, target):
        # Same display-bin midpoint as the reference probes, but query the
        # candidate text without sending dozens of intermediate gain writes.
        def displayed(raw):
            return number(parameter.get_text_for_raw_value(raw), field)

        def edge(upper):
            lo, hi = 0.0, 1.0
            for _ in range(25):
                mid = (lo + hi) / 2
                value = displayed(mid)
                if value < target or (upper and value == target):
                    lo = mid
                else:
                    hi = mid
            return (lo + hi) / 2

        for raw in (0.0, 1.0):
            if displayed(raw) == target:
                return raw
        raw = (edge(False) + edge(True)) / 2
        if displayed(raw) != target:
            raise RuntimeError(f"reference {parameter.name} has no displayed value {target}")
        return raw

    def native_params():
        # Use the native parameters: building Pedalboard's Python range metadata
        # for every band and control dominates repeated captures on this version.
        return {to_python_parameter_name(p): p for p in plugin._parameters}

    def write(p, field, value, raw):
        if number(set_raw(p, raw), field) != value:
            raise RuntimeError(f"reference {p.name} did not accept {value}")

    p = native_params()
    settings = {f"band_{i}_active": 0 for i in range(1, 9)}
    settings.update(processing=0, output=0.5, bypass=0, spatial_bypass=0, mono_audition=0,
                    spatial_bass_protection=sweep["BASE"]["spatial_bass_protection"])
    for slot, hz, gain, depth, q, shape in bands:
        prefix = f"band_{slot}_"
        shape_raw, display = host["SHAPES"][shape]
        if set_raw(p[prefix + "shape"], shape_raw) != display:
            raise RuntimeError(f"reference slot {slot} did not accept {display}")
        for field, value in (("frequency", hz), ("gain", gain), ("q", q)):
            parameter = p[prefix + field]
            write(parameter, field, value, find_raw(parameter, field, value))
        write(p[prefix + "depth"], "depth", round(depth * 100), depth)
        settings.update({prefix + k: v for k, v in dict(active=1, enabled=1, stereo=0, dynamic=0,
                                                       width=0.5, spread=0, sidechain=0).items()})
    for key, raw in settings.items():
        set_raw(p[key], raw)
    if gain_write is not None:
        slot, gain = gain_write
        raw_gain = find_raw(p[f"band_{slot}_gain"], "gain", gain)
    del p

    def process(part, reset):
        y = plugin.process(part.copy(), RATE, buffer_size=host["BLOCK"], reset=reset)
        if y.shape != part.shape or not host["np"].isfinite(y).all():
            raise RuntimeError("unexpected reference shape or non-finite output")
        return y.astype("float64")

    if split is None:
        return process(x, True)
    first = process(x[:, :split], True)
    if gain_write is not None:
        write(native_params()[f"band_{slot}_gain"], "gain", gain, raw_gain)
    return host["np"].concatenate((first, process(x[:, split:], False)), axis=1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("heaven", type=Path, help="actual Windows VST3 module, not the bundle directory")
    parser.add_argument("audio", type=Path)
    parser.add_argument("output", type=Path, help="new directory; keep outputs out of version control")
    parser.add_argument("--seconds", type=float, default=2.5)
    parser.add_argument("--suite", choices=("static", "crossing", "both"), default="static")
    parser.add_argument("--crossing-shape", choices=("bell", "low_shelf"), action="append",
                        help="limit crossing tests to a shape; default: both shapes")
    args = parser.parse_args()
    if not 0.25 <= args.seconds <= 10 or not args.audio.is_file() or not args.heaven.is_file() or args.output.exists():
        parser.error("need a local audio file, built module, new output directory and --seconds in [0.25, 10]")
    host = run_path(str(TOOLS / "Test-Spatial-EQ-MultibandAutomation.py"))
    run_path(str(TOOLS / "Test-Spatial-EQ-Shelves.py"))["identify"]()
    from pedalboard.io import AudioFile
    np = host["np"]
    with args.heaven.open("rb") as file:
        print(f"HEAVEN module: {args.heaven.resolve()} | SHA-256 {hashlib.file_digest(file, 'sha256').hexdigest()}")
    with args.audio.open("rb") as file:
        print(f"Audio: {args.audio.name} | SHA-256 {hashlib.file_digest(file, 'sha256').hexdigest()}")
    with AudioFile(str(args.audio)) as source:
        if source.num_channels not in (1, 2):
            parser.error("expected a mono or stereo recording")
        if source.samplerate == RATE:
            clip = source.read(round(args.seconds * RATE))
        else:
            with source.resampled_to(RATE) as resampled:
                clip = resampled.read(round(args.seconds * RATE))
    if not clip.size or not np.isfinite(clip).all() or not np.any(clip):
        parser.error("empty, silent or non-finite recording")
    if clip.shape[0] == 1:
        clip = np.repeat(clip, 2, axis=0)
    lead = RATE // 10
    x = np.pad((clip * (10 ** (-12 / 20) / np.max(np.abs(clip)))).astype(np.float32),
               ((0, 0), (lead, RATE // 5)))
    module = args.heaven.resolve()

    def cap(config, **kwargs):
        return reference_capture(host, x, config, **kwargs)

    def integrated(config, **kwargs):
        return heaven_capture(host, module, x, config, **kwargs)
    peak = lambda y: float(np.max(np.abs(y)))
    rms = lambda y: float(np.sqrt(np.mean(y**2)))
    db = lambda value: 20 * math.log10(value) if value > 0 else -math.inf

    dry_error = max(peak(integrated(()) - x), peak(cap(()) - x))
    if dry_error > 1e-7:
        raise RuntimeError(f"dry anchor not transparent: {dry_error:.6g} FS")
    print(f"No-band dry anchor max error: {dry_error:.6g} FS")

    def score(name, a, b):
        if min(rms(a), rms(b)) <= 1e-8:
            raise RuntimeError(f"{name}: reference or HEAVEN signal too quiet to score")
        print(f"{name}: L/R pooled residual {db(rms(b-a) / rms(a)):.3f} dBr, peak error {peak(b-a):.6g} FS, "
              f"RMS gain diff {db(rms(b) / rms(a)):+.7f} dB, "
              f"crest ref/HEAVEN {db(peak(a) / rms(a)):.6f}/{db(peak(b) / rms(b)):.6f} dB")

    def export(label, ref, got):
        match = rms(ref) / rms(got)
        pad = min(1.0, 10 ** (-1 / 20) / max(peak(ref), peak(got * match)))
        args.output.mkdir(parents=True, exist_ok=True)
        verified = []
        print(f"Export: {ref.shape[1] / RATE:.6f} s including tail, 48 kHz stereo 24-bit; "
              f"HEAVEN match {db(match):+.7f} dB, shared trim {db(pad):+.7f} dB")
        for tag, signal in (("reference_Spatial_EQ_0.7.0", ref), ("HEAVEN_integrated_Spatial_EQ_VST3", got * match)):
            path = (args.output / f"{label}__{tag}.wav").resolve()
            with AudioFile(str(path), "w", RATE, 2, bit_depth=24) as file:
                file.write((signal * pad).astype(np.float32))
            with AudioFile(str(path)) as file:
                if (file.samplerate, file.num_channels, file.frames) != (RATE, 2, ref.shape[1]):
                    raise RuntimeError(f"unexpected WAV metadata: {path}")
                readback = file.read(file.frames).astype(np.float64)
            verified.append(rms(readback))
            print(f"{path}: RMS {db(verified[-1]):.8f} dBFS, peak {db(peak(readback)):.8f} dBFS")
        if abs(db(verified[1] / verified[0])) > 1e-5:
            raise RuntimeError("exported RMS match failed")

    if args.suite in ("static", "both"):
        print(f"Static preset (slot, Hz, dB, Depth, Q, shape): {BANDS}")
        off = tuple((slot, hz, gain, 0, q, shape) for slot, hz, gain, _, q, shape in BANDS)
        got, got_off = integrated(BANDS)[:, lead:], integrated(off)[:, lead:]
        ref, ref_off = cap(BANDS)[:, lead:], cap(off)[:, lead:]
        score("Full", ref, got)
        score("Depth off", ref_off, got_off)
        score("Depth delta", ref - ref_off, got - got_off)
        export("static_cut_plus_boost", ref, got)

    if args.suite in ("crossing", "both"):
        width = RATE // 10
        # Hold sample phase constant and pick a voiced region on both sides.
        candidates = range(lead + 2 * width + 11, lead + clip.shape[1] - 2 * width, 480)
        write = max(candidates, key=lambda i: min(rms(x[:, i-width:i]), rms(x[:, i:i+width])), default=None)
        if write is None or min(rms(x[:, write-width:write]), rms(x[:, write:write+width])) < 1e-5:
            parser.error("crossing needs a longer or more consistently voiced clip")
        print(f"Between-call write at sample {write - lead} ({(write - lead) / RATE:.6f} s in WAV); "
              "second call uses reset=False in both plugins")
        whole, edge = slice(lead, None), slice(write, write + width)
        shapes = set(args.crossing_shape or ("bell", "low_shelf"))
        for slot, hz, _, depth, q, shape in BANDS[:-1]:
            if shape not in shapes:
                continue
            for gain in (-6, 6):
                bands = ((slot, hz, gain, depth, q, shape), BANDS[-1])
                label = f"{shape} {gain:+} to {-gain:+} dB"
                print(f"Crossing {label}: {bands}", flush=True)
                got_unsplit, got_noop = integrated(bands), integrated(bands, split=write)
                ref_unsplit, ref_noop = cap(bands), cap(bands, split=write)
                for plugin, unsplit, noop in (("HEAVEN", got_unsplit, got_noop),
                                              ("0.7.0", ref_unsplit, ref_noop)):
                    error = peak(noop - unsplit)
                    print(f"  {plugin} no-op split vs unsplit: {error:.6g} FS peak; "
                          f"{'identical' if error == 0 else 'split affects render'}")
                got = integrated(bands, split=write, gain_write=(slot, -gain))
                ref = cap(bands, split=write, gain_write=(slot, -gain))
                if peak(ref[:, :write] - ref_noop[:, :write]) or peak(got[:, :write] - got_noop[:, :write]):
                    raise RuntimeError(f"{label}: gain write affected samples before the split")
                for name, window in (("whole clip", whole), ("first 100 ms", edge)):
                    score(f"  Full / {name}", ref[:, window], got[:, window])
                    score(f"  Change vs no-op / {name}", (ref - ref_noop)[:, window], (got - got_noop)[:, window])
                export(f"cross_{shape}_{gain:+d}_to_{-gain:+d}", ref[:, whole], got[:, whole])


if __name__ == "__main__":
    main()
