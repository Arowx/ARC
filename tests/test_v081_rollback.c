/* ARC 0.8.1 Phase F2 self-test (real-time rollback): included by test_core.c. v081r_init() runs in init, v081r_check() at
   tick 9. Every name here starts with v081r_ (all test files share one translation unit).
   Part 1: several machines in this one process (memory links and in-process datagrams, a test clock), each with its own
   little game through test hooks: 2 and 3 players under simulated lag, jitter, loss, duplicates and reordering; every
   machine's confirmed state and hashes must agree; rollbacks must happen; a stalled peer makes the others wait and then
   recover; a peer going quiet is dropped (its input then zeros everywhere); a changed state is caught as a desync;
   time sync slows down a machine that is ahead.
   Part 2: ARC's own engine state through the sandbox and a synctest (every tick rolls back and runs again): the same
   state as a plain run, particles rolled back with it, every sound played once, events logged once, the default input
   and the input delay; and a game that keeps state outside save_state fails the synctest. */
void arc__rb_test_hooks(const void *hooks);
void arc__rb_test_step(void);
int64_t arc__rb_test_tick(void);
int64_t arc__rb_test_final(void);
void arc__rb_test_stop_at(int64_t t);
uint64_t arc__rb_test_hash_at(int64_t f);
int arc__test_events_emitted(void);
static arc_sound v081r_snd;

/* ======================================================================== */
/* Part 1: machines with hooks                                              */
/* ======================================================================== */
typedef struct {                              /* the same layout as arc_rollback.c's rb_hooks */
    size_t (*save)(void *buf, size_t cap); bool (*load)(const void *buf, size_t n);
    void (*tick)(void); size_t (*state)(void *buf, size_t cap); size_t (*input)(void *buf, size_t cap); void (*start)(uint64_t seed);
} v081r_hooks;
typedef struct { int32_t x[4], y[4], score[4], hits, tick, zeros, sounds; uint32_t rng; } v081r_state;
typedef struct { v081r_state s; int starts, ticks_run, waiting_ev, resumed_ev, dropped_ev, desync_ev; int pattern; } v081r_game;
#define V081R_M 4
static v081r_game v081r_g[V081R_M]; static void *v081r_m[V081R_M]; static int v081r_cur; static double v081r_t;
static bool v081r_paused[V081R_M];
static size_t v081r_save(void *buf, size_t cap) { if (cap >= sizeof(v081r_state)) memcpy(buf, &v081r_g[v081r_cur].s, sizeof(v081r_state)); return sizeof(v081r_state); }
static bool v081r_load(const void *buf, size_t n) { if (n != sizeof(v081r_state)) return false; memcpy(&v081r_g[v081r_cur].s, buf, n); return true; }
static size_t v081r_state_fn(void *buf, size_t cap) { return v081r_save(buf, cap); }
static void v081r_start(uint64_t seed) { v081r_game *g = &v081r_g[v081r_cur]; memset(&g->s, 0, sizeof g->s); g->s.rng = (uint32_t)seed | 1u; g->starts++; }
static void v081r_tick_fn(void) {
    v081r_game *g = &v081r_g[v081r_cur]; v081r_state *s = &g->s;
    s->tick++; g->ticks_run++;
    for (int p = 0; p < 4; p++) {
        int n = 0; const uint8_t *in = (const uint8_t *)arc_net_input(p, &n);
        if (!in) continue;
        uint8_t b = n > 0 ? in[0] : 0;
        if (arc_net_input_dropped(p)) s->zeros++;
        s->x[p] += (b & 1) - ((b >> 1) & 1); s->y[p] += ((b >> 2) & 1) - ((b >> 3) & 1);
        s->rng = s->rng * 1664525u + 1013904223u + b;
        if ((b & 16) && (s->rng >> 28) < 3) {                    /* a hit: a sound and an event, as a game would */
            s->hits++; s->score[p] += 1 + (int)(s->rng >> 30);
            arc_play(v081r_snd, 0.1f, 0); s->sounds++;
            arc_event("v081r_hit", "\"p\":%d", p);
        }
    }
}
static size_t v081r_input_fn(void *buf, size_t cap) {   /* each machine changes its input on its own rhythm */
    (void)cap; v081r_game *g = &v081r_g[v081r_cur];
    int64_t t = arc__rb_test_tick(), per = 5 + 4 * (int64_t)g->pattern;
    ((uint8_t *)buf)[0] = (uint8_t)(((t / per) * (g->pattern * 7 + 3)) & 31);
    return 1;
}
static const v081r_hooks v081r_h = { v081r_save, v081r_load, v081r_tick_fn, v081r_state_fn, v081r_input_fn, v081r_start };
static void v081r_on(int i) { arc__net_test_use(v081r_m[i]); v081r_cur = i; }
static void v081r_events(int i) {
    arc_net_ev ev; v081r_on(i);
    while (arc_net_event(&ev)) {
        if (ev.type == ARC_NET_EV_WAITING) v081r_g[i].waiting_ev++;
        if (ev.type == ARC_NET_EV_RESUMED) v081r_g[i].resumed_ev++;
        if (ev.type == ARC_NET_EV_DROPPED) v081r_g[i].dropped_ev++;
        if (ev.type == ARC_NET_EV_DESYNC) v081r_g[i].desync_ev++;
    }
}
static void v081r_frames(int n) {
    for (int k = 0; k < n; k++) {
        v081r_t += 1.0 / 60.0; arc__net_test_time(v081r_t);
        for (int i = 0; i < V081R_M; i++) if (v081r_m[i] && !v081r_paused[i]) { v081r_on(i); arc__rb_test_step(); v081r_events(i); }
    }
}
static void v081r_new(int i, int pattern, const char *sim) {
    v081r_m[i] = arc__net_test_machine(); memset(&v081r_g[i], 0, sizeof v081r_g[i]); v081r_paused[i] = false; v081r_on(i);
    v081r_g[i].pattern = pattern;
    arc_net_desc d; memset(&d, 0, sizeof d); d.game = "v081r"; d.version = "1"; d.max_players = 4; d.port = 7201; d.mode = ARC_NET_ROLLBACK;
    arc_net_setup(&d); arc_net_set_name(arc_fmt("M%d", i));
    arc__rb_test_hooks(&v081r_h);
    arc_net_sim(sim);
}
static void v081r_kill(int i) { if (v081r_m[i]) { v081r_on(i); arc__net_test_free(v081r_m[i]); v081r_m[i] = NULL; } }
static bool v081r_match(int n, const char **sims, int64_t stop, int max_frames) {   /* host 0, clients 1..n-1; play until every machine has confirmed `stop` */
    for (int i = 0; i < n; i++) v081r_new(i, i, sims[i]);
    v081r_on(0); if (!arc_net_host(NULL)) return false;
    for (int i = 1; i < n; i++) { v081r_on(i); arc_net_join("mem:7201"); }
    v081r_frames(40);
    v081r_on(0); if (arc_net_players(NULL, 0) != n || !arc_net_start()) return false;
    for (int i = 0; i < n; i++) { v081r_on(i); }
    v081r_frames(30);
    for (int i = 0; i < n; i++) { v081r_on(i); arc__rb_test_stop_at(stop); }
    for (int k = 0; k < max_frames; k++) {
        bool done = true;
        for (int i = 0; i < n; i++) { v081r_on(i); if (arc__rb_test_final() < stop) done = false; }
        if (done) { v081r_frames(3); return true; }
        v081r_frames(1);
    }
    return false;
}
static bool v081r_same(int a, int b) { return !memcmp(&v081r_g[a].s, &v081r_g[b].s, sizeof(v081r_state)); }
static arc_net_rb_stats v081r_stats(int i) { v081r_on(i); return arc_net_stats(); }

static void v081r_two(void) {
    /* 2 players, a bad network both ways: lag, jitter (reordering), 10% loss, duplicates */
    const char *sims[2] = { "lag=60,jitter=40,loss=0.1,dup=0.1,seed=3", "lag=40,jitter=60,loss=0.1,dup=0.1,seed=8" };
    int ev0 = arc__test_events_emitted();
    bool ok = v081r_match(2, sims, 600, 3000);
    CHECK(ok);
    CHECK(arc__test_events_emitted() - ev0 == 2 * v081r_g[0].s.hits);   /* every hit's event once on each machine, though ticks ran again */
    CHECK(v081r_g[0].starts >= 1 && v081r_g[1].starts >= 1);
    CHECK(v081r_g[0].s.tick == 600 && v081r_g[1].s.tick == 600);
    CHECK(v081r_same(0, 1));                                   /* the confirmed state is the same everywhere */
    CHECK(v081r_g[0].s.hits > 3 && v081r_g[0].s.x[1] != 0);   /* (and something happened) */
    arc_net_rb_stats a = v081r_stats(0), b = v081r_stats(1);
    CHECK(a.active && b.active && a.players == 2 && a.local == 0 && b.local == 1 && a.input_delay == 2 && a.max_rollback == 8);
    CHECK(a.rollbacks > 10 && b.rollbacks > 10 && a.mispredicted > 0 && a.resim_ticks >= a.rollbacks && a.max_depth <= 8 && b.max_depth <= 8);
    CHECK(a.last_hash_tick == 600 && b.last_hash_tick == 600 && a.last_hash == b.last_hash && a.desync_tick < 0 && b.desync_tick < 0);
    v081r_on(0); uint64_t h300a = arc__rb_test_hash_at(300); v081r_on(1); uint64_t h300b = arc__rb_test_hash_at(300);
    CHECK(h300a != 0 && h300a == h300b);
    CHECK(a.ping_ms > 60 && a.ping_ms < 400 && a.packets_received > 300);
    CHECK(v081r_g[0].desync_ev == 0 && v081r_g[1].desync_ev == 0);
    /* sounds: started once per hit of the confirmed match; those of mispredicted hits were stopped */
    CHECK(a.sounds_played - a.sounds_cancelled == v081r_g[0].s.sounds && b.sounds_played - b.sounds_cancelled == v081r_g[1].s.sounds);
    CHECK(a.sounds_deduped + b.sounds_deduped > 5 && a.sounds_cancelled + b.sounds_cancelled > 0 && a.sounds_late + b.sounds_late > 0);
    /* a desync: machine 1's state changes outside the rules; both machines report it at the same tick */
    for (int i = 0; i < 2; i++) { v081r_on(i); arc__rb_test_stop_at(-1); }
    v081r_g[1].s.score[0] += 5;
    v081r_frames(120);
    a = v081r_stats(0); b = v081r_stats(1);
    CHECK(a.desync_tick >= 600 && a.desync_tick == b.desync_tick && a.desync_tick % 10 == 0);
    CHECK(v081r_g[0].desync_ev == 1 && v081r_g[1].desync_ev == 1);
    v081r_kill(1); v081r_kill(0);
}
static void v081r_three(void) {
    /* 3 players through the host (it relays the clients' inputs), one with a much worse link */
    const char *sims[3] = { "lag=20,jitter=10,seed=1", "lag=30,jitter=20,loss=0.05,seed=2", "lag=90,jitter=50,loss=0.08,dup=0.05,seed=5" };
    CHECK(v081r_match(3, sims, 400, 3000));
    CHECK(v081r_g[0].s.tick == 400 && v081r_same(0, 1) && v081r_same(0, 2));
    arc_net_rb_stats a = v081r_stats(0), c = v081r_stats(2);
    CHECK(a.players == 3 && c.local == 2 && a.last_hash == c.last_hash && c.last_hash_tick == 400 && c.rollbacks > 0);
    CHECK(v081r_g[2].s.x[1] != 0 || v081r_g[2].s.y[1] != 0);  /* client 2 got client 1's inputs (through the host) */
    v081r_kill(2); v081r_kill(1); v081r_kill(0);
}
static void v081r_stall_drop(void) {
    const char *sims[2] = { "lag=30,seed=1", "lag=30,seed=2" };
    CHECK(v081r_match(2, sims, 200, 2000));
    for (int i = 0; i < 2; i++) { v081r_on(i); arc__rb_test_stop_at(-1); }
    v081r_frames(30);
    /* machine 1 stops for a second and a half: machine 0 runs max_rollback ticks ahead, then waits (and says so) */
    v081r_paused[1] = true;
    v081r_on(0); int64_t t0 = arc__rb_test_tick();
    v081r_frames(90);
    v081r_on(0); int64_t t1 = arc__rb_test_tick();
    CHECK(t1 - t0 <= 8 + 2 + 3 && v081r_g[0].waiting_ev == 1 && arc_net_waiting_for() == 1);
    v081r_paused[1] = false;
    v081r_frames(60);
    v081r_on(0);
    CHECK(v081r_g[0].resumed_ev == 1 && arc_net_waiting_for() == -1 && arc__rb_test_tick() > t1 + 30);
    arc_net_rb_stats a = v081r_stats(0);
    CHECK(a.stalled_ms >= 1000 && a.stalled_ms < 2000);
    /* both still agree */
    for (int i = 0; i < 2; i++) { v081r_on(i); arc__rb_test_stop_at(arc__rb_test_tick() + 40); }
    v081r_on(0); int64_t stop = arc__rb_test_tick() + 40; v081r_on(1); if (arc__rb_test_tick() + 40 > stop) stop = arc__rb_test_tick() + 40;
    for (int i = 0; i < 2; i++) { v081r_on(i); arc__rb_test_stop_at(stop); }
    v081r_frames(150);
    CHECK(v081r_g[0].s.tick == stop && v081r_same(0, 1));
    for (int i = 0; i < 2; i++) { v081r_on(i); arc__rb_test_stop_at(-1); }
    /* machine 1 goes quiet for good: after 3 s it is dropped; machine 0 plays on with zeros for it */
    v081r_paused[1] = true;
    v081r_frames(4 * 60);
    v081r_on(0);
    CHECK(v081r_g[0].dropped_ev == 1 && v081r_g[0].s.zeros > 40);
    int64_t after = arc__rb_test_tick(); v081r_frames(60); v081r_on(0); CHECK(arc__rb_test_tick() >= after + 55);   /* no waiting any more */
    CHECK(arc_net_input_predicted(1) == false);
    v081r_kill(1); v081r_kill(0);
}
static void v081r_timesync(void) {
    /* machine 1 hears of the start much later (slow commands): machine 0 is ahead and skips ticks to let it catch up */
    const char *sims[2] = { "lag=10,seed=1", "lag=10,seed=2" };
    for (int i = 0; i < 2; i++) v081r_new(i, i, sims[i]);
    v081r_on(0); CHECK(arc_net_host(NULL)); v081r_on(1); arc_net_join("mem:7201");
    v081r_frames(40);
    v081r_on(1); arc_net_sim("lag=10,seed=2"); v081r_on(0); arc_net_sim("lag=10,seed=1");
    v081r_on(0); CHECK(arc_net_start());
    v081r_paused[1] = true; v081r_frames(6); v081r_paused[1] = false;   /* (machine 1 starts 6 frames late) */
    v081r_frames(400);
    arc_net_rb_stats a = v081r_stats(0), b = v081r_stats(1);
    CHECK(a.skipped >= 2 && b.skipped == 0 && a.frame_advantage < 1.0f && a.frame_advantage > -1.0f);
    v081r_kill(1); v081r_kill(0);
}

/* ======================================================================== */
/* Part 2: ARC's engine state (sandbox) and the synctest                    */
/* ======================================================================== */
typedef struct { int32_t x, y, shots, events, sounds, tick, starts, akey, apress, delayed_at; uint32_t r; } v081r_eng;
static v081r_eng v081r_e;
static int v081r_outside;                      /* state kept outside save_state (the broken game) */
static arc_fx v081r_fx;
static size_t v081r_esave(void *buf, size_t cap) { arc_writer w = { (uint8_t *)buf, cap, 0 }; arc_write(&w, &v081r_e, sizeof v081r_e); return w.len; }
static bool v081r_eload(const void *buf, size_t n) { arc_reader r = { (const uint8_t *)buf, n, 0, false }; arc_read(&r, &v081r_e, sizeof v081r_e); return !r.fail; }
static void v081r_eevent(const arc_net_ev *ev) { if (ev->type == ARC_NET_EV_START) { memset(&v081r_e, 0, sizeof v081r_e); v081r_e.starts = 1; } }
static void v081r_etick(void) {
    v081r_eng *e = &v081r_e;
    e->tick++;
    e->r = e->r * 747796405u + arc_rand_u32();               /* the match generator (reseeded at match tick 0) */
    if (arc_net_key_down(0, ARC_KEY_A)) { e->akey++; if (!e->delayed_at) e->delayed_at = e->tick; }
    if (arc_net_key_pressed(0, ARC_KEY_A)) e->apress++;
    e->x += arc_net_key_down(0, ARC_KEY_D) ? 1 : 0;
    e->y += (int)(arc_net_axis(0, ARC_AXIS_LX) * 127.0f);
    e->y += v081r_outside;
    if (e->tick % 7 == 3) { arc_play(v081r_snd, 0.5f, 0); e->sounds++; }
    if (e->tick % 11 == 5) { arc_play(v081r_snd, 0.5f, 0); arc_play(v081r_snd, 0.5f, 0); e->sounds += 2; }   /* twice in one tick */
    if (e->tick % 13 == 0) { arc_fx_burst(&v081r_fx, arc_v3_make(0, 1, 0), arc_v3_make(0, 1, 0), 5); e->shots++; }
    if (e->tick % 17 == 0) { arc_event("v081r", "\"n\":%d", e->events); e->events++; }
}
static void v081r_engine(void) {
    memset(&v081r_fx, 0, sizeof v081r_fx); v081r_fx.life = 0.8f; v081r_fx.speed = 2;
    arc_app app; memset(&app, 0, sizeof app);
    app.tick = v081r_etick; app.save_state = v081r_esave; app.load_state = v081r_eload; app.net_event = v081r_eevent;
    void *m = arc__net_test_machine();
    v081r_eng plain, first; int plain_particles = 0, plain_marks = 0;
    memset(&plain, 0, sizeof plain); memset(&first, 0, sizeof first);
    for (int run = 0; run < 4; run++) {     /* 0: plain (offline), 1: synctest, 2: synctest of a broken game, 3: synctest after other draws */
        arc__net_test_use(m);
        memset(&v081r_e, 0, sizeof v081r_e); arc_particles_clear(); v081r_outside = 0;
        arc__test_sandbox(&app);
        int ev_before = arc__test_events_emitted();
        for (int k = 0; k < run * 3; k++) (void)arc_rand_u32();   /* (the match generator is reseeded at match tick 0 whatever came before) */
        if (run) { arc__test_command("net synctest 6 2"); CHECK(arc_net_stats().active); }   /* (match tick 0 is the next tick) */
        else { memset(&v081r_e, 0, sizeof v081r_e); v081r_e.starts = 1; }
        if (run == 2) arc__test_command("net input 1 07");
        for (int t = 0; t < 150; t++) {
            if (t == 30) arc__input_key(ARC_KEY_A, true);
            if (t == 34) arc__input_key(ARC_KEY_A, false);
            if (t == 50) arc__input_key(ARC_KEY_D, true);
            if (t == 90) { arc__input_key(ARC_KEY_D, false); arc__input_axis(ARC_AXIS_LX, -0.5f); }
            if (t == 100) arc__input_axis(ARC_AXIS_LX, 0);
            if (run == 2 && t == 60) v081r_outside = 1;           /* changes tick() but not the state ARC saves */
            arc__test_ticks(1);
        }
        arc_net_rb_stats s = arc_net_stats();
        if (run == 0) {
            plain = v081r_e; plain_particles = arc_particle_count(); plain_marks = arc__test_events_emitted() - ev_before;
            CHECK(!s.active && plain.tick == 150 && plain.akey == 4 && plain.apress == 1 && plain.x == 40 && plain.y < 0 && plain.sounds > 30);
            CHECK(plain.delayed_at == 31 && plain_marks == plain.events && plain_particles > 0);
        } else if (run == 1) {
            CHECK(v081r_e.starts == 1 && v081r_e.tick == 150);
            first = v081r_e;
            v081r_eng a = v081r_e, b = plain; a.delayed_at = b.delayed_at = 0; a.r = b.r = 0;   /* (the input delay moves the press; offline arc_rand differs) */
            CHECK(!memcmp(&a, &b, sizeof a));                      /* the same state as playing without rollbacks */
            CHECK(v081r_e.delayed_at == 33);                       /* pressed before tick 31, acting 2 ticks later */
            CHECK(s.desync_tick < 0 && s.rollbacks >= 140 && s.max_depth == 6 && s.resim_ticks > 800);
            CHECK(s.sounds_played == v081r_e.sounds && s.sounds_deduped > 200 && s.sounds_late == 0 && s.sounds_cancelled == 0);   /* each once */
            CHECK(arc__test_events_emitted() - ev_before == v081r_e.events);   /* each event logged once, though its tick ran up to 7 times */
            CHECK(arc_particle_count() == plain_particles);        /* the particles were rolled back with the state */
            CHECK(arc_net_match_tick() == 149 && arc_net_rollback() && !arc_net_resimulating());
        } else if (run == 2) {
            CHECK(s.desync_tick >= 55 && s.desync_tick <= 61);    /* the synctest catches the state outside save_state */
            CHECK(arc_net_input(1, NULL) != NULL);
        } else CHECK(!memcmp(&v081r_e, &first, sizeof first) && s.desync_tick < 0);   /* the same match from a different arc_rand state */
        arc_net_leave();
        CHECK(!arc_net_rollback() && !arc_net_stats().active);
        arc__test_sandbox(NULL);
    }
    arc__net_test_free(m); arc__net_test_use(NULL);
    arc_particles_clear();
    /* offline, no match: arc_net_input(0) is this machine's input now, with no delay */
    int n = 0; CHECK(arc_net_input(0, &n) != NULL && n == 39 && arc_net_input(1, NULL) == NULL && arc_net_match_tick() == -1);
}

/* ======================================================================== */
/* Part 3: ARC's snapshots hold the replayed loads' positions and the UI's  */
/* animation, so going back and playing on again gives the same ticks       */
/* ======================================================================== */
void arc__test_saves_stream(const uint8_t *data, size_t n, FILE *rec);
uint64_t arc__ui_test_cmd_hash(void);
typedef struct { int32_t sum, last, n, nulls, tick; } v081r_sv;
static v081r_sv v081r_svs; static int v081r_svmode;
static size_t v081r_svsave(void *buf, size_t cap) { if (cap >= sizeof v081r_svs) memcpy(buf, &v081r_svs, sizeof v081r_svs); return sizeof v081r_svs; }
static bool v081r_svload(const void *buf, size_t n) { if (n != sizeof v081r_svs) return false; memcpy(&v081r_svs, buf, n); return true; }
static void v081r_svtick(void) {
    v081r_sv *s = &v081r_svs; s->tick++;
    if (v081r_svmode == 1 && s->tick <= 60) { char b[16]; snprintf(b, sizeof b, "%d", s->tick); arc_save("v081r_slot", b, strlen(b)); }   /* a recording game */
    if (v081r_svmode == 1 && s->tick == 100) arc_save("v081r_slot", "999", 3);
    if (s->tick % 3 == 1) {                        /* loads now and then (Droids' title screen checks its save every tick) */
        size_t n = 0; uint8_t *d = arc_load("v081r_slot", arc_frame(), &n);
        if (d) { int v = atoi((const char *)d); s->sum += v * s->tick; s->last = v; s->n++; } else s->nulls++;
    }
    if (v081r_svmode == 2 && s->tick >= 70 && s->tick < 110) { arc_ui_begin("V081 menu", 10, 10, 120); arc_ui_label("open"); arc_ui_end(); }
    if (v081r_svmode == 2 && s->tick >= 150 && s->tick < 190) { arc_ui_begin("V081 menu 2", 30, 20, 100); arc_ui_label("still open"); arc_ui_end(); }
}
static void v081r_snapshots(void) {
    arc_app app; memset(&app, 0, sizeof app);
    app.tick = v081r_svtick; app.save_state = v081r_svsave; app.load_state = v081r_svload;
    /* a replay: arc_load gets the recorded results in order; going back must read the same ones again */
    static uint8_t stream[8192]; size_t sn = 0;
    for (int i = 1; i <= 200; i++) {
        char b[16]; int bl = snprintf(b, sizeof b, "%d", i * 7); uint16_t kl = 10; uint32_t sz = (uint32_t)bl;
        if (i == 5) sz = 0xFFFFFFFFu;               /* (one "no such save") */
        memcpy(stream + sn, &kl, 2); memcpy(stream + sn + 2, "v081r_slot", 10); memcpy(stream + sn + 12, &sz, 4); sn += 16;
        if (sz != 0xFFFFFFFFu) { memcpy(stream + sn, b, (size_t)bl); sn += (size_t)bl; }
    }
    for (int mode = 0; mode < 2; mode++) {
        FILE *rec = mode ? tmpfile() : NULL;
        arc_save_remove("v081r_slot");
        memset(&v081r_svs, 0, sizeof v081r_svs); v081r_svmode = mode;
        arc__test_saves_stream(mode ? NULL : stream, mode ? 0 : sn, rec);
        arc__test_sandbox(&app);
        if (mode) arc__test_command("timeline on");   /* (a live game going back replays its own past, as when recording) */
        arc__test_ticks(130);
        v081r_sv straight = v081r_svs;
        CHECK(straight.tick == 130 && straight.n + straight.nulls == 44 && (mode ? straight.last == 999 : (straight.nulls == 1 && straight.last == 44 * 7)));
        arc__test_command("rewind 62");               /* (from the snapshot at tick 60) */
        CHECK(v081r_svs.tick == 68);
        arc__test_ticks(62);
        CHECK(!memcmp(&v081r_svs, &straight, sizeof straight));   /* the same loads, so the same state */
        arc__test_command("rewind 100"); arc__test_ticks(100);
        CHECK(!memcmp(&v081r_svs, &straight, sizeof straight));
        arc__test_sandbox(NULL);
        arc__test_saves_stream(NULL, 0, NULL);
        if (rec) fclose(rec);
    }
    arc_save_remove("v081r_slot");
    /* the UI: a menu open from tick 70 to 109; going back to tick 64 must not show a fading ghost of it */
    arc_ui_theme th0 = arc_ui_get_theme(), th1 = th0; th1.anim = 0.2f; arc_ui_set_theme(&th1);
    memset(&v081r_svs, 0, sizeof v081r_svs); v081r_svmode = 2;
    arc__test_sandbox(&app);
    uint64_t h[4];
    arc__test_ticks(64); h[0] = arc__ui_test_cmd_hash();
    arc__test_ticks(8); h[1] = arc__ui_test_cmd_hash();           /* (the menu opening) */
    arc__test_ticks(28);
    arc__test_command("seek 64"); CHECK(v081r_svs.tick == 64); h[2] = arc__ui_test_cmd_hash();
    CHECK(h[2] == h[0] && h[1] != h[0]);
    arc__test_ticks(8); h[3] = arc__ui_test_cmd_hash();
    CHECK(h[3] == h[1]);                                          /* opening the same way as the first time */
    arc__test_ticks(50); uint64_t closed = arc__ui_test_cmd_hash();   /* tick 122: closed and faded */
    arc__test_command("seek 112"); arc__test_ticks(10);
    CHECK(arc__ui_test_cmd_hash() == closed);
    arc__test_command("seek 113"); h[0] = arc__ui_test_cmd_hash();   /* (closing: the ghost fading) */
    arc__test_command("seek 125"); arc__test_command("seek 113");
    CHECK(arc__ui_test_cmd_hash() == h[0]);
    /* a menu open since tick 150: just after the snapshot at 180 it is still fully open (not opening again: no flicker
       when a rollback loads a snapshot either) */
    arc__test_ticks(70); h[0] = arc__ui_test_cmd_hash(); arc__test_ticks(2);
    arc__test_command("seek 183"); CHECK(v081r_svs.tick == 183 && arc__ui_test_cmd_hash() == h[0]);
    arc__test_sandbox(NULL);
    arc_ui_set_theme(&th0); v081r_svmode = 0;
}

static void v081r_init(void) {
    arc__net_test_report_dir("net_desync_selftest");
    arc_sfx sfx = { ARC_WAVE_SQUARE, 440, 220, 0.001f, 0.01f, 0.01f, 0.2f, 0, 0, 0 };
    v081r_snd = arc_sound_make(&sfx);
    v081r_t = 1000.0;
    v081r_two();
    v081r_three();
    v081r_stall_drop();
    v081r_timesync();
    arc__net_test_use(NULL);
    v081r_engine();
    v081r_snapshots();
}
static void v081r_check(void) {
    CHECK(!arc_net_rollback() && arc_net_stats().tick == 0 && arc_net_waiting_for() == -1);
}
