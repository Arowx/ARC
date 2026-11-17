# Scene speed-ups: cells, portals, LOD, static batches

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


---- Scene speed-ups (0.7.1): cells and portals, levels of detail, static batches, view tests ----------------------
   All optional: a game that calls none of these draws exactly as before. They work in every renderer (they decide
   what is queued; the renderers draw it as usual).

   View tests against the current camera (the one set last with arc_set_camera, for the pass being drawn):

```c
ARC_API bool arc_sphere_visible(arc_v3 center, float radius);    // false only when it is surely outside the view
ARC_API bool arc_aabb_visible(arc_v3 lo, arc_v3 hi);
```

Cells and portals: rooms (cells: one or more boxes under an id) joined by portals (doorways: a box, usually flat,
   between two cells) the game opens and closes. From the camera's cell, ARC walks through open portals, narrowing the
   view to each portal's rectangle on screen, to find which cells can be seen. A camera outside every cell (top-down
   views over a deck) sees each cell whose box is in view. Meshes drawn after arc_set_draw_cell(id) belong to that
   cell and are not queued while it cannot be seen (0 = no cell: always drawn; it resets to 0 every frame). Particles,
   decals, trails and billboards are never culled this way. The answer is worked out on demand for each camera and kept
   until the camera, the cells or a portal change. Note: a hidden cell's meshes cast no shadows either (keep shadow
   casters that reach through doorways untagged). Agents: `cells` prints what is seen, `cells off` / `cells on` turns
   the culling off and on, `cells draw` toggles a wireframe of cells (green seen, grey hidden) and portals (yellow
   open, red closed).

```c
#define ARC_MAX_CELLS 1024    // ids 1 .. 1023
ARC_API void arc_cell_add(int id, arc_v3 lo, arc_v3 hi);    // add a box to cell id (L-shaped rooms: several boxes)
ARC_API int arc_portal_add(int cell_a, int cell_b, arc_v3 lo, arc_v3 hi);    // returns the portal (1..), 0 if full; open
ARC_API void arc_portal_set_open(int portal, bool open);
ARC_API bool arc_portal_is_open(int portal);
ARC_API void arc_cells_clear(void);    // forget every cell and portal
ARC_API int arc_cell_at(arc_v3 p);    // the smallest cell box holding p (its id), 0 if none
ARC_API bool arc_cell_visible(int id);    // from the current camera; id 0 = true
ARC_API int arc_cells_visible_count(void);
```

The same for any camera and view size, with no drawing state (tick-safe): fills bits (ARC_MAX_CELLS / 32 words,
   bit id set = seen) and returns how many cells are seen.

```c
ARC_API int arc_cells_compute(const arc_camera *c, float view_w, float view_h, uint32_t *bits);
ARC_API void arc_set_draw_cell(int id);
ARC_API int arc_get_draw_cell(void);
ARC_API void arc_cells_set_culling(bool on);    // default on; off = tagged meshes always drawn
ARC_API void arc_cells_debug_draw(void);    // the wireframe above, as 3D lines now
```

Levels of detail: lower-detail meshes drawn in place of `base` for instances that look small. Up to 3 levels
   (1..3) besides the base. With arc_mesh_set_lod, level k is used when the instance's bounding sphere covers less than
   screen_size of the view's height (0.1 = a tenth; the same across field of view and resolution); with
   arc_mesh_set_lod_distance, when the sphere's centre is further than `distance` from the camera. Each level's
   threshold must be smaller (or further) than the one before. lod {0} removes a level. Picked per instance as it is
   queued, so shadows use the same level. Each level is a mesh of its own (texture, material, shadow casting): a level
   with no texture or material borrows the base's when set. Destroying a mesh removes it from any LOD list.

```c
ARC_API void arc_mesh_set_lod(arc_mesh base, int level, arc_mesh lod, float screen_size);
ARC_API void arc_mesh_set_lod_distance(arc_mesh base, int level, arc_mesh lod, float distance);
```

Quality setting: bias 2 keeps detail to twice the distance, 0.5 drops it at half (0 = 1). force: -1 automatic, else
   always draw that level (or the nearest the mesh has): for tests and comparisons. Agents: `lod`, `lod bias X`,
   `lod force N`.

```c
ARC_API void arc_set_lod_bias(float bias);
ARC_API void arc_set_lod_force(int level);
ARC_API int arc_mesh_lod_level(arc_mesh base, arc_m4 model);    // the level an instance would get from the current camera
```

Static batches: many draws that never move (a level's walls and floor tiles) handed over once and merged into a few
   big meshes, one per texture and material (and per cell, and per chunk with arc_static_set_chunk), each drawn with one
   call. The draw's tint is baked into the vertex colours; mirrored matrices are fine; materials, textures (also those
   .arcm files bring) and shadow casting come from the source meshes, which must stay alive while the batch is used
   (skinned meshes are refused). Nothing is merged until arc_static_build; build again after adding or clearing.
   arc_static_draw skips chunks outside the view (not while shadows are on: they may cast into view).

```c
typedef struct { uint32_t id; } arc_static;
ARC_API arc_static arc_static_create(void);
ARC_API void arc_static_add(arc_static s, arc_mesh m, arc_m4 model, uint32_t tint);
ARC_API void arc_static_add_cell(arc_static s, arc_mesh m, arc_m4 model, uint32_t tint, int cell);    // -1 = arc_cell_at(its centre)
ARC_API void arc_static_set_chunk(arc_static s, float size);    // also split on an x/z grid this size (finer culling); 0 = none
ARC_API bool arc_static_build(arc_static s);
ARC_API void arc_static_draw(arc_static s);
ARC_API void arc_static_clear(arc_static s);    // forget the items (what was built stays until the next build)
ARC_API void arc_static_destroy(arc_static s);
ARC_API int arc_static_mesh_count(arc_static s);    // merged meshes from the last build
ARC_API int arc_static_item_count(arc_static s);
```
