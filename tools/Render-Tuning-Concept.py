"""Render an offscreen UI concept only; no plugin or DSP code is changed.

Run with Pillow available: python tools/Render-Tuning-Concept.py
The controls and sample readings describe a proposed interface, not working DSP.
"""
from pathlib import Path
import math
import random

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "design"
FONT = ROOT / "source" / "assets" / "PixelifySans.ttf"
S = 2
W, H = 940, 970
PALETTES = {
    "neon": ("#090d17", "#111827", "#0c1220", "#2b3a53", "#f1f5ff", "#9eafc8", "#ff4ca0", "#64f5e3"),
    "device": ("#292b2a", "#252826", "#171b18", "#50564f", "#e4e4df", "#aeb1a9", "#f5a24b", "#9ac37d"),
    "spectral": ("#191a1e", "#24252b", "#1e1f24", "#383a43", "#e3e4ec", "#9b9daa", "#a390ed", "#63caba"),
}


def render(skin):
    bg, panel, inset, border, text, muted, accent, secondary = PALETTES[skin]
    image = Image.new("RGB", (W * S, H * S), bg)
    d = ImageDraw.Draw(image)

    def box(rect, fill, outline=None, width=1):
        d.rectangle(tuple(round(v * S) for v in rect), fill=fill, outline=outline, width=width * S)

    def line(points, fill, width=1):
        d.line([(round(x * S), round(y * S)) for x, y in points], fill=fill, width=width * S)

    def label(x, y, value, size=14, color=None, anchor="la"):
        d.text((round(x * S), round(y * S)), value, font=ImageFont.truetype(str(FONT), size * S), fill=color or text, anchor=anchor)

    def button(x, y, w, caption, active=False, color=None, h=27):
        c = color or accent
        box((x, y, x + w, y + h), c if active else inset, c if active else border)
        label(x + w / 2, y + h / 2, caption, 13, bg if active else text, "mm")
        if active:
            line([(x + 2, y + 2), (x + w - 2, y + 2)], text)

    def selector(x, y, w, caption):
        box((x, y, x + w, y + 31), inset, border)
        label(x + 10, y + 15, caption, 15, anchor="lm")
        line([(x + w - 22, y + 12), (x + w - 16, y + 18), (x + w - 10, y + 12)], accent, 2)

    def section(x, y, w, h, title, color=accent):
        box((x, y, x + w, y + h), panel, border)
        line([(x, y), (x + w, y)], color, 2)
        label(x + 13, y + 18, title, 13, color, "lm")

    def knob(cx, cy, title, value, proportion, foot):
        r = 39
        label(cx, cy - 66, title, 15, anchor="mm")
        d.ellipse(((cx-r)*S, (cy-r)*S, (cx+r)*S, (cy+r)*S), fill=inset, outline=border, width=3*S)
        bounds = ((cx-r-3)*S, (cy-r-3)*S, (cx+r+3)*S, (cy+r+3)*S)
        d.arc(bounds, 135, 405, fill=border, width=4*S)
        d.arc(bounds, 135, 135 + max(0.008, proportion)*270, fill=accent, width=4*S)
        angle = math.radians(135 + proportion*270)
        line([(cx + math.cos(angle)*26, cy + math.sin(angle)*26), (cx + math.cos(angle)*34, cy + math.sin(angle)*34)], secondary, 3)
        box((cx-49, cy+53, cx+49, cy+79), inset, border)
        label(cx, cy+66, value, 16, anchor="mm")
        label(cx, cy+96, foot, 11, muted, "mm")

    # Subtle square material, using the plugin's actual palette and embedded font.
    rng = random.Random(901)
    for _ in range(140):
        x, y = rng.randrange(0, W), rng.randrange(0, H)
        size = rng.choice((12, 24, 48, 72))
        rgb = tuple(int(bg[i:i+2], 16) + rng.randrange(2, 7) for i in (1, 3, 5))
        box((x, y, min(W, x+size), min(H, y+size)), rgb)

    box((18, 14, 922, 63), panel, border)
    box((18, 14, 21, 63), accent)
    label(34, 38, "VOCAL STACK", 20, anchor="lm")
    button(260, 25, 83, "Stack")
    button(351, 25, 83, "Routing")
    button(442, 25, 83, "Tuning", True)
    button(655, 25, 131, "SKIN / " + skin.upper())
    button(800, 25, 105, "BYPASS")

    label(23, 91, "PITCH CORRECTION", 23, anchor="lm")
    label(23, 117, "One shared tuner. Clean Lead + Tuned Lead feed every voice.", 13, muted, "lm")
    button(677, 80, 96, "TUNING ON", True, secondary)
    button(783, 80, 136, "Reset tuning")

    section(20, 143, 594, 147, "TARGET NOTES")
    label(35, 182, "KEY", 11, muted, "lm")
    selector(35, 194, 84, "C")
    label(133, 182, "SCALE", 11, muted, "lm")
    selector(133, 194, 191, "Natural minor")
    label(340, 182, "REFERENCE", 11, muted, "lm")
    selector(340, 194, 120, "440.0 Hz")
    button(477, 195, 121, "Chromatic", h=30)
    notes = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    enabled = {0, 2, 3, 5, 7, 8, 10}
    for i, note in enumerate(notes):
        button(35+i*47, 240, 42, note, i in enabled, secondary, h=33)

    section(628, 143, 292, 147, "DETECTION")
    label(643, 185, "VOCAL RANGE", 11, muted, "lm")
    selector(643, 198, 261, "Full vocal / 60-1000 Hz")
    label(643, 254, "Single vocal input", 14, secondary, "lm")
    label(643, 275, "Shared detection, not per-voice tuning", 11, muted, "lm")

    section(20, 307, 594, 247, "TUNING CHARACTER")
    knob(96, 420, "Correction", "100 %", 1, "OFF  /  FULL")
    knob(241, 420, "Retune speed", "0 ms", 0, "HARD  /  SLOW")
    knob(386, 420, "Note transition", "0 ms", 0, "STEP  /  GLIDE")
    knob(531, 420, "Note stability", "35 %", .35, "LOOSE  /  HOLD")

    section(628, 307, 292, 247, "VOICE CHARACTER")
    button(644, 348, 260, "[x] Preserve formants", True, secondary, h=34)
    label(645, 407, "Keep the vocal's tonal character", 13, anchor="lm")
    label(645, 429, "while correcting its pitch.", 13, muted, "lm")
    line([(644, 452), (904, 452)], border)
    label(645, 476, "HARD TUNE", 14, accent, "lm")
    label(645, 500, "100% correction + fast transitions", 12, muted, "lm")
    label(645, 524, "No added vibrato or voice effects here.", 11, muted, "lm")

    section(20, 572, 900, 168, "PITCH MONITOR", secondary)
    label(904, 591, "ILLUSTRATIVE DATA / NOT LIVE", 11, muted, "rm")
    for i, note in enumerate(("G3", "F3", "D#3", "D3", "C3")):
        y = 619 + i*24
        label(35, y, note, 11, muted, "lm")
        line([(79, y), (676, y)], border)
    for x in range(79, 677, 75):
        line([(x, 611), (x, 725)], border)
    points, target = [], []
    for i in range(598):
        x = 79+i
        base = 715 if i < 130 else 667 if i < 250 else 691 if i < 370 else 643 if i < 485 else 667
        points.append((x, base + 6*math.sin(i*.07) + 3*math.sin(i*.19)))
        target.append((x, base))
    line(points, muted, 2)
    line(target, secondary, 2)
    box((699, 611, 904, 725), inset, border)
    label(713, 630, "INPUT", 11, muted, "lm")
    label(889, 630, "D#3  +24 ct", 16, anchor="rm")
    label(713, 660, "TARGET", 11, muted, "lm")
    label(889, 660, "D#3", 18, secondary, "rm")
    line([(714, 680), (889, 680)], border)
    label(713, 704, "CORRECTION", 11, muted, "lm")
    label(889, 704, "-24 ct", 16, accent, "rm")

    label(21, 766, "VOICE TUNE MIX", 16, anchor="lm")
    label(918, 766, "CLEAN LEAD <-> TUNED LEAD   /   BEFORE EACH VOICE'S FX", 11, muted, "rm")
    for i, (name, value, desc) in enumerate((("LEAD", 100, "Original vocal"), ("VOICE 1", 100, "+7 st"), ("VOICE 2", 65, "+12 st"), ("VOICE 3", 0, "-12 st"))):
        x = 20+i*228
        section(x, 786, 216, 120, name, secondary if i == 0 else accent)
        label(x+201, 804, desc, 11, muted, "rm")
        y = 836
        box((x+14, y, x+202, y+26), inset, border)
        if value:
            box((x+15, y+1, x+15+186*value/100, y+25), secondary if i == 0 else accent)
        end = x+15+186*value/100
        line([(end, y-3), (end, y+29)], text, 2)
        label(x+108, y+13, f"Tune Mix {value}%", 13, bg if value >= 65 else text, "mm")
        label(x+14, 884, "Clean", 11, muted, "lm")
        label(x+202, 884, "Tuned", 11, muted, "rm")
    label(22, 930, "UI CONCEPT ONLY / No DSP, parameter wiring, or plugin changes", 12, accent, "lm")
    label(918, 949, "Additional voices use the same Tune Mix control. Example values shown.", 11, muted, "rm")
    path = OUT / f"tuning-tab-{skin}.png"
    image.save(path)
    print(path)


if __name__ == "__main__":
    for theme in PALETTES:
        render(theme)
