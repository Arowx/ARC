#!/usr/bin/env python3
"""ARC's MCP server (0.10.10): lets an AI agent (Claude Code, Claude Desktop, any MCP client) build, run, look at and
edit an ARC game live through typed tools, headless or in a window you can watch.

    python3 <ARC>/tools/arc_mcp.py [--game DIR]          (an MCP server on stdin/stdout; clients start it themselves)
    python3 <ARC>/tools/arc_mcp.py --selftest EXE        (start EXE headless, run each tool once, print the results)

Set it up once per project with a `.mcp.json` next to the game (new_game.py writes one; ARC's repo has its own):

    {"mcpServers": {"arc": {"command": "python", "args": ["<ARC>/tools/arc_mcp.py", "--game", "."]}}}

The game is started with `--stdin` and kept running between calls: step, look, change a tunable, reload, look again.
`arc_start(window=true)` opens the real window (desktop builds, 0.10.10): it keeps drawing while it waits, ticks run
only when asked (`arc_step`) or with `arc_live(run=true)`, so the person can watch and play along.

Every command that changes the game is recorded with each `arc_expect`, and `arc_save_check` writes it all as an
```arc-check file: what was found live becomes a normal headless test (`tools/arc_check.py`), with the same seed and
the same result every run. Prefer `arc_frame` (JSON) to `arc_shot` (a picture costs far more tokens); use a shot when
the look itself matters.

No packages needed (Pillow, when present, shrinks pictures). Works on Windows, Linux and macOS.
"""
import argparse, base64, json, os, queue, re, shlex, subprocess, sys, tempfile, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import arc_check  # noqa: E402  (parse_expect, test: the same rules as acceptance checks)

VERSION = '0.10.10'
LOOKS = {'frame', 'dump', 'ui', 'vars', 'get', 'help', 'stats', 'camera', 'shot', 'live', 'echo', 'editor dump',
         'editor info', 'timeline info', 'timeline marks', 'cells', 'lod', 'net status', 'net players'}


def is_look(cmd):
    w = cmd.split()
    if not w: return True
    if w[0] in ('frame', 'dump', 'ui', 'vars', 'get', 'help', 'stats', 'shot', 'echo', 'live'): return True
    if w[0] == 'camera' and len(w) == 1: return True
    if w[0] in ('editor', 'timeline', 'net', 'lod', 'cells') and len(w) >= 2 and ' '.join(w[:2]) in LOOKS: return True
    if w[0] in ('lod', 'cells') and len(w) == 1: return True
    return False


def split_cmds(text):
    """'a; b' -> ['a', 'b'], keeping ; inside double quotes."""
    out, cur, q = [], '', False
    for ch in text:
        if ch == '"': q = not q
        if ch == ';' and not q:
            if cur.strip(): out.append(cur.strip())
            cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out


class Game:
    def __init__(self, game_dir):
        self.game_dir = game_dir
        self.p = None; self.q = None; self.exe = None; self.args = []; self.window = False
        self.record = []          # ('do', cmd) / ('expect', expr)
        self.last_output = []

    # ---- project ----
    def project_name(self):
        for f in ('build.sh', 'build.bat'):
            p = os.path.join(self.game_dir, f)
            if os.path.exists(p):
                m = re.search(r'name=([A-Za-z0-9_-]+)', open(p, encoding='utf-8', errors='replace').read())
                if m: return m.group(1)
        return None

    def is_arc_repo(self):
        return os.path.exists(os.path.join(self.game_dir, 'include', 'arc.h')) and os.path.exists(os.path.join(self.game_dir, 'tools', 'arc_mcp.py'))

    def default_exe(self, window, source=None):
        ext = '.exe' if os.name == 'nt' else ''
        if source:   # ARC's own repo: build/<name>(_headless)
            name = os.path.splitext(os.path.basename(source))[0]
            return os.path.join(self.game_dir, 'build', name + ('' if window or os.name == 'nt' else '_headless') + ext)
        name = self.project_name()
        if not name: return None
        return os.path.join(self.game_dir, 'build', name + ('' if window else '_headless') + ext)

    def build(self, window, source=None):
        d = self.game_dir
        if source and self.is_arc_repo():
            if os.name == 'nt': cmd = ['cmd', '/c', 'build.bat', source]
            else: cmd = ['sh', 'build.sh', 'linux' if window else 'headless', source]
        elif os.name == 'nt': cmd = ['cmd', '/c', 'build.bat'] + ([] if window else ['headless'])
        else: cmd = ['sh', 'build.sh', 'linux' if window else 'headless']
        r = subprocess.run(cmd, cwd=d, capture_output=True, text=True, errors='replace')
        out = (r.stdout + r.stderr).strip().splitlines()
        warn = [l for l in out if re.search(r'warning|error', l, re.I)]
        return r.returncode == 0, out, warn

    # ---- process ----
    def running(self): return self.p is not None and self.p.poll() is None

    def start(self, exe, args, window, timeout):
        self.stop()
        exe = os.path.abspath(exe)
        if not os.path.exists(exe) and os.path.exists(exe + '.exe'): exe += '.exe'
        if not os.path.exists(exe): raise RuntimeError('no such game: %s (build it first: arc_build)' % exe)
        cmd = [exe, '--stdin'] + ([] if window else ['--headless']) + args
        flags = 0x08000000 if (os.name == 'nt' and not window) else 0   # CREATE_NO_WINDOW for headless on Windows
        self.p = subprocess.Popen(cmd, cwd=os.path.dirname(exe), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', bufsize=1,
                                  creationflags=flags)
        self.q = queue.Queue()
        q, p = self.q, self.p
        threading.Thread(target=lambda: [q.put(l) for l in p.stdout] + [q.put(None)], daemon=True).start()
        self.exe, self.args, self.window = exe, args, window
        self.record = []
        return self.until_ready(timeout)

    def until_ready(self, timeout):
        out, end = [], time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0: out.append('(no answer after %.0f s: the game is still busy; ask again later or arc_stop)' % timeout); break
            try: l = self.q.get(timeout=min(left, 0.5))
            except queue.Empty:
                if not self.running(): out.append('(the game exited, code %s)' % self.p.returncode); break
                continue
            if l is None:
                self.p.wait(5); out.append('(the game exited, code %s)' % self.p.returncode); break
            l = l.rstrip('\r\n')
            if l.startswith('ARC ready'): out.append(l); break
            out.append(l)
        self.last_output = out
        return out

    def send(self, line, timeout=120, record=True):
        if not self.running(): raise RuntimeError('no game running: call arc_start first')
        for c in split_cmds(line):
            if record and not is_look(c) and c.split()[0] not in ('quit',): self.record.append(('do', c))
        self.p.stdin.write(line.replace('\n', ' ') + '\n'); self.p.stdin.flush()
        return self.until_ready(timeout)

    def stop(self):
        if self.running():
            try:
                self.p.stdin.write('quit\n'); self.p.stdin.flush(); self.p.wait(5)
            except Exception:
                self.p.kill()
        self.p = None

    # ---- reading output ----
    @staticmethod
    def json_line(out, kind):
        for l in reversed(out):
            if l.startswith('ARC %s {' % kind) or l.startswith('ARC %s [' % kind):
                try: return json.loads(l[len('ARC %s ' % kind):])
                except ValueError: return None
        return None


def brief(out, keep=60):
    """Command output for the agent: ARC lines, long JSON lines cut."""
    lines = [l if len(l) < 600 else l[:600] + ' ...(%d chars; use arc_frame / arc_dump for the full JSON)' % len(l) for l in out]
    if len(lines) > keep: lines = lines[:10] + ['... (%d lines) ...' % (len(lines) - keep + 10)] + lines[-(keep - 10):]
    return '\n'.join(lines)


def pick_path(obj, path):
    """'meshes', 'meshes[name=coin]', 'game.units[0]': arc_check paths on one JSON value."""
    vals = [obj]
    for s in arc_check.split_path(path): vals = arc_check.walk(vals, s)
    return vals[0] if len(vals) == 1 else vals


TOOLS = [
    ('arc_build', 'Build the game (its build.sh / build.bat). window=false builds the headless exe (fast, for tests); '
                  'true the window build. In ARC\'s own repo give source="tests/target_game.c" or a sample.',
     {'window': ('boolean', 'the window build (default false: headless)'), 'source': ('string', 'ARC repo only: the .c file to build')}, []),
    ('arc_start', 'Start the game and keep it running for the other tools. window=true opens the real window so the '
                  'person can watch (it holds still between calls; arc_step moves it on, arc_live(run=true) lets it run). '
                  'args: extra flags, e.g. "--seed 7 --size 1280x720". exe: a path instead of the project\'s build.',
     {'window': ('boolean', 'open the window (default false: headless)'), 'args': ('string', 'extra command-line flags'),
      'exe': ('string', 'game executable (default: the project\'s build/<name>[_headless])'), 'source': ('string', 'ARC repo only: the .c file (finds its exe)')}, []),
    ('arc_command', 'Run ARC agent commands ("step 30; press D; step 10; release D") exactly as in --cmd scripts and '
                    'return their output. Every command in <ARC>/AGENTS.md works, and the game\'s own commands.',
     {'command': ('string', 'one or more commands separated by ;'), 'timeout': ('number', 'seconds to wait (default 120)')}, ['command']),
    ('arc_step', 'Advance the game N ticks (60 a second), optionally holding keys while it runs, e.g. keys="D SPACE".',
     {'ticks': ('integer', 'ticks to run (default 1)'), 'keys': ('string', 'keys to hold during these ticks (space separated)')}, []),
    ('arc_frame', 'What the screen shows, as JSON: camera, meshes with on-screen boxes, markers, texts, sprites, rects, UI, '
                  'targets. select: an arc_check path to return only part, e.g. "meshes[name=droid]" or "texts".',
     {'select': ('string', 'part to return (default: all)')}, []),
    ('arc_dump', 'The game state as JSON (ARC\'s dump plus the game\'s dump()). select: e.g. "game", "game.units[0]".',
     {'select': ('string', 'part to return (default: all)')}, []),
    ('arc_shot', 'A screenshot of the current state, returned as an image. Costs far more tokens than arc_frame: use it '
                 'when the look matters. Optional camera: "orbit YAW PITCH DIST" for a view from elsewhere (restored after).',
     {'max_width': ('integer', 'shrink to at most this width (default 960; needs Pillow)'), 'hud': ('boolean', 'include the HUD (default true)'),
      'camera': ('string', 'e.g. "orbit 30 25 18" (the `camera` command\'s words)'), 'crop': ('boolean', 'crop to the drawn part')}, []),
    ('arc_pick', 'What is at a screen pixel (x, y from the top left): the meshes, markers, texts, sprites and UI whose '
                 'boxes hold it, smallest first. Use it with what the person points at in the window.',
     {'x': ('number', 'pixel x'), 'y': ('number', 'pixel y')}, ['x', 'y']),
    ('arc_set', 'Change a tunable (arc_var) live: name and value. With no name, list every tunable and its value.',
     {'name': ('string', 'tunable name'), 'value': ('string', 'new value')}, []),
    ('arc_editor', 'Run a tile-editor command (the game must call arc_editor_begin): "on", "paint 3 3 #", "object add 3 3 droid", '
                   '"rename \\"Cargo Hold\\"", "save", "view 3d"... Returns the output and the editor state.',
     {'command': ('string', 'the words after `editor`')}, ['command']),
    ('arc_expect', 'Check facts against the live game with acceptance-check expressions ("game.score == 10", '
                   '"count(frame.markers[*]) >= 3", "frame.texts[*].text contains \\"READY\\""). Each is recorded for arc_save_check.',
     {'expect': ('string', 'one expression per line')}, ['expect']),
    ('arc_live', 'Window builds: run=true lets the game run on its own clock (the person can play), run=false holds it; '
                 'speed scales the clock (0.25..64; also how fast arc_step plays in a window).',
     {'run': ('boolean', 'run on the clock'), 'speed': ('number', 'clock speed')}, []),
    ('arc_reload', 'Reload every asset loaded from disk (start the game with args "--hot" for textures, meshes, sounds, levels).', {}, []),
    ('arc_save_check', 'Write what this session did (every command that changed the game, and each arc_expect) as an '
                       'arc-check file that tools/arc_check.py runs headless. Returns the path and text.',
     {'path': ('string', 'file to write (default tests/live_<time>.md in the game folder)'), 'title': ('string', 'heading')}, []),
    ('arc_stop', 'Quit the running game.', {}, []),
]


class Server:
    def __init__(self, game_dir):
        self.g = Game(game_dir)
        self.shots = os.path.join(tempfile.gettempdir(), 'arc_mcp_%d' % os.getpid())
        os.makedirs(self.shots, exist_ok=True)
        self.nshot = 0

    # each returns a list of MCP content items, or raises
    def t_arc_build(self, a):
        ok, out, warn = self.g.build(bool(a.get('window')), a.get('source'))
        tail = '\n'.join(out[-15:])
        return [txt(('built' if ok else 'BUILD FAILED') + ('' if not warn else '\n%d warning/error lines:\n' % len(warn) + '\n'.join(warn[:30])) + '\n--- last lines ---\n' + tail)], not ok

    def t_arc_start(self, a):
        window = bool(a.get('window'))
        exe = a.get('exe') or self.g.default_exe(window, a.get('source'))
        if not exe: raise RuntimeError('no exe: give exe, or run from a game folder (with build.sh / build.bat)')
        if window and not a.get('exe') and not a.get('source') and os.name != 'nt' and not os.path.exists(exe):
            pass
        out = self.g.start(exe, shlex.split(a.get('args') or '', posix=os.name != 'nt'), window, 60)
        live = ''
        if window:
            o = self.g.send('live', record=False)
            live = '\n' + ('the window holds between calls; arc_step moves it on' if any('window=1' in l for l in o) else
                           'note: this build is waiting on stdin like a headless run (window updates only during steps)')
        return [txt('started %s%s\n%s%s' % (os.path.basename(self.g.exe), ' (window)' if window else ' (headless)', brief(out), live))], False

    def t_arc_command(self, a):
        out = self.g.send(a['command'], float(a.get('timeout') or 120))
        return [txt(brief(out))], any(l.startswith('ARC error') for l in out)

    def t_arc_step(self, a):
        n = int(a.get('ticks') or 1); keys = (a.get('keys') or '').split()
        cmd = '; '.join(['press ' + k for k in keys] + ['step %d' % n] + ['release ' + k for k in keys])
        out = self.g.send(cmd)
        return [txt(brief(out))], any(l.startswith('ARC error') for l in out)

    def look(self, kind, a):
        out = self.g.send(kind, record=False)
        j = Game.json_line(out, kind)
        if j is None: return [txt(brief(out))], True
        if a.get('select'): j = pick_path(j, a['select'])
        return [txt(json.dumps(j, separators=(',', ':')))], False

    def t_arc_frame(self, a): return self.look('frame', a)
    def t_arc_dump(self, a): return self.look('dump', a)

    def t_arc_shot(self, a):
        self.nshot += 1
        path = os.path.join(self.shots, 'shot_%03d.png' % self.nshot)
        pre, post = [], []
        if a.get('hud') is False: pre.append('hud off'); post.append('hud on')
        if a.get('camera'): pre.append('camera ' + a['camera']); post.append('camera off')
        cmd = '; '.join(pre + ['shot "%s"%s' % (path, ' crop' if a.get('crop') else '')] + post)
        out = self.g.send(cmd, record=False)
        if not os.path.exists(path): return [txt('no picture\n' + brief(out))], True
        data, mime, size = shrink(path, int(a.get('max_width') or 960))
        return [{'type': 'image', 'data': base64.b64encode(data).decode(), 'mimeType': mime},
                txt('%s (%s)' % (path, size))], False

    def t_arc_pick(self, a):
        x, y = float(a['x']), float(a['y'])
        out = self.g.send('frame', record=False)
        f = Game.json_line(out, 'frame') or {}
        hits = []
        def inside(b): return isinstance(b, list) and len(b) == 4 and b[0] <= x <= b[2] and b[1] <= y <= b[3]
        def area(b): return (b[2] - b[0]) * (b[3] - b[1])
        for m in f.get('meshes', []):
            for i, it in enumerate(m.get('items', [])):
                if inside(it.get('box')): hits.append((area(it['box']), {'mesh': m.get('name'), 'item': i, **{k: it[k] for k in ('pos', 'box', 'target', 'rgb') if k in it}}))
        for kind in ('markers', 'texts', 'sprites', 'rects', 'ui'):
            for i, it in enumerate(f.get(kind, [])):
                b = it.get('box') or it.get('rect')
                if b and len(b) == 4 and kind == 'ui' and b[2] < b[0] + 0.0001: pass
                if kind == 'ui' and b and len(b) == 4 and not inside(b):
                    bb = [b[0], b[1], b[0] + b[2], b[1] + b[3]]   # ui rects are x, y, w, h
                    if inside(bb): hits.append((area(bb), {kind: i, **it})); continue
                if inside(b): hits.append((area(b), {kind: i, **it}))
        hits.sort(key=lambda h: h[0])
        return [txt(json.dumps([h[1] for h in hits[:12]], separators=(',', ':')) if hits else 'nothing drawn at %g, %g' % (x, y))], False

    def t_arc_set(self, a):
        if not a.get('name'): out = self.g.send('vars', record=False)
        else: out = self.g.send('set %s %s' % (a['name'], a.get('value', '')))
        return [txt(brief(out))], any(l.startswith('ARC error') for l in out)

    def t_arc_editor(self, a):
        out = self.g.send('editor ' + a['command'])
        d = Game.json_line(self.g.send('dump', record=False), 'dump') or {}
        ed = d.get('editor')
        return [txt(brief(out) + ('\neditor: ' + json.dumps(ed, separators=(',', ':')) if ed is not None else '\n(no editor in this game)'))], any(l.startswith('ARC error') for l in out)

    def t_arc_expect(self, a):
        exprs = [e.strip() for e in a['expect'].splitlines() if e.strip()]
        need = set()
        for e in exprs:
            k, path, op, want = arc_check.parse_expect(e)
            r = arc_check.split_path(path)[0] if arc_check.split_path(path) else 'dump'
            need.add(r if r in ('frame', 'ui', 'sheet') else 'dump')
        snaps = {}
        for k in need - {'sheet'}:
            snaps[k] = Game.json_line(self.g.send(k, record=False), k)
        if 'sheet' in need: snaps['sheet'] = Game.json_line(self.g.last_output, 'sheet')
        lines, bad = [], 0
        for e in exprs:
            kind, path, op, want = arc_check.parse_expect(e)
            ok, got = arc_check.test(kind, path, op, want, snaps)
            lines.append(('PASS ' if ok else 'FAIL ') + e + ('' if ok else '   (got %s)' % json.dumps(got)))
            bad += not ok
            self.g.record.append(('expect', e))
        return [txt('\n'.join(lines))], bad > 0

    def t_arc_live(self, a):
        cmds = []
        if 'speed' in a: cmds.append('live speed %g' % float(a['speed']))
        if 'run' in a: cmds.append('live run' if a['run'] else 'live hold')
        out = self.g.send('; '.join(cmds) if cmds else 'live', record=False)
        return [txt(brief(out))], False

    def t_arc_reload(self, a):
        out = self.g.send('reload', record=False)
        return [txt(brief(out))], False

    def t_arc_save_check(self, a):
        g = self.g
        path = a.get('path') or os.path.join(g.game_dir, 'tests', time.strftime('live_%Y%m%d_%H%M%S.md'))
        if not os.path.isabs(path): path = os.path.join(g.game_dir, path)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        exe = g.exe or ''
        if g.window:   # tests run headless: point at the headless build when there is one
            alt = re.sub(r'(\.exe)?$', lambda m: '_headless' + (m.group(1) or ''), exe, count=1) if exe else ''
            if alt and os.path.exists(alt): exe = alt
        proj = os.path.exists(os.path.join(g.game_dir, 'arc.txt')) and exe and \
            os.path.normcase(os.path.abspath(exe)) == os.path.normcase(os.path.abspath(g.default_exe(False) or ''))
        if proj and os.path.normcase(path).startswith(os.path.normcase(g.game_dir)): body = ['game .']   # builds it headless first
        else:
            rel = os.path.relpath(exe, os.path.dirname(path)) if exe and os.path.splitdrive(exe)[0] == os.path.splitdrive(path)[0] else exe
            body = ['exe ' + (rel or 'build/GAME_headless').replace('\\', '/')]
        if g.args: body.append('args ' + ' '.join(shlex.quote(x) for x in g.args))
        dos = []
        def flush():
            if dos: body.append('do ' + '; '.join(dos)); dos.clear()
        for kind, v in g.record:
            if kind == 'do':
                if v.split()[0] == 'expect': continue
                dos.append(v)
            else: flush(); body.append('expect ' + v)
        flush()
        title = a.get('title') or 'Live session ' + time.strftime('%Y-%m-%d %H:%M')
        text = '# %s\n\nRecorded by tools/arc_mcp.py. Run: `python3 <ARC>/tools/arc_check.py %s`\n\n```arc-check\n%s\n```\n' % (
            title, os.path.relpath(path, g.game_dir).replace('\\', '/'), '\n'.join(body))
        open(path, 'w', encoding='utf-8', newline='\n').write(text)
        return [txt('wrote %s\n\n%s' % (path, text))], False

    def t_arc_stop(self, a):
        self.g.stop()
        return [txt('stopped')], False

    # ---- MCP ----
    def handle(self, msg):
        mid, method, params = msg.get('id'), msg.get('method'), msg.get('params') or {}
        if method == 'initialize':
            return ok(mid, {'protocolVersion': params.get('protocolVersion', '2025-06-18'),
                            'capabilities': {'tools': {}},
                            'serverInfo': {'name': 'arc', 'version': VERSION},
                            'instructions': 'ARC game engine tools. arc_build, then arc_start (window=true to show the person), '
                                            'then arc_step / arc_command / arc_frame / arc_dump / arc_shot / arc_expect; '
                                            'arc_save_check turns the session into a headless test. Read <ARC>/AGENTS.md for every command.'})
        if method in ('notifications/initialized', 'notifications/cancelled') or (mid is None and method and method.startswith('notifications/')):
            return None
        if method == 'ping': return ok(mid, {})
        if method == 'tools/list':
            tools = []
            for name, desc, props, req in TOOLS:
                schema = {'type': 'object', 'properties': {k: {'type': t, 'description': d} for k, (t, d) in props.items()}}
                if req: schema['required'] = req
                tools.append({'name': name, 'description': desc, 'inputSchema': schema})
            return ok(mid, {'tools': tools})
        if method == 'tools/call':
            name, args = params.get('name'), params.get('arguments') or {}
            fn = getattr(self, 't_' + str(name), None)
            if not fn: return err(mid, -32602, 'unknown tool ' + str(name))
            try:
                content, is_err = fn(args)
            except Exception as e:  # tool errors go back as results the agent can read
                content, is_err = [txt('ARC mcp error: %s' % e)], True
            return ok(mid, {'content': content, 'isError': bool(is_err)})
        if mid is None: return None
        return err(mid, -32601, 'no method ' + str(method))


def txt(s): return {'type': 'text', 'text': s}
def ok(mid, result): return {'jsonrpc': '2.0', 'id': mid, 'result': result}
def err(mid, code, m): return {'jsonrpc': '2.0', 'id': mid, 'error': {'code': code, 'message': m}}


def shrink(path, max_w):
    raw = open(path, 'rb').read()
    try:
        from PIL import Image
        import io
        im = Image.open(io.BytesIO(raw)); w, h = im.size
        if w > max_w:
            im = im.convert('RGB').resize((max_w, max(1, h * max_w // w)), Image.LANCZOS)
            b = io.BytesIO(); im.save(b, 'PNG', optimize=True)
            return b.getvalue(), 'image/png', '%dx%d shown at %dx%d' % (w, h, im.size[0], im.size[1])
        return raw, 'image/png', '%dx%d' % (w, h)
    except Exception:
        return raw, 'image/png', 'full size'


def serve(game_dir):
    s = Server(game_dir)
    out = sys.stdout
    for line in sys.stdin:
        line = line.strip()
        if not line: continue
        try: msg = json.loads(line)
        except ValueError:
            out.write(json.dumps(err(None, -32700, 'parse error')) + '\n'); out.flush(); continue
        msgs = msg if isinstance(msg, list) else [msg]
        replies = [r for r in (s.handle(m) for m in msgs) if r is not None]
        if replies:
            out.write(json.dumps(replies if isinstance(msg, list) else replies[0]) + '\n'); out.flush()
    s.g.stop()


def selftest(exe, game_dir):
    """Drive the server in-process through MCP messages: each tool once against a headless game."""
    s = Server(game_dir)
    n = [0]
    def call(name, **args):
        n[0] += 1
        r = s.handle({'jsonrpc': '2.0', 'id': n[0], 'method': 'tools/call', 'params': {'name': name, 'arguments': args}})
        res = r['result']; t = ' | '.join(c['text'][:160].replace('\n', ' / ') if c['type'] == 'text' else '<image %d bytes>' % len(c['data']) for c in res['content'])
        print('ARC mcp %-15s %s %s' % (name, 'ERROR' if res['isError'] else 'ok', t))
        return res
    init = s.handle({'jsonrpc': '2.0', 'id': 0, 'method': 'initialize', 'params': {'protocolVersion': '2025-06-18'}})
    tl = s.handle({'jsonrpc': '2.0', 'id': 0, 'method': 'tools/list'})
    print('ARC mcp server %s, %d tools' % (init['result']['serverInfo']['version'], len(tl['result']['tools'])))
    fails = 0
    fails += call('arc_start', exe=exe)['isError']
    fails += call('arc_step', ticks=3)['isError']
    fails += call('arc_frame', select='meshes[name=sentry].on_screen')['isError']
    fails += call('arc_dump', select='tick')['isError']
    r = call('arc_shot', max_width=320); fails += r['isError'] or r['content'][0]['type'] != 'image'
    f = Game.json_line(s.g.send('frame', record=False), 'frame') or {}
    box = f['meshes'][0]['items'][0]['box'] if f.get('meshes') and f['meshes'][0].get('items') else [0, 0, 1, 1]
    r = call('arc_pick', x=(box[0] + box[2]) / 2, y=(box[1] + box[3]) / 2); fails += r['isError'] or 'mesh' not in r['content'][0]['text']
    fails += call('arc_command', command='step 2; echo "two words"')['isError']
    fails += call('arc_expect', expect='tick >= 5\nstats.backend exists\nframe.meshes[name=sentry].on_screen == 1')['isError']
    r = call('arc_expect', expect='tick == 99999'); fails += not r['isError']    # a failing check must say so
    s.g.record.pop()
    path = os.path.join(tempfile.gettempdir(), 'arc_mcp_selftest.md')
    r = call('arc_save_check', path=path); fails += r['isError']
    call('arc_stop')
    rc = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'arc_check.py'), path], capture_output=True, text=True)
    print('ARC mcp saved check: ' + (rc.stdout.strip().splitlines() or ['?'])[-1])
    fails += rc.returncode != 0
    print('ARC ok mcp' if not fails else 'ARC error mcp (%d)' % fails)
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description='ARC MCP server', formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('--game', default='.', help='the game folder (with build.sh / build.bat), or ARC\'s repo')
    ap.add_argument('--selftest', metavar='EXE', help='run every tool once against EXE (headless) and exit')
    a = ap.parse_args()
    game = os.path.abspath(a.game)
    if a.selftest: sys.exit(selftest(a.selftest, game))
    serve(game)


if __name__ == '__main__':
    main()
