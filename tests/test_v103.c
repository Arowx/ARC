/* ARC 0.10.3 self-test: the tile editor's layers and its own 3D view (the deck template's acceptance checks draw it).
   Included by test_core.c; v103_init() runs in init. Names start with v103_ (one translation unit). */
static char v103_deck[2][3][5];   /* two layers of 4 x 3 */
static int v103_changes, v103_last_layer;
static void v103_changed(int x, int y, int o, int n, void *u) { (void)x; (void)y; (void)o; (void)n; (void)u; v103_changes++; v103_last_layer = arc_editor_layer(); }
static int v103_mesh(int x, int y, int id, arc_editor_part *out, int max, void *u) { (void)x; (void)y; (void)u; (void)max; (void)out; return id == '#' ? 1 : 0; }
static void v103_init(void) {
    for (int l = 0; l < 2; l++) for (int y = 0; y < 3; y++) { memset(v103_deck[l][y], '.', 4); v103_deck[l][y][4] = 0; }
    static const arc_editor_tile tl[] = { { '.', "Floor", 0x202020, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 }, { '#', "Wall", 0x808080, ARC_KEY_NONE, { 0 }, 0, 0, 0, 0, 0 } };
    static void *layers[2] = { v103_deck[0], v103_deck[1] };
    arc_editor_desc d; memset(&d, 0, sizeof d);
    d.name = "decks"; d.width = 4; d.height = 3; d.stride = 5; d.tiles = tl; d.tile_count = 2; d.changed = v103_changed;
    d.layers = layers; d.layer_count = 2;   /* no cells: layers[0] first */
    arc_editor_begin(&d);
    CHECK(arc_editor_layer() == 0 && arc_editor_get(0, 0) == '.');
    CHECK(arc_editor_paint(1, 1, '#') && v103_deck[0][1][1] == '#' && v103_last_layer == 0);
    arc_editor_set_layer(1); CHECK(arc_editor_layer() == 1 && arc_editor_get(1, 1) == '.');
    CHECK(arc_editor_paint(2, 2, '#') && v103_deck[1][2][2] == '#' && v103_deck[0][2][2] == '.' && v103_last_layer == 1);
    CHECK(arc_editor_undo() && v103_deck[1][2][2] == '.' && arc_editor_layer() == 1);
    CHECK(arc_editor_undo() && v103_deck[0][1][1] == '.' && arc_editor_layer() == 0 && v103_last_layer == 0);   /* undo goes back to its layer */
    CHECK(arc_editor_redo() && v103_deck[0][1][1] == '#');
    arc_editor_set_layer(5); CHECK(arc_editor_layer() == 1);   /* clamped */
    arc__test_command("editor layer 0"); CHECK(arc_editor_layer() == 0);
    /* views: the own 3D view needs tile_mesh, the game's view needs camera + world_cell */
    arc_editor_set_view(ARC_EDITOR_VIEW_3D); CHECK(arc_editor_view() == ARC_EDITOR_VIEW_2D);
    arc_editor_set_view(ARC_EDITOR_VIEW_GAME); CHECK(arc_editor_view() == ARC_EDITOR_VIEW_2D);
    d.tile_mesh = v103_mesh; arc_editor_begin(&d);
    arc_editor_set_view(ARC_EDITOR_VIEW_3D); CHECK(arc_editor_view() == ARC_EDITOR_VIEW_3D);
    arc__test_command("editor view 2d"); CHECK(arc_editor_view() == ARC_EDITOR_VIEW_2D);
    arc__test_command("editor view 3d"); arc__test_command("editor orbit 45 30 6 2 1.5"); CHECK(arc_editor_view() == ARC_EDITOR_VIEW_3D);
    arc_editor_end();
    CHECK(arc_editor_layer() == 0 && arc_editor_view() == 0);
}
