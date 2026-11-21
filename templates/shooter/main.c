/* {title} - a top-down arena shooter: Neon Arena (ARC template "shooter").
 *
 * WASD moves, the mouse aims, left button or SPACE fires. Drones (one hit) and armoured beetles (three hits; they
 * stop, rev up and charge) come in waves from the arena's edges. Every 5th wave a boss beetle comes too. Kills in
 * quick succession build a combo (x2 .. x8). Crystals dropped by kills give spread shot (green), rapid fire
 * (yellow) or a shield (blue). Crates are cover: they stop shots and break after a few. Three lives; R restarts.
 *
 * Where things are:
 *   Game G           all game state in fixed pools (bullets, enemies, crystals, crates, popups): no allocation while
 *                    playing, and save_state/load_state copy it whole (rewind, seek and the timeline work).
 *   tick()           input, movement, firing, collisions, waves, combo, pickups, game over. draw() only draws.
 *   juice            hit-stop (G.hitstop freezes the rules for a few ticks on kills), camera shake (rig), hit
 *                    flashes (whiten), spawn pop-in (ease_out_back), recoil, particles (FX_*), floating score
 *                    numbers (arc_popup_text), a rolling score counter, bloom; sounds at varied pitch (arc_play_var).
 *   assets/          models, sounds and music made with Ludo (ludo.ai) and imported with tools/arc_import.py.
 *                    The player is a rigged model with idle / run / hit / death clips (assets/meshes/player.arca).
 *   dump()           phase, wave, score, combo, lives, player position and aim, power-up, counts of each thing.
 *   command()        agent commands: aim DEG, fire, wave N, spawn N, spawn_at X Z [drone|beetle|boss], crystal X Z
 *                    [spread|rapid|shield], clear, lives N, god, restart.
 *   arc_var          tunables: speed, enemy_speed, fire_rate, bullet_speed, combo_time, drop_chance.
 * Grow it: more enemy kinds (a row in KINDS), new power-ups (a row in POWERS), gamepad sticks (arc_pad_axis),
 * an online high-score table, a level layout per wave.
 */
#include "arc.h"
#include <string.h>
#include <stdlib.h>

#define ARENA 9.5f          /* the arena spans -ARENA..ARENA in x and z */
#define MAX_BULLETS 96
#define MAX_ENEMIES 96
#define MAX_CRYSTALS 8
#define MAX_CRATES 6
enum { PHASE_PLAY, PHASE_BETWEEN, PHASE_DYING, PHASE_OVER };
enum { E_DRONE, E_BEETLE, E_BOSS, E_KINDS };
enum { P_NONE, P_SPREAD, P_RAPID, P_SHIELD, P_KINDS };

/* each enemy kind: speed (x enemy_speed), hit points, score, size (collision radius), model scale */
static const struct { const char *name; float speed; int hp; int score; float radius, scale; } KINDS[E_KINDS] = {
    { "drone",  1.00f,  1,  10, 0.45f, 0.95f },
    { "beetle", 0.60f,  3,  30, 0.60f, 1.10f },
    { "boss",   0.45f, 30, 500, 1.20f, 2.30f },
};
static const struct { const char *name; uint32_t rgb; int ticks; } POWERS[P_KINDS] = {
    { "none", 0xFFFFFF, 0 }, { "spread", 0x40FF80, 480 }, { "rapid", 0xFFE040, 480 }, { "shield", 0x40A0FF, 0 },
};

typedef struct { bool on; arc_v2 p, v; int life; } Bullet;
typedef struct {
    bool on; int kind, hp;
    arc_v2 p, knock;        /* position; knock-back velocity (decays) */
    float speed;
    int age;                /* ticks since spawning: it pops in and holds still for the first 30 */
    int flash;              /* ticks of white hit flash */
    int charge;             /* beetles: >0 revving up (counts down to the dash), <0 dashing (counts up to 0) */
    arc_v2 dash;            /* the dash direction */
} Enemy;
typedef struct { bool on; int kind, age; arc_v2 p; } Crystal;
typedef struct { bool on; int hp, flash; arc_v2 p; } Crate;
typedef struct {
    int phase, wave, score, lives, kills, best;
    int combo, combo_timer;           /* kills in a row, ticks left to extend it */
    arc_v2 pos, vel;                  /* player on the ground plane (x, z) */
    float aim;                        /* radians, 0 = +x */
    int cooldown, safe, between, dying, hitstop, recoil, hurt_flash, banner;
    int power, power_ticks; bool shield;
    float anim_t, run_w, hit_t, death_t;   /* the player's animation clocks and the run / idle blend */
    float shown_score;                /* the HUD counter rolls up to score */
    bool god;
    Bullet b[MAX_BULLETS];
    Enemy e[MAX_ENEMIES];
    Crystal c[MAX_CRYSTALS];
    Crate k[MAX_CRATES];
} Game;
static Game G;

static float *SPEED, *ENEMY_SPEED, *FIRE_RATE, *BULLET_SPEED, *COMBO_TIME, *DROP_CHANCE;
static float v_speed, v_espeed, v_rate, v_bspeed, v_combo, v_drop;
static arc_mesh m_floor, m_wall, m_player, m_enemy[E_KINDS], m_bullet, m_crystal, m_crate;
static arc_anim a_player; static int clip_idle = -1, clip_run = -1, clip_hit = -1, clip_death = -1;
static arc_sound s_shot, s_hit, s_boom, s_hurt, s_pickup, s_wave;
static arc_music music;
static arc_cam_rig rig;

/* ---- particles (described once; ARC moves them with its own generator, so replays stay exact) ---- */
static const arc_fx FX_MUZZLE = { .life = 0.12f, .speed = 6, .speed_var = 3, .spread = 0.5f, .size = 0.25f, .size_end = 0.05f,
                                  .color = 0xFFF0A0, .color_end = 0xFF6020, .glow = 2, .additive = true, .burst = 6 };
static const arc_fx FX_SPARK = { .life = 0.25f, .life_var = 0.1f, .speed = 7, .speed_var = 3, .spread = 1.2f, .size = 0.08f,
                                 .color = 0xFFFFFF, .color_end = 0x40C0FF, .glow = 2, .additive = true, .stretch = 0.04f,
                                 .gravity = { 0, -12, 0 }, .burst = 8 };
static const arc_fx FX_BOUNCE = { .life = 0.3f, .speed = 3, .spread = 0.8f, .size = 0.06f, .color = 0xFFC060, .glow = 1.5f,
                                  .additive = true, .burst = 3 };
static const arc_fx FX_FIRE = { .life = 0.45f, .life_var = 0.15f, .speed = 4, .speed_var = 2, .spread = 3.14159f, .radius = 0.2f,
                                .size = 0.7f, .size_end = 0.1f, .color = 0xFFE080, .color_end = 0xFF3010, .glow = 2.5f,
                                .additive = true, .drag = 3, .burst = 24 };
static const arc_fx FX_SMOKE = { .life = 1.1f, .life_var = 0.3f, .speed = 1.5f, .spread = 3.14159f, .radius = 0.3f, .size = 0.5f,
                                 .size_end = 1.4f, .color = 0x404048, .alpha = 0.6f, .gravity = { 0, 1.2f, 0 }, .drag = 1.5f,
                                 .soft = 0.3f, .burst = 10 };
static const arc_fx FX_DEBRIS = { .life = 1.2f, .life_var = 0.4f, .speed = 7, .speed_var = 3, .spread = 1.0f, .size = 0.12f,
                                  .color = 0x303438, .gravity = { 0, -18, 0 }, .bounce = 0.4f, .spin = 8, .spin_var = 6,
                                  .on_bounce = &FX_BOUNCE, .burst = 10 };
static const arc_fx FX_SPAWN = { .life = 0.5f, .speed = 3, .spread = 3.14159f, .size = 0.15f, .size_end = 0.02f, .color = 0xFF40C0,
                                 .glow = 2, .additive = true, .gravity = { 0, 2, 0 }, .burst = 14 };
static arc_fx FX_PICKUP[P_KINDS];   /* one per power-up colour, filled in init */

/* ---- small juice helpers (ARC's juice kit: arc_play_var, arc_popup_text, arc_yaw_to) ---- */
static void play_var(arc_sound s, float vol, float x) { arc_play_var(s, vol, arc_clampf(x / ARENA, -1, 1) * 0.6f, 0.1f); }   /* panned by x */
static void shake(float amount) { arc_cam_rig_shake(&rig, amount); }
static arc_v3 at(arc_v2 p, float y) { return arc_v3_make(p.x, y, p.y); }
static void popup(arc_v2 p, int value, uint32_t rgb) { arc_popup_text(at(p, 1.2f), rgb, 2.5f, value ? arc_fmt("+%d", value) : "POWER!"); }

static int count_enemies(void) { int n = 0; for (int i = 0; i < MAX_ENEMIES; i++) n += G.e[i].on; return n; }
static int count_kind(int kind) { int n = 0; for (int i = 0; i < MAX_ENEMIES; i++) n += G.e[i].on && G.e[i].kind == kind; return n; }
static int count_bullets(void) { int n = 0; for (int i = 0; i < MAX_BULLETS; i++) n += G.b[i].on; return n; }
static int count_crystals(void) { int n = 0; for (int i = 0; i < MAX_CRYSTALS; i++) n += G.c[i].on; return n; }
static int count_crates(void) { int n = 0; for (int i = 0; i < MAX_CRATES; i++) n += G.k[i].on; return n; }

static void add_enemy(int kind, arc_v2 p) {
    for (int i = 0; i < MAX_ENEMIES; i++) if (!G.e[i].on) {
        float jitter = arc_rand_rangef(0.85f, 1.15f);
        G.e[i] = (Enemy){ .on = true, .kind = kind, .hp = KINDS[kind].hp, .p = p, .speed = KINDS[kind].speed * jitter };
        arc_fx_burst(&FX_SPAWN, at(p, 0.4f), arc_v3_make(0, 1, 0), 0);
        return;
    }
}
static void spawn_enemy(int kind) {
    float t = arc_rand_rangef(-ARENA + 1, ARENA - 1); int side = arc_rand_range(0, 3);
    float e = ARENA - 0.6f;
    arc_v2 p = side == 0 ? arc_v2_make(t, -e) : side == 1 ? arc_v2_make(t, e) : side == 2 ? arc_v2_make(-e, t) : arc_v2_make(e, t);
    add_enemy(kind, p);
}
static void place_crates(void) {
    static const float xz[MAX_CRATES][2] = { { -4.5f, -4.5f }, { 4.5f, -4.5f }, { -4.5f, 4.5f }, { 4.5f, 4.5f }, { 0, -6.5f }, { 0, 6.5f } };
    for (int i = 0; i < MAX_CRATES; i++) G.k[i] = (Crate){ true, 4, 0, arc_v2_make(xz[i][0], xz[i][1]) };
}
static void start_wave(int n) {
    G.wave = n; G.phase = PHASE_PLAY; G.banner = 0;
    int total = 3 + 2 * n;
    float beetle_share = arc_clampf(0.1f * (float)(n - 1), 0, 0.4f);   /* wave 1 is all drones */
    int beetles = (int)((float)total * beetle_share);
    for (int i = 0; i < total; i++) spawn_enemy(i < beetles ? E_BEETLE : E_DRONE);
    if (n % 5 == 0) spawn_enemy(E_BOSS);
    place_crates();
    arc_play(s_wave, 0.5f, 0);
    arc_event("wave", "\"wave\":%d", n);
}
static void reset(void) {
    int best = G.best;
    memset(&G, 0, sizeof G); G.lives = 3; G.best = best;
    start_wave(1);
}
static void fire_one(float ang) {
    for (int i = 0; i < MAX_BULLETS; i++) if (!G.b[i].on) {
        arc_v2 d = arc_v2_make(cosf(ang), sinf(ang));
        G.b[i] = (Bullet){ true, arc_v2_add(G.pos, arc_v2_scale(d, 0.6f)), arc_v2_scale(d, *BULLET_SPEED), 90 };
        return;
    }
}
static void fire(void) {
    if (G.cooldown > 0 || G.phase == PHASE_OVER || G.phase == PHASE_DYING) return;
    fire_one(G.aim);
    if (G.power == P_SPREAD) { fire_one(G.aim - 0.22f); fire_one(G.aim + 0.22f); }
    float rate = *FIRE_RATE * (G.power == P_RAPID ? 2.0f : 1.0f);
    G.cooldown = (int)(60.0f / rate); G.recoil = 4;
    arc_v2 tip = arc_v2_add(G.pos, arc_v2_make(cosf(G.aim) * 0.7f, sinf(G.aim) * 0.7f));
    arc_fx_burst(&FX_MUZZLE, at(tip, 0.75f), arc_v3_make(cosf(G.aim), 0, sinf(G.aim)), 0);
    play_var(s_shot, 0.25f, G.pos.x);
}
static void drop_crystal(arc_v2 p, int kind) {
    for (int i = 0; i < MAX_CRYSTALS; i++) if (!G.c[i].on) { G.c[i] = (Crystal){ true, kind, 0, p }; return; }
}
static void kill_enemy(Enemy *e) {
    const int kind = e->kind;
    e->on = false; G.kills++;
    G.combo = G.combo_timer > 0 ? G.combo + 1 : 1; G.combo_timer = (int)(*COMBO_TIME * 60.0f);
    int mult = G.combo < 8 ? G.combo : 8;
    int pts = KINDS[kind].score * G.wave * mult; G.score += pts;
    popup(e->p, pts, mult > 1 ? 0xFFE060 : 0xFFFFFF);
    arc_v3 c = at(e->p, 0.4f);
    arc_fx_burst(&FX_FIRE, c, arc_v3_make(0, 1, 0), kind == E_BOSS ? 80 : 0);
    arc_fx_burst(&FX_SMOKE, c, arc_v3_make(0, 1, 0), kind == E_BOSS ? 30 : 0);
    arc_fx_burst(&FX_DEBRIS, c, arc_v3_make(0, 1, 0), kind == E_BOSS ? 40 : 0);
    play_var(s_boom, kind == E_BOSS ? 1.0f : 0.5f, e->p.x);
    G.hitstop = kind == E_BOSS ? 14 : 3;
    shake(kind == E_BOSS ? 0.9f : kind == E_BEETLE ? 0.35f : 0.2f);
    if (kind == E_BOSS || arc_rand_float() < *DROP_CHANCE) drop_crystal(e->p, arc_rand_range(P_SPREAD, P_SHIELD));
}
static void hurt_player(void) {
    if (G.safe > 0 || G.god || (G.phase != PHASE_PLAY && G.phase != PHASE_BETWEEN)) return;
    G.safe = 90; G.hurt_flash = 20; G.hit_t = 0.001f; shake(0.7f);
    if (G.shield) { G.shield = false; G.power = P_NONE; play_var(s_hit, 0.6f, G.pos.x); return; }
    G.lives--; G.combo = 0; G.combo_timer = 0;
    play_var(s_hurt, 0.7f, G.pos.x);
    if (G.lives <= 0) {
        G.phase = PHASE_DYING; G.dying = 100; G.death_t = 0;
        if (G.score > G.best) { G.best = G.score; arc_save("best", &G.best, sizeof G.best); }
        arc_event("game_over", "\"wave\":%d,\"score\":%d", G.wave, G.score);
    }
}

static void init(void) {
    SPEED = arc_var("speed", &v_speed, 6); ENEMY_SPEED = arc_var("enemy_speed", &v_espeed, 2.2f);
    FIRE_RATE = arc_var("fire_rate", &v_rate, 8); BULLET_SPEED = arc_var("bullet_speed", &v_bspeed, 18);
    COMBO_TIME = arc_var("combo_time", &v_combo, 1.5f); DROP_CHANCE = arc_var("drop_chance", &v_drop, 0.08f);
    for (int k = 0; k < P_KINDS; k++)
        FX_PICKUP[k] = (arc_fx){ .life = 0.6f, .speed = 5, .spread = 3.14159f, .size = 0.15f, .size_end = 0.02f,
                                 .color = POWERS[k].rgb, .glow = 2.5f, .additive = true, .drag = 2, .burst = 24 };
    m_floor = arc_mesh_plane(0xFFFFFF, 1, 0xFFFFFF); arc_mesh_set_name(m_floor, "floor");
    arc_mesh_set_texture(m_floor, arc_texture_load_ex("assets/textures/floor.jpg", ARC_TEX_MESH));   /* repeats: uv_scale in draw */
    m_wall = arc_mesh_load("assets/meshes/wall.arcm"); arc_mesh_set_name(m_wall, "wall");   /* 2 x 0.9 x 0.8, front +z */
    m_player = arc_mesh_load("assets/meshes/player.arcm"); arc_mesh_set_name(m_player, "player");
    a_player = arc_anim_load("assets/meshes/player.arca");
    clip_idle = arc_anim_find(a_player, "idle"); clip_run = arc_anim_find(a_player, "run");
    clip_hit = arc_anim_find(a_player, "hit"); clip_death = arc_anim_find(a_player, "death");
    m_enemy[E_DRONE] = arc_mesh_load("assets/meshes/drone.arcm");
    m_enemy[E_BEETLE] = m_enemy[E_BOSS] = arc_mesh_load("assets/meshes/beetle.arcm");
    arc_mesh_set_name(m_enemy[E_DRONE], "enemy"); arc_mesh_set_name(m_enemy[E_BEETLE], "beetle");
    m_crystal = arc_mesh_load("assets/meshes/crystal.arcm"); arc_mesh_set_name(m_crystal, "crystal");
    m_crate = arc_mesh_load("assets/meshes/crate.arcm"); arc_mesh_set_name(m_crate, "crate");
    m_bullet = arc_mesh_sphere(0xFFE060, 8); arc_mesh_set_name(m_bullet, "bullet");
    s_shot = arc_sound_load("assets/sfx/shot.mp3"); s_hit = arc_sound_load("assets/sfx/hit.mp3");
    s_boom = arc_sound_load("assets/sfx/boom.mp3"); s_hurt = arc_sound_load("assets/sfx/hurt.mp3");
    s_pickup = arc_sound_load("assets/sfx/pickup.mp3"); s_wave = arc_sound_load("assets/sfx/wave.mp3");
    music = arc_music_load("assets/music/music.mp3"); arc_music_set_volume(music, 0.45f); arc_music_play(music, true);
    rig = (arc_cam_rig){ .kind = ARC_RIG_FIXED, .eye = { 0, 17.0f, 9.5f }, .target = { 0, 0, 1.3f }, .fov_y = 0.95f };
    arc_set_post(&(arc_post){ .bloom = 0.6f, .bloom_threshold = 1.2f, .tonemap = true, .vignette = 0.35f, .contrast = 0.1f, .fxaa = true });
    size_t n = 0; uint8_t *best = arc_load("best", arc_frame(), &n);
    reset();
    if (best && n == sizeof G.best) memcpy(&G.best, best, sizeof G.best);
}

static void tick_player(void) {
    arc_v2 in = arc_v2_make((arc_key_down(ARC_KEY_D) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_A) ? 1.0f : 0.0f),
                            (arc_key_down(ARC_KEY_S) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_W) ? 1.0f : 0.0f));
    G.vel = arc_v2_len(in) > 0 ? arc_v2_scale(arc_v2_norm(in), *SPEED) : arc_v2_make(0, 0);
    arc_v2 np = arc_v2_add(G.pos, arc_v2_scale(G.vel, arc_dt()));
    for (int i = 0; i < MAX_CRATES; i++) if (G.k[i].on) {   /* crates block: push out of a circle around each */
        arc_v2 d = arc_v2_sub(np, G.k[i].p); float l = arc_v2_len(d);
        if (l < 1.0f && l > 1e-4f) np = arc_v2_add(G.k[i].p, arc_v2_scale(d, 1.0f / l));
    }
    G.pos.x = arc_clampf(np.x, -ARENA + 0.5f, ARENA - 0.5f);
    G.pos.y = arc_clampf(np.y, -ARENA + 0.5f, ARENA - 0.5f);
    /* aim at the mouse on the ground (when it moved: agents can aim with the `aim` command) */
    arc_v2 md = arc_mouse_delta();
    if (md.x != 0 || md.y != 0) {
        arc_camera cam = arc_cam_rig_camera(&rig); arc_v3 o, d;
        arc_camera_ray(&cam, (float)arc_width(), (float)arc_height(), arc_mouse(), &o, &d);
        if (d.y < -1e-4f) { float t = -o.y / d.y; G.aim = atan2f(o.z + d.z * t - G.pos.y, o.x + d.x * t - G.pos.x); }
    }
    if (G.cooldown > 0) G.cooldown--;
    if (G.recoil > 0) G.recoil--;
    if (arc_key_down(ARC_MOUSE_LEFT) || arc_key_down(ARC_KEY_SPACE)) fire();
    if (G.power_ticks > 0 && --G.power_ticks == 0 && G.power != P_SHIELD) G.power = P_NONE;
}
static void tick_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) if (G.b[i].on) {
        Bullet *b = &G.b[i];
        b->p = arc_v2_add(b->p, arc_v2_scale(b->v, arc_dt()));
        arc_v3 dir = arc_v3_make(-b->v.x, 0, -b->v.y);
        if (--b->life <= 0 || fabsf(b->p.x) > ARENA || fabsf(b->p.y) > ARENA) {
            if (b->life > 0) arc_fx_burst(&FX_SPARK, at(b->p, 0.5f), dir, 5);
            b->on = false; continue;
        }
        for (int k = 0; k < MAX_CRATES && b->on; k++) if (G.k[k].on && arc_v2_len(arc_v2_sub(G.k[k].p, b->p)) < 0.6f) {
            Crate *c = &G.k[k]; b->on = false; c->flash = 4; arc_fx_burst(&FX_SPARK, at(b->p, 0.5f), dir, 0);
            if (--c->hp <= 0) { c->on = false; arc_fx_burst(&FX_DEBRIS, at(c->p, 0.4f), arc_v3_make(0, 1, 0), 16);
                                arc_fx_burst(&FX_SMOKE, at(c->p, 0.4f), arc_v3_make(0, 1, 0), 6); play_var(s_boom, 0.35f, c->p.x); shake(0.15f); }
            else play_var(s_hit, 0.25f, c->p.x);
        }
        for (int k = 0; k < MAX_ENEMIES && b->on; k++) {
            Enemy *e = &G.e[k];
            if (!e->on || arc_v2_len(arc_v2_sub(e->p, b->p)) >= KINDS[e->kind].radius + 0.15f) continue;
            b->on = false; e->flash = 5;
            e->knock = arc_v2_add(e->knock, arc_v2_scale(arc_v2_norm(b->v), e->kind == E_BOSS ? 1.5f : 6.0f));
            arc_fx_burst(&FX_SPARK, at(b->p, 0.5f), dir, 0);
            if (--e->hp <= 0) kill_enemy(e); else play_var(s_hit, 0.35f, e->p.x);
        }
    }
}
static void tick_enemies(void) {
    if (G.safe > 0) G.safe--;
    for (int k = 0; k < MAX_ENEMIES; k++) if (G.e[k].on) {
        Enemy *e = &G.e[k]; e->age++;
        if (e->flash > 0) e->flash--;
        arc_v2 to = arc_v2_sub(G.pos, e->p); float d = arc_v2_len(to);
        if (e->age > 30) {   /* after popping in */
            float sp = e->speed * *ENEMY_SPEED;
            arc_v2 step = d > 1e-4f ? arc_v2_scale(to, sp * arc_dt() / d) : arc_v2_make(0, 0);
            if (e->kind != E_DRONE) {   /* beetles: rev up within 5 units, then dash */
                if (e->charge == 0 && d < 5.0f && d > 1.5f && e->age % 40 == 0) { e->charge = 30; e->dash = arc_v2_norm(to); }
                if (e->charge > 0) { step = arc_v2_make(0, 0); if (--e->charge == 0) e->charge = -40; }
                else if (e->charge < 0) { step = arc_v2_scale(e->dash, sp * 3.5f * arc_dt()); if (++e->charge == 0) e->charge = 0; }
            }
            e->p = arc_v2_add(e->p, arc_v2_add(step, arc_v2_scale(e->knock, arc_dt())));
        }
        e->knock = arc_v2_scale(e->knock, 0.85f);
        float r = KINDS[e->kind].radius;
        for (int o = k + 1; o < MAX_ENEMIES; o++) if (G.e[o].on) {   /* keep them from stacking */
            float rr = r + KINDS[G.e[o].kind].radius;
            arc_v2 s = arc_v2_sub(G.e[o].p, e->p); float l = arc_v2_len(s);
            if (l > 1e-4f && l < rr) { arc_v2 push = arc_v2_scale(s, (rr - l) * 0.5f / l); e->p = arc_v2_sub(e->p, push); G.e[o].p = arc_v2_add(G.e[o].p, push); }
        }
        for (int i = 0; i < MAX_CRATES; i++) if (G.k[i].on) {
            arc_v2 s = arc_v2_sub(e->p, G.k[i].p); float l = arc_v2_len(s), rr = r + 0.6f;
            if (l > 1e-4f && l < rr) e->p = arc_v2_add(G.k[i].p, arc_v2_scale(s, rr / l));
        }
        e->p.x = arc_clampf(e->p.x, -ARENA + r, ARENA - r); e->p.y = arc_clampf(e->p.y, -ARENA + r, ARENA - r);
        if (d < r + 0.4f && e->age > 30) hurt_player();
    }
}
static void tick_crystals(void) {
    for (int i = 0; i < MAX_CRYSTALS; i++) if (G.c[i].on) {
        Crystal *c = &G.c[i];
        if (++c->age > 600) { c->on = false; continue; }   /* 10 seconds to pick it up */
        if (arc_v2_len(arc_v2_sub(c->p, G.pos)) < 0.8f) {
            c->on = false; G.power = c->kind; G.power_ticks = POWERS[c->kind].ticks; G.shield = c->kind == P_SHIELD;
            arc_fx_burst(&FX_PICKUP[c->kind], at(c->p, 0.6f), arc_v3_make(0, 1, 0), 0);
            popup(c->p, 0, POWERS[c->kind].rgb);
            play_var(s_pickup, 0.6f, c->p.x);
        }
    }
}
static void tick(void) {
    if (arc_key_pressed(ARC_KEY_R)) reset();
    G.shown_score += ((float)G.score - G.shown_score) * 0.2f; if (fabsf((float)G.score - G.shown_score) < 1) G.shown_score = (float)G.score;
    if (G.hurt_flash > 0) G.hurt_flash--;
    for (int i = 0; i < MAX_CRATES; i++) if (G.k[i].flash > 0) G.k[i].flash--;
    G.banner++;
    arc_cam_rig_update(&rig, arc_dt());
    if (G.hitstop > 0) { G.hitstop--; return; }   /* the world holds its breath: rules frozen, looks go on */
    if (G.phase == PHASE_OVER) return;
    /* player animation clocks (pure presentation, but kept in G so rewinds show the same pose) */
    G.anim_t += arc_dt();
    G.run_w = arc_lerpf(G.run_w, arc_v2_len(G.vel) > 0.1f ? 1.0f : 0.0f, 0.2f);
    if (G.hit_t > 0) { G.hit_t += arc_dt(); if (clip_hit < 0 || G.hit_t > arc_anim_length(a_player, clip_hit)) G.hit_t = 0; }
    if (G.phase == PHASE_DYING) {
        G.death_t += arc_dt();
        if (--G.dying <= 0) G.phase = PHASE_OVER;
        return;
    }
    tick_player();
    tick_bullets();
    tick_enemies();
    tick_crystals();
    if (G.combo_timer > 0 && --G.combo_timer == 0) G.combo = 0;
    /* waves */
    if (G.phase == PHASE_PLAY && count_enemies() == 0) { G.phase = PHASE_BETWEEN; G.between = 90; G.banner = 0; }
    else if (G.phase == PHASE_BETWEEN && --G.between <= 0) start_wave(G.wave + 1);
}

static void draw_player(void) {
    bool blink = G.safe > 0 && G.phase != PHASE_DYING && G.phase != PHASE_OVER && (G.safe / 6) % 2;
    if (blink) return;
    float back = G.recoil > 0 ? 0.08f * (float)G.recoil / 4.0f : 0;   /* kick back when firing */
    arc_v2 p = arc_v2_sub(G.pos, arc_v2_make(cosf(G.aim) * back, sinf(G.aim) * back));
    float yaw = arc_yaw_to(cosf(G.aim), sinf(G.aim));   /* aim 0 is +x */
    arc_anim_layer L[3]; int n = 0;
    if (G.phase == PHASE_DYING || G.phase == PHASE_OVER) {
        L[n++] = (arc_anim_layer){ clip_death, G.death_t, 1, false };
    } else {
        L[n++] = (arc_anim_layer){ clip_idle, G.anim_t, 1.0f - G.run_w + 0.001f, true };
        L[n++] = (arc_anim_layer){ clip_run, G.anim_t, G.run_w, true };
        if (G.hit_t > 0) L[n++] = (arc_anim_layer){ clip_hit, G.hit_t, 2.0f, false };
    }
    /* a glowing ring under the player: easy to find in a crowd (cyan; blue with a shield) */
    arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = G.pos.x, .z = G.pos.y, .size = 1.4f, .inner = 0.75f,
                                   .fill_rgb = G.shield ? 0x4080FF : 0x30E0FF, .fill_alpha = 0.8f, .on_top = true });
    static arc_pose pose; arc_anim_pose(a_player, L, n, &pose);
    arc_draw_mesh_posed_styled(m_player, arc_m4_trs_y(at(p, 0), yaw, arc_v3_make(1.15f, 1.15f, 1.15f)), a_player, &pose,
                               &(arc_mesh_style){ .whiten = G.hurt_flash > 12 ? 0.8f : 0 });
    if (G.shield) arc_draw_mesh_styled(m_bullet, arc_m4_trs_y(at(G.pos, 0.7f), 0, arc_v3_make(1.8f, 1.8f, 1.8f)),
                                       &(arc_mesh_style){ .tint = 0x40A0FF, .hologram = 0.6f });
}
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = arc_cam_rig_camera(&rig); arc_set_camera(&cam);
    arc_set_light(arc_v3_make(-0.3f, 1, 0.5f), 0xE0E8FF, 0x283040);
    arc_set_shadows(arc_v3_make(0, 0, 0), 12, 1024);
    arc_draw_mesh_styled(m_floor, arc_m4_trs_y(arc_v3_make(0, 0, 0), 0, arc_v3_make(2 * ARENA, 1, 2 * ARENA)), &(arc_mesh_style){ .uv_scale = 6, .tint = 0x8890A8 });
    for (int i = 0; i < 10; i++) {   /* the arena wall: 10 two-unit segments a side, their fronts facing in */
        float t = -9.0f + 2.0f * (float)i, e = ARENA + 0.4f;
        arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(t, 0, -e), 0, arc_v3_make(1, 1, 1)), 0xFFFFFF);
        arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(t, 0, e), 3.14159265f, arc_v3_make(1, 1, 1)), 0xFFFFFF);
        arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(-e, 0, t), 1.5707963f, arc_v3_make(1, 1, 1)), 0xFFFFFF);
        arc_draw_mesh(m_wall, arc_m4_trs_y(arc_v3_make(e, 0, t), -1.5707963f, arc_v3_make(1, 1, 1)), 0xFFFFFF);
    }
    for (int i = 0; i < MAX_CRATES; i++) if (G.k[i].on)
        arc_draw_mesh_styled(m_crate, arc_m4_trs_y(at(G.k[i].p, 0), 0.3f * (float)i, arc_v3_make(1, 1, 1)),
                             &(arc_mesh_style){ .whiten = G.k[i].flash > 0 ? 0.7f : 0 });
    if (G.phase != PHASE_OVER || G.death_t > 0) draw_player();
    for (int k = 0; k < MAX_ENEMIES; k++) if (G.e[k].on) {
        const Enemy *e = &G.e[k]; float s = KINDS[e->kind].scale;
        float pop = e->age < 20 ? arc_ease_out_back((float)e->age / 20.0f) : 1.0f;      /* pop in */
        float bob = e->kind == E_DRONE ? 0.5f + 0.12f * sinf((float)(e->age + k * 7) * 0.12f) : 0;
        float rev = e->charge > 0 ? 0.06f * sinf((float)e->charge * 1.7f) : 0;            /* shudder while revving */
        arc_v2 to = arc_v2_sub(G.pos, e->p); float yaw = arc_yaw_to(to.x, to.y);
        if (e->charge < 0) yaw = arc_yaw_to(e->dash.x, e->dash.y);
        arc_v3 sc = arc_v3_make(s * pop * (1 + rev), s * pop * (1 - rev), s * pop * (1 + rev));
        if (e->age < 30)   /* where it is coming in: a ring that shrinks onto it */
            arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = e->p.x, .z = e->p.y, .size = 2.5f * s * (1.0f - (float)e->age / 40.0f),
                                           .fill_rgb = 0xFF40C0, .fill_alpha = 0.7f });
        if (e->charge > 0)   /* a beetle revving up: an arrow where it will charge (fair warning) */
            arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_ARROW, .x = e->p.x, .z = e->p.y, .x2 = e->p.x + e->dash.x * 4, .z2 = e->p.y + e->dash.y * 4,
                                           .size = 0.35f, .fill_rgb = 0xFF3030, .fill_alpha = 0.35f + 0.3f * (float)(30 - e->charge) / 30.0f });
        arc_draw_mesh_styled(m_enemy[e->kind], arc_m4_trs_y(at(e->p, bob), yaw, sc),
                             &(arc_mesh_style){ .whiten = e->flash > 0 ? 0.85f : 0, .tint = e->kind == E_BOSS ? 0xFF6060 : 0,
                                                .emissive = e->charge > 0 ? 0.6f : 0 });
    }
    for (int i = 0; i < MAX_BULLETS; i++) if (G.b[i].on) {
        const Bullet *b = &G.b[i];
        arc_draw_mesh_ex(m_bullet, arc_m4_trs_y(at(b->p, 0.6f), arc_yaw_to(b->v.x, b->v.y), arc_v3_make(0.16f, 0.16f, 0.55f)), 0xFFE060, 3);
    }
    for (int i = 0; i < MAX_CRYSTALS; i++) if (G.c[i].on) {
        const Crystal *c = &G.c[i]; uint32_t rgb = POWERS[c->kind].rgb;
        if (c->age > 480 && (c->age / 5) % 2) continue;   /* blink before it goes */
        float t = (float)c->age;
        arc_draw_mesh_styled(m_crystal, arc_m4_trs_y(at(c->p, 0.15f + 0.1f * sinf(t * 0.1f)), t * 0.05f, arc_v3_make(1, 1, 1)),
                             &(arc_mesh_style){ .tint = rgb, .recolour = 0.8f, .emissive = 0.5f });
        arc_light_point(at(c->p, 0.8f), rgb, 1.5f, 3);
        arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_RING, .x = c->p.x, .z = c->p.y, .size = 1.0f, .fill_rgb = rgb, .fill_alpha = 0.6f, .pulse = 2, .pulse_amount = 0.6f });
    }
    if (G.cooldown > 0 && G.recoil > 2) arc_light_point(at(G.pos, 1.0f), 0xFFC060, 2.5f, 4);   /* muzzle light */
    arc_draw_particles();

    /* HUD */
    float W = (float)arc_width(), H = (float)arc_height();
    arc_draw_textf(12, 10, 3, 0xFFFFFF, "SCORE %d", (int)(G.shown_score + 0.5f));
    arc_draw_textf(12, 40, 2, 0xB0B8C8, "WAVE %d   LIVES %d   BEST %d", G.wave, G.lives, G.best);
    if (G.combo > 1) {
        float pulse = 1 + 0.3f * arc_clampf((float)G.combo_timer / (*COMBO_TIME * 60.0f) - 0.7f, 0, 1) * 3;
        arc_draw_text_ex(W - 14, 12, 3 * pulse, 0xFFE060, ARC_TEXT_RIGHT | ARC_TEXT_OUTLINE, arc_fmt("COMBO x%d", G.combo < 8 ? G.combo : 8));
        arc_draw_rect(W - 174, 50, 160 * (float)G.combo_timer / (*COMBO_TIME * 60.0f), 4, 0xFFE060);
    }
    if (G.power != P_NONE)
        arc_draw_text_ex(W - 14, H - 34, 2, POWERS[G.power].rgb, ARC_TEXT_RIGHT | ARC_TEXT_OUTLINE,
                         G.power == P_SHIELD ? "SHIELD" : arc_fmt("%s %d", G.power == P_SPREAD ? "SPREAD" : "RAPID", G.power_ticks / 60 + 1));
    if (G.hurt_flash > 0) arc_draw_rect(0, 0, W, H, arc_rgba(0xFF2020, 0.35f * (float)G.hurt_flash / 20.0f));
    if (G.phase == PHASE_BETWEEN || (G.phase == PHASE_PLAY && G.banner < 60)) {
        float t = arc_clampf((float)G.banner / 20.0f, 0, 1), sc = 4.0f * arc_ease_out_back(t);
        bool boss = G.phase == PHASE_BETWEEN ? (G.wave + 1) % 5 == 0 : G.wave % 5 == 0;
        int n = G.phase == PHASE_BETWEEN ? G.wave + 1 : G.wave;
        if (sc > 0.1f) arc_draw_text_ex(W / 2, H / 3, sc, boss ? 0xFF6060 : 0xFFE060, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE,
                                        boss ? arc_fmt("WAVE %d - BOSS!", n) : arc_fmt("WAVE %d", n));
    }
    if (G.phase == PHASE_OVER) {
        arc_draw_rect(0, 0, W, H, arc_rgba(0x000000, 0.45f));
        arc_draw_text_ex(W / 2, H / 3, 5, 0xFF6060, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE, "GAME OVER - R to restart");
        arc_draw_text_ex(W / 2, H / 3 + 60, 3, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE,
                         G.score >= G.best && G.score > 0 ? arc_fmt("NEW BEST %d", G.score) : arc_fmt("SCORE %d   BEST %d", G.score, G.best));
    }
}
static void dump(arc_json *j) {
    static const char *ph[] = { "play", "between", "dying", "over" };
    arc_json_str_k(j, "phase", ph[G.phase]); arc_json_num_k(j, "wave", G.wave); arc_json_num_k(j, "score", G.score);
    arc_json_num_k(j, "best", G.best); arc_json_num_k(j, "combo", G.combo);
    arc_json_num_k(j, "lives", G.lives); arc_json_num_k(j, "kills", G.kills);
    arc_json_num_k(j, "x", G.pos.x); arc_json_num_k(j, "z", G.pos.y); arc_json_num_k(j, "aim_deg", G.aim * 57.29578f);
    arc_json_str_k(j, "power", POWERS[G.power].name); arc_json_bool_k(j, "shield", G.shield);
    arc_json_num_k(j, "enemies", count_enemies()); arc_json_num_k(j, "drones", count_kind(E_DRONE));
    arc_json_num_k(j, "beetles", count_kind(E_BEETLE)); arc_json_num_k(j, "bosses", count_kind(E_BOSS));
    arc_json_num_k(j, "bullets", count_bullets()); arc_json_num_k(j, "crystals", count_crystals());
    arc_json_num_k(j, "crates", count_crates()); arc_json_num_k(j, "hitstop", G.hitstop); arc_json_bool_k(j, "god", G.god);
}
static int kind_of(const char *s) { for (int k = 0; k < E_KINDS; k++) if (!strcmp(s, KINDS[k].name)) return k; return E_DRONE; }
static int power_of(const char *s) { for (int k = 1; k < P_KINDS; k++) if (!strcmp(s, POWERS[k].name)) return k; return P_SPREAD; }
static void clear_enemies(void) { for (int k = 0; k < MAX_ENEMIES; k++) G.e[k].on = false; }
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "aim") && argc >= 1) { G.aim = (float)atof(argv[0]) / 57.29578f; return true; }
    if (!strcmp(name, "fire")) { fire(); return true; }
    if (!strcmp(name, "wave") && argc >= 1) { clear_enemies(); start_wave(atoi(argv[0])); return true; }
    if (!strcmp(name, "spawn") && argc >= 1) { for (int n = atoi(argv[0]); n > 0; n--) spawn_enemy(argc >= 2 ? kind_of(argv[1]) : E_DRONE); return true; }
    if (!strcmp(name, "spawn_at") && argc >= 2) {   /* one enemy at (x, z), awake at once: for tests */
        add_enemy(argc >= 3 ? kind_of(argv[2]) : E_DRONE, arc_v2_make((float)atof(argv[0]), (float)atof(argv[1])));
        for (int k = MAX_ENEMIES - 1; k >= 0; k--) if (G.e[k].on && G.e[k].age == 0) { G.e[k].age = 31; break; }
        if (G.phase == PHASE_BETWEEN) G.phase = PHASE_PLAY;
        return true;
    }
    if (!strcmp(name, "crystal") && argc >= 2) { drop_crystal(arc_v2_make((float)atof(argv[0]), (float)atof(argv[1])), argc >= 3 ? power_of(argv[2]) : P_SPREAD); return true; }
    if (!strcmp(name, "lives") && argc >= 1) { G.lives = atoi(argv[0]); return true; }
    if (!strcmp(name, "clear")) { clear_enemies(); return true; }
    if (!strcmp(name, "god")) { G.god = !G.god; return true; }
    if (!strcmp(name, "restart")) { reset(); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t size) { if (size != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "{title}", .width = 960, .height = 640, .clear_color = 0x0C0F16,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
