/* ARC 0.8.6: faster web loading.
 *   - Browser image decoding for the loader (arc_app.browser_decode): JPEG/PNG bytes go to the browser's own decoder
 *     (createImageBitmap, ImageDecoder or an <img>, chosen per browser), off the main thread; the result goes straight
 *     to a GL texture (arc_gl.c's tex_create_web), or comes back as RGBA bytes when the CPU needs them.
 *   - Packs fetched on demand (arc_pack_fetch): a web build made with WEB_PACKS preloads a small first package and
 *     lists its other packs in arc_packs.txt ("pack NAME SIZE HASH", "file PACK SIZE PATH" lines, tools/web_packs.py);
 *     they are downloaded with fetch() and kept in the Cache API under "<name>?arc=<hash>", so a revisit reads them from
 *     the cache. Desktop and headless read packs (or loose files) from disk at once.
 * The browser parts are JavaScript inside EM_JS hooks; they talk to C only through polling (no callbacks into wasm). */

const arc__imgdec *arc__imgdec_hook;
const arc__fetcher *arc__fetcher_hook;
int  arc__webimg_stats[4];
bool arc__webimg_bytes;               /* --browser-decode=bytes: read the browser's pixels back instead of GPU-direct */

#if defined(__EMSCRIPTEN__) && !defined(ARC_HEADLESS_ONLY)
/* ---- browser image decoding ---- */
EM_JS(int, web_img_init, (int want), {
    var A = Module.arcImg = Module.arcImg || { jobs: {}, next: 1 };
    var hasID = typeof ImageDecoder === 'function', hasCIB = typeof createImageBitmap === 'function';
    var ua = navigator.userAgentData, chromium = !!(ua && ua.brands && ua.brands.some(function (b) { return /Chromium/.test(b.brand); }));
    var m = want;
    if ((m == 1 && !hasID) || (m == 2 && !hasCIB)) m = 0;
    /* Chromium: createImageBitmap decodes off the main thread. Firefox: only ImageDecoder does (createImageBitmap is
       slower there). Safari (no ImageDecoder): an <img> and decode(). */
    if (!m) m = chromium && hasCIB ? 2 : hasID ? 1 : 3;
    A.method = m;
    A.alphaOk = hasCIB && m != 3;   /* see-through PNGs only through createImageBitmap's premultiplyAlpha 'none' */
    return m;
});
EM_JS(int, web_img_start, (const uint8_t *p, int n, int type, int alpha), {
    var A = Module.arcImg; if (!A) return 0;
    var m = A.method;
    if (alpha) { if (!A.alphaOk) return 0; m = 2; }   /* ImageDecoder frames and <img> come premultiplied */
    var mime = type == 1 ? 'image/jpeg' : 'image/png', bytes = HEAPU8.slice(p, p + n);
    var id = A.next++, job = { s: 0, w: 0, h: 0, src: null }; A.jobs[id] = job;
    if (A.failAll) { job.s = -1; return id; }   /* tests: every decode fails (ARC's decoder must take over) */
    function ok(src, w, h) { if (A.jobs[id] !== job) { if (src && src.close) src.close(); return; } job.src = src; job.w = w; job.h = h; job.s = 1; }
    function fail(e) { job.s = -1; job.err = String(e); }
    try {
        if (m == 1) {
            var d = new ImageDecoder({ type: mime, data: bytes, colorSpaceConversion: 'none' });
            d.decode({ frameIndex: 0 }).then(function (r) { d.close(); ok(r.image, r.image.displayWidth, r.image.displayHeight); }, function (e) { d.close(); fail(e); });
        } else if (m == 2) {
            createImageBitmap(new Blob([bytes], { type: mime }), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' }).then(function (b) { ok(b, b.width, b.height); }, fail);
        } else {
            var u = URL.createObjectURL(new Blob([bytes], { type: mime })), im = new Image();
            im.src = u;
            im.decode().then(function () { URL.revokeObjectURL(u); ok(im, im.naturalWidth, im.naturalHeight); }, function (e) { URL.revokeObjectURL(u); fail(e); });
        }
    } catch (e) { fail(e); }
    return id;
});
EM_JS(int, web_img_poll, (int id, int *w, int *h), {
    var j = Module.arcImg && Module.arcImg.jobs[id];
    if (!j) return -1;
    if (j.s == 1) { HEAP32[w >> 2] = j.w; HEAP32[h >> 2] = j.h; }
    return j.s;
});
EM_JS(void, web_img_release, (int id), {
    var A = Module.arcImg, j = A && A.jobs[id];
    if (!j) return;
    if (j.src && j.src.close) j.src.close();
    delete A.jobs[id];
});
/* the image into the texture bound to TEXTURE_2D: no flip, no premultiplying, no colour conversion (normal and ORM
   maps are data, not colours) */
EM_JS(int, web_img_upload, (int id, int w, int h), {
    var j = Module.arcImg && Module.arcImg.jobs[id], gl = GLctx;
    if (!j || j.s != 1 || !j.src || j.w != w || j.h != h) return 0;
    while (gl.getError()) {}
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false); gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false); gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
    var ok = 1;
    try { gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, j.src); if (gl.getError()) ok = 0; } catch (e) { ok = 0; }
    gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.BROWSER_DEFAULT_WEBGL);
    return ok;
});
/* RGBA bytes back: through a scratch texture and framebuffer (exact; a 2D canvas would premultiply them) */
EM_JS(int, web_img_pixels, (int id, uint8_t *out, int w, int h), {
    var j = Module.arcImg && Module.arcImg.jobs[id], gl = GLctx;
    if (!j || j.s != 1 || !j.src || j.w != w || j.h != h) return 0;
    var df = gl.getParameter(gl.DRAW_FRAMEBUFFER_BINDING), rf = gl.getParameter(gl.READ_FRAMEBUFFER_BINDING), tb = gl.getParameter(gl.TEXTURE_BINDING_2D);
    var t = gl.createTexture(), f = gl.createFramebuffer(), ok = 1;
    while (gl.getError()) {}
    gl.bindTexture(gl.TEXTURE_2D, t);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false); gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false); gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
    try {
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, j.src);
        gl.bindFramebuffer(gl.FRAMEBUFFER, f);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t, 0);
        if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) != gl.FRAMEBUFFER_COMPLETE) ok = 0;
        else gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, HEAPU8.subarray(out, out + w * h * 4));
        if (gl.getError()) ok = 0;
    } catch (e) { ok = 0; }
    gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.BROWSER_DEFAULT_WEBGL);
    gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, df); gl.bindFramebuffer(gl.READ_FRAMEBUFFER, rf); gl.bindTexture(gl.TEXTURE_2D, tb);
    gl.deleteFramebuffer(f); gl.deleteTexture(t);
    return ok;
});
static int  wimg_start(const uint8_t *d, size_t n, int type, bool alpha) { return n > 0x7FFFFFFF ? 0 : web_img_start(d, (int)n, type, alpha ? 1 : 0); }
static bool wimg_pixels(int job, uint8_t *rgba, int w, int h) { return web_img_pixels(job, rgba, w, h) != 0; }
static arc__imgdec g_web_imgdec = { wimg_start, web_img_poll, wimg_pixels, web_img_release, "browser" };
/* for arc_gl.c: upload a decoded job into the texture now bound */
static bool arc__web_tex_upload(int job, int w, int h) { return web_img_upload(job, w, h) != 0; }

/* ---- pack downloads: fetch(), streamed (progress), kept in the Cache API by content hash ---- */
EM_JS(int, web_fetch_start, (const char *url, const char *key), {
    var F = Module.arcFetch = Module.arcFetch || { jobs: {}, next: 1 };
    var u = new URL(UTF8ToString(url), location.href).href, k = key ? new URL(UTF8ToString(key), location.href).href : null;
    var id = F.next++, job = { s: 0, got: 0, total: 0, buf: null, cached: 0 }; F.jobs[id] = job;
    (async function () {
        var cache = null;
        if (k && typeof caches !== 'undefined') { try { cache = await caches.open('arc-packs'); } catch (e) { cache = null; } }
        if (cache) {
            try {
                var hit = await cache.match(k);
                if (hit) { var hb = new Uint8Array(await hit.arrayBuffer()); job.buf = hb; job.got = job.total = hb.length; job.cached = 1; job.s = 1; return; }
            } catch (e) {}
        }
        var r = await fetch(u);
        if (!r.ok) throw new Error('HTTP ' + r.status);
        job.total = +(r.headers.get('Content-Length') || 0);
        if (cache) {   /* the browser stores a copy of the stream itself (off the main thread); then drops older builds' copies */
            var path = new URL(k).pathname;
            cache.put(k, r.clone()).then(function () { return cache.keys(); }).then(function (ks) {
                for (var q = 0; q < ks.length; q++) if (new URL(ks[q].url).pathname === path && ks[q].url !== k) cache.delete(ks[q]);
            }).catch(function () {});
        }
        var buf, rd = r.body && r.body.getReader ? r.body.getReader() : null;
        if (rd) {   /* streamed, for progress; straight into one buffer when the size is known */
            var parts = [], one = job.total > 0 ? new Uint8Array(job.total) : null;
            for (;;) {
                var c = await rd.read(); if (c.done) break;
                if (one && job.got + c.value.length <= one.length) one.set(c.value, job.got); else { if (one) { parts.push(one.subarray(0, job.got)); one = null; } parts.push(c.value); }
                job.got += c.value.length;
            }
            if (one && job.got == one.length) buf = one;
            else { buf = new Uint8Array(job.got); var o = 0; for (var i = 0; i < parts.length; i++) { buf.set(parts[i], o); o += parts[i].length; } }
        } else { buf = new Uint8Array(await r.arrayBuffer()); job.got = buf.length; }
        job.buf = buf; job.s = 1;
    })().catch(function (e) { job.s = -1; console.log('ARC log pack ' + u + ': ' + e); });
    return id;
});
EM_JS(int, web_fetch_poll, (int id, double *got, double *total), {
    var j = Module.arcFetch && Module.arcFetch.jobs[id];
    if (!j) return -1;
    HEAPF64[got >> 3] = j.got; HEAPF64[total >> 3] = j.total;
    return j.s;
});
EM_JS(uint8_t *, web_fetch_take, (int id, int *size), {
    var j = Module.arcFetch && Module.arcFetch.jobs[id];
    if (!j || j.s != 1 || !j.buf) return 0;
    var p = _malloc(j.buf.length + 1);
    if (!p) return 0;
    HEAPU8.set(j.buf, p); HEAP32[size >> 2] = j.buf.length; j.buf = null;
    return p;
});
EM_JS(void, web_fetch_release, (int id), { if (Module.arcFetch) delete Module.arcFetch.jobs[id]; });
EM_JS(int, web_fetch_cached, (int id), { var j = Module.arcFetch && Module.arcFetch.jobs[id]; return j ? j.cached : 0; });
static int wf_poll(int job, long long *got, long long *total) {
    double g = 0, t = 0; int r = web_fetch_poll(job, &g, &t);
    *got = (long long)g; *total = (long long)t; return r;
}
static uint8_t *wf_take(int job, size_t *size) { int n = 0; uint8_t *p = web_fetch_take(job, &n); *size = (size_t)n; return p; }
static const arc__fetcher g_web_fetcher = { web_fetch_start, wf_poll, wf_take, web_fetch_release, web_fetch_cached };
#endif

/* the image decoder for queued loads: a test's stand-in, else the browser's when browser decoding is on, else NULL */
const arc__imgdec *arc__imgdec_get(void) {
    if (arc__imgdec_hook) return arc__imgdec_hook;
#if defined(__EMSCRIPTEN__) && !defined(ARC_HEADLESS_ONLY)
    static int on = -1;
    if (on < 0) {
        const char *bd = arc_arg_value("browser-decode"), *id = arc_arg_value("image-decoder");
        on = (G.app.browser_decode || arc_arg_flag("browser-decode") || bd) && !arc_arg_flag("no-browser-decode") && !(bd && !strcmp(bd, "0"));
        arc__webimg_bytes = bd && !strcmp(bd, "bytes");
        if (on) {
            int want = !id ? 0 : !strcmp(id, "imagedecoder") ? 1 : !strcmp(id, "bitmap") ? 2 : !strcmp(id, "img") ? 3 : 0;
            int m = web_img_init(want);
            static const char *names[] = { "off", "ImageDecoder", "createImageBitmap", "HTMLImageElement" };
            g_web_imgdec.name = names[m >= 0 && m <= 3 ? m : 0];
            on = m > 0;
            if (on) arc_log("browser image decoding: %s%s", g_web_imgdec.name, arc__webimg_bytes ? " (pixels read back)" : "");
        }
    }
    return on ? &g_web_imgdec : NULL;
#else
    return NULL;
#endif
}
/* the pack downloader: a test's stand-in, else the browser's (none on desktop, where packs come from disk) */
static const arc__fetcher *wp_fetcher(void) {
    if (arc__fetcher_hook) return arc__fetcher_hook;
#if defined(__EMSCRIPTEN__) && !defined(ARC_HEADLESS_ONLY)
    return &g_web_fetcher;
#else
    return NULL;
#endif
}

/* ---- the pack table and the build's manifest (arc_packs.txt) ---- */
#define ARC__WPACK_MAX 64
typedef struct { char name[128], hash[72]; long long size, got; int state, job; bool listed, from_cache; } wpack;
typedef struct { char path[200]; int pack; long long size; } wfile;
static struct {
    bool read, have;                  /* manifest looked for / found */
    wpack packs[ARC__WPACK_MAX]; int npacks;
    wfile *files; int nfiles, fcap;
} WP;

static int wp_add(const char *name) {
    for (int i = 0; i < WP.npacks; i++) if (!strcmp(WP.packs[i].name, name)) return i;
    if (WP.npacks >= ARC__WPACK_MAX || strlen(name) >= sizeof WP.packs[0].name) return -1;
    wpack *p = &WP.packs[WP.npacks]; memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", name);
    return WP.npacks++;
}
static void wp_parse(const char *t) {
    while (*t) {
        const char *e = strchr(t, '\n'); size_t n = e ? (size_t)(e - t) : strlen(t);
        char line[512]; if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, t, n); line[n] = 0;
        if (n && line[n - 1] == '\r') line[n - 1] = 0;
        char a[128], b[72]; long long sz = 0; int used = 0;
        if (sscanf(line, "pack %127s %lld %71s", a, &sz, b) == 3) {
            int i = wp_add(a);
            if (i >= 0) { WP.packs[i].listed = true; WP.packs[i].size = sz; snprintf(WP.packs[i].hash, sizeof WP.packs[i].hash, "%s", b); }
        } else if (sscanf(line, "file %127s %lld %n", a, &sz, &used) == 2 && used > 0 && line[used]) {
            int i = wp_add(a);
            if (i >= 0) {
                if (WP.nfiles == WP.fcap) {
                    int cap = WP.fcap ? WP.fcap * 2 : 256; wfile *g = (wfile *)realloc(WP.files, sizeof(wfile) * (size_t)cap);
                    if (!g) return;
                    WP.files = g; WP.fcap = cap;
                }
                wfile *f = &WP.files[WP.nfiles++]; pack_norm(line + used, f->path, sizeof f->path); f->pack = i; f->size = sz;
            }
        }
        t += n; if (e) t = e + 1;
    }
}
static void wp_manifest(void) {
    if (WP.read) return;
    WP.read = true;
    FILE *f = fopen("arc_packs.txt", "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *t = n > 0 ? (char *)malloc((size_t)n + 1) : NULL;
    if (t) { size_t got = fread(t, 1, (size_t)n, f); t[got] = 0; wp_parse(t); WP.have = true; free(t); }
    fclose(f);
}
bool arc__pack_manifest_text(const char *text) {
    WP.read = true; WP.have = text != NULL;
    if (text) wp_parse(text);
    return true;
}
void arc__webload_reset(void) {
    const arc__fetcher *f = wp_fetcher();
    for (int i = 0; i < WP.npacks; i++) if (WP.packs[i].job && f) f->release(WP.packs[i].job);
    free(WP.files); memset(&WP, 0, sizeof WP);
}
bool arc__pack_cache_key(const char *name, const char *hash, char *out, size_t cap) {
    if (!name || !hash || !*hash || !cap) { if (cap) out[0] = 0; return false; }
    int n = snprintf(out, cap, "%s?arc=%s", name, hash);
    return n > 0 && (size_t)n < cap;
}
int arc__pack_find(const char *name) {
    if (!name) return -1;
    while (name[0] == '.' && name[1] == '/') name += 2;
    for (int i = 0; i < WP.npacks; i++) if (!strcmp(WP.packs[i].name, name)) return i;
    return -1;
}
const char *arc__pack_name(int pack) { return pack >= 0 && pack < WP.npacks ? WP.packs[pack].name : ""; }
long long arc__pack_bytes(int pack) { return pack >= 0 && pack < WP.npacks ? WP.packs[pack].size : 0; }
/* start getting a pack: from disk at once (a file of that name; or nothing to get on desktop), else a download */
static int wp_start(int i) {
    wpack *p = &WP.packs[i];
    if (p->state == ARC_PACK_READY || p->state == ARC_PACK_FETCHING) return p->state;
    const arc__fetcher *f = wp_fetcher();
    FILE *fp = fopen(p->name, "rb");
    if (fp) { fclose(fp); p->state = arc_pack_mount(p->name) ? ARC_PACK_READY : ARC_PACK_FAILED; return p->state; }
    if (!f) { p->state = ARC_PACK_READY; return p->state; }   /* desktop / headless: the files are loose on disk */
    wp_manifest();
    if (!WP.have) { p->state = ARC_PACK_READY; return p->state; }   /* 0.10.9: no manifest (a single-package web build): its files were preloaded, nothing to fetch */
    char key[256]; bool keyed = p->listed && arc__pack_cache_key(p->name, p->hash, key, sizeof key);
    p->job = f->start(p->name, keyed ? key : NULL); p->got = 0;
    p->state = p->job ? ARC_PACK_FETCHING : ARC_PACK_FAILED;
    return p->state;
}
int arc__pack_for_file(const char *path, long long *size) {
    wp_manifest();
    if (!WP.nfiles || !path) return -1;
    char nb[512]; pack_norm(path, nb, sizeof nb);
    for (int i = 0; i < WP.nfiles; i++)
        if (!strcmp(WP.files[i].path, nb)) {
            int k = WP.files[i].pack;
            if (WP.packs[k].state == ARC_PACK_READY || WP.packs[k].state == ARC_PACK_FAILED) return -1;   /* mounted yet not there, or failed: missing */
            if (wp_start(k) != ARC_PACK_FETCHING) return -1;
            if (size) *size = WP.files[i].size;
            return k;
        }
    return -1;
}
/* 0 downloading, 1 here (downloaded, or already mounted), -1 failed */
int arc__pack_poll(int pack, long long *got) {
    if (pack < 0 || pack >= WP.npacks) return -1;
    wpack *p = &WP.packs[pack];
    if (got) *got = p->got;
    if (p->state == ARC_PACK_READY) return 1;
    if (p->state != ARC_PACK_FETCHING || !p->job) return -1;
    const arc__fetcher *f = wp_fetcher(); long long g = 0, t = 0;
    int r = f ? f->poll(p->job, &g, &t) : -1;
    p->got = p->size > 0 && g > p->size ? p->size : g;
    if (got) *got = p->got;
    return r > 0 ? 1 : r < 0 ? -1 : 0;
}
bool arc__pack_finish(int pack) {
    if (pack < 0 || pack >= WP.npacks) return false;
    wpack *p = &WP.packs[pack];
    if (p->state == ARC_PACK_READY) return true;
    const arc__fetcher *f = wp_fetcher();
    if (p->state != ARC_PACK_FETCHING || !f || !p->job) return false;
    size_t n = 0; int r = f->poll(p->job, &(long long){ 0 }, &(long long){ 0 });
    uint8_t *d = r > 0 ? f->take(p->job, &n) : NULL;
    p->from_cache = r > 0 && f->from_cache && f->from_cache(p->job);
    f->release(p->job); p->job = 0;
    if (d && arc__pack_mount_owned(d, n, p->name)) {
        p->state = ARC_PACK_READY; p->got = (long long)n;
        arc_log("pack %s: %.2f MB%s", p->name, (double)n / 1e6, p->from_cache ? " from the cache" : " downloaded");
        return true;
    }
    if (!d && !WP.have) {   /* not found and no manifest: a single-package build, where these files are preloaded */
        p->state = ARC_PACK_READY; arc_log("pack %s not found: using the preloaded files", p->name);
        return true;
    }
    p->state = ARC_PACK_FAILED; arc_log("cannot fetch pack %s", p->name);
    return false;
}

/* ---- public ---- */
arc_load_group arc_pack_fetch(const char *name) {
    arc_load_group none = { 0 };
    if (!name || !*name) return none;
    while (name[0] == '.' && name[1] == '/') name += 2;
    wp_manifest();
    int i = wp_add(name);
    if (i < 0) { arc_log("arc_pack_fetch: too many packs (%s)", name); return none; }
    bool own = !arc__load_queueing();
    if (own) arc_load_begin(ARC_LOAD_BACKGROUND);
    if (WP.packs[i].state == ARC_PACK_FAILED) WP.packs[i].state = ARC_PACK_NONE;   /* asked again: try again */
    uint32_t gid = 0;
    if (wp_start(i) == ARC_PACK_FETCHING) gid = arc__load_queue_pack(i);
    if (own) return arc_load_end();
    arc_load_group g = { gid ? gid : arc__load_group_open() }; return g;
}
/* 0.10.9: a file that is here, or that a pack still to come (listed in arc_packs.txt, not failed) holds */
bool arc_file_listed(const char *path) {
    if (!path || !*path) return false;
    if (arc_file_exists(path)) return true;
    wp_manifest();
    char nb[512]; pack_norm(path, nb, sizeof nb);
    for (int i = 0; i < WP.nfiles; i++) if (!strcmp(WP.files[i].path, nb)) return WP.packs[WP.files[i].pack].state != ARC_PACK_FAILED;
    return false;
}
int arc_pack_state(const char *name) { int i = arc__pack_find(name); return i < 0 ? ARC_PACK_NONE : WP.packs[i].state; }
bool arc_pack_ready(const char *name) { return arc_pack_state(name) == ARC_PACK_READY; }
