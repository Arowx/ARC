/* ARC 0.7 Phase C self-test checks (UI and UX): included by test_core.c.
   v07_ui() runs in init: it drives the UI through pretend ticks (arc__input_test_step + the UI tick hooks), then puts
   the UI and the input back as they were. v07_draw() renders rich text into a target; v07_check() reads it (tick 9). */
void arc__input_test_step(void);
void arc__input_mouse(float x, float y);
size_t arc__ui_state(arc_writer *w); bool arc__ui_state_read(arc_reader *r, bool commit);

static void (*v7_build)(void);
static void v7_step(void) { arc__input_test_step(); arc__ui_tick_begin(); if (v7_build) v7_build(); arc__ui_tick_end(); }
static void v7_steps(int n) { for (int i = 0; i < n; i++) v7_step(); }
static void v7_tap(arc_key k) { arc__input_key(k, true); v7_step(); arc__input_key(k, false); v7_step(); }
static bool v7_focus(const char *label) { return !strcmp(arc_ui_focused(), label); }

/* navigation: a 2-column grid, a slider, a button */
static int v7_e_hits, v7_a_hits; static float v7_slider; static bool v7_back;
static void v7_nav_panel(void) {
    arc_ui_begin("Nav", 10, 10, 200);
    arc_ui_columns(2);
    if (arc_ui_button("A")) v7_a_hits++;
    arc_ui_button("B"); arc_ui_button("C"); arc_ui_button("D");
    arc_ui_columns(1);
    arc_ui_slider("S", &v7_slider, 0, 1);
    if (arc_ui_button("E")) v7_e_hits++;
    arc_ui_end();
    if (arc_ui_back_pressed()) v7_back = true;
}
static void v7_nav(void) {
    v7_build = v7_nav_panel;
    v7_step(); CHECK(v7_focus(""));                       /* off: no ring until a key is used (0.6 behaviour) */
    arc_ui_set_nav(true);
    v7_step(); CHECK(v7_focus("A"));                      /* on: the ring shows at once */
    v7_tap(ARC_KEY_RIGHT); CHECK(v7_focus("B"));          /* by layout: right, down, left */
    v7_tap(ARC_KEY_DOWN); CHECK(v7_focus("D"));
    v7_tap(ARC_KEY_LEFT); CHECK(v7_focus("C"));
    v7_tap(ARC_KEY_DOWN); CHECK(v7_focus("S"));
    v7_tap(ARC_KEY_RIGHT); CHECK(v7_focus("S") && fabsf(v7_slider - 0.05f) < 1e-4f);   /* the slider keeps LEFT/RIGHT */
    v7_tap(ARC_KEY_DOWN); CHECK(v7_focus("E"));
    v7_tap(ARC_KEY_DOWN); CHECK(v7_focus("A"));           /* wraps */
    v7_tap(ARC_KEY_UP); CHECK(v7_focus("E"));
    CHECK(v7_e_hits == 0);
    v7_tap(ARC_KEY_ENTER); CHECK(v7_e_hits == 1 && v7_a_hits == 0);
    CHECK(!v7_back); v7_tap(ARC_KEY_ESCAPE); CHECK(v7_back);
    arc__input_pad(true);                                 /* gamepad: stick edges, held d-pad repeats, A presses */
    arc__input_axis(ARC_AXIS_LY, 1); v7_steps(3); CHECK(v7_focus("A"));   /* one move per push, not per tick */
    arc__input_axis(ARC_AXIS_LY, 0); v7_step();
    arc__input_key(ARC_PAD_DOWN, true); v7_steps(28); arc__input_key(ARC_PAD_DOWN, false); v7_step();
    CHECK(v7_focus("S"));                                 /* A -> C at once, -> S after 0.4 s held */
    v7_tap(ARC_PAD_DOWN); v7_tap(ARC_PAD_A); CHECK(v7_e_hits == 2);
    v7_back = false; v7_tap(ARC_PAD_B); CHECK(v7_back);
    arc__input_pad(false);
    arc__input_mouse(300, 170); v7_step(); CHECK(v7_focus(""));   /* the mouse hides the ring */
    arc_ui_set_nav(false);                                /* off again: the 0.6 keys (no moving across columns) */
    v7_tap(ARC_KEY_DOWN); const char *f0 = arc_ui_focused(); char was[64]; snprintf(was, sizeof was, "%s", f0);
    CHECK(was[0] != 0);
    v7_tap(ARC_KEY_RIGHT); CHECK(v7_focus(was));
    arc__input_mouse(0, 0); v7_step();
}

/* scale: widget positions and text scale grow by the factor */
static void v7_scale_panel(void) { arc_ui_begin("Sc", 0, 0, 100); arc_ui_button("Q"); arc_ui_end(); }
static void v7_scale(void) {
    v7_build = v7_scale_panel;
    float x1, y1, x2, y2, s1 = 0, s2 = 0; uint32_t c; char t[32];
    v7_steps(2); CHECK(arc__ui_find("Q", &x1, &y1) && arc__ui_find_text("Q", t, sizeof t, &s1, &c));
    arc_ui_set_scale(2); CHECK(feq(arc_ui_scale(), 2));
    v7_steps(2); CHECK(arc__ui_find("Q", &x2, &y2) && arc__ui_find_text("Q", t, sizeof t, &s2, &c));
    CHECK(feq(x2, 2 * x1) && feq(y2, 2 * y1) && feq(s2, 2 * s1) && feq(s1, 2));
    arc_ui_set_scale(1); v7_steps(2); CHECK(arc__ui_find("Q", &x2, &y2) && feq(x2, x1) && feq(y2, y1));
}

/* rich text in widgets: drawn with markup, listed without, cut as plain text when it can't fit */
static void v7_rich_panel(void) { arc_ui_begin("Rich", 0, 0, 160); arc_ui_label("[c=FF0000]Red[/c] [key=F]"); arc_ui_label("[c=00FF00]a very long label that cannot fit[/c]"); arc_ui_end(); }
static void v7_rich_ui(void) {
    v7_build = v7_rich_panel; float x, y, sc; uint32_t c; char t[128];
    v7_steps(2); CHECK(arc__ui_find_text("[c=FF0", t, sizeof t, &sc, &c) && strstr(t, "..."));   /* off: plain text, too long, cut */
    arc_ui_set_rich(true); v7_steps(2);
    CHECK(arc__ui_find_text("[c=FF0000]Red", t, sizeof t, &sc, &c) && feq(sc, 2));
    CHECK(arc__ui_find_text("a very", t, sizeof t, &sc, &c) && strstr(t, "...") && !strchr(t, '['));
    arc_ui_set_rich(false); (void)x; (void)y;
}

/* animation: a panel opens scaled and see-through, then fades out after it stops being drawn */
static bool v7_show = true;
static void v7_anim_panel(void) { if (v7_show) { arc_ui_begin("Anim", 0, 0, 120); arc_ui_button("Zap"); arc_ui_end(); } }
static void v7_anim(void) {
    arc_ui_theme th = arc_ui_get_theme(); CHECK(th.anim == 0);
    th.anim = 0.1f; arc_ui_set_theme(&th);
    v7_build = v7_anim_panel; float sc = 0; uint32_t c = 0; char t[32];
    v7_step(); CHECK(arc__ui_find_text("Zap", t, sizeof t, &sc, &c) && sc < 2 && sc > 1.5f && (c >> 24) < 0xFF);
    v7_steps(8); CHECK(arc__ui_find_text("Zap", t, sizeof t, &sc, &c) && feq(sc, 2) && (c >> 24) == 0xFF);
    v7_show = false;
    v7_step(); CHECK(arc__ui_find_text("Zap", t, sizeof t, &sc, &c) && (c >> 24) < 0xFF && sc < 2);   /* a ghost, fading */
    v7_steps(6); CHECK(!arc__ui_find_text("Zap", t, sizeof t, &sc, &c));
    arc_ui_set_theme(NULL); CHECK(arc_ui_get_theme().anim == 0);
    v7_show = true; v7_step(); CHECK(arc__ui_find_text("Zap", t, sizeof t, &sc, &c) && feq(sc, 2) && (c >> 24) == 0xFF);   /* off: no animation */
}

static void v7_easing(void) {
    for (int k = 0; k < ARC_EASE_COUNT; k++) {
        CHECK(fabsf(arc_ease((arc_ease_kind)k, 0)) < 1e-5f && fabsf(arc_ease((arc_ease_kind)k, 1) - 1) < 1e-5f);
        CHECK(fabsf(arc_ease((arc_ease_kind)k, -3)) < 1e-5f && fabsf(arc_ease((arc_ease_kind)k, 7) - 1) < 1e-5f);   /* clamped */
    }
    CHECK(feq(arc_ease(ARC_EASE_LINEAR, 0.3f), 0.3f) && feq(arc_ease_in_quad(0.5f), 0.25f) && feq(arc_ease_out_quad(0.5f), 0.75f));
    CHECK(feq(arc_ease_inout_quad(0.5f), 0.5f) && feq(arc_ease_inout_cubic(0.5f), 0.5f) && feq(arc_ease_in_cubic(0.5f), 0.125f) && feq(arc_ease_out_cubic(0.5f), 0.875f));
    CHECK(arc_ease_in_back(0.2f) < 0 && arc_ease_out_back(0.8f) > 1 && arc_ease_out_elastic(0.2f) > 1);   /* overshoot */
    CHECK(feq(arc_ease_inout_sine(0.5f), 0.5f) && feq(arc_ease_inout_back(0.5f), 0.5f) && arc_ease_out_bounce(0.5f) > 0.7f);
    CHECK(feq(arc_tween(10, 20, 0.5f, ARC_EASE_IN_QUAD), 12.5f));
    float prev = 0; bool mono = true; for (int i = 1; i <= 20; i++) { float v = arc_ease_out_cubic((float)i / 20.0f); if (v < prev) mono = false; prev = v; }
    CHECK(mono);
}

static void v7_captions_transitions(void) {
    v7_build = NULL; float sc; uint32_t c; char t[64];
    CHECK(!strcmp(arc_caption_current(), ""));
    arc_caption("Hello", 0.05f, 0); arc_caption("World [c=FF0000]!", 1, 0x00FF00);
    v7_steps(2); CHECK(!strcmp(arc_caption_current(), "Hello") && arc__ui_find_text("Hello", t, sizeof t, &sc, &c) && c == 0xFFFFFFFF);
    v7_step(); CHECK(!strncmp(arc_caption_current(), "World", 5) && arc__ui_find_text("World", t, sizeof t, &sc, &c) && c == 0xFF00FF00);
    arc_caption_clear(); v7_step(); CHECK(!strcmp(arc_caption_current(), "") && !arc__ui_find_text("World", t, sizeof t, &sc, &c));
    /* fade over 0.09 s (5.4 ticks): covered on exactly one tick (the 6th), then uncovers */
    arc_transition(ARC_TRANSITION_FADE, 0.09f, 0x000000); CHECK(arc_transition_active() && arc_transition_amount() == 0);
    int covered_at = -1, covered_n = 0; float last = 0; bool rising = true;
    for (int i = 1; i <= 14; i++) {
        v7_step();
        if (arc_transition_covered()) { covered_n++; covered_at = i; CHECK(feq(arc_transition_amount(), 1)); }
        if (covered_at < 0) { if (arc_transition_amount() < last) rising = false; last = arc_transition_amount(); }
        if (i == 9) CHECK(arc_transition_amount() > 0 && arc_transition_amount() < 1);
    }
    CHECK(covered_n == 1 && covered_at == 6 && rising && !arc_transition_active());
    arc_transition(ARC_TRANSITION_WIPE_LEFT, 1, 0); v7_step(); CHECK(arc_transition_active());
    arc_transition(0, 0, 0); CHECK(!arc_transition_active());
}

static void v7_colour(void) {
    CHECK(arc_palette_cb(0) == 0xE69F00 && arc_palette_cb(8) == 0xE69F00 && arc_palette_cb(-1) == 0x000000 && arc_palette_cb(2) == 0x009E73);
    CHECK(arc_cvd_simulate(0x123456, ARC_CVD_NONE) == 0x123456 && arc_cvd_daltonize(0x80FF8040, ARC_CVD_NONE) == 0x80FF8040);
    for (int m = ARC_CVD_PROTAN; m <= ARC_CVD_TRITAN; m++) {   /* greys look the same to everyone */
        uint32_t g = arc_cvd_simulate(0x808080, m); CHECK(abs((int)((g >> 16) & 255) - 128) <= 3 && abs((int)(g & 255) - 128) <= 3);
    }
    #define V7D(a, b) (sqrtf((float)(((int)((a) >> 16 & 255) - (int)((b) >> 16 & 255)) * ((int)((a) >> 16 & 255) - (int)((b) >> 16 & 255)) + ((int)((a) >> 8 & 255) - (int)((b) >> 8 & 255)) * ((int)((a) >> 8 & 255) - (int)((b) >> 8 & 255)) + ((int)((a) & 255) - (int)((b) & 255)) * ((int)((a) & 255) - (int)((b) & 255)))))
    uint32_t red = 0xD03020, green = 0x30A030;
    float seen = V7D(arc_cvd_simulate(red, ARC_CVD_DEUTAN), arc_cvd_simulate(green, ARC_CVD_DEUTAN));
    float fixed = V7D(arc_cvd_simulate(arc_cvd_daltonize(red, ARC_CVD_DEUTAN), ARC_CVD_DEUTAN), arc_cvd_simulate(arc_cvd_daltonize(green, ARC_CVD_DEUTAN), ARC_CVD_DEUTAN));
    CHECK(seen < 80 && fixed > seen * 1.5f);              /* red and green: close for a deuteranope, apart after the remap */
    uint32_t blue = 0x00A0FF, teal = 0x40D040;           /* sky blue and green: close for a tritanope */
    float ts = V7D(arc_cvd_simulate(blue, ARC_CVD_TRITAN), arc_cvd_simulate(teal, ARC_CVD_TRITAN));
    float tf = V7D(arc_cvd_simulate(arc_cvd_daltonize(blue, ARC_CVD_TRITAN), ARC_CVD_TRITAN), arc_cvd_simulate(arc_cvd_daltonize(teal, ARC_CVD_TRITAN), ARC_CVD_TRITAN));
    CHECK(ts < 60 && tf > ts * 2);
    CHECK(arc_cvd(0x7F112233) == 0x7F112233);
    arc_set_cvd(ARC_CVD_PROTAN); CHECK(arc_get_cvd() == ARC_CVD_PROTAN && arc_cvd(0x7FD03020) == arc_cvd_daltonize(0x7FD03020, ARC_CVD_PROTAN) && (arc_cvd(0x7FD03020) >> 24) == 0x7F);
    arc_set_cvd(99); CHECK(arc_get_cvd() == ARC_CVD_NONE);
    arc_texture lut = arc_cvd_lut(ARC_CVD_DEUTAN); int w = 0, h = 0; arc_texture_size(lut, &w, &h); CHECK(lut.id && w == 256 && h == 16);
    arc_texture_destroy(lut);
    #undef V7D
}

static void v7_tr(void) {
    const char *en = "\xEF\xBB\xBF# English\nhello = Hello\nbye=Goodbye  \r\nmulti = one\\ntwo\nlives = %d lives\n\nbad line\n";
    const char *fr = "hello = Bonjour\nlives = %d vies\ncafe = caf\xC3\xA9\n";
    CHECK(!strcmp(arc_tr("hello"), "hello"));             /* nothing loaded: the key */
    CHECK(arc_tr_load_memory("en", en, strlen(en)) && arc_tr_load_memory("fr", fr, strlen(fr)));
    CHECK(!strcmp(arc_tr("hello"), "Hello") && !strcmp(arc_tr("bye"), "Goodbye") && !strcmp(arc_tr("multi"), "one\ntwo"));   /* fallback = first loaded */
    arc_tr_set_language("fr");
    CHECK(!strcmp(arc_tr_language(), "fr") && !strcmp(arc_tr("hello"), "Bonjour") && !strcmp(arc_tr("cafe"), "caf\xC3\xA9"));
    CHECK(!strcmp(arc_tr("bye"), "Goodbye") && !strcmp(arc_tr("nope"), "nope"));   /* active -> fallback -> key */
    CHECK(!strcmp(arc_trf("lives", 3), "3 vies"));
    const char *names[4]; CHECK(arc_tr_languages(names, 4) == 2 && !strcmp(names[1], "fr") && arc_tr_languages(NULL, 0) == 2);
    CHECK(arc_tr_has("en", "bye") && !arc_tr_has("fr", "bye") && !arc_tr_has("de", "hello"));
    const char *more = "bye = Au revoir\nhello = Salut\n";
    CHECK(arc_file_write("arc_test_tr_fr.txt", more, strlen(more)) && arc_tr_load("fr", "arc_test_tr_fr.txt"));   /* adds; later keys win */
    CHECK(!strcmp(arc_tr("bye"), "Au revoir") && !strcmp(arc_tr("hello"), "Salut") && !strcmp(arc_tr("lives"), "%d vies"));
    arc_tr_set_language("de"); arc_tr_set_fallback("fr");
    CHECK(!strcmp(arc_tr("hello"), "Salut") && !strcmp(arc_tr("multi"), "multi"));
    CHECK(!arc_tr_load("de", "no_such_file.txt"));
    arc_tr_clear(); CHECK(!strcmp(arc_tr("hello"), "hello") && arc_tr_languages(NULL, 0) == 0);
}

static arc_texture v7_icon_tex;
static void v7_rich_measure(void) {
    arc_font none = { 0 };
    CHECK(feq(arc_rich_width(none, 1, "ab[c=FF0000]cd[/c]"), arc_text_width(1, "abcd")));
    CHECK(feq(arc_rich_width(none, 2, "plain text\nline two!"), arc_text_width(2, "plain text\nline two!")));
    CHECK(feq(arc_rich_height(none, 1, "a\nb"), arc_text_height(1, "a\nb")));
    CHECK(feq(arc_rich_width(none, 1, "[[x]"), arc_text_width(1, "[x]")) && feq(arc_rich_width(none, 1, "[nope]"), arc_text_width(1, "[nope]")));
    CHECK(feq(arc_rich_width(none, 1, "[key=F]"), 12));    /* cap max(8, 6 + 4) + a pixel each side */
    CHECK(feq(arc_rich_width(none, 1, "[icon=missing]"), arc_text_width(1, "[icon=missing]")));
    uint8_t px[2 * 2 * 4]; for (int i = 0; i < 4; i++) { px[i * 4] = 0; px[i * 4 + 1] = 255; px[i * 4 + 2] = 0; px[i * 4 + 3] = 255; }
    v7_icon_tex = arc_texture_create(2, 2, px, false); arc_rich_icon("gem", v7_icon_tex, 0, 0, 0, 0);
    CHECK(feq(arc_rich_width(none, 2, "a[icon=gem]"), 16 + 16));
    char t[64]; CHECK(arc_rich_strip("[c=FF0000]Hit[/c] [key=F] [icon=gem][[", t, sizeof t) == 7 && !strcmp(t, "Hit F ["));
}

static void v07_ui(void) {
    static uint8_t saved[1 << 16]; arc_writer w = { saved, sizeof saved, 0 }; arc__ui_state(&w);
    CHECK(w.len <= sizeof saved);
    v7_easing(); v7_colour(); v7_tr(); v7_rich_measure();
    v7_nav(); v7_scale(); v7_rich_ui(); v7_anim(); v7_captions_transitions();
    v7_build = NULL; v7_step();                          /* put input and UI back as they were */
    arc__input_test_step(); arc__input_test_step();
    arc_reader r = { saved, w.len, 0, false }; CHECK(arc__ui_state_read(&r, true) && !r.fail && r.pos == w.len);
}

/* rich text drawn: 64 x 16 target. Row 0: red I, white I, keycap F, green gem icon. Row 8: plain text keeps its
   brackets; ARC_TEXT_RIGHT | ARC_TEXT_RICH puts a green I at the right edge */
static arc_texture tgt70ui;
static void v07_draw(void) {
    if (!tgt70ui.id) tgt70ui = arc_target_create(64, 16, false);
    arc_font none = { 0 };
    arc_target_begin(tgt70ui, true, 0xFF000000);
    arc_rich_draw(none, 0, 0, 1, 0xFFFFFF, 0, "[c=FF0000]I[/c]I[key=F][icon=gem]");
    arc_draw_text_ex(0, 8, 1, 0xFFFFFF, 0, "[c=");
    arc_draw_text_ex(64, 8, 1, 0xFFFFFF, ARC_TEXT_RIGHT | ARC_TEXT_RICH, "[c=00FF00]I[/c]");
    arc_target_end();
}
static const uint8_t *t70px(int x, int y) { return arc__textures[tgt70ui.id].rgba + ((size_t)y * 64 + x) * 4; }
static void v07_check(void) {
    CHECK(tfont.id && feq(arc_rich_width(tfont, 1, "A[c=FF0000]A"), arc_font_width(tfont, 1, 0, "AA")));   /* loaded fonts too */
    CHECK(near8(t70px(3, 0), 255, 0, 0, 2));             /* the I in red */
    CHECK(near8(t70px(11, 0), 255, 255, 255, 2));        /* back to white */
    CHECK(near8(t70px(17, 3), 0xD8, 0xDC, 0xE4, 2));     /* keycap body */
    CHECK(near8(t70px(18, 7), 0x80, 0x88, 0x90, 2));     /* its darker lower edge */
    CHECK(near8(t70px(28, 3), 0, 255, 0, 2) && near8(t70px(35, 7), 0, 255, 0, 2));   /* the icon, 8 x 8 at x 28 */
    CHECK(near8(t70px(36, 3), 0, 0, 0, 2));              /* nothing after it */
    CHECK(near8(t70px(3, 8), 255, 255, 255, 2));         /* plain: '[' drawn in white, not parsed */
    CHECK(near8(t70px(59, 8), 0, 255, 0, 2) && near8(t70px(51, 8), 0, 0, 0, 2));   /* right-aligned rich I */
}
