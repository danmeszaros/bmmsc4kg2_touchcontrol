"""UI mockups for the round 240x240 controller display (GC9A01)."""
import math, os, sys
from PIL import Image, ImageDraw, ImageFont

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
W = 240
C = W // 2
F = "/usr/share/fonts/truetype/dejavu/"
def font(sz, bold=False, cond=False):
    name = "DejaVuSansCondensed" if cond else "DejaVuSans"
    return ImageFont.truetype(F + name + ("-Bold" if bold else "") + ".ttf", sz)

BG = (0, 0, 0)
FG = (235, 235, 235)
DIM = (120, 120, 120)
FAINT = (55, 55, 55)
ACC = (255, 176, 0)      # amber: selected / active
REC = (230, 30, 30)
GREEN = (60, 200, 90)

PAGES = ["MAIN", "IRIS", "WB", "GAIN", "AUDIO"]

def canvas():
    im = Image.new("RGB", (W, W), BG)
    return im, ImageDraw.Draw(im)

def text(d, xy, s, f, fill=FG, anchor="mm"):
    d.text(xy, s, font=f, fill=fill, anchor=anchor)

def dots(d, page):
    n = len(PAGES); sp = 12; x0 = C - sp * (n - 1) / 2
    for i in range(n):
        r = 3.5 if i == page else 2.5
        col = FG if i == page else FAINT
        x = x0 + i * sp
        d.ellipse((x - r, 220 - r, x + r, 220 + r), fill=col)

def title(d, s):
    text(d, (C, 34), s, font(15, True), DIM)

def side_pm(d):
    # -/+ tap zones: left and right half
    text(d, (26, C), "−", font(34), DIM)
    text(d, (W - 26, C), "+", font(34), DIM)

def arc_gauge(d, frac, label_l, label_r):
    # 240° arc along the lower part of the bezel
    box = (10, 10, W - 10, W - 10)
    start, end = 150, 390
    d.arc(box, start, end, fill=FAINT, width=6)
    d.arc(box, start, start + (end - start) * frac, fill=ACC, width=6)
    for ang, lab in ((start, label_l), (end, label_r)):
        a = math.radians(ang); r = 94
        text(d, (C + r * math.cos(a), C + r * math.sin(a) + 10), lab, font(11), DIM)

def finish(im, name):
    mask = Image.new("L", (W, W), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, W - 1, W - 1), fill=255)
    out = Image.new("RGB", (W, W), (40, 40, 40))   # outside of the glass
    out.paste(im, (0, 0), mask)
    out.save(f"{OUT}/{name}.png")
    out.resize((W * 2, W * 2), Image.NEAREST).save(f"{OUT}/{name}@2x.png")
    return out

def status_strip(d, y=170):
    text(d, (C, y), "ISO 400 · f/5.6 · 5600K", font(13, cond=True), DIM)
    text(d, (C, y + 17), "25p · 180°", font(13, cond=True), DIM)

screens = []
PAGES[:] = ["MAIN", "AUDIO", "FPS"]

def back_hint(d):
    text(d, (C, 214), "▾ back", font(11), DIM)

def status_top(d, rec):
    if rec:
        d.ellipse((2, 2, W - 3, W - 3), outline=REC, width=8)
        d.ellipse((C - 58, 31, C - 48, 41), fill=REC)
        text(d, (C - 43, 36), "REC", font(13, True), REC, "lm")
    else:
        text(d, (C - 58, 36), "STBY", font(13, True), DIM, "lm")
    text(d, (C + 58, 36), "1:46 left", font(12), GREEN, "rm")

def tile(d, box, label, value, sub=None, vsize=30, sel=False):
    d.rounded_rectangle(box, radius=12, fill=(28, 28, 28), outline=(70, 70, 70) if not sel else ACC, width=2)
    x0, y0, x1, y1 = box
    return x0, y0, x1, y1

# --- main variant A: three full-width rows
def main_A(rec, name):
    im, d = canvas()
    status_top(d, rec)
    rows = (("IRIS", "f/5.6", None), ("WB", "5600K", "+11"), ("GAIN", "LOW", "ISO 400"))
    ys = (54, 106, 158); h = 46
    for (lab, val, sub), y in zip(rows, ys):
        half = math.sqrt(120**2 - (max(abs(y - C), abs(y + h - C)))**2) - 16
        box = (C - half, y, C + half, y + h)
        tile(d, box, lab, val)
        text(d, (box[0] + 14, y + h / 2), lab, font(12, True), DIM, "lm")
        text(d, (box[2] - 14, y + h / 2 - (6 if sub else 0)), val, font(26, True), FG, "rm")
        if sub: text(d, (box[2] - 14, y + h / 2 + 15), sub, font(10), DIM, "rm")
    dots(d, 0)
    return finish(im, name)

# --- main variant B: big IRIS on top, WB | GAIN below
def main_B(rec, name):
    im, d = canvas()
    status_top(d, rec)
    box = (34, 52, W - 34, 120); tile(d, box, "", "")
    text(d, (48, 66), "IRIS", font(12, True), DIM, "lm")
    text(d, (C, 94), "f/5.6", font(38, True), FG)
    for i, (lab, val, sub) in enumerate((("WB", "5600K", "tint +11"), ("GAIN", "LOW", "ISO 400"))):
        x0 = 32 + i * 90; b = (x0, 126, x0 + 86, 194); tile(d, b, "", "")
        cx = (b[0] + b[2]) / 2
        text(d, (cx, 140), lab, font(11, True), DIM)
        text(d, (cx, 164), val, font(20, True), FG)
        text(d, (cx, 184), sub, font(10), DIM)
    dots(d, 0)
    return finish(im, name)

screens.append(("1A_main_rows", main_A(False, "1A_main_rows")))
screens.append(("1A_main_rows_rec", main_A(True, "1A_main_rows_rec")))
screens.append(("1B_main_tiles", main_B(False, "1B_main_tiles")))
screens.append(("1B_main_tiles_rec", main_B(True, "1B_main_tiles_rec")))

# edit: iris
im, d = canvas()
title(d, "IRIS")
text(d, (C, 104), "f/5.6", font(52, True), FG)
text(d, (C, 146), "34 mm", font(14), DIM)
side_pm(d)
arc_gauge(d, 0.07, "3.5", "22")
back_hint(d)
screens.append(("E1_iris", finish(im, "E1_iris")))

# edit: wb
im, d = canvas()
title(d, "WHITE BAL")
text(d, (C, 96), "5600K", font(38, True), FG)
text(d, (C, 128), "tint +11", font(14), DIM)
side_pm(d)
d.rounded_rectangle((C - 42, 150, C + 42, 180), radius=15, outline=ACC, width=2)
text(d, (C, 165), "AUTO", font(15, True), ACC)
back_hint(d)
screens.append(("E2_wb", finish(im, "E2_wb")))

# edit: gain
im, d = canvas()
title(d, "BASE GAIN")
for i, (lab, iso, sel) in enumerate((("LOW", "ISO 400", True), ("HIGH", "ISO 3200", False))):
    x0 = 30 + i * 92; box = (x0, 72, x0 + 88, 168)
    if sel:
        d.rounded_rectangle(box, radius=14, fill=ACC); col, col2 = BG, BG
    else:
        d.rounded_rectangle(box, radius=14, outline=FAINT, width=2); col, col2 = FG, DIM
    text(d, ((box[0] + box[2]) / 2, 106), lab, font(22, True), col)
    text(d, ((box[0] + box[2]) / 2, 138), iso, font(12), col2)
text(d, (C, 188), "0 dB / +18 dB", font(11), DIM)
back_hint(d)
screens.append(("E3_gain", finish(im, "E3_gain")))

# page 2: audio
im, d = canvas()
title(d, "AUDIO")
rows = (("CH1", "JACK", "+20 dB", 0.6, True), ("CH2", "CAM", "0 dB", 0.5, False))
for i, (ch, src, lvl, frac, sel) in enumerate(rows):
    y = 80 + i * 58
    if sel:
        d.rounded_rectangle((24, y - 22, W - 24, y + 26), radius=10, outline=ACC, width=2)
    text(d, (40, y - 6), ch, font(13, True), DIM, "lm")
    d.rounded_rectangle((80, y - 16, 136, y + 4), radius=9, fill=(40, 40, 40))
    text(d, (108, y - 6), src, font(12, True), FG)
    text(d, (W - 40, y - 6), lvl, font(16, True), FG, "rm")
    d.rectangle((40, y + 12, W - 40, y + 16), fill=FAINT)
    d.rectangle((40, y + 12, 40 + (W - 80) * frac, y + 16), fill=ACC if sel else DIM)
side_pm(d)
text(d, (C, 196), "tap source: CAM ↔ JACK", font(10), DIM)
dots(d, 1)
screens.append(("P2_audio", finish(im, "P2_audio")))

# page 3: frame rate
im, d = canvas()
title(d, "FRAME RATE")
rates = ["23.98", "24", "25", "29.97", "30", "50"]
for i, r in enumerate(rates):
    col, row = i % 2, i // 2
    x = C - 46 + col * 92; y = 78 + row * 40
    box = (x - 40, y - 15, x + 40, y + 15)
    if i == 2:
        d.rounded_rectangle(box, radius=12, fill=ACC); c = BG
    else:
        d.rounded_rectangle(box, radius=12, outline=FAINT, width=2); c = FG
    text(d, (x, y), r, font(17, True), c)
text(d, (C, 196), "shutter stays 180°", font(11), DIM)
dots(d, 2)
screens.append(("P3_fps", finish(im, "P3_fps")))

# contact sheet
pad = 24; cols = 4; rows_n = 3; sz = W * 2
sheet = Image.new("RGB", (cols * (sz + pad) + pad, rows_n * (sz + pad + 30) + pad), (24, 24, 24))
sd = ImageDraw.Draw(sheet)
for i, (name, im) in enumerate(screens):
    x = pad + (i % cols) * (sz + pad); y = pad + (i // cols) * (sz + pad + 30)
    sheet.paste(im.resize((sz, sz), Image.NEAREST), (x, y))
    sd.text((x + sz / 2, y + sz + 15), name, font=font(18), fill=(200, 200, 200), anchor="mm")
sheet.save(f"{OUT}/overview.png")
print("ok", len(screens))
