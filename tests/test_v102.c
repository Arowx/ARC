/* ARC 0.10.2 self-test: split-screen viewports outside a frame (a tick-time area: sizes, the mouse and the safe area
   in the viewport's pixels) and a camera rig's own aspect. The drawing itself is checked on all three renderers by
   test_post.c (L_VPREF / L_VPFULL / L_VPSPLIT) and against full-window renders by tests/split_check.md. Included by
   test_core.c; v102_init() runs in init, v102_tick() in tick. Names start with v102_ (one translation unit). */
static void v102_area(void) {
    int W0 = arc_width(), H0 = arc_height();
    arc_viewport_begin(100, 50, 200, 100);
    CHECK(arc_width() == 200 && arc_height() == 100 && fabsf(arc_aspect() - 2.0f) < 1e-6f);
    CHECK(arc_viewport_current() == -1);   /* no viewport is being drawn */
    arc_rectf sa = arc_safe_area(); CHECK(sa.x == 0 && sa.y == 0 && sa.w == 200 && sa.h == 100);
    arc_viewport_end();
    CHECK(arc_width() == W0 && arc_height() == H0);
    arc_viewport_begin(-50, -40, 100, 100);   /* clipped to the window */
    CHECK(arc_width() == 50 && arc_height() == 60);
    arc_viewport_end();
    arc_viewport_begin(W0 + 10, 0, 100, 100);   /* wholly outside: no area */
    CHECK(arc_width() == W0);
    arc_viewport_end();
    float keep[4]; memcpy(keep, arc__safe_insets, sizeof keep);   /* the window's insets, cut to the viewport */
    arc__safe_insets[0] = 30; arc__safe_insets[1] = 40; arc__safe_insets[2] = 0; arc__safe_insets[3] = 60;
    arc_viewport_begin(0, 0, W0 / 2, H0);
    sa = arc_safe_area(); CHECK(sa.x == 30 && sa.y == 40 && sa.w == (float)(W0 / 2 - 30) && sa.h == (float)(H0 - 100));
    arc_viewport_end();
    arc_viewport_begin(W0 / 2, 20, W0 - W0 / 2, H0 - 20);
    sa = arc_safe_area(); CHECK(sa.x == 0 && sa.y == 20 && sa.w == (float)(W0 - W0 / 2) && sa.h == (float)(H0 - 20 - 20 - 60));
    arc_viewport_end();
    memcpy(arc__safe_insets, keep, sizeof keep);
    CHECK(arc_viewport_count() == 0 && arc_viewport_at(arc_v2_make(10, 10), NULL) == -1);   /* no frame had viewports */
}
static void v102_rig(void) {   /* TOPDOWN bounds use the rig's own aspect: a narrow view may go nearer the edge */
    float x[2];
    for (int k = 0; k < 2; k++) {
        arc_cam_rig r; memset(&r, 0, sizeof r);
        r.kind = ARC_RIG_TOPDOWN; r.target = arc_v3_make(9, 0, 0); r.ortho_half = 4; r.has_bounds = true;
        r.bounds[0] = -10; r.bounds[1] = -10; r.bounds[2] = 10; r.bounds[3] = 10; r.aspect = k ? 0.5f : 2.0f;
        arc_cam_rig_update(&r, 1.0f / 60.0f); x[k] = r.eye_now.x;
    }
    CHECK(fabsf(x[0] - 2.0f) < 1e-4f && fabsf(x[1] - 8.0f) < 1e-4f);
}
static void v102_init(void) { v102_rig(); }
static void v102_tick(int frame) { if (frame == 10) v102_area(); }   /* (init runs inside a frame: there it would be a real viewport) */
