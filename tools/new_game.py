#!/usr/bin/env python3
"""Create a new game project that uses a shared copy of ARC.

    python tools/new_game.py D:/Projects/MyGame            # C game
    python tools/new_game.py D:/Projects/MyGame --cpp      # C++ game
    python tools/new_game.py ../MyGame --title "My Game" --vendor   # copy ARC into the game (pinned)
    python tools/new_game.py D:/Projects/MyGame --update-scripts       # rewrite an existing game's build.bat and
                                                                       # build.sh (old ones kept as *.old)
    python tools/new_game.py D:/Projects/MyGame --web-packs "levels music"   # web build: a small first package, those
                                                                       # folders of assets/ fetched on demand (0.8.6)
    python tools/new_game.py D:/Projects/MyGame --template tactics     # start from a genre template (0.9.1):
                                                                       # tactics, shooter, platformer, racer, cards, deck
    python tools/new_game.py D:/Projects/MyGame --add-mcp             # write .mcp.json into an existing game (0.10.10)
    python tools/new_game.py --list-templates

The new project gets:
    src/main.c|cpp   a working starter game (the hello sample, or the --template game)
    tests/acceptance.md  with --template: runnable checks of the template's rules (tools/arc_check.py)
    release.json     settings for tools/arc_release.py (fill in "itch": "user/game")
    assets/          files the game loads at runtime (copied next to the exe / packed into the web build)
    build.bat        Windows (Visual Studio):  build.bat [headless|web|release]
    build.sh         Linux/WSL/macOS shell:    ./build.sh [linux|headless|win|web|release]
    AGENTS.md        instructions for AI agents working on this game
    arc.txt          where ARC lives (one line); build scripts read it
    .mcp.json        starts ARC's MCP server for Claude Code and other MCP clients (0.10.10, docs/MCP.md)

Shared mode (default): arc.txt holds the path to this ARC folder, so every game uses the same engine
and gets fixes immediately. Vendored mode (--vendor): ARC's include/ and src/ are copied into
<game>/arc/ and arc.txt says "arc" — use this for a release you want frozen.
"""
import argparse, os, shutil, sys

ARC_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
TEMPLATES = os.path.join(ARC_ROOT, 'templates')

def list_templates():
    return sorted(d for d in os.listdir(TEMPLATES) if os.path.exists(os.path.join(TEMPLATES, d, 'main.c'))) if os.path.isdir(TEMPLATES) else []

def template_summary(t):   # first line of the template's header comment, after the dash
    first = open(os.path.join(TEMPLATES, t, 'main.c'), encoding='utf-8').readline()
    return first.split(' - ', 1)[-1].split('(')[0].strip()

BUILD_BAT = r'''@echo off
rem Build {title} with ARC (MSVC; finds Visual Studio itself, or run from a Developer Command Prompt).
rem   build.bat            -> build\{name}.exe   (Direct3D 11 window; --headless also works)
rem   build.bat headless   -> build\{name}_headless.exe (no window code)
rem   build.bat web        -> build\web\index.html (+ .js .wasm .data; needs emsdk: on PATH, in EMSDK, or C:\emsdk)
rem   build.bat release    -> both, then dist\{zipname}-windows.zip (exe + assets) and dist\{zipname}-web.zip
rem                           (files at the top level, ready for itch.io "This file will be played in the browser")
rem Web packs (ARC 0.8.6, <ARC>\docs\WEB.md): WEB_PACKS lists folders of assets\ that become packs next to index.html,
rem fetched while the game runs (queued loads of their files wait for them); the rest is preloaded and cached in the
rem browser. "*" = every folder but assets\first\. Empty = one preloaded package (as before). set WEB_PACKS=... overrides.
setlocal enabledelayedexpansion
if not defined WEB_PACKS set "WEB_PACKS={web_packs}"

cd /d "%~dp0"
set /p ARC=<arc.txt
if not "%ARC_DIR%"=="" set ARC=%ARC_DIR%
set ARC=%ARC:/=\%
if not exist "%ARC%\include\arc.h" (echo ARC not found at "%ARC%" - edit arc.txt or set ARC_DIR & exit /b 1)
if not exist build mkdir build
if not exist build\obj mkdir build\obj
set CSRCS=
set CPPSRCS=
set HASCPP=0
for %%F in (src\*.c) do set CSRCS=!CSRCS! "%%F"
for %%F in (src\*.cpp) do (set CPPSRCS=!CPPSRCS! "%%F"& set HASCPP=1)
if /i "%1"=="web" goto web_only
if /i "%1"=="release" goto release
call :find_cl
if errorlevel 1 exit /b 1
call :build_win %1
if errorlevel 1 exit /b 1
exit /b 0

:web_only
call :build_web
if errorlevel 1 exit /b 1
exit /b 0

:release
call :find_cl
if errorlevel 1 exit /b 1
call :build_win
if errorlevel 1 exit /b 1
call :build_web
if errorlevel 1 exit /b 1
if exist build\release rmdir /s /q build\release
mkdir build\release\win
copy /y build\{name}.exe build\release\win\ >nul
if exist assets xcopy /e /i /y /q assets build\release\win\assets >nul
if not exist dist mkdir dist
call :zip build\release\win dist\{zipname}-windows.zip
if errorlevel 1 exit /b 1
call :zip build\web dist\{zipname}-web.zip
if errorlevel 1 exit /b 1
echo released dist\{zipname}-windows.zip and dist\{zipname}-web.zip
exit /b 0

rem ---- find the MSVC compiler: on PATH already, or locate Visual Studio with vswhere and load vcvars64.bat ----
:find_cl
where cl >nul 2>nul
if not errorlevel 1 exit /b 0
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo Visual Studio not found - install "Desktop development with C++" or run from a Developer Command Prompt & exit /b 1)
set "VSPATH="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%I"
if not defined VSPATH (echo Visual Studio C++ tools not found - install "Desktop development with C++" & exit /b 1)
echo Setting up MSVC from "%VSPATH%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo vcvars64.bat failed & exit /b 1)
where cl >nul 2>nul
if errorlevel 1 (echo cl.exe still not found after vcvars64.bat & exit /b 1)
exit /b 0

rem ---- Windows: C and C++ compile in separate cl calls (MSVC rejects /std:c11 with /std:c++17) ----
:build_win
set COMMON=/nologo /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /I"%ARC%\include"
set OUT=build\{name}.exe
if /i "%1"=="headless" (set COMMON=%COMMON% /DARC_HEADLESS_ONLY& set OUT=build\{name}_headless.exe)
cl %COMMON% /std:c11 /c "%ARC%\src\arc.c" %CSRCS% /Fo:build\obj\
if errorlevel 1 exit /b 1
if "%HASCPP%"=="1" (
  cl %COMMON% /std:c++17 /EHsc /c %CPPSRCS% /Fo:build\obj\
  if errorlevel 1 exit /b 1
)
set OBJS=build\obj\arc.obj
for %%F in (src\*.c src\*.cpp) do set OBJS=!OBJS! build\obj\%%~nF.obj
rem resources (ARC 0.8.2): res\*.rc, e.g. "1 ICON game.ico" for the exe, window and taskbar icon
for %%F in (res\*.rc) do (
  rc /nologo /fo build\obj\%%~nF.res "%%F"
  if errorlevel 1 exit /b 1
  set OBJS=!OBJS! build\obj\%%~nF.res
)
link /nologo /OUT:%OUT% %OBJS% d3d11.lib dxguid.lib winmm.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
if exist assets xcopy /e /i /y /q assets build\assets >nul
echo built %OUT%
exit /b 0

rem ---- web: Emscripten (emcc.bat, so every call is "call emcc") ----
:build_web
rem emcc on PATH, else emsdk from EMSDK, C:\emsdk or %USERPROFILE%\emsdk
where emcc >nul 2>nul
if not errorlevel 1 goto have_emcc
if not defined EMSDK if exist "C:\emsdk\emsdk_env.bat" set "EMSDK=C:\emsdk"
if not defined EMSDK if exist "%USERPROFILE%\emsdk\emsdk_env.bat" set "EMSDK=%USERPROFILE%\emsdk"
if not defined EMSDK (echo emcc not found - install emsdk in C:\emsdk, or set EMSDK to its folder & exit /b 1)
echo Setting up Emscripten from "%EMSDK%"
call "%EMSDK%\emsdk_env.bat" >nul 2>nul
where emcc >nul 2>nul
if errorlevel 1 (echo emcc still not found after "%EMSDK%\emsdk_env.bat" - run "emsdk install latest" and "emsdk activate latest" there & exit /b 1)
:have_emcc
if exist build\web rmdir /s /q build\web
mkdir build\web
if not exist build\webobj mkdir build\webobj
set "SHELL_HTML=%ARC%\web\shell.html"
if exist web\shell.html set "SHELL_HTML=web\shell.html"
set WOBJS=build\webobj\arc.o
call emcc -std=c11 -O2 -I"%ARC%\include" -c "%ARC%\src\arc.c" -o build\webobj\arc.o
if errorlevel 1 exit /b 1
for %%F in (src\*.c) do (
  call emcc -std=c11 -O2 -I"%ARC%\include" -c "%%F" -o build\webobj\%%~nF.o
  if errorlevel 1 exit /b 1
  set WOBJS=!WOBJS! build\webobj\%%~nF.o
)
for %%F in (src\*.cpp) do (
  call em++ -std=c++17 -O2 -I"%ARC%\include" -c "%%F" -o build\webobj\%%~nF.o
  if errorlevel 1 exit /b 1
  set WOBJS=!WOBJS! build\webobj\%%~nF.o
)
set WLINK=emcc
if "%HASCPP%"=="1" set WLINK=em++
set PRELOAD=
if exist assets set PRELOAD=--preload-file assets
if not "%WEB_PACKS%"=="" if exist assets (
  python "%ARC%\tools\web_packs.py" assets build\web build\webstage --packs "%WEB_PACKS%"
  if errorlevel 1 exit /b 1
  set "PRELOAD=--preload-file build\webstage@/ --use-preload-cache"
)
call %WLINK% -O2 %WOBJS% -o build\web\index.html --shell-file "%SHELL_HTML%" -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_arc_web_audio_fill,_arc_web_command" "-sEXPORTED_RUNTIME_METHODS=lengthBytesUTF8,stringToUTF8,HEAPF32" %PRELOAD%
if errorlevel 1 exit /b 1
echo built build\web\index.html
exit /b 0

rem ---- zip a folder's contents (forward slashes inside, so itch.io's servers read it) ----
:zip
if exist "%~2" del "%~2"
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::CreateFromDirectory((Resolve-Path '%~1').Path, (Join-Path (Get-Location) '%~2'))"
if errorlevel 1 (echo zip failed: %~2 & exit /b 1)
exit /b 0
'''

BUILD_SH = r'''#!/usr/bin/env sh
# Build {title} with ARC.   ./build.sh [linux|headless|win|web|release]   (default linux)
#   web      -> build/web/index.html (+ .js .wasm .data)
#   release  -> win + web, then dist/{zipname}-windows.zip (exe + assets) and dist/{zipname}-web.zip (for itch.io)
# Web packs (ARC 0.8.6, <ARC>/docs/WEB.md): WEB_PACKS lists folders of assets/ that become packs next to index.html,
# fetched while the game runs (queued loads of their files wait for them); the rest is preloaded and cached in the
# browser. "*" = every folder but assets/first/. Empty = one preloaded package (as before). WEB_PACKS=... overrides it.
WEB_PACKS="${{WEB_PACKS-{web_packs}}}"
set -e
cd "$(dirname "$0")"
ARC=${{ARC_DIR:-$(head -n1 arc.txt | tr -d '\r')}}
case "$ARC" in /*|?:*) ;; *) ARC="$(pwd)/$ARC" ;; esac
[ -f "$ARC/include/arc.h" ] || {{ echo "ARC not found at $ARC - edit arc.txt or set ARC_DIR"; exit 1; }}
target=${{1:-linux}}; name={name}
mkdir -p build
CS=$(ls src/*.c 2>/dev/null || true); CPPS=$(ls src/*.cpp 2>/dev/null || true)
copy_assets() {{ [ -d assets ] && rm -rf "$1/assets" && cp -r assets "$1/assets" || true; }}
native() {{  # $1 = C compiler, $2 = C++ compiler, $3 = output, $4 = extra C flags, $5 = libs
  $1 -std=c11 -O2 -Wall $4 -I"$ARC/include" -c "$ARC/src/arc.c" -o build/arc.o
  if [ -n "$CPPS" ]; then $2 -std=c++17 -O2 -Wall $4 -I"$ARC/include" $CPPS $CS build/arc.o -o "$3" $5
  else $1 -std=c11 -O2 -Wall $4 -I"$ARC/include" $CS build/arc.o -o "$3" $5; fi
}}
web() {{
  rm -rf build/web; mkdir -p build/web build/webobj
  emcc -std=c11 -O2 -I"$ARC/include" -c "$ARC/src/arc.c" -o build/webobj/arc.o
  OBJS=build/webobj/arc.o
  for f in $CS; do o=build/webobj/$(basename "$f" .c).o; emcc -std=c11 -O2 -I"$ARC/include" -c "$f" -o "$o"; OBJS="$OBJS $o"; done
  for f in $CPPS; do o=build/webobj/$(basename "$f" .cpp).o; em++ -std=c++17 -O2 -I"$ARC/include" -c "$f" -o "$o"; OBJS="$OBJS $o"; done
  LINK=emcc; [ -n "$CPPS" ] && LINK=em++   # C++ games link with em++ (libc++)
  SHELL_HTML="$ARC/web/shell.html"; [ -f web/shell.html ] && SHELL_HTML=web/shell.html   # the game's own page, if it has one
  PRE=""; [ -d assets ] && PRE="--preload-file assets"
  if [ -n "$WEB_PACKS" ] && [ -d assets ]; then
    python3 "$ARC/tools/web_packs.py" assets build/web build/webstage --packs "$WEB_PACKS" || exit 1
    PRE="--preload-file build/webstage@/ --use-preload-cache"
  fi
  $LINK -O2 $OBJS -o build/web/index.html --shell-file "$SHELL_HTML" \
    -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 \
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_arc_web_audio_fill,_arc_web_command \
    -sEXPORTED_RUNTIME_METHODS=lengthBytesUTF8,stringToUTF8,HEAPF32 \
    $PRE
}}
zipdir() {{ python3 -c "import os,sys,zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(src):
        for f in sorted(files):
            p = os.path.join(root, f); z.write(p, os.path.relpath(p, src).replace(os.sep, '/'))" "$1" "$2"; }}
winres() {{   # resources (ARC 0.8.2): res/*.rc, e.g. "1 ICON game.ico" for the exe, window and taskbar icon
  WRES=""
  for f in res/*.rc; do [ -f "$f" ] || continue; o="build/$(basename "$f" .rc).res"
    x86_64-w64-mingw32-windres "$f" -O coff -o "$o" || exit 1; WRES="$WRES $o"; done
}}
case "$target" in
  linux)    native "${{CC:-cc}}" "${{CXX:-c++}}" "build/$name" "-D_GNU_SOURCE" "-lX11 -lGL -ldl -lpthread -lm"; copy_assets build ;;
  headless) native "${{CC:-cc}}" "${{CXX:-c++}}" "build/${{name}}_headless" "-D_GNU_SOURCE -DARC_HEADLESS_ONLY" "-lm"; copy_assets build ;;
  win)      winres; native x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ "build/$name.exe" "" "$WRES -static -ld3d11 -ldxguid -lwinmm -luuid"; copy_assets build ;;
  web)      web ;;
  release)  winres; native x86_64-w64-mingw32-gcc x86_64-w64-mingw32-g++ "build/$name.exe" "" "$WRES -static -ld3d11 -ldxguid -lwinmm -luuid"
            web
            rm -rf build/release && mkdir -p build/release/win dist
            cp "build/$name.exe" build/release/win/; copy_assets build/release/win
            zipdir build/release/win "dist/{zipname}-windows.zip"; zipdir build/web "dist/{zipname}-web.zip"
            echo "released dist/{zipname}-windows.zip and dist/{zipname}-web.zip" ;;
  *) echo "unknown target $target"; exit 1 ;;
esac
echo "built $target"
'''

AGENTS = '''# {title} — notes for AI agents

This game runs on the **ARC** engine. The engine lives at the path in `arc.txt`. Read these first:

- `<ARC>/AGENTS.md` — the build, run and inspect loop, and the agent command reference.
- `<ARC>/docs/QUICK.md` — every engine name, one line each, by topic (topic pages in `<ARC>/docs/api/`).
- `<ARC>/docs/API.md` — every engine function on one page.
- `<ARC>/docs/ARC.md` — architecture and rules.

## This game

- Code is in `src/`; runtime files are in `assets/` (loaded as `assets/...`).
- Build a headless binary with `./build.sh headless` (or `build.bat headless`), then drive it:
  `build/{name}_headless --headless --cmd "step 60; dump; shot a.png; quit"`
- Keep all game logic in `tick()`, keep `draw()` free of state changes, and put every testable fact in `dump()`.
- Do not edit engine files from this project. Engine changes go into the ARC repository so every game gets them.
- Checks: `python <ARC>/tools/arc_check.py tests/acceptance.md` (when there is one) and `python <ARC>/tools/arc_doctor.py`.
- Art: `python <ARC>/tools/arc_import.py art/thing.glb --height 2` files it under assets/ and prints the load line.
- Release: `python <ARC>/tools/arc_release.py --dry-run`, then `--version X.Y.Z` (settings in `release.json`).
- Live (0.10.10): `.mcp.json` starts ARC's MCP server (`<ARC>/tools/arc_mcp.py`): `arc_build`, `arc_start` (window=true
  to let the person watch), `arc_step`, `arc_frame`, `arc_shot`, `arc_pick`, `arc_expect`, and `arc_save_check` to keep
  what you found as a headless test. See `<ARC>/docs/MCP.md`.

## Game-specific commands (`command()` callback)

<!-- list them here as you add them, e.g. `spawn N`, `level N`, `god` -->
'''

def zipname(title):   # "Babel" -> Babel-windows.zip; "My Game!" -> MyGame-windows.zip
    return ''.join(c for c in title if c.isalnum() or c in '_-') or 'game'

def write_mcp(dest, arc_path):   # 0.10.10: Claude Code (and other MCP clients) find ARC's tools for this game
    import json
    cfg = {'mcpServers': {'arc': {'command': 'python', 'args': [arc_path.rstrip('/') + '/tools/arc_mcp.py', '--game', '.']}}}
    p = os.path.join(dest, '.mcp.json')
    if os.path.exists(p):
        try:
            old = json.load(open(p, encoding='utf-8'))
            old.setdefault('mcpServers', {})['arc'] = cfg['mcpServers']['arc']; cfg = old
        except ValueError:
            pass
    open(p, 'w', newline='\n').write(json.dumps(cfg, indent=2) + '\n')
    return p

def add_mcp(dest):
    t = os.path.join(dest, 'arc.txt')
    if not os.path.exists(t): sys.exit('no arc.txt in %s: is it an ARC game folder?' % dest)
    arc_path = open(t, encoding='utf-8').read().strip() or ARC_ROOT
    print('wrote ' + write_mcp(dest, arc_path) + ' (the arc MCP server; Claude Code asks once before using it)')

def update_scripts(dest, a):
    import re
    if not os.path.isfile(os.path.join(dest, 'arc.txt')): sys.exit('not an ARC game (no arc.txt): ' + dest)
    old = open(os.path.join(dest, 'build.bat'), encoding='utf-8', errors='replace').read() if os.path.isfile(os.path.join(dest, 'build.bat')) else ''
    m = re.search(r'set OUT=build\\([^\s.]+)\.exe', old); t = re.search(r'rem Build (.+?) with ARC', old)
    wp = re.search(r'set "WEB_PACKS=(.*?)"', old)   # 0.8.6: keep the game's packs list
    web_packs = a.web_packs if a.web_packs is not None else (wp.group(1) if wp else '')
    name = a.name or (m.group(1) if m else ''.join(c for c in os.path.basename(dest).lower() if c.isalnum() or c in '_-') or 'game')
    title = a.title or (t.group(1) if t else os.path.basename(dest))
    for fn, text, nl in (('build.bat', BUILD_BAT, '\r\n'), ('build.sh', BUILD_SH, '\n')):
        p = os.path.join(dest, fn)
        if os.path.isfile(p): shutil.copy(p, p + '.old')
        open(p, 'w', newline=nl).write(text.format(name=name, title=title, zipname=zipname(title), web_packs=web_packs))
    try: os.chmod(os.path.join(dest, 'build.sh'), 0o755)
    except OSError: pass
    print('rewrote build.bat and build.sh in %s (name %s, title %s; old ones saved as *.old)' % (dest, name, title))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('dest', nargs='?', help='new project folder (must not exist or be empty)')
    ap.add_argument('--template', default=None, help='genre starter: ' + ', '.join(list_templates()))
    ap.add_argument('--list-templates', action='store_true')
    ap.add_argument('--title', default=None)
    ap.add_argument('--cpp', action='store_true', help='C++ starter (main.cpp)')
    ap.add_argument('--vendor', action='store_true', help='copy ARC into the project instead of referencing it')
    ap.add_argument('--update-scripts', action='store_true', help='existing game: rewrite build.bat/build.sh (keeps *.old)')
    ap.add_argument('--add-mcp', action='store_true', help='existing game: write .mcp.json for ARC\'s MCP server (0.10.10)')
    ap.add_argument('--name', default=None, help='executable name (default: from the folder name, or the old build.bat)')
    ap.add_argument('--web-packs', default=None, help='folders of assets/ the web build fetches on demand ("levels music", "*"; ARC 0.8.6)')
    a = ap.parse_args()
    if a.list_templates:
        for t in list_templates(): print('  %-12s %s' % (t, template_summary(t)))
        return
    if not a.dest: ap.error('give the new project folder')
    if a.template and a.template not in list_templates():
        sys.exit('unknown template %r (have: %s)' % (a.template, ', '.join(list_templates())))
    if a.template and a.cpp: sys.exit('--template games are C (rename main.c to main.cpp yourself if you want C++)')
    dest = os.path.abspath(a.dest)
    if a.update_scripts: return update_scripts(dest, a)
    if a.add_mcp: return add_mcp(dest)
    if os.path.exists(dest) and os.listdir(dest): sys.exit('destination exists and is not empty: ' + dest)
    name = a.name or ''.join(c for c in os.path.basename(dest).lower() if c.isalnum() or c in '_-') or 'game'
    title = a.title or os.path.basename(dest)
    os.makedirs(os.path.join(dest, 'src')); os.makedirs(os.path.join(dest, 'assets'))
    if a.template:
        src = open(os.path.join(TEMPLATES, a.template, 'main.c'), encoding='utf-8').read().replace('{title}', title.replace('"', ''))
        os.makedirs(os.path.join(dest, 'tests'))
        shutil.copy(os.path.join(TEMPLATES, a.template, 'acceptance.md'), os.path.join(dest, 'tests', 'acceptance.md'))
    else:
        src = open(os.path.join(ARC_ROOT, 'samples', 'hello', 'hello.c'), encoding='utf-8').read()
        src = src.replace('#include "../../include/arc.h"', '#include "arc.h"').replace('"Hello ARC"', '"%s"' % title.replace('"', ''))
    open(os.path.join(dest, 'src', 'main.cpp' if a.cpp else 'main.c'), 'w', encoding='utf-8', newline='\n').write(src)
    open(os.path.join(dest, 'assets', '.keep'), 'w').close()
    if a.vendor:
        for sub in ('include', 'src', 'web'): shutil.copytree(os.path.join(ARC_ROOT, sub), os.path.join(dest, 'arc', sub))
        shutil.copy(os.path.join(ARC_ROOT, 'AGENTS.md'), os.path.join(dest, 'arc', 'AGENTS.md'))
        shutil.copytree(os.path.join(ARC_ROOT, 'docs'), os.path.join(dest, 'arc', 'docs'), ignore=shutil.ignore_patterns('*.png'))
        arc_path = 'arc'
    else:
        arc_path = os.path.relpath(ARC_ROOT, dest) if os.path.splitdrive(ARC_ROOT)[0] == os.path.splitdrive(dest)[0] else ARC_ROOT
        arc_path = arc_path.replace('\\', '/')
    open(os.path.join(dest, 'arc.txt'), 'w', newline='\n').write(arc_path + '\n')
    write_mcp(dest, arc_path)
    wp = a.web_packs or ''
    open(os.path.join(dest, 'build.bat'), 'w', newline='\r\n').write(BUILD_BAT.format(name=name, title=title, zipname=zipname(title), web_packs=wp))
    sh = os.path.join(dest, 'build.sh'); open(sh, 'w', newline='\n').write(BUILD_SH.format(name=name, title=title, zipname=zipname(title), web_packs=wp))
    try: os.chmod(sh, 0o755)
    except OSError: pass
    agents = AGENTS.format(name=name, title=title)
    if a.template:
        agents += '\n' + open(os.path.join(TEMPLATES, a.template, 'NOTES.md'), encoding='utf-8').read()
    open(os.path.join(dest, 'AGENTS.md'), 'w', encoding='utf-8', newline='\n').write(agents)
    import json
    rel = {'itch': '', 'channels': {'windows': 'dist/*-windows.zip', 'html5': 'dist/*-web.zip'},
           'checks': ['tests/acceptance.md'] if a.template else []}
    open(os.path.join(dest, 'release.json'), 'w', newline='\n').write(json.dumps(rel, indent=2) + '\n')
    open(os.path.join(dest, '.gitignore'), 'w', newline='\n').write('build/\n')
    print('created %s (%s%s, ARC %s: %s)' % (dest, 'C++' if a.cpp else 'C', (', template ' + a.template) if a.template else '',
                                            'vendored' if a.vendor else 'shared', arc_path))

if __name__ == '__main__':
    main()
