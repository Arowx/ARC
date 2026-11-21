# UI: immediate-mode widgets (menus, settings, debug panels)

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **UI: immediate-mode widgets (menus, settings, debug panels)** · [quick reference](../QUICK.md) · [full API](../API.md)


Call these from tick(): a click changes state on the tick it happens, so replays and agents see the same
   thing. ARC records the drawing and shows it on top of the next frame (normal and immediate mode).
   Mouse/touch, plus keyboard and gamepad: UP/DOWN (or the d-pad) move a focus box, ENTER/SPACE/PAD_A press,
   LEFT/RIGHT change sliders and selects. Labels are shown up to "##": "Play##main" shows "Play" (use ## to
   tell apart widgets with the same text). Text that doesn't fit a widget shrinks (to 60%), then ends in "...".
   Agents: `ui` lists last tick's widgets, `ui_click LABEL` clicks one.

       arc_ui_begin("Settings", 20, 20, 300);
       if (arc_ui_button("Start")) start_game();
       arc_ui_slider("Volume", &volume, 0, 1);
       arc_ui_end();

```c
ARC_API void arc_ui_begin(const char *title, float x, float y, float width);    // a panel; "" = no title bar; drag the title to move it
```

0.10.12 panel positions (MechBattleArc): a panel's title bar can be dragged, and ARC keeps the move as an offset
   from where arc_ui_begin puts it, by title ("##" ids count). arc_ui_panel_offset reads it (false: never shown or set),
   arc_ui_set_panel_offset gives it back (before the panel is first shown is fine; 0, 0 resets it), so a game can save
   it with its settings. arc_ui_panel_rect: the panel's rectangle last tick in window pixels, after the drag (its real
   height, for placing it). Dragged panels always keep their title bar inside the window and the safe area, while
   dragging and when the window shrinks. frame.ui_panels lists title, rect and offset.

```c
ARC_API bool arc_ui_panel_offset(const char *title, float *dx, float *dy);
ARC_API void arc_ui_set_panel_offset(const char *title, float dx, float dy);
ARC_API bool arc_ui_panel_rect(const char *title, float *x, float *y, float *w, float *h);
ARC_API void arc_ui_end(void);
ARC_API void arc_ui_columns(int n);    // the next widgets sit n to a row (1 = back to one per row)
ARC_API void arc_ui_label(const char *text);
ARC_API void arc_ui_labelf(const char *fmt, ...);
ARC_API void arc_ui_text_wrapped(const char *text);    // 0.8.2: a paragraph wrapped to the panel (or column) width, in the UI font
ARC_API void arc_ui_separator(void);
ARC_API bool arc_ui_button(const char *label);    // true on the tick it is clicked
ARC_API bool arc_ui_button_ex(const char *label, bool enabled);    // disabled: drawn faded, can't be pressed ("can't afford")
ARC_API void arc_ui_enabled(bool on);    // false: the widgets that follow are disabled (until true, or the next tick)
ARC_API bool arc_ui_sound_settings(void);    // Master/Music/Effects/Interface/Voices sliders; true when one changed
ARC_API bool arc_ui_toggle(const char *label, bool *v);    // these return true when the value changed
ARC_API bool arc_ui_slider(const char *label, float *v, float lo, float hi);
ARC_API bool arc_ui_slider_int(const char *label, int *v, int lo, int hi);
ARC_API bool arc_ui_select(const char *label, int *index, const char *const *items, int count);    // < item >: click a side or LEFT/RIGHT
ARC_API bool arc_ui_list(const char *label, int *selected, const char *const *items, int count);    // one row per item
ARC_API bool arc_ui_text(const char *label, char *buf, int cap);    // click to type; true when ENTER finishes
ARC_API void arc_ui_progress(float fraction, const char *label);
ARC_API void arc_ui_inspector(float x, float y);    // every arc_var as a slider (also ` with dev keys on, or the `inspector` command)
ARC_API bool arc_ui_wants_mouse(void);    // the pointer is over a panel: don't also fire the gun
ARC_API bool arc_ui_wants_keyboard(void);    // typing in a text field, or moving the focus with keys
typedef struct { float scale; float padding, spacing; uint32_t panel, title, widget, hover, active, accent, text, text_dim, focus; float anim; } arc_ui_theme;
ARC_API void arc_ui_set_theme(const arc_ui_theme *t);    // NULL = the default dark theme
ARC_API arc_ui_theme arc_ui_get_theme(void);
ARC_API void arc_ui_set_font(arc_font f, float scale);    // {0} = the built-in font
```

Skins (0.6.1): a nine-slice texture (see arc_draw_nine) in place of a part's flat colour. dst_border is in
   pixels before the DPI scale. {0} turns a part back to its colour. Hover falls back to the widget skin and
   active to hover; a disabled widget draws its skin half see-through.

```c
enum { ARC_UI_SKIN_PANEL, ARC_UI_SKIN_TITLE, ARC_UI_SKIN_WIDGET, ARC_UI_SKIN_HOVER, ARC_UI_SKIN_ACTIVE, ARC_UI_SKIN_COUNT };
ARC_API void arc_ui_set_skin(int part, arc_texture t, float src_border, float dst_border);
```

0.7, all off by default:
   Navigation: arc_ui_set_nav(true) shows the focus ring from the start and moves it by layout: arrows, the d-pad and
   the left stick go to the nearest widget that way (LEFT/RIGHT stay with a focused slider or select, which they
   change), held directions repeat, and UP/DOWN wrap at the ends. ENTER/SPACE/PAD_A press, as before. Moving the mouse
   hides the ring until the next direction. Without it, the 0.6 keys work as they always have.
   arc_ui_back_pressed: ESC or PAD_B went down this tick and no text field took it (always available).
   Scale: multiplies every widget's size, padding and text on top of arc_dpi_scale() (1 = normal; e.g. 1.5 for a
   "large interface" option). Panel positions stay where the game puts them.
   Rich: labels and widget text read markup (see arc_rich_draw); text that won't fit even shrunk loses its markup
   and is cut short with "...". `ui` lists labels without markup.
   Animation: arc_ui_theme.anim = seconds (0.15 is lively): panels scale and fade in when they appear and out when
   they stop being drawn, widgets fade between their colours on hover and sink a little while pressed. Cosmetic only:
   positions used for clicks never move.

```c
ARC_API void arc_ui_set_nav(bool on);
ARC_API bool arc_ui_back_pressed(void);
ARC_API const char *arc_ui_focused(void);    // the label (up to "##") with the focus ring last tick, "" if none
ARC_API void arc_ui_set_scale(float s);
ARC_API float arc_ui_scale(void);
```

0.10.1 layout that fits any window (MechBattleArc's request).
   - arc_ui_scale_auto(1280, 720, 0.75f, 4): the UI scale follows the window (the smaller of w / 1280 and h / 720,
     clamped), every tick from now on; with a bitmap UI font it lands on whole-pixel text sizes. 0, 0 stops it.
   - arc_safe_area(): the window minus notches and browser bars (web: CSS env(safe-area-inset-*)); the whole window
     on desktop. --safe-area L,T,R,B (pixels) pretends there are insets, for tests.
   - arc_ui_begin_anchored(title, ARC_ANCHOR_BOTTOM | ARC_ANCHOR_RIGHT, 10, 10, 220, 0): a panel placed from a
     corner, edge or the centre of the safe area, offsets and size in UI units (h 0 = its height last tick). It
     stays inside the safe area, shrinking (down to a quarter) when the window is too small. Not draggable.
   - arc_orientation(); arc_ui_require_landscape(text): in portrait, covers the screen with a "turn sideways" card
     and returns true (pause the game); call it after the rest of the UI. The text is in the UI's font and scale:
     the first line (up to '\n') a title at twice the size, the rest wrapped to the window (0.10.6).
   - The `ui_fit 1280x720 800x600 ...` command runs one tick at each size and reports widgets outside the window
     (or the safe area) or overlapping each other, as ARC error lines, so arc_check fails on them.

```c
typedef struct { float x, y, w, h; } arc_rectf;
enum { ARC_ANCHOR_CENTER = 0, ARC_ANCHOR_LEFT = 1, ARC_ANCHOR_RIGHT = 2, ARC_ANCHOR_TOP = 4, ARC_ANCHOR_BOTTOM = 8, ARC_ANCHOR_TOP_LEFT = 5, ARC_ANCHOR_TOP_RIGHT = 6, ARC_ANCHOR_BOTTOM_LEFT = 9, ARC_ANCHOR_BOTTOM_RIGHT = 10 };
enum { ARC_LANDSCAPE = 0, ARC_PORTRAIT = 1 };
ARC_API float arc_ui_scale_auto(float ref_w, float ref_h, float min_scale, float max_scale);    // returns the scale now
ARC_API arc_rectf arc_safe_area(void);
ARC_API void arc_ui_begin_anchored(const char *title, int anchor, float offset_x, float offset_y, float w, float h);
ARC_API int arc_orientation(void);
ARC_API bool arc_ui_require_landscape(const char *text);
ARC_API void arc_ui_set_rich(bool on);
```
