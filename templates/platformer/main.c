/* {title} - a side-scrolling platformer (ARC template "platformer").
 *
 * A/D or LEFT/RIGHT run, SPACE/W/UP jumps (hold for higher; you can still jump a moment after leaving a ledge, and
 * a press just before landing counts). Collect the coins, avoid the spikes, jump on the slimes (touching one from
 * the side sends you back like the spikes do), light the checkpoint flag, reach the goal flag. R restarts.
 *
 * Where things are:
 *   LEVEL            the map as strings: '#' ground, 'o' coin, '^' spikes, 's' slime, 'C' checkpoint, 'F' flag, 'P' start.
 *   Game G           player position/velocity, coins taken, slimes, deaths, timers: pointer-free, so
 *                    save_state/load_state copy it and rewind, seek and the timeline work.
 *   move_and_collide the box-vs-tiles physics: move along x, push out; then along y, push out (ground/ceiling).
 *   tick()           input, coyote time and jump buffering, slimes, hazards, pick-ups, win. draw() only draws.
 *   juice            squash and stretch (G.squash), dust on jumps, landings and runs, coin sparkles with a pitch that
 *                    climbs while you chain them, stomp pops, a camera that leads where you run and shakes on hits,
 *                    popups (arc_popup_text), varied pitch (arc_play_var), confetti and a victory dance at the flag.
 *   assets/          models, animations, sounds and music made with Ludo; assets/manifest.json says how.
 *   dump()           x, y, vx, vy, on_ground, coins, slimes, stomps, deaths, phase, checkpoint.
 *   command()        agent commands: tp X Y, kill, win, restart.
 *   arc_var          tunables: run_speed, jump_speed, gravity, slime_speed (`set gravity 30`).
 * Grow it: more levels (an array of maps), moving platforms (boxes the player rides), wall jumps, a timer and best
 * times (arc_save), more enemy kinds (a kind field on Slime).
 */
#include "arc.h"
#include <string.h>
#include <stdlib.h>

#define LW 48
#define LH 10
static const char *LEVEL[LH] = {
    "                                                ",
    "                                                ",
    "                                          ooo  F",
    "                    ooos                 ###### ",
    "           o       #####       o o              ",
    "          ###                 #####    ##       ",
    "    o                                           ",
    " P      ^^   s  ##   ^^   C  # s   ^^^ s   ##   ",
    "################################################",
    "################################################",
};
enum { PHASE_PLAY, PHASE_WON };
#define PW 0.35f   /* half width of the player's box */
#define PH 0.9f    /* height */
#define MAX_SLIMES 8

typedef struct { bool alive; float x, y; int dir, squish; } Slime;   /* y: feet; squish: ticks of its flattened death */
typedef struct {
    int phase;
    arc_v2 p, v;                  /* feet centre; velocity in units a second */
    bool on_ground;
    int coyote, buffer;           /* ticks left to still jump after leaving ground / of a remembered press */
    int coins, deaths, stomps, ticks;
    arc_v2 checkpoint;
    bool taken[LH][LW];           /* coins picked up */
    int face;                     /* -1 left, 1 right */
    Slime s[MAX_SLIMES]; int nslimes;
    /* looks, kept in G so rewinds show the same thing */
    float squash;                 /* 1 = normal; < 1 flattened (landing), > 1 stretched (jump) */
    float cam_x, anim_t, win_t;
    int coin_streak, coin_timer, hurt_flash;
} Game;
static Game G;

static float *RUN, *JUMP, *GRAVITY, *SLIME_SPEED, v_run, v_jump, v_grav, v_slime;
static arc_mesh m_block, m_earth, m_coin, m_spike, m_flag, m_slime, m_player;
static arc_anim a_player; static int c_idle = -1, c_run = -1, c_jump = -1, c_win = -1;
static arc_sound s_jump, s_coin, s_stomp, s_hurt, s_check, s_win;
static arc_music music;
static float shake_t;               /* camera shake (looks: decays in draw time) */
static int total_coins;

static const arc_fx FX_DUST = { .life = 0.45f, .speed = 1.2f, .spread = 1.3f, .radius = 0.1f, .size = 0.22f, .size_end = 0.5f,
                                .color = 0xE8DCC0, .alpha = 0.6f, .gravity = { 0, 0.5f, 0 }, .drag = 3, .burst = 6 };
static const arc_fx FX_SPARK = { .life = 0.5f, .speed = 3.5f, .speed_var = 1.5f, .spread = 3.14159f, .size = 0.12f, .size_end = 0.02f,
                                 .color = 0xFFF0A0, .color_end = 0xFFB020, .glow = 2, .additive = true, .drag = 2, .burst = 14 };
static const arc_fx FX_GOO = { .life = 0.7f, .speed = 4, .speed_var = 2, .spread = 1.2f, .size = 0.16f, .size_end = 0.05f,
                               .color = 0xB060F0, .gravity = { 0, -14, 0 }, .bounce = 0.3f, .burst = 16 };
static const arc_fx FX_POOF = { .life = 0.6f, .speed = 2.5f, .spread = 3.14159f, .radius = 0.2f, .size = 0.35f, .size_end = 0.7f,
                                .color = 0xFFFFFF, .alpha = 0.8f, .drag = 3, .burst = 16 };
static const arc_fx FX_CONFETTI = { .life = 2.5f, .life_var = 0.6f, .speed = 8, .speed_var = 3, .spread = 0.5f, .size = 0.14f,
                                    .color = 0xFF5070, .color_end = 0x50D0FF, .gravity = { 0, -6, 0 }, .drag = 0.8f, .spin = 6,
                                    .spin_var = 6, .burst = 80 };

static char tile(int cx, int cy) {   /* cy counts up from the bottom row */
    int row = LH - 1 - cy;
    if (cx < 0 || cx >= LW) return '#';   /* walls at both ends */
    if (row < 0) return ' ';
    if (row >= LH) return '#';
    return LEVEL[row][cx];
}
static bool solid(int cx, int cy) { return tile(cx, cy) == '#'; }
static arc_v2 find(char c) {
    for (int r = 0; r < LH; r++) for (int x = 0; x < LW; x++) if (LEVEL[r][x] == c) return arc_v2_make((float)x + 0.5f, (float)(LH - 1 - r));
    return arc_v2_make(1.5f, 2);
}
static arc_v3 at(float x, float y) { return arc_v3_make(x, y, 0); }

static void move_and_collide(float dx, float dy) {
    G.p.x += dx;
    int y0 = (int)floorf(G.p.y + 0.01f), y1 = (int)floorf(G.p.y + PH - 0.01f);
    if (dx > 0) { int cx = (int)floorf(G.p.x + PW); for (int cy = y0; cy <= y1; cy++) if (solid(cx, cy)) { G.p.x = (float)cx - PW - 0.001f; G.v.x = 0; break; } }
    if (dx < 0) { int cx = (int)floorf(G.p.x - PW); for (int cy = y0; cy <= y1; cy++) if (solid(cx, cy)) { G.p.x = (float)(cx + 1) + PW + 0.001f; G.v.x = 0; break; } }
    G.p.y += dy; G.on_ground = false;
    int x0 = (int)floorf(G.p.x - PW + 0.01f), x1 = (int)floorf(G.p.x + PW - 0.01f);
    if (dy <= 0) { int cy = (int)floorf(G.p.y); for (int cx = x0; cx <= x1; cx++) if (solid(cx, cy)) { G.p.y = (float)(cy + 1); G.v.y = 0; G.on_ground = true; break; } }
    else { int cy = (int)floorf(G.p.y + PH); for (int cx = x0; cx <= x1; cx++) if (solid(cx, cy)) { G.p.y = (float)cy - PH; G.v.y = 0; break; } }
}
static bool touching(char c) {
    int x0 = (int)floorf(G.p.x - PW + 0.1f), x1 = (int)floorf(G.p.x + PW - 0.1f), y0 = (int)floorf(G.p.y + 0.05f), y1 = (int)floorf(G.p.y + PH - 0.1f);
    for (int cy = y0; cy <= y1; cy++) for (int cx = x0; cx <= x1; cx++) if (tile(cx, cy) == c) return true;
    return false;
}
static void respawn(void) {
    arc_fx_burst(&FX_POOF, at(G.p.x, G.p.y + 0.5f), arc_v3_make(0, 1, 0), 0);
    arc_popup_text(at(G.p.x, G.p.y + 1.4f), 0xFF8080, 3, "OUCH!");
    G.p = G.checkpoint; G.v = arc_v2_make(0, 0); G.deaths++; G.hurt_flash = 20; G.coin_streak = 0;
    arc_play_var(s_hurt, 0.6f, 0, 0.05f); shake_t = 0.35f;
    arc_fx_burst(&FX_POOF, at(G.p.x, G.p.y + 0.5f), arc_v3_make(0, 1, 0), 10);
}
static void reset(void) {
    memset(&G, 0, sizeof G); G.p = G.checkpoint = find('P'); G.face = 1; G.squash = 1; G.cam_x = G.p.x;
    for (int r = 0; r < LH; r++) for (int x = 0; x < LW; x++)
        if (LEVEL[r][x] == 's' && G.nslimes < MAX_SLIMES) G.s[G.nslimes++] = (Slime){ true, (float)x + 0.5f, (float)(LH - 1 - r), -1, 0 };
}
static int count_slimes(void) { int n = 0; for (int i = 0; i < G.nslimes; i++) n += G.s[i].alive; return n; }

static void tick_slimes(float dt) {
    for (int i = 0; i < G.nslimes; i++) {
        Slime *s = &G.s[i];
        if (!s->alive) { if (s->squish > 0) s->squish--; continue; }
        /* walk; turn at walls and at the edge of the ground */
        float nx = s->x + (float)s->dir * *SLIME_SPEED * dt;
        int ahead = (int)floorf(nx + (float)s->dir * 0.4f), row = (int)floorf(s->y + 0.1f);
        if (solid(ahead, row) || !solid(ahead, row - 1) || tile(ahead, row) == '^') s->dir = -s->dir;
        else s->x = nx;
        /* the player: from above squashes it, from the side hurts */
        float dx = G.p.x - s->x, dy = G.p.y - s->y;
        if (fabsf(dx) < PW + 0.35f && dy < 0.6f && dy > -PH) {
            if (G.v.y < 0 && dy > 0.25f) {
                s->alive = false; s->squish = 30; G.stomps++;
                G.v.y = *JUMP * 0.75f; G.squash = 1.25f;   /* bounce off */
                arc_fx_burst(&FX_GOO, at(s->x, s->y + 0.3f), arc_v3_make(0, 1, 0), 0);
                arc_popup_text(at(s->x, s->y + 1.2f), 0xD090FF, 3, "STOMP!");
                arc_play_var(s_stomp, 0.6f, 0, 0.1f); shake_t = 0.15f;
            } else { respawn(); return; }
        }
    }
}
static void init(void) {
    RUN = arc_var("run_speed", &v_run, 7); JUMP = arc_var("jump_speed", &v_jump, 12.5f); GRAVITY = arc_var("gravity", &v_grav, 34);
    SLIME_SPEED = arc_var("slime_speed", &v_slime, 1.4f);
    m_block = arc_mesh_load("assets/meshes/block.arcm"); arc_mesh_set_name(m_block, "ground");
    m_earth = arc_mesh_cube(0xFFFFFF); arc_mesh_set_name(m_earth, "earth");
    arc_mesh_set_texture(m_earth, arc_texture_load_ex("assets/textures/earth.jpg", ARC_TEX_MESH));
    m_coin = arc_mesh_load("assets/meshes/coin.arcm"); arc_mesh_set_name(m_coin, "coin");
    m_spike = arc_mesh_load("assets/meshes/spikes.arcm"); arc_mesh_set_name(m_spike, "spike");
    m_flag = arc_mesh_load("assets/meshes/flag.arcm"); arc_mesh_set_name(m_flag, "flag");
    m_slime = arc_mesh_load("assets/meshes/slime.arcm"); arc_mesh_set_name(m_slime, "slime");
    m_player = arc_mesh_load("assets/meshes/hero.arcm"); arc_mesh_set_name(m_player, "player");
    a_player = arc_anim_load("assets/meshes/hero.arca");
    c_idle = arc_anim_find(a_player, "idle"); c_run = arc_anim_find(a_player, "run");
    c_jump = arc_anim_find(a_player, "jump"); c_win = arc_anim_find(a_player, "win");
    s_jump = arc_sound_load("assets/sfx/jump.mp3"); s_coin = arc_sound_load("assets/sfx/coin.mp3");
    s_stomp = arc_sound_load("assets/sfx/stomp.mp3"); s_hurt = arc_sound_load("assets/sfx/hurt.mp3");
    s_check = arc_sound_load("assets/sfx/check.mp3"); s_win = arc_sound_load("assets/sfx/win.mp3");
    music = arc_music_load("assets/music/music.mp3"); arc_music_set_volume(music, 0.4f); arc_music_play(music, true);
    arc_set_post(&(arc_post){ .bloom = 0.3f, .bloom_threshold = 1.5f, .tonemap = true, .vignette = 0.25f, .contrast = 0.06f, .fxaa = true });
    for (int r = 0; r < LH; r++) for (int x = 0; x < LW; x++) total_coins += LEVEL[r][x] == 'o';
    reset();
}
static void tick(void) {
    if (arc_key_pressed(ARC_KEY_R)) reset();
    G.anim_t += arc_dt();
    G.squash = arc_lerpf(G.squash, 1, 0.18f);
    if (G.hurt_flash > 0) G.hurt_flash--;
    if (G.coin_timer > 0 && --G.coin_timer == 0) G.coin_streak = 0;
    G.cam_x = arc_lerpf(G.cam_x, arc_clampf(G.p.x + (float)G.face * 2.0f, 9.5f, LW - 9.5f), 0.08f);   /* lead where you run */
    if (G.phase != PHASE_PLAY) { G.win_t += arc_dt(); return; }
    G.ticks++;
    float dt = arc_dt();
    float in = (arc_key_down(ARC_KEY_D) || arc_key_down(ARC_KEY_RIGHT) ? 1.0f : 0.0f) - (arc_key_down(ARC_KEY_A) || arc_key_down(ARC_KEY_LEFT) ? 1.0f : 0.0f);
    if (in != 0) G.face = in > 0 ? 1 : -1;
    float target = in * *RUN, accel = (G.on_ground ? 60.0f : 30.0f) * dt;
    G.v.x += arc_clampf(target - G.v.x, -accel, accel);
    bool jump_down = arc_key_down(ARC_KEY_SPACE) || arc_key_down(ARC_KEY_W) || arc_key_down(ARC_KEY_UP);
    bool jump_pressed = arc_key_pressed(ARC_KEY_SPACE) || arc_key_pressed(ARC_KEY_W) || arc_key_pressed(ARC_KEY_UP);
    G.coyote = G.on_ground ? 6 : (G.coyote > 0 ? G.coyote - 1 : 0);
    G.buffer = jump_pressed ? 6 : (G.buffer > 0 ? G.buffer - 1 : 0);
    if (G.buffer > 0 && G.coyote > 0) {
        G.v.y = *JUMP; G.buffer = G.coyote = 0; G.squash = 1.3f;
        arc_play_var(s_jump, 0.35f, 0, 0.08f);
        arc_fx_burst(&FX_DUST, at(G.p.x, G.p.y + 0.05f), arc_v3_make(0, 1, 0), 0);
    }
    if (!jump_down && G.v.y > 0) G.v.y *= 0.55f;   /* let go early: a shorter jump */
    G.v.y = fmaxf(G.v.y - *GRAVITY * dt, -20.0f);
    bool was_ground = G.on_ground; float fall = G.v.y;
    move_and_collide(G.v.x * dt, G.v.y * dt);
    if (G.on_ground && !was_ground && fall < -6) {   /* landing: squash and a puff, harder the faster the fall */
        G.squash = arc_clampf(1.0f + fall * 0.025f, 0.6f, 0.95f);
        arc_fx_burst(&FX_DUST, at(G.p.x, G.p.y + 0.05f), arc_v3_make(0, 1, 0), (int)(-fall * 0.6f));
    }
    if (G.on_ground && fabsf(G.v.x) > 4 && G.ticks % 9 == 0)
        arc_fx_burst(&FX_DUST, at(G.p.x - (float)G.face * 0.2f, G.p.y + 0.05f), arc_v3_make(-(float)G.face, 1, 0), 2);
    tick_slimes(dt);
    /* pick-ups and hazards */
    int x0 = (int)floorf(G.p.x - PW), x1 = (int)floorf(G.p.x + PW), y0 = (int)floorf(G.p.y), y1 = (int)floorf(G.p.y + PH);
    for (int cy = y0; cy <= y1; cy++) for (int cx = x0; cx <= x1; cx++) {
        int row = LH - 1 - cy;
        if (tile(cx, cy) == 'o' && row >= 0 && row < LH && !G.taken[row][cx]) {
            G.taken[row][cx] = true; G.coins++;
            float pitch = 1.0f + 0.06f * (float)(G.coin_streak < 12 ? G.coin_streak : 12);   /* a chain climbs the scale */
            arc_play_ex(s_coin, 0.4f, 0, pitch, false); G.coin_streak++; G.coin_timer = 60;
            arc_fx_burst(&FX_SPARK, at((float)cx + 0.5f, (float)cy + 0.5f), arc_v3_make(0, 1, 0), 0);
            arc_popup_text(at((float)cx + 0.5f, (float)cy + 1.1f), 0xFFE060, 2.5f, arc_fmt("%d/%d", G.coins, total_coins));
        }
    }
    if (touching('C') && G.checkpoint.x != find('C').x) {
        G.checkpoint = find('C');
        arc_play(s_check, 0.6f, 0); arc_popup_text(at(G.checkpoint.x, G.checkpoint.y + 2.2f), 0x80E0FF, 3, "CHECKPOINT");
        arc_fx_burst(&FX_SPARK, at(G.checkpoint.x, G.checkpoint.y + 1.5f), arc_v3_make(0, 1, 0), 30);
    }
    if (touching('^') || G.p.y < -3) { respawn(); return; }
    if (touching('F')) {
        G.phase = PHASE_WON; G.v = arc_v2_make(0, 0); G.face = 1;
        arc_play(s_win, 0.7f, 0); arc_music_set_volume(music, 0.15f);
        arc_fx_burst(&FX_CONFETTI, at(G.p.x, G.p.y + 1), arc_v3_make(0, 1, 0), 0);
        arc_event("won", "\"coins\":%d,\"deaths\":%d,\"ticks\":%d", G.coins, G.deaths, G.ticks);
    }
}
static void draw(float alpha) {
    (void)alpha;
    float sx = 0, sy = 0;
    if (shake_t > 0) { shake_t -= 1.0f / 60.0f; float k = shake_t * 0.6f; sx = k * sinf((float)arc_tick_count() * 2.1f); sy = k * cosf((float)arc_tick_count() * 2.7f); }
    float cx = G.cam_x + sx, cy = arc_clampf(G.p.y * 0.35f + 2.8f, 3.4f, 5.0f) + sy;
    arc_camera cam = { { cx, cy + 1.6f, 13 }, { cx, cy, 0 }, { 0, 1, 0 }, 0.7f, 0, 0.1f, 120 };
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.3f, 1, 0.6f), 0xFFF4E0, 0x5A6A80);
    arc_set_sky(0x4890E0, 0xC0DCF4);
    arc_set_fog(0xB0D0F0, 18, 60);
    arc_set_shadows(arc_v3_make(cx, 2, 0), 12, 1024);
    float t = (float)arc_tick_count() * arc_dt();
    /* far hills: big blocks in the fog */
    for (int i = -2; i < 12; i++) {
        float hx = (float)i * 8.0f + 3.0f, hh = 1.5f + 1.5f * sinf((float)i * 1.7f);
        arc_draw_mesh(m_block, arc_m4_trs_y(arc_v3_make(hx, -2.0f, -30.0f), 0.3f * (float)(i % 3), arc_v3_make(8, hh + 5, 8)), 0x8098B8);
    }
    for (int r = 0; r < LH; r++) for (int x = 0; x < LW; x++) {
        char c = LEVEL[r][x]; float y = (float)(LH - 1 - r);
        if (fabsf((float)x - cx) > 15) continue;   /* only what the camera can see */
        if (c == '#') {
            if (solid(x, (int)y + 1)) arc_draw_mesh(m_earth, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y + 0.5f, 0), 0, arc_v3_make(1, 1, 1)), 0xFFFFFF);
            else arc_draw_mesh(m_block, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y, 0), 0, arc_v3_make(1, 1, 1)), 0xFFFFFF);
        }
        else if (c == 'o' && !G.taken[r][x])
            arc_draw_mesh_ex(m_coin, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y + 0.25f + 0.08f * sinf(t * 3 + (float)x), 0), t * 3 + (float)x, arc_v3_make(1, 1, 1)), 0xFFC830, 0.35f);   /* looks: warm gold glow so coins read against the sky */
        else if (c == '^') arc_draw_mesh(m_spike, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y, 0), 0, arc_v3_make(1, 1, 1)), 0xFFFFFF);
        else if (c == 'F') arc_draw_mesh(m_flag, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y, 0), -0.6f, arc_v3_make(1, 1, 1)), 0xFFFFFF);
        else if (c == 'C') {
            bool lit = G.checkpoint.x == (float)x + 0.5f;
            arc_draw_mesh_styled(m_flag, arc_m4_trs_y(arc_v3_make((float)x + 0.5f, y, 0), -0.6f, arc_v3_make(0.75f, 0.75f, 0.75f)),
                                 &(arc_mesh_style){ .tint = lit ? 0x60E0FF : 0x808080, .recolour = 0.8f, .emissive = lit ? 0.4f : 0 });
        }
    }
    for (int i = 0; i < G.nslimes; i++) {
        const Slime *s = &G.s[i];
        if (!s->alive && s->squish <= 0) continue;
        float wob = s->alive ? 0.08f * sinf(t * 8 + (float)i) : 0, flat = s->alive ? 1 : (float)s->squish / 30.0f * 0.3f;
        arc_draw_mesh(m_slime, arc_m4_trs_y(arc_v3_make(s->x, s->y, 0), arc_yaw_to((float)s->dir, 0.6f),
                                            arc_v3_make(1 + wob + (1 - flat) * 0.6f, (1 - wob) * flat, 1 + wob)), 0xFFFFFF);
    }
    /* the hero: animation by state, squash and stretch, turned a little toward the camera */
    arc_anim_layer L[2]; int n = 0;
    if (G.phase == PHASE_WON) L[n++] = (arc_anim_layer){ c_win, G.win_t, 1, true };
    else if (!G.on_ground) L[n++] = (arc_anim_layer){ c_jump, G.anim_t, 1, true };
    else {
        float run = arc_clampf(fabsf(G.v.x) / *RUN, 0, 1);
        L[n++] = (arc_anim_layer){ c_idle, G.anim_t, 1 - run + 0.001f, true };
        L[n++] = (arc_anim_layer){ c_run, G.anim_t * (0.6f + 0.6f * run), run, true };
    }
    static arc_pose pose; arc_anim_pose(a_player, L, n, &pose);
    float yaw = G.phase == PHASE_WON ? 0 : arc_yaw_to((float)G.face, 0.45f), sq = G.squash;
    arc_draw_mesh_posed_styled(m_player, arc_m4_trs_y(arc_v3_make(G.p.x, G.p.y, 0), yaw, arc_v3_make(1.0f / sqrtf(sq), sq, 1.0f / sqrtf(sq))),
                               a_player, &pose, &(arc_mesh_style){ .whiten = G.hurt_flash > 12 ? 0.7f : 0 });
    arc_draw_particles();
    float Wd = (float)arc_width(), Hd = (float)arc_height();
    arc_draw_text_ex(12, 10, 3, 0xFFE060, ARC_TEXT_OUTLINE, arc_fmt("COINS %d/%d", G.coins, total_coins));
    arc_draw_text_ex(12, 40, 2, 0xFFFFFF, ARC_TEXT_OUTLINE, arc_fmt("DEATHS %d   TIME %.1f", G.deaths, (float)G.ticks * arc_dt()));
    if (G.hurt_flash > 0) arc_draw_rect(0, 0, Wd, Hd, arc_rgba(0xFF3030, 0.3f * (float)G.hurt_flash / 20.0f));
    if (G.phase == PHASE_WON) {
        float sc = 4.0f * arc_ease_out_back(arc_clampf(G.win_t * 2.5f, 0, 1));
        arc_draw_text_ex(Wd / 2.0f, Hd / 3.0f, sc, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE, "LEVEL CLEAR - R to play again");
        arc_draw_text_ex(Wd / 2.0f, Hd / 3.0f + 50, 2.5f, 0xFFE060, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE,
                         arc_fmt("COINS %d/%d   DEATHS %d   TIME %.1f", G.coins, total_coins, G.deaths, (float)G.ticks * arc_dt()));
    }
}
static void dump(arc_json *j) {
    arc_json_str_k(j, "phase", G.phase == PHASE_WON ? "won" : "play");
    arc_json_num_k(j, "x", G.p.x); arc_json_num_k(j, "y", G.p.y); arc_json_num_k(j, "vx", G.v.x); arc_json_num_k(j, "vy", G.v.y);
    arc_json_bool_k(j, "on_ground", G.on_ground); arc_json_num_k(j, "coins", G.coins); arc_json_num_k(j, "total_coins", total_coins);
    arc_json_num_k(j, "slimes", count_slimes()); arc_json_num_k(j, "stomps", G.stomps);
    arc_json_num_k(j, "deaths", G.deaths); arc_json_num_k(j, "checkpoint_x", G.checkpoint.x); arc_json_num_k(j, "ticks", G.ticks);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "tp") && argc >= 2) { G.p = arc_v2_make((float)atof(argv[0]), (float)atof(argv[1])); G.v = arc_v2_make(0, 0); return true; }
    if (!strcmp(name, "kill")) { respawn(); return true; }
    if (!strcmp(name, "win")) { G.p = find('F'); return true; }
    if (!strcmp(name, "restart")) { reset(); return true; }
    return false;
}
static size_t save_state(void *buf, size_t cap) { if (cap >= sizeof G) memcpy(buf, &G, sizeof G); return sizeof G; }
static bool load_state(const void *buf, size_t size) { if (size != sizeof G) return false; memcpy(&G, buf, sizeof G); return true; }

arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "{title}", .width = 960, .height = 540, .clear_color = 0x88B0E0,
                      .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state };
}
