# Turns and grids (time-unit scheduler, tile grid, line of sight, paths)

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Turns and grids (time-unit scheduler, tile grid, line of sight, paths)** · [quick reference](../QUICK.md) · [full API](../API.md)


One rule set, three ways to play. Every action costs time units (TU); the scheduler decides who may act.
  REALTIME  : world clock follows game time (tu_per_second); actors act when ready_at <= now
  ROGUELIKE : world waits for the player; when nobody is ready the clock jumps to the next actor
  TACTICAL  : side phases with action points (AP = TU budget); enemies act in sweeps
Call arc_turns_frame(&s, arc_dt()) once per tick, then let the player act if ready and loop
arc_turns_next_ai() for the AI. Timers on the world clock (s.now) pause automatically in turn modes.

```c
typedef enum { ARC_TURNS_REALTIME = 0, ARC_TURNS_ROGUELIKE, ARC_TURNS_TACTICAL, ARC_TURNS_MODE_COUNT } arc_turns_mode;
ARC_API const char *arc_turns_mode_name(arc_turns_mode m);
typedef struct { double ready_at; int ap, ap_max; int team; bool alive, is_player; } arc_actor;
typedef struct { arc_turns_mode mode; double now; double tu_per_second; int round, sweep, active_team, player_team, round_tu; float ai_delay, ai_timer; } arc_turns;
ARC_API void arc_turns_init(arc_turns *s, arc_turns_mode mode, int player_team);
ARC_API void arc_turns_set_mode(arc_turns *s, arc_turns_mode mode, arc_actor **actors, int n);
ARC_API void arc_turns_frame(arc_turns *s, double dt);    // once per tick
ARC_API bool arc_turns_player_ready(const arc_turns *s, const arc_actor *a);
ARC_API void arc_turns_spend(arc_turns *s, arc_actor *a, int cost_tu, float speed);    // speed 1 = normal, 2 = twice as fast
ARC_API int arc_turns_next_ai(arc_turns *s, arc_actor **actors, int n);    // index of the AI actor to act now, or -1
ARC_API void arc_turns_end_player_phase(arc_turns *s, arc_actor **actors, int n);    // tactical
ARC_API bool arc_turns_waiting_for_player(const arc_turns *s, arc_actor **actors, int n);
```

Tile grid of game-defined ids (uint8).

```c
typedef struct { int w, h; uint8_t *cells; } arc_grid;
ARC_API bool arc_grid_init(arc_grid *g, arc_arena *a, int w, int h);    // cells from the arena (or malloc if a is NULL)
ARC_API uint8_t arc_grid_get(const arc_grid *g, int x, int y, uint8_t outside);
ARC_API void arc_grid_set(arc_grid *g, int x, int y, uint8_t v);
```

Bresenham line of sight; blocks(tile) says whether a tile blocks sight (end points are not tested)

```c
ARC_API bool arc_grid_los(const arc_grid *g, int x0, int y0, int x1, int y1, bool (*blocks)(uint8_t));
```

breadth-first search: first 4-way step from (sx,sy) toward (tx,ty); false if unreachable.
   The target tile itself need not be passable (so you can path to an enemy).

```c
ARC_API bool arc_grid_path_step(const arc_grid *g, int sx, int sy, int tx, int ty, bool (*passable)(uint8_t), int *nx, int *ny);
```

full path length in steps (or -1), same rules as arc_grid_path_step

```c
ARC_API int arc_grid_path_length(const arc_grid *g, int sx, int sy, int tx, int ty, bool (*passable)(uint8_t));
```

Boards for tactics games (0.9.9): each tile has a move cost (0 = blocked; else the points it takes to enter), cover
   (none, half, full: full blocks sight) and an occupant (a unit id, 0 = empty). Plain data (keep it in your game state);
   integer maths, deterministic, nothing allocated.
     arc_board_init(&G.board, 32, 24, ARC_BOARD_CHEBYSHEV);
     arc_board_set_tile(&G.board, x, y, 2, ARC_COVER_HALF);              // forest: costs 2, half cover
     int16_t cost[ARC_BOARD_MAX * ARC_BOARD_MAX];
     arc_board_reach(&G.board, ux, uy, 6, ARC_BOARD_DIAGONAL, cost);     // cost[y * w + x] >= 0: reachable for that many points
     int cover; if (arc_board_los(&G.board, ux, uy, tx, ty, &cover)) ...  // A sees B exactly when B sees A
   Line of sight runs between tile centres; tiles in between give their cover (full blocks), and where the line passes
   exactly through a corner, the two tiles touching it give half cover each (both full: blocked). The two end tiles
   don't count: add the target tile's own cover (arc_board_cover) if your rules want it. unit_cover makes occupied
   tiles in the way give that cover (ARC_COVER_FULL: units block sight). Moves: entering a tile costs its cost, also
   diagonally; a diagonal step can't cut past a blocked corner; occupied tiles are not entered unless
   ARC_BOARD_IGNORE_UNITS (or the path's target with ARC_BOARD_ONTO_TARGET). Ties between equal paths always break the
   same way. Facings 0..7 go round in 45-degree steps from +x toward +y (arc_board_dir gives the step).

```c
#define ARC_BOARD_MAX 128
enum { ARC_BOARD_CHEBYSHEV = 0, ARC_BOARD_MANHATTAN = 1 };    // how arc_board_range counts
enum { ARC_COVER_NONE = 0, ARC_COVER_HALF = 1, ARC_COVER_FULL = 2 };
enum { ARC_BOARD_DIAGONAL = 1, ARC_BOARD_IGNORE_UNITS = 2, ARC_BOARD_ONTO_TARGET = 4 };    // reach / path flags
enum { ARC_ARC_FRONT = 0, ARC_ARC_LEFT = 1, ARC_ARC_RIGHT = 2, ARC_ARC_REAR = 3 };
enum { ARC_EDGE_NORTH = 1, ARC_EDGE_EAST = 2, ARC_EDGE_SOUTH = 4, ARC_EDGE_WEST = 8 };    // north: y = 0
typedef struct { int w, h, metric; int unit_cover; uint8_t cost[ARC_BOARD_MAX * ARC_BOARD_MAX], cover[ARC_BOARD_MAX * ARC_BOARD_MAX]; int16_t occ[ARC_BOARD_MAX * ARC_BOARD_MAX]; } arc_board;
ARC_API bool arc_board_init(arc_board *b, int w, int h, int metric);    // every tile cost 1, no cover, empty
ARC_API void arc_board_set_tile(arc_board *b, int x, int y, int cost, int cover);
ARC_API void arc_board_set_occupant(arc_board *b, int x, int y, int id);    // 0 = empty
ARC_API int arc_board_cost(const arc_board *b, int x, int y);    // 0 outside the board
ARC_API int arc_board_cover(const arc_board *b, int x, int y);    // ARC_COVER_FULL outside
ARC_API int arc_board_occupant(const arc_board *b, int x, int y);
ARC_API bool arc_board_los(const arc_board *b, int x0, int y0, int x1, int y1, int *cover);    // cover: the best on the way
ARC_API int arc_board_range(const arc_board *b, int x0, int y0, int x1, int y1);
```

tiles reachable within budget points: out_cost (w * h, may be NULL) gets the cheapest cost or -1; returns the count

```c
ARC_API int arc_board_reach(const arc_board *b, int x, int y, int budget, int flags, int16_t *out_cost);
```

the cheapest route: steps written to out_xy as x, y pairs (up to max, not counting the start); returns the number
   of steps (0: already there) or -1 when there is none; total_cost (may be NULL) gets its cost

```c
ARC_API int arc_board_path(const arc_board *b, int fx, int fy, int tx, int ty, int flags, int *out_xy, int max, int *total_cost);
ARC_API int arc_board_arc(int facing, int x0, int y0, int x1, int y1);    // where (x1, y1) is seen from (x0, y0): ARC_ARC_*
ARC_API int arc_board_dir(int facing, int *dx, int *dy);    // a facing's step; returns facing 0..7
```

edge tiles (drop zones, exits): sides = ARC_EDGE_* bits; free_only skips blocked and occupied tiles

```c
ARC_API int arc_board_edge_tiles(const arc_board *b, int sides, bool free_only, int *out_xy, int max);
```
