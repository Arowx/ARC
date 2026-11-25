# ARC

ARC is a small, fast game engine in plain C11, with no external libraries. It is designed to be driven by AI agents as easily as by people.

| Target | How it runs |
|---|---|
| **Windows** | Win32 + Direct3D 11, a single static .exe |
| **Linux** | X11 + OpenGL 3.3 |
| **Web** | Emscripten + WebGL2, about 350 KB |
| **Headless** | Software renderer, no display needed; any binary with `--headless` |

## Features

- **Instanced 3D renderer.** 10,000 objects take about 0.5 ms of CPU and 4 draw calls.
- **Built-in essentials.** 2D overlay, built-in font, PNG/QOI/JPEG textures, procedural meshes, a mesh builder (boxes, lathes, extrusions with automatic winding and normals), and an `.arcm` mesh format with a glTF converter.
- **Textured, animated models.** Per-pixel lighting, mipmaps, skinned meshes with blended `.arca` animation clips (skinned on the GPU, still batched), see-through meshes, exponential fog, rim light, MSAA.
- **Materials and lights.** Normal maps, metal/roughness (PBR-lite with environment reflections) and glow maps, read from glTF by the converter; up to 16 point and spot lights per pass.
- **Shadows and post-processing.** A directional shadow map, optionally in 3 cascades with soft edges; HDR post-processing (bloom, ACES tone curve, exposure, vignette, contrast), SSAO, FXAA, colour-grading tables and retro looks (scanlines, CRT curve, pixelate, chromatic aberration).
- **Material effects.** Dissolve with a burning edge, hologram, scrolling textures and outlines, per draw.
- **Particles and billboards.** Bursts and emitters (fire, smoke, sparks that bounce), simulated deterministically, drawn instanced; camera-facing pictures and text (speech bubbles) placed in 3D and hidden by walls.
- **Sky and tilt-shift.** A gradient sky that meets the fog at the horizon; a miniature-look blur band.
- **Collision.** Ray tests, a static collision world in a BVH, and a capsule character controller (slopes, steps, walls).
- **UI.** Immediate-mode widgets (buttons, sliders, toggles, lists, text fields) from `tick`, with disabled states, text that shrinks to fit, keyboard and gamepad navigation, a sound-settings panel, and a tunables inspector (` with `--dev`).
- **Simulation.** Fixed-step and deterministic; input is recorded for exact replays; optional save/load-state callbacks give rewind and time travel.
- **Multiplayer (0.8).** Turn-based LAN sessions hosted by a player's own game: game commands are the network messages, applied in the same order everywhere; alternating turns, a ready-made lobby, LAN discovery, rejoin and resume, desync detection with reports, and deterministic maths (`arc_sinf`, `arc_powf`...) that give the same bits on every platform.
- **Agent interface.**
  - Scripted keys and mouse.
  - JSON state dumps, PNG screenshots, animated GIFs and screenshot comparison.
  - UI widgets listed and clicked by label.
  - Tunable variables and custom debug commands.
  - An interactive `--stdin` mode.
- **Audio.** MP3 (its own decoder), QOA and WAV files, streamed music, synthesised effects, controllable voices (pitch, pan, loop), positional 3D sound, low-pass filters, reverb, PCM streams, volume buses (music, effects, UI, voice), a 32-voice mixer.
- **Smaller downloads, faster iteration.** Compact `.arcm` v3 meshes, compressed `.arcpak` asset packs; assets reload when saved (`--hot`) and game code can reload while it runs (`./build.sh hot`).
- **Profiling.** An FPS overlay with a frame-time graph (`--fps`, F3 with `--dev`, or `arc_show_fps`); profiler zones in `stats` and dumps.

## Quick start

```sh
./build.sh test                       # self-test, headless
./build.sh linux                      # TOY BOX BLITZ sample with a window  -> build/demo
./build.sh headless && build/demo_headless --headless --cmd "step 120; press D; step 60; shot a.png; dump; quit"
./build.sh web                        # -> build/web/demo.html (serve over http)
build.bat                             # Windows, from a VS Developer Prompt -> build\demo.exe
python tools/new_game.py ../MyGame     # a new game; its build.bat/build.sh have web and release (itch.io zips)
```

To start a game, run `python tools/new_game.py D:\Projects\MyGame` (see below).

## Docs

| Doc | What it covers |
|---|---|
| [docs/ARC.md](docs/ARC.md) | Architecture, how the engine works, performance, file formats, extending it |
| [docs/API.md](docs/API.md) | Every public function and type (generated from `include/arc.h`) |
| [docs/SHARING.md](docs/SHARING.md) | Using ARC from other projects: `new_game.py`, shared vs vendored, versioning |
| [docs/MIGRATION.md](docs/MIGRATION.md) | Plan for moving existing games to ARC, and the engine features each needs |
| [docs/MULTIPLAYER.md](docs/MULTIPLAYER.md) | Adding turn-based LAN multiplayer to a command-driven game: steps, lobby, testing with agents, pitfalls |
| [docs/split-screen.md](docs/split-screen.md) | Several players in one window: viewports, per-player cameras and HUDs, picking, gamepads |
| [AGENTS.md](AGENTS.md) | The build, run and inspect loop for AI agents, plus the command reference |
| [CHANGELOG.md](CHANGELOG.md) | Engine versions |

## New game in one line

```bat
python tools\new_game.py D:\Projects\MyGame --title "My Game"
```
