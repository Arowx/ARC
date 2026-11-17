/* Hot reload sample: change the code while it runs.
 *   Linux:   ./build.sh hot samples/hot/hot.c   then   build/arc_host --game build/libgame.so
 *   Windows: build.bat hot samples\hot\hot.c     then   build\arc_host.exe   (game.dll next to it)
 * Edit COLOUR or the spin speed below and run the same build command again: the running game picks up the
 * new code at once and keeps its state (the angle and the counter live in arc_persist memory). */
#include "../../include/arc.h"
#include <stdio.h>

#define COLOUR 0xFF4020

typedef struct { float angle; int ticks; arc_mesh cube; bool ready; } state;
static state *S;            /* points into memory that survives reloads */
static float spin;          /* a tunable: registered again after every reload */

static void attach(void) {
    S = (state *)arc_persist("hot.state", sizeof(state));
    arc_var("spin", &spin, 1.5f);
}
static void init(void) {
    attach();
    if (!S->ready) { S->cube = arc_mesh_cube(0xFFFFFF); S->ready = true; }
}
static void reloaded(void) { attach(); arc_log("new code loaded at tick %d", S->ticks); }
static void tick(void) { S->angle += spin * arc_dt(); S->ticks++; }
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = { { 0, 2, 4 }, { 0, 0, 0 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 50 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.6f), 0xFFFFFF, 0x303840);
    arc_draw_mesh(S->cube, arc_m4_trs_y(arc_v3_make(0, 0, 0), S->angle, arc_v3_make(1.5f, 1.5f, 1.5f)), COLOUR);
    arc_draw_textf(10, 10, 2, 0xFFFFFF, "tick %d  - edit hot.c and rebuild", S->ticks);
}
static void dump(arc_json *j) { arc_json_num_k(j, "angle", S->angle); arc_json_int_k(j, "ticks", S->ticks); arc_json_int_k(j, "colour", COLOUR); }
static bool command(const char *name, int argc, const char **argv) {
    (void)argc; (void)argv;
    if (!strcmp(name, "colour")) { printf("ARC echo colour %06X\n", COLOUR); return true; }
    return false;
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC hot reload", .width = 800, .height = 450, .init = init, .tick = tick, .draw = draw,
                      .dump = dump, .command = command, .reloaded = reloaded };
}
