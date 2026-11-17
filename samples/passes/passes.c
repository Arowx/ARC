/* ARC 0.3 feature check: render targets (2D + 3D inside), clipping, blend modes, 2D triangles,
   rotated/flipped sprites, smooth vs sharp filtering. Used to compare the renderers pixel for pixel. */
#include "../../include/arc.h"

static arc_texture canvas, scene, checker;
static arc_mesh cube;

static void init(void) {
    canvas = arc_target_create(160, 90, false);     /* low-res UI canvas, scaled up sharp */
    scene = arc_target_create(128, 128, true);      /* 3D scene into a texture */
    uint8_t px[8 * 8 * 4];
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        uint8_t *p = px + (y * 8 + x) * 4; int on = (x + y) & 1;
        p[0] = on ? 255 : 30; p[1] = on ? 200 : 60; p[2] = (uint8_t)(y * 32); p[3] = 255;
    }
    px[3] = 0;   /* one transparent texel, top-left */
    checker = arc_texture_create(8, 8, px, false);
    cube = arc_mesh_cube(0xE07040);
}
static void tick(void) {}
static void draw(float alpha) {
    (void)alpha;
    /* 1. 3D into a target */
    arc_target_begin(scene, true, 0xFF203040);
    arc_camera cam = { .eye = { 2, 2, 3 }, .target = { 0, 0, 0 }, .fov_y = 1.0f };
    arc_set_camera(&cam);
    arc_draw_mesh(cube, arc_m4_trs_y(arc_v3_make(0, 0, 0), 0.6f, arc_v3_make(1, 1, 1)), ARC_WHITE);
    arc_draw_line(arc_v3_make(-1.5f, -0.5f, 0), arc_v3_make(1.5f, -0.5f, 0), 0xFFFF00);
    arc_draw_text(4, 4, 1, 0xFFFFFF, "TARGET");
    arc_target_end();
    /* 2. 2D canvas with clip, blend, triangles */
    arc_target_begin(canvas, true, 0);                /* transparent */
    arc_draw_rect(0, 0, 160, 20, 0x303060);
    arc_set_clip(10, 25, 60, 30);
    for (int i = 0; i < 8; i++) arc_draw_rect(5 + (float)i * 10.0f, 22 + (float)i * 4.0f, 30, 8, i & 1 ? 0x60C060 : 0xC06060);
    arc_clear_clip();
    arc_draw_rect_lines(10, 25, 60, 30, 0xFFFFFF);
    arc_set_blend(ARC_BLEND_ADD);
    arc_draw_rect(90, 25, 40, 40, arc_rgba(0x4080FF, 0.6f));
    arc_draw_rect(110, 45, 40, 40, arc_rgba(0xFF4020, 0.6f));
    arc_set_blend(ARC_BLEND_ALPHA);
    arc_vertex2d tri[3] = { { 20, 85, 0, 0, 0xFFFF0000 }, { 60, 85, 0, 0, 0xFF00FF00 }, { 40, 60, 0, 0, arc_rgba(0x0000FF, 0.5f) } };
    arc_draw_triangles2d((arc_texture){ 0 }, tri, 3);
    arc_draw_text(4, 6, 1, 0xFFFF80, "CANVAS");
    arc_target_end();
    /* 3. compose on the screen */
    arc_draw_rect(0, 0, 640, 360, 0x101418);
    arc_draw_texture_ex(scene, 0, 0, 128, 128, 20, 20, 256, 256, 0, 0, 0, ARC_WHITE);
    arc_draw_texture_ex(canvas, 0, 0, 160, 90, 300, 20, 320, 180, 0, 0, 0, ARC_WHITE);
    arc_draw_texture_ex(checker, 0, 0, 8, 8, 380, 280, 48, 48, 24, 24, 0.5f, ARC_WHITE);     /* rotated about centre */
    arc_draw_texture_ex(checker, 0, 0, -8, 8, 440, 256, 48, 48, 0, 0, 0, ARC_WHITE);          /* flipped */
    arc_set_blend(ARC_BLEND_MULTIPLY);
    arc_draw_rect(500, 250, 100, 60, 0x80FF80);
    arc_set_blend(ARC_BLEND_ALPHA);
    arc_draw_texture_ex(checker, 0, 0, 8, 8, 520, 300, 48, 48, 0, 0, 0, arc_rgba(0xFFFFFF, 0.5f));
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC passes", .init = init, .tick = tick, .draw = draw, .width = 640, .height = 360 };
}
