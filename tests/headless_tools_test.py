#!/usr/bin/env python3
"""ARC 0.9.8 checks of the headless tools (build.sh test / build.bat test run it):
    python3 tests/headless_tools_test.py EXE        (EXE: tests/headless_game.c built headless)
--draw-last and --shot-at give the same picture as drawing every tick (and are faster); --shot-at alone quits after its
last shot; --no-assets keeps texture sizes and sound lengths; `reset` returns to the fresh state and a case run after it
matches a fresh process; tools/arc_batch.py gives each case what a fresh process gives; arc_import --fit / --origin.
Prints "ARC ok headless tools" or "ARC error headless tools: ..." (exit code 1). Standard library only."""
import json, os, struct, subprocess, sys, time, wave, zlib

HERE = os.path.dirname(os.path.abspath(__file__)); ARC = os.path.dirname(HERE)


def png(path, w, h):   # an RGB gradient, written with zlib only
    rows = b''.join(b'\0' + b''.join(struct.pack('BBB', x * 255 // max(w - 1, 1), y * 255 // max(h - 1, 1), 96) for x in range(w)) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def run(exe, args, stdin=None):
    t0 = time.time()
    p = subprocess.run([exe, '--headless'] + args, input=stdin, capture_output=True, text=True, timeout=600)
    return p.stdout, time.time() - t0


def dumps(out): return [json.loads(l[9:]) for l in out.splitlines() if l.startswith('ARC dump ')]


def same_png(a, b):
    with open(a, 'rb') as fa, open(b, 'rb') as fb: return fa.read() == fb.read()


def main():
    exe = os.path.abspath(sys.argv[1]); work = os.path.join(ARC, 'build', 'headless_tools'); os.makedirs(work, exist_ok=True); os.chdir(work)
    errs = []
    png('tex.png', 512, 256)
    with wave.open('tone.wav', 'wb') as w: w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050); w.writeframes(b'\0\0' * 22050 * 3)
    files = ['--tex', 'tex.png', '--sound', 'tone.wav']
    # --draw-last / --shot-at: the same final picture, drawn once
    for f in ('all.png', 'last.png', 's10.png', 's25.png'):
        if os.path.exists(f): os.remove(f)
    _, t_all = run(exe, files + ['--frames', '80', '--shot-at', '79=all.png'])
    _, t_last = run(exe, files + ['--frames', '80', '--draw-last', '1', '--shot-at', '79=last.png'])
    out, _ = run(exe, files + ['--draw-last', '2', '--shot-at', '10=s10.png,25=s25.png'])
    if not (os.path.exists('all.png') and os.path.exists('last.png') and same_png('all.png', 'last.png')): errs.append('--draw-last changed the picture')
    if not (os.path.exists('s10.png') and os.path.exists('s25.png') and 'ARC exit tick=25' in out): errs.append('--shot-at did not take both shots and stop: ' + out[-200:])
    print('headless tools: every tick drawn %.2f s, --draw-last 1 %.2f s' % (t_all, t_last))
    # --no-assets: sizes and lengths kept
    full = dumps(run(exe, files + ['--cmd', 'step 3; dump; quit'])[0]); none = dumps(run(exe, files + ['--no-assets', '--cmd', 'step 3; dump; quit'])[0])
    if not full or not none or full[0]['game'] != none[0]['game'] or full[0]['game']['tex_w'] != 512 or abs(full[0]['game']['sound_s'] - 3) > 0.01:
        errs.append('--no-assets changed the dump: %s / %s' % (full and full[0]['game'], none and none[0]['game']))
    # reset: back to the fresh state; a case after it matches a fresh process
    case = 'step 20\nkick\nstep 40\ndump\n'
    fresh = dumps(run(exe, ['--stdin'], 'step 1\ndump\n' + case + 'quit\n')[0])
    again = dumps(run(exe, ['--stdin'], 'step 1\ndump\n' + case + 'step 15\nkick\nreset\nstep 1\ndump\n' + case + 'quit\n')[0])
    if len(fresh) != 2 or len(again) != 4 or again[2]['game'] != fresh[0]['game'] or again[3]['game'] != fresh[1]['game'] or again[2]['tick'] != 1:
        errs.append('reset did not return to the fresh state')
    # arc_batch: each case as a fresh process gives it
    cases = ['step %d\nkick\nstep 30\ndump\n' % (5 * i + 1) for i in range(6)]
    with open('cases.txt', 'w') as f: f.write('\n---\n'.join(cases))
    p = subprocess.run([sys.executable, os.path.join(ARC, 'tools', 'arc_batch.py'), '--exe', exe, '-j', '2', '--cases', 'cases.txt', '--args', '--no-assets'],
                       capture_output=True, text=True, timeout=600)
    res = [json.loads(l) for l in p.stdout.splitlines() if l.startswith('{')]
    if p.returncode != 0 or len(res) != 6: errs.append('arc_batch failed: ' + p.stderr[-300:])
    else:
        for i, c in enumerate(cases):
            want = [d['game'] for d in dumps(run(exe, ['--stdin', '--no-assets'], c + 'quit\n')[0])]
            if res[i]['dumps'] != want: errs.append('arc_batch case %d differs from a fresh process' % (i + 1)); break
    # arc_import --fit / --origin: exact sizes, bounds in --json
    p = subprocess.run([sys.executable, os.path.join(ARC, 'tools', 'arc_import.py'), os.path.join(ARC, 'tests', 'pyramid.obj'), '--game', 'imp', '--fit', '0.5', '2', '0.1',
                        '--origin', 'back', '--no-preview', '--force', '--json'], capture_output=True, text=True, timeout=300)
    try:
        b = json.loads(p.stdout)['files'][0]['bounds']
        if [round(v, 4) for v in b['size']] != [0.5, 2.0, 0.1] or round(b['min'][2], 4) != 0 or round(b['min'][1], 4) != 0: errs.append('arc_import --fit bounds %s' % b)
    except Exception as e: errs.append('arc_import --fit: %s %s' % (e, p.stdout[-200:] + p.stderr[-200:]))
    if errs:
        for e in errs: print('ARC error headless tools: ' + e)
        return 1
    print('ARC ok headless tools')
    return 0


if __name__ == '__main__':
    sys.exit(main())
