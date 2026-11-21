/* ARC software renderer: used by --headless for screenshots, and as a reference for the GPU backends.
 * Same shading model as the GPU shaders: per-pixel Lambert + ambient + emissive + rim, linear or exponential fog,
 * textures with mipmaps (one level per triangle here), skinning, cut-out alpha, see-through and additive passes.
 * Render targets draw straight into their texture's RGBA buffer, so later passes can sample them. */
#include "arc_internal.h"

static struct {
    int w, h; uint8_t *screen; float *screen_depth;     /* the screen */
    float *tdepth[ARC__MAX_TEXTURES]; int tdw[ARC__MAX_TEXTURES], tdh[ARC__MAX_TEXTURES];   /* depth for targets */
    /* the buffers being drawn into now */
    int cw, ch; uint8_t *color; float *depth;
    float *fcolor;                                      /* drawing 3D into the HDR screen (post-processing on) */
    int blend; bool clip; int x0, y0, x1, y1;   /* clip box, exclusive max */
    float sdf2d;                                /* 0.10.1: the 2D batch is distance-field text */
    int na;                                     /* 0.10.1: attributes in use (A_BASE, or A_COUNT with a paint pattern) */
    float *smap; int sres, sstride;                     /* shadow map: depth 0..1, rows bottom-up like a GL texture */
    float *lmap; int lres;                              /* 0.9.5 light shadow atlas: 6 x 4 faces of lres (see arc__pass.lsh_n) */
    float sdis;                                         /* the shadow instance's dissolve */
    int sx0, sy0, scn;                                  /* the cascade being drawn: its square in the atlas */
    float *hdr; int hw, hh;                             /* HDR screen for post-processing */
    float *ao; int aw, ah;                              /* ssao: r = ao * geometry, g = geometry (hw x hh) */
    float *bloom[ARC__BLOOM_LEVELS], *bup[ARC__BLOOM_LEVELS]; int bw[ARC__BLOOM_LEVELS], bh[ARC__BLOOM_LEVELS];
    float *tdown[ARC__TILT_LEVELS], *tup[ARC__TILT_LEVELS]; int tw[ARC__TILT_LEVELS], th[ARC__TILT_LEVELS];   /* tilt-shift blur */
    /* 0.8.2: post buffers of targets with their own post effects (swapped into the fields above while used) */
    struct soft_pset { float *hdr; int hw, hh; float *ao; int aw, ah; float *bloom[ARC__BLOOM_LEVELS], *bup[ARC__BLOOM_LEVELS]; int bw[ARC__BLOOM_LEVELS], bh[ARC__BLOOM_LEVELS];
                       float *tdown[ARC__TILT_LEVELS], *tup[ARC__TILT_LEVELS]; int tw[ARC__TILT_LEVELS], th[ARC__TILT_LEVELS]; } tps[ARC__PSET_MAX];
    uint32_t tps_id[ARC__PSET_MAX];
} S;
typedef struct soft_pset soft_pset;
static void pset_swap(soft_pset *p) {   /* exchange the screen's post buffers with a target's */
    soft_pset t = *p;
    p->hdr = S.hdr; p->hw = S.hw; p->hh = S.hh; p->ao = S.ao; p->aw = S.aw; p->ah = S.ah;
    memcpy(p->bloom, S.bloom, sizeof S.bloom); memcpy(p->bup, S.bup, sizeof S.bup); memcpy(p->bw, S.bw, sizeof S.bw); memcpy(p->bh, S.bh, sizeof S.bh);
    memcpy(p->tdown, S.tdown, sizeof S.tdown); memcpy(p->tup, S.tup, sizeof S.tup); memcpy(p->tw, S.tw, sizeof S.tw); memcpy(p->th, S.th, sizeof S.th);
    S.hdr = t.hdr; S.hw = t.hw; S.hh = t.hh; S.ao = t.ao; S.aw = t.aw; S.ah = t.ah;
    memcpy(S.bloom, t.bloom, sizeof S.bloom); memcpy(S.bup, t.bup, sizeof S.bup); memcpy(S.bw, t.bw, sizeof S.bw); memcpy(S.bh, t.bh, sizeof S.bh);
    memcpy(S.tdown, t.tdown, sizeof S.tdown); memcpy(S.tup, t.tup, sizeof S.tup); memcpy(S.tw, t.tw, sizeof S.tw); memcpy(S.th, t.th, sizeof S.th);
}
static void pset_free(soft_pset *p) {
    free(p->hdr); free(p->ao);
    for (int i = 0; i < ARC__BLOOM_LEVELS; i++) { free(p->bloom[i]); free(p->bup[i]); }
    for (int i = 0; i < ARC__TILT_LEVELS; i++) { free(p->tdown[i]); free(p->tup[i]); }
    memset(p, 0, sizeof *p);
}
static soft_pset *pset_for(uint32_t id, bool make) {   /* the post buffers of target id (a free slot if make) */
    for (int i = 0; i < ARC__PSET_MAX; i++) if (S.tps_id[i] == id) return &S.tps[i];
    if (make) for (int i = 0; i < ARC__PSET_MAX; i++) if (!S.tps_id[i]) { S.tps_id[i] = id; return &S.tps[i]; }
    return NULL;
}

static bool soft_init(void) { return true; }
static void soft_shutdown(void) {
    free(S.screen); free(S.screen_depth); free(S.smap); free(S.hdr); free(S.ao);
    for (int i = 0; i < ARC__BLOOM_LEVELS; i++) { free(S.bloom[i]); free(S.bup[i]); }
    for (int i = 0; i < ARC__TILT_LEVELS; i++) { free(S.tdown[i]); free(S.tup[i]); }
    for (int i = 0; i < ARC__MAX_TEXTURES; i++) free(S.tdepth[i]);
    for (int i = 0; i < ARC__PSET_MAX; i++) pset_free(&S.tps[i]);
    memset(&S, 0, sizeof S);
}
static bool soft_mesh_create(uint32_t id, const arc_vertex *v, int vc, const uint32_t *idx, int ic, const uint8_t *skin) { (void)id; (void)v; (void)vc; (void)idx; (void)ic; (void)skin; return true; }
static void soft_mesh_update(uint32_t id, const arc_vertex *v, int vc) { (void)id; (void)v; (void)vc; }
static void soft_mesh_destroy(uint32_t id) { (void)id; }
static bool soft_tex_create(uint32_t id, int w, int h, const uint8_t *p, int flags) { (void)id; (void)w; (void)h; (void)p; (void)flags; return true; }
static void soft_tex_update(uint32_t id, int w, int h, const uint8_t *p) { (void)id; (void)w; (void)h; (void)p; }
static bool soft_target_create(uint32_t id, int w, int h, bool s) { (void)id; (void)w; (void)h; (void)s; return true; }
static void soft_tex_destroy(uint32_t id) {
    free(S.tdepth[id]); S.tdepth[id] = NULL;
    soft_pset *ps = pset_for(id, false); if (ps) { pset_free(ps); for (int i = 0; i < ARC__PSET_MAX; i++) if (S.tps_id[i] == id) S.tps_id[i] = 0; }
}
static void soft_tex_set_smooth(uint32_t id, bool s) { (void)id; (void)s; }

typedef struct { float x, y, z, w; float r, g, b, a; float u, v; } sv;   /* 2D: screen vertex + colour + uv */

static inline uint8_t to8(float f) { return (uint8_t)(arc_clampf(f, 0, 1) * 255.0f + 0.5f); }

/* texel fetch with the texture's wrap mode */
static bool g_wrap;   /* 0.8.8: hex / world-mapped colour sampling repeats whatever the texture's addressing */
static inline const uint8_t *texel(const arc__tex_rec *t, int level, int x, int y) {
    int w = t->mw[level], h = t->mh[level];
    if ((t->flags & ARC_TEX_REPEAT) || g_wrap) { x %= w; if (x < 0) x += w; y %= h; if (y < 0) y += h; }
    else { x = arc_clampi(x, 0, w - 1); y = arc_clampi(y, 0, h - 1); }
    return t->mip[level] + ((size_t)y * w + x) * 4;
}
static void sample_lod(const arc__tex_rec *t, int level, float u, float v, float out[4]) {
    if (!t || !t->rgba) { out[0] = out[1] = out[2] = out[3] = 1; return; }
    if (level >= t->levels) level = t->levels - 1;
    if (level < 0 || !t->mip[level]) level = 0;
    if (!t->mip[0]) { out[0] = out[1] = out[2] = out[3] = 1; return; }
    int w = t->mw[level], h = t->mh[level];
    if (!t->smooth) {
        const uint8_t *p = texel(t, level, (int)floorf(u * (float)w), (int)floorf(v * (float)h));
        out[0] = p[0] / 255.0f; out[1] = p[1] / 255.0f; out[2] = p[2] / 255.0f; out[3] = p[3] / 255.0f;
        return;
    }
    float fx = u * (float)w - 0.5f, fy = v * (float)h - 0.5f; int ix = (int)floorf(fx), iy = (int)floorf(fy); float ax = fx - (float)ix, ay = fy - (float)iy;
    float acc[4] = { 0, 0, 0, 0 };
    for (int k = 0; k < 4; k++) {
        const uint8_t *p = texel(t, level, ix + (k & 1), iy + (k >> 1));
        float wgt = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
        for (int c = 0; c < 4; c++) acc[c] += p[c] / 255.0f * wgt;
    }
    memcpy(out, acc, sizeof acc);
}
static void sample(const arc__tex_rec *t, float u, float v, float out[4]) { sample_lod(t, 0, u, v, out); }

/* rasterise a 2D screen-space triangle (blended, textured, clipped) */
static void raster2d(const sv *a, const sv *b, const sv *c, const arc__tex_rec *tex) {
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 1e-12f) return;
    if (area > 0) { const sv *t = b; b = c; c = t; area = -area; }
    int minx = (int)floorf(arc_minf(a->x, arc_minf(b->x, c->x))), maxx = (int)ceilf(arc_maxf(a->x, arc_maxf(b->x, c->x)));
    int miny = (int)floorf(arc_minf(a->y, arc_minf(b->y, c->y))), maxy = (int)ceilf(arc_maxf(a->y, arc_maxf(b->y, c->y)));
    int lx = 0, ly = 0, hx = S.cw, hy = S.ch;
    if (S.clip) { lx = arc_maxi(lx, S.x0); ly = arc_maxi(ly, S.y0); hx = arc_mini(hx, S.x1); hy = arc_mini(hy, S.y1); }
    minx = arc_maxi(minx, lx); miny = arc_maxi(miny, ly); maxx = arc_mini(maxx, hx - 1); maxy = arc_mini(maxy, hy - 1);
    float inv = 1.0f / area;
    /* fill rule: a pixel centre exactly on an edge belongs to only one of the two triangles sharing it
       (they walk the edge in opposite directions), so blended quads have no double-drawn seam */
    #define OWN(dx, dy) ((dy) > 0 || ((dy) == 0 && (dx) > 0))
    bool own0 = OWN(c->x - b->x, c->y - b->y), own1 = OWN(a->x - c->x, a->y - c->y), own2 = OWN(b->x - a->x, b->y - a->y);
    for (int y = miny; y <= maxy; y++) {
        float py = (float)y + 0.5f;
        for (int x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f;
            float e0 = (c->x - b->x) * (py - b->y) - (c->y - b->y) * (px - b->x);
            float e1 = (a->x - c->x) * (py - c->y) - (a->y - c->y) * (px - c->x);
            float e2 = (b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x);
            if (e0 > 0 || e1 > 0 || e2 > 0) continue;   /* area < 0, so inside means e <= 0 */
            if ((e0 == 0 && !own0) || (e1 == 0 && !own1) || (e2 == 0 && !own2)) continue;
            float w0 = e0 * inv, w1 = e1 * inv, w2 = 1.0f - w0 - w1;
            uint8_t *o = S.color + ((size_t)y * S.cw + x) * 4;
            float col[4] = { w0 * a->r + w1 * b->r + w2 * c->r, w0 * a->g + w1 * b->g + w2 * c->g,
                             w0 * a->b + w1 * b->b + w2 * c->b, w0 * a->a + w1 * b->a + w2 * c->a }, t[4];
            float su = w0 * a->u + w1 * b->u + w2 * c->u;
            if (su < -2048.0f) t[0] = t[1] = t[2] = t[3] = 1.0f;   /* plain triangles (ARC__UV_PLAIN): white */
            else sample(tex, su, w0 * a->v + w1 * b->v + w2 * c->v, t);
            if (S.sdf2d > 0 && su >= -2048.0f) { float dd = (t[3] - 0.5f) * S.sdf2d, e = arc_clampf(dd + 0.5f, 0, 1); t[0] = t[1] = t[2] = 1.0f; t[3] = e * e * (3.0f - 2.0f * e); }   /* 0.10.1 distance-field text */
            for (int k = 0; k < 4; k++) col[k] *= t[k];
            float d[4] = { o[0] / 255.0f, o[1] / 255.0f, o[2] / 255.0f, o[3] / 255.0f }, al = col[3];
            switch (S.blend) {
            case ARC__BLEND_ADD: if (al <= 0) continue; for (int k = 0; k < 3; k++) d[k] += col[k] * al; d[3] += al; break;
            case ARC__BLEND_MULTIPLY: for (int k = 0; k < 4; k++) d[k] *= col[k]; break;
            case ARC__BLEND_NONE: memcpy(d, col, sizeof d); break;
            default: if (al <= 0) continue; for (int k = 0; k < 3; k++) d[k] = col[k] * al + d[k] * (1 - al); d[3] = al + d[3] * (1 - al); break;
            }
            o[0] = to8(d[0]); o[1] = to8(d[1]); o[2] = to8(d[2]); o[3] = to8(d[3]);
        }
    }
}

/* ---- 3D: per-pixel shading, the same formula as the GPU shaders ---- */
enum { A_WX, A_WY, A_WZ, A_NX, A_NY, A_NZ, A_R, A_G, A_B, A_A, A_U, A_V, A_OX, A_OY, A_OZ, A_BX, A_BY, A_BZ, A_PM, A_COUNT };   /* O: the model's own position (dissolve); 0.10.1 B: bind pose, PM: in the pattern's part mask (only with a pattern) */
#define A_BASE A_BX   /* attributes interpolated without a paint pattern */
typedef struct { float x, y, z, w; float at[A_COUNT]; } v3;   /* clip-space position + attributes */
typedef struct {                       /* what stays the same over one instance */
    const arc__pass *P; const arc__instance *in; const arc__tex_rec *tex;
    int mode;                          /* 0 cull back faces, 2 cull front faces, 3 double-sided */
    int blend;                         /* ARC__MB_* */
    const float *mat;                  /* material (arc__mesh_rec.mat) or NULL */
    const arc__tex_rec *mt[3];         /* its normal, orm and emissive maps (NULL = none) */
    float T[3], B[3]; bool tb;         /* this triangle's dP/du and -dP/dv (normal maps) */
    int zf, zw;                        /* depth compare (ARC__Z_*) and whether it writes depth (0.6.3) */
    float soft;                        /* 0.7 soft particle fade distance (0 = off) */
    int zrev;                          /* 0.7.2 reversed-Z pass: depth 1 near .. 0 far, compares swapped (see arc__pass.zrev) */
    const float *m2;                   /* 0.8.4 arc__mesh_rec.mat2 or NULL (no detail map, no hex tiling) */
    const arc__tex_rec *dt, *mk;       /* its detail map and the map's breakup mask (NULL = none) */
    int dplane;                        /* this triangle's detail plane: 0 (x, z), 1 (z, y), 2 (x, y); -1 = mesh uv */
    int bplane;                        /* 0.8.8 the same for a world-mapped colour texture (-1 = mesh uv) */
    const arc__tex_rec *rt; float ramt;   /* 0.8.7 the pass's reflection and this mesh's amount (NULL / 0 = none) */
    int px, py;                        /* the pixel being shaded (reflections) */
    bool cut, back, flipw;             /* 0.9.9 cutaway on this mesh; the triangle being drawn faces away; winding flipped */
    const arc__tex_rec *pt; float pt2[4], pt3[4]; bool pt3on;   /* 0.8.8 the batch's paint mask and second colour (NULL = none) */
    const arc__pattern *pat; int paxis;   /* 0.10.1 the batch's paint pattern (NULL = none); this triangle's face axis (bind pose) */
    const arc__tex_rec *sl[ARC__MAX_LAYERS]; const float *m3; int nsl;   /* 0.9.6 splat layers (CPU copies), ARC__MAT3_*, count (0 = off) */
} shade_ctx;
/* soft particles (all renderers, see arc__pass.soft): fade with the distance along the pixel's ray from this
   point to what the depth buffer holds there */
static float soft_fade(const arc__pass *P, int x, int y, float depth, const float *at, float soft) {
    const float *m = P->ivp;   /* column-major */
    float nx = ((float)x + 0.5f) / (float)S.cw * 2.0f - 1.0f, ny = 1.0f - ((float)y + 0.5f) / (float)S.ch * 2.0f, nz = P->zrev ? depth * 2.0f - 1.0f : depth;
    float w = m[3] * nx + m[7] * ny + m[11] * nz + m[15];
    if (fabsf(w) < 1e-12f) return 1.0f;
    float sx = (m[0] * nx + m[4] * ny + m[8] * nz + m[12]) / w, sy = (m[1] * nx + m[5] * ny + m[9] * nz + m[13]) / w, sz = (m[2] * nx + m[6] * ny + m[10] * nz + m[14]) / w;
    float dx = sx - at[0], dy = sy - at[1], dz = sz - at[2];
    return arc_clampf(sqrtf(dx * dx + dy * dy + dz * dz) / soft, 0, 1);
}
static int lod_for(const arc__tex_rec *t, float uv_ratio) {   /* mip level: texels covered per pixel */
    if (!t || t->levels <= 1 || uv_ratio <= 0) return 0;
    float r = uv_ratio * (float)t->w * (float)t->h; int lod = r > 1 ? (int)(0.5f * log2f(r) + 0.5f) : 0;
    return lod >= t->levels ? t->levels - 1 : lod;
}
/* the environment gradient (pbr reflections), as the sky: t = ray.y */
static void env_col(const arc__pass *P, float t, float out[3]) {
    const float *to = t >= 0 ? P->env : P->env + 8; float k = fminf(fabsf(t), 1.0f);
    for (int i = 0; i < 3; i++) out[i] = P->env[4 + i] + (to[i] - P->env[4 + i]) * k;
}
static inline float pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

/* shadow lookup, exactly as a GL sampler2DShadow with linear filtering: compare 4 texels, blend the results;
   3x3 of those (soft edges) */
static float shadow_cmp(float u, float v, float ref) {
    int n = S.sres; float fx = u * (float)n - 0.5f, fy = v * (float)n - 0.5f; int ix = (int)floorf(fx), iy = (int)floorf(fy); float ax = fx - (float)ix, ay = fy - (float)iy, r = 0;
    for (int k = 0; k < 4; k++) {
        int x = arc_clampi(ix + (k & 1), 0, n - 1), y = arc_clampi(iy + (k >> 1), 0, n - 1);
        float lit = ref <= S.smap[(size_t)y * n + x] ? 1.0f : 0.0f;
        r += lit * ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
    }
    return r;
}
/* 0.6 cascades / softness (all renderers): try each cascade in turn; inside its square less a margin (the filter's
   reach): pcf there (3x3 bilinear compares 1 texel apart, or with softness a 5x5 tent of them 1 + 2 softness texels
   apart; softness also scales the depth bias by 1 + 4 s and the normal offset by 1 + 2 s), blended toward the next
   cascade over the last 0.1 of the square */
static float pcf_casc(const arc__pass *P, int i, float u, float v, float d) {
    float sc = P->shadow_n > 1 ? 0.5f : 1.0f, t = P->shadow[3], ref = d - P->shadow_cp[i * 4] * (1.0f + 4.0f * P->shadow_soft);
    float au = P->shadow_cp[i * 4 + 2] + u * sc, av = P->shadow_cp[i * 4 + 3] + v * sc, acc = 0;
    if (P->shadow_soft <= 0) { for (int j = -1; j <= 1; j++) for (int k = -1; k <= 1; k++) acc += shadow_cmp(au + (float)k * t, av + (float)j * t, ref); return acc / 9.0f; }
    float sp = (1.0f + 2.0f * P->shadow_soft) * t;
    for (int j = -2; j <= 2; j++) for (int k = -2; k <= 2; k++) acc += (float)((3 - abs(k)) * (3 - abs(j))) * shadow_cmp(au + (float)k * sp, av + (float)j * sp, ref);
    return acc / 81.0f;
}
static bool casc_uv(const arc__pass *P, int i, arc_v3 wp, arc_v3 n, float *u, float *v, float *d, float *edge) {
    arc_v3 q = arc_v3_madd(wp, n, P->shadow_cp[i * 4 + 1] * (1.0f + 2.0f * P->shadow_soft)); const float *m = P->shadow_cvp + i * 16;
    float x = m[0] * q.x + m[4] * q.y + m[8] * q.z + m[12], y = m[1] * q.x + m[5] * q.y + m[9] * q.z + m[13], z = m[2] * q.x + m[6] * q.y + m[10] * q.z + m[14];
    float sc = P->shadow_n > 1 ? 0.5f : 1.0f, mg = ((P->shadow_soft > 0 ? 2.0f * (1.0f + 2.0f * P->shadow_soft) : 1.0f) + 1.5f) * P->shadow[3] / sc;
    *u = x * 0.5f + 0.5f; *v = y * 0.5f + 0.5f; *d = z * 0.5f + 0.5f;
    *edge = arc_minf(arc_minf(*u - mg, 1 - mg - *u), arc_minf(*v - mg, 1 - mg - *v));
    return *edge > 0 && *d < 1;
}
static float shadow_vis(const arc__pass *P, arc_v3 wp, arc_v3 n) {
    if (P->shadow[0] <= 0 || !S.smap) return 1.0f;
    if (P->shadow_n > 1 || P->shadow_soft > 0) {
        for (int i = 0; i < P->shadow_n; i++) {
            float u, v, d, e;
            if (!casc_uv(P, i, wp, n, &u, &v, &d, &e)) continue;
            float vis = pcf_casc(P, i, u, v, d);
            if (i + 1 < P->shadow_n && e < 0.1f) {
                float u2, v2, d2, e2, vis2 = 1.0f;
                if (casc_uv(P, i + 1, wp, n, &u2, &v2, &d2, &e2)) vis2 = pcf_casc(P, i + 1, u2, v2, d2);
                vis = vis2 + (vis - vis2) * (e / 0.1f);
            }
            return 1.0f - P->shadow[0] * (1.0f - vis);
        }
        return 1.0f;
    }
    arc_v3 q = arc_v3_madd(wp, n, P->shadow[2]);
    const float *m = P->shadow_vp;
    float x = m[0] * q.x + m[4] * q.y + m[8] * q.z + m[12], y = m[1] * q.x + m[5] * q.y + m[9] * q.z + m[13], z = m[2] * q.x + m[6] * q.y + m[10] * q.z + m[14];
    float u = x * 0.5f + 0.5f, v = y * 0.5f + 0.5f, d = z * 0.5f + 0.5f;
    if (u <= 0 || u >= 1 || v <= 0 || v >= 1 || d >= 1) return 1.0f;
    float t = P->shadow[3], acc = 0, ref = d - P->shadow[1];
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) acc += shadow_cmp(u + (float)i * t, v + (float)j * t, ref);
    return 1.0f - P->shadow[0] * (1.0f - acc / 9.0f);
}
/* dissolve noise: see arc_internal.h */
static float dn_corner(int x, int y, int z) {
    uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)z * 83492791u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (float)(h & 0xFFFFFFu) / 16777215.0f;
}
static float dn_value(float px, float py, float pz) {
    float fx = floorf(px), fy = floorf(py), fz = floorf(pz); int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    float u = px - fx, v = py - fy, w = pz - fz; u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v); w = w * w * (3 - 2 * w);
    float a = dn_corner(ix, iy, iz) + (dn_corner(ix + 1, iy, iz) - dn_corner(ix, iy, iz)) * u;
    float b = dn_corner(ix, iy + 1, iz) + (dn_corner(ix + 1, iy + 1, iz) - dn_corner(ix, iy + 1, iz)) * u;
    float c = dn_corner(ix, iy, iz + 1) + (dn_corner(ix + 1, iy, iz + 1) - dn_corner(ix, iy, iz + 1)) * u;
    float d = dn_corner(ix, iy + 1, iz + 1) + (dn_corner(ix + 1, iy + 1, iz + 1) - dn_corner(ix, iy + 1, iz + 1)) * u;
    float ab = a + (b - a) * v, cd = c + (d - c) * v;
    return ab + (cd - ab) * w;
}
static float dn_value(float x, float y, float z);
/* 0.10.1 a paint pattern at a bind-pose point (as the GPU shaders): 1 = the pattern's colour */
static float pattern_at(const arc__pattern *P, float px, float py, float pz, int axis) {
    int k = (int)(P->p[0][0] + 0.5f); float is = P->p[0][1], w = P->p[0][2], o = P->p[0][3], ca = P->p[1][0], sa = P->p[1][1];
    if (k == ARC_PAINT_STRIPES) {
        float t = (px * ca + py * sa) * is + o, m = (t - floorf(t)) < w ? 1.0f : 0.0f;
        if (P->p[1][2] > 0.5f) { float t2 = (py * ca - px * sa) * is + o; if ((t2 - floorf(t2)) < w) m = 1.0f; }
        return m;
    }
    if (k == ARC_PAINT_CHECKER || k == ARC_PAINT_DOTS) {
        float u = axis == 0 ? pz : px, v = axis == 1 ? pz : py;
        float ru = (u * ca - v * sa) * is + o, rv = (u * sa + v * ca) * is + o;
        if (k == ARC_PAINT_CHECKER) { float c = floorf(ru) + floorf(rv); return c - 2.0f * floorf(c * 0.5f); }
        float fu = ru - floorf(ru) - 0.5f, fv = rv - floorf(rv) - 0.5f, r = sqrtf(w / 3.14159265f);
        return fu * fu + fv * fv < r * r ? 1.0f : 0.0f;
    }
    if (k == ARC_PAINT_SPLIT) return px * ca + py * sa > o ? 1.0f : 0.0f;
    if (k == ARC_PAINT_NOISE) return dn_value(px * is + P->p[3][0] + o, py * is + P->p[3][1], pz * is + P->p[3][2]) < w ? 1.0f : 0.0f;
    return 0;
}
static float dissolve_noise(float x, float y, float z) { return dn_value(x * 5, y * 5, z * 5) * 0.65f + dn_value(x * 11 + 17, y * 11 + 17, z * 11 + 17) * 0.35f; }
/* 0.8.4 hex tiling (see arc_internal.h, ARC__MAT2_*): the same cells, turns and weights as the GPU shaders */
static uint32_t hx_hash(int x, int y) {
    uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return h;
}
static float hx_gain(float x, float k) { x = arc_clampf(x, 0.0f, 1.0f); float s = 0.5f * powf(arc_maxf(2.0f * (x < 0.5f ? x : 1.0f - x), 0.0f), k); return x < 0.5f ? s : 1.0f - s; }   /* 0.10.12 clamped (GPUs gave NaN) */
static void hex_sample(const arc__tex_rec *t, int lod, float u, float v, float rotate, float contrast, float out[4]) {
    const float S3 = 3.4641016f;   /* 2 sqrt 3 */
    float sx = u * S3, sy = v * S3, px = sx - sy * 0.57735027f, py = sy * 1.15470054f;
    float bx = floorf(px), by = floorf(py), fx = px - bx, fy = py - by, z = 1.0f - fx - fy;
    float s = z < 0 ? 1.0f : 0.0f, s2 = 2.0f * s - 1.0f;
    float w[3] = { -z * s2, s - fy * s2, s - fx * s2 };
    int ib = (int)bx, jb = (int)by, is = (int)s;
    int cx[3] = { ib + is, ib + is, ib + 1 - is }, cy[3] = { jb + is, jb + 1 - is, jb + is };
    float col[3][4], W[3], sum = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t h = hx_hash(cx[i], cy[i]), h2 = hx_hash(cx[i] + 31, cy[i] + 17);
        float ccx = ((float)cx[i] + (float)cy[i] * 0.5f) / S3, ccy = (float)cy[i] * 0.8660254f / S3;
        float a = ((float)(h2 & 0xFFFFu) / 65536.0f * 2.0f - 1.0f) * ARC_PI * rotate, ca = cosf(a), sa = sinf(a);
        float du = u - ccx, dv = v - ccy;
        float uu = ca * du - sa * dv + ccx + (float)(h & 0xFFFFu) / 65536.0f, vv = sa * du + ca * dv + ccy + (float)(h >> 16) / 65536.0f;
        sample_lod(t, lod, uu, vv, col[i]);
        float lum = col[i][0] * 0.3f + col[i][1] * 0.59f + col[i][2] * 0.11f, wi = arc_maxf(w[i], 0), w7 = wi * wi * wi; w7 = w7 * w7 * wi;
        W[i] = (1.0f + (lum - 1.0f) * 0.6f) * w7; sum += W[i];
    }
    if (sum > 0) for (int i = 0; i < 3; i++) W[i] /= sum; else W[0] = W[1] = W[2] = 1.0f / 3.0f;
    if (contrast != 0.5f) {
        float k = logf(1.0f - contrast) / logf(0.5f); sum = 0;
        for (int i = 0; i < 3; i++) { W[i] = hx_gain(W[i], k); sum += W[i]; }
        for (int i = 0; i < 3; i++) W[i] /= sum;
    }
    for (int ch = 0; ch < 4; ch++) out[ch] = W[0] * col[0][ch] + W[1] * col[1][ch] + W[2] * col[2][ch];
}
/* GGX highlight (times N.L) for one light: the same as the GPU shaders' ggx() */
static float ggx(arc_v3 n, arc_v3 V, arc_v3 L, float NdotL, float NdotV, float rough, float *fw_out) {
    arc_v3 H = arc_v3_norm(arc_v3_add(L, V));
    float NdotH = arc_maxf(arc_v3_dot(n, H), 0), VdotH = arc_maxf(arc_v3_dot(V, H), 0);
    float a = rough * rough, a2 = a * a, dd = NdotH * NdotH * (a2 - 1.0f) + 1.0f, D = a2 / (ARC_PI * dd * dd);
    float k = (rough + 1.0f) * (rough + 1.0f) * 0.125f, G = NdotV / (NdotV * (1 - k) + k) * NdotL / (NdotL * (1 - k) + k);
    *fw_out = pow5(1.0f - VdotH);
    return D * G / (4.0f * NdotV * NdotL + 1e-4f) * ARC_PI * NdotL;
}
/* point and spot lights: diffuse (onto kd) plus, for pbr, the GGX highlight */
static const arc__frame_data *g_sf;   /* the frame being drawn (its lights) */
/* 0.9.5 a shadowed light's view of a point (see arc__pass.lsh_n): its face, the point pushed out along the normal by
   1.5 texels and pulled toward the light by 1, then 3x3 bilinear compares 1 texel apart, kept inside the face */
static float lsh_cmp(float u, float v, float ref) {
    int w = S.lres * 6, h = S.lres * 4; float fx = u * (float)w - 0.5f, fy = v * (float)h - 0.5f; int ix = (int)floorf(fx), iy = (int)floorf(fy); float ax = fx - (float)ix, ay = fy - (float)iy, r = 0;
    for (int k = 0; k < 4; k++) {
        int x = arc_clampi(ix + (k & 1), 0, w - 1), y = arc_clampi(iy + (k >> 1), 0, h - 1);
        r += (ref <= S.lmap[(size_t)y * w + x] ? 1.0f : 0.0f) * ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay);
    }
    return r;
}
static float lsh_vis(const float *L8, arc_v3 wp, arc_v3 n) {
    arc_v3 d = arc_v3_make(wp.x - L8[0], wp.y - L8[1], wp.z - L8[2]), F, up; int face = 0;
    bool spot = L8[7] > -0.25f;
    if (spot) { F = arc_v3_make(L8[8], L8[9], L8[10]); up = fabsf(F.y) < 0.99f ? arc_v3_make(0, 1, 0) : arc_v3_make(1, 0, 0); }
    else {
        float ax = fabsf(d.x), ay = fabsf(d.y), az = fabsf(d.z);
        if (ax >= ay && ax >= az) { face = d.x >= 0 ? 0 : 1; F = arc_v3_make(d.x >= 0 ? 1.0f : -1.0f, 0, 0); up = arc_v3_make(0, 1, 0); }
        else if (ay >= az) { face = d.y >= 0 ? 2 : 3; F = arc_v3_make(0, d.y >= 0 ? 1.0f : -1.0f, 0); up = arc_v3_make(0, 0, 1); }
        else { face = d.z >= 0 ? 4 : 5; F = arc_v3_make(0, 0, d.z >= 0 ? 1.0f : -1.0f); up = arc_v3_make(0, 1, 0); }
    }
    arc_v3 Rt = arc_v3_norm(arc_v3_cross(F, up)), U = arc_v3_cross(Rt, F);
    float zn = L8[13], t = L8[14], tx = L8[15], z0 = arc_v3_dot(d, F);
    if (z0 <= zn) return 1.0f;
    float ws = 2.0f * t * z0 * tx;
    arc_v3 q = arc_v3_madd(d, n, 1.5f * ws);
    float z = arc_v3_dot(q, F); if (z <= zn) return 1.0f;
    float u = arc_v3_dot(q, Rt) / (z * t) * 0.5f + 0.5f, v = arc_v3_dot(q, U) / (z * t) * 0.5f + 0.5f;
    if (spot && (u < 0 || u > 1 || v < 0 || v > 1)) return 1.0f;
    float zf = 1.0f / sqrtf(L8[3]), zr = arc_maxf(z - ws, zn), ref = zf / (zf - zn) * (1.0f - zn / zr);
    u = arc_clampf(u, 1.5f * tx, 1 - 1.5f * tx); v = arc_clampf(v, 1.5f * tx, 1 - 1.5f * tx);
    float au = ((float)face + u) / 6.0f, av = (L8[12] + v) / 4.0f, du = tx / 6.0f, dv = tx / 4.0f, acc = 0;
    for (int j = -1; j <= 1; j++) for (int k = -1; k <= 1; k++) acc += lsh_cmp(au + (float)k * du, av + (float)j * dv, ref);
    return acc / 9.0f;
}
static void local_lights(const arc__pass *P, arc_v3 wp, arc_v3 n, const float kd[3], bool pbr, arc_v3 V, float NdotV, float rough, const float F0[3], float out[3]) {
    if (P->lt_tiles < 0 || !g_sf || !g_sf->lights) return;
    /* the point's tile (see arc__pass): the lights that can reach it, in rising order */
    const float *m = P->view_proj.m;
    float cx = m[0] * wp.x + m[4] * wp.y + m[8] * wp.z + m[12], cy = m[1] * wp.x + m[5] * wp.y + m[9] * wp.z + m[13], cw = m[3] * wp.x + m[7] * wp.y + m[11] * wp.z + m[15];
    int tx = 0, ty = 0;
    if (cw > 0) {
        tx = arc_clampi((int)floorf((cx / cw * 0.5f + 0.5f) * ARC__LT_TX), 0, ARC__LT_TX - 1);
        ty = arc_clampi((int)floorf((cy / cw * 0.5f + 0.5f) * ARC__LT_TY), 0, ARC__LT_TY - 1);
    }
    uint32_t e = g_sf->light_tiles[P->lt_tiles + ty * ARC__LT_TX + tx];
    const uint8_t *idx = g_sf->light_index + P->lt_idx + (e >> 8);
    for (int k = 0; k < (int)(e & 255u); k++) {
        const float *L8 = g_sf->lights + (size_t)idx[k] * ARC__LT_FLOATS;
        arc_v3 Lv = arc_v3_make(L8[0] - wp.x, L8[1] - wp.y, L8[2] - wp.z);
        float d2 = arc_v3_dot(Lv, Lv), q = arc_clampf(1.0f - d2 * L8[3], 0, 1);
        if (q <= 0) continue;
        arc_v3 L = arc_v3_scale(Lv, 1.0f / sqrtf(arc_maxf(d2, 1e-12f)));
        float NdotL = arc_maxf(arc_v3_dot(n, L), 0);
        if (NdotL <= 0) continue;
        float sd = -(L.x * L8[8] + L.y * L8[9] + L.z * L8[10]);
        float t = arc_clampf((sd - L8[11]) / (L8[7] - L8[11]), 0, 1), cone = t * t * (3 - 2 * t);
        float w = q * q * cone, fw = 0;
        if (L8[12] >= 0 && (int)L8[12] < P->lsh_n && P->mb_count && S.lmap) { w *= lsh_vis(L8, wp, n); if (w <= 0) continue; }
        float sp = pbr ? ggx(n, V, L, NdotL, NdotV, rough, &fw) : 0;
        for (int ch = 0; ch < 3; ch++) {
            float lc = L8[4 + ch] * w;
            out[ch] += kd[ch] * lc * NdotL;
            if (pbr) out[ch] += sp * (F0[ch] + (1.0f - F0[ch]) * fw) * lc;
        }
    }
}
/* 0.9.6 splat (see ARC__MAT3_*): w = the splat map's sample; each layer at world (x, z) x its scale, at the triangle's
   level for that scale (g_llod) */
static int g_llod[ARC__MAX_LAYERS];
static void splat(const shade_ctx *C, const float *at, const float w[4], float out[4]) {
    const float *m3 = C->m3; int n = C->nsl; float c[ARC__MAX_LAYERS][4], b[ARC__MAX_LAYERS], sum = 0, sh = m3[ARC__MAT3_SHARP];
    for (int i = 0; i < n; i++) sum += w[i];
    g_wrap = true;
    for (int i = 0; i < n; i++) {
        float wi = sum < 1e-4f ? (i == 0 ? 1.0f : 0.0f) : w[i];
        b[i] = wi;
        if (wi > 0) sample_lod(C->sl[i], g_llod[i], at[A_WX] * m3[ARC__MAT3_S0 + i], at[A_WZ] * m3[ARC__MAT3_S0 + i], c[i]);
        else c[i][0] = c[i][1] = c[i][2] = c[i][3] = 0;
    }
    g_wrap = false;
    if (sh > 0) {   /* height blend: layer alpha is the height */
        float v[ARC__MAX_LAYERS], mx = -1e30f, d = 1.0f - 0.95f * sh;
        for (int i = 0; i < n; i++) { v[i] = b[i] > 0 ? b[i] + c[i][3] * sh : -1e30f; if (v[i] > mx) mx = v[i]; }
        for (int i = 0; i < n; i++) b[i] = b[i] > 0 ? arc_maxf(v[i] - (mx - d), 0) : 0;
    }
    float t = 0, r[3] = { 0, 0, 0 };
    for (int i = 0; i < n; i++) { t += b[i]; for (int ch = 0; ch < 3; ch++) r[ch] += b[i] * c[i][ch]; }
    t = t > 0 ? 1.0f / t : 0;
    for (int ch = 0; ch < 3; ch++) out[ch] = r[ch] * t;
    out[3] = 1;
}
/* 0.9.9 world mask (see arc__pass.wm): how visible the point is, 1 = fully */
static float wmask_m(const arc__pass *P, float wx, float wz) {
    const arc__tex_rec *t = &arc__textures[P->wmask]; const uint8_t *px = t->rgba;
    if (!px || t->w <= 0 || t->h <= 0) return 1;
    float gx = (wx - P->wm[0]) * P->wm[2] - 0.5f, gz = (wz - P->wm[1]) * P->wm[2] - 0.5f, bx = floorf(gx), bz = floorf(gz), fx = gx - bx, fz = gz - bz, s = P->wm[3];
    if (s < 0.01f) { fx = fx >= 0.5f ? 1.0f : 0.0f; fz = fz >= 0.5f ? 1.0f : 0.0f; }
    else { float e0 = 0.5f - 0.5f * s, e1 = 0.5f + 0.5f * s;
           float tx = arc_clampf((fx - e0) / (e1 - e0), 0, 1), tz = arc_clampf((fz - e0) / (e1 - e0), 0, 1); fx = tx * tx * (3 - 2 * tx); fz = tz * tz * (3 - 2 * tz); }
    int x0 = arc_clampi((int)bx, 0, t->w - 1), x1 = arc_clampi((int)bx + 1, 0, t->w - 1), z0 = arc_clampi((int)bz, 0, t->h - 1), z1 = arc_clampi((int)bz + 1, 0, t->h - 1);
    float m00 = px[((size_t)z0 * t->w + x0) * 4] / 255.0f, m10 = px[((size_t)z0 * t->w + x1) * 4] / 255.0f;
    float m01 = px[((size_t)z1 * t->w + x0) * 4] / 255.0f, m11 = px[((size_t)z1 * t->w + x1) * 4] / 255.0f;
    float a = m00 + (m10 - m00) * fx, b = m01 + (m11 - m01) * fx; return a + (b - a) * fz;
}
static const float g_bayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static void shade(const shade_ctx *C, const float *at, int lod, float uv_ratio, int dlod, int mlod, int blod, float out[4], bool *discard) {
    const arc__pass *P = C->P; const arc__instance *in = C->in; const float *mat = C->mat;
    const float *fx = in->fx; float glow = 0;
    if (P->mirror && P->clip[0] * at[A_WX] + P->clip[1] * at[A_WY] + P->clip[2] * at[A_WZ] + P->clip[3] < 0) { *discard = true; return; }   /* 0.8.7 */
    float wmv = (P->wm[7] > 0.5f && !arc__ext_nomask(in->extra)) ? wmask_m(P, at[A_WX], at[A_WZ]) : 1.0f;   /* 0.9.9 */
    if (C->cut) {   /* 0.9.9 cutaway (see arc__pass.cut) */
        const float *k = P->cut; float abx = k[4] - k[0], abz = k[6] - k[2], ab2 = abx * abx + abz * abz;
        float t = ab2 > 1e-8f ? arc_clampf(((at[A_WX] - k[0]) * abx + (at[A_WZ] - k[2]) * abz) / ab2, 0, 1) : 0;
        float dx = at[A_WX] - (k[0] + abx * t), dz = at[A_WZ] - (k[2] + abz * t), d = sqrtf(dx * dx + dz * dz);
        if (d < k[3] && at[A_WY] > k[7]) {
            if (k[8] < 1.5f) { *discard = true; return; }
            float f = arc_clampf((at[A_WY] - k[7]) / k[9], 0, 1) * arc_clampf((k[3] - d) / (0.25f * k[3]), 0, 1);
            if ((g_bayer4[(C->py & 3) * 4 + (C->px & 3)] + 0.5f) / 16.0f < f) { *discard = true; return; }
        }
        if (C->back) {   /* the cap: what shows through the cut, a flat grey, then the mask and the fog */
            float dist = arc_v3_dist(arc_v3_make(at[A_WX], at[A_WY], at[A_WZ]), P->eye), fog = 0;
            if (P->fog[2] > 0) { if (P->fog[3] > 0) { float kk = P->fog[3] * dist; fog = 1.0f - expf(-kk * kk); } else fog = arc_clampf((dist - P->fog[0]) * P->fog[1], 0, 1); }
            for (int ch = 0; ch < 3; ch++) { float g = k[10]; g = P->wm[4 + ch] + (g - P->wm[4 + ch]) * wmv; out[ch] = g + (P->fog_col[ch] - g) * fog; }
            out[3] = 1; *discard = false; return;
        }
    }
    if (fx[0] > 0) {
        float nz = dissolve_noise(at[A_OX], at[A_OY], at[A_OZ]);
        if (nz < fx[0]) { *discard = true; return; }
        if (fx[1] > 0 && nz < fx[0] + fx[1]) glow = fx[7] * (1.0f - (nz - fx[0]) / fx[1]);
    }
    const float *m2 = C->m2; float tx[4];
    if (m2 && m2[ARC__MAT2_BMODE] > 0.5f && C->tex) {   /* hex and / or world-mapped colour (0.8.8: always repeating) */
        float bu = at[A_U], bv = at[A_V], bs = m2[ARC__MAT2_BSCALE];
        if (C->bplane == 0) { bu = at[A_WX] * bs; bv = at[A_WZ] * bs; }
        else if (C->bplane == 1) { bu = at[A_WZ] * bs; bv = at[A_WY] * bs; }
        else if (C->bplane == 2) { bu = at[A_WX] * bs; bv = at[A_WY] * bs; }
        g_wrap = true;
        if (m2[ARC__MAT2_BMODE] > 1.5f) hex_sample(C->tex, blod, bu, bv, m2[ARC__MAT2_BROT], m2[ARC__MAT2_BCON], tx);
        else sample_lod(C->tex, blod, bu, bv, tx);
        g_wrap = false;
    } else sample_lod(C->tex, lod, at[A_U], at[A_V], tx);
    if (C->nsl) { float w[4] = { tx[0], tx[1], tx[2], tx[3] }; splat(C, at, w, tx); }
    *discard = C->blend == ARC__MB_OPAQUE && tx[3] < 0.5f;
    if (fx[3] > 0) {   /* outline hull: flat colour, fogged */
        *discard = false;
        float dist = arc_v3_dist(arc_v3_make(at[A_WX], at[A_WY], at[A_WZ]), P->eye), fog = 0;
        if (P->fog[2] > 0) { if (P->fog[3] > 0) { float k = P->fog[3] * dist; fog = 1.0f - expf(-k * k); } else fog = arc_clampf((dist - P->fog[0]) * P->fog[1], 0, 1); }
        for (int ch = 0; ch < 3; ch++) out[ch] = in->tint[ch] + (P->fog_col[ch] - in->tint[ch]) * fog;
        out[3] = 1; return;
    }
    float c[3] = { at[A_R] * tx[0], at[A_G] * tx[1], at[A_B] * tx[2] };
    if (C->dt) {   /* 0.8.4 detail map: colour *= mix(1, 2 d, strength) */
        float du = at[A_U], dv = at[A_V], sc = m2[ARC__MAT2_DSCALE], d[4], k = m2[ARC__MAT2_DSTR];
        if (C->dplane == 0) { du = at[A_WX] * sc; dv = at[A_WZ] * sc; }
        else if (C->dplane == 1) { du = at[A_WZ] * sc; dv = at[A_WY] * sc; }
        else if (C->dplane == 2) { du = at[A_WX] * sc; dv = at[A_WY] * sc; }
        bool hex = m2[ARC__MAT2_DMODE] > 1.5f;
        if (hex) hex_sample(C->dt, dlod, du, dv, m2[ARC__MAT2_DROT], m2[ARC__MAT2_DCON], d);
        else sample_lod(C->dt, dlod, du, dv, d);
        k *= d[3];
        if (C->mk) {   /* breakup mask: same mapping at its own scale */
            float ms = m2[ARC__MAT2_MSCALE], f = C->dplane >= 0 ? (ms > 0 ? ms / sc : 1.0f) : (ms > 0 ? ms : 1.0f), mm[4];
            if (hex) hex_sample(C->mk, mlod, du * f, dv * f, m2[ARC__MAT2_DROT], m2[ARC__MAT2_DCON], mm);
            else sample_lod(C->mk, mlod, du * f, dv * f, mm);
            k *= arc_clampf((mm[0] - m2[ARC__MAT2_MTHR]) / m2[ARC__MAT2_MSOFT], 0, 1);
        }
        for (int ch = 0; ch < 3; ch++) c[ch] *= 1.0f + (2.0f * d[ch] - 1.0f) * k;
    }
    float lum = c[0] * 0.3f + c[1] * 0.59f + c[2] * 0.11f;
    float bc[3];   /* the base colour: tint (multiply, recolour), then (0.8.8) the paint mask's two colours */
    for (int ch = 0; ch < 3; ch++) { float b = c[ch] * (1.0f + (in->tint[ch] - 1.0f) * in->params[2]); bc[ch] = b + (in->tint[ch] * (0.35f + lum) - b) * in->params[0]; }
    if (C->pt) {
        float pm[4]; sample_lod(C->pt, lod_for(C->pt, uv_ratio), at[A_U], at[A_V], pm);
        for (int ch = 0; ch < 3; ch++) {
            bc[ch] += (in->tint[ch] * (0.35f + lum) - bc[ch]) * pm[0];
            bc[ch] += (C->pt2[ch] * (0.35f + lum) - bc[ch]) * pm[1];
            if (C->pt3on) bc[ch] += (C->pt3[ch] * (0.35f + lum) - bc[ch]) * pm[2];   /* 0.8.9 blue: tint3 */
        }
    }
    if (C->pat && at[A_PM] > 0.5f) {   /* 0.10.1 paint pattern */
        float m = pattern_at(C->pat, at[A_BX], at[A_BY], at[A_BZ], C->paxis);
        if (m > 0 && C->pat->p[1][3] > 0.5f) { float pm[4] = { 0, 0, 0, 0 }; if (C->pt) sample_lod(C->pt, lod_for(C->pt, uv_ratio), at[A_U], at[A_V], pm); m *= pm[0]; }   /* armour: where the mask is red */
        if (m > 0) for (int ch = 0; ch < 3; ch++) bc[ch] += (C->pat->p[2][ch] * (0.35f + lum) - bc[ch]) * m;
    }
    {   /* 0.9.9 darken and char (as the GPU shaders) */
        float dk = arc__ext_darken(in->extra), ch8 = arc__ext_char(in->extra);
        if (dk > 0) for (int ch = 0; ch < 3; ch++) bc[ch] *= 1.0f - 0.85f * dk;
        if (ch8 > 0) {
            float n1 = dissolve_noise(at[A_OX] * 1.7f, at[A_OY] * 1.7f, at[A_OZ] * 1.7f), soot = arc_clampf((n1 - 0.35f) / 0.35f, 0, 1); soot = soot * soot * (3 - 2 * soot);
            float n2 = dissolve_noise(at[A_OX] * 7.0f + 31.0f, at[A_OY] * 7.0f + 31.0f, at[A_OZ] * 7.0f + 31.0f), sc = 1.0f - arc_clampf(fabsf(n2 - 0.5f) / 0.035f, 0, 1);
            sc = sc * sc * (3 - 2 * sc);
            for (int ch = 0; ch < 3; ch++) bc[ch] = bc[ch] * (1.0f - 0.8f * ch8) * (1.0f - 0.6f * ch8 * soot) + 0.55f * sc * ch8 * (1.0f - soot);
        }
    }
    arc_v3 n = arc_v3_norm(arc_v3_make(at[A_NX], at[A_NY], at[A_NZ]));
    if (mat && C->mt[0] && C->tb) {          /* normal map: bend the normal in the triangle's (T, B, N) frame */
        float nm[4]; sample_lod(C->mt[0], lod_for(C->mt[0], uv_ratio), at[A_U], at[A_V], nm);
        float tx3 = (nm[0] * 2 - 1) * mat[ARC__MAT_NSCALE], ty3 = (nm[1] * 2 - 1) * mat[ARC__MAT_NSCALE], tz3 = nm[2] * 2 - 1;
        arc_v3 T = arc_v3_make(C->T[0], C->T[1], C->T[2]), B = arc_v3_make(C->B[0], C->B[1], C->B[2]);
        T = arc_v3_sub(T, arc_v3_scale(n, arc_v3_dot(n, T)));
        float tl = arc_v3_len(T);
        if (tl > 1e-12f) {
            T = arc_v3_scale(T, 1.0f / tl);
            arc_v3 Bo = arc_v3_cross(n, T); if (arc_v3_dot(Bo, B) < 0) Bo = arc_v3_scale(Bo, -1.0f);
            n = arc_v3_norm(arc_v3_add(arc_v3_add(arc_v3_scale(T, tx3), arc_v3_scale(Bo, ty3)), arc_v3_scale(n, tz3)));
        }
    }
    float diff = arc_maxf(n.x * P->light_dir[0] + n.y * P->light_dir[1] + n.z * P->light_dir[2], 0.0f);
    arc_v3 wp = arc_v3_make(at[A_WX], at[A_WY], at[A_WZ]);
    float dist = arc_v3_dist(wp, P->eye), rim = 0;
    float vis = (!arc__ext_unlit(in->extra) && diff > 0) ? shadow_vis(P, wp, n) : 1.0f;
    if (P->rim[0] > 0 || P->rim[1] > 0 || P->rim[2] > 0) {
        arc_v3 V = arc_v3_norm(arc_v3_sub(P->eye, wp));
        rim = powf(1.0f - arc_maxf(arc_v3_dot(n, V), 0.0f), P->rim[3]);
    }
    float fog = 0;
    if (P->fog[2] > 0) {
        if (P->fog[3] > 0) { float k = P->fog[3] * dist; fog = 1.0f - expf(-k * k); }
        else fog = arc_clampf((dist - P->fog[0]) * P->fog[1], 0, 1);
    }
    float orm[4] = { 1, 1, 1, 1 }, em[4] = { 1, 1, 1, 1 };
    if (mat && C->mt[1]) sample_lod(C->mt[1], lod_for(C->mt[1], uv_ratio), at[A_U], at[A_V], orm);
    if (mat && C->mt[2]) sample_lod(C->mt[2], lod_for(C->mt[2], uv_ratio), at[A_U], at[A_V], em);
    float ao = mat ? 1.0f - mat[ARC__MAT_OCC] * (1.0f - orm[0]) : 1.0f;
    bool pbr = mat && mat[ARC__MAT_PBR] > 0.5f && !arc__ext_unlit(in->extra);
    float spec[3] = { 0, 0, 0 }, envs[3] = { 0, 0, 0 }, metal = 0;
    if (pbr) {                               /* GGX highlight from the sun, Fresnel-weighted environment */
        metal = arc_clampf(mat[ARC__MAT_METAL] * orm[2], 0, 1);
        float rough = arc_clampf(mat[ARC__MAT_ROUGH] * orm[1], 0.045f, 1.0f);
        arc_v3 V = arc_v3_norm(arc_v3_sub(P->eye, wp));
        float NdotV = arc_maxf(arc_v3_dot(n, V), 1e-4f);
        float base0[3];
        for (int ch = 0; ch < 3; ch++) base0[ch] = bc[ch];
        float F0[3]; for (int ch = 0; ch < 3; ch++) F0[ch] = 0.04f + (base0[ch] - 0.04f) * metal;
        if (diff > 0) {
            float fw, sp = ggx(n, V, arc_v3_make(P->light_dir[0], P->light_dir[1], P->light_dir[2]), diff, NdotV, rough, &fw);
            for (int ch = 0; ch < 3; ch++) spec[ch] = sp * (F0[ch] + (1.0f - F0[ch]) * fw) * P->light_col[ch] * vis;
        }
        if (P->nlights) { float kd[3]; for (int ch = 0; ch < 3; ch++) kd[ch] = base0[ch] * (1.0f - metal); local_lights(P, wp, n, kd, true, V, NdotV, rough, F0, spec); }
        float Ry = 2.0f * NdotV * n.y - V.y, e[3], avg[3];   /* reflected ray's height */
        env_col(P, Ry, e);
        for (int ch = 0; ch < 3; ch++) avg[ch] = (P->env[ch] + 2 * P->env[4 + ch] + P->env[8 + ch]) * 0.25f;
        float fr = pow5(1.0f - NdotV);
        for (int ch = 0; ch < 3; ch++) {
            float ec = e[ch] + (avg[ch] - e[ch]) * rough, mx = arc_maxf(1.0f - rough, F0[ch]);
            envs[ch] = ec * (F0[ch] + (mx - F0[ch]) * fr) * ao * P->env[3];
        }
    }
    float pl[3] = { 0, 0, 0 };   /* plain materials: point and spot lights, diffuse only */
    if (!pbr && P->nlights && !arc__ext_unlit(in->extra)) {
        float kd[3];
        for (int ch = 0; ch < 3; ch++) kd[ch] = bc[ch];
        local_lights(P, wp, n, kd, false, n, 1, 1, kd, pl);
    }
    float rw = 0, rc[4] = { 0, 0, 0, 0 };
    if (C->rt) {   /* 0.8.7 planar reflection: the mirror pass's picture at this pixel, blurred by roughness */
        arc_v3 V = arc_v3_norm(arc_v3_sub(P->eye, wp));
        float nv = 1.0f - arc_maxf(arc_v3_dot(n, V), 0.0f), F = 0.6f + 0.4f * pow5(nv);
        rw = P->refl[0] * C->ramt * at[A_A] * F;
        float rough = pbr ? arc_clampf(mat[ARC__MAT_ROUGH] * orm[1], 0, 1) : 0.0f, lod = arc_minf(6.0f * rough, P->refl[1]);
        float u = ((float)C->px + 0.5f) * P->refl[2] + 0.03f * n.x, v = ((float)C->py + 0.5f) * P->refl[3] + 0.03f * n.z;
        int l0 = (int)lod; float lf = lod - (float)l0, a0[4], a1[4];
        sample_lod(C->rt, l0, u, v, a0);
        if (lf > 0) { sample_lod(C->rt, l0 + 1, u, v, a1); for (int q = 0; q < 3; q++) a0[q] += (a1[q] - a0[q]) * lf; }
        memcpy(rc, a0, sizeof rc);
    }
    for (int ch = 0; ch < 3; ch++) {
        float base = bc[ch];
        float lit;
        if (arc__ext_unlit(in->extra)) lit = base * (1.0f + in->tint[3]);
        else if (pbr) lit = base * (1.0f - metal) * (P->ambient[ch] * ao + P->light_col[ch] * diff * vis) + spec[ch] + envs[ch] + base * in->tint[3] + P->rim[ch] * rim;
        else lit = base * (P->ambient[ch] * ao + P->light_col[ch] * diff * vis + in->tint[3]) + pl[ch] + P->rim[ch] * rim;
        if (mat) lit += mat[ARC__MAT_ER + ch] * em[ch];
        if (glow > 0) { int e = (int)fx[6]; lit += (float)((e >> (8 * ch)) & 0xFF) / 255.0f * glow; }
        if (rw > 0) lit += (rc[ch] - lit) * rw;
        lit += (1.0f - lit) * in->params[1];
        if (wmv < 1.0f) lit = P->wm[4 + ch] + (lit - P->wm[4 + ch]) * wmv;   /* 0.9.9 world mask */
        out[ch] = arc__ext_addfog(in->extra) ? lit * (1.0f - fog) : lit + (P->fog_col[ch] - lit) * fog;
    }
    out[3] = tx[3] * in->params[3] * (C->blend == ARC__MB_OPAQUE ? 1.0f : at[A_A]);   /* see-through: vertex alpha too */
    if (fx[2] > 0) {   /* hologram (additive) */
        arc_v3 V = arc_v3_norm(arc_v3_sub(P->eye, wp)); float fr = 1.0f - fabsf(arc_v3_dot(n, V)); fr *= fr;
        float line = 0.5f + 0.5f * sinf(60.0f * wp.y - 6.0f * P->time), k = (0.6f + 0.8f * fr) * (0.7f + 0.3f * line);
        for (int ch = 0; ch < 3; ch++) { float c0 = in->tint[ch] * k; out[ch] = c0 * (1.0f - fog); }
        out[3] = fx[2] * arc_clampf(0.25f + 0.75f * fr + 0.3f * line, 0, 1);
    }
}

static void raster3d(const v3 *a, const v3 *b, const v3 *c, const shade_ctx *C) {
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    ((shade_ctx *)C)->back = C->flipw ? area < 0 : area > 0;   /* 0.9.9 (for the cutaway's cap) */
    if (C->mode == 0 && area >= 0) return;
    if (C->mode == 2 && area <= 0) return;
    if (fabsf(area) < 1e-12f) return;
    if (area > 0) { const v3 *t = b; b = c; c = t; area = -area; }
    int minx = (int)floorf(arc_minf(a->x, arc_minf(b->x, c->x))), maxx = (int)ceilf(arc_maxf(a->x, arc_maxf(b->x, c->x)));
    int miny = (int)floorf(arc_minf(a->y, arc_minf(b->y, c->y))), maxy = (int)ceilf(arc_maxf(a->y, arc_maxf(b->y, c->y)));
    minx = arc_maxi(minx, 0); miny = arc_maxi(miny, 0); maxx = arc_mini(maxx, S.cw - 1); maxy = arc_mini(maxy, S.ch - 1);
    if (minx > maxx || miny > maxy) return;
    /* mip level for the whole triangle: texels covered per pixel (uv_ratio: texture-space area per pixel) */
    float uv_ratio = 0;
    if (C->nsl) {   /* 0.9.6 each splat layer's level from world (x, z) x its scale */
        float xa = a->at[A_WX] / a->w, za = a->at[A_WZ] / a->w, xb = b->at[A_WX] / b->w, zb = b->at[A_WZ] / b->w, xc = c->at[A_WX] / c->w, zc = c->at[A_WZ] / c->w;
        float wr = fabsf((xb - xa) * (zc - za) - (zb - za) * (xc - xa)) / fabsf(area);
        for (int i = 0; i < C->nsl; i++) g_llod[i] = lod_for(C->sl[i], wr * C->m3[ARC__MAT3_S0 + i] * C->m3[ARC__MAT3_S0 + i]);
    }
    if ((C->tex && C->tex->levels > 1) || C->mat || C->dt || C->bplane >= 0 || C->pt) {
        float ua = a->at[A_U] / a->w, va = a->at[A_V] / a->w, ub = b->at[A_U] / b->w, vb = b->at[A_V] / b->w, uc = c->at[A_U] / c->w, vc = c->at[A_V] / c->w;
        uv_ratio = fabsf((ub - ua) * (vc - va) - (vb - va) * (uc - ua)) / fabsf(area);
    }
    int lod = lod_for(C->tex, uv_ratio), dlod = 0, mlod = 0, blod = lod;
    if (C->bplane >= 0 && C->tex) {   /* 0.8.8 world-mapped colour: its level from its own uv */
        int iu = C->bplane == 1 ? A_WZ : A_WX, iv = C->bplane == 0 ? A_WZ : A_WY; float sc = C->m2[ARC__MAT2_BSCALE];
        float ua = a->at[iu] / a->w * sc, va = a->at[iv] / a->w * sc, ub = b->at[iu] / b->w * sc, vb = b->at[iv] / b->w * sc, uc = c->at[iu] / c->w * sc, vc = c->at[iv] / c->w * sc;
        blod = lod_for(C->tex, fabsf((ub - ua) * (vc - va) - (vb - va) * (uc - ua)) / fabsf(area));
    }
    if (C->dt) {   /* 0.8.4 detail map's own mip level, from its own uv (world plane or mesh uv) */
        float dr = uv_ratio;
        if (C->dplane >= 0) {
            int iu = C->dplane == 1 ? A_WZ : A_WX, iv = C->dplane == 0 ? A_WZ : A_WY; float sc = C->m2[ARC__MAT2_DSCALE];
            float ua = a->at[iu] / a->w * sc, va = a->at[iv] / a->w * sc, ub = b->at[iu] / b->w * sc, vb = b->at[iv] / b->w * sc, uc = c->at[iu] / c->w * sc, vc = c->at[iv] / c->w * sc;
            dr = fabsf((ub - ua) * (vc - va) - (vb - va) * (uc - ua)) / fabsf(area);
        }
        dlod = lod_for(C->dt, dr);
        if (C->mk) {   /* the mask's level: the same area ratio scaled by (its scale / the detail's) squared */
            float ms = C->m2[ARC__MAT2_MSCALE], f = C->dplane >= 0 ? (ms > 0 ? ms / C->m2[ARC__MAT2_DSCALE] : 1.0f) : (ms > 0 ? ms : 1.0f);
            mlod = lod_for(C->mk, dr * f * f);
        }
    }
    float inv = 1.0f / area;
    #define OWN3(dx, dy) ((dy) > 0 || ((dy) == 0 && (dx) > 0))
    bool own0 = OWN3(c->x - b->x, c->y - b->y), own1 = OWN3(a->x - c->x, a->y - c->y), own2 = OWN3(b->x - a->x, b->y - a->y);
    #undef OWN3
    /* attributes were divided by w in to_screen (w holds 1/w): interpolate, then divide by the interpolated 1/w */
    for (int y = miny; y <= maxy; y++) {
        float py = (float)y + 0.5f;
        for (int x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f;
            float e0 = (c->x - b->x) * (py - b->y) - (c->y - b->y) * (px - b->x);
            float e1 = (a->x - c->x) * (py - c->y) - (a->y - c->y) * (px - c->x);
            float e2 = (b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x);
            if (e0 > 0 || e1 > 0 || e2 > 0) continue;
            if ((e0 == 0 && !own0) || (e1 == 0 && !own1) || (e2 == 0 && !own2)) continue;
            float w0 = e0 * inv, w1 = e1 * inv, w2 = 1.0f - w0 - w1;
            size_t i = (size_t)y * S.cw + x;
            float z = w0 * a->z + w1 * b->z + w2 * c->z;
            if (C->zrev ? (z > 1.0f || (C->zf == ARC__Z_LESS ? z <= S.depth[i] : C->zf == ARC__Z_GREATER ? z >= S.depth[i] : false))   /* reversed: GREATER is the usual */
                        : (z < -1.0f || (C->zf == ARC__Z_LESS ? z >= S.depth[i] : C->zf == ARC__Z_GREATER ? z <= S.depth[i] : false))) continue;
            float iw = w0 * a->w + w1 * b->w + w2 * c->w, k = iw != 0 ? 1.0f / iw : 0, at[A_COUNT];
            for (int q = 0; q < S.na; q++) at[q] = (w0 * a->at[q] + w1 * b->at[q] + w2 * c->at[q]) * k;
            float col[4]; bool discard;
            ((shade_ctx *)C)->px = x; ((shade_ctx *)C)->py = y;
            shade(C, at, lod, uv_ratio, dlod, mlod, blod, col, &discard);
            if (discard) continue;
            if (C->soft > 0) col[3] *= soft_fade(C->P, x, y, S.depth[i], at, C->soft);
            if (S.fcolor) {                                  /* HDR screen: no clamping (bloom wants > 1) */
                float *o = S.fcolor + i * 4;
                if (C->zw) S.depth[i] = z;
                if (C->blend == ARC__MB_OPAQUE) { o[0] = col[0]; o[1] = col[1]; o[2] = col[2]; o[3] = 1; }
                else if (C->blend == ARC__MB_ADD) { float al = col[3]; if (al > 0) for (int ch = 0; ch < 3; ch++) o[ch] += col[ch] * al; }
                else { float al = col[3]; if (al > 0) { for (int ch = 0; ch < 3; ch++) o[ch] = col[ch] * al + o[ch] * (1 - al); o[3] = al + o[3] * (1 - al); } }
                continue;
            }
            uint8_t *o = S.color + i * 4;
            if (C->zw) S.depth[i] = z;
            if (C->blend == ARC__MB_OPAQUE) {
                o[0] = to8(col[0]); o[1] = to8(col[1]); o[2] = to8(col[2]); o[3] = 255;
            } else if (C->blend == ARC__MB_ADD) {
                float al = col[3]; if (al <= 0) continue;
                for (int ch = 0; ch < 3; ch++) o[ch] = to8(o[ch] / 255.0f + col[ch] * al);
            } else {
                float al = col[3]; if (al <= 0) continue;
                for (int ch = 0; ch < 3; ch++) o[ch] = to8(col[ch] * al + o[ch] / 255.0f * (1 - al));
                o[3] = to8(al + o[3] / 255.0f * (1 - al));
            }
        }
    }
}

static void to_screen3(v3 *v) {
    float iw = 1.0f / v->w;
    v->x = (v->x * iw * 0.5f + 0.5f) * (float)S.cw; v->y = (0.5f - v->y * iw * 0.5f) * (float)S.ch; v->z = v->z * iw;
    for (int q = 0; q < S.na; q++) v->at[q] *= iw;
    v->w = iw;
}
static v3 lerp_v3(const v3 *a, const v3 *b, float t) {
    v3 r; const float *pa = &a->x, *pb = &b->x; float *pr = &r.x;
    for (int i = 0; i < 4 + S.na; i++) pr[i] = pa[i] + (pb[i] - pa[i]) * t;
    return r;
}
/* clip a clip-space triangle against the near plane (z > -w), then raster */
static void clip_raster3(v3 v0, v3 v1, v3 v2, const shade_ctx *C) {
    v3 in[3] = { v0, v1, v2 }, out[4]; int n = 0;
    for (int i = 0; i < 3; i++) {
        const v3 *a = &in[i], *b = &in[(i + 1) % 3];
        float da = C->zrev ? a->w - a->z : a->z + a->w, db = C->zrev ? b->w - b->z : b->z + b->w;   /* reversed: near is z = w */
        if (da >= 0) out[n++] = *a;
        if ((da >= 0) != (db >= 0)) out[n++] = lerp_v3(a, b, da / (da - db));
    }
    if (n < 3) return;
    for (int i = 0; i < n; i++) to_screen3(&out[i]);
    raster3d(&out[0], &out[1], &out[2], C);
    if (n == 4) raster3d(&out[0], &out[2], &out[3], C);
}

/* sky gradient (all renderers): ray = normalize(far - near) through the pixel (from the inverse view-projection);
   t = ray.y; colour = horizon + (top - horizon) * t above, horizon + (below - horizon) * -t below */
static void sky_fill(const arc__pass *p, float *hdr, uint8_t *c8, float *depth, int w, int h) {
    const float *m = p->sky_ivp;   /* column-major */
    for (int y = 0; y < h; y++) {
        float ny = 1.0f - ((float)y + 0.5f) / (float)h * 2.0f;
        for (int x = 0; x < w; x++) {
            float nx = ((float)x + 0.5f) / (float)w * 2.0f - 1.0f, a[4], b[4];
            for (int r = 0; r < 4; r++) {
                float base = m[r] * nx + m[4 + r] * ny + m[12 + r];
                a[r] = base - m[8 + r]; b[r] = base + m[8 + r];
            }
            float dx = b[0] / b[3] - a[0] / a[3], dy = b[1] / b[3] - a[1] / a[3], dz = b[2] / b[3] - a[2] / a[3];
            float t = dy / sqrtf(dx * dx + dy * dy + dz * dz);
            const float *to = t >= 0 ? p->sky : p->sky + 8;
            float k = fminf(fabsf(t), 1.0f), col[3];
            for (int i = 0; i < 3; i++) col[i] = p->sky[4 + i] + (to[i] - p->sky[4 + i]) * k;
            size_t o = (size_t)y * w + x;
            if (hdr) { hdr[o * 4] = col[0]; hdr[o * 4 + 1] = col[1]; hdr[o * 4 + 2] = col[2]; hdr[o * 4 + 3] = 1; }
            else { c8[o * 4] = to8(col[0]); c8[o * 4 + 1] = to8(col[1]); c8[o * 4 + 2] = to8(col[2]); c8[o * 4 + 3] = 255; }
            depth[o] = p->zrev ? 0.0f : 1.0f;
        }
    }
}
static void bind_pass(const arc__frame_data *f, const arc__pass *p) {
    g_sf = f;
    if (p->target) {
        arc__tex_rec *t = &arc__textures[p->target];
        if (!S.tdepth[p->target] || S.tdw[p->target] != t->w || S.tdh[p->target] != t->h) {
            free(S.tdepth[p->target]); S.tdepth[p->target] = (float *)arc__xalloc(sizeof(float) * (size_t)t->w * t->h);
            S.tdw[p->target] = t->w; S.tdh[p->target] = t->h;
            for (size_t i = 0, n = (size_t)t->w * t->h; i < n; i++) S.tdepth[p->target][i] = p->zrev ? 0.0f : 1.0f;
        }
        S.cw = t->w; S.ch = t->h; S.color = t->rgba; S.depth = S.tdepth[p->target];
        soft_pset *ps = p->post[ARC__POST_ON] > 0 ? pset_for(p->target, true) : NULL;
        S.fcolor = NULL;
        if (ps) {   /* 0.8.2: the target's own post effects: its 3D goes into its HDR buffer, as the screen's does */
            if (!ps->hdr || ps->hw != t->w || ps->hh != t->h) { free(ps->hdr); ps->hw = t->w; ps->hh = t->h; ps->hdr = (float *)arc__xalloc(sizeof(float) * 4 * (size_t)t->w * t->h); }
            S.fcolor = ps->hdr;
            if (p->clear && p->vport && p->sky[3] > 0) { sky_fill(p, ps->hdr, NULL, S.depth, S.cw, S.ch); return; }   /* 0.10.2: a viewport's sky */
            if (p->clear) for (size_t i = 0, n = (size_t)S.cw * S.ch; i < n; i++) { memcpy(ps->hdr + i * 4, p->clear_col, sizeof(float) * 4); S.depth[i] = p->zrev ? 0.0f : 1.0f; }
            return;
        }
    } else {
        if (S.w != f->width || S.h != f->height || !S.screen) {
            free(S.screen); free(S.screen_depth); S.w = f->width; S.h = f->height;
            S.screen = (uint8_t *)arc__xalloc((size_t)S.w * S.h * 4); S.screen_depth = (float *)arc__xalloc(sizeof(float) * (size_t)S.w * S.h);
        }
        S.cw = S.w; S.ch = S.h; S.color = S.screen; S.depth = S.screen_depth;
    }
    S.fcolor = NULL;
    if (!p->target && p->post[ARC__POST_ON] > 0) {
        if (!S.hdr || S.hw != S.w || S.hh != S.h) { free(S.hdr); S.hw = S.w; S.hh = S.h; S.hdr = (float *)arc__xalloc(sizeof(float) * 4 * (size_t)S.w * S.h); }
        S.fcolor = S.hdr;
        if (p->clear) {
            if (p->sky[3] > 0) { sky_fill(p, S.hdr, NULL, S.depth, S.cw, S.ch); return; }
            for (size_t i = 0, n = (size_t)S.cw * S.ch; i < n; i++) { float *o = S.hdr + i * 4; memcpy(o, p->clear_col, sizeof(float) * 4); S.depth[i] = p->zrev ? 0.0f : 1.0f; }
            return;
        }
    }
    if (p->clear && (!p->target || p->mirror || p->vport) && p->sky[3] > 0) { sky_fill(p, NULL, S.color, S.depth, S.cw, S.ch); return; }   /* (0.10.2 viewports too) */
    if (p->clear) {
        uint8_t cr = to8(p->clear_col[0]), cg = to8(p->clear_col[1]), cb = to8(p->clear_col[2]), ca = to8(p->clear_col[3]);
        for (size_t i = 0, n = (size_t)S.cw * S.ch; i < n; i++) { uint8_t *o = S.color + i * 4; o[0] = cr; o[1] = cg; o[2] = cb; o[3] = ca; S.depth[i] = p->zrev ? 0.0f : 1.0f; }
    }
}

/* one batch of mesh instances (or the immediate triangles) */
static void draw_batch(const arc__frame_data *f, const arc__pass *P, const arc__mesh_batch *mb, int blend) {
    static const arc__instance ident = { { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, { 1, 1, 1, 0 }, { 0, 0, 1, 1 }, { 0, 0, 0, 0 }, { 0 }, { 0 } };
    static v3 *tmp = NULL; static int tmp_cap = 0;
    const arc__mesh_rec *M = mb ? &arc__meshes[mb->mesh] : NULL;
    const arc_vertex *verts = M ? M->v : f->imm + P->imm_first;
    const uint32_t *idx = M ? M->idx : NULL;
    int vcount = M ? M->vcount : P->imm_count, icount = M ? M->icount : P->imm_count, count = mb ? mb->count : 1;
    if (!verts || vcount <= 0) return;
    shade_ctx C; C.P = P; C.blend = blend;
    C.zf = mb ? (mb->depth & ARC__Z_FUNC) : ARC__Z_LESS; C.zw = mb ? !(mb->depth & ARC__Z_NOWRITE) : 1; C.zrev = P->zrev;
    C.mode = (!M || M->double_sided) ? 3 : ((mb->flip ^ P->mirror) ? 2 : 0);   /* (0.8.7 mirror passes wind the other way) */
    C.flipw = mb && (mb->flip ^ P->mirror); C.cut = M && M->cutaway && P->cut[8] > 0.5f; C.back = false;   /* 0.9.9 cutaway: both faces (the back is the cap) */
    if (C.cut) C.mode = 3;
    C.tex = (M && M->texture && arc__textures[M->texture].used) ? &arc__textures[M->texture] : NULL;
    C.mat = (M && M->has_mat) ? M->mat : NULL; C.tb = false;
    for (int i = 0; i < 3; i++) C.mt[i] = (C.mat && M->mat_tex[i] && arc__textures[M->mat_tex[i]].used) ? &arc__textures[M->mat_tex[i]] : NULL;
    C.m2 = (M && (M->mat2[ARC__MAT2_DMODE] > 0 || M->mat2[ARC__MAT2_BMODE] > 0)) ? M->mat2 : NULL;
    C.dt = (C.m2 && C.m2[ARC__MAT2_DMODE] > 0 && M->detail_tex && arc__textures[M->detail_tex].used) ? &arc__textures[M->detail_tex] : NULL;
    C.mk = (C.dt && M->mask_tex && C.m2[ARC__MAT2_MON] > 0 && arc__textures[M->mask_tex].used) ? &arc__textures[M->mask_tex] : NULL;
    C.dplane = -1; C.bplane = -1;
    C.nsl = 0; C.m3 = NULL;
    const arc__tex_rec *alb = (M && M->layers_mode == 1 && M->layers_tex && arc__textures[M->layers_tex].used && arc__textures[M->layers_tex].layers > 0) ? &arc__textures[M->layers_tex] : NULL;   /* 0.9.9 */
    if (!alb && M && M->layers_tex && arc__textures[M->layers_tex].used && arc__textures[M->layers_tex].layers > 0 && C.tex) {   /* 0.9.6 splat */
        const arc__tex_rec *A = &arc__textures[M->layers_tex]; C.m3 = M->mat3; C.nsl = arc_mini(A->layers, 4);
        for (int i = 0; i < C.nsl; i++) { C.sl[i] = &arc__textures[A->layer_id[i]]; if (!C.sl[i]->used || !C.sl[i]->rgba) C.nsl = 0; }
    }
    C.rt = (M && M->reflect > 0 && P->refl_tex && P->refl[0] > 0 && arc__textures[P->refl_tex].used) ? &arc__textures[P->refl_tex] : NULL; C.ramt = M ? M->reflect : 0;
    C.px = C.py = 0;
    C.pt = (mb && mb->paint_tex && mb->paint_tex < ARC__MAX_TEXTURES && arc__textures[mb->paint_tex].used) ? &arc__textures[mb->paint_tex] : NULL;
    arc__unpack_rgb(mb ? mb->tint2 : 0, C.pt2);
    arc__unpack_rgb(mb ? mb->tint3 & 0xFFFFFF : 0, C.pt3); C.pt3on = mb && (mb->tint3 >> 24);
    C.pat = (mb && mb->pat > 0 && mb->pat <= f->pat_count) ? &f->pats[mb->pat - 1] : NULL; C.paxis = 2;   /* 0.10.1 */
    S.na = C.pat ? A_COUNT : A_BASE;
    if (vcount > tmp_cap) { tmp_cap = vcount; tmp = (v3 *)realloc(tmp, sizeof(v3) * (size_t)tmp_cap); }
    for (int k = 0; k < count; k++) {
        const arc__instance *in = mb ? &f->instances[mb->first + k] : &ident;
        if (alb) {   /* 0.9.9 the colour from this instance's layer of the mesh's texture array */
            int L = arc_clampi(arc__ext_layer(in->extra), 0, alb->layers - 1); const arc__tex_rec *lt = &arc__textures[alb->layer_id[L]];
            C.tex = lt->used && lt->rgba ? lt : NULL;
        }
        C.in = in; C.soft = (P->soft && in->fx[1] < 0 && in->fx[0] <= 0) ? -in->fx[1] : 0;
        arc_m4 model; memcpy(model.m, in->model, sizeof model.m);
        arc_m4 mvp = arc_m4_mul(P->view_proj, model);
        int bone0 = (M && M->skin && in->extra[1] > 0.5f) ? (int)in->extra[0] : -1;
        for (int i = 0; i < vcount; i++) {
            const arc_vertex *v = &verts[i];
            arc_v3 p = arc_v3_make(v->px, v->py, v->pz), nn = arc_v3_make(v->nx, v->ny, v->nz);
            if (bone0 >= 0) {                  /* linear blend skinning, as in the GPU shaders */
                const uint8_t *sk = M->skin + (size_t)i * 8; float m[12] = { 0 };
                for (int j = 0; j < 4; j++) {
                    if (!sk[4 + j]) continue;
                    int bi = bone0 + sk[j]; if (bi >= f->bone_count) continue;
                    const float *B = f->bones + (size_t)bi * 12; float w = sk[4 + j] / 255.0f;
                    for (int q = 0; q < 12; q++) m[q] += B[q] * w;
                }
                arc_v3 sp = arc_v3_make(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
                arc_v3 sn = arc_v3_make(m[0] * nn.x + m[1] * nn.y + m[2] * nn.z, m[4] * nn.x + m[5] * nn.y + m[6] * nn.z, m[8] * nn.x + m[9] * nn.y + m[10] * nn.z);
                p = sp; nn = sn;
            }
            arc_v4 c = arc_m4_mul_v4(mvp, (arc_v4){ p.x, p.y, p.z, 1 });
            arc_v3 wp = arc_m4_mul_point(model, p);
            arc_v3 n = arc_v3_norm(arc_v3_make(model.m[0] * nn.x + model.m[4] * nn.y + model.m[8] * nn.z,
                                               model.m[1] * nn.x + model.m[5] * nn.y + model.m[9] * nn.z,
                                               model.m[2] * nn.x + model.m[6] * nn.y + model.m[10] * nn.z));
            if (in->fx[3] > 0) {   /* outline hull: out along the normal bent away from the model's origin (hard-edged meshes open up too) */
                arc_v3 cd = arc_v3_sub(wp, arc_v3_make(model.m[12], model.m[13], model.m[14])); float cl = arc_v3_len(cd);
                arc_v3 dir = cl > 1e-6f ? arc_v3_add(n, arc_v3_scale(cd, 1.0f / cl)) : n; float dl = arc_v3_len(dir);
                dir = dl > 1e-4f ? arc_v3_scale(dir, 1.0f / dl) : n;
                wp = arc_v3_madd(wp, dir, in->fx[3]); c = arc_m4_mul_v4(P->view_proj, (arc_v4){ wp.x, wp.y, wp.z, 1 });
            }
            v3 *o = &tmp[i]; o->x = c.x; o->y = c.y; o->z = c.z; o->w = c.w;
            o->at[A_WX] = wp.x; o->at[A_WY] = wp.y; o->at[A_WZ] = wp.z; o->at[A_NX] = n.x; o->at[A_NY] = n.y; o->at[A_NZ] = n.z;
            o->at[A_R] = (v->color & 0xFF) / 255.0f; o->at[A_G] = ((v->color >> 8) & 0xFF) / 255.0f; o->at[A_B] = ((v->color >> 16) & 0xFF) / 255.0f; o->at[A_A] = (v->color >> 24) / 255.0f;
            if (in->uvx[0] != 0 || in->uvx[1] != 0 || in->uvx[2] != 0 || in->uvx[3] != 0) {   /* 0.8.4 per-draw uv transform */
                o->at[A_U] = (1.0f + in->uvx[0]) * v->u + in->uvx[1] * v->v + in->fx[4]; o->at[A_V] = in->uvx[2] * v->u + (1.0f + in->uvx[3]) * v->v + in->fx[5];
            } else { o->at[A_U] = v->u + in->fx[4]; o->at[A_V] = v->v + in->fx[5]; }
            o->at[A_OX] = p.x; o->at[A_OY] = p.y; o->at[A_OZ] = p.z;
            if (C.pat) {   /* 0.10.1 the bind pose, and whether the vertex's strongest joint is in the part mask */
                o->at[A_BX] = v->px; o->at[A_BY] = v->py; o->at[A_BZ] = v->pz;
                float pm = C.pat->p[3][3];
                if (bone0 >= 0) { const uint8_t *sk = M->skin + (size_t)i * 8; int j = 0; for (int q = 1; q < 4; q++) if (sk[4 + q] > sk[4 + j]) j = q; pm = C.pat->jm[sk[j] & 127]; }
                o->at[A_PM] = pm;
            }
        }
        for (int t = 0; t + 2 < icount; t += 3) {
            const v3 *a = &tmp[idx ? idx[t] : (uint32_t)t], *bb = &tmp[idx ? idx[t + 1] : (uint32_t)t + 1], *c = &tmp[idx ? idx[t + 2] : (uint32_t)t + 2];
            if (C.pat) {   /* 0.10.1 the face's axis in the bind pose (CHECKER, DOTS), as the GPU's derivative cross product */
                float e1[3], e2[3];
                for (int q = 0; q < 3; q++) { e1[q] = bb->at[A_BX + q] - a->at[A_BX + q]; e2[q] = c->at[A_BX + q] - a->at[A_BX + q]; }
                float nx = fabsf(e1[1] * e2[2] - e1[2] * e2[1]), ny = fabsf(e1[2] * e2[0] - e1[0] * e2[2]), nz = fabsf(e1[0] * e2[1] - e1[1] * e2[0]);
                C.paxis = (nx >= ny && nx >= nz) ? 0 : (ny >= nz ? 1 : 2);
            }
            bool dw = C.dt && C.m2[ARC__MAT2_DSCALE] > 0, bw = C.m2 && C.m2[ARC__MAT2_BSCALE] > 0;
            if (dw || bw) {   /* 0.8.4 detail / 0.8.8 colour plane: the one the face normal points along most */
                float e1[3], e2[3];
                for (int q = 0; q < 3; q++) { e1[q] = bb->at[A_WX + q] - a->at[A_WX + q]; e2[q] = c->at[A_WX + q] - a->at[A_WX + q]; }
                float nx = fabsf(e1[1] * e2[2] - e1[2] * e2[1]), ny = fabsf(e1[2] * e2[0] - e1[0] * e2[2]), nz = fabsf(e1[0] * e2[1] - e1[1] * e2[0]);
                int pl = (ny >= nx && ny >= nz) ? 0 : (nx >= nz ? 1 : 2);
                if (dw) C.dplane = pl;
                if (bw) C.bplane = pl;
            }
            if (C.mt[0]) {                     /* normal map frame for this triangle: T = dP/du, B = -dP/dv (world) */
                float e1[3], e2[3], du1 = bb->at[A_U] - a->at[A_U], dv1 = bb->at[A_V] - a->at[A_V], du2 = c->at[A_U] - a->at[A_U], dv2 = c->at[A_V] - a->at[A_V];
                for (int q = 0; q < 3; q++) { e1[q] = bb->at[A_WX + q] - a->at[A_WX + q]; e2[q] = c->at[A_WX + q] - a->at[A_WX + q]; }
                float det = du1 * dv2 - du2 * dv1;
                C.tb = fabsf(det) > 1e-20f;
                if (C.tb) { float r = 1.0f / det; for (int q = 0; q < 3; q++) { C.T[q] = (dv2 * e1[q] - dv1 * e2[q]) * r; C.B[q] = -(du1 * e2[q] - du2 * e1[q]) * r; } }
            }
            if (a->w > 0 && bb->w > 0 && c->w > 0 && (C.zrev ? a->z <= a->w && bb->z <= bb->w && c->z <= c->w : a->z >= -a->w && bb->z >= -bb->w && c->z >= -c->w)) {
                v3 x = *a, y = *bb, z = *c; to_screen3(&x); to_screen3(&y); to_screen3(&z); raster3d(&x, &y, &z, &C);
            } else clip_raster3(*a, *bb, *c, &C);
        }
    }
}
static void draw_lines(const arc__frame_data *f, const arc__pass *P) {
    for (int i = P->line_first; i + 1 < P->line_first + P->line_count; i += 2) {
        const arc__line_vertex *a = &f->lines[i], *b = &f->lines[i + 1];
        arc_v4 ca = arc_m4_mul_v4(P->view_proj, (arc_v4){ a->x, a->y, a->z, 1 }), cb2 = arc_m4_mul_v4(P->view_proj, (arc_v4){ b->x, b->y, b->z, 1 });
        float da = P->zrev ? ca.w - ca.z : ca.z + ca.w, db = P->zrev ? cb2.w - cb2.z : cb2.z + cb2.w;   /* the near plane (reversed: z = w) */
        if (da < 0 && db < 0) continue;
        if (da < 0) { float t = da / (da - db); ca.x += (cb2.x - ca.x) * t; ca.y += (cb2.y - ca.y) * t; ca.z += (cb2.z - ca.z) * t; ca.w += (cb2.w - ca.w) * t; }
        if (db < 0) { float t = db / (db - da); cb2.x += (ca.x - cb2.x) * t; cb2.y += (ca.y - cb2.y) * t; cb2.z += (ca.z - cb2.z) * t; cb2.w += (ca.w - cb2.w) * t; }
        float x0 = (ca.x / ca.w * 0.5f + 0.5f) * (float)S.cw, y0 = (0.5f - ca.y / ca.w * 0.5f) * (float)S.ch, z0 = ca.z / ca.w;
        float x1 = (cb2.x / cb2.w * 0.5f + 0.5f) * (float)S.cw, y1 = (0.5f - cb2.y / cb2.w * 0.5f) * (float)S.ch, z1 = cb2.z / cb2.w;
        int steps = (int)arc_maxf(fabsf(x1 - x0), fabsf(y1 - y0)) + 1; if (steps > 8192) steps = 8192;
        for (int s2 = 0; s2 <= steps; s2++) {
            float t = (float)s2 / (float)steps; int x = (int)(x0 + (x1 - x0) * t), y = (int)(y0 + (y1 - y0) * t); float z = z0 + (z1 - z0) * t;
            if (x < 0 || y < 0 || x >= S.cw || y >= S.ch) continue;
            size_t k = (size_t)y * S.cw + x; if (P->zrev ? z < S.depth[k] - 0.5e-4f : z > S.depth[k] + 1e-4f) continue;   /* (the same slack: reversed depth = (1 - ndc) / 2) */
            float g = a->gain > 0 ? a->gain : 1.0f;   /* 0.10.11 glow */
            if (S.fcolor) { float *o = S.fcolor + k * 4; o[0] = (uint8_t)(a->color) / 255.0f * g; o[1] = (uint8_t)(a->color >> 8) / 255.0f * g; o[2] = (uint8_t)(a->color >> 16) / 255.0f * g; o[3] = 1; continue; }
            uint8_t *o = S.color + k * 4;
            if (g == 1.0f) { o[0] = (uint8_t)(a->color); o[1] = (uint8_t)(a->color >> 8); o[2] = (uint8_t)(a->color >> 16); }
            else for (int q = 0; q < 3; q++) { float vq = (float)(uint8_t)(a->color >> (8 * q)) * g; o[q] = (uint8_t)(vq > 255.0f ? 255.0f : vq); }
            o[3] = 255;
        }
    }
}
/* 0.10.11 thick lines: triangles with per-corner colour (alpha = coverage), depth tested (no write), blended over,
   colour times gain. Clipped against the near plane in clip space, then filled with edge functions. */
typedef struct { float x, y, z, w, c[4], g; } ltv;
static void ltri_fill(const arc__pass *P, ltv v[3]) {
    float X[3], Y[3], Z[3];
    for (int i = 0; i < 3; i++) { X[i] = (v[i].x / v[i].w * 0.5f + 0.5f) * (float)S.cw; Y[i] = (0.5f - v[i].y / v[i].w * 0.5f) * (float)S.ch; Z[i] = v[i].z / v[i].w; }
    float area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (fabsf(area) < 1e-12f) return;
    int x0 = (int)floorf(arc_minf(X[0], arc_minf(X[1], X[2]))), x1 = (int)ceilf(arc_maxf(X[0], arc_maxf(X[1], X[2])));
    int y0 = (int)floorf(arc_minf(Y[0], arc_minf(Y[1], Y[2]))), y1 = (int)ceilf(arc_maxf(Y[0], arc_maxf(Y[1], Y[2])));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > S.cw - 1) x1 = S.cw - 1;
    if (y1 > S.ch - 1) y1 = S.ch - 1;
    float inv = 1.0f / area;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float px = (float)x + 0.5f, py = (float)y + 0.5f;
        float b0 = ((X[1] - px) * (Y[2] - py) - (X[2] - px) * (Y[1] - py)) * inv;
        float b1 = ((X[2] - px) * (Y[0] - py) - (X[0] - px) * (Y[2] - py)) * inv;
        float b2 = 1.0f - b0 - b1;
        if (b0 < 0 || b1 < 0 || b2 < 0) continue;
        float z = b0 * Z[0] + b1 * Z[1] + b2 * Z[2];
        size_t k = (size_t)y * S.cw + x; if (P->zrev ? z < S.depth[k] - 0.5e-4f : z > S.depth[k] + 1e-4f) continue;
        float a = b0 * v[0].c[3] + b1 * v[1].c[3] + b2 * v[2].c[3], g = b0 * v[0].g + b1 * v[1].g + b2 * v[2].g;
        if (a <= 0.0f) continue;
        float rgb[3]; for (int q = 0; q < 3; q++) rgb[q] = (b0 * v[0].c[q] + b1 * v[1].c[q] + b2 * v[2].c[q]) * g;
        if (S.fcolor) { float *o = S.fcolor + k * 4; for (int q = 0; q < 3; q++) o[q] = o[q] * (1.0f - a) + rgb[q] * a; o[3] = 1; continue; }
        uint8_t *o = S.color + k * 4;
        for (int q = 0; q < 3; q++) {   /* (an 8-bit target clamps the colour before blending, as GPUs do) */
            float vq = (float)o[q] * (1.0f - a) + arc_minf(rgb[q], 1.0f) * 255.0f * a; o[q] = (uint8_t)(vq > 255.0f ? 255.0f : vq + 0.5f);
        }
        o[3] = 255;
    }
}
static void draw_ltris(const arc__frame_data *f, const arc__pass *P) {
    for (int i = P->ltri_first; i + 2 < P->ltri_first + P->ltri_count; i += 3) {
        ltv in[3], poly[4]; int n = 0;
        for (int k = 0; k < 3; k++) {
            const arc__line_vertex *s0 = &f->ltris[i + k];
            arc_v4 c = arc_m4_mul_v4(P->view_proj, (arc_v4){ s0->x, s0->y, s0->z, 1 });
            in[k].x = c.x; in[k].y = c.y; in[k].z = c.z; in[k].w = c.w; in[k].g = s0->gain > 0 ? s0->gain : 1.0f;
            for (int q = 0; q < 4; q++) in[k].c[q] = (float)(uint8_t)(s0->color >> (8 * q)) / 255.0f;
        }
        for (int k = 0; k < 3; k++) {   /* keep the part in front of the near plane (reversed: z = w) */
            const ltv *A = &in[k], *B = &in[(k + 1) % 3];
            float da = P->zrev ? A->w - A->z : A->z + A->w, db = P->zrev ? B->w - B->z : B->z + B->w;
            if (da >= 0) poly[n++] = *A;
            if ((da >= 0) != (db >= 0) && n < 4) {
                float t = da / (da - db); ltv m;
                m.x = A->x + (B->x - A->x) * t; m.y = A->y + (B->y - A->y) * t; m.z = A->z + (B->z - A->z) * t; m.w = A->w + (B->w - A->w) * t;
                m.g = A->g + (B->g - A->g) * t; for (int q = 0; q < 4; q++) m.c[q] = A->c[q] + (B->c[q] - A->c[q]) * t;
                poly[n++] = m;
            }
        }
        for (int k = 1; k + 1 < n; k++) { ltv t3[3] = { poly[0], poly[k], poly[k + 1] }; if (t3[0].w > 0 && t3[1].w > 0 && t3[2].w > 0) ltri_fill(P, t3); }
    }
}
/* order inside a pass: background and solid meshes, immediate triangles, lines, then see-through/additive meshes
   (back to front) and the overlay layer; each batch brings its own blend and depth state */
static void draw_3d(const arc__frame_data *f, const arc__pass *P) {
    for (int b = P->mb_first; b < P->mb_first + P->mb_count; b++) draw_batch(f, P, &f->mesh_batches[b], f->mesh_batches[b].blend);
    if (P->imm_count) draw_batch(f, P, NULL, ARC__MB_OPAQUE);
    draw_lines(f, P);
    if (P->ltri_count) draw_ltris(f, P);   /* 0.10.11 */
    for (int b = P->mb_first + P->mb_count; b < P->mb_first + P->mb_count + P->tb_count; b++) draw_batch(f, P, &f->mesh_batches[b], f->mesh_batches[b].blend);
}

static void draw_2d(const arc__frame_data *f, const arc__pass *P) {
    for (int bi = P->b2_first; bi < P->b2_first + P->b2_count; bi++) {
        const arc__batch2d *bt = &f->batches2d[bi]; const arc__tex_rec *tex = &arc__textures[bt->texture];
        S.sdf2d = bt->sdf;
        S.blend = bt->blend; S.clip = bt->clip != 0; S.x0 = bt->cx; S.y0 = bt->cy; S.x1 = bt->cx + bt->cw; S.y1 = bt->cy + bt->ch;
        for (int i = bt->first; i + 2 < bt->first + bt->count; i += 3) {
            sv v[3];
            for (int k = 0; k < 3; k++) {
                const arc__vert2d *q = &f->verts2d[i + k];
                v[k].x = q->x; v[k].y = q->y; v[k].z = 0; v[k].w = 1; v[k].u = q->u; v[k].v = q->v;
                v[k].r = (q->color & 0xFF) / 255.0f; v[k].g = ((q->color >> 8) & 0xFF) / 255.0f; v[k].b = ((q->color >> 16) & 0xFF) / 255.0f; v[k].a = (q->color >> 24) / 255.0f;
            }
            raster2d(&v[0], &v[1], &v[2], tex);
        }
    }
}

/* ---- shadow map: the pass's solid meshes seen from the light, depth only ---- */
static const arc__tex_rec *g_lsh_tex;
static void shadow_tri(const float *a, const float *b, const float *c, const arc__tex_rec *tex) {   /* x, y in pixels, depth, u, v */
    float area = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
    if (fabsf(area) < 1e-12f) return;
    if (area > 0) { const float *t = b; b = c; c = t; area = -area; }
    int minx = arc_maxi((int)floorf(arc_minf(a[0], arc_minf(b[0], c[0]))), S.sx0), maxx = arc_mini((int)ceilf(arc_maxf(a[0], arc_maxf(b[0], c[0]))), S.sx0 + S.scn - 1);
    int miny = arc_maxi((int)floorf(arc_minf(a[1], arc_minf(b[1], c[1]))), S.sy0), maxy = arc_mini((int)ceilf(arc_maxf(a[1], arc_maxf(b[1], c[1]))), S.sy0 + S.scn - 1);
    float inv = 1.0f / area;
    bool cut = tex && tex->rgba;
    for (int y = miny; y <= maxy; y++) {
        float py = (float)y + 0.5f;
        for (int x = minx; x <= maxx; x++) {
            float px = (float)x + 0.5f;
            float e0 = (c[0] - b[0]) * (py - b[1]) - (c[1] - b[1]) * (px - b[0]);
            float e1 = (a[0] - c[0]) * (py - c[1]) - (a[1] - c[1]) * (px - c[0]);
            float e2 = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
            if (e0 > 0 || e1 > 0 || e2 > 0) continue;
            float w0 = e0 * inv, w1 = e1 * inv, w2 = 1.0f - w0 - w1;
            float z = w0 * a[2] + w1 * b[2] + w2 * c[2];
            float *d = &S.smap[(size_t)y * S.sstride + x];
            if (z >= *d || z < 0) continue;
            if (cut) { float t[4]; sample_lod(tex, 0, w0 * a[3] + w1 * b[3] + w2 * c[3], w0 * a[4] + w1 * b[4] + w2 * c[4], t); if (t[3] < 0.5f) continue; }
            if (S.sdis > 0 && dissolve_noise(w0 * a[5] + w1 * b[5] + w2 * c[5], w0 * a[6] + w1 * b[6] + w2 * c[6], w0 * a[7] + w1 * b[7] + w2 * c[7]) < S.sdis) continue;
            *d = z;
        }
    }
}
static void shadow_cascade(const arc__frame_data *f, const arc__pass *P, int ci, int n);
static void shadow_pass(const arc__frame_data *f, const arc__pass *P) {
    int res = P->shadow_res, n = P->shadow_n > 1 ? res * 2 : res;
    if (S.sres != n) { free(S.smap); S.smap = (float *)arc__xalloc(sizeof(float) * (size_t)n * n); S.sres = n; }
    for (size_t i = 0, k = (size_t)n * n; i < k; i++) S.smap[i] = 1.0f;
    S.sstride = n;
    for (int ci = 0; ci < P->shadow_n; ci++) shadow_cascade(f, P, ci, res);
}
static void shadow_cascade(const arc__frame_data *f, const arc__pass *P, int ci, int n) {   /* n: the cascade's size */
    static float *tv = NULL; static int tv_cap = 0;
    arc_m4 svp; memcpy(svp.m, P->shadow_n > 1 ? P->shadow_cvp + ci * 16 : P->shadow_vp, sizeof svp.m);
    S.sx0 = (int)(P->shadow_cp[ci * 4 + 2] * (float)S.sres + 0.5f); S.sy0 = (int)(P->shadow_cp[ci * 4 + 3] * (float)S.sres + 0.5f); S.scn = n;
    if (P->shadow_n <= 1) S.sx0 = S.sy0 = 0;
    for (int bi = P->mb_first; bi < P->mb_first + P->mb_count; bi++) {
        const arc__mesh_batch *mb = &f->mesh_batches[bi]; const arc__mesh_rec *M = &arc__meshes[mb->mesh];
        if (!M->used || M->no_shadow || !M->v || mb->blend || mb->depth) continue;
        const arc__tex_rec *tex = (M->texture && arc__textures[M->texture].used && !M->layers_tex) ? &arc__textures[M->texture] : NULL;   /* (0.9.6 a splat map is no cut-out) */
        if (M->vcount > tv_cap) { tv_cap = M->vcount; tv = (float *)realloc(tv, sizeof(float) * 8 * (size_t)tv_cap); }
        for (int k = 0; k < mb->count; k++) {
            const arc__instance *in = &f->instances[mb->first + k];
            if (arc__ext_unlit(in->extra) || in->fx[3] > 0) continue;
            arc_m4 model; memcpy(model.m, in->model, sizeof model.m);
            arc_m4 mvp = arc_m4_mul(svp, model);
            S.sdis = in->fx[0];
            int bone0 = (M->skin && in->extra[1] > 0.5f) ? (int)in->extra[0] : -1;
            for (int i = 0; i < M->vcount; i++) {
                const arc_vertex *v = &M->v[i]; arc_v3 p = arc_v3_make(v->px, v->py, v->pz);
                if (bone0 >= 0) {
                    const uint8_t *sk = M->skin + (size_t)i * 8; float m[12] = { 0 };
                    for (int j = 0; j < 4; j++) {
                        if (!sk[4 + j]) continue;
                        int b = bone0 + sk[j]; if (b >= f->bone_count) continue;
                        const float *B = f->bones + (size_t)b * 12; float w = sk[4 + j] / 255.0f;
                        for (int q = 0; q < 12; q++) m[q] += B[q] * w;
                    }
                    p = arc_v3_make(m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7], m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]);
                }
                arc_v3 c = arc_m4_mul_point(mvp, p);
                float *o = tv + (size_t)i * 8;
                o[0] = (c.x * 0.5f + 0.5f) * (float)n + (float)S.sx0; o[1] = (c.y * 0.5f + 0.5f) * (float)n + (float)S.sy0; o[2] = c.z * 0.5f + 0.5f; o[3] = v->u; o[4] = v->v;
                o[5] = p.x; o[6] = p.y; o[7] = p.z;
            }
            for (int t = 0; t + 2 < M->icount; t += 3) shadow_tri(tv + (size_t)M->idx[t] * 8, tv + (size_t)M->idx[t + 1] * 8, tv + (size_t)M->idx[t + 2] * 8, tex);
        }
    }
}

/* ---- 0.9.5 light shadows: the pass's solid meshes from each shadowed light's faces (see arc__pass.lsh_n);
   perspective, so triangles are cut at the near plane first ---- */
static void lsh_tri(float (*cv)[9], int r) {   /* cv: 3 clip-space vertices (x y z w, u v, ox oy oz); cut at z = -w */
    float poly[6][9]; int np = 0;
    for (int i = 0; i < 3; i++) {
        const float *a = cv[i], *b = cv[(i + 1) % 3]; float da = a[2] + a[3], db = b[2] + b[3];
        if (da >= 0) memcpy(poly[np++], a, sizeof poly[0]);
        if ((da >= 0) != (db >= 0)) { float t = da / (da - db); for (int k = 0; k < 9; k++) poly[np][k] = a[k] + (b[k] - a[k]) * t; np++; }
    }
    if (np < 3) return;
    float sv[6][8];
    for (int i = 0; i < np; i++) {
        float iw = 1.0f / arc_maxf(poly[i][3], 1e-9f);
        sv[i][0] = (poly[i][0] * iw * 0.5f + 0.5f) * (float)r + (float)S.sx0; sv[i][1] = (poly[i][1] * iw * 0.5f + 0.5f) * (float)r + (float)S.sy0;
        sv[i][2] = poly[i][2] * iw * 0.5f + 0.5f; for (int k = 3; k < 8; k++) sv[i][k] = poly[i][k + 1];
    }
    for (int i = 1; i + 1 < np; i++) shadow_tri(sv[0], sv[i], sv[i + 1], g_lsh_tex);
}
static void lshadow_pass(const arc__frame_data *f, const arc__pass *P) {
    static float *cv = NULL; static int cv_cap = 0;
    int r = f->lsh_res, w = r * 6, h = r * 4;
    if (S.lres != r) { free(S.lmap); S.lmap = (float *)arc__xalloc(sizeof(float) * (size_t)w * h); S.lres = r; }
    for (size_t i = 0, k = (size_t)w * h; i < k; i++) S.lmap[i] = 1.0f;
    float *sm = S.smap; int ss = S.sstride; S.smap = S.lmap; S.sstride = w; S.scn = r;
    for (int sl = 0; sl < P->lsh_n; sl++) for (int fi = 0; fi < f->lsh_faces[sl]; fi++) {
        arc_m4 svp; memcpy(svp.m, f->lsh_vp + (sl * 6 + fi) * 16, sizeof svp.m);
        S.sx0 = fi * r; S.sy0 = sl * r;
        for (int bi = P->mb_first; bi < P->mb_first + P->mb_count; bi++) {
            const arc__mesh_batch *mb = &f->mesh_batches[bi]; const arc__mesh_rec *M = &arc__meshes[mb->mesh];
            if (!M->used || M->no_shadow || !M->v || mb->blend || mb->depth) continue;
            g_lsh_tex = (M->texture && arc__textures[M->texture].used && !M->layers_tex) ? &arc__textures[M->texture] : NULL;
            if (M->vcount > cv_cap) { cv_cap = M->vcount; cv = (float *)realloc(cv, sizeof(float) * 9 * (size_t)cv_cap); }
            for (int k = 0; k < mb->count; k++) {
                const arc__instance *in = &f->instances[mb->first + k];
                if (arc__ext_unlit(in->extra) || in->fx[3] > 0) continue;
                arc_m4 model; memcpy(model.m, in->model, sizeof model.m);
                arc_m4 mvp = arc_m4_mul(svp, model); const float *m = mvp.m;
                S.sdis = in->fx[0];
                int bone0 = (M->skin && in->extra[1] > 0.5f) ? (int)in->extra[0] : -1;
                for (int i = 0; i < M->vcount; i++) {
                    const arc_vertex *v = &M->v[i]; arc_v3 p = arc_v3_make(v->px, v->py, v->pz);
                    if (bone0 >= 0) {
                        const uint8_t *sk = M->skin + (size_t)i * 8; float b12[12] = { 0 };
                        for (int j = 0; j < 4; j++) {
                            if (!sk[4 + j]) continue;
                            int b = bone0 + sk[j]; if (b >= f->bone_count) continue;
                            const float *B = f->bones + (size_t)b * 12; float wt = sk[4 + j] / 255.0f;
                            for (int q = 0; q < 12; q++) b12[q] += B[q] * wt;
                        }
                        p = arc_v3_make(b12[0] * p.x + b12[1] * p.y + b12[2] * p.z + b12[3], b12[4] * p.x + b12[5] * p.y + b12[6] * p.z + b12[7], b12[8] * p.x + b12[9] * p.y + b12[10] * p.z + b12[11]);
                    }
                    float *o = cv + (size_t)i * 9;
                    for (int c = 0; c < 4; c++) o[c] = m[c] * p.x + m[4 + c] * p.y + m[8 + c] * p.z + m[12 + c];
                    o[4] = v->u; o[5] = v->v; o[6] = p.x; o[7] = p.y; o[8] = p.z;
                }
                for (int t = 0; t + 2 < M->icount; t += 3) {
                    float tri[3][9];
                    memcpy(tri[0], cv + (size_t)M->idx[t] * 9, sizeof tri[0]); memcpy(tri[1], cv + (size_t)M->idx[t + 1] * 9, sizeof tri[0]); memcpy(tri[2], cv + (size_t)M->idx[t + 2] * 9, sizeof tri[0]);
                    lsh_tri(tri, r);
                }
            }
        }
    }
    S.smap = sm; S.sstride = ss;
}

/* ---- post-processing: bloom (bright pass, 5 levels down, tent filter back up) and the composite ---- */
static void bilin(const float *buf, int w, int h, float u, float v, float out[4]) {   /* clamp to edge */
    float fx = u * (float)w - 0.5f, fy = v * (float)h - 0.5f; int ix = (int)floorf(fx), iy = (int)floorf(fy); float ax = fx - (float)ix, ay = fy - (float)iy;
    out[0] = out[1] = out[2] = out[3] = 0;
    for (int k = 0; k < 4; k++) {
        int x = arc_clampi(ix + (k & 1), 0, w - 1), y = arc_clampi(iy + (k >> 1), 0, h - 1);
        float wgt = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay); const float *p = buf + ((size_t)y * w + x) * 4;
        for (int c = 0; c < 4; c++) out[c] += p[c] * wgt;
    }
}
static void bilin2(const float *buf, int w, int h, float u, float v, float out[2]) {   /* 2 channels, clamp to edge */
    float fx = u * (float)w - 0.5f, fy = v * (float)h - 0.5f; int ix = (int)floorf(fx), iy = (int)floorf(fy); float ax = fx - (float)ix, ay = fy - (float)iy;
    out[0] = out[1] = 0;
    for (int k = 0; k < 4; k++) {
        int x = arc_clampi(ix + (k & 1), 0, w - 1), y = arc_clampi(iy + (k >> 1), 0, h - 1);
        float wgt = ((k & 1) ? ax : 1 - ax) * ((k >> 1) ? ay : 1 - ay); const float *p = buf + ((size_t)y * w + x) * 2;
        out[0] += p[0] * wgt; out[1] += p[1] * wgt;
    }
}
static float post_lum(const float *c) { return c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f; }
/* a blur chain: levels halvings with 4-tap boxes (the first with a bright pass when threshold >= 0), then back up
   with a 3x3 tent of radius r added to each level */
static void soft_chain(float **down, float **up, int *bw, int *bh, int levels, float threshold, float r) {
    int w = S.hw, h = S.hh;
    for (int l = 0; l < levels; l++) {
        int lw = arc_maxi(1, w >> (l + 1)), lh = arc_maxi(1, h >> (l + 1));
        if (bw[l] != lw || bh[l] != lh || !down[l]) {
            free(down[l]); free(up[l]); bw[l] = lw; bh[l] = lh;
            down[l] = (float *)arc__xalloc(sizeof(float) * 4 * (size_t)lw * lh); up[l] = (float *)arc__xalloc(sizeof(float) * 4 * (size_t)lw * lh);
        }
        const float *src = l ? down[l - 1] : S.hdr; int sw = l ? bw[l - 1] : w, sh = l ? bh[l - 1] : h;
        for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) {
            float u = ((float)x + 0.5f) / (float)lw, v = ((float)y + 0.5f) / (float)lh, acc[4] = { 0, 0, 0, 0 }, t[4];
            for (int k = 0; k < 4; k++) {   /* 4 bilinear taps one source texel off the centre: a 4x4 box */
                bilin(src, sw, sh, u + ((k & 1) ? 1.0f : -1.0f) / (float)sw, v + ((k >> 1) ? 1.0f : -1.0f) / (float)sh, t);
                for (int c = 0; c < 3; c++) acc[c] += t[c] * 0.25f;
            }
            if (l == 0 && threshold >= 0) {   /* bright pass */
                float lu = post_lum(acc), k = lu > 1e-4f ? arc_maxf(lu - threshold, 0) / lu : 0;
                for (int c = 0; c < 3; c++) acc[c] = arc_minf(acc[c] * k, 64.0f);
            }
            float *o = down[l] + ((size_t)y * lw + x) * 4; o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2]; o[3] = 1;
        }
    }
    for (int l = levels - 1; l >= 0; l--) {
        int lw = bw[l], lh = bh[l];
        for (int y = 0; y < lh; y++) for (int x = 0; x < lw; x++) {
            float *o = up[l] + ((size_t)y * lw + x) * 4; const float *d = down[l] + ((size_t)y * lw + x) * 4;
            o[0] = d[0]; o[1] = d[1]; o[2] = d[2]; o[3] = 1;
            if (l == levels - 1) continue;
            const float *src = up[l + 1]; int sw = bw[l + 1], sh = bh[l + 1];
            float u = ((float)x + 0.5f) / (float)lw, v = ((float)y + 0.5f) / (float)lh, t[4];
            for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {   /* 3x3 tent */
                float wgt = (float)((i ? 1 : 2) * (j ? 1 : 2)) / 16.0f;
                bilin(src, sw, sh, u + (float)i * r / (float)sw, v + (float)j * r / (float)sh, t);
                for (int c = 0; c < 3; c++) o[c] += t[c] * wgt;
            }
        }
    }
}
/* ---- ssao (all renderers): from the depth buffer only ----
   P = the pixel's world point (inverse view-projection); N from the neighbour with the smaller depth step in x and y;
   16 kernel points in the hemisphere around N (radius R), turned per pixel by a 4x4 Bayer angle; a point counts as
   hidden when the surface seen there is nearer the camera (linear depth: clip w, or z when orthographic) by more
   than 0.025 R, weighted by clamp(2 - |Q - P| / R, 0, 1). ao = 1 - hidden / 16. */
const float arc__ssao_kernel[48] = { 0.0257f, 0.0000f, 0.1511f, -0.0350f, 0.0321f, 0.1562f, 0.0059f, -0.0673f, 0.1667f, 0.0550f, 0.0717f, 0.1819f,
    -0.1159f, -0.0205f, 0.2011f, 0.1272f, -0.0809f, 0.2235f, -0.0494f, 0.1838f, 0.2481f, -0.1094f, -0.2106f, 0.2740f, 0.2746f, 0.1003f, 0.3000f,
    -0.3293f, 0.1359f, 0.3247f, 0.1821f, -0.3890f, 0.3463f, 0.1535f, 0.4894f, 0.3626f, -0.5252f, -0.3044f, 0.3705f, 0.6962f, -0.1530f, 0.3650f,
    -0.4782f, 0.6801f, 0.3369f, -0.1241f, -0.9578f, 0.2591f };
const float arc__bayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static int g_ssao_rev;   /* 0.7.2: the pass is reversed-Z: window depth = what the buffer holds, 0 = nothing drawn */
static float sdepth(int x, int y) {   /* window depth 0..1 (1 = nothing drawn; reversed: 0) */
    x = arc_clampi(x, 0, S.hw - 1); y = arc_clampi(y, 0, S.hh - 1);
    return g_ssao_rev ? S.screen_depth[(size_t)y * S.hw + x] : S.screen_depth[(size_t)y * S.hw + x] * 0.5f + 0.5f;
}
static bool sempty(float d) { return g_ssao_rev ? d <= 0.0f : d >= 1.0f; }
static arc_v3 sworld(const float *ivp, int x, int y, float d) {
    float nx = ((float)x + 0.5f) / (float)S.hw * 2 - 1, ny = 1 - ((float)y + 0.5f) / (float)S.hh * 2, nz = d * 2 - 1, o[4];
    for (int r = 0; r < 4; r++) o[r] = ivp[r] * nx + ivp[4 + r] * ny + ivp[8 + r] * nz + ivp[12 + r];
    return arc_v3_make(o[0] / o[3], o[1] / o[3], o[2] / o[3]);
}
static void soft_ssao(const float *ivp, const float *vp, float R) {
    int w = S.hw, h = S.hh;
    if (!S.ao) S.ao = (float *)arc__xalloc(sizeof(float) * 2 * (size_t)w * h);
    bool persp = fabsf(vp[3]) + fabsf(vp[7]) + fabsf(vp[11]) > 1e-6f;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float *o = S.ao + ((size_t)y * w + x) * 2, d = sdepth(x, y);
        if (sempty(d)) { o[0] = o[1] = 0; continue; }
        arc_v3 P = sworld(ivp, x, y, d);
        float dl = sdepth(x - 1, y), dr = sdepth(x + 1, y), du = sdepth(x, y - 1), dd = sdepth(x, y + 1);
        arc_v3 ex = fabsf(dr - d) < fabsf(d - dl) ? arc_v3_sub(sworld(ivp, x + 1, y, dr), P) : arc_v3_sub(P, sworld(ivp, x - 1, y, dl));
        arc_v3 ey = fabsf(dd - d) < fabsf(d - du) ? arc_v3_sub(sworld(ivp, x, y + 1, dd), P) : arc_v3_sub(P, sworld(ivp, x, y - 1, du));
        arc_v3 N = arc_v3_norm(arc_v3_cross(ey, ex));
        float ang = arc__bayer4[(y & 3) * 4 + (x & 3)] * (2.0f * ARC_PI / 16.0f), ca = cosf(ang), sa = sinf(ang);
        arc_v3 hlp = fabsf(N.y) < 0.99f ? arc_v3_make(0, 1, 0) : arc_v3_make(1, 0, 0);
        arc_v3 T0 = arc_v3_norm(arc_v3_cross(hlp, N)), B0 = arc_v3_cross(N, T0);
        arc_v3 T = arc_v3_add(arc_v3_scale(T0, ca), arc_v3_scale(B0, sa)), B = arc_v3_cross(N, T);
        float occ = 0;
        for (int k = 0; k < 16; k++) {
            const float *kk = arc__ssao_kernel + k * 3;
            arc_v3 sp = arc_v3_add(P, arc_v3_scale(arc_v3_add(arc_v3_add(arc_v3_scale(T, kk[0]), arc_v3_scale(B, kk[1])), arc_v3_scale(N, kk[2])), R));
            float cx = vp[0] * sp.x + vp[4] * sp.y + vp[8] * sp.z + vp[12], cy = vp[1] * sp.x + vp[5] * sp.y + vp[9] * sp.z + vp[13];
            float cz = vp[2] * sp.x + vp[6] * sp.y + vp[10] * sp.z + vp[14], cw = vp[3] * sp.x + vp[7] * sp.y + vp[11] * sp.z + vp[15];
            if (cw <= 1e-6f) continue;
            float sx = (cx / cw * 0.5f + 0.5f) * (float)w, sy = (0.5f - cy / cw * 0.5f) * (float)h;
            if (sx < 0 || sy < 0 || sx >= (float)w || sy >= (float)h) continue;
            int ix = (int)sx, iy = (int)sy; float qd = sdepth(ix, iy);
            if (sempty(qd)) continue;
            arc_v3 Q = sworld(ivp, ix, iy, qd);
            float lq = persp ? vp[3] * Q.x + vp[7] * Q.y + vp[11] * Q.z + vp[15] : vp[2] * Q.x + vp[6] * Q.y + vp[10] * Q.z + vp[14];
            float ls = persp ? cw : cz;
            if (!persp && g_ssao_rev) { lq = -lq; ls = -ls; }   /* reversed orthographic: clip z falls with distance */
            if (persp ? lq < ls - 0.025f * R : lq < ls - 0.025f * R * fabsf(vp[10])) occ += arc_clampf(2.0f - arc_v3_dist(Q, P) / R, 0, 1);
        }
        o[0] = 1.0f - occ / 16.0f; o[1] = 1;
    }
}
static float soft_ao_at(float u, float v) {   /* 3x3 bilinear taps; sky keeps 1 */
    int w = S.hw, h = S.hh; float c[4];
    bilin2(S.ao, w, h, u, v, c);
    if (c[1] < 0.5f) return 1.0f;
    float sr = 0, sg = 0;
    for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) {
        bilin2(S.ao, w, h, u + (float)i * 1.5f / (float)w, v + (float)j * 1.5f / (float)h, c); sr += c[0]; sg += c[1];
    }
    return sg > 1e-6f ? sr / sg : 1.0f;
}
static float fx_luma(const float *c) { return sqrtf(arc_clampf(c[0], 0, 1) * 0.299f + arc_clampf(c[1], 0, 1) * 0.587f + arc_clampf(c[2], 0, 1) * 0.114f); }
/* fxaa (all renderers), after FXAA 3.11: luma = sqrt(dot(clamp(c), 0.299/0.587/0.114)); low contrast -> as it is;
   edge direction from the 3x3 lumas; search along the edge (8 steps: 1 1 1 1 1.5 2 2 4 texels) for its ends; move
   across the edge by 0.5 - (distance to the nearer end) / (span) when that end is the right kind, at least the
   sub-pixel amount ((2 (N+S+E+W) + corners) / 12 vs the centre, smoothstep squared * 0.75) */
static float fx_l(float u, float v) { float c[4]; bilin(S.hdr, S.hw, S.hh, u, v, c); return fx_luma(c); }
static void soft_fxaa(float u, float v, float out[4]) {
    static const float Q[8] = { 1, 1, 1, 1, 1.5f, 2, 2, 4 };
    float tx = 1.0f / (float)S.hw, ty = 1.0f / (float)S.hh, c[4];
    bilin(S.hdr, S.hw, S.hh, u, v, c);
    float M = fx_luma(c), N = fx_l(u, v - ty), So = fx_l(u, v + ty), E = fx_l(u + tx, v), W = fx_l(u - tx, v);
    float mn = fminf(M, fminf(fminf(N, So), fminf(E, W))), mx = fmaxf(M, fmaxf(fmaxf(N, So), fmaxf(E, W))), range = mx - mn;
    if (range < fmaxf(0.0312f, 0.125f * mx)) { memcpy(out, c, sizeof c); return; }
    float NW = fx_l(u - tx, v - ty), NE = fx_l(u + tx, v - ty), SW = fx_l(u - tx, v + ty), SE = fx_l(u + tx, v + ty);
    float sub = arc_clampf(fabsf((2 * (N + So + E + W) + NW + NE + SW + SE) / 12.0f - M) / range, 0, 1); sub = sub * sub * (3 - 2 * sub); sub = sub * sub * 0.75f;
    float eh = fabsf(NW + SW - 2 * W) + 2 * fabsf(N + So - 2 * M) + fabsf(NE + SE - 2 * E);
    float ev = fabsf(NW + NE - 2 * N) + 2 * fabsf(W + E - 2 * M) + fabsf(SW + SE - 2 * So);
    bool horz = eh >= ev;
    float nx = horz ? 0 : -tx, ny = horz ? -ty : 0, ax = horz ? tx : 0, ay = horz ? 0 : ty;   /* toward N (or W); along the edge */
    float L1 = horz ? N : W, L2 = horz ? So : E, g1 = fabsf(L1 - M), g2 = fabsf(L2 - M);
    bool p1 = g1 >= g2; float sx = p1 ? nx : -nx, sy = p1 ? ny : -ny, local = ((p1 ? L1 : L2) + M) * 0.5f, gs = fmaxf(g1, g2) * 0.25f;
    float bu = u + sx * 0.5f, bv = v + sy * 0.5f, nu = bu - ax, nv = bv - ay, pu = bu + ax, pv = bv + ay;
    float en = fx_l(nu, nv) - local, ep = fx_l(pu, pv) - local; bool dn = fabsf(en) >= gs, dp = fabsf(ep) >= gs;
    for (int i = 0; i < 8 && !(dn && dp); i++) {
        if (!dn) { nu -= ax * Q[i]; nv -= ay * Q[i]; en = fx_l(nu, nv) - local; dn = fabsf(en) >= gs; }
        if (!dp) { pu += ax * Q[i]; pv += ay * Q[i]; ep = fx_l(pu, pv) - local; dp = fabsf(ep) >= gs; }
    }
    float dstn = horz ? (u - nu) / tx : (v - nv) / ty, dstp = horz ? (pu - u) / tx : (pv - v) / ty;
    bool mlt = M - local < 0, good = dstn < dstp ? ((en < 0) != mlt) : ((ep < 0) != mlt);
    float off = good ? 0.5f - fminf(dstn, dstp) / (dstn + dstp) : 0.0f; off = fmaxf(off, sub);
    bilin(S.hdr, S.hw, S.hh, u + sx * off, v + sy * off, out);
}
static void soft_lut(const float *post, float c[3]) {
    const arc__tex_rec *t = &arc__textures[(int)post[ARC__POST_LUT]]; float n = post[ARC__POST_LUT_N], a[4], b[4];
    float r = arc_clampf(c[0], 0, 1), g = arc_clampf(c[1], 0, 1), bl = arc_clampf(c[2], 0, 1) * (n - 1);
    float b0 = floorf(bl), b1 = fminf(b0 + 1, n - 1), f = bl - b0, x = r * (n - 1) + 0.5f, y = (g * (n - 1) + 0.5f) / n;
    sample(t, (b0 * n + x) / (n * n), y, a); sample(t, (b1 * n + x) / (n * n), y, b);
    float k = post[ARC__POST_LUT_STRENGTH];
    for (int i = 0; i < 3; i++) c[i] += (a[i] + (b[i] - a[i]) * f - c[i]) * k;
}
static void soft_post(const float *post, const arc__pass *P) {
    if (S.hdr) {   /* 0.10.12: NaN / Inf scene pixels become black (magenta with `post nan on`) and are counted */
        float *c = S.hdr; size_t n = (size_t)S.hw * (size_t)S.hh;
        for (size_t k = 0; k < n; k++, c += 4) {
            bool bad = !isfinite(c[0]) || !isfinite(c[1]) || !isfinite(c[2]);
            if (bad) { arc__post_nan_count++; if (arc__post_nan) { c[0] = 1.0f; c[1] = 0.0f; c[2] = 1.0f; continue; } }
            for (int q = 0; q < 3; q++) if (!isfinite(c[q]) || c[q] < 0.0f) c[q] = 0.0f;
        }
    }
    bool bloom = post[ARC__POST_BLOOM] > 0, tilt = post[ARC__POST_TILT] > 0, ssao = post[ARC__POST_SSAO] > 0, fxaa = post[ARC__POST_FXAA] > 0;
    bool lut = post[ARC__POST_LUT] > 0 && arc__textures[(int)post[ARC__POST_LUT]].used;
    float crt = post[ARC__POST_CRT], pix = post[ARC__POST_PIXELATE], ab = post[ARC__POST_ABERRATION], scan = post[ARC__POST_SCANLINES];
    if (bloom) soft_chain(S.bloom, S.bup, S.bw, S.bh, ARC__BLOOM_LEVELS, post[ARC__POST_THRESHOLD], post[ARC__POST_RADIUS]);
    if (tilt) soft_chain(S.tdown, S.tup, S.tw, S.th, ARC__TILT_LEVELS, -1.0f, 1.0f);
    int w = S.hw, h = S.hh; float aspect = (float)w / (float)h;
    if (ssao) { if (S.aw != w || S.ah != h) { free(S.ao); S.ao = NULL; S.aw = w; S.ah = h; } g_ssao_rev = P->zrev; soft_ssao(P->ivp, P->view_proj.m, post[ARC__POST_SSAO_RADIUS]); }
    float period = fmaxf(2.0f, roundf((float)h / 270.0f));
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float c[3], b[4] = { 0, 0, 0, 0 };
        float u0 = ((float)x + 0.5f) / (float)w, v0 = ((float)y + 0.5f) / (float)h, u = u0, v = v0;
        uint8_t *o = S.screen + ((size_t)y * w + x) * 4;
        if (crt > 0) {
            float dx = u - 0.5f, dy = v - 0.5f, k = 1.0f + 0.25f * crt * (4 * dx * dx + 4 * dy * dy);
            u = 0.5f + dx * k; v = 0.5f + dy * k;
            if (u < 0 || u > 1 || v < 0 || v > 1) { o[0] = o[1] = o[2] = 0; o[3] = 255; continue; }
        }
        float vs = v;   /* scanlines follow the curved picture, not the chunky pixels */
        if (pix > 1) { u = (floorf(u * (float)w / pix) + 0.5f) * pix / (float)w; v = (floorf(v * (float)h / pix) + 0.5f) * pix / (float)h; }
        float s3[4];
        if (fxaa) soft_fxaa(u, v, s3);
        else if (crt > 0 || pix > 1) bilin(S.hdr, w, h, u, v, s3);
        else memcpy(s3, S.hdr + ((size_t)y * w + x) * 4, sizeof s3);   /* exactly the pixel */
        if (ab > 0) {
            float ox = 0.02f * ab * (u - 0.5f), oy = 0.02f * ab * (v - 0.5f), t4[4];
            bilin(S.hdr, w, h, u + ox, v + oy, t4); s3[0] = t4[0];
            bilin(S.hdr, w, h, u - ox, v - oy, t4); s3[2] = t4[2];
        }
        if (bloom) bilin(S.bup[0], S.bw[0], S.bh[0], u, v, b);
        if (tilt) {
            float tk = arc_clampf((fabsf(v - post[ARC__POST_TILT_FOCUS]) - post[ARC__POST_TILT_WIDTH]) / 0.2f, 0, 1), tb[4];
            tk = tk * tk * (3 - 2 * tk) * post[ARC__POST_TILT];
            bilin(S.tup[0], S.tw[0], S.th[0], u, v, tb);
            for (int k = 0; k < 3; k++) s3[k] += (tb[k] / ARC__TILT_LEVELS - s3[k]) * tk;   /* the chain sums its levels */
        }
        if (ssao) { float a = 1.0f + (soft_ao_at(u, v) - 1.0f) * post[ARC__POST_SSAO]; for (int k = 0; k < 3; k++) s3[k] *= a; }
        for (int k = 0; k < 3; k++) c[k] = s3[k] * post[ARC__POST_EXPOSURE] + b[k] * post[ARC__POST_BLOOM] / ARC__BLOOM_LEVELS;
        if (post[ARC__POST_TONEMAP] > 0) for (int k = 0; k < 3; k++) { float q = arc_maxf(c[k], 0); c[k] = (q * (2.51f * q + 0.03f)) / (q * (2.43f * q + 0.59f) + 0.14f); }
        float lu = c[0] * 0.2126f + c[1] * 0.7152f + c[2] * 0.0722f;
        for (int k = 0; k < 3; k++) { c[k] += (lu - c[k]) * post[ARC__POST_DESAT]; c[k] = (c[k] - 0.5f) * (1.0f + post[ARC__POST_CONTRAST]) + 0.5f; }
        if (lut) soft_lut(post, c);
        if (post[ARC__POST_VIGNETTE] > 0) {
            float dx = (u0 - 0.5f) * aspect, dy = v0 - 0.5f, d = sqrtf(dx * dx + dy * dy) / sqrtf(0.25f * aspect * aspect + 0.25f);
            float t = arc_clampf((d - 0.45f) / 0.55f, 0, 1); t = t * t * (3 - 2 * t);
            for (int k = 0; k < 3; k++) c[k] *= 1.0f - post[ARC__POST_VIGNETTE] * t;
        }
        if (scan > 0) { float m = 1.0f - scan * (0.5f - 0.5f * cosf(2.0f * ARC_PI * (vs * (float)h - 0.5f) / period)); for (int k = 0; k < 3; k++) c[k] *= m; }
        o[0] = to8(c[0]); o[1] = to8(c[1]); o[2] = to8(c[2]); o[3] = 255;
    }
}

/* 0.8.2: a target with its own post effects. After the target's last pass of the frame: post from its HDR buffer
   into its texture (ssao with the last of its passes that drew 3D), then the 2D of all its passes on top */
static void soft_target_post(const arc__frame_data *f, int pi) {
    const arc__pass *p = &f->passes[pi]; uint32_t id = p->target;
    for (int k = pi + 1; k < f->pass_count; k++) {
        const arc__pass *q = &f->passes[k];
        if (q->target == id && q->post[ARC__POST_ON] > 0) return;   /* not its last pass yet */
    }
    const arc__pass *src = p;
    for (int k = pi; k >= 0; k--) { const arc__pass *q = &f->passes[k]; if (q->target == id && q->post[ARC__POST_ON] > 0 && (q->mb_count || q->imm_count)) { src = q; break; } }
    arc__tex_rec *t = &arc__textures[id]; soft_pset *ps = pset_for(id, false);
    uint8_t *scr = S.screen; float *sdep = S.screen_depth; int sw = S.w, sh = S.h;
    pset_swap(ps);
    S.screen = t->rgba; S.screen_depth = S.tdepth[id]; S.w = t->w; S.h = t->h;
    soft_post(p->post, src);
    S.screen = scr; S.screen_depth = sdep; S.w = sw; S.h = sh;
    pset_swap(ps);
    for (int k = 0; k <= pi; k++) {
        const arc__pass *q = &f->passes[k];
        if (q->target != id || q->post[ARC__POST_ON] <= 0) continue;
        S.cw = t->w; S.ch = t->h; S.color = t->rgba; S.depth = S.tdepth[id];
        draw_2d(f, q);
    }
}
static void soft_render(const arc__frame_data *f) {
    S.na = A_BASE;   /* 0.10.1 (draw_batch sets it per batch) */
    const float *post = NULL; const arc__pass *post_pass = NULL;
    for (int pi = 0; pi < f->pass_count; pi++) {
        const arc__pass *p = &f->passes[pi];
        if ((f->targets_only && !p->target) || (f->screen_only && p->target)) continue;
        if (p->shadow[0] > 0 && p->shadow_res > 0 && p->mb_count) shadow_pass(f, p);
        if (p->lsh_n > 0 && p->mb_count) lshadow_pass(f, p);
        bind_pass(f, p);
        draw_3d(f, p);
        S.fcolor = NULL;
        if (p->mirror) { arc__tex_build_mips(&arc__textures[p->target]); continue; }   /* 0.8.7: its mip levels (blur by roughness) */
        if (p->target && p->post[ARC__POST_ON] > 0 && pset_for(p->target, false)) {   /* 0.8.2: a target's own post */
            soft_target_post(f, pi);
            continue;
        }
        if (!p->target && p->post[ARC__POST_ON] > 0) {   /* the screen's 2D comes after post; ssao uses the last camera that drew 3D */
            post = p->post; if (!post_pass || p->mb_count || p->imm_count) post_pass = p;
            continue;
        }
        draw_2d(f, p);
    }
    if (post) {
        soft_post(post, post_pass);
        for (int pi = 0; pi < f->pass_count; pi++) {
            const arc__pass *p = &f->passes[pi];
            if (p->target || f->targets_only) continue;
            S.cw = S.w; S.ch = S.h; S.color = S.screen; S.depth = S.screen_depth;
            draw_2d(f, p);
        }
    }
}
static bool soft_read(uint8_t *rgba, int w, int h) {
    if (!S.screen || w != S.w || h != S.h) return false;
    memcpy(rgba, S.screen, (size_t)w * h * 4); return true;
}
const arc__backend arc__backend_soft = { "soft", soft_init, soft_shutdown, soft_mesh_create, soft_mesh_update, soft_mesh_destroy, soft_tex_create,
    soft_target_create, soft_tex_destroy, soft_tex_set_smooth, soft_tex_update, soft_render, soft_read, NULL, NULL, NULL, NULL };   /* compressed: decoded on the CPU; arrays: the layers sampled */
