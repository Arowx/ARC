# Drawing meshes and mesh styles

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


Draw a mesh. tint multiplies vertex colours (0xFFFFFF = unchanged). emissive 0..1 adds glow.
0.9.0 names, shown by the `frame` agent command: a loaded mesh or texture is named after its file (no folder, no
   extension), primitives after their kind ("cube", "sphere", ...); set your own for things made in code.

```c
ARC_API void arc_mesh_set_name(arc_mesh m, const char *name);    // up to 31 characters
ARC_API const char *arc_mesh_name(arc_mesh m);    // "" if none
ARC_API void arc_texture_set_name(arc_texture t, const char *name);
ARC_API const char *arc_texture_name(arc_texture t);
ARC_API void arc_draw_mesh(arc_mesh m, arc_m4 model, uint32_t tint);
ARC_API void arc_draw_mesh_ex(arc_mesh m, arc_m4 model, uint32_t tint, float emissive);
```

Recolour a mesh while keeping its light/dark detail (team colours, ownership, damage):
   colour = lerp(vertex colour, tint * (0.35 + luminance), mix); then lit; whiten 0..1 lifts the result
   toward white (hit flashes, glow). Unlike arc_draw_mesh, tint does not multiply here.
   Mirrored model matrices (negative determinant) are fine with every arc_draw_mesh* call.

```c
ARC_API void arc_draw_mesh_tinted(arc_mesh m, arc_m4 model, uint32_t tint, float mix, float emissive, float whiten);
```

See-through and glowing meshes: drawn after everything solid, sorted back to front, without writing depth.
   alpha 0..1 (1 = solid look, still sorted); additive adds light (sparks, magic, spore clouds). The vertex
   colours' alpha and the texture's alpha multiply in, so a mesh can fade out at its edges (soft shadows, pools).

```c
ARC_API void arc_draw_mesh_alpha(arc_mesh m, arc_m4 model, uint32_t tint, float alpha);
ARC_API void arc_draw_mesh_additive(arc_mesh m, arc_m4 model, uint32_t tint, float intensity);
```

Every option in one call (the functions above are shortcuts). Zero means "default" for every field.
0.10.1 paint patterns drawn by the shader (MechBattleArc's request), from the mesh's bind-pose (model space)
   position, so they stay put on skinned meshes as they move; every renderer, static and skinned meshes. The pattern's
   colour is applied as paint is: rgb x (0.35 + luminance), so shading and wear survive.
   - STRIPES: bands across the model's front (x, y) plane at `angle` (0 = vertical bands); `cross` adds a second set at
     90 degrees (crosshatch).
   - CHECKER, DOTS: on each face's own plane (the axis its normal points along most), so squares stay square and dots
     round; `angle` turns them on the face.
   - SPLIT: one side of a line through the model's front plane at `angle` (0: x > offset).
   - NOISE: value-noise blobs (camo), `seed` picks the pattern.
   scale: one repeat in model units (0 = 0.25); width: the share in the pattern's colour (0 = 0.5); offset: moves it
   (in repeats; SPLIT: in model units). part_mask (ARC_PART_* bits, 0 = all): only these parts of a skinned mesh, from
   each vertex's strongest joint (names: Head, Spine*, *Arm* / *Hand* / *Shoulder* / *Clavicle*, *Leg* / *Foot* /
UpLeg*, others torso; arc_paint_parts_rule adds the game's own); a static mesh counts as torso. With arc_mesh_style.armour_mask the pattern goes only where paint_mask's red is (trim and
   joints keep tint2 / tint3). Draws with the same pattern still batch together.

```c
enum { ARC_PAINT_NONE = 0, ARC_PAINT_STRIPES, ARC_PAINT_CHECKER, ARC_PAINT_DOTS, ARC_PAINT_SPLIT, ARC_PAINT_NOISE };
enum { ARC_PART_HEAD = 1, ARC_PART_TORSO = 2, ARC_PART_ARMS = 4, ARC_PART_LEGS = 8, ARC_PART_ALL = 15 };
typedef struct { int kind; uint32_t rgb; float scale, width, angle, offset; bool cross; uint32_t seed; uint8_t part_mask; } arc_paint_pattern;
```

0.10.6: joints whose name matches `pattern` (case ignored; '*' any run, '?' one character, e.g. "*Pauldron*") belong
   to `part` (an ARC_PART_* bit; 0 removes the rule). Tried before the built-in names, the latest rule first; up to 32.
   pattern NULL or "" removes them all.

```c
ARC_API void arc_paint_parts_rule(const char *pattern, int part);
typedef struct { uint32_t tint; float recolour; float emissive; float whiten; float transparency; bool additive; arc_anim anim; const arc_pose *pose; float dissolve; uint32_t dissolve_rgb; float dissolve_glow; float dissolve_width; float hologram; float scroll_u, scroll_v; float outline; uint32_t outline_rgb; int depth; float depth_pull; int layer; int order; float xray; uint32_t xray_rgb; float uv_rotate, uv_scale; arc_v2 uv_offset; bool uv_mirror; arc_texture paint_mask; uint32_t tint2; uint32_t tint3; int tex_layer; float darken; float charred; bool no_world_mask; arc_paint_pattern pattern; bool armour_mask; } arc_mesh_style;
```

0.9.9: a mesh takes its colour from a texture array (arc_texture_array) instead of its texture, at the layer each draw
   picks (arc_mesh_style.tex_layer, arc_draw_mesh_layer); {0} back to its own texture. Draws of the mesh with different
   layers stay one batch (one draw call per mesh).

```c
ARC_API void arc_mesh_set_texture_array(arc_mesh m, arc_texture array);
ARC_API void arc_draw_mesh_layer(arc_mesh m, arc_m4 model, uint32_t tint, int layer);
```

0.9.9 a tile grid in one call (Moonbase Raid's request): every cell picks one of `meshes` (its index, flags
   ARC_TILE_EMPTY to skip), a layer of that mesh's texture array, a quarter turn about +y and a tint (0 = white). Cells
   are `cell` wide from `origin` (cell (0, 0) spans origin.x .. + cell, origin.z .. + cell), y = origin.y; each mesh
   is centred on its cell, as built-in shapes are, scaled by `cell` in x and z (scale y yourself in the mesh). One
   draw call per mesh kind, however many cells and layers.

```c
enum { ARC_TILE_EMPTY = 1, ARC_TILE_MIRROR = 2 };
typedef struct { uint8_t mesh, layer, turn, flags; uint32_t tint; } arc_tile_cell;
ARC_API void arc_draw_tiles(const arc_tile_cell *cells, int w, int h, float cell, arc_v3 origin, const arc_mesh *meshes, int mesh_count);
```

0.9.9 fog of war for every draw at once: `mask` is a small texture, one texel per tile (cell (0, 0) at origin x, z,
   `cell` wide); its red channel is how visible the tile is: 255 seen, about 80 remembered, 0 never seen. Everything lit
   or unlit that ARC draws in 3D (meshes, skinned meshes, decals, stamps, particles, markers) is darkened toward dark_rgb
   by it, with soft edges between tiles (softness 0 = hard tile steps, 1 = smooth across a whole tile), the same on
   every renderer. Update the texture (arc_texture_update) when vision changes; outside it the edge texels carry on.
   Leave a mesh out with arc_mesh_style.no_world_mask. {0} turns it off. Lasts until changed.

```c
ARC_API void arc_set_world_mask(arc_texture mask, arc_v3 origin, float cell, uint32_t dark_rgb, float softness);
```

0.9.9 cutaway: meshes marked with arc_mesh_set_cutaway (walls, door frames) lose what is above keep_height within
   `radius` of the line from eye to focus, seen from above, so the units behind them show. mode ARC_CUTAWAY_CLIP cuts
   cleanly; ARC_CUTAWAY_DITHER fades in over 0.6 units of height and the last quarter of the radius. Cut walls show
   a dark cap where they are open, and still cast their full shadows. Usually eye = the camera's eye, focus = what it
   looks at; set it each frame as the camera moves. mode ARC_CUTAWAY_OFF (or radius 0) turns it off.

```c
enum { ARC_CUTAWAY_OFF = 0, ARC_CUTAWAY_CLIP = 1, ARC_CUTAWAY_DITHER = 2 };
ARC_API void arc_set_cutaway(arc_v3 eye, arc_v3 focus, float radius, float keep_height, int mode);
ARC_API void arc_mesh_set_cutaway(arc_mesh m, bool on);
```

depth: DEFAULT tests and (solid meshes) writes; TEST_ONLY tests but never hides what is drawn after it (glows,
   ground effects); ON_TOP ignores depth (markers that show through everything); HIDDEN draws only where something
   nearer already covers it (x-ray). ON_TOP and HIDDEN go in the overlay layer unless a layer is given.

```c
enum { ARC_DEPTH_DEFAULT = 0, ARC_DEPTH_TEST_ONLY = 1, ARC_DEPTH_ON_TOP = 2, ARC_DEPTH_HIDDEN = 3 };
```

layers, in drawing order inside a pass: BACKGROUND (sky domes, backdrops), OPAQUE (solid meshes), then immediate
   triangles and lines, TRANSPARENT (see-through and additive, far to near), OVERLAY (last: markers, x-ray, gizmos).
   AUTO picks OPAQUE or TRANSPARENT from the style (OVERLAY for ON_TOP / HIDDEN depth).

```c
enum { ARC_LAYER_AUTO = 0, ARC_LAYER_BACKGROUND = 1, ARC_LAYER_OPAQUE = 2, ARC_LAYER_TRANSPARENT = 3, ARC_LAYER_OVERLAY = 4 };
ARC_API void arc_draw_mesh_styled(arc_mesh m, arc_m4 model, const arc_mesh_style *style);
```

0.8.8: the posed draw with a whole arc_mesh_style (outline, x-ray, recolour, paint mask, dissolve, ...): the same as
   arc_draw_mesh_styled with st->anim and st->pose set to a and pose. NULL st = plain.

```c
ARC_API void arc_draw_mesh_posed_styled(arc_mesh m, arc_m4 model, arc_anim a, const arc_pose *pose, const arc_mesh_style *st);
```
