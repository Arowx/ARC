/* ARC 0.9.7 cross-machine determinism (0.9.8: ragdolls and a rollback match added): fixed scenarios whose exact results must be the same bits on every compiler and
   platform ARC builds for (gcc, clang, MinGW, MSVC x64, Emscripten/wasm). Each one hashes its state (the raw bits of
   every float) as it runs. tests/test_core.c checks the hashes against DETS_GOLDEN (made on Linux gcc); tests/det_check.c
   shows them in a window or a web page and can write a per-step trace to find the first step two machines differ.
   Everything here uses ARC's deterministic paths only: physics (+ - * / sqrt), navigation, terrain heights, splat rules
   and scatter, arc_dmath, arc_rng and noise. Inputs are literals or arithmetic (no C library trig).
   Names start with dets_ (included into other files). */
#include <string.h>
/* this file's own arithmetic (the scenario inputs) must not be fused either: a game's code needs the same */
#if defined(__clang__)
#pragma STDC FP_CONTRACT OFF
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")   /* gcc ignores the STDC pragma; its GNU mode (-std=gnu11) fuses on any FMA cpu */
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

typedef void (*dets_trace_fn)(const char *scenario, int step, uint64_t hash);
static uint64_t dets_h;
static void dets_reset(void) { dets_h = 1469598103934665603ull; }
static void dets_u(uint32_t u) { for (int k = 0; k < 4; k++) { dets_h ^= (u >> (k * 8)) & 0xFF; dets_h *= 1099511628211ull; } }
static void dets_f(float f) { uint32_t u; memcpy(&u, &f, 4); dets_u(u); }
static void dets_v(arc_v3 v) { dets_f(v.x); dets_f(v.y); dets_f(v.z); }

/* ---- 1. physics: a mixed pile, a triangle ramp, a hinge with limits and a motor, a rope, a kinematic pusher ---- */
static uint64_t dets_physics(dets_trace_fn trace) {
    dets_reset();
    arc_physics w = arc_physics_create(NULL);
    arc_physics_add_plane(w, arc_v3_make(0, 1, 0), 0);
    arc_v3 ramp[6] = { { -9, 2.5f, -2 }, { -9, 2.5f, 2 }, { -4, 0, 2 }, { -9, 2.5f, -2 }, { -4, 0, 2 }, { -4, 0, -2 } };
    arc_physics_add_triangles(w, ramp, 6);
    static const arc_quat rots[4] = { { 0, 0, 0, 1 }, { 0, 0.38268343f, 0, 0.92387953f }, { 0.25f, 0.25f, 0.25f, 0.8660254f }, { 0.70710678f, 0, 0, 0.70710678f } };
    int bodies[40], nb = 0;
    for (int i = 0; i < 24; i++) {
        int sh = i % 3;
        arc_body_desc d = { .shape = sh == 0 ? ARC_SHAPE_BOX : sh == 1 ? ARC_SHAPE_SPHERE : ARC_SHAPE_CAPSULE,
                            .pos = { -3 + (float)(i % 6) * 1.1f, 1 + (float)(i / 6) * 1.3f, (float)(i % 2) * 0.4f - 0.2f }, .rot = rots[i % 4],
                            .vel = { (float)(i % 5) * 0.3f - 0.6f, 0, (float)(i % 3) * 0.2f - 0.2f }, .restitution = (float)(i % 4) * 0.1f, .friction = 0.3f + (float)(i % 3) * 0.2f };
        d.size = sh == 0 ? arc_v3_make(0.3f + (float)(i % 4) * 0.05f, 0.25f, 0.35f) : sh == 1 ? arc_v3_make(0.25f + (float)(i % 5) * 0.03f, 0, 0) : arc_v3_make(0.15f, 0.25f, 0);
        bodies[nb++] = arc_physics_add(w, &d);
    }
    for (int i = 0; i < 4; i++) bodies[nb++] = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.3f }, .pos = { -8.5f + (float)i * 0.4f, 4 + (float)i * 0.7f, -1 + (float)i * 0.5f } });
    int pend = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.6f, 0.1f, 0.1f }, .pos = { 6.6f, 4, 0 }, .mass = 3 });
    int jh = arc_physics_add_joint(w, &(arc_joint_desc){ .a = pend, .anchor = { 6, 4, 0 }, .axis = { 0, 0, 1 }, .limit = true, .lower = -1, .upper = 1, .motor = true, .motor_speed = 2, .motor_torque = 4 });
    bodies[nb++] = pend;
    int prev = 0;
    for (int i = 0; i < 5; i++) {
        int s = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.12f }, .pos = { 8 + 0.3f * (float)i, 5, 0 }, .mass = 0.4f });
        arc_physics_add_joint(w, &(arc_joint_desc){ .type = ARC_JOINT_BALL, .a = s, .b = prev, .anchor = { 7.85f + 0.3f * (float)i, 5, 0 } });
        prev = s; bodies[nb++] = s;
    }
    int kin = arc_physics_add(w, &(arc_body_desc){ .type = ARC_BODY_KINEMATIC, .shape = ARC_SHAPE_BOX, .size = { 0.2f, 0.4f, 1.5f }, .pos = { -6, 0.4f, 0 } });
    bodies[nb++] = kin;
    for (int step = 1; step <= 480; step++) {
        arc_physics_move_kinematic(w, kin, arc_v3_make(-6 + (float)step * 0.02f, 0.4f, 0), (arc_quat){ 0, 0, 0, 1 });
        arc_physics_step(w, 1.0f / 60);
        for (int i = 0; i < nb; i++) {
            dets_v(arc_physics_pos(w, bodies[i])); arc_quat q = arc_physics_rot(w, bodies[i]); dets_f(q.x); dets_f(q.y); dets_f(q.z); dets_f(q.w);
            dets_v(arc_physics_vel(w, bodies[i])); dets_v(arc_physics_ang_vel(w, bodies[i])); dets_u(arc_physics_sleeping(w, bodies[i]));
        }
        dets_f(arc_physics_joint_angle(w, jh)); dets_f(arc_physics_joint_impulse(w, jh));
        arc_physics_event ev[64]; int ne = arc_physics_events(w, ev, 64); dets_u((uint32_t)ne);
        for (int e = 0; e < ne && e < 64; e++) { dets_u((uint32_t)ev[e].a); dets_u((uint32_t)ev[e].b); dets_f(ev[e].impulse); }
        if (trace && step % 20 == 0) trace("physics", step, dets_h);
    }
    arc_physics_destroy(w);
    return dets_h;
}

/* ---- 2. navigation: a navmesh from a grid with walls, path queries, agents walking ---- */
static bool dets_walk(uint8_t c) { return c == 0; }
static uint64_t dets_nav(dets_trace_fn trace) {
    dets_reset();
    static uint8_t cells[32 * 32];
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        bool wall = (x == 10 && y < 24) || (x == 21 && y > 8) || (y == 16 && x > 2 && x < 8) || ((x * 7 + y * 13) % 29 == 0);
        cells[y * 32 + x] = wall ? 1 : 0;
    }
    arc_grid g = { 32, 32, cells };
    arc_navmesh nm = arc_navmesh_create(NULL);
    arc_navmesh_add_grid(nm, &g, arc_v3_make(0, 0, 0), 1, dets_walk);
    arc_navmesh_build(nm);
    dets_u((uint32_t)arc_navmesh_triangles(nm)); dets_u((uint32_t)arc_navmesh_islands(nm));
    arc_rng r; arc_rng_seed(&r, 97, 3);
    for (int q = 0; q < 40; q++) {
        /* one draw per statement: C leaves the order of a call's arguments open (gcc and clang differ), so
           f(rng(), rng()) gives different points on different compilers */
        float ax = arc_rng_rangef(&r, 0.5f, 31.5f); float az = arc_rng_rangef(&r, 0.5f, 31.5f);
        float bx = arc_rng_rangef(&r, 0.5f, 31.5f); float bz = arc_rng_rangef(&r, 0.5f, 31.5f);
        arc_v3 a = arc_v3_make(ax, 0, az), b = arc_v3_make(bx, 0, bz);
        arc_v3 pts[128]; int n = arc_navmesh_path(nm, a, b, 0.3f, pts, 128);
        dets_u((uint32_t)n); for (int i = 0; i < n && i < 128; i++) dets_v(pts[i]);
        arc_v3 near; dets_u(arc_navmesh_nearest(nm, a, 3, &near)); dets_v(near);
        if (trace && q % 10 == 9) trace("nav paths", q + 1, dets_h);
    }
    arc_nav_agent ag[6];
    for (int i = 0; i < 6; i++) { arc_nav_agent_init(&ag[i], arc_v3_make(1.5f + (float)i, 0, 1.5f), 3 + (float)i * 0.25f, 0.3f); arc_nav_agent_goto(&ag[i], nm, arc_v3_make(30.5f - (float)i, 0, 30.5f)); }
    for (int step = 1; step <= 360; step++) {
        for (int i = 0; i < 6; i++) { arc_nav_agent_update(&ag[i], nm, 1.0f / 60); dets_v(ag[i].pos); }
        if (trace && step % 60 == 0) trace("nav agents", step, dets_h);
    }
    arc_navmesh_destroy(nm);
    return dets_h;
}

/* ---- 3. terrain: heights, normals, ray hits, splat rules and scatter (needs the renderer up: call from init) ---- */
static uint64_t dets_terrain(dets_trace_fn trace) {
    dets_reset();
    static float h[65 * 65];
    for (int j = 0; j < 65; j++) for (int i = 0; i < 65; i++) h[j * 65 + i] = 9 * arc_noise_fbm2((float)i * 0.07f, (float)j * 0.07f, 4, 31u) + 0.05f * (float)(i - j);
    arc_terrain t = arc_terrain_create(&(arc_terrain_desc){ .heights = h, .samples_x = 65, .samples_z = 65, .size_x = 48, .size_z = 48, .origin = { -24, 1, -24 }, .chunk = 16, .splat_res = 64, .layer_count = 0 });
    for (int j = 0; j <= 20; j++) for (int i = 0; i <= 20; i++) {
        float x = -24 + (float)i * 2.37f, z = -24 + (float)j * 2.41f;
        dets_f(arc_terrain_height(t, x, z)); dets_v(arc_terrain_normal(t, x, z));
    }
    if (trace) trace("terrain heights", 1, dets_h);
    for (int k = 0; k < 30; k++) {
        float d; arc_v3 p; bool hit = arc_terrain_raycast(t, arc_v3_make(-30 + (float)k * 2, 20, -20 + (float)k), arc_v3_make(1, -0.6f, 0.3f + (float)k * 0.01f), 200, &d, &p);
        dets_u(hit); dets_f(d); dets_v(p);
    }
    if (trace) trace("terrain rays", 2, dets_h);
    arc_mesh m = arc_mesh_cube(0xFFFFFF);
    arc_scatter s = arc_scatter_create(t, &(arc_scatter_desc){ .mesh = m, .density = 0.8f, .layer = -1, .slope_max = 30, .scale_min = 0.5f, .scale_max = 1.5f, .align = 0.5f, .sink = 0.1f, .seed = 9 });
    dets_u((uint32_t)arc_scatter_count(s));
    for (int i = 0; i < arc_scatter_count(s); i++) { arc_m4 M = arc_scatter_matrix(s, i); for (int c = 0; c < 16; c++) dets_f(M.m[c]); }
    if (trace) trace("terrain scatter", 3, dets_h);
    arc_scatter_destroy(s); arc_mesh_destroy(m); arc_terrain_destroy(t);
    return dets_h;
}
/* the splat rules need layers (they run on the splat map): a second terrain with 3 plain layers */
static uint64_t dets_splat(dets_trace_fn trace) {
    dets_reset();
    static float h[33 * 33];
    for (int j = 0; j < 33; j++) for (int i = 0; i < 33; i++) h[j * 33 + i] = 6 * arc_noise_fbm2((float)i * 0.11f, (float)j * 0.11f, 3, 5u);
    uint8_t px[4 * 4 * 4]; memset(px, 200, sizeof px);
    arc_texture tx = arc_texture_create_ex(4, 4, px, ARC_TEX_MESH), lay[3] = { tx, tx, tx };
    arc_terrain t = arc_terrain_create(&(arc_terrain_desc){ .heights = h, .samples_x = 33, .samples_z = 33, .size_x = 32, .size_z = 32, .splat_res = 32, .layers = { lay[0], lay[1], lay[2] }, .layer_count = 3 });
    arc_texture_destroy(tx);
    arc_splat_rule rules[2] = { { .layer = 1, .slope_min = 20, .slope_max = 90, .blend = 6 }, { .layer = 2, .height_min = 3.5f, .height_max = 99, .blend = 0.7f } };
    arc_terrain_splat_rules(t, rules, 2);
    arc_terrain_paint(t, 10, 12, 4, 1, 0.7f);
    for (int j = 0; j <= 16; j++) for (int i = 0; i <= 16; i++) for (int l = 0; l < 3; l++) dets_f(arc_terrain_layer_weight(t, (float)i * 1.93f, (float)j * 1.97f, l));
    if (trace) trace("terrain splat", 1, dets_h);
    arc_terrain_destroy(t);
    return dets_h;
}

/* ---- 4. maths: arc_dmath, arc_rng, noise ---- */
static uint64_t dets_math(dets_trace_fn trace) {
    dets_reset();
    for (int i = 0; i < 400; i++) {
        float x = -50 + (float)i * 0.2513f, y = (float)(i % 37) * 0.371f - 6;
        dets_f(arc_sinf(x)); dets_f(arc_cosf(x)); dets_f(arc_tanf(x * 0.03f)); dets_f(arc_atan2f(y, x)); dets_f(arc_expf(x * 0.1f)); dets_f(arc_logf(fabsf(x) + 0.5f));
        dets_f(arc_powf(fabsf(y) + 0.1f, x * 0.05f)); dets_f(arc_sqrtf(fabsf(x))); dets_f(arc_acosf(arc_sinf(x))); dets_f(arc_fmodf(x, 3.3f));
    }
    if (trace) trace("math dmath", 1, dets_h);
    arc_rng r; arc_rng_seed(&r, 12345, 7);
    for (int i = 0; i < 1000; i++) { dets_u((uint32_t)arc_rng_range(&r, -1000000, 1000000)); dets_f(arc_rng_rangef(&r, -3, 3)); }
    for (int i = 0; i < 500; i++) { float x = (float)i * 0.173f - 40, z = (float)i * 0.311f; dets_f(arc_noise_value2(x, z, 77u)); dets_f(arc_noise_fbm2(x * 0.1f, z * 0.1f, 5, 78u)); }
    if (trace) trace("math rng noise", 2, dets_h);
    return dets_h;
}

/* ---- 5. (0.9.8) ragdolls: two bodies falling onto a slope and each other, cones, twist and hinges at work ---- */
static void dets_bodies(arc_physics w, const int *b, int n) {
    for (int i = 0; i < n; i++) {
        dets_v(arc_physics_pos(w, b[i])); arc_quat q = arc_physics_rot(w, b[i]); dets_f(q.x); dets_f(q.y); dets_f(q.z); dets_f(q.w);
        dets_v(arc_physics_vel(w, b[i])); dets_v(arc_physics_ang_vel(w, b[i])); dets_u(arc_physics_sleeping(w, b[i]));
    }
}
static uint64_t dets_ragdoll(dets_trace_fn trace) {
    dets_reset();
    arc_physics w = arc_physics_create(NULL);
    arc_physics_add_plane(w, arc_v3_norm(arc_v3_make(0.25f, 1, 0.1f)), 0);
    arc_ragdoll r1, r2;
    arc_ragdoll_humanoid(w, arc_v3_make(0, 1.2f, 0), 0.7f, 1.8f, &(arc_ragdoll_desc){ .mass = 80, .vel = { 1.5f, 0, -0.5f } }, &r1);
    arc_ragdoll_humanoid(w, arc_v3_make(0.8f, 2.6f, 0.1f), -1.1f, 1.6f, &(arc_ragdoll_desc){ .mass = 60, .layer = 2, .mask = ~2u, .vel = { 0, -1, 0 } }, &r2);
    int crate = arc_physics_add(w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.4f, 0.4f, 0.4f }, .pos = { -1.5f, 0.9f, 0.2f }, .mass = 30 });
    for (int step = 1; step <= 420; step++) {
        arc_physics_step(w, 1.0f / 60);
        dets_bodies(w, r1.body, ARC_RAG_PARTS); dets_bodies(w, r2.body, ARC_RAG_PARTS); dets_bodies(w, &crate, 1);
        for (int p = 1; p < ARC_RAG_PARTS; p++) { dets_f(arc_physics_joint_angle(w, r1.joint[p])); dets_f(arc_physics_joint_swing(w, r2.joint[p])); }
        if (trace && step % 20 == 0) trace("ragdoll", step, dets_h);
    }
    arc_physics_destroy(w);
    return dets_h;
}

/* ---- 6. (0.9.8) rollback: two machines in this process run a real-time rollback match over a simulated network
   (lag, jitter, loss, duplicates; the 0.8.1 self-test's machinery and ARC's own rollback code). Each runs the same
   little physics game: two players push balls (their inputs) into a ragdoll and a stack of crates. Predictions go wrong
   and ticks are run again from snapshots (arc_physics_save / _load) all the time; the confirmed state must be the same
   on both machines and the same bits on every compiler. The hash covers machine 0's state at tick 600, both machines'
   agreement (state and ARC's own state hashes) and that rollbacks happened. */
void *arc__net_test_machine(void); void *arc__net_test_use(void *m); void arc__net_test_time(double t); void arc__net_test_free(void *m);
void arc__rb_test_hooks(const void *hooks); void arc__rb_test_step(void); int64_t arc__rb_test_tick(void); int64_t arc__rb_test_final(void);
void arc__rb_test_stop_at(int64_t t); uint64_t arc__rb_test_hash_at(int64_t f);
typedef struct {   /* the same layout as arc_rollback.c's rb_hooks */
    size_t (*save)(void *buf, size_t cap); bool (*load)(const void *buf, size_t n);
    void (*tick)(void); size_t (*state)(void *buf, size_t cap); size_t (*input)(void *buf, size_t cap); void (*start)(uint64_t seed);
} dets_rb_hooks;
typedef struct { uint32_t tick; int ball[2], crate[6], nb; arc_ragdoll rag; arc_physics w; } dets_rb_game;
static dets_rb_game dets_rb[2]; static int dets_rb_cur; static void *dets_rb_m[2]; static double dets_rb_t;
static void dets_rb_build(dets_rb_game *g) {
    if (g->w.id) arc_physics_destroy(g->w);
    memset(g, 0, sizeof *g);
    g->w = arc_physics_create(&(arc_physics_desc){ .max_bodies = 64 });
    arc_physics_add_plane(g->w, arc_v3_make(0, 1, 0), 0);
    for (int p = 0; p < 2; p++) g->ball[p] = arc_physics_add(g->w, &(arc_body_desc){ .shape = ARC_SHAPE_SPHERE, .size = { 0.35f }, .pos = { -3 + 6 * (float)p, 0.35f, -2 }, .mass = 25, .friction = 0.6f });
    for (int i = 0; i < 6; i++) g->crate[i] = arc_physics_add(g->w, &(arc_body_desc){ .shape = ARC_SHAPE_BOX, .size = { 0.3f, 0.3f, 0.3f }, .pos = { 2.5f, 0.3f + 0.61f * (float)(i % 3), 1.5f + 0.7f * (float)(i / 3) }, .mass = 6 });
    arc_ragdoll_humanoid(g->w, arc_v3_make(0, 0.01f, 0), 0.3f, 1.75f, &(arc_ragdoll_desc){ .mass = 70 }, &g->rag);
}
static size_t dets_rb_save(void *buf, size_t cap) {
    dets_rb_game *g = &dets_rb[dets_rb_cur]; size_t pn = arc_physics_save(g->w, NULL, 0), need = 4 + pn;
    if (cap >= need) { memcpy(buf, &g->tick, 4); arc_physics_save(g->w, (uint8_t *)buf + 4, pn); }
    return need;
}
static bool dets_rb_load(const void *buf, size_t n) {
    dets_rb_game *g = &dets_rb[dets_rb_cur]; if (n < 4) return false;
    memcpy(&g->tick, buf, 4); return arc_physics_load(g->w, (const uint8_t *)buf + 4, n - 4);
}
static size_t dets_rb_state(void *buf, size_t cap) {   /* what ARC hashes for desync checks: the bodies, through getters */
    dets_rb_game *g = &dets_rb[dets_rb_cur]; int nb = arc_physics_body_count(g->w); size_t need = 4 + (size_t)nb * 13 * 4;
    if (cap >= need) {
        uint8_t *o = (uint8_t *)buf; memcpy(o, &g->tick, 4); o += 4;
        for (int b = 1; b <= nb; b++) {
            arc_v3 p = arc_physics_pos(g->w, b), v = arc_physics_vel(g->w, b), a = arc_physics_ang_vel(g->w, b); arc_quat q = arc_physics_rot(g->w, b);
            float f[13] = { p.x, p.y, p.z, q.x, q.y, q.z, q.w, v.x, v.y, v.z, a.x, a.y, a.z }; memcpy(o, f, sizeof f); o += sizeof f;
        }
    }
    return need;
}
static void dets_rb_start(uint64_t seed) { (void)seed; dets_rb_build(&dets_rb[dets_rb_cur]); }
static void dets_rb_tick(void) {
    dets_rb_game *g = &dets_rb[dets_rb_cur]; g->tick++;
    for (int p = 0; p < 2; p++) {
        int n = 0; const uint8_t *in = (const uint8_t *)arc_net_input(p, &n); uint8_t b = in && n > 0 ? in[0] : 0;
        float fx = (float)((b & 1) - ((b >> 1) & 1)), fz = (float)(((b >> 2) & 1) - ((b >> 3) & 1));
        if (fx != 0 || fz != 0) arc_physics_force(g->w, g->ball[p], arc_v3_make(fx * 400, 0, fz * 400), arc_physics_pos(g->w, g->ball[p]));
        if (b & 16) arc_physics_impulse(g->w, g->ball[p], arc_v3_make(0, 60, 0), arc_physics_pos(g->w, g->ball[p]));
    }
    arc_physics_step(g->w, 1.0f / 60);
}
static size_t dets_rb_input(void *buf, size_t cap) {   /* each player steers toward the middle on its own rhythm, hops now and then */
    (void)cap; int64_t t = arc__rb_test_tick(); int me = dets_rb_cur, per = 7 + 5 * me;
    static const uint8_t moves[8] = { 1 | 4, 4, 2 | 4, 1, 2 | 8, 8, 1 | 8, 2 };
    uint8_t b = moves[(t / per + (int64_t)me * 3) & 7];
    if (me == 0) b = (uint8_t)(b & ~2); else b = (uint8_t)(b & ~1);   /* toward the ragdoll in x */
    if ((t / per) % 9 == 4) b |= 16;
    ((uint8_t *)buf)[0] = b; return 1;
}
static const dets_rb_hooks dets_rb_h = { dets_rb_save, dets_rb_load, dets_rb_tick, dets_rb_state, dets_rb_input, dets_rb_start };
static void dets_rb_on(int i) { arc__net_test_use(dets_rb_m[i]); dets_rb_cur = i; }
static void dets_rb_frames(int n) {
    for (int k = 0; k < n; k++) { dets_rb_t += 1.0 / 60.0; arc__net_test_time(dets_rb_t); for (int i = 0; i < 2; i++) { dets_rb_on(i); arc__rb_test_step(); arc_net_ev ev; while (arc_net_event(&ev)) {} } }
}
static uint64_t dets_rollback(dets_trace_fn trace) {
    dets_reset();
    void *was = arc__net_test_use(NULL); arc__net_test_use(was);
    static const char *sims[2] = { "lag=50,jitter=40,loss=0.08,dup=0.05,seed=4", "lag=70,jitter=30,loss=0.08,dup=0.05,seed=9" };
    dets_rb_t = 0; arc__net_test_time(0);
    for (int i = 0; i < 2; i++) {
        dets_rb_m[i] = arc__net_test_machine(); dets_rb_on(i); memset(&dets_rb[i], 0, sizeof dets_rb[i]);
        arc_net_desc d; memset(&d, 0, sizeof d); d.game = "dets_rb"; d.version = "1"; d.max_players = 2; d.port = 7298; d.mode = ARC_NET_ROLLBACK;
        arc_net_setup(&d); arc_net_set_name(i ? "B" : "A"); arc__rb_test_hooks(&dets_rb_h); arc_net_sim(sims[i]);
    }
    bool ok = true; const int64_t stop = 600;
    dets_rb_on(0); ok = ok && arc_net_host(NULL); dets_rb_on(1); arc_net_join("mem:7298");
    dets_rb_frames(40);
    dets_rb_on(0); ok = ok && arc_net_players(NULL, 0) == 2 && arc_net_start();
    dets_rb_frames(30);
    for (int i = 0; i < 2; i++) { dets_rb_on(i); arc__rb_test_stop_at(stop); }
    int64_t traced = 0;
    for (int k = 0; k < 4000 && ok; k++) {
        bool done = true; for (int i = 0; i < 2; i++) { dets_rb_on(i); if (arc__rb_test_final() < stop) done = false; }
        if (done) { dets_rb_frames(3); break; }
        dets_rb_frames(1);
        dets_rb_on(0); int64_t f = arc__rb_test_final();
        if (trace) while (traced + 60 <= f) { traced += 60; trace("rollback confirmed", (int)traced, arc__rb_test_hash_at(traced)); }
    }
    dets_rb_on(0); arc_net_rb_stats s0 = arc_net_stats(); dets_rb_on(1); arc_net_rb_stats s1 = arc_net_stats();
    uint64_t hb[2]; for (int i = 0; i < 2; i++) { dets_rb_on(i); hb[i] = arc__rb_test_hash_at(stop); }
    /* machine 0's state at the stop tick, through the getters */
    dets_rb_game *g0 = &dets_rb[0], *g1 = &dets_rb[1];
    int nb0 = g0->w.id ? arc_physics_body_count(g0->w) : 0, nb1 = g1->w.id ? arc_physics_body_count(g1->w) : -1;
    dets_u(g0->tick); dets_u((uint32_t)nb0);
    bool same = nb0 == nb1 && g0->tick == g1->tick && g0->tick == (uint32_t)stop;
    for (int b = 1; b <= nb0; b++) {
        dets_v(arc_physics_pos(g0->w, b)); arc_quat q = arc_physics_rot(g0->w, b); dets_f(q.x); dets_f(q.y); dets_f(q.z); dets_f(q.w); dets_v(arc_physics_vel(g0->w, b));
        if (same) { arc_v3 a = arc_physics_pos(g0->w, b), c = arc_physics_pos(g1->w, b); same = memcmp(&a, &c, sizeof a) == 0; }
    }
    dets_u(ok); dets_u(same); dets_u(hb[0] != 0 && hb[0] == hb[1]); dets_u(s0.rollbacks > 10 && s1.rollbacks > 10); dets_u(s0.desync_tick < 0 && s1.desync_tick < 0);
    if (!ok || !same || hb[0] != hb[1] || s0.rollbacks <= 10) printf("ARC note determinism rollback: started %d, machines agree %d, hashes %016llx %016llx, rollbacks %d %d\n",
        ok, same, (unsigned long long)hb[0], (unsigned long long)hb[1], s0.rollbacks, s1.rollbacks);
    for (int i = 0; i < 2; i++) { dets_rb_on(i); arc_physics_destroy(dets_rb[i].w); dets_rb[i].w.id = 0; arc__net_test_free(dets_rb_m[i]); dets_rb_m[i] = NULL; }
    arc__net_test_use(was);
    return dets_h;
}

typedef struct { const char *name; uint64_t (*run)(dets_trace_fn); } dets_scenario;
static const dets_scenario DETS[] = { { "physics", dets_physics }, { "navigation", dets_nav }, { "terrain", dets_terrain }, { "splat", dets_splat }, { "maths", dets_math },
                                      { "ragdoll", dets_ragdoll }, { "rollback", dets_rollback } };
#define DETS_N 7
