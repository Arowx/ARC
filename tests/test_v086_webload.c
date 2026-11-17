/* ARC 0.8.6 phase H1: faster web loading (included once from test_core.c; every name here starts with v086w_ because
   the test files share one translation unit). Headless, with stand-ins for the browser: a fake pack downloader
   (arc__fetcher_hook) and a fake image decoder (arc__imgdec_hook) that take a few frames, succeed or fail on demand.
   Checks: which images the browser may decode (headers), the cache key, the build tool's split (tools/web_packs.py,
   run by build.sh test into build/wpstage + build/wpout), packs fetched by the loader for files listed in the manifest
   (queue order, progress, failures, retry), images decoded "by the browser" and read back as bytes (the software
   renderer has no GPU-direct path), ARC's decoder taking over on failure and in arc_load_wait, and desktop behaviour
   (no fetcher: packs read from disk at once). The real browser paths are in tests/web_load_test.py. */

/* ---- stand-in pack downloader: per URL, how many polls it takes, whether it fails, the bytes it brings ---- */
typedef struct { char url[128], key[256]; bool keyed, fail; int polls, left; uint8_t *data; size_t n; int started; } v086w_dl;
static v086w_dl v086w_dls[8]; static int v086w_ndl;
static v086w_dl *v086w_dl_for(const char *url) { for (int i = 0; i < v086w_ndl; i++) if (!strcmp(v086w_dls[i].url, url)) return &v086w_dls[i]; return NULL; }
static void v086w_dl_set(const char *url, uint8_t *data, size_t n, int polls, bool fail) {
    v086w_dl *d = v086w_dl_for(url); if (!d) { d = &v086w_dls[v086w_ndl++]; memset(d, 0, sizeof *d); snprintf(d->url, sizeof d->url, "%s", url); }
    free(d->data); d->data = data; d->n = n; d->polls = polls; d->fail = fail;
}
static int v086w_f_start(const char *url, const char *key) {
    v086w_dl *d = v086w_dl_for(url); if (!d) return 0;
    d->keyed = key != NULL; snprintf(d->key, sizeof d->key, "%s", key ? key : ""); d->left = d->polls; d->started++;
    return (int)(d - v086w_dls) + 1;
}
static int v086w_f_poll(int job, long long *got, long long *total) {
    v086w_dl *d = &v086w_dls[job - 1];
    *total = (long long)d->n;
    if (d->left > 0) { d->left--; *got = (long long)d->n * (d->polls - d->left) / (d->polls + 1); return 0; }
    *got = (long long)d->n; return d->fail ? -1 : 1;
}
static uint8_t *v086w_f_take(int job, size_t *size) { v086w_dl *d = &v086w_dls[job - 1]; uint8_t *p = (uint8_t *)malloc(d->n); memcpy(p, d->data, d->n); *size = d->n; return p; }
static void v086w_f_release(int job) { (void)job; }
static int  v086w_f_cached(int job) { (void)job; return 0; }
static const arc__fetcher v086w_fetcher = { v086w_f_start, v086w_f_poll, v086w_f_take, v086w_f_release, v086w_f_cached };

/* ---- stand-in image decoder: ARC's own decoder after `polls` polls (or a failure) ---- */
static int v086w_img_polls = 2; static bool v086w_img_fail; static int v086w_img_started, v086w_img_types[3];
typedef struct { uint8_t *rgba; int w, h, left; bool used; } v086w_ij;
static v086w_ij v086w_ijs[64];
static int v086w_i_start(const uint8_t *d, size_t n, int type, bool alpha) {
    (void)alpha;
    for (int i = 0; i < 64; i++) if (!v086w_ijs[i].used) {
        v086w_ijs[i].used = true; v086w_ijs[i].left = v086w_img_polls; v086w_ijs[i].rgba = arc_image_decode(d, n, &v086w_ijs[i].w, &v086w_ijs[i].h);
        v086w_img_started++; if (type >= 0 && type < 3) v086w_img_types[type]++;
        return i + 1;
    }
    return 0;
}
static int v086w_i_poll(int job, int *w, int *h) {
    v086w_ij *j = &v086w_ijs[job - 1];
    if (j->left > 0) { j->left--; return 0; }
    if (v086w_img_fail || !j->rgba) return -1;
    *w = j->w; *h = j->h; return 1;
}
static bool v086w_i_pixels(int job, uint8_t *rgba, int w, int h) {
    v086w_ij *j = &v086w_ijs[job - 1];
    if (!j->rgba || w != j->w || h != j->h) return false;
    memcpy(rgba, j->rgba, (size_t)w * h * 4); return true;
}
static void v086w_i_release(int job) { v086w_ij *j = &v086w_ijs[job - 1]; free(j->rgba); memset(j, 0, sizeof *j); }
static const arc__imgdec v086w_imgdec = { v086w_i_start, v086w_i_poll, v086w_i_pixels, v086w_i_release, "fake" };
static int v086w_live_jobs(void) { int n = 0; for (int i = 0; i < 64; i++) n += v086w_ijs[i].used; return n; }

/* ---- helpers: a pack in memory (stored entries), an .arcm v3 triangle with an embedded image ---- */
static uint8_t *v086w_pack(const char **names, uint8_t **datas, const size_t *sizes, int count, size_t *out_n) {
    size_t dirb = 0, total = 0;
    for (int i = 0; i < count; i++) { dirb += 14 + strlen(names[i]); total += sizes[i]; }
    size_t n = 16 + dirb + total; uint8_t *p = (uint8_t *)calloc(1, n), *e = p + 16; uint32_t off = (uint32_t)(16 + dirb);
    memcpy(p, "ARCP", 4); uint32_t v = 1, c = (uint32_t)count, db = (uint32_t)dirb; memcpy(p + 4, &v, 4); memcpy(p + 8, &c, 4); memcpy(p + 12, &db, 4);
    for (int i = 0; i < count; i++) {
        uint32_t sz = (uint32_t)sizes[i]; memcpy(e, &off, 4); memcpy(e + 4, &sz, 4); memcpy(e + 8, &sz, 4); e[12] = 0; e[13] = (uint8_t)strlen(names[i]);
        memcpy(e + 14, names[i], strlen(names[i])); e += 14 + strlen(names[i]);
        memcpy(p + off, datas[i], sizes[i]); off += sz;
    }
    *out_n = n; return p;
}
static uint8_t *v086w_arcm(const uint8_t *tex, size_t tn, size_t *out_n) {
    size_t n = 56 + 3 * 16 + tn; uint8_t *p = (uint8_t *)calloc(1, n);
    uint32_t h[7] = { 3, 3, 0, 0, (uint32_t)tn, 0, 0 }; memcpy(p, "ARCM", 4); memcpy(p + 4, h, sizeof h);
    float b[6] = { -1, -1, 0, 1, 1, 0 }; memcpy(p + 32, b, sizeof b);
    static const uint16_t pos[3][2] = { { 0, 0 }, { 65535, 0 }, { 0, 65535 } };
    for (int i = 0; i < 3; i++) { uint8_t *q = p + 56 + i * 16; memcpy(q, pos[i], 4); q[7] = 127; memset(q + 8, 255, 4); }
    memcpy(p + 56 + 48, tex, tn); *out_n = n; return p;
}
static int v086w_frames(int max, bool (*until)(void)) { int f = 0; while (f < max && !until()) { arc__load_frame(); f++; } return f; }
static arc_load_group v086w_g;
static bool v086w_gdone(void) { return arc_load_done(v086w_g); }

static void v086w_headers(void) {
    bool a; int w, h;
    uint8_t png[64]; memset(png, 0, sizeof png);   /* signature, IHDR (16 x 8, depth, type), then IDAT */
    memcpy(png, "\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR\0\0\0\x10\0\0\0\x08\x08\x02\0\0\0", 29);
    memcpy(png + 33, "\0\0\0\0IDAT", 8);
    CHECK(arc__img_browser_type(png, sizeof png, &a, &w, &h) == 2 && !a && w == 16 && h == 8);   /* 8-bit RGB */
    png[25] = 6; CHECK(arc__img_browser_type(png, sizeof png, &a, &w, &h) == 2 && a);           /* RGBA */
    png[24] = 16; CHECK(arc__img_browser_type(png, sizeof png, &a, &w, &h) == 0);               /* 16-bit: ARC */
    png[24] = 8; png[25] = 3; memcpy(png + 37, "tRNS", 4);                                      /* palette + tRNS before IDAT */
    memcpy(png + 45, "\0\0\0\0IDAT", 8);
    CHECK(arc__img_browser_type(png, sizeof png, &a, &w, &h) == 2 && a);
    memcpy(png + 37, "gAMA", 4); CHECK(arc__img_browser_type(png, sizeof png, &a, &w, &h) == 0);   /* gamma: browsers may apply it */
    memcpy(png + 37, "iCCP", 4); CHECK(arc__img_browser_type(png, sizeof png, &a, &w, &h) == 0);
    memcpy(png + 37, "sRGB", 4); CHECK(arc__img_browser_type(png, sizeof png, &a, &w, &h) == 2);   /* plain sRGB: fine */
    uint8_t jpg[64]; memset(jpg, 0, sizeof jpg);   /* SOI, SOF0 (h 6, w 10, 3 components) */
    memcpy(jpg, "\xFF\xD8\xFF\xC0\x00\x11\x08\x00\x06\x00\x0A\x03", 12);
    CHECK(arc__img_browser_type(jpg, sizeof jpg, &a, &w, &h) == 1 && !a && w == 10 && h == 6);
    jpg[3] = 0xC2; CHECK(arc__img_browser_type(jpg, sizeof jpg, &a, &w, &h) == 1);              /* progressive: the browser (ARC can't) */
    jpg[3] = 0xC9; CHECK(arc__img_browser_type(jpg, sizeof jpg, &a, &w, &h) == 0);              /* arithmetic */
    jpg[3] = 0xC0; jpg[11] = 4; CHECK(arc__img_browser_type(jpg, sizeof jpg, &a, &w, &h) == 0); /* CMYK: browsers convert it */
    /* an APP1 Exif block before the frame: Orientation 6 -> ARC (browsers would turn it), 1 -> the browser */
    static const uint8_t exif[] = { 0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0,
                                    0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0x06, 0x00, 0x0A, 0x03, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t ex[sizeof exif]; memcpy(ex, exif, sizeof ex);
    CHECK(arc__img_browser_type(ex, sizeof ex, &a, &w, &h) == 0);
    ex[30] = 1; CHECK(arc__img_browser_type(ex, sizeof ex, &a, &w, &h) == 1 && w == 10);
    CHECK(arc__img_browser_type((const uint8_t *)"ARCT", 4, &a, &w, &h) == 0 && arc__img_browser_type(NULL, 0, &a, &w, &h) == 0);
    /* the cache key: the pack's name and its content hash; no hash, no caching */
    char k[64];
    CHECK(arc__pack_cache_key("levels.arcpak", "9f86d081884c7d65", k, sizeof k) && !strcmp(k, "levels.arcpak?arc=9f86d081884c7d65"));
    CHECK(!arc__pack_cache_key("levels.arcpak", "", k, sizeof k) && !k[0] && !arc__pack_cache_key("levels.arcpak", NULL, k, sizeof k));
    CHECK(!arc__pack_cache_key("levels.arcpak", "9f86d081884c7d65", k, 8));   /* too long for the buffer */
}

/* run from the checks (tick 9): the earlier tests' queued loads are done by then, and theirs count every group */
static void v086w_check(void) {
    v086w_headers();
    int keep_items = arc__load_items_per_frame, s0[4];
    /* desktop: no downloader, so a pack that is not on disk is "ready" at once (its files are loose), one that is gets mounted */
    arc__webload_reset();
    CHECK(arc_pack_state("v086w_none.arcpak") == ARC_PACK_NONE && !arc_pack_ready("v086w_none.arcpak"));
    arc_load_group gd = arc_pack_fetch("v086w_none.arcpak");
    CHECK(arc_pack_ready("v086w_none.arcpak") && arc_load_done(gd) && arc_load_status(gd).total == 0);
    gd = arc_pack_fetch("./test.arcpak"); CHECK(arc_pack_ready("test.arcpak") && arc_load_done(gd));   /* (made by build.sh test) */
    arc_load_begin(ARC_LOAD_BACKGROUND); gd = arc_pack_fetch("v086w_none.arcpak");                       /* in an open group: that group */
    arc_load_group ge = arc_load_end(); CHECK(gd.id && gd.id == ge.id);
    CHECK(!arc_pack_fetch(NULL).id && !arc_pack_fetch("").id);

    /* the build tool's output (build.sh test: tools/web_packs.py wpsrc/assets wpout wpstage --packs lvl) */
    size_t mn = 0; char *man = (char *)arc_file_read("wpstage/arc_packs.txt", arc_frame(), &mn);
    CHECK(man && strstr(man, "file lvl.arcpak ") && strstr(man, " assets/lvl/pyr.arcm") && strstr(man, " assets/lvl/hello.txt"));
    CHECK(arc_file_exists("wpstage/assets/first/a.txt") && !arc_file_exists("wpstage/assets/lvl/pyr.arcm") && arc_file_exists("wpout/lvl.arcpak"));
    long long lvl_size = arc__file_size("wpout/lvl.arcpak", &(bool){ false });
    char want[64]; snprintf(want, sizeof want, "pack lvl.arcpak %lld ", lvl_size);
    CHECK(man && strstr(man, want) != NULL);
    char hash[32] = { 0 }; if (man && strstr(man, want)) sscanf(strstr(man, want) + strlen(want), "%16s", hash);
    CHECK(strlen(hash) == 16);

    /* with the stand-ins: the loader fetches lvl.arcpak for a file listed in it */
    arc__fetcher_hook = &v086w_fetcher; arc__imgdec_hook = &v086w_imgdec;
    arc__webload_reset(); arc__pack_manifest_text(man);
    size_t pn = 0; uint8_t *pd = arc__file_read_alloc("wpout/lvl.arcpak", &pn, &(bool){ false });
    v086w_dl_set("lvl.arcpak", pd, pn, 3, false);
    arc__load_items_per_frame = 4;
    arc_load_begin(ARC_LOAD_BACKGROUND);
    arc_mesh lm = arc_mesh_load("assets/lvl/pyr.arcm");
    v086w_g = arc_load_end();
    v086w_dl *dl = v086w_dl_for("lvl.arcpak"); char key[64]; snprintf(key, sizeof key, "lvl.arcpak?arc=%s", hash);
    CHECK(lm.id && !arc_mesh_ready(lm) && arc_pack_state("lvl.arcpak") == ARC_PACK_FETCHING && dl->started == 1 && dl->keyed && !strcmp(dl->key, key));
    arc_load_info s = arc_load_status(v086w_g);
    CHECK(s.total == 2 && s.bytes_total == lvl_size + arc__file_size("pyramid.arcm", &(bool){ false }));   /* the pack + the file (sizes from the manifest) */
    arc__load_frame(); float p1 = arc_load_progress(v086w_g); arc__load_frame(); float p2 = arc_load_progress(v086w_g);
    CHECK(p1 > 0 && p2 > p1 && p2 < 1 && !arc_mesh_ready(lm));                                     /* the download's progress */
    int f = v086w_frames(20, v086w_gdone);
    CHECK(f < 20 && arc_mesh_ready(lm) && arc_mesh_triangles(lm) == 6 && arc_pack_ready("lvl.arcpak") && arc_load_status(v086w_g).failed == 0);
    size_t hn = 0; char *ht = (char *)arc_file_read("assets/lvl/hello.txt", arc_frame(), &hn);
    CHECK(ht && hn == 11 * 40 && !strncmp(ht, "level text level text", 21));                       /* and the pack is mounted */
    CHECK(dl->started == 1);
    arc_load_group again = arc_pack_fetch("lvl.arcpak"); CHECK(arc_load_done(again) && dl->started == 1);   /* here: nothing fetched */

    /* images in a pack: the texture and the mesh's map go to the "browser", wait their turn behind the pack */
    uint8_t px[8 * 4 * 4]; for (int i = 0; i < 32; i++) { px[i * 4] = (uint8_t)(i * 8); px[i * 4 + 1] = 77; px[i * 4 + 2] = (uint8_t)(255 - i); px[i * 4 + 3] = (uint8_t)(i * 5); }
    CHECK(arc_image_save_png("v086w_t.png", px, 8, 4));
    size_t tn = 0; uint8_t *tp = arc__file_read_alloc("v086w_t.png", &tn, &(bool){ false });
    size_t an = 0; uint8_t *am = tp ? v086w_arcm(tp, tn, &an) : NULL;
    const char *names[2] = { "v086w/tile.png", "v086w/mesh.arcm" }; uint8_t *datas[2] = { tp, am }; size_t sizes[2] = { tn, an };
    size_t kn = 0; uint8_t *kp = (tp && am) ? v086w_pack(names, datas, sizes, 2, &kn) : NULL;
    char mtext[512]; snprintf(mtext, sizeof mtext, "# test\npack v086w.arcpak %zu 00ff00ff00ff00ff\nfile v086w.arcpak %zu v086w/tile.png\r\nfile v086w.arcpak %zu v086w/mesh.arcm\n"
                              "pack v086w2.arcpak 100 1234\nfile v086w2.arcpak 50 v086w2/x.png\n", kn, tn, an);
    arc__webload_reset(); arc__pack_manifest_text(mtext);
    v086w_dl_set("v086w.arcpak", kp, kn, 4, false);
    memcpy(s0, arc__webimg_stats, sizeof s0); v086w_img_started = 0; v086w_img_polls = 2; v086w_img_fail = false;
    arc_load_begin(ARC_LOAD_BACKGROUND);
    arc_texture t1 = arc_texture_load("v086w/tile.png"); arc_mesh m1 = arc_mesh_load("v086w/mesh.arcm");
    arc_texture t2 = arc_texture_load("v086w_t.png");                     /* on disk, after the pack: waits behind it */
    v086w_g = arc_load_end();
    CHECK(t1.id && m1.id && t2.id && arc_load_status(v086w_g).total == 4 && arc_pack_state("v086w.arcpak") == ARC_PACK_FETCHING);
    for (int i = 0; i < 3; i++) arc__load_frame();
    CHECK(!arc_texture_ready(t2) && !arc_texture_ready(t1) && v086w_img_started == 0);      /* nothing past the pack has started */
    f = v086w_frames(30, v086w_gdone);
    int w = 0, h = 0; arc_texture_size(t1, &w, &h);
    CHECK(f < 30 && arc_texture_ready(t1) && arc_texture_ready(t2) && arc_mesh_ready(m1) && w == 8 && h == 4);
    CHECK(v086w_img_started == 3 && v086w_img_types[2] >= 3 && arc__webimg_stats[0] - s0[0] == 3 && arc__webimg_stats[3] - s0[3] == 3 && arc__webimg_stats[2] == s0[2]);
    CHECK(!memcmp(arc__textures[t1.id].rgba, px, sizeof px) && !memcmp(arc__textures[t2.id].rgba, px, sizeof px));
    arc_texture mt = arc_mesh_texture(m1);
    CHECK(mt.id && !memcmp(arc__textures[mt.id].rgba, px, sizeof px) && v086w_live_jobs() == 0);   /* every job released */

    /* the browser fails: ARC's decoder takes over, with the same pixels */
    memcpy(s0, arc__webimg_stats, sizeof s0); v086w_img_fail = true;
    arc_load_begin(ARC_LOAD_BACKGROUND); arc_texture t3 = arc_texture_load("v086w_t.png"); v086w_g = arc_load_end();
    f = v086w_frames(20, v086w_gdone);
    CHECK(f < 20 && arc_texture_ready(t3) && arc__webimg_stats[2] - s0[2] == 1 && !memcmp(arc__textures[t3.id].rgba, px, sizeof px) && v086w_live_jobs() == 0);
    v086w_img_fail = false;
    /* arc_load_wait cannot wait for the browser: it decodes with ARC there and then */
    v086w_img_polls = 1000; memcpy(s0, arc__webimg_stats, sizeof s0);
    arc_load_begin(ARC_LOAD_BACKGROUND); arc_texture t4 = arc_texture_load("v086w_t.png"); v086w_g = arc_load_end();
    arc__load_frame(); CHECK(!arc_texture_ready(t4) && v086w_live_jobs() == 1);
    arc_load_wait(v086w_g);
    CHECK(arc_texture_ready(t4) && arc__webimg_stats[2] - s0[2] == 1 && !memcmp(arc__textures[t4.id].rgba, px, sizeof px) && v086w_live_jobs() == 0);
    v086w_img_polls = 2;
    /* a destroyed texture's browser job is let go */
    arc_load_begin(ARC_LOAD_BACKGROUND); arc_texture t5 = arc_texture_load("v086w_t.png"); v086w_g = arc_load_end();
    arc__load_frame(); arc_texture_destroy(t5); v086w_frames(20, v086w_gdone);
    CHECK(arc_load_done(v086w_g) && v086w_live_jobs() == 0);

    /* a listed pack that fails: its files fail too; asked again it is tried again */
    v086w_dl_set("v086w2.arcpak", (uint8_t *)calloc(1, 4), 4, 1, true);
    arc_load_begin(ARC_LOAD_BACKGROUND); arc_texture tx = arc_texture_load("v086w2/x.png"); v086w_g = arc_load_end();
    f = v086w_frames(20, v086w_gdone);
    s = arc_load_status(v086w_g);
    CHECK(tx.id && f < 20 && !arc_texture_ready(tx) && s.total == 2 && s.failed == 2 && arc_pack_state("v086w2.arcpak") == ARC_PACK_FAILED);
    arc_load_begin(ARC_LOAD_BACKGROUND); arc_texture ty = arc_texture_load("v086w2/x.png"); arc_load_end();
    CHECK(!ty.id && arc_pack_state("v086w2.arcpak") == ARC_PACK_FAILED);                     /* failed: a missing file, at once */
    v086w_g = arc_pack_fetch("v086w2.arcpak");
    CHECK(arc_pack_state("v086w2.arcpak") == ARC_PACK_FETCHING && v086w_dl_for("v086w2.arcpak")->started == 2);
    v086w_frames(20, v086w_gdone); CHECK(arc_pack_state("v086w2.arcpak") == ARC_PACK_FAILED && arc_load_status(v086w_g).failed == 1);
    /* a pack the manifest does not list: fetched without a cache key; not found -> failed (a manifest exists) */
    v086w_dl_set("extra.arcpak", (uint8_t *)calloc(1, 4), 4, 1, true);
    v086w_g = arc_pack_fetch("extra.arcpak");
    CHECK(!v086w_dl_for("extra.arcpak")->keyed && arc_pack_state("extra.arcpak") == ARC_PACK_FETCHING);
    v086w_frames(20, v086w_gdone); CHECK(arc_pack_state("extra.arcpak") == ARC_PACK_FAILED);
    /* ...without a manifest (a single-package build): not found means its files were preloaded -> ready */
    arc__webload_reset(); arc__pack_manifest_text(NULL);
    v086w_g = arc_pack_fetch("extra.arcpak"); v086w_frames(20, v086w_gdone);
    CHECK(arc_pack_ready("extra.arcpak") && arc_load_status(v086w_g).failed == 0);

    arc__fetcher_hook = NULL; arc__imgdec_hook = NULL; arc__webload_reset();
    /* no stand-ins (desktop, headless): queued images decode with ARC as before, nothing goes to a "browser" */
    memcpy(s0, arc__webimg_stats, sizeof s0); arc__load_items_per_frame = 0;
    arc_load_begin(ARC_LOAD_BACKGROUND); arc_texture t6 = arc_texture_load("v086w_t.png"); arc_load_group g6 = arc_load_end();
    CHECK(arc_load_done(g6) && arc_texture_ready(t6));
    arc__load_items_per_frame = keep_items;
    CHECK(!memcmp(s0, arc__webimg_stats, sizeof s0) && !arc__imgdec_get());
    for (int i = 0; i < v086w_ndl; i++) { free(v086w_dls[i].data); v086w_dls[i].data = NULL; }
    free(tp); free(am);
}
