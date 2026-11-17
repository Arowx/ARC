/* ARC 0.7.2 reversed-Z self-test (included once by test_core.c). The software renderer can switch the depth mode
   between passes (arc__depth_mode_set, tests only), so v072d_draw() draws the same scenes into render targets in each
   mode: standard, reversed and reversed with no far plane. v072d_check() (tick 9) reads them:
   - far away: two tilted quads 2000 units off, red drawn first and green 0.01 in front (znear 0.05, zfar 5000):
     standard can't keep them apart (red shows), both reversed modes can (all green);
   - every depth feature (ARC_DEPTH_HIDDEN / ON_TOP / TEST_ONLY, x-ray, depth_pull both ways, line depth push, lines,
     a target's own cleared depth) draws the same picture in all three modes, and the features still do their job;
   - beyond zfar: drawn only with no far plane;
   - the public matrices stay standard in reversed mode; arc_depth_mode / arc_depth_precise. */
enum { V072D_W = 64, V072D_H = 48, V072D_MODES = 3 };
static arc_texture v072d_far[V072D_MODES], v072d_feat[V072D_MODES], v072d_beyond[V072D_MODES];
static arc_mesh v072d_cube, v072d_ball;
static bool v072d_drawn, v072d_vp_same, v072d_mode_ok, v072d_precise;
static const arc_camera V072D_FCAM = { { 0, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 }, 0.9f, 0, 0.05f, 5000 };
static const arc_camera V072D_PCAM = { { 0, 0, 6 }, { 0, 0, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
static void v072d_tilted(float cx, float z, float off, uint32_t rgb) {   /* 600 x 800, turned 35 degrees about y, 20 about x */
    float ay = 0.6109f, ax = 0.3491f;
    arc_v3 u = arc_v3_make(cosf(ay), 0, -sinf(ay)), v = arc_v3_make(sinf(ay) * sinf(ax), cosf(ax), cosf(ay) * sinf(ax)), n = arc_v3_cross(u, v);
    arc_v3 c = arc_v3_add(arc_v3_make(cx, 0, z), arc_v3_scale(n, off)), p[4];
    for (int i = 0; i < 4; i++) p[i] = arc_v3_add(c, arc_v3_add(arc_v3_scale(u, (i == 1 || i == 2) ? 300.0f : -300.0f), arc_v3_scale(v, i >= 2 ? 400.0f : -400.0f)));
    arc_draw_quad3(p[0], p[1], p[2], p[3], n, rgb);
}
static void v072d_features(void) {   /* the post test's depth look, smaller: a red box with things around and behind it */
    arc_set_camera(&V072D_PCAM); arc_set_light(arc_v3_make(0.3f, 0.8f, 0.6f), 0xC0C0C0, 0x404040); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
    arc_draw_box(arc_v3_make(-1, -1, -1), arc_v3_make(1, 1, 1), 0xC03030);
    arc_mesh_style st; arc_v3 s5 = arc_v3_make(0.5f, 0.5f, 0.5f), plate = arc_v3_make(0.7f, 0.7f, 0.02f);
    memset(&st, 0, sizeof st); st.tint = 0x00FF00; st.depth = ARC_DEPTH_ON_TOP;      /* behind the box, shown on top */
    arc_draw_mesh_styled(v072d_cube, arc_m4_trs_y(arc_v3_make(-0.8f, 0.8f, -2), 0, s5), &st);
    memset(&st, 0, sizeof st); st.tint = 0x0000FF; st.depth = ARC_DEPTH_HIDDEN;      /* behind the box: shown where covered */
    arc_draw_mesh_styled(v072d_cube, arc_m4_trs_y(arc_v3_make(0.8f, 0.8f, -2), 0, s5), &st);
    arc_draw_mesh_styled(v072d_cube, arc_m4_trs_y(arc_v3_make(2.3f, 0.6f, 0), 0, s5), &st);   /* in the open: not */
    memset(&st, 0, sizeof st); st.tint = 0xFFFF00; st.depth_pull = 0.002f;           /* in the box's front face: wins */
    arc_draw_mesh_styled(v072d_cube, arc_m4_trs_y(arc_v3_make(-0.5f, -0.5f, 0.99f), 0, plate), &st);
    st.tint = 0xFF00FF; st.depth_pull = -0.002f;                                     /* ...pushed: loses */
    arc_draw_mesh_styled(v072d_cube, arc_m4_trs_y(arc_v3_make(0.5f, -0.5f, 0.99f), 0, plate), &st);
    memset(&st, 0, sizeof st); st.tint = 0x808080; st.depth = ARC_DEPTH_TEST_ONLY;   /* a plate that hides nothing drawn after it */
    arc_draw_mesh_styled(v072d_cube, arc_m4_trs_y(arc_v3_make(2.3f, -0.6f, 1.5f), 0, plate), &st);
    memset(&st, 0, sizeof st); st.tint = 0xFFFFFF; st.order = 1; st.emissive = 1;
    arc_draw_mesh_styled(v072d_cube, arc_m4_trs_y(arc_v3_make(3.067f, -0.8f, 0), 0, s5), &st);
    memset(&st, 0, sizeof st); st.tint = 0x808080; st.xray = 1;                      /* x-ray: the ball glows through the box */
    arc_draw_mesh_styled(v072d_ball, arc_m4_trs_y(arc_v3_make(0, 0, -2.5f), 0, arc_v3_make(0.9f, 0.9f, 0.9f)), &st);
    arc_set_line_depth_push(0.6f);                                                   /* a green line pushed behind the box */
    arc_draw_line(arc_v3_make(-3, 0.3f, 1.3f), arc_v3_make(3, 0.3f, 1.3f), 0x00FF00);
    arc_set_line_depth_push(0);
    arc_draw_line(arc_v3_make(-3, -1.6f, 0), arc_v3_make(3, -1.6f, 0), 0x00FFFF);    /* a plain one below the box */
}
static void v072d_draw(void) {
    if (v072d_drawn) return;
    v072d_drawn = true;
    v072d_cube = arc_mesh_cube(0xFFFFFF); v072d_ball = arc_mesh_sphere(0xFFFFFF, 16);
    for (int m = 0; m < V072D_MODES; m++) {
        v072d_far[m] = arc_target_create(V072D_W, V072D_H, false);
        v072d_feat[m] = arc_target_create(V072D_W, V072D_H, false);
        v072d_beyond[m] = arc_target_create(V072D_W, V072D_H, false);
        arc__depth_mode_set(m);
        arc_target_begin(v072d_far[m], true, 0xFF000000);
        arc_set_camera(&V072D_FCAM); arc_set_light(arc_v3_make(0, 0, 1), 0x404040, 0xC0C0C0); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
        v072d_tilted(0, -2000, 0, 0xFF0000); v072d_tilted(0, -2000, 0.01f, 0x00FF00);
        arc_target_end();
        arc_target_begin(v072d_feat[m], true, 0xFF000000);
        v072d_features();
        arc_target_end();
        arc_target_begin(v072d_beyond[m], true, 0xFF000000);
        arc_set_camera(&V072D_FCAM);
        v072d_tilted(0, -6000, 0, 0x00FF00);   /* past zfar (5000) */
        arc_target_end();
        if (m == ARC_DEPTH_REVERSED) {         /* the game's matrices stay standard */
            arc_m4 a = arc_view_proj(), b = arc_camera_view_proj(&V072D_FCAM, (float)V072D_W, (float)V072D_H);   /* (the last camera: in the target) */
            v072d_vp_same = true; for (int k = 0; k < 16; k++) if (fabsf(a.m[k] - b.m[k]) > 1e-6f * (1 + fabsf(b.m[k]))) v072d_vp_same = false;
            v072d_mode_ok = arc_depth_mode() == ARC_DEPTH_REVERSED; v072d_precise = arc_depth_precise();
        }
        arc__depth_mode_set(ARC_DEPTH_STANDARD);
    }
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
}
static const uint8_t *v072d_px(arc_texture t, int x, int y) { return arc__textures[t.id].rgba + ((size_t)y * V072D_W + x) * 4; }
static const uint8_t *v072d_at(arc_texture t, arc_v3 wp) {   /* the feature target's pixel at a world point */
    arc_v2 sp; if (!arc_camera_to_screen(&V072D_PCAM, V072D_W, V072D_H, wp, &sp)) return v072d_px(t, 0, 0);
    return v072d_px(t, arc_clampi((int)sp.x, 0, V072D_W - 1), arc_clampi((int)sp.y, 0, V072D_H - 1));
}
static int v072d_green_share(arc_texture t) {   /* percent green among the lit pixels away from the quads' rims */
    int g = 0, all = 0;
    for (int y = 1; y < V072D_H - 1; y++) for (int x = 1; x < V072D_W - 1; x++) {
        bool in = true;
        for (int k = 0; k < 9; k++) { const uint8_t *e = v072d_px(t, x + k % 3 - 1, y + k / 3 - 1); if (e[0] < 60 && e[1] < 60) in = false; }
        if (!in) continue;
        const uint8_t *p = v072d_px(t, x, y); all++; if (p[1] > p[0]) g++;
    }
    return all > 50 ? g * 100 / all : -1;
}
static void v072d_check(void) {
    CHECK(v072d_drawn);
    if (!v072d_drawn) return;
    CHECK(arc_depth_mode() == ARC_DEPTH_STANDARD);   /* the switch was put back */
    CHECK(v072d_mode_ok && v072d_precise && v072d_vp_same);
    /* far away: 0.01 apart at 2000 */
    int gs = v072d_green_share(v072d_far[0]), gr = v072d_green_share(v072d_far[1]), gi = v072d_green_share(v072d_far[2]);
    printf("ARC note reversed-Z self-test: green share 0.01 apart at 2000: standard %d%%, reversed %d%%, no far plane %d%%\n", gs, gr, gi);
    CHECK(gs >= 0 && gs < 90); CHECK(gr == 100); CHECK(gi == 100);
    /* the same picture in every mode (a value apart at most, where interpolation lands either side) */
    for (int m = 1; m < V072D_MODES; m++) {
        int worst = 0;
        for (int y = 0; y < V072D_H; y++) for (int x = 0; x < V072D_W; x++) for (int k = 0; k < 3; k++) {
            int d = abs(v072d_px(v072d_feat[0], x, y)[k] - v072d_px(v072d_feat[m], x, y)[k]); if (d > worst) worst = d;
        }
        CHECK(worst <= 1);
    }
    for (int m = 0; m < V072D_MODES; m++) {          /* ...and the features at work in each */
        arc_texture t = v072d_feat[m];
        const uint8_t *on = v072d_at(t, arc_v3_make(-0.5f, 0.5f, 1)), *hid = v072d_at(t, arc_v3_make(0.5f, 0.5f, 1)), *open = v072d_at(t, arc_v3_make(2.3f, 0.6f, 0));
        CHECK(on[1] > 90 && on[0] < 60);               /* ON_TOP: green over the box */
        CHECK(hid[2] > 90 && hid[0] < 60);             /* HIDDEN: blue where the box covers it */
        CHECK(open[0] < 20 && open[1] < 20 && open[2] < 20);   /* ...and not in the open */
        const uint8_t *pull = v072d_at(t, arc_v3_make(-0.5f, -0.5f, 1)), *push = v072d_at(t, arc_v3_make(0.5f, -0.5f, 1));
        CHECK(pull[0] > 90 && pull[1] > 90 && pull[2] < 60);    /* depth_pull: the yellow plate in the face wins */
        CHECK(push[0] > 90 && push[1] < 60 && push[2] < 60);    /* pushed: the box wins */
        const uint8_t *to = v072d_at(t, arc_v3_make(2.3f, -0.6f, 1.5f));
        CHECK(to[0] > 140 && to[1] > 140 && to[2] > 140);       /* TEST_ONLY: the white cube behind shows */
        const uint8_t *xr = v072d_at(t, arc_v3_make(0, 0, 1)), *box = v072d_at(t, arc_v3_make(0, -0.85f, 1));
        CHECK(xr[1] > box[1] + 12 && xr[2] > box[2] + 12);      /* x-ray glows through the box */
        arc_v2 a, b;   /* the pushed line: hidden over the box, drawn beside it; the plain line below */
        CHECK(arc_camera_to_screen(&V072D_PCAM, V072D_W, V072D_H, arc_v3_make(0, 0.3f, 1.3f), &a) && arc_camera_to_screen(&V072D_PCAM, V072D_W, V072D_H, arc_v3_make(2.6f, 0.3f, 1.3f), &b));
        int gl_over = 0, gl_side = 0;
        for (int dy = -1; dy <= 1; dy++) {
            const uint8_t *o = v072d_px(t, (int)a.x, arc_clampi((int)a.y + dy, 0, V072D_H - 1)), *s = v072d_px(t, (int)b.x, arc_clampi((int)b.y + dy, 0, V072D_H - 1));
            if (o[1] > 200 && o[0] < 60) gl_over++;
            if (s[1] > 200 && s[0] < 60 && s[2] < 60) gl_side++;
        }
        CHECK(gl_over == 0 && gl_side > 0);
        arc_v2 c; int cy = 0;
        CHECK(arc_camera_to_screen(&V072D_PCAM, V072D_W, V072D_H, arc_v3_make(-1.8f, -1.6f, 0), &c));
        for (int dy = -1; dy <= 1; dy++) { const uint8_t *q = v072d_px(t, (int)c.x, arc_clampi((int)c.y + dy, 0, V072D_H - 1)); if (q[1] > 200 && q[2] > 200) cy++; }
        CHECK(cy > 0);
    }
    /* beyond zfar: only with no far plane */
    int lit[V072D_MODES];
    for (int m = 0; m < V072D_MODES; m++) { lit[m] = 0; for (int i = 0; i < V072D_W * V072D_H; i++) if (arc__textures[v072d_beyond[m].id].rgba[i * 4 + 1] > 60) lit[m]++; }
    CHECK(lit[0] == 0 && lit[1] == 0); CHECK(lit[2] > 15);   /* (about 4 x 6 pixels that far) */
}
