# Request (test fixture): the landscape card in a distance-field UI font (0.10.6)

**Why.** MechBattleArc's request: with `arc_ui_set_font(sdf48, px / 48)` the card of `arc_ui_require_landscape` drew
its text many times too big ("RN SIDEWAYS" across a 720 px wide window). The card now sizes its text from the UI's
font and scale and wraps it to the window.

**Ask.** Nothing; `tests/review_game.c --sdf-ui` shows the card in portrait.

## Acceptance

```arc-check
game tests/review_game.c
args --sdf-ui --size 720x1280
do step 2
expect frame.texts[text=TURN SIDEWAYS].box exists
expect ui[label=TURN SIDEWAYS].w < 600
expect count(ui[*]) >= 2
expect max(ui[*].w) < 640
ui_fit 720x1280 360x640 1080x1920 480x854
do step 1
```
Described in words: the card's lines are narrower than the window at every portrait size (`ui_fit` finds none outside),
and the body wraps onto more than one line.
