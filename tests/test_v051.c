/* ARC 0.5.1 self-test checks (Babel's requests): included by test_core.c. */
static bool v3eq(arc_v3 a, arc_v3 b, float e) { return fabsf(a.x - b.x) < e && fabsf(a.y - b.y) < e && fabsf(a.z - b.z) < e; }
/* every triangle of a mesh faces away from `centre` */
static bool faces_out(arc_mesh m, arc_v3 centre) {
    const arc__mesh_rec *r = &arc__meshes[m.id];
    for (int t = 0; t + 2 < r->icount; t += 3) {
        const arc_vertex *a = &r->v[r->idx[t]], *b = &r->v[r->idx[t + 1]], *c = &r->v[r->idx[t + 2]];
        arc_v3 A = arc_v3_make(a->px, a->py, a->pz), B = arc_v3_make(b->px, b->py, b->pz), C = arc_v3_make(c->px, c->py, c->pz);
        arc_v3 n = arc_v3_cross(arc_v3_sub(B, A), arc_v3_sub(C, A)), mid = arc_v3_scale(arc_v3_add(A, arc_v3_add(B, C)), 1.0f / 3);
        if (arc_v3_dot(n, arc_v3_sub(mid, centre)) <= 0) return false;
        if (arc_v3_dot(arc_v3_make(a->nx, a->ny, a->nz), n) <= 0) return false;   /* stored normal agrees with the winding */
    }
    return true;
}
static void v051_math_text(void) {
    arc_m4 r = arc_m4_rotate(arc_v3_make(0, 0, 2), ARC_PI / 2);
    CHECK(v3eq(arc_m4_mul_point(r, arc_v3_make(1, 0, 0)), arc_v3_make(0, 1, 0), 1e-5f));
    arc_m4 sh = arc_m4_shear(0.5f, 0, 0, 0, 0, 0);
    CHECK(v3eq(arc_m4_mul_point(sh, arc_v3_make(0, 2, 0)), arc_v3_make(1, 2, 0), 1e-6f));
    arc_quat q = arc_quat_from_to(arc_v3_make(1, 0, 0), arc_v3_make(0, 0, -3));
    CHECK(v3eq(arc_quat_rotate(q, arc_v3_make(2, 0, 0)), arc_v3_make(0, 0, -2), 1e-5f));
    q = arc_quat_from_to(arc_v3_make(0, 1, 0), arc_v3_make(0, -1, 0));
    CHECK(v3eq(arc_quat_rotate(q, arc_v3_make(0, 1, 0)), arc_v3_make(0, -1, 0), 1e-5f));
    arc_m4 t = arc_m4_trs(arc_v3_make(1, 2, 3), arc_quat_axis_angle(arc_v3_make(0, 1, 0), ARC_PI / 2), arc_v3_make(2, 2, 2));
    CHECK(v3eq(arc_m4_mul_point(t, arc_v3_make(1, 0, 0)), arc_v3_make(1, 2, 1), 1e-5f));
    CHECK(v3eq(arc_m4_mul_dir(t, arc_v3_make(1, 0, 0)), arc_v3_make(0, 0, -2), 1e-5f));
    arc_m4 c = arc_m4_chain(3, arc_m4_translate(arc_v3_make(1, 0, 0)), arc_m4_rotate_y(ARC_PI), arc_m4_translate(arc_v3_make(1, 0, 0)));
    CHECK(v3eq(arc_m4_mul_point(c, arc_v3_make(0, 0, 0)), arc_v3_make(0, 0, 0), 1e-5f));
    CHECK(feq(arc_text_height(2, "a\nb"), 16 + 20) && feq(arc_text_fit_scale("abcd", 64, 3), 2));
    /* mesh builder: boxes, lathes and extrusions face out, with or without a mirroring transform */
    arc_mesh_builder *b = arc_mb_begin(); arc_mb_box(b, arc_v3_make(-1, -1, -1), arc_v3_make(1, 1, 1));
    arc_mesh box = arc_mb_finish(b, 0, 0); CHECK(arc_mesh_triangles(box) == 12 && faces_out(box, arc_v3_make(0, 0, 0)));
    arc_v2 prof[] = { { 0.5f, -1 }, { 1, 0 }, { 0.5f, 1 } };
    b = arc_mb_begin(); arc_mb_lathe(b, prof, 3, 16, true); arc_mesh vase = arc_mb_finish(b, 0, 0);
    CHECK(arc_mesh_triangles(vase) == 16 * 4 + 32 && faces_out(vase, arc_v3_make(0, 0, 0)));
    arc_v2 sq[] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } }, sq2[] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
    b = arc_mb_begin(); arc_mb_extrude(b, sq, 4, -1, 1, true); arc_mesh ex1 = arc_mb_finish(b, 0, 0);
    b = arc_mb_begin(); arc_mb_extrude(b, sq2, 4, -1, 1, true); arc_mesh ex2 = arc_mb_finish(b, 0, 0);
    CHECK(faces_out(ex1, arc_v3_make(0, 0, 0)) && faces_out(ex2, arc_v3_make(0, 0, 0)) && arc_mesh_triangles(ex1) == 16);
    b = arc_mb_begin(); arc_mb_transform(b, arc_m4_scale(arc_v3_make(-1, 1, 1))); arc_mb_box(b, arc_v3_make(-1, -1, -1), arc_v3_make(1, 1, 1));
    arc_mesh mir = arc_mb_finish(b, 0, 0); CHECK(faces_out(mir, arc_v3_make(0, 0, 0)));
    b = arc_mb_begin();   /* a tetrahedron given in random winding: OUTWARD fixes it */
    arc_v3 T[4] = { { 1, 1, 1 }, { -1, -1, 1 }, { -1, 1, -1 }, { 1, -1, -1 } };
    arc_mb_tri(b, T[0], T[1], T[2]); arc_mb_tri(b, T[0], T[3], T[1]); arc_mb_tri(b, T[0], T[2], T[3]); arc_mb_tri(b, T[1], T[2], T[3]);
    arc_mesh tet = arc_mb_finish(b, 0, ARC_MB_OUTWARD); CHECK(faces_out(tet, arc_v3_make(0, 0, 0)));
    b = arc_mb_begin(); arc_mb_lathe(b, prof, 3, 16, false); arc_mesh smooth = arc_mb_finish(b, 80, 0);
    { const arc__mesh_rec *r = &arc__meshes[smooth.id]; int shared = 0;   /* the side's vertices at one point now share a normal */
      for (int i = 0; i < r->vcount; i++) for (int j = i + 1; j < r->vcount; j++)
        if (fabsf(r->v[i].px - r->v[j].px) < 1e-5f && fabsf(r->v[i].py - r->v[j].py) < 1e-5f && fabsf(r->v[i].pz - r->v[j].pz) < 1e-5f &&
            fabsf(r->v[i].nx - r->v[j].nx) < 1e-5f && fabsf(r->v[i].ny - r->v[j].ny) < 1e-5f) shared++;
      CHECK(shared > 16); }
    arc_mesh_destroy(box); arc_mesh_destroy(vase); arc_mesh_destroy(ex1); arc_mesh_destroy(ex2); arc_mesh_destroy(mir); arc_mesh_destroy(tet); arc_mesh_destroy(smooth);
}

/* ---- sound buses ---- */
static void v051_buses(void) {
    extern bool arc__audio_test_mode; arc__audio_test_mode = true;
    arc__audio_init(44100); arc_set_master_volume(1);
    static int16_t dc[4096]; for (int i = 0; i < 4096; i++) dc[i] = 16384;
    arc_sound s = arc_sound_from_pcm(dc, 4096, 44100);
    static float out[2 * 64];
    arc_voice v = arc_play(s, 1, 0); arc__audio_mix(out, 64); float full = out[20];
    CHECK(full > 0.1f);
    arc_set_bus_volume(ARC_BUS_SFX, 0.5f); arc__audio_mix(out, 64);
    CHECK(fabsf(out[20] - full * 0.5f) < 1e-3f);                         /* the effects bus at half */
    CHECK(fabsf(arc_bus_volume(ARC_BUS_SFX) - 0.5f) < 1e-6f);
    arc_voice_stop(v);
    arc_voice u = arc_play_bus(s, ARC_BUS_UI, 1, 0); arc__audio_mix(out, 64);
    CHECK(fabsf(out[20] - full) < 1e-3f);                                /* the UI bus is unaffected */
    arc_set_bus_volume(ARC_BUS_UI, 0); arc__audio_mix(out, 64);
    CHECK(out[20] == 0);
    arc_voice_stop(u);
    arc_sound_set_bus(s, ARC_BUS_VOICE); v = arc_play(s, 1, 0);
    arc_set_bus_volume(ARC_BUS_VOICE, 0.25f); arc__audio_mix(out, 64);
    CHECK(fabsf(out[20] - full * 0.25f) < 1e-3f);                        /* a sound's own bus */
    arc_voice_stop(v); arc__audio_mix(out, 8);
    arc_set_bus_volume(ARC_BUS_SFX, 0.3f); CHECK(arc_audio_save_settings());
    arc_set_bus_volume(ARC_BUS_SFX, 1); arc_audio_load_settings();
    CHECK(fabsf(arc_bus_volume(ARC_BUS_SFX) - 0.3f) < 1e-4f);            /* saved and loaded */
    for (int b = 0; b < ARC_BUS_COUNT; b++) arc_set_bus_volume((arc_bus)b, 1);
    arc_set_master_volume(0.8f); arc__audio_test_mode = false;
}

/* ---- UI: disabled widgets and text that doesn't fit ---- */
bool arc__ui_find_text(const char *prefix, char *out, int cap, float *scale, uint32_t *color);
static int buy_hits, ok_hits;
static void v051_ui(int frame) {
    if (frame < 10 || frame > 17) return;
    arc_ui_begin("Shop", 300, 10, 160);
    if (arc_ui_button_ex("Buy", false)) buy_hits++;
    if (arc_ui_button("OK")) ok_hits++;
    arc_ui_enabled(false); bool t = false; arc_ui_toggle("Locked", &t); arc_ui_enabled(true);
    arc_ui_label("Supercalifragilisticexpialidocious and then some more words");
    arc_ui_label("A slightly long label!!");
    arc_ui_end();
    float x, y;
    if (frame == 11) { CHECK(arc__ui_find("Buy", &x, &y)); arc__input_mouse(x, y); arc__input_key(ARC_MOUSE_LEFT, true); }
    if (frame == 12) arc__input_key(ARC_MOUSE_LEFT, false);
    if (frame == 13) { CHECK(arc__ui_find("OK", &x, &y)); arc__input_mouse(x, y); arc__input_key(ARC_MOUSE_LEFT, true); }
    if (frame == 14) arc__input_key(ARC_MOUSE_LEFT, false);
    if (frame == 16) {
        CHECK(buy_hits == 0 && ok_hits == 1);                            /* the disabled one can't be pressed */
        char s[256]; float sc; uint32_t col; arc_ui_theme th = arc_ui_get_theme();
        CHECK(arc__ui_find_text("Buy", s, sizeof s, &sc, &col) && col == th.text_dim);
        CHECK(arc__ui_find_text("OK", s, sizeof s, &sc, &col) && col == th.text);
        CHECK(arc__ui_find_text("Locked", s, sizeof s, &sc, &col) && col == th.text_dim);
        CHECK(arc__ui_find_text("Super", s, sizeof s, &sc, &col));
        CHECK(strstr(s, "...") && strlen(s) < 40);                       /* too long: cut short */
        CHECK(arc_text_width(sc, s) <= 160 - 12 + 0.01f);
        CHECK(arc__ui_find_text("A slightly", s, sizeof s, &sc, &col));
        CHECK(!strstr(s, "...") && sc < th.scale * arc_dpi_scale() && arc_text_width(sc, s) <= 160 - 12 + 0.01f);   /* shrunk to fit */
    }
}

/* ---- billboards: drawn behind the camera (the shot stays as it was); the text cache under pressure ---- */
static void v051_draw(void) {
    arc_v3 behind = arc_v3_make(0, 0, 50);
    arc_draw_billboard((arc_texture){ 0 }, behind, 1, 1, 0xFFFFFF, ARC_BB_UPRIGHT | ARC_BB_BOTTOM);
    arc_draw_billboard_ex((arc_texture){ 0 }, behind, 8, 8, 0xFF8000, 0.5f, 1, ARC_BB_PIXELS | ARC_BB_ADDITIVE);
    for (int i = 0; i < 140; i++) arc_draw_billboard_text(behind, 0.5f, 0xFFFFFF, i & 1 ? 0xC0000000 : 0, 0, arc_fmt("label %d\nline two", i));
    arc_draw_billboard_text(behind, 0.5f, 0xFFFFFF, 0, 0, "");
    arc_gfx_stats st = arc_stats(); (void)st;
}

/* ---- frame-time percentiles (fed directly: headless frame times aren't steady) ---- */
void arc__fps_test_sample(float ms);
static void v051_frame_times(void) {
    for (int i = 0; i < 600; i++) arc__fps_test_sample(i % 60 == 30 ? 50.0f : 10.0f);   /* 10 spikes of 50 ms */
    arc_frame_times t = arc_get_frame_times();
    CHECK(t.samples == 600 && t.p50 == 10.0f && t.p95 == 10.0f && t.p99 == 50.0f && t.max == 50.0f);
    CHECK(fabsf(t.low1_fps - 20.0f) < 1e-3f && fabsf(t.avg - (590 * 10.0f + 10 * 50.0f) / 600) < 1e-3f);
    CHECK(t.max_1s == 50.0f);                                                             /* one spike in the last 60 */
    for (int i = 0; i < 600; i++) arc__fps_test_sample(16.0f);
    t = arc_get_frame_times(); CHECK(t.p99 == 16.0f && t.max == 16.0f);                 /* old spikes age out */
}

/* ---- JPEG writer: an odd-sized gradient written and read back with ARC's own decoder ---- */
static void v051_jpeg(void) {
    enum { W = 37, H = 21 }; static uint8_t px[W * H * 4];
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { uint8_t *p = px + (y * W + x) * 4; p[0] = (uint8_t)(x * 6); p[1] = (uint8_t)(y * 11); p[2] = 128; p[3] = 255; }
    CHECK(arc_image_save_jpeg("arc_test_jpeg.jpg", px, W, H, 95));
    int w = 0, h = 0; uint8_t *back = arc_image_load("arc_test_jpeg.jpg", &w, &h);
    CHECK(back && w == W && h == H);
    if (back) {
        int worst = 0;
        for (int i = 0; i < W * H; i++) for (int c = 0; c < 3; c++) { int d = abs(back[i * 4 + c] - px[i * 4 + c]); if (d > worst) worst = d; }
        CHECK(worst <= 12);   /* lossy, but close on a smooth gradient */
        free(back);
    }
}
