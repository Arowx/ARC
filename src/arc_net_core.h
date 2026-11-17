/* ARC 0.8.5: the session core shared by a game's LAN host (src/arc_net.c) and the stand-alone relay
   (tools/arc_relay/arc_relay.c), so both sequence commands by the same rules:
     - the wire format (messages: u32 length, u8 type, payload; strings: u16 length + bytes; little-endian)
     - the session view (players, turn owner, turn number, the simultaneous-turn bookkeeping) and how each sequenced
       entry changes it (view_apply)
     - the host's decisions: which commands it accepts from whom, which slot a joining player gets (rejoin by token or
       name, a stale link replaced, version check, full, started), unique names, desync detection from hashes
     - room codes, and the WebSocket pieces (SHA-1, base64, RFC 6455 handshake key, frame header encode and parse), and
       relay URLs
   Header only (static inline functions): no ARC types needed. A file that wants the match generator inside the view
   defines NCORE_RNG (a type) and NCORE_RNG_SEED(rng, seed) before including it (arc_net.c: arc_rng); the relay leaves
   them out. Everything is plain C11, MSVC-friendly. */
#ifndef ARC_NET_CORE_H
#define ARC_NET_CORE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#define NCORE_FN static __inline
#else
#define NCORE_FN static inline
#endif

#define NET_PROTO 1
#define NET_MAGIC 0x4E435241u              /* "ARCN" */
#define NCORE_MAXP 8                        /* = ARC_NET_MAX_PLAYERS */
/* 0.10.4 real-time matches through the relay: datagrams to the relay's UDP port (the same number as its TCP port).
   Every one: "ARU", NCORE_UDP_VER, u8 type, u64 session (the room's), then by type (little-endian):
     REG    client -> relay   u8 slot, u64 token (the HELLO token): "this address is my slot" (every second: keeps NAT
                              mappings open); the relay answers PEERS
     FWD    client -> relay   u8 from, u8 to, payload: the relay sends it on to `to`'s registered address as FROM
     FROM   relay -> client   u8 from, payload
     PEERS  relay -> client   u8 n, n x {u8 slot, u32 ip, u16 port} (host order): the room's other registered players as
                              the relay sees them (their public addresses: for hole punching)
     PUNCH / PUNCH_ACK        client <-> client, sent straight to a peer's public address: u8 from, u8 to
   Payloads are the rollback match's own datagrams ("ARB..."); a pair of players that hear each other's PUNCH talk
   directly, else through the relay. */
#define NCORE_UDP_VER 1
enum { NCORE_UDP_REG = 1, NCORE_UDP_FWD, NCORE_UDP_FROM, NCORE_UDP_PEERS, NCORE_UDP_PUNCH, NCORE_UDP_PUNCH_ACK };
#define NCORE_UDP_HDR 13                    /* "ARU" ver type session */
#define NCORE_UDP_MAX 1472                  /* largest datagram either side sends or takes */
#define NET_TEXT_MAX 4095
#define NCORE_FREE 1                        /* = ARC_NET_FREE */
#define NCORE_RELAY_PORT 47880              /* arc_relay's default port (WebSocket and raw ARC TCP on one port) */
#define NCORE_CODE_MIN 4
#define NCORE_CODE_MAX 6

/* ======================================================================== */
/* Buffers and messages                                                     */
/* ======================================================================== */
typedef struct { uint8_t *b; size_t n, cap; } net_buf;
NCORE_FN void nb_reserve(net_buf *b, size_t extra) {
    if (b->n + extra <= b->cap) return;
    size_t c = b->cap ? b->cap : 4096; while (c < b->n + extra) c *= 2;
    uint8_t *nb = (uint8_t *)realloc(b->b, c); if (!nb) { fprintf(stderr, "ARC fatal out of memory (net)\n"); exit(3); }
    b->b = nb; b->cap = c;
}
NCORE_FN void nb_put(net_buf *b, const void *p, size_t n) { if (!n) return; nb_reserve(b, n); memcpy(b->b + b->n, p, n); b->n += n; }
NCORE_FN void nb_drop_front(net_buf *b, size_t n) { if (n >= b->n) { b->n = 0; return; } memmove(b->b, b->b + n, b->n - n); b->n -= n; }
NCORE_FN void nb_free(net_buf *b) { free(b->b); memset(b, 0, sizeof *b); }

enum { M_HELLO = 1, M_WELCOME, M_REFUSE, M_CMD, M_SEQ, M_REFUSED, M_HASH, M_DESYNC, M_PING, M_PONG, M_PINGS, M_BYE, M_SYNC, M_SYNCED, M_CAUGHT_UP,
       M_RESTORE_FROM, M_RESTORED };       /* 0.8.5: a relay that restarted asks a returning player for the room's log */
enum { R_PROTOCOL = 1, R_VERSION, R_FULL, R_STARTED, R_SESSION, R_ROOM, R_RELAY };   /* (0.8.5: no such room; relay limits) */
/* the message being built (one at a time); m_finish writes its length */
static struct { uint8_t b[NET_TEXT_MAX + 1024]; size_t n; } g_msg;
NCORE_FN void m_begin(int type) { g_msg.n = 4; g_msg.b[g_msg.n++] = (uint8_t)type; }
NCORE_FN void m_u8(unsigned v) { if (g_msg.n < sizeof g_msg.b) g_msg.b[g_msg.n++] = (uint8_t)v; }
NCORE_FN void m_u16(unsigned v) { m_u8(v & 0xFF); m_u8((v >> 8) & 0xFF); }
NCORE_FN void m_u32(uint32_t v) { for (int i = 0; i < 4; i++) m_u8((v >> (8 * i)) & 0xFF); }
NCORE_FN void m_u64(uint64_t v) { for (int i = 0; i < 8; i++) m_u8((unsigned)((v >> (8 * i)) & 0xFF)); }
NCORE_FN void m_f64(double d) { uint64_t u; memcpy(&u, &d, 8); m_u64(u); }
NCORE_FN void m_str(const char *s) {
    size_t n = s ? strlen(s) : 0; if (n > NET_TEXT_MAX) n = NET_TEXT_MAX;
    m_u16((unsigned)n);
    if (g_msg.n + n <= sizeof g_msg.b) { memcpy(g_msg.b + g_msg.n, s, n); g_msg.n += n; }
}
NCORE_FN void m_finish(void) { uint32_t len = (uint32_t)(g_msg.n - 4); for (int i = 0; i < 4; i++) g_msg.b[i] = (uint8_t)(len >> (8 * i)); }
typedef struct { const uint8_t *p; size_t n, i; bool bad; } net_rd;
NCORE_FN unsigned r_u8(net_rd *r) { if (r->i + 1 > r->n) { r->bad = true; return 0; } return r->p[r->i++]; }
NCORE_FN unsigned r_u16(net_rd *r) { unsigned a = r_u8(r); return a | (r_u8(r) << 8); }
NCORE_FN uint32_t r_u32(net_rd *r) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)r_u8(r) << (8 * i); return v; }
NCORE_FN uint64_t r_u64(net_rd *r) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)r_u8(r) << (8 * i); return v; }
NCORE_FN double r_f64(net_rd *r) { uint64_t u = r_u64(r); double d; memcpy(&d, &u, 8); return d; }
NCORE_FN bool r_more(const net_rd *r) { return !r->bad && r->i < r->n; }
NCORE_FN void r_str(net_rd *r, char *out, size_t cap) {
    size_t n = r_u16(r);
    if (r->bad || r->i + n > r->n) { r->bad = true; if (cap) out[0] = 0; return; }
    size_t k = n < cap - 1 ? n : cap - 1;
    memcpy(out, r->p + r->i, k); out[k] = 0;
    for (size_t i = 0; i < k; i++) if ((unsigned char)out[i] < 32) out[i] = ' ';   /* one line, always */
    r->i += n;
}
/* the next whole message at the front of a byte stream: 1 = yes (type, reader, total size), 0 = not yet, -1 = garbled */
NCORE_FN int ncore_next(const net_buf *in, net_rd *r, int *type, size_t *total) {
    if (in->n < 4) return 0;
    uint32_t len = (uint32_t)in->b[0] | ((uint32_t)in->b[1] << 8) | ((uint32_t)in->b[2] << 16) | ((uint32_t)in->b[3] << 24);
    if (len < 1 || len > NET_TEXT_MAX + 1024) return -1;
    if (in->n < 4 + (size_t)len) return 0;
    *type = in->b[4]; r->p = in->b + 5; r->n = len - 1; r->i = 0; r->bad = false; *total = 4 + (size_t)len;
    return 1;
}

/* ======================================================================== */
/* The session view: what the sequence has made of the session so far       */
/* ======================================================================== */
enum { NK_CMD, NK_JOIN, NK_LEAVE, NK_REJOIN, NK_READY, NK_NAME, NK_START, NK_TURN, NK_COUNT };
static const char *const nk_names[NK_COUNT] = { "cmd", "join", "leave", "rejoin", "ready", "name", "start", "turn" };
#ifndef NCORE_RNG
typedef struct { uint64_t state, inc; } ncore_no_rng;
#define NCORE_RNG ncore_no_rng
#define NCORE_RNG_SEED(r, s) ((void)(r), (void)(s))
#endif
/* the same on every machine at the same sequence number (and on the host / relay after every entry it numbers) */
typedef struct {
    bool started; uint64_t seed; int32_t owner, turn;
    NCORE_RNG rng;                                     /* arc_rand_* inside command() (machines only) */
    struct { bool used, connected, ready, in_match; char name[32]; } p[NCORE_MAXP];
    bool simul; uint8_t done;                          /* 0.8.5 simultaneous turns: on, and who has ended this turn */
} net_view;
enum { NCORE_TURN_DONE = 1, NCORE_TURN_ADVANCED = 2 };   /* what view_apply did to the turn */
NCORE_FN int next_owner(const net_view *v) {
    for (int k = 1; k <= NCORE_MAXP; k++) { int i = (v->owner + k) % NCORE_MAXP; if (v->p[i].in_match && v->p[i].connected) return i; }
    return v->owner;
}
/* simultaneous turns: the players still to end this turn (connected, in the match, not done) */
NCORE_FN unsigned ncore_turn_pending(const net_view *v) {
    unsigned m = 0;
    for (int i = 0; i < NCORE_MAXP; i++) if (v->p[i].in_match && v->p[i].connected && !(v->done & (1u << i))) m |= 1u << i;
    return m;
}
NCORE_FN bool ncore_any_connected(const net_view *v) { for (int i = 0; i < NCORE_MAXP; i++) if (v->p[i].in_match && v->p[i].connected) return true; return false; }
/* a simultaneous turn ends when every connected player of the match has ended it (a player who is not connected does not
   hold it up: a drop that leaves only finished players ends the turn at that point of the sequence) */
NCORE_FN int ncore_simul_check(net_view *v) {
    if (!v->simul || !v->started || ncore_turn_pending(v) || !ncore_any_connected(v)) return 0;
    v->turn++; v->done = 0;
    return NCORE_TURN_ADVANCED;
}
NCORE_FN int view_apply(net_view *v, int kind, int from, const char *text) {
    bool okp = from >= 0 && from < NCORE_MAXP;
    int r = 0;
    switch (kind) {
    case NK_JOIN: if (okp) { v->p[from].used = v->p[from].connected = true; v->p[from].ready = false; snprintf(v->p[from].name, sizeof v->p[from].name, "%s", text); } break;
    case NK_REJOIN: if (okp) v->p[from].connected = true; break;
    case NK_LEAVE: if (okp) { v->p[from].connected = false; v->p[from].ready = false; r = ncore_simul_check(v); } break;
    case NK_READY: if (okp) v->p[from].ready = text[0] == '1'; break;
    case NK_NAME: if (okp) snprintf(v->p[from].name, sizeof v->p[from].name, "%s", text); break;
    case NK_START:
        v->started = true; v->seed = strtoull(text, NULL, 10); v->turn = 1; v->owner = -1;
        v->simul = strstr(text, "simultaneous") != NULL; v->done = 0;
        for (int i = 0; i < NCORE_MAXP; i++) { v->p[i].in_match = v->p[i].used && v->p[i].connected; if (v->owner < 0 && v->p[i].in_match) v->owner = i; }
        if (v->owner < 0) v->owner = 0;
        NCORE_RNG_SEED(v->rng, v->seed);
        break;
    case NK_TURN:
        if (v->simul && v->started) {
            if (okp) v->done |= (uint8_t)(1u << from);
            r = NCORE_TURN_DONE | ncore_simul_check(v);
            break;
        }
        v->turn++;
        if (v->started) v->owner = next_owner(v);
        r = NCORE_TURN_ADVANCED;
        break;
    default: break;
    }
    return r;
}

/* ======================================================================== */
/* The host's decisions (a LAN host for its remote players, the relay)      */
/* ======================================================================== */
/* may player p send this? NULL = yes (flags may gain FREE), else why not. owner_powers: p is the room's host on a relay
   (player 0, a remote machine there): it starts the match and may end an absent player's turn */
NCORE_FN const char *ncore_cmd_check(const net_view *v, int p, unsigned kind, unsigned *flags, bool owner_powers) {
    if (p < 0 || p >= NCORE_MAXP) return "not a player";
    switch (kind) {
    case NK_CMD:
        if (*flags & NCORE_FREE) return NULL;
        if (!v->started) return "the match has not started";
        if (v->simul) { if (!v->p[p].in_match) return "not in the match"; return (v->done & (1u << p)) ? "you have already ended this turn" : NULL; }
        return v->owner == p ? NULL : "not your turn";
    case NK_TURN:
        if (!v->started) return "the match has not started";
        if (v->simul) { if (!v->p[p].in_match) return "not in the match"; return (v->done & (1u << p)) ? "you have already ended this turn" : NULL; }
        if (v->owner == p) return NULL;
        if (owner_powers && v->owner >= 0 && v->owner < NCORE_MAXP && !v->p[v->owner].connected) return NULL;   /* the host passes an absent player's turn */
        return "not your turn";
    case NK_READY: case NK_NAME: *flags |= NCORE_FREE; return NULL;
    case NK_START:
        if (!owner_powers) return "only the host starts the match";
        if (v->started) return "the match has started already";
        *flags |= NCORE_FREE; return NULL;
    default: return "not a command";
    }
}
/* HELLO: the first message from a player (0.8.5 online fields follow the 0.8.0 ones; a LAN host ignores them) */
enum { NCORE_OP_LAN, NCORE_OP_CREATE, NCORE_OP_JOIN };
enum { NCORE_HELLO_RESTORE = 1 };   /* flags: this player holds the room's log and may restore it on a relay that lost it */
typedef struct {
    uint32_t magic; unsigned proto;
    char arcv[32], game[48], ver[32], name[32];
    uint64_t token, session; uint32_t last;
    int op; char room[8]; int max_players; char room_name[48]; unsigned flags;
} ncore_hello;
NCORE_FN void ncore_hello_write(const ncore_hello *h) {
    m_begin(M_HELLO); m_u32(h->magic); m_u16(h->proto);
    m_str(h->arcv); m_str(h->game); m_str(h->ver); m_str(h->name);
    m_u64(h->token); m_u64(h->session); m_u32(h->last);
    if (h->op != NCORE_OP_LAN) { m_u8((unsigned)h->op); m_str(h->room); m_u8((unsigned)h->max_players); m_str(h->room_name); m_u8(h->flags); }
}
NCORE_FN bool ncore_hello_read(net_rd *r, ncore_hello *h) {
    memset(h, 0, sizeof *h);
    h->magic = r_u32(r); h->proto = r_u16(r);
    r_str(r, h->arcv, sizeof h->arcv); r_str(r, h->game, sizeof h->game); r_str(r, h->ver, sizeof h->ver); r_str(r, h->name, sizeof h->name);
    h->token = r_u64(r); h->session = r_u64(r); h->last = r_u32(r);
    if (r_more(r)) { h->op = (int)r_u8(r); r_str(r, h->room, sizeof h->room); h->max_players = (int)r_u8(r); r_str(r, h->room_name, sizeof h->room_name); h->flags = r_u8(r); }
    return !r->bad && h->magic == NET_MAGIC && h->proto == NET_PROTO;
}
/* 0 = the same game, version and ARC version; else R_VERSION with the message both sides show */
NCORE_FN int ncore_version_check(const char *game, const char *ver, const char *arcv, const ncore_hello *h, char *msg, size_t n) {
    if (!strcmp(h->arcv, arcv) && !strcmp(h->game, game) && !strcmp(h->ver, ver)) return 0;
    snprintf(msg, n, "version mismatch: the host runs %s %s on ARC %s; you have %s %s on ARC %s", game, ver[0] ? ver : "(no version)", arcv,
             h->game, h->ver[0] ? h->ver : "(no version)", h->arcv);
    return R_VERSION;
}
/* players whose link is to be replaced by this new connection (a player coming back before the host noticed it was gone:
   the same running game by its token, or once the match has started the same name). first = the first remote player's
   index (1 on a LAN host, where player 0 is the host itself; 0 on a relay) */
NCORE_FN unsigned ncore_hello_stale(const net_view *v, const uint64_t *tokens, const bool *live, int first, const ncore_hello *h) {
    unsigned m = 0;
    for (int p = first; p < NCORE_MAXP; p++)
        if (v->p[p].used && v->p[p].connected && live[p] && ((h->token && tokens[p] == h->token) || (v->started && !strcmp(v->p[p].name, h->name)))) m |= 1u << p;
    return m;
}
/* the slot for a joining player (after the stale links are dropped): >= 0 with rejoin and from (the first log entry it
   needs), or -1 with a refusal code and message */
NCORE_FN int ncore_hello_slot(const net_view *v, const uint64_t *tokens, const bool *live, int first, int maxp, const ncore_hello *h,
                              uint64_t session, uint32_t log_n, bool *rejoin, uint32_t *from, int *code, const char **msg) {
    int slot = -1; *rejoin = false; *from = 0;
    for (int p = first; p < NCORE_MAXP && slot < 0; p++) if (v->p[p].used && !v->p[p].connected && !live[p] && tokens[p] && tokens[p] == h->token) { slot = p; *rejoin = true; }
    for (int p = first; p < NCORE_MAXP && slot < 0; p++) if (v->p[p].used && !v->p[p].connected && !live[p] && !strcmp(v->p[p].name, h->name)) { slot = p; *rejoin = true; }
    if (slot < 0 && v->started) { *code = R_STARTED; *msg = "the match has already started (only its players can rejoin, by name)"; return -1; }
    if (slot < 0) for (int p = first; p < maxp && slot < 0; p++) if (!v->p[p].used && !live[p]) slot = p;
    if (slot < 0) { *code = R_FULL; *msg = "the game is full"; return -1; }
    if (*rejoin && h->session == session && h->last <= log_n) *from = h->last;   /* it kept its state: only what it missed */
    else if (h->last > 0) { *code = R_SESSION; *msg = "you hold the state of another session: restart the game to join this one"; return -1; }
    return slot;
}
/* names stay unique: "Player" and "Player 2" */
NCORE_FN void ncore_unique_name(const net_view *v, const char *name, char *out, size_t n) {
    const char *base = name && name[0] ? name : "Player";
    snprintf(out, n, "%s", base);
    for (int k = 2; k < 99; k++) {
        bool clash = false; for (int p = 0; p < NCORE_MAXP; p++) if (v->p[p].used && !strcmp(v->p[p].name, out)) clash = true;
        if (!clash) break;
        snprintf(out, n, "%.24s %d", base, k);
    }
}
/* desync detection: every machine's hash after each sequence number. ref >= 0: that player's hash decides (a LAN host's
   own, player 0); ref < 0 (a relay, which runs no rules): any two hashes that differ. ring: ring_n rows, by seq % ring_n.
   true = a mismatch at seq */
#define NET_HASH_RING 1024
typedef struct { uint32_t seq; uint8_t have; uint64_t h[NCORE_MAXP]; } net_hrow;
NCORE_FN bool ncore_hash_put(net_hrow *ring, int ring_n, uint32_t seq, int player, uint64_t h, int ref) {
    if (player < 0 || player >= NCORE_MAXP || !seq || !ring || ring_n <= 0) return false;
    net_hrow *r = &ring[seq % (uint32_t)ring_n];
    if (r->seq != seq) { memset(r, 0, sizeof *r); r->seq = seq; }
    r->have |= (uint8_t)(1u << player); r->h[player] = h;
    int base = ref;
    if (base < 0) { for (int p = 0; p < NCORE_MAXP; p++) if (r->have & (1u << p)) { base = p; break; } }
    if (base < 0 || !(r->have & (1u << base))) return false;
    for (int p = 0; p < NCORE_MAXP; p++) if ((r->have & (1u << p)) && r->h[p] != r->h[base]) return true;
    return false;
}
/* the DESYNC message for a row: everyone writes a report once it has applied up to `upto` */
NCORE_FN void ncore_desync_msg(const net_hrow *r, uint32_t seq, uint32_t upto) {
    m_begin(M_DESYNC); m_u32(seq); m_u32(upto);
    int n = 0; for (int p = 0; p < NCORE_MAXP; p++) if (r->have & (1u << p)) n++;
    m_u8((unsigned)n);
    for (int p = 0; p < NCORE_MAXP; p++) if (r->have & (1u << p)) { m_u8((unsigned)p); m_u64(r->h[p]); }
}

/* ======================================================================== */
/* Room codes                                                               */
/* ======================================================================== */
/* 31 letters and digits that can't be mistaken for one another when read out or typed (no 0/O, 1/I/L) */
static const char ncore_code_alpha[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
#define NCORE_CODE_ALPHA 31
NCORE_FN void ncore_room_code(uint64_t rnd, int len, char *out) {
    if (len < NCORE_CODE_MIN) len = NCORE_CODE_MIN;
    if (len > NCORE_CODE_MAX) len = NCORE_CODE_MAX;
    for (int i = 0; i < len; i++) { out[i] = ncore_code_alpha[rnd % NCORE_CODE_ALPHA]; rnd /= NCORE_CODE_ALPHA; }
    out[len] = 0;
}
/* a typed code -> canonical (upper case, spaces and dashes dropped); false when it can't be a room code */
NCORE_FN bool ncore_code_normalize(const char *in, char *out, size_t cap) {
    size_t n = 0;
    if (!in || cap < NCORE_CODE_MAX + 1) return false;
    for (; *in; in++) {
        char c = *in;
        if (c == ' ' || c == '-' || c == '\t') continue;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!strchr(ncore_code_alpha, c) || !c) { out[0] = 0; return false; }
        if (n >= NCORE_CODE_MAX) { out[0] = 0; return false; }
        out[n++] = c;
    }
    out[n] = 0;
    return n >= NCORE_CODE_MIN;
}

/* ======================================================================== */
/* WebSocket pieces (RFC 6455): SHA-1, base64, the handshake key, frames    */
/* ======================================================================== */
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t n; } ncore_sha1;
NCORE_FN uint32_t ncore_rol(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
NCORE_FN void ncore_sha1_block(ncore_sha1 *s, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = ncore_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
        uint32_t t = ncore_rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ncore_rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}
NCORE_FN void ncore_sha1_init(ncore_sha1 *s) {
    s->h[0] = 0x67452301u; s->h[1] = 0xEFCDAB89u; s->h[2] = 0x98BADCFEu; s->h[3] = 0x10325476u; s->h[4] = 0xC3D2E1F0u; s->len = 0; s->n = 0;
}
NCORE_FN void ncore_sha1_add(ncore_sha1 *s, const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    s->len += (uint64_t)n;
    while (n) {
        size_t k = 64 - s->n; if (k > n) k = n;
        memcpy(s->buf + s->n, p, k); s->n += k; p += k; n -= k;
        if (s->n == 64) { ncore_sha1_block(s, s->buf); s->n = 0; }
    }
}
NCORE_FN void ncore_sha1_end(ncore_sha1 *s, uint8_t out[20]) {
    uint64_t bits = s->len * 8; uint8_t pad = 0x80, z = 0;
    ncore_sha1_add(s, &pad, 1);
    while (s->n != 56) ncore_sha1_add(s, &z, 1);
    uint8_t L[8]; for (int i = 0; i < 8; i++) L[i] = (uint8_t)(bits >> (56 - 8 * i));
    ncore_sha1_add(s, L, 8);
    for (int i = 0; i < 5; i++) { out[4 * i] = (uint8_t)(s->h[i] >> 24); out[4 * i + 1] = (uint8_t)(s->h[i] >> 16); out[4 * i + 2] = (uint8_t)(s->h[i] >> 8); out[4 * i + 3] = (uint8_t)s->h[i]; }
}
NCORE_FN void ncore_sha1_of(const void *data, size_t n, uint8_t out[20]) { ncore_sha1 s; ncore_sha1_init(&s); ncore_sha1_add(&s, data, n); ncore_sha1_end(&s, out); }
/* base64 (with padding) into out (cap >= 4 * ceil(n / 3) + 1) */
NCORE_FN size_t ncore_base64(const uint8_t *p, size_t n, char *out, size_t cap) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16; if (i + 1 < n) v |= (uint32_t)p[i + 1] << 8; if (i + 2 < n) v |= p[i + 2];
        if (o + 4 >= cap) break;
        out[o++] = T[(v >> 18) & 63]; out[o++] = T[(v >> 12) & 63];
        out[o++] = i + 1 < n ? T[(v >> 6) & 63] : '='; out[o++] = i + 2 < n ? T[v & 63] : '=';
    }
    if (cap) out[o < cap ? o : cap - 1] = 0;
    return o;
}
/* Sec-WebSocket-Accept for a Sec-WebSocket-Key: base64(SHA-1(key + the RFC's GUID)) (out: 29 bytes) */
NCORE_FN void ncore_ws_accept(const char *key, char out[32]) {
    static const char guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    ncore_sha1 s; uint8_t d[20];
    ncore_sha1_init(&s); ncore_sha1_add(&s, key, strlen(key)); ncore_sha1_add(&s, guid, sizeof guid - 1); ncore_sha1_end(&s, d);
    ncore_base64(d, 20, out, 32);
}
enum { WS_CONT = 0, WS_TEXT = 1, WS_BINARY = 2, WS_CLOSE = 8, WS_PING = 9, WS_PONG = 10 };
/* a frame header (FIN set) for a payload of n bytes; mask = 4 bytes (a client's frames) or NULL (a server's). Returns its
   size (2..14); the caller sends the payload after it, masked with ncore_ws_mask when mask is given */
NCORE_FN size_t ncore_ws_header(uint8_t out[14], int opcode, uint64_t n, const uint8_t *mask) {
    size_t k = 0;
    out[k++] = (uint8_t)(0x80 | (opcode & 15));
    uint8_t m = mask ? 0x80 : 0;
    if (n < 126) out[k++] = (uint8_t)(m | n);
    else if (n < 65536) { out[k++] = (uint8_t)(m | 126); out[k++] = (uint8_t)(n >> 8); out[k++] = (uint8_t)n; }
    else { out[k++] = (uint8_t)(m | 127); for (int i = 7; i >= 0; i--) out[k++] = (uint8_t)(n >> (8 * i)); }
    if (mask) { memcpy(out + k, mask, 4); k += 4; }
    return k;
}
NCORE_FN void ncore_ws_mask(uint8_t *p, size_t n, const uint8_t mask[4]) { for (size_t i = 0; i < n; i++) p[i] ^= mask[i & 3]; }
typedef struct { int opcode; bool fin, masked; uint64_t len; size_t hdr; uint8_t mask[4]; } ncore_wsf;
/* a whole frame at the front of b? > 0: its total size (header f->hdr bytes, then f->len payload bytes, still masked when
   f->masked); 0: not all here yet; -1: not a valid frame (reserved bits, a control frame too long or fragmented, a
   payload over max) */
NCORE_FN long ncore_ws_parse(const uint8_t *b, size_t n, uint64_t max, ncore_wsf *f) {
    memset(f, 0, sizeof *f);
    if (n < 2) return 0;
    if (b[0] & 0x70) return -1;                        /* no extensions were agreed */
    f->fin = (b[0] & 0x80) != 0; f->opcode = b[0] & 15; f->masked = (b[1] & 0x80) != 0;
    if (f->opcode != WS_CONT && f->opcode != WS_TEXT && f->opcode != WS_BINARY && f->opcode != WS_CLOSE && f->opcode != WS_PING && f->opcode != WS_PONG) return -1;
    uint64_t len = b[1] & 127; size_t k = 2;
    if (len == 126) { if (n < 4) return 0; len = ((uint64_t)b[2] << 8) | b[3]; k = 4; }
    else if (len == 127) { if (n < 10) return 0; len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | b[2 + i]; k = 10; }
    if (f->opcode >= 8 && (len > 125 || !f->fin)) return -1;
    if (len > max) return -1;
    if (f->masked) { if (n < k + 4) return 0; memcpy(f->mask, b + k, 4); k += 4; }
    f->len = len; f->hdr = k;
    if ((uint64_t)(n - k) < len) return 0;
    return (long)(k + len);
}
/* the value of an HTTP header in a request or response (case-insensitive name), trimmed, into out; false if absent */
NCORE_FN bool ncore_http_header(const char *head, const char *name, char *out, size_t cap) {
    size_t nl = strlen(name);
    for (const char *l = strstr(head, "\r\n"); l && l[2]; l = strstr(l + 2, "\r\n")) {
        const char *s = l + 2; size_t i = 0;
        while (i < nl && s[i] && (s[i] | 32) == (name[i] | 32)) i++;
        if (i != nl || s[i] != ':') continue;
        s += nl + 1; while (*s == ' ' || *s == '\t') s++;
        const char *e = strstr(s, "\r\n"); size_t k = e ? (size_t)(e - s) : strlen(s);
        while (k && (s[k - 1] == ' ' || s[k - 1] == '\t')) k--;
        if (k >= cap) k = cap - 1;
        memcpy(out, s, k); out[k] = 0;
        return true;
    }
    return false;
}
/* does a comma-separated header value hold a token (case-insensitive)? ("Connection: keep-alive, Upgrade") */
NCORE_FN bool ncore_http_token(const char *value, const char *token) {
    size_t tl = strlen(token);
    for (const char *s = value; *s; ) {
        while (*s == ' ' || *s == ',') s++;
        size_t i = 0; while (i < tl && s[i] && (s[i] | 32) == (token[i] | 32)) i++;
        if (i == tl && (s[i] == 0 || s[i] == ',' || s[i] == ' ')) return true;
        while (*s && *s != ',') s++;
    }
    return false;
}

/* ======================================================================== */
/* Relay addresses                                                          */
/* ======================================================================== */
/* "ws://host[:port][/path]", "wss://...", "tcp://host[:port]" (raw ARC protocol), or "host[:port][/path]" (ws).
   scheme: 1 ws, 2 wss, 3 tcp. Default ports: ws 80, wss 443, tcp NCORE_RELAY_PORT */
enum { NCORE_URL_WS = 1, NCORE_URL_WSS, NCORE_URL_TCP };
typedef struct { int scheme; char host[128]; int port; char path[128]; } ncore_url;
NCORE_FN bool ncore_parse_url(const char *s, ncore_url *u) {
    memset(u, 0, sizeof *u);
    if (!s) return false;
    while (*s == ' ') s++;
    u->scheme = NCORE_URL_WS;
    if (!strncmp(s, "ws://", 5)) s += 5;
    else if (!strncmp(s, "wss://", 6)) { u->scheme = NCORE_URL_WSS; s += 6; }
    else if (!strncmp(s, "tcp://", 6)) { u->scheme = NCORE_URL_TCP; s += 6; }
    else if (strstr(s, "://")) return false;
    size_t hn = strcspn(s, ":/ ");
    if (!hn || hn >= sizeof u->host) return false;
    memcpy(u->host, s, hn); u->host[hn] = 0; s += hn;
    u->port = u->scheme == NCORE_URL_WSS ? 443 : u->scheme == NCORE_URL_TCP ? NCORE_RELAY_PORT : 80;
    if (*s == ':') { char *e; long p = strtol(s + 1, &e, 10); if (e == s + 1 || p <= 0 || p > 65535) return false; u->port = (int)p; s = e; }
    size_t pn = strcspn(s, " ");
    if (pn && *s != '/') return false;
    if (pn >= sizeof u->path) return false;
    if (pn) { memcpy(u->path, s, pn); u->path[pn] = 0; } else snprintf(u->path, sizeof u->path, "/");
    return true;
}
#endif /* ARC_NET_CORE_H */
