# Request from Moonbase Raid: what would make a grid tactics game in 3D easier (after 0.9.7)

Moonbase Raid's 3D view (`game/src/view3d.cpp`, about 1,500 lines) draws a turn-based tile map: a floor and walls per
tile, doors, objects, units, fog of war and a cutaway near the camera. These are the places where the game does by
hand, or offline, something ARC could do once for every grid game (Moonbase Raid, Operation Nimrod, Operation
Chariot, the mech game). Most important first.

## 1. A grid renderer: all the floor and wall tiles in one call

**Now.** Each floor tile is its own `arc_draw_mesh`. Giving tiles different looks means a separate mesh per texture:
- 4 dirt levels x 6 patterns x 2 tints (rooms and corridors);
- 4 regolith variants outside;
- 12 older worn variants;
- wall, cut-wall, hull and top meshes.

That is about 70 meshes, all the same box with a different texture. ARC batches by mesh, so it is 70-odd batches a
frame. `arc_texture_array` stops at 4 layers, and an instance can't choose its layer.

**Ask.**
- Texture arrays of up to 32 or 64 layers (same size), and a per-instance layer index
  (`arc_draw_mesh_layer(mesh, model, tint, layer)`, or a field in the instanced path).
- Or, higher up, a grid draw:

```c
typedef struct { uint8_t mesh, layer, turn, flags; uint32_t tint; } arc_grid_cell;   /* turn: quarter turns */
void arc_draw_grid(const arc_grid_cell *cells, int w, int h, float cell, arc_v3 origin,
                   const arc_mesh *meshes, int mesh_count, arc_texture_array layers);
```

One draw per mesh kind, with the layer, turn and tint per cell. It would also suit the mech game's hex board and
Chariot's dockyard.

## 2. A world fog-of-war mask

**Now.** Every tile, object and unit draw picks its own tint: full for visible, about 0.3 for remembered, skipped for
unseen. Edges between seen and unseen are hard tile steps, and every draw path has to remember to do it.

**Ask.** `arc_set_world_mask(arc_texture mask, arc_v3 origin, float cell, uint32_t dark_rgb, float softness)`: a small
texture (one texel per tile: 255 = visible, ~80 = remembered, 0 = unseen) sampled in world x, z by every lit draw
(meshes, skinned meshes, decals, stamps, billboards optionally), multiplying towards `dark_rgb`, with bilinear soft
edges. The game updates the texture when vision changes (a few hundred bytes). `arc_set_world_mask({0}, ...)` turns
it off.

## 3. A built-in cutaway

**Now.** Walls near the camera are swapped for cut-down copies (separate wall, hull, door and frame meshes at a lower
height) when they are inside a radius of the camera's focus and between it and the camera. That means four extra
meshes and their own logic for doors.

**Ask.** A cutaway volume: `arc_set_cutaway(arc_v3 eye, arc_v3 focus, float radius, float keep_height, int mode)`.
Meshes flagged `ARC_MESH_CUTAWAY` (or a style bit) lose their parts above `keep_height` inside the capsule from eye to
focus (mode: clip, or dither-fade over a band), with a cap or darkened rim where they are cut. Shadows still come
from the full walls.

## 4. Tint that reaches textured models, and run-time darkening

**Now.** `arc_mesh_style.tint` multiplies vertex colour. On Ludo's textured models (white vertex colour, the colour all
in the texture) a dark tint barely shows. `recolour` replaces hue, not brightness. To make wrecks and bodies look
burnt, the game ships *charred copies* of models, made by an offline script (`tools/char_glb.py`: darkens the GLB's
base-colour texture, adds soot mottling and bright scratches).

**Ask.** Either:
- `tint` multiplying the sampled texture too (or a `tint_texture` flag); or
- styles for it: `darken` (0..1, towards black with the texture's detail kept), and `char` (darken plus soot noise
  plus bright scratches, world- or UV-mapped, so a wreck looks burnt without a second asset).

Also `arc_image_*` operations on a loaded texture (multiply, overlay another image, noise), so games can make
variants at load time instead of offline.

## 5. `arc_import`: exact fitting and a size report

**Now.** Door leaves, frames and some furniture needed exact sizes. The game converted with `--height 1`, measured
the result, and typed scale factors into the code (for example a 0.48 x 1 x 0.08 leaf scaled by 0.92, 1.29, 1).

**Ask.** `--fit X Y Z` (scale each axis to fit, or `--fit-uniform` to fit inside), `--origin bottom|centre|back`, and
the final bounds printed and written in `--json`, so code can read them rather than having them hard-coded.

## 6. Feature macros in `arc.h`

**Now.** ARC's version number lags its unreleased changes, so Moonbase Raid's CMake greps `arc.h` for
`arc_target_set_post` and `arc_draw_stamps` to decide what to switch on.

**Ask.** `#define ARC_HAS_TARGET_POST 1`, `ARC_HAS_STAMPS`, `ARC_HAS_TERRAIN` and so on in `arc.h` for each feature
group (or `ARC_FEATURE_LEVEL` that rises with each one), so games can `#if` on them directly.

## 7. A fast screenshot mode for headless runs

**Now.** Checking a 3D change means a headless run: `--frames 100 --shot out.png`. The software renderer draws every
frame, so one 1280 x 800 shot with post effects takes 1-3 minutes, and runs past about 200 frames time out. Almost
all of that time is frames nobody looks at.

**Ask.** A headless option to tick without drawing until the last frame (or the last N): `--draw-last 2`. Better,
also `--shot-at 40,80,120` to save several frames from one run (an explosion's start, middle and end) while drawing
only those.

## 8. Small font sizes are blank in headless builds

**Seen.** In headless software-renderer screenshots, button labels at size 7 (the game's tip buttons "GOT IT" / "NO
MORE TIPS") come out blank; size 8 draws. It may be the font's smallest baked size. Either draw it (scaled from the
nearest size) or log a hint once.

## Already adopted from 0.8.8-0.9.7

- World-mapped hex base on the regolith (thanks for the fix).
- Tiled lights and point-light shadows for room lamps.
- Next: animation graphs and events (footsteps, the Rewritten twitching) and `frame` / `arc_check` tests.

**Test cases.**
- (1) A 64 x 64 grid with 32 layers: one draw call per mesh kind (`arc_stats`), each cell showing its own layer and
  turn.
- (2) A mask half visible, half unseen: a soft ramp across the cell edge, and mesh, decal and stamp all darkened
  alike.
- (3) A wall row between eye and focus: cut inside the radius, whole outside, shadows unchanged.
- (7) `--draw-last 1 --frames 600` faster than `--frames 60` today, with the same final picture.

---

## Partly done in ARC 0.9.8 (15 Nov 2026): items 5-8

Items 1-4 (the grid renderer with 64-layer arrays and a layer per instance, the world fog-of-war mask, the built-in
cutaway, and tint/darken/char on textured models) are planned for **ARC 0.9.9**, together with MechBattleArc's grid
tactics and ground markers. This file stays in `incoming` until then.

**5. arc_import: exact fitting and a size report.**
- `--fit X Y Z` scales each axis to exactly that size. Example: `--fit 0.48 1 0.08` for the door leaf. A 0 on an axis
  makes that axis take the smallest of the other scales.
- `--fit-uniform X Y Z` uses one scale so the model fits inside the box.
- `--origin bottom|centre|back` sets where (0, 0, 0) goes:
  - `bottom` (the default): centred on x and z, feet at y = 0;
  - `centre`: the middle of the box;
  - `back`: centred on x, feet at y = 0, the back face at z = 0.
- These work alone or with `--height` / `--footprint`, on GLB and OBJ.
- The final bounds are printed (`bounds min ... max ... size ...`) and are in `--json` as
  `"bounds": {"min", "max", "size"}`, so code can read them instead of hard-coding scale factors.
- `mesh2arcm.py` takes the same options.

**6. Feature macros.**
- `arc.h` now has `ARC_HAS_NET`, `_ROLLBACK`, `_TARGET_POST`, `_STAMPS`, `_RELAY`, `_PACK_FETCH`, `_REFLECTIONS`,
  `_PAINT_MASK`, `_TINT3`, `_FRAME_COMMAND`, `_NAVMESH`, `_ANIM_GRAPH`, `_PHYSICS`, `_LIGHT_SHADOWS`, `_TERRAIN`,
  `_JOINTS`, `_RAGDOLL`, `_IMPORT_FIT` and `_HEADLESS_BATCH`.
- Each one's value is the `ARC_VERSION_NUM` it arrived in.
- Your CMake can test `ARC_HAS_TARGET_POST` and `ARC_HAS_STAMPS` instead of searching for function names.

**7. Fast headless screenshots.**
- Your runs were slow because ARC drew render targets (with their post effects) on every headless tick for
  immediate-mode games. Your 3D view is in such a target.
- `--draw-last N` draws only the last N ticks before `--frames` ends, or before a shot.
- `--shot-at 40,80,120` takes screenshots after those ticks, drawn only then.
  - `40=start.png` names a shot; `--shot-name view_%d.png` sets the pattern for all of them.
  - Without `--frames`, the run ends after the last shot.
- Measured on a test game with a post-processed target: 120 ticks with a final shot went from 44 s to 0.85 s, and the
  picture was identical.
- Use `--draw-last 2` or more if a look depends on earlier frames.
- Your case: `--frames 600 --draw-last 2 --shot-at 599=out.png`, or `--draw-last 2 --shot-at 40,80,120` for the
  explosion.

**8. Small fonts blank.**
- I couldn't reproduce this.
  - The built-in font at 5-10 px draws, though it is rough below 8 px because rows drop out.
  - So do UI buttons at theme scales down to 6 px, and a baked 16 px font drawn at 5-10 px.
- ARC now prints a one-time `ARC hint` when the built-in 8 px font is drawn below 8 px, or a baked font below 60% of
  its size.
- If the labels are still blank, please add to this file the exact call: the function, font file, size or scale, and
  flags. Then I can find the real cause.
- Sharp text at any size is on the list with MechBattleArc's SDF font request (planned for 0.10.1).

Tests: `tests/headless_tools_test.py` (a new "headless tools" stage in `build test`) checks:
- `--draw-last` and `--shot-at` give the same picture as drawing every tick;
- `--shot-at` stops after its last shot;
- `arc_import --fit` / `--origin` produce the expected bounds.

---

## Done in ARC 0.9.9 (15 Nov 2026): items 1-4

**1. Grid renderer.**
- Texture arrays now take up to 64 layers (`ARC_TEXTURE_ARRAY_MAX`, all the same size).
- `arc_mesh_set_texture_array(mesh, array)` makes a mesh take its colour from the array at a layer chosen per draw:
  `arc_draw_mesh_layer(mesh, model, tint, layer)` or `arc_mesh_style.tex_layer`. Different layers still batch.
- `arc_draw_tiles(cells, w, h, cell, origin, meshes, mesh_count)` with `arc_tile_cell { mesh, layer, turn, flags,
  tint }`. It is named `arc_draw_tiles` because `arc_draw_grid` already existed (the debug grid). The array goes on
  each mesh rather than in the call. `ARC_TILE_EMPTY` skips a cell; `ARC_TILE_MIRROR` mirrors it.
- Your ~70 floor meshes become one box mesh and one array: one draw call per mesh kind.
- Test: a 16 x 12 grid from 2 meshes and 32 layers, every cell showing its own layer and turn, 2 draw calls for 192
  instances, on all renderers.

**2. World fog-of-war mask.** `arc_set_world_mask(mask, origin, cell, dark_rgb, softness)` exactly as you asked.
- Red channel: 255 seen, ~80 remembered, 0 unseen.
- It darkens everything ARC draws in 3D: meshes, skinned meshes, decals, stamps, particles and markers.
- Soft edges: softness 0 gives hard tile steps, 1 a ramp across a whole tile.
- `arc_mesh_style.no_world_mask` leaves a mesh out (cursors, selection meshes).
- Update the texture with `arc_texture_update` when vision changes. `{0}` turns it off.

**3. Cutaway.**
- `arc_mesh_set_cutaway(mesh, true)` once per wall, hull, door or frame mesh. It is read when the frame is drawn, so
  set it at load time, not around single draws.
- Then `arc_set_cutaway(eye, focus, radius, keep_height, ARC_CUTAWAY_CLIP | ARC_CUTAWAY_DITHER)` each frame.
- Above keep_height and within `radius` of the eye-to-focus line (seen from above), walls are removed. Dither fades
  over 0.6 units of height and the last quarter of the radius.
- Cut walls draw double-sided, so the inside shows as a dark grey cap.
- Shadows still come from the whole walls. Your four cut-down meshes and their logic can go.

**4. Tint on textured models.**
- `arc_mesh_style.darken` (0..1): towards black, with the texture's detail kept.
- `arc_mesh_style.charred` (0..1): darkened, with soot mottling and a few bright scratches, mapped to the model. This
  replaces `tools/char_glb.py` and the charred copies.
- At load time: `arc_image_multiply`, `arc_image_overlay` (normal / multiply / screen / overlay / add, stretched or
  tiled), `arc_image_noise` (wraps, so tiling textures still tile) and `arc_image_char`, all in place on
  `arc_image_load` pixels before `arc_texture_create`.
- `tint` itself is unchanged, so existing games look the same.

Feature macros: `ARC_HAS_TILES`, `ARC_HAS_WORLD_MASK`, `ARC_HAS_CUTAWAY`, `ARC_HAS_DARKEN` (all 909).

Your test cases:
- (1) as above;
- (2) a mask half seen, half unseen: the unseen ground is black, the seen ground unchanged, a box left out of the mask
  unchanged;
- (3) a red unit behind a wall: hidden with the cutaway off, shown with clip and with dither.
These run on the software renderer, GL and D3D11.

Not covered: a 64 x 64 grid test (the 16 x 12 test checks the same code), and a check that arrays and tiles work with
`--no-assets` 1 x 1 stand-ins (all layers have the same size, so it should; tell me if not).
