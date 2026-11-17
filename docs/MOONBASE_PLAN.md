# Moonbase Raid → ARC: plan for review

> **Done, 31 Oct 2026** (ARC 0.3.0, Moonbase Raid 0.13.0). Allan chose the full move; Operation Nimrod shares the
> codebase. Outcome and method: `MIGRATION.md`, "Moonbase Raid: moving a raylib game". The raylib version is kept as
> `port-raylib` until the ARC version has been played.

Survey of `D:\Projects\Moonbase Raid`, 31 Oct 2026.

## What is there now

- **`source/`** — the JavaScript game (`game.src.html`). It is the reference for the rules, with the Python balance tools around it.
- **`port/`** — a finished C++ port, version 0.12, released for web, Windows and Linux.
  - **Front end:** raylib 5.5.
  - **Build:** CMake.
  - **Content:** fonts, a sprite atlas and 29 MP3s.
- **The port's rules (`core/`) are already engine-free:**
  - no raylib, no files, no clock;
  - checked against the JS game by golden tests (1.79 M map values, 192 recorded games, 36 bot jobs);
  - with a balance bot and tests on top.
- **The front end (`game/`, `platform/`)** is about 8,400 lines, and is the only part that touches raylib:
  - menus, the mission screen, the map view, floor and portrait drawing;
  - audio, input bindings and a 3D view prototype.

**So moving Moonbase Raid to ARC means swapping the front end's engine, not porting the game.** The core, bot, tests, data, saves format and balance stay exactly as they are.

## What ARC needs first (ARC 0.3)

These come from the raylib features the front end actually uses, counted in the source:

| Feature | Used for | Notes |
|---|---|---|
| **Render targets** (draw into a texture, then draw that texture) | Map view, floor layer, UI canvas scaled to the window | GL framebuffer, D3D11 render target view, software |
| **Clip rectangles** | Scrolling panels and tables in menus | Scissor in all backends |
| **Blend modes:** alpha, additive, custom | Glows, the floor layer | Additive is used in the UI; floor uses a separate-alpha blend |
| **2D triangles and transformed sprites** (rotation, source rects, filtering per texture) | Canvas-style drawing in `gfx.cpp` (ellipses, triangles, transform stack), the sprite atlas | Extends the 2D batch from quads to triangles |
| **Fonts from TTF**, baked by a tool | Press Start 2P and VT323 at several sizes, UTF-8 text | `tools/font_bake.py` (Python + Pillow) writes `.arcf` bitmap fonts; the game never parses TTF. Sharp pixels, as raylib draws them now |
| **Audio files and music streaming** | 29 MP3s: sound effects and music | ARC won't decode MP3 (no libraries). A tool converts to **QOA** ("Quite OK Audio", a public-domain format, about 300 lines to decode). Effects load whole; music streams. |
| **Gamepad** | The accessibility options (stick moves the pointer, buttons) | XInput (Windows, loaded at runtime), evdev (Linux), the Gamepad API (web) |
| **Touch on the web** | The phone layout | Pointer events: taps become clicks and drags become scrolls |
| **Text and window extras** | Character input (rebinding keys), resizable window, minimum size, icon, more keys (Ctrl+Z, `.`) | Small additions to each platform |
| **Saves** | Campaign saves, settings, mid-mission resume | `arc_save` / `arc_load`: a data folder on desktop, IndexedDB on the web |

Each feature is useful to other games too:

- Render targets, clipping and fonts suit any UI-heavy game.
- Audio files and gamepad are needed by everything.
- Touch is needed by Trench Strike's mobile version.

## How the front end moves

1. **Write a thin compatibility layer in the game** (`game/src/rl_arc.h`). It provides the ~90 raylib calls the front end uses, built on ARC: `DrawTexturePro`, `BeginScissorMode`, `LoadRenderTexture` and so on. The 8,400 lines then change very little. This is how Droids moved (`view.h`).
2. **Move the main loop** from raylib's frame callback to ARC's `tick`/`draw`. Front-end timing (animation playback, blink, the enemy-turn pacing) runs on ARC's fixed 60 Hz tick, so headless runs and agent scripts are repeatable.
3. **Add `dump()` and agent commands** that mirror the existing `--do "menu start;tap 5 6;end"` options. Agents can then play any screen headless with ARC's normal tools.
4. **Change the build:**
   - CMake + raylib FetchContent becomes the ARC build scripts.
   - The core, bot and tests keep their CMake build, because they don't use an engine.
   - `build_pc.bat`, `build_web.bat` and `tools/release.py` are updated to match.
5. **Content:** `tools/export_assets.py` gains the font bake and the MP3→QOA step. The atlas PNGs load as they are.
6. **Check it:**
   - The existing golden, armoury, bot, undo and save tests must still pass; they touch only the core.
   - Screens are compared side by side with the raylib build, using the port's own `tools/shots/compare.py` (41 cases). Target: the same small differences it already accepts against the JS (blink timing, font smoothing).
   - Builds are checked for web, Windows and Linux.
7. **Keep the raylib version** as `port-raylib/` (or a git tag) until the ARC version has passed all of this and you have played it.

## Order of work and size

| Step | Work | Rough size |
|---|---|---|
| 1 | ARC 0.3: render targets, clipping, blend modes, 2D triangles and transforms (all 4 backends + tests) | Large |
| 2 | ARC 0.3: font bake tool + `.arcf` fonts; QOA decoder, music streaming, converter | Medium |
| 3 | ARC 0.3: gamepad, web touch, text input, window extras, saves | Medium |
| 4 | Moonbase front end on ARC through the compatibility layer; agent commands | Large |
| 5 | Screen comparison, all tests, three builds, docs | Medium |

Steps 1–3 benefit every future game. Step 4 is where Moonbase Raid itself changes.

## Things to decide

- **Whether to move it at all now.** The raylib version works and is released on three platforms. Moving it buys:
  - one engine across all your games;
  - no third-party library;
  - ARC's agent tools (scripted play, dumps, screenshots on every screen);
  - an easier route to a 3D tactical view later.

  It costs a front-end rewrite and a full re-test. If a jam or release date is close, finish that on raylib first.
- **Operation Nimrod.** You said it runs on the same engine. It isn't in this folder. If it shares `core/` and `game/`, it moves with Moonbase Raid; if it is a separate JS game, it is a separate job.
- **Fonts.** Baking keeps today's look (sharp VT323 / Press Start 2P). Smooth, browser-like text would need bigger baked sizes, which can come later.
