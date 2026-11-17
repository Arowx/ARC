# Request from Moonbase Raid / Operation Nimrod: 2D batch limit drops draws silently

**What happened.** Operation Nimrod's map is 136 x 63 tiles. The game bakes its floor into a render target, one tile at a time. Each tile alternates a textured quad with plain fills. Every texture change starts a new 2D batch, and once a frame passes `ARC__MAX_BATCHES` (4096) the rest are dropped **without any message**. Half the floor came out black.

**Workaround in the game.** For Nimrod only, the bake now runs in two passes: all textured tiles first, then all fills. This is in `game/src/floor.cpp`.

**Requests:**
1. When the batch limit is hit, print a one-time `ARC error` line naming the limit, so agents see it in headless runs. Count it in `stats` / `dump` too.
2. Consider raising the limit, or flushing and starting a new set of batches when it is reached, rather than dropping them.

**Test case.** In one frame, draw 5000 quads that alternate between a texture and plain rectangles into a 6528 x 3024 render target, then check the last quad is drawn.

---

## Done in ARC 0.6.4 (4 Nov)

1. **The error line.** The first time any per-frame limit fills, ARC prints one line naming it, e.g.
   `ARC error draw limit: more than 65536 2D batches in one frame; later ones are dropped (counted in stats.dropped).`
   This covers all per-frame limits: 2D vertices and batches, mesh instances, immediate 3D vertices, line points,
   skinning matrices and render passes. Dropped draws are counted in `arc_stats().dropped` (this frame), and the dump
   and `stats` command show `dropped` and `dropped_total`.
2. **The limit.** The 2D batch limit went from 4,096 to 65,536 a frame. Draws are still dropped beyond it rather than
   flushed, but now you are told.
3. **Your test case** is in the self-test (`tests/test_v06.c`, `v064_*`). It fails at the old limit and passes now.

Nimrod's two-pass bake is still worth keeping: it is about 8,600 batches in one pass and 2 in two passes, and fewer
batches mean less CPU time. A future ARC version may stop plain fills from breaking batches at all.

## And in ARC 0.6.5 (4 Nov)

Plain fills no longer break batches: untextured rectangles, lines and triangles join whatever batch is open, and a
batch of only plain shapes takes the next texture. Your original one-pass bake (a texture and fills alternating) is
now a single batch, so the two-pass workaround in `game/src/floor.cpp` is optional. It does no harm either way.
Pictures are unchanged (Moonbase Raid pixel-identical).
