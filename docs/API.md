# ARC API reference

_Generated from `include/arc.h` by `tools/gen_api_docs.py`. Do not edit by hand: change the header, then rerun the tool._

```text
ARC — a small, fast game engine in plain C, with no external libraries.

  Platforms : Windows (Win32 + Direct3D 11), Linux (X11 + OpenGL 3.3),
              Web (Emscripten + WebGL2), and Headless (software renderer, any OS).
  Language  : C11. This header also compiles as C++.
  Design    : one header, unity build, no hidden allocations after startup,
              deterministic fixed-step simulation, agent-drivable from the command line.

Read docs/ARC.md first. In short, a game is four callbacks:

    static void init(void)            { ... }           // once, after the window exists
    static void tick(void)            { ... }           // fixed step (arc_dt() seconds), all game logic
    static void draw(float alpha)     { ... }           // render; alpha = blend between last two ticks
    static void dump(arc_json *j)     { ... }           // optional: write game state for agents/tests
    arc_app arc_main(int argc, char **argv) {
        return (arc_app){ .title = "My Game", .width = 1280, .height = 720,
                          .init = init, .tick = tick, .draw = draw, .dump = dump };
    }

Rules that keep games deterministic (so replays, tests and agents work):
  - Game logic lives in tick(). Read input with arc_key_*, time with arc_tick_count()/arc_dt(),
    randomness with arc_rand_*() (or your own arc_rng seeded from arc_seed()).
  - draw() must not change game state.

Conventions: right-handed, +Y up, camera looks down -Z; angles in radians; colours are 0xRRGGBB
or 0xAARRGGBB (arc_rgba); matrices are column-major float[16].
```

## Sections

- [Math (header-only, inlined)](#math-header-only-inlined)
- [Random numbers (deterministic, PCG32)](#random-numbers-deterministic-pcg32)
- [Memory](#memory)
- [App, time and the main loop](#app-time-and-the-main-loop)
- [Input (sampled once per tick; deterministic and recordable)](#input-sampled-once-per-tick-deterministic-and-recordable)
- [Tunable variables (agents can list/set them: `vars`, `set name value`)](#tunable-variables-agents-can-listset-them-vars-set-name-value)
- [JSON writer (for dump() and tools)](#json-writer-for-dump-and-tools)
- [Graphics](#graphics)
- [Audio: synth effects, sound files, streamed music, 32-voice mixer](#audio-synth-effects-sound-files-streamed-music-32-voice-mixer)
- [Turns and grids (time-unit scheduler, tile grid, line of sight, paths)](#turns-and-grids-time-unit-scheduler-tile-grid-line-of-sight-paths)
- [UI: immediate-mode widgets (menus, settings, debug panels)](#ui-immediate-mode-widgets-menus-settings-debug-panels)
- [UX: easing, screen transitions, captions, colour blindness, localisation](#ux-easing-screen-transitions-captions-colour-blindness-localisation)
- [Collision: ray tests, a static collision world, a character controller](#collision-ray-tests-a-static-collision-world-a-character-controller)
- [Physics (0.9.4): deterministic rigid bodies](#physics-094-deterministic-rigid-bodies)
- [Navigation (0.9.2): navmeshes, paths, agents that follow them](#navigation-092-navmeshes-paths-agents-that-follow-them)
- [Action (0.10.0): AI brains, senses, shots, camera rigs](#action-0100-ai-brains-senses-shots-camera-rigs)
- [Terrain (0.9.6): height grids, splatted ground, scattered props](#terrain-096-height-grids-splatted-ground-scattered-props)
- [Profiling](#profiling)
- [Files](#files)
- [Loading in the background (0.7.1)](#loading-in-the-background-071)
- [Developer tools (0.7.2): tile level editor, replay timeline](#developer-tools-072-tile-level-editor-replay-timeline)
- [Multiplayer (0.8): deterministic maths, turn-based LAN sessions](#multiplayer-08-deterministic-maths-turn-based-lan-sessions)

## Math (header-only, inlined)

```c
#define ARC_PI 3.14159265358979323846f
#define ARC_TAU 6.28318530717958647692f
typedef struct { float x, y; } arc_v2;
typedef struct { float x, y, z; } arc_v3;
typedef struct { float x, y, z, w; } arc_v4;
typedef struct { float m[16]; } arc_m4;    // column-major: m[col*4+row]
typedef struct { float x, y, z, w; } arc_quat;
float arc_minf(float a, float b);
float arc_maxf(float a, float b);
float arc_clampf(float v, float lo, float hi);
float arc_lerpf(float a, float b, float t);
int arc_mini(int a, int b);
int arc_maxi(int a, int b);
int arc_clampi(int v, int lo, int hi);
```

wrap an angle into [-PI, PI)

```c
float arc_wrap_angle(float a);
arc_v2 arc_v2_make(float x, float y);
arc_v2 arc_v2_add(arc_v2 a, arc_v2 b);
arc_v2 arc_v2_sub(arc_v2 a, arc_v2 b);
arc_v2 arc_v2_scale(arc_v2 a, float s);
float arc_v2_dot(arc_v2 a, arc_v2 b);
float arc_v2_len(arc_v2 a);
arc_v2 arc_v2_norm(arc_v2 a);
arc_v3 arc_v3_make(float x, float y, float z);
arc_v3 arc_v3_add(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_sub(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_mul(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_scale(arc_v3 a, float s);
arc_v3 arc_v3_madd(arc_v3 a, arc_v3 b, float s);
float arc_v3_dot(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_cross(arc_v3 a, arc_v3 b);
float arc_v3_len(arc_v3 a);
float arc_v3_dist(arc_v3 a, arc_v3 b);
arc_v3 arc_v3_norm(arc_v3 a);
arc_v3 arc_v3_lerp(arc_v3 a, arc_v3 b, float t);
arc_m4 arc_m4_identity(void);
arc_m4 arc_m4_mul(arc_m4 a, arc_m4 b);
arc_m4 arc_m4_translate(arc_v3 t);
arc_m4 arc_m4_scale(arc_v3 s);
arc_m4 arc_m4_rotate_y(float a);
arc_m4 arc_m4_rotate_x(float a);
arc_m4 arc_m4_rotate_z(float a);
```

translate * rotY(yaw) * scale — the common case for game objects, without three matrix multiplies

```c
arc_m4 arc_m4_trs_y(arc_v3 t, float yaw, arc_v3 s);
```

0.10.14: the yaw that turns a model (front +z, as ARC's models face) toward the direction (dx, dz): for
   arc_m4_trs_y and cam rigs. Looks only: rules code that must match on every machine uses arc_atan2f(dx, dz)

```c
float arc_yaw_to(float dx, float dz);
arc_m4 arc_m4_perspective(float fovy, float aspect, float zn, float zf);
arc_m4 arc_m4_ortho(float l, float r_, float b, float t, float zn, float zf);
arc_m4 arc_m4_look_at(arc_v3 eye, arc_v3 at, arc_v3 up);
arc_v3 arc_m4_mul_point(arc_m4 a, arc_v3 p);
arc_v4 arc_m4_mul_v4(arc_m4 a, arc_v4 p);
ARC_API arc_m4 arc_m4_inverse(arc_m4 m);
```

model matrix from an origin and three (possibly scaled, possibly mirrored) axis vectors:
   world = origin + x*bx + y*by + z*bz

```c
arc_m4 arc_m4_basis(arc_v3 origin, arc_v3 bx, arc_v3 by, arc_v3 bz);
```

shortest-path blend between two angles

```c
float arc_angle_lerp(float a, float b, float t);
arc_quat arc_quat_axis_angle(arc_v3 axis, float a);
ARC_API arc_m4 arc_m4_from_quat(arc_quat q);
ARC_API arc_quat arc_quat_mul(arc_quat a, arc_quat b);
ARC_API arc_quat arc_quat_slerp(arc_quat a, arc_quat b, float t);
```

rotate a vector by a quaternion; the shortest rotation taking direction `from` to `to`

```c
arc_v3 arc_quat_rotate(arc_quat q, arc_v3 v);
ARC_API arc_quat arc_quat_from_to(arc_v3 from, arc_v3 to);
```

any axis (need not be unit length), radians

```c
arc_m4 arc_m4_rotate(arc_v3 axis, float a);
```

translate * rotate(q) * scale: a full transform, as glTF and most editors store them

```c
arc_m4 arc_m4_trs(arc_v3 t, arc_quat q, arc_v3 s);
```

shear: x += xy*y + xz*z, y += yx*x + yz*z, z += zx*x + zy*y (a leaning tower: arc_m4_shear(0.2f, 0, 0, 0, 0, 0))

```c
arc_m4 arc_m4_shear(float xy, float xz, float yx, float yz, float zx, float zy);
```

a direction through a matrix (no translation)

```c
arc_v3 arc_m4_mul_dir(arc_m4 a, arc_v3 d);
```

chain several transforms: arc_m4_chain(3, parent, local, offset) = parent * local * offset

```c
ARC_API arc_m4 arc_m4_chain(int count, ...);
```

## Random numbers (deterministic, PCG32)

```c
typedef struct { uint64_t state, inc; } arc_rng;
ARC_API void arc_rng_seed(arc_rng *r, uint64_t seed, uint64_t stream);
ARC_API uint32_t arc_rng_u32(arc_rng *r);
ARC_API int arc_rng_range(arc_rng *r, int lo, int hi);    // inclusive lo..hi
ARC_API float arc_rng_float(arc_rng *r);    // [0,1)
ARC_API float arc_rng_rangef(arc_rng *r, float lo, float hi);
```

the engine's own generator, seeded from --seed (default 1); use it in tick() only

```c
ARC_API uint32_t arc_rand_u32(void);
ARC_API int arc_rand_range(int lo, int hi);
ARC_API float arc_rand_float(void);
ARC_API float arc_rand_rangef(float lo, float hi);
ARC_API uint64_t arc_seed(void);
```

Noise (0.8.4): smooth value noise for patches, masks and density maps. Same numbers on every machine (integer hash,
   no tables), so it can drive game state. value2: 0..1, one random value per integer lattice point, smoothstep
   between them (features about 1 unit across). fbm2: octaves of it (each twice the frequency, half the weight),
   still 0..1; octaves 0 = 5. A different seed is a different, unrelated pattern.

```c
ARC_API float arc_noise_value2(float x, float z, uint32_t seed);
ARC_API float arc_noise_fbm2(float x, float z, int octaves, uint32_t seed);
```

## Memory


Linear (bump) allocator. Nothing is freed individually; reset or pop to a mark.

```c
typedef struct { uint8_t *base; size_t used, cap; size_t peak; } arc_arena;
ARC_API void arc_arena_init(arc_arena *a, void *mem, size_t cap);
ARC_API void *arc_arena_alloc(arc_arena *a, size_t size, size_t align);    // zeroed; returns NULL when full
#define arc_arena_new(a, T, n) ((T *)arc_arena_alloc((a), sizeof(T) * (size_t)(n), alignof(T)))
size_t arc_arena_mark(arc_arena *a);
void arc_arena_pop(arc_arena *a, size_t mark);
void arc_arena_reset(arc_arena *a);
```

Engine arenas. perm lives for the program; frame is reset at the start of every frame.

```c
ARC_API arc_arena *arc_perm(void);
ARC_API arc_arena *arc_frame(void);
ARC_API char *arc_fmt(const char *fmt, ...);    // printf into the frame arena
```

A named, zeroed block that lives until the program ends and survives code reloads (hot-reload builds):
   the same name gives the same memory. G = arc_persist("game", sizeof *G); in init() and reloaded().

```c
ARC_API void *arc_persist(const char *name, size_t size);
```

Fixed-capacity pool of same-sized items with generation-checked handles.
Iterate live items with: for (int i = 0; i < p.cap; i++) if (arc_pool_alive(&p, i)) {...}

```c
typedef struct { uint32_t v; } arc_handle;    // 0 = invalid. low 20 bits index+1, high 12 bits generation
typedef struct { uint8_t *items; uint16_t *gen; uint8_t *alive; uint32_t *free_list; int item_size, cap, count, free_top; } arc_pool;
ARC_API void arc_pool_init(arc_pool *p, arc_arena *a, int item_size, int cap);
#define arc_pool_make(p, a, T, cap) arc_pool_init((p), (a), (int)sizeof(T), (cap))
ARC_API arc_handle arc_pool_add(arc_pool *p);    // zeroed item; invalid handle when full
ARC_API void arc_pool_remove(arc_pool *p, arc_handle h);
ARC_API void *arc_pool_get(arc_pool *p, arc_handle h);    // NULL if stale
bool arc_pool_alive(const arc_pool *p, int i);
void *arc_pool_at(const arc_pool *p, int i);
ARC_API arc_handle arc_pool_handle_at(const arc_pool *p, int i);
```

Snapshots for arc_app.save_state / load_state. The writer counts bytes even past cap (so return w.len and
ARC calls again with room enough); the reader fails safely on short data.
    size_t save(void *buf, size_t cap) { arc_writer w = { buf, cap }; arc_write(&w, &G.game, sizeof G.game); arc_write_pool(&w, &G.enemies); return w.len; }
    bool load(const void *buf, size_t n) { arc_reader r = { buf, n }; arc_read(&r, &G.game, sizeof G.game); arc_read_pool(&r, &G.enemies); return !r.fail; }

```c
typedef struct { uint8_t *buf; size_t cap, len; } arc_writer;
typedef struct { const uint8_t *data; size_t size, pos; bool fail; } arc_reader;
ARC_API void arc_write(arc_writer *w, const void *p, size_t n);
ARC_API bool arc_read(arc_reader *r, void *p, size_t n);
ARC_API void arc_write_pool(arc_writer *w, const arc_pool *p);    // the items and the free list (the pool must be the same size when read)
ARC_API bool arc_read_pool(arc_reader *r, arc_pool *p);
```

## App, time and the main loop

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

## Input (sampled once per tick; deterministic and recordable)

```c
typedef enum { ARC_KEY_NONE = 0, ARC_KEY_A = 'A', ARC_KEY_B, ARC_KEY_C, ARC_KEY_D, ARC_KEY_E, ARC_KEY_F, ARC_KEY_G, ARC_KEY_H, ARC_KEY_I, ARC_KEY_J, ARC_KEY_K, ARC_KEY_L, ARC_KEY_M, ARC_KEY_N, ARC_KEY_O, ARC_KEY_P, ARC_KEY_Q, ARC_KEY_R, ARC_KEY_S, ARC_KEY_T, ARC_KEY_U, ARC_KEY_V, ARC_KEY_W, ARC_KEY_X, ARC_KEY_Y, ARC_KEY_Z, ARC_KEY_0 = '0', ARC_KEY_1, ARC_KEY_2, ARC_KEY_3, ARC_KEY_4, ARC_KEY_5, ARC_KEY_6, ARC_KEY_7, ARC_KEY_8, ARC_KEY_9, ARC_KEY_SPACE = ' ', ARC_KEY_ESCAPE = 128, ARC_KEY_ENTER, ARC_KEY_TAB, ARC_KEY_BACKSPACE, ARC_KEY_LEFT, ARC_KEY_RIGHT, ARC_KEY_UP, ARC_KEY_DOWN, ARC_KEY_SHIFT, ARC_KEY_CTRL, ARC_KEY_ALT, ARC_KEY_F1, ARC_KEY_F2, ARC_KEY_F3, ARC_KEY_F4, ARC_KEY_F5, ARC_KEY_F6, ARC_KEY_F7, ARC_KEY_F8, ARC_KEY_F9, ARC_KEY_F10, ARC_KEY_F11, ARC_KEY_F12, ARC_KEY_GRAVE, ARC_KEY_MINUS, ARC_KEY_EQUAL, ARC_KEY_LBRACKET, ARC_KEY_RBRACKET, ARC_KEY_BACKSLASH, ARC_KEY_SEMICOLON, ARC_KEY_APOSTROPHE, ARC_KEY_COMMA, ARC_KEY_PERIOD, ARC_KEY_SLASH, ARC_KEY_INSERT, ARC_KEY_DELETE, ARC_KEY_HOME, ARC_KEY_END, ARC_KEY_PAGEUP, ARC_KEY_PAGEDOWN, ARC_KEY_KP_ENTER, ARC_MOUSE_LEFT = 200, ARC_MOUSE_RIGHT, ARC_MOUSE_MIDDLE, ARC_PAD_A = 210, ARC_PAD_B, ARC_PAD_X, ARC_PAD_Y, ARC_PAD_LB, ARC_PAD_RB, ARC_PAD_LT, ARC_PAD_RT, ARC_PAD_BACK, ARC_PAD_START, ARC_PAD_LSTICK, ARC_PAD_RSTICK, ARC_PAD_UP, ARC_PAD_DOWN, ARC_PAD_LEFT, ARC_PAD_RIGHT, ARC_PAD_GUIDE, ARC_KEY_COUNT = 256 } arc_key;
ARC_API bool arc_key_down(arc_key k);    // held this tick
ARC_API bool arc_key_pressed(arc_key k);    // went down this tick
ARC_API bool arc_key_released(arc_key k);    // went up this tick
ARC_API arc_v2 arc_mouse(void);    // pixels, origin top-left
ARC_API arc_v2 arc_mouse_delta(void);    // pixels moved this tick
```

0.6.7 mouse look: while captured the pointer is hidden and held inside the window, and arc_mouse_delta() keeps
   reporting movement at any edge (arc_mouse() is then an unbounded virtual position: use only its changes). It is
   let go while the window is out of focus and taken back on return; on the web the browser needs a click first, so
   arc_mouse_captured() says whether it is in effect. Call it every frame with what the game wants (cheap).

```c
ARC_API void arc_mouse_capture(bool on);
ARC_API bool arc_mouse_captured(void);
ARC_API float arc_mouse_wheel(void);    // wheel steps this tick
ARC_API arc_key arc_key_from_name(const char *name);    // "A", "SPACE", "LEFT", "MOUSE_LEFT", "PAD_A"...
ARC_API const char *arc_key_name(arc_key k);
ARC_API arc_key arc_key_any_pressed(void);    // a keyboard key that went down this tick (for rebinding), else NONE
```

A press and release between two ticks still counts: the key is down for one tick.

```c
ARC_API const char *arc_text_input(void);    // UTF-8 text typed this tick ("" if none); for name entry, chat
```

Gamepad: buttons are arc_keys (ARC_PAD_*); sticks and triggers are axes. Sticks -1..1 (+Y down), triggers 0..1.

```c
typedef enum { ARC_AXIS_LX, ARC_AXIS_LY, ARC_AXIS_RX, ARC_AXIS_RY, ARC_AXIS_LT, ARC_AXIS_RT, ARC_AXIS_COUNT } arc_axis;
ARC_API float arc_pad_axis(arc_axis a);
ARC_API bool arc_pad_connected(void);
```

0.10.5 several pads (couch multiplayer): pad N is a slot that keeps its number while the pad stays connected
   (Windows: the XInput player number, its light; Linux: the order pads were found; web: the browser's gamepad index).
   The ARC_PAD_* keys and arc_pad_axis above read the first connected pad, as before; these read one pad each.
   "Pad N drives player N": see docs/split-screen.md. Recorded in replays (format 3; format 2 replays still play).
   Agents: `pad N connect|disconnect`, `pad N press|release BUTTON`, `pad N axis LX 0.5`.

```c
#define ARC_MAX_PADS 4
ARC_API int arc_pad_count(void);    // pads connected now
ARC_API bool arc_pad_is_connected(int pad);
ARC_API bool arc_pad_down(int pad, arc_key button);    // button: ARC_PAD_A .. ARC_PAD_GUIDE
ARC_API bool arc_pad_pressed(int pad, arc_key button);
ARC_API bool arc_pad_released(int pad, arc_key button);
ARC_API float arc_pad_axis_of(int pad, arc_axis a);
ARC_API int arc_pad_first(void);    // the pad the ARC_PAD_* keys and arc_pad_axis read (-1: none)
ARC_API int arc_pad_any_pressed(arc_key *button);    // a pad on which a button went down this tick ("press A to join"), -1: none
```

Touch (web): the first finger drives the mouse and MOUSE_LEFT. True while the last pointer input was a finger
   (no hover between taps), false again once a real mouse moves.

```c
ARC_API bool arc_touch_mode(void);
```

Multi-touch (0.10.11): every finger, read in tick() like the keys, recorded and replayed like other input (web and
   Windows touch screens; headless: `touch down ID X Y` / `touch move ID X Y` / `touch up ID`). id is small (0, 1, ...)
   and stays while the finger is down; a lifted finger's id is reused. x, y are window pixels (as the mouse, before
   viewports); start_x, start_y where it went down. A finger lifted this tick is still listed for this tick with
   released set (at its last place), so `pressed` / `released` work like arc_key_pressed / _released, and a tap shorter
   than a tick is still seen for one tick. The first finger still drives the mouse and MOUSE_LEFT. arc_ui buttons
   answer any finger, so a thumb steering elsewhere doesn't block a tap. dump.input lists them.

```c
#define ARC_TOUCH_MAX 10
typedef struct { int id; float x, y; float start_x, start_y; bool pressed; bool released; } arc_touch;
ARC_API int arc_touch_count(void);    // fingers listed this tick (down, or lifted this tick)
ARC_API const arc_touch *arc_touch_get(int i);    // NULL past the end; ordered by when they went down
ARC_API const arc_touch *arc_touch_find(int id);    // NULL: no such finger this tick
```

## Tunable variables (agents can list/set them: `vars`, `set name value`)


Register a float the game reads every tick. Returns the pointer you pass in, so:
    static float speed; arc_var("player.speed", &speed, 6.0f);   (call in init)

```c
ARC_API float *arc_var(const char *name, float *ptr, float initial);
ARC_API bool arc_var_set(const char *name, float value);
ARC_API float arc_var_get(const char *name);
```

## JSON writer (for dump() and tools)


Inside dump(), you are already inside an object: write members with the *_k functions.
  arc_json_num_k(j, "score", score);
  arc_json_arr_k(j, "enemies"); for (...) { arc_json_obj(j); arc_json_num_k(j,"x",e->x); arc_json_end(j); } arc_json_end(j);

```c
ARC_API void arc_json_obj(arc_json *j);    // begin unnamed object (inside array)
ARC_API void arc_json_arr(arc_json *j);
ARC_API void arc_json_obj_k(arc_json *j, const char *key);
ARC_API void arc_json_arr_k(arc_json *j, const char *key);
ARC_API void arc_json_end(arc_json *j);
ARC_API void arc_json_num(arc_json *j, double v);
ARC_API void arc_json_int(arc_json *j, long long v);
ARC_API void arc_json_str(arc_json *j, const char *s);
ARC_API void arc_json_bool(arc_json *j, bool b);
ARC_API void arc_json_num_k(arc_json *j, const char *key, double v);
ARC_API void arc_json_int_k(arc_json *j, const char *key, long long v);
ARC_API void arc_json_str_k(arc_json *j, const char *key, const char *s);
ARC_API void arc_json_bool_k(arc_json *j, const char *key, bool b);
ARC_API void arc_json_v3_k(arc_json *j, const char *key, arc_v3 v);    // [x,y,z]
```

## Graphics


Colours: 0xRRGGBB is opaque. Use arc_rgba() for alpha.
An alpha byte of 0 means opaque (so plain 0xRRGGBB works); arc_rgba never returns alpha 0.

```c
uint32_t arc_rgba(uint32_t rgb, float a);
#define ARC_WHITE 0xFFFFFF
#define ARC_BLACK 0x000000
typedef struct { uint32_t id; } arc_mesh;    // 0 = none
typedef struct { uint32_t id; } arc_texture;    // 0 = none (white)
typedef struct { float px, py, pz; float nx, ny, nz; uint32_t color; float u, v; } arc_vertex;
uint32_t arc_vcol(uint32_t rgb);
```

Upload a mesh to the GPU once; draw it thousands of times per frame (instanced).

```c
ARC_API arc_mesh arc_mesh_create(const arc_vertex *v, int vcount, const uint32_t *idx, int icount);
ARC_API void arc_mesh_destroy(arc_mesh m);    // also destroys a texture the mesh file brought with it
ARC_API int arc_mesh_triangles(arc_mesh m);
ARC_API int arc_mesh_vertex_count(arc_mesh m);
```

Skinned mesh: each vertex follows up to 4 joints of an arc_anim skeleton (weights out of 255, summing to 255).

```c
typedef struct { uint8_t joint[4]; uint8_t weight[4]; } arc_skin_vertex;
ARC_API arc_mesh arc_mesh_create_skinned(const arc_vertex *v, int vcount, const uint32_t *idx, int icount, const arc_skin_vertex *skin);
ARC_API bool arc_mesh_skinned(arc_mesh m);
```

Replace a mesh's vertices (same count; indices stay). For shapes rebuilt on the CPU: water, cloth,
   procedural terrain. Update before drawing the mesh in a frame: a frame draws every instance with the
   vertices the mesh has when the frame ends.

```c
ARC_API void arc_mesh_update(arc_mesh m, const arc_vertex *v, int vcount);
```

A texture drawn on the mesh (with its vertex colours multiplied in). The texture is not owned by the
   mesh unless it came from the mesh file. Pixels with alpha below 0.5 are cut out (leaves, fences).

```c
ARC_API void arc_mesh_set_texture(arc_mesh m, arc_texture t);
ARC_API arc_texture arc_mesh_texture(arc_mesh m);
```

Materials (0.6): extra maps and surface settings for a mesh. .arcm files made by mesh2arcm.py carry them from glTF
   (normal, metallic-roughness, occlusion and emissive maps). A mesh without one is shaded exactly as before.
   - normal: tangent-space normal map (OpenGL convention: green = up the texture), sharing the colour texture's u, v.
     No tangents are stored: they are worked out per pixel from positions and texture coordinates.
   - pbr: lit as metal or non-metal with sharp or broad highlights (GGX) and reflections of the environment
     (arc_set_environment, or the sky). metallic 0..1 and roughness 0..1 multiply the map's blue and green.
   - orm: glTF's packed map: R = ambient occlusion (strength `occlusion`), G = roughness, B = metallic.
   - emissive: glows with emissive_rgb (HDR: above 1 blooms) times the emissive map's colour (no map: everywhere).
   The mesh owns maps loaded with it; maps the game sets stay the game's.

```c
typedef struct { arc_texture normal, orm, emissive; float normal_scale; bool pbr; float metallic, roughness; float occlusion; float emissive_rgb[3]; arc_texture detail; float detail_scale, detail_strength; int detail_tiling, base_tiling; float detail_rotate, base_rotate; float detail_contrast, base_contrast; arc_texture detail_mask; float mask_scale, mask_threshold, mask_softness; float base_scale; arc_texture layers; float layer_scale[4]; float splat_sharpness; } arc_material;
enum { ARC_TILE_REPEAT = 0, ARC_TILE_HEX = 1 };
ARC_API void arc_mesh_set_material(arc_mesh m, const arc_material *mat);    // NULL = none (plain shading)
ARC_API bool arc_mesh_material(arc_mesh m, arc_material *out);    // false if the mesh has none
```

Procedural meshes — handy for prototyping with no art. Unit sized, centred on the origin.

```c
ARC_API arc_mesh arc_mesh_cube(uint32_t rgb);    // 1x1x1
ARC_API arc_mesh arc_mesh_sphere(uint32_t rgb, int segments);    // diameter 1
ARC_API arc_mesh arc_mesh_cylinder(uint32_t rgb, int segments);    // diameter 1, height 1
ARC_API arc_mesh arc_mesh_cone(uint32_t rgb, int segments);    // base diameter 1, height 1, tip +Y
ARC_API arc_mesh arc_mesh_plane(uint32_t rgb, int tiles, uint32_t rgb2);    // 1x1 in XZ, checkerboard of tiles x tiles
```

Mesh builder: procedural shapes with the triangle winding and normals worked out for you.
    arc_mesh_builder *b = arc_mb_begin();
    arc_mb_color(b, 0xC8A060); arc_mb_box(b, lo, hi);
    arc_v2 vase[] = { {0.3f,0}, {0.5f,0.4f}, {0.2f,1} }; arc_mb_lathe(b, vase, 3, 24, true);
    arc_mesh m = arc_mb_finish(b, 0, 0);      // flat shading; 60 = smooth across edges gentler than 60 degrees
Triangles and quads are counter-clockwise seen from their front. arc_mb_transform places what follows (a mirroring
transform keeps faces pointing out). Lathe profiles are (radius, height) going upward, spun round +Y; extrude
outlines are (x, z) in either direction. Texture coordinates come along (lathe: u around, v down the profile).

```c
typedef struct arc_mesh_builder arc_mesh_builder;
enum { ARC_MB_OUTWARD = 1 };    // finish: turn every triangle to face away from the shape's centre (closed solids)
ARC_API arc_mesh_builder *arc_mb_begin(void);
ARC_API void arc_mb_color(arc_mesh_builder *b, uint32_t rgb);    // for the triangles that follow
ARC_API void arc_mb_transform(arc_mesh_builder *b, arc_m4 m);
ARC_API void arc_mb_reset_transform(arc_mesh_builder *b);
ARC_API void arc_mb_tri(arc_mesh_builder *b, arc_v3 a, arc_v3 c, arc_v3 d);
ARC_API void arc_mb_quad(arc_mesh_builder *b, arc_v3 a, arc_v3 c, arc_v3 d, arc_v3 e);
ARC_API void arc_mb_box(arc_mesh_builder *b, arc_v3 lo, arc_v3 hi);
ARC_API void arc_mb_lathe(arc_mesh_builder *b, const arc_v2 *profile, int count, int segments, bool caps);
ARC_API void arc_mb_extrude(arc_mesh_builder *b, const arc_v2 *outline, int count, float y0, float y1, bool caps);
ARC_API int arc_mb_vertex_count(const arc_mesh_builder *b);
ARC_API arc_mesh arc_mb_finish(arc_mesh_builder *b, float smooth_degrees, int flags);    // frees the builder
```

Load an .arcm file (see tools/mesh2arcm.py, which converts GLB/glTF/OBJ). Returns {0} on failure.
   Version 2 files can carry a texture (loaded smooth, mipmapped and repeating) and a skin.

```c
ARC_API arc_mesh arc_mesh_load(const char *path);    // also reads AXE .axm (AXM2) files
ARC_API arc_mesh arc_mesh_from_memory(const uint8_t *data, size_t size);
```

Draw both sides of every triangle (for models with mixed winding, open shells, foliage).
   .axm files load double-sided, matching how AXE drew them.

```c
ARC_API void arc_mesh_set_double_sided(arc_mesh m, bool on);
```

bounding box of a mesh in model space

```c
ARC_API void arc_mesh_bounds(arc_mesh m, arc_v3 *lo, arc_v3 *hi);
```

Textures (RGBA8). Used by 2D sprites, text and meshes.

```c
ARC_API arc_texture arc_texture_create(int w, int h, const uint8_t *rgba, bool smooth);
ARC_API void arc_texture_update(arc_texture t, const uint8_t *rgba);
ARC_API arc_texture arc_texture_load(const char *path);    // .png (non-interlaced 8-bit), .qoi or baseline .jpg; sharp pixels
```

Options for textures, mostly for meshes: SMOOTH = bilinear filtering; REPEAT = texture coordinates outside
   0..1 tile (ground, walls); MIPMAPS = smaller copies for distant or slanted surfaces (no shimmer).

```c
enum { ARC_TEX_SMOOTH = 1, ARC_TEX_REPEAT = 2, ARC_TEX_MIPMAPS = 4, ARC_TEX_MESH = 7 };
```

0.8.2: the largest side of a mipmapped texture (every mesh texture and material map, and ARC_TEX_MIPMAPS loads)
   when loaded from now on; 0 = no cap (default). Trades 3D texture detail for start time, upload and GPU memory without
   re-converting assets. Compressed .arct files skip their bigger levels (free); PNG/JPEG are halved after decoding (the
   decode itself costs the same). 2D textures, fonts and render targets are never capped. Also --texture-max-size N.

```c
ARC_API void arc_set_texture_max_size(int px);
ARC_API int arc_texture_max_size(void);
ARC_API arc_texture arc_texture_create_ex(int w, int h, const uint8_t *rgba, int flags);
```

0.9.6 a texture array for splatting (arc_material.layers): textures of the same size (RGBA, not compressed or
   still loading), copied in (the originals can be destroyed). Mipmapped, smooth and repeating. Not drawable as a
   texture of its own. {0} on a bad set (logged). arc_texture_destroy frees it. 0.9.9: up to 64 layers (splatting
   uses the first 4), and a mesh can take its colour from one per draw (arc_mesh_set_texture_array): one mesh and one
   draw call for floor tiles in 30 different looks. Each layer uses a texture slot of its own (1024 in all).

```c
#define ARC_TEXTURE_ARRAY_MAX 64
ARC_API arc_texture arc_texture_array(const arc_texture *layers, int n);
ARC_API arc_texture arc_texture_load_ex(const char *path, int flags);
```

Images on the CPU (RGBA8, top row first). Results are malloc'd: free() them.

```c
ARC_API uint8_t *arc_image_load(const char *path, int *w, int *h);
ARC_API uint8_t *arc_image_decode(const uint8_t *data, size_t size, int *w, int *h);    // PNG, QOI or baseline JPEG bytes (or .arct: its first level)
ARC_API bool arc_image_save_png(const char *path, const uint8_t *rgba, int w, int h);    // RGBA if any pixel has alpha < 255, else RGB
ARC_API bool arc_image_save_jpeg(const char *path, const uint8_t *rgba, int w, int h, int quality);    // baseline 4:2:0; quality 1..100 (0 = 90)
ARC_API void arc_texture_size(arc_texture t, int *w, int *h);
```

Grime overlays (0.8.4): turn a dirt photo into a see-through overlay whose alpha says where the dirt is, in ragged
   islands that fade out at the edges, for arc_draw_stamps (or arc_material.detail). Per pixel of the output:
     - a crop of the source (a seeded square, `crop` of its shorter side: the dirt's scale), resized to size x size;
     - dirt amount = how far the pixel's brightness is from the crop's mean, over 2 standard deviations (0..1);
       darker than the mean is grime (colour times `darken`), lighter is a scratch (kept pale);
     - breakup mask = ramp((fbm - threshold) / softness) of value noise `mask_cells` across, OR-ed (max) with
       `second` times a second, unrelated mask;
     - edge fade = 1 inside, smoothstep to 0 at the circle that touches the edges, over `edge` of the radius.
   alpha = amount x mask x fade. tools/grime.py makes the same files offline (same numbers, give or take a level).

```c
typedef struct { int size; float crop; float threshold; float softness; float mask_cells; int octaves; float second; float edge; float darken; uint32_t seed; } arc_breakup;
ARC_API uint8_t *arc_image_breakup(const uint8_t *rgba, int w, int h, const arc_breakup *b);    // size x size RGBA, free() it; NULL b = defaults
ARC_API arc_texture arc_texture_breakup(const char *path, const arc_breakup *b);    // loads the image, makes the overlay, uploads it (mipmapped, smooth, repeating)
```

0.9.9 image operations, in place on RGBA pixels (from arc_image_load or arc_image_decode), to make variants at load time
   instead of offline (then arc_texture_create). Alpha is kept. Deterministic: the same pixels on every platform.
   - multiply: colour = mix(colour, colour x rgb, amount) (amount 0..1; rgb 0x808080 at 1 halves it).
   - overlay: `src` (sw x sh, its alpha x opacity is how much shows) blended onto the image with `mode`, stretched
     over the whole image (bilinear), or with `tile` repeated at its own size from the top-left.
   - noise: brightness x (1 + amount x (2n - 1)), n = fbm value noise with `cells` features across (whole numbers
     wrap, so a texture that tiles still tiles), `octaves` (0 = 4).
   - char: burnt, as arc_mesh_style.charred but baked: darkened, soot mottling and a few bright scratches (amount
     0..1); what tools/char_glb.py-style scripts did offline.

```c
enum { ARC_IMAGE_NORMAL = 0, ARC_IMAGE_MULTIPLY, ARC_IMAGE_SCREEN, ARC_IMAGE_OVERLAY, ARC_IMAGE_ADD };
ARC_API void arc_image_multiply(uint8_t *rgba, int w, int h, uint32_t rgb, float amount);
ARC_API void arc_image_overlay(uint8_t *rgba, int w, int h, const uint8_t *src, int sw, int sh, int mode, float opacity, bool tile);
ARC_API void arc_image_noise(uint8_t *rgba, int w, int h, float cells, int octaves, float amount, uint32_t seed);
ARC_API void arc_image_char(uint8_t *rgba, int w, int h, float amount, uint32_t seed);
```

Compressed textures (0.7.1). .arct files made by tools/texcompress.py (and the textures inside .arcm meshes made with
   mesh2arcm.py --compress, or recompressed with texcompress.py --arcm) hold GPU block formats with their mip levels:
   BC1 (RGB, or RGB with cut-out alpha; 4 bits a texel), BC3 (RGBA; 8 bits), BC5 (two channels, for normal maps: Z is
   rebuilt; 8 bits), BC7 (RGBA, best quality; 8 bits). The usual loaders take them (arc_texture_load(_ex), arc_mesh_load,
   packs, hot reload) and send them to the GPU as they are: 4-8 times less GPU memory than RGBA8 and no image decoding at
   load. Where the GPU cannot take a format (and always in the software renderer, or with --no-compressed-textures)
   ARC decodes them to RGBA8 at load instead, so they draw the same everywhere. A texture's mip levels are used only
   with ARC_TEX_MIPMAPS (mesh textures have it). arc_texture_update on a compressed texture turns it into RGBA8.

```c
enum { ARC_TEXFMT_RGBA8 = 0, ARC_TEXFMT_BC1 = 1, ARC_TEXFMT_BC3 = 3, ARC_TEXFMT_BC4 = 4, ARC_TEXFMT_BC5 = 5, ARC_TEXFMT_BC7 = 7 };
ARC_API arc_texture arc_texture_from_memory(const uint8_t *data, size_t size, int flags);    // PNG, QOI, JPEG or .arct bytes
ARC_API int arc_texture_format(arc_texture t);    // as stored on the GPU: ARC_TEXFMT_BC* if uploaded compressed
ARC_API bool arc_texture_format_supported(int fmt);    // this GPU takes fmt as it is (never in the software renderer)
```

3D camera for this frame. Call before arc_draw_mesh.

```c
typedef struct { arc_v3 eye, target, up; float fov_y; float ortho_height; float znear, zfar; } arc_camera;
```

0.7.2 reversed-Z (arc_app.depth_mode, chosen at start). STANDARD: the classic depth buffer (near 0, far 1; 24-bit
   on the GPU), which runs out of precision far away: two surfaces a few centimetres apart flicker (z-fight) at a few
   hundred metres with a small znear. REVERSED: near 1, far 0 in a 32-bit float depth buffer: the float's precision
   near 0 cancels the projection's loss with distance, so surfaces 1 cm apart stay apart at 2 km with znear 0.05.
   REVERSED_INFINITE: the same with no far clip plane for perspective cameras (zfar still bounds arc_sphere_visible
   and arc_aabb_visible). The picture is otherwise the same: every depth feature (ARC_DEPTH_* styles, x-ray,
   depth_pull, line depth push, soft particles, ssao, decals, sky, render targets) follows; shadow maps keep their own
   standard depth. Where the GPU can't give a 0..1 clip range (WebGL2 without EXT_clip_control, GL without 4.5 /
   ARB_clip_control, D3D11 under Wine) ARC still draws reversed with float depth, but the gain is smaller (about 10x
   instead of 1000x; ARC logs it). arc_view_proj, arc_camera_view_proj and the screen helpers keep the standard
   matrix in every mode. The software renderer stores float depth in both modes.

```c
enum { ARC_DEPTH_STANDARD = 0, ARC_DEPTH_REVERSED = 1, ARC_DEPTH_REVERSED_INFINITE = 2 };
ARC_API int arc_depth_mode(void);    // the mode in effect (ARC_DEPTH_STANDARD / _REVERSED / _REVERSED_INFINITE)
ARC_API bool arc_depth_precise(void);    // reversed with a float buffer and a 0..1 clip range: the full precision gain
```

Replace the game's screen camera while drawing (NULL = the game's own again); render targets are untouched.
   Used by the `camera` agent command so screenshot scripts can frame any game.

```c
ARC_API void arc_set_camera_override(const arc_camera *c);
```

Hide all 2D drawing on the screen (HUD, text, UI, FPS overlay) - for page art and trailers. The `hud off` / `hud on`
   agent commands and --no-hud do the same. 2D drawn into render targets is kept (it may be part of the 3D world).

```c
ARC_API void arc_set_hud_hidden(bool hidden);
ARC_API bool arc_hud_hidden(void);
ARC_API void arc_set_camera(const arc_camera *c);
ARC_API arc_m4 arc_view_proj(void);
```

Lighting (per pixel): one directional light + ambient, optional rim light, linear or exponential fog.

```c
ARC_API void arc_set_light(arc_v3 dir_to_light, uint32_t light_rgb, uint32_t ambient_rgb);
ARC_API void arc_set_fog(uint32_t rgb, float start, float end);    // linear; end <= start disables
ARC_API void arc_set_fog_exp(uint32_t rgb, float density);    // exponential-squared: 1 - exp(-(density*d)^2); 0 disables
```

sky: the screen's background becomes a gradient by view direction, from horizon (level) to top (straight up)
   and to below (straight down). Linear in the view ray's height. Use the fog colour as the horizon so distant
   things fade into the sky. Replaces the clear colour for the screen (not render targets).

```c
ARC_API void arc_set_sky(uint32_t top_rgb, uint32_t horizon_rgb);    // below the horizon = horizon
ARC_API void arc_set_sky_ex(uint32_t top_rgb, uint32_t horizon_rgb, uint32_t below_rgb);
ARC_API void arc_sky_off(void);
```

billboards: camera-facing pictures and text placed in the 3D world. They are depth tested (walls in front
   hide them), unlit and see-through, sorted with particles. Sizes are world units, or screen pixels with
   ARC_BB_PIXELS (same size at any distance). pos is the centre, or the bottom middle with ARC_BB_BOTTOM.
   ARC_BB_UPRIGHT turns only around the vertical (trees, signs, people); ARC_BB_ADDITIVE adds light (glows).

```c
enum { ARC_BB_PIXELS = 1, ARC_BB_BOTTOM = 2, ARC_BB_UPRIGHT = 4, ARC_BB_ADDITIVE = 8 };
ARC_API void arc_draw_billboard(arc_texture tex, arc_v3 pos, float w, float h, uint32_t tint, int flags);    // tex {0} = a soft round dot
ARC_API void arc_draw_billboard_ex(arc_texture tex, arc_v3 pos, float w, float h, uint32_t tint, float alpha, float glow, int flags);
```

text: height = one line (world units, or pixels with ARC_BB_PIXELS). bg_argb: 0 = none, else a box behind it
   (0xE0FFFFFF = a white speech bubble). Lines split at '\n'. Each different text becomes a small cached texture.

```c
ARC_API void arc_draw_billboard_text(arc_v3 pos, float height, uint32_t rgb, uint32_t bg_argb, int flags, const char *text);
```

Rim light: brightens edges that face away from the camera, so models stand out from the ground.
   power shapes the rim (2 = wide, 4 = thin); rgb 0 turns it off.

```c
ARC_API void arc_set_rim(uint32_t rgb, float power);
```

What shiny (pbr) materials reflect: a gradient from straight up (top) through the horizon to straight down.
   Default: the sky when it is on, else a dim version of the ambient and light colours. intensity 0 turns
   reflections off; arc_set_environment(0, 0, 0, -1) goes back to the default.

```c
ARC_API void arc_set_environment(uint32_t top_rgb, uint32_t horizon_rgb, uint32_t below_rgb, float intensity);
```

Point and spot lights (0.6). Added each frame like drawing: call them from draw() (they light the pass being
   drawn and every pass after it this frame). 0.9.5: up to 256 a frame (tiled: each pixel only pays for the lights
   that reach it; up to 64 may overlap one 16th x 9th of the screen, the strongest near the camera winning there),
   and up to 4 of them may cast shadows (the _ex forms with ARC_LIGHT_SHADOW). intensity multiplies rgb (above 1 for bright lights with post bloom); the light fades
   smoothly to nothing at `range`. Spots point along `dir`, full strength inside `inner` and none outside `outer`
   (half-angles in radians, e.g. 0.3 and 0.5). Unlit meshes ignore them.

```c
#define ARC_MAX_LIGHTS 256    // per frame (0.9.5; was 16 per pass)
#define ARC_MAX_SHADOW_LIGHTS 4    // lights casting shadows per frame
ARC_API void arc_light_point(arc_v3 pos, uint32_t rgb, float intensity, float range);
ARC_API void arc_light_spot(arc_v3 pos, arc_v3 dir, uint32_t rgb, float intensity, float range, float inner, float outer);
ARC_API int arc_light_count(void);    // lights added so far this frame
```

0.9.5: the same with flags. ARC_LIGHT_SHADOW: solid meshes (those that cast sun shadows) cast shadows from this
   light, in every pass after it except mirror reflections. The first 4 a frame get them (the rest light without
   shadows; one log line). A point light renders the scene 6 times per pass, a spot once: keep shadowed lights few,
   and ranges short. Spot shadows reach at most 72 degrees from the spot's direction.

```c
#define ARC_LIGHT_SHADOW 1
ARC_API void arc_light_point_ex(arc_v3 pos, uint32_t rgb, float intensity, float range, int flags);
ARC_API void arc_light_spot_ex(arc_v3 pos, arc_v3 dir, uint32_t rgb, float intensity, float range, float inner, float outer, int flags);
ARC_API void arc_set_light_shadow_resolution(int res);    // each face's size: 128, 256, 512 (default) or 1024
ARC_API int arc_light_shadow_count(void);    // shadowed lights so far this frame
```

Shadows from the directional light: solid meshes cast them onto everything lit, inside a box of `radius`
   around `center` (usually what the camera looks at; the box follows it without shimmering). resolution: the
   shadow map's size (2048 default; 0 = default). radius 0 turns shadows off. strength: how much of the direct
   light a shadow takes away (1 = all; 0.6 keeps some). Meshes can opt out of casting (foliage, effects).

```c
ARC_API void arc_set_shadows(arc_v3 center, float radius, int resolution);
ARC_API void arc_set_shadow_strength(float strength);
```

0.6: cascades (2 or 3; 1 = the one box above): the shadow area follows the screen camera in slices, sharp near it
   and reaching `distance` away (0 = 4 x radius); arc_set_shadows' center is then not used and resolution is per
   slice (1024 is plenty; they share a 2x2 atlas). Perspective cameras only. softness 0..1 widens shadow edges
   (0 = the classic 3x3 filter).

```c
ARC_API void arc_set_shadow_cascades(int count, float distance);
ARC_API void arc_set_shadow_softness(float softness);
ARC_API void arc_mesh_set_cast_shadow(arc_mesh m, bool on);
```

Planar reflections (0.8.7): polished floors, water, ice. The scene is drawn a second time from the camera mirrored
   in the plane y = `y` (opaque and see-through meshes and models, skinned ones too, lit as usual, no shadows; nothing
   below y + 0.005), into a render target `scale` x the screen (0.1..1, 0 = 0.5) with mip levels. Meshes marked
   reflective sample it at their pixel on the screen, so the screen pass must be the one drawing them:
     colour = mix(colour, reflection, strength x amount x vertex alpha x F),  F = 0.6 + 0.4 (1 - N.V)^5 (more at
     grazing angles); the reflection blurred by the material's roughness (mip level 6 x roughness; plain materials
     are sharp) and nudged by its normal map. Vertex alpha is the per-cell mask (opaque meshes stay opaque).
   strength 0 turns it off: no extra pass, the frame exactly as without it. One plane, for the first screen pass
   that draws a reflective mesh. Reflective meshes and those set not reflected are left out of the mirror pass;
   immediate triangles, lines, 2D, decals' and stamps' quads near the floor are not mirrored (they are below
   y + 0.005 or not meshes). Static batches are culled to the main camera, so things just off screen may be
   missing from the reflection. Costs one more scene pass at `scale`; WebGL2 too.

```c
ARC_API void arc_reflection_plane(float y, float strength, float scale);
ARC_API void arc_mesh_set_reflective(arc_mesh m, float amount);    // 0..1 (0 = not reflective)
ARC_API void arc_mesh_set_reflected(arc_mesh m, bool on);    // false: left out of the mirror pass (default true)
```

Post-processing of the 3D picture (2D drawing stays untouched on top). Zero means "off/default" everywhere:
   bloom makes bright parts glow (emissive, glowing particles, light above bloom_threshold); tonemap rolls bright
   colours off smoothly instead of clipping them. NULL turns it all off. Kept until changed.

```c
typedef struct { float bloom; float bloom_threshold; float bloom_radius; float exposure; bool tonemap; float desaturate; float contrast; float vignette; float tilt_shift; float tilt_focus; float tilt_width; float ssao; float ssao_radius; bool fxaa; arc_texture lut; float lut_strength; float scanlines; float crt_curve; float pixelate; float aberration; } arc_post;
```

A colour-grading table for arc_post.lut: fn maps each colour (0..1, in place) to its graded colour, sampled on a
   size^3 grid (16 or 32; 0 = 16). Or load one from a PNG made by tools/make_lut.py (it has presets).

```c
ARC_API arc_texture arc_lut_build(void (*fn)(float rgb[3], void *user), void *user, int size);
ARC_API void arc_set_post(const arc_post *p);
```

0.8.2: post effects for one render target, for 3D drawn into a target that is then placed on the screen (a view
   inside a frame of 2D). They run when the target's last pass of the frame has drawn its 3D; the 2D drawn into the
   target goes on top afterwards, untouched, as on the screen. NULL (or all zero) = none, as before. A target with
   post comes out opaque. Up to 8 targets at once (false and an ARC error line past that). Kept until changed or the
   target is destroyed. Vignette, scanlines, CRT and the rest follow the target's own size.

```c
ARC_API bool arc_target_set_post(arc_texture target, const arc_post *p);
```

0.8.2: shadow cascades (arc_set_shadow_cascades) for passes into this target, following the camera set while
   drawing into it (perspective only). Off by default: target passes use the single arc_set_shadows box.

```c
ARC_API void arc_target_set_shadow_cascades(arc_texture target, bool on);
```

world point -> screen pixels; returns false if behind the camera

```c
ARC_API bool arc_world_to_screen(arc_v3 p, arc_v2 *out);
```

screen pixel -> world ray

```c
ARC_API void arc_screen_ray(arc_v2 px, arc_v3 *origin, arc_v3 *dir);
```

The same two, for any camera and view size, with no drawing state: use these in tick() (headless runs never
   call draw(), so the two above would see an old camera there and replays would aim differently).

```c
ARC_API bool arc_camera_to_screen(const arc_camera *c, float view_w, float view_h, arc_v3 p, arc_v2 *out);
ARC_API void arc_camera_ray(const arc_camera *c, float view_w, float view_h, arc_v2 px, arc_v3 *origin, arc_v3 *dir);
ARC_API arc_m4 arc_camera_view_proj(const arc_camera *c, float view_w, float view_h);
```

0.10.1: the camera moved so the box fills the view: keeps base's view direction, up and field of view (or
   orthographic), centres the box and leaves `margin` (a share of the frame, e.g. 0.05) on each side of the tighter
   axis. aspect = view width / height. For art reviews, previews and line-ups.

```c
ARC_API arc_camera arc_camera_fit(arc_camera base, arc_v3 box_min, arc_v3 box_max, float aspect, float margin);
```

Draw a mesh. tint multiplies vertex colours (0xFFFFFF = unchanged). emissive 0..1 adds glow.
0.9.0 names, shown by the `frame` agent command: a loaded mesh or texture is named after its file (no folder, no
   extension), primitives after their kind ("cube", "sphere", ...); set your own for things made in code.

```c
ARC_API void arc_mesh_set_name(arc_mesh m, const char *name);    // up to 31 characters
ARC_API const char *arc_mesh_name(arc_mesh m);    // "" if none
ARC_API void arc_texture_set_name(arc_texture t, const char *name);
ARC_API const char *arc_texture_name(arc_texture t);
ARC_API void arc_draw_mesh(arc_mesh m, arc_m4 model, uint32_t tint);
ARC_API void arc_draw_mesh_ex(arc_mesh m, arc_m4 model, uint32_t tint, float emissive);
```

Recolour a mesh while keeping its light/dark detail (team colours, ownership, damage):
   colour = lerp(vertex colour, tint * (0.35 + luminance), mix); then lit; whiten 0..1 lifts the result
   toward white (hit flashes, glow). Unlike arc_draw_mesh, tint does not multiply here.
   Mirrored model matrices (negative determinant) are fine with every arc_draw_mesh* call.

```c
ARC_API void arc_draw_mesh_tinted(arc_mesh m, arc_m4 model, uint32_t tint, float mix, float emissive, float whiten);
```

See-through and glowing meshes: drawn after everything solid, sorted back to front, without writing depth.
   alpha 0..1 (1 = solid look, still sorted); additive adds light (sparks, magic, spore clouds). The vertex
   colours' alpha and the texture's alpha multiply in, so a mesh can fade out at its edges (soft shadows, pools).

```c
ARC_API void arc_draw_mesh_alpha(arc_mesh m, arc_m4 model, uint32_t tint, float alpha);
ARC_API void arc_draw_mesh_additive(arc_mesh m, arc_m4 model, uint32_t tint, float intensity);
```

---- skeletal animation --------------------------------------------------------------------------
   A skinned mesh (.arcm from a rigged GLB) bends with a skeleton. Its clips live in an .arca file made from
   the same rig (tools/mesh2arcm.py rig.glb --anim idle.glb jog.glb ...). Each draw picks a pose: blend any
   number of clips by weight (crossfades, run/walk mixes). Sampling is pure and deterministic: the game owns
   the clock, so a pose can be computed in tick() (to aim with a hand) or in draw().

```c
#define ARC_MAX_JOINTS 128
typedef struct { uint32_t id; } arc_anim;    // a skeleton and its clips
typedef struct { int clip; float time; float weight; bool loop; } arc_anim_layer;    // time in seconds
typedef struct { int count; arc_m4 joint[ARC_MAX_JOINTS]; } arc_pose;    // joints in model space
ARC_API arc_anim arc_anim_load(const char *path);    // .arca
ARC_API arc_anim arc_anim_from_memory(const uint8_t *data, size_t size);
ARC_API void arc_anim_destroy(arc_anim a);
ARC_API int arc_anim_clip_count(arc_anim a);
ARC_API int arc_anim_find(arc_anim a, const char *clip_name);    // -1 if missing
ARC_API const char *arc_anim_clip_name(arc_anim a, int clip);
ARC_API float arc_anim_length(arc_anim a, int clip);    // seconds
ARC_API int arc_anim_joint_count(arc_anim a);
ARC_API int arc_anim_joint(arc_anim a, const char *joint_name);    // -1 if missing
ARC_API const char *arc_anim_joint_name(arc_anim a, int joint);
```

Blend clips into a pose. Weights are normalised; no layers (or all weights 0) gives the rest pose.

```c
ARC_API void arc_anim_pose(arc_anim a, const arc_anim_layer *layers, int count, arc_pose *out);
```

where a joint is (attach a weapon to a hand: model * arc_pose_joint(&p, hand))

```c
arc_m4 arc_pose_joint(const arc_pose *p, int joint);
```

Draw a skinned mesh in a pose. A skinned mesh drawn with plain arc_draw_mesh shows its bind pose.

```c
ARC_API void arc_draw_mesh_posed(arc_mesh m, arc_m4 model, uint32_t tint, arc_anim a, const arc_pose *pose);
```

---- animation state machines, events, IK (0.9.3) --------------------------------------------------------------
   A graph says which clips play when: states (one clip, or a 1D blend of clips by a parameter, like walk..run by
   speed), transitions between them with conditions on parameters, crossfade times and exit times, and events
   marked on clips (footsteps, the frame a sword hits). Build the graph once; each character keeps an arc_animator:
   plain data with no pointers, so it goes in your game state and save_state/load_state copy it.
     G.g = arc_anim_graph_create(rig);
     int speed = arc_anim_graph_param(G.g, "speed", 0), jump = arc_anim_graph_param(G.g, "jump", ARC_ANIM_TRIGGER);
     int idle = arc_anim_graph_state(G.g, "idle", arc_anim_find(rig, "idle"), 1, true);
     int move = arc_anim_graph_blend(G.g, "move", speed, clips, points, 2, true);     // walk at 1.5, run at 5
     int air  = arc_anim_graph_state(G.g, "jump", arc_anim_find(rig, "jump"), 1, false);
     arc_anim_graph_when(G.g, arc_anim_graph_transition(G.g, idle, move, 0.2f, -1), speed, ARC_ANIM_GT, 0.1f);
     arc_anim_graph_when(G.g, arc_anim_graph_transition(G.g, move, idle, 0.2f, -1), speed, ARC_ANIM_LE, 0.1f);
     arc_anim_graph_when(G.g, arc_anim_graph_transition(G.g, ARC_ANIM_ANY, air, 0.1f, -1), jump, ARC_ANIM_SET, 0);
     arc_anim_graph_transition(G.g, air, idle, 0.15f, 1.0f);                            // when the jump clip ends
     arc_anim_graph_event(G.g, arc_anim_find(rig, "run"), 0.3f, "step");
   tick:  arc_animator_set(&G.anim, speed, v); arc_animator_update(&G.anim, arc_dt());
          for (int i = 0; i < G.anim.nevents; i++) if (G.anim.events[i] == G.step_event) play the footstep;
   draw:  arc_pose p; arc_animator_pose(&G.anim, &p); arc_draw_mesh_posed(mesh, model, 0xFFFFFF, rig, &p);

```c
typedef struct { uint32_t id; } arc_anim_graph;
#define ARC_ANIM_MAX_PARAMS 16
#define ARC_ANIM_MAX_EVENTS 8    // events one update can report
#define ARC_ANIM_ANY (-1)    // a transition from any state
enum { ARC_ANIM_FLOAT = 0, ARC_ANIM_TRIGGER = 1 };    // a trigger is set by arc_animator_trigger and cleared when a transition uses it
typedef enum { ARC_ANIM_GT, ARC_ANIM_GE, ARC_ANIM_LT, ARC_ANIM_LE, ARC_ANIM_EQ, ARC_ANIM_NE, ARC_ANIM_SET } arc_anim_op;
ARC_API arc_anim_graph arc_anim_graph_create(arc_anim skeleton);
ARC_API void arc_anim_graph_destroy(arc_anim_graph g);
ARC_API int arc_anim_graph_param(arc_anim_graph g, const char *name, int kind);    // index, or the existing one's
ARC_API int arc_anim_graph_find_param(arc_anim_graph g, const char *name);    // -1 if none
```

a state playing one clip at `speed` (1 = as made); returns the state's index (-1 on error)

```c
ARC_API int arc_anim_graph_state(arc_anim_graph g, const char *name, int clip, float speed, bool loop);
```

a 1D blend: clips[i] plays fully at parameter value points[i] (points rising); between two points the two clips
   mix, and their times are kept in step (as fractions of each clip), so a walk and a run of different lengths keep
   their feet together. Up to 8 clips.

```c
ARC_API int arc_anim_graph_blend(arc_anim_graph g, const char *name, int param, const int *clips, const float *points, int count, bool loop);
ARC_API int arc_anim_graph_find_state(arc_anim_graph g, const char *name);
ARC_API const char *arc_anim_graph_state_name(arc_anim_graph g, int state);
```

a transition from `from` (or ARC_ANIM_ANY) to `to`, crossfading over `fade` seconds. exit_time >= 0: only once
   the state has played that fraction of its clip (1 = the end; with no conditions, it goes then); < 0: any time.
   Transitions are tried in the order they were made; the first that passes is taken. Returns its index.

```c
ARC_API int arc_anim_graph_transition(arc_anim_graph g, int from, int to, float fade, float exit_time);
```

a condition on a transition (all of a transition's conditions must hold; up to 4)

```c
ARC_API void arc_anim_graph_when(arc_anim_graph g, int transition, int param, arc_anim_op op, float value);
```

an event at `time` seconds into a clip; the same name gives the same id (returned) in every clip

```c
ARC_API int arc_anim_graph_event(arc_anim_graph g, int clip, float time, const char *name);
ARC_API const char *arc_anim_graph_event_name(arc_anim_graph g, int event);
typedef struct { arc_anim_graph graph; int state; float time; float state_time; int prev; float prev_time; float fade, fade_len; float param[ARC_ANIM_MAX_PARAMS]; int nevents; int events[ARC_ANIM_MAX_EVENTS]; int changes; } arc_animator;
ARC_API void arc_animator_init(arc_animator *an, arc_anim_graph g, int start_state);
ARC_API void arc_animator_set(arc_animator *an, int param, float value);
ARC_API bool arc_animator_setf(arc_animator *an, const char *param, float value);    // by name; false if none
ARC_API void arc_animator_trigger(arc_animator *an, int param);
ARC_API void arc_animator_play(arc_animator *an, int state, float fade);    // go now, whatever the transitions say
ARC_API void arc_animator_update(arc_animator *an, float dt);    // in tick
ARC_API void arc_animator_pose(const arc_animator *an, arc_pose *out);    // tick or draw
ARC_API bool arc_animator_fired(const arc_animator *an, int event);    // that event passed in the last update
ARC_API const char *arc_animator_state_name(const arc_animator *an);
```

how far into the current state's clip, 0..1 (blend: the phase)

```c
ARC_API float arc_animator_progress(const arc_animator *an);
```

Two-bone IK: bend the chain ending at `tip` (its parent is the middle joint, the parent's parent the upper one:
   thigh-knee-ankle, shoulder-elbow-wrist) so the tip reaches `target`, with the middle joint pointing toward `pole`
   (in front of a knee, behind an elbow). Points are in the pose's space (model space: for world points, multiply by
   the inverse of the model matrix). weight 0..1 blends from the pose as it was. keep_tip_rotation keeps the tip's
   own orientation (feet stay flat on the ground). Joints below the tip follow. Returns false if the target is out of
   reach (the chain then points straight at it, fully stretched). Call after arc_anim_pose / arc_animator_pose.

```c
ARC_API bool arc_pose_two_bone_ik(arc_anim a, arc_pose *pose, int tip, arc_v3 target, arc_v3 pole, float weight, bool keep_tip_rotation);
```

a joint's position in the pose (model space)

```c
arc_v3 arc_pose_joint_pos(const arc_pose *p, int joint);
```

---- particles ------------------------------------------------------------------------------------
   Describe an effect once (a static arc_fx), then fire bursts or attach emitters. Particles move in tick()
   with ARC's own random generator (seeded from --seed), so replays and tests stay exact and arc_rand_* is not
   disturbed. Call arc_draw_particles() in draw() (after setting the camera) to show them: camera-facing
   quads, unlit, sorted with the other see-through meshes. Zero means "default" for every field.

```c
typedef struct arc_fx { float life, life_var; float speed, speed_var; float spread; float radius; arc_v3 gravity; float drag; arc_v3 inherit; float size, size_end, size_var; uint32_t color, color_end; float alpha, alpha_end; float fade_in; float glow; bool additive; float stretch; float spin, spin_var; float bounce; float floor_y; float rate; int burst; arc_texture texture; int frames_x, frames_y; float fps; bool frame_random; float soft; const struct arc_fx *on_death; int on_death_count; const struct arc_fx *on_bounce; int on_bounce_count; bool (*solid)(float x, float y, float z, void *user); void *solid_user; } arc_fx;
ARC_API void arc_fx_burst(const arc_fx *fx, arc_v3 pos, arc_v3 dir, int count);    // count 0 = fx->burst
```

0.10.14 floating text: score popups, damage numbers, "+1 UP". It pops up at a world point, rises and shrinks away
   (0.85 s); several close together stack into a column. ARC ages and draws them after draw() (looks only, never game
   state), and they show in `frame.texts`. scale 0 = 2 (the built-in font's size)

```c
ARC_API void arc_popup_text(arc_v3 pos, uint32_t rgb, float scale, const char *text);
ARC_API void arc_popup_text_2d(float x, float y, uint32_t rgb, float scale, const char *text);    // screen pixels, for 2D games
ARC_API void arc_popups_clear(void);
ARC_API int arc_popup_count(void);
typedef struct { uint32_t id; } arc_emitter;
ARC_API arc_emitter arc_emitter_create(const arc_fx *fx, arc_v3 pos, arc_v3 dir);    // emits fx->rate per second until destroyed
ARC_API void arc_emitter_move(arc_emitter e, arc_v3 pos, arc_v3 dir);    // every tick for moving things: spawns spread along the path
ARC_API void arc_emitter_set_rate(arc_emitter e, float scale);    // multiplies fx->rate (0 = pause)
ARC_API void arc_emitter_set_active(arc_emitter e, bool on);
ARC_API void arc_emitter_destroy(arc_emitter e);    // stops emitting; live particles finish
ARC_API bool arc_emitter_alive(arc_emitter e);
ARC_API void arc_draw_particles(void);
ARC_API int arc_particle_count(void);
ARC_API void arc_particles_clear(void);
```

Trails (0.7): a camera-facing ribbon through the last positions of a moving thing (blades, rockets, tyre marks
   in the air). The game owns the arc_trail (part of its state; no allocation): push the position every tick,
   draw in draw(). Points older than `life` drop off the tail; width, colour and alpha blend from head (newest)
   to tail by age. Up to 256 trails drawn per frame.

```c
#define ARC_TRAIL_MAX 64
typedef struct { float life; float min_dist; arc_v3 p[ARC_TRAIL_MAX]; float age[ARC_TRAIL_MAX]; int first, count; } arc_trail;
typedef struct { float width, width_end; uint32_t color, color_end; float alpha, alpha_end; float glow; bool additive; arc_texture texture; } arc_trail_style;
ARC_API void arc_trail_push(arc_trail *t, arc_v3 pos);    // in tick: ages points by arc_dt(), drops old ones, adds pos
ARC_API void arc_trail_age(arc_trail *t);    // in tick, nothing to add (the thing is gone): the tail catches up
ARC_API void arc_trail_clear(arc_trail *t);    // teleports: start again
ARC_API void arc_trail_draw(const arc_trail *t, const arc_trail_style *s);    // NULL style = defaults
```

Decals (0.7): flat marks laid on floors and walls (scorch, blast, oil, footprints), lit like the surface and
   never z-fighting it (drawn depth-test-only, pulled toward the camera). A fixed pool of ARC_MAX_DECALS: the
   oldest is replaced when full. They live with the particles (state saves, rewinds); fade out over the last
   quarter of `life` (0 = until replaced). size in world units across, angle (radians) turns it about the normal.
   argb: alpha 0 = opaque. Keep a decal smaller than the surface: a flat quad hangs over edges. Draw them with
   arc_draw_decals() in draw().

```c
#define ARC_MAX_DECALS 256
enum { ARC_DECAL_ADDITIVE = 1 };    // adds light instead of covering (glowing marks, hot metal)
ARC_API void arc_decal_add(arc_texture tex, arc_v3 pos, arc_v3 normal, float size, float angle, uint32_t argb, float life);    // tex {0} = soft round dot
ARC_API void arc_decal_add_ex(arc_texture tex, arc_v3 pos, arc_v3 normal, float w, float h, float angle, uint32_t argb, float life, int flags);
ARC_API void arc_decal_fade_distance(float start, float end);    // fade out between these camera distances (0, 0 = never)
ARC_API void arc_draw_decals(void);
ARC_API int arc_decal_count(void);
ARC_API void arc_decals_clear(void);
```

Stamps (0.8.4): scattered see-through overlays over a surface (dirt, wear, dust), made and drawn by ARC. The
   surface is a grid of cells; each cell gets `per_cell` stamps (a fraction gives some cells one fewer), each with a
   hashed texture, turn, size and offset, so the result never repeats. Stamps should be bigger than a cell and fade at
   their edges (see arc_image_breakup) so the grid never shows; two or more a cell read as patches, one as a pattern.
   density(ud, cx, cz) 0..1 scales each cell's stamps (NULL = 1; 0 draws nothing there), so the game says where it
   is dirtier (by walls, doorways). Lit like the surface, tinted by tint, depth-test-only and pulled toward the
   camera (layers `layer_gap` apart), drawn in the see-through pass before other see-through things (and before
   decals), batched one draw per texture. Call in draw(); everything comes from (seed, cell), so it is the same every
   frame and on every machine.

```c
typedef struct { arc_texture tex[8]; int count; float size_min, size_max; float jitter; float per_cell; float rotate; float strength; float cell; float layer_gap; uint32_t seed; } arc_stamps;
```

Over the floor cells covering [x0, x1) x [z0, z1) (cell (cx, cz) spans cx * cell ..) at height y, facing up.

```c
ARC_API void arc_draw_stamps(const arc_stamps *s, float x0, float z0, float x1, float z1, float y, float (*density)(void *ud, int cx, int cz), void *ud, uint32_t tint);
```

Any flat surface (walls, ramps): cells_u x cells_v cells from origin, one cell = u_step by v_step (world vectors);
   the stamps face cross(v_step, u_step) (a wall at z = 0 seen from +z: u_step = (1, 0, 0), v_step = (0, -1, 0)).
   density gets (cu, cv) from 0. Use a different seed per surface.

```c
ARC_API void arc_draw_stamps_on(const arc_stamps *s, arc_v3 origin, arc_v3 u_step, arc_v3 v_step, int cells_u, int cells_v, float (*density)(void *ud, int cu, int cv), void *ud, uint32_t tint);
```

Every option in one call (the functions above are shortcuts). Zero means "default" for every field.
0.10.1 paint patterns drawn by the shader (MechBattleArc's request), from the mesh's bind-pose (model space)
   position, so they stay put on skinned meshes as they move; every renderer, static and skinned meshes. The pattern's
   colour is applied as paint is: rgb x (0.35 + luminance), so shading and wear survive.
   - STRIPES: bands across the model's front (x, y) plane at `angle` (0 = vertical bands); `cross` adds a second set at
     90 degrees (crosshatch).
   - CHECKER, DOTS: on each face's own plane (the axis its normal points along most), so squares stay square and dots
     round; `angle` turns them on the face.
   - SPLIT: one side of a line through the model's front plane at `angle` (0: x > offset).
   - NOISE: value-noise blobs (camo), `seed` picks the pattern.
   scale: one repeat in model units (0 = 0.25); width: the share in the pattern's colour (0 = 0.5); offset: moves it
   (in repeats; SPLIT: in model units). part_mask (ARC_PART_* bits, 0 = all): only these parts of a skinned mesh, from
   each vertex's strongest joint (names: Head, Spine*, *Arm* / *Hand* / *Shoulder* / *Clavicle*, *Leg* / *Foot* /
UpLeg*, others torso; arc_paint_parts_rule adds the game's own); a static mesh counts as torso. With arc_mesh_style.armour_mask the pattern goes only where paint_mask's red is (trim and
   joints keep tint2 / tint3). Draws with the same pattern still batch together.

```c
enum { ARC_PAINT_NONE = 0, ARC_PAINT_STRIPES, ARC_PAINT_CHECKER, ARC_PAINT_DOTS, ARC_PAINT_SPLIT, ARC_PAINT_NOISE };
enum { ARC_PART_HEAD = 1, ARC_PART_TORSO = 2, ARC_PART_ARMS = 4, ARC_PART_LEGS = 8, ARC_PART_ALL = 15 };
typedef struct { int kind; uint32_t rgb; float scale, width, angle, offset; bool cross; uint32_t seed; uint8_t part_mask; } arc_paint_pattern;
```

0.10.6: joints whose name matches `pattern` (case ignored; '*' any run, '?' one character, e.g. "*Pauldron*") belong
   to `part` (an ARC_PART_* bit; 0 removes the rule). Tried before the built-in names, the latest rule first; up to 32.
   pattern NULL or "" removes them all.

```c
ARC_API void arc_paint_parts_rule(const char *pattern, int part);
typedef struct { uint32_t tint; float recolour; float emissive; float whiten; float transparency; bool additive; arc_anim anim; const arc_pose *pose; float dissolve; uint32_t dissolve_rgb; float dissolve_glow; float dissolve_width; float hologram; float scroll_u, scroll_v; float outline; uint32_t outline_rgb; int depth; float depth_pull; int layer; int order; float xray; uint32_t xray_rgb; float uv_rotate, uv_scale; arc_v2 uv_offset; bool uv_mirror; arc_texture paint_mask; uint32_t tint2; uint32_t tint3; int tex_layer; float darken; float charred; bool no_world_mask; arc_paint_pattern pattern; bool armour_mask; arc_texture emissive_mask; uint32_t glow_rgb[3]; float glow[3]; } arc_mesh_style;
```

0.10.15 a mesh's own glow mask, used when a draw's arc_mesh_style.emissive_mask is {0} (a style's mask replaces it).
   arc_import.py --glow-mask IMG stores one in the .arcm, and arc_mesh_load sets it. {0} removes it. The mesh does not
   own the texture (free it yourself), except one loaded from the file. arc_mesh_glow_mask reads it back.

```c
ARC_API void arc_mesh_set_glow_mask(arc_mesh m, arc_texture mask);
ARC_API arc_texture arc_mesh_glow_mask(arc_mesh m);
```

0.9.9: a mesh takes its colour from a texture array (arc_texture_array) instead of its texture, at the layer each draw
   picks (arc_mesh_style.tex_layer, arc_draw_mesh_layer); {0} back to its own texture. Draws of the mesh with different
   layers stay one batch (one draw call per mesh).

```c
ARC_API void arc_mesh_set_texture_array(arc_mesh m, arc_texture array);
ARC_API void arc_draw_mesh_layer(arc_mesh m, arc_m4 model, uint32_t tint, int layer);
```

0.9.9 a tile grid in one call (Moonbase Raid's request): every cell picks one of `meshes` (its index, flags
   ARC_TILE_EMPTY to skip), a layer of that mesh's texture array, a quarter turn about +y and a tint (0 = white). Cells
   are `cell` wide from `origin` (cell (0, 0) spans origin.x .. + cell, origin.z .. + cell), y = origin.y; each mesh
   is centred on its cell, as built-in shapes are, scaled by `cell` in x and z (scale y yourself in the mesh). One
   draw call per mesh kind, however many cells and layers.

```c
enum { ARC_TILE_EMPTY = 1, ARC_TILE_MIRROR = 2 };
typedef struct { uint8_t mesh, layer, turn, flags; uint32_t tint; } arc_tile_cell;
ARC_API void arc_draw_tiles(const arc_tile_cell *cells, int w, int h, float cell, arc_v3 origin, const arc_mesh *meshes, int mesh_count);
```

0.9.9 fog of war for every draw at once: `mask` is a small texture, one texel per tile (cell (0, 0) at origin x, z,
   `cell` wide); its red channel is how visible the tile is: 255 seen, about 80 remembered, 0 never seen. Everything lit
   or unlit that ARC draws in 3D (meshes, skinned meshes, decals, stamps, particles, markers) is darkened toward dark_rgb
   by it, with soft edges between tiles (softness 0 = hard tile steps, 1 = smooth across a whole tile), the same on
   every renderer. Update the texture (arc_texture_update) when vision changes; outside it the edge texels carry on.
   Leave a mesh out with arc_mesh_style.no_world_mask. {0} turns it off. Lasts until changed.

```c
ARC_API void arc_set_world_mask(arc_texture mask, arc_v3 origin, float cell, uint32_t dark_rgb, float softness);
```

0.9.9 cutaway: meshes marked with arc_mesh_set_cutaway (walls, door frames) lose what is above keep_height within
   `radius` of the line from eye to focus, seen from above, so the units behind them show. mode ARC_CUTAWAY_CLIP cuts
   cleanly; ARC_CUTAWAY_DITHER fades in over 0.6 units of height and the last quarter of the radius. Cut walls show
   a dark cap where they are open, and still cast their full shadows. Usually eye = the camera's eye, focus = what it
   looks at; set it each frame as the camera moves. mode ARC_CUTAWAY_OFF (or radius 0) turns it off.

```c
enum { ARC_CUTAWAY_OFF = 0, ARC_CUTAWAY_CLIP = 1, ARC_CUTAWAY_DITHER = 2 };
ARC_API void arc_set_cutaway(arc_v3 eye, arc_v3 focus, float radius, float keep_height, int mode);
ARC_API void arc_mesh_set_cutaway(arc_mesh m, bool on);
```

depth: DEFAULT tests and (solid meshes) writes; TEST_ONLY tests but never hides what is drawn after it (glows,
   ground effects); ON_TOP ignores depth (markers that show through everything); HIDDEN draws only where something
   nearer already covers it (x-ray). ON_TOP and HIDDEN go in the overlay layer unless a layer is given.

```c
enum { ARC_DEPTH_DEFAULT = 0, ARC_DEPTH_TEST_ONLY = 1, ARC_DEPTH_ON_TOP = 2, ARC_DEPTH_HIDDEN = 3 };
```

layers, in drawing order inside a pass: BACKGROUND (sky domes, backdrops), OPAQUE (solid meshes), then immediate
   triangles and lines, TRANSPARENT (see-through and additive, far to near), OVERLAY (last: markers, x-ray, gizmos).
   AUTO picks OPAQUE or TRANSPARENT from the style (OVERLAY for ON_TOP / HIDDEN depth).

```c
enum { ARC_LAYER_AUTO = 0, ARC_LAYER_BACKGROUND = 1, ARC_LAYER_OPAQUE = 2, ARC_LAYER_TRANSPARENT = 3, ARC_LAYER_OVERLAY = 4 };
ARC_API void arc_draw_mesh_styled(arc_mesh m, arc_m4 model, const arc_mesh_style *style);
```

0.8.8: the posed draw with a whole arc_mesh_style (outline, x-ray, recolour, paint mask, dissolve, ...): the same as
   arc_draw_mesh_styled with st->anim and st->pose set to a and pose. NULL st = plain.

```c
ARC_API void arc_draw_mesh_posed_styled(arc_mesh m, arc_m4 model, arc_anim a, const arc_pose *pose, const arc_mesh_style *st);
```

Immediate geometry: triangles rebuilt every frame (procedural walls, tubes, trails).
   Lit like meshes, drawn double-sided. Use meshes for anything static and repeated.

```c
ARC_API void arc_draw_triangles(const arc_vertex *v, int count);    // count = vertices, multiple of 3
ARC_API void arc_draw_tri(arc_v3 a, arc_v3 b, arc_v3 c, uint32_t rgb);    // flat normal from the winding
ARC_API void arc_draw_quad3(arc_v3 a, arc_v3 b, arc_v3 c, arc_v3 d, arc_v3 normal, uint32_t rgb);    // normal zero = from winding
ARC_API void arc_draw_box(arc_v3 lo, arc_v3 hi, uint32_t rgb);    // solid axis-aligned box (instanced)
```

Quick shapes (use built-in meshes)

```c
ARC_API void arc_draw_cube(arc_v3 center, arc_v3 size, uint32_t rgb);
ARC_API void arc_draw_sphere(arc_v3 center, float radius, uint32_t rgb);
```

3D lines (unlit, drawn after meshes, depth-tested)

```c
ARC_API void arc_draw_line(arc_v3 a, arc_v3 b, uint32_t rgb);
```

Depth offset for lines (0.6.2): lines drawn after this are pushed `fraction` further from the current camera
   along each point's own line of sight (0 = off; 0.3 = 30% further away). Each point lands on the same pixel, so a
   line still looks as if it runs through the models it was placed on, but it sits behind them and never cuts
   across them: guide rings, grids and outlines on busy scenes. Perspective cameras push away from the eye,
   orthographic ones straight back. Set the camera first; it lasts until changed and resets to 0 each frame.

```c
ARC_API void arc_set_line_depth_push(float fraction);
```

Thick and glowing lines (0.10.11). arc_set_line_width(px): 3D lines drawn after it are px pixels wide on the screen (or
   render target), whatever their distance, with a soft one-pixel edge; 1 (the default) is the plain one-pixel line.
   arc_set_line_glow(x): their colour is multiplied by x (1 = as given): above 1 they pass the bloom threshold of
   arc_post's bloom and glow, like emissive meshes (1..64; affects thin lines too). arc_set_line_cap: the ends of thick
   lines, 3D and 2D (square, the 3D default: half the width past each end; round; butt: stop at the end, the 2D
   default). All three stay until changed. Thick lines are depth tested but write no depth, so they never hide each
   other; frame.lines reports the frame's counts and widest width. arc_draw_lines: count points taken in pairs (a
   ship's edges), each moved by model. The limit is 131,072 thin lines and about 9,000 thick ones a frame.

```c
typedef enum { ARC_LINE_CAP_SQUARE = 0, ARC_LINE_CAP_ROUND = 1, ARC_LINE_CAP_BUTT = 2 } arc_line_cap;
ARC_API void arc_set_line_width(float pixels);
ARC_API void arc_set_line_glow(float intensity);
ARC_API void arc_set_line_cap(arc_line_cap cap);
ARC_API float arc_line_width(void);
ARC_API void arc_draw_lines(const arc_v3 *pts, int count, arc_m4 model, uint32_t rgb);
ARC_API void arc_draw_box_lines(arc_v3 lo, arc_v3 hi, uint32_t rgb);
ARC_API void arc_draw_grid(float half_size, float step, uint32_t rgb);
```

2D overlay in screen pixels (origin top-left), drawn after 3D in call order.

```c
ARC_API void arc_draw_rect(float x, float y, float w, float h, uint32_t argb_or_rgb);
ARC_API void arc_draw_rect_lines(float x, float y, float w, float h, uint32_t rgb);
ARC_API void arc_draw_line2d(float x0, float y0, float x1, float y1, float thickness, uint32_t rgb);
```

0.10.13 glowing 2D lines, one quad each: count points taken in pairs; width: the bright core in pixels (soft edges);
   glow: the halo on each side as a multiple of the width (0 = none); halo_alpha: the halo's strength next to the core
   (0..1), fading to nothing at its edge. 6 vertices a segment, against 12 for a halo and a core drawn separately.
   frame.lines.lines2d counts segments. (Core share and halo strength go in 16 steps each.)

```c
ARC_API void arc_draw_lines2d(const arc_v2 *pts, int count, float width, float glow, float halo_alpha, uint32_t rgb);    // 1.5 px and wider: soft edges (0.10.11)
```

Nine-slice: the texture's corners (src_border texels square) drawn dst_border pixels square and unstretched,
   its edges stretched one way, its middle both ways, to fill x, y, w, h. Frames, panels and buttons of any size.
   Corners shrink when the rect is smaller than two borders. (0.6.1)

```c
ARC_API void arc_draw_nine(arc_texture t, float x, float y, float w, float h, float src_border, float dst_border, uint32_t tint);
ARC_API void arc_draw_sprite(arc_texture t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t tint);
```

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

---- 2D extras: blend modes, clipping, triangles, rotated sprites ----

```c
typedef enum { ARC_BLEND_ALPHA = 0, ARC_BLEND_ADD, ARC_BLEND_MULTIPLY, ARC_BLEND_NONE } arc_blend;
ARC_API void arc_set_blend(arc_blend b);    // applies to following 2D draws; ALPHA by default each frame
ARC_API arc_blend arc_get_blend(void);
```

2D offsets (0.10.13): 2D drawn after arc_push_offset2d(x, y, w, h) is moved by (x, y) pixels and, when w and h are
   above 0, clipped to that area (inside any clip already set); pushes nest up to 8 deep and add up; arc_pop_offset2d
   undoes the last. With the safe area: arc_rectf sa = arc_safe_area(); arc_push_offset2d(sa.x, sa.y, sa.w, sa.h); draw
   a layout for a sa.w x sa.h window from (0, 0); arc_pop_offset2d(). frame boxes report where things land. The mouse
   is not moved: subtract arc_offset2d() (the total in force) for hit tests in draw, or the safe area's x, y in tick.
   Offsets left pushed are dropped at the end of the frame (with a hint).

```c
ARC_API void arc_push_offset2d(float x, float y, float w, float h);
ARC_API void arc_pop_offset2d(void);
ARC_API arc_v2 arc_offset2d(void);
ARC_API void arc_set_clip(float x, float y, float w, float h);    // cut off 2D draws outside this rect (pixels of the current target)
ARC_API void arc_clear_clip(void);
typedef struct { float x, y, u, v; uint32_t color; } arc_vertex2d;    // color 0xAARRGGBB, alpha 0 = opaque (see arc_rgba)
ARC_API void arc_draw_triangles2d(arc_texture t, const arc_vertex2d *v, int count);    // t {0} = untextured
```

Draw part of a texture. src rect in texture pixels (negative sw/sh flip it). The point (ox, oy) of the
   destination rect (size dw x dh) is placed at (dx, dy), and the rect rotates around it by `radians`.

```c
ARC_API void arc_draw_texture_ex(arc_texture t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh, float ox, float oy, float radians, uint32_t tint);
ARC_API void arc_texture_set_smooth(arc_texture t, bool smooth);    // bilinear (true) or sharp pixels (false)
ARC_API void arc_texture_destroy(arc_texture t);
```

---- Render targets: draw into a texture, then draw that texture (UI canvases, minimaps, pixel-art scaling) ----

```c
ARC_API arc_texture arc_target_create(int w, int h, bool smooth);
```

Until arc_target_end, every draw (2D and 3D) goes into `target`. Targets nest. clear: fill with argb first
   (alpha byte 0 = transparent here). Each pass uses the camera and lights set while it is current.

```c
ARC_API void arc_target_begin(arc_texture target, bool clear, uint32_t argb);
ARC_API void arc_target_end(void);
ARC_API int arc_target_width(void);    // size of what is being drawn into now (the screen outside any target)
ARC_API int arc_target_height(void);
```

---- 0.10.2 split-screen: several views of one world in one window ----
   Between arc_viewport_begin and arc_viewport_end everything is drawn into that rectangle of the window (pixels,
   top-left origin; clipped to the window) as if it were the whole screen: set its camera with arc_set_camera, draw
   the world, then its HUD. Inside a viewport arc_width / arc_height / arc_aspect, arc_target_width, 2D, UI,
   arc_safe_area, arc_mouse, arc_world_to_screen and arc_screen_ray all work in the viewport's own pixels (0,0 = its
   top-left), so HUD code doesn't change. Each viewport gets its own sky, light tiles, light shadows, sun shadow
   cascades fitted to its camera, planar reflection, and the post effects of arc_set_post (SSAO, bloom, FXAA, ...).
   A frame with no viewports draws exactly as before. Viewports can't nest, and must start outside render targets
   (targets can be used inside one). Up to ARC_MAX_VIEWPORTS a frame. The `camera` / `cam rig` overrides change only
   the window's own camera. On D3D11 with MSAA the viewports are drawn without it.
   Between frames (in tick) arc_screen_ray(px) with a window pixel picks through the viewport under px (last frame's
   viewports and cameras); arc_viewport_at tells which one that is. Players and pads: see docs/split-screen.md.

```c
#define ARC_MAX_VIEWPORTS 4
ARC_API void arc_viewport_begin(int x, int y, int w, int h);
ARC_API void arc_viewport_end(void);
ARC_API int arc_viewport_current(void);    // index of the viewport being drawn (in begin order), -1 outside
ARC_API int arc_viewport_count(void);    // viewports this frame so far (in draw), else last frame's
ARC_API bool arc_viewport_rect(int index, int *x, int *y, int *w, int *h);    // its window rectangle (as above)
```

the viewport under window pixel px (-1: none); local (may be NULL) = px in that viewport's pixels

```c
ARC_API int arc_viewport_at(arc_v2 px, arc_v2 *local);
```

a world point to window pixels through viewport index's camera (false: behind it)

```c
ARC_API bool arc_viewport_world_to_screen(int index, arc_v3 p, arc_v2 *out);
```

Save the last rendered frame as PNG (works in every backend, including headless).

```c
ARC_API bool arc_screenshot(const char *path);    // .png, or .jpg/.jpeg (quality 90: a 1920x1080 shot is well under 1 MB)
```

Save a screenshot once the current frame has been drawn (from tick or draw; works headless and in
   immediate mode). Prints "ARC ok shot PATH" like the agent command.

```c
ARC_API void arc_screenshot_after_frame(const char *path);
```

0.10.1 contact sheets for art review: register meshes in draw() with arc_sheet_add (it only records while the
   `sheet` command collects, so it costs nothing otherwise); `sheet FILE.png [COLS] [cell WxH] [bg RRGGBB|transparent]
   [margin M]` draws each in its own cell, framed by arc_camera_fit from the front (a little above and to the right),
   under the same light, with its label underneath, and prints "ARC sheet {cells...}". Cells are at most the window's
   size. Posed meshes are framed by their bind-pose box. `shot FILE.png crop [MARGIN]` trims a screenshot to what
   differs from its corner colour.

```c
ARC_API void arc_sheet_add(arc_mesh mesh, arc_m4 model, const arc_mesh_style *style, const char *label);
ARC_API int arc_sheet_count(void);    // items registered while collecting
typedef struct { int draw_calls, instances, triangles, lines, quads, passes; float cpu_ms, frame_ms; float tick_ms; double avg_frame_ms; const char *backend; int dropped; int culled; int lod_instances[4]; int cells_visible; int static_draws; int textures, textures_compressed; long long texture_bytes; int vertices_2d, vertices_2d_max; } arc_gfx_stats;
ARC_API arc_gfx_stats arc_stats(void);
```

The engine's FPS overlay (top left): frame rate, frame-time percentiles and the worst frame of the last 10 s,
   frame/CPU/tick times, draw calls, triangles and a frame-time graph (green 60+, amber 30+, red below; it rescales
   so spikes fit, with a line at the 99th percentile). Also --fps, the `fps` command, or F3 with --dev /
   arc_app.dev_keys.

```c
ARC_API void arc_show_fps(bool on);
ARC_API bool arc_fps_shown(void);
```

Frame times (ms between rendered frames) over the last 10 seconds (600 frames). p99 is the time 1 frame in 100
   is slower than; low1_fps = 1000 / p99 ("1% low"). samples = 0 until a frame has been rendered.

```c
typedef struct { float avg, p50, p95, p99, max, max_1s, low1_fps; int samples; } arc_frame_times;
ARC_API arc_frame_times arc_get_frame_times(void);
```

Frame budget (0.7.1): where the time of a frame goes, against a target frame time. The FPS overlay shows it as bars
   (a part over the whole budget is red; when the frame is over, its biggest part is amber), `stats` prints it and
   `dump` has it as "budget". Parts: tick (game logic, all ticks of the frame), draw (draw(): building the draw queue,
   with the UI), sort (ARC batching the queue), submit (the renderer: on GPUs handing the frame over, in the soft
   renderer all the drawing), present (swap / vsync wait: where the GPU's time shows up), audio (ARC's audio work per
   tick and the mixer), other (the rest of the frame). zones: the game's own ARC_PROF zones (they are inside tick or
   draw), largest first. Values are smoothed over about half a second. Display only: nothing here changes the game.

```c
typedef struct { float budget_ms; float frame_ms; float tick_ms, draw_ms, sort_ms, submit_ms, present_ms, audio_ms, other_ms; int zone_count; const char *zone_name[8]; float zone_ms[8]; bool over; } arc_frame_budget;
ARC_API arc_frame_budget arc_get_frame_budget(void);
ARC_API void arc_set_frame_budget(float ms);    // e.g. 8.33 for 120 Hz; 0 = the default
```

---- Scene speed-ups (0.7.1): cells and portals, levels of detail, static batches, view tests ----------------------
   All optional: a game that calls none of these draws exactly as before. They work in every renderer (they decide
   what is queued; the renderers draw it as usual).

   View tests against the current camera (the one set last with arc_set_camera, for the pass being drawn):

```c
ARC_API bool arc_sphere_visible(arc_v3 center, float radius);    // false only when it is surely outside the view
ARC_API bool arc_aabb_visible(arc_v3 lo, arc_v3 hi);
```

Cells and portals: rooms (cells: one or more boxes under an id) joined by portals (doorways: a box, usually flat,
   between two cells) the game opens and closes. From the camera's cell, ARC walks through open portals, narrowing the
   view to each portal's rectangle on screen, to find which cells can be seen. A camera outside every cell (top-down
   views over a deck) sees each cell whose box is in view. Meshes drawn after arc_set_draw_cell(id) belong to that
   cell and are not queued while it cannot be seen (0 = no cell: always drawn; it resets to 0 every frame). Particles,
   decals, trails and billboards are never culled this way. The answer is worked out on demand for each camera and kept
   until the camera, the cells or a portal change. Note: a hidden cell's meshes cast no shadows either (keep shadow
   casters that reach through doorways untagged). Agents: `cells` prints what is seen, `cells off` / `cells on` turns
   the culling off and on, `cells draw` toggles a wireframe of cells (green seen, grey hidden) and portals (yellow
   open, red closed).

```c
#define ARC_MAX_CELLS 1024    // ids 1 .. 1023
ARC_API void arc_cell_add(int id, arc_v3 lo, arc_v3 hi);    // add a box to cell id (L-shaped rooms: several boxes)
ARC_API int arc_portal_add(int cell_a, int cell_b, arc_v3 lo, arc_v3 hi);    // returns the portal (1..), 0 if full; open
ARC_API void arc_portal_set_open(int portal, bool open);
ARC_API bool arc_portal_is_open(int portal);
ARC_API void arc_cells_clear(void);    // forget every cell and portal
ARC_API int arc_cell_at(arc_v3 p);    // the smallest cell box holding p (its id), 0 if none
ARC_API bool arc_cell_visible(int id);    // from the current camera; id 0 = true
ARC_API int arc_cells_visible_count(void);
```

The same for any camera and view size, with no drawing state (tick-safe): fills bits (ARC_MAX_CELLS / 32 words,
   bit id set = seen) and returns how many cells are seen.

```c
ARC_API int arc_cells_compute(const arc_camera *c, float view_w, float view_h, uint32_t *bits);
ARC_API void arc_set_draw_cell(int id);
ARC_API int arc_get_draw_cell(void);
ARC_API void arc_cells_set_culling(bool on);    // default on; off = tagged meshes always drawn
ARC_API void arc_cells_debug_draw(void);    // the wireframe above, as 3D lines now
```

Levels of detail: lower-detail meshes drawn in place of `base` for instances that look small. Up to 3 levels
   (1..3) besides the base. With arc_mesh_set_lod, level k is used when the instance's bounding sphere covers less than
   screen_size of the view's height (0.1 = a tenth; the same across field of view and resolution); with
   arc_mesh_set_lod_distance, when the sphere's centre is further than `distance` from the camera. Each level's
   threshold must be smaller (or further) than the one before. lod {0} removes a level. Picked per instance as it is
   queued, so shadows use the same level. Each level is a mesh of its own (texture, material, shadow casting): a level
   with no texture or material borrows the base's when set. Destroying a mesh removes it from any LOD list.

```c
ARC_API void arc_mesh_set_lod(arc_mesh base, int level, arc_mesh lod, float screen_size);
ARC_API void arc_mesh_set_lod_distance(arc_mesh base, int level, arc_mesh lod, float distance);
```

Quality setting: bias 2 keeps detail to twice the distance, 0.5 drops it at half (0 = 1). force: -1 automatic, else
   always draw that level (or the nearest the mesh has): for tests and comparisons. Agents: `lod`, `lod bias X`,
   `lod force N`.

```c
ARC_API void arc_set_lod_bias(float bias);
ARC_API void arc_set_lod_force(int level);
ARC_API int arc_mesh_lod_level(arc_mesh base, arc_m4 model);    // the level an instance would get from the current camera
```

Static batches: many draws that never move (a level's walls and floor tiles) handed over once and merged into a few
   big meshes, one per texture and material (and per cell, and per chunk with arc_static_set_chunk), each drawn with one
   call. The draw's tint is baked into the vertex colours; mirrored matrices are fine; materials, textures (also those
   .arcm files bring) and shadow casting come from the source meshes, which must stay alive while the batch is used
   (skinned meshes are refused). Nothing is merged until arc_static_build; build again after adding or clearing.
   arc_static_draw skips chunks outside the view (not while shadows are on: they may cast into view).

```c
typedef struct { uint32_t id; } arc_static;
ARC_API arc_static arc_static_create(void);
ARC_API void arc_static_add(arc_static s, arc_mesh m, arc_m4 model, uint32_t tint);
ARC_API void arc_static_add_cell(arc_static s, arc_mesh m, arc_m4 model, uint32_t tint, int cell);    // -1 = arc_cell_at(its centre)
ARC_API void arc_static_set_chunk(arc_static s, float size);    // also split on an x/z grid this size (finer culling); 0 = none
ARC_API bool arc_static_build(arc_static s);
ARC_API void arc_static_draw(arc_static s);
ARC_API void arc_static_clear(arc_static s);    // forget the items (what was built stays until the next build)
ARC_API void arc_static_destroy(arc_static s);
ARC_API int arc_static_mesh_count(arc_static s);    // merged meshes from the last build
ARC_API int arc_static_item_count(arc_static s);
```

## Audio: synth effects, sound files, streamed music, 32-voice mixer

```c
typedef enum { ARC_WAVE_SQUARE, ARC_WAVE_SAW, ARC_WAVE_SINE, ARC_WAVE_NOISE, ARC_WAVE_TRIANGLE } arc_wave;
typedef struct { arc_wave wave; float freq, freq_end; float attack, sustain, decay; float volume; float duty; float vibrato_hz; float vibrato_depth; } arc_sfx;
typedef struct { uint32_t id; } arc_sound;
ARC_API arc_sound arc_sound_make(const arc_sfx *s);    // render a synth effect into a sample
ARC_API arc_sound arc_sound_from_pcm(const int16_t *mono, int frames, int rate);
ARC_API arc_sound arc_sound_load(const char *path);    // .mp3, .qoa or 16-bit .wav, mono or stereo, any rate
ARC_API arc_sound arc_sound_from_memory(const uint8_t *data, size_t size);
ARC_API float arc_sound_length(arc_sound s);    // seconds
```

Playing a sound returns a voice: change or stop it while it plays. A voice handle goes stale (and the
   calls below do nothing) once the sound ends or its voice is reused. 32 voices; the oldest is reused.

```c
typedef struct { uint32_t id; } arc_voice;
ARC_API arc_voice arc_play(arc_sound s, float volume, float pan);    // pan -1..1
```

pitch: playback speed (1 = normal, 2 = an octave up); loop: repeat until arc_voice_stop

```c
ARC_API arc_voice arc_play_ex(arc_sound s, float volume, float pan, float pitch, bool loop);
```

0.10.14: pitch 1 +/- pitch_var at random (0.1 = 10%), so repeated shots, hits and footsteps don't sound the same.
   The randomness is ARC's own, for looks: the game's arc_rand_* sequence and its replays are untouched

```c
ARC_API arc_voice arc_play_var(arc_sound s, float volume, float pan, float pitch_var);
ARC_API void arc_voice_stop(arc_voice v);
ARC_API void arc_voice_set_volume(arc_voice v, float volume);
ARC_API void arc_voice_set_pan(arc_voice v, float pan);
ARC_API void arc_voice_set_pitch(arc_voice v, float pitch);
ARC_API bool arc_voice_playing(arc_voice v);
ARC_API void arc_set_master_volume(float v);
```

Positional sound: place voices in the world and the mixer pans and fades them from where the listener is
   (usually the camera: arc_set_listener(cam.eye, target - eye, up)). Volume falls off from min_dist (full) to
   max_dist (silent); defaults 1 and 40 units, changed for new voices by arc_set_audio_range. Moving voices:
   arc_voice_set_position every tick. Gains update after each tick.

```c
ARC_API void arc_set_listener(arc_v3 pos, arc_v3 forward, arc_v3 up);
ARC_API arc_voice arc_play_3d(arc_sound s, arc_v3 pos, float volume);
ARC_API arc_voice arc_play_3d_ex(arc_sound s, arc_v3 pos, float volume, float pitch, bool loop);
ARC_API void arc_voice_set_position(arc_voice v, arc_v3 pos);
ARC_API void arc_voice_set_range(arc_voice v, float min_dist, float max_dist);
ARC_API void arc_set_audio_range(float min_dist, float max_dist);
```

Filters and space: a low-pass per voice (muffled: behind a wall, under water; hz 0 = off), one for everything
   (arc_set_master_lowpass: pause menus, explosions' deafness), and a room reverb that voices feed (music and
   streams stay dry). arc_set_reverb(0, 0) turns it off; room_size 0..1 (small room .. cavern).

```c
ARC_API void arc_voice_set_lowpass(arc_voice v, float hz);
ARC_API void arc_voice_set_reverb(arc_voice v, float send);    // 0..1 (default 1)
ARC_API void arc_set_reverb(float mix, float room_size);    // mix 0..1
ARC_API void arc_set_master_lowpass(float hz);
```

Streams: PCM the game makes as it goes (synthesised music, speech), played back to back with no gaps.
   Push a chunk whenever arc_stream_queued() drops below what you want buffered (say half a second).

```c
typedef struct { uint32_t id; } arc_stream;
ARC_API arc_stream arc_stream_create(int rate, int channels);    // channels 1 or 2
ARC_API void arc_stream_push(arc_stream s, const int16_t *pcm, int frames);    // interleaved if stereo
ARC_API float arc_stream_queued(arc_stream s);    // seconds not played yet
ARC_API void arc_stream_set_volume(arc_stream s, float volume);
ARC_API void arc_stream_clear(arc_stream s);    // drop what is queued (stop at once)
ARC_API void arc_stream_destroy(arc_stream s);
```

Volume groups ("buses") for a sound-settings menu: every sound plays on one. Sounds default to SFX (set once with
   arc_sound_set_bus, or per play with arc_play_bus); music is on MUSIC; streams default to MUSIC. Bus volumes
   multiply in the mixer at once, even for voices already playing. arc_ui_sound_settings() draws the sliders;
   arc_audio_save_settings / _load_settings keep them with arc_save.

```c
typedef enum { ARC_BUS_SFX = 0, ARC_BUS_MUSIC, ARC_BUS_UI, ARC_BUS_VOICE, ARC_BUS_COUNT } arc_bus;
ARC_API void arc_set_bus_volume(arc_bus bus, float volume);    // 0..1 (up to 2)
ARC_API float arc_bus_volume(arc_bus bus);
ARC_API void arc_sound_set_bus(arc_sound s, arc_bus bus);
ARC_API arc_voice arc_play_bus(arc_sound s, arc_bus bus, float volume, float pan);
ARC_API void arc_stream_set_bus(arc_stream s, arc_bus bus);
ARC_API bool arc_audio_save_settings(void);
ARC_API bool arc_audio_load_settings(void);    // false if nothing was saved
```

Music: an .mp3 or .qoa file kept compressed in memory and decoded while it plays. MP3 loops gaplessly when
   the file has a LAME/Info tag (LAME and FFmpeg write one).

```c
typedef struct { uint32_t id; } arc_music;
ARC_API arc_music arc_music_load(const char *path);
ARC_API arc_music arc_music_from_memory(const uint8_t *data, size_t size);
ARC_API void arc_music_play(arc_music m, bool loop);    // from the start
ARC_API void arc_music_stop(arc_music m);
ARC_API void arc_music_pause(arc_music m, bool paused);
ARC_API void arc_music_set_volume(arc_music m, float volume);
ARC_API bool arc_music_playing(arc_music m);
ARC_API float arc_music_length(arc_music m);    // seconds
```

0.10.13 a loop section: when the track loops it goes back to start_s and plays to end_s (seconds; 0 = the file's end),
   seamlessly; the first play still starts at 0, so an intro plays once. arc_music_set_loop(m, 0, 0) loops the whole
   file again. MP3s are decoded to start_s once, when this is called. arc_music_set_loop_fade: a crossfade of that many
   seconds (0..2) into the loop point, for sections that don't quite line up (0.08 is a good start): the jump then
   happens fade seconds before end_s while the end fades out over it. arc_music_position reads the place in the file.

```c
ARC_API void arc_music_set_loop(arc_music m, float start_s, float end_s);
ARC_API void arc_music_set_loop_fade(arc_music m, float seconds);
```

Variety and limits. A sound can vary each time it plays: pitch by up to +-semitones and volume down by up to
   volume_range (0.2 = 80..100%), drawn from the audio system's own generator (arc_audio_seed), never arc_rand_*, so
   the game's random stream is untouched. Limits: at most max_voices of a sound (or on a bus) at once; a play over
   the limit takes one of them (oldest or quietest) or, with ARC_STEAL_NONE, is refused (returns a stale voice).
   Priority: a play only takes voices of its own or lower priority (lowest first), for limits and when all 32
   voices are busy; finding none, it is refused. Everything 0 = as before. Stolen voices stop at once.

```c
typedef enum { ARC_STEAL_OLDEST, ARC_STEAL_QUIETEST, ARC_STEAL_NONE } arc_steal;
ARC_API void arc_sound_set_variation(arc_sound s, float semitones, float volume_range);
ARC_API void arc_sound_set_limit(arc_sound s, int max_voices, arc_steal steal);    // 0 = no limit
ARC_API void arc_sound_set_priority(arc_sound s, int priority);    // default 0; higher keeps its voice
ARC_API void arc_bus_set_limit(arc_bus bus, int max_voices, arc_steal steal);    // voices on the bus; 0 = no limit
ARC_API void arc_audio_seed(uint64_t seed);    // the variation generator (default seed 1)
ARC_API float arc_audio_random(float lo, float hi);    // from that generator: per-play variety of your own
```

Ducking: a bus drops by db while a voice, stream or music on any of trigger_buses (a mask: 1 << ARC_BUS_VOICE)
   plays, e.g. music under speech: arc_bus_duck(ARC_BUS_MUSIC, 1 << ARC_BUS_VOICE, 9, 0.05f, 0.5f). attack and
   release: seconds to go all the way down and back up (straight lines in dB). db 0 = off.

```c
ARC_API void arc_bus_duck(arc_bus bus, unsigned trigger_buses, float db, float attack, float release);
ARC_API float arc_bus_ducked(arc_bus bus);    // dB it is lowered by now
```

Occlusion: how much is between a positional voice and the listener, 0 (clear) .. 1 (behind a wall). The hook is
   asked when a positional voice starts and after every tick (keep it cheap: one ray, or a cached answer); or set
   an amount on any voice yourself (the hook overrides it on positional voices). It is heard as a gain and a
   low-pass that glide over 50 ms (no zipper noise); fully occluded = arc_set_occlusion_filter (0.4, 800 Hz).

```c
typedef float (*arc_occlusion_fn)(arc_v3 from, arc_v3 to, void *user);    // from: the sound, to: the listener
ARC_API void arc_set_occlusion(arc_occlusion_fn fn, void *user);    // NULL = off
ARC_API void arc_voice_set_occlusion(arc_voice v, float amount);
ARC_API void arc_set_occlusion_filter(float gain, float lowpass_hz);
```

Adaptive music. Stems: tracks of one piece (same rate and length) linked to a lead play, pause, stop and loop
   with it, locked to the sample (link them before playing the lead; their own play/stop/pause is ignored). Every
   track has a fade level under its volume (arc_music_fade glides it, 1 at load). arc_music_set_intensity drives the
   stems: the lead always plays and stem k of n (in link order) fades in as intensity goes from (k-1)/n to k/n.
   Tempo (bpm, beats per bar, first beat in seconds) lets changes wait for the next beat or bar of a track (loops
   start the grid over). arc_music_switch starts `to` (and its stems) from the top on the next beat / bar of `from`,
   crossfading over fade seconds (0 = a cut); `from` stops when faded out. A stinger is a one-shot on the music bus
   that starts exactly on the next beat / bar. With no tempo, or the clock track not playing: at once.

```c
typedef enum { ARC_SYNC_NOW, ARC_SYNC_BEAT, ARC_SYNC_BAR } arc_sync;
ARC_API bool arc_music_link(arc_music stem, arc_music lead);    // lead {0}: unlink; false if rates differ
ARC_API void arc_music_fade(arc_music m, float level, float seconds);    // level 0..1 (up to 4)
ARC_API void arc_music_set_intensity(arc_music lead, float intensity, float seconds, arc_sync when);
ARC_API void arc_music_set_tempo(arc_music m, float bpm, int beats_per_bar, float first_beat);
ARC_API float arc_music_position(arc_music m);    // seconds into the track, updated by the mixer
ARC_API float arc_music_beat(arc_music m);    // beats since the first beat (the fraction is the phase)
ARC_API void arc_music_switch(arc_music from, arc_music to, bool loop, arc_sync when, float fade);
ARC_API arc_voice arc_music_stinger(arc_music clock, arc_sound s, float volume, arc_sync when);
```

## Turns and grids (time-unit scheduler, tile grid, line of sight, paths)


One rule set, three ways to play. Every action costs time units (TU); the scheduler decides who may act.
  REALTIME  : world clock follows game time (tu_per_second); actors act when ready_at <= now
  ROGUELIKE : world waits for the player; when nobody is ready the clock jumps to the next actor
  TACTICAL  : side phases with action points (AP = TU budget); enemies act in sweeps
Call arc_turns_frame(&s, arc_dt()) once per tick, then let the player act if ready and loop
arc_turns_next_ai() for the AI. Timers on the world clock (s.now) pause automatically in turn modes.

```c
typedef enum { ARC_TURNS_REALTIME = 0, ARC_TURNS_ROGUELIKE, ARC_TURNS_TACTICAL, ARC_TURNS_MODE_COUNT } arc_turns_mode;
ARC_API const char *arc_turns_mode_name(arc_turns_mode m);
typedef struct { double ready_at; int ap, ap_max; int team; bool alive, is_player; } arc_actor;
typedef struct { arc_turns_mode mode; double now; double tu_per_second; int round, sweep, active_team, player_team, round_tu; float ai_delay, ai_timer; } arc_turns;
ARC_API void arc_turns_init(arc_turns *s, arc_turns_mode mode, int player_team);
ARC_API void arc_turns_set_mode(arc_turns *s, arc_turns_mode mode, arc_actor **actors, int n);
ARC_API void arc_turns_frame(arc_turns *s, double dt);    // once per tick
ARC_API bool arc_turns_player_ready(const arc_turns *s, const arc_actor *a);
ARC_API void arc_turns_spend(arc_turns *s, arc_actor *a, int cost_tu, float speed);    // speed 1 = normal, 2 = twice as fast
ARC_API int arc_turns_next_ai(arc_turns *s, arc_actor **actors, int n);    // index of the AI actor to act now, or -1
ARC_API void arc_turns_end_player_phase(arc_turns *s, arc_actor **actors, int n);    // tactical
ARC_API bool arc_turns_waiting_for_player(const arc_turns *s, arc_actor **actors, int n);
```

Tile grid of game-defined ids (uint8).

```c
typedef struct { int w, h; uint8_t *cells; } arc_grid;
ARC_API bool arc_grid_init(arc_grid *g, arc_arena *a, int w, int h);    // cells from the arena (or malloc if a is NULL)
ARC_API uint8_t arc_grid_get(const arc_grid *g, int x, int y, uint8_t outside);
ARC_API void arc_grid_set(arc_grid *g, int x, int y, uint8_t v);
```

Bresenham line of sight; blocks(tile) says whether a tile blocks sight (end points are not tested)

```c
ARC_API bool arc_grid_los(const arc_grid *g, int x0, int y0, int x1, int y1, bool (*blocks)(uint8_t));
```

breadth-first search: first 4-way step from (sx,sy) toward (tx,ty); false if unreachable.
   The target tile itself need not be passable (so you can path to an enemy).

```c
ARC_API bool arc_grid_path_step(const arc_grid *g, int sx, int sy, int tx, int ty, bool (*passable)(uint8_t), int *nx, int *ny);
```

full path length in steps (or -1), same rules as arc_grid_path_step

```c
ARC_API int arc_grid_path_length(const arc_grid *g, int sx, int sy, int tx, int ty, bool (*passable)(uint8_t));
```

Boards for tactics games (0.9.9): each tile has a move cost (0 = blocked; else the points it takes to enter), cover
   (none, half, full: full blocks sight) and an occupant (a unit id, 0 = empty). Plain data (keep it in your game state);
   integer maths, deterministic, nothing allocated.
     arc_board_init(&G.board, 32, 24, ARC_BOARD_CHEBYSHEV);
     arc_board_set_tile(&G.board, x, y, 2, ARC_COVER_HALF);              // forest: costs 2, half cover
     int16_t cost[ARC_BOARD_MAX * ARC_BOARD_MAX];
     arc_board_reach(&G.board, ux, uy, 6, ARC_BOARD_DIAGONAL, cost);     // cost[y * w + x] >= 0: reachable for that many points
     int cover; if (arc_board_los(&G.board, ux, uy, tx, ty, &cover)) ...  // A sees B exactly when B sees A
   Line of sight runs between tile centres; tiles in between give their cover (full blocks), and where the line passes
   exactly through a corner, the two tiles touching it give half cover each (both full: blocked). The two end tiles
   don't count: add the target tile's own cover (arc_board_cover) if your rules want it. unit_cover makes occupied
   tiles in the way give that cover (ARC_COVER_FULL: units block sight). Moves: entering a tile costs its cost, also
   diagonally; a diagonal step can't cut past a blocked corner; occupied tiles are not entered unless
   ARC_BOARD_IGNORE_UNITS (or the path's target with ARC_BOARD_ONTO_TARGET). Ties between equal paths always break the
   same way. Facings 0..7 go round in 45-degree steps from +x toward +y (arc_board_dir gives the step).

```c
#define ARC_BOARD_MAX 128
enum { ARC_BOARD_CHEBYSHEV = 0, ARC_BOARD_MANHATTAN = 1 };    // how arc_board_range counts
enum { ARC_COVER_NONE = 0, ARC_COVER_HALF = 1, ARC_COVER_FULL = 2 };
enum { ARC_BOARD_DIAGONAL = 1, ARC_BOARD_IGNORE_UNITS = 2, ARC_BOARD_ONTO_TARGET = 4 };    // reach / path flags
enum { ARC_ARC_FRONT = 0, ARC_ARC_LEFT = 1, ARC_ARC_RIGHT = 2, ARC_ARC_REAR = 3 };
enum { ARC_EDGE_NORTH = 1, ARC_EDGE_EAST = 2, ARC_EDGE_SOUTH = 4, ARC_EDGE_WEST = 8 };    // north: y = 0
typedef struct { int w, h, metric; int unit_cover; uint8_t cost[ARC_BOARD_MAX * ARC_BOARD_MAX], cover[ARC_BOARD_MAX * ARC_BOARD_MAX]; int16_t occ[ARC_BOARD_MAX * ARC_BOARD_MAX]; } arc_board;
ARC_API bool arc_board_init(arc_board *b, int w, int h, int metric);    // every tile cost 1, no cover, empty
ARC_API void arc_board_set_tile(arc_board *b, int x, int y, int cost, int cover);
ARC_API void arc_board_set_occupant(arc_board *b, int x, int y, int id);    // 0 = empty
ARC_API int arc_board_cost(const arc_board *b, int x, int y);    // 0 outside the board
ARC_API int arc_board_cover(const arc_board *b, int x, int y);    // ARC_COVER_FULL outside
ARC_API int arc_board_occupant(const arc_board *b, int x, int y);
ARC_API bool arc_board_los(const arc_board *b, int x0, int y0, int x1, int y1, int *cover);    // cover: the best on the way
ARC_API int arc_board_range(const arc_board *b, int x0, int y0, int x1, int y1);
```

tiles reachable within budget points: out_cost (w * h, may be NULL) gets the cheapest cost or -1; returns the count

```c
ARC_API int arc_board_reach(const arc_board *b, int x, int y, int budget, int flags, int16_t *out_cost);
```

the cheapest route: steps written to out_xy as x, y pairs (up to max, not counting the start); returns the number
   of steps (0: already there) or -1 when there is none; total_cost (may be NULL) gets its cost

```c
ARC_API int arc_board_path(const arc_board *b, int fx, int fy, int tx, int ty, int flags, int *out_xy, int max, int *total_cost);
ARC_API int arc_board_arc(int facing, int x0, int y0, int x1, int y1);    // where (x1, y1) is seen from (x0, y0): ARC_ARC_*
ARC_API int arc_board_dir(int facing, int *dx, int *dy);    // a facing's step; returns facing 0..7
```

edge tiles (drop zones, exits): sides = ARC_EDGE_* bits; free_only skips blocked and occupied tiles

```c
ARC_API int arc_board_edge_tiles(const arc_board *b, int sides, bool free_only, int *out_xy, int max);
```

## UI: immediate-mode widgets (menus, settings, debug panels)


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

## UX: easing, screen transitions, captions, colour blindness, localisation


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

## Collision: ray tests, a static collision world, a character controller


Rays: o + d * t for t in [0, max_t] (d need not be unit length; t is in units of d). Hits report the nearest
   point; the normal faces the ray. Pass NULL for h to just test. All of this is plain deterministic maths:
   use it in tick().

```c
typedef struct { bool hit; float t; arc_v3 point, normal; int tri; uint32_t user; } arc_hit;
ARC_API bool arc_ray_sphere(arc_v3 o, arc_v3 d, arc_v3 center, float radius, float max_t, arc_hit *h);
ARC_API bool arc_ray_box(arc_v3 o, arc_v3 d, arc_v3 lo, arc_v3 hi, float max_t, arc_hit *h);
ARC_API bool arc_ray_triangle(arc_v3 o, arc_v3 d, arc_v3 a, arc_v3 b, arc_v3 c, float max_t, arc_hit *h);
ARC_API bool arc_ray_mesh(arc_mesh m, arc_m4 model, arc_v3 o, arc_v3 d, float max_t, arc_hit *h);    // any mesh, every triangle (picking)
```

A collision world: static level geometry (meshes, boxes, triangles) in a bounding volume tree, built when first
   queried after a change. `user` is your own tag, returned in hits (a surface type, an object id).

```c
typedef struct { uint32_t id; } arc_cworld;
ARC_API arc_cworld arc_cworld_create(void);
ARC_API void arc_cworld_destroy(arc_cworld w);
ARC_API void arc_cworld_clear(arc_cworld w);
ARC_API void arc_cworld_add_mesh(arc_cworld w, arc_mesh m, arc_m4 model, uint32_t user);
ARC_API void arc_cworld_add_box(arc_cworld w, arc_v3 lo, arc_v3 hi, uint32_t user);
ARC_API void arc_cworld_add_triangles(arc_cworld w, const arc_v3 *v, int count, uint32_t user);    // count = vertices
ARC_API int arc_cworld_triangles(arc_cworld w);
ARC_API bool arc_cworld_raycast(arc_cworld w, arc_v3 o, arc_v3 d, float max_t, arc_hit *h);
ARC_API bool arc_cworld_overlap_sphere(arc_cworld w, arc_v3 center, float radius);
ARC_API int arc_cworld_push_sphere(arc_cworld w, arc_v3 *center, float radius);    // moves the sphere out; returns triangles touched
```

Capsule character: set velocity (x/z from input; y to jump) and call arc_character_move once per tick.
   It falls with gravity, slides along walls, stands on floors up to max_slope without sliding, climbs steps
   up to step_height and sticks to the ground going down slopes. Zero fields take the defaults shown.

```c
typedef struct { arc_v3 pos; arc_v3 velocity; float radius; float height; float step_height; float max_slope; float gravity; float snap_distance; bool on_ground, hit_wall, hit_ceiling; arc_v3 ground_normal; } arc_character;
ARC_API void arc_character_move(arc_cworld w, arc_character *c, float dt);
```

## Physics (0.9.4): deterministic rigid bodies


Spheres, boxes and capsules that fall, stack, slide, bounce and roll, against each other and against static
   ground: planes and triangles (level meshes). Step it in tick() with arc_dt(). Deterministic: the same bodies and the
   same calls give the same bytes on every run and every machine (only + - * / and square roots, fixed iterations,
   everything in index order), so replays, rewind and rollback multiplayer keep working; arc_physics_save/_load copy
   the whole state for save_state/load_state.
     G.phys = arc_physics_create(NULL);                                  // gravity -9.81 in y
     arc_physics_add_plane(G.phys, arc_v3_make(0, 1, 0), 0);              // the ground at y = 0
     int crate = arc_physics_add(G.phys, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.5f, 0.5f },
                                                          .pos = { 0, 3, 0 }, .mass = 10 });
     arc_physics_step(G.phys, arc_dt());                                  // in tick
     arc_draw_mesh(cube, arc_m4_mul(arc_physics_matrix(G.phys, crate), arc_m4_scale(arc_v3_make(1, 1, 1))), 0xFFFFFF);
   Bodies are numbered from 1 (0 = none / the static world). Units are metres, kilograms and seconds.

```c
typedef struct { uint32_t id; } arc_physics;
enum { ARC_BODY_DYNAMIC = 0, ARC_BODY_STATIC = 1, ARC_BODY_KINEMATIC = 2 };
enum { ARC_SHAPE_SPHERE = 0, ARC_SHAPE_BOX = 1, ARC_SHAPE_CAPSULE = 2 };
typedef struct { arc_v3 gravity; bool no_gravity; int iterations; int max_bodies; bool no_sleep; } arc_physics_desc;
typedef struct { int type; int shape; arc_v3 size; arc_v3 pos; arc_quat rot; arc_v3 vel, ang_vel; float mass; float friction; float restitution; float linear_damping, angular_damping; float rolling_friction; uint32_t layer, mask; uint32_t user; bool lock_rotation; } arc_body_desc;
typedef struct { int body; float t; arc_v3 point, normal; } arc_physics_hit;    // body 0 = static ground
typedef struct { int a, b; arc_v3 point, normal; float impulse; } arc_physics_event;    // a < b; b may be 0 (the ground)
ARC_API arc_physics arc_physics_create(const arc_physics_desc *desc);
ARC_API void arc_physics_destroy(arc_physics w);
ARC_API int arc_physics_add(arc_physics w, const arc_body_desc *d);    // the body's number, 0 if full
ARC_API void arc_physics_remove(arc_physics w, int body);
```

static ground: an infinite plane (points p with dot(normal, p) = d are on it; above is outside), and triangles
   (counter-clockwise from the side bodies stay on, like ARC meshes)

```c
ARC_API void arc_physics_add_plane(arc_physics w, arc_v3 normal, float d);
ARC_API void arc_physics_add_triangles(arc_physics w, const arc_v3 *v, int count);    // count = vertices
ARC_API void arc_physics_add_mesh(arc_physics w, arc_mesh m, arc_m4 model);
ARC_API void arc_physics_step(arc_physics w, float dt);    // in tick, with arc_dt()
ARC_API arc_v3 arc_physics_pos(arc_physics w, int body);
ARC_API arc_quat arc_physics_rot(arc_physics w, int body);
ARC_API arc_m4 arc_physics_matrix(arc_physics w, int body);    // position and rotation (scale your mesh to the size)
ARC_API arc_v3 arc_physics_vel(arc_physics w, int body);
ARC_API arc_v3 arc_physics_ang_vel(arc_physics w, int body);
ARC_API bool arc_physics_sleeping(arc_physics w, int body);
ARC_API uint32_t arc_physics_user(arc_physics w, int body);
ARC_API int arc_physics_body_count(arc_physics w);
```

moving bodies yourself (each wakes the body)

```c
ARC_API void arc_physics_set_pos(arc_physics w, int body, arc_v3 pos, arc_quat rot);    // a teleport (velocity kept)
ARC_API void arc_physics_set_vel(arc_physics w, int body, arc_v3 vel, arc_v3 ang_vel);
ARC_API void arc_physics_impulse(arc_physics w, int body, arc_v3 impulse, arc_v3 at);    // kg m/s at a world point
ARC_API void arc_physics_force(arc_physics w, int body, arc_v3 force, arc_v3 at);    // newtons, for the next step
```

kinematic bodies: reach this pose by the end of the next step (moving at the speed that takes, so they push)

```c
ARC_API void arc_physics_move_kinematic(arc_physics w, int body, arc_v3 pos, arc_quat rot);
ARC_API void arc_physics_wake(arc_physics w, int body);
```

the first body or ground along o + d * t, t in 0..max_t (d need not be unit length: t is in units of d)

```c
ARC_API bool arc_physics_raycast(arc_physics w, arc_v3 o, arc_v3 d, float max_t, uint32_t mask, arc_physics_hit *hit);
```

bodies whose shapes overlap a sphere; returns how many (writes up to max)

```c
ARC_API int arc_physics_overlap_sphere(arc_physics w, arc_v3 center, float radius, uint32_t mask, int *bodies, int max);
```

pairs that started touching in the last step (impacts: for sounds and damage), with the impulse that stopped them

```c
ARC_API int arc_physics_events(arc_physics w, arc_physics_event *out, int max);
```

the whole simulation state (bodies, contacts carried between steps); static ground is not included: add the same
   ground before loading. Returns the bytes needed (writes them if cap is enough).

```c
ARC_API size_t arc_physics_save(arc_physics w, void *buf, size_t cap);
ARC_API bool arc_physics_load(arc_physics w, const void *buf, size_t size);
ARC_API void arc_physics_draw(arc_physics w, uint32_t rgb);    // debug: shapes as lines, sleeping ones dimmer
```

0.9.7 joints: a hinge (a door, a wheel, a lever, a drawbridge) turns about one axis through a pivot; a ball joint
   keeps a pivot together and turns freely (ropes and chains: a row of small bodies joined end to end). Pivot and axis
   are given in the world, as the bodies stand when the joint is added; angle 0 is that pose. A hinge can have limits
   (radians, within -3.1 .. 3.1) and a motor (it drives a's turn relative to b toward motor_speed rad/s, right hand
   about the axis, with at most motor_torque
   N m: a winch, a powered wheel; speed 0 and some torque make a brake or a stiff hinge). Joined bodies pass through
   each other unless `collide`. b = 0 pins a body to the world. Joints are deterministic and part of
   arc_physics_save / _load; removing a body removes its joints. arc_physics_draw shows them.

```c
enum { ARC_JOINT_HINGE = 0, ARC_JOINT_BALL = 1 };
typedef struct { int type; int a, b; arc_v3 anchor; arc_v3 axis; bool limit; float lower, upper; bool motor; float motor_speed, motor_torque; bool collide; float cone; arc_v3 cone_axis; } arc_joint_desc;
ARC_API int arc_physics_add_joint(arc_physics w, const arc_joint_desc *d);    // joint number (1 ...); 0 on bad input (logged)
ARC_API void arc_physics_remove_joint(arc_physics w, int joint);
ARC_API float arc_physics_joint_angle(arc_physics w, int joint);    // a hinge's turn from angle 0: a's relative to b, about the axis (right hand)
ARC_API void arc_physics_joint_motor(arc_physics w, int joint, float speed, float max_torque);    // max_torque 0 = off
ARC_API void arc_physics_joint_limits(arc_physics w, int joint, bool on, float lower, float upper);
ARC_API float arc_physics_joint_impulse(arc_physics w, int joint);    // the pivot's impulse last step (N s): break a joint above some value
ARC_API int arc_physics_joint_count(arc_physics w);
ARC_API float arc_physics_joint_swing(arc_physics w, int joint);    // 0.9.8 ball: angle between a's axis and the cone's centre
```

Ragdolls (0.9.8): a body of 11 capsules (pelvis, chest, head, upper and lower arms, thighs, shins) joined with human
   limits: ball joints with cones and twist at the waist, neck, shoulders and hips; hinges at the elbows and knees that
   bend one way. Build one from a skinned character's pose when it dies (its joints found by name: mixamo's Hips,
   Spine, Neck, Head, LeftArm, LeftForeArm, LeftHand, LeftUpLeg, LeftLeg, LeftFoot and the Unreal and Blender styles
   work too), then draw the character from the ragdoll each frame:
     arc_ragdoll_from_pose(G.phys, rig, &pose, model, &(arc_ragdoll_desc){ .mass = 80, .prev = &last_pose, .prev_dt = arc_dt() }, &G.rag);
     arc_ragdoll_pose(G.phys, &G.rag, model, &pose); arc_draw_mesh_posed(mesh, model, 0xFFFFFF, rig, &pose);
   or without a skeleton (arc_ragdoll_humanoid) and draw the parts with arc_ragdoll_draw. Parts don't collide with each
   other (they share a layer, 0x40000000 by default, that their mask leaves out). The struct is plain data (save it with
   your state, next to arc_physics_save); deterministic like the rest of the physics.

```c
enum { ARC_RAG_PELVIS, ARC_RAG_CHEST, ARC_RAG_HEAD, ARC_RAG_UPPER_ARM_L, ARC_RAG_FOREARM_L, ARC_RAG_UPPER_ARM_R, ARC_RAG_FOREARM_R, ARC_RAG_THIGH_L, ARC_RAG_SHIN_L, ARC_RAG_THIGH_R, ARC_RAG_SHIN_R, ARC_RAG_PARTS };
typedef struct { float mass; float radius_scale; float friction; uint32_t layer, mask; uint32_t user; arc_v3 vel; const arc_pose *prev; float prev_dt; } arc_ragdoll_desc;
typedef struct { int body[ARC_RAG_PARTS]; int joint[ARC_RAG_PARTS]; float radius[ARC_RAG_PARTS], half[ARC_RAG_PARTS]; int joints; int8_t owner[ARC_MAX_JOINTS]; arc_m4 off[ARC_MAX_JOINTS]; } arc_ragdoll;
ARC_API bool arc_ragdoll_from_pose(arc_physics w, arc_anim rig, const arc_pose *pose, arc_m4 model, const arc_ragdoll_desc *d, arc_ragdoll *out);
ARC_API bool arc_ragdoll_humanoid(arc_physics w, arc_v3 feet, float yaw, float height, const arc_ragdoll_desc *d, arc_ragdoll *out);
ARC_API void arc_ragdoll_pose(arc_physics w, const arc_ragdoll *r, arc_m4 model, arc_pose *out);    // the skeleton as the ragdoll lies
ARC_API void arc_ragdoll_draw(arc_physics w, const arc_ragdoll *r, arc_mesh unit, uint32_t tint);    // each part as `unit` (diameter 1, height 1) scaled
ARC_API void arc_ragdoll_remove(arc_physics w, arc_ragdoll *r);
```

## Navigation (0.9.2): navmeshes, paths, agents that follow them


A navmesh is the walkable surface as triangles. Build it once (in init, or when the level loads) from any mix of
   triangles, loaded meshes and tile grids; then ask it for paths, the nearest walkable point, whether a straight walk
   is clear, and random walkable points. Paths go around walls and gaps and keep `radius` from corners.
     G.nav = arc_navmesh_create(NULL);
     arc_navmesh_add_mesh(G.nav, level_floor, arc_m4_identity());   // or arc_navmesh_add_grid(G.nav, &grid, ...)
     arc_navmesh_build(G.nav);
     arc_nav_agent_init(&G.guard, start, 3.5f, 0.4f);              // speed (units/s), radius
     arc_nav_agent_goto(&G.guard, G.nav, target);                  // in tick, when the target changes
     arc_nav_agent_update(&G.guard, G.nav, arc_dt());              // every tick
   Triangles join where they share an edge (vertices closer than `weld` count as one). An edge that meets the middle
   of a longer edge does not join (a "T-junction"): split the long edge, or use add_grid, which never makes them.
   Everything is deterministic (same inputs, same paths and moves on every machine), and queries never allocate.
   Agents are plain structs with no pointers: keep them in your game state and save_state/load_state copy them.

```c
typedef struct { uint32_t id; } arc_navmesh;
typedef struct { float max_slope; float weld; } arc_navmesh_desc;
ARC_API arc_navmesh arc_navmesh_create(const arc_navmesh_desc *desc);    // NULL = defaults
ARC_API void arc_navmesh_destroy(arc_navmesh nm);
```

add walkable geometry (before arc_navmesh_build; adding after a build starts a new one at the next build).
   Triangles wind counter-clockwise seen from above (like ARC's meshes); those facing down or too steep are skipped.

```c
ARC_API void arc_navmesh_add_triangles(arc_navmesh nm, const arc_v3 *v, int count);    // count = vertices, 3 a triangle
ARC_API void arc_navmesh_add_mesh(arc_navmesh nm, arc_mesh m, arc_m4 model);    // the mesh's upward triangles
```

a tile grid: every cell where walkable(id) becomes a square cell x cell at height origin.y, cell (x, y) at
   origin + (x * cell, 0, y * cell) .. + cell (so grid y runs along +Z). walkable NULL = id 0 is walkable.

```c
ARC_API void arc_navmesh_add_grid(arc_navmesh nm, const arc_grid *g, arc_v3 origin, float cell, bool (*walkable)(uint8_t));
```

take out the walkable area inside a box (obstacles placed on a floor mesh): triangles whose centre is inside go

```c
ARC_API void arc_navmesh_remove_box(arc_navmesh nm, arc_v3 lo, arc_v3 hi);
ARC_API bool arc_navmesh_build(arc_navmesh nm);    // false if nothing walkable
ARC_API int arc_navmesh_triangles(arc_navmesh nm);    // walkable triangles after the build
ARC_API int arc_navmesh_islands(arc_navmesh nm);    // separate walkable areas (not joined to each other)
```

Path from `from` to `to` for an agent of `radius`: writes up to max corner points, the first = from (moved onto the
   mesh), the last = to (or the nearest walkable point to it). Returns the number of points; 0 when there is no path
   (different islands, or every way is narrower than 2 * radius). A longer path is cut at max (its total count is
   still returned, so compare with max).

```c
ARC_API int arc_navmesh_path(arc_navmesh nm, arc_v3 from, arc_v3 to, float radius, arc_v3 *out, int max);
ARC_API float arc_navmesh_path_length(const arc_v3 *pts, int n);
```

nearest point on the walkable surface within max_dist (0 = any distance); false if none

```c
ARC_API bool arc_navmesh_nearest(arc_navmesh nm, arc_v3 p, float max_dist, arc_v3 *out);
```

walk a straight line over the surface from `from` toward `to` (in x/z, following its height): true if it reaches
   `to`; false if it meets an edge of the walkable area first, with *hit (may be NULL) where it stopped

```c
ARC_API bool arc_navmesh_walkable_line(arc_navmesh nm, arc_v3 from, arc_v3 to, arc_v3 *hit);
ARC_API bool arc_navmesh_connected(arc_navmesh nm, arc_v3 a, arc_v3 b);    // on the same island
ARC_API float arc_navmesh_height(arc_navmesh nm, arc_v3 p, bool *found);    // the surface's height under/over p
```

a random walkable point, spread evenly by area; rng NULL = arc_rand (seeded by --seed)

```c
ARC_API arc_v3 arc_navmesh_random_point(arc_navmesh nm, arc_rng *rng);
```

debug drawing (from draw): the triangle edges in rgb, the outline of the walkable area brighter

```c
ARC_API void arc_navmesh_draw(arc_navmesh nm, uint32_t rgb);
```

An agent that follows paths: steering, slowing down to arrive, sliding along the edge of the walkable area,
   finding the path again when it runs past the stored corners. Plain data: copy it freely.

```c
#define ARC_NAV_CORNERS 32
typedef struct { arc_v3 pos, vel; float speed; float accel; float radius; float arrive; arc_v3 target; arc_v3 corners[ARC_NAV_CORNERS]; int ncorners, corner; bool moving; bool arrived; bool failed; bool partial; } arc_nav_agent;
ARC_API void arc_nav_agent_init(arc_nav_agent *a, arc_v3 pos, float speed, float radius);
ARC_API bool arc_nav_agent_goto(arc_nav_agent *a, arc_navmesh nm, arc_v3 target);    // false = no path (a->failed)
ARC_API void arc_nav_agent_stop(arc_nav_agent *a);
ARC_API void arc_nav_agent_update(arc_nav_agent *a, arc_navmesh nm, float dt);    // in tick
```

agents keep `radius` apart: pushes overlapping ones away from each other and back onto the surface (in tick,
   after the updates; n agents in one array, O(n^2): fine for a few hundred)

```c
ARC_API void arc_nav_agents_separate(arc_nav_agent *agents, int n, arc_navmesh nm);
ARC_API float arc_nav_agent_remaining(const arc_nav_agent *a);    // distance left along the stored path
ARC_API void arc_nav_agent_draw(const arc_nav_agent *a, uint32_t rgb);    // its path, from draw
```

## Action (0.10.0): AI brains, senses, shots, camera rigs


---- AI brains: state machines ----
   Describe the states and transitions once in an arc_brain_def (static game data), keep one arc_brain per agent in
   the game state, and call arc_brain_update(&agent.brain, &def, &agent, dt) in tick. Each update: a forced state
   (arc_brain_force, the `brain` command) or the first transition whose conditions all hold switches state (exit, then
   enter); then the state's tick runs; a state can also ask for a change with arc_brain_go. One switch per update,
   transitions tried in the order they were added.

     static arc_brain_def DROID;
     enum { PATROL, ALERT, ATTACK, SEARCH };     // arc_brain_def_state returns 0, 1, 2 ... in order
     arc_brain_def_state(&DROID, "PATROL", patrol_enter, patrol_tick, NULL);
     ...
     arc_brain_def_transition(&DROID, PATROL, ALERT, ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_AWARE, 0.2f), ARC_BRAIN_NONE);
     arc_brain_def_transition(&DROID, ALERT, ATTACK, ARC_BRAIN_IF(ARC_BRAIN_TIME_GE, 0, 0.5f), ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_AWARE, 1));
     arc_brain_def_transition(&DROID, ARC_BRAIN_ANY, FLEE, ARC_BRAIN_IF(ARC_BRAIN_F_LE, B_HEALTH, 0.25f), ARC_BRAIN_NONE);
     arc_brain_watch("droid", &G.droid[0].brain, G.ndroids, sizeof G.droid[0], &DROID);   // dump + `brain` command

   The blackboard (f, i, v) is the agent's memory for the rules: write it from the game or the state callbacks; name
   the slots with def->f_name[k] etc. for the dump. Triggers are one-tick events (arc_brain_trigger): a transition on
   a trigger takes it; the rest are cleared at the end of the update. Watched brains appear in `dump` as "brains":
   {"droid": [{"state": "SEARCH", "time": 1.25, ...}]}, and `brain droid 3 SEARCH` forces a state.

```c
#define ARC_BRAIN_STATES 32
#define ARC_BRAIN_TRANSITIONS 96
#define ARC_BRAIN_F 16
#define ARC_BRAIN_I 8
#define ARC_BRAIN_V 4
#define ARC_BRAIN_ANY (-1)
enum { ARC_BRAIN_COND_NONE = 0, ARC_BRAIN_TRIGGER, ARC_BRAIN_F_GE, ARC_BRAIN_F_LE, ARC_BRAIN_I_EQ, ARC_BRAIN_I_NE, ARC_BRAIN_I_GE, ARC_BRAIN_I_LE, ARC_BRAIN_TIME_GE, ARC_BRAIN_FUNC };    // condition kinds: slot and value as noted
typedef struct { int kind, slot; float value; } arc_brain_cond;
arc_brain_cond arc_brain_if(int kind, int slot, float value);
#define ARC_BRAIN_IF(kind, slot, value) arc_brain_if((kind), (slot), (float)(value))    // (a function: C++ has no compound literals)
#define ARC_BRAIN_NONE arc_brain_if(ARC_BRAIN_COND_NONE, 0, 0.0f)
typedef struct arc_brain { int state, prev; int ticks; float time; float f[ARC_BRAIN_F]; int i[ARC_BRAIN_I]; arc_v3 v[ARC_BRAIN_V]; uint32_t triggers; int pending; bool forced, started; int changes; uint8_t hist[8][2]; int hist_n; } arc_brain;
typedef struct arc_brain_def arc_brain_def;
typedef void (*arc_brain_fn)(arc_brain *b, void *user, float dt);    // enter / exit get dt 0
typedef bool (*arc_brain_test)(const arc_brain *b, void *user);
struct arc_brain_def { const char *name; int state_count; struct { const char *name; arc_brain_fn enter, tick, exit; } states[ARC_BRAIN_STATES]; int transition_count; struct { int from, to; arc_brain_cond c[2]; arc_brain_test func; } tr[ARC_BRAIN_TRANSITIONS]; const char *f_name[ARC_BRAIN_F], *i_name[ARC_BRAIN_I], *v_name[ARC_BRAIN_V], *trigger_name[32]; };
ARC_API int arc_brain_def_state(arc_brain_def *d, const char *name, arc_brain_fn enter, arc_brain_fn tick, arc_brain_fn exit);    // the state's number, -1 if full
ARC_API int arc_brain_def_transition(arc_brain_def *d, int from, int to, arc_brain_cond c0, arc_brain_cond c1);    // -1 if full
ARC_API int arc_brain_def_transition_fn(arc_brain_def *d, int from, int to, arc_brain_test func);    // on a test of the game's own
ARC_API int arc_brain_def_find(const arc_brain_def *d, const char *state_name);    // -1 if none
ARC_API void arc_brain_init(arc_brain *b, int state);    // blackboard zeroed; `state` entered on the first update
ARC_API void arc_brain_update(arc_brain *b, const arc_brain_def *d, void *user, float dt);    // in tick
ARC_API void arc_brain_trigger(arc_brain *b, int trigger);    // 0..31, for this update
ARC_API void arc_brain_go(arc_brain *b, int state);    // from a callback: switch after this tick (or at the next update)
ARC_API void arc_brain_force(arc_brain *b, int state);    // switch at the next update, whatever the transitions say
ARC_API const char *arc_brain_state_name(const arc_brain *b, const arc_brain_def *d);
```

dump and the `brain GROUP N STATE` command for an array of agents (brain inside each, `stride` bytes apart); up to
   8 groups; call again with count 0 to stop (do it before the array goes away)

```c
ARC_API void arc_brain_watch(const char *group, arc_brain *first, int count, size_t stride, const arc_brain_def *d);
```

---- Senses: sight, hearing, memory, alerts ----
   An arc_senses in the game state holds up to 64 agents and what each knows about up to 4 targets. Each tick: set
   the agents' positions and facings (arc_senses_agent fields), report noises, then arc_senses_update with the
   targets and a line-of-sight test of the game's own (grid walk, arc_cworld ray, physics ray: arc_senses_los_cworld
   and arc_senses_los_physics are ready-made ones).
   - Sight: a target is checked when inside the cone (half_angle, radians) and range; each of its sample points is
     tested from the agent's eye. awareness rises by dt * visible_fraction / build_up and falls by dt / build_up
     when unseen; at 1 the target is seen (memory updated). Partly hidden targets take longer.
   - Hearing: arc_senses_noise(pos, radius, kind, loudness): agents within radius * loudness * hearing hear it; a
     blocked line multiplies the radius by `muffle` (0 = walls stop sound, 1 = no effect). A heard noise from a
     target updates that target's memory (as heard, without awareness); one from no target (-1) is kept as the
     agent's last noise.
   - Memory: last known position, when, how (seen, heard, told); forgotten after `forget` seconds.
   - Alerts: arc_senses_alert shares an agent's memories with every agent within a radius (as told).
   - arc_senses_search_points gives points on a navmesh around a last known position, the same every time.

```c
#define ARC_SENSES_AGENTS 64
#define ARC_SENSES_TARGETS 4
#define ARC_SENSES_NOISES 32
#define ARC_SENSES_POINTS 9
enum { ARC_KNOW_NONE = 0, ARC_KNOW_SEEN, ARC_KNOW_HEARD, ARC_KNOW_TOLD };
typedef struct { int target; float awareness; bool seen_now; int how; arc_v3 pos; float when; } arc_senses_memory;
typedef struct { bool used; arc_v3 pos, facing; float eye_height; float half_angle, range; float build_up; float forget; float hearing; float muffle; int team; arc_senses_memory mem[ARC_SENSES_TARGETS]; bool heard_noise; arc_v3 noise_pos; int noise_kind; float noise_when; } arc_senses_agent;
typedef struct { int id, team; arc_v3 points[ARC_SENSES_POINTS]; int npoints; } arc_senses_target;
typedef struct { float time; int agent_count; arc_senses_agent agent[ARC_SENSES_AGENTS]; int noise_count; struct { arc_v3 pos; float radius, loudness; int kind, source; } noise[ARC_SENSES_NOISES]; } arc_senses;
typedef bool (*arc_senses_los_fn)(arc_v3 from, arc_v3 to, void *user);    // true = clear
ARC_API void arc_senses_init(arc_senses *s);
ARC_API int arc_senses_add(arc_senses *s, arc_v3 pos, arc_v3 facing);    // the agent's number, -1 if full; defaults set
ARC_API void arc_senses_remove(arc_senses *s, int agent);
ARC_API void arc_senses_set(arc_senses *s, int agent, arc_v3 pos, arc_v3 facing);    // every tick it moves
ARC_API void arc_senses_noise(arc_senses *s, arc_v3 pos, float radius, int kind, float loudness, int source_target);    // heard at the next update
ARC_API void arc_senses_update(arc_senses *s, float dt, const arc_senses_target *targets, int count, arc_senses_los_fn los, void *user);
ARC_API const arc_senses_memory *arc_senses_knows(const arc_senses *s, int agent, int target);    // NULL if nothing
ARC_API void arc_senses_forget(arc_senses *s, int agent, int target);
ARC_API int arc_senses_alert(arc_senses *s, int agent, float radius);    // agents told
ARC_API int arc_senses_search_points(arc_navmesh nm, arc_v3 center, float radius, int n, arc_v3 *out);    // the first is the nearest to center
ARC_API bool arc_senses_los_cworld(arc_v3 from, arc_v3 to, void *cworld);    // user = &your arc_cworld
ARC_API bool arc_senses_los_physics(arc_v3 from, arc_v3 to, void *physics); /* user = &your arc_physics; any body in the way blocks
(the ray stops 0.05 short of the point) */ ARC_API void arc_senses_draw(const arc_senses *s);
ARC_API void arc_senses_watch(const arc_senses *s, const char *name);    // in dump as "senses" (NULL s stops)
```

---- Shots: hitscan and projectiles ----
   arc_shot_fire walks a shot through the world: the grid (a blocks(x, z) callback of the game's own: doors half open
   and all), the collision world, physics bodies and the game's own moving targets (arc_shots_targets, set each tick
   before firing), nearest first. Hitscan shots (speed 0) resolve at once; projectiles move in arc_shots_update. The
   game applies damage from arc_shots_hits (this tick's hits). Looks are optional: a tracer, an impact decal, sparks
   (an effect registered with arc_shots_fx), a sound and a short light. State: arc_shots_save / _load (with the
   game's own save_state; fixed size, no pointers). Call arc_shots_draw() from draw for tracers, projectiles and
   flashes. `frame` lists "shots": tracers, projectiles.

```c
#define ARC_SHOTS_MAX 256    // projectiles in flight
#define ARC_SHOTS_TARGETS 128
#define ARC_SHOTS_HITS 64
enum { ARC_HIT_NONE = 0, ARC_HIT_TARGET, ARC_HIT_GRID, ARC_HIT_WORLD, ARC_HIT_BODY };
typedef struct arc_shot_desc { arc_v3 from, dir; float range; float speed; float radius; uint32_t mask; int owner; uint32_t user; uint32_t tracer_rgb; float tracer_width, tracer_life; float projectile_size; arc_texture impact_decal; float decal_size; uint32_t decal_argb; int impact_fx; arc_sound impact_sound; float sound_volume; float flash_light; uint32_t flash_rgb; } arc_shot_desc;
typedef struct { int shot; int kind; int id; int cell_x, cell_z; arc_v3 point, normal; float dist; int owner; uint32_t user; } arc_shot_hit;
typedef struct { int id; uint32_t layer; arc_v3 a, b; float radius; } arc_shot_target;    // a sphere (a == b) or a capsule; layer 0 = 1
ARC_API void arc_shots_set_world(arc_cworld cw, arc_physics phys, bool (*grid_blocks)(int x, int z, void *user), void *user);    // any can be {0} / NULL
ARC_API void arc_shots_set_grid(float cell, float origin_x, float origin_z);    // cell (x, z) spans origin + x * cell ..; default 1, 0, 0
ARC_API void arc_shots_targets(const arc_shot_target *t, int count);    // this tick's moving targets (copied)
ARC_API int arc_shots_fx(const arc_fx *fx);    // register an impact effect: its number for impact_fx (keep fx alive)
ARC_API int arc_shot_fire(const arc_shot_desc *d);    // the shot's number (> 0); a hitscan shot's hit is in arc_shots_hits now
ARC_API bool arc_shot_cast(const arc_shot_desc *d, arc_shot_hit *out);    // the same walk with no shot, looks or state (aim checks)
ARC_API void arc_shots_update(float dt);    // in tick: projectiles move, tracers age
ARC_API int arc_shots_hits(arc_shot_hit *out, int max);    // hits made during this tick
ARC_API int arc_shots_in_flight(void);
ARC_API void arc_shots_clear(void);
ARC_API void arc_shots_draw(void);
ARC_API int arc_shots_save(void *buf, int max);    // bytes written (or needed when buf is NULL)
ARC_API bool arc_shots_load(const void *buf, int size);
```

---- Camera rigs: top-down, follow, orbit, first person ----
   Keep one arc_cam_rig per view in the game state; each tick set target (and yaw / pitch for the facing or mouse
   look) and call arc_cam_rig_update; in draw pass arc_cam_rig_camera to arc_set_camera. FOLLOW and ORBIT keep a
   sphere of collide_radius out of walls (arc_cam_rig_set_world: grid, collision world, physics statics): the eye
   pulls in at once when something is in the way and eases back out over follow_lag, so it never pops outward.
   arc_cam_rig_blend switches views smoothly (position, aim and field of view; orthographic <-> perspective through
   a matched view, so top-down <-> third person doesn't jump). Facing: yaw 0 looks along +z, as a model drawn with
   arc_m4_trs_y(pos, yaw, ...) whose front is +z; pitch > 0 looks up (FIRST) or raises the eye (ORBIT).
   Commands: `cam` (the rig's state), `cam rig topdown|follow|orbit|first|off`, `cam shake N`. `frame` shows the
   rig in "camera".

```c
typedef enum { ARC_RIG_TOPDOWN = 0, ARC_RIG_FOLLOW, ARC_RIG_ORBIT, ARC_RIG_FIRST, ARC_RIG_FIXED } arc_rig_kind;
typedef struct arc_cam_rig { arc_rig_kind kind; arc_v3 target; float yaw, pitch; float distance, height; float look_ahead; float aim_height; float ortho_half; float bounds[4]; bool has_bounds; float fov_y; float follow_lag; float collide_radius; arc_v3 eye; float znear, zfar; float aspect; arc_v3 eye_now, target_now; float yaw_now, dist_now, shake, shake_time; bool started; float blend_left, blend_len; arc_camera blend_from; } arc_cam_rig;
ARC_API void arc_cam_rig_set_world(arc_cworld cw, arc_physics phys, uint32_t phys_mask, bool (*grid_blocks)(int x, int z, void *user), void *user, float cell);    // for every rig
ARC_API void arc_cam_rig_update(arc_cam_rig *r, float dt);    // lag, collision, blend and shake
ARC_API arc_camera arc_cam_rig_camera(const arc_cam_rig *r);    // for arc_set_camera
ARC_API void arc_cam_rig_blend(arc_cam_rig *from, const arc_cam_rig *to, float seconds);    // from becomes to, smoothly
ARC_API bool arc_cam_rig_blending(const arc_cam_rig *r);
ARC_API void arc_cam_rig_shake(arc_cam_rig *r, float amount);    // trauma 0..1 added; decays by itself
ARC_API void arc_cam_rig_snap(arc_cam_rig *r);    // no lag this update (after a teleport)
```

## Terrain (0.9.6): height grids, splatted ground, scattered props


A terrain is a grid of heights drawn as chunks of triangles (lower detail further away, skirts hiding the seams),
   textured by up to 4 ground layers blended by a splat map (paint it, or fill it from height and slope rules).
   Heights, normals and ray hits follow the triangles as drawn. Scatter places copies of a mesh (grass, rocks, trees)
   over it by layer, height and slope, the same every run (a seed), and draws those near the camera.
     arc_texture lay[4] = { grass, dirt, rock, snow };   // same-size textures (loaded or made)
     G.ter = arc_terrain_create(&(arc_terrain_desc){ .heights = h, .samples_x = 257, .samples_z = 257,
                                .size_x = 200, .size_z = 200, .origin = { -100, 0, -100 },
                                .layers = { lay[0], lay[1], lay[2], lay[3] }, .layer_count = 4, .layer_scale = { 0.25f, 0.3f, 0.2f, 0.2f } });
     arc_terrain_splat_rules(G.ter, rules, 3);                    // rock on slopes, snow up high ...
     G.grass = arc_scatter_create(G.ter, &(arc_scatter_desc){ .mesh = tuft, .density = 2, .layer = 0, .seed = 1 });
     // draw():  arc_terrain_draw(G.ter, ARC_WHITE); arc_scatter_draw(G.grass);
   For physics and navmeshes hand over its triangles (arc_terrain_triangles). All handles are freed with _destroy.

```c
typedef struct { uint32_t id; } arc_terrain;
typedef struct { const float *heights; int samples_x, samples_z; float size_x, size_z; arc_v3 origin; int chunk; int splat_res; arc_texture layers[4]; int layer_count; float layer_scale[4]; float splat_sharpness; const arc_material *material; float lod_distance; arc_texture color_map; } arc_terrain_desc;
ARC_API arc_terrain arc_terrain_create(const arc_terrain_desc *d);    // {0} on bad input (logged)
ARC_API void arc_terrain_destroy(arc_terrain t);
ARC_API void arc_terrain_draw(arc_terrain t, uint32_t tint);    // from draw(); chunks cast and take shadows
```

0.9.7: chunks out of view are skipped. With sun shadows on, chunks whose shadow can reach the view still draw; with a
   reflection plane on, all do (the mirror sees elsewhere). Chunks out of view cast no point/spot light shadows.

```c
ARC_API int arc_terrain_drawn(arc_terrain t);    // chunks the last arc_terrain_draw drew
ARC_API int arc_terrain_chunk_count(arc_terrain t);
ARC_API void arc_terrain_set_culling(arc_terrain t, bool on);    // on by default; off draws every chunk
```

Ground markers (0.9.9): tile highlights, rings under units, drop zones, intent arrows, in one call each. They lie on
   the ground (a flat y, an arc_terrain, or your height function; big shapes are split so they follow bumps), unlit and
   see-through, pulled a hair toward the camera so they never z-fight it, and batched (consecutive markers in a pass
   are one draw). edge_width is in screen pixels, so outlines stay crisp at any zoom; pulse breathes the fill; dash
   breaks the edge; on_top also shows a faint copy where units or walls cover the marker; label is text centred above
   (2D, so frame.texts has it). Every marker is listed in the frame command's frame.markers (shape as a name such as
   "ring", pos, size, rgb, label; target when drawn into a render target).
   Draw them in draw() (or tick() in immediate mode), like meshes. Zero is the default for every field:
     arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = u.x, .z = u.z, .size = 1.1f, .inner = 0.82f,
                                     .fill_rgb = 0x38D8F0, .fill_alpha = 0.7f, .edge_rgb = 0xFFFFFF, .edge_width = 1.5f,
                                     .terrain = G.ground, .on_top = true });

```c
enum { ARC_MARK_TILE = 0, ARC_MARK_RING = 1, ARC_MARK_DISC = 2, ARC_MARK_ARROW = 3, ARC_MARK_CHEVRON = 4, ARC_MARK_DIAMOND = 5 };
typedef struct { int shape; float x, z; float size; float yaw; float x2, z2; float inner; float head; uint32_t fill_rgb; float fill_alpha; uint32_t edge_rgb; float edge_width; float edge_alpha; float pulse, pulse_amount; float dash; bool on_top; float y; arc_terrain terrain; float (*height)(void *ud, float x, float z); void *height_ud; float lift; const char *label; uint32_t label_rgb; float label_height, label_scale; } arc_marker;
ARC_API void arc_draw_marker(const arc_marker *m);
ARC_API float arc_terrain_height(arc_terrain t, float x, float z);    // on the drawn triangles; outside, the nearest edge
ARC_API arc_v3 arc_terrain_normal(arc_terrain t, float x, float z);    // smooth (as lit), unit length
ARC_API bool arc_terrain_inside(arc_terrain t, float x, float z);
```

the first hit along dir (any length) within max_dist: *dist and *point set (either may be NULL)

```c
ARC_API bool arc_terrain_raycast(arc_terrain t, arc_v3 origin, arc_v3 dir, float max_dist, float *dist, arc_v3 *point);
```

editing: raise (amount < 0: lower) with a smooth falloff to `radius`; flatten toward a height by strength 0..1;
   replace them all. The chunks follow at once; scatter keeps its places until arc_scatter_rebuild.

```c
ARC_API void arc_terrain_sculpt(arc_terrain t, float x, float z, float radius, float amount);
ARC_API void arc_terrain_flatten(arc_terrain t, float x, float z, float radius, float height, float strength);
ARC_API void arc_terrain_set_heights(arc_terrain t, const float *heights);    // samples_x * samples_z
```

the splat map: paint a layer in (strength 0..1, smooth falloff; the other layers make room), fill from rules, or
   ask a layer's weight 0..1 at a point (footstep sounds, what grows where)

```c
ARC_API void arc_terrain_paint(arc_terrain t, float x, float z, float radius, int layer, float strength);
typedef struct { int layer; float height_min, height_max; float slope_min, slope_max; float blend; float amount; } arc_splat_rule;
ARC_API void arc_terrain_splat_rules(arc_terrain t, const arc_splat_rule *rules, int n);    // whole map from layer 0; later rules over earlier
ARC_API float arc_terrain_layer_weight(arc_terrain t, float x, float z, int layer);
```

full-detail triangles in world space, 3 points each: fills up to `max` points and returns how many there are (out
   NULL: just the count). For arc_physics_add_triangles and arc_navmesh_add_triangles.

```c
ARC_API int arc_terrain_triangles(arc_terrain t, arc_v3 *out, int max);
ARC_API arc_texture arc_terrain_splat_map(arc_terrain t);    // RGBA = layer weights (for a minimap)
```

0.10.13 (Grand Token Acquisition): ten kinds of ground from four layers. Bake the map's look into a colour map and let
   the layers add texture on top; write a whole splat map at once (both the texture and the copy arc_terrain_layer_weight,
   _paint and scatter read; arc_texture_update on arc_terrain_splat_map changes only the picture).

```c
ARC_API void arc_terrain_set_color_map(arc_terrain t, arc_texture tex);    // copied now: call again after changing tex; {0} = none
ARC_API void arc_terrain_set_splat(arc_terrain t, const uint8_t *rgba);    // splat_res * splat_res RGBA weights, rows along +x from origin
ARC_API int arc_terrain_splat_res(arc_terrain t);    // the splat map's side in pixels
typedef struct { uint32_t id; } arc_scatter;
typedef struct { arc_mesh mesh; float density; int layer; float min_weight; float height_min, height_max; float slope_max; float scale_min, scale_max; float align; float sink; bool fixed_yaw; uint32_t tint, tint2; float draw_distance; uint32_t seed; } arc_scatter_desc;
ARC_API arc_scatter arc_scatter_create(arc_terrain t, const arc_scatter_desc *d);
ARC_API void arc_scatter_destroy(arc_scatter s);
ARC_API void arc_scatter_rebuild(arc_scatter s);    // after sculpting or painting: places from the same seed
ARC_API void arc_scatter_draw(arc_scatter s);    // from draw(): copies in view within draw_distance
ARC_API int arc_scatter_count(arc_scatter s);    // copies placed (drawn or not)
ARC_API arc_m4 arc_scatter_matrix(arc_scatter s, int i);    // copy i's model matrix (0 .. count - 1): obstacles, picking
ARC_API int arc_scatter_drawn(arc_scatter s);    // copies the last arc_scatter_draw drew
```

## Profiling


Named CPU timers, summed per frame. Shown by `stats`, included in dumps.
  ARC_PROF("physics") { ...code... }

```c
ARC_API int arc_prof_begin(const char *name);
ARC_API void arc_prof_end(int id);
#define ARC_PROF(name)
```

## Files


Read a whole file into the given arena (NUL-terminated). NULL on failure.

```c
ARC_API uint8_t *arc_file_read(const char *path, arc_arena *a, size_t *size_out);
ARC_API bool arc_file_write(const char *path, const void *data, size_t size);
ARC_API bool arc_file_exists(const char *path);
```

Asset packs: one .arcpak holds many files (zlib-compressed where that helps; tools/arcpak.py makes them).
   Every ARC loader (meshes, textures, sounds, fonts, arc_file_read) looks in mounted packs first, by the same
   relative path ("assets/ui/icons.png"); the newest pack wins. assets.arcpak next to the program is mounted
   at startup.

```c
ARC_API bool arc_pack_mount(const char *path);
ARC_API bool arc_pack_mount_memory(const uint8_t *data, size_t size, const char *name);
```

## Loading in the background (0.7.1)


Loads between arc_load_begin and arc_load_end are queued instead of done at once: arc_texture_load(_ex),
   arc_mesh_load, arc_sound_load and arc_music_load return their handle straight away ({0} if the file is missing, as
   always) and it becomes ready once the file has been read, decoded and handed to the GPU.
     init():  arc_load_begin(0); G.ship = arc_mesh_load("assets/ship.arcm"); ...; arc_load_end();
   - Desktop: worker threads read and decode (PNG, JPEG, .arct, .arcm, sounds; --load-threads N, default the CPU count
     less one, 1 to 4); the main thread uploads, within a time budget each frame (arc_load_set_budget, 4 ms), so frames
     stay smooth. Web: the main thread does both, a slice within the budget each frame (0.8.6: with
     arc_app.browser_decode the browser decodes JPEG/PNG off the main thread; packs fetched on demand: arc_pack_fetch).
   - Until ready a handle is safe: a texture draws nothing and is 0 x 0, a mesh draws nothing (no triangles, zero
     bounds), a sound plays nothing (arc_play gives no voice), arc_music_play waits and starts the music when it is
     ready. Settings made meanwhile (arc_mesh_set_texture / _material / _double_sided / _cast_shadow, arc_sound_set_*,
     arc_texture_set_smooth, arc_music_set_volume) are kept. A load that fails logs as usual and stays empty.
   - While a group (flags 0) loads, ticks wait: tick() is not called, and draw() is replaced by arc_app.draw_loading
     (or ARC's loading screen). The game's first tick then sees everything loaded, so ticks, dumps and replays do not
     depend on how long loading took. arc_load_on_done runs a function (main thread) when a group is done: work that
     needs the data (bounds, copying a material) goes there or in tick().
   - ARC_LOAD_BACKGROUND groups load while the game runs (the next level's assets). On desktop when they finish depends
     on the machine, so tick logic must not depend on it (check arc_load_done only to start using them, and keep that
     out of anything a replay must reproduce).
   - Headless runs (agents, tests) are deterministic: arc_load_end finishes the group before it returns, in queue order
     on the main thread, so a headless run gives the same dumps whether the game queues or loads at once.
   Fonts, animations and arc_image_load always load at once; packs are looked in first, as with every loader; hot reload
   (--hot) watches queued files too. Handles get the same ids in every run (they are reserved in queue order).

```c
typedef struct { uint32_t id; } arc_load_group;    // {0} = every load queued so far
enum { ARC_LOAD_BACKGROUND = 1 };
typedef struct { int total, done, failed; long long bytes_total, bytes_done; float progress; double seconds; } arc_load_info;
ARC_API void arc_load_begin(int flags);    // flags: 0 or ARC_LOAD_BACKGROUND; groups do not nest
ARC_API arc_load_group arc_load_end(void);
ARC_API bool arc_load_done(arc_load_group g);
ARC_API float arc_load_progress(arc_load_group g);
ARC_API arc_load_info arc_load_status(arc_load_group g);
ARC_API void arc_load_wait(arc_load_group g);    // finish it now, on this thread (blocks)
ARC_API void arc_load_on_done(arc_load_group g, void (*fn)(void *user), void *user);    // at once if done already
ARC_API void arc_load_set_budget(float ms);    // main-thread upload time per frame (default 4)
ARC_API bool arc_texture_ready(arc_texture t);    // loaded and usable (true for any texture that was not queued)
ARC_API bool arc_mesh_ready(arc_mesh m);
ARC_API bool arc_sound_ready(arc_sound s);
ARC_API bool arc_music_ready(arc_music m);
ARC_API void arc_draw_loading_screen(float progress, const char *text);    // ARC's own: text over a bar, centred
```

---- Packs fetched on demand (0.8.6): a web build that starts small and downloads the rest while the game runs ----
   A web build made with WEB_PACKS (build.sh / build.bat, tools/web_packs.py; docs/WEB.md) preloads only part of assets/
   (the title screen's files) and puts each listed folder in a pack next to index.html: assets/levels/ -> levels.arcpak.
   The page starts as soon as the small first package is in, and:
     arc_load_begin(ARC_LOAD_BACKGROUND); G.lvl = arc_mesh_load("assets/levels/l1.arcm"); ...; G.grp = arc_load_end();
   fetches levels.arcpak by itself (a file listed in a pack that is not here yet: its pack is fetched first, then the
   file loads from it), with progress in arc_load_progress(G.grp). arc_pack_fetch("levels.arcpak") starts a download
   early (e.g. on the title screen) and returns its group (inside an open group the pack becomes part of that group).
   Fetched packs are kept in the browser's Cache API keyed by their content hash, so a second visit (or a reload) reads
   them from the cache; a new build's changed pack is downloaded again and the old copy deleted.
   - Desktop and headless (and single-package web builds): the files are already here, loose under assets/ (or in a
     pack file of that name, which is mounted at once): arc_pack_fetch's group is done at once and nothing changes.
   - Only queued loads (arc_load_begin / end) wait for a pack; loading a file of a pack that is not here yet at once
     (outside a group) fails as for any missing file. arc_load_wait cannot wait for a download on the web.

```c
enum { ARC_PACK_NONE = 0, ARC_PACK_FETCHING, ARC_PACK_READY, ARC_PACK_FAILED };
ARC_API arc_load_group arc_pack_fetch(const char *name);    // "levels.arcpak" (a URL relative to the page, or a file)
ARC_API int arc_pack_state(const char *name);    // ARC_PACK_*: NONE before arc_pack_fetch (or a load) asked for it
ARC_API bool arc_pack_ready(const char *name);    // mounted (or nothing to fetch: desktop, a single-package build)
```

0.10.9 arc_file_exists, or a file that a pack still to come holds (listed in arc_packs.txt, its pack not failed): a
   queued load of it will work. On desktop the same as arc_file_exists.

```c
ARC_API bool arc_file_listed(const char *path);
```

## Developer tools (0.7.2): tile level editor, replay timeline


---- 0.10.8 level files (arc_level): layers of character rows, settings and objects, in a text format ARC reads and
   writes, so games don't write their own (Droids' request). A file:
     arclvl 1
     title = "Droids"                       # the file's settings: name = value
     layer "Deck 1" 32 16                   # a layer: name, width, height
     light = 0.8                            # the layer's settings
     |################################|     # height rows, each '|' + exactly width cells + '|'
     |#....e.........................#|
     object 3 5 droid class=4 facing=2      # objects: cell, kind, fields name=value
     end
   Values are bare words (letters, digits, _ . + - / :) or "quoted" (\" \\ \n inside); lines starting with # are comments
   (outside rows). Saving writes the same text for the same level (settings and objects in the order they were set), so
   files diff well in git and a saved file loads and saves back byte for byte. A malformed file is refused:
   arc_level_load returns NULL and arc_level_error() says where ("decks.arclvl:12: a row must be ..."). With --hot (or
   arc_app.hot_reload) a loaded level reloads when its file changes (arc_level_reloaded says so once; the editor reloads
   with it); cells keep their memory when the layer sizes are unchanged. Layers need not share a size (the editor wants
   them to). Cells are printable ASCII. Every getter takes layer -1 for the file's own settings.

```c
typedef struct arc_level arc_level;
ARC_API arc_level *arc_level_new(void);
ARC_API arc_level *arc_level_load(const char *path);    // NULL on failure: arc_level_error()
ARC_API arc_level *arc_level_parse(const char *text, size_t len, const char *name);    // name: for error messages
ARC_API const char *arc_level_error(void);    // the last load / parse / save problem ("" = none)
ARC_API bool arc_level_save(arc_level *l, const char *path);    // path NULL = where it was loaded or last saved
ARC_API bool arc_level_reload(arc_level *l, const char *path);    // read again in place (path NULL = its own)
ARC_API char *arc_level_text(const arc_level *l, size_t *len);    // the file's text (free() it)
ARC_API bool arc_level_reloaded(arc_level *l);    // true once after a hot reload (rebuild what you build from it)
ARC_API const char *arc_level_path(const arc_level *l);
ARC_API void arc_level_free(arc_level *l);
ARC_API int arc_level_layer_count(const arc_level *l);
ARC_API int arc_level_add_layer(arc_level *l, const char *name, int w, int h, char fill);    // its index, -1
ARC_API void arc_level_remove_layer(arc_level *l, int layer);
ARC_API const char *arc_level_layer_name(const arc_level *l, int layer);
ARC_API void arc_level_set_layer_name(arc_level *l, int layer, const char *name);
ARC_API int arc_level_width(const arc_level *l, int layer);
ARC_API int arc_level_height(const arc_level *l, int layer);
```

the layer's cells: height rows of width chars, each row followed by a '\0' (stride width + 1; edit them in place)

```c
ARC_API char *arc_level_cells(arc_level *l, int layer);
ARC_API char arc_level_cell(const arc_level *l, int layer, int x, int y);    // 0 outside
ARC_API void arc_level_set_cell(arc_level *l, int layer, int x, int y, char c);
ARC_API const char *arc_level_get(const arc_level *l, int layer, const char *key, const char *def);    // layer -1: the file's
ARC_API float arc_level_get_num(const arc_level *l, int layer, const char *key, float def);
ARC_API void arc_level_set(arc_level *l, int layer, const char *key, const char *value);    // NULL removes it
ARC_API void arc_level_set_num(arc_level *l, int layer, const char *key, float value);
ARC_API int arc_level_setting_count(const arc_level *l, int layer);
ARC_API const char *arc_level_setting_key(const arc_level *l, int layer, int i);
ARC_API int arc_level_object_count(const arc_level *l, int layer);
ARC_API int arc_level_object_add(arc_level *l, int layer, int x, int y, const char *kind);    // its index, -1
ARC_API void arc_level_object_remove(arc_level *l, int layer, int i);    // later objects move down one
ARC_API const char *arc_level_object_kind(const arc_level *l, int layer, int i);
ARC_API int arc_level_object_x(const arc_level *l, int layer, int i);
ARC_API int arc_level_object_y(const arc_level *l, int layer, int i);
ARC_API void arc_level_object_move(arc_level *l, int layer, int i, int x, int y);
ARC_API const char *arc_level_object_get(const arc_level *l, int layer, int i, const char *key, const char *def);
ARC_API float arc_level_object_get_num(const arc_level *l, int layer, int i, const char *key, float def);
ARC_API void arc_level_object_set(arc_level *l, int layer, int i, const char *key, const char *value);    // NULL removes
ARC_API void arc_level_object_set_num(arc_level *l, int layer, int i, const char *key, float value);
ARC_API int arc_level_object_field_count(const arc_level *l, int layer, int i);
ARC_API const char *arc_level_object_field_key(const arc_level *l, int layer, int i, int k);
ARC_API int arc_level_object_at(const arc_level *l, int layer, int x, int y, int from);    // first index >= from there, -1
```

---- Tile level editor: any game with a grid of tiles can edit it in-game, the game owning the data and its file ----
   The game describes its palette and hands ARC a pointer to its grid; ARC draws a 2D top-down view over the game (or, with
   `world_cell` and `camera`, a grid over the game's own 3D view) and edits the cells in place, calling `changed` for every
   cell so the game rebuilds what it builds from them (meshes, collision, spawns). Nothing happens until arc_editor_begin.
     static const arc_editor_tile pal[] = { { '#', "Wall", 0x707880, ARC_KEY_W }, { '.', "Floor", 0x283038, ARC_KEY_F }, ... };
     arc_editor_begin(&(arc_editor_desc){ .name = "Deck 1", .width = 32, .height = 16, .cells = deck, .stride = 33,
                      .tiles = pal, .tile_count = 10, .changed = on_cell, .save = save_deck, .user = NULL });
   Opening: F2 (only with dev keys: arc_app.dev_keys or --dev; or desc.any_build), arc_editor_open(), or `editor on`.
   Ticks: the editor runs inside the tick, from the tick's input, like the UI kit (arc_ui_*): every click and key it reacts
   to is in recordings, so replays and `seek` redo the same edits. While it is open the game's tick() is not called
   (the game is paused; ARC's tick count still advances) until P ("play from here") lets it run with the editor open.
   draw() keeps being called. save/load hooks are not called while a replay plays or `seek` re-simulates (a replay never
   writes files); a replay that pressed load reads whatever the file holds now.
   Mouse: left = the tool with the selected tile, right = the same tool with the erase tile, middle drag (or SPACE + left)
   pans, wheel zooms at the pointer. Keys: B brush, L line, R rectangle (filled; SHIFT = outline), F flood fill, I eyedropper
   (or ALT + click), [ ] previous / next tile, a tile's own `key` selects it (a tile key wins over a tool key of the same letter;
   SHIFT + the tool letter always picks the tool), CTRL+Z undo, CTRL+Y or CTRL+SHIFT+Z redo, CTRL+S save, CTRL+O load (revert),
   arrows pan, HOME fits the grid, G grid lines, V 2D / 3D view (with world_cell and camera), P play / pause the game, F2 or
   ESC closes. Undo is a bounded ring of cell edits (a fill is one step).
   0.10.8: M the select tool (drag a rectangle); CTRL+C / CTRL+X copy / cut it (cells, and with a level its objects),
   CTRL+V pastes at the pointer (R turns the clipboard 90 degrees, F mirrors it, SHIFT+F top-bottom; click places; right
   or ESC gives up), DEL clears the selection (or deletes the selected object); O shows the layer above, then below,
   then none, faintly in 2D; T test-plays from the cell under the pointer (desc.playtest); TAB switches the panel's
   tabs. With desc.level: the Objects tab (an object kind's key selects it; left places one or picks and drags the one
   there; right removes it; its fields are edited in the panel: - / + steps, a click on a number or text types it,
   Enter keeps it) and the Layer tab (the layer's name and layer_fields). Every edit is one undo step, cells and
   objects together. A letter the editor uses (G P V O T, tools) goes to a tile with that key first; SHIFT + it then.
   0.10.3 the editor's own 3D view (with `tile_mesh`): the grid as the game builds it (walls raised, doors, lifts and kit),
   seen from an orbit camera of the editor's own, over the whole editing area; paint in it as in 2D (the pointer picks
   the floor cell). V cycles 2D -> own 3D -> the game's view (when `camera` is given too). In the 3D view: middle drag
   (or SPACE + right drag) orbits, SHIFT + middle drag (or SPACE + left drag) pans, wheel zooms, arrows pan, HOME fits.
   It is drawn with the game's lights, sky and post (the lights the game added in draw() light it too).
   0.10.3 layers (`layers`, `layer_count`): several grids of the same size and type (a ship's decks, a building's
   floors), PAGE UP / PAGE DOWN switch; undo goes back across layers. `changed` and `save` see arc_editor_layer().
   Agent commands (headless level editing; these change the grid directly and, like `set`, are not part of recordings):
   editor [on|off] | editor paint X Y CH | editor fill X0 Y0 X1 Y1 CH | editor flood X Y CH | editor undo | editor redo |
   editor save | editor load | editor dump (the grid as `ARC editor row` text lines) | editor tile CH | editor tool NAME |
   editor play|pause | editor info | editor layer N | editor view 2d|3d|game | editor orbit YAW PITCH DIST (degrees,
   cells). CH is a character ('#'), or a number with '=' in front ("=12") for int grids.
   0.10.8: editor object add X Y KIND [name=value ...] | object set N NAME [VALUE] | object move N X Y | object del N |
   object select N | object list | setting KEY [VALUE] | rename NAME | select X0 Y0 X1 Y1 | select none | copy | cut |
   paste X Y | clear | rotate | flip h|v | onion above|below|off | playtest X Y | tab tiles|objects|layer | kind KIND.
   dump's editor object adds tab, onion (the layer shown, -1 none), pasting, selection, clipboard (its size), and with a
   level: file, layer_name, object_kind, object_selected, settings, objects ({i, kind, x, y, fields}).
   --editor [FILE] starts in the editor (and F2 works on any build); FILE is read into desc.level when the game gives
   one (arc_editor_launch_file() says which).

```c
enum { ARC_EDITOR_CHAR = 0, ARC_EDITOR_U8 = 0, ARC_EDITOR_I32 = 1 };    // cell types: one byte (chars) or int32
typedef struct { int id; const char *name; uint32_t rgb; arc_key key; arc_texture icon; float sx, sy, sw, sh; int layer; } arc_editor_tile;
```

0.10.3 one mesh of a cell in the editor's own 3D view. model is in cell units around the cell's floor centre: a unit
   cube scaled (1, 2, 1) and moved up 1 is a wall two cells high filling the cell.

```c
typedef struct { arc_mesh mesh; arc_m4 model; uint32_t tint; } arc_editor_part;
#define ARC_EDITOR_PARTS 8
```

0.10.8 a field of an object kind (or of a layer's settings), edited in the editor's panel and kept in the arc_level as
   text. FACING: 0 = up the grid (-y), turning clockwise: 0..3 (N E S W), or 0..7 (N NE E ... NW) with max 7.

```c
enum { ARC_FIELD_INT, ARC_FIELD_NUM, ARC_FIELD_TEXT, ARC_FIELD_FACING, ARC_FIELD_CHOICE, ARC_FIELD_BOOL };
typedef struct { const char *name; int type; float min, max, step; const char *def; const char *choices; bool optional; } arc_editor_field;
```

0.10.8 a kind of object the editor places (a droid spawn, a camera, a lift link)

```c
typedef struct { const char *kind; const char *name; uint32_t rgb; arc_key key; arc_texture icon; float sx, sy, sw, sh; const arc_editor_field *fields; int field_count; } arc_editor_object;
enum { ARC_EDITOR_VIEW_2D, ARC_EDITOR_VIEW_GAME, ARC_EDITOR_VIEW_3D };
typedef struct { const char *name; int width, height; void *cells; int cell_type; int stride; const arc_editor_tile *tiles; int tile_count; int erase_tile; float cell_px; int undo_cells; void *user; void (*changed)(int x, int y, int old_id, int new_id, void *user); bool (*save)(void *user); bool (*load)(void *user); bool (*validate)(char *msg, size_t n, void *user); bool any_build; bool start_playing; arc_v3 origin; float world_cell; void (*camera)(arc_camera *out, void *user); int (*tile_mesh)(int x, int y, int id, arc_editor_part *out, int max, void *user); void *const *layers; const char *const *layer_names; int layer_count; arc_level *level; const char *level_path; const arc_editor_object *objects; int object_count; const arc_editor_field *layer_fields; int layer_field_count; int (*object_mesh)(int layer, int index, arc_editor_part *out, int max, void *user); void (*objects_changed)(int layer, void *user); void (*playtest)(int layer, int x, int y, void *user); } arc_editor_desc;
ARC_API void arc_editor_begin(const arc_editor_desc *d);    // (again to switch grids: the undo history is cleared)
ARC_API void arc_editor_end(void);    // closes and forgets the grid
ARC_API void arc_editor_open(bool on);
ARC_API bool arc_editor_is_open(void);
ARC_API void arc_editor_set_playing(bool on);    // the game's tick() runs while the editor is open
ARC_API bool arc_editor_playing(void);
ARC_API int arc_editor_get(int x, int y);    // -1 outside the grid
```

edits, each one undo step (false / 0 when nothing changed or out of the grid); also what the agent commands do

```c
ARC_API bool arc_editor_paint(int x, int y, int id);
ARC_API int arc_editor_fill(int x0, int y0, int x1, int y1, int id);    // a filled rectangle (corners in any order)
ARC_API int arc_editor_flood(int x, int y, int id);    // the 4-connected area of the same id
ARC_API int arc_editor_line(int x0, int y0, int x1, int y1, int id);
ARC_API bool arc_editor_undo(void);
ARC_API bool arc_editor_redo(void);
ARC_API int arc_editor_undo_steps(void);    // steps that can be undone now
ARC_API bool arc_editor_save(void);    // calls desc.save; clears the "changed" mark
ARC_API bool arc_editor_load(void);
ARC_API bool arc_editor_dirty(void);    // edited since the last save or load
ARC_API const char *arc_editor_message(void);    // the last validate message ("" = fine)
ARC_API bool arc_editor_hover(int *x, int *y);    // the cell under the pointer last tick
ARC_API int arc_editor_layer(void);    // 0.10.3 the layer being edited (0 without layers)
ARC_API void arc_editor_set_layer(int layer);
ARC_API void arc_editor_set_view(int view);    // 0.10.3 ARC_EDITOR_VIEW_*; one the desc can't show is ignored
ARC_API int arc_editor_view(void);
```

0.10.8 objects, settings, copy and paste, onion skin, test-play: each edit one undo step, on the current layer

```c
ARC_API arc_level *arc_editor_level(void);
ARC_API int arc_editor_object_add(int x, int y, const char *kind);    // its index (fields get their defaults), -1
ARC_API bool arc_editor_object_set(int index, const char *key, const char *value);    // value NULL removes the field
ARC_API bool arc_editor_object_move(int index, int x, int y);
ARC_API bool arc_editor_object_delete(int index);
ARC_API int arc_editor_object_selected(void);    // -1 = none
ARC_API bool arc_editor_set_setting(const char *key, const char *value);    // the current layer's (NULL removes)
ARC_API bool arc_editor_set_layer_name(const char *name);
ARC_API bool arc_editor_select(int x0, int y0, int x1, int y1);    // a rectangle (corners in any order); x0 < 0 = none
ARC_API bool arc_editor_copy(bool cut);    // the selection's cells and objects to the clipboard
ARC_API bool arc_editor_paste(int x, int y);    // the clipboard's top-left at x, y
ARC_API bool arc_editor_clear_selection(void);    // erase tile + no objects in the selection
ARC_API void arc_editor_rotate_clipboard(void);    // 90 degrees clockwise (FACING fields turn with it)
ARC_API void arc_editor_flip_clipboard(bool vertical);    // left-right (or top-bottom)
ARC_API void arc_editor_onion(int which);    // the layer above (-1: layer - 1), below (+1) or none (0), faint in 2D
ARC_API bool arc_editor_playtest(int x, int y);    // calls desc.playtest (false without it)
ARC_API const char *arc_editor_launch_file(void);    // --editor FILE's file ("" without)
```

---- Replay timeline (scrubber) ----
   A bar along the bottom of the screen over a running game or a replay (--replay FILE, e.g. a play session's input.rec):
   the current tick and the length, F8 bookmarks and arc_event markers (from events.jsonl beside the replay, and from
   this run), play / pause, one tick forward / back, speed 0.25x..8x, and click or drag to go to any tick.
   F9 shows it (with dev keys, or in any --replay run); --timeline opens it at the start. Keys while it is open: SPACE
   play / pause, LEFT / RIGHT (or , .) a tick back / forward, - = slower / faster, HOME / END the start / the end. In a live
   game those keys stay the game's while it plays: only when paused does the timeline take them; clicks on the bar never
   reach the game. A paused desktop game runs no ticks but keeps drawing. Headless runs ignore the pause (`step` steps).
   Going back needs the game's save_state / load_state: ARC keeps snapshots every 60 ticks (spaced out further on long
   runs) and re-simulates silently from the nearest one, so seeking is exact: the state at tick T is the same whether
   you seek there or play straight to it. Without the callbacks only forward seeking works (a game cannot be restarted
   inside one run: replay it again from the start). Going forward in a replay re-simulates silently, without drawing.
   In a live game the timeline's end is the newest tick played; going back and playing again replays the recorded input
   up to there and then hands control back to the player. While recording (--record, play sessions) going back is
   allowed this way and the recording stays exact (the past is only replayed, never re-recorded).
   Agent commands: timeline [on|off] | timeline seek T | timeline step [N] | timeline back [N] | timeline play | timeline pause |
   timeline speed X | timeline marks (prints `ARC mark tick=N kind=bookmark|event name=...`) | timeline info.
   Display only: nothing here changes what a tick computes.

```c
ARC_API void arc_timeline_show(bool on);
ARC_API bool arc_timeline_shown(void);
ARC_API bool arc_timeline_seek(uint64_t tick);    // false (and an "ARC error" line) if it can't get there
ARC_API void arc_timeline_set_paused(bool paused);
ARC_API bool arc_timeline_paused(void);
ARC_API void arc_timeline_set_speed(float x);    // clamped to 0.25..8
ARC_API uint64_t arc_timeline_length(void);    // the replay's length, or the newest tick of a live game
ARC_API int arc_timeline_mark_count(void);
ARC_API bool arc_timeline_mark(int i, uint64_t *tick, const char **name, bool *bookmark);
```

## Multiplayer (0.8): deterministic maths, turn-based LAN sessions


---- Deterministic maths ----
   The C library's sinf, expf, powf... differ in the last bit between compilers and platforms (MSVC, glibc, MinGW,
   Emscripten), so two machines running the same rules drift apart. These are computed by ARC itself and give the same
   bits everywhere (built with -ffp-contract=off, as build.sh does; never -ffast-math), within 1 ulp of the exact result.
   In a multiplayer game use them (and plain + - * / and arc_sqrtf, which IEEE makes exact) for everything that changes
   game state; drawing can keep using anything. ARC's own helpers (arc_m4_rotate_*, quaternions, arc_v3_len...) still use
   the C library so old games draw exactly as before: don't feed their results into game state of a multiplayer game.

```c
ARC_API float arc_sinf(float x);
ARC_API float arc_cosf(float x);
ARC_API float arc_tanf(float x);
ARC_API float arc_asinf(float x);
ARC_API float arc_acosf(float x);
ARC_API float arc_atanf(float x);
ARC_API float arc_atan2f(float y, float x);
ARC_API float arc_sqrtf(float x);
ARC_API float arc_hypotf(float x, float y);
ARC_API float arc_expf(float x);
ARC_API float arc_exp2f(float x);
ARC_API float arc_logf(float x);
ARC_API float arc_log2f(float x);
ARC_API float arc_log10f(float x);
ARC_API float arc_powf(float x, float y);
ARC_API float arc_floorf(float x);
ARC_API float arc_ceilf(float x);
ARC_API float arc_roundf(float x);    // halfway cases away from zero
ARC_API float arc_truncf(float x);
ARC_API float arc_fmodf(float x, float y);
ARC_API float arc_fabsf(float x);
```

---- Sessions: turn-based multiplayer over a LAN, hosted by a player's own game ----
   The game's rules already run on text commands (its command() callback, as agents type them). In multiplayer a command
   is the network message:
     - The game calls arc_net_command("move 3 5 7") instead of changing its state. The host gives every command the next
       sequence number and sends it to everyone, the sender included; every machine (the host too) applies them strictly
       in sequence order through the game's own command() callback.
     - Where: at the start of a tick, after input is read and before tick() (and before the tile editor): every command
       that has arrived is applied then, in order. A game without arc_app.net_event stops the batch after each sequenced
       event (start, join, leave, turn...), so tick() sees that event (arc_net_event) before any later command.
       A command takes effect one tick after arc_net_command at the soonest; different machines apply it on different
       ticks, so the rules must not depend on when: change game state ONLY in command() (and in the sequenced events),
       never in tick(). tick() may animate, but whatever it changes is not game state (keep it out of net_state).
     - Inside command(), arc_net_command_from() is the player who sent it (-1 for commands typed by an agent or the
       console, which are applied at once on this machine only) and arc_rand_*() draws from the match's own generator
       (seeded from the host's seed), so random rules (hit rolls) agree everywhere whatever tick() drew.
     - Offline (no session) arc_net_command applies locally the same way (next tick, through command()), so
       single-player code needs no branches. Offline the local player is 0, the only player, and it is always its turn.
   Turns alternate: player 0 (the host) first, then by player index, skipping players who are not connected. Commands from
   a player whose turn it isn't are refused by the host (an ARC_NET_EV_REFUSED event for the sender), except those sent
   with ARC_NET_FREE (chat, ready, lobby settings). Before the match starts only free commands are accepted. If the
   player whose turn it is drops out, the turn waits for them (they can rejoin and carry on); the host may end it for them
   with arc_net_turn_end(). Join, leave, rejoin, ready, name, start and turn changes are part of the same sequence, so
   every machine sees them at the same point.
   Joining needs the same game name, game version string and ARC version as the host (else both sides get a clear
   "version mismatch" message). A player who drops out and comes back (same name, or the same running game) gets the
   commands they missed and catches up at once (arc_net_catching_up() is true meanwhile: skip sounds and effects).
   Desyncs: after every applied command each machine hashes its state (arc_app.net_state, else save_state, else the dump's
   "game" object, plus ARC's own session state) and sends the hash to the host. On a mismatch every machine prints
   "ARC error desync seq=N tick=T", gets ARC_NET_EV_DESYNC, and writes a report folder (net_desync/seqN_pP/, or
   --net-report DIR): info.json (hashes), dump.json, log.txt (the command log), state.bin (the hashed bytes) - all taken
   at the same sequence number on every machine, so an agent can diff them.
   Transport: TCP (port ARC_NET_PORT, or desc.port), LAN discovery over UDP (port ARC_NET_PORT + 1), polled once a frame on
   the main thread. Windows loads ws2_32.dll at run time (no extra libraries to link). The web build has no LAN sessions
   (browsers cannot accept connections): without a relay arc_net_host / arc_net_join fail with status
   ARC_NET_UNAVAILABLE; offline arc_net_command still works. With a relay set they go online (0.8.5, "Online" below).
   Agent commands: net host [PORT] | net join ADDR | net find | net games | net leave | net status | net players | net name NAME |
   net ready [0|1] | net start | net cmd TEXT | net free TEXT | net end_turn | net log [FILE] | net resume FILE |
   net sync | net wait started|turn|turn_number N|connected|players N|seq N|desync|status NAME [SECONDS] (scripts pause until
   then; `players N` = exactly N connected; `turn` = my turn with nothing of mine in flight) | net wait time SECONDS (0.8.5:
   real seconds, the network running).
   `dump` gains a "net" object. --net-sim lag=MS,jitter=MS,loss=P,drop_at=TICK,rejoin_at=TICK,timeout=MS,seed=N
   simulates a bad network on this machine's sends (deterministic for a seed). See docs/MULTIPLAYER.md.

```c
#define ARC_NET_PORT 47800
#define ARC_NET_MAX_PLAYERS 8
enum { ARC_NET_OFFLINE, ARC_NET_HOSTING, ARC_NET_JOINING, ARC_NET_CONNECTED, ARC_NET_LOST, ARC_NET_REFUSED, ARC_NET_UNAVAILABLE };
enum { ARC_NET_FREE = 1 };    // arc_net_command_ex flags: allowed whoever's turn it is, and before the match starts
typedef struct { int port; const char *name; const char *game; const char *version; int max_players; uint64_t seed; void *user; int mode; int input_delay; int max_rollback; const char *relay; int turns; } arc_net_desc;
ARC_API void arc_net_setup(const arc_net_desc *d); /* describe this game once (in init): game, version, port, players; used by
arc_net_host(NULL), arc_net_join, the lobby and the `net` agent commands */ ARC_API bool arc_net_host(const arc_net_desc *d);
ARC_API bool arc_net_join(const char *address); /* "192.168.1.20", "pc-name:47800", "localhost"; while ARC_NET_LOST
(or with the same address again) it rejoins and catches up */ ARC_API void arc_net_leave(void);
ARC_API int arc_net_status(void);    // ARC_NET_OFFLINE / HOSTING / JOINING / CONNECTED / LOST / REFUSED / UNAVAILABLE
ARC_API const char *arc_net_status_name(int status);    // "offline", "hosting"...
ARC_API const char *arc_net_message(void);    // why the last refusal or loss happened ("" if none)
ARC_API const arc_net_desc *arc_net_desc_get(void);    // the host's settings (client: the name, game and version it joined)
ARC_API bool arc_net_is_host(void);
typedef struct { int index; char name[32]; bool connected; bool ready; bool in_match; bool local; int ping_ms; } arc_net_player;
ARC_API int arc_net_players(arc_net_player *out, int max);    // players who ever joined, by index; returns the count
ARC_API int arc_net_local_player(void);    // this machine's player index (0 offline, -1 before a join is accepted)
ARC_API void arc_net_set_name(const char *name);    // before or during a session (sequenced: everyone sees it change)
ARC_API const char *arc_net_name(void);
ARC_API void arc_net_set_ready(bool ready);    // lobby ready flag (sequenced)
ARC_API bool arc_net_start(void);    // host: start the match (everyone starts at the same sequence point)
ARC_API bool arc_net_started(void);    // the start has been applied here
ARC_API uint64_t arc_net_seed(void);    // the match seed (0 before the start)
ARC_API bool arc_net_command(const char *text);    // send a game command (see above); false = refused here at once
ARC_API bool arc_net_command_ex(const char *text, int flags);
ARC_API int arc_net_command_from(void);    // inside command(): the sending player, -1 when not a net command
ARC_API bool arc_net_applying(void);    // inside command(): a net command is being applied
ARC_API bool arc_net_catching_up(void);    // applying a backlog (a rejoin) or re-simulating (seek): no effects
ARC_API int arc_net_turn_owner(void);    // whose turn it is (-1 before the start; 0 offline)
ARC_API int arc_net_turn(void);    // the turn number, 1 at the start (offline: 1 + turns ended)
ARC_API bool arc_net_my_turn(void);    // started (or offline) and it is the local player's turn
ARC_API bool arc_net_turn_end(void);    // end the local player's turn (sequenced like a command)
ARC_API uint32_t arc_net_seq(void);    // the sequence number of the last command applied here
```

Events (also given to arc_app.net_event): poll until false, e.g. in tick(). The queue holds the last 64.

```c
enum { ARC_NET_EV_NONE, ARC_NET_EV_START, ARC_NET_EV_JOINED, ARC_NET_EV_LEFT, ARC_NET_EV_REJOINED, ARC_NET_EV_TURN, ARC_NET_EV_READY, ARC_NET_EV_NAME, ARC_NET_EV_CONNECTED, ARC_NET_EV_DESYNC, ARC_NET_EV_HOST_LOST, ARC_NET_EV_REFUSED, ARC_NET_EV_VERSION, ARC_NET_EV_WAITING, ARC_NET_EV_RESUMED, ARC_NET_EV_DROPPED, ARC_NET_EV_ROOM, ARC_NET_EV_TURN_DONE };
struct arc_net_ev { int type; int player; uint32_t seq; bool sequenced; char text[160]; };
ARC_API bool arc_net_event(arc_net_ev *ev);
```

LAN games: arc_net_find() broadcasts a question and hosts answer over about a second (call it again to refresh).

```c
typedef struct { char name[48], game[48], version[32], arc_version[16]; char address[64]; int players, max_players; bool started; } arc_net_game;
ARC_API void arc_net_find(void);
ARC_API bool arc_net_finding(void);
ARC_API int arc_net_games(arc_net_game *out, int max);    // answers so far (all games: compare .game yourself)
```

The command log: every sequenced command of the session, in order (text lines; also `net log`). A host can resume a
   match from a saved log: arc_net_host, then arc_net_resume(path): it re-applies the log, and the players rejoin by name
   and receive it.

```c
ARC_API bool arc_net_log_save(const char *path);
ARC_API bool arc_net_resume(const char *path);
ARC_API int arc_net_log_count(void);
ARC_API const char *arc_net_log_line(int i);    // "seq from kind flags text", into the frame arena (NULL past the end)
```

A ready-made lobby drawn with the UI kit (call it from tick() while the game shows its lobby): name, host, LAN games,
   join by address, the player list with ready flags, start (host) and leave. Mouse, keyboard and gamepad (the UI kit's
   focus keys). desc = how to host (NULL = defaults). Returns ARC_NET_LOBBY_STARTED once the match has started here, or
   ARC_NET_LOBBY_BACK when the player backs out (Back / ESC while offline); otherwise ARC_NET_LOBBY_OPEN.

```c
enum { ARC_NET_LOBBY_OPEN, ARC_NET_LOBBY_STARTED, ARC_NET_LOBBY_BACK };
ARC_API int arc_net_lobby(const arc_net_desc *desc);
```

simulate a bad network (also --net-sim): "lag=80,jitter=40,loss=0.05,drop_at=600,rejoin_at=900,timeout=10000,seed=3";
   NULL or "" turns it off (it applies to LAN links and to WebSocket / TCP links to a relay alike)

```c
ARC_API void arc_net_sim(const char *spec);
```

---- Online: the relay (0.8.5) ----
   Between machines that are not on one LAN, and from the web build, a session goes through arc_relay (tools/arc_relay,
   docs/RELAY.md): a small server with no game code that does what a LAN host does (numbers commands and sends them to
   everyone, refuses commands out of turn, sequences joins / leaves / rejoins / turn ends, compares the state hashes, keeps
   each room's log for rejoins) for rooms named by a short code ("K7QF": 4-6 letters and digits from an alphabet with no
   0/O or 1/I/L). Everything above works the same (commands, turns, events, hashes, desync reports, rejoin, net_sim,
   the `net` agent commands); what differs:
     - arc_net_host_online() asks the relay for a new room: this machine becomes its player 0, the room's host (status
       ARC_NET_JOINING, then ARC_NET_HOSTING and arc_net_is_host(); it starts the match and may end an absent player's
       turn). ARC_NET_EV_ROOM gives the code (also arc_net_room()); show it so the others can type it.
     - arc_net_join_online("K7QF") joins it (JOINING, then CONNECTED and ARC_NET_EV_ROOM). "ws://host:port/K7QF" also
       names the relay. A wrong code is refused (ARC_NET_REFUSED, "no room ... on the relay"); so are another game, game
       version or ARC version (as on a LAN), a full room, and a started match (except its players rejoining by name).
     - The room lives on the relay, not on a player's machine: a player who drops (the host too) reconnects by itself for
       30 s, or rejoins by name with the code; the match goes on meanwhile (an absent player's turn waits, as on a LAN).
       If the relay restarts, the first players back give it the room's log and play carries on (docs/RELAY.md).
     - Transports: the web build uses the browser's WebSocket (wss:// from an https page such as itch.io); desktop builds
       speak ws:// (plain: no TLS on desktop) or tcp:// (ARC's own protocol) to the relay's plain port. The relay
       speaks both on one port; a TLS proxy (Caddy, nginx) in front gives the web its wss://.
     - 0.10.4 rollback (real-time) matches play online too, desktop builds only (UDP): after the room's start, every
       player's datagrams go to the relay's UDP port (the same number as its TCP port) and on to the others, or straight
       to them once hole punching works (it is tried at once and keeps being checked; a path that goes quiet for 1.5 s
       falls back to the relay). The lowest player in the match is the hub, doing the LAN host's part (passing inputs on,
       deciding drops). The same arc_net calls as on a LAN; `dump` shows net.rollback.online (hub, each peer's path).
       --net-relay-only (or `net direct off`) keeps every datagram on the relay. A browser has no UDP: a web build that
       hosts a rollback room is refused, and one that joins plays alone.
     - On the web, arc_net_host / arc_net_join go online by themselves when a relay is set (arc_net_join takes the code),
       and only without a relay do they fail with ARC_NET_UNAVAILABLE (browsers cannot accept connections).
   Agent commands: net host_online | net join_online CODE | net room (prints "ARC net room CODE relay=URL player=N", also
   printed when the relay gives the room); --net-relay URL sets the relay (tests run a local arc_relay).

```c
ARC_API bool arc_net_host_online(void);    // ask the relay for a room (the setup's desc); false = no relay / refused
ARC_API bool arc_net_join_online(const char *code);    // "K7QF", "k7qf", "K7-QF" or "ws://relay:47880/K7QF"
ARC_API const char *arc_net_room(void);    // the room code ("" when not in a relay room)
ARC_API bool arc_net_relay_available(void);    // a relay this build can speak is set (the lobby shows the online buttons)
```

---- Simultaneous turns (0.8.5) ----
   arc_net_desc.turns = ARC_NET_TURNS_SIMULTANEOUS (the host's setting; LAN host and relay alike): from the start every
   player in the match may send commands during a turn (in any order: the host numbers them as they arrive, and every
   machine applies them in that one order), and the turn ends when every connected player of the match has called
   arc_net_turn_end() (once each: commands or another end of turn after it are refused until the next turn). Each end
   of turn is sequenced (ARC_NET_EV_TURN_DONE, with who is still to finish); after the last one comes ARC_NET_EV_TURN
   (player ARC_NET_EVERYONE), so "next minute" runs in the TURN event, at the same point on every machine. A player who is
   not connected does not hold the turn up: if a drop leaves only players who have ended it, the turn ends right after
   that player's LEFT event; one who rejoins before the turn ends must end it too. arc_net_turn_owner() is
   ARC_NET_EVERYONE, arc_net_my_turn() is "I have not ended this turn yet". The game still checks in command() that the
   sender may order that unit (arc_net_command_from()).

```c
enum { ARC_NET_TURNS_ALTERNATE = 0, ARC_NET_TURNS_SIMULTANEOUS = 1 };
#define ARC_NET_EVERYONE (-2)    // arc_net_turn_owner() and the TURN event's player, simultaneous turns
ARC_API unsigned arc_net_turn_waiting(void);    // bit i = player i still to end this turn (alternating: the owner's bit)
```

---- Real-time rollback matches (0.8.1) ----
   For action games: every machine runs the whole simulation every tick from every player's INPUT (not commands), GGPO
   style. Set the match up with the session above (host, join, ready, start: the lobby, LAN discovery and version check
   are the same) and arc_net_desc.mode = ARC_NET_ROLLBACK (the host's mode decides). From the start the match runs over UDP:
     - Every tick each machine packs its local player's input (arc_app.net_input, or ARC's default: keys, pad buttons and
       sticks) and sends it to the others (each datagram repeats every input not yet acknowledged, so a lost one costs
       nothing). The input is used `input_delay` ticks later (2 by default), which hides that much lag completely.
     - tick() reads every player's input for this tick with arc_net_input(player, &size) (or arc_net_key_down(player, k),
       arc_net_axis... with the default input) and nothing else: no arc_key_*, no arc_mouse in the rules.
     - An input that has not arrived yet is PREDICTED (the player's last known input repeated) and the tick runs at once.
       When the real input arrives and differs, ARC loads the snapshot from before that tick (arc_app.save_state plus
       ARC's own state: arc_rand_*, particles, emitters, decals, UI focus, arc_vars) and runs the ticks again up to now,
       silently, before the next tick (arc_net_resimulating() is true meanwhile). So save_state / load_state are required,
       and must hold EVERYTHING tick() changes that a later tick reads.
     - A machine never runs more than max_rollback ticks (8) past the inputs it has: then it waits (and after half a
       second ARC_NET_EV_WAITING names the player it waits for; ARC_NET_EV_RESUMED when it runs again). Machines keep
       their clocks together: the one that is ahead skips a tick now and then (time sync), so rollbacks stay short.
     - Side effects: sounds started in tick() are played once: a sound a re-simulated tick plays again (the same sound,
       the n-th time in that tick) is not repeated, one that a corrected tick plays for the first time starts late, and
       one whose tick no longer plays it is stopped. Music calls are ignored while re-simulating. Particles, emitters and
       decals are rolled back with the snapshot. arc_event lines are held until their tick is confirmed (every input
       known), so the log holds exactly the confirmed match, identical on every machine. Anything else tick() does
       outside the game state (camera shake, UI toasts, rumble) must be skipped while arc_net_resimulating(), or be
       worked out in draw() from the state.
     - arc_rand_* is reseeded from the match seed at match tick 0; arc_net_match_tick() counts ticks from there (use it,
       not arc_tick_count(), in the rules: machines start at different ARC ticks). The START event arrives at match tick 0,
       before tick(): set the match up there (from arc_net_seed()). Deterministic maths and the other rules above apply.
     - Desyncs: every 10 confirmed ticks each machine hashes its state (arc_app.net_state, else save_state's bytes) and
       the hashes are compared: on a mismatch "ARC error desync tick=T ..." and a report folder (net_desync/tickT_pP/:
       info.json, state.bin at tick T on every machine, dump.json, inputs.txt).
     - A player whose packets stop for 3 s (or whose link closes) is dropped: ARC_NET_EV_DROPPED, and from the tick the
       host decides their input is all zeros everywhere (arc_net_input_dropped). Rejoining a rollback match is not supported.
     - Up to 4 players (the host relays inputs between clients). --record records every player's confirmed input, so
       --replay reproduces the match offline exactly (with seek and the timeline). Offline (no match) arc_net_input(0)
       is this machine's input with no delay, so single-player code needs no branches.
   Agent commands: net mode rollback|turns | net delay N | net rollback N | net input HEX|off | net input P HEX (synctest) |
   net stop_at T | net stats | net synctest N [PLAYERS] | net wait tick N|confirmed N|waiting|resumed|dropped.
   --net-sim also takes dup=P (duplicate datagrams) and freeze_at=TICK,freeze_ms=MS (this machine hangs); lag, jitter (which
   reorders datagrams) and loss apply to every datagram. --net-synctest N checks a game offline: every tick it rolls back N
   ticks and compares the state hashes (an "ARC error synctest" + report folder when save_state misses something).
   See docs/MULTIPLAYER.md ("Real-time games").

```c
enum { ARC_NET_TURNS = 0, ARC_NET_ROLLBACK = 1 };
#define ARC_NET_NO_DELAY (-1)
#define ARC_NET_INPUT_MAX 64    // bytes of one player's input for one tick
#define ARC_NET_MAX_ROLLBACK 15
#define ARC_NET_ROLLBACK_PLAYERS 4
```

arc_app.net_input (optional): pack this machine's input for one tick into buf (at most cap = ARC_NET_INPUT_MAX bytes)
   and return its size. Called once per new tick, before tick(), never while re-simulating; read arc_key_*, arc_mouse,
   arc_pad_axis there. Keep it small and quantised (bits, bytes): it is sent every tick.

```c
ARC_API bool arc_net_rollback(void);    // a rollback match is running here (live, replayed or synctest)
ARC_API int64_t arc_net_match_tick(void);    // the match tick being simulated (0 at the start); -1 outside a match
```

inside tick(): a player's input for this tick (confirmed, or predicted until it arrives); size gets its byte count.
   Bytes past size read as zeros (up to ARC_NET_INPUT_MAX). NULL for a player not in the match. Offline: player 0 is this
   machine's input for this tick.

```c
ARC_API const void *arc_net_input(int player, int *size);
ARC_API bool arc_net_input_predicted(int player);    // that input is a guess so far (presentation only: never in rules)
ARC_API bool arc_net_input_dropped(int player);    // the player has left the match as of this tick (input all zeros)
```

ARC's default input (games without arc_app.net_input): every key and pad button, and the pad's axes

```c
ARC_API bool arc_net_key_down(int player, arc_key k);
ARC_API bool arc_net_key_pressed(int player, arc_key k);    // down this tick, not the tick before
ARC_API float arc_net_axis(int player, arc_axis a);    // -1..1 (0..1 triggers), in 1/127 steps
ARC_API bool arc_net_resimulating(void);    // re-running ticks after a correction: no side effects outside the state
ARC_API void arc_net_set_input_delay(int ticks);    // 0..10, this machine, at any time (also `net delay N`)
ARC_API int arc_net_input_delay(void);
ARC_API int arc_net_waiting_for(void);    // the player the match waits for (-1 = running)
typedef struct { bool active; int64_t tick; int64_t confirmed; int input_delay, max_rollback, players, local; int rollbacks; int max_depth; float avg_depth; int64_t resim_ticks; int64_t predicted; int64_t mispredicted; int skipped; int64_t stalled_ms; int ping_ms; float frame_advantage; int sounds_played, sounds_deduped, sounds_late, sounds_cancelled; int64_t packets_sent, packets_received; int64_t last_hash_tick; uint64_t last_hash; int64_t desync_tick; } arc_net_rb_stats;
ARC_API arc_net_rb_stats arc_net_stats(void);
} #endif #endif
```
