/* arc_doctor test fixture: rand(), a load in tick(), a draw in tick(), a missing asset, no dump */
#include "arc.h"
#include <stdlib.h>
static arc_texture t; static arc_mesh m;
static void init(void) { m = arc_mesh_load("ship.arcm"); }
static void tick(void) { if (rand() % 2) t = arc_texture_load("missing.png"); arc_draw_text(0, 0, 1, 0xFFFFFF, "hi"); }
static void draw(float a) { (void)a; arc_draw_mesh(m, arc_m4_identity(), 0xFFFFFF); }
arc_app arc_main(int argc, char **argv) { (void)argc; (void)argv; return (arc_app){ .title = "fixture", .init = init, .tick = tick, .draw = draw }; }
