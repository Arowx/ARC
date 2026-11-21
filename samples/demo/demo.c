/* SWARM — the ARC sample game. Survive waves of drones in an arena.
 *   Move: WASD / arrows   Fire: SPACE (or hold left mouse)   Restart: R   Camera: C
 *   Agents: try  --headless --cmd "step 120; press D; step 30; shot a.png; dump"
 *   Benchmark: --bench 20000  (draws N spinning meshes; see `stats`)
 */
#include "../../include/arc.h"
#include <stdio.h>
#include <stdlib.h>

#define MAX_ENEMIES   4096
#define MAX_BULLETS   512
#define MAX_PARTICLES 16384
#define ARENA 20.0f

typedef struct { arc_v3 pos, vel; float hp, phase; int kind; } enemy;
typedef struct { arc_v3 pos, vel; float life; } bullet;
typedef struct { arc_v3 pos, vel; float life, size; uint32_t color; } particle;

static struct {
    arc_v3 pos, prev_pos; float yaw, prev_yaw; arc_v3 aim;
    int lives, score, wave; float invuln, fire_cd, spawn_cd; int to_spawn; bool dead; float dead_t;
    arc_pool enemies, bullets, particles;
    arc_mesh floor_mesh, ship, drone, heavy, shard, bolt;
    arc_sound s_shoot, s_boom, s_hit;
    int cam_mode; bool god;
    int bench; float bench_t;
    int demo_setting; bool saved;   /* 0.10.12: `set demo_setting N` (a game command named like ARC's), save_test */
} G;

static float v_speed, v_enemy_speed, v_fire_rate, v_bullet_speed, v_spawn_rate;
static float v_line_test;   /* 0.10.11: 1 draws a few thick, glowing vector lines over the arena */
static float v_nan_test;    /* 0.10.12: 1 turns bloom on and draws a cube whose glow is NaN (post effects must not spread it) */
static float v_text_test, v_save_test;   /* 0.10.12: a centred DOCK button label; save once to --save-dir */

static void reset(void) {
    for (int i = 0; i < G.enemies.cap; i++) if (arc_pool_alive(&G.enemies, i)) arc_pool_remove(&G.enemies, arc_pool_handle_at(&G.enemies, i));
    for (int i = 0; i < G.bullets.cap; i++) if (arc_pool_alive(&G.bullets, i)) arc_pool_remove(&G.bullets, arc_pool_handle_at(&G.bullets, i));
    G.pos = G.prev_pos = arc_v3_make(0, 0, 0); G.yaw = G.prev_yaw = 0; G.aim = arc_v3_make(0, 0, -1);
    G.lives = 3; G.score = 0; G.wave = 0; G.invuln = 2; G.to_spawn = 0; G.spawn_cd = 1; G.dead = false;
}

static void spawn_enemy(void) {
    arc_handle h = arc_pool_add(&G.enemies); enemy *e = (enemy *)arc_pool_get(&G.enemies, h); if (!e) return;
    float a = arc_rand_rangef(0, ARC_TAU), r = ARENA * 0.95f;
    e->pos = arc_v3_make(cosf(a) * r, 0.5f, sinf(a) * r);
    e->kind = (G.wave >= 3 && arc_rand_range(0, 5) == 0) ? 1 : 0;
    e->hp = e->kind ? 4.0f : 1.0f; e->phase = arc_rand_rangef(0, ARC_TAU);
}
static void burst(arc_v3 p, uint32_t color, int n, float speed) {
    for (int i = 0; i < n; i++) {
        arc_handle h = arc_pool_add(&G.particles); particle *q = (particle *)arc_pool_get(&G.particles, h); if (!q) return;
        arc_v3 d = arc_v3_norm(arc_v3_make(arc_rand_rangef(-1, 1), arc_rand_rangef(-0.2f, 1), arc_rand_rangef(-1, 1)));
        q->pos = p; q->vel = arc_v3_scale(d, arc_rand_rangef(0.3f, 1.0f) * speed);
        q->life = arc_rand_rangef(0.4f, 1.0f); q->size = arc_rand_rangef(0.08f, 0.2f); q->color = color;
    }
}

static void init(void) {
    arc_var("player.speed", &v_speed, 9.0f);
    arc_var("enemy.speed", &v_enemy_speed, 3.2f);
    arc_var("fire.rate", &v_fire_rate, 9.0f);
    arc_var("bullet.speed", &v_bullet_speed, 30.0f);
    arc_var("spawn.rate", &v_spawn_rate, 6.0f);
    arc_var("line_test", &v_line_test, 0.0f);
    arc_var("nan_test", &v_nan_test, 0.0f);
    arc_var("text_test", &v_text_test, 0.0f);
    arc_var("save_test", &v_save_test, 0.0f);
    arc_pool_make(&G.enemies, arc_perm(), enemy, MAX_ENEMIES);
    arc_pool_make(&G.bullets, arc_perm(), bullet, MAX_BULLETS);
    arc_pool_make(&G.particles, arc_perm(), particle, MAX_PARTICLES);
    G.floor_mesh = arc_mesh_plane(0x1A2230, 20, 0x202A3A);
    G.ship = arc_mesh_cone(0x40E0C0, 3);
    G.drone = arc_mesh_sphere(0xE85A3A, 8);
    G.heavy = arc_mesh_cube(0xF0B030);
    G.shard = arc_mesh_cube(0xFFFFFF);
    G.bolt = arc_mesh_sphere(0xFFFFFF, 6);
    arc_sfx shoot = { ARC_WAVE_SQUARE, 880, 220, 0.002f, 0.02f, 0.06f, 0.25f, 0.3f, 0, 0 };
    arc_sfx boom = { ARC_WAVE_NOISE, 900, 60, 0.002f, 0.05f, 0.35f, 0.5f, 0, 0, 0 };
    arc_sfx hit = { ARC_WAVE_SAW, 300, 60, 0.002f, 0.1f, 0.3f, 0.5f, 0, 0, 0 };
    G.s_shoot = arc_sound_make(&shoot); G.s_boom = arc_sound_make(&boom); G.s_hit = arc_sound_make(&hit);
    const char *b = arc_arg_value("bench"); if (b) G.bench = atoi(b);
    arc_set_fog(0x0B0E14, 25, 60);
    reset();
}

static void tick(void) {
    if (v_save_test > 0.5f && !G.saved) { int v = 42; G.saved = arc_save("demo_test", &v, sizeof v); }   /* 0.10.12 */
    float dt = arc_dt();
    if (G.bench) { G.bench_t += dt; return; }
    if (arc_key_pressed(ARC_KEY_C)) G.cam_mode ^= 1;
    if (arc_key_pressed(ARC_KEY_R)) reset();
    G.prev_pos = G.pos; G.prev_yaw = G.yaw;

    ARC_PROF("player") {
        if (G.dead) { G.dead_t += dt; }
        else {
            arc_v3 mv = arc_v3_make(0, 0, 0);
            if (arc_key_down(ARC_KEY_A) || arc_key_down(ARC_KEY_LEFT)) mv.x -= 1;
            if (arc_key_down(ARC_KEY_D) || arc_key_down(ARC_KEY_RIGHT)) mv.x += 1;
            if (arc_key_down(ARC_KEY_W) || arc_key_down(ARC_KEY_UP)) mv.z -= 1;
            if (arc_key_down(ARC_KEY_S) || arc_key_down(ARC_KEY_DOWN)) mv.z += 1;
            if (arc_v3_len(mv) > 0) {
                mv = arc_v3_norm(mv); G.aim = mv;
                G.pos = arc_v3_madd(G.pos, mv, v_speed * dt);
                G.pos.x = arc_clampf(G.pos.x, -ARENA + 0.5f, ARENA - 0.5f); G.pos.z = arc_clampf(G.pos.z, -ARENA + 0.5f, ARENA - 0.5f);
            }
            float want = atan2f(G.aim.x, G.aim.z);
            G.yaw += arc_wrap_angle(want - G.yaw) * arc_minf(1, dt * 14);
            G.fire_cd -= dt; G.invuln -= dt;
            if ((arc_key_down(ARC_KEY_SPACE) || arc_key_down(ARC_MOUSE_LEFT)) && G.fire_cd <= 0) {
                G.fire_cd = 1.0f / v_fire_rate;
                arc_handle h = arc_pool_add(&G.bullets); bullet *b = (bullet *)arc_pool_get(&G.bullets, h);
                if (b) { b->pos = arc_v3_madd(arc_v3_make(G.pos.x, 0.5f, G.pos.z), G.aim, 0.7f); b->vel = arc_v3_scale(G.aim, v_bullet_speed); b->life = 1.2f; arc_play(G.s_shoot, 0.35f, G.pos.x / ARENA); }
            }
        }
    }
    ARC_PROF("waves") {
        if (G.to_spawn == 0 && G.enemies.count == 0 && !G.dead) { G.wave++; G.to_spawn = 6 + G.wave * 6; }
        G.spawn_cd -= dt;
        while (G.to_spawn > 0 && G.spawn_cd <= 0) { spawn_enemy(); G.to_spawn--; G.spawn_cd += 1.0f / (v_spawn_rate + (float)G.wave); }
    }
    ARC_PROF("enemies") {
        for (int i = 0; i < G.enemies.cap; i++) {
            if (!arc_pool_alive(&G.enemies, i)) continue;
            enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
            arc_v3 to = arc_v3_sub(G.pos, e->pos); to.y = 0;
            float d = arc_v3_len(to);
            float sp = v_enemy_speed * (e->kind ? 0.6f : 1.0f) * (1.0f + (float)G.wave * 0.04f);
            arc_v3 want = arc_v3_scale(arc_v3_norm(to), sp);
            e->phase += dt * 3;
            want = arc_v3_add(want, arc_v3_make(cosf(e->phase) * 0.8f, 0, sinf(e->phase) * 0.8f));
            e->vel = arc_v3_lerp(e->vel, want, arc_minf(1, dt * 3));
            e->pos = arc_v3_madd(e->pos, e->vel, dt);
            if (!G.dead && d < (e->kind ? 1.0f : 0.8f) && G.invuln <= 0 && !G.god) {
                G.lives--; G.invuln = 2; burst(G.pos, 0x40E0C0, 60, 8); arc_play(G.s_hit, 0.8f, 0);
                arc_pool_remove(&G.enemies, arc_pool_handle_at(&G.enemies, i));
                if (G.lives <= 0) { G.dead = true; G.dead_t = 0; burst(G.pos, 0xFFFFFF, 200, 12); }
            }
        }
    }
    ARC_PROF("bullets") {
        for (int i = 0; i < G.bullets.cap; i++) {
            if (!arc_pool_alive(&G.bullets, i)) continue;
            bullet *b = (bullet *)arc_pool_at(&G.bullets, i);
            b->pos = arc_v3_madd(b->pos, b->vel, dt); b->life -= dt;
            bool gone = b->life <= 0 || fabsf(b->pos.x) > ARENA || fabsf(b->pos.z) > ARENA;
            for (int k = 0; k < G.enemies.cap && !gone; k++) {
                if (!arc_pool_alive(&G.enemies, k)) continue;
                enemy *e = (enemy *)arc_pool_at(&G.enemies, k);
                float r = e->kind ? 0.8f : 0.55f;
                float dx = e->pos.x - b->pos.x, dz = e->pos.z - b->pos.z;
                if (dx * dx + dz * dz < r * r) {
                    gone = true; e->hp -= 1;
                    if (e->hp <= 0) {
                        G.score += e->kind ? 50 : 10;
                        burst(e->pos, e->kind ? 0xF0B030 : 0xE85A3A, e->kind ? 50 : 24, 7);
                        arc_play(G.s_boom, 0.5f, e->pos.x / ARENA);
                        arc_pool_remove(&G.enemies, arc_pool_handle_at(&G.enemies, k));
                    } else burst(b->pos, 0xFFFFFF, 6, 4);
                }
            }
            if (gone) arc_pool_remove(&G.bullets, arc_pool_handle_at(&G.bullets, i));
        }
    }
    ARC_PROF("particles") {
        for (int i = 0; i < G.particles.cap; i++) {
            if (!arc_pool_alive(&G.particles, i)) continue;
            particle *p = (particle *)arc_pool_at(&G.particles, i);
            p->vel.y -= 14 * dt; p->pos = arc_v3_madd(p->pos, p->vel, dt);
            if (p->pos.y < 0) { p->pos.y = 0; p->vel.y *= -0.4f; p->vel.x *= 0.7f; p->vel.z *= 0.7f; }
            p->life -= dt;
            if (p->life <= 0) arc_pool_remove(&G.particles, arc_pool_handle_at(&G.particles, i));
        }
    }
}

static void draw_bench(void) {
    float t = G.bench_t;
    arc_camera cam = { { sinf(t * 0.2f) * 60, 40, cosf(t * 0.2f) * 60 }, { 0, 0, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.5f, 400 };
    arc_set_camera(&cam);
    int side = (int)ceilf(sqrtf((float)G.bench));
    for (int i = 0; i < G.bench; i++) {
        float x = ((float)(i % side) - (float)side * 0.5f) * 1.2f, z = ((float)(i / side) - (float)side * 0.5f) * 1.2f;
        float y = sinf(t * 2 + x * 0.3f + z * 0.2f) * 1.5f;
        arc_draw_mesh(i & 1 ? G.heavy : G.drone, arc_m4_trs_y(arc_v3_make(x, y, z), t + (float)i * 0.1f, arc_v3_make(0.8f, 0.8f, 0.8f)), 0xFFFFFF);
    }
    arc_gfx_stats st = arc_stats();
    arc_draw_rect(8, 8, 420, 52, arc_rgba(0x000000, 0.6f));
    arc_draw_textf(16, 16, 2, 0xFFFFFF, "BENCH %d meshes", G.bench);
    arc_draw_textf(16, 40, 1, 0x9FB0C8, "%s  %.2f ms/frame  %d draw calls  %d tris", st.backend, st.avg_frame_ms, st.draw_calls, st.triangles);
}

static void draw(float alpha) {
    if (G.bench) { draw_bench(); return; }
    arc_v3 p = arc_v3_lerp(G.prev_pos, G.pos, alpha);
    arc_camera cam;
    if (G.cam_mode == 0) cam = (arc_camera){ { p.x, 22, p.z + 14 }, { p.x, 0, p.z }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 200 };
    else cam = (arc_camera){ { 0, 46, 0.01f }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 44, 0.1f, 200 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1, 0.25f), 0xFFF4E0, 0x3A4458);

    arc_draw_mesh(G.floor_mesh, arc_m4_scale(arc_v3_make(ARENA * 2, 1, ARENA * 2)), 0xFFFFFF);
    arc_draw_box_lines(arc_v3_make(-ARENA, 0, -ARENA), arc_v3_make(ARENA, 1.0f, ARENA), 0x2A6A8A);
    if (!G.dead && (G.invuln <= 0 || fmod(arc_time() * 10, 2) < 1)) {
        float yaw = G.prev_yaw + arc_wrap_angle(G.yaw - G.prev_yaw) * alpha;
        arc_m4 m = arc_m4_mul(arc_m4_trs_y(arc_v3_make(p.x, 0.5f, p.z), yaw, arc_v3_make(0.9f, 1.4f, 0.9f)), arc_m4_rotate_x(ARC_PI * 0.5f));
        arc_draw_mesh_ex(G.ship, m, 0xFFFFFF, 0.15f);
    }
    for (int i = 0; i < G.enemies.cap; i++) {
        if (!arc_pool_alive(&G.enemies, i)) continue;
        enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
        arc_v3 ep = arc_v3_madd(e->pos, e->vel, (alpha - 1) * arc_dt());
        ep.y = 0.5f + sinf(e->phase * 2) * 0.12f;
        if (e->kind) arc_draw_mesh_ex(G.heavy, arc_m4_trs_y(ep, e->phase, arc_v3_make(1.2f, 1.2f, 1.2f)), 0xFFFFFF, 0.1f);
        else arc_draw_mesh_ex(G.drone, arc_m4_trs_y(ep, 0, arc_v3_make(0.9f, 0.9f, 0.9f)), 0xFFFFFF, 0.1f);
    }
    for (int i = 0; i < G.bullets.cap; i++) {
        if (!arc_pool_alive(&G.bullets, i)) continue;
        bullet *b = (bullet *)arc_pool_at(&G.bullets, i);
        arc_draw_mesh_ex(G.bolt, arc_m4_trs_y(arc_v3_madd(b->pos, b->vel, (alpha - 1) * arc_dt()), 0, arc_v3_make(0.3f, 0.3f, 0.3f)), 0x9FFFF0, 1.0f);
    }
    for (int i = 0; i < G.particles.cap; i++) {
        if (!arc_pool_alive(&G.particles, i)) continue;
        particle *q = (particle *)arc_pool_at(&G.particles, i);
        float s = q->size * arc_clampf(q->life * 2, 0, 1);
        arc_draw_mesh_ex(G.shard, arc_m4_trs_y(q->pos, q->life * 8, arc_v3_make(s, s, s)), q->color, 0.8f);
    }
    if (v_nan_test > 0.5f) {   /* 0.10.12: one bad pixel source; with the guard it stays a dark patch, not a glowing blotch */
        static bool posted; if (!posted) { posted = true; arc_set_post(&(arc_post){ .bloom = 0.6f, .bloom_threshold = 1.0f, .exposure = 1.0f }); }
        volatile float zero = 0.0f;
        arc_draw_mesh_ex(G.shard, arc_m4_trs_y(arc_v3_make(p.x + 3, 1, p.z), 0, arc_v3_make(2, 2, 2)), 0xFFFFFF, zero / zero);
    }
    if (v_line_test > 0.5f) {   /* 0.10.11: a vector-style wireframe, 4 pixels wide, glowing */
        static const arc_v3 ship[] = { { 0, 0, -1.5f }, { 1, 0, 1 }, { 1, 0, 1 }, { 0, 0, 0.5f }, { 0, 0, 0.5f }, { -1, 0, 1 }, { -1, 0, 1 }, { 0, 0, -1.5f } };
        arc_set_line_width(4.0f); arc_set_line_glow(2.5f); arc_set_line_cap(ARC_LINE_CAP_ROUND);
        arc_draw_lines(ship, 8, arc_m4_trs_y(arc_v3_make(p.x - 4, 1.5f, p.z), 0.4f, arc_v3_make(2, 2, 2)), 0x40FFC0);
        arc_draw_line(arc_v3_make(p.x - 8, 0.2f, p.z - 3), arc_v3_make(p.x + 8, 0.2f, p.z - 3), 0xFF60C0);
        arc_set_line_width(1.0f); arc_set_line_glow(1.0f); arc_set_line_cap(ARC_LINE_CAP_SQUARE);
        arc_draw_line2d(20, 60, 300, 90, 5.0f, 0x40FFC0);
    }
    if (v_text_test > 0.5f) {   /* 0.10.12: a 63-pixel button at x = 384.5 with its label centred at the largest size that fits */
        float bx = 384.5f, by = 300, bw = 63, bh = 28;
        arc_draw_rect(bx, by, bw, bh, 0x203040);
        arc_draw_text_ex(bx + bw * 0.5f, by + bh * 0.5f, arc_text_fit_scale("DOCK", bw - 4, 3), 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE, "DOCK");
    }
    /* HUD */
    arc_gfx_stats st = arc_stats();
    arc_draw_rect(0, 0, (float)arc_width(), 36, arc_rgba(0x000000, 0.55f));
    arc_draw_textf(12, 10, 2, 0x40E0C0, "SCORE %06d", G.score);
    arc_draw_textf(260, 10, 2, 0xF0B030, "WAVE %d", G.wave);
    arc_draw_textf(420, 10, 2, 0xE85A3A, "LIVES %d", G.lives > 0 ? G.lives : 0);
    arc_draw_textf((float)arc_width() - 300, 14, 1, 0x7F8CA0, "%s %.1fms %d obj %d calls", st.backend, st.avg_frame_ms, st.instances, st.draw_calls);
    if (G.dead) {
        const char *t = "GAME OVER - PRESS R"; float w = arc_text_width(4, t);
        arc_draw_text(((float)arc_width() - w) * 0.5f, (float)arc_height() * 0.45f, 4, 0xFFFFFF, t);
    }
    arc_draw_text(12, (float)arc_height() - 20, 1, 0x7F8CA0, "WASD/ARROWS MOVE   SPACE FIRE   C CAMERA   R RESTART");
}

static void dump(arc_json *j) {
    arc_json_v3_k(j, "player", G.pos);
    arc_json_num_k(j, "player_yaw", G.yaw);
    arc_json_int_k(j, "lives", G.lives); arc_json_int_k(j, "score", G.score); arc_json_int_k(j, "wave", G.wave);
    arc_json_bool_k(j, "dead", G.dead);
    arc_json_int_k(j, "demo_setting", G.demo_setting); arc_json_bool_k(j, "saved", G.saved);
    arc_json_int_k(j, "enemies", G.enemies.count); arc_json_int_k(j, "bullets", G.bullets.count); arc_json_int_k(j, "particles", G.particles.count);
    arc_json_arr_k(j, "nearest_enemies");
    for (int n = 0, i = 0; i < G.enemies.cap && n < 8; i++) {
        if (!arc_pool_alive(&G.enemies, i)) continue;
        enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
        arc_json_obj(j); arc_json_v3_k(j, "pos", e->pos); arc_json_num_k(j, "dist", arc_v3_dist(e->pos, G.pos)); arc_json_int_k(j, "kind", e->kind); arc_json_end(j); n++;
    }
    arc_json_end(j);
}

/* snapshots: lets agents `seek` backwards, `rewind`, `state_save` / `state_load` (and `state_check` tests it) */
static size_t save_state(void *buf, size_t cap) {
    arc_writer w = { (uint8_t *)buf, cap, 0 };
    arc_write(&w, &G, sizeof G);                      /* plain values (the pool headers' pointers are not used on load) */
    arc_write_pool(&w, &G.enemies); arc_write_pool(&w, &G.bullets); arc_write_pool(&w, &G.particles);
    return w.len;
}
static bool load_state(const void *buf, size_t size) {
    arc_reader r = { (const uint8_t *)buf, size, 0, false };
    arc_pool pools[3] = { G.enemies, G.bullets, G.particles };   /* the pools' memory belongs to this run: keep it */
    if (!arc_read(&r, &G, sizeof G)) return false;
    G.enemies = pools[0]; G.bullets = pools[1]; G.particles = pools[2];
    arc_read_pool(&r, &G.enemies); arc_read_pool(&r, &G.bullets); arc_read_pool(&r, &G.particles);
    return !r.fail;
}

static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "spawn")) { int n = argc ? atoi(argv[0]) : 1; while (n-- > 0) spawn_enemy(); return true; }
    if (!strcmp(name, "god")) { G.god = argc ? atoi(argv[0]) != 0 : !G.god; return true; }
    if (!strcmp(name, "reset")) { reset(); return true; }
    if (!strcmp(name, "set") && argc >= 2 && !strcmp(argv[0], "demo_setting")) { G.demo_setting = atoi(argv[1]); return true; }   /* 0.10.12: shadowed by ARC's set */
    return false;
}

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "SWARM - ARC sample", .width = 1280, .height = 720, .clear_color = 0x0B0E14,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
