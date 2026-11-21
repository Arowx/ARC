# Request from Vector Trader: a 2D offset, for drawing a layout inside the safe area

**Now.** `arc_safe_area()` says where the notch and the browser bars are, but 2D drawing is always in window pixels
from the top-left. A game whose HUD and screens are laid out from (0, 0) has to add the insets to every coordinate
itself; ARC's own UI kit handles this, but hand-drawn HUDs don't use it.

**Workaround in the game.** A `vt_safe()` helper returns the insets, and the HUD, the touch buttons, the docked tabs,
the screens and the title each add them by hand (and the tap tests subtract them). Easy to miss one; the chart and the
settings panel still assume the whole window.

## Ask

```c
/* 2D draws after this are moved by (x, y) pixels, and clipped to (x, y, w, h) if w, h > 0; pushes nest (up to 8).
   arc_mouse() is unchanged (games subtract the offset for hit tests), or arc_mouse_local() returns it offset too. */
ARC_API void arc_push_offset2d(float x, float y, float w, float h);
ARC_API void arc_pop_offset2d(void);
```

With it a game does `arc_rectf sa = arc_safe_area(); arc_push_offset2d(sa.x, sa.y, sa.w, sa.h);`, draws its layout
for a window of sa.w x sa.h, and pops.

## Acceptance

```arc-check
game samples/demo/demo.c
args --safe-area 40,0,40,20
do set offset2d_test 1; step 2; frame
expect frame.rects[0].box[0] >= 40
expect frame.texts[0].box[0] >= 40
```

(The demo draws a rect and a label at (0, 0) inside `arc_push_offset2d(safe area)`; both land at x = 40.)

## Done in ARC 0.10.13

- `arc_push_offset2d(x, y, w, h)` / `arc_pop_offset2d()` / `arc_offset2d()`. Everything 2D after a push (quads,
  sprites, text, lines, `arc_draw_lines2d`) is moved by (x, y). With w and h above 0 it is also clipped to that area,
  inside any clip already set. Pushes nest up to 8 deep and add up; the offset is reset each frame, and an `ARC hint`
  says when a push was left open.
- Safe-area HUD: `arc_rectf sa = arc_safe_area(); arc_push_offset2d(sa.x, sa.y, sa.w, sa.h);`, draw the layout for a
  `sa.w` x `sa.h` screen from (0, 0), then `arc_pop_offset2d()`.
- `frame` boxes report where things land after the offset. `tests/offset2d_check.md` (demo var `offset2d_test`,
  `--safe-area 40,0,40,20`) checks the panel and the "SAFE" label start at x 40 and 48. It passes and runs in
  `build test`.
- `#if ARC_HAS_OFFSET2D`.
