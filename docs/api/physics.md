# Physics (0.9.4): deterministic rigid bodies

_Generated from `include/arc.h` by `tools/gen_api_docs.py` - do not edit by hand._

Part of **Physics (0.9.4): deterministic rigid bodies** · [quick reference](../QUICK.md) · [full API](../API.md)


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
