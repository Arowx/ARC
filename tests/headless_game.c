/* ARC 0.9.8 test game for the headless tools (tests/headless_tools_test.py): an immediate-mode game whose 3D view is
   drawn into a render target with post effects every tick (the slow case for headless screenshots), a texture and a
   sound loaded from files (--tex FILE, --sound FILE), and a little random walk kept in save_state, so `reset`,
   --draw-last, --shot-at and --no-assets can be checked. Agent command: `kick` (a random push). */
#include "../include/arc.h"
#include <string.h>
typedef struct { float x, z, vx, vz; int kicks; } Game;
static Game G;
static arc_texture view, tex; static arc_sound snd; static arc_mesh cube;
static void init(void) {
    view = arc_target_create(320, 200, true); cube = arc_mesh_cube(0xFFFFFF);
    arc_target_set_post(view, &(arc_post){ .bloom = 0.4f, .bloom_threshold = 0.8f, .vignette = 0.3f, .ssao = 0.5f });
    const char *t = arc_arg_value("tex"), *s = arc_arg_value("sound");
    if (t) tex = arc_texture_load_ex(t, ARC_TEX_MESH);
    if (s) snd = arc_sound_load(s);
    memset(&G, 0, sizeof G);
}
static void tick(void) {
    G.vx += arc_rand_rangef(-0.2f, 0.2f); G.vz += arc_rand_rangef(-0.2f, 0.2f);
    G.x += G.vx * arc_dt(); G.z += G.vz * arc_dt();
    arc_target_begin(view, true, 0xFF203040);
    arc_camera cam = { { 3, 3, 4 }, { G.x, 0, G.z }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 50 };
    arc_set_camera(&cam); arc_set_light(arc_v3_make(0.4f, 1, 0.3f), 0xFFFFFF, 0x303040);
    for (int i = 0; i < 24; i++) arc_draw_mesh(cube, arc_m4_trs_y(arc_v3_make((float)(i % 6) - 3 + G.x, 0, (float)(i / 6) - 2 + G.z), (float)i + G.x, arc_v3_make(0.5f, 0.5f, 0.5f)), 0xC08040);
    arc_target_end();
    arc_draw_sprite(view, 0, 0, 320, 200, 0, 0, 1, 1, 0xFFFFFF);
    if (tex.id) arc_draw_sprite(tex, 260, 10, 50, 50, 0, 0, 1, 1, 0xFFFFFF);
}
static void dump(arc_json *j) {
    int w = 0, h = 0; arc_texture_size(tex, &w, &h);
    arc_json_num_k(j, "x", G.x); arc_json_num_k(j, "z", G.z); arc_json_num_k(j, "kicks", G.kicks);
    arc_json_num_k(j, "tex_w", w); arc_json_num_k(j, "tex_h", h); arc_json_num_k(j, "sound_s", arc_sound_length(snd));
}
static bool command(const char *name, int argc, const char **argv) {
    (void)argc; (void)argv;
    if (!strcmp(name, "kick")) { G.vx += arc_rand_rangef(-3, 3); G.kicks++; return true; }
    return false;
}
static size_t save_state(void *b, size_t cap) { if (cap >= sizeof G) memcpy(b, &G, sizeof G); return sizeof G; }
static bool load_state(const void *b, size_t n) { if (n != sizeof G) return false; memcpy(&G, b, sizeof G); return true; }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "headless tools", .width = 320, .height = 200, .init = init, .tick = tick, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state, .immediate = true };
}
