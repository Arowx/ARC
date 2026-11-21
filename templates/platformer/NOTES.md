## This game started from the platformer template (Hop Hero)

- **Level:** the `LEVEL` strings (`#` ground, `o` coin, `^` spikes, `s` slime, `C` checkpoint, `F` flag, `P` start). Row 0
  is the top; `tile(cx, cy)` counts `cy` up from the bottom. One tile = one unit.
- **Physics:** `move_and_collide` moves along x then y and pushes the player's box (half width `PW`, height `PH`) out of
  `#` tiles. Jumps use coyote time (`coyote`) and a jump buffer (`buffer`), 6 ticks each; letting go cuts the rise.
- **Slimes:** patrol between walls and ledges. Landing on one from above squashes it and bounces you; touching it from
  the side respawns you like spikes do.
- **Juice:** squash and stretch (`G.squash`), dust on jumps, landings and runs, coin sparkles whose pitch climbs while
  you chain them, stomp pops, a camera that leads where you run and shakes on hits, popups (`arc_popup_text`),
  varied pitch (`arc_play_var`), confetti and the `win` dance at the flag.
- **Assets:** made with Ludo (ludo.ai). `assets/manifest.json` lists every file: how it was made, a description, and
  for models the `arc_import.py` options. Put the source GLBs in `art/` and
  `python3 <ARC>/tools/arc_import.py --manifest --game .` re-imports every model the same way. Import new models
  with `--prompt "..."` and they are recorded too. Models face +z (check the green FRONT +Z arrow in
  `build/import/NAME.png`). The hero has the clips idle, run, jump and win.
- **State:** `Game G` is pointer-free; `save_state`/`load_state` copy it whole.
- **Agent commands:** `tp X Y` (feet position), `kill`, `win`, `restart`.
- **Tunables:** `run_speed`, `jump_speed`, `gravity`, `slime_speed`.
- **Tests:** `tests/acceptance.md` has two blocks: movement and the level, and slimes and juice.
