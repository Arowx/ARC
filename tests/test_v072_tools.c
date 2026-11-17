/* ARC 0.7.2 Phase E1 self-test (developer tools: the tile level editor and the replay timeline): included by test_core.c.
   v072t_init() runs in init. Part 1 edits a grid through the API and the agent commands. Part 2 runs a pretend game inside
   the engine (arc__test_sandbox) to drive the editor with recorded input, then seeks back and forth with the timeline and
   checks that every state matches the one reached by playing straight there. Every name here starts with v072t_ (all test
   files share one translation unit). */
void arc__test_sandbox(const arc_app *app);
void arc__test_ticks(int n);
void arc__test_command(const char *line);
void arc__test_timeline_frame(void);
void arc__input_mouse(float x, float y);

/* ---- part 1: a char grid edited through the API ---- */
#define V072T_W 8
#define V072T_H 5
static char v072t_grid[V072T_H][V072T_W + 1];
static int v072t_changes, v072t_saves, v072t_loads, v072t_last_old, v072t_last_new;
static char v072t_file[V072T_H][V072T_W + 1];   /* what save writes, load reads */
static void v072t_changed(int x, int y, int o, int n, void *u) { (void)x; (void)y; (void)u; v072t_changes++; v072t_last_old = o; v072t_last_new = n; }
static bool v072t_save(void *u) { (void)u; v072t_saves++; memcpy(v072t_file, v072t_grid, sizeof v072t_file); return true; }
static bool v072t_load(void *u) { (void)u; v072t_loads++; memcpy(v072t_grid, v072t_file, sizeof v072t_grid); return true; }
static bool v072t_validate(char *msg, size_t n, void *u) {
    (void)u; for (int y = 0; y < V072T_H; y++) if (strchr(v072t_grid[y], '@')) return true;
    snprintf(msg, n, "no start"); return false;
}
static const arc_editor_tile v072t_tiles[] = {
    { '.', "Floor", 0x202020, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 }, { '#', "Wall", 0x808080, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 },
    { 'X', "Crate", 0xA06030, ARC_KEY_X, { 0 }, 0, 0, 0, 0, 1 }, { '@', "Start", 0xFFFFFF, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 1 },
};
static void v072t_reset_grid(void) {
    for (int y = 0; y < V072T_H; y++) { memset(v072t_grid[y], '.', V072T_W); v072t_grid[y][V072T_W] = 0; }
    for (int y = 0; y < V072T_H; y++) v072t_grid[y][4] = '#';   /* a wall down column 4 */
}
static int v072t_count(char c) { int n = 0; for (int y = 0; y < V072T_H; y++) for (int x = 0; x < V072T_W; x++) n += v072t_grid[y][x] == c; return n; }
static void v072t_begin(int undo_cells) {
    arc_editor_desc d; memset(&d, 0, sizeof d);
    d.name = "t"; d.width = V072T_W; d.height = V072T_H; d.cells = v072t_grid; d.cell_type = ARC_EDITOR_CHAR; d.stride = V072T_W + 1;
    d.tiles = v072t_tiles; d.tile_count = 4; d.changed = v072t_changed; d.save = v072t_save; d.load = v072t_load; d.validate = v072t_validate;
    d.undo_cells = undo_cells;
    arc_editor_begin(&d);
}
static void v072t_api(void) {
    v072t_reset_grid(); v072t_begin(0);
    CHECK(!strcmp(arc_editor_message(), "no start"));                /* validate ran at begin */
    CHECK(!arc_editor_is_open() && arc_editor_get(4, 0) == '#' && arc_editor_get(8, 0) == -1 && arc_editor_get(-1, 0) == -1);
    CHECK(arc_editor_paint(1, 1, '@') && v072t_grid[1][1] == '@' && v072t_changes == 1 && v072t_last_old == '.' && v072t_last_new == '@');
    CHECK(!strcmp(arc_editor_message(), ""));                         /* the problem is gone */
    CHECK(!arc_editor_paint(1, 1, '@') && v072t_changes == 1);         /* no change, no step */
    CHECK(!arc_editor_paint(9, 1, '#') && arc_editor_undo_steps() == 1);
    CHECK(arc_editor_fill(3, 0, 0, 1, 'X') == 8 && v072t_count('X') == 8);   /* corners in any order; @ was in it */
    CHECK(v072t_grid[1][1] == 'X' && arc_editor_undo_steps() == 2 && arc_editor_dirty());
    CHECK(arc_editor_undo() && v072t_count('X') == 0 && v072t_grid[1][1] == '@');   /* the fill is one step */
    CHECK(arc_editor_redo() && v072t_count('X') == 8 && !arc_editor_redo());
    CHECK(arc_editor_undo() && arc_editor_undo() && v072t_grid[1][1] == '.' && !arc_editor_undo());
    CHECK(arc_editor_flood(6, 2, 'X') == 15 && v072t_count('X') == 15 && v072t_grid[2][3] == '.');   /* stops at the wall */
    CHECK(arc_editor_flood(6, 2, 'X') == 0);
    CHECK(arc_editor_line(0, 0, 3, 3, '#') == 4 && v072t_grid[0][0] == '#' && v072t_grid[1][1] == '#' && v072t_grid[3][3] == '#');
    CHECK(arc_editor_undo() && arc_editor_undo() && v072t_count('X') == 0 && v072t_count('#') == 5);
    /* agent commands */
    arc__test_command("editor paint 2 3 @"); CHECK(v072t_grid[3][2] == '@');
    arc__test_command("editor fill 5 0 6 1 Crate"); CHECK(v072t_count('X') == 4);   /* a tile by name */
    arc__test_command("editor flood 0 0 =35"); CHECK(v072t_grid[0][0] == '#' && v072t_count('#') == 5 + 19);   /* 35 = '#'; the @ stays */
    arc__test_command("editor undo"); CHECK(v072t_count('#') == 5);
    arc__test_command("editor redo"); CHECK(v072t_count('#') == 24);
    arc__test_command("editor undo"); arc__test_command("editor dump");
    arc__test_command("editor on"); CHECK(arc_editor_is_open());
    arc__test_command("editor off"); CHECK(!arc_editor_is_open());
    /* save / load hooks: the game's own format */
    int ch = v072t_changes;
    arc__test_command("editor save"); CHECK(v072t_saves == 1 && !arc_editor_dirty() && v072t_file[3][2] == '@');
    CHECK(arc_editor_paint(0, 4, 'X') && arc_editor_paint(1, 4, 'X') && arc_editor_dirty());
    ch = v072t_changes;
    CHECK(arc_editor_load() && v072t_loads == 1 && v072t_grid[4][0] == '.' && v072t_grid[4][1] == '.');
    CHECK(v072t_changes == ch + 2 && arc_editor_undo_steps() == 0 && !arc_editor_dirty());   /* changed for each cell that differs; history cleared */
    /* the undo ring is bounded: the oldest whole steps go */
    v072t_reset_grid(); v072t_begin(4);
    arc_editor_paint(0, 0, 'X'); arc_editor_paint(1, 0, 'X'); arc_editor_fill(0, 1, 2, 1, '#');
    CHECK(arc_editor_undo_steps() == 2);        /* 1 + 1 + 3 cells > 4: the first paint is gone */
    CHECK(arc_editor_undo() && arc_editor_undo() && !arc_editor_undo() && v072t_grid[0][0] == 'X' && v072t_grid[0][1] == '.');
    arc_editor_fill(0, 0, 7, 4, 'X');           /* one step bigger than the ring: not undoable, nothing half-undone */
    CHECK(arc_editor_undo_steps() == 0 && !arc_editor_undo() && v072t_count('X') == 40);
    /* an int grid */
    static int32_t ig[3][4]; memset(ig, 0, sizeof ig);
    static const arc_editor_tile itl[] = { { 0, "Empty", 0, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 }, { 300, "Big", 0xFF0000, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 } };
    arc_editor_desc d; memset(&d, 0, sizeof d);
    d.width = 4; d.height = 3; d.cells = ig; d.cell_type = ARC_EDITOR_I32; d.tiles = itl; d.tile_count = 2;
    arc_editor_begin(&d);
    arc__test_command("editor paint 3 2 =300"); CHECK(ig[2][3] == 300 && arc_editor_get(3, 2) == 300);
    CHECK(arc_editor_flood(0, 0, 300) == 11 && ig[0][0] == 300 && arc_editor_undo() && ig[0][0] == 0 && ig[2][3] == 300);
    arc_editor_end();
    CHECK(arc_editor_get(0, 0) == -1 && !arc_editor_paint(0, 0, 1));
}

/* ---- part 2: a pretend game with the editor, driven by input through ticks, then seeks ---- */
#define V072T_GW 16
#define V072T_GH 8
typedef struct { int ticks, x, y, clicks; uint32_t acc; char grid[V072T_GH][V072T_GW + 1]; } v072t_game_t;
static v072t_game_t v072t_g;
static void v072t_gtick(void) {
    v072t_g.ticks++; v072t_g.acc = v072t_g.acc * 31u + arc_rand_u32();
    if (arc_key_down(ARC_KEY_D)) v072t_g.x++;
    if (arc_key_pressed(ARC_KEY_J)) v072t_g.y++;
    if (arc_key_pressed(ARC_MOUSE_LEFT)) v072t_g.clicks++;
    if (v072t_g.ticks % 25 == 0) arc_event("v072t_ping", "\"n\":%d", v072t_g.ticks);
}
static size_t v072t_gsave(void *buf, size_t cap) { arc_writer w = { (uint8_t *)buf, cap, 0 }; arc_write(&w, &v072t_g, sizeof v072t_g); return w.len; }
static bool v072t_gload(const void *buf, size_t n) { arc_reader r = { (const uint8_t *)buf, n, 0, false }; arc_read(&r, &v072t_g, sizeof v072t_g); return !r.fail; }
static uint32_t v072t_hash(void) {   /* the game's state, and the editor's open / playing state */
    const uint8_t *p = (const uint8_t *)&v072t_g; uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof v072t_g; i++) { h ^= p[i]; h *= 16777619u; }
    h ^= (uint32_t)arc_editor_is_open() * 7u + (uint32_t)arc_editor_playing() * 13u + (uint32_t)arc_editor_undo_steps() * 101u;
    return h ^ (uint32_t)arc_tick_count() * 2654435761u;
}
static uint32_t v072t_h_at[400]; static bool v072t_rec;   /* the state after every tick of the first run */
static void v072t_step(int n) {
    for (int i = 0; i < n; i++) { arc__test_ticks(1); if (v072t_rec && arc_tick_count() < 400) v072t_h_at[arc_tick_count()] = v072t_hash(); }
}
static void v072t_tap(arc_key k) { arc__input_key(k, true); v072t_step(1); arc__input_key(k, false); v072t_step(1); }
static void v072t_combo(arc_key mod, arc_key k) { arc__input_key(mod, true); arc__input_key(k, true); v072t_step(1); arc__input_key(k, false); arc__input_key(mod, false); v072t_step(1); }
/* the 320 x 180 test window, cell_px 10: the 2D view sits at (24, 42) (see ed_layout / ed_fit) */
static void v072t_mouse_cell(int cx, int cy) { arc__input_mouse(24.0f + 10.0f * (float)cx + 5.0f, 42.0f + 10.0f * (float)cy + 5.0f); }
static void v072t_click_cell(int cx, int cy) { v072t_mouse_cell(cx, cy); v072t_step(1); arc__input_key(ARC_MOUSE_LEFT, true); v072t_step(1); arc__input_key(ARC_MOUSE_LEFT, false); v072t_step(1); }
static void v072t_reset_game(void) {
    memset(&v072t_g, 0, sizeof v072t_g);
    for (int y = 0; y < V072T_GH; y++) { memset(v072t_g.grid[y], (y == 0 || y == V072T_GH - 1) ? '#' : '.', V072T_GW); v072t_g.grid[y][0] = v072t_g.grid[y][V072T_GW - 1] = '#'; v072t_g.grid[y][8] = '#'; }
}
static void v072t_gbegin(void) {
    arc_editor_desc d; memset(&d, 0, sizeof d);
    d.name = "g"; d.width = V072T_GW; d.height = V072T_GH; d.cells = v072t_g.grid; d.stride = V072T_GW + 1;
    d.tiles = v072t_tiles; d.tile_count = 3; d.cell_px = 10;
    arc_editor_begin(&d);
}
static void v072t_sandbox(void) {
    arc_app app; memset(&app, 0, sizeof app);
    app.tick = v072t_gtick;
    /* without dev keys F2 does nothing; without save_state going back is refused (forward works) */
    v072t_reset_game(); arc__test_sandbox(&app); v072t_gbegin();
    v072t_step(5); v072t_tap(ARC_KEY_F2); CHECK(!arc_editor_is_open() && v072t_g.ticks == 7);
    CHECK(!arc_timeline_seek(3) && arc_tick_count() == 7);
    CHECK(arc_timeline_seek(100) && arc_tick_count() == 7);       /* the end of a live game is the newest tick */
    arc_editor_end(); arc__test_sandbox(NULL);

    app.dev_keys = true; app.save_state = v072t_gsave; app.load_state = v072t_gload;
    v072t_reset_game(); arc__test_sandbox(&app); v072t_gbegin();
    uint32_t *h_at = v072t_h_at; memset(v072t_h_at, 0, sizeof v072t_h_at); v072t_rec = true;
    arc__input_key(ARC_KEY_D, true); v072t_step(10); arc__input_key(ARC_KEY_D, false); v072t_step(1);
    CHECK(v072t_g.x == 10 && v072t_g.ticks == 11);
    uint64_t t_before = arc_tick_count();
    v072t_tap(ARC_KEY_F2); CHECK(arc_editor_is_open());
    int frozen = v072t_g.ticks; v072t_step(5); CHECK(v072t_g.ticks == frozen);   /* the game is paused while editing */
    v072t_tap(ARC_KEY_X);                                                        /* a tile's own key */
    v072t_mouse_cell(3, 2); v072t_step(1);
    int hx = -1, hy = -1; CHECK(arc_editor_hover(&hx, &hy) && hx == 3 && hy == 2);
    arc__input_key(ARC_MOUSE_LEFT, true); v072t_step(1); CHECK(v072t_g.grid[2][3] == 'X');   /* brush */
    v072t_mouse_cell(6, 2); v072t_step(1); arc__input_key(ARC_MOUSE_LEFT, false); v072t_step(1);
    CHECK(!strncmp(&v072t_g.grid[2][3], "XXXX", 4) && v072t_g.grid[2][7] == '.' && arc_editor_undo_steps() == 1);   /* a stroke is one step */
    CHECK(v072t_g.clicks == 0);
    uint64_t t_mid = arc_tick_count();
    v072t_combo(ARC_KEY_CTRL, ARC_KEY_Z); CHECK(v072t_g.grid[2][3] == '.' && v072t_g.grid[2][6] == '.');
    v072t_combo(ARC_KEY_CTRL, ARC_KEY_Y); CHECK(!strncmp(&v072t_g.grid[2][3], "XXXX", 4));
    v072t_tap(ARC_KEY_R);                                                        /* rectangle with the right button: the erase tile */
    v072t_mouse_cell(2, 2); v072t_step(1); arc__input_key(ARC_MOUSE_RIGHT, true); v072t_step(1);
    v072t_mouse_cell(4, 3); v072t_step(1); CHECK(v072t_g.grid[2][3] == 'X');      /* not until let go */
    arc__input_key(ARC_MOUSE_RIGHT, false); v072t_step(1);
    CHECK(!strncmp(&v072t_g.grid[2][2], "...XX", 5) && v072t_g.grid[3][4] == '.');
    v072t_tap(ARC_KEY_F); v072t_click_cell(10, 4);                               /* flood the right-hand room (6 x 6) with X */
    int nx = 0; for (int y = 1; y < V072T_GH - 1; y++) for (int x = 9; x < V072T_GW - 1; x++) nx += v072t_g.grid[y][x] == 'X';
    CHECK(nx == 36 && v072t_g.grid[4][8] == '#');
    v072t_tap(ARC_KEY_I); v072t_click_cell(0, 0);                                /* eyedropper: '#' */
    v072t_tap(ARC_KEY_B); v072t_click_cell(5, 5); CHECK(v072t_g.grid[5][5] == '#');
    v072t_tap(ARC_KEY_L); v072t_mouse_cell(1, 6); v072t_step(1); arc__input_key(ARC_MOUSE_LEFT, true); v072t_step(1);   /* line */
    v072t_mouse_cell(4, 6); v072t_step(1); arc__input_key(ARC_MOUSE_LEFT, false); v072t_step(1);
    CHECK(!strncmp(&v072t_g.grid[6][1], "####", 4));
    v072t_tap(ARC_KEY_P); frozen = v072t_g.ticks; v072t_step(4); CHECK(arc_editor_playing() && v072t_g.ticks == frozen + 4);   /* play from here */
    v072t_tap(ARC_KEY_P); frozen = v072t_g.ticks; v072t_step(3); CHECK(!arc_editor_playing() && v072t_g.ticks == frozen);
    CHECK(v072t_g.clicks == 0);
    v072t_tap(ARC_KEY_F2); CHECK(!arc_editor_is_open());
    arc__input_key(ARC_KEY_J, true); v072t_step(1); arc__input_key(ARC_KEY_J, false);
    v072t_step(120);
    v072t_rec = false; uint64_t t_end = arc_tick_count(); uint32_t h_end = v072t_hash();
    char g_end[V072T_GH][V072T_GW + 1]; memcpy(g_end, v072t_g.grid, sizeof g_end);
    int marks = arc_timeline_mark_count(); CHECK(marks >= 4);
    uint64_t mt = 0; const char *mn = NULL; bool bm = true;
    CHECK(arc_timeline_mark(0, &mt, &mn, &bm) && mn && !strcmp(mn, "v072t_ping") && !bm);
    /* seeking: every state the same as when it was played straight to */
    CHECK(arc_timeline_seek(t_mid) && arc_tick_count() == t_mid && v072t_hash() == h_at[t_mid] && arc_editor_is_open());
    CHECK(arc_timeline_seek(t_before) && v072t_hash() == h_at[t_before] && !arc_editor_is_open() && v072t_g.grid[2][3] == '.');
    CHECK(arc_timeline_seek(t_end) && v072t_hash() == h_end && !memcmp(g_end, v072t_g.grid, sizeof g_end));   /* forward again */
    bool all = true;
    for (uint64_t t = t_end; t-- > t_before;) if (!h_at[t] || (!arc_timeline_seek(t) || v072t_hash() != h_at[t])) all = false;
    CHECK(all);
    arc__test_command("timeline seek 7"); CHECK(arc_tick_count() == 7 && v072t_g.x == 7);
    arc__test_command("rewind 2"); CHECK(arc_tick_count() == 5);
    arc__test_command("timeline step 30"); CHECK(arc_tick_count() == 35 && v072t_hash() == h_at[35]);
    arc__test_command("timeline back 4"); CHECK(arc_tick_count() == 31);
    arc__test_command("timeline marks"); CHECK(arc_timeline_mark_count() == marks);   /* re-simulated events are not marked twice */
    CHECK(arc_timeline_length() == t_end);
    /* play on from the past: the inputs already played come back (not the live ones) up to the newest tick */
    arc_timeline_seek(t_end - 20); arc__input_key(ARC_KEY_D, true); v072t_step(20); arc__input_key(ARC_KEY_D, false);
    CHECK(arc_tick_count() == t_end && v072t_hash() == h_end);
    v072t_step(2); CHECK(arc_timeline_length() == t_end + 2);
    /* the bar's own input: keys while paused, clicks on the track and buttons; clicks on the bar never reach the game */
    arc_timeline_show(true); arc_timeline_set_paused(true);
    uint64_t t0 = arc_tick_count();
    arc__input_key(ARC_KEY_LEFT, true); arc__test_timeline_frame(); arc__input_key(ARC_KEY_LEFT, false); arc__test_timeline_frame();
    CHECK(arc_tick_count() == t0 - 1);
    arc__input_key(ARC_KEY_RIGHT, true); arc__test_timeline_frame(); arc__input_key(ARC_KEY_RIGHT, false); arc__test_timeline_frame();
    CHECK(arc_tick_count() == t0 && arc_timeline_paused());
    arc__input_key(ARC_KEY_EQUAL, true); arc__test_timeline_frame(); arc__input_key(ARC_KEY_EQUAL, false); arc__test_timeline_frame();
    arc__test_command("timeline info");
    /* 320 x 180: bar from y 138, buttons 24 wide from x 6 every 27 at y 155..175, the track from x 176 to 308 */
    uint64_t len = arc_timeline_length();
    arc__input_mouse(242, 165); arc__input_key(ARC_MOUSE_LEFT, true); arc__test_timeline_frame();
    arc__input_key(ARC_MOUSE_LEFT, false); arc__test_timeline_frame();
    CHECK(arc_tick_count() + 1 >= len / 2 && arc_tick_count() <= len / 2 + 1);
    arc__input_mouse(70, 165); arc__input_key(ARC_MOUSE_LEFT, true); arc__test_timeline_frame(); arc__input_key(ARC_MOUSE_LEFT, false); arc__test_timeline_frame();
    CHECK(!arc_timeline_paused());                                              /* the play button */
    arc_timeline_seek(len); int clicks = v072t_g.clicks;
    arc__input_mouse(150, 145); arc__input_key(ARC_MOUSE_LEFT, true); arc__test_timeline_frame(); v072t_step(1);
    arc__input_key(ARC_MOUSE_LEFT, false); arc__test_timeline_frame(); v072t_step(1);
    CHECK(v072t_g.clicks == clicks);                                             /* on the bar: not the game's */
    arc__input_mouse(150, 60); arc__input_key(ARC_MOUSE_LEFT, true); arc__test_timeline_frame(); v072t_step(1);
    arc__input_key(ARC_MOUSE_LEFT, false); arc__test_timeline_frame(); v072t_step(1);
    CHECK(v072t_g.clicks == clicks + 1);
    arc__input_mouse(0, 0);
    arc_editor_end(); arc__test_sandbox(NULL);
    CHECK(!arc_timeline_shown() && arc_tick_count() == 0);
}
static void v072t_init(void) { v072t_api(); v072t_sandbox(); }
