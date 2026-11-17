#!/usr/bin/env python3
"""Check an ARC game folder for common problems, each with the fix.

    python3 tools/arc_doctor.py D:/Projects/MyGame         (or . inside the game folder)
    python3 tools/arc_doctor.py MyGame --json               (machine-readable)

Looks at the game's sources (src/, or the folder itself; ARC's own copy in arc/ is skipped), its assets/ and its build
scripts:
  - determinism: rand(), srand(), time(), clock() and friends in game code (replays, tests and multiplayer drift);
  - testability: an arc_app without .dump, files loaded inside tick()/draw();
  - assets: files named in the code that don't exist, very large textures and sounds, uncompressed WAV music;
  - the project: build scripts, AGENTS.md, which ARC it uses (arc.txt) and whether a vendored copy is older.
Exit code 1 when there is an ERROR (WARN and INFO don't fail).
"""
import argparse, json, os, re, struct, sys

ARC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
ASSET_EXT = ('.png', '.jpg', '.jpeg', '.arcm', '.axm', '.arca', '.arct', '.arcf', '.arcpak', '.glb', '.gltf', '.obj',
             '.wav', '.ogg', '.qoa', '.mp3', '.txt', '.json', '.lvl')
SRC_EXT = ('.c', '.cpp', '.cc', '.h', '.hpp')

findings = []


def add(level, where, msg, fix=''):
    findings.append({'level': level, 'where': where, 'msg': msg, 'fix': fix})


def arc_version(header):
    try:
        m = re.search(r'#define ARC_VERSION_NUM (\d+)', open(header, encoding='utf-8', errors='replace').read())
        return int(m.group(1)) if m else None
    except OSError:
        return None


def image_size(path):
    try:
        with open(path, 'rb') as f:
            head = f.read(32)
            if head[:8] == b'\x89PNG\r\n\x1a\n':
                return struct.unpack('>II', head[16:24])
            if head[:2] == b'\xff\xd8':
                f.seek(2)
                while True:
                    b = f.read(1)
                    while b and b != b'\xff': b = f.read(1)
                    while b == b'\xff': b = f.read(1)
                    if not b: return None
                    marker = b[0]
                    if marker in (0xC0, 0xC1, 0xC2):
                        f.read(3); h, w = struct.unpack('>HH', f.read(4)); return (w, h)
                    seg = struct.unpack('>H', f.read(2))[0]; f.seek(seg - 2, 1)
    except (OSError, struct.error):
        return None
    return None


def strip_comments(code):
    code = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), code, flags=re.S)
    return re.sub(r'//[^\n]*', '', code)


def function_bodies(code, names):
    """yields (name, body, line) for C functions with these names (best effort: brace matching)"""
    for m in re.finditer(r'\b(?:static\s+)?void\s+(\w+)\s*\([^;{)]*\)\s*\{', code):
        if m.group(1) not in names: continue
        i, depth = m.end(), 1
        while i < len(code) and depth:
            depth += code[i] == '{'; depth -= code[i] == '}'; i += 1
        yield m.group(1), code[m.end():i], code.count('\n', 0, m.start()) + 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('game', nargs='?', default='.')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args()
    root = os.path.abspath(a.game)
    if not os.path.isdir(root): sys.exit('arc_doctor: no such folder: ' + root)

    # sources
    srcs = []
    for d, dirs, files in os.walk(root):
        rel = os.path.relpath(d, root)
        parts = rel.split(os.sep)
        if parts[0] in ('arc', 'build', 'dist', '.git', 'node_modules') or (len(parts) > 0 and parts[0] == 'assets'):
            dirs[:] = []; continue
        srcs += [os.path.join(d, f) for f in files if f.endswith(SRC_EXT)]
    if not srcs: add('ERROR', root, 'no C/C++ sources found', 'put the game in src/main.c (tools/new_game.py makes a working project)')
    app_found = dump_found = False; immediate = False
    named_assets = {}
    for p in srcs:
        rel = os.path.relpath(p, root)
        raw = open(p, encoding='utf-8', errors='replace').read()
        code = strip_comments(raw)
        for pat, what in ((r'(?<![\w.])rand\s*\(', 'rand()'), (r'(?<![\w.])srand\s*\(', 'srand()'), (r'(?<![\w.])time\s*\(\s*(NULL|0|nullptr)?\s*\)', 'time()'),
                          (r'(?<![\w.])clock\s*\(\s*\)', 'clock()'), (r'GetTickCount|QueryPerformanceCounter|gettimeofday|std::random_device|std::chrono', 'the system clock / std random')):
            for m in re.finditer(pat, code):
                line = code.count('\n', 0, m.start()) + 1
                add('WARN', '%s:%d' % (rel, line), '%s in game code: replays, tests and multiplayer stop matching run to run' % what,
                    'use arc_rand_*() (seeded by --seed) or an arc_rng; read time as arc_tick_count() / arc_dt() (arc_real_time() only for display)')
        if re.search(r'\barc_app\b', code) and re.search(r'\.(tick|draw)\s*=', code):
            app_found = True
            if re.search(r'\.dump\s*=', code): dump_found = True
            if re.search(r'\.immediate\s*=\s*true', code): immediate = True
        for name, body, line in function_bodies(code, ('tick', 'draw', 'update', 'render')):
            for m in re.finditer(r'arc_(texture|mesh|sound|music|font|anim)_load\w*\s*\(', body):
                add('WARN', '%s:%d' % (rel, line + body.count('\n', 0, m.start())), 'a file is loaded inside %s(): it stalls that frame (and every frame if it runs each time)' % name,
                    'load it once in init() and keep the handle, or queue it with arc_load_begin(ARC_LOAD_BACKGROUND)')
            if name in ('tick', 'update') and not immediate and re.search(r'arc_draw_\w+\s*\(', body):
                add('WARN', '%s:%d' % (rel, line), 'arc_draw_* inside %s(): nothing drawn there shows (each frame\'s draw list starts empty)' % name,
                    'draw in draw(), or set .immediate = true in arc_app to draw from tick()')
        for m in re.finditer(r'"([^"\n]+\.(?:%s))"' % '|'.join(e[1:] for e in ASSET_EXT), raw, re.I):
            named_assets.setdefault(m.group(1), '%s:%d' % (rel, raw.count('\n', 0, m.start()) + 1))
    if srcs and not app_found: add('INFO', root, 'no arc_app with .tick/.draw found (maybe built some other way)')
    if app_found and not dump_found:
        add('ERROR', root, 'the arc_app has no .dump: agents and tests can only check what the dump holds',
            'add static void dump(arc_json *j) { arc_json_num_k(j, "score", score); ... } and .dump = dump')

    # assets named in the code
    for name, where in sorted(named_assets.items()):
        if '%' in name or '*' in name: continue
        cands = [os.path.join(root, name), os.path.join(root, 'assets', name), os.path.join(root, 'build', name)]
        if not any(os.path.exists(c) for c in cands):
            add('ERROR', where, 'asset "%s" not found (looked in the game folder and assets/)' % name, 'add the file to assets/ or fix the name (paths are relative to assets/ at run time)')

    # assets on disk
    total = 0; wav_big = []
    adir = os.path.join(root, 'assets')
    for d, _, files in os.walk(adir):
        for f in files:
            p = os.path.join(d, f); rel = os.path.relpath(p, root); size = os.path.getsize(p); total += size
            low = f.lower()
            if low.endswith(('.png', '.jpg', '.jpeg')):
                wh = image_size(p)
                if wh and max(wh) > 2048:
                    add('WARN', rel, 'texture %dx%d: slow to load and %d MB of GPU memory' % (wh[0], wh[1], wh[0] * wh[1] * 4 * 4 // 3 // 1048576),
                        'resize to 2048 or less, or let the game cap it with arc_set_texture_max_size(1024); tools/texcompress.py makes BC/.arct files 4-8x smaller on the GPU')
            if low.endswith('.wav') and size > 2 * 1048576: wav_big.append((rel, size))
            if size > 32 * 1048576: add('WARN', rel, 'file of %d MB' % (size // 1048576), 'web players download it before playing: put it in a web pack (docs/WEB.md) or shrink it')
    for rel, size in wav_big:
        add('WARN', rel, 'WAV of %.1f MB' % (size / 1048576), 'tools/audio_convert.py makes .qoa (about 5x smaller, ARC decodes it) or use .mp3 for music')
    if total > 64 * 1048576:
        add('INFO', 'assets/', 'assets total %d MB' % (total // 1048576), 'for web builds list big folders in WEB_PACKS (build.bat) so the first download stays small')

    # project files
    for f, why in (('build.bat', 'Windows builds'), ('build.sh', 'Linux/web builds'), ('AGENTS.md', 'notes for AI agents')):
        if not os.path.exists(os.path.join(root, f)):
            add('WARN', root, 'no %s (%s)' % (f, why), 'python tools/new_game.py %s --update-scripts writes the build scripts; copy AGENTS.md from a new project' % root)
    arc_txt = os.path.join(root, 'arc.txt'); arc_dir = None
    if os.path.exists(arc_txt):
        arc_dir = open(arc_txt, encoding='utf-8').read().strip()
        if not os.path.isabs(arc_dir): arc_dir = os.path.join(root, arc_dir)
        mine = arc_version(os.path.join(ARC_ROOT, 'include', 'arc.h')); theirs = arc_version(os.path.join(arc_dir, 'include', 'arc.h'))
        if theirs is None: add('ERROR', 'arc.txt', 'arc.txt points at %s, which has no include/arc.h' % arc_dir, 'write the path of the ARC folder in arc.txt')
        elif mine and theirs < mine and os.path.abspath(arc_dir) != ARC_ROOT:
            add('INFO', 'arc.txt', 'the game uses ARC %d.%d.%d (vendored or another copy); this ARC is %d.%d.%d' % (theirs // 100 // 10 % 10, theirs // 10 % 10, theirs % 10, mine // 100 // 10 % 10, mine // 10 % 10, mine % 10),
                'to update a vendored copy: python tools/new_game.py %s --vendor (or point arc.txt at this ARC)' % root)
    else:
        add('INFO', root, 'no arc.txt (the build scripts made by tools/new_game.py read ARC\'s location from it)')

    order = {'ERROR': 0, 'WARN': 1, 'INFO': 2}
    findings.sort(key=lambda x: order[x['level']])
    if a.json:
        print(json.dumps({'game': root, 'findings': findings}, indent=1))
    else:
        for f in findings:
            print('%-5s %s: %s' % (f['level'], f['where'], f['msg']))
            if f['fix']: print('      fix: ' + f['fix'])
        n = {k: sum(1 for f in findings if f['level'] == k) for k in order}
        print('arc_doctor: %d errors, %d warnings, %d notes (%d source files)' % (n['ERROR'], n['WARN'], n['INFO'], len(srcs)))
    sys.exit(1 if any(f['level'] == 'ERROR' for f in findings) else 0)


if __name__ == '__main__':
    main()
