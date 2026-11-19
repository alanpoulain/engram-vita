# Puts the chosen designs' images into sce_sys: the bubble from
# design/make_icons.py, and the LiveArea's background and start gate from
# design/make_mockups.py (run those two first).
# Run from anywhere: python make_livearea.py
# The Vita wants 8-bit palette PNGs.
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ICON = 'sphere'      # a design of design/out/icons
DESIGN = 'bubbles'   # a folder of design/out


def with_alpha(img, colors=255, rounds=12):
    """A see-through image as a palette image whose entries each have their own
    alpha. Pillow's own quantizer leaves steps in a soft shadow, so the palette
    is fitted here (k-means over the image's colours)."""
    pixels = np.array(img).reshape(-1, 4)
    unique, inverse, counts = np.unique(pixels, axis=0, return_inverse=True, return_counts=True)
    inverse = inverse.reshape(-1)
    points = unique.astype(float)
    if len(points) <= colors:
        centres = points
    else:
        order = np.lexsort((points[:, 0] + points[:, 1] + points[:, 2], points[:, 3]))
        centres = points[order[np.linspace(0, len(order) - 1, colors).astype(int)]]
        for _ in range(rounds):
            nearest = ((points[:, None, :] - centres[None, :, :]) ** 2).sum(axis=2).argmin(axis=1)
            for i in range(colors):
                mine = nearest == i
                if mine.any():
                    centres[i] = np.average(points[mine], axis=0, weights=counts[mine])
    nearest = ((points[:, None, :] - centres[None, :, :]) ** 2).sum(axis=2).argmin(axis=1)
    centres = np.clip(np.rint(centres), 0, 255).astype(np.uint8)
    out = Image.fromarray(nearest[inverse].reshape(img.size[1], img.size[0]).astype(np.uint8), 'P')
    out.putpalette(centres[:, :3].tobytes())
    out.info['transparency'] = centres[:, 3].tobytes()
    return out


def save(img, rel):
    path = os.path.join(HERE, 'sce_sys', rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if img.mode == 'RGBA':
        with_alpha(img).save(path, optimize=True)
    else:
        # dithered, so the shading of the background's balls doesn't come out in bands
        rgb = img.convert('RGB')
        palette = rgb.quantize(colors=255, method=Image.MEDIANCUT)
        rgb.quantize(palette=palette, dither=Image.FLOYDSTEINBERG).save(path, optimize=True)
    print('wrote', rel, img.size)


def design(name, rel):
    """One of the chosen designs' images, as its script in design/ drew it."""
    path = os.path.join(HERE, 'design', 'out', name)
    if not os.path.exists(path):
        sys.exit('missing ' + path + ': run design/make_icons.py and design/make_mockups.py first')
    save(Image.open(path), rel)


design(os.path.join('icons', ICON + '.png'), 'icon0.png')
design(os.path.join(DESIGN, 'bg.png'), 'livearea/contents/bg.png')
design(os.path.join(DESIGN, 'startup.png'), 'livearea/contents/startup.png')
