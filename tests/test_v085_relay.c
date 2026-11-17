/* ARC 0.8.5 Phase G1 self-test (online play through arc_relay, simultaneous turns): included once by test_core.c;
   v085r_init() runs in init, v085r_check() at tick 9. Every name here starts with v085r_ (all test files share one
   translation unit, where same-named statics would silently merge).
   Part 1: the shared session core (src/arc_net_core.h, also built into arc_relay): SHA-1 and base64 test vectors, the
   RFC 6455 handshake key, WebSocket frame headers and parsing (the RFC's own examples), HTTP headers, room codes, relay
   URLs, and the host's decisions (who may send what in alternating and simultaneous turns, joining slots, hashes).
   Part 2: a simultaneous-turn session of three machines in this process over in-process links: orders sent in a
   scrambled order are applied in one sequence everywhere, the turn advances once after the third end of turn, a
   dropped player does not hold the turn, a second end of turn is refused by the host, and ARC's snapshot keeps it.
   Part 3: the online API without a relay process: refusals, the lobby's online widgets (code entry by the select
   boxes), and a WebSocket client handshake and framing against a socket this test listens on (POSIX only). */
#include "../src/arc_net_core.h"
#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/tcp.h>
#endif
void arc__net_test_sockets_on(void);
bool arc__net_test_auto_retry(void);

/* ======================================================================== */
/* Part 1: the shared core                                                  */
/* ======================================================================== */
static void v085r_hex(const uint8_t *d, int n, char *out) { for (int i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", d[i]); }
static bool v085r_sha(const char *msg, const char *want) { uint8_t d[20]; char h[41]; ncore_sha1_of(msg, strlen(msg), d); v085r_hex(d, 20, h); return !strcmp(h, want); }
static bool v085r_b64(const char *in, const char *want) { char o[64]; ncore_base64((const uint8_t *)in, strlen(in), o, sizeof o); return !strcmp(o, want); }
static void v085r_core(void) {
    /* SHA-1: FIPS 180 / RFC 3174 vectors, and a million 'a's fed in uneven pieces */
    CHECK(v085r_sha("abc", "a9993e364706816aba3e25717850c26c9cd0d89d"));
    CHECK(v085r_sha("", "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    CHECK(v085r_sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
    { ncore_sha1 s; ncore_sha1_init(&s); char a[1000]; memset(a, 'a', sizeof a); size_t left = 1000000, k = 1;
      while (left) { size_t n = k < left ? k : left; if (n > sizeof a) n = sizeof a; ncore_sha1_add(&s, a, n); left -= n; k = k * 7 % 997 + 1; }
      uint8_t d[20]; char h[41]; ncore_sha1_end(&s, d); v085r_hex(d, 20, h); CHECK(!strcmp(h, "34aa973cd4c4daa4f61eeb2bdbad27316534016f")); }
    /* base64 (RFC 4648 section 10) */
    CHECK(v085r_b64("", "") && v085r_b64("f", "Zg==") && v085r_b64("fo", "Zm8=") && v085r_b64("foo", "Zm9v") && v085r_b64("foob", "Zm9vYg==") && v085r_b64("foobar", "Zm9vYmFy"));
    /* the handshake: RFC 6455 section 1.3's key and accept value */
    char acc[32]; ncore_ws_accept("dGhlIHNhbXBsZSBub25jZQ==", acc); CHECK(!strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    /* frames: RFC 6455 section 5.7 (an unmasked and a masked "Hello", a ping), and the 16 / 64-bit lengths */
    static const uint8_t v085r_hello[] = { 0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f };
    static const uint8_t v085r_hellom[] = { 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 };
    ncore_wsf f; uint8_t h[14];
    CHECK(ncore_ws_parse(v085r_hello, sizeof v085r_hello, 1000, &f) == 7 && f.fin && f.opcode == WS_TEXT && !f.masked && f.len == 5 && f.hdr == 2 && !memcmp(v085r_hello + 2, "Hello", 5));
    uint8_t mb[sizeof v085r_hellom]; memcpy(mb, v085r_hellom, sizeof mb);
    CHECK(ncore_ws_parse(mb, sizeof mb, 1000, &f) == 11 && f.masked && f.len == 5 && f.hdr == 6);
    ncore_ws_mask(mb + f.hdr, 5, f.mask); CHECK(!memcmp(mb + 6, "Hello", 5));
    CHECK(ncore_ws_parse(v085r_hello, 6, 1000, &f) == 0 && ncore_ws_parse(v085r_hello, 1, 1000, &f) == 0);   /* not all here yet */
    CHECK(ncore_ws_header(h, WS_TEXT, 5, NULL) == 2 && !memcmp(h, v085r_hello, 2));
    static const uint8_t v085r_m4[4] = { 0x37, 0xfa, 0x21, 0x3d };
    CHECK(ncore_ws_header(h, WS_TEXT, 5, v085r_m4) == 6 && !memcmp(h, v085r_hellom, 6));
    CHECK(ncore_ws_header(h, WS_BINARY, 256, NULL) == 4 && h[0] == 0x82 && h[1] == 0x7E && h[2] == 0x01 && h[3] == 0x00);
    CHECK(ncore_ws_header(h, WS_BINARY, 65536, NULL) == 10 && h[1] == 0x7F && h[7] == 0x01 && h[8] == 0 && h[9] == 0 && h[2] == 0);
    { static uint8_t big[4 + 300]; memset(big, 0, sizeof big); size_t k = ncore_ws_header(big, WS_BINARY, 300, NULL); CHECK(k == 4 && ncore_ws_parse(big, k + 300, 1000, &f) == (long)(k + 300) && f.len == 300);
      CHECK(ncore_ws_parse(big, k + 300, 299, &f) == -1); }                                        /* over the limit */
    static const uint8_t v085r_rsv[] = { 0xC1, 0x00 }, v085r_bigping[] = { 0x89, 0x7E, 0x00, 0x80 }, v085r_fragping[] = { 0x09, 0x00 }, v085r_badop[] = { 0x83, 0x00 };
    CHECK(ncore_ws_parse(v085r_rsv, 2, 100, &f) == -1 && ncore_ws_parse(v085r_bigping, 4, 1000, &f) == -1 && ncore_ws_parse(v085r_fragping, 2, 100, &f) == -1 && ncore_ws_parse(v085r_badop, 2, 100, &f) == -1);
    /* HTTP headers (any case, trimmed) and Connection tokens */
    const char *req = "GET /arc HTTP/1.1\r\nHost: x\r\nupgrade:  WebSocket \r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Key: abc==\r\n\r\n";
    char v[64];
    CHECK(ncore_http_header(req, "Upgrade", v, sizeof v) && !strcmp(v, "WebSocket") && ncore_http_token(v, "websocket"));
    CHECK(ncore_http_header(req, "connection", v, sizeof v) && ncore_http_token(v, "upgrade") && !ncore_http_token(v, "close") && !ncore_http_header(req, "Origin", v, sizeof v));
    CHECK(ncore_http_header(req, "Sec-WebSocket-Key", v, sizeof v) && !strcmp(v, "abc=="));
    /* room codes: from the unambiguous alphabet, the length asked, spread out; typed codes normalised */
    char c[8]; int bad = 0, same = 0; char prev[8] = "";
    uint64_t r = 12345;
    for (int i = 0; i < 2000; i++) {
        r = r * 6364136223846793005ull + 1442695040888963407ull;
        ncore_room_code(r >> 11, 4 + i % 3, c);
        if ((int)strlen(c) != 4 + i % 3) bad++;
        for (const char *p = c; *p; p++) if (!strchr("23456789ABCDEFGHJKMNPQRSTUVWXYZ", *p) || strchr("01OIL", *p)) bad++;
        if (!strcmp(c, prev)) same++;
        memcpy(prev, c, sizeof c);
    }
    CHECK(bad == 0 && same == 0 && NCORE_CODE_ALPHA == 31 && strlen(ncore_code_alpha) == 31);
    { int hist[NCORE_CODE_ALPHA] = { 0 }; for (int i = 0; i < 31 * 64; i++) { ncore_room_code((uint64_t)i * 0x9E3779B97F4A7C15ull >> 7, 4, c); hist[strchr(ncore_code_alpha, c[0]) - ncore_code_alpha]++; }
      int lo = 99999, hi = 0; for (int i = 0; i < NCORE_CODE_ALPHA; i++) { if (hist[i] < lo) lo = hist[i]; if (hist[i] > hi) hi = hist[i]; } CHECK(lo > 20 && hi < 120); }
    CHECK(ncore_code_normalize("k7qf", c, sizeof c) && !strcmp(c, "K7QF") && ncore_code_normalize(" K7-QF ", c, sizeof c) && !strcmp(c, "K7QF"));
    CHECK(!ncore_code_normalize("K0QF", c, sizeof c) && !ncore_code_normalize("KIQF", c, sizeof c) && !ncore_code_normalize("K7Q", c, sizeof c) && !ncore_code_normalize("K7QFK7Q", c, sizeof c) && ncore_code_normalize("K7QFK7", c, sizeof c));
    /* relay URLs */
    ncore_url u;
    CHECK(ncore_parse_url("ws://relay.example.com:47880/arc", &u) && u.scheme == NCORE_URL_WS && !strcmp(u.host, "relay.example.com") && u.port == 47880 && !strcmp(u.path, "/arc"));
    CHECK(ncore_parse_url("wss://relay.example.com", &u) && u.scheme == NCORE_URL_WSS && u.port == 443 && !strcmp(u.path, "/"));
    CHECK(ncore_parse_url("tcp://10.0.0.2", &u) && u.scheme == NCORE_URL_TCP && u.port == NCORE_RELAY_PORT && ncore_parse_url("localhost:9", &u) && u.scheme == NCORE_URL_WS && u.port == 9);
    CHECK(!ncore_parse_url("http://x", &u) && !ncore_parse_url("ws://x:99999", &u) && !ncore_parse_url("ws://:80", &u) && !ncore_parse_url("ws://x:80path", &u));
    /* the host's decisions: alternating turns */
    net_view vw; memset(&vw, 0, sizeof vw); vw.owner = -1;
    view_apply(&vw, NK_JOIN, 0, "A"); view_apply(&vw, NK_JOIN, 1, "B"); view_apply(&vw, NK_JOIN, 2, "C");
    unsigned fl = 0;
    CHECK(ncore_cmd_check(&vw, 1, NK_CMD, &fl, false) != NULL && (fl = NCORE_FREE, ncore_cmd_check(&vw, 1, NK_CMD, &fl, false) == NULL));   /* only free before the start */
    fl = 0; CHECK(ncore_cmd_check(&vw, 1, NK_READY, &fl, false) == NULL && fl == NCORE_FREE);
    fl = 0; CHECK(ncore_cmd_check(&vw, 1, NK_START, &fl, false) != NULL && ncore_cmd_check(&vw, 0, NK_START, &fl, true) == NULL);       /* the room's host starts */
    view_apply(&vw, NK_START, 0, "77");
    CHECK(vw.started && !vw.simul && vw.owner == 0 && vw.turn == 1 && vw.seed == 77);
    fl = 0; CHECK(ncore_cmd_check(&vw, 0, NK_CMD, &fl, false) == NULL && ncore_cmd_check(&vw, 1, NK_CMD, &fl, false) != NULL && ncore_cmd_check(&vw, 1, NK_TURN, &fl, false) != NULL);
    CHECK(ncore_cmd_check(&vw, 0, NK_START, &fl, true) != NULL);                                                                        /* once */
    CHECK(view_apply(&vw, NK_TURN, 0, "") == NCORE_TURN_ADVANCED && vw.owner == 1 && vw.turn == 2);
    view_apply(&vw, NK_LEAVE, 1, "");
    fl = 0; CHECK(ncore_cmd_check(&vw, 2, NK_TURN, &fl, false) != NULL && ncore_cmd_check(&vw, 0, NK_TURN, &fl, false) != NULL && ncore_cmd_check(&vw, 0, NK_TURN, &fl, true) == NULL);   /* absent owner: the host passes it */
    view_apply(&vw, NK_TURN, 0, ""); CHECK(vw.owner == 2);                                                                              /* the absent player is skipped */
    /* joining: rejoin by name or token, a stale link replaced, full, started, another session */
    uint64_t tok[NCORE_MAXP] = { 11, 22, 33 }; bool live[NCORE_MAXP] = { true, false, true };
    ncore_hello hh; memset(&hh, 0, sizeof hh); snprintf(hh.name, sizeof hh.name, "B"); hh.session = 5; hh.last = 4;
    bool rj; uint32_t from; int code; const char *why;
    CHECK(ncore_hello_slot(&vw, tok, live, 0, 4, &hh, 5, 9, &rj, &from, &code, &why) == 1 && rj && from == 4);
    hh.session = 6; CHECK(ncore_hello_slot(&vw, tok, live, 0, 4, &hh, 5, 9, &rj, &from, &code, &why) < 0 && code == R_SESSION);
    hh.last = 0; CHECK(ncore_hello_slot(&vw, tok, live, 0, 4, &hh, 5, 9, &rj, &from, &code, &why) == 1 && from == 0);              /* a restarted game: all of it */
    snprintf(hh.name, sizeof hh.name, "D"); CHECK(ncore_hello_slot(&vw, tok, live, 0, 4, &hh, 5, 9, &rj, &from, &code, &why) < 0 && code == R_STARTED);
    snprintf(hh.name, sizeof hh.name, "C"); hh.token = 33; CHECK(ncore_hello_stale(&vw, tok, live, 0, &hh) == (1u << 2));          /* C again, same running game */
    hh.token = 99; CHECK(ncore_hello_stale(&vw, tok, live, 0, &hh) == (1u << 2));                                                   /* started: by name too */
    net_view lob; memset(&lob, 0, sizeof lob); lob.owner = -1; view_apply(&lob, NK_JOIN, 0, "A"); view_apply(&lob, NK_JOIN, 1, "A 2");
    bool live2[NCORE_MAXP] = { true, true }; hh.last = 0; snprintf(hh.name, sizeof hh.name, "Z"); hh.token = 1;
    CHECK(ncore_hello_slot(&lob, tok, live2, 0, 2, &hh, 5, 2, &rj, &from, &code, &why) < 0 && code == R_FULL);
    CHECK(ncore_hello_slot(&lob, tok, live2, 0, 3, &hh, 5, 2, &rj, &from, &code, &why) == 2 && !rj);
    char nm[32]; ncore_unique_name(&lob, "A", nm, sizeof nm); CHECK(!strcmp(nm, "A 3"));
    char msg[300]; snprintf(hh.game, sizeof hh.game, "g"); snprintf(hh.ver, sizeof hh.ver, "2"); snprintf(hh.arcv, sizeof hh.arcv, "0.8.4");
    CHECK(ncore_version_check("g", "2", "0.8.4", &hh, msg, sizeof msg) == 0 && ncore_version_check("g", "3", "0.8.4", &hh, msg, sizeof msg) == R_VERSION && strstr(msg, "version mismatch") && strstr(msg, "g 3"));
    /* hashes: the LAN host's own decides; on the relay any two that differ */
    static net_hrow v085r_ring[NET_HASH_RING]; memset(v085r_ring, 0, sizeof v085r_ring);
    CHECK(!ncore_hash_put(v085r_ring, NET_HASH_RING, 7, 1, 100, 0) && !ncore_hash_put(v085r_ring, NET_HASH_RING, 7, 0, 100, 0) && ncore_hash_put(v085r_ring, NET_HASH_RING, 7, 2, 101, 0));
    CHECK(!ncore_hash_put(v085r_ring, NET_HASH_RING, 8, 3, 5, -1) && !ncore_hash_put(v085r_ring, NET_HASH_RING, 8, 1, 5, -1) && ncore_hash_put(v085r_ring, NET_HASH_RING, 8, 2, 6, -1));
    /* HELLO round trip, with the online fields (a LAN host reads the 0.8.0 part and ignores the rest) */
    ncore_hello ho; memset(&ho, 0, sizeof ho); ho.magic = NET_MAGIC; ho.proto = NET_PROTO; snprintf(ho.name, sizeof ho.name, "Ann"); ho.token = 9; ho.session = 3; ho.last = 2;
    ho.op = NCORE_OP_JOIN; snprintf(ho.room, sizeof ho.room, "K7QF"); ho.max_players = 4; snprintf(ho.room_name, sizeof ho.room_name, "Ann's game"); ho.flags = NCORE_HELLO_RESTORE;
    ncore_hello_write(&ho); m_finish();
    net_buf nbf; memset(&nbf, 0, sizeof nbf); nb_put(&nbf, g_msg.b, g_msg.n);
    net_rd rd; int ty; size_t tot; ncore_hello back;
    CHECK(ncore_next(&nbf, &rd, &ty, &tot) == 1 && ty == M_HELLO && tot == g_msg.n && ncore_hello_read(&rd, &back) && back.op == NCORE_OP_JOIN && !strcmp(back.room, "K7QF") &&
          back.max_players == 4 && back.flags == NCORE_HELLO_RESTORE && back.last == 2 && !strcmp(back.room_name, "Ann's game"));
    ho.op = NCORE_OP_LAN; ncore_hello_write(&ho); m_finish(); nbf.n = 0; nb_put(&nbf, g_msg.b, g_msg.n);
    CHECK(ncore_next(&nbf, &rd, &ty, &tot) == 1 && ncore_hello_read(&rd, &back) && back.op == NCORE_OP_LAN && !back.room[0]);
    nbf.b[0] = 0xFF; nbf.b[3] = 0x7F; CHECK(ncore_next(&nbf, &rd, &ty, &tot) == -1);                                                  /* a length no message has */
    nb_free(&nbf);
}
/* simultaneous turns in the view alone: who has ended, the advance at the last one, a drop that ends the turn, a rejoin */
static void v085r_view_simul(void) {
    net_view v; memset(&v, 0, sizeof v); v.owner = -1;
    view_apply(&v, NK_JOIN, 0, "A"); view_apply(&v, NK_JOIN, 1, "B"); view_apply(&v, NK_JOIN, 2, "C");
    view_apply(&v, NK_START, 0, "5 simultaneous");
    CHECK(v.simul && v.started && v.turn == 1 && ncore_turn_pending(&v) == 7u);
    unsigned fl = 0;
    CHECK(ncore_cmd_check(&v, 2, NK_CMD, &fl, false) == NULL && ncore_cmd_check(&v, 1, NK_CMD, &fl, false) == NULL);   /* anyone */
    CHECK(view_apply(&v, NK_TURN, 1, "") == NCORE_TURN_DONE && v.turn == 1 && ncore_turn_pending(&v) == 5u);
    CHECK(ncore_cmd_check(&v, 1, NK_TURN, &fl, false) != NULL && ncore_cmd_check(&v, 1, NK_CMD, &fl, false) != NULL);   /* once, and done */
    fl = NCORE_FREE; CHECK(ncore_cmd_check(&v, 1, NK_CMD, &fl, false) == NULL);                                         /* chat still goes */
    CHECK(view_apply(&v, NK_TURN, 0, "") == NCORE_TURN_DONE && ncore_turn_pending(&v) == 4u);
    CHECK(view_apply(&v, NK_TURN, 2, "") == (NCORE_TURN_DONE | NCORE_TURN_ADVANCED) && v.turn == 2 && v.done == 0 && ncore_turn_pending(&v) == 7u);
    view_apply(&v, NK_TURN, 0, ""); view_apply(&v, NK_TURN, 2, "");
    CHECK(v.turn == 2 && view_apply(&v, NK_LEAVE, 1, "") == NCORE_TURN_ADVANCED && v.turn == 3);                         /* the drop ends it */
    CHECK(ncore_turn_pending(&v) == 5u && view_apply(&v, NK_REJOIN, 1, "") == 0 && ncore_turn_pending(&v) == 7u);        /* back: counts again */
    view_apply(&v, NK_TURN, 1, ""); CHECK(view_apply(&v, NK_LEAVE, 0, "") == 0 && view_apply(&v, NK_LEAVE, 2, "") == NCORE_TURN_ADVANCED && v.turn == 4);
    CHECK(view_apply(&v, NK_LEAVE, 1, "") == 0 && v.turn == 4);                                                         /* nobody left: no turn ends */
}

/* ======================================================================== */
/* Part 2: a simultaneous-turn session of three machines (in-process links) */
/* ======================================================================== */
typedef struct { int orders, turns, dones; uint32_t turn_seq, last_done_seq, h; char log[96]; int refusals, rooms; } v085r_rules;   /* (hashed: up to refusals) */
#define V085R_M 4
static v085r_rules v085r_g[V085R_M]; static void *v085r_m[V085R_M]; static v085r_rules *v085r_cur; static double v085r_t;
static bool v085r_command(const char *name, int argc, const char **argv) {
    v085r_rules *g = v085r_cur;
    if (!strcmp(name, "o") && argc >= 1) {
        char t[24]; snprintf(t, sizeof t, "%d%s ", arc_net_command_from(), argv[0]);
        strncat(g->log, t, sizeof g->log - strlen(g->log) - 1); g->orders++;
        for (const char *p = t; *p; p++) { g->h ^= (uint8_t)*p; g->h *= 16777619u; }
        return true;
    }
    return !strcmp(name, "chat");
}
static void v085r_event(const arc_net_ev *ev) {
    v085r_rules *g = v085r_cur;
    if (ev->type == ARC_NET_EV_TURN) { g->turns++; g->turn_seq = ev->seq; if (ev->player != ARC_NET_EVERYONE) g->h ^= 0xBAD; }
    if (ev->type == ARC_NET_EV_TURN_DONE) { g->dones++; g->last_done_seq = ev->seq; }
    if (ev->type == ARC_NET_EV_REFUSED) g->refusals++;
    if (ev->type == ARC_NET_EV_ROOM) g->rooms++;
}
static size_t v085r_state(void *buf, size_t cap) { size_t n = offsetof(v085r_rules, refusals); if (cap >= n) memcpy(buf, v085r_cur, n); return n; }
static void v085r_on(int i) { arc__net_test_use(v085r_m[i]); v085r_cur = &v085r_g[i]; }
static void v085r_frames(int n) {
    for (int k = 0; k < n; k++) {
        v085r_t += 1.0 / 60.0; arc__net_test_time(v085r_t);
        for (int i = 0; i < V085R_M; i++) if (v085r_m[i]) { v085r_on(i); arc__net_frame(); arc__net_tick_begin(); }
    }
}
static void v085r_new(int i, const char *name, int turns, int lag) {
    v085r_m[i] = arc__net_test_machine(); memset(&v085r_g[i], 0, sizeof v085r_g[i]); v085r_on(i);
    arc_net_desc d; memset(&d, 0, sizeof d); d.game = "v085r"; d.version = "1"; d.max_players = 4; d.port = 7301; d.turns = turns;
    arc_net_setup(&d); arc_net_set_name(name);
    arc_net_sim(arc_fmt("lag=%d,jitter=%d,seed=%d", lag, lag / 2, i + 3));
}
static void v085r_kill(int i) { if (v085r_m[i]) { arc__net_test_free(v085r_m[i]); v085r_m[i] = NULL; } }
static bool v085r_settled(int n) {
    v085r_on(0); int want = arc_net_log_count();
    for (int i = 0; i < n; i++) { v085r_on(i); if ((int)arc_net_seq() != want || arc__net_test_inflight()) return false; }
    return true;
}
static void v085r_settle(int n) { for (int k = 0; k < 600 && !v085r_settled(n); k++) v085r_frames(1); v085r_frames(2); }
static bool v085r_all_same(int n) { for (int i = 1; i < n; i++) if (memcmp(&v085r_g[0], &v085r_g[i], offsetof(v085r_rules, refusals))) return false; return true; }
static void v085r_session(void) {
    v085r_new(0, "A", ARC_NET_TURNS_SIMULTANEOUS, 40); v085r_new(1, "B", 0, 10); v085r_new(2, "C", 0, 70);   /* (the host's setting decides) */
    v085r_on(0); CHECK(arc_net_host(NULL));
    v085r_on(1); CHECK(arc_net_join("mem:7301")); v085r_on(2); CHECK(arc_net_join("mem:7301"));
    v085r_frames(60);
    v085r_on(0); CHECK(arc_net_start()); v085r_settle(3);
    for (int i = 0; i < 3; i++) { v085r_on(i); CHECK(arc_net_started() && arc_net_turn_owner() == ARC_NET_EVERYONE && arc_net_my_turn() && arc_net_turn_waiting() == 7u && arc_net_turn() == 1); }
    /* scrambled: C, A, B, then B ends, C and A again, A ends, C ends (on links of different lag) */
    v085r_on(2); CHECK(arc_net_command("o c1")); v085r_frames(2);
    v085r_on(0); CHECK(arc_net_command("o a1")); v085r_on(1); CHECK(arc_net_command("o b1")); v085r_frames(1);
    v085r_on(1); CHECK(arc_net_turn_end()); v085r_on(2); CHECK(arc_net_command("o c2")); v085r_frames(3);
    v085r_on(0); CHECK(arc_net_command("o a2"));
    v085r_settle(3);
    for (int i = 0; i < 3; i++) { v085r_on(i); CHECK(arc_net_turn() == 1 && arc_net_turn_waiting() == 5u && v085r_g[i].dones == 1 && v085r_g[i].turns == 0); }
    v085r_on(1); CHECK(!arc_net_my_turn() && !arc_net_command("o b2") && !arc_net_turn_end());       /* done: refused at once */
    v085r_on(0); CHECK(arc_net_my_turn() && arc_net_turn_end()); v085r_on(2); CHECK(arc_net_command("o c3"));
    v085r_settle(3);
    for (int i = 0; i < 3; i++) { v085r_on(i); CHECK(arc_net_turn() == 1 && arc_net_turn_waiting() == 4u); }
    v085r_on(2); CHECK(arc_net_turn_end()); v085r_settle(3);
    for (int i = 0; i < 3; i++) {
        v085r_on(i); v085r_rules *g = &v085r_g[i];
        CHECK(arc_net_turn() == 2 && g->turns == 1 && g->dones == 3 && g->turn_seq == g->last_done_seq && arc_net_turn_waiting() == 7u && arc_net_my_turn());
        CHECK(g->orders == 6);
    }
    CHECK(v085r_all_same(3) && strstr(v085r_g[0].log, "2c1") && strstr(v085r_g[0].log, "2c3"));
    { uint64_t h0 = 0; for (int i = 0; i < 3; i++) { v085r_on(i); uint64_t h = arc__net_test_hash(); if (!i) h0 = h; else CHECK(h == h0 && h != 0); } }
    /* ARC's snapshot keeps the turn bookkeeping (mid-turn, A has ended) */
    v085r_on(0); CHECK(arc_net_turn_end()); v085r_settle(3);
    v085r_on(1); { uint8_t buf[2048]; arc_writer w = { buf, sizeof buf, 0 }; arc__net_state(&w);
      arc_reader rr = { buf, w.len, 0, false }; CHECK(arc__net_state_read(&rr, true) && arc_net_turn_waiting() == 6u && arc_net_turn() == 2 && arc_net_my_turn()); }
    /* a second end of turn sent before the first comes back: the host refuses it */
    v085r_on(1); int ref = v085r_g[1].refusals; CHECK(arc_net_turn_end() && arc_net_turn_end()); v085r_settle(3);
    v085r_on(1); CHECK(v085r_g[1].refusals == ref + 1 && arc_net_turn_waiting() == 4u);
    /* C drops while the others have ended: the turn ends at its LEFT, on every machine still here */
    v085r_on(2); arc__net_test_drop(); v085r_frames(30); v085r_settle(2);
    for (int i = 0; i < 2; i++) { v085r_on(i); CHECK(arc_net_turn() == 3 && v085r_g[i].turns == 2 && arc_net_turn_waiting() == 3u); }
    /* it comes back, catches up, and has to end turn 3 like everyone else */
    v085r_on(2); CHECK(arc_net_join("mem:7301")); v085r_settle(3);
    v085r_on(2); CHECK(arc_net_turn() == 3 && arc_net_my_turn() && arc_net_turn_waiting() == 7u && v085r_g[2].turns == 2);
    CHECK(v085r_all_same(3));
    for (int i = 0; i < 3; i++) { v085r_on(i); CHECK(arc_net_turn_end()); } v085r_settle(3);
    for (int i = 0; i < 3; i++) { v085r_on(i); CHECK(arc_net_turn() == 4); }
    CHECK(v085r_all_same(3));
    for (int i = 0; i < V085R_M; i++) v085r_kill(i);
    arc__net_test_use(NULL);
}

/* ======================================================================== */
/* Part 3: the online API, the lobby, a WebSocket client against a socket   */
/* ======================================================================== */
static int v085r_lobby_result;
static void v085r_lstep(void) { arc__input_test_step(); arc__ui_tick_begin(); int r = arc_net_lobby(NULL); if (r != ARC_NET_LOBBY_OPEN) v085r_lobby_result = r; arc__ui_tick_end(); }
static bool v085r_click(const char *label) {
    float x, y; if (!arc__ui_find(label, &x, &y)) return false;
    arc__input_mouse(x, y); v085r_lstep();
    arc__input_key(ARC_MOUSE_LEFT, true); v085r_lstep(); arc__input_key(ARC_MOUSE_LEFT, false); v085r_lstep(); v085r_lstep();
    return true;
}
static void v085r_api(void) {
    static uint8_t v085r_ui[1 << 16]; arc_writer uw = { v085r_ui, sizeof v085r_ui, 0 }; arc__ui_state(&uw);   /* (put back at the end) */
    void *m = arc__net_test_machine(); arc__net_test_use(m); v085r_cur = &v085r_g[3]; memset(v085r_cur, 0, sizeof *v085r_cur);
    arc_net_desc d; memset(&d, 0, sizeof d); d.game = "v085api"; d.version = "1"; arc_net_setup(&d);
    CHECK(!strcmp(arc_net_room(), "") && !arc_net_relay_available());
    CHECK(!arc_net_host_online() && strstr(arc_net_message(), "no relay") && arc_net_status() == ARC_NET_OFFLINE);
    CHECK(!arc_net_join_online("K1") && strstr(arc_net_message(), "not a room code"));
    d.relay = "wss://only.example.com"; arc_net_setup(&d);
    CHECK(!arc_net_relay_available() && !arc_net_host_online() && strstr(arc_net_message(), "ws://"));        /* desktop: no TLS */
    d.relay = "wss://relay.example.com ws://127.0.0.1:9"; arc_net_setup(&d);
    CHECK(arc_net_relay_available() && !strcmp(arc_net_desc_get()->relay, "wss://relay.example.com ws://127.0.0.1:9"));
    d.mode = ARC_NET_ROLLBACK; arc_net_setup(&d);
    { bool on = arc_net_host_online(); CHECK(!strstr(arc_net_message(), "LAN only")); if (on) arc_net_leave(); }   /* 0.10.4: rollback goes online too */
    d.mode = 0; arc_net_setup(&d);
    /* the lobby with a relay: Host online, the room code typed (keyboard) or on a letter grid (gamepad, touch), Join by
       code once the code is whole */
    arc_net_set_name("Lob");
    v085r_lstep(); v085r_lstep();
    float x, y;
    CHECK(arc__ui_find("Host online", &x, &y) && arc__ui_find("Room code", &x, &y) && arc__ui_find("Join by code", &x, &y) && !arc__ui_find("Del", &x, &y));
    CHECK(arc__ui_find("Host a game", &x, &y));                                                                 /* LAN stays */
    char before[256]; snprintf(before, sizeof before, "%s", arc_net_message());
    CHECK(v085r_click("Join by code") && arc_net_status() == ARC_NET_OFFLINE && !strcmp(arc_net_message(), before));   /* disabled: no code */
    CHECK(v085r_click("Room code"));
    for (const char *t = "k7-q"; *t; t++) { arc__input_char((uint32_t)*t); v085r_lstep(); }
    CHECK(v085r_click("Join by code") && !strcmp(arc_net_message(), before));                                 /* "k7-q": not whole yet */
    CHECK(v085r_click("Room code")); arc__input_char('f'); v085r_lstep(); arc__input_key(ARC_KEY_ENTER, true); v085r_lstep(); arc__input_key(ARC_KEY_ENTER, false); v085r_lstep();
    CHECK(strstr(arc_net_message(), "cannot connect to the relay") != NULL);                                   /* ENTER joined (nothing listens: it tried) */
    /* a gamepad: the grid appears; the focus keys walk to a letter, A types it, Del takes one back */
    arc__input_pad(true); arc_ui_set_nav(true); v085r_lstep(); v085r_lstep();
    CHECK(arc__ui_find("2", &x, &y) && arc__ui_find("Z", &x, &y) && arc__ui_find("Del", &x, &y));
    CHECK(v085r_click("Del") && v085r_click("Del"));                                                            /* "K7QF" -> "K7" */
    for (int k = 0; k < 12 && strcmp(arc_ui_focused(), "2"); k++) { arc__input_key(ARC_PAD_DOWN, true); v085r_lstep(); arc__input_key(ARC_PAD_DOWN, false); v085r_lstep(); }
    CHECK(!strcmp(arc_ui_focused(), "2"));
    arc__input_key(ARC_PAD_RIGHT, true); v085r_lstep(); arc__input_key(ARC_PAD_RIGHT, false); v085r_lstep(); CHECK(!strcmp(arc_ui_focused(), "3"));
    arc__input_key(ARC_PAD_A, true); v085r_lstep(); arc__input_key(ARC_PAD_A, false); v085r_lstep();
    CHECK(v085r_click("M"));
    arc_net_leave(); v085r_lstep();
    CHECK(v085r_click("Join by code") && strstr(arc_net_message(), "cannot connect to the relay"));
    arc_ui_set_nav(false); arc__input_pad(false);
    arc_net_leave(); d.relay = NULL; arc_net_setup(&d); v085r_lstep(); v085r_lstep();
    CHECK(!arc__ui_find("Host online", &x, &y) && arc__ui_find("Host a game", &x, &y));                       /* no relay: as 0.8 */
    arc__net_test_free(m); arc__net_test_use(NULL);
    arc__input_test_step(); arc__ui_tick_begin(); arc__ui_tick_end();
    arc_reader ur = { v085r_ui, uw.len, 0, false }; arc__ui_state_read(&ur, true);
    arc__input_test_step();
}
#if !defined(_WIN32)
/* a pretend relay: this test listens, the ARC client connects with ws://, and the bytes are checked both ways */
static int v085r_fd = -1, v085r_lfd = -1; static net_buf v085r_in;
static void v085r_pump(int frames) {
    for (int k = 0; k < frames; k++) {
        v085r_t += 1.0 / 60.0; arc__net_test_time(v085r_t); arc__net_frame(); arc__net_tick_begin();
        if (v085r_fd < 0 && v085r_lfd >= 0) { int s = accept(v085r_lfd, NULL, NULL); if (s >= 0) { int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one); fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); v085r_fd = s; } }
        if (v085r_fd >= 0) for (;;) { uint8_t b[4096]; ssize_t n = recv(v085r_fd, b, sizeof b, 0); if (n <= 0) break; nb_put(&v085r_in, b, (size_t)n); }
        usleep(1000);
    }
}
static void v085r_send_raw(const void *p, size_t n) { if (v085r_fd >= 0) { ssize_t k = send(v085r_fd, p, n, 0); (void)k; } }
static void v085r_send_msg(void) { m_finish(); uint8_t h[14]; size_t k = ncore_ws_header(h, WS_BINARY, g_msg.n, NULL); v085r_send_raw(h, k); v085r_send_raw(g_msg.b, g_msg.n); }
/* the next masked frame from the client of one opcode (others skipped); its unmasked payload into out */
static bool v085r_frame(int opcode, net_buf *out) {
    for (int tries = 0; tries < 400; tries++) {
        ncore_wsf f; long k = ncore_ws_parse(v085r_in.b, v085r_in.n, 1 << 20, &f);
        if (k < 0) return false;
        if (k > 0) {
            bool ok = f.masked;
            uint8_t *pl = v085r_in.b + f.hdr; if (f.masked) ncore_ws_mask(pl, (size_t)f.len, f.mask);
            bool want = f.opcode == opcode; if (want && out) { out->n = 0; nb_put(out, pl, (size_t)f.len); }
            nb_drop_front(&v085r_in, (size_t)k);
            if (!ok) return false;
            if (want) return true;
            continue;
        }
        v085r_pump(1);
    }
    return false;
}
static void v085r_ws_client(void) {
    v085r_lfd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(0x7F000001u); sa.sin_port = 0;
    socklen_t sl = sizeof sa;
    if (v085r_lfd < 0 || bind(v085r_lfd, (struct sockaddr *)&sa, sizeof sa) || listen(v085r_lfd, 2) || getsockname(v085r_lfd, (struct sockaddr *)&sa, &sl)) { CHECK(!"cannot listen for the WebSocket test"); return; }
    fcntl(v085r_lfd, F_SETFL, fcntl(v085r_lfd, F_GETFL, 0) | O_NONBLOCK);
    int port = ntohs(sa.sin_port);
    void *m = arc__net_test_machine(); arc__net_test_use(m); arc__net_test_sockets_on(); v085r_cur = &v085r_g[3]; memset(v085r_cur, 0, sizeof *v085r_cur);
    arc_net_desc d; memset(&d, 0, sizeof d); d.game = "wst"; d.version = "2"; d.max_players = 3;
    char relay[64]; snprintf(relay, sizeof relay, "wss://nowhere.example.com ws://127.0.0.1:%d/arc", port); d.relay = relay;
    arc_net_setup(&d); arc_net_set_name("Wes");
    CHECK(arc_net_host_online() && arc_net_status() == ARC_NET_JOINING && !arc_net_is_host());
    /* the upgrade request */
    const char *end = NULL;
    for (int k = 0; k < 300 && !end; k++) { v085r_pump(1); if (v085r_in.n) { uint8_t z = 0; nb_put(&v085r_in, &z, 1); v085r_in.n--; end = strstr((const char *)v085r_in.b, "\r\n\r\n"); } }
    CHECK(end != NULL);
    if (!end) goto done;
    {
        const char *head = (const char *)v085r_in.b; char key[64] = "", v[64] = "";
        CHECK(!strncmp(head, "GET /arc HTTP/1.1\r\n", 19) && ncore_http_header(head, "Sec-WebSocket-Key", key, sizeof key) && strlen(key) == 24);
        CHECK(ncore_http_header(head, "Upgrade", v, sizeof v) && ncore_http_token(v, "websocket") && ncore_http_header(head, "Sec-WebSocket-Version", v, sizeof v) && !strcmp(v, "13"));
        CHECK(ncore_http_header(head, "Host", v, sizeof v) && !strcmp(v, arc_fmt("127.0.0.1:%d", port)));
        char acc[32]; ncore_ws_accept(key, acc);
        nb_drop_front(&v085r_in, (size_t)(end - head) + 4);
        char resp[256]; int n = snprintf(resp, sizeof resp, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
        v085r_send_raw(resp, (size_t)n);
        /* HELLO in a masked binary frame: create a room */
        net_buf pl; memset(&pl, 0, sizeof pl);
        CHECK(v085r_frame(WS_BINARY, &pl));
        net_rd rd; int ty; size_t tot; ncore_hello h;
        CHECK(ncore_next(&pl, &rd, &ty, &tot) == 1 && ty == M_HELLO && ncore_hello_read(&rd, &h) && h.op == NCORE_OP_CREATE && !strcmp(h.game, "wst") && !strcmp(h.ver, "2") && !strcmp(h.name, "Wes") && h.max_players == 3);
        /* the relay's answer: the room TEST, the join, caught up */
        m_begin(M_WELCOME); m_u16(NET_PROTO); m_u8(0); m_u8(3); m_u64(4242); m_str("Wes's game"); m_str("TEST"); v085r_send_msg();
        m_begin(M_SEQ); m_u32(1); m_u8(0); m_u8(0); m_u8(NK_JOIN); m_str("Wes"); v085r_send_msg();
        m_begin(M_CAUGHT_UP); m_u32(1); v085r_send_msg();
        for (int k = 0; k < 100 && arc_net_status() != ARC_NET_HOSTING; k++) v085r_pump(1);
        v085r_pump(2);
        CHECK(arc_net_status() == ARC_NET_HOSTING && arc_net_is_host() && !strcmp(arc_net_room(), "TEST") && arc_net_players(NULL, 0) == 1 && arc_net_local_player() == 0);
        arc_net_ev ev; bool room_ev = false; while (arc_net_event(&ev)) if (ev.type == ARC_NET_EV_ROOM && !strcmp(ev.text, "TEST")) room_ev = true;
        CHECK(room_ev);
        /* a ping frame: answered with a masked pong of the same payload */
        static const uint8_t v085r_ping[] = { 0x89, 0x02, 'h', 'i' };
        v085r_send_raw(v085r_ping, sizeof v085r_ping);
        CHECK(v085r_frame(WS_PONG, &pl) && pl.n == 2 && !memcmp(pl.b, "hi", 2));
        /* a message split over two frames (FIN clear, then a continuation): the ready flag arrives whole */
        m_begin(M_SEQ); m_u32(2); m_u8(0); m_u8(1); m_u8(NK_READY); m_str("1"); m_finish();
        uint8_t hd[14]; size_t half = g_msg.n / 2, k1 = ncore_ws_header(hd, WS_BINARY, half, NULL); hd[0] &= 0x7F;   /* not the last fragment */
        v085r_send_raw(hd, k1); v085r_send_raw(g_msg.b, half);
        size_t k2 = ncore_ws_header(hd, WS_CONT, g_msg.n - half, NULL); v085r_send_raw(hd, k2); v085r_send_raw(g_msg.b + half, g_msg.n - half);
        for (int k = 0; k < 200 && arc_net_seq() < 2; k++) v085r_pump(1);
        arc_net_player ps[2]; CHECK(arc_net_players(ps, 2) == 1 && ps[0].ready && arc_net_seq() == 2);
        /* the relay going down: BYE with "come back" set, so the game reconnects by itself */
        m_begin(M_BYE); m_str("the relay is shutting down"); m_u8(1); v085r_send_msg();
        for (int k = 0; k < 200 && arc_net_status() != ARC_NET_LOST; k++) v085r_pump(1);
        CHECK(arc_net_status() == ARC_NET_LOST && arc__net_test_auto_retry() && strstr(arc_net_message(), "shutting down"));
        nb_free(&pl);
    }
done:
    arc_net_leave();
    arc__net_test_free(m); arc__net_test_use(NULL);
    if (v085r_fd >= 0) close(v085r_fd);
    close(v085r_lfd); v085r_fd = v085r_lfd = -1; nb_free(&v085r_in);
}
#endif

static void v085r_init(void) {
    v085r_core();
    v085r_view_simul();
    arc_app app; memset(&app, 0, sizeof app);
    app.command = v085r_command; app.net_event = v085r_event; app.net_state = v085r_state;
    v085r_cur = &v085r_g[0];
    arc__test_sandbox(&app);
    v085r_session();
    arc__test_sandbox(NULL);
    v085r_api();
#if !defined(_WIN32)
    v085r_ws_client();
#endif
}
static void v085r_check(void) {
    CHECK(arc_net_status() == ARC_NET_OFFLINE && !strcmp(arc_net_room(), ""));   /* the tests above left the real machine alone */
}
