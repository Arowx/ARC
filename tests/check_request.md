# Request (test fixture): walk right and collect a coin

**Why.** This file tests `tools/arc_check.py` and the `frame` command: it is shaped like a real request.

**Ask.** Nothing; the game already does this.

## Acceptance

```arc-check
game tests/check_game.c
expect game.score == 0
expect frame.meshes[name=coin].on_screen == 3
expect frame.texts[*].text contains "SCORE 0"
do step 10; press D; step 40; release D
expect game.score == 10
expect game.x > 2
expect frame.meshes[name=player].on_screen == 1
expect count(frame.meshes[name=coin].items) == 2
expect frame.texts[*].text contains "SCORE 10"
expect frame.rects[*].rgb == "#40e040"
expect ui[label=Restart].kind == "button"
expect frame.meshes[name=nothing] missing
do ui_click Restart; step 2
expect game.restarted == true
expect game.score == 0
```
