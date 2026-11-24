#!/usr/bin/env python3
"""One health report for an ARC game (ARC 0.10.16): everything the separate tools say, in one short page.

    python3 <ARC>/tools/arc_report.py [GAME_DIR] [--seeds 1-20 --frames N --field game.score ...] [--sessions] [--json]

It runs, in order:

  1. the doctor (tools/arc_doctor.py): errors and warnings;
  2. the compiler warning check on the game's own sources (tools/warn_check.py: MSVC when `cl` is on the PATH,
     else clang; skipped when neither is there);
  3. every acceptance file in tests/ (tools/arc_check.py; builds the game headless): blocks and checks passed;
  4. `state_check` (save, load, compare the dump) when the game has save_state;
  5. with --seeds: a seed sweep (tools/sweep.py) with min / median / max of each --field (default: the numbers under
     `game`), after --frames ticks (default 600) with --cmd's commands;
  6. with --sessions: the newest play session (sessions/ or build/sessions/): events, F8 bookmarks and the replay
     command for each.

Prints a short markdown report (--json: the same as JSON) and exits 1 when anything failed. Standard library only.
"""
import argparse, csv, glob, json, os, re, shutil, statistics, subprocess, sys, tempfile

ARC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ARC_ROOT, 'tools')
sys.path.insert(0, TOOLS)
import arc_brief  # noqa: E402  (finding the game's parts the same way the brief does)


def py(*args, cwd=None, timeout=3600):
    r = subprocess.run([sys.executable] + list(args), cwd=cwd, capture_output=True, text=True, errors='replace', timeout=timeout)
    return r.returncode, r.stdout, r.stderr


def doctor(game):
    rc, out, err = py(os.path.join(TOOLS, 'arc_doctor.py'), game, '--json')
    try: j = json.loads(out)
    except ValueError: return {'ok': False, 'error': (err or out).strip()[-300:]}
    f = j.get('findings', [])
    errs = [x for x in f if x.get('level') == 'ERROR']; warns = [x for x in f if x.get('level') == 'WARN']
    return {'ok': not errs, 'errors': len(errs), 'warnings': len(warns),
            'items': [{'level': x['level'], 'msg': x.get('msg'), 'where': os.path.relpath(x['where'], game) if x.get('where') and os.path.isabs(x['where']) else x.get('where'), 'fix': x.get('fix')} for x in errs + warns]}


def warnings(game):
    srcs = [f for f in arc_brief.sources(game, None) if f.endswith('.c')]   # (warn_check compiles C11; C++ games skip it)
    if not srcs: return {'ok': True, 'skipped': 'no C sources (the check compiles C11)'}
    args = [os.path.join(TOOLS, 'warn_check.py')] + srcs + (['--msvc'] if os.name == 'nt' and shutil.which('cl') else [])
    rc, out, err = py(*args, cwd=game)
    lines = (out + err).strip().splitlines()
    last = lines[-1] if lines else '(no output)'
    return {'ok': rc == 0, 'skipped': last if 'skipped' in last else None, 'summary': last,
            'warnings': [l.strip() for l in lines if l.strip().startswith('new warning') or re.search(r'warning C\d+', l)][:20]}


def acceptance(game):
    out = []
    for f in arc_brief.check_files(game, None):
        blocks, last, ok = arc_brief.run_checks(f)
        out.append({'file': os.path.relpath(f, game).replace('\\', '/'), 'ok': ok, 'summary': last,
                    'blocks': [{'name': b[0], 'ok': b[1], 'passed': b[2], 'total': b[3]} for b in blocks]})
    return out


def state_check(game, exe, has_save):
    if not has_save: return {'ok': True, 'skipped': 'no save_state (rewind, the timeline and state_check need it)'}
    if not exe: return {'ok': False, 'error': 'no headless build to run'}
    try:
        r = subprocess.run([exe, '--headless', '--cmd', 'step 120; state_check; quit'], cwd=os.path.dirname(exe),
                           capture_output=True, text=True, errors='replace', timeout=300)
    except (OSError, subprocess.TimeoutExpired) as e:
        return {'ok': False, 'error': str(e)}
    lines = [l for l in r.stdout.splitlines() if 'state_check' in l]
    return {'ok': any(l.startswith('ARC ok state_check') for l in lines), 'summary': lines[-1] if lines else 'no state_check line (exit %s)' % r.returncode}


def sweep(exe, seeds, frames, fields, cmd):
    if not exe: return {'ok': False, 'error': 'no headless build to run'}
    tmp = tempfile.mkdtemp(prefix='arc_report_')
    try:
        path = os.path.join(tmp, 'sweep.csv')
        args = [os.path.join(TOOLS, 'sweep.py'), exe, '--seeds', seeds, '--frames', str(frames), '--csv', path]
        for f in fields or []: args += ['--field', f]
        if cmd: args += ['--cmd', cmd]
        rc, out, err = py(*args)
        if not os.path.exists(path): return {'ok': False, 'error': (err or out).strip()[-400:]}
        with open(path, newline='', encoding='utf-8') as fh: rows = list(csv.DictReader(fh))
        cols = [c for c in (rows[0].keys() if rows else []) if c != 'seed']
        stats = {}
        for c in cols:
            nums = []
            for r in rows:
                try: nums.append(float(r[c]))
                except (TypeError, ValueError): pass
            if nums: stats[c] = {'min': min(nums), 'median': statistics.median(nums), 'max': max(nums), 'runs': len(nums)}
            else: stats[c] = {'values': sorted({r[c] for r in rows})[:8]}
        return {'ok': rc == 0, 'seeds': seeds, 'frames': frames, 'runs': len(rows), 'fields': stats}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def newest_session(game, exe):
    import sessions as S
    roots = [d for d in (os.path.join(game, 'sessions'), os.path.join(game, 'build', 'sessions')) if os.path.isdir(d)]
    dirs = sorted((d for r in roots for d in S.session_dirs(r)), key=lambda d: os.path.basename(d))
    if not dirs: return {'ok': True, 'skipped': 'no play sessions (arc_app.sessions = true records them)'}
    d = dirs[-1]; evs = S.load_events(d); sm = S.load_summary(d)
    counts = {}
    for e in evs: counts[e.get('ev')] = counts.get(e.get('ev'), 0) + 1
    rec = os.path.join(d, 'input.rec'); x = exe or 'GAME'
    if re.search(r'_headless(\.exe)?$', x): x += ' --headless'   # (the window build replays it on screen)
    marks = [{'tick': e.get('tick', 0), 'time': S.fmt_secs(e.get('t', 0)), 'note': e.get('note'),
              'shot': os.path.join(d, 'mark_%02d.jpg' % e.get('n', 0)) if os.path.exists(os.path.join(d, 'mark_%02d.jpg' % e.get('n', 0))) else None,
              'replay': '%s --replay %s --cmd "step %d"' % (x, rec, max(0, e.get('tick', 0) - 300))} for e in evs if e.get('ev') == 'bookmark']
    return {'ok': True, 'session': os.path.relpath(d, game), 'sessions': len(dirs),
            'played': S.fmt_secs(sm.get('seconds_played', 0)) if sm else None, 'closed_normally': sm is not None,
            'events': dict(sorted(counts.items(), key=lambda kv: -kv[1])[:12]), 'bookmarks': marks,
            'replay': '%s --replay %s' % (x, rec)}


def report(game, seeds=None, frames=600, fields=None, cmd=None, sessions=False):
    game = os.path.abspath(game)
    texts = {f: arc_brief.read(f) for f in arc_brief.sources(game, None)}
    cbs = arc_brief.callbacks(texts)
    R = {'game': game, 'title': arc_brief.title_of(texts, game)}
    R['doctor'] = doctor(game)
    R['warnings'] = warnings(game)
    R['acceptance'] = acceptance(game)   # builds the headless exe on the way (game . lines)
    exe = arc_brief.headless_exe(game, None, None)
    if not exe and arc_brief.project_name(game):   # no check files built it: build it here
        cmdline = ['cmd', '/c', 'build.bat', 'headless'] if os.name == 'nt' else ['sh', 'build.sh', 'headless']
        subprocess.run(cmdline, cwd=game, capture_output=True, env=dict(os.environ, ARC_NO_LAUNCH='1'))
        exe = arc_brief.headless_exe(game, None, None)
    R['exe'] = exe
    R['state_check'] = state_check(game, exe, 'save_state' in cbs)
    if seeds: R['sweep'] = sweep(exe, seeds, frames, fields, cmd)
    if sessions: R['sessions'] = newest_session(game, exe)
    parts = [R['doctor'], R['warnings'], R['state_check']] + R['acceptance'] + ([R['sweep']] if seeds else []) + ([R['sessions']] if sessions else [])
    R['ok'] = all(p.get('ok') for p in parts)
    return R


def fnum(v): return ('%d' % v) if float(v).is_integer() else ('%.3g' % v)


def markdown(R):
    L = ['# Report: %s  %s' % (R['title'], 'OK' if R['ok'] else 'FAILED'), '']
    d = R['doctor']
    L.append('- **Doctor:** %s' % ('could not run: ' + d['error'] if 'error' in d else '%d errors, %d warnings' % (d['errors'], d['warnings'])))
    for x in d.get('items', [])[:12]: L.append('  - %s %s%s' % (x['level'], x['msg'], ('  (%s)' % x['where']) if x.get('where') and x['where'] != '.' else ''))
    w = R['warnings']
    L.append('- **Compiler warnings:** %s' % (w.get('summary') or w.get('skipped')))
    for x in w.get('warnings', [])[:10]: L.append('  - ' + x)
    if R['acceptance']:
        tb = sum(len(a['blocks']) for a in R['acceptance']); pb = sum(sum(b['ok'] for b in a['blocks']) for a in R['acceptance'])
        L.append('- **Acceptance:** %d/%d blocks passed in %d file%s' % (pb, tb, len(R['acceptance']), '' if len(R['acceptance']) == 1 else 's'))
        for a in R['acceptance']:
            L.append('  - %s: %s' % (a['file'], a['summary']))
            for b in a['blocks']:
                if not b['ok']: L.append('    - FAILED %s (%d/%d)' % (b['name'], b['passed'], b['total']))
    else:
        L.append('- **Acceptance:** no check files in tests/')
    s = R['state_check']
    L.append('- **state_check:** %s' % (s.get('summary') or s.get('skipped') or s.get('error')))
    if 'sweep' in R:
        sw = R['sweep']
        if 'error' in sw: L.append('- **Sweep:** could not run: ' + sw['error'])
        else:
            L.append('- **Sweep:** %d runs, seeds %s, %d ticks each' % (sw['runs'], sw['seeds'], sw['frames']))
            for k, v in sw['fields'].items():
                L.append('  - %s: %s' % (k, ('min %s, median %s, max %s' % (fnum(v['min']), fnum(v['median']), fnum(v['max']))) if 'min' in v else ', '.join(v['values'])))
    if 'sessions' in R:
        se = R['sessions']
        if 'skipped' in se: L.append('- **Play sessions:** ' + se['skipped'])
        else:
            L.append('- **Newest play session:** %s (%d in all), played %s%s' % (se['session'], se['sessions'], se['played'] or '?', '' if se['closed_normally'] else ', did NOT close normally (crash?)'))
            L.append('  - events: ' + ', '.join('%s %d' % kv for kv in se['events'].items()))
            L.append('  - replay: `%s`' % se['replay'])
            for m in se['bookmarks'][:10]:
                L.append('  - F8 at %s (tick %d)%s: `%s`%s' % (m['time'], m['tick'], (' "%s"' % m['note']) if m['note'] else '', m['replay'], ('  shot ' + m['shot']) if m['shot'] else ''))
    return '\n'.join(L) + '\n'


def main():
    ap = argparse.ArgumentParser(description='One health report for an ARC game', formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('game', nargs='?', default='.', help='the game folder (with arc.txt)')
    ap.add_argument('--seeds', help='also sweep these seeds: "20", "1-20" or "1,5,9"')
    ap.add_argument('--frames', type=int, default=600, help='ticks per sweep run (default 600)')
    ap.add_argument('--field', action='append', help='dump field to sweep (dotted, wildcards); repeat for more')
    ap.add_argument('--cmd', help='agent commands for each sweep run')
    ap.add_argument('--sessions', action='store_true', help='summarise the newest play session')
    ap.add_argument('--json', action='store_true', help='print JSON instead of markdown')
    ap.add_argument('--out', help='also write the markdown here')
    a = ap.parse_args()
    if not os.path.isdir(a.game): sys.exit('no such game folder: ' + a.game)
    R = report(a.game, a.seeds, a.frames, a.field, a.cmd, a.sessions)
    md = markdown(R)
    if a.out:
        with open(a.out, 'w', encoding='utf-8', newline='\n') as f: f.write(md)
    print(json.dumps(R, indent=1) if a.json else md, end='' if not a.json else '\n')
    sys.exit(0 if R['ok'] else 1)


if __name__ == '__main__':
    main()
