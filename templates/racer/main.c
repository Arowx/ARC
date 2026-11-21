/* {title} - a lap racing game (ARC template "racer").
 *
 * W/UP accelerates, S/DOWN brakes and reverses, A/D or LEFT/RIGHT steer. Race three computer karts over three laps
 * of a closed track. Leaving the road slows you down; the glowing chevron pads boost you. Laps count only when you
 * pass every checkpoint in order. After the countdown the clock runs; the HUD shows lap, position, speed and the best
 * lap. Karts bump each other. R restarts.
 *
 * Where things are:
 *   TRACK            the centre line as a loop of points (also the checkpoints); HALF_WIDTH is half the road. The
 *                    road you see is built from the same shape (build_road): what looks like road is road.
 *   Car              position, heading, speed, boost, next checkpoint, lap: one struct for the player and the AIs.
 *   drive()          arcade kart physics shared by all (throttle, brake, steering that grows with speed, grip,
 *                    off-road drag, boost pads); ai_input() steers a computer kart toward its next checkpoint.
 *   Game G           everything, pointer-free: save_state/load_state copy it (rewind, seek and the timeline work).
 *   juice            engine pitch follows speed, skids squeal and smoke, grass throws dust, bumps spark and shake,
 *                    the camera lags and widens with speed, countdown numbers pop, lap and boost popups
 *                    (arc_popup_text), confetti at the finish.
 *   assets/          models, textures, sounds and music made with Ludo; assets/manifest.json says how.
 *   dump()           phase, lap, checkpoint, speed, boost, position in the race, on_track, times, the AIs' progress.
 *   command()        agent commands: go (skip the countdown), tp_cp N, lap N, ai off|on, boost, restart.
 *   arc_var          tunables: max_speed, accel, steer, grip, ai_skill (`set max_speed 30`).
 * Grow it: more tracks (an array of point loops), items to throw, a minimap (a render target), ghost replays of the
 * best lap (the recorded input), split-screen (arc_viewport_begin).
 */
#include "arc.h"
#include <string.h>
#include <stdlib.h>

static const arc_v2 TRACK[] = {
    { 0, -20 }, { 20, -20 }, { 32, -14 }, { 36, 0 }, { 30, 12 }, { 18, 16 }, { 8, 10 }, { 0, 16 },
    { -12, 20 }, { -28, 16 }, { -34, 2 }, { -30, -12 }, { -18, -20 },
};
#define NTRACK ((int)(sizeof TRACK / sizeof TRACK[0]))
#define HALF_WIDTH 4.0f
#define LAPS 3
#define NCARS 4                  /* 0 is the player */
#define NPADS 3
enum { PHASE_COUNTDOWN, PHASE_RACE, PHASE_FINISHED };
static const int PAD_SEG[NPADS] = { 2, 6, 10 };   /* a boost pad in the middle of these track segments */
static const uint32_t CAR_RGB[NCARS] = { 0x3080FF, 0xF03030, 0x30C050, 0xF0C020 };

typedef struct { arc_v2 p; float heading, speed, skill; int next_cp, lap, boost; bool finished; int finish_tick; } Car;
typedef struct {
    int phase, countdown, race_ticks, lap_start, best_lap;   /* ticks */
    Car car[NCARS];
    bool ai_on;
    int place;                                               /* 1..NCARS when finished */
    float cam_fov, skid;                                     /* looks */
} Game;
static Game G;

static float *MAX_SPEED, *ACCEL, *STEER, *GRIP, *AI_SKILL, v_max, v_acc, v_steer, v_grip, v_ai;
static arc_mesh m_grass, m_road, m_kerb, m_line, m_car, m_ai, m_cone, m_tyres, m_arch;
static arc_sound s_beep, s_go, s_lap, s_boost, s_bump, s_engine, s_skid;
static arc_music music;
static arc_voice v_engine, v_skid;
static float shake_t;

static const arc_fx FX_SMOKE = { .life = 0.8f, .speed = 0.8f, .spread = 1.2f, .size = 0.5f, .size_end = 1.4f, .color = 0xD8D8D8,
                                 .alpha = 0.45f, .gravity = { 0, 0.8f, 0 }, .drag = 2, .soft = 0.3f, .burst = 2 };
static const arc_fx FX_GRASS = { .life = 0.5f, .speed = 3, .speed_var = 1.5f, .spread = 0.6f, .size = 0.12f, .color = 0x5A8A30,
                                 .gravity = { 0, -12, 0 }, .burst = 3 };
static const arc_fx FX_SPARK = { .life = 0.3f, .speed = 7, .speed_var = 3, .spread = 1.4f, .size = 0.08f, .color = 0xFFF0A0,
                                 .color_end = 0xFF6010, .glow = 2, .additive = true, .stretch = 0.05f, .gravity = { 0, -10, 0 }, .burst = 16 };
static const arc_fx FX_FLAME = { .life = 0.25f, .speed = 4, .spread = 0.3f, .size = 0.35f, .size_end = 0.05f, .color = 0x80D0FF,
                                 .color_end = 0x2040FF, .glow = 2.5f, .additive = true, .burst = 2 };
static const arc_fx FX_CONFETTI = { .life = 2.5f, .life_var = 0.6f, .speed = 9, .speed_var = 3, .spread = 0.6f, .size = 0.2f,
                                    .color = 0xFFE040, .color_end = 0xFF4080, .gravity = { 0, -6, 0 }, .drag = 0.8f, .spin = 6,
                                    .spin_var = 6, .burst = 90 };

static float seg_dist(arc_v2 p, arc_v2 a, arc_v2 b) {
    arc_v2 ab = arc_v2_sub(b, a), ap = arc_v2_sub(p, a);
    float t = arc_clampf((ap.x * ab.x + ap.y * ab.y) / (ab.x * ab.x + ab.y * ab.y), 0, 1);
    return arc_v2_len(arc_v2_sub(p, arc_v2_add(a, arc_v2_scale(ab, t))));
}
static float track_dist(arc_v2 p) { float d = 1e9f; for (int i = 0; i < NTRACK; i++) d = fminf(d, seg_dist(p, TRACK[i], TRACK[(i + 1) % NTRACK])); return d; }
static bool on_track(arc_v2 p) { return track_dist(p) <= HALF_WIDTH; }
static float angle_to(arc_v2 from, arc_v2 to) { return atan2f(to.y - from.y, to.x - from.x); }
static float wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static arc_v2 pad_pos(int i) { return arc_v2_scale(arc_v2_add(TRACK[PAD_SEG[i]], TRACK[(PAD_SEG[i] + 1) % NTRACK]), 0.5f); }
static arc_v3 at(arc_v2 p, float y) { return arc_v3_make(p.x, y, p.y); }

static void place_car(Car *c, int cp, float side, float back) {   /* at checkpoint cp facing the next one; side: metres left/right */
    arc_v2 a = TRACK[cp], b = TRACK[(cp + 1) % NTRACK]; float h = angle_to(a, b);
    c->p = arc_v2_add(a, arc_v2_make(-sinf(h) * side - cosf(h) * back, cosf(h) * side - sinf(h) * back)); c->heading = h; c->speed = 0;
    c->next_cp = (cp + 1) % NTRACK; c->boost = 0;
}
static void reset(void) {
    memset(&G, 0, sizeof G); G.ai_on = true; G.countdown = 180; G.cam_fov = 0.95f;
    place_car(&G.car[0], 0, -1.6f, 0); place_car(&G.car[1], 0, 1.6f, 0);
    place_car(&G.car[2], 0, -1.6f, 5); place_car(&G.car[3], 0, 1.6f, 5);
    for (int i = 1; i < NCARS; i++) G.car[i].skill = *AI_SKILL * (1.0f - 0.04f * (float)(i - 1));   /* a spread of pace */
}

static void drive(Car *c, float throttle, float steer) {   /* throttle -1..1, steer -1..1 */
    float dt = arc_dt(), maxs = *MAX_SPEED * (on_track(c->p) ? 1.0f : 0.45f) * (c->boost > 0 ? 1.4f : 1.0f);
    if (c->boost > 0) { c->boost--; c->speed += *ACCEL * 1.5f * dt; }
    if (throttle > 0) c->speed += throttle * *ACCEL * dt;
    else if (throttle < 0) c->speed += throttle * (c->speed > 0 ? 2.5f : 1.0f) * *ACCEL * dt;   /* brake, then reverse */
    else { c->speed -= arc_clampf(c->speed, -1, 1) * 4 * dt; if (fabsf(c->speed) < 0.05f) c->speed = 0; }   /* rolling */
    if (!on_track(c->p)) c->speed -= c->speed * 1.5f * dt;                                       /* grass drag */
    c->speed = arc_clampf(c->speed, -maxs * 0.35f, maxs);
    float turn = steer * *STEER * arc_clampf(fabsf(c->speed) / 8.0f, 0, 1) * (c->speed < 0 ? -1.0f : 1.0f);
    c->heading = wrap(c->heading + turn * dt * (1.0f - 0.35f * fabsf(c->speed) / (*MAX_SPEED * *GRIP)));
    c->p = arc_v2_add(c->p, arc_v2_make(cosf(c->heading) * c->speed * dt, sinf(c->heading) * c->speed * dt));
    for (int i = 0; i < NPADS; i++) if (c->boost < 60 && arc_v2_len(arc_v2_sub(c->p, pad_pos(i))) < 2.0f) {   /* boost pads */
        c->boost = 90;
        if (c == &G.car[0]) { arc_play(s_boost, 0.6f, 0); arc_popup_text(at(c->p, 2.2f), 0x80D0FF, 3, "BOOST!"); }
    }
    /* checkpoints in order; back at checkpoint 0 = a lap */
    if (arc_v2_len(arc_v2_sub(c->p, TRACK[c->next_cp])) < HALF_WIDTH + 2) {
        if (c->next_cp == 0 && !c->finished) {
            c->lap++;
            if (c == &G.car[0]) {
                int t = G.race_ticks - G.lap_start; bool best = !G.best_lap || t < G.best_lap;
                if (best) G.best_lap = t;
                G.lap_start = G.race_ticks;
                arc_play(s_lap, 0.6f, 0); arc_event("lap", "\"lap\":%d,\"ticks\":%d", c->lap, t);
                if (c->lap < LAPS) arc_popup_text(at(c->p, 2.5f), best ? 0x60FF90 : 0xFFFFFF, 3.5f,
                                                  c->lap == LAPS - 1 ? "FINAL LAP!" : best && c->lap > 1 ? "BEST LAP!" : arc_fmt("LAP %d", c->lap + 1));
            }
            if (c->lap >= LAPS) { c->finished = true; c->finish_tick = G.race_ticks; }
        }
        c->next_cp = (c->next_cp + 1) % NTRACK;
    }
}
static void bumps(void) {   /* karts push apart; a hard knock sparks */
    for (int i = 0; i < NCARS; i++) for (int j = i + 1; j < NCARS; j++) {
        Car *a = &G.car[i], *b = &G.car[j]; arc_v2 d = arc_v2_sub(b->p, a->p); float l = arc_v2_len(d);
        if (l < 1.6f && l > 1e-4f) {
            arc_v2 n = arc_v2_scale(d, 1.0f / l), push = arc_v2_scale(n, (1.6f - l) * 0.5f);
            a->p = arc_v2_sub(a->p, push); b->p = arc_v2_add(b->p, push);
            float rel = fabsf(a->speed - b->speed);
            if (rel > 3) {
                float m = (a->speed + b->speed) * 0.5f; a->speed = arc_lerpf(a->speed, m, 0.5f); b->speed = arc_lerpf(b->speed, m, 0.5f);
                if (i == 0) { arc_play_var(s_bump, 0.6f, 0, 0.1f); shake_t = 0.25f; arc_fx_burst(&FX_SPARK, at(arc_v2_add(a->p, arc_v2_scale(n, 0.8f)), 0.5f), arc_v3_make(0, 1, 0), 0); }
            }
        }
    }
}
static void ai_input(const Car *c, float *throttle, float *steer) {
    arc_v2 target = TRACK[c->next_cp];
    float err = wrap(angle_to(c->p, target) - c->heading);
    *steer = arc_clampf(err * 2.0f, -1, 1);
    *throttle = fabsf(err) > 0.6f && c->speed > *MAX_SPEED * 0.5f ? -0.3f : c->skill;
}
static int progress(const Car *c) { return c->lap * NTRACK + (c->next_cp + NTRACK - 1) % NTRACK; }   /* checkpoints passed */
static bool ahead(const Car *a, const Car *b) {
    if (a->finished && b->finished) return a->finish_tick < b->finish_tick;
    if (a->finished != b->finished) return a->finished;
    int pa = progress(a), pb = progress(b);
    if (pa != pb) return pa > pb;
    return arc_v2_len(arc_v2_sub(a->p, TRACK[a->next_cp])) < arc_v2_len(arc_v2_sub(b->p, TRACK[b->next_cp]));
}
static int race_place(void) { int p = 1; for (int i = 1; i < NCARS; i++) p += ahead(&G.car[i], &G.car[0]); return p; }

/* the road and its kerbs, built from TRACK: a strip along each segment and a disc at each corner (the same shape
   on_track tests), textured by world position so the pieces meet without seams */
static void quad(arc_vertex *v, uint32_t *ix, int *nv, int *ni, arc_v2 a, arc_v2 b, arc_v2 c, arc_v2 d, float y, uint32_t col, float uvs) {
    arc_v2 q[4] = { a, b, c, d };
    for (int k = 0; k < 4; k++) v[*nv + k] = (arc_vertex){ q[k].x, y, q[k].y, 0, 0, 0, col, q[k].x / uvs, q[k].y / uvs };
    uint32_t o = (uint32_t)*nv; uint32_t t[6] = { o, o + 2, o + 1, o, o + 3, o + 2 };
    memcpy(ix + *ni, t, sizeof t); *nv += 4; *ni += 6;
}
static void build_road(void) {
    static arc_vertex v[8192]; static uint32_t ix[16384]; int nv = 0, ni = 0;
    for (int i = 0; i < NTRACK; i++) {
        arc_v2 a = TRACK[i], b = TRACK[(i + 1) % NTRACK]; float h = angle_to(a, b);
        arc_v2 n = arc_v2_make(-sinf(h) * HALF_WIDTH, cosf(h) * HALF_WIDTH);
        quad(v, ix, &nv, &ni, arc_v2_sub(a, n), arc_v2_sub(b, n), arc_v2_add(b, n), arc_v2_add(a, n), 0.02f, arc_vcol(0xFFFFFF), 8);
        for (int k = 0; k < 24; k++) {   /* the corner disc, as a fan of thin quads */
            float a0 = (float)k * 6.2831853f / 24, a1 = (float)(k + 1) * 6.2831853f / 24;
            arc_v2 p0 = arc_v2_add(a, arc_v2_make(cosf(a0) * HALF_WIDTH, sinf(a0) * HALF_WIDTH));
            arc_v2 p1 = arc_v2_add(a, arc_v2_make(cosf(a1) * HALF_WIDTH, sinf(a1) * HALF_WIDTH));
            quad(v, ix, &nv, &ni, a, p1, p0, a, 0.02f, arc_vcol(0xFFFFFF), 8);
        }
    }
    m_road = arc_mesh_create(v, nv, ix, ni); arc_mesh_set_name(m_road, "road");
    nv = ni = 0;   /* kerbs: red and white blocks along both edges of each straight */
    for (int i = 0; i < NTRACK; i++) {
        arc_v2 a = TRACK[i], b = TRACK[(i + 1) % NTRACK]; float h = angle_to(a, b), len = arc_v2_len(arc_v2_sub(b, a));
        arc_v2 f = arc_v2_make(cosf(h), sinf(h)), n = arc_v2_make(-sinf(h), cosf(h));
        int pieces = (int)(len / 1.5f);
        for (int k = 1; k < pieces - 1; k++) for (int s = -1; s <= 1; s += 2) {
            arc_v2 p0 = arc_v2_add(a, arc_v2_scale(f, (float)k * 1.5f)), p1 = arc_v2_add(a, arc_v2_scale(f, (float)(k + 1) * 1.5f));
            arc_v2 i0 = arc_v2_add(p0, arc_v2_scale(n, (float)s * (HALF_WIDTH - 0.5f))), o0 = arc_v2_add(p0, arc_v2_scale(n, (float)s * HALF_WIDTH));
            arc_v2 i1 = arc_v2_add(p1, arc_v2_scale(n, (float)s * (HALF_WIDTH - 0.5f))), o1 = arc_v2_add(p1, arc_v2_scale(n, (float)s * HALF_WIDTH));
            uint32_t col = arc_vcol(k & 1 ? 0xE02020 : 0xF0F0F0);
            if (s > 0) quad(v, ix, &nv, &ni, i0, i1, o1, o0, 0.035f, col, 1); else quad(v, ix, &nv, &ni, o0, o1, i1, i0, 0.035f, col, 1);
        }
    }
    m_kerb = arc_mesh_create(v, nv, ix, ni); arc_mesh_set_name(m_kerb, "kerb");
}

static void init(void) {
    MAX_SPEED = arc_var("max_speed", &v_max, 24); ACCEL = arc_var("accel", &v_acc, 14); STEER = arc_var("steer", &v_steer, 2.4f);
    GRIP = arc_var("grip", &v_grip, 1.2f); AI_SKILL = arc_var("ai_skill", &v_ai, 0.86f);
    m_grass = arc_mesh_plane(0xFFFFFF, 1, 0xFFFFFF); arc_mesh_set_name(m_grass, "grass");
    arc_mesh_set_texture(m_grass, arc_texture_load_ex("assets/textures/grass.jpg", ARC_TEX_MESH));
    build_road();
    arc_mesh_set_texture(m_road, arc_texture_load_ex("assets/textures/road.jpg", ARC_TEX_MESH));
    m_line = arc_mesh_cube(0xF0F0F0); arc_mesh_set_name(m_line, "start_line");
    m_car = arc_mesh_load("assets/meshes/kart.arcm"); arc_mesh_set_name(m_car, "car");
    m_ai = arc_mesh_load("assets/meshes/kart.arcm"); arc_mesh_set_name(m_ai, "ai_car");   /* the same file: a second name */
    m_cone = arc_mesh_load("assets/meshes/cone.arcm"); arc_mesh_set_name(m_cone, "cone");
    m_tyres = arc_mesh_load("assets/meshes/tyres.arcm"); arc_mesh_set_name(m_tyres, "tyres");
    m_arch = arc_mesh_load("assets/meshes/arch.arcm"); arc_mesh_set_name(m_arch, "arch");
    s_beep = arc_sound_load("assets/sfx/beep.mp3"); s_go = arc_sound_load("assets/sfx/go.mp3"); s_lap = arc_sound_load("assets/sfx/lap.mp3");
    s_boost = arc_sound_load("assets/sfx/boost.mp3"); s_bump = arc_sound_load("assets/sfx/bump.mp3");
    s_engine = arc_sound_load("assets/sfx/engine.qoa"); s_skid = arc_sound_load("assets/sfx/skid.qoa");
    v_engine = arc_play_ex(s_engine, 0.25f, 0, 0.6f, true); v_skid = arc_play_ex(s_skid, 0, 0, 1, true);
    music = arc_music_load("assets/music/music.mp3"); arc_music_set_volume(music, 0.35f); arc_music_play(music, true);
    arc_set_post(&(arc_post){ .bloom = 0.35f, .bloom_threshold = 1.4f, .tonemap = true, .vignette = 0.25f, .contrast = 0.08f, .fxaa = true });
    reset();
}
static void tick(void) {
    if (arc_key_pressed(ARC_KEY_R)) reset();
    Car *me = &G.car[0];
    /* sound and looks follow the player's kart */
    float sp = fabsf(me->speed) / *MAX_SPEED;
    arc_voice_set_pitch(v_engine, 0.6f + 1.1f * sp + (me->boost > 0 ? 0.2f : 0));
    G.cam_fov = arc_lerpf(G.cam_fov, 0.9f + 0.25f * sp + (me->boost > 0 ? 0.15f : 0), 0.1f);
    if (G.phase == PHASE_COUNTDOWN) {
        if (G.countdown % 60 == 0 && G.countdown > 0) arc_play(s_beep, 0.5f, 0);
        if (--G.countdown <= 0) { G.phase = PHASE_RACE; arc_play(s_go, 0.6f, 0); arc_popup_text(at(me->p, 3), 0x60FF60, 5, "GO!"); }
        return;
    }
    if (G.phase == PHASE_RACE) G.race_ticks++;
    float th = 0, st = 0;
    if (!me->finished) {
        th = (arc_key_down(ARC_KEY_W) || arc_key_down(ARC_KEY_UP) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_S) || arc_key_down(ARC_KEY_DOWN) ? 1.0f : 0.0f);
        st = (arc_key_down(ARC_KEY_D) || arc_key_down(ARC_KEY_RIGHT) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_A) || arc_key_down(ARC_KEY_LEFT) ? 1.0f : 0.0f);
    }
    drive(me, th, st);
    for (int i = 1; i < NCARS; i++) {
        if (G.ai_on && !G.car[i].finished) { float at_, as; ai_input(&G.car[i], &at_, &as); drive(&G.car[i], at_, as); }
        else drive(&G.car[i], 0, 0);
    }
    bumps();
    /* skids: hard turns at speed, and the grass */
    G.skid = arc_lerpf(G.skid, fabsf(st) * sp > 0.55f && on_track(me->p) ? 1.0f : 0.0f, 0.15f);
    arc_voice_set_volume(v_skid, 0.35f * G.skid);
    arc_v2 rear = arc_v2_sub(me->p, arc_v2_make(cosf(me->heading) * 0.9f, sinf(me->heading) * 0.9f));
    if (G.skid > 0.5f && G.race_ticks % 3 == 0) arc_fx_burst(&FX_SMOKE, at(rear, 0.2f), arc_v3_make(0, 1, 0), 0);
    if (!on_track(me->p) && sp > 0.15f && G.race_ticks % 2 == 0) arc_fx_burst(&FX_GRASS, at(rear, 0.2f), arc_v3_make(-cosf(me->heading), 1, -sinf(me->heading)), 0);
    for (int i = 0; i < NCARS; i++) if (G.car[i].boost > 0) {
        arc_v2 r = arc_v2_sub(G.car[i].p, arc_v2_make(cosf(G.car[i].heading) * 1.0f, sinf(G.car[i].heading) * 1.0f));
        arc_fx_burst(&FX_FLAME, at(r, 0.35f), arc_v3_make(-cosf(G.car[i].heading), 0.2f, -sinf(G.car[i].heading)), 0);
    }
    if (G.phase == PHASE_RACE && me->finished) {
        G.phase = PHASE_FINISHED; G.place = race_place();
        arc_fx_burst(&FX_CONFETTI, at(me->p, 1), arc_v3_make(0, 1, 0), 0);
        arc_event("finished", "\"place\":%d,\"ticks\":%d,\"best_lap\":%d", G.place, G.race_ticks, G.best_lap);
    }
}
static void draw_car(int i) {
    const Car *c = &G.car[i];
    float bob = 0.02f * sinf((float)arc_tick_count() * 0.9f + (float)i) * arc_clampf(fabsf(c->speed) / 10.0f, 0, 1);
    arc_draw_mesh_styled(i == 0 ? m_car : m_ai, arc_m4_trs_y(at(c->p, bob), arc_yaw_to(cosf(c->heading), sinf(c->heading)), arc_v3_make(1, 1, 1)),
                         &(arc_mesh_style){ .tint = CAR_RGB[i], .recolour = i == 1 ? 0 : 0.85f });
}
static void draw(float alpha) {
    (void)alpha;
    const Car *me = &G.car[0];
    arc_v2 f = arc_v2_make(cosf(me->heading), sinf(me->heading));
    float sx = 0, sy = 0;
    if (shake_t > 0) { shake_t -= 1.0f / 60.0f; sx = shake_t * 0.8f * sinf((float)arc_tick_count() * 2.3f); sy = shake_t * 0.6f * cosf((float)arc_tick_count() * 2.9f); }
    arc_camera cam = { { me->p.x - f.x * 7.5f + sx, 3.8f + sy, me->p.y - f.y * 7.5f }, { me->p.x + f.x * 4, 0.6f, me->p.y + f.y * 4 },
                       { 0, 1, 0 }, G.cam_fov, 0, 0.1f, 300 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.3f), 0xFFF4E0, 0x4A5868);
    arc_set_sky(0x3A70C0, 0xB8D0E8);
    arc_set_fog(0xB8D0E8, 70, 180);
    arc_set_shadows(at(me->p, 0), 14, 1024);
    arc_draw_mesh_styled(m_grass, arc_m4_trs_y(arc_v3_make(0, 0, 0), 0, arc_v3_make(160, 1, 140)), &(arc_mesh_style){ .uv_scale = 40 });
    arc_draw_mesh(m_road, arc_m4_identity(), 0xB0B0B0);
    arc_draw_mesh(m_kerb, arc_m4_identity(), 0xFFFFFF);
    for (int i = 0; i < NTRACK; i++) {   /* cones on both sides at every corner, tyre stacks outside every other one */
        arc_v2 a = TRACK[i], b = TRACK[(i + 1) % NTRACK]; float h = angle_to(a, b);
        arc_v2 out = arc_v2_make(-sinf(h) * (HALF_WIDTH + 1.0f), cosf(h) * (HALF_WIDTH + 1.0f));
        arc_draw_mesh(m_cone, arc_m4_trs_y(at(arc_v2_add(a, out), 0), 0, arc_v3_make(1, 1, 1)), 0xFFFFFF);
        arc_draw_mesh(m_cone, arc_m4_trs_y(at(arc_v2_sub(a, out), 0), 0, arc_v3_make(1, 1, 1)), 0xFFFFFF);
        if (i % 2) arc_draw_mesh(m_tyres, arc_m4_trs_y(at(arc_v2_add(a, arc_v2_scale(out, 1.8f)), 0), h, arc_v3_make(1, 1, 1)), 0xFFFFFF);
    }
    for (int i = 0; i < NPADS; i++) {   /* boost pads: three chevrons along the road, pulsing */
        arc_v2 p = pad_pos(i), a = TRACK[PAD_SEG[i]], b = TRACK[(PAD_SEG[i] + 1) % NTRACK]; float h = angle_to(a, b);
        for (int k = -1; k <= 1; k++)
            arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_CHEVRON, .x = p.x + cosf(h) * (float)k * 1.2f, .z = p.y + sinf(h) * (float)k * 1.2f,
                                           .size = 2.4f, .yaw = arc_atan2f(-sinf(h), cosf(h)), .fill_rgb = 0x40C0FF, .fill_alpha = 0.85f,
                                           .pulse = 3, .pulse_amount = 0.5f, .lift = 0.05f });
    }
    float h0 = angle_to(TRACK[0], TRACK[1]);
    arc_draw_mesh(m_line, arc_m4_trs_y(arc_v3_make(TRACK[0].x + 2, 0.05f, TRACK[0].y), -h0, arc_v3_make(0.6f, 0.04f, HALF_WIDTH * 2)), 0xFFFFFF);
    arc_draw_mesh(m_arch, arc_m4_trs_y(arc_v3_make(TRACK[0].x + 2, 0, TRACK[0].y), arc_yaw_to(cosf(h0), sinf(h0)), arc_v3_make(1, 1, 1)), 0xFFFFFF);
    for (int i = 0; i < NCARS; i++) draw_car(i);
    arc_draw_particles();
    float Wd = (float)arc_width(), Hd = (float)arc_height();
    int lap = me->lap + 1 > LAPS ? LAPS : me->lap + 1;
    static const char *ord[] = { "", "1ST", "2ND", "3RD", "4TH" };
    arc_draw_text_ex(12, 10, 3, 0xFFFFFF, ARC_TEXT_OUTLINE, arc_fmt("LAP %d/%d   POS %d/%d", lap, LAPS, race_place(), NCARS));
    arc_draw_text_ex(12, 40, 2, 0xE0E8F0, ARC_TEXT_OUTLINE, arc_fmt("TIME %.2f   BEST %.2f", (float)G.race_ticks * arc_dt(), (float)G.best_lap * arc_dt()));
    arc_draw_text_ex(Wd - 14, 10, 5, CAR_RGB[0], ARC_TEXT_RIGHT | ARC_TEXT_OUTLINE, ord[race_place()]);
    float kmh = fabsf(me->speed) * 3.6f;   /* a speed bar that glows on boost */
    arc_draw_rect(Wd - 214, Hd - 34, 200, 14, 0x202830);
    arc_draw_rect(Wd - 212, Hd - 32, 196 * arc_clampf(kmh / (*MAX_SPEED * 3.6f * 1.4f), 0, 1), 10, me->boost > 0 ? 0x60E0FF : 0xFFC040);
    arc_draw_text_ex(Wd - 14, Hd - 40, 3, 0xFFFFFF, ARC_TEXT_RIGHT | ARC_TEXT_BOTTOM | ARC_TEXT_OUTLINE, arc_fmt("%d km/h", (int)kmh));
    if (G.phase == PHASE_COUNTDOWN) {   /* 3, 2, 1: each number pops */
        float t = (float)(60 - G.countdown % 60) / 60.0f, sc = 3.0f + 5.0f * arc_ease_out_back(arc_clampf(t * 3, 0, 1));
        arc_draw_text_ex(Wd / 2.0f, Hd / 3.0f, sc, 0xFFE040, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE | ARC_TEXT_OUTLINE, arc_fmt("%d", G.countdown / 60 + 1));
    }
    if (G.phase == PHASE_FINISHED)
        arc_draw_text_ex(Wd / 2.0f, Hd / 3.0f, 4, G.place == 1 ? 0xFFE040 : 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE,
                         arc_fmt("FINISHED - %s - R to race again", ord[G.place]));
}
static void dump(arc_json *j) {
    static const char *ph[] = { "countdown", "race", "finished" };
    const Car *me = &G.car[0];
    arc_json_str_k(j, "phase", ph[G.phase]); arc_json_num_k(j, "lap", me->lap); arc_json_num_k(j, "next_cp", me->next_cp);
    arc_json_num_k(j, "speed", me->speed); arc_json_num_k(j, "boost", me->boost); arc_json_num_k(j, "x", me->p.x); arc_json_num_k(j, "z", me->p.y);
    arc_json_num_k(j, "heading_deg", me->heading * 57.29578f); arc_json_bool_k(j, "on_track", on_track(me->p));
    arc_json_num_k(j, "place", G.phase == PHASE_FINISHED ? G.place : race_place()); arc_json_num_k(j, "cars", NCARS);
    arc_json_num_k(j, "race_ticks", G.race_ticks); arc_json_num_k(j, "best_lap", G.best_lap);
    arc_json_num_k(j, "ai_lap", G.car[1].lap); arc_json_num_k(j, "ai_next_cp", G.car[1].next_cp); arc_json_num_k(j, "checkpoints", NTRACK);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "go")) { if (G.phase == PHASE_COUNTDOWN) { G.countdown = 0; G.phase = PHASE_RACE; } return true; }
    if (!strcmp(name, "tp_cp") && argc >= 1) { place_car(&G.car[0], ((atoi(argv[0]) % NTRACK) + NTRACK) % NTRACK, 0, 0); return true; }
    if (!strcmp(name, "lap") && argc >= 1) { G.car[0].lap = atoi(argv[0]); return true; }
    if (!strcmp(name, "ai") && argc >= 1) { G.ai_on = strcmp(argv[0], "off") != 0; return true; }
    if (!strcmp(name, "boost")) { G.car[0].boost = 90; return true; }
    if (!strcmp(name, "restart")) { reset(); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t size) { if (size != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "{title}", .width = 960, .height = 540, .clear_color = 0xB8D0E8,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
