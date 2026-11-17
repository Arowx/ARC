# Request from MechBattleArc (Molten Breach): word-wrapped text in arc_ui and 2D

Molten Breach's How to play and Credits pages needed paragraphs that wrap to a column. ARC has `arc_text_width`
but no wrapping, so the game wraps words itself (`page_para` in main.c, built on `hud_text_w`). `arc_ui_label`
doesn't wrap either, so long lines run past a window's edge at small interface sizes.

**Ask.**
- `float arc_draw_text_wrapped(float x, float y, float width, float size, uint32_t rgba, int align, const char *text)`:
  draws word-wrapped text and returns the height used. A `measure only` call (rgba 0, or a flag) would size boxes.
- `arc_ui_text_wrapped(const char *text)` (or a flag on `arc_ui_label`): wraps to the window's content width.
