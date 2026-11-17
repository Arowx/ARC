/* ARC 0.10.8 test fixture (Droids' deck-editor request): a two-layer level edited with objects, layer settings, copy and
   paste, the onion skin and test-play. Checked by tests/level_check.md (agent commands) and tests/level_launch_check.md
   (--editor FILE). Saves to level_out.arclvl beside the program. */
#include "../include/arc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
static arc_level *LV;
static struct { int layer, px, py, playtests, file_objects; bool file_same; char probe[2]; } G;
static const char *ROWS[2][6] = {
    { "################", "#..............#", "#..............#", "#..............#", "#.....L........#", "################" },
    { "################", "#..K...........#", "#..............#", "#..............#", "#.....L........#", "################" } };
static const arc_editor_tile PAL[] = {
    { '.', "Floor", 0x2A3038, 0, { 0 }, 0, 0, 0, 0, 0 }, { '#', "Wall", 0x8A93A6, 0, { 0 }, 0, 0, 0, 0, 0 },
    { 'D', "Door", 0xD0B040, 0, { 0 }, 0, 0, 0, 0, 0 }, { 'L', "Lift", 0x40B0E0, 0, { 0 }, 0, 0, 0, 0, 0 },
    { 'K', "Locked", 0xE04040, 0, { 0 }, 0, 0, 0, 0, 0 } };
static const arc_editor_field DROID_F[] = { { "class", ARC_FIELD_INT, 1, 9, 1, "1", NULL, false }, { "number", ARC_FIELD_INT, 100, 999, 1, NULL, NULL, true },
                                            { "facing", ARC_FIELD_FACING, 0, 3, 1, NULL, NULL, false } };
static const arc_editor_field CAMERA_F[] = { { "wall", ARC_FIELD_CHOICE, 0, 0, 0, NULL, "n|e|s|w", false } };
static const arc_editor_object OBJS[] = { { "droid", "Droid", 0x40D0FF, 0, { 0 }, 0, 0, 0, 0, DROID_F, 3 },
                                          { "camera", "Camera", 0xFFA040, 0, { 0 }, 0, 0, 0, 0, CAMERA_F, 1 } };
static const arc_editor_field LAYER_F[] = { { "music", ARC_FIELD_TEXT, 0, 0, 0, NULL, NULL, true }, { "tint", ARC_FIELD_NUM, 0, 1, 0.1f, "1", NULL, true } };
static void playtest(int layer, int x, int y, void *u) { (void)u; G.layer = layer; G.px = x; G.py = y; G.playtests++; }
static void init(void) {
    LV = arc_level_new();
    for (int l = 0; l < 2; l++) {
        arc_level_add_layer(LV, l ? "B" : "A", 16, 6, '.');
        for (int y = 0; y < 6; y++) memcpy(arc_level_cells(LV, l) + y * 17, ROWS[l][y], 16);
    }
    arc_editor_begin(&(arc_editor_desc){ .name = "Level fixture", .tiles = PAL, .tile_count = 5, .level = LV, .level_path = "level_out.arclvl",
                     .objects = OBJS, .object_count = 2, .layer_fields = LAYER_F, .layer_field_count = 2, .playtest = playtest });
    G.probe[0] = '?';
}
static void tick(void) {}
static void draw(float alpha) { (void)alpha; arc_draw_textf(10, 10, 2, 0xFFFFFF, "LAYER %d AT %d,%d", G.layer, G.px, G.py); }
static void dump(arc_json *j) {
    arc_json_obj_k(j, "player"); arc_json_int_k(j, "x", G.px); arc_json_int_k(j, "y", G.py); arc_json_int_k(j, "layer", G.layer); arc_json_end(j);
    arc_json_int_k(j, "playtests", G.playtests); arc_json_int_k(j, "file_objects", G.file_objects); arc_json_bool_k(j, "file_same", G.file_same);
    arc_json_str_k(j, "probe", G.probe);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "probe") && argc >= 2) { int l = argc >= 3 ? atoi(argv[2]) : arc_editor_layer(); G.probe[0] = arc_level_cell(LV, l, atoi(argv[0]), atoi(argv[1])); G.probe[1] = 0; return true; }
    if (!strcmp(name, "reread")) {   /* the saved file: its objects on layer 0, and whether it is the level's text byte for byte */
        arc_level *f = arc_level_load("level_out.arclvl"); G.file_objects = f ? arc_level_object_count(f, 0) : -1;
        size_t a = 0, b = 0; char *ta = arc_level_text(LV, &a), *tb = f ? arc_level_text(f, &b) : NULL;
        size_t fn = 0; uint8_t *fb = arc_file_read("level_out.arclvl", arc_frame(), &fn);
        G.file_same = tb && a == b && !memcmp(ta, tb, a) && fb && fn == a && !memcmp(fb, ta, a);
        free(ta); free(tb); arc_level_free(f); return true;
    }
    return false;
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC level fixture", .width = 1280, .height = 720, .clear_color = 0x101418, .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command };
}
