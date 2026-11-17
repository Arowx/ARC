## This game started from the tactics template

- State: `Game G` holds everything (units, phase, turn); `save_state`/`load_state` copy it, so `rewind`, `seek` and the
  timeline work. Keep it pointer-free when you add fields.
- Turn flow: `PHASE_PLAYER` -> `end_player_turn()` -> `PHASE_ENEMY` (one red unit acts every 20 ticks) ->
  `start_player_turn()`. Win/lose in `check_end()`.
- Paths: `arc_grid_path_length` / `arc_grid_path_step` over `occ` (walls + other units, rebuilt by `build_occ`).
- Agent commands: `select N`, `move X Y` (a click on that cell), `attack N`, `end`, `win`, `restart`.
- Tunables: `move`, `attack`, `hp` (`set move 4`).
- Tests: `tests/acceptance.md` (`python <ARC>/tools/arc_check.py tests/acceptance.md`).
