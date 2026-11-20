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
```

Thick and glowing lines (0.10.11). arc_set_line_width(px): 3D lines drawn after it are px pixels wide on the screen (or
   render target), whatever their distance, with a soft one-pixel edge; 1 (the default) is the plain one-pixel line.
   arc_set_line_glow(x): their colour is multiplied by x (1 = as given): above 1 they pass the bloom threshold of
   arc_post's bloom and glow, like emissive meshes (1..64; affects thin lines too). arc_set_line_cap: the ends of thick
   lines, 3D and 2D (square, the 3D default: half the width past each end; round; butt: stop at the end, the 2D
   default). All three stay until changed. Thick lines are depth tested but write no depth, so they never hide each
   other; frame.lines reports the frame's counts and widest width. arc_draw_lines: count points taken in pairs (a
   ship's edges), each moved by model. The limit is 131,072 thin lines and about 9,000 thick ones a frame.

```c
typedef enum { ARC_LINE_CAP_SQUARE = 0, ARC_LINE_CAP_ROUND = 1, ARC_LINE_CAP_BUTT = 2 } arc_line_cap;
ARC_API void arc_set_line_width(float pixels);
ARC_API void arc_set_line_glow(float intensity);
ARC_API void arc_set_line_cap(arc_line_cap cap);
ARC_API float arc_line_width(void);
ARC_API void arc_draw_lines(const arc_v3 *pts, int count, arc_m4 model, uint32_t rgb);
ARC_API void arc_draw_box_lines(arc_v3 lo, arc_v3 hi, uint32_t rgb);
ARC_API void arc_draw_grid(float half_size, float step, uint32_t rgb);
```
