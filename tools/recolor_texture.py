#!/usr/bin/env python3
"""Recolour part of a texture into several versions: one per team, tongue, faction...

    python tools/recolor_texture.py tent.glb assets/tex tent --select red --colors D8553F,3F6FCF,4E9E3E
    python tools/recolor_texture.py cabin.glb assets/tex cabin --select red --strict          (skip orange-brown wood)
    python tools/recolor_texture.py worker.glb assets/tex worker --select pale --size 512    (pale linen: tunics)
    python tools/recolor_texture.py flag.png assets/tex flag --select hue:200-260            (any hue range, in degrees)

Input: a .glb/.gltf model (its first material's base-colour texture) or an image file.
Writes OUT/NAME_0.png, NAME_1.png ... one per colour, the same size as the input (or --size N square).
Load them onto copies of the model's mesh with arc_mesh_set_texture.

What gets recoloured (--select):
  red          strongly red pixels (paint the parts to recolour red when you make the model). --strict also requires
               green/red < 0.4, so orange-brown wood stays as it is.
  pale         pale, unsaturated, slightly yellow pixels (linen, canvas, white cloth).
  hue:A-B      saturated pixels whose hue is between A and B degrees (0 = red, 120 = green, 240 = blue).
The selection is soft at its edges, and the recoloured pixels keep their light and shade.
--preview writes NAME_mask.png too (white = recoloured), to check the selection.

Needs Python 3 with numpy and Pillow. (From the Babel project's dye_textures.py and tongue_textures.py.)
"""
import argparse, io, json, os, struct, sys
import numpy as np
from PIL import Image

DEFAULT_COLORS = 'D8553F,3F6FCF,4E9E3E,E3B52E,9150BE,2EA6A0'   # red, blue, green, gold, purple, teal


def gltf_base_texture(path):
    """The first material's base-colour image from a .glb (or a .gltf with an embedded or neighbouring image)."""
    data = open(path, 'rb').read()
    js, binc = None, None
    if data[:4] == b'glTF':
        off = 12
        while off < len(data):
            ln, typ = struct.unpack_from('<II', data, off)
            chunk = data[off + 8: off + 8 + ln]
            if typ == 0x4E4F534A: js = json.loads(chunk)
            elif typ == 0x004E4942: binc = chunk
            off += 8 + ln
    else:
        js = json.loads(data)
    for m in js.get('materials', []):
        t = m.get('pbrMetallicRoughness', {}).get('baseColorTexture')
        if t is None: continue
        img = js['images'][js['textures'][t['index']]['source']]
        if 'bufferView' in img:
            bv = js['bufferViews'][img['bufferView']]
            if binc is None:
                import base64
                uri = js['buffers'][bv['buffer']]['uri']
                binc = base64.b64decode(uri.split(',', 1)[1]) if uri.startswith('data:') else open(os.path.join(os.path.dirname(path), uri), 'rb').read()
            raw = binc[bv.get('byteOffset', 0): bv.get('byteOffset', 0) + bv['byteLength']]
            return Image.open(io.BytesIO(raw)).convert('RGB')
        uri = img['uri']
        if uri.startswith('data:'):
            import base64
            return Image.open(io.BytesIO(base64.b64decode(uri.split(',', 1)[1]))).convert('RGB')
        return Image.open(os.path.join(os.path.dirname(path), uri)).convert('RGB')
    sys.exit('no base-colour texture in ' + path)


def smooth(x, lo, hi):
    return np.clip((x - lo) / max(hi - lo, 1e-6), 0, 1)


def select(a, how, strict):
    """0..1 weight per pixel: how much of it is recoloured"""
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mx, mn = a.max(2), a.min(2)
    sat = (mx - mn) / np.maximum(mx, 1e-4)
    if how == 'red':
        w = smooth(sat, 0.35, 0.55) * smooth(r - np.maximum(g, b) * 1.25, 0, 0.15)
        if strict: w *= np.clip((0.4 - g / np.maximum(r, 1e-3)) / 0.15, 0, 1)
        return w
    if how == 'pale':
        gr = g / np.maximum(r, 1e-4)                      # linen is yellowish: green close to red (skin is orange)
        return np.clip((0.42 - sat) / 0.08, 0, 1) * smooth(mx, 0.45, 0.55) * smooth(gr, 0.82, 0.87)
    if how.startswith('hue:'):
        lo, hi = (float(v) for v in how[4:].split('-'))
        d = mx - mn
        h = np.zeros_like(mx)
        m = d > 1e-5
        rr = (mx == r) & m; gg = (mx == g) & m & ~rr; bb = m & ~rr & ~gg
        h[rr] = (60 * ((g - b)[rr] / d[rr])) % 360
        h[gg] = 60 * ((b - r)[gg] / d[gg]) + 120
        h[bb] = 60 * ((r - g)[bb] / d[bb]) + 240
        inside = ((h >= lo) & (h <= hi)) if lo <= hi else ((h >= lo) | (h <= hi))   # 330-30 wraps through red
        return inside.astype(np.float32) * smooth(sat, 0.25, 0.4) * smooth(mx, 0.1, 0.2)
    sys.exit('unknown --select ' + how)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', help='.glb/.gltf model or an image')
    ap.add_argument('out', help='output folder')
    ap.add_argument('name', help='output name: NAME_0.png ...')
    ap.add_argument('--select', default='red', help='red | pale | hue:A-B (default red)')
    ap.add_argument('--strict', action='store_true', help='red: only true red, not orange-brown')
    ap.add_argument('--colors', default=DEFAULT_COLORS, help='comma-separated RRGGBB (default: 6 team colours)')
    ap.add_argument('--size', type=int, default=0, help='resize to N x N first (0 = keep)')
    ap.add_argument('--preview', action='store_true', help='also write NAME_mask.png')
    a = ap.parse_args()
    ext = os.path.splitext(a.src)[1].lower()
    tex = gltf_base_texture(a.src) if ext in ('.glb', '.gltf') else Image.open(a.src).convert('RGB')
    if a.size > 0: tex = tex.resize((a.size, a.size), Image.LANCZOS)
    os.makedirs(a.out, exist_ok=True)
    px = np.asarray(tex).astype(np.float32) / 255
    w = select(px, a.select, a.strict)
    if a.preview: Image.fromarray((w * 255).astype(np.uint8)).save(os.path.join(a.out, a.name + '_mask.png'))
    mx = px.max(2)
    # light and shade of the selected area, relative to its bright parts, carried over to the new colour
    core = w > 0.9
    ref = float(np.percentile(mx[core], 90)) if core.any() else 1.0
    shade = mx / max(ref, 1e-3)
    colors = [c.strip().lstrip('#') for c in a.colors.split(',') if c.strip()]
    for i, hexc in enumerate(colors):
        c = np.array([int(hexc[k:k + 2], 16) for k in (0, 2, 4)], np.float32) / 255
        dyed = np.clip(c[None, None, :] * shade[..., None] * 1.05, 0, 1)
        res = px * (1 - w[..., None]) + dyed * w[..., None]
        Image.fromarray((res * 255 + 0.5).astype(np.uint8)).save(os.path.join(a.out, '%s_%d.png' % (a.name, i)))
    print('%s: %d textures %dx%d, %.1f%% of the texture recoloured' % (a.name, len(colors), tex.width, tex.height, 100 * float(w.mean())))


if __name__ == '__main__':
    main()
