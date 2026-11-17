## This game started from the racer template

- Track: `TRACK` is the centre line (a loop of points, also the checkpoints in order); `HALF_WIDTH` is half the road.
  `on_track()` measures the distance to the nearest segment.
- Cars: one `Car` struct for the player and the AI; `drive()` is the shared physics, `ai_input()` steers toward
  `TRACK[next_cp]`. A lap counts when `next_cp` comes back round to 0. `LAPS` laps finish the race.
- State: `Game G` is pointer-free; `save_state`/`load_state` copy it whole.
- Agent commands: `go` (skip the countdown), `tp_cp N` (place the car at checkpoint N facing the next), `lap N`,
  `ai off|on`, `restart`.
- Tunables: `max_speed`, `accel`, `steer`, `grip`, `ai_skill`.
- Tests: `tests/acceptance.md`.
