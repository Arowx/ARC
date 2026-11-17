/* arc_relay: the internet host for ARC's turn-based sessions (ARC 0.8.5). No game code and no rules: it does what a
   game's LAN host does (src/arc_net.c), with the same shared core (src/arc_net_core.h), for many rooms at once:
     - numbers every command and sends it to everyone in the room (the sender too), refuses commands out of turn,
       sequences joins, leaves, rejoins, ready flags, names, the start and every end of turn (alternating or
       simultaneous turns), compares the players' state hashes (a mismatch: DESYNC to everyone), keeps each room's log so
       a player who drops (or restarts the game) rejoins and catches up;
     - rooms are named by a short code (4-6 letters and digits that can't be misread); the player who creates one is its
       player 0, the room's host (it starts the match);
     - speaks WebSocket (RFC 6455, for browsers and desktop builds) and ARC's raw TCP protocol on ONE port (told apart by
       the first bytes); a plain HTTP GET (no upgrade) answers a one-line status (health checks);
     - limits: rooms, connections (in all and per address), room creations and wrong codes per address per minute,
       messages and bytes per second per connection, a room's log size; empty rooms expire, quiet rooms close;
     - if it restarts, players reconnect by themselves: the first ones back give it the room's log (the one with the most
       commands is asked) and the match carries on;
     - SIGINT / SIGTERM (Ctrl+C, Ctrl+Break, systemd stop): tells every player it is going down (they reconnect) and exits.
   Logs to stdout. Build: ./build.sh relay (Linux, and Windows with MinGW) or build.bat relay (MSVC). See docs/RELAY.md.

   usage: arc_relay [--port 47880] [--bind 0.0.0.0] [--max-rooms 1000] [--max-conns 1000] [--max-conns-per-ip 32]
                    [--room-idle 900] [--room-quiet 21600] [--creates-per-min 20] [--bad-codes-per-min 30]
                    [--rate-msgs 200] [--rate-bytes 524288] [--max-log 1000000] [--max-log-mb 16] [--restore-wait 2] [--code-len 4]
                    [--no-raw] [--verbose] [--seed N] */
#if defined(_WIN32)
#define FD_SETSIZE 1024
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600                             /* inet_pton / inet_ntop */
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET rsock;
#define RBAD INVALID_SOCKET
#define rclose closesocket
#define RERR() WSAGetLastError()
#define R_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK)
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
typedef int rsock;
#define RBAD (-1)
#define rclose close
#define RERR() errno
#define R_WOULDBLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK || (e) == EINTR)
#endif
#include "../../src/arc_net_core.h"
#include <stdarg.h>
#include <time.h>

/* ======================================================================== */
/* Settings, clock, log, random                                             */
/* ======================================================================== */
static struct {
    int port; char bind[64]; int max_rooms, max_conns, max_conns_ip, creates_min, bad_min, rate_msgs, rate_bytes, max_log, code_len, max_log_mb;
    double room_idle, room_quiet, restore_wait; bool raw, verbose; uint64_t seed;
} CFG = { NCORE_RELAY_PORT, "0.0.0.0", 1000, 1000, 32, 20, 30, 200, 512 * 1024, 1000000, 4, 16, 900, 21600, 2.0, true, false, 0 };
#define RELAY_HASH_RING 256                             /* hashes come back right after each command: a short ring per room */
static volatile int g_stop;
static double now_s(void) {
#if defined(_WIN32)
    static LARGE_INTEGER f; LARGE_INTEGER c; if (!f.QuadPart) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
#endif
}
static double g_t0;
static void rlog(const char *fmt, ...) {
    char b[1200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    printf("relay %9.3f %s\n", now_s() - g_t0, b); fflush(stdout);
}
static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t rnd64(void) { g_rng ^= g_rng >> 12; g_rng ^= g_rng << 25; g_rng ^= g_rng >> 27; return g_rng * 0x2545F4914F6CDD1Dull; }
static void rnd_seed(void) {
    uint64_t s = (uint64_t)time(NULL) * 0x9E3779B97F4A7C15ull ^ (uint64_t)(now_s() * 1e9);
#if defined(_WIN32)
    s ^= (uint64_t)GetCurrentProcessId() << 32 ^ (uint64_t)GetTickCount();
#else
    s ^= (uint64_t)getpid() << 32;
    FILE *f = fopen("/dev/urandom", "rb"); if (f) { uint64_t u = 0; if (fread(&u, 8, 1, f) == 1) s ^= u; fclose(f); }
#endif
    if (CFG.seed) s = CFG.seed;
    g_rng = s ? s : 1; for (int i = 0; i < 8; i++) (void)rnd64();
}

/* ======================================================================== */
/* Connections and rooms                                                    */
/* ======================================================================== */
enum { P_UNKNOWN, P_RAW, P_WS, P_HTTP };
enum { CS_FREE, CS_START, CS_HELLO, CS_HOLD, CS_RESTORING, CS_LIVE, CS_CLOSING };
typedef struct {
    int state, proto; rsock fd; char ip[48];
    net_buf in, msg, out;                              /* socket bytes in; the ARC byte stream (WebSocket payloads); bytes out */
    double opened, last_recv, next_ping, close_at; int ping_ms;
    int room, slot;                                    /* -1 when not in a room */
    ncore_hello hello;                                 /* kept while held for a restore */
    double tok_msgs, tok_bytes, tok_at;                /* rate limits (token buckets) */
} rconn;
typedef struct { uint32_t off, len; int8_t from; uint8_t flags, kind; } rentry;
typedef struct {
    bool used; char code[8]; char game[48], ver[32], arcv[32], name[48]; int maxp; uint64_t session;
    net_view AV; uint64_t tokens[NCORE_MAXP]; int conn[NCORE_MAXP];
    rentry *log; int log_n, log_cap; net_buf text;
    net_hrow *hring; uint32_t desync_seq;
    double created, empty_since, last_seq, next_pings;
    int restoring; double restore_until; int restore_conn;   /* 1 = collecting returning players, 2 = one of them sends the log */
    struct { bool known; uint32_t ip; uint16_t port; double last; } ua[NCORE_MAXP];   /* 0.10.4 each slot's UDP address (network order) */
    double udp_tok, udp_at;                                  /* forwarded datagrams per second (a token bucket) */
} rroom;
static rconn *C; static int NC;
static rroom *RM; static int NR;
static rsock g_listen = RBAD, g_udp = RBAD;
static struct { int conns_now; long long accepted, refused, commands, udp_fwd, udp_bad; } STAT;

/* per address: connections now, room creations and wrong codes this minute */
#define IPT 2048
static struct { char ip[48]; int conns, creates, bad; double window; } IPS[IPT];
static int ip_slot(const char *ip) {
    uint32_t h = 2166136261u; for (const char *p = ip; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
    for (int k = 0; k < IPT; k++) {
        int i = (int)((h + (uint32_t)k) % IPT);
        if (!IPS[i].ip[0] || !strcmp(IPS[i].ip, ip)) { if (!IPS[i].ip[0]) snprintf(IPS[i].ip, sizeof IPS[i].ip, "%s", ip); return i; }
    }
    return (int)(h % IPT);                              /* (a full table shares slots: limits get stricter, never looser) */
}
static void ip_window(int i, double now) { if (now - IPS[i].window > 60.0) { IPS[i].window = now; IPS[i].creates = 0; IPS[i].bad = 0; } }

static void c_send_bytes(rconn *c, const void *p, size_t n) { nb_put(&c->out, p, n); }
/* the message in g_msg to one connection (a binary WebSocket frame, or as is on raw TCP) */
static void c_send(rconn *c) {
    m_finish();
    if (c->proto == P_WS) { uint8_t h[14]; size_t k = ncore_ws_header(h, WS_BINARY, g_msg.n, NULL); c_send_bytes(c, h, k); }
    c_send_bytes(c, g_msg.b, g_msg.n);
}
static void c_close_after(rconn *c, double now) {   /* send what is queued, then close */
    if (c->state == CS_FREE || c->state == CS_CLOSING) return;
    if (c->proto == P_WS) { uint8_t f[4] = { 0x88, 2, 0x03, 0xE8 }; c_send_bytes(c, f, 4); }   /* close 1000 */
    c->state = CS_CLOSING; c->close_at = now + 2.0;
}
static void c_refuse(rconn *c, int code, const char *text, double now) {
    m_begin(M_REFUSE); m_u8((unsigned)code); m_str(text); c_send(c);
    STAT.refused++;
    c_close_after(c, now);
}
static void c_bye(rconn *c, const char *why, bool retry, double now) {
    m_begin(M_BYE); m_str(why); m_u8(retry ? 1 : 0); c_send(c);
    c_close_after(c, now);
}

static const char *entry_text_r(const rroom *r, const rentry *e) { return (const char *)r->text.b + e->off; }
static bool room_append(rroom *r, int kind, int from, int flags, const char *text) {
    if (r->log_n == r->log_cap) {
        int nc = r->log_cap ? r->log_cap * 2 : 256;
        rentry *nl = (rentry *)realloc(r->log, sizeof(rentry) * (size_t)nc); if (!nl) return false;
        r->log = nl; r->log_cap = nc;
    }
    size_t len = strlen(text); if (len > NET_TEXT_MAX) len = NET_TEXT_MAX;
    rentry *e = &r->log[r->log_n++];
    e->off = (uint32_t)r->text.n; e->len = (uint32_t)len; e->from = (int8_t)from; e->flags = (uint8_t)flags; e->kind = (uint8_t)kind;
    nb_put(&r->text, text, len); uint8_t z = 0; nb_put(&r->text, &z, 1);
    return true;
}
static void send_seq_r(rconn *c, const rroom *r, int i) {
    const rentry *e = &r->log[i];
    m_begin(M_SEQ); m_u32((uint32_t)i + 1); m_u8((unsigned)(uint8_t)e->from); m_u8(e->flags); m_u8(e->kind); m_str(entry_text_r(r, e)); c_send(c);
}
/* number an entry, apply it to the room's view and send it to everyone in the room */
static void room_assign(rroom *r, int kind, int from, int flags, const char *text, double now) {
    if (!room_append(r, kind, from, flags, text)) return;
    view_apply(&r->AV, kind, from, text);
    for (int p = 0; p < NCORE_MAXP; p++) if (r->conn[p] >= 0 && C[r->conn[p]].state == CS_LIVE) send_seq_r(&C[r->conn[p]], r, r->log_n - 1);
    r->last_seq = now;
    if (kind == NK_CMD) STAT.commands++;
    if (CFG.verbose) rlog("room %s seq %d from %d %s %s", r->code, r->log_n, from, nk_names[kind], text);
}
static void room_pings(rroom *r) {
    m_begin(M_PINGS); m_u8(NCORE_MAXP);
    for (int p = 0; p < NCORE_MAXP; p++) { int ci = r->conn[p]; m_u16((unsigned)(ci >= 0 && C[ci].state == CS_LIVE && C[ci].ping_ms >= 0 ? C[ci].ping_ms : 0xFFFF)); }
    for (int p = 0; p < NCORE_MAXP; p++) { int ci = r->conn[p]; if (ci >= 0 && C[ci].state == CS_LIVE) { size_t keep = g_msg.n; c_send(&C[ci]); g_msg.n = keep; } }
}
static void room_free(rroom *r, const char *why) {
    rlog("room %s closed (%s): %d commands", r->code, why, r->log_n);
    free(r->log); nb_free(&r->text); free(r->hring);
    memset(r, 0, sizeof *r);
}
static int room_find(const char *code) { for (int i = 0; i < NR; i++) if (RM[i].used && !strcmp(RM[i].code, code)) return i; return -1; }
static int room_count(void) { int n = 0; for (int i = 0; i < NR; i++) n += RM[i].used; return n; }
static int room_new(const char *code, const ncore_hello *h, double now) {
    int ri = -1; for (int i = 0; i < NR; i++) if (!RM[i].used) { ri = i; break; }
    if (ri < 0) return -1;
    rroom *r = &RM[ri]; memset(r, 0, sizeof *r);
    r->used = true;
    if (code) snprintf(r->code, sizeof r->code, "%s", code);
    else {
        int len = CFG.code_len; if (room_count() > 20000 && len < 5) len = 5;
        for (int tries = 0; ; tries++) {
            char c[8]; ncore_room_code(rnd64(), len + tries / 50, c);
            if (room_find(c) < 0) { memcpy(r->code, c, sizeof c); break; }
        }
    }
    snprintf(r->game, sizeof r->game, "%s", h->game); snprintf(r->ver, sizeof r->ver, "%s", h->ver); snprintf(r->arcv, sizeof r->arcv, "%s", h->arcv);
    snprintf(r->name, sizeof r->name, "%s", h->room_name[0] ? h->room_name : "ARC game");
    r->maxp = h->max_players < 2 ? 2 : h->max_players > NCORE_MAXP ? NCORE_MAXP : h->max_players;
    r->session = h->session ? h->session : (rnd64() | 1);
    memset(&r->AV, 0, sizeof r->AV); r->AV.owner = -1;
    for (int p = 0; p < NCORE_MAXP; p++) r->conn[p] = -1;
    r->hring = NULL;                                   /* (made on the first hash: a lobby that never plays costs nothing) */
    r->created = r->last_seq = r->next_pings = now; r->empty_since = now; r->restore_conn = -1;
    return ri;
}
static bool room_empty(const rroom *r) {
    for (int p = 0; p < NCORE_MAXP; p++) if (r->conn[p] >= 0) return false;
    for (int i = 0; i < NC; i++) if (C[i].state != CS_FREE && C[i].room == (int)(r - RM) && (C[i].state == CS_HOLD || C[i].state == CS_RESTORING)) return false;
    return true;
}
/* a player's link is gone (closed, timed out, left, replaced): LEAVE for everyone */
static void room_drop(int ci, const char *why, double now) {
    rconn *c = &C[ci];
    if (c->room < 0) return;
    rroom *r = &RM[c->room];
    if (c->slot >= 0 && r->conn[c->slot] == ci) {
        r->conn[c->slot] = -1; r->ua[c->slot].known = false;   /* (0.10.4: its UDP address too) */
        rlog("room %s player %d (%s) dropped: %s", r->code, c->slot, r->AV.p[c->slot].name, why);
        if (r->AV.p[c->slot].used && r->AV.p[c->slot].connected) room_assign(r, NK_LEAVE, c->slot, 0, "", now);
    }
    if (r->restoring == 2 && r->restore_conn == ci) { r->restoring = 1; r->restore_until = now; r->restore_conn = -1; }   /* ask another one */
    c->room = -1; c->slot = -1;
    if (room_empty(r)) r->empty_since = now;
}
static void c_free(int ci, const char *why, double now) {
    rconn *c = &C[ci];
    if (c->state == CS_FREE) return;
    room_drop(ci, why, now);
    if (c->fd != RBAD) rclose(c->fd);
    int s = ip_slot(c->ip); if (IPS[s].conns > 0) IPS[s].conns--;
    nb_free(&c->in); nb_free(&c->msg); nb_free(&c->out);
    memset(c, 0, sizeof *c); c->fd = RBAD; c->room = c->slot = -1;
    STAT.conns_now--;
}

/* a player into a room's slot (a new join or a rejoin): the session's HELLO rules of a LAN host, shared core */
static void room_admit(int ci, int ri, double now) {
    rconn *c = &C[ci]; rroom *r = &RM[ri]; const ncore_hello *h = &c->hello;
    char msg[300];
    if (ncore_version_check(r->game, r->ver, r->arcv, h, msg, sizeof msg)) { rlog("room %s refused %s: %s", r->code, h->name, msg); c_refuse(c, R_VERSION, msg, now); return; }
    bool live[NCORE_MAXP]; for (int p = 0; p < NCORE_MAXP; p++) live[p] = r->conn[p] >= 0;
    unsigned stale = ncore_hello_stale(&r->AV, r->tokens, live, 0, h);
    /* on the internet a name is not proof: a live player is replaced by name (a restarted game) only once its link has
       gone quiet (a crashed game sends nothing; the same running game, by its token, at once) */
    for (int p = 0; p < NCORE_MAXP; p++) if ((stale & (1u << p)) && h->token != r->tokens[p] && now - C[r->conn[p]].last_recv < 5.0) {
        char m[120]; snprintf(m, sizeof m, "a player named %s is already playing in this room", r->AV.p[p].name);
        rlog("room %s refused %s from %s: %s", r->code, h->name, c->ip, m); c_refuse(c, R_STARTED, m, now); return;
    }
    for (int p = 0; p < NCORE_MAXP; p++) if (stale & (1u << p)) {
        int old = r->conn[p]; room_drop(old, "replaced by a new connection from the same player", now); c_close_after(&C[old], now); live[p] = false;
    }
    bool rejoin; uint32_t from; int code = 0; const char *why = NULL;
    int slot = ncore_hello_slot(&r->AV, r->tokens, live, 0, r->maxp, h, r->session, (uint32_t)r->log_n, &rejoin, &from, &code, &why);
    if (slot < 0) { rlog("room %s refused %s: %s", r->code, h->name, why); c_refuse(c, code, why, now); return; }
    r->tokens[slot] = h->token;
    char nm[32];
    if (!rejoin) ncore_unique_name(&r->AV, h->name, nm, sizeof nm); else snprintf(nm, sizeof nm, "%s", r->AV.p[slot].name);
    r->conn[slot] = ci; c->room = ri; c->slot = slot; c->state = CS_LIVE; c->next_ping = now;
    m_begin(M_WELCOME); m_u16(NET_PROTO); m_u8((unsigned)slot); m_u8((unsigned)r->maxp); m_u64(r->session); m_str(r->name); m_str(r->code); c_send(c);
    for (uint32_t i = from; i < (uint32_t)r->log_n; i++) send_seq_r(c, r, (int)i);
    m_begin(M_CAUGHT_UP); m_u32((uint32_t)r->log_n); c_send(c);
    rlog("room %s player %d %s %s from %s (sent %u commands)", r->code, slot, rejoin ? "rejoined:" : "joined:", nm, c->ip, (unsigned)r->log_n - from);
    if (rejoin) room_assign(r, NK_REJOIN, slot, 0, "", now); else room_assign(r, NK_JOIN, slot, 0, nm, now);
    room_pings(r);
}
/* the log has come back from a returning player: nobody is in the room yet (LEAVE for everyone the log has connected),
   then the players who were waiting go in, the one who sent it first */
static void room_restored(rroom *r, double now) {
    int ri = (int)(r - RM);
    memset(&r->AV, 0, sizeof r->AV); r->AV.owner = -1;
    for (int i = 0; i < r->log_n; i++) view_apply(&r->AV, r->log[i].kind, r->log[i].from, entry_text_r(r, &r->log[i]));
    for (int p = 0; p < NCORE_MAXP; p++) if (r->AV.p[p].used && r->AV.p[p].connected) room_assign(r, NK_LEAVE, p, 0, "", now);
    rlog("room %s restored from a player's log (%d commands)", r->code, r->log_n);
    int first = r->restore_conn; r->restoring = 0; r->restore_conn = -1;
    if (first >= 0 && C[first].state == CS_RESTORING) room_admit(first, ri, now);
    for (int i = 0; i < NC; i++) if (C[i].state == CS_HOLD && C[i].room == ri) room_admit(i, ri, now);
}

/* ======================================================================== */
/* Messages                                                                 */
/* ======================================================================== */
static void on_hello(int ci, net_rd *rd, double now) {
    rconn *c = &C[ci]; ncore_hello *h = &c->hello;
    if (!ncore_hello_read(rd, h)) { c_refuse(c, R_PROTOCOL, "this game speaks another network protocol: update ARC (the relay runs protocol 1)", now); return; }
    if (h->op == NCORE_OP_LAN) { c_refuse(c, R_RELAY, "this is an ARC relay: host or join a room by code (arc_net_host_online / arc_net_join_online)", now); return; }
    int s = ip_slot(c->ip); ip_window(s, now);
    if (h->op == NCORE_OP_CREATE) {
        if (++IPS[s].creates > CFG.creates_min) { c_refuse(c, R_RELAY, "too many rooms made from your address: wait a minute", now); return; }
        if (room_count() >= CFG.max_rooms) { c_refuse(c, R_RELAY, "the relay is full (too many rooms): try later", now); return; }
        h->session = 0; h->last = 0;
        int ri = room_new(NULL, h, now);
        if (ri < 0) { c_refuse(c, R_RELAY, "the relay is full: try later", now); return; }
        rlog("room %s created by %s from %s: %s %s (ARC %s), up to %d players", RM[ri].code, h->name, c->ip, h->game, h->ver, h->arcv, RM[ri].maxp);
        room_admit(ci, ri, now);
        return;
    }
    if (IPS[s].bad > CFG.bad_min) { c_refuse(c, R_RELAY, "too many wrong room codes from your address: wait a minute", now); return; }
    char code[8];
    if (!ncore_code_normalize(h->room, code, sizeof code)) { IPS[s].bad++; c_refuse(c, R_ROOM, "that is not a room code", now); return; }
    int ri = room_find(code);
    if (ri < 0 && (h->flags & NCORE_HELLO_RESTORE) && h->session && h->last > 0 && room_count() < CFG.max_rooms) {
        ri = room_new(code, h, now);                   /* a player of a room this relay has lost (it restarted): hold on */
        if (ri >= 0) { RM[ri].restoring = 1; RM[ri].restore_until = now + CFG.restore_wait; rlog("room %s: %s is back with %u commands; waiting %.1f s for the others", code, h->name, h->last, CFG.restore_wait); }
    }
    if (ri < 0) {
        IPS[s].bad++;
        char m[160]; snprintf(m, sizeof m, "no room %s on the relay (a wrong code, or the room has closed)", code);
        c_refuse(c, R_ROOM, m, now); return;
    }
    if (RM[ri].restoring) { c->state = CS_HOLD; c->room = ri; c->slot = -1; c->next_ping = now; if (RM[ri].restoring == 1) rlog("room %s: %s waits for the restore (%u commands)", code, h->name, h->last); return; }
    room_admit(ci, ri, now);
}
static void on_live(int ci, int type, net_rd *rd, double now) {
    rconn *c = &C[ci]; rroom *r = &RM[c->room]; int p = c->slot;
    switch (type) {
    case M_CMD: {
        unsigned flags = r_u8(rd), kind = r_u8(rd); char text[NET_TEXT_MAX + 1]; r_str(rd, text, sizeof text);
        if (rd->bad) return;
        const char *why = ncore_cmd_check(&r->AV, p, kind, &flags, p == 0);
        if (!why && (r->log_n >= CFG.max_log || r->text.n + strlen(text) > (size_t)CFG.max_log_mb << 20)) why = "the room's log is full";
        if (why) {
            if (kind == NK_TURN && !text[0]) snprintf(text, sizeof text, "end_turn");
            m_begin(M_REFUSED); m_str(why); m_str(text); c_send(c);
            if (CFG.verbose) rlog("room %s refused player %d (%s): %s", r->code, p, why, text);
            return;
        }
        room_assign(r, (int)kind, p, (int)flags, text, now);
        if (kind == NK_START) rlog("room %s: the match starts (%s)", r->code, text);
        break; }
    case M_HASH: {
        uint32_t s = r_u32(rd); uint64_t h = r_u64(rd);
        if (rd->bad || !s || s > (uint32_t)r->log_n || r->desync_seq) return;
        if (!r->hring && !(r->hring = (net_hrow *)calloc(RELAY_HASH_RING, sizeof(net_hrow)))) return;
        if (ncore_hash_put(r->hring, RELAY_HASH_RING, s, p, h, -1)) {   /* (no rules here: any two players that differ) */
            r->desync_seq = s;
            rlog("room %s DESYNC at seq %u (players' hashes differ)", r->code, s);
            ncore_desync_msg(&r->hring[s % RELAY_HASH_RING], s, (uint32_t)r->log_n);
            for (int q = 0; q < NCORE_MAXP; q++) { int qi = r->conn[q]; if (qi >= 0 && C[qi].state == CS_LIVE) { size_t keep = g_msg.n; c_send(&C[qi]); g_msg.n = keep; } }
        }
        break; }
    case M_PING: { double t = r_f64(rd); m_begin(M_PONG); m_f64(t); c_send(c); break; }
    case M_PONG: { double t = r_f64(rd); if (!rd->bad) c->ping_ms = (int)((now - t) * 1000.0 + 0.5); break; }
    case M_SYNC: { uint32_t id = r_u32(rd); m_begin(M_SYNCED); m_u32(id); m_u32((uint32_t)r->log_n); c_send(c); break; }
    case M_BYE: room_drop(ci, "left", now); c_close_after(c, now); break;
    default: break;
    }
}
static void on_restoring(int ci, int type, net_rd *rd, double now) {
    rconn *c = &C[ci]; rroom *r = &RM[c->room];
    if (type == M_SEQ) {
        uint32_t seq = r_u32(rd); int from = (int)(int8_t)r_u8(rd); unsigned flags = r_u8(rd), kind = r_u8(rd); char text[NET_TEXT_MAX + 1]; r_str(rd, text, sizeof text);
        if (rd->bad || kind >= NK_COUNT || seq != (uint32_t)r->log_n + 1 || r->log_n >= CFG.max_log || r->text.n > (size_t)CFG.max_log_mb << 20) { c_refuse(c, R_RELAY, "garbled room log", now); room_drop(ci, "garbled log", now); return; }
        room_append(r, (int)kind, from, (int)flags, text);
    } else if (type == M_RESTORED) {
        uint32_t n = r_u32(rd);
        if (rd->bad || n != (uint32_t)r->log_n) { c_refuse(c, R_RELAY, "incomplete room log", now); return; }
        room_restored(r, now);
    } else if (type == M_PING) { double t = r_f64(rd); m_begin(M_PONG); m_f64(t); c_send(c); }
}
static void on_message(int ci, int type, net_rd *rd, double now) {
    rconn *c = &C[ci];
    switch (c->state) {
    case CS_HELLO: if (type != M_HELLO) { c_close_after(c, now); return; } on_hello(ci, rd, now); break;
    case CS_LIVE: on_live(ci, type, rd, now); break;
    case CS_RESTORING: on_restoring(ci, type, rd, now); break;
    case CS_HOLD: if (type == M_PING) { double t = r_f64(rd); m_begin(M_PONG); m_f64(t); c_send(c); } break;
    default: break;
    }
}

/* ======================================================================== */
/* Bytes: the first ones (WebSocket upgrade, HTTP status, raw ARC), frames  */
/* ======================================================================== */
static void http_answer(rconn *c, const char *status, const char *body, double now) {
    char b[600]; int n = snprintf(b, sizeof b, "HTTP/1.1 %s\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n%s", status, (int)strlen(body), body);
    c_send_bytes(c, b, (size_t)n); c->proto = P_HTTP; c->state = CS_CLOSING; c->close_at = now + 2.0;
}
static void on_start(int ci, double now) {
    rconn *c = &C[ci];
    if (c->in.n < 4) return;
    if (memcmp(c->in.b, "GET ", 4)) {                   /* ARC's raw protocol: a HELLO (its length is never "GET ") */
        if (!CFG.raw) { c_free(ci, "raw TCP is off", now); return; }
        c->proto = P_RAW; c->state = CS_HELLO; return;
    }
    uint8_t z = 0; nb_put(&c->in, &z, 1); c->in.n--;
    const char *head = (const char *)c->in.b, *end = strstr(head, "\r\n\r\n");
    if (!end) { if (c->in.n > 8192) c_free(ci, "request too long", now); return; }
    size_t hl = (size_t)(end - head) + 4;
    char up[64] = "", conn[128] = "", key[64] = "", ver[16] = "";
    ncore_http_header(head, "Upgrade", up, sizeof up); ncore_http_header(head, "Connection", conn, sizeof conn);
    ncore_http_header(head, "Sec-WebSocket-Key", key, sizeof key); ncore_http_header(head, "Sec-WebSocket-Version", ver, sizeof ver);
    if (!ncore_http_token(up, "websocket")) {          /* a plain GET: status, for health checks */
        char body[200]; snprintf(body, sizeof body, "ARC relay (protocol %d): %d rooms, %d connections\n", NET_PROTO, room_count(), STAT.conns_now);
        http_answer(c, "200 OK", body, now); nb_drop_front(&c->in, hl); return;
    }
    if (!ncore_http_token(conn, "upgrade") || !key[0] || strcmp(ver, "13")) { http_answer(c, "400 Bad Request", "bad WebSocket request\n", now); return; }
    char acc[32]; ncore_ws_accept(key, acc);
    char b[300]; int n = snprintf(b, sizeof b, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
    c_send_bytes(c, b, (size_t)n);
    nb_drop_front(&c->in, hl);
    c->proto = P_WS; c->state = CS_HELLO;
}
/* WebSocket frames from a client (masked) -> the ARC byte stream in c->msg; false = close the link */
static bool ws_frames(int ci, double now) {
    rconn *c = &C[ci];
    for (;;) {
        ncore_wsf f; long k = ncore_ws_parse(c->in.b, c->in.n, (uint64_t)1 << 20, &f);
        if (k == 0) return true;
        if (k < 0 || !f.masked) { rlog("conn %s: bad WebSocket frame", c->ip); return false; }
        uint8_t *pl = c->in.b + f.hdr; ncore_ws_mask(pl, (size_t)f.len, f.mask);
        if (f.opcode == WS_BINARY || f.opcode == WS_CONT) nb_put(&c->msg, pl, (size_t)f.len);
        else if (f.opcode == WS_TEXT) { rlog("conn %s: text frames are not ARC messages", c->ip); return false; }
        else if (f.opcode == WS_PING) { uint8_t h[14]; size_t hk = ncore_ws_header(h, WS_PONG, f.len, NULL); c_send_bytes(c, h, hk); c_send_bytes(c, pl, (size_t)f.len); }
        else if (f.opcode == WS_CLOSE) { nb_drop_front(&c->in, (size_t)k); room_drop(ci, "closed", now); c_close_after(c, now); return true; }
        nb_drop_front(&c->in, (size_t)k);
    }
}
static void on_bytes(int ci, double now) {
    rconn *c = &C[ci];
    if (c->state == CS_START) { on_start(ci, now); if (c->state == CS_START || c->state == CS_FREE) return; }
    if (c->state == CS_CLOSING || c->state == CS_FREE) return;
    if (c->proto == P_WS) { if (!ws_frames(ci, now)) { c_free(ci, "bad WebSocket data", now); return; } }
    net_buf *s = c->proto == P_WS ? &c->msg : &c->in;
    for (;;) {
        if (c->state == CS_CLOSING || c->state == CS_FREE) return;
        net_rd rd; int type; size_t total;
        int k = ncore_next(s, &rd, &type, &total);
        if (k == 0) break;
        if (k < 0) { c_free(ci, "garbled data", now); return; }
        /* rate limit: messages per second per link (the log a restoring player sends is exempt) */
        if (c->state != CS_RESTORING) {
            c->tok_msgs += (now - c->tok_at) * CFG.rate_msgs; c->tok_bytes += (now - c->tok_at) * CFG.rate_bytes; c->tok_at = now;
            if (c->tok_msgs > CFG.rate_msgs * 10.0) c->tok_msgs = CFG.rate_msgs * 10.0;
            if (c->tok_bytes > CFG.rate_bytes * 8.0) c->tok_bytes = CFG.rate_bytes * 8.0;
            c->tok_msgs -= 1.0; c->tok_bytes -= (double)total;
            if (c->tok_msgs < 0 || c->tok_bytes < 0) { rlog("conn %s: rate limit exceeded", c->ip); room_drop(ci, "rate limit", now); c_bye(c, "rate limit exceeded: too many messages", false, now); return; }
        }
        on_message(ci, type, &rd, now);
        if (c->state == CS_FREE) return;
        nb_drop_front(s, total);
    }
}

/* ======================================================================== */
/* Sockets                                                                  */
/* ======================================================================== */
static bool set_nonblock(rsock s) {
#if defined(_WIN32)
    u_long one = 1; return ioctlsocket(s, FIONBIO, &one) == 0;
#else
    int f = fcntl(s, F_GETFL, 0); return f >= 0 && fcntl(s, F_SETFL, f | O_NONBLOCK) == 0;
#endif
}
static bool listen_on(void) {
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == RBAD) return false;
    int one = 1;
#if !defined(_WIN32)
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, (socklen_t)sizeof one);
#else
    (void)one;
#endif
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)CFG.port);
    if (inet_pton(AF_INET, CFG.bind, &sa.sin_addr) != 1) { rlog("bad --bind address %s", CFG.bind); return false; }
    if (bind(g_listen, (struct sockaddr *)&sa, (socklen_t)sizeof sa) != 0 || listen(g_listen, 64) != 0 || !set_nonblock(g_listen)) return false;
    return true;
}
static void accept_all(double now) {
    for (;;) {
        struct sockaddr_in sa; socklen_t sl = (socklen_t)sizeof sa;
        rsock s = accept(g_listen, (struct sockaddr *)&sa, &sl);
        if (s == RBAD) return;
        char ip[48]; inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof ip);
        int slot = -1; for (int i = 0; i < NC; i++) if (C[i].state == CS_FREE) { slot = i; break; }
        int is = ip_slot(ip);
#if !defined(_WIN32)
        if (s >= FD_SETSIZE) slot = -1;
#endif
        if (slot < 0 || IPS[is].conns >= CFG.max_conns_ip || !set_nonblock(s)) { rclose(s); STAT.refused++; if (CFG.verbose) rlog("conn %s refused (connection limit)", ip); continue; }
        int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, (socklen_t)sizeof one);
        rconn *c = &C[slot]; memset(c, 0, sizeof *c);
        c->fd = s; snprintf(c->ip, sizeof c->ip, "%s", ip); c->state = CS_START; c->opened = c->last_recv = c->tok_at = now; c->room = c->slot = -1; c->ping_ms = -1;
        c->tok_msgs = CFG.rate_msgs * 10.0; c->tok_bytes = CFG.rate_bytes * 8.0;
        IPS[is].conns++; STAT.accepted++; STAT.conns_now++;
        if (CFG.verbose) rlog("conn %s accepted", ip);
    }
}
static void flush_out(rconn *c) {
    while (c->out.n) {
#if defined(_WIN32)
        int k = send(c->fd, (const char *)c->out.b, (int)c->out.n, 0);
#else
        ssize_t k = send(c->fd, c->out.b, c->out.n, MSG_NOSIGNAL);
#endif
        if (k > 0) { nb_drop_front(&c->out, (size_t)k); continue; }
        if (k < 0 && R_WOULDBLOCK(RERR())) break;
        c->out.n = 0; c->close_at = 0; break;            /* the link is gone */
    }
}
static void read_conn(int ci, double now) {
    rconn *c = &C[ci];
    for (int rounds = 0; rounds < 16; rounds++) {
        nb_reserve(&c->in, 16384);
#if defined(_WIN32)
        int k = recv(c->fd, (char *)c->in.b + c->in.n, (int)(c->in.cap - c->in.n), 0);
#else
        ssize_t k = recv(c->fd, c->in.b + c->in.n, c->in.cap - c->in.n, 0);
#endif
        if (k > 0) { c->in.n += (size_t)k; c->last_recv = now; if (c->in.n > (8u << 20)) { c_free(ci, "too much data", now); return; } continue; }
        if (k < 0 && R_WOULDBLOCK(RERR())) break;
        on_bytes(ci, now);                              /* (what came before the close: a BYE) */
        if (c->state != CS_FREE) c_free(ci, "connection closed", now);
        return;
    }
    on_bytes(ci, now);
}

/* ======================================================================== */
/* 0.10.4 UDP: real-time (rollback) datagrams between a room's players       */
/* ======================================================================== */
static bool udp_open(void) {
    g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_udp == RBAD) return false;
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_port = htons((unsigned short)CFG.port);
    if (inet_pton(AF_INET, CFG.bind, &sa.sin_addr) != 1 || bind(g_udp, (struct sockaddr *)&sa, (socklen_t)sizeof sa) != 0 || !set_nonblock(g_udp)) { rclose(g_udp); g_udp = RBAD; return false; }
    int big = 1 << 20; setsockopt(g_udp, SOL_SOCKET, SO_RCVBUF, (const char *)&big, (socklen_t)sizeof big); setsockopt(g_udp, SOL_SOCKET, SO_SNDBUF, (const char *)&big, (socklen_t)sizeof big);
    return true;
}
static void udp_to(uint32_t ip, uint16_t port, const uint8_t *d, size_t n) {
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_addr.s_addr = ip; sa.sin_port = port;
    sendto(g_udp, (const char *)d, (int)n, 0, (struct sockaddr *)&sa, (socklen_t)sizeof sa);
}
static uint64_t le64(const uint8_t *p) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
static void put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void udp_head(uint8_t *b, int type, uint64_t session) { b[0] = 'A'; b[1] = 'R'; b[2] = 'U'; b[3] = NCORE_UDP_VER; b[4] = (uint8_t)type; put64(b + 5, session); }
static int room_by_session(uint64_t s) { for (int i = 0; i < NR; i++) if (RM[i].used && RM[i].session == s) return i; return -1; }
static void udp_datagram(const uint8_t *d, int n, uint32_t ip, uint16_t port, double now) {
    if (n < NCORE_UDP_HDR + 2 || d[0] != 'A' || d[1] != 'R' || d[2] != 'U' || d[3] != NCORE_UDP_VER) { STAT.udp_bad++; return; }
    int type = d[4]; int ri = room_by_session(le64(d + 5));
    if (ri < 0 || RM[ri].restoring) { STAT.udp_bad++; return; }
    rroom *r = &RM[ri]; const uint8_t *q = d + NCORE_UDP_HDR;
    if (type == NCORE_UDP_REG && n >= NCORE_UDP_HDR + 9) {
        int slot = q[0]; uint64_t token = le64(q + 1);
        if (slot >= r->maxp || !token || r->tokens[slot] != token || r->conn[slot] < 0) { STAT.udp_bad++; return; }
        if (!r->ua[slot].known || r->ua[slot].ip != ip || r->ua[slot].port != port) {
            char a[48]; struct in_addr ia; ia.s_addr = ip; inet_ntop(AF_INET, &ia, a, sizeof a);
            rlog("room %s player %d (%s) UDP from %s:%d", r->code, slot, r->AV.p[slot].name, a, (int)ntohs(port));
        }
        r->ua[slot].known = true; r->ua[slot].ip = ip; r->ua[slot].port = port; r->ua[slot].last = now;
        uint8_t b[NCORE_UDP_HDR + 1 + NCORE_MAXP * 7]; udp_head(b, NCORE_UDP_PEERS, r->session); int k = NCORE_UDP_HDR + 1, cnt = 0;
        for (int p = 0; p < NCORE_MAXP; p++) if (p != slot && r->ua[p].known && r->conn[p] >= 0) {
            uint32_t hip = ntohl(r->ua[p].ip); uint16_t hp = ntohs(r->ua[p].port);
            b[k++] = (uint8_t)p; for (int i = 0; i < 4; i++) b[k++] = (uint8_t)(hip >> (8 * i)); b[k++] = (uint8_t)(hp & 255); b[k++] = (uint8_t)(hp >> 8); cnt++;
        }
        b[NCORE_UDP_HDR] = (uint8_t)cnt; udp_to(ip, port, b, (size_t)k);
        return;
    }
    if (type == NCORE_UDP_FWD && n >= NCORE_UDP_HDR + 2) {
        int from = q[0], to = q[1];
        if (from >= NCORE_MAXP || to >= NCORE_MAXP || !r->ua[from].known || r->ua[from].ip != ip || r->ua[from].port != port || !r->ua[to].known || r->conn[to] < 0) { STAT.udp_bad++; return; }
        r->udp_tok = r->udp_tok + (now - r->udp_at) * 600.0 * r->maxp; r->udp_at = now;   /* a few hundred a second per player is plenty */
        if (r->udp_tok > 1200.0 * r->maxp) r->udp_tok = 1200.0 * r->maxp;
        if (r->udp_tok < 1.0) { STAT.udp_bad++; return; }
        r->udp_tok -= 1.0;
        uint8_t b[NCORE_UDP_MAX]; size_t pl = (size_t)n - NCORE_UDP_HDR - 2;
        if (NCORE_UDP_HDR + 1 + pl > sizeof b) return;
        udp_head(b, NCORE_UDP_FROM, r->session); b[NCORE_UDP_HDR] = (uint8_t)from; memcpy(b + NCORE_UDP_HDR + 1, q + 2, pl);
        r->ua[from].last = now;
        udp_to(r->ua[to].ip, r->ua[to].port, b, NCORE_UDP_HDR + 1 + pl); STAT.udp_fwd++;
        return;
    }
    STAT.udp_bad++;
}
static void udp_read(double now) {
    for (int k = 0; k < 1024; k++) {
        uint8_t d[NCORE_UDP_MAX]; struct sockaddr_in sa; socklen_t sl = (socklen_t)sizeof sa;
#if defined(_WIN32)
        int n = recvfrom(g_udp, (char *)d, (int)sizeof d, 0, (struct sockaddr *)&sa, &sl);
        if (n < 0 && RERR() == WSAECONNRESET) continue;   /* an ICMP "port unreachable" from an earlier send */
#else
        ssize_t n = recvfrom(g_udp, d, sizeof d, 0, (struct sockaddr *)&sa, &sl);
        if (n < 0 && (errno == ECONNREFUSED || errno == EINTR)) continue;
#endif
        if (n <= 0) return;
        udp_datagram(d, (int)n, sa.sin_addr.s_addr, sa.sin_port, now);
    }
}

/* ======================================================================== */
/* Housekeeping                                                             */
/* ======================================================================== */
static void housekeeping(double now) {
    for (int i = 0; i < NC; i++) {
        rconn *c = &C[i];
        if (c->state == CS_FREE) continue;
        if (c->state == CS_CLOSING) { if (!c->out.n || now > c->close_at) c_free(i, "closed", now); continue; }
        if ((c->state == CS_START || c->state == CS_HELLO) && now - c->opened > 10.0) { c_free(i, "no hello", now); continue; }
        if ((c->state == CS_LIVE || c->state == CS_HOLD || c->state == CS_RESTORING) && now - c->last_recv > 15.0) { room_drop(i, "timed out", now); c_free(i, "timed out", now); continue; }
        if ((c->state == CS_LIVE || c->state == CS_HOLD) && now >= c->next_ping) { m_begin(M_PING); m_f64(now); c_send(c); c->next_ping = now + 1.0; }
    }
    for (int ri = 0; ri < NR; ri++) {
        rroom *r = &RM[ri]; if (!r->used) continue;
        if (r->restoring == 1 && now >= r->restore_until) {   /* ask the waiting player with the longest log for it */
            int best = -1;
            for (int i = 0; i < NC; i++) if (C[i].state == CS_HOLD && C[i].room == ri && C[i].hello.session == r->session && (best < 0 || C[i].hello.last > C[best].hello.last)) best = i;
            if (best < 0) { room_free(r, "nobody came back to restore it"); for (int i = 0; i < NC; i++) if (C[i].state == CS_HOLD && C[i].room == ri) { C[i].room = -1; c_refuse(&C[i], R_ROOM, "the room could not be restored", now); } continue; }
            r->restoring = 2; r->restore_conn = best; C[best].state = CS_RESTORING; C[best].last_recv = now;
            r->log_n = 0; r->text.n = 0;
            m_begin(M_RESTORE_FROM); m_u32(0); c_send(&C[best]);
            rlog("room %s: asking %s for its log (%u commands)", r->code, C[best].hello.name, C[best].hello.last);
        }
        if (r->restoring) continue;
        if (now >= r->next_pings) { room_pings(r); r->next_pings = now + 2.0; }
        if (room_empty(r) && now - r->empty_since > CFG.room_idle) { room_free(r, "empty"); continue; }
        if (now - r->last_seq > CFG.room_quiet) {
            for (int p = 0; p < NCORE_MAXP; p++) if (r->conn[p] >= 0) { int ci = r->conn[p]; C[ci].room = -1; c_bye(&C[ci], "the room has closed (nothing happened in it for too long)", false, now); }
            room_free(r, "quiet");
        }
    }
}
#if defined(_WIN32)
static BOOL WINAPI on_console(DWORD t) { (void)t; g_stop = 1; return TRUE; }
#else
static void on_signal(int s) { (void)s; g_stop = 1; }
#endif
static void usage(void) {
    printf("arc_relay (ARC 0.8.5): rooms for ARC turn-based sessions over WebSocket and raw TCP on one port\n"
           "  --port N (47880) --bind ADDR (0.0.0.0) --max-rooms N (1000) --max-conns N (1000) --max-conns-per-ip N (32)\n"
           "  --room-idle S (900: empty rooms are kept that long) --room-quiet S (21600: rooms with no command that long close)\n"
           "  --creates-per-min N (20) --bad-codes-per-min N (30) --rate-msgs N (200/s) --rate-bytes N (524288/s)\n"
           "  --max-log N (1000000 entries a room) --max-log-mb N (16 MB of command text a room) --restore-wait S (2)\n"
           "  --code-len N (4..6) --no-raw --verbose --seed N\n");
}
int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
#define OPT(name) (!strcmp(a, name) && v && ++i)
        if (OPT("--port")) CFG.port = atoi(v);
        else if (OPT("--bind")) snprintf(CFG.bind, sizeof CFG.bind, "%s", v);
        else if (OPT("--max-rooms")) CFG.max_rooms = atoi(v);
        else if (OPT("--max-conns")) CFG.max_conns = atoi(v);
        else if (OPT("--max-conns-per-ip")) CFG.max_conns_ip = atoi(v);
        else if (OPT("--room-idle")) CFG.room_idle = atof(v);
        else if (OPT("--room-quiet")) CFG.room_quiet = atof(v);
        else if (OPT("--creates-per-min")) CFG.creates_min = atoi(v);
        else if (OPT("--bad-codes-per-min")) CFG.bad_min = atoi(v);
        else if (OPT("--rate-msgs")) CFG.rate_msgs = atoi(v);
        else if (OPT("--rate-bytes")) CFG.rate_bytes = atoi(v);
        else if (OPT("--max-log")) CFG.max_log = atoi(v);
        else if (OPT("--max-log-mb")) CFG.max_log_mb = atoi(v);
        else if (OPT("--restore-wait")) CFG.restore_wait = atof(v);
        else if (OPT("--code-len")) CFG.code_len = atoi(v);
        else if (OPT("--seed")) CFG.seed = strtoull(v, NULL, 10);
        else if (!strcmp(a, "--no-raw")) CFG.raw = false;
        else if (!strcmp(a, "--verbose")) CFG.verbose = true;
        else { usage(); return !strcmp(a, "--help") || !strcmp(a, "-h") ? 0 : 2; }
#undef OPT
    }
    if (CFG.code_len < NCORE_CODE_MIN) CFG.code_len = NCORE_CODE_MIN;
    if (CFG.code_len > NCORE_CODE_MAX) CFG.code_len = NCORE_CODE_MAX;
    if (CFG.max_conns > FD_SETSIZE - 16) CFG.max_conns = FD_SETSIZE - 16;
    if (CFG.max_conns < 4) CFG.max_conns = 4;
    if (CFG.max_rooms < 1) CFG.max_rooms = 1;
    if (CFG.rate_msgs < 10) CFG.rate_msgs = 10;
    if (CFG.rate_bytes < 16384) CFG.rate_bytes = 16384;
    if (CFG.max_log_mb < 1) CFG.max_log_mb = 1;
#if defined(_WIN32)
    WSADATA wd; if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { printf("relay: no Winsock\n"); return 1; }
    SetConsoleCtrlHandler(on_console, TRUE);
#else
    signal(SIGPIPE, SIG_IGN); signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
#endif
    g_t0 = now_s(); rnd_seed();
    NC = CFG.max_conns; C = (rconn *)calloc((size_t)NC, sizeof *C);
    NR = CFG.max_rooms; RM = (rroom *)calloc((size_t)NR, sizeof *RM);
    if (!C || !RM) { printf("relay: out of memory\n"); return 1; }
    for (int i = 0; i < NC; i++) { C[i].fd = RBAD; C[i].room = C[i].slot = -1; }
    if (!listen_on()) { rlog("cannot listen on %s:%d (in use?)", CFG.bind, CFG.port); return 1; }
    if (!udp_open()) rlog("cannot open UDP port %d: real-time (rollback) matches will not work through this relay", CFG.port);
    rlog("listening on %s:%d (WebSocket%s; UDP %s), protocol %d, up to %d rooms and %d connections", CFG.bind, CFG.port, CFG.raw ? " and raw ARC TCP" : "", g_udp != RBAD ? "for real-time matches" : "off", NET_PROTO, CFG.max_rooms, CFG.max_conns);
    double next_house = 0;
    while (!g_stop) {
        fd_set rs, ws; FD_ZERO(&rs); FD_ZERO(&ws);
        FD_SET(g_listen, &rs);
        rsock maxfd = g_listen;
        if (g_udp != RBAD) { FD_SET(g_udp, &rs); if (g_udp > maxfd) maxfd = g_udp; }
        for (int i = 0; i < NC; i++) {
            if (C[i].state == CS_FREE) continue;
            FD_SET(C[i].fd, &rs); if (C[i].out.n) FD_SET(C[i].fd, &ws);
            if (C[i].fd > maxfd) maxfd = C[i].fd;
        }
        struct timeval tv = { 0, 50000 };
        int k = select((int)maxfd + 1, &rs, &ws, NULL, &tv);
        double now = now_s();
        if (k < 0) {
#if !defined(_WIN32)
            if (errno == EINTR) continue;
#endif
            rlog("select failed (%d)", RERR()); break;
        }
        if (k > 0 && FD_ISSET(g_listen, &rs)) accept_all(now);
        if (k > 0 && g_udp != RBAD && FD_ISSET(g_udp, &rs)) udp_read(now);
        for (int i = 0; i < NC; i++) {
            if (C[i].state == CS_FREE) continue;
            if (k > 0 && FD_ISSET(C[i].fd, &rs)) read_conn(i, now);
        }
        for (int i = 0; i < NC; i++) if (C[i].state != CS_FREE && C[i].out.n) flush_out(&C[i]);
        if (now >= next_house) { housekeeping(now); next_house = now + 0.05; }
    }
    /* a clean shutdown: every player is told (and reconnects by itself when the relay is back) */
    double now = now_s();
    int told = 0;
    for (int i = 0; i < NC; i++) if (C[i].state == CS_LIVE || C[i].state == CS_HOLD || C[i].state == CS_RESTORING) { C[i].room = -1; c_bye(&C[i], "the relay is shutting down (it may be restarting)", true, now); told++; }
    rlog("shutting down: %d rooms, %d players told, %lld commands relayed, %lld datagrams forwarded (%lld refused)", room_count(), told, STAT.commands, STAT.udp_fwd, STAT.udp_bad);
    for (double until = now_s() + 1.0; now_s() < until; ) {
        bool any = false;
        for (int i = 0; i < NC; i++) if (C[i].state != CS_FREE && C[i].out.n) { flush_out(&C[i]); any = any || C[i].out.n; }
        if (!any) break;
#if defined(_WIN32)
        Sleep(10);
#else
        struct timespec ts = { 0, 10000000 }; nanosleep(&ts, NULL);
#endif
    }
    for (int i = 0; i < NC; i++) if (C[i].state != CS_FREE) { if (C[i].fd != RBAD) rclose(C[i].fd); nb_free(&C[i].in); nb_free(&C[i].msg); nb_free(&C[i].out); }
    for (int ri = 0; ri < NR; ri++) if (RM[ri].used) { free(RM[ri].log); nb_free(&RM[ri].text); free(RM[ri].hring); }
    free(C); free(RM);
    if (g_listen != RBAD) rclose(g_listen);
    if (g_udp != RBAD) rclose(g_udp);
#if defined(_WIN32)
    WSACleanup();
#endif
    rlog("stopped");
    return 0;
}
