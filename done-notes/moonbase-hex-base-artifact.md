# Report from Moonbase Raid: hex tiling on a base texture looks embossed and seamy (0.8.7)

**What happened.** Moonbase Raid set `base_tiling = ARC_TILE_HEX`, `base_rotate = 1` on its outdoor floor tiles (one
box mesh per tile, top face UV 0..1, a 512x512 lunar regolith texture). On Allan's PC (D3D11) and in the software
renderer, the ground became an embossed, geometric pattern: chevron and hexagon shapes, pinkish seams along each
tile's edges, the craters smeared. The detail layer with hex tiling (`detail_tiling = ARC_TILE_HEX`) on the indoor
floor looks fine.

**Guess.** The base texture's hex cells are laid in the mesh's UV space (0..1 per tile) rather than world space,
so each tile holds about one cell and the blend weights show; and the rotated cells may sample outside 0..1 with
clamp addressing (the pink at the edges looks like the texture's border or a wrap problem). The detail layer is in
world space, which is why it works.

**Would help.** Base hex tiling in world space (as the detail layer is), with an `arc_material.base_scale`, and the
texture set to repeat while hex sampling. The game has gone back to its own four variants turned per tile meanwhile.

**Also noted.** With `bloom = .6` and the default `bloom_threshold = 1`, the pale floor and white suits under a point
light glared. `bloom_threshold = 1.6`, `bloom = .35` fixed it. A word in the `arc_post` docs that lit pale surfaces
pass 1 easily under point lights would save the next game the surprise.
