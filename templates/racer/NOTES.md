## This game started from the racer template

- **Track:** `TRACK` is the centre line (a loop of points, also the checkpoints in order); `HALF_WIDTH` is half the
  road. `build_road()` makes the road and kerb meshes from the same points, so what looks like road is road
  (`on_track()`).
- **Cars:** `NCARS` karts in one `Car` array (0 is the player; the AIs are recoloured with `CAR_RGB`). `drive()` is the
  shared physics; `ai_input()` steers toward `TRACK[next_cp]`. Boost pads sit on segments 2, 6 and 10. A lap counts
  when `next_cp` comes back round to 0; `LAPS` laps finish the race.
- **Juice:** engine pitch follows speed (a looping voice), skids squeal and smoke, grass throws dust, bumps spark and
  shake, the camera lags and its field of view widens with speed, countdown numbers pop, lap and boost popups,
  confetti at the finish.
- **Assets:** made with Ludo (ludo.ai). `assets/manifest.json` lists every file: how it was made, a description, and
  for models the `arc_import.py` options. Put the source GLBs in `art/` and
  `python3 <ARC>/tools/arc_import.py --manifest --game .` re-imports every model the same way. Import new models
  with `--prompt "..."` and they are recorded too. Models face +z (check the green FRONT +Z arrow in
  `build/import/NAME.png`). The engine and skid loops are QOA files, because MP3 leaves a gap where a loop repeats.
- **State:** `Game G` is pointer-free; `save_state`/`load_state` copy it whole.
- **Agent commands:** `go` (skip the countdown), `tp_cp N` (place the car at checkpoint N facing the next), `lap N`,
  `ai off|on`, `boost`, `restart`.
- **Tunables:** `max_speed`, `accel`, `steer`, `grip`, `ai_skill`.
- **Tests:** `tests/acceptance.md` has two blocks: the race rules, and rivals, boost and props.
