/* ARC 0.9.6 self-test: terrain, splatting and scatter. Included by test_core.c; v096_init() runs in init, v096_draw()
   from draw, v096_check() with the frame-8 checks. Names start with v096_ (one translation unit). */
static arc_texture v096_tgt; static arc_terrain v096_flat;
static bool v096_near(float a, float b, float e) { return fabsf(a - b) <= e; }
static arc_texture v096_solid(uint32_t rgb) {
    uint8_t px[8 * 8 * 4];
    for (int i = 0; i < 64; i++) { px[i * 4] = (uint8_t)(rgb >> 16); px[i * 4 + 1] = (uint8_t)(rgb >> 8); px[i * 4 + 2] = (uint8_t)rgb; px[i * 4 + 3] = 255; }
    return arc_texture_create_ex(8, 8, px, ARC_TEX_MESH);
}
static float v096_tri_height(const arc_v3 *t, float x, float z, bool *in) {   /* height on one triangle (x, z), if inside */
    float d = (t[1].z - t[2].z) * (t[0].x - t[2].x) + (t[2].x - t[1].x) * (t[0].z - t[2].z);
    float a = ((t[1].z - t[2].z) * (x - t[2].x) + (t[2].x - t[1].x) * (z - t[2].z)) / d, b = ((t[2].z - t[0].z) * (x - t[2].x) + (t[0].x - t[2].x) * (z - t[2].z)) / d, c = 1 - a - b;
    *in = a >= -1e-5f && b >= -1e-5f && c >= -1e-5f;
    return a * t[0].y + b * t[1].y + c * t[2].y;
}
static void v096_heights(void) {
    /* a sloping plane: heights exact everywhere, normal as worked out, origin y added */
    static float h[9 * 5];
    for (int j = 0; j < 5; j++) for (int i = 0; i < 9; i++) h[j * 9 + i] = 0.5f * (float)i + 0.25f * (float)j;
    arc_terrain t = arc_terrain_create(&(arc_terrain_desc){ .heights = h, .samples_x = 9, .samples_z = 5, .size_x = 8, .size_z = 4, .origin = { 10, 2, 20 }, .chunk = 4 });
    CHECK(t.id != 0);
    CHECK(v096_near(arc_terrain_height(t, 10, 20), 2, 1e-5f) && v096_near(arc_terrain_height(t, 13.3f, 21.7f), 2 + 0.5f * 3.3f + 0.25f * 1.7f, 1e-4f));
    CHECK(v096_near(arc_terrain_height(t, 100, 21), 2 + 4 + 0.25f, 1e-4f));   /* outside: the nearest edge */
    arc_v3 n = arc_terrain_normal(t, 14, 22), e = arc_v3_norm(arc_v3_make(-0.5f, 1, -0.25f));
    CHECK(v096_near(n.x, e.x, 1e-4f) && v096_near(n.y, e.y, 1e-4f) && v096_near(n.z, e.z, 1e-4f));
    CHECK(arc_terrain_inside(t, 18, 24) && !arc_terrain_inside(t, 18.1f, 24) && !arc_terrain_inside(t, 9.9f, 22));
    /* triangles: count, facing up, and the height function lies on them */
    int nt = arc_terrain_triangles(t, NULL, 0); CHECK(nt == 8 * 4 * 6);
    arc_v3 *tri = (arc_v3 *)malloc(sizeof(arc_v3) * (size_t)nt); CHECK(arc_terrain_triangles(t, tri, nt) == nt);
    int up = 0; for (int k = 0; k < nt; k += 3) up += arc_v3_cross(arc_v3_sub(tri[k + 1], tri[k]), arc_v3_sub(tri[k + 2], tri[k])).y > 0;
    CHECK(up == nt / 3);
    free(tri); arc_terrain_destroy(t);
    CHECK(arc_terrain_height(t, 10, 20) == 0);   /* a destroyed handle answers nothing */
    /* a bumpy grid: every height query matches the triangle it falls in */
    static float b[17 * 17];
    for (int j = 0; j < 17; j++) for (int i = 0; i < 17; i++) b[j * 17 + i] = arc_noise_fbm2((float)i * 0.4f, (float)j * 0.4f, 3, 9u) * 3;
    t = arc_terrain_create(&(arc_terrain_desc){ .heights = b, .samples_x = 17, .samples_z = 17, .size_x = 16, .size_z = 16, .chunk = 8 });
    nt = arc_terrain_triangles(t, NULL, 0); tri = (arc_v3 *)malloc(sizeof(arc_v3) * (size_t)nt); arc_terrain_triangles(t, tri, nt);
    arc_rng r; arc_rng_seed(&r, 96, 1); int ok = 0, tries = 200;
    for (int k = 0; k < tries; k++) {
        float x = arc_rng_rangef(&r, 0.01f, 15.99f), z = arc_rng_rangef(&r, 0.01f, 15.99f), hq = arc_terrain_height(t, x, z);
        int cell = ((int)z * 16 + (int)x) * 6; bool in0, in1;
        float h0 = v096_tri_height(tri + cell, x, z, &in0), h1 = v096_tri_height(tri + cell + 3, x, z, &in1);
        ok += (in0 && v096_near(h0, hq, 1e-4f)) || (in1 && v096_near(h1, hq, 1e-4f));
    }
    CHECK(ok == tries);
    /* raycasts: straight down, slanted, upward (no hit), from beside the terrain */
    float d; arc_v3 p;
    CHECK(arc_terrain_raycast(t, arc_v3_make(5.3f, 20, 7.1f), arc_v3_make(0, -1, 0), 100, &d, &p) && v096_near(p.y, arc_terrain_height(t, 5.3f, 7.1f), 1e-3f) && v096_near(d, 20 - p.y, 1e-3f));
    CHECK(arc_terrain_raycast(t, arc_v3_make(-5, 8, -5), arc_v3_make(1, -0.4f, 0.8f), 100, &d, &p) && v096_near(p.y, arc_terrain_height(t, p.x, p.z), 2e-3f));
    CHECK(!arc_terrain_raycast(t, arc_v3_make(5, 10, 5), arc_v3_make(0.1f, 1, 0), 100, NULL, NULL));
    CHECK(!arc_terrain_raycast(t, arc_v3_make(5.3f, 20, 7.1f), arc_v3_make(0, -1, 0), 5, NULL, NULL));   /* too short */
    free(tri);
    /* sculpting: the centre rises by the amount, nothing past the radius changes; flatten pulls toward a height */
    float c0 = arc_terrain_height(t, 8, 8), far0 = arc_terrain_height(t, 2, 2);
    arc_terrain_sculpt(t, 8, 8, 3, 1.5f);
    CHECK(v096_near(arc_terrain_height(t, 8, 8), c0 + 1.5f, 1e-4f) && arc_terrain_height(t, 2, 2) == far0);
    arc_terrain_flatten(t, 8, 8, 3, 0.25f, 1);
    CHECK(v096_near(arc_terrain_height(t, 8, 8), 0.25f, 1e-4f) && arc_terrain_height(t, 2, 2) == far0);
    float *flat = (float *)calloc(17 * 17, sizeof(float)); arc_terrain_set_heights(t, flat); free(flat);
    CHECK(arc_terrain_height(t, 3.3f, 9.1f) == 0);
    arc_terrain_destroy(t);
    /* bad input */
    CHECK(arc_terrain_create(&(arc_terrain_desc){ .samples_x = 1, .samples_z = 5, .size_x = 1, .size_z = 1 }).id == 0);
    CHECK(arc_terrain_create(NULL).id == 0);
}
static void v096_splat_and_scatter(void) {
    /* texture arrays: matching sizes only; a material keeps the array and its settings */
    arc_texture red = v096_solid(0xFF0000), blue = v096_solid(0x0000FF), green = v096_solid(0x00FF00);
    uint8_t big[16 * 16 * 4] = { 0 }; arc_texture other = arc_texture_create_ex(16, 16, big, ARC_TEX_MESH);
    arc_texture bad[2] = { red, other }, two[2] = { red, blue };
    CHECK(arc_texture_array(bad, 2).id == 0 && arc_texture_array(two, 5).id == 0);
    arc_texture arr = arc_texture_array(two, 2); CHECK(arr.id != 0);
    arc_mesh q = arc_mesh_plane(0xFFFFFF, 1, 0xFFFFFF); arc_material m; memset(&m, 0, sizeof m);
    m.layers = arr; m.layer_scale[0] = 2; m.splat_sharpness = 0.3f; arc_mesh_set_material(q, &m);
    arc_material back; CHECK(arc_mesh_material(q, &back) && back.layers.id == arr.id && back.layer_scale[0] == 2 && back.layer_scale[1] == 1 && v096_near(back.splat_sharpness, 0.3f, 1e-6f));
    m.layers = red; arc_mesh_set_material(q, &m); CHECK(arc_mesh_material(q, &back) && back.layers.id == 0);   /* not an array */
    arc_mesh_destroy(q); arc_texture_destroy(arr); arc_texture_destroy(other);
    /* a flat 20 x 20 terrain, layers red / blue / green (copied in: the originals can go) */
    arc_texture lay[3] = { red, blue, green };
    arc_terrain t = arc_terrain_create(&(arc_terrain_desc){ .samples_x = 41, .samples_z = 41, .size_x = 20, .size_z = 20, .chunk = 10, .splat_res = 64, .layers = { lay[0], lay[1], lay[2] }, .layer_count = 3 });
    arc_texture_destroy(red); arc_texture_destroy(blue); arc_texture_destroy(green);
    CHECK(t.id != 0 && arc_terrain_splat_map(t).id != 0);
    CHECK(v096_near(arc_terrain_layer_weight(t, 10, 10, 0), 1, 1e-4f) && arc_terrain_layer_weight(t, 10, 10, 1) == 0);
    /* paint layer 1 into a disc: full at the centre, none far away, weights still summing to 1 */
    arc_terrain_paint(t, 5, 10, 3, 1, 1);
    float w0 = arc_terrain_layer_weight(t, 5, 10, 0), w1 = arc_terrain_layer_weight(t, 5, 10, 1);
    CHECK(w1 > 0.9f && w0 < 0.1f && v096_near(w0 + w1 + arc_terrain_layer_weight(t, 5, 10, 2), 1, 0.01f));
    CHECK(arc_terrain_layer_weight(t, 15, 10, 1) == 0);
    arc_terrain_paint(t, 5, 10, 3, 7, 1);   /* no such layer: nothing */
    /* scatter: about density x area, the same every build, placed only where the layer is */
    arc_mesh tuft = arc_mesh_cone(0xFFFFFF, 5);
    arc_scatter all = arc_scatter_create(t, &(arc_scatter_desc){ .mesh = tuft, .density = 2, .layer = -1, .seed = 7 });
    int na = arc_scatter_count(all);
    CHECK(na > 700 && na <= 800);   /* 2 a square unit over 400: jittered cells, one each */
    arc_scatter again = arc_scatter_create(t, &(arc_scatter_desc){ .mesh = tuft, .density = 2, .layer = -1, .seed = 7 });
    bool same = arc_scatter_count(again) == na;
    for (int i = 0; same && i < na; i++) same = !memcmp(arc_scatter_matrix(all, i).m, arc_scatter_matrix(again, i).m, sizeof(arc_m4));
    CHECK(same);
    arc_scatter other_seed = arc_scatter_create(t, &(arc_scatter_desc){ .mesh = tuft, .density = 2, .layer = -1, .seed = 8 });
    CHECK(memcmp(arc_scatter_matrix(all, 0).m, arc_scatter_matrix(other_seed, 0).m, sizeof(arc_m4)) != 0);
    arc_scatter onb = arc_scatter_create(t, &(arc_scatter_desc){ .mesh = tuft, .density = 2, .layer = 1, .scale_min = 0.5f, .scale_max = 1.5f, .seed = 7 });
    int nb = arc_scatter_count(onb), outside = 0, badscale = 0;
    for (int i = 0; i < nb; i++) {
        arc_m4 M = arc_scatter_matrix(onb, i); float x = M.m[12], z = M.m[14], s = arc_v3_len(arc_v3_make(M.m[4], M.m[5], M.m[6]));
        outside += arc_terrain_layer_weight(t, x, z, 1) < 0.3f; badscale += s < 0.5f - 1e-4f || s > 1.5f + 1e-4f;
    }
    CHECK(nb >= 8 && nb <= 30 && outside == 0 && badscale == 0);   /* weight above ~0.5 inside half the brush: ~7 square units, ~14 copies */
    /* paint more and rebuild: more of them; copies away from the paint keep their exact places */
    arc_m4 keep = arc_scatter_matrix(all, na - 1);
    arc_terrain_paint(t, 15, 15, 3, 1, 1); arc_scatter_rebuild(onb); arc_scatter_rebuild(all);
    CHECK(arc_scatter_count(onb) >= nb + 8 && arc_scatter_count(all) == na && !memcmp(arc_scatter_matrix(all, na - 1).m, keep.m, sizeof keep));
    /* rules: layer 2 (green) above height 1 after raising a hill; slope limits keep scatter off its sides */
    arc_terrain_sculpt(t, 10, 4, 4, 3);
    arc_splat_rule rule = { .layer = 2, .height_min = 1, .height_max = 100, .blend = 0.2f };
    arc_terrain_splat_rules(t, &rule, 1);
    CHECK(arc_terrain_layer_weight(t, 10, 4, 2) > 0.9f && arc_terrain_layer_weight(t, 10, 16, 0) > 0.9f && arc_terrain_layer_weight(t, 5, 10, 1) == 0);   /* rules start from layer 0 */
    arc_scatter flat = arc_scatter_create(t, &(arc_scatter_desc){ .mesh = tuft, .density = 2, .layer = -1, .slope_max = 15, .seed = 7 });
    int steep = 0; for (int i = 0; i < arc_scatter_count(flat); i++) { arc_m4 M = arc_scatter_matrix(flat, i); steep += arc_terrain_normal(t, M.m[12], M.m[14]).y < cosf(15 * ARC_PI / 180) - 1e-4f; }
    CHECK(steep == 0 && arc_scatter_count(flat) < na - 20);
    /* upright copies stand on the ground; align 1 leans them with it */
    arc_scatter lean = arc_scatter_create(t, &(arc_scatter_desc){ .mesh = tuft, .density = 0.5f, .layer = -1, .slope_max = 89, .align = 1, .fixed_yaw = true, .seed = 2 });
    int leaned = 0, grounded = 0;
    for (int i = 0; i < arc_scatter_count(lean); i++) {
        arc_m4 M = arc_scatter_matrix(lean, i); arc_v3 y = arc_v3_norm(arc_v3_make(M.m[4], M.m[5], M.m[6])), n = arc_terrain_normal(t, M.m[12], M.m[14]);
        leaned += arc_v3_dot(y, n) > 0.9999f; grounded += v096_near(M.m[13], arc_terrain_height(t, M.m[12], M.m[14]), 1e-4f);
    }
    CHECK(leaned == arc_scatter_count(lean) && grounded == arc_scatter_count(lean));
    arc_scatter_destroy(all); arc_scatter_destroy(again); arc_scatter_destroy(other_seed); arc_scatter_destroy(onb); arc_scatter_destroy(flat); arc_scatter_destroy(lean);
    CHECK(arc_scatter_count(all) == 0);
    CHECK(arc_scatter_create(t, &(arc_scatter_desc){ .mesh = tuft, .density = 0 }).id == 0);
    arc_terrain_destroy(t); arc_mesh_destroy(tuft);
}
static void v096_physics(void) {
    /* a ball dropped on the terrain's triangles comes to rest on it */
    static float h[33 * 33];
    for (int j = 0; j < 33; j++) for (int i = 0; i < 33; i++) h[j * 33 + i] = 0.6f * sinf((float)i * 0.3f) + 0.4f * cosf((float)j * 0.25f);
    arc_terrain t = arc_terrain_create(&(arc_terrain_desc){ .heights = h, .samples_x = 33, .samples_z = 33, .size_x = 16, .size_z = 16, .origin = { -8, 0, -8 } });
    int n = arc_terrain_triangles(t, NULL, 0); arc_v3 *v = (arc_v3 *)malloc(sizeof(arc_v3) * (size_t)n); arc_terrain_triangles(t, v, n);
    arc_physics w = arc_physics_create(NULL); arc_physics_add_triangles(w, v, n); free(v);
    int b = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.3f }, .pos = { 0.4f, 4, -1.3f } });
    for (int i = 0; i < 600; i++) arc_physics_step(w, 1.0f / 60);
    arc_v3 p = arc_physics_pos(w, b);
    float under = arc_terrain_height(t, p.x, p.z);
    CHECK(p.y > under && p.y < under + 0.31f + 0.02f);   /* touching the ground (it may lean on a slope) */
    arc_physics_destroy(w); arc_terrain_destroy(t);
}
static void v096_init(void) {
    v096_tgt = arc_target_create(32, 16, false);
    v096_heights(); v096_splat_and_scatter(); v096_physics();
    /* for the picture: flat, red with its right half painted blue */
    arc_texture red = v096_solid(0xFF0000), blue = v096_solid(0x0000FF), lay[2] = { red, blue };
    v096_flat = arc_terrain_create(&(arc_terrain_desc){ .samples_x = 9, .samples_z = 5, .size_x = 8, .size_z = 4, .origin = { 300, 0, -2 }, .splat_res = 32, .layers = { lay[0], lay[1] }, .layer_count = 2 });
    arc_texture_destroy(red); arc_texture_destroy(blue);
    for (int k = 0; k < 9; k++) arc_terrain_paint(v096_flat, 306.5f, -2 + (float)k * 0.5f, 2.2f, 1, 1);
}
static void v096_draw(void) {   /* once, into a target: the splat seen from straight above */
    static bool done; if (done || !v096_tgt.id) return; done = true;
    arc_target_begin(v096_tgt, true, 0xFF000000);
    arc_camera cam = { { 304, 5, 0 }, { 304, 0, 0 }, { 0, 0, -1 }, 0, 4.0f, 0.1f, 20 };
    arc_set_camera(&cam); arc_set_light(arc_v3_make(0, 1, 0), 0xFFFFFF, 0x000000); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_terrain_draw(v096_flat, 0xFFFFFF);
    arc_target_end();
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
}
static const uint8_t *v096_px(int x, int y) { return arc__textures[v096_tgt.id].rgba + ((size_t)y * 32 + x) * 4; }
static void v096_check(void) {
    if (!v096_tgt.id || !arc__textures[v096_tgt.id].rgba) return;
    const uint8_t *l = v096_px(4, 8), *r = v096_px(29, 8);   /* x 300.5 (red ground) and 307.6 (painted blue) */
    printf("ARC note splat self-test: left %d %d %d, right %d %d %d\n", l[0], l[1], l[2], r[0], r[1], r[2]);
    CHECK(l[0] > 200 && l[2] < 30 && r[2] > 200 && r[0] < 30);
}
