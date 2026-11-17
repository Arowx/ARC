# 2D drawing: rects, sprites, nine-slice, blend, clip

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


2D overlay in screen pixels (origin top-left), drawn after 3D in call order.

```c
ARC_API void arc_draw_rect(float x, float y, float w, float h, uint32_t argb_or_rgb);
ARC_API void arc_draw_rect_lines(float x, float y, float w, float h, uint32_t rgb);
ARC_API void arc_draw_line2d(float x0, float y0, float x1, float y1, float thickness, uint32_t rgb);
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
