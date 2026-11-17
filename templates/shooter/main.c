/* {title} - a top-down arena shooter starter (ARC template "shooter").
 *
 * WASD moves, the mouse aims, left button or SPACE fires. Red enemies come in waves from the edges of the arena and
 * chase you; one shot removes one. Touching one costs a life (then a moment of safety). Clear a wave and the next,
 * bigger one starts. Three lives; R restarts.
 *
 * Where things are:
 *   Game G           all game state in fixed pools (bullets, enemies): no allocation while playing, and
 *                    save_state/load_state copy it whole (rewind, seek and the timeline work).
 *   tick()           input, movement, firing, collisions, waves, game over. draw() only draws.
 *   dump()           phase, wave, score, lives, player position and aim, live enemy and bullet counts.
 *   command()        agent commands: aim DEG, fire, wave N, spawn N, spawn_at X Z, clear, lives N, god, restart.
 *   arc_var          tunables: speed, enemy_speed, fire_rate, bullet_speed (`set enemy_speed 3`).
 * Grow it: enemy kinds (a kind field: speed, hp, score), pick-ups, a boss wave, gamepad sticks (arc_pad_axis),
 * particles on hits (arc_fx_burst), a high-score save (arc_save).
 */
#include "arc.h"
#include <string.h>
#include <stdlib.h>

#define ARENA 9.5f          /* the arena spans -ARENA..ARENA in x and z */
#define MAX_BULLETS 64
#define MAX_ENEMIES 96
enum { PHASE_PLAY, PHASE_BETWEEN, PHASE_OVER };

typedef struct { bool on; arc_v2 p, v; int life; } Bullet;
typedef struct { bool on; arc_v2 p; float speed; int flash; } Enemy;
typedef struct {
    int phase, wave, score, lives, kills;
    arc_v2 pos;             /* player on the ground plane (x, z) */
    float aim;              /* radians, 0 = +x */
    int cooldown, safe, between;   /* ticks: until the next shot, of safety after a hit, before the next wave */
    bool god;
    Bullet b[MAX_BULLETS];
    Enemy e[MAX_ENEMIES];
} Game;
static Game G;

static float *SPEED, *ENEMY_SPEED, *FIRE_RATE, *BULLET_SPEED, v_speed, v_espeed, v_rate, v_bspeed;
static arc_mesh m_floor, m_wall, m_player, m_gun, m_enemy, m_bullet;
static arc_sound s_shot, s_kill, s_hurt;
static arc_camera cam;

static int count_enemies(void) { int n = 0; for (int i = 0; i < MAX_ENEMIES; i++) n += G.e[i].on; return n; }
static int count_bullets(void) { int n = 0; for (int i = 0; i < MAX_BULLETS; i++) n += G.b[i].on; return n; }

static void spawn_enemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) if (!G.e[i].on) {
        float t = arc_rand_rangef(-ARENA, ARENA); int side = arc_rand_range(0, 3);
        arc_v2 p = side == 0 ? arc_v2_make(t, -ARENA) : side == 1 ? arc_v2_make(t, ARENA) : side == 2 ? arc_v2_make(-ARENA, t) : arc_v2_make(ARENA, t);
        G.e[i] = (Enemy){ true, p, *ENEMY_SPEED * arc_rand_rangef(0.8f, 1.2f), 0 };
        return;
    }
}
static void start_wave(int n) {
    G.wave = n; G.phase = PHASE_PLAY;
    for (int i = 0; i < 3 + 2 * n; i++) spawn_enemy();
    arc_event("wave", "\"wave\":%d", n);
}
static void reset(void) {
    memset(&G, 0, sizeof G); G.lives = 3;
    start_wave(1);
}
static void fire(void) {
    if (G.cooldown > 0 || G.phase == PHASE_OVER) return;
    for (int i = 0; i < MAX_BULLETS; i++) if (!G.b[i].on) {
        arc_v2 d = arc_v2_make(cosf(G.aim), sinf(G.aim));
        G.b[i] = (Bullet){ true, arc_v2_add(G.pos, arc_v2_scale(d, 0.6f)), arc_v2_scale(d, *BULLET_SPEED), 90 };
        G.cooldown = (int)(60.0f / *FIRE_RATE); arc_play(s_shot, 0.25f, 0);
        return;
    }
}

static void init(void) {
    SPEED = arc_var("speed", &v_speed, 6); ENEMY_SPEED = arc_var("enemy_speed", &v_espeed, 2.2f);
    FIRE_RATE = arc_var("fire_rate", &v_rate, 8); BULLET_SPEED = arc_var("bullet_speed", &v_bspeed, 18);
    m_floor = arc_mesh_plane(0x3A4250, 20, 0x323946); arc_mesh_set_name(m_floor, "floor");
    m_wall = arc_mesh_cube(0x6A7080); arc_mesh_set_name(m_wall, "wall");
    m_player = arc_mesh_cube(0x40A0FF); arc_mesh_set_name(m_player, "player");
    m_gun = arc_mesh_cube(0xD0E0FF); arc_mesh_set_name(m_gun, "gun");
    m_enemy = arc_mesh_sphere(0xE04848, 16); arc_mesh_set_name(m_enemy, "enemy");
    m_bullet = arc_mesh_sphere(0xFFE060, 8); arc_mesh_set_name(m_bullet, "bullet");
    s_shot = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_SQUARE, .freq = 880, .freq_end = 440, .sustain = 0.02f, .decay = 0.06f, .duty = 0.25f });
    s_kill = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_NOISE, .freq = 1200, .freq_end = 100, .sustain = 0.04f, .decay = 0.25f });
    s_hurt = arc_sound_make(&(arc_sfx){ .wave = ARC_WAVE_SAW, .freq = 220, .freq_end = 80, .sustain = 0.1f, .decay = 0.3f });
    cam = (arc_camera){ { 0, 19, 10.5f }, { 0, 0, 0.8f }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 100 };
    reset();
}
static void tick(void) {
    if (arc_key_pressed(ARC_KEY_R)) reset();
    if (G.phase == PHASE_OVER) return;
    /* move */
    arc_v2 in = arc_v2_make((arc_key_down(ARC_KEY_D) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_A) ? 1.0f : 0.0f),
                            (arc_key_down(ARC_KEY_S) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_W) ? 1.0f : 0.0f));
    if (arc_v2_len(in) > 0) in = arc_v2_scale(arc_v2_norm(in), *SPEED * arc_dt());
    G.pos.x = arc_clampf(G.pos.x + in.x, -ARENA + 0.5f, ARENA - 0.5f);
    G.pos.y = arc_clampf(G.pos.y + in.y, -ARENA + 0.5f, ARENA - 0.5f);
    /* aim at the mouse on the ground (when it moved: agents can aim with the `aim` command) */
    arc_v2 md = arc_mouse_delta();
    if (md.x != 0 || md.y != 0) {
        arc_v3 o, d; arc_camera_ray(&cam, (float)arc_width(), (float)arc_height(), arc_mouse(), &o, &d);
        if (d.y < -1e-4f) { float t = -o.y / d.y; G.aim = atan2f(o.z + d.z * t - G.pos.y, o.x + d.x * t - G.pos.x); }
    }
    if (G.cooldown > 0) G.cooldown--;
    if (arc_key_down(ARC_MOUSE_LEFT) || arc_key_down(ARC_KEY_SPACE)) fire();
    /* bullets */
    for (int i = 0; i < MAX_BULLETS; i++) if (G.b[i].on) {
        Bullet *b = &G.b[i];
        b->p = arc_v2_add(b->p, arc_v2_scale(b->v, arc_dt()));
        if (--b->life <= 0 || fabsf(b->p.x) > ARENA || fabsf(b->p.y) > ARENA) { b->on = false; continue; }
        for (int k = 0; k < MAX_ENEMIES; k++) if (G.e[k].on && arc_v2_len(arc_v2_sub(G.e[k].p, b->p)) < 0.55f) {
            G.e[k].on = false; b->on = false; G.score += 10 * G.wave; G.kills++; arc_play(s_kill, 0.4f, 0); break;
        }
    }
    /* enemies chase; touching costs a life */
    if (G.safe > 0) G.safe--;
    for (int k = 0; k < MAX_ENEMIES; k++) if (G.e[k].on) {
        Enemy *e = &G.e[k]; arc_v2 to = arc_v2_sub(G.pos, e->p); float d = arc_v2_len(to);
        if (d > 1e-4f) e->p = arc_v2_add(e->p, arc_v2_scale(to, e->speed * arc_dt() / d));
        for (int o = k + 1; o < MAX_ENEMIES; o++) if (G.e[o].on) {   /* keep them from stacking */
            arc_v2 s = arc_v2_sub(G.e[o].p, e->p); float l = arc_v2_len(s);
            if (l > 1e-4f && l < 0.8f) { arc_v2 push = arc_v2_scale(s, (0.8f - l) * 0.5f / l); e->p = arc_v2_sub(e->p, push); G.e[o].p = arc_v2_add(G.e[o].p, push); }
        }
        if (d < 0.8f && G.safe == 0 && !G.god) {
            G.lives--; G.safe = 90; arc_play(s_hurt, 0.6f, 0);
            if (G.lives <= 0) { G.phase = PHASE_OVER; arc_event("game_over", "\"wave\":%d,\"score\":%d", G.wave, G.score); return; }
        }
    }
    /* waves */
    if (G.phase == PHASE_PLAY && count_enemies() == 0) { G.phase = PHASE_BETWEEN; G.between = 90; }
    else if (G.phase == PHASE_BETWEEN && --G.between <= 0) start_wave(G.wave + 1);
}
static void draw(float alpha) {
    (void)alpha;
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(-0.3f, 1, 0.5f), 0xFFF0E0, 0x384050);
    arc_set_shadows(arc_v3_make(0, 0, 0), 12, 1024);
    arc_draw_mesh(m_floor, arc_m4_trs_y(arc_v3_make(0, 0, 0), 0, arc_v3_make(2 * ARENA, 1, 2 * ARENA)), 0xFFFFFF);
    float L = 2 * ARENA + 1;
    arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(0, 0.4f, -ARENA - 0.5f), 0, arc_v3_make(L, 0.8f, 1)), 0xFFFFFF);
    arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(0, 0.4f, ARENA + 0.5f), 0, arc_v3_make(L, 0.8f, 1)), 0xFFFFFF);
    arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(-ARENA - 0.5f, 0.4f, 0), 0, arc_v3_make(1, 0.8f, L)), 0xFFFFFF);
    arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(ARENA + 0.5f, 0.4f, 0), 0, arc_v3_make(1, 0.8f, L)), 0xFFFFFF);
    bool blink = G.safe > 0 && (G.safe / 6) % 2;
    if (G.phase != PHASE_OVER && !blink) {
        arc_draw_mesh(m_player, arc_m4_trs_y(arc_v3_make(G.pos.x, 0.4f, G.pos.y), -G.aim, arc_v3_make(0.8f, 0.8f, 0.8f)), 0xFFFFFF);
        arc_v2 g = arc_v2_add(G.pos, arc_v2_make(cosf(G.aim) * 0.6f, sinf(G.aim) * 0.6f));
        arc_draw_mesh(m_gun, arc_m4_trs_y(arc_v3_make(g.x, 0.55f, g.y), -G.aim, arc_v3_make(0.6f, 0.18f, 0.18f)), 0xFFFFFF);
    }
    for (int k = 0; k < MAX_ENEMIES; k++) if (G.e[k].on)
        arc_draw_mesh(m_enemy, arc_m4_trs_y(arc_v3_make(G.e[k].p.x, 0.35f, G.e[k].p.y), 0, arc_v3_make(0.7f, 0.7f, 0.7f)), 0xFFFFFF);
    for (int i = 0; i < MAX_BULLETS; i++) if (G.b[i].on)
        arc_draw_mesh(m_bullet, arc_m4_trs_y(arc_v3_make(G.b[i].p.x, 0.5f, G.b[i].p.y), 0, arc_v3_make(0.25f, 0.25f, 0.25f)), 0xFFFFFF);
    arc_draw_textf(10, 10, 3, 0xFFFFFF, "SCORE %d", G.score);
    arc_draw_textf(10, 40, 2, 0xB0B8C8, "WAVE %d   LIVES %d", G.wave, G.lives);
    if (G.phase == PHASE_BETWEEN) arc_draw_text_ex((float)arc_width() / 2.0f, (float)arc_height() / 3.0f, 4, 0xFFE060, ARC_TEXT_CENTER | ARC_TEXT_SHADOW, arc_fmt("WAVE %d", G.wave + 1));
    if (G.phase == PHASE_OVER) arc_draw_text_ex((float)arc_width() / 2.0f, (float)arc_height() / 3.0f, 4, 0xFF6060, ARC_TEXT_CENTER | ARC_TEXT_SHADOW, "GAME OVER - R to restart");
}
static void dump(arc_json *j) {
    static const char *ph[] = { "play", "between", "over" };
    arc_json_str_k(j, "phase", ph[G.phase]); arc_json_num_k(j, "wave", G.wave); arc_json_num_k(j, "score", G.score);
    arc_json_num_k(j, "lives", G.lives); arc_json_num_k(j, "kills", G.kills);
    arc_json_num_k(j, "x", G.pos.x); arc_json_num_k(j, "z", G.pos.y); arc_json_num_k(j, "aim_deg", G.aim * 57.29578f);
    arc_json_num_k(j, "enemies", count_enemies()); arc_json_num_k(j, "bullets", count_bullets()); arc_json_bool_k(j, "god", G.god);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "aim") && argc >= 1) { G.aim = (float)atof(argv[0]) / 57.29578f; return true; }
    if (!strcmp(name, "fire")) { fire(); return true; }
    if (!strcmp(name, "wave") && argc >= 1) { for (int k = 0; k < MAX_ENEMIES; k++) G.e[k].on = false; start_wave(atoi(argv[0])); return true; }
    if (!strcmp(name, "spawn") && argc >= 1) { for (int n = atoi(argv[0]); n > 0; n--) spawn_enemy(); return true; }
    if (!strcmp(name, "spawn_at") && argc >= 2) {   /* one enemy at (x, z): for tests */
        for (int k = 0; k < MAX_ENEMIES; k++) if (!G.e[k].on) { G.e[k] = (Enemy){ true, arc_v2_make((float)atof(argv[0]), (float)atof(argv[1])), *ENEMY_SPEED, 0 }; break; }
        if (G.phase == PHASE_BETWEEN) G.phase = PHASE_PLAY;
        return true;
    }
    if (!strcmp(name, "lives") && argc >= 1) { G.lives = atoi(argv[0]); return true; }
    if (!strcmp(name, "clear")) { for (int k = 0; k < MAX_ENEMIES; k++) G.e[k].on = false; return true; }
    if (!strcmp(name, "god")) { G.god = !G.god; return true; }
    if (!strcmp(name, "restart")) { reset(); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t size) { if (size != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "{title}", .width = 960, .height = 640, .clear_color = 0x15181F,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
