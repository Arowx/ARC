/* ARC 0.10.7 test fixture (Moonbase Raid's request): the 3D view drawn into a render target at half size, then put on
   the screen scaled up, with the HUD on top, as Moonbase Raid's 3D view does. `frame` must still list the meshes,
   markers and texts drawn into the target. Checked by tests/target_check.md, and (0.10.9, --immediate: the game draws
   in its tick, as Moonbase Raid does with arc_app.immediate) tests/target_immediate_check.md. */
#include "../include/arc.h"
#include <string.h>
static arc_mesh m_sentry, m_trooper;
static arc_texture view;
static void init(void) {
    m_sentry = arc_mesh_cube(0xC04040); arc_mesh_set_name(m_sentry, "sentry");
    m_trooper = arc_mesh_cylinder(0x40A0E0, 16); arc_mesh_set_name(m_trooper, "trooper");
}
static bool imm;
static void scene(void) {
    int W = arc_width(), H = arc_height();
    if (!view.id) { view = arc_target_create(W / 2, H / 2, true); arc_texture_set_name(view, "view3d"); }
    arc_target_begin(view, true, 0xFF203040);
    arc_camera cam = { { 0, 6, 9 }, { 0, 0, 0 }, { 0, 1, 0 }, 0.8f, 0, 0.1f, 100 }; arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.5f), 0xFFFFFF, 0x404850);
    arc_draw_mesh(m_sentry, arc_m4_translate(arc_v3_make(-2, 0.5f, 0)), 0xFFFFFF);
    arc_draw_mesh(m_trooper, arc_m4_translate(arc_v3_make(2, 0.5f, 0)), 0xFFFFFF);
    arc_draw_mesh(m_trooper, arc_m4_translate(arc_v3_make(2, 0.5f, -3)), 0xFFFFFF);
    arc_marker mk; memset(&mk, 0, sizeof mk);
    mk.shape = ARC_MARK_RING; mk.x = 2; mk.z = 0; mk.size = 1.6f; mk.edge_rgb = 0xFFD040; mk.edge_width = 3; mk.label = "!";
    arc_draw_marker(&mk);
    arc_draw_text_ex(8, 8, 2, 0xFFFFFF, 0, "IN VIEW");   /* 2D inside the target */
    arc_target_end();
    arc_draw_texture_ex(view, 0, 0, (float)(W / 2), (float)(H / 2), 0, 0, (float)W, (float)H, 0, 0, 0, 0xFFFFFFFF);
    arc_draw_text_ex(10, (float)H - 30, 2, 0xFFFFFF, 0, "HUD 3 UNITS");
}
static void tick(void) { if (imm) scene(); }
static void draw(float alpha) { (void)alpha; scene(); }
static void dump(arc_json *j) { arc_json_int_k(j, "w", arc_width()); }
arc_app arc_main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--immediate")) imm = true;
    return (arc_app){ .title = "ARC target fixture", .width = 1280, .height = 720, .clear_color = 0x101010, .init = init, .tick = tick,
                      .draw = imm ? NULL : draw, .dump = dump, .immediate = imm };
}
