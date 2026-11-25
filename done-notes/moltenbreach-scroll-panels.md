# Request from MOLTEN BREACH: scrollable arc_ui panels (a height limit, clipped, scrolled by swipe and wheel)

**Why.** On a phone (844 x 390, interface at 1.5x) an arc_ui panel holds about six button rows. MOLTEN BREACH's long
panels (company, campaign orders, intel, keys) are taller than the screen, and arc_ui has no way to limit a panel's
height and scroll what's inside. The game works around it with a pager (`HudPager` in src/hud.c): it emits only
the items that fit from a first index, adds ^ / v buttons and an "a-b of n" label, and turns swipes and the wheel
into whole-item steps. It works, but it moves in whole rows, every list has to be wrapped item by item with
height guesses (`hud_pg_item(p, h)`), and wrapped text rows are estimated. Screenshots: phone views in the repo history
(25 Nov, commit 8f1be05).

**Ask.**

```c
/* A panel no taller than max_h: its content is laid out as usual but drawn clipped to the panel, offset by *scroll
   (pixels, kept in 0..content_h - visible_h). Drag inside the panel (mouse or touch) or the wheel over it scrolls it;
   a drag that moves more than a few pixels does not click the widget it started on. A thin scroll bar shows when the
   content is taller than the panel. max_h <= 0: no limit (same as arc_ui_begin). */
ARC_API void  arc_ui_begin_scroll(const char *title, float x, float y, float w, float max_h, float *scroll);
ARC_API float arc_ui_content_height(void);   /* after arc_ui_end: the last panel's full content height */
```

- `arc_ui_end` closes it as now. Widgets scrolled out of view take no clicks (and don't show in `frame.ui`, or are
  flagged `clipped`).
- Keyboard / gamepad focus moving to a hidden widget scrolls it into view.
- Must not change: existing panels (no max_h) look and behave exactly as before; `ui_fit` counts a scroll panel as
  fitting when its visible box fits.

**Game side (ready).** Each pager-wrapped panel becomes `arc_ui_begin_scroll(..., h, &scroll)` with the `PROW` /
`PLAB` guards removed; the help page (hand-drawn) already scrolls with `arc_push_offset2d` clipping. The pager stays
for lists of hand-drawn cards (market, contracts, parts) unless ARC wants to offer a scroll area for 2D too.

## Acceptance

```arc-check
game samples/demo/demo.c
args --size 844x390
do set scroll_panel_test 1; step 2; frame
expect frame.ui_panels[0].rect[3] <= 200
expect frame.ui[*].label contains "Row 1"
do mouse 100 150; press MOUSE_LEFT; step 1; mouse 100 60; step 1; release MOUSE_LEFT; step 2; frame
expect frame.ui[*].label contains "Row 12"
```

(The demo opens a 200-pixel-high scroll panel holding 30 buttons "Row 1".."Row 30"; a 90-pixel upward drag scrolls
later rows into view without clicking "Row 3".)

## Done in ARC 0.10.17

`arc_ui_begin_scroll(title, x, y, w, max_h, &scroll)` and `arc_ui_content_height()` as asked, with these details:

- `max_h` is in UI units, the same as `w` (multiplied by the UI scale and DPI), so a panel keeps its proportions when
  the interface is at 1.5x. On your phone at 1.5x, `max_h = 200` gives a 300-pixel panel; pass `max_h = h / 1.5f`
  (or `h / arc_ui_scale()`) if you work out the height in pixels. `*scroll` is in pixels, as asked.
- A drag past 6 pixels up or down scrolls the panel and presses nothing. A drag that goes sideways first is left
  alone, so sliders inside a scroll panel still work. The wheel scrolls 40 pixels a step (scaled).
- Hidden widgets take no clicks or touches and are left out of `ui` / `frame.ui`. A widget partly in view is listed
  with only its visible part and `"clipped": true`, so `ui_click` lands on the part you can see.
- Keyboard and gamepad focus moving to a hidden widget scrolls it into view on the next tick.
- `ui_fit` checks the visible box only. Panels without `max_h` draw exactly as before; the self-test compares
  their drawing.
- `scroll` may be NULL, and ARC then keeps the position itself. Keeping it in your game state is still better: it
  survives `save_state`, and you can reset it when the panel opens.
- New agent command: `wheel N` (+ = up). `ARC_HAS_UI_SCROLL` is 1017.
- Not done: a scroll area for hand-drawn 2D (the market and contract cards). `arc_push_offset2d` clipping plus your
  pager still covers those. Ask again if you want ARC to own the drag there too.

The acceptance block passes as written. The demo panel is 300 x 200 pixels with the UI at half size, so that a
90-pixel drag brings Row 12 into view. `tests/scroll_panel_check.md` adds checks for a plain click after scrolling,
the wheel and its end stop, the wheel away from the panel, keyboard focus scrolling into view, and `ui_fit`.
