/* ARC 0.8 multiplayer: turn-based sessions where game commands are the network messages.
   On a LAN the host's game process is the relay: it numbers every command and sends it to everyone (itself included);
   every machine applies them in order at the start of a tick through the game's command() callback. Online (0.8.5) the
   stand-alone arc_relay (tools/arc_relay) does the host's part for rooms with short codes, and every player's game is a
   client of it, over WebSocket (desktop: sockets; web: the browser's WebSocket) or raw TCP. See include/arc.h
   ("Multiplayer (0.8)"), docs/MULTIPLAYER.md and docs/RELAY.md.

   Wire protocol (src/arc_net_core.h, shared with the relay): each message is u32 length (of what follows), u8 type,
   payload. Strings are u16 length + bytes. The first message from a client is HELLO with the protocol version; the host
   answers WELCOME or REFUSE. Over WebSocket the same bytes travel in binary frames (a byte stream: frame boundaries mean
   nothing). LAN discovery: UDP datagrams to port ARC_NET_PORT + 1 ("ARCF" question, "ARCH" answer).
   Everything runs on the main thread: sockets are non-blocking and polled once a frame (arc__net_frame).
   A machine's whole session state is one net_machine (NET), so the self-test can run several in one process over an
   in-process transport (memory links) as well as real sockets. */

#define NCORE_RNG arc_rng
#define NCORE_RNG_SEED(r, s) arc_rng_seed(&(r), (s), 0x4D41544348ull)   /* "MATCH" */
#include "arc_net_core.h"
#define NET_MAXP ARC_NET_MAX_PLAYERS
#define NET_DISC_PORT_OFS 1
#define NET_NO_TICK (~(uint64_t)0)
typedef char net_maxp_check[(NCORE_MAXP == ARC_NET_MAX_PLAYERS && NCORE_FREE == ARC_NET_FREE) ? 1 : -1];

/* ======================================================================== */
/* Sockets (BSD on Linux/macOS, Winsock loaded at run time on Windows)      */
/* ======================================================================== */
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define NET_SOCKETS 0
typedef int net_sock;
#define NET_BAD (-1)
static void ns_close(net_sock s) { (void)s; }
#else
#define NET_SOCKETS 1
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET net_sock;
#define NET_BAD INVALID_SOCKET
/* ws2_32.dll is loaded when a session starts, so no game needs to link it (MSVC, MinGW, hot-reload builds alike) */
static struct {
    bool tried, ok;
    int (WSAAPI *Startup)(WORD, LPWSADATA);
    SOCKET (WSAAPI *socket_)(int, int, int);
    int (WSAAPI *closesocket_)(SOCKET);
    int (WSAAPI *bind_)(SOCKET, const struct sockaddr *, int);
    int (WSAAPI *listen_)(SOCKET, int);
    SOCKET (WSAAPI *accept_)(SOCKET, struct sockaddr *, int *);
    int (WSAAPI *connect_)(SOCKET, const struct sockaddr *, int);
    int (WSAAPI *send_)(SOCKET, const char *, int, int);
    int (WSAAPI *recv_)(SOCKET, char *, int, int);
    int (WSAAPI *sendto_)(SOCKET, const char *, int, int, const struct sockaddr *, int);
    int (WSAAPI *recvfrom_)(SOCKET, char *, int, int, struct sockaddr *, int *);
    int (WSAAPI *ioctlsocket_)(SOCKET, long, u_long *);
    int (WSAAPI *setsockopt_)(SOCKET, int, int, const char *, int);
    int (WSAAPI *lasterr_)(void);
    int (WSAAPI *getaddrinfo_)(const char *, const char *, const struct addrinfo *, struct addrinfo **);
    void (WSAAPI *freeaddrinfo_)(struct addrinfo *);
} NSW;
static bool ns_load(HMODULE h, void *dst, const char *name) { FARPROC f = GetProcAddress(h, name); memcpy(dst, &f, sizeof f); return f != NULL; }
static bool ns_init(void) {
    if (NSW.tried) return NSW.ok;
    NSW.tried = true;
    HMODULE h = LoadLibraryA("ws2_32.dll");
    if (!h) return false;
    bool ok = ns_load(h, &NSW.Startup, "WSAStartup") && ns_load(h, &NSW.socket_, "socket") && ns_load(h, &NSW.closesocket_, "closesocket") &&
              ns_load(h, &NSW.bind_, "bind") && ns_load(h, &NSW.listen_, "listen") && ns_load(h, &NSW.accept_, "accept") &&
              ns_load(h, &NSW.connect_, "connect") && ns_load(h, &NSW.send_, "send") && ns_load(h, &NSW.recv_, "recv") &&
              ns_load(h, &NSW.sendto_, "sendto") && ns_load(h, &NSW.recvfrom_, "recvfrom") && ns_load(h, &NSW.ioctlsocket_, "ioctlsocket") &&
              ns_load(h, &NSW.setsockopt_, "setsockopt") && ns_load(h, &NSW.lasterr_, "WSAGetLastError") &&
              ns_load(h, &NSW.getaddrinfo_, "getaddrinfo") && ns_load(h, &NSW.freeaddrinfo_, "freeaddrinfo");
    WSADATA wd;
    NSW.ok = ok && NSW.Startup(MAKEWORD(2, 2), &wd) == 0;
    return NSW.ok;
}
static void ns_close(net_sock s) { if (s != NET_BAD) NSW.closesocket_(s); }
static int  ns_err(void) { return NSW.lasterr_(); }
#define NS_WOULDBLOCK(e) ((e) == 10035)                    /* WSAEWOULDBLOCK */
#define NS_CONNECTING(e) ((e) == 10035 || (e) == 10036 || (e) == 10037 || (e) == 10022)   /* +INPROGRESS, ALREADY, EINVAL */
#define NS_CONNECTED(e)  ((e) == 10056)                    /* WSAEISCONN */
#define ns_socket NSW.socket_
#define ns_bind NSW.bind_
#define ns_listen NSW.listen_
#define ns_setsockopt NSW.setsockopt_
typedef int ns_len;
static bool ns_nonblock(net_sock s) { u_long one = 1; return NSW.ioctlsocket_(s, (long)FIONBIO, &one) == 0; }
#define NS_SEND_FLAGS 0
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
typedef int net_sock;
#define NET_BAD (-1)
static bool ns_init(void) { return true; }
static void ns_close(net_sock s) { if (s >= 0) close(s); }
static int  ns_err(void) { return errno; }
#define NS_WOULDBLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK || (e) == EINTR)
#define NS_CONNECTING(e) ((e) == EINPROGRESS || (e) == EALREADY || (e) == EINTR)
#define NS_CONNECTED(e)  ((e) == EISCONN)
#define ns_socket socket
#define ns_bind bind
#define ns_listen listen
#define ns_setsockopt setsockopt
typedef socklen_t ns_len;
static bool ns_nonblock(net_sock s) { int f = fcntl(s, F_GETFL, 0); return f >= 0 && fcntl(s, F_SETFL, f | O_NONBLOCK) == 0; }
#if defined(MSG_NOSIGNAL)
#define NS_SEND_FLAGS MSG_NOSIGNAL
#else
#define NS_SEND_FLAGS 0
#endif
#endif
static struct sockaddr_in ns_addr(uint32_t ip, int port) {   /* ip: a.b.c.d as (a << 24) | ... */
    struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    unsigned char *pp = (unsigned char *)&sa.sin_port, *ap = (unsigned char *)&sa.sin_addr;
    pp[0] = (unsigned char)(port >> 8); pp[1] = (unsigned char)port;
    ap[0] = (unsigned char)(ip >> 24); ap[1] = (unsigned char)(ip >> 16); ap[2] = (unsigned char)(ip >> 8); ap[3] = (unsigned char)ip;
    return sa;
}
static void ns_from_addr(const struct sockaddr_in *sa, uint32_t *ip, int *port) {
    const unsigned char *pp = (const unsigned char *)&sa->sin_port, *ap = (const unsigned char *)&sa->sin_addr;
    *port = (pp[0] << 8) | pp[1];
    *ip = ((uint32_t)ap[0] << 24) | ((uint32_t)ap[1] << 16) | ((uint32_t)ap[2] << 8) | ap[3];
}
static void ns_opts(net_sock s) {
    int one = 1;
    ns_setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, (ns_len)sizeof one);
}
static net_sock ns_tcp_listen(int port) {
    net_sock s = ns_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == NET_BAD) return NET_BAD;
#if !defined(_WIN32)
    int one = 1; ns_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, (ns_len)sizeof one);   /* restart without waiting out TIME_WAIT */
#endif
    struct sockaddr_in sa = ns_addr(0, port);
    if (ns_bind(s, (const struct sockaddr *)&sa, (ns_len)sizeof sa) != 0 || ns_listen(s, 8) != 0 || !ns_nonblock(s)) { ns_close(s); return NET_BAD; }
    return s;
}
static net_sock ns_accept(net_sock l) {
    struct sockaddr_in sa; ns_len n = (ns_len)sizeof sa;
#if defined(_WIN32)
    net_sock s = NSW.accept_(l, (struct sockaddr *)&sa, &n);
#else
    net_sock s = accept(l, (struct sockaddr *)&sa, &n);
#endif
    if (s == NET_BAD) return NET_BAD;
    if (!ns_nonblock(s)) { ns_close(s); return NET_BAD; }
    ns_opts(s);
    return s;
}
/* connect without blocking: calling connect again tells how it went (done / still going / failed), with no select or poll */
static int ns_connect_step(net_sock s, uint32_t ip, int port) {   /* 1 connected, 0 in progress, -1 failed */
    struct sockaddr_in sa = ns_addr(ip, port);
#if defined(_WIN32)
    int r = NSW.connect_(s, (const struct sockaddr *)&sa, (int)sizeof sa);
#else
    int r = connect(s, (const struct sockaddr *)&sa, (ns_len)sizeof sa);
#endif
    if (r == 0) return 1;
    int e = ns_err();
    if (NS_CONNECTED(e)) return 1;
    if (NS_CONNECTING(e)) return 0;
    return -1;
}
static net_sock ns_tcp_connect(uint32_t ip, int port) {
    net_sock s = ns_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == NET_BAD) return NET_BAD;
    if (!ns_nonblock(s)) { ns_close(s); return NET_BAD; }
    ns_opts(s);
    if (ns_connect_step(s, ip, port) < 0) { ns_close(s); return NET_BAD; }
    return s;
}
static int ns_send(net_sock s, const void *p, size_t n) {   /* bytes sent (0: would block), -1 error */
    if (n > (1u << 30)) n = 1u << 30;
#if defined(_WIN32)
    int r = NSW.send_(s, (const char *)p, (int)n, 0);
#else
    ssize_t r = send(s, p, n, NS_SEND_FLAGS);
#endif
    if (r >= 0) return (int)r;
    return NS_WOULDBLOCK(ns_err()) ? 0 : -1;
}
static int ns_recv(net_sock s, void *p, size_t n) {         /* bytes (0: nothing now), -1 closed or failed */
#if defined(_WIN32)
    int r = NSW.recv_(s, (char *)p, (int)n, 0);
#else
    ssize_t r = recv(s, p, n, 0);
#endif
    if (r > 0) return (int)r;
    if (r == 0) return -1;
    return NS_WOULDBLOCK(ns_err()) ? 0 : -1;
}
static net_sock ns_udp(int port, bool broadcast) {
    net_sock s = ns_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == NET_BAD) return NET_BAD;
    int one = 1;
    ns_setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, (ns_len)sizeof one);   /* several hosts on one machine */
    if (broadcast) ns_setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char *)&one, (ns_len)sizeof one);
    struct sockaddr_in sa = ns_addr(0, port);
    if (ns_bind(s, (const struct sockaddr *)&sa, (ns_len)sizeof sa) != 0 || !ns_nonblock(s)) { ns_close(s); return NET_BAD; }
    return s;
}
static void ns_udp_send(net_sock s, uint32_t ip, int port, const void *p, size_t n) {
    struct sockaddr_in sa = ns_addr(ip, port);
#if defined(_WIN32)
    NSW.sendto_(s, (const char *)p, (int)n, 0, (const struct sockaddr *)&sa, (int)sizeof sa);
#else
    sendto(s, p, n, 0, (const struct sockaddr *)&sa, (ns_len)sizeof sa);
#endif
}
static int ns_udp_recv(net_sock s, void *p, size_t n, uint32_t *ip, int *port) {
    struct sockaddr_in sa; ns_len sl = (ns_len)sizeof sa;
#if defined(_WIN32)
    int r = NSW.recvfrom_(s, (char *)p, (int)n, 0, (struct sockaddr *)&sa, &sl);
#else
    ssize_t r = recvfrom(s, p, n, 0, (struct sockaddr *)&sa, &sl);
#endif
    if (r <= 0) return 0;
    ns_from_addr(&sa, ip, port);
    return (int)r;
}
static bool ns_resolve(const char *host, uint32_t *ip) {
    struct addrinfo hints, *res = NULL; memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
#if defined(_WIN32)
    if (NSW.getaddrinfo_(host, NULL, &hints, &res) != 0 || !res) return false;
#else
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
#endif
    int port; ns_from_addr((const struct sockaddr_in *)res->ai_addr, ip, &port);
#if defined(_WIN32)
    NSW.freeaddrinfo_(res);
#else
    freeaddrinfo(res);
#endif
    return true;
}
#endif /* sockets */

/* "a.b.c.d[:port]", "name[:port]", "mem:port" (in-process, self-test) */
static bool net_parse_addr(const char *addr, char *host, size_t hn, int *port) {
    *port = ARC_NET_PORT;
    if (!addr || !*addr) return false;
    const char *colon = strrchr(addr, ':');
    size_t n = colon ? (size_t)(colon - addr) : strlen(addr);
    if (n >= hn) return false;
    memcpy(host, addr, n); host[n] = 0;
    if (colon) { int p = atoi(colon + 1); if (p <= 0 || p > 65535) return false; *port = p; }
    if (!n) snprintf(host, hn, "127.0.0.1");
    return true;
}
#if NET_SOCKETS
static bool net_dotted(const char *s, uint32_t *ip) {
    unsigned a, b, c, d; char tail;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return false;
    *ip = (a << 24) | (b << 16) | (c << 8) | d; return true;
}
#endif

/* ======================================================================== */
/* Session state                                                            */
/* ======================================================================== */
typedef struct { uint32_t seq; int8_t from; uint8_t flags, kind, backlog; uint32_t off, len; uint64_t tick; } net_entry;
enum { MODE_OFFLINE, MODE_HOST, MODE_CLIENT };
enum { C_FREE, C_CONNECTING, C_HELLO, C_LIVE, C_UPGRADE };   /* (0.8.5 C_UPGRADE: a WebSocket handshake under way) */
typedef struct net_memlink { net_buf q[2]; bool closed[2]; } net_memlink;
typedef struct {
    int state, kind;                                   /* kind 1 socket, 2 memory, 3 the browser's WebSocket (0.8.5) */
    net_sock fd; net_memlink *mem; int side;
    uint32_t ip; int port;
    net_buf in, wire, simq;                            /* received; ready for the transport; held back by --net-sim */
    double last_recv, next_ping, opened, sim_last; int ping_ms, sim_losses; bool hello_sent, peer_closed;
    /* 0.8.5 WebSocket over a socket (a client of the relay): frames out and in, the handshake's expected accept value */
    bool ws, ws_open, ws_bad; net_buf wsout, wsin; char ws_accept[32]; int web_id;
} net_conn;
struct net_rb;                                         /* 0.8.1 rollback match (arc_rollback.c) */
typedef struct net_machine {
    int mode, status; char message[256];
    bool touched, memory;                              /* memory: in-process links (self-test) */
    arc_net_desc desc; char d_name[48], d_game[48], d_version[32];
    int local; char my_name[32]; uint64_t token, session;
    /* host */
    net_sock lsock, dsock; int port;
    net_conn conns[NET_MAXP];                          /* by player (host); client: conns[0] = the link to the host */
    net_conn pend[NET_MAXP];                           /* host: accepted, no HELLO yet */
    net_view AV;                                       /* host: after every command it has numbered */
    uint64_t tokens[NET_MAXP];
    double next_pings;
    /* every machine */
    net_view V;                                        /* after every command applied here */
    net_entry *log; int log_n, log_cap; net_buf text;
    int napplied; uint32_t backlog_end;
    int cur_from; bool applying, cur_backlog;
    int inflight;                                      /* client: commands sent, not yet back */
    bool catching;                                     /* client: receiving the backlog (until CAUGHT_UP) */
    bool desc_set;                                     /* arc_net_setup was called */
    net_hrow *hring; uint64_t last_hash; uint32_t last_hash_seq;
    uint32_t desync_seq, desync_upto; bool desync_pending, desync_done; uint64_t desync_hashes[NET_MAXP]; uint8_t desync_have;
    net_buf sbuf;                                      /* the hashed bytes */
    int ping_table[NET_MAXP];
    arc_net_ev evq[64]; int ev_head, ev_n;
    /* client */
    char join_addr[340]; uint32_t join_ip; int join_port; double retry_at, retry_until; bool auto_retry, sim_dropped, was_connected;
    uint32_t sync_id, sync_target; bool sync_known;
    /* discovery */
    net_sock fsock; double find_until, find_next; arc_net_game found[16]; int nfound;
    /* --net-sim (0.8.1: dup, freeze_at, freeze_ms for rollback datagrams) */
    struct { bool on; double lag, jitter, loss, timeout, dup, freeze_ms; uint64_t drop_at, rejoin_at, freeze_at; arc_rng rng; } sim;
    /* scripts: net wait / net sync */
    struct { int kind; long long arg; char name[24]; double deadline; } wait;
    /* recording (input.rec.net) and replays of it */
    char rec_path[1100]; FILE *rec; bool rec_failed; bool replay, replay_session; int replay_local;
    double (*clock)(void);
    struct { bool init, searched; char name[32], addr[64]; int sel; char code[16]; } lobby;   /* (0.8.5: code) */
    /* 0.8.1 rollback: the match, test hooks, and settings from flags / agent commands (-2 / -1 = from the desc) */
    struct net_rb *rb; const void *rb_hooks; int cfg_delay, cfg_window, cfg_mode;
    /* 0.8.5 online: a client of a relay room (the room's host is its player 0); the relay list (desc, --net-relay), the
       URL in use, the room code, what HELLO asks for (create / join), the WebSocket masks' generator */
    bool online; int online_op; char room[8]; char d_relay[512], flag_relay[512], relay_url[300]; ncore_url rurl; uint64_t ws_rng;
} net_machine;
static net_machine g_net0;
static net_machine *NET = &g_net0;
static bool g_net_verbose; static char g_net_report[512] = "net_desync";

static double net_now(void) { return NET->clock ? NET->clock() : arc__plat_time(); }
/* 0.8.1 rollback (arc_rollback.c) */
static void rb_arm(uint64_t seed);
static void rb_drop_player(int p, const char *why);
static void rb_lost_host(const char *why);
static void rb_end(void);
static void rb_frame(void);
static bool rb_agent(const char *sub, int argc, const char **argv, const char *rest);
static void rb_dump(arc_json *j);
static bool rb_wait_cond(int kind, long long arg);
static bool rb_replay_line(const char *line);
static void rb_free(struct net_rb *rb);
static bool net_rollback_mode(void);
static bool net_quiet(void) { return g_resim; }
static void net_out(const char *fmt, ...) {
    if (net_quiet()) return;
    va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); fflush(stdout);
}
static bool net_session(void) { return NET->mode != MODE_OFFLINE || (NET->replay && NET->replay_session); }
static void view_reset(net_view *v, bool session, uint64_t rng_seed) {
    memset(v, 0, sizeof *v);
    v->owner = session ? -1 : 0; v->turn = session ? 0 : 1;
    arc_rng_seed(&v->rng, rng_seed, 0x6E6574u);
}
static const char *entry_text(const net_entry *e) { return (const char *)NET->text.b + e->off; }
static void log_clear(void) { NET->log_n = 0; NET->text.n = 0; NET->napplied = 0; NET->backlog_end = 0; }
static net_entry *log_append(uint32_t seq, int from, int flags, int kind, const char *text, uint64_t tick) {
    net_machine *N = NET;
    if (N->log_n == N->log_cap) {
        int nc = N->log_cap ? N->log_cap * 2 : 4096;
        net_entry *nl = (net_entry *)realloc(N->log, sizeof(net_entry) * (size_t)nc);
        if (!nl) { fprintf(stderr, "ARC fatal out of memory (net log)\n"); exit(3); }
        N->log = nl; N->log_cap = nc;
    }
    size_t len = text ? strlen(text) : 0; if (len > NET_TEXT_MAX) len = NET_TEXT_MAX;
    if (!N->text.cap) nb_reserve(&N->text, 1 << 20);
    net_entry *e = &N->log[N->log_n++];
    e->seq = seq; e->from = (int8_t)from; e->flags = (uint8_t)flags; e->kind = (uint8_t)kind; e->backlog = 0;
    e->off = (uint32_t)N->text.n; e->len = (uint32_t)len; e->tick = tick;
    nb_put(&N->text, text ? text : "", len); uint8_t z = 0; nb_put(&N->text, &z, 1);
    N->touched = true;
    return e;
}
/* the view as bytes, field by field (no padding): hashed with the game's state, and kept in ARC's state snapshots */
static void view_write(arc_writer *w, const net_view *v) {
    uint8_t st = (uint8_t)((v->started ? 1 : 0) | (v->simul ? 2 : 0)); arc_write(w, &st, 1); arc_write(w, &v->seed, 8); arc_write(w, &v->owner, 4); arc_write(w, &v->turn, 4);
    arc_write(w, &v->rng.state, 8); arc_write(w, &v->rng.inc, 8);
    if (v->simul) arc_write(w, &v->done, 1);          /* 0.8.5 simultaneous turns (absent otherwise: the same bytes as 0.8.0) */
    for (int i = 0; i < NET_MAXP; i++) {
        uint8_t f = (uint8_t)((v->p[i].used ? 1 : 0) | (v->p[i].connected ? 2 : 0) | (v->p[i].ready ? 4 : 0) | (v->p[i].in_match ? 8 : 0));
        arc_write(w, &f, 1); arc_write(w, v->p[i].name, sizeof v->p[i].name);
    }
}
static bool view_read(arc_reader *r, net_view *v) {
    memset(v, 0, sizeof *v); uint8_t st = 0;
    arc_read(r, &st, 1); arc_read(r, &v->seed, 8); arc_read(r, &v->owner, 4); arc_read(r, &v->turn, 4);
    arc_read(r, &v->rng.state, 8); arc_read(r, &v->rng.inc, 8); v->started = (st & 1) != 0; v->simul = (st & 2) != 0;
    if (v->simul) arc_read(r, &v->done, 1);
    for (int i = 0; i < NET_MAXP; i++) {
        uint8_t f = 0; arc_read(r, &f, 1); arc_read(r, v->p[i].name, sizeof v->p[i].name); v->p[i].name[sizeof v->p[i].name - 1] = 0;
        v->p[i].used = f & 1; v->p[i].connected = (f & 2) != 0; v->p[i].ready = (f & 4) != 0; v->p[i].in_match = (f & 8) != 0;
    }
    return !r->fail;
}

/* ---- events ---- */
static void ev_push(int type, int player, uint32_t seq, bool sequenced, const char *text) {
    net_machine *N = NET;
    arc_net_ev e; memset(&e, 0, sizeof e);
    e.type = type; e.player = player; e.seq = seq; e.sequenced = sequenced; snprintf(e.text, sizeof e.text, "%s", text ? text : "");
    int at = (N->ev_head + N->ev_n) % 64;
    N->evq[at] = e;
    if (N->ev_n < 64) N->ev_n++; else N->ev_head = (N->ev_head + 1) % 64;
    if (G.app.net_event) G.app.net_event(&e);
}
bool arc_net_event(arc_net_ev *ev) {
    net_machine *N = NET;
    if (!N->ev_n) { if (ev) memset(ev, 0, sizeof *ev); return false; }
    if (ev) *ev = N->evq[N->ev_head];
    N->ev_head = (N->ev_head + 1) % 64; N->ev_n--;
    return true;
}

/* ======================================================================== */
/* Connections: sockets or memory links, with the --net-sim layer on sends  */
/* ======================================================================== */
static void conn_reset(net_conn *c) {
    nb_free(&c->in); nb_free(&c->wire); nb_free(&c->simq); nb_free(&c->wsout); nb_free(&c->wsin);
    memset(c, 0, sizeof *c); c->fd = NET_BAD; c->ping_ms = -1; c->web_id = -1;
}
static void web_ws_close_id(int id);
static void conn_close(net_conn *c) {
    if (c->kind == 1) { if (c->fd != NET_BAD) ns_close(c->fd); }
    else if (c->kind == 3) web_ws_close_id(c->web_id);
    else if (c->kind == 2 && c->mem) {
        c->mem->closed[c->side] = true;
        if (c->mem->closed[0] && c->mem->closed[1]) { nb_free(&c->mem->q[0]); nb_free(&c->mem->q[1]); free(c->mem); }
    }
    conn_reset(c);
}
static void conn_queue_now(net_conn *c) {      /* the message in g_msg, past --net-sim: the last words before closing a link */
    m_finish();
    if (c->simq.n) {                           /* (after what is still held back, in order) */
        size_t used = 0;
        while (used + sizeof(double) + 4 <= c->simq.n) { uint32_t n; memcpy(&n, c->simq.b + used + sizeof(double), 4); nb_put(&c->wire, c->simq.b + used + sizeof(double) + 4, n); used += sizeof(double) + 4 + n; }
        c->simq.n = 0;
    }
    nb_put(&c->wire, g_msg.b, g_msg.n);
}
static void conn_queue(net_conn *c) {          /* the message in g_msg */
    m_finish();
    net_machine *N = NET;
    if (!N->sim.on) { nb_put(&c->wire, g_msg.b, g_msg.n); return; }
    /* lag and jitter delay each message; "loss" stalls it as TCP's retransmission would (200 ms, doubling), and too many
       losses in a row break the link. Messages never overtake each other (TCP keeps order). */
    double now = net_now(), rel = now + N->sim.lag * 0.001 + N->sim.jitter * 0.001 * (double)arc_rng_float(&N->sim.rng);
    int k = 0;
    while (N->sim.loss > 0 && (double)arc_rng_float(&N->sim.rng) < N->sim.loss) { rel += 0.2 * (double)(1 << k); if (++k >= 6) break; }
    if (k >= 6) { c->sim_losses = 99; return; }   /* the link dies (seen by conn_pump) */
    if (rel < c->sim_last) rel = c->sim_last;
    c->sim_last = rel;
    uint32_t n = (uint32_t)g_msg.n;
    nb_put(&c->simq, &rel, sizeof rel); nb_put(&c->simq, &n, 4); nb_put(&c->simq, g_msg.b, n);
}
/* ---- 0.8.5 WebSocket client framing over a socket (RFC 6455: a client masks every frame) ---- */
#if NET_SOCKETS
static void ws_mask_key(uint8_t m[4]) {
    net_machine *N = NET;
    N->ws_rng ^= N->ws_rng << 13; N->ws_rng ^= N->ws_rng >> 7; N->ws_rng ^= N->ws_rng << 17; if (!N->ws_rng) N->ws_rng = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 4; i++) m[i] = (uint8_t)(N->ws_rng >> (8 * i + 8));
}
static void ws_wrap(net_conn *c, int opcode, const uint8_t *p, size_t n) {   /* one frame into wsout */
    uint8_t h[14], m[4]; ws_mask_key(m);
    size_t k = ncore_ws_header(h, opcode, n, m);
    nb_put(&c->wsout, h, k);
    size_t at = c->wsout.n; nb_put(&c->wsout, p, n); ncore_ws_mask(c->wsout.b + at, n, m);
}
static void ws_unwrap(net_conn *c) {                    /* the relay's frames: data to c->in, answers to pings, the close */
    for (;;) {
        ncore_wsf f; long k = ncore_ws_parse(c->wsin.b, c->wsin.n, (uint64_t)1 << 24, &f);
        if (k == 0) break;
        if (k < 0) { c->ws_bad = true; break; }
        uint8_t *pl = c->wsin.b + f.hdr;
        if (f.masked) ncore_ws_mask(pl, (size_t)f.len, f.mask);
        if (f.opcode == WS_BINARY || f.opcode == WS_CONT || f.opcode == WS_TEXT) nb_put(&c->in, pl, (size_t)f.len);
        else if (f.opcode == WS_PING) ws_wrap(c, WS_PONG, pl, (size_t)f.len);
        else if (f.opcode == WS_CLOSE) { if (!c->peer_closed) ws_wrap(c, WS_CLOSE, pl, f.len >= 2 ? 2 : 0); c->peer_closed = true; }
        nb_drop_front(&c->wsin, (size_t)k);
    }
}
#endif
/* ---- 0.8.5 the browser's WebSocket (web build): JS keeps each socket and the bytes it has received ---- */
#if defined(__EMSCRIPTEN__)
EM_JS(int, web_ws_open, (const char *url), {
    var u = UTF8ToString(url), S = Module.arcWS || (Module.arcWS = { next: 1, socks: {} });
    try {
        var ws = new WebSocket(u); ws.binaryType = "arraybuffer";
        var id = S.next++, e = { ws: ws, q: [], state: 0, err: "" };
        ws.onopen = function() { e.state = 1; };
        ws.onmessage = function(m) { if (m.data instanceof ArrayBuffer) e.q.push(new Uint8Array(m.data)); else e.q.push(new TextEncoder().encode(String(m.data))); };
        ws.onclose = function(ev) { e.state = 3; if (!e.err) e.err = "closed (" + ev.code + ")"; };
        ws.onerror = function() { e.err = "error"; };
        S.socks[id] = e; return id;
    } catch (x) { console.log("ARC log net websocket: " + x); return -1; }
});
EM_JS(int, web_ws_state, (int id), { var S = Module.arcWS, e = S && S.socks[id]; return e ? e.state : 3; });   /* 0 opening, 1 open, 3 closed */
EM_JS(int, web_ws_send, (int id, const uint8_t *p, int n), {
    var S = Module.arcWS, e = S && S.socks[id]; if (!e || e.state !== 1) return 0;
    try { e.ws.send(HEAPU8.slice(p, p + n)); return 1; } catch (x) { return 0; }
});
EM_JS(int, web_ws_recv, (int id, uint8_t *p, int cap), {   /* bytes copied (whole chunks, or the front of one) */
    var S = Module.arcWS, e = S && S.socks[id]; if (!e) return 0;
    var n = 0;
    while (e.q.length && n < cap) {
        var c = e.q[0], k = Math.min(c.length, cap - n);
        HEAPU8.set(c.subarray(0, k), p + n); n += k;
        if (k < c.length) e.q[0] = c.subarray(k); else e.q.shift();
    }
    return n;
});
EM_JS(void, web_ws_close, (int id), { var S = Module.arcWS, e = S && S.socks[id]; if (!e) return; try { e.ws.close(); } catch (x) {} delete S.socks[id]; });
EM_JS(int, web_page_https, (void), { return location.protocol === "https:" ? 1 : 0; });
static void web_ws_close_id(int id) { if (id >= 0) web_ws_close(id); }
static bool web_ws_pump(net_conn *c, double now) {
    int st = web_ws_state(c->web_id);
    if (st == 1 && c->wire.n) { if (web_ws_send(c->web_id, c->wire.b, (int)c->wire.n)) c->wire.n = 0; }
    for (;;) {
        nb_reserve(&c->in, 16384);
        int k = web_ws_recv(c->web_id, c->in.b + c->in.n, (int)(c->in.cap - c->in.n));
        if (k <= 0) break;
        c->in.n += (size_t)k; c->last_recv = now;
    }
    if (st == 3) c->peer_closed = true;
    return !(c->peer_closed && !c->in.n);
}
#else
static void web_ws_close_id(int id) { (void)id; }
static bool web_ws_pump(net_conn *c, double now) { (void)c; (void)now; return false; }
#endif
/* move released messages to the wire and the wire to the transport; read what arrived. false: the link is gone */
static bool conn_pump(net_conn *c) {
    if (c->sim_losses >= 99) return false;
    double now = net_now();
    size_t used = 0;
    while (used + sizeof(double) + 4 <= c->simq.n) {
        double rel; uint32_t n; memcpy(&rel, c->simq.b + used, sizeof rel); memcpy(&n, c->simq.b + used + sizeof rel, 4);
        if (rel > now) break;
        nb_put(&c->wire, c->simq.b + used + sizeof rel + 4, n); used += sizeof rel + 4 + n;
    }
    if (used) nb_drop_front(&c->simq, used);
    if (c->kind == 2) {
        net_memlink *m = c->mem; if (!m) return false;
        if (c->wire.n) { nb_put(&m->q[c->side], c->wire.b, c->wire.n); c->wire.n = 0; }
        net_buf *q = &m->q[1 - c->side];
        if (q->n) { nb_put(&c->in, q->b, q->n); q->n = 0; c->last_recv = now; }
        else if (m->closed[1 - c->side]) c->peer_closed = true;
        return !(c->peer_closed && !c->in.n);
    }
    if (c->kind == 3) return web_ws_pump(c, now);
#if NET_SOCKETS
    if (c->ws && c->ws_open && c->wire.n) ws_wrap(c, WS_BINARY, c->wire.b, c->wire.n), c->wire.n = 0;
    net_buf *out = c->ws ? &c->wsout : &c->wire, *in = c->ws ? &c->wsin : &c->in;
    while (out->n) {
        int k = ns_send(c->fd, out->b, out->n);
        if (k < 0) return false;
        if (k == 0) break;
        nb_drop_front(out, (size_t)k);
    }
    for (;;) {
        nb_reserve(in, 16384);
        int k = ns_recv(c->fd, in->b + in->n, in->cap - in->n);
        if (k < 0) { c->peer_closed = true; break; }   /* (what came before the close is still read: a REFUSE, a BYE) */
        if (k == 0) break;
        in->n += (size_t)k; c->last_recv = now;
    }
    if (c->ws && c->ws_open) ws_unwrap(c);
    if (c->ws_bad) return false;
#endif
    return !(c->peer_closed && !c->in.n);
}
/* the next whole message in c->in (consumed by msg_done) */
static bool conn_next(net_conn *c, net_rd *r, int *type, size_t *total, bool *bad) {
    int k = ncore_next(&c->in, r, type, total);
    *bad = k < 0;
    return k > 0;
}

/* ---- in-process links (the self-test's machines, and "mem:PORT" addresses) ---- */
static struct { net_machine *host; int port; net_memlink *pend[NET_MAXP]; int npend; } g_memhosts[8];
static int memhost_find(int port) { for (int i = 0; i < 8; i++) if (g_memhosts[i].host && g_memhosts[i].port == port) return i; return -1; }

/* ======================================================================== */
/* Sending                                                                  */
/* ======================================================================== */
static void send_seq(net_conn *c, const net_entry *e) {
    m_begin(M_SEQ); m_u32(e->seq); m_u8((unsigned)(uint8_t)e->from); m_u8(e->flags); m_u8(e->kind); m_str(entry_text(e)); conn_queue(c);
}
static void host_broadcast_entry(const net_entry *e) {
    for (int p = 1; p < NET_MAXP; p++) if (NET->conns[p].state == C_LIVE) send_seq(&NET->conns[p], e);
}
/* host: number a command, append it to the log and send it to everyone */
static uint32_t host_assign(int kind, int from, int flags, const char *text) {
    net_machine *N = NET;
    net_entry *e = log_append((uint32_t)N->log_n + 1, from, flags, kind, text, NET_NO_TICK);
    view_apply(&N->AV, kind, from, text);
    host_broadcast_entry(e);
    return e->seq;
}
static void net_set_message(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(NET->message, sizeof NET->message, fmt, ap); va_end(ap);
}

/* ======================================================================== */
/* Hashing, desync reports                                                  */
/* ======================================================================== */
static uint64_t fnv64(uint64_t h, const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }
/* the bytes a desync check compares: the game's rules state, then ARC's view of the session */
static size_t net_state_bytes(void) {
    net_machine *N = NET; net_buf *b = &N->sbuf; b->n = 0;
    size_t (*fn)(void *, size_t) = G.app.net_state ? G.app.net_state : G.app.save_state;
    if (fn) {
        if (b->cap < 65536) nb_reserve(b, 65536);
        size_t n = fn(b->b, b->cap);
        if (n > b->cap) { nb_reserve(b, n); n = fn(b->b, b->cap); if (n > b->cap) n = 0; }
        b->n = n;
    } else if (G.app.dump) {
        arc_json j; memset(&j, 0, sizeof j); arc_json_obj(&j); G.app.dump(&j); arc_json_end(&j);
        if (j.buf) nb_put(b, j.buf, j.len);
        free(j.buf);
    }
    arc_writer w = { NULL, 0, 0 }; view_write(&w, &N->V);
    nb_reserve(b, w.len); w.buf = b->b + b->n; w.cap = w.len; w.len = 0; view_write(&w, &N->V); b->n += w.len;
    return b->n;
}
static void net_write_report(void);
static void host_check_hash(uint32_t seq, int player, uint64_t h);
static void net_hash_after(const net_entry *e, bool send) {
    net_machine *N = NET;
    net_state_bytes();
    uint64_t h = fnv64(1469598103934665603ull, N->sbuf.b, N->sbuf.n);
    N->last_hash = h; N->last_hash_seq = e->seq;
    if (!N->hring) N->hring = (net_hrow *)arc__xalloc(sizeof(net_hrow) * NET_HASH_RING);
    if (N->mode == MODE_HOST) host_check_hash(e->seq, N->local, h);
    else {
        net_hrow *r = &N->hring[e->seq % NET_HASH_RING]; r->seq = e->seq; r->have = 1; r->h[0] = h;
        if (send && N->conns[0].state == C_LIVE) { m_begin(M_HASH); m_u32(e->seq); m_u64(h); conn_queue(&N->conns[0]); }
    }
}
static void host_desync(uint32_t seq) {
    net_machine *N = NET;
    if (N->desync_seq) return;                       /* the first one is the one that matters */
    net_hrow *r = &N->hring[seq % NET_HASH_RING];
    N->desync_seq = seq; N->desync_upto = (uint32_t)N->log_n; N->desync_pending = true;
    N->desync_have = r->have; memcpy(N->desync_hashes, r->h, sizeof r->h);
    ncore_desync_msg(r, seq, N->desync_upto);
    for (int p = 1; p < NET_MAXP; p++) if (N->conns[p].state == C_LIVE) { net_conn *c = &N->conns[p]; size_t keep = g_msg.n; conn_queue(c); g_msg.n = keep; }
}
static void host_check_hash(uint32_t seq, int player, uint64_t h) {
    net_machine *N = NET;
    if (!N->hring) N->hring = (net_hrow *)arc__xalloc(sizeof(net_hrow) * NET_HASH_RING);
    if (ncore_hash_put(N->hring, NET_HASH_RING, seq, player, h, 0)) host_desync(seq);   /* the host's own hash decides */
}
static void net_mkdir(const char *p) { ARC__MKDIR(p); }
static void net_log_line(const net_entry *e, char *out, size_t n) {
    snprintf(out, n, "%u %d %s %u %s", e->seq, e->from, e->kind < NK_COUNT ? nk_names[e->kind] : "?", (unsigned)e->flags, entry_text(e));
}
/* every machine writes one when it has applied up to the host's sequence number at detection, so they all compare */
static void net_write_report(void) {
    net_machine *N = NET;
    N->desync_pending = false; N->desync_done = true;
    uint32_t s = N->desync_seq;
    uint64_t t = (s >= 1 && (int)s <= N->log_n) ? N->log[s - 1].tick : 0;
    char dir[700]; net_mkdir(g_net_report);
    snprintf(dir, sizeof dir, "%s/seq%u_p%d", g_net_report, s, N->local); net_mkdir(dir);
    printf("ARC error desync seq=%u tick=%llu player=%d report=%s\n", s, (unsigned long long)t, N->local, dir); fflush(stdout);
    char path[800]; FILE *f;
    snprintf(path, sizeof path, "%s/dump.json", dir); do_dump(path);
    snprintf(path, sizeof path, "%s/log.txt", dir); arc_net_log_save(path);
    net_state_bytes();
    snprintf(path, sizeof path, "%s/state.bin", dir); arc_file_write(path, N->sbuf.b, N->sbuf.n);
    snprintf(path, sizeof path, "%s/info.json", dir);
    if ((f = fopen(path, "wb")) != NULL) {
        uint64_t mine = 0; bool have = false;
        if (N->hring) { net_hrow *r = &N->hring[s % NET_HASH_RING]; if (r->seq == s) { have = true; mine = N->mode == MODE_HOST ? r->h[0] : r->h[0]; } }
        fprintf(f, "{\"desync_seq\":%u,\"tick\":%llu,\"report_seq\":%u,\"report_tick\":%llu,\"player\":%d,\"my_hash\":\"%016llx\",\"my_hash_known\":%s,\"hashes\":{",
                s, (unsigned long long)t, (unsigned)N->napplied, (unsigned long long)G.ticks, N->local, (unsigned long long)mine, have ? "true" : "false");
        bool first = true;
        for (int p = 0; p < NET_MAXP; p++) if (N->desync_have & (1u << p)) { fprintf(f, "%s\"%d\":\"%016llx\"", first ? "" : ",", p, (unsigned long long)N->desync_hashes[p]); first = false; }
        fprintf(f, "},\"state_bytes\":%zu,\"hash_source\":\"%s\"}\n", N->sbuf.n, G.app.net_state ? "net_state" : G.app.save_state ? "save_state" : "dump.game");
        fclose(f);
    }
    char txt[160]; snprintf(txt, sizeof txt, "desync at seq %u (report %.100s)", s, dir);
    ev_push(ARC_NET_EV_DESYNC, -1, s, false, txt);
}

/* ======================================================================== */
/* Applying commands (at the start of every tick)                           */
/* ======================================================================== */
static void net_rec_write(const net_entry *e);
static void net_run_game_command(const net_entry *e) {
    net_machine *N = NET;
    char buf[NET_TEXT_MAX + 1]; snprintf(buf, sizeof buf, "%s", entry_text(e));
    const char *argv[64]; int argc = 0;
    for (char *p = strtok(buf, " \t"); p && argc < 64; p = strtok(NULL, " \t")) argv[argc++] = p;
    if (!argc) return;
    arc_rng saved = g_rng; g_rng = N->V.rng;          /* arc_rand_* inside command(): the match's own generator */
    N->cur_from = e->from; N->applying = true;
    bool ok = G.app.command && G.app.command(argv[0], argc - 1, argv + 1);
    N->applying = false; N->cur_from = -1;
    N->V.rng = g_rng; g_rng = saved;
    if (!ok && !net_quiet()) { printf("ARC error net command not handled (seq %u from %d): %s\n", e->seq, e->from, entry_text(e)); fflush(stdout); }
}
static void net_turn_event(const net_entry *e) {
    net_machine *N = NET; char line[48];
    int owner = N->V.simul ? ARC_NET_EVERYONE : N->V.owner;
    snprintf(line, sizeof line, "turn %d", N->V.turn);
    if (g_net_verbose || !N->cur_backlog) net_out("ARC net turn %d owner=%d seq=%u\n", N->V.turn, owner, e->seq);
    ev_push(ARC_NET_EV_TURN, owner, e->seq, true, line);
}
static void net_apply(net_entry *e) {
    net_machine *N = NET;
    const char *t = entry_text(e);
    N->cur_backlog = e->backlog != 0;
    if (g_net_verbose) net_out("ARC net apply seq=%u tick=%llu from=%d %s %s\n", e->seq, (unsigned long long)G.ticks, e->from, nk_names[e->kind], t);
    if (e->kind == NK_CMD) net_run_game_command(e);
    else {
        int tr = view_apply(&N->V, e->kind, e->from, t);
        char line[160];
        switch (e->kind) {
        case NK_JOIN: net_out("ARC net joined %d %s\n", e->from, t); ev_push(ARC_NET_EV_JOINED, e->from, e->seq, true, t); break;
        case NK_REJOIN: net_out("ARC net rejoined %d %s\n", e->from, N->V.p[e->from].name); ev_push(ARC_NET_EV_REJOINED, e->from, e->seq, true, N->V.p[e->from].name); break;
        case NK_LEAVE: net_out("ARC net left %d %s\n", e->from, N->V.p[e->from].name); ev_push(ARC_NET_EV_LEFT, e->from, e->seq, true, N->V.p[e->from].name);
            if (N->rb && N->online) rb_drop_player(e->from, "left the room");   /* 0.10.4: the relay says so (the hub drops them; the hub gone: alone) */
            if (tr & NCORE_TURN_ADVANCED) net_turn_event(e);   /* 0.8.5: the one everybody else was waiting for left */
            break;
        case NK_READY: ev_push(ARC_NET_EV_READY, e->from, e->seq, true, t); break;
        case NK_NAME: ev_push(ARC_NET_EV_NAME, e->from, e->seq, true, t); break;
        case NK_START: {
            int n = 0; for (int i = 0; i < NET_MAXP; i++) n += N->V.p[i].in_match;
            if (strstr(t, "rollback")) {             /* 0.8.1: a rollback match starts next tick (match tick 0), with its START event */
                net_out("ARC net start seed=%llu players=%d seq=%u rollback\n", (unsigned long long)N->V.seed, n, e->seq);
                rb_arm(N->V.seed); break;
            }
            net_out("ARC net start seed=%llu players=%d seq=%u\n", (unsigned long long)N->V.seed, n, e->seq);
            ev_push(ARC_NET_EV_START, N->V.owner, e->seq, true, t); break; }
        case NK_TURN:
            if (tr & NCORE_TURN_DONE) {                /* 0.8.5 simultaneous: one more player has ended the turn */
                unsigned w = ncore_turn_pending(&N->V); int k = snprintf(line, sizeof line, "waiting for");
                for (int i = 0; i < NET_MAXP && k > 0 && k < (int)sizeof line - 4; i++) if (w & (1u << i)) k += snprintf(line + k, sizeof line - (size_t)k, " %d", i);
                if (!w) snprintf(line, sizeof line, "everyone has ended the turn");
                if (g_net_verbose || !N->cur_backlog) net_out("ARC net turn_done %d turn=%d seq=%u %s\n", e->from, (tr & NCORE_TURN_ADVANCED) ? N->V.turn - 1 : N->V.turn, e->seq, line);
                ev_push(ARC_NET_EV_TURN_DONE, e->from, e->seq, true, line);
            }
            if (tr & NCORE_TURN_ADVANCED) net_turn_event(e);
            break;
        default: break;
        }
        (void)line;
    }
    if (e->from == N->local && N->mode == MODE_CLIENT && N->inflight > 0 && (e->kind == NK_CMD || e->kind == NK_TURN || e->kind == NK_READY || e->kind == NK_NAME || e->kind == NK_START)) N->inflight--;
    N->cur_backlog = false;
}
static bool net_by_tick(void) { return NET->replay || ((g_resim || g_in_past) && NET->mode != MODE_OFFLINE); }
void arc__net_tick_begin(void) {
    net_machine *N = NET;
    if (N->napplied >= N->log_n) return;
    bool by_tick = net_by_tick();
    bool live_session = !by_tick && N->mode != MODE_OFFLINE;
    int batch_end = N->log_n;
    while (N->napplied < batch_end) {
        net_entry *e = &N->log[N->napplied];
        if (by_tick) { if (e->tick == NET_NO_TICK || e->tick > G.ticks) break; }
        else e->tick = G.ticks;
        net_apply(e);
        N->napplied++;
        if (!by_tick) net_rec_write(e);
        if (live_session && !net_rollback_mode()) {     /* (0.8.1: a rollback game's state is compared per tick instead) */
            bool last_of_backlog = e->backlog && e->seq == N->backlog_end;
            net_hash_after(e, !e->backlog || last_of_backlog);
        }
        if (N->desync_pending && (uint32_t)N->napplied >= N->desync_upto) net_write_report();
        if (!by_tick && e->kind != NK_CMD && !G.app.net_event) break;   /* polling games see the event before what follows */
    }
}

/* ======================================================================== */
/* Host                                                                     */
/* ======================================================================== */
static void host_drop(int p, const char *why) {
    net_machine *N = NET; net_conn *c = &N->conns[p];
    if (c->state == C_FREE) return;
    conn_close(c);
    net_out("ARC log net player %d dropped (%s)\n", p, why);
    if (N->rb) rb_drop_player(p, why);                /* 0.8.1: out of the rollback match too */
    if (N->AV.p[p].used && N->AV.p[p].connected) host_assign(NK_LEAVE, p, 0, "");
}
static void host_refuse(net_conn *c, int code, const char *text) {
    m_begin(M_REFUSE); m_u8((unsigned)code); m_str(text); conn_queue_now(c);
    conn_pump(c);                                     /* best effort: send it before closing */
    conn_close(c);
}
static void host_send_pings(void) {
    net_machine *N = NET;
    m_begin(M_PINGS); m_u8(NET_MAXP);
    for (int p = 0; p < NET_MAXP; p++) m_u16((unsigned)(p == 0 ? 0 : N->conns[p].state == C_LIVE && N->conns[p].ping_ms >= 0 ? N->conns[p].ping_ms : 0xFFFF));
    for (int p = 1; p < NET_MAXP; p++) if (N->conns[p].state == C_LIVE) { size_t keep = g_msg.n; conn_queue(&N->conns[p]); g_msg.n = keep; }
    for (int p = 0; p < NET_MAXP; p++) N->ping_table[p] = p == 0 ? 0 : (N->conns[p].state == C_LIVE ? N->conns[p].ping_ms : -1);
}
static void host_hello(net_conn *pc, net_rd *r) {
    net_machine *N = NET;
    ncore_hello h;
    if (!ncore_hello_read(r, &h)) { host_refuse(pc, R_PROTOCOL, "this game speaks another network protocol: update ARC on both machines"); return; }
    char msg[300];
    if (ncore_version_check(N->d_game, N->d_version, ARC_VERSION, &h, msg, sizeof msg)) {
        net_out("ARC log net refused %s: %s\n", h.name, msg);
        ev_push(ARC_NET_EV_VERSION, -1, 0, false, msg);
        host_refuse(pc, R_VERSION, msg); return;
    }
    bool live[NET_MAXP]; for (int p = 0; p < NET_MAXP; p++) live[p] = p == 0 || N->conns[p].state != C_FREE;
    /* a player coming back before this host noticed it was gone (a crash or a power cut sends nothing; the timeout takes
       10 s): the same running game (token), or once the match has started the same name, replaces the stale link */
    unsigned stale = ncore_hello_stale(&N->AV, N->tokens, live, 1, &h);
    for (int p = 1; p < NET_MAXP; p++) if (stale & (1u << p)) { host_drop(p, "replaced by a new connection from the same player"); live[p] = N->conns[p].state != C_FREE; }
    bool rejoin; uint32_t from; int code = 0; const char *why = NULL; int maxp = N->desc.max_players;
    int slot = ncore_hello_slot(&N->AV, N->tokens, live, 1, maxp, &h, N->session, (uint32_t)N->log_n, &rejoin, &from, &code, &why);
    if (slot < 0) { host_refuse(pc, code, why); return; }
    N->tokens[slot] = h.token;
    char nm[32];
    if (!rejoin) ncore_unique_name(&N->AV, h.name, nm, sizeof nm); else snprintf(nm, sizeof nm, "%s", N->AV.p[slot].name);
    net_conn *c = &N->conns[slot]; *c = *pc; memset(pc, 0, sizeof *pc); pc->fd = NET_BAD;
    c->state = C_LIVE; c->next_ping = net_now();
    m_begin(M_WELCOME); m_u16(NET_PROTO); m_u8((unsigned)slot); m_u8((unsigned)maxp); m_u64(N->session); m_str(N->d_name); conn_queue(c);
    for (uint32_t i = from; i < (uint32_t)N->log_n; i++) send_seq(c, &N->log[i]);
    m_begin(M_CAUGHT_UP); m_u32((uint32_t)N->log_n); conn_queue(c);
    net_out("ARC log net player %d %s %s (sent %u commands)\n", slot, rejoin ? "rejoining:" : "joining:", nm, (unsigned)N->log_n - from);
    if (rejoin) host_assign(NK_REJOIN, slot, 0, ""); else host_assign(NK_JOIN, slot, 0, nm);
    host_send_pings();
}
static void host_message(int p, int type, net_rd *r) {
    net_machine *N = NET; net_conn *c = &N->conns[p];
    switch (type) {
    case M_CMD: {
        unsigned flags = r_u8(r), kind = r_u8(r); char text[NET_TEXT_MAX + 1]; r_str(r, text, sizeof text);
        if (r->bad) break;
        const char *why = ncore_cmd_check(&N->AV, p, kind, &flags, false);   /* (shared with arc_relay) */
        if (why) {
            if (kind == NK_TURN && !text[0]) snprintf(text, sizeof text, "end_turn");
            m_begin(M_REFUSED); m_str(why); m_str(text); conn_queue(c);
            net_out("ARC log net refused player %d (%s): %s\n", p, why, text);
            break;
        }
        host_assign((int)kind, p, (int)flags, text);
        break; }
    case M_HASH: { uint32_t s = r_u32(r); uint64_t h = r_u64(r); if (!r->bad) host_check_hash(s, p, h); break; }
    case M_PING: { double t = r_f64(r); m_begin(M_PONG); m_f64(t); conn_queue(c); break; }
    case M_PONG: { double t = r_f64(r); c->ping_ms = (int)((net_now() - t) * 1000.0 + 0.5); break; }
    case M_SYNC: { uint32_t id = r_u32(r); m_begin(M_SYNCED); m_u32(id); m_u32((uint32_t)N->log_n); conn_queue(c); break; }
    case M_BYE: host_drop(p, "left"); break;
    default: break;
    }
}
static void host_frame(void) {
    net_machine *N = NET; double now = net_now();
    /* accept */
    for (;;) {
        int slot = -1; for (int i = 0; i < NET_MAXP; i++) if (N->pend[i].state == C_FREE) { slot = i; break; }
        net_conn *c = slot >= 0 ? &N->pend[slot] : NULL;
        if (N->memory) {
            int mh = memhost_find(N->port);
            if (mh < 0 || !g_memhosts[mh].npend) break;
            net_memlink *m = g_memhosts[mh].pend[0];
            memmove(g_memhosts[mh].pend, g_memhosts[mh].pend + 1, sizeof(net_memlink *) * (size_t)(--g_memhosts[mh].npend));
            if (!c) { m->closed[1] = true; continue; }
            conn_reset(c); c->kind = 2; c->mem = m; c->side = 1;
        } else {
#if NET_SOCKETS
            if (N->lsock == NET_BAD) break;
            net_sock s = ns_accept(N->lsock);
            if (s == NET_BAD) break;
            if (!c) { ns_close(s); continue; }
            conn_reset(c); c->kind = 1; c->fd = s;
#else
            break;
#endif
        }
        c->state = C_HELLO; c->opened = c->last_recv = now;
    }
    /* clients waiting to say hello */
    for (int i = 0; i < NET_MAXP; i++) {
        net_conn *c = &N->pend[i]; if (c->state != C_HELLO) continue;
        if (!conn_pump(c) || now - c->opened > 10.0) { conn_close(c); continue; }
        net_rd r; int type; size_t total; bool bad;
        if (conn_next(c, &r, &type, &total, &bad)) {
            if (type != M_HELLO) { conn_close(c); continue; }
            nb_drop_front(&c->in, total);
            host_hello(c, &r);
        } else if (bad || c->peer_closed) conn_close(c);
    }
    /* players */
    for (int p = 1; p < NET_MAXP; p++) {
        net_conn *c = &N->conns[p]; if (c->state != C_LIVE) continue;
        if (now >= c->next_ping) { m_begin(M_PING); m_f64(now); conn_queue(c); c->next_ping = now + 1.0; }
        if (!conn_pump(c)) { host_drop(p, c->sim_losses >= 99 ? "network failure (simulated)" : "connection closed"); continue; }
        if (now - c->last_recv > N->sim.timeout) { host_drop(p, "timed out"); continue; }
        net_rd r; int type; size_t total; bool bad;
        while (c->state == C_LIVE && conn_next(c, &r, &type, &total, &bad)) {
            host_message(p, type, &r);
            if (c->state == C_LIVE) nb_drop_front(&c->in, total);
        }
        if (bad) host_drop(p, "garbled data");
        else if (c->state == C_LIVE && c->peer_closed) host_drop(p, "connection closed");
        else if (c->state == C_LIVE) conn_pump(c);
    }
    if (now >= N->next_pings) { host_send_pings(); N->next_pings = now + 2.0; }
#if NET_SOCKETS
    /* LAN discovery: answer "ARCF" questions */
    if (!N->memory && N->dsock != NET_BAD) {
        uint8_t q[512]; uint32_t ip; int port; int n;
        while ((n = ns_udp_recv(N->dsock, q, sizeof q, &ip, &port)) > 0) {
            if (n < 6 || memcmp(q, "ARCF", 4)) continue;
            int players = 0; for (int i = 0; i < NET_MAXP; i++) players += N->AV.p[i].used && N->AV.p[i].connected;
            m_begin(0); g_msg.n = 0;
            m_u8('A'); m_u8('R'); m_u8('C'); m_u8('H'); m_u16(NET_PROTO);
            m_str(N->d_name); m_str(N->d_game); m_str(N->d_version); m_str(ARC_VERSION);
            m_u8((unsigned)players); m_u8((unsigned)N->desc.max_players); m_u8(N->AV.started ? 1 : 0); m_u16((unsigned)N->port);
            ns_udp_send(N->dsock, ip, port, g_msg.b, g_msg.n);
        }
    }
#endif
}

/* ======================================================================== */
/* Client                                                                   */
/* ======================================================================== */
static void client_lost(const char *why, bool retry) {
    net_machine *N = NET;
    conn_close(&N->conns[0]);
    if (N->rb) rb_lost_host(why);                      /* 0.8.1 */
    if (N->status == ARC_NET_LOST) return;
    N->status = ARC_NET_LOST; N->inflight = 0;
    net_set_message("%s", why);
    net_out("ARC log net lost: %s\n", why);
    ev_push(ARC_NET_EV_HOST_LOST, 0, 0, false, why);
    N->auto_retry = retry && !N->sim_dropped; N->retry_at = net_now() + 1.0; N->retry_until = net_now() + 30.0;
}
static bool client_open(void) {
    net_machine *N = NET; net_conn *c = &N->conns[0];
    conn_reset(c);
    if (N->memory) {
        int mh = memhost_find(N->join_port);
        if (mh < 0 || g_memhosts[mh].npend >= NET_MAXP) return false;
        net_memlink *m = (net_memlink *)arc__xalloc(sizeof *m);
        g_memhosts[mh].pend[g_memhosts[mh].npend++] = m;
        c->kind = 2; c->mem = m; c->side = 0; c->state = C_HELLO;
    } else if (N->online && N->rurl.scheme != NCORE_URL_TCP && !NET_SOCKETS) {
#if defined(__EMSCRIPTEN__)
        int id = web_ws_open(N->relay_url);            /* 0.8.5: the browser's WebSocket to the relay */
        if (id < 0) return false;
        c->kind = 3; c->web_id = id; c->state = C_CONNECTING;
#else
        return false;
#endif
    } else {
#if NET_SOCKETS
        net_sock s = ns_tcp_connect(N->join_ip, N->join_port);
        if (s == NET_BAD) return false;
        c->kind = 1; c->fd = s; c->state = C_CONNECTING;
        c->ws = N->online && N->rurl.scheme == NCORE_URL_WS;   /* 0.8.5: a relay's WebSocket (tcp:// = the raw protocol) */
#else
        return false;
#endif
    }
    c->ip = N->join_ip; c->port = N->join_port; c->opened = c->last_recv = net_now(); c->next_ping = net_now() + 1.0;
    N->status = ARC_NET_JOINING;
    return true;
}
/* 0.8.5: the WebSocket handshake (RFC 6455 section 4.1) on a connected socket: the request now, the answer checked in
   client_frame (101 Switching Protocols with the matching Sec-WebSocket-Accept) */
#if NET_SOCKETS
static void client_ws_request(net_conn *c) {
    net_machine *N = NET;
    uint8_t key[16]; for (int i = 0; i < 16; i += 4) ws_mask_key(key + i);
    char k64[32]; ncore_base64(key, 16, k64, sizeof k64);
    ncore_ws_accept(k64, c->ws_accept);
    char host[160];
    if ((N->rurl.scheme == NCORE_URL_WS && N->rurl.port == 80)) snprintf(host, sizeof host, "%s", N->rurl.host);
    else snprintf(host, sizeof host, "%s:%d", N->rurl.host, N->rurl.port);
    char req[640];
    int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
                     "Sec-WebSocket-Version: 13\r\nUser-Agent: ARC/%s\r\n\r\n", N->rurl.path, host, k64, ARC_VERSION);
    if (n > 0) nb_put(&c->wsout, req, (size_t)n < sizeof req ? (size_t)n : sizeof req - 1);
}
#endif
/* 1 = upgraded, 0 = not yet, -1 = refused (message set) */
static int client_ws_answer(net_conn *c) {
    if (c->wsin.n < 12) return 0;
    uint8_t z = 0; nb_put(&c->wsin, &z, 1); c->wsin.n--;   /* (a 0 after the bytes, for the string functions) */
    const char *head = (const char *)c->wsin.b, *end = strstr(head, "\r\n\r\n");
    if (!end) { if (c->wsin.n > 8192) { net_set_message("the relay sent a bad WebSocket answer"); return -1; } return 0; }
    size_t hl = (size_t)(end - head) + 4;
    char st[64]; snprintf(st, sizeof st, "%.*s", (int)strcspn(head, "\r"), head);
    char acc[64] = "";
    ncore_http_header(head, "Sec-WebSocket-Accept", acc, sizeof acc);
    if (strncmp(st, "HTTP/1.1 101", 12)) { net_set_message("the relay refused the WebSocket (%s): is %s an ARC relay?", st, NET->relay_url); return -1; }
    if (strcmp(acc, c->ws_accept)) { net_set_message("the relay's WebSocket answer has the wrong key"); return -1; }
    nb_drop_front(&c->wsin, hl);
    c->ws_open = true;
    return 1;
}
static void client_send_hello(void) {
    net_machine *N = NET; net_conn *c = &N->conns[0];
    ncore_hello h; memset(&h, 0, sizeof h);
    h.magic = NET_MAGIC; h.proto = NET_PROTO;
    snprintf(h.arcv, sizeof h.arcv, "%s", ARC_VERSION); snprintf(h.game, sizeof h.game, "%s", N->d_game);
    snprintf(h.ver, sizeof h.ver, "%s", N->d_version); snprintf(h.name, sizeof h.name, "%s", N->my_name);
    h.token = N->token; h.session = N->session; h.last = (uint32_t)N->log_n;
    if (N->online) {                                   /* 0.8.5: which room, and how to make one */
        h.op = N->online_op; snprintf(h.room, sizeof h.room, "%s", N->room);
        h.max_players = N->desc.max_players; snprintf(h.room_name, sizeof h.room_name, "%s", N->d_name);
        h.flags = (N->session && N->log_n > 0) ? NCORE_HELLO_RESTORE : 0;
    }
    ncore_hello_write(&h);
    conn_queue(c);
}
static void client_message(int type, net_rd *r) {
    net_machine *N = NET; net_conn *c = &N->conns[0];
    switch (type) {
    case M_WELCOME: {
        unsigned proto = r_u16(r), slot = r_u8(r), maxp = r_u8(r); uint64_t session = r_u64(r); char hn[48]; r_str(r, hn, sizeof hn);
        if (r->bad || proto != NET_PROTO || slot >= NET_MAXP) { client_lost("garbled welcome", false); return; }
        bool same = session == N->session && N->session != 0;
        if (!same) {                                   /* a new session: everything comes from the host */
            log_clear(); N->session = session; view_reset(&N->V, true, session);
        }
        char room[16] = ""; if (r_more(r)) r_str(r, room, sizeof room);   /* 0.8.5: a relay names the room */
        N->local = (int)slot; N->desc.max_players = (int)maxp; snprintf(N->d_name, sizeof N->d_name, "%s", hn);
        c->state = C_LIVE; N->status = ARC_NET_CONNECTED; N->message[0] = 0; N->auto_retry = false;
        bool new_room = false;
        if (N->online) {
            new_room = strcmp(N->room, room) != 0 || !same;
            snprintf(N->room, sizeof N->room, "%s", room); N->online_op = NCORE_OP_JOIN;   /* (a reconnection joins it again) */
            if (N->local == 0) N->status = ARC_NET_HOSTING;   /* the room's host: it starts the match */
        }
        net_out("ARC net connected player=%d host=%s%s\n", N->local, hn, same ? " (rejoined)" : "");
        ev_push(ARC_NET_EV_CONNECTED, N->local, 0, false, hn);
        if (N->online && new_room) { net_out("ARC net room %s relay=%s player=%d\n", N->room, N->relay_url, N->local); ev_push(ARC_NET_EV_ROOM, N->local, 0, false, N->room); }
        N->was_connected = true;
        break; }
    case M_REFUSE: {
        unsigned code = r_u8(r); char text[300]; r_str(r, text, sizeof text);
        conn_close(c);
        N->status = ARC_NET_REFUSED; N->mode = MODE_OFFLINE; N->online = false; net_set_message("%s", text);
        printf("ARC error net refused: %s\n", text); fflush(stdout);
        ev_push(code == R_VERSION ? ARC_NET_EV_VERSION : ARC_NET_EV_REFUSED, -1, 0, false, text);
        break; }
    case M_SEQ: {
        uint32_t seq = r_u32(r); int from = (int)(int8_t)r_u8(r); unsigned flags = r_u8(r), kind = r_u8(r); char text[NET_TEXT_MAX + 1]; r_str(r, text, sizeof text);
        if (r->bad || kind >= NK_COUNT) { client_lost("garbled command", false); return; }
        if (seq <= (uint32_t)N->log_n) break;          /* already have it */
        if (seq != (uint32_t)N->log_n + 1) { client_lost("commands arrived out of order", true); return; }
        net_entry *e = log_append(seq, from, (int)flags, (int)kind, text, NET_NO_TICK);
        e->backlog = N->catching ? 1 : 0;              /* until CAUGHT_UP, what arrives is the backlog */
        break; }
    case M_CAUGHT_UP: { uint32_t s = r_u32(r); N->backlog_end = s; N->catching = false; break; }
    case M_REFUSED: {
        char why[128], text[NET_TEXT_MAX + 1]; r_str(r, why, sizeof why); r_str(r, text, sizeof text);
        if (N->inflight > 0) N->inflight--;
        printf("ARC error net refused (%s): %s\n", why, text); fflush(stdout);
        char t[160]; snprintf(t, sizeof t, "%.30s: %.120s", why, text);
        ev_push(ARC_NET_EV_REFUSED, N->local, 0, false, t);
        break; }
    case M_DESYNC: {
        uint32_t s = r_u32(r), upto = r_u32(r); unsigned n = r_u8(r);
        if (N->desync_seq) break;
        N->desync_seq = s; N->desync_upto = upto; N->desync_pending = true; N->desync_have = 0;
        for (unsigned i = 0; i < n && !r->bad; i++) { unsigned p = r_u8(r); uint64_t h = r_u64(r); if (p < NET_MAXP) { N->desync_have |= (uint8_t)(1u << p); N->desync_hashes[p] = h; } }
        if ((uint32_t)N->napplied >= upto) net_write_report();
        break; }
    case M_PING: { double t = r_f64(r); m_begin(M_PONG); m_f64(t); conn_queue(c); break; }
    case M_PONG: { double t = r_f64(r); c->ping_ms = (int)((net_now() - t) * 1000.0 + 0.5); break; }
    case M_PINGS: { unsigned n = r_u8(r); for (unsigned i = 0; i < n && i < NET_MAXP; i++) { unsigned v = r_u16(r); N->ping_table[i] = v == 0xFFFF ? -1 : (int)v; } break; }
    case M_SYNCED: { uint32_t id = r_u32(r), s = r_u32(r); if (id == N->sync_id) { N->sync_target = s; N->sync_known = true; } break; }
    case M_BYE: {
        char why[128]; r_str(r, why, sizeof why);
        bool retry = r_more(r) && r_u8(r) != 0;        /* 0.8.5: a relay going down (a restart) asks its players to come back */
        client_lost(why[0] ? why : "the host left", retry); break; }
    case M_RESTORE_FROM: {                             /* 0.8.5: a restarted relay rebuilds the room from this machine's log */
        uint32_t from = r_u32(r);
        if (r->bad || from > (uint32_t)N->log_n) { client_lost("garbled restore request", true); return; }
        for (uint32_t i = from; i < (uint32_t)N->log_n; i++) send_seq(c, &N->log[i]);
        m_begin(M_RESTORED); m_u32((uint32_t)N->log_n); conn_queue(c);
        net_out("ARC log net restoring room %s on the relay (%u commands)\n", N->room, (unsigned)N->log_n - from);
        break; }
    default: break;
    }
}
static void client_frame(void) {
    net_machine *N = NET; net_conn *c = &N->conns[0]; double now = net_now();
    /* --net-sim drop_at / rejoin_at: pull the cable at a tick, plug it back in later */
    if (N->sim.on && N->sim.drop_at && !N->sim_dropped && G.ticks >= N->sim.drop_at && c->state == C_LIVE) {
        N->sim_dropped = true; client_lost("network dropped (simulated)", false);
    }
    if (N->sim_dropped && N->sim.rejoin_at && G.ticks >= N->sim.rejoin_at && N->status == ARC_NET_LOST) {
        N->sim.rejoin_at = 0; net_out("ARC log net rejoining %s (simulated)\n", N->join_addr);
        if (!client_open()) client_lost("cannot reach the host", false);
    }
    if (N->status == ARC_NET_LOST && N->auto_retry && now >= N->retry_at) {
        if (now > N->retry_until) N->auto_retry = false;
        else { N->retry_at = now + 2.0; if (client_open()) net_out("ARC log net reconnecting to %s\n", N->join_addr); else N->status = ARC_NET_LOST; }
    }
    if (c->state == C_FREE) return;
    if (c->state == C_CONNECTING || c->state == C_UPGRADE) {
        int k = 0;
        if (c->kind == 3) {                            /* 0.8.5: the browser's WebSocket: open, still opening, or failed */
#if defined(__EMSCRIPTEN__)
            int st = web_ws_state(c->web_id); k = st == 1 ? 1 : st == 3 ? -1 : 0;
#endif
            if (k > 0) c->state = C_HELLO;
        } else if (c->state == C_CONNECTING) {
#if NET_SOCKETS
            k = ns_connect_step(c->fd, c->ip, c->port);
            if (k > 0 && c->ws) { client_ws_request(c); c->state = C_UPGRADE; k = 0; }
            else if (k > 0) c->state = C_HELLO;
#endif
        } else {                                       /* C_UPGRADE: the request out, the answer in */
            if (!conn_pump(c) && !c->wsin.n) k = -1;
            else { k = client_ws_answer(c); if (k < 0) { conn_close(c); N->status = ARC_NET_REFUSED; N->mode = MODE_OFFLINE; N->online = false;
                                                         printf("ARC error net %s\n", N->message); fflush(stdout); ev_push(ARC_NET_EV_REFUSED, -1, 0, false, N->message); return; }
                   if (k > 0) c->state = C_HELLO; }
        }
        if (k < 0 || (c->state != C_HELLO && now - c->opened > 10.0)) {
            conn_close(c);
            if (N->was_connected) { N->status = ARC_NET_LOST; net_out("ARC log net cannot reach the %s at %s (%s)\n", N->online ? "relay" : "host", N->join_addr, k < 0 ? "refused" : "timed out"); return; }
            N->status = ARC_NET_OFFLINE; N->mode = MODE_OFFLINE; N->online = false; net_set_message("cannot connect to %s", N->join_addr);
            printf("ARC error net cannot connect to %s\n", N->join_addr); fflush(stdout);
            ev_push(ARC_NET_EV_HOST_LOST, -1, 0, false, N->message);
            return;
        }
        if (c->state != C_HELLO) return;
    }
    if (c->state == C_HELLO && !c->hello_sent) { client_send_hello(); c->hello_sent = true; N->catching = true; }
    if (c->state == C_LIVE && now >= c->next_ping) { m_begin(M_PING); m_f64(now); conn_queue(c); c->next_ping = now + 1.0; }
    if (!conn_pump(c)) { if (N->status != ARC_NET_REFUSED) client_lost(c->sim_losses >= 99 ? "network failure (simulated)" : c->ws_bad ? "garbled WebSocket data" : N->online ? "connection to the relay closed" : "connection to the host closed", true); return; }
    if (c->state == C_LIVE && now - c->last_recv > N->sim.timeout) { client_lost("the host stopped answering", true); return; }
    net_rd r; int type; size_t total; bool bad;
    while (c->state != C_FREE && conn_next(c, &r, &type, &total, &bad)) {
        client_message(type, &r);
        if (c->state != C_FREE) nb_drop_front(&c->in, total);
    }
    if (bad) { client_lost("garbled data", true); return; }
    if (c->state != C_FREE && c->peer_closed) { client_lost("connection to the host closed", true); return; }
    if (c->state != C_FREE) conn_pump(c);
    if (c->state == C_LIVE) N->ping_table[N->local] = c->ping_ms;
}

/* ---- LAN discovery (client side) ---- */
static void find_frame(void) {
    net_machine *N = NET; double now = net_now(); (void)now;
    if (N->find_until <= 0) return;
    if (N->memory) {
        N->nfound = 0;
        for (int i = 0; i < 8 && N->nfound < 16; i++) if (g_memhosts[i].host) {
            net_machine *h = g_memhosts[i].host; arc_net_game *g = &N->found[N->nfound++]; memset(g, 0, sizeof *g);
            snprintf(g->name, sizeof g->name, "%s", h->d_name); snprintf(g->game, sizeof g->game, "%s", h->d_game);
            snprintf(g->version, sizeof g->version, "%s", h->d_version); snprintf(g->arc_version, sizeof g->arc_version, "%s", ARC_VERSION);
            snprintf(g->address, sizeof g->address, "mem:%d", g_memhosts[i].port);
            for (int p = 0; p < NET_MAXP; p++) g->players += h->AV.p[p].used && h->AV.p[p].connected;
            g->max_players = h->desc.max_players; g->started = h->AV.started;
        }
        N->find_until = 0; return;
    }
#if NET_SOCKETS
    if (N->fsock == NET_BAD) { N->find_until = 0; return; }
    if (now >= N->find_next) {
        uint8_t q[64]; size_t n = 0; memcpy(q, "ARCF", 4); n = 4; q[n++] = NET_PROTO & 0xFF; q[n++] = 0;
        int dp = ARC_NET_PORT + NET_DISC_PORT_OFS;
        ns_udp_send(N->fsock, 0xFFFFFFFFu, dp, q, n);          /* the LAN */
        ns_udp_send(N->fsock, 0x7F000001u, dp, q, n);          /* and this machine */
        N->find_next = now + 0.5;
    }
    uint8_t b[1024]; uint32_t ip; int port; int k;
    while ((k = ns_udp_recv(N->fsock, b, sizeof b, &ip, &port)) > 0) {
        if (k < 6 || memcmp(b, "ARCH", 4)) continue;
        net_rd r = { b + 4, (size_t)k - 4, 0, false };
        unsigned proto = r_u16(&r); arc_net_game g; memset(&g, 0, sizeof g);
        r_str(&r, g.name, sizeof g.name); r_str(&r, g.game, sizeof g.game); r_str(&r, g.version, sizeof g.version); r_str(&r, g.arc_version, sizeof g.arc_version);
        g.players = (int)r_u8(&r); g.max_players = (int)r_u8(&r); g.started = r_u8(&r) != 0; unsigned tport = r_u16(&r);
        if (r.bad || proto != NET_PROTO) continue;
        snprintf(g.address, sizeof g.address, "%u.%u.%u.%u:%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, tport);
        int at = -1; for (int i = 0; i < N->nfound; i++) if (!strcmp(N->found[i].address, g.address)) at = i;
        if (at < 0 && N->nfound < 16) at = N->nfound++;
        if (at >= 0) N->found[at] = g;
    }
    if (now > N->find_until) { N->find_until = 0; ns_close(N->fsock); N->fsock = NET_BAD; }
#endif
}

/* ======================================================================== */
/* Every frame                                                              */
/* ======================================================================== */
static bool net_wait_check(void);
void arc__net_frame(void) {
    net_machine *N = NET;
    if (N->mode == MODE_HOST) host_frame();
    else if (N->mode == MODE_CLIENT) client_frame();
    if (N->rb) rb_frame();                             /* 0.8.1: the rollback match's datagrams */
    if (N->find_until > 0) find_frame();
    if (N->desync_pending && (uint32_t)N->napplied >= N->desync_upto) net_write_report();
    if (N->wait.kind && G.headless && !N->clock && G.plat) G.plat->sleep(0.001);   /* a script waiting on the network: don't spin */
}

/* ======================================================================== */
/* Public API                                                               */
/* ======================================================================== */
static void net_default_name(void) {
    net_machine *N = NET;
    if (N->my_name[0]) return;
    const char *u = getenv("ARC_PLAYER"); if (!u || !*u) u = getenv("USER"); if (!u || !*u) u = getenv("USERNAME");
    snprintf(N->my_name, sizeof N->my_name, "%s", u && *u ? u : "Player");
}
static void net_token(void) {
    net_machine *N = NET;
    if (N->token) return;
    uint64_t t = (uint64_t)(arc__plat_time() * 1e9) ^ ((uint64_t)(uintptr_t)N << 17) ^ (uint64_t)time(NULL) * 0x9E3779B97F4A7C15ull;
    arc_rng r; arc_rng_seed(&r, t, 99); N->token = ((uint64_t)arc_rng_u32(&r) << 32) | arc_rng_u32(&r); if (!N->token) N->token = 1;
}
static void net_begin_session(int mode) {
    net_machine *N = NET;
    if (N->log_n > N->napplied && !net_quiet()) { printf("ARC log net: %d offline commands not applied yet were dropped\n", N->log_n - N->napplied); fflush(stdout); }
    log_clear(); N->mode = mode; N->touched = true; N->inflight = 0; N->ev_n = 0;
    N->desync_seq = 0; N->desync_pending = N->desync_done = false; N->was_connected = false; N->sim_dropped = false;
    if (N->hring) memset(N->hring, 0, sizeof(net_hrow) * NET_HASH_RING);
    memset(N->tokens, 0, sizeof N->tokens); N->catching = false; N->last_hash = 0; N->last_hash_seq = 0;
    for (int i = 0; i < NET_MAXP; i++) N->ping_table[i] = -1;
    if (N->sim.timeout == 0.0) N->sim.timeout = 10.0;
    N->online = false; N->room[0] = 0;
    net_default_name(); net_token();
    if (!N->ws_rng) N->ws_rng = N->token ^ 0xA5A5F00Dull;
}
/* 0.8.5: a client of the relay is "live" when the relay has welcomed it (the room's host shows HOSTING) */
static bool client_live(void) { net_machine *N = NET; return N->status == ARC_NET_CONNECTED || (N->online && N->status == ARC_NET_HOSTING); }
static bool net_room_host(void) { net_machine *N = NET; return N->online && N->mode == MODE_CLIENT && N->local == 0; }
const char *arc_net_status_name(int s) {
    static const char *const n[] = { "offline", "hosting", "joining", "connected", "lost", "refused", "unavailable" };
    return s >= 0 && s <= ARC_NET_UNAVAILABLE ? n[s] : "?";
}
int  arc_net_status(void) { return NET->status; }
const char *arc_net_message(void) { return NET->message; }
const arc_net_desc *arc_net_desc_get(void) { return &NET->desc; }
bool arc_net_is_host(void) { return NET->mode == MODE_HOST || net_room_host(); }
static void net_fill_desc(const arc_net_desc *d) {
    net_machine *N = NET;
    arc_net_desc z; memset(&z, 0, sizeof z); if (!d) d = &z;
    char game[48], ver[32], name[48];               /* (d may point at our own strings) */
    snprintf(game, sizeof game, "%s", d->game ? d->game : G.app.title ? G.app.title : "ARC");
    snprintf(ver, sizeof ver, "%s", d->version ? d->version : "");
    if (d->name) snprintf(name, sizeof name, "%s", d->name); else { net_default_name(); snprintf(name, sizeof name, "%.30s's game", N->my_name); }
    const char *fv = arc_arg_value("net-version"); if (fv) snprintf(ver, sizeof ver, "%s", fv);   /* tests: pretend to be another version */
    char relay[512]; snprintf(relay, sizeof relay, "%s", d->relay ? d->relay : "");
    N->desc = *d;
    memcpy(N->d_game, game, sizeof game); memcpy(N->d_version, ver, sizeof ver); memcpy(N->d_name, name, sizeof name);
    memcpy(N->d_relay, relay, sizeof relay);
    N->desc.game = N->d_game; N->desc.version = N->d_version; N->desc.name = N->d_name;
    N->desc.relay = N->d_relay[0] ? N->d_relay : NULL;
    if (N->desc.turns != ARC_NET_TURNS_SIMULTANEOUS) N->desc.turns = ARC_NET_TURNS_ALTERNATE;
    if (N->desc.max_players <= 0 || N->desc.max_players > NET_MAXP) N->desc.max_players = NET_MAXP;
    if (N->desc.max_players < 2) N->desc.max_players = 2;
    if (!N->desc.port) N->desc.port = ARC_NET_PORT;
    if (N->desc.mode == ARC_NET_ROLLBACK && N->desc.max_players > ARC_NET_ROLLBACK_PLAYERS) N->desc.max_players = ARC_NET_ROLLBACK_PLAYERS;
}
void arc_net_setup(const arc_net_desc *d) { net_fill_desc(d); NET->desc_set = true; }
/* ---- 0.8.5 online: the relay ---- */
/* the relay list: --net-relay, else arc_net_desc.relay; several URLs separated by spaces, each platform taking the first
   it can speak (desktop: ws:// or tcp://; web: wss://, or ws:// on a page that is not https) */
static const char *net_relay_list(void) { net_machine *N = NET; return N->flag_relay[0] ? N->flag_relay : N->d_relay; }
static bool net_pick_relay(const char *list, char *why, size_t wn) {
    net_machine *N = NET; char buf[512]; snprintf(buf, sizeof buf, "%s", list ? list : "");
    bool any = false, https = false;
#if defined(__EMSCRIPTEN__)
    https = web_page_https() != 0;
#endif
    why[0] = 0; (void)https;
    for (char *t = strtok(buf, " ,"); t; t = strtok(NULL, " ,")) {
        ncore_url u; if (!ncore_parse_url(t, &u)) { snprintf(why, wn, "'%s' is not a relay address (ws://host:port)", t); continue; }
        any = true;
#if defined(__EMSCRIPTEN__)
        if (u.scheme == NCORE_URL_TCP) { snprintf(why, wn, "a browser cannot use %s (tcp://): give a ws:// or wss:// address", t); continue; }
        if (https && u.scheme == NCORE_URL_WS) { snprintf(why, wn, "an https page can only reach a wss:// relay (not %s)", t); continue; }
        snprintf(N->relay_url, sizeof N->relay_url, "%s://%s:%d%s", u.scheme == NCORE_URL_WSS ? "wss" : "ws", u.host, u.port, u.path);
#else
        if (u.scheme == NCORE_URL_WSS) { snprintf(why, wn, "desktop builds speak ws:// (no TLS): add a ws:// address of the relay to arc_net_desc.relay (see docs/RELAY.md)"); continue; }
        snprintf(N->relay_url, sizeof N->relay_url, "%s://%s:%d%s", u.scheme == NCORE_URL_TCP ? "tcp" : "ws", u.host, u.port, u.scheme == NCORE_URL_TCP ? "" : u.path);
#endif
        N->rurl = u;
        return true;
    }
    if (!any && !why[0]) snprintf(why, wn, "no relay is set (arc_net_desc.relay or --net-relay URL)");
    return false;
}
bool arc_net_relay_available(void) {
    char why[200]; net_machine *N = NET; ncore_url keep = N->rurl; char ku[sizeof N->relay_url]; memcpy(ku, N->relay_url, sizeof ku);
    bool ok = net_pick_relay(net_relay_list(), why, sizeof why);
    N->rurl = keep; memcpy(N->relay_url, ku, sizeof ku);
    return ok;
}
static void net_no_online(const char *why) {
    net_machine *N = NET;
    net_set_message("%s", why);
    if (!NET_SOCKETS && !N->memory) N->status = ARC_NET_UNAVAILABLE;
    printf("ARC error net online: %s\n", why); fflush(stdout);
}
/* open the link to the relay for create (code NULL) or join */
static bool net_online_open(int op, const char *code, bool rejoin) {
    net_machine *N = NET;
    char why[200];
    if (!net_pick_relay(net_relay_list(), why, sizeof why)) { net_no_online(why); return false; }
    uint32_t ip = 0;
#if NET_SOCKETS
    if (!ns_init()) { net_no_online("networking is not available (ws2_32)"); return false; }
    if (!strcmp(N->rurl.host, "localhost")) ip = 0x7F000001u;
    else if (!net_dotted(N->rurl.host, &ip) && !ns_resolve(N->rurl.host, &ip)) { net_no_online(arc_fmt("cannot find the relay '%s'", N->rurl.host)); return false; }
#endif
    if (!rejoin) {
        if (!N->desc_set && !N->d_game[0]) net_fill_desc(NULL);
        net_begin_session(MODE_CLIENT); N->session = 0; N->local = -1;
        view_reset(&N->V, true, 0);
        N->online = true; N->online_op = op; snprintf(N->room, sizeof N->room, "%s", code ? code : "");
    }
    size_t ul = strlen(N->relay_url); bool slash = ul && N->relay_url[ul - 1] == '/';
    snprintf(N->join_addr, sizeof N->join_addr, "%s%s%s", N->relay_url, code && !slash ? "/" : "", code ? code : "");
    N->join_ip = ip; N->join_port = N->rurl.port; N->auto_retry = false;
    if (!client_open()) {
        if (rejoin) { N->status = ARC_NET_LOST; return false; }
        N->mode = MODE_OFFLINE; N->online = false; N->status = NET_SOCKETS ? ARC_NET_OFFLINE : ARC_NET_UNAVAILABLE;
        net_set_message("cannot connect to the relay %s", N->relay_url);
        printf("ARC error net online: %s\n", N->message); fflush(stdout); return false;
    }
    net_out("ARC log net %s %s\n", op == NCORE_OP_CREATE ? "asking the relay for a room at" : rejoin ? "rejoining" : "joining", N->join_addr);
    return true;
}
bool arc_net_host_online(void) {
    net_machine *N = NET;
    if (N->replay) { net_set_message("a replay cannot host"); return false; }
    if (N->mode != MODE_OFFLINE) { net_set_message("already in a session (arc_net_leave first)"); printf("ARC error net host_online: %s\n", N->message); fflush(stdout); return false; }
    if (!N->desc_set) { net_fill_desc(NULL); N->desc_set = true; }
    if (N->cfg_mode >= 0) N->desc.mode = N->cfg_mode;   /* 0.10.4: rollback matches play online too (UDP through the relay) */
    if (N->desc.mode == ARC_NET_ROLLBACK && N->desc.max_players > ARC_NET_ROLLBACK_PLAYERS) N->desc.max_players = ARC_NET_ROLLBACK_PLAYERS;
#if !NET_SOCKETS
    if (N->desc.mode == ARC_NET_ROLLBACK) { net_no_online("real-time (rollback) matches need UDP: desktop builds only (a browser has no UDP)"); return false; }
#endif
    return net_online_open(NCORE_OP_CREATE, NULL, false);
}
bool arc_net_join_online(const char *code) {
    net_machine *N = NET;
    if (N->replay) { net_set_message("a replay cannot join"); return false; }
    if (N->mode == MODE_HOST) { net_set_message("already hosting"); printf("ARC error net join_online: %s\n", N->message); fflush(stdout); return false; }
    char buf[300]; snprintf(buf, sizeof buf, "%s", code ? code : "");
    const char *c = buf; char *slash = strrchr(buf, '/');
    if (slash) {                                       /* "ws://relay.example.com:47880/K7QF": that relay, that room */
        *slash = 0; c = slash + 1;
        snprintf(N->flag_relay, sizeof N->flag_relay, "%s%s", strstr(buf, "://") ? "" : NET_SOCKETS ? "ws://" : "wss://", buf);
    }
    char room[8];
    if (!ncore_code_normalize(c, room, sizeof room)) { net_set_message("'%s' is not a room code (4 to 6 letters and digits)", c); printf("ARC error net join_online: %s\n", N->message); fflush(stdout); return false; }
    bool rejoin = N->online && N->mode == MODE_CLIENT && (N->status == ARC_NET_LOST || N->status == ARC_NET_JOINING) && !strcmp(room, N->room);
    if (N->mode == MODE_CLIENT && !rejoin) arc_net_leave();
    return net_online_open(NCORE_OP_JOIN, room, rejoin);
}
const char *arc_net_room(void) { return NET->online ? NET->room : ""; }
bool arc_net_host(const arc_net_desc *d) {
    net_machine *N = NET;
    if (N->replay) { net_set_message("a replay cannot host"); return false; }
#if !NET_SOCKETS
    if (!N->memory && N->mode == MODE_OFFLINE && net_relay_list()[0]) {   /* 0.8.5: the web build hosts on the relay */
        if (d) { net_fill_desc(d); N->desc_set = true; }
        return arc_net_host_online();
    }
#endif
    if (N->mode != MODE_OFFLINE) { net_set_message("already in a session (arc_net_leave first)"); printf("ARC error net host: %s\n", N->message); fflush(stdout); return false; }
    if (d || !N->desc_set) net_fill_desc(d);
    if (N->cfg_mode >= 0) { N->desc.mode = N->cfg_mode; if (N->desc.mode == ARC_NET_ROLLBACK && N->desc.max_players > ARC_NET_ROLLBACK_PLAYERS) N->desc.max_players = ARC_NET_ROLLBACK_PLAYERS; }
    N->port = N->desc.port;
    if (N->memory) {
        int slot = -1; for (int i = 0; i < 8; i++) if (!g_memhosts[i].host) { slot = i; break; }
        if (slot < 0 || memhost_find(N->port) >= 0) { net_set_message("port %d is in use", N->port); return false; }
        g_memhosts[slot].host = N; g_memhosts[slot].port = N->port; g_memhosts[slot].npend = 0;
    } else {
#if NET_SOCKETS
        if (!ns_init()) { N->status = ARC_NET_UNAVAILABLE; net_set_message("networking is not available (ws2_32)"); printf("ARC error net %s\n", N->message); fflush(stdout); return false; }
        N->lsock = ns_tcp_listen(N->port);
        if (N->lsock == NET_BAD) { net_set_message("cannot listen on port %d (in use?)", N->port); printf("ARC error net host: %s\n", N->message); fflush(stdout); return false; }
        N->dsock = ns_udp(ARC_NET_PORT + NET_DISC_PORT_OFS, false);
#else
        N->status = ARC_NET_UNAVAILABLE; net_set_message("multiplayer is not available in the web build (browsers cannot accept connections)");
        printf("ARC error net %s\n", N->message); fflush(stdout); return false;
#endif
    }
    net_begin_session(MODE_HOST);
    net_token();
    N->session = N->token ^ 0x5E5510Bull; if (!N->session) N->session = 1;
    view_reset(&N->V, true, N->session); view_reset(&N->AV, true, N->session);
    N->local = 0; N->status = ARC_NET_HOSTING; N->message[0] = 0; N->next_pings = net_now() + 2.0;
    host_assign(NK_JOIN, 0, 0, N->my_name);
    net_out("ARC net hosting port=%d game=%s version=%s name=%s\n", N->port, N->d_game, N->d_version, N->d_name);
    return true;
}
bool arc_net_join(const char *address) {
    net_machine *N = NET;
    if (N->replay) { net_set_message("a replay cannot join"); return false; }
    if (N->mode == MODE_HOST) { net_set_message("already hosting"); printf("ARC error net join: %s\n", N->message); fflush(stdout); return false; }
    if (address && !N->memory && (strstr(address, "://") || (!NET_SOCKETS && net_relay_list()[0])))   /* 0.8.5: a relay room */
        return arc_net_join_online(address);
    char host[100]; int port;
    if (!address || !net_parse_addr(address, host, sizeof host, &port)) { net_set_message("bad address '%s'", address ? address : ""); printf("ARC error net join: %s\n", N->message); fflush(stdout); return false; }
    bool rejoin = N->mode == MODE_CLIENT && (N->status == ARC_NET_LOST || N->status == ARC_NET_JOINING) && !strcmp(address, N->join_addr);
    if (N->mode == MODE_CLIENT && !rejoin) arc_net_leave();
    bool mem = !strcmp(host, "mem");
    uint32_t ip = 0;
    if (!mem) {
#if NET_SOCKETS
        if (!ns_init()) { N->status = ARC_NET_UNAVAILABLE; net_set_message("networking is not available (ws2_32)"); return false; }
        if (!strcmp(host, "localhost")) ip = 0x7F000001u;
        else if (!net_dotted(host, &ip) && !ns_resolve(host, &ip)) { net_set_message("cannot find host '%s'", host); printf("ARC error net join: %s\n", N->message); fflush(stdout); return false; }
#else
        N->status = ARC_NET_UNAVAILABLE; net_set_message("multiplayer is not available in the web build (browsers cannot accept connections)");
        printf("ARC error net %s\n", N->message); fflush(stdout); return false;
#endif
    }
    if (!rejoin) {
        if (!N->desc_set && !N->d_game[0]) net_fill_desc(NULL);
        net_begin_session(MODE_CLIENT); N->session = 0; N->local = -1;
        view_reset(&N->V, true, 0);
    }
    N->memory = N->memory || mem;
    snprintf(N->join_addr, sizeof N->join_addr, "%s", address); N->join_ip = ip; N->join_port = port;
    N->auto_retry = false;
    if (!client_open()) {
        if (rejoin) { N->status = ARC_NET_LOST; return false; }
        N->mode = MODE_OFFLINE; N->status = ARC_NET_OFFLINE; net_set_message("cannot connect to %s", address);
        printf("ARC error net join: %s\n", N->message); fflush(stdout); return false;
    }
    net_out("ARC log net %s %s\n", rejoin ? "rejoining" : "joining", address);
    return true;
}
void arc_net_leave(void) {
    net_machine *N = NET;
    if (N->rb) rb_end();                               /* 0.8.1: tell the match, close its socket */
    if (N->mode == MODE_HOST) {
        for (int p = 1; p < NET_MAXP; p++) if (N->conns[p].state == C_LIVE) { m_begin(M_BYE); m_str("the host left"); conn_queue_now(&N->conns[p]); conn_pump(&N->conns[p]); }
        for (int p = 0; p < NET_MAXP; p++) { conn_close(&N->conns[p]); conn_close(&N->pend[p]); }
#if NET_SOCKETS
        if (N->lsock != NET_BAD) ns_close(N->lsock);
        if (N->dsock != NET_BAD) ns_close(N->dsock);
#endif
        N->lsock = N->dsock = NET_BAD;
        int mh = memhost_find(N->port); if (mh >= 0 && g_memhosts[mh].host == N) { for (int i = 0; i < g_memhosts[mh].npend; i++) g_memhosts[mh].pend[i]->closed[1] = true; memset(&g_memhosts[mh], 0, sizeof g_memhosts[mh]); }
        net_out("ARC net stopped hosting\n");
    } else if (N->mode == MODE_CLIENT) {
        net_conn *c = &N->conns[0];
        if (c->state == C_LIVE) { m_begin(M_BYE); m_str("left"); conn_queue_now(c); conn_pump(c); }
        conn_close(c);
        net_out("ARC net left the %s\n", N->online ? "room" : "session");
    }
    N->mode = MODE_OFFLINE; N->status = ARC_NET_OFFLINE; N->local = 0; N->session = 0; N->auto_retry = false;
    N->online = false; N->room[0] = 0;
    log_clear(); view_reset(&N->V, false, g_seed); N->wait.kind = 0;
}
int arc_net_players(arc_net_player *out, int max) {
    net_machine *N = NET; int n = 0;
    for (int i = 0; i < NET_MAXP; i++) {
        if (!N->V.p[i].used) continue;
        if (out && n < max) {
            arc_net_player *p = &out[n]; memset(p, 0, sizeof *p);
            p->index = i; snprintf(p->name, sizeof p->name, "%s", N->V.p[i].name);
            p->connected = N->V.p[i].connected; p->ready = N->V.p[i].ready; p->in_match = N->V.p[i].in_match; p->local = i == N->local;
            p->ping_ms = N->ping_table[i];
        }
        n++;
    }
    return n;
}
int  arc_net_local_player(void) { return NET->replay ? NET->replay_local : NET->mode == MODE_OFFLINE ? 0 : NET->local; }
const char *arc_net_name(void) { net_default_name(); return NET->my_name; }
static bool net_client_send(int kind, int flags, const char *text) {
    net_machine *N = NET; net_conn *c = &N->conns[0];
    if (c->state != C_LIVE) { net_set_message("not connected"); return false; }
    m_begin(M_CMD); m_u8((unsigned)flags); m_u8((unsigned)kind); m_str(text); conn_queue(c);
    N->inflight++;
    return true;
}
void arc_net_set_name(const char *name) {
    net_machine *N = NET;
    char nm[32]; snprintf(nm, sizeof nm, "%s", name ? name : "");
    for (char *p = nm; *p; p++) if ((unsigned char)*p < 32) *p = ' ';
    if (!nm[0]) return;
    snprintf(N->my_name, sizeof N->my_name, "%s", nm);
    if (net_by_tick()) return;
    if (N->mode == MODE_HOST) host_assign(NK_NAME, 0, ARC_NET_FREE, nm);
    else if (N->mode == MODE_CLIENT && N->conns[0].state == C_LIVE) net_client_send(NK_NAME, ARC_NET_FREE, nm);
}
void arc_net_set_ready(bool ready) {
    net_machine *N = NET;
    if (net_by_tick()) return;
    if (N->mode == MODE_HOST) host_assign(NK_READY, 0, ARC_NET_FREE, ready ? "1" : "0");
    else if (N->mode == MODE_CLIENT) net_client_send(NK_READY, ARC_NET_FREE, ready ? "1" : "0");
}
bool arc_net_start(void) {
    net_machine *N = NET;
    bool room = net_room_host();                       /* 0.8.5: the relay room's host asks the relay to start it */
    if (N->mode != MODE_HOST && !room) { net_set_message("only the host starts the match"); printf("ARC error net start: %s\n", N->message); fflush(stdout); return false; }
    if (room ? N->V.started : N->AV.started) { net_set_message("the match has started already"); return false; }
    if (room && !client_live()) { net_set_message("not connected to the relay"); printf("ARC error net start: %s\n", N->message); fflush(stdout); return false; }
    uint64_t seed = N->desc.seed;
    if (!seed) { arc_rng r; arc_rng_seed(&r, (uint64_t)(arc__plat_time() * 1e6) ^ N->session, 5); seed = ((uint64_t)arc_rng_u32(&r) << 21) ^ arc_rng_u32(&r); if (!seed) seed = 1; }
    char t[64]; snprintf(t, sizeof t, "%llu%s%s", (unsigned long long)seed, N->desc.mode == ARC_NET_ROLLBACK ? " rollback" : "",   /* (0.10.4: in a relay room too) */
                         N->desc.turns == ARC_NET_TURNS_SIMULTANEOUS ? " simultaneous" : "");
    if (room) return net_client_send(NK_START, ARC_NET_FREE, t);
    host_assign(NK_START, 0, ARC_NET_FREE, t);
    return true;
}
bool arc_net_started(void) { return NET->V.started; }
uint64_t arc_net_seed(void) { return NET->V.seed; }
static void net_refuse_local(const char *why, const char *text) {
    net_set_message("%s", why);
    if (!net_quiet()) { printf("ARC error net refused (%s): %s\n", why, text); fflush(stdout); }
    char t[160]; snprintf(t, sizeof t, "%.30s: %.120s", why, text);
    if (!net_by_tick()) ev_push(ARC_NET_EV_REFUSED, arc_net_local_player(), 0, false, t);
}
bool arc_net_command_ex(const char *text, int flags) {
    net_machine *N = NET;
    if (!text || !*text) return false;
    if (strlen(text) > NET_TEXT_MAX || strchr(text, '\n') || strchr(text, '\r')) { net_refuse_local("a command is one line of at most 4095 characters", text); return false; }
    bool by_tick = net_by_tick();
    if (N->mode == MODE_OFFLINE && !(N->replay && N->replay_session)) {
        if (by_tick || (N->replay)) return true;       /* the log already holds what this call queued the first time */
        log_append((uint32_t)N->log_n + 1, 0, flags, NK_CMD, text, NET_NO_TICK);
        return true;
    }
    /* in a session: check the turn here too (the host decides; this catches the obvious case at once) */
    const net_view *v = N->mode == MODE_HOST ? &N->AV : &N->V;
    int me = arc_net_local_player();
    if (!(flags & ARC_NET_FREE)) {
        if (!v->started) { net_refuse_local("the match has not started", text); return false; }
        if (v->simul) {                                /* 0.8.5: anyone in the match, until they end the turn */
            if (me >= 0 && me < NET_MAXP && (v->done & (1u << me))) { net_refuse_local("you have already ended this turn", text); return false; }
        } else if (v->owner != me) { net_refuse_local("not your turn", text); return false; }
    }
    if (by_tick) return true;                         /* re-simulating: the log has it */
    if (N->mode == MODE_HOST) { host_assign(NK_CMD, 0, flags, text); return true; }
    if (!net_client_send(NK_CMD, flags, text)) { net_refuse_local("not connected", text); return false; }
    return true;
}
bool arc_net_command(const char *text) { return arc_net_command_ex(text, 0); }
int  arc_net_command_from(void) { return NET->applying ? NET->cur_from : -1; }
bool arc_net_applying(void) { return NET->applying; }
bool arc_net_catching_up(void) { return NET->cur_backlog || g_resim; }
int  arc_net_turn_owner(void) { return net_session() ? (NET->V.started ? (NET->V.simul ? ARC_NET_EVERYONE : NET->V.owner) : -1) : 0; }
int  arc_net_turn(void) { return NET->V.turn; }
bool arc_net_my_turn(void) {
    net_machine *N = NET;
    if (!net_session()) return true;
    if (!N->replay && N->mode == MODE_CLIENT && !client_live()) return false;   /* (can't act while the link is down) */
    if (!N->replay && N->mode == MODE_CLIENT && (N->catching || (uint32_t)N->napplied < N->backlog_end)) return false;   /* nor before the backlog is in */
    int me = arc_net_local_player();
    if (N->V.started && N->V.simul) return me >= 0 && me < NET_MAXP && N->V.p[me].in_match && !(N->V.done & (1u << me));   /* 0.8.5 */
    return N->V.started && N->V.owner == me;
}
unsigned arc_net_turn_waiting(void) {
    net_machine *N = NET;
    if (!net_session()) return 1u;
    if (!N->V.started) return 0;
    if (N->V.simul) return ncore_turn_pending(&N->V);
    return N->V.owner >= 0 && N->V.owner < NET_MAXP ? 1u << N->V.owner : 0;
}
uint32_t arc_net_seq(void) { return (uint32_t)NET->napplied; }
bool arc_net_turn_end(void) {
    net_machine *N = NET;
    if (N->mode == MODE_OFFLINE && !(N->replay && N->replay_session)) {
        if (net_by_tick() || N->replay) return true;
        log_append((uint32_t)N->log_n + 1, 0, 0, NK_TURN, "", NET_NO_TICK); return true;
    }
    const net_view *v = N->mode == MODE_HOST ? &N->AV : &N->V;
    int me = arc_net_local_player();
    bool for_absent = (N->mode == MODE_HOST || net_room_host()) && v->started && !v->simul && v->owner != me && v->owner >= 0 && !v->p[v->owner].connected;   /* the host passes a missing player's turn */
    if (!v->started) { net_refuse_local("the match has not started", "end_turn"); return false; }
    if (v->simul) {                                    /* 0.8.5: every player ends it once */
        if (me < 0 || me >= NET_MAXP || (v->done & (1u << me))) { net_refuse_local("you have already ended this turn", "end_turn"); return false; }
    } else if (v->owner != me && !for_absent) { net_refuse_local("not your turn", "end_turn"); return false; }
    if (net_by_tick()) return true;
    if (N->mode == MODE_HOST) { host_assign(NK_TURN, 0, 0, ""); return true; }
    if (!net_client_send(NK_TURN, 0, "")) { net_refuse_local("not connected", "end_turn"); return false; }
    return true;
}
/* ---- discovery ---- */
void arc_net_find(void) {
    net_machine *N = NET;
    N->nfound = 0; N->find_until = net_now() + 1.5; N->find_next = 0;
    if (N->memory) return;
#if NET_SOCKETS
    if (!ns_init()) { N->find_until = 0; return; }
    if (N->fsock == NET_BAD) N->fsock = ns_udp(0, true);
    if (N->fsock == NET_BAD) N->find_until = 0;
#else
    N->find_until = 0;
#endif
}
bool arc_net_finding(void) { return NET->find_until > 0; }
int  arc_net_games(arc_net_game *out, int max) {
    int n = NET->nfound < max ? NET->nfound : max;
    if (out) for (int i = 0; i < n; i++) out[i] = NET->found[i];
    return NET->nfound;
}

/* ---- the command log as text ---- */
int arc_net_log_count(void) { return NET->log_n; }
const char *arc_net_log_line(int i) {
    if (i < 0 || i >= NET->log_n) return NULL;
    char b[NET_TEXT_MAX + 64]; net_log_line(&NET->log[i], b, sizeof b); return arc_fmt("%s", b);
}
bool arc_net_log_save(const char *path) {
    net_machine *N = NET;
    FILE *f = fopen(path, "wb"); if (!f) return false;
    fprintf(f, "ARC net log 1\nsession %016llx\ngame %s\nversion %s\narc %s\n---\n", (unsigned long long)N->session, N->d_game, N->d_version, ARC_VERSION);
    char b[NET_TEXT_MAX + 64];
    for (int i = 0; i < N->log_n; i++) { net_log_line(&N->log[i], b, sizeof b); fprintf(f, "%s\n", b); }
    return fclose(f) == 0;
}
static int nk_from_name(const char *s) { for (int k = 0; k < NK_COUNT; k++) if (!strcmp(s, nk_names[k])) return k; return -1; }
/* "seq from kind flags text" (+ "tick" first in a replay's .net file); false at a bad line */
static bool net_parse_line(char *line, bool with_tick, uint64_t *tick, uint32_t *seq, int *from, int *kind, int *flags, char **text) {
    size_t L = strlen(line); while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = 0;
    char *p = line, *end;
    if (with_tick) { *tick = strtoull(p, &end, 10); if (end == p) return false; p = end; }
    *seq = (uint32_t)strtoul(p, &end, 10); if (end == p) return false; p = end;
    *from = (int)strtol(p, &end, 10); if (end == p) return false; p = end;
    while (*p == ' ') p++;
    char kn[16]; size_t k = 0; while (*p && *p != ' ' && k < sizeof kn - 1) kn[k++] = *p++; kn[k] = 0;
    *kind = nk_from_name(kn); if (*kind < 0) return false;
    *flags = (int)strtol(p, &end, 10); if (end == p) return false; p = end;
    if (*p == ' ') p++;
    *text = p;
    return true;
}
bool arc_net_resume(const char *path) {
    net_machine *N = NET;
    if (N->mode != MODE_HOST || N->AV.started || N->log_n > 1) { net_set_message("resume: host first, before anyone joins or the match starts"); printf("ARC error net resume: %s\n", N->message); fflush(stdout); return false; }
    FILE *f = fopen(path, "rb"); if (!f) { net_set_message("cannot read %s", path); printf("ARC error net resume: %s\n", N->message); fflush(stdout); return false; }
    char line[NET_TEXT_MAX + 128]; bool body = false, ok = true; uint64_t session = 0; int count = 0;
    log_clear(); view_reset(&N->AV, true, N->session);
    while (fgets(line, sizeof line, f)) {
        if (!body) {
            if (!strncmp(line, "session ", 8)) session = strtoull(line + 8, NULL, 16);
            if (!strncmp(line, "game ", 5)) { char g[64]; snprintf(g, sizeof g, "%.63s", line + 5); g[strcspn(g, "\r\n")] = 0; if (strcmp(g, N->d_game)) { net_set_message("the log is from another game (%s)", g); ok = false; break; } }
            if (!strncmp(line, "---", 3)) body = true;
            continue;
        }
        uint64_t tk; uint32_t seq; int from, kind, flags; char *text;
        if (!net_parse_line(line, false, &tk, &seq, &from, &kind, &flags, &text)) continue;
        if (seq != (uint32_t)N->log_n + 1) { net_set_message("the log skips from %d to %u", N->log_n, seq); ok = false; break; }
        log_append(seq, from, flags, kind, text, NET_NO_TICK)->backlog = 1;
        view_apply(&N->AV, kind, from, text); count++;
    }
    fclose(f);
    if (!ok || !count) {
        if (ok) net_set_message("the log %s is empty", path);
        printf("ARC error net resume: %s\n", N->message); fflush(stdout);
        log_clear(); view_reset(&N->AV, true, N->session); host_assign(NK_JOIN, 0, 0, N->my_name); return false;
    }
    if (session) N->session = session;
    view_reset(&N->V, true, N->session);
    N->backlog_end = (uint32_t)N->log_n;
    for (int p = 1; p < NET_MAXP; p++) if (N->AV.p[p].used && N->AV.p[p].connected) host_assign(NK_LEAVE, p, 0, "");   /* nobody is here yet */
    if (N->AV.p[0].used && !N->AV.p[0].connected) host_assign(NK_REJOIN, 0, 0, "");
    net_out("ARC net resumed %d commands from %s (players rejoin by name)\n", count, path);
    return true;
}

/* ---- recording: input.rec.net beside a recording; a replay applies it at the same ticks ---- */
void arc__net_rec_open(const char *rec_path) { snprintf(NET->rec_path, sizeof NET->rec_path, "%s.net", rec_path); }
static void net_rec_write(const net_entry *e) {
    net_machine *N = NET;
    if (!N->rec_path[0] || N->rec_failed || N->replay) return;
    if (!N->rec) {
        N->rec = fopen(N->rec_path, "wb");
        if (!N->rec) { N->rec_failed = true; return; }
        fprintf(N->rec, "ARC net rec 1 local=%d session=%d\n", arc_net_local_player(), N->mode != MODE_OFFLINE ? 1 : 0);
    }
    char b[NET_TEXT_MAX + 64]; net_log_line(e, b, sizeof b);
    fprintf(N->rec, "%llu %s\n", (unsigned long long)e->tick, b); fflush(N->rec);
}
static void net_rec_raw(const char *line) {             /* 0.8.1: rollback inputs ("I tick player hex") and drops */
    net_machine *N = NET;
    if (!N->rec_path[0] || N->rec_failed || N->replay) return;
    if (!N->rec) {
        N->rec = fopen(N->rec_path, "wb");
        if (!N->rec) { N->rec_failed = true; return; }
        fprintf(N->rec, "ARC net rec 1 local=%d session=%d\n", arc_net_local_player(), N->mode != MODE_OFFLINE ? 1 : 0);
    }
    fputs(line, N->rec); fputc('\n', N->rec);
}
void arc__net_replay_open(const char *replay_path) {
    net_machine *N = NET;
    char path[1100]; snprintf(path, sizeof path, "%s.net", replay_path);
    FILE *f = fopen(path, "rb"); if (!f) return;
    char line[NET_TEXT_MAX + 128]; int n = 0;
    if (fgets(line, sizeof line, f)) {
        const char *l = strstr(line, "local="), *s = strstr(line, "session=");
        N->replay_local = l ? atoi(l + 6) : 0; N->replay_session = s ? atoi(s + 8) != 0 : false;
    }
    view_reset(&N->V, N->replay_session, g_seed);
    while (fgets(line, sizeof line, f)) {
        uint64_t tk; uint32_t seq; int from, kind, flags; char *text;
        if (rb_replay_line(line)) continue;             /* 0.8.1: a rollback match's inputs */
        if (!net_parse_line(line, true, &tk, &seq, &from, &kind, &flags, &text)) continue;
        log_append(seq, from, flags, kind, text, tk); n++;
    }
    fclose(f);
    N->replay = true; N->touched = true;
    printf("ARC log net replay: %d commands from %s\n", n, path); fflush(stdout);
}

/* ---- ARC's state snapshots (seek, rewind, state_save): the session view and how far the log has been applied ---- */
#define NET_STATE_MAGIC 0x3154454Eu   /* "NET1" */
size_t arc__net_state(arc_writer *w) {
    net_machine *N = NET;
    if (!N->touched) return w->len;
    uint32_t magic = NET_STATE_MAGIC, na = (uint32_t)N->napplied;
    arc_write(w, &magic, 4); arc_write(w, &na, 4); view_write(w, &N->V);
    return w->len;
}
bool arc__net_state_read(arc_reader *r, bool commit) {
    net_machine *N = NET;
    uint32_t peek = 0; if (r->pos + 4 <= r->size) memcpy(&peek, r->data + r->pos, 4);
    if (r->pos >= r->size || peek == 0x31564153u || peek == 0x31544853u) {   /* (0.10.0: or the shots' block) */   /* saved before any net use (0.8.1: the replayed loads' block may follow) */
        if (commit && N->touched) {
            N->napplied = 0; view_reset(&N->V, net_session(), N->mode == MODE_OFFLINE ? g_seed : N->session);
            if (N->mode == MODE_OFFLINE && !N->replay) { N->log_n = 0; N->text.n = 0; }
        }
        return true;
    }
    uint32_t magic = 0, na = 0;
    if (!arc_read(r, &magic, 4) || magic != NET_STATE_MAGIC || !arc_read(r, &na, 4)) { r->fail = true; return false; }
    net_view v; if (!view_read(r, &v)) return false;
    if (na > (uint32_t)N->log_n) { r->fail = true; return false; }
    if (!commit) return true;
    N->napplied = (int)na; N->V = v;
    if (N->mode == MODE_OFFLINE && !N->replay) { N->log_n = (int)na; N->text.n = na ? N->log[na - 1].off + N->log[na - 1].len + 1 : 0; }   /* offline: tick() makes the rest again */
    return true;
}

/* ---- dump ---- */
void arc__net_dump_json(arc_json *j) {
    net_machine *N = NET;
    if (!N->touched && N->status == ARC_NET_OFFLINE) return;
    arc_json_obj_k(j, "net");
    arc_json_str_k(j, "status", arc_net_status_name(N->status));
    if (N->message[0]) arc_json_str_k(j, "message", N->message);
    arc_json_bool_k(j, "host", arc_net_is_host());
    arc_json_int_k(j, "local", arc_net_local_player());
    arc_json_int_k(j, "seq", N->napplied); arc_json_int_k(j, "received", N->log_n);
    arc_json_bool_k(j, "started", N->V.started);
    arc_json_int_k(j, "seed", (long long)N->V.seed);
    arc_json_int_k(j, "turn", N->V.turn); arc_json_int_k(j, "owner", arc_net_turn_owner());
    arc_json_str_k(j, "last_hash", arc_fmt("%016llx", (unsigned long long)N->last_hash)); arc_json_int_k(j, "last_hash_seq", N->last_hash_seq);
    if (N->V.simul) { arc_json_str_k(j, "turns", "simultaneous"); arc_json_int_k(j, "waiting", (long long)arc_net_turn_waiting()); }   /* 0.8.5 */
    if (N->online) { arc_json_str_k(j, "room", N->room); arc_json_str_k(j, "relay", N->relay_url); }
    if (N->desync_seq) arc_json_int_k(j, "desync_seq", N->desync_seq);
    arc_json_arr_k(j, "players");
    for (int i = 0; i < NET_MAXP; i++) if (N->V.p[i].used) {
        arc_json_obj(j); arc_json_int_k(j, "index", i); arc_json_str_k(j, "name", N->V.p[i].name);
        arc_json_bool_k(j, "connected", N->V.p[i].connected); arc_json_bool_k(j, "ready", N->V.p[i].ready); arc_json_bool_k(j, "in_match", N->V.p[i].in_match);
        arc_json_int_k(j, "ping_ms", N->ping_table[i]); arc_json_end(j);
    }
    arc_json_end(j);
    rb_dump(j);                                        /* 0.8.1 */
    arc_json_end(j);
}

/* ---- --net-sim ---- */
void arc_net_sim(const char *spec) {
    net_machine *N = NET;
    double timeout = N->sim.timeout;
    memset(&N->sim, 0, sizeof N->sim); N->sim.timeout = timeout > 0 ? timeout : 10.0;
    uint64_t seed = 1;
    if (!spec || !*spec) { arc_rng_seed(&N->sim.rng, seed, 77); return; }
    char buf[256]; snprintf(buf, sizeof buf, "%s", spec);
    for (char *p = strtok(buf, ", "); p; p = strtok(NULL, ", ")) {
        char *eq = strchr(p, '='); if (!eq) continue; *eq = 0; const char *v = eq + 1;
        if (!strcmp(p, "lag")) N->sim.lag = atof(v);
        else if (!strcmp(p, "jitter")) N->sim.jitter = atof(v);
        else if (!strcmp(p, "loss")) N->sim.loss = arc_clampf((float)atof(v), 0, 0.95f);
        else if (!strcmp(p, "drop_at")) N->sim.drop_at = strtoull(v, NULL, 10);
        else if (!strcmp(p, "rejoin_at")) N->sim.rejoin_at = strtoull(v, NULL, 10);
        else if (!strcmp(p, "timeout")) N->sim.timeout = atof(v) * 0.001;
        else if (!strcmp(p, "seed")) seed = strtoull(v, NULL, 10);
        else if (!strcmp(p, "dup")) N->sim.dup = arc_clampf((float)atof(v), 0, 0.95f);          /* 0.8.1: rollback datagrams */
        else if (!strcmp(p, "freeze_at")) N->sim.freeze_at = strtoull(v, NULL, 10);
        else if (!strcmp(p, "freeze_ms")) N->sim.freeze_ms = atof(v);
        else { printf("ARC error net-sim: unknown setting '%s' (lag jitter loss dup drop_at rejoin_at freeze_at freeze_ms timeout seed)\n", p); fflush(stdout); }
    }
    N->sim.on = N->sim.lag > 0 || N->sim.jitter > 0 || N->sim.loss > 0 || N->sim.drop_at || N->sim.rejoin_at || N->sim.dup > 0 || N->sim.freeze_at;
    arc_rng_seed(&N->sim.rng, seed, 77);
}
void arc__net_startup(void) {
    net_machine *N = NET;
    N->lsock = N->dsock = N->fsock = NET_BAD; N->cur_from = -1; N->sim.timeout = 10.0;
    for (int i = 0; i < NET_MAXP; i++) { N->conns[i].fd = NET_BAD; N->pend[i].fd = NET_BAD; N->ping_table[i] = -1; }
    view_reset(&N->V, false, g_seed);
    g_net_verbose = arc_arg_flag("net-verbose");
    N->cfg_delay = -2; N->cfg_window = -1; N->cfg_mode = -1;
    const char *v;
    if ((v = arc_arg_value("net-report"))) snprintf(g_net_report, sizeof g_net_report, "%s", v);
    if ((v = arc_arg_value("net-sim"))) arc_net_sim(v);
    if (!N->sim.on) arc_rng_seed(&N->sim.rng, 1, 77);   /* 0.10.4 --net-lag MS, --net-loss PCT, --net-jitter MS: the same, one at a time */
    if ((v = arc_arg_value("net-lag"))) N->sim.lag = atof(v);
    if ((v = arc_arg_value("net-jitter"))) N->sim.jitter = atof(v);
    if ((v = arc_arg_value("net-loss"))) N->sim.loss = arc_clampf((float)atof(v) * 0.01f, 0, 0.95f);
    if (N->sim.lag > 0 || N->sim.jitter > 0 || N->sim.loss > 0) N->sim.on = true;
    if ((v = arc_arg_value("net-name"))) snprintf(N->my_name, sizeof N->my_name, "%s", v);
    if ((v = arc_arg_value("net-relay"))) snprintf(N->flag_relay, sizeof N->flag_relay, "%s", v);    /* 0.8.5 */
    if ((v = arc_arg_value("net-delay"))) N->cfg_delay = arc_clampi(atoi(v), 0, 10);           /* 0.8.1 */
    if ((v = arc_arg_value("net-rollback"))) N->cfg_window = arc_clampi(atoi(v), 0, ARC_NET_MAX_ROLLBACK);
    if ((v = arc_arg_value("net-mode"))) N->cfg_mode = !strcmp(v, "rollback") ? ARC_NET_ROLLBACK : ARC_NET_TURNS;
}
void arc__net_shutdown(void) {
    if (NET->mode != MODE_OFFLINE) arc_net_leave();
    if (NET->rec) { fclose(NET->rec); NET->rec = NULL; }
}

/* ======================================================================== */
/* Agent commands: net ...                                                  */
/* ======================================================================== */
enum { W_NONE, W_STARTED, W_TURN, W_CONNECTED, W_PLAYERS, W_SEQ, W_DESYNC, W_STATUS, W_TURNNO, W_SYNC,
       W_TICK, W_CONFIRMED, W_WAITING, W_RESUMED, W_DROPPED, W_ALONE, W_TIME, W_COUNT };   /* (0.8.1: the rollback ones; 0.8.5: time) */
static const char *const w_names[] = { "", "started", "turn", "connected", "players", "seq", "desync", "status", "turn_number", "sync",
                                       "tick", "confirmed", "waiting", "resumed", "dropped", "alone", "time" };
static bool net_wait_cond(void) {
    net_machine *N = NET;
    switch (N->wait.kind) {
    case W_STARTED: return N->V.started && N->napplied == N->log_n;
    case W_TURN: return arc_net_my_turn() && N->inflight == 0 && N->napplied == N->log_n;
    case W_CONNECTED: return (N->status == ARC_NET_CONNECTED || N->status == ARC_NET_HOSTING) && N->napplied == N->log_n;
    case W_PLAYERS: { int n = 0; for (int i = 0; i < NET_MAXP; i++) n += N->V.p[i].used && N->V.p[i].connected; return n == N->wait.arg; }
    case W_SEQ: return N->napplied >= N->wait.arg;
    case W_DESYNC: return N->desync_done || rb_wait_cond(W_DESYNC, 0);
    case W_STATUS: return !strcmp(arc_net_status_name(N->status), N->wait.name);
    case W_TURNNO: return N->V.turn >= N->wait.arg && N->napplied == N->log_n;
    case W_SYNC:
        if (N->mode == MODE_HOST) return N->napplied >= N->log_n;
        if (N->mode != MODE_CLIENT) return true;
        return N->sync_known && (uint32_t)N->napplied >= N->sync_target && N->inflight == 0;
    case W_TICK: case W_CONFIRMED: case W_WAITING: case W_RESUMED: case W_DROPPED: case W_ALONE: return rb_wait_cond(N->wait.kind, N->wait.arg);
    case W_TIME: return net_now() >= N->wait.deadline;   /* 0.8.5: real seconds, the network running meanwhile */
    default: return true;
    }
}
static bool net_wait_check(void) {
    net_machine *N = NET;
    if (!N->wait.kind) return false;
    if (net_wait_cond()) {
        printf("ARC ok net wait %s tick=%llu seq=%d\n", w_names[N->wait.kind], (unsigned long long)G.ticks, N->napplied); fflush(stdout);
        N->wait.kind = 0; return false;
    }
    if (net_now() > N->wait.deadline && N->wait.kind != W_TIME) {
        printf("ARC error net wait %s timed out (status %s, seq %d, turn %d owner %d)\n", w_names[N->wait.kind], arc_net_status_name(N->status), N->napplied, N->V.turn, N->V.owner); fflush(stdout);
        N->wait.kind = 0; return false;
    }
    return true;
}
bool arc__net_waiting(void) { return net_wait_check(); }
static void net_print_players(void) {
    arc_net_player ps[NET_MAXP]; int n = arc_net_players(ps, NET_MAXP);
    for (int i = 0; i < n; i++)
        printf("ARC net player %d name=\"%s\" connected=%d ready=%d in_match=%d local=%d ping=%d\n", ps[i].index, ps[i].name, ps[i].connected, ps[i].ready, ps[i].in_match, ps[i].local, ps[i].ping_ms);
}
void arc__net_agent(int argc, const char **argv, const char *line) {
    net_machine *N = NET;
    const char *sub = argc > 1 ? argv[1] : "status";
    const char *rest = line;                            /* the text after "net <sub> " */
    for (int k = 0; k < 2 && rest; k++) { rest = strchr(rest, ' '); if (rest) while (*rest == ' ') rest++; }
    if (!rest) rest = "";
    if (!strcmp(sub, "host")) {
        arc_net_desc d; memset(&d, 0, sizeof d);
        if (N->desc_set) d = N->desc;                  /* what the game set up, with the port (and player count) given here */
        if (argc > 2) d.port = atoi(argv[2]);
        if (argc > 3) d.max_players = atoi(argv[3]);
        if (arc_net_host(&d)) printf("ARC ok net host port=%d\n", N->port);
    } else if (!strcmp(sub, "join")) { if (argc < 3) printf("ARC error net join ADDRESS\n"); else if (arc_net_join(argv[2])) printf("ARC ok net join %s\n", argv[2]); }
    else if (!strcmp(sub, "leave")) { arc_net_leave(); printf("ARC ok net leave\n"); }
    else if (!strcmp(sub, "find")) {
        arc_net_find(); printf("ARC ok net find (answers arrive over about a second: `net games`)\n");
    } else if (!strcmp(sub, "games")) {
        arc_net_game gs[16]; int n = arc_net_games(gs, 16);
        for (int i = 0; i < n && i < 16; i++) printf("ARC net game address=%s name=\"%s\" game=\"%s\" version=\"%s\" arc=%s players=%d/%d started=%d\n", gs[i].address, gs[i].name, gs[i].game, gs[i].version, gs[i].arc_version, gs[i].players, gs[i].max_players, gs[i].started);
        printf("ARC ok net games %d%s\n", n, arc_net_finding() ? " (still searching)" : "");
    } else if (!strcmp(sub, "status")) {
        printf("ARC net status=%s local=%d host=%d seq=%d received=%d started=%d turn=%d owner=%d inflight=%d hash=%016llx%s%s%s%s\n", arc_net_status_name(N->status), arc_net_local_player(),
               arc_net_is_host(), N->napplied, N->log_n, N->V.started, N->V.turn, arc_net_turn_owner(), N->inflight, (unsigned long long)N->last_hash,
               N->online ? " room=" : "", N->online ? N->room : "", N->message[0] ? " message=" : "", N->message);
    } else if (!strcmp(sub, "host_online")) { if (arc_net_host_online()) printf("ARC ok net host_online %s\n", N->relay_url); }   /* 0.8.5 */
    else if (!strcmp(sub, "join_online")) { if (argc < 3) printf("ARC error net join_online CODE\n"); else if (arc_net_join_online(argv[2])) printf("ARC ok net join_online %s\n", N->room); }
    else if (!strcmp(sub, "room")) { printf("ARC net room %s relay=%s player=%d\n", arc_net_room()[0] ? arc_net_room() : "-", N->online ? N->relay_url : "-", arc_net_local_player()); printf("ARC ok net room\n");
    } else if (!strcmp(sub, "players")) { net_print_players(); printf("ARC ok net players\n"); }
    else if (!strcmp(sub, "name")) { if (argc > 2) arc_net_set_name(rest); printf("ARC ok net name %s\n", arc_net_name()); }
    else if (!strcmp(sub, "ready")) { bool on = argc > 2 ? atoi(argv[2]) != 0 : true; arc_net_set_ready(on); printf("ARC ok net ready %d\n", on); }
    else if (!strcmp(sub, "start")) { if (arc_net_start()) printf("ARC ok net start\n"); }
    else if (!strcmp(sub, "cmd") || !strcmp(sub, "free")) {
        if (!*rest) printf("ARC error net %s TEXT\n", sub);
        else if (arc_net_command_ex(rest, !strcmp(sub, "free") ? ARC_NET_FREE : 0)) printf("ARC ok net %s %s\n", sub, rest);
    } else if (!strcmp(sub, "end_turn")) { if (arc_net_turn_end()) printf("ARC ok net end_turn\n"); }
    else if (!strcmp(sub, "log")) {
        if (argc > 2) { if (arc_net_log_save(argv[2])) printf("ARC ok net log %s (%d commands)\n", argv[2], N->log_n); else printf("ARC error net log cannot write %s\n", argv[2]); }
        else { char b[NET_TEXT_MAX + 64]; for (int i = 0; i < N->log_n; i++) { net_log_line(&N->log[i], b, sizeof b); printf("ARC net log %s\n", b); } printf("ARC ok net log %d\n", N->log_n); }
    } else if (!strcmp(sub, "resume")) { if (argc < 3) printf("ARC error net resume FILE\n"); else if (arc_net_resume(argv[2])) printf("ARC ok net resume %s\n", argv[2]); }
    else if (!strcmp(sub, "sim")) { arc_net_sim(rest); printf("ARC ok net sim %s\n", N->sim.on ? rest : "off"); }
    else if (!strcmp(sub, "sync")) {
        N->wait.kind = W_SYNC; N->wait.deadline = net_now() + 60.0;
        if (N->mode == MODE_CLIENT) { N->sync_known = false; N->sync_id++; if (N->conns[0].state == C_LIVE) { m_begin(M_SYNC); m_u32(N->sync_id); conn_queue(&N->conns[0]); } }
    } else if (!strcmp(sub, "wait")) {
        const char *what = argc > 2 ? argv[2] : "";
        int k = 0; for (int i = 1; i < W_COUNT; i++) if (!strcmp(what, w_names[i])) k = i;
        if (!k || k == W_SYNC) { printf("ARC error net wait started|turn|turn_number N|connected|players N|seq N|desync|status NAME|tick N|confirmed N|waiting|resumed|dropped|alone [SECONDS] | time SECONDS\n"); fflush(stdout); return; }
        int ai = 3;
        N->wait.arg = 0; N->wait.name[0] = 0;
        if (k == W_PLAYERS || k == W_SEQ || k == W_TURNNO || k == W_TICK || k == W_CONFIRMED) { N->wait.arg = argc > 3 ? atoll(argv[3]) : 1; ai = 4; }
        if (k == W_STATUS) { snprintf(N->wait.name, sizeof N->wait.name, "%s", argc > 3 ? argv[3] : "connected"); ai = 4; }
        if (k == W_TIME) ai = 3;                        /* net wait time SECONDS */
        N->wait.kind = k; N->wait.deadline = net_now() + (argc > ai ? atof(argv[ai]) : k == W_TIME ? 1.0 : 60.0);
    } else if (rb_agent(sub, argc, argv, rest)) {        /* 0.8.1: mode, delay, rollback, input, stop_at, stats, synctest */
    } else printf("ARC error net: host [PORT] | join ADDR | host_online | join_online CODE | room | find | games | leave | status | players | name NAME | ready [0|1] | start | cmd TEXT | free TEXT | "
                  "end_turn | log [FILE] | resume FILE | sim SPEC | sync | wait started|turn|connected|players N|seq N|desync|status NAME|tick N|confirmed N|waiting|resumed|dropped|alone [SECONDS] | "
                  "mode rollback|turns | delay N | rollback N | input [P] HEX|off | stop_at T | stats | synctest N [PLAYERS]\n");
    fflush(stdout);
}

/* ======================================================================== */
/* The ready-made lobby (UI kit)                                            */
/* ======================================================================== */
int arc_net_lobby(const arc_net_desc *desc) {
    net_machine *N = NET;
    if (N->V.started && net_session()) return ARC_NET_LOBBY_STARTED;
    if (desc && N->mode == MODE_OFFLINE) { net_fill_desc(desc); N->desc_set = true; }
    if (!N->desc_set && N->mode == MODE_OFFLINE) { net_fill_desc(NULL); N->desc_set = true; }
    if (!N->lobby.init) {
        N->lobby.init = true; net_default_name();
        snprintf(N->lobby.name, sizeof N->lobby.name, "%s", N->my_name); snprintf(N->lobby.addr, sizeof N->lobby.addr, "localhost");
    }
    float k = arc_ui_scale() * arc_dpi_scale();
    float w = arc_minf(440.0f * k, (float)arc_width() - 16.0f), x = ((float)arc_width() - w) * 0.5f, y = arc_minf(40.0f * k, (float)arc_height() * 0.05f);
    int result = ARC_NET_LOBBY_OPEN;
    bool lan = NET_SOCKETS || N->memory;               /* (the web build has no LAN games) */
    if (N->mode == MODE_OFFLINE) {
        if (!N->lobby.searched && !N->replay && lan) { N->lobby.searched = true; arc_net_find(); }
        arc_ui_begin("Multiplayer", x, y, w);
        if (N->message[0]) arc_ui_label(N->message);
        if (arc_ui_text("Your name", N->lobby.name, sizeof N->lobby.name)) arc_net_set_name(N->lobby.name);
        if (arc_net_relay_available()) {               /* 0.8.5: online, through the relay */
            if (arc_ui_button("Host online")) { arc_net_set_name(N->lobby.name); arc_net_host_online(); }
            bool enter = arc_ui_text("Room code", N->lobby.code, (int)sizeof N->lobby.code - 9);
            /* with a gamepad (or a touch screen, where there is no keyboard) the code is typed on a grid of letter buttons:
               the UI kit's focus keys move across it, A presses */
            if (arc_pad_connected() || arc_touch_mode()) {
                arc_ui_columns(8);
                for (int i = 0; i < NCORE_CODE_ALPHA; i++)
                    if (arc_ui_button(arc_fmt("%c##codekey", ncore_code_alpha[i]))) { size_t n = strlen(N->lobby.code); if (n < NCORE_CODE_MAX) { N->lobby.code[n] = ncore_code_alpha[i]; N->lobby.code[n + 1] = 0; } }
                if (arc_ui_button("Del##codekey")) { size_t n = strlen(N->lobby.code); if (n) N->lobby.code[n - 1] = 0; }
                arc_ui_columns(1);
            }
            char norm[8]; bool valid = ncore_code_normalize(N->lobby.code, norm, sizeof norm);
            if ((arc_ui_button_ex("Join by code", valid) || (enter && valid))) { arc_net_set_name(N->lobby.name); arc_net_join_online(norm); }
            if (lan) arc_ui_separator();
        } else if (!lan) arc_ui_label("Online play needs a relay (arc_net_desc.relay)");
        if (!lan) {
            if (arc_ui_button("Back") || (arc_ui_back_pressed() && !arc_ui_wants_keyboard())) result = ARC_NET_LOBBY_BACK;
            arc_ui_end();
            return result;
        }
        if (arc_ui_button("Host a game")) { arc_net_set_name(N->lobby.name); arc_net_host(NULL); }
        arc_ui_separator();
        arc_net_game gs[16]; int n = arc_net_games(gs, 16), m = 0; const char *items[16];
        for (int i = 0; i < n && i < 16; i++) if (!strcmp(gs[i].game, N->d_game)) {
            gs[m] = gs[i];
            items[m] = arc_fmt("%s  %d/%d%s%s", gs[i].name, gs[i].players, gs[i].max_players, gs[i].started ? "  (playing)" : "",
                               strcmp(gs[i].version, N->d_version) || strcmp(gs[i].arc_version, ARC_VERSION) ? "  (other version)" : "");
            m++;
        }
        arc_ui_label(arc_net_finding() ? "Looking for games on this network..." : m ? "Games on this network:" : "No games found on this network");
        if (m) {
            if (N->lobby.sel >= m) N->lobby.sel = 0;
            arc_ui_list("##lan games", &N->lobby.sel, items, m);
            if (arc_ui_button("Join selected")) { arc_net_set_name(N->lobby.name); arc_net_join(gs[N->lobby.sel].address); }
        }
        if (arc_ui_button_ex("Search again", !arc_net_finding())) arc_net_find();
        arc_ui_separator();
        arc_ui_text("Address (host or host:port)", N->lobby.addr, sizeof N->lobby.addr);
        if (arc_ui_button("Join address")) { arc_net_set_name(N->lobby.name); arc_net_join(N->lobby.addr); }
        if (arc_ui_button("Back") || (arc_ui_back_pressed() && !arc_ui_wants_keyboard())) result = ARC_NET_LOBBY_BACK;
        arc_ui_end();
        return result;
    }
    if (N->mode == MODE_CLIENT && !client_live()) {
        arc_ui_begin("Multiplayer", x, y, w);
        if (N->status == ARC_NET_LOST) arc_ui_label(arc_fmt("Connection lost: %s%s", N->message, N->auto_retry ? " (reconnecting...)" : ""));
        else if (N->online) arc_ui_label(N->room[0] ? arc_fmt("Joining room %s...", N->room) : "Asking the relay for a room...");
        else arc_ui_label(arc_fmt("Connecting to %s...", N->join_addr));
        if (N->status == ARC_NET_LOST && arc_ui_button("Try again")) { if (N->online) arc_net_join_online(N->room); else arc_net_join(N->join_addr); }
        if (arc_ui_button("Cancel") || arc_ui_back_pressed()) arc_net_leave();
        arc_ui_end();
        return result;
    }
    arc_ui_begin(arc_fmt("Lobby: %s", N->d_name), x, y, w);
    if (N->online) arc_ui_label(arc_fmt("Room code: %s%s", N->room, net_room_host() ? "  (give it to the other players)" : ""));   /* 0.8.5 */
    int count = 0, ready = 0;
    for (int i = 0; i < NET_MAXP; i++) {
        if (!N->V.p[i].used) continue;
        bool c = N->V.p[i].connected;
        if (c) { count++; ready += N->V.p[i].ready; }
        int ping = N->ping_table[i];
        arc_ui_label(arc_fmt("%d. %s%s%s  %s%s", i + 1, N->V.p[i].name, i == 0 ? " (host)" : "", i == N->local ? " (you)" : "",
                             !c ? "gone" : N->V.p[i].ready ? "ready" : "not ready", c && i != 0 && ping >= 0 ? arc_fmt("  %d ms", ping) : ""));
    }
    bool me_ready = N->local >= 0 && N->V.p[N->local].ready;
    if (arc_ui_toggle("Ready", &me_ready)) arc_net_set_ready(me_ready);
    if (arc_net_is_host()) {
        bool can = count >= 2 && ready == count;
        if (arc_ui_button_ex("Start match", can)) arc_net_start();
        if (!can) arc_ui_label(count < 2 ? "Waiting for players..." : "Waiting for everyone to be ready");
    } else arc_ui_label("Waiting for the host to start");
    if (arc_ui_button("Leave") || arc_ui_back_pressed()) arc_net_leave();
    arc_ui_end();
    return result;
}

/* ======================================================================== */
/* Self-test hooks: several machines in one process over memory links       */
/* ======================================================================== */
static double g_net_test_time;
static double net_test_clock(void) { return g_net_test_time; }
void *arc__net_test_machine(void) {                  /* a new machine (memory links, the test clock) */
    net_machine *m = (net_machine *)arc__xalloc(sizeof *m);
    net_machine *save = NET; NET = m; arc__net_startup(); NET = save;
    m->memory = true; m->clock = net_test_clock; m->sim.timeout = 10.0;
    return m;
}
void *arc__net_test_use(void *m) { void *old = NET; NET = m ? (net_machine *)m : &g_net0; return old; }
void arc__net_test_time(double t) { g_net_test_time = t; }
void arc__net_test_free(void *m) {
    net_machine *N = (net_machine *)m, *save = NET; NET = N;
    arc_net_leave();
    if (N->rb) { rb_free(N->rb); N->rb = NULL; }
    free(N->log); nb_free(&N->text); nb_free(&N->sbuf); free(N->hring);
    NET = save; free(N);
}
int arc__net_test_inflight(void) { return NET->inflight; }
uint64_t arc__net_test_hash(void) { return NET->last_hash; }
uint32_t arc__net_test_desync_seq(void) { return NET->desync_seq; }
bool arc__net_test_desync_reported(void) { return NET->desync_done; }
void arc__net_test_drop(void) { client_lost("dropped by the test", false); }
void arc__net_test_report_dir(const char *dir) { snprintf(g_net_report, sizeof g_net_report, "%s", dir ? dir : "net_desync"); }
int  arc__net_test_status_refused(void) { return NET->status == ARC_NET_REFUSED; }
/* 0.8.5: a test machine on real sockets (the self-test's WebSocket handshake against a socket it listens on) */
void arc__net_test_sockets_on(void) { NET->memory = false; NET->lsock = NET->dsock = NET->fsock = NET_BAD; }
bool arc__net_test_auto_retry(void) { return NET->auto_retry; }
