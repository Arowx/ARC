/* ARC 0.10.14 self-test: the juice kit. Popups (pool, stacking, ageing, clear), arc_yaw_to agreeing with
   arc_m4_trs_y's front, and arc_play_var with any pitch spread. Included by test_core.c (after
   test_v1013.c); v1014_init() runs in init. Names start with v1014_. */
static void v1014_popups(void) {
    arc_popups_clear(); CHECK(arc_popup_count() == 0);
    arc_popup_text(arc_v3_make(0, 1, 0), 0xFFFFFF, 2, "+10");
    arc_popup_text(arc_v3_make(0, 1, 0), 0xFFFFFF, 2, "+20");   /* close to the first: stacks above it */
    arc_popup_text_2d(100, 100, 0xFFFFFF, 2, "2D");
    arc_popup_text(arc_v3_make(5, 1, 5), 0xFFFFFF, 0, "");      /* empty text: ignored */
    CHECK(arc_popup_count() == 3);
    arc__popups_tick(0.5f); CHECK(arc_popup_count() == 3);      /* still floating */
    arc__popups_tick(0.5f); CHECK(arc_popup_count() == 0);      /* gone after 0.85 s */
    for (int i = 0; i < 100; i++) arc_popup_text(arc_v3_make((float)i * 3.0f, 0, 0), 0xFFFFFF, 1, "x");
    CHECK(arc_popup_count() == 64);                              /* a full pool reuses the oldest */
    arc_popups_clear(); CHECK(arc_popup_count() == 0);
}
static void v1014_yaw(void) {   /* a model turned by arc_yaw_to(d) looks along d */
    const float d[4][2] = { { 0, 1 }, { 1, 0 }, { -1, -1 }, { 0.3f, -2 } };
    for (int i = 0; i < 4; i++) {
        arc_m4 m = arc_m4_trs_y(arc_v3_make(0, 0, 0), arc_yaw_to(d[i][0], d[i][1]), arc_v3_make(1, 1, 1));
        arc_v3 f = arc_m4_mul_point(m, arc_v3_make(0, 0, 1)); float n = sqrtf(d[i][0] * d[i][0] + d[i][1] * d[i][1]);
        CHECK(fabsf(f.x - d[i][0] / n) < 1e-4f && fabsf(f.z - d[i][1] / n) < 1e-4f);
    }
}
static void v1014_play_var(void) {   /* varied pitch: any pitch_var works (a huge one is clamped above 0); the pitch comes from
                                        ARC's own generator, so replays of games that use it stay exact (the shooter template's checks) */
    arc_sfx blip = { .wave = ARC_WAVE_SQUARE, .freq = 440, .sustain = 0.01f, .decay = 0.01f };
    arc_sound s = arc_sound_make(&blip); CHECK(s.id != 0);
    for (int i = 0; i < 8; i++) { arc_voice v = arc_play_var(s, 0.0f, 0, i < 4 ? 0.0f : 5.0f); arc_voice_stop(v); }
}
static void v1014_init(void) { v1014_popups(); v1014_yaw(); v1014_play_var(); }
