/* arc_view: a tiny ARC program that shows one asset, for tools/arc_import.py previews (0.9.0).
     arc_view --headless --view assets/meshes/ship.arcm --cmd "step 2; shot ship_preview.png; dump; quit"
   Meshes are framed from their bounds (three-quarter view, on a grid, lit by a sun); textures are drawn centred on a
   checkerboard (so transparency shows); fonts show a sample line. dump() reports what was loaded. */
#include "../include/arc.h"
#include <string.h>
#include <stdio.h>

static const char *path;
static int kind;                 /* 0 none, 1 mesh, 2 texture, 3 font */
static arc_mesh mesh; static arc_texture tex; static arc_font font;
static arc_v3 lo, hi;
static int tw, th;
static float spin;

static bool ends(const char *s, const char *e) { size_t a = strlen(s), b = strlen(e); return a >= b && strcmp(s + a - b, e) == 0; }

static void init(void) {
    path = arc_arg_value("view");
    if (!path) { arc_log("arc_view: pass --view FILE"); return; }
    if (ends(path, ".arcm") || ends(path, ".axm")) {
        mesh = arc_mesh_load(path);
        if (mesh.id) { kind = 1; arc_mesh_bounds(mesh, &lo, &hi); }
    } else if (ends(path, ".arcf")) {
        font = arc_font_load(path); if (font.id) kind = 3;
    } else {
        tex = arc_texture_load(path);
        if (tex.id) { kind = 2; arc_texture_size(tex, &tw, &th); }
    }
    if (!kind) arc_log("arc_view: could not load %s", path);
}
static void tick(void) { if (arc_arg_flag("spin")) spin += arc_dt() * 0.6f; }
static void draw(float alpha) {
    (void)alpha;
    int W = arc_width(), H = arc_height();
    if (kind == 1) {
        arc_v3 c = arc_v3_scale(arc_v3_add(lo, hi), 0.5f), d = arc_v3_sub(hi, lo);
        float r = 0.5f * arc_v3_len(d); if (r < 0.01f) r = 0.01f;
        float dist = r / sinf(0.35f) * 1.15f;
        arc_v3 dir = arc_v3_norm(arc_v3_make(sinf(0.7f + spin), 0.55f, cosf(0.7f + spin)));
        arc_camera cam = { arc_v3_add(c, arc_v3_scale(dir, dist)), c, { 0, 1, 0 }, 0.7f, 0, dist * 0.02f, dist * 4 };
        arc_set_camera(&cam);
        arc_set_light(arc_v3_make(0.4f, 0.8f, 0.3f), 0xFFF4E8, 0x50586A);
        arc_set_sky(0x2A3448, 0x6A7488);
        float g = r * 2; float step = g / 8;
        arc_draw_grid(g, step, 0x8890A0);
        arc_draw_mesh(mesh, arc_m4_identity(), 0xFFFFFF);
        /* 0.10.14: an arrow on the ground along +z, the way ARC's models face (arc_m4_trs_y yaw 0, cam rigs, arc_yaw_to):
           a model whose face is not at the arrow's tip needs arc_import --yaw */
        float al = (hi.z - lo.z) * 0.5f + r * 0.6f;
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_ARROW, .x = c.x, .z = c.z, .x2 = c.x, .z2 = c.z + al, .size = r * 0.08f,
                                       .fill_rgb = 0x40E060, .fill_alpha = 0.85f, .on_top = true, .y = lo.y,
                                       .label = "FRONT +Z", .label_rgb = 0x60FF80, .label_height = 0.01f });
        arc_draw_textf(10, 10, 2, 0xFFFFFF, "%s", path);
        arc_draw_textf(10, 30, 2, 0xC0C8D8, "%d tris  %.2f x %.2f x %.2f", arc_mesh_triangles(mesh), d.x, d.y, d.z);
    } else if (kind == 2) {
        for (int y = 0; y < H; y += 16) for (int x = 0; x < W; x += 16) arc_draw_rect((float)x, (float)y, 16, 16, ((x + y) / 16) & 1 ? 0x505050 : 0x303030);
        float s = fminf(((float)W - 40.0f) / (float)tw, ((float)H - 80.0f) / (float)th); if (s > 8) s = 8;
        float w = (float)tw * s, h = (float)th * s;
        arc_draw_sprite(tex, ((float)W - w) / 2, ((float)H - h) / 2 + 20, w, h, 0, 0, 1, 1, 0xFFFFFF);
        arc_draw_textf(10, 10, 2, 0xFFFFFF, "%s  %dx%d", path, tw, th);
    } else if (kind == 3) {
        arc_draw_rect(0, 0, (float)W, (float)H, 0x202630);
        arc_font_draw(font, 20, 60, 0xFFFFFF, "The quick brown fox jumps over the lazy dog");
        arc_font_draw(font, 20, 60 + (float)arc_font_size(font) * 1.6f, 0xFFD060, "0123456789 !?.,:;()[]+-*/=%&");
        arc_draw_textf(10, 10, 2, 0xC0C8D8, "%s  %dpx", path, arc_font_size(font));
    } else {
        arc_draw_text(10, 10, 2, 0xFF6060, "nothing loaded (--view FILE)");
    }
}
static void dump(arc_json *j) {
    arc_json_str_k(j, "path", path ? path : "");
    arc_json_str_k(j, "kind", kind == 1 ? "mesh" : kind == 2 ? "texture" : kind == 3 ? "font" : "none");
    if (kind == 1) {
        arc_json_num_k(j, "triangles", arc_mesh_triangles(mesh));
        arc_json_num_k(j, "vertices", arc_mesh_vertex_count(mesh));
        arc_json_arr_k(j, "lo"); arc_json_num(j, lo.x); arc_json_num(j, lo.y); arc_json_num(j, lo.z); arc_json_end(j);
        arc_json_arr_k(j, "hi"); arc_json_num(j, hi.x); arc_json_num(j, hi.y); arc_json_num(j, hi.z); arc_json_end(j);
    } else if (kind == 2) { arc_json_num_k(j, "w", tw); arc_json_num_k(j, "h", th); }
    else if (kind == 3) arc_json_num_k(j, "size", arc_font_size(font));
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "arc_view", .width = 640, .height = 480, .init = init, .tick = tick, .draw = draw, .dump = dump };
}
