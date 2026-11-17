/* ARC 0.10.5 self-test: several gamepads (pad slots, the old single-pad view from the first one, scripts' `pad`
   command, replay format 2 widened to 3). Included by test_core.c; v105_init() runs in init. Names start with v105_. */
static void v105_init(void) {
    arc__input_pad_slot(2, true); arc__input_pad_button(2, ARC_PAD_A, true); arc__input_pad_axis(2, ARC_AXIS_LY, -0.75f);
    arc__input_test_step();
    CHECK(arc_pad_count() == 1 && arc_pad_is_connected(2) && !arc_pad_is_connected(0) && arc_pad_first() == 2);
    CHECK(arc_pad_down(2, ARC_PAD_A) && arc_pad_pressed(2, ARC_PAD_A) && !arc_pad_down(0, ARC_PAD_A));
    CHECK(arc_key_down(ARC_PAD_A) && arc_pad_connected() && arc_pad_axis(ARC_AXIS_LY) == -0.75f && arc_pad_axis_of(2, ARC_AXIS_LY) == -0.75f);   /* the old view: the first pad */
    arc_key b = ARC_KEY_NONE; CHECK(arc_pad_any_pressed(&b) == 2 && b == ARC_PAD_A);
    arc__input_pad_slot(0, true); arc__input_pad_axis(0, ARC_AXIS_LX, 0.5f);
    arc__input_test_step();
    CHECK(arc_pad_count() == 2 && arc_pad_first() == 0 && !arc_key_down(ARC_PAD_A) && arc_pad_axis(ARC_AXIS_LX) == 0.5f && arc_pad_axis(ARC_AXIS_LY) == 0);   /* now pad 0 is first */
    CHECK(arc_pad_down(2, ARC_PAD_A) && !arc_pad_pressed(2, ARC_PAD_A) && arc_pad_any_pressed(NULL) == -1);
    arc__input_pad_button(2, ARC_PAD_A, false); arc__input_pad_button(0, ARC_PAD_B, true); arc__input_pad_button(0, ARC_PAD_B, false);   /* a tap between ticks */
    arc__input_test_step();
    CHECK(arc_pad_released(2, ARC_PAD_A) && arc_pad_down(0, ARC_PAD_B) && arc_pad_any_pressed(&b) == 0 && b == ARC_PAD_B && arc_key_down(ARC_PAD_B));
    arc__input_pad_slot(0, false); arc__input_test_step();
    CHECK(arc_pad_count() == 1 && arc_pad_first() == 2 && arc_pad_axis_of(0, ARC_AXIS_LX) == 0 && arc_pad_axis(ARC_AXIS_LY) == -0.75f);
    /* scripts: pad N ... */
    arc__test_command("pad 3 press X"); arc__test_command("pad 3 axis RT 1"); arc__input_test_step();
    CHECK(arc_pad_is_connected(3) && arc_pad_down(3, ARC_PAD_X) && arc_pad_axis_of(3, ARC_AXIS_RT) == 1.0f && arc_pad_first() == 2);
    arc__test_command("pad 3 release PAD_X"); arc__input_test_step(); CHECK(arc_pad_released(3, ARC_PAD_X));
    arc__test_command("pad 3 disconnect"); arc__input_pad_slot(2, false); arc__input_test_step();
    CHECK(arc_pad_count() == 0 && arc_pad_first() == -1 && !arc_pad_connected() && !arc_key_down(ARC_PAD_A));
    /* a version 2 replay: snaps of the old size, one pad -> pad 0 */
    size_t v2 = offsetof(arc__input_snap, padmask); uint8_t buf[20 + 2 * (4 + sizeof(arc__input_snap)) + 8]; memset(buf, 0, sizeof buf);
    memcpy(buf, "ARCR", 4); uint32_t ver = 2; memcpy(buf + 4, &ver, 4);
    arc__input_snap old; memset(&old, 0, sizeof old); old.flags = ARC__IN_PAD; old.axes[ARC_AXIS_LX] = 0.25f;
    old.keys[ARC_PAD_Y >> 3] |= (uint8_t)(1 << (ARC_PAD_Y & 7));
    uint32_t t0 = 5, end = 0xFFFFFFFFu, total = 9; size_t o = 20;
    memcpy(buf + o, &t0, 4); memcpy(buf + o + 4, &old, v2); o += 4 + v2; memcpy(buf + o, &end, 4); memcpy(buf + o + 4, &total, 4); o += 8;
    size_t n3 = 0; uint8_t *w = arc__replay_widen(buf, o, &n3);
    CHECK(n3 == 20 + 4 + sizeof(arc__input_snap) + 8);
    arc__input_snap ns; memcpy(&ns, w + 24, sizeof ns); uint32_t t1; memcpy(&t1, w + 20, 4); uint32_t e1; memcpy(&e1, w + 24 + sizeof ns, 4);
    CHECK(t1 == 5 && e1 == end && ns.padmask == 1 && (ns.pbtn[0] >> (ARC_PAD_Y - ARC_PAD_A) & 1) && ns.pax[0][ARC_AXIS_LX] == 0.25f && ns.axes[ARC_AXIS_LX] == 0.25f);
    free(w);
}
