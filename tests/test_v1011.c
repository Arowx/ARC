/* ARC 0.10.11 self-test (Vector Trader's requests): multi-touch fingers through the platform hook (ids, pressed,
   released, a tap shorter than a tick, ids not re-pressed in the tick a finger lifts), a UI button pressed by a second
   finger, version 3 replays read as version 4, the line width / glow / cap state. Included by test_core.c after
   test_v07_ui.c (it uses v7_step); v1011_init() runs in init. Names start with v1011_. */
static int v1011_hits;
static void v1011_panel(void) { arc_ui_begin("Touch", 400, 300, 200); if (arc_ui_button("Fire")) v1011_hits++; arc_ui_end(); }
static void v1011_touch(void) {
    v7_build = NULL;
    /* one finger, then a second */
    arc__input_touch(5001, 0, 100, 200); v7_step();
    CHECK(arc_touch_count() == 1 && arc_touch_get(0)->id == 0 && arc_touch_get(0)->pressed && arc_touch_mode());
    arc__input_touch(5002, 0, 900, 500); arc__input_touch(5001, 1, 120, 210); v7_step();
    const arc_touch *a = arc_touch_find(0), *b = arc_touch_find(1);
    CHECK(arc_touch_count() == 2 && a && b && !a->pressed && b->pressed && a->x == 120 && a->start_x == 100 && b->x == 900);
    CHECK(arc_touch_get(2) == NULL && arc_touch_find(7) == NULL);
    /* a tap that starts and ends between two ticks is seen for one tick, then released */
    arc__input_touch(5003, 0, 50, 60); arc__input_touch(5003, 2, 50, 60); v7_step();
    const arc_touch *c = arc_touch_find(2);
    CHECK(arc_touch_count() == 3 && c && c->pressed && !c->released);
    v7_step(); c = arc_touch_find(2);
    CHECK(c && c->released && arc_touch_count() == 3);
    v7_step(); CHECK(arc_touch_find(2) == NULL && arc_touch_count() == 2);
    /* finger 0 lifts and a new finger lands in the same tick: it gets another id (0 is listed as released) */
    arc__input_touch(5001, 2, 120, 210); arc__input_touch(5004, 0, 300, 300); v7_step();
    const arc_touch *gone = arc_touch_find(0); int newid = -1;
    for (int i = 0; i < arc_touch_count(); i++) if (arc_touch_get(i)->pressed) newid = arc_touch_get(i)->id;
    CHECK(gone && gone->released && newid > 1);
    v7_step(); CHECK(arc_touch_find(0) == NULL);
    /* a UI button pressed by a finger other than the first: the first stays down elsewhere (steering) */
    v7_build = v1011_panel; v7_step();
    float bx = 0, by = 0; CHECK(arc__ui_find("Fire", &bx, &by));
    int before = v1011_hits;
    arc__input_touch(5005, 0, bx, by); v7_step(); arc__input_touch(5005, 2, bx, by); v7_step(); v7_step();
    CHECK(v1011_hits == before + 1);
    /* a finger that slides onto the button and lifts there does not press it */
    arc__input_touch(5006, 0, bx - 300, by); v7_step(); arc__input_touch(5006, 1, bx, by); v7_step(); arc__input_touch(5006, 2, bx, by); v7_step(); v7_step();
    CHECK(v1011_hits == before + 1);
    /* lift everything and put the input back */
    arc__input_touch(5002, 2, 900, 500); arc__input_touch(5004, 2, 300, 300);
    v7_build = NULL; v7_step(); v7_step(); v7_step();
    CHECK(arc_touch_count() == 0);
    arc__input_touch_mode(false); v7_step(); v7_step();
}
static void v1011_replay_v3(void) {   /* a version 3 replay (no touches) widens to the current snap, keys kept */
    size_t old = offsetof(arc__input_snap, tcount);
    uint8_t buf[20 + 4 + sizeof(arc__input_snap) + 8]; memset(buf, 0, sizeof buf);
    memcpy(buf, "ARCR", 4); uint32_t v = 3, hz = 60, t = 5, end = 0xFFFFFFFFu, total = 9; uint64_t seed = 7;
    memcpy(buf + 4, &v, 4); memcpy(buf + 8, &seed, 8); memcpy(buf + 16, &hz, 4);
    arc__input_snap sn; memset(&sn, 0, sizeof sn); sn.keys[ARC_KEY_A / 8] = (uint8_t)(1u << (ARC_KEY_A % 8)); sn.mx = 33;
    memcpy(buf + 20, &t, 4); memcpy(buf + 24, &sn, old); memcpy(buf + 24 + old, &end, 4); memcpy(buf + 28 + old, &total, 4);
    size_t n = 0; uint8_t *w = arc__replay_widen(buf, 32 + old, &n);
    arc__input_snap got; memcpy(&got, w + 24, sizeof got); uint32_t t2; memcpy(&t2, w + 20, 4); uint32_t e2; memcpy(&e2, w + 24 + sizeof got, 4);
    CHECK(n == 20 + 4 + sizeof(arc__input_snap) + 8 && t2 == 5 && got.mx == 33 && (got.keys[ARC_KEY_A / 8] >> (ARC_KEY_A % 8) & 1) && got.tcount == 0 && e2 == 0xFFFFFFFFu);
    free(w);
}
static void v1011_lines(void) {   /* the state functions and their limits */
    float w0 = arc_line_width();
    arc_set_line_width(4); CHECK(arc_line_width() == 4.0f);
    arc_set_line_width(0.5f); CHECK(arc_line_width() == 1.0f);
    arc_set_line_width(1000); CHECK(arc_line_width() == 256.0f);
    arc_set_line_width(w0);
    arc_set_line_glow(1); arc_set_line_cap(ARC_LINE_CAP_SQUARE);
}
static void v1011_init(void) { v1011_touch(); v1011_replay_v3(); v1011_lines(); }
