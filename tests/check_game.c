/* A tiny ARC game for testing tools/arc_check.py and the `frame` command (0.9.0): a named player cube moved with
   A/D, a score that counts collected coins, a text HUD, a health bar rect and a "Restart" button. */
#include "../include/arc.h"
#include <string.h>

static arc_mesh player_mesh, coin_mesh;
static float px; static int score, coins_left = 3; static float coin_x[3] = { 2, 4, 6 }; static bool coin_taken[3];
static bool restarted;

static void reset(void) { px = 0; score = 0; coins_left = 3; memset(coin_taken, 0, sizeof coin_taken); }
static void init(void) {
    player_mesh = arc_mesh_cube(0x40A0FF); arc_mesh_set_name(player_mesh, "player");
    coin_mesh = arc_mesh_sphere(0xFFD040, 12); arc_mesh_set_name(coin_mesh, "coin");
    reset();
}
static void tick(void) {
    float dir = (arc_key_down(ARC_KEY_D) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_A) ? 1.0f : 0.0f);
    px += dir * 4.0f * arc_dt();
    for (int i = 0; i < 3; i++) if (!coin_taken[i] && px > coin_x[i] - 0.5f && px < coin_x[i] + 0.5f) { coin_taken[i] = true; score += 10; coins_left--; }
    arc_ui_begin("", 10, 60, 160);
    if (arc_ui_button("Restart")) { reset(); restarted = true; }
    arc_ui_end();
}
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = { { 3, 4, 10 }, { 3, 0, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
    arc_set_camera(&cam);
    arc_draw_mesh(player_mesh, arc_m4_translate(arc_v3_make(px, 0.5f, 0)), 0xFFFFFF);
    for (int i = 0; i < 3; i++) if (!coin_taken[i]) arc_draw_mesh(coin_mesh, arc_m4_trs_y(arc_v3_make(coin_x[i], 0.5f, 0), 0, arc_v3_make(0.5f, 0.5f, 0.5f)), 0xFFFFFF);
    arc_draw_textf(10, 10, 2, 0xFFFFFF, "SCORE %d", score);
    arc_draw_rect(10, 34, 100, 10, 0x40E040);
}
static void dump(arc_json *j) {
    arc_json_num_k(j, "x", px); arc_json_num_k(j, "score", score); arc_json_num_k(j, "coins_left", coins_left);
    arc_json_bool_k(j, "restarted", restarted);
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "check game", .width = 640, .height = 360, .init = init, .tick = tick, .draw = draw, .dump = dump };
}
