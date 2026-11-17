# Request from MechBattleArc: UI that scales and anchors itself (safe areas, short-side scale, orientation)

**Why.** A player reported the UI cut off on their screen; mobile players see a squashed layout. The game's fix was
hand-written: `hud_s = clamp(min(h/720, w/1280), 0.75, 4)`, every panel position in code relative to screen edges, a
portrait "TURN SIDEWAYS" overlay, and checks at 10 window sizes. Every ARC game with a HUD will need the same.

**Ask.**
- `arc_ui_scale_auto(ref_w, ref_h, min, max)`: sets the UI scale from the window (smaller of w/ref_w, h/ref_h), on
  whole-pixel font sizes when fonts allow; `arc_ui_scale()` reads it.
- Anchored panels: `arc_ui_begin_anchored(name, ARC_ANCHOR_TOP_LEFT | ..., offset_x, offset_y, w, h)` in UI units,
  kept inside the **safe area** (`arc_safe_area()`: the window minus notches / browser chrome on web; the window on
  desktop), shrinking or scrolling when the window is too small instead of drawing off screen.
- `arc_orientation()` (landscape / portrait) and an optional built-in "rotate your device" overlay for landscape-only
  games (`arc_ui_require_landscape(text)`).
- A check command: `ui_fit WxH...` runs one frame at each size and lists widgets outside the window or overlapping.

**Game side (ready / to do).** The HUD in `src/hud.c` / `src/main.c` already works in UI units with `hud_s`; it would
switch its panels to anchors and drop the portrait overlay and the scale maths.

## Acceptance

```arc-check
exe build/mechbattlearc_headless
args --no-session
do newbattle 3; step 5
ui_fit 1280x720 1920x1080 2560x1080 1024x768 800x600 1366x768
expect count(ui[*]) > 10
```
Described in words: `ui_fit` reports no widget outside the window at any of the sizes; an anchored panel at
BOTTOM_RIGHT stays inside the safe area when it is smaller than the window.

---

## Done in ARC 0.10.1 (16 Nov 2026)

All four asks (feature macro `ARC_HAS_UI_LAYOUT`).

- **Scale:** `arc_ui_scale_auto(1280, 720, 0.75f, 4)` sets the UI scale from the window (the smaller of w / 1280
  and h / 720, clamped) and keeps following it every tick. With a bitmap UI font it lands on whole-pixel text sizes.
  `arc_ui_scale()` reads it. `hud_s` becomes this.
- **Anchored panels:** `arc_ui_begin_anchored(name, ARC_ANCHOR_BOTTOM_RIGHT, 10, 10, 220, 0)`.
  - Anchors: `ARC_ANCHOR_TOP_LEFT`, `_TOP`, `_TOP_RIGHT`, `_LEFT`, `_CENTER`, `_RIGHT`, `_BOTTOM_LEFT`, `_BOTTOM`,
    `_BOTTOM_RIGHT` (bits: LEFT 1, RIGHT 2, TOP 4, BOTTOM 8).
  - Offsets and size are in UI units; height 0 means its height last tick.
  - The panel stays inside the safe area and shrinks (down to a quarter) when the window is too small. Anchored
    panels don't drag.
- **Safe area:** `arc_safe_area()` returns an `arc_rectf`.
  - Web: the window minus notches and browser bars, from `env(safe-area-inset-*)` through CSS variables in ARC's
    `web/shell.html` (it now has `viewport-fit=cover`). If MechBattleArc has its own shell, copy the `:root
    { --arc-sl ... }` line.
  - Desktop: the whole window. `--safe-area L,T,R,B` pretends there are insets, for tests.
- **Orientation:** `arc_orientation()` returns `ARC_LANDSCAPE` or `ARC_PORTRAIT`. `arc_ui_require_landscape("TURN
  SIDEWAYS")`, called after the rest of the UI, covers the screen with a turn-sideways card in portrait and returns
  true (pause the game). This replaces your overlay.
- **`ui_fit WxH ...`:** runs one tick at each size and lists widgets outside the window, outside the safe area, or
  overlapping each other, as `ARC error` lines, so arc_check fails on them. It prints
  `ARC ui_fit WxH widgets=N outside=K overlaps=M` per size, then restores the window.

Acceptance (`tests/review_check.md` in `build test`, on a fixture with four anchored panels):
- `args --safe-area 0,40,0,60`, then `ui_fit` at 1280x720 1920x1080 2560x1080 1024x768 800x600 1366x768 720x1280:
  nothing outside the window or safe area, no overlaps, and the bottom-right panel inside the safe area at every
  size.
- The scale is 1 at 1280x720 and follows the window.
- `ui_click` still works on anchored panels.

Your arc-check block runs as written once the HUD uses arc_ui widgets (it counts `ui[*]`). Widgets drawn by hand
with `arc_draw_text` aren't in `ui` or `ui_fit`.
