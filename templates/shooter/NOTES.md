## This game started from the shooter template

- State: `Game G` with fixed pools (`MAX_BULLETS`, `MAX_ENEMIES`); `save_state`/`load_state` copy it whole. Keep it
  pointer-free when you add fields.
- Flow: `PHASE_PLAY` -> (wave cleared) `PHASE_BETWEEN` for 90 ticks -> `start_wave(n + 1)`; `PHASE_OVER` at 0 lives.
- Aim follows the mouse only when the mouse moves, so the `aim DEG` command holds in scripted runs.
- Agent commands: `aim DEG`, `fire`, `wave N`, `spawn N`, `spawn_at X Z`, `clear`, `lives N`, `god`, `restart`.
- Tunables: `speed`, `enemy_speed`, `fire_rate`, `bullet_speed`.
- Tests: `tests/acceptance.md`.
