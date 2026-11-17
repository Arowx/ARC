/* ARC 0.10.1 self-test: arc_camera_fit (MechBattleArc's review sheets) and distance-field font files. Included by
   test_core.c; v101_init() runs in init. Names start with v101_ (one translation unit). */
static void v101_fit(void) {
    arc_v3 lo = arc_v3_make(-0.5f, 0, -0.5f), hi = arc_v3_make(0.5f, 2, 0.5f);   /* a 1 x 2 x 1 box */
    const float aspects[3] = { 0.5f, 1.0f, 2.35f }, margin = 0.05f;
    for (int persp = 0; persp < 2; persp++) for (int a = 0; a < 3; a++) {
        arc_camera base = { { 3, 2.5f, 4 }, { 0, 1, 0 }, { 0, 1, 0 }, persp ? 0.8f : 0.0f, persp ? 0 : 5.0f, 0.1f, 100 };
        arc_camera c = arc_camera_fit(base, lo, hi, aspects[a], margin);
        float vw = 100.0f * aspects[a], vh = 100.0f;
        arc_m4 vp = arc_camera_view_proj(&c, vw, vh);
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (int i = 0; i < 8; i++) {
            arc_v3 p = arc_v3_make((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
            arc_v4 q = arc_m4_mul_v4(vp, (arc_v4){ p.x, p.y, p.z, 1 });
            float nx = q.x / q.w, ny = q.y / q.w;
            x0 = arc_minf(x0, nx); x1 = arc_maxf(x1, nx); y0 = arc_minf(y0, ny); y1 = arc_maxf(y1, ny);
        }
        float fill = 1 - 2 * margin, tight = arc_maxf(arc_maxf(x1, -x0), arc_maxf(y1, -y0));
        bool centred = fabsf(x0 + x1) < 2e-3f && fabsf(y0 + y1) < 2e-3f;
        /* the same direction as base */
        arc_v3 d0 = arc_v3_norm(arc_v3_sub(base.target, base.eye)), d1 = arc_v3_norm(arc_v3_sub(c.target, c.eye));
        if (!(fabsf(tight - fill) < 2e-3f && centred && arc_v3_dot(d0, d1) > 0.9999f && (persp ? c.fov_y == base.fov_y : c.fov_y == 0)))
            printf("ARC note fit %s aspect %.2f: x %.4f..%.4f y %.4f..%.4f\n", persp ? "perspective" : "orthographic", aspects[a], x0, x1, y0, y1);
        CHECK(fabsf(tight - fill) < 2e-3f && centred && arc_v3_dot(d0, d1) > 0.9999f);
        CHECK(persp ? c.fov_y == base.fov_y : (c.fov_y == 0 && c.ortho_height > 0));
    }
}
static void v101_fonts(void) {
    static const char *dirs[3] = { "../tests/fonts", "../../tests/fonts", "tests/fonts" };
    int d = 0; while (d < 3 && !arc_file_exists(arc_fmt("%s/dejavu_sdf48.arcf", dirs[d]))) d++;
    if (d == 3) { printf("ARC note v101: tests/fonts not found, font checks skipped\n"); return; }
    arc_font s = arc_font_load(arc_fmt("%s/dejavu_sdf48.arcf", dirs[d])), b = arc_font_load(arc_fmt("%s/dejavu_14.arcf", dirs[d]));
    CHECK(s.id && b.id && arc_font_is_sdf(s) && !arc_font_is_sdf(b) && arc_font_size(s) == 48 && arc_font_size(b) == 14);
    /* the metrics scale with the size asked for */
    float w48 = arc_font_width(s, 1, 0, "Halcyon"), w24 = arc_font_width(s, 0.5f, 0, "Halcyon");
    CHECK(w48 > 100 && fabsf(w24 * 2 - w48) < 0.01f);
    arc_font_destroy(s); arc_font_destroy(b);
}
static void v101_init(void) { v101_fit(); v101_fonts(); }
