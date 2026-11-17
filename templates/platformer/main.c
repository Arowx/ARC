/* {title} - a side-scrolling platformer starter (ARC template "platformer").
 *
 * A/D or LEFT/RIGHT run, SPACE/W/UP jumps (hold for higher; you can still jump a moment after leaving a ledge, and
 * a press just before landing counts). Collect the coins, avoid the spikes (back to the start, or the last
 * checkpoint), reach the flag. R restarts.
 *
 * Where things are:
 *   LEVEL            the map as strings: '#' ground, 'o' coin, '^' spikes, 'C' checkpoint, 'F' flag, 'P' start.
 *   Game G           player position/velocity, coins taken, deaths, timers: pointer-free, so save_state/load_state
 *                    copy it and rewind, seek and the timeline work.
 *   move_and_collide the box-vs-tiles physics: move along x, push out; then along y, push out (ground/ceiling).
 *   tick()           input, coyote time and jump buffering, hazards, pick-ups, win. draw() only draws.
 *   dump()           x, y, vx, vy, on_ground, coins, deaths, phase, checkpoint.
 *   command()        agent commands: tp X Y, kill, win, restart.
 *   arc_var          tunables: run_speed, jump_speed, gravity (`set gravity 30`).
 * Grow it: more levels (an array of maps), enemies that walk between walls, moving platforms (boxes the player
 * rides), wall jumps, a timer and best times (arc_save).
 */
#include "arc.h"
#include <string.h>
#include <stdlib.h>

#define LW 48
#define LH 10
static const char *LEVEL[LH] = {
    "                                                ",
    "                                                ",
    "                                          ooo  F",
    "                    ooo                  ###### ",
    "           o       #####       o o              ",
    "          ###                 #####    ##       ",
    "    o                                           ",
    " P      ^^      ##   ^^   C  #     ^^^     ##   ",
    "################################################",
    "################################################",
};
enum { PHASE_PLAY, PHASE_WON };
#define PW 0.35f   /* half width of the player's box */
#define PH 0.9f    /* height */

typedef struct {
    int phase;
    arc_v2 p, v;                  /* feet centre; velocity in units a second */
    bool on_ground;
    int coyote, buffer;           /* ticks left to still jump after leaving ground / of a remembered press */
    int coins, deaths, ticks;
    arc_v2 checkpoint;
    bool taken[LH][LW];           /* coins picked up */
    int face;                     /* -1 left, 1 right */
} Game;
static Game G;

static float *RUN, *JUMP, *GRAVITY, v_run, v_jump, v_grav;
static arc_mesh m_ground, m_coin, m_spike, m_flag, m_player, m_check;
static arc_sound s_jump, s_coin, s_die, s_win;
static int total_coins;

static char tile(int cx, int cy) {   /* cy counts up from the bottom row */
    int row = LH - 1 - cy;
    if (cx < 0 || cx >= LW) return '#';   /* walls at both ends */
    if (row < 0) return ' ';
    if (row >= LH) return '#';
    return LEVEL[row][cx];
}
static bool solid(int cx, int cy) { return tile(cx, cy) == '#'; }
static arc_v2 find(char c) {
    for (int r = 0; r < LH; r++) for (int x = 0; x < LW; x++) if (LEVEL[r][x] == c) return arc_v2_make((float)x + 0.5f, (float)(LH - 1 - r));
    return arc_v2_make(1.5f, 2);
}

static void move_and_collide(float dx, float dy) {
    G.p.x += dx;
    int y0 = (int)floorf(G.p.y + 0.01f), y1 = (int)floorf(G.p.y + PH - 0.01f);
    if (dx > 0) { int cx = (int)floorf(G.p.x + PW); for (int cy = y0; cy <= y1; cy++) if (solid(cx, cy)) { G.p.x = (float)cx - PW - 0.001f; G.v.x = 0; break; } }
    if (dx < 0) { int cx = (int)floorf(G.p.x - PW); for (int cy = y0; cy <= y1; cy++) if (solid(cx, cy)) { G.p.x = (float)(cx + 1) + PW + 0.001f; G.v.x = 0; break; } }
    G.p.y += dy; G.on_ground = false;
    int x0 = (int)floorf(G.p.x - PW + 0.01f), x1 = (int)floorf(G.p.x + PW - 0.01f);
    if (dy <= 0) { int cy = (int)floorf(G.p.y); for (int cx = x0; cx <= x1; cx++) if (solid(cx, cy)) { G.p.y = (float)(cy + 1); G.v.y = 0; G.on_ground = true; break; } }
    else { int cy = (int)floorf(G.p.y + PH); for (int cx = x0; cx <= x1; cx++) if (solid(cx, cy)) { G.p.y = (float)cy - PH; G.v.y = 0; break; } }
}
static bool touching(char c) {
    int x0 = (int)floorf(G.p.x - PW + 0.1f), x1 = (int)floorf(G.p.x + PW - 0.1f), y0 = (int)floorf(G.p.y + 0.05f), y1 = (int)floorf(G.p.y + PH - 0.1f);
    for (int cy = y0; cy <= y1; cy++) for (int cx = x0; cx <= x1; cx++) if (tile(cx, cy) == c) return true;
    return false;
}
static void respawn(void) { G.p = G.checkpoint; G.v = arc_v2_make(0, 0); G.deaths++; arc_play(s_die, 0.5f, 0); }
static void reset(void) {
    memset(&G, 0, sizeof G); G.p = G.checkpoint = find('P'); G.face = 1;
}

static void init(void) {
    RUN = arc_var("run_speed", &v_run, 7); JUMP = arc_var("jump_speed", &v_jump, 12.5f); GRAVITY = arc_var("gravity", &v_grav, 34);
    m_ground = arc_mesh_cube(0x6A9050); arc_mesh_set_name(m_ground, "ground");
    m_coin = arc_mesh_cylinder(0xFFD040, 16); arc_mesh_set_name(m_coin, "coin");
    m_spike = arc_mesh_cone(0xC8C8D0, 4); arc_mesh_set_name(m_spike, "spike");
    m_flag = arc_mesh_cube(0xF04060); arc_mesh_set_name(m_flag, "flag");
    m_check = arc_mesh_cube(0x60C0FF); arc_mesh_set_name(m_check, "checkpoint");
    m_player = arc_mesh_cube(0xF0A030); arc_mesh_set_name(m_player, "player");
    s_jump = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_SQUARE, .freq = 300, .freq_end = 700, .sustain = 0.03f, .decay = 0.08f, .duty = 0.3f });
    s_coin = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_SINE, .freq = 1200, .freq_end = 1800, .sustain = 0.03f, .decay = 0.12f });
    s_die = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_SAW, .freq = 400, .freq_end = 60, .sustain = 0.1f, .decay = 0.3f });
    s_win = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_TRIANGLE, .freq = 523, .freq_end = 1046, .sustain = 0.3f, .decay = 0.4f });
    for (int r = 0; r < LH; r++) for (int x = 0; x < LW; x++) total_coins += LEVEL[r][x] == 'o';
    reset();
}
static void tick(void) {
    if (arc_key_pressed(ARC_KEY_R)) reset();
    if (G.phase != PHASE_PLAY) return;
    G.ticks++;
    float dt = arc_dt();
    float in = (arc_key_down(ARC_KEY_D) || arc_key_down(ARC_KEY_RIGHT) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_A) || arc_key_down(ARC_KEY_LEFT) ? 1.0f : 0.0f);
    if (in != 0) G.face = in > 0 ? 1 : -1;
    float target = in * *RUN, accel = (G.on_ground ? 60.0f : 30.0f) * dt;
    G.v.x += arc_clampf(target - G.v.x, -accel, accel);
    bool jump_down = arc_key_down(ARC_KEY_SPACE) || arc_key_down(ARC_KEY_W) || arc_key_down(ARC_KEY_UP);
    bool jump_pressed = arc_key_pressed(ARC_KEY_SPACE) || arc_key_pressed(ARC_KEY_W) || arc_key_pressed(ARC_KEY_UP);
    G.coyote = G.on_ground ? 6 : (G.coyote > 0 ? G.coyote - 1 : 0);
    G.buffer = jump_pressed ? 6 : (G.buffer > 0 ? G.buffer - 1 : 0);
    if (G.buffer > 0 && G.coyote > 0) { G.v.y = *JUMP; G.buffer = G.coyote = 0; arc_play(s_jump, 0.3f, 0); }
    if (!jump_down && G.v.y > 0) G.v.y *= 0.55f;   /* let go early: a shorter jump */
    G.v.y = fmaxf(G.v.y - *GRAVITY * dt, -20.0f);
    move_and_collide(G.v.x * dt, G.v.y * dt);
    /* pick-ups and hazards */
    int x0 = (int)floorf(G.p.x - PW), x1 = (int)floorf(G.p.x + PW), y0 = (int)floorf(G.p.y), y1 = (int)floorf(G.p.y + PH);
    for (int cy = y0; cy <= y1; cy++) for (int cx = x0; cx <= x1; cx++) {
        int row = LH - 1 - cy;
        if (tile(cx, cy) == 'o' && row >= 0 && row < LH && !G.taken[row][cx]) { G.taken[row][cx] = true; G.coins++; arc_play(s_coin, 0.3f, 0); }
    }
    if (touching('C')) G.checkpoint = find('C');
    if (touching('^') || G.p.y < -3) { respawn(); return; }
    if (touching('F')) { G.phase = PHASE_WON; arc_play(s_win, 0.5f, 0); arc_event("won", "\"coins\":%d,\"deaths\":%d,\"ticks\":%d", G.coins, G.deaths, G.ticks); }
}
static void draw(float alpha) {
    (void)alpha;
    float cx = arc_clampf(G.p.x, 9.5f, LW - 9.5f);
    arc_camera cam = { { cx, 5, 13 }, { cx, 3.6f, 0 }, { 0, 1, 0 }, 0.7f, 0, 0.1f, 100 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.6f), 0xFFF4E0, 0x506070);
    arc_set_sky(0x4878C0, 0xA8C8E8);
    float t = (float)arc_time();
    for (int r = 0; r < LH; r++) for (int x = 0; x < LW; x++) {
        char c = LEVEL[r][x]; float y = (float)(LH - 1 - r);
        if (fabsf((float)x - cx) > 14) continue;   /* only what the camera can see */
        if (c == '#') arc_draw_mesh(m_ground, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y + 0.5f, 0), 0, arc_v3_make(1, 1, 1)), solid(x, (int)y + 1) ? 0x8A6A48 : 0xFFFFFF);
        else if (c == 'o' && !G.taken[r][x]) arc_draw_mesh(m_coin, arc_m4_mul(arc_m4_translate(arc_v3_make((float)x + 0.5f, y + 0.5f, 0)), arc_m4_mul(arc_m4_rotate_y(t * 3), arc_m4_mul(arc_m4_rotate_x(1.5708f), arc_m4_scale(arc_v3_make(0.5f, 0.1f, 0.5f))))), 0xFFFFFF);
        else if (c == '^') arc_draw_mesh(m_spike, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y + 0.25f, 0), 0.785f, arc_v3_make(0.7f, 0.5f, 0.7f)), 0xFFFFFF);
        else if (c == 'F') { arc_draw_mesh(m_flag, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y + 1.0f, 0), 0, arc_v3_make(0.1f, 2, 0.1f)), 0xE0E0E0);
                             arc_draw_mesh(m_flag, arc_m4_trs_y(arc_v3_make((float)x + 0.85f, y + 1.7f, 0), 0, arc_v3_make(0.6f, 0.4f, 0.05f)), 0xFFFFFF); }
        else if (c == 'C') arc_draw_mesh(m_check, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y + 0.6f, 0), 0, arc_v3_make(0.1f, 1.2f, 0.1f)), G.checkpoint.x == (float)x + 0.5f ? 0xFFFFFF : 0x707070);
    }
    float squash = G.on_ground ? 1.0f : 1.08f;
    arc_draw_mesh(m_player, arc_m4_trs_y(arc_v3_make(G.p.x, G.p.y + PH * 0.5f * squash, 0), 0, arc_v3_make(PW * 2 / squash, PH * squash, PW * 2)), 0xFFFFFF);
    arc_draw_textf(10, 10, 3, 0xFFFFFF, "COINS %d/%d", G.coins, total_coins);
    arc_draw_textf(10, 40, 2, 0xE0E8F0, "DEATHS %d   TIME %.1f", G.deaths, (float)G.ticks * arc_dt());
    if (G.phase == PHASE_WON) arc_draw_text_ex((float)arc_width() / 2.0f, (float)arc_height() / 3.0f, 4, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE, "LEVEL CLEAR - R to play again");
}
static void dump(arc_json *j) {
    arc_json_str_k(j, "phase", G.phase == PHASE_WON ? "won" : "play");
    arc_json_num_k(j, "x", G.p.x); arc_json_num_k(j, "y", G.p.y); arc_json_num_k(j, "vx", G.v.x); arc_json_num_k(j, "vy", G.v.y);
    arc_json_bool_k(j, "on_ground", G.on_ground); arc_json_num_k(j, "coins", G.coins); arc_json_num_k(j, "total_coins", total_coins);
    arc_json_num_k(j, "deaths", G.deaths); arc_json_num_k(j, "checkpoint_x", G.checkpoint.x); arc_json_num_k(j, "ticks", G.ticks);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "tp") && argc >= 2) { G.p = arc_v2_make((float)atof(argv[0]), (float)atof(argv[1])); G.v = arc_v2_make(0, 0); return true; }
    if (!strcmp(name, "kill")) { respawn(); return true; }
    if (!strcmp(name, "win")) { G.p = find('F'); return true; }
    if (!strcmp(name, "restart")) { reset(); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t size) { if (size != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "{title}", .width = 960, .height = 540, .clear_color = 0x88B0E0,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
