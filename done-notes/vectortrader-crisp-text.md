# Request from Vector Trader: crisp built-in font at any size

**Now.** The built-in 8x8 font drawn at a fractional scale (or at a half-pixel position) drops whole rows or columns
of pixels: on a phone at 844x390, "DOCK" read "DUCK" and "MISSILE 2" read "NISSILE 2". The scales came from
`arc_text_fit_scale(label, max_w, max_scale)`, which returns fractions like 0.88, and from centring text
(`ARC_TEXT_CENTER | ARC_TEXT_MIDDLE`), which lands on half pixels.

**Workaround in the game.** Button labels are floored to whole scales and drawn at whole-pixel positions computed
by hand; one label was shortened ("MISSILE") so it fits at scale 1.

## Ask

1. The built-in font always drawn at a whole-pixel position (snap x, y after alignment), and at a scale snapped to a
   whole number when it is 1 or more (or to 0.5 steps), unless a flag asks for exact scaling.
2. `arc_text_fit_scale` returns a whole-number scale (when it is at least 1) for the built-in font, so "the largest
   size that fits" never picks a blurry one. A new name is fine if the old behaviour must stay.

## Acceptance

```arc-check
game samples/demo/demo.c
args --size 844x390
do set text_test 1; step 2; frame
expect frame.texts[text=DOCK].scale == 1
expect frame.texts[text=DOCK].box[0] == 400
```

(The demo centres "DOCK" in a 63-pixel-wide button at x = 384.5 with `arc_text_fit_scale`; the label should come out
at scale 1 on a whole pixel. A pixel check of the 'O' in a shot would do as well.)

## Done in ARC 0.10.12

- **Whole pixels:** the built-in font is now always drawn on whole pixels, after alignment, so centring
  (`ARC_TEXT_CENTER | ARC_TEXT_MIDDLE`) can't land it on a half pixel.
- **Whole sizes when fitting:** `arc_text_fit_scale` returns a whole size when the fit is 1 or more (the largest that
  fits). Below 1 it is still the exact fraction: the 8x8 font can't keep every row there, and ARC already hints about
  that. For small text, bake a font (`tools/font_bake.py`).
- **Other sizes of 1 and more are kept as given.** Nearest sampling hits every row at any size from 1 up, so nothing
  drops. Rounding them by default broke layouts that were sized with fractional scales: ARC's own UI kit text
  overflowed its buttons in the test pictures. To round every size from 1 up as well, call
  `arc_text_snap_builtin(ARC_TEXT_SNAP_SIZES)`; `arc_text_width` / `_height` then agree. `ARC_TEXT_SNAP_OFF` is the old
  behaviour, and `ARC_TEXT_EXACT` skips snapping for one `arc_draw_text_ex`.
- `frame.texts[*].scale` is reported for the built-in font.
- **Your check passes on the demo** (`text_test`, a 63-pixel button at x = 384.5 at 844x390): "DOCK" at scale 1,
  `box[0] == 400`. ARC's copy is `tests/text_check.md`.
- Your workaround (flooring scales, hand-placed positions) can go. "MISSILE 2" fits once there's room for scale 1.
- Macro `ARC_HAS_TEXT_SNAP` (1012).
