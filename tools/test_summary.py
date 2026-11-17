#!/usr/bin/env python3
"""Summarise an ARC `build test` log (0.9.1): one line per stage, then the verdict.

    python3 tools/test_summary.py build/test.log      (build.sh test / build.bat test run this at the end)

The full log stays in build/test.log. `ARC error` lines inside a passing stage are expected: the self-tests check
that mistakes are reported. A stage that never printed its result line is shown as MISSING (the run stopped before
it); the lines just before the stop are printed to show why. Exit code 1 unless every stage passed.
"""
import re, sys

# (stage, pass pattern, fail pattern); the first match in the log decides
STAGES = [
    ('self-test',          r'ARC test OK (\d+)/(\d+) checks passed',               r'ARC test FAILED (\d+)/(\d+) checks passed'),
    ('acceptance checks',  r'arc_check: (\d+)/(\d+) checks passed',                r'arc_check: (\d+)/(\d+) checks passed.*FAIL|arc_check: .*failed'),
    ('doctor',             r'ARC ok doctor',                                        r'ARC error doctor'),
    ('API docs current',   r'ARC ok api docs',                                      r'ARC error api docs'),
    ('QUICK.md example',   r'ARC ok quick example',                                 r'ARC error quick example'),
    ('asset import',       r'ARC ok import',                                        r'ARC error import'),
    ('release dry run',    r'ARC ok release dry run',                               r'ARC error release dry run'),
    ('templates',          r'ARC ok templates (\d+)',                               r'ARC error template'),
    ('warnings',           r'ARC ok warnings',                                      r'ARC error warnings'),
    ('headless tools',     r'ARC ok headless tools',                                r'ARC error headless tools'),
    ('post and shadow',    r'ARC test OK (\d+)/(\d+) post and shadow checks passed', r'ARC test FAILED (\d+)/(\d+) post and shadow checks passed'),
]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'build/test.log'
    try:
        raw = open(path, 'rb').read()
        # Windows PowerShell's Tee-Object writes UTF-16 with a byte-order mark
        text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8', errors='replace')
    except OSError:
        print('test summary: no log at %s' % path); return 1
    lines = text.splitlines()
    rows, ok_all, last_seen = [], True, -1
    for name, pat_ok, pat_fail in STAGES:
        mf = next((i for i, l in enumerate(lines) if re.search(pat_fail, l)), None)
        mo = next((i for i, l in enumerate(lines) if re.search(pat_ok, l)), None)
        if mf is not None and (mo is None or mf <= mo):
            rows.append((name, 'FAIL', lines[mf].strip())); ok_all = False; last_seen = max(last_seen, mf)
        elif mo is not None:
            m = re.search(pat_ok, lines[mo]); g = m.groups()
            detail = ('%s/%s' % g[:2]) if len(g) >= 2 else (g[0] if g else '')
            rows.append((name, 'ok', detail)); last_seen = max(last_seen, mo)
        else:
            rows.append((name, 'MISSING', '')); ok_all = False
    fails = [l.strip() for l in lines if re.match(r'\s*ARC test FAIL ', l)]
    print('')
    print('==== ARC test summary (full log: %s) ====' % path)
    for name, st, detail in rows:
        print('  %-20s %-8s %s' % (name, st, detail))
    for f in fails[:10]: print('  ' + f)
    if any(st == 'MISSING' for _, st, _ in rows):
        print('  -- the run stopped after this (last lines of the log):')
        for l in lines[-12:]: print('     ' + l)
    print('==== %s ====' % ('ALL PASSED' if ok_all else 'FAILED'))
    return 0 if ok_all else 1


if __name__ == '__main__':
    sys.exit(main())
