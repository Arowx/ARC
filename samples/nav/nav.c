/* ARC sample (0.9.2): a navmesh from a tile grid, agents that walk to random places, a click sends them all there.
   N shows or hides the navmesh, P the paths. Agent commands: `goto X Z` sends every agent, `scatter` sends each to a
   random point. dump() reports each agent's position and whether it has arrived. */
#include "../../include/arc.h"
#include <string.h>
#include <stdlib.h>

#define W 24
#define H 16
#define NAGENTS 12
static const char *LEVEL[H] = {
    "........................",
    "..####.........#####....",
    "..#............#........",
    "..#....####....#..####..",
    "..#.......#.......#.....",
    "......#...#.......#.....",
    "......#...######..#..##.",
    "..#####.............#...",
    "............#.......#...",
    "....###.....#...#####...",
    "....#.......#...........",
    "....#...#####.....####..",
    "....#.............#.....",
    "........#####.....#.....",
    "..............#.........",
    "........................",
};
typedef struct { arc_nav_agent a[NAGENTS]; bool show_mesh, show_paths; int arrivals; } Game;
static Game G;
static arc_grid grid;
static arc_navmesh nav;
static arc_mesh m_floor, m_wall, m_agent;
static arc_camera cam;

static void scatter(void) { for (int i = 0; i < NAGENTS; i++) arc_nav_agent_goto(&G.a[i], nav, arc_navmesh_random_point(nav, NULL)); }
static void init(void) {
    arc_grid_init(&grid, NULL, W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) arc_grid_set(&grid, x, y, LEVEL[y][x] == '#');
    nav = arc_navmesh_create(NULL);
    arc_navmesh_add_grid(nav, &grid, arc_v3_make(0, 0, 0), 1, NULL);
    arc_navmesh_build(nav);
    m_floor = arc_mesh_plane(0x46505E, 1, 0x46505E); m_wall = arc_mesh_cube(0x9098A8);
    m_agent = arc_mesh_cylinder(0xF0A040, 16); arc_mesh_set_name(m_agent, "agent");
    for (int i = 0; i < NAGENTS; i++) {
        arc_nav_agent_init(&G.a[i], arc_navmesh_random_point(nav, NULL), arc_rand_rangef(2.5f, 4.5f), 0.3f);
    }
    G.show_mesh = true; G.show_paths = true;
    cam = (arc_camera){ { 12, 17, 19 }, { 12, 0, 7.5f }, { 0, 1, 0 }, 0.8f, 0, 0.1f, 100 };
    scatter();
}
static void tick(void) {
    if (arc_key_pressed(ARC_KEY_N)) G.show_mesh = !G.show_mesh;
    if (arc_key_pressed(ARC_KEY_P)) G.show_paths = !G.show_paths;
    if (arc_key_pressed(ARC_MOUSE_LEFT)) {
        arc_v3 o, d; arc_camera_ray(&cam, (float)arc_width(), (float)arc_height(), arc_mouse(), &o, &d);
        if (d.y < -1e-4f) { float t = -o.y / d.y; arc_v3 p = arc_v3_add(o, arc_v3_scale(d, t)); for (int i = 0; i < NAGENTS; i++) arc_nav_agent_goto(&G.a[i], nav, p); }
    }
    for (int i = 0; i < NAGENTS; i++) {
        arc_nav_agent_update(&G.a[i], nav, arc_dt());
        if (G.a[i].arrived) { G.arrivals++; G.a[i].arrived = false; }
    }
    arc_nav_agents_separate(G.a, NAGENTS, nav);
    if (arc_key_pressed(ARC_KEY_SPACE)) scatter();
}
static void draw(float alpha) {
    (void)alpha;
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.5f), 0xFFF0E0, 0x404858);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        if (LEVEL[y][x] == '#') arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, 0.5f, (float)y + 0.5f), 0, arc_v3_make(1, 1, 1)), 0xFFFFFF);
        else arc_draw_mesh(m_floor, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, 0, (float)y + 0.5f), 0, arc_v3_make(1, 1, 1)), ((x + y) & 1) ? 0xFFFFFF : 0xE0E4EC);
    }
    if (G.show_mesh) arc_navmesh_draw(nav, 0x40E0A0);
    for (int i = 0; i < NAGENTS; i++) {
        arc_draw_mesh(m_agent, arc_m4_trs_y(arc_v3_add(G.a[i].pos, arc_v3_make(0, 0.35f, 0)), 0, arc_v3_make(0.6f, 0.7f, 0.6f)), G.a[i].moving ? 0xFFFFFF : 0x909090);
        if (G.show_paths) arc_nav_agent_draw(&G.a[i], 0xFFE060);
    }
    arc_draw_text(10, 10, 2, 0xFFFFFF, "click: send everyone   SPACE: scatter   N: navmesh   P: paths");
    arc_draw_textf(10, 32, 2, 0xB0B8C8, "%d triangles, %d islands, %d arrivals", arc_navmesh_triangles(nav), arc_navmesh_islands(nav), G.arrivals);
}
static void dump(arc_json *j) {
    int moving = 0; for (int i = 0; i < NAGENTS; i++) moving += G.a[i].moving;
    arc_json_num_k(j, "moving", moving); arc_json_num_k(j, "arrivals", G.arrivals);
    arc_json_num_k(j, "triangles", arc_navmesh_triangles(nav)); arc_json_num_k(j, "islands", arc_navmesh_islands(nav));
    arc_json_arr_k(j, "agents");
    for (int i = 0; i < NAGENTS; i++) {
        arc_json_obj(j); arc_json_num_k(j, "x", G.a[i].pos.x); arc_json_num_k(j, "z", G.a[i].pos.z);
        arc_json_bool_k(j, "moving", G.a[i].moving); arc_json_bool_k(j, "failed", G.a[i].failed); arc_json_end(j);
    }
    arc_json_end(j);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "goto") && argc >= 2) { arc_v3 p = arc_v3_make((float)atof(argv[0]), 0, (float)atof(argv[1])); for (int i = 0; i < NAGENTS; i++) arc_nav_agent_goto(&G.a[i], nav, p); return true; }
    if (!strcmp(name, "scatter")) { scatter(); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t size) { if (size != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC navigation", .width = 1100, .height = 700, .clear_color = 0x1A1E28, .init = init, .tick = tick,
                      .draw = draw, .dump = dump, .command = command, .save_state = save_state, .load_state = load_state };
}
