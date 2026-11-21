# ARC 0.10.13 (Vector Trader): 20,000 glowing 2D segments in one call, 6 vertices each, nothing dropped
# (no `hud off` here: it hides all 2D, these lines too)

```arc-check
game samples/demo/demo.c
args --size 1280x720
do set lines2d_test 20000; step 2; shot lines.png; stats
expect frame.dropped == 0
expect game.stats.vertices_2d <= 130000
expect game.stats.vertices_2d >= 120000
expect frame.lines.lines2d == 20000
```
