# Request from Grand Token Acquisition: a per-draw emissive mask (lamps that switch on per instance)

## What

A glow mask in `arc_mesh_style`, the emissive cousin of `paint_mask`: a texture in the mesh's UVs whose red, green and
blue channels mark three groups of pixels, and per-draw colours and strengths for each group. Lit pixels glow
(unlit, added after lighting, HDR so values above 1 bloom); everything else is drawn exactly as today.

    glow = emissive_mask.r * glow_rgb[0] * glow[0]
         + emissive_mask.g * glow_rgb[1] * glow[1]
         + emissive_mask.b * glow_rgb[2] * glow[2]

Strengths of 0 mean "off", so one mesh and one mask serve every instance, each with its own lamps on or off.

## Why

GTA's traffic needs indicators (left or right, blinking) and brake lights per car. The material's emissive map
(`arc_material.emissive`, `emissive_rgb`) can't do it, because it belongs to the mesh: every hatchback on the road shares
one, so all their lamps would glow together and nothing could blink. The per-draw `emissive` glows the whole mesh
evenly, and the paint mask's colours are lit like paint, so they can't glow.

The game's workaround (in GTAarc today): `tools/car_lights.py` cuts each car model's lamp triangles out into four extra
meshes (front/rear, left/right) that are drawn with `arc_draw_mesh_ex(..., emissive 1)` plus an additive halo copy.
It works, but it means 4 extra meshes per car model, z-fighting care (the lamps are nudged 2 cm out), and lamps
limited to whole triangles. A mask would light exactly the painted pixels on the car's own texture.

Other uses: house windows lit at night, the HQ's screens, the Rate Limiter cars' light bars, shop signs, a robot's
eyes; any model where parts glow some of the time or on some instances.

## API I'd like

```c
/* 0.10.13: glowing parts through a mask in the mesh's UVs (all renderers; skinned meshes too).
   red, green and blue each mark a group of pixels; group i glows glow_rgb[i] x glow[i] (0 = off; above 1 blooms),
   unlit and added after lighting. Draws with the same mesh, mask, colours and strengths still batch together. */
typedef struct {
    ...
    arc_texture emissive_mask;   /* {0} = none */
    uint32_t glow_rgb[3];        /* each group's colour (0 = white) */
    float    glow[3];            /* each group's strength, 0 = off */
} arc_mesh_style;
#define ARC_HAS_EMISSIVE_MASK 1013
```

Nice to have: `mesh2arcm.py --glow-mask IMG` to store a default mask with the mesh (`arc_mesh_glow_mask(m)` to fetch it),
so a game doesn't have to load and pass the image itself; a style mask, when set, would replace it.

## Check (should pass once done)

```arc-check
game samples/lights
do step 2; glow_mask on; step 2; frame
expect frame.meshes[name=glow_test].count >= 2
shot glow_mask.png
```

(`glow_mask on` would be a debug command added to samples/lights that draws two instances of one mesh with the same
emissive mask, the first with `glow = {3, 0, 0}` and the second with `glow = {0, 0, 2}`: the shot should show only the
red-masked part glowing on the first and only the blue-masked part on the second, both blooming, with the rest of each
model lit normally.)

## In the game meanwhile

Separate lamp meshes per car model (`tools/car_lights.py`, `draw_car_lights` in GTAarc/src/main.c). Once this lands,
`car_lights.py` would write a mask image per model instead (lamp pixels: red = left indicators, green = right
indicators, blue = brake lights) and `draw_car_lights` becomes three numbers in the car's style.

## Done in ARC 0.10.15

- `arc_mesh_style.emissive_mask`, `glow_rgb[3]`, `glow[3]`, as asked. Group i glows `glow_rgb[i] x glow[i]`
  (0 = white; glow 0 = off), unlit, added after lighting; above about 1 it blooms. Works in the software, GL and
  D3D11 renderers, and on skinned meshes. Draws with the same mesh, mask, colours and strengths batch together, so
  every car with its indicators off is one draw call, the ones blinking left another, and so on. A draw with every
  group at 0 is an ordinary draw.
- **One difference from the request:** while any group glows, the mask takes the place of the mesh's own emissive
  map and emissive colour on that draw. The GL shader already uses 16 texture samplers, which is all WebGL2
  promises, so the mask shares the emissive map's slot. For cars this changes nothing: give them no emissive map.
- **The nice-to-have is in:** `arc_import.py --glow-mask IMG` (or `mesh2arcm.py --glow-mask`) stores the mask in
  the `.arcm` as a trailing `GLOW` block (lossless PNG; `--compress` leaves it alone, and ARC before 0.10.15 just
  ignores it). `arc_mesh_load` sets it as the mesh's own mask. `arc_mesh_glow_mask(m)` reads it back, and
  `arc_mesh_set_glow_mask(m, tex)` sets one at run time. A style's `emissive_mask` replaces it for that draw. The
  import is recorded in `assets/manifest.json` with the other options.
- **For GTA:** have `car_lights.py` write one mask per car model (red = left indicators, green = right
  indicators, blue = brake lights) and import it with `--glow-mask`. `draw_car_lights` then becomes three strengths
  in the car's style, e.g. `st.glow[0] = blink_left ? 2.0f : 0; st.glow[1] = blink_right ? 2.0f : 0;
  st.glow[2] = braking ? 1.5f : 0;` with `glow_rgb = { 0xFFA020, 0xFFA020, 0xFF2010 }`. The four lamp meshes, the
  2 cm nudge and the additive halo copies can go. Keep a halo sprite if you like the flare.
- Your check is `tests/glow_mask_check.md` on `samples/lights` (`glow_mask on|off`, G in the window). It passes and
  runs in `build test`. The post test has a new look, `L_GLOW` (red group on, blue group on, all off, a mesh's own
  mask), checked pixel by pixel: the software renderer and GL (all depth modes) give identical values. D3D11 compiles,
  and `build.bat post-d3d` runs the same look there.
- `#if ARC_HAS_EMISSIVE_MASK` (1015).
