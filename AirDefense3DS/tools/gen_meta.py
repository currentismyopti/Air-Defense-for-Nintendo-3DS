#!/usr/bin/env python3
"""Draws the HOME Menu icon (48x48), the banner image (256x128) and a short radar-ping banner sound."""
import math, os, struct, sys, wave
from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1] if len(sys.argv) > 1 else "meta"
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

def scope(size, cx, cy, r, sweep_deg, scale=4):
    w, h = size
    im = Image.new("RGB", (w * scale, h * scale), (10, 16, 13))
    d = ImageDraw.Draw(im, "RGBA")
    S = scale
    for gx in range(0, w, 8):
        d.line([(gx * S, 0), (gx * S, h * S)], fill=(90, 130, 100, 40), width=1)
    for gy in range(0, h, 8):
        d.line([(0, gy * S), (w * S, gy * S)], fill=(90, 130, 100, 40), width=1)
    for k in range(18):
        a0 = sweep_deg + k * 3
        d.pieslice([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], a0, a0 + 3.5, fill=(100, 255, 170, max(0, 120 - k * 7)))
    for k in (1, 2, 3):
        rr = r * k / 3
        d.ellipse([(cx - rr) * S, (cy - rr) * S, (cx + rr) * S, (cy + rr) * S], outline=(90, 210, 150, 200), width=max(1, S // 2))
    d.line([((cx - r) * S, cy * S), ((cx + r) * S, cy * S)], fill=(90, 210, 150, 110), width=1)
    d.line([(cx * S, (cy - r) * S), (cx * S, (cy + r) * S)], fill=(90, 210, 150, 110), width=1)
    return im, d, S

def jet(d, S, x, y, s, ang, col):
    pts = [(0, 1), (0.1, 0.45), (0.75, -0.25), (0.75, -0.38), (0.14, -0.3), (0.32, -0.8), (0.3, -0.9), (0, -0.75)]
    poly = pts + [(-px, py) for px, py in reversed(pts[1:-1])]
    ca, sa = math.cos(ang), math.sin(ang)
    d.polygon([((x + (px * ca - py * sa) * s) * S, (y - (px * sa + py * ca) * s) * S) for px, py in poly], fill=col)

im, d, S = scope((48, 48), 24, 24, 20, 300)
jet(d, S, 31, 14, 9, math.radians(215), (255, 105, 90, 255))
jet(d, S, 15, 31, 7, math.radians(40), (110, 180, 255, 255))
d.rectangle([0, 0, 48 * S - 1, 48 * S - 1], outline=(120, 160, 120, 255), width=S)
im.resize((48, 48), Image.LANCZOS).save(os.path.join(out, "icon.png"))

im, d, S = scope((256, 128), 196, 64, 52, 290)
jet(d, S, 214, 38, 13, math.radians(215), (255, 105, 90, 255))
jet(d, S, 168, 84, 10, math.radians(200), (255, 105, 90, 255))
jet(d, S, 222, 92, 11, math.radians(60), (110, 180, 255, 255))
f1, f2 = ImageFont.truetype(BOLD, 25 * S), ImageFont.truetype(BOLD, 10 * S)
d.text((12 * S, 30 * S), "AIR DEFENSE", font=f1, fill=(240, 245, 225, 255))
d.text((12 * S, 58 * S), "COMMAND", font=f1, fill=(240, 245, 225, 255))
d.text((13 * S, 92 * S), "Hold the line. Own the sky.", font=f2, fill=(150, 220, 175, 255))
im.resize((256, 128), Image.LANCZOS).save(os.path.join(out, "banner.png"))

# two soft radar pings, 16-bit mono, 2.4 s
rate, n = 32000, int(32000 * 2.4)
frames = bytearray()
for i in range(n):
    t = i / rate
    v = 0.0
    for start, freq in ((0.05, 1320.0), (1.15, 1320.0)):
        if t >= start:
            u = t - start
            v += math.sin(2 * math.pi * freq * u) * math.exp(-u * 5.5) * min(1.0, u * 400)
            v += 0.35 * math.sin(2 * math.pi * freq * 2 * u) * math.exp(-u * 9)
    frames += struct.pack("<h", int(max(-1, min(1, v * 0.45)) * 32767))
with wave.open(os.path.join(out, "banner.wav"), "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(bytes(frames))
