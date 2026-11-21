# Particles, trails, decals and stamps

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


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
