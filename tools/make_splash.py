#!/usr/bin/env python3
"""PORT32X: splash screen for the 32X layer (256-colour mode, 320x224).
  tools/make_splash.py image.jpg SPLASH.SPL [preview.png]
SPLASH.SPL = 256 palette words (big-endian, 32X format: bit 0-4 red, 5-9 green, 10-14 blue)
+ 320x224 palette indices. Index 0 is the 32X's see-through colour and is never used; 254 =
the progress bar's frame, 255 = the progress bar's red. The image is fitted to the height in
the middle; the sides show the same image enlarged and darkened. Needs Pillow
(sudo apt install python3-pil)."""
import struct, sys
from PIL import Image, ImageEnhance, ImageFilter

W, H = 320, 224
BAR_FRAME, BAR_RED = 254, 255

def to32x(r, g, b):
    v = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
    return v or 0x0400  # Kobo rule 5: colour 0x0000 is see-through on the 32X

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src = Image.open(sys.argv[1]).convert("RGB")
    # behind it: the same image covering the screen, blurred and darkened
    cover = max(W / src.width, H / src.height)
    bg = src.resize((max(W, round(src.width * cover)), max(H, round(src.height * cover))), Image.LANCZOS)
    left, top = (bg.width - W) // 2, (bg.height - H) // 2
    bg = bg.crop((left, top, left + W, top + H)).filter(ImageFilter.GaussianBlur(3))
    bg = ImageEnhance.Brightness(bg).enhance(0.55)
    # the whole image, as large as fits (any shape), in the middle
    scale = min(W / src.width, H / src.height)
    fg = src.resize((max(1, round(src.width * scale)), max(1, round(src.height * scale))), Image.LANCZOS)
    bg.paste(fg, ((W - fg.width) // 2, (H - fg.height) // 2))
    q = bg.quantize(colors=253, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)
    pal = q.getpalette()[:253 * 3]
    idx = bytes(i + 1 for i in q.tobytes())
    words = [0] * 256
    for i in range(253):
        words[i + 1] = to32x(*pal[3 * i:3 * i + 3])
    words[BAR_FRAME] = to32x(24, 24, 24)
    words[BAR_RED] = to32x(232, 16, 16)
    with open(sys.argv[2], "wb") as f:
        f.write(b"".join(struct.pack(">H", w) for w in words))
        f.write(idx)
    if len(sys.argv) > 3:  # preview with the bar half full, as the SH2 draws it
        p = Image.new("RGB", (W, H))
        px = p.load()
        for y in range(H):
            for x in range(W):
                v = words[idx[y * W + x]]
                px[x, y] = ((v & 31) << 3, (v >> 5 & 31) << 3, (v >> 10 & 31) << 3)
        for y in range(BAR_Y, BAR_Y + BAR_H):
            for x in range(BAR_X, BAR_X + BAR_W):
                inner = BAR_Y < y < BAR_Y + BAR_H - 1 and BAR_X < x < BAR_X + BAR_W - 1
                red = inner and x - BAR_X - 1 < (BAR_W - 2) // 2
                v = words[BAR_RED if red else BAR_FRAME]
                px[x, y] = ((v & 31) << 3, (v >> 5 & 31) << 3, (v >> 10 & 31) << 3)
        p.save(sys.argv[3])
    print(f"{sys.argv[2]}: {512 + len(idx)} bytes (palette 512 + 320x224 pixels)")

# the progress bar (keep in step with cd32x/sh2_splash.c)
BAR_X, BAR_Y, BAR_W, BAR_H = 40, 204, 240, 8

if __name__ == "__main__":
    main()
