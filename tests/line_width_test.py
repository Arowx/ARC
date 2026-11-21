#!/usr/bin/env python3
"""ARC 0.10.11: thick, glowing lines measured in pixels. The demo's `line_test` draws a magenta line 4 pixels wide with
glow 2.5 across the arena; this counts how many pixels tall it is where it crosses the middle column, and checks that
its core is brighter than its colour (the glow).

    python3 tests/line_width_test.py build/demo_headless      (no packages needed)
"""
import os, struct, subprocess, sys, tempfile, zlib


def read_png(path):
    """ARC's screenshots: 8-bit RGB or RGBA, not interlaced. Returns (w, h, pixel(x, y) -> (r, g, b))."""
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n': raise OSError('not a PNG: ' + path)
    pos, idat, w = 8, b'', 0
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8]); body = data[pos + 8:pos + 8 + n]; pos += 12 + n
        if kind == b'IHDR': w, h, depth, ctype = struct.unpack('>IIBB', body[:10])
        elif kind == b'IDAT': idat += body
        elif kind == b'IEND': break
    if depth != 8 or ctype not in (2, 6): raise OSError('unexpected PNG format')
    bpp = 3 if ctype == 2 else 4; stride = w * bpp; raw = zlib.decompress(idat); rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]; line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line); prev = line
    return w, h, lambda x, y: tuple(rows[y][x * bpp:x * bpp + 3])

exe = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp(prefix='arc_lines_')
png = os.path.join(d, 'thick.png')
r = subprocess.run([exe, '--headless', '--size', '640x360', '--cmd', 'hud off; set line_test 1; step 2; shot ' + png + '; quit'],
                   cwd=os.path.dirname(exe), capture_output=True, text=True, timeout=120)
bad = []
try:
    w, h, px = read_png(png)
    col = [px(320, y) for y in range(100, 190)]
    line = [p for p in col if p[0] > 150 and p[2] > 120]              # magenta-ish pixels (the floor is dark blue)
    core = [p for p in col if p[0] > 240 and p[1] > 200 and p[2] > 240]  # 0xFF60C0 * 2.5 saturates to nearly white
    print('ARC note line column: %d line pixels, %d glowing core pixels' % (len(line), len(core)))
    if not 3 <= len(line) <= 7: bad.append('the 4-pixel line is %d pixels tall' % len(line))
    if len(core) < 2: bad.append('no glowing core')
    soft = [p for p in col if 60 < p[0] < 240 and p[2] > 60]          # the soft edge: in between floor and line
    if not soft: bad.append('no soft edge')
except OSError as e:
    bad.append('no picture: %s %s' % (e, r.stdout[-400:]))
print('ARC ok line width' if not bad else 'ARC error line width: ' + '; '.join(bad))
sys.exit(1 if bad else 0)
