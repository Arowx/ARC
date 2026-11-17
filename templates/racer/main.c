/* {title} - a lap racing starter (ARC template "racer").
 *
 * W/UP accelerates, S/DOWN brakes and reverses, A/D or LEFT/RIGHT steer. Race one computer car over three laps of
 * a closed track. Leaving the road slows you down. Laps count only when you pass every checkpoint in order. After
 * the countdown the clock runs; the HUD shows lap, position, speed and the best lap. R restarts.
 *
 * Where things are:
 *   TRACK            the centre line as a loop of points (also the checkpoints); HALF_WIDTH is half the road.
 *   Car              position, heading, speed, next checkpoint, lap: one struct for the player and the AI.
 *   drive()          arcade car physics shared by both (throttle, brake, steering that grows with speed, grip,
 *                    off-road drag); ai_input() steers the computer car toward its next checkpoint.
 *   Game G           everything, pointer-free: save_state/load_state copy it (rewind, seek and the timeline work).
 *   dump()           phase, lap, checkpoint, speed, position in the race, on_track, times, the AI's progress.
 *   command()        agent commands: go (skip the countdown), tp_cp N, lap N, ai off|on, restart.
 *   arc_var          tunables: max_speed, accel, steer, grip, ai_skill (`set max_speed 30`).
 * Grow it: more AI cars (an array), boost pads, a track editor (points from a file), a minimap (a render target),
 * ghost replays of the best lap (the recorded input), split-screen (two cameras into two targets).
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
enum { PHASE_COUNTDOWN, PHASE_RACE, PHASE_FINISHED };

typedef struct { arc_v2 p; float heading, speed; int next_cp, lap; bool finished; int finish_tick; } Car;
typedef struct {
    int phase, countdown, race_ticks, lap_start, best_lap;   /* ticks */
    Car car, ai;
    bool ai_on;
    int place;                                               /* 1 or 2 when finished */
} Game;
static Game G;

static float *MAX_SPEED, *ACCEL, *STEER, *GRIP, *AI_SKILL, v_max, v_acc, v_steer, v_grip, v_ai;
static arc_mesh m_grass, m_road, m_line, m_car, m_ai, m_cabin, m_cone;
static arc_sound s_beep, s_go, s_lap;

static float seg_dist(arc_v2 p, arc_v2 a, arc_v2 b) {
    arc_v2 ab = arc_v2_sub(b, a), ap = arc_v2_sub(p, a);
    float t = arc_clampf((ap.x * ab.x + ap.y * ab.y) / (ab.x * ab.x + ab.y * ab.y), 0, 1);
    return arc_v2_len(arc_v2_sub(p, arc_v2_add(a, arc_v2_scale(ab, t))));
}
static float track_dist(arc_v2 p) { float d = 1e9f; for (int i = 0; i < NTRACK; i++) d = fminf(d, seg_dist(p, TRACK[i], TRACK[(i + 1) % NTRACK])); return d; }
static bool on_track(arc_v2 p) { return track_dist(p) <= HALF_WIDTH; }
static float angle_to(arc_v2 from, arc_v2 to) { return atan2f(to.y - from.y, to.x - from.x); }
static float wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

static void place_car(Car *c, int cp, float side) {   /* at checkpoint cp, facing the next one, side = metres left/right */
    arc_v2 a = TRACK[cp], b = TRACK[(cp + 1) % NTRACK]; float h = angle_to(a, b);
    c->p = arc_v2_add(a, arc_v2_make(-sinf(h) * side, cosf(h) * side)); c->heading = h; c->speed = 0;
    c->next_cp = (cp + 1) % NTRACK;
}
static void reset(void) {
    memset(&G, 0, sizeof G); G.ai_on = true; G.countdown = 180;
    place_car(&G.car, 0, -1.6f); place_car(&G.ai, 0, 1.6f);
}

static void drive(Car *c, float throttle, float steer) {   /* throttle -1..1, steer -1..1 */
    float dt = arc_dt(), maxs = *MAX_SPEED * (on_track(c->p) ? 1.0f : 0.45f);
    if (throttle > 0) c->speed += throttle * *ACCEL * dt;
    else if (throttle < 0) c->speed += throttle * (c->speed > 0 ? 2.5f : 1.0f) * *ACCEL * dt;   /* brake, then reverse */
    else { c->speed -= arc_clampf(c->speed, -1, 1) * 4 * dt; if (fabsf(c->speed) < 0.05f) c->speed = 0; }   /* rolling */
    if (!on_track(c->p)) c->speed -= c->speed * 1.5f * dt;                                       /* grass drag */
    c->speed = arc_clampf(c->speed, -maxs * 0.35f, maxs);
    float turn = steer * *STEER * arc_clampf(fabsf(c->speed) / 8.0f, 0, 1) * (c->speed < 0 ? -1.0f : 1.0f);
    c->heading = wrap(c->heading + turn * dt * (1.0f - 0.35f * fabsf(c->speed) / (*MAX_SPEED * *GRIP)));
    c->p = arc_v2_add(c->p, arc_v2_make(cosf(c->heading) * c->speed * dt, sinf(c->heading) * c->speed * dt));
    /* checkpoints in order; back at checkpoint 0 = a lap */
    if (arc_v2_len(arc_v2_sub(c->p, TRACK[c->next_cp])) < HALF_WIDTH + 2) {
        if (c->next_cp == 0 && !c->finished) {
            c->lap++;
            if (c == &G.car) {
                int t = G.race_ticks - G.lap_start; if (!G.best_lap || t < G.best_lap) G.best_lap = t; G.lap_start = G.race_ticks;
                arc_play(s_lap, 0.5f, 0); arc_event("lap", "\"lap\":%d,\"ticks\":%d", c->lap, t);
            }
            if (c->lap >= LAPS) { c->finished = true; c->finish_tick = G.race_ticks; }
        }
        c->next_cp = (c->next_cp + 1) % NTRACK;
    }
}
static void ai_input(const Car *c, float *throttle, float *steer) {
    arc_v2 target = TRACK[c->next_cp];
    float err = wrap(angle_to(c->p, target) - c->heading);
    *steer = arc_clampf(err * 2.0f, -1, 1);
    *throttle = fabsf(err) > 0.6f && c->speed > *MAX_SPEED * 0.5f ? -0.3f : *AI_SKILL;
}
static int progress(const Car *c) { return c->lap * NTRACK + (c->next_cp + NTRACK - 1) % NTRACK; }   /* checkpoints passed */
static int race_place(void) {
    if (G.car.finished && G.ai.finished) return G.car.finish_tick <= G.ai.finish_tick ? 1 : 2;
    if (G.car.finished != G.ai.finished) return G.car.finished ? 1 : 2;
    int a = progress(&G.car), b = progress(&G.ai);
    if (a != b) return a > b ? 1 : 2;
    return arc_v2_len(arc_v2_sub(G.car.p, TRACK[G.car.next_cp])) <= arc_v2_len(arc_v2_sub(G.ai.p, TRACK[G.ai.next_cp])) ? 1 : 2;
}

static void init(void) {
    MAX_SPEED = arc_var("max_speed", &v_max, 24); ACCEL = arc_var("accel", &v_acc, 14); STEER = arc_var("steer", &v_steer, 2.4f);
    GRIP = arc_var("grip", &v_grip, 1.2f); AI_SKILL = arc_var("ai_skill", &v_ai, 0.86f);
    m_grass = arc_mesh_plane(0x4C8A3C, 40, 0x468236); arc_mesh_set_name(m_grass, "grass");
    m_road = arc_mesh_cube(0x50545C); arc_mesh_set_name(m_road, "road");
    m_line = arc_mesh_cube(0xF0F0F0); arc_mesh_set_name(m_line, "start_line");
    m_car = arc_mesh_cube(0x3070F0); arc_mesh_set_name(m_car, "car");
    m_ai = arc_mesh_cube(0xF04030); arc_mesh_set_name(m_ai, "ai_car");
    m_cabin = arc_mesh_cube(0xC8D0DC); arc_mesh_set_name(m_cabin, "cabin");
    m_cone = arc_mesh_cone(0xFF8020, 12); arc_mesh_set_name(m_cone, "cone");
    s_beep = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_SQUARE, .freq = 440, .freq_end = 440, .sustain = 0.12f, .decay = 0.05f });
    s_go = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_SQUARE, .freq = 880, .freq_end = 880, .sustain = 0.3f, .decay = 0.1f });
    s_lap = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_TRIANGLE, .freq = 660, .freq_end = 990, .sustain = 0.1f, .decay = 0.2f });
    reset();
}
static void tick(void) {
    if (arc_key_pressed(ARC_KEY_R)) reset();
    if (G.phase == PHASE_COUNTDOWN) {
        if (G.countdown % 60 == 0 && G.countdown > 0) arc_play(s_beep, 0.4f, 0);
        if (--G.countdown <= 0) { G.phase = PHASE_RACE; arc_play(s_go, 0.5f, 0); }
        return;
    }
    if (G.phase == PHASE_RACE) G.race_ticks++;
    float th = 0, st = 0;
    if (!G.car.finished) {
        th = (arc_key_down(ARC_KEY_W) || arc_key_down(ARC_KEY_UP) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_S) || arc_key_down(ARC_KEY_DOWN) ? 1.0f : 0.0f);
        st = (arc_key_down(ARC_KEY_D) || arc_key_down(ARC_KEY_RIGHT) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_A) || arc_key_down(ARC_KEY_LEFT) ? 1.0f : 0.0f);
    }
    drive(&G.car, th, st);
    if (G.ai_on && !G.ai.finished) { float at, as; ai_input(&G.ai, &at, &as); drive(&G.ai, at, as); }
    else drive(&G.ai, 0, 0);
    if (G.phase == PHASE_RACE && G.car.finished) {
        G.phase = PHASE_FINISHED; G.place = race_place();
        arc_event("finished", "\"place\":%d,\"ticks\":%d,\"best_lap\":%d", G.place, G.race_ticks, G.best_lap);
    }
}
static void draw_car(arc_mesh m, const Car *c) {
    arc_draw_mesh(m, arc_m4_trs_y(arc_v3_make(c->p.x, 0.45f, c->p.y), -c->heading, arc_v3_make(2.0f, 0.6f, 1.1f)), 0xFFFFFF);
    arc_v2 f = arc_v2_add(c->p, arc_v2_make(cosf(c->heading) * -0.2f, sinf(c->heading) * -0.2f));
    arc_draw_mesh(m_cabin, arc_m4_trs_y(arc_v3_make(f.x, 0.9f, f.y), -c->heading, arc_v3_make(1.0f, 0.45f, 0.95f)), 0xFFFFFF);
}
static void draw(float alpha) {
    (void)alpha;
    arc_v2 f = arc_v2_make(cosf(G.car.heading), sinf(G.car.heading));
    arc_camera cam = { { G.car.p.x - f.x * 8, 4.2f, G.car.p.y - f.y * 8 }, { G.car.p.x + f.x * 4, 0.5f, G.car.p.y + f.y * 4 }, { 0, 1, 0 }, 0.95f, 0, 0.1f, 300 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.3f), 0xFFF4E0, 0x4A5868);
    arc_set_sky(0x3A70C0, 0xB8D0E8);
    arc_set_fog(0xB8D0E8, 60, 160);
    arc_draw_mesh(m_grass, arc_m4_trs_y(arc_v3_make(0, -0.01f, 0), 0, arc_v3_make(120, 1, 100)), 0xFFFFFF);
    for (int i = 0; i < NTRACK; i++) {
        arc_v2 a = TRACK[i], b = TRACK[(i + 1) % NTRACK], m = arc_v2_scale(arc_v2_add(a, b), 0.5f);
        float len = arc_v2_len(arc_v2_sub(b, a)), h = angle_to(a, b);
        arc_draw_mesh(m_road, arc_m4_trs_y(arc_v3_make(m.x, 0.02f, m.y), -h, arc_v3_make(len, 0.04f, HALF_WIDTH * 2)), 0xFFFFFF);
        arc_draw_mesh(m_road, arc_m4_trs_y(arc_v3_make(a.x, 0.021f, a.y), 0, arc_v3_make(HALF_WIDTH * 2, 0.04f, HALF_WIDTH * 2)), 0xFFFFFF);   /* round the joint */
        arc_v2 out = arc_v2_make(-sinf(h) * (HALF_WIDTH + 0.8f), cosf(h) * (HALF_WIDTH + 0.8f));
        arc_draw_mesh(m_cone, arc_m4_trs_y(arc_v3_make(a.x + out.x, 0.35f, a.y + out.y), 0, arc_v3_make(0.5f, 0.7f, 0.5f)), 0xFFFFFF);
        arc_draw_mesh(m_cone, arc_m4_trs_y(arc_v3_make(a.x - out.x, 0.35f, a.y - out.y), 0, arc_v3_make(0.5f, 0.7f, 0.5f)), 0xFFFFFF);
    }
    float h0 = angle_to(TRACK[0], TRACK[1]);
    arc_draw_mesh(m_line, arc_m4_trs_y(arc_v3_make(TRACK[0].x + 2, 0.05f, TRACK[0].y), -h0, arc_v3_make(0.6f, 0.04f, HALF_WIDTH * 2)), 0xFFFFFF);
    draw_car(m_car, &G.car); draw_car(m_ai, &G.ai);
    int lap = G.car.lap + 1 > LAPS ? LAPS : G.car.lap + 1;
    arc_draw_textf(10, 10, 3, 0xFFFFFF, "LAP %d/%d   POS %d/2", lap, LAPS, race_place());
    arc_draw_textf(10, 40, 2, 0xE0E8F0, "TIME %.2f   BEST %.2f", (float)G.race_ticks * arc_dt(), (float)G.best_lap * arc_dt());
    arc_draw_text_ex((float)arc_width() - 10.0f, (float)arc_height() - 40.0f, 3, 0xFFFFFF, ARC_TEXT_RIGHT | ARC_TEXT_SHADOW, arc_fmt("%d km/h", (int)(fabsf(G.car.speed) * 3.6f)));
    if (G.phase == PHASE_COUNTDOWN) arc_draw_text_ex((float)arc_width() / 2.0f, (float)arc_height() / 3.0f, 6, 0xFFE040, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE, arc_fmt("%d", G.countdown / 60 + 1));
    if (G.phase == PHASE_FINISHED) arc_draw_text_ex((float)arc_width() / 2.0f, (float)arc_height() / 3.0f, 4, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE, arc_fmt("FINISHED - %s - R to race again", G.place == 1 ? "1ST" : "2ND"));
}
static void dump(arc_json *j) {
    static const char *ph[] = { "countdown", "race", "finished" };
    arc_json_str_k(j, "phase", ph[G.phase]); arc_json_num_k(j, "lap", G.car.lap); arc_json_num_k(j, "next_cp", G.car.next_cp);
    arc_json_num_k(j, "speed", G.car.speed); arc_json_num_k(j, "x", G.car.p.x); arc_json_num_k(j, "z", G.car.p.y);
    arc_json_num_k(j, "heading_deg", G.car.heading * 57.29578f); arc_json_bool_k(j, "on_track", on_track(G.car.p));
    arc_json_num_k(j, "place", G.phase == PHASE_FINISHED ? G.place : race_place());
    arc_json_num_k(j, "race_ticks", G.race_ticks); arc_json_num_k(j, "best_lap", G.best_lap);
    arc_json_num_k(j, "ai_lap", G.ai.lap); arc_json_num_k(j, "ai_next_cp", G.ai.next_cp); arc_json_num_k(j, "checkpoints", NTRACK);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "go")) { if (G.phase == PHASE_COUNTDOWN) { G.countdown = 0; G.phase = PHASE_RACE; } return true; }
    if (!strcmp(name, "tp_cp") && argc >= 1) { place_car(&G.car, ((atoi(argv[0]) % NTRACK) + NTRACK) % NTRACK, 0); return true; }
    if (!strcmp(name, "lap") && argc >= 1) { G.car.lap = atoi(argv[0]); return true; }
    if (!strcmp(name, "ai") && argc >= 1) { G.ai_on = strcmp(argv[0], "off") != 0; return true; }
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
