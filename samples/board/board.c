/* ARC sample (0.9.9): a tactics board (arc_board) drawn with ground markers (arc_draw_marker) on bumpy ground.
   Walls (grey, block movement and sight), forest (green, costs 2, half cover) and water (blue, costs 3). The blue unit
   shows where it can move this turn (arc_board_reach, 6 points), the red units it can see are marked (line of sight,
   with cover), and an arrow shows the cheapest path to the tile under the mouse. Rings sit under the units and show
   through walls (on_top); the drop zone pulses. Click a lit tile to move there; F turns to face the mouse; TAB picks
   the next unit. Agent commands: `move X Y`, `select N`. dump(): the selected unit, its reach, which enemies it sees. */
#include "../../include/arc.h"
#include <string.h>
#include <stdlib.h>

#define W 16
#define H 12
static const char *MAP[H] = {
    "................",
    "..##......~~....",
    "..#....ff.~~....",
    "......fff.......",
    ".........#####..",
    "...ff.......f#..",
    "...ff....A..f#..",
    "........ff......",
    ".#####..........",
    "......~~..ff....",
    "..B...~~..ff..C.",
    "................" };
typedef struct { int x, y, facing, team; } Unit;
typedef struct { arc_board b; Unit u[3]; int sel; int16_t reach[ARC_BOARD_MAX * ARC_BOARD_MAX]; int nreach; } Game;
static Game G;
static arc_mesh m_cube, m_tile, m_unit;

static float ground(void *ud, float x, float z) { (void)ud; return 0.18f * arc_sinf(x * 0.7f) * arc_cosf(z * 0.5f) + 0.1f * arc_sinf(x * 1.9f + z * 1.3f); }
static float column_top(void *ud, float x, float z) { return ground(ud, floorf(x) + 0.5f, floorf(z) + 0.5f); }   /* the drawn ground: flat-topped columns */
static void update_reach(void) { Unit *u = &G.u[G.sel]; G.nreach = arc_board_reach(&G.b, u->x, u->y, 6, ARC_BOARD_DIAGONAL, G.reach); }
static void init(void) {
    arc_board_init(&G.b, W, H, ARC_BOARD_CHEBYSHEV);
    int n = 0;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        char c = MAP[y][x];
        if (c == '#') arc_board_set_tile(&G.b, x, y, 0, ARC_COVER_FULL);
        else if (c == 'f') arc_board_set_tile(&G.b, x, y, 2, ARC_COVER_HALF);
        else if (c == '~') arc_board_set_tile(&G.b, x, y, 3, ARC_COVER_NONE);
        else if (c >= 'A' && c <= 'C') { G.u[n] = (Unit){ x, y, 0, c == 'A' ? 0 : 1 }; arc_board_set_occupant(&G.b, x, y, n + 1); n++; }
    }
    m_cube = arc_mesh_cube(0xFFFFFF); arc_mesh_set_name(m_cube, "block");
    m_tile = arc_mesh_cube(0xFFFFFF); arc_mesh_set_name(m_tile, "ground");
    m_unit = arc_mesh_cylinder(0xFFFFFF, 16); arc_mesh_set_name(m_unit, "unit");
    update_reach();
}
static arc_camera board_cam(void) { return (arc_camera){ { 8, 13, 19 }, { 8, 0, 5.5f }, { 0, 1, 0 }, 0.75f, 0, 0.1f, 100 }; }
static bool mouse_tile(int *tx, int *ty) {   /* the tile under the mouse (on the flat board plane: close enough) */
    arc_camera cam = board_cam(); arc_v3 o, d;
    arc_camera_ray(&cam, (float)arc_width(), (float)arc_height(), arc_mouse(), &o, &d);
    if (d.y > -1e-4f) return false;
    float t = -o.y / d.y; arc_v3 p = arc_v3_add(o, arc_v3_scale(d, t));
    *tx = (int)floorf(p.x); *ty = (int)floorf(p.z);
    return *tx >= 0 && *ty >= 0 && *tx < W && *ty < H;
}
static void move_to(int x, int y) {
    Unit *u = &G.u[G.sel];
    if (x < 0 || y < 0 || x >= W || y >= H || G.reach[y * W + x] < 0 || (x == u->x && y == u->y)) return;
    int dx = x - u->x, dy = y - u->y;   /* face the way it moved: the nearest of the 8 facings */
    for (int f = 0; f < 8; f++) { int fx, fy; arc_board_dir(f, &fx, &fy); if ((fx == (dx > 0) - (dx < 0)) && (fy == (dy > 0) - (dy < 0))) u->facing = f; }
    arc_board_set_occupant(&G.b, u->x, u->y, 0); u->x = x; u->y = y; arc_board_set_occupant(&G.b, x, y, G.sel + 1);
    update_reach();
}
static void tick(void) {
    int tx, ty;
    if (arc_key_pressed(ARC_KEY_TAB)) { G.sel = (G.sel + 1) % 3; update_reach(); }
    if (arc_key_pressed(ARC_MOUSE_LEFT) && mouse_tile(&tx, &ty)) move_to(tx, ty);
    if (arc_key_pressed(ARC_KEY_F) && mouse_tile(&tx, &ty)) {
        Unit *u = &G.u[G.sel]; int best = 0; long bd = -1;
        for (int f = 0; f < 8; f++) { int fx, fy; arc_board_dir(f, &fx, &fy); long d = (long)fx * (tx - u->x) + (long)fy * (ty - u->y); if (d > bd) { bd = d; best = f; } }
        u->facing = best;
    }
}
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = board_cam(); arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.45f), 0xFFF2E0, 0x485868);
    arc_set_shadows(arc_v3_make(8, 0, 6), 12, 2048);
    arc_set_sky(0x46628C, 0x9AB0C8);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {   /* the ground as columns that follow the bumps */
        float cx = (float)x + 0.5f, cz = (float)y + 0.5f, h = ground(NULL, cx, cz);
        char c = MAP[y][x]; uint32_t col = c == 'f' ? 0x4E7A3A : c == '~' ? 0x3A6A9A : ((x + y) & 1) ? 0x8A8070 : 0x948A78;
        arc_draw_mesh(m_tile, arc_m4_trs_y(arc_v3_make(cx, (h - 1) * 0.5f, cz), 0, arc_v3_make(1, h + 1, 1)), col);
        if (c == '#') arc_draw_mesh(m_cube, arc_m4_trs_y(arc_v3_make(cx, h + 0.6f, cz), 0, arc_v3_make(1, 1.2f, 1)), 0x9A9AA4);
        if (c == 'f') arc_draw_mesh(m_unit, arc_m4_trs_y(arc_v3_make(cx, h + 0.45f, cz), 0, arc_v3_make(0.35f, 0.9f, 0.35f)), 0x2E5A22);
    }
    Unit *s = &G.u[G.sel];
    /* where the selected unit can go: tiles, brighter for cheaper */
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int c = G.reach[y * W + x]; if (c <= 0) continue;
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_TILE, .x = (float)x + 0.5f, .z = (float)y + 0.5f, .size = 0.9f, .inner = 0.15f,
                                       .fill_rgb = 0x40B0FF, .fill_alpha = 0.45f - 0.04f * (float)c, .edge_rgb = 0x90D8FF, .edge_width = 1, .height = column_top });
    }
    /* the path to the tile under the mouse */
    int tx, ty;
    if (mouse_tile(&tx, &ty) && G.reach[ty * W + tx] > 0) {
        int xy[64], n = arc_board_path(&G.b, s->x, s->y, tx, ty, ARC_BOARD_DIAGONAL, xy, 32, NULL);
        float px = (float)s->x + 0.5f, pz = (float)s->y + 0.5f;
        for (int i = 0; i < n && i < 32; i++) {
            float nx = (float)xy[2 * i] + 0.5f, nz = (float)xy[2 * i + 1] + 0.5f;
            if (i == n - 1) arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_ARROW, .x = px, .z = pz, .x2 = nx, .z2 = nz, .size = 0.18f, .fill_rgb = 0xFFE060, .fill_alpha = 0.9f, .height = column_top, .lift = 0.04f });
            else arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_ARROW, .x = px, .z = pz, .x2 = nx, .z2 = nz, .size = 0.18f, .head = 0.01f, .fill_rgb = 0xFFE060, .fill_alpha = 0.9f, .height = column_top, .lift = 0.04f });
            px = nx; pz = nz;
        }
    }
    /* units: a ring each (shows through walls), a chevron for the facing, a diamond over enemies it can see */
    for (int i = 0; i < 3; i++) {
        Unit *u = &G.u[i]; float cx = (float)u->x + 0.5f, cz = (float)u->y + 0.5f, h = ground(NULL, cx, cz);
        uint32_t team = u->team ? 0xFF3828 : 0x38D8F0;
        arc_draw_mesh(m_unit, arc_m4_trs_y(arc_v3_make(cx, h + 0.45f, cz), 0, arc_v3_make(0.5f, 0.9f, 0.5f)), u->team ? 0xC04030 : 0x3070C0);
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = cx, .z = cz, .size = 0.95f, .inner = 0.78f, .fill_rgb = team, .fill_alpha = 0.85f,
                                       .edge_rgb = i == G.sel ? 0xFFFFFF : 0x101010, .edge_width = i == G.sel ? 2.0f : 1.0f, .on_top = true, .height = column_top });
        int fx, fy; arc_board_dir(u->facing, &fx, &fy);
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_CHEVRON, .x = cx + (float)fx * 0.62f, .z = cz + (float)fy * 0.62f, .size = 0.35f, .yaw = -arc_atan2f((float)fy, (float)fx),
                                       .fill_rgb = team, .fill_alpha = 1, .height = column_top });
        int cover;
        if (u->team && arc_board_los(&G.b, s->x, s->y, u->x, u->y, &cover))
            arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_DIAMOND, .x = cx, .z = cz, .size = 0.5f, .fill_rgb = cover ? 0xFFA030 : 0xFF3030, .fill_alpha = 0.9f,
                                           .y = h + 1.15f, .label = cover ? "HALF COVER" : "CLEAR SHOT", .label_height = 0.5f, .label_scale = 1.5f });
    }
    /* the drop zone along the south edge */
    int edge[2 * W], ne = arc_board_edge_tiles(&G.b, ARC_EDGE_SOUTH, true, edge, W);
    for (int i = 0; i < ne; i++)
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_TILE, .x = (float)edge[2 * i] + 0.5f, .z = (float)edge[2 * i + 1] + 0.5f, .size = 0.96f,
                                       .fill_rgb = 0x60FF80, .fill_alpha = 0.3f, .edge_rgb = 0x60FF80, .edge_width = 1.5f, .dash = 0.15f, .pulse = 1, .pulse_amount = 0.6f,
                                       .height = column_top, .label = i == ne / 2 ? "DROP ZONE" : NULL, .label_height = 0.3f });
    arc_draw_textf(10, 10, 2, 0xFFFFFF, "unit %d   reach %d tiles   TAB next unit, click a lit tile to move, F to face the mouse", G.sel, G.nreach);
}
static void dump(arc_json *j) {
    Unit *s = &G.u[G.sel];
    arc_json_num_k(j, "selected", G.sel); arc_json_num_k(j, "x", s->x); arc_json_num_k(j, "y", s->y); arc_json_num_k(j, "facing", s->facing);
    arc_json_num_k(j, "reach", G.nreach);
    arc_json_arr_k(j, "sees");
    for (int i = 0; i < 3; i++) { int cover; if (G.u[i].team != s->team && arc_board_los(&G.b, s->x, s->y, G.u[i].x, G.u[i].y, &cover)) { arc_json_obj(j); arc_json_num_k(j, "unit", i); arc_json_num_k(j, "cover", cover); arc_json_end(j); } }
    arc_json_end(j);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "move") && argc >= 2) { move_to(atoi(argv[0]), atoi(argv[1])); return true; }
    if (!strcmp(name, "select") && argc >= 1) { G.sel = arc_clampi(atoi(argv[0]), 0, 2); update_reach(); return true; }
    return false;
}
static size_t save_state(void *b, size_t cap) { if (cap >= sizeof G) memcpy(b, &G, sizeof G); return sizeof G; }
static bool load_state(const void *b, size_t n) { if (n != sizeof G) return false; memcpy(&G, b, sizeof G); return true; }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC board and ground markers", .width = 1100, .height = 700, .clear_color = 0x9AB0C8, .init = init, .tick = tick, .draw = draw,
                      .dump = dump, .command = command, .save_state = save_state, .load_state = load_state };
}
