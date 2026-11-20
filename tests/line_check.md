# ARC 0.10.11 (Vector Trader): thick, glowing lines on the demo (its line_test tunable)

```arc-check
game samples/demo/demo.c
args --size 640x360
do hud off; set line_test 1; step 2; shot thick.png
expect frame.lines.width_max >= 3
expect frame.dropped == 0
expect frame.lines.thick >= 5
expect frame.lines.glow_max > 2
```
