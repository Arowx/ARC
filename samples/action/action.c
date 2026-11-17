/* ARC sample (0.10.0): AI brains, senses, shots and camera rigs together.
   You are the green droid; four red security droids patrol the deck. They see in a cone (and only after a moment),
   hear your shots, remember where they last saw you, call each other, search, and flee when badly hurt. You fire
   hitscan bolts (tracer, scorch mark, sparks); they fire slow plasma that you can dodge and that lights the walls.
   Keys: W/S move, A/D turn, SPACE fire, 1-4 views (top-down, follow, orbit, first person: they blend), V shows what
   the droids see. Agent commands: `fire`, `face DEG`, `goto X Z`, `view N`; ARC's own `brain droid N STATE`,
   `cam shake N`. dump(): the player, each droid's state and health, the view. */
#include "../../include/arc.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#define MW 20
#define MH 14
static const char *MAP[MH] = {
    "####################",
    "#..........#.......#",
    "#..........#.......#",
    "#...####...#...#...#",
    "#...#..........#...#",
    "#...#......##..#...#",
    "#..........##......#",
    "#..................#",
    "#...##.........#...#",
    "#...##....###..#...#",
    "#.........#........#",
    "#.........#........#",
    "#..................#",
    "####################" };
static bool wall(int x, int z, void *u) { (void)u; return x < 0 || z < 0 || x >= MW || z >= MH || MAP[z][x] == '#'; }
static bool walkable(uint8_t v) { return v == 0; }

enum { PATROL, ALERT, ATTACK, SEARCH, FLEE };
enum { B_AWARE, B_HEALTH, B_KNOWS, B_SEES };   /* blackboard floats */
enum { T_HEARD };                              /* triggers */
#define NDROIDS 4
typedef struct {
    bool alive; arc_v3 pos; float yaw; int hp; float cool, flash;
    arc_nav_agent nav; arc_brain brain; int sense, way, search_i, nsearch; arc_v3 search[6];
} Droid;
typedef struct {
    arc_v3 pos; float yaw; int hp; float cool, hurt; int kills;
    Droid d[NDROIDS];
    arc_senses senses;
    arc_cam_rig cam; int view; bool show_senses;
} Game;
static Game G;
static arc_brain_def DEF;
static arc_navmesh NAV;
static arc_mesh m_cube, m_floor, m_body;
static arc_fx SPARKS = { .life = 0.35f, .speed = 4, .speed_var = 2, .spread = 1.2f, .gravity = { 0, -9.8f, 0 }, .size = 0.06f, .color = 0xFFD070, .color_end = 0xFF5010, .glow = 2, .additive = true, .stretch = 0.03f, .burst = 14 };
static int FX_SPARKS;
static const arc_v3 WAY[NDROIDS][2] = { { { 2.5f, 0, 2.5f }, { 9.5f, 0, 2.5f } }, { { 13.5f, 0, 2.5f }, { 17.5f, 0, 6.5f } }, { { 17.5f, 0, 11.5f }, { 11.5f, 0, 11.5f } }, { { 2.5f, 0, 11.5f }, { 2.5f, 0, 6.5f } } };

static arc_v3 fwd(float yaw) { return arc_v3_make(arc_sinf(yaw), 0, arc_cosf(yaw)); }
static float yaw_to(arc_v3 from, arc_v3 to) { return arc_atan2f(to.x - from.x, to.z - from.z); }
static bool los(arc_v3 a, arc_v3 b, void *u) {   /* the walls only (shots' grid with no targets: mask leaves them out) */
    (void)u; arc_v3 d = arc_v3_sub(b, a); float L = arc_v3_len(d); if (L < 1e-4f) return true;
    arc_shot_desc s; memset(&s, 0, sizeof s); s.from = a; s.dir = d; s.range = L; s.mask = 0x80000000u;
    return !arc_shot_cast(&s, NULL);
}
static Droid *droid_of(arc_brain *b) { return (Droid *)((char *)b - offsetof(Droid, brain)); }
static const arc_senses_memory *knows(const Droid *d) { return arc_senses_knows(&G.senses, d->sense, 1); }
/* ---- brain states ---- */
static void patrol_tick(arc_brain *b, void *u, float dt) {
    (void)u; (void)dt; Droid *d = droid_of(b);
    if (!d->nav.moving || d->nav.arrived) { d->way ^= 1; arc_nav_agent_goto(&d->nav, NAV, WAY[d - G.d][d->way]); }
}
static void alert_enter(arc_brain *b, void *u, float dt) { (void)u; (void)dt; arc_nav_agent_stop(&droid_of(b)->nav); }
static void alert_tick(arc_brain *b, void *u, float dt) {   /* turn toward what it noticed */
    (void)u; (void)dt; Droid *d = droid_of(b); const arc_senses_agent *a = &G.senses.agent[d->sense];
    arc_v3 at = d->pos; bool any = false;
    for (int k = 0; k < ARC_SENSES_TARGETS; k++) if (a->mem[k].target == 1) { at = a->mem[k].pos; any = true; }   /* known or only glimpsed */
    if (!any && a->heard_noise) { at = a->noise_pos; any = true; }
    if (!any) return;
    float want = yaw_to(d->pos, at), diff = want - d->yaw;
    while (diff > 3.14159265f) diff -= 6.2831853f;
    while (diff < -3.14159265f) diff += 6.2831853f;
    d->yaw += diff * 0.15f;
}
static void attack_enter(arc_brain *b, void *u, float dt) { (void)u; (void)dt; Droid *d = droid_of(b); arc_nav_agent_stop(&d->nav); arc_senses_alert(&G.senses, d->sense, 7.0f); d->cool = 0.4f; }
static void attack_tick(arc_brain *b, void *u, float dt) {
    (void)u; Droid *d = droid_of(b); d->yaw = yaw_to(d->pos, G.pos);
    if ((d->cool -= dt) > 0) return;
    d->cool = 0.9f;
    arc_shot_desc s; memset(&s, 0, sizeof s);
    s.from = arc_v3_add(d->pos, arc_v3_make(fwd(d->yaw).x * 0.45f, 0.6f, fwd(d->yaw).z * 0.45f)); s.dir = arc_v3_sub(arc_v3_add(G.pos, arc_v3_make(0, 0.5f, 0)), s.from);
    s.speed = 8; s.radius = 0.08f; s.range = 25; s.owner = 10 + (int)(d - G.d); s.mask = 1;
    s.tracer_rgb = 0xFF4060; s.tracer_width = 0.12f; s.projectile_size = 0.6f; s.flash_light = 3; s.flash_rgb = 0xFF3050;
    s.decal_size = 0.35f; s.impact_fx = FX_SPARKS;
    arc_shot_fire(&s);
}
static void search_enter(arc_brain *b, void *u, float dt) {
    (void)u; (void)dt; Droid *d = droid_of(b); const arc_senses_memory *m = knows(d); const arc_senses_agent *a = &G.senses.agent[d->sense];
    arc_v3 at = m ? m->pos : a->heard_noise ? a->noise_pos : d->pos;
    d->nsearch = arc_senses_search_points(NAV, arc_v3_make(at.x, 0, at.z), 2.5f, 6, d->search); d->search_i = 0;
    if (d->nsearch) arc_nav_agent_goto(&d->nav, NAV, d->search[0]);
}
static void search_tick(arc_brain *b, void *u, float dt) {
    (void)u; (void)dt; Droid *d = droid_of(b);
    if (d->nav.arrived && d->search_i + 1 < d->nsearch) arc_nav_agent_goto(&d->nav, NAV, d->search[++d->search_i]);
}
static void flee_tick(arc_brain *b, void *u, float dt) {
    (void)u; (void)dt; Droid *d = droid_of(b);
    if (d->nav.moving && !d->nav.arrived) return;
    arc_v3 away = arc_v3_add(d->pos, arc_v3_scale(arc_v3_norm(arc_v3_sub(d->pos, G.pos)), 6.0f)), q;
    if (arc_navmesh_nearest(NAV, away, 6.0f, &q)) arc_nav_agent_goto(&d->nav, NAV, q);
}
static void make_brain(void) {
    arc_brain_def *D = &DEF; D->name = "droid";
    arc_brain_def_state(D, "PATROL", NULL, patrol_tick, NULL);
    arc_brain_def_state(D, "ALERT", alert_enter, alert_tick, NULL);
    arc_brain_def_state(D, "ATTACK", attack_enter, attack_tick, NULL);
    arc_brain_def_state(D, "SEARCH", search_enter, search_tick, NULL);
    arc_brain_def_state(D, "FLEE", NULL, flee_tick, NULL);
    arc_brain_def_transition(D, ARC_BRAIN_ANY, FLEE, ARC_BRAIN_IF(ARC_BRAIN_F_LE, B_HEALTH, 0.25f), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, PATROL, ALERT, ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_AWARE, 0.05f), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, PATROL, ALERT, ARC_BRAIN_IF(ARC_BRAIN_TRIGGER, T_HEARD, 0), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, PATROL, SEARCH, ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_KNOWS, 1), ARC_BRAIN_NONE);     /* told by another */
    arc_brain_def_transition(D, ALERT, ATTACK, ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_SEES, 1), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, ALERT, SEARCH, ARC_BRAIN_IF(ARC_BRAIN_TIME_GE, 0, 1.2f), ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_KNOWS, 1));
    arc_brain_def_transition(D, ALERT, PATROL, ARC_BRAIN_IF(ARC_BRAIN_TIME_GE, 0, 2.5f), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, ATTACK, SEARCH, ARC_BRAIN_IF(ARC_BRAIN_F_LE, B_SEES, 0), ARC_BRAIN_IF(ARC_BRAIN_TIME_GE, 0, 0.3f));
    arc_brain_def_transition(D, SEARCH, ATTACK, ARC_BRAIN_IF(ARC_BRAIN_F_GE, B_SEES, 1), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, SEARCH, PATROL, ARC_BRAIN_IF(ARC_BRAIN_F_LE, B_KNOWS, 0), ARC_BRAIN_NONE);
    arc_brain_def_transition(D, SEARCH, PATROL, ARC_BRAIN_IF(ARC_BRAIN_TIME_GE, 0, 10), ARC_BRAIN_NONE);
    D->f_name[B_AWARE] = "aware"; D->f_name[B_HEALTH] = "health";
}
/* ---- views ---- */
static arc_cam_rig view_rig(int v) {
    arc_cam_rig r; memset(&r, 0, sizeof r);
    r.target = arc_v3_add(G.pos, arc_v3_make(0, 0.5f, 0)); r.yaw = G.yaw; r.follow_lag = 0.15f;
    switch (v) {
    case 0: r.kind = ARC_RIG_TOPDOWN; r.ortho_half = 7.5f; r.has_bounds = true; r.bounds[0] = 0; r.bounds[1] = 0; r.bounds[2] = MW; r.bounds[3] = MH; r.yaw = 3.14159265f; break;
    case 1: r.kind = ARC_RIG_FOLLOW; r.distance = 4.5f; r.height = 2.2f; r.aim_height = 0.3f; r.look_ahead = 1.5f; r.collide_radius = 0.3f; break;
    case 2: r.kind = ARC_RIG_ORBIT; r.distance = 7; r.pitch = 0.75f; r.collide_radius = 0.3f; r.yaw = G.yaw + 0.6f; break;
    default: r.kind = ARC_RIG_FIRST; r.target = arc_v3_add(G.pos, arc_v3_make(0, 0.7f, 0)); r.follow_lag = 0.05f; break;
    }
    return r;
}
static void set_view(int v) { if (v < 0 || v > 3) return; arc_cam_rig to = view_rig(v); G.view = v; arc_cam_rig_blend(&G.cam, &to, 0.5f); }

static void init(void) {
    memset(&G, 0, sizeof G);
    if (!m_cube.id) {
        m_cube = arc_mesh_cube(0xFFFFFF); arc_mesh_set_name(m_cube, "wall");
        m_floor = arc_mesh_cube(0xFFFFFF); arc_mesh_set_name(m_floor, "floor");
        m_body = arc_mesh_cylinder(0xFFFFFF, 20); arc_mesh_set_name(m_body, "droid");
        FX_SPARKS = arc_shots_fx(&SPARKS);
        make_brain();
        arc_grid g; arc_grid_init(&g, NULL, MW, MH);
        for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) arc_grid_set(&g, x, z, (uint8_t)(MAP[z][x] == '#'));
        NAV = arc_navmesh_create(NULL); arc_navmesh_add_grid(NAV, &g, arc_v3_make(0, 0, 0), 1, walkable); arc_navmesh_build(NAV);
        free(g.cells);
        arc_shots_set_world((arc_cworld){ 0 }, (arc_physics){ 0 }, wall, NULL);
        arc_cam_rig_set_world((arc_cworld){ 0 }, (arc_physics){ 0 }, 0, wall, NULL, 1);
    }
    G.pos = arc_v3_make(6.5f, 0, 7.5f); G.yaw = 0; G.hp = 10;
    arc_senses_init(&G.senses);
    for (int i = 0; i < NDROIDS; i++) {
        Droid *d = &G.d[i]; d->alive = true; d->hp = 4; d->pos = WAY[i][0]; d->yaw = yaw_to(WAY[i][0], WAY[i][1]);
        arc_nav_agent_init(&d->nav, d->pos, 1.6f, 0.3f);
        arc_brain_init(&d->brain, PATROL);
        d->sense = arc_senses_add(&G.senses, d->pos, fwd(d->yaw));
        G.senses.agent[d->sense].eye_height = 0.7f; G.senses.agent[d->sense].range = 9; G.senses.agent[d->sense].build_up = 0.6f; G.senses.agent[d->sense].forget = 8;
    }
    arc_brain_watch("droid", &G.d[0].brain, NDROIDS, sizeof(Droid), &DEF);
    arc_senses_watch(&G.senses, "droids");
    G.view = 1; G.cam = view_rig(1);
    arc_shots_clear();
}
static void fire(void) {
    if (G.cool > 0 || G.hp <= 0) return;
    G.cool = 0.25f;
    arc_shot_desc s; memset(&s, 0, sizeof s);
    s.from = arc_v3_add(G.pos, arc_v3_make(fwd(G.yaw).x * 0.45f, 0.55f, fwd(G.yaw).z * 0.45f)); s.dir = fwd(G.yaw); s.range = 30; s.owner = 1; s.mask = 1;
    s.tracer_rgb = 0x80FFB0; s.tracer_width = 0.04f; s.tracer_life = 0.1f; s.flash_light = 2; s.flash_rgb = 0x80FFB0;
    s.decal_size = 0.3f; s.impact_fx = FX_SPARKS;
    arc_shot_fire(&s);
    arc_senses_noise(&G.senses, G.pos, 10, 1, 1, 1);   /* droids within 10 hear it (5 through walls) */
}
static void tick(void) {
    float dt = arc_dt();
    /* the player */
    if (G.hp > 0) {
        if (arc_key_down(ARC_KEY_A)) G.yaw += 2.5f * dt;
        if (arc_key_down(ARC_KEY_D)) G.yaw -= 2.5f * dt;
        float mv = (arc_key_down(ARC_KEY_W) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_S) ? 1.0f : 0.0f);
        arc_v3 np = arc_v3_add(G.pos, arc_v3_scale(fwd(G.yaw), mv * 3.0f * dt));
        if (!wall((int)floorf(np.x + (np.x > G.pos.x ? 0.3f : -0.3f)), (int)floorf(G.pos.z), NULL)) G.pos.x = np.x;
        if (!wall((int)floorf(G.pos.x), (int)floorf(np.z + (np.z > G.pos.z ? 0.3f : -0.3f)), NULL)) G.pos.z = np.z;
        if (arc_key_down(ARC_KEY_SPACE)) fire();
    }
    if (G.cool > 0) G.cool -= dt;
    if (G.hurt > 0) G.hurt -= dt;
    for (int k = 0; k < 4; k++) if (arc_key_pressed(ARC_KEY_1 + k)) set_view(k);
    if (arc_key_pressed(ARC_KEY_V)) G.show_senses = !G.show_senses;
    /* targets for shots and senses */
    arc_shot_target tg[1 + NDROIDS]; int nt = 0;
    tg[nt++] = (arc_shot_target){ 1, 1, arc_v3_add(G.pos, arc_v3_make(0, 0.3f, 0)), arc_v3_add(G.pos, arc_v3_make(0, 0.8f, 0)), 0.3f };
    for (int i = 0; i < NDROIDS; i++) if (G.d[i].alive) tg[nt++] = (arc_shot_target){ 10 + i, 1, arc_v3_add(G.d[i].pos, arc_v3_make(0, 0.3f, 0)), arc_v3_add(G.d[i].pos, arc_v3_make(0, 0.8f, 0)), 0.3f };
    arc_shots_targets(tg, nt);
    arc_senses_target me; memset(&me, 0, sizeof me); me.id = 1; me.npoints = 5;
    me.points[0] = arc_v3_add(G.pos, arc_v3_make(0, 0.6f, 0)); me.points[1] = arc_v3_add(G.pos, arc_v3_make(0.25f, 0.4f, 0)); me.points[2] = arc_v3_add(G.pos, arc_v3_make(-0.25f, 0.4f, 0));
    me.points[3] = arc_v3_add(G.pos, arc_v3_make(0, 0.4f, 0.25f)); me.points[4] = arc_v3_add(G.pos, arc_v3_make(0, 0.4f, -0.25f));
    for (int i = 0; i < NDROIDS; i++) if (G.d[i].alive) arc_senses_set(&G.senses, G.d[i].sense, G.d[i].pos, fwd(G.d[i].yaw));
    arc_senses_update(&G.senses, dt, &me, G.hp > 0 ? 1 : 0, los, NULL);
    /* droids: what they know onto the blackboard, then the brain, then movement */
    for (int i = 0; i < NDROIDS; i++) {
        Droid *d = &G.d[i]; if (!d->alive) continue;
        const arc_senses_agent *a = &G.senses.agent[d->sense]; const arc_senses_memory *m = knows(d);
        d->brain.f[B_AWARE] = 0; for (int k = 0; k < ARC_SENSES_TARGETS; k++) if (a->mem[k].target == 1) d->brain.f[B_AWARE] = a->mem[k].awareness;
        d->brain.f[B_HEALTH] = (float)d->hp / 4.0f; d->brain.f[B_KNOWS] = m ? 1.0f : 0.0f; d->brain.f[B_SEES] = m && m->seen_now ? 1.0f : 0.0f;
        if (a->heard_noise && a->noise_when == G.senses.time) arc_brain_trigger(&d->brain, T_HEARD);
        arc_brain_update(&d->brain, &DEF, d, dt);
        arc_nav_agent_update(&d->nav, NAV, dt);
        arc_v3 v = d->nav.vel; if (v.x * v.x + v.z * v.z > 0.04f && d->brain.state != ATTACK && d->brain.state != ALERT) d->yaw = arc_atan2f(v.x, v.z);
        d->pos = d->nav.pos;
        if (d->flash > 0) d->flash -= dt;
    }
    /* shots, then what they hit */
    arc_shots_update(dt);
    arc_shot_hit hits[32]; int nh = arc_shots_hits(hits, 32);
    for (int k = 0; k < nh; k++) {
        if (hits[k].kind != ARC_HIT_TARGET) continue;
        if (hits[k].id == 1 && G.hp > 0) { G.hp--; G.hurt = 0.3f; arc_cam_rig_shake(&G.cam, 0.45f); }
        int i = hits[k].id - 10;
        if (i >= 0 && i < NDROIDS && G.d[i].alive) {
            Droid *d = &G.d[i]; d->hp--; d->flash = 0.15f;
            arc_senses_noise(&G.senses, d->pos, 6, 2, 1, 1);
            if (d->hp <= 0) { d->alive = false; arc_senses_remove(&G.senses, d->sense); G.kills++; arc_cam_rig_shake(&G.cam, 0.3f); }
        }
    }
    /* the camera */
    arc_cam_rig want = view_rig(G.view);
    G.cam.target = want.target; G.cam.yaw = want.yaw; G.cam.pitch = want.pitch;
    arc_cam_rig_update(&G.cam, dt);
}
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = arc_cam_rig_camera(&G.cam); arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.4f), 0xB8C4D8, 0x303848);
    arc_draw_mesh(m_floor, arc_m4_trs_y(arc_v3_make(MW * 0.5f, -0.05f, MH * 0.5f), 0, arc_v3_make(MW, 0.1f, MH)), 0x5A606A);
    for (int z = 0; z < MH; z++) for (int x = 0; x < MW; x++) if (MAP[z][x] == '#') arc_draw_mesh(m_cube, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, 0.6f, (float)z + 0.5f), 0, arc_v3_make(1, 1.2f, 1)), 0x8A93A6);
    static const uint32_t STATE_RGB[] = { 0xC03030, 0xE0A020, 0xFF2020, 0xE07020, 0x8040C0 };
    if (G.hp > 0) arc_draw_mesh_styled(m_body, arc_m4_trs_y(arc_v3_add(G.pos, arc_v3_make(0, 0.4f, 0)), G.yaw, arc_v3_make(0.6f, 0.8f, 0.6f)), &(arc_mesh_style){ .tint = 0x40D070, .whiten = G.hurt > 0 ? 0.7f : 0 });
    if (G.hp > 0 && G.view != 3) arc_draw_cube(arc_v3_add(G.pos, arc_v3_make(fwd(G.yaw).x * 0.32f, 0.6f, fwd(G.yaw).z * 0.32f)), arc_v3_make(0.15f, 0.12f, 0.15f), 0xE0FFE0);
    for (int i = 0; i < NDROIDS; i++) {
        const Droid *d = &G.d[i]; if (!d->alive) continue;
        arc_draw_mesh_styled(m_body, arc_m4_trs_y(arc_v3_add(d->pos, arc_v3_make(0, 0.4f, 0)), d->yaw, arc_v3_make(0.6f, 0.8f, 0.6f)), &(arc_mesh_style){ .tint = STATE_RGB[d->brain.state % 5], .whiten = d->flash > 0 ? 0.8f : 0 });
        arc_draw_cube(arc_v3_add(d->pos, arc_v3_make(fwd(d->yaw).x * 0.32f, 0.6f, fwd(d->yaw).z * 0.32f)), arc_v3_make(0.15f, 0.12f, 0.15f), 0xFFE0A0);
        arc_draw_billboard_text(arc_v3_add(d->pos, arc_v3_make(0, 1.2f, 0)), 0.25f, 0xFFFFFF, 0x80000000u, 0, arc_brain_state_name(&d->brain, &DEF));
    }
    if (G.show_senses) arc_senses_draw(&G.senses);
    arc_draw_decals(); arc_draw_particles(); arc_shots_draw();
    static const char *VIEWS[] = { "TOP-DOWN", "FOLLOW", "ORBIT", "FIRST PERSON" };
    arc_draw_textf(12, 12, 2, G.hp > 3 ? 0xFFFFFF : 0xFF6060, "HP %d  DOWN %d/%d  VIEW %s%s", G.hp, G.kills, NDROIDS, VIEWS[G.view], arc_cam_rig_blending(&G.cam) ? " ..." : "");
    arc_draw_text(12, (float)arc_height() - 26, 1.5f, 0x9AA4B8, "W/S move  A/D turn  SPACE fire  1-4 view  V senses");
    if (G.hp <= 0) arc_draw_text((float)arc_width() * 0.5f - 90, (float)arc_height() * 0.5f, 4, 0xFF4040, "DESTROYED");
}
static void dump(arc_json *j) {
    arc_json_num_k(j, "x", G.pos.x); arc_json_num_k(j, "z", G.pos.z); arc_json_num_k(j, "yaw", G.yaw); arc_json_int_k(j, "hp", G.hp);
    arc_json_int_k(j, "view", G.view); arc_json_bool_k(j, "blending", arc_cam_rig_blending(&G.cam)); arc_json_num_k(j, "cam_dist", G.cam.dist_now);
    arc_json_int_k(j, "kills", G.kills); arc_json_int_k(j, "in_flight", arc_shots_in_flight());
    arc_json_arr_k(j, "droids");
    for (int i = 0; i < NDROIDS; i++) {
        const Droid *d = &G.d[i]; arc_json_obj(j);
        arc_json_bool_k(j, "alive", d->alive); arc_json_str_k(j, "state", d->alive ? arc_brain_state_name(&d->brain, &DEF) : "DEAD");
        arc_json_int_k(j, "hp", d->hp); arc_json_num_k(j, "x", d->pos.x); arc_json_num_k(j, "z", d->pos.z);
        const arc_senses_memory *m = d->alive ? knows(d) : NULL; arc_json_bool_k(j, "knows", m != NULL); arc_json_bool_k(j, "sees", m && m->seen_now);
        arc_json_end(j);
    }
    arc_json_end(j);
}
static bool command(const char *cmd, int argc, const char **argv) {
    if (!strcmp(cmd, "fire")) { G.cool = 0; fire(); return true; }
    if (!strcmp(cmd, "face") && argc >= 1) { G.yaw = (float)atof(argv[0]) * 3.14159265f / 180.0f; return true; }
    if (!strcmp(cmd, "goto") && argc >= 2) { G.pos.x = (float)atof(argv[0]); G.pos.z = (float)atof(argv[1]); arc_cam_rig_snap(&G.cam); return true; }
    if (!strcmp(cmd, "view") && argc >= 1) { set_view(atoi(argv[0])); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t n) { if (n != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "ARC action: brains, senses, shots, camera rigs", .width = 1280, .height = 720, .clear_color = 0x0A0C12,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command, .save_state = save_state, .load_state = load_state };
}
