# Request from MechBattleArc: a grid module for tactics games (line of sight, cover, costed paths, reach)

**Why.** MOLTEN BREACH (and Moonbase Raid) implement the same board rules by hand in `src/sim.c`: line of sight over
tiles with blocking and partial cover, firing arcs and range bands, movement paths with terrain costs (water, rubble,
forest), reachable-tile flood fill within a movement budget, and drop-zone / edge-tile queries. The new `tactics`
template does plain moves only. Moonbase Raid needed the same pieces again.

**Ask.** `arc_board` (plain data, fits `save_state`, deterministic, integer maths):
- `arc_board_init(b, w, h)`; per tile a `cost` (0 = blocked, else move points) and `cover` (0 none, 1 half, 2 full,
  blocks sight) and an occupant id; `arc_board_set_tile`, `_set_occupant`.
- `arc_board_los(b, x0, y0, x1, y1, &cover)`: Bresenham-symmetric (A sees B iff B sees A), returns visible and the
  best cover on the way (corners count as half cover); occupants optionally block.
- `arc_board_reach(b, x, y, budget, diag, out_cost)`: flood fill of tiles reachable within the budget (8- or
  4-connected, no diagonal corner cutting through blocked tiles), with `arc_board_path(b, from, to, out, max)` for
  the cheapest route (A*, ties broken in a fixed order).
- `arc_board_arc(facing, x0, y0, x1, y1)`: front / side / rear sector for 8 facings; `arc_board_range` (Chebyshev or
  Manhattan, chosen at init).
- `arc_board_edge_tiles(b, sides_mask, free_only, out, max)` for drop zones and exits.
- Drawing helpers optional: `arc_board_draw_tiles(b, list, rgb, style)`.

**Game side (ready / to do).** The game would keep its own rules (heat, armour facing, damage) and replace its LOS,
cover, reach and path code with these, keeping its tests (`smoke_los.py`, `smoke_walls.py`, `smoke_withdraw.py`) as
the check that behaviour is unchanged.

## Acceptance

Described in words (ARC self-test): LOS symmetric on 10 000 random pairs on a random board; reach equals a brute-force
Dijkstra on 200 random boards; paths never cut a blocked corner; arcs for all 8 facings; byte-identical across runs.

---

## Done in ARC 0.9.9 (15 Nov 2026)

`arc_board` is in `arc.h` (feature macro `ARC_HAS_BOARD`). It is plain data (fixed arrays up to 128 x 128), so keep it
in the game struct and it saves with `save_state`. All maths is integer.

- `arc_board_init(&b, w, h, ARC_BOARD_CHEBYSHEV | ARC_BOARD_MANHATTAN)`: every tile cost 1, no cover, empty.
- `arc_board_set_tile(&b, x, y, cost, cover)`: cost 0 = blocked; cover `ARC_COVER_NONE` / `_HALF` / `_FULL` (full
  blocks sight). `arc_board_set_occupant(&b, x, y, id)` (0 = empty); read back with `arc_board_cost` / `_cover` /
  `_occupant`. `b.unit_cover` sets the cover a unit in the way gives (0: units don't block sight).
- `arc_board_los(&b, x0, y0, x1, y1, &cover)`: symmetric (A sees B exactly when B sees A). Returns visible and the best
  cover on the way. A line that passes exactly through a tile corner counts half cover on each side; two full
  blocks at a corner stop it. Units in the way count as `b.unit_cover`.
- `arc_board_reach(&b, x, y, budget, flags, cost_out)`: `cost_out[y * w + x]` is the cheapest cost or -1.
  `ARC_BOARD_DIAGONAL` = 8-connected; diagonals never cut a blocked corner; occupied tiles are not entered unless
  `ARC_BOARD_IGNORE_UNITS`.
- `arc_board_path(&b, x0, y0, x1, y1, flags, out_xy, max, &total_cost)`: A*, ties broken in a fixed order; returns
  the step count or -1. `ARC_BOARD_ONTO_TARGET` lets the path end on an occupied tile (melee, boarding).
- `arc_board_arc(facing, x0, y0, x1, y1)`: `ARC_ARC_FRONT` / `_LEFT` / `_RIGHT` / `_REAR` for 8 facings (front and
  rear 120 degrees, sides 60, exact integer test). Facings 0..7 go round in 45-degree steps from +x toward +y; `arc_board_dir` gives a facing's
  step.
- `arc_board_range` (Chebyshev or Manhattan, as set at init); `arc_board_edge_tiles(&b, ARC_EDGE_NORTH | ..., free_only,
  out, max)` for drop zones and exits.

Drawing: use the new ground markers (`arc_draw_marker`, see `mechbattlearc-ground-markers.md`) rather than a board
draw helper. `samples/board/board.c` shows both together.

Acceptance (ARC self-test, `tests/test_v099_board.c`):
- line of sight on 10 000 random pairs: 0 asymmetric;
- reach equals a brute-force Dijkstra on 200 random boards;
- 803 paths checked, none cuts a blocked corner, all as cheap as reach says;
- arcs for all 8 facings; byte-identical results across runs.

To do in the game: replace the LOS, cover, reach and path code in `src/sim.c` with these and keep `smoke_los.py`,
`smoke_walls.py` and `smoke_withdraw.py` as the check that behaviour is unchanged. If a rule differs (for example how
corners count), tell me which test case and I can add a flag.
