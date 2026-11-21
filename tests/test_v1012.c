/* ARC 0.10.12 self-test: panel positions (MechBattleArc: read and set the dragged offset, the panel's rectangle, kept
   on screen) and the built-in font on whole sizes (Vector Trader). Included by test_core.c after test_v07_ui.c (it
   uses v7_step); v1012_init() runs in init. Names start with v1012_. */
static void v1012_panel(void) { arc_ui_begin("Tutorial##v1012", 60, 40, 120); arc_ui_label("Move the card"); arc_ui_end(); }
static void v1012_panels(void) {
    v7_build = v1012_panel; v7_step(); v7_step();
    float dx = 9, dy = 9, x = 0, y = 0, w = 0, h = 0;
    CHECK(arc_ui_panel_offset("Tutorial##v1012", &dx, &dy) && dx == 0 && dy == 0);
    CHECK(arc_ui_panel_rect("Tutorial##v1012", &x, &y, &w, &h) && x == 60 && y == 40 && w > 100 && h > 20);
    CHECK(!arc_ui_panel_offset("No such panel", &dx, &dy) && !arc_ui_panel_rect("No such panel", &x, &y, &w, &h));
    /* drag the title bar 40 right, 30 down */
    arc__input_mouse(70, 45); v7_step();
    arc__input_key(ARC_MOUSE_LEFT, true); v7_step();
    arc__input_mouse(110, 75); v7_step(); v7_step();
    arc__input_key(ARC_MOUSE_LEFT, false); v7_step(); v7_step();
    CHECK(arc_ui_panel_offset("Tutorial##v1012", &dx, &dy) && dx == 40 && dy == 30);
    CHECK(arc_ui_panel_rect("Tutorial##v1012", &x, &y, NULL, NULL) && x == 100 && y == 70);
    /* given back (as a game would from its settings), then reset */
    arc_ui_set_panel_offset("Tutorial##v1012", -50, 20); v7_step();
    CHECK(arc_ui_panel_rect("Tutorial##v1012", &x, &y, NULL, NULL) && x == 10 && y == 60);
    /* far off screen: the title bar comes back into the window */
    arc_ui_set_panel_offset("Tutorial##v1012", -5000, -5000); v7_step(); v7_step();
    CHECK(arc_ui_panel_rect("Tutorial##v1012", &x, &y, &w, NULL) && x + w >= 40.0f && y >= 0.0f);
    arc_ui_set_panel_offset("Tutorial##v1012", 5000, 5000); v7_step(); v7_step();
    CHECK(arc_ui_panel_rect("Tutorial##v1012", &x, &y, NULL, NULL) && x <= (float)arc_width() - 40.0f && y <= (float)arc_height());
    arc_ui_set_panel_offset("Tutorial##v1012", 0, 0); v7_step();
    CHECK(arc_ui_panel_rect("Tutorial##v1012", &x, &y, NULL, NULL) && x == 60 && y == 40);
    /* a panel set before it is ever shown */
    arc_ui_set_panel_offset("Later##v1012", 12, 7); CHECK(arc_ui_panel_offset("Later##v1012", &dx, &dy) && dx == 12 && dy == 7);
    v7_build = NULL; v7_step(); arc__input_mouse(0, 0); v7_step();
}
static void v1012_text(void) {
    CHECK(arc_text_fit_scale("DOCK", 59, 3) == 1.0f);          /* 59 / 32 = 1.84: the largest whole size */
    CHECK(arc_text_fit_scale("DOCK", 200, 3) == 3.0f);
    CHECK(arc_text_fit_scale("MISSILE 2", 40, 3) < 1.0f);       /* below 1 stays exact */
    CHECK(arc_text_width(1.5f, "AB") == 24.0f);                  /* sizes stay as given by default */
    arc_text_snap_builtin(ARC_TEXT_SNAP_SIZES);
    CHECK(arc_text_width(1.6f, "AB") == 32.0f && arc_text_width(1.4f, "AB") == 16.0f && arc_text_height(2.4f, "A") == 16.0f);
    arc_text_snap_builtin(ARC_TEXT_SNAP_OFF);
    CHECK(arc_text_width(1.5f, "AB") == 24.0f && arc_text_fit_scale("DOCK", 59, 3) > 1.8f);
    arc_text_snap_builtin(ARC_TEXT_SNAP_PIXELS);
}
static void v1012_init(void) { v1012_panels(); v1012_text(); }
