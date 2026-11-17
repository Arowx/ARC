# Request from MechBattleArc (Molten Breach): 2D drawn over arc_ui windows

Molten Breach shows a full-screen "TURN SIDEWAYS" notice when a phone is held upright, and full-page How to play /
Credits panels. These are 2D drawn in `draw()`, but `arc_ui` windows are drawn after the game's 2D, so they show
through on top of a screen that should cover everything.

**Ask.** One of:
- an `arc_app.draw_top` callback, run after the UI is drawn (2D only), or
- `arc_ui_hide_all(bool)` for this frame, or
- a layer value for `arc_draw_*` that is drawn after the UI.
