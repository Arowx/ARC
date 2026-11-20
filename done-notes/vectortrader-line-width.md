# Request from Vector Trader: thick, glowing 3D and 2D lines

Vector Trader draws everything as vector lines, in the style of 1980s vector monitors: ships are their edges, the
HUD is lines and text. On ARC it will draw thousands of `arc_draw_line` segments a frame.

**Now.** `arc_draw_line` and `arc_draw_line2d` are one pixel wide. On a phone (a 1080p or 1440p screen held at
arm's length, often with device-pixel scaling) one-pixel lines are hard to see, and they don't take part in bloom, so
the glow of the original (a soft halo round each line) isn't possible without drawing every edge as a mesh.

## Ask

1. **Width.** A line width in pixels for the lines drawn after it, anti-aliased, with round or square caps:

```c
ARC_API void arc_set_line_width(float pixels);        /* 1 = today's lines; applies to 3D and 2D lines */
```

   On high-DPI screens the width could follow the UI scale (`arc_ui_scale()`), or the game passes it.

2. **Glow.** Lines that bloom: an intensity multiplier so a line's colour can go above 1 and pass the post bloom
   threshold, like emissive meshes:

```c
ARC_API void arc_set_line_glow(float intensity);      /* 1 = today's look; 2..4 blooms with arc_post.bloom */
```

   Or one call with both: `arc_draw_line_ex(a, b, rgb, width, glow)`.

3. **Many lines.** A batched form for a whole ship's edges (one model matrix):

```c
ARC_API void arc_draw_lines(const arc_v3 *pts, int count, arc_mat4 model, uint32_t rgb);  /* pairs of points */
```

   with the per-frame line limit raised (or settable), since a busy fight is around 6,000 segments.

`ARC_HAS_LINE_WIDTH` (and `ARC_HAS_LINE_GLOW` if it comes separately).

## Acceptance

```arc-check
game samples/demo/demo.c
args --size 640x360
do hud off; set line_test 1; step 2; shot thick.png
expect frame.lines.width_max >= 3
expect frame.dropped == 0
```

(The sample would need a `line_test` variable drawing a few thick, glowing lines; a pixel check that the line is at
least 3 pixels wide in `thick.png` would do as well.)

## Done in ARC 0.10.11

All three asks are in, with the names you suggested.

- **`arc_set_line_width(pixels)`:** 3D lines after it are that many pixels wide on the screen (or render target) at
  any distance. They have a soft one-pixel edge, and `arc_set_line_cap(ARC_LINE_CAP_SQUARE | _ROUND | _BUTT)` sets the
  ends (square by default in 3D).
  - It is set until changed (not reset each frame), so a vector game can set it once in `init`.
  - Width 1 is today's line exactly.
  - Thick lines are depth tested but write no depth, so crossing edges never cut each other.
  - The width doesn't follow the UI scale by itself: pass `arc_ui_scale()` times your base width if you want that.
- **`arc_set_line_glow(intensity)`:** multiplies the line colour (thin lines too). Above 1 it passes `arc_post`'s
  bloom threshold, like emissive meshes. Without bloom, a strong glow saturates towards white, which reads as a hot
  core.
- **`arc_draw_lines(pts, count, model, rgb)`:** point pairs, each moved by `model`: a ship's edges in one call. The
  limit is about 9,000 thick segments a frame (5 MB, allocated at the first thick line), and thin lines still allow
  131,072. A 6,000-segment fight fits.
- **`arc_draw_line2d`** 1.5 pixels and wider now has soft edges too (butt ends unless you call `arc_set_line_cap`), so
  the HUD matches. 2D isn't bloomed: it's drawn after post.
- **Checks:** `frame.lines` has `count`, `thick`, `width_max`, `glow_max` and `triangles`, and `frame.dropped` is
  there too. Your check passes on the demo, which has the `line_test` tunable (a glowing wireframe ship, a thick line,
  a soft 2D line): ARC's `tests/line_check.md`. `tests/line_width_test.py` measures the pixels: a 4-pixel line is 4
  pixels tall where it crosses the middle, with a saturated glowing core and soft edges.
- **Renderers:**
  - Software and OpenGL give the same picture. Their only differences are the ones that were already there in thin
    one-pixel lines.
  - Direct3D 11 builds, but couldn't be run in ARC's cloud workspace this time. Allan's `build.bat post-d3d` and a
    look at the game on Windows are its first run.
- Macros: `ARC_HAS_LINE_WIDTH`, `ARC_HAS_LINE_GLOW` (1011).
