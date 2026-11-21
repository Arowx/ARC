/* ARC sample (0.9.6): terrain. 200 x 200 m of hills from noise, four ground layers (grass, dirt, rock, snow) laid down
   by height and slope rules, and scattered grass tufts, trees and rocks that grow where their layer is. The camera
   circles the valley (arrow keys turn and zoom). Hold the left mouse button to work on the ground under the cursor:
   1 paints dirt, 2 paints grass, 3 raises, 4 lowers, 5 flattens; the scatter regrows when you let go. Agent commands:
   `paint X Z LAYER`, `sculpt X Z AMOUNT`, `regrow`, `terrain_colour red|map|off` (0.10.13: a colour map over the
   ground, solid red or a patchwork of fields; the layers' detail stays on top). dump() reports the copies placed and drawn and the height under
   the camera's target. */
#include "../../include/arc.h"
#include <string.h>
#include <stdlib.h>

#define N 257
static arc_texture cmap_tex; static int cmap_kind;   /* 0.10.13 terrain_colour: 0 off, 1 red, 2 fields */
static float heights[N * N];
static arc_terrain ter;
static arc_scatter grass, trees, rocks;
static arc_mesh m_tuft, m_tree, m_rock, m_marker;
typedef struct { float yaw, dist; int tool; bool dirty; arc_v3 aim; bool aim_ok; } Game;
static Game G;

static arc_texture make_layer(int kind) {   /* 128 x 128 ground textures from noise; alpha = height for the blend */
    static uint8_t px[128 * 128 * 4];
    static const float base[4][3] = { { 0.28f, 0.50f, 0.18f }, { 0.46f, 0.34f, 0.22f }, { 0.45f, 0.44f, 0.43f }, { 0.90f, 0.92f, 0.96f } };
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
        float n = arc_noise_fbm2((float)x * 0.09f, (float)y * 0.09f, 4, 20u + (uint32_t)kind), fine = arc_noise_value2((float)x * 0.6f, (float)y * 0.6f, 40u + (uint32_t)kind);
        float g = 0.7f + 0.45f * (n - 0.5f) + 0.15f * (fine - 0.5f);
        uint8_t *p = px + (y * 128 + x) * 4;
        for (int c = 0; c < 3; c++) p[c] = (uint8_t)arc_clampf(base[kind][c] * g * 255.0f, 0, 255);
        p[3] = (uint8_t)arc_clampf((kind == 2 ? n * 1.4f - 0.2f : kind == 1 ? 0.35f : 0.5f) * 255.0f, 0, 255);
    }
    return arc_texture_create_ex(128, 128, px, ARC_TEX_MESH);
}
static void regrow(void) { arc_scatter_rebuild(grass); arc_scatter_rebuild(trees); arc_scatter_rebuild(rocks); G.dirty = false; }
static void init(void) {
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        float x = (float)i / (N - 1), z = (float)j / (N - 1);
        float hills = arc_noise_fbm2(x * 4, z * 4, 5, 3u), ridge = 1.0f - fabsf(arc_noise_fbm2(x * 2 + 9, z * 2, 3, 4u) * 2 - 1);
        float valley = 0.3f + 0.7f * fminf(1.0f, ((x - 0.5f) * (x - 0.5f) + (z - 0.5f) * (z - 0.5f)) * 6);   /* lower in the middle */
        heights[j * N + i] = (hills * 18 + ridge * ridge * 22) * valley - 8;
    }
    arc_texture lay[4] = { make_layer(0), make_layer(1), make_layer(2), make_layer(3) };
    ter = arc_terrain_create(&(arc_terrain_desc){ .heights = heights, .samples_x = N, .samples_z = N, .size_x = 200, .size_z = 200, .origin = { -100, 0, -100 },
        .layers = { lay[0], lay[1], lay[2], lay[3] }, .layer_count = 4, .layer_scale = { 0.35f, 0.3f, 0.12f, 0.2f }, .splat_sharpness = 0.6f, .splat_res = 512 });
    for (int i = 0; i < 4; i++) arc_texture_destroy(lay[i]);
    arc_splat_rule rules[3] = {
        { .layer = 1, .height_min = -10, .height_max = -2.5f, .blend = 1.5f },   /* dirt along the valley floor */
        { .layer = 2, .slope_min = 30, .slope_max = 90, .blend = 8 },           /* rock where it is steep */
        { .layer = 3, .height_min = 22, .height_max = 100, .blend = 3 },        /* snow on the tops */
    };
    arc_terrain_splat_rules(ter, rules, 3);
    for (int k = 0; k <= 120; k++) { float z = -70 + (float)k * (140.0f / 120); arc_terrain_paint(ter, 12 * sinf(z * 0.05f) - 6, z, 3.5f, 1, 0.8f); }   /* a winding dirt track */
    m_tuft = arc_mesh_cone(0x60A040, 4); arc_mesh_set_name(m_tuft, "tuft"); arc_mesh_set_cast_shadow(m_tuft, false);
    m_tree = arc_mesh_cone(0x2A5A2C, 8); arc_mesh_set_name(m_tree, "tree");
    m_rock = arc_mesh_sphere(0x908C86, 8); arc_mesh_set_name(m_rock, "rock");
    m_marker = arc_mesh_cylinder(0xFFD040, 12); arc_mesh_set_cast_shadow(m_marker, false);
    grass = arc_scatter_create(ter, &(arc_scatter_desc){ .mesh = m_tuft, .density = 1.2f, .layer = 0, .min_weight = 0.6f, .scale_min = 0.25f, .scale_max = 0.45f,
                                .sink = -0.4f, .align = 0.6f, .tint = 0xC8E0A0, .tint2 = 0x80A060, .draw_distance = 45, .seed = 1 });
    trees = arc_scatter_create(ter, &(arc_scatter_desc){ .mesh = m_tree, .density = 0.012f, .layer = 0, .slope_max = 22, .scale_min = 2.5f, .scale_max = 5,
                                .sink = -0.45f, .tint = 0xFFFFFF, .tint2 = 0x90B090, .draw_distance = 160, .seed = 2 });
    rocks = arc_scatter_create(ter, &(arc_scatter_desc){ .mesh = m_rock, .density = 0.06f, .layer = 2, .slope_max = 70, .scale_min = 0.5f, .scale_max = 1.6f,
                                .align = 1, .sink = 0.15f, .draw_distance = 120, .seed = 3 });
    G.yaw = 0.6f; G.dist = 95;
}
static void tick(void) {
    if (arc_key_down(ARC_KEY_LEFT)) G.yaw -= arc_dt();
    if (arc_key_down(ARC_KEY_RIGHT)) G.yaw += arc_dt();
    if (arc_key_down(ARC_KEY_UP)) G.dist = fmaxf(25, G.dist - 40 * arc_dt());
    if (arc_key_down(ARC_KEY_DOWN)) G.dist = fminf(160, G.dist + 40 * arc_dt());
    for (int k = 0; k < 5; k++) if (arc_key_pressed((arc_key)(ARC_KEY_1 + k))) G.tool = k;
    G.yaw += arc_dt() * 0.05f;
    if (arc_key_down(ARC_MOUSE_LEFT) && G.aim_ok) {
        arc_v3 p = G.aim; float dt = arc_dt();
        if (G.tool == 0) arc_terrain_paint(ter, p.x, p.z, 4, 1, 4 * dt);
        else if (G.tool == 1) arc_terrain_paint(ter, p.x, p.z, 4, 0, 4 * dt);
        else if (G.tool == 2) arc_terrain_sculpt(ter, p.x, p.z, 8, 6 * dt);
        else if (G.tool == 3) arc_terrain_sculpt(ter, p.x, p.z, 8, -6 * dt);
        else arc_terrain_flatten(ter, p.x, p.z, 8, p.y, 3 * dt);
        G.dirty = true;
    }
    if (G.dirty && !arc_key_down(ARC_MOUSE_LEFT)) regrow();
}
static arc_camera camera(void) {
    arc_v3 at = arc_v3_make(0, 0, 0); at.y = arc_terrain_height(ter, 0, 0) + 2;
    return (arc_camera){ { at.x + G.dist * sinf(G.yaw), at.y + G.dist * 0.45f, at.z + G.dist * cosf(G.yaw) }, at, { 0, 1, 0 }, 0.8f, 0, 0.5f, 600 };
}
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = camera(); arc_set_camera(&cam);
    arc_set_sky(0x4A78B8, 0xB8CCE0); arc_set_fog(0xB8CCE0, 120, 420);
    arc_set_light(arc_v3_make(0.5f, 0.8f, 0.3f), 0xFFF0D8, 0x506070);
    arc_set_shadows(cam.target, 60, 2048);
    arc_terrain_draw(ter, 0xFFFFFF);
    arc_scatter_draw(grass); arc_scatter_draw(trees); arc_scatter_draw(rocks);
    /* the cursor on the ground */
    arc_v3 o, d; arc_camera_ray(&cam, (float)arc_width(), (float)arc_height(), arc_mouse(), &o, &d);
    G.aim_ok = arc_terrain_raycast(ter, o, d, 800, NULL, &G.aim);
    if (G.aim_ok) arc_draw_mesh(m_marker, arc_m4_trs_y(G.aim, 0, arc_v3_make(0.4f, 3, 0.4f)), 0xFFD040);
    static const char *tools[5] = { "paint dirt", "paint grass", "raise", "lower", "flatten" };
    arc_draw_textf(10, 10, 2, 0xFFFFFF, "tool %d: %s   grass %d/%d  trees %d/%d  rocks %d/%d", G.tool + 1, tools[G.tool],
                   arc_scatter_drawn(grass), arc_scatter_count(grass), arc_scatter_drawn(trees), arc_scatter_count(trees), arc_scatter_drawn(rocks), arc_scatter_count(rocks));
    arc_draw_text(10, 32, 2, 0xE0E8F0, "hold the mouse on the ground to use the tool   1-5: tools   arrows: turn and zoom");
}
static void dump(arc_json *j) {
    arc_json_num_k(j, "grass", arc_scatter_count(grass)); arc_json_num_k(j, "grass_drawn", arc_scatter_drawn(grass));
    arc_json_num_k(j, "trees", arc_scatter_count(trees)); arc_json_num_k(j, "rocks", arc_scatter_count(rocks));
    arc_json_num_k(j, "height_at_target", arc_terrain_height(ter, 0, 0)); arc_json_num_k(j, "tool", G.tool + 1);
    arc_json_num_k(j, "colour_map", cmap_kind); arc_json_num_k(j, "dirt_at_0_0", arc_terrain_layer_weight(ter, 0, 0, 1));
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "paint") && argc >= 3) { arc_terrain_paint(ter, (float)atof(argv[0]), (float)atof(argv[1]), 6, atoi(argv[2]), 1); G.dirty = true; return true; }
    if (!strcmp(name, "sculpt") && argc >= 3) { arc_terrain_sculpt(ter, (float)atof(argv[0]), (float)atof(argv[1]), 10, (float)atof(argv[2])); G.dirty = true; return true; }
    if (!strcmp(name, "regrow")) { regrow(); return true; }
    if (!strcmp(name, "terrain_colour") && argc >= 1) {   /* 0.10.13 a colour map made here (a game bakes one from its map) */
        static uint8_t px[64 * 64 * 4];
        cmap_kind = !strcmp(argv[0], "red") ? 1 : !strcmp(argv[0], "map") ? 2 : 0;
        for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
            static const uint32_t fields[4] = { 0xE0D070, 0x70B050, 0xC09060, 0x90C0E0 };
            uint32_t c = cmap_kind == 1 ? 0xFF2020 : fields[((x / 16) + (y / 16) * 3) % 4]; uint8_t *q = &px[(y * 64 + x) * 4];
            q[0] = (uint8_t)(c >> 16); q[1] = (uint8_t)(c >> 8); q[2] = (uint8_t)c; q[3] = 255;
        }
        if (cmap_tex.id) arc_texture_destroy(cmap_tex);
        cmap_tex.id = 0; if (cmap_kind) cmap_tex = arc_texture_create_ex(64, 64, px, ARC_TEX_SMOOTH);
        arc_terrain_set_color_map(ter, cmap_tex);   /* copied: cmap_tex could be freed now */
        return true;
    }
    return false;
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC terrain", .width = 1100, .height = 650, .clear_color = 0xB8CCE0, .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command };
}
