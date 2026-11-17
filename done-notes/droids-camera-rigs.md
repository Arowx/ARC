# Request from Droids: camera rigs (top-down, follow, orbit, first person) that keep out of walls and blend between views

**Why.** Every action game on ARC builds its own camera. Droids has three views (top-down, third person, first
person; C or 1/2/3 switches) in `src/view.h` (`dv::Camera`, about 50 lines):
- **Top-down:** orthographic, clamped to the deck bounds.
- **Third person:** pulls in when a wall is behind the droid. It marches 32 steps along a grid callback
  `blocked(x, z)`, so it only knows about grid walls, not meshes or doors.
- **First person:** mouse look through ARC 0.6.7 mouse capture.

What it lacks, and what every game will want:
- the switch between views is a cut, not a blend;
- the third-person pull-in snaps, so the camera pops when a wall passes behind;
- the camera doesn't follow smoothly;
- there's no shake for hits and explosions;
- the collision only works for grid games.

HEATLINE, Moonbase Raid and the templates (shooter, racer, platformer) each carry their own version of the same
code.

**Ask.** A small camera-rig module (plain data, fits `save_state`; deterministic so recordings and rewinds replay
the same camera):

```c
typedef enum { ARC_RIG_TOPDOWN, ARC_RIG_FOLLOW, ARC_RIG_ORBIT, ARC_RIG_FIRST, ARC_RIG_FIXED } arc_rig_kind;
typedef struct arc_cam_rig {
    arc_rig_kind kind;
    arc_v3 target;            /* what it looks at / follows (the game sets it each tick) */
    float yaw, pitch;         /* the game's facing, or mouse look (FIRST, ORBIT) */
    float distance, height;   /* FOLLOW / ORBIT: wanted distance behind and height above */
    float look_ahead;         /* FOLLOW: aim this far in front of the target */
    float ortho_half;         /* TOPDOWN: half the view height in world units */
    float bounds[4]; bool has_bounds;   /* TOPDOWN: keep the view inside x0 z0 x1 z1 */
    float fov_y;              /* perspective rigs */
    float follow_lag;         /* seconds to close most of the gap (0 = locked on) */
    float collide_radius;     /* FOLLOW / ORBIT: keep a sphere this big out of walls (0 = off) */
    /* runtime (ARC fills these) */
    arc_v3 eye_now, target_now; float dist_now, shake;
} arc_cam_rig;

void arc_cam_rig_update(arc_cam_rig *r, float dt);            /* lag, collision, shake decay */
arc_camera arc_cam_rig_camera(const arc_cam_rig *r);           /* what to pass to arc_set_camera */
void arc_cam_rig_blend(arc_cam_rig *from, const arc_cam_rig *to, float seconds);   /* smooth switch, incl. ortho <-> perspective */
void arc_cam_rig_shake(arc_cam_rig *r, float amount);          /* trauma-style shake, decays on its own */
```

- **Collision:**
  - The rig sweeps a sphere of `collide_radius` from the target to the wanted eye, against everything the game registers:
    - `arc_cworld` (collision world),
    - `arc_physics` statics,
    - a grid callback, as Droids has now.
  - It pulls in at once when blocked, and eases back out over `follow_lag`, so it never pops outward.
  - Something like `arc_cam_rig_set_world(r, cworld, physics, grid_cb)`, all optional.
- **Blend:** blends position, target and FOV. For orthographic ↔ perspective it blends through a matched view, so
  a top-down ↔ third-person switch doesn't jump. Done is reported (`arc_cam_rig_blending()`).
- **Agent command:** `cam rig KIND` and `cam shake N`, and the rig's state in `dump` / `frame.camera`.
- **Must not change:** existing `arc_camera` use; games that don't use rigs see nothing new.

**Game side (ready).**
- Droids will replace `dv::Camera::build()` with three rigs (TOPDOWN with the deck bounds, FOLLOW with
  `collide_radius` 0.3 and the grid callback, FIRST) and blend on C / 1 / 2 / 3.
- It will shake on hits, explosions and losing a transfer.
- `view topdown|third|first` keeps working.

## Acceptance

In words (ARC's own tests):
- A FOLLOW rig behind a target with a wall 1 unit behind it: the eye ends inside the wall's near side minus the radius, never past it, and eases back to `distance` within ~3 × `follow_lag` when the wall moves away.
- No frame-to-frame outward jump larger than the lag allows while a wall passes behind (pop test over 200 ticks).
- A blend from TOPDOWN to FOLLOW over 0.5 s: every in-between camera is valid, there's no sudden jump in the centre
  of the screen, and it ends exactly on the FOLLOW camera.
- The same inputs give the same eye positions every run, and across a `save_state` / `load_state`.

```arc-check
game samples/demo/demo.c
do step 30; cam rig follow; step 60
expect frame.camera exists
```

---

## Done in ARC 0.10.0 (15 Nov 2026)

`arc_cam_rig` as you sketched (feature macro `ARC_HAS_CAM_RIG`), with a few additions.

**Fields.**
- Your fields, plus `aim_height` (aim above the target), `eye` (FIXED), `znear` / `zfar`, and runtime fields ARC
  fills.
- Facing: `yaw` 0 looks along +z (a model drawn with `arc_m4_trs_y(pos, yaw, ...)` whose front is +z). `pitch` > 0
  looks up (FIRST) or raises the eye (ORBIT).
- TOPDOWN: `yaw` turns the map (screen-up is the facing). Use `yaw = PI` for −z up, and `bounds` keeps the view on
  the deck.

**Each tick and frame.**
- Set `target` (and `yaw` / `pitch`) and call `arc_cam_rig_update(&rig, dt)`.
- In draw: `arc_camera c = arc_cam_rig_camera(&rig); arc_set_camera(&c);`.

**Collision.** `arc_cam_rig_set_world(cworld, physics, phys_mask, grid_blocks, user, cell)`, once for all rigs.
- It sweeps a `collide_radius` sphere from the aim point to the wanted eye against all three. The pointers live
  outside the rig, so the rig stays plain data.
- The eye pulls in at once and eases back out over `follow_lag` (at least 0.25 s), so it never pops outward.

**Blends and shake.**
- `arc_cam_rig_blend(&live, &next, 0.5f)`: `live` becomes `next` smoothly. Position, aim, up and field of view all
  blend.
- Orthographic ↔ perspective goes through a matched view: the aim point stays put while the field of view opens
  from almost nothing. `arc_cam_rig_blending()` tells you when it's done.
- `arc_cam_rig_shake(&rig, 0.5f)`: trauma-style, decays by itself. `arc_cam_rig_snap` after a teleport.

**Commands.**
- `cam` shows the rig's state.
- `cam rig topdown|follow|orbit|first|fixed`: blends the game's rig to that kind. In a game with no rig, it puts
  one over the game camera, following its aim. `cam rig off` removes it.
- `cam shake N`.
- `frame.camera.rig` gives kind, dist, shake and blending.
- A game's own `cam` command wins, so Droids' `view topdown|third|first` keeps working as it is. Existing
  `arc_camera` use is unchanged.

Acceptance (ARC's self-test):
- **Wall behind:** with a wall 1 unit behind the target, the eye stopped at z 0.310 (the wall face is at 0, radius
  0.3), never past it. When the wall went, it was back to 99% of the distance after 27 ticks (0.45 s; 3 ×
  follow_lag = 0.9 s), never stepping out faster than the lag allows.
- **Pop test:** over 200 ticks with a pillar passing behind, every outward step stayed within the lag; it pulled in
  when the pillar came.
- **Blend:** TOPDOWN → FOLLOW over 0.5 s. Every camera was valid, the aim point moved at most 0.016 a tick, and the
  old aim point stayed within 1.9 px of the screen centre. It ends on the FOLLOW camera.
- **Determinism:** the same inputs are byte-identical, also across a save / load.
- Your arc-check block (`samples/demo`: `step 30; cam rig follow; step 60; expect frame.camera exists`) passes.
  `samples/action` checks `frame.camera.rig.kind` for topdown and orbit, plus shake.
