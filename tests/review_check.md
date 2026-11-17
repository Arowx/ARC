# Request (test fixture): review sheets and UI layout (0.10.1)

**Why.** This file checks `tests/review_game.c` the way MechBattleArc's review-sheet and UI-layout requests asked:
`sheet` draws one labelled cell per registered mesh, `ui_fit` finds nothing outside the window, the safe area or
overlapping at any size, and the UI scale follows the window. 0.10.6: every sheet cell shows its mesh and label, also
with the HUD hidden (`hud off`, which used to leave the sheet blank).

**Ask.** Nothing; the fixture already does this.

## Acceptance

```arc-check
game tests/review_game.c
args --safe-area 0,40,0,60
do step 3
expect count(ui[*]) > 6
expect game.ui_scale == 1
expect game.safe_y == 40
expect game.safe_bottom == 660
ui_fit 1280x720 1920x1080 2560x1080 1024x768 800x600 1366x768 720x1280
do step 2
expect game.ui_scale == 1
sheet review_sheet.png 3
expect frame.texts[*].text contains "Halcyon"
expect game.sheet_items == 3
expect min(sheet.cells[*].filled) > 0.1
expect min(sheet.cells[*].label_ink) > 0.01
do hud off; step 1
sheet review_sheet_nohud.png 3
expect min(sheet.cells[*].filled) > 0.1
expect min(sheet.cells[*].label_ink) > 0.01
do hud on; step 1
shot review_crop.png crop
do ui_click Play; step 2
expect game.clicks == 1
```
