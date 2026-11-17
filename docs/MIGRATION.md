# Moving your games to ARC

This plan has three parts:

- which games to move, and in what order;
- what ARC still needs before each game can move;
- the step-by-step method for porting one game.

It is based on the games I know about from our work together. I have not opened most of the other folders in `D:\Projects`, so step 0 is a survey.

## Step 0 — survey `D:\Projects`

Your projects folder holds about 70 projects. For each one, record:

- the language and engine (Unity C#, browser JavaScript, Three.js, LittleJS, C/C++, 6502);
- its size;
- its status: active, shipped or shelved.

Connect `D:\Projects` to a session and ask: *"survey D:\Projects for the ARC migration and fill in docs/MIGRATION.md"*. Shelved and finished projects can usually stay as they are. Moving a game earns its keep only if you will keep working on it or want a native .exe (Steam) build.

## Which games, in what order

| # | Game | Today | Why move it | Needs in ARC first | Effort |
|---|---|---|---|---|---|
| 1 | **Droids** (`Paradroid90`) | C++ on AXE (sokol) | ✅ **Ported 31 Oct**: `Paradroid90\droids-arc`, on ARC 0.2. Runs on Windows, Linux and the web, with a smoke test. | Done in 0.2: turns module, `.axm` loading, tinted, mirrored and double-sided meshes, immediate geometry | Done |
| 2 | **Moonbase Raid** and **Operation Nimrod** | C++ port on raylib 5.5 (`Moonbase Raid\port`), from the JS game | ✅ **Front end moved 31 Oct**, on ARC 0.3. One codebase, so Nimrod moves with it. The raylib version is kept as `port-raylib` until the ARC one has been played. | Done in 0.3: render targets, clipping, blend modes, TTF fonts, QOA audio, gamepad, touch, saves, immediate mode | Done |
| 3 | **Trench Strike 3D** | Three.js | Native and web from one codebase; better performance on mobile | Textured meshes (UVs and materials), touch input, gamepad | Medium |
| 4 | Future 3D turn-based games | — | Start them on ARC directly | Items 1–3 above | — |
| — | **FLATLINE**, **Last Call**, **Exit Interview** | Single-file HTML jam games | Leave them as they are unless one becomes a full game. They already run everywhere, and a port is a rewrite. | — | — |
| — | **Sap** (LittleJS Jam 3) | LittleJS | The jam requires LittleJS. Only port it if you develop it further after the jam. | 2D layer | — |
| — | **Trench Strike** (RP6502) | 6502 for the Picocomputer | Hardware-specific; it can't move. The 3D version is item 3. | — | — |

## Engine features to add, in the order the games need them

Each feature goes into ARC with a test, an API comment, and a `CHANGELOG.md` entry (see `SHARING.md`).

| Version | Feature | Needed by | Notes |
|---|---|---|---|
| 0.2 ✅ | **Turn-based module** (time-unit scheduler, grid, line of sight, pathfinding), ported from AXE | Droids, Moonbase Raid, Nimrod, future tactics games | Shipped as `arc_turns_*` / `arc_grid_*` |
| 0.2 ✅ | **Model import**: the `.axm` reader | Droids | `arc_mesh_load` reads `.axm` directly |
| 0.3 ✅ | **2D for UI-heavy games**: render targets, clipping, blend modes, 2D triangles, rotated/flipped sprites, immediate mode | Moonbase Raid, Nimrod, Sap | Moonbase's own UI code kept its widgets; ARC supplies the drawing |
| 0.3 ✅ | **Fonts from TTF**, baked offline | Moonbase Raid, Nimrod | `tools/font_bake.py` → `.arcf`; matches raylib's glyphs |
| 0.3 ✅ | **Saves**: `arc_save` / `arc_load` | Moonbase Raid, Nimrod | Files on desktop; localStorage on the web |
| 0.3 ✅ | **Audio files and music**: QOA and WAV, streamed music | All | `tools/audio_convert.py` from MP3/OGG |
| 0.3 ✅ | **Gamepad and touch**, text input | Moonbase Raid, Trench Strike 3D, Steam builds | XInput, Linux evdev, browser Gamepad API; web touch |
| 0.4 ✅ | **Textured meshes and animation**: UVs, one texture per mesh, mipmaps, JPEG; skinned meshes and `.arca` clips; see-through meshes; MSAA | Evolution, Trench Strike 3D, rigged Ludo models | `.arcm` v2 from `tools/mesh2arcm.py` (glTF built in) |
| 0.5 ✅ | **Shadows, post-processing, particles, collision, UI, positional audio, hot reload, packs, agent tooling** | Evolution, Trench Strike 3D, jam games | `arc_set_shadows`, `arc_set_post`, `arc_fx`, `arc_cworld`, `arc_ui_*`, `arc_play_3d`, `--hot`, `.arcpak`, `gif`/`compare`/`seek` |
| 0.6 | Multiplayer: relay server, command relay, rollback; a job system | Droids, Evolution, Trench Strike | builds on save_state/load_state |

## How to port one game

1. **Create the project:** `python tools\new_game.py D:\Projects\<Game>-arc --cpp` (or without `--cpp` for C). Keep the old project untouched until the port is finished.
2. **Write `dump()` first.** List the state that defines the game: player, enemies, map, score, turn and phase. Agents will compare this between the old and new versions.
3. **Port the rules, not the rendering.**
   - Move the game logic into `tick()` (real-time games) or turn handlers (turn-based games).
   - Replace `Math.random()` and `Random.Range` with `arc_rand_*` so runs can be replayed.
4. **Rendering:** rebuild `draw()` with ARC calls. Assets go through `tools/mesh2arcm.py` (models) or stay as PNG files (sprites).
5. **Debug commands:** add the cheats you used while developing (`level N`, `give ammo`, `win`) as `command()` handlers, so agents can reach any situation quickly.
6. **Test headless:** script a playthrough of the first level. Record it with `--record`, and keep the `.rec` file in the game's `tests/` folder.
7. **Check all targets:** `build.bat`, `./build.sh web`, plus a headless run. Compare screenshots with the old game.
8. **Switch over:** archive the old project, or keep it as `<Game>-legacy`, and rename the ARC version.

## Droids: how it was mapped (first migration, done)

| AXE / sokol | ARC |
|---|---|
| `ax_main`, sokol app callbacks | `arc_main` returning `arc_app{init, tick, draw, dump, command}` |
| `ax_sched_*` (TU scheduler, three time modes) | `arc_turns_*` |
| `ax_grid_*` | `arc_grid_*` |
| `ax_draw_mesh(_basis)`, AXM2 meshes | `arc_draw_mesh_tinted` with `arc_m4_basis`; `.axm` loads directly |
| `ax_draw_box`, `ax_draw_prism`, `ax_draw_tube_block` | `arc_draw_box` (instanced), and `arc_draw_quad3` immediate geometry in the game's `view.h` |
| `ax_camera_apply` (top-down, chase, first-person rigs) | game-side `dv::Camera` building an `arc_camera` |
| sokol_debugtext | `arc_draw_text` (8×8 font, scalable) |
| `--duel=NNN`, `--gallery` flags | the same flags through `arc_arg_value` / `arc_arg_flag`, plus `command()` handlers for agents |
| Transfer duel (`transfer_duel.h`, `duel_policies.h`) | unchanged: plain C++ game code |
| Balance tools (`duel_sim`, `duel_gt`, `nash.py`) | unchanged: they don't use the engine |

## Moonbase Raid: moving a raylib game (second migration, done)

Moonbase Raid's front end was 8,400 lines of raylib calls. Rewriting every screen would have risked the look the
game had been tuned to, so it moved through a **compatibility layer** instead:

1. **`game/src/rl_arc.h` / `rl_arc.cpp`** (~600 lines) give the ~90 raylib functions the game calls, built on ARC.
   They copy raylib 5.5's own geometry (rectangle outlines, line strips, 36-segment circles, `DrawTexturePro`'s
   corners), keep a canvas-style transform stack for `rlPushMatrix`/`rlTranslatef`, flip render-texture
   coordinates (raylib stores them upside down), and keep raylib's key numbers (the game saves bindings with them).
   The game's own files changed only their `#include`.
2. **Audio, clicks and the platform file** were small enough to port to ARC directly.
3. **The main loop** became `arc_main` with `immediate = true`: the screens handle clicks while they draw, so each
   tick runs the old frame function whole. The game's `--frames N --shot F` options kept working through
   `arc_app.own_flags` and `arc_screenshot_after_frame`.
4. **Content**: `tools/arc_content.py` bakes the TTFs into `.arcf`. The MP3s load as they are (ARC 0.3.1 decodes MP3).
5. **Checking**: `tools/shots/compare_engines.py` screenshots 42 game states (every menu and shop page, mission
   starts and later turns on every map, touch mode, the 3D prototype) with both builds and compares them. The
   core's 10 test suites (golden tests against the JS game, bot, saves) ran unchanged.

For the next raylib game, copy `rl_arc.h`/`.cpp` and add whatever calls it uses that Moonbase did not.

| raylib | ARC |
|---|---|
| `InitWindow`, `while (!WindowShouldClose())` | `arc_main` returning `arc_app{init, tick, ...}`, `immediate = true` |
| `BeginTextureMode` / `LoadRenderTexture` | `arc_target_begin` / `arc_target_create` |
| `BeginScissorMode`, `BeginBlendMode` | `arc_set_clip`, `arc_set_blend` |
| `DrawTexturePro`, shapes, `rlBegin(RL_TRIANGLES)` | `arc_draw_triangles2d` (the layer builds raylib's triangles) |
| `LoadFontEx` + `DrawTextCodepoint` | `.arcf` from `font_bake.py`, `arc_font_glyph` |
| `LoadSound` / `LoadMusicStream` (MP3) | `arc_sound_load` / `arc_music_load` (MP3, QOA, WAV) |
| GLFW click latching, browser touch listeners | ARC latches taps and handles touch itself |
| `GetApplicationDirectory`, localStorage glue | `arc_app_dir`, `arc_save` / `arc_load` |
