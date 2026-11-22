/* ARC graphics front end: resources, draw lists, camera, text, procedural meshes, PNG/QOI. */
#include "arc_internal.h"

const arc__backend *arc__gfx;
arc__mesh_rec arc__meshes[ARC__MAX_MESHES];
arc__tex_rec  arc__textures[ARC__MAX_TEXTURES];

enum { TEX_WHITE = 1, TEX_FONT = 2, TEX_FIRST_USER = 3 };
#define ARC__MAX_BATCHES3D (ARC__MAX_MESHES * 2 + 65536)   /* solid batches (one per mesh and pass) + see-through runs */

/* key = mesh*2 + mirrored; slot: where in the pass (SL_*); z: the batch depth state (ARC__Z_*); order: inside the slot */
typedef struct { uint32_t key; int blend; float depth; int slot, z, order; uint32_t paint, paint2, paint3; int pat, glw; arc__instance inst; } raw_instance;   /* 0.10.15 glw: index + 1 in R.glows */   /* 0.10.1 pat: see arc__mesh_batch */
/* slots, in drawing order. SL_FAST: solid, usual depth, order 0 - counting-sorted by mesh (the common case).
   x-ray: the silhouettes (SL_XRAY) come after every other solid mesh and before the meshes they belong to (SL_XOBJ),
   so they are tested against the scene without their own mesh (a mesh never x-rays its own hidden parts) */
enum { SL_BG = 0, SL_PRE = 1, SL_FAST = 2, SL_PAINT = 3, SL_POST = 4, SL_XRAY = 5, SL_XOBJ = 6, SL_TRANS = 7, SL_OVER = 8 };
/* 0.8.8 SL_PAINT: what would be SL_FAST but painted (arc_mesh_style.paint_mask): sorted by (mask, tint2, mesh) so
   each paint scheme of a mesh is one batch */
/* per-frame limits: a draw that does not fit is dropped and counted (stats.dropped); the first time each limit
   fills, one "ARC error" line names it, so headless runs show it (0.6.4, from Moonbase Raid's half-black floor) */
enum { LIM_VERTS2D, LIM_BATCH2D, LIM_INSTANCES, LIM_IMM, LIM_LINES, LIM_BONES, LIM_PASSES, LIM_COUNT };
static int g_dropped, g_dropped_total;
static int g_plain_first = -1, g_plain_n;   /* see plain_fixup */
/* 0.10.12+1 (0.10.13) 2D offsets (Vector Trader): the vertices of the last 2D allocation are moved by the offset in
   force when they were reserved, at the next reservation or the frame's end (like plain_fixup) */
static struct { float x, y; bool clip; int cx, cy, cw, ch; } g_off_stack[8]; static int g_off_n;
static float g_off_x, g_off_y; static int g_offfix_first = -1, g_offfix_n; static float g_offfix_x, g_offfix_y;
/* 0.7.1 scene speed-ups (arc_scene.c, included after this file): the cell meshes are tagged with, this frame's counts,
   and the camera version (bumped by arc_set_camera) that cached visibility is checked against */
int arc__draw_cell;
static int g_culled, g_lod_n[4], g_static_draws; static uint32_t g_cam_version = 1;
static bool scene_cell_drawable(int cell);
static uint32_t scene_lod_pick(uint32_t mesh, const float *model);
static void scene_lod_forget(uint32_t mesh);
static int g_lines_thick, g_lines2d_frame; static float g_lines_wmax, g_lines_gmax;
bool arc__post_nan; int arc__post_nan_count; int arc__max_v2 = ARC__MAX_VERTS2D;   /* 0.10.12 (see arc_internal.h) */   /* 0.10.11 this frame's thick lines, widest, brightest */
static bool g_lod_any;   /* some mesh has LODs */
static void over_limit(int which) {
    static bool told[LIM_COUNT];
    static const struct { const char *what; int max; const char *tip; } L[LIM_COUNT] = {
        { "2D vertices", ARC__MAX_VERTS2D, "" },
        { "2D batches", ARC__MAX_BATCHES, " A new batch starts at every texture, blend or clip change: draw everything with one texture together" },
        { "mesh instances", ARC__MAX_INSTANCES, "" },
        { "immediate 3D vertices", ARC__MAX_IMM, "" },
        { "3D line points", ARC__MAX_LINES * 2, "" },
        { "skinning matrices", ARC__MAX_BONES, "" },
        { "render passes", ARC__MAX_PASSES, "" } };
    g_dropped++; g_dropped_total++;
    if (told[which]) return;
    told[which] = true;
    printf("ARC error draw limit: more than %d %s in one frame; later ones are dropped (counted in stats.dropped).%s%s\n",
           which == LIM_VERTS2D ? arc__max_v2 : L[which].max, L[which].what, L[which].tip,
           which == LIM_VERTS2D ? " arc_app.max_2d_vertices raises it; stats.vertices_2d shows how much a frame uses" : "");
    fflush(stdout);
}
int arc__gfx_dropped_total(void) { return g_dropped_total; }

static struct {
    bool ready, in_frame, has_frame;
    int w, h;                                   /* screen */
    float sdf2d;                                /* 0.10.1: distance-field text being drawn (see arc__batch2d.sdf) */
    arc__pattern pats[ARC__MAX_PATTERNS]; int npats;   /* 0.10.1 this frame's paint patterns */
    arc__glow glows[ARC__MAX_GLOWS]; int nglows;       /* 0.10.15 this frame's glow schemes */
    uint32_t clear;
    /* current camera + lights (copied into each pass) */
    arc_m4 view, proj, view_proj, inv_view_proj; arc_v3 eye;
    arc_m4 vp_rev;                              /* 0.7.2: the reversed-Z view-projection (see arc__pass.zrev) */
    arc_v3 view_fwd; bool view_persp; float line_push;   /* the current camera (any pass) and arc_set_line_depth_push */
    float light_dir[4], light_col[4], ambient[4], fog_col[4], fog[4], rim[4];
    uint32_t wmask; float wm[8], cut[12];   /* 0.9.9 world mask and cutaway (see arc__pass) */
    arc_v3 sh_center; float sh_radius, sh_strength; int sh_res;   /* shadows: radius 0 = off */
    int sh_casc; float sh_dist, sh_soft;                         /* cascades (1 = one box), their reach, softness */
    arc_v3 cam_fwd; float cam_tx, cam_ty; bool cam_persp;        /* the screen camera, for cascades */
    arc_v3 vcam_fwd; float vcam_tx, vcam_ty; bool vcam_persp;    /* 0.8.2: the camera set last (any target), for target cascades */
    uint32_t tp_id[ARC__TPOST_MAX]; float tp[ARC__TPOST_MAX][ARC__POST_COUNT];   /* 0.8.2: targets with their own post */
    bool tcasc[ARC__MAX_TEXTURES];                               /* 0.8.2: targets whose passes get shadow cascades */
    float post[ARC__POST_COUNT];
    float sky[12];
    float env[12]; bool env_set;                /* arc_set_environment (else derived per pass) */
    float refl_y, refl_strength, refl_scale; uint32_t refl_tex;   /* 0.8.7 planar reflection and its target (made on use) */
    float lt[ARC__MAX_FRAME_LIGHTS][ARC__LT_FLOATS]; int lt_count;   /* this frame's point/spot lights (see arc__pass) */
    float lt_score[ARC__MAX_FRAME_LIGHTS]; uint8_t lt_owner[ARC__MAX_FRAME_LIGHTS];   /* (0.10.2: 0, or the viewport index + 1 it was added in) */
    float lsh_vp[ARC__MAX_SHADOW_LIGHTS * 6 * 16]; int lsh_faces[ARC__MAX_SHADOW_LIGHTS], lsh_light[ARC__MAX_SHADOW_LIGHTS], lsh_count, lsh_res, lsh_fres;   /* 0.9.5 light shadows */
    uint32_t lt_tiles[ARC__MAX_PASSES * ARC__LT_TILES]; int lt_ntiles;   /* 0.9.5 tile tables, a pass after another */
    uint8_t *lt_idx; int lt_nidx, lt_cidx;                               /* light lists */
    /* per-frame lists */
    float *bones; int bone_count;
    raw_instance *raw; int raw_count;
    arc__instance *sorted; arc__mesh_batch *mesh_batches; int mesh_batch_count;
    int *mesh_counts;
    arc_vertex *imm; int imm_count;
    arc__line_vertex *lines; int line_count;
    arc__line_vertex *ltris; int ltri_count;          /* 0.10.11 thick lines as triangles (allocated at the first one) */
    float line_width, line_glow; int line_cap; bool line_cap2d_set;   /* 0.10.11, kept until changed (arc_set_line_width / _glow / _cap) */
    arc__vert2d *v2; int v2_count;
    arc__batch2d *b2; int b2_count;
    arc__pass passes[ARC__MAX_PASSES]; int pass_count;
    uint32_t target_stack[16]; int target_depth;     /* texture ids; 0 = screen */
    /* 0.10.2 viewports: this frame's (vps), the one being drawn (vp_cur, -1 none), last frame's (vpl); each one's
       target (kept between frames while used) and its planar reflection target; the window camera saved meanwhile */
    struct vport_rec { int x, y, w, h; arc_m4 vp, ivp; arc_v3 eye, fwd; int pass0; } vps[ARC_MAX_VIEWPORTS], vpl[ARC_MAX_VIEWPORTS];
    int vp_count, vp_cur, vpl_count; bool ta_on; int ta[4];   /* ta: arc_viewport_begin outside a frame (tick): UI and sizes only */ uint32_t vp_tex[ARC_MAX_VIEWPORTS], vp_refl[ARC_MAX_VIEWPORTS]; bool vp_used[ARC_MAX_VIEWPORTS];
    struct { arc_m4 view, proj, view_proj, inv_view_proj, vp_rev; arc_v3 eye, view_fwd, cam_fwd; bool view_persp, cam_persp; float cam_tx, cam_ty; } vp_cam;
    int blend; bool clip; int cx, cy, cw, ch;
    /* builtins */
    arc_mesh cube, sphere;
    /* stats */
    arc_gfx_stats stats; double avg_ms; float last_tick_ms;
    char pending_shots[8][512]; int pending_shot_count;
} R;

/* ======================================================================== */
/* Init / shutdown                                                          */
/* ======================================================================== */

static uint32_t alloc_tex_id(void) { for (uint32_t i = TEX_FIRST_USER; i < ARC__MAX_TEXTURES; i++) if (!arc__textures[i].used) return i; return 0; }
static uint32_t alloc_mesh_id(void) { for (uint32_t i = 1; i < ARC__MAX_MESHES; i++) if (!arc__meshes[i].used) return i; return 0; }

/* CPU mipmaps for the software renderer (GPU backends make their own): each level halves the last one */
static void tex_free_mips(arc__tex_rec *t) {
    for (int i = 1; i < t->levels; i++) { free(t->mip[i]); t->mip[i] = NULL; }
    t->levels = 0;
}
static void tex_build_mips2(arc__tex_rec *t, bool always);
void arc__tex_build_mips(arc__tex_rec *t) { tex_build_mips2(t, false); }
static void tex_build_mips2(arc__tex_rec *t, bool always) {   /* always: on every backend (0.9.6 texture arrays upload them) */
    tex_free_mips(t);
    t->mip[0] = t->rgba; t->mw[0] = t->w; t->mh[0] = t->h; t->levels = 1;
    if (!(t->flags & ARC_TEX_MIPMAPS) || (arc__gfx != &arc__backend_soft && !always)) return;
    while (t->levels < ARC__MAX_MIPS) {
        int l = t->levels, pw = t->mw[l - 1], ph = t->mh[l - 1];
        if (pw == 1 && ph == 1) break;
        int w = pw > 1 ? pw / 2 : 1, h = ph > 1 ? ph / 2 : 1;
        uint8_t *src = t->mip[l - 1], *dst = (uint8_t *)arc__xalloc((size_t)w * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                for (int c = 0; c < 4; c++) {
                    int x0 = arc_mini(x * 2, pw - 1), x1 = arc_mini(x * 2 + 1, pw - 1), y0 = arc_mini(y * 2, ph - 1), y1 = arc_mini(y * 2 + 1, ph - 1);
                    int sum = src[((size_t)y0 * pw + x0) * 4 + c] + src[((size_t)y0 * pw + x1) * 4 + c] + src[((size_t)y1 * pw + x0) * 4 + c] + src[((size_t)y1 * pw + x1) * 4 + c];
                    dst[((size_t)y * w + x) * 4 + c] = (uint8_t)((sum + 2) / 4);
                }
        t->mip[l] = dst; t->mw[l] = w; t->mh[l] = h; t->levels++;
    }
}
#define TEX_FAILED 0x10000                 /* internal flag bit: a queued load that failed (its 1x1 stand-in stays) */
static size_t tex_rgba_bytes(int w, int h, int flags) { size_t b = (size_t)w * (size_t)h * 4; return (flags & ARC_TEX_MIPMAPS) ? b + b / 3 : b; }
static bool tex_create_id(uint32_t id, int w, int h, const uint8_t *rgba, int flags) {
    arc__tex_rec *t = &arc__textures[id];
    t->used = true; t->w = w; t->h = h; t->smooth = (flags & ARC_TEX_SMOOTH) != 0; t->flags = flags;
    t->fmt = ARC_TEXFMT_RGBA8; t->gpu_bytes = tex_rgba_bytes(w, h, flags);
    t->rgba = (uint8_t *)arc__xalloc((size_t)w * (size_t)h * 4);
    if (rgba) memcpy(t->rgba, rgba, (size_t)w * (size_t)h * 4);
    arc__tex_build_mips(t);
    return arc__gfx->tex_create(id, w, h, t->rgba, flags);
}

/* ---- 0.7.1 compressed textures and images decoded off the main thread ---- */
bool arc__no_ctex;
static bool tex_direct_ok(int fmt, int w, int h) {     /* the GPU takes these blocks as they are (caps are fixed after init) */
    return !arc__no_ctex && arc__gfx && arc__gfx->tex_create_compressed && arc__gfx->compressed_formats &&
           ((arc__gfx->compressed_formats() >> fmt) & 1) && w % 4 == 0 && h % 4 == 0;
}
void arc__img_free(arc__img *o) {
    if (o->web) { const arc__imgdec *dec = arc__imgdec_get(); if (dec) dec->release(o->web); }   /* 0.8.6 */
    free(o->rgba); arc__ctex_free(&o->c);
    for (int i = 0; i < o->nmips; i++) free(o->mips[i]);
    memset(o, 0, sizeof *o);
}
/* 0.8.2 (MechBattleArc's request): the largest side of a mipmapped (3D) texture after loading, 0 = no cap. Read by
   loader threads; set it before loading. */
static volatile int g_tex_max;
static bool arc__img_decode_full(const uint8_t *d, size_t n, int flags, arc__img *o);
void arc_set_texture_max_size(int px) { g_tex_max = px > 0 ? arc_maxi(px, 4) : 0; }
int  arc_texture_max_size(void) { return g_tex_max; }
static void img_cap(arc__img *o, int flags) {   /* halve (2 x 2 box) until the largest side fits */
    int cap = g_tex_max;
    if (!cap || !(flags & ARC_TEX_MIPMAPS) || !o->rgba) return;
    while (o->w > cap || o->h > cap) {
        int pw = o->w, ph = o->h, w = pw > 1 ? (pw + 1) / 2 : 1, h = ph > 1 ? (ph + 1) / 2 : 1;
        const uint8_t *src = o->rgba; uint8_t *dst = (uint8_t *)arc__xalloc((size_t)w * h * 4);
        for (int y = 0; y < h; y++) {
            int y0 = arc_mini(y * 2, ph - 1), y1 = arc_mini(y * 2 + 1, ph - 1);
            for (int x = 0; x < w; x++) {
                int x0 = arc_mini(x * 2, pw - 1), x1 = arc_mini(x * 2 + 1, pw - 1);
                for (int c = 0; c < 4; c++)
                    dst[((size_t)y * w + x) * 4 + c] = (uint8_t)((src[((size_t)y0 * pw + x0) * 4 + c] + src[((size_t)y0 * pw + x1) * 4 + c] + src[((size_t)y1 * pw + x0) * 4 + c] + src[((size_t)y1 * pw + x1) * 4 + c] + 2) / 4);
            }
        }
        free(o->rgba); o->rgba = dst; o->w = w; o->h = h;
        for (int i = 0; i < o->nmips; i++) free(o->mips[i]);
        o->nmips = 0;   /* the soft renderer's file levels no longer line up: it builds its own */
    }
}
static void ctex_cap(arc__ctex *c, int flags) {   /* skip the levels that are too big (only while smaller ones exist) */
    int cap = g_tex_max, k = 0;
    if (!cap || !(flags & ARC_TEX_MIPMAPS)) return;
    while (k < c->levels - 1 && (c->lw[k] > cap || c->lh[k] > cap)) k++;
    if (!k) return;
    for (int i = 0; i + k < c->levels; i++) { c->off[i] = c->off[i + k]; c->lw[i] = c->lw[i + k]; c->lh[i] = c->lh[i + k]; }
    c->levels -= k; c->w = c->lw[0]; c->h = c->lh[0];
}
bool arc__img_decode(const uint8_t *d, size_t n, int flags, arc__img *o) {
    bool ok = arc__img_decode_full(d, n, flags, o);
    if (ok) img_cap(o, flags);
    return ok;
}
static bool arc__img_decode_full(const uint8_t *d, size_t n, int flags, arc__img *o) {
    memset(o, 0, sizeof *o);
    if (d && n >= 32 && !memcmp(d, "ARCT", 4)) {
        if (!arc__ctex_open(d, n, &o->c)) return false;
        ctex_cap(&o->c, flags);
        o->w = o->c.w; o->h = o->c.h;
        if (tex_direct_ok(o->c.fmt, o->c.w, o->c.h)) { o->comp = true; return true; }
        o->rgba = arc__ctex_decode(&o->c, 0);    /* the CPU path: RGBA8 (the software renderer samples the file's own mips) */
        if (o->rgba && (flags & ARC_TEX_MIPMAPS) && arc__gfx == &arc__backend_soft)
            for (int l = 1; l < o->c.levels && o->nmips < ARC__MAX_MIPS - 1; l++) { uint8_t *m = arc__ctex_decode(&o->c, l); if (!m) break; o->mips[o->nmips++] = m; }
        arc__ctex_free(&o->c);
        return o->rgba != NULL;
    }
    if (g_tex_max && (flags & ARC_TEX_MIPMAPS) && d && n > 4 && d[0] == 0xFF && d[1] == 0xD8)   /* 0.8.3: a capped JPEG decodes small */
        o->rgba = arc__jpeg_read_max(d, n, &o->w, &o->h, g_tex_max);
    else o->rgba = arc_image_decode(d, n, &o->w, &o->h);
    return o->rgba != NULL;
}
/* ---- 0.8.6 images the browser decodes (web, arc_app.browser_decode) ---- */
static uint32_t img_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static int jpeg_exif_turned(const uint8_t *p, size_t n) {   /* an APP1 Exif block whose Orientation is not 1 */
    if (n < 14 || memcmp(p, "Exif\0\0", 6)) return 0;
    const uint8_t *t = p + 6; size_t tn = n - 6; bool le = t[0] == 'I';
    #define EXIF16(q) (le ? (uint32_t)((q)[0] | (q)[1] << 8) : (uint32_t)((q)[0] << 8 | (q)[1]))
    #define EXIF32(q) (le ? (uint32_t)((q)[0] | (q)[1] << 8 | (q)[2] << 16 | (uint32_t)(q)[3] << 24) : img_be32(q))
    if (tn < 8 || (t[0] != 'I' && t[0] != 'M')) return 0;
    uint32_t ifd = EXIF32(t + 4);
    if ((size_t)ifd + 2 > tn) return 0;
    uint32_t cnt = EXIF16(t + ifd);
    for (uint32_t i = 0; i < cnt && (size_t)ifd + 2 + (i + 1) * 12 <= tn; i++) {
        const uint8_t *e = t + ifd + 2 + i * 12;
        if (EXIF16(e) == 0x0112) return EXIF16(e + 8) != 1;
    }
    #undef EXIF16
    #undef EXIF32
    return 0;
}
int arc__img_browser_type(const uint8_t *d, size_t n, bool *alpha, int *w, int *h) {
    *alpha = false; *w = *h = 0;
    if (d && n > 33 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8) && !memcmp(d + 12, "IHDR", 4)) {
        int depth = d[24], ct = d[25];
        *w = (int)img_be32(d + 16); *h = (int)img_be32(d + 20);
        if (depth > 8 || *w <= 0 || *h <= 0) return 0;             /* 16-bit: browsers reduce it their own way */
        *alpha = ct == 4 || ct == 6;
        for (size_t at = 8; at + 12 <= n;) {                        /* chunks up to the image data */
            uint32_t len = img_be32(d + at); const uint8_t *ty = d + at + 4;
            if (!memcmp(ty, "IDAT", 4)) return 2;
            if (!memcmp(ty, "tRNS", 4)) *alpha = true;
            if (!memcmp(ty, "iCCP", 4) || !memcmp(ty, "gAMA", 4) || !memcmp(ty, "cHRM", 4) || !memcmp(ty, "eXIf", 4)) return 0;   /* colour managed / turned */
            if (len > n - at - 12) return 0;
            at += 12 + (size_t)len;
        }
        return 0;
    }
    if (d && n > 4 && d[0] == 0xFF && d[1] == 0xD8) {
        for (size_t at = 2; at + 4 <= n;) {
            if (d[at] != 0xFF) return 0;
            int m = d[at + 1];
            if (m == 0xFF) { at++; continue; }                     /* fill bytes */
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { at += 2; continue; }
            if (m == 0xDA || m == 0xD9) return 0;                    /* image data before any frame header */
            size_t len = (size_t)(d[at + 2] << 8 | d[at + 3]);
            if (len < 2 || at + 2 + len > n) return 0;
            const uint8_t *q = d + at + 4;
            if (m == 0xE1 && jpeg_exif_turned(q, len - 2)) return 0;   /* browsers turn it, ARC doesn't */
            if (m == 0xC0 || m == 0xC1 || m == 0xC2) {               /* baseline, extended, progressive */
                if (len < 8) return 0;
                *h = q[1] << 8 | q[2]; *w = q[3] << 8 | q[4];
                return (q[5] == 1 || q[5] == 3) && *w > 0 && *h > 0 ? 1 : 0;   /* grey or YCbCr (not CMYK) */
            }
            if (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) return 0;   /* lossless, arithmetic */
            at += 2 + len;
        }
    }
    return 0;
}
bool arc__img_decode_or_defer(const uint8_t *d, size_t n, int flags, arc__img *o, bool defer) {
    bool alpha; int w, h, type = defer ? arc__img_browser_type(d, n, &alpha, &w, &h) : 0;
    if (type && g_tex_max && (flags & ARC_TEX_MIPMAPS) && (w > g_tex_max || h > g_tex_max)) type = 0;   /* capped: ARC's box filter */
    if (!type) return arc__img_decode(d, n, flags, o);
    memset(o, 0, sizeof *o); o->src = d; o->src_n = n; o->w = w; o->h = h;
    return true;
}
/* a deferred image to RGBA bytes: the browser's pixels (decoded), else ARC's own decoder on its bytes */
static bool img_resolve(arc__img *img, int flags) {
    if (img->rgba || img->comp || !img->src) return img->rgba || img->comp;
    const arc__imgdec *dec = arc__imgdec_get();
    if (img->web_done && dec && img->w > 0 && img->h > 0) {
        uint8_t *px = (uint8_t *)malloc((size_t)img->w * img->h * 4);
        if (px && dec->pixels(img->web, px, img->w, img->h)) { dec->release(img->web); img->web = 0; img->web_done = false; img->rgba = px; arc__webimg_stats[3]++; img_cap(img, flags); return true; }
        free(px);
    }
    if (img->web && dec) dec->release(img->web);
    const uint8_t *src = img->src; size_t sn = img->src_n;
    bool ok = arc__img_decode(src, sn, flags, img);              /* (clears img) */
    arc__webimg_stats[2]++;
    return ok;
}
static void tex_free_mips(arc__tex_rec *t);
/* make texture `id` from a decoded image (main thread; takes what img holds). false: nothing made, the record is clear */
bool arc__no_assets;
static int tex_w(uint32_t id) { return arc__textures[id].vw ? arc__textures[id].vw : arc__textures[id].w; }   /* 0.9.8 the size a game sees */
static int tex_h(uint32_t id) { return arc__textures[id].vh ? arc__textures[id].vh : arc__textures[id].h; }
static bool tex_create_img(uint32_t id, arc__img *img, int flags) {
    arc__tex_rec *t = &arc__textures[id];
    if (arc__no_assets && !img->web && img->w > 0 && img->h > 0) {   /* 0.9.8: keep the size and the average colour */
        uint32_t sum[4] = { 0, 0, 0, 0 }, n = 0; uint8_t px[4] = { 160, 160, 160, 255 };
        if (img->rgba) {
            for (int j = 0; j < 8; j++) for (int i = 0; i < 8; i++) {
                const uint8_t *q = img->rgba + ((size_t)(j * img->h / 8) * (size_t)img->w + (size_t)(i * img->w / 8)) * 4;
                for (int c = 0; c < 4; c++) sum[c] += q[c];
                n++;
            }
            for (int c = 0; c < 4; c++) px[c] = (uint8_t)(sum[c] / n);
        }
        int vw = img->w, vh = img->h;
        arc__img_free(img);
        if (!tex_create_id(id, 1, 1, px, flags)) return false;
        t->vw = vw; t->vh = vh; return true;
    }
    if (img->src && !img->rgba && !img->comp) {                     /* 0.8.6 an image the browser decoded */
        if (img->web_done && !arc__webimg_bytes && arc__gfx->tex_create_web && arc__gfx->tex_create_web(id, img->web, img->w, img->h, flags)) {
            t->used = true; t->w = img->w; t->h = img->h; t->smooth = (flags & ARC_TEX_SMOOTH) != 0; t->flags = flags; t->rgba = NULL; t->levels = 0;
            t->fmt = ARC_TEXFMT_RGBA8; t->gpu_bytes = tex_rgba_bytes(img->w, img->h, flags);
            arc__webimg_stats[1]++; arc__img_free(img); return true;
        }
        if (!img_resolve(img, flags)) { memset(t, 0, sizeof *t); return false; }
    }
    t->used = true; t->w = img->w; t->h = img->h; t->smooth = (flags & ARC_TEX_SMOOTH) != 0; t->flags = flags; t->rgba = NULL; t->levels = 0;
    if (img->comp) {
        int levels = (flags & ARC_TEX_MIPMAPS) ? img->c.levels : 1; const uint8_t *lv[ARC__MAX_MIPS]; size_t lb[ARC__MAX_MIPS], total = 0;
        for (int i = 0; i < levels; i++) { lv[i] = img->c.data + img->c.off[i]; lb[i] = arc__ctex_level_bytes(&img->c, i); total += lb[i]; }
        t->fmt = img->c.fmt; t->gpu_bytes = total;
        if (arc__gfx->tex_create_compressed(id, img->c.fmt, img->w, img->h, levels, lv, lb, flags)) { arc__img_free(img); return true; }
        img->rgba = arc__ctex_decode(&img->c, 0); arc__ctex_free(&img->c); img->comp = false;   /* refused: decode it */
        if (!img->rgba) { memset(t, 0, sizeof *t); return false; }
    }
    t->fmt = ARC_TEXFMT_RGBA8; t->gpu_bytes = tex_rgba_bytes(img->w, img->h, flags);
    t->rgba = img->rgba; img->rgba = NULL;
    if (img->nmips && (flags & ARC_TEX_MIPMAPS) && arc__gfx == &arc__backend_soft) {   /* the file's own levels */
        t->mip[0] = t->rgba; t->mw[0] = t->w; t->mh[0] = t->h; t->levels = 1;
        for (int i = 0; i < img->nmips; i++) { t->mip[t->levels] = img->mips[i]; t->mw[t->levels] = arc_maxi(t->w >> (i + 1), 1); t->mh[t->levels] = arc_maxi(t->h >> (i + 1), 1); t->levels++; }
        img->nmips = 0;
    } else arc__tex_build_mips(t);
    arc__img_free(img);
    if (!arc__gfx->tex_create(id, t->w, t->h, t->rgba, flags)) { tex_free_mips(t); free(t->rgba); memset(t, 0, sizeof *t); return false; }
    return true;
}

bool arc__gfx_init(const arc__backend *backend, int w, int h) {
    arc__gfx = backend; R.w = w; R.h = h; R.vp_cur = -1;
    arc__no_ctex = arc_arg_flag("no-compressed-textures") || getenv("ARC_NO_COMPRESSED_TEXTURES") != NULL;
    { const char *tm = arc_arg_value("texture-max-size"); if (tm) arc_set_texture_max_size(atoi(tm)); }   /* 0.8.2 */
    arc__no_assets = arc_arg_flag("no-assets");   /* 0.9.8 (MechBattleArc): lighter headless test runs */
    if (backend == &arc__backend_soft) arc__depth_precise = true;   /* float depth, any range (GPU backends set it in init) */
    if (!backend->init()) { fprintf(stderr, "ARC error backend %s init failed\n", backend->name); return false; }
    if (arc__depth_mode != ARC_DEPTH_STANDARD)
        printf("ARC log depth: %s on %s%s\n", arc__depth_mode == ARC_DEPTH_REVERSED_INFINITE ? "reversed-Z, no far plane" : "reversed-Z", backend->name,
               arc__depth_precise ? " (float depth, 0..1 clip range)" : " (float depth, but no 0..1 clip range here: a smaller precision gain)");
    R.raw = (raw_instance *)arc__xalloc(sizeof(raw_instance) * ARC__MAX_INSTANCES);
    R.sorted = (arc__instance *)arc__xalloc(sizeof(arc__instance) * ARC__MAX_INSTANCES);
    R.mesh_batches = (arc__mesh_batch *)arc__xalloc(sizeof(arc__mesh_batch) * ARC__MAX_BATCHES3D);
    R.mesh_counts = (int *)arc__xalloc(sizeof(int) * ARC__MAX_MESHES * 2);
    R.imm = (arc_vertex *)arc__xalloc(sizeof(arc_vertex) * ARC__MAX_IMM);
    R.lines = (arc__line_vertex *)arc__xalloc(sizeof(arc__line_vertex) * ARC__MAX_LINES * 2);
    if (arc__max_v2 < 65536) arc__max_v2 = ARC__MAX_VERTS2D;   /* 0.10.13 arc_app.max_2d_vertices */
    R.v2 = (arc__vert2d *)arc__xalloc(sizeof(arc__vert2d) * (size_t)arc__max_v2);
    R.b2 = (arc__batch2d *)arc__xalloc(sizeof(arc__batch2d) * ARC__MAX_BATCHES);
    uint8_t white[4] = { 255, 255, 255, 255 };
    tex_create_id(TEX_WHITE, 1, 1, white, ARC_TEX_REPEAT);
    /* font atlas: 16 x 6 glyphs of 8x8 -> 128 x 48 */
    uint8_t *font = (uint8_t *)arc__xalloc(128 * 48 * 4);
    for (int g = 0; g < 96; g++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int px = (g % 16) * 8 + x, py = (g / 16) * 8 + y;
                uint8_t on = (arc__font8x8[g][y] >> (7 - x)) & 1;
                uint8_t *p = font + (py * 128 + px) * 4; p[0] = p[1] = p[2] = 255; p[3] = on ? 255 : 0;
            }
    tex_create_id(TEX_FONT, 128, 48, font, 0); free(font);
    R.clear = 0x0B0E14;
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.3f), 0xFFFFFF, 0x404850);
    arc_set_fog(0, 0, 0);
    arc_set_rim(0, 3.0f);
    R.bones = (float *)arc__xalloc(sizeof(float) * 12 * ARC__MAX_BONES);
    arc_camera c = { { 0, 2, 6 }, { 0, 0, 0 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 500.0f };
    arc_set_camera(&c);
    R.ready = true;                      /* before the built-in meshes: arc_mesh_create needs it */
    R.cube = arc_mesh_cube(0xFFFFFF);
    R.sphere = arc_mesh_sphere(0xFFFFFF, 16);
    R.stats.backend = backend->name;
    return true;
}
void arc__gfx_shutdown(void) {
    if (!R.ready) return;
    if (arc__gfx && arc__gfx->shutdown) arc__gfx->shutdown();
    R.ready = false;
}

/* ======================================================================== */
/* Meshes                                                                   */
/* ======================================================================== */

static bool mesh_ok(arc_mesh m) { return m.id && m.id < ARC__MAX_MESHES && arc__meshes[m.id].used; }
static void mesh_bounds(arc__mesh_rec *m) {
    const arc_vertex *v = m->v;
    m->lo = m->hi = arc_v3_make(v[0].px, v[0].py, v[0].pz);
    for (int i = 1; i < m->vcount; i++) {
        m->lo.x = arc_minf(m->lo.x, v[i].px); m->lo.y = arc_minf(m->lo.y, v[i].py); m->lo.z = arc_minf(m->lo.z, v[i].pz);
        m->hi.x = arc_maxf(m->hi.x, v[i].px); m->hi.y = arc_maxf(m->hi.y, v[i].py); m->hi.z = arc_maxf(m->hi.z, v[i].pz);
    }
}
/* every normal zero: smooth normals, each face weighted by its area */
static void mesh_auto_normals(arc_vertex *v, int vcount, const uint32_t *idx, int icount) {
    for (int i = 0; i < vcount; i++) if (v[i].nx != 0 || v[i].ny != 0 || v[i].nz != 0) return;
    for (int t = 0; t + 2 < icount; t += 3) {
        uint32_t i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
        if (i0 >= (uint32_t)vcount || i1 >= (uint32_t)vcount || i2 >= (uint32_t)vcount) continue;
        arc_v3 a = { v[i0].px, v[i0].py, v[i0].pz }, b = { v[i1].px, v[i1].py, v[i1].pz }, c = { v[i2].px, v[i2].py, v[i2].pz };
        arc_v3 n = arc_v3_cross(arc_v3_sub(b, a), arc_v3_sub(c, a));
        uint32_t k[3] = { i0, i1, i2 };
        for (int j = 0; j < 3; j++) { v[k[j]].nx += n.x; v[k[j]].ny += n.y; v[k[j]].nz += n.z; }
    }
    for (int i = 0; i < vcount; i++) {
        arc_v3 n = arc_v3_make(v[i].nx, v[i].ny, v[i].nz); float l = arc_v3_len(n);
        if (l > 1e-12f) n = arc_v3_scale(n, 1.0f / l); else n = arc_v3_make(0, 1, 0);
        v[i].nx = n.x; v[i].ny = n.y; v[i].nz = n.z;
    }
}
/* 0.9.0 names for meshes and textures (the file's name without folder or extension, the primitive's kind, or
   arc_mesh_set_name), shown by the `frame` command and in logs */
static char g_mname[ARC__MAX_MESHES][32], g_tname[ARC__MAX_TEXTURES][32];
static void name_from_path(char *dst, const char *path) {
    if (!path) { dst[0] = 0; return; }
    const char *b = path; for (const char *q = path; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
    size_t n = strlen(b); const char *dot = strrchr(b, '.'); if (dot && dot > b) n = (size_t)(dot - b);
    if (n > 31) n = 31;
    memcpy(dst, b, n); dst[n] = 0;
}
static void mesh_name_set(uint32_t id, const char *path) { if (id && id < ARC__MAX_MESHES) name_from_path(g_mname[id], path); }
static void tex_name_set(uint32_t id, const char *path) { if (id && id < ARC__MAX_TEXTURES) name_from_path(g_tname[id], path); }
static arc_mesh mesh_create_at(uint32_t id, const arc_vertex *v, int vcount, const uint32_t *idx, int icount, const arc_skin_vertex *skin) {
    arc_mesh none = { 0 };
    if (!R.ready || vcount <= 0 || !v) return none;
    if (!id) id = alloc_mesh_id();            /* else a slot the loader reserved */
    if (!id) { arc_log("mesh limit reached (%d)", ARC__MAX_MESHES); return none; }
    arc__mesh_rec *m = &arc__meshes[id];
    uint32_t *seq = NULL;
    if (!idx) { icount = vcount; seq = (uint32_t *)arc__xalloc(sizeof(uint32_t) * (size_t)vcount); for (int i = 0; i < vcount; i++) seq[i] = (uint32_t)i; idx = seq; }
    memset(m, 0, sizeof *m);
    m->used = true; m->vcount = vcount; m->icount = icount;
    m->v = (arc_vertex *)arc__xalloc(sizeof(arc_vertex) * (size_t)vcount); memcpy(m->v, v, sizeof(arc_vertex) * (size_t)vcount);
    m->idx = (uint32_t *)arc__xalloc(sizeof(uint32_t) * (size_t)icount); memcpy(m->idx, idx, sizeof(uint32_t) * (size_t)icount);
    for (int i = 0; i < icount; i++) if (m->idx[i] >= (uint32_t)vcount) m->idx[i] = 0;   /* never index past the vertices */
    if (skin) {
        m->skin = (uint8_t *)arc__xalloc((size_t)vcount * 8); memcpy(m->skin, skin, (size_t)vcount * 8);
        for (int i = 0; i < vcount; i++) {   /* weights must add up to 255 (the shaders divide by it) */
            uint8_t *k = m->skin + (size_t)i * 8; int sum = k[4] + k[5] + k[6] + k[7];
            if (sum == 0) { k[4] = 255; k[5] = k[6] = k[7] = 0; }
            for (int j = 0; j < 4; j++) if (k[4 + j] && k[j] + 1 > m->joints) m->joints = k[j] + 1;
        }
    }
    mesh_auto_normals(m->v, vcount, m->idx, icount);
    mesh_bounds(m);
    free(seq);
    if (!arc__gfx->mesh_create(id, m->v, vcount, m->idx, icount, m->skin)) { free(m->v); free(m->idx); free(m->skin); memset(m, 0, sizeof *m); return none; }
    arc_mesh r = { id }; return r;
}
static arc_mesh mesh_create(const arc_vertex *v, int vcount, const uint32_t *idx, int icount, const arc_skin_vertex *skin) { return mesh_create_at(0, v, vcount, idx, icount, skin); }
arc_mesh arc_mesh_create(const arc_vertex *v, int vcount, const uint32_t *idx, int icount) { return mesh_create(v, vcount, idx, icount, NULL); }
arc_mesh arc_mesh_create_skinned(const arc_vertex *v, int vcount, const uint32_t *idx, int icount, const arc_skin_vertex *skin) {
    return mesh_create(v, vcount, idx, icount, skin);
}
bool arc_mesh_skinned(arc_mesh m) { return mesh_ok(m) && arc__meshes[m.id].skin != NULL; }
void arc_mesh_update(arc_mesh mh, const arc_vertex *v, int vcount) {
    if (!mesh_ok(mh) || !v) return;
    arc__mesh_rec *m = &arc__meshes[mh.id];
    if (vcount != m->vcount) { arc_log("arc_mesh_update: %d vertices given, the mesh has %d", vcount, m->vcount); return; }
    memcpy(m->v, v, sizeof(arc_vertex) * (size_t)vcount);
    mesh_auto_normals(m->v, vcount, m->idx, m->icount);
    mesh_bounds(m);
    if (arc__gfx->mesh_update) arc__gfx->mesh_update(mh.id, m->v, vcount);
}
static void tex_destroy_id(uint32_t id);
void arc_mesh_destroy(arc_mesh mh) {
    if (!mesh_ok(mh)) return;
    arc__mesh_rec *m = &arc__meshes[mh.id];
    if (m->owns_texture && m->texture) tex_destroy_id(m->texture);
    for (int i = 0; i < 3; i++) if ((m->mat_owns >> i & 1) && m->mat_tex[i]) tex_destroy_id(m->mat_tex[i]);
    if (m->glow_owned && m->glow_tex) tex_destroy_id(m->glow_tex);   /* 0.10.15 */
    arc__gfx->mesh_destroy(mh.id);
    free(m->v); free(m->idx); free(m->skin); memset(m, 0, sizeof(arc__mesh_rec)); g_mname[mh.id][0] = 0;
    if (g_lod_any) scene_lod_forget(mh.id);
}
int arc_mesh_triangles(arc_mesh m) { return mesh_ok(m) ? arc__meshes[m.id].icount / 3 : 0; }
int arc_mesh_vertex_count(arc_mesh m) { return mesh_ok(m) ? arc__meshes[m.id].vcount : 0; }
void arc_mesh_set_texture(arc_mesh mh, arc_texture t) {
    if (!mesh_ok(mh)) return;
    arc__mesh_rec *m = &arc__meshes[mh.id];
    if (m->owns_texture && m->texture && m->texture != t.id) tex_destroy_id(m->texture);
    m->owns_texture = false;
    m->texture = (t.id && t.id < ARC__MAX_TEXTURES && arc__textures[t.id].used) ? t.id : 0;
    if (m->pending) m->pend_set |= 1;
}
arc_texture arc_mesh_texture(arc_mesh m) { arc_texture t = { mesh_ok(m) ? arc__meshes[m.id].texture : 0 }; return t; }
static bool tex_ok(arc_texture t);
static void mesh_material_free_owned(arc__mesh_rec *m, const uint32_t keep[3]) {
    for (int i = 0; i < 3; i++)
        if ((m->mat_owns >> i & 1) && m->mat_tex[i]) {
            bool kept = false; for (int k = 0; k < 3; k++) kept |= keep && keep[k] == m->mat_tex[i];
            if (!kept) tex_destroy_id(m->mat_tex[i]);
        }
    m->mat_owns = 0;
}
/* the material's packed form (arc__mesh_rec.mat) */
static void material_pack_splat(arc__mesh_rec *m, const arc_material *q);
static void material_pack(arc__mesh_rec *m, const arc_material *q) {
    material_pack_splat(m, q);
    memset(m->mat, 0, sizeof m->mat);
    uint32_t t[3] = { tex_ok(q->normal) ? q->normal.id : 0, tex_ok(q->orm) ? q->orm.id : 0, tex_ok(q->emissive) ? q->emissive.id : 0 };
    memcpy(m->mat_tex, t, sizeof t);
    m->mat[ARC__MAT_HAS_N] = t[0] ? 1.0f : 0.0f;
    m->mat[ARC__MAT_NSCALE] = q->normal_scale != 0 ? q->normal_scale : 1.0f;
    m->mat[ARC__MAT_PBR] = q->pbr ? 1.0f : 0.0f;
    m->mat[ARC__MAT_OCC] = t[1] ? arc_clampf(q->occlusion, 0, 1) : 0.0f;
    m->mat[ARC__MAT_METAL] = arc_clampf(q->metallic, 0, 1);
    m->mat[ARC__MAT_ROUGH] = arc_clampf(q->roughness, 0, 1);
    m->mat[ARC__MAT_HAS_O] = t[1] ? 1.0f : 0.0f;
    m->mat[ARC__MAT_HAS_E] = t[2] ? 1.0f : 0.0f;
    for (int i = 0; i < 3; i++) m->mat[ARC__MAT_ER + i] = arc_maxf(q->emissive_rgb[i], 0);
    /* 0.8.4 detail layer and hex tiling */
    memset(m->mat2, 0, sizeof m->mat2); m->detail_tex = tex_ok(q->detail) ? q->detail.id : 0;
    if (m->detail_tex) {
        m->mat2[ARC__MAT2_DMODE] = q->detail_tiling == ARC_TILE_HEX ? 2.0f : 1.0f;
        m->mat2[ARC__MAT2_DSCALE] = arc_maxf(q->detail_scale, 0);
        m->mat2[ARC__MAT2_DSTR] = q->detail_strength > 0 ? arc_clampf(q->detail_strength, 0, 1) : 1.0f;
        m->mat2[ARC__MAT2_DROT] = arc_clampf(q->detail_rotate, 0, 1);
        m->mat2[ARC__MAT2_DCON] = q->detail_contrast > 0 ? arc_clampf(q->detail_contrast, 0.5f, 0.99f) : 0.75f;
        m->mask_tex = tex_ok(q->detail_mask) ? q->detail_mask.id : 0;
        if (m->mask_tex) {
            m->mat2[ARC__MAT2_MON] = 1.0f;
            m->mat2[ARC__MAT2_MSCALE] = q->mask_scale > 0 ? q->mask_scale : 0.0f;
            m->mat2[ARC__MAT2_MTHR] = arc_clampf(q->mask_threshold, 0, 1);
            m->mat2[ARC__MAT2_MSOFT] = q->mask_softness > 0 ? arc_maxf(q->mask_softness, 0.001f) : 0.12f;
        }
    } else m->mask_tex = 0;
    if (q->base_tiling == ARC_TILE_HEX && !m->layers_tex) {
        m->mat2[ARC__MAT2_BMODE] = 2.0f; m->mat2[ARC__MAT2_BROT] = arc_clampf(q->base_rotate, 0, 1);
        m->mat2[ARC__MAT2_BCON] = q->base_contrast > 0 ? arc_clampf(q->base_contrast, 0.5f, 0.99f) : 0.75f;
    }
    if (q->base_scale > 0) {   /* 0.8.8: the colour texture world-mapped */
        if (m->mat2[ARC__MAT2_BMODE] < 0.5f) m->mat2[ARC__MAT2_BMODE] = 1.0f;
        m->mat2[ARC__MAT2_BSCALE] = q->base_scale;
    }
    m->has_mat = true;
}
/* 0.8.4: the detail and tiling part of a packed material, back as arc_material fields */
static void material_unpack2(const arc__mesh_rec *m, arc_material *out) {
    out->detail.id = m->detail_tex;
    if (m->detail_tex) {
        out->detail_tiling = m->mat2[ARC__MAT2_DMODE] > 1.5f ? ARC_TILE_HEX : ARC_TILE_REPEAT;
        out->detail_scale = m->mat2[ARC__MAT2_DSCALE]; out->detail_strength = m->mat2[ARC__MAT2_DSTR];
        out->detail_rotate = m->mat2[ARC__MAT2_DROT]; out->detail_contrast = m->mat2[ARC__MAT2_DCON];
        if (m->mask_tex) { out->detail_mask.id = m->mask_tex; out->mask_scale = m->mat2[ARC__MAT2_MSCALE]; out->mask_threshold = m->mat2[ARC__MAT2_MTHR]; out->mask_softness = m->mat2[ARC__MAT2_MSOFT]; }
    }
    if (m->mat2[ARC__MAT2_BMODE] > 1.5f) { out->base_tiling = ARC_TILE_HEX; out->base_rotate = m->mat2[ARC__MAT2_BROT]; out->base_contrast = m->mat2[ARC__MAT2_BCON]; }
    out->base_scale = m->mat2[ARC__MAT2_BSCALE];
    if (m->layers_tex && !m->layers_mode) { out->layers.id = m->layers_tex; for (int i = 0; i < 4; i++) out->layer_scale[i] = m->mat3[ARC__MAT3_S0 + i]; out->splat_sharpness = m->mat3[ARC__MAT3_SHARP]; }
}
static void material_pack_splat(arc__mesh_rec *m, const arc_material *q) {   /* 0.9.6 (see ARC__MAT3_*) */
    memset(m->mat3, 0, sizeof m->mat3); m->layers_tex = 0; m->layers_mode = 0;
    uint32_t L = q->layers.id;
    if (!L || L >= ARC__MAX_TEXTURES || !arc__textures[L].used || arc__textures[L].layers <= 0) { if (L) arc_log("arc_material.layers: not a texture array (arc_texture_array)"); return; }
    m->layers_tex = L;
    for (int i = 0; i < 4; i++) m->mat3[ARC__MAT3_S0 + i] = q->layer_scale[i] > 0 ? q->layer_scale[i] : 1.0f;
    m->mat3[ARC__MAT3_SHARP] = arc_clampf(q->splat_sharpness, 0, 1);
    m->mat3[ARC__MAT3_N] = (float)arc_mini(arc__textures[L].layers, 4);   /* (0.9.9: arrays may hold 64; splatting uses 4) */
}
/* 0.9.9: the colour from a texture array at each draw's layer */
void arc_mesh_set_texture_array(arc_mesh mh, arc_texture a) {
    if (!mesh_ok(mh)) return;
    arc__mesh_rec *m = &arc__meshes[mh.id];
    if (!a.id) { if (m->layers_mode) { m->layers_tex = 0; m->layers_mode = 0; } return; }
    if (a.id >= ARC__MAX_TEXTURES || !arc__textures[a.id].used || arc__textures[a.id].layers <= 0) { arc_log("arc_mesh_set_texture_array: not a texture array (arc_texture_array)"); return; }
    memset(m->mat3, 0, sizeof m->mat3); m->layers_tex = a.id; m->layers_mode = 1;
}
void arc_mesh_set_material(arc_mesh mh, const arc_material *q) {
    if (!mesh_ok(mh)) return;
    arc__mesh_rec *m = &arc__meshes[mh.id];
    uint32_t keep[3] = { q && tex_ok(q->normal) ? q->normal.id : 0, q && tex_ok(q->orm) ? q->orm.id : 0, q && tex_ok(q->emissive) ? q->emissive.id : 0 };
    uint8_t owns = 0; for (int i = 0; i < 3; i++) if ((m->mat_owns >> i & 1) && m->mat_tex[i] == keep[i] && keep[i]) owns |= (uint8_t)(1 << i);
    mesh_material_free_owned(m, keep);
    if (m->pending) m->pend_set |= 2;
    if (!q) { m->has_mat = false; memset(m->mat_tex, 0, sizeof m->mat_tex); memset(m->mat, 0, sizeof m->mat); m->detail_tex = 0; m->mask_tex = 0; memset(m->mat2, 0, sizeof m->mat2); m->layers_tex = 0; memset(m->mat3, 0, sizeof m->mat3); return; }
    material_pack(m, q);
    m->mat_owns = owns;                          /* maps the mesh loaded and still uses stay its own */
}
bool arc_mesh_material(arc_mesh mh, arc_material *out) {
    if (!mesh_ok(mh) || !arc__meshes[mh.id].has_mat) { if (out) memset(out, 0, sizeof *out); return false; }
    const arc__mesh_rec *m = &arc__meshes[mh.id];
    if (out) {
        memset(out, 0, sizeof *out);
        out->normal.id = m->mat_tex[0]; out->orm.id = m->mat_tex[1]; out->emissive.id = m->mat_tex[2];
        out->normal_scale = m->mat[ARC__MAT_NSCALE]; out->pbr = m->mat[ARC__MAT_PBR] > 0.5f;
        out->metallic = m->mat[ARC__MAT_METAL]; out->roughness = m->mat[ARC__MAT_ROUGH]; out->occlusion = m->mat[ARC__MAT_OCC];
        for (int i = 0; i < 3; i++) out->emissive_rgb[i] = m->mat[ARC__MAT_ER + i];
        material_unpack2(m, out);
    }
    return true;
}

/* procedural builders: flat-shaded triangles appended to a growable list */
typedef struct { arc_vertex *v; int n, cap; } vbuild;
static void vb_push(vbuild *b, arc_v3 p, arc_v3 n, uint32_t col) {
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 256; b->v = (arc_vertex *)realloc(b->v, sizeof(arc_vertex) * (size_t)b->cap); }
    arc_vertex *v = &b->v[b->n++]; v->px = p.x; v->py = p.y; v->pz = p.z; v->nx = n.x; v->ny = n.y; v->nz = n.z; v->color = col; v->u = v->v = 0;
}
static void vb_tri(vbuild *b, arc_v3 a, arc_v3 c, arc_v3 d, uint32_t col) {
    arc_v3 n = arc_v3_norm(arc_v3_cross(arc_v3_sub(c, a), arc_v3_sub(d, a)));
    vb_push(b, a, n, col); vb_push(b, c, n, col); vb_push(b, d, n, col);
}
/* corners counter-clockwise from the bottom left; the texture spans the quad upright (v = 0 at the top) */
static void vb_quad(vbuild *b, arc_v3 a, arc_v3 c, arc_v3 d, arc_v3 e, uint32_t col) {
    vb_tri(b, a, c, d, col); vb_tri(b, a, d, e, col);
    static const float uv[6][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 1 }, { 1, 0 }, { 0, 0 } };
    for (int i = 0; i < 6; i++) { b->v[b->n - 6 + i].u = uv[i][0]; b->v[b->n - 6 + i].v = uv[i][1]; }
}
static arc_mesh vb_finish(vbuild *b) { arc_mesh m = arc_mesh_create(b->v, b->n, NULL, 0); free(b->v); return m; }

static arc_mesh mesh_cube_impl(uint32_t rgb) {
    vbuild b = { 0 }; uint32_t c = arc_vcol(rgb); float h = 0.5f;
    arc_v3 p[8] = { { -h, -h, -h }, { h, -h, -h }, { h, h, -h }, { -h, h, -h }, { -h, -h, h }, { h, -h, h }, { h, h, h }, { -h, h, h } };
    vb_quad(&b, p[4], p[5], p[6], p[7], c); vb_quad(&b, p[1], p[0], p[3], p[2], c);
    vb_quad(&b, p[5], p[1], p[2], p[6], c); vb_quad(&b, p[0], p[4], p[7], p[3], c);
    vb_quad(&b, p[7], p[6], p[2], p[3], c); vb_quad(&b, p[0], p[1], p[5], p[4], c);
    return vb_finish(&b);
}
static arc_mesh mesh_sphere_impl(uint32_t rgb, int seg) {
    if (seg < 4) seg = 4;
    arc_vertex *v = (arc_vertex *)arc__xalloc(sizeof(arc_vertex) * (size_t)((seg + 1) * (seg + 1)));
    uint32_t *idx = (uint32_t *)arc__xalloc(sizeof(uint32_t) * (size_t)(seg * seg * 6));
    uint32_t c = arc_vcol(rgb); int n = 0, k = 0;
    for (int y = 0; y <= seg; y++)
        for (int x = 0; x <= seg; x++) {
            float th = ARC_PI * (float)y / (float)seg, ph = ARC_TAU * (float)x / (float)seg;
            arc_v3 d = arc_v3_make(sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph));
            v[n].px = d.x * 0.5f; v[n].py = d.y * 0.5f; v[n].pz = d.z * 0.5f; v[n].nx = d.x; v[n].ny = d.y; v[n].nz = d.z; v[n].color = c;
            v[n].u = (float)x / (float)seg; v[n].v = (float)y / (float)seg; n++;
        }
    for (int y = 0; y < seg; y++)
        for (int x = 0; x < seg; x++) {
            uint32_t a = (uint32_t)(y * (seg + 1) + x), b2 = a + (uint32_t)seg + 1;
            idx[k++] = a; idx[k++] = a + 1; idx[k++] = b2; idx[k++] = a + 1; idx[k++] = b2 + 1; idx[k++] = b2;
        }
    arc_mesh m = arc_mesh_create(v, n, idx, k); free(v); free(idx); return m;
}
static arc_mesh lathe(uint32_t rgb, int seg, float r_top) {
    if (seg < 3) seg = 3;
    vbuild b = { 0 }; uint32_t c = arc_vcol(rgb);
    for (int i = 0; i < seg; i++) {
        float a0 = ARC_TAU * (float)i / (float)seg, a1 = ARC_TAU * ((float)(i + 1)) / (float)seg;
        arc_v3 b0 = { 0.5f * cosf(a0), -0.5f, 0.5f * sinf(a0) }, b1 = { 0.5f * cosf(a1), -0.5f, 0.5f * sinf(a1) };
        arc_v3 t0 = { r_top * cosf(a0), 0.5f, r_top * sinf(a0) }, t1 = { r_top * cosf(a1), 0.5f, r_top * sinf(a1) };
        /* smooth side normals */
        float slope = (0.5f - r_top);
        arc_v3 n0 = arc_v3_norm(arc_v3_make(cosf(a0), slope, sinf(a0))), n1 = arc_v3_norm(arc_v3_make(cosf(a1), slope, sinf(a1)));
        vb_push(&b, b0, n0, c); vb_push(&b, t1, n1, c); vb_push(&b, b1, n1, c);
        if (r_top > 0) { vb_push(&b, b0, n0, c); vb_push(&b, t0, n0, c); vb_push(&b, t1, n1, c); }
        else { b.v[b.n - 2].px = 0; b.v[b.n - 2].pz = 0; }
        vb_tri(&b, arc_v3_make(0, -0.5f, 0), b0, b1, c);
        if (r_top > 0) vb_tri(&b, arc_v3_make(0, 0.5f, 0), t1, t0, c);
    }
    return vb_finish(&b);
}
arc_mesh arc_mesh_cylinder(uint32_t rgb, int seg) { arc_mesh m = lathe(rgb, seg, 0.5f); mesh_name_set(m.id, "cylinder"); return m; }
arc_mesh arc_mesh_cone(uint32_t rgb, int seg) { arc_mesh m = lathe(rgb, seg, 0.0f); mesh_name_set(m.id, "cone"); return m; }
arc_mesh arc_mesh_cube(uint32_t rgb) { arc_mesh m = mesh_cube_impl(rgb); mesh_name_set(m.id, "cube"); return m; }
arc_mesh arc_mesh_sphere(uint32_t rgb, int seg) { arc_mesh m = mesh_sphere_impl(rgb, seg); mesh_name_set(m.id, "sphere"); return m; }
static arc_mesh mesh_plane_impl(uint32_t rgb, int tiles, uint32_t rgb2) {
    if (tiles < 1) tiles = 1;
    vbuild b = { 0 }; uint32_t c1 = arc_vcol(rgb), c2 = arc_vcol(rgb2); float s = 1.0f / (float)tiles;
    for (int z = 0; z < tiles; z++)
        for (int x = 0; x < tiles; x++) {
            float x0 = -0.5f + (float)x * s, z0 = -0.5f + (float)z * s;
            vb_quad(&b, arc_v3_make(x0, 0, z0), arc_v3_make(x0, 0, z0 + s), arc_v3_make(x0 + s, 0, z0 + s), arc_v3_make(x0 + s, 0, z0), ((x + z) & 1) ? c2 : c1);
        }
    for (int i = 0; i < b.n; i++) { b.v[i].u = b.v[i].px + 0.5f; b.v[i].v = b.v[i].pz + 0.5f; }   /* one texture across the plane */
    return vb_finish(&b);
}
void arc_mesh_set_double_sided(arc_mesh m, bool on) { if (m.id && m.id < ARC__MAX_MESHES && arc__meshes[m.id].used) { arc__meshes[m.id].double_sided = on; if (arc__meshes[m.id].pending) arc__meshes[m.id].pend_set |= 4; } }
void arc_mesh_bounds(arc_mesh m, arc_v3 *lo, arc_v3 *hi) {
    arc_v3 z = { 0, 0, 0 };
    bool ok = m.id && m.id < ARC__MAX_MESHES && arc__meshes[m.id].used;
    if (lo) *lo = ok ? arc__meshes[m.id].lo : z;
    if (hi) *hi = ok ? arc__meshes[m.id].hi : z;
}
/* Mesh files.
   .arcm v2: "ARCM" u32 version(2) u32 vcount u32 icount u32 flags u32 texture_bytes u32 0 u32 0      (32-byte header)
             then vcount x 36-byte arc_vertex (position, normal, RGBA8 colour, u, v),
             if flags & 1 (skinned): vcount x 8 bytes (4 joint indices, 4 weights out of 255),
             icount indices (u32, or u16 if flags & 4) padded to 4 bytes, then the texture file (PNG, QOI or JPEG).
             flags & 2: double-sided.
             0.6: the header's u32 at byte 24 = material_bytes; that many bytes follow the texture:
             "MATL" u32 1, u32 flags (1 = pbr), f32 normal_scale, metallic, roughness, occlusion, emissive r, g, b,
             u32 normal_bytes, orm_bytes, emissive_bytes, then those images (PNG, QOI or JPEG). Older ARC ignores it.
   .arcm v3: v2 with a 56-byte header (+ f32 lo[3], hi[3]) and 16-byte vertices (see verts_from16).
   .arcm v4 (0.7.1): v3 whose texture and material maps may also be .arct block-compressed images (src/arc_texc.c);
             older ARC refuses it (unknown version) rather than losing its textures.
   .arcm v1: "ARCM" u32 1 u32 vcount u32 icount, vcount x 28-byte vertices (no u, v), u32[icount]
   .axm (AXE): "AXM2" u32 tris, f32 bmin[3], f32 bmax[3], then tris*3 28-byte vertices */
static arc_vertex *verts_from28(const uint8_t *d, int n) {
    arc_vertex *v = (arc_vertex *)arc__xalloc(sizeof(arc_vertex) * (size_t)n);
    for (int i = 0; i < n; i++) { memcpy(&v[i], d + (size_t)i * 28, 28); v[i].u = v[i].v = 0; }
    return v;
}
/* .arcm v3 compact vertices (16 bytes): u16 position in the mesh bounds, octahedral normal in 2 x s8,
   RGBA8 colour, half-float u, v */
static float half_to_float(uint16_t h) {
    uint32_t s = (uint32_t)(h >> 15) << 31, e = (h >> 10) & 31, m = h & 1023, bits;
    if (e == 0) { if (!m) bits = s; else { float f = (float)m / 16777216.0f; return (h >> 15) ? -f : f; } }   /* zero / subnormal */
    else if (e == 31) bits = s | 0x7F800000u | (m << 13);
    else bits = s | ((e + 112) << 23) | (m << 13);
    float f; memcpy(&f, &bits, 4); return f;
}
static arc_vertex *verts_from16(const uint8_t *d, int n, const float lo[3], const float hi[3]) {
    arc_vertex *v = (arc_vertex *)arc__xalloc(sizeof(arc_vertex) * (size_t)n);
    for (int i = 0; i < n; i++) {
        const uint8_t *q = d + (size_t)i * 16; arc_vertex *o = &v[i];
        float *P = &o->px;
        for (int k = 0; k < 3; k++) { uint16_t u = (uint16_t)(q[k * 2] | q[k * 2 + 1] << 8); P[k] = lo[k] + (hi[k] - lo[k]) * ((float)u / 65535.0f); }
        float x = (float)(int8_t)q[6] / 127.0f, y = (float)(int8_t)q[7] / 127.0f, z = 1.0f - fabsf(x) - fabsf(y);
        if (z < 0) { float ox = x; x = (1.0f - fabsf(y)) * (ox >= 0 ? 1.0f : -1.0f); y = (1.0f - fabsf(ox)) * (y >= 0 ? 1.0f : -1.0f); }
        arc_v3 nn = arc_v3_norm(arc_v3_make(x, y, z)); o->nx = nn.x; o->ny = nn.y; o->nz = nn.z;
        memcpy(&o->color, q + 8, 4);
        o->u = half_to_float((uint16_t)(q[12] | q[13] << 8)); o->v = half_to_float((uint16_t)(q[14] | q[15] << 8));
    }
    return v;
}
/* parse an .arcm / .axm into plain data (any thread: no handles, no logging; o->err says what was wrong) */
bool arc__mesh_parse(const uint8_t *d, size_t n, const char *name, arc__mesh_data *o) { return arc__mesh_parse_ex(d, n, name, o, false); }
bool arc__mesh_parse_ex(const uint8_t *d, size_t n, const char *name, arc__mesh_data *o, bool defer) {
    memset(o, 0, sizeof *o);
    if (d && n >= 32 && !memcmp(d, "AXM2", 4)) {
        uint32_t tris; memcpy(&tris, d + 4, 4);
        if (tris == 0 || tris > (1u << 24) || 32 + (size_t)tris * 3 * 28 > n) { snprintf(o->err, sizeof o->err, "truncated mesh %s", name); return false; }
        o->axm = true; o->vc = (int)tris * 3; o->v = verts_from28(d + 32, o->vc);
        return true;
    }
    if (!d || n < 16 || memcmp(d, "ARCM", 4)) { snprintf(o->err, sizeof o->err, "cannot load mesh %s", name); return false; }
    uint32_t ver, vc, ic; memcpy(&ver, d + 4, 4); memcpy(&vc, d + 8, 4); memcpy(&ic, d + 12, 4);
    if (vc == 0 || vc > (1u << 24) || ic > (1u << 26)) { snprintf(o->err, sizeof o->err, "bad mesh %s", name); return false; }
    if (ver == 1) {
        if (16 + (size_t)vc * 28 + (size_t)ic * 4 > n) { snprintf(o->err, sizeof o->err, "truncated mesh %s", name); return false; }
        o->vc = (int)vc; o->ic = (int)ic; o->v = verts_from28(d + 16, (int)vc);
        if (ic) { o->idx = (uint32_t *)arc__xalloc((size_t)ic * 4); memcpy(o->idx, d + 16 + (size_t)vc * 28, (size_t)ic * 4); }
        return true;
    }
    /* version 4 (0.7.1) is version 3 whose texture and material maps may be .arct (block-compressed) images */
    if ((ver != 2 && ver != 3 && ver != 4) || n < 32) { snprintf(o->err, sizeof o->err, "mesh %s has an unknown version %u (this ARC reads 1 to 4)", name, ver); return false; }
    uint32_t flags, tex_bytes, mat_bytes; memcpy(&flags, d + 16, 4); memcpy(&tex_bytes, d + 20, 4); memcpy(&mat_bytes, d + 24, 4);
    size_t at = ver >= 3 ? 56 : 32, vbytes = (size_t)vc * (ver >= 3 ? 16 : 36), sbytes = (flags & 1) ? (size_t)vc * 8 : 0, ibytes = (size_t)ic * ((flags & 4) ? 2 : 4);
    size_t need = at + vbytes + sbytes + ((ibytes + 3) & ~(size_t)3) + tex_bytes;
    if (need > n) { snprintf(o->err, sizeof o->err, "truncated mesh %s", name); return false; }
    o->flags = flags; o->vc = (int)vc; o->ic = (int)ic;
    if (ver >= 3) { float lo[3], hi[3]; memcpy(lo, d + 32, 12); memcpy(hi, d + 44, 12); o->v = verts_from16(d + at, (int)vc, lo, hi); }
    else { o->v = (arc_vertex *)arc__xalloc(sizeof(arc_vertex) * (size_t)vc); memcpy(o->v, d + at, vbytes); }
    at += vbytes;
    if (flags & 1) { o->skin = (uint8_t *)arc__xalloc(sbytes); memcpy(o->skin, d + at, sbytes); at += sbytes; }
    if (ic) {
        o->idx = (uint32_t *)arc__xalloc((size_t)ic * 4);
        if (flags & 4) for (uint32_t i = 0; i < ic; i++) o->idx[i] = (uint32_t)d[at + i * 2] | (uint32_t)d[at + i * 2 + 1] << 8;
        else memcpy(o->idx, d + at, ibytes);
    }
    at += (ibytes + 3) & ~(size_t)3;
    if (tex_bytes) { o->has_tex = true; o->tex_bad = !arc__img_decode_or_defer(d + at, tex_bytes, ARC_TEX_MESH, &o->tex, defer); }
    at += tex_bytes;
    if (mat_bytes >= 52 && at + mat_bytes <= n && !memcmp(d + at, "MATL", 4)) {   /* material: factors and maps */
        const uint8_t *q = d + at; uint32_t mflags, sz[3]; float f[7];
        memcpy(&mflags, q + 8, 4); memcpy(f, q + 12, 28); memcpy(sz, q + 40, 12);
        arc_material *mat = &o->mat; o->has_mat = true;
        mat->pbr = (mflags & 1) != 0; mat->normal_scale = f[0]; mat->metallic = f[1]; mat->roughness = f[2]; mat->occlusion = f[3];
        mat->emissive_rgb[0] = f[4]; mat->emissive_rgb[1] = f[5]; mat->emissive_rgb[2] = f[6];
        size_t off = 52;
        for (int i = 0; i < 3; i++) {
            if (!sz[i] || off + sz[i] > mat_bytes) { off += sz[i]; continue; }
            o->map_bad[i] = !arc__img_decode_or_defer(q + off, sz[i], ARC_TEX_MESH, &o->maps[i], defer);
            off += sz[i];
        }
    }
    at += mat_bytes;
    /* 0.10.15 an optional glow mask after everything else (ARC before 0.10.15 never reads this far): "GLOW", its size, the image */
    if (at + 8 <= n && !memcmp(d + at, "GLOW", 4)) {
        uint32_t gb; memcpy(&gb, d + at + 4, 4);
        if (gb && at + 8 + (size_t)gb <= n) { o->has_glow = true; o->glow_bad = !arc__img_decode_or_defer(d + at + 8, gb, ARC_TEX_MESH, &o->glow, defer); }
    }
    return true;
}
void arc__mesh_data_free(arc__mesh_data *o) {
    free(o->v); free(o->idx); free(o->skin); arc__img_free(&o->tex);
    for (int i = 0; i < 3; i++) arc__img_free(&o->maps[i]);
    arc__img_free(&o->glow);
    memset(o, 0, sizeof *o);
}
/* parsed data -> a mesh (main thread), into slot `id` (0 = a new one); takes the images; returns the mesh id or 0 */
static uint32_t mesh_build(arc__mesh_data *md, const char *name, uint32_t id) {
    if (md->axm) {
        arc_mesh m = mesh_create_at(id, md->v, md->vc, NULL, 0, NULL);
        arc_mesh_set_double_sided(m, true);   /* AXE drew without culling; its baked models rely on that */
        return m.id;
    }
    arc_mesh m = mesh_create_at(id, md->v, md->vc, md->idx, md->ic, (const arc_skin_vertex *)md->skin);
    if (!m.id) return 0;
    if (md->flags & 2) arc_mesh_set_double_sided(m, true);
    if (md->has_tex) {
        uint32_t t = md->tex_bad ? 0 : alloc_tex_id();
        if (md->tex_bad) arc_log("mesh %s: its texture could not be decoded", name);
        else if (t && tex_create_img(t, &md->tex, ARC_TEX_MESH)) { arc__meshes[m.id].texture = t; arc__meshes[m.id].owns_texture = true; }
    }
    if (md->has_mat) {
        arc_material mat = md->mat; arc_texture *maps[3] = { &mat.normal, &mat.orm, &mat.emissive }; uint8_t owns = 0;
        for (int i = 0; i < 3; i++) {
            if (md->map_bad[i]) { arc_log("mesh %s: material map %d could not be decoded", name, i); continue; }
            if (!md->maps[i].rgba && !md->maps[i].comp && !md->maps[i].src) continue;
            uint32_t t = alloc_tex_id();
            if (t && tex_create_img(t, &md->maps[i], ARC_TEX_MESH)) { maps[i]->id = t; owns |= (uint8_t)(1 << i); }
        }
        material_pack(&arc__meshes[m.id], &mat);
        arc__meshes[m.id].mat_owns = owns;
    }
    if (md->has_glow) {   /* 0.10.15 */
        uint32_t t = md->glow_bad ? 0 : alloc_tex_id();
        if (md->glow_bad) arc_log("mesh %s: its glow mask could not be decoded", name);
        else if (t && tex_create_img(t, &md->glow, ARC_TEX_MESH)) { arc__meshes[m.id].glow_tex = t; arc__meshes[m.id].glow_owned = true; }
    }
    return m.id;
}
static arc_mesh mesh_from_memory(const uint8_t *d, size_t n, const char *name) {
    arc__mesh_data md; arc_mesh m = { 0 };
    if (arc__mesh_parse(d, n, name, &md)) { m.id = mesh_build(&md, name, 0); mesh_name_set(m.id, name); }
    else arc_log("%s", md.err);
    arc__mesh_data_free(&md);
    return m;
}
arc_mesh arc_mesh_from_memory(const uint8_t *data, size_t size) { return mesh_from_memory(data, size, "(memory)"); }
/* hot reload: load `path` again into the same handles */
bool arc__mesh_reload(arc_mesh old, const char *path) {
    if (!mesh_ok(old)) return false;
    size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    arc_mesh fresh = d ? mesh_from_memory(d, n, path) : (arc_mesh){ 0 };
    arc_arena_pop(arc_frame(), mark);
    if (!fresh.id) return false;
    arc__mesh_rec *o = &arc__meshes[old.id], *f = &arc__meshes[fresh.id];
    arc__gfx->mesh_destroy(old.id);
    if (!arc__gfx->mesh_create(old.id, f->v, f->vcount, f->idx, f->icount, f->skin)) {
        arc__gfx->mesh_create(old.id, o->v, o->vcount, o->idx, o->icount, o->skin);   /* put the old one back */
        arc_mesh_destroy(fresh); return false;
    }
    bool keep_tex = !o->owns_texture && o->texture && !f->texture;   /* a texture the game set stays */
    uint32_t game_tex = o->texture; bool no_shadow = o->no_shadow, dbl = o->double_sided, unrefl = o->unreflected; float refl = o->reflect;
    if (o->owns_texture && o->texture && o->texture != f->texture) tex_destroy_id(o->texture);
    bool keep_mat = o->has_mat && !o->mat_owns && !f->has_mat;    /* a material the game set (no loaded maps) stays */
    arc__mesh_rec game_mat = *o;
    mesh_material_free_owned(o, f->mat_tex);
    bool keep_glow = !o->glow_owned && o->glow_tex && !f->glow_tex; uint32_t game_glow = o->glow_tex;   /* 0.10.15 a mask the game set stays */
    if (o->glow_owned && o->glow_tex && o->glow_tex != f->glow_tex) tex_destroy_id(o->glow_tex);
    free(o->v); free(o->idx); free(o->skin);
    *o = *f;                                   /* the new data, now under the old id */
    if (keep_glow) { o->glow_tex = game_glow; o->glow_owned = false; }
    o->no_shadow = no_shadow; o->double_sided = o->double_sided || dbl; o->reflect = refl; o->unreflected = unrefl;
    if (keep_tex) { o->texture = game_tex; o->owns_texture = false; }
    if (keep_mat) { o->has_mat = true; o->mat_owns = 0; memcpy(o->mat_tex, game_mat.mat_tex, sizeof o->mat_tex); memcpy(o->mat, game_mat.mat, sizeof o->mat); }
    arc__gfx->mesh_destroy(fresh.id);
    memset(f, 0, sizeof *f);
    return true;
}
static void tex_release(uint32_t id);
bool arc__texture_reload(arc_texture t, const char *path) {
    if (!t.id || t.id >= ARC__MAX_TEXTURES || !arc__textures[t.id].used) return false;
    size_t mark = arc_arena_mark(arc_frame()), n = 0; int w = 0, h = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    arc__tex_rec *r = &arc__textures[t.id];
    if ((d && n >= 4 && !memcmp(d, "ARCT", 4)) || r->fmt != ARC_TEXFMT_RGBA8) {   /* compressed (now or before): remake it */
        arc__img img; bool ok = d && arc__img_decode(d, n, r->flags, &img);
        if (ok) { int flags = r->flags; tex_release(t.id); ok = tex_create_img(t.id, &img, flags); if (!ok) arc__img_free(&img); }
        arc_arena_pop(arc_frame(), mark);
        return ok;
    }
    uint8_t *px = d ? arc_image_decode(d, n, &w, &h) : NULL;
    arc_arena_pop(arc_frame(), mark);
    if (!px) return false;
    if (w == r->w && h == r->h) arc_texture_update(t, px);
    else {                                     /* a new size: rebuild it under the same id (meshes keep using it) */
        int flags = r->flags;
        if (arc__gfx->tex_destroy) arc__gfx->tex_destroy(t.id);
        tex_free_mips(r); free(r->rgba); memset(r, 0, sizeof *r);
        tex_create_id(t.id, w, h, px, flags);
    }
    free(px);
    return true;
}
/* 0.9.0 hints about loading: the same file loaded again and again, and files loaded while the game runs */
static void load_hints(const char *path) {
    if (!path) return;
    static struct { uint32_t h; int n; } seen[256];
    uint32_t h = 2166136261u; for (const char *q = path; *q; q++) h = (h ^ (uint8_t)*q) * 16777619u;
    int i = (int)(h & 255);
    if (seen[i].h != h) { seen[i].h = h; seen[i].n = 0; }
    if (++seen[i].n == 4) arc__hint(path, "%s loaded 4 times: every load makes a new copy in memory. Load it once in init() and keep the handle.", path);
    if ((arc__phase == ARC__PHASE_TICK || arc__phase == ARC__PHASE_DRAW) && !arc__load_queueing())   /* (0.10.9: a queued load is fine) */
        arc__hint("load-in-play", "%s loaded during %s: reading and decoding files stalls that frame. Load in init(), or queue it with arc_load_begin(ARC_LOAD_BACKGROUND).", path, arc__phase == ARC__PHASE_TICK ? "tick()" : "draw()");
}
arc_mesh arc_mesh_load(const char *path) {
    load_hints(path);
    if (arc__load_queueing()) { arc_mesh q = { arc__load_queue(ARC__LOAD_MESH, path, 0) }; mesh_name_set(q.id, path); return q; }
    size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    arc_mesh m = { 0 };
    if (d) m = mesh_from_memory(d, n, path); else arc_log("cannot load mesh %s", path);
    arc_arena_pop(arc_frame(), mark);
    if (m.id) arc__hot_watch(ARC__HOT_MESH, m.id, path);
    return m;
}

arc_mesh arc_mesh_plane(uint32_t rgb, int tiles, uint32_t rgb2) { arc_mesh m = mesh_plane_impl(rgb, tiles, rgb2); mesh_name_set(m.id, "plane"); return m; }
void arc_mesh_set_name(arc_mesh m, const char *name) { if (mesh_ok(m)) { size_t n = name ? strlen(name) : 0; if (n > 31) n = 31; memcpy(g_mname[m.id], name ? name : "", n); g_mname[m.id][n] = 0; } }
const char *arc_mesh_name(arc_mesh m) { return mesh_ok(m) ? g_mname[m.id] : ""; }

/* ======================================================================== */
/* Textures and render targets                                              */
/* ======================================================================== */

arc_texture arc_texture_create_ex(int w, int h, const uint8_t *rgba, int flags) {
    arc_texture none = { 0 };
    if (!R.ready || w <= 0 || h <= 0) return none;
    uint32_t id = alloc_tex_id(); if (!id) { arc_log("texture limit reached (%d)", ARC__MAX_TEXTURES); return none; }
    if (!tex_create_id(id, w, h, rgba, flags)) { tex_free_mips(&arc__textures[id]); free(arc__textures[id].rgba); memset(&arc__textures[id], 0, sizeof(arc__tex_rec)); return none; }
    arc_texture t = { id }; return t;
}
/* 0.9.6 texture arrays (see arc__tex_rec.layers) */
arc_texture arc_texture_array(const arc_texture *src, int n) {
    arc_texture none = { 0 };
    if (!R.ready || !src || n < 1 || n > ARC__MAX_LAYERS) { arc_log("arc_texture_array: 1 to %d layers", ARC__MAX_LAYERS); return none; }
    int w = 0, h = 0;
    for (int i = 0; i < n; i++) {
        uint32_t s = src[i].id;
        if (!s || s >= ARC__MAX_TEXTURES || !arc__textures[s].used || !arc__textures[s].rgba || arc__textures[s].pending || arc__textures[s].target || arc__textures[s].layers) {
            arc_log("arc_texture_array: layer %d is not a plain loaded texture (RGBA, not compressed, loading or a render target)", i); return none; }
        if (i == 0) { w = arc__textures[s].w; h = arc__textures[s].h; }
        else if (arc__textures[s].w != w || arc__textures[s].h != h) { arc_log("arc_texture_array: layer %d is %d x %d, layer 0 %d x %d (they must match)", i, arc__textures[s].w, arc__textures[s].h, w, h); return none; }
    }
    uint32_t id = alloc_tex_id(); if (!id) { arc_log("texture limit reached (%d)", ARC__MAX_TEXTURES); return none; }
    arc__tex_rec *A = &arc__textures[id];
    memset(A, 0, sizeof *A); A->used = true;   /* claimed before the layers take ids */
    uint32_t sub[ARC__MAX_LAYERS] = { 0 };
    for (int i = 0; i < n; i++) {
        uint32_t c = alloc_tex_id();
        if (!c) { for (int k = 0; k < i; k++) { tex_free_mips(&arc__textures[sub[k]]); free(arc__textures[sub[k]].rgba); memset(&arc__textures[sub[k]], 0, sizeof(arc__tex_rec)); }
                  memset(A, 0, sizeof *A); arc_log("texture limit reached (%d)", ARC__MAX_TEXTURES); return none; }
        arc__tex_rec *t = &arc__textures[c];
        t->used = true; t->cpu_only = true; t->w = w; t->h = h; t->smooth = true; t->flags = ARC_TEX_MESH; t->fmt = ARC_TEXFMT_RGBA8;
        t->rgba = (uint8_t *)arc__xalloc((size_t)w * (size_t)h * 4); memcpy(t->rgba, arc__textures[src[i].id].rgba, (size_t)w * (size_t)h * 4);
        tex_build_mips2(t, true);
        sub[i] = c;
    }
    A->w = w; A->h = h; A->smooth = true; A->flags = ARC_TEX_MESH; A->fmt = ARC_TEXFMT_RGBA8; A->layers = n;
    memcpy(A->layer_id, sub, sizeof sub);
    A->gpu_bytes = tex_rgba_bytes(w, h, ARC_TEX_MESH) * (size_t)n;
    if (arc__gfx->tex_create_array) {
        const arc__tex_rec *t0 = &arc__textures[sub[0]]; int lv = t0->levels > 0 ? t0->levels : 1;
        const uint8_t *mp[ARC__MAX_LAYERS * ARC__MAX_MIPS]; int mw[ARC__MAX_MIPS], mh[ARC__MAX_MIPS];
        for (int k = 0; k < lv; k++) { mw[k] = k ? t0->mw[k] : w; mh[k] = k ? t0->mh[k] : h; }
        for (int i = 0; i < n; i++) for (int k = 0; k < lv; k++) mp[i * lv + k] = k ? arc__textures[sub[i]].mip[k] : arc__textures[sub[i]].rgba;
        if (!arc__gfx->tex_create_array(id, w, h, n, lv, mp, mw, mh)) { arc_log("arc_texture_array: the GPU refused it"); arc_texture t = { id }; arc_texture_destroy(t); return none; }
    }
    arc_texture t = { id }; return t;
}
arc_texture arc_texture_create(int w, int h, const uint8_t *rgba, bool smooth) { return arc_texture_create_ex(w, h, rgba, smooth ? ARC_TEX_SMOOTH : 0); }
static bool tex_ok(arc_texture t) { return t.id && t.id < ARC__MAX_TEXTURES && arc__textures[t.id].used; }
void arc_texture_update(arc_texture t, const uint8_t *rgba) {
    if (!tex_ok(t) || arc__textures[t.id].target || arc__textures[t.id].pending) return;
    arc__tex_rec *r = &arc__textures[t.id];
    if (!r->rgba) {                            /* compressed on the GPU: it becomes an RGBA8 texture */
        int w = r->w, h = r->h, flags = r->flags; tex_release(t.id);
        if (!tex_create_id(t.id, w, h, rgba, flags)) { tex_free_mips(r); free(r->rgba); memset(r, 0, sizeof *r); }
        return;
    }
    memcpy(r->rgba, rgba, (size_t)r->w * (size_t)r->h * 4);
    arc__tex_build_mips(r);
    arc__gfx->tex_update(t.id, r->w, r->h, r->rgba);
}
void arc_texture_size(arc_texture t, int *w, int *h) {
    int ww = 0, hh = 0;
    if (tex_ok(t) && !arc__textures[t.id].pending && !(arc__textures[t.id].flags & TEX_FAILED)) { ww = tex_w(t.id); hh = tex_h(t.id); }
    if (w) *w = ww;
    if (h) *h = hh;
}
void arc_texture_set_smooth(arc_texture t, bool smooth) {
    if (!tex_ok(t) || arc__textures[t.id].smooth == smooth) return;
    arc__textures[t.id].smooth = smooth;
    arc__textures[t.id].flags = smooth ? (arc__textures[t.id].flags | ARC_TEX_SMOOTH) : (arc__textures[t.id].flags & ~ARC_TEX_SMOOTH);
    if (arc__gfx->tex_set_smooth) arc__gfx->tex_set_smooth(t.id, smooth);
}
static uint32_t g_dead_tex[ARC__MAX_TEXTURES]; static int g_dead_count;   /* destroyed mid-frame, freed after it renders */
static void tex_free_now(uint32_t id) {
    if (arc__gfx->tex_destroy && !arc__textures[id].cpu_only) arc__gfx->tex_destroy(id);
    for (int i = 1; i < ARC__MAX_MESHES; i++) {        /* meshes stop using it (its id may be reused) */
        if (arc__meshes[i].used && arc__meshes[i].texture == id) { arc__meshes[i].texture = 0; arc__meshes[i].owns_texture = false; }
        if (arc__meshes[i].used && arc__meshes[i].layers_tex == id) { arc__meshes[i].layers_tex = 0; arc__meshes[i].layers_mode = 0; }   /* 0.9.6 */
    }
    for (int k = 0; k < arc__textures[id].layers; k++) {   /* 0.9.6 an array's CPU copies of its layers */
        arc__tex_rec *c = &arc__textures[arc__textures[id].layer_id[k]];
        if (c->used && c->cpu_only) { tex_free_mips(c); free(c->rgba); memset(c, 0, sizeof *c); }
    }
    tex_free_mips(&arc__textures[id]);
    free(arc__textures[id].rgba); memset(&arc__textures[id], 0, sizeof(arc__tex_rec));
}
static void tex_destroy_id(uint32_t id) {
    if (!id || id >= ARC__MAX_TEXTURES || !arc__textures[id].used || id < TEX_FIRST_USER) return;
    g_tname[id][0] = 0;
    arc__textures[id].pending = 0;          /* a queued load for it is dropped */
    for (int i = 0; i < ARC__TPOST_MAX; i++) if (R.tp_id[i] == id) R.tp_id[i] = 0;   /* 0.8.2: its post and cascades */
    R.tcasc[id] = false;
    if (R.in_frame) {                       /* this frame may still draw it: free it once the frame has rendered */
        for (int i = 0; i < g_dead_count; i++) if (g_dead_tex[i] == id) return;
        if (g_dead_count < ARC__MAX_TEXTURES) { g_dead_tex[g_dead_count++] = id; return; }
    }
    tex_free_now(id);
}
void arc_texture_destroy(arc_texture t) { tex_destroy_id(t.id); }
uint8_t *arc_image_decode(const uint8_t *d, size_t n, int *w, int *h) {
    uint8_t *out = NULL;   /* decoded straight to the heap: a big atlas would not fit the frame arena */
    if (d && n > 8 && !memcmp(d, "qoif", 4)) out = arc__qoi_read(d, n, w, h, NULL);
    else if (d && n > 8 && !memcmp(d, "\x89PNG", 4)) out = arc__png_read(d, n, w, h, NULL);
    else if (d && n > 4 && d[0] == 0xFF && d[1] == 0xD8) out = arc__jpeg_read(d, n, w, h);
    else if (d && n >= 32 && !memcmp(d, "ARCT", 4)) {   /* block-compressed: the first level, decoded */
        arc__ctex c; if (arc__ctex_open(d, n, &c)) { out = arc__ctex_decode(&c, 0); if (out) { *w = c.w; *h = c.h; } arc__ctex_free(&c); }
    }
    return out;
}
uint8_t *arc_image_load(const char *path, int *w, int *h) {
    size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n), *out = d ? arc_image_decode(d, n, w, h) : NULL;
    if (!out) arc_log("cannot load image %s", path);
    arc_arena_pop(arc_frame(), mark);
    return out;
}
static bool png_write_n(const char *path, const uint8_t *rgba, int w, int h, int ch);
bool arc_image_save_png(const char *path, const uint8_t *rgba, int w, int h) {   /* 0.8.4: RGBA when any pixel is see-through (was always RGB) */
    if (!rgba || w <= 0 || h <= 0) return false;
    bool alpha = false; for (size_t i = 0, n = (size_t)w * h; i < n && !alpha; i++) alpha = rgba[i * 4 + 3] != 255;
    return png_write_n(path, rgba, w, h, alpha ? 4 : 3);
}
arc_texture arc_texture_load(const char *path) { return arc_texture_load_ex(path, 0); }
static arc_texture tex_from_memory(const uint8_t *d, size_t n, int flags, const char *name) {
    arc_texture none = { 0 }; arc__img img;
    if (!R.ready) return none;
    if (!arc__img_decode(d, n, flags, &img)) { arc_log("cannot load texture %s", name); return none; }
    if (img.w <= 0 || img.h <= 0) { arc__img_free(&img); return none; }
    uint32_t id = alloc_tex_id();
    if (!id) { arc__img_free(&img); arc_log("texture limit reached (%d)", ARC__MAX_TEXTURES); return none; }
    if (!tex_create_img(id, &img, flags)) { arc__img_free(&img); return none; }
    tex_name_set(id, name); arc_texture t = { id }; return t;
}
arc_texture arc_texture_from_memory(const uint8_t *data, size_t size, int flags) { return tex_from_memory(data, size, flags, "(memory)"); }
arc_texture arc_texture_load_ex(const char *path, int flags) {
    load_hints(path);
    if (arc__load_queueing()) { arc_texture q = { arc__load_queue(ARC__LOAD_TEXTURE, path, flags) }; tex_name_set(q.id, path); return q; }
    arc_texture t = { 0 }; size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    if (d) t = tex_from_memory(d, n, flags, path); else arc_log("cannot load texture %s", path);
    arc_arena_pop(arc_frame(), mark);
    if (t.id) arc__hot_watch(ARC__HOT_TEXTURE, t.id, path);
    return t;
}
int  arc_texture_format(arc_texture t) { return tex_ok(t) ? arc__textures[t.id].fmt : ARC_TEXFMT_RGBA8; }
bool arc_texture_format_supported(int fmt) { return fmt != ARC_TEXFMT_RGBA8 && fmt > 0 && fmt < 32 && tex_direct_ok(fmt, 4, 4); }
bool arc_texture_ready(arc_texture t) { return tex_ok(t) && !arc__textures[t.id].pending && !(arc__textures[t.id].flags & TEX_FAILED); }
bool arc_mesh_ready(arc_mesh m) { return mesh_ok(m) && !arc__meshes[m.id].pending && arc__meshes[m.id].vcount > 0; }
/* the GPU side and pixels of a texture, keeping its slot (the meshes using it keep using it) */
static void tex_release(uint32_t id) {
    if (arc__gfx->tex_destroy) arc__gfx->tex_destroy(id);
    arc__tex_rec *r = &arc__textures[id]; uint32_t pend = r->pending;
    tex_free_mips(r); free(r->rgba); memset(r, 0, sizeof *r); r->pending = pend;
}

/* ---- 0.7.1 the loader's reservations: a slot that stands in until the data arrives ---- */
uint32_t arc__tex_reserve(int flags, uint32_t serial) {
    if (!R.ready) return 0;
    uint32_t id = alloc_tex_id(); if (!id) { arc_log("texture limit reached (%d)", ARC__MAX_TEXTURES); return 0; }
    uint8_t clear[4] = { 0, 0, 0, 0 };     /* transparent: draws nothing, and cut out on meshes */
    if (!tex_create_id(id, 1, 1, clear, flags)) { tex_free_mips(&arc__textures[id]); free(arc__textures[id].rgba); memset(&arc__textures[id], 0, sizeof(arc__tex_rec)); return 0; }
    arc__textures[id].pending = serial;
    return id;
}
bool arc__tex_finish(uint32_t id, uint32_t serial, arc__img *img) {
    arc__tex_rec *r = &arc__textures[id];
    if (!r->used || r->pending != serial) return false;            /* destroyed (and maybe reused) meanwhile */
    int flags = r->flags & ~TEX_FAILED;                            /* with any arc_texture_set_smooth made meanwhile */
    tex_release(id); r->pending = 0;
    if (tex_create_img(id, img, flags)) return true;
    uint8_t clear[4] = { 0, 0, 0, 0 };
    tex_create_id(id, 1, 1, clear, flags | TEX_FAILED);
    return false;
}
void arc__tex_fail(uint32_t id, uint32_t serial) {
    arc__tex_rec *r = &arc__textures[id];
    if (r->used && r->pending == serial) { r->pending = 0; r->flags |= TEX_FAILED; }
}
uint32_t arc__mesh_reserve(uint32_t serial) {
    if (!R.ready) return 0;
    uint32_t id = alloc_mesh_id(); if (!id) { arc_log("mesh limit reached (%d)", ARC__MAX_MESHES); return 0; }
    arc__mesh_rec *m = &arc__meshes[id]; memset(m, 0, sizeof *m);
    m->used = true; m->pending = serial;   /* no vertices: draws nothing, no bounds */
    return id;
}
bool arc__mesh_finish(uint32_t id, uint32_t serial, arc__mesh_data *md, const char *name) {
    arc__mesh_rec *r = &arc__meshes[id];
    if (!r->used || r->pending != serial) return false;            /* destroyed (and maybe reused) meanwhile */
    arc__mesh_rec saved = *r;
    memset(r, 0, sizeof *r);
    if (!mesh_build(md, name, id)) { *r = saved; r->pending = 0; r->pend_set = 0; r->used = true; return false; }
    arc_mesh m = { id };                                           /* settings the game made meanwhile win, as if made after */
    if (saved.pend_set & 1) arc_mesh_set_texture(m, (arc_texture){ saved.texture });
    if (saved.pend_set & 2) {
        arc_material q; memset(&q, 0, sizeof q);
        q.normal.id = saved.mat_tex[0]; q.orm.id = saved.mat_tex[1]; q.emissive.id = saved.mat_tex[2];
        q.normal_scale = saved.mat[ARC__MAT_NSCALE]; q.pbr = saved.mat[ARC__MAT_PBR] > 0.5f; q.metallic = saved.mat[ARC__MAT_METAL];
        q.roughness = saved.mat[ARC__MAT_ROUGH]; q.occlusion = saved.mat[ARC__MAT_OCC];
        for (int i = 0; i < 3; i++) q.emissive_rgb[i] = saved.mat[ARC__MAT_ER + i];
        material_unpack2(&saved, &q);
        arc_mesh_set_material(m, saved.has_mat ? &q : NULL);
    }
    if (saved.pend_set & 4) r->double_sided = saved.double_sided;
    if (saved.pend_set & 32) arc_mesh_set_glow_mask(m, (arc_texture){ saved.glow_tex });   /* 0.10.15 */
    if (saved.pend_set & 8) r->no_shadow = saved.no_shadow;
    if (saved.pend_set & 16) { r->reflect = saved.reflect; r->unreflected = saved.unreflected; }
    return true;
}
void arc__mesh_fail(uint32_t id, uint32_t serial) { arc__mesh_rec *r = &arc__meshes[id]; if (r->used && r->pending == serial) r->pending = 0; }
arc_texture arc_target_create(int w, int h, bool smooth) {
    arc_texture none = { 0 };
    if (!R.ready || w <= 0 || h <= 0 || !arc__gfx->target_create) return none;
    uint32_t id = alloc_tex_id(); if (!id) return none;
    arc__tex_rec *t = &arc__textures[id];
    t->used = true; t->target = true; t->w = w; t->h = h; t->smooth = smooth; t->flags = smooth ? ARC_TEX_SMOOTH : 0;
    t->rgba = (uint8_t *)arc__xalloc((size_t)w * (size_t)h * 4);
    t->mip[0] = t->rgba; t->mw[0] = w; t->mh[0] = h; t->levels = 1;
    if (!arc__gfx->target_create(id, w, h, smooth)) { free(t->rgba); memset(t, 0, sizeof *t); return none; }
    arc_texture r = { id }; return r;
}

/* ======================================================================== */
/* Passes: every frame is a list of passes, one per stretch of drawing into one target */
/* ======================================================================== */

static arc__pass *cur_pass(void) { return &R.passes[R.pass_count - 1]; }
/* the light's view of the shadow area: an orthographic box around sh_center, snapped to whole shadow-map texels
   so the shadows don't shimmer as the area follows the camera */
/* one orthographic light box of radius r around c: view-projection and { depth bias, normal offset } */
static void shadow_box(arc_v3 c0, float r, int res, arc_v3 L, float *vp_out, float *bias, float *off) {
    arc_v3 up = fabsf(L.y) > 0.99f ? arc_v3_make(0, 0, 1) : arc_v3_make(0, 1, 0);
    arc_m4 view = arc_m4_look_at(arc_v3_madd(c0, L, 4 * r), c0, up);
    float texel = 2 * r / (float)res;
    arc_v3 c = arc_m4_mul_point(view, c0);
    view.m[12] += roundf(c.x / texel) * texel - c.x; view.m[13] += roundf(c.y / texel) * texel - c.y;
    arc_m4 proj = arc_m4_ortho(-r, r, -r, r, 0.0f, 8 * r);
    arc_m4 vp = arc_m4_mul(proj, view);
    memcpy(vp_out, vp.m, sizeof vp.m);
    *bias = 0.5f * texel / (8 * r);       /* depth bias, in the map's 0..1 depth */
    *off = 1.5f * texel;                  /* normal offset, world units */
}
/* the light's view of the shadow area: an orthographic box around sh_center, snapped to whole shadow-map texels
   so the shadows don't shimmer as the area follows the camera. Cascades: boxes around slices of the camera's
   view (0 - 8% - 30% - 100% of the reach for 3, 0 - 25% - 100% for 2), each the bounding sphere of its slice */
static void shadow_setup(arc__pass *p) {
    memset(p->shadow, 0, sizeof p->shadow); p->shadow_res = 0; p->shadow_n = 1; p->shadow_soft = 0;
    if (R.sh_radius <= 0 || R.sh_strength <= 0) return;
    float r = R.sh_radius; int res = R.sh_res;
    arc_v3 L = arc_v3_make(R.light_dir[0], R.light_dir[1], R.light_dir[2]);
    if (arc_v3_len(L) < 0.5f) return;
    float bias, off;
    shadow_box(R.sh_center, r, res, L, p->shadow_vp, &bias, &off);
    p->shadow[0] = arc_clampf(R.sh_strength, 0, 1);
    p->shadow[1] = bias; p->shadow[2] = off;
    p->shadow[3] = 1.0f / (float)res;
    p->shadow_res = res;
    p->shadow_soft = R.sh_soft;
    memcpy(p->shadow_cvp, p->shadow_vp, sizeof p->shadow_vp);
    p->shadow_cp[0] = bias; p->shadow_cp[1] = off; p->shadow_cp[2] = 0; p->shadow_cp[3] = 0;
    bool tc = p->target && p->target < ARC__MAX_TEXTURES && R.tcasc[p->target];   /* 0.8.2: a target's own camera */
    arc_v3 cfwd = tc ? R.vcam_fwd : R.cam_fwd; float ctx = tc ? R.vcam_tx : R.cam_tx, cty = tc ? R.vcam_ty : R.cam_ty;
    if (R.sh_casc > 1 && (tc ? R.vcam_persp : R.cam_persp && (!p->target || p->vport))) {   /* (0.10.2 viewports: their camera) */
        static const float split2[3] = { 0.25f, 1.0f, 1.0f }, split3[3] = { 0.08f, 0.3f, 1.0f };
        int n = R.sh_casc; const float *sp = n == 2 ? split2 : split3;
        float reach = R.sh_dist > 0 ? R.sh_dist : 4 * r, k2 = ctx * ctx + cty * cty, k = sqrtf(k2), a0 = 0;
        for (int i = 0; i < n; i++) {
            float b0 = reach * sp[i], c = arc_minf(b0, (a0 + b0) * 0.5f * (1.0f + k2));
            float rad = arc_maxf(sqrtf((c - a0) * (c - a0) + a0 * a0 * k2), sqrtf((b0 - c) * (b0 - c) + b0 * b0 * k2));
            rad = ceilf(rad * 16.0f) / 16.0f;   /* steady size: no shimmer from rounding */
            arc_v3 ctr = arc_v3_madd(p->eye, cfwd, c);
            shadow_box(ctr, rad, res, L, p->shadow_cvp + i * 16, &p->shadow_cp[i * 4], &p->shadow_cp[i * 4 + 1]);
            p->shadow_cp[i * 4 + 2] = (float)(i & 1) * 0.5f; p->shadow_cp[i * 4 + 3] = (float)(i >> 1) * 0.5f;
            a0 = b0; (void)k;
        }
        p->shadow_n = n;
        memcpy(p->shadow_vp, p->shadow_cvp, sizeof p->shadow_vp);
        p->shadow[1] = p->shadow_cp[0]; p->shadow[2] = p->shadow_cp[1];
        p->shadow[3] = 0.5f / (float)res;   /* one texel of the 2x2 atlas */
    }
}
static void pass_copy_view(arc__pass *p) {
    p->zrev = arc__depth_mode != ARC_DEPTH_STANDARD; p->view_proj_std = R.view_proj;
    p->view_proj = p->zrev ? R.vp_rev : R.view_proj; p->eye = R.eye; p->time = (float)fmod(arc_time(), 1000.0);
    memcpy(p->light_dir, R.light_dir, 16); memcpy(p->light_col, R.light_col, 16); memcpy(p->ambient, R.ambient, 16);
    memcpy(p->fog_col, R.fog_col, 16); memcpy(p->fog, R.fog, 16); memcpy(p->rim, R.rim, 16);
    p->wmask = R.wmask && R.wmask < ARC__MAX_TEXTURES && arc__textures[R.wmask].used ? R.wmask : 0;
    memcpy(p->wm, R.wm, sizeof p->wm); if (!p->wmask) p->wm[7] = 0;
    memcpy(p->cut, R.cut, sizeof p->cut);
    shadow_setup(p);
    if (!p->target || p->vport) memcpy(p->post, R.post, sizeof p->post);   /* (0.10.2 a viewport: as the screen) */
    else {   /* 0.8.2: a target's own post effects, else none */
        memset(p->post, 0, sizeof p->post);
        for (int i = 0; i < ARC__TPOST_MAX; i++) if (R.tp_id[i] == p->target) { memcpy(p->post, R.tp[i], sizeof p->post); break; }
    }
    if (!p->target || p->vport) memcpy(p->sky, R.sky, sizeof p->sky); else memset(p->sky, 0, sizeof p->sky);
    if (R.env_set) memcpy(p->env, R.env, sizeof p->env);
    else if (R.sky[3] > 0) { memcpy(p->env, R.sky, sizeof p->env); p->env[3] = 1; }
    else {   /* no sky: a dim room of the ambient and light colours, brighter above */
        for (int i = 0; i < 3; i++) {
            p->env[i] = R.ambient[i] + R.light_col[i] * 0.35f;
            p->env[4 + i] = R.ambient[i] + R.light_col[i] * 0.15f;
            p->env[8 + i] = R.ambient[i] * 0.5f;
        }
        p->env[3] = 1;
    }
}
/* 0.9.5 tiled lights (see arc__pass): which of the first n lights can reach each screen tile. 0.10.2: only lights
   added outside viewports (owner 0) and those added inside viewport owner - 1 (owner = its index + 1) */
static void pass_lights(arc__pass *p, int n, int owner) {
    p->nlights = n; p->lt_tiles = -1; p->lt_idx = p->lt_idx_count = 0;
    if (n <= 0 || R.lt_ntiles + ARC__LT_TILES > ARC__MAX_PASSES * ARC__LT_TILES) return;
    static int16_t list[ARC__LT_TILES][ARC__LT_CAP]; static int cnt[ARC__LT_TILES];
    memset(cnt, 0, sizeof cnt);
    const float *m = p->view_proj.m;
    for (int i = 0; i < n; i++) {   /* how much a light matters here (for a full tile: the strongest near the eye stay) */
        const float *L = R.lt[i];
        float r = 1.0f / sqrtf(L[3]), d = arc_v3_dist(arc_v3_make(L[0], L[1], L[2]), p->eye);
        float out = arc_maxf(d - r, 0) / r, peak = arc_maxf(L[4], arc_maxf(L[5], L[6]));
        R.lt_score[i] = peak / (1.0f + out * out);
    }
    bool overflow = false;
    for (int i = 0; i < n; i++) {
        if (R.lt_owner[i] && R.lt_owner[i] != owner) continue;   /* 0.10.2 another viewport's */
        const float *L = R.lt[i]; float r = 1.0f / sqrtf(L[3]);
        /* the screen rectangle of the light's box (all 8 corners in front: their projections; else the whole screen) */
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f; bool behind = false; int outl = 0, outr = 0, outb = 0, outt = 0;
        for (int k = 0; k < 8; k++) {
            float wx = L[0] + (k & 1 ? r : -r), wy = L[1] + (k & 2 ? r : -r), wz = L[2] + (k & 4 ? r : -r);
            float cx = m[0] * wx + m[4] * wy + m[8] * wz + m[12], cy = m[1] * wx + m[5] * wy + m[9] * wz + m[13];
            float cw = m[3] * wx + m[7] * wy + m[11] * wz + m[15];
            if (cw <= 1e-6f) { behind = true; continue; }
            float nx = cx / cw, ny = cy / cw;
            x0 = arc_minf(x0, nx); x1 = arc_maxf(x1, nx); y0 = arc_minf(y0, ny); y1 = arc_maxf(y1, ny);
            outl += nx < -1; outr += nx > 1; outb += ny < -1; outt += ny > 1;
        }
        int tx0, tx1, ty0, ty1;
        if (behind) { tx0 = 0; ty0 = 0; tx1 = ARC__LT_TX - 1; ty1 = ARC__LT_TY - 1; }
        else {
            if (outl == 8 || outr == 8 || outb == 8 || outt == 8) continue;   /* off screen */
            const float e = 1e-3f;
            tx0 = (int)floorf(((x0 - e) * 0.5f + 0.5f) * ARC__LT_TX); tx1 = (int)floorf(((x1 + e) * 0.5f + 0.5f) * ARC__LT_TX);
            ty0 = (int)floorf(((y0 - e) * 0.5f + 0.5f) * ARC__LT_TY); ty1 = (int)floorf(((y1 + e) * 0.5f + 0.5f) * ARC__LT_TY);
            tx0 = arc_maxi(tx0, 0); ty0 = arc_maxi(ty0, 0); tx1 = arc_mini(tx1, ARC__LT_TX - 1); ty1 = arc_mini(ty1, ARC__LT_TY - 1);
        }
        for (int ty = ty0; ty <= ty1; ty++) for (int tx = tx0; tx <= tx1; tx++) {
            int t = ty * ARC__LT_TX + tx;
            if (cnt[t] < ARC__LT_CAP) { list[t][cnt[t]++] = (int16_t)i; continue; }
            overflow = true;   /* full: replace the weakest if this one matters more */
            int w = 0; for (int k = 1; k < ARC__LT_CAP; k++) if (R.lt_score[list[t][k]] < R.lt_score[list[t][w]]) w = k;
            if (R.lt_score[i] > R.lt_score[list[t][w]]) list[t][w] = (int16_t)i;
        }
    }
    if (overflow) { static bool told; if (!told) { arc_log("more than %d lights reach one part of the screen: the strongest are used there", ARC__LT_CAP); told = true; } }
    int total = 0; for (int t = 0; t < ARC__LT_TILES; t++) total += cnt[t];
    if (R.lt_nidx + total > R.lt_cidx) {
        int c = R.lt_cidx ? R.lt_cidx : 16384; while (c < R.lt_nidx + total) c *= 2;
        uint8_t *nb2 = (uint8_t *)realloc(R.lt_idx, (size_t)c); if (!nb2) return;
        R.lt_idx = nb2; R.lt_cidx = c;
    }
    p->lt_tiles = R.lt_ntiles; p->lt_idx = R.lt_nidx;
    int at = 0;
    for (int t = 0; t < ARC__LT_TILES; t++) {
        int c = cnt[t];
        for (int a = 1; a < c; a++) { int16_t v = list[t][a]; int b = a - 1; while (b >= 0 && list[t][b] > v) { list[t][b + 1] = list[t][b]; b--; } list[t][b + 1] = v; }   /* rising order */
        R.lt_tiles[R.lt_ntiles + t] = ((uint32_t)at << 8) | (uint32_t)c;
        for (int k = 0; k < c; k++) R.lt_idx[R.lt_nidx + at + k] = (uint8_t)list[t][k];
        at += c;
    }
    R.lt_ntiles += ARC__LT_TILES; R.lt_nidx += at; p->lt_idx_count = at;
}
/* 0.9.5 a shadowed light's faces (see arc__pass.lsh_n): perspective views from the light, a margin of 2.5 texels
   past the face's edge for the filter */
static void light_shadow(float *L, int slot, bool spot, float outer, float range) {
    if (slot == 0) R.lsh_fres = R.lsh_res > 0 ? R.lsh_res : 512;   /* one size for the frame's atlas */
    int res = R.lsh_fres;
    float zn = arc_maxf(range * 0.02f, 0.01f), m = 1.0f / (1.0f - 5.0f / (float)res);
    float t = (spot ? tanf(arc_minf(outer, 1.25f)) : 1.0f) * m;
    static const float fw[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    arc_v3 p = arc_v3_make(L[0], L[1], L[2]); int nf = spot ? 1 : 6;
    arc_m4 pr = arc_m4_perspective(2.0f * atanf(t), 1, zn, range);
    for (int fi = 0; fi < nf; fi++) {
        arc_v3 F = spot ? arc_v3_make(L[8], L[9], L[10]) : arc_v3_make(fw[fi][0], fw[fi][1], fw[fi][2]), up;
        if (spot) up = fabsf(F.y) < 0.99f ? arc_v3_make(0, 1, 0) : arc_v3_make(1, 0, 0);
        else up = fi == 2 || fi == 3 ? arc_v3_make(0, 0, 1) : arc_v3_make(0, 1, 0);
        arc_m4 vp = arc_m4_mul(pr, arc_m4_look_at(p, arc_v3_add(p, F), up));
        memcpy(R.lsh_vp + (slot * 6 + fi) * 16, vp.m, sizeof vp.m);
    }
    R.lsh_faces[slot] = nf; R.lsh_light[slot] = R.lt_count - 1;
    L[12] = (float)slot; L[13] = zn; L[14] = t; L[15] = 1.0f / (float)res;
}
static void add_light(arc_v3 pos, arc_v3 dir, uint32_t rgb, float intensity, float range, float ci, float co, int flags, float outer) {
    if (!R.in_frame) return;
    if (R.lt_count >= ARC__MAX_FRAME_LIGHTS) { static bool told; if (!told) { arc_log("more than %d lights in one frame", ARC__MAX_FRAME_LIGHTS); told = true; } return; }
    if (!(range > 0) || !(intensity > 0)) return;
    R.lt_owner[R.lt_count] = (uint8_t)(R.vp_cur + 1);
    float *L = R.lt[R.lt_count++], c[4]; arc__unpack_rgb(rgb, c);
    L[0] = pos.x; L[1] = pos.y; L[2] = pos.z; L[3] = 1.0f / (range * range);
    L[4] = c[0] * intensity; L[5] = c[1] * intensity; L[6] = c[2] * intensity; L[7] = ci;
    L[8] = dir.x; L[9] = dir.y; L[10] = dir.z; L[11] = co; L[12] = -1; L[13] = L[14] = L[15] = 0;
    if (flags & ARC_LIGHT_SHADOW) {
        if (R.lsh_count < ARC__MAX_SHADOW_LIGHTS) light_shadow(L, R.lsh_count++, ci > -0.25f, outer, range);
        else { static bool told; if (!told) { arc_log("more than %d shadowed lights in one frame: the rest cast none", ARC__MAX_SHADOW_LIGHTS); told = true; } }
    }
}
void arc_light_point_ex(arc_v3 pos, uint32_t rgb, float intensity, float range, int flags) { add_light(pos, arc_v3_make(0, 0, 0), rgb, intensity, range, -0.5f, -1.0f, flags, 0); }
void arc_light_point(arc_v3 pos, uint32_t rgb, float intensity, float range) { arc_light_point_ex(pos, rgb, intensity, range, 0); }
void arc_light_spot_ex(arc_v3 pos, arc_v3 dir, uint32_t rgb, float intensity, float range, float inner, float outer, int flags) {
    outer = arc_clampf(outer, 0.001f, ARC_PI * 0.5f); inner = arc_clampf(inner, 0, outer);
    float co = cosf(outer), ci = cosf(inner); if (ci < co + 1e-4f) ci = co + 1e-4f;
    float l = arc_v3_len(dir); if (l < 1e-9f) return;
    add_light(pos, arc_v3_scale(dir, 1.0f / l), rgb, intensity, range, ci, co, flags, outer);
}
void arc_light_spot(arc_v3 pos, arc_v3 dir, uint32_t rgb, float intensity, float range, float inner, float outer) { arc_light_spot_ex(pos, dir, rgb, intensity, range, inner, outer, 0); }
void arc_set_light_shadow_resolution(int res) { R.lsh_res = res <= 0 ? 512 : res <= 128 ? 128 : res <= 256 ? 256 : res <= 512 ? 512 : 1024; }
int arc_light_shadow_count(void) { return R.lsh_count; }
int arc_light_count(void) { return R.lt_count; }
arc_texture arc_lut_build(void (*fn)(float rgb[3], void *user), void *user, int size) {
    int n = size == 32 ? 32 : 16, w = n * n;
    uint8_t *px = (uint8_t *)arc__xalloc((size_t)w * n * 4);
    for (int g = 0; g < n; g++) for (int b = 0; b < n; b++) for (int r = 0; r < n; r++) {
        float c[3] = { (float)r / (float)(n - 1), (float)g / (float)(n - 1), (float)b / (float)(n - 1) };
        if (fn) fn(c, user);
        uint8_t *o = px + ((size_t)g * w + (size_t)b * n + r) * 4;
        for (int k = 0; k < 3; k++) o[k] = (uint8_t)(arc_clampf(c[k], 0, 1) * 255.0f + 0.5f);
        o[3] = 255;
    }
    arc_texture t = arc_texture_create_ex(w, n, px, ARC_TEX_SMOOTH); free(px); return t;
}
void arc_set_environment(uint32_t top, uint32_t horizon, uint32_t below, float intensity) {
    if (intensity < 0) R.env_set = false;
    else { arc__unpack_rgb(top, R.env); arc__unpack_rgb(horizon, R.env + 4); arc__unpack_rgb(below, R.env + 8); R.env[3] = intensity; R.env_set = true; }
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
void arc_set_sky_ex(uint32_t top, uint32_t horizon, uint32_t below) {
    arc__unpack_rgb(top, R.sky); R.sky[3] = 1; arc__unpack_rgb(horizon, R.sky + 4); arc__unpack_rgb(below, R.sky + 8);
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
void arc_set_sky(uint32_t top, uint32_t horizon) { arc_set_sky_ex(top, horizon, horizon); }
void arc_sky_off(void) { memset(R.sky, 0, sizeof R.sky); if (R.in_frame && R.pass_count) pass_copy_view(cur_pass()); }
void arc_set_shadows(arc_v3 center, float radius, int resolution) {
    R.sh_center = center; R.sh_radius = radius > 0 ? radius : 0;
    R.sh_res = resolution > 0 ? arc_clampi(resolution, 128, 8192) : 2048;
    if (R.sh_strength <= 0) R.sh_strength = 1;
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
void arc_set_shadow_cascades(int count, float distance) {
    R.sh_casc = count >= 3 ? 3 : count == 2 ? 2 : 1; R.sh_dist = distance > 0 ? distance : 0;
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
void arc_set_shadow_softness(float s) { R.sh_soft = arc_clampf(s, 0, 1); if (R.in_frame && R.pass_count) pass_copy_view(cur_pass()); }
void arc_set_shadow_strength(float s) { R.sh_strength = arc_clampf(s, 0, 1); if (R.in_frame && R.pass_count) pass_copy_view(cur_pass()); }
void arc_mesh_set_reflective(arc_mesh m, float amount) { if (m.id && m.id < ARC__MAX_MESHES && arc__meshes[m.id].used) { arc__meshes[m.id].reflect = arc_clampf(amount, 0, 1); if (arc__meshes[m.id].pending) arc__meshes[m.id].pend_set |= 16; } }
void arc_mesh_set_reflected(arc_mesh m, bool on) { if (m.id && m.id < ARC__MAX_MESHES && arc__meshes[m.id].used) { arc__meshes[m.id].unreflected = !on; if (arc__meshes[m.id].pending) arc__meshes[m.id].pend_set |= 16; } }
void arc_reflection_plane(float y, float strength, float scale) {   /* 0.8.7 */
    R.refl_y = y; R.refl_strength = arc_clampf(strength, 0, 1); R.refl_scale = scale > 0 ? arc_clampf(scale, 0.1f, 1) : 0.5f;
}
void arc_mesh_set_cast_shadow(arc_mesh m, bool on) { if (m.id && m.id < ARC__MAX_MESHES && arc__meshes[m.id].used) { arc__meshes[m.id].no_shadow = !on; if (arc__meshes[m.id].pending) arc__meshes[m.id].pend_set |= 8; } }
static void post_pack(const arc_post *q, float *out) {
    memset(out, 0, sizeof(float) * ARC__POST_COUNT);
    if (q) {
        float ex = q->exposure > 0 ? q->exposure : 1.0f;
        bool lut = q->lut.id && q->lut.id < ARC__MAX_TEXTURES && arc__textures[q->lut.id].used && arc__textures[q->lut.id].w == arc__textures[q->lut.id].h * arc__textures[q->lut.id].h;
        bool on = q->bloom > 0 || q->tonemap || q->vignette > 0 || q->desaturate != 0 || q->contrast != 0 || ex != 1.0f || q->tilt_shift > 0
               || q->ssao > 0 || q->fxaa || lut || q->scanlines > 0 || q->crt_curve > 0 || q->pixelate > 1 || q->aberration > 0;
        out[ARC__POST_ON] = on ? 1.0f : 0.0f;
        out[ARC__POST_BLOOM] = arc_maxf(q->bloom, 0);
        out[ARC__POST_THRESHOLD] = q->bloom_threshold > 0 ? q->bloom_threshold : 1.0f;
        out[ARC__POST_RADIUS] = q->bloom_radius > 0 ? q->bloom_radius : 1.0f;
        out[ARC__POST_EXPOSURE] = ex;
        out[ARC__POST_TONEMAP] = q->tonemap ? 1.0f : 0.0f;
        out[ARC__POST_DESAT] = arc_clampf(q->desaturate, -1, 1);
        out[ARC__POST_CONTRAST] = arc_clampf(q->contrast, -1, 2);
        out[ARC__POST_VIGNETTE] = arc_clampf(q->vignette, 0, 1);
        out[ARC__POST_TILT] = arc_clampf(q->tilt_shift, 0, 1);
        out[ARC__POST_TILT_FOCUS] = q->tilt_focus > 0 ? arc_clampf(q->tilt_focus, 0, 1) : 0.5f;
        out[ARC__POST_TILT_WIDTH] = q->tilt_width > 0 ? arc_clampf(q->tilt_width, 0, 1) : 0.15f;
        out[ARC__POST_SSAO] = arc_clampf(q->ssao, 0, 1);
        out[ARC__POST_SSAO_RADIUS] = q->ssao_radius > 0 ? q->ssao_radius : 0.5f;
        out[ARC__POST_FXAA] = q->fxaa ? 1.0f : 0.0f;
        if (lut) {
            if (!arc__textures[q->lut.id].smooth) arc_texture_set_smooth(q->lut, true);
            out[ARC__POST_LUT] = (float)q->lut.id; out[ARC__POST_LUT_N] = (float)arc__textures[q->lut.id].h;
            out[ARC__POST_LUT_STRENGTH] = q->lut_strength > 0 ? arc_clampf(q->lut_strength, 0, 1) : 1.0f;
        }
        out[ARC__POST_SCANLINES] = arc_clampf(q->scanlines, 0, 1);
        out[ARC__POST_CRT] = arc_clampf(q->crt_curve, 0, 1);
        out[ARC__POST_PIXELATE] = q->pixelate > 1 ? arc_minf(q->pixelate, 64) : 0;
        out[ARC__POST_ABERRATION] = arc_clampf(q->aberration, 0, 1);
    }
}
void arc_set_post(const arc_post *q) {
    post_pack(q, R.post);
    if (R.in_frame) for (int i = 0; i < R.pass_count; i++) if (!R.passes[i].target || R.passes[i].vport) memcpy(R.passes[i].post, R.post, sizeof R.post);
}
/* 0.8.2: post effects of one render target, run when the target's last pass of the frame has drawn its 3D (its 2D
   goes on top afterwards, as on the screen); NULL = none, as before. A target with post comes out opaque. */
bool arc_target_set_post(arc_texture t, const arc_post *q) {
    if (!t.id || t.id >= ARC__MAX_TEXTURES || !arc__textures[t.id].used || !arc__textures[t.id].target) return false;
    int slot = -1, free_slot = -1;
    for (int i = 0; i < ARC__TPOST_MAX; i++) { if (R.tp_id[i] == t.id) slot = i; else if (!R.tp_id[i] && free_slot < 0) free_slot = i; }
    float pk[ARC__POST_COUNT]; post_pack(q, pk);
    if (pk[ARC__POST_ON] <= 0) { if (slot >= 0) R.tp_id[slot] = 0; slot = -1; }
    else {
        if (slot < 0) slot = free_slot;
        if (slot < 0) {
            static bool told; if (!told) { printf("ARC error arc_target_set_post: more than %d targets with post effects\n", ARC__TPOST_MAX); fflush(stdout); told = true; }
            return false;
        }
        R.tp_id[slot] = t.id; memcpy(R.tp[slot], pk, sizeof pk);
    }
    if (R.in_frame) for (int i = 0; i < R.pass_count; i++) if (R.passes[i].target == t.id) {
        if (slot >= 0) memcpy(R.passes[i].post, R.tp[slot], sizeof R.passes[i].post); else memset(R.passes[i].post, 0, sizeof R.passes[i].post);
    }
    return true;
}
void arc_target_set_shadow_cascades(arc_texture t, bool on) {
    if (!t.id || t.id >= ARC__MAX_TEXTURES || !arc__textures[t.id].used || !arc__textures[t.id].target) return;
    R.tcasc[t.id] = on;
    if (R.in_frame && R.pass_count && cur_pass()->target == t.id) pass_copy_view(cur_pass());
}

static void begin_pass(uint32_t target, bool clear, uint32_t argb) {
    if (R.pass_count >= ARC__MAX_PASSES) { over_limit(LIM_PASSES); return; }
    arc__pass *p = &R.passes[R.pass_count++];
    memset(p, 0, sizeof *p);
    p->target = target;
    p->width = target ? arc__textures[target].w : R.w; p->height = target ? arc__textures[target].h : R.h;
    p->clear = clear; arc__unpack_rgb(argb, p->clear_col);
    if (target) p->clear_col[3] = (float)(argb >> 24) / 255.0f;   /* targets: alpha byte 0 = transparent */
    p->vport = target && R.vp_cur >= 0 && target == R.vp_tex[R.vp_cur] ? R.vp_cur + 1 : 0;   /* 0.10.2 */
    pass_copy_view(p);
    p->inst_first = R.raw_count; p->imm_first = R.imm_count; p->line_first = R.line_count; p->b2_first = R.b2_count;
    p->ltri_first = R.ltri_count;
}
static void end_pass_counts(void) {
    if (!R.pass_count) return;
    arc__pass *p = cur_pass();
    pass_lights(p, R.lt_count, R.vp_cur + 1);
    p->lsh_n = 0; while (p->lsh_n < R.lsh_count && R.lsh_light[p->lsh_n] < p->nlights) p->lsh_n++;
    p->inst_count = R.raw_count - p->inst_first; p->imm_count = R.imm_count - p->imm_first;
    p->line_count = R.line_count - p->line_first; p->b2_count = R.b2_count - p->b2_first;
    p->ltri_count = R.ltri_count - p->ltri_first;
}
void arc_target_begin(arc_texture t, bool clear, uint32_t argb) {
    if (!R.in_frame || !tex_ok(t) || !arc__textures[t.id].target || R.target_depth >= 15) return;
    arc__markers_flush();   /* 0.9.9: markers so far belong to the pass that is ending */
    end_pass_counts();
    R.target_stack[++R.target_depth] = t.id;
    begin_pass(t.id, clear, argb);
}
void arc_target_end(void) {
    if (!R.in_frame || R.target_depth <= 0) return;
    arc__markers_flush();
    end_pass_counts();
    R.target_depth--;
    begin_pass(R.target_stack[R.target_depth], false, 0);
}
int arc_target_width(void) { return R.pass_count ? cur_pass()->width : R.w; }
int arc_target_height(void) { return R.pass_count ? cur_pass()->height : R.h; }

static void quad(uint32_t tex, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                 float u0, float v0, float u1, float v1, uint32_t argb);
static bool g_vp_compositing;   /* 0.10.2 the viewport copy is not HUD (v2_alloc) */
/* ---- 0.10.2 viewports: each draws into its own target, a stand-in for the screen (see arc__pass.vport), copied onto
   the screen at arc_viewport_end by a 2D quad, pixel for pixel. The window camera is put back afterwards. ---- */
static void vp_cam_save(void) {
    R.vp_cam.view = R.view; R.vp_cam.proj = R.proj; R.vp_cam.view_proj = R.view_proj; R.vp_cam.inv_view_proj = R.inv_view_proj; R.vp_cam.vp_rev = R.vp_rev;
    R.vp_cam.eye = R.eye; R.vp_cam.view_fwd = R.view_fwd; R.vp_cam.view_persp = R.view_persp;
    R.vp_cam.cam_fwd = R.cam_fwd; R.vp_cam.cam_persp = R.cam_persp; R.vp_cam.cam_tx = R.cam_tx; R.vp_cam.cam_ty = R.cam_ty;
}
static void vp_cam_restore(void) {
    R.view = R.vp_cam.view; R.proj = R.vp_cam.proj; R.view_proj = R.vp_cam.view_proj; R.inv_view_proj = R.vp_cam.inv_view_proj; R.vp_rev = R.vp_cam.vp_rev;
    R.eye = R.vp_cam.eye; R.view_fwd = R.vp_cam.view_fwd; R.view_persp = R.vp_cam.view_persp;
    R.cam_fwd = R.vp_cam.cam_fwd; R.cam_persp = R.vp_cam.cam_persp; R.cam_tx = R.vp_cam.cam_tx; R.cam_ty = R.vp_cam.cam_ty;
    g_cam_version++;
}
bool arc__viewport_cur(int *x, int *y, int *w, int *h) {
    int r[4];
    if (R.in_frame && R.vp_cur >= 0) { const struct vport_rec *v = &R.vps[R.vp_cur]; r[0] = v->x; r[1] = v->y; r[2] = v->w; r[3] = v->h; }
    else if (!R.in_frame && R.ta_on) memcpy(r, R.ta, sizeof r);
    else return false;
    if (x) { *x = r[0]; }
    if (y) { *y = r[1]; }
    if (w) { *w = r[2]; }
    if (h) { *h = r[3]; }
    return true;
}
void arc__viewport_close(void) { if (R.in_frame && R.vp_cur >= 0) arc_viewport_end(); R.ta_on = false; }   /* after tick and draw */
void arc_viewport_begin(int x, int y, int w, int h) {
    if (!R.in_frame) {   /* in tick: UI built here (and arc_width, arc_mouse, ...) works in the viewport's pixels */
        int ww, wh; arc__window_size(&ww, &wh);
        int a0 = arc_maxi(x, 0), b0 = arc_maxi(y, 0), a1 = arc_mini(x + w, ww), b1 = arc_mini(y + h, wh);
        R.ta_on = a1 > a0 && b1 > b0; R.ta[0] = a0; R.ta[1] = b0; R.ta[2] = a1 - a0; R.ta[3] = b1 - b0;
        return;
    }
    if (R.vp_cur >= 0) { static bool told; if (!told) { printf("ARC error arc_viewport_begin: already inside a viewport (call arc_viewport_end first)\n"); fflush(stdout); told = true; } return; }
    if (R.target_depth != 0) { static bool told2; if (!told2) { printf("ARC error arc_viewport_begin: inside a render target (viewports start on the screen)\n"); fflush(stdout); told2 = true; } return; }
    int x0 = arc_maxi(x, 0), y0 = arc_maxi(y, 0), x1 = arc_mini(x + w, R.w), y1 = arc_mini(y + h, R.h);
    if (x1 <= x0 || y1 <= y0) return;
    if (R.vp_count >= ARC_MAX_VIEWPORTS) { static bool told3; if (!told3) { printf("ARC error arc_viewport_begin: more than %d viewports in a frame\n", ARC_MAX_VIEWPORTS); fflush(stdout); told3 = true; } return; }
    int i = R.vp_count, vw = x1 - x0, vh = y1 - y0;
    uint32_t id = R.vp_tex[i];
    if (!(id && arc__textures[id].used && arc__textures[id].w == vw && arc__textures[id].h == vh)) {
        if (id) arc_texture_destroy((arc_texture){ id });
        R.vp_tex[i] = id = arc_target_create(vw, vh, false).id;
        if (!id) return;
    }
    R.vp_used[i] = true;
    struct vport_rec *v = &R.vps[i]; memset(v, 0, sizeof *v);
    v->x = x0; v->y = y0; v->w = vw; v->h = vh; v->pass0 = R.pass_count;
    v->vp = R.view_proj; v->ivp = R.inv_view_proj; v->eye = R.eye; v->fwd = R.view_fwd;   /* until it sets its camera */
    vp_cam_save();
    arc__markers_flush(); end_pass_counts();
    R.vp_count++; R.vp_cur = i;
    R.target_stack[++R.target_depth] = id;
    begin_pass(id, true, R.clear | 0xFF000000u);
}
void arc_viewport_end(void) {
    if (!R.in_frame) { R.ta_on = false; return; }
    if (R.vp_cur < 0) return;
    while (R.target_depth > 1) arc_target_end();   /* targets left open inside it */
    struct vport_rec *v = &R.vps[R.vp_cur];
    arc__markers_flush(); end_pass_counts();
    uint32_t id = R.vp_tex[R.vp_cur];
    R.target_depth = 0; R.vp_cur = -1;
    vp_cam_restore();
    begin_pass(0, false, 0);
    int blend = R.blend; bool clip = R.clip; float sdf = R.sdf2d;   /* the copy: no blend, no clip, every pixel */
    R.blend = ARC__BLEND_NONE; R.clip = false; R.sdf2d = 0; g_vp_compositing = true;
    float fx = (float)v->x, fy = (float)v->y, fw = (float)v->w, fh = (float)v->h;
    quad(id, fx, fy, fx + fw, fy, fx + fw, fy + fh, fx, fy + fh, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
    R.blend = blend; R.clip = clip; R.sdf2d = sdf; g_vp_compositing = false;
}
int arc_viewport_current(void) { return R.in_frame ? R.vp_cur : -1; }
int arc_viewport_count(void) { return R.in_frame ? R.vp_count : R.vpl_count; }
static const struct vport_rec *vp_get(int i) {
    if (R.in_frame) return i >= 0 && i < R.vp_count ? &R.vps[i] : NULL;
    return i >= 0 && i < R.vpl_count ? &R.vpl[i] : NULL;
}
bool arc_viewport_rect(int i, int *x, int *y, int *w, int *h) {
    const struct vport_rec *v = vp_get(i); if (!v) return false;
    if (x) { *x = v->x; }
    if (y) { *y = v->y; }
    if (w) { *w = v->w; }
    if (h) { *h = v->h; }
    return true;
}
int arc_viewport_at(arc_v2 px, arc_v2 *local) {
    int n = arc_viewport_count();
    for (int i = n - 1; i >= 0; i--) {   /* the last drawn is on top */
        const struct vport_rec *v = vp_get(i);
        if (px.x >= (float)v->x && px.y >= (float)v->y && px.x < (float)(v->x + v->w) && px.y < (float)(v->y + v->h)) {
            if (local) { local->x = px.x - (float)v->x; local->y = px.y - (float)v->y; }
            return i;
        }
    }
    return -1;
}
/* ======================================================================== */
/* Camera / light (they apply to the pass being drawn)                      */
/* ======================================================================== */

static void camera_matrices(const arc_camera *c, float vw, float vh, arc_m4 *view, arc_m4 *proj) {
    arc_v3 up = c->up; if (up.x == 0 && up.y == 0 && up.z == 0) up = arc_v3_make(0, 1, 0);
    float zn = c->znear > 0 ? c->znear : 0.1f, zf = c->zfar > zn ? c->zfar : 500.0f;
    *view = arc_m4_look_at(c->eye, c->target, up);
    float aspect = vh > 0 ? vw / vh : 1.0f;
    if (c->fov_y > 0) *proj = arc_m4_perspective(c->fov_y, aspect, zn, zf);
    else { float hh = (c->ortho_height > 0 ? c->ortho_height : 10.0f) * 0.5f; *proj = arc_m4_ortho(-hh * aspect, hh * aspect, -hh, hh, zn, zf); }
}
/* 0.7.2 reversed-Z projection (see arc__pass.zrev): built from the camera itself (not from the standard matrix, whose
   z would lose the precision on the way): perspective z_clip = zn (zf + z) / (zf - zn) (zn with no far plane),
   w = -z; orthographic z_clip = (zf + z) / (zf - zn). Near maps to 1, far to 0. */
static arc_m4 camera_proj_rev(const arc_camera *c, float vw, float vh) {
    float zn = c->znear > 0 ? c->znear : 0.1f, zf = c->zfar > zn ? c->zfar : 500.0f;
    float aspect = vh > 0 ? vw / vh : 1.0f;
    arc_m4 r; memset(&r, 0, sizeof r);
    if (c->fov_y > 0) {
        float f = 1.0f / tanf(c->fov_y * 0.5f);
        r.m[0] = f / aspect; r.m[5] = f; r.m[11] = -1.0f;
        if (arc__depth_mode == ARC_DEPTH_REVERSED_INFINITE) { r.m[10] = 0; r.m[14] = zn; }
        else { r.m[10] = zn / (zf - zn); r.m[14] = zf * zn / (zf - zn); }
    } else {
        float hh = (c->ortho_height > 0 ? c->ortho_height : 10.0f) * 0.5f, l = -hh * aspect, rr = hh * aspect, b = -hh, t = hh;
        r.m[0] = 2.0f / (rr - l); r.m[5] = 2.0f / (t - b); r.m[12] = -(rr + l) / (rr - l); r.m[13] = -(t + b) / (t - b);
        r.m[10] = 1.0f / (zf - zn); r.m[14] = zf / (zf - zn); r.m[15] = 1.0f;
    }
    return r;
}
bool arc__depth_precise;
int  arc_depth_mode(void) { return arc__depth_mode; }
bool arc_depth_precise(void) { return arc__depth_mode != ARC_DEPTH_STANDARD && arc__depth_precise; }
void arc__depth_mode_set(int mode) {
    arc__depth_mode = mode >= ARC_DEPTH_STANDARD && mode <= ARC_DEPTH_REVERSED_INFINITE ? mode : ARC_DEPTH_STANDARD;
    if (arc__gfx == &arc__backend_soft) arc__depth_precise = true;
}
/* camera override (the `camera` command, arc_set_camera_override): replaces the game's screen camera when drawing,
   so screenshot scripts can frame any game the same way. Render targets keep the game's own cameras. */
static struct { bool on; arc_camera c; bool have_game; arc_camera game; } CAMO;
void arc_set_camera_override(const arc_camera *c) { if (c) { CAMO.c = *c; CAMO.on = true; } else CAMO.on = false; }
static void cam_print(const char *what, const arc_camera *c) {
    printf("ARC camera %s eye %g %g %g target %g %g %g fov %g\n", what, c->eye.x, c->eye.y, c->eye.z, c->target.x, c->target.y, c->target.z, c->fov_y * 180.0f / ARC_PI);
    fflush(stdout);
}
void arc__camera_command(int argc, const char **argv) {
    float v[7];
    if (argc >= 2 && !strcmp(argv[1], "off")) { CAMO.on = false; printf("ARC ok camera off\n"); fflush(stdout); return; }
    if (argc == 1) {
        if (CAMO.on) cam_print("override", &CAMO.c);
        if (CAMO.have_game) cam_print("game", &CAMO.game); else if (!CAMO.on) { printf("ARC camera none yet\n"); fflush(stdout); }
        return;
    }
    arc_camera base = CAMO.on ? CAMO.c : CAMO.have_game ? CAMO.game : (arc_camera){ { 0, 2, 6 }, { 0, 0, 0 }, { 0, 1, 0 }, 1.0f, 0, 0.1f, 500.0f };
    if (!strcmp(argv[1], "orbit") && argc >= 5) {   /* camera orbit YAW PITCH DIST [FOV]: around the game camera's target */
        for (int i = 0; i < 3; i++) v[i] = (float)atof(argv[2 + i]);
        arc_v3 t = CAMO.have_game ? CAMO.game.target : base.target;
        float yaw = v[0] * ARC_PI / 180.0f, pitch = v[1] * ARC_PI / 180.0f;
        base.target = t; base.up = arc_v3_make(0, 1, 0);
        base.eye = arc_v3_make(t.x + sinf(yaw) * cosf(pitch) * v[2], t.y + sinf(pitch) * v[2], t.z + cosf(yaw) * cosf(pitch) * v[2]);
        if (argc >= 6) base.fov_y = (float)atof(argv[5]) * ARC_PI / 180.0f;
    } else if (argc >= 7) {                         /* camera EX EY EZ TX TY TZ [FOV] */
        for (int i = 0; i < 6; i++) v[i] = (float)atof(argv[1 + i]);
        base.eye = arc_v3_make(v[0], v[1], v[2]); base.target = arc_v3_make(v[3], v[4], v[5]); base.up = arc_v3_make(0, 1, 0);
        if (argc >= 8) base.fov_y = (float)atof(argv[7]) * ARC_PI / 180.0f;
    } else { printf("ARC error camera: use 'camera', 'camera EX EY EZ TX TY TZ [FOV]', 'camera orbit YAW PITCH DIST [FOV]' or 'camera off'\n"); fflush(stdout); return; }
    CAMO.c = base; CAMO.on = true; cam_print("override", &CAMO.c);
}
void arc__cam_command(int argc, const char **argv) { arc__rig_command(argc, argv, CAMO.have_game ? &CAMO.game : NULL); }   /* 0.10.0 */
void arc_set_camera(const arc_camera *c) {
    if (R.target_depth == 0 && R.in_frame) { CAMO.game = *c; CAMO.have_game = true; c = arc__rig_override(c); if (CAMO.on) c = &CAMO.c; }   /* (0.10.0: `cam rig`) */
    R.eye = c->eye; R.view_fwd = arc_v3_norm(arc_v3_sub(c->target, c->eye)); R.view_persp = c->fov_y > 0;
    camera_matrices(c, (float)arc_target_width(), (float)arc_target_height(), &R.view, &R.proj);
    {   /* 0.8.2: the frustum of the camera set last, for a target's cascades */
        float aspect = arc_target_height() > 0 ? (float)arc_target_width() / (float)arc_target_height() : 1.0f;
        R.vcam_fwd = arc_v3_norm(arc_v3_sub(c->target, c->eye)); R.vcam_persp = c->fov_y > 0;
        R.vcam_ty = tanf(c->fov_y * 0.5f); R.vcam_tx = R.vcam_ty * aspect;
    }
    if (R.target_depth == (R.vp_cur >= 0 ? 1 : 0)) {   /* the screen (or viewport) camera's frustum, for shadow cascades */
        float aspect = arc_target_height() > 0 ? (float)arc_target_width() / (float)arc_target_height() : 1.0f;
        R.cam_fwd = arc_v3_norm(arc_v3_sub(c->target, c->eye)); R.cam_persp = c->fov_y > 0;
        R.cam_ty = tanf(c->fov_y * 0.5f); R.cam_tx = R.cam_ty * aspect;
    }
    R.view_proj = arc_m4_mul(R.proj, R.view);
    R.inv_view_proj = arc_m4_inverse(R.view_proj);
    R.vp_rev = arc_m4_mul(camera_proj_rev(c, (float)arc_target_width(), (float)arc_target_height()), R.view);
    g_cam_version++;
    if (R.in_frame && R.vp_cur >= 0 && R.target_depth == 1) {   /* 0.10.2: the viewport's camera, for picking later */
        struct vport_rec *v = &R.vps[R.vp_cur]; v->vp = R.view_proj; v->ivp = R.inv_view_proj; v->eye = R.eye; v->fwd = R.view_fwd;
    }
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
arc_m4 arc_camera_view_proj(const arc_camera *c, float vw, float vh) { arc_m4 v, p; camera_matrices(c, vw, vh, &v, &p); return arc_m4_mul(p, v); }
static bool to_screen_vp(arc_m4 vp, float vw, float vh, arc_v3 p, arc_v2 *out) {
    arc_v4 c = arc_m4_mul_v4(vp, (arc_v4){ p.x, p.y, p.z, 1.0f });
    if (c.w <= 1e-6f) return false;
    out->x = (c.x / c.w * 0.5f + 0.5f) * vw; out->y = (0.5f - c.y / c.w * 0.5f) * vh;
    return true;
}
static void ray_ivp(arc_m4 ivp, float vw, float vh, arc_v2 px, arc_v3 *origin, arc_v3 *dir) {
    float nx = px.x / vw * 2.0f - 1.0f, ny = 1.0f - px.y / vh * 2.0f;
    arc_v4 a = arc_m4_mul_v4(ivp, (arc_v4){ nx, ny, -1, 1 }), b = arc_m4_mul_v4(ivp, (arc_v4){ nx, ny, 1, 1 });
    arc_v3 pa = arc_v3_make(a.x / a.w, a.y / a.w, a.z / a.w), pb = arc_v3_make(b.x / b.w, b.y / b.w, b.z / b.w);
    *origin = pa; *dir = arc_v3_norm(arc_v3_sub(pb, pa));
}
bool arc_camera_to_screen(const arc_camera *c, float vw, float vh, arc_v3 p, arc_v2 *out) { return to_screen_vp(arc_camera_view_proj(c, vw, vh), vw, vh, p, out); }
void arc_camera_ray(const arc_camera *c, float vw, float vh, arc_v2 px, arc_v3 *origin, arc_v3 *dir) {
    ray_ivp(arc_m4_inverse(arc_camera_view_proj(c, vw, vh)), vw, vh, px, origin, dir);
}
arc_m4 arc_view_proj(void) { return R.view_proj; }
arc_v3 arc__view_eye(void) { return R.eye; }
void arc__cull_info(arc_v3 *to_sun, bool *sun_shadows, bool *mirror) {
    *to_sun = arc_v3_make(R.light_dir[0], R.light_dir[1], R.light_dir[2]);
    *sun_shadows = R.sh_radius > 0 && R.sh_strength > 0; *mirror = R.refl_strength > 0;
}
void arc_set_light(arc_v3 d, uint32_t light, uint32_t ambient) {
    d = arc_v3_norm(d); R.light_dir[0] = d.x; R.light_dir[1] = d.y; R.light_dir[2] = d.z; R.light_dir[3] = 0;
    arc__unpack_rgb(light, R.light_col); arc__unpack_rgb(ambient, R.ambient);
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
/* 0.9.9 fog of war and cutaway (Moonbase Raid's request) */
void arc_set_world_mask(arc_texture mask, arc_v3 origin, float cell, uint32_t dark_rgb, float softness) {
    R.wmask = tex_ok(mask) && cell > 0 && !arc__textures[mask.id].layers ? mask.id : 0;
    R.wm[0] = origin.x; R.wm[1] = origin.z; R.wm[2] = cell > 0 ? 1.0f / cell : 1; R.wm[3] = arc_clampf(softness, 0, 1);
    arc__unpack_rgb(dark_rgb, R.wm + 4); R.wm[7] = R.wmask ? 1.0f : 0.0f;
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
void arc_set_cutaway(arc_v3 eye, arc_v3 focus, float radius, float keep_height, int mode) {
    float c[12] = { eye.x, eye.y, eye.z, radius, focus.x, focus.y, focus.z, keep_height, (float)(radius > 0 ? arc_clampi(mode, 0, 2) : 0), 0.6f, 0.16f, 0 };
    memcpy(R.cut, c, sizeof c);
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
/* 0.10.3 the editor's own 3D view shows the whole grid: no fog of war, no cutaway while it draws (then the game's back) */
static struct { uint32_t wmask; float wm[8], cut[12]; int depth; } g_vfx;
void arc__view_effects_off(bool off) {
    if (off) {
        if (g_vfx.depth++) return;
        g_vfx.wmask = R.wmask; memcpy(g_vfx.wm, R.wm, sizeof R.wm); memcpy(g_vfx.cut, R.cut, sizeof R.cut);
        R.wmask = 0; R.wm[7] = 0; memset(R.cut, 0, sizeof R.cut);
    } else {
        if (!g_vfx.depth || --g_vfx.depth) return;
        R.wmask = g_vfx.wmask; memcpy(R.wm, g_vfx.wm, sizeof R.wm); memcpy(R.cut, g_vfx.cut, sizeof R.cut);
    }
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
void arc_mesh_set_cutaway(arc_mesh m, bool on) { if (mesh_ok(m)) arc__meshes[m.id].cutaway = on; }
void arc_set_fog(uint32_t rgb, float start, float end) {
    arc__unpack_rgb(rgb, R.fog_col);
    R.fog[0] = start; R.fog[1] = end > start ? 1.0f / (end - start) : 0.0f; R.fog[2] = end > start ? 1.0f : 0.0f; R.fog[3] = 0;
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
void arc_set_fog_exp(uint32_t rgb, float density) {
    arc__unpack_rgb(rgb, R.fog_col);
    R.fog[0] = 0; R.fog[1] = 0; R.fog[2] = density > 0 ? 1.0f : 0.0f; R.fog[3] = density > 0 ? density : 0;
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
void arc_set_rim(uint32_t rgb, float power) {
    arc__unpack_rgb(rgb & 0xFFFFFF, R.rim); R.rim[3] = power > 0.1f ? power : 0.1f;
    if (R.in_frame && R.pass_count) pass_copy_view(cur_pass());
}
bool arc_world_to_screen(arc_v3 p, arc_v2 *out) { return to_screen_vp(R.view_proj, (float)arc_target_width(), (float)arc_target_height(), p, out); }
void arc_screen_ray(arc_v2 px, arc_v3 *origin, arc_v3 *dir) {
    if (!R.in_frame && R.vpl_count) {   /* 0.10.2: between frames, through last frame's viewport under px */
        arc_v2 l, wp = px; if (R.ta_on) { wp.x += (float)R.ta[0]; wp.y += (float)R.ta[1]; }   /* (inside a tick-time viewport: its pixels) */
        int i = arc_viewport_at(wp, &l);
        if (i >= 0) { ray_ivp(R.vpl[i].ivp, (float)R.vpl[i].w, (float)R.vpl[i].h, l, origin, dir); return; }
    }
    ray_ivp(R.inv_view_proj, (float)arc_target_width(), (float)arc_target_height(), px, origin, dir);
}
bool arc_viewport_world_to_screen(int i, arc_v3 p, arc_v2 *out) {
    const struct vport_rec *v = vp_get(i); if (!v) return false;
    arc_v2 l; if (!to_screen_vp(v->vp, (float)v->w, (float)v->h, p, &l)) return false;
    if (out) { out->x = l.x + (float)v->x; out->y = l.y + (float)v->y; }
    return true;
}

/* ======================================================================== */
/* Draw lists                                                               */
/* ======================================================================== */

static arc__frame_data g_last_frame; static bool g_last_valid;   /* immediate mode: the last frame built */
void arc__bill_frame(void);
uint64_t arc__gfx_frame;   /* frames begun: caches use it to avoid freeing what this frame already drew with */
void arc__gfx_begin(void) {
    g_last_valid = false; arc__bill_frame(); arc__gfx_frame++;
    R.raw_count = R.line_count = R.v2_count = R.b2_count = R.imm_count = R.pass_count = R.bone_count = R.lt_count = 0; R.ltri_count = 0; arc__post_nan_count = 0;
    g_lines_thick = 0; g_lines_wmax = 0; g_lines_gmax = 0; g_lines2d_frame = 0; R.lt_ntiles = 0; R.lt_nidx = 0; R.lsh_count = 0;
    g_plain_first = -1; g_plain_n = 0; g_offfix_first = -1; g_offfix_n = 0;
    if (g_off_n) { arc__hint("offset2d-open", "arc_push_offset2d was not popped by the end of the frame: the offset is reset each frame"); g_off_n = 0; }
    g_off_x = g_off_y = 0;
    R.line_push = 0; R.npats = 0; R.nglows = 0; R.sdf2d = 0;
    arc__draw_cell = 0; g_culled = g_static_draws = 0; memset(g_lod_n, 0, sizeof g_lod_n);
    R.target_depth = 0; R.target_stack[0] = 0; R.blend = ARC__BLEND_ALPHA; R.clip = false;
    R.vp_cur = -1; R.vp_count = 0;   /* 0.10.2: viewport targets last frame didn't use are let go */
    for (int i = 0; i < ARC_MAX_VIEWPORTS; i++) {
        if (!R.vp_used[i] && R.vp_tex[i]) { arc_texture_destroy((arc_texture){ R.vp_tex[i] }); R.vp_tex[i] = 0; }
        if (!R.vp_used[i] && R.vp_refl[i]) { arc_texture_destroy((arc_texture){ R.vp_refl[i] }); R.vp_refl[i] = 0; }
        R.vp_used[i] = false;
    }
    R.in_frame = true;
    R.w = arc_width(); R.h = arc_height();
    begin_pass(0, true, R.clear);
}
/* one mesh instance. blend: ARC__MB_*; alpha 1 = opaque look; bone_base < 0 = not posed */
static raw_instance *draw_mesh_full(arc_mesh m, arc_m4 model, uint32_t tint, float multiply, float mix, float emissive, float whiten,
                           int blend, float alpha, int bone_base) {
    if (!mesh_ok(m) || !arc__meshes[m.id].vcount) return NULL;   /* vcount 0: still loading (or failed): nothing */
    if (!R.in_frame) { arc__hint("draw-outside", "a mesh was drawn outside draw() (from tick() or init()): nothing shows, because each frame's draw list starts empty. Draw in draw(), or set arc_app.immediate to draw from tick()."); return NULL; }
    if (arc__draw_cell && !scene_cell_drawable(arc__draw_cell)) { g_culled++; return NULL; }   /* 0.7.1 cells */
    if (g_lod_any) m.id = scene_lod_pick(m.id, model.m);                                       /* 0.7.1 LOD */
    if (R.raw_count >= ARC__MAX_INSTANCES) { over_limit(LIM_INSTANCES); return NULL; }
    const float *a = model.m;   /* mirrored (negative determinant) instances are batched separately and wound the other way */
    float det = a[0] * (a[5] * a[10] - a[6] * a[9]) - a[4] * (a[1] * a[10] - a[2] * a[9]) + a[8] * (a[1] * a[6] - a[2] * a[5]);
    raw_instance *r = &R.raw[R.raw_count++];
    r->key = m.id * 2u + (det < 0 ? 1u : 0u);
    r->blend = blend;
    r->depth = blend ? arc_v3_dist(arc_v3_make(a[12], a[13], a[14]), R.eye) : 0;
    r->slot = blend ? SL_TRANS : SL_FAST; r->z = blend ? ARC__Z_NOWRITE : 0; r->order = 0; r->paint = r->paint2 = r->paint3 = 0; r->pat = 0; r->glw = 0;
    memcpy(r->inst.model, model.m, sizeof model.m);
    arc__unpack_rgb(tint, r->inst.tint); r->inst.tint[3] = emissive;
    r->inst.params[0] = arc_clampf(mix, 0, 1); r->inst.params[1] = arc_clampf(whiten, 0, 1); r->inst.params[2] = multiply;
    r->inst.params[3] = blend ? arc_clampf(alpha, 0, 1) : 1.0f;
    r->inst.extra[0] = bone_base >= 0 ? (float)bone_base : 0; r->inst.extra[1] = bone_base >= 0 ? 1.0f : 0;
    r->inst.extra[2] = 0; r->inst.extra[3] = blend == ARC__MB_ADD ? 1.0f : 0.0f;   /* unlit, additive (fog fades it to black) */
    memset(r->inst.fx, 0, sizeof r->inst.fx); memset(r->inst.uvx, 0, sizeof r->inst.uvx);
    return r;
}
/* particles: an unlit see-through instance (lit = colour * (1 + glow)); additive ones fade to black in fog */
void arc__draw_unlit(uint32_t mesh, arc_m4 model, uint32_t tint, float glow, bool additive, float alpha) {
    arc_mesh m = { mesh };
    int before = R.raw_count, cell = arc__draw_cell; arc__draw_cell = 0;   /* effects are never culled by cell */
    draw_mesh_full(m, model, tint, 1, 0, glow, 0, additive ? ARC__MB_ADD : ARC__MB_ALPHA, alpha, -1);
    arc__draw_cell = cell;
    if (R.raw_count > before) { R.raw[before].inst.extra[2] = 1; R.raw[before].inst.extra[3] = additive ? 1.0f : 0.0f; }
}
/* 0.7 particles: flipbook frame (uv offset, as scroll_u/v) and soft fade distance (stored as fx[1] = -soft:
   fx[1] is the dissolve edge width only when fx[0] > 0, so a negative value with no dissolve means soft) */
void arc__draw_unlit_fx(uint32_t mesh, arc_m4 model, uint32_t tint, float glow, bool additive, float alpha, float u, float v, float soft) {
    int before = R.raw_count;
    arc__draw_unlit(mesh, model, tint, glow, additive, alpha);
    if (R.raw_count > before) { float *fx = R.raw[before].inst.fx; fx[4] = u; fx[5] = v; if (soft > 0) fx[1] = -soft; }
}
static void pull_model(float *mm, float f); static void set_order(raw_instance *r, int depth, int layer, int order);
/* 0.9.9 ground markers (arc_marker.c): an unlit see-through batch with a given depth mode, sorted by its centre */
void arc__draw_marker_mesh(uint32_t mesh, int depth, arc_v3 centre) {
    arc_mesh m = { mesh };
    int before = R.raw_count, cell = arc__draw_cell; arc__draw_cell = 0;
    draw_mesh_full(m, arc_m4_identity(), 0xFFFFFF, 1, 0, 0, 0, ARC__MB_ALPHA, 1, -1);
    arc__draw_cell = cell;
    if (R.raw_count > before) {
        raw_instance *r = &R.raw[before];
        r->inst.extra[2] = 1; r->depth = arc_v3_dist(centre, R.eye);
        pull_model(r->inst.model, 0.002f);
        set_order(r, depth, depth == ARC_DEPTH_HIDDEN ? ARC_LAYER_OVERLAY : ARC_LAYER_TRANSPARENT, -1);
    }
}
void arc_draw_mesh_layer(arc_mesh m, arc_m4 model, uint32_t tint, int layer) {
    arc_mesh_style st; memset(&st, 0, sizeof st); st.tint = tint; st.tex_layer = layer;
    arc_draw_mesh_styled(m, model, &st);
}
void arc_draw_tiles(const arc_tile_cell *cells, int w, int h, float cell, arc_v3 origin, const arc_mesh *meshes, int mesh_count) {
    if (!cells || !meshes || w <= 0 || h <= 0 || cell <= 0) return;
    static const float qc[4] = { 1, 0, -1, 0 }, qs[4] = { 0, 1, 0, -1 };   /* exact quarter turns, as arc_m4_rotate_y */
    arc_mesh_style st; memset(&st, 0, sizeof st);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const arc_tile_cell *c = &cells[y * w + x];
        if ((c->flags & ARC_TILE_EMPTY) || c->mesh >= mesh_count) continue;
        int t = c->turn & 3; float sx = (c->flags & ARC_TILE_MIRROR) ? -cell : cell;
        arc_m4 m = arc_m4_identity();
        m.m[0] = qc[t] * sx; m.m[2] = -qs[t] * sx; m.m[8] = qs[t] * cell; m.m[10] = qc[t] * cell;
        m.m[12] = origin.x + ((float)x + 0.5f) * cell; m.m[13] = origin.y; m.m[14] = origin.z + ((float)y + 0.5f) * cell;
        st.tint = c->tint; st.tex_layer = c->layer;
        arc_draw_mesh_styled(meshes[c->mesh], m, &st);
    }
}
int arc__gfx_pass_index(void) { return R.in_frame && R.pass_count ? R.pass_count - 1 : -1; }
arc_v3 arc__camera_eye(void) { return R.eye; }
arc_v3 arc__camera_axis(int i) { return arc_v3_make(R.view.m[i], R.view.m[4 + i], R.view.m[8 + i]); }
void arc_draw_mesh_tinted(arc_mesh m, arc_m4 model, uint32_t tint, float mix, float emissive, float whiten) { draw_mesh_full(m, model, tint, 0, mix, emissive, whiten, 0, 1, -1); }
void arc_draw_mesh_ex(arc_mesh m, arc_m4 model, uint32_t tint, float emissive) { draw_mesh_full(m, model, tint, 1, 0, emissive, 0, 0, 1, -1); }
void arc_draw_mesh(arc_mesh m, arc_m4 model, uint32_t tint) { draw_mesh_full(m, model, tint, 1, 0, 0, 0, 0, 1, -1); }
void arc_draw_mesh_alpha(arc_mesh m, arc_m4 model, uint32_t tint, float alpha) { draw_mesh_full(m, model, tint, 1, 0, 0, 0, ARC__MB_ALPHA, alpha, -1); }
void arc_draw_mesh_additive(arc_mesh m, arc_m4 model, uint32_t tint, float intensity) { draw_mesh_full(m, model, tint, 1, 0, 0, 0, ARC__MB_ADD, intensity, -1); }
/* skinning matrices for this frame (3 rows of 4); returns the first index, or -1 when the frame is full */
int arc__gfx_push_bones(const float *rows, int count) {
    if (count <= 0) return -1;
    if (R.bone_count + count > ARC__MAX_BONES) { over_limit(LIM_BONES); return -1; }
    int base = R.bone_count;
    memcpy(R.bones + (size_t)base * 12, rows, sizeof(float) * 12 * (size_t)count);
    R.bone_count += count;
    return base;
}
int arc__anim_skin_rows(arc_anim a, const arc_pose *p, float *rows);   /* arc_anim.c: pose -> skinning rows; returns the count */
static int pose_bones(arc_mesh m, arc_anim a, const arc_pose *pose) {
    if (!pose || !mesh_ok(m) || !arc__meshes[m.id].skin) return -1;
    static float rows[ARC_MAX_JOINTS * 12];
    int n = arc__anim_skin_rows(a, pose, rows);
    if (n < arc__meshes[m.id].joints) {
        static uint32_t warned; if (warned != m.id) { warned = m.id; arc_log("mesh %u uses %d joints but the skeleton has %d: drawn unposed", m.id, arc__meshes[m.id].joints, n); }
        return -1;
    }
    return n > 0 ? arc__gfx_push_bones(rows, n) : -1;
}
void arc_draw_mesh_posed(arc_mesh m, arc_m4 model, uint32_t tint, arc_anim a, const arc_pose *pose) {
    if (arc__draw_cell && !scene_cell_drawable(arc__draw_cell)) { g_culled++; return; }   /* before its bones are made */
    draw_mesh_full(m, model, tint, 1, 0, 0, 0, 0, 1, pose_bones(m, a, pose));
}
/* depth_pull: the same pixels, nearer the camera (see arc_mesh_style.depth_pull). Perspective: scale about the eye;
   orthographic: p + fwd * f * dot(p - eye, fwd) the other way (as arc_set_line_depth_push), both affine */
static void pull_model(float *mm, float f) {
    arc_m4 m, a = arc_m4_identity(); memcpy(m.m, mm, sizeof m.m);
    if (R.view_persp) {
        float k = 1.0f - f;
        a.m[0] = a.m[5] = a.m[10] = k;
        a.m[12] = R.eye.x * f; a.m[13] = R.eye.y * f; a.m[14] = R.eye.z * f;
    } else {
        arc_v3 d = R.view_fwd; float e = arc_v3_dot(d, R.eye), g = -f;
        float v[3] = { d.x, d.y, d.z };
        for (int c = 0; c < 3; c++) for (int r = 0; r < 3; r++) a.m[c * 4 + r] += g * v[r] * v[c];
        a.m[12] = -g * e * d.x; a.m[13] = -g * e * d.y; a.m[14] = -g * e * d.z;
    }
    m = arc_m4_mul(a, m); memcpy(mm, m.m, sizeof m.m);
}
static void set_order(raw_instance *r, int depth, int layer, int order) {
    int z = r->blend ? ARC__Z_NOWRITE : 0;
    if (depth == ARC_DEPTH_TEST_ONLY) z = ARC__Z_NOWRITE;
    else if (depth == ARC_DEPTH_ON_TOP) z = ARC__Z_ALWAYS | ARC__Z_NOWRITE;
    else if (depth == ARC_DEPTH_HIDDEN) z = ARC__Z_GREATER | ARC__Z_NOWRITE;
    if (layer < ARC_LAYER_BACKGROUND || layer > ARC_LAYER_OVERLAY)
        layer = (depth == ARC_DEPTH_ON_TOP || depth == ARC_DEPTH_HIDDEN) ? ARC_LAYER_OVERLAY : r->blend ? ARC_LAYER_TRANSPARENT : ARC_LAYER_OPAQUE;
    r->z = z; r->order = order;
    r->slot = layer == ARC_LAYER_BACKGROUND ? SL_BG : layer == ARC_LAYER_TRANSPARENT ? SL_TRANS : layer == ARC_LAYER_OVERLAY ? SL_OVER
            : order < 0 ? SL_PRE : (order > 0 || z || r->blend) ? SL_POST : SL_FAST;
}
void arc_draw_mesh_posed_styled(arc_mesh m, arc_m4 model, arc_anim a, const arc_pose *pose, const arc_mesh_style *st) {
    arc_mesh_style s; if (st) s = *st; else memset(&s, 0, sizeof s);
    s.anim = a; s.pose = pose; arc_draw_mesh_styled(m, model, &s);
}
/* 0.10.1 paint patterns: this frame's table (deduplicated), index + 1 */
/* 0.10.6: the game's own joint-name rules (arc_paint_parts_rule), tried before the built-in names, the latest first */
static struct { char pat[48]; int part; } g_part_rule[32];
static int g_npart_rule;
static bool part_glob(const char *p, const char *s) {   /* lower-case p; '*' any run, '?' one character */
    for (; *p; p++, s++) {
        if (*p == '*') { while (p[1] == '*') p++; if (!p[1]) return true; for (; *s; s++) if (part_glob(p + 1, s)) return true; return false; }
        if (!*s || (*p != '?' && *p != *s)) return false;
    }
    return !*s;
}
void arc_paint_parts_rule(const char *pattern, int part) {
    if (!pattern || !*pattern) { g_npart_rule = 0; return; }
    char low[48]; int n = 0;
    for (; pattern[n] && n < 47; n++) low[n] = (char)tolower((unsigned char)pattern[n]);
    low[n] = 0;
    for (int i = 0; i < g_npart_rule; i++) if (!strcmp(g_part_rule[i].pat, low)) {   /* replaced (or removed with 0), and moved last */
        memmove(&g_part_rule[i], &g_part_rule[i + 1], sizeof g_part_rule[0] * (size_t)(g_npart_rule - i - 1)); g_npart_rule--; break;
    }
    part &= ARC_PART_ALL;
    if (!part) return;
    if (g_npart_rule >= 32) { arc__hint("part-rules-full", "arc_paint_parts_rule: more than 32 rules; the oldest is dropped"); memmove(&g_part_rule[0], &g_part_rule[1], sizeof g_part_rule[0] * 31); g_npart_rule = 31; }
    memcpy(g_part_rule[g_npart_rule].pat, low, sizeof low); g_part_rule[g_npart_rule].part = part; g_npart_rule++;
}
static int joint_part(const char *name) {
    char low[64]; int n = 0;
    for (; name && name[n] && n < 63; n++) low[n] = (char)tolower((unsigned char)name[n]);
    low[n] = 0;
    for (int i = g_npart_rule - 1; i >= 0; i--) if (part_glob(g_part_rule[i].pat, low)) return g_part_rule[i].part;
    if (strstr(low, "head")) return ARC_PART_HEAD;
    if (strstr(low, "shoulder") || strstr(low, "clavicle")) return ARC_PART_ARMS;   /* 0.10.6: shoulder pads paint with the arms */
    if (strstr(low, "spine")) return ARC_PART_TORSO;
    if (strstr(low, "arm") || strstr(low, "hand")) return ARC_PART_ARMS;
    if (strstr(low, "leg") || strstr(low, "foot") || strstr(low, "toe")) return ARC_PART_LEGS;
    return ARC_PART_TORSO;
}
int arc__joint_part(const char *name) { return joint_part(name); }
static int pattern_index(const arc_paint_pattern *pp, bool armour, arc_anim anim, uint32_t tint2) {
    arc__pattern q; memset(&q, 0, sizeof q);
    float sc = pp->scale > 0 ? pp->scale : 0.25f, ang = pp->angle;
    q.p[0][0] = (float)pp->kind; q.p[0][1] = 1.0f / sc; q.p[0][2] = pp->width > 0 ? arc_clampf(pp->width, 0, 1) : 0.5f; q.p[0][3] = pp->offset;
    q.p[1][0] = cosf(ang); q.p[1][1] = sinf(ang); q.p[1][2] = pp->cross ? 1.0f : 0.0f; q.p[1][3] = armour ? 1.0f : 0.0f;
    arc__unpack_rgb(pp->rgb ? pp->rgb : (tint2 ? tint2 : 0xFFFFFF), q.p[2]); q.p[2][3] = 0;
    float s = (float)(pp->seed % 1000u);
    q.p[3][0] = s * 0.6180339f + 0.37f; q.p[3][1] = s * 0.1372549f + 0.71f; q.p[3][2] = s * 0.3819660f + 0.13f;
    uint8_t mask = pp->part_mask & ARC_PART_ALL;
    q.p[3][3] = (!mask || (mask & ARC_PART_TORSO)) ? 1.0f : 0.0f;
    for (int j = 0; j < 128; j++) q.jm[j] = 1.0f;
    if (mask) {
        int nj = anim.id ? arc_anim_joint_count(anim) : 0;
        for (int j = 0; j < 128; j++) q.jm[j] = (j < nj ? (joint_part(arc_anim_joint_name(anim, j)) & mask) : (mask & ARC_PART_TORSO)) ? 1.0f : 0.0f;
    }
    for (int i = 0; i < R.npats; i++) if (!memcmp(&R.pats[i], &q, sizeof q)) return i + 1;
    if (R.npats >= ARC__MAX_PATTERNS) { arc__hint("patterns-full", "more than 64 different paint patterns in one frame: the rest draw without one"); return 0; }
    R.pats[R.npats++] = q; return R.npats;
}
/* 0.10.15 glow schemes: this frame's table (deduplicated), index + 1; 0 = no glow (no mask, or every group off) */
static bool glow_tex_ok(uint32_t id) { return id && id < ARC__MAX_TEXTURES && arc__textures[id].used; }
static int glow_index(arc_mesh m, const arc_mesh_style *st) {
    if (!(st->glow[0] > 0 || st->glow[1] > 0 || st->glow[2] > 0)) return 0;
    uint32_t tex = glow_tex_ok(st->emissive_mask.id) ? st->emissive_mask.id
                 : m.id && m.id < ARC__MAX_MESHES && glow_tex_ok(arc__meshes[m.id].glow_tex) ? arc__meshes[m.id].glow_tex : 0;
    if (!tex) return 0;
    arc__glow q; memset(&q, 0, sizeof q); q.tex = tex;
    for (int i = 0; i < 3; i++) {
        float g = st->glow[i] > 0 ? st->glow[i] : 0.0f, c[4];
        arc__unpack_rgb(st->glow_rgb[i] ? st->glow_rgb[i] : 0xFFFFFF, c);
        for (int k = 0; k < 3; k++) q.c[i][k] = c[k] * g;
    }
    for (int i = 0; i < R.nglows; i++) if (!memcmp(&R.glows[i], &q, sizeof q)) return i + 1;
    if (R.nglows >= ARC__MAX_GLOWS) { arc__hint("glows-full", "more than 256 different glow schemes in one frame: the rest draw without their glow"); return 0; }
    R.glows[R.nglows++] = q; return R.nglows;
}
void arc_mesh_set_glow_mask(arc_mesh m, arc_texture mask) {
    if (!m.id || m.id >= ARC__MAX_MESHES || !arc__meshes[m.id].used) return;
    arc__mesh_rec *M = &arc__meshes[m.id];
    if (M->glow_owned && M->glow_tex && M->glow_tex != mask.id) tex_destroy_id(M->glow_tex);
    M->glow_tex = glow_tex_ok(mask.id) ? mask.id : 0; M->glow_owned = false;
    if (M->pending) M->pend_set |= 32;
}
arc_texture arc_mesh_glow_mask(arc_mesh m) {
    if (!m.id || m.id >= ARC__MAX_MESHES || !arc__meshes[m.id].used) return (arc_texture){ 0 };
    return (arc_texture){ arc__meshes[m.id].glow_tex };
}
void arc_draw_mesh_styled(arc_mesh m, arc_m4 model, const arc_mesh_style *st) {
    arc_mesh_style z; if (!st) { memset(&z, 0, sizeof z); st = &z; }
    if (arc__draw_cell && !scene_cell_drawable(arc__draw_cell)) { g_culled++; return; }   /* before its bones are made */
    uint32_t tint = st->tint ? st->tint : 0xFFFFFF;
    int blend = st->additive ? ARC__MB_ADD : st->transparency > 0 ? ARC__MB_ALPHA : ARC__MB_OPAQUE;
    float alpha = st->additive ? 1.0f - arc_clampf(st->transparency, 0, 1) : 1.0f - st->transparency;
    if (st->hologram > 0) { blend = ARC__MB_ADD; alpha = 1; }
    raw_instance *r = draw_mesh_full(m, model, tint, (float)(st->recolour > 0 ? 0 : 1), st->recolour, st->emissive, st->whiten, blend, alpha, pose_bones(m, st->anim, st->pose));
    if (!r) return;
    float *fx = r->inst.fx; double t = arc_time();
    if (st->dissolve > 0) {
        uint32_t e = st->dissolve_rgb ? st->dissolve_rgb : 0xFF8020;
        fx[0] = arc_clampf(st->dissolve, 0, 1.001f); fx[1] = st->dissolve_width > 0 ? st->dissolve_width : 0.04f;
        fx[6] = (float)(((e >> 16) & 0xFF) + ((e >> 8) & 0xFF) * 256 + (e & 0xFF) * 65536); fx[7] = st->dissolve_glow > 0 ? st->dissolve_glow : 4.0f;
    }
    if (st->hologram > 0) fx[2] = arc_clampf(st->hologram, 0, 1);
    if (st->scroll_u != 0) fx[4] = (float)fmod(t * st->scroll_u, 1.0);
    if (st->scroll_v != 0) fx[5] = (float)fmod(t * st->scroll_v, 1.0);
    if (st->uv_rotate != 0 || (st->uv_scale != 0 && st->uv_scale != 1) || st->uv_offset.x != 0 || st->uv_offset.y != 0 || st->uv_mirror) {
        /* 0.8.4: uv' = M (uv - 0.5) + 0.5 + offset, M = s R diag(mirror ? -1 : 1, 1); uvx = M - I, the rest to fx[4..5] */
        float sc = st->uv_scale != 0 ? st->uv_scale : 1.0f, a = sc * cosf(st->uv_rotate), b = sc * sinf(st->uv_rotate), mx = st->uv_mirror ? -1.0f : 1.0f;
        float m00 = a * mx, m01 = -b, m10 = b * mx, m11 = a, *u = r->inst.uvx;
        u[0] = m00 - 1.0f; u[1] = m01; u[2] = m10; u[3] = m11 - 1.0f;
        fx[4] += 0.5f - 0.5f * (m00 + m01) + st->uv_offset.x; fx[5] += 0.5f - 0.5f * (m10 + m11) + st->uv_offset.y;
    }
    {   /* 0.9.9 packed into the instance flags (see arc__instance.extra) */
        int L = arc_clampi(st->tex_layer, 0, ARC__MAX_LAYERS - 1), dk = (int)(arc_clampf(st->darken, 0, 1) * 255.0f + 0.5f), ch = (int)(arc_clampf(st->charred, 0, 1) * 255.0f + 0.5f);
        r->inst.extra[3] += 2.0f * (float)L;
        r->inst.extra[2] += 2.0f * (float)dk + 512.0f * (float)ch + (st->no_world_mask ? 131072.0f : 0.0f);
    }
    if (st->depth_pull != 0) pull_model(r->inst.model, st->depth_pull);
    set_order(r, st->depth, st->layer, st->order);
    if (st->paint_mask.id && st->paint_mask.id < ARC__MAX_TEXTURES && arc__textures[st->paint_mask.id].used) {   /* 0.8.8 */
        r->paint = st->paint_mask.id; r->paint2 = st->tint2 ? st->tint2 & 0xFFFFFF : 0xFFFFFF; r->paint3 = st->tint3 ? (st->tint3 & 0xFFFFFF) | 0x1000000u : 0;   /* bit 24: blue on */
        r->inst.params[2] = 0;   /* the mask decides where the tint goes */
        if (r->slot == SL_FAST) r->slot = SL_PAINT;
    }
    if (st->pattern.kind > ARC_PAINT_NONE && st->pattern.kind <= ARC_PAINT_NOISE) {   /* 0.10.1 paint pattern */
        r->pat = pattern_index(&st->pattern, st->armour_mask && r->paint, st->anim, st->tint2);
        if (r->pat && r->slot == SL_FAST) r->slot = SL_PAINT;
    }
    r->glw = glow_index(m, st);   /* 0.10.15 */
    if (r->glw && r->slot == SL_FAST) r->slot = SL_PAINT;
    if (st->xray > 0 && R.raw_count < ARC__MAX_INSTANCES) {   /* the silhouette: a hologram of it where something nearer hides it */
        raw_instance *x = &R.raw[R.raw_count++]; *x = *r;
        x->blend = ARC__MB_ADD; x->depth = r->depth;
        arc__unpack_rgb(st->xray_rgb ? st->xray_rgb : 0x40E0FF, x->inst.tint); x->inst.tint[3] = 0;
        x->inst.params[0] = 0; x->inst.params[1] = 0; x->inst.params[2] = 1; x->inst.params[3] = 1; x->inst.extra[3] = 1;
        x->inst.fx[2] = arc_clampf(st->xray, 0, 1); x->inst.fx[3] = 0; x->inst.fx[4] = x->inst.fx[5] = 0;
        x->slot = SL_XRAY; x->z = ARC__Z_GREATER | ARC__Z_NOWRITE; x->order = st->order; x->paint = x->paint2 = x->paint3 = 0; x->pat = 0; x->glw = 0;
        if (r->slot < SL_TRANS) r->slot = SL_XOBJ;   /* the solid mesh itself after its silhouette */
    }
    if (st->outline > 0 && !arc__meshes[m.id].double_sided && R.raw_count < ARC__MAX_INSTANCES) {   /* the hull: same mesh, culled the other way */
        raw_instance *o = &R.raw[R.raw_count++]; *o = *r;
        o->key ^= 1u; o->blend = ARC__MB_OPAQUE; o->depth = 0; o->paint = o->paint2 = o->paint3 = 0; o->pat = 0; o->glw = 0;
        set_order(o, st->depth, st->layer, st->order);
        arc__unpack_rgb(st->outline_rgb, o->inst.tint); o->inst.tint[3] = 0;
        o->inst.params[3] = 1; o->inst.extra[3] = 0;
        o->inst.fx[1] = 0; o->inst.fx[2] = 0; o->inst.fx[3] = st->outline; o->inst.fx[7] = 0;
    }
}
void arc_draw_cube(arc_v3 c, arc_v3 s, uint32_t rgb) { arc_draw_mesh(R.cube, arc_m4_trs_y(c, 0, s), rgb); }
void arc_draw_box(arc_v3 lo, arc_v3 hi, uint32_t rgb) {
    arc_draw_mesh(R.cube, arc_m4_trs_y(arc_v3_scale(arc_v3_add(lo, hi), 0.5f), 0, arc_v3_sub(hi, lo)), rgb);
}
void arc_draw_triangles(const arc_vertex *v, int count) {
    if (count <= 0) return;
    if (R.imm_count + count > ARC__MAX_IMM) { over_limit(LIM_IMM); return; }
    memcpy(R.imm + R.imm_count, v, sizeof(arc_vertex) * (size_t)count); R.imm_count += count - count % 3;
}
static inline void imm_vert(arc_v3 p, arc_v3 n, uint32_t c) {
    arc_vertex *v = &R.imm[R.imm_count++]; v->px = p.x; v->py = p.y; v->pz = p.z; v->nx = n.x; v->ny = n.y; v->nz = n.z; v->color = c; v->u = v->v = 0;
}
void arc_draw_tri(arc_v3 a, arc_v3 b, arc_v3 c, uint32_t rgb) {
    if (R.imm_count + 3 > ARC__MAX_IMM) { over_limit(LIM_IMM); return; }
    arc_v3 n = arc_v3_norm(arc_v3_cross(arc_v3_sub(b, a), arc_v3_sub(c, a))); uint32_t col = arc_vcol(rgb);
    imm_vert(a, n, col); imm_vert(b, n, col); imm_vert(c, n, col);
}
void arc_draw_quad3(arc_v3 a, arc_v3 b, arc_v3 c, arc_v3 d, arc_v3 n, uint32_t rgb) {
    if (R.imm_count + 6 > ARC__MAX_IMM) { over_limit(LIM_IMM); return; }
    if (n.x == 0 && n.y == 0 && n.z == 0) n = arc_v3_norm(arc_v3_cross(arc_v3_sub(b, a), arc_v3_sub(c, a)));
    uint32_t col = arc_vcol(rgb);
    imm_vert(a, n, col); imm_vert(b, n, col); imm_vert(c, n, col); imm_vert(a, n, col); imm_vert(c, n, col); imm_vert(d, n, col);
}
void arc_draw_sphere(arc_v3 c, float r, uint32_t rgb) { arc_draw_mesh(R.sphere, arc_m4_trs_y(c, 0, arc_v3_make(r * 2, r * 2, r * 2)), rgb); }

void arc_set_line_depth_push(float fraction) { R.line_push = fraction > 0 ? fraction : 0; }
static arc_v3 line_push(arc_v3 p) {   /* the same pixel, further away (see arc_set_line_depth_push) */
    arc_v3 d = arc_v3_sub(p, R.eye);
    if (R.view_persp) return arc_v3_add(R.eye, arc_v3_scale(d, 1.0f + R.line_push));
    return arc_v3_add(p, arc_v3_scale(R.view_fwd, arc_v3_dot(d, R.view_fwd) * R.line_push));
}
/* ---- 0.10.11 thick and glowing lines (Vector Trader) ----
   A line wider than one pixel is a strip of triangles facing the camera, built here from the current camera: its
   world width at each end is width * (one pixel at that end's depth: 2 w / (target height * proj[1][1])). A one-pixel
   soft edge (colour alpha 255 -> 0) runs round it; square caps reach half the width past each end, round caps are
   half discs, butt caps stop at the end. One shape for 3D and 2D: line_shape() lists its corners as (end, along px,
   across px, alpha) and the caller places them. */
void arc_set_line_width(float pixels) { R.line_width = pixels > 1.0f ? (pixels < 256.0f ? pixels : 256.0f) : 1.0f; }
void arc_set_line_glow(float intensity) { R.line_glow = intensity > 0.0f ? (intensity < 64.0f ? intensity : 64.0f) : 1.0f; }
void arc_set_line_cap(arc_line_cap cap) { R.line_cap = (int)cap >= 0 && (int)cap <= 2 ? (int)cap : 0; R.line_cap2d_set = true; }
float arc_line_width(void) { return R.line_width > 1.0f ? R.line_width : 1.0f; }
typedef struct { int end; float along, across; uint8_t a; } line_pt;
#define LINE_ROUND_SEG 6
static int line_shape(float width, int cap, line_pt *out) {   /* triangle corners, 3 per triangle; returns the count */
    float h = width * 0.5f - 0.5f, f = 1.0f; if (h < 0.25f) h = 0.25f;
    int n = 0;
    #define LP(E, AL, AC, A) (out[n].end = (E), out[n].along = (AL), out[n].across = (AC), out[n].a = (uint8_t)(A), n++)
    float e = cap == ARC_LINE_CAP_SQUARE ? h : 0.0f;
    if (cap != ARC_LINE_CAP_ROUND) {   /* 3 x 3 cells: edge rows and columns fade out */
        float ac[4] = { -(h + f), -h, h, h + f }, al[4] = { -(e + f), -e, e, e + f };
        int en[4] = { 0, 0, 1, 1 };
        for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
            int R0 = r, R1 = r + 1, C0 = c, C1 = c + 1;
            #define ALPHA(RR, CC) (((RR) == 1 || (RR) == 2) && ((CC) == 1 || (CC) == 2) ? 255 : 0)
            LP(en[R0], al[R0], ac[C0], ALPHA(R0, C0)); LP(en[R0], al[R0], ac[C1], ALPHA(R0, C1)); LP(en[R1], al[R1], ac[C1], ALPHA(R1, C1));
            LP(en[R0], al[R0], ac[C0], ALPHA(R0, C0)); LP(en[R1], al[R1], ac[C1], ALPHA(R1, C1)); LP(en[R1], al[R1], ac[C0], ALPHA(R1, C0));
            #undef ALPHA
        }
    } else {
        float ac[4] = { -(h + f), -h, h, h + f };
        for (int c = 0; c < 3; c++) {   /* the body, end to end */
            int a0 = (c == 1 || c == 2) ? 255 : 0, a1 = (c + 1 == 1 || c + 1 == 2) ? 255 : 0;
            LP(0, 0, ac[c], a0); LP(0, 0, ac[c + 1], a1); LP(1, 0, ac[c + 1], a1);
            LP(0, 0, ac[c], a0); LP(1, 0, ac[c + 1], a1); LP(1, 0, ac[c], a0);
        }
        for (int end = 0; end < 2; end++) {   /* half discs: a fan, then a fading ring */
            float sgn = end ? 1.0f : -1.0f;
            for (int k = 0; k < LINE_ROUND_SEG; k++) {
                float t0 = (float)k / LINE_ROUND_SEG * ARC_PI, t1 = (float)(k + 1) / LINE_ROUND_SEG * ARC_PI;
                float s0 = sinf(t0) * sgn, c0 = cosf(t0), s1 = sinf(t1) * sgn, c1 = cosf(t1);
                LP(end, 0, 0, 255); LP(end, s0 * h, c0 * h, 255); LP(end, s1 * h, c1 * h, 255);
                LP(end, s0 * h, c0 * h, 255); LP(end, s0 * (h + f), c0 * (h + f), 0); LP(end, s1 * (h + f), c1 * (h + f), 0);
                LP(end, s0 * h, c0 * h, 255); LP(end, s1 * (h + f), c1 * (h + f), 0); LP(end, s1 * h, c1 * h, 255);
            }
        }
    }
    #undef LP
    return n;
}
#define LINE_SHAPE_MAX (2 * LINE_ROUND_SEG * 9 + 18)
static void line_stats(float w) {
    if (w > g_lines_wmax) g_lines_wmax = w;
    if (R.line_glow > g_lines_gmax) g_lines_gmax = R.line_glow;
}
static void thick_line(arc_v3 a, arc_v3 b, uint32_t c) {
    float wa = arc_m4_mul_v4(R.view_proj, (arc_v4){ a.x, a.y, a.z, 1 }).w, wb = arc_m4_mul_v4(R.view_proj, (arc_v4){ b.x, b.y, b.z, 1 }).w;
    const float eps = 1e-4f;
    if (wa < eps && wb < eps) return;   /* behind the camera */
    if (wa < eps) { float t = (eps - wa) / (wb - wa); a = arc_v3_lerp(a, b, t); wa = eps; }
    if (wb < eps) { float t = (eps - wb) / (wa - wb); b = arc_v3_lerp(b, a, t); wb = eps; }
    arc_v3 d = arc_v3_sub(b, a); float len = arc_v3_len(d); if (len < 1e-12f) return;
    d = arc_v3_scale(d, 1.0f / len);
    float H = (float)arc_target_height(), p11 = R.proj.m[5];
    if (H < 1) H = 1;
    if (fabsf(p11) < 1e-12f) p11 = 1;
    arc_v3 P[2] = { a, b }, side[2]; float px[2] = { 2.0f * wa / (H * fabsf(p11)), 2.0f * wb / (H * fabsf(p11)) };
    for (int k = 0; k < 2; k++) {
        arc_v3 view = R.view_persp ? arc_v3_sub(P[k], R.eye) : R.view_fwd;
        arc_v3 sd = arc_v3_cross(d, view); float l = arc_v3_len(sd);
        if (l < 1e-9f) { sd = arc_v3_cross(d, fabsf(d.y) < 0.9f ? arc_v3_make(0, 1, 0) : arc_v3_make(1, 0, 0)); l = arc_v3_len(sd); }
        side[k] = arc_v3_scale(sd, 1.0f / l);
    }
    line_pt sh[LINE_SHAPE_MAX]; int n = line_shape(R.line_width, R.line_cap, sh);
    if (!R.ltris) R.ltris = (arc__line_vertex *)arc__xalloc(sizeof(arc__line_vertex) * ARC__MAX_LTRIS);
    if (R.ltri_count + n > ARC__MAX_LTRIS) { over_limit(LIM_LINES); return; }
    arc__line_vertex *v = &R.ltris[R.ltri_count]; R.ltri_count += n;
    for (int i = 0; i < n; i++) {
        int e = sh[i].end; float s = px[e];
        arc_v3 q = arc_v3_add(P[e], arc_v3_add(arc_v3_scale(d, sh[i].along * s), arc_v3_scale(side[e], sh[i].across * s)));
        v[i].x = q.x; v[i].y = q.y; v[i].z = q.z; v[i].color = (c & 0x00FFFFFFu) | ((uint32_t)(((c >> 24) * sh[i].a + 127) / 255) << 24); v[i].gain = R.line_glow;
    }
    g_lines_thick++;
}
void arc_draw_line(arc_v3 a, arc_v3 b, uint32_t rgb) {
    if (R.line_push > 0) { a = line_push(a); b = line_push(b); }
    uint32_t c = arc__to_abgr(rgb);
    if (R.line_glow <= 0.0f) R.line_glow = 1.0f;
    line_stats(R.line_width > 1.0f ? R.line_width : 1.0f);
    if (R.line_width > 1.0f) { thick_line(a, b, c); return; }
    if (R.line_count + 2 > ARC__MAX_LINES * 2) { over_limit(LIM_LINES); return; }
    arc__line_vertex *v = &R.lines[R.line_count]; R.line_count += 2;
    v[0].x = a.x; v[0].y = a.y; v[0].z = a.z; v[0].color = c; v[0].gain = R.line_glow;
    v[1].x = b.x; v[1].y = b.y; v[1].z = b.z; v[1].color = c; v[1].gain = R.line_glow;
}
void arc_draw_lines(const arc_v3 *pts, int count, arc_m4 model, uint32_t rgb) {
    if (!pts) return;
    for (int i = 0; i + 1 < count; i += 2) arc_draw_line(arc_m4_mul_point(model, pts[i]), arc_m4_mul_point(model, pts[i + 1]), rgb);
}
void arc_draw_box_lines(arc_v3 lo, arc_v3 hi, uint32_t rgb) {
    arc_v3 p[8]; for (int i = 0; i < 8; i++) p[i] = arc_v3_make(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
    static const int e[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (int i = 0; i < 12; i++) arc_draw_line(p[e[i][0]], p[e[i][1]], rgb);
}
void arc_draw_grid(float half, float step, uint32_t rgb) {
    if (step <= 0) return;
    for (float x = -half; x <= half + 1e-4f; x += step) {
        arc_draw_line(arc_v3_make(x, 0, -half), arc_v3_make(x, 0, half), rgb);
        arc_draw_line(arc_v3_make(-half, 0, x), arc_v3_make(half, 0, x), rgb);
    }
}

/* ---- 2D: triangles batched by texture, blend mode and clip rectangle ---- */
void arc_set_blend(arc_blend b) { R.blend = (int)b; }
arc_blend arc_get_blend(void) { return (arc_blend)R.blend; }
void arc_set_clip(float x, float y, float w, float h) {
    R.clip = true; R.cx = (int)floorf(x); R.cy = (int)floorf(y);
    R.cw = (int)ceilf(x + w) - R.cx; R.ch = (int)ceilf(y + h) - R.cy;
    if (R.cw < 0) R.cw = 0;
    if (R.ch < 0) R.ch = 0;
}
void arc_clear_clip(void) { R.clip = false; }
/* `hud off`: no 2D on the screen (text, HUD, UI, FPS overlay), for clean screenshots. Render targets still get theirs. */
static bool g_hud_hidden;
void arc_set_hud_hidden(bool hidden) { g_hud_hidden = hidden; }
bool arc_hud_hidden(void) { return g_hud_hidden; }
/* 0.10.6 review frames (sheets) draw with ARC's own state, whatever the game set: the HUD shown (their 2D is the sheet
   itself), no post, fog, rim, environment, shadows, reflection, camera override, world mask or cutaway; then the game's back */
static struct { bool on, hud, camo, env_set; float post[ARC__POST_COUNT], fog[4], fog_col[4], rim[4], env[12], sh_radius, refl; } g_rev;
void arc__review_state(bool on) {
    if (on == g_rev.on) return;
    if (on) {
        g_rev.hud = g_hud_hidden; g_rev.camo = CAMO.on; g_rev.env_set = R.env_set;
        memcpy(g_rev.post, R.post, sizeof R.post); memcpy(g_rev.fog, R.fog, sizeof R.fog); memcpy(g_rev.fog_col, R.fog_col, sizeof R.fog_col);
        memcpy(g_rev.rim, R.rim, sizeof R.rim); memcpy(g_rev.env, R.env, sizeof R.env); g_rev.sh_radius = R.sh_radius; g_rev.refl = R.refl_strength;
        g_hud_hidden = false; CAMO.on = false; R.env_set = false; post_pack(NULL, R.post); memset(R.fog, 0, sizeof R.fog);
        R.rim[0] = R.rim[1] = R.rim[2] = 0; R.sh_radius = 0; R.refl_strength = 0;
        arc__view_effects_off(true);
    } else {
        arc__view_effects_off(false);
        g_hud_hidden = g_rev.hud; CAMO.on = g_rev.camo; R.env_set = g_rev.env_set;
        memcpy(R.post, g_rev.post, sizeof R.post); memcpy(R.fog, g_rev.fog, sizeof R.fog); memcpy(R.fog_col, g_rev.fog_col, sizeof R.fog_col);
        memcpy(R.rim, g_rev.rim, sizeof R.rim); memcpy(R.env, g_rev.env, sizeof R.env); R.sh_radius = g_rev.sh_radius; R.refl_strength = g_rev.refl;
    }
    g_rev.on = on;
}
/* the vertices of the last plain (untextured) allocation: the caller fills them in after v2_alloc returns, so their u
   is set to ARC__UV_PLAIN at the next allocation or when the frame ends */
static void plain_fixup(void) {
    for (int i = 0; i < g_plain_n; i++) { R.v2[g_plain_first + i].u = ARC__UV_PLAIN; R.v2[g_plain_first + i].v = 0; }
    g_plain_first = -1; g_plain_n = 0;
    for (int i = 0; i < g_offfix_n; i++) { R.v2[g_offfix_first + i].x += g_offfix_x; R.v2[g_offfix_first + i].y += g_offfix_y; }   /* 0.10.13 */
    g_offfix_first = -1; g_offfix_n = 0;
}
void arc_push_offset2d(float x, float y, float w, float h) {
    plain_fixup();
    if (g_off_n >= 8) { arc__hint("offset2d-deep", "arc_push_offset2d nested more than 8 deep: the extra push is ignored (pop as often as you push)"); g_off_n++; return; }
    g_off_stack[g_off_n].x = g_off_x; g_off_stack[g_off_n].y = g_off_y; g_off_stack[g_off_n].clip = R.clip;
    g_off_stack[g_off_n].cx = R.cx; g_off_stack[g_off_n].cy = R.cy; g_off_stack[g_off_n].cw = R.cw; g_off_stack[g_off_n].ch = R.ch;
    g_off_n++;
    float nx = g_off_x + x, ny = g_off_y + y;
    if (w > 0 && h > 0) {   /* clip to the new area, inside any clip already set */
        int x0 = (int)floorf(nx), y0 = (int)floorf(ny), x1 = (int)ceilf(nx + w), y1 = (int)ceilf(ny + h);
        if (R.clip) { x0 = arc_maxi(x0, R.cx); y0 = arc_maxi(y0, R.cy); x1 = arc_mini(x1, R.cx + R.cw); y1 = arc_mini(y1, R.cy + R.ch); }
        R.clip = true; R.cx = x0; R.cy = y0; R.cw = arc_maxi(0, x1 - x0); R.ch = arc_maxi(0, y1 - y0);
    }
    g_off_x = nx; g_off_y = ny;
}
void arc_pop_offset2d(void) {
    plain_fixup();
    if (g_off_n <= 0) { arc__hint("offset2d-pop", "arc_pop_offset2d without a matching arc_push_offset2d"); return; }
    g_off_n--;
    if (g_off_n >= 8) return;
    g_off_x = g_off_stack[g_off_n].x; g_off_y = g_off_stack[g_off_n].y; R.clip = g_off_stack[g_off_n].clip;
    R.cx = g_off_stack[g_off_n].cx; R.cy = g_off_stack[g_off_n].cy; R.cw = g_off_stack[g_off_n].cw; R.ch = g_off_stack[g_off_n].ch;
}
arc_v2 arc_offset2d(void) { return arc_v2_make(g_off_x, g_off_y); }
/* reserve n vertices in the right batch; NULL when full. A new batch starts when the texture, blend or clip changes,
   except that plain triangles join any batch, and a batch of only plain ones takes the next texture (0.6.5) */
static arc__vert2d *v2_alloc(uint32_t tex, int n) {
    if (!R.in_frame) { arc__hint("draw2d-outside", "2D (text, rects, sprites) was drawn outside draw(): nothing shows. Draw in draw() or draw_top(), or set arc_app.immediate."); return NULL; }
    if (g_hud_hidden && R.target_depth == (R.vp_cur >= 0 ? 1 : 0) && !g_vp_compositing) return NULL;
    plain_fixup();
    if (R.v2_count + n > arc__max_v2) { over_limit(LIM_VERTS2D); return NULL; }
    if (!tex) tex = TEX_WHITE;
    bool plain = tex == TEX_WHITE;
    arc__pass *p = cur_pass();
    arc__batch2d *b = R.b2_count > p->b2_first ? &R.b2[R.b2_count - 1] : NULL;
    bool same_state = b && b->blend == R.blend && b->clip == (int)R.clip && b->sdf == (plain ? b->sdf : R.sdf2d) &&
                      (!R.clip || (b->cx == R.cx && b->cy == R.cy && b->cw == R.cw && b->ch == R.ch));
    if (same_state && !plain && b->texture != tex && b->plain) { b->texture = tex; b->plain = 0; b->sdf = R.sdf2d; }   /* plain so far: take this texture */
    if (!same_state || (!plain && b->texture != tex)) {
        if (R.b2_count >= ARC__MAX_BATCHES) { over_limit(LIM_BATCH2D); return NULL; }
        b = &R.b2[R.b2_count++];
        b->texture = tex; b->first = R.v2_count; b->count = 0; b->blend = R.blend; b->clip = R.clip;
        b->cx = R.cx; b->cy = R.cy; b->cw = R.cw; b->ch = R.ch; b->plain = plain; b->sdf = plain ? 0 : R.sdf2d;
    }
    if (!plain) b->plain = 0;
    b->count += n;
    if (plain) { g_plain_first = R.v2_count; g_plain_n = n; }
    if (g_off_x != 0 || g_off_y != 0) { g_offfix_first = R.v2_count; g_offfix_n = n; g_offfix_x = g_off_x; g_offfix_y = g_off_y; }
    arc__vert2d *v = &R.v2[R.v2_count]; R.v2_count += n; return v;
}
static void quad(uint32_t tex, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                 float u0, float v0, float u1, float v1, uint32_t argb) {
    arc__vert2d *v = v2_alloc(tex, 6); if (!v) return;
    uint32_t c = arc__to_abgr(argb);
    arc__vert2d a = { x0, y0, u0, v0, c }, b = { x1, y1, u1, v0, c }, d = { x2, y2, u1, v1, c }, e = { x3, y3, u0, v1, c };
    v[0] = a; v[1] = b; v[2] = d; v[3] = a; v[4] = d; v[5] = e;
}
void arc_draw_triangles2d(arc_texture t, const arc_vertex2d *src, int count) {
    count -= count % 3; if (count <= 0) return;
    arc__vert2d *v = v2_alloc(tex_ok(t) ? t.id : TEX_WHITE, count); if (!v) return;
    for (int i = 0; i < count; i++) { v[i].x = src[i].x; v[i].y = src[i].y; v[i].u = src[i].u; v[i].v = src[i].v; v[i].color = arc__to_abgr(src[i].color); }
}
static void fr_texture_ex(uint32_t tex, float sx, float sy, float sw, float sh, const float *X, const float *Y, float radians, uint32_t tint);
void arc_draw_texture_ex(arc_texture t, float sx, float sy, float sw, float sh, float dx, float dy, float dw, float dh,
                         float ox, float oy, float radians, uint32_t tint) {
    uint32_t id = tex_ok(t) ? t.id : TEX_WHITE;
    float tw = (float)tex_w(id), th = (float)tex_h(id);
    float u0 = sx / tw, v0 = sy / th, u1 = (sx + sw) / tw, v1 = (sy + sh) / th;   /* negative sw/sh flip */
    float c = cosf(radians), s = sinf(radians);
    float px[4] = { -ox, dw - ox, dw - ox, -ox }, py[4] = { -oy, -oy, dh - oy, dh - oy }, X[4], Y[4];
    for (int i = 0; i < 4; i++) { X[i] = dx + px[i] * c - py[i] * s; Y[i] = dy + px[i] * s + py[i] * c; }
    fr_texture_ex(tex_ok(t) ? t.id : 0, sx, sy, sw, sh, X, Y, radians, tint);   /* 0.10.7 */
    quad(id, X[0], Y[0], X[1], Y[1], X[2], Y[2], X[3], Y[3], u0, v0, u1, v1, tint);
}

/* ---- 0.9.0 frame description: 2D recorded while on (the `frame` command turns it on for one render) ---- */
typedef struct { float x, y, w, h; uint32_t rgb; int off; uint32_t tgt; float scale; } frec_text;
typedef struct { float x, y, w, h; uint32_t rgb, tex, tgt; } frec_quad;
/* 0.10.7 (Moonbase Raid): a render target drawn somewhere (the screen or another target): its source rectangle in the
   target's pixels and where that lands, so things drawn into the target can be placed on the screen */
typedef struct { uint32_t tex, into; float sx, sy, sw, sh, x0, y0, x1, y1; } frec_blit;
static struct { bool on; int depth; int nt, ns, nr, nchars, nb; frec_text t[512]; frec_quad sp[256], rc[256]; frec_blit bl[64]; char chars[32768]; } g_fr;
bool arc__frame_recording(void) { return g_fr.on; }   /* 0.9.9 */
/* the render target 2D and markers go into now, for `frame` (0: the screen, or a viewport, which has its own list) */
uint32_t arc__frame_target(void) { return (R.target_depth > 0 && R.vp_cur < 0) ? R.target_stack[R.target_depth] : 0; }
static void fr_blit(uint32_t tex, float sx, float sy, float sw, float sh, const float *X, const float *Y) {
    if (!g_fr.on || g_fr.depth || !tex || tex >= ARC__MAX_TEXTURES || !arc__textures[tex].target || g_fr.nb >= 64 || sw == 0 || sh == 0) return;
    frec_blit *b = &g_fr.bl[g_fr.nb++]; b->tex = tex; b->into = arc__frame_target();
    b->sx = sx; b->sy = sy; b->sw = sw; b->sh = sh;
    b->x0 = arc_minf(arc_minf(X[0], X[1]), arc_minf(X[2], X[3])); b->x1 = arc_maxf(arc_maxf(X[0], X[1]), arc_maxf(X[2], X[3]));
    b->y0 = arc_minf(arc_minf(Y[0], Y[1]), arc_minf(Y[2], Y[3])); b->y1 = arc_maxf(arc_maxf(Y[0], Y[1]), arc_maxf(Y[2], Y[3]));
}
/* a box in target `tgt`'s pixels through the last blits of it (and of the targets it lands in) toward the screen;
   returns where it ended: 0 = the screen */
static uint32_t fr_map(uint32_t tgt, float b[4]) {
    for (int hop = 0; tgt && hop < 4; hop++) {
        const frec_blit *f = NULL;
        for (int i = g_fr.nb - 1; i >= 0 && !f; i--) if (g_fr.bl[i].tex == tgt && g_fr.bl[i].into != tgt) f = &g_fr.bl[i];
        if (!f) return tgt;
        float kx = (f->x1 - f->x0) / f->sw, ky = (f->y1 - f->y0) / f->sh, ox = f->sw < 0 ? f->x1 : f->x0, oy = f->sh < 0 ? f->y1 : f->y0;
        float a = ox + (b[0] - f->sx) * kx, c = ox + (b[2] - f->sx) * kx, d = oy + (b[1] - f->sy) * ky, e = oy + (b[3] - f->sy) * ky;
        b[0] = arc_minf(a, c); b[2] = arc_maxf(a, c); b[1] = arc_minf(d, e); b[3] = arc_maxf(d, e);
        tgt = f->into;
    }
    return tgt;
}
void arc__frame_target_name(uint32_t id, char *out, size_t n) { if (id < ARC__MAX_TEXTURES && g_tname[id][0]) snprintf(out, n, "%s", g_tname[id]); else snprintf(out, n, "#%u", (unsigned)id); }
void arc__frame_record(bool on) { g_fr.on = on; g_fr.depth = 0; g_fr.nt = g_fr.ns = g_fr.nr = g_fr.nchars = g_fr.nb = 0; }
/* 0.10.9 (Moonbase Raid) immediate mode: every tick's frame is recorded while it is drawn (arc__frame_record(true)
   before the tick), then kept, recording off (this), so `frame` describes the frame it shows without drawing again */
void arc__frame_keep(void) { g_fr.on = false; g_fr.depth = 0; }
static float g_fr_scale;   /* 0.10.12 the scale of the text being recorded (0: not known) */
static void fr_text(float x, float y, float w, float h, uint32_t rgb, const char *s) {
    if (!g_fr.on || g_fr.depth || !s || !*s || g_fr.nt >= 512) return;
    size_t n = strlen(s); if (n > 400) n = 400;
    if (g_fr.nchars + (int)n + 1 > (int)sizeof g_fr.chars) return;
    if (R.vp_cur >= 0 && R.target_depth == 1) { x += (float)R.vps[R.vp_cur].x; y += (float)R.vps[R.vp_cur].y; }   /* 0.10.2: window pixels */
    x += g_off_x; y += g_off_y;   /* 0.10.13 */
    frec_text *t = &g_fr.t[g_fr.nt++]; t->x = x; t->y = y; t->w = w; t->h = h; t->rgb = rgb; t->off = g_fr.nchars; t->tgt = arc__frame_target(); t->scale = g_fr_scale;
    memcpy(g_fr.chars + g_fr.nchars, s, n); g_fr.chars[g_fr.nchars + n] = 0; g_fr.nchars += (int)n + 1;
}
static void fr_quad(bool sprite, float x, float y, float w, float h, uint32_t rgb, uint32_t tex) {
    if (!g_fr.on || g_fr.depth) return;
    frec_quad *q = sprite ? (g_fr.ns < 256 ? &g_fr.sp[g_fr.ns++] : NULL) : (g_fr.nr < 256 ? &g_fr.rc[g_fr.nr++] : NULL);
    if (q && R.vp_cur >= 0 && R.target_depth == 1) { x += (float)R.vps[R.vp_cur].x; y += (float)R.vps[R.vp_cur].y; }
    x += g_off_x; y += g_off_y;   /* 0.10.13 */
    if (q) { q->x = x; q->y = y; q->w = w; q->h = h; q->rgb = rgb; q->tex = tex; q->tgt = arc__frame_target(); }
}
/* 0.10.7: arc_draw_texture_ex is listed in frame.sprites; a render target's placement is kept (fr_blit) */
static void fr_texture_ex(uint32_t tex, float sx, float sy, float sw, float sh, const float *X, const float *Y, float radians, uint32_t tint) {
    if (!g_fr.on || g_fr.depth) return;
    float x0 = arc_minf(arc_minf(X[0], X[1]), arc_minf(X[2], X[3])), y0 = arc_minf(arc_minf(Y[0], Y[1]), arc_minf(Y[2], Y[3]));
    float x1 = arc_maxf(arc_maxf(X[0], X[1]), arc_maxf(X[2], X[3])), y1 = arc_maxf(arc_maxf(Y[0], Y[1]), arc_maxf(Y[2], Y[3]));
    fr_quad(true, x0, y0, x1 - x0, y1 - y0, tint, tex);
    if (radians == 0) fr_blit(tex, sx, sy, sw, sh, X, Y);
}
static void arc__draw_nine_body(arc_texture t, float x, float y, float w, float h, float sb, float db, uint32_t tint);
void arc_draw_nine(arc_texture t, float x, float y, float w, float h, float sb, float db, uint32_t tint) {
    fr_quad(true, x, y, w, h, tint, t.id); g_fr.depth++;
    arc__draw_nine_body(t, x, y, w, h, sb, db, tint); g_fr.depth--;
}
static void arc__draw_nine_body(arc_texture t, float x, float y, float w, float h, float sb, float db, uint32_t tint) {
    if (w <= 0 || h <= 0) return;
    uint32_t id = tex_ok(t) ? t.id : TEX_WHITE;
    float tw = (float)tex_w(id), th = (float)tex_h(id);
    sb = arc_clampf(sb, 0, arc_minf(tw, th) * 0.5f);
    float bx = arc_minf(db, w * 0.5f), by = arc_minf(db, h * 0.5f);
    float xs[4] = { x, x + bx, x + w - bx, x + w }, ys[4] = { y, y + by, y + h - by, y + h };
    float us[4] = { 0, sb / tw, 1 - sb / tw, 1 }, vs[4] = { 0, sb / th, 1 - sb / th, 1 };
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            if (xs[i + 1] <= xs[i] || ys[j + 1] <= ys[j]) continue;
            quad(id, xs[i], ys[j], xs[i + 1], ys[j], xs[i + 1], ys[j + 1], xs[i], ys[j + 1], us[i], vs[j], us[i + 1], vs[j + 1], tint);
        }
}
void arc_draw_rect(float x, float y, float w, float h, uint32_t c) { fr_quad(false, x, y, w, h, c, 0); quad(TEX_WHITE, x, y, x + w, y, x + w, y + h, x, y + h, 0, 0, 1, 1, c); }
void arc_draw_rect_lines(float x, float y, float w, float h, uint32_t c) {
    arc_draw_rect(x, y, w, 1, c); arc_draw_rect(x, y + h - 1, w, 1, c); arc_draw_rect(x, y, 1, h, c); arc_draw_rect(x + w - 1, y, 1, h, c);
}
void arc_draw_line2d(float x0, float y0, float x1, float y1, float t, uint32_t c) {
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy); if (l < 1e-6f) return;
    if (t < 1.5f) {   /* thin: the hard-edged quad of old */
        float nx = -dy / l * t * 0.5f, ny = dx / l * t * 0.5f;
        quad(TEX_WHITE, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, 0, 0, 1, 1, c);
        return;
    }
    /* 0.10.11: 1.5 pixels and wider get a soft edge and arc_set_line_cap's ends (butt ends by default, as before) */
    line_pt sh[LINE_SHAPE_MAX]; int cap = R.line_cap2d_set ? R.line_cap : ARC_LINE_CAP_BUTT;
    int n = line_shape(t, cap, sh);
    arc__vert2d *v = v2_alloc(TEX_WHITE, n); if (!v) return;
    uint32_t col = arc__to_abgr(c); float ux = dx / l, uy = dy / l, vx = -uy, vy = ux, X[2] = { x0, x1 }, Y[2] = { y0, y1 };
    for (int i = 0; i < n; i++) {
        int e = sh[i].end;
        v[i].x = X[e] + ux * sh[i].along + vx * sh[i].across; v[i].y = Y[e] + uy * sh[i].along + vy * sh[i].across;
        v[i].u = 0.5f; v[i].v = 0.5f;
        v[i].color = (col & 0x00FFFFFFu) | ((uint32_t)(((col >> 24) * sh[i].a + 127) / 255) << 24);
    }
}
/* 0.10.13 (Vector Trader) glowing 2D lines in one quad each: a 64 x 256 profile texture (u across the quad, rows by
   the core's share of the width and the halo's strength, 16 x 16) holds a solid core with a one-texel soft edge and a
   halo fading to 0, so a core and its glow cost 6 vertices instead of two quads' 12 */
#define LP_W 64
#define LP_ROWS 256
static uint32_t g_lp_tex;
static uint32_t lines2d_profile(void) {
    if (g_lp_tex && tex_ok((arc_texture){ g_lp_tex })) return g_lp_tex;
    uint8_t *px = (uint8_t *)arc__xalloc(LP_W * LP_ROWS * 4);
    for (int r = 0; r < LP_ROWS; r++) {
        float core = (float)(r % 16 + 1) / 16.0f, halo = (float)(r / 16) / 15.0f;   /* core share 1/16..1, halo 0..1 */
        for (int x = 0; x < LP_W; x++) {
            float d = fabsf(((float)x + 0.5f) / (float)LP_W * 2.0f - 1.0f);           /* 0 at the middle, 1 at the edge */
            float edge = 1.5f / (float)LP_W * 2.0f;                                     /* the core's soft edge */
            float a = d <= core - edge ? 1.0f : d < core ? (core - d) / edge : 0.0f;
            if (core < 1.0f && d >= core - edge) { float t = arc_clampf((d - core) / (1.0f - core), 0.0f, 1.0f); float h = halo * (1.0f - t) * (1.0f - t); if (h > a) a = h; }
            uint8_t *o = px + ((size_t)r * LP_W + (size_t)x) * 4; o[0] = o[1] = o[2] = 255; o[3] = (uint8_t)(arc_clampf(a, 0, 1) * 255.0f + 0.5f);
        }
    }
    g_lp_tex = arc_texture_create(LP_W, LP_ROWS, px, true).id; free(px);
    return g_lp_tex;
}
void arc_draw_lines2d(const arc_v2 *pts, int count, float width, float glow, float halo_alpha, uint32_t rgb) {
    if (!pts || count < 2 || width <= 0) return;
    if (glow < 0) glow = 0;
    float total = width * (1.0f + 2.0f * glow) + 2.0f;   /* +1 pixel each side for the soft edge */
    int ci = arc_clampi((int)(width / total * 16.0f + 0.5f) - 1, 0, 15), hi = arc_clampi((int)(arc_clampf(halo_alpha, 0, 1) * 15.0f + 0.5f), 0, 15);
    if (glow <= 0) hi = 0;
    float v = ((float)(hi * 16 + ci) + 0.5f) / (float)LP_ROWS, half = total * 0.5f;
    uint32_t tex = lines2d_profile(), col = arc__to_abgr(rgb);
    int segs = count / 2;
    g_lines2d_frame += segs;
    arc__vert2d *o = v2_alloc(tex, segs * 6); if (!o) return;
    int n = 0;
    for (int i = 0; i + 1 < count; i += 2) {
        float x0 = pts[i].x, y0 = pts[i].y, x1 = pts[i + 1].x, y1 = pts[i + 1].y, dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
        if (l < 1e-6f) { dx = 1; dy = 0; l = 1; }
        float nx = -dy / l * half, ny = dx / l * half, ex = dx / l * width * 0.5f, ey = dy / l * width * 0.5f;   /* ends reach half the core past each point */
        float X[4] = { x0 - ex + nx, x1 + ex + nx, x1 + ex - nx, x0 - ex - nx }, Y[4] = { y0 - ey + ny, y1 + ey + ny, y1 + ey - ny, y0 - ey - ny }, U[4] = { 0, 0, 1, 1 };
        static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k = 0; k < 6; k++) { arc__vert2d *q = &o[n++]; q->x = X[idx[k]]; q->y = Y[idx[k]]; q->u = U[idx[k]]; q->v = v; q->color = col; }
    }
}
void arc_draw_sprite(arc_texture t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t tint) {
    fr_quad(true, x, y, w, h, tint, t.id);
    if (g_fr.on && tex_ok(t)) {   /* 0.10.7: a render target placed with uv */
        float X[4] = { x, x + w, x + w, x }, Y[4] = { y, y, y + h, y + h }, tw = (float)tex_w(t.id), th = (float)tex_h(t.id);
        fr_blit(t.id, u0 * tw, v0 * th, (u1 - u0) * tw, (v1 - v0) * th, X, Y);
    }
    quad(t.id ? t.id : TEX_WHITE, x, y, x + w, y, x + w, y + h, x, y + h, u0, v0, u1, v1, tint);
}
/* 0.10.12 (Vector Trader): the built-in 8x8 font drops whole rows and columns below scale 1 and on half pixels
   ("DOCK" read "DUCK"). It now lands on whole pixels, and arc_text_fit_scale picks whole sizes. At 1 and above, nearest
   sampling keeps every row whatever the size, so sizes stay as given (layouts made with them still fit);
   arc_text_snap_builtin(ARC_TEXT_SNAP_SIZES) rounds them to whole numbers too (widths and heights agree). ARC_TEXT_EXACT: as given. */
static int g_text_snap8 = 1;
static bool g_text_exact;   /* (one draw with ARC_TEXT_EXACT) */
void arc_text_snap_builtin(int mode) { g_text_snap8 = arc_clampi(mode, 0, 2); }
static float text_scale(float s) { return !g_text_exact && g_text_snap8 == 2 && s >= 1.0f ? floorf(s + 0.5f) : s; }
static float text_pos(float v) { return g_text_exact || g_text_snap8 == 0 ? v : floorf(v + 0.5f); }
static float text_draw_raw(float x, float y, float scale, uint32_t rgb, const char *text);
float arc_draw_text(float x, float y, float scale, uint32_t rgb, const char *text) {
    if (!text) return 0;
    scale = text_scale(scale); x = text_pos(x); y = text_pos(y);
    if (g_fr.on && !g_fr.depth) { g_fr_scale = scale; fr_text(x, y, arc_text_width(scale, text), arc_text_height(scale, text), rgb, text); g_fr_scale = 0; }
    return text_draw_raw(x, y, scale, rgb, text);
}
static float text_draw_raw(float x, float y, float scale, uint32_t rgb, const char *text) {
    if (scale > 0 && scale < 0.95f && text && *text)   /* 0.9.8 (Moonbase): the 8 px built-in font below 8 px loses rows */
        arc__hint("text_small_builtin", "text at %.1f px with the built-in 8 px font loses rows and can look blank on the software renderer: draw it at scale 1 or more, or bake a font at that size (tools/font_bake.py) and use arc_font_draw", 8.0f * scale);
    float cx = x, cy = y, s = 8.0f * scale, maxw = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '\n') { maxw = arc_maxf(maxw, cx - x); cx = x; cy += s * 1.25f; continue; }
        int g = (*p >= 32 && *p < 128) ? *p - 32 : '?' - 32;
        if (g != 0) {
            /* 0.8.2: sampled a hundredth of a texel inside the glyph's cell, so text on a half pixel no longer picks up the
               edge of the glyph below or beside it in the atlas */
            const float eu = 0.01f / 128.0f, ev = 0.01f / 48.0f;
            float u = (g % 16) * 8.0f / 128.0f, v = ((float)(g / 16)) * 8.0f / 48.0f;
            quad(TEX_FONT, cx, cy, cx + s, cy, cx + s, cy + s, cx, cy + s, u + eu, v + ev, u + 8.0f / 128.0f - eu, v + 8.0f / 48.0f - ev, rgb);
        }
        cx += s;
    }
    return arc_maxf(maxw, cx - x);
}
float arc_text_height(float scale, const char *text) {
    scale = text_scale(scale);
    int lines = 1; for (const char *p = text; *p; p++) if (*p == '\n') lines++;
    return 8.0f * scale + ((float)(lines - 1)) * 8.0f * scale * 1.25f;
}
float arc_text_fit_scale(const char *text, float max_width, float max_scale) {
    float w = arc_text_width(1.0f, text);
    float f = w > 0 && max_width > 0 ? arc_minf(max_scale, max_width / w) : max_scale;
    return g_text_snap8 != 0 && f >= 1.0f ? floorf(f + 1e-4f) : f;   /* 0.10.12: a whole size, the largest that fits */
}
static void text_anchor(float *x, float *y, float w, float h, int flags) {
    if (flags & ARC_TEXT_CENTER) *x -= w * 0.5f; else if (flags & ARC_TEXT_RIGHT) *x -= w;
    if (flags & ARC_TEXT_MIDDLE) *y -= h * 0.5f; else if (flags & ARC_TEXT_BOTTOM) *y -= h;
}
static uint32_t text_dark(uint32_t rgb) { uint32_t a = rgb >> 24; float al = a ? (float)a / 255.0f : 1.0f; return arc_rgba(0x000000, 0.7f * al); }
float arc_draw_text_ex(float x, float y, float scale, uint32_t rgb, int flags, const char *text) {
    if (flags & ARC_TEXT_RICH) { arc_font none = { 0 }; return arc_rich_draw(none, x, y, scale, rgb, flags, text); }
    bool keep = g_text_exact; if (flags & ARC_TEXT_EXACT) g_text_exact = true;
    scale = text_scale(scale);
    float w = arc_text_width(scale, text), h = arc_text_height(scale, text), o = arc_maxf(1.0f, scale);
    text_anchor(&x, &y, w, h, flags);
    x = text_pos(x); y = text_pos(y);
    g_fr_scale = scale; fr_text(x, y, w, h, rgb, text); g_fr_scale = 0; g_fr.depth++;
    uint32_t d = text_dark(rgb);
    if (flags & ARC_TEXT_OUTLINE) for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) if (i || j) arc_draw_text(x + (float)i * o, y + (float)j * o, scale, d, text);
    if (flags & ARC_TEXT_SHADOW) arc_draw_text(x + o, y + o, scale, d, text);
    arc_draw_text(x, y, scale, rgb, text);
    g_fr.depth--;
    g_text_exact = keep;
    return w;
}
float arc_text_width(float scale, const char *text) {
    scale = text_scale(scale);
    float w = 0, best = 0;
    for (const char *p = text; *p; p++) { if (*p == '\n') { best = arc_maxf(best, w); w = 0; } else w += 8.0f * scale; }
    return arc_maxf(best, w);
}
float arc_draw_textf(float x, float y, float scale, uint32_t rgb, const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    return arc_draw_text(x, y, scale, rgb, buf);
}

/* ---- baked bitmap fonts (.arcf, made by tools/font_bake.py) ---- */
#define ARC__MAX_FONTS 128
typedef struct { uint32_t cp; uint16_t x, y, w, h; int16_t ox, oy, adv; } arc__glyph;
typedef struct { bool used; int size, ascent, descent, count; uint32_t tex; int aw, ah; arc__glyph *g; int16_t ascii[128]; bool sdf; float spread; } arc__font_rec;   /* 0.10.1 sdf: a distance field, `spread` atlas pixels each way */
static arc__font_rec g_fonts[ARC__MAX_FONTS];

uint32_t arc_utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s; uint32_t c = p[0];
    if (!c) return 0;
    int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
    if (n == 2) c &= 0x1F; else if (n == 3) c &= 0x0F; else if (n == 4) c &= 0x07;
    for (int i = 1; i < n; i++) { if ((p[i] & 0xC0) != 0x80) { n = 1; c = 0xFFFD; break; } c = (c << 6) | (p[i] & 0x3F); }
    *s += n; return c;
}

static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static int rs16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
arc_font arc_font_from_memory(const uint8_t *d, size_t n) {
    arc_font none = { 0 };
    if (!d || n < 24 || memcmp(d, "ARCF", 4) || d[4] != 1) { arc_log("not an .arcf font"); return none; }
    int slot = 1; while (slot < ARC__MAX_FONTS && g_fonts[slot].used) slot++;
    if (slot >= ARC__MAX_FONTS) { arc_log("too many fonts"); return none; }
    int count = rd16(d + 16), aw = rd16(d + 18), ah = rd16(d + 20), flags = rd16(d + 22);
    size_t need = 24 + (size_t)count * 18 + (size_t)aw * ah;
    if (n < need || aw <= 0 || ah <= 0) { arc_log("truncated .arcf font"); return none; }
    arc__font_rec *f = &g_fonts[slot]; memset(f, 0, sizeof *f);
    f->size = rd16(d + 8); f->ascent = rs16(d + 12); f->descent = rs16(d + 14); f->count = count; f->aw = aw; f->ah = ah;
    f->g = (arc__glyph *)arc__xalloc(sizeof(arc__glyph) * (size_t)(count ? count : 1));
    for (int i = 0; i < 128; i++) f->ascii[i] = -1;
    const uint8_t *q = d + 24;
    for (int i = 0; i < count; i++, q += 18) {
        arc__glyph *g = &f->g[i];
        g->cp = (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24;
        g->x = (uint16_t)rd16(q + 4); g->y = (uint16_t)rd16(q + 6); g->w = (uint16_t)rd16(q + 8); g->h = (uint16_t)rd16(q + 10);
        g->ox = (int16_t)rs16(q + 12); g->oy = (int16_t)rs16(q + 14); g->adv = (int16_t)rs16(q + 16);
        if (g->cp < 128) f->ascii[g->cp] = (int16_t)i;
    }
    f->sdf = (flags & 2) != 0; f->spread = (float)((flags >> 8) & 0xFF); if (f->sdf && f->spread <= 0) f->spread = 4;
    uint8_t *rgba = (uint8_t *)arc__xalloc((size_t)aw * ah * 4);
    for (size_t i = 0, np = (size_t)aw * ah; i < np; i++) { rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255; rgba[i * 4 + 3] = q[i]; }
    arc_texture t = arc_texture_create(aw, ah, rgba, (flags & 1) != 0 || f->sdf); free(rgba);   /* (a distance field is always read smoothly) */
    if (!t.id) { free(f->g); return none; }
    f->tex = t.id; f->used = true;
    arc_font r = { (uint32_t)slot }; return r;
}
arc_font arc_font_load(const char *path) {
    size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    arc_font f = { 0 };
    if (d) f = arc_font_from_memory(d, n); else arc_log("cannot load font %s", path);
    arc_arena_pop(arc_frame(), mark);
    return f;
}
static arc__font_rec *font_rec(arc_font f) { return f.id && f.id < ARC__MAX_FONTS && g_fonts[f.id].used ? &g_fonts[f.id] : NULL; }
void arc_font_destroy(arc_font f) {
    arc__font_rec *r = font_rec(f); if (!r) return;
    arc_texture t = { r->tex }; arc_texture_destroy(t); free(r->g); memset(r, 0, sizeof *r);
}
static const arc__glyph *find_glyph(const arc__font_rec *f, uint32_t cp) {
    if (cp < 128) return f->ascii[cp] >= 0 ? &f->g[f->ascii[cp]] : NULL;
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (f->g[m].cp == cp) return &f->g[m]; if (f->g[m].cp < cp) lo = m + 1; else hi = m - 1; }
    return NULL;
}
static const arc__glyph *glyph_or_fallback(const arc__font_rec *f, uint32_t cp) {
    const arc__glyph *g = find_glyph(f, cp); return g ? g : find_glyph(f, '?');   /* raylib's fallback too */
}
bool arc_font_glyph(arc_font f, uint32_t cp, arc_glyph *out) {
    arc__font_rec *r = font_rec(f); if (!r || !out) return false;
    const arc__glyph *g = glyph_or_fallback(r, cp); if (!g) return false;
    out->texture.id = r->tex; out->atlas_w = r->aw; out->atlas_h = r->ah;
    out->sx = g->x; out->sy = g->y; out->sw = g->w; out->sh = g->h; out->ox = g->ox; out->oy = g->oy; out->advance = (float)(g->adv ? g->adv : g->w);
    return true;
}
int  arc_font_size(arc_font f) { arc__font_rec *r = font_rec(f); return r ? r->size : 0; }
bool arc_font_is_sdf(arc_font f) { arc__font_rec *r = font_rec(f); return r && r->sdf; }
static bool g_text_snap = true;   /* 0.10.1: lines of baked-font text start on whole pixels */
void arc_text_snap(bool on) { g_text_snap = on; }
bool arc_font_has(arc_font f, uint32_t cp) { arc__font_rec *r = font_rec(f); return r && find_glyph(r, cp); }
float arc_font_advance(arc_font f, uint32_t cp, float scale) {
    arc__font_rec *r = font_rec(f); if (!r) return 0;
    const arc__glyph *g = glyph_or_fallback(r, cp); if (!g) return 0;
    return (g->adv ? g->adv : g->w) * scale;
}
void arc_font_draw_glyph(arc_font f, uint32_t cp, float x, float y, float scale, uint32_t tint) {
    arc__font_rec *r = font_rec(f); if (!r) return;
    const arc__glyph *g = glyph_or_fallback(r, cp); if (!g || !g->w || !g->h || cp == ' ') return;
    float x0 = x + g->ox * scale, y0 = y + g->oy * scale, iw = 1.0f / (float)r->aw, ih = 1.0f / (float)r->ah;
    float keep = R.sdf2d; if (r->sdf) R.sdf2d = 2.0f * r->spread * scale;   /* 0.10.1: screen pixels per unit of the field */
    quad(r->tex, x0, y0, x0 + g->w * scale, y0, x0 + g->w * scale, y0 + g->h * scale, x0, y0 + g->h * scale,
         g->x * iw, g->y * ih, (g->x + g->w) * iw, (g->y + g->h) * ih, tint);
    R.sdf2d = keep;
}
static float font_run(arc_font f, float x, float y, float scale, float spacing, uint32_t tint, const char *s, bool draw) {
    arc__font_rec *r = font_rec(f); if (!r || !s) return 0;
    if (draw && g_fr.on && !g_fr.depth) { g_fr.depth++; float w = font_run(f, x, y, scale, spacing, tint, s, false); g_fr.depth--;
        int lines = 1; for (const char *q = s; *q; q++) lines += *q == '\n';
        fr_text(x, y, w, (float)r->size * scale * (float)lines, tint, s); }
    if (draw && g_text_snap) { x = floorf(x + 0.5f); y = floorf(y + 0.5f); }   /* 0.10.1: on whole pixels (arc_text_snap) */
    float cx = x, cy = y, best = 0;
    for (uint32_t cp; (cp = arc_utf8_next(&s)) != 0;) {
        if (cp == '\n') { best = arc_maxf(best, cx - x); cx = x; cy += (float)r->size * scale; continue; }
        if (draw) arc_font_draw_glyph(f, cp, cx, cy, scale, tint);
        cx += arc_font_advance(f, cp, scale) + spacing;
    }
    return arc_maxf(best, cx - x);
}
float arc_font_draw_aligned(arc_font f, float x, float y, float scale, uint32_t tint, int flags, const char *utf8) {
    if (flags & ARC_TEXT_RICH) return arc_rich_draw(f, x, y, scale, tint, flags, utf8);
    float w = arc_font_width(f, scale, 0, utf8), h = (float)arc_font_size(f) * scale, o = arc_maxf(1.0f, scale);
    text_anchor(&x, &y, w, h, flags);
    fr_text(x, y, w, h, tint, utf8); g_fr.depth++;
    uint32_t d = text_dark(tint);
    if (flags & ARC_TEXT_OUTLINE) for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) if (i || j) arc_font_draw_ex(f, x + (float)i * o, y + (float)j * o, scale, 0, d, utf8);
    if (flags & ARC_TEXT_SHADOW) arc_font_draw_ex(f, x + o, y + o, scale, 0, d, utf8);
    arc_font_draw_ex(f, x, y, scale, 0, tint, utf8);
    g_fr.depth--;
    return w;
}
float arc_font_draw(arc_font f, float x, float y, uint32_t tint, const char *utf8) { return font_run(f, x, y, 1, 0, tint, utf8, true); }
float arc_font_draw_ex(arc_font f, float x, float y, float scale, float spacing, uint32_t tint, const char *utf8) {
    if (scale > 0 && scale < 0.6f && utf8 && *utf8 && f.id && !arc_font_is_sdf(f)) {   /* 0.9.8 (Moonbase): far below the baked size, strokes drop out (0.10.1: not with a distance field) */
        int sz = arc_font_size(f);
        arc__hint("text_small_font", "a %d px font drawn at %.1f px (scale %.2f) drops strokes and can look blank: bake the size you draw (tools/font_bake.py --sizes %d,...)", sz, (float)sz * scale, scale, (int)((float)sz * scale + 0.5f));
    }
    return font_run(f, x, y, scale, spacing, tint, utf8, true);
}
float arc_font_width(arc_font f, float scale, float spacing, const char *utf8) { return font_run(f, 0, 0, scale, spacing, 0, utf8, false); }

/* ---- word-wrapped text (0.8.2, MechBattleArc's request) ----
   Lines break at spaces and at '\n'; a word wider than the column is split at a whole UTF-8 character. Trailing spaces
   are dropped. Each line goes to emit(start, length); returns the number of lines. */
typedef float (*arc__wrap_measure)(const void *ctx, const char *s);
static int arc__wrap(const char *text, float width, arc__wrap_measure m, const void *ctx, void (*emit)(void *u, const char *s, int n), void *u) {
    char buf[1024]; int lines = 0; const char *p = text ? text : "";
#define WRAP_W(a, b) ((size_t)((b) - (a)) >= sizeof buf ? 1e30f : (memcpy(buf, (a), (size_t)((b) - (a))), buf[(b) - (a)] = 0, m(ctx, buf)))
    for (;;) {
        const char *start = p, *fit = p, *q = p;
        for (;;) {   /* add words while they fit */
            const char *we = q; while (*we && *we != ' ' && *we != '\n') we++;
            if (WRAP_W(start, we) <= width || width <= 0) {
                fit = we;
                if (!*we || *we == '\n') { emit(u, start, (int)(fit - start)); lines++; if (!*we) return lines; p = we + 1; goto next_line; }
                q = we; while (*q == ' ') q++;
                if (!*q) { emit(u, start, (int)(fit - start)); return lines + 1; }
                if (*q == '\n') { emit(u, start, (int)(fit - start)); lines++; p = q + 1; goto next_line; }
                continue;
            }
            if (fit > start) { emit(u, start, (int)(fit - start)); lines++; p = fit; while (*p == ' ') p++; if (!*p) return lines; goto next_line; }
            {   /* one word wider than the column: as many whole characters as fit, at least one */
                const char *c = start, *best = NULL;
                while (c < we) { const char *n = c; arc_utf8_next(&n); if (best && WRAP_W(start, n) > width) break; best = n; c = n; }
                emit(u, start, (int)(best - start)); lines++; p = best; while (*p == ' ') p++; if (!*p) return lines; goto next_line;
            }
        }
    next_line:;
    }
#undef WRAP_W
}
typedef struct { arc_font f; float scale; } wrap_ctx;
static float wrap_measure_text(const void *c, const char *s) { const wrap_ctx *w = (const wrap_ctx *)c; return w->f.id ? arc_font_width(w->f, w->scale, 0, s) : arc_text_width(w->scale, s); }
static void wrap_count(void *u, const char *s, int n) { (void)u; (void)s; (void)n; }
typedef struct { wrap_ctx c; float x, y, width, line_h; uint32_t rgb; int flags; } wrap_draw;
static void wrap_emit_draw(void *u, const char *s, int n) {
    wrap_draw *d = (wrap_draw *)u; char line[1024]; if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
    memcpy(line, s, (size_t)n); line[n] = 0;
    float lw = wrap_measure_text(&d->c, line), x = d->x;
    if (d->flags & ARC_TEXT_CENTER) x += (d->width - lw) * 0.5f; else if (d->flags & ARC_TEXT_RIGHT) x += d->width - lw;
    int f = d->flags & (ARC_TEXT_SHADOW | ARC_TEXT_OUTLINE);
    if (d->c.f.id) arc_font_draw_aligned(d->c.f, x, d->y, d->c.scale, d->rgb, f, line); else arc_draw_text_ex(x, d->y, d->c.scale, d->rgb, f, line);
    d->y += d->line_h;
}
static float wrap_line_h(arc_font f, float scale) { return f.id ? (float)arc_font_size(f) * scale : 8.0f * scale * 1.25f; }
static float wrap_height(int lines, arc_font f, float scale) {   /* as arc_text_height: the last line without its gap */
    if (lines <= 0) return 0;
    return f.id ? (float)lines * (float)arc_font_size(f) * scale : 8.0f * scale + (float)(lines - 1) * 8.0f * scale * 1.25f;
}
static float wrap_body(arc_font f, float x, float y, float width, float scale, uint32_t rgb, int flags, const char *text, bool draw);
static float wrap_any(arc_font f, float x, float y, float width, float scale, uint32_t rgb, int flags, const char *text, bool draw) {
    if (!draw || !g_fr.on || g_fr.depth) return wrap_body(f, x, y, width, scale, rgb, flags, text, draw);
    g_fr.depth++; float h = wrap_body(f, x, y, width, scale, rgb, flags, text, true); g_fr.depth--;
    fr_text(x, y, width, h, rgb, text); return h;
}
static float wrap_body(arc_font f, float x, float y, float width, float scale, uint32_t rgb, int flags, const char *text, bool draw) {
    wrap_ctx c = { f, scale };
    float h = wrap_height(arc__wrap(text, width, wrap_measure_text, &c, wrap_count, NULL), f, scale);
    if (!draw) return h;
    if (flags & ARC_TEXT_MIDDLE) y -= h * 0.5f; else if (flags & ARC_TEXT_BOTTOM) y -= h;
    wrap_draw d = { c, x, y, width, wrap_line_h(f, scale), rgb, flags };
    arc__wrap(text, width, wrap_measure_text, &c, wrap_emit_draw, &d);
    return h;
}
float arc_draw_text_wrapped(float x, float y, float width, float scale, uint32_t rgb, int flags, const char *text) { arc_font none = { 0 }; return wrap_any(none, x, y, width, scale, rgb, flags, text, true); }
float arc_text_wrapped_height(float width, float scale, const char *text) { arc_font none = { 0 }; return wrap_any(none, 0, 0, width, scale, 0, 0, text, false); }
float arc_font_draw_wrapped(arc_font f, float x, float y, float width, float scale, uint32_t tint, int flags, const char *utf8) { return wrap_any(f, x, y, width, scale, tint, flags, utf8, true); }
float arc_font_wrapped_height(arc_font f, float width, float scale, const char *utf8) { return wrap_any(f, 0, 0, width, scale, 0, 0, utf8, false); }

static void gfx_end_mode(int mode);   /* 0 nothing, 1 targets only, 2 everything */
/* immediate mode: show the last built frame again (its data stays put until the next arc__gfx_begin) */
static bool g_last_targets_done;   /* 0.9.8: the last frame's render targets were drawn (not when --draw-last skipped them) */
bool arc__gfx_render_last(void) {
    if (!g_last_valid || R.in_frame) return false;
    arc__frame_data f = g_last_frame; f.targets_only = false; f.screen_only = g_last_targets_done;
    arc__gfx->render(&f); R.has_frame = true; g_last_targets_done = true; return true;
}
void arc__gfx_end(bool render) { gfx_end_mode(render ? 2 : 0); }
void arc__gfx_end_offscreen(void) { gfx_end_mode(1); }
/* 0.8.7 planar reflection: before the first screen pass that draws a reflective mesh, a mirror pass into R.refl_tex
   (see arc__pass.mirror): that pass's view through the plane, its lights, sky and fog, no shadows, post or 2D, and
   its mesh batches minus reflective and unreflected meshes. Returns the new batch count. */
static uint32_t refl_target(uint32_t *slot, int w, int h) {   /* (0.10.2: one per surface: R.refl_tex, R.vp_refl[i]) */
    if (*slot && arc__textures[*slot].used && arc__textures[*slot].w == w && arc__textures[*slot].h == h) return *slot;
    if (*slot) { arc_texture_destroy((arc_texture){ *slot }); *slot = 0; }
    if (!arc__gfx->target_create) return 0;
    uint32_t id = alloc_tex_id(); if (!id) return 0;
    arc__tex_rec *t = &arc__textures[id];
    t->used = true; t->target = true; t->w = w; t->h = h; t->smooth = true; t->flags = ARC_TEX_SMOOTH | ARC_TEX_MIPMAPS;   /* backends give it mips */
    t->rgba = (uint8_t *)arc__xalloc((size_t)w * (size_t)h * 4);
    t->mip[0] = t->rgba; t->mw[0] = w; t->mh[0] = h; t->levels = 1;
    if (!arc__gfx->target_create(id, w, h, true)) { free(t->rgba); memset(t, 0, sizeof *t); return 0; }
    return *slot = id;
}
/* the mirror pass for surface `vport` (0 = the screen, else viewport vport - 1) before its first pass that draws a
   reflective mesh; returns the new batch count */
static int refl_pass_one(int nb, int vport) {
    int at = -1;
    for (int pi = 0; pi < R.pass_count && at < 0; pi++) {
        const arc__pass *p = &R.passes[pi]; if (vport ? p->vport != vport : p->target != 0) continue;
        for (int b = p->mb_first; b < p->mb_first + p->mb_count + p->tb_count; b++) if (arc__meshes[R.mesh_batches[b].mesh].reflect > 0) { at = pi; break; }
    }
    if (at < 0 || R.pass_count >= ARC__MAX_PASSES) return nb;
    int sw = vport ? R.vps[vport - 1].w : R.w, sh = vport ? R.vps[vport - 1].h : R.h;
    int tw = arc_maxi(1, (int)((float)sw * R.refl_scale + 0.5f)), th = arc_maxi(1, (int)((float)sh * R.refl_scale + 0.5f));
    uint32_t tex = refl_target(vport ? &R.vp_refl[vport - 1] : &R.refl_tex, tw, th); if (!tex) return nb;
    memmove(&R.passes[at + 1], &R.passes[at], sizeof(arc__pass) * (size_t)(R.pass_count - at)); R.pass_count++;
    arc__pass *M = &R.passes[at], *P = &R.passes[at + 1];
    *M = *P;
    float y = R.refl_y;
    arc_m4 ref = arc_m4_identity(); ref.m[5] = -1; ref.m[13] = 2 * y;   /* (x, 2y - y, z) */
    M->target = tex; M->width = tw; M->height = th; M->clear = true; M->clear_col[3] = 1; M->vport = 0;
    M->view_proj = arc_m4_mul(P->view_proj, ref); M->view_proj_std = arc_m4_mul(P->view_proj_std, ref); M->eye.y = 2 * y - P->eye.y;
    memset(M->post, 0, sizeof M->post); M->soft = 0; M->shadow[0] = 0;
    if (M->sky[3] > 0) memcpy(M->sky_ivp, arc_m4_inverse(M->view_proj_std).m, sizeof M->sky_ivp);
    M->imm_count = M->line_count = M->b2_count = M->ltri_count = 0;
    M->mirror = 1; M->clip[0] = 0; M->clip[1] = 1; M->clip[2] = 0; M->clip[3] = -(y + 0.005f); M->refl_tex = 0;
    pass_lights(M, P->nlights, vport);   /* its own tiles: the mirrored view */
    M->lsh_n = 0;   /* no light shadows in the mirror (as no sun shadows) */
    int mb0 = nb, ranges[2][2] = { { P->mb_first, P->mb_count }, { P->mb_first + P->mb_count, P->tb_count } };
    for (int r = 0; r < 2; r++) {
        int start = nb;
        for (int b = ranges[r][0]; b < ranges[r][0] + ranges[r][1] && nb < ARC__MAX_BATCHES3D; b++) {
            const arc__mesh_rec *mr = &arc__meshes[R.mesh_batches[b].mesh];
            if (mr->reflect > 0 || mr->unreflected) continue;
            R.mesh_batches[nb++] = R.mesh_batches[b];
        }
        if (r == 0) { M->mb_first = mb0; M->mb_count = nb - start; } else M->tb_count = nb - start;
    }
    int deepest = 0; for (int s2 = arc_maxi(tw, th); s2 > 1; s2 >>= 1) deepest++;
    P->refl_tex = tex; P->refl[0] = R.refl_strength; P->refl[1] = (float)deepest; P->refl[2] = 1.0f / (float)P->width; P->refl[3] = 1.0f / (float)P->height;
    return nb;
}
static int refl_pass(int nb) {
    if (!(R.refl_strength > 0)) {
        if (R.refl_tex) { arc_texture_destroy((arc_texture){ R.refl_tex }); R.refl_tex = 0; }
        for (int i = 0; i < ARC_MAX_VIEWPORTS; i++) if (R.vp_refl[i]) { arc_texture_destroy((arc_texture){ R.vp_refl[i] }); R.vp_refl[i] = 0; }
        return nb;
    }
    nb = refl_pass_one(nb, 0);
    for (int v = 1; v <= R.vp_count; v++) nb = refl_pass_one(nb, v);
    return nb;
}
static void gfx_end_mode(int mode) {
    if (R.vp_cur >= 0) arc_viewport_end();   /* 0.10.2: one left open */
    R.vpl_count = R.vp_count; memcpy(R.vpl, R.vps, sizeof R.vps);
    arc__markers_flush();   /* 0.9.9 ground markers queued in the last pass */
    plain_fixup();
    end_pass_counts();
    R.in_frame = false;
    int prof_sort = arc_prof_begin("gfx.sort");
    /* inside each pass: the common solid instances (SL_FAST) are grouped by mesh (a counting sort keeps submission
       order inside a mesh); every other instance is sorted by slot, then order, then (see-through) far to near, ties
       keeping submission order, and runs of the same mesh and state become batches. Drawing order:
       SL_BG, SL_PRE, SL_FAST, SL_POST, SL_XRAY, SL_XOBJ | immediate, lines | SL_TRANS, SL_OVER */
    static uint32_t batch_key[ARC__MAX_BATCHES3D];
    static int *tsort, *tmerge;
    if (!tsort) { tsort = (int *)arc__xalloc(sizeof(int) * ARC__MAX_INSTANCES); tmerge = (int *)arc__xalloc(sizeof(int) * ARC__MAX_INSTANCES); }
    int nb = 0, tris = R.imm_count / 3;
    for (int pi = 0; pi < R.pass_count; pi++) {
        arc__pass *p = &R.passes[pi];
        int lo = p->inst_first, hi = p->inst_first + p->inst_count, nt = 0;
        for (int i = lo; i < hi; i++) if (R.raw[i].slot != SL_FAST) tsort[nt++] = i;
        /* stable merge sort: slot, order, then far to near for see-through */
        for (int width = 1; width < nt; width *= 2) {
            for (int l = 0; l < nt; l += 2 * width) {
                int m = arc_mini(l + width, nt), r = arc_mini(l + 2 * width, nt), i = l, j = m, o = l;
                while (i < m && j < r) {
                    const raw_instance *x = &R.raw[tsort[j]], *y = &R.raw[tsort[i]];   /* take x first only if strictly before y */
                    bool first = x->slot != y->slot ? x->slot < y->slot : x->order != y->order ? x->order < y->order
                               : x->slot == SL_TRANS ? x->depth > y->depth
                               : x->slot == SL_PAINT && (x->paint != y->paint ? x->paint < y->paint : x->paint2 != y->paint2 ? x->paint2 < y->paint2 : x->paint3 != y->paint3 ? x->paint3 < y->paint3 : x->pat != y->pat ? x->pat < y->pat : x->glw != y->glw ? x->glw < y->glw : x->key < y->key);
                    tmerge[o++] = first ? tsort[j++] : tsort[i++];
                }
                while (i < m) tmerge[o++] = tsort[i++];
                while (j < r) tmerge[o++] = tsort[j++];
            }
            int *sw = tsort; tsort = tmerge; tmerge = sw;
        }
        int at = lo, k = 0, b0 = nb;
        #define EMIT_SPECIALS(cond) \
            for (int run0 = nb; k < nt && (cond); k++) { \
                const raw_instance *ri = &R.raw[tsort[k]]; \
                R.sorted[at] = ri->inst; \
                arc__mesh_batch *last = nb > run0 ? &R.mesh_batches[nb - 1] : NULL; \
                if (last && last->mesh == (ri->key >> 1) && last->flip == (int)(ri->key & 1) && last->blend == ri->blend && last->depth == ri->z && last->first + last->count == at \
                    && last->paint_tex == ri->paint && last->tint2 == ri->paint2 && last->tint3 == ri->paint3 && last->pat == ri->pat && last->glow == ri->glw) last->count++; \
                else if (nb < ARC__MAX_BATCHES3D) { \
                    arc__mesh_batch *mb = &R.mesh_batches[nb++]; \
                    mb->mesh = ri->key >> 1; mb->flip = (int)(ri->key & 1); mb->blend = ri->blend; mb->depth = ri->z; mb->first = at; mb->count = 1; \
                    mb->paint_tex = ri->paint; mb->tint2 = ri->paint2; mb->tint3 = ri->paint3; mb->pat = ri->pat; mb->glow = ri->glw; \
                } \
                at++; tris += arc__meshes[ri->key >> 1].icount / 3; \
            }
        EMIT_SPECIALS(R.raw[tsort[k]].slot < SL_FAST)
        int f0 = nb;
        for (int i = lo; i < hi; i++) {
            if (R.raw[i].slot != SL_FAST) continue;
            uint32_t key = R.raw[i].key;
            if (R.mesh_counts[key]++ == 0) {
                batch_key[nb] = key; arc__mesh_batch *mb = &R.mesh_batches[nb++];
                mb->mesh = key >> 1; mb->flip = (int)(key & 1); mb->count = 0; mb->blend = 0; mb->depth = 0; mb->paint_tex = mb->tint2 = mb->tint3 = 0; mb->pat = 0; mb->glow = 0;
            }
        }
        for (int b = f0; b < nb; b++) { R.mesh_batches[b].first = at; at += R.mesh_counts[batch_key[b]]; R.mesh_counts[batch_key[b]] = R.mesh_batches[b].first; }
        for (int i = lo; i < hi; i++) { if (R.raw[i].slot != SL_FAST) continue; int dst = R.mesh_counts[R.raw[i].key]++; R.sorted[dst] = R.raw[i].inst; }
        for (int b = f0; b < nb; b++) {
            arc__mesh_batch *mb = &R.mesh_batches[b];
            mb->count = R.mesh_counts[batch_key[b]] - mb->first; R.mesh_counts[batch_key[b]] = 0;
            tris += mb->count * (arc__meshes[mb->mesh].icount / 3);
        }
        EMIT_SPECIALS(R.raw[tsort[k]].slot < SL_TRANS)
        p->mb_first = b0; p->mb_count = nb - b0;
        int tb0 = nb;
        EMIT_SPECIALS(true)
        #undef EMIT_SPECIALS
        p->tb_count = nb - tb0;
        p->soft = 0;   /* 0.7: does any see-through instance want the depth behind it (soft particles)? */
        if (tb0 < nb) for (int i = R.mesh_batches[tb0].first; i < at; i++) if (R.sorted[i].fx[1] < 0 && R.sorted[i].fx[0] <= 0) { p->soft = 1; break; }
    }
    R.mesh_batch_count = nb;
    arc_prof_end(prof_sort);
    if (R.vp_count) {   /* 0.10.2: with viewports and no 3D of the window's own, the window needs no post of its own */
        bool any3d = false;
        for (int pi = 0; pi < R.pass_count; pi++) { const arc__pass *q = &R.passes[pi]; if (!q->target && (q->inst_count || q->imm_count || q->line_count)) any3d = true; }
        if (!any3d) for (int pi = 0; pi < R.pass_count; pi++) if (!R.passes[pi].target) R.passes[pi].post[ARC__POST_ON] = 0;
    }
    /* a screen pass that clears draws its sky with the camera of the first screen pass that draws 3D (the game
       may set its camera after drawing into a render target); 0.10.2: a viewport's the same, among its own passes */
    for (int pi = 0; pi < R.pass_count; pi++) {
        arc__pass *p = &R.passes[pi], *src = p;
        if ((p->target && !p->vport) || !p->clear) continue;
        for (int k = pi; k < R.pass_count; k++) {
            const arc__pass *q = &R.passes[k];
            if (q->target != p->target) continue;
            if (k > pi && q->clear) break;
            if (q->inst_count || q->imm_count || q->line_count) { src = (arc__pass *)q; break; }
        }
        memcpy(p->sky, src->sky, sizeof p->sky);
        if (p->sky[3] > 0) memcpy(p->sky_ivp, arc_m4_inverse(src->view_proj_std).m, sizeof p->sky_ivp);
    }
    for (int pi = 0; pi < R.pass_count; pi++) if (!R.passes[pi].target || R.passes[pi].vport || R.passes[pi].soft || R.passes[pi].post[ARC__POST_ON] > 0) {
        arc__pass *p = &R.passes[pi]; arc_m4 iv = arc_m4_inverse(p->view_proj);
        if (p->zrev) {   /* reversed: fold depth * 2 - 1 -> depth into it (see arc__pass.zrev) */
            arc_m4 h = arc_m4_identity(); h.m[10] = 0.5f; h.m[14] = 0.5f;
            iv = arc_m4_mul(iv, h);
        }
        memcpy(p->ivp, iv.m, sizeof p->ivp);
    }

    nb = refl_pass(nb);   /* 0.8.7 */
    R.mesh_batch_count = nb;

    arc__frame_data f; memset(&f, 0, sizeof f);
    f.width = R.w; f.height = R.h;
    f.passes = R.passes; f.pass_count = R.pass_count;
    f.instances = R.sorted; f.instance_count = R.raw_count;
    f.mesh_batches = R.mesh_batches; f.mesh_batch_count = nb; f.pats = R.pats; f.pat_count = R.npats; f.glows = R.glows; f.glow_count = R.nglows;
    f.imm = R.imm; f.imm_count = R.imm_count;
    f.lines = R.lines; f.line_vertex_count = R.line_count;
    f.ltris = R.ltris; f.ltri_vertex_count = R.ltri_count;
    f.verts2d = R.v2; f.vert2d_count = R.v2_count;
    f.batches2d = R.b2; f.batch2d_count = R.b2_count;
    f.bones = R.bones; f.bone_count = R.bone_count;
    f.lights = &R.lt[0][0]; f.light_count = R.lt_count; f.light_tiles = R.lt_tiles; f.light_index = R.lt_idx;
    f.lsh_vp = R.lsh_vp; f.lsh_faces = R.lsh_faces; f.lsh_count = R.lsh_count; f.lsh_res = R.lsh_count ? R.lsh_fres : 512;
    f.targets_only = mode == 1;
    bool dead = g_dead_count > 0;
    g_last_frame = f; g_last_frame.targets_only = false; g_last_valid = true;
    g_last_targets_done = mode != 0;
    if (mode) { int pr = arc_prof_begin("gfx.submit"); arc__gfx->render(&f); arc_prof_end(pr); if (mode == 2) R.has_frame = true; }
    if (dead) { for (int i = 0; i < g_dead_count; i++) tex_free_now(g_dead_tex[i]); g_dead_count = 0; g_last_valid = false; }

    int passes_drawn = 0;
    for (int pi = 0; pi < R.pass_count; pi++) {
        const arc__pass *p = &R.passes[pi];
        if (p->mb_count || p->tb_count || p->imm_count || p->line_count || p->ltri_count || p->b2_count || p->clear) passes_drawn++;
    }
    R.stats.draw_calls = nb + R.b2_count + (R.imm_count ? 1 : 0) + (R.line_count ? 1 : 0) + (R.ltri_count ? 1 : 0);
    R.stats.instances = R.raw_count; R.stats.triangles = tris + R.v2_count / 3;
    R.stats.lines = R.line_count / 2 + g_lines_thick; R.stats.quads = R.v2_count / 6; R.stats.passes = passes_drawn;
    R.stats.vertices_2d = R.v2_count; R.stats.vertices_2d_max = arc__max_v2;   /* 0.10.13 */
    R.stats.dropped = g_dropped; g_dropped = 0;
    R.stats.culled = g_culled; memcpy(R.stats.lod_instances, g_lod_n, sizeof g_lod_n); R.stats.static_draws = g_static_draws;
    for (int i = 0; i < R.pending_shot_count; i++) { bool ok = arc_screenshot(R.pending_shots[i]); printf(ok ? "ARC ok shot %s\n" : "ARC error shot %s\n", R.pending_shots[i]); }
    R.pending_shot_count = 0;
}
bool arc__gfx_has_frame(void) { return R.has_frame; }
void arc__gfx_count_tick_ms(float ms) { R.last_tick_ms = ms; }
void arc__stats_frame_ms(float ms, float cpu) {
    R.stats.frame_ms = ms; R.stats.cpu_ms = cpu; R.stats.tick_ms = R.last_tick_ms;
    R.avg_ms = R.avg_ms == 0 ? ms : R.avg_ms * 0.95 + ms * 0.05; R.stats.avg_frame_ms = R.avg_ms;
}
arc_gfx_stats arc_stats(void) {
    arc_gfx_stats st = R.stats; st.textures = st.textures_compressed = 0; st.texture_bytes = 0;
    st.vertices_2d_max = arc__max_v2;   /* 0.10.13 (known before the first frame too) */
    for (int i = 1; i < ARC__MAX_TEXTURES; i++) {
        const arc__tex_rec *t = &arc__textures[i]; if (!t->used) continue;
        st.textures++; st.textures_compressed += t->fmt != ARC_TEXFMT_RGBA8;
        st.texture_bytes += (long long)(t->gpu_bytes ? t->gpu_bytes : (size_t)t->w * (size_t)t->h * 4);
    }
    return st;
}
/* internal hook so the app can set the clear colour via arc_app */
void arc__gfx_set_clear(uint32_t rgb) { R.clear = rgb; }

/* the last rendered frame as malloc'd RGBA (top row first) */
uint8_t *arc__gfx_read_frame(int *w, int *h) {
    if (!R.has_frame || !arc__gfx->read_pixels) return NULL;
    uint8_t *px = (uint8_t *)arc__xalloc((size_t)R.w * (size_t)R.h * 4);
    if (!arc__gfx->read_pixels(px, R.w, R.h)) { free(px); return NULL; }
    *w = R.w; *h = R.h; return px;
}
bool arc_screenshot(const char *path) {
    if (R.in_frame) { if (R.pending_shot_count < 8) snprintf(R.pending_shots[R.pending_shot_count++], 512, "%s", path); return true; }
    if (!R.has_frame || !arc__gfx->read_pixels) return false;
    uint8_t *px = (uint8_t *)arc__xalloc((size_t)R.w * (size_t)R.h * 4);
    bool ok = arc__gfx->read_pixels(px, R.w, R.h) && arc__image_write(path, px, R.w, R.h);
    free(px); return ok;
}

/* ======================================================================== */
/* PNG writer (zlib with fixed-Huffman LZ77)                                */
/* ======================================================================== */

typedef struct { uint8_t *d; size_t n, cap; uint32_t bits; int nbits; } bitw;
static void bw_byte(bitw *w, uint8_t b) {
    if (w->n == w->cap) { w->cap = w->cap ? w->cap * 2 : 65536; w->d = (uint8_t *)realloc(w->d, w->cap); }
    w->d[w->n++] = b;
}
static void bw_bits(bitw *w, uint32_t v, int n) { w->bits |= v << w->nbits; w->nbits += n; while (w->nbits >= 8) { bw_byte(w, (uint8_t)w->bits); w->bits >>= 8; w->nbits -= 8; } }
static void bw_rev(bitw *w, uint32_t code, int n) { uint32_t r = 0; for (int i = 0; i < n; i++) r |= ((code >> i) & 1) << (n - 1 - i); bw_bits(w, r, n); }
static void fixed_lit(bitw *w, int v) {
    if (v < 144) bw_rev(w, 0x30 + v, 8); else if (v < 256) bw_rev(w, 0x190 + v - 144, 9);
    else if (v < 280) bw_rev(w, v - 256, 7); else bw_rev(w, 0xC0 + v - 280, 8);
}
static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static uint8_t *zlib_compress(const uint8_t *src, size_t n, size_t *out_n) {
    bitw w = { 0 };
    bw_byte(&w, 0x78); bw_byte(&w, 0x01);
    bw_bits(&w, 1, 1); bw_bits(&w, 1, 2);    /* final block, fixed Huffman */
    enum { HBITS = 15, WIN = 32768 };
    int32_t *head = (int32_t *)arc__xalloc(sizeof(int32_t) << HBITS); int32_t *prev = (int32_t *)arc__xalloc(sizeof(int32_t) * WIN);
    for (int i = 0; i < (1 << HBITS); i++) head[i] = -1;
    size_t i = 0;
    while (i < n) {
        int best_len = 0, best_dist = 0;
        if (i + 3 <= n) {
            uint32_t h = ((uint32_t)src[i] * 506832829u ^ (uint32_t)src[i + 1] * 2654435761u ^ (uint32_t)src[i + 2] * 40503u) >> (32 - HBITS);
            int32_t cand = head[h]; int chain = 32;
            while (cand >= 0 && (int64_t)i - cand <= WIN - 1 && chain--) {
                size_t l = 0, maxl = n - i < 258 ? n - i : 258;
                while (l < maxl && src[cand + l] == src[i + l]) l++;
                if ((int)l > best_len) { best_len = (int)l; best_dist = (int)(i - (size_t)cand); if (l == maxl) break; }
                cand = prev[cand % WIN];
            }
            prev[i % WIN] = head[h]; head[h] = (int32_t)i;
        }
        if (best_len >= 3) {
            int li = 28; while (len_base[li] > best_len) li--;
            fixed_lit(&w, 257 + li); if (len_extra[li]) bw_bits(&w, (uint32_t)(best_len - len_base[li]), len_extra[li]);
            int di = 29; while (dist_base[di] > best_dist) di--;
            bw_rev(&w, (uint32_t)di, 5); if (dist_extra[di]) bw_bits(&w, (uint32_t)(best_dist - dist_base[di]), dist_extra[di]);
            /* insert hashes for the skipped positions (cheaply: every position) */
            for (size_t k = i + 1; k < i + (size_t)best_len && k + 3 <= n; k++) {
                uint32_t h = ((uint32_t)src[k] * 506832829u ^ (uint32_t)src[k + 1] * 2654435761u ^ (uint32_t)src[k + 2] * 40503u) >> (32 - HBITS);
                prev[k % WIN] = head[h]; head[h] = (int32_t)k;
            }
            i += (size_t)best_len;
        } else { fixed_lit(&w, src[i]); i++; }
    }
    fixed_lit(&w, 256);
    if (w.nbits) bw_bits(&w, 0, 8 - w.nbits);
    uint32_t a = 1, b = 0; for (size_t k = 0; k < n; k++) { a = (a + src[k]) % 65521; b = (b + a) % 65521; }
    uint32_t ad = (b << 16) | a;
    bw_byte(&w, (uint8_t)(ad >> 24)); bw_byte(&w, (uint8_t)(ad >> 16)); bw_byte(&w, (uint8_t)(ad >> 8)); bw_byte(&w, (uint8_t)ad);
    free(head); free(prev);
    *out_n = w.n; return w.d;
}
static uint32_t crc_table[256];
static uint32_t crc32_of(const uint8_t *d, size_t n, uint32_t c) {
    if (!crc_table[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t x = i; for (int k = 0; k < 8; k++) x = x & 1 ? 0xEDB88320u ^ (x >> 1) : x >> 1; crc_table[i] = x; }
    c = ~c; for (size_t i = 0; i < n; i++) c = crc_table[(c ^ d[i]) & 0xFF] ^ (c >> 8); return ~c;
}
static void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void png_chunk(FILE *f, const char *type, const uint8_t *d, size_t n) {
    uint8_t hdr[8]; put_be32(hdr, (uint32_t)n); memcpy(hdr + 4, type, 4); fwrite(hdr, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t c = crc32_of(hdr + 4, 4, 0); c = crc32_of(d, n, c); uint8_t cb[4]; put_be32(cb, c); fwrite(cb, 1, 4, f);
}
static bool png_write_n(const char *path, const uint8_t *rgba, int w, int h, int ch) {   /* ch 3 (RGB) or 4 (RGBA) */
    size_t stride = (size_t)w * ch, raw_n = (stride + 1) * (size_t)h;
    uint8_t *raw = (uint8_t *)arc__xalloc(raw_n);
    for (int y = 0; y < h; y++) {        /* filter "Up" (2) — good on rendered images; RGB only */
        uint8_t *row = raw + (size_t)y * (stride + 1); row[0] = y ? 2 : 0;
        for (int x = 0; x < w; x++)
            for (int c = 0; c < ch; c++) {
                uint8_t v = rgba[((size_t)y * w + x) * 4 + c], up = y ? rgba[((size_t)(y - 1) * w + x) * 4 + c] : 0;
                row[1 + x * ch + c] = (uint8_t)(v - up);
            }
    }
    size_t zn; uint8_t *z = zlib_compress(raw, raw_n, &zn); free(raw);
    FILE *f = fopen(path, "wb"); if (!f) { free(z); return false; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ih[13]; put_be32(ih, (uint32_t)w); put_be32(ih + 4, (uint32_t)h); ih[8] = 8; ih[9] = ch == 4 ? 6 : 2; ih[10] = ih[11] = ih[12] = 0;
    png_chunk(f, "IHDR", ih, 13); png_chunk(f, "IDAT", z, zn); png_chunk(f, "IEND", NULL, 0);
    fclose(f); free(z); return true;
}
bool arc__png_write(const char *path, const uint8_t *rgba, int w, int h) { return png_write_n(path, rgba, w, h, 3); }   /* screenshots: RGB */

/* ======================================================================== */
/* PNG reader (inflate) and QOI reader                                      */
/* ======================================================================== */

/* ---- inflate (zlib / deflate), 0.8.3: a 64-bit LSB-first bit buffer and 10-bit lookup tables (one step for most
   symbols; longer codes walk the canonical code a bit at a time). Reading past the end gives zero bits, counted in
   `over`, and a stream that needs them is an error. */
typedef struct { const uint8_t *s; size_t n, pos; uint64_t bits; int nbits, over; bool err; } bitr;
static inline void br_fill(bitr *r) {
    while (r->nbits <= 56) {
        uint64_t b = 0;
        if (r->pos < r->n) b = r->s[r->pos++]; else r->over += 8;
        r->bits |= b << r->nbits; r->nbits += 8;
    }
}
static inline uint32_t br_bits(bitr *r, int n) {   /* n <= 32 */
    if (!n) return 0;
    if (r->nbits < n) br_fill(r);
    uint32_t v = (uint32_t)(r->bits & ((1ull << n) - 1)); r->bits >>= n; r->nbits -= n;
    if (r->over > r->nbits) r->err = true;
    return v;
}
#define HUFF_FAST 10
typedef struct { uint16_t count[16], sym[320]; uint16_t fast[1 << HUFF_FAST]; } huff;   /* fast: sym << 4 | length, 0 = slow */
static void huff_build(huff *h, const uint8_t *lens, int n) {
    uint16_t offs[16]; memset(h->count, 0, sizeof h->count); memset(h->fast, 0, sizeof h->fast);
    for (int i = 0; i < n; i++) h->count[lens[i]]++;
    h->count[0] = 0; offs[1] = 0; for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + h->count[i];
    for (int i = 0; i < n; i++) if (lens[i]) h->sym[offs[lens[i]]++] = (uint16_t)i;
    int code = 0, k = 0;   /* canonical codes in order, bit-reversed into the table */
    for (int len = 1; len <= HUFF_FAST; len++) {
        for (int i = 0; i < h->count[len]; i++, k++, code++) {
            int rev = 0; for (int b = 0; b < len; b++) rev |= ((code >> b) & 1) << (len - 1 - b);
            for (int f = rev; f < (1 << HUFF_FAST); f += 1 << len) h->fast[f] = (uint16_t)(h->sym[k] << 4 | len);
        }
        code <<= 1;
    }
}
static inline int huff_decode(bitr *r, const huff *h) {
    if (r->nbits < 16) br_fill(r);
    uint16_t e = h->fast[r->bits & ((1u << HUFF_FAST) - 1)];
    if (e) { int len = e & 15; r->bits >>= len; r->nbits -= len; if (r->over > r->nbits) r->err = true; return e >> 4; }
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {   /* a long code: walk it */
        code |= (int)(r->bits & 1); r->bits >>= 1; r->nbits--;
        int c = h->count[len];
        if (code - c < first) { if (r->over > r->nbits) r->err = true; return h->sym[index + (code - first)]; }
        index += c; first += c; first <<= 1; code <<= 1;
    }
    r->err = true; return -1;
}
static uint8_t *inflate_zlib(const uint8_t *src, size_t n, size_t expect, size_t *out_n);
uint8_t *arc__inflate(const uint8_t *src, size_t n, size_t expect, size_t *out_n) { return inflate_zlib(src, n, expect, out_n); }
static uint8_t *inflate_grow(uint8_t *out, size_t *cap, size_t need) {
    if (need <= *cap) return out;
    size_t c = *cap ? *cap : 64; while (c < need) c *= 2;
    uint8_t *o = (uint8_t *)realloc(out, c); if (!o) { free(out); return NULL; }
    *cap = c; return o;
}
static uint8_t *inflate_zlib(const uint8_t *src, size_t n, size_t expect, size_t *out_n) {
    if (n < 2) return NULL;
    bitr r = { src + 2, n - 2, 0, 0, 0, 0, false };
    size_t cap = expect ? expect : n * 4, len = 0; uint8_t *out = (uint8_t *)malloc(cap ? cap : 64);
    if (!out) return NULL;
    if (!cap) cap = 64;
    int final = 0;
    while (!final && !r.err) {
        final = (int)br_bits(&r, 1); int type = (int)br_bits(&r, 2);
        if (type == 0) {   /* stored: back to the byte boundary (the buffered whole bytes go back to the stream) */
            r.bits >>= r.nbits & 7; r.nbits -= r.nbits & 7;
            if (r.over > r.nbits) { r.err = true; break; }
            r.pos -= (size_t)((r.nbits - r.over) / 8); r.bits = 0; r.nbits = 0; r.over = 0;
            if (r.pos + 4 > r.n) { r.err = true; break; }
            uint16_t l = (uint16_t)(r.s[r.pos] | (r.s[r.pos + 1] << 8)); r.pos += 4;
            if (r.pos + l > r.n) { r.err = true; break; }
            if (!(out = inflate_grow(out, &cap, len + l))) return NULL;
            memcpy(out + len, r.s + r.pos, l); len += l; r.pos += l; continue;
        }
        huff lit, dist; uint8_t lens[320];
        if (type == 1) {
            for (int i = 0; i < 288; i++) lens[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            huff_build(&lit, lens, 288); for (int i = 0; i < 30; i++) lens[i] = 5; huff_build(&dist, lens, 30);
        } else if (type == 2) {
            int hlit = (int)br_bits(&r, 5) + 257, hdist = (int)br_bits(&r, 5) + 1, hclen = (int)br_bits(&r, 4) + 4;
            static const uint8_t ord[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
            uint8_t cl[19] = { 0 }; for (int i = 0; i < hclen; i++) cl[ord[i]] = (uint8_t)br_bits(&r, 3);
            huff clh; huff_build(&clh, cl, 19);
            int i = 0;
            while (i < hlit + hdist && !r.err) {
                int sym = huff_decode(&r, &clh);
                if (sym < 0) break;
                if (sym < 16) lens[i++] = (uint8_t)sym;
                else { int rep = 0; uint8_t val = 0;
                    if (sym == 16) { if (!i) { r.err = true; break; } val = lens[i - 1]; rep = 3 + (int)br_bits(&r, 2); }
                    else if (sym == 17) rep = 3 + (int)br_bits(&r, 3); else rep = 11 + (int)br_bits(&r, 7);
                    while (rep-- && i < hlit + hdist) lens[i++] = val; }
            }
            huff_build(&lit, lens, hlit); huff_build(&dist, lens + hlit, hdist);
        } else { r.err = true; break; }
        for (;;) {
            int sym = huff_decode(&r, &lit); if (sym < 0 || r.err) break;
            if (sym < 256) {
                if (len == cap && !(out = inflate_grow(out, &cap, len + 1))) return NULL;
                out[len++] = (uint8_t)sym; continue;
            }
            if (sym == 256) break;
            sym -= 257; if (sym >= 29) { r.err = true; break; }
            int l = len_base[sym] + (int)br_bits(&r, len_extra[sym]);
            int ds = huff_decode(&r, &dist); if (ds < 0 || ds >= 30) { r.err = true; break; }
            size_t d = dist_base[ds] + br_bits(&r, dist_extra[ds]);
            if (d > len || r.err) { r.err = true; break; }
            if (!(out = inflate_grow(out, &cap, len + (size_t)l))) return NULL;
            uint8_t *o = out + len; const uint8_t *from = o - d;
            if (d >= (size_t)l) memcpy(o, from, (size_t)l);
            else if (d == 1) memset(o, from[0], (size_t)l);
            else for (int k = 0; k < l; k++) o[k] = from[k];
            len += (size_t)l;
        }
    }
    if (r.err) { free(out); return NULL; }
    *out_n = len; return out;
}
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
uint8_t *arc__png_read(const uint8_t *d, size_t n, int *w_out, int *h_out, arc_arena *a) {
    if (n < 33 || memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return NULL;
    size_t pos = 8; uint32_t w = 0, h = 0; int depth = 0, ctype = 0, interlace = 0;
    uint8_t *idat = NULL; size_t idat_n = 0; uint8_t pal[256][4]; memset(pal, 255, sizeof pal); int pal_n = 0;
    while (pos + 8 <= n) {
        uint32_t len = be32(d + pos); const uint8_t *type = d + pos + 4, *body = d + pos + 8;
        if (pos + 12 + len > n) break;
        if (!memcmp(type, "IHDR", 4)) { w = be32(body); h = be32(body + 4); depth = body[8]; ctype = body[9]; interlace = body[12]; }
        else if (!memcmp(type, "PLTE", 4)) { pal_n = (int)(len / 3); for (int i = 0; i < pal_n && i < 256; i++) { pal[i][0] = body[i * 3]; pal[i][1] = body[i * 3 + 1]; pal[i][2] = body[i * 3 + 2]; } }
        else if (!memcmp(type, "tRNS", 4) && ctype == 3) { for (uint32_t i = 0; i < len && i < 256; i++) pal[i][3] = body[i]; }
        else if (!memcmp(type, "IDAT", 4)) { idat = (uint8_t *)realloc(idat, idat_n + len); memcpy(idat + idat_n, body, len); idat_n += len; }
        else if (!memcmp(type, "IEND", 4)) break;
        pos += 12 + len;
    }
    if (!w || !h || depth != 8 || interlace || !idat) { free(idat); arc_log("PNG: only 8-bit non-interlaced images are supported"); return NULL; }
    int ch = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!ch) { free(idat); return NULL; }
    size_t stride = (size_t)w * ch, raw_n;
    uint8_t *raw = inflate_zlib(idat, idat_n, (stride + 1) * h, &raw_n); free(idat);
    if (!raw || raw_n < (stride + 1) * h) { free(raw); return NULL; }
    uint8_t *out = a ? (uint8_t *)arc_arena_alloc(a, (size_t)w * h * 4, 16) : (uint8_t *)arc__xalloc((size_t)w * h * 4);   /* NULL arena: heap */
    uint8_t *prevrow = (uint8_t *)calloc(stride, 1), *cur = (uint8_t *)malloc(stride);
    if (!out) { free(raw); free(prevrow); free(cur); return NULL; }
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *in = raw + y * (stride + 1); int f = in[0]; in++;
        for (size_t x = 0; x < stride; x++) {
            int left = x >= (size_t)ch ? cur[x - ch] : 0, up = prevrow[x], ul = x >= (size_t)ch ? prevrow[x - ch] : 0, v = in[x];
            switch (f) {
            case 1: v += left; break; case 2: v += up; break; case 3: v += (left + up) / 2; break;
            case 4: { int p = left + up - ul, pa = abs(p - left), pb = abs(p - up), pc = abs(p - ul); v += (pa <= pb && pa <= pc) ? left : pb <= pc ? up : ul; } break;
            default: break; }
            cur[x] = (uint8_t)v;
        }
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *o = out + ((size_t)y * w + x) * 4; const uint8_t *c = cur + (size_t)x * ch;
            switch (ctype) {
            case 0: o[0] = o[1] = o[2] = c[0]; o[3] = 255; break;
            case 2: o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = 255; break;
            case 3: memcpy(o, pal[c[0]], 4); break;
            case 4: o[0] = o[1] = o[2] = c[0]; o[3] = c[1]; break;
            default: memcpy(o, c, 4); break; }
        }
        uint8_t *t = prevrow; prevrow = cur; cur = t;
    }
    free(raw); free(prevrow); free(cur);
    *w_out = (int)w; *h_out = (int)h; return out;
}
uint8_t *arc__qoi_read(const uint8_t *d, size_t n, int *w_out, int *h_out, arc_arena *a) {
    if (n < 22) return NULL;
    uint32_t w = be32(d + 4), h = be32(d + 8);
    if (!w || !h || (uint64_t)w * h > (1u << 28)) return NULL;
    uint8_t *out = a ? (uint8_t *)arc_arena_alloc(a, (size_t)w * h * 4, 16) : (uint8_t *)arc__xalloc((size_t)w * h * 4); if (!out) return NULL;
    uint8_t idx[64][4]; memset(idx, 0, sizeof idx); uint8_t px[4] = { 0, 0, 0, 255 };
    size_t p = 14, total = (size_t)w * h; int run = 0;
    for (size_t i = 0; i < total; i++) {
        if (run > 0) run--;
        else if (p < n) {
            uint8_t b = d[p++];
            if (b == 0xFE) { px[0] = d[p]; px[1] = d[p + 1]; px[2] = d[p + 2]; p += 3; }
            else if (b == 0xFF) { memcpy(px, d + p, 4); p += 4; }
            else if ((b & 0xC0) == 0x00) memcpy(px, idx[b], 4);
            else if ((b & 0xC0) == 0x40) { px[0] += ((b >> 4) & 3) - 2; px[1] += ((b >> 2) & 3) - 2; px[2] += (b & 3) - 2; }
            else if ((b & 0xC0) == 0x80) { int dg = (b & 0x3F) - 32; uint8_t b2 = d[p++]; px[1] += dg; px[0] += dg - 8 + ((b2 >> 4) & 15); px[2] += dg - 8 + (b2 & 15); }
            else run = b & 0x3F;
            memcpy(idx[(px[0] * 3 + px[1] * 5 + px[2] * 7 + px[3] * 11) % 64], px, 4);
        }
        memcpy(out + i * 4, px, 4);
    }
    *w_out = (int)w; *h_out = (int)h; return out;
}

/* ======================================================================== */
/* 0.9.0 frame description (the `frame` agent command)                       */
/* ======================================================================== */

static void fr_hex(char out[8], uint32_t rgb) { snprintf(out, 8, "#%06x", rgb & 0xFFFFFF); }
static uint32_t fr_pack(const float *c) {
    int r = (int)(arc_clampf(c[0], 0, 1) * 255 + 0.5f), g = (int)(arc_clampf(c[1], 0, 1) * 255 + 0.5f), b = (int)(arc_clampf(c[2], 0, 1) * 255 + 0.5f);
    return (uint32_t)(r << 16 | g << 8 | b);
}
/* a box's screen rectangle in 2D coordinates (arc_width x arc_height, y down); false when it is wholly behind the
   camera or off the screen */
static bool fr_box(const arc_m4 *vp, const float *model, arc_v3 lo, arc_v3 hi, float W, float H, float out[4], float *depth) {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f, zmin = 1e30f; int front = 0;
    for (int k = 0; k < 8; k++) {
        float px = (k & 1) ? hi.x : lo.x, py = (k & 2) ? hi.y : lo.y, pz = (k & 4) ? hi.z : lo.z;
        float wx = model[0] * px + model[4] * py + model[8] * pz + model[12];
        float wy = model[1] * px + model[5] * py + model[9] * pz + model[13];
        float wz = model[2] * px + model[6] * py + model[10] * pz + model[14];
        const float *m = vp->m;
        float cx = m[0] * wx + m[4] * wy + m[8] * wz + m[12], cy = m[1] * wx + m[5] * wy + m[9] * wz + m[13];
        float cw = m[3] * wx + m[7] * wy + m[11] * wz + m[15];
        if (cw <= 1e-4f) continue;
        front++;
        float sx = (cx / cw * 0.5f + 0.5f) * W, sy = (0.5f - cy / cw * 0.5f) * H;
        x0 = arc_minf(x0, sx); y0 = arc_minf(y0, sy); x1 = arc_maxf(x1, sx); y1 = arc_maxf(y1, sy); zmin = arc_minf(zmin, cw);
    }
    if (!front || x1 < 0 || y1 < 0 || x0 > W || y0 > H) return false;
    out[0] = arc_maxf(x0, 0); out[1] = arc_maxf(y0, 0); out[2] = arc_minf(x1, W); out[3] = arc_minf(y1, H); *depth = zmin;
    return true;
}
/* 0.10.7: an item drawn into a render target: its pass (meshes), target, its box in the target's pixels, and whether
   its "box" could be placed on the screen (else "box" is the target box too) */
static void fr_json_target(arc_json *j, int pass, uint32_t tgt, uint32_t ended, const float *tbox) {
    char nm[40]; arc__frame_target_name(tgt, nm, sizeof nm);
    if (pass >= 0) arc_json_num_k(j, "pass", pass);
    arc_json_str_k(j, "target", nm);
    arc_json_arr_k(j, "target_box"); for (int q = 0; q < 4; q++) arc_json_num(j, (double)(int)(tbox[q] + (q >= 2 ? 0.5f : 0.0f))); arc_json_end(j);
    if (ended) arc_json_bool_k(j, "on_screen_box", false);
}
void arc__frame_json(arc_json *j) {
    float W = (float)arc_width(), H = (float)arc_height();
    const arc__pass *P = NULL;
    for (int pi = 0; pi < R.pass_count && !P; pi++) if (!R.passes[pi].target && R.passes[pi].mb_count + R.passes[pi].tb_count > 0) P = &R.passes[pi];
    for (int pi = 0; pi < R.pass_count && !P; pi++) if (!R.passes[pi].target) P = &R.passes[pi];
    arc_json_obj(j);
    arc_json_num_k(j, "tick", (double)arc_tick_count());
    arc_json_arr_k(j, "size"); arc_json_num(j, W); arc_json_num(j, H); arc_json_end(j);
    int targets = 0; for (int pi = 0; pi < R.pass_count; pi++) targets += R.passes[pi].target != 0;
    arc_json_num_k(j, "passes", R.pass_count); arc_json_num_k(j, "target_passes", targets);
    {   /* 0.10.11 lines: thin and thick segments this frame, the widest (pixels) and the brightest glow; dropped draws */
        int thin = 0, tv = 0; for (int pi = 0; pi < R.pass_count; pi++) { thin += R.passes[pi].line_count / 2; tv += R.passes[pi].ltri_count; }
        arc_json_obj_k(j, "lines");
        arc_json_num_k(j, "count", thin + g_lines_thick); arc_json_num_k(j, "thick", g_lines_thick);
        arc_json_num_k(j, "width_max", g_lines_wmax > 0 ? g_lines_wmax : (thin ? 1.0f : 0.0f)); arc_json_num_k(j, "glow_max", g_lines_gmax);
        arc_json_num_k(j, "triangles", tv / 3);
        arc_json_num_k(j, "lines2d", g_lines2d_frame);   /* 0.10.13 arc_draw_lines2d segments */
        arc_json_end(j);
        arc_json_num_k(j, "dropped", R.stats.dropped);
        arc_json_num_k(j, "vertices_2d", R.stats.vertices_2d); arc_json_num_k(j, "vertices_2d_max", arc__max_v2);
    }
    {   /* 0.10.12 post: non-finite scene pixels (the software renderer counts them; on a GPU, with `post nan on`, the
           magenta pixels of the last frame) */
        int nan_px = arc__post_nan_count;
        if (arc__post_nan && arc__gfx && strcmp(arc__gfx->name, "soft")) {
            int w = 0, h = 0; uint8_t *px = arc__gfx_read_frame(&w, &h); nan_px = 0;
            if (px) { for (size_t k = 0; k < (size_t)w * (size_t)h; k++) { const uint8_t *q = px + k * 4; nan_px += q[0] > 200 && q[1] < 60 && q[2] > 200; } free(px); }
        }
        arc_json_obj_k(j, "post"); arc_json_bool_k(j, "nan_mode", arc__post_nan); arc_json_int_k(j, "nan_pixels", nan_px); arc_json_end(j);
    }
    if (P) {
        arc_json_obj_k(j, "camera");
        arc_json_arr_k(j, "eye"); arc_json_num(j, P->eye.x); arc_json_num(j, P->eye.y); arc_json_num(j, P->eye.z); arc_json_end(j);
        arc__rig_json(j);   /* 0.10.0 */
        arc_json_end(j);
        arc__shots_json(j);
        /* meshes: grouped by mesh, on-screen instances with their boxes (nearest first, at most 24 each) */
        static int stamp[ARC__MAX_MESHES], slot_of[ARC__MAX_MESHES]; static int gen = 0; gen++;
        typedef struct { uint32_t mesh; int total, shown; } grp;
        typedef struct { int g, pass; uint32_t tgt, to; float box[4], tbox[4], depth, pos[3]; uint32_t rgb; float alpha; } item;
        grp *gs = (grp *)malloc(sizeof(grp) * 512); item *it = (item *)malloc(sizeof(item) * 4096);
        int ng = 0, ni = 0, skipped = 0;
        /* the screen pass, then (0.10.7, Moonbase Raid) every pass into a render target (not mirrors or viewports, which
           have their own lists): items there carry their pass and target, boxes placed on the screen through the
           target's blit when it was drawn there */
        for (int pi = -1; pi < R.pass_count; pi++) {
        const arc__pass *Q = pi < 0 ? P : &R.passes[pi];
        if (pi >= 0 && (Q == P || !Q->target || Q->mirror || Q->vport || Q->target >= ARC__MAX_TEXTURES)) continue;
        float QW = pi < 0 ? W : (float)arc__textures[Q->target].w, QH = pi < 0 ? H : (float)arc__textures[Q->target].h;
        for (int b = Q->mb_first; gs && it && b < Q->mb_first + Q->mb_count + Q->tb_count; b++) {
            const arc__mesh_batch *mb = &R.mesh_batches[b];
            if ((mb->depth & ARC__Z_FUNC) == ARC__Z_GREATER) continue;   /* x-ray copies */
            const arc__mesh_rec *M = &arc__meshes[mb->mesh];
            if (!M->used) continue;
            int gi;
            if (stamp[mb->mesh] == gen) gi = slot_of[mb->mesh];
            else { if (ng >= 512) { skipped += mb->count; continue; } gi = ng++; stamp[mb->mesh] = gen; slot_of[mb->mesh] = gi; gs[gi].mesh = mb->mesh; gs[gi].total = gs[gi].shown = 0; }
            for (int k = 0; k < mb->count; k++) {
                const arc__instance *in = &R.sorted[mb->first + k];
                if (in->fx[3] > 0) continue;   /* outline hulls */
                gs[gi].total++;
                float box[4], d;
                if (!fr_box(&Q->view_proj, in->model, M->lo, M->hi, QW, QH, box, &d)) continue;
                gs[gi].shown++;
                if (ni < 4096) {
                    item *x = &it[ni++]; x->g = gi; memcpy(x->box, box, sizeof box); memcpy(x->tbox, box, sizeof box); x->depth = d; x->pos[0] = in->model[12]; x->pos[1] = in->model[13]; x->pos[2] = in->model[14]; x->rgb = fr_pack(in->tint); x->alpha = mb->blend ? 1.0f - in->tint[3] : 1.0f;
                    x->pass = pi < 0 ? (int)(P - R.passes) : pi; x->tgt = pi < 0 ? 0 : Q->target; x->to = x->tgt ? fr_map(x->tgt, x->box) : 0;
                }
            }
        }
        }
        arc_json_arr_k(j, "meshes");
        for (int gi = 0; gi < ng; gi++) {
            arc_json_obj(j);
            const char *nm = g_mname[gs[gi].mesh];
            if (nm[0]) arc_json_str_k(j, "name", nm);
            arc_json_num_k(j, "mesh", gs[gi].mesh); arc_json_num_k(j, "count", gs[gi].total); arc_json_num_k(j, "on_screen", gs[gi].shown);
            arc_json_arr_k(j, "items");
            for (int listed = 0; listed < 24;) {   /* nearest first */
                int best = -1;
                for (int k = 0; k < ni; k++) if (it[k].g == gi && it[k].depth >= 0 && (best < 0 || it[k].depth < it[best].depth)) best = k;
                if (best < 0) break;
                item *x = &it[best]; char hx[8]; fr_hex(hx, x->rgb);
                arc_json_obj(j);
                arc_json_arr_k(j, "box"); for (int q = 0; q < 4; q++) arc_json_num(j, (double)(int)(x->box[q] + 0.5f)); arc_json_end(j);
                arc_json_arr_k(j, "pos"); for (int q = 0; q < 3; q++) arc_json_num(j, x->pos[q]); arc_json_end(j);
                if (x->tgt) fr_json_target(j, x->pass, x->tgt, x->to, x->tbox);
                arc_json_str_k(j, "rgb", hx);
                if (x->alpha < 1) arc_json_num_k(j, "alpha", x->alpha);
                arc_json_end(j);
                x->depth = -1; listed++;
            }
            arc_json_end(j);
            arc_json_end(j);
        }
        arc_json_end(j);
        free(gs); free(it);
        if (skipped) arc_json_num_k(j, "meshes_not_listed", skipped);
        {   /* 0.10.7: each pass into a render target: its size, camera, meshes and where the target was drawn */
            int nt = 0;
            for (int pi = 0; pi < R.pass_count; pi++) {
                const arc__pass *Q = &R.passes[pi];
                if (!Q->target || Q->mirror || Q->vport || Q->target >= ARC__MAX_TEXTURES) continue;
                if (!nt++) arc_json_arr_k(j, "targets");
                char nm[40]; arc__frame_target_name(Q->target, nm, sizeof nm);
                int meshes = 0; for (int b = Q->mb_first; b < Q->mb_first + Q->mb_count + Q->tb_count; b++) meshes += R.mesh_batches[b].count;
                arc_json_obj(j); arc_json_num_k(j, "pass", pi); arc_json_str_k(j, "target", nm);
                arc_json_arr_k(j, "size"); arc_json_num(j, arc__textures[Q->target].w); arc_json_num(j, arc__textures[Q->target].h); arc_json_end(j);
                arc_json_arr_k(j, "eye"); arc_json_num(j, Q->eye.x); arc_json_num(j, Q->eye.y); arc_json_num(j, Q->eye.z); arc_json_end(j);
                arc_json_num_k(j, "meshes", meshes);
                float bx[4] = { 0, 0, (float)arc__textures[Q->target].w, (float)arc__textures[Q->target].h };
                if (!fr_map(Q->target, bx)) { arc_json_arr_k(j, "on_screen_at"); for (int q = 0; q < 4; q++) arc_json_num(j, (double)(int)(bx[q] + 0.5f)); arc_json_end(j); }
                arc_json_end(j);
            }
            if (nt) arc_json_end(j);
        }
        /* lights */
        arc_json_arr_k(j, "lights");
        arc_json_obj(j); arc_json_str_k(j, "kind", "sun");
        arc_json_arr_k(j, "dir"); for (int q = 0; q < 3; q++) arc_json_num(j, R.light_dir[q]); arc_json_end(j);
        { char hx[8]; fr_hex(hx, fr_pack(R.light_col)); arc_json_str_k(j, "rgb", hx); fr_hex(hx, fr_pack(R.ambient)); arc_json_str_k(j, "ambient", hx); }
        arc_json_end(j);
        for (int l = 0; l < R.lt_count && l < 64; l++) {
            const float *L = R.lt[l]; arc_json_obj(j);
            arc_json_str_k(j, "kind", L[11] <= -0.9f ? "point" : "spot");
            arc_json_arr_k(j, "pos"); for (int q = 0; q < 3; q++) arc_json_num(j, L[q]); arc_json_end(j);
            arc_json_num_k(j, "range", L[3] > 0 ? 1.0 / sqrt(L[3]) : 0);
            float mx = arc_maxf(L[4], arc_maxf(L[5], L[6])); float c[3] = { mx > 0 ? L[4] / mx : 0, mx > 0 ? L[5] / mx : 0, mx > 0 ? L[6] / mx : 0 };
            char hx[8]; fr_hex(hx, fr_pack(c)); arc_json_str_k(j, "rgb", hx); arc_json_num_k(j, "intensity", mx);
            if (L[12] >= 0) arc_json_bool_k(j, "shadow", true);   /* 0.9.5 */
            arc_json_end(j);
        }
        arc_json_end(j);
        if (R.lt_count > 64) arc_json_num_k(j, "lights_not_listed", R.lt_count - 64);
    }
    if (R.vpl_count) {   /* 0.10.2: each viewport's rectangle, camera and meshes (by name, with counts) */
        arc_json_arr_k(j, "viewports");
        for (int i = 0; i < R.vpl_count; i++) {
            const struct vport_rec *v = &R.vpl[i];
            arc_json_obj(j);
            arc_json_arr_k(j, "rect"); arc_json_num(j, (double)v->x); arc_json_num(j, (double)v->y); arc_json_num(j, (double)v->w); arc_json_num(j, (double)v->h); arc_json_end(j);
            arc_json_obj_k(j, "camera");
            arc_json_arr_k(j, "eye"); arc_json_num(j, v->eye.x); arc_json_num(j, v->eye.y); arc_json_num(j, v->eye.z); arc_json_end(j);
            arc_json_arr_k(j, "dir"); arc_json_num(j, v->fwd.x); arc_json_num(j, v->fwd.y); arc_json_num(j, v->fwd.z); arc_json_end(j);
            arc_json_end(j);
            arc_json_arr_k(j, "meshes");
            static int vcount[ARC__MAX_MESHES], von[ARC__MAX_MESHES]; static uint32_t vlist[512]; int nl = 0;
            for (int pi = 0; pi < R.pass_count; pi++) {
                const arc__pass *q = &R.passes[pi]; if (q->vport != i + 1 || q->mirror) continue;
                for (int b = q->mb_first; b < q->mb_first + q->mb_count + q->tb_count; b++) {
                    const arc__mesh_batch *mb = &R.mesh_batches[b];
                    if ((mb->depth & ARC__Z_FUNC) == ARC__Z_GREATER || !arc__meshes[mb->mesh].used) continue;
                    if (!vcount[mb->mesh] && !von[mb->mesh]) { if (nl >= 512) continue; vlist[nl++] = mb->mesh; }
                    for (int k = 0; k < mb->count; k++) {
                        const arc__instance *in = &R.sorted[mb->first + k]; float box[4], d;
                        if (in->fx[3] > 0) continue;
                        vcount[mb->mesh]++;
                        if (fr_box(&v->vp, in->model, arc__meshes[mb->mesh].lo, arc__meshes[mb->mesh].hi, (float)v->w, (float)v->h, box, &d)) von[mb->mesh]++;
                    }
                }
            }
            for (int k = 0; k < nl; k++) {
                uint32_t m = vlist[k];
                if (vcount[m]) {
                    arc_json_obj(j);
                    if (g_mname[m][0]) arc_json_str_k(j, "name", g_mname[m]);
                    arc_json_num_k(j, "mesh", (double)m); arc_json_num_k(j, "count", (double)vcount[m]); arc_json_num_k(j, "on_screen", (double)von[m]);
                    arc_json_end(j);
                }
                vcount[m] = von[m] = 0;
            }
            arc_json_end(j);
            arc_json_end(j);
        }
        arc_json_end(j);
    }
    arc__markers_json(j);   /* 0.9.9 ground markers */
    /* 2D, as drawn (only while recording) */
    arc_json_arr_k(j, "texts");
    for (int i = 0; i < g_fr.nt; i++) {
        const frec_text *t = &g_fr.t[i]; char hx[8]; fr_hex(hx, t->rgb);
        arc_json_obj(j); arc_json_str_k(j, "text", g_fr.chars + t->off);
        float bx[4] = { t->x, t->y, t->x + t->w, t->y + t->h }, tb[4] = { t->x, t->y, t->x + t->w, t->y + t->h }; uint32_t to = t->tgt ? fr_map(t->tgt, bx) : 0;
        arc_json_arr_k(j, "box"); arc_json_num(j, (int)bx[0]); arc_json_num(j, (int)bx[1]); arc_json_num(j, (int)(bx[2] + 0.5f)); arc_json_num(j, (int)(bx[3] + 0.5f)); arc_json_end(j);
        if (t->tgt) fr_json_target(j, -1, t->tgt, to, tb);
        if (t->scale > 0) arc_json_num_k(j, "scale", t->scale);
        arc_json_str_k(j, "rgb", hx); arc_json_end(j);
    }
    arc_json_end(j);
    for (int pass = 0; pass < 2; pass++) {
        arc_json_arr_k(j, pass ? "rects" : "sprites");
        int n = pass ? g_fr.nr : g_fr.ns; const frec_quad *qs = pass ? g_fr.rc : g_fr.sp;
        for (int i = 0; i < n; i++) {
            const frec_quad *q = &qs[i]; char hx[8]; fr_hex(hx, q->rgb);
            arc_json_obj(j);
            if (!pass && q->tex < ARC__MAX_TEXTURES && g_tname[q->tex][0]) arc_json_str_k(j, "texture", g_tname[q->tex]);
            else if (!pass) arc_json_num_k(j, "texture_id", q->tex);
            float bx[4] = { q->x, q->y, q->x + q->w, q->y + q->h }, tb[4] = { q->x, q->y, q->x + q->w, q->y + q->h }; uint32_t to = q->tgt ? fr_map(q->tgt, bx) : 0;
            arc_json_arr_k(j, "box"); arc_json_num(j, (int)bx[0]); arc_json_num(j, (int)bx[1]); arc_json_num(j, (int)(bx[2] + 0.5f)); arc_json_num(j, (int)(bx[3] + 0.5f)); arc_json_end(j);
            if (q->tgt) fr_json_target(j, -1, q->tgt, to, tb);
            arc_json_str_k(j, "rgb", hx);
            if ((q->rgb >> 24) && (q->rgb >> 24) != 255) arc_json_num_k(j, "alpha", (double)(q->rgb >> 24) / 255.0);
            arc_json_end(j);
        }
        arc_json_end(j);
    }
    arc__ui_json(j, "ui");
    arc__ui_panels_json(j);   /* 0.10.12 */
    arc_json_end(j);
}
void arc_texture_set_name(arc_texture t, const char *name) { if (tex_ok(t)) { size_t n = name ? strlen(name) : 0; if (n > 31) n = 31; memcpy(g_tname[t.id], name ? name : "", n); g_tname[t.id][n] = 0; } }
const char *arc_texture_name(arc_texture t) { return tex_ok(t) ? g_tname[t.id] : ""; }
