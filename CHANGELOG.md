# ARC changelog

Version numbers follow `ARC_VERSION` in `include/arc.h`. Games can check it at compile time.

## 0.10.18 — 25 Nov 2026: the demo becomes TOY BOX BLITZ

From Allan's request (#13): `samples/demo` (SWARM) is remade to show off what ARC does now. You play a wind-up tin
robot defending a child's bedroom floor from clockwork bugs. No engine changes. New check: `tests/demo_check.md` (11 blocks, in
`build test`).

- **Art (Ludo, imported with `arc_import.py`, 3.5 MB in `samples/demo/assets/`, `manifest.json` says how):**
  - the tin robot, rigged, with idle, run, hit and death clips;
  - three bugs: the ant (swarms), the beetle (still 4 hits), and a new flea that hops;
  - alphabet blocks (the wall ring and cover), crayons and a toy drum;
  - a play-mat floor texture.
- **Look:** black cartoon outlines (`arc_mesh_style.outline`). Ludo's models are marked double-sided, so the demo sets
  them one-sided for the outline hull. A warm desk lamp with shadows, light bloom, a touch of tilt-shift.
- **Mouse look:**
  - A click captures the pointer, and the mouse turns a third-person camera (yaw, and pitch within limits). The
    gamepad's right stick turns it too.
  - WASD and the left stick move relative to the camera. Holding the mouse button fires at the crosshair. ESC lets go.
  - Tunables `mouse.sensitivity` and `mouse.invert`.
  - The jump of the pointer on the click that captures it is not a turn.
  - Keyboard-only play, `C` (the top view) and `--bench` behave as before.
- **Juice:** hit-stop, camera shake, white hit flashes, squash-and-stretch pop-ins, recoil, sparks and confetti,
  floating scores (`arc_popup_text`), a combo meter with a multiplier (x2 at 5 kills in a row, up to x5), a wave banner
  and a title.
- **Sound and font:** six Ludo sound effects played with `arc_play_var`, and a toy-box music loop. The HUD, banners
  and title use Fredoka (OFL, from google/fonts), baked with `font_bake.py --sdf`.
- **Kept:**
  - every test tunable and command, and every dump field;
  - `line_test` and `nan_test` draw SWARM's plain arena and camera, so their pixel tests read the same;
  - without the art (`--no-assets`, or no `assets/` folder) the demo runs on the code-made shapes.
- **New dump fields:** `theme`, `art`, `cam_yaw`, `cam_pitch`, `captured`, `cam_mode`, `combo`, `best_combo`,
  `multiplier`, `kills`, `hitstop`, `shake`, `kinds.{ant,beetle,flea}`, `music`.
- **New commands:** `spawn N [ant|beetle|flea]`, `spawn_at X Z [kind]`, `look YAW_DEG [PITCH_DEG]`, `capture [0|1]`.
- **Web build:** `./build.sh web samples/demo/demo.c` (and `build.bat web`) preloads the demo's assets.

## 0.10.17 — 25 Nov 2026: scroll panels (MOLTEN BREACH)

From MOLTEN BREACH's request (`incoming/moltenbreach-scroll-panels.md`): arc_ui panels taller than a phone screen can
now be limited in height and scrolled, so the game's whole-row pager (`HudPager`) can go. Existing panels draw and
behave exactly as before (the self-test compares their drawing). New check: `tests/scroll_panel_check.md` (demo
`scroll_panel_test`); self-test `tests/test_v1017.c`.

- **`arc_ui_begin_scroll(title, x, y, w, max_h, &scroll)`**: a panel no taller than `max_h` (UI units, like `w`).
  - Its content is laid out as usual, drawn clipped to the panel and moved up by `scroll` (pixels, kept in range).
  - A drag inside the panel (mouse or touch) or the wheel over it (40 pixels a step, scaled) scrolls it.
  - A drag that moves more than 6 pixels up or down presses nothing; one that goes sideways first is left to a slider.
  - A thin bar on the right shows the position when the content is taller than the panel.
  - Keyboard and gamepad focus moving to a hidden widget scrolls it into view.
  - `scroll` may be NULL (ARC keeps the position). `max_h <= 0` is a plain panel.
- **`arc_ui_content_height()`**: after `arc_ui_end`, the last panel's full height in pixels (any panel).
- **Hidden widgets**: widgets scrolled out of view take no clicks or touches and are left out of `ui` / `frame.ui`. A
  widget partly in view is listed with its visible part and `"clipped": true`, so `ui_click` presses it where it
  shows. `ui_fit` sees the visible box, so a scroll panel fits whatever its content.
- **State**: a drag under way, panel heights and ARC-kept positions are saved with the UI state (rewind, rollback);
  older states still load.
- **Agents**: new command `wheel N` (mouse wheel steps next tick, + up). `ARC_HAS_UI_SCROLL` (1017).
- The UI's drawing culls commands wholly scrolled out of view, so long lists don't fill the command buffer.

## 0.10.16 — 24 Nov 2026: agent speed tools (arc_brief, arc_report, arc_run)

From the tooling request (#12): rather than an ARC code editor, three tools that cut the time an agent spends getting
up to speed, gathering results and making MCP round trips. No engine changes. New test: `tests/agent_tools_test.py`
(on a tactics game and `samples/board`, in `build test`); `tests/mcp_test.py` checks `arc_run`.

- **`tools/arc_brief.py [GAME_DIR | FILE.c] [--out BRIEF.md] [--json] [--check]`**: a one-page brief for an agent to
  read first, instead of reading `main.c` over again each session. It gives:
  - the title, the ARC it builds against and that ARC's version;
  - the ARC the game needs: its newest engine calls, dated from `arc.h`'s version notes and `ARC_HAS_*` comments, plus
    any `ARC_HAS_*` macros it checks;
  - source files with line counts, and `file:line` of `init` / `tick` / `draw` / `dump` / `command` / `save_state` /
    `load_state`;
  - the `dump()` schema from a real headless run (`step 60`), or from the source when there is no build;
  - debug commands and how many words they take, and tunables with their defaults;
  - named meshes, assets by kind, and the engine features used;
  - with `--check`, each acceptance block's result;
  - open request issues for the game on GitHub.
  The tactics template's brief is 45 lines.
- **`tools/arc_report.py [GAME_DIR] [--seeds 1-20 --frames N --field ...] [--sessions] [--json]`**: one health report
  that exits 1 on any failure. It runs:
  - the doctor;
  - the compiler-warning check on the game's sources (MSVC when `cl` is there);
  - every acceptance file, blocks and checks passed (failed blocks named);
  - `state_check`;
  - optionally a seed sweep (min / median / max per field);
  - optionally the newest play session, with its events and each F8 bookmark's replay command.
- **MCP `arc_run`**: build if the game's or ARC's sources changed, start if not running, run a script and test
  `expect` lines in one call; it returns `PASS` / `FAIL` per line with the value each path holds. Also **`arc_brief`**
  and **`arc_report`** as MCP tools. The server's instructions now point agents at `arc_brief` and `arc_run` first.
- `new_game.py`: new games' `AGENTS.md` starts with the brief, the report and `arc_run`. `--update-scripts` adds the
  same "Start here" note to an older game's `AGENTS.md` (once).

## 0.10.15 — 21 Nov 2026: glow masks (lamps per draw), build.bat offers to launch, a Windows build fix

Self-test 2709 (9 new, `tests/test_v1015.c`). Post test: a new look, `L_GLOW`. New checks: `tests/glow_mask_check.md`
(on `samples/lights`) and `tests/launch_prompt_test.py`. Both from Grand Token Acquisition.

- **Fix: ARC 0.10.14 did not compile on Windows.** The popup code had a variable called `near`, which the Windows
  headers define as an empty macro. It is renamed. ARC and all six templates now compile with MinGW here.
- **Glow masks (GTA's car lamps):** `arc_mesh_style.emissive_mask` is a texture in the mesh's UVs. Its red, green
  and blue channels each mark a group of pixels, and group i glows `glow_rgb[i] x glow[i]` (0 = off; above about 1
  it blooms), unlit and added after lighting.
  - One mesh and one mask serve every instance, each with its own lamps on or off: indicators that blink, brake
    lights, lit windows, screens.
  - It works in all renderers and on skinned meshes. Draws with the same mesh, mask, colours and strengths batch
    together (one draw call per lamp state).
  - While any group glows, the mask takes the place of the mesh's own emissive map and colour on that draw. (The
    GL shader is at WebGL2's 16 samplers, so the mask shares the emissive map's slot.)
- **A mesh's own glow mask:** `arc_import.py --glow-mask IMG` (also `mesh2arcm.py`) stores it in the `.arcm` as a
  trailing `GLOW` block. It is lossless PNG and `--compress` leaves it alone; ARC before 0.10.15 ignores the block.
  - `arc_mesh_load` sets it; `arc_mesh_glow_mask(m)` reads it; `arc_mesh_set_glow_mask(m, tex)` sets one at run time.
  - A style's `emissive_mask` replaces it for that draw.
  - `ARC_HAS_EMISSIVE_MASK`.
- `samples/lights`: `glow_mask on|off` (G) shows two boxes sharing one mesh and mask with different lamps lit.
- **New games' `build.bat` offers to launch the game** after a plain build: `Launch <Title> now? [Y/N]`, No after
  15 s.
  - `build.bat run` builds and launches without asking, and `./build.sh run` does the same on Linux.
  - `ARC_NO_LAUNCH=1` never asks, and `headless`, `web` and `release` builds never ask. `tools/arc_mcp.py` sets it
    for its window builds.
  - Existing games get it with `new_game.py GAME --update-scripts`.

## 0.10.14 — 21 Nov 2026: template games with art and juice, a juice kit, asset manifests

Self-test 2700 (11 new, `tests/test_v1014.c`). All six templates are finished mini-games with Ludo-made models,
animation, sound, music and particles. Their acceptance checks grow to 13 blocks (169 checks).

- **Six finished template games.** `tools/new_game.py --template X` now starts from a small game, not a sketch. Each
  one keeps its rules, `dump()`, agent commands and save state, and its `NOTES.md` explains how it is built.
  - **shooter, Neon Arena:** a rigged robot (idle, run, hit, death); drones, charging beetles and a boss every 5th
    wave; combos, power-ups and crates; hit-stop and bloom.
  - **tactics, Toy Skirmish:** knights and an archer against orcs and a goblin archer, all rigged and animated (idle,
    walk, attack, hit, death). The rules resolve at once and the animation follows: units walk their path tile by tile,
    arrows fly, and hits land a moment later with a damage number. Ruins and trees.
  - **platformer, Hop Hero:** an animated hero (idle, run, jump, win), slimes you stomp, and spinning coins whose chime
    climbs as you chain them. Squash and stretch, dust, a leading camera and confetti.
  - **racer:** four karts (three AIs), boost pads, a textured road built from the track's own points, cones, tyre
    stacks and a start arch. Engine pitch follows speed (a QOA loop), skids squeal and smoke, and the field of view
    widens with speed.
  - **cards, Twenty-One:** felt, a card back, suit art and chips. Cards fly from the shoe and flip, the hole card
    turns, and chips slide to the winner; "+20" / "-10" popups.
  - **deck:** textured walls and floors; door, lift, terminal and energiser models; a droid player; security robots.
    Sparks, blasts, smoke and parts, a "!" when a robot spots you, bloom. The droid fades when a wall pulls the follow
    camera close, and is hidden in first person.
- **The juice kit (`ARC_HAS_JUICE`):** three calls every template had written by hand.
  - `arc_popup_text(pos, rgb, scale, text)` / `arc_popup_text_2d(x, y, ...)`: floating text for scores, damage and
    "+1 UP". ARC ages it, pops it in and draws it after the game's `draw()`, so it is looks only. Popups close
    together stack into a column. It shows in `frame.texts`. Also `arc_popups_clear`, `arc_popup_count`; `reset`
    clears them.
  - `arc_play_var(s, volume, pan, pitch_var)`: the same sound at a slightly different pitch each time. The pitch
    comes from ARC's own generator, so the game's `arc_rand_*` sequence and its replays are untouched.
  - `arc_yaw_to(dx, dz)`: the yaw that turns a model (front +z) toward a direction, for `arc_m4_trs_y`.
- **Asset manifests:** `tools/arc_import.py` records every import in the game's `assets/manifest.json`: the source,
  the options, the clips and `--prompt "..."` (how the asset was made). `arc_import.py --manifest --game .`
  re-imports every model from `art/` (or `--sources DIR`) the same way, byte for byte. Entries made outside
  `arc_import` (sounds, painted textures) carry a `made` note and are skipped. The templates' manifests list every
  file.
- **Model previews show the front:** `build/import/NAME.png` has a green arrow labelled FRONT +Z, so a model that
  faces the wrong way is plain to see before it is used.
- `tools/arc_import.py --texture-size N` shrinks a model's textures (512 suits props: a third of the size).
- `tools/arc_check.py` gives each check run its own `--save-dir` (a temporary folder, removed afterwards) unless the
  block sets one, so saves left by an earlier run (a high score) no longer change a check's result.
- `tools/new_game.py --template` copies the template's `assets/` folder into the new game.

## 0.10.13 — 21 Nov 2026: glowing 2D lines in bulk, a safe-area offset, music loop sections, a terrain colour map

Self-test 2689 (11 new, `tests/test_v1013.c`). New acceptance checks: `tests/offset2d_check.md`, `lines2d_check.md`,
`music_loop_check.md` (on the demo) and `terrain_colour_check.md` (on `samples/terrain`). Pictures unchanged. From
Vector Trader and Grand Token Acquisition.

- **2D drawn inside an offset (Vector Trader):** `arc_push_offset2d(x, y, w, h)` moves everything 2D after it by
  (x, y) and clips it to w x h there (0 x 0 = no clip; nested ones add up and clip inside each other);
  `arc_pop_offset2d()` ends it; `arc_offset2d()` reads the total. Wrap a HUD in the safe area with
  `arc_rectf r = arc_safe_area(); arc_push_offset2d(r.x, r.y, r.w, r.h); ... arc_pop_offset2d();`. Quads, sprites,
  text, lines, `arc_draw_lines2d` and the `frame` boxes all follow it. The offset is reset each frame, and an
  `ARC hint` names a push left open.
- **Glowing 2D lines in bulk (Vector Trader):** `arc_draw_lines2d(pts, count, width, glow, halo_alpha, rgb)` draws
  `count / 2` segments (point pairs) as one batch: a soft-edged core `width` pixels wide and a halo `glow` widths out
  on each side, one quad (6 vertices) a segment. 20,000 segments use 120,000 vertices.
  - The 2D vertex budget is `arc_app.max_2d_vertices` (0 = the old 1,048,576). Going over drops the rest of the frame
    and prints the budget and that tip once.
  - `arc_stats()` and the dump's `stats` have `vertices_2d` and `vertices_2d_max`; `frame` has `vertices_2d` and
    `lines.lines2d`.
  - `hud off` hides these lines like all other 2D.
- **Music loop sections (Vector Trader):** `arc_music_set_loop(m, start_s, end_s)` loops a looping track between two
  times after its first pass (an intro once, then the body). `arc_music_set_loop_fade(m, seconds)` (up to 2 s)
  crossfades the end into the start with equal-power gains instead of a hard jump. MP3 and QOA, sample-exact.
  `arc_music_position` stays inside the section. (0, 0) loops the whole file again.
- **`--audio-sim`:** headless runs normally have no mixer. With this flag the mixer runs at the tick rate, so
  music positions, loops and fades can be checked with no sound card (`args --audio-sim` in an arc-check block).
- **Terrain colour map (GTA):**
  - `arc_terrain_desc.color_map` and `arc_terrain_set_color_map(t, tex)` take a picture that spans the whole
    terrain. It is multiplied into the splatted ground (white changes nothing), and the four layers add texture
    detail on top. It is copied when given, so call it again after changing the picture.
  - It uses the material's detail layer, in every renderer, at 7 bits a channel. A detail layer in
    `arc_terrain_desc.material` gives way to it, with an `ARC hint`; clearing the map gives the detail back.
  - `arc_terrain_set_splat(t, rgba)` writes the whole splat map, the texture and the CPU copy, so
    `arc_terrain_layer_weight`, `arc_terrain_paint` and scatter agree with it. `arc_terrain_splat_res(t)` gives its
    side in pixels.
  - `samples/terrain` has a `terrain_colour red|map|off` command.
- **Requests as GitHub issues:** `tools/arc_request.py request.md --game NAME` files a request as an issue on the ARC
  repository, labelled `request`, so requests can be picked up while the PC that wrote them is off. `--list` shows
  the open ones. It uses `gh api` (REST); if GitHub can't be reached it saves the request in `incoming/`, as before.
  `docs/REQUESTS.md` and AGENTS.md describe it. `build test` runs a `--dry-run`.
- Feature macros: `ARC_HAS_OFFSET2D`, `ARC_HAS_LINES2D`, `ARC_HAS_MUSIC_LOOP`, `ARC_HAS_TERRAIN_COLOR_MAP` (1013).

## 0.10.12 — 21 Nov 2026: NaN-safe post effects, crisp built-in text, every arc-check block, panel positions

Self-test 2678 (15 new). New acceptance checks on the demo: `tests/nan_check.md`, `text_check.md`,
`save_path_check.md`, `command_clash_check.md`; `tools/tests/two_blocks.md` (one block fails on purpose). Pictures:
looks 28, 33, 41, 42 and 59 move built-in text by a fraction of a pixel (new baselines). From Moonbase Raid, Grand
Token Acquisition, MechBattleArc and Vector Trader.

- **Glowing black spots on GPUs (Moonbase):**
  - The hex-tiling contrast gain took `pow` of a tiny negative number when a cell weight rounded to 1.0000001. GPUs
    gave NaN, and bloom spread each one. It is clamped now, in the GL and D3D11 shaders and in the software renderer.
  - The post chain turns NaN and Inf pixels black, and negatives to 0, wherever it reads the scene: bloom's first pass
    and the composite (and so FXAA and the rest). The test is on the exponent bits, so no shader compiler can fold it
    away. One bad pixel then stays one dark pixel.
  - New command `post nan on|off`: non-finite scene pixels show magenta before the post chain.
  - `frame.post` has `nan_mode` and `nan_pixels`. The software renderer always counts them; on a GPU they are counted
    from the magenta pixels while `post nan on` is set.
  - The demo has a `nan_test` tunable: a cube whose glow is NaN, under bloom.
- **Crisp built-in text (Vector Trader):**
  - The 8x8 font is drawn on whole pixels after alignment, so half-pixel centring no longer drops rows.
  - `arc_text_fit_scale` returns whole sizes from 1 up.
  - Sizes of 1 and more are otherwise kept: nearest sampling keeps every row there, and layouts made with them still
    fit. `arc_text_snap_builtin(ARC_TEXT_SNAP_SIZES)` also rounds them, and `ARC_TEXT_SNAP_OFF` gives the old
    behaviour. `ARC_TEXT_EXACT` turns snapping off for one `arc_draw_text_ex`.
  - `frame.texts[*].scale` for the built-in font.
- **`arc_check` runs every block (GTA):**
  - Each block runs in a fresh process, named by its `##` heading, with a pass/fail line per block and `B/C blocks
    passed` on the last line; any failure exits 1.
  - Later blocks reuse the last `game` / `exe` / `args`. `continue` keeps the previous block's game running.
  - New options `--block N`, `--name TEXT`, `--fail-fast`.
  - A file with one block prints exactly what it did before.
- **Check paths (Vector Trader):** relative paths given in `args` to `--save-dir`, `--record`, `--replay`, `--script`,
  `--editor`, `--dump-at-exit` are read from the check file's folder, not from `build/`. A failed save now prints
  `ARC error save ... failed: cannot write ...` (once until a save works).
- **Command clash (Vector Trader):** when ARC's `set` finds no tunable of that name, the game's own `command()` gets the
  line. If the game handles it, an `ARC hint` says the name clashes, instead of an error.
- **Panel positions (MechBattleArc):**
  - `arc_ui_panel_offset(title, &dx, &dy)` reads where the player dragged a panel, and `arc_ui_set_panel_offset` gives
    it back, so a game can save it with its settings. It works before the panel is first shown; 0, 0 resets it.
  - `arc_ui_panel_rect(title, ...)` is last tick's rectangle after the drag.
  - Dragged panels always keep their title bar inside the window and the safe area, while dragging and after a resize.
  - `frame.ui_panels` lists title, rect and offset.

## 0.10.11 — 20 Nov 2026: thick, glowing lines and multi-touch

Self-test 2663 (16 new). New acceptance checks `tests/line_check.md` and `tests/touch_check.md` (on the demo), plus
`tests/line_width_test.py` (pixels) and `tests/touch_replay_test.py`. `tests/web_touch_test.js` sends real browser touch
events to a web build (by hand: emsdk and Playwright). From Vector Trader's requests.

- **Thick lines:** `arc_set_line_width(px)` makes the 3D lines drawn after it that many pixels wide at any distance,
  with a soft one-pixel edge. Each segment becomes a strip of triangles facing the camera, built when it is drawn,
  and every renderer draws them blended, depth tested and without depth writes, right after the thin lines.
  `arc_set_line_cap`: square (the 3D default), round or butt. Width 1, the default, is today's one-pixel line, so
  existing games draw exactly as before. About 9,000 thick segments a frame (the thick-line buffer is allocated at
  the first one); thin lines still allow 131,072.
- **Glowing lines:** `arc_set_line_glow(x)` multiplies the colour of the lines after it (thin ones too), so above 1
  they pass `arc_post`'s bloom threshold like emissive meshes. Line vertices carry this gain (20 bytes, was 16). The
  software renderer clamps a colour to 1 before blending into an 8-bit target, as GPUs do.
- **`arc_draw_lines(pts, count, model, rgb)`:** a whole wireframe (point pairs) in one call.
- **`arc_draw_line2d`** 1.5 pixels and wider now has soft edges, and butt ends unless the game calls
  `arc_set_line_cap`. Thinner 2D lines are unchanged.
- **`frame.lines`** (`count`, `thick`, `width_max`, `glow_max`, `triangles`) and `frame.dropped`. `stats.lines`
  counts thick segments too.
- **Multi-touch:** up to 10 fingers (`ARC_TOUCH_MAX`), read in `tick()` with `arc_touch_count` / `arc_touch_get` /
  `arc_touch_find`.
  - Each finger has a small id that stays while it is down, its pixels, its start point, and pressed / released. A
    lifted finger is listed for one more tick.
  - A tap shorter than a tick is still seen, and a lifted finger's id is never reused in the same tick.
  - The first finger still drives the mouse and `MOUSE_LEFT`, so existing games don't change.
  - Platforms: the web (every finger; the page still never scrolls or zooms) and Windows touch screens
    (`WM_POINTER`, found at run time, so Windows 7 still runs). A real mouse move turns touch mode off on Windows,
    as it already did on the web.
  - Fingers are part of each tick's input, so recordings, replays, rewind and the timeline include them (replay
    format 4; versions 2 and 3 are still read).
  - `arc_ui` widgets answer any finger that goes down and comes up on them, so a steering thumb doesn't block a tap.
  - Headless: `touch down ID X Y`, `touch move ID X Y`, `touch up ID` (the first scripted finger is the mouse too).
    `dump.input` has `touches`, `touch_mode` and `touch[*]`.
- **The demo** has a `line_test` tunable (a glowing wireframe, a thick line, a soft 2D line).
- `incoming/README.md` keeps the request folder in git.

## 0.10.10 — 18 Nov 2026: an MCP server for live work

Self-test 2647 (unchanged). Pictures unchanged. New build-test stage "MCP server" (`tests/mcp_test.py`). From Allan:
agents working on a running game, which the person can watch.

- **`tools/arc_mcp.py`**: an MCP server (Claude Code, Claude Desktop, any MCP client), with no packages needed. Its
  tools build the game, start it (headless or in its window), step, run any agent command, and read `frame` / `dump`
  (with `select` to return one part). They also take screenshots returned as images (shrunk, optional orbit camera),
  pick what is at a pixel, set tunables, run editor commands, test acceptance-check lines live, and reload assets.
  `arc_save_check` writes the session (every command that changed the game, and each check) as an arc-check file, so
  what was found live replays headless. `docs/MCP.md`.
- **Window builds driven through a pipe** (`--stdin` from a program, not a console): the window keeps drawing and
  answering the OS while ARC waits for a command, and ticks run only when asked. A new agent command, `live run` /
  `live hold` / `live speed X`, lets the game run on its own clock so the person can play while an agent watches.
  Headless runs and console `--stdin` are unchanged.
- **`.mcp.json`**: `tools/new_game.py` writes one for new games, and `--add-mcp` adds one to an existing game. ARC's
  repo has its own.
- `arc_check.py`: an `exe` path not found from the current folder is read from the check file's folder.

## 0.10.9 — 17 Nov 2026: `frame` in immediate mode, web loading fixes

Self-test 2647 (6 new). Pictures unchanged. New acceptance check `tests/target_immediate_check.md` (12 checks, the
target checks on `tests/target_game.c --immediate`); `tests/level_check.md` has 43 (renames with spaces). From
Moonbase's and Droids' requests.

- **`frame` in immediate mode**: games with `arc_app.immediate` draw from `tick()`, and `frame` listed only what was
  drawn while it ran, so markers, labels, texts and the meshes drawn into render targets were missing or empty. ARC
  now records every immediate tick (and the ticks re-run by `seek` / `rewind`) and `frame` lists that tick's drawing,
  render targets included (`target`, `target_box`, `frame.targets`), the same as for games with `draw()`.
- **Packs fetched together load together**: files from two packs queued in one group, interleaved, with the second
  pack's download finishing first, failed with "cannot load" for some of the first pack's files. Look-ahead now
  stops at a pack that is not mounted yet.
- **No "loaded during tick()" hint for queued loads**: a load inside `arc_load_begin` / `arc_load_end` from `tick()`
  is the intended way to stream, so it no longer asks for the hint.
- **`arc_pack_fetch` in a web build without packs** (no `arc_packs.txt`) is done at once and fetches nothing, as on
  desktop, instead of requesting a pack file that does not exist.
- **`arc_file_listed(path)`**: true when a file is in the assets or in a web pack that can still be fetched (listed in
  `arc_packs.txt` and not failed). Use it for "is this level there?" before queuing it; `arc_file_exists` is only true
  once the pack is in.
- **Doctor false alarms**: names the game writes (`arc_screenshot`, `arc_image_save_png`, `arc_file_write`,
  `arc_level_save`, `arc_save_write`, `fopen(.., "w")`), names in comments, `arc_packs.txt` and the `.arcpak` files of
  `WEB_PACKS` are no longer reported as missing assets; a line with `doctor: not an asset` is skipped. The version
  note prints `0.10.6`, not `1006`.
- **Quoted words in commands**: `editor rename "Cargo Hold"` (or `editor rename Cargo Hold`, every word after it) and
  `editor setting title "Deck One"`; any agent command can keep spaces inside "double quotes". `type` is unchanged.

## 0.10.8 — 16 Nov 2026: level files and a full deck editor

Self-test 2641 (27 new). Picture look 28 (the editor in a small window) changed: the panel has a Select tool and a Test button. New acceptance checks `tests/level_check.md` (40 checks) and `tests/level_launch_check.md`
(7), on `tests/level_game.c`. The `deck` template uses all of it. From Droids' deck-editor request.

- **Level files (`arc_level`)**: a text format ARC reads and writes: layers of `|row|` lines (any printable ASCII),
  `key = value` settings for the file and for each layer, and `object X Y kind name=value ...` lines per layer.
  Values are bare words or "quoted". Saving writes the same text for the same level, so files diff well and a saved
  file loads and saves back byte for byte. A malformed file is refused with `file:line: why` (`arc_level_error`).
  `arc_level_load` / `parse` / `save` / `reload` / `text`, layers (`add_layer`, `cells`, names), settings
  (`get` / `set`, numbers too), objects (`object_add` / `remove` / `move` / `get` / `set` / `at`). With `--hot` a loaded
  level reloads when its file changes (`arc_level_reloaded`); cells keep their memory when sizes are unchanged.
- **The editor edits a level** (`arc_editor_desc.level`, `level_path`): its layers are the grids, save writes the file
  (then calls `save` if given), load reads it back.
  - **Objects tab** (`objects`: kinds with fields INT, NUM, TEXT, FACING, CHOICE, BOOL, optional or with defaults):
    place, pick, drag, delete; the selected object's fields in the panel (- / + steps, typing numbers and text,
    facing and choices cycle). `object_mesh` draws them in the editor's 3D view; `objects_changed` is called after
    each change.
  - **Layer tab**: the layer's name and `layer_fields` (its settings).
  - **Copy, cut, paste**: the select tool (M), CTRL+C / X / V with the objects inside; R turns the clipboard 90
    degrees and F mirrors it (FACING fields turn with it); DEL clears. Each is one undo step.
  - **Undo** covers cells, objects, settings and names together (one step per edit).
  - **Onion skin** (O): the layer above, then below, faintly in the 2D view, objects too.
  - **Test-play** (T, `playtest(layer, x, y)`): a copy of the level goes to `level_path.playtest`, the editor closes
    and the game starts there; F2 comes back where it was with the edits.
  - **`--editor [FILE]`** starts in the editor (F2 then works on any build); FILE is read into the level.
  - Agent commands: `editor object add|set|move|del|select|list`, `setting`, `rename`, `select`, `copy`, `cut`,
    `paste`, `clear`, `rotate`, `flip`, `onion`, `playtest`, `tab`, `kind`. `dump`'s `editor` lists the objects
    (with their fields), the settings, the selection, the clipboard and the onion layer.
  - Games without `level` behave as before (copy and paste of cells, the onion skin and test-play work for them too).
- **`deck` template**: its decks are an `arc_level` (`decks.arclvl` replaces `decks.txt`); enemy objects with `hp` and
  `facing`; each deck's `light`; a validate hook (a start or lift on every deck, no enemy in a wall); test-play
  restarts on the chosen deck and cell; `dump.player` gives the player's cell.
- `arc_check`: `editor.objects[0].fields.class` and the rest read as any dump path.
- Macro `ARC_HAS_LEVELS`.

## 0.10.7 — 16 Nov 2026: `frame` sees inside render targets

Self-test 2614 (unchanged). New acceptance check `tests/target_check.md` (`tests/target_game.c`: a 3D view drawn into
a half-size target, scaled onto the screen, HUD on top). From Moonbase Raid, whose 3D view is drawn that way.

- **`frame` lists what is drawn into render targets** (passes into targets other than mirrors and viewports): meshes,
  markers, texts, sprites and rects drawn there carry `"target"` (the name from `arc_texture_set_name`, else `"#id"`);
  meshes also `"pass"`. `box` is on the screen, placed through the last `arc_draw_texture_ex` / `arc_draw_sprite` of
  that target (and of a target it was drawn into, up to 4 deep); `target_box` is in the target's pixels. A target never
  drawn on the screen keeps target pixels in `box` and says `"on_screen_box": false`. A target drawn rotated isn't
  placed.
- `frame.targets[*]`: each pass into a target: `pass`, `target`, `size`, camera `eye`, `meshes`, and `on_screen_at`
  (where the whole target landed).
- `arc_draw_texture_ex` is listed in `frame.sprites` (it wasn't).
- Markers drawn into a target say so (`frame.markers[*].target`). Marker shapes are names (`"ring"`), as before.
- Macro `ARC_HAS_FRAME_TARGETS`.

## 0.10.6 — 16 Nov 2026: MechBattleArc fixes (landscape card, shoulder stripes, blank sheets)

Self-test 2614 (11 new). New acceptance check `tests/card_check.md`; `tests/review_check.md` has 4 more checks; picture
look 92 (`L_PATARM`, stripes on the arms only) in `test_post`. Three requests from MechBattleArc.

- **`arc_ui_require_landscape` with any UI font:** the card's text is in the UI's font and scale (`arc_ui_set_font`,
  `arc_ui_scale_auto`), the first line (up to `\n`) as a title at twice the size, the rest below, wrapped to 88 % of the
  window; a title word wider than that shrinks the title. Before, it took the built-in font's size and handed it to the
  UI font as its scale, so a 48 px SDF font came out several times too big. The card's lines are now the UI's widgets
  (`ui`, `ui_fit` see them; what the card covers is left out).
- **Paint-pattern parts:** joints named `*Shoulder*` or `*Clavicle*` count as `ARC_PART_ARMS` (mixamo shoulder pads
  keep the arms' stripes). New `arc_paint_parts_rule(pattern, part)`: the game's own joint names (`*` and `?`, case
  ignored), tried before the built-in ones, the latest first; part 0 removes a rule, NULL removes them all.
- **`sheet` was blank with the HUD hidden:** `hud off` dropped every 2D draw on the screen, which is how a sheet copies
  each cell and writes its label. Sheet frames now draw with ARC's own state whatever the game set: HUD shown, no post,
  fog, rim, environment, shadows, reflection, camera override, world mask or cutaway (the game's settings come back
  after). Empty cells are the background colour, not transparent.
- `sheet` reports each cell's `filled` and `label_ink` (shares of the picture and of the label row unlike the
  background); `arc_check` reads them as `sheet.cells[*].filled`. `arc_check` also has `min(path)` / `max(path)`.
- Macro `ARC_HAS_PART_RULES`.

## 0.10.5 — 16 Nov 2026: several gamepads

Self-test 2603 (13 new). Raised by the split-screen work (0.10.2): ARC read only the first pad.

- **Up to `ARC_MAX_PADS` (4) gamepads:** `arc_pad_count`, `arc_pad_is_connected(pad)`, `arc_pad_down` / `pressed` /
  `released(pad, button)`, `arc_pad_axis_of(pad, axis)`, `arc_pad_first`, `arc_pad_any_pressed(&button)` ("press A to
  join"). Pad N keeps its number while connected: Windows the XInput player number (all four slots are read; empty
  ones are asked every 2 s, staggered), Linux the order evdev pads were found (a new pad takes the lowest free
  number), web the browser's gamepad index.
- The `ARC_PAD_*` keys and `arc_pad_axis` / `arc_pad_connected` still read the first connected pad: one-player games
  are unchanged, and the scripts' `tap PAD_A` / `axis LX` still drive it.
- Replays record every pad: format 3. Format 2 replays still play (their pad becomes pad 0).
- Agents: `pad N connect|disconnect|press B|release B|axis A V`; `dump` lists the connected `pads`.
- `docs/split-screen.md`: "pad N drives player N" and "press A to join".
- Macro `ARC_HAS_PADS`.

## 0.10.4 — 16 Nov 2026: real-time (rollback) matches online

Self-test 2590. `tests/relay_rollback_test.py` (new): 3000-tick netarena matches at 80 ms lag, 5 % loss and 20 ms
jitter through a local relay, direct and relayed, three players, a dropped player and a dropped hub; also with a
Windows player (MinGW, Wine) against a Linux hub. LAN rollback (`rollback_test.py`), turn-based relay
(`relay_test.py`) and LAN turns (`net_test.py`) unchanged. From Droids' real-time online request.

- **Rollback matches through the relay** (desktop builds): the same calls as on a LAN (`d.mode = ARC_NET_ROLLBACK`,
  `arc_net_host_online` / `arc_net_join_online`, `arc_net_start`). After the start each player registers a UDP socket
  with the relay's UDP port (the same number as its TCP port); datagrams go through the relay, or straight to the other
  player once hole punching works (tried at once, re-checked; a quiet direct path falls back to the relay after
  1.5 s). The lowest player in the match is the hub (the LAN host's part: passes inputs on, decides drops). A player
  who leaves or goes quiet is dropped and reported; if the hub goes, the others are alone.
- **arc_relay**: a UDP socket on its port: `REG` (slot + HELLO token), `FWD` between a room's registered players,
  `PEERS` (public addresses for hole punching); rate-limited per room. Open UDP 47880 next to TCP.
- `--net-lag MS`, `--net-loss PCT`, `--net-jitter MS` (the `--net-sim` settings one at a time); `--net-relay-only` /
  `net direct off` (no hole punching). `dump`: `net.rollback.online` (hub; each peer's `path`, datagrams direct and
  relayed). The rollback count and deepest rollback were already there (`rollbacks`, `max_depth`).
- **Browsers:** not included. A browser's only UDP-like path is WebRTC, and talking to it would need DTLS + SCTP + ICE in
  the relay and every desktop build: too big without libraries. A web build that hosts a rollback room is refused with
  that reason; one that joins plays alone.
- Fixed three clang conversion warnings in `samples/shaders/shaders.c`.
- Macro `ARC_HAS_ONLINE_ROLLBACK`.

## 0.10.3 — 16 Nov 2026: deck crawler template, 3D tile editor view, editor layers

Self-test 2590 (15 new). Templates 6 (the new `deck` template: 31 acceptance checks). Post test unchanged (397 on the
software renderer); the 0.10.2 pictures are unchanged on all three renderers. From Droids' deck-template request.

- **Template `deck`** (`tools/new_game.py DIR --template deck`): a ship of three indoor decks from text maps (`#` wall,
  `D` door, `K` locked door, `T` terminal, `L` lift, `E` energiser, `S` start, `e` enemy).
  - Rooms found from the map become cells, doors portals, so rooms behind shut doors aren't drawn in first person.
  - Doors slide open when someone comes near; locked doors open after their deck's terminal is used.
  - Lifts change deck (lifts that line up join); an energiser repairs you.
  - Fog of war (`arc_set_world_mask`), a minimap and line of sight.
  - Enemies: `arc_brain` + `arc_senses` + nav agents on each deck's navmesh; shots both ways; camera rigs (top-down,
    follow, first person).
  - Each room has a light; a cleared deck's lights go dim (reported in `dump`).
  - Saves (`save` / `load`, `save_state`), `dump`, agent commands (`deck N`, `tp X Y`, `open_doors`, `clear`, `use`,
    `lift up|down`, ...), and the deck editor with every deck as a layer, saving `decks.txt` in the format the game loads.
- **Tile editor, its own 3D view:** `arc_editor_desc.tile_mesh` gives the meshes the game builds for a cell (walls raised,
  doors, lifts, kit). V (or `editor view 3d`) shows the grid that way from the editor's own orbit camera; orbit, pan and
  zoom with the mouse, paint as in 2D. It's drawn in a viewport with the game's lights, sky and post, without fog of war
  or cutaway. V now cycles 2D → own 3D → the game's view.
- **Tile editor layers:** `arc_editor_desc.layers` / `layer_count` / `layer_names`: several grids of one size (decks,
  floors); PAGE UP / DOWN, `arc_editor_set_layer`, `editor layer N`. Undo goes back across layers.
- `arc_editor_view` / `arc_editor_set_view`, `arc_editor_layer`, `editor orbit`; the dump's `editor` object adds `view`,
  `layer`, `layers`, `hover`. The 2D editor is unchanged.
- `build.sh` / `build.bat` compile with `-Iinclude` / `/Iinclude`, so a game file that includes `"arc.h"` builds from the
  ARC folder (`arc_check.py` with `game templates/deck/main.c`).
- Macro `ARC_HAS_EDITOR_3D`.

## 0.10.2 — 16 Nov 2026: split-screen viewports

Self-test 2575 (10 new). A new arc_check stage (`tests/split_game.c`, 13 checks). Post test 397 on the software
renderer (3 new looks). The 0.10.1 pictures are unchanged on the software renderer, GL and D3D11. From Droids'
split-screen request.

- **Viewports:** `arc_viewport_begin(x, y, w, h)` / `arc_viewport_end()` in draw. Everything between them is drawn
  into that rectangle of the window as if it were the whole screen.
  - Inside one, `arc_width` / `arc_height` / `arc_aspect`, `arc_target_width`, 2D, text, `arc_safe_area`,
    `arc_mouse`, `arc_world_to_screen` and `arc_screen_ray` all work in the viewport's own pixels, so HUD code
    doesn't change.
  - Each viewport gets its own sky, light tiles, sun shadow cascades fitted to its camera, light shadows, planar
    reflection and the `arc_set_post` effects (SSAO, bloom, FXAA, ...). Render targets work inside one.
  - Lights added inside a viewport light only that viewport; lights added outside light every viewport.
  - A window-sized viewport gives the same picture as the screen on all three renderers (exact on the software
    renderer and D3D11, within 1 on GL). Two 320 x 180 viewports side by side each match a full-window 320 x 180
    render of their camera exactly (software renderer, `tests/split_check.md`).
  - Up to `ARC_MAX_VIEWPORTS` (4) a frame; a viewport left open is closed after draw. A frame with no viewports
    draws exactly as before.
  - On D3D11 with MSAA, viewports are drawn without it.
- **UI made in tick:** `arc_viewport_begin` / `end` outside a frame set an area: sizes, the mouse, the safe area and
  the anchored panels work in it, and the widgets are drawn in it.
- **Picking:** in tick, `arc_screen_ray(px)` goes through last frame's viewport under `px`. `arc_viewport_at`,
  `arc_viewport_rect`, `arc_viewport_count`, `arc_viewport_current`, `arc_viewport_world_to_screen`.
- **Camera rigs:** `arc_cam_rig.aspect` (0 = the window's), for TOPDOWN bounds in a viewport.
- **Agents:** `frame` lists `viewports` (rect, camera eye and direction, meshes with counts); text drawn in a viewport
  is listed at its window position. The `camera` / `cam rig` overrides change only the window's camera.
- `docs/split-screen.md`, including gamepads: ARC reads one pad today, so two players means keyboard + pad.
- Macro `ARC_HAS_VIEWPORTS`.

## 0.10.1 — 16 Nov 2026: crisp fonts, UI that fits any window, review sheets, paint patterns

Self-test 2565 (14 new). A new arc_check stage (`tests/review_game.c`, 8 checks, including `ui_fit` at 7 sizes and
a 3-cell sheet). Post test 382 on the software renderer (4 new looks). The 0.10.0 pictures are unchanged on the
software renderer, GL and D3D11; the GL shaders were also checked as GLSL ES 3.00. From MechBattleArc's crisp-fonts,
UI-layout, review-sheet and paint-pattern requests.

- **Distance-field fonts:**
  - `tools/font_bake.py --sdf [--spread 6]` bakes one `.arcf` (48 px is plenty) that draws sharp at any size:
    `arc_font_draw_ex(f, x, y, px / 48.0f, ...)`. Measured against bitmaps baked at 11, 14, 17.5, 23 and 64 px, the
    stem widths stay within a pixel.
  - The edge is smoothed over one screen pixel the same way on every renderer.
  - The file format grows a flag (bit 2) and a spread byte; old `.arcf` files load as before.
  - `arc_font_is_sdf`. `arc_text_snap` (on by default) starts lines of baked-font text on whole pixels.
- **UI layout:**
  - `arc_ui_scale_auto(1280, 720, min, max)` follows the window, on whole-pixel text sizes with a bitmap UI font.
  - `arc_safe_area()`: on the web this comes from `env(safe-area-inset-*)` through the shell's CSS variables. On
    desktop `--safe-area L,T,R,B` pretends there are insets, for tests.
  - `arc_ui_begin_anchored(title, ARC_ANCHOR_*, ox, oy, w, h)`: corners, edges or the centre of the safe area, in UI
    units. The panel shrinks to fit when the window is too small.
  - `arc_orientation()`, and `arc_ui_require_landscape(text)` (a "turn sideways" card).
  - The `ui_fit 1280x720 800x600 ...` command runs one tick at each size and reports widgets outside the window or
    safe area, or overlapping each other, as ARC errors.
- **Review sheets:**
  - `arc_camera_fit(base, box_min, box_max, aspect, margin)` keeps the view direction and field of view
    (perspective or orthographic) and centres the box. It was checked on a 1 x 2 x 1 box at aspects 0.5, 1 and 2.35.
  - `arc_sheet_add(mesh, model, style, label)` in draw, then the `sheet FILE.png [COLS] [cell WxH]
    [bg RRGGBB|transparent] [margin M]` command: one auto-framed cell per mesh, the same light, its label
    underneath. "transparent" gives real alpha.
  - `shot FILE.png crop [MARGIN]` trims to what differs from the corner colour.
- **Paint patterns:** `arc_mesh_style.pattern`, computed by the shader from the bind-pose position, so it stays put
  on skinned meshes. Every renderer, static and skinned meshes; draws with the same pattern still batch.
  - Kinds: `ARC_PAINT_STRIPES` (`cross` for crosshatch), `_CHECKER` and `_DOTS` (on each face's own plane: square
    cells, round dots), `_SPLIT`, `_NOISE` (camo, `seed`). Plus `scale`, `width`, `angle`, `offset` and its own
    `rgb`.
  - `part_mask` (`ARC_PART_HEAD / TORSO / ARMS / LEGS`) comes from each vertex's strongest joint, with mixamo-style
    names.
  - `armour_mask` limits the pattern to `paint_mask`'s red, so trim and joints keep `tint2` / `tint3`.
  - Without a pattern nothing changes.
- `tests/fonts`: DejaVu Sans baked for the text test (README with the licence).
- Feature macros `ARC_HAS_SDF_FONTS`, `_UI_LAYOUT`, `_SHEETS`, `_PAINT_PATTERNS`.
- Inside:
  - 2D batches carry a distance-field factor (GL `u_sdf`, D3D cbuffer b3).
  - Mesh batches carry a paint-pattern index into a per-frame table. D3D cbuffer M grows to 768 bytes and is also
    bound to the vertex shader.
  - The software renderer interpolates 4 more attributes, only for patterned batches.

## 0.10.0 — 15 Nov 2026: AI brains, senses, shots, camera rigs

Self-test 2551 (73 new). A new arc_check stage (`samples/action`, 13 checks). Post test 367 on the software
renderer (one new look: tracers, a plasma bolt lighting a wall, a scorch). The 0.9.9 pictures are unchanged on the
software renderer, GL and D3D11. From Droids' AI-behaviour, weapons and camera-rig requests. All four modules are
plain data with fixed sizes, use ARC's deterministic maths, and give the same results after save / load.

- **AI brains** (`arc_brain`): state machines as data.
  - `arc_brain_def` holds states (enter / tick / exit callbacks) and transitions, tried in order: blackboard floats
    and ints, time in state, one-tick triggers, the game's own tests, `ARC_BRAIN_ANY`. One switch per update.
  - `arc_brain` per agent: state, time, blackboard, the last 8 changes.
  - `arc_brain_go` (from a callback) and `arc_brain_force`.
  - `arc_brain_watch` puts a group in `dump` (`"brains": {"droid": [{"state": "SEARCH", ...}]}`) and adds the
    `brain GROUP N STATE` command.
- **Senses** (`arc_senses`): up to 64 agents, 4 targets each.
  - Sight: a view cone and range, the game's line-of-sight test (ready-made ones for collision worlds and physics),
    up to 9 sample points per target, and a build-up, so a glimpse takes a moment and partial cover takes longer.
  - Hearing: `arc_senses_noise`, muffled through walls.
  - Memory: last known position, time and how (seen / heard / told), forgotten after `forget` seconds.
  - `arc_senses_alert` shares memories within a radius; `arc_senses_search_points` gives navmesh points around a
    last known position.
  - `arc_senses_draw`; `arc_senses_watch` puts them in `dump`.
- **Shots**: `arc_shot_fire` (hitscan, or projectiles moved by `arc_shots_update`), rays or spheres.
  - They hit the grid (the game's `blocks(x, z)`), collision worlds, physics bodies and the game's own moving
    targets (`arc_shots_targets`: spheres and capsules). The nearest hit wins. A shot never hits its owner, and layer
    masks apply.
  - `arc_shots_hits` returns this tick's hits; `arc_shot_cast` does the same walk for aim checks.
  - Looks: tracers (thin, or wide additive streaks), projectile streaks, impact decals, sparks (`arc_shots_fx`),
    3D sounds, and short lights at the muzzle, the impact and on projectiles.
  - The state goes into ARC's snapshots (rewinds, `state_save`) by itself; `arc_shots_save` / `_load` for a game's
    own saves. `frame` lists tracers and projectiles.
- **Camera rigs** (`arc_cam_rig`): TOPDOWN (orthographic, kept inside bounds), FOLLOW, ORBIT, FIRST, FIXED.
  - Follow lag on the target and the facing.
  - FOLLOW and ORBIT keep a sphere out of walls (grid, collision world, physics: `arc_cam_rig_set_world`). The eye
    pulls in at once and eases back out over the lag, so it never pops outward.
  - `arc_cam_rig_blend` switches views smoothly; orthographic <-> perspective goes through a matched view (the aim
    point stays put while the field of view opens).
  - `arc_cam_rig_shake` (trauma that decays by itself).
  - Commands: `cam`, `cam rig KIND|off` (also over a game that has no rig), `cam shake N`. `frame.camera.rig`.
- `samples/action`: four security droids that patrol, notice, attack, call each other, search and flee, against a
  player with hitscan bolts, plasma that lights the walls, and four blended views. `tests/action_check.md` checks it.
- Feature macros `ARC_HAS_BRAIN`, `_SENSES`, `_SHOTS`, `_CAM_RIG`. The header still compiles as C++.

## 0.9.9 — 15 Nov 2026: grid tactics, ground markers, tile maps, fog of war, cutaway, burnt models

Self-test 2478 (28 new), a new arc_check stage (samples/board), post test 364 on the software renderer (18 new checks;
the 0.9.8 pictures are unchanged on the software renderer, GL and D3D11 with standard and reversed depth). From
MechBattleArc's grid-tactics and ground-marker requests and Moonbase Raid's items 1-4.

- **arc_board** (grid tactics, plain data, integer maths, deterministic):
  - per tile a move cost (0 = blocked), cover (none / half / full, full blocks sight) and an occupant;
  - `arc_board_los`: symmetric (A sees B exactly when B sees A; checked on 10 000 random pairs), with the best cover
    on the way; corners count as half cover; occupants block unless `ARC_BOARD_IGNORE_UNITS`;
  - `arc_board_reach` (Dijkstra within a budget, 4- or 8-connected, no corner cutting; checked against brute force on
    200 boards) and `arc_board_path` (A*, ties in a fixed order);
  - `arc_board_arc` (front / rear 120 degrees, sides 60, for 8 facings), `arc_board_dir`, `arc_board_range`
    (Chebyshev or Manhattan), `arc_board_edge_tiles` (drop zones, exits).
- **Ground markers**: `arc_draw_marker` draws tiles (rounded corners), rings, discs, arrows, chevrons and diamonds.
  - Fill, edge (screen pixels), dashes and pulse; `on_top` shows a faint copy through units.
  - They follow uneven ground (a height callback, a terrain, or a flat y) and never z-fight, even at grazing angles.
  - Labels ("DROP ZONE") are drawn as text above them.
  - Batched into pooled meshes; listed in `frame.markers` (labels in `frame.texts`).
  - `samples/board`: a board with reach, path and line-of-sight markers; `tests/board_check.md` checks it.
- **Tile maps** (Moonbase Raid 1): texture arrays of up to 64 layers; a mesh can take its colour from an array at a
  layer chosen per draw (`arc_mesh_set_texture_array`, `arc_draw_mesh_layer`, `arc_mesh_style.tex_layer`), still one
  batch. `arc_draw_tiles` draws a grid with a mesh, layer, quarter turn and tint per cell: one draw call per mesh kind.
- **Fog of war** (Moonbase Raid 2): `arc_set_world_mask` darkens every 3D draw (meshes, skinned, decals, stamps,
  particles, markers) by a texel-per-tile mask, with soft edges, the same on every renderer.
  `arc_mesh_style.no_world_mask` leaves a mesh out.
- **Cutaway** (Moonbase Raid 3): meshes marked with `arc_mesh_set_cutaway` lose what is above a keep height near the
  eye-to-focus line (`arc_set_cutaway`: clip or dither fade), with a dark cap on the cut. Shadows come from whole walls.
- **Darken and char** (Moonbase Raid 4): `arc_mesh_style.darken` (towards black, texture detail kept) and `charred`
  (darkened, soot mottling, bright scratches) work on textured models.
  - Load-time image operations: `arc_image_multiply`, `arc_image_overlay` (normal, multiply, screen, overlay, add;
    stretched or tiled), `arc_image_noise` (wraps, so tiling textures still tile) and `arc_image_char`.
- Feature macros `ARC_HAS_BOARD`, `_MARKERS`, `_TILES`, `_WORLD_MASK`, `_CUTAWAY`, `_DARKEN`.
- Inside: per-instance flags are packed into existing instance floats, since WebGL2 allows only 16 vertex
  attributes. The world mask uses texture unit 16, which keeps fragment samplers at WebGL2's minimum of 16.

## 0.9.8 — 15 Nov 2026: ragdolls, a rollback match in the determinism check, faster headless runs

Self-test 2450 (34 new), a new build-test stage (headless tools), post test 346 (pictures unchanged). The determinism
check has 7 scenarios; the first five hashes are unchanged, so 0.9.7 physics, navigation and terrain give the same
results. Ragdoll and rollback hashes match on gcc and clang (-O0 to -O3, with and without FMA, ISO and GNU modes) and on
MinGW (Windows C runtime); MSVC and Emscripten are checked by `build.bat test` and `tests/det_check.c`.

- **Ball joint limits**: `arc_joint_desc.cone` / `cone_axis` keep a ball joint's child axis (`axis`) inside a cone fixed
  on the base, and `limit` / `lower` / `upper` now bound a ball joint's twist about its axis.
  - Both are speculative rows, like the hinge's limits.
  - `arc_physics_joint_angle` reads a ball joint's twist; `arc_physics_joint_swing` reads its angle from the cone's centre.
  - `arc_physics_draw` shows the cone.
  - The physics state format is now "PHS3" (0.9.7 states don't load).
- **Ragdolls**: 11 capsules on joints with human limits:
  - ball joints with cones and twist at the waist, neck, shoulders and hips;
  - one-way hinges at the elbows and knees.
  `arc_ragdoll_from_pose` builds one from a skinned character's pose:
  - its joints are found by name (mixamo, Unreal and Blender styles), in any scale (a centimetre mixamo rig works);
  - an optional earlier pose gives each part its motion.
  `arc_ragdoll_pose` gives the skeleton back as the ragdoll lies, to draw the mesh with. Also `arc_ragdoll_humanoid`
  (no skeleton), `arc_ragdoll_draw` and `arc_ragdoll_remove`. Parts don't collide with each other (a shared layer).
  `samples/physics`: F drops ragdolls onto the pyramid.
- **Determinism check**: two new scenarios.
  - "ragdoll": two ragdolls and a crate on a slope.
  - "rollback": two machines in one process play a real-time rollback match over a simulated bad network, using ARC's
    own rollback code. Lag, jitter, 8% loss and duplicates make predictions go wrong, and physics with a ragdoll is
    run again from snapshots. Both machines must agree, and match the golden hash.
  `det_check` shows 7 rows.
- **Headless runs** (Moonbase Raid, MechBattleArc):
  - `--draw-last N`: immediate-mode games used to draw their render targets on every headless tick. Now only the last N
    ticks before the end, or before a shot, are drawn. A 120-tick screenshot of a game with post effects in a target
    took 44 s and now takes 0.85 s, with the same picture.
  - `--shot-at 40,80,120` (or `40=name.png`): screenshots after those ticks, drawn only then; the run ends after the
    last one when there is no `--frames`.
  - `--no-assets`: loaded images keep their size (`arc_texture_size`) and an average colour, sounds their length
    (`arc_sound_length`) but no samples. A test game with six 2048 x 2048 textures and a 30 s sound used 156 MB and now
    uses 56 MB, with the same dump.
  - The `reset` command (and `arc_reset`, always ARC's) returns to the state right after init: tick 0, the seed, vars
    and `--set` values. Games without save_state run init again.
  - `tools/arc_batch.py` runs many agent scripts through a few persistent `--stdin` processes (`-j`), with `arc_reset`
    between them, and writes one JSON line per case. Results match a fresh process per case.
  - `tests/headless_tools_test.py` checks all of these in `build test`.
- **`arc_import` / `mesh2arcm`**:
  - `--fit X Y Z` scales each axis to an exact size; `--fit-uniform X Y Z` fits inside a box;
    `--origin bottom|centre|back` places (0, 0, 0).
  - The final bounds are printed, and are in `--json` as "bounds".
- **Feature macros**: `ARC_HAS_NET` ... `ARC_HAS_RAGDOLL` in `arc.h`, one per feature group, valued with the version
  each arrived in.
- **Small text**: a one-time `ARC hint` when the built-in 8 px font is drawn below 8 px, or a baked font far below its
  size (strokes drop out and can look blank on the software renderer).
- Later (15 Nov), after MSVC's C4244 warnings kept turning up on Allan's PC:
  - A **warnings** stage in `build test` (`tools/warn_check.py`) compiles everything ARC ships: the engine (windowed
    and headless), every sample, every template, the test programs and `arc_view`.
    - On Windows it uses real MSVC with `/W3 /WX`: any warning fails the stage, with its file and line.
    - Elsewhere, clang's conversion warnings stand in: int, 64-bit or double values turned into float, and `size_t`
      into `int`.
    - The rest of the test still runs when it fails.
  - `warn_check.py FILES` checks a game's own sources the same way.
  - The 52 such spots in ARC were fixed, mostly `(float)` casts on ints in samples, templates and tests. One was a
    double tested as a bool in `arc_net.c`. No values changed: the determinism hashes are the same.
  - AGENTS.md has the rule for game code: `f` on float literals, `(float)` on ints given to floats.
  - Also `samples/demo`: an enemy's hit points were set from int constants (C4244).

## 0.9.7 — 14 Nov 2026: physics joints, terrain chunk culling, a cross-machine determinism check

Self-test 2416 (34 new); post test 346 software (pictures unchanged). The determinism hashes match on gcc and clang (-O0
to -O3, with and without FMA, ISO and GNU modes) and MinGW on the Windows C runtime; MSVC and Emscripten are checked by
`build.bat test` and `tests/det_check.c`.

- **Joints** (`arc_physics_add_joint`): hinges and ball joints between two bodies, or a body and the world (b = 0).
  - Pivot and axis are given in the world as the bodies stand.
  - Hinges take limits (`lower` / `upper`, radians) and a motor (`motor_speed`, `motor_torque`), changeable later
    (`arc_physics_joint_limits`, `_joint_motor`). `arc_physics_joint_angle` reads a's turn relative to b,
    `_joint_impulse` the pivot's load (for breakable joints).
  - Joined bodies pass through each other unless `collide`.
  - Solved with the contacts: sequential impulses, warm-started:
    - the pivot is a 3 x 3 block;
    - the hinge axis takes 2 angular rows;
    - limits are speculative rows (a gap may close in a step, no further);
    - the motor's impulse is capped by torque x dt.
  - Joined pairs wake and sleep together. Removing a body removes its joints. Joints are in `arc_physics_save` /
    `_load` (state format "PHS2") and drawn by `arc_physics_draw`.
  - Only + - * / and sqrt, as the rest of the physics: the hinge angle uses ARC's own atan2.
- **Terrain chunk culling**: `arc_terrain_draw` skips chunks out of view.
  - With sun shadows on, each chunk's box is first stretched away from the sun by the longest shadow the terrain
    can throw, so hills just off screen still shade the view.
  - With a reflection plane on, every chunk draws.
  - `arc_terrain_drawn` / `_chunk_count` / `_set_culling`. The terrain post look and the terrain sample (sun shadows)
    are pixel-identical with culling on.
- **Cross-machine determinism**: `tests/det_scenarios.h` runs five fixed scenarios and hashes every float of their
  state:
  - physics with a ramp, a hinge with limits and a motor, a rope, and a kinematic pusher;
  - navigation paths and agents;
  - terrain heights, normals, rays and scatter;
  - splat rules;
  - `arc_dmath`, `arc_rng` and noise.
  The self-test checks them against golden hashes. `tests/det_check.c` shows them in a window or web page (`build.bat
  tests\det_check.c`, `build.bat web tests\det_check.c`); `--trace FILE` writes a hash every few steps, and
  `tools/det_compare.py A B` names the first step two machines part. Fixes it found:
  - `src/arc.c` now turns off fused multiply-add for all of ARC on clang/Emscripten (`STDC FP_CONTRACT`), MSVC
    (`fp_contract`) and gcc (`GCC optimize`: gcc's GNU mode fused on any FMA CPU and gave other physics, navigation,
    terrain and dmath results).
  - The physics contact list sorted equal keys in whatever order the C library's `qsort` chose; ties now keep a fixed
    order.
  - Navigation's slope test and terrain's slope rules and scatter turns use `arc_dmath` instead of the C library's
    trig.
- `samples/physics`: a rope of capsules on ball joints and a motor-driven gate on a hinge with limits.
- AGENTS.md: joints, and the determinism rules for games, among them never two `arc_rng` draws in one call's
  arguments (C leaves their order open: gcc and clang differ, which the check caught in its own first draft).
- Later fixes (15 Nov): terrain culling read the view matrix through a pointer into a temporary (clang
  `-Wdangling` on the web build; it worked, but the compiler could reuse that memory); MSVC C4244 warnings in the joint
  solver and `det_check`; an unused-function warning for sessions on the web.

## 0.9.6 — 14 Nov 2026: terrain with splatting and instanced scatter

Self-test 2382 (39 new); post test 346 software (1 new look). Every earlier picture is identical on all three
renderers in all three depth modes. This completes the "AI items 1-7, then engine features 1-5" plan (0.9.0-0.9.6).

- **Splatting in the renderers** (all three): `arc_material.layers` (an `arc_texture_array` of 1-4 ground textures),
  `layer_scale[4]` and `splat_sharpness`. The mesh's own texture becomes the splat map (r, g, b, a = layer weights at
  the mesh's UVs); each layer is mapped from world x, z at its own scale, trilinear and repeating. Sharpness above 0
  blends by height (each layer's alpha), so stones poke through sand and borders get crisp. Detail layers, normal
  maps and pbr still apply on top. Shadow passes skip the alpha cut-out for splatted meshes.
- **`arc_texture_array`**: 1-4 same-size RGBA textures copied into one array (GL `TEXTURE_2D_ARRAY` on unit 15,
  D3D11 `Texture2DArray` on t18, the software renderer samples hidden CPU copies). ARC builds the mips itself, so
  every renderer samples the same levels.
- **`arc_terrain`** (`src/arc_terrain.c`): a height grid (up to 4097 a side) drawn as chunks (32 cells by default)
  with two lower levels of detail further away (`lod_distance`) and skirts hiding the seams between them.
  `arc_terrain_height` / `_normal` / `_raycast` / `_inside` follow the full-detail triangles exactly.
  `_sculpt`, `_flatten` and `_set_heights` edit it, rebuilding only the chunks touched. `_paint` and
  `_splat_rules` (by height and slope, soft edges, later rules over earlier) fill the splat map, and
  `_layer_weight` reads it. `_triangles` hands the surface to `arc_physics_add_triangles` and
  `arc_navmesh_add_triangles`; `_splat_map` gives the map for a minimap.
- **`arc_scatter`**: copies of a mesh placed by layer weight, height range and slope limit. Each copy gets a random
  size, turn, tint mix, lean with the ground (`align`) and sink. It is one candidate per jittered cell, and every
  number comes from a hash of the seed and the cell, so a layout is the same every run. `arc_scatter_rebuild`
  after editing keeps every copy away from the changes exactly where it was. Copies are grouped by terrain chunk;
  `arc_scatter_draw` skips chunks out of view or beyond `draw_distance`, and copies shrink away over the last 15%
  instead of popping. The mesh's own LODs and shadow casting apply.
- **Fixed (D3D11, since 0.7.2): orthographic ssao black with reversed depth.** The shader cache named entries by entry
  point and target only, so a run in one depth mode handed its `ps_ssao` (built with or without `ARC_ZREV`) to a run
  in the other. Perspective ssao hid it; orthographic ssao tests clip z and came out black. Cache names now carry the
  defines, and old caches are discarded. Real Windows (RX 6900) and Wine both now pass reversed and infinite.
- `build.bat post-d3d`: the post test in a window on Direct3D 11 (build.bat test runs it headless on the software
  renderer), standard then reversed depth, with the lines that matter printed and logs in `build\postd3d`.
- The compressed-texture check allows the GPU's own decoding 2 levels on in-between values (endpoints stay exact; ARC's
  decoder stays within 1): an RX 6900 rounds BC1/BC3 colour and BC5 interpolants exactly 2 away from the reference
  formulas, which D3D allows. It prints any texel further off.
- `build.bat web [game.c]`: the engine's own Windows build script makes WebGL2 builds too (emsdk from PATH, `EMSDK`,
  `C:\emsdk` or `%USERPROFILE%\emsdk`), and an argument that is neither a target nor a file stops with the list of
  targets instead of reaching the linker.
- `samples/terrain/terrain.c`: 200 x 200 m of hills from noise, grass, dirt, rock and snow from rules plus a painted
  track, grass tufts, trees and rocks scattered by layer. Hold the mouse to paint, raise, lower or flatten the
  ground; the scatter regrows when you let go.
- Tests:
  - Post look: hills with all four layers from rules, with trees and rocks scattered.
  - Self-test heights and normals: exact heights and normal on a sloping plane, and heights matching the
    triangles at 200 random points on bumpy ground.
  - Self-test raycasts: down, slanted, missing, and too short.
  - Self-test editing and splat: sculpt and flatten (nothing past the radius changes); texture arrays and
    materials; painting.
  - Self-test scatter: density (800 copies for 2 a square unit over 400), the same layout every build, a different
    one for another seed, only where the layer is, sizes in range, rebuilds keeping untouched copies, the slope
    limit, and leaning with the ground.
  - Self-test physics and rendering: a ball coming to rest on the terrain's triangles, and a software-rendered
    splat (red ground, blue paint).

## 0.9.5 — 14 Nov 2026: many lights (tiled) and shadows from point and spot lights

Self-test 2343 (8 new); post test 342 software, 382 GL, 358 D3D11 (4 new looks). Every earlier picture is identical
on all three renderers in all three depth modes: tiled lighting sums the same lights in the same order.

- **Up to 256 lights a frame** (`ARC_MAX_LIGHTS` is now 256 per frame; it was 16 per pass). Lights are culled to a
  16 x 9 grid of screen tiles per pass: each light's range box is projected, and each tile lists the lights that
  can reach it (up to 64; when more overlap one tile, the strongest near the camera win, logged once). A pixel only
  loops over its tile's lights. The light data, tile table and lists go to the GPU as textures (GL units 11-13,
  D3D11 t14-t16); mirror passes get tiles of their own.
- **Shadows from point and spot lights**: `arc_light_point_ex` / `arc_light_spot_ex` with `ARC_LIGHT_SHADOW`, up to
  4 a frame (`ARC_MAX_SHADOW_LIGHTS`; more light without shadows, logged once). A point light renders the pass's
  shadow-casting meshes into 6 faces (90 degrees each plus a filter margin), a spot into one (up to 72 degrees off
  its direction). All faces share one depth atlas of 6 x 4 faces; `arc_set_light_shadow_resolution` sets the face
  size (128 / 256 / 512 default / 1024). Lookups pick the face analytically, push the point out along its normal
  by 1.5 texels and toward the light by 1, and filter 3 x 3 bilinear compares inside the face. The same meshes cast
  as for the sun (`arc_mesh_set_cast_shadow`, cut-out alpha and dissolve respected); mirror passes skip them.
  `arc_light_shadow_count()` counts this frame's shadowed lights.
- `samples/lights/lights.c`: a hall of pillars with a flickering shadowed torch, a sweeping shadowed searchlight and
  up to 200 wisps that are each a light; S, W and SPACE toggle shadows, wisp count and the torch.
- Tests: post looks with 8 coloured point and spot lights (plain and pbr), the same plus a 10 x 10 grid of 100
  lights (brighter across more than 50 of the 144 screen tiles, nothing darker), and a shadow scene against the
  same without shadows (shadows only take light away, a fair area shaded, unblocked lit pixels unchanged: no acne).
  Self-test: the limits (256 lights, 4 shadowed, the 5th unshadowed) and a box shading a wall from a shadowed point
  light (the shaded pixel 0, its mirror image across the light 108).
- The post test runs up to 300 frames (`build.sh` / `build.bat`), room for more looks.

## 0.9.4 — 14 Nov 2026: deterministic rigid-body physics

Self-test 2335 (47 new); no renderer changes (post test 331, pictures as before). 500 bodies: 0.13 ms a step on
average, 1.2 ms at worst while they fall and settle.

- **`arc_physics`** (`src/arc_phys.c`): worlds of spheres, boxes and capsules; dynamic, static and kinematic bodies;
  mass (or from the volume), friction, restitution, damping, rolling resistance for round shapes, locked rotation,
  collision layers and masks, a user value. Static ground: infinite planes and one-sided triangles
  (`arc_physics_add_mesh`, `_add_triangles`), found through an x/z grid.
- Contacts: closed forms for sphere and capsule pairs (two contacts for parallel capsules), SAT with face clipping
  for box-box (up to 4 points, steady stacks) and edge-edge contacts, closest points for capsule-box, corners and
  triangle corners for box-triangle. **Speculative contacts**: pairs closer than their speed covers in a step also
  make contacts, so a step can only close a gap, not pass it (no deep overlaps on impact, no tunnelling at game
  speeds); a bounce is applied when the shapes touch, from the speed they arrived with.
- Solver: sequential impulses (friction then normal), accumulated and clamped, **warm-started** from the last step's
  contacts (matched by position), Baumgarte correction with a 5 mm slop, a fixed number of iterations (10).
- **Sleeping**: still bodies sleep after 0.5 s, whole touching groups together; moving bodies (and moved kinematic
  ones) wake what they touch.
- **Deterministic**: only + - * / and sqrt in the simulation, fixed iteration counts, a stable index order everywhere
  (sorted broadphase pairs, triangle candidates, contact caches). `arc_physics_save` / `_load` copy the whole state
  (bodies and the warm-start contacts): load and step gives the same bytes as never having saved.
- Queries and control: `arc_physics_raycast` (bodies and ground, with a mask), `_overlap_sphere`, `_events` (pairs
  that started touching this step with the impact impulse, for sounds and damage), `_impulse`, `_force`, `_set_pos`,
  `_set_vel`, `_move_kinematic` (pushes what it meets), `_wake`, `_matrix`, `_draw` (debug lines).
- `samples/physics/physics.c`: a crate pyramid, balls and capsules rolling down a triangle ramp, a spinning kinematic
  paddle, click to shoot, impact sounds, reset by loading the saved start state, save_state/load_state with physics.
- Tests: resting heights for every shape, a tumbling box ending on a face, a six-crate stack standing and sleeping
  then knocked off, bounce height with restitution 0.8 (1.276 m, predicted 1.28), sliding distance, friction on a
  20 degree slope (holds at 0.8, slides at 0.1), equal spheres swapping velocities, one impact event with the right
  impulse (55 N s for 10 kg at 5.4 m/s), round shapes never passing through each other, rolling down a triangle ramp
  at 5/7 g sin, a kinematic pusher, masks, raycasts, overlap, removal, and a pile of 40 mixed shapes: byte-identical
  across runs and across a save/load, all at rest above the ground.

## 0.9.3 — 14 Nov 2026: animation state machines, events, two-bone IK

Self-test 2288 (43 new); no renderer changes (post test 331, pictures as before).

- **Animation graphs** (`arc_anim_graph`): parameters (floats and triggers), states (a clip at a speed, looping or
  once), **1D blend states** (up to 8 clips placed along a parameter, like idle/walk/run by speed, their times kept in
  step as fractions so feet stay together), **transitions** from a state or from any state, with up to 4 conditions
  (> >= < <= == != and "trigger set"), a crossfade time and an exit time (a fraction of the clip: 1 = when it ends),
  tried in the order they were made; a trigger is cleared when a transition uses it.
- **Events on clips** (`arc_anim_graph_event`): names at times; an update reports the ones it passed (through loops,
  several per update if needed) in `arc_animator.events`, `arc_animator_fired`. A blend reports its heavier clip's.
- **`arc_animator`**: plain data (fits save_state): `_init`, `_set`, `_setf`, `_trigger`, `_play` (forced, with a
  fade), `_update`, `_pose` (the current state and, while fading, the previous one), `_state_name`, `_progress`.
- **Two-bone IK** (`arc_pose_two_bone_ik`): bends parent-of-parent / parent / tip so the tip reaches a target, the
  middle joint toward a pole; weight 0..1; optionally keeps the tip's own orientation (flat feet); joints below the
  tip follow; out-of-reach targets get the chain pointing straight at them (returns false). `arc_pose_joint_pos`.
- `samples/anim/anim.c`: a stick figure built in code (no art), idle/walk/run blended by speed, a jump on a trigger
  that returns when its clip ends, footstep events, and foot IK on bumpy ground (planted feet at exactly the ankle
  height above it, the hips lowered to the lower foot).
- Tests: transitions by condition, trigger and exit time; fades; blend poses and blend loop lengths; footstep counts
  through loops; forced play; byte-identical repeat runs; IK reach, bone lengths, pole side (both ways), kept tip
  rotation, out of reach, weights, a joint without a grandparent, IK on an animated pose, and 500 random targets and
  poles from random bent poses.

## 0.9.2 — 14 Nov 2026: navigation (navmesh and path following)

Self-test 2245 (65 new); post test 331 and every earlier picture unchanged (no renderer changes).

- **Navmesh** (`src/arc_nav.c`, `arc_navmesh`): built from any mix of `arc_navmesh_add_triangles`, `_add_mesh`
  (a loaded or made mesh's upward triangles, through a model matrix) and `_add_grid` (walkable tiles of an
  `arc_grid`); `_remove_box` takes out the area under obstacles. Triangles steeper than `max_slope` (0.8 rad) or
  facing down are left out; vertices within `weld` (0.01) join, so separate floor pieces connect. `_islands` counts
  separate areas, `_triangles` the walkable triangles.
- **Paths**: `arc_navmesh_path(nm, from, to, radius, out, max)`: A* over triangles (entering each where the line to
  the goal crosses, so straight routes win), the funnel algorithm for the fewest corners, corners kept `radius` from
  the outline, doorways narrower than 2 x radius closed, then corners dropped where a band as wide as the agent is
  clear. An unreachable target gives the path to its nearest walkable point; separate islands give 0.
- **Queries**: `_nearest` (closest walkable point), `_walkable_line` (a straight walk over the surface, with where it
  stops), `_connected`, `_height`, `_random_point` (even by area; an `arc_rng` or arc_rand), `_path_length`, and
  `arc_navmesh_draw` for debugging. Queries never allocate; everything is deterministic.
- **Agents** (`arc_nav_agent`, plain data: fits `save_state`): `_init`, `_goto`, `_update` (steering, acceleration,
  slowing to arrive, turning before corners, sliding along the outline, finding the rest of a path longer than 32
  corners), `_stop`, `_remaining`, `_draw`, and `arc_nav_agents_separate` for crowds. `moving`, `arrived`, `failed`,
  `partial` say where it is.
- `samples/nav/nav.c`: a walled level, 12 agents walking to random points, click to send them, the navmesh drawn.
- Tests: paths around a wall (with and without radius), doorway widths, islands, welding, slopes, meshes and boxes
  taken out, nearest/walkable/height/random points, agents arriving, determinism (byte-identical runs), sliding,
  separation, a 40-corner zigzag, and 300 random paths across a walled level (all walkable, none shorter than
  straight).

## 0.9.1 — 14 Nov 2026: genre templates, a test summary

No engine changes (pictures as 0.9.0). `build.sh test`: self-test 2180, post test 331, every stage below passes.

- **Genre templates**: `tools/new_game.py DIR --template NAME` (`--list-templates`) makes a playable game with no art.
  Each has all its rules in `tick()`, a full `dump()`, agent commands, `arc_var` tunables, `save_state`/`load_state`
  (rewind, seek and the timeline work), named meshes for `frame`, synth sounds, and `tests/acceptance.md`:
  - `tactics`: 8x8 board with walls, three units a side, move (paths around walls and units) and attack, the red
    side walks toward you and attacks; win/lose. Commands `select`, `move X Y`, `attack`, `end`, `win`. 15 checks.
  - `shooter`: top-down arena, WASD + mouse aim + fire, enemies in growing waves, lives, score. Commands `aim`,
    `fire`, `wave`, `spawn`, `spawn_at X Z`, `clear`, `lives`, `god`. 18 checks.
  - `platformer`: tile map from strings, box-vs-tile physics, coyote time, jump buffering, variable jump height,
    coins, spikes, checkpoint, flag. Commands `tp X Y`, `kill`, `win`. 19 checks.
  - `racer`: a closed track from centre-line points, arcade car physics, off-road drag, checkpoints in order, three
    laps, a computer car, countdown, best lap, race position. Commands `go`, `tp_cp N`, `lap N`, `ai off`. 13 checks.
  - `cards`: Twenty-One against the dealer: a seeded deck, hands, hidden dealer card, betting, 3:2 payout, the dealer
    drawing to 17, UI buttons, slide-in dealing. Commands `deal`, `hit`, `stand`, `bet`, `chips`, `stack AS,KH,...`
    (a deck with chosen cards on top, for exact tests). 20 checks.
  The game's `AGENTS.md` gets the template's notes; every new game also gets a `release.json`, and its `AGENTS.md`
  points at QUICK.md, arc_check, the doctor, arc_import and arc_release.
- **`arc_check.py`: `game .`** builds the game project the check file is in (the folder with `arc.txt`) with its own
  `build.sh headless` / `build.bat headless`.
- **Test summary**: `build.sh test` and `build.bat test` keep the whole run in `build/test.log` and end with one line
  per stage and ALL PASSED / FAILED (`tools/test_summary.py`); a stage that never reported shows as MISSING with the
  last lines of the log. `build.bat test` gained the release dry run and the templates stage.

## 0.9.0 — 14 Nov 2026: tools for working with AI (part 1 of the 0.9 series)

Engine pictures unchanged: every post-test picture pixel-identical to 0.8.9 in the software renderer (209), GL (209)
and D3D11 under Wine (209). Self-test 2180; post test 331 (software), 342 (D3D11 under Wine). New test steps in
`build.sh test` / `build.bat test`: acceptance checks, doctor, API docs up to date, the QUICK.md example builds and
runs, asset import, release dry run.

- **`frame [FILE.json]` agent command**: draws the current state and describes the screen as JSON: the camera, each
  mesh by name with its on-screen instances (screen box, position, tint; nearest 24), lights, texts (including rich
  text, wrapped and aligned text), sprites, rects and UI widgets. X-ray copies and outline hulls are left out.
  Tests can ask "is the coin visible?" without opening a picture.
- **Mesh and texture names**: `arc_mesh_set_name`, `arc_mesh_name`, `arc_texture_set_name`, `arc_texture_name`. Loaded
  files are named after the file (no folder, no extension), primitives after their kind ("cube", "sphere"...).
- **Hints**: `ARC hint ...` lines, once per kind, for things that work but break tests or speed: drawing outside
  `draw()`, `arc_rand_*` in `draw()`, the same file loaded again and again, loads inside `tick`/`draw`, `dump` with no
  dump callback.
- **`tools/arc_check.py`**: runnable acceptance tests in a request (an ```arc-check block): builds the game headless,
  plays the `do` lines and checks each `expect` against the dump, the frame and the UI (`==`, `<`, `contains`,
  `exists`, `count()`, selectors `[name=coin]`, `[*]`, `[N]`). Exit 1 on any FAIL or `ARC error`.
  `docs/REQUESTS.md`: the request template with the block.
- **`tools/arc_doctor.py`**: reads a game's code and assets and lists problems, each with a fix: `rand()`/time in game
  code, no `.dump`, loads in `tick`/`draw`, drawing in `tick`, assets named in code but missing, big textures/WAVs,
  missing build scripts or AGENTS.md. `--json`; ERROR exits 1.
- **API docs for agents**: `tools/gen_api_docs.py` now also writes `docs/QUICK.md` (every public name, one line each, by
  topic, with a complete small game) and `docs/api/<topic>.md` (29 topic pages; Graphics split into 11). `--check`
  fails the build test when they don't match `include/arc.h`; the QUICK.md example is compiled and run in the test.
- **`tools/arc_import.py`**: one step from an art file to a game asset: models -> `assets/meshes/*.arcm` (mesh2arcm;
  `--height`/`--footprint`/`--yaw`/`--anim`), pictures -> `assets/textures/` (shrunk past `--max-size`; `--compress` ->
  `.arct`), sounds -> `assets/sfx/` or `assets/music/` (by length; OGG/FLAC converted), fonts -> `assets/fonts/*.arcf`.
  Renders a preview of each mesh, texture and font with the new `tools/arc_view.c` into `build/import/`, warns about
  off-centre, mis-scaled, huge or non-power-of-two assets, and prints the line of C that loads each one. `--dry-run`,
  `--json`.
- **`tools/arc_release.py`**: doctor, acceptance files, `build release`, signtool (when `release.json` has a `sign`
  section; password from `ARC_SIGN_PASSWORD`) and `butler push` per channel with `--userversion`. `--dry-run` runs the
  doctor and shows every command; missing butler or signtool are reported up front. Log in `dist/release_log.json`.
- AGENTS.md: sections for frame, hints, arc_check, doctor, import and release; the loop now starts from docs/QUICK.md.
- Known (not new): under Wine, D3D11 SSAO with an orthographic camera and reversed depth comes out black (look
  L_OSSAO; the same picture as 0.8.9). GL and the software renderer are right; to be checked on real Windows.

## 0.8.9 — 14 Nov 2026: a third paint colour (MechBattleArc's request)

- **`arc_mesh_style.tint3`**: with a `paint_mask`, the mask's blue channel takes `tint3`, applied after red and green
  the same way: colour = mix(that, tint3 x (0.35 + luminance), B). `tint3` 0 = blue ignored, so masks with blue in them
  look as before until a game sets it. Static and skinned meshes, every renderer; batches split per (mask, tint2,
  tint3) as before.
- Test (post test, `L_PAINT3`): two schemes interleaved, one with tint3 orange (bottom-right orange, bottom-left the
  grey texture), one with tint3 0 (both grey). Software, GL and D3D11 identical; earlier pictures unchanged. Self-test
  2180; post test 331 (software), 357 (GL), 342 (D3D11 under Wine).

## 0.8.8 — 12 Nov 2026: world-mapped hex base (Moonbase Raid's report), two-colour paint (MechBattleArc's request)

Opt-in; every earlier post-test picture pixel-identical in the software renderer, GL and D3D11 (Wine). Self-test 2180
(2168 on Windows); post test 318 (software), 341 (GL; 333 with msaa 4), 322-331 (D3D11 under Wine: the count varies
from run to run there, as before); reversed and infinite reversed depth pass. GL, D3D11 and the software renderer agree
to 2 levels on the new looks (GL and D3D11 to 0).

## Fixed: hex tiling on a base texture (Moonbase Raid, 11 Nov)

- `base_tiling = ARC_TILE_HEX` laid its cells in the mesh's UVs, so on a floor of tile meshes (UVs 0..1 a tile) every
  tile showed the same few cells and the tile edges didn't meet (the "embossed, seamy" look); a texture with clamped
  addressing also smeared where turned cells ran past 0..1. Now:
  - **`arc_material.base_scale`**: the colour texture is mapped from world position (repeats a world unit, on the
    plane each triangle faces most), plain or hex: one texture runs across all the tiles. Normal, ORM and emissive maps
    keep the mesh's UVs.
  - Hex and world-mapped colour sampling always repeats the texture, whatever its own addressing (GL sets
    `GL_REPEAT` for the batch, D3D11 picks the repeating sampler, the software renderer wraps).
  - The docs say not to turn (`base_rotate`) textures with baked light and shadow (craters, pebbles): turned cells
    point their highlights different ways and look embossed. Moonbase's regolith: `base_tiling = ARC_TILE_HEX`,
    `base_scale` about 0.25-0.5, `base_rotate = 0` (or 0.1).
- `arc_post.bloom_threshold` docs: lit pale surfaces pass 1 easily under point lights; try 1.4-1.8 with bloom 0.3-0.4.

## New: two-colour paint and posed styled draws (MechBattleArc, 12 Nov)

- **`arc_mesh_style.paint_mask`, `tint2`**: a mask in the mesh's UVs: red = how much of `tint`, green = how much of
  `tint2`, black keeps the texture's colour. colour = mix(texture, tint x (0.35 + luminance), R), then mix(that, tint2 x
  (0.35 + luminance), G), so shading and wear survive. With a mask, tint no longer multiplies the whole mesh. Static and
  skinned meshes, every renderer; the mask uses its own filtering.
- Batching: painted solid draws go to their own slot, sorted by (mask, tint2, mesh), so each paint scheme of a mesh is
  one batch however the game interleaves its draws; see-through and ordered painted draws keep their order and carry
  the paint in their batch (`arc__mesh_batch.paint_tex`, `tint2`).
- **`arc_draw_mesh_posed_styled(mesh, model, anim, pose, style)`**: the posed draw with the whole style. (The style
  already took `anim` and `pose`, so recolour, outline, x-ray, dissolve and the rest worked on skinned meshes through
  `arc_draw_mesh_styled`; this is the call MechBattleArc asked for.) Outline and x-ray copies are never painted.

## Tests

- Post test, 4 looks (`v088_`): a floor of 48 tile meshes (UVs 0..1, a clamped texture, hex, rotate 0): mesh UVs give
  autocorrelation 1.000 at one tile's lag and tile-edge steps 1.7x the rest; `base_scale` 0.37 gives -0.034 and 1.15x.
  Four quads with two paint schemes interleaved: tint top-left, tint2 top-right, the grey texture below, on each. A
  skinned cube moved by its pose, drawn with `arc_draw_mesh_posed_styled`: painted red over blue, yellow outline.

## 0.8.7 — 10 Nov 2026: planar reflections (HOSTLESS's request)

Opt-in: with `arc_reflection_plane` never called (or strength 0) every frame is exactly as on 0.8.6: all earlier post-test
pictures pixel-identical in the software renderer, GL and D3D11 (Wine). Self-test 2180; post test 296 (software),
315 / 311 (GL msaa 0 / 4), 304 (D3D11 under Wine); reversed and infinite reversed depth pass. WebGL2 not run here
(no emsdk in this session): the shader is GLSL ES 3.0 throughout (`textureLod`, `gl_FragCoord`), unchanged in kind.

## New

- **`arc_reflection_plane(y, strength, scale)`**, **`arc_mesh_set_reflective(mesh, amount)`**,
  **`arc_mesh_set_reflected(mesh, on)`**: polished floors, water, ice. At the end of the frame ARC adds a mirror pass
  before the first screen pass that draws a reflective mesh: that pass's camera reflected in y, its lights, sky and
  fog, no shadows, post or 2D, opaque and see-through meshes and models (skinned too), pixels below y + 0.005
  discarded, culling swapped for the mirrored winding, into a target `scale` x the screen (0 = 0.5) with mip levels.
  Reflective meshes sample it at their own pixel: `colour = mix(colour, reflection, strength x amount x vertex alpha x
  F)`, F = 0.6 + 0.4 (1 - N.V)^5, blurred by the material's roughness (mip level 6 x roughness, trilinear; plain
  materials sharp) and nudged by the normal map, before the flash and the fog. Vertex alpha is the per-cell polish
  mask (opaque meshes stay opaque), so HOSTLESS can use one floor mesh and fade cells in as the robot buffs them.
- Reflective meshes and meshes set not reflected are left out of the mirror pass; immediate triangles, lines, 2D and
  decal/stamp quads at floor level are not mirrored. The settings survive background loading, mesh reloads, LOD
  meshes and static batches (which merge only meshes with the same settings). Turning strength to 0 frees the target.

## Internal

- `arc__pass` gains `mirror`, `clip[4]`, `refl_tex`, `refl[4]` (see its comment for the shading every backend does);
  `arc__mesh_rec` gains `reflect`, `unreflected`. The front end builds the mirror pass (`refl_pass` in arc_gfx.c) by
  copying the screen pass, reflecting its matrices and filtering its batches; the reflection target is a render
  target with `ARC_TEX_MIPMAPS` (GL `glGenerateMipmap`, D3D11 `GenerateMips` after the pass, software: box mips).
- GL: `u_clip`, `u_rpar`, `u_ramt`, `u_rtex` on unit 9. D3D11: `clipp`, `rpar` in cbuffer F, `m6` in cbuffer M,
  `rtex` t12 with a clamped mip sampler s3.

## Tests

- Post test, 5 looks (`v087r_`): a polished floor (pbr, metallic 0.45, roughness 0.05 / 0.6), a grey pillar with a red
  cap, an emissive ball, a green box under the floor. Strength 0 = the frame with nothing reflective, pixel for pixel;
  the cap's reflection lies below the floor line, beyond the reflected pillar (upright mirror image); the ball's glow
  shows in the floor; no green anywhere (nothing from below the plane); roughness 0.6 halves the edge contrast or
  more (3.48 -> 1.00); vertex alpha 0 on half the floor removes the ball's reflection and keeps the pillar's.
  The software renderer, GL and D3D11 agree to 1 level on all five.

## Limits

- One plane, horizontal. Static batches are culled to the main camera, so things just off screen can be missing
  from the reflection. The target is 8-bit, so HDR glow is clipped in the reflection (bloom still sees the floor).
- Particles drawn as meshes face the main camera in the reflection too.

## 0.8.6 — 9 Nov 2026: faster web loading

Opt-in: games that change nothing build and behave exactly as on 0.8.5 (Droids and Moonbase Raid pixel-identical,
Evolution within its own run-to-run noise; desktop and headless never use the new paths). Self-test 2180 checks (53 new); post test 284 (software, WebGL2), 296 / 302 (GL msaa 0 / 4),
290 / 291 (D3D11 under Wine, msaa 0 / 4); `tests/web_load_test.py` (9 browser runs), net_test.py, relay_test.py and
web_mouse_test.py pass. How to use it all and the measurements: `docs/WEB.md`.

## New

- **Browser image decoding** (`arc_app.browser_decode`, appended at the end of `arc_app`; also `--browser-decode`,
  `--no-browser-decode`, `--browser-decode=bytes`, `--image-decoder bitmap|imagedecoder|img`): on the web, JPEG and PNG
  images in queued loads (textures and the maps inside `.arcm` meshes) are decoded by the browser off the main thread
  (Chromium: `createImageBitmap` with `premultiplyAlpha: 'none'`, `colorSpaceConversion: 'none'`; Firefox: `ImageDecoder`
  for JPEG / opaque PNG; Safari: `<img>` + `decode()`, see-through PNGs left to ARC), up to 16 at once, started ahead of
  the loader's finish point within its budget, and uploaded GPU-direct (`texImage2D` from the image, no flip / premultiply
  / colour conversion, mips by `glGenerateMipmap`), or read back exactly through a scratch framebuffer when the CPU needs
  the bytes. ARC keeps images it would decode differently (16-bit PNG; gAMA / cHRM / iCCP / eXIf chunks; EXIF-turned,
  CMYK, lossless or arithmetic JPEGs; textures over `arc_set_texture_max_size`), and decodes with its own decoder when the
  browser fails or `arc_load_wait` must finish now. PNG results identical to ARC's; on Droids' 98 JPEG textures the
  largest difference was 1 level, mean 0.06 (createImageBitmap) / 0.0003 (ImageDecoder).
- **Packs fetched on demand**: `arc_pack_fetch(name)` (returns a load group), `arc_pack_state`, `arc_pack_ready`,
  `ARC_PACK_NONE/FETCHING/READY/FAILED`. A queued load of a file listed in the build's `arc_packs.txt` whose pack is not
  here yet fetches that pack first (a pack item in the group, mounted in queue order; its download counts in the group's
  bytes and progress). Downloads use `fetch()` (streamed), and the browser's Cache API keeps each pack under
  `<name>?arc=<content hash>` (written from the response stream by the browser; older builds' copies deleted), so a
  second visit reads it from the cache. Desktop, headless and single-package web builds: done at once (a pack file of
  that name is mounted; otherwise the files are loose / preloaded). Failed packs fail their files; asking again retries.
- **Build support**: `tools/web_packs.py` (splits `assets/`: listed folders -> `<folder>.arcpak` next to the page, the
  rest + `arc_packs.txt` into the preloaded package); `WEB_PACKS="levels music"` / `"*"` in ARC's `build.sh web`
  (+ `WEB_CACHE=1` for `--use-preload-cache` alone) and in the `tools/new_game.py` build.sh / build.bat templates
  (`--web-packs` option; `--update-scripts` keeps a game's list; build.bat keeps CRLF). With packs the first package is
  cached in IndexedDB (`--use-preload-cache`). The default is unchanged: one preloaded package.
- `.arct` / `.arcm` v4 blocks: confirmed uploaded as blocks in WebGL2 (S3TC, RGTC, BPTC present in Chromium/SwiftShader),
  and decoded to RGBA8 with `--no-compressed-textures` (now covered by the browser test). `WebAssembly.instantiateStreaming`
  is Emscripten's default with the stock shell (the server must send `application/wasm`); documented with cache-header
  advice and what itch.io does and does not let you control.

## Internal

- `src/arc_webload.c` (new, in the unity build after `arc_load.c`): the pack table and manifest, the JavaScript hooks
  (EM_JS only; C polls them, no callbacks into wasm). `arc__imgdec` / `arc__fetcher` interfaces with test hooks
  (`arc__imgdec_hook`, `arc__fetcher_hook`); `arc__img` gained `src`, `src_n`, `web`, `web_done`; `arc__backend` gained
  `tex_create_web` (WebGL2 only; NULL in soft, D3D11, desktop GL); `arc__mesh_parse_ex` (defer images);
  `arc__pack_mount_owned`; `ARC__MAX_PACKS` 8 -> 32; loader items can wait asynchronously (state 3) without worker
  threads; `arc__load_busy_ms` (main-thread time in the loader, for measurements).
- `tests/test_v086_webload.c` (`v086w_`, run from the tick-9 checks): headers, cache keys, the tool's split, the loader
  fetching packs through a fake downloader (order, progress, failure, retry, unlisted packs, no manifest), the async
  image path through a fake decoder (read-back bytes, fallback, `arc_load_wait`, destroyed handles), desktop behaviour.
  `build.sh test` and `build.bat test` now also run `tools/web_packs.py` into `build/wpstage`, `build/wpout`.
- `tests/web_load.c` + `tests/web_load_test.py`: browser checks, `--webload-bench` (measurements) and
  `--webload-compare` (decode differences on real textures). `tests/web_run.js`: `--visits`, `--throttle`, `--metrics`.

## Limits

- Only Chromium (headless, SwiftShader) was tested; the Firefox and Safari paths are written to the APIs' documented
  behaviour but are untested here. `<img>` uploads may re-decode on the main thread in some browsers.
- Plain loads at once (`arc_texture_load` outside a group) keep ARC's decoders (they must return finished textures).
- A group with flags 0 that waits for a pack holds the ticks until the download is done (correct, not faster).
- `build.bat test`'s new web_packs step was written but not run here (no Windows/MSVC in this session).

## 0.8.5 — 9 Nov 2026: online and browser multiplayer (Operation Chariot's request)

Opt-in: games that do not use the new calls behave exactly as on 0.8.4 (Droids and Moonbase Raid pixel-identical, Evolution within its own noise). Self-test 2127 checks; post test 284 (software); tests/relay_test.py (also with Windows clients under Wine), net_test.py, rollback_test.py and web_mouse_test.py pass.

Operation Chariot's request (`done-notes/chariot-web-multiplayer.md`): ARC 0.8's turn-based sessions over the internet and
from the web build, through a relay with room codes; simultaneous turns. `ARC_VERSION` is unchanged (0.8.4) in this
phase; old games build and behave as before (all new fields zero = old behaviour).

## New

- **`arc_relay`** (`tools/arc_relay/arc_relay.c`, `./build.sh relay`, `build.bat relay`; Linux and Windows): a small server
  with no game code that does a LAN host's part for rooms named by short codes (4-6 characters from
  `23456789ABCDEFGHJKMNPQRSTUVWXYZ`): numbers and broadcasts commands, refuses commands out of turn, sequences joins,
  leaves, rejoins, ready flags, names, the start and turn ends, compares state hashes (DESYNC to everyone), keeps each
  room's log for rejoins. WebSocket (RFC 6455 handshake and framing, SHA-1 and base64 written in C) and ARC's raw TCP
  protocol on one port; a plain GET answers a status line. Limits: rooms, connections (in all, per address), room
  creations and wrong codes per address per minute, messages and bytes per second per link, a room's log (entries and
  MB); empty rooms expire, quiet rooms close. SIGTERM / Ctrl+C: a clean shutdown that tells the players to come back.
  After a restart the players reconnect and the room is restored from the longest log among those who come back.
  A live player is not replaced by a newcomer using its name (only by its own running game, or after 5 s of silence).
  Logs to stdout. Deployment (VPS, Caddy / nginx for wss://, systemd, a Cloudflare Worker + Durable Object sketch):
  `docs/RELAY.md`.
- **Shared session core** `src/arc_net_core.h` (header only, used by `arc_net.c` and `arc_relay.c`): the wire format,
  the session view and `view_apply`, the host's decisions (`ncore_cmd_check`, `ncore_hello_stale` / `ncore_hello_slot`,
  `ncore_unique_name`, `ncore_version_check`, `ncore_hash_put`), room codes, SHA-1, base64, the WebSocket accept key,
  frame header encode / parse, HTTP header parsing, relay URLs. The LAN host was refactored onto it (same messages and
  behaviour; `tests/net_test.py` passes unchanged).
- **WebSocket client transport**: desktop builds speak `ws://` (and `tcp://`, the raw protocol) to the relay over the
  existing non-blocking sockets (Winsock still loaded at run time); the web build uses the browser's WebSocket through
  EM_JS (no pthreads). `wss://` on desktop is not supported (no TLS without a library; documented). `--net-sim` applies
  to these links as to LAN links.
- **API** (`include/arc.h`, "Online: the relay" and "Simultaneous turns"):
  `arc_net_desc.relay` and `arc_net_desc.turns` (appended at the end), `arc_net_host_online()`,
  `arc_net_join_online(code)`, `arc_net_room()`, `arc_net_relay_available()`, `arc_net_turn_waiting()`,
  `ARC_NET_TURNS_ALTERNATE` / `ARC_NET_TURNS_SIMULTANEOUS`, `ARC_NET_EVERYONE`, events `ARC_NET_EV_ROOM` and
  `ARC_NET_EV_TURN_DONE` (appended to the enum). Status: the room's creator goes JOINING -> HOSTING (and
  `arc_net_is_host()`), joiners JOINING -> CONNECTED; wrong code / version / full / started -> REFUSED with the reason.
  On the web `arc_net_host` / `arc_net_join` go online when a relay is set (UNAVAILABLE only without one).
  `arc_net_join("ws://host:port/CODE")` also joins online. Rollback matches stay LAN only (refused online).
- **Simultaneous turns** (LAN host and relay alike): anyone in the match sends during a turn; the turn ends when every
  connected player has called `arc_net_turn_end()` once; a dropped player does not hold the turn (if a drop leaves only
  finished players, the turn ends right after its LEFT); TURN_DONE events and `arc_net_turn_waiting()` tell who is
  still to finish. The mode travels in the sequenced START entry ("seed simultaneous"), so every machine agrees.
- **Lobby**: "Host online", a "Room code" field and "Join by code" when a relay is set; with a gamepad or a touch screen
  a grid of letter buttons for the code (focus keys + A); the room code shown in the room; the web build shows only the
  online part.
- **Agent commands / flags**: `net host_online`, `net join_online CODE`, `net room`, `net wait time SECONDS`,
  `--net-relay URL`; `net status` shows `room=`, `dump` has `net.room`, `net.relay`, `net.turns`, `net.waiting`.
  The relay's room code is printed as `ARC net room CODE relay=URL player=N`.
- **netturns** (sample): `--simultaneous`, an `order TEXT` command (recorded in sequence order in the rules state), and
  the tunable `netturns.hp` (default 10, as before) for longer test matches.

## Fixed / changed

- Refusals of an end of turn now show `end_turn` as the refused command (the text was empty).
- `arc__input_test_step` (the self-test's pretend ticks) carries text typed with `arc__input_char`, as a real frame
  does.

## Tests

- Self-test: `tests/test_v085_relay.c` (144 checks): SHA-1 (FIPS vectors, a million 'a's in uneven pieces), base64
  (RFC 4648), the RFC 6455 accept key, frame headers and parsing (the RFC's examples, 16/64-bit lengths, invalid frames),
  HTTP headers, room codes (alphabet, spread, normalising), relay URLs, the host's decisions (alternating and
  simultaneous), HELLO round trip, hash comparison; a simultaneous session of three in-process machines (scrambled
  orders, one sequence everywhere, the turn advancing once after the third end, a host-side refusal of a second end, a
  drop that ends the turn, a rejoin, the snapshot); the online API refusals; the lobby (typing a code, the gamepad letter
  grid with nav keys and A); a WebSocket client handshake and framing against a socket the test listens on (masking,
  ping/pong, a message split across a fragment and a continuation, BYE with retry). Each group was checked to fail on a
  deliberate breakage. Total 2127 checks.
- Post test: a new look (the lobby with a relay set), 284 checks (software), all four renderers at msaa 0 and 4.
- `tests/relay_test.py`: 12 scenarios (basic with lag/jitter/loss, raw TCP + WebSocket, three players with a pulled
  cable, crash and rejoin, turn refusals, desync and reports, version, codes / full / started / impostor, relay restart
  by SIGTERM and SIGKILL with restore, simultaneous turns on the relay and on a LAN host, limits / status / shutdown,
  and `--web`: the web build in headless Chromium + a Linux client (+ `--wine` a Windows client) in one room, same hash
  after 50+ game commands). Passes with Linux clients, Windows clients under Wine, and the Windows relay under Wine.
- `tests/web_mouse_test.py` + `tests/web_mouse.c` + `tests/web_run.js`: an `arc_ui_button` clicked with real mouse
  events in headless Chromium with the canvas letterboxed, pillarboxed and scaled: fires every time (and fails on the
  0.8.3 code, checked).

## 0.8.4 — 9 Nov 2026

Verified on all four renderers including WebGL2 (headless Chromium), with and without MSAA: the hex tiling, detail layer, detail mask and stamps shaders, and the web build with the mouse fix. Self-test 1983; post test 277 (software), 289-290 (GL), 281-288 (D3D11 under Wine), 277-278 (WebGL2). Droids and Moonbase Raid pictures unchanged from 0.8.3; against 0.8.1 Droids differs by under 100 pixels a shot (the 0.8.3 JPEG decoder).

### New: dirt and wear overlays (Moonbase Raid's second request, 8 Nov 2026; all renderers)
What Moonbase Raid built by hand (masked grime textures from a Python script, two hashed see-through quads a floor
tile, a noise density map) as four pieces of ARC:
- **Stamps** (`arc_stamps`, `arc_draw_stamps`, `arc_draw_stamps_on`): scattered overlays over a grid of cells,
  `per_cell` a cell (fractions allowed), each with a hashed texture (up to 8), turn (`rotate`), size (default 1.2-2.2
  cells) and offset (`jitter`), stacked `layer_gap` apart. A `density(ud, cx, cz)` callback scales each cell (0 draws
  nothing). Lit like the surface, depth-test-only with `depth_pull` 0.002, drawn first in the see-through pass (before
  decals), one draw per texture (each texture gets its own `order`, so the sort keeps them together).
  `arc_draw_stamps_on` lays them on any flat surface (walls, ramps) from an origin and two cell vectors.
- **Grime overlays** (`arc_breakup`, `arc_image_breakup`, `arc_texture_breakup`, and `tools/grime.py` offline): a
  seeded crop of a dirt image; alpha = distance from the mean brightness x an fbm breakup mask (threshold, softness,
  an optional second mask OR-ed in) x a round edge fade; grime darkened, scratches kept pale. The tool writes N
  variants with crop sizes and thresholds spread over ranges (`--sizes 260,1000 --threshold .4,.55`, as asked); its
  pixels match the C function's (colour exact, alpha within 1 level).
- **Noise** (`arc_noise_value2`, `arc_noise_fbm2`): smoothstep value noise and its fbm, 0..1, from an integer hash
  (the same one the tool uses).
- **A breakup mask on the detail layer** (`arc_material.detail_mask`, `mask_scale`, `mask_threshold`,
  `mask_softness`): the detail's amount is also times `clamp((mask - threshold) / softness)`, the mask mapped like the
  detail at its own scale. Dirt in patches with one draw a tile and no overlay quads. The detail's alpha now says how
  much of it applies (it was ignored; textures without alpha are unchanged).
- Also: `arc_mesh_style.uv_mirror` (u flipped before the turn; missed from the first request), and
  `arc_image_save_png` keeps alpha when an image has any (it always wrote RGB; screenshots still do).
- Test (post test, 6 looks, 20 checks): a 32 x 32 floor with stamps at 2 a cell, rotate 1, sizes 1.2-2.2: the
  picture's autocorrelation at one cell's lag is 0.001, at two cells 0.001, at other lags -0.010 (no grid shows);
  with `density` 0 the picture is the bare floor, pixel for pixel. Stamps on a wall; the masked detail leaves about
  half the floor clean; `uv_mirror`; noise ranges and seeds; a PNG with alpha round-trips. GL and D3D11 (Wine) agree
  to 1 level; the software renderer differs by up to 17 on the stamps (it picks one mip level a triangle). Earlier
  pictures unchanged on all three.

### New: textures that don't visibly repeat (Moonbase Raid's request, 8 Nov 2026; all renderers)
- `arc_mesh_style` gains `uv_rotate` (radians, about the texture's centre), `uv_scale` (0 = 1), `uv_offset` and `uv_mirror`,
  applied at draw time to the mesh's UVs (all its maps follow; scrolling still adds on top). One floor mesh drawn per
  tile with a random quarter turn or offset no longer lines up. Per instance: the instance record grows from 144 to
  160 bytes.
- `arc_material` gains a detail layer: `detail` (a texture), `detail_scale` (repeats a world unit; 0 = the mesh's own
  UVs), `detail_strength` (0 = 1). It multiplies the colour by `mix(1, 2 * detail, strength)`, so 50% grey changes
  nothing. With a scale it is mapped from world position on the plane each triangle faces most (floors x, z; walls
  x, y or z, y), so it ignores the tile grid and the mesh's UVs.
- Hex tiling (Mikkelsen, JCGT 2022): `detail_tiling` / `base_tiling = ARC_TILE_HEX`, with `*_rotate` (0..1: 0 for
  bricks, 1 for dirt and sand) and `*_contrast` (0.5..0.99, 0 = 0.75). Each hexagonal cell gets a random offset and
  turn; three samples are blended by weight and brightness so no seams show; mip levels come from the untouched UVs
  (`textureGrad` / `SampleGrad`, one level a triangle in the software renderer), so it doesn't sparkle. `base_tiling`
  affects the colour texture only; normal, ORM and emissive maps keep plain UVs. Cells, turns and weights use ARC's
  integer hash, so every renderer picks the same ones.
- Cost: hex is three texture reads instead of one (six with a hex detail map). Meshes without these settings draw
  exactly as before: every earlier post-test picture is pixel-identical in the software renderer, GL and D3D11.
- `arc_mesh_material` reads the new fields back (defaults filled in); static batches and LOD meshes keep them.
- Test (post test, 13 checks): the request's 16 x 16 floor with dirt tiled 16 times, seen from above. Autocorrelation
  at one tile's lag: 1.000 plain repeat, 0.001 hex with rotate 1, -0.010 hex with rotate 0. At a grazing angle hex
  stays within the sparkle limit (mean neighbour difference 3.3 vs 2.6 for repeat in software, 0.68 vs 0.44 on GL).
  A 50% grey detail changes nothing; a world-mapped checker lands on 1-unit-aligned squares on both floor and wall
  whatever the UVs; `uv_rotate` turns the 3 x 3 colour texture a quarter turn. GL and D3D11 (Wine) agree with each
  other to 1 level and with the software renderer to 2 (hex looks from above).
- Not done from the request: Quilez's per-cell method and texture bombing (hex covers both uses).

### Fixed: web mouse clicks on a canvas that isn't at the page's top-left (HOSTLESS's request, 8 Nov 2026)
- `mouseup` is registered on the window (so a release outside the canvas still counts), and Emscripten gives a
  window target's `targetX/Y` in page coordinates. On the default shell's centred, letterboxed canvas the release
  landed off by the canvas's offset, so `arc_ui_button` never fired on a real click (keys and `ui_click` worked).
- `plat_web.c` now maps every mouse event from `clientX/Y` minus the canvas's `getBoundingClientRect()`. Touch is
  registered on the canvas and was already right. Pointer lock (mouse look) is unchanged.
- Not built or run here (no emsdk in this session): check with HOSTLESS's letterboxed-page click test, then remove its
  `shell.html` workaround.

## 0.8.3 — 8 Nov 2026: faster loading

From the loading research (`claude/arc-loading-research.md` in the project). Measured on a 2-core cloud machine:
16 Ludo models (three 1024 JPEG maps each) load in 0.84 s instead of 2.2 s, 0.66 s instead of 2.0 s with a 512 cap.

### JPEG decoder: 3-5x faster, and closer to libjpeg
- The integer IDCT of the IJG libjpeg ("islow", 13-bit constants) replaces the float matrix IDCT, with shortcuts for
  columns, rows and whole blocks that hold only their DC term.
- 4:4:4 converts straight from the planes by table (as libjpeg does). 4:2:0, 4:2:2 and 4:4:0 upsample chroma in
  integers with the same bilinear weights as before (1/4, 3/4); other ratios keep the float path.
- A 64-bit bit buffer, refilled once a coefficient; coefficients cleared only where a block set them.
- 1024 textures: 7-16 ms instead of 35-48 ms (Ludo textures 2.9-5.1x faster, a 2048 JPEG 182 ms -> 47 ms).
- Output: on real Ludo textures identical to libjpeg-turbo (the old float decoder was up to 3 levels off it); against
  0.8.2's output, up to 3 levels on some pixels. Tested on 51 files: 4:4:4, 4:2:2, 4:2:0, greyscale, 1x1, odd sizes,
  quality 30-100, restart markers.

### Inflate (PNG and compressed .arct): about 2x faster
- 10-bit lookup tables decode most Huffman symbols in one step (longer codes walk the canonical code), a 64-bit
  bit buffer, memcpy/memset for matches. Was one bit at a time.
- Same output as zlib on 141 streams (stored, fixed and dynamic blocks; every zlib strategy and level; mixed flushes);
  damaged streams fail cleanly. A 1024 PNG decodes in 28 ms instead of 59 ms; a 2048 BC1 .arct in 26 ms instead of 58.

### JPEG decoded small under the texture cap
- With `arc_set_texture_max_size`, a JPEG is decoded at 1/2, 1/4 or 1/8 size (each block's pixels averaged; at 1/8
  only the DC term is used) before colour conversion, then halved as needed: the same size as decoding in full and
  halving. On real textures the pixels differ by a mean of 0.1-0.3 levels (a few pixels by up to 13 at strong edges).
- Faster by 1.7-3.6x for 2048 sources and ~2x at 1/8; little at 1/2 on 4:4:4 textures, where reading the coded data
  dominates.

### Also
- `mesh2arcm.py`'s decode-time estimates use the new rates.
- `ARC_VERSION` 0.8.3 (`ARC_VERSION_NUM` 803); 0.8.2's changes below were never given their own number.
- Not done: D3D11 textures created with all mip levels at once. It needs mips built on the CPU, which may cost more
  than the GPU's `GenerateMips` saves; to measure on a real GPU first.
- Tests: self-test 1983 (9 new: a JPEG and a PNG round trip, capped JPEG sizes); post test 231. Software, GL and D3D11
  (Wine).

## 0.8.2 — 8 Nov 2026: requests from Moonbase Raid and MechBattleArc

### Fixed: web saves shared between games on one site (MechBattleArc's request, 8 Nov 2026)
- `plat_web.c`: localStorage keys are now `"<save_folder>/<key>"` (`save_folder` defaults to the title, as on desktop).
  itch.io serves every HTML5 game from one origin, so ARC games overwrote each other's saves and `arc_audio` volumes,
  and could read another game's `"settings"`.
- A save from an older build (the bare key) is read once and copied to the new key, so players keep their saves. The
  bare key is left alone (it may be another game's). Removing a save that also exists bare leaves a small marker so
  the old copy can't come back. On its first load a game could still pick up another game's bare key of the same
  name, as before; its version check applies.
- Desktop is unchanged. Checked: the key logic against a fake localStorage (12 cases). Not checked: a browser.

### Added: word-wrapped text (MechBattleArc's request, 8 Nov 2026)
- `arc_draw_text_wrapped(x, y, width, scale, rgb, flags, text)` draws a paragraph in a column and returns its height;
  `arc_text_wrapped_height(width, scale, text)` measures without drawing (colour 0 is black in ARC, so it can't mean
  "measure"). `arc_font_draw_wrapped` / `arc_font_wrapped_height` do the same in a baked font.
- Lines break at spaces and `\n`; a word wider than the column is split at a whole UTF-8 character; trailing spaces
  are dropped. Flags: CENTER/RIGHT align each line in the column, MIDDLE/BOTTOM place the block, SHADOW, OUTLINE.
- `arc_ui_text_wrapped(text)`: a paragraph wrapped to the panel's (or column's) content width in the UI font and
  scale, as tall as it needs.
- Fixed with it: the built-in font sampled the edge of the next glyph in its atlas when text sat exactly on a half
  pixel (a bar under some letters). Glyphs are now sampled a hundredth of a texel inside their cell. This can move a
  few pixels of text drawn at a non-integer scale (in the post test: 40-200 pixels of shrunk UI text in two looks);
  games comparing screenshots may see that.

### Added: texture size cap and load-cost report (MechBattleArc's request, 8 Nov 2026)
- `arc_set_texture_max_size(px)` / `arc_texture_max_size()` (and `--texture-max-size N`): mipmapped textures (every
  mesh texture and material map, and `ARC_TEX_MIPMAPS` loads) loaded from then on have their largest side capped;
  0 = no cap (default). 2D textures, fonts and render targets are never capped. Works on the loader threads too.
- Compressed `.arct` with mips skip their bigger levels (free). PNG/JPEG are halved (2 x 2 box) after decoding: this
  saves upload, mip building and GPU memory, but the decode costs the same.
- `tools/mesh2arcm.py` prints each texture's size, format, bytes and estimated decode time in ARC, a total, and a note
  when textures are dense for the model's size (more than ~512 px a unit) with the `--texture-size` to try.
- Background loading already exists (0.7.1): `arc_load_begin(ARC_LOAD_BACKGROUND)`, the normal `arc_mesh_load` /
  `arc_texture_load` calls, `arc_load_end()`, then `arc_mesh_ready` / `arc_texture_ready` or `arc_load_progress`.
  Decoding runs on worker threads on desktop and in time slices on the web.
- Measured (one core, cloud x86): ARC decodes a 1024 JPEG in ~45 ms, PNG ~58 ms, BC1 .arct ~14 ms; libjpeg-turbo
  takes ~5 ms for the same JPEG. See `claude/arc-loading-research.md` in the project.

### Added: `arc_app.draw_top`, 2D over the UI (MechBattleArc's request, 8 Nov 2026)
- An optional callback run after the arc_ui panels are drawn (every frame after `draw()`, and in immediate mode after
  the tick), so a full-screen notice, fade or cursor covers the panels. 2D only; ARC's developer overlays still go on
  top. Panels under a cover still take clicks, so skip arc_ui calls while it shows. Added at the end of `arc_app`.
- Tests: self-test 1974 (21 new: wrapping, the cap); post test 231 (3 new looks: wrapped UI text stays in its panel,
  `draw()`'s 2D goes under a panel and `draw_top`'s covers it). Software, GL and D3D11 (Wine).

### Added: Windows window and taskbar icon from the exe (MechBattleArc's request, 8 Nov 2026)
- `plat_win32.c`: the window class takes icon resource 1 from the exe (`1 ICON "game.ico"` in an `.rc`), at the
  system's large and small icon sizes, so the title bar, taskbar and Alt-Tab show the game's icon, not only
  Explorer. An exe without one keeps the generic icon.
- Game templates (`tools/new_game.py`; existing games get it with `--update-scripts`): `build.bat` compiles every
  `res\*.rc` with `rc` and links it; `build.sh win` and `release` do the same with `windres`. No `res` folder, no
  change.
- Checked with MinGW under Wine: with a resource the class gets a 32 px and a 16 px icon, without it none (generic);
  template games with and without `res/` build and run. Not checked: MSVC `rc` in `build.bat`, a real taskbar.

### Added: post effects and shadow cascades for render targets (Moonbase Raid's request, 8 Nov 2026)
- `arc_target_set_post(target, &post)` gives one render target its own post effects (bloom, tonemap, SSAO, FXAA,
  LUT, vignette and the rest). For 3D drawn into a target that the game then places inside a 2D frame (Moonbase
  Raid's mission view). NULL or all zero = none, as before. Returns false for a texture that is not a render target
  and past 8 targets at once (with one `ARC error` line). Kept until changed or the target is destroyed.
- It works as the screen's post does: every pass into that target draws its 3D into the target's own HDR buffer;
  after the target's last pass of the frame, post runs into the target, then the 2D drawn into the target goes on
  top, sharp. SSAO uses the camera of the last of those passes that drew 3D. Vignette, scanlines and CRT follow the
  target's size. A target with post comes out opaque. Targets are never multisampled, so neither is their post
  (use `fxaa`).
- `arc_target_set_shadow_cascades(target, on)`: `arc_set_shadow_cascades` slices for passes into that target,
  following the camera set while drawing into it (perspective only). Off by default: target passes keep the single
  `arc_set_shadows` box, as before.
- All four renderers. GL, WebGL2 and D3D11 keep the post buffers in sets (the screen's plus one per target), so a
  target and the screen don't resize each other's buffers every frame.
- Usage (Moonbase's `drawMapPass`): once, `arc_target_set_post(map_rt, &p)` with `p.bloom = 0.6f; p.tonemap = true;
  p.ssao = 0.6f; p.fxaa = true;`, and `arc_target_set_shadow_cascades(map_rt, true)` with
  `arc_set_shadow_cascades(2, 30)`; the target is drawn and placed exactly as now.
- Tests: post test 224 checks (7 new looks: the request's emissive cube with bloom, its 2D and the screen around it
  unchanged and the vignette at the target's corners; SSAO in a target; cascades reaching a far box only when turned
  on for the target; the API's limits). Passes in software, GL (X11, MSAA 0 and 4, reversed-Z) and D3D11 (Wine,
  MSAA 0 and 4). Self-test 1953 unchanged.
- Old pictures unchanged: all 34 earlier post-test looks are pixel-identical before and after in software, GL and
  D3D11 (one D3D11 look moves with real time in a window and varies by the same 144 pixels between runs of 0.8.1);
  samples demo, deck and passes identical.
- Not checked here: WebGL2 (no emsdk in this session; the GL code is shared and has no WebGL-only branch for this),
  real GPUs and MSVC. Wine's D3D11 already failed orthographic SSAO under reversed-Z before this change (look 32,
  identical numbers on 0.8.1).

### Fixed: web canvas smaller than the minimum size (MechBattleArc's request, 8 Nov 2026)
- A resizable web app whose page (an itch.io embed, a small browser window) is smaller than `min_width` x `min_height`
  used to clamp each side of the backbuffer separately, so the browser stretched the frame one way: text squashed and,
  with only one side clamped, the mouse off. The backbuffer now keeps the page's shape and scales up uniformly until
  both sides meet the minimum (`k = max(min_w / w, min_h / h)`), capped at 8192 a side.
- One helper (`web_fit_size` in `plat_web.c`) now sizes the canvas at start-up and on every poll; the 64-pixel floor
  when no minimum is set scales uniformly too. Pointer mapping already divided by the CSS size, so it stays exact.
- Desktop and fixed-size web apps are unchanged.
- Checked: the sizing maths natively (big page, hiDPI, small embed, short and tall pages, phone portrait, no minimum).
  Not yet built with emcc or tried in a browser.

## 0.8.1 — 6 Nov 2026: real-time rollback multiplayer

Opt-in: games that do not use the new calls behave exactly as on 0.8.0 (Droids and Moonbase Raid pixel-identical, Evolution within its own noise). Self-test 1953 checks; post test 191 in all four renderers; tests/rollback_test.py over real UDP (Linux, and Windows clients under Wine against a Linux host) and tests/net_test.py.

(`ARC_VERSION` is unchanged; this file is to be merged into CHANGELOG.md.)

## Added: rollback matches for action games (GGPO style)
- `arc_net_desc.mode = ARC_NET_ROLLBACK` (new fields at the end: `mode`, `input_delay`, `max_rollback`; zero = the 0.8.0
  turn-based behaviour). The 0.8 session (host, join, LAN discovery, version check, ready, start, seed) sets the match up;
  from the START entry it runs over UDP (Winsock loaded at run time on Windows, BSD sockets elsewhere; the web build still
  reports `ARC_NET_UNAVAILABLE`). Up to 4 players; clients send to the host, which relays.
- Per-tick input exchange: `arc_app.net_input` (new, at the end) packs this machine's input (up to `ARC_NET_INPUT_MAX` = 64
  bytes) once per new tick, never while re-simulating; without it ARC sends a default input (all keys and pad buttons,
  the pad's axes) read with `arc_net_key_down/pressed(player, k)`, `arc_net_axis(player, a)`. `tick()` reads every
  player's input with `arc_net_input(player, &size)`; `arc_net_input_predicted`, `arc_net_input_dropped`.
  Offline `arc_net_input(0)` is the local input this tick (one code path).
- Input delay (default 2 ticks, 0..10, per machine, changeable at any time: `arc_net_set_input_delay`, `net delay`,
  `--net-delay`); prediction (the last known input repeated); rollback: a snapshot of ARC's whole state per tick (game
  `save_state` + arc_rand, particles, emitters, decals, UI, arc_vars, net view) in a ring of `max_rollback` + 2 (default 8,
  up to 15: `--net-rollback`, `net rollback`), and re-simulation of the ticks since a misprediction before the next tick.
  A game without save_state/load_state plays in lockstep.
- Datagrams repeat every unacknowledged input (redundancy), carry acknowledgements, round-trip timing, frame advantage,
  drops and the latest confirmed state hashes. Stalls: a machine never runs more than max_rollback ticks past its inputs;
  `ARC_NET_EV_WAITING` / `ARC_NET_EV_RESUMED` (after 0.5 s), `arc_net_waiting_for()`, "Waiting for player" in netarena.
- Time sync: frame-advantage estimate per peer (smoothed), the machine that is ahead skips ticks (rounded half-tick
  threshold, at most 8 per second).
- Desync detection: state hashes every 10 confirmed ticks compared between machines; `ARC error desync tick=T ...` and a
  report folder `net_desync/tickT_pP/` (info.json, state.bin at tick T, dump.json, inputs.txt, log.txt).
- Drops: no datagrams for 3 s, a closed link, or a BYE: the host decides the tick, the player's input is zeros from there
  on every remaining machine (`ARC_NET_EV_DROPPED`). Rejoining a running rollback match is not supported.
- Side effects: `arc_play*` deduplicated per (tick, sound, n-th play in the tick): repeats skipped, newly heard sounds
  started late, sounds of ticks that no longer play them stopped; music calls ignored while re-simulating; `arc_event`
  lines held until their tick is confirmed (so the log is the confirmed match). `arc_net_resimulating()`.
- `arc_net_match_tick()`; `arc_rand_*` reseeded from the match seed at match tick 0, where the START event is delivered
  (again after a correction to tick 0).
- Recording: `--record` writes every player's confirmed input ("I tick player hex" / "D player tick" lines in FILE.net);
  `--replay` reproduces the match offline exactly, with seek and the timeline.
- Rollback matches run in real time even headless (the clock paces ticks; the process sleeps between them).
- Stats: `arc_net_stats()` (`arc_net_rb_stats`), `net stats`, `dump` → `net.rollback`, and a line in the FPS overlay.
- Agent tooling: `net mode`, `net delay`, `net rollback`, `net input [P] HEX|off`, `net stop_at T`, `net stats`,
  `net synctest N [PLAYERS]` / `--net-synctest N` (roll back N ticks every tick and compare state hashes: finds what
  save_state misses), `net wait tick|confirmed|waiting|resumed|dropped|alone`; `--net-sim` now also `dup`, `freeze_at`,
  `freeze_ms`, and lag/jitter/loss apply to datagrams (real loss, duplication, reordering).
- `samples/netarena/`: a 2-4 player arena shooter (ships, bullets, pickups, random spawns from the match generator,
  deterministic maths, sounds, particles, events, a bot, offline mode).

## Fixed: ARC's snapshots (seek, rewind, timeline, rollback)
- Replayed `arc_load` results: the read position of each key in the recorded stream is now in ARC's snapshot (a new
  optional "SAV1" block after the net block; older snapshots without it still load and keep the positions), so going
  back and playing on reads the same recorded results again (it used to read further entries or the real disk). The
  bounded list of 256 used entries is replaced by per-key positions (no limit on loads per replay). While recording, a
  live game going back over ticks it recorded now reads those ticks' results from the recording (never the disk again,
  and they are not recorded twice).
- UI panel animation (0.7 `anim`): the animation clock, panel and widget animation, fading ghosts of closed panels and the
  last tick's drawing are in ARC's snapshot (an optional block at the end of the UI part, written while animation is
  on); a state without it starts the animation afresh. A seek no longer shows a ghost of a menu from the future, the
  picture after a seek equals a straight run, and loading a snapshot (rollback) does not restart panel animations.

## Tests
- `tests/test_v081_rollback.c` (in test_core.c): machines in one process over in-process datagrams with simulated lag,
  jitter, loss, duplicates (2 and 3 players, desync, stall + recovery, drop by timeout, time sync), sounds/events once;
  the engine through a synctest (same state as a plain run, particles, sounds, events, default input, input delay,
  reseed; a game with state outside save_state is caught); the two snapshot fixes. Every check was confirmed to fail
  with its feature broken.
- `tests/rollback_test.py`: real UDP between headless processes: lag 0/50/150 with jitter, loss, duplicates; scripted
  inputs; 3 players; desync; a 2 s stall; a killed client; a rollback-heavy sound check; every final state and confirmed
  hash compared and every recording replayed offline. `--client-exe "wine .../netarena_headless.exe"` for Windows clients.

## 0.8.0 — 6 Nov 2026: turn-based LAN multiplayer

Opt-in: games that do not use the new calls behave exactly as on 0.7.2 (Droids and Moonbase Raid pixel-identical, Evolution within its own noise). Self-test 1886 checks; post test 191 in software, OpenGL, Direct3D 11 and WebGL2, with and without MSAA; tests/net_test.py over real sockets on Linux and with Windows (Wine) clients against a Linux host.

Games whose rules run on text commands (their `command()` callback, as agents play them) can now be played by several
people on a LAN, one of them hosting from their own game. Old games are unchanged: nothing runs until a game calls the
new API, and every new `arc_app` field is at the end (zero = as before).

### Multiplayer sessions (`include/arc.h`, "Multiplayer (0.8)"; `src/arc_net.c`; guide: `docs/MULTIPLAYER.md`)
- **Commands are the network message.** `arc_net_command("move 3 5 7")` sends a command; the host numbers it and sends it
  to everyone (itself included); every machine applies every command in sequence order through the game's own
  `command()` at the start of a tick (after input is read, before `tick()` and the tile editor). Inside `command()`,
  `arc_net_command_from()` is the sending player (-1 for commands an agent typed on this machine) and `arc_rand_*()` draws
  from the match's own generator, seeded from the host's seed, whatever `tick()` drew. Offline the same call applies
  locally on the next tick, so single-player code needs no branches. `arc_net_command_ex(text, ARC_NET_FREE)` for chat,
  ready and lobby settings.
- **Alternating turns:** `arc_net_turn_end()` (sequenced), `arc_net_turn_owner()`, `arc_net_turn()`, `arc_net_my_turn()`.
  Player 0 (the host) first, then by index, skipping players who are not connected. The host refuses commands from a
  player whose turn it isn't (the sender gets `ARC_NET_EV_REFUSED` and an `ARC error net refused` line); before the start
  only free commands go through. If the player whose turn it is drops out, the turn waits; the host may end it for them.
- **Sessions:** `arc_net_setup(desc)` (game name, version, port, max players; once in init), `arc_net_host`, `arc_net_join`
  ("host:port"; again while lost = rejoin), `arc_net_leave`, `arc_net_status` / `arc_net_status_name` / `arc_net_message`,
  `arc_net_players` (index, name, connected, ready, in match, local, ping), `arc_net_local_player`, `arc_net_set_name`,
  `arc_net_set_ready`, `arc_net_start` (host; everyone starts at the same sequence point with the host's seed),
  `arc_net_started`, `arc_net_seed`, `arc_net_seq`, `arc_net_is_host`, `arc_net_desc_get`.
- **Events:** `arc_app.net_event` (new, last field but one) and the polled `arc_net_event(&ev)`: START, JOINED, LEFT,
  REJOINED, TURN, READY, NAME are sequenced (same point on every machine; a polling game gets each before any later
  command); CONNECTED, DESYNC, HOST_LOST, REFUSED, VERSION when they happen.
- **Version check:** a different game name, game version string, ARC version or protocol is refused, with the reason on
  both sides ("version mismatch: the host runs X 1.0 on ARC 0.8.0; you have X 0.9 on ARC 0.8.0").
- **Rejoin:** a client that drops reconnects by itself (30 s) or with `arc_net_join`, receives what it missed and
  applies it at once; a restarted game rejoins by name and receives the whole match. If the host still holds the old
  link (it has not noticed the crash yet), the new connection replaces it (the same running game by its token; after the
  start, the same name). `arc_net_catching_up()` is true while a backlog (or a seek) is being applied.
- **Command log:** `arc_net_log_save(path)` / `net log FILE` (text, one command a line), `arc_net_log_count`,
  `arc_net_log_line`; `arc_net_resume(path)`: a host re-applies a saved match and the players rejoin by name.
- **Desync detection:** after every applied command each machine hashes its state (new `arc_app.net_state`, else
  `save_state`'s bytes, else the dump's `game` object; plus ARC's own session view) and sends the hash to the host. On a
  mismatch every machine prints `ARC error desync seq=N tick=T player=P report=DIR` and writes `net_desync/seqN_pP/`
  (`--net-report DIR`): dump.json, log.txt, info.json (every player's hash at seq N), state.bin - all taken at the same
  sequence number on every machine.
- **LAN discovery:** `arc_net_find`, `arc_net_finding`, `arc_net_games` (UDP broadcast on port 47801, plus this machine).
- **Lobby:** `arc_net_lobby(desc)`: a ready-made UI-kit panel (name, host, LAN games, join by address, players with
  ready flags and ping, start, leave; mouse, keyboard, gamepad), returning `ARC_NET_LOBBY_STARTED` / `_BACK`.
- **Transport:** TCP, length-prefixed binary messages with a protocol version, non-blocking sockets polled once a frame on
  the main thread, keepalive pings every second, 10 s timeouts. Windows loads `ws2_32.dll` at run time, so no build
  script, project template or game build line changes (MSVC, MinGW and hot-reload builds link as before). The web build
  compiles and works offline; hosting or joining reports `ARC_NET_UNAVAILABLE` ("browsers cannot accept connections").
- **Simulated network:** `--net-sim lag=MS,jitter=MS,loss=P,drop_at=TICK,rejoin_at=TICK,timeout=MS,seed=N` or
  `arc_net_sim(spec)`: delays and uneven delays on this machine's sends, losses as TCP-like stalls (200 ms doubling;
  six in a row break the link), a pulled and replugged cable; deterministic for a seed. Also `--net-name`,
  `--net-version` (pretend to be another version), `--net-verbose`.
- **Agent commands:** `net host [PORT] | join ADDR | find | games | leave | status | players | name NAME | ready [0|1] |
  start | cmd TEXT | free TEXT | end_turn | log [FILE] | resume FILE | sim SPEC | sync | wait started|turn|turn_number N|
  connected|players N|seq N|desync|status NAME [SECONDS]`. `dump` gains a `net` object (status, local player, seq, turn,
  owner, last hash, players with ping) once the net layer has been used.
- **Snapshots, seek, replays:** ARC's state snapshots include the session view and how far the log was applied, so
  `seek`, `rewind`, `state_save`/`state_load` and the timeline work in a session (commands are re-applied at the ticks
  they were applied on). `--record FILE` also writes `FILE.net`; `--replay FILE` then reproduces a networked match
  offline. Offline commands queued by `tick()` are simply made again while re-simulating.

### Deterministic maths (`src/arc_dmath.c`)
- `arc_sinf`, `arc_cosf`, `arc_tanf`, `arc_asinf`, `arc_acosf`, `arc_atanf`, `arc_atan2f`, `arc_sqrtf`, `arc_hypotf`,
  `arc_expf`, `arc_exp2f`, `arc_logf`, `arc_log2f`, `arc_log10f`, `arc_powf`, `arc_floorf`, `arc_ceilf`, `arc_roundf`,
  `arc_truncf`, `arc_fmodf`, `arc_fabsf`: computed by ARC (no C library maths; IEEE double operations and integer bit work
  only), bit-identical across gcc, MinGW, MSVC x64 and Emscripten. Exact range reduction for any argument (Payne-Hanek).
  On 2 million random arguments each they equal the C library's double-precision result rounded to float (where glibc's
  own float functions are up to 2 ulp off). ARC's existing helpers (`arc_m4_rotate_*`, quaternions, renderers) are not
  switched, so old games draw byte-identically; multiplayer games use these for game state.

### Other
- Hot-reload builds now also take the new code's `net_event`, `net_state` and `draw_loading` after a reload (before,
  `draw_loading` kept pointing into the unloaded library).
- The tile editor's snapshot block reader accepts the net block after it.
- `samples/netturns`: a 2-4 player alternating-turn tactics game on a grid using all of the above (lobby, ARC maths,
  match random numbers, `save_state`, an `ai` command and `autoplay N` for agents, a `cheat` command to make a desync).

### Tests
- `tests/test_v080_net.c` (in the self-test): golden hashes of the maths over fixed sweeps (the same on Linux, Windows
  under Wine and wasm under Node), accuracy against the C library, special values; several machines in one process over
  in-process links with a simulated bad network: joins, unique names, version refusal on both sides, lobby commands,
  start, turns, refusals (locally and by the host), hashes, a drop-out during its turn passed on by the host and skipped,
  its rejoin, a restarted player catching up from nothing (replacing its old link the host had not seen die), resume from a saved log, an injected desync with its reports,
  leaving; offline commands with ARC's snapshots and `rewind`; the lobby clicked through. 1886 checks (149 new).
- `tests/net_test.py`: real sockets, separate headless processes on localhost: basic (lag, jitter, loss), three players
  with a pulled cable, a client killed and restarted, version refusal, refused commands, an injected desync, resume,
  LAN discovery, replay and rewind of a recorded client. Also run with Windows clients (Wine) against a Linux host.
- Post test: a lobby look (`L_LOBBY`) in all four renderers.

## 0.7.2 — 6 Nov 2026

Tools and depth. Everything is opt-in: games that do not use the new calls draw and behave exactly as on 0.7.1 (Droids and Moonbase Raid pixel-identical, Evolution within its own noise). Self-test 1737 checks; post test 185 checks (219 with reversed-Z, comparing each look with the standard picture) in software, OpenGL, Direct3D 11 and WebGL2, with and without MSAA.

Known: in OpenGL at MSAA 0 with reversed-Z (and, as before, with MSAA on), the screen is drawn into ARC's own framebuffer, where 2D text at non-integer scales can sample slightly different glyph-atlas rows than when drawn straight to the window (a few hundred pixels of HUD text).

### Tile level editor and replay timeline

### Developer tools: a tile level editor and a replay timeline

Both opt-in: a game that calls neither behaves and draws exactly as before (Droids and Moonbase Raid pixel-identical to
0.7.1, Evolution within its own run-to-run noise). They are 2D drawing on top of the frame, so all four renderers get
them; the post test has a look for the editor's grid view (software, GL 3.3, D3D11, WebGL2).

### Added
- **Tile level editor** (`arc_editor_*`, new `src/arc_editor.c`): any game with a grid of tiles describes its palette
  (`arc_editor_tile`: id, name, colour, shortcut key, optional icon texture and sub-rect, layer) and hands ARC its own grid
  (`arc_editor_desc`: size, cell pointer, `ARC_EDITOR_CHAR` or `ARC_EDITOR_I32` cells, row stride) with callbacks:
  `changed(x, y, old, new)` for every cell edit (also undo, redo, load), `save` / `load` (the game writes and reads its own
  format), `validate` (a problem shown in the status line, e.g. "deck has no lift"). `arc_editor_begin / end / open /
  is_open / set_playing / playing / get / paint / fill / flood / line / undo / redo / undo_steps / save / load / dirty /
  message / hover`.
  - A 2D top-down view over the game (pan: middle drag or SPACE + drag, arrows; zoom: wheel at the pointer; HOME fits),
    or with `world_cell` + `camera` a grid over the game's own 3D view (V), the cell under the pointer picked by a ray
    from the game's camera against the ground plane. Palette panel with tools and actions, status line (tool, tile, the
    cell under the pointer, undo / redo counts, zoom, the validate message).
  - Tools: brush (B, strokes are one undo step), line (L), rectangle (R, SHIFT = outline), flood fill (F), eyedropper (I or
    ALT + click); left button = the selected tile, right = the erase tile. Tiles by their own key or [ ]. CTRL+Z / CTRL+Y
    (CTRL+SHIFT+Z) undo / redo from a bounded ring of cell edits (`undo_cells`, default 4096 or twice the grid; a step that
    doesn't fit is kept out whole, never half-undone), CTRL+S save, CTRL+O load (revert), G grid lines.
  - F2 opens it (with dev keys, or `any_build`). It runs inside the tick from the tick's input, like the UI kit, so every
    edit is in recordings and replays and `seek` redoes it; its state (open, tool, view, undo ring) is part of state
    snapshots. While open the game's tick() is paused until P ("play from here"); draw() goes on. save is never called by a
    replay or a silent re-simulation.
  - Agent commands: `editor on|off`, `editor paint X Y CH`, `editor fill X0 Y0 X1 Y1 CH`, `editor line ...`,
    `editor flood X Y CH`, `editor undo|redo|save|load`, `editor dump` (`ARC editor row NN ...` lines), `editor tile CH`,
    `editor tool NAME`, `editor play|pause`, `editor info`; CH is a character, `=N` or a tile name. `dump` has an `editor`
    object once an editor has begun.
- **Replay timeline** (`arc_timeline_*`): a bar over a live game or a replay (`--replay`, e.g. a play session's
  `input.rec`, whose `events.jsonl` markers and F8 bookmarks it shows; this run's `arc_event`s are added too) with the tick
  and length, play / pause, a tick back / forward, speed 0.25x..8x, start / end buttons, and click or drag to seek. F9 (dev
  keys, or any replay), `--timeline`, `arc_timeline_show`. Keys while open: SPACE, LEFT / RIGHT (, .), - =, HOME, END (a
  live game keeps them while it plays). Clicks on the bar never reach the game. `arc_timeline_seek / show / shown /
  set_paused / paused / set_speed / length / mark_count / mark`. Commands: `timeline on|off|seek T|step [N]|back [N]|play|
  pause|speed X|marks|info` (also `scrub`). Display only: the bar reads the platform's raw input once a frame; seeking goes
  through the snapshots and silent re-simulation, so a state reached by seeking is the same as one played straight to
  (self-tested tick by tick). Going forward re-simulates without drawing.
- `samples/tiles`: a small tile game (a scratch copy of Droids' Cargo Hold deck) with the editor in 2D and 3D, saving to a
  text file, and save_state / load_state so the timeline can go back.

### Changed
- Going back (`seek`, `rewind`, the timeline) is allowed while recording (`--record`, play sessions): the inputs already
  recorded are replayed up to the newest tick and only then does live input continue, so the recording stays exact; the
  session's events.jsonl and bookmarks are not written twice. A live game gone back with the timeline does the same.
- A replay sought backwards and then played kept only one tick of its input history (the next ticks read the replay
  file from the wrong place); the history is now kept whole.
- A replay whose timeline has been used stays open (paused) at its end instead of quitting.
- State snapshots carry the editor's block after the decals' (older snapshots still load).

### Reversed-Z depth and the WebGPU spike

Opt-in: games that leave `arc_app.depth_mode` at 0 draw exactly as on 0.7.1 (Droids and Moonbase Raid pixel-identical,
Evolution within its own run-to-run noise). Self-test 1663 checks (43 new); post test 165 checks in software, OpenGL,
Direct3D 11 and WebGL2 with and without MSAA, and 199 checks with `--reversed-z` (and `--depth-mode infinite`), which
also compares every look with the standard run's picture.

### Added
- **Reversed-Z depth** (`arc_app.depth_mode`): `ARC_DEPTH_STANDARD` (0, as before), `ARC_DEPTH_REVERSED` (near 1, far 0
  in a 32-bit float depth buffer), `ARC_DEPTH_REVERSED_INFINITE` (the same with no far clip plane for perspective
  cameras; `zfar` still bounds `arc_sphere_visible` / `arc_aabb_visible`). Chosen once at start (the depth buffers are
  made for it); `--depth-mode standard|reversed|infinite` or `--reversed-z` override it. `arc_depth_mode()` says which
  is in effect, `arc_depth_precise()` whether this GPU gives the full gain (float depth and a 0..1 clip range).
  Standard depth can't keep two surfaces 1 cm apart at 2 km with `znear` 0.05 (they z-fight); reversed can (post test
  look `L_ZFAR`: green share of the nearer of two tilted quads 0.01 / 0.1 / 1 / 10 apart at 2000 units: standard
  1-46% / 0-65% / 7-80% / 100% across the renderers, reversed 100% everywhere with a 0..1 clip range).
  - Software renderer: depth holds clip z / w in both modes (1..0 reversed), compares and clears swapped, near-plane
    clipping at z = w, the line depth slack scaled to match.
  - OpenGL 3.3: `GL_DEPTH_COMPONENT32F` for every depth buffer ARC makes (render targets, MSAA, post scene, soft-
    particle copy); the screen is always drawn into ARC's own framebuffer (the window's depth is 24-bit) and blitted;
    `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` when GL 4.5 or `GL_ARB_clip_control` is there.
  - WebGL2: `DEPTH_COMPONENT32F` likewise and `EXT_clip_control` when the browser has it (Chrome, Safari; partly
    Firefox); with reversed-Z the canvas is not antialiased by the browser: ARC multisamples its own screen.
  - Direct3D 11: `DXGI_FORMAT_D32_FLOAT` (`R32_TYPELESS` + `R32_FLOAT` views where the depth is read), clear 0,
    swapped compare states; D3D's 0..1 clip range needs no fix-up.
  - Without a 0..1 clip range (GL < 4.5 without the extension, WebGL2 without `EXT_clip_control`, D3D11 under Wine)
    ARC still draws reversed with float depth (the vertex shader writes z * 2 - w); the gain is about 10x instead of
    1000x (resolves 1 unit at 2000 instead of 10) and ARC logs it. `--no-clip-control` forces this path on GL/WebGL2.
  - Everything that touches depth follows: per-batch compares (`ARC__Z_LESS` / `GREATER` swapped: `ARC_DEPTH_HIDDEN`,
    x-ray, `ON_TOP`, `TEST_ONLY`), soft particles (the inverse view-projection includes the 0..1 step, so every
    renderer's reconstruction is unchanged), SSAO (empty depth = 0, orthographic depth test flipped), the sky (built
    from the standard matrix), render targets (cleared to 0). `depth_pull` and the line depth push move points in the
    world, so they need no change. Shadow maps stay standard (their own matrices, LESS, cleared to 1).
  - `arc_view_proj`, `arc_camera_view_proj`, `arc_world_to_screen`, `arc_screen_ray`, billboards, cells and LOD keep
    the standard matrix in every mode.

### Tests
- `tests/test_v072_depth.c` (self-test, software renderer, switching modes between render-target passes with the
  internal `arc__depth_mode_set`): precision far away, every depth feature drawing the same picture in all three modes
  and still working, geometry beyond `zfar` only with no far plane, the public matrices staying standard.
- `tests/test_post.c`: looks `L_ZFAR` (precision), `L_OSKY` (sky under an orthographic camera), `L_OPLAIN` / `L_OSSAO`
  (SSAO under an orthographic camera); with `--reversed-z` / `--depth-mode infinite` the pictures are saved as
  `arc_post_rz_N.png` / `arc_post_inf_N.png` and compared with `arc_post_N.png` from a standard run in the same folder
  (at most 0.2% of pixels more than 24 apart; the x-ray look 2%, its scan lines move with time in a window).

### Experiments
- `experiments/webgpu/`: a WebGPU spike, not part of the engine (see its `REPORT.md`): ARC's meshes and 144-byte
  instances drawn with raw `navigator.gpu` through EM_JS and a WGSL port of the basic mesh shader, reversed-Z
  `depth32float`; matches ARC's software renderer (mean 0.33/255). Estimate for a full backend: about 1,900 lines.
  Recommendation: later.

## 0.7.1 — 6 Nov 2026

Speed. Everything is opt-in: games that do not use the new calls draw and behave exactly as on 0.7.0 (Droids and Moonbase Raid pixel-identical, Evolution within its own noise). Self-test 1620 checks; post test 153 checks in software, OpenGL, Direct3D 11 and WebGL2, with and without MSAA, and with compressed textures on the GPU and decoded on the CPU.

### Scene speed: cells, LOD, static batches, frame budget

All opt-in: a game that calls none of the new functions draws exactly as before (Droids, Moonbase Raid: pixel-identical
to ARC 0.7.0; Evolution: within its own run-to-run noise). Everything decides what is *queued*, so all four renderers
(software, GL 3.3, D3D11, WebGL2) get it with no shader changes.

### Added
- **View tests**: `arc_sphere_visible(center, radius)`, `arc_aabb_visible(lo, hi)` against the current camera.
- **Cells and portals** (room visibility): `arc_cell_add(id, lo, hi)` (several boxes per cell), `arc_portal_add(a, b, lo, hi)`,
  `arc_portal_set_open` / `arc_portal_is_open`, `arc_cells_clear`, `arc_cell_at(p)`, `arc_cell_visible(id)`,
  `arc_cells_visible_count`, `arc_cells_compute(camera, w, h, bits)` (tick-safe), `arc_set_draw_cell(id)` /
  `arc_get_draw_cell`, `arc_cells_set_culling(on)`, `arc_cells_debug_draw()`. From the camera's cell ARC walks through
  open portals, narrowing the view to each portal's screen rectangle (near-plane clipped); a camera outside every cell
  (top-down) sees each cell whose box is in view. Meshes drawn after `arc_set_draw_cell(id)` are not queued while that
  cell is hidden (resets to 0 each frame; particles, decals, trails and billboards are never culled). Cached per camera.
  Commands: `cells`, `cells on|off`, `cells draw` (wireframe overlay).
- **Levels of detail**: `arc_mesh_set_lod(base, level, lod, screen_size)` (fraction of the view's height covered by the
  instance's bounding sphere) and `arc_mesh_set_lod_distance(base, level, lod, distance)`, levels 1..3;
  `arc_set_lod_bias(bias)` (quality setting), `arc_set_lod_force(level)`, `arc_mesh_lod_level(base, model)`. Picked per
  instance when queued, so shadows use the same level. Commands: `lod`, `lod bias X`, `lod force N`.
- **Static batches**: `arc_static_create / add / add_cell (-1 = from its centre) / set_chunk / build / draw / clear /
  destroy / mesh_count / item_count`. Merges static draws into one mesh per texture + material (+ cell, + chunk), the
  tint baked into vertex colours, mirrored matrices handled, materials/textures shared (not owned), shadow casting kept.
  `arc_static_draw` skips chunks outside the view (not while shadows are on).
- **Frame budget**: `arc_get_frame_budget()`, `arc_set_frame_budget(ms)` (default 16.67). Parts: tick, draw (queue
  build), sort, submit, present (GPU/vsync wait), audio (audio tick + mixer), other, plus the game's ARC_PROF zones;
  smoothed. Shown as bars under the FPS overlay graph (red: a part alone over budget; amber: the biggest part of an
  over-budget frame), printed by `stats` (`ARC scene`, `ARC budget`, `ARC budget_zone` lines), and in `dump` as `budget`.
- `arc_gfx_stats` gains `culled`, `lod_instances[4]`, `cells_visible`, `static_draws` (at the end; dumped when non-zero).
- `samples/deck/deck.c`: a 40-room Droids-like deck (4,000 static tiles and wall blocks, 200 droids with 3 LODs), every
  feature switchable with `--set deck.cells|static|lod|view|doors|shadows|chunk`; doubles as the benchmark.

### Measured (deck sample, 960x540, p50 ms per frame; GL = llvmpipe under Xvfb, only indicative)
| view | none | cells | static | lod | static+cells | all |
|---|---|---|---|---|---|---|
| first person, soft | 131.0 | 79.7 | 117.9 | 108.1 | 69.6 | 65.6 |
| top-down, soft | 139.4 | 115.9 | 131.7 | 122.5 | 110.2 | 99.3 |
| overview, soft | 94.4 | 90.8 | 88.1 | 58.1 | 87.8 | 54.2 |
| first person, GL | 76.6 | 32.9 | 79.0 | 44.8 | 32.0 | 22.1 |
| top-down, GL | 58.8 | 38.4 | 58.5 | 35.9 | 36.9 | 27.1 |
| overview, GL | 77.8 | 78.1 | 120.3 | 52.4 | 121.8 | 49.9 |

Instances queued per frame: 4,200 without static batching, 202 with it (77 with cells in first person). Draw calls were
already 5 without batching, because ARC instances every mesh; with static+cells they become ~24 (one per visible room
and group). Static batching's gain in ARC is the per-instance work (queue, sort, 144-byte instance uploads), not draw calls.

### Loading and compressed textures

Both are opt-in: a game that does not call `arc_load_begin` and has no `.arct` / version 4 `.arcm` files loads, draws and
behaves exactly as before.

## Background asset loading
- `arc_load_begin(flags)` / `arc_load_end()`: between them `arc_texture_load(_ex)`, `arc_mesh_load`, `arc_sound_load`
  and `arc_music_load` queue the file and return the handle at once ({0} if the file is missing, as before). Handles are
  reserved in queue order, so they are the same in every run.
- Desktop: worker threads (pthreads on Linux, Win32 threads with SRW locks and condition variables on Windows, behind a
  small `arc__threads` table in the platform layer; `--load-threads N`, default CPUs - 1, 1..4) read and decode
  (PNG, JPEG, QOI, `.arct`, `.arcm`, MP3/QOA/WAV sounds); the main thread uploads in queue order within a time budget
  per frame (`arc_load_set_budget`, 4 ms default, at least one file a frame). Web: no threads (no pthread flags): the
  main thread decodes and uploads a slice per frame within the same budget. Packs are looked in first, as before.
- While a group with flags 0 loads, ticks wait: `tick()` is not called and `arc_app.draw_loading(progress)` (new field,
  at the end of `arc_app`) or ARC's own `arc_draw_loading_screen` is drawn instead of `draw()`. The first tick therefore
  sees everything loaded, so ticks, dumps and replays do not depend on load time. Loading-screen frames are capped at
  60 a second on desktop (without vsync an uncapped screen took the CPU from the loader threads). `ARC_LOAD_BACKGROUND` groups load
  while the game runs (tick logic must not depend on when they finish).
- Headless (agents, tests) is deterministic: `arc_load_end` finishes the group before returning, in queue order on the
  main thread. `--load-items N` instead finishes N files a frame (headless too, still deterministic).
- `arc_load_done`, `arc_load_progress` (by bytes), `arc_load_status` (counts, bytes, failures, seconds),
  `arc_load_wait`, `arc_load_on_done`, `arc_texture_ready`, `arc_mesh_ready`, `arc_sound_ready`, `arc_music_ready`.
- Handles used before they are ready are safe: a texture is a 1x1 transparent stand-in reporting 0 x 0, a mesh has no
  vertices (draws nothing, zero bounds and triangles), a sound gives no voice (the audio variation random stream still
  advances, so it stays the same), `arc_music_play` / `_stop` / `_set_volume` are remembered and applied when the music
  is ready. `arc_mesh_set_texture` / `_material` / `_double_sided` / `_cast_shadow` and `arc_texture_set_smooth` made
  meanwhile are kept, as if made after loading. Destroying a handle before it is ready drops the load (also when the
  slot has been reused). A load that fails logs as before and leaves an empty handle.
- Hot reload (`--hot`) watches queued files too; `.arct` files hot-reload (also switching between PNG and `.arct`).

## GPU-compressed textures
- New `.arct` container (see `src/arc_texc.c`): BC1, BC3, BC4, BC5, BC7 with mip levels, payload optionally zlib.
  `.arcm` version 4 = version 3 whose texture and material maps may be `.arct` (older ARC refuses it with "unknown
  version"); version 2 files may also carry `.arct` images (kept at version 2 by the tools). Old files load unchanged.
- Loading: `arc_texture_load(_ex)`, `arc_mesh_load`, the new `arc_texture_from_memory`, packs and hot reload take them.
  The blocks go to the GPU as they are where the GPU supports the format (D3D11: BC1/3/4/5/7 checked with
  CheckFormatSupport; desktop GL: `GL_EXT_texture_compression_s3tc`, RGTC (core in GL 3), `GL_ARB_texture_compression_bptc`
  or GL 4.2; WebGL2: `WEBGL_compressed_texture_s3tc`, `EXT_texture_compression_rgtc`, `EXT_texture_compression_bptc`,
  enabled by name) and the first level's sides are multiples of 4; otherwise, always in the software renderer and with
  `--no-compressed-textures` (or env `ARC_NO_COMPRESSED_TEXTURES`), ARC decodes them to RGBA8 at load with its own C
  decoders (all eight BC7 modes). The software renderer samples the file's own mip levels.
- BC5 normal maps: ARC rebuilds Z (blue) on the CPU path; on the GPU path the mesh shaders of all GPU renderers rebuild
  it when the material's normal map is BC5 (a flag in the material constants). Uncompressed textures are unchanged.
- `arc_texture_format`, `arc_texture_format_supported`, `ARC_TEXFMT_*`; `arc_gfx_stats` gains `textures`,
  `textures_compressed`, `texture_bytes` (appended). `arc_texture_update` on a compressed texture turns it into RGBA8.
- Tools: `tools/texcompress.py` (numpy): PCA endpoints + least-squares refits + a +-1 endpoint search for BC1/BC3,
  both BC4 modes for alpha and BC5, BC7 mode 6; mips box-filtered before compressing (normals renormalised); `--arcm`
  / `--arcm-batch` recompress existing meshes, `--report` compares with a naive encoder. `tools/mesh2arcm.py --compress`
  compresses straight from the source images.
- New flags: `--no-compressed-textures`, `--load-threads N`, `--load-items N` (in `--help` and AGENTS.md).

## Tests
- `tests/test_v071_load.c` (in the self-test): hand-made BC1/BC3/BC4/BC5/BC7 blocks decoded bit-exactly, `.arct`
  parsing (zlib, mips, bad files), a version 4 mesh with a BC1 texture and BC5 normal map, version 1/2/3 and `.axm`
  files, the queue (progress by bytes, holding ticks, handles before ready, settings kept, failures, destroy before
  ready, packs, music started when ready, same data as loading at once), hot reload, the loading screen.
- `tests/test_post.c` look `L_CTEX`: compressed and RGBA8 versions of the same textures drawn side by side plus a lit
  BC5 normal map; run in all four renderers, with and without `--no-compressed-textures`.

## 0.7.0 — 5 Nov 2026

Effects, audio and UI. Everything is opt-in: games that do not use the new calls and fields draw, sound and behave exactly as on 0.6.7 (Droids and Moonbase Raid pixel-identical, Evolution within its own noise). Self-test 1497 checks; post test 85 checks in software, OpenGL, Direct3D 11 and WebGL2, each with and without MSAA.

### Particles, trails and decals

**Flipbook particles.** `arc_fx` gains `frames_x`, `frames_y` (the texture is a sheet of frames, read left to right,
top down), `fps` (frames per second, looping; 0 = the sheet plays once over each particle's life) and `frame_random`
(each particle starts on a random frame, from the particles' own random stream). Each flipbook effect gets a quad of
its own covering one frame; the frame is chosen per particle by the instance's texture offset (the 0.6 scroll
fields), so the four renderers needed no change.

```c
static arc_fx SMOKE = { .life = 1.5f, .size = 0.6f, .frames_x = 4, .frames_y = 4, .soft = 0.3f };   /* .texture = the sheet, set in init */
```

**Soft particles.** `arc_fx.soft` (world units): the particle fades out where it comes within that distance of what
is drawn behind it, so smoke and dust have no hard line where they cut the floor or a wall. Before the see-through
batches of a pass that holds a soft particle, the depth drawn so far is made readable: the software renderer reads
its depth buffer; GL and WebGL2 blit it into a depth texture (the framebuffers ARC makes are DEPTH_COMPONENT24; for
the window's own depth the formats are tried once and the one that works is kept: DEPTH_COMPONENT24 under GLX,
DEPTH24_STENCIL8 in Chromium's WebGL2); Direct3D 11 copies it (sample 0 when multisampled) into an R32F texture, so
the window, multisampled, post-processing and render-target depth buffers are now all created readable (same depth
format, typeless storage). Each soft pixel's alpha is multiplied by `clamp(|S - P| / soft, 0, 1)`, P = the pixel's
world point, S = the depth buffer's point at that pixel through the inverse view-projection (perspective and
orthographic alike). No soft particle in a pass: no copy, nothing changes. If a GL driver can't copy the window's
depth, one `ARC log` line says so and particles are drawn hard.

**Sub-emitters.** `on_death` / `on_death_count`: burst another effect where each particle ends (along its last
velocity); `on_bounce` / `on_bounce_count`: where it bounces off the floor plane or a solid faster than 1 unit/s
(along the surface normal). Count 0 = the sub-effect's own `burst`. Resolved when the effect is first used (an effect
may name itself); bursts fire after the tick's move, at most 4096 a tick.

**Solid collisions.** `arc_fx.solid(x, y, z, user)` with `solid_user`: the game's own test (tiles, walls, a height
map). A particle that moves into a solid has the move undone on each axis that went in, and bounces on it keeping
`bounce` of its speed (the other axes slow as on the floor); with `bounce` 0 it ends there and fires `on_death`. One
call per particle per tick, three more on a hit.

**Trails.** A camera-facing ribbon through the last positions of a moving thing. The game owns an `arc_trail` (part
of its state; no allocation): `arc_trail_push(&t, pos)` every tick (ages the points by `arc_dt()`, drops those older
than `t.life`, adds the new one; `t.min_dist` moves the head instead of adding a point when it has hardly moved),
`arc_trail_age(&t)` when there is nothing to add, `arc_trail_clear(&t)` on teleports, and
`arc_trail_draw(&t, &style)` in draw(). `arc_trail_style`: `width`/`width_end`, `color`/`color_end`,
`alpha`/`alpha_end` (head to tail by age; default 1 to 0), `glow`, `additive`, `texture` (u along, v across; {0} = soft
edges). Up to 64 points (`ARC_TRAIL_MAX`), 256 trails drawn a frame; drawn as unlit see-through meshes from a pool
made once.

```c
arc_trail_push(&G.rocket_trail, G.rocket.pos);                                  /* tick */
arc_trail_draw(&G.rocket_trail, &(arc_trail_style){ .width = 0.3f, .color = 0xFFA040, .additive = true });   /* draw */
```

**Decals.** Flat marks laid on floors and walls (scorch, blast, oil, footprints):
`arc_decal_add(tex, pos, normal, size, angle, argb, life)` and `arc_decal_add_ex(..., w, h, ..., flags)`
(`ARC_DECAL_ADDITIVE` for glowing marks). A fixed pool of `ARC_MAX_DECALS` (256): an expired slot is reused first,
else the oldest is replaced. They fade out over the last quarter of `life` (0 = until replaced), optionally with
camera distance (`arc_decal_fade_distance(start, end)`), and are lit like the surface. `arc_draw_decals()` in draw()
draws them as quads on the surface with the 0.6.3 depth path (`ARC_DEPTH_TEST_ONLY` + `depth_pull` 0.002, first in the
see-through layer), so they never z-fight. `arc_decal_count()`, `arc_decals_clear()`. Decals are engine state like
particles: state saves, `seek` and `rewind` carry them (written only once a game has used decals, so older states and
games without decals are unchanged).

- Opt-in throughout: an `arc_fx` with the new fields zero, and games that never call the new functions, run and draw
  exactly as before. Moonbase Raid identical to the 0.6.2 backup; Droids (game, gallery, duel, map, pause, title)
  pixel-identical with 0.6.7; Evolution within its own run-to-run noise.
- Tests: `tests/test_v07_fx.c` (self-test, 1333 checks): exact flipbook frames by age (fps and play-once, a 4 x 1 and
  a 2 x 2 sheet), the soft fade (0.1 / 0.4 of white over black), a decal on a wall with no z-fighting, the oldest
  decal replaced and expired slots reused first, decal lifetimes and state round trip, trail ageing, capacity,
  min_dist and its fade along the ribbon, sub-emitters on death and bounce (and an effect naming itself), solid
  bounces and stops. Post test (85 checks) adds five looks: flipbook (random start frames, each quad one whole
  frame), soft particles against the window's depth, the post-processing buffer's and a render target's (a soft one
  fades near a wall, a hard one does not, a soft one with only sky behind is untouched), decals on a floor and a wall,
  a trail fading from head to tail. Each new check was seen to fail with its feature broken.
- Renderers checked: software, GL 3.3 (Xvfb), Direct3D 11 (Wine) and WebGL2 (headless Chromium), each with
  `--msaa 0` and `--msaa 4`.

### Sound and music

**Game audio that does not sound mechanical, and music that follows the game.** All of it is opt-in: with nothing
set, the mixer's output is bit-for-bit what 0.6.7 produced (checked by mixing a fixed scene of plays, 3D voices,
filters, reverb, buses, streams, QOA and MP3 music and 40 voices over the 32-voice pool with both builds).

- Variation: `arc_sound_set_variation(s, semitones, volume_range)` gives every play of a sound a random pitch
  (+-semitones) and volume (down by up to volume_range). Drawn from the audio system's own generator
  (`arc_audio_seed`, default seed 1), never `arc_rand_*`, so the game's random stream and replays are untouched.
  `arc_audio_random(lo, hi)` gives the same generator to games for per-play variety of their own.
- Voice limits and priorities: `arc_sound_set_limit(s, max, steal)` and `arc_bus_set_limit(bus, max, steal)` cap how
  many voices play at once; a play over the cap takes the oldest (`ARC_STEAL_OLDEST`) or quietest
  (`ARC_STEAL_QUIETEST`) of them, or is refused (`ARC_STEAL_NONE`; the voice handle is stale).
  `arc_sound_set_priority(s, p)`: a play only takes voices of its own or lower priority, lowest first, for caps and
  when all 32 voices are busy (which otherwise works as before: the oldest one-shot, then the oldest loop).
- Ducking: `arc_bus_duck(bus, trigger_mask, db, attack, release)` lowers a bus by db while a voice, stream or music
  on any trigger bus plays (music under speech or explosions); straight lines in dB over attack / release seconds.
  `arc_bus_ducked(bus)` reports the current dB.
- Occlusion: `arc_set_occlusion(fn, user)` asks the game how blocked a positional voice is (0..1, from the sound to
  the listener) when it starts and after every tick; `arc_voice_set_occlusion(v, amount)` sets it by hand. Heard as
  a gain and a two-pole low-pass (fully occluded: 0.4 and 800 Hz, `arc_set_occlusion_filter`) that glide over 50 ms;
  at 0 the voice is untouched.
- Layered music: `arc_music_link(stem, lead)` makes tracks of one piece play, pause, stop and loop with their lead,
  started by one mixer command so they stay locked to the sample. `arc_music_fade(m, level, seconds)` glides a
  track's fade level; `arc_music_set_intensity(lead, 0..1, seconds, when)` fades stems in one after another.
- Beat sync: `arc_music_set_tempo(m, bpm, beats_per_bar, first_beat)`; `arc_music_switch(from, to, loop, when, fade)`
  starts `to` (and its stems) on the next beat or bar of `from` (`ARC_SYNC_NOW / BEAT / BAR`) with a crossfade and
  stops `from` when faded out; `arc_music_stinger(clock, sound, volume, when)` plays a one-shot on the music bus that
  starts exactly on the next beat or bar; intensity changes can wait for one too. `arc_music_position(m)` and
  `arc_music_beat(m)` tell the game where the music is.
- Self-test: `tests/test_v07_audio.c` (43 checks on rendered samples: variation spread and reproducibility, refusal
  and stealing over limits, priority, 6 dB after a 10 ms attack and back after release, occluded noise quieter and
  with far less high-frequency energy, a glide with no step over 0.001, stems sample-aligned over loops, odd chunk
  sizes and pause, a switch landing on frame 200 of a 100-frame beat, a stinger starting on its beat and bar).
  Each check was seen to fail with its feature broken. Self-test 1339 checks.

### UI and UX

**Keyboard and gamepad navigation, easing and UI animation, accessibility, rich text and string tables.** All
opt-in: without the new calls, games look and behave exactly as before (Droids, Moonbase Raid unchanged; Evolution
within its own noise).

- **Navigation.** `arc_ui_set_nav(true)` shows the focus ring from the start and moves it by layout: arrows, the
  d-pad and the left stick go to the nearest widget in that direction (so 2-column grids work), held directions
  repeat (after 0.4 s, then 10 a second), UP/DOWN wrap at the ends, TAB / SHIFT+TAB step in layout order. A focused
  slider or select keeps LEFT/RIGHT (and the stick, with repeat). ENTER/SPACE/PAD_A press, as in 0.6; moving the
  mouse hides the ring until the next direction. Off, the 0.6 keys work as they always have.
  `arc_ui_back_pressed()`: ESC or PAD_B went down this tick and no text field took it. `arc_ui_focused()`: the label
  with the ring last tick. All of it reads ARC's recorded input, so replays and `rewind` match; the navigation state
  is in state snapshots (a 0.7 block after the 0.6 one; older state files still load).
- **Easing.** `arc_ease_in/out/inout_quad`, `_cubic`, `_back`, `arc_ease_out_elastic`, `arc_ease_out_bounce`,
  `arc_ease_inout_sine`, `arc_ease(kind, t)` by number and `arc_tween(from, to, t, kind)`. t is clamped; every curve
  gives 0 at 0 and 1 at 1.
- **UI animation.** `arc_ui_theme.anim` (new last field; 0 = none): panels scale and fade in when they appear and out
  when they stop being drawn (from a copy of their last drawing), widgets fade between their colours on hover and
  sink a little while pressed. Cosmetic: click rects never move.
- **Screen transitions.** `arc_transition(ARC_TRANSITION_FADE / WIPE_LEFT / WIPE_DOWN, seconds, rgb)` covers the
  screen, holds for one tick (`arc_transition_covered()`: swap the level now), then uncovers. Drawn on top of the UI,
  driven by ticks. `arc_transition_active()`, `arc_transition_amount()`.
- **UI scale.** `arc_ui_set_scale(s)` multiplies every widget's size, padding and text (built-in or loaded font) on
  top of `arc_dpi_scale()`; `arc_ui_scale()`.
- **Colour blindness.** `arc_palette_cb(i)`: the Okabe-Ito colours. `arc_cvd_simulate(rgb, mode)` (Machado 2009) and
  `arc_cvd_daltonize(rgb, mode)` for ARC_CVD_PROTAN / DEUTAN / TRITAN; `arc_set_cvd(mode)` + `arc_cvd(rgb)` to wrap a
  game's key colours once and let an options menu pick; `arc_cvd_lut(mode)` builds a colour-grading LUT that
  daltonizes the whole picture through `arc_post.lut` (no renderer changes).
- **Captions.** `arc_caption(text, seconds, rgb)` queues subtitle lines, shown one after another centred near the
  bottom on a backdrop, in the UI font and scale, with markup; `arc_caption_current()`, `arc_caption_clear()`,
  `arc_caption_style(scale, backdrop)`.
- **Rich text.** `[c=FF8000]..[/c]` colours (nesting), `[icon=name]` (registered with `arc_rich_icon`, a texture or
  part of one, one line tall), `[key=F]` keycaps and `[key=PAD_A]`.. coloured pad buttons, `[[` for a bracket.
  `arc_rich_draw / arc_rich_width / arc_rich_height` (built-in font or a loaded one), the `ARC_TEXT_RICH` flag for
  `arc_draw_text_ex` and `arc_font_draw_aligned`, `arc_rich_strip`. In the UI, `arc_ui_set_rich(true)`: widget text
  is measured and fitted with its markup (too long even shrunk: cut as plain text), and `ui` lists labels without
  it. Plain text, and markup when not asked for, draw exactly as before.
- **Localisation.** `arc_tr_load(lang, path)` (from packs first, like every loader) / `arc_tr_load_memory`: UTF-8
  `key = text` lines, `#` comments, `\n` escapes; loading a language again adds to it. `arc_tr(key)`: the active
  language, then the fallback (the first loaded unless `arc_tr_set_fallback`), then the key. `arc_trf(key, ...)`
  formats the text into the frame arena. `arc_tr_set_language`, `arc_tr_language`, `arc_tr_has`,
  `arc_tr_languages`, `arc_tr_clear`.
- Tests: `tests/test_v07_ui.c` (self-test 1417 checks): navigation in a 2-column grid by keys, stick edges, held
  d-pad repeat, ENTER and PAD_A, back, the 0.6 behaviour with it off; scale doubling positions and text; panel open
  and close animation; easing endpoints and shapes; caption timing and colour; transition covered on exactly one
  tick; colour-blind remaps separating confused pairs; string-table fallback chain; rich text widths and pixels
  (colour runs, keycap, icon, right-aligned) read back from a software render target, and plain text left alone.
  Each check fails with its feature disabled. Post test unchanged (51 checks) in software, GL and D3D11.

## 0.6.7 — 5 Nov 2026

**Mouse look.** `arc_mouse_capture(bool)` hides the pointer and holds it in the window, and `arc_mouse_delta()` keeps
reporting movement at any edge (`arc_mouse()` becomes an unbounded virtual position, so replays and the agent
`mouse X Y` command work as before). `arc_mouse_captured()` says whether it is in effect.

- Windows: Raw Input, the cursor hidden and clipped to the window; measured from the window centre when no raw input
  arrives (some remote desktops and drivers). Let go when the window loses the focus, taken back when it returns.
- Linux: the pointer grabbed into the window with a blank cursor and warped back to the centre after each move;
  checked under Xvfb (moves of 14 window widths reported in full).
- Web: the browser's pointer lock (asked for at once and again on each click while wanted; Esc lets go).
- Headless: captured as asked. Self-test 1296 checks.

Droids uses it for first-person mouse look.

## 0.6.6 — 5 Nov 2026

**Long agent commands are no longer cut short** (MechBattleArc's request: its 18-word `loadout` command lost its last
values without a word).

- A command may have up to 64 words (was 16) and 4,095 characters (was 1,023, cut silently when the script was
  split). A longer one is not run, and says so: `ARC error command has more than 64 words, not run: ...` or
  `... longer than 4095 characters ...`.
- `--stdin` reads lines of up to 16 KB.
- Self-test: a 21-word game command arrives whole (1294 checks; `build.sh` and `build.bat test` send it).

## 0.6.5 — 4 Nov 2026

**Plain fills no longer break 2D batches.** Untextured 2D drawing (rectangles, lines, circles, untextured
`arc_draw_triangles2d`) now joins whatever batch is open, and a batch holding only plain shapes so far takes the
next texture. Only a change of texture between textured draws, or of blend or clip, starts a new batch. Plain
vertices carry a marker texture coordinate (`u = -4096`) that the 2D shaders read as white, in all four renderers;
nothing changes in the API.

- Moonbase Raid's case (a texture and fills alternating, 5000 quads) is now one batch instead of 5000; the self-test
  frame drawing it dropped from over 5000 draw calls to 157. HUDs that mix boxes and text batch the same way.
- Pictures are unchanged: Droids (game, HUD, deck map, pause menu, title, transfer screen) pixel-identical with
  0.6.4; Moonbase Raid identical; Evolution within its own noise.
- Tests: self-test 1293 checks (draw calls for the 5000-quad frame); post test 51 checks (a fill between two textured
  quads shows pure green on the GPU; it fails if a renderer ignores the marker), passing in software, GL, D3D11 and
  WebGL2.

## 0.6.4 — 4 Nov 2026

**Draw limits are no longer silent** (Moonbase Raid / Operation Nimrod's request: past 4096 2D batches in a frame,
half its baked floor came out black with no message).

- The 2D batch limit is now 65,536 a frame (was 4,096; a new batch starts at every texture, blend or clip change).
- When any per-frame limit fills (2D vertices, 2D batches, mesh instances, immediate 3D vertices, line points,
  skinning matrices, render passes), the draws that do not fit are counted, and the first time each limit fills one
  line names it: `ARC error draw limit: more than N ... in one frame; later ones are dropped`. The count is
  `arc_stats().dropped` (this frame), and the dump's `stats` has `dropped` and `dropped_total`; the `stats` command
  prints both.
- Self-test: the request's case (5000 quads alternating a texture and fills into a 6528 x 3024 target, the last one
  with a texture of its own) draws its last quad and drops nothing; it fails at the old limit. 1292 checks.

## 0.6.3 — 4 Nov 2026

**Depth and draw order for meshes.** New `arc_mesh_style` fields, all renderers; games that do not set them draw
exactly as before (Droids and Moonbase Raid pixel-identical with 0.6.2; Evolution within its own run-to-run noise):

- `depth`: `ARC_DEPTH_TEST_ONLY` (tested, but never hides what comes after: glows, ground effects),
  `ARC_DEPTH_ON_TOP` (ignores depth: markers that show through everything), `ARC_DEPTH_HIDDEN` (only where
  something nearer covers it).
- `depth_pull`: moves a mesh toward the camera by a fraction of its distance, on the same pixels, so floor markings
  and decals never z-fight the surface they lie on (negative pushes it away). Like 0.6.2's line push it is done when
  queued, so identical in every renderer.
- `layer` and `order`: background, opaque, transparent and overlay layers drawn in that order in each pass (immediate
  triangles and lines between opaque and transparent), and an order number inside a layer.
- `xray` / `xray_rgb`: also draw the mesh as a glowing see-through silhouette wherever other solid meshes hide it (a
  player behind walls). The silhouette is drawn after the other solid meshes and before the mesh itself, so a mesh
  never x-rays its own hidden parts (the post test checks this too).

Each mesh batch now carries its own blend and depth state, which the GL, WebGL2, D3D11 and software renderers apply
per batch; shadow maps take only the solid batches with the usual depth. Solid meshes with the usual depth and
order 0 are still batched by mesh. The post test has a new look with every mode beside a control case (49 checks),
passing in software, GL, D3D11 (Wine) and WebGL2 (Chromium).

**D3D11 shader cache.** Compiled shaders are kept in the save folder (`arc_shaders.d3d11`), keyed by a hash of the
HLSL and ARC's version, so later starts skip `d3dcompiler` (in Wine: 200 ms to 3 ms). A damaged or stale file is
compiled again and rewritten; `--no-shader-cache` turns it off. The start-up log line says how many came from the
cache.

## 0.6.2 — 3 Nov 2026

**Line depth push.** `arc_set_line_depth_push(fraction)`: lines drawn after it are moved `fraction` further from the
camera along each point's own line of sight (perspective: away from the eye; orthographic: straight back). Each
point lands on the same pixel, so a guide line still reads as running through the models it was placed on, but it
sits behind them and never cuts across them, from any camera position. Done when the line is queued, so it is the
same in every renderer. It lasts until changed and resets each frame. The post test checks it (a line in front of
a box, pushed behind: hidden over the box, on the same row beside it) in software, GL and D3D11; with the push off
the check fails, as it should.

## 0.6.1 — 3 Nov 2026

**Skins.** `arc_draw_nine` draws a nine-slice texture: the corners unstretched, the edges stretched one way and the
middle both, so one frame image makes panels and buttons of any size. `arc_ui_set_skin` gives arc_ui a nine-slice
texture for its panel, title bar, and widget / hover / active states in place of the theme's flat colours (hover
falls back to the widget skin, active to hover; disabled widgets draw half see-through). Sliders and progress bars
draw a see-through accent bar inside a skinned frame. Games without skins draw exactly as before. The post test
checks the nine-slice regions pixel by pixel; Droids' Ludo-made sci-fi skin was checked in GL, D3D11 and WebGL2.

**The post test runs on real renderers.** `tests/test_post.c` now waits for each look's picture to reach the disk before
drawing the next, and runs at 192 x 128 (Windows will not open a narrower window), so it passes in a GL window and in
D3D11 as well as headless: `build/test_post_gl --frames 1300`, or the Windows build of it. `build.sh` / `build.bat`
give the headless run 200 frames.

## 0.6.0 — 3 Nov 2026

The shader update: nine features, in all four renderers (software, GL 3.3, WebGL2, D3D11), each checked against the
software renderer (the reference) and by the self-test. Games that don't opt in draw exactly as before (Droids,
Moonbase Raid and Evolution compared pixel for pixel). Multiplayer moves to 0.7.

**Materials** (`arc_material`, `arc_mesh_set_material`): a normal map (OpenGL convention, green = up the texture;
no tangents stored: the frame comes from the triangle), an ORM map (occlusion, roughness, metallic, as glTF) with
metallic / roughness factors for **PBR-lite** shading (GGX highlight, Fresnel, reflections of a sky-like environment
that blur with roughness; `arc_set_environment` to choose it), and an **emissive map** times an emissive colour
(above 1 blooms). `.arcm` files carry a material block after the texture (older ARC ignores it), and
`tools/mesh2arcm.py` fills it from glTF materials (`normalTexture`, `metallicRoughnessTexture`, `occlusionTexture`,
`emissiveTexture`, the factors, `KHR_materials_emissive_strength`), or from options: `--normal-map`,
`--normal-from-height` (bump from a height/colour image), `--orm-map`, `--emissive-map`, `--metallic`, `--roughness`,
`--no-material`. Ludo models come in with their normal and metal/rough maps as they are.

**Point and spot lights**: `arc_light_point(pos, rgb, intensity, range)`, `arc_light_spot(pos, dir, rgb, intensity,
range, inner, outer)`, added each frame from `draw()` like drawing. Up to 16 per pass (`ARC_MAX_LIGHTS`); with more,
the strongest near the camera win. Smooth falloff to zero at `range`; diffuse on every lit mesh, GGX highlights on
PBR ones. No shadows from them.

**Post-processing** (new `arc_post` fields): `ssao` / `ssao_radius` (ambient occlusion from the depth buffer, works
with MSAA), `fxaa` (edge-searching, after FXAA 3.11: steps smoothed, straight edges left sharp), `lut` /
`lut_strength` (colour grading; `arc_lut_build(fn, user, size)` makes a table in code, `tools/make_lut.py` makes a PNG
from 10 presets or for grading in an image editor), and the retro set: `scanlines`, `crt_curve`, `pixelate`,
`aberration`.

**Material effects** (new `arc_mesh_style` fields, for `arc_draw_mesh_styled`): `dissolve` (burns away through a
noise pattern fixed to the model, with a glowing edge: `dissolve_rgb`, `dissolve_glow`, `dissolve_width`; shadows burn
too), `hologram` (see-through, glowing, scan lines, in the tint colour), `scroll_u` / `scroll_v` (moving textures) and
`outline` / `outline_rgb` (inverted-hull outline in world units). The instance grew from 112 to 144 bytes.

**Shadows**: `arc_set_shadow_cascades(count, distance)`: 2 or 3 cascades following the screen camera (a 2x2 atlas),
sharp near it and reaching `distance`, blended where they meet. `arc_set_shadow_softness(s)`: a 5x5 tent of
comparisons for wider, softer edges. Defaults keep the single box and the 3x3 filter.

Also: `build.sh web` takes games outside the ARC folder; WebGL2 shaders use high-precision ints; a second self-test
(`tests/test_post.c`, run by `build.sh test`) checks the post looks, cascades and softness.

## 0.5.4 — 2 Nov 2026

**Play sessions** (`arc_app.sessions = true`): every real play (desktop, not headless or scripted runs) is recorded
in `sessions/<date-time>/` where the game runs:
- `input.rec` replays the session exactly (`GAME --replay sessions/<id>/input.rec`), even after a crash (it is
  flushed as it goes);
- `input.rec.saves` holds what `arc_load` returned while recording, so a replay starts from the same saved state
  (settings, profiles, progress) even after the real saves have changed. Replays never write saves. `--record`
  recordings get this too;
- `events.jsonl`: the game's own log, one JSON line per `arc_event("name", "\"field\":%d", ...)` with the tick and
  time (also printed as `ARC event {...}` for agents);
- `log.txt` (the `arc_log` lines) and `summary.json` (time played, frame-time percentiles, the game's final dump);
- F8 (or `arc_bookmark(note)`) flags a moment: a screenshot `mark_N.jpg` and a `bookmark` event, with a short
  on-screen confirmation.
`--session` forces recording, `--no-session` stops it, `--session-dir DIR` moves it. `tools/sessions.py sessions/`
lists sessions, bookmarks (`--marks`, with the command that replays to each), a field's mean across sessions
(`--event duel_end --field won`) or its values in order (`--timeline`).

## 0.5.3 — 1 Nov 2026

Shipping and art tools, from the Babel project's feedback: what Babel did by hand, the next game gets for free.

**Web and release builds** (`tools/new_game.py`): a new game's `build.bat` now has `web` and `release`, like its
`build.sh`. Web builds write `build/web/index.html` (itch.io needs that name). `release` builds Windows and web and
writes `dist/<Title>-windows.zip` (exe + assets) and `dist/<Title>-web.zip` (files at the top level). Older games:
`python tools/new_game.py <game folder> --update-scripts` rewrites both scripts and keeps the old ones as `*.old`.

**Default web page** (`web/shell.html`): the tab shows the game's title (set by the engine from `arc_app.title`);
loading shows a progress bar and no words; Space, Tab, arrows, Page Up/Down, Home, End and Backspace no longer
scroll the page (or the itch.io page around the game), even while loading; clicking the game gives it the keys.

**JPEG screenshots**: `shot FILE.jpg` (and `arc_screenshot`, `shots`) save JPEG at quality 90. A 1920x1080 shot is
about 0.3 MB instead of 2.6 MB. `arc_image_save_jpeg(path, rgba, w, h, quality)` for your own images.

**`camera` command**: `camera EX EY EZ TX TY TZ [FOV]`, `camera orbit YAW PITCH DIST [FOV]` (around the game's own
camera target), `camera off`, and `camera` to print the cameras. It replaces the game's screen camera while drawing
(render targets keep theirs), so screenshot scripts work in any game. Also `arc_set_camera_override`.

**`hud off`** (and `hud on`, `--no-hud`, `arc_set_hud_hidden`): hides all 2D on the screen (HUD, text, UI, FPS
overlay) so screenshots for page art are clean. 2D drawn into render targets is kept.

**emsdk found by itself**: a game's `build.bat web`/`release` uses `emcc` from PATH, else the emsdk in `EMSDK`,
`C:\emsdk` or `%USERPROFILE%\emsdk` (it runs `emsdk_env.bat` for you).

**Ludo models come out at full brightness by default**: `mesh2arcm.py` now ignores a flat grey `baseColorFactor`
(r = g = b < 1) on a textured material and says so. Ludo's older models all have 0.4 and its newer ones 1.0, and its
files can't be told apart from other trimesh exports, so the rule looks at the factor. `--keep-color-factor` applies
it as the glTF standard says; `--ignore-color-factor` ignores any colour factor.

**New tools** (from Babel's scripts, with their paths made into options):
- `tools/recolor_texture.py`: one texture per team/tongue/faction colour from a model's red parts (`--select red`,
  `--strict` to leave orange-brown wood), pale cloth (`pale`) or any hue range (`hue:200-260`). GLB or image in.
- `tools/audio_prep.py sfx|voice|loop IN OUT`: trims silence, evens out loudness (measured after trimming), limits
  peaks, adds tiny fades; loops are only levelled. Needs ffmpeg.
- `tools/itch_art.py`: itch.io cover (630x500 and 2x) and banner (1920x800, 960x400) cropped from your pictures, with
  the title on a plaque (your nine-sliced panel image and font, or a drawn parchment box).

**No Visual Studio warnings from ARC's own files**: 120 implicit int-to-float conversions are now explicit casts
(the results are identical: same pictures, same replays), and the random generator's rotate no longer applies minus
to an unsigned number (MSVC warning C4146 at `arc_core.c` line 84).

## 0.5.2 — 1 Nov 2026

Fix: tilt-shift made the blurred parts of the picture about 4 times too bright, in every renderer (reported by
Babel). The blur chain adds its 4 levels together, and the composite now divides by 4, as bloom already did. The
self-test now runs tilt-shift and checks that a fully blurred area keeps its brightness.

`tools/mesh2arcm.py --ignore-color-factor`: for textured materials, ignore the glTF `baseColorFactor` colour (its
alpha still applies). The converter follows the glTF standard and multiplies the texture by that factor, but some
model generators (Ludo, for one) export a dark factor alongside the texture, so their models came out too dark.
Suggested by Babel.

FPS overlay: it now shows the 1% low frame rate, the worst frame and the frame-time percentiles (50th, 95th, 99th)
of the last 10 seconds, so short stutters show up. The graph grows to fit spikes (lines at 60, 30, 20 fps... and a
white line at the 99th percentile). The first 5 frames after start-up are left out. The same figures are available
from `arc_get_frame_times()`, the `stats` command (an `ARC frames` line) and the `stats` section of dumps
(`frame_p50_ms`, `frame_p95_ms`, `frame_p99_ms`, `frame_max_ms`, `low1_fps`).

## 0.5.1 — 1 Nov 2026

Nine requests from the Babel project. Everything that draws works in all four renderers, checked with one feature
scene (under 0.6% of pixels differ between software, OpenGL, WebGL and Direct3D). Games written for 0.5.0 build and
behave the same.

**Billboards** (`src/arc_bill.c`): `arc_draw_billboard(_ex)` draws a camera-facing picture in the 3D world, and
`arc_draw_billboard_text(_font)` draws text, optionally on a speech-bubble box. Both are depth tested, so a tower in
front hides a speech bubble behind it. Flags: `ARC_BB_PIXELS` (same size on screen at any distance), `ARC_BB_BOTTOM`
(anchored at the bottom middle), `ARC_BB_UPRIGHT` (turns only around the vertical), `ARC_BB_ADDITIVE`. Each different
text becomes a small cached texture (128 at a time).

**Tilt-shift**: `arc_post.tilt_shift`, `tilt_focus`, `tilt_width`. The picture stays sharp in a horizontal band and
blurs above and below it, for a miniature look. It uses its own blur chain, so it works with or without bloom.

**Mesh builder** (`src/arc_meshb.c`): `arc_mb_begin`, then `arc_mb_tri/quad/box/lathe/extrude` with colours and a
transform stack, then `arc_mb_finish(b, smooth_degrees, flags)`. Winding is fixed automatically (and `ARC_MB_OUTWARD`
turns every face away from the centre); normals are flat, or smoothed across edges below the given angle.

**Sky gradient**: `arc_set_sky(top, horizon)` / `arc_set_sky_ex(top, horizon, below)` / `arc_sky_off()`. The screen's
background follows the view direction. Use the fog colour as the horizon and distant things fade into the sky.

**UI**: `arc_ui_button_ex(label, enabled)` and `arc_ui_enabled(false)` draw widgets faded and ignore clicks ("can't
afford"). Text that doesn't fit a widget is shrunk to 60%, then cut short with "...".

**Sound buses**: every sound plays on a bus (`ARC_BUS_SFX`, `MUSIC`, `UI`, `VOICE`), each with its own volume:
`arc_set_bus_volume`, `arc_sound_set_bus`, `arc_play_bus`, `arc_stream_set_bus`. `arc_audio_save_settings` /
`arc_audio_load_settings` keep them, and `arc_ui_sound_settings()` is a ready-made settings panel.

**Text helpers**: `arc_draw_text_ex(x, y, scale, rgb, flags, text)` with `ARC_TEXT_CENTER/RIGHT/MIDDLE/BOTTOM` and
`ARC_TEXT_SHADOW/OUTLINE`; `arc_font_draw_aligned`; `arc_text_height`; `arc_text_fit_scale`.

**Balance sweeps**: `tools/sweep.py GAME --seeds 1-50 --frames 6000 --field game.wins` runs the game headless over
many seeds in parallel and prints one row per seed, then median, min, max and mean. `--set` passes tunables,
`--csv` saves the table.

**Matrix helpers**: `arc_m4_rotate(axis, angle)`, `arc_m4_trs(t, q, s)`, `arc_m4_shear`, `arc_m4_mul_dir`,
`arc_m4_chain(n, ...)`, `arc_quat_rotate`, `arc_quat_from_to`.

Also: a mesh that uses a render target as its texture is no longer upside down in OpenGL and WebGL (it now matches
Direct3D and the software renderer; a game that flipped its UVs to make up for it should stop doing so).

## 0.5.0 — 1 Nov 2026

Nine additions, each in all four renderers where it draws (software, OpenGL 3.3, WebGL2, Direct3D 11) and compared
across them with one feature scene (under 1.3% of pixels differ between any two). Games written for 0.4 build and
behave the same: Droids, Moonbase Raid and Evolution give the same dumps and screenshots.

**Particles** (`src/arc_fx.c`): describe an effect once (`arc_fx`: life, speed, spread, gravity, drag, size and colour
over life, fade, glow, additive, velocity stretch for sparks, spin, floor bounce, texture) and fire `arc_fx_burst` or
attach `arc_emitter`s that follow moving things smoothly. Simulated in tick with their own random stream (replays stay
exact, `arc_rand` untouched); `arc_draw_particles()` draws them as unlit instanced quads in the see-through pass.

**Shadows**: `arc_set_shadows(center, radius, resolution)`, `arc_set_shadow_strength`, `arc_mesh_set_cast_shadow`.
One directional shadow map around a point (snapped to texels, so no shimmer as it follows the camera), soft 3x3
filtered edges, normal-offset bias, cut-out textures cast cut-out shadows, skinned meshes cast too.

**Post-processing**: `arc_set_post` with bloom (bright pass, 5-level down/up chain), exposure, ACES tone curve,
desaturate, contrast and vignette. The screen's 3D renders in HDR (16-bit float; multisampled with MSAA); the
screen's 2D is drawn afterwards, untouched. Glowing particles and emissive meshes above 1.0 bloom.

**Collision** (`src/arc_collide.c`): `arc_ray_sphere/box/triangle/mesh`; `arc_cworld` collision worlds (meshes, boxes,
triangles in a deterministic BVH) with raycasts, sphere overlap and push-out; `arc_character_move`, a capsule
controller with gravity, slopes without sliding, step climbing, wall sliding and ground snapping.

**Positional audio**: `arc_set_listener`, `arc_play_3d(_ex)`, `arc_voice_set_position/range` (distance falloff,
equal-power panning, a little quieter behind), `arc_voice_set_lowpass`, `arc_set_master_lowpass`,
`arc_set_reverb` (Freeverb-style) with per-voice sends. Music and streams stay dry.

**UI** (`src/arc_ui.c`): immediate-mode widgets called from tick: panels (draggable), columns, label, button, toggle,
slider, int slider, select, list, text field, progress, separator; keyboard and gamepad focus navigation; themes and
TTF fonts. `arc_ui_inspector` shows every `arc_var` as a slider (` with `--dev` or `arc_app.dev_keys`). Agents: `ui` and `ui_click LABEL`.

**Agent tooling**: `gif` (ARC's own GIF encoder, median-cut palettes), `shots` (PNG sequences), `compare` (screenshot
regression with a diff image). `arc_app.save_state/load_state` (with `arc_writer`/`arc_reader` and pool helpers)
enable `seek`/`rewind` backwards (snapshots every 60 ticks, which also hold particles, emitters and UI focus;
re-simulated silently from the input history),
`state_save`/`state_load` and `state_check`. SWARM implements them.

**Smaller downloads**: `.arcm` version 3 (16-byte quantised vertices, about 40% smaller; written by default by
`mesh2arcm.py`, `--v2` for the old layout). `.arcpak` packs (`tools/arcpak.py`, zlib) that every loader reads first;
`assets.arcpak` mounts itself. Evolution's assets: 8.0 MB -> 4.2 MB.

**Hot reload**: `--hot` (or `arc_app.hot_reload`) reloads textures, meshes, animations and sounds when their files
change, keeping their handles; `reload` does it on demand. Code: `./build.sh hot game.c` / `build.bat hot game.c`
build the engine as a host (`arc_host`) and the game as a DLL / shared object that reloads when rebuilt;
`arc_persist(name, size)` memory survives, and `arc_app.reloaded` runs after each reload. `samples/hot`.

**FPS overlay**: `arc_show_fps(true)`, `--fps`, the `fps` command or F3 with `--dev`: frame rate, frame/CPU/tick
times, draw calls, triangles and a 120-frame graph (green 60+, amber 30+, red below).

**Also**: `arc_file_exists`; `ARC_API` on every engine function (exported by hot-reload hosts); `build.bat test`
matches `build.sh test` again. Builds use `-ffp-contract=off` (no fused multiply-adds, so the same maths on
x86, ARM and WebAssembly). Packs, `.arcm` v3 and state files are validated against damaged or hostile data
(fuzzed under ASan). Self-test: 1238 checks (also clean under ASan/UBSan).

## 0.4.0 — 31 Oct 2026

Driven by Evolution's art feedback (textured Ludo models, a rigged player, water, fog). Every feature works in all
four renderers (software, OpenGL 3.3, WebGL2, Direct3D 11) and they render the same test scene alike.

**Textured meshes**

- `arc_vertex` gains `u, v` (36 bytes). Meshes take one texture: `arc_mesh_set_texture`; `.arcm` files carry their
  own. Lighting is per pixel now. All-zero normals are computed for you (smooth).
- `arc_texture_create_ex` / `arc_texture_load_ex` with `ARC_TEX_SMOOTH`, `ARC_TEX_REPEAT`, `ARC_TEX_MIPMAPS`.
- Baseline JPEG decoding (`src/arc_jpeg.c`, ARC's own) for textures and `arc_image_load`.
- `.arcm` version 2: UVs, embedded texture, skin weights, 16-bit indices. Version 1 and `.axm` still load.
- `tools/mesh2arcm.py` rewritten: built-in glTF/GLB reader (textures, atlases for several materials, vertex
  colours, skins, animations), `--footprint/--height/--size`, `--texture-size`, `--batch`.
- `arc_mesh_update` changes a mesh's vertices in place (same count).

**Skeletal animation**

- `.arca` files (skeleton plus clips), written by `mesh2arcm.py --anim`. `arc_anim_load`, `arc_anim_find`,
  `arc_anim_pose` blends any number of weighted layers; `arc_pose_joint` gives a joint's matrix (to hang a weapon on a
  hand). `arc_draw_mesh_posed` draws a skinned mesh in that pose: skinned on the GPU, still batched.

**See-through meshes**: `arc_draw_mesh_alpha`, `arc_draw_mesh_additive`, and `arc_draw_mesh_styled` (tint,
recolour, emissive, whiten, transparency, pose in one struct). Sorted back to front after the solid geometry;
texture and vertex alpha count.

**Look**: `arc_set_fog_exp` (exponential fog), `arc_set_rim` (rim light), `arc_app.msaa` / `--msaa N`
(multisampling), `arc_app.hidpi` with `arc_dpi_scale()` for sharp web canvases on high-DPI screens.

**Audio**: `arc_play` returns an `arc_voice`; `arc_play_ex` adds pitch and looping; voices can be stopped, moved and
repitched. `arc_stream_*` queues PCM the game generates. `arc_sfx` gains vibrato.

**Fixes and tools**

- `shot` draws and saves immediately, so it captures the state the script just stepped to.
- A built-in command name with an argument that isn't a key (`release` in a game) reaches the game's `command()`.
- `arc_camera_ray` / `arc_camera_to_screen` / `arc_camera_view_proj` take a camera, so picking works in `tick`
  and headless.
- `tools/new_game.py` writes the MSVC `build.bat` tested with Evolution.

**Upgrading**: code that fills `arc_vertex` with positional initialisers still compiles (u, v default to 0);
`arc_sfx` positional initialisers need the two vibrato fields or designated initialisers. Self-test: 1182 checks.

## 0.3.1 — 31 Oct 2026

**MP3**: ARC decodes MP3 itself now (`src/arc_mp3.c`, written from ISO/IEC 11172-3 and 13818-3; no third-party code).
`arc_sound_load` and `arc_music_load` take `.mp3` files: sounds decode whole, music streams a frame at a time.

- MPEG-1, 2 and 2.5 Layer III; mono and stereo, mid/side and intensity stereo; long, short and mixed blocks; CBR,
  VBR and free format; streams that switch between mono and stereo; ID3v2 tags skipped.
- Gapless: the Xing/Info + LAME tag's encoder delay and padding are trimmed exactly as FFmpeg trims them, so the
  length is exact and looping music has no gap.
- Checked against all 13 ISO/IEC 11172-4 Layer III conformance streams (full accuracy: no sample off by more
  than 1) and against FFmpeg on 44 files (same length, 92-115 dB). Robust against damaged files (fuzzed under
  AddressSanitizer). About 200x real time.
- The standard's tables are generated by `tools/mp3_tables_gen.py` into `src/arc_mp3_tables.h`.
- Self-test: 1142 checks, including an embedded MP3 decoded, streamed and looped.

**Fix**: `arc_app_dir()` ends in `/` on Windows too, as documented.

## 0.3.0 — 31 Oct 2026

Added to move Moonbase Raid off raylib, and useful to any UI-heavy game. Checked by putting Moonbase Raid's whole
front end on ARC: 42 screens compared with the raylib build pixel by pixel.

**Render targets, clipping, blending** (all four renderers: software, OpenGL 3.3, WebGL2, Direct3D 11)

- `arc_target_create`, `arc_target_begin(target, clear, argb)` / `arc_target_end`: draw 2D and 3D into a texture,
  then draw that texture. Targets nest; each pass keeps the camera and lights set while it was current.
  `arc_target_width/height` give the size being drawn into.
- `arc_set_clip` / `arc_clear_clip`: scissor rectangle for 2D drawing.
- `arc_set_blend`: `ARC_BLEND_ALPHA` (source-over, alpha kept), `ADD`, `MULTIPLY`, `NONE`.
- `arc_draw_triangles2d`: textured, vertex-coloured 2D triangles. `arc_draw_texture_ex`: source rect, flip,
  rotation about an origin (raylib's DrawTexturePro). `arc_texture_set_smooth`, `arc_texture_destroy`.
- Textures destroyed in the middle of a frame are freed once that frame has rendered.
- The software renderer has a proper fill rule now: blended quads no longer double-draw their diagonal.

**Immediate mode** (`arc_app.immediate`): for games whose screens take input while they draw. `tick()` may draw;
every tick's render-target passes run (so cached targets stay right) and the screen shows the latest frame.
`init()` may draw into render targets in every mode (baking art at load).

**Fonts from TTF**: `tools/font_bake.py` bakes `.arcf` bitmap fonts with fontTools; glyph boxes, offsets and
coverage match stb_truetype/raylib exactly (alpha within 1/255). `arc_font_load`, `arc_font_draw(_ex)`,
`arc_font_width`, `arc_font_glyph` (for your own layout), UTF-8 with `arc_utf8_next`.

**Audio files and music**: `arc_sound_load` reads QOA and 16-bit WAV, mono or stereo, at any rate (the mixer
resamples while it plays). `arc_music_*` streams QOA from memory, looping or not. `tools/audio_convert.py` turns
MP3/OGG/WAV into QOA through ffmpeg and `tools/qoa_encode.c`, keeping hissy short effects as WAV.

**Input**: a press and release between two ticks now counts (the key is down for one tick). Gamepads (XInput on
Windows, evdev on Linux, the Gamepad API on the web) as `ARC_PAD_*` keys and `arc_pad_axis`. Touch on the web
(first finger = mouse, `arc_touch_mode`). Typed text (`arc_text_input`), `arc_key_any_pressed` (key rebinding),
17 more keys (punctuation, Insert/Delete/Home/End/Page keys, keypad Enter). All of it is in the recorded input,
so replays and agent runs stay exact. Agent commands: `axis LX 0.5`, `type TEXT`, `touch on|off`, `tap PAD_A`.

**Window and files**: `arc_app.resizable` (+ `min_width/height`; windows are fixed-size by default now),
`arc_window_resized`, `arc_window_set_icon`, `arc_window_toggle_fullscreen`, `arc_app_dir`.
`arc_save` / `arc_load` / `arc_save_remove`: atomic files under the user's data folder (`arc_app.save_folder`), or
localStorage on the web; headless runs use `./arc_saves` or `--save-dir`. `arc_image_load/decode/save_png` for
images on the CPU. `arc_screenshot_after_frame` for a game's own capture options. `arc_app.own_flags` hands flags
such as `--frames` to the game.

**Fixes**: values like the `help` in `--panel help` were taken for flags; negative numbers can be flag values;
large images (a 2048x2048 atlas) no longer overflow the frame arena while loading.

**Recordings**: the input record format is version 2 (gamepad, touch, text); version 1 files are refused with a message.

## 0.2.0 — 31 Oct 2026

Added for the Droids port, and useful to any game.

**Turns and grids** (`arc_turns_*`, `arc_grid_*`)

- Time-unit scheduler with real-time, roguelike and tactical modes.
- Tile grid with line of sight and breadth-first pathfinding (`path_step`, `path_length`).
- Ported from the AXE prototype.

**Meshes**

- `arc_mesh_load` reads AXE `.axm` (AXM2) files directly; `tools/mesh2arcm.py --axm` writes them.
- `arc_mesh_bounds` returns a mesh's bounding box.
- `arc_mesh_set_double_sided`; `.axm` files load double-sided.

**Drawing**

- `arc_draw_mesh_tinted`: recolour toward a tint while keeping light and dark detail, plus a whiten amount for hit flashes and glow.
- Mirrored model matrices now render correctly with every mesh draw.
- Immediate geometry for shapes rebuilt every frame: `arc_draw_triangles`, `arc_draw_tri`, `arc_draw_quad3`. It is lit like meshes and drawn double-sided.
- `arc_draw_box`: a solid axis-aligned box.

**Math:** `arc_m4_basis`, `arc_angle_lerp`.

**Fix:** `arc_draw_cube` and `arc_draw_sphere` drew nothing in 0.1.0, because the built-in meshes were created before the renderer was ready.

**Tools:** C++ games built for the web now link with `em++` (`new_game.py` template).

## 0.1.0 — 31 Oct 2026

First release.

**Platforms**

- Windows: Win32 + Direct3D 11.
- Linux: X11 + OpenGL 3.3.
- Web: Emscripten + WebGL2.
- Headless: software renderer.

**Engine**

- Fixed-step deterministic loop.
- Input recording and replay.
- Agent commands (`--cmd`, `--script`, `--stdin`).
- JSON dumps and PNG screenshots.

**Graphics**

- Instanced meshes, lines, and a 2D overlay (rectangles, sprites, built-in font).
- Procedural meshes, `.arcm` loader, PNG and QOI textures.

**Other**

- Synthesised sound effects and a mixer.
- Tunable variables and profiler zones.
- Memory arenas and handle pools.

**Tools**

- `mesh2arcm.py` — converts models to `.arcm`.
- `new_game.py` — scaffolds a new game project.
- `gen_api_docs.py` — regenerates `docs/API.md`.
- `font_gen.py` — regenerates the built-in font.
