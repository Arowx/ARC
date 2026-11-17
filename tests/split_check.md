# Request (test fixture): split-screen viewports (0.10.2)

**Why.** This file checks `tests/split_game.c` the way Droids' split-screen request asked: the same scene drawn in two
side-by-side viewports matches a full-window render of each camera at that size, the HUD code inside each viewport
sees the viewport's own size, and `frame` lists both viewports.

**Ask.** Nothing; the fixture already does this.

## Acceptance

```arc-check
game tests/split_game.c
args --render-all
do step 12
expect game.phase == 3
expect game.window_width == 640
expect game.left_width == 320
expect game.right_width == 320
expect game.left_diff == 0
expect game.right_diff == 0
expect game.left_max <= 2
expect game.right_max <= 2
expect game.viewports == 2
expect count(frame.viewports[*]) == 2
expect frame.viewports[1].rect[0] == 320
expect frame.viewports[0].meshes[name=cube].count == 3
expect frame.texts[*].text contains "P2"
```
