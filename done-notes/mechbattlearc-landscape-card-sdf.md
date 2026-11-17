# Request from MechBattleArc: arc_ui_require_landscape text is huge with an SDF UI font

**Why.** We tried `arc_ui_require_landscape("TURN SIDEWAYS\nMolten Breach needs a landscape screen.")` at the end of
`draw()` to replace our own overlay. With the UI font set by `arc_ui_set_font(text_sdf48, px / 48)` (about 0.4 at
1280x720 scale 1) the card's text comes out many times too big: at 720x1280 only "RN SIDEWAYS" / "lten Breach nee"
fit across the screen. Our own overlay with the same font draws at the right size. Looks like the card ignores the
UI font's scale (or applies the SDF base size twice).

**Ask.** The card sizes its text from the UI scale and the font's own scale, so it fits the width (wrapping if needed)
with any font kind.

**Game side.** We keep `draw_portrait()` until this is fixed, then switch to the card.

## Acceptance

```arc-check
exe build/mechbattlearc_headless
args --no-session --size 720x1280
do newbattle 3; step 10
expect frame.texts[text=TURN SIDEWAYS].box exists
```
Described in words: the card's text box is narrower than the window at 720x1280 and 360x640.

## Done in ARC 0.10.6

The card now uses the UI's font and scale (`arc_ui_set_font`, `arc_ui_scale_auto`). Before, it worked out a size for
the built-in 8 px font and handed that number to the UI font as its scale, so a 48 px SDF font came out about six times
too big.

- The first line (up to `\n`) is a title at twice the UI text size; the rest is below it at 1.25 times, wrapped to 88 %
  of the window. A title word wider than that shrinks the title. Bitmap fonts keep whole-pixel sizes.
- Your call works as it is: `arc_ui_require_landscape("TURN SIDEWAYS\nMolten Breach needs a landscape screen.")`.
  You can drop `draw_portrait()`.
- The card's lines are UI widgets now, so `ui` lists them and `ui_fit` checks them; the UI the card covers is left out
  of `ui` while it shows.
- Checked by `tests/card_check.md` (ARC's review fixture with a 48 px SDF UI font at about 19 px): at 720x1280 the title
  is 202 px wide and the body 486 px; `ui_fit 720x1280 360x640 1080x1920 480x854` finds nothing outside the window
  (the body wraps onto two lines at 360x640).
- Your acceptance (`frame.texts[text=TURN SIDEWAYS].box exists`) should pass, because the title is its own text.
