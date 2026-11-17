# Immediate geometry, quick shapes and 3D lines

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


Immediate geometry: triangles rebuilt every frame (procedural walls, tubes, trails).
   Lit like meshes, drawn double-sided. Use meshes for anything static and repeated.

```c
ARC_API void arc_draw_triangles(const arc_vertex *v, int count);    // count = vertices, multiple of 3
ARC_API void arc_draw_tri(arc_v3 a, arc_v3 b, arc_v3 c, uint32_t rgb);    // flat normal from the winding
ARC_API void arc_draw_quad3(arc_v3 a, arc_v3 b, arc_v3 c, arc_v3 d, arc_v3 normal, uint32_t rgb);    // normal zero = from winding
ARC_API void arc_draw_box(arc_v3 lo, arc_v3 hi, uint32_t rgb);    // solid axis-aligned box (instanced)
```

Quick shapes (use built-in meshes)

```c
ARC_API void arc_draw_cube(arc_v3 center, arc_v3 size, uint32_t rgb);
ARC_API void arc_draw_sphere(arc_v3 center, float radius, uint32_t rgb);
```

3D lines (unlit, drawn after meshes, depth-tested)

```c
ARC_API void arc_draw_line(arc_v3 a, arc_v3 b, uint32_t rgb);
```

Depth offset for lines (0.6.2): lines drawn after this are pushed `fraction` further from the current camera
   along each point's own line of sight (0 = off; 0.3 = 30% further away). Each point lands on the same pixel, so a
   line still looks as if it runs through the models it was placed on, but it sits behind them and never cuts
   across them: guide rings, grids and outlines on busy scenes. Perspective cameras push away from the eye,
   orthographic ones straight back. Set the camera first; it lasts until changed and resets to 0 each frame.

```c
ARC_API void arc_set_line_depth_push(float fraction);
ARC_API void arc_draw_box_lines(arc_v3 lo, arc_v3 hi, uint32_t rgb);
ARC_API void arc_draw_grid(float half_size, float step, uint32_t rgb);
```
