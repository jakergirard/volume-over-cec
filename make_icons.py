#!/usr/bin/env python3
# App icons: a segmented amber level-meter mark on a matte-dark faceplate.
from PIL import Image, ImageDraw

BG1 = (16, 17, 20)      # matte faceplate
BG2 = (26, 28, 33)      # top sheen
AMBER = (240, 169, 42)
AMBER_DIM = (120, 86, 26)

def icon(size, path):
    s = 4
    S = size * s
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(S * 0.22)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=r, fill=BG1)
    # subtle top sheen
    d.rounded_rectangle([0, 0, S - 1, int(S * 0.30)], radius=r, fill=BG2)
    d.rectangle([0, int(S * 0.16), S - 1, S - 1], fill=BG1)
    d.rounded_rectangle([0, 0, S - 1, S - 1], radius=r, outline=(44, 48, 54), width=max(1, int(S*0.006)))

    # level meter: 5 vertical bars of rising height, leading bar brightest
    bars = 5
    heights = [0.30, 0.44, 0.60, 0.78, 0.94]
    lit = [AMBER_DIM, AMBER_DIM, AMBER, AMBER, AMBER]
    gap = S * 0.045
    total_w = S * 0.56
    bw = (total_w - gap * (bars - 1)) / bars
    x0 = (S - total_w) / 2
    base = S * 0.76
    for i in range(bars):
        h = S * 0.52 * heights[i]
        x = x0 + i * (bw + gap)
        d.rounded_rectangle([x, base - h, x + bw, base],
                            radius=int(bw * 0.35), fill=lit[i])

    img = img.resize((size, size), Image.LANCZOS)
    img.save(path)
    print("wrote", path, size)

icon(80, "app/icon.png")
icon(130, "app/largeIcon.png")
