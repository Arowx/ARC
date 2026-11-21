# ARC 0.10.13 (Grand Token Acquisition): a colour map over the terrain, multiplied into the splatted ground

```arc-check
game samples/terrain/terrain.c
do step 2; terrain_colour red; step 2
expect frame.meshes[name=terrain].count >= 1
expect game.colour_map == 1
shot terrain_colour.png
```
