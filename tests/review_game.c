/* ARC 0.10.1 test fixture: contact sheets (arc_sheet_add + `sheet`), anchored panels in the safe area, the automatic
   UI scale and `ui_fit`. Checked by tests/review_check.md and (0.10.6, the landscape card in an SDF font) card_check.md. */
#include "../include/arc.h"
#include <string.h>
static arc_mesh m_cube, m_ball, m_can;
static int clicks;
static void init(void) {
    m_cube = arc_mesh_cube(0xD04040); m_ball = arc_mesh_sphere(0x40A0E0, 16); m_can = arc_mesh_cylinder(0x60C060, 16);
    if (arc_arg_flag("sdf-ui")) {   /* 0.10.6 (tests/card_check.md): the UI in a 48 px distance-field font at about 19 px, as MechBattleArc */
        static const char *dirs[3] = { "tests/fonts", "../tests/fonts", "../../tests/fonts" };
        for (int i = 0; i < 3; i++) if (arc_file_exists(arc_fmt("%s/dejavu_sdf48.arcf", dirs[i]))) { arc_ui_set_font(arc_font_load(arc_fmt("%s/dejavu_sdf48.arcf", dirs[i])), 19.0f / 48.0f); break; }
    }
}
static void tick(void) {
    arc_ui_scale_auto(1280, 720, 0.75f, 4);
    arc_ui_begin_anchored("Top left", ARC_ANCHOR_TOP_LEFT, 10, 10, 200, 0);
    if (arc_ui_button("Play")) clicks++;
    arc_ui_button("Options");
    arc_ui_end();
    arc_ui_begin_anchored("Top right", ARC_ANCHOR_TOP_RIGHT, 10, 10, 200, 0);
    arc_ui_label("Score 1200"); arc_ui_label("Time 3:20");
    arc_ui_end();
    arc_ui_begin_anchored("Bottom right", ARC_ANCHOR_BOTTOM_RIGHT, 10, 10, 220, 0);
    arc_ui_button("End turn"); arc_ui_button("Undo"); arc_ui_button("Menu##br");
    arc_ui_end();
    arc_ui_begin_anchored("Bottom left", ARC_ANCHOR_BOTTOM_LEFT, 10, 10, 200, 0);
    arc_ui_label("Units 4"); arc_ui_progress(0.6f, "Heat");
    arc_ui_end();
    arc_ui_require_landscape("TURN SIDEWAYS\nThis review fixture needs a landscape screen to show its panels.");
}
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = { { 0, 1.5f, 6 }, { 0, 0.5f, 0 }, { 0, 1, 0 }, 0.8f, 0, 0.1f, 100 }; arc_set_camera(&cam);
    static const char *names[3] = { "Halcyon", "Brute", "Scout" };
    arc_mesh ms[3] = { m_cube, m_ball, m_can };
    arc_m4 at[3] = { arc_m4_trs_y(arc_v3_make(-2, 0.5f, 0), 0.5f, arc_v3_make(1, 2, 1)), arc_m4_translate(arc_v3_make(0, 0.5f, 0)), arc_m4_trs_y(arc_v3_make(2, 0.75f, 0), 0, arc_v3_make(0.6f, 1.5f, 0.6f)) };
    for (int i = 0; i < 3; i++) {
        arc_draw_mesh(ms[i], at[i], 0xFFFFFF);
        arc_sheet_add(ms[i], at[i], NULL, names[i]);
        arc_v2 sp; arc_v3 p = arc_v3_make(at[i].m[12], -0.3f, at[i].m[14]);
        if (arc_camera_to_screen(&cam, (float)arc_width(), (float)arc_height(), p, &sp)) arc_draw_text_ex(sp.x, sp.y, 2, 0xFFFFFF, ARC_TEXT_CENTER, names[i]);
    }
}
static void dump(arc_json *j) {
    arc_rectf sa = arc_safe_area();
    arc_json_num_k(j, "ui_scale", arc_ui_scale()); arc_json_int_k(j, "orientation", arc_orientation()); arc_json_int_k(j, "clicks", clicks);
    arc_json_num_k(j, "safe_y", sa.y); arc_json_num_k(j, "safe_bottom", sa.y + sa.h); arc_json_int_k(j, "sheet_items", arc_sheet_count());
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC review fixture", .width = 1280, .height = 720, .clear_color = 0x202830, .init = init, .tick = tick, .draw = draw, .dump = dump };
}
