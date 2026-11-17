/* ARC 0.6 post-processing and shadow self-test (run by build.sh test after test_core): one look at a time, saved after
   the frame is drawn; the next look starts once that picture is on disk, so it also runs in a real window on any
   renderer (GL, D3D11: `--frames 600`), where frames are not drawn in step with ticks. 192 x 128 (Windows will not
   make a window narrower than about 120 pixels): sky above, a floor, a box on it. Checks are written for 96 x 64 and
   scaled (S). */
#include "../include/arc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int checks, fails, frame, look_now;   /* look_now: the look being drawn and waited for */
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("ARC test FAIL %s:%d look %d: %s\n", __FILE__, __LINE__, look_now, #c); } } while (0)
enum { W = 192, H = 128 };
#define S(v) ((v) * 2)   /* 96 x 64 coordinates to the test's size */
enum { L_PLAIN, L_SSAO, L_FXAA, L_LUT_ID, L_LUT_INV, L_CRT, L_PIX, L_SCAN, L_ABER, L_NOSH, L_SH1, L_CASC, L_CASC_SOFT, L_NINE, L_PUSH, L_DEPTH, L_FLIP, L_SOFT, L_SOFTP, L_SOFTT, L_DECAL, L_TRAIL, L_CTEX,
       L_LOD, L_STATU, L_STATB, L_CELLC, L_CELLO,   /* 0.7.1 scene speed-ups */
       L_EDIT,                                      /* 0.7.2 the tile editor's 2D view */
       L_ZFAR, L_OSKY, L_OPLAIN, L_OSSAO,           /* 0.7.2 depth precision */
       L_LOBBY,                                     /* 0.8 the multiplayer lobby (UI kit) */
       L_TPOFF, L_TPON, L_TPLAIN, L_TSSAO, L_TNOSH, L_TSH1, L_TCASC,   /* 0.8.2 post effects and cascades in render targets */
       L_UIWRAP, L_UNDER, L_TOP,                    /* 0.8.2 wrapped UI text; 2D over a panel: draw() (under) and draw_top */
       L_AREP, L_AHEX, L_AHEX0, L_AGREP, L_AGHEX, L_ADGREY, L_ADFLOOR, L_ADWALL, L_AUVX,   /* 0.8.4 textures that don't repeat */
       L_SBASE, L_STAMP, L_STAMP0, L_STAMPW, L_DMASK, L_UVM,                                /* 0.8.4 dirt overlays */
       L_LOBBY_ON,                                  /* 0.8.5 the lobby with a relay set (Host online, the room code) */
       L_RPLAIN, L_RZERO, L_RON, L_RRUF, L_RMASK,   /* 0.8.7 planar reflections */
       L_TILEOLD, L_TILENEW, L_PAINT, L_PSKIN,       /* 0.8.8 world-mapped hex base; two-colour paint; posed + styled */
       L_PAINT3,                                     /* 0.8.9 the mask's blue channel: tint3 */
       L_LIGHTS,                                     /* 0.9.5 point and spot lights (plain and pbr) */
       L_MANY,                                       /* 0.9.5 a hundred lights at once (tiled) */
       L_LSH, L_LSH0,                                /* 0.9.5 shadows from a point light and a spot, and the same without */
       L_TERRAIN,                                    /* 0.9.6 terrain: 4 splatted layers from rules, scattered trees and rocks */
       L_TILES,                                      /* 0.9.9 a 16 x 12 tile grid: 2 meshes, 32 layers of one texture array, quarter turns */
       L_MARK, L_MARK0,                              /* 0.9.9 ground markers on a slope at a grazing angle (on_top behind a cube); without them */
       L_FX, L_FX0,                                  /* 0.9.9 world mask (left half unseen), darken, charred, a box left out of the mask; none */
       L_CUT, L_CUTD, L_CUT0,                        /* 0.9.9 a red box behind a wall: cutaway clip, dither, off */
       L_SHOTS, L_SHOTS0,                            /* 0.10.0 a wide tracer, a plasma bolt lighting a wall, a scorch; none */
       L_SDF, L_SDFREF,                              /* 0.10.1 "II" from one 48 px distance-field font at 11, 14, 17.5, 23, 64 px; bitmap bakes at those sizes */
       L_PAT, L_PATLEG,                              /* 0.10.1 paint patterns on cubes and a skinned "mech" (part mask all); the mech with legs only */
       L_VPREF, L_VPFULL, L_VPSPLIT,                 /* 0.10.2 a full scene (sky, cascades, a shadowed point light, reflection, post, a minimap
                                                        target, text); the same in one window-sized viewport; two viewports side by side */
       L_PATARM,                                     /* 0.10.6 stripes on the arms only: the mech, and pads on LeftShoulder, clavicle_r and a game-ruled *Pauldron* joint */
       L_COUNT };
static const arc_camera SCAM = { { 0, 3.0f, 6 }, { 0, 0, -12 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };   /* the shadow looks */
static uint8_t *img[L_COUNT]; static arc_texture lut_id, lut_inv, nine; static arc_mesh cube, ball, twobox;
/* nine-slice: a 9 x 9 texture, each 3 x 3 block its own colour */
static const uint32_t NINE_RGB[9] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x808080, 0x00FFFF, 0xFF00FF, 0xFFFFFF, 0xFF8000 };
static arc_texture sheet, white4; static int v7_spawned = -1;
static const uint32_t SHEET_RGB[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };
static void ct_init(void);
static void v071p_init(void);
static void v071p_draw(int look);
static void v072p_draw(void);
static void v072p_check(void);
static void v072p_tick(void);
static void v080p_tick(void);
static void v080p_draw(void);
static void v080p_check(void);
static void v085p_tick(void);
static void v085p_draw(void);
static void v085p_check(void);
static void v082p_draw(int look);
static void v082p_check(void);
static void v082u_draw(int look);
static void v082u_check(void);
static void v082u_tick(void);
static void v082u_top(float alpha);
static void v084p_draw(int look);
static void v084p_check(void);
static void v084d_draw(int look);
static void v084d_check(void);
static void v087r_draw(int look);
static void v087r_check(void);
static void v088_draw(int look);
static void v088_check(void);
static void v095_draw(int look);
static void v096_init(void); static void v096_draw(int look); static void v096_check(void);
static void v099_init(void); static void v099_draw(int look); static void v099_check(void);
static void v095_check(void);
static arc_mesh v095_pball;
static void invert(float c[3], void *u) { (void)u; for (int i = 0; i < 3; i++) c[i] = 1.0f - c[i]; }

/* 0.7.2: run with --reversed-z (or --depth-mode reversed|infinite) every look is drawn with reversed depth and saved as
   arc_post_rz_N.png (arc_post_inf_N.png); the same checks must pass, and when a standard run's pictures are in the
   folder each look is compared with them */
static const char *look_prefix(int mode) { return mode == ARC_DEPTH_REVERSED ? "arc_post_rz" : mode == ARC_DEPTH_REVERSED_INFINITE ? "arc_post_inf" : "arc_post"; }
static void look_path(char *buf, size_t n, int look) { snprintf(buf, n, "%s_%d.png", look_prefix(arc_depth_mode()), look); }
static void init_files(void) { for (int i = 0; i < L_COUNT; i++) { char p[64]; look_path(p, sizeof p, i); remove(p); } }   /* none left from a run before */
static void init(void) {
    init_files(); v096_init(); v099_init();
    lut_id = arc_lut_build(NULL, NULL, 16); lut_inv = arc_lut_build(invert, NULL, 16);
    uint8_t px9[9 * 9 * 4];
    for (int y = 0; y < 9; y++) for (int x = 0; x < 9; x++) {
        uint32_t c = NINE_RGB[(y / 3) * 3 + x / 3]; uint8_t *p = px9 + (y * 9 + x) * 4;
        p[0] = (uint8_t)(c >> 16); p[1] = (uint8_t)(c >> 8); p[2] = (uint8_t)c; p[3] = 255;
    }
    nine = arc_texture_create(9, 9, px9, false);
    cube = arc_mesh_cube(0xFFFFFF); ball = arc_mesh_sphere(0xFFFFFF, 16);
    { v095_pball = arc_mesh_sphere(0xFFFFFF, 16); arc_material m; memset(&m, 0, sizeof m); m.pbr = true; m.metallic = 0.3f; m.roughness = 0.35f; arc_mesh_set_material(v095_pball, &m); }
    arc_mesh_builder *b = arc_mb_begin(); arc_mb_color(b, 0xA0A0A0);   /* one mesh, its front box hiding part of its back one */
    arc_mb_box(b, arc_v3_make(-0.2f, -0.2f, 0.2f), arc_v3_make(0.2f, 0.2f, 0.4f)); arc_mb_box(b, arc_v3_make(-0.35f, -0.35f, -0.4f), arc_v3_make(0.35f, 0.35f, -0.2f));
    twobox = arc_mb_finish(b, 0, 0);
    uint8_t sp[4 * 4 * 4];   /* 2 x 2 frames of 2 x 2 pixels: red, green / blue, yellow */
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) { uint32_t c = SHEET_RGB[(y / 2) * 2 + x / 2]; uint8_t *q = sp + (y * 4 + x) * 4; q[0] = (uint8_t)(c >> 16); q[1] = (uint8_t)(c >> 8); q[2] = (uint8_t)c; q[3] = 255; }
    sheet = arc_texture_create(4, 4, sp, false);
    uint8_t wp[16]; memset(wp, 255, sizeof wp); white4 = arc_texture_create(2, 2, wp, false);
    v071p_init();
    ct_init();
}
static const uint8_t *px(int look, int x, int y) { return img[look] + ((size_t)y * W + x) * 4; }
/* ---- 0.7.1 compressed textures: each 8 x 8 test texture (2 x 2 hand-made blocks) drawn as uploaded (blocks: decoded by
   the GPU, or by ARC on the CPU where the GPU can't, and in the software renderer) and as RGBA8 made from ARC's CPU
   decoder; both must show the same texels, and the values worked out by hand. Then three lit quads: a BC5 normal map
   (Z rebuilt in the shader on the GPU path), the same normal as RGBA8, and no normal map. ---- */
enum { CT_N = 5 };   /* BC1, BC3, BC5, BC7, BC1 with the GPU path off for this one texture (as RGBA8) */
static arc_texture ct_c[CT_N], ct_u[CT_N], ct_nc, ct_nu; static arc_mesh ct_q1, ct_q2, ct_q3; static int ct_fmt[CT_N];
static const uint8_t CT_BC1[4][8] = {   /* 4-colour red..blue; 3-colour green/white with cut-outs; grey; red..blue again */
    { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x1B, 0x50, 0xFA }, { 0xE0, 0x07, 0xFF, 0xFF, 0xE4, 0x1B, 0x50, 0xFA },
    { 0x10, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x1B, 0x50, 0xFA } };
static const uint8_t CT_A8[8] = { 245, 0, 0x88, 0xC6, 0xFA, 0x00, 0x00, 0x00 };   /* 8-level: rows 0-1 codes 0..7 */
static const uint8_t CT_A6[8] = { 0, 250, 0x88, 0xC6, 0xFA, 0x00, 0x00, 0x00 };   /* 6-level: 0, 250, 50, 100, 150, 200, 0, 255 */
static const uint8_t CT_BC7_M6[16] = { 0x40, 0xC0, 0x1F, 0xF0, 0x07, 0x00, 0xFE, 0x7F, 0x11, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE };
static const uint8_t CT_BC7_M1[16] = { 0x8A, 0x4A, 0xE5, 0xF1, 0xA9, 0x41, 0x06, 0xA0, 0x95, 0x6A, 0x26, 0xAF, 0xBC, 0xCD, 0xAF, 0xE5 };
static size_t ct_arct(uint8_t *out, int fmt, int w, int h, const uint8_t *blocks, size_t n) {
    uint32_t hd[7] = { 1, (uint32_t)fmt, (uint32_t)w, (uint32_t)h, 1, 0, (uint32_t)n };
    memcpy(out, "ARCT", 4); memcpy(out + 4, hd, sizeof hd); memcpy(out + 32, blocks, n); return 32 + n;
}
static arc_mesh ct_quad(void) {   /* 1 x 1, facing +z, u along +x, v down */
    arc_vertex v[4]; memset(v, 0, sizeof v);
    for (int i = 0; i < 4; i++) { v[i].px = (i == 1 || i == 2) ? 0.5f : -0.5f; v[i].py = i >= 2 ? -0.5f : 0.5f; v[i].nz = 1; v[i].color = 0xFFFFFFFFu; v[i].u = v[i].px + 0.5f; v[i].v = 0.5f - v[i].py; }
    uint32_t idx[6] = { 0, 2, 1, 0, 3, 2 };
    return arc_mesh_create(v, 4, idx, 6);
}
static void ct_init(void) {
    uint8_t blk[4][16], file[32 + 64], rgba[8 * 8 * 4]; int fmts[CT_N] = { ARC_TEXFMT_BC1, ARC_TEXFMT_BC3, ARC_TEXFMT_BC5, ARC_TEXFMT_BC7, ARC_TEXFMT_BC1 };
    for (int t = 0; t < CT_N; t++) {
        int f = fmts[t]; size_t bb = f == ARC_TEXFMT_BC1 ? 8 : 16;
        for (int b = 0; b < 4; b++) {
            if (f == ARC_TEXFMT_BC1) memcpy(blk[b], CT_BC1[b], 8);
            if (f == ARC_TEXFMT_BC3) { memcpy(blk[b], b & 1 ? CT_A6 : CT_A8, 8); memcpy(blk[b] + 8, CT_BC1[b == 1 ? 0 : b], 8); }   /* (BC3 colour: 4-colour) */
            if (f == ARC_TEXFMT_BC5) { memcpy(blk[b], b & 1 ? CT_A6 : CT_A8, 8); memcpy(blk[b] + 8, b & 2 ? CT_A8 : CT_A6, 8); }
            if (f == ARC_TEXFMT_BC7) memcpy(blk[b], b == 1 || b == 2 ? CT_BC7_M1 : CT_BC7_M6, 16);
        }
        uint8_t packed[64]; for (int b = 0; b < 4; b++) memcpy(packed + b * bb, blk[b], bb);
        size_t n = ct_arct(file, f, 8, 8, packed, bb * 4);
        int w = 0, h = 0; uint8_t *dec = arc_image_decode(file, n, &w, &h);   /* ARC's CPU decoder */
        CHECK(dec && w == 8 && h == 8);
        if (!dec) continue;
        memcpy(rgba, dec, sizeof rgba); free(dec);
        ct_u[t] = arc_texture_create_ex(8, 8, rgba, 0);
        if (t == CT_N - 1) { ct_c[t] = arc_texture_create_ex(8, 8, rgba, 0); ct_fmt[t] = ARC_TEXFMT_RGBA8; }
        else { ct_c[t] = arc_texture_from_memory(file, n, 0); ct_fmt[t] = f; }
        CHECK(ct_c[t].id && ct_u[t].id);
        bool direct = arc_texture_format_supported(f) && t != CT_N - 1;          /* the GPU path when the GPU takes it */
        CHECK(arc_texture_format(ct_c[t]) == (direct ? f : ARC_TEXFMT_RGBA8));
        printf("ARC note compressed format %d: %s\n", f, direct ? "uploaded as blocks" : "decoded on the CPU");
    }
    /* the normal map: X = 0.6 (204), Y = 0 (128); as RGBA8 with Z = 0.8 (229) */
    uint8_t nb[64], nf[32 + 64], nx[8] = { 204, 0, 0, 0, 0, 0, 0, 0 }, ny[8] = { 128, 0, 0, 0, 0, 0, 0, 0 };
    for (int b = 0; b < 4; b++) { memcpy(nb + b * 16, nx, 8); memcpy(nb + b * 16 + 8, ny, 8); }
    size_t nn = ct_arct(nf, ARC_TEXFMT_BC5, 8, 8, nb, 64);
    ct_nc = arc_texture_from_memory(nf, nn, ARC_TEX_MESH);
    for (int i = 0; i < 64; i++) { rgba[i * 4] = 204; rgba[i * 4 + 1] = 128; rgba[i * 4 + 2] = 229; rgba[i * 4 + 3] = 255; }
    ct_nu = arc_texture_create_ex(8, 8, rgba, ARC_TEX_MESH);
    ct_q1 = ct_quad(); ct_q2 = ct_quad(); ct_q3 = ct_quad();
    arc_material m; memset(&m, 0, sizeof m); m.normal_scale = 1;
    m.normal = ct_nc; arc_mesh_set_material(ct_q1, &m);
    m.normal = ct_nu; arc_mesh_set_material(ct_q2, &m);
}
static void ct_draw(void) {
    arc_set_post(NULL); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0); arc_set_sky_ex(0x000000, 0x000000, 0x000000);
    for (int t = 0; t < CT_N; t++) {   /* 8 x 8 texels at 4 x 4 pixels: compressed at y 4, RGBA8 at y 40 */
        arc_draw_texture_ex(ct_c[t], 0, 0, 8, 8, 4.0f + 38.0f * (float)t, 4, 32, 32, 0, 0, 0, ARC_WHITE);
        arc_draw_texture_ex(ct_u[t], 0, 0, 8, 8, 4.0f + 38.0f * (float)t, 40, 32, 32, 0, 0, 0, ARC_WHITE);
    }
    arc_camera cam = { { 0, 0, 6 }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 4.0f, 0.1f, 100 };   /* orthographic: 4 units over 128 px */
    arc_set_camera(&cam); arc_set_light(arc_v3_make(0.8f, 0, 0.6f), 0xA0A0A0, 0x101010); arc_set_rim(0, 3); arc_set_fog(0, 0, 0);
    arc_v3 s = arc_v3_make(1, 1, 1);
    arc_draw_mesh(ct_q1, arc_m4_trs_y(arc_v3_make(-1.5f, -1.4f, 0), 0, s), 0xFFFFFF);
    arc_draw_mesh(ct_q2, arc_m4_trs_y(arc_v3_make(0, -1.4f, 0), 0, s), 0xFFFFFF);
    arc_draw_mesh(ct_q3, arc_m4_trs_y(arc_v3_make(1.5f, -1.4f, 0), 0, s), 0xFFFFFF);
}
/* expected texels, worked out by hand from the blocks above (src/arc_texc.c's formulas); alpha blends over the
   background (bg), as the 2D blend does: c * a + bg * (1 - a) */
static const uint8_t *ct_px(int t, int tx, int ty, bool rgba8) { return px(L_CTEX, 4 + 38 * t + tx * 4 + 2, (rgba8 ? 40 : 4) + ty * 4 + 2); }
static bool ct_is(int t, int tx, int ty, bool u, int r, int g, int b, int a, int tol) {
    const uint8_t *p = ct_px(t, tx, ty, u), *bg = px(L_CTEX, 1, 1); int c[3] = { r, g, b };
    if (!u && tol == 1) tol = 2;   /* the GPU's own decoding of in-between values: vendors round differently (an RX 6900: 2 off) */
    for (int k = 0; k < 3; k++) { int e = (c[k] * a + bg[k] * (255 - a) + 127) / 255; if (c[k] >= 0 && abs(p[k] - e) > tol) return false; }
    return true;
}
static void check_ctex(void) {
    int worst = 0;
    for (int t = 0; t < CT_N; t++)
        for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
            const uint8_t *a = ct_px(t, tx, ty, false), *b = ct_px(t, tx, ty, true);
            for (int k = 0; k < (ct_fmt[t] == ARC_TEXFMT_BC5 ? 2 : 3); k++) {   /* BC5: blue is Z (CPU) or 0 (GPU) */
                int d = abs(a[k] - b[k]); if (d > worst) worst = d;
                if (d > 2) printf("ARC note compressed texel: format %d texel %d,%d channel %d: GPU %d, ARC's decoder %d\n", ct_fmt[t], tx, ty, k, a[k], b[k]);
            }
        }
    CHECK(worst <= 2);   /* GPUs interpolate in-between values their own way: within 2 of ARC's decoder (an RX 6900 differs by
                            exactly 2 on BC1/BC3 colour and BC5; endpoints exact everywhere) */
    for (int k = 0; k < 2; k++) {   /* both rows: as uploaded, and ARC's decoder as RGBA8 */
        bool u = k == 1;
        CHECK(ct_is(0, 0, 0, u, 255, 0, 0, 255, 0) && ct_is(0, 1, 0, u, 0, 0, 255, 255, 0));            /* BC1 4-colour */
        CHECK(ct_is(0, 2, 0, u, 170, 0, 85, 255, 1) && ct_is(0, 3, 0, u, 85, 0, 170, 255, 1));
        CHECK(ct_is(0, 0, 2, u, 255, 0, 0, 255, 0) && ct_is(0, 3, 3, u, 85, 0, 170, 255, 1) && ct_is(0, 0, 1, u, 85, 0, 170, 255, 1));
        CHECK(ct_is(0, 4, 0, u, 0, 255, 0, 255, 0) && ct_is(0, 5, 0, u, 255, 255, 255, 255, 0));        /* 3-colour */
        CHECK(ct_is(0, 6, 0, u, 128, 255, 128, 255, 1) && ct_is(0, 7, 0, u, 0, 0, 0, 0, 1));           /* index 3: cut out */
        CHECK(ct_is(0, 1, 5, u, 132, 130, 132, 255, 0));                                                /* 565 -> 888 */
        CHECK(ct_is(1, 0, 0, u, 255, 0, 0, 245, 1) && ct_is(1, 1, 0, u, 0, 0, 255, 0, 1));             /* BC3: 8-level alpha */
        CHECK(ct_is(1, 2, 0, u, 170, 0, 85, 210, 1) && ct_is(1, 3, 0, u, 85, 0, 170, 175, 1) && ct_is(1, 3, 1, u, 255, 0, 0, 35, 1));
        CHECK(ct_is(1, 4, 0, u, 255, 0, 0, 0, 1) && ct_is(1, 5, 0, u, 0, 0, 255, 250, 1));             /* 6-level alpha */
        CHECK(ct_is(1, 6, 0, u, 170, 0, 85, 50, 1) && ct_is(1, 6, 1, u, 0, 0, 255, 0, 1) && ct_is(1, 7, 1, u, 255, 0, 0, 255, 1));
        CHECK(ct_is(2, 1, 0, u, 0, 250, -1, 255, 0) && ct_is(2, 2, 1, u, 70, 0, -1, 255, 1));          /* BC5 (red X, green Y) */
        CHECK(ct_is(2, 3, 0, u, 175, 100, -1, 255, 1) && ct_is(2, 7, 1, u, 255, 255, -1, 255, 0) && ct_is(2, 6, 1, u, 0, 0, -1, 255, 0));
        CHECK(ct_is(2, 1, 4, u, 0, 0, -1, 255, 0) && ct_is(2, 5, 4, u, 250, 0, -1, 255, 0));          /* X 8 / Y 8, X 6 / Y 8 */
        CHECK(ct_is(3, 0, 0, u, 0, 0, 0, 254, 1) && ct_is(3, 1, 0, u, 16, 16, 0, 254, 1));            /* BC7 mode 6 */
        CHECK(ct_is(3, 0, 2, u, 135, 135, 1, 255, 0) && ct_is(3, 3, 3, u, 255, 255, 1, 255, 0));
        CHECK(ct_is(3, 4, 0, u, 46, 145, 123, 255, 0) && ct_is(3, 7, 2, u, 243, 6, 106, 255, 0));     /* mode 1 (as Pillow decodes it) */
        CHECK(ct_is(3, 5, 3, u, 59, 106, 112, 255, 0) && ct_is(3, 2, 4, u, 78, 44, 94, 255, 0));
        CHECK(ct_is(4, 2, 0, u, 170, 0, 85, 255, 1));
    }
    const uint8_t *q1 = px(L_CTEX, W / 2 - 48, 64 + 45), *q2 = px(L_CTEX, W / 2, 64 + 45), *q3 = px(L_CTEX, W / 2 + 48, 64 + 45);
    CHECK(abs(q1[0] - q2[0]) <= 3 && abs(q1[1] - q2[1]) <= 3 && abs(q1[2] - q2[2]) <= 3);   /* BC5 normal = RGBA8 normal */
    CHECK(q2[0] > q3[0] + 30);                                                              /* ...and it bends the light */
    printf("ARC note compressed normal map: %d %d (rgba8) %d (none)\n", q1[0], q2[0], q3[0]);
}
static int diff(int a, int b, int x, int y) { int m = 0; for (int k = 0; k < 3; k++) { int d = abs(px(a, x, y)[k] - px(b, x, y)[k]); if (d > m) m = d; } return m; }
static int lum8(int look, int x, int y) { const uint8_t *p = px(look, x, y); return (p[0] * 3 + p[1] * 6 + p[2]) / 10; }
static int area(int look, arc_v3 wp) {   /* 3x3 mean brightness around a world point */
    arc_v2 sp; if (!arc_camera_to_screen(&SCAM, W, H, wp, &sp)) return -1;
    int x0 = (int)sp.x, y0 = (int)sp.y, sum = 0;
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) sum += lum8(look, arc_clampi(x0 + i, 0, W - 1), arc_clampi(y0 + j, 0, H - 1));
    return sum / 9;
}
static void check_shadows(void) {
    arc_v3 nearp = arc_v3_make(-0.75f, 0, 0), farp = arc_v3_make(-2.6f, 0, -16);   /* ground in each box's shadow */
    int nn = area(L_NOSH, nearp), n1 = area(L_SH1, nearp), nc = area(L_CASC, nearp);
    int fn = area(L_NOSH, farp), f1 = area(L_SH1, farp), fc = area(L_CASC, farp);
    CHECK(n1 < nn - 15 && nc < nn - 15);          /* near: both shadow it */
    CHECK(abs(f1 - fn) <= 2);                     /* far: outside the one box */
    CHECK(fc < fn - 15);                          /* ...but inside the cascades */
    int mid_h = 0, mid_s = 0;                     /* softness: more pixels part way between lit and shadowed */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int l = lum8(L_NOSH, x, y), a = lum8(L_CASC, x, y), b = lum8(L_CASC_SOFT, x, y);
        if (l < 30) continue;
        if (a < l - 4 && a > l - 25) mid_h++;
        if (b < l - 4 && b > l - 25) mid_s++;
    }
    CHECK(mid_s > mid_h + 10);
}
static bool near_rgb(int look, int x, int y, uint32_t c) {
    const uint8_t *p = px(look, x, y);
    return abs(p[0] - (int)(c >> 16)) <= 8 && abs(p[1] - (int)((c >> 8) & 255)) <= 8 && abs(p[2] - (int)(c & 255)) <= 8;
}
static void check_nine(void) {   /* drawn at (8, 8) 80 x 48, corners 12 px (in pixels, not scaled): each region its block's colour */
    CHECK(near_rgb(L_NINE, 12, 12, NINE_RGB[0]) && near_rgb(L_NINE, 48, 12, NINE_RGB[1]) && near_rgb(L_NINE, 83, 12, NINE_RGB[2]));
    CHECK(near_rgb(L_NINE, 12, 32, NINE_RGB[3]) && near_rgb(L_NINE, 48, 32, NINE_RGB[4]) && near_rgb(L_NINE, 83, 32, NINE_RGB[5]));
    CHECK(near_rgb(L_NINE, 12, 51, NINE_RGB[6]) && near_rgb(L_NINE, 48, 51, NINE_RGB[7]) && near_rgb(L_NINE, 83, 51, NINE_RGB[8]));
    CHECK(near_rgb(L_NINE, 19, 19, NINE_RGB[0]) && near_rgb(L_NINE, 21, 21, NINE_RGB[4]));   /* the corner is exactly 12 px */
    CHECK(!near_rgb(L_NINE, 4, 4, NINE_RGB[0]) && !near_rgb(L_NINE, 92, 30, NINE_RGB[5]));   /* nothing outside the rect */
}
/* line depth push: a green line in front of a red box, pushed behind it, vanishes over the box but not beside it,
   and stays on the row it would be on without the push */
static const arc_camera PCAM = { { 0, 0, 6 }, { 0, 0, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
static void check_push(void) {
    arc_v2 mid, side;
    CHECK(arc_camera_to_screen(&PCAM, W, H, arc_v3_make(0, 0.3f, 1.3f), &mid) && arc_camera_to_screen(&PCAM, W, H, arc_v3_make(1.9f, 0.3f, 1.3f), &side));
    int y = (int)(mid.y), xs = (int)side.x, xm = (int)mid.x;
    const uint8_t *c = px(L_PUSH, xm, y), *g = px(L_PUSH, xs, y);
    CHECK(!(c[1] > 200 && c[0] < 60));          /* over the box: the box, not the line */
    CHECK(g[1] > 200 && g[0] < 60 && g[2] < 60); /* beside it: the line, on the same row as unpushed */
}
/* 0.6.3 depth and order: a red box (front face z = 1) seen from PCAM; see draw() for what sits where */
static const uint8_t *dpx(arc_v3 wp) {
    arc_v2 sp; if (!arc_camera_to_screen(&PCAM, W, H, wp, &sp)) return px(L_DEPTH, 0, 0);
    return px(L_DEPTH, arc_clampi((int)sp.x, 0, W - 1), arc_clampi((int)sp.y, 0, H - 1));
}
#define RED(p) ((p)[0] > 90 && (p)[1] < 60 && (p)[2] < 60)
static void check_depth(void) {
    const uint8_t *a = dpx(arc_v3_make(-0.5f, 0.5f, 1)), *b = dpx(arc_v3_make(0.5f, 0.5f, 1));
    CHECK(a[1] > 90 && a[0] < 60);                       /* on top: the green cube behind the box shows over it */
    CHECK(b[2] > 90 && b[0] < 60);                       /* hidden: the blue cube behind the box shows where covered */
    const uint8_t *h = dpx(arc_v3_make(2.3f, 0.6f, 0));
    CHECK(h[0] < 20 && h[1] < 20 && h[2] < 20);          /* ...and nowhere it is not covered */
    const uint8_t *y = dpx(arc_v3_make(-0.5f, -0.5f, 1)), *m = dpx(arc_v3_make(0.5f, -0.5f, 1));
    CHECK(y[0] > 90 && y[1] > 90 && y[2] < 60);          /* pulled: the yellow plate in the box's face wins */
    CHECK(RED(m));                                       /* pushed: the magenta one loses */
    const uint8_t *o = dpx(arc_v3_make(-2.3f, 0.6f, 0));
    CHECK(o[1] > 90 && o[2] > 90 && o[0] < 60);          /* order: cyan (order 2) over orange (order 1), though drawn first */
    const uint8_t *t = dpx(arc_v3_make(2.3f, -0.6f, 1.5f)), *g = dpx(arc_v3_make(-2.3f, -0.6f, 1.5f));
    CHECK(t[0] > 140 && t[1] > 140 && t[2] > 140);       /* test-only plate: the white cube behind it shows */
    CHECK(g[0] < 140 && g[0] > 40 && abs(g[0] - g[2]) < 12);   /* the usual plate hides it */
    const uint8_t *x = dpx(arc_v3_make(0, 0, 1)), *r = dpx(arc_v3_make(0, 0.85f, 1));
    CHECK(RED(r) && x[1] > r[1] + 12 && x[2] > r[2] + 12);   /* x-ray: the hidden ball glows teal through the box */
    const uint8_t *s1 = dpx(arc_v3_make(-1.6f, 1.75f, 0.4f)), *s2 = dpx(arc_v3_make(1.6f, 1.75f, 0.4f));
    CHECK(s1[0] > 60 && abs(s1[0] - s2[0]) < 10 && abs(s1[1] - s2[1]) < 10 && abs(s1[2] - s2[2]) < 10);   /* ...but never through itself */
}
static void check_plain(void) {   /* 0.6.5: the fill shows pure green; the textured quads either side still show the texture */
    const uint8_t *g = px(L_DEPTH, 19, 7), *a = px(L_DEPTH, 7, 7), *b = px(L_DEPTH, 31, 7);   /* screen pixels */
    CHECK(g[0] < 8 && g[1] > 247 && g[2] < 8);
    CHECK(abs(a[0] - b[0]) < 4 && abs(a[1] - b[1]) < 4 && abs(a[2] - b[2]) < 4 && !(a[0] < 8 && a[1] > 247));
}
/* ---- 0.7 particles, trails, decals ---- */
static const arc_camera FCAM = { { 0, 0, 6 }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 4.0f, 0.1f, 100 };   /* orthographic, 4 units tall */
static const arc_fx FX_FLIP = { .life = 1000, .size = 0.4f, .alpha = 1, .alpha_end = 1, .frames_x = 2, .frames_y = 2, .frame_random = true };
static const arc_fx FX_SOFT = { .life = 1000, .size = 0.6f, .alpha = 1, .alpha_end = 1, .soft = 0.6f };
static const arc_fx FX_HARD = { .life = 1000, .size = 0.6f, .alpha = 1, .alpha_end = 1 };
static const arc_camera DCAM = { { 0, 2.5f, 6 }, { 0, -0.5f, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
static const arc_camera SOCAM = { { 0, 0, 6 }, { 0, 0, 0 }, { 0, 1, 0 }, 0.6f, 0, 0.1f, 100 };
static void v7_particles(int look) {   /* spawned once, the first time a look that needs them is drawn */
    if (v7_spawned == look) return;
    v7_spawned = look; arc_particles_clear();
    arc_fx f = FX_FLIP; f.texture = sheet;
    if (look == L_FLIP) for (int i = 0; i < 8; i++) arc_fx_burst(&f, arc_v3_make(-2.45f + 0.7f * (float)i, 0, 0), arc_v3_make(0, 1, 0), 1);
    arc_fx s = FX_SOFT, h = FX_HARD; s.texture = h.texture = white4;
    if (look == L_SOFT || look == L_SOFTP || look == L_SOFTT) {   /* 0.15 in front of the wall (z = 0): soft, hard; soft again with only sky behind */
        arc_fx_burst(&s, arc_v3_make(-0.8f, -0.3f, 0.15f), arc_v3_make(0, 1, 0), 1);
        arc_fx_burst(&h, arc_v3_make(0.8f, -0.3f, 0.15f), arc_v3_make(0, 1, 0), 1);
        arc_fx_burst(&s, arc_v3_make(0, 1.2f, 0.15f), arc_v3_make(0, 1, 0), 1);
    }
}
static const uint8_t *cpx(int look, const arc_camera *c, arc_v3 wp) {
    arc_v2 sp; if (!arc_camera_to_screen(c, W, H, wp, &sp)) return px(look, 0, 0);
    return px(look, arc_clampi((int)sp.x, 0, W - 1), arc_clampi((int)sp.y, 0, H - 1));
}
static int which_rgb(const uint8_t *p) { for (int i = 0; i < 4; i++) { uint32_t c = SHEET_RGB[i]; if (abs(p[0] - (int)(c >> 16)) < 40 && abs(p[1] - (int)((c >> 8) & 255)) < 40 && abs(p[2] - (int)(c & 255)) < 40) return i; } return -1; }
static void check_v07(void) {
    /* flipbook: each particle shows exactly one frame of the sheet (all of its quad one colour), and the random start
       frames differ */
    int seen = 0, whole = 1;
    for (int i = 0; i < 8; i++) {
        float x = -2.45f + 0.7f * (float)i; int c0 = which_rgb(cpx(L_FLIP, &FCAM, arc_v3_make(x, 0, 0)));
        for (int k = 0; k < 4; k++) if (which_rgb(cpx(L_FLIP, &FCAM, arc_v3_make(x + (k & 1 ? 0.12f : -0.12f), k & 2 ? 0.12f : -0.12f, 0))) != c0) whole = 0;
        if (c0 < 0) whole = 0; else seen |= 1 << c0;
    }
    CHECK(whole); CHECK(seen != 1 && seen != 2 && seen != 4 && seen != 8);
    /* soft: 0.15 in front of the wall with soft 0.6 lets about a quarter through; the hard one is solid; with sky
       behind, soft changes nothing */
    for (int l = L_SOFT; l <= L_SOFTT; l++) {
        const uint8_t *a = cpx(l, &SOCAM, arc_v3_make(-0.8f, -0.3f, 0.15f)), *b = cpx(l, &SOCAM, arc_v3_make(0.8f, -0.3f, 0.15f)), *c = cpx(l, &SOCAM, arc_v3_make(0, 1.2f, 0.15f));
        CHECK(a[0] > 40 && a[0] < 110); CHECK(b[0] > 245); CHECK(c[0] > 245);
        const uint8_t *e = cpx(l, &SOCAM, arc_v3_make(-0.8f, -0.05f, 0.15f));   /* toward its top edge: still the same fade */
        CHECK(abs(e[0] - a[0]) < 16);
    }
    /* decals on a floor and a wall: the mark's colour all over its middle (no z-fighting speckle), nothing around it */
    for (int k = -2; k <= 2; k++) {
        const uint8_t *f = cpx(L_DECAL, &DCAM, arc_v3_make(-1.0f + 0.1f * (float)k, -1, 0.5f + 0.1f * (float)k));
        const uint8_t *w = cpx(L_DECAL, &DCAM, arc_v3_make(1.0f + 0.1f * (float)k, 0.2f + 0.1f * (float)k, -1));
        CHECK(f[0] > 2 * f[1] + 30 && f[0] > 2 * f[2] + 30);   /* red on the grey floor */
        CHECK(w[2] > 2 * w[0] + 30 && w[2] > 2 * w[1] + 30);   /* blue on the grey wall */
    }
    const uint8_t *g0 = cpx(L_DECAL, &DCAM, arc_v3_make(-2.2f, -1, 0.5f)), *g1 = cpx(L_DECAL, &DCAM, arc_v3_make(2.2f, 0.2f, -1));
    CHECK(abs(g0[0] - g0[2]) < 12 && abs(g1[0] - g1[2]) < 12);   /* grey beside them */
    /* trail: green along the path, brighter at the head than toward the tail, black off it */
    const uint8_t *hd = cpx(L_TRAIL, &FCAM, arc_v3_make(2.4f, 0, 0)), *tl = cpx(L_TRAIL, &FCAM, arc_v3_make(-1.6f, 0, 0)), *off = cpx(L_TRAIL, &FCAM, arc_v3_make(0, 1.0f, 0));
    CHECK(hd[1] > 150 && hd[0] < 40); CHECK(tl[1] > 15 && tl[1] < hd[1] - 40); CHECK(off[1] < 10);
}
/* 0.7.1: LOD (red base, green beyond 10, blue under a tenth of the view), a static batch and its single draws, cells */
static arc_mesh v071p_red, v071p_green, v071p_blue, v071p_tile, v071p_tex; static arc_static v071p_st;
static const arc_camera LCAM = { { 0, 1.5f, 6 }, { 0, 0, -10 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
static const arc_camera TCAM = { { 0, 5, 4.5f }, { 0, 0, 0 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 100 };
static const arc_camera CCAM = { { 1, 1.5f, 2 }, { 10, 1.5f, 2 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 100 };
static arc_mesh v071p_floor_mesh(int x, int z) { return (x + z) % 4 == 0 ? v071p_tex : v071p_tile; }
static arc_m4 v071p_floor_at(int x, int z) { return arc_m4_trs_y(arc_v3_make((float)x - 3.5f, 0, (float)z - 3.5f), 0.08f * (float)z, arc_v3_make((x & 1) ? -0.7f : 0.7f, 1, 0.7f)); }
static uint32_t v071p_tint(int x, int z) { return 0xC00000u >> (8 * ((x + 2 * z) % 3)) | 0x303030u; }
static void v071p_init(void) {
    v071p_red = arc_mesh_cube(0xFF0000); v071p_green = arc_mesh_cube(0x00FF00); v071p_blue = arc_mesh_cube(0x0000FF);
    arc_mesh_set_lod_distance(v071p_red, 1, v071p_green, 10); arc_mesh_set_lod(v071p_red, 2, v071p_blue, 0.1f);
    arc_mesh_builder *b = arc_mb_begin(); arc_mb_color(b, 0xFFFFFF); arc_mb_box(b, arc_v3_make(-0.5f, -0.05f, -0.5f), arc_v3_make(0.5f, 0, 0.5f)); v071p_tile = arc_mb_finish(b, 0, 0);
    b = arc_mb_begin(); arc_mb_color(b, 0xE0E0E0); arc_mb_box(b, arc_v3_make(-0.5f, -0.05f, -0.5f), arc_v3_make(0.5f, 0, 0.5f)); v071p_tex = arc_mb_finish(b, 0, 0);
    uint8_t tx[4 * 4 * 4]; for (int i = 0; i < 16; i++) { tx[i * 4] = (uint8_t)(i * 16); tx[i * 4 + 1] = 220; tx[i * 4 + 2] = (uint8_t)(255 - i * 16); tx[i * 4 + 3] = 255; }
    arc_mesh_set_texture(v071p_tex, arc_texture_create(4, 4, tx, false));
    v071p_st = arc_static_create();
    for (int z = 0; z < 8; z++) for (int x = 0; x < 8; x++) arc_static_add(v071p_st, v071p_floor_mesh(x, z), v071p_floor_at(x, z), v071p_tint(x, z));
    arc_static_add(v071p_st, cube, arc_m4_trs_y(arc_v3_make(0.3f, 0.5f, 0.2f), 0.6f, arc_v3_make(1, 1, 1)), 0xD0D0D0);   /* casts a shadow */
    arc_static_build(v071p_st);
}
static void v071p_rooms(bool open) {
    arc_cells_clear();
    for (int i = 0; i < 3; i++) arc_cell_add(1 + i, arc_v3_make(4.0f * (float)i, 0, 0), arc_v3_make(4.0f * (float)i + 4, 3, 4));
    arc_portal_add(1, 2, arc_v3_make(4, 0, 1.5f), arc_v3_make(4, 2.5f, 2.5f));
    int d = arc_portal_add(2, 3, arc_v3_make(8, 0, 1.5f), arc_v3_make(8, 2.5f, 2.5f));
    arc_portal_set_open(d, open);
}
static void v071p_draw(int look) {
    arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadow_cascades(1, 0); arc_set_shadow_softness(0);
    if (look == L_LOD) {
        arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0); arc_set_camera(&LCAM); arc_set_light(arc_v3_make(0.2f, 0.6f, 1.0f), 0xFFFFFF, 0x303030);
        arc_draw_mesh(v071p_red, arc_m4_translate(arc_v3_make(-1.5f, 0, 0)), 0xFFFFFF);
        arc_draw_mesh(v071p_red, arc_m4_trs_y(arc_v3_make(1.5f, 0, -8), 0, arc_v3_make(2, 2, 2)), 0xFFFFFF);
        arc_draw_mesh(v071p_red, arc_m4_trs_y(arc_v3_make(-8, 0, -40), 0, arc_v3_make(2, 2, 2)), 0xFFFFFF);
    } else if (look == L_STATU || look == L_STATB) {
        arc_set_camera(&TCAM); arc_set_light(arc_v3_make(0.5f, 1.0f, 0.3f), 0xD0D0D0, 0x404040);
        arc_set_shadows(arc_v3_make(0, 0, 0), 5, 1024); arc_set_shadow_strength(0.7f);
        if (look == L_STATB) arc_static_draw(v071p_st);
        else {
            for (int z = 0; z < 8; z++) for (int x = 0; x < 8; x++) arc_draw_mesh(v071p_floor_mesh(x, z), v071p_floor_at(x, z), v071p_tint(x, z));
            arc_draw_mesh(cube, arc_m4_trs_y(arc_v3_make(0.3f, 0.5f, 0.2f), 0.6f, arc_v3_make(1, 1, 1)), 0xD0D0D0);
        }
        arc_set_shadow_strength(1);
    } else {
        arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0); v071p_rooms(look == L_CELLO);
        arc_set_camera(&CCAM); arc_set_light(arc_v3_make(-1, 0.4f, 0.2f), 0xFFFFFF, 0x303030);
        arc_draw_box(arc_v3_make(0, -0.1f, 0), arc_v3_make(12, 0, 4), 0x606060);   /* the floor: no cell, always drawn */
        arc_set_draw_cell(2); arc_draw_cube(arc_v3_make(6, 1.5f, 1.2f), arc_v3_make(0.5f, 0.5f, 0.5f), 0xFF0000);
        arc_set_draw_cell(3); arc_draw_cube(arc_v3_make(10, 1.5f, 2.8f), arc_v3_make(1, 1, 1), 0x00FF00);
        arc_set_draw_cell(0); arc_cells_clear();
    }
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static int v071p_dom(const uint8_t *p) { return p[0] > p[1] + 40 && p[0] > p[2] + 40 ? 0 : p[1] > p[0] + 40 && p[1] > p[2] + 40 ? 1 : p[2] > p[0] + 40 && p[2] > p[1] + 40 ? 2 : -1; }
static void v071p_check(void) {
    CHECK(v071p_dom(cpx(L_LOD, &LCAM, arc_v3_make(-1.5f, 0, 0.5f))) == 0);   /* near: the full mesh */
    CHECK(v071p_dom(cpx(L_LOD, &LCAM, arc_v3_make(1.5f, 0, -7))) == 1);      /* beyond 10: level 1 */
    CHECK(v071p_dom(cpx(L_LOD, &LCAM, arc_v3_make(-8, 0, -39))) == 2);        /* small on screen: level 2 */
    int off = 0, lit = 0, shade = 0;   /* the static batch: the picture of its single draws, shadow included */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        if (diff(L_STATU, L_STATB, x, y) > 12) off++;
        if (lum8(L_STATU, x, y) > 40) lit++;
    }
    arc_v2 sp; if (arc_camera_to_screen(&TCAM, W, H, arc_v3_make(1.8f, 0, 0.9f), &sp)) shade = lum8(L_STATB, (int)sp.x, (int)sp.y);
    CHECK(lit > W * H / 5); CHECK(off <= W * H / 1000);
    (void)shade;
    const uint8_t *g0 = cpx(L_CELLC, &CCAM, arc_v3_make(9.5f, 1.5f, 2.8f)), *g1 = cpx(L_CELLO, &CCAM, arc_v3_make(9.5f, 1.5f, 2.8f));
    const uint8_t *r0 = cpx(L_CELLC, &CCAM, arc_v3_make(5.75f, 1.5f, 1.2f)), *r1 = cpx(L_CELLO, &CCAM, arc_v3_make(5.75f, 1.5f, 1.2f));
    CHECK(g0[1] < 20); CHECK(g1[1] > 120 && g1[0] < 60);     /* room 3's cube: culled behind the closed door, drawn when open */
    CHECK(r0[0] > 120 && r1[0] > 120 && r0[1] < 60);          /* room 2's: drawn both times */
}
/* 0.7.2 depth precision far away: four pairs of tilted quads 2000 units off (znear 0.05, zfar 5000), red drawn first,
   green in front of it by 0.01, 0.1, 1 and 10 units along their normal. Standard depth can't tell 0.01 apart there
   (red shows through, in z-fighting bands); reversed-Z with float depth can: all green. */
static const arc_camera ZCAM = { { 0, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, 0.9f, 0, 0.05f, 5000 };
static const float ZSEP[4] = { 0.01f, 0.1f, 1.0f, 10.0f };
static void zfar_quad(float cx, float off, uint32_t rgb) {   /* 560 x 760, turned 35 degrees about y and 20 about x */
    float ay = 0.6109f, ax = 0.3491f, cy = cosf(ay), sy = sinf(ay), cxa = cosf(ax), sxa = sinf(ax);
    arc_v3 u = arc_v3_make(cy, 0, -sy), v = arc_v3_make(sy * sxa, cxa, cy * sxa), n = arc_v3_cross(u, v);
    arc_v3 c = arc_v3_add(arc_v3_make(cx, 0, -2000), arc_v3_scale(n, off)), p[4];
    for (int i = 0; i < 4; i++) p[i] = arc_v3_add(c, arc_v3_add(arc_v3_scale(u, (i == 1 || i == 2) ? 280.0f : -280.0f), arc_v3_scale(v, i >= 2 ? 380.0f : -380.0f)));
    arc_draw_quad3(p[0], p[1], p[2], p[3], n, rgb);
}
static void zfar_draw(void) {
    arc_set_post(NULL); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_fog(0, 0, 0);
    arc_set_camera(&ZCAM); arc_set_light(arc_v3_make(0, 0, 1), 0x404040, 0xC0C0C0);
    for (int i = 0; i < 4; i++) { float x = -1050.0f + 700.0f * (float)i; zfar_quad(x, 0, 0xFF0000); zfar_quad(x, ZSEP[i], 0x00FF00); }
    char path[64]; look_path(path, sizeof path, L_ZFAR); arc_screenshot_after_frame(path);
}
static void zfar_check(void) {   /* the share of green among the pair's red and green pixels, per separation */
    int pct[4];
    for (int i = 0; i < 4; i++) {
        int g = 0, all = 0; arc_v2 sp;
        if (!arc_camera_to_screen(&ZCAM, W, H, arc_v3_make(-1050.0f + 700.0f * (float)i, 0, -2000), &sp)) { pct[i] = -1; continue; }
        for (int y = (int)sp.y - 12; y <= (int)sp.y + 12; y++) for (int x = (int)sp.x - 12; x <= (int)sp.x + 12; x++) {
            bool in = true;   /* inside the quads (not the one-pixel rim where the nearer one sits a little aside) */
            for (int k = 0; k < 9; k++) { const uint8_t *e = px(L_ZFAR, arc_clampi(x + k % 3 - 1, 0, W - 1), arc_clampi(y + k / 3 - 1, 0, H - 1)); if (e[0] < 60 && e[1] < 60) in = false; }
            const uint8_t *q = px(L_ZFAR, arc_clampi(x, 0, W - 1), arc_clampi(y, 0, H - 1));
            if (in) { all++; if (q[1] > q[0]) g++; }
        }
        pct[i] = all ? g * 100 / all : -1;
    }
    printf("ARC note depth %s%s at 2000 (znear 0.05): green (nearer) share for 0.01 / 0.1 / 1 / 10 apart: %d%% %d%% %d%% %d%%\n",
           arc_depth_mode() == ARC_DEPTH_STANDARD ? "standard" : arc_depth_mode() == ARC_DEPTH_REVERSED ? "reversed" : "reversed infinite",
           arc_depth_mode() != ARC_DEPTH_STANDARD && !arc_depth_precise() ? " (no 0..1 clip range)" : "", pct[0], pct[1], pct[2], pct[3]);
    for (int i = 0; i < 4; i++) CHECK(pct[i] >= 0);
    if (arc_depth_mode() == ARC_DEPTH_STANDARD || !arc_depth_precise()) CHECK(pct[0] < 90);   /* 0.01: red shows through */
    else for (int i = 0; i < 4; i++) CHECK(pct[i] == 100);                                  /* every pair resolved */
}
/* reversed runs: each look against the standard run's picture when it is here (same pictures, give or take a pixel
   where two surfaces touch) */
static void compare_standard(void) {
    if (arc_depth_mode() == ARC_DEPTH_STANDARD) return;
    int looks = 0, worst_look = -1, worst_n = 0;
    for (int l = 0; l < L_COUNT; l++) {
        if (l == L_ZFAR || l == L_EDIT || l == L_LOBBY || l == L_LOBBY_ON) continue;   /* L_EDIT is 2D only: depth cannot change it (GL glyph sampling differs
                                                  slightly between the window and ARC's own framebuffer at msaa 0) */
        char p[64]; snprintf(p, sizeof p, "%s_%d.png", look_prefix(ARC_DEPTH_STANDARD), l);
        int w = 0, h = 0; uint8_t *ref = arc_image_load(p, &w, &h);
        if (!ref || w != W || h != H) { free(ref); continue; }
        int n = 0, mx = 0;
        for (int i = 0; i < W * H; i++) { int m = 0; for (int k = 0; k < 3; k++) { int d = abs(ref[i * 4 + k] - img[l][i * 4 + k]); if (d > m) m = d; } if (m > mx) mx = m; if (m > 24) n++; }
        free(ref); looks++;
        if (n > worst_n) { worst_n = n; worst_look = l; }
        if (n) printf("ARC note reversed vs standard look %d: %d pixels differ by more than 24 (max %d)\n", l, n, mx);
        CHECK(n <= (l == L_DEPTH ? W * H / 50 : W * H / 500));   /* at most 0.2% of the picture (x-ray's scan lines move with time in a window) */
    }
    printf("ARC note reversed vs standard: %d looks compared, worst look %d with %d pixels\n", looks, worst_look, worst_n);
}
static void check_all(void) {
    zfar_check(); compare_standard();
    int obig = 0, obright = 0;   /* orthographic ssao: darker where the box meets the floor, never brighter */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { if (px(L_OSSAO, x, y)[1] + 6 < px(L_OPLAIN, x, y)[1]) obig++; if (px(L_OSSAO, x, y)[1] > px(L_OPLAIN, x, y)[1] + 1) obright++; }
    CHECK(obig > 20); CHECK(obright == 0);
    const uint8_t *osky = px(L_OSKY, W / 2, H / 2); CHECK(osky[0] > osky[2] + 40 && osky[0] > 150);   /* orthographic sky: toward the below colour (red) */
    v071p_check(); check_v07(); check_shadows(); check_nine(); check_push(); check_depth(); check_plain();
    int big = 0;   /* ssao: darker where the box meets the floor, the open floor and the sky as they were */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (px(L_SSAO, x, y)[1] + 6 < px(L_PLAIN, x, y)[1]) big++;
    CHECK(big > 20);
    CHECK(diff(L_SSAO, L_PLAIN, S(4), S(60)) <= 2 && diff(L_SSAO, L_PLAIN, S(48), S(3)) == 0);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (px(L_SSAO, x, y)[1] > px(L_PLAIN, x, y)[1] + 1) { CHECK(!"ssao never brightens"); y = H; break; }
    int edge = 0, flat_ok = 1;   /* fxaa: some edge pixels change, flat areas do not */
    for (int y = 1; y < H - 1; y++) for (int x = 1; x < W - 1; x++) if (diff(L_FXAA, L_PLAIN, x, y) > 4) edge++;
    if (diff(L_FXAA, L_PLAIN, S(48), S(3)) || diff(L_FXAA, L_PLAIN, S(6), S(58)) > 1) flat_ok = 0;
    CHECK(edge > 10 && flat_ok);
    int worst = 0, worst_inv = 0;   /* identity table: unchanged; inverting table: 255 - plain */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int d = diff(L_LUT_ID, L_PLAIN, x, y); if (d > worst) worst = d;
        for (int k = 0; k < 3; k++) { int e = abs(px(L_LUT_INV, x, y)[k] - (255 - px(L_PLAIN, x, y)[k])); if (e > worst_inv) worst_inv = e; }
    }
    CHECK(worst <= 2); CHECK(worst_inv <= 3);
    const uint8_t *k = px(L_CRT, 0, 0); CHECK(k[0] == 0 && k[1] == 0 && k[2] == 0);   /* crt: corners black, the centre the same */
    CHECK(diff(L_CRT, L_PLAIN, W / 2, H / 2) <= 2);
    bool blocks = true;   /* pixelate 4: every 4 x 4 block is one colour */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (diff(L_PIX, L_PIX, x, y), memcmp(px(L_PIX, x, y), px(L_PIX, x & ~3, y & ~3), 3)) blocks = false;
    CHECK(blocks);
    const uint8_t *s0 = px(L_SCAN, S(48), 2), *s1 = px(L_SCAN, S(48), 3), *p0 = px(L_PLAIN, S(48), 2);   /* scanlines 1: rows alternate */
    CHECK(abs(s0[1] - p0[1]) <= 1 && s1[1] <= 1);
    int fr = 0;   /* aberration: red and blue move apart toward the edges, green stays */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        const uint8_t *a = px(L_ABER, x, y), *p = px(L_PLAIN, x, y);
        if (abs(a[0] - p[0]) > 8 || abs(a[2] - p[2]) > 8) fr++;
        if (a[1] != p[1]) { CHECK(!"aberration keeps green"); y = H; break; }
    }
    CHECK(fr > 10);
}
static void finish(void) {
    bool all = true; for (int i = 0; i < L_COUNT; i++) if (!img[i]) all = false;
    CHECK(all);
    if (all) { check_all(); check_ctex(); v072p_check(); v080p_check(); v085p_check(); v082p_check(); v082u_check(); v084p_check(); v084d_check(); v087r_check(); v088_check(); v095_check(); v096_check(); v099_check(); }
    printf("ARC test %s %d/%d post and shadow checks passed\n", fails ? "FAILED" : "OK", checks - fails, checks); exit(fails ? 1 : 0);
}
/* ---- 0.7.2 the tile editor: a 4 x 2 grid in the 2D view at 20 px a cell. ARC draws it over the frame (after draw()):
   the editor opens (playing, so ticks go on) when this look starts. 192 x 128: the view is 125 x 112 left of the palette,
   so the grid sits at (22, 36); cells are inset 1 px, the grid-line colour 0x383E48 shows between them. ---- */
static char v072p_grid[2][5] = { "#.X@", "X@#." };
static const arc_editor_tile v072p_tiles[] = {
    { '.', "Floor", 0x0000FF, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 }, { '#', "Wall", 0xFF0000, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 },
    { 'X', "Crate", 0x00FF00, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 }, { '@', "Start", 0xFFFF00, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 } };
static void v072p_tick(void) {
    if (look_now == L_EDIT && !arc_editor_is_open()) {
        arc_editor_desc d; memset(&d, 0, sizeof d);
        d.name = "post"; d.width = 4; d.height = 2; d.cells = v072p_grid; d.stride = 5; d.tiles = v072p_tiles; d.tile_count = 4;
        d.cell_px = 20; d.start_playing = true;
        arc_editor_begin(&d); arc_editor_open(true);
    }
    if (look_now != L_EDIT && arc_editor_is_open()) arc_editor_end();   /* later looks draw without it */
}
static void v072p_draw(void) {
    arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    char path[64]; look_path(path, sizeof path, L_EDIT); arc_screenshot_after_frame(path);
}
static int v072p_is(int x, int y, uint32_t rgb) {
    const uint8_t *p = px(L_EDIT, x, y);
    return abs(p[0] - (int)(rgb >> 16)) <= 2 && abs(p[1] - (int)((rgb >> 8) & 255)) <= 2 && abs(p[2] - (int)(rgb & 255)) <= 2;
}
static void v072p_check(void) {
    static const uint32_t want[2][4] = { { 0xFF0000, 0x0000FF, 0x00FF00, 0xFFFF00 }, { 0x00FF00, 0xFFFF00, 0xFF0000, 0x0000FF } };
    for (int y = 0; y < 2; y++) for (int x = 0; x < 4; x++) {
        int cx = 22 + 20 * x, cy = 36 + 20 * y;
        CHECK(v072p_is(cx + 4, cy + 4, want[y][x]) && v072p_is(cx + 16, cy + 16, want[y][x]));   /* inside each cell (clear of its letter) */
        CHECK(v072p_is(cx, cy + 10, 0x383E48) && v072p_is(cx + 10, cy, 0x383E48));               /* the grid line left of and above it */
    }
    CHECK(v072p_is(22 + 80, 36 + 10, 0x383E48) && v072p_is(22 + 10, 36 + 40, 0x383E48));        /* the right and bottom edges */
    const uint8_t *bg = px(L_EDIT, 8, 100), *pal = px(L_EDIT, 190, 20);
    CHECK(bg[0] < 30 && bg[1] < 30 && bg[2] < 40);                                               /* the view's dark backdrop */
    CHECK(pal[0] >= 0x10 && pal[0] < 0x30 && pal[2] >= 0x18 && pal[2] < 0x40);                    /* the palette panel */
}
/* ---- 0.8 the multiplayer lobby: ARC's ready-made panel (offline: name, host, LAN games, join), drawn by the UI kit over
   the frame. A test machine with in-process links (no sockets, so no LAN game can turn up in the list). ---- */
void *arc__net_test_machine(void);
void *arc__net_test_use(void *m);
static void *v080p_m;
static void v080p_tick(void) {
    if (look_now == L_LOBBY) {
        if (!v080p_m) {
            v080p_m = arc__net_test_machine(); arc__net_test_use(v080p_m);
            arc_net_desc d; memset(&d, 0, sizeof d); d.game = "post"; d.version = "1"; arc_net_setup(&d); arc_net_set_name("Post");
        }
        arc_net_lobby(NULL);
    } else if (v080p_m) arc__net_test_use(NULL);
}
static void v080p_draw(void) {
    arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    char path[64]; look_path(path, sizeof path, L_LOBBY); arc_screenshot_after_frame(path);
}
static int v080p_is(int x, int y, uint32_t argb) {
    const uint8_t *p = px(L_LOBBY, x, y);
    return abs(p[0] - (int)((argb >> 16) & 255)) <= 3 && abs(p[1] - (int)((argb >> 8) & 255)) <= 3 && abs(p[2] - (int)(argb & 255)) <= 3;
}
static void v080p_check(void) {   /* the panel at x 8..183: title bar, two widgets (the name field, Host a game), its backdrop */
    CHECK(v080p_is(3, 60, 0x000000) && v080p_is(187, 60, 0x000000));         /* outside the panel */
    CHECK(v080p_is(10, 10, 0x2C3E58) && v080p_is(12, 30, 0x2C3E58));         /* the title bar (theme.title), clear of its text */
    CHECK(v080p_is(15, 42, 0x2A303C) && v080p_is(100, 42, 0x2A303C));        /* "Your name" field (theme.widget), above its text */
    CHECK(v080p_is(15, 75, 0x2A303C) && v080p_is(170, 75, 0x2A303C));        /* "Host a game" */
    CHECK(v080p_is(9, 60, 0x14171D) && v080p_is(182, 60, 0x14171D));         /* the panel: theme.panel 0xD0181C24 over black */
}
/* ---- 0.8.5 the lobby with a relay set (Operation Chariot's request): under the name field come "Host online", the room
   code field and "Join by code" (the LAN part follows further down, off this small screen). A second test machine, with
   desc.relay set (never contacted: the lobby only checks that this build can speak the address). ---- */
static void *v085p_m;
static void v085p_tick(void) {
    if (look_now == L_LOBBY_ON) {
        if (!v085p_m) {
            v085p_m = arc__net_test_machine(); arc__net_test_use(v085p_m);
            arc_net_desc d; memset(&d, 0, sizeof d); d.game = "post"; d.version = "1"; d.relay = "ws://relay.test:47880"; arc_net_setup(&d); arc_net_set_name("Post");
        } else arc__net_test_use(v085p_m);
        arc_net_lobby(NULL);
    } else if (v085p_m) arc__net_test_use(NULL);
}
static void v085p_draw(void) {
    arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    char path[64]; look_path(path, sizeof path, L_LOBBY_ON); arc_screenshot_after_frame(path);
}
static int v085p_is(int x, int y, uint32_t argb) {
    const uint8_t *p = px(L_LOBBY_ON, x, y);
    return abs(p[0] - (int)((argb >> 16) & 255)) <= 3 && abs(p[1] - (int)((argb >> 8) & 255)) <= 3 && abs(p[2] - (int)(argb & 255)) <= 3;
}
static void v085p_check(void) {
    CHECK(v085p_is(3, 60, 0x000000) && v085p_is(187, 60, 0x000000));        /* outside the panel */
    CHECK(v085p_is(10, 10, 0x2C3E58) && v085p_is(15, 42, 0x2A303C));        /* the title bar, "Your name" (as the LAN lobby) */
    CHECK(v085p_is(15, 75, 0x2A303C) && v085p_is(170, 75, 0x2A303C));       /* "Host online" where "Host a game" was */
    CHECK(v085p_is(15, 108, 0x2A303C) && v085p_is(170, 108, 0x2A303C));     /* the "Room code" field */
    CHECK(v080p_is(15, 108, 0x14171D) || !v080p_is(15, 108, 0x2A303C));     /* (the LAN lobby has no field there) */
    CHECK(v085p_is(9, 92, 0x14171D) && v085p_is(182, 92, 0x14171D));        /* the panel between the two */
}
/* ---- 0.8.2 post effects for a render target (Moonbase Raid's request): 3D drawn into a target that is then placed on
   the screen. TPOFF / TPON: an emissive cube in a 128 x 72 target at (32, 28), with 2D drawn into the target after the
   3D; the screen has no post. TPON gives the target bloom and a vignette: the cube must glow past its edges, the
   target's 2D and everything outside the target stay exactly as they were, and the vignette follows the target's own
   corners. TPLAIN / TSSAO: the floor-and-box scene in a W x H target shown 1:1, plain post and ssao. TNOSH / TSH1 /
   TCASC: the shadow scene in that target; cascades only reach the far box when the target has them turned on. ---- */
static arc_texture v082p_rt, v082p_full;
enum { TPX = 32, TPY = 28, TPW = 128, TPH = 72 };
static const arc_camera TPCAM = { { 0, 0.8f, 3.4f }, { 0, 0, 0 }, { 0, 1, 0 }, 0.8f, 0, 0.1f, 50 };
static void v082p_draw(int look) {
    if (!v082p_rt.id) { v082p_rt = arc_target_create(TPW, TPH, false); v082p_full = arc_target_create(W, H, false); }
    arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    arc_set_shadow_cascades(1, 0); arc_set_shadow_softness(0);
    if (look == L_TPOFF || look == L_TPON) {
        arc_post p = { 0 }; p.bloom = 1; p.vignette = 0.6f;
        CHECK(arc_target_set_post(v082p_rt, look == L_TPON ? &p : NULL));
        arc_target_begin(v082p_rt, true, 0xFF282828);
        arc_set_camera(&TPCAM); arc_set_light(arc_v3_make(0.3f, 0.8f, 0.6f), 0xC0C0C0, 0x404040);
        arc_draw_mesh_ex(cube, arc_m4_trs_y(arc_v3_make(0, 0, 0), 0.6f, arc_v3_make(0.8f, 0.8f, 0.8f)), 0xFFFFFF, 1);
        arc_draw_rect(4, 4, 14, 8, 0x00FF00);                      /* the target's own 2D, after its 3D */
        arc_target_end();
        arc_draw_texture_ex(v082p_rt, 0, 0, TPW, TPH, TPX, TPY, TPW, TPH, 0, 0, 0, ARC_WHITE);
        arc_draw_rect(4, 4, 20, 12, 0xFF00FF);                     /* the screen's 2D */
    } else if (look == L_TPLAIN || look == L_TSSAO) {
        arc_post p = { 0 }; p.exposure = 1.0001f; if (look == L_TSSAO) { p.ssao = 1; p.ssao_radius = 0.4f; }
        CHECK(arc_target_set_post(v082p_full, &p));
        arc_camera cam = { { 0, 1.6f, 3.2f }, { 0, 0.3f, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 50 };
        arc_target_begin(v082p_full, true, 0xFF6080A0);
        arc_set_camera(&cam); arc_set_light(arc_v3_make(-0.4f, 0.9f, 0.5f), 0xB0B0B0, 0x505050);
        arc_draw_box(arc_v3_make(-3, -0.1f, -3), arc_v3_make(3, 0, 3), 0x808080);
        arc_draw_box(arc_v3_make(-0.5f, 0, -0.5f), arc_v3_make(0.5f, 1, 0.5f), 0xC04040);
        arc_target_end();
        arc_draw_texture_ex(v082p_full, 0, 0, W, H, 0, 0, W, H, 0, 0, 0, ARC_WHITE);
    } else {
        arc_post p = { 0 }; p.exposure = 1.0001f; CHECK(arc_target_set_post(v082p_full, &p));
        if (look != L_TNOSH) arc_set_shadows(arc_v3_make(0, 0, 0), 3, 512);
        arc_set_shadow_cascades(3, 40);
        arc_target_set_shadow_cascades(v082p_full, look == L_TCASC);
        arc_target_begin(v082p_full, true, 0xFF6080A0);
        arc_set_camera(&SCAM); arc_set_light(arc_v3_make(0.5f, 1.0f, 0.3f), 0xC0C0C0, 0x404040);
        arc_draw_box(arc_v3_make(-10, -0.1f, -30), arc_v3_make(10, 0, 8), 0x909090);
        arc_draw_box(arc_v3_make(-0.5f, 0, -0.5f), arc_v3_make(0.5f, 1, 0.5f), 0xC04040);
        arc_draw_box(arc_v3_make(-1.5f, 0, -17.5f), arc_v3_make(1.5f, 3, -14.5f), 0x4040C0);
        arc_target_end();
        arc_target_set_shadow_cascades(v082p_full, false);
        arc_draw_texture_ex(v082p_full, 0, 0, W, H, 0, 0, W, H, 0, 0, 0, ARC_WHITE);
    }
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static void v082p_check(void) {
    /* bloom: the cube is the bright part of TPOFF; the ring 1..4 pixels outside it is brighter in TPON */
    static uint8_t mask[H][W];
    int cube_px = 0;
    for (int y = TPY; y < TPY + TPH; y++) for (int x = TPX; x < TPX + TPW; x++) if (lum8(L_TPOFF, x, y) > 150) { mask[y][x] = 1; cube_px++; }
    CHECK(cube_px > 200);
    long on = 0, off = 0; int ring = 0;
    for (int y = TPY + 4; y < TPY + TPH - 4; y++) for (int x = TPX + 4; x < TPX + TPW - 4; x++) {
        if (mask[y][x]) continue;
        bool nearc = false;
        for (int j = -4; j <= 4 && !nearc; j++) for (int i = -4; i <= 4; i++) if (mask[y + j][x + i]) { nearc = true; break; }
        if (!nearc) continue;
        on += lum8(L_TPON, x, y); off += lum8(L_TPOFF, x, y); ring++;
    }
    CHECK(ring > 50);
    if (ring) { printf("ARC note target bloom: ring of %d pixels, mean %ld off, %ld on\n", ring, off / ring, on / ring); CHECK(on / ring > off / ring + 8); }
    int outside = 0, twod = 0;   /* the screen outside the target, and the target's 2D: unchanged */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        bool in = x >= TPX && x < TPX + TPW && y >= TPY && y < TPY + TPH;
        if (!in && diff(L_TPON, L_TPOFF, x, y) > 0) outside++;
    }
    for (int y = TPY + 5; y < TPY + 11; y++) for (int x = TPX + 5; x < TPX + 17; x++) if (diff(L_TPON, L_TPOFF, x, y) > 1 || !near_rgb(L_TPON, x, y, 0x00FF00)) twod++;
    CHECK(outside == 0); CHECK(twod == 0);
    CHECK(lum8(L_TPON, TPX + TPW - 2, TPY + TPH - 2) + 8 < lum8(L_TPOFF, TPX + TPW - 2, TPY + TPH - 2));   /* the vignette: the target's corner */
    CHECK(near_rgb(L_TPON, 10, 10, 0xFF00FF));
    /* ssao in a target: darker where the box meets the floor, never brighter */
    int big = 0, brighter = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { if (px(L_TSSAO, x, y)[1] + 6 < px(L_TPLAIN, x, y)[1]) big++; if (px(L_TSSAO, x, y)[1] > px(L_TPLAIN, x, y)[1] + 1) brighter++; }
    CHECK(big > 20); CHECK(brighter == 0);
    /* cascades in a target: near box shadowed with or without; far box only with the target's cascades */
    arc_v3 nearp = arc_v3_make(-0.75f, 0, 0), farp = arc_v3_make(-2.6f, 0, -16);
    int nn = area(L_TNOSH, nearp), n1 = area(L_TSH1, nearp), nc = area(L_TCASC, nearp);
    int fn = area(L_TNOSH, farp), f1 = area(L_TSH1, farp), fc = area(L_TCASC, farp);
    printf("ARC note target shadows: near %d / %d / %d, far %d / %d / %d (none / one box / cascades)\n", nn, n1, nc, fn, f1, fc);
    CHECK(n1 < nn - 15 && nc < nn - 15); CHECK(abs(f1 - fn) <= 2); CHECK(fc < fn - 15);
    /* the API: only render targets; 8 at once (two in use above); off frees a slot; all-zero settings count as off */
    arc_post bp = { 0 }; bp.bloom = 1;
    CHECK(!arc_target_set_post(white4, &bp));
    arc_texture more[7]; int took = 0;
    for (int i = 0; i < 7; i++) { more[i] = arc_target_create(8, 8, false); if (arc_target_set_post(more[i], &bp)) took++; }
    CHECK(took == 6);                                          /* the 9th target is refused (ARC error line) */
    CHECK(arc_target_set_post(more[0], NULL)); CHECK(arc_target_set_post(more[6], &bp));   /* off frees a slot */
    arc_post zero = { 0 }; CHECK(arc_target_set_post(more[6], &zero)); CHECK(arc_target_set_post(more[0], &bp));
    for (int i = 0; i < 7; i++) arc_texture_destroy(more[i]);
    CHECK(arc_target_set_post(arc_target_create(8, 8, false), &bp));   /* destroyed targets give their slots back */
}
/* ---- 0.8.2 UI: UIWRAP, a paragraph wrapped to a 120-pixel panel at (10, 10) (no white pixel right of the panel, and
   several lines); UNDER / TOP: a panel with a magenta rectangle over the whole screen, drawn in draw() (the panel shows
   through) and in draw_top (it covers the panel). ---- */
static const char *V082U_TEXT = "Move each mech in turn, then fire. Heat builds with every shot, and a mech that runs too hot shuts down.";
static void v082u_tick(void) {
    if (look_now == L_UIWRAP) { arc_ui_begin("", 10, 10, 120); arc_ui_text_wrapped(V082U_TEXT); arc_ui_end(); }
    if (look_now == L_UNDER || look_now == L_TOP) { arc_ui_begin("Panel", 30, 30, 120); arc_ui_label("Under the cover"); arc_ui_button("Button"); arc_ui_end(); }
}
static void v082u_draw(int look) {
    arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    if (look == L_UNDER) arc_draw_rect(0, 0, W, H, 0xFF00FF);
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static void v082u_top(float alpha) { (void)alpha; if (look_now == L_TOP) arc_draw_rect(0, 0, W, H, 0xFF00FF); }
static void v082u_check(void) {
    int right = 0, rows = 0, last = -10;
    for (int y = 0; y < H; y++) {
        bool any = false;
        for (int x = 0; x < W; x++) { const uint8_t *p = px(L_UIWRAP, x, y); if (p[0] > 180 && p[1] > 180 && p[2] > 180) { any = true; if (x >= 130) right++; } }
        if (any && y > last + 3) rows++;
        if (any) last = y;
    }
    printf("ARC note wrapped UI text: %d lines, %d pixels past the panel\n", rows, right);
    CHECK(right == 0); CHECK(rows >= 5);
    int panel = 0, covered = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { if (!near_rgb(L_UNDER, x, y, 0xFF00FF)) panel++; if (near_rgb(L_TOP, x, y, 0xFF00FF)) covered++; }
    CHECK(panel > 2000);          /* draw()'s 2D goes under the panel */
    CHECK(covered == W * H);      /* draw_top's covers it */
}

/* ---- 0.8.4 textures that don't visibly repeat (Moonbase Raid's request). A 16 x 16 floor (32 x 32 quads) with a dirt
   texture tiled 16 times, seen from straight above (orthographic, 4 units high: one tile is 32 pixels):
   AREP plain repeat, AHEX hex tiling with rotate 1, AHEX0 hex with rotate 0 (offsets only). The texture's own
   autocorrelation at one tile's lag is 1 for plain repeat and should fall near 0 with hex. AGREP / AGHEX: the same at a
   grazing angle; hex must not sparkle more than repeat (mean neighbour difference far away). ADGREY: hex plus a 50%
   grey detail map: no change. ADFLOOR / ADWALL: a world-mapped detail checker (1 world unit squares) on the floor and
   on a wall, whatever the mesh's UVs (64 grey: x 0.5; 192: x 1.5, white). AUVX: arc_mesh_style uv_rotate a quarter turn on the 3 x 3 colour texture. ---- */
static arc_texture v084_dirt, v084_grey, v084_check; static arc_mesh v084_floor, v084_floor2, v084_wall, v084_quad;
static float v084_vn(int x, int y, int per) {   /* periodic value noise lattice */
    x = ((x % per) + per) % per; y = ((y % per) + per) % per;
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16;
    return (float)(h & 0xFFFF) / 65535.0f;
}
static float v084_noise(float x, float y, int per) {
    int ix = (int)floorf(x), iy = (int)floorf(y); float fx = x - (float)ix, fy = y - (float)iy;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    float a = v084_vn(ix, iy, per), b = v084_vn(ix + 1, iy, per), c = v084_vn(ix, iy + 1, per), d = v084_vn(ix + 1, iy + 1, per);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
static arc_mesh v084_plane(float uvs, float uoff) {   /* 16 x 16 at y = 0, 32 x 32 quads, uv = (x + 8, z + 8) * uvs + uoff */
    enum { N = 32 }; static arc_vertex v[(N + 1) * (N + 1)]; static uint32_t idx[N * N * 6]; int k = 0;
    for (int j = 0; j <= N; j++) for (int i = 0; i <= N; i++) {
        arc_vertex *q = &v[j * (N + 1) + i]; memset(q, 0, sizeof *q);
        q->px = -8 + 16.0f * (float)i / N; q->pz = -8 + 16.0f * (float)j / N; q->ny = 1; q->color = 0xFFFFFFFFu;
        q->u = (q->px + 8) * uvs + uoff; q->v = (q->pz + 8) * uvs;
    }
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        uint32_t a = (uint32_t)(j * (N + 1) + i), b = a + N + 1, c = a + 1, d = b + 1;   /* (x0,z0) (x0,z1) (x1,z0) (x1,z1) */
        idx[k++] = a; idx[k++] = b; idx[k++] = c; idx[k++] = c; idx[k++] = b; idx[k++] = d;
    }
    return arc_mesh_create(v, (N + 1) * (N + 1), idx, k);
}
static void v084p_init(void) {
    static uint8_t px[64 * 64 * 4];
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {   /* tileable dirt: 3 octaves, with dark pebbles */
        float n = 0.5f * v084_noise((float)x / 8, (float)y / 8, 8) + 0.3f * v084_noise((float)x / 4, (float)y / 4, 16) + 0.2f * v084_noise((float)x / 2, (float)y / 2, 32);
        uint8_t *p = px + (y * 64 + x) * 4; float l = 40 + 190 * n;
        p[0] = (uint8_t)l; p[1] = (uint8_t)(l * 0.8f); p[2] = (uint8_t)(l * 0.6f); p[3] = 255;
    }
    v084_dirt = arc_texture_create_ex(64, 64, px, ARC_TEX_MESH);
    memset(px, 128, 8 * 8 * 4); for (int i = 0; i < 64; i++) px[i * 4 + 3] = 255;
    v084_grey = arc_texture_create_ex(8, 8, px, ARC_TEX_MESH);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { uint8_t *p = px + (y * 8 + x) * 4; uint8_t l = ((x < 4) ^ (y < 4)) ? 192 : 64; p[0] = p[1] = p[2] = l; p[3] = 255; }
    v084_check = arc_texture_create_ex(8, 8, px, ARC_TEX_MESH);
    v084_floor = v084_plane(1, 0); v084_floor2 = v084_plane(0.37f, 0.21f);   /* the second: UVs that a world-mapped detail must ignore */
    arc_vertex w[4]; memset(w, 0, sizeof w);   /* a wall facing +z: x -4..4, y -2..2, odd UVs */
    for (int i = 0; i < 4; i++) { w[i].px = (i == 1 || i == 2) ? 4.0f : -4.0f; w[i].py = i >= 2 ? -2.0f : 2.0f; w[i].nz = 1; w[i].color = 0xFFFFFFFFu; w[i].u = w[i].px * 0.3f; w[i].v = w[i].py * 0.7f; }
    uint32_t wi[6] = { 0, 2, 1, 0, 3, 2 }; v084_wall = arc_mesh_create(w, 4, wi, 6);
    v084_quad = ct_quad(); arc_mesh_set_texture(v084_quad, nine);
}
static void v084p_draw(int look) {
    if (!v084_dirt.id) v084p_init();
    arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    arc_set_shadow_cascades(1, 0); arc_set_shadow_softness(0);
    arc_set_light(arc_v3_make(0, 1, 0.0001f), 0x010101, 0xFFFFFF);   /* (almost) ambient only: the picture is the texture */
    arc_camera top = { { 0, 10, 0 }, { 0, 0, 0 }, { 0, 0, -1 }, 0, 4.0f, 0.1f, 50 };
    arc_camera graze = { { 0, 0.5f, 8 }, { 0, 0, -8 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 50 };
    arc_camera front = { { 0, 0, 10 }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 4.0f, 0.1f, 50 };
    arc_material m; memset(&m, 0, sizeof m);
    arc_mesh mesh = v084_floor; arc_m4 model = arc_m4_identity();
    switch (look) {
    case L_AREP: case L_AGREP: break;
    case L_AHEX: case L_AGHEX: case L_ADGREY: m.base_tiling = ARC_TILE_HEX; m.base_rotate = 1; break;
    case L_AHEX0: m.base_tiling = ARC_TILE_HEX; m.base_rotate = 0; break;
    default: break;
    }
    if (look == L_ADGREY) { m.detail = v084_grey; m.detail_scale = 0.5f; }
    if (look == L_ADFLOOR || look == L_ADWALL) { m.detail = v084_check; m.detail_scale = 0.125f; m.detail_strength = 1; }   /* 4-unit squares */
    arc_set_camera(look == L_AGREP || look == L_AGHEX ? &graze : look == L_ADWALL || look == L_AUVX ? &front : &top);
    if (look == L_AUVX) {
        arc_mesh_style st; memset(&st, 0, sizeof st); st.tint = 0xFFFFFF; st.uv_rotate = ARC_PI / 2;
        arc_draw_mesh_styled(v084_quad, arc_m4_scale(arc_v3_make(3, 3, 1)), &st);
    } else {
        if (look == L_ADFLOOR) mesh = v084_floor2;
        if (look == L_ADWALL) mesh = v084_wall;
        arc_mesh_set_texture(mesh, look == L_ADFLOOR || look == L_ADWALL ? (arc_texture){ 0 } : v084_dirt);
        arc_mesh_set_material(mesh, &m);
        arc_draw_mesh(mesh, model, 0xFFFFFF);
    }
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static double v084_corr(int look, int lag) {   /* normalised autocorrelation of luminance at (lag, 0) and (0, lag), averaged */
    double r[2];
    for (int dir = 0; dir < 2; dir++) {
        double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0; int n = 0;
        for (int y = 0; y + (dir ? lag : 0) < H; y++) for (int x = 0; x + (dir ? 0 : lag) < W; x++) {
            double a = lum8(look, x, y), b = lum8(look, x + (dir ? 0 : lag), y + (dir ? lag : 0));
            sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b; n++;
        }
        double ma = sa / n, mb = sb / n, va = saa / n - ma * ma, vb = sbb / n - mb * mb;
        r[dir] = (va > 0 && vb > 0) ? (sab / n - ma * mb) / sqrt(va * vb) : 0;
    }
    return (r[0] + r[1]) / 2;
}
static double v084_sparkle(int look) {   /* mean neighbour difference on the far floor (rows 66..81 of 128, just under the horizon) */
    long s = 0; int n = 0;
    for (int y = 66; y < 82; y++) for (int x = 0; x + 1 < W; x++) {
        int a = lum8(look, x, y), b = lum8(look, x + 1, y); if (a < 8 || b < 8) continue;
        s += abs(a - b); n++;
    }
    return n ? (double)s / n : -1;
}
static void v084p_check(void) {
    double cr = v084_corr(L_AREP, 32), ch = v084_corr(L_AHEX, 32), c0 = v084_corr(L_AHEX0, 32), crh = v084_corr(L_AREP, 16), chh = v084_corr(L_AHEX, 16);
    printf("ARC note anti-tiling: autocorrelation at one tile %.3f repeat, %.3f hex, %.3f hex without turns (half a tile %.3f / %.3f)\n", cr, ch, c0, crh, chh);
    CHECK(cr > 0.9); CHECK(fabs(ch) < 0.15); CHECK(fabs(c0) < 0.3);
    double sr = v084_sparkle(L_AGREP), sh = v084_sparkle(L_AGHEX);
    printf("ARC note anti-tiling at a grazing angle: mean neighbour difference %.2f repeat, %.2f hex\n", sr, sh);
    CHECK(sr >= 0 && sh >= 0 && sh <= sr * 1.25 + 1.0);
    int grey = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (diff(L_ADGREY, L_AHEX, x, y) > 2) grey++;
    CHECK(grey == 0);   /* 50% grey detail: no change (128/255 is a hair over half) */
    /* world-mapped checker at 0.125 repeats a unit: squares of 4 units. Top camera: x = (px - 96) / 32, z = (py - 64) / 32;
       front camera: y = (64 - py) / 32. Squares change at x = 0 (and y = 0 on the wall) */
    int fa = lum8(L_ADFLOOR, 96 + 64, 64 + 32), fb = lum8(L_ADFLOOR, 96 - 64, 64 + 32);
    int wa = lum8(L_ADWALL, 96 + 64, 64 - 32), wb = lum8(L_ADWALL, 96 - 64, 64 - 32), wc = lum8(L_ADWALL, 96 + 64, 64 + 32);
    printf("ARC note world-mapped detail: floor %d / %d, wall %d / %d / %d\n", fa, fb, wa, wb, wc);
    CHECK(abs(fa - fb) > 80); CHECK(abs(wa - wb) > 80); CHECK(abs(wa - wc) > 80);
    CHECK(abs(fa - wa) <= 8 && abs(fb - wb) <= 8);   /* the same square shades on floor and wall */
    /* uv_rotate a quarter turn: the quad's top-left shows the texture's top-right (blue), top-right its bottom-right */
    CHECK(near_rgb(L_AUVX, 96 - 40, 64 - 40, 0x0000FF)); CHECK(near_rgb(L_AUVX, 96 + 40, 64 - 40, 0xFF8000));
    CHECK(near_rgb(L_AUVX, 96 - 40, 64 + 40, 0xFF0000)); CHECK(near_rgb(L_AUVX, 96, 64, 0x808080));
    arc_material back;   /* the settings read back (defaults filled in) */
    CHECK(arc_mesh_material(v084_wall, &back) && back.detail.id == v084_check.id && back.detail_scale == 0.125f && back.detail_strength == 1
          && back.detail_tiling == ARC_TILE_REPEAT && back.detail_contrast == 0.75f && back.base_tiling == ARC_TILE_REPEAT);
    CHECK(arc_mesh_material(v084_floor, &back) && back.base_tiling == ARC_TILE_HEX && back.base_rotate == 1 && back.base_contrast == 0.75f
          && back.detail.id == v084_grey.id && back.detail_strength == 1);
    arc_mesh_set_material(v084_floor, NULL); CHECK(!arc_mesh_material(v084_floor, &back) && back.detail.id == 0 && back.base_tiling == 0);
}

/* ---- 0.8.4 dirt overlays (Moonbase Raid's second request). A 32 x 32 floor of one plain texture seen from above
   (1 cell = 32 pixels): SBASE alone; STAMP with stamps (8 overlays made by arc_image_breakup from a dirt image,
   2 a cell, rotate 1, sizes 1.2-2.2): the picture's autocorrelation at one cell's lag must be close to that at other
   lags (no grid); STAMP0 with density 0 everywhere: the same as SBASE. STAMPW: stamps on a wall
   (arc_draw_stamps_on). DMASK: the floor's own detail map (dark grime) broken up by a mask: some of the floor clean,
   some dirty. UVM: uv_mirror. Plus value noise, fbm and arc_image_save_png's alpha. ---- */
static arc_texture v084d_tex[8], v084d_floortex, v084d_grime, v084d_mask; static arc_mesh v084d_floor, v084d_floor2, v084d_wall;
static float v084d_zero(void *ud, int cx, int cz) { (void)ud; (void)cx; (void)cz; return 0; }
static arc_mesh v084d_plane(float half) {   /* flat floor at y = 0, -half..half, 16 x 16 quads, uv = world */
    enum { N = 16 }; static arc_vertex v[(N + 1) * (N + 1)]; static uint32_t idx[N * N * 6]; int k = 0;
    for (int j = 0; j <= N; j++) for (int i = 0; i <= N; i++) {
        arc_vertex *q = &v[j * (N + 1) + i]; memset(q, 0, sizeof *q);
        q->px = -half + 2 * half * (float)i / N; q->pz = -half + 2 * half * (float)j / N; q->ny = 1; q->color = 0xFFFFFFFFu; q->u = q->px; q->v = q->pz;
    }
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        uint32_t a = (uint32_t)(j * (N + 1) + i), b = a + N + 1, c = a + 1, d = b + 1;
        idx[k++] = a; idx[k++] = b; idx[k++] = c; idx[k++] = c; idx[k++] = b; idx[k++] = d;
    }
    return arc_mesh_create(v, (N + 1) * (N + 1), idx, k);
}
static void v084d_init(void) {
    enum { S = 256 }; static uint8_t src[S * S * 4], px[64 * 64 * 4];
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {   /* a dirt photo stand-in: fbm grime with pale streaks */
        float n = arc_noise_fbm2((float)x / 24, (float)y / 24, 5, 3), l = 30 + 200 * n;
        uint8_t *p = src + (y * S + x) * 4; p[0] = (uint8_t)l; p[1] = (uint8_t)(l * 0.85f); p[2] = (uint8_t)(l * 0.7f); p[3] = 255;
    }
    for (int i = 0; i < 8; i++) {
        arc_breakup b; memset(&b, 0, sizeof b); b.size = 128; b.crop = 0.3f + 0.1f * (float)i; b.threshold = 0.4f + 0.02f * (float)i; b.second = 0.5f; b.seed = 11 + (uint32_t)i;
        uint8_t *o = arc_image_breakup(src, S, S, &b); v084d_tex[i] = arc_texture_create_ex(128, 128, o, ARC_TEX_MESH); free(o);
    }
    for (int i = 0; i < 64 * 64; i++) { px[i * 4] = 170; px[i * 4 + 1] = 175; px[i * 4 + 2] = 180; px[i * 4 + 3] = 255; }
    v084d_floortex = arc_texture_create_ex(64, 64, px, ARC_TEX_MESH);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {   /* grime detail: dark (0.25: x 0.5), alpha 255 */
        uint8_t *p = px + (y * 64 + x) * 4; p[0] = p[1] = p[2] = 64; p[3] = 255;
    }
    v084d_grime = arc_texture_create_ex(64, 64, px, ARC_TEX_MESH);
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {   /* the breakup mask: tileable fbm (period 4 cells via wrap) */
        float n = 0; float wsum = 0, a = 1;
        for (int o = 0; o < 3; o++) { int per = 4 << o; n += a * v084_noise((float)x / 64 * (float)per, (float)y / 64 * (float)per, per); wsum += a; a *= 0.5f; }
        uint8_t *p = px + (y * 64 + x) * 4; p[0] = p[1] = p[2] = (uint8_t)(n / wsum * 255); p[3] = 255;
    }
    v084d_mask = arc_texture_create_ex(64, 64, px, ARC_TEX_MESH);
    v084d_floor = v084d_plane(16); arc_mesh_set_texture(v084d_floor, v084d_floortex);
    v084d_floor2 = v084d_plane(16); arc_mesh_set_texture(v084d_floor2, v084d_floortex);
    arc_material m; memset(&m, 0, sizeof m); m.detail = v084d_grime; m.detail_scale = 0.25f; m.detail_mask = v084d_mask; m.mask_scale = 0.13f; m.mask_threshold = 0.45f; m.mask_softness = 0.08f;
    arc_mesh_set_material(v084d_floor2, &m);
    arc_vertex w[4]; memset(w, 0, sizeof w);
    for (int i = 0; i < 4; i++) { w[i].px = (i == 1 || i == 2) ? 4.0f : -4.0f; w[i].py = i >= 2 ? -2.0f : 2.0f; w[i].nz = 1; w[i].color = 0xFFFFFFFFu; w[i].u = w[i].px; w[i].v = w[i].py; }
    uint32_t wi[6] = { 0, 2, 1, 0, 3, 2 }; v084d_wall = arc_mesh_create(w, 4, wi, 6); arc_mesh_set_texture(v084d_wall, v084d_floortex);
}
static void v084d_draw(int look) {
    if (!v084d_tex[0].id) v084d_init();
    arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    arc_set_shadow_cascades(1, 0); arc_set_shadow_softness(0);
    arc_set_light(arc_v3_make(0, 1, 0.0001f), 0x010101, 0xFFFFFF);
    arc_camera top = { { 0, 10, 0 }, { 0, 0, 0 }, { 0, 0, -1 }, 0, 4.0f, 0.1f, 50 };
    arc_camera front = { { 0, 0, 10 }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 4.0f, 0.1f, 50 };
    arc_stamps st; memset(&st, 0, sizeof st);
    for (int i = 0; i < 8; i++) st.tex[i] = v084d_tex[i];
    st.count = 8; st.per_cell = 2; st.rotate = 1; st.jitter = 0.4f; st.seed = 5;
    if (look == L_STAMPW || look == L_UVM) {
        arc_set_camera(&front);
        if (look == L_UVM) {
            arc_mesh_style ms; memset(&ms, 0, sizeof ms); ms.uv_mirror = true;
            arc_mesh q = ct_quad(); arc_mesh_set_texture(q, nine);
            arc_draw_mesh_styled(q, arc_m4_scale(arc_v3_make(3, 3, 1)), &ms);
        } else {
            arc_draw_mesh(v084d_wall, arc_m4_identity(), 0xFFFFFF);
            arc_draw_stamps_on(&st, arc_v3_make(-4, 2, 0), arc_v3_make(1, 0, 0), arc_v3_make(0, -1, 0), 8, 4, NULL, NULL, 0xFFFFFF);
        }
    } else {
        arc_set_camera(&top);
        arc_draw_mesh(look == L_DMASK ? v084d_floor2 : v084d_floor, arc_m4_identity(), 0xFFFFFF);
        if (look == L_STAMP) arc_draw_stamps(&st, -16, -16, 16, 16, 0, NULL, NULL, 0xFFFFFF);
        if (look == L_STAMP0) arc_draw_stamps(&st, -16, -16, 16, 16, 0, v084d_zero, NULL, 0xFFFFFF);
    }
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static double v084d_corr(int look, int lx, int ly) {   /* normalised autocorrelation of luminance at (lx, ly) */
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0; int n = 0;
    for (int y = 0; y + ly < H; y++) for (int x = 0; x + lx < W; x++) {
        double a = lum8(look, x, y), b = lum8(look, x + lx, y + ly);
        sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b; n++;
    }
    double ma = sa / n, mb = sb / n, va = saa / n - ma * ma, vb = sbb / n - mb * mb;
    return (va > 0 && vb > 0) ? (sab / n - ma * mb) / sqrt(va * vb) : 0;
}
static void v084d_check(void) {
    /* stamps: dirt is there, and one cell's lag correlates no more than other lags of about that size */
    int changed = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (diff(L_STAMP, L_SBASE, x, y) > 6) changed++;
    double c1 = (v084d_corr(L_STAMP, 32, 0) + v084d_corr(L_STAMP, 0, 32)) / 2, c2 = (v084d_corr(L_STAMP, 64, 0) + v084d_corr(L_STAMP, 0, 64)) / 2;
    double cr = (v084d_corr(L_STAMP, 23, 0) + v084d_corr(L_STAMP, 0, 23) + v084d_corr(L_STAMP, 41, 0) + v084d_corr(L_STAMP, 0, 41)) / 4;
    printf("ARC note stamps: %d%% of the floor dirtied; autocorrelation one cell %.3f, two cells %.3f, other lags %.3f\n", changed * 100 / (W * H), c1, c2, cr);
    CHECK(changed > W * H / 5); CHECK(fabs(c1 - cr) < 0.12); CHECK(fabs(c2 - cr) < 0.12);
    int same = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (diff(L_STAMP0, L_SBASE, x, y) == 0) same++;
    CHECK(same == W * H);   /* density 0: nothing drawn */
    int wall = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (abs(lum8(L_STAMPW, x, y) - lum8(L_SBASE, x, y)) > 6) wall++;
    CHECK(wall > W * H / 5);
    /* detail + breakup mask: clean floor (base colour) in some places, grime (half as bright) in others */
    int clean = 0, dirty = 0, base = lum8(L_SBASE, 96, 64);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { int l = lum8(L_DMASK, x, y); if (abs(l - base) <= 3) clean++; else if (abs(l - base / 2) <= 6) dirty++; }
    printf("ARC note detail mask: %d%% clean, %d%% fully dirty (base %d)\n", clean * 100 / (W * H), dirty * 100 / (W * H), base);
    CHECK(clean > W * H / 6 && dirty > W * H / 10);
    /* uv_mirror: u flipped, so the quad's top-left shows the texture's top-right */
    CHECK(near_rgb(L_UVM, 96 - 40, 64 - 40, 0x0000FF)); CHECK(near_rgb(L_UVM, 96 + 40, 64 - 40, 0xFF0000)); CHECK(near_rgb(L_UVM, 96 - 40, 64 + 40, 0xFF8000));
    /* noise: 0..1, lattice values, seeds unrelated, fbm about 0.5 on average */
    float lo = 1, hi = 0; double fsum = 0; int same_seed = 0;
    for (int i = 0; i < 4000; i++) {
        float x = (float)(i % 71) * 0.37f - 9, z = (float)(i / 71) * 0.53f - 7, v = arc_noise_value2(x, z, 4);
        lo = arc_minf(lo, v); hi = arc_maxf(hi, v); fsum += arc_noise_fbm2(x, z, 0, 9);
        if (fabsf(v - arc_noise_value2(x, z, 5)) < 1e-6f) same_seed++;
    }
    CHECK(lo >= 0 && hi <= 1 && hi - lo > 0.8f); CHECK(same_seed < 40); CHECK(fabs(fsum / 4000 - 0.5) < 0.08);
    CHECK(arc_noise_value2(2.0f, 3.0f, 1) == arc_noise_value2(2.0f, 3.0f, 1) && arc_noise_value2(2.25f, 3, 1) != arc_noise_value2(2.75f, 3, 1));
    /* arc_image_save_png keeps alpha now */
    uint8_t img[4 * 4 * 4]; for (int i = 0; i < 16; i++) { img[i * 4] = (uint8_t)(i * 16); img[i * 4 + 1] = 7; img[i * 4 + 2] = 200; img[i * 4 + 3] = (uint8_t)(i * 17); }
    CHECK(arc_image_save_png("arc_post_alpha.png", img, 4, 4));
    FILE *f = fopen("arc_post_alpha.png", "rb"); uint8_t buf[4096]; size_t n = f ? fread(buf, 1, sizeof buf, f) : 0; if (f) fclose(f);
    int w = 0, h = 0; uint8_t *back = n ? arc_image_decode(buf, n, &w, &h) : NULL;
    CHECK(back && w == 4 && h == 4 && memcmp(back, img, sizeof img) == 0); free(back);
}

/* ---- 0.8.7 planar reflections (HOSTLESS's request). A dark polished floor (pbr, metallic 0.45, roughness 0.05 or
   0.6) at y = 0, a grey pillar x = -1.5 (0..2) with a red cap (2..2.4), an emissive yellow ball at x = 1.5, y = 1.2,
   and a bright green box under the floor (y -1.2..-0.2, hidden by it). RPLAIN: nothing reflective, no plane;
   RZERO: the floor reflective but strength 0 (the same picture, pixel for pixel); RON: strength 1; RRUF: roughness
   0.6 (blurrier); RMASK: the floor's right half has vertex alpha 0 (no reflection there). ---- */
static arc_mesh v087_floor, v087_floorm, v087_cube, v087_ball;
static arc_mesh v087_plane(bool mask) {   /* 20 x 20 at y = 0, 20 x 20 quads; mask: vertex alpha 0 where x > 0 */
    enum { N = 20 }; static arc_vertex v[(N + 1) * (N + 1)]; static uint32_t idx[N * N * 6]; int k = 0;
    for (int j = 0; j <= N; j++) for (int i = 0; i <= N; i++) {
        arc_vertex *q = &v[j * (N + 1) + i]; memset(q, 0, sizeof *q);
        q->px = -10 + 20.0f * (float)i / N; q->pz = -10 + 20.0f * (float)j / N; q->ny = 1;
        q->color = (mask && q->px > 0.01f ? 0x00000000u : 0xFF000000u) | 0x303030u;
    }
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        uint32_t a = (uint32_t)(j * (N + 1) + i), b = a + N + 1, c = a + 1, d = b + 1;
        idx[k++] = a; idx[k++] = b; idx[k++] = c; idx[k++] = c; idx[k++] = b; idx[k++] = d;
    }
    return arc_mesh_create(v, (N + 1) * (N + 1), idx, k);
}
static void v087r_draw(int look) {
    if (!v087_floor.id) {
        v087_floor = v087_plane(false); v087_floorm = v087_plane(true);
        v087_cube = arc_mesh_cube(0xFFFFFF); v087_ball = arc_mesh_sphere(0xFFFFFF, 16);
        arc_material e; memset(&e, 0, sizeof e); e.emissive_rgb[0] = 1.0f; e.emissive_rgb[1] = 0.9f; e.emissive_rgb[2] = 0.2f;
        arc_mesh_set_material(v087_ball, &e);
    }
    arc_material fm; memset(&fm, 0, sizeof fm); fm.pbr = true; fm.metallic = 0.45f; fm.roughness = look == L_RRUF ? 0.6f : 0.05f;
    arc_mesh fl = look == L_RMASK ? v087_floorm : v087_floor;
    arc_mesh_set_material(fl, &fm);
    arc_mesh_set_reflective(fl, look == L_RPLAIN ? 0.0f : 1.0f);
    arc_reflection_plane(0, look == L_RPLAIN || look == L_RZERO ? 0.0f : 1.0f, 0.5f);
    arc_set_post(NULL); arc_set_sky_ex(0x101018, 0x101018, 0x101018); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    arc_set_shadow_cascades(1, 0); arc_set_shadow_softness(0);
    arc_set_light(arc_v3_make(0.3f, 1, 0.5f), 0xFFFFFF, 0x404040);
    arc_camera cam = { { 0, 2.0f, 7 }, { 0, 0.6f, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
    arc_set_camera(&cam);
    arc_draw_mesh(fl, arc_m4_identity(), 0xFFFFFF);
    arc_draw_mesh(v087_cube, arc_m4_trs_y(arc_v3_make(-1.5f, 1.0f, 0), 0.4f, arc_v3_make(0.8f, 2.0f, 0.8f)), 0xA0A0A0);
    arc_draw_mesh(v087_cube, arc_m4_trs_y(arc_v3_make(-1.5f, 2.2f, 0), 0.4f, arc_v3_make(0.9f, 0.4f, 0.9f)), 0xFF2020);
    arc_draw_mesh(v087_ball, arc_m4_trs_y(arc_v3_make(1.5f, 1.2f, 0), 0, arc_v3_make(0.8f, 0.8f, 0.8f)), 0x202020);
    arc_draw_mesh(v087_cube, arc_m4_trs_y(arc_v3_make(0.2f, -0.7f, 1.0f), 0, arc_v3_make(1.5f, 1.0f, 1.5f)), 0x00FF00);
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static int v087_project(float x, float y, float z, int *py) {   /* the look camera's pixel for a world point */
    arc_camera cam = { { 0, 2.0f, 7 }, { 0, 0.6f, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
    arc_m4 vp = arc_camera_view_proj(&cam, (float)W, (float)H);
    arc_v4 c = arc_m4_mul_v4(vp, (arc_v4){ x, y, z, 1 });
    *py = (int)((0.5f - c.y / c.w * 0.5f) * (float)H); return (int)((c.x / c.w * 0.5f + 0.5f) * (float)W);
}
static bool v087_red(int look, int x, int y) { const uint8_t *p = px(look, x, y); return p[0] > 90 && p[0] > 2 * p[1] + 20 && p[0] > 2 * p[2] + 20; }
static double v087_edges(int look, int x0, int x1, int y0, int y1) {   /* mean neighbour difference in a box */
    long s = 0; int n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { s += abs(lum8(look, x, y) - lum8(look, x + 1, y)) + abs(lum8(look, x, y) - lum8(look, x, y + 1)); n++; }
    return n ? (double)s / n : 0;
}
static void v087r_check(void) {
    int same = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (diff(L_RZERO, L_RPLAIN, x, y) == 0) same++;
    CHECK(same == W * H);   /* strength 0: exactly the frame without reflections */
    /* the pillar's column: the real cap above the floor line, its reflection below, further from the line than the
       reflected pillar (upright mirror image: the cap at the far end) */
    int fy, cx = v087_project(-1.5f, 0, 0, &fy), cy_top, cy_ref; v087_project(-1.5f, 2.2f, 0, &cy_top); v087_project(-1.5f, -2.2f, 0, &cy_ref);
    int red_above = 0, red_below = 0, red_between = 0;
    for (int y = 0; y < H; y++) for (int x = cx - 2; x <= cx + 2; x++) {
        if (!v087_red(L_RON, x, y)) continue;
        if (y < fy) red_above++; else if (abs(y - cy_ref) <= 6) red_below++; else red_between++;
    }
    printf("ARC note reflection: floor line y %d, cap at %d, its reflection at %d: red above %d, at the reflection %d, elsewhere below %d\n", fy, cy_top, cy_ref, red_above, red_below, red_between);
    CHECK(red_above > 0 && red_below > 0 && red_between < red_below);
    int plain_red = 0; for (int y = fy; y < H; y++) for (int x = cx - 2; x <= cx + 2; x++) plain_red += v087_red(L_RPLAIN, x, y);
    CHECK(plain_red == 0);   /* (no red below the line without the reflection) */
    /* the emissive ball's reflection: bright yellow below it */
    int by, bx = v087_project(1.5f, -1.2f, 0, &by), bright = 0;
    for (int y = by - 3; y <= by + 3; y++) for (int x = bx - 3; x <= bx + 3; x++) { const uint8_t *q = px(L_RON, x, y); if (q[0] > 120 && q[1] > 100 && q[2] < q[0] / 2) bright++; }
    CHECK(bright > 10);
    /* nothing from below the plane: no green anywhere */
    int green = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { const uint8_t *q = px(L_RON, x, y); if (q[1] > 80 && q[1] > 2 * q[0] && q[1] > 2 * q[2]) green++; }
    CHECK(green == 0);
    /* rougher: the reflection's edges soften */
    double sharp = v087_edges(L_RON, cx - 20, bx + 20, fy + 4, arc_mini(H - 2, cy_ref + 10)), soft = v087_edges(L_RRUF, cx - 20, bx + 20, fy + 4, arc_mini(H - 2, cy_ref + 10));
    printf("ARC note reflection edges: roughness 0.05 %.2f, 0.6 %.2f\n", sharp, soft);
    CHECK(soft < sharp * 0.7);
    /* vertex alpha 0 on the right half: the ball's reflection gone, the pillar's kept */
    int bright_m = 0; for (int y = by - 3; y <= by + 3; y++) for (int x = bx - 3; x <= bx + 3; x++) { const uint8_t *q = px(L_RMASK, x, y); if (q[0] > 120 && q[1] > 100 && q[2] < q[0] / 2) bright_m++; }
    int red_m = 0; for (int y = cy_ref - 6; y <= cy_ref + 6; y++) for (int x = cx - 2; x <= cx + 2; x++) red_m += v087_red(L_RMASK, x, y);
    CHECK(bright_m == 0 && red_m > 0);
}

/* ---- 0.8.8. TILEOLD / TILENEW (Moonbase Raid's report): floor tiles 1 x 1, each its own quad with UVs 0..1, a
   regolith-like texture that does NOT repeat (clamped), base hex tiling rotate 0, seen from above (1 tile = 32
   pixels). Old way (mesh UVs): every tile shows the same cells and its edges don't meet. New (base_scale 0.37): one
   texture across the tiles: low autocorrelation at one tile's lag, no seams at the tile edges.
   PAINT: four quads, two paint schemes interleaved (red/blue, green/yellow), mask: top-left red channel, top-right
   green, bottom black (keeps the grey texture). PSKIN: a skinned cube moved by its pose (x + 1) drawn with
   arc_draw_mesh_posed_styled: paint (top red, bottom blue) and a yellow outline. ---- */
static arc_texture v088_rego, v088_grey, v088_mask, v088_mask2, v088_mask3; static arc_mesh v088_tile, v088_quad, v088_skin; static arc_anim v088_anim;
static uint8_t *v088_put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); return p + 4; }
static uint8_t *v088_putf(uint8_t *p, float v) { memcpy(p, &v, 4); return p + 4; }
static void v088_init(void) {
    static uint8_t px[64 * 64 * 4];
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {   /* regolith stand-in: noise with dark crater dots, clamped */
        float n = 0.6f * arc_noise_fbm2((float)x / 9, (float)y / 9, 4, 21) + 0.4f * arc_noise_value2((float)x / 3, (float)y / 3, 22);
        float l = 50 + 180 * n; uint8_t *p = px + (y * 64 + x) * 4; p[0] = (uint8_t)l; p[1] = (uint8_t)(l * 0.95f); p[2] = (uint8_t)(l * 0.9f); p[3] = 255;
    }
    v088_rego = arc_texture_create_ex(64, 64, px, ARC_TEX_SMOOTH | ARC_TEX_MIPMAPS);
    for (int i = 0; i < 16 * 4; i++) px[i] = (i % 4 == 3) ? 255 : 0x99;
    v088_grey = arc_texture_create_ex(4, 4, px, 0);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {   /* paint: top-left R, top-right G, bottom black */
        uint8_t *p = px + (y * 8 + x) * 4; p[0] = (y < 4 && x < 4) ? 255 : 0; p[1] = (y < 4 && x >= 4) ? 255 : 0; p[2] = 0; p[3] = 255;
    }
    v088_mask = arc_texture_create_ex(8, 8, px, 0);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {   /* skinned cube: top half R, bottom half G */
        uint8_t *p = px + (y * 8 + x) * 4; p[0] = y < 4 ? 255 : 0; p[1] = y < 4 ? 0 : 255; p[2] = 0; p[3] = 255;
    }
    v088_mask2 = arc_texture_create_ex(8, 8, px, 0);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {   /* 0.8.9: top-left R, top-right G, bottom-right B, bottom-left black */
        uint8_t *p = px + (y * 8 + x) * 4; p[0] = (y < 4 && x < 4) ? 255 : 0; p[1] = (y < 4 && x >= 4) ? 255 : 0; p[2] = (y >= 4 && x >= 4) ? 255 : 0; p[3] = 255;
    }
    v088_mask3 = arc_texture_create_ex(8, 8, px, 0);
    arc_vertex t[4]; memset(t, 0, sizeof t);   /* a floor tile 0..1 x 0..1 at y = 0, UVs 0..1 */
    float tx[4] = { 0, 1, 1, 0 }, tz[4] = { 0, 0, 1, 1 };
    for (int i = 0; i < 4; i++) { t[i].px = tx[i]; t[i].pz = tz[i]; t[i].ny = 1; t[i].color = 0xFFFFFFFFu; t[i].u = tx[i]; t[i].v = tz[i]; }
    uint32_t ti[6] = { 0, 3, 1, 1, 3, 2 }; v088_tile = arc_mesh_create(t, 4, ti, 6); arc_mesh_set_texture(v088_tile, v088_rego);
    v088_quad = ct_quad(); arc_mesh_set_texture(v088_quad, v088_grey);
    /* a skinned unit cube (24 vertices, every one on joint 1) as .arcm v2, and the 2-joint "slide" clip */
    static const float fn[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    arc_vertex cv[24]; uint16_t ci[36]; int nv = 0, ni = 0;
    for (int f = 0; f < 6; f++) {
        arc_v3 n = arc_v3_make(fn[f][0], fn[f][1], fn[f][2]);
        arc_v3 up = fabsf(n.y) > 0.5f ? arc_v3_make(0, 0, -n.y) : arc_v3_make(0, 1, 0), rt = arc_v3_cross(up, n);   /* right x up = n */
        for (int k = 0; k < 4; k++) {
            float su = (k == 1 || k == 2) ? 1.0f : -1.0f, sv = k >= 2 ? -1.0f : 1.0f;   /* 0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left */
            arc_v3 p = arc_v3_add(arc_v3_scale(n, 0.5f), arc_v3_add(arc_v3_scale(rt, 0.5f * su), arc_v3_scale(up, 0.5f * sv)));
            arc_vertex *v = &cv[nv + k]; memset(v, 0, sizeof *v);
            v->px = p.x; v->py = p.y; v->pz = p.z; v->nx = n.x; v->ny = n.y; v->nz = n.z; v->color = 0xFFFFFFFFu; v->u = su > 0 ? 1.0f : 0.0f; v->v = sv > 0 ? 0.0f : 1.0f;
        }
        uint16_t b = (uint16_t)nv; uint16_t q[6] = { b, (uint16_t)(b + 3), (uint16_t)(b + 2), b, (uint16_t)(b + 2), (uint16_t)(b + 1) };
        memcpy(ci + ni, q, sizeof q); nv += 4; ni += 6;
    }
    static uint8_t mf[32 + 24 * 36 + 24 * 8 + 36 * 2]; uint8_t *p = mf; memcpy(p, "ARCM", 4); p += 4;
    p = v088_put32(p, 2); p = v088_put32(p, 24); p = v088_put32(p, 36); p = v088_put32(p, 1 | 4); p = v088_put32(p, 0); p = v088_put32(p, 0); p = v088_put32(p, 0);
    for (int i = 0; i < 24; i++) { memcpy(p, &cv[i], 36); p += 36; }
    for (int i = 0; i < 24; i++) { p[0] = 1; p[1] = p[2] = p[3] = 0; p[4] = 255; p[5] = p[6] = p[7] = 0; p += 8; }
    for (int i = 0; i < 36; i++) { memcpy(p, &ci[i], 2); p += 2; }
    v088_skin = arc_mesh_from_memory(mf, sizeof mf);
    static uint8_t ar[4096]; p = ar; memcpy(p, "ARCA", 4); p += 4; p = v088_put32(p, 1); p = v088_put32(p, 2); p = v088_put32(p, 1);
    arc_m4 id = arc_m4_identity(); for (int i = 0; i < 16; i++) p = v088_putf(p, id.m[i]);
    const char *names[2] = { "root", "hand" };
    for (int j = 0; j < 2; j++) {
        memset(p, 0, 32); strcpy((char *)p, names[j]); p += 32; p = v088_put32(p, (uint32_t)(j - 1));
        for (int i = 0; i < 16; i++) p = v088_putf(p, id.m[i]);
        float trs[10] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }; for (int i = 0; i < 10; i++) p = v088_putf(p, trs[i]);
    }
    memset(p, 0, 32); strcpy((char *)p, "slide"); p += 32; p = v088_putf(p, 1.0f); p = v088_put32(p, 2); p = v088_put32(p, 0);
    for (int f = 0; f < 2; f++) for (int j = 0; j < 2; j++) { float trs[10] = { j == 1 ? 2.0f * (float)f : 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }; for (int i = 0; i < 10; i++) p = v088_putf(p, trs[i]); }
    v088_anim = arc_anim_from_memory(ar, (size_t)(p - ar));
}
static void v088_draw(int look) {
    if (!v088_rego.id) v088_init();
    arc_reflection_plane(0, 0, 0);
    arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    arc_set_shadow_cascades(1, 0); arc_set_shadow_softness(0);
    arc_set_light(arc_v3_make(0, 1, 0.0001f), 0x010101, 0xFFFFFF);
    arc_camera top = { { 0, 10, 0 }, { 0, 0, 0 }, { 0, 0, -1 }, 0, 4.0f, 0.1f, 50 };
    arc_camera front = { { 0, 0, 10 }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 4.0f, 0.1f, 50 };
    if (look == L_TILEOLD || look == L_TILENEW) {
        arc_material m; memset(&m, 0, sizeof m); m.base_tiling = ARC_TILE_HEX; m.base_rotate = 0;
        if (look == L_TILENEW) m.base_scale = 0.37f;
        arc_mesh_set_material(v088_tile, &m);
        arc_set_camera(&top);
        for (int z = -3; z < 3; z++) for (int x = -4; x < 4; x++) arc_draw_mesh(v088_tile, arc_m4_translate(arc_v3_make((float)x, 0, (float)z)), 0xFFFFFF);
    } else if (look == L_PAINT || look == L_PAINT3) {
        arc_set_camera(&front);
        for (int i = 0; i < 4; i++) {   /* schemes A B A B, left to right (PAINT3: A has tint3 orange, B tint3 0 = blue ignored) */
            arc_mesh_style st; memset(&st, 0, sizeof st); st.paint_mask = look == L_PAINT3 ? v088_mask3 : v088_mask;
            if (look == L_PAINT3 && !(i & 1)) st.tint3 = 0xF08010;
            st.tint = (i & 1) ? 0x20E020 : 0xE02020; st.tint2 = (i & 1) ? 0xE0E020 : 0x2020E0;
            arc_draw_mesh_styled(v088_quad, arc_m4_trs_y(arc_v3_make(-2.25f + 1.5f * (float)i, 0, 0), 0, arc_v3_make(1.3f, 1.3f, 1)), &st);
        }
    } else {
        arc_set_camera(&front);
        arc_anim_layer L = { 0, 0.5f, 1, false }; arc_pose pose; arc_anim_pose(v088_anim, &L, 1, &pose);   /* hand at x = 1 */
        arc_mesh_style st; memset(&st, 0, sizeof st);
        st.paint_mask = v088_mask2; st.tint = 0xE02020; st.tint2 = 0x2020E0; st.outline = 0.12f; st.outline_rgb = 0xFFFF00;
        arc_draw_mesh_posed_styled(v088_skin, arc_m4_scale(arc_v3_make(1.5f, 1.5f, 1.5f)), v088_anim, &pose, &st);
    }
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static double v088_corr(int look, int lx, int ly) {
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0; int n = 0;
    for (int y = 0; y + ly < H; y++) for (int x = 0; x + lx < W; x++) {
        double a = lum8(look, x, y), b = lum8(look, x + lx, y + ly);
        sa += a; sb += b; saa += a * a; sbb += b * b; sab += a * b; n++;
    }
    double ma = sa / n, mb = sb / n, va = saa / n - ma * ma, vb = sbb / n - mb * mb;
    return (va > 0 && vb > 0) ? (sab / n - ma * mb) / sqrt(va * vb) : 0;
}
static double v088_seams(int look) {   /* mean step across tile edges (x = 32 k) over the mean step elsewhere */
    double e = 0, o = 0; int ne = 0, no = 0;
    for (int y = 2; y < H - 2; y++) for (int x = 1; x + 1 < W; x++) {
        double d = abs(lum8(look, x, y) - lum8(look, x + 1, y));
        if ((x + 1) % 32 == 0) { e += d; ne++; } else { o += d; no++; }
    }
    return (ne && no && o > 0) ? (e / ne) / (o / no) : 0;
}
static bool v088_hue(int look, int x, int y, int ch) {   /* channel ch clearly the strongest */
    const uint8_t *p = px(look, x, y); int a = p[ch], b = p[(ch + 1) % 3], c = p[(ch + 2) % 3];
    return a > 120 && a > b + 60 && a > c + 60;
}
static void v088_check(void) {
    double co = v088_corr(L_TILEOLD, 32, 0), cn = v088_corr(L_TILENEW, 32, 0), so = v088_seams(L_TILEOLD), sn = v088_seams(L_TILENEW);
    printf("ARC note base hex on tiles: one tile's lag %.3f (mesh UVs) / %.3f (world), tile-edge steps x %.2f / %.2f\n", co, cn, so, sn);
    CHECK(co > 0.8); CHECK(fabs(cn) < 0.25); CHECK(sn < 1.35);
    /* paint: schemes A B A B; top-left = tint, top-right = tint2, bottom = the grey texture */
    for (int i = 0; i < 4; i++) {
        int cx = 96 + (int)((-2.25f + 1.5f * (float)i) * 32), ty = 64 - 10, by = 64 + 10;
        bool A = !(i & 1);
        CHECK(v088_hue(L_PAINT, cx - 10, ty, A ? 0 : 1));                                   /* red / green */
        const uint8_t *tr = px(L_PAINT, cx + 10, ty);
        CHECK(A ? v088_hue(L_PAINT, cx + 10, ty, 2) : (tr[0] > 150 && tr[1] > 150 && tr[2] < 90));   /* blue / yellow */
        const uint8_t *g = px(L_PAINT, cx, by); CHECK(abs(g[0] - g[1]) < 6 && abs(g[1] - g[2]) < 6 && g[0] > 120 && g[0] < 180);   /* grey kept */
    }
    /* posed + styled: the cube is at x = +1 (posed): red above, blue below; yellow outline around it; empty at x = -1 */
    int cx = 96 + 32 * 1 * 3 / 2;
    CHECK(v088_hue(L_PSKIN, cx, 64 - 12, 0) && v088_hue(L_PSKIN, cx, 64 + 12, 2));
    int yel = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { const uint8_t *q = px(L_PSKIN, x, y); if (q[0] > 180 && q[1] > 180 && q[2] < 80) yel++; }
    CHECK(yel > 40);
    CHECK(lum8(L_PSKIN, 96 - 48, 64) < 10);
    /* 0.8.9 tint3: scheme A's bottom-right orange, bottom-left grey; scheme B (tint3 0) both grey */
    for (int i = 0; i < 4; i++) {
        int cx = 96 + (int)((-2.25f + 1.5f * (float)i) * 32), by = 64 + 10;
        const uint8_t *bl = px(L_PAINT3, cx - 10, by), *br = px(L_PAINT3, cx + 10, by);
        CHECK(abs(bl[0] - bl[1]) < 6 && abs(bl[1] - bl[2]) < 6);
        if (!(i & 1)) CHECK(br[0] > 180 && br[1] > 80 && br[1] < br[0] - 50 && br[2] < 60);
        else CHECK(abs(br[0] - br[1]) < 6 && abs(br[1] - br[2]) < 6);
        CHECK(v088_hue(L_PAINT3, cx - 10, 64 - 10, (i & 1) ? 1 : 0));
    }
}
static void v102_tick(void);
static void tick(void) {
    frame++;
    if (look_now < L_COUNT) {   /* is the current look's picture on disk yet? then on to the next */
        char path[64]; look_path(path, sizeof path, look_now);
        FILE *f = fopen(path, "rb");
        if (f) {
            fclose(f); int w = 0, h = 0;
            img[look_now] = arc_image_load(path, &w, &h);
            if (img[look_now]) { CHECK(w == W && h == H); look_now++; }
        }
    }
    v102_tick();
    v072p_tick();   /* (after the look moved on: the editor shows in the look's first frame) */
    v080p_tick();
    v085p_tick();
    v082u_tick();
    if (look_now >= L_COUNT || frame > 1200) finish();   /* 1200 ticks: 20 s for a slow window */
}
static void draw(float alpha) {
    (void)alpha;
    int look = look_now; if (look >= L_COUNT) return;
    if (look == L_CTEX) { ct_draw(); char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path); return; }
    if (look == L_EDIT) { v072p_draw(); return; }
    if (look == L_LOBBY) { v080p_draw(); return; }
    if (look == L_LOBBY_ON) { v085p_draw(); return; }
    arc_set_world_mask((arc_texture){ 0 }, arc_v3_make(0, 0, 0), 1, 0, 0); arc_set_cutaway(arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 0), 0, 0, ARC_CUTAWAY_OFF);   /* 0.9.9 */
    if (look >= L_TILES) { v099_draw(look); return; }
    if (look >= L_TERRAIN) { v096_draw(look); return; }
    if (look >= L_LIGHTS) { v095_draw(look); return; }
    if (look >= L_TILEOLD) { v088_draw(look); return; }
    if (look >= L_RPLAIN) { v087r_draw(look); return; }
    if (look >= L_SBASE) { v084d_draw(look); return; }
    if (look >= L_AREP) { v084p_draw(look); return; }
    if (look >= L_UIWRAP) { v082u_draw(look); return; }
    if (look >= L_TPOFF) { v082p_draw(look); return; }
    if (look == L_ZFAR) { zfar_draw(); return; }
    if (look == L_OSKY) {   /* 0.7.2: the sky under an orthographic camera looking 30 degrees down: the below colour's side */
        arc_camera oc = { { 0, 5, 8.66f }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 6.0f, 0.1f, 100 };
        arc_set_camera(&oc); arc_set_post(NULL); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0); arc_set_sky_ex(0x0000FF, 0x808080, 0xFF0000);
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    if (look == L_OPLAIN || look == L_OSSAO) {   /* 0.7.2: ssao under an orthographic camera (its depth test is on clip z) */
        arc_camera oc = { { 0, 3.0f, 4.0f }, { 0, 0.3f, 0 }, { 0, 1, 0 }, 0, 3.0f, 0.1f, 50 };
        arc_set_camera(&oc); arc_set_light(arc_v3_make(-0.4f, 0.9f, 0.5f), 0xB0B0B0, 0x505050); arc_set_sky_ex(0x6080A0, 0xA0B0C0, 0x404040);
        arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
        arc_post p = { 0 }; p.exposure = 1.0001f; if (look == L_OSSAO) { p.ssao = 1; p.ssao_radius = 0.4f; } arc_set_post(&p);
        arc_draw_box(arc_v3_make(-3, -0.1f, -3), arc_v3_make(3, 0, 3), 0x808080);
        arc_draw_box(arc_v3_make(-0.5f, 0, -0.5f), arc_v3_make(0.5f, 1, 0.5f), 0xC04040);
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    if (look >= L_LOD) { v071p_draw(look); return; }
    if (look == L_SOFTT) {   /* soft particles drawn into a render target (its own depth), shown 1:1 */
        static arc_texture rt; if (!rt.id) rt = arc_target_create(W, H, false);
        v7_particles(look); arc_set_post(NULL); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0); arc_set_sky_ex(0x000000, 0x000000, 0x000000);
        arc_target_begin(rt, true, 0xFF000000);
        arc_set_camera(&SOCAM);
        arc_draw_box(arc_v3_make(-2, -1, -1), arc_v3_make(2, 0.6f, 0), 0x000000);
        arc_draw_particles();
        arc_target_end();
        arc_draw_texture_ex(rt, 0, 0, W, H, 0, 0, W, H, 0, 0, 0, ARC_WHITE);
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    if (look == L_FLIP || look == L_SOFT || look == L_SOFTP) {
        v7_particles(look);
        bool so = look != L_FLIP;
        arc_set_camera(so ? &SOCAM : &FCAM); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0); arc_set_sky_ex(0x000000, 0x000000, 0x000000);
        if (look == L_SOFTP) { arc_post p = { 0 }; p.exposure = 1.0001f; arc_set_post(&p); } else arc_set_post(NULL);   /* the window's own depth, and the post buffer's */
        if (so) arc_draw_box(arc_v3_make(-2, -1, -1), arc_v3_make(2, 0.6f, 0), 0x000000);   /* a black wall, front face z = 0 */
        arc_draw_particles();
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    if (look == L_DECAL) {   /* a grey floor (top y = -1) and wall (front z = -1); decals exactly on them */
        if (v7_spawned != look) { v7_spawned = look; arc_particles_clear(); arc_decals_clear();
            arc_decal_add(white4, arc_v3_make(-1, -1, 0.5f), arc_v3_make(0, 1, 0), 0.9f, 0.3f, 0xFF2020, 0);
            arc_decal_add(white4, arc_v3_make(1, 0.2f, -1), arc_v3_make(0, 0, 1), 0.9f, 0.5f, 0x2020FF, 0); }
        arc_set_camera(&DCAM); arc_post p = { 0 }; p.exposure = 1.0001f; arc_set_post(&p); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
        arc_set_light(arc_v3_make(0.3f, 0.8f, 0.6f), 0xC0C0C0, 0x404040); arc_set_sky_ex(0x000000, 0x000000, 0x000000);
        arc_draw_box(arc_v3_make(-3, -2, -3), arc_v3_make(3, -1, 3), 0x909090);
        arc_draw_box(arc_v3_make(-3, -1, -2), arc_v3_make(3, 3, -1), 0x909090);
        arc_draw_decals();
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    if (look == L_TRAIL) {   /* a straight trail from x = -3 to the head at x = 2.4; additive green on black */
        if (v7_spawned != look) { v7_spawned = look; arc_decals_clear(); }
        static arc_trail tr; arc_trail_clear(&tr); tr.life = 0.35f;
        for (int i = 0; i <= 18; i++) arc_trail_push(&tr, arc_v3_make(-3.0f + 0.3f * (float)i, 0, 0));
        arc_set_camera(&FCAM); arc_set_post(NULL); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0); arc_set_sky_ex(0x000000, 0x000000, 0x000000);
        arc_trail_style ts = { 0 }; ts.width = 0.5f; ts.color = 0x00FF00; ts.alpha = 1; ts.alpha_end = 0; ts.additive = true;
        arc_trail_draw(&tr, &ts);
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    if (look == L_PUSH) {
        arc_set_camera(&PCAM); arc_post p = { 0 }; p.exposure = 1.0001f; arc_set_post(&p); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
        arc_set_light(arc_v3_make(0.3f, 0.8f, 0.6f), 0xC0C0C0, 0x404040); arc_set_sky_ex(0x000000, 0x000000, 0x000000);
        arc_draw_box(arc_v3_make(-1, -1, -1), arc_v3_make(1, 1, 1), 0xC03030);
        arc_set_line_depth_push(0.6f);
        for (int k = -1; k <= 1; k++) arc_draw_line(arc_v3_make(-3, 0.3f + (float)k * 0.004f, 1.3f), arc_v3_make(3, 0.3f + (float)k * 0.004f, 1.3f), 0x00FF00);
        arc_set_line_depth_push(0);
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    if (look == L_DEPTH) {
        arc_set_camera(&PCAM); arc_post p = { 0 }; p.exposure = 1.0001f; arc_set_post(&p); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
        arc_set_light(arc_v3_make(0.3f, 0.8f, 0.6f), 0xC0C0C0, 0x404040); arc_set_sky_ex(0x000000, 0x000000, 0x000000);
        arc_draw_box(arc_v3_make(-1, -1, -1), arc_v3_make(1, 1, 1), 0xC03030);
        arc_mesh_style st; arc_v3 s5 = arc_v3_make(0.5f, 0.5f, 0.5f), plate = arc_v3_make(0.7f, 0.7f, 0.02f);
        memset(&st, 0, sizeof st); st.tint = 0x00FF00; st.depth = ARC_DEPTH_ON_TOP;            /* behind the box, shown on top */
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(-0.8f, 0.8f, -2), 0, s5), &st);
        memset(&st, 0, sizeof st); st.tint = 0x0000FF; st.depth = ARC_DEPTH_HIDDEN;            /* behind the box: shown */
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(0.8f, 0.8f, -2), 0, s5), &st);
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(2.3f, 0.6f, 0), 0, s5), &st);       /* in the open: not */
        memset(&st, 0, sizeof st); st.tint = 0xFFFF00; st.depth_pull = 0.002f;                 /* in the box's front face */
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(-0.5f, -0.5f, 0.99f), 0, plate), &st);   /* front face exactly z = 1 */
        st.tint = 0xFF00FF; st.depth_pull = -0.002f;
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(0.5f, -0.5f, 0.99f), 0, plate), &st);
        memset(&st, 0, sizeof st); st.tint = 0x00FFFF; st.depth = ARC_DEPTH_ON_TOP; st.order = 2;
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(-2.3f, 0.6f, 0), 0, s5), &st);
        st.tint = 0xFF8000; st.order = 1;
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(-2.3f, 0.6f, 0), 0, s5), &st);
        memset(&st, 0, sizeof st); st.tint = 0x808080;                                         /* plates in front, cubes after */
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(-2.3f, -0.6f, 1.5f), 0, plate), &st);
        st.depth = ARC_DEPTH_TEST_ONLY;
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(2.3f, -0.6f, 1.5f), 0, plate), &st);
        memset(&st, 0, sizeof st); st.tint = 0xFFFFFF; st.order = 1; st.emissive = 1;
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(-3.067f, -0.8f, 0), 0, s5), &st);   /* on the line of sight through each plate */
        arc_draw_mesh_styled(cube, arc_m4_trs_y(arc_v3_make(3.067f, -0.8f, 0), 0, s5), &st);
        memset(&st, 0, sizeof st); st.tint = 0xFFFFFF;                                         /* the same mesh with and without x-ray */
        arc_draw_mesh_styled(twobox, arc_m4_trs_y(arc_v3_make(-1.6f, 1.75f, 0), 0, arc_v3_make(1, 1, 1)), &st);
        st.xray = 1;
        arc_draw_mesh_styled(twobox, arc_m4_trs_y(arc_v3_make(1.6f, 1.75f, 0), 0, arc_v3_make(1, 1, 1)), &st);
        memset(&st, 0, sizeof st); st.tint = 0x808080; st.xray = 1;                            /* hidden behind the box */
        arc_draw_mesh_styled(ball, arc_m4_trs_y(arc_v3_make(0, 0, -2.5f), 0, arc_v3_make(0.9f, 0.9f, 0.9f)), &st);
        arc_draw_texture_ex(nine, 0, 0, 9, 9, 2, 2, 10, 10, 0, 0, 0, ARC_WHITE);   /* 0.6.5: a fill between two textured quads */
        arc_draw_rect(14, 2, 10, 10, 0x00FF00);                                       /* shares their batch */
        arc_draw_texture_ex(nine, 0, 0, 9, 9, 26, 2, 10, 10, 0, 0, 0, ARC_WHITE);
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    if (look == L_NINE) {
        arc_set_camera(&SCAM); arc_post p = { 0 }; p.exposure = 1.0001f; arc_set_post(&p); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
        arc_set_sky_ex(0x000000, 0x000000, 0x000000);
        arc_draw_nine(nine, 8, 8, 80, 48, 3, 12, 0xFFFFFF);
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    if (look >= L_NOSH) {   /* shadows: a near box and a big far one */
        arc_set_camera(&SCAM); arc_set_light(arc_v3_make(0.5f, 1.0f, 0.3f), 0xC0C0C0, 0x404040); arc_set_sky_ex(0x6080A0, 0xA0B0C0, 0x404040);
        arc_post p = { 0 }; p.exposure = 1.0001f; arc_set_post(&p);
        if (look != L_NOSH) arc_set_shadows(arc_v3_make(0, 0, 0), 3, 512); else arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
        arc_set_shadow_cascades(look >= L_CASC ? 3 : 1, 40); arc_set_shadow_softness(look == L_CASC_SOFT ? 1.0f : 0);
        arc_draw_box(arc_v3_make(-10, -0.1f, -30), arc_v3_make(10, 0, 8), 0x909090);
        arc_draw_box(arc_v3_make(-0.5f, 0, -0.5f), arc_v3_make(0.5f, 1, 0.5f), 0xC04040);
        arc_draw_box(arc_v3_make(-1.5f, 0, -17.5f), arc_v3_make(1.5f, 3, -14.5f), 0x4040C0);
        char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
        return;
    }
    arc_camera cam = { { 0, 1.6f, 3.2f }, { 0, 0.3f, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 50 };
    arc_set_camera(&cam); arc_set_light(arc_v3_make(-0.4f, 0.9f, 0.5f), 0xB0B0B0, 0x505050); arc_set_sky_ex(0x6080A0, 0xA0B0C0, 0x404040);
    arc_post p = { 0 }; p.exposure = 1.0001f;   /* post on, otherwise plain */
    if (look == L_SSAO) { p.ssao = 1; p.ssao_radius = 0.4f; }
    if (look == L_FXAA) p.fxaa = true;
    if (look == L_LUT_ID) p.lut = lut_id;
    if (look == L_LUT_INV) p.lut = lut_inv;
    if (look == L_CRT) p.crt_curve = 1;
    if (look == L_PIX) p.pixelate = 4;
    if (look == L_SCAN) p.scanlines = 1;
    if (look == L_ABER) p.aberration = 1;
    arc_set_post(&p);
    arc_draw_box(arc_v3_make(-3, -0.1f, -3), arc_v3_make(3, 0, 3), 0x808080);
    arc_draw_box(arc_v3_make(-0.5f, 0, -0.5f), arc_v3_make(0.5f, 1, 0.5f), 0xC04040);
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "arc-post-test", .width = W, .height = H, .init = init, .tick = tick, .draw = draw, .draw_top = v082u_top };   /* each look waits for its picture; headless --render-all needs ~2 frames a look */
}

/* ---- 0.9.5: local lights ---- */
static void v095_lights(void) {   /* six coloured points around the objects and two spots, over a dim sun */
    static const uint32_t col[6] = { 0xFF3020, 0x30FF40, 0x3060FF, 0xFFD030, 0xF040F0, 0x40F0F0 };
    for (int i = 0; i < 6; i++) {
        float a = (float)i * 1.0472f;
        arc_light_point(arc_v3_make(2.2f * cosf(a), 0.6f + 0.25f * (float)(i % 2), -1.5f + 1.6f * sinf(a)), col[i], 2.5f, 2.6f);
    }
    arc_light_spot(arc_v3_make(-1.2f, 2.5f, -0.5f), arc_v3_norm(arc_v3_make(0.3f, -1, -0.4f)), 0xFFFFFF, 3, 5, 0.25f, 0.4f);
    arc_light_spot(arc_v3_make(1.6f, 2.0f, -2.5f), arc_v3_norm(arc_v3_make(-0.2f, -1, 0.5f)), 0xFF9040, 3, 5, 0.3f, 0.45f);
}
static void v095_shadow_scene(int look) {   /* a box and balls on a floor before a wall, a point light and a spot */
    arc_camera cam = { { 0, 2.6f, 3.6f }, { 0, 0.3f, -1.5f }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 50 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.2f), 0x101010, 0x0A0A0C); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    int fl = look == L_LSH ? ARC_LIGHT_SHADOW : 0;
    arc_light_point_ex(arc_v3_make(-0.4f, 1.1f, -1.2f), 0xFFF0E0, 2.2f, 4.5f, fl);
    arc_light_spot_ex(arc_v3_make(1.9f, 2.4f, -0.6f), arc_v3_norm(arc_v3_make(-0.35f, -1, -0.45f)), 0x6080FF, 3, 5, 0.3f, 0.5f, fl);
    arc_draw_mesh(cube, arc_m4_trs_y(arc_v3_make(0, -0.1f, -1.5f), 0, arc_v3_make(7, 0.2f, 6)), 0xC0C0C0);
    arc_draw_mesh(cube, arc_m4_trs_y(arc_v3_make(0, 1.2f, -3.6f), 0, arc_v3_make(7, 2.6f, 0.2f)), 0xB0B0B0);
    arc_draw_mesh(cube, arc_m4_trs_y(arc_v3_make(0.35f, 0.3f, -1.5f), 0.4f, arc_v3_make(0.5f, 0.6f, 0.5f)), 0xE0E0E0);
    arc_draw_mesh(ball, arc_m4_trs_y(arc_v3_make(-1.2f, 0.3f, -2.0f), 0, arc_v3_make(0.6f, 0.6f, 0.6f)), 0xE0E0E0);
    arc_draw_mesh(v095_pball, arc_m4_trs_y(arc_v3_make(1.35f, 0.35f, -1.25f), 0, arc_v3_make(0.7f, 0.7f, 0.7f)), 0xD0D0D0);
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static void v095_draw(int look) {
    if (look == L_LSH || look == L_LSH0) { v095_shadow_scene(look); return; }
    arc_camera cam = { { 0, 2.8f, 4.0f }, { 0, 0.2f, -1.5f }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 50 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.2f), 0x202020, 0x0C0E12); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    v095_lights();
    arc_draw_mesh(cube, arc_m4_trs_y(arc_v3_make(0, -0.1f, -1.5f), 0, arc_v3_make(7, 0.2f, 6)), 0xC0C0C0);
    arc_draw_mesh(cube, arc_m4_trs_y(arc_v3_make(-1.0f, 0.4f, -1.6f), 0.5f, arc_v3_make(0.8f, 0.8f, 0.8f)), 0xE0E0E0);
    arc_draw_mesh(ball, arc_m4_trs_y(arc_v3_make(0.3f, 0.45f, -0.9f), 0, arc_v3_make(0.9f, 0.9f, 0.9f)), 0xE0E0E0);
    arc_draw_mesh(v095_pball, arc_m4_trs_y(arc_v3_make(1.2f, 0.45f, -2.0f), 0, arc_v3_make(0.9f, 0.9f, 0.9f)), 0xD0D0D0);
    if (look == L_MANY) {   /* a 10 x 10 grid of small lights over the floor, on top of the eight: far past the old 16 */
        for (int i = 0; i < 100; i++) {
            int gx = i % 10, gz = i / 10;
            uint32_t c = (uint32_t)(((gx * 25 + 30) << 16) | ((gz * 25 + 30) << 8) | (255 - gx * 12));
            arc_light_point(arc_v3_make(-3.0f + 0.66f * (float)gx, 0.25f, -4.3f + 0.6f * (float)gz), c, 1.6f, 0.7f);
        }
    }
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static void v095_check(void) {
    /* lit by the coloured lights: clearly brighter than the dim sun alone, and with colour in it */
    int lit = 0, coloured = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        const uint8_t *q = px(L_LIGHTS, x, y);
        int mx = q[0] > q[1] ? (q[0] > q[2] ? q[0] : q[2]) : (q[1] > q[2] ? q[1] : q[2]), mn = q[0] < q[1] ? (q[0] < q[2] ? q[0] : q[2]) : (q[1] < q[2] ? q[1] : q[2]);
        lit += mx > 90; coloured += mx - mn > 60;
    }
    CHECK(lit > W * H / 8);
    CHECK(coloured > W * H / 20);
    /* many: the grid adds light across the floor. Count the screen's 16 x 9 tiles that got clearly brighter than
       with the eight lights alone; nothing may get darker (lights only add) */
    int brighter = 0, darker = 0;
    for (int ty = 0; ty < 9; ty++) for (int tx = 0; tx < 16; tx++) {
        int up = 0;
        for (int y = ty * H / 9; y < (ty + 1) * H / 9; y++) for (int x = tx * W / 16; x < (tx + 1) * W / 16; x++) {
            const uint8_t *a = px(L_LIGHTS, x, y), *b = px(L_MANY, x, y);
            int d = (b[0] + b[1] + b[2]) - (a[0] + a[1] + a[2]);
            if (d > 45) up++;
            if (d < -6) darker++;
        }
        brighter += up > 4;
    }
    printf("ARC note many lights: %d of 144 tiles brighter, %d pixels darker\n", brighter, darker);
    CHECK(brighter > 50);
    CHECK(darker < W * H / 1000);
    /* light shadows: shadows only take light away; a fair part of the picture is in shadow (behind the box and balls,
       on floor and wall); and where both lights reach a pixel unblocked it looks as without shadows (no acne) */
    int shade = 0, brighter2 = 0, same = 0, lit2 = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        const uint8_t *a = px(L_LSH0, x, y), *b = px(L_LSH, x, y);
        int d = (b[0] + b[1] + b[2]) - (a[0] + a[1] + a[2]);
        if (d < -40) shade++;
        if (d > 9) brighter2++;
        if (a[0] + a[1] + a[2] > 150) { lit2++; if (abs(d) <= 6) same++; }
    }
    printf("ARC note light shadows: %d pixels in shadow, %d brighter, %d of %d lit pixels unchanged\n", shade, brighter2, same, lit2);
    CHECK(shade > W * H / 60);
    CHECK(brighter2 < W * H / 1000);
    CHECK(same > lit2 / 2);
}

/* ---- 0.9.6: terrain ---- */
static arc_terrain v096_ter; static arc_scatter v096_trees, v096_rocks; static arc_mesh v096_tree, v096_rock;
static arc_texture v096_layer(int kind) {   /* 64 x 64 ground textures made from noise: grass, dirt, rock (alpha = height), snow */
    static uint8_t px[64 * 64 * 4];
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        float n = arc_noise_fbm2((float)x * 0.18f, (float)y * 0.18f, 3, 11u + (uint32_t)kind), g = 0.75f + 0.5f * (n - 0.5f);
        static const float base[4][3] = { { 0.30f, 0.55f, 0.20f }, { 0.50f, 0.38f, 0.24f }, { 0.48f, 0.47f, 0.46f }, { 0.92f, 0.94f, 0.98f } };
        uint8_t *p = px + (y * 64 + x) * 4;
        for (int c = 0; c < 3; c++) p[c] = (uint8_t)arc_clampf(base[kind][c] * g * 255.0f, 0, 255);
        p[3] = (uint8_t)arc_clampf(kind == 2 ? n * 255.0f : 128.0f, 0, 255);
    }
    return arc_texture_create_ex(64, 64, px, ARC_TEX_MESH);
}
static void v096_init(void) {
    static float h[65 * 65];
    for (int j = 0; j < 65; j++) for (int i = 0; i < 65; i++) {
        float x = (float)i / 64.0f, z = (float)j / 64.0f;
        h[j * 65 + i] = 9.0f * (arc_noise_fbm2(x * 3.0f, z * 3.0f, 4, 5u) - 0.5f) + 3.0f * (1.0f - z) * x;
    }
    arc_texture lay[4] = { v096_layer(0), v096_layer(1), v096_layer(2), v096_layer(3) };
    v096_ter = arc_terrain_create(&(arc_terrain_desc){ .heights = h, .samples_x = 65, .samples_z = 65, .size_x = 16, .size_z = 16, .origin = { -8, 0, -12 },
        .chunk = 16, .splat_res = 128, .layers = { lay[0], lay[1], lay[2], lay[3] }, .layer_count = 4, .layer_scale = { 1.2f, 1.0f, 0.8f, 0.6f }, .splat_sharpness = 0.5f });
    for (int i = 0; i < 4; i++) arc_texture_destroy(lay[i]);   /* the terrain keeps copies */
    arc_splat_rule rules[3] = {
        { .layer = 1, .height_min = -5, .height_max = 0.4f, .blend = 0.6f },          /* dirt low down */
        { .layer = 2, .slope_min = 26, .slope_max = 90, .blend = 8 },                 /* rock on slopes */
        { .layer = 3, .height_min = 2.6f, .height_max = 50, .blend = 0.5f },          /* snow up high */
    };
    arc_terrain_splat_rules(v096_ter, rules, 3);
    v096_tree = arc_mesh_cone(0x2E6A30, 8); v096_rock = arc_mesh_sphere(0x8A8680, 8);
    v096_trees = arc_scatter_create(v096_ter, &(arc_scatter_desc){ .mesh = v096_tree, .density = 0.35f, .layer = 0, .scale_min = 0.35f, .scale_max = 0.7f, .sink = -0.45f, .seed = 3, .draw_distance = 40 });
    v096_rocks = arc_scatter_create(v096_ter, &(arc_scatter_desc){ .mesh = v096_rock, .density = 0.6f, .layer = 2, .slope_max = 70, .scale_min = 0.25f, .scale_max = 0.45f, .align = 1, .sink = 0.1f, .seed = 4, .draw_distance = 40 });
}
static int v096_trees_drawn;
static void v096_draw(int look) {
    arc_camera cam = { { 0, 9.0f, 5.0f }, { 0, 0.0f, -5 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 80 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.5f, 1, 0.3f), 0xFFF4E0, 0x40485A); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_set_shadows(arc_v3_make(0, 1, -4), 10, 1024);
    arc_terrain_draw(v096_ter, 0xFFFFFF);
    arc_scatter_draw(v096_trees); arc_scatter_draw(v096_rocks);
    v096_trees_drawn = arc_scatter_drawn(v096_trees);
    arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static void v096_check(void) {
    /* the ground shows each layer: green grass, brown dirt, grey rock, white snow; trees stand on the grass */
    int green = 0, brown = 0, grey = 0, white = 0, sky = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        const uint8_t *q = px(L_TERRAIN, x, y); int r = q[0], g = q[1], b = q[2];
        if (r + g + b < 12) { sky++; continue; }
        if (g > r + 15 && g > b + 15) green++;
        else if (r > g + 12 && g > b + 8) brown++;
        else if (r > 170 && g > 170 && b > 170) white++;
        else if (abs(r - g) < 14 && abs(g - b) < 18 && r > 50) grey++;
    }
    printf("ARC note terrain: green %d, brown %d, grey %d, white %d, empty %d; trees placed %d (drawn %d), rocks %d\n", green, brown, grey, white, sky,
           arc_scatter_count(v096_trees), v096_trees_drawn, arc_scatter_count(v096_rocks));
    CHECK(green > W * H / 20 && brown > W * H / 200 && grey > W * H / 100 && white > W * H / 200);
    CHECK(sky < W * H / 3);
    CHECK(arc_scatter_count(v096_trees) > 10 && v096_trees_drawn > 5 && arc_scatter_count(v096_rocks) > 5);
}

/* ---- 0.9.9: ground markers ---- */
static arc_mesh v099_ground, v099_box, v099_wall;
static float v099_h(void *ud, float x, float z) { (void)ud; return 0.2f * x + 0.12f * arc_sinf(z * 0.9f); }
static void v099_tiles_init(void);
static void v099_init(void) {   /* the sloped ground as a 24 x 24 grid mesh of the same function the markers follow */
    enum { N = 24 }; static arc_vertex v[(N + 1) * (N + 1)]; static uint32_t idx[N * N * 6]; int k = 0;
    for (int j = 0; j <= N; j++) for (int i = 0; i <= N; i++) {
        float x = -6 + 12.0f * (float)i / N, z = -14 + 12.0f * (float)j / N; arc_vertex *q = &v[j * (N + 1) + i]; memset(q, 0, sizeof *q);
        q->px = x; q->py = v099_h(NULL, x, z); q->pz = z; q->color = arc_vcol(0x8C8478);
    }
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        uint32_t a = (uint32_t)(j * (N + 1) + i), b = a + 1, c = a + N + 1, d = c + 1;
        idx[k++] = a; idx[k++] = c; idx[k++] = b; idx[k++] = b; idx[k++] = c; idx[k++] = d;
    }
    v099_ground = arc_mesh_create(v, (N + 1) * (N + 1), idx, k);
    v099_box = arc_mesh_cube(0xFFFFFF);
    v099_wall = arc_mesh_cube(0xFFFFFF); arc_mesh_set_cutaway(v099_wall, true);   /* (read when the frame is drawn, so set once) */
    v099_tiles_init();
}
static arc_mesh v099_quad[2]; static arc_texture v099_arr; static arc_gfx_stats v099_tstats;
static uint32_t v099_lcol(int i) { int r = 40 + (i * 53) % 200, g = 40 + (i * 97 + 60) % 200, b = 40 + (i * 151 + 120) % 200; return (uint32_t)(r << 16 | g << 8 | b); }
static void v099_tiles_init(void) {   /* 32 layers of 16 x 16: each its own colour with a white dot in the top-left quarter */
    arc_texture t[32]; uint8_t px16[16 * 16 * 4];
    for (int i = 0; i < 32; i++) {
        uint32_t c = v099_lcol(i);
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) { uint8_t *q = px16 + (y * 16 + x) * 4; bool dot = x < 4 && y < 4;
            q[0] = dot ? 255 : (uint8_t)(c >> 16); q[1] = dot ? 255 : (uint8_t)(c >> 8); q[2] = dot ? 255 : (uint8_t)c; q[3] = 255; }
        t[i] = arc_texture_create_ex(16, 16, px16, ARC_TEX_MESH);
    }
    v099_arr = arc_texture_array(t, 32);
    for (int i = 0; i < 32; i++) arc_texture_destroy(t[i]);
    arc_vertex v[4]; uint32_t idx[6] = { 0, 2, 1, 1, 2, 3 };   /* a 1 x 1 quad at y = 0, u along x, v along z */
    for (int k = 0; k < 4; k++) { memset(&v[k], 0, sizeof v[k]); v[k].px = (k & 1) ? 0.5f : -0.5f; v[k].pz = (k & 2) ? 0.5f : -0.5f; v[k].ny = 1; v[k].color = 0xFFFFFFFFu; v[k].u = (k & 1) ? 1.0f : 0.0f; v[k].v = (k & 2) ? 1.0f : 0.0f; }
    for (int m = 0; m < 2; m++) { v099_quad[m] = arc_mesh_create(v, 4, idx, 6); arc_mesh_set_texture_array(v099_quad[m], v099_arr); }
}
static int v099_tlayer(int x, int y) { return (x + y * 16) % 32; }
static int v099_tturn(int x, int y) { return (x * 3 + y) & 3; }
static void v099_tiles_draw(void) {
    arc_camera cam = { { 8, 16, 6.0f }, { 8, 0, 6.0f }, { 0, 0, -1 }, 0.9f, 0, 0.1f, 80 };   /* straight down: screen x = world x, screen y = world z */
    arc_set_camera(&cam); arc_set_light(arc_v3_make(0, 1, 0), 0xA0A0A0, 0x404040); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    static arc_tile_cell cells[16 * 12];
    for (int y = 0; y < 12; y++) for (int x = 0; x < 16; x++) cells[y * 16 + x] = (arc_tile_cell){ (uint8_t)((x + y) & 1), (uint8_t)v099_tlayer(x, y), (uint8_t)v099_tturn(x, y), 0, 0 };
    arc_draw_tiles(cells, 16, 12, 1.0f, arc_v3_make(0, 0, 0), v099_quad, 2);
    char path[64]; look_path(path, sizeof path, L_TILES); arc_screenshot_after_frame(path);
}
static bool v099_screen(float wx, float wz, int *sx, int *sy) {   /* where a ground point lands in the 192 x 128 look */
    arc_camera cam = { { 8, 16, 6.0f }, { 8, 0, 6.0f }, { 0, 0, -1 }, 0.9f, 0, 0.1f, 80 };
    arc_m4 vp = arc_camera_view_proj(&cam, (float)W, (float)H); arc_v4 c = arc_m4_mul_v4(vp, (arc_v4){ wx, 0, wz, 1 });
    if (c.w <= 0) return false;
    *sx = (int)((c.x / c.w * 0.5f + 0.5f) * W); *sy = (int)((0.5f - c.y / c.w * 0.5f) * H);
    return *sx >= 0 && *sy >= 0 && *sx < W && *sy < H;
}
static const arc_camera V099_CAM = { { 0, 2.6f, 0 }, { 0, 0, -9 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 80 };   /* low: the ground at about 16 degrees */
static const arc_camera V099_CCAM = { { 0, 6, 4 }, { 0, 0, -4 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 80 };     /* the cutaway looks */
static bool v099_proj(const arc_camera *cam, arc_v3 p, int *sx, int *sy) {
    arc_m4 vp = arc_camera_view_proj(cam, (float)W, (float)H); arc_v4 c = arc_m4_mul_v4(vp, (arc_v4){ p.x, p.y, p.z, 1 });
    if (c.w <= 0) return false;
    *sx = (int)((c.x / c.w * 0.5f + 0.5f) * W); *sy = (int)((0.5f - c.y / c.w * 0.5f) * H);
    return *sx >= 2 && *sy >= 2 && *sx < W - 2 && *sy < H - 2;
}
static arc_texture v099_mask;
static const float V099_FXX[3] = { -1.8f, 0.9f, 2.6f };   /* boxes: left out of the mask (unseen side), darkened, charred */
static void v099_fx_draw(int look) {
    arc_camera cam = V099_CAM; arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.5f), 0xFFF4E0, 0x40485A); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    if (!v099_mask.id) { const uint8_t m[8] = { 0, 0, 0, 255, 255, 255, 255, 255 }; v099_mask = arc_texture_create(2, 1, m, false); }
    if (look == L_FX) arc_set_world_mask(v099_mask, arc_v3_make(-8, 0, -20), 8, 0x000000, 0);   /* x < 0 unseen */
    arc_draw_mesh(v099_ground, arc_m4_identity(), 0xFFFFFF);
    for (int i = 0; i < 3; i++) {
        float x = V099_FXX[i]; arc_mesh_style st = { 0 }; st.tint = 0xD0D0D0;
        if (look == L_FX) { st.no_world_mask = i == 0; st.darken = i == 1 ? 1.0f : 0; st.charred = i == 2 ? 1.0f : 0; }
        arc_draw_mesh_styled(v099_box, arc_m4_trs_y(arc_v3_make(x, v099_h(NULL, x, -5) + 0.6f, -5), 0, arc_v3_make(1.2f, 1.2f, 1.2f)), &st);
    }
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static void v099_cut_draw(int look) {
    arc_camera cam = V099_CCAM; arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.5f), 0xFFF4E0, 0x40485A); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    if (look != L_CUT0) arc_set_cutaway(cam.eye, cam.target, 2.0f, 1.2f, look == L_CUT ? ARC_CUTAWAY_CLIP : ARC_CUTAWAY_DITHER);
    arc_draw_mesh(v099_wall, arc_m4_trs_y(arc_v3_make(0, 1.5f, -1), 0, arc_v3_make(7, 3, 0.4f)), 0xA0A0A0);   /* the wall */
    arc_draw_mesh(v099_box, arc_m4_trs_y(arc_v3_make(0, 0.5f, -4), 0, arc_v3_make(1, 1, 1)), 0xFF2020);      /* the unit: under keep height */
    arc_draw_mesh(v099_box, arc_m4_trs_y(arc_v3_make(0, -0.05f, -3), 0, arc_v3_make(10, 0.1f, 10)), 0x506050); /* the floor */
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
/* ---- 0.10.1: distance-field text, paint patterns ---- */
static arc_font v101_sdf, v101_ref[6]; static const float V101_PX[5] = { 11, 14, 17.5f, 23, 64 };
static const float V101_Y[5] = { 4, 22, 42, 66, 30 }, V101_X[5] = { 8, 8, 8, 8, 100 };
static arc_font v101_font(const char *name) {   /* from the build folder or the repository root */
    static const char *dirs[3] = { "../tests/fonts", "../../tests/fonts", "tests/fonts" };   /* build, build/postd3d, the root */
    arc_font f = { 0 };
    for (int i = 0; i < 3 && !f.id; i++) if (arc_file_exists(arc_fmt("%s/%s", dirs[i], name))) f = arc_font_load(arc_fmt("%s/%s", dirs[i], name));
    return f;
}
static arc_mesh v101_cube, v101_mech; static arc_anim v101_anim;
static void v101_box(arc_vertex *cv, uint8_t *sk, uint16_t *ci, int *nv, int *ni, arc_v3 c, arc_v3 h, int joint) {   /* a box on one joint */
    static const float fn[6][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
    for (int f = 0; f < 6; f++) {
        arc_v3 n = arc_v3_make(fn[f][0], fn[f][1], fn[f][2]);
        arc_v3 up = fabsf(n.y) > 0.5f ? arc_v3_make(0, 0, -n.y) : arc_v3_make(0, 1, 0), rt = arc_v3_cross(up, n);
        for (int k = 0; k < 4; k++) {
            float su = (k == 1 || k == 2) ? 1.0f : -1.0f, sv = k >= 2 ? -1.0f : 1.0f;
            arc_v3 q = arc_v3_add(n, arc_v3_add(arc_v3_scale(rt, su), arc_v3_scale(up, sv)));
            arc_vertex *v = &cv[*nv + k]; memset(v, 0, sizeof *v);
            v->px = c.x + q.x * h.x; v->py = c.y + q.y * h.y; v->pz = c.z + q.z * h.z; v->nx = n.x; v->ny = n.y; v->nz = n.z; v->color = 0xFFFFFFFFu;
            uint8_t *w = sk + (size_t)(*nv + k) * 8; w[0] = (uint8_t)joint; w[1] = w[2] = w[3] = 0; w[4] = 255; w[5] = w[6] = w[7] = 0;
        }
        uint16_t b = (uint16_t)*nv; uint16_t q6[6] = { b, (uint16_t)(b + 3), (uint16_t)(b + 2), b, (uint16_t)(b + 2), (uint16_t)(b + 1) };
        memcpy(ci + *ni, q6, sizeof q6); *nv += 4; *ni += 6;
    }
}
static void v101_init(void) {
    v101_sdf = v101_font("dejavu_sdf48.arcf");
    static const char *refs[6] = { "dejavu_11.arcf", "dejavu_14.arcf", "dejavu_17.arcf", "dejavu_18.arcf", "dejavu_23.arcf", "dejavu_64.arcf" };
    for (int i = 0; i < 6; i++) v101_ref[i] = v101_font(refs[i]);
    v101_cube = arc_mesh_cube(0xFFFFFF);
    /* the "mech": head, torso, two arms, two legs, each a box on its own joint (mixamo-like names) */
    static arc_vertex cv[6 * 24]; static uint8_t sk[6 * 24 * 8]; static uint16_t ci[6 * 36]; int nv = 0, ni = 0;
    v101_box(cv, sk, ci, &nv, &ni, arc_v3_make(0, 1.75f, 0), arc_v3_make(0.25f, 0.22f, 0.25f), 1);      /* Head */
    v101_box(cv, sk, ci, &nv, &ni, arc_v3_make(0, 1.15f, 0), arc_v3_make(0.45f, 0.35f, 0.3f), 2);       /* Spine */
    v101_box(cv, sk, ci, &nv, &ni, arc_v3_make(-0.65f, 1.15f, 0), arc_v3_make(0.17f, 0.35f, 0.17f), 3); /* LeftArm */
    v101_box(cv, sk, ci, &nv, &ni, arc_v3_make(0.65f, 1.15f, 0), arc_v3_make(0.17f, 0.35f, 0.17f), 4);  /* RightArm */
    v101_box(cv, sk, ci, &nv, &ni, arc_v3_make(-0.25f, 0.4f, 0), arc_v3_make(0.18f, 0.4f, 0.2f), 5);    /* LeftUpLeg */
    v101_box(cv, sk, ci, &nv, &ni, arc_v3_make(0.25f, 0.4f, 0), arc_v3_make(0.18f, 0.4f, 0.2f), 6);     /* RightUpLeg */
    static uint8_t mf[32 + 144 * 36 + 144 * 8 + 216 * 2]; uint8_t *p = mf; memcpy(p, "ARCM", 4); p += 4;
    p = v088_put32(p, 2); p = v088_put32(p, (uint32_t)nv); p = v088_put32(p, (uint32_t)ni); p = v088_put32(p, 1 | 4); p = v088_put32(p, 0); p = v088_put32(p, 0); p = v088_put32(p, 0);
    for (int i = 0; i < nv; i++) { memcpy(p, &cv[i], 36); p += 36; }
    memcpy(p, sk, (size_t)nv * 8); p += nv * 8;
    for (int i = 0; i < ni; i++) { memcpy(p, &ci[i], 2); p += 2; }
    v101_mech = arc_mesh_from_memory(mf, (size_t)(p - mf));
    static uint8_t ar[8192]; p = ar; memcpy(p, "ARCA", 4); p += 4; p = v088_put32(p, 1); p = v088_put32(p, 7); p = v088_put32(p, 1);
    arc_m4 id = arc_m4_identity(); for (int i = 0; i < 16; i++) p = v088_putf(p, id.m[i]);
    const char *names[7] = { "Hips", "Head", "Spine1", "LeftArm", "RightForeArm", "LeftUpLeg", "RightFoot" };
    for (int j = 0; j < 7; j++) {
        memset(p, 0, 32); strcpy((char *)p, names[j]); p += 32; p = v088_put32(p, (uint32_t)(j ? 0 : -1));
        for (int i = 0; i < 16; i++) p = v088_putf(p, id.m[i]);
        float trs[10] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }; for (int i = 0; i < 10; i++) p = v088_putf(p, trs[i]);
    }
    memset(p, 0, 32); strcpy((char *)p, "still"); p += 32; p = v088_putf(p, 1.0f); p = v088_put32(p, 1); p = v088_put32(p, 0);
    for (int j = 0; j < 7; j++) { float trs[10] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }; for (int i = 0; i < 10; i++) p = v088_putf(p, trs[i]); }
    v101_anim = arc_anim_from_memory(ar, (size_t)(p - ar));
}
/* 0.10.6: three shoulder pads, each a box on its own joint (Hips is joint 0) */
static arc_mesh v106_pads; static arc_anim v106_anim;
static void v106_init(void) {
    static arc_vertex cv[3 * 24]; static uint8_t sk[3 * 24 * 8]; static uint16_t ci[3 * 36]; int nv = 0, ni = 0;
    for (int i = 0; i < 3; i++) v101_box(cv, sk, ci, &nv, &ni, arc_v3_make(-1.6f + 1.6f * (float)i, 0, 0), arc_v3_make(0.5f, 0.5f, 0.5f), i + 1);
    static uint8_t mf[32 + 72 * 36 + 72 * 8 + 108 * 2]; uint8_t *p = mf; memcpy(p, "ARCM", 4); p += 4;
    p = v088_put32(p, 2); p = v088_put32(p, (uint32_t)nv); p = v088_put32(p, (uint32_t)ni); p = v088_put32(p, 1 | 4); p = v088_put32(p, 0); p = v088_put32(p, 0); p = v088_put32(p, 0);
    for (int i = 0; i < nv; i++) { memcpy(p, &cv[i], 36); p += 36; }
    memcpy(p, sk, (size_t)nv * 8); p += nv * 8;
    for (int i = 0; i < ni; i++) { memcpy(p, &ci[i], 2); p += 2; }
    v106_pads = arc_mesh_from_memory(mf, (size_t)(p - mf));
    static uint8_t ar[4096]; p = ar; memcpy(p, "ARCA", 4); p += 4; p = v088_put32(p, 1); p = v088_put32(p, 4); p = v088_put32(p, 1);
    arc_m4 id = arc_m4_identity(); for (int i = 0; i < 16; i++) p = v088_putf(p, id.m[i]);
    const char *names[4] = { "Hips", "mixamorig:LeftShoulder", "clavicle_r", "Pauldron_L" };
    for (int j = 0; j < 4; j++) {
        memset(p, 0, 32); strcpy((char *)p, names[j]); p += 32; p = v088_put32(p, (uint32_t)(j ? 0 : -1));
        for (int i = 0; i < 16; i++) p = v088_putf(p, id.m[i]);
        float trs[10] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }; for (int i = 0; i < 10; i++) p = v088_putf(p, trs[i]);
    }
    memset(p, 0, 32); strcpy((char *)p, "still"); p += 32; p = v088_putf(p, 1.0f); p = v088_put32(p, 1); p = v088_put32(p, 0);
    for (int j = 0; j < 4; j++) { float trs[10] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1 }; for (int i = 0; i < 10; i++) p = v088_putf(p, trs[i]); }
    v106_anim = arc_anim_from_memory(ar, (size_t)(p - ar));
}
static void v102_draw(int look);
static void v102_check(void);
static void v101_draw(int look) {
    if (look >= L_VPREF && look <= L_VPSPLIT) { v102_draw(look); return; }
    if (!v101_cube.id) v101_init();
    arc_reflection_plane(0, 0, 0); arc_set_post(NULL); arc_set_sky_ex(0x000000, 0x000000, 0x000000); arc_set_shadows(arc_v3_make(0, 0, 0), 0, 0);
    char path[64]; look_path(path, sizeof path, look);
    if (look == L_SDF || look == L_SDFREF) {
        arc_camera c0 = { { 0, 0, 5 }, { 0, 0, 0 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 50 }; arc_set_camera(&c0);
        for (int i = 0; i < 5; i++) {
            if (look == L_SDF) arc_font_draw_ex(v101_sdf, V101_X[i], V101_Y[i], V101_PX[i] / 48.0f, 0, 0xFFFFFF, "II");
            else if (i == 2) { arc_font_draw_ex(v101_ref[2], V101_X[i], V101_Y[i], 1, 0, 0xFFFFFF, "II"); arc_font_draw_ex(v101_ref[3], V101_X[i] + 40, V101_Y[i], 1, 0, 0xFFFFFF, "II"); }
            else arc_font_draw_ex(v101_ref[i == 4 ? 5 : i > 2 ? 4 : i], V101_X[i], V101_Y[i], 1, 0, 0xFFFFFF, "II");
        }
        arc_screenshot_after_frame(path); return;
    }
    arc_set_light(arc_v3_make(0.2f, 0.4f, 1), 0x707070, 0x909090); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_camera front = { { 0, 0, 20 }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 6.0f, 0.1f, 50 }; arc_set_camera(&front);
    arc_mesh_style st; memset(&st, 0, sizeof st); st.tint = 0xE02020; st.pattern.rgb = 0x2040E0;
    if (look == L_PAT) {   /* top row: stripes (crossed), checker, dots, split, noise on cubes; checker again turned to show its top face */
        static const int kinds[5] = { ARC_PAINT_STRIPES, ARC_PAINT_CHECKER, ARC_PAINT_DOTS, ARC_PAINT_SPLIT, ARC_PAINT_NOISE };
        for (int i = 0; i < 5; i++) {
            st.pattern.kind = kinds[i]; st.pattern.scale = i == 4 ? 0.35f : i == 2 ? 0.5f : 0.25f; st.pattern.width = i == 2 ? 0.3f : 0.5f; st.pattern.cross = i == 0; st.pattern.seed = 7;
            arc_draw_mesh_styled(v101_cube, arc_m4_trs_y(arc_v3_make(-3.6f + 1.8f * (float)i, 1.6f, 0), 0, arc_v3_make(1.5f, 1.5f, 1.5f)), &st);
        }
        st.pattern.kind = ARC_PAINT_CHECKER; st.pattern.scale = 0.25f; st.pattern.cross = false;
        arc_m4 turned = arc_m4_mul(arc_m4_translate(arc_v3_make(-3.6f, -1.4f, 0)), arc_m4_mul(arc_m4_rotate_x(-1.5707963f), arc_m4_scale(arc_v3_make(1.5f, 1.5f, 1.5f))));
        arc_draw_mesh_styled(v101_cube, turned, &st);
    }
    if (look == L_PATARM) {   /* 0.10.6: the mech with arms only (no stripes on the torso) above three pads with arms only */
        if (!v106_pads.id) v106_init();
        arc_paint_parts_rule("*pauldron*", ARC_PART_ARMS);
        arc_anim_layer L = { 0, 0, 1, false }; arc_pose pose, pp; arc_anim_pose(v101_anim, &L, 1, &pose); arc_anim_pose(v106_anim, &L, 1, &pp);
        arc_mesh_style ms = st; ms.anim = v101_anim; ms.pose = &pose;
        ms.pattern.kind = ARC_PAINT_STRIPES; ms.pattern.scale = 0.2f; ms.pattern.width = 0.5f; ms.pattern.angle = 0.7853982f; ms.pattern.cross = false; ms.pattern.part_mask = ARC_PART_ARMS;
        arc_draw_mesh_styled(v101_mech, arc_m4_translate(arc_v3_make(0, -0.4f, 0)), &ms);
        ms.anim = v106_anim; ms.pose = &pp;
        arc_draw_mesh_styled(v106_pads, arc_m4_translate(arc_v3_make(0, -2.2f, 0)), &ms);
        arc_screenshot_after_frame(path);
        return;
    }
    {   /* the skinned mech, stripes: all parts (PAT) or legs only (PATLEG) */
        arc_anim_layer L = { 0, 0, 1, false }; arc_pose pose; arc_anim_pose(v101_anim, &L, 1, &pose);
        arc_mesh_style ms = st; ms.anim = v101_anim; ms.pose = &pose;
        ms.pattern.kind = ARC_PAINT_STRIPES; ms.pattern.scale = 0.2f; ms.pattern.width = 0.5f; ms.pattern.angle = 0.7853982f; ms.pattern.cross = false;
        ms.pattern.part_mask = look == L_PATLEG ? ARC_PART_LEGS : 0;
        arc_draw_mesh_styled(v101_mech, arc_m4_translate(arc_v3_make(look == L_PAT ? 1.6f : 0, -2.9f, 0)), &ms);
    }
    arc_screenshot_after_frame(path);
}
static int v101_stem(int look, int x0, int x1, int y, float *width, int *partial) {   /* the first stem's coverage (px) and its partly covered pixels */
    float sum = 0; int part = 0, in = 0, end = x1;
    for (int x = x0; x < x1; x++) {
        float v = (float)px(look, x, y)[0] / 255.0f;
        if (v > 0.05f) { in = 1; sum += v; part += v < 0.92f; }
        else if (in) { end = x; break; }
    }
    *width = sum; *partial = part; return end;
}
static bool v101_blue(const uint8_t *q) { return q[2] > q[0] + 30; }
static bool v101_red(const uint8_t *q) { return q[0] > q[2] + 30; }
static void v101_check(void) {
    /* text: stem widths at each size against bitmaps baked at that size; the edge no softer than a pixel */
    if (!v101_sdf.id) { printf("ARC note sdf: test fonts not found (tests/fonts)\n"); CHECK(false); }
    for (int i = 0; i < 5; i++) {
        float h = V101_PX[i] * 0.45f; int y = (int)(V101_Y[i] + V101_PX[i] * 0.42f + h * 0.5f);
        float ws, wr, wr2 = 0; int ps, pr, pr2 = 0;
        v101_stem(L_SDF, (int)V101_X[i] - 2, (int)V101_X[i] + 40, y, &ws, &ps);
        v101_stem(L_SDFREF, (int)V101_X[i] - 2, (int)V101_X[i] + 40, y, &wr, &pr);
        if (i == 2) v101_stem(L_SDFREF, (int)V101_X[i] + 38, (int)V101_X[i] + 80, y, &wr2, &pr2);
        float want = i == 2 ? (wr + wr2) * 0.5f : wr;
        printf("ARC note sdf %.1f px: stem %.2f px (bitmap %.2f%s), partly covered pixels %d (bitmap %d)\n", V101_PX[i], ws, want, i == 2 ? ", between 17 and 18" : "", ps, pr);
        CHECK(fabsf(ws - want) <= 1.0f && ps <= 2 + (i == 2 ? arc_maxi(pr, pr2) : pr) && ws > 0.5f);
    }
    /* patterns on the cubes: (cube centres at x -3.6 + 1.8 i, y 1.6; 6 units tall over 128 px) */
    float k = 128.0f / 6.0f; int cy = (int)(64 - 1.6f * k);
    #define V101_CX(i) ((int)(96 + (-3.6f + 1.8f * (float)(i)) * k))
    int blue = 0, red = 0;
    for (int y = cy - 14; y <= cy + 14; y++) for (int x = V101_CX(0) - 14; x <= V101_CX(0) + 14; x++) { blue += v101_blue(px(L_PAT, x, y)); red += v101_red(px(L_PAT, x, y)); }
    CHECK(blue > 100 && red > 50);   /* crossed stripes: both colours */
    /* checker: a cell's run lengths across and down, on the front face and (turned cube) the top face */
    int runs[2][2];
    for (int t = 0; t < 2; t++) {
        int ccx = V101_CX(t ? 0 : 1), ccy = t ? (int)(64 + 1.4f * k) : cy;
        for (int dir = 0; dir < 2; dir++) {   /* the blue run through the face's centre region, longest found */
            int best = 0;
            for (int o = -6; o <= 6; o += 3) {
                int run = 0;
                for (int s2 = -15; s2 <= 15; s2++) {
                    int x = dir ? ccx + o : ccx + s2, y = dir ? ccy + s2 : ccy + o;
                    if (v101_blue(px(L_PAT, x, y))) { run++; if (run > best && run < 12) best = run; } else run = 0;
                }
            }
            runs[t][dir] = best;
        }
    }
    printf("ARC note pattern checker cells: front face %dx%d px, top face %dx%d px (0.25 units = %.1f px)\n", runs[0][0], runs[0][1], runs[1][0], runs[1][1], 0.25f * 1.5f * k);
    CHECK(runs[0][0] >= 6 && abs(runs[0][0] - runs[0][1]) <= arc_maxi(1, runs[0][0] / 10) && runs[1][0] >= 6 && abs(runs[1][0] - runs[1][1]) <= arc_maxi(1, runs[1][0] / 10));
    /* dots: one dot's width and height */
    {
        int bx0 = 999, bx1 = -1, by0 = 999, by1 = -1, ccx = V101_CX(2);
        /* flood from the blue pixel nearest the cell centre at (cube centre + half a cell) */
        int sx = ccx, sy = cy;   /* the blue pixel nearest the face's centre */
        for (int r = 0, found = 0; r < 9 && !found; r++) for (int dy = -r; dy <= r && !found; dy++) for (int dx = -r; dx <= r && !found; dx++) if (v101_blue(px(L_PAT, ccx + dx, cy + dy))) { sx = ccx + dx; sy = cy + dy; found = 1; }
        static uint8_t seen[192 * 128]; memset(seen, 0, sizeof seen); int st2[4096], n = 0;
        if (v101_blue(px(L_PAT, sx, sy))) { st2[n++] = sy * W + sx; seen[sy * W + sx] = 1; }
        while (n) { int c = st2[--n], x = c % W, y = c / W; bx0 = arc_mini(bx0, x); bx1 = arc_maxi(bx1, x); by0 = arc_mini(by0, y); by1 = arc_maxi(by1, y);
            const int dx4[4] = { 1, -1, 0, 0 }, dy4[4] = { 0, 0, 1, -1 };
            for (int d = 0; d < 4; d++) { int X = x + dx4[d], Y = y + dy4[d]; if (X < 0 || Y < 0 || X >= W || Y >= H || seen[Y * W + X] || !v101_blue(px(L_PAT, X, Y)) || n >= 4095) continue; seen[Y * W + X] = 1; st2[n++] = Y * W + X; } }
        int dw = bx1 - bx0 + 1, dh = by1 - by0 + 1;
        printf("ARC note pattern dot: %dx%d px\n", dw, dh);
        CHECK(dw >= 3 && dw < 12 && abs(dw - dh) <= arc_maxi(1, dw / 10));
    }
    /* split: second colour right of the cube's middle only */
    {
        int ccx = V101_CX(3), wrong = 0;
        for (int y = cy - 12; y <= cy + 12; y++) for (int dx = 2; dx <= 12; dx++) { wrong += !v101_red(px(L_PAT, ccx - dx, y)); wrong += !v101_blue(px(L_PAT, ccx + dx, y)); }
        printf("ARC note pattern split: %d pixels on the wrong side\n", wrong);
        CHECK(wrong == 0);
    }
    /* noise: both colours */
    blue = red = 0;
    for (int y = cy - 14; y <= cy + 14; y++) for (int x = V101_CX(4) - 14; x <= V101_CX(4) + 14; x++) { blue += v101_blue(px(L_PAT, x, y)); red += v101_red(px(L_PAT, x, y)); }
    CHECK(blue > 60 && red > 60);
    /* the mech: stripes everywhere (PAT, at x 1.6) / on the legs only (PATLEG, at x 0) */
    int mx[2] = { (int)(96 + 1.6f * k), 96 };
    int torso_blue[2] = { 0, 0 }, leg_blue[2] = { 0, 0 }, head_blue[2] = { 0, 0 };
    for (int t = 0; t < 2; t++) {
        int L = t ? L_PATLEG : L_PAT;
        for (int dy = -6; dy <= 6; dy++) for (int dx = -7; dx <= 7; dx++) torso_blue[t] += v101_blue(px(L, mx[t] + dx, (int)(64 - (-2.9f + 1.15f) * k) + dy));
        for (int dy = -6; dy <= 6; dy++) for (int dx = -3; dx <= 3; dx++) leg_blue[t] += v101_blue(px(L, mx[t] - (int)(0.25f * k) + dx, (int)(64 - (-2.9f + 0.4f) * k) + dy));
        for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) head_blue[t] += v101_blue(px(L, mx[t] + dx, (int)(64 - (-2.9f + 1.75f) * k) + dy));
    }
    printf("ARC note pattern mech: all parts torso %d legs %d head %d blue px; legs only torso %d legs %d head %d\n", torso_blue[0], leg_blue[0], head_blue[0], torso_blue[1], leg_blue[1], head_blue[1]);
    CHECK(torso_blue[0] > 20 && leg_blue[0] > 10 && torso_blue[1] == 0 && head_blue[1] == 0 && leg_blue[1] > 10);
    {   /* 0.10.6 arms only: stripes on the mech's arms but not its torso; on all three pads (shoulder, clavicle, ruled pauldron) */
        int arm = 0, torso = 0, pad[3] = { 0, 0, 0 };
        for (int dy = -5; dy <= 5; dy++) for (int dx = -2; dx <= 2; dx++) arm += v101_blue(px(L_PATARM, 96 - (int)(0.65f * k) + dx, (int)(64 - (-0.4f + 1.15f) * k) + dy));
        for (int dy = -5; dy <= 5; dy++) for (int dx = -6; dx <= 6; dx++) torso += v101_blue(px(L_PATARM, 96 + dx, (int)(64 - (-0.4f + 1.15f) * k) + dy));
        for (int i = 0; i < 3; i++) for (int dy = -8; dy <= 8; dy++) for (int dx = -8; dx <= 8; dx++) pad[i] += v101_blue(px(L_PATARM, (int)(96 + (-1.6f + 1.6f * (float)i) * k) + dx, (int)(64 + 2.2f * k) + dy));
        printf("ARC note pattern arms only: arm %d torso %d pads %d %d %d blue px\n", arm, torso, pad[0], pad[1], pad[2]);
        CHECK(arm > 5 && torso == 0 && pad[0] > 30 && pad[1] > 30 && pad[2] > 30);
    }
    #undef V101_CX
}
static arc_cworld v100_cw;
static void v100_shots_draw(int look) {   /* 0.10.0 shots: drawn by arc_shots_draw (wide tracers and bolts are additive meshes, flashes lights) */
    arc_camera cam = { { 0, 2, 6 }, { 0, 1, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 80 }; arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.6f), 0x606870, 0x202428); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    static bool fired = false;
    if (!fired) {   /* once: the look can take several frames */
        fired = true;
        v100_cw = arc_cworld_create(); arc_cworld_add_box(v100_cw, arc_v3_make(-3, 0, -2), arc_v3_make(3, 3, -1.5f), 1);
        arc_shots_set_world(v100_cw, (arc_physics){ 0 }, NULL, NULL); arc_shots_clear();
        arc_shot_desc d; memset(&d, 0, sizeof d);
        d.from = arc_v3_make(-2, 1, 4); d.dir = arc_v3_sub(arc_v3_make(1, 1, -1.5f), d.from); d.range = 20;
        d.tracer_rgb = 0x40FF60; d.tracer_width = 0.08f; d.tracer_life = 1000; d.decal_size = 0.6f;
        arc_shot_fire(&d);
        memset(&d, 0, sizeof d); d.from = arc_v3_make(2.2f, 1.6f, 2); d.dir = arc_v3_make(-1, 0, -1); d.speed = 1; d.range = 20;
        d.tracer_rgb = 0xFF3040; d.projectile_size = 0.7f; d.tracer_width = 0.07f; d.flash_light = 4; d.flash_rgb = 0xFF3040;
        arc_shot_fire(&d);
        arc_shots_update(0.5f);
    }
    arc_draw_cube(arc_v3_make(0, 1.5f, -1.75f), arc_v3_make(6, 3, 0.5f), 0xB0B0B0);
    arc_draw_cube(arc_v3_make(0, -0.05f, 1), arc_v3_make(10, 0.1f, 8), 0x707070);
    if (look == L_SHOTS) { arc_draw_decals(); arc_shots_draw(); }
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static void v099_draw(int look) {
    /* the first frame after the last tile-grid frame: arc_stats() is the tile grid's (a look can take several
       frames while its screenshot is written, so this can't just be "the first L_MARK frame") */
    static int prev = -1;
    if (prev == L_TILES && look != L_TILES && !v099_tstats.instances) v099_tstats = arc_stats();
    prev = look;
    if (look == L_TILES) { v099_tiles_draw(); return; }
    if (look == L_FX || look == L_FX0) { v099_fx_draw(look); return; }
    if (look >= L_SDF) { v101_draw(look); return; }
    if (look >= L_SHOTS) { v100_shots_draw(look); return; }
    if (look >= L_CUT) { v099_cut_draw(look); return; }
    arc_camera cam = V099_CAM; arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.5f), 0xFFF4E0, 0x40485A); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_draw_mesh(v099_ground, arc_m4_identity(), 0xFFFFFF);
    arc_draw_mesh(v099_box, arc_m4_trs_y(arc_v3_make(0.3f, v099_h(NULL, 0.3f, -8.5f) + 0.6f, -8.5f), 0, arc_v3_make(1.6f, 1.2f, 0.6f)), 0xB0B0B0);
    if (look == L_MARK) {
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = -2.4f, .z = -6.5f, .size = 3.0f, .inner = 0.5f, .fill_rgb = 0xFF2020, .fill_alpha = 0.9f, .height = v099_h });
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_TILE, .x = 2.4f, .z = -6, .size = 2.4f, .inner = 0.2f, .fill_rgb = 0x20FF20, .fill_alpha = 0.9f, .edge_rgb = 0xFFFFFF, .edge_width = 2, .height = v099_h });
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = 0.3f, .z = -10, .size = 2.2f, .inner = 0.5f, .fill_rgb = 0x2040FF, .fill_alpha = 0.95f, .on_top = true, .height = v099_h });
    }
    char path[64]; look_path(path, sizeof path, look); arc_screenshot_after_frame(path);
}
static void v099_tiles_check(void) {
    /* each cell shows its own layer's colour (nearest of the 32, brightness aside) and its dot in the turned corner */
    int colour_ok = 0, turn_ok = 0, cells = 0;
    static const float qc[4] = { 1, 0, -1, 0 }, qs[4] = { 0, 1, 0, -1 };
    for (int y = 1; y < 11; y++) for (int x = 1; x < 15; x++) {
        int sx, sy; if (!v099_screen((float)x + 0.5f, (float)y + 0.5f, &sx, &sy)) continue;
        cells++;
        const uint8_t *q = px(L_TILES, sx, sy); float tot = (float)(q[0] + q[1] + q[2]) + 1;
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < 32; i++) { uint32_t c = v099_lcol(i); float ct = (float)((c >> 16 & 255) + (c >> 8 & 255) + (c & 255));
            float d = fabsf(q[0] / tot - (float)(c >> 16 & 255) / ct) + fabsf(q[1] / tot - (float)(c >> 8 & 255) / ct) + fabsf(q[2] / tot - (float)(c & 255) / ct);
            if (d < bd) { bd = d; best = i; } }
        colour_ok += best == v099_tlayer(x, y);
        /* the dot's corner: local (-0.375, -0.375) turned as arc_draw_tiles turns (as arc_m4_rotate_y) */
        int t = v099_tturn(x, y); float lx = -0.36f, lz = -0.36f, wx = lx * qc[t] + lz * qs[t], wz = -lx * qs[t] + lz * qc[t];
        int bx, by, brightest = -1, bv = -1, want = -1;
        for (int c4 = 0; c4 < 4; c4++) {
            float ox = (c4 & 1) ? 0.36f : -0.36f, oz = (c4 & 2) ? 0.36f : -0.36f;
            if (!v099_screen((float)x + 0.5f + ox, (float)y + 0.5f + oz, &bx, &by)) continue;
            const uint8_t *p2 = px(L_TILES, bx, by); int v2 = p2[0] + p2[1] + p2[2];
            if (v2 > bv) { bv = v2; brightest = c4; }
            if (fabsf(ox - wx) < 0.01f && fabsf(oz - wz) < 0.01f) want = c4;
        }
        turn_ok += brightest == want;
    }
    printf("ARC note tiles: %d cells, %d with their layer's colour, %d turned right; the frame: %d draw calls for %d instances\n", cells, colour_ok, turn_ok, v099_tstats.draw_calls, v099_tstats.instances);
    CHECK(cells > 100 && colour_ok == cells && turn_ok == cells);
    CHECK(v099_tstats.instances >= 16 * 12 && v099_tstats.draw_calls <= 4);   /* one per mesh kind (2), whatever the layers */
}
static float v099_avg(int look, int cx, int cy, float *var) {   /* mean brightness of a 5 x 5 window (and its spread) */
    float s = 0, s2 = 0;
    for (int y = -2; y <= 2; y++) for (int x = -2; x <= 2; x++) { const uint8_t *q = px(look, cx + x, cy + y); float v = (float)(q[0] + q[1] + q[2]) / 3; s += v; s2 += v * v; }
    s /= 25; if (var) *var = s2 / 25 - s * s; return s;
}
static void v099_fx_check(void) {
    arc_camera cam = V099_CAM; int sx, sy; float a[3], a0[3], var = 0, var0 = 0;
    for (int i = 0; i < 3; i++) {   /* each box's front face */
        float x = V099_FXX[i]; v099_proj(&cam, arc_v3_make(x, v099_h(NULL, x, -5) + 0.6f, -4.4f), &sx, &sy);
        a[i] = v099_avg(L_FX, sx, sy, i == 2 ? &var : NULL); a0[i] = v099_avg(L_FX0, sx, sy, i == 2 ? &var0 : NULL);
    }
    /* ground in front of the boxes: unseen (x < 0) black, seen unchanged */
    float gu, gu0, gs, gs0;
    v099_proj(&cam, arc_v3_make(-1.2f, v099_h(NULL, -1.2f, -3.6f), -3.6f), &sx, &sy); gu = v099_avg(L_FX, sx, sy, NULL); gu0 = v099_avg(L_FX0, sx, sy, NULL);
    v099_proj(&cam, arc_v3_make(1.2f, v099_h(NULL, 1.2f, -3.6f), -3.6f), &sx, &sy); gs = v099_avg(L_FX, sx, sy, NULL); gs0 = v099_avg(L_FX0, sx, sy, NULL);
    printf("ARC note fx: ground unseen %.0f (was %.0f), seen %.0f (was %.0f); box out of the mask %.0f (was %.0f), darkened %.0f (was %.0f), charred %.0f spread %.0f (was %.0f, %.0f)\n",
           gu, gu0, gs, gs0, a[0], a0[0], a[1], a0[1], a[2], var, a0[2], var0);
    CHECK(gu0 > 40 && gu < 4 && fabsf(gs - gs0) < 4);
    CHECK(fabsf(a[0] - a0[0]) < 4);                      /* left out of the mask: as if no mask */
    CHECK(a[1] < a0[1] * 0.25f && a[1] > 0);             /* darken 1: 15% left */
    CHECK(a[2] < a0[2] * 0.6f && var > var0 + 4);         /* charred: darker and mottled */
}
static void v099_cut_check(void) {
    int red[3] = { 0 }; const int L[3] = { L_CUT, L_CUTD, L_CUT0 };
    for (int k = 0; k < 3; k++) for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { const uint8_t *q = px(L[k], x, y); red[k] += q[0] > q[1] + 80 && q[0] > q[2] + 80; }
    printf("ARC note cutaway: red unit pixels: clip %d, dither %d, off %d\n", red[0], red[1], red[2]);
    CHECK(red[2] < 10 && red[0] > 150 && red[1] > red[0] / 3 && red[1] <= red[0] + 5);
}
static void v100_shots_check(void) {
    int green = 0, red = 0; double wall = 0, wall0 = 0; int nwall = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        const uint8_t *q = px(L_SHOTS, x, y), *q0 = px(L_SHOTS0, x, y);
        green += q[1] > 200 && q[1] > q[0] + 60 && q[1] > q[2] + 40;
        red += q[0] > 200 && q[0] > q[1] + 80;
        if (y < H / 3 && x > W / 2) { wall += q[0]; wall0 += q0[0]; nwall++; }   /* the wall's upper right, near the bolt */
    }
    wall /= nwall; wall0 /= nwall;
    printf("ARC note shots look: tracer pixels %d, bolt pixels %d, wall red %.0f (without shots %.0f)\n", green, red, wall, wall0);
    CHECK(green > 40 && red > 4 && wall > wall0 + 8);
}
static void v099_check(void) {
    v099_tiles_check();
    /* red and green markers show only with markers on, as solid patches: z-fighting would leave holes (a pixel off the
       marker colour with marker pixels on all four sides) */
    int red = 0, green = 0, red0 = 0, holes = 0, through = 0, boxpx = 0;
    #define V099_RED(q) ((q)[0] > (q)[1] + 70 && (q)[0] > (q)[2] + 70)
    #define V099_GRN(q) ((q)[1] > (q)[0] + 50 && (q)[1] > (q)[2] + 50)
    for (int y = 1; y < H - 1; y++) for (int x = 1; x < W - 1; x++) {
        const uint8_t *q = px(L_MARK, x, y), *q0 = px(L_MARK0, x, y);
        bool r = V099_RED(q), g = V099_GRN(q); red += r; green += g; red0 += V099_RED(q0) || V099_GRN(q0);
        if (!r && !g) {
            const uint8_t *n[4] = { px(L_MARK, x - 1, y), px(L_MARK, x + 1, y), px(L_MARK, x, y - 1), px(L_MARK, x, y + 1) };
            int rr = 0, gg = 0; for (int k = 0; k < 4; k++) { rr += V099_RED(n[k]); gg += V099_GRN(n[k]); }
            holes += rr == 4 || gg == 4;
        }
        /* the box (grey in the picture without markers): where the blue ring is behind it, a faint blue shows through */
        if (abs(q0[0] - q0[1]) < 10 && abs(q0[1] - q0[2]) < 12 && q0[0] > 110) { boxpx++; if (q[2] > q0[2] + 12 && q[2] > q[0] + 8) through++; }
    }
    printf("ARC note markers: red %d, green %d, holes %d; marker colours without markers %d; box pixels %d, blue through the box %d\n", red, green, holes, red0, boxpx, through);
    CHECK(red > W * H / 200 && green > W * H / 200 && red0 < 20);
    CHECK(holes * 200 < red + green);           /* under half a percent */
    CHECK(boxpx > 500 && through > 100);
    v099_fx_check(); v099_cut_check(); v100_shots_check(); v101_check(); v102_check();
}

/* ---- 0.10.2 split-screen viewports (Droids). L_VPFULL draws L_VPREF's scene inside arc_viewport_begin(0, 0, W, H):
   every pass a viewport stands in for (sky, cascades, light shadows, the reflection, post with ssao / bloom / fxaa /
   vignette, a render target used inside it, 2D) must give the same picture as the screen. L_VPSPLIT: two cameras side
   by side, text at (10, 10) in each; picking between frames goes through the viewport under the pixel. ---- */
static const arc_camera V102_A = { { 0, 3.0f, 7 }, { 0, 0.5f, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 60 };
static const arc_camera V102_B = { { 6, 2.0f, -3 }, { 0, 0.8f, 0 }, { 0, 1, 0 }, 1.1f, 0, 0.1f, 60 };
static arc_mesh v102_floor, v102_box; static arc_texture v102_map;
static int v102_sizes[8]; static bool v102_sized, v102_picked; static float v102_pick_dot, v102_pick_sx, v102_pick_sy; static int v102_pick_vp, v102_pick_n, v102_tick_w, v102_after_w;
static void v102_scene(const arc_camera *c, bool sizes, int slot) {
    if (!v102_floor.id) {
        v102_floor = arc_mesh_plane(0x8090A0, 4, 0x606878); arc_mesh_set_reflective(v102_floor, 0.6f);
        v102_box = arc_mesh_cube(0xFFFFFF); v102_map = arc_target_create(40, 30, false);
    }
    arc_set_camera(c);
    arc_set_light(arc_v3_make(-0.5f, 1, 0.35f), 0xFFF0D0, 0x404858); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_set_sky_ex(0x3060C0, 0xC0D0E0, 0x302820);
    arc_set_shadows(arc_v3_make(0, 0, 0), 6, 1024); arc_set_shadow_cascades(2, 12);
    arc_reflection_plane(0, 0.5f, 0.5f);
    arc_post p; memset(&p, 0, sizeof p); p.ssao = 0.8f; p.ssao_radius = 0.5f; p.bloom = 0.4f; p.bloom_threshold = 0.8f; p.fxaa = true; p.vignette = 0.3f; p.tonemap = true;
    arc_set_post(&p);
    arc_draw_mesh(v102_floor, arc_m4_trs_y(arc_v3_make(0, 0, 0), 0, arc_v3_make(12, 1, 12)), 0xFFFFFF);
    arc_draw_mesh(v102_box, arc_m4_trs_y(arc_v3_make(-1.2f, 0.5f, 0), 0.4f, arc_v3_make(1, 1, 1)), 0xD04040);
    arc_draw_mesh(v102_box, arc_m4_trs_y(arc_v3_make(1.0f, 0.75f, -0.5f), -0.3f, arc_v3_make(0.8f, 1.5f, 0.8f)), 0x40B060);
    arc_draw_mesh(v102_box, arc_m4_trs_y(arc_v3_make(0.2f, 0.25f, 1.6f), 0, arc_v3_make(0.5f, 0.5f, 0.5f)), 0xE0E0E0);
    arc_light_point_ex(arc_v3_make(0.3f, 1.6f, 1.2f), 0xFFA040, 3.0f, 5.0f, ARC_LIGHT_SHADOW);
    if (sizes) { v102_sizes[slot * 4] = arc_width(); v102_sizes[slot * 4 + 1] = arc_height(); v102_sizes[slot * 4 + 2] = arc_target_width(); v102_sizes[slot * 4 + 3] = (int)(arc_aspect() * 100.0f + 0.5f); }
    arc_target_begin(v102_map, true, 0xFF102030);   /* a "minimap" target inside the view */
    arc_draw_rect(5, 5, 10, 10, 0xFFFF00);
    arc_target_end();
    arc_draw_texture_ex(v102_map, 0, 0, 40, 30, (float)arc_width() - 46, 6, 40, 30, 0, 0, 0, 0xFFFFFF);
    arc_draw_text(10, 10, 2, 0xFFFFFF, "VP");
}
static void v102_draw(int look) {
    char path[64]; look_path(path, sizeof path, look);
    if (look == L_VPREF) v102_scene(&V102_A, false, 0);
    else if (look == L_VPFULL) { arc_viewport_begin(0, 0, W, H); v102_scene(&V102_A, false, 0); arc_viewport_end(); }
    else {
        arc_set_post(NULL); arc_set_sky_ex(0, 0, 0);
        arc_viewport_begin(0, 0, W / 2, H); v102_scene(&V102_A, !v102_sized, 0); int c0 = arc_viewport_current(); arc_viewport_end();
        arc_viewport_begin(W / 2, 0, W / 2, H); v102_scene(&V102_B, !v102_sized, 1); int c1 = arc_viewport_current(); arc_viewport_end();
        if (!v102_sized) {
            int x = 0, y = 0, w = 0, h = 0; bool r = arc_viewport_rect(1, &x, &y, &w, &h);
            v102_sizes[0] = c0 == 0 && c1 == 1 && arc_viewport_current() == -1 && arc_viewport_count() == 2 && r && x == W / 2 && y == 0 && w == W / 2 && h == H ? v102_sizes[0] : -1;
            v102_after_w = arc_width(); v102_sized = true;
        }
    }
    arc_screenshot_after_frame(path);
}
static void v102_tick(void) {
    if (look_now == L_VPSPLIT + 1 && !v102_picked) {   /* the split frame was the last drawn */
        v102_picked = true; v102_pick_n = arc_viewport_count();
        arc_v2 l = { 0, 0 }; v102_pick_vp = arc_viewport_at(arc_v2_make((float)W * 0.75f, (float)H * 0.5f), &l);
        arc_v3 o, d; arc_screen_ray(arc_v2_make((float)W * 0.75f, (float)H * 0.5f), &o, &d);
        v102_pick_dot = arc_v3_dot(d, arc_v3_norm(arc_v3_sub(V102_B.target, V102_B.eye)));
        arc_v2 sp = { -1, -1 }; if (arc_viewport_world_to_screen(1, V102_B.target, &sp)) { v102_pick_sx = sp.x; v102_pick_sy = sp.y; }
        arc_viewport_begin(W / 2, 0, W / 2, H); v102_tick_w = arc_width(); arc_viewport_end();   /* a tick-time area (UI) */
        if (fabsf(l.x - (float)W * 0.25f) > 0.01f) v102_pick_vp = -2;
    }
}
static void v102_check(void) {
    int n = 0, mx = 0;   /* one window-sized viewport = the screen */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { int d = diff(L_VPFULL, L_VPREF, x, y); if (d > mx) mx = d; if (d > 2) n++; }
    if (n || mx) printf("ARC note viewport vs screen: %d pixels differ by more than 2 (max %d)\n", n, mx);
    CHECK(n == 0);
    /* the scene shows: sky at the top, the yellow minimap square at the top right, shadows */
    const uint8_t *sky = px(L_VPREF, W / 2, 1); CHECK(sky[2] > sky[0] + 8);
    const uint8_t *mm = px(L_VPREF, W - 46 + 10, 6 + 10); CHECK(mm[0] > 200 && mm[1] > 200 && mm[2] < 80);
    /* text at (10, 10) in each half: white pixels in its first glyph's box, none to the left of x 10 */
    for (int v = 0; v < 2; v++) {
        int ox = v * (W / 2), white = 0, early = 0;
        for (int y = 10; y < 26; y++) for (int x = 10; x < 26; x++) { const uint8_t *q = px(L_VPSPLIT, ox + x, y); if (q[0] > 235 && q[1] > 235 && q[2] > 235) white++; }
        for (int y = 10; y < 26; y++) for (int x = 2; x < 9; x++) { const uint8_t *q = px(L_VPSPLIT, ox + x, y); if (q[0] > 235 && q[1] > 235 && q[2] > 235) early++; }
        CHECK(white > 20 && early == 0);
        const uint8_t *m2 = px(L_VPSPLIT, ox + W / 2 - 46 + 10, 16); CHECK(m2[0] > 200 && m2[1] > 200 && m2[2] < 80);   /* each one's minimap at its own top right */
    }
    int differ = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W / 2; x++) if (memcmp(px(L_VPSPLIT, x, y), px(L_VPSPLIT, x + W / 2, y), 3)) differ++;
    CHECK(differ > W * H / 8);   /* two different cameras */
    CHECK(v102_sizes[0] == W / 2 && v102_sizes[1] == H && v102_sizes[2] == W / 2 && v102_sizes[3] == (int)((float)(W / 2) / (float)H * 100.0f + 0.5f));
    CHECK(v102_sizes[4] == W / 2 && v102_sizes[6] == W / 2 && v102_after_w == W);
    CHECK(v102_picked && v102_pick_n == 2 && v102_pick_vp == 1 && v102_pick_dot > 0.999f && v102_tick_w == W / 2);
    CHECK(fabsf(v102_pick_sx - (float)W * 0.75f) < 0.5f && fabsf(v102_pick_sy - (float)H * 0.5f) < 0.5f);
    printf("ARC note viewports: pick through the right view %.5f, its centre at %.1f %.1f\n", v102_pick_dot, v102_pick_sx, v102_pick_sy);
}
