/* ARC 0.8.1: real-time rollback matches (GGPO-style) on top of the 0.8 sessions (arc_net.c).
   The match is set up over the TCP session (host, join, ready, start with a seed). From the START entry on, every machine
   simulates every tick from every player's input:
     - Each new tick the local input is packed (arc_app.net_input or ARC's default) for tick + input_delay and sent over
       UDP; every datagram repeats all the inputs the receiver has not acknowledged (and acknowledges what it got).
       Star topology: clients talk to the host, which relays the other players' inputs (2 players: direct).
     - A remote input that has not arrived is predicted (the player's last known input). When it arrives and differs from
       what a simulated tick used, the snapshot from before that tick is loaded and the ticks up to now run again silently.
       Snapshots: one per tick in a ring of max_rollback + 2 (ARC's whole state: game save_state, arc_rand, particles...).
     - A machine runs at most max_rollback ticks past the inputs it has (then it waits); time sync makes the one ahead
       skip a tick now and then. Every 10 confirmed ticks the state hashes are exchanged (desync detection).
     - Side effects: sounds are deduplicated per (tick, sound, n-th play in that tick); arc_event lines wait until their
       tick is confirmed; music calls are ignored while re-simulating.
   Datagram (little-endian): "ARB" proto, u32 session, u8 from, u8 type, u32 frame, u32 t_send, u32 t_echo, u16 hold,
   i16 advantage*100, u8 flags, u32 desync tick, u8 np, np x u32 acks, u8 ndrops {u8 p, u32 tick}, u8 nhash {u32 tick,
   u64 hash}, u8 nblocks {u8 p, u32 first, u8 count, count x {u8 size, bytes}}.
   All the rings are allocated when the match starts; nothing is allocated per tick (snapshot buffers grow only when the
   state does). The self-test runs several machines in one process with hooks instead of ARC's engine state. */

#define RB_MAXP ARC_NET_ROLLBACK_PLAYERS
#define RB_RING 128                    /* input ticks kept per player (power of 2) */
#define RB_SLOTS (ARC_NET_MAX_ROLLBACK + 2)
#define RB_HASH_EVERY 10
#define RB_HRING 64
#define RB_BRING 32
#define RB_SNDS 512
#define RB_EVS 512
#define RB_EVTEXT 65536
#define RB_PKT 1400
#define RB_BUDGET 1200
#define RB_SIMQ 512
#define RB_TIMEOUT 3.0
#define RB_WAIT_EVENT 0.5
#define RB_PROTO 1
#define RB_NONE INT64_MAX
#define RB_DEFAULT_INPUT 39            /* 32 bytes of key bits, 6 axes, flags */

enum { RBK_LIVE = 1, RBK_SYNCTEST, RBK_REPLAY, RBK_TEST };
enum { U_INPUT = 1, U_BYE = 2 };

typedef struct { int64_t frame; uint8_t size, confirmed, used, pad; uint8_t d[ARC_NET_INPUT_MAX]; } rb_in;
typedef struct { int64_t frame; uint8_t *data; size_t size, cap; } rb_slot;
typedef struct { int64_t frame; uint64_t mine; bool have; uint8_t rmask, bad; uint64_t remote[RB_MAXP]; } rb_hrec;
typedef struct { int64_t frame; uint8_t *d; size_t n, cap; } rb_bytes;
typedef struct { int64_t frame; uint64_t tick; uint32_t off, len; } rb_ev;
typedef struct { int64_t frame; uint32_t snd; uint16_t nth; uint8_t seen; arc_voice v; } rb_snd;
typedef struct { double at; uint32_t ip; int port; uint16_t len; uint8_t d[RB_PKT]; } rb_dg;
typedef struct {
    bool on, known; uint32_t ip; int port;
    double last_recv, last_send, opened;
    uint32_t echo; double echo_at;
    int64_t has[RB_MAXP];                /* what the peer has of each player's input (its acknowledgements) */
    int64_t frame; double frame_at; bool frame_known;
    float radv, adv; bool adv_init;      /* its advantage over me (as it says), mine over it */
    float rtt; bool rtt_known;
} rb_peer;
/* the self-test's machines: their own little game instead of ARC's engine state */
typedef struct {
    size_t (*save)(void *buf, size_t cap); bool (*load)(const void *buf, size_t n);
    void (*tick)(void); size_t (*state)(void *buf, size_t cap); size_t (*input)(void *buf, size_t cap); void (*start)(uint64_t seed);
} rb_hooks;

typedef struct net_rb {
    int kind; bool armed;
    uint64_t start_tick, seed;
    int local; uint8_t mask; int np;                       /* players in the match: a bit each */
    int delay, window, synctest;
    int64_t cur;                                           /* the next tick to simulate (live) */
    int64_t sim_frame; bool in_sim, resim;
    int64_t local_next, rollback_to, final_, rec_upto, flushed;
    int64_t have[RB_MAXP], drop_at[RB_MAXP];
    rb_in in[RB_MAXP][RB_RING];
    rb_slot slot[RB_SLOTS];
    rb_hrec hr[RB_HRING]; rb_bytes br[RB_BRING];
    int64_t last_hash_tick; uint64_t last_hash;
    rb_peer peer[RB_MAXP];                                 /* host: by player; client: [0] = the host */
    net_sock sock; int mem_ep; bool sock_ok;
    rb_dg *simq; int simn;
    uint8_t pkt[RB_PKT];
    int skip_debt, skip_gap; int64_t next_sync;
    double stall_since, stall_mark; int waiting; bool wait_told; int n_waiting, n_resumed, n_dropped;
    int64_t stop_at;
    double freeze_until; bool freeze_done;
    bool script_on[RB_MAXP]; uint8_t script[RB_MAXP][ARC_NET_INPUT_MAX]; uint8_t script_n[RB_MAXP];
    rb_ev ev[RB_EVS]; int ev_n; char *evtext; uint32_t ev_used;
    rb_snd snd[RB_SNDS]; int snd_n, snd_last;
    arc_net_rb_stats st; double depth_sum;
    int64_t desync_tick; bool desync_reported, synctest_failed;
    bool sent_tick; double last_any_send;
    const rb_hooks *hooks;
    uint8_t *sbuf; size_t scap;
    bool started_printed;
    /* 0.10.4 online (a relay room): the hub (the lowest player in the match) plays the LAN host's part; datagrams go
       through the relay's UDP port, or straight to a peer once hole punching has worked (rt) */
    bool online; int hub; uint32_t relay_ip; int relay_port; double next_reg; bool peers_heard;
    struct { bool pub_known, direct; uint32_t pip; int pport; double last_direct, last_punch; int64_t sent_direct, sent_relayed; } rt[RB_MAXP];
} net_rb;

/* ---- the replay of a recorded match: every player's confirmed input by tick ---- */
static struct { int64_t frames; uint8_t *sz, *d; int64_t drop_at[RB_MAXP]; bool any; } RBR;
static uint8_t g_rb_zero[ARC_NET_INPUT_MAX];

static bool net_rollback_mode(void) {
    net_machine *N = NET;
    return N->rb != NULL || N->desc.mode == ARC_NET_ROLLBACK || N->cfg_mode == ARC_NET_ROLLBACK;
}
static bool rb_in_match(const net_rb *rb, int p) { return p >= 0 && p < RB_MAXP && (rb->mask >> p) & 1; }
static bool rb_dropped_at(const net_rb *rb, int p, int64_t f) { return rb->drop_at[p] >= 0 && f >= rb->drop_at[p]; }
static int64_t rb_eff_have(const net_rb *rb, int p) {
    if (rb->drop_at[p] >= 0 && rb->have[p] >= rb->drop_at[p]) return RB_NONE;
    return rb->have[p];
}
static double rb_hz(void) { return G.tick_hz > 0 ? (double)G.tick_hz : 60.0; }
/* the machine every other one talks to (LAN: the host, player 0; online: the hub) */
static bool rb_is_hub(const net_rb *rb) { return rb->online ? rb->local == rb->hub : NET->mode == MODE_HOST; }
static int rb_hub_player(const net_rb *rb) { return rb->online ? rb->hub : 0; }
static bool g_rb_relay_only;   /* 0.10.4 --net-relay-only / `net direct off`: no hole punching (tests the relayed path) */

/* ======================================================================== */
/* Datagrams: sockets, in-process mailboxes (self-test), --net-sim          */
/* ======================================================================== */
static struct { bool used; int port; rb_dg *q; int n; } g_memudp[16];
static int memudp_open(int port) {
    for (int i = 0; i < 16; i++) if (g_memudp[i].used && g_memudp[i].port == port) return -1;
    for (int i = 0; i < 16; i++) if (!g_memudp[i].used) {
        g_memudp[i].used = true; g_memudp[i].port = port; g_memudp[i].n = 0;
        if (!g_memudp[i].q) g_memudp[i].q = (rb_dg *)arc__xalloc(sizeof(rb_dg) * 256);
        return i;
    }
    return -1;
}
static void memudp_close(int ep) { if (ep >= 0 && ep < 16) { g_memudp[ep].used = false; g_memudp[ep].n = 0; } }
static void rb_wire_send(net_rb *rb, uint32_t ip, int port, const uint8_t *d, size_t n) {
    net_machine *N = NET;
    if (N->memory) {
        for (int i = 0; i < 16; i++) if (g_memudp[i].used && g_memudp[i].port == port) {
            if (g_memudp[i].n >= 256) return;                /* a full mailbox drops it, as a socket would */
            rb_dg *g = &g_memudp[i].q[g_memudp[i].n++];
            g->ip = 0x7F000001u; g->port = rb->mem_ep >= 0 ? g_memudp[rb->mem_ep].port : 0; g->len = (uint16_t)n; memcpy(g->d, d, n);
            return;
        }
        return;
    }
#if NET_SOCKETS
    if (rb->sock_ok) ns_udp_send(rb->sock, ip, port, d, n);
#else
    (void)ip; (void)port; (void)d; (void)n;
#endif
}
static int rb_wire_recv(net_rb *rb, uint8_t *d, size_t cap, uint32_t *ip, int *port) {
    net_machine *N = NET;
    if (N->memory) {
        if (rb->mem_ep < 0) return 0;
        if (!g_memudp[rb->mem_ep].n) return 0;
        rb_dg *g = &g_memudp[rb->mem_ep].q[0]; int n = g->len < cap ? g->len : (int)cap;
        memcpy(d, g->d, (size_t)n); *ip = g->ip; *port = g->port;
        memmove(g_memudp[rb->mem_ep].q, g_memudp[rb->mem_ep].q + 1, sizeof(rb_dg) * (size_t)(--g_memudp[rb->mem_ep].n));
        return n;
    }
#if NET_SOCKETS
    if (!rb->sock_ok) return 0;
    for (int k = 0; k < 16; k++) {                         /* Windows reports an ICMP "port unreachable" as a failed read: skip it */
        struct sockaddr_in sa; ns_len sl = (ns_len)sizeof sa;
#if defined(_WIN32)
        int r = NSW.recvfrom_(rb->sock, (char *)d, (int)cap, 0, (struct sockaddr *)&sa, &sl);
        if (r < 0 && ns_err() == 10054) continue;
#else
        ssize_t r = recvfrom(rb->sock, d, cap, 0, (struct sockaddr *)&sa, &sl);
        if (r < 0 && (errno == ECONNREFUSED || errno == ECONNRESET || errno == EINTR)) continue;
#endif
        if (r <= 0) return 0;
        ns_from_addr(&sa, ip, port);
        return (int)r;
    }
#else
    (void)d; (void)cap; (void)ip; (void)port;
#endif
    return 0;
}
/* --net-sim on datagrams: lag + jitter (which reorders), loss, duplicates */
static void rb_send_dgram(net_rb *rb, uint32_t ip, int port, const uint8_t *d, size_t n) {
    net_machine *N = NET;
    if (N->sim_dropped) return;                            /* the cable is out */
    if (!N->sim.on || (N->sim.lag <= 0 && N->sim.jitter <= 0 && N->sim.loss <= 0 && N->sim.dup <= 0)) { rb_wire_send(rb, ip, port, d, n); rb->st.packets_sent++; return; }
    int copies = 1;
    if (N->sim.dup > 0 && (double)arc_rng_float(&N->sim.rng) < N->sim.dup) copies = 2;
    for (int c = 0; c < copies; c++) {
        if (N->sim.loss > 0 && (double)arc_rng_float(&N->sim.rng) < N->sim.loss) continue;
        if (rb->simn >= RB_SIMQ) continue;
        rb_dg *g = &rb->simq[rb->simn++];
        g->at = net_now() + N->sim.lag * 0.001 + N->sim.jitter * 0.001 * (double)arc_rng_float(&N->sim.rng);
        g->ip = ip; g->port = port; g->len = (uint16_t)n; memcpy(g->d, d, n);
    }
    rb->st.packets_sent++;
}
static void rb_simq_flush(net_rb *rb) {
    double now = net_now(); int k = 0;
    for (int i = 0; i < rb->simn; i++) {
        rb_dg *g = &rb->simq[i];
        if (g->at <= now) rb_wire_send(rb, g->ip, g->port, g->d, g->len);
        else { if (k != i) rb->simq[k] = *g; k++; }
    }
    rb->simn = k;
}

/* ---- 0.10.4 online: datagrams through the relay (ARU), hole punching (see arc_net_core.h) ---- */
static size_t rb_aru_head(uint8_t *b, int type) {
    uint64_t s = (uint64_t)NET->session;
    b[0] = 'A'; b[1] = 'R'; b[2] = 'U'; b[3] = NCORE_UDP_VER; b[4] = (uint8_t)type;
    for (int i = 0; i < 8; i++) b[5 + i] = (uint8_t)(s >> (8 * i));
    return NCORE_UDP_HDR;
}
static bool rb_aru_mine(const uint8_t *d) { uint64_t s = 0; for (int i = 0; i < 8; i++) s |= (uint64_t)d[5 + i] << (8 * i); return s == (uint64_t)NET->session; }
static void rb_route(net_rb *rb, int slot, const uint8_t *d, size_t n) {
    if (slot < 0 || slot >= RB_MAXP || n + NCORE_UDP_HDR + 2 > NCORE_UDP_MAX) return;
    double now = net_now();
    if (rb->rt[slot].direct && now - rb->rt[slot].last_direct > 1.5) {
        rb->rt[slot].direct = false;
        printf("ARC log net path to %d: through the relay (the direct path went quiet)\n", slot); fflush(stdout);
    }
    if (rb->rt[slot].direct && !g_rb_relay_only) { rb_send_dgram(rb, rb->rt[slot].pip, rb->rt[slot].pport, d, n); rb->rt[slot].sent_direct++; return; }
    uint8_t b[NCORE_UDP_MAX]; size_t k = rb_aru_head(b, NCORE_UDP_FWD);
    b[k++] = (uint8_t)rb->local; b[k++] = (uint8_t)slot; memcpy(b + k, d, n);
    rb_send_dgram(rb, rb->relay_ip, rb->relay_port, b, k + n); rb->rt[slot].sent_relayed++;
}
static bool rb_talks_to(const net_rb *rb, int p) {   /* star: the hub talks to everyone, the others to the hub */
    if (!rb_in_match(rb, p) || p == rb->local || rb->drop_at[p] >= 0) return false;
    return rb_is_hub(rb) || p == rb->hub;
}
static void rb_online_upkeep(net_rb *rb) {
    if (!rb->online || !rb->sock_ok || NET->sim_dropped) return;
    double now = net_now(); uint8_t b[64];
    if (now >= rb->next_reg) {                            /* "this address is my slot" (keeps the NAT mapping open) */
        size_t k = rb_aru_head(b, NCORE_UDP_REG); uint64_t t = NET->token;
        b[k++] = (uint8_t)rb->local; for (int i = 0; i < 8; i++) b[k++] = (uint8_t)(t >> (8 * i));
        rb_wire_send(rb, rb->relay_ip, rb->relay_port, b, k);
        rb->next_reg = now + (rb->peers_heard ? 1.0 : 0.25);
    }
    if (g_rb_relay_only) return;
    for (int p = 0; p < RB_MAXP; p++) {
        if (!rb_talks_to(rb, p) || !rb->rt[p].pub_known) continue;
        if (now - rb->rt[p].last_punch < (rb->rt[p].direct ? 0.5 : 0.25)) continue;
        size_t k = rb_aru_head(b, NCORE_UDP_PUNCH); b[k++] = (uint8_t)rb->local; b[k++] = (uint8_t)p;
        rb_send_dgram(rb, rb->rt[p].pip, rb->rt[p].pport, b, k); rb->rt[p].last_punch = now;
    }
}
static void rb_receive(net_rb *rb, const uint8_t *d, int n, uint32_t ip, int port);
static void rb_aru(net_rb *rb, const uint8_t *d, int n, uint32_t ip, int port) {
    if (!rb->online || n < NCORE_UDP_HDR + 1 || d[3] != NCORE_UDP_VER || !rb_aru_mine(d)) return;
    int type = d[4]; const uint8_t *q = d + NCORE_UDP_HDR; int qn = n - NCORE_UDP_HDR; double now = net_now();
    if (type == NCORE_UDP_FROM && ip == rb->relay_ip && port == rb->relay_port) { rb_receive(rb, q + 1, qn - 1, ip, port); return; }
    if (type == NCORE_UDP_PEERS && ip == rb->relay_ip && port == rb->relay_port) {
        int cnt = q[0]; rb->peers_heard = true;
        for (int i = 0; i < cnt && 1 + i * 7 + 7 <= qn; i++) {
            const uint8_t *e = q + 1 + i * 7; int p = e[0];
            uint32_t pip = (uint32_t)e[1] | (uint32_t)e[2] << 8 | (uint32_t)e[3] << 16 | (uint32_t)e[4] << 24; int pport = e[5] | e[6] << 8;
            if (p >= RB_MAXP) continue;
            if (!rb->rt[p].pub_known || rb->rt[p].pip != pip || rb->rt[p].pport != pport) { rb->rt[p].pip = pip; rb->rt[p].pport = pport; rb->rt[p].direct = false; }
            rb->rt[p].pub_known = true;
        }
        return;
    }
    if ((type == NCORE_UDP_PUNCH || type == NCORE_UDP_PUNCH_ACK) && qn >= 2 && !g_rb_relay_only) {
        int from = q[0], to = q[1];
        if (to != rb->local || !rb_talks_to(rb, from)) return;
        if (type == NCORE_UDP_PUNCH) {                    /* they reach me: answer to where it came from */
            uint8_t b[32]; size_t k = rb_aru_head(b, NCORE_UDP_PUNCH_ACK); b[k++] = (uint8_t)rb->local; b[k++] = (uint8_t)from;
            rb_send_dgram(rb, ip, port, b, k);
            return;
        }
        if (!rb->rt[from].direct) { printf("ARC log net path to %d: direct (hole punching worked)\n", from); fflush(stdout); }
        rb->rt[from].direct = true; rb->rt[from].pip = ip; rb->rt[from].pport = port; rb->rt[from].pub_known = true; rb->rt[from].last_direct = now;
    }
}

/* ======================================================================== */
/* Inputs                                                                   */
/* ======================================================================== */
static rb_in *rb_slot_in(net_rb *rb, int p, int64_t f) { return &rb->in[p][f & (RB_RING - 1)]; }
static void rb_store(net_rb *rb, int p, int64_t f, const uint8_t *d, int n) {
    rb_in *e = rb_slot_in(rb, p, f);
    if (e->frame != f) e->used = 0;
    e->frame = f; e->size = (uint8_t)n; memset(e->d, 0, sizeof e->d); if (n > 0) memcpy(e->d, d, (size_t)n); e->confirmed = 1;
}
static void rb_advance_have(net_rb *rb, int p) {
    for (;;) { rb_in *e = rb_slot_in(rb, p, rb->have[p]); if (e->frame != rb->have[p] || !e->confirmed) break; rb->have[p]++; }
}
static int64_t rb_low(const net_rb *rb) {             /* the oldest input still needed (recording, prediction, peers' acks) */
    int64_t lo = rb->final_ - 1; if (rb->rec_upto < lo) lo = rb->rec_upto;
    for (int q = 0; q < RB_MAXP; q++) if (rb->peer[q].on) for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p) && rb->peer[q].has[p] < lo) lo = rb->peer[q].has[p];
    return lo < 0 ? 0 : lo;
}
static void rb_arrive(net_rb *rb, int p, int64_t f, const uint8_t *d, int n) {
    if (!rb_in_match(rb, p) || p == rb->local || f < 0 || n > ARC_NET_INPUT_MAX) return;
    if (rb_dropped_at(rb, p, f) || f < rb->have[p]) return;
    if (f >= rb_low(rb) + RB_RING - 2) return;             /* too far ahead to keep */
    rb_in *e = rb_slot_in(rb, p, f);
    if (e->frame == f && e->confirmed) return;
    if (e->frame == f && e->used && f < rb->cur && (e->size != n || (n > 0 && memcmp(e->d, d, (size_t)n)))) {
        if (f < rb->rollback_to) rb->rollback_to = f;
        rb->st.mispredicted++;
    }
    bool keep_used = e->frame == f && e->used;
    rb_store(rb, p, f, d, n); e->used = keep_used ? 1 : 0;
    rb_advance_have(rb, p);
}
/* the input a tick uses: confirmed, or the last known one repeated (marked: compared when the real one arrives) */
static const rb_in *rb_input_at(net_rb *rb, int p, int64_t f, bool mark) {
    static rb_in zero;
    if (!rb_in_match(rb, p) || f < 0) { memset(&zero, 0, sizeof zero); zero.frame = f; zero.confirmed = 1; return &zero; }
    if (rb->kind == RBK_REPLAY) {
        static rb_in rin; memset(&rin, 0, sizeof rin); rin.frame = f; rin.confirmed = 1;
        if (RBR.drop_at[p] >= 0 && f >= RBR.drop_at[p]) return &rin;
        for (int64_t k = f < RBR.frames ? f : RBR.frames - 1; k >= 0; k--) {
            uint8_t sz = RBR.sz[k * RB_MAXP + p];
            if (sz == 0xFF) continue;
            rin.size = sz; memcpy(rin.d, RBR.d + (size_t)(k * RB_MAXP + p) * ARC_NET_INPUT_MAX, ARC_NET_INPUT_MAX); rin.confirmed = k == f;
            return &rin;
        }
        rin.confirmed = 0; return &rin;
    }
    if (rb_dropped_at(rb, p, f)) { memset(&zero, 0, sizeof zero); zero.frame = f; zero.confirmed = 1; return &zero; }
    rb_in *e = rb_slot_in(rb, p, f);
    if (e->frame == f && e->confirmed) { if (mark) e->used = 1; return e; }
    if (!mark) {                                            /* a look (draw, a dump): the guess, without remembering it */
        static rb_in look; memset(&look, 0, sizeof look); look.frame = f;
        if (rb->have[p] > 0) { const rb_in *src = rb_slot_in(rb, p, rb->have[p] - 1); look.size = src->size; memcpy(look.d, src->d, sizeof look.d); }
        return &look;
    }
    bool was_used = e->frame == f && e->used;
    if (e->frame != f) { memset(e, 0, sizeof *e); e->frame = f; }
    if (rb->have[p] > 0) { const rb_in *src = rb_slot_in(rb, p, rb->have[p] - 1); e->size = src->size; memcpy(e->d, src->d, sizeof e->d); }
    else { e->size = 0; memset(e->d, 0, sizeof e->d); }
    e->confirmed = 0; e->used = 1;
    if (!was_used && !rb->resim) rb->st.predicted++;
    return e;
}
static int rb_default_pack(uint8_t *b) {
    memcpy(b, g_cur.keys, 32);
    for (int a = 0; a < ARC_AXIS_COUNT; a++) { float v = arc_clampf(g_cur.axes[a], -1, 1) * 127.0f; b[32 + a] = (uint8_t)(int8_t)(v < 0 ? v - 0.5f : v + 0.5f); }
    b[38] = g_cur.flags;
    return RB_DEFAULT_INPUT;
}
static int rb_pack_local(net_rb *rb, uint8_t *b) {
    memset(b, 0, ARC_NET_INPUT_MAX);
    int p = rb->local;
    if (rb->script_on[p]) { memcpy(b, rb->script[p], rb->script_n[p]); return rb->script_n[p]; }
    size_t n;
    if (rb->hooks) n = rb->hooks->input ? rb->hooks->input(b, ARC_NET_INPUT_MAX) : 0;
    else if (G.app.net_input) n = G.app.net_input(b, ARC_NET_INPUT_MAX);
    else n = (size_t)rb_default_pack(b);
    if (n > ARC_NET_INPUT_MAX) { n = ARC_NET_INPUT_MAX; }
    return (int)n;
}

/* ======================================================================== */
/* Snapshots, hashes                                                        */
/* ======================================================================== */
static bool rb_capture(net_rb *rb, int64_t f) {
    rb_slot *s = &rb->slot[f % RB_SLOTS];
    s->frame = -1;
    if (rb->hooks) {
        size_t n = rb->hooks->save(s->data, s->cap);
        if (n > s->cap) { size_t nc = n + (n >> 2) + 64; uint8_t *nd = (uint8_t *)realloc(s->data, nc); if (!nd) return false; s->data = nd; s->cap = nc; n = rb->hooks->save(s->data, s->cap); }
        if (n > s->cap) return false;
        s->size = n;
    } else {
        size_t n = rb_eng_capture(&s->data, &s->cap);
        if (!n) return false;
        s->size = n;
    }
    s->frame = f;
    return true;
}
static size_t rb_state_bytes(net_rb *rb) {
    size_t (*fn)(void *, size_t) = rb->hooks ? rb->hooks->state : G.app.net_state ? G.app.net_state : G.app.save_state;
    if (!fn) return 0;
    if (rb->scap < 4096) { rb->scap = 65536; rb->sbuf = (uint8_t *)realloc(rb->sbuf, rb->scap); }
    size_t n = fn(rb->sbuf, rb->scap);
    if (n > rb->scap) { rb->scap = n + (n >> 2); rb->sbuf = (uint8_t *)realloc(rb->sbuf, rb->scap); n = fn(rb->sbuf, rb->scap); if (n > rb->scap) n = 0; }
    return n;
}
static int64_t rb_hidx(const net_rb *rb, int64_t f) { return rb->synctest ? f : f / RB_HASH_EVERY; }
static bool rb_hashed_tick(const net_rb *rb, int64_t f) { return rb->synctest ? true : f % RB_HASH_EVERY == 0; }
static void rb_synctest_fail(net_rb *rb, int64_t f, const rb_bytes *first, const uint8_t *now, size_t n);
/* the state at the start of tick f (now): hashed, the bytes kept for a desync report */
static void rb_hash_start(net_rb *rb, int64_t f) {
    if (!rb_hashed_tick(rb, f)) return;
    size_t n = rb_state_bytes(rb);
    uint64_t h = fnv64(1469598103934665603ull, rb->sbuf, n);
    rb_hrec *r = &rb->hr[rb_hidx(rb, f) % RB_HRING];
    rb_bytes *b = &rb->br[rb_hidx(rb, f) % RB_BRING];
    if (rb->synctest && r->frame == f && r->have && r->mine != h && !rb->synctest_failed) rb_synctest_fail(rb, f, b->frame == f ? b : NULL, rb->sbuf, n);
    if (r->frame != f) { memset(r, 0, sizeof *r); r->frame = f; }
    r->mine = h; r->have = true;
    if (b->cap < n) { size_t nc = n + (n >> 2) + 64; uint8_t *nd = (uint8_t *)realloc(b->d, nc); if (!nd) return; b->d = nd; b->cap = nc; }
    if (n) memcpy(b->d, rb->sbuf, n);
    b->n = n; b->frame = f;
}

/* ======================================================================== */
/* Side effects: sounds once per (tick, sound, n-th), events when confirmed */
/* ======================================================================== */
int arc__rb_sound_gate(uint32_t snd, arc_voice *prev) {
    net_rb *rb = NET->rb;
    if (!rb || !rb->in_sim || rb->kind == RBK_REPLAY) return -1;
    int64_t f = rb->sim_frame; int nth = 0;
    for (int i = 0; i < rb->snd_n; i++) if (rb->snd[i].frame == f && rb->snd[i].snd == snd && rb->snd[i].seen) nth++;
    if (rb->resim)
        for (int i = 0; i < rb->snd_n; i++) {
            rb_snd *r = &rb->snd[i];
            if (r->frame == f && r->snd == snd && r->nth == nth && !r->seen) { r->seen = 1; *prev = r->v; rb->st.sounds_deduped++; return 1; }
        }
    if (rb->snd_n == RB_SNDS) { memmove(rb->snd, rb->snd + 1, sizeof(rb_snd) * (RB_SNDS - 1)); rb->snd_n--; }
    rb_snd *r = &rb->snd[rb->snd_n]; memset(r, 0, sizeof *r);
    r->frame = f; r->snd = snd; r->nth = (uint16_t)nth; r->seen = 1;
    rb->snd_last = rb->snd_n++;
    rb->st.sounds_played++;
    if (rb->resim) rb->st.sounds_late++;
    return 0;
}
void arc__rb_sound_voice(arc_voice v) { net_rb *rb = NET->rb; if (rb && rb->snd_last >= 0 && rb->snd_last < rb->snd_n) rb->snd[rb->snd_last].v = v; }
static void rb_sounds_before_resim(net_rb *rb, int64_t from) { for (int i = 0; i < rb->snd_n; i++) if (rb->snd[i].frame >= from) rb->snd[i].seen = 0; }
static void rb_sounds_after_resim(net_rb *rb, int64_t from, int64_t to) {
    int k = 0;
    for (int i = 0; i < rb->snd_n; i++) {
        rb_snd *r = &rb->snd[i];
        if (r->frame >= from && r->frame < to && !r->seen) {   /* its tick no longer plays it */
            if (r->v.id && !rb->hooks) arc_voice_stop(r->v);
            rb->st.sounds_cancelled++; continue;
        }
        if (k != i) rb->snd[k] = *r;
        k++;
    }
    rb->snd_n = k; rb->snd_last = -1;
}
static void rb_sounds_prune(net_rb *rb) {
    int64_t keep = rb->final_ < rb->cur - RB_SLOTS - 1 ? rb->final_ : rb->cur - RB_SLOTS - 1;
    int k = 0;
    for (int i = 0; i < rb->snd_n; i++) if (rb->snd[i].frame >= keep) { if (k != i) rb->snd[k] = rb->snd[i]; k++; }
    rb->snd_n = k; rb->snd_last = -1;
}
bool arc__rb_event(const char *name, const char *body) {
    net_rb *rb = NET->rb;
    if (!rb || !rb->in_sim || rb->kind == RBK_REPLAY) return false;
    int64_t f = rb->sim_frame;
    if (f < rb->flushed) return true;                      /* logged already (a synctest going over confirmed ticks) */
    size_t ln = strlen(name), lb = strlen(body), need = ln + lb + 2;
    if (rb->ev_n == RB_EVS || rb->ev_used + need > RB_EVTEXT) {   /* full: the oldest goes out now (unconfirmed) */
        if (rb->ev_n) {
            rb_ev *e = &rb->ev[0]; const char *nm = rb->evtext + e->off;
            event_emit(e->tick, nm, nm + strlen(nm) + 1, true);
            uint32_t cut = e->off + e->len;
            memmove(rb->evtext, rb->evtext + cut, rb->ev_used - cut); rb->ev_used -= cut;
            memmove(rb->ev, rb->ev + 1, sizeof(rb_ev) * (size_t)(--rb->ev_n));
            for (int i = 0; i < rb->ev_n; i++) rb->ev[i].off -= cut;
        }
        if (rb->ev_used + need > RB_EVTEXT) { event_emit(G.ticks, name, body, true); return true; }
    }
    rb_ev *e = &rb->ev[rb->ev_n++]; e->frame = f; e->tick = G.ticks; e->off = rb->ev_used; e->len = (uint32_t)need;
    memcpy(rb->evtext + rb->ev_used, name, ln + 1); memcpy(rb->evtext + rb->ev_used + ln + 1, body, lb + 1); rb->ev_used += (uint32_t)need;
    return true;
}
static void rb_events_drop_from(net_rb *rb, int64_t from) {
    for (int i = 0; i < rb->ev_n; i++) if (rb->ev[i].frame >= from) { rb->ev_used = rb->ev[i].off; rb->ev_n = i; return; }
}
static void rb_events_flush(net_rb *rb, int64_t upto) {
    int k = 0;
    while (k < rb->ev_n && rb->ev[k].frame < upto) {
        const char *nm = rb->evtext + rb->ev[k].off;
        event_emit(rb->ev[k].tick, nm, nm + strlen(nm) + 1, true);
        k++;
    }
    if (upto > rb->flushed) rb->flushed = upto;
    if (!k) return;
    uint32_t cut = k < rb->ev_n ? rb->ev[k].off : rb->ev_used;
    memmove(rb->evtext, rb->evtext + cut, rb->ev_used - cut); rb->ev_used -= cut;
    memmove(rb->ev, rb->ev + k, sizeof(rb_ev) * (size_t)(rb->ev_n - k)); rb->ev_n -= k;
    for (int i = 0; i < rb->ev_n; i++) rb->ev[i].off -= cut;
}

/* ======================================================================== */
/* The match: start, ticks, corrections                                     */
/* ======================================================================== */
static void rb_free(net_rb *rb) {
    if (!rb) return;
    for (int i = 0; i < RB_SLOTS; i++) free(rb->slot[i].data);
    for (int i = 0; i < RB_BRING; i++) free(rb->br[i].d);
    free(rb->simq); free(rb->evtext); free(rb->sbuf);
#if NET_SOCKETS
    if (rb->sock_ok && rb->sock != NET_BAD) ns_close(rb->sock);
#endif
    memudp_close(rb->mem_ep);
    free(rb);
}
static net_rb *rb_new(int kind) {
    net_machine *N = NET;
    net_rb *rb = (net_rb *)arc__xalloc(sizeof *rb);
    memset(rb, 0, sizeof *rb);
    rb->kind = kind; rb->sock = NET_BAD; rb->mem_ep = -1; rb->waiting = -1; rb->stop_at = -1; rb->snd_last = -1;
    rb->rollback_to = RB_NONE; rb->desync_tick = -1; rb->last_hash_tick = -1;
    for (int i = 0; i < RB_MAXP; i++) { rb->drop_at[i] = -1; for (int k = 0; k < RB_RING; k++) rb->in[i][k].frame = -1; }
    for (int i = 0; i < RB_SLOTS; i++) rb->slot[i].frame = -1;
    for (int i = 0; i < RB_HRING; i++) rb->hr[i].frame = -1;
    for (int i = 0; i < RB_BRING; i++) rb->br[i].frame = -1;
    rb->evtext = (char *)arc__xalloc(RB_EVTEXT);
    rb->hooks = (const rb_hooks *)N->rb_hooks;
    int d = N->cfg_delay != -2 ? N->cfg_delay : N->desc.input_delay == 0 ? 2 : N->desc.input_delay < 0 ? 0 : N->desc.input_delay;
    rb->delay = arc_clampi(d, 0, 10);
    int w = N->cfg_window >= 0 ? N->cfg_window : N->desc.max_rollback > 0 ? N->desc.max_rollback : 8;
    rb->window = arc_clampi(w, 0, ARC_NET_MAX_ROLLBACK);
    if (!rb->hooks && (!G.app.save_state || !G.app.load_state) && kind != RBK_REPLAY) {
        if (rb->window) { printf("ARC log net rollback: the game has no save_state/load_state: playing in lockstep (every input known before each tick)\n"); fflush(stdout); }
        rb->window = 0;
    }
    rb->st.ping_ms = -1;
    return rb;
}
static void rb_lost_host(const char *why);
/* the START entry: the match begins at the next tick. Called again when a replay or a re-simulation applies it again. */
static void rb_arm(uint64_t seed) {
    net_machine *N = NET;
    if (N->rb) {
        if (N->rb->kind == RBK_REPLAY || N->rb->kind == RBK_LIVE || N->rb->kind == RBK_TEST) return;   /* (applied again: nothing new) */
        rb_free(N->rb); N->rb = NULL;
    }
    int kind = N->replay ? RBK_REPLAY : N->rb_hooks ? RBK_TEST : RBK_LIVE;
    net_rb *rb = rb_new(kind);
    rb->seed = seed; rb->start_tick = G.ticks + 1; rb->local = arc_net_local_player();
    for (int p = 0; p < RB_MAXP; p++) if (N->V.p[p].in_match) { rb->mask |= (uint8_t)(1u << p); rb->np = p + 1; }
    if (!rb_in_match(rb, rb->local) && kind != RBK_REPLAY) { printf("ARC error net rollback: this machine is not in the match\n"); fflush(stdout); }
    N->rb = rb;
    if (kind == RBK_REPLAY) { for (int p = 0; p < RB_MAXP; p++) if (RBR.drop_at[p] >= 0) rb->drop_at[p] = RBR.drop_at[p]; rb->armed = true; return; }
    rb->simq = (rb_dg *)arc__xalloc(sizeof(rb_dg) * RB_SIMQ);
    /* the local input's first `delay` ticks are empty, sent like any other */
    for (int64_t f = 0; f < rb->delay; f++) rb_store(rb, rb->local, f, NULL, 0);
    rb->local_next = rb->delay; rb->have[rb->local] = rb->delay;
    double now = net_now();
    rb->online = N->online && !N->memory;   /* 0.10.4: a relay room */
    if (rb->online) {
        rb->hub = -1; for (int p = 0; p < RB_MAXP && rb->hub < 0; p++) if (rb_in_match(rb, p)) rb->hub = p;
        rb->relay_ip = N->join_ip; rb->relay_port = N->join_port; rb->next_reg = now;
    }
    if (rb_is_hub(rb)) {
        for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p) && p != rb->local) { rb->peer[p].on = true; rb->peer[p].opened = now; rb->peer[p].last_recv = now; rb->peer[p].known = rb->online; }
    } else { rb->peer[0].on = true; rb->peer[0].known = true; rb->peer[0].ip = N->join_ip; rb->peer[0].port = N->join_port; rb->peer[0].opened = now; rb->peer[0].last_recv = now; }
    if (N->memory) {
        rb->mem_ep = memudp_open(N->mode == MODE_HOST ? N->port : 50000 + (int)(((uintptr_t)N >> 4) % 10000));
        if (rb->mem_ep < 0) rb->mem_ep = memudp_open(60000 + rb->local);
    } else {
#if NET_SOCKETS
        int uport = N->mode == MODE_HOST && !rb->online ? N->port : 0;   /* (online: any port; the relay learns it) */
        rb->sock = ns_udp(uport, false);
        rb->sock_ok = rb->sock != NET_BAD;
        if (!rb->sock_ok) { printf("ARC error net rollback: cannot open UDP port %d\n", uport); fflush(stdout); }
#endif
    }
    rb->armed = true;
#if !NET_SOCKETS
    if (rb->online) {   /* 0.10.4: a browser has no UDP: it plays alone (the others drop it) */
        printf("ARC error net rollback: real-time matches through the relay need UDP (desktop builds); this build plays alone\n"); fflush(stdout);
        rb_lost_host("no UDP in this build");
    }
#endif
}
/* match tick 0 (and again when a correction goes back to it): the match generator and the START event */
static void rb_match_begin(net_rb *rb) {
    net_machine *N = NET;
    if (rb->hooks) { if (rb->hooks->start) rb->hooks->start(rb->seed); return; }
    arc_rng_seed(&g_rng, rb->seed, 0x524F4C4Cull);         /* "ROLL" */
    if (!rb->started_printed && !g_resim) {
        int n = 0; for (int p = 0; p < RB_MAXP; p++) n += rb_in_match(rb, p);
        printf("ARC net rollback start seed=%llu players=%d local=%d delay=%d max_rollback=%d tick=%llu\n", (unsigned long long)rb->seed, n, rb->local, rb->delay, rb->window, (unsigned long long)G.ticks);
        fflush(stdout); rb->started_printed = true;
    }
    char t[48]; snprintf(t, sizeof t, "%llu rollback", (unsigned long long)rb->seed);
    ev_push(ARC_NET_EV_START, N->V.owner, 0, true, t);
}
void arc__rb_tick_begin(void) {
    net_rb *rb = NET->rb;
    if (!rb || !rb->armed || rb->hooks) return;
    if (G.ticks < rb->start_tick) { rb->in_sim = false; return; }
    rb->sim_frame = (int64_t)(G.ticks - rb->start_tick); rb->in_sim = true;
    if (rb->sim_frame == 0) rb_match_begin(rb);
}
static void rb_hook_tick(net_rb *rb, int64_t f) {
    rb->sim_frame = f; rb->in_sim = true;
    if (f == 0) rb_match_begin(rb);
    rb->hooks->tick();
    rb->in_sim = false;
}
static void rb_do_rollback(net_rb *rb) {
    if (rb->rollback_to == RB_NONE) return;
    int64_t R = rb->rollback_to; rb->rollback_to = RB_NONE;
    if (R >= rb->cur) return;
    rb_slot *s = &rb->slot[R % RB_SLOTS];
    if (s->frame != R) { printf("ARC error net rollback: no snapshot of tick %lld (now %lld)\n", (long long)R, (long long)rb->cur); fflush(stdout); return; }
    bool ok = rb->hooks ? rb->hooks->load(s->data, s->size) : rb_eng_restore(s->data, s->size);
    if (!ok) { printf("ARC error net rollback: load_state refused the snapshot of tick %lld\n", (long long)R); fflush(stdout); return; }
    int depth = (int)(rb->cur - R);
    rb->st.rollbacks++; rb->depth_sum += depth; if (depth > rb->st.max_depth) rb->st.max_depth = depth;
    rb_events_drop_from(rb, R); rb_sounds_before_resim(rb, R);
    bool eng = !rb->hooks;
    rb->resim = true;
    if (eng) { g_resim = true; arc__audio_silent = true; arc__audio_rb = true; }
    for (int64_t f = R; f < rb->cur; f++) {
        if (f > R) rb_capture(rb, f);
        rb_hash_start(rb, f);
        if (eng) { rb_eng_resim_tick(); rb->in_sim = false; }
        else rb_hook_tick(rb, f);
        rb->st.resim_ticks++;
    }
    if (eng) { g_resim = false; arc__audio_silent = false; arc__audio_rb = false; }
    rb->resim = false;
    rb_sounds_after_resim(rb, R, rb->cur);
}
static void rb_desync(net_rb *rb, int64_t f, int with, uint64_t theirs);
static void rb_update_final(net_rb *rb) {
    net_machine *N = NET;
    int64_t m = rb->cur;
    if (rb->kind == RBK_LIVE || rb->kind == RBK_TEST)
        for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p) && p != rb->local) { int64_t e = rb_eff_have(rb, p); if (e < m) m = e; }
    if (m > rb->final_) rb->final_ = m;
    /* the state now is the start of cur: hash it once it is final (a frozen or waiting match still compares) */
    if (rb->final_ == rb->cur && rb_hashed_tick(rb, rb->cur) && !rb->synctest) {
        rb_hrec *r = &rb->hr[rb_hidx(rb, rb->cur) % RB_HRING];
        if (r->frame != rb->cur || !r->have) rb_hash_start(rb, rb->cur);
    }
    rb_events_flush(rb, rb->final_);
    /* recording: every player's confirmed input, in tick order */
    if (N->rec_path[0] && !N->replay && !rb->hooks) {
        for (; rb->rec_upto < rb->final_; rb->rec_upto++) {
            for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p)) {
                if (rb_dropped_at(rb, p, rb->rec_upto)) continue;    /* (a "D" line says so) */
                const rb_in *e = rb_slot_in(rb, p, rb->rec_upto);
                char line[ARC_NET_INPUT_MAX * 2 + 64]; int k = snprintf(line, sizeof line, "I %lld %d ", (long long)rb->rec_upto, p);
                for (int i = 0; i < e->size; i++) k += snprintf(line + k, sizeof line - (size_t)k, "%02x", e->d[i]);
                if (!e->size) snprintf(line + k, sizeof line - (size_t)k, "-");
                net_rec_raw(line);
            }
        }
        if (N->rec) fflush(N->rec);
    } else rb->rec_upto = rb->final_;
    /* hashes: compare what the others sent with mine, once mine is final */
    for (int i = 0; i < RB_HRING; i++) {
        rb_hrec *r = &rb->hr[i];
        if (r->frame < 0 || !r->have || r->frame > rb->final_) continue;
        if (r->frame > rb->last_hash_tick || rb->last_hash_tick < 0) { if (!rb->synctest) { rb->last_hash_tick = r->frame; rb->last_hash = r->mine; } }
        for (int p = 0; p < RB_MAXP; p++) if ((r->rmask >> p) & 1 && !((r->bad >> p) & 1) && r->remote[p] != r->mine) { r->bad |= (uint8_t)(1u << p); rb_desync(rb, r->frame, p, r->remote[p]); }
    }
    if (rb->synctest && rb->cur > 0) { rb->last_hash_tick = rb->cur - 1; rb_hrec *r = &rb->hr[rb_hidx(rb, rb->cur - 1) % RB_HRING]; if (r->frame == rb->cur - 1) rb->last_hash = r->mine; }
    rb_sounds_prune(rb);
}

/* ======================================================================== */
/* Datagrams in and out                                                     */
/* ======================================================================== */
typedef struct { uint8_t *p; size_t n, cap; bool full; } rb_w;
static void w8(rb_w *w, unsigned v) { if (w->n + 1 > w->cap) { w->full = true; return; } w->p[w->n++] = (uint8_t)v; }
static void w16(rb_w *w, unsigned v) { w8(w, v & 0xFF); w8(w, (v >> 8) & 0xFF); }
static void w32(rb_w *w, uint32_t v) { for (int i = 0; i < 4; i++) w8(w, (v >> (8 * i)) & 0xFF); }
static void w64(rb_w *w, uint64_t v) { for (int i = 0; i < 8; i++) w8(w, (unsigned)((v >> (8 * i)) & 0xFF)); }
static uint32_t rb_ms(void) { return (uint32_t)(int64_t)(net_now() * 1000.0); }
static void rb_send_peer(net_rb *rb, int q, int type) {
    net_machine *N = NET; rb_peer *pe = &rb->peer[q];
    if (!pe->on || !pe->known) return;
    double now = net_now();
    rb_w w = { rb->pkt, 0, RB_PKT, false };
    w8(&w, 'A'); w8(&w, 'R'); w8(&w, 'B'); w8(&w, RB_PROTO);
    w32(&w, (uint32_t)N->session); w8(&w, (unsigned)rb->local); w8(&w, (unsigned)type);
    w32(&w, (uint32_t)rb->cur); w32(&w, rb_ms()); w32(&w, pe->echo);
    double hold = pe->echo ? (now - pe->echo_at) * 1000.0 : 0; w16(&w, (unsigned)arc_clampi((int)hold, 0, 65535));
    int adv = (int)(pe->adv * 100.0f); w16(&w, (unsigned)(uint16_t)(int16_t)arc_clampi(adv, -32000, 32000));
    w8(&w, rb->desync_reported ? 1u : 0u); w32(&w, (uint32_t)(rb->desync_tick < 0 ? 0 : rb->desync_tick));
    w8(&w, RB_MAXP);
    for (int p = 0; p < RB_MAXP; p++) w32(&w, (uint32_t)rb->have[p]);
    bool hub = rb_is_hub(rb);
    int nd = 0; for (int p = 0; p < RB_MAXP; p++) nd += rb->drop_at[p] >= 0 && hub;
    w8(&w, (unsigned)nd);
    for (int p = 0; p < RB_MAXP; p++) if (rb->drop_at[p] >= 0 && hub) { w8(&w, (unsigned)p); w32(&w, (uint32_t)rb->drop_at[p]); }
    /* my newest final hashes */
    int64_t x0 = (rb->final_ / RB_HASH_EVERY) * RB_HASH_EVERY; int nh = 0; int64_t hf[4]; uint64_t hv[4];
    for (int k = 0; k < 4 && x0 - k * RB_HASH_EVERY >= 0 && !rb->synctest; k++) {
        int64_t f = x0 - k * RB_HASH_EVERY; rb_hrec *r = &rb->hr[rb_hidx(rb, f) % RB_HRING];
        if (r->frame == f && r->have && f <= rb->final_) { hf[nh] = f; hv[nh] = r->mine; nh++; }
    }
    w8(&w, (unsigned)nh);
    for (int k = 0; k < nh; k++) { w32(&w, (uint32_t)hf[k]); w64(&w, hv[k]); }
    /* inputs: client its own; host every player's but the peer's own; from what the peer has */
    size_t nb_at = w.n; w8(&w, 0); int nblocks = 0;
    if (type == U_INPUT)
        for (int p = 0; p < RB_MAXP; p++) {
            if (!rb_in_match(rb, p) || p == q) continue;
            if (!hub && p != rb->local) continue;
            int64_t from = pe->has[p], to = rb->have[p];
            if (rb->drop_at[p] >= 0 && to > rb->drop_at[p]) to = rb->drop_at[p];
            if (from < to - (RB_RING - 4)) from = to - (RB_RING - 4);
            if (from >= to) continue;
            if (to - from > 60) to = from + 60;
            if (w.n + 6 + 2 > RB_BUDGET) break;
            size_t hdr = w.n; w8(&w, (unsigned)p); w32(&w, (uint32_t)from); w8(&w, 0);
            int cnt = 0;
            for (int64_t f = from; f < to; f++) {
                const rb_in *e = rb_slot_in(rb, p, f);
                if (e->frame != f || !e->confirmed) break;
                if (w.n + 1 + e->size > RB_BUDGET) break;
                w8(&w, e->size); for (int i = 0; i < e->size; i++) w8(&w, e->d[i]);
                cnt++;
            }
            if (!cnt) { w.n = hdr; continue; }
            rb->pkt[hdr + 5] = (uint8_t)cnt; nblocks++;
        }
    rb->pkt[nb_at] = (uint8_t)nblocks;
    if (w.full) return;
    if (rb->online) rb_route(rb, hub ? q : rb->hub, rb->pkt, w.n);   /* 0.10.4: through the relay, or straight */
    else rb_send_dgram(rb, pe->ip, pe->port, rb->pkt, w.n);
    pe->last_send = now; rb->last_any_send = now;
}
static void rb_send_all(net_rb *rb, bool force) {
    double now = net_now();
    for (int q = 0; q < RB_MAXP; q++) {
        rb_peer *pe = &rb->peer[q];
        if (!pe->on || !pe->known) continue;
        if (force || now - pe->last_send >= 1.0 / 60.0) rb_send_peer(rb, q, U_INPUT);
    }
}
typedef struct { const uint8_t *p; size_t n, i; bool bad; } rb_r;
static unsigned rr8(rb_r *r) { if (r->i + 1 > r->n) { r->bad = true; return 0; } return r->p[r->i++]; }
static unsigned rr16(rb_r *r) { unsigned a = rr8(r); return a | (rr8(r) << 8); }
static uint32_t rr32(rb_r *r) { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)rr8(r) << (8 * i); return v; }
static uint64_t rr64(rb_r *r) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)rr8(r) << (8 * i); return v; }
static void rb_set_drop(net_rb *rb, int p, int64_t f, const char *why);
static void rb_receive(net_rb *rb, const uint8_t *d, int n, uint32_t ip, int port) {
    net_machine *N = NET;
    rb_r r = { d, (size_t)n, 0, false };
    if (n < 8 || d[0] != 'A' || d[1] != 'R' || d[2] != 'B' || d[3] != RB_PROTO) return;
    r.i = 4;
    uint32_t sess = rr32(&r); int from = (int)rr8(&r), type = (int)rr8(&r);
    if (r.bad || sess != (uint32_t)N->session) return;
    bool hub = rb_is_hub(rb);
    int q = hub ? from : 0;
    if (hub) { if (!rb_in_match(rb, from) || from == rb->local) return; }
    else if (from != rb_hub_player(rb)) return;
    rb_peer *pe = &rb->peer[q];
    if (!pe->on) return;
    double now = net_now();
    if (hub && !rb->online) { pe->ip = ip; pe->port = port; pe->known = true; }
    pe->last_recv = now; rb->st.packets_received++;
    if (type == U_BYE) { if (hub) rb_drop_player(from, "left the match"); else rb_lost_host("the host left the match"); return; }
    uint32_t frame = rr32(&r), tsend = rr32(&r), techo = rr32(&r); unsigned hold = rr16(&r); int adv = (int)(int16_t)rr16(&r);
    unsigned flags = rr8(&r); uint32_t dtick = rr32(&r);
    unsigned np = rr8(&r); if (r.bad || np > 16) return;
    uint32_t acks[16]; for (unsigned p = 0; p < np; p++) acks[p] = rr32(&r);
    if (r.bad) return;
    if (techo) {
        float rtt = (float)((double)(rb_ms() - techo) - (double)hold); if (rtt < 0) rtt = 0;
        pe->rtt = pe->rtt_known ? pe->rtt * 0.8f + rtt * 0.2f : rtt; pe->rtt_known = true;
    }
    pe->echo = tsend ? tsend : 1; pe->echo_at = now;
    if (!pe->frame_known || (int64_t)frame >= pe->frame) { pe->frame = frame; pe->frame_at = now; pe->frame_known = true; pe->radv = (float)adv / 100.0f; }
    for (unsigned p = 0; p < np && p < RB_MAXP; p++) if ((int64_t)acks[p] > pe->has[p]) pe->has[p] = acks[p];
    unsigned nd = rr8(&r);
    for (unsigned k = 0; k < nd && !r.bad; k++) { unsigned p = rr8(&r); uint32_t f = rr32(&r); if (!r.bad && !hub && p < RB_MAXP) rb_set_drop(rb, (int)p, f, "the host dropped them"); }
    unsigned nh = rr8(&r);
    for (unsigned k = 0; k < nh && !r.bad; k++) {
        uint32_t f = rr32(&r); uint64_t h = rr64(&r);
        if (r.bad || f % RB_HASH_EVERY) continue;
        rb_hrec *hr = &rb->hr[rb_hidx(rb, f) % RB_HRING];
        if (hr->frame != (int64_t)f) { if (hr->frame > (int64_t)f) continue; memset(hr, 0, sizeof *hr); hr->frame = f; }
        hr->remote[from] = h; hr->rmask |= (uint8_t)(1u << from);
    }
    unsigned nb = rr8(&r);
    for (unsigned k = 0; k < nb && !r.bad; k++) {
        unsigned p = rr8(&r); uint32_t first = rr32(&r); unsigned cnt = rr8(&r);
        for (unsigned i = 0; i < cnt && !r.bad; i++) {
            unsigned sz = rr8(&r); if (sz > ARC_NET_INPUT_MAX || r.i + sz > r.n) { r.bad = true; break; }
            const uint8_t *in = r.p + r.i; r.i += sz;
            if (hub && (int)p != from) continue;     /* a client speaks for itself only */
            if (p < RB_MAXP) rb_arrive(rb, (int)p, (int64_t)first + i, in, (int)sz);
        }
    }
    if ((flags & 1) && !rb->desync_reported) rb_desync(rb, (int64_t)dtick, from, 0);
}
static void rb_poll(net_rb *rb) {
    if (rb->kind != RBK_LIVE && rb->kind != RBK_TEST) return;
    if (NET->sim_dropped) return;
    rb_simq_flush(rb);
    uint8_t buf[RB_PKT]; uint32_t ip; int port, n, k = 0;
    while (k++ < 256 && (n = rb_wire_recv(rb, buf, sizeof buf, &ip, &port)) > 0) {
        if (n >= NCORE_UDP_HDR && buf[0] == 'A' && buf[1] == 'R' && buf[2] == 'U') { rb_aru(rb, buf, n, ip, port); continue; }   /* 0.10.4 */
        if (rb->online) {                                  /* straight from a peer: only from the address it punched from */
            int from = n > 8 ? buf[8] : -1;
            if (from < 0 || from >= RB_MAXP || !rb->rt[from].pub_known || rb->rt[from].pip != ip || rb->rt[from].pport != port) continue;
            rb->rt[from].last_direct = net_now();
        }
        rb_receive(rb, buf, n, ip, port);
    }
}

/* ---- leaving, drops ---- */
static void rb_set_drop(net_rb *rb, int p, int64_t f, const char *why) {
    net_machine *N = NET;
    if (!rb_in_match(rb, p) || p == rb->local || rb->drop_at[p] >= 0) return;
    rb->drop_at[p] = f;
    if (f < rb->cur && f < rb->rollback_to) rb->rollback_to = f;   /* the ticks from there ran with guesses: now zeros */
    if (rb_is_hub(rb)) rb->peer[p].on = false;
    rb->n_dropped++;
    char name[64]; snprintf(name, sizeof name, "%s", N->V.p[p].name);
    if (!rb->hooks || g_net_verbose) { printf("ARC net dropped %d %s at tick %lld (%s)\n", p, name, (long long)f, why); fflush(stdout); }
    char line[64]; snprintf(line, sizeof line, "D %d %lld", p, (long long)f); if (!rb->hooks) net_rec_raw(line);
    ev_push(ARC_NET_EV_DROPPED, p, (uint32_t)f, false, name);
}
static void rb_drop_player(int p, const char *why) {
    net_rb *rb = NET->rb;
    if (!rb || (rb->kind != RBK_LIVE && rb->kind != RBK_TEST)) return;
    if (!rb_is_hub(rb)) { if (rb->online && p == rb->hub && p != rb->local) rb_lost_host(why); return; }   /* (0.10.4: the relay says the hub left) */
    rb_set_drop(rb, p, rb->have[p], why);
}
static void rb_lost_host(const char *why) {
    net_rb *rb = NET->rb;
    if (!rb || (rb->kind != RBK_LIVE && rb->kind != RBK_TEST)) return;
    for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p) && p != rb->local) rb_set_drop(rb, p, rb->have[p], why);
    rb->peer[0].on = false;
}
static void rb_end(void) {
    net_machine *N = NET; net_rb *rb = N->rb;
    if (!rb) return;
    if (rb->kind == RBK_LIVE || rb->kind == RBK_TEST) { for (int k = 0; k < 2; k++) for (int q = 0; q < RB_MAXP; q++) if (rb->peer[q].on && rb->peer[q].known) { bool s = N->sim.on; N->sim.on = false; rb_send_peer(rb, q, U_BYE); N->sim.on = s; } }
    rb_free(rb); N->rb = NULL;
}

/* ======================================================================== */
/* Desync and synctest reports                                              */
/* ======================================================================== */
static void rb_write_inputs(net_rb *rb, const char *path, int64_t upto) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    for (int64_t t = upto - 64 < 0 ? 0 : upto - 64; t < upto && t < rb->final_; t++)
        for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p)) {
            const rb_in *e = rb_slot_in(rb, p, t);
            if (e->frame != t) continue;
            fprintf(f, "%lld %d ", (long long)t, p);
            for (int i = 0; i < e->size; i++) fprintf(f, "%02x", e->d[i]);
            fprintf(f, "\n");
        }
    fclose(f);
}
static void rb_desync(net_rb *rb, int64_t f, int with, uint64_t theirs) {
    net_machine *N = NET;
    if (rb->desync_reported) return;
    rb->desync_reported = true; rb->desync_tick = f; rb->st.desync_tick = f;
    char dir[700]; net_mkdir(g_net_report);
    snprintf(dir, sizeof dir, "%s/tick%lld_p%d", g_net_report, (long long)f, rb->local); net_mkdir(dir);
    printf("ARC error desync tick=%lld arc_tick=%llu player=%d with=%d report=%s\n", (long long)f, (unsigned long long)(rb->start_tick + (uint64_t)f), rb->local, with, dir); fflush(stdout);
    char path[800]; FILE *fp;
    rb_hrec *r = &rb->hr[rb_hidx(rb, f) % RB_HRING]; rb_bytes *b = &rb->br[rb_hidx(rb, f) % RB_BRING];
    if (!rb->hooks) { snprintf(path, sizeof path, "%s/dump.json", dir); do_dump(path); }
    snprintf(path, sizeof path, "%s/log.txt", dir); arc_net_log_save(path);
    snprintf(path, sizeof path, "%s/inputs.txt", dir); rb_write_inputs(rb, path, f);
    snprintf(path, sizeof path, "%s/state.bin", dir);
    if (b->frame == f) arc_file_write(path, b->d, b->n);
    snprintf(path, sizeof path, "%s/info.json", dir);
    if ((fp = fopen(path, "wb")) != NULL) {
        fprintf(fp, "{\"desync_tick\":%lld,\"player\":%d,\"with\":%d,\"report_tick\":%lld,\"my_hash\":\"%016llx\",\"my_hash_known\":%s,\"hashes\":{\"%d\":\"%016llx\"",
                (long long)f, rb->local, with, (long long)rb->cur, (unsigned long long)(r->frame == f ? r->mine : 0), r->frame == f && r->have ? "true" : "false",
                rb->local, (unsigned long long)(r->frame == f ? r->mine : 0));
        for (int p = 0; p < RB_MAXP; p++) if (r->frame == f && ((r->rmask >> p) & 1)) fprintf(fp, ",\"%d\":\"%016llx\"", p, (unsigned long long)r->remote[p]);
        if (theirs && !(r->frame == f && ((r->rmask >> with) & 1))) fprintf(fp, ",\"%d\":\"%016llx\"", with, (unsigned long long)theirs);
        fprintf(fp, "},\"state_bytes\":%zu,\"state_tick_known\":%s,\"hash_source\":\"%s\"}\n", b->frame == f ? b->n : (size_t)0, b->frame == f ? "true" : "false",
                rb->hooks ? "test" : G.app.net_state ? "net_state" : "save_state");
        fclose(fp);
    }
    char txt[160]; snprintf(txt, sizeof txt, "desync at tick %lld (report %.100s)", (long long)f, dir);
    ev_push(ARC_NET_EV_DESYNC, with, (uint32_t)f, false, txt);
    N->desync_done = true;
}
static void rb_synctest_fail(net_rb *rb, int64_t f, const rb_bytes *first, const uint8_t *now, size_t n) {
    rb->synctest_failed = true; rb->desync_tick = f; rb->st.desync_tick = f;
    char dir[700]; net_mkdir(g_net_report);
    snprintf(dir, sizeof dir, "%s/synctest_tick%lld", g_net_report, (long long)f); net_mkdir(dir);
    size_t at = 0; if (first) { size_t m = first->n < n ? first->n : n; while (at < m && first->d[at] == now[at]) at++; }
    printf("ARC error synctest tick=%lld: the state differs when tick %lld is simulated again (first differing byte %zu of %zu): "
           "save_state/load_state miss something tick() changes, or tick() reads something outside the state; report=%s\n", (long long)f, (long long)f, at, n, dir);
    fflush(stdout);
    char path[800];
    if (first) { snprintf(path, sizeof path, "%s/state_first.bin", dir); arc_file_write(path, first->d, first->n); }
    snprintf(path, sizeof path, "%s/state_again.bin", dir); arc_file_write(path, now, n);
    if (!rb->hooks) { snprintf(path, sizeof path, "%s/dump.json", dir); do_dump(path); }
    NET->desync_done = true;
    char txt[160]; snprintf(txt, sizeof txt, "synctest failed at tick %lld (report %.100s)", (long long)f, dir);
    ev_push(ARC_NET_EV_DESYNC, rb->local, (uint32_t)f, false, txt);
}

/* ======================================================================== */
/* The main loop's hooks                                                    */
/* ======================================================================== */
int arc__rb_mode(void) {
    net_rb *rb = NET->rb;
    if (!rb || !rb->armed) return 0;
    return rb->kind == RBK_LIVE ? 1 : rb->kind == RBK_SYNCTEST ? 2 : 0;
}
bool arc__rb_live_match(void) { net_rb *rb = NET->rb; return rb && rb->armed && (rb->kind == RBK_LIVE || rb->kind == RBK_SYNCTEST); }
static void rb_time_sync(net_rb *rb) {
    double now = net_now(), hz = rb_hz();
    float need = 0; bool any = false;
    for (int q = 0; q < RB_MAXP; q++) {
        rb_peer *pe = &rb->peer[q];
        if (!pe->on || !pe->frame_known) continue;
        double est = (double)pe->frame + ((now - pe->frame_at) + (pe->rtt_known ? pe->rtt * 0.0005 : 0.0)) * hz;
        float la = (float)((double)rb->cur - est);
        pe->adv = pe->adv_init ? pe->adv * 0.9f + la * 0.1f : la; pe->adv_init = true;
        float d = (pe->adv - pe->radv) * 0.5f;
        if (!any || d > need) need = d;
        any = true;
    }
    rb->st.frame_advantage = any ? need : 0;
    if (rb->cur >= rb->next_sync) {
        rb->next_sync = rb->cur + 60;
        if (any && need >= 0.6f && !rb->skip_debt) rb->skip_debt = arc_mini((int)(need + 0.5f), 8);   /* (rounded: half a tick ahead is worth one) */
    }
}
static void rb_waiting_check(net_rb *rb, bool stalled) {
    net_machine *N = NET; double now = net_now();
    if (stalled) {
        if (rb->stall_since <= 0) rb->stall_since = rb->stall_mark = now;
        rb->st.stalled_ms += (int64_t)((now - rb->stall_mark) * 1000.0); rb->stall_mark = now;
        if (!rb->wait_told && now - rb->stall_since >= RB_WAIT_EVENT) {
            int who = -1; int64_t lo = RB_NONE;
            for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p) && p != rb->local && rb_eff_have(rb, p) < lo) { lo = rb_eff_have(rb, p); who = p; }
            rb->wait_told = true; rb->waiting = who; rb->n_waiting++;
            if (!rb->hooks || g_net_verbose) { printf("ARC net waiting for player %d at tick %lld\n", who, (long long)rb->cur); fflush(stdout); }
            ev_push(ARC_NET_EV_WAITING, who, (uint32_t)rb->cur, false, who >= 0 ? N->V.p[who].name : "");
        }
        return;
    }
    if (rb->stall_since > 0) {
        rb->st.stalled_ms += (int64_t)((now - rb->stall_mark) * 1000.0);
        if (rb->wait_told) {
            if (!rb->hooks || g_net_verbose) { printf("ARC net resumed at tick %lld after %.0f ms\n", (long long)rb->cur, (now - rb->stall_since) * 1000.0); fflush(stdout); }
            ev_push(ARC_NET_EV_RESUMED, rb->waiting, (uint32_t)rb->cur, false, "");
            rb->n_resumed++;
        }
        rb->stall_since = 0; rb->wait_told = false; rb->waiting = -1;
    }
}
static void rb_timeouts(net_rb *rb) {
    double now = net_now();
    if (rb->kind != RBK_LIVE && rb->kind != RBK_TEST) return;
    for (int q = 0; q < RB_MAXP; q++) {
        rb_peer *pe = &rb->peer[q];
        if (!pe->on) continue;
        double quiet = now - pe->last_recv;
        if (quiet > RB_TIMEOUT && (pe->known || quiet > 10.0)) {
            if (rb_is_hub(rb)) rb_set_drop(rb, q, rb->have[q], "no packets for 3 s");
            else rb_lost_host("no packets from the host for 3 s");
        }
    }
}
static bool rb_frozen(net_rb *rb) {          /* --net-sim freeze_at: this machine hangs (no ticks, no packets) */
    net_machine *N = NET; double now = net_now();
    if (N->sim.freeze_at && !rb->freeze_done && G.ticks >= N->sim.freeze_at && !rb->hooks) {
        rb->freeze_done = true; rb->freeze_until = now + N->sim.freeze_ms * 0.001;
        printf("ARC log net frozen for %.0f ms (simulated)\n", N->sim.freeze_ms); fflush(stdout);
    }
    return rb->freeze_until > now;
}
static void rb_frame(void) {                    /* once a frame: datagrams, corrections, confirmations */
    net_rb *rb = NET->rb;
    if (!rb || !rb->armed || (rb->kind != RBK_LIVE && rb->kind != RBK_TEST)) return;
    if (rb_frozen(rb)) return;
    rb_online_upkeep(rb);   /* 0.10.4 */
    if (G.ticks < rb->start_tick && !rb->hooks) { rb_poll(rb); return; }
    rb_poll(rb);
    if (!rb->in_sim) { rb_do_rollback(rb); rb_update_final(rb); }
    rb_timeouts(rb);
    rb_send_all(rb, false);
}
int arc__rb_before_tick(void) {
    net_rb *rb = NET->rb;
    if (!rb || !rb->armed) return 0;
    if (rb->kind == RBK_LIVE || rb->kind == RBK_TEST) {
        if (rb_frozen(rb)) return 2;
        rb_poll(rb);
    }
    if (rb->synctest && rb->cur > 0) { int64_t R = rb->cur - rb->synctest; rb->rollback_to = R < 0 ? 0 : R; }
    rb_do_rollback(rb);
    rb_update_final(rb);
    if (rb->stop_at >= 0 && rb->cur >= rb->stop_at) return 2;
    if (rb->kind == RBK_LIVE || rb->kind == RBK_TEST) {
        int64_t lo = RB_NONE;
        for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p) && p != rb->local) { int64_t e = rb_eff_have(rb, p); if (e < lo) lo = e; }
        if (lo != RB_NONE && rb->cur >= lo + rb->window) { rb_waiting_check(rb, true); rb_send_all(rb, false); return 2; }
        if (rb->skip_debt > 0 && rb->skip_gap <= 0) { rb->skip_debt--; rb->skip_gap = 2; rb->st.skipped++; rb_send_all(rb, false); return 1; }
        rb_waiting_check(rb, false);
    }
    if (!rb_capture(rb, rb->cur)) { printf("ARC error net rollback: cannot snapshot tick %lld\n", (long long)rb->cur); fflush(stdout); }
    rb_hash_start(rb, rb->cur);
    return 0;
}
void arc__rb_new_tick(void) {                   /* the local input for tick cur + delay */
    net_rb *rb = NET->rb;
    if (!rb || !rb->armed || rb->kind == RBK_REPLAY) return;
    int64_t target = rb->cur + rb->delay;
    if (target < rb->local_next) return;                      /* the delay just went down: this one is dropped */
    uint8_t b[ARC_NET_INPUT_MAX]; int n = rb_pack_local(rb, b);
    for (int64_t f = rb->local_next; f <= target; f++) {
        rb_store(rb, rb->local, f, b, n);
        if (rb->kind == RBK_SYNCTEST)                         /* the other players: scripted, or nothing */
            for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p) && p != rb->local) rb_store(rb, p, f, rb->script[p], rb->script_on[p] ? rb->script_n[p] : 0);
    }
    rb->local_next = target + 1; rb->have[rb->local] = rb->local_next;
    if (rb->kind == RBK_SYNCTEST) for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p)) rb->have[p] = rb->local_next;
}
void arc__rb_after_tick(void) {
    net_rb *rb = NET->rb;
    if (!rb || !rb->armed || rb->kind == RBK_REPLAY) return;
    rb->in_sim = false;
    rb->cur++;
    if (rb->skip_gap > 0) rb->skip_gap--;
    if (rb->kind == RBK_LIVE || rb->kind == RBK_TEST) { rb_time_sync(rb); rb_send_all(rb, true); }
}

/* ======================================================================== */
/* Public API                                                               */
/* ======================================================================== */
static int64_t rb_query_frame(const net_rb *rb) {
    if (rb->in_sim) return rb->sim_frame;
    if (rb->kind == RBK_REPLAY || !rb->hooks) return (int64_t)G.ticks - (int64_t)rb->start_tick - 1;   /* (outside a tick: the last one) */
    return rb->cur - 1;
}
static bool rb_active(const net_rb *rb) { return rb && rb->armed && (rb->hooks || rb->kind == RBK_REPLAY ? true : G.ticks >= rb->start_tick); }
bool arc_net_rollback(void) { net_rb *rb = NET->rb; return rb_active(rb) && (rb->hooks || G.ticks >= rb->start_tick); }
int64_t arc_net_match_tick(void) {
    net_rb *rb = NET->rb;
    if (!arc_net_rollback()) return -1;
    int64_t f = rb_query_frame(rb); return f < 0 ? 0 : f;
}
static struct { uint64_t tick; bool have; int n; uint8_t d[ARC_NET_INPUT_MAX]; } g_rb_off;   /* offline: this tick's input */
const void *arc_net_input(int player, int *size) {
    net_rb *rb = NET->rb;
    if (!arc_net_rollback()) {
        if (player != arc_net_local_player()) { if (size) *size = 0; return NULL; }
        if (!g_rb_off.have || g_rb_off.tick != G.ticks) {
            memset(g_rb_off.d, 0, sizeof g_rb_off.d);
            size_t n = G.app.net_input ? G.app.net_input(g_rb_off.d, ARC_NET_INPUT_MAX) : (size_t)rb_default_pack(g_rb_off.d);
            g_rb_off.n = (int)(n > ARC_NET_INPUT_MAX ? ARC_NET_INPUT_MAX : n); g_rb_off.tick = G.ticks; g_rb_off.have = true;
        }
        if (size) *size = g_rb_off.n;
        return g_rb_off.d;
    }
    if (!rb_in_match(rb, player)) { if (size) *size = 0; return NULL; }
    const rb_in *e = rb_input_at(rb, player, rb_query_frame(rb), rb->in_sim);
    if (size) *size = e->size;
    return e->d;
}
bool arc_net_input_predicted(int player) {
    net_rb *rb = NET->rb;
    if (!arc_net_rollback() || !rb_in_match(rb, player)) return false;
    return !rb_input_at(rb, player, rb_query_frame(rb), false)->confirmed;
}
bool arc_net_input_dropped(int player) {
    net_rb *rb = NET->rb;
    if (!arc_net_rollback() || !rb_in_match(rb, player)) return false;
    return rb_dropped_at(rb, player, rb_query_frame(rb));
}
static const uint8_t *rb_default_at(int player, int back) {
    net_rb *rb = NET->rb;
    if (!arc_net_rollback()) return NULL;
    if (!rb_in_match(rb, player)) return g_rb_zero;
    int64_t f = rb_query_frame(rb) - back;
    if (f < 0) return g_rb_zero;
    const rb_in *e = rb_input_at(rb, player, f, rb->in_sim);
    return e->size >= 32 || e->size == 0 ? e->d : g_rb_zero;
}
bool arc_net_key_down(int player, arc_key k) {
    if (k <= 0 || k >= ARC_KEY_COUNT) return false;
    if (!arc_net_rollback()) return player == arc_net_local_player() && arc_key_down(k);
    const uint8_t *b = rb_default_at(player, 0); return (b[k >> 3] >> (k & 7)) & 1;
}
bool arc_net_key_pressed(int player, arc_key k) {
    if (k <= 0 || k >= ARC_KEY_COUNT) return false;
    if (!arc_net_rollback()) return player == arc_net_local_player() && arc_key_pressed(k);
    const uint8_t *b = rb_default_at(player, 0); bool now = (b[k >> 3] >> (k & 7)) & 1;
    if (!now) return false;
    const uint8_t *p = rb_default_at(player, 1); return !((p[k >> 3] >> (k & 7)) & 1);
}
float arc_net_axis(int player, arc_axis a) {
    if (a < 0 || a >= ARC_AXIS_COUNT) return 0;
    if (!arc_net_rollback()) return player == arc_net_local_player() ? (float)(int)((arc_pad_axis(a) * 127.0f) + (arc_pad_axis(a) < 0 ? -0.5f : 0.5f)) / 127.0f : 0;
    const uint8_t *b = rb_default_at(player, 0); return (float)(int8_t)b[32 + a] / 127.0f;
}
bool arc_net_resimulating(void) { net_rb *rb = NET->rb; return rb && rb->resim; }
void arc_net_set_input_delay(int ticks) {
    net_machine *N = NET; ticks = arc_clampi(ticks, 0, 10); N->cfg_delay = ticks;
    if (N->rb && N->rb->kind != RBK_REPLAY) N->rb->delay = ticks;
}
int arc_net_input_delay(void) {
    net_machine *N = NET;
    if (N->rb) return N->rb->delay;
    if (N->cfg_delay != -2) return N->cfg_delay;
    return N->desc.input_delay == 0 ? 2 : N->desc.input_delay < 0 ? 0 : arc_mini(N->desc.input_delay, 10);
}
int arc_net_waiting_for(void) { net_rb *rb = NET->rb; return rb && rb->wait_told ? rb->waiting : -1; }
arc_net_rb_stats arc_net_stats(void) {
    net_rb *rb = NET->rb; arc_net_rb_stats s;
    if (!rb) { memset(&s, 0, sizeof s); s.ping_ms = -1; s.last_hash_tick = -1; s.desync_tick = -1; s.input_delay = arc_net_input_delay(); s.local = arc_net_local_player(); return s; }
    s = rb->st;
    s.active = rb->armed && (rb->kind == RBK_LIVE || rb->kind == RBK_SYNCTEST || rb->kind == RBK_TEST);
    s.tick = rb->kind == RBK_REPLAY ? arc_net_match_tick() + 1 : rb->cur; s.confirmed = rb->kind == RBK_REPLAY ? s.tick : rb->final_;
    s.input_delay = rb->delay; s.max_rollback = rb->window; s.local = rb->local;
    s.players = 0; for (int p = 0; p < RB_MAXP; p++) s.players += rb_in_match(rb, p);
    s.avg_depth = rb->st.rollbacks ? (float)(rb->depth_sum / rb->st.rollbacks) : 0;
    int ping = -1; for (int q = 0; q < RB_MAXP; q++) if (rb->peer[q].rtt_known && (int)rb->peer[q].rtt > ping) ping = (int)(rb->peer[q].rtt + 0.5f);
    s.ping_ms = ping;
    s.last_hash_tick = rb->last_hash_tick; s.last_hash = rb->last_hash; s.desync_tick = rb->desync_tick;
    return s;
}
int arc__rb_fps_line(char *out, size_t n) {
    net_rb *rb = NET->rb;
    if (!rb || !rb->armed || (rb->kind != RBK_LIVE && rb->kind != RBK_SYNCTEST)) return 0;
    arc_net_rb_stats s = arc_net_stats();
    snprintf(out, n, "net d%d rb %d/%d avg %.1f %dms %+.1f%s", s.input_delay, s.rollbacks, s.max_depth, (double)s.avg_depth, s.ping_ms, (double)s.frame_advantage,
             rb->wait_told ? " WAIT" : "");
    return 1;
}

/* ---- recordings: "I tick player hex" / "D player tick" lines in the .net file ---- */
static bool rb_replay_line(const char *line) {
    if (line[0] == 'D' && line[1] == ' ') {
        int p; long long f; if (sscanf(line + 2, "%d %lld", &p, &f) == 2 && p >= 0 && p < RB_MAXP) RBR.drop_at[p] = f;
        return true;
    }
    if (line[0] != 'I' || line[1] != ' ') return false;
    if (!RBR.any) { for (int p = 0; p < RB_MAXP; p++) RBR.drop_at[p] = -1; RBR.any = true; }
    long long f; int p, used = 0;
    if (sscanf(line + 2, "%lld %d %n", &f, &p, &used) < 2 || f < 0 || p < 0 || p >= RB_MAXP || f > (1ll << 26)) return true;
    if (f >= RBR.frames) {
        int64_t nf = RBR.frames ? RBR.frames : 4096; while (nf <= f) nf *= 2;
        RBR.sz = (uint8_t *)realloc(RBR.sz, (size_t)nf * RB_MAXP); RBR.d = (uint8_t *)realloc(RBR.d, (size_t)nf * RB_MAXP * ARC_NET_INPUT_MAX);
        if (!RBR.sz || !RBR.d) { fprintf(stderr, "ARC fatal out of memory (replay inputs)\n"); exit(3); }
        memset(RBR.sz + RBR.frames * RB_MAXP, 0xFF, (size_t)(nf - RBR.frames) * RB_MAXP);
        memset(RBR.d + (size_t)RBR.frames * RB_MAXP * ARC_NET_INPUT_MAX, 0, (size_t)(nf - RBR.frames) * RB_MAXP * ARC_NET_INPUT_MAX);
        RBR.frames = nf;
    }
    const char *h = line + 2 + used; uint8_t *d = RBR.d + (size_t)(f * RB_MAXP + p) * ARC_NET_INPUT_MAX; int n = 0;
    while (n < ARC_NET_INPUT_MAX && isxdigit((unsigned char)h[0]) && isxdigit((unsigned char)h[1])) { unsigned v; sscanf(h, "%2x", &v); d[n++] = (uint8_t)v; h += 2; }
    RBR.sz[f * RB_MAXP + p] = (uint8_t)n;
    return true;
}

/* ---- dump, waits, agent commands ---- */
static void rb_dump(arc_json *j) {
    net_rb *rb = NET->rb;
    if (!rb) return;
    arc_net_rb_stats s = arc_net_stats();
    arc_json_obj_k(j, "rollback");
    arc_json_str_k(j, "kind", rb->kind == RBK_LIVE ? "live" : rb->kind == RBK_SYNCTEST ? "synctest" : rb->kind == RBK_REPLAY ? "replay" : "test");
    arc_json_int_k(j, "tick", (long long)s.tick); arc_json_int_k(j, "confirmed", (long long)s.confirmed);
    arc_json_int_k(j, "delay", s.input_delay); arc_json_int_k(j, "max_rollback", s.max_rollback); arc_json_int_k(j, "players", s.players);
    arc_json_int_k(j, "rollbacks", s.rollbacks); arc_json_int_k(j, "max_depth", s.max_depth); arc_json_num_k(j, "avg_depth", s.avg_depth);
    arc_json_int_k(j, "resim_ticks", (long long)s.resim_ticks); arc_json_int_k(j, "predicted", (long long)s.predicted); arc_json_int_k(j, "mispredicted", (long long)s.mispredicted);
    arc_json_int_k(j, "skipped", s.skipped); arc_json_int_k(j, "stalled_ms", (long long)s.stalled_ms); arc_json_int_k(j, "ping_ms", s.ping_ms);
    arc_json_num_k(j, "frame_advantage", s.frame_advantage);
    arc_json_int_k(j, "sounds_played", s.sounds_played); arc_json_int_k(j, "sounds_deduped", s.sounds_deduped);
    arc_json_int_k(j, "sounds_late", s.sounds_late); arc_json_int_k(j, "sounds_cancelled", s.sounds_cancelled);
    arc_json_int_k(j, "packets_sent", (long long)s.packets_sent); arc_json_int_k(j, "packets_received", (long long)s.packets_received);
    arc_json_int_k(j, "waits", rb->n_waiting); arc_json_int_k(j, "dropped", rb->n_dropped);
    arc_json_int_k(j, "last_hash_tick", (long long)s.last_hash_tick); arc_json_str_k(j, "last_hash", arc_fmt("%016llx", (unsigned long long)s.last_hash));
    arc_json_int_k(j, "desync_tick", (long long)s.desync_tick);
    if (rb->online) {   /* 0.10.4: through a relay room; how each peer is reached */
        arc_json_obj_k(j, "online");
        arc_json_int_k(j, "hub", rb->hub); arc_json_bool_k(j, "relay_only", g_rb_relay_only);
        arc_json_arr_k(j, "peers");
        for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p) && p != rb->local && (rb_is_hub(rb) || p == rb->hub)) {
            arc_json_obj(j); arc_json_int_k(j, "player", p); arc_json_str_k(j, "path", rb->rt[p].direct && !g_rb_relay_only ? "direct" : "relay");
            arc_json_int_k(j, "sent_direct", (long long)rb->rt[p].sent_direct); arc_json_int_k(j, "sent_relayed", (long long)rb->rt[p].sent_relayed); arc_json_end(j);
        }
        arc_json_end(j);
        arc_json_end(j);
    }
    arc_json_end(j);
}
static bool rb_wait_cond(int kind, long long arg) {
    net_rb *rb = NET->rb;
    switch (kind) {
    case W_DESYNC: return rb && (rb->desync_reported || rb->synctest_failed);
    case W_TICK: return rb && rb->armed && (rb->kind == RBK_REPLAY ? arc_net_match_tick() + 1 : rb->cur) >= arg;
    case W_CONFIRMED:                         /* confirmed here, and every other machine has acknowledged what it needs from here */
        if (!rb || !rb->armed || rb->final_ < arg || rb->rollback_to != RB_NONE) return false;
        if (rb->kind == RBK_LIVE || rb->kind == RBK_TEST)
            for (int q = 0; q < RB_MAXP; q++) if (rb->peer[q].on)
                for (int p = 0; p < RB_MAXP; p++) {
                    if (!rb_in_match(rb, p) || p == q || rb->drop_at[p] >= 0) continue;
                    if (!rb_is_hub(rb) && p != rb->local) continue;
                    if (rb->peer[q].has[p] < arg) return false;
                }
        return true;
    case W_WAITING: return rb && rb->n_waiting > 0;
    case W_RESUMED: return rb && rb->n_resumed > 0;
    case W_DROPPED: return rb && rb->n_dropped > 0;
    case W_ALONE:                             /* every other player has left the match */
        if (!rb) return false;
        for (int p = 0; p < RB_MAXP; p++) if (rb_in_match(rb, p) && p != rb->local && rb->drop_at[p] < 0) return false;
        return true;
    default: return false;
    }
}
static int rb_hex(const char *h, uint8_t *out) {
    int n = 0;
    if (!strcmp(h, "-")) return 0;
    while (n < ARC_NET_INPUT_MAX && isxdigit((unsigned char)h[0]) && isxdigit((unsigned char)h[1])) { unsigned v; sscanf(h, "%2x", &v); out[n++] = (uint8_t)v; h += 2; }
    return *h ? -1 : n;
}
static void rb_print_stats(void) {
    arc_net_rb_stats s = arc_net_stats();
    printf("ARC net rollback active=%d tick=%lld confirmed=%lld delay=%d max_rollback=%d rollbacks=%d max_depth=%d avg_depth=%.2f resim=%lld predicted=%lld mispredicted=%lld "
           "skipped=%d stalled_ms=%lld ping=%d advantage=%.2f sounds=%d/%d/%d/%d packets=%lld/%lld hash_tick=%lld hash=%016llx desync=%lld\n",
           s.active, (long long)s.tick, (long long)s.confirmed, s.input_delay, s.max_rollback, s.rollbacks, s.max_depth, (double)s.avg_depth, (long long)s.resim_ticks,
           (long long)s.predicted, (long long)s.mispredicted, s.skipped, (long long)s.stalled_ms, s.ping_ms, (double)s.frame_advantage,
           s.sounds_played, s.sounds_deduped, s.sounds_late, s.sounds_cancelled, (long long)s.packets_sent, (long long)s.packets_received,
           (long long)s.last_hash_tick, (unsigned long long)s.last_hash, (long long)s.desync_tick);
}
static void rb_synctest_start(int n, int players) {
    net_machine *N = NET;
    if (N->mode != MODE_OFFLINE || N->replay) { printf("ARC error net synctest: offline only (not in a session or a replay)\n"); return; }
    if (N->rb) { rb_free(N->rb); N->rb = NULL; }
    net_rb *rb = rb_new(RBK_SYNCTEST);
    if (!rb->hooks && (!G.app.save_state || !G.app.load_state)) { printf("ARC error net synctest: the game has no save_state/load_state\n"); rb_free(rb); return; }
    rb->synctest = arc_clampi(n, 1, RB_SLOTS - 2); rb->window = rb->synctest;
    rb->seed = g_seed; rb->start_tick = G.ticks; rb->local = 0;     /* (a command runs between ticks: the next one is match tick 0) */
    players = arc_clampi(players, 1, RB_MAXP);
    rb->mask = (uint8_t)((1u << players) - 1); rb->np = players;
    for (int p = 0; p < players; p++) { for (int64_t f = 0; f < rb->delay; f++) rb_store(rb, p, f, NULL, 0); rb->have[p] = rb->delay; }
    rb->local_next = rb->delay;
    rb->armed = true; N->rb = rb;
    printf("ARC ok net synctest %d players=%d (every tick rolls back %d ticks and compares the state)\n", rb->synctest, players, rb->synctest);
}
static bool rb_agent(const char *sub, int argc, const char **argv, const char *rest) {
    net_machine *N = NET; net_rb *rb = N->rb; (void)rest;
    if (!strcmp(sub, "mode")) {
        const char *m = argc > 2 ? argv[2] : "";
        if (!strcmp(m, "rollback")) N->cfg_mode = ARC_NET_ROLLBACK; else if (!strcmp(m, "turns")) N->cfg_mode = ARC_NET_TURNS;
        else { printf("ARC error net mode rollback|turns\n"); return true; }
        if ((N->mode == MODE_HOST || N->online) && !N->V.started) { N->desc.mode = N->cfg_mode; if (N->desc.mode == ARC_NET_ROLLBACK && N->desc.max_players > RB_MAXP) N->desc.max_players = RB_MAXP; }
        printf("ARC ok net mode %s\n", m);
    } else if (!strcmp(sub, "delay")) {
        if (argc > 2) arc_net_set_input_delay(atoi(argv[2]));
        printf("ARC ok net delay %d\n", arc_net_input_delay());
    } else if (!strcmp(sub, "rollback")) {
        if (argc > 2) { N->cfg_window = arc_clampi(atoi(argv[2]), 0, ARC_NET_MAX_ROLLBACK); if (rb && rb->kind == RBK_LIVE && rb->window) rb->window = N->cfg_window; }
        printf("ARC ok net rollback %d\n", rb ? rb->window : N->cfg_window >= 0 ? N->cfg_window : N->desc.max_rollback > 0 ? N->desc.max_rollback : 8);
    } else if (!strcmp(sub, "input")) {
        if (!rb || rb->kind == RBK_REPLAY) { printf("ARC error net input: no rollback match here yet (start it first)\n"); return true; }
        int p = rb->local; const char *v = argc > 2 ? argv[2] : "";
        if (argc > 3) { p = atoi(argv[2]); v = argv[3]; }
        if (p < 0 || p >= RB_MAXP || (p != rb->local && rb->kind != RBK_SYNCTEST)) { printf("ARC error net input: only this machine's player (others in a synctest)\n"); return true; }
        if (!strcmp(v, "off")) { rb->script_on[p] = false; printf("ARC ok net input off\n"); return true; }
        uint8_t b[ARC_NET_INPUT_MAX]; int n = rb_hex(v, b);
        if (n < 0) { printf("ARC error net input HEX (up to %d bytes, e.g. 0105) or off\n", ARC_NET_INPUT_MAX); return true; }
        memset(rb->script[p], 0, sizeof rb->script[p]); memcpy(rb->script[p], b, (size_t)n); rb->script_n[p] = (uint8_t)n; rb->script_on[p] = true;
        printf("ARC ok net input %d %s\n", p, v);
    } else if (!strcmp(sub, "stop_at")) {
        if (!rb) { printf("ARC error net stop_at: no rollback match here yet\n"); return true; }
        rb->stop_at = argc > 2 ? atoll(argv[2]) : -1; if (rb->stop_at <= 0) rb->stop_at = -1;
        printf("ARC ok net stop_at %lld\n", (long long)rb->stop_at);
    } else if (!strcmp(sub, "stats")) { rb_print_stats(); printf("ARC ok net stats\n"); }
    else if (!strcmp(sub, "direct")) {   /* 0.10.4: hole punching on / off (off: every datagram goes through the relay) */
        g_rb_relay_only = argc > 2 && !strcmp(argv[2], "off");
        printf("ARC ok net direct %s\n", g_rb_relay_only ? "off" : "on");
    }
    else if (!strcmp(sub, "synctest")) { rb_synctest_start(argc > 2 ? atoi(argv[2]) : 2, argc > 3 ? atoi(argv[3]) : 1); }
    else return false;
    fflush(stdout);
    return true;
}
/* --net-synctest N: start checking once the game has started (after init) */
void arc__rb_startup(void) {
    if (arc_arg_flag("net-relay-only")) g_rb_relay_only = true;   /* 0.10.4 */
    const char *v = arc_arg_value("net-synctest");
    if (v) rb_synctest_start(atoi(v), arc_arg_value("net-synctest-players") ? atoi(arc_arg_value("net-synctest-players")) : 1);
}

/* ======================================================================== */
/* Self-test hooks: machines with their own little game                     */
/* ======================================================================== */
void arc__rb_test_hooks(const void *hooks) { NET->rb_hooks = hooks; }
/* one 1/60 s frame of a hook machine (the caller has made it current and set the test clock) */
void arc__rb_test_step(void) {
    arc__net_frame();
    arc__net_tick_begin();
    net_rb *rb = NET->rb;
    if (!rb || !rb->armed || rb->kind != RBK_TEST) return;
    int go = arc__rb_before_tick();
    if (go) return;
    arc__rb_new_tick();
    rb_hook_tick(rb, rb->cur);
    arc__rb_after_tick();
}
int64_t arc__rb_test_tick(void) { return NET->rb ? NET->rb->cur : -1; }
int64_t arc__rb_test_final(void) { return NET->rb ? NET->rb->final_ : -1; }
void arc__rb_test_stop_at(int64_t t) { if (NET->rb) NET->rb->stop_at = t; }
uint64_t arc__rb_test_hash_at(int64_t f) {
    net_rb *rb = NET->rb; if (!rb) return 0;
    rb_hrec *r = &rb->hr[rb_hidx(rb, f) % RB_HRING]; return r->frame == f && r->have ? r->mine : 0;
}
