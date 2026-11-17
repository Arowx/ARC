#!/usr/bin/env python3
"""Make one worker texture per tongue from the Ludo worker model.

    python3 art_src/tongue_textures.py art_src/glb/worker.glb assets/tex

The worker's tunic is the only pale, unsaturated area of its texture (the skin is peach, the cap, belt and boots
brown), so those pixels are recoloured to each tongue's colour, keeping their light and shade.
Writes worker_0.png .. worker_5.png (the game loads them onto copies of worker.arcm).
"""
import io, json, os, struct, sys
import numpy as np
from PIL import Image

TONGUES = [0xD8553F, 0x3F6FCF, 0x4E9E3E, 0xE3B52E, 0x9150BE, 0x2EA6A0]


def glb_base_texture(path):
    data = open(path, 'rb').read()
    off, js, binc = 12, None, None
    while off < len(data):
        ln, typ = struct.unpack_from('<II', data, off)
        chunk = data[off + 8: off + 8 + ln]
        if typ == 0x4E4F534A: js = json.loads(chunk)
        elif typ == 0x004E4942: binc = chunk
        off += 8 + ln
    mat = js['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['index']
    img = js['images'][js['textures'][mat]['source']]
    bv = js['bufferViews'][img['bufferView']]
    raw = binc[bv.get('byteOffset', 0): bv.get('byteOffset', 0) + bv['byteLength']]
    return Image.open(io.BytesIO(raw)).convert('RGB')


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    tex = glb_base_texture(src)
    tex = tex.resize((512, 512), Image.LANCZOS)
    a = np.asarray(tex).astype(np.float32) / 255
    mx, mn = a.max(2), a.min(2)
    sat = (mx - mn) / np.maximum(mx, 1e-4)
    gr = a[..., 1] / np.maximum(a[..., 0], 1e-4)               # linen is yellowish (green close to red); skin is orange
    tunic = (mx > 0.5) & (sat < 0.4) & (gr > 0.84)             # pale linen
    w = np.clip((0.42 - sat) / 0.08, 0, 1) * np.clip((mx - 0.45) / 0.1, 0, 1) * np.clip((gr - 0.82) / 0.05, 0, 1)
    w = (w * tunic)[..., None]
    lum = (0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2])[..., None]
    for i, rgb in enumerate(TONGUES):
        c = np.array([(rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255], np.float32) / 255
        dyed = np.clip(c * (lum / 0.78) ** 1.3, 0, 1)
        res = a * (1 - w) + dyed * w
        Image.fromarray((res * 255).astype(np.uint8)).save(os.path.join(out, f'worker_{i}.png'))
    print(f'{out}/worker_0..{len(TONGUES) - 1}.png  ({int(tunic.mean() * 100)}% of the texture is tunic)')


if __name__ == '__main__':
    main()
