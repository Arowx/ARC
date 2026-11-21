#!/usr/bin/env python3
"""Import assets into an ARC game in one step (0.9.0): convert, put in the right folder, preview, report.

    python3 tools/arc_import.py FILE [FILE ...] [--game DIR] [options]
    python3 tools/arc_import.py art/ship.glb --height 2                 -> assets/meshes/ship.arcm + preview
    python3 tools/arc_import.py art/*.png --compress                    -> assets/textures/*.arct
    python3 tools/arc_import.py art/hit.wav art/theme.ogg               -> assets/sfx/hit.wav, assets/music/theme.qoa
    python3 tools/arc_import.py art/Inter.ttf --sizes 16,32             -> assets/fonts/inter_16.arcf, inter_32.arcf

What each kind of file becomes (the folder is under DIR/assets, DIR = --game, default the current folder):
  .glb .gltf .obj (.fbx etc. with trimesh)  meshes/NAME.arcm  via mesh2arcm.py (--height/--footprint/--yaw/--flat,
                                            0.9.8 --fit X Y Z / --fit-uniform X Y Z / --origin bottom|centre|back pass
                                            through; the final bounds are in the report and --json "bounds"); --anim NAME.glb ... adds an .arca of clips);
                                            --compress: block-compressed textures inside (texcompress.py --arcm);
                                            0.10.14 --texture-size N: textures at most N pixels a side (512 for props)
  .png .jpg .jpeg .qoi .tga .bmp .webp      textures/NAME.png (or .jpg kept as is); larger than --max-size are
                                            shrunk; --compress: textures/NAME.arct (BC1/BC3, mipmapped);
                                            --normal: a normal map (BC5 when compressed)
  .wav .mp3 .qoa                            sfx/NAME.ext copied as is (ARC loads them); --music puts them in music/
  .ogg .flac .m4a .aac .opus                converted with audio_convert.py (QOA, or WAV if QOA would hiss);
                                            longer than 20 s -> music/, else sfx/
  .ttf .otf                                 fonts/NAME_SIZE.arcf for each of --sizes (default 16,32) via font_bake.py

0.10.14: every import is recorded in DIR/assets/manifest.json (the source file name, the options, the clips and any
--prompt "how it was made"). `--manifest --sources art/` imports the whole set again from the originals in art/ with
the options it lists (after changing one, or with fresh art made from the same prompts). --no-manifest skips recording.

Afterwards, every mesh, texture and font is opened in tools/arc_view.c (built once into build/arc_view) and a
preview picture is written next to the report (DIR/build/import/NAME.png), so you can look at what you imported
without running the game. --no-preview skips that.

The report (printed, and --json for machines) gives for each file: where it went, its size, what the viewer
measured (triangles, bounds, texture size) and the line of C that loads it. Problems (a mesh far from the origin,
a huge texture, a file the engine cannot read) are listed as warnings. Exit code 1 if any file failed.

Options:  --name NAME (single file)   --into SUBFOLDER (instead of meshes/ textures/ ...)   --force (overwrite)
          --dry-run (say what would happen, write nothing)
"""
import argparse, json, os, re, shutil, struct, subprocess, sys, wave

TOOLS = os.path.dirname(os.path.abspath(__file__))
ARC = os.path.dirname(TOOLS)
PY = sys.executable or 'python3'

MESH = {'.glb', '.gltf', '.obj', '.fbx', '.dae', '.ply', '.stl', '.3ds'}
IMG = {'.png', '.jpg', '.jpeg', '.qoi', '.tga', '.bmp', '.webp', '.gif'}
SND_NATIVE = {'.wav', '.mp3', '.qoa'}
SND_CONVERT = {'.ogg', '.flac', '.m4a', '.aac', '.opus'}
FONT = {'.ttf', '.otf'}


def safe_name(path):
    n = os.path.splitext(os.path.basename(path))[0].lower()
    return re.sub(r'[^a-z0-9_]+', '_', n).strip('_') or 'asset'


def run(cmd, dry):
    if dry: return True, ''
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode == 0, (p.stdout + p.stderr).strip()


def human(n):
    for u in ('B', 'KB', 'MB', 'GB'):
        if n < 1024 or u == 'GB': return ('%d %s' % (n, u)) if u == 'B' else ('%.1f %s' % (n, u))
        n /= 1024.0


def wav_seconds(path):
    try:
        with wave.open(path) as w: return w.getnframes() / float(w.getframerate())
    except Exception: return None


def probe_seconds(path):
    try:
        out = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', path],
                             capture_output=True, text=True).stdout.strip()
        return float(out)
    except Exception: return None


_viewer_error = ''

def viewer_exe():
    """build/arc_view(.exe), built once (rebuilt when arc_view.c or the engine is newer). Tries each compiler found
    (cl first on Windows), and keeps the last compiler's output for the warning when none works."""
    global _viewer_error
    exe = os.path.join(ARC, 'build', 'arc_view' + ('.exe' if os.name == 'nt' else ''))
    src = [os.path.join(TOOLS, 'arc_view.c'), os.path.join(ARC, 'src', 'arc.c')]
    if os.path.exists(exe) and os.path.getmtime(exe) >= max(os.path.getmtime(s) for s in src): return exe
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    gcc_like = [c for c in [os.environ.get('CC'), 'cc', 'gcc', 'clang'] if c and shutil.which(c)]
    tries = []
    if os.name == 'nt' and shutil.which('cl'):
        objdir = os.path.join(ARC, 'build', 'arc_view_obj'); os.makedirs(objdir, exist_ok=True)
        tries.append(['cl', '/nologo', '/O2', '/MT', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS', '/DARC_HEADLESS_ONLY',
                      src[0], src[1], '/Fo:' + objdir + os.sep, '/Fe:' + exe])
    for cc in dict.fromkeys(gcc_like):
        tries.append([cc, '-std=c11', '-O2', '-D_GNU_SOURCE', '-DARC_HEADLESS_ONLY', src[0], src[1], '-o', exe, '-lm'])
    if not tries:
        _viewer_error = 'no C compiler found for the preview viewer (cl, cc, gcc or clang)'; return None
    for cmd in tries:
        try:
            p = subprocess.run(cmd, capture_output=True, text=True)
        except OSError as e:
            _viewer_error = '%s: %s' % (cmd[0], e); continue
        if p.returncode == 0 and os.path.exists(exe): return exe
        out = [l for l in (p.stdout + p.stderr).splitlines() if l.strip()]
        _viewer_error = 'the preview viewer did not build with %s: %s' % (cmd[0], ' | '.join(out[-3:]) or 'exit %d' % p.returncode)
    return None


def preview(asset, out_png):
    exe = viewer_exe()
    if not exe: return None, _viewer_error
    d, f = os.path.split(os.path.abspath(asset))
    p = subprocess.run([exe, '--headless', '--view', f, '--cmd', 'step 2; shot %s; dump; quit' % os.path.abspath(out_png)],
                       cwd=d, capture_output=True, text=True, timeout=120)
    info = None
    for line in p.stdout.splitlines():
        if line.startswith('ARC dump '):
            try: info = json.loads(line[9:]).get('game')
            except Exception: pass
    if not info or info.get('kind') == 'none': return info, 'ARC could not load it'
    return info, None


# ---- 0.10.14: assets/manifest.json remembers how every asset was imported (and made), so the whole set can be imported
# again in one go (new sizes, a fixed facing, fresh art from the same prompts) and a person or agent can see where each
# file came from. {"version": 1, "assets": {"meshes/beetle.arcm": {"source": "beetle.glb", "options": [...],
# "anims": ["idle.glb", ...], "prompt": "..."}}}
def import_options(a):
    o = []
    for flag, v in (('--into', a.into), ('--height', a.height), ('--footprint', a.footprint), ('--yaw', a.yaw),
                    ('--origin', a.origin), ('--texture-size', a.texture_size or None)):
        if v is not None: o += [flag, ('%g' % v) if isinstance(v, float) else str(v)]
    for flag, v in (('--fit', a.fit), ('--fit-uniform', a.fit_uniform)):
        if v: o += [flag] + ['%g' % x for x in v]
    for flag, v in (('--flat', a.flat), ('--compress', a.compress), ('--normal', a.normal), ('--music', a.music)):
        if v: o.append(flag)
    if a.max_size != 2048: o += ['--max-size', str(a.max_size)]
    if a.sizes != '16,32': o += ['--sizes', a.sizes]
    return o


def manifest_path(game): return os.path.join(game, 'assets', 'manifest.json')


def load_manifest(game):
    try:
        with open(manifest_path(game), encoding='utf-8') as f: m = json.load(f)
        if isinstance(m.get('assets'), dict): return m
    except (OSError, ValueError): pass
    return {'version': 1, 'assets': {}}


def record_manifest(a, game, assets, results):
    m = load_manifest(game); changed = False
    for r in results:
        if 'error' in r or not r['outputs']: continue
        key = os.path.relpath(r['outputs'][0], assets).replace(os.sep, '/')
        e = {'source': os.path.basename(r['source']), 'options': import_options(a)}
        if a.anim: e['anims'] = [os.path.basename(x) for x in a.anim]
        old = m['assets'].get(key, {})
        if a.prompt or old.get('prompt'): e['prompt'] = a.prompt or old['prompt']
        if old != e: m['assets'][key] = e; changed = True
    if changed:
        m['assets'] = dict(sorted(m['assets'].items()))
        with open(manifest_path(game), 'w', encoding='utf-8', newline='\n') as f: json.dump(m, f, indent=1); f.write('\n')


def reimport_manifest(a):
    game = os.path.abspath(a.game); m = load_manifest(game)
    src_dir = os.path.abspath(a.sources or os.path.join(game, 'art'))
    if not m['assets']: sys.exit('arc_import: %s lists no assets' % manifest_path(game))
    done = skipped = failed = 0
    for key, e in m['assets'].items():
        if not e.get('source'):   # made outside arc_import (sounds, painted textures): 'made' says how
            print('skip %s: not imported with arc_import (%s)' % (key, e.get('made', 'no source'))); continue
        src = os.path.join(src_dir, e.get('source', ''))
        if not os.path.isfile(src): print('skip %s: %s is not in %s' % (key, e.get('source'), src_dir)); skipped += 1; continue
        cmd = [sys.executable, os.path.abspath(__file__), src, '--game', game, '--force', '--no-manifest',
               '--name', os.path.splitext(os.path.basename(key))[0]] + e.get('options', [])
        if e.get('anims'): cmd += ['--anim'] + [os.path.join(src_dir, x) for x in e['anims']]
        if a.no_preview: cmd.append('--no-preview')
        if a.dry_run: cmd.append('--dry-run')
        p = subprocess.run(cmd, capture_output=True, text=True)
        line = next((l for l in p.stdout.splitlines() if l.startswith(('ok', 'FAIL', 'plan'))), (p.stdout + p.stderr).strip()[-200:])
        print(line); done += p.returncode == 0; failed += p.returncode != 0
    print('arc_import --manifest: %d imported, %d skipped (source not found), %d failed' % (done, skipped, failed))
    sys.exit(1 if failed else 0)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*')
    ap.add_argument('--game', default='.')
    ap.add_argument('--name'); ap.add_argument('--into')
    ap.add_argument('--height', type=float); ap.add_argument('--footprint', type=float)
    ap.add_argument('--yaw', type=float); ap.add_argument('--flat', action='store_true')
    ap.add_argument('--fit', type=float, nargs=3, metavar=('X', 'Y', 'Z')); ap.add_argument('--fit-uniform', type=float, nargs=3, metavar=('X', 'Y', 'Z'))
    ap.add_argument('--origin', choices=['bottom', 'centre', 'center', 'back'])
    ap.add_argument('--anim', nargs='+', metavar='CLIP')
    ap.add_argument('--compress', action='store_true'); ap.add_argument('--normal', action='store_true')
    ap.add_argument('--max-size', type=int, default=2048)
    ap.add_argument('--texture-size', type=int, default=0, help='models: shrink their textures to at most this many pixels a side (512 suits small props)')
    ap.add_argument('--music', action='store_true')
    ap.add_argument('--sizes', default='16,32')
    ap.add_argument('--no-preview', action='store_true'); ap.add_argument('--force', action='store_true')
    ap.add_argument('--dry-run', action='store_true'); ap.add_argument('--json', action='store_true')
    ap.add_argument('--prompt', help='0.10.14: how the asset was made (a generator prompt, an artist, a URL): kept in assets/manifest.json')
    ap.add_argument('--no-manifest', action='store_true', help='0.10.14: do not record this import in assets/manifest.json')
    ap.add_argument('--manifest', action='store_true', help='0.10.14: import again every asset assets/manifest.json lists, from --sources')
    ap.add_argument('--sources', help='0.10.14: with --manifest, the folder holding the original files (default GAME/art)')
    a = ap.parse_args()
    if a.manifest: return reimport_manifest(a)
    if not a.files: ap.error('give files to import (or --manifest)')
    if a.name and len(a.files) > 1: ap.error('--name needs exactly one file')

    game = os.path.abspath(a.game)
    assets = os.path.join(game, 'assets')
    prev_dir = os.path.join(game, 'build', 'import')
    results, failed = [], 0

    for src in a.files:
        r = {'source': src, 'outputs': [], 'warnings': []}
        results.append(r)
        if not os.path.isfile(src):
            r['error'] = 'file not found'; failed += 1; continue
        ext = os.path.splitext(src)[1].lower()
        name = a.name or safe_name(src)
        def dest(sub, fname):
            folder = os.path.join(assets, a.into or sub)
            if not a.dry_run: os.makedirs(folder, exist_ok=True)
            p = os.path.join(folder, fname)
            if os.path.exists(p) and not a.force and not a.dry_run and os.path.abspath(p) != os.path.abspath(src):
                r['warnings'].append('replaced existing %s' % os.path.relpath(p, game))
            return p
        ok, out = True, ''
        if ext in MESH:
            r['kind'] = 'mesh'
            dst = dest('meshes', name + '.arcm')
            cmd = [PY, os.path.join(TOOLS, 'mesh2arcm.py'), src, dst]
            if a.height: cmd += ['--height', str(a.height)]
            if a.footprint: cmd += ['--footprint', str(a.footprint)]
            if a.yaw: cmd += ['--yaw', str(a.yaw)]
            if a.flat: cmd += ['--flat']
            if a.fit: cmd += ['--fit'] + [str(v) for v in a.fit]
            if a.fit_uniform: cmd += ['--fit-uniform'] + [str(v) for v in a.fit_uniform]
            if a.origin: cmd += ['--origin', a.origin]
            if a.texture_size: cmd += ['--texture-size', str(a.texture_size)]   # 0.10.14
            if a.anim:
                arca = dest('meshes', name + '.arca'); cmd += ['--anim', arca] + a.anim; r['outputs'].append(arca)
            ok, out = run(cmd, a.dry_run)
            bm = re.search(r'bounds min (\S+) (\S+) (\S+) max (\S+) (\S+) (\S+) size (\S+) (\S+) (\S+)', out or '')
            if bm:
                v = [round(float(x), 6) for x in bm.groups()]
                r['bounds'] = {'min': v[0:3], 'max': v[3:6], 'size': v[6:9]}
            if ok and a.compress: ok, out = run([PY, os.path.join(TOOLS, 'texcompress.py'), '--arcm', dst, dst], a.dry_run)
            r['outputs'].insert(0, dst)
            r['load'] = 'arc_mesh m = arc_mesh_load("%s");' % os.path.relpath(dst, game).replace(os.sep, '/')
            if a.anim: r['load'] += '  arc_anim an = arc_anim_load("%s");' % os.path.relpath(r['outputs'][1], game).replace(os.sep, '/')
        elif ext in IMG:
            r['kind'] = 'texture'
            if a.compress:
                dst = dest('textures', name + '.arct')
                tmp = None
                cmd = [PY, os.path.join(TOOLS, 'texcompress.py'), src, dst] + (['--normal'] if a.normal else [])
                ok, out = run(cmd, a.dry_run)
            else:
                keep_jpg = ext in ('.jpg', '.jpeg')
                dst = dest('textures', name + ('.jpg' if keep_jpg else '.png'))
                if not a.dry_run:
                    try:
                        from PIL import Image
                        im = Image.open(src); w, h = im.size
                        if max(w, h) > a.max_size > 0:
                            s = a.max_size / float(max(w, h)); im = im.resize((max(1, int(w * s)), max(1, int(h * s))), Image.LANCZOS)
                            r['warnings'].append('shrunk from %dx%d (--max-size %d)' % (w, h, a.max_size))
                        elif ext in ('.png', '.jpg', '.jpeg', '.qoi'):
                            im = None
                        if im is None: shutil.copyfile(src, dst)
                        elif keep_jpg: im.convert('RGB').save(dst, quality=90)
                        else: im.convert('RGBA').save(dst, optimize=True)
                    except ImportError:
                        if ext in ('.png', '.jpg', '.jpeg', '.qoi'): shutil.copyfile(src, dst)
                        else: ok, out = False, 'needs Pillow to convert %s (pip install pillow)' % ext
                    except Exception as e:
                        ok, out = False, str(e)
            r['outputs'].append(dst)
            flags = "ARC_TEX_MESH"
            r['load'] = 'arc_texture t = arc_texture_load("%s");   /* or arc_texture_load_ex(path, %s) for meshes */' % (
                os.path.relpath(dst, game).replace(os.sep, '/'), flags)
        elif ext in SND_NATIVE or ext in SND_CONVERT:
            r['kind'] = 'sound'
            secs = wav_seconds(src) if ext == '.wav' else probe_seconds(src)
            music = a.music or (secs is not None and secs > 20)
            sub = 'music' if music else 'sfx'
            if ext in SND_NATIVE:
                dst = dest(sub, name + ext)
                if not a.dry_run: shutil.copyfile(src, dst)
                if ext == '.wav':
                    try:
                        with wave.open(src) as w:
                            if w.getsampwidth() != 2: ok, out = False, 'WAV is %d-bit: ARC reads 16-bit WAV (convert with audio_convert.py)' % (8 * w.getsampwidth())
                    except Exception as e: ok, out = False, 'not a readable WAV: %s' % e
            else:
                folder = os.path.join(assets, a.into or sub)
                if not a.dry_run: os.makedirs(folder, exist_ok=True)
                ok, out = run([PY, os.path.join(TOOLS, 'audio_convert.py'), src, '--out', folder], a.dry_run)
                base = os.path.splitext(os.path.basename(src))[0]
                dst = next((os.path.join(folder, base + e) for e in ('.qoa', '.wav') if os.path.exists(os.path.join(folder, base + e))),
                           os.path.join(folder, base + '.qoa'))
            r['outputs'].append(dst)
            if secs is not None: r['seconds'] = round(secs, 2)
            rel = os.path.relpath(dst, game).replace(os.sep, '/')
            r['load'] = ('arc_music mu = arc_music_load("%s");  arc_music_play(mu, true);' % rel) if music else \
                        ('arc_sound s = arc_sound_load("%s");  arc_play(s, 1.0f, 0.0f);' % rel)
        elif ext in FONT:
            r['kind'] = 'font'
            folder = os.path.join(assets, a.into or 'fonts')
            if not a.dry_run: os.makedirs(folder, exist_ok=True)
            ok, out = run([PY, os.path.join(TOOLS, 'font_bake.py'), src, folder, '--sizes', a.sizes, '--name', name], a.dry_run)
            for sz in a.sizes.split(','): r['outputs'].append(os.path.join(folder, '%s_%s.arcf' % (name, sz.strip())))
            r['load'] = 'arc_font f = arc_font_load("%s");' % os.path.relpath(r['outputs'][0], game).replace(os.sep, '/')
        else:
            r['error'] = 'unknown kind of file (%s)' % ext; failed += 1; continue
        if not ok:
            r['error'] = out.splitlines()[-1] if out else 'conversion failed'; r['log'] = out; failed += 1; continue
        if a.dry_run: continue
        for o in r['outputs']:
            if os.path.exists(o): r.setdefault('bytes', 0); r['bytes'] += os.path.getsize(o)
        if r.get('bytes', 0) > 8 << 20: r['warnings'].append('large (%s): consider --compress or a smaller --max-size' % human(r['bytes']))
        if r['kind'] in ('mesh', 'texture', 'font') and not a.no_preview:
            os.makedirs(prev_dir, exist_ok=True)
            png = os.path.join(prev_dir, name + ('_%s' % r['kind'] if r['kind'] != 'mesh' else '') + '.png')
            info, err = preview(r['outputs'][0], png)
            if err: r['warnings'].append('preview: ' + err)
            if info:
                r['measured'] = {k: v for k, v in info.items() if k not in ('path', 'kind')}
                if os.path.exists(png): r['preview'] = png
                if r['kind'] == 'mesh' and 'lo' in info:
                    lo, hi = info['lo'], info['hi']; size = [hi[i] - lo[i] for i in range(3)]
                    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
                    if max(size) > 0 and (abs(cx) > max(size) or abs(cz) > max(size)):
                        r['warnings'].append('far from the origin (centre %.1f, %.1f): try --footprint or --height to recentre' % (cx, cz))
                    if max(size) > 500 or (0 < max(size) < 0.01):
                        r['warnings'].append('size %.3g looks wrong for ARC units (1 = 1 metre): try --height N' % max(size))
                    if info.get('triangles', 0) > 200000:
                        r['warnings'].append('%d triangles: heavy for the web build' % info['triangles'])
                if r['kind'] == 'texture' and 'w' in info and (info['w'] & (info['w'] - 1) or info['h'] & (info['h'] - 1)):
                    r['warnings'].append('%dx%d is not a power of two: fine for sprites, but mipmaps/repeat on meshes work best with powers of two' % (info['w'], info['h']))

    if not a.dry_run and not a.no_manifest: record_manifest(a, game, assets, results)
    if a.json:
        print(json.dumps({'game': game, 'dry_run': a.dry_run, 'files': results, 'failed': failed}, indent=1))
    else:
        for r in results:
            tag = 'FAIL' if 'error' in r else ('plan' if a.dry_run else 'ok')
            outs = ', '.join(os.path.relpath(o, game) for o in r['outputs']) or '-'
            print('%-4s %s -> %s%s' % (tag, r['source'], outs, ('  (%s)' % human(r['bytes'])) if r.get('bytes') else ''))
            if 'error' in r: print('     error: ' + r['error'])
            m = r.get('measured')
            if m: print('     ' + ', '.join('%s %s' % (k, v) for k, v in m.items()))
            if r.get('bounds'): b = r['bounds']; print('     bounds min %g %g %g  max %g %g %g  size %g x %g x %g' % (*b['min'], *b['max'], *b['size']))
            if r.get('seconds') is not None: print('     %.2f s' % r['seconds'])
            for w in r['warnings']: print('     warning: ' + w)
            if r.get('preview'): print('     preview: ' + os.path.relpath(r['preview'], game))
            if r.get('load') and 'error' not in r: print('     load: ' + r['load'])
        print('arc_import: %d file(s), %d failed%s' % (len(results), failed, ' (dry run: nothing written)' if a.dry_run else ''))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
