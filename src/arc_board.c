/* ARC 0.9.9 boards for tactics games (MechBattleArc's MOLTEN BREACH and Moonbase Raid asked for it): tiles with a move
   cost, cover and an occupant; symmetric line of sight that reports cover; movement reach within a budget; cheapest
   paths; firing arcs; range; edge tiles. All integer maths, no allocation (scratch arrays are static), deterministic.
   Names start with brd_ (one translation unit). */
#include "arc_internal.h"

#define BRD_N (ARC_BOARD_MAX * ARC_BOARD_MAX)
static bool brd_in(const arc_board *b, int x, int y) { return b && x >= 0 && y >= 0 && x < b->w && y < b->h; }
static int brd_i(const arc_board *b, int x, int y) { return y * b->w + x; }

bool arc_board_init(arc_board *b, int w, int h, int metric) {
    if (!b) return false;
    if (w < 1 || h < 1 || w > ARC_BOARD_MAX || h > ARC_BOARD_MAX) { arc_log("arc_board_init: %d x %d (each side 1..%d)", w, h, ARC_BOARD_MAX); return false; }
    memset(b, 0, sizeof *b);
    b->w = w; b->h = h; b->metric = metric == ARC_BOARD_MANHATTAN ? ARC_BOARD_MANHATTAN : ARC_BOARD_CHEBYSHEV;
    memset(b->cost, 1, (size_t)w * (size_t)h);
    return true;
}
void arc_board_set_tile(arc_board *b, int x, int y, int cost, int cover) {
    if (!brd_in(b, x, y)) return;
    int i = brd_i(b, x, y);
    b->cost[i] = (uint8_t)arc_clampi(cost, 0, 255); b->cover[i] = (uint8_t)arc_clampi(cover, 0, 2);
}
void arc_board_set_occupant(arc_board *b, int x, int y, int id) { if (brd_in(b, x, y)) b->occ[brd_i(b, x, y)] = (int16_t)arc_clampi(id, 0, 32767); }
int arc_board_cost(const arc_board *b, int x, int y) { return brd_in(b, x, y) ? b->cost[brd_i(b, x, y)] : 0; }
int arc_board_cover(const arc_board *b, int x, int y) { return brd_in(b, x, y) ? b->cover[brd_i(b, x, y)] : ARC_COVER_FULL; }
int arc_board_occupant(const arc_board *b, int x, int y) { return brd_in(b, x, y) ? b->occ[brd_i(b, x, y)] : 0; }

/* what a tile in the way gives: its own cover, or the unit standing on it (unit_cover) when that is more */
static int brd_block(const arc_board *b, int x, int y) {
    if (!brd_in(b, x, y)) return ARC_COVER_FULL;
    int i = brd_i(b, x, y), c = b->cover[i];
    if (b->occ[i] && b->unit_cover > c) c = b->unit_cover;
    return c;
}
/* Line of sight between tile centres. The tiles the line passes through (not the two ends) give their cover: full
   blocks, half is reported. Where the line passes exactly through a corner, the two tiles that only touch it count as
   half each way: one full tile there is half cover, two block. The walk compares (1 + 2i) dy with (1 + 2j) dx, so it
   visits the same tiles in both directions: A sees B exactly when B sees A, with the same cover. */
bool arc_board_los(const arc_board *b, int x0, int y0, int x1, int y1, int *cover_out) {
    int best = 0;
    if (cover_out) *cover_out = 0;
    if (!brd_in(b, x0, y0) || !brd_in(b, x1, y1)) return false;
    int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x1 > x0 ? 1 : -1, sy = y1 > y0 ? 1 : -1;
    int x = x0, y = y0;
    for (int ix = 0, iy = 0; ix < dx || iy < dy;) {
        long a = (long)(1 + 2 * ix) * dy, c = (long)(1 + 2 * iy) * dx;
        if (a == c) {   /* through a corner: the two side tiles touch the line only there */
            int s1 = brd_block(b, x + sx, y), s2 = brd_block(b, x, y + sy);
            int full = (s1 == ARC_COVER_FULL) + (s2 == ARC_COVER_FULL);
            if (full == 2) return false;
            if (full == 1 && best < ARC_COVER_HALF) best = ARC_COVER_HALF;
            x += sx; y += sy; ix++; iy++;
        } else if (a < c) { x += sx; ix++; }
        else { y += sy; iy++; }
        if (x == x1 && y == y1) break;
        int k = brd_block(b, x, y);
        if (k == ARC_COVER_FULL) return false;
        if (k > best) best = k;
    }
    if (cover_out) *cover_out = best;
    return true;
}

int arc_board_range(const arc_board *b, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    return b && b->metric == ARC_BOARD_MANHATTAN ? dx + dy : (dx > dy ? dx : dy);
}

/* Firing arcs: front 120 degrees around the facing, rear 120 opposite, a 60-degree side on each side between them.
   Exact in integers: with d = dot(facing, to target) and c = cross, |c| <= sqrt(3) d is inside 60 degrees. */
static const int brd_dir[8][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };
int arc_board_arc(int facing, int x0, int y0, int x1, int y1) {
    int f = ((facing % 8) + 8) % 8, tx = x1 - x0, ty = y1 - y0;
    if (!tx && !ty) return ARC_ARC_FRONT;
    long d = (long)brd_dir[f][0] * tx + (long)brd_dir[f][1] * ty, c = (long)brd_dir[f][0] * ty - (long)brd_dir[f][1] * tx;
    if (c * c <= 3 * d * d) return d > 0 ? ARC_ARC_FRONT : ARC_ARC_REAR;
    return (c > 0) ? ARC_ARC_RIGHT : ARC_ARC_LEFT;   /* which side, turning from the facing toward +y */
}
int arc_board_dir(int facing, int *dx, int *dy) { int f = ((facing % 8) + 8) % 8; if (dx) *dx = brd_dir[f][0]; if (dy) *dy = brd_dir[f][1]; return f; }

/* ---- reach and paths: Dijkstra / A* on a binary heap keyed (cost, then tile index): ties always break the same way ---- */
static int brd_cost[BRD_N], brd_from[BRD_N], brd_heap[BRD_N * 8 + 8], brd_hkey[BRD_N * 8 + 8], brd_hn;
static uint8_t brd_done[BRD_N];
static void brd_push(int key, int i) {
    int k = brd_hn++; brd_heap[k] = i; brd_hkey[k] = key;
    while (k > 0) {
        int p = (k - 1) / 2;
        if (brd_hkey[p] < brd_hkey[k] || (brd_hkey[p] == brd_hkey[k] && brd_heap[p] <= brd_heap[k])) break;
        int t = brd_heap[p]; brd_heap[p] = brd_heap[k]; brd_heap[k] = t; t = brd_hkey[p]; brd_hkey[p] = brd_hkey[k]; brd_hkey[k] = t; k = p;
    }
}
static int brd_pop(int *key) {
    int top = brd_heap[0]; *key = brd_hkey[0];
    brd_hn--; brd_heap[0] = brd_heap[brd_hn]; brd_hkey[0] = brd_hkey[brd_hn];
    for (int k = 0;;) {
        int l = 2 * k + 1, r = l + 1, m = k;
        if (l < brd_hn && (brd_hkey[l] < brd_hkey[m] || (brd_hkey[l] == brd_hkey[m] && brd_heap[l] < brd_heap[m]))) m = l;
        if (r < brd_hn && (brd_hkey[r] < brd_hkey[m] || (brd_hkey[r] == brd_hkey[m] && brd_heap[r] < brd_heap[m]))) m = r;
        if (m == k) break;
        int t = brd_heap[m]; brd_heap[m] = brd_heap[k]; brd_heap[k] = t; t = brd_hkey[m]; brd_hkey[m] = brd_hkey[k]; brd_hkey[k] = t; k = m;
    }
    return top;
}
/* may a mover step from (x, y) by (ddx, ddy)? Returns the cost of entering, or 0 when it can't */
static int brd_step(const arc_board *b, int x, int y, int ddx, int ddy, int flags, int tx, int ty) {
    int nx = x + ddx, ny = y + ddy;
    if (!brd_in(b, nx, ny)) return 0;
    int i = brd_i(b, nx, ny), c = b->cost[i];
    if (!c) return 0;
    if (b->occ[i] && !(flags & ARC_BOARD_IGNORE_UNITS) && !(nx == tx && ny == ty && (flags & ARC_BOARD_ONTO_TARGET))) return 0;
    if (ddx && ddy && (!b->cost[brd_i(b, x + ddx, y)] || !b->cost[brd_i(b, x, y + ddy)])) return 0;   /* no corner cutting past a blocked tile */
    return c;
}
static void brd_search(const arc_board *b, int sx, int sy, int budget, int flags, int tx, int ty) {
    int n = b->w * b->h;
    for (int i = 0; i < n; i++) { brd_cost[i] = -1; brd_from[i] = -1; brd_done[i] = 0; }
    brd_hn = 0;
    int s = brd_i(b, sx, sy); brd_cost[s] = 0;
    bool astar = tx >= 0;
    int dirs = (flags & ARC_BOARD_DIAGONAL) ? 8 : 4;
    static const int order[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };   /* orthogonal steps first */
    brd_push(astar ? arc_board_range(b, sx, sy, tx, ty) : 0, s);
    while (brd_hn > 0) {
        int key, i = brd_pop(&key);
        if (brd_done[i]) continue;
        brd_done[i] = 1;
        int x = i % b->w, y = i / b->w;
        if (astar && x == tx && y == ty) return;
        for (int k = 0; k < dirs; k++) {
            int d = order[k], c = brd_step(b, x, y, brd_dir[d][0], brd_dir[d][1], flags, tx, ty);
            if (!c) continue;
            int j = brd_i(b, x + brd_dir[d][0], y + brd_dir[d][1]), nc = brd_cost[i] + c;
            if (budget >= 0 && nc > budget) continue;
            if (brd_cost[j] < 0 || nc < brd_cost[j]) {
                brd_cost[j] = nc; brd_from[j] = i;
                int h = 0;
                if (astar) { int ddx = abs(tx - (x + brd_dir[d][0])), ddy = abs(ty - (y + brd_dir[d][1])); h = dirs == 8 ? (ddx > ddy ? ddx : ddy) : ddx + ddy; }
                brd_push(nc + h, j);
            }
        }
    }
}
int arc_board_reach(const arc_board *b, int x, int y, int budget, int flags, int16_t *out_cost) {
    if (!brd_in(b, x, y)) return 0;
    brd_search(b, x, y, budget < 0 ? 0 : budget, flags, -1, -1);
    int n = b->w * b->h, count = 0;
    for (int i = 0; i < n; i++) { if (out_cost) out_cost[i] = (int16_t)brd_cost[i]; count += brd_cost[i] >= 0; }
    return count;
}
int arc_board_path(const arc_board *b, int fx, int fy, int tx, int ty, int flags, int *out_xy, int max, int *total_cost) {
    if (total_cost) *total_cost = -1;
    if (!brd_in(b, fx, fy) || !brd_in(b, tx, ty)) return -1;
    if (fx == tx && fy == ty) { if (total_cost) *total_cost = 0; return 0; }
    brd_search(b, fx, fy, -1, flags, tx, ty);
    int t = brd_i(b, tx, ty);
    if (brd_cost[t] < 0) return -1;
    if (total_cost) *total_cost = brd_cost[t];
    int n = 0; for (int i = t; i != brd_i(b, fx, fy); i = brd_from[i]) n++;   /* steps, not counting the start */
    if (out_xy) {
        int k = n - 1;
        for (int i = t; i != brd_i(b, fx, fy); i = brd_from[i], k--) if (k < max) { out_xy[2 * k] = i % b->w; out_xy[2 * k + 1] = i / b->w; }
    }
    return n;
}
int arc_board_edge_tiles(const arc_board *b, int sides, bool free_only, int *out_xy, int max) {
    if (!b) return 0;
    int n = 0;
    for (int y = 0; y < b->h; y++) for (int x = 0; x < b->w; x++) {
        bool on = ((sides & ARC_EDGE_NORTH) && y == 0) || ((sides & ARC_EDGE_SOUTH) && y == b->h - 1) ||
                  ((sides & ARC_EDGE_WEST) && x == 0) || ((sides & ARC_EDGE_EAST) && x == b->w - 1);
        if (!on) continue;
        int i = brd_i(b, x, y);
        if (free_only && (!b->cost[i] || b->occ[i])) continue;
        if (out_xy && n < max) { out_xy[2 * n] = x; out_xy[2 * n + 1] = y; }
        n++;
    }
    return n;
}
