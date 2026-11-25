/* TOY BOX BLITZ — the ARC sample game (it was SWARM until 0.10.18). You're a wind-up tin robot defending a child's
 * bedroom floor from swarms of clockwork bugs: ants that swarm, beetles that soak up 4 hits, fleas that hop.
 *   Move: WASD / arrows (or the left stick), relative to the camera.  Fire: SPACE, hold the left mouse button, PAD A.
 *   Mouse look: click to capture the pointer; the mouse turns the camera (and the right stick); ESC lets go.
 *   Camera: C switches to the top-down view.  Restart: R.
 *   Agents: try  --headless --cmd "step 120; press D; step 30; shot a.png; dump"
 *   Benchmark: --bench 20000  (draws N spinning meshes; see `stats`)
 * Art, sound and music are made with Ludo and live in samples/demo/assets (assets/manifest.json says how); without
 * them (or with --no-assets) the game draws code-made shapes instead, so ARC's own tests never depend on the art.
 * The test tunables (line_test, nan_test, text_test, ...) are ARC's engine checks: line_test and nan_test show the
 * old plain arena and camera, so their pictures stay comparable. */
#include "../../include/arc.h"
#include <stdio.h>
#include <stdlib.h>

#define MAX_ENEMIES   4096
#define MAX_BULLETS   512
#define MAX_PARTICLES 16384
#define ARENA 20.0f
#define MAX_COLLIDERS 64

enum { K_ANT, K_BEETLE, K_FLEA, K_KINDS };
typedef struct { arc_v3 pos, vel; float hp, phase; int kind; float age, flash, hop; arc_v3 goal; float repath; } enemy;   /* goal: the next corner of its path (0.10.19) */
typedef struct { arc_v3 pos, vel; float life; } bullet;
typedef struct { arc_v3 pos, vel; float life, size; uint32_t color; float spin; int flat; } particle;   /* flat: confetti */

static struct {
    arc_v3 pos, prev_pos; float yaw, prev_yaw; arc_v3 aim;
    int lives, score, wave; float invuln, fire_cd, spawn_cd; int to_spawn; bool dead; float dead_t;
    arc_pool enemies, bullets, particles;
    int cam_mode; bool god;
    int bench; float bench_t;
    int demo_setting; bool saved;   /* 0.10.12: `set demo_setting N` (a game command named like ARC's), save_test */
    /* 0.10.18 TOY BOX BLITZ: the camera, mouse look and juice (all in the state, so rewinds and replays show the same) */
    float cam_yaw, cam_pitch, prev_cam_yaw, prev_cam_pitch; bool captured;
    float shake, hitstop, recoil, hurt_flash, hit_t, anim_t, run_w, key_spin, banner_t, title_t, shown_score;
    int combo, best_combo, kills; float combo_t;
} G;

static float v_enemy_nav;   /* 0.10.19: 1 = bugs find their way round the cover on the navmesh, 0 = straight at the robot */
static float v_speed, v_enemy_speed, v_fire_rate, v_bullet_speed, v_spawn_rate, v_sens, v_invert, v_combo_time;
static float v_line_test;   /* 0.10.11: 1 draws a few thick, glowing vector lines over the arena */
static float v_nan_test;    /* 0.10.12: 1 turns bloom on and draws a cube whose glow is NaN (post effects must not spread it) */
static float v_text_test, v_save_test;   /* 0.10.12: a centred DOCK button label; save once to --save-dir */
static float v_offset2d_test;   /* 0.10.13: a panel and a label laid out from (0, 0) inside the safe area */
static float v_lines2d_test;    /* 0.10.13: that many glowing 2D segments in one arc_draw_lines2d call */
static float v_music_loop_test; /* 0.10.13: play tests/loop_test.qoa (8 s, a step up in pitch each second) looping 2..6 s */
static float v_scroll_panel_test;   /* 0.10.17: a 200-pixel-high scroll panel of 30 buttons "Row 1".."Row 30" (UI at half size) */
static struct { float scroll; int clicks; char last[16]; } g_scroll;
static arc_music g_loop_music;

/* looks (not game state) */
static bool g_art;                                   /* the Ludo assets loaded */
static arc_mesh m_floor_old, m_ship, m_drone, m_heavy, m_shard, m_bolt;   /* code-made: the fallback, --bench, line/nan tests */
static arc_mesh m_floor, m_player, m_enemy[K_KINDS], m_block, m_crayon, m_drum, m_key, m_cork, m_flea_old;
static arc_anim a_player; static int clip_idle = -1, clip_run = -1, clip_hit = -1, clip_death = -1;
static arc_sound s_shoot, s_hit, s_kill, s_pickup, s_wave, s_hurt;
static arc_music g_music;
static arc_font g_font;
static const uint32_t CONFETTI[] = { 0xFF4040, 0xFFD030, 0x40C0FF, 0x60E060, 0xFF70D0, 0xFFFFFF };
static const int KIND_SCORE[K_KINDS] = { 10, 50, 25 };

/* the arena's cover: alphabet blocks, crayons lying down, a toy drum (circles for collisions) */
typedef struct { int kind; float x, z, yaw, scale; } prop;   /* kind 0 block, 1 crayon, 2 drum */
static const prop PROPS[] = {
    { 0, -6, -5, 0.3f, 1.4f }, { 0, 6.5f, -6, 1.1f, 1.3f }, { 0, -9, 7, 2.0f, 1.5f }, { 0, 10, 5, 0.7f, 1.3f },
    { 0, 0, 10.5f, 1.6f, 1.4f }, { 0, -13, -12, 0.2f, 1.6f }, { 0, 13, -11, 2.6f, 1.4f }, { 0, 3, -13.5f, 0.9f, 1.3f },
    { 1, -4.5f, 4.5f, 0.6f, 1.2f }, { 1, 8.5f, -1, 2.0f, 1.2f }, { 1, -13, 0.5f, 1.2f, 1.2f }, { 1, 5, 14, 2.8f, 1.2f },
    { 2, 4, -8.5f, 0, 1.0f }, { 2, 13, 13, 0, 1.1f }, { 2, -14, 13.5f, 0, 1.0f },
};
#define NPROPS ((int)(sizeof PROPS / sizeof PROPS[0]))
static struct { float x, z, r, h; } g_col[MAX_COLLIDERS]; static int g_ncol;

static float sens(void) { return v_sens > 0 ? v_sens : 1.0f; }
static bool classic_view(void) { return v_line_test > 0.5f || v_nan_test > 0.5f; }   /* the engine checks' plain arena */
static arc_v3 cam_forward(float yaw) { return arc_v3_make(-sinf(yaw), 0, -cosf(yaw)); }
static arc_v3 cam_right(float yaw) { return arc_v3_make(cosf(yaw), 0, -sinf(yaw)); }

static void reset(void) {
    for (int i = 0; i < G.enemies.cap; i++) if (arc_pool_alive(&G.enemies, i)) arc_pool_remove(&G.enemies, arc_pool_handle_at(&G.enemies, i));
    for (int i = 0; i < G.bullets.cap; i++) if (arc_pool_alive(&G.bullets, i)) arc_pool_remove(&G.bullets, arc_pool_handle_at(&G.bullets, i));
    G.pos = G.prev_pos = arc_v3_make(0, 0, 0); G.yaw = G.prev_yaw = 0; G.aim = arc_v3_make(0, 0, -1);
    G.lives = 3; G.score = 0; G.wave = 0; G.invuln = 2; G.to_spawn = 0; G.spawn_cd = 1; G.dead = false;
    G.cam_yaw = G.prev_cam_yaw = 0; G.cam_pitch = G.prev_cam_pitch = 0.5f;
    G.shake = G.hitstop = G.recoil = G.hurt_flash = G.hit_t = G.combo_t = 0; G.combo = 0; G.kills = 0;
    G.shown_score = 0; G.title_t = 0; G.banner_t = 99;
}

static void spawn_enemy(void) {
    arc_handle h = arc_pool_add(&G.enemies); enemy *e = (enemy *)arc_pool_get(&G.enemies, h); if (!e) return;
    float a = arc_rand_rangef(0, ARC_TAU), r = ARENA * 0.95f;
    e->pos = arc_v3_make(cosf(a) * r, 0.5f, sinf(a) * r);
    e->kind = (G.wave >= 3 && arc_rand_range(0, 5) == 0) ? K_BEETLE : K_ANT;
    if (e->kind == K_ANT && G.wave >= 2 && arc_rand_range(0, 4) == 0) e->kind = K_FLEA;
    e->hp = e->kind == K_BEETLE ? 4.0f : 1.0f; e->phase = arc_rand_rangef(0, ARC_TAU);
    e->vel = arc_v3_make(0, 0, 0); e->age = 0; e->flash = 0; e->hop = arc_rand_rangef(0.2f, 0.9f);
    e->goal = G.pos; e->repath = 0;
}
static void burst(arc_v3 p, uint32_t color, int n, float speed) {   /* sparks (one colour) */
    for (int i = 0; i < n; i++) {
        arc_handle h = arc_pool_add(&G.particles); particle *q = (particle *)arc_pool_get(&G.particles, h); if (!q) return;
        float dx = arc_rand_rangef(-1, 1), dy = arc_rand_rangef(-0.2f, 1), dz = arc_rand_rangef(-1, 1);
        arc_v3 d = arc_v3_norm(arc_v3_make(dx, dy, dz));
        float s = arc_rand_rangef(0.3f, 1.0f);
        q->pos = p; q->vel = arc_v3_scale(d, s * speed);
        q->life = arc_rand_rangef(0.4f, 1.0f); q->size = arc_rand_rangef(0.08f, 0.2f); q->color = color; q->spin = 8; q->flat = 0;
    }
}
static void confetti(arc_v3 p, int n) {   /* flat paper squares in toy colours that flutter down */
    for (int i = 0; i < n; i++) {
        arc_handle h = arc_pool_add(&G.particles); particle *q = (particle *)arc_pool_get(&G.particles, h); if (!q) return;
        float dx = arc_rand_rangef(-1, 1), dz = arc_rand_rangef(-1, 1), up = arc_rand_rangef(4, 8), out = arc_rand_rangef(1, 4);
        arc_v3 d = arc_v3_norm(arc_v3_make(dx, 0, dz));
        q->pos = arc_v3_make(p.x, p.y + 0.3f, p.z); q->vel = arc_v3_make(d.x * out, up, d.z * out);
        q->life = arc_rand_rangef(0.9f, 1.6f); q->size = arc_rand_rangef(0.12f, 0.22f);
        q->color = CONFETTI[arc_rand_range(0, 5)]; q->spin = arc_rand_rangef(4, 12); q->flat = 1;
    }
}
static void sound(arc_sound s, float vol, float x) { arc_play_var(s, vol, arc_clampf(x / ARENA, -1, 1) * 0.6f, 0.08f); }
static void kick(float amount) { G.shake = arc_minf(1.0f, G.shake + amount); }

/* circles: push a point out of the cover; true when it was inside one */
static bool push_out(arc_v3 *p, float radius) {
    bool hit = false;
    for (int i = 0; i < g_ncol; i++) {
        float dx = p->x - g_col[i].x, dz = p->z - g_col[i].z, r = g_col[i].r + radius, d2 = dx * dx + dz * dz;
        if (d2 < r * r) {
            float d = sqrtf(d2); hit = true;
            if (d < 1e-4f) { dx = 1; dz = 0; d = 1; }
            p->x = g_col[i].x + dx / d * r; p->z = g_col[i].z + dz / d * r;
        }
    }
    return hit;
}
static bool in_cover(arc_v3 p) {
    for (int i = 0; i < g_ncol; i++) {
        float dx = p.x - g_col[i].x, dz = p.z - g_col[i].z;
        if (dx * dx + dz * dz < g_col[i].r * g_col[i].r && p.y < g_col[i].h) return true;
    }
    return false;
}
/* 0.10.19 the navmesh: the arena floor as a grid of half-unit cells, the cells under the cover taken out. The bugs
   path round the cover on it (arc_navmesh_path keeps each bug's radius from the corners). */
static arc_navmesh g_nav; static bool g_nav_draw;
#define NAV_CELL 0.5f
#define NAV_N 80   /* 2 * ARENA / NAV_CELL */
static void build_nav(void) {
    static uint8_t cells[NAV_N * NAV_N]; arc_grid g = { NAV_N, NAV_N, cells };
    for (int y = 0; y < NAV_N; y++) for (int x = 0; x < NAV_N; x++) {
        float cx = -ARENA + ((float)x + 0.5f) * NAV_CELL, cz = -ARENA + ((float)y + 0.5f) * NAV_CELL; uint8_t v = 0;
        for (int i = 0; i < g_ncol && !v; i++) { float dx = cx - g_col[i].x, dz = cz - g_col[i].z, r = g_col[i].r + 0.1f; if (dx * dx + dz * dz < r * r) v = 1; }
        cells[y * NAV_N + x] = v;
    }
    g_nav = arc_navmesh_create(NULL);
    arc_navmesh_add_grid(g_nav, &g, arc_v3_make(-ARENA, 0, -ARENA), NAV_CELL, NULL);
    arc_navmesh_build(g_nav);
}
/* where a bug heads now: the robot itself when the way is clear, else the next corner of a path round the cover;
   asked again every 0.4 s or so (spread over the bugs) and when it reaches the corner */
static bool band_clear(arc_v3 from, arc_v3 to, float radius) {   /* a straight walk is clear for the bug's whole width */
    arc_v3 dir = arc_v3_sub(to, from); float len = arc_v3_len(dir);
    arc_v3 side = len > 1e-4f ? arc_v3_make(-dir.z / len * radius, 0, dir.x / len * radius) : arc_v3_make(0, 0, 0);
    return arc_navmesh_walkable_line(g_nav, from, to, NULL) && arc_navmesh_walkable_line(g_nav, arc_v3_add(from, side), arc_v3_add(to, side), NULL) &&
           arc_navmesh_walkable_line(g_nav, arc_v3_sub(from, side), arc_v3_sub(to, side), NULL);
}
static void steer_goal(enemy *e, int index, float radius, float dt) {
    if (v_enemy_nav < 0.5f || !g_nav.id) { e->goal = G.pos; return; }
    float gx = e->goal.x - e->pos.x, gz = e->goal.z - e->pos.z;
    e->repath -= dt;
    if (e->repath > 0 && gx * gx + gz * gz > 0.09f) return;
    e->repath = 0.35f + (float)(index % 8) * 0.02f;
    arc_v3 from = arc_v3_make(e->pos.x, 0, e->pos.z), to = arc_v3_make(G.pos.x, 0, G.pos.z);
    if (band_clear(from, to, radius)) { e->goal = G.pos; return; }
    arc_v3 pts[8]; int n = arc_navmesh_path(g_nav, from, to, radius, pts, 8); if (n > 8) n = 8;
    e->goal = n >= 2 ? pts[1] : G.pos;
    for (int k = n - 1; k >= 2; k--) if (band_clear(from, pts[k], radius * 0.75f)) { e->goal = pts[k]; break; }   /* skip corners it can cut (the path keeps the full radius already) */
}
static void build_colliders(void) {
    g_ncol = 0;
    for (int i = 0; i < NPROPS && g_ncol < MAX_COLLIDERS - 3; i++) {
        const prop *p = &PROPS[i];
        if (p->kind == 0) { g_col[g_ncol].x = p->x; g_col[g_ncol].z = p->z; g_col[g_ncol].r = 0.75f * p->scale; g_col[g_ncol].h = 1.2f * p->scale; g_ncol++; }
        else if (p->kind == 2) { g_col[g_ncol].x = p->x; g_col[g_ncol].z = p->z; g_col[g_ncol].r = 0.9f * p->scale; g_col[g_ncol].h = 1.0f * p->scale; g_ncol++; }
        else for (int k = -1; k <= 1; k++) {   /* a crayon lying along its yaw: three circles */
            float t = (float)k * 0.85f * p->scale;
            g_col[g_ncol].x = p->x + sinf(p->yaw) * t; g_col[g_ncol].z = p->z + cosf(p->yaw) * t;
            g_col[g_ncol].r = 0.35f * p->scale; g_col[g_ncol].h = 0.5f * p->scale; g_ncol++;
        }
    }
}

/* asset folder: run from the repo, from build/ (ARC's tests) or from samples/demo */
static char g_base[64];
static const char *asset(const char *rel) { return arc_fmt("%s%s", g_base, rel); }
static bool find_assets(void) {
    static const char *bases[] = { "samples/demo/assets/", "../samples/demo/assets/", "assets/", "../assets/", "../../samples/demo/assets/" };
    if (arc_arg_flag("no-assets")) return false;
    for (int i = 0; i < 5; i++)
        if (arc_file_exists(arc_fmt("%smeshes/player.arcm", bases[i]))) { snprintf(g_base, sizeof g_base, "%s", bases[i]); return true; }
    return false;
}
static void load_art(void) {
    m_floor = arc_mesh_plane(0xFFFFFF, 1, 0xFFFFFF); arc_mesh_set_name(m_floor, "floor");
    arc_mesh_set_texture(m_floor, arc_texture_load_ex(asset("textures/floor.jpg"), ARC_TEX_MESH));
    m_player = arc_mesh_load(asset("meshes/player.arcm")); arc_mesh_set_name(m_player, "player");
    a_player = arc_anim_load(asset("meshes/player.arca"));
    clip_idle = arc_anim_find(a_player, "idle"); clip_run = arc_anim_find(a_player, "run");
    clip_hit = arc_anim_find(a_player, "hit"); clip_death = arc_anim_find(a_player, "death");
    m_enemy[K_ANT] = arc_mesh_load(asset("meshes/ant.arcm")); arc_mesh_set_name(m_enemy[K_ANT], "ant");
    m_enemy[K_BEETLE] = arc_mesh_load(asset("meshes/beetle.arcm")); arc_mesh_set_name(m_enemy[K_BEETLE], "beetle");
    m_enemy[K_FLEA] = arc_mesh_load(asset("meshes/flea.arcm")); arc_mesh_set_name(m_enemy[K_FLEA], "flea");
    m_block = arc_mesh_load(asset("meshes/block.arcm")); arc_mesh_set_name(m_block, "block");
    m_crayon = arc_mesh_load(asset("meshes/crayon.arcm")); arc_mesh_set_name(m_crayon, "crayon");
    m_drum = arc_mesh_load(asset("meshes/drum.arcm")); arc_mesh_set_name(m_drum, "drum");
    {   /* Ludo's models come marked double-sided; one-sided lets the outline hull show (arc_mesh_style.outline) */
        arc_mesh one[] = { m_player, m_enemy[K_ANT], m_enemy[K_BEETLE], m_enemy[K_FLEA], m_block, m_crayon, m_drum };
        for (int i = 0; i < 7; i++) arc_mesh_set_double_sided(one[i], false);
    }
    s_shoot = arc_sound_load(asset("sfx/shoot.mp3")); s_hit = arc_sound_load(asset("sfx/hit.mp3"));
    s_kill = arc_sound_load(asset("sfx/kill.mp3")); s_pickup = arc_sound_load(asset("sfx/pickup.mp3"));
    s_wave = arc_sound_load(asset("sfx/wave.mp3")); s_hurt = arc_sound_load(asset("sfx/hurt.mp3"));
    g_music = arc_music_load(asset("music/toybox.mp3"));
    g_font = arc_font_load(asset("fonts/fredoka_sdf48.arcf"));
}

static void init(void) {
    arc_var("player.speed", &v_speed, 9.0f);
    arc_var("enemy.speed", &v_enemy_speed, 3.2f);
    arc_var("fire.rate", &v_fire_rate, 9.0f);
    arc_var("bullet.speed", &v_bullet_speed, 30.0f);
    arc_var("spawn.rate", &v_spawn_rate, 6.0f);
    arc_var("mouse.sensitivity", &v_sens, 1.0f);   /* 0.10.18 */
    arc_var("mouse.invert", &v_invert, 0.0f);
    arc_var("combo.time", &v_combo_time, 1.6f);
    arc_var("enemy.nav", &v_enemy_nav, 1.0f);   /* 0.10.19 */
    arc_var("line_test", &v_line_test, 0.0f);
    arc_var("nan_test", &v_nan_test, 0.0f);
    arc_var("text_test", &v_text_test, 0.0f);
    arc_var("save_test", &v_save_test, 0.0f);
    arc_var("offset2d_test", &v_offset2d_test, 0.0f);
    arc_var("lines2d_test", &v_lines2d_test, 0.0f);
    arc_var("music_loop_test", &v_music_loop_test, 0.0f);
    arc_var("scroll_panel_test", &v_scroll_panel_test, 0.0f);
    arc_pool_make(&G.enemies, arc_perm(), enemy, MAX_ENEMIES);
    arc_pool_make(&G.bullets, arc_perm(), bullet, MAX_BULLETS);
    arc_pool_make(&G.particles, arc_perm(), particle, MAX_PARTICLES);
    m_floor_old = arc_mesh_plane(0x1A2230, 20, 0x202A3A);
    m_ship = arc_mesh_cone(0x40E0C0, 3);
    m_drone = arc_mesh_sphere(0xE85A3A, 8);
    m_heavy = arc_mesh_cube(0xF0B030);
    m_flea_old = arc_mesh_sphere(0xFF9030, 6);
    m_shard = arc_mesh_cube(0xFFFFFF);
    m_bolt = arc_mesh_sphere(0xFFFFFF, 6);
    m_cork = arc_mesh_sphere(0xFFD040, 8); arc_mesh_set_name(m_cork, "cork");
    m_key = arc_mesh_cube(0xD0A040);
    const char *b = arc_arg_value("bench"); if (b) G.bench = atoi(b);
    build_colliders();
    build_nav();
    g_art = find_assets();
    if (g_art) load_art();
    else {   /* code-made sounds (as SWARM had) */
        arc_mesh_set_name(m_ship, "player");
        arc_sfx shoot = { ARC_WAVE_SQUARE, 880, 220, 0.002f, 0.02f, 0.06f, 0.25f, 0.3f, 0, 0 };
        arc_sfx boom = { ARC_WAVE_NOISE, 900, 60, 0.002f, 0.05f, 0.35f, 0.5f, 0, 0, 0 };
        arc_sfx hit = { ARC_WAVE_SAW, 300, 60, 0.002f, 0.1f, 0.3f, 0.5f, 0, 0, 0 };
        s_shoot = arc_sound_make(&shoot); s_kill = arc_sound_make(&boom); s_hurt = s_hit = arc_sound_make(&hit);
    }
    if (g_music.id && !G.bench) { arc_music_set_volume(g_music, 0.4f); arc_music_play(g_music, true); arc_music_set_loop_fade(g_music, 0.05f); }
    reset();
}

static void kill_enemy(int k, enemy *e) {
    G.kills++;
    G.combo = G.combo_t > 0 ? G.combo + 1 : 1; G.combo_t = v_combo_time;
    if (G.combo > G.best_combo) G.best_combo = G.combo;
    int mult = 1 + G.combo / 5; if (mult > 5) mult = 5;
    int pts = KIND_SCORE[e->kind] * mult; G.score += pts;
    arc_popup_text(arc_v3_make(e->pos.x, 1.4f, e->pos.z), mult > 1 ? 0xFFE060 : 0xFFFFFF, mult > 1 ? 3.0f : 2.5f, arc_fmt("+%d", pts));
    if (G.combo > 1 && G.combo % 5 == 0) arc_popup_text(arc_v3_make(G.pos.x, 2.4f, G.pos.z), 0xFF70D0, 3.0f, arc_fmt("x%d!", mult));
    static const uint32_t col[K_KINDS] = { 0x60E060, 0xB060FF, 0xFF9030 };
    burst(e->pos, col[e->kind], e->kind == K_BEETLE ? 40 : 18, 7);
    confetti(e->pos, e->kind == K_BEETLE ? 40 : 14);
    sound(s_kill, e->kind == K_BEETLE ? 0.8f : 0.5f, e->pos.x);
    if (G.combo % 5 == 0 && s_pickup.id) sound(s_pickup, 0.5f, 0);
    G.hitstop = e->kind == K_BEETLE ? 0.08f : 0.025f;
    kick(e->kind == K_BEETLE ? 0.45f : 0.15f);
    arc_pool_remove(&G.enemies, arc_pool_handle_at(&G.enemies, k));
}

static void tick(void) {
    if (v_music_loop_test > 0.5f && !g_loop_music.id) {   /* 0.10.13 (a test: loaded once, when the tunable is set) */
        const char *paths[] = { "loop_test.qoa", "../tests/loop_test.qoa", "../../tests/loop_test.qoa" };
        for (int i = 0; i < 3 && !g_loop_music.id; i++) if (arc_file_exists(paths[i])) g_loop_music = arc_music_load(paths[i]);
        if (g_loop_music.id) { if (g_music.id) arc_music_stop(g_music); arc_music_set_loop(g_loop_music, 2.0f, 6.0f); arc_music_play(g_loop_music, true); }
    }
    if (v_save_test > 0.5f && !G.saved) { int v = 42; G.saved = arc_save("demo_test", &v, sizeof v); }   /* 0.10.12 */
    if (v_scroll_panel_test > 0.5f) {   /* 0.10.17 (MOLTEN BREACH): drag or wheel to scroll; a drag presses nothing */
        arc_ui_set_scale(0.5f);
        arc_ui_begin_scroll("Orders", 20, 40, 600, 400, &g_scroll.scroll);   /* 300 x 200 pixels at half size */
        for (int i = 1; i <= 30; i++) {
            char label[16]; snprintf(label, sizeof label, "Row %d", i);
            if (arc_ui_button(label)) { g_scroll.clicks++; snprintf(g_scroll.last, sizeof g_scroll.last, "%s", label); }
        }
        arc_ui_end();
    }
    float dt = arc_dt();
    if (G.bench) { G.bench_t += dt; return; }
    if (arc_key_pressed(ARC_KEY_C)) G.cam_mode ^= 1;
    if (arc_key_pressed(ARC_KEY_R)) reset();
    G.prev_pos = G.pos; G.prev_yaw = G.yaw; G.prev_cam_yaw = G.cam_yaw; G.prev_cam_pitch = G.cam_pitch;

    /* mouse look: a click captures the pointer (not over the UI, not with fingers), ESC lets go */
    bool grabbed = false;   /* the tick the pointer is captured: its jump to the click isn't a turn */
    if (!G.captured && arc_key_pressed(ARC_MOUSE_LEFT) && !arc_ui_wants_mouse() && arc_touch_count() == 0 && G.cam_mode == 0) G.captured = grabbed = true;
    if (G.captured && (arc_key_pressed(ARC_KEY_ESCAPE) || G.cam_mode != 0)) G.captured = false;
    arc_mouse_capture(G.captured);
    if (G.captured && !grabbed) {
        arc_v2 md = arc_mouse_delta();
        G.cam_yaw -= md.x * 0.0035f * sens();
        G.cam_pitch += md.y * 0.0035f * sens() * (v_invert > 0.5f ? -1.0f : 1.0f);
    }
    {   /* the right stick turns the camera too */
        float rx = arc_pad_axis(ARC_AXIS_RX), ry = arc_pad_axis(ARC_AXIS_RY);
        if (fabsf(rx) > 0.15f) G.cam_yaw -= rx * 2.6f * sens() * dt;
        if (fabsf(ry) > 0.15f) G.cam_pitch += ry * 1.6f * sens() * dt * (v_invert > 0.5f ? -1.0f : 1.0f);
    }
    G.cam_yaw = arc_wrap_angle(G.cam_yaw); G.cam_pitch = arc_clampf(G.cam_pitch, 0.08f, 1.05f);

    /* juice clocks (looks, but kept in the state so a rewind shows the same frame) */
    G.title_t += dt; G.banner_t += dt; G.anim_t += dt; G.key_spin += dt * (G.dead ? 0.5f : 6.0f);
    G.shake = arc_maxf(0, G.shake - dt * 1.6f); G.recoil = arc_maxf(0, G.recoil - dt * 8); G.hurt_flash = arc_maxf(0, G.hurt_flash - dt * 2.5f);
    if (G.hit_t > 0) { G.hit_t += dt; if (clip_hit < 0 || G.hit_t > arc_anim_length(a_player, clip_hit)) G.hit_t = 0; }
    G.shown_score += ((float)G.score - G.shown_score) * arc_minf(1, dt * 10); if (fabsf((float)G.score - G.shown_score) < 0.5f) G.shown_score = (float)G.score;
    if (G.hitstop > 0) { G.hitstop = arc_maxf(0, G.hitstop - dt); return; }   /* hit-stop: the world holds still for a moment */
    if (G.combo_t > 0) { G.combo_t -= dt; if (G.combo_t <= 0) G.combo = 0; }

    ARC_PROF("player") {
        if (G.dead) { G.dead_t += dt; }
        else {
            float ix = 0, iz = 0;
            if (arc_key_down(ARC_KEY_A) || arc_key_down(ARC_KEY_LEFT)) ix -= 1;
            if (arc_key_down(ARC_KEY_D) || arc_key_down(ARC_KEY_RIGHT)) ix += 1;
            if (arc_key_down(ARC_KEY_W) || arc_key_down(ARC_KEY_UP)) iz -= 1;
            if (arc_key_down(ARC_KEY_S) || arc_key_down(ARC_KEY_DOWN)) iz += 1;
            float lx = arc_pad_axis(ARC_AXIS_LX), ly = arc_pad_axis(ARC_AXIS_LY);
            if (fabsf(lx) > 0.2f) ix += lx;
            if (fabsf(ly) > 0.2f) iz += ly;
            float cy = G.cam_mode == 0 ? G.cam_yaw : 0;   /* top-down: the world's axes */
            arc_v3 mv = arc_v3_add(arc_v3_scale(cam_right(cy), ix), arc_v3_scale(cam_forward(cy), -iz));
            float ml = arc_v3_len(mv);
            if (ml > 0) {
                if (ml > 1) mv = arc_v3_scale(mv, 1.0f / ml);
                if (!G.captured) G.aim = arc_v3_norm(mv);
                G.pos = arc_v3_madd(G.pos, mv, v_speed * dt);
                G.pos.x = arc_clampf(G.pos.x, -ARENA + 0.5f, ARENA - 0.5f); G.pos.z = arc_clampf(G.pos.z, -ARENA + 0.5f, ARENA - 0.5f);
                push_out(&G.pos, 0.45f);
            }
            if (G.captured) G.aim = cam_forward(G.cam_yaw);   /* mouse look: fire at the crosshair */
            G.run_w += ((ml > 0.1f ? 1.0f : 0.0f) - G.run_w) * arc_minf(1, dt * 10);
            float want = atan2f(G.aim.x, G.aim.z);
            G.yaw += arc_wrap_angle(want - G.yaw) * arc_minf(1, dt * 14);
            G.fire_cd -= dt; G.invuln -= dt;
            if ((arc_key_down(ARC_KEY_SPACE) || arc_key_down(ARC_MOUSE_LEFT) || arc_key_down(ARC_PAD_A) || arc_pad_axis(ARC_AXIS_RT) > 0.5f) && G.fire_cd <= 0) {
                G.fire_cd = 1.0f / v_fire_rate;
                arc_handle h = arc_pool_add(&G.bullets); bullet *b = (bullet *)arc_pool_get(&G.bullets, h);
                if (b) {
                    b->pos = arc_v3_madd(arc_v3_make(G.pos.x, 0.5f, G.pos.z), G.aim, 0.7f); b->vel = arc_v3_scale(G.aim, v_bullet_speed); b->life = 1.2f;
                    sound(s_shoot, 0.35f, G.pos.x); G.recoil = 1;
                }
            }
        }
    }
    ARC_PROF("waves") {
        if (G.to_spawn == 0 && G.enemies.count == 0 && !G.dead) {
            G.wave++; G.to_spawn = 6 + G.wave * 6; G.banner_t = 0;
            if (s_wave.id) arc_play(s_wave, 0.5f, 0);
        }
        G.spawn_cd -= dt;
        while (G.to_spawn > 0 && G.spawn_cd <= 0) { spawn_enemy(); G.to_spawn--; G.spawn_cd += 1.0f / (v_spawn_rate + (float)G.wave); }
    }
    ARC_PROF("enemies") {
        for (int i = 0; i < G.enemies.cap; i++) {
            if (!arc_pool_alive(&G.enemies, i)) continue;
            enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
            e->age += dt; e->flash = arc_maxf(0, e->flash - dt);
            arc_v3 to = arc_v3_sub(G.pos, e->pos); to.y = 0;
            float d = arc_v3_len(to);
            steer_goal(e, i, e->kind == K_BEETLE ? 0.65f : 0.45f, dt);   /* 0.10.19: round the cover */
            arc_v3 way = arc_v3_sub(e->goal, e->pos); way.y = 0;
            if (arc_v3_len(way) < 0.01f) way = to;
            if (e->kind == K_FLEA) {   /* hops: still on the ground, then a leap toward the robot */
                e->hop -= dt;
                if (e->hop <= 0) {
                    float side = arc_rand_rangef(-1.5f, 1.5f);
                    arc_v3 dir = arc_v3_norm(way);
                    e->vel = arc_v3_add(arc_v3_scale(dir, v_enemy_speed * 2.4f), arc_v3_scale(arc_v3_make(-dir.z, 0, dir.x), side));
                    e->hop = 0.95f; e->phase = 0;
                }
                e->phase += dt;
                if (e->phase > 0.45f) e->vel = arc_v3_scale(e->vel, arc_maxf(0, 1 - dt * 12));
            } else {
                float sp = v_enemy_speed * (e->kind == K_BEETLE ? 0.6f : 1.0f) * (1.0f + (float)G.wave * 0.04f);
                arc_v3 want = arc_v3_scale(arc_v3_norm(way), sp);
                e->phase += dt * 3;
                want = arc_v3_add(want, arc_v3_make(cosf(e->phase) * 0.8f, 0, sinf(e->phase) * 0.8f));
                e->vel = arc_v3_lerp(e->vel, want, arc_minf(1, dt * 3));
            }
            e->pos = arc_v3_madd(e->pos, e->vel, dt);
            if (push_out(&e->pos, e->kind == K_BEETLE ? 0.6f : 0.4f) && e->kind == K_BEETLE) e->vel = arc_v3_scale(e->vel, 0.5f);   /* cover breaks up a charge */
            if (!G.dead && d < (e->kind == K_BEETLE ? 1.0f : 0.8f) && G.invuln <= 0 && !G.god) {
                G.lives--; G.invuln = 2; burst(G.pos, 0xFF4040, 60, 8); sound(s_hurt, 0.8f, 0);
                G.hurt_flash = 1; G.hit_t = 0.001f; G.combo = 0; G.combo_t = 0; G.hitstop = 0.12f; kick(0.8f);
                arc_pool_remove(&G.enemies, arc_pool_handle_at(&G.enemies, i));
                if (G.lives <= 0) { G.dead = true; G.dead_t = 0; burst(G.pos, 0xFFFFFF, 200, 12); confetti(G.pos, 80); }
            }
        }
    }
    ARC_PROF("bullets") {
        for (int i = 0; i < G.bullets.cap; i++) {
            if (!arc_pool_alive(&G.bullets, i)) continue;
            bullet *b = (bullet *)arc_pool_at(&G.bullets, i);
            b->pos = arc_v3_madd(b->pos, b->vel, dt); b->life -= dt;
            bool gone = b->life <= 0 || fabsf(b->pos.x) > ARENA || fabsf(b->pos.z) > ARENA;
            if (!gone && in_cover(b->pos)) { gone = true; burst(b->pos, 0xFFE080, 5, 3); }   /* a cork bounces off the cover */
            for (int k = 0; k < G.enemies.cap && !gone; k++) {
                if (!arc_pool_alive(&G.enemies, k)) continue;
                enemy *e = (enemy *)arc_pool_at(&G.enemies, k);
                float r = e->kind == K_BEETLE ? 0.8f : 0.55f;
                float dx = e->pos.x - b->pos.x, dz = e->pos.z - b->pos.z;
                if (dx * dx + dz * dz < r * r) {
                    gone = true; e->hp -= 1; e->flash = 0.1f;
                    if (e->hp <= 0) kill_enemy(k, e);
                    else { burst(b->pos, 0xFFFFFF, 6, 4); sound(s_hit, 0.5f, e->pos.x); e->vel = arc_v3_madd(e->vel, b->vel, 0.05f); kick(0.05f); }
                }
            }
            if (gone) arc_pool_remove(&G.bullets, arc_pool_handle_at(&G.bullets, i));
        }
    }
    ARC_PROF("particles") {
        for (int i = 0; i < G.particles.cap; i++) {
            if (!arc_pool_alive(&G.particles, i)) continue;
            particle *p = (particle *)arc_pool_at(&G.particles, i);
            if (p->flat) { p->vel.y -= 7 * dt; p->vel.x *= 1 - dt * 1.5f; p->vel.z *= 1 - dt * 1.5f; if (p->vel.y < -2.5f) p->vel.y = -2.5f; }
            else p->vel.y -= 14 * dt;
            p->pos = arc_v3_madd(p->pos, p->vel, dt);
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
        arc_draw_mesh(i & 1 ? m_heavy : m_drone, arc_m4_trs_y(arc_v3_make(x, y, z), t + (float)i * 0.1f, arc_v3_make(0.8f, 0.8f, 0.8f)), 0xFFFFFF);
    }
    arc_gfx_stats st = arc_stats();
    arc_draw_rect(8, 8, 420, 52, arc_rgba(0x000000, 0.6f));
    arc_draw_textf(16, 16, 2, 0xFFFFFF, "BENCH %d meshes", G.bench);
    arc_draw_textf(16, 40, 1, 0x9FB0C8, "%s  %.2f ms/frame  %d draw calls  %d tris", st.backend, st.avg_frame_ms, st.draw_calls, st.triangles);
}

/* text in the Fredoka font when it loaded, else the built-in one; px = the line height in pixels */
static float hud_text(float x, float y, float px, uint32_t rgb, int flags, const char *s) {
    if (g_font.id) return arc_font_draw_aligned(g_font, x, y, px / (float)arc_font_size(g_font), rgb, flags, s);
    float sc = arc_maxf(1.0f, floorf(px / 8.0f)); arc_draw_text_ex(x, y, sc, rgb, flags, s); return arc_text_width(sc, s);
}

static void draw_world_classic(arc_v3 p, float alpha) {   /* SWARM's plain look: the engine checks (line_test, nan_test) */
    arc_draw_mesh(m_floor_old, arc_m4_scale(arc_v3_make(ARENA * 2, 1, ARENA * 2)), 0xFFFFFF);
    arc_draw_box_lines(arc_v3_make(-ARENA, 0, -ARENA), arc_v3_make(ARENA, 1.0f, ARENA), 0x2A6A8A);
    if (!G.dead && (G.invuln <= 0 || fmod(arc_time() * 10, 2) < 1)) {
        float yaw = G.prev_yaw + arc_wrap_angle(G.yaw - G.prev_yaw) * alpha;
        arc_m4 m = arc_m4_mul(arc_m4_trs_y(arc_v3_make(p.x, 0.5f, p.z), yaw, arc_v3_make(0.9f, 1.4f, 0.9f)), arc_m4_rotate_x(ARC_PI * 0.5f));
        arc_draw_mesh_ex(m_ship, m, 0xFFFFFF, 0.15f);
    }
    for (int i = 0; i < G.enemies.cap; i++) {
        if (!arc_pool_alive(&G.enemies, i)) continue;
        enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
        arc_v3 ep = arc_v3_madd(e->pos, e->vel, (alpha - 1) * arc_dt());
        ep.y = 0.5f + sinf(e->phase * 2) * 0.12f;
        if (e->kind == K_BEETLE) arc_draw_mesh_ex(m_heavy, arc_m4_trs_y(ep, e->phase, arc_v3_make(1.2f, 1.2f, 1.2f)), 0xFFFFFF, 0.1f);
        else arc_draw_mesh_ex(e->kind == K_FLEA ? m_flea_old : m_drone, arc_m4_trs_y(ep, 0, arc_v3_make(0.9f, 0.9f, 0.9f)), 0xFFFFFF, 0.1f);
    }
    for (int i = 0; i < G.bullets.cap; i++) {
        if (!arc_pool_alive(&G.bullets, i)) continue;
        bullet *b = (bullet *)arc_pool_at(&G.bullets, i);
        arc_draw_mesh_ex(m_bolt, arc_m4_trs_y(arc_v3_madd(b->pos, b->vel, (alpha - 1) * arc_dt()), 0, arc_v3_make(0.3f, 0.3f, 0.3f)), 0x9FFFF0, 1.0f);
    }
}

static float flea_height(const enemy *e) { return e->kind == K_FLEA && e->phase < 0.45f ? sinf(e->phase / 0.45f * ARC_PI) * 1.3f : 0; }

static void draw_world_toybox(arc_v3 p, float alpha) {
    const float OL = 0.045f;   /* black cartoon outlines */
    if (g_art) arc_draw_mesh_styled(m_floor, arc_m4_trs_y(arc_v3_make(0, 0, 0), 0, arc_v3_make(ARENA * 2 + 4, 1, ARENA * 2 + 4)), &(arc_mesh_style){ .uv_scale = 4 });
    else arc_draw_mesh(m_floor_old, arc_m4_scale(arc_v3_make(ARENA * 2, 1, ARENA * 2)), 0xFFFFFF);
    /* the walls: a ring of alphabet blocks, each turned a different way so the letters vary */
    for (int i = 0; i < 21; i++) {
        float t = -ARENA + 2.0f * (float)i, e = ARENA + 1.0f, s = 1.35f;
        float turn = (float)((i * 7) % 4) * 1.5707963f;
        if (g_art) {
            arc_draw_mesh(m_block, arc_m4_trs_y(arc_v3_make(t, 0, -e), turn, arc_v3_make(s, s, s)), 0xFFFFFF);
            arc_draw_mesh(m_block, arc_m4_trs_y(arc_v3_make(t, 0, e), turn + 1.0f * 1.5707963f, arc_v3_make(s, s, s)), 0xFFFFFF);
            if (i > 0 && i < 20) {
                arc_draw_mesh(m_block, arc_m4_trs_y(arc_v3_make(-e, 0, t), turn + 2.0f * 1.5707963f, arc_v3_make(s, s, s)), 0xFFFFFF);
                arc_draw_mesh(m_block, arc_m4_trs_y(arc_v3_make(e, 0, t), turn + 3.0f * 1.5707963f, arc_v3_make(s, s, s)), 0xFFFFFF);
            }
        }
    }
    if (!g_art) arc_draw_box_lines(arc_v3_make(-ARENA, 0, -ARENA), arc_v3_make(ARENA, 1.0f, ARENA), 0x2A6A8A);
    for (int i = 0; i < NPROPS; i++) {   /* cover */
        const prop *q = &PROPS[i]; arc_v3 sc = arc_v3_make(q->scale, q->scale, q->scale);
        arc_m4 m = arc_m4_trs_y(arc_v3_make(q->x, 0, q->z), q->yaw, sc);
        if (!g_art) { arc_draw_mesh(m_heavy, arc_m4_trs_y(arc_v3_make(q->x, 0.5f * q->scale, q->z), q->yaw, q->kind == 1 ? arc_v3_make(0.5f, 0.5f, 2.4f * q->scale) : sc), q->kind == 0 ? 0xF0C040 : q->kind == 1 ? 0x4060FF : 0xE04040); continue; }
        arc_draw_mesh_styled(q->kind == 0 ? m_block : q->kind == 1 ? m_crayon : m_drum, m, &(arc_mesh_style){ .outline = OL });
    }
    /* the robot */
    bool blink = !G.dead && G.invuln > 0 && fmodf(G.anim_t * 10, 2) < 1;
    if (!blink) {
        float yaw = G.prev_yaw + arc_wrap_angle(G.yaw - G.prev_yaw) * alpha;
        arc_v3 f = arc_v3_make(sinf(yaw), 0, cosf(yaw));
        float back = G.recoil * 0.12f, squash = G.recoil * 0.06f;
        arc_v3 rp = arc_v3_madd(p, f, -back);
        if (g_art && m_player.id) {
            arc_anim_layer L[3]; int n = 0;
            if (G.dead) L[n++] = (arc_anim_layer){ clip_death, G.dead_t, 1, false };
            else {
                L[n++] = (arc_anim_layer){ clip_idle, G.anim_t, 1.0f - G.run_w + 0.001f, true };
                L[n++] = (arc_anim_layer){ clip_run, G.anim_t * 1.3f, G.run_w, true };
                if (G.hit_t > 0) L[n++] = (arc_anim_layer){ clip_hit, G.hit_t, 2.0f, false };
            }
            static arc_pose pose; arc_anim_pose(a_player, L, n, &pose);
            arc_draw_mesh_posed_styled(m_player, arc_m4_trs_y(rp, yaw, arc_v3_make(1 + squash, 1 - squash, 1 + squash)), a_player, &pose,
                                       &(arc_mesh_style){ .whiten = G.hurt_flash > 0.6f ? 0.8f : 0, .outline = OL });
        } else {
            arc_m4 m = arc_m4_mul(arc_m4_trs_y(arc_v3_make(rp.x, 0.5f, rp.z), yaw, arc_v3_make(0.9f, 1.4f, 0.9f)), arc_m4_rotate_x(ARC_PI * 0.5f));
            arc_draw_mesh_ex(m_ship, m, 0xFFFFFF, 0.15f);
        }
        if (!G.dead) {   /* the wind-up key turns on its back */
            arc_v3 kp = arc_v3_madd(arc_v3_make(rp.x, 0.85f, rp.z), f, -0.42f);
            arc_m4 km = arc_m4_mul(arc_m4_trs_y(kp, yaw, arc_v3_make(1, 1, 1)), arc_m4_rotate_z(G.key_spin));
            arc_draw_mesh_styled(m_key, arc_m4_mul(km, arc_m4_scale(arc_v3_make(0.42f, 0.12f, 0.05f))), &(arc_mesh_style){ .tint = 0xD8A838, .recolour = 1, .outline = OL });
            arc_draw_mesh_styled(m_key, arc_m4_mul(km, arc_m4_scale(arc_v3_make(0.08f, 0.08f, 0.25f))), &(arc_mesh_style){ .tint = 0xB08020, .recolour = 1 });
        }
    }
    /* the bugs */
    for (int i = 0; i < G.enemies.cap; i++) {
        if (!arc_pool_alive(&G.enemies, i)) continue;
        enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
        arc_v3 ep = arc_v3_madd(e->pos, e->vel, (alpha - 1) * arc_dt());
        float pop = e->age < 0.35f ? arc_ease_out_back(e->age / 0.35f) : 1.0f;   /* squash-and-stretch pop-in */
        float wob = e->kind == K_FLEA ? 0 : sinf(e->phase * 6 + (float)i) * 0.06f;
        float hy = flea_height(e), st = e->kind == K_FLEA ? (hy > 0.05f ? 0.18f : -0.12f) : 0;
        arc_v3 sc = arc_v3_make(pop * (1 + wob - st * 0.5f), pop * (1 - wob + st), pop * (1 + wob - st * 0.5f));
        float yaw = arc_yaw_to(G.pos.x - e->pos.x, G.pos.z - e->pos.z);
        if (g_art) {
            arc_draw_mesh_styled(m_enemy[e->kind], arc_m4_trs_y(arc_v3_make(ep.x, hy, ep.z), yaw, sc),
                                 &(arc_mesh_style){ .whiten = e->flash > 0 ? 0.9f : 0, .outline = OL });
            if (hy > 0.05f) arc_draw_marker(&(arc_marker){ .shape = ARC_MARK_DISC, .x = ep.x, .z = ep.z, .size = 0.7f, .fill_rgb = 0x000000, .fill_alpha = 0.25f });
        } else {
            arc_mesh mm = e->kind == K_BEETLE ? m_heavy : e->kind == K_FLEA ? m_flea_old : m_drone;
            float s = e->kind == K_BEETLE ? 1.2f : 0.9f;
            arc_draw_mesh_tinted(mm, arc_m4_trs_y(arc_v3_make(ep.x, 0.5f + hy, ep.z), yaw, arc_v3_scale(sc, s)), 0xFFFFFF, 0, 0.1f, e->flash > 0 ? 0.9f : 0);
        }
    }
    if (g_nav_draw && g_nav.id) {   /* 0.10.19 debugging: the walkable area and each bug's next corner */
        arc_navmesh_draw(g_nav, 0x2060FF);
        for (int i = 0; i < G.enemies.cap; i++) if (arc_pool_alive(&G.enemies, i)) {
            enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
            arc_draw_line(arc_v3_make(e->pos.x, 0.3f, e->pos.z), arc_v3_make(e->goal.x, 0.3f, e->goal.z), 0xFF40FF);
        }
    }
    for (int i = 0; i < G.bullets.cap; i++) {   /* corks */
        if (!arc_pool_alive(&G.bullets, i)) continue;
        bullet *b = (bullet *)arc_pool_at(&G.bullets, i);
        arc_v3 bp = arc_v3_madd(b->pos, b->vel, (alpha - 1) * arc_dt());
        arc_draw_mesh_ex(m_cork, arc_m4_trs_y(bp, arc_yaw_to(b->vel.x, b->vel.z), arc_v3_make(0.22f, 0.22f, 0.4f)), 0xFFE070, 1.6f);
    }
    if (G.recoil > 0.5f && !G.dead) arc_light_point(arc_v3_make(G.pos.x + G.aim.x, 1.0f, G.pos.z + G.aim.z), 0xFFD070, 2.0f, 4);   /* muzzle pop */
}

static void draw(float alpha) {
    if (G.bench) { draw_bench(); return; }
    arc_v3 p = arc_v3_lerp(G.prev_pos, G.pos, alpha);
    bool classic = classic_view();
    static int post_mode = -1;   /* the post effects follow the view (the nan test sets its own, below) */
    if (post_mode != (classic ? 0 : 1)) {
        post_mode = classic ? 0 : 1;
        if (classic) arc_set_post(NULL);
        else arc_set_post(&(arc_post){ .bloom = 0.3f, .bloom_threshold = 1.5f, .tonemap = true, .vignette = 0.25f, .contrast = 0.08f, .desaturate = -0.15f, .tilt_shift = 0.35f, .fxaa = true });
    }
    arc_camera cam;
    if (classic) cam = (arc_camera){ { p.x, 22, p.z + 14 }, { p.x, 0, p.z }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 200 };
    else if (G.cam_mode == 1) cam = (arc_camera){ { 0, 46, 0.01f }, { 0, 0, 0 }, { 0, 1, 0 }, 0, 44, 0.1f, 200 };
    else {   /* third person behind the robot, turned by the mouse; shaken by hits */
        float yaw = G.prev_cam_yaw + arc_wrap_angle(G.cam_yaw - G.prev_cam_yaw) * alpha, pitch = arc_lerpf(G.prev_cam_pitch, G.cam_pitch, alpha);
        float dist = 8.5f, t = G.anim_t * 40.0f, sh = G.shake * G.shake * 0.45f;
        arc_v3 target = arc_v3_make(p.x, 1.2f, p.z);
        arc_v3 eye = arc_v3_make(target.x + sinf(yaw) * cosf(pitch) * dist, target.y + sinf(pitch) * dist, target.z + cosf(yaw) * cosf(pitch) * dist);
        arc_v3 jig = arc_v3_make(sinf(t * 1.3f) * sh, sinf(t * 1.7f + 1) * sh, sinf(t * 1.1f + 2) * sh);
        cam = (arc_camera){ arc_v3_add(eye, jig), arc_v3_add(target, arc_v3_scale(jig, 0.5f)), { 0, 1, 0 }, 1.0f, 0, 0.1f, 200 };
    }
    arc_set_camera(&cam);
    arc_set_fog(classic ? 0x0B0E14 : 0x6F8FB8, classic ? 25.0f : 70.0f, classic ? 60.0f : 160.0f);
    if (classic) { arc_set_light(arc_v3_make(0.4f, 1, 0.25f), 0xFFF4E0, 0x3A4458); draw_world_classic(p, alpha); }
    else {
        arc_set_light(arc_v3_make(-0.45f, 1, 0.35f), 0xFFE6B8, 0x5A6888);   /* a warm desk lamp, cool fill */
        arc_set_shadows(p, 18, 2048);
        arc_light_point(arc_v3_make(-16, 9, -16), 0xFFC880, 0.8f, 40);
        draw_world_toybox(p, alpha);
    }
    for (int i = 0; i < G.particles.cap; i++) {
        if (!arc_pool_alive(&G.particles, i)) continue;
        particle *q = (particle *)arc_pool_at(&G.particles, i);
        float s = q->size * arc_clampf(q->life * 2, 0, 1);
        arc_m4 m = arc_m4_trs_y(q->pos, q->life * q->spin, q->flat ? arc_v3_make(s, s * 0.12f, s) : arc_v3_make(s, s, s));
        if (q->flat) m = arc_m4_mul(m, arc_m4_rotate_x(q->life * q->spin * 0.7f));
        arc_draw_mesh_ex(m_shard, m, q->color, q->flat ? 0.3f : 0.8f);
    }
    if (v_nan_test > 0.5f) {   /* 0.10.12: one bad pixel source; with the guard it stays a dark patch, not a glowing blotch */
        static bool posted; if (!posted) { posted = true; arc_set_post(&(arc_post){ .bloom = 0.6f, .bloom_threshold = 1.0f, .exposure = 1.0f }); }
        volatile float zero = 0.0f;
        arc_draw_mesh_ex(m_shard, arc_m4_trs_y(arc_v3_make(p.x + 3, 1, p.z), 0, arc_v3_make(2, 2, 2)), 0xFFFFFF, zero / zero);
    }
    if (v_line_test > 0.5f) {   /* 0.10.11: a vector-style wireframe, 4 pixels wide, glowing */
        static const arc_v3 ship[] = { { 0, 0, -1.5f }, { 1, 0, 1 }, { 1, 0, 1 }, { 0, 0, 0.5f }, { 0, 0, 0.5f }, { -1, 0, 1 }, { -1, 0, 1 }, { 0, 0, -1.5f } };
        arc_set_line_width(4.0f); arc_set_line_glow(2.5f); arc_set_line_cap(ARC_LINE_CAP_ROUND);
        arc_draw_lines(ship, 8, arc_m4_trs_y(arc_v3_make(p.x - 4, 1.5f, p.z), 0.4f, arc_v3_make(2, 2, 2)), 0x40FFC0);
        arc_draw_line(arc_v3_make(p.x - 8, 0.2f, p.z - 3), arc_v3_make(p.x + 8, 0.2f, p.z - 3), 0xFF60C0);
        arc_set_line_width(1.0f); arc_set_line_glow(1.0f); arc_set_line_cap(ARC_LINE_CAP_SQUARE);
        arc_draw_line2d(20, 60, 300, 90, 5.0f, 0x40FFC0);
    }
    if (v_lines2d_test >= 1.0f) {   /* 0.10.13: a star of glowing vector segments (frame arena: no allocation) */
        int n = (int)v_lines2d_test; arc_v2 *pts = (arc_v2 *)arc_arena_alloc(arc_frame(), sizeof(arc_v2) * (size_t)n * 2, 8);
        if (pts) {
            float cx = (float)arc_width() * 0.5f, cy = (float)arc_height() * 0.5f;
            for (int i = 0; i < n; i++) {
                float a = (float)i * 2.39996f, r0 = 20.0f + (float)(i % 37) * 4.0f, r1 = r0 + 30.0f;
                pts[i * 2] = arc_v2_make(cx + cosf(a) * r0, cy + sinf(a) * r0); pts[i * 2 + 1] = arc_v2_make(cx + cosf(a) * r1, cy + sinf(a) * r1);
            }
            arc_draw_lines2d(pts, n * 2, 2.0f, 2.0f, 0.5f, 0x40FFC0);
        }
    }
    if (v_offset2d_test > 0.5f) {   /* 0.10.13: drawn from (0, 0); the offset puts it inside the notch and bars */
        arc_rectf sa = arc_safe_area();
        arc_push_offset2d(sa.x, sa.y, sa.w, sa.h);
        arc_draw_rect(0, 0, 200, 40, 0x203050);
        arc_draw_text(8, 16, 1, 0xFFFFFF, "SAFE");
        arc_pop_offset2d();
    }
    if (v_text_test > 0.5f) {   /* 0.10.12: a 63-pixel button at x = 384.5 with its label centred at the largest size that fits */
        float bx = 384.5f, by = 300, bw = 63, bh = 28;
        arc_draw_rect(bx, by, bw, bh, 0x203040);
        arc_draw_text_ex(bx + bw * 0.5f, by + bh * 0.5f, arc_text_fit_scale("DOCK", bw - 4, 3), 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE, "DOCK");
    }
    /* HUD */
    float W = (float)arc_width(), H = (float)arc_height();
    arc_gfx_stats st = arc_stats();
    if (G.hurt_flash > 0) arc_draw_rect(0, 0, W, H, arc_rgba(0xFF3030, 0.3f * G.hurt_flash));
    hud_text(16, 10, 34, 0xFFFFFF, ARC_TEXT_OUTLINE, arc_fmt("SCORE %06d", (int)(G.shown_score + 0.5f)));
    hud_text(16, 48, 24, 0xFFD040, ARC_TEXT_OUTLINE, arc_fmt("WAVE %d", G.wave));
    hud_text(140, 48, 24, 0xFF6060, ARC_TEXT_OUTLINE, arc_fmt("LIVES %d", G.lives > 0 ? G.lives : 0));
    if (G.combo > 1) {   /* the combo meter: the multiplier, and how long until it runs out */
        int mult = 1 + G.combo / 5; if (mult > 5) mult = 5;
        float k = arc_clampf(G.combo_t / v_combo_time, 0, 1), pulse = 1 + 0.25f * arc_clampf(k - 0.75f, 0, 1) * 4;
        hud_text(W - 16, 10, 32 * pulse, 0xFF70D0, ARC_TEXT_RIGHT | ARC_TEXT_OUTLINE, arc_fmt("COMBO %d  x%d", G.combo, mult));
        arc_draw_rect(W - 196, 50, 180, 8, arc_rgba(0x000000, 0.4f));
        arc_draw_rect(W - 196, 50, 180 * k, 8, 0xFF70D0);
    }
    if (G.banner_t < 2.2f && G.wave > 0) {   /* the wave banner: pops in, holds, slides away */
        float t = G.banner_t, in = arc_ease_out_back(arc_clampf(t / 0.35f, 0, 1)), out = arc_clampf((t - 1.7f) / 0.5f, 0, 1);
        float y = H * 0.3f - out * out * H * 0.4f;
        hud_text(W * 0.5f, y, 84 * in, 0xFFD040, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE | ARC_TEXT_OUTLINE, arc_fmt("WAVE %d", G.wave));
        if (in > 0.9f) hud_text(W * 0.5f, y + 54, 28, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE | ARC_TEXT_OUTLINE, G.wave == 1 ? "DEFEND THE TOY BOX!" : "HERE THEY COME!");
    }
    if (G.title_t < 3.0f && !classic) {
        float a = 1 - arc_clampf((G.title_t - 2.2f) / 0.8f, 0, 1);
        hud_text(W * 0.5f, H * 0.14f, 64, arc_rgba(0xFF5050, a), ARC_TEXT_CENTER | ARC_TEXT_MIDDLE | ARC_TEXT_OUTLINE, "TOY BOX BLITZ");
    }
    if (G.captured && G.cam_mode == 0 && !G.dead) {   /* the crosshair */
        float cx = W * 0.5f, cy = H * 0.5f;
        arc_draw_rect(cx - 12, cy - 1, 8, 3, 0xFFFFFF); arc_draw_rect(cx + 5, cy - 1, 8, 3, 0xFFFFFF);
        arc_draw_rect(cx - 1, cy - 12, 3, 8, 0xFFFFFF); arc_draw_rect(cx - 1, cy + 5, 3, 8, 0xFFFFFF);
    }
    if (G.dead) {
        hud_text(W * 0.5f, H * 0.42f, 72, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE | ARC_TEXT_OUTLINE, "WOUND DOWN!");
        hud_text(W * 0.5f, H * 0.42f + 60, 30, 0xFFD040, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE | ARC_TEXT_OUTLINE, arc_fmt("SCORE %d   BEST COMBO %d   PRESS R", G.score, G.best_combo));
    }
    arc_draw_textf(W - 300, H - 20, 1, 0x7F8CA0, "%s %.1fms %d obj %d calls", st.backend, st.avg_frame_ms, st.instances, st.draw_calls);
    hud_text(16, H - 30, 20, 0xE8ECF4, ARC_TEXT_OUTLINE, G.captured ? "WASD MOVE   HOLD MOUSE FIRE   ESC FREE MOUSE   C TOP VIEW   R RESTART"
                                                                   : "CLICK TO AIM WITH THE MOUSE   WASD MOVE   SPACE FIRE   C TOP VIEW   R RESTART");
}

static void dump(arc_json *j) {
    arc_json_v3_k(j, "player", G.pos);
    arc_json_num_k(j, "player_yaw", G.yaw);
    arc_json_int_k(j, "lives", G.lives); arc_json_int_k(j, "score", G.score); arc_json_int_k(j, "wave", G.wave);
    arc_json_bool_k(j, "dead", G.dead);
    arc_json_int_k(j, "demo_setting", G.demo_setting); arc_json_bool_k(j, "saved", G.saved);
    if (v_scroll_panel_test > 0.5f) {   /* 0.10.17 */
        arc_json_num_k(j, "scroll", g_scroll.scroll); arc_json_int_k(j, "scroll_clicks", g_scroll.clicks); arc_json_str_k(j, "scroll_last", g_scroll.last);
        arc_json_num_k(j, "scroll_content_h", arc_ui_content_height());
    }
    arc_json_num_k(j, "music_position", g_loop_music.id ? arc_music_position(g_loop_music) : -1.0f); arc_json_bool_k(j, "music_playing", arc_music_playing(g_loop_music));
    { arc_gfx_stats st = arc_stats(); arc_json_obj_k(j, "stats"); arc_json_int_k(j, "vertices_2d", st.vertices_2d); arc_json_end(j); }   /* 0.10.13 */
    arc_json_int_k(j, "enemies", G.enemies.count); arc_json_int_k(j, "bullets", G.bullets.count); arc_json_int_k(j, "particles", G.particles.count);
    arc_json_arr_k(j, "nearest_enemies");
    for (int n = 0, i = 0; i < G.enemies.cap && n < 8; i++) {
        if (!arc_pool_alive(&G.enemies, i)) continue;
        enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
        arc_json_obj(j); arc_json_v3_k(j, "pos", e->pos); arc_json_num_k(j, "dist", arc_v3_dist(e->pos, G.pos)); arc_json_int_k(j, "kind", e->kind); arc_json_end(j); n++;
    }
    arc_json_end(j);
    /* 0.10.18 TOY BOX BLITZ */
    static const char *names[K_KINDS] = { "ant", "beetle", "flea" }; int count[K_KINDS] = { 0, 0, 0 };
    for (int i = 0; i < G.enemies.cap; i++) if (arc_pool_alive(&G.enemies, i)) count[((enemy *)arc_pool_at(&G.enemies, i))->kind]++;
    arc_json_str_k(j, "theme", "toybox"); arc_json_bool_k(j, "art", g_art);
    arc_json_num_k(j, "cam_yaw", G.cam_yaw); arc_json_num_k(j, "cam_pitch", G.cam_pitch); arc_json_bool_k(j, "captured", G.captured);
    arc_json_int_k(j, "cam_mode", G.cam_mode);
    arc_json_int_k(j, "combo", G.combo); arc_json_int_k(j, "best_combo", G.best_combo); arc_json_int_k(j, "kills", G.kills);
    { int mult = 1 + G.combo / 5; arc_json_int_k(j, "multiplier", mult > 5 ? 5 : mult); }
    arc_json_num_k(j, "hitstop", G.hitstop); arc_json_num_k(j, "shake", G.shake);
    arc_json_obj_k(j, "kinds"); for (int k = 0; k < K_KINDS; k++) arc_json_int_k(j, names[k], count[k]); arc_json_end(j);
    arc_json_bool_k(j, "music", g_music.id && arc_music_playing(g_music));
    arc_json_int_k(j, "nav_triangles", g_nav.id ? arc_navmesh_triangles(g_nav) : 0);   /* 0.10.19 */
    arc_json_arr_k(j, "bugs");   /* each bug (up to 16): where it is and the corner it heads for */
    for (int n = 0, i = 0; i < G.enemies.cap && n < 16; i++) {
        if (!arc_pool_alive(&G.enemies, i)) continue;
        enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
        arc_json_obj(j); arc_json_str_k(j, "kind", names[e->kind]); arc_json_num_k(j, "x", e->pos.x); arc_json_num_k(j, "z", e->pos.z);
        arc_json_num_k(j, "dist", arc_v3_dist(arc_v3_make(e->pos.x, 0, e->pos.z), arc_v3_make(G.pos.x, 0, G.pos.z)));
        arc_json_num_k(j, "goal_x", e->goal.x); arc_json_num_k(j, "goal_z", e->goal.z); arc_json_end(j); n++;
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
    if (!strcmp(name, "spawn")) {   /* spawn [N] [ant|beetle|flea] */
        int n = argc ? atoi(argv[0]) : 1; int kind = -1;
        if (argc >= 2) kind = !strcmp(argv[1], "beetle") ? K_BEETLE : !strcmp(argv[1], "flea") ? K_FLEA : K_ANT;
        while (n-- > 0) {
            spawn_enemy();
            if (kind >= 0) for (int i = G.enemies.cap - 1; i >= 0; i--) if (arc_pool_alive(&G.enemies, i)) {
                enemy *e = (enemy *)arc_pool_at(&G.enemies, i); if (e->age == 0) { e->kind = kind; e->hp = kind == K_BEETLE ? 4.0f : 1.0f; }
            }
        }
        return true;
    }
    if (!strcmp(name, "spawn_at") && argc >= 2) {   /* 0.10.18: spawn_at X Z [ant|beetle|flea]: one bug there (tests) */
        spawn_enemy();
        for (int i = G.enemies.cap - 1; i >= 0; i--) if (arc_pool_alive(&G.enemies, i)) {
            enemy *e = (enemy *)arc_pool_at(&G.enemies, i);
            if (e->age != 0) continue;
            int kind = argc >= 3 ? (!strcmp(argv[2], "beetle") ? K_BEETLE : !strcmp(argv[2], "flea") ? K_FLEA : K_ANT) : K_ANT;
            e->pos = arc_v3_make((float)atof(argv[0]), 0.5f, (float)atof(argv[1])); e->kind = kind; e->hp = kind == K_BEETLE ? 4.0f : 1.0f;
            break;
        }
        return true;
    }
    if (!strcmp(name, "god")) { G.god = argc ? atoi(argv[0]) != 0 : !G.god; return true; }
    if (!strcmp(name, "reset")) { reset(); return true; }
    if (!strcmp(name, "look") && argc >= 1) { G.cam_yaw = arc_wrap_angle((float)atof(argv[0]) / 57.29578f); if (argc >= 2) G.cam_pitch = arc_clampf((float)atof(argv[1]) / 57.29578f, 0.08f, 1.25f); return true; }   /* 0.10.18: look YAW_DEG [PITCH_DEG] */
    if (!strcmp(name, "navdraw")) { g_nav_draw = argc ? atoi(argv[0]) != 0 : !g_nav_draw; return true; }   /* 0.10.19: show the navmesh and where each bug heads */
    if (!strcmp(name, "capture")) { G.captured = argc ? atoi(argv[0]) != 0 : !G.captured; return true; }
    if (!strcmp(name, "set") && argc >= 2 && !strcmp(argv[0], "demo_setting")) { G.demo_setting = atoi(argv[1]); return true; }   /* 0.10.12: shadowed by ARC's set */
    return false;
}

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "TOY BOX BLITZ - ARC sample", .width = 1280, .height = 720, .clear_color = 0x6F8FB8,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
