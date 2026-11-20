#!/usr/bin/env python3
"""ARC 0.10.11: thick, glowing lines measured in pixels. The demo's `line_test` draws a magenta line 4 pixels wide with
glow 2.5 across the arena; this counts how many pixels tall it is where it crosses the middle column, and checks that
its core is brighter than its colour (the glow).

    python3 tests/line_width_test.py build/demo_headless      (Pillow)
"""
import os, subprocess, sys, tempfile
from PIL import Image

exe = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp(prefix='arc_lines_')
png = os.path.join(d, 'thick.png')
r = subprocess.run([exe, '--headless', '--size', '640x360', '--cmd', 'hud off; set line_test 1; step 2; shot ' + png + '; quit'],
                   cwd=os.path.dirname(exe), capture_output=True, text=True, timeout=120)
bad = []
try:
    im = Image.open(png).convert('RGB')
    col = [im.getpixel((320, y)) for y in range(100, 190)]
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
