# Request: `frame` should describe meshes drawn into render targets

From: Moonbase Raid (3D view), Nov 2026

## Problem
Moonbase Raid draws its 3D view into a render target, so it can apply post effects and lay the game's 2D UI over the
top. It then blits that target to the screen. `frame` (`arc__frame_json`) describes only the first screen pass that has
content. That means `frame.meshes`, `frame.markers` and `frame.texts` come back empty for the whole 3D view, and
arc_check can't see a droid, a wreck or an intent marker.
Only `lights` (global state) come through:

    ARC frame {"passes":9,"target_passes":4,"meshes":[],"markers":[],"texts":[], "lights":[...3...]}

Today the game works around it by counting what it drew itself and listing that in its dump (`game.drawn3d`).

## Ask
One of these, in order of preference:
1. `frame` also lists the content of target passes, with each mesh, marker and text tagged with its pass, e.g.
   `"pass": 2, "target": "t1"`. Screen-space boxes would be in the target's pixels, or mapped through the blit when
   ARC knows it.
2. `frame pass N` / `frame target` picks which pass to describe, and `frame passes` lists the passes with their item
   counts.
3. Let the game name the pass that holds the "main view" (e.g. `arc_target_set_name` plus a flag such as
   `arc_target_set_described(t, true)`), and have `frame` describe that one.

## Check
```arc-check
exe ../../Moonbase Raid/port/build/game/moonbase_raid.exe
args --seed 7 --mission 3 --put 0 10 3
step 30
do view3d on; focus 10 3; cam3d 45 9
step 20
expect frame.meshes[name=sentry] exists
expect frame.meshes[name=trooper] exists
expect frame.markers[shape=1] exists
expect frame.texts[*].text contains "!"
```

## Also seen
`ARC hint .../models/trooper.arcm loaded during tick()`. This one is the game's fault: the 3D view loads its models the
first time it opens. Moonbase Raid will move that into init or onto arc_load_begin. No change is needed in ARC.

## Done in ARC 0.10.7

Option 1. `frame` now also lists what is drawn into render targets (every pass into a target except planar-reflection
mirrors and split-screen viewports, which have their own lists):

- **Meshes** drawn into a target appear in `frame.meshes` like screen meshes. Each item also carries `"pass"`,
  `"target"` (the name from `arc_texture_set_name`, else `"#id"`) and `"target_box"` (in the target's pixels).
- **`box` is on the screen.** ARC now records where each target is drawn (`arc_draw_texture_ex`, `arc_draw_sprite`),
  and maps boxes through that, also when the target was first drawn into another target (up to 4 deep). A target never
  drawn on the screen keeps target pixels in `box` and says `"on_screen_box": false`. A target drawn rotated isn't
  mapped.
- **Texts, sprites and rects** drawn into a target get the same `target` / `target_box`, with `box` on the screen.
  Markers drawn into a target get `"target"`.
- **`frame.targets[*]`** lists each pass into a target: `pass`, `target`, `size`, camera `eye`, `meshes` (instances),
  and `on_screen_at` (where the whole target landed on the screen).
- `arc_draw_texture_ex` now shows in `frame.sprites`, so `frame.sprites[texture=view3d]` finds your blit if you name
  the target.
- `#ifdef ARC_HAS_FRAME_TARGETS` tells you it's there.

Checked by `tests/target_check.md` (12 checks; `tests/target_game.c` draws a 3D view into a 640x360 target, scales it
onto a 1280x720 screen and puts a HUD on top), on Linux and in a Windows (MinGW) build.

**Your check needs two changes:**
- Marker shapes are names, not numbers: write `frame.markers[shape=ring]` (or `tile`, `disc`, `arrow`, `chevron`,
  `diamond`), not `shape=1`.
- In the fixture, markers and marker labels (2D texts) already came through before this change, even inside a target.
  If yours were still empty, look at how the game draws them: a label drawn as a billboard (3D text) isn't a 2D text,
  and the `ui` list (not `texts`) holds `arc_ui_*` text. I couldn't run Moonbase Raid itself here (its folder isn't
  connected), so tell me if `markers` or `texts` stay empty with 0.10.7.

Name the target so checks can read it: `arc_texture_set_name(view, "view3d")`. Then, for example:

```arc-check
expect frame.meshes[name=sentry].items[0].target == "view3d"
expect frame.markers[shape=ring] exists
expect frame.texts[*].text contains "!"
```
