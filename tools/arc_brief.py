#!/usr/bin/env python3
"""A short brief of an ARC game for an AI agent to read first (ARC 0.10.16): what the game is, where things are, what
it dumps, which commands and tunables it has, what it uses, and whether its checks pass. Instead of many reads of
main.c at the start of every session, one page.

    python3 <ARC>/tools/arc_brief.py [GAME_DIR | FILE.c] [--out BRIEF.md] [--json] [--check] [--no-run] [--no-net]

GAME_DIR is a game made by new_game.py (the folder with arc.txt; default: the current folder). FILE.c is a single
source in ARC's repo (a sample, a test game). The brief lists:

  - the title, the ARC it builds against (arc.txt, or a vendored arc/ folder), that ARC's version, and the version
    the game assumes (the highest ARC_HAS_* macro it uses);
  - source files with line counts, and where init / tick / draw / dump / command / save_state / load_state live;
  - the dump() schema: the fields of a real headless `dump` after `step 60` when a headless build exists (--no-run:
    read from the source instead);
  - debug commands handled in command(), tunables (arc_var names and defaults);
  - named meshes, assets (by kind), and the engine features it uses (board, nav, physics, net, editor...);
  - with --check, each acceptance block's result (tools/arc_check.py; builds the game headless);
  - open request issues for this game on GitHub (`gh`, as tools/arc_request.py; --no-net skips it).

The markdown is meant to stay under ~150 lines. --json prints the same facts as JSON. Standard library only.
"""
import argparse, glob, json, os, re, subprocess, sys

ARC_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ARC_ROOT, 'tools'))

CALLBACKS = ('init', 'tick', 'draw', 'dump', 'command', 'save_state', 'load_state', 'shutdown', 'net_event', 'net_input')
FEATURES = [   # (name, regex in the game's code)
    ('board (grid tactics)', r'\barc_board_'), ('navigation', r'\barc_nav(mesh|_agent)_'), ('physics', r'\barc_physics_'),
    ('ragdoll', r'\barc_ragdoll_'), ('animation graph', r'\barc_anim(ator|_graph)_'), ('skinned animation', r'\barc_anim_(clip|pose)|arc_draw_mesh_posed'),
    ('terrain', r'\barc_terrain_'), ('scatter', r'\barc_scatter_'), ('tile editor', r'\barc_editor_begin'), ('level files', r'\barc_level_'),
    ('brains (AI states)', r'\barc_brain_'), ('senses', r'\barc_senses_'), ('shots', r'\barc_shots?_'), ('camera rigs', r'\barc_cam_rig'),
    ('lights', r'\barc_light_(point|spot)'), ('post effects', r'\barc_set_post\b'), ('particles', r'\barc_(fx_|particles_|shots_fx)'),
    ('UI kit', r'\barc_ui_'), ('split-screen', r'\barc_viewport_begin'), ('render targets', r'\barc_target_|arc_render_target'),
    ('fog of war', r'\barc_set_world_mask'), ('markers', r'\barc_draw_marker'), ('popups', r'\barc_popup_text'),
    ('glow masks', r'emissive_mask|\.glow\['), ('music', r'\barc_music_'), ('sound', r'\barc_play'), ('saves', r'\barc_save\b|arc_load_save'),
    ('play sessions', r'\.sessions\s*=\s*true'), ('immediate mode', r'\.immediate\s*=\s*true'), ('touch', r'\barc_touch_'),
    ('gamepads', r'\barc_pad_'), ('net: turns', r'\barc_net_(command|turn_end)'), ('net: rollback', r'ARC_NET_ROLLBACK'),
    ('net: online relay', r'\barc_net_(host|join)_online'),
]


def read(p):
    try:
        with open(p, encoding='utf-8', errors='replace') as f: return f.read()
    except OSError: return ''


def find_arc(game_dir, single):
    """(path to the ARC this game builds against, how it was found)"""
    if single: return ARC_ROOT, 'this ARC (a file in its repo)'
    t = os.path.join(game_dir, 'arc.txt')
    if os.path.exists(t):
        p = read(t).splitlines()[0].strip() if read(t).strip() else ''
        if p:
            q = p if os.path.isabs(p) else os.path.normpath(os.path.join(game_dir, p))
            if os.path.exists(os.path.join(q, 'include', 'arc.h')): return q, 'arc.txt -> ' + p
            return None, 'arc.txt -> %s (not found)' % p
    if os.path.exists(os.path.join(game_dir, 'arc', 'include', 'arc.h')): return os.path.join(game_dir, 'arc'), 'vendored arc/'
    if os.environ.get('ARC_DIR'): return os.environ['ARC_DIR'], '$ARC_DIR'
    return None, 'not found (no arc.txt)'


def arc_macros(arc):
    h = read(os.path.join(arc, 'include', 'arc.h')) if arc else ''
    ver = re.search(r'#define ARC_VERSION\s+"([^"]+)"', h)
    return (ver.group(1) if ver else None), {m.group(1): int(m.group(2)) for m in re.finditer(r'#define (ARC_HAS_\w+)\s+(\d+)', h)}


def api_versions(arc):
    """{function: ARC_VERSION_NUM it arrived in}, from arc.h: the ARC_HAS_* macros' comments (exact), then the version
    noted in the comment just above each declaration (0.10.14 ...), so a game's calls say which ARC it needs."""
    h = read(os.path.join(arc, 'include', 'arc.h')) if arc else ''
    out = {}
    def vnum(s):
        a, b, c = (int(x) for x in s.split('.'))
        return a * 10000 + b * 100 + c if a else b * 100 + c
    lines = h.splitlines()
    for i, l in enumerate(lines):
        m = re.match(r'ARC_API\b[^(]*?\b(arc_\w+)\s*\(', l)
        if not m: continue
        best, j, block = 0, i, [l]
        while j > 0 and (lines[j - 1].strip().startswith(('/*', '*')) or lines[j - 1].rstrip().endswith('*/') or
                         (lines[j - 1].startswith(' ') and not lines[j - 1].strip().startswith('ARC_API'))) and i - j < 10:
            j -= 1; block.append(lines[j])
            if lines[j].lstrip().startswith('/*'): break
        for v in re.findall(r'\b0\.\d{1,2}\.\d{1,2}\b', ' '.join(block)): best = max(best, vnum(v))
        if best: out[m.group(1)] = best
    for m in re.finditer(r'#define ARC_HAS_\w+\s+(\d+)\s*/\*(.*?)\*/', h):
        for f in re.findall(r'\barc_\w+', m.group(2)): out[f] = int(m.group(1))
    return out


def num_ver(n):   # 1015 -> 0.10.15, 998 -> 0.9.8
    return '%d.%d.%d' % (n // 10000, n // 100 % 100, n % 100) if n >= 10000 else '0.%d.%d' % (n // 100, n % 100)


def sources(game_dir, single):
    if single: return [single]
    fs = []
    for pat in ('src/**/*.c', 'src/**/*.h', 'src/**/*.cpp', '*.c'):
        fs += glob.glob(os.path.join(game_dir, pat), recursive=True)
    return sorted(set(fs))


def body_of(text, name):
    """The text of function `name` (from its definition to the matching brace), or ''."""
    m = re.search(r'^[^\n;{}]*\b%s\s*\([^;{]*\)\s*\{' % re.escape(name), text, re.M)
    if not m: return ''
    i, depth = m.end(), 1
    while i < len(text) and depth:
        c = text[i]
        if c == '{': depth += 1
        elif c == '}': depth -= 1
        i += 1
    return text[m.start():i]


def where(texts, name):
    """file:line of the definition of function `name`."""
    for f, t in texts.items():
        m = re.search(r'^[^\n;{}()]*\b%s\s*\([^;{]*\)\s*\{' % re.escape(name), t, re.M)
        if m: return '%s:%d' % (f, t.count('\n', 0, m.start()) + 1)
    return None


def callbacks(texts):
    """{callback: (function name, file:line)} from the arc_app arc_main returns."""
    out = {}
    for f, t in texts.items():
        m = re.search(r'arc_main\s*\([^)]*\)\s*\{', t)
        if not m: continue
        body = body_of(t[m.start():], 'arc_main')
        for cb in CALLBACKS:
            mm = re.search(r'\.%s\s*=\s*&?(\w+)' % cb, body)
            if mm: out[cb] = (mm.group(1), where(texts, mm.group(1)))
    for cb in CALLBACKS:   # no arc_main field (rare): a function with that very name
        if cb not in out and cb in ('init', 'tick', 'draw', 'dump', 'command'):
            w = where(texts, cb)
            if w: out[cb] = (cb, w)
    return out


def title_of(texts, game_dir):
    for t in texts.values():
        m = re.search(r'\.title\s*=\s*"([^"]*)"', t)
        if m: return m.group(1)
    return os.path.basename(os.path.abspath(game_dir))


def project_name(game_dir):
    m = re.search(r'name=([A-Za-z0-9_-]+)', read(os.path.join(game_dir, 'build.sh')) or read(os.path.join(game_dir, 'build.bat')))
    return m.group(1) if m else None


def headless_exe(game_dir, single, arc):
    ext = '.exe' if os.name == 'nt' else ''
    if single:
        stem = os.path.splitext(os.path.basename(single))[0]
        cands = [os.path.join(ARC_ROOT, 'build', stem + '_headless' + ext)]
    else:
        n = project_name(game_dir)
        cands = [os.path.join(game_dir, 'build', n + '_headless' + ext)] if n else []
    return next((c for c in cands if os.path.exists(c)), None)


def schema(v, depth=0):
    """A compact description of a dump value: {"x": "num", "units": [{"hp": "num", ...}] ...}"""
    if isinstance(v, bool): return 'bool'
    if isinstance(v, (int, float)): return 'num'
    if isinstance(v, str): return 'str'
    if v is None: return 'null'
    if isinstance(v, list):
        if not v: return '[]'
        return [schema(v[0], depth + 1), len(v)] if depth < 3 else 'list[%d]' % len(v)
    if isinstance(v, dict):
        return {k: schema(x, depth + 1) for k, x in v.items()} if depth < 4 else 'obj'
    return type(v).__name__


def schema_lines(s, indent='  ', key=''):
    """The schema as short lines: `units[6]: kind str, hp num, ...`"""
    out = []
    if isinstance(s, dict):
        flat = [(k, v) for k, v in s.items() if isinstance(v, str)]
        if flat: out.append(indent + (key + ': ' if key else '') + ', '.join('%s %s' % kv for kv in flat))
        for k, v in s.items():
            if not isinstance(v, str): out += schema_lines(v, indent + ('  ' if key else ''), k)
    elif isinstance(s, list):
        inner, n = s
        if isinstance(inner, dict): out.append(indent + '%s[%d]: %s' % (key, n, ', '.join('%s %s' % (k, v if isinstance(v, str) else '...') for k, v in inner.items())))
        else: out.append(indent + '%s[%d]: %s' % (key, n, inner))
    return out


def live_dump(exe):
    try:
        r = subprocess.run([exe, '--headless', '--cmd', 'step 60; dump; quit'], cwd=os.path.dirname(exe), capture_output=True,
                           text=True, errors='replace', timeout=120)
    except (OSError, subprocess.TimeoutExpired) as e:
        return None, str(e)
    for l in r.stdout.splitlines():
        if l.startswith('ARC dump {'):
            try: return json.loads(l[9:]), None
            except ValueError: pass
    return None, 'no dump line (exit %s)' % r.returncode


def source_dump_fields(texts, fn):
    body = ''
    for t in texts.values():
        body = body_of(t, fn) if fn else ''
        if body: break
    return list(dict.fromkeys(re.findall(r'arc_json_\w+_k\s*\(\s*\w+\s*,\s*"([^"]+)"', body)))


def commands(texts, fn):
    """[(name, needs N words)] from the command callback."""
    body = ''
    for t in texts.values():
        body = body_of(t, fn) if fn else ''
        if body: break
    out = []
    for line in body.splitlines():
        for m in re.finditer(r'strcmp\s*\(\s*\w+\s*,\s*"([^"]+)"\s*\)', line):
            if 'argv' in line[:m.start()].split('strcmp')[-1]: continue
            n = re.search(r'argc\s*>=?\s*(\d+)', line)
            out.append((m.group(1), int(n.group(1)) + (0 if '>=' in n.group(0) else 1) if n else 0))
    seen, res = set(), []
    for c in out:
        if c[0] not in seen: seen.add(c[0]); res.append(c)
    return res


def tunables(texts):
    out = []
    for t in texts.values():
        for m in re.finditer(r'arc_var\s*\(\s*"([^"]+)"\s*,[^,]+,\s*([^)]+)\)', t):
            out.append((m.group(1), m.group(2).strip()))
    return list(dict.fromkeys(out))


def assets_summary(game_dir, texts, single):
    names = []
    for t in texts.values(): names += re.findall(r'arc_mesh_set_name\s*\(\s*[^,]+,\s*"([^"]+)"', t)
    loaded = set()
    for t in texts.values(): loaded |= set(re.findall(r'"(assets/[^"%]+)"', t))
    kinds = {}
    adir = os.path.join(game_dir, 'assets') if not single else None
    files = []
    if adir and os.path.isdir(adir):
        for d, _, fs in os.walk(adir):
            files += [os.path.relpath(os.path.join(d, f), adir).replace('\\', '/') for f in fs if f != 'manifest.json' and not f.startswith('.')]
    for f in files or [p[len('assets/'):] for p in loaded]:
        k = f.split('/')[0] if '/' in f else os.path.splitext(f)[1].lstrip('.') or 'other'
        kinds[k] = kinds.get(k, 0) + 1
    return list(dict.fromkeys(names)), kinds, len(loaded), os.path.exists(os.path.join(adir, 'manifest.json')) if adir else False


def features(texts):
    allt = '\n'.join(texts.values())
    return [n for n, rx in FEATURES if re.search(rx, allt)]


def template_of(game_dir):
    for f in ('NOTES.md', 'AGENTS.md'):
        m = re.search(r'started from the (\w+) template', read(os.path.join(game_dir, f)))
        if m: return m.group(1)
    return None


def check_files(game_dir, single):
    if single: return []
    return [f for f in sorted(glob.glob(os.path.join(game_dir, 'tests', '*.md')) + glob.glob(os.path.join(game_dir, 'tests', '*.txt')))
            if '```arc-check' in read(f) or f.endswith('.txt')]


def run_checks(path):
    """[(block name, ok, passed, total)], summary line"""
    r = subprocess.run([sys.executable, os.path.join(ARC_ROOT, 'tools', 'arc_check.py'), path], capture_output=True, text=True, errors='replace')
    blocks, cur = [], None
    for l in r.stdout.splitlines():
        m = re.match(r'--- block (\d+): (.*)', l)
        if m: cur = m.group(2).strip(); continue
        m = re.match(r'block (\d+) (ok|FAIL)\w*:? (\d+)/(\d+)', l)
        if m: blocks.append((cur or 'block ' + m.group(1), m.group(2) == 'ok', int(m.group(3)), int(m.group(4))))
    last = (r.stdout.strip().splitlines() or ['(no output)'])[-1]
    if not blocks:   # one-block files print only the summary
        m = re.search(r'(\d+)/(\d+) checks', last)
        if m: blocks.append(('checks', r.returncode == 0, int(m.group(1)), int(m.group(2))))
    return blocks, last, r.returncode == 0


def open_requests(game):
    try:
        import arc_request
        ok, res = arc_request.gh_api(['repos/%s/issues?labels=request&state=open&per_page=100' % os.environ.get('ARC_REPO', 'Arowx/arc')])
    except Exception as e:  # no gh, no network: say so
        return None, str(e)
    if not ok or not isinstance(res, list): return None, 'GitHub not reachable (gh)'
    g = (game or '').lower()
    return [(i['number'], i['title']) for i in res if g and ('[%s]' % g) in i['title'].lower()], None


def brief(target, check=False, run=True, net=True):
    target = os.path.abspath(target)
    single = target if os.path.isfile(target) else None
    game_dir = os.path.dirname(target) if single else target
    arc, arc_how = find_arc(game_dir, single)
    arc_ver, macros = arc_macros(arc)
    files = sources(game_dir, single)
    base = ARC_ROOT if single else game_dir
    texts = {os.path.relpath(f, base).replace('\\', '/'): read(f) for f in files}
    used = sorted({m for t in texts.values() for m in re.findall(r'\bARC_HAS_\w+', t)})
    needs = max((macros.get(m, 0) for m in used), default=0)
    av = api_versions(arc)
    calls = {f for t in texts.values() for f in re.findall(r'\b(arc_\w+)\s*\(', t)}
    dated = sorted(((av[f], f) for f in calls if f in av), reverse=True)
    newest = [f for v, f in dated if dated and v == dated[0][0]][:4]
    calls_need = dated[0][0] if dated else 0
    cbs = callbacks(texts)
    exe = headless_exe(game_dir, single, arc) if run else None
    dump, why = (live_dump(exe) if exe else (None, 'no headless build (build it: ./build.sh headless)' if run else '--no-run'))
    title = title_of(texts, game_dir)
    names, kinds, nloaded, manifest = assets_summary(game_dir, texts, single)
    B = {
        'title': title, 'game_dir': game_dir, 'file': single, 'name': project_name(game_dir) if not single else os.path.splitext(os.path.basename(single))[0],
        'template': template_of(game_dir) if not single else None,
        'arc': {'path': arc, 'found': arc_how, 'version': arc_ver, 'macros_used': used,
                'needs': num_ver(needs) if needs else None,
                'calls_need': num_ver(calls_need) if calls_need else None, 'newest_calls': newest},
        'sources': [{'file': f, 'lines': t.count('\n') + 1} for f, t in texts.items()],
        'callbacks': {k: {'function': v[0], 'at': v[1]} for k, v in cbs.items()},
        'dump': {'from': 'headless run (step 60)' if dump else 'source', 'exe': exe, 'note': None if dump else why},
        'commands': [{'name': n, 'words': w} for n, w in commands(texts, cbs.get('command', (None,))[0])],
        'tunables': [{'name': n, 'default': d} for n, d in tunables(texts)],
        'meshes': names, 'assets': kinds, 'assets_named_in_code': nloaded, 'manifest': manifest,
        'features': features(texts), 'checks': [], 'requests': None,
    }
    if dump:
        B['dump']['game'] = schema(dump.get('game', {}))
        B['dump']['fields'] = list(dump.get('game', {}).keys()) if isinstance(dump.get('game'), dict) else []
        B['dump']['tick'] = dump.get('tick')
    else:
        B['dump']['fields'] = source_dump_fields(texts, cbs.get('dump', (None,))[0])
    if check:
        for f in check_files(game_dir, single):
            blocks, last, ok = run_checks(f)
            B['checks'].append({'file': os.path.relpath(f, game_dir).replace('\\', '/'), 'ok': ok, 'summary': last,
                                'blocks': [{'name': b[0], 'ok': b[1], 'passed': b[2], 'total': b[3]} for b in blocks]})
    if net:
        reqs, err = open_requests(B['title'] if single else (B['title'] or B['name']))
        B['requests'] = [{'number': n, 'title': t} for n, t in reqs] if reqs is not None else {'error': err}
    return B


def markdown(B):
    L = ['# Brief: %s' % B['title'], '']
    a = B['arc']
    L.append('- **ARC:** %s, %s%s' % (a['found'], ('version ' + a['version']) if a['version'] else 'version unknown',
             ('; the game checks for %s or later (%s)' % (a['needs'], ', '.join(m[8:] for m in a['macros_used']))) if a['needs'] else ''))
    if a.get('calls_need'): L.append('- **Needs ARC %s or later:** its newest engine calls are %s' % (a['calls_need'], ', '.join('`%s`' % f for f in a['newest_calls'])))
    if B['template']: L.append('- **Started from** the %s template (see NOTES.md).' % B['template'])
    if B['file']: L.append('- **File:** %s (in ARC\'s repo)' % B['file'])
    L.append('- **Sources:** ' + ', '.join('%s (%d)' % (s['file'], s['lines']) for s in B['sources'][:12]) + (' ...' if len(B['sources']) > 12 else ''))
    L += ['', '## Callbacks', '']
    for cb in CALLBACKS:
        c = B['callbacks'].get(cb)
        if c: L.append('- `%s`: `%s()` at %s' % (cb, c['function'], c['at'] or '?'))
    missing = [cb for cb in ('dump', 'command', 'save_state') if cb not in B['callbacks']]
    if missing: L.append('- none: ' + ', '.join('`%s`' % m for m in missing) + ('  (no save_state: rewind and state_check can\'t work)' if 'save_state' in missing else ''))
    d = B['dump']
    L += ['', '## dump() (%s)' % d['from'], '']
    if d.get('game') is not None:
        L.append('`game`, after %s ticks:' % d.get('tick'))
        L += [l for l in schema_lines(d['game'])][:40]
    elif d['fields']:
        L.append('fields written (from the source; %s): %s' % (d['note'], ', '.join(d['fields'][:60])))
    else:
        L.append('no fields found (%s)' % d['note'])
    L += ['', '## Commands (`command()`)', '']
    L.append(', '.join('`%s%s`' % (c['name'], ' %s' % ' '.join('A%d' % (i + 1) for i in range(c['words'])) if c['words'] else '') for c in B['commands']) or 'none')
    L += ['', '## Tunables (`arc_var`)', '']
    L.append(', '.join('`%s` = %s' % (t['name'], t['default']) for t in B['tunables']) or 'none')
    L += ['', '## Content', '']
    L.append('- **Named meshes:** ' + (', '.join(B['meshes'][:30]) or 'none'))
    L.append('- **Assets:** ' + (', '.join('%s %d' % kv for kv in sorted(B['assets'].items())) or 'none') +
             ('; assets/manifest.json says how each was made' if B['manifest'] else ''))
    L.append('- **Uses:** ' + (', '.join(B['features']) or 'nothing special'))
    if B['checks']:
        L += ['', '## Acceptance checks', '']
        for c in B['checks']:
            L.append('- %s: %s' % (c['file'], c['summary']))
            for b in c['blocks']: L.append('  - %s %s (%d/%d)' % ('ok' if b['ok'] else 'FAILED', b['name'], b['passed'], b['total']))
    elif not B['file']:
        L += ['', '## Acceptance checks', '', '(not run: add --check)' if check_files(B['game_dir'], None) else 'no check files in tests/']
    r = B['requests']
    if r is not None:
        L += ['', '## Open ARC requests for this game', '']
        if isinstance(r, dict): L.append('(%s)' % r['error'])
        else: L += ['- #%d %s' % (x['number'], x['title']) for x in r] or ['none']
    return '\n'.join(L) + '\n'


def main():
    ap = argparse.ArgumentParser(description='A short brief of an ARC game, for agents to read first', formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('game', nargs='?', default='.', help='the game folder (with arc.txt), or a .c file in ARC\'s repo')
    ap.add_argument('--out', help='also write the markdown here (e.g. BRIEF.md)')
    ap.add_argument('--json', action='store_true', help='print JSON instead of markdown')
    ap.add_argument('--check', action='store_true', help='run the acceptance checks (builds the game headless)')
    ap.add_argument('--no-run', action='store_true', help='read the dump fields from the source, never run the game')
    ap.add_argument('--no-net', action='store_true', help='skip the GitHub lookup of open requests')
    a = ap.parse_args()
    if not os.path.exists(a.game): sys.exit('no such game: ' + a.game)
    B = brief(a.game, check=a.check, run=not a.no_run, net=not a.no_net)
    md = markdown(B)
    if a.out:
        with open(a.out, 'w', encoding='utf-8', newline='\n') as f: f.write(md)
    print(json.dumps(B, indent=1) if a.json else md, end='' if not a.json else '\n')
    if any(not c['ok'] for c in B['checks']): sys.exit(1)


if __name__ == '__main__':
    main()
