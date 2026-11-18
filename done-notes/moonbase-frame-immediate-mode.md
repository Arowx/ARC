# Request: `frame` in immediate mode should list markers, labels and where targets land

From: Moonbase Raid, after ARC 0.10.7 / 0.10.8 (thanks for frame-in-targets: meshes inside the map target now list
well, with `"target": "map"` and `pass`).

## Problem
Moonbase Raid runs with `arc_app.immediate`, so `frame` gets what it shows from `render_now()` →
`arc__gfx_render_last()`, which replays the recorded frame. In that replay:
- `frame.markers` is empty. `arc_draw_marker` records into `mk_recs` only when it is called *while* `g_fr.on` is set.
  In immediate mode it was called during the tick, before `frame` turned recording on, and the replay only draws the
  queued batch (it shows up as a mesh named `marker`). A debug print in `arc_draw_marker` showed `rec=0` on every call.
- `frame.texts` / `sprites` / `rects` for the 2D drawn into targets are empty: marker labels, the game's HUD text in the
  `ui` target, and so on. This looks like the same cause: the 2D record lists fill at call time.
- Mesh items inside the `map` target come back `"on_screen_box": false`, although the game blits `map` (and `ui`) to
  the screen with `arc_draw_texture_ex`. `fr_blit` is also called at draw time, so the blits are never recorded either.

Seen on: `--headless --seed 7 --mission 3 --put 0 10 3 --cmd "step 30; view3d on; focus 10 3; cam3d 45 9; step 20; frame"`.
The droid next to the squad has a red ring marker with the label "!".

## Ask
In immediate mode, keep the frame-recording information (markers, 2D items, target blits) with the recorded frame, or
record it on every immediate frame, and have `frame` report what was kept for the frame it replays. A cheap
alternative: when `immediate` is set, `frame` waits one tick with recording on, so the next tick records itself.

## Check
```arc-check
exe ../../Moonbase Raid/port/build/game/moonbase_raid.exe
args --seed 7 --mission 3 --put 0 10 3
step 30
do view3d on; focus 10 3; cam3d 45 9
step 20
expect frame.markers[shape=ring] exists
expect frame.markers[label=!].target == "map"
expect frame.texts[*].text contains "!"
expect frame.meshes[name=trooper].items[0].on_screen_box != false
```

Until then the game counts what it drew itself (`game.drawn3d.engaged`), and its tests read that.

## Done in ARC 0.10.9

ARC took the first option: in immediate mode it records every tick. Markers, labels, texts, sprites, rects, target
passes and blits to the screen are all kept with the frame, and `frame` reports the frame it replays. The re-run ticks
of `seek` and `rewind` record too. Games with `draw()` behave as before.

- Your check should pass as written: `shape=ring` is the right spelling (marker shapes are names), and
  `frame.markers[label=!].target == "map"`, `frame.texts[*].text contains "!"` and the trooper's `on_screen_box` all
  come from the recorded tick now.
- ARC's own copy of your case is `tests/target_immediate_check.md`. It runs the 0.10.7 target test game
  (`tests/target_game.c --immediate`), which draws a target in `tick()` and blits it with `arc_draw_texture_ex`. It has
  a ring marker labelled "!", a text inside the target and HUD text. 12/12 checks pass; on 0.10.8 it passed 5/12, the
  same symptoms you saw.
- Once your check passes, `game.drawn3d.engaged` and the tests that read it can come out.
- `ARC_HAS_IMMEDIATE_FRAME` (1009) marks it.
