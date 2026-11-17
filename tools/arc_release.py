#!/usr/bin/env python3
"""Release an ARC game in one command (0.9.0): check, build, sign, push to itch.io. Try it with --dry-run first.

    python3 <ARC>/tools/arc_release.py --dry-run                 show every step, run nothing that changes anything
    python3 <ARC>/tools/arc_release.py --version 1.2.0           check, build, sign (if set up), push
    python3 <ARC>/tools/arc_release.py --skip-build --version 1.2.0     push the zips already in dist/

Run it in the game's folder (or pass --game DIR). The steps, in order:
  1. doctor    tools/arc_doctor.py on the game; an ERROR stops the release (--no-doctor skips it)
  2. check     tools/arc_check.py on each acceptance file given with --check FILE (or listed in release.json)
  3. build     build.bat release (Windows) or ./build.sh release: dist/<zip>-windows.zip and dist/<zip>-web.zip
  4. sign      Windows only, when set up: signtool signs the exe in build/release/win and the zip is rebuilt
  5. push      butler push of each channel's zip to itch.io (user/game:channel, --userversion)

Settings come from <game>/release.json, and options override them:
  {
    "itch": "allanr/my-game",                       itch.io user/game (no push without it)
    "channels": { "windows": "dist/*-windows.zip",  channel -> zip (glob, newest match); html5 is itch's web channel
                  "html5":   "dist/*-web.zip" },
    "checks": ["tests/acceptance.md"],              acceptance files run in step 2
    "sign": { "pfx": "C:/keys/code.pfx",            a .pfx file (its password from the env var ARC_SIGN_PASSWORD), or
              "thumbprint": "AB12...",              a certificate in the Windows store
              "timestamp": "http://timestamp.digicert.com" }
  }

What it needs that ARC cannot provide: butler installed and logged in (`butler login`, once), and for signing a
code-signing certificate and signtool (Windows SDK). Without them those steps are skipped with a note (signing) or
stop the release (push), and --dry-run tells you which are missing before you start.

A log of what happened is written to dist/release_log.json. Exit code 0 only if every step that ran succeeded.
"""
import argparse, datetime, glob, json, os, shutil, subprocess, sys, zipfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable or 'python3'


class Release:
    def __init__(self, a):
        self.a = a
        self.game = os.path.abspath(a.game)
        self.cfg = {}
        p = os.path.join(self.game, 'release.json')
        if os.path.exists(p):
            with open(p, encoding='utf-8') as f: self.cfg = json.load(f)
        self.log = {'game': self.game, 'dry_run': a.dry_run, 'started': datetime.datetime.now().isoformat(timespec='seconds'), 'steps': []}
        self.failed = False

    def step(self, name, status, detail='', **extra):
        e = {'step': name, 'status': status, 'detail': detail}; e.update(extra); self.log['steps'].append(e)
        print('%-7s %-5s %s' % (name, status, detail))
        if status == 'FAIL': self.failed = True

    def run(self, cmd, cwd=None, shell=False):
        shown = cmd if isinstance(cmd, str) else ' '.join(('"%s"' % c) if ' ' in c else c for c in cmd)
        if self.a.dry_run:
            print('        would run: ' + shown); return True, ''
        print('        run: ' + shown)
        p = subprocess.run(cmd, cwd=cwd or self.game, capture_output=True, text=True, shell=shell)
        out = (p.stdout or '') + (p.stderr or '')
        if self.a.verbose and out.strip(): print('\n'.join('        | ' + l for l in out.strip().splitlines()[-40:]))
        return p.returncode == 0, out

    # 1
    def doctor(self):
        if self.a.no_doctor: self.step('doctor', 'skip', '--no-doctor'); return
        # read-only, so it runs in a dry run too
        p = subprocess.run([PY, os.path.join(TOOLS, 'arc_doctor.py'), self.game, '--json'], capture_output=True, text=True)
        ok, out = p.returncode == 0, p.stdout + p.stderr
        try: rep = json.loads(out[out.index('{'):])
        except Exception: rep = None
        errs = [f for f in (rep or {}).get('findings', []) if f.get('level') == 'ERROR']
        if errs or (rep is None and not ok):
            self.step('doctor', 'FAIL', '; '.join(f.get('msg', '') for f in errs) or out.strip().splitlines()[-1:] and out.strip().splitlines()[-1])
        else:
            warns = [f for f in (rep or {}).get('findings', []) if f.get('level') == 'WARN']
            self.step('doctor', 'ok', '%d warning(s)' % len(warns))

    # 2
    def checks(self):
        files = self.a.check or self.cfg.get('checks', [])
        if not files: self.step('check', 'skip', 'no acceptance files (--check FILE or "checks" in release.json)'); return
        for f in files:
            ok, out = self.run([PY, os.path.join(TOOLS, 'arc_check.py'), f], cwd=self.game)
            last = out.strip().splitlines()[-1] if out.strip() else ''
            self.step('check', 'plan' if self.a.dry_run else ('ok' if ok else 'FAIL'), '%s %s' % (f, last))

    # 3
    def build(self):
        if self.a.skip_build: self.step('build', 'skip', '--skip-build: using the zips already in dist/'); return
        if os.name == 'nt':
            script = os.path.join(self.game, 'build.bat'); cmd = ['cmd', '/c', 'build.bat', 'release']
        else:
            script = os.path.join(self.game, 'build.sh'); cmd = ['sh', 'build.sh', 'release']
        if not os.path.exists(script): self.step('build', 'FAIL', '%s not found (make the game with tools/new_game.py)' % os.path.basename(script)); return
        ok, out = self.run(cmd)
        if self.a.dry_run: self.step('build', 'plan'); return
        self.step('build', 'ok' if ok else 'FAIL', '' if ok else (out.strip().splitlines() or ['build failed'])[-1])

    def channels(self):
        ch = dict(self.cfg.get('channels') or {'windows': 'dist/*-windows.zip', 'html5': 'dist/*-web.zip'})
        if self.a.channels:
            want = [c.strip() for c in self.a.channels.split(',')]
            ch = {k: v for k, v in ch.items() if k in want}
        found = {}
        for k, pat in ch.items():
            m = sorted(glob.glob(os.path.join(self.game, pat)), key=os.path.getmtime)
            found[k] = m[-1] if m else None
        return found

    # 4
    def sign(self):
        s = self.cfg.get('sign') or {}
        pfx, thumb = self.a.pfx or s.get('pfx'), self.a.thumbprint or s.get('thumbprint')
        if not (pfx or thumb): self.step('sign', 'skip', 'not set up (sign.pfx or sign.thumbprint in release.json)'); return
        signtool = shutil.which('signtool') or shutil.which('signtool.exe')
        if not signtool:
            self.step('sign', 'skip' if self.a.dry_run else 'FAIL', 'signtool not found (install the Windows SDK, or run from a Developer prompt)'); return
        exes = glob.glob(os.path.join(self.game, 'build', 'release', 'win', '*.exe'))
        if not exes and not self.a.dry_run: self.step('sign', 'FAIL', 'no exe in build/release/win'); return
        ts = s.get('timestamp', 'http://timestamp.digicert.com')
        cmd = [signtool, 'sign', '/fd', 'SHA256', '/tr', ts, '/td', 'SHA256']
        if pfx:
            pw = os.environ.get('ARC_SIGN_PASSWORD')
            if pw is None and not self.a.dry_run: self.step('sign', 'FAIL', 'set ARC_SIGN_PASSWORD to the .pfx password'); return
            cmd += ['/f', pfx] + (['/p', pw] if pw else [])
        else:
            cmd += ['/sha1', thumb]
        for exe in exes or ['build/release/win/<game>.exe']:
            ok, out = self.run(cmd + [exe])
            if not ok: self.step('sign', 'FAIL', (out.strip().splitlines() or ['signtool failed'])[-1]); return
        # rebuild the windows zip from the signed folder
        win = self.channels().get('windows')
        if win and not self.a.dry_run:
            src = os.path.join(self.game, 'build', 'release', 'win')
            tmp = win + '.tmp'
            with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
                for root, _, fs in os.walk(src):
                    for f in fs: p = os.path.join(root, f); z.write(p, os.path.relpath(p, src))
            os.replace(tmp, win)
        self.step('sign', 'plan' if self.a.dry_run else 'ok', '%d exe(s), windows zip rebuilt' % len(exes))

    # 5
    def push(self):
        target = self.a.itch or self.cfg.get('itch')
        if self.a.no_push: self.step('push', 'skip', '--no-push'); return
        if not target: self.step('push', 'FAIL' if not self.a.dry_run else 'skip', 'no itch.io target ("itch": "user/game" in release.json, or --itch)'); return
        butler = shutil.which('butler') or shutil.which('butler.exe')
        if not butler:
            self.step('push', 'FAIL', 'butler not found: install it from https://itch.io/docs/butler/ and run `butler login` once'); return
        if not self.a.dry_run:
            ok, out = self.run([butler, 'status', target])   # fails when not logged in or the game does not exist
            if not ok and ('login' in out.lower() or 'credentials' in out.lower()):
                self.step('push', 'FAIL', 'butler is not logged in: run `butler login` once'); return
        ver = self.a.version or self.cfg.get('version') or datetime.date.today().strftime('%Y.%m.%d')
        for ch, z in self.channels().items():
            if not z and not self.a.dry_run: self.step('push', 'FAIL', 'channel %s: no zip found' % ch); continue
            cmd = [butler, 'push', z or 'dist/<zip>', '%s:%s' % (target, ch), '--userversion', ver]
            if self.a.if_changed: cmd.append('--if-changed')
            ok, out = self.run(cmd)
            self.step('push', 'plan' if self.a.dry_run else ('ok' if ok else 'FAIL'),
                      '%s -> %s:%s (%s)' % (os.path.relpath(z, self.game) if z else '?', target, ch, ver) +
                      ('' if ok else ': ' + (out.strip().splitlines() or [''])[-1]))

    def go(self):
        print('arc_release: %s%s' % (self.game, '  (dry run: nothing is built, signed or pushed)' if self.a.dry_run else ''))
        for st in (self.doctor, self.checks, self.build, self.sign, self.push):
            st()
            if self.failed and not self.a.dry_run: break
        ch = self.channels()
        self.log['zips'] = {k: v for k, v in ch.items()}
        self.log['ok'] = not self.failed
        if not self.a.dry_run:
            os.makedirs(os.path.join(self.game, 'dist'), exist_ok=True)
            with open(os.path.join(self.game, 'dist', 'release_log.json'), 'w', encoding='utf-8') as f: json.dump(self.log, f, indent=1)
        print('arc_release: %s' % ('FAILED' if self.failed else ('dry run done' if self.a.dry_run else 'released')))
        return 1 if self.failed else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', default='.')
    ap.add_argument('--version'); ap.add_argument('--itch', help='user/game on itch.io')
    ap.add_argument('--channels', help='only these channels, e.g. windows,html5')
    ap.add_argument('--check', action='append', help='acceptance file for arc_check.py (repeatable)')
    ap.add_argument('--pfx'); ap.add_argument('--thumbprint')
    ap.add_argument('--dry-run', action='store_true'); ap.add_argument('--skip-build', action='store_true')
    ap.add_argument('--no-doctor', action='store_true'); ap.add_argument('--no-push', action='store_true')
    ap.add_argument('--if-changed', action='store_true', help='butler: skip a channel whose files have not changed')
    ap.add_argument('-v', '--verbose', action='store_true')
    sys.exit(Release(ap.parse_args()).go())


if __name__ == '__main__':
    main()
