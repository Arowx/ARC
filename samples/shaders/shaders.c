/* ARC 0.6 shader showcase: materials (normal maps, metal/roughness, glow maps), point and spot lights, post looks,
   material effects. --set s6.stage=1..4 (4 = everything), s6.look=0..5 (post looks), s6.t=SECONDS freezes the lights. */
#include "arc.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static arc_mesh wall, panel, floor_m, plain; static arc_mesh balls[10];
static arc_texture brick_n, brick_orm, panel_e, brick_c;

/* a UV sphere with texture coordinates (u around, v down from the top) */
static arc_mesh uv_sphere(int seg, int rings, uint32_t rgb) {
    int vc = (seg + 1) * (rings + 1), ic = seg * rings * 6;
    arc_vertex *v = calloc((size_t)vc, sizeof *v); uint32_t *idx = calloc((size_t)ic, 4);
    for (int r = 0; r <= rings; r++) for (int s = 0; s <= seg; s++) {
        float th = (float)r / (float)rings * ARC_PI, ph = (float)s / (float)seg * 2.0f * ARC_PI;
        arc_vertex *o = &v[r * (seg + 1) + s];
        o->nx = sinf(th) * cosf(ph); o->ny = cosf(th); o->nz = -sinf(th) * sinf(ph);
        o->px = o->nx * 0.5f; o->py = o->ny * 0.5f; o->pz = o->nz * 0.5f;
        o->color = arc_vcol(rgb); o->u = (float)s / (float)seg * 2.0f; o->v = (float)r / (float)rings;
    }
    int k = 0;
    for (int r = 0; r < rings; r++) for (int s = 0; s < seg; s++) {
        int a = r * (seg + 1) + s, b = a + seg + 1;
        idx[k++] = a; idx[k++] = b; idx[k++] = a + 1; idx[k++] = a + 1; idx[k++] = b; idx[k++] = b + 1;
    }
    arc_mesh m = arc_mesh_create(v, vc, idx, ic); free(v); free(idx); return m;
}
/* a flat quad facing +z (w x h), uv repeating `rep` times */
static arc_mesh quad(float w, float h, float rep, uint32_t rgb) {
    arc_vertex v[4] = { { -w / 2, h, 0, 0, 0, 1, arc_vcol(rgb), 0, 0 }, { -w / 2, 0, 0, 0, 0, 1, arc_vcol(rgb), 0, rep },
                        { w / 2, 0, 0, 0, 0, 1, arc_vcol(rgb), rep * w / h, rep }, { w / 2, h, 0, 0, 0, 1, arc_vcol(rgb), rep * w / h, 0 } };
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    return arc_mesh_create(v, 4, idx, 6);
}
/* bricks: height map -> colour, normal map (OpenGL: green = up the texture), orm (occlusion in mortar, rough) */
static void make_bricks(void) {
    enum { N = 256 }; static float hgt[N * N]; static uint8_t col[N * N * 4], nrm[N * N * 4], orm[N * N * 4], em[N * N * 4];
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        int row = y / 32, bx = (x + (row & 1) * 32) % 64, by = y % 32;
        float m = (bx < 3 || by < 3) ? 0.0f : 1.0f;
        float e = fminf(fminf((float)bx - 3, 63 - (float)bx), fminf((float)by - 3, 31 - (float)by));
        float h = m * fminf(1.0f, e / 4.0f) * (0.9f + 0.1f * sinf((float)x * 0.7f) * cosf((float)y * 0.5f));
        hgt[y * N + x] = h;
        uint8_t *c = col + (y * N + x) * 4;
        int tone = (x / 64 * 37 + row * 53) % 40;
        if (m > 0) { c[0] = (uint8_t)(150 + tone); c[1] = (uint8_t)(70 + tone / 2); c[2] = 50; } else { c[0] = c[1] = c[2] = 170; }
        c[3] = 255;
    }
    for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
        float dx = hgt[y * N + (x + 1) % N] - hgt[y * N + (x + N - 1) % N], dy = hgt[((y + 1) % N) * N + x] - hgt[((y + N - 1) % N) * N + x];
        float nx = -dx * 2.0f, ny = dy * 2.0f, nz = 1.0f, l = sqrtf(nx * nx + ny * ny + nz * nz);
        uint8_t *o = nrm + (y * N + x) * 4;
        o[0] = (uint8_t)((nx / l * 0.5f + 0.5f) * 255); o[1] = (uint8_t)((ny / l * 0.5f + 0.5f) * 255); o[2] = (uint8_t)((nz / l * 0.5f + 0.5f) * 255); o[3] = 255;
        uint8_t *q = orm + (y * N + x) * 4; float h = hgt[y * N + x];
        q[0] = (uint8_t)(255 * (0.4f + 0.6f * h)); q[1] = (uint8_t)(255 * (h > 0 ? 0.55f : 0.95f)); q[2] = 0; q[3] = 255;
        uint8_t *e = em + (y * N + x) * 4;   /* emissive: glowing stripes */
        bool lit = (y / 16) % 4 == 1 && x % 64 > 6 && x % 64 < 58;
        e[0] = lit ? 40 : 0; e[1] = lit ? 230 : 0; e[2] = lit ? 255 : 0; e[3] = 255;
    }
    brick_c = arc_texture_create_ex(N, N, col, ARC_TEX_MESH); brick_n = arc_texture_create_ex(N, N, nrm, ARC_TEX_MESH);
    brick_orm = arc_texture_create_ex(N, N, orm, ARC_TEX_MESH); panel_e = arc_texture_create_ex(N, N, em, ARC_TEX_MESH);
}
static float stage = 1, spin = 0, tfix = -1, look = 0; static arc_texture grade;
static void teal_orange(float c[3], void *u) { (void)u; float l = c[0] * 0.3f + c[1] * 0.59f + c[2] * 0.11f; float k = l * l * (3 - 2 * l);
    c[0] = c[0] * 0.85f + k * 0.25f; c[1] = c[1] * 0.92f + 0.03f; c[2] = c[2] * 0.8f + (1 - k) * 0.18f; }
static void init(void) {
    arc_var("s6.stage", &stage, 1); arc_var("s6.spin", &spin, 0); arc_var("s6.t", &tfix, -1); arc_var("s6.look", &look, 0); grade = arc_lut_build(teal_orange, NULL, 16);
    make_bricks();
    plain = uv_sphere(32, 16, 0xC8C8C8);
    for (int i = 0; i < 10; i++) {
        balls[i] = uv_sphere(32, 16, i < 5 ? 0xD04030 : 0xE0B060);
        arc_material m = { 0 }; m.pbr = true; m.metallic = i < 5 ? 0.0f : 1.0f; m.roughness = 0.08f + 0.23f * (float)(i % 5);
        arc_mesh_set_material(balls[i], &m);
    }
    wall = quad(4, 2, 1, 0xFFFFFF); arc_mesh_set_texture(wall, brick_c);
    arc_material wm = { 0 }; wm.normal = brick_n; wm.orm = brick_orm; wm.occlusion = 1; wm.pbr = true; wm.metallic = 0; wm.roughness = 1;
    arc_mesh_set_material(wall, &wm);
    panel = quad(1.6f, 1, 1, 0x303438);
    arc_material pm = { 0 }; pm.emissive = panel_e; pm.emissive_rgb[0] = pm.emissive_rgb[1] = pm.emissive_rgb[2] = 2.5f; pm.pbr = true; pm.metallic = 1; pm.roughness = 0.3f;
    arc_mesh_set_material(panel, &pm);
    floor_m = arc_mesh_plane(0x50555C, 8, 0x45494F);
}
static void tick(void) {}
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = { { 0, 2.6f, 6.2f }, { 0, 1.0f, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
    arc_set_camera(&cam);
    bool lights = stage >= 2;
    float t = tfix >= 0 ? tfix : (float)arc_tick_count() / 60.0f + spin;
    if (lights) arc_set_light(arc_v3_make(-0.5f, 0.8f, 0.6f), 0x302C28, 0x0C0E12);
    else arc_set_light(arc_v3_make(-0.5f, 0.8f, 0.6f), 0xF0E8DC, 0x2A2E36);
    if (lights) { arc_set_sky_ex(0x0A1020, 0x182030, 0x080808); }
    else arc_set_sky_ex(0x6A90C0, 0xC8D0D8, 0x30302C);
    if (lights) {   /* three coloured points circling the spheres, a white spot on the wall, a warm point by the panel */
        static const uint32_t cols[3] = { 0xFF4030, 0x30FF60, 0x4080FF };
        for (int i = 0; i < 3; i++) {
            float a = t * 0.7f + (float)i * 2.094f;
            arc_v3 p = arc_v3_make(sinf(a) * 2.6f, 0.9f + 0.4f * sinf(t + (float)i), 0.6f + cosf(a) * 1.3f);
            arc_light_point(p, cols[i], 3.0f, 3.5f);
            arc_draw_mesh_tinted(plain, arc_m4_trs_y(p, 0, arc_v3_make(0.12f, 0.12f, 0.12f)), cols[i], 1, 2, 0);
        }
        arc_v3 sp = arc_v3_make(-1.2f + sinf(t * 0.5f) * 0.8f, 3.2f, 1.2f);
        arc_light_spot(sp, arc_v3_sub(arc_v3_make(-1.2f + sinf(t * 0.5f) * 1.2f, 1.0f, -1.4f), sp), 0xFFF4E0, 4.0f, 7.0f, 0.18f, 0.32f);
        arc_light_point(arc_v3_make(2.6f, 1.2f, -0.4f), 0xFFA040, 2.0f, 2.5f);
    }
    arc_post post = { 0 }; post.bloom = lights ? 0.8f : 0.6f; post.tonemap = true;
    int lk = (int)look;   /* 1 ssao, 2 fxaa, 3 grading, 4 retro (crt, scanlines, pixelate, aberration), 5 all of 1-3 */
    if (lk == 1 || lk == 5) { post.ssao = 0.9f; post.ssao_radius = 0.5f; }
    if (lk == 2 || lk == 5) post.fxaa = true;
    if (lk == 3 || lk == 5) post.lut = grade;
    if (lk == 4) { post.crt_curve = 0.6f; post.scanlines = 0.35f; post.pixelate = 3; post.aberration = 0.6f; post.vignette = 0.4f; }
    arc_set_post(&post);
    arc_set_shadows(arc_v3_make(0, 0, 0), 6, 2048);
    arc_draw_mesh(floor_m, arc_m4_trs_y(arc_v3_make(0, 0, 0), 0, arc_v3_make(14, 1, 14)), 0xFFFFFF);
    bool fx = stage >= 4;   /* 4: material effects */
    for (int i = 0; i < 10; i++) {
        float x = -2.4f + 1.2f * (float)(i % 5), y = i < 5 ? 0.45f : 1.45f;
        arc_m4 M = arc_m4_trs_y(arc_v3_make(x, y, 0.6f), spin, arc_v3_make(0.85f, 0.85f, 0.85f));
        if (!fx) { arc_draw_mesh(balls[i], M, 0xFFFFFF); continue; }
        arc_mesh_style st = { 0 };
        if (i < 4) st.dissolve = 0.2f * (float)(i + 1);                       /* bottom row: burning away */
        if (i == 4) { st.outline = 0.03f; st.outline_rgb = 0x101010; }        /* toon outline */
        if (i == 5 || i == 6) { st.hologram = i == 5 ? 1.0f : 0.6f; st.tint = i == 5 ? 0x40E0FF : 0x60FF80; }
        if (i == 7) { st.dissolve = 0.45f; st.dissolve_rgb = 0x40C0FF; st.dissolve_glow = 6; st.outline = 0.02f; st.outline_rgb = 0x2060FF; }
        if (i == 8) { st.outline = 0.04f; st.outline_rgb = 0xFFD040; }
        arc_draw_mesh_styled(balls[i], M, &st);
    }
    if (fx) { arc_mesh_style ws = { 0 }; ws.scroll_u = 0.25f; arc_draw_mesh_styled(wall, arc_m4_trs_y(arc_v3_make(-1.2f, 0, -1.4f), 0.25f, arc_v3_make(1, 1, 1)), &ws); }
    else arc_draw_mesh(wall, arc_m4_trs_y(arc_v3_make(-1.2f, 0, -1.4f), 0.25f, arc_v3_make(1, 1, 1)), 0xFFFFFF);
    arc_draw_mesh(panel, arc_m4_trs_y(arc_v3_make(2.2f, 0.4f, -1.2f), -0.4f, arc_v3_make(1, 1, 1)), 0xFFFFFF);
    arc_draw_mesh(plain, arc_m4_trs_y(arc_v3_make(3.4f, 0.45f, 1.2f), 0, arc_v3_make(0.85f, 0.85f, 0.85f)), 0xFFFFFF);
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC scene 0.6", .width = 960, .height = 540, .init = init, .tick = tick, .draw = draw, .clear_color = 0x8098B0, .msaa = 4 };
}
