# Working on an ARC game — instructions for AI agents

ARC is built so you can develop and test a game without seeing a screen. Use this loop.

## The loop

1. **Edit** game code (`init`, `tick`, `draw`, `dump`, `command`). Start from `docs/QUICK.md` (every name, one line each, by topic)
   and open the topic page it links (`docs/api/*.md`) for signatures. `include/arc.h` is the single source of truth; those docs are
   generated from it.
2. **Build headless** — fast, no display needed:
   `./build.sh headless path/to/game.c` → `build/game_headless`
3. **Run a script** that plays the game and records what happened:
   `build/game_headless --headless --cmd "step 60; press D; step 30; release D; dump; shot out.png; quit"`
4. **Check results**:
   - `dump` prints one line, `ARC dump {...}` (JSON). It holds engine state plus whatever your `dump()` callback writes. Assert on that.
   - `shot FILE.png` saves a screenshot. Open the PNG to check visuals.
   - A non-zero exit code or `ARC error ...` lines mean something failed.
5. **Repeat**. Build the window or web build (`./build.sh linux|win|web`) only when you need to check real-GPU output.

## Rules that keep the game testable

- All game logic goes in `tick()`. `draw()` only renders and must never change game state.
- Use `arc_rand_*()` (seeded by `--seed`) or an `arc_rng` for randomness. Never use `rand()`, wall-clock time or addresses.
- Read time as `arc_tick_count()` / `arc_dt()`. `arc_real_time()` is only for display.
- Implement `dump()`. Write every fact a test would need: positions, counts, health, score, game phase. If it isn't in the dump, an agent can't check it.
- Register tunables with `arc_var()`. They can be changed with no rebuild: `--set name=value` or the `set` command.
- Add game-specific debug commands through the `command()` callback, for example `spawn 10`, `god`, `level 3`.
  A built-in name (`press`, `release`, `tap`, `step`...) with an argument that is not a key or number is passed to the game too, but
  distinct names are clearer.
- No allocation in `tick`/`draw` beyond the frame arena (`arc_frame()`, reset every frame). Size pools in `init`.
- Games in **immediate mode** (`arc_app.immediate`) draw from `tick()`; the rules above still hold for game state.
- Saves: headless runs write to `./arc_saves` (or `--save-dir DIR`), never to a player's data folder.
- Write code MSVC compiles without warnings (0.9.8): float literals always get `f` (`0.5f`, `4.0f`, also in `a ? 4.0f :
  1.0f`), ints and `size_t` passed or assigned to a float get `(float)` (`(float)arc_width() / 2`, `(float)i * 0.5f`),
  `size_t` into `int` gets `(int)`. ARC's own build test fails on any such warning (`tools/warn_check.py`: real MSVC
  with `/W3 /WX` on Windows, clang's conversion warnings elsewhere); run `python3 <ARC>/tools/warn_check.py` on a game's
  sources the same way if you like.
- Loading in the background (`arc_load_begin` / `arc_load_end`, 0.7.1): a group queued with flags 0 holds ticks until
  it is loaded (the loading screen shows meanwhile), so the game starts on the same tick every run. `ARC_LOAD_BACKGROUND`
  groups finish at different ticks on different machines: never let tick logic branch on them. Headless runs finish
  every group inside `arc_load_end`.

## Agent commands

Pass commands with `--cmd "a; b"`, `--script file.txt`, or interactively with `--stdin`.

| Command | Effect |
|---|---|
| `step N` / `wait N` | advance N ticks (60 per second by default) |
| `press K` / `release K` | hold or let go of a key: `A`..`Z`, `0`..`9`, `SPACE`, `ENTER`, `ESC`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `SHIFT`, `CTRL`, `ALT`, `TAB`, `F1`..`F12`, `MOUSE_LEFT`, `MOUSE_RIGHT` |
| `tap K [N]` | hold K for N ticks (default 1) |
| `mouse X Y` / `click [LEFT\|RIGHT]` | move the mouse / click it |
| `tap PAD_A` / `axis LX 0.5` | gamepad buttons (`PAD_A`..`PAD_GUIDE`) and axes (`LX LY RX RY LT RT`; `axis LX 0` lets go) |
| `pad N connect` / `pad N press A` / `pad N release A` / `pad N axis LX 0.5` / `pad N disconnect` | 0.10.5: pad N of several (couch multiplayer: `arc_pad_down(N, ARC_PAD_A)`, `arc_pad_axis_of`); `dump` lists `pads` |
| `type TEXT` | typed text, read by the game as `arc_text_input()` next tick |
| `touch on` / `touch off` | report the pointer as a finger (`arc_touch_mode()`) |
| `touch down ID X Y` / `touch move ID X Y` / `touch up ID` | 0.10.11 multi-touch: fingers by id (`arc_touch_get`; the first one is also the mouse); `dump.input.touches`, `input.touch[id=1].x` |
| `shot FILE.png` | draw the current state and save it now (works between `step`s); `FILE.jpg` saves a JPEG (quality 90, ~10x smaller) |
| `hud off` / `hud on` | hide or show all 2D on the screen (HUD, text, UI, FPS overlay) for clean screenshots; also `--no-hud` |
| `camera` / `camera EX EY EZ TX TY TZ [FOV]` / `camera orbit YAW PITCH DIST [FOV]` / `camera off` | show the camera, or take over the game's screen camera for screenshots (orbit: around the target of the camera the game last drew with; angles in degrees) |
| `dump [FILE.json]` | state as JSON (to stdout if no file) |
| `reset` / `arc_reset` | back to the state right after init: tick 0, the same seed, vars at their defaults and `--set` values (0.9.8). Games with save_state/load_state are restored; others run init again. A game's own `reset` command wins; `arc_reset` is always ARC's. Use it to run many cases in one `--stdin` process (`tools/arc_batch.py`) |
| `frame [FILE.json]` | what the screen shows, as JSON (0.9.0): the camera, each mesh by name with its on-screen instances (screen box `[x0,y0,x1,y1]`, position, tint), lights, texts, sprites, rects and UI widgets. Name your meshes (`arc_mesh_set_name`); loaded files are named after the file |
| `vars` / `set NAME V` / `get NAME` | tunable variables |
| `ui` / `ui_click LABEL` | list the UI widgets of the last tick (label, kind, rect, value) / click one by label, then `step 2` |
| `inspector [on\|off]` | the tunables panel (also the ` key when run with `--dev` or `arc_app.dev_keys`) |
| `gif FILE.gif N [EVERY] [MAXW]` | record N frames, one every EVERY ticks (default 2), at most MAXW pixels wide (480) — then `step` |
| `shots PREFIX N [EVERY]` | PNG frames `PREFIX_00000.png`... |
| `compare REF.png [TOL%]` | draw now and compare with a reference (made from this frame if missing); `ARC error` + `REF_diff.png` above TOL (0.1%) |
| `seek T` / `rewind N` | go to tick T / N ticks back. Back needs `save_state`/`load_state`: ARC loads the nearest snapshot and re-runs the recorded input silently |
| `state_save [NAME]` / `state_load [NAME]` | snapshots in memory by name, or files (a name with `.` or `/`) |
| `state_check` | save, load, and check the game's dump is unchanged (tests your save_state/load_state) |
| `fps [on\|off]` | the FPS overlay: frame rate, frame/CPU/tick ms, draw calls, a frame-time graph (also `--fps`, F3 with `--dev`) |
| `reload` | reload every asset loaded from disk (with `--hot`) |
| `stats` | timings, draw calls, profiler zones, culled/LOD/static counts and the frame budget (`ARC scene`, `ARC budget` lines) |
| `cells` / `cells on\|off` / `cells draw` | cells and portals: which cells the camera sees / switch tagged-mesh culling / wireframe of cells and portals |
| `lod` / `lod bias X` / `lod force N` | level-of-detail counts / quality bias / always draw level N (`-1` = automatic) |
| `editor on\|off` | the tile level editor (0.7.2; the game must have called `arc_editor_begin`; also F2 with dev keys) |
| `editor paint X Y CH` / `editor fill X0 Y0 X1 Y1 CH` / `editor line X0 Y0 X1 Y1 CH` / `editor flood X Y CH` | edit cells; CH is a character (`#`), `=N` for a number, or a tile name. Each is one undo step |
| `editor undo` / `editor redo` / `editor save` / `editor load` | undo, redo, the game's own save / load (revert) hooks |
| `editor dump` / `editor info` | the grid as `ARC editor row NN ...` lines / tool, tile, undo counts, validate message |
| `editor tile CH` / `editor tool brush\|line\|rect\|fill\|pick` / `editor play\|pause` | select a tile or tool / let the game's tick run while editing (play from here) |
| `editor layer N` / `editor view 2d\|3d\|game` / `editor orbit YAW PITCH DIST [X Z]` | 0.10.3: the layer (deck) edited / the view / the 3D view's camera (degrees, cells) |
| `editor object add X Y KIND [name=value ...]` / `object set N NAME [VALUE]` / `object move N X Y` / `object del N` / `object list` | 0.10.8 (with `arc_editor_desc.level`): objects on the current layer, each one undo step; `dump`'s `editor.objects` lists them |
| `editor setting KEY [VALUE]` / `editor rename NAME` | 0.10.8: the current layer's settings / name (no VALUE removes it; 0.10.9: every word after it counts, `editor rename Cargo Hold`) |
| `editor select X0 Y0 X1 Y1\|none` / `copy` / `cut` / `paste X Y` / `clear` / `rotate` / `flip h\|v` | 0.10.8: a rectangle of cells and the objects in it; rotate / flip work on the clipboard; each edit one undo step |
| `editor onion above\|below\|off` / `editor playtest X Y` / `editor tab tiles\|objects\|layer` / `editor kind KIND` | 0.10.8: the neighbouring layer faint in 2D / the game's playtest hook (the editor closes) / the panel |
| `timeline on\|off` | the replay timeline bar (0.7.2; also F9 with dev keys or in any `--replay` run, `--timeline` at start); `scrub` is the same command |
| `timeline seek T` / `timeline step [N]` / `timeline back [N]` | go to tick T (forward without drawing, back from the nearest snapshot) / N ticks on / back |
| `timeline play\|pause` / `timeline speed X` / `timeline marks` / `timeline info` | desktop playback (headless ignores the pause) / 0.25..8x / `ARC mark tick=N kind=bookmark\|event name=...` lines / state |
| `net host [PORT]` / `net join ADDR` / `net leave` | multiplayer (0.8): host a session (this machine is player 0) / join one (`localhost:47900`, `192.168.1.20`) / leave |
| `net status` / `net players` / `net log [FILE]` | `ARC net status=... seq=... turn=... owner=...` / one `ARC net player` line each / the command log (or save it) |
| `net find` / `net games` | look for LAN games / list the answers (`ARC net game address=...`) |
| `net name NAME` / `net ready [0\|1]` / `net start` | lobby: name, ready flag, start the match (host) |
| `net cmd TEXT` / `net free TEXT` / `net end_turn` | send a game command as this machine's player (`free`: allowed out of turn, e.g. chat) / end the turn |
| `net wait started\|turn\|turn_number N\|connected\|players N\|seq N\|desync\|status NAME [SECONDS]` / `net sync` | pause the script until then (`turn`: my turn with nothing of mine in flight; `players N`: exactly N connected) / until this machine has applied all the host had |
| `net host_online` / `net join_online CODE` / `net room` | online (0.8.5): ask the relay (`--net-relay URL`) for a room (this machine is its host, player 0) / join one by its code / print `ARC net room CODE relay=URL player=N` (also printed when the room is given) |
| `net wait time SECONDS` | pause the script for real seconds while the network runs (0.8.5; e.g. before leaving, so the others dump first) |
| `net resume FILE` / `net sim SPEC` | host: re-apply a saved log (players rejoin by name) / simulate a bad network (as `--net-sim`) |
| `net mode rollback\|turns` / `net delay N` / `net rollback N` | real-time rollback matches (0.8.1): the match mode (host), this machine's input delay (ticks), how far it may run ahead of the inputs it has |
| `net input HEX` / `net input off` / `net input P HEX` | this machine's input as bytes, overriding the game's `net_input` (P: another player, in a synctest) |
| `net stop_at T` / `net stats` / `net synctest N [PLAYERS]` | hold the match at match tick T (inputs still flow) / one `ARC net rollback ...` line / offline check: roll back N ticks every tick and compare the state |
| `net wait tick N\|confirmed N\|waiting\|resumed\|dropped\|alone` | rollback: match tick N reached / every input before N known here and acknowledged by the others / the match waited, ran again, lost a player, has no one else left |
| `post nan on\|off` | 0.10.12: NaN / Inf scene pixels show magenta (post effects otherwise turn them black); `frame.post.nan_pixels` counts them |
| `live` / `live run\|hold` / `live speed X` | 0.10.10: a window build driven through `--stdin` from a program (`tools/arc_mcp.py`) keeps drawing while it waits; `run` lets it tick on its own clock (the person can play), `hold` stops it; X 0.25..64 also sets how fast `step` plays there |
| `echo TEXT` / `quit` / `help` | — |
| anything else | passed to the game's `command()` callback |

Words are split at spaces; "double quotes" keep spaces inside one word and are dropped (0.10.9: `editor object add 3 3
droid note="a repair droid"`). `type TEXT` takes the rest of the line as it is.

With `--stdin`, the engine prints `ARC ready tick=N` whenever it is waiting for a command.

Output lines all start with `ARC `:

- `start`, `ok`, `error`, `dump`, `var`, `stats`, `prof`, `log`, `echo`, `ready`, `exit`
- The self-test also prints `test`.

## Live work through MCP (0.10.10)

`tools/arc_mcp.py` is an MCP server. A `.mcp.json` next to the game starts it: `tools/new_game.py` writes one, and
`--add-mcp` adds one to an existing game. Its tools build, start (`arc_start(window=true)` opens the real window for
the person to watch), step, look (`arc_frame`, `arc_dump` with `select`, `arc_shot` as an image, `arc_pick` at a pixel),
change (`arc_set`, `arc_editor`, `arc_command`) and check (`arc_expect`) a running game. Look with JSON first and take
a shot only when the look matters. Before stopping, `arc_save_check` keeps the session as an arc-check file, so what
you found replays headless. `docs/MCP.md` has the details.

## Starting a new game from a template (0.9.1)

`python3 <ARC>/tools/new_game.py ../MyGame --template tactics` (or `shooter`, `platformer`, `racer`, `cards`, `deck`;
`--list-templates`) makes a playable game: the rules in `tick()`, a full `dump()`, debug commands for
agents, tunables, `save_state`/`load_state` (so rewind and the timeline work), `tests/acceptance.md` with checks of its
rules (`python3 <ARC>/tools/arc_check.py tests/acceptance.md`, which builds the game with its own script: `game .`), a
`release.json`, and notes on its structure at the end of the game's `AGENTS.md`. Grow the game from there and keep the
acceptance checks passing; add a block for each feature. 0.10.14: every template is a finished mini-game (Neon Arena, Toy Skirmish, Hop Hero, a kart
racer, Twenty-One, the deck crawler) with models, animation, sound, music, particles and juice made with Ludo, copied
into the game's `assets/`. `assets/manifest.json` says how each file was made and imported, and the game's `NOTES.md`
explains the code. Swap the art by importing over a file with the same name, or grow the rules and keep the checks
passing.

## Seeing the screen without pictures: `frame` (0.9.0)

`frame` draws the current state and describes it: `ARC frame {"camera":..,"meshes":[{"name":"coin","count":3,"on_screen":2,
"items":[{"box":[x0,y0,x1,y1],"pos":[..],"rgb":"#ffd040"},..]}],"lights":[..],"texts":[{"text":"SCORE 10","x":..}],"rects":[..],
"ui":[..]}`. Boxes are screen pixels (y down). Use it to answer "is the coin visible?", "is the score drawn?", "is the button on
screen?" in a test, and open a `shot` only when the look itself matters. Name meshes made in code (`arc_mesh_set_name(m, "player")`).
A 3D view drawn into a render target (0.10.7) is listed too: its meshes, markers, texts, sprites and rects carry
`"target"` (the target's name from `arc_texture_set_name`, else `"#id"`), meshes also `"pass"`; `box` is on the screen
when the target was drawn there with `arc_draw_texture_ex` / `arc_draw_sprite`, and `target_box` is in the target's
pixels (`"on_screen_box": false` when it never reached the screen). `frame.targets[*]` lists each target pass (size,
camera `eye`, mesh count, `on_screen_at`). Marker shapes are names: `frame.markers[shape=ring]`.
Immediate-mode games (0.10.9): `frame` lists what the last tick drew, targets, markers and texts included, the same
as for games with `draw()`.

## Thick, glowing lines and multi-touch (0.10.11)

- **Vector looks:** `arc_set_line_width(4)` and the 3D lines after it are 4 pixels wide at any distance, with soft
  edges; `arc_set_line_glow(2.5f)` makes them bloom with `arc_post` bloom; `arc_set_line_cap(ARC_LINE_CAP_ROUND)`;
  `arc_draw_lines(pts, n, model, rgb)` draws a wireframe of point pairs. They stay set until changed. Check them with
  `frame.lines.width_max`, `frame.lines.thick` and `frame.dropped`.
- **Phones:** `for (int i = 0; i < arc_touch_count(); i++) { const arc_touch *t = arc_touch_get(i); ... }` in tick:
  `id`, `x`, `y`, `start_x`, `start_y`, `pressed`, `released`. Steer with one finger, fire with another; `arc_ui`
  buttons take any finger. Test with `touch down 0 200 400; step 2; touch down 1 1100 600; step 2` and
  `expect input.touches == 2`. Fingers are recorded and replayed like keys.

## Safe-area HUDs, 2D lines in bulk, music loops, terrain colour maps (0.10.13)

- **HUD inside the safe area:** `arc_rectf sa = arc_safe_area(); arc_push_offset2d(sa.x, sa.y, sa.w, sa.h);` then
  draw the HUD from (0, 0) for a `sa.w` x `sa.h` screen, then `arc_pop_offset2d()`. Everything 2D is moved and
  clipped, and `frame` boxes show where it lands. Test with `args --safe-area 40,0,40,20` and
  `expect frame.texts[text=PAUSED].box[0] >= 40` (one of your texts, matched exactly).
- **Many glowing 2D lines:** `arc_draw_lines2d(pts, n, width, glow, halo_alpha, rgb)` takes point pairs (build them
  in the frame arena). It costs 6 vertices a segment. Raise `arc_app.max_2d_vertices` if `frame.vertices_2d` gets
  near `vertices_2d_max`, and check `frame.dropped == 0` and `frame.lines.lines2d`. `hud off` hides these lines too,
  so don't use it in a check of them.
- **Music with an intro:** `arc_music_play(m, true); arc_music_set_loop(m, 8.0f, 72.0f);` plays 0..72 s, then loops
  8..72 s; `arc_music_set_loop_fade(m, 0.05f)` crossfades the seam. Headless runs have no mixer: add `--audio-sim`
  (`args --audio-sim` in a check) and put `arc_music_position` in `dump()`.
- **Terrain with many kinds of ground:** bake a colour map of the whole terrain and pass `color_map` in
  `arc_terrain_desc` (or call `arc_terrain_set_color_map`); the four splat layers add detail on top. Write a whole
  splat map with `arc_terrain_set_splat(t, rgba)` (`arc_terrain_splat_res(t)` a side), not with `arc_texture_update`
  on `arc_terrain_splat_map`: only `set_splat` updates the weights that `arc_terrain_layer_weight`, paint and scatter
  read.

## Juice and asset manifests (0.10.14)

- **Floating text:** `arc_popup_text(pos, rgb, scale, "+10")` (or `arc_popup_text_2d(x, y, ...)` in pixels) in tick
  when something happens. ARC ages and draws it after `draw()`, so it is looks only; popups close together stack. A
  test sees it in `frame.texts` for about 50 ticks: `expect frame.texts[*].text contains "+10"`, then
  `expect frame.texts[text=+10] missing`.
- **Varied sounds:** `arc_play_var(s, volume, pan, 0.1f)` plays at a pitch within 10%; it never draws from the game's
  `arc_rand_*`, so replays stay exact.
- **Facing:** models face +z. `arc_m4_trs_y(pos, arc_yaw_to(dx, dz), scale)` turns one toward (dx, dz).
- **Recording how assets were made:** import with `--prompt "what you asked the generator for"`. Each import goes into
  `assets/manifest.json` with its options and clips; `arc_import.py --manifest --game . --sources art` re-imports
  every model the same way. Check the green FRONT +Z arrow in the preview before using a model.

## Glow masks and launching (0.10.15)

- **Lamps that switch per draw:** paint a mask in the model's UVs (red, green and blue = three groups: left
  indicators, right indicators, brake lights) and import it with the model: `arc_import.py car.glb --glow-mask
  car_lamps.png`. Then each draw picks what glows: `st.glow_rgb[0] = 0xFFA020; st.glow[0] = blinking ? 2.0f : 0;`
  (0 = off, above about 1 blooms with `arc_post` bloom). One mesh and one mask serve every instance, and identical
  lamp states batch together. `st.emissive_mask` gives a mask for one draw instead of the mesh's own. Check it with
  a `shot` (`samples/lights`, `glow_mask on`, shows it).
- **Building:** a game's plain `build.bat` asks to launch it (No after 15 s); `build.bat run` launches without
  asking, and `./build.sh run` does the same on Linux. Set `ARC_NO_LAUNCH=1` in scripts that call a plain `build.bat`.

## Getting up to speed and checking fast (0.10.16)

- **First, the brief:** `python3 <ARC>/tools/arc_brief.py GAME_DIR` (or a `.c` file in ARC's repo) prints one page:
  the ARC version the game needs, where `init` / `tick` / `draw` / `dump` / `command` / `save_state` live (file:line),
  the `dump()` fields from a real headless run, debug commands, tunables, meshes, assets, features and open requests.
  `--check` adds each acceptance block's result, `--json` gives the same as JSON. Read it before opening the code.
- **Before a commit, the report:** `python3 <ARC>/tools/arc_report.py GAME_DIR` runs the doctor, the warning check,
  every acceptance file and `state_check`, and exits 1 on any failure. Add `--seeds 1-20 --field game.score` for a
  balance sweep and `--sessions` for the newest play session (its F8 bookmarks with replay commands).
- **Through MCP, one call per change:** `arc_run(script="select 0; move 2 0; step 30", expect=["game.units[0].x == 2"])`
  builds if a source changed, starts the game if needed, runs the script and tests the lines, returning
  `PASS` / `FAIL` with the values found. `arc_brief` and `arc_report` are MCP tools too.

## Asking ARC for a change (0.10.13)

When the game needs something ARC doesn't do, write the request in the `docs/REQUESTS.md` template, with an
arc-check block that passes once it's done. Then file it with `python3 <ARC>/tools/arc_request.py request.md --game
"My Game"`: it becomes a GitHub issue, or a file in `<ARC>/incoming/` when GitHub can't be reached. Carry on with a
workaround meanwhile, and say in the request what the workaround is.

## Hints (0.9.0)

ARC prints `ARC hint <what went wrong>: <the fix>` once per kind when a game does something that works but breaks testing or speed:
drawing outside `draw()`, `arc_rand_*` called from `draw()`, loading the same file again and again, loading inside
`tick`/`draw` (a queued load, inside `arc_load_begin`/`arc_load_end`, is fine), a `dump` with no dump callback. Treat a hint as a to-do item; they never stop the game.

## Engine self-test summary (0.9.1)

`build.sh test` / `build.bat test` print everything and save it in `build/test.log`, then end with one line per stage
(self-test, acceptance checks, doctor, API docs, example, import, release dry run, templates, post test) and
`ALL PASSED` or `FAILED`. `ARC error` lines above a passing stage are expected: the tests check that mistakes are
reported.

## Acceptance checks: `tools/arc_check.py` (0.9.0)

Write the result a request wants as an ```arc-check block (see `docs/REQUESTS.md`), then run
`python3 <ARC>/tools/arc_check.py request.md`: it builds the game headless, plays the `do` lines and tests each `expect`
against the dump (`game.score == 10`), the frame (`frame.meshes[name=coin].on_screen == 3`, `count(...)`, `min(...)`,
`max(...)`), the UI and the last `sheet` (`sheet.cells[*].filled`).
It prints PASS/FAIL per line and exits 1 on any failure or `ARC error`. Finish a request only when its block passes.
0.10.12: every block in a file runs (one fresh game each, named by its `##` heading; `continue` carries on in the
previous block's game; `--block N`, `--name TEXT`, `--fail-fast`), and relative paths in `args` (`--save-dir`,
`--record`, `--replay`, `--editor`...) are read from the check file's folder.

## Doctor: `tools/arc_doctor.py` (0.9.0)

`python3 <ARC>/tools/arc_doctor.py [GAME_DIR] [--json]` reads the game's code and assets and lists problems with a fix
for each: `rand()`/time in game code, no `.dump`, loads in `tick`/`draw`, assets named in code but missing, huge
textures or sounds, missing build scripts. ERROR lines exit 1. Run it before a release and when something is odd.
It skips names the game writes (screenshots, saves, `arc_file_write`), `arc_packs.txt` and `WEB_PACKS` packs (0.10.9);
put `doctor: not an asset` in a comment on a line it reports wrongly.

## Importing assets: `tools/arc_import.py` (0.9.0)

`python3 <ARC>/tools/arc_import.py art/ship.glb --height 2` converts and files any asset in one step: models to
`assets/meshes/*.arcm`, pictures to `assets/textures/` (`--compress` for `.arct`), sounds to `assets/sfx/` or
`assets/music/` (long ones), fonts to `assets/fonts/*.arcf`. It renders a preview into `build/import/NAME.png` (open it to
check the model faces the right way and has the right size), warns about off-centre, mis-scaled or huge assets, and prints
the line of C that loads each file. `--dry-run` and `--json` for planning.

0.9.8: `--fit X Y Z` scales each axis to an exact size (a door leaf: `--fit 0.9 2.0 0.05`; 0 = that axis takes the
smallest of the other scales), `--fit-uniform X Y Z` fits inside a box, `--origin bottom|centre|back` places (0, 0, 0).
The final bounds are printed and in `--json` (`"bounds": {"min", "max", "size"}`): read them instead of measuring by hand.

## Releasing: `tools/arc_release.py` (0.9.0)

`python3 <ARC>/tools/arc_release.py --dry-run` first: it runs the doctor and lists what would be built, signed and pushed.
Then `--version 1.2.0` does it: doctor, acceptance files (`--check FILE`), `build release`, signtool (when `release.json`
has a `sign` section), and `butler push` per channel to the itch.io page in `release.json` (`"itch": "user/game"`).
Signing needs the owner's certificate and pushing needs `butler login` on the owner's machine: never invent either.

## Navigation (0.9.2)

Build a navmesh once (`arc_navmesh_create`, `arc_navmesh_add_grid` / `_add_mesh` / `_add_triangles`,
`arc_navmesh_build`), keep `arc_nav_agent` structs in the game state, `arc_nav_agent_goto` when a target changes and
`arc_nav_agent_update` every tick (`arc_nav_agents_separate` after, for crowds). Put each agent's position, `moving`,
`arrived` and `failed` in `dump()` so tests can check movement; draw `arc_navmesh_draw` / `arc_nav_agent_draw` while
debugging and look at a `shot`. `samples/nav/nav.c` shows all of it. Everything is deterministic and agents are plain
data, so `save_state`/rewind work.

## Animation graphs and IK (0.9.3)

Build an `arc_anim_graph` once (parameters, states, 1D blends, transitions with conditions/exit times/fades, events
on clips); keep an `arc_animator` per character in the game state; `arc_animator_set` / `_trigger` and
`arc_animator_update` in tick, react to `arc_animator_fired(&an, event)` there (footstep sounds, the hit frame), and
`arc_animator_pose` + `arc_pose_two_bone_ik` (feet on the ground, hands on things) before drawing. Put the state name
(`arc_animator_state_name`), `arc_animator_progress` and anything IK placed (foot heights) in `dump()` so tests can
check them. Transitions are tried in the order they were made: make the urgent ones (jump, hit, death) first.
`samples/anim/anim.c` shows all of it on a stick figure made in code.

## Physics (0.9.4)

`arc_physics_create`, static ground (`_add_plane`, `_add_mesh`, `_add_triangles`), bodies with `arc_physics_add`
(sphere, box, capsule; dynamic, static, kinematic), `arc_physics_step(w, arc_dt())` in tick, then draw each body with
`arc_physics_matrix`. It is deterministic, so put `arc_physics_save` / `_load` inside the game's save_state/load_state
(with the game struct) and rewind, replays and rollback keep working. Use `arc_physics_events` for impact sounds and
damage, `arc_physics_raycast` for shooting and picking, `arc_physics_draw` while debugging. Put the positions and
`arc_physics_sleeping` of the bodies a test cares about in `dump()`. `samples/physics/physics.c` shows all of it.

Joints (0.9.7): `arc_physics_add_joint` with `ARC_JOINT_HINGE` (doors, wheels, levers: an axis, optional `limit` and
`motor`) or `ARC_JOINT_BALL` (ropes and chains: a row of small bodies joined end to end). Pivot and axis are world
values as the bodies stand; `b = 0` pins to the world. `arc_physics_joint_angle` is a's turn relative to b (put it in
`dump()`); drive things with `arc_physics_joint_motor`. Joints are saved with the physics state.

Ball joint limits and ragdolls (0.9.8): a ball joint's `axis` is the child's long axis; `limit` / `lower` / `upper` bound
its twist and `cone` (radians) keeps it within `cone_axis` (`arc_physics_joint_swing` reads the angle). When a character
dies, `arc_ragdoll_from_pose(phys, rig, &pose, model, &(arc_ragdoll_desc){ .mass = 80, .prev = &last_pose, .prev_dt =
arc_dt() }, &G.rag)` builds 11 capsules with human limits from its skeleton (mixamo, Unreal or Blender joint names); each
frame `arc_ragdoll_pose` gives the pose to draw the skinned mesh with. `arc_ragdoll_humanoid` makes one without a
skeleton (draw it with `arc_ragdoll_draw`). Keep the `arc_ragdoll` struct in the game state next to the physics save;
put a part's position (`arc_physics_pos(phys, G.rag.body[ARC_RAG_HEAD])`) in `dump()`.

## Lights and light shadows (0.9.5)

Add lights in draw() like drawing: `arc_light_point` / `arc_light_spot`, up to 256 a frame (each pixel only pays for
the lights that reach it, so give them short ranges). For a torch, lamp or searchlight that should cast shadows use
`arc_light_point_ex` / `arc_light_spot_ex` with `ARC_LIGHT_SHADOW`: at most 4 a frame, and each shadowed point light
redraws the scene 6 times, so shadow only the lights that matter. Glowing things that are lights themselves (wisps,
lamps' bulbs) should not cast: `arc_mesh_set_cast_shadow(m, false)`. The `frame` command lists lights (with
`"shadow":true`); `arc_light_count()` / `arc_light_shadow_count()` are handy in `dump()`. `samples/lights/lights.c`
shows all of it.

## Terrain (0.9.6)

`arc_terrain_create` from a height grid (noise with `arc_noise_fbm2`, or a heightmap image's grey values) with up to 4
ground layers; lay the layers down with `arc_terrain_splat_rules` (height and slope) and `arc_terrain_paint`, draw
with `arc_terrain_draw`. Stand things on it with `arc_terrain_height` and `arc_terrain_normal`; pick with
`arc_terrain_raycast`; give physics and navmeshes `arc_terrain_triangles`. Grass, rocks and trees: `arc_scatter_create`
per mesh (by layer, height and slope, seeded) and `arc_scatter_draw`; call `arc_scatter_rebuild` after editing the
ground. Built-in shapes are centred, so scatter them with `sink = -0.5`. Put `arc_scatter_count` / `_drawn` and a few
heights in `dump()`. `samples/terrain/terrain.c` shows all of it.

## Grid tactics, markers, tiles, fog of war, cutaway (0.9.9)

Board rules: keep an `arc_board` in the game struct (plain data: it saves with save_state). `arc_board_set_tile(b, x, y,
cost, cover)` (cost 0 = blocked; `ARC_COVER_HALF` / `_FULL`), `arc_board_set_occupant` for units. Then
`arc_board_los` (symmetric, with the best cover on the way), `arc_board_reach` (costs within a budget, no corner
cutting), `arc_board_path` (cheapest route, fixed tie order), `arc_board_arc` (front / side / rear for 8 facings),
`arc_board_range`, `arc_board_edge_tiles` (drop zones, exits). All integer: the same results everywhere.

Markers: `arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x, .z, .size, .fill_rgb, .fill_alpha, .height =
my_height_fn })` each frame for tile highlights, team rings, arrows and drop zones (`.label = "DROP ZONE"`). They
follow the ground (a height callback, `.terrain`, or a flat `.y`), never z-fight, and `.on_top` shows through units.
They are listed in `frame.markers` (labels in `frame.texts`), so tests can `expect count(frame.markers[*]) >= 3`.
`samples/board/board.c` shows the board and markers together.

Tile maps: put every tile look in one `arc_texture_array` (up to 64 layers of one size), `arc_mesh_set_texture_array`
on the tile meshes, and draw the map with `arc_draw_tiles(cells, w, h, cell, origin, meshes, n)`: one draw call per
mesh kind, with a layer, quarter turn and tint per cell. Single draws: `arc_draw_mesh_layer` or `arc_mesh_style.tex_layer`.

Fog of war: one texel per tile in a small texture (red 255 seen, ~80 remembered, 0 unseen), `arc_set_world_mask(mask,
origin, cell, dark_rgb, softness)`; update it with `arc_texture_update` when vision changes. Everything 3D is darkened
by it; leave cursors out with `arc_mesh_style.no_world_mask`. Cutaway: `arc_mesh_set_cutaway(wall_mesh, true)` once,
then `arc_set_cutaway(eye, focus, radius, keep_height, ARC_CUTAWAY_DITHER)` each frame. Burnt wrecks without a second
model: `arc_mesh_style.charred = 1` (or `darken`); at load time `arc_image_multiply` / `_overlay` / `_noise` / `_char`.

## Enemies and action: brains, senses, shots, camera rigs (0.10.0)

Keep everything in the game struct: these are plain data with fixed sizes, so `save_state` is one `memcpy` and
rewinds and replays stay exact. `samples/action/action.c` shows all four together.

- **Brains:** describe states and transitions once (`arc_brain_def_state`, `arc_brain_def_transition` with
  `ARC_BRAIN_IF(kind, slot, value)` conditions), keep an `arc_brain` per enemy, and call
  `arc_brain_update(&e->brain, &DEF, e, dt)` in tick, after writing what it knows onto its blackboard
  (`brain.f[...]`). `arc_brain_watch("droid", &G.e[0].brain, n, sizeof G.e[0], &DEF)` puts every state in `dump`
  as `brains.droid[i].state`. Tests can then say `expect brains.droid[3].state == "SEARCH"`, and
  `brain droid 3 FLEE` forces one.
- **Senses:** `arc_senses_add` per enemy, then each tick `arc_senses_set` (position and facing),
  `arc_senses_noise` for gunfire and explosions, and `arc_senses_update` with the targets (sample points on the
  body) and a line-of-sight test of your own. Read `arc_senses_knows(&s, agent, target)` (`seen_now`, `pos`, `how`).
  Use `arc_senses_alert` to call others and `arc_senses_search_points` to search.
- **Shots:** set the world once (`arc_shots_set_world`), the moving targets each tick (`arc_shots_targets`), then
  `arc_shot_fire`, `arc_shots_update(dt)`, and apply damage from `arc_shots_hits`. Call `arc_shots_draw()` in draw.
  Don't hand-roll tracers or bullet walks.
- **Camera rigs:** one `arc_cam_rig` per view (or one that you `arc_cam_rig_blend` into the next). Set
  `target` / `yaw` each tick, call `arc_cam_rig_update`, and pass `arc_cam_rig_camera` to `arc_set_camera`.
  Shake on hits with `arc_cam_rig_shake`. `cam rig follow` / `cam shake 0.5` work in tests, and `frame.camera.rig`
  shows the rig.

## Text, layout, review sheets, paint patterns (0.10.1)

- **Text:** bake one distance-field font per face, not a file per size:
  `python3 <ARC>/tools/font_bake.py Font.ttf assets/fonts --sizes 48 --sdf`. Then
  `arc_font_draw_ex(f, x, y, px / 48.0f, 0, rgb, text)` at any pixel size. Text starts on whole pixels by default.
- **Built-in font (0.10.12):** drawn on whole pixels; `arc_text_fit_scale` gives whole sizes from 1 up, so the 8x8
  font keeps every row (below scale 1 it can't: bake a font for small text). `arc_text_snap_builtin(ARC_TEXT_SNAP_SIZES)`
  rounds every size of 1 and more too.
- **Panels the player moves (0.10.12):** save `arc_ui_panel_offset(title, &dx, &dy)` with the game's settings and give
  it back with `arc_ui_set_panel_offset` at start; place things around `arc_ui_panel_rect` (the real size). Dragged
  panels keep their title bar on screen.
- **HUDs that fit every screen:** call `arc_ui_scale_auto(1280, 720, 0.75f, 4)` once per tick, place panels with
  `arc_ui_begin_anchored(name, ARC_ANCHOR_BOTTOM_RIGHT, 10, 10, 220, 0)` (UI units, inside `arc_safe_area()`), and
  end the tick with `arc_ui_require_landscape(NULL)` (or `"TURN SIDEWAYS\nOne line about why"`; it uses the UI's
  font and scale and wraps) for landscape-only games. Check them with
  `ui_fit 1280x720 1920x1080 2560x1080 1024x768 800x600 1366x768` in an arc-check block (add
  `args --safe-area 0,40,0,60` to test a notch).
- **Art review:** call `arc_sheet_add(mesh, model, &style, "label")` in draw for each thing to review, then
  `sheet review.png 4` (or `cell 320x320`, `transparent`). Sheets ignore the game's HUD switch, post, fog and world
  mask. In an arc-check block, `expect min(sheet.cells[*].filled) > 0.1` checks no cell is blank. For one framed view in game code, use
  `arc_camera_fit(cam, lo, hi, aspect, 0.05f)`. Use `shot out.png crop` instead of cropping with PIL.
- **Liveries:** use `style.pattern = (arc_paint_pattern){ .kind = ARC_PAINT_STRIPES, .rgb = 0x2040E0, .scale = 0.2f,
  .angle = 0.8f, .part_mask = ARC_PART_LEGS }` (with `.armour_mask = true` and a `paint_mask` to keep trim and
  joints) instead of baking a mask per pattern. `*Shoulder*` / `*Clavicle*` joints count as arms; for other rigs map
  names with `arc_paint_parts_rule("*Pauldron*", ARC_PART_ARMS)` once in init.

## Split-screen (0.10.2)

- **Several players in one window:** in draw, wrap each player's view in `arc_viewport_begin(x, y, w, h)` /
  `arc_viewport_end()` and draw the world and that player's HUD inside it as if it were the whole screen
  (`arc_width()` etc. are the viewport's). Don't draw each view into a render target by hand. For `arc_ui_*` made in
  tick, wrap each player's widgets in the same calls there. One `arc_cam_rig` per player, with `rig.aspect` set to
  the view's width / height. Lights added inside a viewport light only that viewport. See `docs/split-screen.md`
  (it also covers gamepads: 0.10.5 reads up to 4, `arc_pad_down(pad, button)`, pad N drives player N).
- **Checking it:** `frame.viewports[*]` lists each view's `rect`, camera and meshes; `arc_screen_ray(arc_mouse())` in
  tick picks through the view under the mouse.

## Feature macros (0.9.8)

`arc.h` defines `ARC_HAS_<GROUP>` for each feature group (value: the `ARC_VERSION_NUM` it arrived in), e.g.
`ARC_HAS_TARGET_POST`, `ARC_HAS_STAMPS`, `ARC_HAS_TERRAIN`, `ARC_HAS_RAGDOLL`. Use `#if ARC_HAS_RAGDOLL` in game code, or
grep for the macro in a build script, instead of searching `arc.h` for function names.

## Useful flags

| Flag | What it does |
|---|---|
| `--headless` | Runs without a window (works with every binary). |
| `--frames N` | Quits after N ticks. |
| `--seed N` | Sets the random seed. |
| `--set name=value` | Presets a tunable variable. |
| `--size WxH` | Sets the window or render size. |
| `--record FILE` / `--replay FILE` | Records input, or plays it back; a replay reproduces a run exactly. |
| `--dump-at-exit FILE` | Writes a JSON state dump when the program exits. |
| `--render-all` | Renders every frame when headless (for benchmarking). |
| `--draw-last N` | Headless: draws (render targets included) only the last N ticks before `--frames` ends or a `--shot-at` shot (0.9.8). Immediate-mode games with post effects in a target draw every tick otherwise: a 600-tick screenshot run goes from minutes to seconds. Use 2 or more if the look depends on earlier frames. |
| `--shot-at 40,80,120` | Screenshots after those ticks, drawn only then (`40=boom.png` names one; `--shot-name shot_%d.png`). Without `--frames` the run ends after the last shot (0.9.8). |
| `--no-assets` | Loaded images keep their size but not their pixels (a flat colour), sounds their length but not their samples: lighter headless test processes (0.9.8). `frame`, `dump` and picking still work; pictures don't. |
| `--mute` | No audio. |
| `--audio-sim` | Headless: run the audio mixer at the tick rate (no sound card), so music positions, loops and fades can be tested (0.10.13). |
| `--no-vsync` | Turns off vsync. |
| `--save-dir DIR` | Where `arc_save` writes (headless default: `./arc_saves`). |
| `--hot` | Reload textures, meshes, animations, sounds and levels (`arc_level_load`) when their files change (desktop). |
| `--editor [FILE]` | Start in the tile editor (0.10.8; games that call `arc_editor_begin`; F2 then works without `--dev`). FILE: a level file read into `arc_editor_desc.level`. |
| `--dev` | Developer keys: ` toggles the tunables inspector, F3 the FPS overlay, F2 the tile editor (games that call `arc_editor_begin`), F9 the replay timeline. |
| `--timeline` | Show the replay timeline from the start (0.7.2). |
| `--fps` | Show the FPS overlay. |
| `--msaa N` | Multisampling (overrides `arc_app.msaa`). |
| `--depth-mode M` / `--reversed-z` | Depth buffer: `standard`, `reversed` or `infinite` (overrides `arc_app.depth_mode`, 0.7.2). |
| `--no-compressed-textures` | Decode block-compressed textures (`.arct`, `.arcm` v4) on the CPU, as on a GPU without them (0.7.1). |
| `--load-items N` | Queued loads (`arc_load_begin`/`end`) finish N files a frame, also headless, where a group otherwise finishes inside `arc_load_end` (0.7.1). |
| `--net-name NAME` | This machine's player name in sessions (default: the user name). |
| `--net-sim SPEC` | Simulate a bad network on this machine's sends: `lag=MS,jitter=MS,loss=P,drop_at=TICK,rejoin_at=TICK,timeout=MS,seed=N` (0.8). |
| `--net-version V` | Pretend to be game version V (tests of the version check). |
| `--net-report DIR` | Where desync reports go (default `net_desync`). |
| `--net-verbose` | Print every applied net command (`ARC net apply seq=... tick=... from=...`). |
| `--net-delay N` / `--net-rollback N` / `--net-mode rollback` | Rollback matches (0.8.1): input delay, how far ahead of the inputs, the match mode. |
| `--net-lag MS` / `--net-loss PCT` / `--net-jitter MS` | A bad connection on this machine's sends, one setting at a time (0.10.4; the same as `--net-sim`). |
| `--net-relay-only` | Online rollback matches: no hole punching, every datagram through the relay (0.10.4; also `net direct off`). |
| `--net-synctest N` | Check a game for rollback: every tick roll back N ticks and compare the state (`ARC error synctest` + report when `save_state` misses something). |
| `--net-relay URL` | The relay for online sessions (0.8.5): `ws://127.0.0.1:47880`, `tcp://host:port`; overrides `arc_net_desc.relay`. |
| `--browser-decode` / `--no-browser-decode` | Web (0.8.6): the browser decodes queued JPEG/PNG loads (as `arc_app.browser_decode`), or not. `--browser-decode=bytes` reads its pixels back instead of uploading GPU-direct (tests). As URL arguments: `index.html?--browser-decode`. |
| `--image-decoder bitmap\|imagedecoder\|img` | Web (0.8.6): force the browser API used for that (default: picked per browser). |

## Web builds that load fast (0.8.6)

`docs/WEB.md` has the whole picture and measurements. In short, all opt-in:

- `arc_app.browser_decode = true`: JPEG and PNG images in queued loads (`arc_load_begin`/`arc_load_end`: textures and the
  maps inside `.arcm` meshes) are decoded by the browser off the main thread and uploaded straight to the GPU. PNGs are
  identical to ARC's decode; JPEGs differ by a level or so per channel. Desktop and headless runs never change, so agent
  tests, dumps and screenshots stay the same.
- `WEB_PACKS="levels music"` (or `"*"`) in the game's build.sh / build.bat (or `tools/new_game.py --web-packs`): the web
  build preloads only the rest of `assets/` (keep the title screen in `assets/first/`) and puts each listed folder in a
  pack next to `index.html`. Queued loads of files in a pack fetch it on their own; `arc_pack_fetch("levels.arcpak")`
  starts it early; `arc_pack_ready` / the group's `arc_load_progress` tell when. Packs (and the first package) are kept
  in the browser's cache, so a second visit downloads nothing big. Only queued loads can wait for a pack: load files that
  live in packs inside `arc_load_begin`/`arc_load_end`, never with a plain load at once. `arc_file_listed(path)` (0.10.9)
  says whether a file is in the assets or in a pack that is coming (`arc_file_exists` is false until the pack is in).
- Desktop and headless builds read everything from `assets/` as before (`arc_pack_fetch` is done at once), so the same
  game code works everywhere. Test the web build itself in headless Chromium: `python3 tests/web_load_test.py`.

## Checking motion, UI and visuals

- Motion: `gif out.gif 60; step 120` and open the GIF (it loops).
- Menus: `ui` prints every widget; `ui_click Start; step 2` presses one exactly as a player would (recorded in replays).
- Visual regression: `compare golden/title.png 0.1` after reaching a known state; commit the golden PNGs.
- Time travel (games with `save_state`/`load_state`): `state_save before; ...; state_load before` to try alternatives
  from the same moment, `rewind 300` to look again, `state_check` once to prove the callbacks round-trip.

## Level editing and the replay timeline (0.7.2)

A tile game can hand its grid to ARC's editor (`arc_editor_begin`, see `include/arc.h` and `samples/tiles`): the game keeps
its data and its file format, ARC edits the cells in place and calls the game back for every change. Headless:

```sh
build/game_headless --headless --cmd "editor paint 3 3 #; editor fill 2 2 5 5 X; editor flood 9 4 E; editor undo; editor dump; editor save; dump"
```

The `dump` then has an `editor` object (open, playing, dirty, tool, tile, undo, redo, message: the game's validate text, e.g.
"deck has no lift"). The editor works inside the tick from the tick's input (like the UI kit), so mouse and key edits are in
recordings and replays; the agent `editor ...` commands change the grid directly and, like `set`, are not recorded.
While the editor is open the game's `tick()` is paused (ARC's tick count goes on) until `editor play` (P).

0.10.3: give `tile_mesh` (the meshes the game builds for a cell) and the editor gets its own 3D view of the grid (V, or
`editor view 3d`; `editor orbit YAW PITCH DIST` aims it); give `layers` and each grid (a deck, a floor) is a layer
(PAGE UP / DOWN, `editor layer N`, counting from 0). The `editor` object adds `view`, `layer`, `layers`, `hover`. In the
3D view `frame.viewports[0].meshes` lists what it drew. The `deck` template shows all of it.

0.10.8 level files: don't write a map format; use `arc_level` (`arc_level_load` / `arc_level_save`, a text format with
layers of `|row|` lines, `key = value` settings and `object X Y kind name=value` lines that diffs well and saves back byte
for byte). Give the editor `.level` (and `.level_path`): it then edits the level's layers, `objects` (kinds with fields:
INT, NUM, TEXT, FACING, CHOICE, BOOL) and `layer_fields` (each layer's settings) in Objects and Layer tabs, copies,
cuts, pastes (R turns, F flips the clipboard), shows the layer above or below faintly (O), and test-plays with
`playtest(layer, x, y)` (T; F2 comes back with the edits). Read the objects and settings with `arc_level_object_*` /
`arc_level_get`, and rebuild when `arc_level_reloaded(lvl)` says the file changed (`--hot`, or the editor's load).
Check edits headless: `editor object add 3 3 droid class=4; editor save` then `expect editor.objects[0].fields.class == "4"`.

To look through a recording: `--replay FILE` (a play session's `input.rec` brings its `events.jsonl` markers and F8
bookmarks) and `timeline seek T; dump; shot t.png`. Seeking is exact: the state at T is the same as playing straight to
T (going back needs the game's `save_state`/`load_state`). `timeline marks` lists the bookmarks and events.

## Multiplayer (0.8): turn-based LAN sessions

Read `docs/MULTIPLAYER.md` for the whole picture; `samples/netturns` is a complete example. The rules a multiplayer game
must follow (they also keep a single-player game agent-friendly):

- **Every rules change goes through `command()`.** The game sends `arc_net_command("move 3 5 7")` instead of applying it;
  every machine applies every command in the same order at the start of a tick. Never change rules state in `tick()`
  (animate there) or `draw()`. Offline the same call applies locally next tick, so there is one code path.
- **Inside `command()`:** `arc_net_command_from()` is the sending player (-1 = typed by an agent on this machine only);
  `arc_rand_*()` is the match's own generator. Check that the sender may move that unit (the host only checks turns).
- **Deterministic maths in rules:** `arc_sinf`, `arc_cosf`, `arc_atan2f`, `arc_expf`, `arc_powf`, `arc_floorf`... (ARC's
  own, bit-identical on every platform), never the C library's or helpers built on them (`arc_m4_rotate_*`, quaternions).
  ARC's physics, navigation, terrain heights, splat rules, scatter, `arc_rng` and noise are bit-identical on every
  compiler too (0.9.7, checked by the self-test).
- **One random draw per statement:** never `f(arc_rand_int(..), arc_rand_int(..))` or `arc_v3_make(rng(), 0, rng())`:
  C leaves the order of a call's arguments open, and gcc and clang evaluate them in opposite orders. Draw into
  variables first.
- **No fused multiply-add in rules code:** put this at the top of the game's rules files (ARC's own `src/arc.c` has it):
  `#if defined(__clang__)` / `#pragma STDC FP_CONTRACT OFF` / `#elif defined(__GNUC__)` / `#pragma GCC optimize
  ("fp-contract=off")` / `#elif defined(_MSC_VER)` / `#pragma fp_contract(off)` / `#endif`. Without it, clang on an ARM
  or FMA machine and gcc in GNU mode compute `a * b + c` with one rounding and drift from everyone else.
- **Checking a build:** `build.bat tests\det_check.c` (or `build.bat web tests\det_check.c`, `build.sh linux
  tests/det_check.c`) shows whether this compiler matches ARC's golden hashes; on a mismatch run it with `--trace a.txt`
  on both machines and `python tools/det_compare.py a.txt b.txt` names the first step they part.
- **`save_state`/`load_state`** (or `arc_app.net_state`) hold the rules state: its bytes are hashed after every command to
  catch desyncs. No time (`arc_tick_count`) in rules, no pointers, no uninitialised bytes.
- **Turns:** `arc_net_turn_end()`, `arc_net_my_turn()`, `arc_net_turn_owner()`; rules work at a turn change goes in the
  `arc_app.net_event` callback (`ARC_NET_EV_TURN`, `ARC_NET_EV_START`...), which runs at the exact point in the sequence.

Testing headless: run one process per player on localhost and script them; `net sync` before a final `dump` on each, then
compare the dumps' `game` sections (and `net.seq`, `net.last_hash`):

```sh
build/game_headless --headless --net-name A --cmd "net host 47900; net wait players 2; net start; net wait turn; net cmd move 0 1 1; net end_turn; net wait turn_number 3; net sync; dump a.json; net wait players 1; quit" &
build/game_headless --headless --net-name B --cmd "net join localhost:47900; net wait turn; net cmd move 2 6 6; net end_turn; net wait turn_number 3; net sync; dump b.json; net leave; quit"
```

`--net-sim lag=80,jitter=40,loss=0.05` adds a bad network; `--net-sim drop_at=300,rejoin_at=700` pulls a client's cable and
plugs it back in; `--net-version 0.9` checks the refusal. A typed debug command that changes one machine's state makes a
desync: every machine prints `ARC error desync seq=N tick=T` and writes `net_desync/seqN_pP/` (dump.json, log.txt,
info.json, state.bin) at the same sequence number: diff them. `python3 tests/net_test.py` runs ARC's own scenarios
(add `--client-exe "wine build/netturns_headless.exe"` for Windows clients against a Linux host).
`--record FILE` writes `FILE.net` too, so `--replay FILE` reproduces a networked match offline.

## Multiplayer online (0.8.5): the relay, room codes, simultaneous turns

Internet and web play go through `arc_relay` (`./build.sh relay`, `docs/RELAY.md`): the same commands, turns, events and
hashes as a LAN session, in rooms named by short codes. `arc_net_desc.relay` (or `--net-relay URL`) sets it;
`arc_net_host_online()` makes a room (`ARC_NET_EV_ROOM` gives the code), `arc_net_join_online("K7QF")` joins it.
`arc_net_desc.turns = ARC_NET_TURNS_SIMULTANEOUS`: everyone sends orders in the same turn and it ends when every connected
player has called `arc_net_turn_end()` (`ARC_NET_EV_TURN_DONE` per player, then `ARC_NET_EV_TURN` with `ARC_NET_EVERYONE`).
0.10.4: rollback matches play online too (desktop: UDP through the relay, direct once hole punching works; `docs/RELAY.md`). Testing:

```sh
build/arc_relay --port 47880 &
build/game_headless --headless --net-name A --net-relay ws://127.0.0.1:47880 --stdin        # net host_online -> "ARC net room K7QF ..."
build/game_headless --headless --net-name B --net-relay ws://127.0.0.1:47880 --cmd "net join_online K7QF; net wait started; ...; net sync; dump b.json; net wait time 1.5; net leave; quit"
```

Read the code from the host's output. Online the host's `net sync` also goes through the relay, so a player that leaves
must wait a moment after its final dump (`net wait time 1.5`) or its LEAVE lands in another machine's dump.
`python3 tests/relay_test.py` runs ARC's own online scenarios (`--web` adds the web build in headless Chromium,
`--client-exe "wine /abs/path/game_headless.exe"` Windows clients).

## Multiplayer (0.8.1): real-time rollback matches

For action games (`arc_net_desc.mode = ARC_NET_ROLLBACK`; `samples/netarena` is the example; the whole guide is in
`docs/MULTIPLAYER.md`, "Real-time games"):

- **The rules read only `arc_net_input(player, &size)`** (or `arc_net_key_down(player, k)` with ARC's default input),
  never `arc_key_*`: pack this machine's input in `arc_app.net_input` (a few bytes).
- **`save_state`/`load_state` must hold everything `tick()` changes** that a later tick reads: ticks are re-run after a
  correction. Prove it with `--net-synctest 8` (it fails at the first tick that differs when run again).
- **Time is `arc_net_match_tick()`**; `arc_rand_*` is the match's (reseeded at match tick 0); set up in `ARC_NET_EV_START`.
- **Side effects:** ARC plays each sound once and logs each `arc_event` once (when confirmed); skip your own (shake,
  rumble, saves) while `arc_net_resimulating()`.

Testing: two headless processes (a match runs in real time, headless too), `--net-sim lag=..,jitter=..,loss=..,dup=..`,
then `net stop_at T; net wait confirmed T; dump` on each and compare the `game` sections; `--record` and `--replay` the
recordings offline to the same state. `python3 tests/rollback_test.py` runs ARC's scenarios (add `--client-exe "wine
/abs/path/netarena_headless.exe"` for Windows clients).

## Regression testing pattern

Record a good run, then replay it after every change and compare the dumps:

```sh
build/game_headless --headless --record good.rec --cmd "...inputs...; quit" --dump-at-exit expected.json
# after changes:
build/game_headless --headless --replay good.rec --dump-at-exit actual.json
```

Then diff the `game` sections of the two JSON files.

## Balance sweeps

To see how a change plays out across many games, run the game headless over a range of seeds and tabulate fields
from its dump (the `game` section, `vars` or `stats`):

```sh
python tools/sweep.py build/game_headless --seeds 1-50 --frames 6000 --field game.winner --field game.turns
python tools/sweep.py build/game_headless --seeds 20 --frames 6000 --set enemy_hp=40 --csv hp40.csv
```

It prints one row per seed, then the median, min, max and mean of each field. Wildcards work (`--field "game.*"`);
`--cmd` gives every run the same agent commands. Compare two settings by running it twice with different `--set`.

Many cases (0.9.8): `python3 <ARC>/tools/arc_batch.py --exe build/game_headless -j 4 --args "--no-assets" cases/*.txt`
runs agent scripts through a few long-lived processes, `arc_reset` between cases, one JSON line per case (its dumps,
errors, ok) — far faster than a process per case, and the same results.

## Page art and releases

```sh
build/game_headless --headless --size 1920x1080 --cmd "step 600; hud off; shot x.jpg; camera orbit 30 25 18; shot cover.jpg; quit"
python tools/itch_art.py --title "My Game" --cover cover.jpg --banner cover.jpg --out dist/itch --font fonts/title.ttf
./build.sh release          # or build.bat release: dist/<Title>-windows.zip and dist/<Title>-web.zip
```

Other art and sound helpers: `tools/recolor_texture.py` (one texture per team colour from a model's red or pale
parts), `tools/audio_prep.py` (trim silence and even out the loudness of sound effects, voices and loops; ffmpeg),
`tools/grime.py` (see-through dirt overlays with ragged, edge-faded alpha from a dirt photo, for `arc_draw_stamps` or a
material's detail map; the same as `arc_image_breakup` at run time; Pillow and numpy).

## Play sessions and feedback

A game with `arc_app.sessions = true` records every real play in `sessions/<date-time>/`: `input.rec` (exact
replay), `events.jsonl` (the game's `arc_event` lines), `log.txt`, `summary.json` and `mark_N.jpg` for each F8 the
player pressed. To investigate a report:

```sh
python tools/sessions.py sessions/ --marks --exe build/game_headless      # what was flagged, and replay commands
build/game_headless --headless --replay sessions/<id>/input.rec --cmd "step N; dump; shot moment.png; quit"
python tools/sessions.py sessions/ --event duel_end --field won              # balance across sessions
```

Log the facts that matter for balance and bugs with `arc_event` (results, deaths, scores, difficulty changes) in
`tick()`; replays reproduce them exactly.
