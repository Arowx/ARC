# Terrain (0.9.6): height grids, splatted ground, scattered props

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Terrain (0.9.6): height grids, splatted ground, scattered props** · [quick reference](../QUICK.md) · [full API](../API.md)


A terrain is a grid of heights drawn as chunks of triangles (lower detail further away, skirts hiding the seams),
   textured by up to 4 ground layers blended by a splat map (paint it, or fill it from height and slope rules).
   Heights, normals and ray hits follow the triangles as drawn. Scatter places copies of a mesh (grass, rocks, trees)
   over it by layer, height and slope, the same every run (a seed), and draws those near the camera.
     arc_texture lay[4] = { grass, dirt, rock, snow };   // same-size textures (loaded or made)
     G.ter = arc_terrain_create(&(arc_terrain_desc){ .heights = h, .samples_x = 257, .samples_z = 257,
                                .size_x = 200, .size_z = 200, .origin = { -100, 0, -100 },
                                .layers = { lay[0], lay[1], lay[2], lay[3] }, .layer_count = 4, .layer_scale = { 0.25f, 0.3f, 0.2f, 0.2f } });
     arc_terrain_splat_rules(G.ter, rules, 3);                    // rock on slopes, snow up high ...
     G.grass = arc_scatter_create(G.ter, &(arc_scatter_desc){ .mesh = tuft, .density = 2, .layer = 0, .seed = 1 });
     // draw():  arc_terrain_draw(G.ter, ARC_WHITE); arc_scatter_draw(G.grass);
   For physics and navmeshes hand over its triangles (arc_terrain_triangles). All handles are freed with _destroy.

```c
typedef struct { uint32_t id; } arc_terrain;
typedef struct { const float *heights; int samples_x, samples_z; float size_x, size_z; arc_v3 origin; int chunk; int splat_res; arc_texture layers[4]; int layer_count; float layer_scale[4]; float splat_sharpness; const arc_material *material; float lod_distance; } arc_terrain_desc;
ARC_API arc_terrain arc_terrain_create(const arc_terrain_desc *d);    // {0} on bad input (logged)
ARC_API void arc_terrain_destroy(arc_terrain t);
ARC_API void arc_terrain_draw(arc_terrain t, uint32_t tint);    // from draw(); chunks cast and take shadows
```

0.9.7: chunks out of view are skipped. With sun shadows on, chunks whose shadow can reach the view still draw; with a
   reflection plane on, all do (the mirror sees elsewhere). Chunks out of view cast no point/spot light shadows.

```c
ARC_API int arc_terrain_drawn(arc_terrain t);    // chunks the last arc_terrain_draw drew
ARC_API int arc_terrain_chunk_count(arc_terrain t);
ARC_API void arc_terrain_set_culling(arc_terrain t, bool on);    // on by default; off draws every chunk
```

Ground markers (0.9.9): tile highlights, rings under units, drop zones, intent arrows, in one call each. They lie on
   the ground (a flat y, an arc_terrain, or your height function; big shapes are split so they follow bumps), unlit and
   see-through, pulled a hair toward the camera so they never z-fight it, and batched (consecutive markers in a pass
   are one draw). edge_width is in screen pixels, so outlines stay crisp at any zoom; pulse breathes the fill; dash
   breaks the edge; on_top also shows a faint copy where units or walls cover the marker; label is text centred above
   (2D, so frame.texts has it). Every marker is listed in the frame command's frame.markers (shape as a name such as
   "ring", pos, size, rgb, label; target when drawn into a render target).
   Draw them in draw() (or tick() in immediate mode), like meshes. Zero is the default for every field:
     arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = u.x, .z = u.z, .size = 1.1f, .inner = 0.82f,
                                     .fill_rgb = 0x38D8F0, .fill_alpha = 0.7f, .edge_rgb = 0xFFFFFF, .edge_width = 1.5f,
                                     .terrain = G.ground, .on_top = true });

```c
enum { ARC_MARK_TILE = 0, ARC_MARK_RING = 1, ARC_MARK_DISC = 2, ARC_MARK_ARROW = 3, ARC_MARK_CHEVRON = 4, ARC_MARK_DIAMOND = 5 };
typedef struct { int shape; float x, z; float size; float yaw; float x2, z2; float inner; float head; uint32_t fill_rgb; float fill_alpha; uint32_t edge_rgb; float edge_width; float edge_alpha; float pulse, pulse_amount; float dash; bool on_top; float y; arc_terrain terrain; float (*height)(void *ud, float x, float z); void *height_ud; float lift; const char *label; uint32_t label_rgb; float label_height, label_scale; } arc_marker;
ARC_API void arc_draw_marker(const arc_marker *m);
ARC_API float arc_terrain_height(arc_terrain t, float x, float z);    // on the drawn triangles; outside, the nearest edge
ARC_API arc_v3 arc_terrain_normal(arc_terrain t, float x, float z);    // smooth (as lit), unit length
ARC_API bool arc_terrain_inside(arc_terrain t, float x, float z);
```

the first hit along dir (any length) within max_dist: *dist and *point set (either may be NULL)

```c
ARC_API bool arc_terrain_raycast(arc_terrain t, arc_v3 origin, arc_v3 dir, float max_dist, float *dist, arc_v3 *point);
```

editing: raise (amount < 0: lower) with a smooth falloff to `radius`; flatten toward a height by strength 0..1;
   replace them all. The chunks follow at once; scatter keeps its places until arc_scatter_rebuild.

```c
ARC_API void arc_terrain_sculpt(arc_terrain t, float x, float z, float radius, float amount);
ARC_API void arc_terrain_flatten(arc_terrain t, float x, float z, float radius, float height, float strength);
ARC_API void arc_terrain_set_heights(arc_terrain t, const float *heights);    // samples_x * samples_z
```

the splat map: paint a layer in (strength 0..1, smooth falloff; the other layers make room), fill from rules, or
   ask a layer's weight 0..1 at a point (footstep sounds, what grows where)

```c
ARC_API void arc_terrain_paint(arc_terrain t, float x, float z, float radius, int layer, float strength);
typedef struct { int layer; float height_min, height_max; float slope_min, slope_max; float blend; float amount; } arc_splat_rule;
ARC_API void arc_terrain_splat_rules(arc_terrain t, const arc_splat_rule *rules, int n);    // whole map from layer 0; later rules over earlier
ARC_API float arc_terrain_layer_weight(arc_terrain t, float x, float z, int layer);
```

full-detail triangles in world space, 3 points each: fills up to `max` points and returns how many there are (out
   NULL: just the count). For arc_physics_add_triangles and arc_navmesh_add_triangles.

```c
ARC_API int arc_terrain_triangles(arc_terrain t, arc_v3 *out, int max);
ARC_API arc_texture arc_terrain_splat_map(arc_terrain t);    // RGBA = layer weights (for a minimap)
typedef struct { uint32_t id; } arc_scatter;
typedef struct { arc_mesh mesh; float density; int layer; float min_weight; float height_min, height_max; float slope_max; float scale_min, scale_max; float align; float sink; bool fixed_yaw; uint32_t tint, tint2; float draw_distance; uint32_t seed; } arc_scatter_desc;
ARC_API arc_scatter arc_scatter_create(arc_terrain t, const arc_scatter_desc *d);
ARC_API void arc_scatter_destroy(arc_scatter s);
ARC_API void arc_scatter_rebuild(arc_scatter s);    // after sculpting or painting: places from the same seed
ARC_API void arc_scatter_draw(arc_scatter s);    // from draw(): copies in view within draw_distance
ARC_API int arc_scatter_count(arc_scatter s);    // copies placed (drawn or not)
ARC_API arc_m4 arc_scatter_matrix(arc_scatter s, int i);    // copy i's model matrix (0 .. count - 1): obstacles, picking
ARC_API int arc_scatter_drawn(arc_scatter s);    // copies the last arc_scatter_draw drew
```
