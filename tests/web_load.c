/* ARC 0.8.6 web loading test page (tests/web_load_test.py builds and runs it in headless Chromium).
 *
 *   ?--webload-check [--browser-decode] ...   the browser checks: the browser's decoders against ARC's (PNG exact, JPEG
 *        close), textures and mesh maps the loader hands to the browser and uploads GPU-direct (drawn and compared with
 *        ARC-decoded ones), .arct blocks uploaded as blocks, ARC's decoder taking over when the browser fails, and
 *        (--expect-off) nothing changing when browser decoding is off. Prints "ARC test web load OK n/n" (or FAILED).
 *   ?--webload-compare=LIST --browser-decode   every image named in LIST decoded by the browser and by ARC: per-image
 *        max/mean differences, "ARC test webcompare OK jpeg=N max=M mean=X png=N png_differing=0 ..." (PNGs must match).
 *   ?--webload-bench [--browser-decode] [--hold]   the measurement: draws a title at once (assets/first/title.png), queues
 *        every file in assets/first/load_list.txt (meshes, textures) in one background group (--hold: a group that holds
 *        ticks), draws a loading bar until they are in, then all of them once, and prints
 *        "ARC test webload first_frame=MS loaded=MS longest=MS ..." (times from navigation start, performance.now()).
 * Files in a pack that is not here yet (a WEB_PACKS build) are fetched by the loader on its own. */
#include "../src/arc_internal.h"
#include <stdio.h>
#include <emscripten.h>
extern double arc__load_busy_ms;   /* src/arc_load.c */

static int checks, fails;
#define WCHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("ARC log web load FAILED line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
EM_JS(double, now_ms, (void), { return performance.now(); });
EM_JS(void, fail_all_decodes, (void), { if (Module.arcImg) Module.arcImg.failAll = 1; });

/* ---- bench ---- */
static bool bench, check;
static arc_texture title; static arc_load_group grp;
static arc_mesh meshes[128]; static arc_texture texs[128]; static int nmesh, ntex, nfail_queue;
static double t_first, t_loaded, t_prev, longest; static int frames, load_frames, phase;

static void queue_list(const char *path, int flags) {
    size_t n = 0; char *t = (char *)arc_file_read(path, arc_frame(), &n);
    if (!t) { printf("ARC log no %s\n", path); return; }
    arc_load_begin(flags);
    for (char *l = strtok(t, "\r\n"); l; l = strtok(NULL, "\r\n")) {
        if (!*l || *l == '#') continue;
        size_t k = strlen(l);
        if (k > 5 && !strcmp(l + k - 5, ".arcm")) { if (nmesh < 128) { meshes[nmesh] = arc_mesh_load(l); if (!meshes[nmesh].id) nfail_queue++; nmesh++; } }
        else if (ntex < 128) { texs[ntex] = arc_texture_load_ex(l, ARC_TEX_MIPMAPS | ARC_TEX_SMOOTH); if (!texs[ntex].id) nfail_queue++; ntex++; }
    }
    grp = arc_load_end();
}
static void bench_draw(void) {
    double t = now_ms();
    if (!frames) t_first = t; else if (!t_loaded && t - t_prev > longest) longest = t - t_prev;
    t_prev = t; frames++;
    float w = (float)arc_width(), h = (float)arc_height();
    if (title.id) { int tw, th; arc_texture_size(title, &tw, &th); if (tw > 0) arc_draw_sprite(title, w * 0.5f - 160, 40, 320, 320.0f * (float)th / (float)tw, 0, 0, 1, 1, 0xFFFFFF); }
    if (phase == 0) {
        load_frames++;
        arc_draw_loading_screen(arc_load_progress(grp), "Loading");
        if (arc_load_done(grp)) { t_loaded = t; phase = 1; }
        return;
    }
    arc_camera c = { { 0, 9, 16 }, { 0, 0, 0 }, { 0, 1, 0 }, 0.9f, 0, 0.1f, 200.0f };
    arc_set_camera(&c);
    for (int i = 0; i < nmesh; i++) arc_draw_mesh(meshes[i], arc_m4_translate(arc_v3_make((float)(i % 7) * 3.0f - 9.0f, 0, (float)(i / 7) * 3.0f - 6.0f)), 0xFFFFFF);
    for (int i = 0; i < ntex; i++) arc_draw_sprite(texs[i], 10.0f + (float)i * 40.0f, h - 50, 36, 36, 0, 0, 1, 1, 0xFFFFFF);
    if (phase++ == 2) {
        arc_load_info s = arc_load_status(grp); int ready = 0;
        for (int i = 0; i < nmesh; i++) ready += arc_mesh_ready(meshes[i]);
        for (int i = 0; i < ntex; i++) ready += arc_texture_ready(texs[i]);
        printf("ARC test webload first_frame=%.0f loaded=%.0f longest=%.1f frames=%d loading_frames=%d files=%d ready=%d failed=%d bytes=%lld "
               "browser=%d direct=%d fallback=%d readback=%d decoder=%s load_ms=%.0f\n", t_first, t_loaded, longest, frames, load_frames, s.total, ready, s.failed + nfail_queue,
               s.bytes_total, arc__webimg_stats[0], arc__webimg_stats[1], arc__webimg_stats[2], arc__webimg_stats[3], arc__imgdec_get() ? arc__imgdec_get()->name : "ARC", arc__load_busy_ms);
        fflush(stdout);
    }
}

/* ---- checks ---- */
#define CK_DIR "assets/first/check/"
static const char *ck_names[] = { "photo.jpg", "grey.jpg", "prog.jpg", "opaque.png", "alpha.png", "pal.png", "srgb.png", "exif.jpg", "deep16.png" };
static const int ck_expect[] = { 1, 1, 1, 2, 2, 2, 2, 0, 0 };   /* arc__img_browser_type: 0 = left to ARC */
#define CK_N 9
static struct { uint8_t *file; size_t n; int job, type, w, h; bool done, alpha; } ck[CK_N];
static int step, wait_frames, draws;   /* wait_frames: drawn frames to wait (a screenshot is saved after its frame) */
static bool expect_off;
static arc_texture ref_alpha, ref_photo, b_alpha, b_photo, b_arct, f_alpha, f_photo; static arc_mesh b_mesh;
static arc_load_group g1, g2; static int stats0[4], stats1[4];
static const arc__imgdec *dec; static bool img_method;   /* <img>: see-through PNGs stay with ARC */

static void ck_compare(int i) {
    int aw = 0, ah = 0; uint8_t *a = arc_image_decode(ck[i].file, ck[i].n, &aw, &ah);
    uint8_t *b = (uint8_t *)malloc((size_t)ck[i].w * ck[i].h * 4);
    if (!a && ck[i].type == 1 && ck_names[i][0] == 'p' && ck_names[i][1] == 'r') {   /* progressive: ARC can't, the browser can */
        bool px = b && dec->pixels(ck[i].job, b, ck[i].w, ck[i].h);
        WCHECK(px && ck[i].w == 128, "%s: the browser should decode this progressive JPEG", ck_names[i]);
        printf("ARC log webload compare %s: progressive, decoded by the browser only (ARC reads baseline JPEGs)\n", ck_names[i]);
        free(b); return;
    }
    bool got = a && b && aw == ck[i].w && ah == ck[i].h && dec->pixels(ck[i].job, b, ck[i].w, ck[i].h);
    WCHECK(got, "%s: no pixels (ARC %dx%d, browser %dx%d)", ck_names[i], aw, ah, ck[i].w, ck[i].h);
    if (got) {
        int mx = 0; double sum = 0; size_t n = (size_t)aw * ah * 4;
        for (size_t k = 0; k < n; k++) { int d = abs((int)a[k] - (int)b[k]); if (d > mx) mx = d; sum += d; }
        printf("ARC log webload compare %s %s %dx%d max=%d mean=%.4f\n", ck_names[i], ck[i].type == 1 ? "jpeg" : "png", aw, ah, mx, sum / (double)n);
        if (ck[i].type == 2) WCHECK(mx == 0, "%s: PNG differs from ARC's (max %d)", ck_names[i], mx);
        else WCHECK(mx <= 12 && sum / (double)n < 1.5, "%s: JPEG too far from ARC's (max %d mean %.3f)", ck_names[i], mx, sum / (double)n);
    }
    free(a); free(b);
}
/* the screenshot of the last frame: ARC-decoded on the left, the other on the right (row y0, size w x h) */
static uint8_t *shot_px; static int shot_w, shot_h;
static int region_diff(int x0, int y0, int x1, int y1, int w, int h, double *mean) {
    int mx = 0; double sum = 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) for (int c = 0; c < 3; c++) {
        int d = abs((int)shot_px[((size_t)(y0 + y) * shot_w + x0 + x) * 4 + c] - (int)shot_px[((size_t)(y1 + y) * shot_w + x1 + x) * 4 + c]);
        if (d > mx) mx = d;
        sum += d;
    }
    if (mean) *mean = sum / (double)(w * h * 3);
    return mx;
}
static void draw_pairs(arc_texture l0, arc_texture r0, arc_texture l1, arc_texture r1) {
    arc_set_blend(ARC_BLEND_NONE);                       /* RGB exactly as stored (alpha would hide premultiplying) */
    arc_draw_sprite(l0, 0, 0, 64, 64, 0, 0, 1, 1, 0xFFFFFF); arc_draw_sprite(r0, 100, 0, 64, 64, 0, 0, 1, 1, 0xFFFFFF);
    arc_draw_sprite(l1, 0, 100, 128, 128, 0, 0, 1, 1, 0xFFFFFF); arc_draw_sprite(r1, 200, 100, 128, 128, 0, 0, 1, 1, 0xFFFFFF);
    arc_set_blend(ARC_BLEND_ALPHA);                      /* and blended: premultiplied texels would come out darker */
    arc_draw_sprite(l0, 0, 300, 64, 64, 0, 0, 1, 1, 0xFFFFFF); arc_draw_sprite(r0, 100, 300, 64, 64, 0, 0, 1, 1, 0xFFFFFF);
}
static bool load_shot(const char *path) {
    free(shot_px); shot_px = arc_image_load(path, &shot_w, &shot_h);
    return shot_px != NULL;
}
static void check_tick(void) {
    if (wait_frames > 0 && draws < wait_frames) return;
    wait_frames = 0;
    switch (step) {
    case 0: {
        dec = arc__imgdec_get();
        img_method = dec && !strcmp(dec->name, "HTMLImageElement");
        if (expect_off) WCHECK(!dec, "browser decoding should be off");
        else WCHECK(dec != NULL, "no browser decoder");
        for (int i = 0; i < CK_N; i++) {
            size_t n = 0; uint8_t *d = arc_file_read(arc_fmt(CK_DIR "%s", ck_names[i]), arc_frame(), &n);
            WCHECK(d != NULL, "missing %s", ck_names[i]);
            if (!d) continue;
            ck[i].file = (uint8_t *)malloc(n); memcpy(ck[i].file, d, n); ck[i].n = n;
            int w, h; ck[i].type = arc__img_browser_type(ck[i].file, n, &ck[i].alpha, &w, &h);
            WCHECK(ck[i].type == ck_expect[i], "%s: browser type %d, expected %d", ck_names[i], ck[i].type, ck_expect[i]);
            if (dec && ck[i].type) {
                ck[i].job = dec->start(ck[i].file, n, ck[i].type, ck[i].alpha);
                if (img_method && ck[i].alpha) WCHECK(ck[i].job == 0, "%s: see-through PNGs are not for <img> (premultiplied)", ck_names[i]);
                else WCHECK(ck[i].job > 0, "%s: not taken", ck_names[i]);
            }
        }
        ref_alpha = arc_texture_load_ex(CK_DIR "alpha.png", 0); ref_photo = arc_texture_load_ex(CK_DIR "photo.jpg", 0);   /* ARC decodes these */
        memcpy(stats0, arc__webimg_stats, sizeof stats0);
        arc_load_begin(ARC_LOAD_BACKGROUND);
        b_alpha = arc_texture_load_ex(CK_DIR "alpha.png", 0); b_photo = arc_texture_load_ex(CK_DIR "photo.jpg", 0);
        b_mesh = arc_mesh_load(CK_DIR "mesh.arcm"); b_arct = arc_texture_load_ex(CK_DIR "tex.arct", ARC_TEX_MIPMAPS);
        g1 = arc_load_end();
        step = 1; break; }
    case 1: {   /* the browser's own decodes against ARC's */
        bool all = true;
        for (int i = 0; i < CK_N; i++) {
            if (!ck[i].job || ck[i].done) continue;
            int r = dec->poll(ck[i].job, &ck[i].w, &ck[i].h);
            if (r == 0) { all = false; continue; }
            ck[i].done = true;
            WCHECK(r > 0, "%s: the browser failed to decode it", ck_names[i]);
            if (r > 0) ck_compare(i);
            dec->release(ck[i].job);
        }
        if (all && arc_load_done(g1)) step = 2;
        break; }
    case 2: {   /* the loader's results */
        memcpy(stats1, arc__webimg_stats, sizeof stats1);
        WCHECK(arc_texture_ready(b_alpha) && arc_texture_ready(b_photo) && arc_mesh_ready(b_mesh) && arc_texture_ready(b_arct), "loads not ready");
        int direct = stats1[1] - stats0[1], browser = stats1[0] - stats0[0];
        printf("ARC log webload loader browser=%d direct=%d fallback=%d readback=%d fmt=%d; GPU takes BC1 %d BC3 %d BC4 %d BC5 %d BC7 %d\n", browser, direct, stats1[2] - stats0[2], stats1[3] - stats0[3],
               arc_texture_format(b_arct), arc_texture_format_supported(ARC_TEXFMT_BC1), arc_texture_format_supported(ARC_TEXFMT_BC3), arc_texture_format_supported(ARC_TEXFMT_BC4),
               arc_texture_format_supported(ARC_TEXFMT_BC5), arc_texture_format_supported(ARC_TEXFMT_BC7));
        if (expect_off) WCHECK(browser == 0 && direct == 0, "browser decoding used while off (%d, %d)", browser, direct);
        else if (arc__webimg_bytes) WCHECK(browser == 4 && stats1[3] - stats0[3] == 4, "bytes mode: browser %d readback %d (4 each expected)", browser, stats1[3] - stats0[3]);
        else if (img_method) WCHECK(browser == 2 && direct == 2 && stats1[2] - stats0[2] == 2, "<img>: browser %d direct %d fallback %d (the JPEGs to the browser, the see-through PNGs to ARC: 2 each)", browser, direct, stats1[2] - stats0[2]);
        else WCHECK(browser == 4 && direct == 4, "browser %d direct %d (the two textures, the mesh's texture and normal map: 4 each expected)", browser, direct);
        if (arc_arg_flag("expect-s3tc")) WCHECK(arc_texture_format_supported(ARC_TEXFMT_BC1), "this browser should take BC1 (WEBGL_compressed_texture_s3tc)");
        if (arc_texture_format_supported(ARC_TEXFMT_BC1)) WCHECK(arc_texture_format(b_arct) == ARC_TEXFMT_BC1, ".arct not uploaded as BC1 blocks (format %d)", arc_texture_format(b_arct));
        else WCHECK(arc_texture_format(b_arct) == ARC_TEXFMT_RGBA8, ".arct format %d without S3TC", arc_texture_format(b_arct));
        arc_screenshot_after_frame("webload_a.png");
        step = 3; wait_frames = draws + 3; break; }
    case 3: {
        WCHECK(load_shot("webload_a.png"), "no screenshot");
        if (shot_px) {
            double m0, m1, m2; int d0 = region_diff(0, 0, 100, 0, 64, 64, &m0), d1 = region_diff(0, 100, 200, 100, 128, 128, &m1), d2 = region_diff(0, 300, 100, 300, 64, 64, &m2);
            printf("ARC log webload drawn alpha.png max=%d mean=%.4f, photo.jpg max=%d mean=%.4f, alpha.png blended max=%d mean=%.4f\n", d0, m0, d1, m1, d2, m2);
            WCHECK(d0 == 0 && d2 == 0, "the loaded PNG draws differently from ARC's (%d, blended %d)", d0, d2);
            WCHECK(d1 <= 12 && m1 < 1.5, "the loaded JPEG draws too far from ARC's (%d, %.3f)", d1, m1);
        }
        if (dec && !expect_off) fail_all_decodes();   /* every browser decode fails from now on */
        memcpy(stats0, arc__webimg_stats, sizeof stats0);
        arc_load_begin(ARC_LOAD_BACKGROUND);
        f_alpha = arc_texture_load_ex(CK_DIR "alpha.png", 0); f_photo = arc_texture_load_ex(CK_DIR "photo.jpg", 0);
        g2 = arc_load_end();
        step = 4; break; }
    case 4:
        if (!arc_load_done(g2)) break;
        WCHECK(arc_texture_ready(f_alpha) && arc_texture_ready(f_photo), "fallback loads not ready");
        if (dec && !expect_off) WCHECK(arc__webimg_stats[2] - stats0[2] == 2, "ARC's decoder did not take over (%d)", arc__webimg_stats[2] - stats0[2]);
        arc_screenshot_after_frame("webload_b.png");
        step = 5; wait_frames = draws + 3; break;
    case 5:
        WCHECK(load_shot("webload_b.png"), "no screenshot");
        if (shot_px) {
            int d0 = region_diff(0, 0, 100, 0, 64, 64, NULL), d1 = region_diff(0, 100, 200, 100, 128, 128, NULL);
            WCHECK(d0 == 0 && d1 == 0, "fallback textures differ from ARC's (%d, %d)", d0, d1);
        }
        printf("ARC test web load %s %d/%d checks passed\n", fails ? "FAILED" : "OK", checks - fails, checks); fflush(stdout);
        step = 6; arc_quit(); break;
    default: break;
    }
}
static void check_draw(void) {
    draws++;
    if (step == 3) draw_pairs(ref_alpha, b_alpha, ref_photo, b_photo);
    else if (step == 5) draw_pairs(ref_alpha, f_alpha, ref_photo, f_photo);
    else arc_draw_rect(0, 0, 10, 10, 0x404040);
}

/* ---- compare: every image in a list file, the browser's decode against ARC's (real textures: max/mean differences) ---- */
static char cmp_names[256][160]; static uint8_t *cmp_file[256]; static size_t cmp_n[256]; static int cmp_job[256], cmp_count, cmp_left;
static int cmp_worst; static double cmp_mean_sum; static int cmp_png_bad, cmp_jpeg, cmp_png, cmp_arc_only;
static void compare_start(const char *list) {
    dec = arc__imgdec_get();
    size_t n = 0; char *t = (char *)arc_file_read(list, arc_frame(), &n);
    if (!t || !dec) { printf("ARC test webcompare FAILED (no list or no browser decoder)\n"); return; }
    for (char *l = strtok(t, "\r\n"); l && cmp_count < 256; l = strtok(NULL, "\r\n")) {
        if (!*l) continue;
        int i = cmp_count++; snprintf(cmp_names[i], sizeof cmp_names[i], "%s", l);
        size_t fn = 0; uint8_t *d = arc_file_read(l, arc_frame(), &fn);
        if (!d) { cmp_job[i] = 0; continue; }
        cmp_file[i] = (uint8_t *)malloc(fn); memcpy(cmp_file[i], d, fn); cmp_n[i] = fn;
        bool al; int w, h, type = arc__img_browser_type(cmp_file[i], fn, &al, &w, &h);
        cmp_job[i] = type ? dec->start(cmp_file[i], fn, type, al) : 0;
        if (!cmp_job[i]) cmp_arc_only++; else cmp_left++;
    }
}
static void compare_tick(void) {
    if (!cmp_left) return;
    for (int i = 0; i < cmp_count; i++) {
        if (!cmp_job[i]) continue;
        int w, h, r = dec->poll(cmp_job[i], &w, &h);
        if (!r) continue;
        int aw = 0, ah = 0; uint8_t *a = arc_image_decode(cmp_file[i], cmp_n[i], &aw, &ah), *b = (uint8_t *)malloc((size_t)(w > 0 ? w : 1) * (h > 0 ? h : 1) * 4);
        bool jpeg = cmp_file[i][0] == 0xFF;
        if (r > 0 && a && aw == w && ah == h && dec->pixels(cmp_job[i], b, w, h)) {
            int mx = 0; double sum = 0; size_t k, nn = (size_t)w * h * 4;
            for (k = 0; k < nn; k++) { int d = abs((int)a[k] - (int)b[k]); if (d > mx) mx = d; sum += d; }
            printf("ARC log webcompare %s %s %dx%d max=%d mean=%.4f\n", cmp_names[i], jpeg ? "jpeg" : "png", w, h, mx, sum / (double)nn);
            if (jpeg) { cmp_jpeg++; if (mx > cmp_worst) cmp_worst = mx; cmp_mean_sum += sum / (double)nn; } else { cmp_png++; if (mx) cmp_png_bad++; }
        } else printf("ARC log webcompare %s: no comparison (browser %d, ARC %dx%d)\n", cmp_names[i], r, aw, ah);
        free(a); free(b); dec->release(cmp_job[i]); cmp_job[i] = 0; cmp_left--;
    }
    if (!cmp_left) {
        printf("ARC test webcompare %s jpeg=%d max=%d mean=%.4f png=%d png_differing=%d left_to_arc=%d\n", cmp_png_bad ? "FAILED" : "OK", cmp_jpeg, cmp_worst,
               cmp_jpeg ? cmp_mean_sum / cmp_jpeg : 0.0, cmp_png, cmp_png_bad, cmp_arc_only);
        fflush(stdout);
    }
}

static void init(void) {
    if (arc_arg_value("webload-compare")) compare_start(arc_arg_value("webload-compare"));
    bench = arc_arg_flag("webload-bench"); check = arc_arg_flag("webload-check"); expect_off = arc_arg_flag("expect-off");
    if (bench) {
        title = arc_texture_load("assets/first/title.png");
        queue_list("assets/first/load_list.txt", arc_arg_flag("hold") ? 0 : ARC_LOAD_BACKGROUND);
    }
}
static void tick(void) { if (check) check_tick(); compare_tick(); }
static void draw(float alpha) { (void)alpha; if (bench) bench_draw(); else if (check) check_draw(); }
arc_app arc_main(int argc, char **argv) {
    (void)argc; (void)argv;
    return (arc_app){ .title = "web load", .width = 960, .height = 540, .init = init, .tick = tick, .draw = draw, .clear_color = 0x101418 };
}
