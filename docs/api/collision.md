# Collision: ray tests, a static collision world, a character controller

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Collision: ray tests, a static collision world, a character controller** · [quick reference](../QUICK.md) · [full API](../API.md)


Rays: o + d * t for t in [0, max_t] (d need not be unit length; t is in units of d). Hits report the nearest
   point; the normal faces the ray. Pass NULL for h to just test. All of this is plain deterministic maths:
   use it in tick().

```c
typedef struct { bool hit; float t; arc_v3 point, normal; int tri; uint32_t user; } arc_hit;
ARC_API bool arc_ray_sphere(arc_v3 o, arc_v3 d, arc_v3 center, float radius, float max_t, arc_hit *h);
ARC_API bool arc_ray_box(arc_v3 o, arc_v3 d, arc_v3 lo, arc_v3 hi, float max_t, arc_hit *h);
ARC_API bool arc_ray_triangle(arc_v3 o, arc_v3 d, arc_v3 a, arc_v3 b, arc_v3 c, float max_t, arc_hit *h);
ARC_API bool arc_ray_mesh(arc_mesh m, arc_m4 model, arc_v3 o, arc_v3 d, float max_t, arc_hit *h);    // any mesh, every triangle (picking)
```

A collision world: static level geometry (meshes, boxes, triangles) in a bounding volume tree, built when first
   queried after a change. `user` is your own tag, returned in hits (a surface type, an object id).

```c
typedef struct { uint32_t id; } arc_cworld;
ARC_API arc_cworld arc_cworld_create(void);
ARC_API void arc_cworld_destroy(arc_cworld w);
ARC_API void arc_cworld_clear(arc_cworld w);
ARC_API void arc_cworld_add_mesh(arc_cworld w, arc_mesh m, arc_m4 model, uint32_t user);
ARC_API void arc_cworld_add_box(arc_cworld w, arc_v3 lo, arc_v3 hi, uint32_t user);
ARC_API void arc_cworld_add_triangles(arc_cworld w, const arc_v3 *v, int count, uint32_t user);    // count = vertices
ARC_API int arc_cworld_triangles(arc_cworld w);
ARC_API bool arc_cworld_raycast(arc_cworld w, arc_v3 o, arc_v3 d, float max_t, arc_hit *h);
ARC_API bool arc_cworld_overlap_sphere(arc_cworld w, arc_v3 center, float radius);
ARC_API int arc_cworld_push_sphere(arc_cworld w, arc_v3 *center, float radius);    // moves the sphere out; returns triangles touched
```

Capsule character: set velocity (x/z from input; y to jump) and call arc_character_move once per tick.
   It falls with gravity, slides along walls, stands on floors up to max_slope without sliding, climbs steps
   up to step_height and sticks to the ground going down slopes. Zero fields take the defaults shown.

```c
typedef struct { arc_v3 pos; arc_v3 velocity; float radius; float height; float step_height; float max_slope; float gravity; float snap_distance; bool on_ground, hit_wall, hit_ceiling; arc_v3 ground_normal; } arc_character;
ARC_API void arc_character_move(arc_cworld w, arc_character *c, float dt);
```
