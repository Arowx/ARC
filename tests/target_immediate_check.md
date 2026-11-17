# Request (test fixture): `frame` in immediate mode (0.10.9)

**Why.** Moonbase Raid runs with `arc_app.immediate`: it draws in its tick, and `frame` only replays that picture, so
markers, texts and target blits drawn in the tick were not in `frame` (0.10.7 listed only the meshes). ARC now
records every immediate tick's frame as it is drawn and `frame` reports it. These are tests/target_check.md's checks,
with the fixture drawing in its tick (`--immediate`).

**Ask.** Nothing; `tests/target_game.c --immediate`.

## Acceptance

```arc-check
game tests/target_game.c
args --immediate
do step 3
expect frame.meshes[name=sentry].on_screen == 1
expect frame.meshes[name=trooper].count == 2
expect frame.meshes[name=sentry].items[0].target == "view3d"
expect frame.meshes[name=sentry].items[0].box[0] > 400
expect frame.meshes[name=sentry].items[0].target_box[0] < 300
expect frame.targets[target=view3d].on_screen_at[2] == 1280
expect frame.markers[shape=ring].label == "!"
expect frame.markers[shape=ring].target == "view3d"
expect frame.texts[text=!].box[0] > 700
expect frame.texts[text=IN VIEW].target == "view3d"
expect frame.texts[text=HUD 3 UNITS].target missing
expect frame.sprites[texture=view3d] exists
```
Described in words: the same answers as without --immediate.
