/* ARC 0.6 self-test checks: included by test_core.c. Materials first; lights, post looks, effects and cascades follow. */
static arc_mesh m6_quad(uint32_t rgb) {   /* 1 x 1 quad facing +z, u along +x, v down */
    arc_vertex v[4] = { { -0.5f, 0.5f, 0, 0, 0, 1, arc_vcol(rgb), 0, 0 }, { -0.5f, -0.5f, 0, 0, 0, 1, arc_vcol(rgb), 0, 1 },
                        { 0.5f, -0.5f, 0, 0, 0, 1, arc_vcol(rgb), 1, 1 }, { 0.5f, 0.5f, 0, 0, 0, 1, arc_vcol(rgb), 1, 0 } };
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    return arc_mesh_create(v, 4, idx, 6);
}
static arc_texture m6_flat(int r, int g, int b) { uint8_t px[16]; for (int i = 0; i < 4; i++) { px[i * 4] = (uint8_t)r; px[i * 4 + 1] = (uint8_t)g; px[i * 4 + 2] = (uint8_t)b; px[i * 4 + 3] = 255; } return arc_texture_create_ex(2, 2, px, ARC_TEX_MESH); }
static arc_mesh q_right, q_plain, q_left, q_glow; static arc_texture tgt60, tgt61; static int v06_nlights;

static void v06_materials(void) {
    q_right = m6_quad(0xFFFFFF); q_plain = m6_quad(0xFFFFFF); q_left = m6_quad(0xFFFFFF); q_glow = m6_quad(0xFFFFFF);
    arc_material m = { 0 }, back;
    CHECK(!arc_mesh_material(q_plain, &back));                      /* a plain mesh has none */
    m.normal = m6_flat(204, 128, 230); m.normal_scale = 1;          /* tilted toward +u (+x) */
    arc_mesh_set_material(q_right, &m);
    CHECK(arc_mesh_material(q_right, &back) && back.normal.id == m.normal.id && feq(back.normal_scale, 1) && !back.pbr);
    m.normal = m6_flat(51, 128, 230); arc_mesh_set_material(q_left, &m);   /* tilted toward -x */
    arc_material g = { 0 }; g.emissive_rgb[0] = 1; g.emissive_rgb[1] = 0.5f; g.emissive_rgb[2] = 0.25f;
    arc_mesh_set_material(q_glow, &g);
    arc_mesh tmp = m6_quad(0xFFFFFF); arc_mesh_set_material(tmp, &g); arc_mesh_set_material(tmp, NULL);
    CHECK(!arc_mesh_material(tmp, &back));                          /* NULL takes it off again */
    arc_mesh_destroy(tmp);
}
static void v06_draw(void) {   /* 32 x 16 target: four 8-pixel columns  [normal +x] [plain] [normal -x] [emissive on black] */
    if (!tgt60.id) tgt60 = arc_target_create(32, 16, false);
    if (!q_plain.id) return;
    arc_target_begin(tgt60, true, 0xFF000000);
    arc_camera cam = { { 0, 0, 5 }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 2.0f, 0.1f, 10 };
    arc_set_camera(&cam); arc_set_light(arc_v3_make(1, 0, 1), 0xFFFFFF, 0x000000); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_m4 s = arc_m4_scale(arc_v3_make(1, 2, 1));
    arc_draw_mesh(q_right, arc_m4_mul(arc_m4_translate(arc_v3_make(-1.5f, 0, 0)), s), 0xFFFFFF);
    arc_draw_mesh(q_plain, arc_m4_mul(arc_m4_translate(arc_v3_make(-0.5f, 0, 0)), s), 0xFFFFFF);
    arc_draw_mesh(q_left, arc_m4_mul(arc_m4_translate(arc_v3_make(0.5f, 0, 0)), s), 0xFFFFFF);
    arc_draw_mesh(q_glow, arc_m4_mul(arc_m4_translate(arc_v3_make(1.5f, 0, 0)), s), 0x000000);
    arc_target_end();
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
}
/* lights: 32 x 16 target over x 98..102; plain quads at x 99.5 (a point light 0.5 above) and 100.5 (a spot 0.5 above,
   pointing at it, cone 0.2 / 0.3 rad); 20 out-of-range lights first so the pass has to choose 16 */
static void v06_draw_lights(void) {
    if (!tgt61.id) tgt61 = arc_target_create(32, 16, false);
    if (!q_plain.id) return;
    for (int i = 0; i < 20; i++) arc_light_point(arc_v3_make(100, 0, -500 - (float)i), 0xFFFFFF, 5, 1);
    arc_light_point(arc_v3_make(99.5f, 0, 0.5f), 0xFFFFFF, 1, 1);
    arc_light_spot(arc_v3_make(100.5f, 0, 0.5f), arc_v3_make(0, 0, -1), 0xFFFFFF, 1, 1, 0.2f, 0.3f);
    v06_nlights = arc_light_count();
    arc_target_begin(tgt61, true, 0xFF000000);
    arc_camera cam = { { 100, 0, 5 }, { 100, 0, 0 }, { 0, 1, 0 }, 0, 2.0f, 0.1f, 10 };
    arc_set_camera(&cam); arc_set_light(arc_v3_make(0, 0, 1), 0x000000, 0x000000); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_draw_mesh(q_plain, arc_m4_trs_y(arc_v3_make(99.5f, 0, 0), 0, arc_v3_make(1, 2, 1)), 0xFFFFFF);
    arc_draw_mesh(q_plain, arc_m4_trs_y(arc_v3_make(100.5f, 0, 0), 0, arc_v3_make(1, 2, 1)), 0xFFFFFF);
    arc_target_end();
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
}
static const uint8_t *t61px(int x, int y) { return arc__textures[tgt61.id].rgba + ((size_t)y * 32 + x) * 4; }
static void v06_check_lights(void) {
    CHECK(v06_nlights == 22);
    CHECK(abs(t61px(11, 7)[0] - 138) <= 6);   /* under the point light: N.L 0.985, falloff (1 - d^2/r^2)^2 = 0.55 */
    CHECK(abs(t61px(8, 7)[0] - 59) <= 6);     /* off to the side: 0.75 x 0.31 */
    CHECK(t61px(3, 7)[0] == 0);               /* no quad there */
    CHECK(abs(t61px(19, 7)[0] - 138) <= 6);   /* in the spot's cone */
    CHECK(t61px(22, 7)[0] <= 2);              /* outside it */
    CHECK(t61px(14, 1)[0] <= 2);              /* out of the point light's range */
}
static const uint8_t *t60px(int x, int y) { return arc__textures[tgt60.id].rgba + ((size_t)y * 32 + x) * 4; }
static void v06_check(void) {
    const uint8_t *r = t60px(4, 8), *p = t60px(12, 8), *l = t60px(20, 8), *e = t60px(28, 8);
    CHECK(abs(p[0] - 180) <= 4 && p[0] == p[1] && p[1] == p[2]);    /* plain: N.L = 0.707 */
    CHECK(r[0] > p[0] + 50 && r[0] >= 245);                          /* bent toward the light: N.L = 0.99 */
    CHECK(l[0] < p[0] - 100 && abs(l[0] - 36) <= 6);                 /* bent away: N.L = 0.14 */
    CHECK(near8(e, 255, 128, 64, 3));                                /* emissive shows on a black surface */
    arc_mesh pm = arc_mesh_load("pyramid_mat.arcm");                 /* mesh2arcm --metallic 1 --roughness 0.25 */
    arc_material back; CHECK(pm.id && arc_mesh_material(pm, &back) && back.pbr && feq(back.metallic, 1) && feq(back.roughness, 0.25f) && !back.normal.id);
    arc_mesh pp = arc_mesh_load("pyramid.arcm"); CHECK(pp.id && !arc_mesh_material(pp, &back));
}

/* effects: 48 x 16 target, six 8-pixel columns:
   [dissolve 1.001: gone] [dissolve 0.001: whole] [hologram on black] [scrolled 2-texel texture] [cube + outline] [plain cube] */
static arc_texture tgt62, tex_rb; static arc_mesh q_fx, cube_fx; static float fx_off;
static void v06_draw_fx(void) {
    if (!tgt62.id) {
        tgt62 = arc_target_create(48, 16, false);
        uint8_t px[8] = { 255, 0, 0, 255, 0, 0, 255, 255 }; tex_rb = arc_texture_create_ex(2, 1, px, ARC_TEX_REPEAT);
        q_fx = m6_quad(0xFFFFFF); arc_mesh_set_texture(q_fx, tex_rb);
        arc_mesh_builder *b = arc_mb_begin(); arc_mb_box(b, arc_v3_make(-0.25f, -0.25f, -0.25f), arc_v3_make(0.25f, 0.25f, 0.25f)); cube_fx = arc_mb_finish(b, 0, 0);
    }
    if (!q_plain.id) return;
    arc_target_begin(tgt62, true, 0xFF000000);
    arc_camera cam = { { 200, 0, 5 }, { 200, 0, 0 }, { 0, 1, 0 }, 0, 2.0f, 0.1f, 10 };
    arc_set_camera(&cam); arc_set_light(arc_v3_make(0, 0, 1), 0x000000, 0xFFFFFF); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_m4 s = arc_m4_scale(arc_v3_make(1, 2, 1));
    arc_mesh_style st = { 0 }; st.dissolve = 1.001f;
    arc_draw_mesh_styled(q_plain, arc_m4_mul(arc_m4_translate(arc_v3_make(197.5f, 0, 0)), s), &st);
    st.dissolve = 0.001f; st.dissolve_width = 0.0001f;
    arc_draw_mesh_styled(q_plain, arc_m4_mul(arc_m4_translate(arc_v3_make(198.5f, 0, 0)), s), &st);
    memset(&st, 0, sizeof st); st.hologram = 1; st.tint = 0xFF8040;
    arc_draw_mesh_styled(q_plain, arc_m4_mul(arc_m4_translate(arc_v3_make(199.5f, 0, 0)), s), &st);
    memset(&st, 0, sizeof st); st.scroll_u = 0.37f; fx_off = (float)fmod(arc_time() * 0.37, 1.0);
    arc_draw_mesh_styled(q_fx, arc_m4_mul(arc_m4_translate(arc_v3_make(200.5f, 0, 0)), s), &st);
    memset(&st, 0, sizeof st); st.outline = 0.25f; st.outline_rgb = 0x00FF00;
    arc_draw_mesh_styled(cube_fx, arc_m4_translate(arc_v3_make(201.5f, 0, 0)), &st);
    arc_draw_mesh(cube_fx, arc_m4_translate(arc_v3_make(202.5f, 0, 0)), 0xFFFFFF);
    arc_target_end();
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
}
static const uint8_t *t62px(int x, int y) { return arc__textures[tgt62.id].rgba + ((size_t)y * 48 + x) * 4; }
static void v06_check_fx(void) {
    CHECK(near8(t62px(4, 8), 0, 0, 0, 0));                         /* burned away */
    CHECK(near8(t62px(12, 8), 255, 255, 255, 1));                  /* whole (white under white ambient) */
    const uint8_t *h = t62px(20, 8);                               /* hologram: dim, see-through, the tint's hue */
    CHECK(h[0] > 20 && h[0] < 200 && h[0] > h[1] && h[1] > h[2]);
    float u = (1.5f + 0.5f) / 8.0f + fx_off; u -= floorf(u);       /* column pixel 1 of the scrolled quad */
    const uint8_t *q = t62px(25, 8); CHECK(u < 0.5f ? (q[0] > 200 && q[2] < 50) : (q[2] > 200 && q[0] < 50));
    const uint8_t *o = t62px(33, 8), *c = t62px(36, 8), *e = t62px(46, 8);
    CHECK(near8(o, 0, 255, 0, 1));                                 /* just outside the cube: outline */
    CHECK(c[0] > 200 && c[1] > 200);                               /* the cube itself still shows */
    CHECK(near8(e, 0, 0, 0, 0) && near8(t62px(41, 8), 0, 0, 0, 0)); /* the plain cube has no outline */
}

/* 0.6.4, Moonbase Raid's request: 5000 quads alternating a texture and plain fills (5000 batches, past the old 4096
   limit) into a 6528 x 3024 target in one frame; the last one must be drawn. Drawn once. */
static arc_texture tgt64, tex64, tex64g; static int v064_state, v064_calls = -1;
static void v064_draw(void) {
    if (v064_state) return;
    v064_state = 1;
    if (!tex64.id) { uint8_t px[16]; for (int i = 0; i < 4; i++) { px[i * 4] = 255; px[i * 4 + 1] = 0; px[i * 4 + 2] = 0; px[i * 4 + 3] = 255; } tex64 = arc_texture_create(2, 2, px, false); }
    tgt64 = arc_target_create(6528, 3024, false);
    arc_target_begin(tgt64, true, 0xFF000000);
    for (int i = 0; i < 4999; i++) {
        float x = (float)(i % 136) * 48, y = (float)(i / 136) * 48;
        if (i & 1) arc_draw_rect(x, y, 48, 48, 0x0000FF);
        else arc_draw_texture_ex(tex64, 0, 0, 2, 2, x, y, 48, 48, 0, 0, 0, ARC_WHITE);
    }
    uint8_t g[16] = { 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255 };
    if (!tex64g.id) tex64g = arc_texture_create(2, 2, g, false);
    arc_draw_texture_ex(tex64g, 0, 0, 2, 2, 6000, 2800, 528, 224, 0, 0, 0, ARC_WHITE);   /* the 5000th, a texture of its own: bottom right */
    arc_target_end();
}
static void v064_tick(void) { if (v064_state == 1 && v064_calls < 0) v064_calls = arc_stats().draw_calls; }   /* the bake's frame */
static void v067_check(void) {   /* 0.6.7 mouse look: headless captures as asked (and `mouse X Y` still moves it) */
    arc_mouse_capture(true); CHECK(arc_mouse_captured());
    arc_mouse_capture(false); CHECK(!arc_mouse_captured());
}
static void v064_check(void) {
    if (v064_state != 1 || !arc__textures[tgt64.id].rgba) return;
    const uint8_t *p = arc__textures[tgt64.id].rgba + ((size_t)3000 * 6528 + 6500) * 4;
    CHECK(p[1] == 255 && p[0] == 0 && p[2] == 0);   /* the last quad is there */
    CHECK(arc_stats().dropped == 0);
    CHECK(v064_calls > 0 && v064_calls < 400);   /* 0.6.5: fills join the texture's batch (it was 5000+ draw calls) */
    v064_state = 2;
}
