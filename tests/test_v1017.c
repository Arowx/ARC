/* ARC 0.10.17 self-test: scroll panels (MOLTEN BREACH). A panel with no limit records exactly what arc_ui_begin does;
   a limited one is as tall as its limit, scrolls by wheel and by drag (a drag presses nothing, a click still does),
   keeps the scroll in range, leaves hidden widgets out of `ui`, and saves a drag under way with the UI state (older
   states, without the block, still read). The game-level behaviour is checked by tests/scroll_panel_check.md. Included
   by test_core.c (after test_v1015.c); v1017_init() runs in init. Names start with v1017_. Uses test_v07_ui.c's v7_ steps. */
uint64_t arc__ui_test_cmd_hash(void);
static float v1017_scroll, v1017_max; static int v1017_hits, v1017_mode;   /* mode 0: arc_ui_begin, 1: begin_scroll */
static void v1017_panel(void) {
    if (v1017_mode == 0) arc_ui_begin("Scroll1017", 10, 10, 200);
    else arc_ui_begin_scroll("Scroll1017", 10, 10, 200, v1017_max, &v1017_scroll);
    for (int i = 1; i <= 20; i++) { char l[16]; snprintf(l, sizeof l, "R%02d", i); if (arc_ui_button(l)) v1017_hits = i; }
    arc_ui_end();
}
static bool v1017_listed(const char *label) {
    float x, y; return arc__ui_find(label, &x, &y);
}
static void v1017_init(void) {
    static uint8_t saved[1 << 16]; arc_writer w0 = { saved, sizeof saved, 0 }; arc__ui_state(&w0);
    float d = arc_dpi_scale() * arc_ui_scale();
    v7_build = v1017_panel;
    /* no limit: the same drawing as arc_ui_begin */
    v1017_mode = 0; v7_step(); uint64_t plain = arc__ui_test_cmd_hash(); float plain_h = arc_ui_content_height();
    v1017_mode = 1; v1017_max = 0; v7_step(); CHECK(arc__ui_test_cmd_hash() == plain && arc_ui_content_height() == plain_h);
    /* limited to 100: that tall, the full height still known, hidden rows not listed */
    v1017_max = 100; v1017_scroll = 0; v7_steps(2);
    float x, y, pw, ph; CHECK(arc_ui_panel_rect("Scroll1017", &x, &y, &pw, &ph) && fabsf(ph - 100 * d) < 0.01f);
    CHECK(fabsf(arc_ui_content_height() - plain_h) < 0.01f && plain_h > 100 * d);
    CHECK(v1017_listed("R01") && !v1017_listed("R20"));
    /* the wheel over it: 40 pixels a step (scaled), up to the end and no further */
    arc__input_mouse(x + 20, y + 60 * d); v7_step();
    arc__input_wheel(-2); v7_step(); CHECK(fabsf(v1017_scroll - 80 * d) < 0.01f);
    arc__input_wheel(-1000); v7_step(); CHECK(fabsf(v1017_scroll - (plain_h - 100 * d)) < 0.01f);
    v7_step(); CHECK(v1017_listed("R20") && !v1017_listed("R01"));
    arc__input_wheel(1000); v7_step(); CHECK(v1017_scroll == 0);
    /* a drag up scrolls and presses nothing */
    v1017_hits = 0;
    arc__input_mouse(x + 20, y + 80 * d); v7_step();
    arc__input_key(ARC_MOUSE_LEFT, true); v7_step();
    arc__input_mouse(x + 20, y + 50 * d); v7_step(); CHECK(fabsf(v1017_scroll - 30 * d) < 0.01f);
    {   /* mid-drag: the state holds it, and a state without the 0.10.17 block still reads */
        static uint8_t st[1 << 16]; arc_writer w = { st, sizeof st, 0 }; arc__ui_state(&w);
        size_t cut = 0; for (size_t i = w.len >= 4 ? w.len - 4 : 0; i > 0 && !cut; i--) { uint32_t m; memcpy(&m, st + i, 4); if (m == 0x37315355u) cut = i; }
        CHECK(cut > 0);
        arc_reader r = { st, cut, 0, false }; CHECK(arc__ui_state_read(&r, false) && !r.fail);
        arc_reader r2 = { st, w.len, 0, false }; CHECK(arc__ui_state_read(&r2, true) && !r2.fail && r2.pos == w.len);
    }
    arc__input_key(ARC_MOUSE_LEFT, false); v7_step(); CHECK(v1017_hits == 0);
    v7_step(); CHECK(v1017_hits == 0);
    /* a click without moving still presses the row under it */
    { float cx, cy; CHECK(arc__ui_find("R03", &cx, &cy)); arc__input_mouse(cx, cy); v7_step();
      arc__input_key(ARC_MOUSE_LEFT, true); v7_step(); arc__input_key(ARC_MOUSE_LEFT, false); v7_step(); CHECK(v1017_hits == 3); }
    /* a NaN scroll comes back to 0 */
    v1017_scroll = NAN; v7_step(); CHECK(v1017_scroll == 0);
    v7_build = NULL; v7_step();   /* put input and UI back as they were */
    arc__input_mouse(0, 0); arc__input_test_step(); arc__input_test_step();
    arc_reader r = { saved, w0.len, 0, false }; CHECK(arc__ui_state_read(&r, true) && !r.fail && r.pos == w0.len);
}
