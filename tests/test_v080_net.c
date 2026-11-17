/* ARC 0.8 Phase F1 self-test (multiplayer): included by test_core.c. v080n_init() runs in init, v080n_check() at tick 9.
   Part 1: the deterministic maths: accuracy against the C library (in double) over sweeps, special values, and golden
   hashes of the exact result bits over fixed sweeps (hard-coded: a compiler or platform that rounds differently fails).
   Part 2: sessions with several "machines" in this one process over in-process links (arc__net_test_machine), with a
   simulated bad network: joins, names, version refusal, lobby commands, start, alternating turns and refusals, hashes,
   a dropout skipped in the turn order and its rejoin, a restarted player catching up from nothing, a resumed log, a
   desync with its report, and offline use with ARC's snapshots (rewind). Part 3: the lobby UI driven by pretend ticks.
   Every name here starts with v080n_ (all test files share one translation unit). */
void arc__net_frame(void);
void arc__net_tick_begin(void);
void *arc__net_test_machine(void);
void *arc__net_test_use(void *m);
void arc__net_test_time(double t);
void arc__net_test_free(void *m);
int arc__net_test_inflight(void);
uint64_t arc__net_test_hash(void);
uint32_t arc__net_test_desync_seq(void);
bool arc__net_test_desync_reported(void);
void arc__net_test_drop(void);
void arc__net_test_report_dir(const char *dir);
int arc__net_test_status_refused(void);
size_t arc__net_state(arc_writer *w);
bool arc__net_state_read(arc_reader *r, bool commit);
void arc__test_sandbox(const arc_app *app);
void arc__test_ticks(int n);
void arc__test_command(const char *line);
void arc__input_test_step(void);
void arc__input_mouse(float x, float y);

/* ======================================================================== */
/* Part 1: maths                                                            */
/* ======================================================================== */
static uint32_t v080n_rs;
static uint32_t v080n_rnd(void) { v080n_rs ^= v080n_rs << 13; v080n_rs ^= v080n_rs >> 17; v080n_rs ^= v080n_rs << 5; return v080n_rs; }
static float v080n_arg(int i, float lo, float hi) {   /* even i: uniform in [lo, hi); odd: any float bits (NaN, inf, subnormal...) */
    uint32_t u = v080n_rnd();
    if (i & 1) { float f; memcpy(&f, &u, 4); return f; }
    return lo + (hi - lo) * (float)(u >> 8) * (1.0f / 16777216.0f);
}
static uint32_t v080n_bits(float f) { uint32_t u; if (f != f) return 0x7FC00000u; memcpy(&u, &f, 4); return u; }   /* NaN bits differ by CPU: one NaN */
static uint64_t v080n_mix(uint64_t h, uint32_t v) { for (int k = 0; k < 4; k++) { h ^= (v >> (8 * k)) & 0xFF; h *= 1099511628211ull; } return h; }
static int v080n_ulp(float a, float b) {
    if (a != a && b != b) return 0;
    if (a != a || b != b) return 1 << 30;
    if (a == b) return 0;
    int32_t ia, ib; memcpy(&ia, &a, 4); memcpy(&ib, &b, 4);
    if (ia < 0) ia = (int32_t)0x80000000 - ia;
    if (ib < 0) ib = (int32_t)0x80000000 - ib;
    long long d = (long long)ia - ib; if (d < 0) d = -d;
    return d > (1 << 30) ? (1 << 30) : (int)d;
}
static float v080n_dsin(float x) { return (float)sin((double)x); }
static float v080n_dcos(float x) { return (float)cos((double)x); }
static float v080n_dtan(float x) { return (float)tan((double)x); }
static float v080n_dasin(float x) { return (float)asin((double)x); }
static float v080n_dacos(float x) { return (float)acos((double)x); }
static float v080n_datan(float x) { return (float)atan((double)x); }
static float v080n_dexp(float x) { return (float)exp((double)x); }
static float v080n_dlog(float x) { return (float)log((double)x); }
static float v080n_dlog2(float x) { return (float)(log((double)x) / log(2.0)); }
static float v080n_dlog10(float x) { return (float)log10((double)x); }
static float v080n_datan2(float y, float x) { return (float)atan2((double)y, (double)x); }
static float v080n_dpow(float x, float y) { return (float)pow((double)x, (double)y); }
typedef struct { const char *name; float (*f)(float); float (*ref)(float); float lo, hi; uint64_t golden; } v080n_f1;
typedef struct { const char *name; float (*f)(float, float); float (*ref)(float, float); float lo, hi; uint64_t golden; } v080n_f2;
/* golden: FNV-1a 64 of the result bits over 4096 arguments from v080n_arg (seeded per function) */
static v080n_f1 v080n_f1s[] = {
    { "sin", arc_sinf, v080n_dsin, -100, 100, 0x9ddeae3c7dded13bull },  { "cos", arc_cosf, v080n_dcos, -100, 100, 0x7f64ba49a0b982d7ull },
    { "tan", arc_tanf, v080n_dtan, -10, 10, 0x79e80637ef8ed4baull },     { "asin", arc_asinf, v080n_dasin, -1, 1, 0xdf773d9a7d1a0c9dull },
    { "acos", arc_acosf, v080n_dacos, -1, 1, 0x4e73a17f396773e3ull },     { "atan", arc_atanf, v080n_datan, -50, 50, 0xe3219cfe66f7fa09ull },
    { "exp", arc_expf, v080n_dexp, -90, 88, 0x4875792554df180full },      { "log", arc_logf, v080n_dlog, 0, 1000, 0x44f85e60628c66e1ull },
    { "log2", arc_log2f, v080n_dlog2, 0, 1000, 0x66175541a1ec2addull },   { "log10", arc_log10f, v080n_dlog10, 0, 1000, 0x35af2b5570e6f5e0ull },
    { "sqrt", arc_sqrtf, sqrtf, 0, 1000, 0x9c7ea2723f130161ull },                         { "floor", arc_floorf, floorf, -1e5f, 1e5f, 0x0b70e0dbc67ee3bdull },
    { "ceil", arc_ceilf, ceilf, -1e5f, 1e5f, 0xad5bfdc1ec0cb767ull },                     { "round", arc_roundf, roundf, -1e5f, 1e5f, 0xb2ca757c08ef569cull },
    { "trunc", arc_truncf, truncf, -1e5f, 1e5f, 0x8f6b6fdd4ddf9267ull },                  { "exp2", arc_exp2f, exp2f, -140, 120, 0x5527221036377454ull },
    { "sinbig", arc_sinf, v080n_dsin, -1e7f, 1e7f, 0x93b439e4a38a498full },
};
static v080n_f2 v080n_f2s[] = {
    { "atan2", arc_atan2f, v080n_datan2, -10, 10, 0x72b95386096f4fb3ull }, { "pow", arc_powf, v080n_dpow, 0, 6, 0xfce4a6548e9aacffull },
    { "fmod", arc_fmodf, fmodf, -100, 100, 0x40d0986f7a2ce325ull },          { "hypot", arc_hypotf, hypotf, -100, 100, 0x97ec4aee7b470e91ull },
};
static int v080n_golden_print;   /* set to 1 to print the hashes (for a new golden table) */
static void v080n_maths(void) {
    int n = 4096;
    for (int i = 0; i < ARC__COUNTOF(v080n_f1s); i++) {
        v080n_f1 *t = &v080n_f1s[i]; v080n_rs = 0x9E3779B9u ^ (uint32_t)(i * 7919 + 1);
        uint64_t h = 1469598103934665603ull; int worst = 0; float wx = 0;
        for (int k = 0; k < n; k++) {
            float x = v080n_arg(k, t->lo, t->hi), r = t->f(x);
            h = v080n_mix(h, v080n_bits(r));
            if (t->f == arc_sinf || t->f == arc_cosf || t->f == arc_tanf) { if (!(fabsf(x) < 1e6f)) continue; }   /* (C libraries differ out there; checked below) */
            int u = v080n_ulp(r, t->ref(x)); if (u > worst) { worst = u; wx = x; }
        }
        if (v080n_golden_print) printf("ARC note golden %s 0x%016llxull\n", t->name, (unsigned long long)h);
        CHECK(worst <= 1);   /* against the C library in double, rounded to float (exactly rounded results: 0 or 1 ulp) */
        if (worst > 1) printf("ARC note %s is %d ulp off at %.9g\n", t->name, worst, wx);
        CHECK(h == t->golden);
        if (h != t->golden) printf("ARC note golden %s: 0x%016llx, expected 0x%016llx (a platform or compiler difference)\n", t->name, (unsigned long long)h, (unsigned long long)t->golden);
    }
    for (int i = 0; i < ARC__COUNTOF(v080n_f2s); i++) {
        v080n_f2 *t = &v080n_f2s[i]; v080n_rs = 0x85EBCA6Bu ^ (uint32_t)(i * 104729 + 1);
        uint64_t h = 1469598103934665603ull; int worst = 0;
        for (int k = 0; k < n; k++) {
            float x = v080n_arg(k, t->lo, t->hi), y = v080n_arg(k, t->lo, t->hi), r = t->f(x, y);
            h = v080n_mix(h, v080n_bits(r));
            int u = v080n_ulp(r, t->ref(x, y)); if (u > worst) worst = u;
        }
        if (v080n_golden_print) printf("ARC note golden %s 0x%016llxull\n", t->name, (unsigned long long)h);
        CHECK(worst <= 1);
        if (worst > 1) printf("ARC note %s is %d ulp off\n", t->name, worst);
        CHECK(h == t->golden);
        if (h != t->golden) printf("ARC note golden %s: 0x%016llx, expected 0x%016llx (a platform or compiler difference)\n", t->name, (unsigned long long)h, (unsigned long long)t->golden);
    }
    /* exact values and special cases */
    float inf = 1.0f / (float)atof("0"), nan = inf - inf;
    CHECK(arc_powf(2, 10) == 1024 && arc_powf(3, 2) == 9 && arc_exp2f(10) == 1024 && arc_log2f(8) == 3 && arc_log10f(1000) == 3);
    CHECK(arc_expf(0) == 1 && arc_logf(1) == 0 && arc_sinf(0) == 0 && arc_cosf(0) == 1 && arc_sqrtf(2) == sqrtf(2));
    CHECK(arc_powf(-2, 3) == -8 && arc_powf(-2, 0.5f) != arc_powf(-2, 0.5f) && arc_powf(0, -1) == inf && arc_powf(nan, 0) == 1 && arc_powf(1, nan) == 1);
    CHECK(arc_powf(-1, inf) == 1 && arc_powf(0.5f, inf) == 0 && arc_powf(2, -inf) == 0 && arc_powf(-inf, 3) == -inf && arc_powf(-inf, -3) == 0);
    CHECK(arc_floorf(-0.5f) == -1 && arc_ceilf(-0.5f) == 0 && signbit(arc_ceilf(-0.5f)) && arc_roundf(2.5f) == 3 && arc_roundf(-2.5f) == -3 && arc_truncf(-2.7f) == -2);
    CHECK(arc_fmodf(7.5f, 2) == 1.5f && arc_fmodf(-7.5f, 2) == -1.5f && arc_fmodf(1, 0) != arc_fmodf(1, 0) && arc_fabsf(-3) == 3);
    CHECK(arc_atan2f(0, -1) == arc_atan2f(1e-30f, -1) && arc_atan2f(-0.0f, -1) < 0 && arc_atan2f(1, 0) == arc_atan2f(1, 1e-38f) && fabsf(arc_atan2f(1, 1) - 0.785398163f) < 1e-7f);
    CHECK(arc_asinf(2) != arc_asinf(2) && arc_acosf(1) == 0 && arc_logf(-1) != arc_logf(-1) && arc_logf(0) == -inf && arc_expf(-200) == 0 && arc_expf(100) == inf);
    CHECK(arc_sinf(inf) != arc_sinf(inf) && arc_hypotf(3, 4) == 5 && arc_hypotf(nan, inf) == inf);
    /* huge arguments (exact range reduction), against values worked out to 60 digits (some C libraries get these wrong) */
    CHECK(v080n_bits(arc_sinf(1e30f)) == 0xbf4a89b0u && v080n_bits(arc_cosf(-3e9f)) == 0xbe248bfcu && v080n_bits(arc_sinf(1e22f)) == 0xbf3becc4u && v080n_bits(arc_cosf(123456789.0f)) == 0xbe8ea22cu);
}

/* ======================================================================== */
/* Part 2: sessions in one process                                          */
/* ======================================================================== */
typedef struct { int started, pos[8], score[8]; float f; } v080n_rules;   /* hashed (net_state) */
typedef struct { v080n_rules r; int joins, lefts, rejoins, turns, starts, chats, last_from, catching_cmds, refusals; uint32_t start_seq; } v080n_game;
#define V080N_M 10
static v080n_game v080n_g[V080N_M]; static void *v080n_m[V080N_M]; static v080n_game *v080n_cur; static double v080n_t;
static bool v080n_command(const char *name, int argc, const char **argv) {
    v080n_game *g = v080n_cur; int from = arc_net_command_from();
    if (!strcmp(name, "go") && argc >= 1) {
        int k = atoi(argv[0]), p = from < 0 ? 0 : from;
        g->r.pos[p] += k; g->r.score[p] += arc_rand_range(1, 6);
        g->r.f += arc_sinf((float)k * 0.37f) * arc_expf(-0.1f * (float)g->r.pos[p]) + arc_powf(1.1f, (float)k);
        g->last_from = from; if (arc_net_catching_up()) g->catching_cmds++;
        return true;
    }
    if (!strcmp(name, "chat")) { g->chats++; return true; }
    return false;
}
static void v080n_event(const arc_net_ev *ev) {
    v080n_game *g = v080n_cur;
    if (ev->type == ARC_NET_EV_START) { g->starts++; g->r.started = 1; g->start_seq = ev->seq; }
    if (ev->type == ARC_NET_EV_JOINED) g->joins++;
    if (ev->type == ARC_NET_EV_LEFT) g->lefts++;
    if (ev->type == ARC_NET_EV_REJOINED) g->rejoins++;
    if (ev->type == ARC_NET_EV_TURN) g->turns++;
    if (ev->type == ARC_NET_EV_REFUSED) g->refusals++;
}
static size_t v080n_net_state(void *buf, size_t cap) { if (cap >= sizeof(v080n_rules)) memcpy(buf, &v080n_cur->r, sizeof(v080n_rules)); return sizeof(v080n_rules); }
static size_t v080n_save(void *buf, size_t cap) { if (cap >= sizeof(v080n_game)) memcpy(buf, v080n_cur, sizeof(v080n_game)); return sizeof(v080n_game); }
static bool v080n_load(const void *buf, size_t n) { if (n != sizeof(v080n_game)) return false; memcpy(v080n_cur, buf, n); return true; }
static void v080n_on(int i) { arc__net_test_use(v080n_m[i]); v080n_cur = &v080n_g[i]; }
static void v080n_frames(int n) {
    for (int k = 0; k < n; k++) {
        v080n_t += 1.0 / 60.0; arc__net_test_time(v080n_t);
        for (int i = 0; i < V080N_M; i++) if (v080n_m[i]) { v080n_on(i); arc__net_frame(); arc__net_tick_begin(); }
    }
}
static void v080n_new(int i, const char *name, const char *version) {
    v080n_m[i] = arc__net_test_machine(); memset(&v080n_g[i], 0, sizeof v080n_g[i]); v080n_on(i);
    arc_net_desc d; memset(&d, 0, sizeof d); d.game = "v080n"; d.version = version; d.max_players = 6; d.port = 7001;
    arc_net_setup(&d); arc_net_set_name(name);
    arc_net_sim(arc_fmt("lag=40,jitter=30,loss=0.02,seed=%d", i + 1));
}
static void v080n_kill(int i) { if (v080n_m[i]) { arc__net_test_free(v080n_m[i]); v080n_m[i] = NULL; } }
static bool v080n_ev_seen(int type) { arc_net_ev ev; bool seen = false; while (arc_net_event(&ev)) if (ev.type == type) seen = true; return seen; }
static int v080n_owner(void) { v080n_on(0); return arc_net_turn_owner(); }
static bool v080n_all_at(int *ms, int n, uint32_t seq) { for (int i = 0; i < n; i++) { v080n_on(ms[i]); if (arc_net_seq() != seq || arc__net_test_inflight()) return false; } return true; }
static void v080n_settle(int *ms, int n) {   /* run until every listed machine has applied all the host has, with nothing in flight */
    for (int k = 0; k < 600; k++) { v080n_on(0); uint32_t s = (uint32_t)arc_net_log_count(); if (v080n_all_at(ms, n, s)) { v080n_frames(2); if (v080n_all_at(ms, n, s)) return; } v080n_frames(1); }
}
static uint64_t v080n_hash_of(int i) { v080n_on(i); return arc__net_test_hash(); }
static uint32_t v080n_seq_of(int i) { v080n_on(i); return arc_net_seq(); }
static bool v080n_same(int a, int b) { return !memcmp(&v080n_g[a].r, &v080n_g[b].r, sizeof(v080n_rules)); }
/* the owner plays one turn: a move and the end of the turn */
static void v080n_turn(int *ms, int n, int step) {
    int owner = v080n_owner(), mi = -1;
    for (int i = 0; i < n; i++) { v080n_on(ms[i]); if (arc_net_local_player() == owner) mi = ms[i]; }
    if (mi < 0) return;
    v080n_on(mi); arc_net_command(arc_fmt("go %d", step)); arc_net_turn_end();
    v080n_settle(ms, n);
}
static void v080n_sessions(void) {
    arc__net_test_report_dir("net_desync_selftest");
    /* ---- joining ---- */
    v080n_new(0, "Ann", "1"); v080n_new(1, "Bob", "1"); v080n_new(2, "Ann", "1"); v080n_new(3, "Old", "0.9");
    v080n_on(0); CHECK(arc_net_host(NULL) && arc_net_status() == ARC_NET_HOSTING && arc_net_is_host());
    v080n_on(1); CHECK(arc_net_join("mem:7001") && arc_net_status() == ARC_NET_JOINING && arc_net_local_player() == -1);
    v080n_frames(30);
    v080n_on(2); CHECK(arc_net_join("mem:7001"));
    v080n_on(3); CHECK(arc_net_join("mem:7001"));
    v080n_frames(60);
    int ms[3] = { 0, 1, 2 };
    for (int i = 0; i < 3; i++) { v080n_on(i); CHECK(arc_net_local_player() == i && v080n_g[i].joins == 3 && arc_net_seq() == 3); }
    v080n_on(1); CHECK(arc_net_status() == ARC_NET_CONNECTED);
    arc_net_player ps[8]; v080n_on(2); int np = arc_net_players(ps, 8);
    CHECK(np == 3 && !strcmp(ps[0].name, "Ann") && !strcmp(ps[1].name, "Bob") && !strcmp(ps[2].name, "Ann 2") && ps[2].local && ps[0].connected);   /* names kept unique */
    v080n_on(3); CHECK(arc__net_test_status_refused() && strstr(arc_net_message(), "version mismatch") && strstr(arc_net_message(), "v080n 1") && v080n_ev_seen(ARC_NET_EV_VERSION));
    v080n_on(0); CHECK(v080n_ev_seen(ARC_NET_EV_VERSION));      /* both sides are told */
    v080n_kill(3);
    /* ---- the lobby: only free commands before the start; only the host starts ---- */
    v080n_on(1); CHECK(!arc_net_command("go 1") && !arc_net_turn_end() && !arc_net_start());
    CHECK(arc_net_command_ex("chat hello", ARC_NET_FREE)); arc_net_set_ready(true);
    v080n_settle(ms, 3);
    for (int i = 0; i < 3; i++) { v080n_on(i); CHECK(v080n_g[i].chats == 1 && arc_net_players(ps, 8) == 3 && ps[1].ready && !ps[2].ready && !arc_net_started() && arc_net_turn_owner() == -1); }
    v080n_on(0); CHECK(arc_net_start()); v080n_settle(ms, 3);
    uint64_t seed0 = 0;
    for (int i = 0; i < 3; i++) {
        v080n_on(i);
        CHECK(arc_net_started() && v080n_g[i].starts == 1 && v080n_g[i].start_seq == 6 && arc_net_turn_owner() == 0 && arc_net_turn() == 1);
        if (!i) seed0 = arc_net_seed(); else CHECK(arc_net_seed() == seed0 && seed0 != 0);
        CHECK(arc_net_players(ps, 8) == 3 && ps[0].in_match && ps[2].in_match);
    }
    v080n_on(0); CHECK(arc_net_my_turn()); v080n_on(1); CHECK(!arc_net_my_turn());
    /* ---- alternating turns ---- */
    for (int r = 0; r < 4; r++) v080n_turn(ms, 3, r + 1);
    v080n_on(0); CHECK(arc_net_turn() == 5 && arc_net_turn_owner() == 1);
    for (int i = 1; i < 3; i++) CHECK(v080n_same(0, i) && v080n_hash_of(i) == v080n_hash_of(0) && v080n_hash_of(0) != 0);
    v080n_on(0); CHECK(v080n_g[0].r.pos[0] == 1 + 4 && v080n_g[0].r.pos[1] == 2 && v080n_g[0].r.pos[2] == 3 && v080n_g[0].turns == 4);
    /* refusals: a player out of turn (refused at once on its machine), and a command sent after its own end of turn
       (its machine still thinks it is its turn; the host refuses it) */
    v080n_on(2); CHECK(!arc_net_command("go 50"));
    v080n_on(1); int ref1 = v080n_g[1].refusals; arc_net_turn_end(); CHECK(arc_net_command("go 70"));
    v080n_settle(ms, 3);
    v080n_on(1); CHECK(v080n_g[1].refusals == ref1 + 1 && v080n_g[0].r.pos[1] == 2 && arc_net_turn_owner() == 2);
    CHECK(v080n_ev_seen(ARC_NET_EV_REFUSED));
    for (int i = 1; i < 3; i++) CHECK(v080n_same(0, i));
    v080n_on(0); CHECK(arc_net_log_save("v080n_mid.log")); v080n_rules mid = v080n_g[0].r; uint32_t mid_seq = arc_net_seq();
    /* ---- a dropout during its own turn: the turn waits; the host passes it on; then the turn order skips the absent
       player until it comes back with what it missed ---- */
    v080n_on(2); CHECK(arc_net_my_turn()); arc__net_test_drop(); CHECK(arc_net_status() == ARC_NET_LOST && !arc_net_my_turn() && !arc_net_command("go 1"));
    v080n_frames(30);
    v080n_on(0); CHECK(v080n_g[0].lefts == 1 && arc_net_players(ps, 8) == 3 && !ps[2].connected && arc_net_turn_owner() == 2);   /* waiting for it */
    v080n_on(1); CHECK(!arc_net_turn_end());                 /* only the host may end an absent player's turn */
    v080n_on(0); CHECK(arc_net_turn_end()); v080n_frames(30); v080n_on(0); CHECK(arc_net_turn_owner() == 0);
    int two[2] = { 0, 1 };
    for (int r = 0; r < 4; r++) { CHECK(v080n_owner() != 2); v080n_turn(two, 2, 10 + r); }
    v080n_on(2); CHECK(arc_net_join("mem:7001"));
    v080n_settle(ms, 3);
    for (int i = 0; i < 3; i++) { v080n_on(i); CHECK(v080n_g[i].rejoins == 1 && v080n_same(0, i)); }
    v080n_on(2); CHECK(v080n_g[2].lefts == 1 && arc_net_status() == ARC_NET_CONNECTED);
    v080n_turn(ms, 3, 21); v080n_turn(ms, 3, 22); v080n_turn(ms, 3, 23);
    /* ---- a restarted player (a new machine, nothing applied) rejoins by name and catches up silently; the host has not
       noticed the old one is gone (its link still looks fine): the new connection replaces it ---- */
    v080n_new(4, "Bob", "1"); v080n_on(4); CHECK(arc_net_join("mem:7001"));
    int ms4[3] = { 0, 4, 2 };
    v080n_settle(ms4, 3);
    v080n_on(1); CHECK(arc_net_status() == ARC_NET_LOST); v080n_kill(1);
    v080n_on(4); CHECK(arc_net_local_player() == 1 && v080n_same(0, 4) && v080n_g[4].catching_cmds > 5 && v080n_g[4].starts == 1);
    v080n_turn(ms4, 3, 31); v080n_turn(ms4, 3, 32);
    for (int i = 1; i < 3; i++) CHECK(v080n_same(0, ms4[i]) && v080n_seq_of(ms4[i]) == v080n_seq_of(0) && v080n_hash_of(ms4[i]) == v080n_hash_of(0));
    /* ---- resume a saved log: a new host re-applies it, a player rejoins by name and receives it ---- */
    v080n_new(5, "Ann", "1"); v080n_on(5); arc_net_desc d5 = *arc_net_desc_get(); d5.port = 7002; CHECK(arc_net_host(&d5) && arc_net_resume("v080n_mid.log"));
    v080n_frames(2);
    CHECK(!memcmp(&v080n_g[5].r, &mid, sizeof mid) && arc_net_started());
    v080n_new(6, "Bob", "1"); v080n_on(6); CHECK(arc_net_join("mem:7002"));
    v080n_frames(120);
    v080n_on(6); CHECK(arc_net_local_player() == 1 && !memcmp(&v080n_g[6].r, &mid, sizeof mid) && arc_net_seq() > mid_seq && arc_net_turn_owner() == 2);
    v080n_on(5); CHECK(arc_net_players(ps, 8) == 3 && ps[1].connected && !ps[2].connected);
    v080n_kill(5); v080n_kill(6);
    /* ---- a desync: one machine changes its state on its own; the next command shows it everywhere ---- */
    v080n_g[4].r.score[0] += 1;                             /* (as a game bug or a local cheat would) */
    v080n_turn(ms4, 3, 41);
    v080n_frames(60);
    uint32_t ds = 0;
    for (int i = 0; i < 3; i++) { v080n_on(ms4[i]); CHECK(arc__net_test_desync_reported() && v080n_ev_seen(ARC_NET_EV_DESYNC)); if (!i) ds = arc__net_test_desync_seq(); else CHECK(arc__net_test_desync_seq() == ds); }
    FILE *rf = fopen(arc_fmt("net_desync_selftest/seq%u_p1/dump.json", ds), "rb"); CHECK(rf != NULL); if (rf) fclose(rf);
    rf = fopen(arc_fmt("net_desync_selftest/seq%u_p0/info.json", ds), "rb"); CHECK(rf != NULL); if (rf) fclose(rf);
    /* ---- leaving ---- */
    v080n_on(4); arc_net_leave(); CHECK(arc_net_status() == ARC_NET_OFFLINE && arc_net_local_player() == 0);
    v080n_frames(20);
    v080n_on(0); CHECK(arc_net_players(ps, 8) == 3 && !ps[1].connected);
    v080n_on(0); arc_net_leave(); v080n_frames(20);
    v080n_on(2); CHECK(arc_net_status() == ARC_NET_LOST && strstr(arc_net_message(), "host left"));
    for (int i = 0; i < V080N_M; i++) v080n_kill(i);
    arc__net_test_use(NULL);
}
/* offline: arc_net_command applies at the next tick through command(), with the match generator, and survives rewind */
static int v080n_go_every;
static void v080n_tick(void) {
    if (v080n_go_every && arc_tick_count() % (uint64_t)v080n_go_every == 3) arc_net_command(arc_fmt("go %d", (int)(arc_tick_count() % 5) + 1));
    if (v080n_go_every && arc_tick_count() % 50 == 49) arc_net_turn_end();
    (void)arc_rand_u32();                                   /* tick() draws from the global generator too */
}
static void v080n_offline(void) {
    arc_app app; memset(&app, 0, sizeof app);
    app.command = v080n_command; app.save_state = v080n_save; app.load_state = v080n_load; app.net_event = v080n_event;
    v080n_m[7] = arc__net_test_machine(); v080n_on(7); memset(&v080n_g[7], 0, sizeof v080n_g[7]);
    arc__test_sandbox(&app);
    CHECK(arc_net_status() == ARC_NET_OFFLINE && arc_net_my_turn() && arc_net_turn_owner() == 0 && arc_net_turn() == 1 && arc_net_local_player() == 0);
    CHECK(arc_net_command("go 2") && v080n_g[7].r.pos[0] == 0);   /* not yet */
    arc__net_tick_begin();
    CHECK(v080n_g[7].r.pos[0] == 2 && v080n_g[7].last_from == 0 && arc_net_seq() == 1 && arc_net_command_from() == -1);
    CHECK(arc_net_turn_end()); arc__net_tick_begin(); CHECK(arc_net_turn() == 2 && v080n_g[7].turns == 1);
    /* the commands' random numbers come from the session's own generator, whatever else draws from arc_rand */
    int sc[2];
    for (int t = 0; t < 2; t++) {
        void *m8 = arc__net_test_machine(); arc__net_test_use(m8); v080n_cur = &v080n_g[8]; memset(v080n_cur, 0, sizeof *v080n_cur);
        for (int k = 0; k < 12; k++) { for (int j = 0; j < 1 + t * (k % 3); j++) (void)arc_rand_u32(); arc_net_command("go 2"); arc__net_tick_begin(); }
        sc[t] = v080n_g[8].r.score[0];
        arc__net_test_free(m8);
    }
    CHECK(sc[0] == sc[1] && sc[0] >= 12);
    /* the session part of ARC's snapshots */
    v080n_on(7);
    uint8_t buf[2048]; arc_writer w = { buf, sizeof buf, 0 }; arc__net_state(&w); CHECK(w.len > 8 && w.len < sizeof buf);
    arc_net_command("go 1"); arc_net_turn_end(); arc__net_tick_begin(); arc__net_tick_begin(); CHECK(arc_net_turn() == 3 && arc_net_seq() == 4);
    arc_reader r = { buf, w.len, 0, false }; CHECK(arc__net_state_read(&r, true) && arc_net_turn() == 2 && arc_net_seq() == 2);
    arc_reader bad = { buf, 6, 0, false }; CHECK(!arc__net_state_read(&bad, false));
    arc__test_sandbox(NULL);
    /* rewind: commands queued by tick() are made again while ARC re-simulates, so the state matches playing straight on */
    app.tick = v080n_tick;
    memset(v080n_cur, 0, sizeof *v080n_cur); v080n_go_every = 7;
    arc_net_leave();                                          /* (offline again, from the start) */
    arc__test_sandbox(&app);
    arc__test_ticks(240);
    v080n_game straight = *v080n_cur; uint32_t sseq = arc_net_seq(); int sturn = arc_net_turn();
    CHECK(straight.r.pos[0] > 20 && sseq > 30 && sturn == 5);
    arc__test_command("rewind 100");
    CHECK(arc_tick_count() == 140 && arc_net_seq() < sseq);
    arc__test_ticks(100);
    CHECK(!memcmp(&straight.r, &v080n_cur->r, sizeof straight.r) && arc_net_seq() == sseq && arc_net_turn() == sturn);
    arc__test_command("state_check");
    v080n_go_every = 0;
    arc__test_sandbox(NULL);
    v080n_kill(7);
    arc__net_test_use(NULL);
}

/* ======================================================================== */
/* Part 3: the lobby, driven like the UI tests (pretend ticks)              */
/* ======================================================================== */
static int v080n_lobby_result;
static void v080n_lobby_step(void) { arc__input_test_step(); arc__ui_tick_begin(); int r = arc_net_lobby(NULL); if (r != ARC_NET_LOBBY_OPEN) v080n_lobby_result = r; arc__ui_tick_end(); }
static bool v080n_click(const char *label) {
    float x, y; if (!arc__ui_find(label, &x, &y)) return false;
    arc__input_mouse(x, y); v080n_lobby_step();
    arc__input_key(ARC_MOUSE_LEFT, true); v080n_lobby_step(); arc__input_key(ARC_MOUSE_LEFT, false); v080n_lobby_step(); v080n_lobby_step();
    return true;
}
static void v080n_lobby(void) {
    v080n_m[9] = arc__net_test_machine(); v080n_on(9);
    arc_net_desc d; memset(&d, 0, sizeof d); d.game = "lobbytest"; d.version = "1"; d.port = 7100; arc_net_setup(&d); arc_net_set_name("Una");
    v080n_lobby_step(); v080n_lobby_step();
    float x, y;
    CHECK(arc__ui_find("Host a game", &x, &y) && arc__ui_find("Join address", &x, &y) && arc__ui_find("Back", &x, &y) && !arc__ui_find("Start match", &x, &y));
    CHECK(v080n_click("Host a game") && arc_net_status() == ARC_NET_HOSTING);
    v080n_frames(1); v080n_on(9); v080n_lobby_step(); v080n_lobby_step();
    CHECK(arc__ui_find("Start match", &x, &y) && arc__ui_find("Leave", &x, &y) && arc__ui_find("Ready", &x, &y) && !arc__ui_find("Host a game", &x, &y));
    CHECK(v080n_click("Start match")); v080n_frames(2); v080n_on(9); CHECK(!arc_net_started());   /* one player: disabled */
    CHECK(v080n_click("Ready")); v080n_frames(1); v080n_on(9); CHECK(arc_net_players(NULL, 0) == 1);
    { arc_net_player p; arc_net_players(&p, 1); CHECK(p.ready && !strcmp(p.name, "Una")); }
    CHECK(v080n_click("Leave") && arc_net_status() == ARC_NET_OFFLINE);
    v080n_lobby_step(); v080n_lobby_result = 0; CHECK(v080n_click("Back") && v080n_lobby_result == ARC_NET_LOBBY_BACK);
    v080n_kill(9);
    arc__net_test_use(NULL);
    arc__input_test_step(); arc__ui_tick_begin(); arc__ui_tick_end();   /* no lobby left in the UI */
}

static void v080n_init(void) {
    v080n_golden_print = getenv("ARC_GOLDEN") != NULL;
    v080n_maths();
    arc_app app; memset(&app, 0, sizeof app);
    app.command = v080n_command; app.save_state = v080n_save; app.load_state = v080n_load; app.net_event = v080n_event; app.net_state = v080n_net_state;
    v080n_cur = &v080n_g[9];                                 /* (the sandbox's first snapshot reads the game) */
    arc__test_sandbox(&app);
    v080n_sessions();
    v080n_lobby();
    arc__test_sandbox(NULL);
    v080n_offline();
}
static void v080n_check(void) {
    /* the engine's own (default) session is untouched by all of the above */
    CHECK(arc_net_status() == ARC_NET_OFFLINE && arc_net_seq() == 0 && arc_net_log_count() == 0);
}
