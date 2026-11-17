# Request from Droids: shots and hits (hitscan and projectiles, tracers, impacts)

**Why.** Droids, HEATLINE, the shooter template and Moonbase Raid each write the same shooting code:
- walk a shot along the grid or a ray;
- stop at the first wall, door or body;
- draw a tracer;
- spawn sparks and a scorch;
- play a sound where it hits.

Droids does it with `shot_blocked()` per cell and its own `add_fx` pool (lines plus a life), which can't hit things
that aren't on the grid. ARC now has the pieces (physics raycasts, collision worlds, decals, particles, positional
sound, lights) but nothing that ties them into "fire this".

**Ask.** A small, deterministic shots module (plain data, fits `save_state`):

```c
typedef struct arc_shot_desc {
    arc_v3 from, dir; float range;
    float speed;              /* 0 = hitscan (instant), else a projectile that travels (and can be dodged) */
    float radius;             /* 0 = a ray; >0 = a sphere sweep (fat bullets, plasma) */
    uint32_t mask;            /* collision layers it hits */
    int owner;                /* never hits its own owner */
    uint32_t user;            /* game data, handed back on the hit */
    /* looks (all optional) */
    uint32_t tracer_rgb; float tracer_width, tracer_life;
    arc_texture impact_decal; float decal_size;
    int impact_particles;     /* an arc_fx preset */
    arc_sound impact_sound;
    float flash_light;        /* a short point light at the muzzle / impact (0 = none) */
} arc_shot_desc;

int  arc_shot_fire(const arc_shot_desc *d);       /* hitscan resolves now; projectiles step with arc_shots_update */
void arc_shots_update(float dt);
int  arc_shots_hits(arc_shot_hit *out, int max);  /* this tick's hits: point, normal, what (body id / grid cell / world), user */
void arc_shots_draw(void);                        /* tracers and projectiles */
void arc_shots_set_world(arc_cworld w, arc_physics_world *p, bool (*grid_blocks)(int x, int z, void *u), void *u);
```

- Grid, collision-world and physics targets all at once, nearest hit wins. Droids' doors that are half open block
  through the grid callback.
- Bodies the game moves itself (droids aren't physics bodies) through a short per-tick list of spheres or capsules
  with ids: `arc_shots_targets(...)`.
- The hits are reported; the game applies damage.
- **Must not change:** anything for games that don't use it.

**Game side (ready).**
- Droids would replace `shot_blocked` / `add_fx` / its tracer code with `arc_shot_fire`.
- Blaster shots (the weapons locker) become projectiles with a flash light, so they light the corridor as they fly
  (0.9.5 lights).
- Scorch decals on walls, and sparks.

## Acceptance

In words:
- A hitscan shot at a wall 5 units away hits at 5 with the wall's normal.
- With a target sphere at 3 in front of the wall, it hits the target.
- Its own owner is never hit.
- A projectile at 10 units/s reaches a target 5 away in 0.5 s (±1 tick), and misses if the target moves out of the
  way first.
- A fat projectile grazes a corner that a ray would miss.
- The same shots give byte-identical results every run and across save / load.
- The tracer and decal appear in `frame` (decal count, tracer lines).

---

## Done in ARC 0.10.0 (15 Nov 2026)

Almost exactly the API you sketched (feature macro `ARC_HAS_SHOTS`).

- `arc_shots_set_world(cworld, physics, grid_blocks, user)` and `arc_shots_set_grid(cell, origin_x, origin_z)`.
  Your half-open doors block through the grid callback. Any of the three can be left out; nearest hit wins.
- `arc_shots_targets(list, n)` each tick for things that aren't physics bodies (droids). Each target is
  `{ id, layer, a, b, radius }`: a sphere (a == b) or a capsule.
- `arc_shot_fire(&desc)` returns the shot's number.
  - `speed` 0 is hitscan (resolved at once); otherwise a projectile moved by `arc_shots_update(dt)`, which can be
    dodged.
  - `radius` > 0 sweeps a sphere, so fat plasma grazes corners.
  - `mask` is the physics and target layers; `owner` (target id or body) is never hit; `user` comes back on the hit.
- `arc_shots_hits(out, max)`: this tick's hits.
  - Each hit has `kind` (TARGET / GRID / WORLD / BODY), `id` (target id, body, or grid cell), `cell_x` / `cell_z`,
    `point`, `normal`, `dist`, `owner` and `user`.
  - The game applies damage.
  - `arc_shot_cast` is the same walk with no state, for aim checks.
- Looks, all optional:
  - `tracer_rgb` / `_width` (0 = a thin line, else an additive glowing streak) / `_life`;
  - `projectile_size`, the streak length of a bolt;
  - `impact_decal` / `decal_size` / `decal_argb`;
  - `impact_fx`: an int, as you wrote, from `arc_shots_fx(&my_arc_fx)`;
  - `impact_sound` (3D);
  - `flash_light`: a short light at the muzzle and the impact, and a light carried by projectiles, so blaster bolts
    light the corridor as they fly.
- Call `arc_shots_draw()` in draw. `frame` has `"shots": {"projectiles", "tracers", "tracer_lines", "decals"}`.
- State: in ARC's own snapshots (rewind, `state_save`, rollback) automatically. `arc_shots_save` / `_load` are for
  your own save format. No pointers inside, so the bytes are the same on every machine.
- Games that don't call it see nothing new.

Acceptance (ARC's self-test):
- **Hitscan:** a shot at a grid wall 5 away hits at 5.000 with normal −x. A target sphere at 3 is hit first. Its
  owner is passed, and the capsule behind is hit. A layer mask skips a target.
- **Projectile:** at 10 units/s it reached a target 5 away after 31 ticks (0.5 s ± 1). It flew on to the wall when
  the target stepped aside at tick 15.
- **Fat shots:** a 0.2 sphere grazes a grid corner that the ray misses; the same against a collision-world box
  corner.
- **Physics:** a body is hit; with it as owner, the one behind is hit.
- **All worlds at once:** the nearest wins.
- **Determinism:** 10 projectiles with a save / load halfway give byte-identical hits.
- **Frame:** `frame` lists the tracer (`[[0,0.5,0.5,5,0.5,0.5]]`) and it goes away after its life.
- **Pictures:** a post-test look (a wide tracer, a plasma bolt lighting a wall, a scorch) on the software renderer,
  GL and D3D11.
