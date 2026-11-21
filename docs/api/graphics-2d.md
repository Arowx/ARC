# 2D drawing: rects, sprites, nine-slice, blend, clip

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


2D overlay in screen pixels (origin top-left), drawn after 3D in call order.

```c
ARC_API void arc_draw_rect(float x, float y, float w, float h, uint32_t argb_or_rgb);
ARC_API void arc_draw_rect_lines(float x, float y, float w, float h, uint32_t rgb);
ARC_API void arc_draw_line2d(float x0, float y0, float x1, float y1, float thickness, uint32_t rgb);
```

0.10.13 glowing 2D lines, one quad each: count points taken in pairs; width: the bright core in pixels (soft edges);
   glow: the halo on each side as a multiple of the width (0 = none); halo_alpha: the halo's strength next to the core
   (0..1), fading to nothing at its edge. 6 vertices a segment, against 12 for a halo and a core drawn separately.
   frame.lines.lines2d counts segments. (Core share and halo strength go in 16 steps each.)

```c
ARC_API void arc_draw_lines2d(const arc_v2 *pts, int count, float width, float glow, float halo_alpha, uint32_t rgb);    // 1.5 px and wider: soft edges (0.10.11)
```

Nine-slice: the texture's corners (src_border texels square) drawn dst_border pixels square and unstretched,
   its edges stretched one way, its middle both ways, to fill x, y, w, h. Frames, panels and buttons of any size.
   Corners shrink when the rect is smaller than two borders. (0.6.1)

```c
ARC_API void arc_draw_nine(arc_texture t, float x, float y, float w, float h, float src_border, float dst_border, uint32_t tint);
ARC_API void arc_draw_sprite(arc_texture t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t tint);
```

---- 2D extras: blend modes, clipping, triangles, rotated sprites ----

```c
typedef enum { ARC_BLEND_ALPHA = 0, ARC_BLEND_ADD, ARC_BLEND_MULTIPLY, ARC_BLEND_NONE } arc_blend;
ARC_API void arc_set_blend(arc_blend b);    // applies to following 2D draws; ALPHA by default each frame
ARC_API arc_blend arc_get_blend(void);
```

2D offsets (0.10.13): 2D drawn after arc_push_offset2d(x, y, w, h) is moved by (x, y) pixels and, when w and h are
   above 0, clipped to that area (inside any clip already set); pushes nest up to 8 deep and add up; arc_pop_offset2d
   undoes the last. With the safe area: arc_rectf sa = arc_safe_area(); arc_push_offset2d(sa.x, sa.y, sa.w, sa.h); draw
   a layout for a sa.w x sa.h window from (0, 0); arc_pop_offset2d(). frame boxes report where things land. The mouse
   is not moved: subtract arc_offset2d() (the total in force) for hit tests in draw, or the safe area's x, y in tick.
   Offsets left pushed are dropped at the end of the frame (with a hint).

```c
ARC_API void arc_push_offset2d(float x, float y, float w, float h);
ARC_API void arc_pop_offset2d(void);
ARC_API arc_v2 arc_offset2d(void);
ARC_API void arc_set_clip(float x, float y, float w, float h);    // cut off 2D draws outside this rect (pixels of the current target)
ARC_API void arc_clear_clip(void);
typedef struct { float x, y, u, v; uint32_t color; } arc_vertex2d;    // color 0xAARRGGBB, alpha 0 = opaque (see arc_rgba)
ARC_API void arc_draw_triangles2d(arc_texture t, const arc_vertex2d *v, int count);    // t {0} = untextured
```

Draw part of a texture. src rect in texture pixels (negative sw/sh flip it). The point (ox, oy) of the
   destination rect (size dw x dh) is placed at (dx, dy), and the rect rotates around it by `radians`.

```c
ARC_API void arc_draw_texture_ex(arc_texture t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, float ox, float oy, float radians, uint32_t tint);
ARC_API void arc_texture_set_smooth(arc_texture t, bool smooth);    // bilinear (true) or sharp pixels (false)
ARC_API void arc_texture_destroy(arc_texture t);
```
