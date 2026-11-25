"""ARC 0.10.15 (Grand Token Acquisition's request): a new game's build.bat asks "Launch <Title> now? [Y/N]" after a plain
build (No after 15 s, never with ARC_NO_LAUNCH or any argument), `build.bat run` builds and launches without asking, and
`build.sh run` builds and starts the Linux build. Checks the scripts new_game.py writes (build.bat can only be run on
Windows), and that ARC's tools that run a game's plain build.bat set ARC_NO_LAUNCH. Exit 1 on failure."""
import os, shutil, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = []
def check(cond, what):
    if not cond: fails.append(what)
d = tempfile.mkdtemp(prefix='arc_launch_')
try:
    g = os.path.join(d, 'launch_game')
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'new_game.py'), g, '--title', 'Launch Test'], capture_output=True, text=True)
    check(r.returncode == 0, 'new_game.py failed: ' + r.stderr[-300:])
    bat = open(os.path.join(g, 'build.bat'), encoding='utf-8', newline='').read()
    sh = open(os.path.join(g, 'build.sh'), encoding='utf-8').read()
    name = [l for l in bat.splitlines() if l.startswith('start "" /d')]
    check('ARC_NO_LAUNCH' in bat, 'build.bat has no ARC_NO_LAUNCH')
    check('choice /c YN /n /t 15 /d N /m "Launch Launch Test now? [Y/N] "' in bat, 'build.bat does not ask "Launch Launch Test now?" with a 15 s No')
    check('if "%1"=="" call :ask_launch' in bat, 'build.bat asks after other targets too (only a plain build should)')
    check('if /i "%1"=="run" goto launch' in bat, 'build.bat run does not launch')
    check(len(name) == 1 and 'launch_game.exe' in name[0] and '/d "%CD%\\build"' in name[0], 'build.bat does not start build\\<name>.exe in build\\: %s' % name)
    check('\r\n' in bat, 'build.bat lost its CRLF line ends')
    check('  run)' in sh and 'exec "./$name"' in sh, 'build.sh has no run target')
    # 0.10.19: web builds offer to serve the page and open the browser
    check('choice /c YN /n /t 15 /d N /m "Serve Launch Test and open it in the browser now? [Y/N] "' in bat, 'build.bat web does not offer to serve it')
    check('if /i "%1"=="serve" goto serve' in bat and 'tools\\web_serve.py" build\\web --open index.html' in bat, 'build.bat has no serve target')
    check(bat.index('ARC_NO_LAUNCH (echo serve it') < bat.index('Serve Launch Test'), 'build.bat web asks even with ARC_NO_LAUNCH')
    check('  serve)' in sh and 'web_serve.py" build/web --open index.html' in sh, 'build.sh has no serve target')
    root_bat = open(os.path.join(ROOT, 'build.bat'), encoding='utf-8').read()
    check('Serve build\\web and open %NAME%.html in the browser now? [Y/N]' in root_bat and ':serve' in root_bat, "ARC's build.bat web does not offer to serve")
    # build.sh run, for real, with the compiler swapped for a stub that writes a game which prints a line
    stub = os.path.join(d, 'cc')
    open(stub, 'w').write('#!/bin/sh\nout=""\nwhile [ $# -gt 0 ]; do [ "$1" = -o ] && { out="$2"; shift; }; shift; done\n'
                          'printf "#!/bin/sh\\necho launched from \\$(basename \\$(pwd))\\n" > "$out"; chmod +x "$out"\n')
    os.chmod(stub, 0o755)
    if os.name != 'nt':
        r = subprocess.run(['sh', 'build.sh', 'run'], cwd=g, capture_output=True, text=True, env=dict(os.environ, CC=stub, CXX=stub))
        check(r.returncode == 0 and 'launched from build' in r.stdout, 'build.sh run did not start the game in build/: ' + (r.stdout + r.stderr)[-300:])
    mcp = open(os.path.join(ROOT, 'tools', 'arc_mcp.py'), encoding='utf-8').read()
    check("ARC_NO_LAUNCH='1'" in mcp, 'arc_mcp.py builds without ARC_NO_LAUNCH')
    # tools/web_serve.py serves the folder with the right types (and stops after one page with --once)
    import threading, time, urllib.request
    w = os.path.join(d, 'web'); os.makedirs(w); open(os.path.join(w, 'index.html'), 'w').write('<p>hi</p>'); open(os.path.join(w, 'g.wasm'), 'wb').write(b'\0asm')
    p = subprocess.Popen([sys.executable, os.path.join(ROOT, 'tools', 'web_serve.py'), w, '--port', '18765', '--no-open'], stdout=subprocess.PIPE, text=True)
    try:
        line = p.stdout.readline(); port = int(line.split('localhost:')[1].split('/')[0]) if 'localhost:' in line else 0
        r1 = urllib.request.urlopen('http://127.0.0.1:%d/index.html' % port, timeout=5)
        r2 = urllib.request.urlopen('http://127.0.0.1:%d/g.wasm' % port, timeout=5)
        check(b'hi' in r1.read() and r2.headers.get('Content-Type') == 'application/wasm' and 'no-cache' in (r2.headers.get('Cache-Control') or ''),
              'web_serve.py serves the wrong things: %s %s' % (r2.headers.get('Content-Type'), r2.headers.get('Cache-Control')))
    except Exception as e:
        check(False, 'web_serve.py: %s' % e)
    finally:
        p.terminate(); p.wait(5)
finally:
    shutil.rmtree(d, ignore_errors=True)
for f in fails: print('ARC error launch prompt: ' + f)
if not fails: print('ARC ok launch prompt')
sys.exit(1 if fails else 0)
