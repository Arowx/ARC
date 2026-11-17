# Request (test fixture): `frame` describes what is drawn into render targets (0.10.7)

**Why.** Moonbase Raid draws its 3D view into a render target, puts it on the screen, and draws its HUD on top. `frame`
listed only the screen pass's meshes, so arc_check couldn't see a unit in that view. `tests/target_game.c` does the same
at half size (a 640x360 target scaled to 1280x720).

**Ask.** Nothing; the fixture already does this.

## Acceptance

```arc-check
game tests/target_game.c
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
Described in words: the meshes, the marker and the texts drawn into the target are listed, tagged with it, with boxes on
the screen (twice their place in the target) and in the target; the HUD drawn on the screen has no target.
