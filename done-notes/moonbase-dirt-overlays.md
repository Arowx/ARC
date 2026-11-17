# Request from Moonbase Raid: dirt and wear overlays made easy (scattered, masked, see-through layers)

**What the game does today, by hand.** Moonbase Raid's 3D floor needed dirt that never visibly repeats. What worked
(Allan's idea: a grayscale breakup mask OR-ed over the dirt) is built from parts ARC already has, but it takes a page
of game code and an offline Python step:

1. **Grime textures with a breakup mask in the alpha** (offline, Python): eight RGBA 512x512 images. Colour is a
   Ludo grime texture darkened (scratches kept pale). Alpha is `dirt amount x breakup mask x edge fade`, where:
   - dirt amount is how far each pixel is from the texture's mean (darker = grime, lighter = scratch);
   - the breakup mask is a 5-octave value-noise fbm thresholded with a soft ramp (`clamp((fbm - t) / .12)`), OR-ed
     (max) with a fainter second fbm, so the dirt comes in ragged islands;
   - the edge fade is a radial falloff, so overlapping squares blend with no visible edge.
   Each of the eight uses a different crop size of the source (the dirt's scale), threshold and seed.
2. **A near-clean floor** under them (the panel texture with only a trace of wear).
3. **Two overlays per floor tile at draw time**: a flat 1x1 quad (`arc_mb_quad`) textured with one of the eight, drawn
   with `arc_draw_mesh_styled`, `transparency = 1 - strength` and `depth_pull = .002`. Each one gets:
   - a hashed variant;
   - any angle;
   - a size of 1.2 to 2.2 tiles;
   - an offset of up to 0.4 tile;
   - a slightly different height per layer.
   The overlays are bigger than the tile and overlap the neighbours, so the dirt crosses the seams and no
   combination repeats.
4. **A density mask over the world** (`dirtLevel()`): smooth value noise (cells about 5 tiles across, smoothstep
   interpolation) plus extra near walls (+.09 each neighbour) and doorways (+.18), quantised to four strengths
   (.35, .6, .85, 1).

It looks right (scratches, spills, tracks and dust in patches, heavier where people walk and along walls), but every
game that wants it has to rebuild all four parts.

## What would make it easy

**A. A mask-making helper** (in `tools/`, or `arc_texture_*` at load time):

```
python tools/grime.py SOURCE.png OUT_PREFIX --count 8 --sizes 260,1000 --threshold .4,.55 --seed 1
```

It writes `OUT_PREFIX0..N.png` as above: crops of different scales, dirt amount to alpha, an fbm breakup mask
(threshold, softness, octaves, an optional second mask OR-ed in), and a radial edge fade. The same thing at run time
would be `arc_texture arc_texture_breakup(arc_texture src, const arc_breakup *b)` (the mask computed on the CPU once).

**B. A stamp layer for surfaces**: scattered see-through quads managed by ARC, so the game only says where the
surface is and how dirty:

```c
typedef struct {
    arc_texture tex[8]; int count;      /* the masked overlays (alpha = where the dirt is) */
    float size_min, size_max;           /* world units across (bigger than a tile, so they overlap) */
    float jitter;                       /* random offset from each cell's centre, world units */
    float per_cell;                     /* stamps per cell (2 in Moonbase Raid), fractional = some cells fewer */
    float rotate;                       /* 0..1 of a full random turn (0 for things with a direction) */
    float strength;                     /* overall opacity */
    float cell;                         /* the grid the stamps are scattered on, world units (1 = a tile) */
    uint32_t seed;
} arc_stamps;

/* Lay stamps over the floor cells [x0,x1) x [z0,z1) at height y. density(x, z) 0..1 (NULL = 1) scales each
   stamp's opacity, so the game supplies its own "dirtier by walls" rule. Drawn with depth_pull, sorted with the
   see-through pass, in tint (remembered tiles darker). */
void arc_draw_stamps(const arc_stamps *s, float x0, float z0, float x1, float z1, float y,
                     float (*density)(void *ud, int cx, int cz), void *ud, uint32_t tint);
```

ARC can batch these better than the game can (one instanced draw per texture instead of hundreds of separate
see-through meshes), and it could also run on walls (a normal per call) and on terrain.

**C. A density noise helper**: `float arc_noise_value2(float x, float z, uint32_t seed)` (smoothstep value noise,
0..1) and `arc_noise_fbm2(x, z, octaves, seed)`. Moonbase Raid hand-rolled both: for the dirt patches and the masks.

**D. Better still, in the shader** (this goes with `moonbase-anti-tiling.md`): a detail layer with its own breakup mask
texture, both sampled in world space at their own scales and turns, with the mask thresholded per material:

```c
arc_texture detail, detail_mask;   /* dirt colour (alpha = amount) and a grayscale breakup mask */
float detail_scale, mask_scale;    /* different scales, so the two never line up */
float mask_threshold, mask_softness;
```

One draw per tile and no overlay quads. The stamps in B would stay useful for big individual marks (spills, tracks,
blast marks), like decals but scattered.

## Notes from doing it by hand

- Overlay squares must be bigger than a cell and fade at their edges, or the grid shows through.
- Use two or more layers per cell with different variants: one layer alone still reads as a pattern.
- Scratches (lighter than the floor) and grime (darker) can share one texture: colour picks which, alpha says how much.
- `depth_pull = .002` was enough to stop the overlays fighting the floor; layers also sit .001 apart.
- Light the overlays like the floor (they are lit in Moonbase Raid, because they go through `arc_draw_mesh_styled`).

**Where it is in the game.** `game/src/view3d.cpp`: `dirtLevel()`, the grime block after the floor draw, and the
overlay meshes in `View3D::build()`. The textures are `content/textures/grime0..7.png`, from Ludo's grime textures
in `game/res/models3d/floor/`.

**Test case.** A 32x32 floor of one plain texture with the stamp layer at 2 per cell, rotate 1, sizes 1.2–2.2: no
seams at cell edges, and the rendered image's autocorrelation at a lag of one cell close to that at a random lag.
With `density` returning 0 everywhere, nothing is drawn.
