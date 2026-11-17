# Action (0.10.0): AI brains, senses, shots, camera rigs

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Action (0.10.0): AI brains, senses, shots, camera rigs** · [quick reference](../QUICK.md) · [full API](../API.md)


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
