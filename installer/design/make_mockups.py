# Draws LiveArea designs for the Engram Installer (the 840 x 500 background and
# the 280 x 158 start gate of each) and a mock-up of each on the Vita, made by
# putting them under the system's own parts cut from a real screenshot
# (vita_screenshot.png: the status bar, the three buttons, the gate's frame,
# the Start button and the page curl).
#
# Where the Vita puts things over the background, in the background's own
# pixels (measured from the screenshot):
#   the three buttons   x 273-571, y 0-65
#   the gate            x 275-568, y 133-305
#   the page curl       x 760-840, y 0-80
# and over the gate's image, the Start button: x 73-207, y 118-152.
#
# The background also shows with nothing over it for a moment while the page
# opens or closes, so each has to look finished without the gate (bare.png).
#
#   python make_mockups.py            writes out/<design>/{bg,startup,mockup,bare}.png
# The fonts (Righteous and Poppins, SIL Open Font License) only draw the images.
import os
import subprocess

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', '..', '..', '..', 'fonts')
OUT = os.path.join(HERE, 'out')
BG_W, BG_H, GATE_W, GATE_H = 840, 500, 280, 158
S = 2  # everything is drawn at twice the size, then scaled down

TOP, BOTTOM = (14, 16, 24), (28, 34, 62)
TEXT, DIM, ACCENT = (236, 236, 240), (168, 174, 190), (124, 192, 255)
VIOLET, MAGENTA, TEAL = (170, 96, 255), (255, 84, 160), (40, 210, 190)


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), int(size * S))


def title(size):
    return font('Righteous-Regular.ttf', size)


def body(size):
    return font('Poppins-Regular.ttf', size)


def gradient(w, h, top=TOP, bottom=BOTTOM):
    t = np.linspace(0.0, 1.0, h * S)[:, None, None]
    row = np.array(top, float)[None, None, :] * (1 - t) + np.array(bottom, float)[None, None, :] * t
    return np.repeat(row, w * S, axis=1)


def grid(arr):
    h, w = arr.shape[:2]
    y, x = np.mgrid[0:h, 0:w]
    return x / S, y / S


def glow(arr, cx, cy, radius, color, strength):
    x, y = grid(arr)
    d2 = ((x - cx) ** 2 + (y - cy) ** 2) / (radius * radius)
    arr += np.exp(-d2)[:, :, None] * np.array(color, float)[None, None, :] * strength


def ring(arr, cx, cy, radius, width, color, strength):
    x, y = grid(arr)
    d = np.abs(np.sqrt((x - cx) ** 2 + (y - cy) ** 2) - radius)
    arr += np.clip(1.0 - d / width, 0, 1)[:, :, None] * np.array(color, float)[None, None, :] * strength


def orb(arr, cx, cy, r, edge, core, alpha=1.0):
    """A glossy ball, lit from the upper left like the installer's liquid."""
    x, y = grid(arr)
    dx, dy = (x - cx) / r, (y - cy) / r
    d2 = dx * dx + dy * dy
    inside = np.clip((1.0 - np.sqrt(d2)) * r * 1.2, 0, 1) * alpha
    nz = np.sqrt(np.clip(1.0 - d2, 0, 1))
    diffuse = np.clip(-dx * 0.45 - dy * 0.55 + nz * 0.704, 0, 1)
    half = np.clip(-dx * 0.244 - dy * 0.298 + nz * 0.923, 0, 1)
    spec = half ** 48
    rim = (1.0 - nz) ** 2
    colour = np.array(edge, float)[None, None, :] * (1 - nz[:, :, None]) + np.array(core, float)[None, None, :] * nz[:, :, None]
    lit = colour * (0.44 + diffuse * 0.62 + rim * 0.45)[:, :, None] + spec[:, :, None] * 255.0
    arr[:] = arr * (1 - inside[:, :, None]) + lit * inside[:, :, None]


def image(arr):
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), 'RGB')


def text(draw, x, y, s, fnt, fill, anchor='la', spacing=0.0):
    """spacing: extra pixels between letters, for small capitals."""
    if not spacing:
        draw.text((x * S, y * S), s, font=fnt, fill=fill, anchor=anchor)
        return
    width = sum(draw.textlength(c, font=fnt) for c in s) + spacing * S * (len(s) - 1)
    px = x * S - (width / 2 if anchor[0] == 'm' else width if anchor[0] == 'r' else 0)
    for c in s:
        draw.text((px, y * S), c, font=fnt, fill=fill, anchor='l' + anchor[1])
        px += draw.textlength(c, font=fnt) + spacing * S


def finish(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def liquid_background(balls, colour_time):
    """The installer's own liquid, drawn by preview/liquid.js over the gradient."""
    base = np.zeros((544, 960, 4), np.uint8)
    t = np.linspace(0.0, 1.0, 544)[:, None]
    base[:, :, :3] = (np.array(TOP, float)[None, :] * (1 - t) + np.array(BOTTOM, float)[None, :] * t)[:, None, :]
    base[:, :, 3] = 255
    base_path, out_path = os.path.join(OUT, 'base.rgba'), os.path.join(OUT, 'liquid.rgba')
    base.tofile(base_path)
    args = ['%d,%d,%d' % (x + 60, y + 22, r) for x, y, r in balls]
    subprocess.run(['node', os.path.join(HERE, 'render_liquid.js'), base_path, out_path, str(colour_time)] + args, check=True)
    frame = np.fromfile(out_path, np.uint8).reshape(544, 960, 4)[22:522, 60:900, :3]
    os.remove(base_path)
    os.remove(out_path)
    big = Image.fromarray(frame, 'RGB').resize((BG_W * S, BG_H * S), Image.BICUBIC)
    return np.array(big).astype(float)


# ---- the designs ----

def design_liquid():
    balls = [(690, 285, 92), (585, 385, 62), (480, 425, 46), (385, 432, 33), (305, 418, 23), (240, 392, 13),
             (772, 150, 16), (806, 404, 10)]
    arr = liquid_background(balls, 2.8)
    glow(arr, 421, 219, 190, ACCENT, 0.07)   # so the middle isn't empty while the gate is away
    img = image(arr)
    d = ImageDraw.Draw(img)
    text(d, 34, 104, 'Engram', title(62), TEXT)
    text(d, 38, 184, 'INSTALLER', body(14), ACCENT, spacing=5)
    text(d, 38, 222, 'Home screen tweaks', body(15), DIM)
    text(d, 38, 244, 'for the PS Vita', body(15), DIM)
    bg = finish(img, BG_W, BG_H)

    arr = gradient(GATE_W, GATE_H, (16, 20, 34), (30, 40, 78))
    orb(arr, 56, 62, 32, (20, 60, 235), (70, 190, 255))
    orb(arr, 96, 98, 9, (20, 60, 235), (70, 190, 255))
    img = image(arr)
    d = ImageDraw.Draw(img)
    text(d, 108, 30, 'Engram', title(34), TEXT)
    text(d, 110, 76, 'INSTALLER', body(11), ACCENT, spacing=3)
    return bg, finish(img, GATE_W, GATE_H)


FEATURES = ['Scroll Animations', 'Motion Depth', 'Icon Placement', 'App Page Animation', 'Fonts']


def design_features():
    arr = gradient(BG_W, BG_H)
    glow(arr, 422, 220, 250, ACCENT, 0.10)
    glow(arr, 120, 470, 220, VIOLET, 0.07)
    img = image(arr)
    d = ImageDraw.Draw(img)
    text(d, 34, 104, 'Engram', title(62), TEXT)
    text(d, 38, 184, 'INSTALLER', body(14), ACCENT, spacing=5)
    text(d, 38, 222, 'Home screen tweaks', body(15), DIM)
    text(d, 38, 244, 'for the PS Vita', body(15), DIM)
    text(d, 604, 104, 'WHAT IT ADDS', body(11), ACCENT, spacing=3)
    for i, name in enumerate(FEATURES):
        y = 138 + i * 34
        d.ellipse([604 * S, (y + 8) * S, 610 * S, (y + 14) * S], fill=ACCENT)
        text(d, 622, y, name, body(15.5), TEXT)
    d.line([34 * S, 352 * S, 806 * S, 352 * S], fill=(52, 60, 92), width=S)
    text(d, 420, 384, 'Install, update or remove the plugin. Then set it up in', body(15), DIM, anchor='ma')
    text(d, 420, 408, 'Settings > System > Engram', body(15), TEXT, anchor='ma')
    bg = finish(img, BG_W, BG_H)

    arr = gradient(GATE_W, GATE_H, (16, 20, 34), (30, 40, 78))
    glow(arr, 140, 60, 80, ACCENT, 0.16)
    ring(arr, 140, 60, 40, 2.6, ACCENT, 1.0)
    img = image(arr)
    d = ImageDraw.Draw(img)
    text(d, 141, 60, 'E', title(52), TEXT, anchor='mm')
    return bg, finish(img, GATE_W, GATE_H)


def design_wordmark():
    arr = gradient(BG_W, BG_H)
    glow(arr, 60, 440, 300, VIOLET, 0.16)
    glow(arr, 800, 120, 300, ACCENT, 0.14)
    for radius, strength in ((190, 0.10), (250, 0.075), (316, 0.05), (388, 0.03)):
        ring(arr, 421, 219, radius, 1.2, ACCENT, strength)
    img = image(arr)
    d = ImageDraw.Draw(img)
    text(d, 420, 316, 'Engram', title(88), TEXT, anchor='ma')
    text(d, 424, 424, 'INSTALLER', body(14), ACCENT, anchor='ma', spacing=9)
    text(d, 420, 458, 'Home screen tweaks for the PS Vita', body(14), DIM, anchor='ma')
    bg = finish(img, BG_W, BG_H)

    arr = gradient(GATE_W, GATE_H, (26, 60, 200), (120, 60, 220))
    glow(arr, 40, 20, 150, (120, 220, 255), 0.5)
    img = image(arr)
    d = ImageDraw.Draw(img)
    text(d, 140, 30, 'Engram', title(40), (255, 255, 255), anchor='ma')
    text(d, 142, 84, 'INSTALLER', body(11), (226, 236, 255), anchor='ma', spacing=5)
    return bg, finish(img, GATE_W, GATE_H)


def design_bubbles():
    arr = gradient(BG_W, BG_H)
    hues = [((20, 60, 235), (70, 190, 255)), ((92, 24, 228), (200, 130, 255)), ((200, 20, 140), (255, 138, 196)),
            ((0, 124, 160), (110, 246, 200)), ((30, 90, 230), (140, 210, 255))]
    # the icons as Icon Placement's sphere lays them out: big in the middle,
    # smaller and closer together towards the edge
    cx, cy, n = 421, 228, 0
    for row in range(-5, 6):
        for col in range(-8, 9):
            gx = col * 78 + (39 if row % 2 else 0)
            gy = row * 68
            dist = (gx * gx + gy * gy) ** 0.5
            if dist > 620:
                continue
            k = 1.0 / (1.0 + (dist / 430.0) ** 2)
            x, y, r = cx + gx * (0.55 + 0.45 * k), cy + gy * (0.55 + 0.45 * k), 30 * k
            if r < 6:
                continue
            edge, core = hues[(row * 7 + col * 3 + n) % len(hues)]
            orb(arr, x, y, r, edge, core, alpha=0.20 + 0.50 * k)
            n += 1
    # calm it down where the words go
    x, y = grid(arr)
    shade = np.clip((y - 285) / 110.0, 0, 1) * 0.90
    arr[:] = arr * (1 - shade[:, :, None]) + np.array((16, 19, 32), float)[None, None, :] * shade[:, :, None]
    arr *= 0.80
    img = image(arr)
    d = ImageDraw.Draw(img)
    text(d, 34, 368, 'Engram', title(64), TEXT)
    text(d, 268, 406, 'INSTALLER', body(14), ACCENT, spacing=5)
    text(d, 806, 388, 'Home screen tweaks', body(15), DIM, anchor='ra')
    text(d, 806, 410, 'for the PS Vita', body(15), DIM, anchor='ra')
    bg = finish(img, BG_W, BG_H)

    # the gate: only the writing, so the icons show through it. A soft dark
    # shadow keeps it readable over the bright ones in the middle.
    shadow = Image.new('L', (GATE_W * S, GATE_H * S), 0)
    d = ImageDraw.Draw(shadow)
    text(d, 140, 26, 'Engram', title(44), 255, anchor='ma')
    text(d, 142, 86, 'INSTALLER', body(12), 255, anchor='ma', spacing=6)
    shadow = shadow.filter(ImageFilter.GaussianBlur(7 * S)).point(lambda v: min(255, int(v * 2.6)))
    img = Image.new('RGBA', (GATE_W * S, GATE_H * S), (8, 10, 18, 0))
    img.putalpha(shadow.point(lambda v: int(v * 0.72)))
    d = ImageDraw.Draw(img)
    text(d, 140, 26, 'Engram', title(44), (255, 255, 255, 255), anchor='ma')
    text(d, 142, 86, 'INSTALLER', body(12), (214, 232, 255, 255), anchor='ma', spacing=6)
    return bg, finish(img, GATE_W, GATE_H)


DESIGNS = [('liquid', 'Liquid', design_liquid), ('features', 'Feature list', design_features),
           ('wordmark', 'Wordmark', design_wordmark), ('bubbles', 'Sphere of icons', design_bubbles)]

# ---- the mock-up: the designs under the system's own parts ----

BG_BOX = (58, 43, 902, 544)          # where the background shows on screen
GATE_OUTER = (334, 177, 626, 346)    # the gate's frame
GATE_INNER = (344, 187, 617, 337)    # its image
START = (415, 301, 545, 331)         # the Start button


def rounded_mask(size, box, radius):
    mask = Image.new('L', size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([box[0], box[1], box[2] - 1, box[3] - 1], radius=radius, fill=255)
    return mask


def bare(shot, bg):
    """The background with nothing over it, as it shows for a moment while the page opens or closes."""
    out = shot.copy()
    # the tops of the three buttons stand above the background in the screenshot: paint them out
    for y in range(30, BG_BOX[1]):
        out.paste(shot.getpixel((320, y)), (322, y, 640, y + 1))
    out.paste(bg.resize((BG_BOX[2] - BG_BOX[0], BG_BOX[3] - BG_BOX[1]), Image.LANCZOS), BG_BOX[:2])
    return out


def mockup(shot, bg, gate):
    out = bare(shot, bg)
    # the three buttons and the page curl: whatever in the screenshot isn't its plain background
    pixels = np.array(shot).astype(int)
    plain = pixels[:, 70:71, :]                      # a column with only the gradient in it
    differs = (np.abs(pixels - plain).sum(axis=2) > 36).astype(np.uint8) * 255
    system = Image.new('L', shot.size, 0)
    for box in ((325, 34, 636, 114), (812, 43, 902, 128)):
        system.paste(Image.fromarray(differs, 'L').crop(box), box[:2])
    system = system.filter(ImageFilter.MaxFilter(3))
    out.paste(shot, (0, 0), system)
    # the gate: its frame from the screenshot, the design's image inside it, the Start button on top
    frame = ImageChops.subtract(rounded_mask(shot.size, GATE_OUTER, 10), rounded_mask(shot.size, GATE_INNER, 5))
    out.paste(shot, (0, 0), frame)
    inner = gate.convert('RGBA').resize((GATE_INNER[2] - GATE_INNER[0], GATE_INNER[3] - GATE_INNER[1]), Image.LANCZOS)
    shown = ImageChops.multiply(inner.getchannel('A'), rounded_mask(inner.size, (0, 0) + inner.size, 4))
    out.paste(inner.convert('RGB'), GATE_INNER[:2], shown)
    out.paste(shot, (0, 0), rounded_mask(shot.size, START, 15))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    shot = Image.open(os.path.join(HERE, 'vita_screenshot.png')).convert('RGB')
    for key, name, make in DESIGNS:
        bg, gate = make()
        folder = os.path.join(OUT, key)
        os.makedirs(folder, exist_ok=True)
        bg.save(os.path.join(folder, 'bg.png'))
        gate.save(os.path.join(folder, 'startup.png'))
        mockup(shot, bg, gate).save(os.path.join(folder, 'mockup.png'))
        bare(shot, bg).save(os.path.join(folder, 'bare.png'))
        print('drew', name)


if __name__ == '__main__':
    main()
