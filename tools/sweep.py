#!/usr/bin/env python3
"""ARC balance sweep: run a game headless over many seeds and tabulate its dump fields.

    python tools/sweep.py build/game.exe --seeds 1-50 --frames 6000 --field game.wins --field game.turns
    python tools/sweep.py build/game --seeds 20 --frames 3000 --set enemy_hp=40 --csv out.csv
    python tools/sweep.py build/game --seeds 1-8 --cmd "tap SPACE; step 600" --field "game.*"

Each run is   GAME --headless --seed S --frames N [--set name=value ...] --dump-at-exit FILE [extra args]
(with --cmd the commands are passed as --cmd, and the game should end on its own or via --frames).
Fields are dotted paths into the dump JSON ("game.score", "vars.enemy_hp", "stats.particles");
wildcards work ("game.*"). With no --field, the numbers directly under "game" are shown.
Prints one row per seed, then median / min / max / mean for each field. Runs in parallel (--jobs).
No libraries beyond Python 3.
"""
import argparse, concurrent.futures, csv, fnmatch, json, os, statistics, subprocess, sys, tempfile


def parse_seeds(text):
    seeds = []
    for part in text.split(','):
        part = part.strip()
        if not part:
            continue
        if '-' in part:
            a, b = part.split('-', 1)
            seeds.extend(range(int(a), int(b) + 1))
        elif not seeds and ',' not in text:
            seeds.extend(range(1, int(part) + 1))     # "--seeds 20" means seeds 1..20
        else:
            seeds.append(int(part))
    return seeds


def flatten(obj, prefix='', out=None):
    out = {} if out is None else out
    if isinstance(obj, dict):
        for k, v in obj.items():
            flatten(v, f'{prefix}.{k}' if prefix else k, out)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            flatten(v, f'{prefix}.{i}', out)
    else:
        out[prefix] = obj
    return out


def run_one(args, seed, tmp):
    path = os.path.join(tmp, f'dump_{seed}.json')
    cmd = [args.game, '--headless', '--seed', str(seed), '--dump-at-exit', path]
    if args.frames:
        cmd += ['--frames', str(args.frames)]
    for s in args.set or []:
        cmd += ['--set', s]
    if args.cmd:
        cmd += ['--cmd', args.cmd]
    cmd += args.extra
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout)
    except subprocess.TimeoutExpired:
        return seed, None, 'timed out'
    except OSError as e:
        return seed, None, str(e)
    try:
        with open(path, encoding='utf-8') as f:
            return seed, flatten(json.load(f)), None if p.returncode == 0 else f'exit code {p.returncode}'
    except (OSError, ValueError):
        tail = (p.stdout + p.stderr).strip().splitlines()[-3:]
        return seed, None, f'no dump (exit code {p.returncode}) ' + ' | '.join(tail)


def pick_fields(patterns, rows):
    keys = []
    for r in rows:
        for k in r:
            if k not in keys:
                keys.append(k)
    if not patterns:     # by default: the plain numbers directly under "game" (not lists or nested objects)
        return [k for k in keys if k.startswith('game.') and k.count('.') == 1
                and any(isinstance(r.get(k), (int, float)) for r in rows)]
    chosen = []
    for pat in patterns:
        if not any(fnmatch.fnmatchcase(k, pat) or k == pat for k in keys):
            print(f'note: no dump field matches "{pat}"', file=sys.stderr)
        for k in keys:
            if (fnmatch.fnmatchcase(k, pat) or k == pat) and k not in chosen:
                if any(isinstance(r.get(k), (int, float)) and not isinstance(r.get(k), bool) for r in rows) or pat == k:
                    chosen.append(k)
    return chosen


def fmt(v):
    if v is None:
        return '-'
    if isinstance(v, bool):
        return 'yes' if v else 'no'
    if isinstance(v, float):
        return f'{v:.4g}'
    return str(v)


def main():
    ap = argparse.ArgumentParser(description='Run an ARC game over many seeds and tabulate its dump.',
                                 epilog='Arguments after -- go to the game unchanged.')
    ap.add_argument('game', help='the game executable')
    ap.add_argument('--seeds', default='1-10', help='"20" (1..20), "5-40" or "1,7,9" (default 1-10)')
    ap.add_argument('--frames', type=int, default=0, help='ticks to run before the game is stopped')
    ap.add_argument('--field', action='append', help='dotted dump field or wildcard; repeat for more')
    ap.add_argument('--set', action='append', help='name=value for an arc_var, passed to every run')
    ap.add_argument('--cmd', help='agent commands for every run (as for --cmd)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4, help='runs at once')
    ap.add_argument('--timeout', type=float, default=600, help='seconds before a run is given up')
    ap.add_argument('--csv', help='also write every row to this CSV file')
    ap.add_argument('extra', nargs='*', help=argparse.SUPPRESS)
    args = ap.parse_args()
    if not os.path.exists(args.game) and os.path.exists(args.game + '.exe'):
        args.game += '.exe'
    args.game = os.path.abspath(args.game)
    seeds = parse_seeds(args.seeds)
    if not seeds:
        sys.exit('no seeds')

    results = {}
    with tempfile.TemporaryDirectory() as tmp, concurrent.futures.ThreadPoolExecutor(max(1, args.jobs)) as pool:
        for seed, row, err in pool.map(lambda s: run_one(args, s, tmp), seeds):
            results[seed] = (row, err)
            if err:
                print(f'seed {seed}: {err}', file=sys.stderr)
    rows = [results[s][0] for s in seeds if results[s][0]]
    if not rows:
        sys.exit('no run produced a dump')
    fields = pick_fields(args.field, rows)
    if not fields:
        sys.exit('no matching fields; the dump has: ' + ', '.join(sorted(rows[0])))

    header = ['seed'] + fields
    table = [[str(s)] + [fmt((results[s][0] or {}).get(f)) for f in fields] for s in seeds]
    stats = []
    for name, fn in (('median', statistics.median), ('min', min), ('max', max), ('mean', statistics.fmean)):
        line = [name]
        for f in fields:
            vals = [r[f] for r in rows if isinstance(r.get(f), (int, float))]
            line.append(fmt(float(fn(vals)) if vals else None))
        stats.append(line)
    widths = [max(len(x[i]) for x in [header] + table + stats) for i in range(len(header))]
    show = lambda cells: '  '.join(c.rjust(w) for c, w in zip(cells, widths))
    print(show(header))
    print('  '.join('-' * w for w in widths))
    for line in table:
        print(show(line))
    print('  '.join('-' * w for w in widths))
    for line in stats:
        print(show(line))
    failed = len(seeds) - len(rows)
    print(f'{len(rows)} of {len(seeds)} runs' + (f', {failed} failed' if failed else ''))

    if args.csv:
        with open(args.csv, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(header)
            for s in seeds:
                r = results[s][0] or {}
                w.writerow([s] + [r.get(fld, '') for fld in fields])
        print(f'wrote {args.csv}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
