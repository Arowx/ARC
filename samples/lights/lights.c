/* ARC sample (0.9.5): many lights and light shadows. A dark hall of pillars: a flickering torch circles the hall and
   casts shadows all round (a shadowed point light), a sweeping searchlight casts them one way (a shadowed spot), and
   up to 200 glowing wisps drift about, each a small light of its own (tiled: a pixel only pays for the lights that
   reach it). S: shadows on/off; W: wisps 0 / 50 / 200; SPACE: stop the torch. Agent commands: `wisps N`, `shadows 0|1`,
   `torch X Z` (put the torch there and stop it). dump() reports the light counts and where the torch is. */
#include "../../include/arc.h"
#include <string.h>
#include <stdlib.h>

#define MAXW 200
typedef struct { int wisps; bool shadows, stopped; float t; arc_v3 torch; arc_v3 wp[MAXW], wv[MAXW]; } Game;
static Game G;
static arc_mesh m_floor, m_pillar, m_wall, m_ball, m_crate;
static arc_camera cam;

static void init(void) {
    m_floor = arc_mesh_plane(0x5A5048, 16, 0x4E4640); m_pillar = arc_mesh_cylinder(0xB8B0A4, 20); m_wall = arc_mesh_cube(0x8A8278);
    m_ball = arc_mesh_sphere(0xFFFFFF, 12); m_crate = arc_mesh_cube(0xA07848);
    arc_mesh_set_name(m_floor, "floor"); arc_mesh_set_name(m_pillar, "pillar"); arc_mesh_set_name(m_wall, "wall");
    arc_mesh_set_name(m_ball, "wisp"); arc_mesh_set_name(m_crate, "crate");
    arc_mesh_set_cast_shadow(m_ball, false);   /* the wisps glow: they cast no shadows */
    G.wisps = 50; G.shadows = true;
    arc_rng r; arc_rng_seed(&r, 7, 1);
    for (int i = 0; i < MAXW; i++) {
        G.wp[i] = arc_v3_make(arc_rng_rangef(&r, -9, 9), arc_rng_rangef(&r, 0.4f, 2.4f), arc_rng_rangef(&r, -7, 7));
        G.wv[i] = arc_v3_make(arc_rng_rangef(&r, -1, 1), arc_rng_rangef(&r, -0.2f, 0.2f), arc_rng_rangef(&r, -1, 1));
    }
    cam = (arc_camera){ { 0, 9, 13 }, { 0, 0.5f, -0.5f }, { 0, 1, 0 }, 0.85f, 0, 0.1f, 100 };
    arc_post p; memset(&p, 0, sizeof p); p.bloom = 0.5f; p.bloom_threshold = 1.2f; p.exposure = 1.1f; arc_set_post(&p);
}
static void tick(void) {
    float dt = arc_dt();
    if (arc_key_pressed(ARC_KEY_S)) G.shadows = !G.shadows;
    if (arc_key_pressed(ARC_KEY_W)) G.wisps = G.wisps == 0 ? 50 : G.wisps == 50 ? MAXW : 0;
    if (arc_key_pressed(ARC_KEY_SPACE)) G.stopped = !G.stopped;
    G.t += dt;
    if (!G.stopped) { float a = G.t * 0.5f; G.torch = arc_v3_make(5.5f * cosf(a), 1.6f, 4 * sinf(a)); }
    for (int i = 0; i < G.wisps; i++) {   /* drift, bouncing off the hall's walls */
        arc_v3 *p = &G.wp[i], *v = &G.wv[i];
        *p = arc_v3_madd(*p, *v, dt);
        if (p->x < -9.5f || p->x > 9.5f) v->x = -v->x;
        if (p->y < 0.3f || p->y > 2.6f) v->y = -v->y;
        if (p->z < -7.5f || p->z > 7.5f) v->z = -v->z;
    }
}
static void draw(float alpha) {
    (void)alpha;
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.4f), 0x101418, 0x0C0E14);   /* a faint moon: the lights do the work */
    int fl = G.shadows ? ARC_LIGHT_SHADOW : 0;
    float flick = 0.9f + 0.1f * sinf(G.t * 23) * sinf(G.t * 7.3f);
    arc_light_point_ex(G.torch, 0xFFB060, 3.2f * flick, 9, fl);
    float s = G.t * 0.4f;
    arc_light_spot_ex(arc_v3_make(-8, 6, -6), arc_v3_norm(arc_v3_make(8 + 6 * sinf(s), -6, 6 + 3 * cosf(s))), 0xA0C0FF, 5, 22, 0.18f, 0.28f, fl);
    static const uint32_t wc[4] = { 0x60FFB0, 0x60B0FF, 0xFF70C0, 0xFFE070 };
    for (int i = 0; i < G.wisps; i++) arc_light_point(G.wp[i], wc[i & 3], 1.3f, 1.6f);
    /* the hall */
    arc_draw_mesh(m_floor, arc_m4_scale(arc_v3_make(20, 1, 16)), 0xFFFFFF);
    arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(0, 1.5f, -8), 0, arc_v3_make(20, 3, 0.4f)), 0xFFFFFF);
    arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(-10, 1.5f, 0), 0, arc_v3_make(0.4f, 3, 16)), 0xFFFFFF);
    arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(10, 1.5f, 0), 0, arc_v3_make(0.4f, 3, 16)), 0xFFFFFF);
    for (int x = -1; x <= 1; x += 2) for (int z = -1; z <= 1; z++) arc_draw_mesh(m_pillar, arc_m4_trs_y(arc_v3_make(3.0f * (float)x, 1.5f, 3.2f * (float)z), 0, arc_v3_make(0.7f, 3, 0.7f)), 0xFFFFFF);
    arc_draw_mesh(m_crate, arc_m4_trs_y(arc_v3_make(0, 0.4f, 0), 0.4f, arc_v3_make(0.8f, 0.8f, 0.8f)), 0xFFFFFF);
    arc_draw_mesh(m_crate, arc_m4_trs_y(arc_v3_make(-6, 0.5f, -5), 0.9f, arc_v3_make(1, 1, 1)), 0xFFFFFF);
    arc_draw_mesh(m_crate, arc_m4_trs_y(arc_v3_make(6.5f, 0.35f, 5), 0.2f, arc_v3_make(0.7f, 0.7f, 0.7f)), 0xFFFFFF);
    /* the torch and the wisps themselves: small glowing balls */
    arc_draw_mesh_tinted(m_ball, arc_m4_trs_y(G.torch, 0, arc_v3_make(0.18f, 0.18f, 0.18f)), 0xFFC070, 1, 3, 0);
    for (int i = 0; i < G.wisps; i++) arc_draw_mesh_tinted(m_ball, arc_m4_trs_y(G.wp[i], 0, arc_v3_make(0.07f, 0.07f, 0.07f)), wc[i & 3], 1, 2.5f, 0);
    arc_draw_textf(10, 10, 2, 0xFFFFFF, "%d lights, %d casting shadows", arc_light_count(), arc_light_shadow_count());
    arc_draw_text(10, 32, 2, 0xC8D0E0, "S: shadows   W: wisps 0 / 50 / 200   SPACE: stop the torch");
}
static void dump(arc_json *j) {
    arc_json_num_k(j, "wisps", G.wisps); arc_json_bool_k(j, "shadows", G.shadows);
    arc_json_arr_k(j, "torch"); arc_json_num(j, G.torch.x); arc_json_num(j, G.torch.y); arc_json_num(j, G.torch.z); arc_json_end(j);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "wisps") && argc >= 1) { int n = atoi(argv[0]); G.wisps = n < 0 ? 0 : n > MAXW ? MAXW : n; return true; }
    if (!strcmp(name, "shadows") && argc >= 1) { G.shadows = atoi(argv[0]) != 0; return true; }
    if (!strcmp(name, "torch") && argc >= 2) { G.torch = arc_v3_make((float)atof(argv[0]), 1.6f, (float)atof(argv[1])); G.stopped = true; return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t size) { if (size != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC lights", .width = 1100, .height = 650, .clear_color = 0x000000, .init = init, .tick = tick, .draw = draw,
                      .dump = dump, .command = command, .save_state = save_state, .load_state = load_state };
}
