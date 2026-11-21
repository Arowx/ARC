## This game started from the tactics template (Toy Skirmish)

- **State:** `Game G` holds everything (units, arrows in flight, pending impacts, phase, turn); `save_state` /
  `load_state` copy it, so `rewind`, `seek` and the timeline work. Keep it pointer-free when you add fields.
- **Content:** `KINDS[]` is one row per unit kind (team, hp, attack, range, model scale). `LEVEL` places them:
  `k`/`b` knight, `a` archer, `o`/`r` orc, `g` goblin, `#` ruin, `t` tree. Range > 1 needs a clear line
  (`arc_grid_los`).
- **Turn flow:** `PHASE_PLAYER` -> `end_player_turn()` -> `PHASE_ENEMY` (each red unit walks toward the nearest blue
  unit and attacks when it can, one after another) -> `start_player_turn()`. `check_end()` decides win or lose.
- **Rules first, looks after:** `move_unit` / `attack` change the state at once (tests read it straight away). The
  animation catches up on `G.clock`: units walk their `path[]` a tile every `STEP_TICKS`, then play `attack`; arrows
  fly (`Arrow`), and the hit lands later (`Pending`, `land_impacts`) with the flinch, number, sparks and sound.
- **Juice:** turn banner with a horn, hit flashes (`whiten`), damage numbers (`arc_popup_text`), varied pitch
  (`arc_play_var`), dust where units walk, the fallen play `death` and dissolve, confetti on a win, markers for
  moves and targets.
- **Assets:** made with Ludo (ludo.ai). `assets/manifest.json` lists every file: how it was made, a description, and
  for models the `arc_import.py` options. Put the source GLBs in `art/` and
  `python3 <ARC>/tools/arc_import.py --manifest --game .` re-imports every model the same way. Import new models
  with `--prompt "..."` and they are recorded too. Models face +z (check the green FRONT +Z arrow in
  `build/import/NAME.png`). Units have the clips idle, walk, attack, hit and death (`arc_anim_clip_find`).
- **Agent commands:** `select N`, `move X Y` (a click on that cell), `attack N`, `end`, `win`, `restart`.
- **Tunables:** `move`, `hp_scale`.
- **Tests:** `tests/acceptance.md` has two blocks: the rules, and the animation and juice (a red turn now takes up
  to about 200 ticks, because units walk).
