#!/usr/bin/env python3
"""Run an ARC acceptance check: play a game headless with agent commands and test what it shows.

    python3 tools/arc_check.py REQUEST.md [--exe build/game_headless] [--block N | --name TEXT] [--fail-fast] [-v]
    python3 tools/arc_check.py CHECKS.txt  ...                (a file that is only check lines)

The checks live in a fenced block marked arc-check (in a request .md, see docs/REQUESTS.md) or a plain text file:

    game samples/demo/demo.c           build it with build.sh headless (or: exe build/demo_headless)
    game .                             the game project this file is in (the folder with arc.txt), built
                                       headless with its own build.sh / build.bat (0.9.1)
    args --seed 3                      extra command-line flags
    do step 10; press D; step 40; release D
    expect game.score == 10            the dump (game. = what the game's dump() wrote)
    expect frame.meshes[name=player].on_screen == 1
    expect frame.texts[*].text contains "SCORE 10"
    expect ui[label=Restart].kind == "button"
    expect count(frame.meshes[name=coin].items) == 2
    do ui_click Restart; step 2
    expect game.restarted == true
    shot after.png                     any other agent command runs as it is (shot, compare REF.png 0.5, ...)

Every ```arc-check block in the file runs (0.10.12), in order, each in a fresh game process, named by the `##` heading
above it; a block with a line `continue` carries on in the previous block's process. A block without `game` / `exe` /
`args` lines uses the last ones given. The last line says how many checks and blocks passed; exit 1 if any failed.
An `exe` path is read from the current folder, else from the check file's folder (0.10.10). Relative paths given to
--save-dir, --record, --replay, --script, --editor, --dump-at-exit are read from the check file's folder (0.10.12).
Paths: `dump.` (or a bare first name) reads the dump, `frame.` the frame description (the `frame` command), `ui` the
widget list. `name[k=v]` keeps list items whose k equals v, `name[*]` all items, `name[N]` the Nth. When a path gives
several values, == / contains / < ... pass if any value passes; `count(path)` is how many there are, `min(path)` /
`max(path)` the smallest / largest (so `min(...) > x` means every one). `sheet.` reads the last `sheet` command's
result (`sheet.cells[*].filled`, `.label_ink`: shares of the cell / label row unlike the background). Operators: ==
!= < <= > >= contains exists missing. Values: numbers, "strings", true, false, null.
Exit code 0 when every check passes. The game's `ARC error` lines fail the run; `ARC hint` lines are shown.
"""
import argparse, json, os, re, shlex, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def clean_lines(body):
    lines = []
    for raw in body.splitlines():
        line = raw.split(' #')[0].strip() if not raw.strip().startswith('expect') else raw.strip()
        if line.startswith('#') or not line:
            continue
        lines.append(line)
    return lines


def read_blocks(path):
    """0.10.12: every ```arc-check block, in order, with the `##` heading above it as its name: [(name, lines)]. A file
    with no block is one block of plain check lines."""
    text = open(path, encoding='utf-8').read()
    blocks, heading = [], ''
    pos = 0
    for m in re.finditer(r'```arc-check[^\n]*\n(.*?)```', text, re.S):
        for h in re.finditer(r'^#{1,6}\s+(.+?)\s*$', text[pos:m.start()], re.M): heading = h.group(1)
        blocks.append((heading, clean_lines(m.group(1))))
        pos = m.end()
    return blocks if blocks else [('', clean_lines(text))]


def read_checks(path):   # (the first block's lines; kept for tools that import it)
    return read_blocks(path)[0][1]


def split_path(p):
    out, i, cur = [], 0, ''
    while i < len(p):
        c = p[i]
        if c == '.':
            if cur: out.append(cur)
            cur = ''
        elif c == '[':
            j = p.index(']', i)
            if cur: out.append(cur)
            cur = ''
            out.append('[' + p[i + 1:j] + ']')
            i = j
        else:
            cur += c
        i += 1
    if cur: out.append(cur)
    return out


def literal(s):
    s = s.strip()
    if s.startswith('"') and s.endswith('"'): return s[1:-1]
    if s in ('true', 'false'): return s == 'true'
    if s == 'null': return None
    try: return int(s)
    except ValueError: pass
    try: return float(s)
    except ValueError: return s


def walk(values, seg):
    out = []
    for v in values:
        if seg.startswith('['):
            inner = seg[1:-1]
            if not isinstance(v, list): continue
            if inner == '*': out.extend(v)
            elif '=' in inner:
                k, want = inner.split('=', 1); want = literal(want)
                out.extend(x for x in v if isinstance(x, dict) and (x.get(k) == want or str(x.get(k)) == str(want)))
            else:
                idx = int(inner)
                if -len(v) <= idx < len(v): out.append(v[idx])
        elif isinstance(v, dict) and seg in v:
            out.append(v[seg])
    return out


def resolve(path, snaps):
    segs = split_path(path)
    root = segs[0] if segs else ''
    if root in ('dump', 'frame', 'ui', 'sheet'):
        src = snaps.get(root); segs = segs[1:]
    else:
        src = snaps.get('dump')
    if src is None: return None
    vals = [src]
    for s in segs:
        vals = walk(vals, s)
    return vals


OPS = ['==', '!=', '<=', '>=', '<', '>', ' contains ', ' exists', ' missing']


def parse_expect(expr):
    m = re.match(r'(count|min|max)\((.+?)\)\s*(==|!=|<=|>=|<|>)\s*(.+)$', expr)
    if m: return (m.group(1), m.group(2).strip(), m.group(3), literal(m.group(4)))
    for op in OPS:
        if op.strip() in ('exists', 'missing') and expr.endswith(op.strip()):
            return ('path', expr[:-len(op.strip())].strip(), op.strip(), None)
        if op in expr:
            a, b = expr.split(op, 1)
            return ('path', a.strip(), op.strip(), literal(b))
    raise ValueError('cannot read check: ' + expr)


def test(kind, path, op, want, snaps):
    vals = resolve(path, snaps)
    if vals is None: return False, 'no ' + (split_path(path)[0] if path else '?') + ' output'
    if kind == 'count':
        got = len(vals[0]) if len(vals) == 1 and isinstance(vals[0], list) else len(vals); vals = [got]
    elif kind in ('min', 'max'):   # 0.10.6: every value at least (min) / at most (max)
        nums = [v for v in vals if isinstance(v, (int, float)) and not isinstance(v, bool)]
        if not nums: return False, 'no numbers'
        vals = [min(nums) if kind == 'min' else max(nums)]
    if op == 'exists': return len(vals) > 0, vals[:3]
    if op == 'missing': return len(vals) == 0, vals[:3]
    def one(v):
        try:
            if op == '==': return v == want or (isinstance(v, float) and isinstance(want, (int, float)) and abs(v - want) < 1e-6)
            if op == '!=': return v != want
            if op == 'contains': return (isinstance(v, str) and str(want) in v) or (isinstance(v, list) and want in v)
            if op == '<': return v < want
            if op == '<=': return v <= want
            if op == '>': return v > want
            if op == '>=': return v >= want
        except TypeError:
            return False
        return False
    return any(one(v) for v in vals), (vals if len(vals) <= 3 else vals[:3] + ['...'])


def run_game(cmd, cwd):
    """Run the game. Windows Smart App Control / App Control can refuse a freshly built unsigned exe (WinError 4551,
    "An Application Control policy has blocked this file"); it often lets it through a moment later, so retry."""
    import time
    for attempt in range(5):
        try:
            return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=600)
        except OSError as e:
            if getattr(e, 'winerror', None) not in (4551, 1260) and 'Application Control' not in str(e): raise
            if attempt == 4:
                sys.exit('arc_check: Windows blocked %s (Smart App Control / App Control policy: the exe is new and '
                         'unsigned). Run the check again; if it keeps happening, Smart App Control can be set to Off in '
                         'Windows Security > App & browser control.' % cmd[0])
            print('arc_check: Windows blocked the new exe (App Control); trying again in %d s' % (2 + 2 * attempt))
            time.sleep(2 + 2 * attempt)


def build_project(check_file):
    """`game .`: find the game folder (with arc.txt) at or above the check file, build it headless with its own
    build script and return the headless exe."""
    d = os.path.dirname(os.path.abspath(check_file))
    while not os.path.exists(os.path.join(d, 'arc.txt')):
        up = os.path.dirname(d)
        if up == d: sys.exit('arc_check: `game .` needs the check file inside a game folder (one with arc.txt)')
        d = up
    name = None
    for f in ('build.sh', 'build.bat'):
        p = os.path.join(d, f)
        if os.path.exists(p):
            m = re.search(r'name=([A-Za-z0-9_-]+)', open(p, encoding='utf-8', errors='replace').read())
            if m: name = m.group(1); break
    if not name: sys.exit('arc_check: no name= in the game\'s build.sh')
    if os.name == 'nt':
        r = subprocess.run(['cmd', '/c', 'build.bat', 'headless'], cwd=d, capture_output=True, text=True)
        exe = os.path.join(d, 'build', name + '_headless.exe')
    else:
        r = subprocess.run(['sh', 'build.sh', 'headless'], cwd=d, capture_output=True, text=True)
        exe = os.path.join(d, 'build', name + '_headless')
    if r.returncode: print(r.stdout + r.stderr); sys.exit('arc_check: build failed')
    return exe


PATH_FLAGS = ('--save-dir', '--record', '--replay', '--script', '--editor', '--dump-at-exit', '--net-report', '--trace')


def fix_args(args, base):
    """0.10.12: a relative path given to a path flag (`--save-dir test_saves`, `--record=a.rec`) is read from the check
    file's folder, not from the folder the game runs in (its exe's)."""
    out, i = [], 0
    while i < len(args):
        x = args[i]
        if x in PATH_FLAGS and i + 1 < len(args):
            v = args[i + 1]; out += [x, v if os.path.isabs(v) else os.path.join(base, v)]; i += 2; continue
        k, eq, v = x.partition('=')
        if eq and k in PATH_FLAGS and v and not os.path.isabs(v): x = k + '=' + os.path.join(base, v)
        out.append(x); i += 1
    return out


def build_source(src):
    name = os.path.splitext(os.path.basename(src))[0]
    if os.name == 'nt':   # Windows: build.bat makes the window build, which runs headless too
        r = subprocess.run(['cmd', '/c', os.path.join(ROOT, 'build.bat'), src], cwd=ROOT, capture_output=True, text=True)
        exe = os.path.join(ROOT, 'build', name + '.exe')
    else:
        r = subprocess.run(['sh', os.path.join(ROOT, 'build.sh'), 'headless', src], cwd=ROOT, capture_output=True, text=True)
        exe = os.path.join(ROOT, 'build', name + '_headless')
    if r.returncode: print(r.stdout + r.stderr); sys.exit('arc_check: build failed')
    return exe


def plan_block(lines, st, a):
    """Turn one block's lines into commands and expectations, appending to the run state st (cmds, plan, snap counts).
    Returns the block's own exe / args lines (None when it has none)."""
    exe, args, have_args = None, [], False
    for line in lines:
        word, _, rest = line.partition(' ')
        if word == 'game' and rest.strip() in ('', '.'):   # 0.9.1: the game project this check file is in
            if a.exe: continue
            if '.' not in st['built']: st['built']['.'] = build_project(a.checks)
            exe = st['built']['.']
        elif word == 'game':
            if a.exe: continue
            src = rest.strip()
            if src not in st['built']: st['built'][src] = build_source(src)
            exe = st['built'][src]
        elif word == 'exe': exe = exe or rest.strip()
        elif word == 'args': args += shlex.split(rest); have_args = True
        elif word == 'continue': pass
        elif word == 'do': st['cmds'].append(rest.strip()); st['taken'] = None
        elif word == 'expect':
            kind, path, op, want = parse_expect(rest.strip())
            root = split_path(path)[0] if split_path(path) else 'dump'
            if root == 'sheet':   # 0.10.6: the last `sheet` command's result (cells[*].filled, label_ink, on_screen)
                st['plan'].append((rest.strip(), kind, path, op, want, {'sheet': st['count']['sheet'] - 1})); continue
            src = root if root in ('frame', 'ui') else 'dump'
            if st['taken'] is None: st['taken'] = {}
            if src not in st['taken']:
                st['cmds'].append(src); st['taken'][src] = st['count'][src]; st['count'][src] += 1
            st['plan'].append((rest.strip(), kind, path, op, want, dict(st['taken'])))
        else:
            st['cmds'].append(line); st['taken'] = None
            if word == 'sheet': st['count']['sheet'] += 1
    return exe, (args if have_args else None)


def run_group(exe, args, st, a):
    """One game process: play the group's commands, read its snapshots, test its expectations."""
    if not os.path.isabs(exe) and not os.path.exists(exe) and not os.path.exists(exe + '.exe'):
        exe = os.path.join(os.path.dirname(os.path.abspath(a.checks)), exe)   # 0.10.10: relative to the check file
    exe = os.path.abspath(exe)
    if not os.path.exists(exe) and os.path.exists(exe + '.exe'): exe += '.exe'
    if not os.path.exists(exe): sys.exit('arc_check: no such game: ' + exe)
    fixed = fix_args(args, os.path.dirname(os.path.abspath(a.checks)))
    tmp_saves = None
    if not any(x == '--save-dir' or x.startswith('--save-dir=') for x in fixed):   # 0.10.14: each game starts with no saves (a best score from one block can't leak into the next)
        tmp_saves = tempfile.mkdtemp(prefix='arc_check_saves_'); fixed = fixed + ['--save-dir', tmp_saves]
    cmd = [exe, '--headless'] + fixed + ['--cmd', '; '.join(st['cmds'] + ['quit'])]
    try:
        r = run_game(cmd, os.path.dirname(os.path.abspath(exe)) or '.')
    finally:
        if tmp_saves: shutil.rmtree(tmp_saves, ignore_errors=True)
    snaps = {'dump': [], 'frame': [], 'ui': [], 'sheet': []}
    errors, hints = [], []
    for l in r.stdout.splitlines():
        for k in ('dump', 'frame', 'ui', 'sheet'):
            if l.startswith('ARC %s ' % k) and not l.startswith('ARC %s %s' % (k, '-')):
                try: snaps[k].append(json.loads(l[len('ARC %s ' % k):]))
                except ValueError: pass
        if l.startswith('ARC error'): errors.append(l)
        if l.startswith('ARC hint'): hints.append(l)
    if a.verbose: print(r.stdout)
    if r.returncode not in (0,) and not errors: errors.append('ARC error the game exited with code %d' % r.returncode)
    return snaps, errors, hints


def main():
    ap = argparse.ArgumentParser(description='Run ARC acceptance checks', formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('checks')
    ap.add_argument('--exe', help='the headless game to run (else the check file\'s game/exe line)')
    ap.add_argument('--block', type=int, help='run only block N (counting from 1)')
    ap.add_argument('--name', help='run only the blocks whose heading contains TEXT')
    ap.add_argument('--fail-fast', action='store_true', help='stop at the first block that fails')
    ap.add_argument('--keep-going', action='store_true', help='(kept for scripts; every check always runs)')
    ap.add_argument('-v', '--verbose', action='store_true', help='show the game\'s output')
    a = ap.parse_args()
    blocks = read_blocks(a.checks)
    chosen = [i for i, (name, _) in enumerate(blocks)
              if (a.block is None or i + 1 == a.block) and (a.name is None or a.name.lower() in name.lower())]
    if not chosen: sys.exit('arc_check: no block matches (%d in the file)' % len(blocks))
    multi = len(blocks) > 1
    # groups: a block that says `continue` carries on in the previous block's game process
    groups = []
    for i in chosen:
        cont = any(l.split(' ')[0] == 'continue' for l in blocks[i][1])
        if cont and groups and groups[-1][-1] == i - 1: groups[-1].append(i)
        else: groups.append([i])
    built, default_exe, default_args = {}, a.exe, []
    total = failed = blocks_ok = blocks_run = 0
    n_errors = 0
    for g in groups:
        st = {'cmds': [], 'plan': [], 'count': {'dump': 0, 'frame': 0, 'ui': 0, 'sheet': 0}, 'taken': None, 'built': built}
        exe, args, starts = None, None, []
        for i in g:
            starts.append(len(st['plan']))
            e, ar = plan_block(blocks[i][1], st, a)
            exe = exe or e; args = args if args is not None else ar
        exe = a.exe or exe or default_exe
        if args is None: args = default_args
        if not exe: sys.exit('arc_check: no game: give --exe or a `game` / `exe` line')
        default_exe, default_args = exe, args   # later blocks without their own game / args use these
        snaps, errors, hints = run_group(exe, args, st, a)
        starts.append(len(st['plan']))
        for bi, i in enumerate(g):
            name = blocks[i][0]
            if multi: print('--- block %d%s' % (i + 1, ': ' + name if name else ''))
            bf = 0
            for expr, kind, path, op, want, idx in st['plan'][starts[bi]:starts[bi + 1]]:
                cur = {k: (snaps[k][j] if j < len(snaps[k]) else None) for k, j in idx.items()}
                ok, got = test(kind, path, op, want, cur)
                print(('PASS ' if ok else 'FAIL ') + expr + ('' if ok else '   (got %s)' % json.dumps(got)))
                bf += not ok
            n = starts[bi + 1] - starts[bi]
            bad = bf or (errors and bi == len(g) - 1)
            if bi == len(g) - 1:
                for h in hints: print(h)
                for e in errors: print(e)
            if multi: print('block %d %s: %d/%d checks%s' % (i + 1, 'FAIL' if bad else 'ok', n - bf, n, ', %d errors' % len(errors) if errors and bi == len(g) - 1 else ''))
            total += n; failed += bf; blocks_run += 1; blocks_ok += not bad
        n_errors += len(errors)
        if a.fail_fast and blocks_ok < blocks_run: break
    tail = ', %d/%d blocks passed' % (blocks_ok, blocks_run) if multi else ''
    print('arc_check: %d/%d checks passed%s%s' % (total - failed, total, ', %d errors' % n_errors if n_errors else '', tail))
    sys.exit(0 if blocks_ok == blocks_run and not failed else 1)


if __name__ == '__main__':
    main()
