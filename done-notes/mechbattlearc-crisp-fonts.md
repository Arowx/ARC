# Request from MechBattleArc: text that stays sharp at any size (SDF fonts or on-demand sizes)

**Why.** Players said the text looked "murky and blurred". Cause: a bitmap font atlas drawn at a size it wasn't baked
for, and at fractional pixel positions. The game's fix was to bake 89 fonts (`assets/fonts/text_11..64`,
`head_14..80`), load the nearest size lazily, and snap every draw to whole pixels. That works, but costs download size
and code in every game, and a UI scale between baked sizes still picks the nearest.

**Ask.** Either of:
1. **Signed-distance-field fonts**: `font_bake.py --sdf` writes an `.arcf` with a distance atlas (one bake, e.g. 48 px
   with 6 px spread); `arc_draw_text` at any size renders it sharp (smoothstep over about 1 screen pixel, so small
   sizes don't go soft or bold). Optional `outline` and `shadow` from the same field.
2. **On-demand sizes**: `arc_font_load_ttf(path)` keeps the TTF and rasterises glyphs at the exact pixel size asked
   for (cached per size, built on first use), so the game asks for `text` at 17.3 px and gets crisp 17 px glyphs.
Either way: text placed on whole pixels by default (`arc_text_snap`, default on), the same metrics on all
renderers, and existing bitmap `.arcf` files unchanged.

**Game side (ready / to do).** `src/hud.c` already asks for fonts by exact pixel size (`pick(kind, px)` with
`font_try`); it would load one font per face instead of 89 and drop the baked sizes from `assets/fonts/`.

## Acceptance

Described in words: a line of text drawn at 11, 14, 17.5, 23 and 64 px matches a reference rasterised at that exact
size within 1 level in the stem widths (post test); no blur halo wider than 1 pixel at 11 px; the web build's font
data for two faces under 1 MB.

---

## Done in ARC 0.10.1 (16 Nov 2026)

Option 1, signed-distance-field fonts (feature macro `ARC_HAS_SDF_FONTS`).

**Baking.**
- `python3 <ARC>/tools/font_bake.py Font.ttf assets/fonts --sizes 48 --sdf --name text` writes
  `text_sdf48.arcf`. Use `--spread N` (default 6) to set how far the field reaches.
- One file per face replaces the 89 sizes. DejaVu Sans at 48 px is 150 KB, so two faces are about 300 KB, well
  under 1 MB.

**Drawing.**
- `arc_font_draw_ex(f, x, y, px / 48.0f, 0, rgb, text)` at any size. In `hud.c`, `pick(kind, px)` becomes one
  font per kind and a scale.
- `arc_font_width` and `arc_font_draw_wrapped` scale the same way. `arc_font_is_sdf(f)` tells you which kind a
  font is.
- The edge is smoothed over one screen pixel, the same on the software renderer, GL and D3D11, so small sizes don't
  go soft or bold.
- `ARC_TEXT_OUTLINE` / `ARC_TEXT_SHADOW` work as with bitmap fonts.
- `arc_ui_set_font(sdf, px / 48.0f)` works for the UI.
- 3D billboard text with an SDF font keeps the field's soft edge, so use a bitmap font there.

**Snapping.** Lines of baked-font text now start on whole pixels by default (`arc_text_snap(false)` turns it off).
It changed none of ARC's existing test pictures. Old bitmap `.arcf` files load and draw as before.

**Acceptance** (ARC's post test, all three renderers): "II" drawn from one 48 px field at 11, 14, 17.5, 23 and
64 px, compared with bitmaps baked at those sizes (17 and 18 for 17.5).

| Size | Distance field stem (px) | Bitmap stem (px) |
|---|---|---|
| 11 | 0.95 | 1.87 |
| 14 | 1.24 | 1.18 |
| 17.5 | 1.55 | 1.46 |
| 23 | 1.97 | 1.95 |
| 64 | 5.65 | 5.42 |

At most one partly covered pixel per stem edge. At 11 px the bitmap is the blurrier of the two: 4 partly covered
pixels against 0.
