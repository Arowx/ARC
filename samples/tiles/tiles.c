/* ARC sample: a small tile game with the in-game level editor (ARC 0.7.2) and the replay timeline.
 *   ./build.sh headless samples/tiles/tiles.c   ->  build/tiles_headless
 *   ./build.sh linux samples/tiles/tiles.c      ->  build/tiles     (F2 editor, F9 timeline: dev keys are on here)
 * The level is a ship deck as text (a copy of Droids' "Cargo Hold"): # wall, . floor, D door, L lift, E energiser,
 * C console, X crate, K locker, @ the player's start, 1-3 droids. Walk with the arrows or WASD and collect energisers;
 * droids wander. The game owns the grid and its file: the editor edits `G.deck` in place, calls on_cell for every change,
 * and save_level / load_level write and read tiles_level.txt (or --level FILE) in the game's own text format.
 * Agents: --cmd "editor paint 3 3 #; editor fill 2 2 5 5 X; editor undo; editor dump; editor save; dump" */
#include "../../include/arc.h"
#include <stdio.h>

#define W 32
#define H 16
#define MAX_DROIDS 32
static const char *k_cargo_hold[H] = {   /* Droids, deck 1 (tools/ship_decks.py), a scratch copy */
    "################################",
    "#XX....#........#.....XX.......#",
    "#X.....#...1....#..............#",
    "#......D........D......X...2...#",
    "#..@...#........#......XX......#",
    "#......#####D####..............#",
    "#..L...#........#######D########",
    "#......#..XX....#..............#",
    "###D####..XX....D......1....C..#",
    "#......#........#..............#",
    "#..EE..#...2....#####D##########",
    "#......D........#..............#",
    "#..C...#........D.......3......#",
    "#......#........#..............#",
    "#K.....#........#..............#",
    "################################",
};

typedef struct { int x, y, kind; } droid;
static struct {
    char deck[H][W + 1];          /* the level: rows of text, edited in place by the editor */
    int px, py, score, move_cd;
    droid droids[MAX_DROIDS]; int ndroids;
    int rebuilds;                 /* how many times the droids were rebuilt from the grid (for tests) */
} G;
static const char *g_level_path = "tiles_level.txt";
static arc_mesh g_floor;

static bool solid(char c) { return c == '#' || c == 'X' || c == 'C' || c == 'K'; }
static void rebuild(void) {       /* everything the game builds from the grid: the droids */
    G.ndroids = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        char c = G.deck[y][x];
        if (c >= '1' && c <= '9' && G.ndroids < MAX_DROIDS) { droid *d = &G.droids[G.ndroids++]; d->x = x; d->y = y; d->kind = c - '0'; }
    }
    G.rebuilds++;
}
static void place_player(void) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (G.deck[y][x] == '@') { G.px = x; G.py = y; return; }
    G.px = 1; G.py = 1;
}
/* the editor's hooks: the game decides what a change means, and owns its file */
static void on_cell(int x, int y, int old_id, int new_id, void *user) {
    (void)user; (void)x; (void)y;
    if ((old_id >= '1' && old_id <= '9') || (new_id >= '1' && new_id <= '9')) rebuild();
    if (new_id == '@') { G.px = x; G.py = y; }
}
static bool save_level(void *user) {
    (void)user; char buf[H * (W + 1) + 1]; int n = 0;
    for (int y = 0; y < H; y++) { memcpy(buf + n, G.deck[y], W); n += W; buf[n++] = '\n'; }
    return arc_file_write(g_level_path, buf, (size_t)n);
}
static bool load_level(void *user) {
    (void)user; size_t n = 0; char *t = (char *)arc_file_read(g_level_path, arc_frame(), &n);
    if (!t) return false;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) G.deck[y][x] = (*t && *t != '\n' && *t != '\r') ? *t++ : '.';
        while (*t && *t != '\n') t++;
        if (*t) t++;
    }
    rebuild(); place_player();
    return true;
}
static bool validate(char *msg, size_t n, void *user) {
    (void)user; int starts = 0, lifts = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { starts += G.deck[y][x] == '@'; lifts += G.deck[y][x] == 'L'; }
    if (starts != 1) { snprintf(msg, n, "deck needs one @ start (has %d)", starts); return false; }
    if (!lifts) { snprintf(msg, n, "deck has no lift"); return false; }
    return true;
}
static void game_camera(arc_camera *c, void *user) {
    (void)user; memset(c, 0, sizeof *c);
    c->eye = arc_v3_make(W * 0.5f, 22, H + 9.0f); c->target = arc_v3_make(W * 0.5f, 0, H * 0.5f + 0.5f);
    c->fov_y = 0.8f; c->znear = 0.1f; c->zfar = 200;
}
static const arc_editor_tile k_tiles[] = {
    { '.', "Floor",     0x2A3038, ARC_KEY_Q, { 0 }, 0, 0, 0, 0, 0 },
    { '#', "Wall",      0x8A94A4, ARC_KEY_W, { 0 }, 0, 0, 0, 0, 0 },
    { 'D', "Door",      0x4080D0, ARC_KEY_D, { 0 }, 0, 0, 0, 0, 1 },
    { 'L', "Lift",      0x40C060, ARC_KEY_L, { 0 }, 0, 0, 0, 0, 1 },
    { 'E', "Energiser", 0xF0D040, ARC_KEY_E, { 0 }, 0, 0, 0, 0, 1 },
    { 'C', "Console",   0x30B0B0, ARC_KEY_C, { 0 }, 0, 0, 0, 0, 1 },
    { 'X', "Crate",     0xA06A30, ARC_KEY_X, { 0 }, 0, 0, 0, 0, 1 },
    { 'K', "Locker",    0x7050A0, ARC_KEY_K, { 0 }, 0, 0, 0, 0, 1 },
    { '@', "Start",     0xFFFFFF, ARC_KEY_T, { 0 }, 0, 0, 0, 0, 1 },
    { '1', "Droid 1",   0xE05050, ARC_KEY_1, { 0 }, 0, 0, 0, 0, 1 },
    { '2', "Droid 2",   0xE08040, ARC_KEY_2, { 0 }, 0, 0, 0, 0, 1 },
    { '3', "Droid 3",   0xD040C0, ARC_KEY_3, { 0 }, 0, 0, 0, 0, 1 },
};

static void init(void) {
    const char *lv = arc_arg_value("level"); if (lv) g_level_path = lv;
    for (int y = 0; y < H; y++) memcpy(G.deck[y], k_cargo_hold[y], W + 1);
    if (!arc_arg_flag("fresh")) load_level(NULL);    /* the saved level, if there is one */
    rebuild(); place_player();
    g_floor = arc_mesh_plane(0x30363E, 1, 0x30363E);
    arc_editor_begin(&(arc_editor_desc){
        .name = "Cargo Hold", .width = W, .height = H, .cells = G.deck, .cell_type = ARC_EDITOR_CHAR, .stride = W + 1,
        .tiles = k_tiles, .tile_count = (int)(sizeof k_tiles / sizeof k_tiles[0]), .erase_tile = 0,
        .changed = on_cell, .save = save_level, .load = load_level, .validate = validate,
        .origin = { 0, 0, 0 }, .world_cell = 1.0f, .camera = game_camera });
    if (arc_arg_flag("editor")) arc_editor_open(true);
}
static void tick(void) {
    int dx = 0, dy = 0;
    if (arc_key_down(ARC_KEY_LEFT) || arc_key_down(ARC_KEY_A)) dx = -1;
    else if (arc_key_down(ARC_KEY_RIGHT) || arc_key_down(ARC_KEY_D)) dx = 1;
    else if (arc_key_down(ARC_KEY_UP) || arc_key_down(ARC_KEY_W)) dy = -1;
    else if (arc_key_down(ARC_KEY_DOWN) || arc_key_down(ARC_KEY_S)) dy = 1;
    if (G.move_cd > 0) G.move_cd--;
    if ((dx || dy) && G.move_cd == 0) {
        int nx = G.px + dx, ny = G.py + dy;
        if (nx >= 0 && ny >= 0 && nx < W && ny < H && !solid(G.deck[ny][nx])) { G.px = nx; G.py = ny; G.move_cd = 8; }
        if (G.deck[G.py][G.px] == 'E') { G.deck[G.py][G.px] = '.'; G.score++; arc_event("pickup", "\"score\":%d", G.score); }
    }
    if (arc_tick_count() % 30 == 0)              /* droids wander (seeded random: replays repeat it) */
        for (int i = 0; i < G.ndroids; i++) {
            droid *d = &G.droids[i]; int dir = arc_rand_range(0, 3);
            int nx = d->x + (dir == 0) - (dir == 1), ny = d->y + (dir == 2) - (dir == 3);
            if (nx >= 0 && ny >= 0 && nx < W && ny < H && !solid(G.deck[ny][nx]) && G.deck[ny][nx] != 'D') { d->x = nx; d->y = ny; }
        }
}
static uint32_t col_of(char c) { for (size_t i = 0; i < sizeof k_tiles / sizeof k_tiles[0]; i++) if (k_tiles[i].id == c) return k_tiles[i].rgb; return 0xFF00FF; }
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam; game_camera(&cam, NULL); arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.3f), 0xFFF4E0, 0x404858);
    arc_draw_mesh(g_floor, arc_m4_trs_y(arc_v3_make(W * 0.5f, 0, H * 0.5f), 0, arc_v3_make(W, 1, H)), 0xFFFFFF);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        char c = G.deck[y][x]; float h = c == '#' ? 1.2f : solid(c) ? 0.6f : (c == '.' || c == '@' || (c >= '1' && c <= '9')) ? 0 : 0.15f;
        if (h > 0) arc_draw_box(arc_v3_make((float)x + 0.04f, 0, (float)y + 0.04f), arc_v3_make((float)x + 0.96f, h, (float)y + 0.96f), col_of(c));
    }
    for (int i = 0; i < G.ndroids; i++) arc_draw_sphere(arc_v3_make((float)G.droids[i].x + 0.5f, 0.45f, (float)G.droids[i].y + 0.5f), 0.4f, col_of((char)('0' + G.droids[i].kind)));
    arc_draw_cube(arc_v3_make((float)G.px + 0.5f, 0.4f, (float)G.py + 0.5f), arc_v3_make(0.6f, 0.8f, 0.6f), 0xFFFFFF);
    arc_draw_textf(10, 10, 2, 0xFFFFFF, "Energy %d   F2 editor  F9 timeline  F8 bookmark", G.score);
}
static void dump(arc_json *j) {
    arc_json_int_k(j, "px", G.px); arc_json_int_k(j, "py", G.py); arc_json_int_k(j, "score", G.score);
    arc_json_int_k(j, "droids", G.ndroids); arc_json_int_k(j, "rebuilds", G.rebuilds);
    arc_json_arr_k(j, "droid_xy"); for (int i = 0; i < G.ndroids; i++) { arc_json_int(j, G.droids[i].x); arc_json_int(j, G.droids[i].y); } arc_json_end(j);
    uint32_t h = 2166136261u; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { h ^= (uint8_t)G.deck[y][x]; h *= 16777619u; }
    arc_json_int_k(j, "deck_hash", (long long)h);
    arc_json_str_k(j, "row4", G.deck[4]);
}
/* snapshots for seek / rewind / the timeline: the whole game is plain data (the deck included, so edits rewind too) */
static size_t save_state(void *buf, size_t cap) { arc_writer w = { (uint8_t *)buf, cap, 0 }; arc_write(&w, &G, sizeof G); return w.len; }
static bool load_state(const void *buf, size_t n) { arc_reader r = { (const uint8_t *)buf, n, 0, false }; arc_read(&r, &G, sizeof G); return !r.fail; }

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC Tiles", .width = 1280, .height = 720, .init = init, .tick = tick, .draw = draw, .dump = dump,
                      .dev_keys = true, .sessions = true, .save_state = save_state, .load_state = load_state, .clear_color = 0x101418 };
}
