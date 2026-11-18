# ARC's MCP server: live tools for AI agents (0.10.10)

`tools/arc_mcp.py` is an [MCP](https://modelcontextprotocol.io) server. An agent in Claude Code, Claude Desktop or any
MCP client can use it to build an ARC game, keep it running, and step, look, change and check it through typed tools.
It can run headless, or in the real window so the person can watch and play along.

It complements the headless scripts and doesn't replace them. Scripts (`--cmd`, `arc_check.py`) give the same result on
every run, which is why tests are trusted. A live session is for exploring: tuning a look, building a level, chasing a
bug. Each session can be saved as an acceptance check (`arc_save_check`), so whatever it finds becomes a script.

## Setting it up

A `.mcp.json` file in a project tells Claude Code to start the server. Then:

- **New games:** `tools/new_game.py` writes one.
- **Existing games:** `python <ARC>/tools/new_game.py D:/Projects/MyGame --add-mcp` writes one, using the path in
  `arc.txt`. It keeps any other servers already in the file.
- **ARC's own repo:** has one too (`--game .`), for work on the engine's test games and samples.

```json
{"mcpServers": {"arc": {"command": "python", "args": ["../ARC/tools/arc_mcp.py", "--game", "."]}}}
```

Claude Code asks once before it trusts a project's server. On Linux or macOS, if there is no `python` command, change
`"python"` to `"python3"`. The server needs no packages; if Pillow is installed it shrinks screenshots.

## The tools

| Tool | What it does |
|---|---|
| `arc_build(window, source)` | Runs the game's `build.bat` / `build.sh`: headless by default, the window build with `window=true`. Shows warnings and errors. In ARC's repo, `source="tests/target_game.c"`. |
| `arc_start(window, args, exe)` | Starts the game with `--stdin` and keeps it running. `window=true` opens the real window; `args` e.g. `"--seed 7 --size 1280x720 --hot"`. |
| `arc_command(command)` | Any agent commands, as in `--cmd` (`"step 30; press D; step 10; release D"`), and their output. |
| `arc_step(ticks, keys)` | Runs N ticks, holding `keys` (e.g. `"D SPACE"`) while they run. |
| `arc_frame(select)` | The `frame` JSON: what is on screen. `select` returns one part, e.g. `"meshes[name=droid]"`, `"texts"`, `"markers[shape=ring]"`. |
| `arc_dump(select)` | The `dump` JSON. `select` e.g. `"game"`, `"game.units[0]"`, `"editor"`. |
| `arc_shot(max_width, hud, camera, crop)` | A screenshot returned as an image (960 wide by default). `camera="orbit 30 25 18"` shows the scene from elsewhere for one shot. |
| `arc_pick(x, y)` | What is at a pixel: meshes, markers, texts, sprites and UI whose boxes hold it, smallest first. |
| `arc_set(name, value)` | Changes a tunable, or lists them all. |
| `arc_editor(command)` | An `editor ...` command, plus the editor's state from the dump. |
| `arc_expect(expect)` | Acceptance-check lines tested on the live game (`game.score == 10`, `count(frame.markers[*]) >= 3`). Each one is recorded. |
| `arc_live(run, speed)` | Window only: `run=true` lets the game run on its own clock (the person can play); `speed` 0.25 to 64 also sets how fast `arc_step` plays. |
| `arc_reload()` | Reloads assets loaded from disk (start with `--hot`). |
| `arc_save_check(path, title)` | Writes the session as an arc-check file: `game .` (or `exe ...`), the `do` lines of every command that changed the game, and each `arc_expect`. Default `tests/live_<time>.md`. |
| `arc_stop()` | Quits the game. |

## Watching the window

With `arc_start(window=true)`:

- **Between calls the window keeps drawing and stays responsive, but the game holds still.** Ticks run only for
  `arc_step` / `step` (at the clock's pace, so motion is visible; `arc_live(speed=8)` speeds it up).
- **Playing along:** `arc_live(run=true)` lets the game run on its own clock, so the person can play while the agent
  watches. `arc_frame`, `arc_dump`, `arc_shot` and `arc_pick` all work while it runs. `arc_live(run=false)` holds it
  again.
- **Pointing at things:** when the person says "that one", `arc_pick(x, y)` says what is drawn there. The window's
  size in pixels is in `dump.width` / `height`.
- **Under the hood:** this is the engine's `live run|hold|speed X` command (0.10.10). A window build started with
  `--stdin` on a pipe no longer blocks while it waits for a command.

## Tips

- Look with `arc_frame` and `arc_dump` first: a picture costs far more tokens than JSON. Use `arc_shot` when the look
  itself is the question.
- Start headless for rules and logic, and with the window for look, feel and levels.
- Save the session (`arc_save_check`) before you stop, whenever you found or fixed something. Run the saved file with
  `tools/arc_check.py` to be sure it replays. Commands like `camera`, `shot` and `frame` are not recorded.
- `arc_build` then `arc_start` again after changing code. Changes to tunables (`arc_set`) and assets (`arc_reload`)
  need no rebuild.

## Checking the server

`python3 tests/mcp_test.py build/target_headless` talks to the server exactly as an MCP client does (part of
`build test`). `--window` with a window build (`build.bat tests\target_game.c`, then `build\target_game.exe`) checks
the live window. `python3 tools/arc_mcp.py --selftest EXE` runs each tool once against ARC's target test game.
