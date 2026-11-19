# Draws designs for the Engram Installer's bubble (sce_sys/icon0.png, 128 x 128).
# The home screen shows the icon cut to a circle, so each keeps what matters
# inside a radius of about 56 from the middle.
#
#   python make_icons.py     writes out/icons/<design>.png (128 x 128), a large
#                            copy of each (<design>-large.png) and sheet.png,
#                            which shows them as bubbles
# Shares its drawing with make_mockups.py (the LiveArea's designs).
import os

import numpy as np
from PIL import Image, ImageDraw

import make_mockups as mm

mm.S = 4  # the icons are small: draw them at four times the size
from make_mockups import ACCENT, TEXT, glow, gradient, image, orb, ring, text, title

SIZE = 128
OUT = os.path.join(mm.OUT, 'icons')
BLUE = ((20, 60, 235), (70, 190, 255))
HUES = [((92, 24, 228), (200, 130, 255)), ((200, 20, 140), (255, 138, 196)), ((0, 124, 160), (110, 246, 200)),
        ((30, 90, 230), (140, 210, 255)), ((92, 24, 228), (200, 130, 255)), ((0, 124, 160), (110, 246, 200))]


def letter(d, cx, cy, size, fill=(255, 255, 255), shade=(10, 30, 120)):
    """Engram's E, over a soft copy of itself so it stands off what's behind."""
    text(d, cx + 1.0, cy + 1.6, 'E', title(size), shade, anchor='mm')
    text(d, cx + 1, cy, 'E', title(size), fill, anchor='mm')


def arrow(d, cx, cy, size, fill):
    """A down arrow: install."""
    s = mm.S
    pts = [(cx, cy + size), (cx - size, cy), (cx - size * 0.42, cy), (cx - size * 0.42, cy - size * 0.9),
           (cx + size * 0.42, cy - size * 0.9), (cx + size * 0.42, cy), (cx + size, cy)]
    d.polygon([(x * s, y * s) for x, y in pts], fill=fill)


def design_drop():
    arr = gradient(SIZE, SIZE, (12, 14, 24), (30, 38, 74))
    glow(arr, 64, 62, 60, (40, 110, 255), 0.35)
    orb(arr, 64, 62, 45, *BLUE)
    orb(arr, 103, 100, 9, *BLUE)
    img = image(arr)
    letter(ImageDraw.Draw(img), 64, 63, 62)
    return img


def design_sphere():
    arr = gradient(SIZE, SIZE, (12, 14, 24), (28, 34, 62))
    for i, (edge, core) in enumerate(HUES):
        a = np.pi / 6 + i * np.pi / 3
        orb(arr, 64 + np.cos(a) * 43, 64 + np.sin(a) * 43, 14, edge, core, alpha=0.92)
    orb(arr, 64, 64, 30, *BLUE)
    img = image(arr)
    letter(ImageDraw.Draw(img), 64, 65, 42)
    return img


def design_gradient():
    arr = gradient(SIZE, SIZE, (26, 70, 215), (128, 60, 226))
    glow(arr, 26, 16, 70, (120, 220, 255), 0.55)
    img = image(arr)
    d = ImageDraw.Draw(img)
    letter(d, 60, 58, 82, shade=(30, 30, 140))
    s = mm.S
    d.ellipse([(94 - 15) * s, (95 - 15) * s, (94 + 15) * s, (95 + 15) * s], fill=(255, 255, 255))
    arrow(d, 94, 96, 8.5, (40, 80, 220))
    return img


def design_ring():
    arr = gradient(SIZE, SIZE, (12, 14, 24), (30, 38, 74))
    glow(arr, 64, 64, 56, ACCENT, 0.18)
    ring(arr, 64, 64, 49, 3.4, ACCENT, 1.0)
    img = image(arr)
    d = ImageDraw.Draw(img)
    text(d, 65, 55, 'E', title(56), TEXT, anchor='mm')
    arrow(d, 64, 94, 8, ACCENT)
    return img


DESIGNS = [('drop', 'Drop', design_drop), ('sphere', 'Sphere', design_sphere),
           ('gradient', 'Gradient', design_gradient), ('ring', 'Ring', design_ring)]


def bubble(img, size):
    """The icon as the home screen shows it: cut to a circle."""
    big = img.resize((size * 4, size * 4), Image.LANCZOS)
    mask = Image.new('L', big.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, big.size[0] - 1, big.size[1] - 1], fill=255)
    out = Image.new('RGBA', big.size, (0, 0, 0, 0))
    out.paste(big, (0, 0), mask)
    return out.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(OUT, exist_ok=True)
    sheet = Image.new('RGB', (len(DESIGNS) * 300, 380), (20, 24, 40))
    for i, (key, name, make) in enumerate(DESIGNS):
        large = make()
        large.save(os.path.join(OUT, key + '-large.png'))
        small = large.resize((SIZE, SIZE), Image.LANCZOS)
        small.save(os.path.join(OUT, key + '.png'))
        for size, y in ((256, 20), (64, 296), (28, 314)):
            b = bubble(small if size <= SIZE else large, size)
            x = i * 300 + (150 - size // 2 if size == 256 else 100 if size == 64 else 190)
            sheet.paste(b, (x, y), b)
        print('drew', name)
    sheet.save(os.path.join(OUT, 'sheet.png'))


if __name__ == '__main__':
    main()
