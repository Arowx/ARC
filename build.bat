@echo off
rem ARC build for Windows with Visual Studio (run from a "Developer Command Prompt" or "x64 Native Tools" prompt).
rem   build.bat                 -> build\demo.exe   (samples\demo\demo.c, Direct3D 11)
rem   build.bat path\game.c     -> build\game.exe
rem   build.bat test            -> runs the headless self-tests; full log in build\test.log, summary at the end
rem   build.bat web [game.c]    -> build\web\game.html (+ .js .wasm), WebGL2 (0.9.6; needs emsdk: emcc on PATH, in EMSDK,
rem                                C:\emsdk or %USERPROFILE%\emsdk). Serve the folder (python -m http.server) and open it.
rem   build.bat post-d3d        -> the post test in a window on Direct3D 11 (not the software renderer), standard depth then
rem                                reversed (which is compared with standard); pictures in build\postd3d
rem   build.bat relay           -> build\arc_relay.exe, the online host for turn-based sessions (0.8.5, docs\RELAY.md)
rem   build.bat hot [game.c]    -> hot reload: build\arc_host.exe (the engine, built once) + build\game.dll (the game).
rem                                Start build\arc_host.exe, edit the game, run "build.bat hot game.c" again: the
rem                                running program loads the new code. "build.bat hot-host" rebuilds the host.
rem No CMake and no libraries: one cl.exe call. Only Windows system libraries are linked.
setlocal
cd /d "%~dp0"
if not exist build mkdir build
if /i "%1"=="web" goto web
rem Find the MSVC compiler: use it if it's already on PATH, otherwise locate Visual Studio with vswhere and
rem load vcvars64.bat (so this works from plain PowerShell or cmd too).
where cl >nul 2>nul && goto :have_cl
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo Visual Studio not found - install "Desktop development with C++" or run from a Developer Command Prompt & exit /b 1)
set "VSPATH="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%I"
if not defined VSPATH (echo Visual Studio C++ tools not found - install "Desktop development with C++" & exit /b 1)
echo Setting up MSVC from "%VSPATH%"
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo vcvars64.bat failed & exit /b 1)
where cl >nul 2>nul || (echo cl.exe still not found after vcvars64.bat & exit /b 1)
:have_cl
set CFLAGS=/nologo /std:c11 /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /Iinclude
set LIBS=d3d11.lib dxguid.lib winmm.lib user32.lib gdi32.lib
if /i "%1"=="test" goto testsum
if /i "%1"=="testrun" goto test
if /i "%1"=="relay" goto relay
if /i "%1"=="hot" goto hot
if /i "%1"=="hot-host" goto hothost
if /i "%1"=="post-d3d" goto postd3d
set GAME=%1
if "%GAME%"=="" set GAME=samples\demo\demo.c
if not exist "%GAME%" (echo "%GAME%" is not a file. Use: build.bat [game.c ^| test ^| web [game.c] ^| post-d3d ^| relay ^| hot [game.c] ^| hot-host] & exit /b 1)
for %%F in ("%GAME%") do set NAME=%%~nF
cl %CFLAGS% "%GAME%" src\arc.c /Fo:build\ /Fe:build\%NAME%.exe /link %LIBS% || exit /b 1
echo built build\%NAME%.exe
exit /b 0

:relay
cl /nologo /std:c11 /O2 /W3 /MT /D_CRT_SECURE_NO_WARNINGS /D_WINSOCK_DEPRECATED_NO_WARNINGS tools\arc_relay\arc_relay.c /Fo:build\ /Fe:build\arc_relay.exe /link ws2_32.lib || exit /b 1
echo built build\arc_relay.exe
exit /b 0

:testsum
rem 0.9.1: the whole run is shown and saved to build\test.log, then one line per stage at the end
powershell -NoProfile -ExecutionPolicy Bypass -Command "cmd /c 'build.bat testrun 2>&1' | Tee-Object -FilePath build\test.log"
python tools\test_summary.py build\test.log
exit /b %ERRORLEVEL%

:test
python tools\mesh2arcm.py tests\pyramid.obj build\pyramid.arcm >nul
python tools\mesh2arcm.py tests\pyramid.obj build\pyramid.axm --axm >nul
python tools\mesh2arcm.py tests\pyramid.obj build\pyramid_mat.arcm --metallic 1 --roughness 0.25 >nul
python tools\mesh2arcm.py tests\pyramid.obj build\pyramid_v2.arcm --v2 >nul
if not exist build\packsrc\inpack mkdir build\packsrc\inpack
copy /y build\pyramid.arcm build\packsrc\inpack\pyr.arcm >nul
python -c "open(r'build\packsrc\inpack\hello.txt','w').write('hello pack ' * 50)"
pushd build\packsrc
python ..\..\tools\arcpak.py ..\test.arcpak inpack >nul
popd
if exist build\wpsrc rmdir /s /q build\wpsrc
mkdir build\wpsrc\assets\first build\wpsrc\assets\lvl
copy /y build\pyramid.arcm build\wpsrc\assets\lvl\pyr.arcm >nul
python -c "open(r'build\wpsrc\assets\first\a.txt','w').write('first file'); open(r'build\wpsrc\assets\lvl\hello.txt','w').write('level text ' * 40)"
pushd build\wpsrc
python ..\..\tools\web_packs.py assets ..\wpout ..\wpstage --packs lvl >nul
popd
cl %CFLAGS% /DARC_HEADLESS_ONLY tests\test_core.c src\arc.c /Fo:build\ /Fe:build\arc_test.exe || exit /b 1
pushd build
python ..\tools\run_retry.py arc_test.exe --headless --cmd "step 2; tap A; type hi; axis RX 0.25; release; args a1 a2 a3 a4 a5 a6 a7 a8 a9 a10 a11 a12 a13 a14 a15 a16 a17 a18 a19 a20; step 3; shot arc_test_shot.png; step 20"
set RC=%ERRORLEVEL%
popd
if not "%RC%"=="0" exit /b %RC%
rem 0.9.0: acceptance checks (tools\arc_check.py) and the frame command, on a tiny game
cl %CFLAGS% /DARC_HEADLESS_ONLY tests\check_game.c src\arc.c /Fo:build\ /Fe:build\check_game_headless.exe || exit /b 1
python tools\arc_check.py tests\check_request.md --exe build\check_game_headless.exe || exit /b 1
rem 0.9.9: ground markers and arc_board (samples\board)
cl %CFLAGS% /DARC_HEADLESS_ONLY samples\board\board.c src\arc.c /Fo:build\ /Fe:build\board_headless.exe || exit /b 1
python tools\arc_check.py tests\board_check.md --exe build\board_headless.exe || exit /b 1
rem 0.10.0: brains, senses, shots, camera rigs (samples\action)
cl %CFLAGS% /DARC_HEADLESS_ONLY samples\action\action.c src\arc.c /Fo:build\ /Fe:build\action_headless.exe || exit /b 1
python tools\arc_check.py tests\action_check.md --exe build\action_headless.exe || exit /b 1
rem 0.10.1: review sheets and UI layout (tests\review_game.c)
cl %CFLAGS% /DARC_HEADLESS_ONLY tests\review_game.c src\arc.c /Fo:build\ /Fe:build\review_headless.exe || exit /b 1
python tools\arc_check.py tests\review_check.md --exe build\review_headless.exe || exit /b 1
python tools\arc_check.py tests\card_check.md --exe build\review_headless.exe || exit /b 1
rem 0.10.7: frame lists what is drawn into render targets (tests\target_game.c)
cl %CFLAGS% /DARC_HEADLESS_ONLY tests\target_game.c src\arc.c /Fo:build\ /Fe:build\target_headless.exe || exit /b 1
python tools\arc_check.py tests\target_check.md --exe build\target_headless.exe || exit /b 1
python tools\arc_check.py tests\target_immediate_check.md --exe build\target_headless.exe || exit /b 1
rem 0.10.8: level files and the deck editor (tests\level_game.c)
cl %CFLAGS% /DARC_HEADLESS_ONLY tests\level_game.c src\arc.c /Fo:build\ /Fe:build\level_headless.exe || exit /b 1
python tools\arc_check.py tests\level_check.md --exe build\level_headless.exe || exit /b 1
python tools\arc_check.py tests\level_launch_check.md --exe build\level_headless.exe || exit /b 1
rem 0.10.2: split-screen viewports (tests\split_game.c)
cl %CFLAGS% /DARC_HEADLESS_ONLY tests\split_game.c src\arc.c /Fo:build\ /Fe:build\split_headless.exe || exit /b 1
python tools\arc_check.py tests\split_check.md --exe build\split_headless.exe || exit /b 1
rem 0.10.11: thick lines and multi-touch (Vector Trader) on the demo
cl %CFLAGS% /DARC_HEADLESS_ONLY samples\demo\demo.c src\arc.c /Fo:build\ /Fe:build\demo_headless.exe || exit /b 1
python tools\arc_check.py tests\line_check.md --exe build\demo_headless.exe || exit /b 1
python tools\arc_check.py tests\touch_check.md --exe build\demo_headless.exe || exit /b 1
python tests\line_width_test.py build\demo_headless.exe || exit /b 1
python tests\touch_replay_test.py build\demo_headless.exe || exit /b 1
python tools\arc_doctor.py tests\doctor_fixture > build\doctor.txt
findstr /c:"missing.png" build\doctor.txt >nul || (echo ARC error doctor & exit /b 1)
findstr /c:"rand() in game code" build\doctor.txt >nul || (echo ARC error doctor & exit /b 1)
findstr /c:"comment_only" build\doctor.txt >nul && (echo ARC error doctor false alarm comment_only & exit /b 1)
findstr /c:"arc_packs.txt" build\doctor.txt >nul && (echo ARC error doctor false alarm arc_packs.txt & exit /b 1)
findstr /c:"lvl.arcpak" build\doctor.txt >nul && (echo ARC error doctor false alarm lvl.arcpak & exit /b 1)
findstr /c:"view.png" build\doctor.txt >nul && (echo ARC error doctor false alarm view.png & exit /b 1)
findstr /c:"notes_out" build\doctor.txt >nul && (echo ARC error doctor false alarm notes_out & exit /b 1)
findstr /c:"made_at_run_time" build\doctor.txt >nul && (echo ARC error doctor false alarm made_at_run_time & exit /b 1)
findstr /c:"uses ARC 0.10.6" build\doctor.txt >nul || (echo ARC error doctor version & exit /b 1)
echo ARC ok doctor
python tools\gen_api_docs.py --check || exit /b 1
python -c "import re;s=open('docs/QUICK.md').read();open('build/quick_example.c','w').write(re.search(r'```c\n(.*?)```',s,re.S).group(1))" || exit /b 1
cl %CFLAGS% /DARC_HEADLESS_ONLY /Iinclude build\quick_example.c src\arc.c /Fo:build\ /Fe:build\quick_example.exe || exit /b 1
echo ARC ok quick example
if exist build\import_test rmdir /s /q build\import_test
python tools\arc_import.py tests\pyramid.obj --game build\import_test --height 2 > build\import.txt || (type build\import.txt & echo ARC error import & exit /b 1)
findstr /c:"triangles 6" build\import.txt >nul || (type build\import.txt & echo ARC error import & exit /b 1)
echo ARC ok import
python tools\arc_release.py --game tests\doctor_fixture --dry-run > build\release.txt
findstr /c:"doctor  FAIL" build\release.txt >nul || (type build\release.txt & echo ARC error release dry run & exit /b 1)
echo ARC ok release dry run
for %%T in (tactics shooter platformer racer cards deck) do (
  if exist build\tpl_%%T rmdir /s /q build\tpl_%%T
  python tools\new_game.py build\tpl_%%T --template %%T --title "Template %%T" >nul || (echo ARC error template %%T & exit /b 1)
  python tools\arc_check.py build\tpl_%%T\tests\acceptance.md > build\tpl_%%T.txt 2>&1 || (type build\tpl_%%T.txt & echo ARC error template %%T & exit /b 1)
  echo template %%T passed
)
echo ARC ok templates 6
rem 0.9.8: every program ARC ships compiled with /W3 /WX: any MSVC warning fails the test (file and line printed)
python tools\warn_check.py --msvc
rem 0.9.8: --draw-last, --shot-at, --no-assets, reset, arc_batch, arc_import --fit
cl %CFLAGS% /DARC_HEADLESS_ONLY tests\headless_game.c src\arc.c /Fo:build\ /Fe:build\headless_game.exe || exit /b 1
python tests\headless_tools_test.py build\headless_game.exe || exit /b 1
python tests\mcp_test.py build\target_headless.exe || exit /b 1
cl %CFLAGS% /DARC_HEADLESS_ONLY tests\test_post.c src\arc.c /Fo:build\ /Fe:build\arc_test_post.exe || exit /b 1
pushd build
python ..\tools\run_retry.py arc_test_post.exe --headless --render-all --msaa 0 --frames 300
set RC=%ERRORLEVEL%
popd
exit /b %RC%

:hothost
if exist build\arc_host.exe del build\arc_host.exe
:hot
rem hot builds share one C runtime (/MD) so memory can pass between the host and the game
set HFLAGS=/nologo /std:c11 /O2 /W3 /MD /D_CRT_SECURE_NO_WARNINGS
set GAME=%2
if "%GAME%"=="" set GAME=samples\hot\hot.c
if not exist build\hot mkdir build\hot
if not exist build\arc_host.exe (
  cl %HFLAGS% /DARC_HOT_HOST src\arc.c /Fo:build\hot\ /Fe:build\arc_host.exe /link %LIBS% /IMPLIB:build\hot\arc_host.lib || exit /b 1
  echo built build\arc_host.exe
)
rem a fresh PDB name each time: a debugger may hold the last one open
cl %HFLAGS% /LD /DARC_HOT_GAME "%GAME%" build\hot\arc_host.lib /Fo:build\hot\ /Fe:build\hot\game_new.dll /link /PDB:build\hot\game_%RANDOM%.pdb || exit /b 1
move /y build\hot\game_new.dll build\game.dll >nul
echo built build\game.dll  (run build\arc_host.exe; build again to reload the code)
exit /b 0

rem ---- web: Emscripten, the same flags as build.sh web (emcc.bat, so every call is "call emcc") ----
:web
set "GAME=%~2"
if "%GAME%"=="" set GAME=samples\demo\demo.c
if not exist "%GAME%" (echo "%GAME%" is not a file & exit /b 1)
for %%F in ("%GAME%") do set NAME=%%~nF
where emcc >nul 2>nul
if not errorlevel 1 goto web_have_emcc
if not defined EMSDK if exist "C:\emsdk\emsdk_env.bat" set "EMSDK=C:\emsdk"
if not defined EMSDK if exist "%USERPROFILE%\emsdk\emsdk_env.bat" set "EMSDK=%USERPROFILE%\emsdk"
if not defined EMSDK (echo emcc not found - install emsdk in C:\emsdk, or set EMSDK to its folder & exit /b 1)
echo Setting up Emscripten from "%EMSDK%"
call "%EMSDK%\emsdk_env.bat" >nul 2>nul
where emcc >nul 2>nul
if errorlevel 1 (echo emcc still not found after "%EMSDK%\emsdk_env.bat" - run "emsdk install latest" and "emsdk activate latest" there & exit /b 1)
:web_have_emcc
if not exist build\web mkdir build\web
set "PRELOAD="
if exist assets set "PRELOAD=--preload-file assets"
call emcc -std=c11 -Iinclude -Wall -O2 -ffp-contract=off "%GAME%" src\arc.c -o build\web\%NAME%.html --shell-file web\shell.html -sUSE_WEBGL2=1 -sMIN_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_arc_web_audio_fill,_arc_web_command" "-sEXPORTED_RUNTIME_METHODS=lengthBytesUTF8,stringToUTF8,HEAPF32" %PRELOAD% || exit /b 1
echo built build\web\%NAME%.html - serve the folder: cd build\web ^&^& python -m http.server 8000, then open http://localhost:8000/%NAME%.html
exit /b 0

rem ---- the post test on Direct3D 11: standard depth, then reversed compared with it ----
:postd3d
cl %CFLAGS% tests\test_post.c src\arc.c /Fo:build\ /Fe:build\post_d3d.exe /link %LIBS% || exit /b 1
if not exist build\postd3d mkdir build\postd3d
pushd build\postd3d
echo ---- standard depth ----
..\post_d3d.exe --render-all --msaa 0 --frames 400 > standard.log 2>&1
findstr /c:"ARC test" /c:"backend=" /c:"compressed texel" standard.log
echo ---- reversed depth ----
..\post_d3d.exe --render-all --msaa 0 --frames 400 --depth-mode reversed > reversed.log 2>&1
findstr /c:"ARC test" /c:"reversed vs standard" /c:"backend=" /c:"compressed texel" reversed.log
popd
echo full logs: build\postd3d\standard.log and build\postd3d\reversed.log
exit /b 0
