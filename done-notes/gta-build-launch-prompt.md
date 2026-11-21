# Request from Grand Token Acquisition: build.bat offers to launch the game

## What

After a plain `build.bat` (Windows desktop build, no argument) succeeds, ask:

    Launch <Title> now? [Y/N]

with `choice /c YN /n /t 15 /d N`, so an unattended or scripted build is never held up (No after 15 s). Also:

- `build.bat run`: build, then launch without asking.
- `ARC_NO_LAUNCH=1` (environment): never ask (CI, arc_check, tools that call build.bat).
- headless, web, release and test builds never ask.
- launch with `start "" /d "%CD%\build" "%CD%\build\<name>.exe"` so the game finds `build\assets`.
- `./build.sh run` could do the same on Linux.

## Why

Allan asked for it while working on GTA: most builds are followed by starting the game by hand. GTA's own
build.bat has it now (below), but `new_game.py --update-scripts` would overwrite it, so the template is the right
place, and every game gets it.

## GTA's version (in `D:\Projects\GTAarc\build.bat`)

```bat
if /i "%1"=="run" (call :build_win) else (call :build_win %1)
if errorlevel 1 exit /b 1
if /i "%1"=="run" goto launch
if "%1"=="" call :ask_launch
exit /b 0

:ask_launch
if defined ARC_NO_LAUNCH exit /b 0
choice /c YN /n /t 15 /d N /m "Launch Grand Token Acquisition now? [Y/N] "
if errorlevel 2 exit /b 0
:launch
start "" /d "%CD%\build" "%CD%\build\grandtokenacquisition.exe"
exit /b 0
```

## Check (should pass once done)

```arc-check
exe python
args tools/new_game.py build/tmp_launch_game --title "Launch Test"
expect file("build/tmp_launch_game/build.bat") contains "ARC_NO_LAUNCH"
expect file("build/tmp_launch_game/build.bat") contains "choice /c YN"
```

## Done in ARC 0.10.15

- `tools/new_game.py` writes the prompt into every new game's `build.bat`: after a plain `build.bat` it asks
  `Launch <Title> now? [Y/N]` (No after 15 s) and starts `build\<name>.exe` in `build\` so it finds its assets.
  `build.bat run` builds and launches without asking. `ARC_NO_LAUNCH=1` never asks. `headless`, `web` and
  `release` never ask.
- `./build.sh run` builds the Linux version and starts it.
- `tools/arc_mcp.py` sets `ARC_NO_LAUNCH` for the window builds it runs. (`arc_check` and `arc_release` only run
  `headless` and `release`, which never ask.)
- **For GTA:** `python <ARC>/tools/new_game.py D:/Projects/GTAarc --update-scripts` now gives you the same thing, so
  your hand-made version can go (the old scripts are kept as `*.old`).
- Your check couldn't be an arc-check (those run games), so it is `tests/launch_prompt_test.py`. It makes a game,
  checks the prompt, `run`, the CRLF line ends and the MCP server, and runs `build.sh run` for real with a stub
  compiler. It runs in `build test` on both platforms.
