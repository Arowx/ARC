# Request from MechBattleArc: auto-framed cameras and labelled contact sheets for art review

**Why.** Every art review in MOLTEN BREACH (frames, styles, 21 livery patterns, faction line-ups, colour options) meant
writing a debug view into the game (`mechview`, `paintview`, `patview F ROW`), hand-tuning the camera so 4, 9 or 11
mechs fill the shot at that window shape (several tries each), then cropping and labelling the screenshot in Python
with PIL. The same is true of the import previews in other games.

**Ask.**
- `arc_camera arc_camera_fit(arc_camera base, arc_v3 box_min, arc_v3 box_max, float aspect, float margin)`: keeps the
  view direction and fov of `base`, moves the eye so the box fills the view with `margin` (share of the frame) on the
  tightest side. Works for perspective and orthographic.
- An agent command `sheet FILE.png [cols] [cell WxH]` drawing every mesh the game registered with
  `arc_sheet_add(mesh, model, style, label)` this frame (or from a JSON list for `tools/arc_view`): one cell each,
  each auto-framed with `arc_camera_fit`, the label under it, same light for all, transparent or flat background.
- `shot FILE.png crop` trims to non-background pixels plus a margin.

**Game side (ready / to do).** The review views already exist in `src/main.c`; they would register their mechs with
`arc_sheet_add(...)` and drop their hand-set cameras and the PIL scripts.

## Acceptance

Described in words: `arc_camera_fit` on a 1x2x1 box at aspects 0.5, 1, 2.35 leaves the box's projected bounds within
the margin on the tight side and centred; a sheet of 3 primitives with labels renders 3 cells, each object on screen
and its label below it (`frame.texts`), identical on all renderers.

```arc-check
exe build/mechbattlearc_headless
args --no-session
do hud off; paintview 1; step 30
sheet review_sheet.png 4
expect frame.texts[*].text contains "Halcyon"
```

---

## Done in ARC 0.10.1 (16 Nov 2026)

Feature macro `ARC_HAS_SHEETS`.

**`arc_camera_fit(base, box_min, box_max, aspect, margin)`.**
- It keeps `base`'s view direction, up and field of view (or orthographic) and moves the eye (orthographic: sizes
  the view) so the box fills the frame.
- It is centred, with `margin` (a share of the frame) on each side of the tighter axis.

**Sheets.**
- In draw, call `arc_sheet_add(mesh, model, &style, "Halcyon")` for each mech. It records only while the command
  collects, so it costs nothing otherwise. Posed meshes are copied with their pose.
- Then `sheet FILE.png [COLS] [cell WxH] [bg RRGGBB | transparent] [margin M]`:
  - one cell per mesh, framed by `arc_camera_fit` from the front (a little above and to the right), the same light
    for all, and the label underneath;
  - defaults: 256x256 cells (at most the window's size), a dark flat background, columns to make it roughly square;
  - `transparent` writes real alpha, worked out from black and white backgrounds;
  - it prints `ARC sheet {"cells": [{"label", "x", "y", "w", "h", "on_screen", "label_at"}]}` and
    `ARC ok sheet FILE N cells WxH`.
- `shot FILE.png crop [MARGIN]` trims a screenshot to what differs from its corner colour (margin default 8 px).

**Acceptance.**
- ARC's self-test: `arc_camera_fit` on a 1 x 2 x 1 box at aspects 0.5, 1 and 2.35, perspective and orthographic.
  The projected box touches the margin on the tight side (within 0.002) and is centred on both axes, and the view
  direction and field of view are kept.
- `tests/review_check.md`: a sheet of a cube, a sphere and a cylinder with labels gives 3 cells, each object on
  screen and its label below it.

Your check (`paintview 1; step 30; sheet review_sheet.png 4; expect frame.texts[*].text contains "Halcyon"`) needs
`paintview` to call `arc_sheet_add`. The `frame` after `sheet` describes the game's own frame, so the "Halcyon" text
must be drawn by paintview, as it is now.

**Not done:** sheets from a JSON list in `tools/arc_view`. `arc_view` can call `arc_sheet_add` itself; tell me if you
want that.
