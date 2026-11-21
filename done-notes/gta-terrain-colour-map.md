# Request from Grand Token Acquisition: a colour map for arc_terrain (and setting the splat map)

## What

1. **A whole-terrain colour map.** `arc_terrain_desc.color_map` (an `arc_texture` spanning origin .. origin + size, like
   the splat map) multiplied into the splatted ground colour, plus `arc_terrain_set_color_map(t, tex)` to swap it later.
   `{0}` = none (today's look).
2. **Set the whole splat map at once.** `arc_terrain_set_splat(t, const uint8_t *rgba)` (sres x sres RGBA weights) that
   updates the CPU copy as well as the texture, so `arc_terrain_layer_weight` and later `arc_terrain_paint` calls agree
   with it.

## Why

GTA's map has about ten ground kinds (fields, park grass, gardens, sand, dunes, marsh, pavement, rock, woods, sea bed),
generated from an SVG. Four splat layers can't show them all; a colour map baked by `tools/svg2map.py` would keep the
map's look (and the minimap's) on the 3D ground, with the four layers adding texture detail on top.

Today the game blends weights per tile and writes them with `arc_texture_update(arc_terrain_splat_map(t), rgba)`. That
works for drawing, but the terrain's CPU copy still says "all layer 0", so `arc_terrain_layer_weight` lies and any
later `arc_terrain_paint` would upload the stale copy over it.

## API I'd like

```c
typedef struct { ...; arc_texture color_map; } arc_terrain_desc;   /* multiplied in; {0} = none */
ARC_API void arc_terrain_set_color_map(arc_terrain t, arc_texture tex);
ARC_API void arc_terrain_set_splat(arc_terrain t, const uint8_t *rgba);   /* sres * sres * 4 */
```

## Check (should pass once done)

```arc-check
game samples/terrain/terrain.c
do step 2; terrain_colour red; step 2
expect frame.meshes[name=terrain].count >= 1
shot terrain_colour.png
```

(`terrain_colour red` would be a debug command added to samples/terrain that sets a solid red colour map: the shot
should show red ground with the layer textures' detail.)

Workaround in the game meanwhile: per-tile layer mixes (fields = grass + a little sand, gardens = grass + paving,
woods = grass + rock) written through `arc_texture_update` on the splat map texture (see `splat_from_tiles` in
GTAarc/src/main.c).

## Done in ARC 0.10.13

- `arc_terrain_desc.color_map` and `arc_terrain_set_color_map(t, tex)`: a picture over the whole terrain (origin ..
  origin + size, like the splat map) is multiplied into the splatted ground, with the layers' texture on top.
  White changes nothing; `{0}` clears it.
  - It is **copied** when set, so after changing the picture (e.g. `arc_texture_update`), call
    `arc_terrain_set_color_map` again. The texture must be plain RGBA (not compressed, not an array).
  - It works in every renderer through the material's detail layer, at 7 bits a channel. If
    `arc_terrain_desc.material` has a detail layer, the colour map takes its place, with an `ARC hint`.
- `arc_terrain_set_splat(t, rgba)`: `splat_res * splat_res` RGBA weights, rows along +x from the origin. It updates
  the texture and the CPU copy, so `arc_terrain_layer_weight`, later `arc_terrain_paint` calls and scatter agree
  with it. `arc_terrain_splat_res(t)` gives the side in pixels. Replace the `arc_texture_update` in
  `splat_from_tiles` with this.
- `samples/terrain`: the `terrain_colour red|map|off` command. Your check is `tests/terrain_colour_check.md` (plus
  `expect game.colour_map == 1`); it passes and runs in `build test`. The red and fields shots match on the software
  and GL renderers. D3D11 uses the same detail path, so `build.bat post-d3d` covers it.
- `#if ARC_HAS_TERRAIN_COLOR_MAP`.
