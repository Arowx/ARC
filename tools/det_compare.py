#!/usr/bin/env python3
"""ARC 0.9.7: compare two determinism traces (tests/det_check.c --trace FILE) from two machines or compilers and name
the first point they part.  python tools/det_compare.py a.txt b.txt"""
import sys

def read(path):
    rows = []
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) >= 3 and p[0] not in ('ARC',):
                rows.append((' '.join(p[:-2]), int(p[-2]), p[-1]))
            elif len(p) >= 5 and p[0] == 'ARC' and p[1] == 'trace':   # a console log with "ARC trace ..." lines
                rows.append((' '.join(p[2:-2]), int(p[-2]), p[-1]))
    return rows

def main():
    if len(sys.argv) != 3:
        print(__doc__); return 2
    a, b = read(sys.argv[1]), read(sys.argv[2])
    if not a or not b:
        print('ARC error det_compare: a trace is empty'); return 2
    seen = set()
    for (sa, ka, ha), (sb, kb, hb) in zip(a, b):
        if (sa, ka) != (sb, kb):
            print(f'ARC error det_compare: the traces have different steps ({sa} {ka} / {sb} {kb}): different ARC versions?'); return 1
        scen = sa.split()[0]
        if ha != hb and scen not in seen:
            seen.add(scen)
            print(f'ARC error det_compare: {sa} first differs at step {ka} ({ha} / {hb})')
    if len(a) != len(b):
        print(f'ARC error det_compare: {len(a)} lines / {len(b)} lines'); return 1
    if not seen:
        print(f'ARC ok det_compare: all {len(a)} points match'); return 0
    return 1

if __name__ == '__main__':
    sys.exit(main())
