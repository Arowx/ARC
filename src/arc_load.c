/* ARC background loading (0.7.1): arc_load_begin / arc_load_end queue the asset loaders.
 *
 * A queued load reserves its handle at once (in queue order, so ids are the same in every run). Then:
 *   read + decode: worker threads on desktop (the platform's arc__threads), else the main thread a slice per frame;
 *   finish:        the main thread, in queue order, within a time budget per frame (GPU uploads, audio slots).
 * Headless runs finish a group inside arc_load_end (deterministic). While a group without ARC_LOAD_BACKGROUND is
 * unfinished, arc__load_frame says "hold": the core runs no ticks and draws the loading screen instead.
 * Memory: items and groups are the loader's own pools (malloc'd when queued, freed when finished).
 * 0.8.6, without worker threads (web; tests with stand-ins): items may also wait for something asynchronous (state 3):
 *   the browser decoding their JPEG/PNG images (arc_app.browser_decode: up to ARC__LOAD_WEB_JOBS images at once, started
 *   ahead of the finish point, within the budget), or a pack being downloaded (ARC__LOAD_PACK items, arc_webload.c).
 *   Items after an unfinished pack are not started early: they may be in it. */

typedef struct {
    int kind, flags; uint32_t id, serial, group; char path[260];
    long long bytes; bool from_pack;
    int state;                       /* 0 queued, 1 decoding, 2 decoded (under the lock when threads run), 3 waiting (0.8.6) */
    int pack; long long counted;     /* 0.8.6 ARC__LOAD_PACK: the pack table's index; bytes already counted as done */
    unsigned polled;                 /* 0.8.6: the frame it was last looked at (once a frame is enough) */
    int nweb; arc__img *web[4]; bool *web_bad[4];   /* 0.8.6 images the browser decodes (the texture, a mesh's maps) */
    uint8_t *file; size_t file_n;    /* the file's bytes (kept while decoded data points into them) */
    bool ok; char err[400];
    arc__img img; arc__mesh_data md;
    int16_t *pcm; int frames, ch, rate;
} load_item;
typedef struct {
    int flags; bool ended, notified;
    int total, done, failed; long long bytes_total, bytes_done;
    double t0, t1;
    void (*fn)(void *user); void *user;
} load_group;

#define ARC__LOAD_MAX_THREADS 4
#define ARC__LOAD_WEB_JOBS 16             /* 0.8.6: browser image decodes running at once */
#define ARC__LOAD_AHEAD 64                /* 0.8.6: how far past the finish point decoding may start */
/* --load-items N (and the self-test): at most N files finish per frame, and headless runs no longer finish a group
   inside arc_load_end: it loads a few files a frame (frames are ticks there), still the same in every run */
int arc__load_items_per_frame = -1;   /* -1: not read yet; 0: no limit */
static struct {
    load_item **items; int count, cap;   /* queue order */
    int next_job, next_fin;               /* next to decode (workers take it under the lock), next to finish (main) */
    load_group *groups; int ngroups, gcap; /* groups[0]: unused (id 0 = all) */
    int cur; bool open;                   /* the group being queued */
    uint32_t serial; float budget_ms; bool budget_set;
    const arc__threads *th; void *lock, *cond; void *thread[ARC__LOAD_MAX_THREADS]; int nthreads; int quit;
    bool started, warmed;
    int web_jobs;                         /* 0.8.6: browser decodes running */
    unsigned frame;                       /* 0.8.6: arc__load_frame calls */
} LQ;

static void lq_lock(void) { if (LQ.nthreads) LQ.th->lock(LQ.lock); }
static void lq_unlock(void) { if (LQ.nthreads) LQ.th->unlock(LQ.lock); }

/* ---- decoding (any thread: no handles, no logging, no arenas) ---- */
static void load_decode(load_item *it) {
    it->file = arc__file_read_alloc(it->path, &it->file_n, &it->from_pack);
    if (!it->file) { snprintf(it->err, sizeof it->err, "cannot load %s %s", it->kind == ARC__LOAD_TEXTURE ? "texture" : it->kind == ARC__LOAD_MESH ? "mesh" : it->kind == ARC__LOAD_SOUND ? "sound" : "music", it->path); return; }
    switch (it->kind) {
    case ARC__LOAD_TEXTURE:
        it->ok = arc__img_decode(it->file, it->file_n, it->flags, &it->img) && it->img.w > 0;
        if (!it->ok) snprintf(it->err, sizeof it->err, "cannot load texture %s", it->path);
        if (!it->img.comp || it->img.c.owned) { free(it->file); it->file = NULL; }   /* nothing points into it */
        break;
    case ARC__LOAD_MESH:
        it->ok = arc__mesh_parse(it->file, it->file_n, it->path, &it->md);   /* compressed maps may point into the file */
        if (!it->ok) snprintf(it->err, sizeof it->err, "%s", it->md.err);
        break;
    case ARC__LOAD_SOUND: {
        const char *e = arc__sound_decode(it->file, it->file_n, &it->pcm, &it->frames, &it->ch, &it->rate);
        it->ok = e == NULL; if (e) snprintf(it->err, sizeof it->err, "%s", e);
        free(it->file); it->file = NULL;
        break; }
    default: it->ok = true; break;        /* music stays compressed: the mixer decodes it as it plays */
    }
}
static void load_worker(void *arg) {
    (void)arg;
    LQ.th->lock(LQ.lock);
    for (;;) {
        while (!LQ.quit && LQ.next_job >= LQ.count) LQ.th->wait(LQ.cond, LQ.lock);
        if (LQ.quit) break;
        load_item *it = LQ.items[LQ.next_job++]; it->state = 1;
        LQ.th->unlock(LQ.lock);
        load_decode(it);
        LQ.th->lock(LQ.lock);
        it->state = 2; LQ.th->wake_all(LQ.cond);
    }
    LQ.th->unlock(LQ.lock);
}
/* ---- 0.8.6 asynchronous items (no worker threads): browser image decoding, pack downloads ---- */
static const arc__imgdec *load_imgdec(void) { return LQ.nthreads ? NULL : arc__imgdec_get(); }
static void load_web_add(load_item *it, arc__img *img, bool *bad) {
    if (img->src && !img->rgba && !img->comp && it->nweb < 4) { it->web[it->nweb] = img; it->web_bad[it->nweb] = bad; it->nweb++; }
}
/* ARC's own decoder for one of the item's images (the browser refused, failed, or we can't wait) */
static void load_web_fallback(load_item *it, int k) {
    arc__img *img = it->web[k]; const arc__imgdec *dec = arc__imgdec_get();
    if (img->web && !img->web_done) LQ.web_jobs--;
    if (img->web && dec) dec->release(img->web);
    img->web = 0; img->web_done = false;
    const uint8_t *src = img->src; size_t n = img->src_n;
    bool ok = arc__img_decode(src, n, it->kind == ARC__LOAD_TEXTURE ? it->flags : ARC_TEX_MESH, img) && img->w > 0;
    arc__webimg_stats[2]++;
    if (!ok) { if (it->web_bad[k]) *it->web_bad[k] = true; else { it->ok = false; snprintf(it->err, sizeof it->err, "cannot load texture %s", it->path); } }
}
/* start an item: decode it now (state 2), or hand its images to the browser / wait for its pack (state 3) */
static void load_begin(load_item *it) {
    const arc__imgdec *dec = load_imgdec();
    if (it->kind == ARC__LOAD_PACK) { it->state = 3; return; }
    if (!dec || (it->kind != ARC__LOAD_TEXTURE && it->kind != ARC__LOAD_MESH)) { load_decode(it); it->state = 2; return; }
    it->file = arc__file_read_alloc(it->path, &it->file_n, &it->from_pack);
    if (!it->file) { snprintf(it->err, sizeof it->err, "cannot load %s %s", it->kind == ARC__LOAD_TEXTURE ? "texture" : "mesh", it->path); it->state = 2; return; }
    if (it->kind == ARC__LOAD_TEXTURE) {
        it->ok = arc__img_decode_or_defer(it->file, it->file_n, it->flags, &it->img, true) && it->img.w > 0;
        if (!it->ok) snprintf(it->err, sizeof it->err, "cannot load texture %s", it->path);
        if (it->ok) load_web_add(it, &it->img, NULL);
        if (!it->nweb && (!it->img.comp || it->img.c.owned)) { free(it->file); it->file = NULL; }
    } else {
        it->ok = arc__mesh_parse_ex(it->file, it->file_n, it->path, &it->md, true);
        if (!it->ok) snprintf(it->err, sizeof it->err, "%s", it->md.err);
        else {
            if (it->md.has_tex && !it->md.tex_bad) load_web_add(it, &it->md.tex, &it->md.tex_bad);
            for (int i = 0; i < 3; i++) if (it->md.has_mat && !it->md.map_bad[i]) load_web_add(it, &it->md.maps[i], &it->md.map_bad[i]);
        }
    }
    bool waiting = false;
    for (int k = 0; k < it->nweb; k++) {
        arc__img *img = it->web[k]; bool alpha; int w, h, type = arc__img_browser_type(img->src, img->src_n, &alpha, &w, &h);
        img->web = type ? dec->start(img->src, img->src_n, type, alpha) : 0;
        if (img->web) { LQ.web_jobs++; waiting = true; } else load_web_fallback(it, k);
    }
    it->state = waiting ? 3 : 2;
}
/* look at a waiting item; force: images still decoding are decoded here by ARC instead (a pack can't be forced) */
static void load_poll(load_item *it, bool force) {
    if (it->polled == LQ.frame && !force) return;
    it->polled = LQ.frame;
    if (it->kind == ARC__LOAD_PACK) {
        long long got = 0; int r = arc__pack_poll(it->pack, &got);
        load_group *g = &LQ.groups[it->group];
        long long add = (it->bytes > 0 && got > it->bytes ? it->bytes : got) - it->counted;
        if (add > 0) { g->bytes_done += add; it->counted += add; }
        if (r != 0) { it->ok = r > 0; it->state = 2; }
        return;
    }
    const arc__imgdec *dec = arc__imgdec_get(); bool pending = false;
    for (int k = 0; k < it->nweb; k++) {
        arc__img *img = it->web[k];
        if (!img->web || img->web_done) continue;
        int w = 0, h = 0, r = dec ? dec->poll(img->web, &w, &h) : -1;
        if (r > 0 && w > 0 && h > 0) { img->web_done = true; img->w = w; img->h = h; LQ.web_jobs--; arc__webimg_stats[0]++; }
        else if (r < 0 || r > 0 || force) load_web_fallback(it, k);
        else pending = true;
    }
    if (!pending) it->state = 2;
}
/* start decoding the items after the finish point (images go to the browser; files are read and parsed) */
static void load_ahead(double t0, double budget) {
    if (LQ.nthreads || !load_imgdec()) return;
    int started = 0;
    for (int i = LQ.next_fin; i < LQ.count && i < LQ.next_fin + ARC__LOAD_AHEAD; i++) {
        load_item *it = LQ.items[i];
        if (it->kind == ARC__LOAD_PACK && it->state != 2) break;      /* what follows may be inside it */
        if (it->state != 0 || (it->kind != ARC__LOAD_TEXTURE && it->kind != ARC__LOAD_MESH)) continue;
        if (LQ.web_jobs >= ARC__LOAD_WEB_JOBS) break;
        if (started && arc_real_time() - t0 >= budget) break;          /* at least one a frame */
        it->state = 1; load_begin(it); started++;
    }
}
static void load_poll_all(void) {
    if (LQ.nthreads) return;
    for (int i = LQ.next_fin; i < LQ.count; i++) if (LQ.items[i]->state == 3) load_poll(LQ.items[i], false);
}
static int load_items_limit(void) {
    if (arc__load_items_per_frame < 0) { const char *v = arc_arg_value("load-items"); arc__load_items_per_frame = v ? arc_maxi(atoi(v), 0) : 0; }
    return arc__load_items_per_frame;
}
static void load_start(void) {
    if (LQ.started) return;
    LQ.started = true;
    if (!LQ.budget_set) LQ.budget_ms = 4.0f;
    if (!LQ.warmed) { arc__decoders_warm(); LQ.warmed = true; }
    const arc__threads *th = arc__load_threads();
    if (!th || arc_headless()) return;     /* headless: deterministic, on the main thread; web: no threads */
    int n = th->cpus ? th->cpus() - 1 : 1;
    const char *v = arc_arg_value("load-threads"); if (v) n = atoi(v);
    n = arc_clampi(n, 0, ARC__LOAD_MAX_THREADS);
    if (!n) return;
    LQ.th = th; LQ.lock = th->lock_create(); LQ.cond = th->cond_create();
    if (!LQ.lock || !LQ.cond) return;
    LQ.nthreads = n;                       /* set before starting: the workers lock from the start */
    int ok = 0;
    for (int i = 0; i < n; i++) { LQ.thread[ok] = th->start(load_worker, NULL); if (LQ.thread[ok]) ok++; }
    LQ.nthreads = ok;
}

/* ---- finishing (main thread, queue order) ---- */
static void load_finish(load_item *it) {
    load_group *g = &LQ.groups[it->group];
    if (!it->ok && it->err[0]) arc_log("%s", it->err);
    bool ok = it->ok;
    switch (it->kind) {
    case ARC__LOAD_TEXTURE:
        if (ok) ok = arc__tex_finish(it->id, it->serial, &it->img); else arc__tex_fail(it->id, it->serial);
        arc__img_free(&it->img);
        break;
    case ARC__LOAD_MESH:
        if (ok) ok = arc__mesh_finish(it->id, it->serial, &it->md, it->path); else arc__mesh_fail(it->id, it->serial);
        arc__mesh_data_free(&it->md);
        break;
    case ARC__LOAD_SOUND:
        if (ok) { arc__sound_finish(it->id, it->pcm, it->frames, it->ch, it->rate); it->pcm = NULL; }
        break;
    case ARC__LOAD_PACK:
        ok = arc__pack_finish(it->pack);                           /* mounts it (or says why not) */
        break;
    default:
        if (ok) { ok = arc__music_finish(it->id, it->file, it->file_n); if (!ok) arc__music_fail(it->id); } else arc__music_fail(it->id);
        break;
    }
    if (ok && it->kind != ARC__LOAD_MUSIC && it->kind != ARC__LOAD_PACK)
        arc__hot_watch_ex(it->kind == ARC__LOAD_TEXTURE ? ARC__HOT_TEXTURE : it->kind == ARC__LOAD_MESH ? ARC__HOT_MESH : ARC__HOT_SOUND, it->id, it->path, it->from_pack);
    free(it->file); free(it->pcm);
    g->done++; g->bytes_done += it->bytes - it->counted; if (!ok) g->failed++;
    if (g->ended && g->done == g->total) g->t1 = arc_real_time();
    free(it);
}
/* finish the next item; decode it here if no thread has it. wait: block for a worker's item (else give up); force
   (0.8.6): an item waiting for the browser's decoder is decoded here instead */
static bool load_step(bool wait, bool force) {
    if (LQ.next_fin >= LQ.count) return false;
    load_item *it = LQ.items[LQ.next_fin];
    lq_lock();
    if (it->state == 0 && (LQ.nthreads ? LQ.next_job == LQ.next_fin && wait : true)) {   /* nobody has it: decode it here */
        if (LQ.next_job <= LQ.next_fin) LQ.next_job = LQ.next_fin + 1;
        it->state = 1; lq_unlock();
        if (LQ.nthreads) { load_decode(it); it->state = 2; } else load_begin(it);
        lq_lock();
    }
    if (it->state == 3) { lq_unlock(); load_poll(it, force); lq_lock(); }
    while (wait && it->state != 2 && LQ.nthreads) LQ.th->wait(LQ.cond, LQ.lock);
    int st = it->state;
    lq_unlock();
    if (st != 2) return false;
    LQ.items[LQ.next_fin++] = NULL;
    load_finish(it);
    lq_lock();
    if (LQ.next_fin == LQ.count) { LQ.next_fin = LQ.next_job = LQ.count = 0; }   /* all done: the list starts over */
    lq_unlock();
    return true;
}
static bool group_done(const load_group *g) { return g->ended && g->done >= g->total; }
static void load_notify(void) {
    for (int i = 1; i < LQ.ngroups; i++) {
        load_group *g = &LQ.groups[i];
        if (!g->notified && group_done(g)) { g->notified = true; if (g->fn) { void (*fn)(void *) = g->fn; g->fn = NULL; fn(g->user); } }
    }
}

/* ---- public ---- */
bool arc__load_queueing(void) { return LQ.open; }
uint32_t arc__load_queue(int kind, const char *path, int flags) {
    static const char *names[] = { "", "texture", "mesh", "sound", "music" };
    bool fp = false; long long size = path ? arc__file_size(path, &fp) : -1;
    if (size < 0 && path && !LQ.nthreads) {   /* 0.8.6: in a pack still to come (web builds with WEB_PACKS) */
        long long fs = 0; int pk = arc__pack_for_file(path, &fs);
        if (pk >= 0) {
            bool queued = false;
            for (int i = LQ.next_fin; i < LQ.count && !queued; i++) queued = LQ.items[i]->kind == ARC__LOAD_PACK && LQ.items[i]->pack == pk;
            if (!queued) arc__load_queue_pack(pk);
            size = fs;
        }
    }
    if (size < 0) { arc_log("cannot load %s %s", names[kind], path ? path : "(null)"); return 0; }
    load_start();
    uint32_t serial = ++LQ.serial; if (!serial) serial = ++LQ.serial;
    uint32_t id = kind == ARC__LOAD_TEXTURE ? arc__tex_reserve(flags, serial) : kind == ARC__LOAD_MESH ? arc__mesh_reserve(serial)
                : kind == ARC__LOAD_SOUND ? arc__sound_reserve() : arc__music_reserve();
    if (!id) return 0;
    load_item *it = (load_item *)calloc(1, sizeof *it);
    if (!it) return 0;
    it->kind = kind; it->flags = flags; it->id = id; it->serial = serial; it->group = (uint32_t)LQ.cur; it->bytes = size;
    snprintf(it->path, sizeof it->path, "%s", path);
    lq_lock();
    if (LQ.count == LQ.cap) {
        int cap = LQ.cap ? LQ.cap * 2 : 64; load_item **grown = (load_item **)realloc(LQ.items, sizeof(load_item *) * (size_t)cap);
        if (!grown) { lq_unlock(); free(it); return id; }
        LQ.items = grown; LQ.cap = cap;
    }
    LQ.items[LQ.count++] = it;
    if (LQ.nthreads) LQ.th->wake_all(LQ.cond);
    lq_unlock();
    load_group *g = &LQ.groups[LQ.cur]; g->total++; g->bytes_total += size;
    return id;
}
/* 0.8.6: a pack being downloaded, as an item of the open group (it mounts in queue order); the group's id */
uint32_t arc__load_queue_pack(int pack) {
    if (!LQ.open) return 0;
    load_start();
    load_item *it = (load_item *)calloc(1, sizeof *it);
    if (!it) return 0;
    it->kind = ARC__LOAD_PACK; it->pack = pack; it->group = (uint32_t)LQ.cur; it->bytes = arc__pack_bytes(pack);
    it->state = 3; it->polled = LQ.frame - 1;   /* downloading already */
    snprintf(it->path, sizeof it->path, "%s", arc__pack_name(pack));
    if (LQ.count == LQ.cap) {
        int cap = LQ.cap ? LQ.cap * 2 : 64; load_item **grown = (load_item **)realloc(LQ.items, sizeof(load_item *) * (size_t)cap);
        if (!grown) { free(it); return 0; }
        LQ.items = grown; LQ.cap = cap;
    }
    LQ.items[LQ.count++] = it;
    load_group *g = &LQ.groups[LQ.cur]; g->total++; g->bytes_total += it->bytes;
    return (uint32_t)LQ.cur;
}
uint32_t arc__load_group_open(void) { return LQ.open ? (uint32_t)LQ.cur : 0; }
void arc_load_begin(int flags) {
    if (LQ.open) { arc_log("arc_load_begin: a group is already open (groups do not nest)"); return; }
    if (LQ.ngroups == LQ.gcap) {
        int cap = LQ.gcap ? LQ.gcap * 2 : 16; load_group *grown = (load_group *)realloc(LQ.groups, sizeof(load_group) * (size_t)cap);
        if (!grown) return;
        LQ.groups = grown; LQ.gcap = cap;
        if (!LQ.ngroups) { memset(&LQ.groups[0], 0, sizeof(load_group)); LQ.ngroups = 1; }
    }
    LQ.cur = LQ.ngroups++; LQ.open = true;
    load_group *g = &LQ.groups[LQ.cur]; memset(g, 0, sizeof *g);
    g->flags = flags; g->t0 = arc_real_time();
}
arc_load_group arc_load_end(void) {
    arc_load_group r = { 0 };
    if (!LQ.open) { arc_log("arc_load_end without arc_load_begin"); return r; }
    LQ.open = false; r.id = (uint32_t)LQ.cur;
    load_group *g = &LQ.groups[LQ.cur]; g->ended = true;
    if (g->done == g->total) g->t1 = arc_real_time();
    if (arc_headless() && !load_items_limit()) arc_load_wait(r);   /* the deterministic rule: done before arc_load_end returns */
    return r;
}
static bool group_ok(arc_load_group g) { return g.id && (int)g.id < LQ.ngroups; }
arc_load_info arc_load_status(arc_load_group g) {
    arc_load_info s; memset(&s, 0, sizeof s);
    int a = group_ok(g) ? (int)g.id : 1, b = group_ok(g) ? (int)g.id + 1 : LQ.ngroups;
    double t0 = 0, t1 = 0; bool all = true;
    for (int i = a; i < b; i++) {
        const load_group *q = &LQ.groups[i];
        s.total += q->total; s.done += q->done; s.failed += q->failed; s.bytes_total += q->bytes_total; s.bytes_done += q->bytes_done;
        if (i == a || q->t0 < t0) t0 = q->t0;
        if (q->t1 > t1) t1 = q->t1;
        all = all && group_done(q);
    }
    s.progress = s.bytes_total > 0 ? (float)((double)s.bytes_done / (double)s.bytes_total) : (s.total ? (float)s.done / (float)s.total : 1.0f);
    if (all) s.progress = 1.0f; else if (s.progress > 0.999f) s.progress = 0.999f;
    if (b > a) s.seconds = (all ? t1 : arc_real_time()) - t0;
    return s;
}
bool  arc_load_done(arc_load_group g) {
    if (group_ok(g)) return group_done(&LQ.groups[g.id]);
    for (int i = 1; i < LQ.ngroups; i++) if (!group_done(&LQ.groups[i])) return false;
    return true;
}
float arc_load_progress(arc_load_group g) { return arc_load_status(g).progress; }
void  arc_load_wait(arc_load_group g) {
    while (!arc_load_done(g) && LQ.next_fin < LQ.count) if (!load_step(true, true)) break;   /* (a download can't be waited for) */
    load_notify();
}
void  arc_load_on_done(arc_load_group g, void (*fn)(void *user), void *user) {
    if (!group_ok(g) || !fn) return;
    load_group *q = &LQ.groups[g.id];
    if (q->notified || group_done(q)) { q->notified = true; fn(user); return; }
    q->fn = fn; q->user = user;
}
void  arc_load_set_budget(float ms) { LQ.budget_ms = ms > 0.1f ? ms : 0.1f; LQ.budget_set = true; }
double arc__load_busy_ms;                 /* 0.8.6: main-thread time spent in arc__load_frame (measurements) */
bool  arc__load_frame(void) {
    if (LQ.next_fin < LQ.count) {
        double tb = arc_real_time();
        double t0 = arc_real_time(), budget = (double)LQ.budget_ms / 1000.0; int limit = load_items_limit(), n = 0;
        LQ.frame++; load_poll_all();                                           /* 0.8.6: downloads and browser decodes */
        if (limit) { while (n < limit && load_step(arc_headless(), false)) n++; }   /* a fixed number: headless waits for each */
        else while (load_step(false, false) && arc_real_time() - t0 < budget) {}
        load_ahead(t0, budget);
        arc__load_busy_ms += (arc_real_time() - tb) * 1000.0;
    }
    load_notify();
    for (int i = 1; i < LQ.ngroups; i++) { const load_group *g = &LQ.groups[i]; if (!(g->flags & ARC_LOAD_BACKGROUND) && g->ended && !group_done(g)) return true; }
    return false;
}
void arc__load_shutdown(void) {
    if (LQ.nthreads) {
        LQ.th->lock(LQ.lock); LQ.quit = 1; LQ.th->wake_all(LQ.cond); LQ.th->unlock(LQ.lock);
        for (int i = 0; i < LQ.nthreads; i++) LQ.th->join(LQ.thread[i]);
        LQ.th->cond_destroy(LQ.cond); LQ.th->lock_destroy(LQ.lock); LQ.nthreads = 0;
    }
}
/* ARC's loading screen: the text and a bar, centred, on the clear colour (a game's arc_app.draw_loading replaces it) */
void arc_draw_loading_screen(float progress, const char *text) {
    float w = (float)arc_target_width(), h = (float)arc_target_height(), s = arc_maxf(1.0f, arc_minf(w, h) / 360.0f);
    float bw = arc_minf(w * 0.5f, 480.0f * s), bh = 6.0f * s, x = (w - bw) * 0.5f, y = h * 0.5f;
    progress = arc_clampf(progress, 0, 1);
    arc_draw_text_ex(w * 0.5f, y - 10.0f * s, 2.0f * s, 0xC8D0DC, ARC_TEXT_CENTER | ARC_TEXT_BOTTOM, text ? text : "Loading");
    arc_draw_rect(x - s, y - s, bw + 2 * s, bh + 2 * s, 0x303844);
    arc_draw_rect(x, y, bw * progress, bh, 0xC8D0DC);
}
void arc__draw_loading(float progress) { arc_draw_loading_screen(progress, "Loading"); }
