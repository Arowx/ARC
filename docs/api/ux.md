# UX: easing, screen transitions, captions, colour blindness, localisation

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **UX: easing, screen transitions, captions, colour blindness, localisation** · [quick reference](../QUICK.md) · [full API](../API.md)


Easing (0.7): t is clamped to 0..1; every curve gives 0 at 0 and 1 at 1 (back and elastic overshoot between).
   arc_ease(ARC_EASE_OUT_BACK, t) picks one by number (for settings and data files).

```c
typedef enum { ARC_EASE_LINEAR, ARC_EASE_IN_QUAD, ARC_EASE_OUT_QUAD, ARC_EASE_INOUT_QUAD, ARC_EASE_IN_CUBIC, ARC_EASE_OUT_CUBIC, ARC_EASE_INOUT_CUBIC, ARC_EASE_IN_BACK, ARC_EASE_OUT_BACK, ARC_EASE_INOUT_BACK, ARC_EASE_OUT_ELASTIC, ARC_EASE_OUT_BOUNCE, ARC_EASE_INOUT_SINE, ARC_EASE_COUNT } arc_ease_kind;
ARC_API float arc_ease(arc_ease_kind kind, float t);
ARC_API float arc_ease_in_quad(float t);
ARC_API float arc_ease_out_quad(float t);
ARC_API float arc_ease_inout_quad(float t);
ARC_API float arc_ease_in_cubic(float t);
ARC_API float arc_ease_out_cubic(float t);
ARC_API float arc_ease_inout_cubic(float t);
ARC_API float arc_ease_in_back(float t);
ARC_API float arc_ease_out_back(float t);    // overshoots ~10% then settles: pop-ups
ARC_API float arc_ease_inout_back(float t);
ARC_API float arc_ease_out_elastic(float t);    // springs past and wobbles
ARC_API float arc_ease_out_bounce(float t);
ARC_API float arc_ease_inout_sine(float t);
ARC_API float arc_tween(float from, float to, float t, arc_ease_kind kind);    // from + (to - from) * arc_ease(kind, t)
```

Screen transitions (0.7): the screen is covered over `seconds` (in rgb), stays covered for one tick, then is
   uncovered over `seconds`. Swap levels or screens on the tick arc_transition_covered() is true. Drawn on top of
   everything (UI included; captions stay above). Ticks drive it, so replays match. seconds <= 0 stops one.

```c
enum { ARC_TRANSITION_FADE, ARC_TRANSITION_WIPE_LEFT, ARC_TRANSITION_WIPE_DOWN };
ARC_API void arc_transition(int kind, float seconds, uint32_t rgb);
ARC_API bool arc_transition_covered(void);    // the one tick the screen is fully covered
ARC_API bool arc_transition_active(void);
ARC_API float arc_transition_amount(void);    // how much is covered, 0..1
```

Captions / subtitles (0.7): queued lines shown one after another for their seconds, centred near the bottom of
   the screen on a dark backdrop, in the UI font and scale; markup works (see arc_rich_draw). rgb 0 = white.
   Time passes in ticks (pausing the game's ticks pauses them).

```c
ARC_API void arc_caption(const char *text, float seconds, uint32_t rgb);    // up to 16 waiting; '\n' for two lines
ARC_API void arc_caption_clear(void);
ARC_API const char *arc_caption_current(void);    // the line showing now ("" if none)
ARC_API void arc_caption_style(float scale, uint32_t backdrop_argb);    // scale x the UI text (0 = 1); backdrop 0 = 0xB0000000
```

Colour blindness (0.7). arc_palette_cb: the Okabe-Ito colours, told apart by people with any common colour
   blindness: 0 orange, 1 sky blue, 2 bluish green, 3 yellow, 4 blue, 5 vermilion, 6 reddish purple, 7 black (index
   wraps). Pick team and key colours from it. arc_cvd_simulate: how a colour looks with that colour blindness (Machado
   2009, full strength). arc_cvd_daltonize: moves the difference they can't see into colours they can. arc_set_cvd
   picks a mode for arc_cvd(), so a game wraps its key colours once (arc_cvd(0xFF0000)) and an options menu chooses
   the mode; NONE returns colours unchanged. arc_cvd_lut: a colour-grading LUT that daltonizes the whole picture
   (arc_post.lut), made once per mode.

```c
enum { ARC_CVD_NONE, ARC_CVD_PROTAN, ARC_CVD_DEUTAN, ARC_CVD_TRITAN };
ARC_API uint32_t arc_palette_cb(int index);
ARC_API uint32_t arc_cvd_simulate(uint32_t rgb, int mode);
ARC_API uint32_t arc_cvd_daltonize(uint32_t rgb, int mode);
ARC_API void arc_set_cvd(int mode);
ARC_API int arc_get_cvd(void);
ARC_API uint32_t arc_cvd(uint32_t rgb);    // keeps the alpha byte
ARC_API arc_texture arc_cvd_lut(int mode);
```

Localisation (0.7): string tables, one UTF-8 text file per language, `key = text` per line ('#' starts a comment
   line; \n in the text is a new line, \\ a backslash). Files load from mounted .arcpak packs first, like every
   loader; loading a language again adds to it (later keys win). arc_tr: the active language's text, else the
   fallback language's (the first one loaded unless set), else the key itself, so missing strings show up.
   The pointers stay valid until arc_tr_clear.

```c
ARC_API bool arc_tr_load(const char *lang, const char *path);
ARC_API bool arc_tr_load_memory(const char *lang, const char *text, size_t size);
ARC_API void arc_tr_set_language(const char *lang);
ARC_API const char *arc_tr_language(void);
ARC_API void arc_tr_set_fallback(const char *lang);
ARC_API const char *arc_tr(const char *key);
ARC_API char *arc_trf(const char *key, ...);    // the text as a printf format, into the frame arena
ARC_API bool arc_tr_has(const char *lang, const char *key);
ARC_API int arc_tr_languages(const char **names, int max);    // loaded language names, in load order
ARC_API void arc_tr_clear(void);
```
