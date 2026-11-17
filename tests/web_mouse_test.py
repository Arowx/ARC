#!/usr/bin/env python3
"""ARC 0.8.5 web mouse test (HOSTLESS's request, done-notes/hostless-web-mouseup.md): an arc_ui_button in the web build
must fire when clicked with real mouse events while the canvas is not at the page's top-left.

    python3 tests/web_mouse_test.py [--no-build]

Builds tests/web_mouse.c for the web (emcc, the default web/shell.html, which keeps the 960x540 canvas 16:9 and centred),
then opens it in headless Chromium (tests/web_run.js, Playwright) in windows of several shapes: letterboxed (bars above
and below), pillarboxed (left and right), smaller than the canvas (scaled down, and offset) and the exact size. Each time
the runner reads the button's rectangle from ARC (`ui`), converts it to page coordinates through the canvas's bounding
box, and clicks: mouse move, down, 120 ms, up (mousedown lands on the canvas, mouseup on the window). Exit code 0 when
the button fired every time."""
import os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'build', 'webtest_mouse')

def main():
    if '--no-build' not in sys.argv:
        os.makedirs(OUT, exist_ok=True)
        subprocess.check_call(['bash', '-c', 'source /home/claude/emsdk/emsdk_env.sh >/dev/null 2>&1; emcc -std=c11 -Iinclude -Wall -Wextra -O2 -ffp-contract=off tests/web_mouse.c src/arc.c '
                               '-o build/webtest_mouse/mouse.html --shell-file web/shell.html -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 '
                               '-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_arc_web_audio_fill,_arc_web_command -sEXPORTED_RUNTIME_METHODS=lengthBytesUTF8,stringToUTF8,HEAPF32'], cwd=ROOT)
    env = dict(os.environ, NODE_PATH=subprocess.run(['npm', 'root', '-g'], capture_output=True, text=True).stdout.strip())
    fails = 0
    for vp, what in (('1000x900', 'letterboxed'), ('1500x600', 'pillarboxed'), ('800x700', 'scaled down, letterboxed'), ('1200x500', 'scaled down, pillarboxed'), ('960x540', 'exact size')):
        r = subprocess.run(['node', os.path.join(ROOT, 'tests', 'web_run.js'), OUT, 'mouse.html', '--args', '--mute', '--until', 'ARC test', '--viewport', vp,
                            '--click', 'Press me', '--port', '8792'], capture_output=True, text=True, env=env, timeout=120)
        lines = [l for l in r.stdout.splitlines() if l.startswith('runner:') or l.startswith('ARC test')]
        ok = any('ARC test web click OK' in l for l in lines)
        fails += not ok
        print(('ok    ' if ok else 'FAIL  ') + f'{vp} ({what}): ' + ' | '.join(lines), flush=True)
    print(f'ARC web mouse test {"FAILED" if fails else "OK"}: {fails} failed')
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
