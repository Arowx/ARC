## This game started from the shooter template (Neon Arena)

- **State:** `Game G` holds fixed pools (`MAX_BULLETS`, `MAX_ENEMIES`, `MAX_CRYSTALS`, `MAX_CRATES`), and
  `save_state`/`load_state` copy it whole. Keep it pointer-free when you add fields.
- **Content tables:**
  - `KINDS[]` holds each enemy kind's speed, hit points, score, radius and model scale;
  - `POWERS[]` holds each power-up's colour and duration.
  - Add a kind or a power-up as a new row, then handle its behaviour in `tick_enemies` / `tick_crystals`.
- **Flow:**
  - `PHASE_PLAY` -> (wave cleared) `PHASE_BETWEEN` for 90 ticks -> `start_wave(n + 1)`. Every 5th wave adds a boss.
  - At 0 lives: `PHASE_DYING` (the death animation, 100 ticks) -> `PHASE_OVER`. R restarts.
  - Enemies pop in, then hold still for 30 ticks. Beetles rev up for 30 ticks (a red arrow shows where), then charge.
- **Juice:**
  - `G.hitstop` freezes the rules for a few ticks on a kill (3 ticks; a boss 14), while popups, the camera and
    particles keep going.
  - `shake()` is the camera rig's trauma.
  - `whiten` gives hit flashes; `arc_ease_out_back` drives pop-ins, banners and popups.
  - `play_var()` calls ARC's `arc_play_var` (a slightly varied pitch, from ARC's own random generator for looks, so the
    rules' `arc_rand_*` is untouched); `popup()` calls `arc_popup_text` (floating numbers that ARC ages and draws).
  - The `FX_*` tables are the particle effects; `arc_draw_marker` draws the player ring, spawn rings and charge arrows.
- **Assets:** the files in `assets/` were made with Ludo (ludo.ai) and imported with `tools/arc_import.py`. Their
  sizes and turns:

  | Asset | Import options |
  |---|---|
  | player | `--height 1.4`, plus `--anim idle.glb run.glb death.glb hit.glb` (the clips are named after the files) |
  | drone | `--height 0.8 --origin centre` |
  | beetle | `--footprint 1.4 --yaw 180` (the model faced backwards) |
  | crystal | `--height 0.9` |
  | crate | `--footprint 1.2` |
  | wall | `--fit 2.0 0.9 0.8` |

  All use `--texture-size 512`. The floor is a 512 px tile repeated with `uv_scale`. `assets/manifest.json` records
  every file (sounds, music and the floor too); `arc_import.py --manifest --game .` re-imports the models from `art/`.
- **Aim and facing:** aim follows the mouse only when the mouse moves, so the `aim DEG` command holds in scripted runs.
  Models face +z: `yaw = atan2f(dx, dz)`.
- **Agent commands:** `aim DEG`, `fire`, `wave N`, `spawn N [kind]`, `spawn_at X Z [drone|beetle|boss]` (awake at
  once), `crystal X Z [spread|rapid|shield]`, `clear`, `lives N`, `god`, `restart`.
- **Tunables:** `speed`, `enemy_speed`, `fire_rate`, `bullet_speed`, `combo_time`, `drop_chance`.
- **High score:** kept with `arc_save("best")`.
- **Tests:** `tests/acceptance.md` has four blocks: basics, beetles and combos, power-ups and crates, the boss wave.
