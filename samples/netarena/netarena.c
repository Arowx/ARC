/* netarena: a small real-time arena shooter for ARC 0.8.1 rollback multiplayer (and its tests).
   2-4 ships in a walled arena: turn, thrust, fire. A ship hit three times blows up (a point for the shooter) and comes
   back after 1.5 s at a random spawn point; health pickups appear at random places. Everything random comes from
   arc_rand_* (the match generator), the maths from ARC's deterministic functions (arc_sinf, arc_cosf, arc_sqrtf).
   The rules read nothing but arc_net_input(player): each tick's input is one byte (left, right, thrust, fire) packed by
   net_input() from the keys, the pad, or the built-in bot. Sounds, particles and arc_event calls in tick() exercise
   ARC's rollback side-effect handling; save_state / load_state hold the whole rules state.

     build:   ./build.sh headless samples/netarena/netarena.c     (or linux / win)
     host:    build/netarena_headless --headless --cmd "net host 47950; net wait players 2; net start; bot 1; net wait tick 600; ..."
     join:    build/netarena_headless --headless --cmd "net join localhost:47950; bot 2; ..."
     window:  build/netarena: the lobby (host / LAN games / join); keys A/D or LEFT/RIGHT turn, W/UP thrust, SPACE fire;
              O in the lobby plays offline against a simple computer ship.
   Agent commands: bot N (this machine's ship plays itself, style N) | bot off | cheat P (changes ship P on this machine
   only: a desync on purpose) | offline (a match against the computer, no network). */
#include "../../include/arc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXS 4
#define MAXB 64
#define MAXP 4
#define ARENA 20.0f                       /* the arena is -ARENA..ARENA on x and y */
enum { IN_LEFT = 1, IN_RIGHT = 2, IN_THRUST = 4, IN_FIRE = 8 };
typedef struct { float x, y, vx, vy, ang; int32_t hp, score, cool, respawn, inv, alive; } ship;
typedef struct { float x, y, vx, vy; int32_t life, owner, used; } bullet;
typedef struct { float x, y; int32_t alive, timer; } pickup;
typedef struct {                          /* the rules state: everything tick() changes (save_state writes exactly this) */
    int32_t started, players, offline, tick, shots, hits, kills, pickups_taken, sounds, next_pickup;
    ship s[MAXS]; bullet b[MAXB]; pickup pk[MAXP];
} rules;
static rules S;
static struct {                           /* this machine only: sounds, effects, the bot, the screen */
    arc_sound s_shoot, s_boom, s_pick;
    arc_fx boom, spark;
    int bot; arc_rng bot_rng; int bot_turn_until, bot_dir;
    bool lobby_done;
} P;
static float v_turn = 0.07f, v_accel = 0.018f, v_drag = 0.985f, v_max = 0.45f, v_bullet = 0.9f;

static const float spawn[8][2] = { { -15, -15 }, { 15, 15 }, { 15, -15 }, { -15, 15 }, { 0, -16 }, { 0, 16 }, { -16, 0 }, { 16, 0 } };

/* ---------------- rules ---------------- */
static void place_ship(int i) {
    ship *s = &S.s[i];
    int k = arc_rand_range(0, 7);
    s->x = spawn[k][0]; s->y = spawn[k][1]; s->vx = s->vy = 0;
    s->ang = arc_atan2f(-s->y, -s->x);   /* towards the middle */
    s->hp = 3; s->alive = 1; s->inv = 60; s->respawn = 0; s->cool = 0;
}
static void setup_match(int players, int offline) {
    memset(&S, 0, sizeof S);
    S.started = 1; S.players = players < 2 ? 2 : players > MAXS ? MAXS : players; S.offline = offline; S.next_pickup = 120;
    for (int i = 0; i < S.players; i++) {
        place_ship(i);
        S.s[i].x = spawn[i][0]; S.s[i].y = spawn[i][1]; S.s[i].ang = arc_atan2f(-S.s[i].y, -S.s[i].x);   /* first time: their own corners */
    }
}
static int play(arc_sound s, float x) { arc_play(s, 0.4f, arc_clampf(x / ARENA, -1, 1)); return 1; }
static uint8_t offline_ai(int me) {      /* (offline only: the computer's ship is part of the rules, from the state alone) */
    ship *s = &S.s[me], *t = &S.s[0];
    if (!s->alive || !t->alive) return 0;
    float want = arc_atan2f(t->y - s->y, t->x - s->x), d = arc_wrap_angle(want - s->ang);
    uint8_t in = d > 0.05f ? IN_LEFT : d < -0.05f ? IN_RIGHT : 0;
    if (arc_fabsf(d) < 0.3f) in |= IN_FIRE;
    if ((S.tick / 40) % 3 == 0) in |= IN_THRUST;
    return in;
}
static void rules_tick(void) {
    S.tick++;
    for (int i = 0; i < S.players; i++) {
        ship *s = &S.s[i];
        int n = 0; const uint8_t *inp = (const uint8_t *)arc_net_input(i, &n);
        uint8_t in = inp && n > 0 ? inp[0] : 0;
        if (S.offline && i > 0) in = offline_ai(i);
        if (!s->alive) {
            if (s->respawn > 0 && --s->respawn == 0) place_ship(i);
            continue;
        }
        if (in & IN_LEFT) s->ang += v_turn;
        if (in & IN_RIGHT) s->ang -= v_turn;
        s->ang = arc_wrap_angle(s->ang);
        float c = arc_cosf(s->ang), sn = arc_sinf(s->ang);
        if (in & IN_THRUST) { s->vx += c * v_accel; s->vy += sn * v_accel; }
        s->vx *= v_drag; s->vy *= v_drag;
        float sp = arc_sqrtf(s->vx * s->vx + s->vy * s->vy);
        if (sp > v_max) { s->vx *= v_max / sp; s->vy *= v_max / sp; }
        s->x += s->vx; s->y += s->vy;
        if (s->x < -ARENA) { s->x = -ARENA; s->vx = -s->vx * 0.5f; }
        if (s->x > ARENA) { s->x = ARENA; s->vx = -s->vx * 0.5f; }
        if (s->y < -ARENA) { s->y = -ARENA; s->vy = -s->vy * 0.5f; }
        if (s->y > ARENA) { s->y = ARENA; s->vy = -s->vy * 0.5f; }
        if (s->cool > 0) s->cool--;
        if (s->inv > 0) s->inv--;
        if ((in & IN_FIRE) && s->cool == 0) {
            for (int b = 0; b < MAXB; b++) if (!S.b[b].used) {
                bullet *u = &S.b[b]; u->used = 1; u->owner = i; u->life = 50;
                u->x = s->x + c * 1.2f; u->y = s->y + sn * 1.2f; u->vx = c * v_bullet + s->vx; u->vy = sn * v_bullet + s->vy;
                s->cool = 14; S.shots++; S.sounds += play(P.s_shoot, s->x);
                break;
            }
        }
        for (int k = 0; k < MAXP; k++) {
            pickup *p = &S.pk[k];
            if (!p->alive) continue;
            float dx = p->x - s->x, dy = p->y - s->y;
            if (dx * dx + dy * dy < 1.6f * 1.6f) {
                p->alive = 0; s->hp = arc_mini(s->hp + 1, 5); S.pickups_taken++; S.sounds += play(P.s_pick, p->x);
                arc_event("pickup", "\"ship\":%d,\"hp\":%d", i, s->hp);
            }
        }
    }
    for (int b = 0; b < MAXB; b++) {
        bullet *u = &S.b[b];
        if (!u->used) continue;
        u->x += u->vx; u->y += u->vy;
        if (--u->life <= 0 || u->x < -ARENA - 2 || u->x > ARENA + 2 || u->y < -ARENA - 2 || u->y > ARENA + 2) { u->used = 0; continue; }
        for (int i = 0; i < S.players; i++) {
            ship *s = &S.s[i];
            if (i == u->owner || !s->alive || s->inv > 0) continue;
            float dx = s->x - u->x, dy = s->y - u->y;
            if (dx * dx + dy * dy > 1.1f * 1.1f) continue;
            u->used = 0; S.hits++; s->hp--;
            arc_fx_burst(&P.spark, arc_v3_make(u->x, 0.5f, u->y), arc_v3_make(u->vx, 0.3f, u->vy), 0);
            if (s->hp <= 0) {
                s->alive = 0; s->respawn = 90; S.kills++; S.s[u->owner].score++;
                arc_fx_burst(&P.boom, arc_v3_make(s->x, 0.5f, s->y), arc_v3_make(0, 1, 0), 0);
                S.sounds += play(P.s_boom, s->x);
                arc_event("kill", "\"by\":%d,\"ship\":%d,\"score\":%d", u->owner, i, S.s[u->owner].score);
            }
            break;
        }
    }
    if (--S.next_pickup <= 0) {
        S.next_pickup = 240 + arc_rand_range(0, 120);
        for (int k = 0; k < MAXP; k++) if (!S.pk[k].alive) { S.pk[k].alive = 1; S.pk[k].x = arc_rand_rangef(-16, 16); S.pk[k].y = arc_rand_rangef(-16, 16); break; }
    }
}

/* ---------------- this machine's input ---------------- */
static uint8_t bot_input(int me) {        /* a simple bot: reads the state (perhaps predicted: it is only this machine's input) */
    ship *s = &S.s[me];
    if (!S.started || me < 0 || me >= S.players || !s->alive) return 0;
    int target = -1; float best = 1e9f;
    for (int i = 0; i < S.players; i++) if (i != me && S.s[i].alive) { float dx = S.s[i].x - s->x, dy = S.s[i].y - s->y, d = dx * dx + dy * dy; if (d < best) { best = d; target = i; } }
    uint8_t in = 0;
    if (P.bot_turn_until > S.tick) in |= P.bot_dir > 0 ? IN_LEFT : IN_RIGHT;
    else if (target >= 0) {
        float want = arc_atan2f(S.s[target].y - s->y, S.s[target].x - s->x), d = arc_wrap_angle(want - s->ang);
        in |= d > 0.06f ? IN_LEFT : d < -0.06f ? IN_RIGHT : 0;
        if (arc_fabsf(d) < 0.25f && arc_rng_range(&P.bot_rng, 0, 99) < 70) in |= IN_FIRE;
        if (best > 64.0f && arc_rng_range(&P.bot_rng, 0, 99) < 60) in |= IN_THRUST;
    }
    if (arc_rng_range(&P.bot_rng, 0, 99) < 2 + P.bot) { P.bot_turn_until = S.tick + arc_rng_range(&P.bot_rng, 5, 25); P.bot_dir = arc_rng_range(&P.bot_rng, 0, 1) ? 1 : -1; }
    if (arc_fabsf(s->x) > ARENA - 3 || arc_fabsf(s->y) > ARENA - 3) in |= IN_THRUST;
    return in;
}
static size_t net_input(void *buf, size_t cap) {
    (void)cap;
    uint8_t in = 0;
    if (P.bot) in = bot_input(arc_net_local_player());
    if (arc_key_down(ARC_KEY_A) || arc_key_down(ARC_KEY_LEFT) || arc_key_down(ARC_PAD_LEFT) || arc_pad_axis(ARC_AXIS_LX) < -0.4f) in |= IN_LEFT;
    if (arc_key_down(ARC_KEY_D) || arc_key_down(ARC_KEY_RIGHT) || arc_key_down(ARC_PAD_RIGHT) || arc_pad_axis(ARC_AXIS_LX) > 0.4f) in |= IN_RIGHT;
    if (arc_key_down(ARC_KEY_W) || arc_key_down(ARC_KEY_UP) || arc_key_down(ARC_PAD_A)) in |= IN_THRUST;
    if (arc_key_down(ARC_KEY_SPACE) || arc_key_down(ARC_PAD_RB) || arc_key_down(ARC_PAD_X)) in |= IN_FIRE;
    ((uint8_t *)buf)[0] = in;
    return 1;
}

/* ---------------- callbacks ---------------- */
static void net_event(const arc_net_ev *ev) {
    if (ev->type == ARC_NET_EV_START) {   /* match tick 0, on every machine (and again if a correction goes back to it) */
        arc_net_player ps[8]; int n = arc_net_players(ps, 8), in = 0;
        for (int i = 0; i < n; i++) in += ps[i].in_match;
        setup_match(in, 0);
    }
}
static size_t save_state(void *buf, size_t cap) { arc_writer w = { (uint8_t *)buf, cap, 0 }; arc_write(&w, &S, sizeof S); return w.len; }
static bool load_state(const void *buf, size_t n) { arc_reader r = { (const uint8_t *)buf, n, 0, false }; arc_read(&r, &S, sizeof S); return !r.fail; }
static uint32_t rules_hash(void) { uint32_t h = 2166136261u; const uint8_t *p = (const uint8_t *)&S; for (size_t i = 0; i < sizeof S; i++) { h ^= p[i]; h *= 16777619u; } return h; }
static void dump(arc_json *j) {
    arc_json_int_k(j, "started", S.started); arc_json_int_k(j, "tick", S.tick); arc_json_int_k(j, "players", S.players);
    arc_json_int_k(j, "shots", S.shots); arc_json_int_k(j, "hits", S.hits); arc_json_int_k(j, "kills", S.kills);
    arc_json_int_k(j, "pickups", S.pickups_taken); arc_json_int_k(j, "sounds", S.sounds);
    arc_json_str_k(j, "rules_hash", arc_fmt("%08x", rules_hash()));
    arc_json_arr_k(j, "ships");
    for (int i = 0; i < S.players; i++) {
        ship *s = &S.s[i]; arc_json_obj(j);
        arc_json_num_k(j, "x", s->x); arc_json_num_k(j, "y", s->y); arc_json_num_k(j, "ang", s->ang);
        arc_json_int_k(j, "hp", s->hp); arc_json_int_k(j, "score", s->score); arc_json_int_k(j, "alive", s->alive);
        arc_json_end(j);
    }
    arc_json_end(j);
}
static bool command(const char *name, int argc, const char **argv) {
    if (!strcmp(name, "bot")) {
        P.bot = argc > 0 && strcmp(argv[0], "off") ? arc_maxi(1, atoi(argv[0])) : argc > 0 ? 0 : 1;
        arc_rng_seed(&P.bot_rng, (uint64_t)P.bot * 7919u + (uint64_t)arc_net_local_player(), 11);
        return true;
    }
    if (!strcmp(name, "cheat") && argc >= 1) { int p = atoi(argv[0]); if (p >= 0 && p < MAXS) { S.s[p].hp += 5; S.s[p].x += 1.0f; } return true; }   /* this machine only */
    if (!strcmp(name, "offline")) { setup_match(2, 1); P.lobby_done = true; return true; }
    return false;
}
static void init(void) {
    arc_net_desc d; memset(&d, 0, sizeof d);
    d.game = "netarena"; d.version = "1.0"; d.max_players = 4; d.mode = ARC_NET_ROLLBACK;
    arc_net_setup(&d);
    arc_sfx shoot = { ARC_WAVE_SQUARE, 880, 220, 0.002f, 0.03f, 0.08f, 0.25f, 0.3f, 0, 0 };
    arc_sfx boom = { ARC_WAVE_NOISE, 300, 40, 0.005f, 0.1f, 0.4f, 0.5f, 0, 0, 0 };
    arc_sfx pick = { ARC_WAVE_SINE, 520, 1040, 0.005f, 0.06f, 0.1f, 0.35f, 0, 0, 0 };
    P.s_shoot = arc_sound_make(&shoot); P.s_boom = arc_sound_make(&boom); P.s_pick = arc_sound_make(&pick);
    memset(&P.boom, 0, sizeof P.boom);
    P.boom.life = 0.7f; P.boom.speed = 5; P.boom.spread = 3.2f; P.boom.size = 0.5f; P.boom.size_end = 0.1f; P.boom.color = 0xFFB040; P.boom.color_end = 0xFF2010;
    P.boom.additive = true; P.boom.drag = 2; P.boom.burst = 40; P.boom.glow = 1;
    P.spark = P.boom; P.spark.burst = 10; P.spark.speed = 3; P.spark.size = 0.25f; P.spark.color = 0xC0E0FF; P.spark.color_end = 0x4060FF; P.spark.life = 0.35f;
    arc_var("turn", &v_turn, v_turn); arc_var("accel", &v_accel, v_accel);
    if (arc_arg_flag("offline")) command("offline", 0, NULL);
}
static void tick(void) {
    if (!S.started) {
        if (!arc_headless() && !P.lobby_done) {
            int r = arc_net_lobby(NULL);
            if (r == ARC_NET_LOBBY_BACK || arc_key_pressed(ARC_KEY_O)) command("offline", 0, NULL);
        }
        return;
    }
    rules_tick();
}
static const uint32_t ship_col[MAXS] = { 0x40C0FF, 0xFF6040, 0x60FF60, 0xFFE040 };
static void draw(float alpha) {
    (void)alpha;
    arc_camera cam = { { 0, 46, 0.01f }, { 0, 0, 0 }, { 0, 1, 0 }, 0.85f, 0, 0.5f, 200 };
    arc_set_camera(&cam);
    arc_draw_box(arc_v3_make(-ARENA - 1, -0.2f, -ARENA - 1), arc_v3_make(ARENA + 1, 0, ARENA + 1), 0x182028);
    float e = ARENA + 1;
    arc_draw_line(arc_v3_make(-e, 0.05f, -e), arc_v3_make(e, 0.05f, -e), 0x6080A0); arc_draw_line(arc_v3_make(e, 0.05f, -e), arc_v3_make(e, 0.05f, e), 0x6080A0);
    arc_draw_line(arc_v3_make(e, 0.05f, e), arc_v3_make(-e, 0.05f, e), 0x6080A0); arc_draw_line(arc_v3_make(-e, 0.05f, e), arc_v3_make(-e, 0.05f, -e), 0x6080A0);
    for (int i = 0; i < S.players && S.started; i++) {
        ship *s = &S.s[i];
        if (!s->alive) continue;
        if (s->inv > 0 && (s->inv / 4) % 2) continue;
        float c = cosf(s->ang), sn = sinf(s->ang);
        arc_v3 nose = arc_v3_make(s->x + c * 1.2f, 0.4f, s->y + sn * 1.2f);
        arc_v3 l = arc_v3_make(s->x - c * 0.8f - sn * 0.7f, 0.4f, s->y - sn * 0.8f + c * 0.7f), r = arc_v3_make(s->x - c * 0.8f + sn * 0.7f, 0.4f, s->y - sn * 0.8f - c * 0.7f);
        arc_draw_tri(nose, r, l, ship_col[i]); arc_draw_tri(nose, l, r, ship_col[i]);
    }
    for (int b = 0; b < MAXB; b++) if (S.b[b].used) arc_draw_cube(arc_v3_make(S.b[b].x, 0.4f, S.b[b].y), arc_v3_make(0.35f, 0.35f, 0.35f), 0xFFFFFF);
    for (int k = 0; k < MAXP; k++) if (S.pk[k].alive) arc_draw_sphere(arc_v3_make(S.pk[k].x, 0.5f, S.pk[k].y), 0.6f, 0x60FF90);
    arc_draw_particles();
    float k = arc_dpi_scale();
    if (S.started) {
        for (int i = 0; i < S.players; i++) arc_draw_textf(10 * k + (float)i * 230 * k, (float)arc_height() - 30 * k, 2 * k, ship_col[i], "P%d %d  hp %d", i + 1, S.s[i].score, S.s[i].hp);
        int w = arc_net_waiting_for();
        if (w >= 0) arc_draw_text((float)arc_width() * 0.5f - 160 * k, 40 * k, 2.5f * k, 0xFFE070, arc_fmt("Waiting for player %d...", w + 1));
    }
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "netarena", .width = 960, .height = 720, .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state, .net_event = net_event, .net_input = net_input, .dev_keys = true };
}
