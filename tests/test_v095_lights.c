/* ARC 0.9.5 self-test: many lights and light shadows. Included by test_core.c; v095_init() runs in init, v095_draw()
   from draw, v095_check() with the frame-8 checks. Names start with v095_ (one translation unit). */
static arc_texture v095_tgt; static arc_mesh v095_box;
static int v095_n_all, v095_n_sh, v095_n_sh_over;
static void v095_init(void) {
    v095_box = arc_mesh_cube(0xFFFFFF);
    v095_tgt = arc_target_create(32, 16, false);
    CHECK(ARC_MAX_LIGHTS == 256 && ARC_MAX_SHADOW_LIGHTS == 4);
}
static void v095_draw(void) {   /* once, at the first frame drawn (headless runs draw only some ticks) */
    static bool done; if (done) return; done = true;
    if (v095_tgt.id) {
        /* a wall facing the camera, a point light before it and a small box between: its shadow falls to the
           light's left on the wall; the same spot on the right stays lit */
        arc_set_light_shadow_resolution(256);
        arc_light_point_ex(arc_v3_make(200, 0, 1.2f), 0xFFFFFF, 1.5f, 3, ARC_LIGHT_SHADOW);
        arc_target_begin(v095_tgt, true, 0xFF000000);
        arc_camera cam = { { 200, 0, 5 }, { 200, 0, 0 }, { 0, 1, 0 }, 0, 2.0f, 0.1f, 10 };
        arc_set_camera(&cam); arc_set_light(arc_v3_make(0, 0, 1), 0x000000, 0x000000); arc_set_fog(0, 0, 0); arc_set_rim(0, 3);
        arc_draw_mesh(v095_box, arc_m4_trs_y(arc_v3_make(200, 0, -0.05f), 0, arc_v3_make(4, 2, 0.1f)), 0xFFFFFF);
        arc_draw_mesh(v095_box, arc_m4_trs_y(arc_v3_make(199.4f, 0, 0.6f), 0, arc_v3_make(0.4f, 0.4f, 0.1f)), 0xFFFFFF);
        arc_target_end();
        arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
        arc_set_light_shadow_resolution(0);
    }
    {
        /* limits: 256 lights a frame, 4 of them shadowed (far below everything, too small to reach it) */
        for (int i = 0; i < 6; i++) arc_light_point_ex(arc_v3_make((float)i, -1000, 0), 0xFFFFFF, 1, 0.01f, ARC_LIGHT_SHADOW);
        v095_n_sh = arc_light_shadow_count();   /* 1 from the wall above, then 3 of these */
        for (int i = 0; i < 300; i++) arc_light_spot(arc_v3_make((float)i, -1000, 0), arc_v3_make(0, -1, 0), 0xFFFFFF, 1, 0.01f, 0.2f, 0.3f);
        v095_n_all = arc_light_count();
        arc_light_spot_ex(arc_v3_make(0, -1000, 0), arc_v3_make(0, -1, 0), 0xFFFFFF, 1, 0.01f, 0.2f, 0.3f, ARC_LIGHT_SHADOW);
        v095_n_sh_over = arc_light_shadow_count();
    }
}
static const uint8_t *v095_px(int x, int y) { return arc__textures[v095_tgt.id].rgba + ((size_t)y * 32 + x) * 4; }
static void v095_check(void) {
    printf("ARC note light limits: %d shadowed, %d lights, %d shadowed after one more\n", v095_n_sh, v095_n_all, v095_n_sh_over);
    CHECK(v095_n_sh == 4);          /* the 5th and 6th light without shadows */
    CHECK(v095_n_all == 256);       /* the rest of the 306 dropped */
    CHECK(v095_n_sh_over == 4);
    if (!v095_tgt.id || !arc__textures[v095_tgt.id].rgba) return;
    /* 8 pixels a unit, x 198 at pixel 0; the light at x 200. The box (199.2..199.6, 0.55..0.65 out) shades about
       198.25..199.26 on the wall (pixels 2..10) seen from the light; the camera sees the box itself over pixels 9..12.
       Pixel 5 (x 198.69) mirrors pixel 26 (x 201.31) about the light */
    int shade = v095_px(5, 8)[0], lit = v095_px(26, 8)[0], mid = v095_px(16, 8)[0];
    printf("ARC note light shadow self-test: shaded %d, lit %d, under the light %d\n", shade, lit, mid);
    CHECK(mid > 150);               /* lit straight on */
    CHECK(lit > 40);                /* the far right: some light */
    CHECK(shade < 8);               /* the mirror spot on the left: in the box's shadow */
    CHECK(v095_px(4, 7)[0] < 8 && v095_px(27, 7)[0] > 40);
}
