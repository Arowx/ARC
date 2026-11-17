# Request from Droids: AI behaviour (state machines or behaviour trees) and senses (sight, hearing, memory)

**Why.** ARC 0.9.2 gives agents a way to get somewhere (navmesh, `arc_nav_agent`), but nothing for deciding what
to do or what they know.

Droids does both by hand:
- **Deciding** is one `ai_act()` function in `src/droids.cpp`: repair rounds, spot then fire, hunters close in,
  patrol and sometimes turn. It has an `alerted` flag, and stops chasing the moment sight is lost.
- **Sight** is `arc_grid_los` plus a Manhattan range, with no view cone and no memory. The player's own view is 9
  rays per droid between body edges (`visible_to_player`), with fade in and out.

What's missing:
- droids don't search where they last saw you;
- they don't hear gunfire or explosions;
- they don't call others;
- they don't flee when damaged, which the 3D Paradroid design wants (low classes fleeing, security droids
  converging).

Every game with enemies (HEATLINE, Moonbase Raid's aliens, the shooter template) will need the same pieces.

**Ask.** Two small, deterministic, plain-data modules (fit `save_state`, no allocation per tick):

1. **Behaviour.** One of these; ARC's choice, as long as it's data you can dump and test:
   - **AI state machine** (like `arc_anim_graph`, which already has states, conditions and triggers): states with
     enter / tick / exit callbacks, transitions on parameters, timers and triggers; or
   - **a small behaviour tree**: sequence, selector, parallel, inverter, cooldown, and leaf callbacks returning
     success / failure / running.

   Either way:
   - a per-agent blackboard of floats, ints and vectors;
   - the current state or node path in `dump` (`agent N state`), so tests can say "droid 3 is SEARCHING";
   - an agent command to force a state.

2. **Senses** (`arc_senses`):
   - **Sight:** a view cone (half-angle, range), a line-of-sight test the game supplies (a grid callback,
     `arc_cworld` ray or physics ray), several sample points on the target's body (as Droids' 9 rays), and a
     "seen for T seconds" build-up, so a glimpse doesn't alert instantly.
   - **Hearing:** `arc_senses_noise(pos, radius, kind, loudness)` events, heard by agents in range, optionally
     blocked or muffled by walls.
   - **Memory:** last known position and time per target, forgotten after N seconds, plus a "searching around last
     known" helper that picks navmesh points near it.
   - **Alerts:** an agent can share what it knows with others in a radius (droids calling security).
   - **Debugging:** `arc_senses_draw` draws cones and memories.

- **Must not change:** existing nav agents and `arc_grid_los`.

**Game side (ready).** Droids will move `ai_act()` onto states, as data so the adaptive difficulty can tune them:

| State | What it does |
|---|---|
| Patrol | walks its rounds |
| Repair | the 476's camera rounds |
| Alert | spotted something; builds up before acting |
| Attack | fires on the player |
| Hunt | closes in (hunter classes) |
| Search | goes to the last known position |
| Flee | low classes when damaged |
| Call security | brings others in |

Gunfire and explosions become noises, and the cameras' sightings become alerts. The player's own 9-ray view would
use the same sight test.

## Acceptance

In words (ARC's own tests):
- Sight:
  - a target inside the cone and range with a clear line is seen after the build-up time;
  - behind the agent, or behind a wall, it is never seen;
  - partly covered (some sample points blocked) builds up more slowly.
- Hearing: a noise of radius 5 is heard by an agent 4 away and not by one 6 away, nor by one 4 away behind a
  muffling wall when muffling is on.
- Memory: last known position kept, then forgotten after its time.
- Search: the agent walks to the last known point, then to nearby navmesh points.
- Behaviour: the transitions fire in order, the state shows in `dump`, and the same runs are byte-identical,
  including across a save / load.

---

## Done in ARC 0.10.0 (15 Nov 2026)

ARC chose the **state machine**: your state table maps onto it directly. Feature macros: `ARC_HAS_BRAIN`,
`ARC_HAS_SENSES`. `samples/action/action.c` is a small Droids-like deck with all of it (and shots and camera rigs).

**Behaviour (`arc_brain`).**
- Build an `arc_brain_def` once:
  - `arc_brain_def_state(&def, "PATROL", enter, tick, exit)` returns 0, 1, 2 ... in order;
  - `arc_brain_def_transition(&def, from, to, c0, c1)`, where conditions are `ARC_BRAIN_IF(kind, slot, value)`:
    blackboard floats `F_GE` / `F_LE`, ints `I_EQ` / `NE` / `GE` / `LE`, `TIME_GE` (seconds in the state),
    `TRIGGER` (a one-tick event);
  - `arc_brain_def_transition_fn` for a test of your own; `from = ARC_BRAIN_ANY` for "from anywhere" (Flee).
- Transitions are tried in the order added, one switch per update. Exit runs, then enter, then the new state's tick.
- Keep an `arc_brain` in each droid: state, time in state, blackboard (16 floats, 8 ints, 4 vectors), triggers, the
  last 8 changes. Call `arc_brain_update(&d->brain, &DEF, d, dt)` in tick. A callback can ask for a change with
  `arc_brain_go`.
- Everything is data, so the adaptive difficulty can tune thresholds and times in the def.
- Dump and tests: `arc_brain_watch("droid", &G.droid[0].brain, n, sizeof G.droid[0], &DEF)`.
  - `dump` gets `brains.droid[i].state`, `.time`, `.prev`, and named blackboard slots (`def.f_name[k]`).
  - `brain droid 3 SEARCH` forces a state. `brain` lists them all.

**Senses (`arc_senses`).** One struct in the game state: up to 64 agents, each remembering up to 4 targets.
- Each tick: `arc_senses_set(&s, i, pos, facing)` per droid, `arc_senses_noise(...)` for gunfire and explosions,
  then `arc_senses_update(&s, dt, targets, n, los, user)`.
- **Sight:**
  - `half_angle`, `range`, and `eye_height` per agent;
  - your LOS callback (your grid walk, or the ready-made `arc_senses_los_cworld` / `_physics`);
  - up to 9 sample points per target (your 9 body-edge rays);
  - `build_up` seconds: a fully visible target is seen after build_up, a partly hidden one more slowly;
    `awareness` goes 0..1;
  - while awareness builds, `pos` already says where the glimpse was, so Alert can turn toward it.
- **Hearing:** radius × loudness × the agent's `hearing`. Through a blocked line the radius is × `muffle`
  (0.5 by default; 0 = walls stop sound). A noise with a source target updates that target's memory as "heard".
- **Memory:** `arc_senses_knows(&s, i, target)` gives `pos`, `when`, `how` (seen / heard / told) and `seen_now`. It
  is forgotten `forget` seconds after the last news.
- **Alerts:** `arc_senses_alert(&s, i, radius)` (call security). **Search:** `arc_senses_search_points(navmesh,
  last_pos, radius, n, out)` gives navmesh points around it, nearest first, the same every time.
- `arc_senses_draw` draws cones (red when seeing), memory crosses and the last noise. `arc_senses_watch(&s,
  "droids")` puts them in `dump`.
- Nav agents and `arc_grid_los` are unchanged.

Acceptance (ARC's self-test, `tests/test_v100_action.c`):
- **Sight:** seen after 30 ticks (0.5 s) in the clear; never from behind, beyond range or behind a wall; partly
  covered took 270 ticks.
- **Hearing:** radius 5 is heard at 4, not at 6, and not at 4 behind a muffling wall (heard there with muffle 1).
- **Memory:** kept at 1.5 s, forgotten after 2 s.
- **Alerts and teams:** a nearby droid is told and a far one isn't; same-team targets are ignored.
- **Search:** points are on the navmesh and near the last known position; a nav agent walks to the first, then the
  next.
- **Behaviour:** PATROL→ALERT→ATTACK→SEARCH→PATROL→FLEE in order, with enter, exit and tick counts right.
- **Determinism:** the same runs are byte-identical, including across a save / load; also for senses.
- `samples/action` (`tests/action_check.md`, 13 checks): states and senses in `dump`, `brain droid 2 FLEE` works.
