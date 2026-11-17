#!/usr/bin/env python3
"""One texture per tongue from a Ludo model whose tongue-coloured parts were made red.

    python3 art_src/dye_textures.py art_src/glb/tent.glb assets/tex tent     (marquee: red and cream stripes)
    python3 art_src/dye_textures.py art_src/glb/cabin.glb assets/tex cabin strict   (log cabin: red flag; strict skips the reddish wood)

The red is recoloured to each tongue's colour, keeping its light and shade; everything else stays as it is.
Writes NAME_0.png .. NAME_5.png.
"""
import os, sys
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__))
from tongue_textures import glb_base_texture, TONGUES

def main():
    src, out, name = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(out, exist_ok=True)
    tex = glb_base_texture(src).resize((1024, 1024), Image.LANCZOS)
    a = np.asarray(tex).astype(np.float32) / 255
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx, mn = a.max(2), a.min(2)
    sat = (mx - mn) / np.maximum(mx, 1e-4)
    red = np.clip((sat - 0.35) / 0.2, 0, 1) * np.clip((r - np.maximum(g, b) * 1.25) / 0.15, 0, 1)   # how red a pixel is
    if 'strict' in sys.argv[4:]:   # only true red, not orange-brown wood
        red *= np.clip((0.4 - g / np.maximum(r, 1e-3)) / 0.15, 0, 1)
    shade = mx / max(float(np.percentile(mx[red > 0.9], 90)) if (red > 0.9).any() else 1.0, 1e-3)    # its light and shade
    for t, rgb in enumerate(TONGUES):
        c = np.array([(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255], np.float32) / 255
        dyed = np.clip(c[None, None, :] * shade[..., None] * 1.05, 0, 1)
        res = a * (1 - red[..., None]) + dyed * red[..., None]
        Image.fromarray((res * 255).astype(np.uint8)).save(os.path.join(out, f'{name}_{t}.png'))
    print(name, 'textures:', len(TONGUES), 'red share', round(float(red.mean()), 3))

if __name__ == '__main__':
    main()
