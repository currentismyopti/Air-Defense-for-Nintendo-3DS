#!/usr/bin/env python3
"""Stacks each pair of <name>_top.ppm / <name>_bot.ppm from the test harness into one PNG, 2x scale."""
import sys, glob, os
from PIL import Image
d = sys.argv[1]
for top in sorted(glob.glob(os.path.join(d, "*_top.ppm"))):
    name = top[:-8]
    t, b = Image.open(top), Image.open(name + "_bot.ppm")
    im = Image.new("RGB", (400, 484), (40, 40, 44))
    im.paste(t, (0, 0)); im.paste(b, (40, 244))
    im.resize((800, 968), Image.NEAREST).save(name + ".png")
    for f in (top, name + "_bot.ppm"): os.remove(f)
