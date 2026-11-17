#!/usr/bin/env python3
"""Make a colour-grading table (LUT) for arc_post.lut: an N*N x N PNG strip (red across each slice, green down,
blue by slice), loaded with arc_texture_load.

    python3 tools/make_lut.py teal_orange assets/grade.png          # a preset
    python3 tools/make_lut.py identity assets/lut_identity.png --size 32
    python3 tools/make_lut.py warm out.png --strength 0.5            # half way to the preset
    python3 tools/make_lut.py --list

Grading a screenshot: run this with `identity`, open a screenshot and the identity PNG together in an image editor,
grade them with the same adjustment layers (curves, hue, colour balance), then export the graded identity PNG.
Presets: identity, warm, cool, teal_orange, noir, sepia, bleach, night, gameboy, vivid.
"""
import argparse, math, struct, sys, zlib


def lum(r, g, b): return 0.2126 * r + 0.7152 * g + 0.0722 * b
def smooth(x): x = min(max(x, 0.0), 1.0); return x * x * (3 - 2 * x)
def mix(a, b, t): return a + (b - a) * t
def clamp(x): return min(max(x, 0.0), 1.0)


def preset(name):
    if name == 'identity': return lambda r, g, b: (r, g, b)
    if name == 'warm': return lambda r, g, b: (clamp(r * 1.06 + 0.02), clamp(g * 1.01), clamp(b * 0.88))
    if name == 'cool': return lambda r, g, b: (clamp(r * 0.9), clamp(g * 0.99 + 0.01), clamp(b * 1.08 + 0.03))
    if name == 'teal_orange':
        def f(r, g, b):
            l = smooth(lum(r, g, b))
            return clamp(mix(r, r * 0.8 + 0.25 * l, 0.7)), clamp(g * 0.95 + 0.02), clamp(mix(b, b * 0.85 + 0.2 * (1 - l), 0.7))
        return f
    if name == 'noir':
        def f(r, g, b):
            l = lum(r, g, b); l = smooth((l - 0.05) / 0.9); return l, l, l
        return f
    if name == 'sepia':
        def f(r, g, b):
            l = lum(r, g, b); return clamp(l * 1.07 + 0.04), clamp(l * 0.94 + 0.02), clamp(l * 0.74)
        return f
    if name == 'bleach':
        def f(r, g, b):
            l = lum(r, g, b); return tuple(clamp(mix(c, smooth(l), 0.55) * 1.04) for c in (r, g, b))
        return f
    if name == 'night':
        def f(r, g, b):
            l = lum(r, g, b); return clamp(l * 0.35), clamp(l * 0.55 + 0.02), clamp(l * 0.95 + 0.05)
        return f
    if name == 'gameboy':
        pal = [(0.06, 0.22, 0.06), (0.19, 0.38, 0.19), (0.55, 0.67, 0.06), (0.61, 0.74, 0.06)]
        def f(r, g, b):
            k = min(3, int(lum(r, g, b) * 4)); return pal[k]
        return f
    if name == 'vivid':
        def f(r, g, b):
            l = lum(r, g, b); return tuple(clamp(l + (c - l) * 1.35) for c in (r, g, b))
        return f
    sys.exit('unknown preset %s (try --list)' % name)


def write_png(path, w, h, rows):
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as o:
        o.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('preset', nargs='?'); ap.add_argument('out', nargs='?')
    ap.add_argument('--size', type=int, default=16, choices=(16, 32))
    ap.add_argument('--strength', type=float, default=1.0, help='0..1 mix from identity to the preset')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    if a.list: print('identity warm cool teal_orange noir sepia bleach night gameboy vivid'); return
    if not a.preset or not a.out: ap.error('give a preset and an output .png')
    f, n = preset(a.preset), a.size
    rows = []
    for gi in range(n):
        row = []
        for bi in range(n):
            for ri in range(n):
                r, g, b = ri / (n - 1), gi / (n - 1), bi / (n - 1)
                o = f(r, g, b); o = [mix(c, d, a.strength) for c, d in zip((r, g, b), o)]
                row += [int(round(clamp(c) * 255)) for c in o]
        rows.append(row)
    write_png(a.out, n * n, n, rows)
    print('wrote %s: %dx%d %s' % (a.out, n * n, n, a.preset))


if __name__ == '__main__':
    main()
