# Request from Moonbase Raid: textures that don't visibly repeat (anti-tiling)

**What happened.** Moonbase Raid's 3D floor is one mesh per tile, each a box whose top face maps the whole texture
0..1. The wear (scratches, oil, dust, cart tracks) is baked into six copies of the floor-panel texture, and each tile
picks one by a hash of its position. Even so, Allan sees the dirt repeat: the same smears at the same angle, tile after
tile. The game can't rotate the tile itself (that would break the panel grid between tiles). The real fix is to
sample the grime separately from the panels, in world space, with a random turn and offset per cell. ARC has no way to
do that yet: one texture per mesh, UVs fixed in the mesh, and only `scroll_u` / `scroll_v` in `arc_mesh_style`.

The same problem will come up with every large tiled surface: the lunar regolith outside (four copies there, each
turned a quarter per tile), wall panels, terrain in the mech game, and so on.

## The known techniques (cheapest first)

1. **Per-draw UV transform.** A rotation, mirror, offset and scale applied to the mesh's UVs at draw time. With it a
   game can turn the texture per tile itself, without copying meshes. The texture still repeats, but no longer
   lines up. Cost: nothing (a 2x3 matrix in the vertex shader).
2. **Detail layer in world space.** A second texture multiplied over the base colour (around 1.0 = no change, darker =
   dirt, lighter = scratches), mapped from world XZ (or triplanar) at its own scale, so it is not tied to the mesh's
   UVs or the tile grid. That is the usual "detail map" / "grime overlay". Cost: one extra sample.
3. **Random turn and offset per cell, blended at the seams** (Inigo Quilez, "texture repetition", technique 1). The
   detail layer's UV plane is split into cells, and each cell gets a hashed offset and orientation (mirroring in
   x and/or y, or any rotation). Four neighbouring cells are sampled and blended near the borders so there are no
   seams. Use `textureGrad` with the unmodified derivatives so mipmaps don't break at the cell edges.
   https://iquilezles.org/articles/texturerepetition
4. **Hex tiling** (Heitz and Neyret 2018, "by-example noise"; Morten Mikkelsen, "Practical Real-Time Hex-Tiling",
   JCGT 2022). Each hexagonal cell gets a random offset (and optionally a random rotation); three samples are blended
   with barycentric weights, with a contrast function on the weights to win back crispness. Mikkelsen's version
   samples the original texture directly: no histogram transform or lookup texture, so it drops into any shader. Its
   strength knobs are a rotation amount (0 for things with parallel lines like bricks, up to 1 for dirt, sand and
   gravel) and contrast (0.75 recommended). Works for normal maps too. MIT-licensed reference code exists.
   https://jcgt.org/published/0011/03/05/
5. **Texture bombing** (GPU Gems chapter 20). Small images (a stain, a crack) placed at random spots, turns and
   scales in a grid of cells: good for sparse marks rather than a continuous surface.
   https://developer.nvidia.com/gpugems/gpugems/part-iii-materials/chapter-20-texture-bombing

## What would help

In `arc_mesh_style` (and an `arc_material` default, so a whole mesh can carry it):

```c
float    uv_rotate, uv_scale;          /* 1. the mesh's own UVs turned and scaled at draw time (0 = none / 1) */
arc_v2   uv_offset;
arc_texture detail;                    /* 2. a detail texture multiplied over the colour (0.5 grey = no change ...) */
float    detail_scale;                 /*    ... mapped from world XZ at this many tiles per world unit (0 = mesh UVs) */
float    detail_strength;              /*    0..1 */
int      detail_tiling;                /* 3/4. ARC_TILE_REPEAT (plain), ARC_TILE_CELLS (Quilez), ARC_TILE_HEX (Mikkelsen) */
float    detail_rotate;                /*    random turn per cell, 0..1 (0 = offsets only; bricks), as Mikkelsen's knob */
```

and the same `*_tiling` / `*_rotate` option for the base texture (for regolith, sand, rock, where the base itself is
stochastic), and for terrain if ARC grows terrain.

The minimum that would fix Moonbase Raid today is 1 + 2: the panel texture on the mesh, the grime as a world-space
detail layer with its own scale. 3 or 4 on top of 2 would make the grime never repeat at all.

**Workaround meanwhile.** The game bakes more copies of the floor (the grime turned and mirrored in each) and picks one
per tile by hash. That helps, but the copies still repeat.

**Test case.** A 16x16 plane with a dirt texture tiled 16 times: with `detail_tiling = ARC_TILE_HEX`,
`detail_rotate = 1`, compare against `ARC_TILE_REPEAT`. The autocorrelation of the rendered image at a lag of one
tile should drop to near zero, and no seams should show at cell edges (no pixel jumps above the texture's own local
variation). Also check mipmapping at a grazing angle: no sparkle at the cell edges.
