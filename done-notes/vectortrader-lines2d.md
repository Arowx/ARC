# Request from Vector Trader: batched 2D lines with the glow built in, and room for more 2D vertices

**Now.** Vector Trader projects its ships in software (for hidden-line removal) and draws every visible edge with
`arc_draw_line2d`, twice: a wide translucent halo, then the thin bright core. A ship close up is ~1,500 edges, so
~3,000 quads, plus the black face fills. At 2x resolution a busy fight passed the per-frame limit of 1,048,576 2D
vertices; everything after that was dropped, which is the HUD (drawn last): `ARC error draw limit`.

**Workaround in the game.** Level of detail judged in screen units: small ships draw outline edges only, with no halo
or fills. That fixed it (worst scene now ~150k triangles), but the halo still doubles the cost of every big ship, and
a game can't tell how close to the limit it is until it goes over.

## Ask

1. A batched 2D polyline call where one quad per segment carries both the core and a soft halo (the shader fades
   from core to halo across the quad's width), so a glowing line costs one quad instead of two:

```c
/* count points taken in pairs; width: the bright core in pixels; glow: halo width as a multiple of it (0 = none);
   halo_alpha: the halo's strength. Soft edges as arc_draw_line2d. */
ARC_API void arc_draw_lines2d(const arc_v2 *pts, int count, float width, float glow, float halo_alpha, uint32_t rgb);
```

2. The 2D vertex budget: either a bigger default, a setting (`arc_app.max_2d_vertices`), or at least a way to read
   how much of it this frame used (`stats.vertices_2d` in `stats` and the dump), so a game can lower its detail
   before anything is dropped.

## Acceptance

```arc-check
game samples/demo/demo.c
args --size 1280x720
do hud off; set lines2d_test 20000; step 2; shot lines.png; stats
expect frame.dropped == 0
expect game.stats.vertices_2d <= 130000
```

(20,000 glowing segments in one call should take about 120,000 vertices: six per segment, not twelve.)

## Done in ARC 0.10.13

- `arc_draw_lines2d(pts, count, width, glow, halo_alpha, rgb)`: `count / 2` segments from point pairs, in one batch.
  Each segment is one quad (6 vertices) with a soft core `width` pixels wide and a halo `glow` widths out on each
  side at `halo_alpha`. The ends are extended by half the width, so joined segments meet without gaps.
- 2D vertex budget: `arc_app.max_2d_vertices` (0 = the old 1,048,576). Going over drops the rest of the frame's 2D
  and prints the budget with that tip, once.
- Counts: `frame.vertices_2d`, `frame.vertices_2d_max`, `frame.lines.lines2d`, and `arc_stats().vertices_2d` /
  `vertices_2d_max` (also in the dump's `stats`).
- **About your check:** `hud off` hides all 2D, these lines too. So with `hud off`, `vertices_2d <= 130000` passed
  without drawing anything. `tests/lines2d_check.md` drops `hud off` and adds `vertices_2d >= 120000` and
  `frame.lines.lines2d == 20000`. 20,000 segments use 120,000 vertices, with nothing dropped. It runs in
  `build test`. For clean screenshots of a game whose look is 2D lines, don't use `hud off`; hide your HUD text
  with a tunable instead.
- `#if ARC_HAS_LINES2D`.
