# Text: built-in font, baked fonts, rich text

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


Built-in 8x8 font. scale 1 = 8px tall. Supports \n. Returns width in pixels.

```c
ARC_API float arc_draw_text(float x, float y, float scale, uint32_t rgb, const char *text);
ARC_API float arc_text_width(float scale, const char *text);
ARC_API float arc_draw_textf(float x, float y, float scale, uint32_t rgb, const char *fmt, ...);
```

Aligned and decorated text (built-in font). (x, y) is the anchor the flags describe: ARC_TEXT_CENTER/RIGHT
   (horizontal; default left), ARC_TEXT_MIDDLE/BOTTOM (vertical; default top), ARC_TEXT_SHADOW (a dark copy one
   step down-right), ARC_TEXT_OUTLINE (dark all round). Returns the width.
   ARC_TEXT_RICH (0.7) reads markup (see arc_rich_draw); arc_font_draw_aligned takes the same flags.

```c
enum { ARC_TEXT_CENTER = 1, ARC_TEXT_RIGHT = 2, ARC_TEXT_MIDDLE = 4, ARC_TEXT_BOTTOM = 8, ARC_TEXT_SHADOW = 16, ARC_TEXT_OUTLINE = 32, ARC_TEXT_RICH = 64, ARC_TEXT_EXACT = 128 };
```

0.10.12: the built-in 8x8 font drops rows below scale 1 and on half pixels ("DOCK" read "DUCK"). It is drawn on whole
   pixels (after alignment), and arc_text_fit_scale returns whole sizes from 1 up. arc_text_snap_builtin: ARC_TEXT_SNAP_PIXELS
   (the default), ARC_TEXT_SNAP_SIZES (sizes of 1 and more rounded to whole numbers as well; arc_text_width / _height
   agree), ARC_TEXT_SNAP_OFF (as given, the old behaviour). ARC_TEXT_EXACT for one draw. Baked fonts are unchanged.

```c
enum { ARC_TEXT_SNAP_OFF = 0, ARC_TEXT_SNAP_PIXELS = 1, ARC_TEXT_SNAP_SIZES = 2 };
ARC_API void arc_text_snap_builtin(int mode);
ARC_API float arc_draw_text_ex(float x, float y, float scale, uint32_t rgb, int flags, const char *text);
ARC_API float arc_text_height(float scale, const char *text);
```

the largest built-in-font scale (up to max_scale) at which text fits max_width (for fitting labels)

```c
ARC_API float arc_text_fit_scale(const char *text, float max_width, float max_scale);
```

0.8.2: word-wrapped text in a column `width` pixels wide starting at (x, y). Lines break at spaces and '\n'; a word
   wider than the column is split. Flags: ARC_TEXT_CENTER/RIGHT align each line in the column, MIDDLE/BOTTOM place the
   block's middle/bottom at y, SHADOW, OUTLINE (not RICH). Returns the height used; arc_text_wrapped_height measures
   without drawing (for sizing boxes). Line spacing as arc_draw_text.

```c
ARC_API float arc_draw_text_wrapped(float x, float y, float width, float scale, uint32_t rgb, int flags, const char *text);
ARC_API float arc_text_wrapped_height(float width, float scale, const char *text);
```

---- Bitmap fonts from TTF, baked offline by tools/font_bake.py into .arcf (one file per pixel size) ----
   Glyphs are placed exactly as raylib's LoadFontEx/DrawTextEx place them. (x, y) is the top of the line.
   Text is UTF-8; a codepoint the font lacks draws as '?'.

```c
typedef struct { uint32_t id; } arc_font;
ARC_API arc_font arc_font_load(const char *path);
ARC_API arc_font arc_font_from_memory(const uint8_t *data, size_t size);
ARC_API void arc_font_destroy(arc_font f);
ARC_API int arc_font_size(arc_font f);    // the pixel size it was baked at (= line height)
```

0.10.1 distance-field fonts (tools/font_bake.py --sdf): one .arcf drawn sharp at any size: scale = wanted px /
   arc_font_size (e.g. a 48 px bake drawn at 17.5 px: scale 17.5f / 48). The edge is smoothed over one screen pixel
   on every renderer; outline and shadow flags work as for bitmap fonts. 3D billboard text keeps the field's soft
   edge. Lines of baked-font text start on whole pixels (arc_text_snap, on by default) so sharp bitmaps stay sharp.

```c
ARC_API bool arc_font_is_sdf(arc_font f);
ARC_API void arc_text_snap(bool on);
ARC_API bool arc_font_has(arc_font f, uint32_t codepoint);
ARC_API float arc_font_draw(arc_font f, float x, float y, uint32_t tint, const char *utf8);    // returns width drawn
ARC_API float arc_font_draw_ex(arc_font f, float x, float y, float scale, float spacing, uint32_t tint, const char *utf8);
ARC_API float arc_font_width(arc_font f, float scale, float spacing, const char *utf8);
ARC_API float arc_font_draw_aligned(arc_font f, float x, float y, float scale, uint32_t tint, int flags, const char *utf8);    // ARC_TEXT_*
```

0.8.2: word-wrapped text in a baked font, as arc_draw_text_wrapped (lines arc_font_size * scale apart)

```c
ARC_API float arc_font_draw_wrapped(arc_font f, float x, float y, float width, float scale, uint32_t tint, int flags, const char *utf8);
ARC_API float arc_font_wrapped_height(arc_font f, float width, float scale, const char *utf8);
ARC_API void arc_draw_billboard_text_font(arc_font f, arc_v3 pos, float height, uint32_t rgb, uint32_t bg_argb, int flags, const char *utf8);    // 3D text in a loaded font
ARC_API void arc_font_draw_glyph(arc_font f, uint32_t codepoint, float x, float y, float scale, uint32_t tint);
ARC_API float arc_font_advance(arc_font f, uint32_t codepoint, float scale);
ARC_API uint32_t arc_utf8_next(const char **s);    // decode one codepoint and advance *s (0 at the end)
```

One glyph, for drawing text yourself (rotated, through your own transforms): its rect in the atlas texture,
   where its top-left goes relative to the pen (x, line top), and how far the pen moves. Missing -> '?'.

```c
typedef struct { arc_texture texture; int atlas_w, atlas_h; float sx, sy, sw, sh, ox, oy, advance; } arc_glyph;
ARC_API bool arc_font_glyph(arc_font f, uint32_t codepoint, arc_glyph *out);
```

---- Rich text (0.7): markup inside one string, only read by these functions, ARC_TEXT_RICH and arc_ui_set_rich ----
     [c=FF8000]orange[/c]   colour (RRGGBB, or AARRGGBB); nests; [/c] goes back to the colour before
     [icon=coin]            a registered icon, one line tall, as wide as its picture's aspect
     [key=F] [key=SPACE]    a keycap with the key's name; [key=PAD_A].. [key=PAD_Y] draw the pad's coloured buttons
     [[                     a literal '['. Anything else in brackets is drawn as it is.
   f {0} = the built-in font (scale 1 = 8 px), else that font (scale 1 = its size). Lines split at '\n'. Plain text
   draws and measures exactly like arc_draw_text / arc_font_draw_ex. Flags are ARC_TEXT_* (alignment, shadow,
   outline: icons and keycaps get no shadow). Returns the width; widths and heights include icons and keycaps.

```c
ARC_API float arc_rich_draw(arc_font f, float x, float y, float scale, uint32_t rgb, int flags, const char *text);
ARC_API float arc_rich_width(arc_font f, float scale, const char *text);
ARC_API float arc_rich_height(arc_font f, float scale, const char *text);
ARC_API void arc_rich_icon(const char *name, arc_texture t, float sx, float sy, float sw, float sh);    // sw or sh 0 = the whole texture; up to 64 names
ARC_API size_t arc_rich_strip(const char *text, char *out, size_t cap);    // the plain text: tags dropped, [key=F] -> F; returns its length
```
