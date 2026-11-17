# Skeletal animation

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Graphics** · [quick reference](../QUICK.md) · [full API](../API.md)


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
