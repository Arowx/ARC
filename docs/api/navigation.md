# Navigation (0.9.2): navmeshes, paths, agents that follow them

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Navigation (0.9.2): navmeshes, paths, agents that follow them** · [quick reference](../QUICK.md) · [full API](../API.md)


A navmesh is the walkable surface as triangles. Build it once (in init, or when the level loads) from any mix of
   triangles, loaded meshes and tile grids; then ask it for paths, the nearest walkable point, whether a straight walk
   is clear, and random walkable points. Paths go around walls and gaps and keep `radius` from corners.
     G.nav = arc_navmesh_create(NULL);
     arc_navmesh_add_mesh(G.nav, level_floor, arc_m4_identity());   // or arc_navmesh_add_grid(G.nav, &grid, ...)
     arc_navmesh_build(G.nav);
     arc_nav_agent_init(&G.guard, start, 3.5f, 0.4f);              // speed (units/s), radius
     arc_nav_agent_goto(&G.guard, G.nav, target);                  // in tick, when the target changes
     arc_nav_agent_update(&G.guard, G.nav, arc_dt());              // every tick
   Triangles join where they share an edge (vertices closer than `weld` count as one). An edge that meets the middle
   of a longer edge does not join (a "T-junction"): split the long edge, or use add_grid, which never makes them.
   Everything is deterministic (same inputs, same paths and moves on every machine), and queries never allocate.
   Agents are plain structs with no pointers: keep them in your game state and save_state/load_state copy them.

```c
typedef struct { uint32_t id; } arc_navmesh;
typedef struct { float max_slope; float weld; } arc_navmesh_desc;
ARC_API arc_navmesh arc_navmesh_create(const arc_navmesh_desc *desc);    // NULL = defaults
ARC_API void arc_navmesh_destroy(arc_navmesh nm);
```

add walkable geometry (before arc_navmesh_build; adding after a build starts a new one at the next build).
   Triangles wind counter-clockwise seen from above (like ARC's meshes); those facing down or too steep are skipped.

```c
ARC_API void arc_navmesh_add_triangles(arc_navmesh nm, const arc_v3 *v, int count);    // count = vertices, 3 a triangle
ARC_API void arc_navmesh_add_mesh(arc_navmesh nm, arc_mesh m, arc_m4 model);    // the mesh's upward triangles
```

a tile grid: every cell where walkable(id) becomes a square cell x cell at height origin.y, cell (x, y) at
   origin + (x * cell, 0, y * cell) .. + cell (so grid y runs along +Z). walkable NULL = id 0 is walkable.

```c
ARC_API void arc_navmesh_add_grid(arc_navmesh nm, const arc_grid *g, arc_v3 origin, float cell, bool (*walkable)(uint8_t));
```

take out the walkable area inside a box (obstacles placed on a floor mesh): triangles whose centre is inside go

```c
ARC_API void arc_navmesh_remove_box(arc_navmesh nm, arc_v3 lo, arc_v3 hi);
ARC_API bool arc_navmesh_build(arc_navmesh nm);    // false if nothing walkable
ARC_API int arc_navmesh_triangles(arc_navmesh nm);    // walkable triangles after the build
ARC_API int arc_navmesh_islands(arc_navmesh nm);    // separate walkable areas (not joined to each other)
```

Path from `from` to `to` for an agent of `radius`: writes up to max corner points, the first = from (moved onto the
   mesh), the last = to (or the nearest walkable point to it). Returns the number of points; 0 when there is no path
   (different islands, or every way is narrower than 2 * radius). A longer path is cut at max (its total count is
   still returned, so compare with max). 0.10.19: "narrower" means a gap between two points on the edge of the walkable
   area; small inner triangles (a fine add_grid) no longer block wide agents.

```c
ARC_API int arc_navmesh_path(arc_navmesh nm, arc_v3 from, arc_v3 to, float radius, arc_v3 *out, int max);
ARC_API float arc_navmesh_path_length(const arc_v3 *pts, int n);
```

nearest point on the walkable surface within max_dist (0 = any distance); false if none

```c
ARC_API bool arc_navmesh_nearest(arc_navmesh nm, arc_v3 p, float max_dist, arc_v3 *out);
```

walk a straight line over the surface from `from` toward `to` (in x/z, following its height): true if it reaches
   `to`; false if it meets an edge of the walkable area first, with *hit (may be NULL) where it stopped

```c
ARC_API bool arc_navmesh_walkable_line(arc_navmesh nm, arc_v3 from, arc_v3 to, arc_v3 *hit);
ARC_API bool arc_navmesh_connected(arc_navmesh nm, arc_v3 a, arc_v3 b);    // on the same island
ARC_API float arc_navmesh_height(arc_navmesh nm, arc_v3 p, bool *found);    // the surface's height under/over p
```

a random walkable point, spread evenly by area; rng NULL = arc_rand (seeded by --seed)

```c
ARC_API arc_v3 arc_navmesh_random_point(arc_navmesh nm, arc_rng *rng);
```

debug drawing (from draw): the triangle edges in rgb, the outline of the walkable area brighter

```c
ARC_API void arc_navmesh_draw(arc_navmesh nm, uint32_t rgb);
```

An agent that follows paths: steering, slowing down to arrive, sliding along the edge of the walkable area,
   finding the path again when it runs past the stored corners. Plain data: copy it freely.

```c
#define ARC_NAV_CORNERS 32
typedef struct { arc_v3 pos, vel; float speed; float accel; float radius; float arrive; arc_v3 target; arc_v3 corners[ARC_NAV_CORNERS]; int ncorners, corner; bool moving; bool arrived; bool failed; bool partial; } arc_nav_agent;
ARC_API void arc_nav_agent_init(arc_nav_agent *a, arc_v3 pos, float speed, float radius);
ARC_API bool arc_nav_agent_goto(arc_nav_agent *a, arc_navmesh nm, arc_v3 target);    // false = no path (a->failed)
ARC_API void arc_nav_agent_stop(arc_nav_agent *a);
ARC_API void arc_nav_agent_update(arc_nav_agent *a, arc_navmesh nm, float dt);    // in tick
```

agents keep `radius` apart: pushes overlapping ones away from each other and back onto the surface (in tick,
   after the updates; n agents in one array, O(n^2): fine for a few hundred)

```c
ARC_API void arc_nav_agents_separate(arc_nav_agent *agents, int n, arc_navmesh nm);
ARC_API float arc_nav_agent_remaining(const arc_nav_agent *a);    // distance left along the stored path
ARC_API void arc_nav_agent_draw(const arc_nav_agent *a, uint32_t rgb);    // its path, from draw
```
