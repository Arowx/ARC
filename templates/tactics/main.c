/* {title} - a turn-based tactics starter (ARC template "tactics").
 *
 * An 8x8 board with walls, three blue units against three red ones. Your turn: click a blue unit, click a highlighted
 * tile to move it (up to MOVE tiles around walls and units), click a red unit next to it to attack. Each unit moves
 * once and attacks once per turn. SPACE or "End turn" lets the red side act: each red unit walks toward the nearest
 * blue unit and attacks when next to it. Win by removing every red unit. R restarts.
 *
 * Where things are:
 *   Game G           every bit of game state (no pointers), so save_state/load_state copy it whole: rewind, seek
 *                    and the replay timeline work out of the box.
 *   tick()           all rules: input, moves, attacks, the red side's turn, win/lose. draw() only draws.
 *   dump()           everything a test needs: phase, turn, each unit's team/x/y/hp, counts.
 *   command()        debug commands for agents: select N, move X Y, attack N, end, win, restart.
 *   arc_var          tunables (MOVE, ATTACK, HP): `set move 4` changes them without a rebuild.
 * Grow it: add unit kinds (a kind field + a table of stats), terrain costs (grid ids), ranged attacks (arc_grid_los),
 * a fog of war, several levels (more strings in LEVELS).
 */
#include "arc.h"
#include <string.h>
#include <stdlib.h>

#define W 8
#define H 8
#define MAX_UNITS 12
enum { PHASE_PLAYER, PHASE_ENEMY, PHASE_WON, PHASE_LOST };
enum { TEAM_BLUE, TEAM_RED };

typedef struct { int team, x, y, hp; bool alive, moved, acted; } Unit;
typedef struct {
    int phase, turn, selected;          /* selected: unit index or -1 */
    int nunits; Unit u[MAX_UNITS];
    int enemy_timer;                    /* ticks until the next red unit acts (so you can watch them) */
    int enemy_next;
    int last_hit_tick;
} Game;
static Game G;

/* the board: '#' wall, 'b' blue unit, 'r' red unit, '.' floor */
static const char *LEVEL[H] = {
    "b......r",
    ".b.##..r",
    "..#....r",
    "b...#...",
    "........",
    "...#..#.",
    ".##.....",
    "........",
};

static float *MOVE, *ATTACK, *HP, v_move, v_attack, v_hp;   /* tunables: *MOVE etc. */
static arc_grid walls;
static arc_mesh m_tile, m_wall, m_blue, m_red, m_mark;
static arc_sound s_move, s_hit;
static arc_camera cam;

/* ---- rules ---------------------------------------------------------------------------------------------------- */
static int unit_at(int x, int y) { for (int i = 0; i < G.nunits; i++) if (G.u[i].alive && G.u[i].x == x && G.u[i].y == y) return i; return -1; }
static uint8_t occ_cells[W * H];
static bool passable(uint8_t t) { return t == 0; }
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
static bool adjacent(int a, int b) { int dx = G.u[a].x - G.u[b].x, dy = G.u[a].y - G.u[b].y; return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) == 1; }

static void check_end(void) {
    if (count(TEAM_RED) == 0) { G.phase = PHASE_WON; arc_event("won", "\"turn\":%d", G.turn); }
    else if (count(TEAM_BLUE) == 0) { G.phase = PHASE_LOST; arc_event("lost", "\"turn\":%d", G.turn); }
}
static void attack(int a, int b) {
    G.u[b].hp -= (int)*ATTACK; G.u[a].acted = true; G.last_hit_tick = (int)arc_tick_count();
    if (G.u[b].hp <= 0) { G.u[b].hp = 0; G.u[b].alive = false; }
    arc_play(s_hit, 0.6f, 0); check_end();
}
static void move_unit(int i, int x, int y) { G.u[i].x = x; G.u[i].y = y; G.u[i].moved = true; arc_play(s_move, 0.4f, 0); }

static void reset(void) {
    memset(&G, 0, sizeof G); G.selected = -1; G.turn = 1;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        char c = LEVEL[y][x];
        arc_grid_set(&walls, x, y, c == '#');
        if ((c == 'b' || c == 'r') && G.nunits < MAX_UNITS)
            G.u[G.nunits++] = (Unit){ c == 'b' ? TEAM_BLUE : TEAM_RED, x, y, (int)*HP, true, false, false };
    }
}
static void end_player_turn(void) {
    if (G.phase != PHASE_PLAYER) return;
    G.phase = PHASE_ENEMY; G.selected = -1; G.enemy_next = 0; G.enemy_timer = 20;
}
static void enemy_act(int i) {   /* walk toward the nearest blue unit, attack if next to one */
    Unit *e = &G.u[i];
    for (int step = 0; step < (int)*MOVE; step++) {
        int best = -1, bestd = 1 << 30;
        for (int j = 0; j < G.nunits; j++) if (G.u[j].alive && G.u[j].team == TEAM_BLUE) {
            if (adjacent(i, j)) { attack(i, j); return; }
            build_occ(i); occ_cells[G.u[j].y * W + G.u[j].x] = 0;   /* path to the target's own cell */
            int d = arc_grid_path_length(&occ, e->x, e->y, G.u[j].x, G.u[j].y, passable);
            if (d > 0 && d < bestd) { bestd = d; best = j; }
        }
        if (best < 0) return;
        build_occ(i); occ_cells[G.u[best].y * W + G.u[best].x] = 0;
        int nx, ny;
        if (!arc_grid_path_step(&occ, e->x, e->y, G.u[best].x, G.u[best].y, passable, &nx, &ny) || unit_at(nx, ny) >= 0) return;
        e->x = nx; e->y = ny;
    }
    for (int j = 0; j < G.nunits; j++) if (G.u[j].alive && G.u[j].team == TEAM_BLUE && adjacent(i, j)) { attack(i, j); return; }
}
static void start_player_turn(void) {
    G.phase = PHASE_PLAYER; G.turn++;
    for (int i = 0; i < G.nunits; i++) G.u[i].moved = G.u[i].acted = false;
}
/* a click on board cell (x, y) in the player's turn */
static void click_cell(int x, int y) {
    int t = unit_at(x, y);
    if (t >= 0 && G.u[t].team == TEAM_BLUE) { G.selected = t; return; }
    if (G.selected < 0) return;
    if (t >= 0 && G.u[t].team == TEAM_RED) { if (!G.u[G.selected].acted && adjacent(G.selected, t)) attack(G.selected, t); return; }
    if (t < 0 && !G.u[G.selected].moved && can_move_to(G.selected, x, y)) move_unit(G.selected, x, y);
}

/* ---- the frame loop --------------------------------------------------------------------------------------------- */
static void init(void) {
    MOVE = arc_var("move", &v_move, 3); ATTACK = arc_var("attack", &v_attack, 4); HP = arc_var("hp", &v_hp, 10);
    arc_grid_init(&walls, NULL, W, H); occ = (arc_grid){ W, H, occ_cells };
    m_tile = arc_mesh_plane(0x4A5468, 1, 0x4A5468); arc_mesh_set_name(m_tile, "tile");
    m_wall = arc_mesh_cube(0x8A8F9C); arc_mesh_set_name(m_wall, "wall");
    m_blue = arc_mesh_cylinder(0x3C82F0, 20); arc_mesh_set_name(m_blue, "blue_unit");
    m_red = arc_mesh_cone(0xE04838, 20); arc_mesh_set_name(m_red, "red_unit");
    m_mark = arc_mesh_plane(0xFFFFFF, 1, 0xFFFFFF); arc_mesh_set_name(m_mark, "reachable");
    s_move = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_TRIANGLE, .freq = 440, .freq_end = 660, .sustain = 0.05f, .decay = 0.08f });
    s_hit = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_NOISE, .freq = 900, .freq_end = 200, .sustain = 0.05f, .decay = 0.2f });
    cam = (arc_camera){ { 4, 11, 12.5f }, { 4, 0, 3.6f }, { 0, 1, 0 }, 0.75f, 0, 0.1f, 100 };
    reset();
}
static void tick(void) {
    arc_ui_begin("", 10, (float)arc_height() - 60.0f, 220);
    arc_ui_columns(2);
    if (arc_ui_button_ex("End turn", G.phase == PHASE_PLAYER)) end_player_turn();
    if (arc_ui_button("Restart")) reset();
    arc_ui_end();
    if (arc_key_pressed(ARC_KEY_R)) reset();
    if (G.phase == PHASE_PLAYER) {
        if (arc_key_pressed(ARC_KEY_SPACE)) end_player_turn();
        if (arc_key_pressed(ARC_MOUSE_LEFT) && !arc_ui_wants_mouse()) {
            arc_v3 o, d; arc_camera_ray(&cam, (float)arc_width(), (float)arc_height(), arc_mouse(), &o, &d);
            if (d.y < -1e-4f) {
                float t = -o.y / d.y; int x = (int)floorf(o.x + d.x * t), y = (int)floorf(o.z + d.z * t);
                if (x >= 0 && y >= 0 && x < W && y < H) click_cell(x, y);
            }
        }
    } else if (G.phase == PHASE_ENEMY && --G.enemy_timer <= 0) {
        while (G.enemy_next < G.nunits && !(G.u[G.enemy_next].alive && G.u[G.enemy_next].team == TEAM_RED)) G.enemy_next++;
        if (G.enemy_next < G.nunits) { enemy_act(G.enemy_next++); G.enemy_timer = 20; }
        if (G.phase == PHASE_ENEMY && G.enemy_next >= G.nunits) start_player_turn();
    }
}
static void draw(float alpha) {
    (void)alpha;
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.6f), 0xFFF2E0, 0x404858);
    arc_set_shadows(arc_v3_make(4, 0, 4), 7, 1024);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        arc_v3 c = arc_v3_make((float)x + 0.5f, 0, (float)y + 0.5f);
        if (arc_grid_get(&walls, x, y, 1)) arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(c.x, 0.5f, c.z), 0, arc_v3_make(0.96f, 1, 0.96f)), 0xFFFFFF);
        else arc_draw_mesh(m_tile, arc_m4_trs_y(c, 0, arc_v3_make(0.94f, 1, 0.94f)), ((x + y) & 1) ? 0xFFFFFF : 0xD8DCE6);
        if (G.phase == PHASE_PLAYER && G.selected >= 0 && !G.u[G.selected].moved && unit_at(x, y) < 0 && can_move_to(G.selected, x, y))
            arc_draw_mesh(m_mark, arc_m4_trs_y(arc_v3_make(c.x, 0.01f, c.z), 0, arc_v3_make(0.8f, 1, 0.8f)), 0x60D0FF);
    }
    for (int i = 0; i < G.nunits; i++) if (G.u[i].alive) {
        Unit *u = &G.u[i]; arc_v3 p = arc_v3_make((float)u->x + 0.5f, 0, (float)u->y + 0.5f);
        bool hit = (int)arc_tick_count() - G.last_hit_tick < 10;
        uint32_t tint = (u->team == TEAM_BLUE && (u->moved && u->acted)) ? 0x808080 : 0xFFFFFF;
        if (i == G.selected) arc_draw_mesh(m_mark, arc_m4_trs_y(arc_v3_make(p.x, 0.02f, p.z), 0, arc_v3_make(0.95f, 1, 0.95f)), 0xFFE040);
        arc_draw_mesh(u->team == TEAM_BLUE ? m_blue : m_red, arc_m4_trs_y(arc_v3_make(p.x, 0.45f, p.z), 0, arc_v3_make(0.6f, 0.9f, 0.6f)), hit && u->team == TEAM_RED ? 0xFFFFFF : tint);
        arc_v2 s; if (arc_world_to_screen(arc_v3_make(p.x, 1.2f, p.z), &s))
            arc_draw_text_ex(s.x, s.y, 2, u->team == TEAM_BLUE ? 0xA0C8FF : 0xFFB0A0, ARC_TEXT_CENTER | ARC_TEXT_SHADOW, arc_fmt("%d", u->hp));
    }
    const char *msg = G.phase == PHASE_PLAYER ? "YOUR TURN" : G.phase == PHASE_ENEMY ? "ENEMY TURN" : G.phase == PHASE_WON ? "VICTORY - R to play again" : "DEFEAT - R to try again";
    arc_draw_textf(10, 10, 3, 0xFFFFFF, "%s", msg);
    arc_draw_textf(10, 40, 2, 0xB0B8C8, "Turn %d   blue %d   red %d", G.turn, count(TEAM_BLUE), count(TEAM_RED));
    arc_draw_text(10, 62, 2, 0x8890A0, "click a unit, a tile, an enemy - SPACE ends the turn");
}
static void dump(arc_json *j) {
    static const char *ph[] = { "player", "enemy", "won", "lost" };
    arc_json_str_k(j, "phase", ph[G.phase]); arc_json_num_k(j, "turn", G.turn); arc_json_num_k(j, "selected", G.selected);
    arc_json_num_k(j, "blue", count(TEAM_BLUE)); arc_json_num_k(j, "red", count(TEAM_RED));
    arc_json_arr_k(j, "units");
    for (int i = 0; i < G.nunits; i++) {
        Unit *u = &G.u[i]; arc_json_obj(j);
        arc_json_num_k(j, "i", i); arc_json_str_k(j, "team", u->team == TEAM_BLUE ? "blue" : "red");
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
    return (arc_app){ .title = "{title}", .width = 960, .height = 640, .clear_color = 0x1A1E28,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
