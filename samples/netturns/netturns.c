/* netturns: a small alternating-turn tactics game for ARC 0.8 multiplayer (and its tests).
   2-4 players, two units each, on an 8x8 grid with a few walls. On your turn each unit may move (up to 3 steps) and
   attack (an adjacent enemy). Attacks from behind hit harder: facing, hit chance and damage use ARC's deterministic
   maths (arc_atan2f, arc_cosf, arc_powf...) and the match's random numbers (arc_rand_* inside command()).
   Every rule change goes through arc_net_command -> command(); tick() only animates (the display positions are not
   game state and are not in save_state).

     build:   ./build.sh headless samples/netturns/netturns.c      (or linux / win)
     host:    build/netturns_headless --headless --stdin             then: net host; net wait players 2; net start
     join:    build/netturns_headless --headless --stdin             then: net join localhost
     play:    ai (the computer picks this player's moves); net end_turn; or by hand: net cmd move 0 2 3, net cmd attack 0 5
     window:  build/netturns: the lobby (host / LAN games / join), then click a unit, click a cell to move, an enemy to
              attack, SPACE (or the End turn button) to end the turn.
   Debug: `cheat U HP` changes a unit's HP on this machine only (a typed command, not sent): the next command shows a
   desync on every machine.
   0.8.5: online through arc_relay:  --net-relay ws://127.0.0.1:47880, then `net host_online` / `net join_online CODE`
   (the lobby shows Host online / Join by code). --simultaneous: simultaneous turns (every player moves its own units,
   the turn ends when all have ended it). `order TEXT` (sent: net cmd order TEXT) records an order in the rules state, in
   the order the host / relay numbered them (tests of the sequence). --set netturns.hp=N: units start with N HP (10). */
#include "../../include/arc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GW 8
#define GH 8
#define MAXU 8
typedef struct { int8_t x, y, hp, owner; float facing; uint8_t moved, acted, alive, pad; } unit;
typedef struct {                                   /* the rules state: everything here changes only in command() */
    int started, players, nunits, winner, turn, owner, moves, attacks, hits;
    uint8_t wall[GH][GW];
    unit u[MAXU];
    char chat[64];
    float total_damage;                            /* float maths in the rules, so a platform difference would show */
    int orders; uint32_t order_hash; char order_log[96];   /* `order` commands as applied (0.8.5 tests) */
} rules;
static float unit_hp;                              /* tunable: netturns.hp (rules set-up: the same on every machine) */
static rules S;
static struct { float x[MAXU], y[MAXU]; int sel; int autoplay, played; } P;   /* presentation and this machine's input (not game state) */

/* ---------------- rules ---------------- */
static bool on_grid(int x, int y) { return x >= 0 && y >= 0 && x < GW && y < GH; }
static int unit_at(int x, int y) { for (int i = 0; i < S.nunits; i++) if (S.u[i].alive && S.u[i].x == x && S.u[i].y == y) return i; return -1; }
static bool passable(uint8_t t) { return t == 0; }
static arc_grid grid_view(void) {                  /* walls plus units as obstacles, for paths */
    static uint8_t cells[GW * GH]; arc_grid g = { GW, GH, cells };
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) cells[y * GW + x] = S.wall[y][x];
    for (int i = 0; i < S.nunits; i++) if (S.u[i].alive) cells[S.u[i].y * GW + S.u[i].x] = 2;
    return g;
}
static void setup_match(uint64_t seed, int players) {
    memset(&S, 0, sizeof S);
    S.started = 1; S.players = players < 2 ? 2 : players; S.winner = -1; S.turn = 1; S.owner = 0;
    arc_rng r; arc_rng_seed(&r, seed, 3);
    for (int k = 0; k < 6; k++) { int x = arc_rng_range(&r, 2, 5), y = arc_rng_range(&r, 1, 6); S.wall[y][x] = 1; }   /* the middle only */
    static const int8_t start[4][2][2] = { { { 0, 0 }, { 0, 2 } }, { { 7, 7 }, { 7, 5 } }, { { 7, 0 }, { 5, 0 } }, { { 0, 7 }, { 2, 7 } } };
    for (int p = 0; p < S.players && p < 4; p++) for (int k = 0; k < 2; k++) {
        unit *u = &S.u[S.nunits++]; u->x = start[p][k][0]; u->y = start[p][k][1]; u->hp = (int8_t)arc_clampi((int)unit_hp, 1, 120); u->owner = (int8_t)p; u->alive = 1;
        u->facing = arc_atan2f((float)(3.5f - u->y), (float)(3.5f - u->x));   /* towards the middle */
        S.wall[u->y][u->x] = 0;
    }
    for (int i = 0; i < S.nunits; i++) { P.x[i] = S.u[i].x; P.y[i] = S.u[i].y; }
    P.sel = -1;
}
static void check_winner(void) {
    int alive_owner = -1, owners = 0;
    for (int p = 0; p < S.players; p++) { bool any = false; for (int i = 0; i < S.nunits; i++) if (S.u[i].alive && S.u[i].owner == p) any = true; if (any) { owners++; alive_owner = p; } }
    if (owners == 1) S.winner = alive_owner;
}
static bool cmd_move(int from, int ui, int x, int y) {
    if (ui < 0 || ui >= S.nunits || !on_grid(x, y)) return false;
    unit *u = &S.u[ui];
    if (!u->alive || u->owner != from || u->moved || S.winner >= 0) return false;
    if (S.wall[y][x] || unit_at(x, y) >= 0) return false;
    arc_grid g = grid_view();
    int steps = arc_grid_path_length(&g, u->x, u->y, x, y, passable);
    if (steps < 1 || steps > 3) return false;
    u->facing = arc_atan2f((float)(y - u->y), (float)(x - u->x));
    u->x = (int8_t)x; u->y = (int8_t)y; u->moved = 1; S.moves++;
    return true;
}
static bool cmd_attack(int from, int ui, int ti) {
    if (ui < 0 || ui >= S.nunits || ti < 0 || ti >= S.nunits || S.winner >= 0) return false;
    unit *a = &S.u[ui], *t = &S.u[ti];
    if (!a->alive || !t->alive || a->owner != from || t->owner == from || a->acted) return false;
    float dx = (float)(a->x - t->x), dy = (float)(a->y - t->y), dist = arc_hypotf(dx, dy);
    if (dist > 1.5f) return false;
    a->acted = 1; S.attacks++;
    /* where the attacker stands seen from the target: 1 = in front of it, -1 = behind */
    float front = arc_cosf(arc_atan2f(dy, dx) - t->facing);
    float hit = 0.55f + 0.35f * (1.0f - front) * 0.5f + 0.1f * arc_expf(-dist);
    a->facing = arc_atan2f(-dy, -dx);
    if (arc_rand_float() >= hit) return true;      /* a miss is still an attack */
    float dmg = 2.0f + (float)arc_rand_range(0, 2) + arc_roundf(2.0f * arc_powf(0.5f * (1.0f - front), 0.75f)) + arc_floorf(arc_sqrtf(dist) * arc_logf(2.0f + dist));
    S.total_damage += dmg * 0.37f + arc_sinf(dmg) * 0.01f;
    t->hp = (int8_t)(t->hp - (int)dmg); S.hits++;
    if (t->hp <= 0) { t->hp = 0; t->alive = 0; }
    check_winner();
    return true;
}
/* the net event callback: sequenced events at their exact place in the command sequence */
static void net_event(const arc_net_ev *ev) {
    if (ev->type == ARC_NET_EV_START) {
        arc_net_player ps[ARC_NET_MAX_PLAYERS]; int n = arc_net_players(ps, ARC_NET_MAX_PLAYERS), in = 0;
        for (int i = 0; i < n; i++) in += ps[i].in_match;
        setup_match(arc_net_seed(), in);
        S.owner = arc_net_turn_owner();
    } else if (ev->type == ARC_NET_EV_CONNECTED) {
        P.played = 0;                              /* (re)connected: commands in flight may have been lost: play this turn again */
    } else if (ev->type == ARC_NET_EV_TURN && S.started) {
        S.turn = arc_net_turn(); S.owner = ev->player;   /* (simultaneous turns: ARC_NET_EVERYONE, every unit again) */
        for (int i = 0; i < S.nunits; i++) if (S.u[i].owner == ev->player || ev->player == ARC_NET_EVERYONE) { S.u[i].moved = 0; S.u[i].acted = 0; }
    }
}
static int player_slot(int net_player) {           /* net player index -> the side it plays (players in index order) */
    arc_net_player ps[ARC_NET_MAX_PLAYERS]; int n = arc_net_players(ps, ARC_NET_MAX_PLAYERS), k = 0;
    for (int i = 0; i < n; i++) { if (!ps[i].in_match) continue; if (ps[i].index == net_player) return k; k++; }
    return net_player;
}

/* ---------------- the computer player (for agents and tests): sends this machine's moves ---------------- */
static void ai_turn(void) {
    if (!S.started || S.winner >= 0 || !arc_net_my_turn()) return;
    int me = player_slot(arc_net_local_player());
    for (int i = 0; i < S.nunits; i++) {
        unit *u = &S.u[i]; if (!u->alive || u->owner != me) continue;
        int best = -1; float bd = 1e9f;
        for (int j = 0; j < S.nunits; j++) if (S.u[j].alive && S.u[j].owner != me) {
            float d = arc_hypotf((float)(S.u[j].x - u->x), (float)(S.u[j].y - u->y)); if (d < bd) { bd = d; best = j; }
        }
        if (best < 0) return;
        if (bd <= 1.5f && !u->acted) { arc_net_command(arc_fmt("attack %d %d", i, best)); continue; }
        if (u->moved) continue;
        /* the reachable free cell nearest the target (ties: lowest y, then x) */
        arc_grid g = grid_view(); int bx = -1, by = -1; float bdist = 1e9f;
        for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
            if (S.wall[y][x] || unit_at(x, y) >= 0) continue;
            int steps = arc_grid_path_length(&g, u->x, u->y, x, y, passable);
            if (steps < 1 || steps > 3) continue;
            float d = arc_hypotf((float)(S.u[best].x - x), (float)(S.u[best].y - y));
            if (d < bdist - 1e-4f) { bdist = d; bx = x; by = y; }
        }
        if (bx >= 0 && bdist < bd) arc_net_command(arc_fmt("move %d %d %d", i, bx, by));
    }
}

/* ---------------- ARC callbacks ---------------- */
static bool command(const char *name, int argc, const char **argv) {
    int from = arc_net_command_from();
    if (!strcmp(name, "ai")) { ai_turn(); return true; }                              /* typed: this machine's player */
    if (!strcmp(name, "autoplay")) { P.autoplay = argc ? atoi(argv[0]) : 1000; return true; }   /* the computer plays this machine's turns until turn N */
    if (!strcmp(name, "cheat") && argc >= 2 && from < 0) {                            /* typed, this machine only: a desync */
        int i = atoi(argv[0]); if (i >= 0 && i < S.nunits) S.u[i].hp = (int8_t)atoi(argv[1]); return true;
    }
    if (!strcmp(name, "order") && argc >= 1) {                                         /* recorded in sequence order */
        S.orders++;
        char t[48]; snprintf(t, sizeof t, "%d:%s ", from, argv[0]);
        for (const char *c = t; *c; c++) { S.order_hash ^= (uint8_t)*c; S.order_hash *= 16777619u; }
        strncat(S.order_log, t, sizeof S.order_log - strlen(S.order_log) - 1);
        return true;
    }
    if (!strcmp(name, "chat")) {                                                       /* sent with ARC_NET_FREE */
        char t[64] = ""; for (int i = 0; i < argc; i++) { strncat(t, argv[i], sizeof t - strlen(t) - 1); if (i + 1 < argc) strncat(t, " ", sizeof t - strlen(t) - 1); }
        snprintf(S.chat, sizeof S.chat, "%d: %s", from, t); return true;
    }
    int side = from >= 0 ? player_slot(from) : arc_net_turn_owner() >= 0 ? player_slot(arc_net_turn_owner()) : 0;   /* typed by an agent: the current player */
    if (!strcmp(name, "move") && argc >= 3) { if (!cmd_move(side, atoi(argv[0]), atoi(argv[1]), atoi(argv[2])) && !arc_net_catching_up()) arc_log("move refused by the rules"); return true; }
    if (!strcmp(name, "attack") && argc >= 2) { if (!cmd_attack(side, atoi(argv[0]), atoi(argv[1])) && !arc_net_catching_up()) arc_log("attack refused by the rules"); return true; }
    if (!strcmp(name, "solo")) { setup_match(arc_seed(), 2); return true; }            /* offline: hot-seat test match */
    return false;
}
static size_t save_state(void *buf, size_t cap) { arc_writer w = { (uint8_t *)buf, cap, 0 }; arc_write(&w, &S, sizeof S); return w.len; }
static bool load_state(const void *buf, size_t n) { arc_reader r = { (const uint8_t *)buf, n, 0, false }; rules t; arc_read(&r, &t, sizeof t); if (r.fail) return false; S = t; return true; }
static void dump(arc_json *j) {
    arc_json_int_k(j, "started", S.started); arc_json_int_k(j, "turn", S.turn); arc_json_int_k(j, "owner", S.owner);
    arc_json_int_k(j, "winner", S.winner); arc_json_int_k(j, "moves", S.moves); arc_json_int_k(j, "attacks", S.attacks); arc_json_int_k(j, "hits", S.hits);
    arc_json_num_k(j, "total_damage", S.total_damage); arc_json_str_k(j, "chat", S.chat);
    if (S.orders) { arc_json_int_k(j, "orders", S.orders); arc_json_str_k(j, "order_log", S.order_log); arc_json_str_k(j, "order_hash", arc_fmt("%08x", S.order_hash)); }
    arc_json_arr_k(j, "units");
    for (int i = 0; i < S.nunits; i++) {
        arc_json_obj(j); arc_json_int_k(j, "owner", S.u[i].owner); arc_json_int_k(j, "x", S.u[i].x); arc_json_int_k(j, "y", S.u[i].y);
        arc_json_int_k(j, "hp", S.u[i].hp); arc_json_num_k(j, "facing", S.u[i].facing); arc_json_end(j);
    }
    arc_json_end(j);
    uint32_t h = 2166136261u; const uint8_t *b = (const uint8_t *)&S; for (size_t i = 0; i < sizeof S; i++) { h ^= b[i]; h *= 16777619u; }
    arc_json_str_k(j, "rules_hash", arc_fmt("%08x", h));
}
static arc_v2 cell_px(float x, float y) {
    float cs = arc_minf((float)arc_width(), (float)arc_height()) / (GW + 2);
    return arc_v2_make(((float)arc_width() - cs * GW) * 0.5f + x * cs, cs + y * cs);
}
static void tick(void) {
    arc_net_ev ev; while (arc_net_event(&ev)) {}                                     /* (handled in net_event) */
    for (int i = 0; i < S.nunits; i++) { P.x[i] += (S.u[i].x - P.x[i]) * 0.25f; P.y[i] += (S.u[i].y - P.y[i]) * 0.25f; }   /* animation only */
    if (!S.started) { arc_net_lobby(NULL); return; }
    if (P.autoplay && arc_net_my_turn() && arc_net_turn() < P.autoplay && arc_net_turn() != P.played) {   /* input, like a player's clicks */
        ai_turn();
        if (arc_net_turn_end()) P.played = arc_net_turn();
    }
    if (arc_headless()) return;
    /* mouse play: click your unit, then a cell (move) or an enemy (attack); SPACE ends the turn */
    if (arc_net_my_turn() && S.winner < 0) {
        float cs = arc_minf((float)arc_width(), (float)arc_height()) / (GW + 2); arc_v2 o = cell_px(0, 0), m = arc_mouse();
        int cx = (int)((m.x - o.x) / cs), cy = (int)((m.y - o.y) / cs);
        if (arc_key_pressed(ARC_MOUSE_LEFT) && on_grid(cx, cy)) {
            int at = unit_at(cx, cy), me = player_slot(arc_net_local_player());
            if (at >= 0 && S.u[at].owner == me) P.sel = at;
            else if (P.sel >= 0 && at >= 0) arc_net_command(arc_fmt("attack %d %d", P.sel, at));
            else if (P.sel >= 0) arc_net_command(arc_fmt("move %d %d %d", P.sel, cx, cy));
        }
        if (arc_key_pressed(ARC_KEY_SPACE)) arc_net_turn_end();
    }
}
static void draw(float alpha) {
    (void)alpha;
    if (!S.started) return;                        /* the lobby (UI kit) draws itself */
    float cs = arc_minf((float)arc_width(), (float)arc_height()) / (GW + 2);
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) { arc_v2 p = cell_px((float)x, (float)y); arc_draw_rect(p.x + 1, p.y + 1, cs - 2, cs - 2, S.wall[y][x] ? 0x505868 : ((x + y) & 1) ? 0x202630 : 0x262C38); }
    for (int i = 0; i < S.nunits; i++) {
        if (!S.u[i].alive) continue;
        arc_v2 p = cell_px(P.x[i], P.y[i]); uint32_t col = arc_palette_cb(S.u[i].owner);
        arc_draw_rect(p.x + cs * 0.2f, p.y + cs * 0.2f, cs * 0.6f, cs * 0.6f, i == P.sel ? 0xFFFFFF : col);
        arc_draw_line2d(p.x + cs * 0.5f, p.y + cs * 0.5f, p.x + cs * (0.5f + 0.4f * cosf(S.u[i].facing)), p.y + cs * (0.5f + 0.4f * sinf(S.u[i].facing)), 2, 0x000000);
        arc_draw_textf(p.x + cs * 0.25f, p.y + cs * 0.3f, 1.5f, 0x000000, "%d", S.u[i].hp);
    }
    arc_draw_textf(10, 10, 2, 0xE0E0E0, S.winner >= 0 ? "Player %d wins" : "Turn %d: player %d%s", S.winner >= 0 ? S.winner : S.turn, S.owner, arc_net_my_turn() ? " (you: click a unit, SPACE ends)" : "");
    if (S.chat[0]) arc_draw_textf(10, 30, 1.5f, 0xA0C0FF, "%s", S.chat);
}
static void init(void) {
    arc_net_desc d; memset(&d, 0, sizeof d);
    d.game = "netturns"; d.version = "1.0"; d.max_players = 4;
    if (arc_arg_flag("simultaneous")) d.turns = ARC_NET_TURNS_SIMULTANEOUS;   /* 0.8.5 */
    arc_net_setup(&d);
    arc_var("netturns.hp", &unit_hp, 10.0f);
    arc_ui_set_nav(true);
    S.winner = -1; P.sel = -1;
}
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "netturns", .width = 960, .height = 600, .init = init, .tick = tick, .draw = draw, .dump = dump, .command = command,
                      .save_state = save_state, .load_state = load_state, .net_event = net_event };
}
