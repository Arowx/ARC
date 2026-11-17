#!/usr/bin/env python3
"""ARC 0.8.6 web loading tests (headless Chromium through tests/web_run.js and Playwright; emsdk; Pillow + numpy).

    python3 tests/web_load_test.py [--no-build] [--only NAME]

Makes test images (JPEG: baseline, greyscale, progressive, EXIF-turned; PNG: RGB, RGBA with every alpha level, palette
with tRNS, an sRGB chunk, 16-bit), a mesh with a JPEG texture and a PNG normal map, and a BC1 .arct, then runs
tests/web_load.c in headless Chromium:
  check-bitmap / check-imagedecoder / check-img   browser decoding with each browser API: the browser's pixels against
                     ARC's (PNG identical, JPEG within a few levels), the loader's GPU-direct textures drawn next to
                     ARC-decoded ones, the mesh's maps, .arct uploaded as blocks when S3TC is there, and ARC's decoder
                     taking over when every browser decode fails
  check-bytes        --browser-decode=bytes: the browser's pixels read back as bytes (the CPU path)
  check-noctex       --no-compressed-textures: the .arct decoded on the CPU and uploaded as RGBA8 (the phones' path)
  check-off          browser decoding off (the default): nothing changes, nothing goes to the browser
  packs              a WEB_PACKS build (tools/web_packs.py): the first package without the models, the loader fetching
                     models.arcpak for files in it, everything ready; then a second visit that reads the pack from the
                     browser's Cache API ("from the cache"), and fetches nothing big
Exit code 0 when every test passed."""
import io, os, re, shutil, struct, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'build', 'webtest_load')
EMCC = ('source /home/claude/emsdk/emsdk_env.sh >/dev/null 2>&1; emcc -std=c11 -Iinclude -Wall -Wextra -O2 -ffp-contract=off tests/web_load.c src/arc.c '
        '-o {out}/load.html --shell-file web/shell.html -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 '
        '-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_arc_web_audio_fill,_arc_web_command -sEXPORTED_RUNTIME_METHODS=lengthBytesUTF8,stringToUTF8,HEAPF32 {pre}')


def make_assets(d):
    from PIL import Image
    import numpy as np
    ck = os.path.join(d, 'first', 'check'); os.makedirs(ck, exist_ok=True)
    rng = np.random.default_rng(7)
    y, x = np.mgrid[0:128, 0:128]
    rgb = np.stack([(x * 2) % 256, (y * 2) % 256, ((x + y) * 3) % 256], -1).astype(np.int32)
    rgb = np.clip(rgb + rng.integers(-20, 20, rgb.shape), 0, 255).astype(np.uint8)
    im = Image.fromarray(rgb, 'RGB')
    im.save(os.path.join(ck, 'photo.jpg'), quality=90)
    im.convert('L').save(os.path.join(ck, 'grey.jpg'), quality=85)
    im.save(os.path.join(ck, 'prog.jpg'), quality=80, progressive=True)
    exif = Image.Exif(); exif[0x0112] = 6
    im.save(os.path.join(ck, 'exif.jpg'), quality=90, exif=exif.tobytes())
    im.save(os.path.join(ck, 'opaque.png'))
    a = np.zeros((64, 64, 4), np.uint8)                   # every alpha level, colours that premultiplying would lose
    a[..., 0] = (np.arange(64)[None, :] * 4 + 3) % 256; a[..., 1] = 255 - a[..., 0]; a[..., 2] = 200
    a[..., 3] = (np.arange(64 * 64).reshape(64, 64) % 256).astype(np.uint8)
    Image.fromarray(a, 'RGBA').save(os.path.join(ck, 'alpha.png'))
    p = Image.fromarray(rgb[:64, :64]).quantize(16); p.info['transparency'] = 3
    p.save(os.path.join(ck, 'pal.png'), transparency=3, bits=8)
    srgb = io.BytesIO(); im.save(srgb, 'PNG'); b = srgb.getvalue()   # the same RGB PNG with an sRGB chunk after IHDR
    import zlib
    chunk = struct.pack('>I', 1) + b'sRGB' + b'\x00' + struct.pack('>I', zlib.crc32(b'sRGB\x00') & 0xffffffff)
    open(os.path.join(ck, 'srgb.png'), 'wb').write(b[:33] + chunk + b[33:])
    Image.fromarray((rgb[:32, :32, 0].astype(np.uint16) * 257)).save(os.path.join(ck, 'deep16.png'))
    # a mesh (.arcm v3): one triangle, texture = photo.jpg, material normal map = alpha.png
    tex = open(os.path.join(ck, 'photo.jpg'), 'rb').read(); nrm = open(os.path.join(ck, 'alpha.png'), 'rb').read()
    verts = b''.join(struct.pack('<3H2b4B2H', px, py, 0, 0, 127, 255, 255, 255, 255, 0x3C00 * u, 0x3C00 * v)
                     for px, py, u, v in ((0, 0, 0, 0), (65535, 0, 1, 0), (0, 65535, 0, 1)))
    mat = b'MATL' + struct.pack('<II7f3I', 1, 0, 1, 0, 1, 1, 0, 0, 0, len(nrm), 0, 0) + nrm
    head = b'ARCM' + struct.pack('<3I', 3, 3, 0) + struct.pack('<4I', 0, len(tex), len(mat), 0) + struct.pack('<6f', -1, -1, 0, 1, 1, 0)
    open(os.path.join(ck, 'mesh.arcm'), 'wb').write(head + verts + tex + mat)
    Image.fromarray(rgb[:64, :64], 'RGB').save(os.path.join(d, 'tmp_bc.png'))
    subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'texcompress.py'), os.path.join(d, 'tmp_bc.png'), os.path.join(ck, 'tex.arct'), '--format', 'bc1'], stdout=subprocess.DEVNULL)
    os.remove(os.path.join(d, 'tmp_bc.png'))
    Image.fromarray(rgb, 'RGB').resize((256, 128)).save(os.path.join(d, 'first', 'title.png'))
    # "models" for the pack test: copies of the check files in their own folder
    md = os.path.join(d, 'models'); os.makedirs(md, exist_ok=True)
    for n in ('photo.jpg', 'opaque.png', 'alpha.png', 'mesh.arcm', 'tex.arct'): shutil.copy(os.path.join(ck, n), os.path.join(md, n))
    open(os.path.join(d, 'first', 'load_list.txt'), 'w').write('\n'.join('assets/models/' + n for n in sorted(os.listdir(md))) + '\n')


def run(page_dir, args, until='ARC test', visits=1, extra=()):
    env = dict(os.environ, NODE_PATH=subprocess.run(['npm', 'root', '-g'], capture_output=True, text=True).stdout.strip())
    r = subprocess.run(['node', os.path.join(ROOT, 'tests', 'web_run.js'), page_dir, 'load.html', '--args', args, '--until', until, '--port', '8794',
                        '--visits', str(visits), '--metrics', '--timeout', '120'] + list(extra), capture_output=True, text=True, env=env, timeout=600)
    return r.returncode, r.stdout


def main():
    only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
    one = os.path.join(OUT, 'one'); split = os.path.join(OUT, 'split')
    if '--no-build' not in sys.argv:
        shutil.rmtree(OUT, ignore_errors=True)
        src = os.path.join(OUT, 'src', 'assets'); make_assets(src)
        os.makedirs(one); os.makedirs(split)
        subprocess.check_call(['bash', '-c', EMCC.format(out=one, pre='--preload-file %s@/assets' % src)], cwd=ROOT)
        subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'web_packs.py'), src, split, os.path.join(OUT, 'stage'), '--packs', 'models'], stdout=subprocess.DEVNULL)
        subprocess.check_call(['bash', '-c', EMCC.format(out=split, pre='--preload-file %s@/ --use-preload-cache' % os.path.join(OUT, 'stage'))], cwd=ROOT)
    fails = 0
    tests = [('check-bitmap', one, '--mute --webload-check --browser-decode --image-decoder=bitmap --expect-s3tc', 1),
             ('check-imagedecoder', one, '--mute --webload-check --browser-decode --image-decoder=imagedecoder', 1),
             ('check-img', one, '--mute --webload-check --browser-decode --image-decoder=img', 1),
             ('check-auto', one, '--mute --webload-check --browser-decode', 1),
             ('check-bytes', one, '--mute --webload-check --browser-decode=bytes', 1),
             ('check-noctex', one, '--mute --webload-check --browser-decode --no-compressed-textures', 1),
             ('check-off', one, '--mute --webload-check --expect-off --expect-s3tc', 1),
             ('check-no', one, '--mute --webload-check --browser-decode --no-browser-decode --expect-off', 1),
             ('packs', split, '--mute --webload-bench --browser-decode', 2)]
    for name, d, args, visits in tests:
        if only and name != only: continue
        rc, out = run(d, args, visits=visits)
        lines = [l for l in out.splitlines() if l.startswith('ARC test') or 'webload' in l or 'FAILED' in l or l.startswith('runner:') or 'pack ' in l or 'browser image' in l]
        if name == 'packs':
            res = re.findall(r'ARC test webload .*files=(\d+) ready=(\d+) failed=(\d+)', out)
            first = re.search(r'pack models\.arcpak: [\d.]+ MB downloaded', out); again = re.search(r'pack models\.arcpak: [\d.]+ MB from the cache', out)
            visit2 = re.search(r'runner: visit 2 done .* files=(\S*)', out)
            ok = rc == 0 and len(res) == 2 and all(int(r) == 5 and int(x) == 0 for f, r, x in res) and first and again and visit2 \
                and 'models.arcpak' not in visit2.group(1).split(',') and 'load.data' not in visit2.group(1).split(',')
        else:
            ok = rc == 0 and re.search(r'ARC test web load OK', out) is not None
        fails += not ok
        print(('ok    ' if ok else 'FAIL  ') + name, flush=True)
        for l in lines: print('      ' + l)
    print('ARC web load test %s: %d failed' % ('FAILED' if fails else 'OK', fails))
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
