#!/usr/bin/env python3
"""Bake a TrueType/OpenType font into ARC bitmap fonts (.arcf), one file per pixel size.

    python3 tools/font_bake.py FONT.ttf OUT_DIR --sizes 8,16 [--name display] [--chars "extra chars"]
                               [--codepoints 0x2019,0x2014] [--smooth] [--sdf [--spread 6]]

Writes OUT_DIR/<name>_<size>.arcf. The game never parses TTF; it loads these with arc_font_load().

Glyphs are rasterised the way stb_truetype (and so raylib's LoadFontEx) does it: the size is the
pixel height from ascender to descender, the glyph box is floor/ceil of the scaled outline box, and
each pixel's alpha is the exact area the outline covers. Glyph offsets match raylib's GlyphInfo, so a
game moving from raylib draws the same pixels. Pixel fonts (straight edges only) come out identical;
curved outlines may differ by a level or two of alpha on edge pixels.

--sdf (ARC 0.10.1) writes a signed distance field instead of coverage: one bake (48 px is plenty) draws sharp at
any size with arc_font_draw_ex(f, x, y, px / 48.0f, ...). Each texel holds 128 + distance * 127 / spread, the
distance in atlas pixels to the outline (positive inside), out to --spread pixels (default 6) beyond the glyph box.
Metrics are those of the baked size. Writes OUT_DIR/<name>_sdf<size>.arcf.

Needs fontTools (pip install fonttools) — a tool-time dependency only; the engine has none.

.arcf layout (little-endian):
  "ARCF" u32 version=1
  u16 size, u16 line_height, i16 ascent, i16 descent, u16 glyph_count, u16 atlas_w, u16 atlas_h,
  u16 flags (1 = smooth, 2 = distance field; bits 8..15: the field's spread in pixels)
  glyph_count x { u32 codepoint, u16 x, y, w, h (atlas rect), i16 offset_x, offset_y, advance }   sorted by codepoint
  atlas_w * atlas_h bytes of alpha
"""
import argparse, math, os, struct, sys


def f32(v):
    """round to 32-bit float, as stb_truetype computes in float"""
    return struct.unpack('<f', struct.pack('<f', v))[0]

try:
    from fontTools.ttLib import TTFont
    from fontTools.pens.basePen import BasePen
except ImportError:
    sys.exit("font_bake.py needs fontTools:  pip install fonttools")

ASCII = list(range(32, 127))
FLAT = 0.05   # curve flattening tolerance in pixels


class Raster:
    """Exact-area coverage accumulation (signed area per cell, then a running sum along each row)."""
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.a = [0.0] * (w * h + w + 4)

    def line(self, x0, y0, x1, y1):
        if abs(y0 - y1) <= 1e-9:
            return
        # the box is floor/ceil of the outline, so points sit inside it up to float error
        x0 = min(max(x0, 0.0), float(self.w)); x1 = min(max(x1, 0.0), float(self.w))
        if y0 < y1:
            d = 1.0
        else:
            d = -1.0; x0, y0, x1, y1 = x1, y1, x0, y0
        dxdy = (x1 - x0) / (y1 - y0)
        x = x0
        if y0 < 0:
            x -= y0 * dxdy
        a, w = self.a, self.w
        for y in range(max(0, int(math.floor(y0))), min(self.h, int(math.ceil(y1)))):
            ls = y * w
            dy = min(y + 1.0, y1) - max(float(y), y0)
            xn = x + dxdy * dy
            dd = dy * d
            xa, xb = (x, xn) if x < xn else (xn, x)
            xaf = math.floor(xa); xai = int(xaf)
            xbc = math.ceil(xb); xbi = int(xbc)
            if xbi <= xai + 1:
                xm = 0.5 * (x + xn) - xaf
                a[ls + xai] += dd - dd * xm
                a[ls + xai + 1] += dd * xm
            else:
                s = 1.0 / (xb - xa)
                x0f = xa - xaf
                a0 = 0.5 * s * (1.0 - x0f) * (1.0 - x0f)
                x1f = xb - xbc + 1.0
                am = 0.5 * s * x1f * x1f
                a[ls + xai] += dd * a0
                if xbi == xai + 2:
                    a[ls + xai + 1] += dd * (1.0 - a0 - am)
                else:
                    a1 = s * (1.5 - x0f)
                    a[ls + xai + 1] += dd * (a1 - a0)
                    for xi in range(xai + 2, xbi - 1):
                        a[ls + xi] += dd * s
                    a2 = a1 + (xbi - xai - 3) * s
                    a[ls + xbi - 1] += dd * (1.0 - a2 - am)
                a[ls + xbi] += dd * am
            x = xn

    def alpha(self):
        # one running sum over the whole buffer: a contribution written one past a row's end is
        # carried into the next row, where it cancels (each closed contour sums to zero per row)
        out = bytearray(self.w * self.h); acc = 0.0
        for i in range(self.w * self.h):
            acc += self.a[i]
            v = int(min(1.0, abs(acc)) * 255.0 + 0.5)
            out[i] = 255 if v > 255 else v
        return out


class SegRaster(Raster):
    """Coverage as Raster, and keeps the segments for the distance field."""
    def __init__(self, w, h):
        super().__init__(w, h); self.segs = []
    def line(self, x0, y0, x1, y1):
        self.segs.append((x0, y0, x1, y1)); super().line(x0, y0, x1, y1)


def distance_field(r, spread):
    """Signed distance (texel centres to the nearest segment), clamped to spread, as bytes."""
    w, h, cell = r.w, r.h, max(1.0, float(spread))
    gw, gh = int(w / cell) + 2, int(h / cell) + 2
    grid = [[] for _ in range(gw * gh)]
    for sg in r.segs:   # each segment in every bucket its box (grown by spread) touches
        x0, y0, x1, y1 = sg
        bx0, bx1 = int(max(0, (min(x0, x1) - spread) // cell)), int(min(gw - 1, (max(x0, x1) + spread) // cell))
        by0, by1 = int(max(0, (min(y0, y1) - spread) // cell)), int(min(gh - 1, (max(y0, y1) + spread) // cell))
        for by in range(by0, by1 + 1):
            for bx in range(bx0, bx1 + 1):
                grid[by * gw + bx].append(sg)
    cov = r.alpha(); out = bytearray(w * h)
    for y in range(h):
        py = y + 0.5; row = int(py // cell) * gw
        for x in range(w):
            px = x + 0.5; best = spread * spread
            for (x0, y0, x1, y1) in grid[row + int(px // cell)]:
                dx, dy = x1 - x0, y1 - y0; L = dx * dx + dy * dy
                t = 0.0 if L <= 1e-12 else max(0.0, min(1.0, ((px - x0) * dx + (py - y0) * dy) / L))
                ex, ey = px - x0 - t * dx, py - y0 - t * dy; d2 = ex * ex + ey * ey
                if d2 < best: best = d2
            d = math.sqrt(best)
            if cov[y * w + x] < 128: d = -d
            v = int(round(128 + d * 127.0 / spread))
            out[y * w + x] = 0 if v < 0 else 255 if v > 255 else v
    return out


def bake_sdf(font, size, cps, spread):
    """As bake(), but each glyph is a distance field with `spread` pixels of room around it."""
    hhea = font['hhea']; cmap = font.getBestCmap() or {}
    glyphset = font.getGlyphSet(); hmtx = font['hmtx']; order = font.getGlyphOrder()
    asc, desc = hhea.ascent, hhea.descent
    scale = f32(size / f32(asc - desc)); asc_px = int(f32(asc * scale))
    glyphs = []
    for cp in cps:
        gname = cmap.get(cp, order[0]); adv = int(f32(hmtx[gname][0] * scale))
        box = glyph_box(font, gname, glyphset) if cp != 32 else None
        if not box:
            glyphs.append(dict(cp=cp, w=0, h=0, ox=0, oy=asc_px, adv=adv, px=b'')); continue
        x0, y0, x1, y1 = box
        ix0, iy0 = math.floor(f32(x0 * scale)) - spread, math.floor(f32(-y1 * scale)) - spread
        ix1, iy1 = math.ceil(f32(x1 * scale)) + spread, math.ceil(f32(-y0 * scale)) + spread
        w, h = ix1 - ix0, iy1 - iy0
        r = SegRaster(w, h)
        glyphset[gname].draw(OutlinePen(glyphset, scale, ix0, iy0, r))
        glyphs.append(dict(cp=cp, w=w, h=h, ox=ix0, oy=iy0 + asc_px, adv=adv, px=distance_field(r, spread)))
    return glyphs, asc_px, int(desc * scale)


class OutlinePen(BasePen):
    """Flattens the outline into pixel-space line segments (y down, origin at the glyph box)."""
    def __init__(self, glyphset, scale, ox, oy, raster):
        super().__init__(glyphset)
        self.s, self.ox, self.oy, self.r = scale, ox, oy, raster
        self.start = self.cur = None

    def _p(self, pt):
        return (pt[0] * self.s - self.ox, -pt[1] * self.s - self.oy)

    def _moveTo(self, pt):
        self.start = self.cur = self._p(pt)

    def _lineTo(self, pt):
        p = self._p(pt); self.r.line(self.cur[0], self.cur[1], p[0], p[1]); self.cur = p

    def _qCurveToOne(self, c, pt):
        p0, p1, p2 = self.cur, self._p(c), self._p(pt)
        dist = math.hypot(p0[0] - 2 * p1[0] + p2[0], p0[1] - 2 * p1[1] + p2[1])
        n = max(1, int(math.ceil(math.sqrt(dist / (8 * FLAT)))))
        prev = p0
        for i in range(1, n + 1):
            t = i / n; u = 1 - t
            q = (u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0], u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1])
            self.r.line(prev[0], prev[1], q[0], q[1]); prev = q
        self.cur = p2

    def _curveToOne(self, c1, c2, pt):
        p0, p1, p2, p3 = self.cur, self._p(c1), self._p(c2), self._p(pt)
        n = 16
        prev = p0
        for i in range(1, n + 1):
            t = i / n; u = 1 - t
            q = tuple(u * u * u * p0[k] + 3 * u * u * t * p1[k] + 3 * u * t * t * p2[k] + t * t * t * p3[k] for k in range(2))
            self.r.line(prev[0], prev[1], q[0], q[1]); prev = q
        self.cur = p3

    def _closePath(self):
        if self.cur and self.start and self.cur != self.start:
            self.r.line(self.cur[0], self.cur[1], self.start[0], self.start[1])
        self.cur = self.start

    _endPath = _closePath


def glyph_box(font, gname, glyphset):
    """The outline's bounding box in font units (as stored in glyf, or computed for CFF)."""
    if 'glyf' in font:
        g = font['glyf'][gname]
        if g.numberOfContours == 0:
            return None
        if not hasattr(g, 'xMin'):
            g.recalcBounds(font['glyf'])
        return g.xMin, g.yMin, g.xMax, g.yMax
    from fontTools.pens.boundsPen import BoundsPen
    bp = BoundsPen(glyphset); glyphset[gname].draw(bp)
    return bp.bounds


def bake(font, size, cps):
    hhea = font['hhea']; cmap = font.getBestCmap() or {}
    glyphset = font.getGlyphSet(); hmtx = font['hmtx']
    order = font.getGlyphOrder()
    asc, desc = hhea.ascent, hhea.descent
    scale = f32(size / f32(asc - desc))            # stbtt_ScaleForPixelHeight, in float
    asc_px = int(f32(asc * scale))
    glyphs = []
    for cp in cps:
        gname = cmap.get(cp, order[0])            # missing -> .notdef, as stb draws it
        adv = int(f32(hmtx[gname][0] * scale))
        if cp == 32:                              # raylib makes a blank advance x size cell for space
            glyphs.append(dict(cp=cp, w=adv, h=size, ox=0, oy=0, adv=adv, px=bytes(adv * size)))
            continue
        box = glyph_box(font, gname, glyphset)
        if not box:
            glyphs.append(dict(cp=cp, w=0, h=0, ox=0, oy=asc_px, adv=adv, px=b''))
            continue
        x0, y0, x1, y1 = box
        ix0, iy0 = math.floor(f32(x0 * scale)), math.floor(f32(-y1 * scale))
        ix1, iy1 = math.ceil(f32(x1 * scale)), math.ceil(f32(-y0 * scale))
        w, h = ix1 - ix0, iy1 - iy0
        r = Raster(w, h)
        glyphset[gname].draw(OutlinePen(glyphset, scale, ix0, iy0, r))
        glyphs.append(dict(cp=cp, w=w, h=h, ox=ix0, oy=iy0 + asc_px, adv=adv, px=r.alpha()))
    return glyphs, asc_px, int(desc * scale)


def pack(glyphs, pad=2):
    """Shelf packing into a power-of-two-wide atlas."""
    area = sum((g['w'] + pad) * (g['h'] + pad) for g in glyphs)
    aw = 64
    while aw * aw < area * 1.3:
        aw *= 2
    x = y = pad; row = 0
    for g in sorted(glyphs, key=lambda g: -g['h']):
        if x + g['w'] + pad > aw:
            x = pad; y += row + pad; row = 0
        g['x'], g['y'] = x, y
        x += g['w'] + pad; row = max(row, g['h'])
    ah = y + row + pad
    atlas = bytearray(aw * ah)
    for g in glyphs:
        for r in range(g['h']):
            atlas[(g['y'] + r) * aw + g['x']:(g['y'] + r) * aw + g['x'] + g['w']] = g['px'][r * g['w']:(r + 1) * g['w']]
    return aw, ah, atlas


def write_arcf(path, size, glyphs, asc, desc, smooth, sdf_spread=0):
    aw, ah, atlas = pack(glyphs, pad=2 if not sdf_spread else 1)
    glyphs = sorted(glyphs, key=lambda g: g['cp'])
    with open(path, 'wb') as f:
        f.write(b'ARCF' + struct.pack('<I', 1))
        flags = (1 if smooth else 0) | ((2 | (sdf_spread << 8)) if sdf_spread else 0)
        f.write(struct.pack('<HHhhHHHH', size, size, asc, desc, len(glyphs), aw, ah, flags))
        for g in glyphs:
            f.write(struct.pack('<IHHHHhhh', g['cp'], g['x'], g['y'], g['w'], g['h'], g['ox'], g['oy'], g['adv']))
        f.write(atlas)
    return aw, ah


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('font'); ap.add_argument('out_dir')
    ap.add_argument('--sizes', required=True, help='comma-separated pixel sizes, e.g. 8,16')
    ap.add_argument('--name', help='output name prefix (default: font file name)')
    ap.add_argument('--chars', default='', help='extra characters to include besides ASCII 32..126')
    ap.add_argument('--codepoints', default='', help='extra codepoints, e.g. 0x2019,0x2014')
    ap.add_argument('--smooth', action='store_true', help='mark the font for bilinear filtering (default sharp)')
    ap.add_argument('--sdf', action='store_true', help='bake a distance field: one file draws sharp at any size (ARC 0.10.1)')
    ap.add_argument('--spread', type=int, default=6, help='--sdf: how far the field reaches beyond the outline, in pixels (default 6)')
    a = ap.parse_args()
    cps = list(ASCII)
    for ch in a.chars:
        if ord(ch) not in cps: cps.append(ord(ch))
    for c in filter(None, a.codepoints.split(',')):
        v = int(c, 0)
        if v not in cps: cps.append(v)
    font = TTFont(a.font)
    name = a.name or os.path.splitext(os.path.basename(a.font))[0]
    os.makedirs(a.out_dir, exist_ok=True)
    for s in [int(v) for v in a.sizes.split(',')]:
        if a.sdf:
            sp = max(1, min(255, a.spread))
            glyphs, asc, desc = bake_sdf(font, s, cps, sp)
            out = os.path.join(a.out_dir, f'{name}_sdf{s}.arcf')
            aw, ah = write_arcf(out, s, glyphs, asc, desc, True, sp)
        else:
            glyphs, asc, desc = bake(font, s, cps)
            out = os.path.join(a.out_dir, f'{name}_{s}.arcf')
            aw, ah = write_arcf(out, s, glyphs, asc, desc, a.smooth)
        print(f'{out}: {len(glyphs)} glyphs, atlas {aw}x{ah}')


if __name__ == '__main__':
    main()
