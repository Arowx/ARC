/* ARC 0.10.0 self-test: AI brains, senses, shots, camera rigs (Droids' requests). Included by test_core.c;
   v100_init() runs in init. Names start with v100_ (one translation unit). */
char *arc__json_capture(void (*fn)(arc_json *j));
void arc__brain_dump_json(arc_json *j); void arc__brain_command(int argc, const char **argv);
void arc__senses_dump_json(arc_json *j); void arc__shots_json(arc_json *j);

/* ---- brains ---- */
enum { V100_PATROL, V100_ALERT, V100_ATTACK, V100_SEARCH, V100_FLEE };
static arc_brain_def v100_def;
typedef struct { int enters[5], exits[5], ticks[5]; } v100_counts;
static void v100_enter(arc_brain *b, void *u, float dt) { (void)dt; ((v100_counts *)u)->enters[b->state]++; }
static void v100_exit(arc_brain *b, void *u, float dt) { (void)dt; ((v100_counts *)u)->exits[b->state]++; }
static void v100_tick(arc_brain *b, void *u, float dt) { (void)dt; ((v100_counts *)u)->ticks[b->state]++; }
static void v100_search_tick(arc_brain *b, void *u, float dt) { v100_tick(b, u, dt); b->i[0]++; }   /* counts its own ticks on the blackboard */
static bool v100_low(const arc_brain *b, void *u) { (void)u; return b->f[1] <= 0.25f; }
static void v100_make_def(void) {
    arc_brain_def *d = &v100_def; memset(d, 0, sizeof *d); d->name = "droid";
    CHECK(arc_brain_def_state(d, "PATROL", v100_enter, v100_tick, v100_exit) == V100_PATROL);
    CHECK(arc_brain_def_state(d, "ALERT", v100_enter, v100_tick, v100_exit) == V100_ALERT);
    CHECK(arc_brain_def_state(d, "ATTACK", v100_enter, v100_tick, v100_exit) == V100_ATTACK);
    CHECK(arc_brain_def_state(d, "SEARCH", v100_enter, v100_search_tick, v100_exit) == V100_SEARCH);
    CHECK(arc_brain_def_state(d, "FLEE", v100_enter, v100_tick, v100_exit) == V100_FLEE);
    arc_brain_def_transition_fn(d, ARC_BRAIN_ANY, V100_FLEE, v100_low);                    /* first: it wins over the rest */
    arc_brain_def_transition(d, V100_PATROL, V100_ALERT, ARC_BRAIN_IF(ARC_BRAIN_F_GE, 0, 0.2f), ARC_BRAIN_NONE);
    arc_brain_def_transition(d, V100_ALERT, V100_ATTACK, ARC_BRAIN_IF(ARC_BRAIN_TIME_GE, 0, 0.5f), ARC_BRAIN_IF(ARC_BRAIN_F_GE, 0, 1.0f));
    arc_brain_def_transition(d, V100_ALERT, V100_PATROL, ARC_BRAIN_IF(ARC_BRAIN_F_LE, 0, 0.0f), ARC_BRAIN_NONE);
    arc_brain_def_transition(d, V100_ATTACK, V100_SEARCH, ARC_BRAIN_IF(ARC_BRAIN_TRIGGER, 3, 0), ARC_BRAIN_NONE);
    arc_brain_def_transition(d, V100_SEARCH, V100_PATROL, ARC_BRAIN_IF(ARC_BRAIN_TIME_GE, 0, 2.0f), ARC_BRAIN_NONE);
    d->f_name[0] = "aware"; d->f_name[1] = "health"; d->i_name[0] = "search_ticks";
    CHECK(arc_brain_def_find(d, "search") == V100_SEARCH && arc_brain_def_find(d, "nope") == -1);
}
/* one scripted run: returns the final brain; `cut` > 0 saves at that tick and goes on from the copy */
static void v100_run(arc_brain *b, v100_counts *c, int cut, arc_brain *saved) {
    const float dt = 1.0f / 60.0f;
    arc_brain_init(b, V100_PATROL); memset(c, 0, sizeof *c); b->f[1] = 1.0f;
    for (int t = 0; t < 400; t++) {
        if (t == cut && saved) { *saved = *b; *b = *saved; }
        b->f[0] = t < 10 ? 0 : t < 40 ? 0.3f : t < 120 ? 1.0f : 0;   /* notices, then sure, then gone */
        if (t == 120) arc_brain_trigger(b, 3);                           /* lost sight */
        if (t == 300) b->f[1] = 0.2f;                                    /* badly hurt */
        arc_brain_update(b, &v100_def, c, dt);
    }
}
static void v100_brains(void) {
    v100_make_def();
    arc_brain b, b2, mid; v100_counts c, c2;
    v100_run(&b, &c, -1, NULL);
    /* PATROL -> ALERT at t 10; ALERT -> ATTACK when 0.5 s in and sure (t 40); trigger at 120 -> SEARCH; 2 s later
       PATROL (t 240); hurt at 300 -> FLEE */
    static const uint8_t want[4][2] = { { V100_PATROL, V100_ALERT }, { V100_ALERT, V100_ATTACK }, { V100_ATTACK, V100_SEARCH }, { V100_SEARCH, V100_PATROL } };
    CHECK(b.state == V100_FLEE && b.changes == 5 && b.hist_n == 5);
    for (int k = 0; k < 4; k++) CHECK(b.hist[k][0] == want[k][0] && b.hist[k][1] == want[k][1]);
    CHECK(b.hist[4][0] == V100_PATROL && b.hist[4][1] == V100_FLEE);
    CHECK(c.enters[V100_PATROL] == 2 && c.exits[V100_PATROL] == 2 && c.enters[V100_FLEE] == 1 && c.exits[V100_FLEE] == 0);
    CHECK(b.i[0] >= 120 && b.i[0] <= 121);                 /* SEARCH ran its tick for 2 s */
    CHECK(c.ticks[V100_ATTACK] == 80);                     /* t 40..119 */
    printf("ARC note brain: states %d->%d->%d->%d->%d->%d, %d changes\n", b.hist[0][0], b.hist[0][1], b.hist[1][1], b.hist[2][1], b.hist[3][1], b.hist[4][1], b.changes);
    /* the same run again, and with a save / load halfway: byte-identical */
    v100_run(&b2, &c2, -1, NULL); CHECK(memcmp(&b, &b2, sizeof b) == 0 && memcmp(&c, &c2, sizeof c) == 0);
    v100_run(&b2, &c2, 170, &mid); CHECK(memcmp(&b, &b2, sizeof b) == 0);
    /* forcing, arc_brain_go from outside, the watch (dump and command) */
    static struct { int hp; arc_brain brain; } agents[3];
    for (int i = 0; i < 3; i++) { arc_brain_init(&agents[i].brain, V100_PATROL); agents[i].brain.f[1] = 1.0f; }
    arc_brain_watch("droid", &agents[0].brain, 3, sizeof agents[0], &v100_def);
    const char *argv[] = { "brain", "droid", "2", "search" };
    arc__brain_command(4, argv);
    for (int i = 0; i < 3; i++) arc_brain_update(&agents[i].brain, &v100_def, &c, 1.0f / 60.0f);
    CHECK(agents[2].brain.state == V100_SEARCH && agents[0].brain.state == V100_PATROL);
    char *jb = arc__json_capture(arc__brain_dump_json);
    CHECK(jb && strstr(jb, "\"brains\":{\"droid\":[") && strstr(jb, "\"state\":\"SEARCH\"") && strstr(jb, "\"health\":1"));
    free(jb);
    arc_brain_watch("droid", NULL, 0, 0, NULL);
    jb = arc__json_capture(arc__brain_dump_json);
    CHECK(!jb || !strstr(jb, "brains")); free(jb);
}

/* ---- senses ---- */
typedef struct { float lo[3], hi[3]; } v100_box;
static v100_box v100_walls[4]; static int v100_nwalls;
static bool v100_seg_box(arc_v3 a, arc_v3 b, const v100_box *w) {   /* the segment touches the box */
    float t0 = 0, t1 = 1, o[3] = { a.x, a.y, a.z }, d[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
    for (int k = 0; k < 3; k++) {
        if (fabsf(d[k]) < 1e-9f) { if (o[k] < w->lo[k] || o[k] > w->hi[k]) return false; continue; }
        float u = (w->lo[k] - o[k]) / d[k], v = (w->hi[k] - o[k]) / d[k]; if (u > v) { float s = u; u = v; v = s; }
        if (u > t0) t0 = u;
        if (v < t1) t1 = v;
        if (t0 > t1) return false;
    }
    return true;
}
static bool v100_los(arc_v3 a, arc_v3 b, void *u) { (void)u; for (int i = 0; i < v100_nwalls; i++) if (v100_seg_box(a, b, &v100_walls[i])) return false; return true; }
static int v100_ticks_to_see(arc_senses *s, int agent, const arc_senses_target *t, int max) {
    for (int k = 1; k <= max; k++) { arc_senses_update(s, 1.0f / 60.0f, t, 1, v100_los, NULL); const arc_senses_memory *m = arc_senses_knows(s, agent, t->id); if (m && m->seen_now) return k; }
    return -1;
}
static void v100_target(arc_senses_target *t, int id, arc_v3 p, int npoints) {
    memset(t, 0, sizeof *t); t->id = id; t->npoints = npoints;
    for (int k = 0; k < npoints; k++) t->points[k] = arc_v3_make(p.x + (float)(k % 3 - 1) * 0.2f, 0.2f + 0.2f * (float)k, p.z);
}
static void v100_senses(void) {
    static arc_senses s; arc_senses_target t;
    /* sight: in front, clear: after the build-up (0.5 s = 30 ticks) */
    arc_senses_init(&s); v100_nwalls = 0;
    int a = arc_senses_add(&s, arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1));
    v100_target(&t, 7, arc_v3_make(0, 0, 5), 1); t.points[0] = arc_v3_make(0, 1, 5);
    int clear = v100_ticks_to_see(&s, a, &t, 200);
    CHECK(clear >= 29 && clear <= 31);
    /* behind: never */
    arc_senses_init(&s); a = arc_senses_add(&s, arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1));
    t.points[0] = arc_v3_make(0, 1, -5); CHECK(v100_ticks_to_see(&s, a, &t, 300) < 0 && arc_senses_knows(&s, a, 7) == NULL);
    /* beyond range: never */
    arc_senses_init(&s); a = arc_senses_add(&s, arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1));
    t.points[0] = arc_v3_make(0, 1, 12); CHECK(v100_ticks_to_see(&s, a, &t, 300) < 0);
    /* behind a wall: never */
    arc_senses_init(&s); a = arc_senses_add(&s, arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1));
    v100_walls[0] = (v100_box){ { -3, 0, 2 }, { 3, 3, 2.5f } }; v100_nwalls = 1;
    t.points[0] = arc_v3_make(0, 1, 5); CHECK(v100_ticks_to_see(&s, a, &t, 300) < 0);
    /* partly covered (a low wall hides the lower points of 9): slower */
    arc_senses_init(&s); a = arc_senses_add(&s, arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1));
    s.agent[a].eye_height = 0.5f;
    v100_walls[0] = (v100_box){ { -3, 0, 2 }, { 3, 0.95f, 2.5f } }; v100_nwalls = 1;
    v100_target(&t, 7, arc_v3_make(0, 0, 5), 9);
    int partial = v100_ticks_to_see(&s, a, &t, 600);
    printf("ARC note senses: seen after %d ticks in the clear, %d partly covered\n", clear, partial);
    CHECK(partial > clear * 3 / 2 && partial < 600);
    /* hearing: radius 5 heard at 4, not at 6, not at 4 behind a muffling wall (muffle 0.5), but heard with muffle 1 */
    arc_senses_init(&s); v100_nwalls = 0;
    int h4 = arc_senses_add(&s, arc_v3_make(4, -1, 0), arc_v3_make(1, 0, 0)), h6 = arc_senses_add(&s, arc_v3_make(0, -1, 6), arc_v3_make(1, 0, 0));
    int hw = arc_senses_add(&s, arc_v3_make(-4, -1, 0), arc_v3_make(1, 0, 0)), hm = arc_senses_add(&s, arc_v3_make(0, -1, -4), arc_v3_make(1, 0, 0));
    s.agent[hm].muffle = 1.0f;
    v100_walls[0] = (v100_box){ { -2.5f, -5, -1 }, { -2, 5, 1 } }; v100_walls[1] = (v100_box){ { -1, -5, -2.5f }, { 1, 5, -2 } }; v100_nwalls = 2;
    arc_senses_noise(&s, arc_v3_make(0, 0, 0), 5, 1, 1, 7);
    arc_senses_update(&s, 1.0f / 60.0f, NULL, 0, v100_los, NULL);
    CHECK(s.agent[h4].heard_noise && !s.agent[h6].heard_noise && !s.agent[hw].heard_noise && s.agent[hm].heard_noise);
    const arc_senses_memory *m = arc_senses_knows(&s, h4, 7);
    CHECK(m && m->how == ARC_KNOW_HEARD && m->pos.x == 0 && !m->seen_now);
    CHECK(s.noise_count == 0);
    /* memory: kept, then forgotten after `forget` (2 s) */
    arc_senses_init(&s); v100_nwalls = 0;
    a = arc_senses_add(&s, arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1)); s.agent[a].forget = 2.0f; s.agent[a].build_up = 0;
    v100_target(&t, 9, arc_v3_make(1, 0, 4), 1); t.points[0] = arc_v3_make(1, 1, 4);
    arc_senses_update(&s, 1.0f / 60.0f, &t, 1, v100_los, NULL);
    CHECK(arc_senses_knows(&s, a, 9) && arc_senses_knows(&s, a, 9)->seen_now);
    for (int k = 0; k < 90; k++) arc_senses_update(&s, 1.0f / 60.0f, NULL, 0, v100_los, NULL);   /* 1.5 s gone */
    m = arc_senses_knows(&s, a, 9);
    CHECK(m && !m->seen_now && m->how == ARC_KNOW_SEEN && m->pos.x == 1 && m->pos.z == 4);
    for (int k = 0; k < 40; k++) arc_senses_update(&s, 1.0f / 60.0f, NULL, 0, v100_los, NULL);   /* 2.17 s */
    CHECK(arc_senses_knows(&s, a, 9) == NULL);
    /* alerts: told within the radius */
    arc_senses_init(&s);
    a = arc_senses_add(&s, arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1)); s.agent[a].build_up = 0;
    int near = arc_senses_add(&s, arc_v3_make(3, 0, -3), arc_v3_make(0, 0, -1)), far = arc_senses_add(&s, arc_v3_make(30, 0, 0), arc_v3_make(0, 0, -1));
    arc_senses_update(&s, 1.0f / 60.0f, &t, 1, v100_los, NULL);
    CHECK(arc_senses_knows(&s, near, 9) == NULL);
    CHECK(arc_senses_alert(&s, a, 10) == 1);
    m = arc_senses_knows(&s, near, 9);
    CHECK(m && m->how == ARC_KNOW_TOLD && m->pos.z == 4 && arc_senses_knows(&s, far, 9) == NULL);
    /* teams: a target of the agent's own team is ignored */
    arc_senses_init(&s); a = arc_senses_add(&s, arc_v3_make(0, 0, 0), arc_v3_make(0, 0, 1)); s.agent[a].build_up = 0; s.agent[a].team = 2; t.team = 2;
    arc_senses_update(&s, 1.0f / 60.0f, &t, 1, v100_los, NULL); CHECK(arc_senses_knows(&s, a, 9) == NULL); t.team = 0;
    /* search: points on the navmesh around the last known position; an agent walks to the first, then the next */
    static const char *rows[] = { "..........", "..........", "....##....", "....##....", "..........", "..........", "..........", "..........", "..........", ".........." };
    v092_grid(10, 10, rows);
    arc_navmesh nm = arc_navmesh_create(NULL); arc_navmesh_add_grid(nm, &v092_g, arc_v3_make(0, 0, 0), 1, NULL); arc_navmesh_build(nm);
    arc_v3 pts[6], pts2[6]; arc_v3 last = arc_v3_make(5.0f, 0, 3.0f);   /* inside the block: the nearest walkable point comes first */
    int np = arc_senses_search_points(nm, last, 3.0f, 6, pts), np2 = arc_senses_search_points(nm, last, 3.0f, 6, pts2);
    CHECK(np >= 4 && np == np2 && memcmp(pts, pts2, sizeof(arc_v3) * (size_t)np) == 0);
    bool on = true; for (int k = 0; k < np; k++) { arc_v3 q; on = on && arc_navmesh_nearest(nm, pts[k], 0.01f, &q) && arc_v3_len(arc_v3_sub(pts[k], last)) < 3.6f; }
    CHECK(on);
    arc_nav_agent ag; arc_nav_agent_init(&ag, arc_v3_make(1.5f, 0, 8.5f), 3.0f, 0.2f);
    int reached = 0;
    for (int leg = 0; leg < 2 && leg < np; leg++) {
        arc_nav_agent_goto(&ag, nm, pts[leg]);
        for (int k = 0; k < 600 && !ag.arrived; k++) arc_nav_agent_update(&ag, nm, 1.0f / 60.0f);
        reached += ag.arrived && arc_v3_len(arc_v3_sub(ag.pos, pts[leg])) < 0.3f;
    }
    CHECK(reached == 2);
    arc_navmesh_destroy(nm);
    /* determinism: the same run twice, and with a save / load in the middle */
    static arc_senses r1, r2;
    for (int run = 0; run < 2; run++) {
        arc_senses *S = run ? &r2 : &r1; arc_senses_init(S);
        for (int i = 0; i < 8; i++) { int k = arc_senses_add(S, arc_v3_make((float)i, 0, 0), arc_v3_make(0, 0, 1)); S->agent[k].build_up = 0.3f; }
        v100_walls[0] = (v100_box){ { 2, 0, 3 }, { 4, 3, 3.5f } }; v100_nwalls = 1;
        for (int k = 0; k < 300; k++) {
            arc_senses_target tt; v100_target(&tt, 1, arc_v3_make(arc_sinf((float)k * 0.03f) * 6.0f, 0, 5.0f + arc_cosf((float)k * 0.05f) * 3.0f), 5);
            if (k % 37 == 0) arc_senses_noise(S, tt.points[0], 6, 2, 1, 1);
            if (run && k == 150) { static arc_senses tmp; tmp = *S; *S = tmp; }
            arc_senses_update(S, 1.0f / 60.0f, &tt, 1, v100_los, NULL);
            if (k % 50 == 0) arc_senses_alert(S, 0, 4);
        }
    }
    CHECK(memcmp(&r1, &r2, sizeof r1) == 0);
    arc_senses_watch(&r1, "droids");
    char *jb = arc__json_capture(arc__senses_dump_json);
    CHECK(jb && strstr(jb, "\"senses\":{\"droids\":[{\"agent\":0"));
    free(jb); arc_senses_watch(NULL, "droids");
}

/* ---- shots ---- */
static int v100_wall_x = 5, v100_corner = 0;
static bool v100_grid(int x, int z, void *u) {
    (void)u;
    if (v100_corner) return x == 5 && z == 1;
    return x == v100_wall_x;
}
static void v100_shots(void) {
    arc_shot_hit h, hits[16];
    arc_shots_clear(); arc_shots_set_world((arc_cworld){ 0 }, (arc_physics){ 0 }, v100_grid, NULL); arc_shots_set_grid(1, 0, 0); arc_shots_targets(NULL, 0);
    v100_wall_x = 5; v100_corner = 0;
    /* hitscan at a wall 5 units away: hits at 5 with the wall's normal */
    arc_shot_desc d; memset(&d, 0, sizeof d); d.from = arc_v3_make(0, 0.5f, 0.5f); d.dir = arc_v3_make(1, 0, 0); d.range = 50; d.tracer_rgb = 0xFFC060;
    int id = arc_shot_fire(&d);
    int n = arc_shots_hits(hits, 16);
    CHECK(id > 0 && n == 1 && hits[0].kind == ARC_HIT_GRID && fabsf(hits[0].dist - 5) < 1e-4f && hits[0].normal.x == -1 && hits[0].cell_x == 5 && hits[0].shot == id);
    /* a target sphere at 3 in front of the wall is hit first; its own owner never */
    arc_shot_target tg[2] = { { 11, 0, { 3, 0.5f, 0.5f }, { 3, 0.5f, 0.5f }, 0.5f }, { 12, 0, { 4, 0, 0.5f }, { 4, 2, 0.5f }, 0.25f } };
    arc_shots_targets(tg, 2);
    CHECK(arc_shot_cast(&d, &h) && h.kind == ARC_HIT_TARGET && h.id == 11 && fabsf(h.dist - 2.5f) < 1e-3f && h.normal.x < -0.99f);
    d.owner = 11; CHECK(arc_shot_cast(&d, &h) && h.kind == ARC_HIT_TARGET && h.id == 12 && fabsf(h.dist - 3.75f) < 1e-3f);   /* the capsule behind */
    tg[1].layer = 2; arc_shots_targets(tg, 2); d.mask = 1; CHECK(arc_shot_cast(&d, &h) && h.kind == ARC_HIT_GRID); d.mask = 0; d.owner = 0;
    /* a projectile at 10 units/s reaches a target 5 away in 0.5 s (+-1 tick) */
    arc_shots_clear(); v100_wall_x = 20;
    arc_shot_target t1 = { 21, 0, { 5.25f, 0.5f, 0.5f }, { 5.25f, 0.5f, 0.5f }, 0.25f };
    arc_shots_targets(&t1, 1);
    d.speed = 10; d.tracer_rgb = 0; id = arc_shot_fire(&d);
    int at = -1;
    for (int k = 1; k <= 60 && at < 0; k++) { arc_shots_update(1.0f / 60.0f); if (arc_shots_hits(hits, 16) > 0) at = k; }
    CHECK(at >= 29 && at <= 31 && hits[0].kind == ARC_HIT_TARGET && hits[0].id == 21 && fabsf(hits[0].dist - 5) < 0.01f && arc_shots_in_flight() == 0);
    /* ... and misses when the target steps aside first: it flies on to the wall */
    arc_shots_clear(); arc_shots_targets(&t1, 1); id = arc_shot_fire(&d);
    for (int k = 1; k <= 200; k++) {
        if (k == 15) { arc_shot_target moved = t1; moved.a.z = moved.b.z = 3; arc_shots_targets(&moved, 1); }
        arc_shots_update(1.0f / 60.0f);
    }
    n = arc_shots_hits(hits, 16);
    CHECK(n == 1 && hits[0].kind == ARC_HIT_GRID && fabsf(hits[0].dist - 20) < 0.01f);
    /* a fat projectile grazes a corner that a ray misses */
    arc_shots_clear(); arc_shots_targets(NULL, 0); v100_corner = 1;
    arc_shot_desc g; memset(&g, 0, sizeof g); g.from = arc_v3_make(0, 0.5f, 0.9f); g.dir = arc_v3_make(1, 0, 0); g.range = 12;
    CHECK(!arc_shot_cast(&g, &h));
    g.radius = 0.2f; CHECK(arc_shot_cast(&g, &h) && h.kind == ARC_HIT_GRID && h.cell_x == 5 && h.cell_z == 1 && h.dist > 4.6f && h.dist < 5.0f);
    v100_corner = 0;
    /* the collision world: a box; a ray hits its face, a fat shot grazes its corner */
    arc_cworld cw = arc_cworld_create(); arc_cworld_add_box(cw, arc_v3_make(8, 0, 2), arc_v3_make(9, 2, 3), 77);
    arc_shots_set_world(cw, (arc_physics){ 0 }, NULL, NULL);
    arc_shot_desc c; memset(&c, 0, sizeof c); c.from = arc_v3_make(0, 1, 2.5f); c.dir = arc_v3_make(1, 0, 0); c.range = 20;
    CHECK(arc_shot_cast(&c, &h) && h.kind == ARC_HIT_WORLD && fabsf(h.dist - 8) < 1e-3f && h.normal.x < -0.99f);
    c.from.z = 1.85f; CHECK(!arc_shot_cast(&c, &h));
    c.radius = 0.25f; CHECK(arc_shot_cast(&c, &h) && h.kind == ARC_HIT_WORLD && h.dist > 7.5f && h.dist < 8.0f);
    arc_cworld_destroy(cw);
    /* physics: a box body; its own owner is passed through */
    arc_physics ph = arc_physics_create(NULL);
    int body = arc_physics_add(ph, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.5f, 0.5f }, .pos = { 6, 1, 0 }, .type = ARC_BODY_STATIC });
    int body2 = arc_physics_add(ph, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.5f, 0.5f, 0.5f }, .pos = { 9, 1, 0 }, .type = ARC_BODY_STATIC });
    arc_shots_set_world((arc_cworld){ 0 }, ph, NULL, NULL);
    arc_shot_desc p; memset(&p, 0, sizeof p); p.from = arc_v3_make(0, 1, 0); p.dir = arc_v3_make(1, 0, 0); p.range = 20;
    CHECK(arc_shot_cast(&p, &h) && h.kind == ARC_HIT_BODY && h.id == body && fabsf(h.dist - 5.5f) < 0.01f);
    p.owner = body; CHECK(arc_shot_cast(&p, &h) && h.kind == ARC_HIT_BODY && h.id == body2 && fabsf(h.dist - 8.5f) < 0.01f);
    p.radius = 0.2f; CHECK(arc_shot_cast(&p, &h) && h.kind == ARC_HIT_BODY && h.id == body2 && h.dist > 8.1f && h.dist < 8.5f);
    arc_physics_destroy(ph);
    /* all at once: the nearest wins */
    cw = arc_cworld_create(); arc_cworld_add_box(cw, arc_v3_make(3, 0, 0), arc_v3_make(3.5f, 2, 1), 1);
    arc_shots_set_world(cw, (arc_physics){ 0 }, v100_grid, NULL); v100_wall_x = 5;
    CHECK(arc_shot_cast(&d, &h) && h.kind == ARC_HIT_WORLD && fabsf(h.dist - 3) < 1e-3f);
    arc_cworld_destroy(cw); arc_shots_set_world((arc_cworld){ 0 }, (arc_physics){ 0 }, v100_grid, NULL);
    /* determinism: projectiles with a save / load halfway give the same hits */
    arc_shot_target ts[3] = { { 1, 0, { 6, 0.5f, 0.5f }, { 6, 0.5f, 0.5f }, 0.3f }, { 2, 0, { 3, 0, 2.5f }, { 3, 1, 2.5f }, 0.3f }, { 3, 0, { 2, 0.5f, -1 }, { 2, 0.5f, -1 }, 0.4f } };
    arc_shot_hit run_hits[2][40]; int run_n[2] = { 0, 0 };
    static uint8_t saved[1 << 16]; int ssize = 0;
    for (int run = 0; run < 2; run++) {
        arc_shots_clear(); arc_shots_targets(ts, 3); v100_wall_x = 12;
        for (int k = 0; k < 120; k++) {
            if (k < 40 && k % 4 == 0) {
                arc_shot_desc q; memset(&q, 0, sizeof q); q.from = arc_v3_make(0, 0.5f, 0.5f);
                q.dir = arc_v3_make(1, 0, arc_sinf((float)k * 0.21f) * 0.4f); q.speed = 7.0f + (float)(k % 3); q.radius = k % 8 == 0 ? 0.1f : 0; q.range = 30; q.user = (uint32_t)k;
                arc_shot_fire(&q);
            }
            if (k == 60) { if (run == 0) ssize = arc_shots_save(saved, sizeof saved); else { CHECK(arc_shots_save(NULL, 0) == ssize); CHECK(arc_shots_load(saved, ssize)); } }
            arc_shots_update(1.0f / 60.0f);
        }
        run_n[run] = arc_shots_hits(run_hits[run], 40);
    }
    CHECK(run_n[0] > 5 && run_n[0] == run_n[1] && memcmp(run_hits[0], run_hits[1], sizeof(arc_shot_hit) * (size_t)run_n[0]) == 0);
    printf("ARC note shots: projectile hit after %d ticks, %d hits in the determinism run, state %d bytes\n", at, run_n[0], ssize);
    /* frame: tracers listed */
    arc_shots_clear(); arc_shots_targets(NULL, 0); v100_wall_x = 5; d.speed = 0; d.tracer_rgb = 0xFFFFFF; arc_shot_fire(&d);
    char *jb = arc__json_capture(arc__shots_json);
    CHECK(jb && strstr(jb, "\"tracers\":1") && strstr(jb, "\"tracer_lines\":[[0,0.5,0.5,5,0.5,0.5]]"));
    free(jb);
    arc_shots_update(0.2f); jb = arc__json_capture(arc__shots_json);
    CHECK(jb && strstr(jb, "\"tracers\":0")); free(jb);
    arc_shots_clear(); arc_shots_set_world((arc_cworld){ 0 }, (arc_physics){ 0 }, NULL, NULL);
}

/* ---- camera rigs ---- */
static int v100_rig_wall = 1;      /* 1: a wall along z = -1..0 (cells z = -1) */
static float v100_pillar_x = -100; /* a pillar cell passing behind */
static bool v100_rig_grid(int x, int z, void *u) {
    (void)u;
    if (v100_rig_wall && z == -1) return true;
    int px = (int)floorf(v100_pillar_x);
    return z == -2 && x == px;
}
static void v100_rigs(void) {
    const float dt = 1.0f / 60.0f;
    arc_cam_rig_set_world((arc_cworld){ 0 }, (arc_physics){ 0 }, 0, v100_rig_grid, NULL, 1);
    /* FOLLOW behind a target with a wall 1 unit behind: the eye stays on the near side, radius off the wall */
    arc_cam_rig r; memset(&r, 0, sizeof r);
    r.kind = ARC_RIG_FOLLOW; r.target = arc_v3_make(0.5f, 1, 1.0f); r.yaw = 0; r.distance = 5; r.height = 1; r.aim_height = 1; r.follow_lag = 0.3f; r.collide_radius = 0.3f;
    v100_rig_wall = 1; v100_pillar_x = -100;
    float minz = 99;
    for (int k = 0; k < 60; k++) { arc_cam_rig_update(&r, dt); if (r.eye_now.z < minz) minz = r.eye_now.z; }
    CHECK(minz >= 0.3f - 0.02f && r.eye_now.z < 0.35f && r.dist_now < 0.8f);
    arc_camera cam = arc_cam_rig_camera(&r);
    CHECK(cam.fov_y > 0 && cam.target.z > cam.eye.z);
    /* the wall goes: back to `distance` within about 3 x follow_lag, never jumping out */
    v100_rig_wall = 0;
    float last = r.dist_now, worst = 0; int back = -1;
    for (int k = 1; k <= 120; k++) {
        arc_cam_rig_update(&r, dt);
        float allowed_step = (5.0f - last) * (1.0f - arc_expf(-3.0f * dt / 0.3f)) + 1e-4f;
        if (r.dist_now - last > allowed_step) worst = r.dist_now - last;
        last = r.dist_now;
        if (back < 0 && r.dist_now > 4.95f) back = k;
    }
    printf("ARC note rig: wall behind: eye z %.3f (wall face 0, radius 0.3); back to 99%% of the distance after %d ticks\n", minz, back);
    CHECK(back > 0 && (float)back * dt <= 3.0f * 0.3f + 0.05f && worst == 0);
    /* pop test: a pillar passing behind over 200 ticks; every outward step within what the lag allows */
    memset(&r, 0, sizeof r); r.kind = ARC_RIG_FOLLOW; r.distance = 4; r.height = 0.5f; r.aim_height = 0.5f; r.follow_lag = 0.25f; r.collide_radius = 0.3f;
    v100_rig_wall = 0; int pulls = 0; worst = 0; last = -1;
    for (int k = 0; k < 200; k++) {
        v100_pillar_x = -3.0f + (float)k * 0.03f;
        r.target = arc_v3_make(0.5f, 0.5f, 1.0f);
        arc_cam_rig_update(&r, dt);
        if (last >= 0) {
            float out = r.dist_now - last, allowed_step = (4.0f - last) * (1.0f - arc_expf(-3.0f * dt / 0.25f)) + 1e-4f;
            if (out > allowed_step && out > worst) worst = out;
            pulls += out < -0.05f;
        }
        last = r.dist_now;
    }
    CHECK(pulls >= 1 && worst == 0);
    /* blend TOPDOWN -> FOLLOW over 0.5 s: valid cameras, no jump of the aim point, ends exactly on FOLLOW */
    arc_cam_rig top, fol, ref; memset(&top, 0, sizeof top); memset(&fol, 0, sizeof fol);
    top.kind = ARC_RIG_TOPDOWN; top.ortho_half = 6; top.target = arc_v3_make(2, 0, 3); top.yaw = 0;
    fol.kind = ARC_RIG_FOLLOW; fol.distance = 5; fol.height = 2; fol.target = arc_v3_make(2, 0, 3); fol.yaw = 0.5f; fol.follow_lag = 0.1f;
    v100_rig_wall = 0; v100_pillar_x = -100;
    for (int k = 0; k < 10; k++) arc_cam_rig_update(&top, dt);
    arc_camera prev = arc_cam_rig_camera(&top);
    CHECK(prev.fov_y == 0 && prev.ortho_height == 12);
    arc_cam_rig_blend(&top, &fol, 0.5f); CHECK(arc_cam_rig_blending(&top));
    ref = fol;
    bool valid = true; float jump = 0, centre_jump = 0;
    for (int k = 0; k < 45; k++) {
        top.target = fol.target = ref.target = arc_v3_make(2 + (float)k * 0.01f, 0, 3);
        arc_cam_rig_update(&top, dt); arc_cam_rig_update(&ref, dt);
        arc_camera c = arc_cam_rig_camera(&top);
        float dl = arc_v3_len(arc_v3_sub(c.target, c.eye));
        valid = valid && dl > 1e-3f && (c.fov_y > 0 || c.ortho_height > 0) && c.zfar > c.znear && dl == dl;
        float tj = arc_v3_len(arc_v3_sub(c.target, prev.target)); if (tj > jump) jump = tj;
        /* the aim point stays in the middle of the screen: project the previous aim point with the new camera */
        arc_v2 sp; if (arc_camera_to_screen(&c, 1280, 720, prev.target, &sp)) { float cj = fabsf(sp.x - 640) + fabsf(sp.y - 360); if (cj > centre_jump) centre_jump = cj; }
        prev = c;
    }
    arc_camera end = arc_cam_rig_camera(&top), want = arc_cam_rig_camera(&ref);
    printf("ARC note rig blend: aim point moves at most %.3f a tick, the old aim point at most %.1f px from the centre\n", jump, centre_jump);
    CHECK(valid && jump < 0.2f && centre_jump < 40 && !arc_cam_rig_blending(&top));
    CHECK(arc_v3_len(arc_v3_sub(end.eye, want.eye)) < 1e-3f && arc_v3_len(arc_v3_sub(end.target, want.target)) < 1e-3f && end.fov_y == want.fov_y && end.ortho_height == 0);   /* (the switched rig's lag had caught up) */
    /* shake decays by itself; FIRST looks along yaw and pitch; TOPDOWN keeps inside its bounds */
    arc_cam_rig_shake(&ref, 0.8f); arc_camera s0 = arc_cam_rig_camera(&ref);
    CHECK(arc_v3_len(arc_v3_sub(s0.eye, want.eye)) > 0.001f || ref.shake > 0.7f);
    for (int k = 0; k < 60; k++) arc_cam_rig_update(&ref, dt);
    CHECK(ref.shake == 0);
    arc_cam_rig fp; memset(&fp, 0, sizeof fp); fp.kind = ARC_RIG_FIRST; fp.target = arc_v3_make(1, 1.6f, 1); fp.yaw = 3.14159265f * 0.5f; fp.pitch = 0;
    arc_cam_rig_update(&fp, dt); cam = arc_cam_rig_camera(&fp);
    CHECK(fabsf(cam.target.x - cam.eye.x - 1) < 1e-4f && fabsf(cam.target.z - cam.eye.z) < 1e-4f && cam.eye.y == 1.6f);
    arc_cam_rig tb; memset(&tb, 0, sizeof tb); tb.kind = ARC_RIG_TOPDOWN; tb.ortho_half = 4; tb.has_bounds = true;
    tb.bounds[0] = 0; tb.bounds[1] = 0; tb.bounds[2] = 40; tb.bounds[3] = 20; tb.target = arc_v3_make(-5, 0, 30);
    arc_cam_rig_update(&tb, dt);
    float hx = 4.0f * (arc_width() > 0 && arc_height() > 0 ? (float)arc_width() / (float)arc_height() : 16.0f / 9.0f);
    CHECK(fabsf(tb.eye_now.x - hx) < 1e-4f && fabsf(tb.eye_now.z - 16) < 1e-4f);
    /* determinism: the same inputs twice, and across a save / load */
    arc_cam_rig a1, a2;
    for (int run = 0; run < 2; run++) {
        arc_cam_rig *A = run ? &a2 : &a1; memset(A, 0, sizeof *A);
        A->kind = ARC_RIG_FOLLOW; A->distance = 4; A->height = 1.5f; A->follow_lag = 0.2f; A->collide_radius = 0.25f;
        for (int k = 0; k < 300; k++) {
            v100_pillar_x = -3.0f + (float)k * 0.02f; v100_rig_wall = (k / 60) % 2;
            A->target = arc_v3_make(arc_sinf((float)k * 0.02f) * 2.0f, 0.5f, 1.5f); A->yaw = arc_sinf((float)k * 0.013f);
            if (k == 100) arc_cam_rig_shake(A, 0.5f);
            if (run && k == 150) { arc_cam_rig tmp = *A; memset(A, 0xAB, sizeof *A); *A = tmp; }
            arc_cam_rig_update(A, dt);
        }
    }
    CHECK(memcmp(&a1, &a2, sizeof a1) == 0);
    arc_cam_rig_set_world((arc_cworld){ 0 }, (arc_physics){ 0 }, 0, NULL, NULL, 1);
}
static void v100_init(void) { v100_brains(); v100_senses(); v100_shots(); v100_rigs(); }
