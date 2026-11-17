#!/usr/bin/env python3
"""Make see-through grime overlays from a dirt photo, for arc_draw_stamps (or arc_material.detail).

    python3 tools/grime.py SOURCE.png OUT_PREFIX [--count 8] [--size 512] [--crops .25,1] [--threshold .4,.55]
                           [--softness .12] [--cells 4] [--octaves 5] [--second .5] [--edge .35] [--darken .6] [--seed 1]

Writes OUT_PREFIX0.png .. OUT_PREFIX<count-1>.png. Each is ARC's arc_image_breakup() (see arc.h) with its own seed
and with crop and threshold spread evenly over the ranges given, so the eight differ in scale and coverage:
  colour: the crop of the source, grime (darker than the mean) times --darken, scratches kept pale;
  alpha:  dirt amount (distance from the mean brightness, over 2 standard deviations)
          x breakup mask (fbm value noise ramped at the threshold, OR-ed with a fainter second mask)
          x edge fade (to 0 at the circle that touches the edges), so overlapping stamps show no square.
The numbers match the C function to within a level (same hash, same noise). Needs Pillow and numpy.
"""
import argparse, sys

try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit("grime.py needs Pillow and numpy: pip install pillow numpy")

M32 = 0xFFFFFFFF


def noise_hash(x, y, seed):
    """ARC's noise hash (arc_core.c noise_hash), on uint32 arrays or ints."""
    x = np.asarray(x, dtype=np.int64) & M32
    y = np.asarray(y, dtype=np.int64) & M32
    h = ((x * 73856093) & M32) ^ ((y * 19349663) & M32) ^ ((seed * 83492791) & M32)
    h ^= h >> 13
    h = (h * 0x5bd1e995) & M32
    h ^= h >> 15
    return h


def value2(x, z, seed):
    fx, fz = np.floor(x), np.floor(z)
    ix, iz = fx.astype(np.int64), fz.astype(np.int64)
    u, v = (x - fx).astype(np.float32), (z - fz).astype(np.float32)
    u = u * u * (3 - 2 * u)
    v = v * v * (3 - 2 * v)
    k = np.float32(1.0 / 16777215.0)
    a = (noise_hash(ix, iz, seed) >> 8).astype(np.float32) * k
    b = (noise_hash(ix + 1, iz, seed) >> 8).astype(np.float32) * k
    c = (noise_hash(ix, iz + 1, seed) >> 8).astype(np.float32) * k
    d = (noise_hash(ix + 1, iz + 1, seed) >> 8).astype(np.float32) * k
    ab = a + (b - a) * u
    cd = c + (d - c) * u
    return ab + (cd - ab) * v


def fbm2(x, z, octaves, seed):
    octaves = 5 if octaves <= 0 else min(octaves, 16)
    s = np.zeros_like(x, dtype=np.float32)
    amp, total, f = 1.0, 0.0, 1.0
    for i in range(octaves):
        s += np.float32(amp) * value2((x * f).astype(np.float32), (z * f).astype(np.float32), (seed + i * 1013904223) & M32)
        total += amp
        amp *= 0.5
        f *= 2.0
    return s / np.float32(total)


def breakup(src, size=512, crop=1.0, threshold=0.45, softness=0.12, cells=4.0, octaves=5, second=0.0, edge=0.35,
            darken=0.6, seed=0):
    """src: HxWx4 uint8. Returns size x size x 4 uint8, as arc_image_breakup."""
    h, w = src.shape[:2]
    seed &= M32
    crop = min(crop, 1.0) if crop > 0 else 1.0
    cs = max(1, int(min(w, h) * crop + 0.5))
    cx0 = int(noise_hash(17, 29, seed) % (w - cs + 1)) if w > cs else 0
    cy0 = int(noise_hash(31, 7, seed) % (h - cs + 1)) if h > cs else 0
    sub = src[cy0:cy0 + cs, cx0:cx0 + cs, :3].astype(np.float32)
    sc = np.float32(cs / size)
    t = (np.arange(size, dtype=np.float32) + np.float32(0.5)) * sc - np.float32(0.5)
    t = np.clip(t, 0, cs - 1)
    i0 = t.astype(np.int64)
    i1 = np.minimum(i0 + 1, cs - 1)
    f = (t - i0).astype(np.float32)
    fx, fy = f[None, :, None], f[:, None, None]
    r0 = sub[i0][:, i0] + (sub[i0][:, i1] - sub[i0][:, i0]) * fx
    r1 = sub[i1][:, i0] + (sub[i1][:, i1] - sub[i1][:, i0]) * fx
    col = r0 + (r1 - r0) * fy
    lum = (np.float32(0.3) * col[..., 0] + np.float32(0.59) * col[..., 1] + np.float32(0.11) * col[..., 2]).astype(np.float64)
    out = np.empty((size, size, 4), dtype=np.uint8)
    rgb = np.clip(col + 0.5, 0, 255).astype(np.uint8).astype(np.float32)
    mean, sd = lum.mean(), lum.std()
    amount = np.clip(np.abs(lum - mean) / (2 * sd), 0, 1) if sd > 0 else np.zeros_like(lum)
    dark = (lum < mean)[..., None]
    rgb = np.where(dark, np.floor(rgb * np.float32(darken) + np.float32(0.5)), rgb)
    out[..., :3] = rgb.astype(np.uint8)
    g = (np.arange(size, dtype=np.float32) + np.float32(0.5)) / np.float32(size) * np.float32(cells)
    nx, ny = np.meshgrid(g, g)
    m = np.clip((fbm2(nx, ny, octaves, (seed * 7 + 1) & M32) - threshold) / softness, 0, 1)
    if second > 0:
        m = np.maximum(m, min(second, 1.0) * np.clip((fbm2(nx, ny, octaves, (seed * 7 + 2) & M32) - threshold) / softness, 0, 1))
    fade = np.ones_like(m)
    if edge > 0:
        half = size * 0.5
        c = (np.arange(size, dtype=np.float32) + np.float32(0.5) - half) / half
        dx, dy = np.meshgrid(c, c)
        r = np.sqrt(dx * dx + dy * dy)
        e0 = 1 - min(edge, 1.0)
        tt = np.clip((r - e0) / (1 - e0), 0, 1)
        fade = 1 - tt * tt * (3 - 2 * tt)
    out[..., 3] = np.floor(amount * m * fade * 255.0 + 0.5).astype(np.uint8)
    return out


def pair(text):
    v = [float(x) for x in text.split(',')]
    return (v[0], v[-1])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('source')
    ap.add_argument('out_prefix')
    ap.add_argument('--count', type=int, default=8)
    ap.add_argument('--size', type=int, default=512)
    ap.add_argument('--crops', type=pair, default=(0.25, 1.0), help='crop range, part of the shorter side (min,max)')
    ap.add_argument('--sizes', type=pair, default=None, help='crop range in source pixels (min,max), instead of --crops')
    ap.add_argument('--threshold', type=pair, default=(0.4, 0.55))
    ap.add_argument('--softness', type=float, default=0.12)
    ap.add_argument('--cells', type=float, default=4.0, help='mask features across each overlay')
    ap.add_argument('--octaves', type=int, default=5)
    ap.add_argument('--second', type=float, default=0.5, help='strength of the second mask (0 = none)')
    ap.add_argument('--edge', type=float, default=0.35, help='edge fade, part of the radius (negative = none)')
    ap.add_argument('--darken', type=float, default=0.6)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    src = np.asarray(Image.open(a.source).convert('RGBA'))
    short = min(src.shape[0], src.shape[1])
    crops = (a.sizes[0] / short, a.sizes[1] / short) if a.sizes else a.crops
    n = max(1, a.count)
    for i in range(n):
        k = i / (n - 1) if n > 1 else 0.0
        crop = min(1.0, crops[0] + (crops[1] - crops[0]) * k)
        thr = a.threshold[0] + (a.threshold[1] - a.threshold[0]) * ((i * 5 % n) / (n - 1) if n > 1 else 0.0)
        img = breakup(src, a.size, crop, thr, a.softness, a.cells, a.octaves, a.second, a.edge, a.darken, a.seed + i)
        path = f'{a.out_prefix}{i}.png'
        Image.fromarray(img, 'RGBA').save(path)
        cover = (img[..., 3] > 32).mean() * 100
        print(f'{path}: crop {crop:.2f} ({int(short * crop + 0.5)} px), threshold {thr:.2f}, {cover:.0f}% covered')


if __name__ == '__main__':
    main()
