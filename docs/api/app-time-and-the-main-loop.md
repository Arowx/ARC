# App, time and the main loop

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **App, time and the main loop** · [quick reference](../QUICK.md) · [full API](../API.md)

```c
typedef struct arc_json arc_json;
typedef struct arc_net_ev arc_net_ev;    // 0.8 multiplayer events (see Multiplayer below)
typedef struct { const char *title; int width, height; int tick_hz; uint32_t clear_color; size_t perm_mb, frame_mb; void (*init)(void); void (*tick)(void); void (*draw)(float alpha); void (*dump)(arc_json *j); void (*shutdown)(void); bool (*command)(const char *name, int argc, const char **argv); bool resizable; int min_width, min_height; const char *save_folder; bool immediate; const char *own_flags; int msaa; bool hidpi; bool dev_keys; size_t (*save_state)(void *buf, size_t cap); bool (*load_state)(const void *buf, size_t size); bool hot_reload; void (*reloaded)(void); bool sessions; void (*draw_loading)(float progress); int depth_mode; void (*net_event)(const arc_net_ev *ev); size_t (*net_state)(void *buf, size_t cap); size_t (*net_input)(void *buf, size_t cap); void (*draw_top)(float alpha); bool browser_decode; int max_2d_vertices; } arc_app;
ARC_GAME_API arc_app arc_main(int argc, char **argv);    // YOU implement this
```

Play-session log (see arc_app.sessions). arc_event("duel_end", "\"won\":%d,\"enemy\":%d", won, n) appends
   {"tick":..,"t":seconds,"ev":"duel_end","won":1,"enemy":442} to events.jsonl (and prints "ARC event {...}" for
   agents). fields is printf-style JSON members without braces; quote strings yourself.

```c
ARC_API void arc_event(const char *name, const char *fields, ...);
ARC_API void arc_bookmark(const char *note);    // what F8 does: a screenshot + a "bookmark" event (note may be NULL)
ARC_API const char *arc_session_dir(void);    // the folder being written, or NULL
ARC_API float arc_dt(void);    // fixed tick length in seconds
ARC_API uint64_t arc_tick_count(void);    // ticks simulated so far
ARC_API double arc_time(void);    // simulated seconds = ticks * dt
ARC_API double arc_real_time(void);    // wall-clock seconds since start (not deterministic; UI only)
ARC_API int arc_width(void);
ARC_API int arc_height(void);
ARC_API float arc_aspect(void);
ARC_API float arc_dpi_scale(void);    // pixels per CSS pixel on the web with hidpi (else 1)
ARC_API bool arc_headless(void);
ARC_API bool arc_window_resized(void);    // the window size changed since the last tick
ARC_API void arc_window_set_icon(const uint8_t *rgba, int w, int h);    // desktop; ignored on the web
ARC_API void arc_window_toggle_fullscreen(void);    // borderless fullscreen on desktop, the Fullscreen API on the web
ARC_API bool arc_window_fullscreen(void);
ARC_API const char *arc_app_dir(void);    // folder of the executable, ending in '/' ("/" on the web)
```

Saves: small files by key. Desktop: %APPDATA%\<save_folder> or $XDG_DATA_HOME (~/.local/share)/<save_folder>,
   written atomically. Web: localStorage (text stored as is, binary as base64). Headless runs use ./arc_saves
   unless --save-dir DIR is given, so tests never touch a player's saves.

```c
ARC_API bool arc_save(const char *key, const void *data, size_t size);
ARC_API uint8_t *arc_load(const char *key, arc_arena *a, size_t *size_out);    // NULL if none; zero-terminated
ARC_API bool arc_save_remove(const char *key);
ARC_API void arc_quit(void);
ARC_API void arc_log(const char *fmt, ...);    // prints "ARC log <text>" to stdout
ARC_API int arc_argc(void);
ARC_API const char *arc_argv(int i);
ARC_API const char *arc_arg_value(const char *name);    // "--name=value" or "--name value" -> value, else NULL
ARC_API bool arc_arg_flag(const char *name);
```
