#!/usr/bin/env python3
"""ARC 0.10.11: fingers are recorded and replayed. Plays a demo with scripted touches under --record, replays the
recording, and compares the fingers (and the game) in the two dumps taken at the same tick.

    python3 tests/touch_replay_test.py build/demo_headless
"""
import json, os, subprocess, sys, tempfile

exe = os.path.abspath(sys.argv[1])
d = tempfile.mkdtemp(prefix='arc_touch_')
rec, a, b = os.path.join(d, 't.rec'), os.path.join(d, 'a.json'), os.path.join(d, 'b.json')
script = ('touch down 0 200 400; step 3; touch down 1 1000 600; step 2; touch move 0 260 380; step 2; '
          'touch up 1; step 1; dump ' + a + '; step 4; quit')
subprocess.run([exe, '--headless', '--record', rec, '--cmd', script], cwd=d, capture_output=True, text=True, timeout=120)
subprocess.run([exe, '--headless', '--replay', rec, '--cmd', 'step 8; dump ' + b + '; quit'], cwd=d, capture_output=True, text=True, timeout=120)
bad = []
try:
    A, B = json.load(open(a)), json.load(open(b))
    if A['tick'] != B['tick']: bad.append('ticks %s / %s' % (A['tick'], B['tick']))
    if A['input'] != B['input']: bad.append('fingers differ: %s / %s' % (A['input'], B['input']))
    if A['input']['touches'] != 1 or not any(t['released'] and t['id'] == 1 for t in A['input']['touch']): bad.append('unexpected fingers %s' % A['input'])
    if A.get('game') != B.get('game'): bad.append('game differs')
except (OSError, ValueError, KeyError) as e:
    bad.append(str(e))
print('ARC ok touch replay' if not bad else 'ARC error touch replay: ' + '; '.join(bad))
sys.exit(1 if bad else 0)
