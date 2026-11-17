/* ARC turns: time-unit scheduler (real-time / roguelike / tactical) and tile grid utilities.
 * Ported from the AXE prototype that Droids was built on, so game rules carry over unchanged. */
#include "arc_internal.h"

static const char *k_turns_names[ARC_TURNS_MODE_COUNT] = { "REAL-TIME", "ROGUELIKE", "TACTICAL" };
const char *arc_turns_mode_name(arc_turns_mode m) { return (m >= 0 && m < ARC_TURNS_MODE_COUNT) ? k_turns_names[m] : "?"; }

void arc_turns_init(arc_turns *s, arc_turns_mode mode, int player_team) {
    memset(s, 0, sizeof *s);
    s->mode = mode; s->tu_per_second = 40.0; s->round = 1;
    s->player_team = s->active_team = player_team; s->round_tu = 40; s->ai_delay = 0.15f;
}
static void turns_refill(arc_actor **a, int n, int team) {
    for (int i = 0; i < n; i++) if (a[i]->alive && a[i]->team == team) a[i]->ap = a[i]->ap_max;
}
void arc_turns_set_mode(arc_turns *s, arc_turns_mode mode, arc_actor **a, int n) {
    s->mode = mode;
    for (int i = 0; i < n; i++) if (a[i]->ready_at > s->now) a[i]->ready_at = s->now;   /* nobody stuck in the future */
    s->active_team = s->player_team; turns_refill(a, n, s->player_team); s->ai_timer = 0;
}
void arc_turns_frame(arc_turns *s, double dt) {
    if (s->mode == ARC_TURNS_REALTIME) s->now += dt * s->tu_per_second;
    if (s->ai_timer > 0) s->ai_timer -= (float)dt;
}
bool arc_turns_player_ready(const arc_turns *s, const arc_actor *a) {
    if (!a->alive) return false;
    if (s->mode == ARC_TURNS_TACTICAL) return s->active_team == a->team && a->ap > 0;
    return a->ready_at <= s->now;
}
void arc_turns_spend(arc_turns *s, arc_actor *a, int cost, float speed) {
    if (speed < 0.1f) speed = 0.1f;
    int scaled = (int)((float)cost / speed + 0.5f); if (scaled < 1) scaled = 1;
    if (s->mode == ARC_TURNS_TACTICAL) { a->ap -= scaled; if (a->ap < 0) a->ap = 0; }
    else { double base = a->ready_at > s->now ? a->ready_at : s->now; a->ready_at = base + scaled; }
}
static int turns_ready_ai(const arc_turns *s, arc_actor **a, int n) {
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (!a[i]->alive || a[i]->is_player) continue;
        if (a[i]->ready_at <= s->now && (best < 0 || a[i]->ready_at < a[best]->ready_at)) best = i;
    }
    return best;
}
int arc_turns_next_ai(arc_turns *s, arc_actor **a, int n) {
    switch (s->mode) {
    case ARC_TURNS_REALTIME: return turns_ready_ai(s, a, n);
    case ARC_TURNS_ROGUELIKE: {
        int i = turns_ready_ai(s, a, n); if (i >= 0) return i;
        double next = 1e300; bool player_ready = false;
        for (int k = 0; k < n; k++) {
            if (!a[k]->alive) continue;
            if (a[k]->is_player && a[k]->ready_at <= s->now) player_ready = true;
            if (a[k]->ready_at < next) next = a[k]->ready_at;
        }
        if (player_ready || next >= 1e300) return -1;
        s->now = next;
        return turns_ready_ai(s, a, n);
    }
    case ARC_TURNS_TACTICAL: {
        if (s->active_team == s->player_team || s->ai_timer > 0) return -1;
        for (int k = s->sweep; k < n; k++)
            if (a[k]->alive && !a[k]->is_player && a[k]->team == s->active_team && a[k]->ap > 0) { s->sweep = k + 1; return k; }
        s->sweep = 0;
        for (int k = 0; k < n; k++)
            if (a[k]->alive && !a[k]->is_player && a[k]->team == s->active_team && a[k]->ap > 0) { s->ai_timer = s->ai_delay; return -1; }
        s->round++; s->now += s->round_tu; s->active_team = s->player_team; turns_refill(a, n, s->player_team);
        return -1;
    }
    default: return -1;
    }
}
void arc_turns_end_player_phase(arc_turns *s, arc_actor **a, int n) {
    if (s->mode != ARC_TURNS_TACTICAL || s->active_team != s->player_team) return;
    for (int k = 0; k < n; k++) if (a[k]->is_player) a[k]->ap = 0;
    int enemy = -1;
    for (int k = 0; k < n; k++) if (a[k]->alive && a[k]->team != s->player_team) { enemy = a[k]->team; break; }
    if (enemy < 0) { s->round++; s->now += s->round_tu; turns_refill(a, n, s->player_team); return; }
    s->active_team = enemy; s->sweep = 0; turns_refill(a, n, enemy); s->ai_timer = s->ai_delay;
}
bool arc_turns_waiting_for_player(const arc_turns *s, arc_actor **a, int n) {
    for (int k = 0; k < n; k++) if (a[k]->is_player) return arc_turns_player_ready(s, a[k]);
    return false;
}

/* ---- grid ---- */
bool arc_grid_init(arc_grid *g, arc_arena *a, int w, int h) {
    g->w = w; g->h = h;
    g->cells = a ? (uint8_t *)arc_arena_alloc(a, (size_t)w * h, 1) : (uint8_t *)calloc((size_t)w * h, 1);
    return g->cells != NULL;
}
uint8_t arc_grid_get(const arc_grid *g, int x, int y, uint8_t outside) {
    if (x < 0 || y < 0 || x >= g->w || y >= g->h) return outside;
    return g->cells[y * g->w + x];
}
void arc_grid_set(arc_grid *g, int x, int y, uint8_t v) {
    if (x < 0 || y < 0 || x >= g->w || y >= g->h) return;
    g->cells[y * g->w + x] = v;
}
bool arc_grid_los(const arc_grid *g, int x0, int y0, int x1, int y1, bool (*blocks)(uint8_t)) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        if (x0 == x1 && y0 == y1) return true;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        if ((x0 != x1 || y0 != y1) && blocks(arc_grid_get(g, x0, y0, 255))) return false;
    }
}
/* BFS into scratch memory from the frame arena; returns the goal's predecessor chain via prev[] */
static int *grid_bfs(const arc_grid *g, int sx, int sy, int tx, int ty, bool (*passable)(uint8_t), bool *found) {
    int n = g->w * g->h;
    *found = false;
    if (sx < 0 || sy < 0 || sx >= g->w || sy >= g->h || tx < 0 || ty < 0 || tx >= g->w || ty >= g->h) return NULL;
    int *prev = (int *)arc_arena_alloc(arc_frame(), sizeof(int) * (size_t)n, 4);
    int *queue = (int *)arc_arena_alloc(arc_frame(), sizeof(int) * (size_t)n, 4);
    if (!prev || !queue) return NULL;
    for (int i = 0; i < n; i++) prev[i] = -2;
    int head = 0, tail = 0, start = sy * g->w + sx, goal = ty * g->w + tx;
    prev[start] = -1; queue[tail++] = start;
    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    while (head < tail) {
        int c = queue[head++];
        if (c == goal) { *found = true; break; }
        int cx = c % g->w, cy = c / g->w;
        for (int d = 0; d < 4; d++) {
            int x = cx + DX[d], y = cy + DY[d];
            if (x < 0 || y < 0 || x >= g->w || y >= g->h) continue;
            int id = y * g->w + x;
            if (prev[id] != -2) continue;
            if (id != goal && !passable(g->cells[id])) continue;
            prev[id] = c; queue[tail++] = id;
        }
    }
    return prev;
}
bool arc_grid_path_step(const arc_grid *g, int sx, int sy, int tx, int ty, bool (*passable)(uint8_t), int *nx, int *ny) {
    size_t mark = arc_arena_mark(arc_frame()); bool found; bool ok = false;
    int *prev = grid_bfs(g, sx, sy, tx, ty, passable, &found);
    int start = sy * g->w + sx, goal = ty * g->w + tx;
    if (prev && found && goal != start) {
        int c = goal; while (prev[c] != start) c = prev[c];
        *nx = c % g->w; *ny = c / g->w; ok = true;
    }
    arc_arena_pop(arc_frame(), mark);
    return ok;
}
int arc_grid_path_length(const arc_grid *g, int sx, int sy, int tx, int ty, bool (*passable)(uint8_t)) {
    size_t mark = arc_arena_mark(arc_frame()); bool found; int len = -1;
    int *prev = grid_bfs(g, sx, sy, tx, ty, passable, &found);
    if (prev && found) { len = 0; for (int c = ty * g->w + tx; prev[c] != -1; c = prev[c]) len++; }
    arc_arena_pop(arc_frame(), mark);
    return len;
}
