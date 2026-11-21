# MechBattleArc: read and set where the player dragged an arc_ui panel

**From:** MechBattleArc (MOLTEN BREACH), 20 Nov 2026

## Problem
`arc_ui_begin(title, x, y, w)` lets the player drag a panel by its title bar, and ARC keeps the offset
(`ui_panel_rec.dx/dy`, keyed by the title's hash) until the program ends. The game can't see that offset or give it
back, so a moved panel snaps back to its default place every time the game starts.

The tutorial card is the case (`ui_tutorial` in main.c). Players move it off whatever they're looking at. It now stays
put across tutorial steps and after the debrief, because its title no longer changes. But on the next run it's back at
the bottom of the right column, and the player has to move it again.

Two smaller gaps came up at the same time:
- The game can't learn a panel's size, so it guesses the card's height from its text length to place it above the hint
  bar (`(220 + 19.5 x lines) x hud_s()`, with lines being about 36 characters each).
- A panel dragged partly off the window (or left there after the window shrinks) stays off-screen.

## Ask
1. **Read and set the offset**, so the game can save it with its other settings:
   ```c
   bool arc_ui_panel_offset(const char *title, float *dx, float *dy);   /* false if the panel was never shown */
   void arc_ui_set_panel_offset(const char *title, float dx, float dy); /* before or after it is first shown */
   ```
   (Or the equivalent keyed by the same hash `arc_ui_begin` uses, "##" ids included.) A reset is just setting 0, 0.
2. **Last tick's rectangle of a panel**, after any drag:
   ```c
   bool arc_ui_panel_rect(const char *title, float *x, float *y, float *w, float *h);
   ```
   The game could then size and place a panel from its real height, not an estimate. It could also keep it clear of the
   hand-drawn HUD cards.
3. **Keep dragged panels on screen**: clamp the offset so at least the title bar stays inside the window (and the safe
   area), both while dragging and when the window is resized. Without this, a saved offset from a bigger window could
   bring a panel back out of view.

## How the game would use it
- On start (`settings_load`), read `tut_dx` / `tut_dy` and call `arc_ui_set_panel_offset(ICO_KEYS " Tutorial", dx, dy)`.
- In `settings_tick`, read `arc_ui_panel_offset(...)`. If it changed, save it in the settings with the rest
  (`moltenbreach.settings`).
- An Options or Help item, "Reset panel positions", sets it back to 0, 0.
- `ui_tutorial` uses `arc_ui_panel_rect` from the last tick to sit the card right above the hint bar, and drops its
  height estimate.

## Tests that would show it works
- Headless: `mouse X Y; press MOUSE_LEFT; mouse X2 Y2; release MOUSE_LEFT` then `arc_ui_panel_offset` returns the move.
  After a set, the panel draws at default + offset on its first tick. The `ui` listing could show each panel's rect and
  offset.
- Dragging a panel past the window edge leaves its title bar on screen. Resizing to 960x540 pulls an off-screen panel
  back in.

## Done in ARC 0.10.12

All three, keyed by the title as `arc_ui_begin` hashes it ("##" ids included).

1. **Read and set the offset:**
   - `bool arc_ui_panel_offset(const char *title, float *dx, float *dy)` returns false if the panel was never shown
     or set.
   - `void arc_ui_set_panel_offset(const char *title, float dx, float dy)` works before the panel is first shown, so
     `settings_load` can call it at start. 0, 0 resets it.
2. **The rectangle:** `bool arc_ui_panel_rect(title, &x, &y, &w, &h)` gives last tick's rectangle in window pixels,
   after the drag and with the real height. `ui_tutorial` can sit the card right above the hint bar from it.
3. **Kept on screen:** every tick, a dragged panel's offset is clamped so its title bar stays inside the window and
   the safe area: at least 48 UI pixels of it across, and the whole bar down.
   - That holds while dragging, after the window shrinks, and when a saved offset from a bigger window is given back.
   - Anchored panels are unchanged (they never move).

Also:
- `frame.ui_panels` lists each panel shown with its `title`, `rect` and `offset`, so checks can see it.
- The self-test drags a panel's title bar 40 right and 30 down headless (`mouse`, `press MOUSE_LEFT`, `mouse`,
  `release`) and reads back the move. It sets an offset, resets it, and pushes the panel far off each side to see it
  come back.
- Resizing to 960x540 uses the same per-tick clamp. There's no window resize in the self-test, so that isn't run as
  such.
- Macro `ARC_HAS_PANEL_OFFSET` (1012).
