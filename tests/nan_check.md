# ARC 0.10.12 (Moonbase Raid): NaN / Inf scene pixels don't spread through bloom; `post nan on` shows and counts them

```arc-check
game samples/demo/demo.c
args --size 640x360
do hud off; set nan_test 1; step 3
expect frame.post.nan_pixels > 100
expect frame.post.nan_mode == false
do post nan on; step 1
expect frame.post.nan_mode == true
expect frame.post.nan_pixels > 100
```
