/* ARC 0.10.2 test fixture: split-screen viewports. The same scene (sky, sun shadow cascades, a shadowed point light,
   a planar reflection, post with SSAO / bloom / FXAA / vignette, a minimap target and text at (10, 10)) is drawn
   full-window at 320 x 180 with camera A, then with camera B, then the window becomes 640 x 180 and the two cameras
   draw side by side in viewports. Each half must match its full-window picture. Checked by tests/split_check.md. */
#include "../include/arc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void arc__set_size(int w, int h);   /* engine-internal (test only): the window size, between frames */

enum { VW = 320, VH = 180 };
static const arc_camera CAM_A = { { 0, 3.0f, 7 }, { 0, 0.5f, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 60 };
static const arc_camera CAM_B = { { 6, 2.0f, -3 }, { 0, 0.8f, 0 }, { 0, 1, 0 }, 1.1f, 0, 0.1f, 60 };
static arc_mesh floor_m, box_m; static arc_texture map_t;
static int phase;   /* 0 A full, 1 B full, 2 split, 3 done */
static uint8_t *ref[2]; static int diff_n[2] = { -1, -1 }, diff_max[2] = { -1, -1 }, in_w[2];
static const char *SHOTS[3] = { "split_a.png", "split_b.png", "split_s.png" };

static void init(void) {
    floor_m = arc_mesh_plane(0x8090A0, 4, 0x606878); arc_mesh_set_reflective(floor_m, 0.6f);
    box_m = arc_mesh_cube(0xFFFFFF); map_t = arc_target_create(40, 30, false);
    for (int i = 0; i < 3; i++) remove(SHOTS[i]);
    arc__set_size(VW, VH);
}
static void scene(const arc_camera *c, const char *label) {
    arc_set_camera(c);
    arc_set_light(arc_v3_make(-0.5f, 1, 0.35f), 0xFFF0D0, 0x404858);
    arc_set_sky_ex(0x3060C0, 0xC0D0E0, 0x302820);
    arc_set_shadows(arc_v3_make(0, 0, 0), 6, 1024); arc_set_shadow_cascades(2, 12);
    arc_reflection_plane(0, 0.5f, 0.5f);
    arc_post p; memset(&p, 0, sizeof p); p.ssao = 0.8f; p.ssao_radius = 0.5f; p.bloom = 0.4f; p.bloom_threshold = 0.8f; p.fxaa = true; p.vignette = 0.3f; p.tonemap = true;
    arc_set_post(&p);
    arc_draw_mesh(floor_m, arc_m4_trs_y(arc_v3_make(0, 0, 0), 0, arc_v3_make(12, 1, 12)), 0xFFFFFF);
    arc_draw_mesh(box_m, arc_m4_trs_y(arc_v3_make(-1.2f, 0.5f, 0), 0.4f, arc_v3_make(1, 1, 1)), 0xD04040);
    arc_draw_mesh(box_m, arc_m4_trs_y(arc_v3_make(1.0f, 0.75f, -0.5f), -0.3f, arc_v3_make(0.8f, 1.5f, 0.8f)), 0x40B060);
    arc_draw_mesh(box_m, arc_m4_trs_y(arc_v3_make(0.2f, 0.25f, 1.6f), 0, arc_v3_make(0.5f, 0.5f, 0.5f)), 0xE0E0E0);
    arc_light_point_ex(arc_v3_make(0.3f, 1.6f, 1.2f), 0xFFA040, 3.0f, 5.0f, ARC_LIGHT_SHADOW);
    arc_target_begin(map_t, true, 0xFF102030);
    arc_draw_rect(5, 5, 10, 10, 0xFFFF00);
    arc_target_end();
    arc_draw_texture_ex(map_t, 0, 0, 40, 30, (float)arc_width() - 46, 6, 40, 30, 0, 0, 0, 0xFFFFFF);
    arc_draw_text(10, 10, 2, 0xFFFFFF, label);
}
static void draw(float alpha) {
    (void)alpha;
    if (phase == 0) scene(&CAM_A, "P1");
    else if (phase == 1) scene(&CAM_B, "P2");
    else {
        arc_set_post(NULL); arc_set_sky_ex(0, 0, 0);
        arc_viewport_begin(0, 0, VW, VH); in_w[0] = arc_width(); scene(&CAM_A, "P1"); arc_viewport_end();
        arc_viewport_begin(VW, 0, VW, VH); in_w[1] = arc_width(); scene(&CAM_B, "P2"); arc_viewport_end();
    }
    if (phase < 3) arc_screenshot_after_frame(SHOTS[phase]);
}
static void tick(void) {
    if (phase >= 3) return;
    FILE *f = fopen(SHOTS[phase], "rb"); if (!f) return;
    fclose(f);
    int w = 0, h = 0; uint8_t *img = arc_image_load(SHOTS[phase], &w, &h);
    if (!img) return;
    if (phase < 2) { ref[phase] = (w == VW && h == VH) ? img : NULL; if (!ref[phase]) free(img); }
    else {
        for (int v = 0; v < 2; v++) {
            int n = 0, mx = 0;
            if (ref[v] && w == 2 * VW && h == VH)
                for (int y = 0; y < VH; y++) for (int x = 0; x < VW; x++) {
                    const uint8_t *a = img + ((size_t)y * w + (size_t)(x + v * VW)) * 4, *b = ref[v] + ((size_t)y * VW + (size_t)x) * 4;
                    int m = 0; for (int k = 0; k < 3; k++) { int d = abs(a[k] - b[k]); if (d > m) m = d; }
                    if (m > mx) { mx = m; }
                    if (m > 2) { n++; }
                }
            else n = -1;
            diff_n[v] = n; diff_max[v] = mx;
        }
        free(img);
    }
    phase++;
    if (phase == 2) arc__set_size(2 * VW, VH);
}
static void dump(arc_json *j) {
    arc_json_int_k(j, "phase", phase);
    arc_json_int_k(j, "left_diff", diff_n[0]); arc_json_int_k(j, "right_diff", diff_n[1]);
    arc_json_int_k(j, "left_max", diff_max[0]); arc_json_int_k(j, "right_max", diff_max[1]);
    arc_json_int_k(j, "left_width", in_w[0]); arc_json_int_k(j, "right_width", in_w[1]);
    arc_json_int_k(j, "window_width", arc_width()); arc_json_int_k(j, "viewports", arc_viewport_count());
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC split-screen fixture", .width = VW, .height = VH, .clear_color = 0x000000, .init = init, .tick = tick, .draw = draw, .dump = dump };
}
