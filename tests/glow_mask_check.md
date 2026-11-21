# ARC 0.10.15 (Grand Token Acquisition): glow masks, lamps that switch on per draw

Two boxes share one mesh and one mask (red, green and blue stripes). The left one lights only its red group, the right
one only its blue group (`samples/lights`, `glow_mask on`). The picture test (`tests/test_post.c`, look `L_GLOW`)
checks the pixels on every renderer.

```arc-check
game samples/lights/lights.c
do step 2; glow_mask on; step 2; frame
expect frame.meshes[name=glow_test].count >= 2
expect frame.meshes[name=glow_test].on_screen == 2
expect game.glow_mask == true
shot glow_mask.png
```

## Off: no lamp boxes

```arc-check
game samples/lights/lights.c
do step 2; frame
expect frame.meshes[name=glow_test] missing
expect game.glow_mask == false
```
