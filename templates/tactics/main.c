/* {title} - a turn-based tactics game: Toy Skirmish (ARC template "tactics").
 *
 * An 8x8 board of grass and ruins: two knights and an archer (blue) against two orcs and a goblin archer (red). Your
 * turn: click a blue unit, click a highlighted tile to move it (up to MOVE tiles around ruins and units), click a red
 * unit to attack it: knights and orcs strike a neighbour, archers shoot anything within 3 tiles they can see. Each
 * unit moves once and attacks once per turn. SPACE or "End turn" lets the red side act, one unit after another: each
 * walks toward the nearest blue unit and attacks when it can. Win by removing every red unit. R restarts.
 *
 * Where things are:
 *   KINDS[]          each unit kind: team, hit points, attack, range, model and animation clips. LEVEL letters:
 *                    k knight, a archer, o orc, g goblin ('b' / 'r' are knight / orc), '#' ruin, 't' tree.
 *   Game G           every bit of game state (no pointers), so save_state/load_state copy it whole: rewind, seek
 *                    and the replay timeline work out of the box.
 *   rules            move_unit / attack resolve at once (tests read the result straight away); the animation follows
 *                    (G.clock): units walk the path tile by tile, strike or loose an arrow, the target flinches, the
 *                    damage pops up on impact (pending[]), the fallen play their death and dissolve.
 *   tick()           all rules: input, moves, attacks, the red side's turn, win/lose. draw() only draws.
 *   juice            turn banners with a horn, hit flashes, damage numbers (arc_popup_text), sounds at varied pitch
 *                    (arc_play_var), dust where units walk, confetti on a win, markers for moves and targets.
 *   assets/          models, animations, sounds and music made with Ludo; assets/manifest.json says how each was
 *                    made and imported.
 *   dump()           everything a test needs: phase, turn, each unit's kind/team/x/y/hp, counts.
 *   command()        debug commands for agents: select N, move X Y, attack N, end, win, restart.
 *   arc_var          tunables (move, hp_scale): `set move 4` changes them without a rebuild.
 * Grow it: more unit kinds (a row in KINDS), terrain costs, healing, a fog of war (arc_set_world_mask), several
 * levels (more strings in LEVELS), a campaign that keeps survivors.
 */
#include "arc.h"
#include <string.h>
#include <stdlib.h>

#define W 8
#define H 8
#define MAX_UNITS 12
#define MAX_PATH 10
#define MAX_ARROWS 6
#define MAX_PENDING 12
#define STEP_TICKS 9          /* ticks to walk one tile */
enum { PHASE_PLAYER, PHASE_ENEMY, PHASE_WON, PHASE_LOST };
enum { TEAM_BLUE, TEAM_RED };
enum { K_KNIGHT, K_ARCHER, K_ORC, K_GOBLIN, K_KINDS };
enum { ACT_NONE, ACT_ATTACK, ACT_HIT, ACT_DIE };

static const struct { const char *name, *model; int team, hp, attack, range; float scale; } KINDS[K_KINDS] = {
    { "knight", "knight", TEAM_BLUE, 10, 4, 1, 1.10f },
    { "archer", "archer", TEAM_BLUE,  6, 3, 3, 1.10f },
    { "orc",    "orc",    TEAM_RED,  10, 4, 1, 1.20f },
    { "goblin", "goblin", TEAM_RED,   6, 3, 3, 1.00f },
};

typedef struct {
    int kind, team, x, y, hp; bool alive, moved, acted;
    /* presentation, driven by G.clock (in G so rewinds show the same pose) */
    int path[MAX_PATH][2], npath, walk_start, from_x, from_y;   /* the walk: from (from_x, from_y) through path[] */
    int act, act_start, flash_until;
    float face;               /* yaw: the way the model looks */
} Unit;
typedef struct { bool on; float x0, z0, x1, z1; int start, len; } Arrow;
typedef struct { bool on; int at, dmg, target; bool kill; } Pending;   /* an impact still to land: popup, flinch, sound */
typedef struct {
    int phase, turn, selected;          /* selected: unit index or -1 */
    int nunits; Unit u[MAX_UNITS];
    int enemy_timer, enemy_next;        /* ticks until the next red unit acts (so you can watch them) */
    int clock, banner_start;            /* ticks since start; when the turn banner last popped */
    Arrow arrows[MAX_ARROWS];
    Pending pending[MAX_PENDING];
} Game;
static Game G;

/* the board: '#' ruin, 't' tree (blocks too), k/b knight, a archer, o/r orc, g goblin, '.' grass */
static const char *LEVEL[H] = {
    "b......r",
    ".k.##..o",
    "..#....g",
    "a...#...",
    "........",
    "...#..t.",
    ".##.....",
    "t.......",
};

static float *MOVE, *HP_SCALE, v_move, v_hp;   /* tunables */
static arc_grid walls;
static arc_mesh m_floor, m_ruin, m_tree, m_unit[K_KINDS], m_arrow;
static arc_anim a_unit[K_KINDS];
static int clip[K_KINDS][5];        /* idle, walk, attack, hit, death */
static arc_sound s_select, s_move, s_hit, s_arrow, s_death, s_turn, s_win;
static arc_music music;
static arc_camera cam;

static const arc_fx FX_DUST = { .life = 0.5f, .speed = 0.8f, .spread = 1.4f, .radius = 0.15f, .size = 0.18f, .size_end = 0.4f,
                                .color = 0xC8B890, .alpha = 0.5f, .gravity = { 0, 0.6f, 0 }, .drag = 2, .burst = 4 };
static const arc_fx FX_HIT = { .life = 0.35f, .speed = 3.5f, .speed_var = 1.5f, .spread = 1.2f, .size = 0.1f, .size_end = 0.02f,
                               .color = 0xFFF0B0, .color_end = 0xFF8020, .glow = 1.5f, .additive = true, .gravity = { 0, -6, 0 }, .burst = 12 };
static const arc_fx FX_POOF = { .life = 0.9f, .life_var = 0.3f, .speed = 1.2f, .spread = 3.14159f, .radius = 0.25f, .size = 0.35f,
                                .size_end = 0.8f, .color = 0xE0E0E0, .alpha = 0.7f, .gravity = { 0, 1, 0 }, .drag = 2, .burst = 14 };
static const arc_fx FX_CONFETTI = { .life = 2.2f, .life_var = 0.6f, .speed = 7, .speed_var = 3, .spread = 0.6f, .size = 0.12f,
                                    .color = 0xFFE040, .color_end = 0x40A0FF, .gravity = { 0, -5, 0 }, .drag = 0.8f, .spin = 6,
                                    .spin_var = 6, .burst = 60 };

/* ---- rules ---------------------------------------------------------------------------------------------------- */
static int unit_at(int x, int y) { for (int i = 0; i < G.nunits; i++) if (G.u[i].alive && G.u[i].x == x && G.u[i].y == y) return i; return -1; }
static uint8_t occ_cells[W * H];
static bool passable(uint8_t t) { return t == 0; }
static bool blocks_sight(uint8_t t) { return t != 0; }
static arc_grid occ;   /* walls + units, rebuilt before each path search */
static void build_occ(int except) {
    memcpy(occ_cells, walls.cells, sizeof occ_cells);
    for (int i = 0; i < G.nunits; i++) if (G.u[i].alive && i != except) occ_cells[G.u[i].y * W + G.u[i].x] = 1;
}
static int path_len(int who, int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= W || ty >= H) return -1;
    build_occ(who);
    if (occ_cells[ty * W + tx]) return -1;
    return arc_grid_path_length(&occ, G.u[who].x, G.u[who].y, tx, ty, passable);
}
static bool can_move_to(int who, int x, int y) { int n = path_len(who, x, y); return n > 0 && n <= (int)*MOVE; }
static int count(int team) { int n = 0; for (int i = 0; i < G.nunits; i++) n += G.u[i].alive && G.u[i].team == team; return n; }
static int dist(int a, int b) { int dx = G.u[a].x - G.u[b].x, dy = G.u[a].y - G.u[b].y; return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy); }
static bool can_hit(int a, int b) {   /* melee: a neighbour; archers: within range, with a clear line */
    int r = KINDS[G.u[a].kind].range, d = dist(a, b);
    if (d < 1 || d > r) return false;
    return r == 1 || arc_grid_los(&walls, G.u[a].x, G.u[a].y, G.u[b].x, G.u[b].y, blocks_sight);
}
static float cx(int x) { return (float)x + 0.5f; }
static int walk_end(const Unit *u) { return u->walk_start + STEP_TICKS * u->npath; }

static void check_end(void) {
    if (G.phase >= PHASE_WON) return;
    if (count(TEAM_RED) == 0) { G.phase = PHASE_WON; G.banner_start = G.clock; arc_event("won", "\"turn\":%d", G.turn); }
    else if (count(TEAM_BLUE) == 0) { G.phase = PHASE_LOST; G.banner_start = G.clock; arc_event("lost", "\"turn\":%d", G.turn); }
}
static void face_toward(Unit *u, float x, float z) { u->face = arc_yaw_to(x - cx(u->x), z - cx(u->y)); }
static void attack(int a, int b) {
    Unit *A = &G.u[a], *B = &G.u[b];
    int dmg = KINDS[A->kind].attack;
    B->hp -= dmg; A->acted = true;
    bool kill = B->hp <= 0;
    if (kill) { B->hp = 0; B->alive = false; }
    /* looks: strike when it has finished walking; the hit lands a moment later (arrows fly) */
    int start = G.clock > walk_end(A) ? G.clock : walk_end(A);
    A->act = ACT_ATTACK; A->act_start = start; face_toward(A, cx(B->x), cx(B->y));
    int land = start + 18;
    if (KINDS[A->kind].range > 1) {
        arc_play_var(s_arrow, 0.5f, 0, 0.08f);
        for (int i = 0; i < MAX_ARROWS; i++) if (!G.arrows[i].on) {
            G.arrows[i] = (Arrow){ true, cx(A->x), cx(A->y), cx(B->x), cx(B->y), start + 14, 6 + 3 * dist(a, b) };
            land = G.arrows[i].start + G.arrows[i].len; break;
        }
    }
    for (int i = 0; i < MAX_PENDING; i++) if (!G.pending[i].on) { G.pending[i] = (Pending){ true, land, dmg, b, kill }; break; }
    check_end();
}
static void set_path(int i, int tx, int ty) {   /* the tiles it walks through (for the walk), then the move */
    Unit *u = &G.u[i]; int x = u->x, y = u->y; u->npath = 0; u->from_x = x; u->from_y = y;
    build_occ(i);
    while ((x != tx || y != ty) && u->npath < MAX_PATH) {
        int nx, ny; if (!arc_grid_path_step(&occ, x, y, tx, ty, passable, &nx, &ny)) break;
        u->path[u->npath][0] = nx; u->path[u->npath][1] = ny; u->npath++; x = nx; y = ny;
    }
    u->walk_start = G.clock;
    if (u->npath) face_toward(u, cx(u->path[0][0]), cx(u->path[0][1]));
}
static void move_unit(int i, int x, int y) {
    set_path(i, x, y);
    G.u[i].x = x; G.u[i].y = y; G.u[i].moved = true;
    arc_play_var(s_move, 0.5f, 0, 0.1f);
}

static void reset(void) {
    memset(&G, 0, sizeof G); G.selected = -1; G.turn = 1;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        char c = LEVEL[y][x];
        arc_grid_set(&walls, x, y, c == '#' || c == 't' ? (c == 't' ? 2 : 1) : 0);
        int k = c == 'k' || c == 'b' ? K_KNIGHT : c == 'a' ? K_ARCHER : c == 'o' || c == 'r' ? K_ORC : c == 'g' ? K_GOBLIN : -1;
        if (k >= 0 && G.nunits < MAX_UNITS) {
            int hp = (int)((float)KINDS[k].hp * *HP_SCALE + 0.5f);
            G.u[G.nunits++] = (Unit){ .kind = k, .team = KINDS[k].team, .x = x, .y = y, .hp = hp, .alive = true,
                                      .face = KINDS[k].team == TEAM_BLUE ? 1.5707963f : -1.5707963f };
        }
    }
}
static void end_player_turn(void) {
    if (G.phase != PHASE_PLAYER) return;
    G.phase = PHASE_ENEMY; G.selected = -1; G.enemy_next = 0; G.enemy_timer = 40; G.banner_start = G.clock;
    arc_play(s_turn, 0.45f, 0);
}
static void enemy_act(int i) {   /* walk toward the nearest blue unit, attack when it can */
    Unit *e = &G.u[i];
    for (int j = 0; j < G.nunits; j++) if (G.u[j].alive && G.u[j].team == TEAM_BLUE && can_hit(i, j)) { attack(i, j); return; }
    int best = -1, bestd = 1 << 30;
    for (int j = 0; j < G.nunits; j++) if (G.u[j].alive && G.u[j].team == TEAM_BLUE) {
        build_occ(i); occ_cells[G.u[j].y * W + G.u[j].x] = 0;   /* path to the target's own cell */
        int d = arc_grid_path_length(&occ, e->x, e->y, G.u[j].x, G.u[j].y, passable);
        if (d > 0 && d < bestd) { bestd = d; best = j; }
    }
    if (best < 0) return;
    int x = e->x, y = e->y;
    for (int step = 0; step < (int)*MOVE; step++) {   /* walk up to MOVE tiles, stopping where it can strike */
        build_occ(i); occ_cells[G.u[best].y * W + G.u[best].x] = 0;
        int nx, ny;
        if (!arc_grid_path_step(&occ, x, y, G.u[best].x, G.u[best].y, passable, &nx, &ny) || unit_at(nx, ny) >= 0) break;
        x = nx; y = ny;
        int ox = e->x, oy = e->y; e->x = x; e->y = y; bool ok = can_hit(i, best); e->x = ox; e->y = oy;
        if (ok) break;
    }
    if (x != e->x || y != e->y) move_unit(i, x, y);
    for (int j = 0; j < G.nunits; j++) if (G.u[j].alive && G.u[j].team == TEAM_BLUE && can_hit(i, j)) { attack(i, j); return; }
}
static void start_player_turn(void) {
    G.phase = PHASE_PLAYER; G.turn++; G.banner_start = G.clock;
    for (int i = 0; i < G.nunits; i++) G.u[i].moved = G.u[i].acted = false;
    arc_play(s_turn, 0.45f, 0);
}
/* a click on board cell (x, y) in the player's turn */
static void click_cell(int x, int y) {
    int t = unit_at(x, y);
    if (t >= 0 && G.u[t].team == TEAM_BLUE) { G.selected = t; arc_play_var(s_select, 0.5f, 0, 0.05f); return; }
    if (G.selected < 0) return;
    if (t >= 0 && G.u[t].team == TEAM_RED) { if (!G.u[G.selected].acted && can_hit(G.selected, t)) attack(G.selected, t); return; }
    if (t < 0 && !G.u[G.selected].moved && can_move_to(G.selected, x, y)) move_unit(G.selected, x, y);
}
static void land_impacts(void) {   /* looks that wait for the strike to land: flinch, sparks, number, sound */
    for (int i = 0; i < MAX_PENDING; i++) if (G.pending[i].on && G.clock >= G.pending[i].at) {
        Pending *p = &G.pending[i]; Unit *b = &G.u[p->target]; p->on = false;
        arc_v3 at = arc_v3_make(cx(b->x), 0.8f, cx(b->y));
        b->act = p->kill ? ACT_DIE : ACT_HIT; b->act_start = G.clock; b->flash_until = G.clock + 8;
        arc_fx_burst(&FX_HIT, at, arc_v3_make(0, 1, 0), 0);
        arc_popup_text(arc_v3_make(at.x, 1.6f, at.z), b->team == TEAM_BLUE ? 0xFF8080 : 0xFFE060, 3, arc_fmt("-%d", p->dmg));
        arc_play_var(s_hit, 0.6f, (at.x - 4) / 6, 0.1f);
        if (p->kill) { arc_play_var(s_death, 0.6f, (at.x - 4) / 6, 0.05f); arc_fx_burst(&FX_POOF, at, arc_v3_make(0, 1, 0), 0); }
        if (p->kill && G.phase == PHASE_WON) { arc_play(s_win, 0.7f, 0); arc_fx_burst(&FX_CONFETTI, arc_v3_make(4, 0.5f, 4), arc_v3_make(0, 1, 0), 0); }
    }
    for (int i = 0; i < MAX_ARROWS; i++) if (G.arrows[i].on && G.clock > G.arrows[i].start + G.arrows[i].len) G.arrows[i].on = false;
}

/* ---- the frame loop --------------------------------------------------------------------------------------------- */
static void init(void) {
    MOVE = arc_var("move", &v_move, 3); HP_SCALE = arc_var("hp_scale", &v_hp, 1);
    arc_grid_init(&walls, NULL, W, H); occ = (arc_grid){ W, H, occ_cells };
    m_floor = arc_mesh_plane(0xFFFFFF, 1, 0xFFFFFF); arc_mesh_set_name(m_floor, "board");
    arc_mesh_set_texture(m_floor, arc_texture_load_ex("assets/textures/grass.jpg", ARC_TEX_MESH));
    m_ruin = arc_mesh_load("assets/meshes/ruin.arcm"); arc_mesh_set_name(m_ruin, "wall");
    m_tree = arc_mesh_load("assets/meshes/tree.arcm"); arc_mesh_set_name(m_tree, "tree");
    m_arrow = arc_mesh_cylinder(0xE8D8B0, 6); arc_mesh_set_name(m_arrow, "arrow");
    for (int k = 0; k < K_KINDS; k++) {
        m_unit[k] = arc_mesh_load(arc_fmt("assets/meshes/%s.arcm", KINDS[k].model)); arc_mesh_set_name(m_unit[k], KINDS[k].name);
        a_unit[k] = arc_anim_load(arc_fmt("assets/meshes/%s.arca", KINDS[k].model));
        static const char *cn[5] = { "idle", "walk", "attack", "hit", "death" };
        for (int c = 0; c < 5; c++) clip[k][c] = arc_anim_find(a_unit[k], cn[c]);
    }
    s_select = arc_sound_load("assets/sfx/select.mp3"); s_move = arc_sound_load("assets/sfx/move.mp3");
    s_hit = arc_sound_load("assets/sfx/hit.mp3"); s_arrow = arc_sound_load("assets/sfx/arrow.mp3");
    s_death = arc_sound_load("assets/sfx/death.mp3"); s_turn = arc_sound_load("assets/sfx/turn.mp3");
    s_win = arc_sound_load("assets/sfx/win.mp3");
    music = arc_music_load("assets/music/music.mp3"); arc_music_set_volume(music, 0.4f); arc_music_play(music, true);
    cam = (arc_camera){ { 4, 9.0f, 10.4f }, { 4, 0, 3.9f }, { 0, 1, 0 }, 0.75f, 0, 0.1f, 100 };
    arc_set_post(&(arc_post){ .bloom = 0.35f, .bloom_threshold = 1.4f, .tonemap = true, .vignette = 0.3f, .contrast = 0.08f, .fxaa = true });
    reset();
}
static bool mouse_cell(int *x, int *y) {
    arc_v3 o, d; arc_camera_ray(&cam, (float)arc_width(), (float)arc_height(), arc_mouse(), &o, &d);
    if (d.y > -1e-4f) return false;
    float t = -o.y / d.y; *x = (int)floorf(o.x + d.x * t); *y = (int)floorf(o.z + d.z * t);
    return *x >= 0 && *y >= 0 && *x < W && *y < H;
}
static void tick(void) {
    G.clock++;
    land_impacts();
    arc_ui_begin("", 10, (float)arc_height() - 60.0f, 220);
    arc_ui_columns(2);
    if (arc_ui_button_ex("End turn", G.phase == PHASE_PLAYER)) end_player_turn();
    if (arc_ui_button("Restart")) reset();
    arc_ui_end();
    if (arc_key_pressed(ARC_KEY_R)) reset();
    for (int i = 0; i < G.nunits; i++) {   /* dust under walking feet */
        Unit *u = &G.u[i]; int t = G.clock - u->walk_start;
        if (u->npath && t >= 0 && t < STEP_TICKS * u->npath && t % STEP_TICKS == 0)
            arc_fx_burst(&FX_DUST, arc_v3_make(cx(u->path[t / STEP_TICKS][0]), 0.05f, cx(u->path[t / STEP_TICKS][1])), arc_v3_make(0, 1, 0), 0);
    }
    if (G.phase == PHASE_PLAYER) {
        if (arc_key_pressed(ARC_KEY_SPACE)) end_player_turn();
        int x, y;
        if (arc_key_pressed(ARC_MOUSE_LEFT) && !arc_ui_wants_mouse() && mouse_cell(&x, &y)) click_cell(x, y);
    } else if (G.phase == PHASE_ENEMY && --G.enemy_timer <= 0) {
        while (G.enemy_next < G.nunits && !(G.u[G.enemy_next].alive && G.u[G.enemy_next].team == TEAM_RED)) G.enemy_next++;
        if (G.enemy_next < G.nunits) {
            int i = G.enemy_next++; enemy_act(i);
            G.enemy_timer = 25 + STEP_TICKS * G.u[i].npath * (G.u[i].walk_start == G.clock) + (G.u[i].acted ? 30 : 0);
        }
        if (G.phase == PHASE_ENEMY && G.enemy_next >= G.nunits && G.enemy_timer <= 25) start_player_turn();
    }
}

/* ---- drawing ---------------------------------------------------------------------------------------------------- */
static void draw_unit(int i) {
    const Unit *u = &G.u[i]; const int k = u->kind;
    int dead_for = !u->alive && u->act == ACT_DIE ? G.clock - u->act_start : 0;
    if (!u->alive && (u->act != ACT_DIE || dead_for > 150)) return;
    /* position along the walk: segment s goes from the tile before path[s] to path[s] */
    float x = cx(u->x), z = cx(u->y), yaw = u->face;
    int t = G.clock - u->walk_start;
    bool walking = u->npath && t >= 0 && t < STEP_TICKS * u->npath;
    if (walking) {
        int sg = t / STEP_TICKS; float f = arc_ease_inout_quad((float)(t % STEP_TICKS) / (float)STEP_TICKS);
        int fx = sg ? u->path[sg - 1][0] : u->from_x, fz = sg ? u->path[sg - 1][1] : u->from_y;
        x = arc_lerpf(cx(fx), cx(u->path[sg][0]), f); z = arc_lerpf(cx(fz), cx(u->path[sg][1]), f);
        yaw = arc_yaw_to((float)(u->path[sg][0] - fx), (float)(u->path[sg][1] - fz));
    }
    /* animation layers */
    arc_anim_layer L[2]; int n = 0; float now = (float)G.clock / 60.0f;
    float act_t = (float)(G.clock - u->act_start) / 60.0f;
    if (u->act == ACT_DIE && !u->alive) L[n++] = (arc_anim_layer){ clip[k][4], act_t, 1, false };
    else {
        L[n++] = (arc_anim_layer){ walking ? clip[k][1] : clip[k][0], now, 1, true };
        int c = u->act == ACT_ATTACK ? clip[k][2] : u->act == ACT_HIT ? clip[k][3] : -1;
        if (c >= 0 && act_t >= 0 && act_t < arc_anim_length(a_unit[k], c)) L[n++] = (arc_anim_layer){ c, act_t, 3, false };
    }
    static arc_pose pose; arc_anim_pose(a_unit[k], L, n, &pose);
    arc_mesh_style st = { .whiten = G.clock < u->flash_until ? 0.8f : 0,
                          .darken = u->team == TEAM_BLUE && u->moved && u->acted && G.phase == PHASE_PLAYER ? 0.35f : 0,
                          .dissolve = dead_for > 90 ? (float)(dead_for - 90) / 60.0f : 0 };
    float s = KINDS[k].scale;
    arc_draw_mesh_posed_styled(m_unit[k], arc_m4_trs_y(arc_v3_make(x, 0, z), yaw, arc_v3_make(s, s, s)), a_unit[k], &pose, &st);
    arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_DISC, .x = x, .z = z, .size = 0.7f, .fill_rgb = u->team == TEAM_BLUE ? 0x3070E0 : 0xD04030,
                                   .fill_alpha = u->alive ? 0.55f : 0 });   /* team colour under its feet */
}
static void draw(float alpha) {
    (void)alpha;
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.6f), 0xFFF2E0, 0x485060);
    arc_set_shadows(arc_v3_make(4, 0, 4), 8, 1024);
    arc_set_sky(0x8FB8E8, 0xD8E8F0);
    /* the board: grass, a darker skirt round it, grid lines */
    arc_draw_mesh_styled(m_floor, arc_m4_trs_y(arc_v3_make(4, -0.01f, 4), 0, arc_v3_make(14, 1, 14)), &(arc_mesh_style){ .uv_scale = 7, .tint = 0x80A070 });
    arc_draw_mesh_styled(m_floor, arc_m4_trs_y(arc_v3_make(4, 0, 4), 0, arc_v3_make(8, 1, 8)), &(arc_mesh_style){ .uv_scale = 4, .depth_pull = 0.001f });
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float px = cx(x), pz = cx(y); uint8_t w = (uint8_t)arc_grid_get(&walls, x, y, 1);
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_TILE, .x = px, .z = pz, .size = 0.98f, .inner = 0.1f,
                                       .edge_rgb = 0x203018, .edge_width = 1, .edge_alpha = 0.35f });
        if (w == 1) arc_draw_mesh(m_ruin, arc_m4_trs_y(arc_v3_make(px, 0, pz), (float)((x * 7 + y * 3) % 4) * 1.5707963f, arc_v3_make(1, 0.6f, 1)), 0xFFFFFF);   /* low walls: units behind stay visible */
        if (w == 2) arc_draw_mesh(m_tree, arc_m4_trs_y(arc_v3_make(px, 0, pz), (float)(x + y), arc_v3_make(0.7f, 0.7f, 0.7f)), 0xFFFFFF);
    }
    static const float deco[][3] = { { -1.5f, 2, 0.9f }, { -1.2f, 6.5f, 1.1f }, { 9.4f, 1.5f, 1.0f }, { 9.6f, 5.5f, 1.2f }, { 2, 9.5f, 1 }, { 6.5f, 9.8f, 1.1f } };
    for (int i = 0; i < 6; i++) arc_draw_mesh(m_tree, arc_m4_trs_y(arc_v3_make(deco[i][0], 0, deco[i][1]), deco[i][0], arc_v3_make(deco[i][2], deco[i][2], deco[i][2])), 0xFFFFFF);
    /* markers: moves, targets, the selected unit, the tile under the mouse */
    if (G.phase == PHASE_PLAYER && G.selected >= 0) {
        const Unit *s = &G.u[G.selected];
        if (!s->moved) for (int y = 0; y < H; y++) for (int x = 0; x < W; x++)
            if (unit_at(x, y) < 0 && can_move_to(G.selected, x, y))
                arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_TILE, .x = cx(x), .z = cx(y), .size = 0.86f, .inner = 0.2f,
                                               .fill_rgb = 0x60D0FF, .fill_alpha = 0.35f, .pulse = 1.2f, .pulse_amount = 0.4f });
        if (!s->acted) for (int j = 0; j < G.nunits; j++) if (G.u[j].alive && G.u[j].team == TEAM_RED && can_hit(G.selected, j))
            arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = cx(G.u[j].x), .z = cx(G.u[j].y), .size = 0.95f,
                                           .fill_rgb = 0xFF4030, .fill_alpha = 0.8f, .pulse = 2, .pulse_amount = 0.5f, .on_top = true });
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = cx(s->x), .z = cx(s->y), .size = 0.95f, .fill_rgb = 0xFFE040, .fill_alpha = 0.9f, .on_top = true });
    }
    int mx, my;
    if (G.phase == PHASE_PLAYER && mouse_cell(&mx, &my))
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_TILE, .x = cx(mx), .z = cx(my), .size = 0.96f, .inner = 0.15f,
                                       .edge_rgb = 0xFFFFFF, .edge_width = 2, .edge_alpha = 0.8f });
    for (int i = 0; i < G.nunits; i++) draw_unit(i);
    for (int i = 0; i < MAX_ARROWS; i++) if (G.arrows[i].on && G.clock >= G.arrows[i].start) {   /* arrows in flight, on an arc */
        const Arrow *a = &G.arrows[i]; float f = arc_clampf((float)(G.clock - a->start) / (float)a->len, 0, 1);
        float x = arc_lerpf(a->x0, a->x1, f), z = arc_lerpf(a->z0, a->z1, f), y = 0.9f + 1.2f * f * (1 - f) * (float)a->len / 10.0f;
        arc_m4 m = arc_m4_mul(arc_m4_trs_y(arc_v3_make(x, y, z), arc_yaw_to(a->x1 - a->x0, a->z1 - a->z0), arc_v3_make(1, 1, 1)),
                              arc_m4_mul(arc_m4_rotate_x(1.5707963f), arc_m4_scale(arc_v3_make(0.04f, 0.5f, 0.04f))));
        arc_draw_mesh(m_arrow, m, 0xFFFFFF);
    }
    arc_draw_particles();
    /* health pips over each unit */
    for (int i = 0; i < G.nunits; i++) if (G.u[i].alive) {
        const Unit *u = &G.u[i]; arc_v2 s;
        if (!arc_world_to_screen(arc_v3_make(cx(u->x), 1.55f, cx(u->y)), &s)) continue;
        int maxhp = (int)((float)KINDS[u->kind].hp * *HP_SCALE + 0.5f); float w = 40, f = (float)u->hp / (float)(maxhp > 0 ? maxhp : 1);
        arc_draw_rect(s.x - w / 2 - 1, s.y - 1, w + 2, 7, 0x101010);
        arc_draw_rect(s.x - w / 2, s.y, w * f, 5, u->team == TEAM_BLUE ? 0x50A0FF : 0xFF5040);
        arc_draw_text_ex(s.x, s.y - 4, 1, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_BOTTOM | ARC_TEXT_SHADOW, arc_fmt("%d", u->hp));
    }
    /* HUD and the turn banner */
    float Wd = (float)arc_width(), Hd = (float)arc_height();
    const char *msg = G.phase == PHASE_PLAYER ? "YOUR TURN" : G.phase == PHASE_ENEMY ? "ENEMY TURN" : G.phase == PHASE_WON ? "VICTORY - R to play again" : "DEFEAT - R to try again";
    uint32_t col = G.phase == PHASE_PLAYER ? 0x80C0FF : G.phase == PHASE_ENEMY ? 0xFF9080 : G.phase == PHASE_WON ? 0xFFE060 : 0xFF6060;
    arc_draw_text_ex(10, 10, 3, col, ARC_TEXT_OUTLINE, msg);
    arc_draw_textf(10, 40, 2, 0xE0E4EC, "Turn %d   blue %d   red %d", G.turn, count(TEAM_BLUE), count(TEAM_RED));
    arc_draw_text(10, 62, 2, 0xC0C8D8, "click a unit, a tile, an enemy - SPACE ends the turn");
    int bt = G.clock - G.banner_start;
    if (bt < 70 || G.phase >= PHASE_WON) {
        float t = arc_clampf((float)bt / 18.0f, 0, 1), out = G.phase >= PHASE_WON ? 1 : arc_clampf((float)(70 - bt) / 12.0f, 0, 1);
        float sc = 5.0f * arc_ease_out_back(t) * out;
        if (sc > 0.2f) arc_draw_text_ex(Wd / 2, Hd / 3, sc, col, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE | ARC_TEXT_OUTLINE,
                                        G.phase == PHASE_WON ? "VICTORY!" : G.phase == PHASE_LOST ? "DEFEAT" : msg);
    }
}
static void dump(arc_json *j) {
    static const char *ph[] = { "player", "enemy", "won", "lost" };
    arc_json_str_k(j, "phase", ph[G.phase]); arc_json_num_k(j, "turn", G.turn); arc_json_num_k(j, "selected", G.selected);
    arc_json_num_k(j, "blue", count(TEAM_BLUE)); arc_json_num_k(j, "red", count(TEAM_RED));
    arc_json_arr_k(j, "units");
    for (int i = 0; i < G.nunits; i++) {
        Unit *u = &G.u[i]; arc_json_obj(j);
        arc_json_num_k(j, "i", i); arc_json_str_k(j, "kind", KINDS[u->kind].name); arc_json_str_k(j, "team", u->team == TEAM_BLUE ? "blue" : "red");
        arc_json_num_k(j, "x", u->x); arc_json_num_k(j, "y", u->y); arc_json_num_k(j, "hp", u->hp);
        arc_json_bool_k(j, "alive", u->alive); arc_json_bool_k(j, "moved", u->moved); arc_json_bool_k(j, "acted", u->acted);
        arc_json_end(j);
    }
    arc_json_end(j);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "select") && argc >= 1) { int i = atoi(argv[0]); if (i >= 0 && i < G.nunits && G.u[i].alive && G.u[i].team == TEAM_BLUE) G.selected = i; return true; }
    if (!strcmp(name, "move") && argc >= 2) { if (G.phase == PHASE_PLAYER) click_cell(atoi(argv[0]), atoi(argv[1])); return true; }
    if (!strcmp(name, "attack") && argc >= 1) { int t = atoi(argv[0]); if (G.phase == PHASE_PLAYER && t >= 0 && t < G.nunits && G.u[t].alive) click_cell(G.u[t].x, G.u[t].y); return true; }
    if (!strcmp(name, "end")) { end_player_turn(); return true; }
    if (!strcmp(name, "win")) { for (int i = 0; i < G.nunits; i++) if (G.u[i].team == TEAM_RED) G.u[i].alive = false; check_end(); return true; }
    if (!strcmp(name, "restart")) { reset(); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t size) { if (size != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "{title}", .width = 960, .height = 640, .clear_color = 0x8FB8E8,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
