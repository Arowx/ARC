#!/usr/bin/env python3
"""Compiler warnings as a test stage (ARC 0.9.8, after MSVC's C4244 warnings kept turning up on Allan's PC).

    python3 tools/warn_check.py --msvc          (build.bat test: real MSVC, /W3 /WX: any warning fails)
    python3 tools/warn_check.py                 (build.sh test: clang's conversion warnings against a baseline)
    python3 tools/warn_check.py --update        (clang: accept today's warnings as the baseline)
    python3 <ARC>/tools/warn_check.py src/*.c   (a game's own files: clang, or --msvc; every warning counts, no baseline)

It compiles every program that ships with ARC: the engine (src/arc.c, windowed and headless), every sample, every
genre template and the test programs. Only MSVC knows exactly what MSVC warns about, so on Windows the stage is the
real compiler with warnings as errors, and each warning is printed with its file and line.

On Linux, clang's nearest warnings stand in: int and 64-bit values turned into float, double into float, size_t into int
(-Wimplicit-int-float-conversion -Wimplicit-float-conversion -Wfloat-conversion -Wshorten-64-to-32). Older code has
some of these, so they are kept in tools/warn_baseline.txt (by file, warning and the text of the line, not line
numbers, so edits elsewhere don't disturb them); only new ones fail. clang does not catch everything MSVC does (an
int constant in a ?: assigned to a float, for one): the MSVC stage is the real safety net.
Prints "ARC ok warnings ..." or "ARC error warnings ..." (exit code 1).
"""
import glob, os, re, shutil, subprocess, sys

ARC = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ARC, 'tools', 'warn_baseline.txt')
CLANG_FLAGS = ['-Wimplicit-int-float-conversion', '-Wimplicit-float-conversion', '-Wfloat-conversion', '-Wshorten-64-to-32']


GAME_FILES = [a for a in sys.argv[1:] if not a.startswith('--')]


def units():
    """(source, extra defines) for every program: the engine twice, samples, templates, test programs (or the files given)"""
    if GAME_FILES: return [(os.path.abspath(f), []) for f in GAME_FILES]
    u = [('src/arc.c', []), ('src/arc.c', ['ARC_HEADLESS_ONLY'])]
    u += [(p, []) for p in sorted(glob.glob(os.path.join(ARC, 'samples', '*', '*.c')))]
    u += [(p, []) for p in sorted(glob.glob(os.path.join(ARC, 'templates', '*', 'main.c')))]
    u += [('tests/' + n, ['ARC_HEADLESS_ONLY']) for n in ('test_core.c', 'test_post.c', 'check_game.c', 'det_check.c', 'headless_game.c')]
    u += [('tools/arc_view.c', [])]
    return [(os.path.join(ARC, p), d) for p, d in u]


def msvc():
    if not shutil.which('cl'):
        print('ARC error warnings: cl not found (run from build.bat, which sets up MSVC)'); return 1
    out_dir = os.path.join(ARC, 'build', 'warn'); os.makedirs(out_dir, exist_ok=True)
    bad, n = [], 0
    for src, defs in units():
        cmd = ['cl', '/c', '/nologo', '/std:c11', '/W3', '/WX', '/D_CRT_SECURE_NO_WARNINGS', '/I' + os.path.join(ARC, 'include'),
               '/Fo' + out_dir + os.sep] + ['/D' + d for d in defs] + [src]
        p = subprocess.run(cmd, capture_output=True, text=True); n += 1
        lines = [l.strip() for l in (p.stdout + p.stderr).splitlines() if re.search(r'\b(warning|error) [A-Z]+\d+', l)]
        if p.returncode != 0 or lines:
            bad += lines or ['%s: cl failed (exit %d)' % (src, p.returncode)]
    for l in dict.fromkeys(bad): print('  ' + l)
    if bad:
        print('ARC error warnings: %d MSVC warning(s) in %d files (/W3 /WX): fix them (float literals with f, (float) casts on ints)' % (len(set(bad)), n))
        return 1
    print('ARC ok warnings %d files (MSVC /W3, none)' % n)
    return 0


def clang_warnings():
    cc = shutil.which('clang')
    if not cc: return None
    found = []
    for src, defs in units():
        cmd = [cc, '-std=c11', '-fsyntax-only', '-D_GNU_SOURCE', '-I' + os.path.join(ARC, 'include'), '-I' + os.path.join(ARC, 'src')] + \
              ['-D' + d for d in defs] + CLANG_FLAGS + [src]
        p = subprocess.run(cmd, capture_output=True, text=True)
        for m in re.finditer(r'^(.+?):(\d+):\d+: (?:fatal )?error: (.*)$', p.stderr, re.M):   # a file that doesn't compile counts too
            found.append(('%s | error | %s' % (os.path.abspath(m.group(1)), m.group(3)), '%s:%s error' % (m.group(1), m.group(2))))
        for m in re.finditer(r'^(.+?):(\d+):\d+: warning: .*\[(-W[^\]]+)\]$', p.stderr, re.M):
            path, line, flag = m.group(1), int(m.group(2)), m.group(3)
            full = os.path.abspath(path)
            rel = os.path.relpath(full, ARC).replace(os.sep, '/') if not GAME_FILES else full
            try: text = open(full, encoding='utf-8', errors='replace').read().splitlines()[line - 1].strip()
            except (OSError, IndexError): text = ''
            found.append(('%s | %s | %s' % (rel, flag, re.sub(r'\s+', ' ', text)), '%s:%d %s' % (rel, line, flag)))
    return found


def main():
    if '--msvc' in sys.argv: return msvc()
    found = clang_warnings()
    if found is None:
        print('ARC ok warnings (skipped: no clang here; build.bat test runs the MSVC check)'); return 0
    keys = sorted(set(k for k, _ in found))
    if '--update' in sys.argv and not GAME_FILES:
        with open(BASELINE, 'w') as f:
            f.write('# clang conversion warnings in older code, accepted (tools/warn_check.py --update). New ones fail build test.\n')
            for k in keys: f.write(k + '\n')
        print('ARC ok warnings baseline written: %d entries' % len(keys)); return 0
    base = set()
    if os.path.exists(BASELINE) and not GAME_FILES:
        base = {l.rstrip('\n') for l in open(BASELINE, encoding='utf-8') if l.strip() and not l.startswith('#')}
    new = [(k, where) for k, where in found if k not in base]
    if new:
        seen = set()
        for k, where in new:
            if k in seen: continue
            seen.add(k); print('  new warning %s: %s' % (where, k.split(' | ', 2)[2]))
        print('ARC error warnings: %d new conversion warning(s) (clang; MSVC would warn C4244/C4267): use f on float literals, (float) on ints' % len(seen))
        return 1
    print('ARC ok warnings %d files (clang conversion warnings: none new, %d old ones in the baseline)' % (len(units()), len(keys)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
