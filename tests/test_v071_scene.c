/* ARC 0.7.1 scene speed-ups self-test (included once by test_core.c): view tests, cells and portals (pure walk and
   culled draws), levels of detail, static batches and the frame budget. Every name here starts with v071s_ (other test
   files share this translation unit). v071s_init() runs the camera-free checks; v071s_draw() draws into 64 x 64 render
   targets (the soft renderer: pixels read back at tick 9) and runs the camera checks; v071s_check() reads the targets
   and this frame's stats. v071s_draw() is called first in draw() and puts the engine's default camera and light back. */
static arc_texture v071s_t_cull, v071s_t_nocull, v071s_t_lod, v071s_t_unb, v071s_t_bat;
static arc_mesh v071s_red, v071s_green, v071s_blue, v071s_tile, v071s_tile2, v071s_glow;
static arc_static v071s_st, v071s_st2, v071s_st3;
static int v071s_drawn; static arc_mesh v071s_gone_base;
static const arc_camera V071S_DEF = { { 0, 2, 6 }, { 0, 0, 0 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 500.0f };   /* the engine's default */
static const uint8_t *v071s_px(arc_texture t, int x, int y) { return arc__textures[t.id].rgba + ((size_t)y * 64 + x) * 4; }
static bool v071s_bit(const uint32_t *b, int i) { return (b[i >> 5] >> (i & 31)) & 1u; }

/* three rooms in a row along +x (cells 1, 2, 3: x 0-4, 4-8, 8-12; z 0-4; y 0-3) and one north of the first (cell 4:
   z 4-8), doors 1 wide and 2.5 high: 1|2 and 2|3 at z 1.5-2.5, 1|4 at x 1.5-2.5 */
static int v071s_p12, v071s_p23, v071s_p14;
static void v071s_rooms(void) {
    arc_cells_clear();
    for (int i = 0; i < 3; i++) arc_cell_add(1 + i, arc_v3_make(4.0f * (float)i, 0, 0), arc_v3_make(4.0f * (float)i + 4, 3, 4));
    arc_cell_add(4, arc_v3_make(0, 0, 4), arc_v3_make(4, 3, 8));
    v071s_p12 = arc_portal_add(1, 2, arc_v3_make(4, 0, 1.5f), arc_v3_make(4, 2.5f, 2.5f));
    v071s_p23 = arc_portal_add(2, 3, arc_v3_make(8, 0, 1.5f), arc_v3_make(8, 2.5f, 2.5f));
    v071s_p14 = arc_portal_add(1, 4, arc_v3_make(1.5f, 0, 4), arc_v3_make(2.5f, 2.5f, 4));
}
static int v071s_mask(const uint32_t *b) { int m = 0; for (int i = 1; i < 16; i++) if (v071s_bit(b, i)) m |= 1 << i; return m; }
static void v071s_cells_pure(void) {
    uint32_t bits[ARC_MAX_CELLS / 32];
    v071s_rooms();
    CHECK(v071s_p12 == 1 && v071s_p23 == 2 && v071s_p14 == 3);
    CHECK(arc_cell_at(arc_v3_make(1, 1, 1)) == 1 && arc_cell_at(arc_v3_make(5, 1, 1)) == 2 && arc_cell_at(arc_v3_make(1, 1, 6)) == 4);
    CHECK(arc_cell_at(arc_v3_make(100, 0, 0)) == 0 && arc_cell_at(arc_v3_make(1, 5, 1)) == 0);
    arc_cell_add(9, arc_v3_make(0.1f, 0, 0.1f), arc_v3_make(0.5f, 1, 0.5f));      /* a cupboard inside room 1: the smaller wins */
    CHECK(arc_cell_at(arc_v3_make(0.3f, 0.5f, 0.3f)) == 9 && arc_cell_at(arc_v3_make(1, 0.5f, 1)) == 1);
    v071s_rooms();
    arc_camera east = { { 1, 1.5f, 2 }, { 10, 1.5f, 2 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 100 };
    CHECK(arc_cells_compute(&east, 64, 64, bits) == 3 && v071s_mask(bits) == ((1 << 1) | (1 << 2) | (1 << 3)));   /* through both doors; not 4 (to the side) */
    arc_portal_set_open(v071s_p23, false); CHECK(!arc_portal_is_open(v071s_p23) && arc_portal_is_open(v071s_p12));
    CHECK(arc_cells_compute(&east, 64, 64, bits) == 2 && !v071s_bit(bits, 3));
    arc_portal_set_open(v071s_p23, true);
    arc_camera north = { { 1, 1.5f, 2 }, { 1, 1.5f, 10 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 100 };
    CHECK(arc_cells_compute(&north, 64, 64, bits) == 2 && v071s_mask(bits) == ((1 << 1) | (1 << 4)));
    arc_camera west = { { 3, 1.5f, 2 }, { -10, 1.5f, 2 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 100 };   /* doors behind: only its own room */
    CHECK(arc_cells_compute(&west, 64, 64, bits) == 1 && v071s_bit(bits, 1));
    arc_camera doorway = { { 3.9f, 1.5f, 2 }, { 3.9f, 1.5f, -10 }, { 0, 1, 0 }, 1.6f, 0, 0.1f, 100 };   /* beside a door, looking along the wall */
    CHECK(arc_cells_compute(&doorway, 64, 64, bits) >= 1 && v071s_bit(bits, 1));
    /* top-down over room 2 from outside every cell: the cells in view */
    arc_camera top = { { 6, 20, 2 }, { 6, 0, 2 }, { 0, 0, -1 }, 0, 2.0f, 0.1f, 100 };
    CHECK(arc_cells_compute(&top, 64, 64, bits) == 1 && v071s_bit(bits, 2));
    top.ortho_height = 7.0f; CHECK(arc_cells_compute(&top, 64, 64, bits) == 4);   /* x 2.5..9.5, z -1.5..5.5: all four */
    top.eye.x = top.target.x = 14; top.ortho_height = 2; CHECK(arc_cells_compute(&top, 64, 64, bits) == 0);   /* past the rooms */
    /* the view narrows at each door: a far door well inside the screen but outside what the first door shows is not
       followed (a frustum-only test would see room 3) */
    arc_cells_clear();
    for (int i = 0; i < 3; i++) arc_cell_add(1 + i, arc_v3_make(4.0f * (float)i, 0, 0), arc_v3_make(4.0f * (float)i + 4, 3, 4));
    arc_portal_add(1, 2, arc_v3_make(4, 0, 1.5f), arc_v3_make(4, 2.5f, 2.5f));
    arc_portal_add(2, 3, arc_v3_make(8, 0, 0.1f), arc_v3_make(8, 2.5f, 0.6f));
    CHECK(arc_cells_compute(&east, 64, 64, bits) == 2 && !v071s_bit(bits, 3));
    arc_camera wide = east; wide.fov_y = 2.4f; wide.eye = arc_v3_make(3.5f, 1.5f, 2);   /* near the door it opens up: room 3 seen */
    CHECK(arc_cells_compute(&wide, 64, 64, bits) == 3 && v071s_bit(bits, 3));
    arc_cells_clear(); CHECK(arc_cell_at(arc_v3_make(1, 1, 1)) == 0 && arc_cell_visible(1) && arc_cells_visible_count() == 0);
}
static arc_mesh v071s_quad(uint32_t rgb) {   /* 1 x 1 quad on the floor (y = 0) facing up, u along x */
    arc_vertex v[4]; memset(v, 0, sizeof v);
    for (int i = 0; i < 4; i++) { v[i].px = (i == 1 || i == 2) ? 0.5f : -0.5f; v[i].pz = (i >= 2) ? -0.5f : 0.5f; v[i].ny = 1; v[i].color = arc_vcol(rgb); v[i].u = v[i].px + 0.5f; v[i].v = v[i].pz + 0.5f; }
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    return arc_mesh_create(v, 4, idx, 6);
}
static void v071s_init(void) {
    v071s_cells_pure();
    /* LOD meshes: red base, green level 1 (beyond 10 units), blue level 2 (under a tenth of the view's height) */
    v071s_red = arc_mesh_cube(0xFF0000); v071s_green = arc_mesh_cube(0x00FF00); v071s_blue = arc_mesh_cube(0x0000FF);
    arc_mesh_set_lod_distance(v071s_red, 1, v071s_green, 10);
    arc_mesh_set_lod(v071s_red, 2, v071s_blue, 0.1f);
    arc_mesh_set_lod(v071s_red, 4, v071s_blue, 0.1f);    /* refused (levels are 1..3) */
    /* destroying a level takes it out of the list */
    arc_mesh l = arc_mesh_cube(0xFFFFFF); v071s_gone_base = arc_mesh_cube(0xFFFFFF);
    arc_mesh_set_lod_distance(v071s_gone_base, 1, l, 1);
    arc_mesh_destroy(l);
    v071s_tile = v071s_quad(0xFFFFFF); v071s_tile2 = v071s_quad(0xC0C0C0);
    v071s_glow = v071s_quad(0xFFFFFF);
    arc_material gm; memset(&gm, 0, sizeof gm); gm.emissive_rgb[0] = 0.0f; gm.emissive_rgb[1] = 0.4f; gm.emissive_rgb[2] = 0.0f;
    arc_mesh_set_material(v071s_glow, &gm);
    uint8_t tx[4 * 4 * 4]; for (int i = 0; i < 16; i++) { tx[i * 4] = (uint8_t)(i * 16); tx[i * 4 + 1] = 200; tx[i * 4 + 2] = (uint8_t)(255 - i * 16); tx[i * 4 + 3] = 255; }
    arc_mesh_set_texture(v071s_tile2, arc_texture_create(4, 4, tx, false));
    /* static batch: a 6 x 6 floor of tiles, tinted, some mirrored, some textured, two glowing; and the same as single draws */
    v071s_st = arc_static_create(); CHECK(v071s_st.id != 0);
    for (int z = 0; z < 6; z++) for (int x = 0; x < 6; x++) {
        arc_mesh m = (x + z) % 5 == 0 ? v071s_tile2 : (x == 1 && z == 3) || (x == 3 && z == 1) ? v071s_glow : v071s_tile;
        arc_v3 s = arc_v3_make((x & 1) ? -0.7f : 0.7f, 1, 0.7f);   /* odd columns mirrored */
        uint32_t tint = 0xFF0000u >> (8 * ((x + 2 * z) % 3)) | 0x404040u;
        arc_static_add(v071s_st, m, arc_m4_trs_y(arc_v3_make((float)x - 2.5f, 0, (float)z - 2.5f), 0.1f * (float)z, s), tint);
    }
    arc_static_add(v071s_st, (arc_mesh){ 0 }, arc_m4_identity(), 0xFFFFFF);   /* ignored */
    CHECK(arc_static_item_count(v071s_st) == 36 && arc_static_mesh_count(v071s_st) == 0);
    CHECK(arc_static_build(v071s_st) && arc_static_mesh_count(v071s_st) == 3);   /* plain, textured, glowing */
    CHECK(arc_static_build(v071s_st) && arc_static_mesh_count(v071s_st) == 3);   /* a rebuild replaces, not adds */
    /* by cell: the same tiles split over two cells (-1: from their centres) */
    arc_cells_clear(); arc_cell_add(5, arc_v3_make(-3, -1, -3), arc_v3_make(0, 1, 3)); arc_cell_add(6, arc_v3_make(0, -1, -3), arc_v3_make(3, 1, 3));
    v071s_st2 = arc_static_create();
    for (int z = 0; z < 6; z++) for (int x = 0; x < 6; x++) arc_static_add_cell(v071s_st2, v071s_tile, arc_m4_translate(arc_v3_make((float)x - 2.5f, 0, (float)z - 2.5f)), 0xFFFFFF, -1);
    CHECK(arc_static_build(v071s_st2) && arc_static_mesh_count(v071s_st2) == 2);
    /* by chunk: 2 x 2 chunks of size 3 */
    v071s_st3 = arc_static_create(); arc_static_set_chunk(v071s_st3, 3);
    for (int z = 0; z < 6; z++) for (int x = 0; x < 6; x++) arc_static_add(v071s_st3, v071s_tile, arc_m4_translate(arc_v3_make((float)x - 2.5f, 0, (float)z - 2.5f)), 0xFFFFFF);
    CHECK(arc_static_build(v071s_st3) && arc_static_mesh_count(v071s_st3) == 4);
    arc_static tmp = arc_static_create(); arc_mesh gone = arc_mesh_cube(0xFFFFFF);   /* a destroyed source is skipped */
    arc_static_add(tmp, gone, arc_m4_identity(), 0xFFFFFF); arc_static_add(tmp, v071s_tile, arc_m4_identity(), 0xFFFFFF);
    arc_mesh_destroy(gone);
    CHECK(arc_static_build(tmp) && arc_static_mesh_count(tmp) == 1);
    arc_static_clear(tmp); CHECK(arc_static_item_count(tmp) == 0 && arc_static_mesh_count(tmp) == 1);
    arc_static_destroy(tmp); CHECK(arc_static_mesh_count(tmp) == 0);
    /* frame budget */
    arc_frame_budget fb = arc_get_frame_budget(); CHECK(fabsf(fb.budget_ms - 16.6667f) < 0.01f);
    arc_set_frame_budget(8.0f); CHECK(fabsf(arc_get_frame_budget().budget_ms - 8.0f) < 1e-4f);
    arc_set_frame_budget(0); CHECK(fabsf(arc_get_frame_budget().budget_ms - 16.6667f) < 0.01f);
}
static void v071s_view(const arc_camera *c) {
    arc_set_camera(c); arc_set_light(arc_v3_make(0.3f, 1.0f, 0.5f), 0xFFFFFF, 0x303030); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
}
static int v071s_lv[8]; static bool v071s_vis[12];
static void v071s_draw(void) {
    if (v071s_drawn++) return;
    if (!v071s_t_cull.id) {
        v071s_t_cull = arc_target_create(64, 64, false); v071s_t_nocull = arc_target_create(64, 64, false); v071s_t_lod = arc_target_create(64, 64, false);
        v071s_t_unb = arc_target_create(64, 64, false); v071s_t_bat = arc_target_create(64, 64, false);
    }
    /* view tests: camera at the origin looking down -z */
    arc_camera c0 = { { 0, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 500 };
    arc_target_begin(v071s_t_lod, true, 0xFF000000);
    v071s_view(&c0);
    v071s_vis[0] = arc_sphere_visible(arc_v3_make(0, 0, -5), 0.5f);
    v071s_vis[1] = arc_sphere_visible(arc_v3_make(0, 0, 5), 0.5f);          /* behind */
    v071s_vis[2] = arc_sphere_visible(arc_v3_make(20, 0, -5), 1.0f);        /* far to the side */
    v071s_vis[3] = arc_sphere_visible(arc_v3_make(0, 0, -600), 1.0f);       /* past the far plane */
    v071s_vis[4] = arc_sphere_visible(arc_v3_make(3.2f, 0, -5), 0.6f);      /* straddling the edge (x edge at 2.73) */
    v071s_vis[5] = arc_aabb_visible(arc_v3_make(-1, -1, -6), arc_v3_make(1, 1, -4));
    v071s_vis[6] = arc_aabb_visible(arc_v3_make(-1, -1, 4), arc_v3_make(1, 1, 6));
    v071s_vis[7] = arc_aabb_visible(arc_v3_make(-100, -1, -6), arc_v3_make(100, 1, -4));   /* spans the view */
    v071s_vis[8] = arc_aabb_visible(arc_v3_make(10, 10, -6), arc_v3_make(11, 11, -4));
    /* LOD levels and pixels: left near (red base), middle beyond 10 (green), right far and small enough (blue) */
    v071s_lv[0] = arc_mesh_lod_level(v071s_red, arc_m4_translate(arc_v3_make(0, 0, -5)));
    v071s_lv[1] = arc_mesh_lod_level(v071s_red, arc_m4_translate(arc_v3_make(0, 0, -15)));
    v071s_lv[2] = arc_mesh_lod_level(v071s_red, arc_m4_translate(arc_v3_make(0, 0, -200)));
    arc_set_lod_bias(16); v071s_lv[3] = arc_mesh_lod_level(v071s_red, arc_m4_translate(arc_v3_make(0, 0, -200))); arc_set_lod_bias(0);
    arc_set_lod_force(0); v071s_lv[4] = arc_mesh_lod_level(v071s_red, arc_m4_translate(arc_v3_make(0, 0, -200)));
    arc_set_lod_force(3); v071s_lv[5] = arc_mesh_lod_level(v071s_red, arc_m4_translate(arc_v3_make(0, 0, -5))); arc_set_lod_force(-1);
    v071s_lv[6] = arc_mesh_lod_level(v071s_red, arc_m4_trs_y(arc_v3_make(0, 0, -200), 0, arc_v3_make(20, 20, 20)));   /* big: by size, not level 2 */
    v071s_lv[7] = arc_mesh_lod_level(v071s_gone_base, arc_m4_translate(arc_v3_make(0, 0, -200)));                      /* its level was destroyed */
    arc_draw_mesh(v071s_red, arc_m4_trs_y(arc_v3_make(-1.5f, 0, -5), 0, arc_v3_make(1, 1, 1)), 0xFFFFFF);
    arc_draw_mesh(v071s_red, arc_m4_trs_y(arc_v3_make(0, 0, -15), 0, arc_v3_make(4, 4, 4)), 0xFFFFFF);
    arc_draw_mesh(v071s_red, arc_m4_trs_y(arc_v3_make(20, 0, -60), 0, arc_v3_make(3, 3, 3)), 0xFFFFFF);
    arc_target_end();
    /* culled draws: from room 1 looking east, door 2|3 closed: the green cube in room 3 is not queued */
    for (int pass = 0; pass < 2; pass++) {
        v071s_rooms(); arc_portal_set_open(v071s_p23, false);
        arc_cells_set_culling(pass == 0);
        arc_target_begin(pass ? v071s_t_nocull : v071s_t_cull, true, 0xFF000000);
        arc_camera e = { { 1, 1.5f, 2 }, { 10, 1.5f, 2 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 100 };
        v071s_view(&e); arc_set_light(arc_v3_make(-1, 0.4f, 0.2f), 0xFFFFFF, 0x303030);   /* lights the cubes' faces toward the camera */
        if (!pass) { v071s_vis[9] = arc_cell_visible(2); v071s_vis[10] = arc_cell_visible(3); v071s_vis[11] = arc_cells_visible_count() == 2; }
        arc_set_draw_cell(2); arc_draw_cube(arc_v3_make(6, 1.5f, 1.2f), arc_v3_make(0.5f, 0.5f, 0.5f), 0xFF0000);
        arc_set_draw_cell(3); arc_draw_cube(arc_v3_make(10, 1.5f, 2.8f), arc_v3_make(1, 1, 1), 0x00FF00);
        arc_set_draw_cell(0); arc_draw_particles();   /* effects are never culled (nothing to see here: just no crash) */
        arc_target_end();
    }
    arc_cells_set_culling(true);
    /* static batch against the same tiles drawn one by one (no shadows; a light from the side) */
    arc_camera top = { { 0, 6, 4 }, { 0, 0, 0 }, { 0, 1, 0 }, 1.1f, 0, 0.1f, 100 };
    for (int pass = 0; pass < 2; pass++) {
        arc_target_begin(pass ? v071s_t_bat : v071s_t_unb, true, 0xFF000000);
        v071s_view(&top);
        if (pass) arc_static_draw(v071s_st);
        else for (int z = 0; z < 6; z++) for (int x = 0; x < 6; x++) {
            arc_mesh m = (x + z) % 5 == 0 ? v071s_tile2 : (x == 1 && z == 3) || (x == 3 && z == 1) ? v071s_glow : v071s_tile;
            arc_v3 s = arc_v3_make((x & 1) ? -0.7f : 0.7f, 1, 0.7f);
            uint32_t tint = 0xFF0000u >> (8 * ((x + 2 * z) % 3)) | 0x404040u;
            arc_draw_mesh(m, arc_m4_trs_y(arc_v3_make((float)x - 2.5f, 0, (float)z - 2.5f), 0.1f * (float)z, s), tint);
        }
        arc_target_end();
    }
    arc_cells_clear();
    arc_set_camera(&V071S_DEF); arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850); arc_set_fog(0, 0, 0); arc_set_rim(0, 3.0f);
}
static void v071s_check(void) {
    CHECK(v071s_drawn >= 1);
    CHECK(v071s_vis[0] && !v071s_vis[1] && !v071s_vis[2] && !v071s_vis[3] && v071s_vis[4]);
    CHECK(v071s_vis[5] && !v071s_vis[6] && v071s_vis[7] && !v071s_vis[8]);
    CHECK(v071s_lv[0] == 0 && v071s_lv[1] == 1 && v071s_lv[2] == 2 && v071s_lv[3] == 1 && v071s_lv[4] == 0 && v071s_lv[5] == 2 && v071s_lv[6] == 1 && v071s_lv[7] == 0);
    /* LOD pixels: the near cube red, the middle one green, the far one blue */
    arc_v2 sp; arc_camera c0 = { { 0, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 500 };
    const uint8_t *p;
    CHECK(arc_camera_to_screen(&c0, 64, 64, arc_v3_make(-1.5f, 0.0f, -4.5f), &sp)); p = v071s_px(v071s_t_lod, (int)sp.x, (int)sp.y); CHECK(p[0] > 60 && p[1] < 30 && p[2] < 30);
    CHECK(arc_camera_to_screen(&c0, 64, 64, arc_v3_make(0, 0.5f, -13), &sp)); p = v071s_px(v071s_t_lod, (int)sp.x, (int)sp.y); CHECK(p[1] > 60 && p[0] < 30 && p[2] < 30);
    CHECK(arc_camera_to_screen(&c0, 64, 64, arc_v3_make(20, 0, -58.5f), &sp)); p = v071s_px(v071s_t_lod, (int)sp.x, (int)sp.y); CHECK(p[2] > 60 && p[0] < 30 && p[1] < 30);
    /* cells: room 2 seen, room 3 behind the closed door not; its cube is not drawn there, but is with culling off */
    CHECK(v071s_vis[9] && !v071s_vis[10] && v071s_vis[11]);
    arc_camera e = { { 1, 1.5f, 2 }, { 10, 1.5f, 2 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 100 };
    CHECK(arc_camera_to_screen(&e, 64, 64, arc_v3_make(9.5f, 1.5f, 2.8f), &sp));
    p = v071s_px(v071s_t_cull, (int)sp.x, (int)sp.y); CHECK(p[1] < 10);
    p = v071s_px(v071s_t_nocull, (int)sp.x, (int)sp.y); CHECK(p[1] > 60);
    CHECK(arc_camera_to_screen(&e, 64, 64, arc_v3_make(5.75f, 1.5f, 1.2f), &sp));
    p = v071s_px(v071s_t_cull, (int)sp.x, (int)sp.y); CHECK(p[0] > 60);   /* room 2's cube is drawn */
    /* static batch: the same picture as the single draws (tints baked into vertex colours round to within 1/255) */
    int worst = 0, off = 0, lit = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        const uint8_t *a = v071s_px(v071s_t_unb, x, y), *b = v071s_px(v071s_t_bat, x, y); int d = 0;
        for (int k = 0; k < 3; k++) d = arc_maxi(d, abs(a[k] - b[k]));
        worst = arc_maxi(worst, d); if (d > 3) off++; if (a[0] + a[1] + a[2] > 60) lit++;
    }
    CHECK(lit > 800); CHECK(off <= 4);
    /* this frame's stats: culled draws, LOD counts, static draws */
    arc_gfx_stats st = arc_stats();
    CHECK(st.culled >= 1); CHECK(st.lod_instances[0] >= 1 && st.lod_instances[1] >= 1 && st.lod_instances[2] >= 1);
    CHECK(st.static_draws == 3);
    arc_frame_budget fb = arc_get_frame_budget();
    CHECK(fb.frame_ms >= 0 && fb.tick_ms >= 0 && fb.draw_ms >= 0 && fb.other_ms >= 0 && fb.zone_count >= 0 && fb.zone_count <= 8);
}
