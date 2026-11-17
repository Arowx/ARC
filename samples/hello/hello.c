/* The smallest ARC program: a spinning cube you can move with the arrow keys. Copy this to start a game. */
#include "../../include/arc.h"

static arc_mesh cube;
static arc_v3 pos, prev;
static float spin, speed;

static void init(void) {
    cube = arc_mesh_cube(0x40E0C0);
    arc_var("speed", &speed, 4.0f);          /* agents can change it: `set speed 8` */
}
static void tick(void) {                      /* all game logic, fixed 60 Hz */
    prev = pos;
    if (arc_key_down(ARC_KEY_LEFT))  pos.x -= speed * arc_dt();
    if (arc_key_down(ARC_KEY_RIGHT)) pos.x += speed * arc_dt();
    if (arc_key_down(ARC_KEY_UP))    pos.z -= speed * arc_dt();
    if (arc_key_down(ARC_KEY_DOWN))  pos.z += speed * arc_dt();
    spin += arc_dt();
}
static void draw(float alpha) {               /* render only; alpha blends between ticks */
    arc_camera cam = { .eye = { 0, 3, 6 }, .target = { 0, 0, 0 }, .fov_y = 1.0f };
    arc_set_camera(&cam);
    arc_draw_grid(5, 1, 0x2A3444);
    arc_v3 p = arc_v3_lerp(prev, pos, alpha); p.y = 0.5f;          /* sit on the grid */
    arc_draw_mesh(cube, arc_m4_trs_y(p, spin, arc_v3_make(1, 1, 1)), ARC_WHITE);
    arc_draw_text(10, 10, 2, ARC_WHITE, "HELLO ARC - ARROWS MOVE");
}
static void dump(arc_json *j) { arc_json_v3_k(j, "pos", pos); }   /* what agents see with `dump` */

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "Hello ARC", .init = init, .tick = tick, .draw = draw, .dump = dump };
}
