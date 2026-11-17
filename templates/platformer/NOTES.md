## This game started from the platformer template

- Level: the `LEVEL` strings (`#` ground, `o` coin, `^` spikes, `C` checkpoint, `F` flag, `P` start). Row 0 is the top;
  `tile(cx, cy)` counts `cy` up from the bottom. One tile = one unit.
- Physics: `move_and_collide` moves along x then y and pushes the player's box (half width `PW`, height `PH`) out of
  `#` tiles. Jumps use coyote time (`coyote`) and a jump buffer (`buffer`), 6 ticks each; letting go cuts the rise.
- State: `Game G` is pointer-free; `save_state`/`load_state` copy it whole.
- Agent commands: `tp X Y` (feet position), `kill`, `win`, `restart`.
- Tunables: `run_speed`, `jump_speed`, `gravity`.
- Tests: `tests/acceptance.md`.
