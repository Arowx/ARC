# ARC 0.10.13 (Vector Trader): 2D drawn from (0, 0) inside arc_push_offset2d(safe area) lands in the safe area

```arc-check
game samples/demo/demo.c
args --safe-area 40,0,40,20
do set offset2d_test 1; step 2
expect frame.rects[0].box[0] >= 40
expect frame.texts[0].box[0] >= 40
expect frame.texts[text=SAFE].box[0] == 48
```
