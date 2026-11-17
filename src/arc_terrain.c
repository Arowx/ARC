/* ARC terrain (0.9.6): height grids drawn as chunks with LOD and skirts, splatted with up to 4 layers (arc_material
 * splat, see ARC__MAT3_*), a CPU splat map painted or filled from rules, and scatter (instanced props placed by layer,
 * height and slope from a seed). Part of the unity build: internal names start with ter_ / sct_. */
#include "arc_internal.h"

#define TER_MAX 8
#define SCT_MAX 32
typedef struct { arc_mesh lv[3]; int nlv; int x0, z0, x1, z1; float ylo, yhi; } ter_chunk;   /* sample range x0..x1, z0..z1 (inclusive); its heights (0.9.7 culling) */
typedef struct {
    bool used; int sx, sz; float size_x, size_z, dx, dz; arc_v3 org;
    float *h; arc_v3 *nrm; float hmin, hmax;
    int chunk, ncx, ncz; ter_chunk *ch; float skirt;
    int sres; uint8_t *splat; arc_texture splat_tex, layers; int nlayers;
    int drawn; bool no_cull;                  /* 0.9.7 chunks the last draw drew; culling off */
} ter_rec;
static ter_rec ter_pool[TER_MAX];
typedef struct {
    bool used; uint32_t ter; arc_scatter_desc d;
    int n; arc_m4 *m; uint32_t *tint; int *cstart;   /* copies grouped by chunk: chunk k holds cstart[k] .. cstart[k + 1] - 1 */
    arc_v3 *clo, *chi;                                  /* per chunk: the box round its copies (mesh size included) */
    int drawn;
} sct_rec;
static sct_rec sct_pool[SCT_MAX];

static ter_rec *ter_get(arc_terrain t) { return t.id && t.id <= TER_MAX && ter_pool[t.id - 1].used ? &ter_pool[t.id - 1] : NULL; }
static sct_rec *sct_get(arc_scatter s) { return s.id && s.id <= SCT_MAX && sct_pool[s.id - 1].used ? &sct_pool[s.id - 1] : NULL; }
static float ter_hs(const ter_rec *T, int i, int j) { i = arc_clampi(i, 0, T->sx - 1); j = arc_clampi(j, 0, T->sz - 1); return T->h[(size_t)j * T->sx + i]; }
static arc_v3 ter_pos(const ter_rec *T, int i, int j) { return arc_v3_make(T->org.x + (float)i * T->dx, T->org.y + ter_hs(T, i, j), T->org.z + (float)j * T->dz); }

static void ter_normals(ter_rec *T, int i0, int j0, int i1, int j1) {   /* central differences (one-sided at the edges) */
    i0 = arc_maxi(i0, 0); j0 = arc_maxi(j0, 0); i1 = arc_mini(i1, T->sx - 1); j1 = arc_mini(j1, T->sz - 1);
    for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        int a = arc_maxi(i - 1, 0), b = arc_mini(i + 1, T->sx - 1), c = arc_maxi(j - 1, 0), d = arc_mini(j + 1, T->sz - 1);
        float gx = (ter_hs(T, b, j) - ter_hs(T, a, j)) / ((float)(b - a) * T->dx), gz = (ter_hs(T, i, d) - ter_hs(T, i, c)) / ((float)(d - c) * T->dz);
        T->nrm[(size_t)j * T->sx + i] = arc_v3_norm(arc_v3_make(-gx, 1, -gz));
    }
}
static void ter_range(ter_rec *T) {
    T->hmin = 1e30f; T->hmax = -1e30f;
    for (size_t k = 0, n = (size_t)T->sx * T->sz; k < n; k++) { T->hmin = arc_minf(T->hmin, T->h[k]); T->hmax = arc_maxf(T->hmax, T->h[k]); }
}

/* ---- chunk meshes: a grid at step 1 << level (always reaching the chunk's far edges) and a skirt down each edge ---- */
static int ter_steps(int a, int b, int step, int *out) {   /* sample indices a, a + step, ..., b */
    int n = 0;
    for (int i = a; i < b; i += step) { if (out) out[n] = i; n++; }
    if (out) out[n] = b;
    return n + 1;
}
static void ter_vert(const ter_rec *T, arc_vertex *v, int i, int j, float down) {
    arc_v3 p = ter_pos(T, i, j), n = T->nrm[(size_t)j * T->sx + i];
    v->px = p.x; v->py = p.y - down; v->pz = p.z; v->nx = n.x; v->ny = n.y; v->nz = n.z; v->color = 0xFFFFFFFFu;
    v->u = (float)i / (float)(T->sx - 1); v->v = (float)j / (float)(T->sz - 1);
}
/* vertices of a chunk at a level; with idx (and *ic) the triangles too (the same every time for a chunk and level) */
static int ter_build(const ter_rec *T, const ter_chunk *C, int level, arc_vertex *v, uint32_t *idx, int *ic) {
    int xs[4200], zs[4200], step = 1 << level;
    int nx = ter_steps(C->x0, C->x1, step, xs), nz = ter_steps(C->z0, C->z1, step, zs), nv = 0, ni = 0;
    if (v) for (int b = 0; b < nz; b++) for (int a = 0; a < nx; a++) ter_vert(T, &v[b * nx + a], xs[a], zs[b], 0);
    nv = nx * nz;
    if (idx) for (int b = 0; b + 1 < nz; b++) for (int a = 0; a + 1 < nx; a++) {   /* split along (a, b)-(a + 1, b + 1), facing up */
        uint32_t p00 = (uint32_t)(b * nx + a), p10 = p00 + 1, p01 = p00 + (uint32_t)nx, p11 = p01 + 1;
        idx[ni++] = p00; idx[ni++] = p11; idx[ni++] = p10; idx[ni++] = p00; idx[ni++] = p01; idx[ni++] = p11;
    }
    /* skirts: the edge rows again, lowered, joined to them facing out (south z0, north z1, west x0, east x1) */
    for (int e = 0; e < 4; e++) {
        int cnt = e < 2 ? nx : nz, base = nv;
        for (int k = 0; k < cnt; k++) {
            int i = e < 2 ? xs[k] : (e == 2 ? C->x0 : C->x1), j = e < 2 ? (e == 0 ? C->z0 : C->z1) : zs[k];
            if (v) ter_vert(T, &v[nv], i, j, T->skirt);
            nv++;
        }
        if (idx) for (int k = 0; k + 1 < cnt; k++) {
            uint32_t t0 = e == 0 ? (uint32_t)k : e == 1 ? (uint32_t)((nz - 1) * nx + k) : e == 2 ? (uint32_t)(k * nx) : (uint32_t)(k * nx + nx - 1);
            uint32_t t1 = e == 0 ? t0 + 1 : e == 1 ? t0 + 1 : t0 + (uint32_t)nx, s0 = (uint32_t)(base + k), s1 = s0 + 1;
            bool flip = e == 0 || e == 3;   /* outward: -z, +z, -x, +x */
            if (flip) { idx[ni++] = t0; idx[ni++] = t1; idx[ni++] = s1; idx[ni++] = t0; idx[ni++] = s1; idx[ni++] = s0; }
            else { idx[ni++] = t0; idx[ni++] = s1; idx[ni++] = t1; idx[ni++] = t0; idx[ni++] = s0; idx[ni++] = s1; }
        }
    }
    if (ic) *ic = ni;
    return nv;
}
static void ter_chunk_range(const ter_rec *T, ter_chunk *C) {   /* the chunk's lowest and highest ground */
    C->ylo = 1e30f; C->yhi = -1e30f;
    for (int j = C->z0; j <= C->z1; j++) for (int i = C->x0; i <= C->x1; i++) { float h = T->org.y + ter_hs(T, i, j); C->ylo = arc_minf(C->ylo, h); C->yhi = arc_maxf(C->yhi, h); }
}
static void ter_chunk_upload(ter_rec *T, ter_chunk *C, bool create) {
    ter_chunk_range(T, C);
    for (int l = 0; l < C->nlv; l++) {
        int nv = ter_build(T, C, l, NULL, NULL, NULL), ni = 0;
        arc_vertex *v = (arc_vertex *)arc__xalloc(sizeof(arc_vertex) * (size_t)nv);
        uint32_t *idx = create ? (uint32_t *)arc__xalloc(sizeof(uint32_t) * (size_t)(nv * 6 + 64)) : NULL;
        ter_build(T, C, l, v, idx, &ni);
        if (create) { C->lv[l] = arc_mesh_create(v, nv, idx, ni); arc_mesh_set_name(C->lv[l], "terrain"); }
        else arc_mesh_update(C->lv[l], v, nv);
        free(v); free(idx);
    }
}
static void ter_update_area(ter_rec *T, int i0, int j0, int i1, int j1) {
    ter_normals(T, i0 - 1, j0 - 1, i1 + 1, j1 + 1); ter_range(T);
    for (int k = 0; k < T->ncx * T->ncz; k++) {
        ter_chunk *C = &T->ch[k];
        if (C->x1 < i0 - 2 || C->x0 > i1 + 2 || C->z1 < j0 - 2 || C->z0 > j1 + 2) continue;
        ter_chunk_upload(T, C, false);
    }
}

/* ---- the splat map ---- */
static void ter_splat_get(const ter_rec *T, int px, int py, float w[4]) {   /* normalised weights of one pixel */
    const uint8_t *q = T->splat + ((size_t)py * T->sres + px) * 4; float s = 0;
    for (int c = 0; c < 4; c++) { w[c] = c < T->nlayers ? q[c] / 255.0f : 0; s += w[c]; }
    if (s < 1e-4f) { w[0] = 1; w[1] = w[2] = w[3] = 0; } else for (int c = 0; c < 4; c++) w[c] /= s;
}
static void ter_splat_put(ter_rec *T, int px, int py, const float w[4]) {
    uint8_t *q = T->splat + ((size_t)py * T->sres + px) * 4;
    for (int c = 0; c < 4; c++) q[c] = c < T->nlayers ? (uint8_t)(arc_clampf(w[c], 0, 1) * 255.0f + 0.5f) : 0;
    if (T->nlayers < 4) q[3] = 255;   /* unused alpha stays opaque */
}
static void ter_splat_world(const ter_rec *T, int px, int py, float *x, float *z) {   /* a pixel's centre in the world */
    *x = T->org.x + ((float)px + 0.5f) / (float)T->sres * T->size_x; *z = T->org.z + ((float)py + 0.5f) / (float)T->sres * T->size_z;
}
static void ter_splat_upload(ter_rec *T) { arc_texture_update(T->splat_tex, T->splat); }

/* ======================================================================== */
arc_terrain arc_terrain_create(const arc_terrain_desc *d) {
    arc_terrain none = { 0 };
    if (!d || d->samples_x < 2 || d->samples_z < 2 || d->samples_x > 4097 || d->samples_z > 4097 || !(d->size_x > 0) || !(d->size_z > 0)) { arc_log("arc_terrain_create: samples 2..4097 a side and sizes above 0"); return none; }
    if (d->layer_count < 0 || d->layer_count > 4) { arc_log("arc_terrain_create: layer_count 0..4"); return none; }
    int slot = -1; for (int i = 0; i < TER_MAX; i++) if (!ter_pool[i].used) { slot = i; break; }
    if (slot < 0) { arc_log("arc_terrain_create: more than %d terrains", TER_MAX); return none; }
    ter_rec *T = &ter_pool[slot]; memset(T, 0, sizeof *T);
    T->sx = d->samples_x; T->sz = d->samples_z; T->size_x = d->size_x; T->size_z = d->size_z; T->org = d->origin;
    T->dx = d->size_x / (float)(T->sx - 1); T->dz = d->size_z / (float)(T->sz - 1);
    size_t ns = (size_t)T->sx * T->sz;
    T->h = (float *)arc__xalloc(sizeof(float) * ns); T->nrm = (arc_v3 *)arc__xalloc(sizeof(arc_v3) * ns);
    if (d->heights) memcpy(T->h, d->heights, sizeof(float) * ns); else memset(T->h, 0, sizeof(float) * ns);
    ter_range(T); ter_normals(T, 0, 0, T->sx - 1, T->sz - 1);
    /* the splat map: all layer 0 */
    T->nlayers = d->layer_count > 0 ? d->layer_count : 1;
    T->sres = d->splat_res > 0 ? arc_clampi(d->splat_res, 16, 4096) : 256;
    T->splat = (uint8_t *)arc__xalloc((size_t)T->sres * T->sres * 4);
    float w0[4] = { 1, 0, 0, 0 };
    for (int y = 0; y < T->sres; y++) for (int x = 0; x < T->sres; x++) ter_splat_put(T, x, y, w0);
    T->splat_tex = arc_texture_create_ex(T->sres, T->sres, T->splat, ARC_TEX_SMOOTH | ARC_TEX_MIPMAPS);
    if (d->layer_count > 0) T->layers = arc_texture_array(d->layers, d->layer_count);
    else { uint8_t white[16 * 4]; memset(white, 255, sizeof white); arc_texture w = arc_texture_create_ex(4, 4, white, ARC_TEX_MESH); T->layers = arc_texture_array(&w, 1); arc_texture_destroy(w); }
    if (!T->splat_tex.id || !T->layers.id) { arc_log("arc_terrain_create: its textures failed"); arc_terrain t = { (uint32_t)slot + 1 }; arc_terrain_destroy(t); return none; }
    arc_material mat; if (d->material) mat = *d->material; else memset(&mat, 0, sizeof mat);
    mat.layers = T->layers; mat.splat_sharpness = d->splat_sharpness;
    for (int i = 0; i < 4; i++) mat.layer_scale[i] = d->layer_scale[i] > 0 ? d->layer_scale[i] : 0.25f;
    /* chunks */
    T->chunk = d->chunk > 0 ? arc_clampi(d->chunk, 4, 256) : 32;
    T->ncx = (T->sx - 2) / T->chunk + 1; T->ncz = (T->sz - 2) / T->chunk + 1;
    T->ch = (ter_chunk *)calloc((size_t)T->ncx * T->ncz, sizeof(ter_chunk));
    float cw = (float)T->chunk * arc_maxf(T->dx, T->dz);
    T->skirt = arc_maxf(cw * 0.08f, (T->hmax - T->hmin) * 0.02f + 0.05f);
    int nlv = d->lod_distance < 0 ? 1 : (T->chunk >= 16 ? 3 : T->chunk >= 8 ? 2 : 1);
    float ld = d->lod_distance > 0 ? d->lod_distance : 4 * cw;
    for (int cz = 0; cz < T->ncz; cz++) for (int cx = 0; cx < T->ncx; cx++) {
        ter_chunk *C = &T->ch[cz * T->ncx + cx];
        C->x0 = cx * T->chunk; C->x1 = arc_mini(C->x0 + T->chunk, T->sx - 1); C->z0 = cz * T->chunk; C->z1 = arc_mini(C->z0 + T->chunk, T->sz - 1);
        C->nlv = nlv;
        ter_chunk_upload(T, C, true);
        arc_mesh_set_texture(C->lv[0], T->splat_tex); arc_mesh_set_material(C->lv[0], &mat);
        for (int l = 1; l < nlv; l++) { arc_mesh_set_texture(C->lv[l], T->splat_tex); arc_mesh_set_material(C->lv[l], &mat); arc_mesh_set_lod_distance(C->lv[0], l, C->lv[l], ld * (float)(1 << (l - 1))); }
    }
    T->used = true;
    arc_terrain t = { (uint32_t)slot + 1 }; return t;
}
void arc_terrain_destroy(arc_terrain t) {
    ter_rec *T = t.id && t.id <= TER_MAX ? &ter_pool[t.id - 1] : NULL;
    if (!T || (!T->used && !T->h)) return;
    if (T->ch) for (int k = 0; k < T->ncx * T->ncz; k++) for (int l = 0; l < T->ch[k].nlv; l++) arc_mesh_destroy(T->ch[k].lv[l]);
    if (T->splat_tex.id) arc_texture_destroy(T->splat_tex);
    if (T->layers.id) arc_texture_destroy(T->layers);
    free(T->h); free(T->nrm); free(T->ch); free(T->splat);
    memset(T, 0, sizeof *T);
}
/* 0.9.7 chunks out of view are skipped. With sun shadows each chunk's box is first stretched away from the sun by the
   longest shadow the terrain's height can throw (a hill just off screen still shades the view); with a reflection
   plane nothing is skipped (the mirrored view sees other chunks) */
void arc_terrain_draw(arc_terrain t, uint32_t tint) {
    ter_rec *T = ter_get(t); if (!T) return;
    arc_v3 sun; bool shadows, mirror; arc__cull_info(&sun, &shadows, &mirror);
    bool cull = !T->no_cull && !mirror;
    float pl[6][4]; arc_m4 vp = arc_view_proj(); const float *v = vp.m;
    for (int p = 0; p < 6; p++) { int r = p >> 1; float sg = (p & 1) ? -1.0f : 1.0f; for (int c = 0; c < 4; c++) pl[p][c] = v[c * 4 + 3] + sg * v[c * 4 + r]; }
    float S = shadows ? arc_minf((T->hmax - T->hmin + T->skirt) / arc_maxf(sun.y, 0.15f), arc_maxf(T->size_x, T->size_z)) : 0;
    T->drawn = 0;
    for (int k = 0; k < T->ncx * T->ncz; k++) {
        const ter_chunk *C = &T->ch[k];
        if (cull) {
            arc_v3 lo = arc_v3_make(T->org.x + (float)C->x0 * T->dx, C->ylo - T->skirt, T->org.z + (float)C->z0 * T->dz);
            arc_v3 hi = arc_v3_make(T->org.x + (float)C->x1 * T->dx, C->yhi, T->org.z + (float)C->z1 * T->dz);
            if (S > 0) {   /* and where its shadow can fall */
                arc_v3 o = arc_v3_scale(sun, -S);
                lo = arc_v3_make(arc_minf(lo.x, lo.x + o.x), arc_minf(lo.y, lo.y + o.y), arc_minf(lo.z, lo.z + o.z));
                hi = arc_v3_make(arc_maxf(hi.x, hi.x + o.x), arc_maxf(hi.y, hi.y + o.y), arc_maxf(hi.z, hi.z + o.z));
            }
            bool out = false;
            for (int p = 0; p < 6 && !out; p++) {
                float x = pl[p][0] >= 0 ? hi.x : lo.x, y = pl[p][1] >= 0 ? hi.y : lo.y, z = pl[p][2] >= 0 ? hi.z : lo.z;
                if (pl[p][0] * x + pl[p][1] * y + pl[p][2] * z + pl[p][3] < 0) out = true;
            }
            if (out) continue;
        }
        arc_draw_mesh(C->lv[0], arc_m4_identity(), tint);   /* LODs picked per chunk */
        T->drawn++;
    }
}
int arc_terrain_drawn(arc_terrain t) { ter_rec *T = ter_get(t); return T ? T->drawn : 0; }
int arc_terrain_chunk_count(arc_terrain t) { ter_rec *T = ter_get(t); return T ? T->ncx * T->ncz : 0; }
void arc_terrain_set_culling(arc_terrain t, bool on) { ter_rec *T = ter_get(t); if (T) T->no_cull = !on; }
bool arc_terrain_inside(arc_terrain t, float x, float z) {
    ter_rec *T = ter_get(t); if (!T) return false;
    return x >= T->org.x && x <= T->org.x + T->size_x && z >= T->org.z && z <= T->org.z + T->size_z;
}
static float ter_height(const ter_rec *T, float x, float z) {   /* on the full-detail triangles (see ter_build) */
    float gx = arc_clampf((x - T->org.x) / T->dx, 0, (float)(T->sx - 1)), gz = arc_clampf((z - T->org.z) / T->dz, 0, (float)(T->sz - 1));
    int i = arc_mini((int)gx, T->sx - 2), j = arc_mini((int)gz, T->sz - 2); float fx = gx - (float)i, fz = gz - (float)j;
    float h00 = ter_hs(T, i, j), h10 = ter_hs(T, i + 1, j), h01 = ter_hs(T, i, j + 1), h11 = ter_hs(T, i + 1, j + 1);
    float h = fx >= fz ? h00 + (h10 - h00) * fx + (h11 - h10) * fz : h00 + (h11 - h01) * fx + (h01 - h00) * fz;
    return T->org.y + h;
}
float arc_terrain_height(arc_terrain t, float x, float z) { ter_rec *T = ter_get(t); return T ? ter_height(T, x, z) : 0; }
static arc_v3 ter_normal(const ter_rec *T, float x, float z) {   /* the sample normals blended bilinearly */
    float gx = arc_clampf((x - T->org.x) / T->dx, 0, (float)(T->sx - 1)), gz = arc_clampf((z - T->org.z) / T->dz, 0, (float)(T->sz - 1));
    int i = arc_mini((int)gx, T->sx - 2), j = arc_mini((int)gz, T->sz - 2); float fx = gx - (float)i, fz = gz - (float)j;
    const arc_v3 *N = T->nrm; size_t s = (size_t)T->sx, a = (size_t)j * s + (size_t)i;
    arc_v3 n = arc_v3_add(arc_v3_add(arc_v3_scale(N[a], (1 - fx) * (1 - fz)), arc_v3_scale(N[a + 1], fx * (1 - fz))),
                          arc_v3_add(arc_v3_scale(N[a + s], (1 - fx) * fz), arc_v3_scale(N[a + s + 1], fx * fz)));
    return arc_v3_norm(n);
}
arc_v3 arc_terrain_normal(arc_terrain t, float x, float z) { ter_rec *T = ter_get(t); return T ? ter_normal(T, x, z) : arc_v3_make(0, 1, 0); }
bool arc_terrain_raycast(arc_terrain t, arc_v3 o, arc_v3 dir, float max_dist, float *dist, arc_v3 *point) {
    ter_rec *T = ter_get(t); float L = arc_v3_len(dir);
    if (!T || L < 1e-12f || !(max_dist > 0)) return false;
    arc_v3 d = arc_v3_scale(dir, 1.0f / L);
    /* clip to the terrain's box */
    float lo[3] = { T->org.x, T->org.y + T->hmin - 1e-3f, T->org.z }, hi[3] = { T->org.x + T->size_x, T->org.y + T->hmax + 1e-3f, T->org.z + T->size_z };
    float oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z }, t0 = 0, t1 = max_dist;
    for (int a = 0; a < 3; a++) {
        if (fabsf(dd[a]) < 1e-12f) { if (oo[a] < lo[a] || oo[a] > hi[a]) return false; continue; }
        float ta = (lo[a] - oo[a]) / dd[a], tb = (hi[a] - oo[a]) / dd[a]; if (ta > tb) { float q = ta; ta = tb; tb = q; }
        t0 = arc_maxf(t0, ta); t1 = arc_minf(t1, tb); if (t0 > t1) return false;
    }
    float hl = sqrtf(d.x * d.x + d.z * d.z), step = hl > 1e-6f ? 0.5f * arc_minf(T->dx, T->dz) / hl : t1 - t0;
    step = arc_maxf(step, 1e-4f);
    float ta = t0; arc_v3 p = arc_v3_madd(o, d, ta); float fa = p.y - ter_height(T, p.x, p.z);
    if (fa <= 0) { if (dist) *dist = ta; if (point) *point = arc_v3_make(p.x, ter_height(T, p.x, p.z), p.z); return true; }
    while (ta < t1) {
        float tb = arc_minf(ta + step, t1); arc_v3 q = arc_v3_madd(o, d, tb); float fb = q.y - ter_height(T, q.x, q.z);
        if (fb <= 0) {   /* crossed: bisect */
            for (int it = 0; it < 24; it++) { float tm = 0.5f * (ta + tb); arc_v3 m = arc_v3_madd(o, d, tm); float fm = m.y - ter_height(T, m.x, m.z); if (fm > 0) ta = tm; else tb = tm; }
            arc_v3 hp = arc_v3_madd(o, d, tb);
            if (dist) *dist = tb;
            if (point) *point = hp;
            return true;
        }
        ta = tb;
    }
    return false;
}
static float ter_fall(float d, float r) { if (d >= r) return 0; float s = d / r; return 1.0f - s * s * (3 - 2 * s); }
static void ter_brush(ter_rec *T, float x, float z, float radius, float amount, bool flatten, float target, float strength) {
    if (!(radius > 0)) return;
    int i0 = (int)floorf((x - radius - T->org.x) / T->dx), i1 = (int)ceilf((x + radius - T->org.x) / T->dx);
    int j0 = (int)floorf((z - radius - T->org.z) / T->dz), j1 = (int)ceilf((z + radius - T->org.z) / T->dz);
    i0 = arc_maxi(i0, 0); j0 = arc_maxi(j0, 0); i1 = arc_mini(i1, T->sx - 1); j1 = arc_mini(j1, T->sz - 1);
    if (i0 > i1 || j0 > j1) return;
    for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        float px = T->org.x + (float)i * T->dx, pz = T->org.z + (float)j * T->dz, f = ter_fall(sqrtf((px - x) * (px - x) + (pz - z) * (pz - z)), radius);
        if (f <= 0) continue;
        float *h = &T->h[(size_t)j * T->sx + i];
        if (flatten) *h += (target - T->org.y - *h) * arc_clampf(strength, 0, 1) * f; else *h += amount * f;
    }
    ter_update_area(T, i0, j0, i1, j1);
}
void arc_terrain_sculpt(arc_terrain t, float x, float z, float radius, float amount) { ter_rec *T = ter_get(t); if (T) ter_brush(T, x, z, radius, amount, false, 0, 0); }
void arc_terrain_flatten(arc_terrain t, float x, float z, float radius, float height, float strength) { ter_rec *T = ter_get(t); if (T) ter_brush(T, x, z, radius, 0, true, height, strength); }
void arc_terrain_set_heights(arc_terrain t, const float *heights) {
    ter_rec *T = ter_get(t); if (!T || !heights) return;
    memcpy(T->h, heights, sizeof(float) * (size_t)T->sx * T->sz);
    ter_update_area(T, 0, 0, T->sx - 1, T->sz - 1);
}
void arc_terrain_paint(arc_terrain t, float x, float z, float radius, int layer, float strength) {
    ter_rec *T = ter_get(t); if (!T || layer < 0 || layer >= T->nlayers || !(radius > 0)) return;
    float ppx = (float)T->sres / T->size_x, ppz = (float)T->sres / T->size_z;
    int x0 = arc_maxi((int)floorf((x - radius - T->org.x) * ppx), 0), x1 = arc_mini((int)ceilf((x + radius - T->org.x) * ppx), T->sres - 1);
    int y0 = arc_maxi((int)floorf((z - radius - T->org.z) * ppz), 0), y1 = arc_mini((int)ceilf((z + radius - T->org.z) * ppz), T->sres - 1);
    bool any = false;
    for (int py = y0; py <= y1; py++) for (int px = x0; px <= x1; px++) {
        float wx, wz; ter_splat_world(T, px, py, &wx, &wz);
        float k = arc_clampf(strength, 0, 1) * ter_fall(sqrtf((wx - x) * (wx - x) + (wz - z) * (wz - z)), radius);
        if (k <= 0) continue;
        float w[4]; ter_splat_get(T, px, py, w);
        for (int c = 0; c < 4; c++) w[c] *= 1 - k;
        w[layer] += k; ter_splat_put(T, px, py, w); any = true;
    }
    if (any) ter_splat_upload(T);
}
static float ter_band(float v, float lo, float hi, float b) {   /* 1 inside lo..hi, ramps of width b centred on the edges */
    return arc_clampf((v - (lo - b * 0.5f)) / b, 0, 1) * arc_clampf(((hi + b * 0.5f) - v) / b, 0, 1);
}
void arc_terrain_splat_rules(arc_terrain t, const arc_splat_rule *rules, int n) {
    ter_rec *T = ter_get(t); if (!T || (n > 0 && !rules)) return;
    for (int py = 0; py < T->sres; py++) for (int px = 0; px < T->sres; px++) {
        float x, z; ter_splat_world(T, px, py, &x, &z);
        float h = ter_height(T, x, z); arc_v3 nn = ter_normal(T, x, z);
        float slope = arc_acosf(arc_clampf(nn.y, -1, 1)) * (180.0f / ARC_PI), w[4] = { 1, 0, 0, 0 };   /* (0.9.7: arc_dmath, the same everywhere) */
        for (int r = 0; r < n; r++) {
            const arc_splat_rule *q = &rules[r];
            if (q->layer < 0 || q->layer >= T->nlayers) continue;
            float b = q->blend > 0 ? q->blend : 2.0f, k = q->amount > 0 ? arc_clampf(q->amount, 0, 1) : 1.0f;
            if (q->height_min != 0 || q->height_max != 0) k *= ter_band(h, q->height_min, q->height_max, b);
            if (q->slope_min != 0 || q->slope_max != 0) k *= ter_band(slope, q->slope_min, q->slope_max, b);
            if (k <= 0) continue;
            for (int c = 0; c < 4; c++) w[c] *= 1 - k;
            w[q->layer] += k;
        }
        ter_splat_put(T, px, py, w);
    }
    ter_splat_upload(T);
}
static float ter_weight(const ter_rec *T, float x, float z, int layer) {   /* bilinear like the GPU's sampling */
    float u = (x - T->org.x) / T->size_x * (float)T->sres - 0.5f, v = (z - T->org.z) / T->size_z * (float)T->sres - 0.5f;
    int i = (int)floorf(u), j = (int)floorf(v); float fu = u - (float)i, fv = v - (float)j, r = 0;
    for (int k = 0; k < 4; k++) {
        int a = arc_clampi(i + (k & 1), 0, T->sres - 1), b = arc_clampi(j + (k >> 1), 0, T->sres - 1); float w[4]; ter_splat_get(T, a, b, w);
        r += w[layer] * ((k & 1) ? fu : 1 - fu) * ((k >> 1) ? fv : 1 - fv);
    }
    return r;
}
float arc_terrain_layer_weight(arc_terrain t, float x, float z, int layer) {
    ter_rec *T = ter_get(t); if (!T || layer < 0 || layer >= T->nlayers) return 0;
    return ter_weight(T, x, z, layer);
}
int arc_terrain_triangles(arc_terrain t, arc_v3 *out, int max) {
    ter_rec *T = ter_get(t); if (!T) return 0;
    long long total = (long long)(T->sx - 1) * (T->sz - 1) * 6;
    if (total > 0x7FFFFFFF) total = 0x7FFFFFFF;
    if (!out) return (int)total;
    int n = 0;
    for (int j = 0; j + 1 < T->sz; j++) for (int i = 0; i + 1 < T->sx; i++) {
        arc_v3 p00 = ter_pos(T, i, j), p10 = ter_pos(T, i + 1, j), p01 = ter_pos(T, i, j + 1), p11 = ter_pos(T, i + 1, j + 1);
        arc_v3 q[6] = { p00, p11, p10, p00, p01, p11 };
        for (int k = 0; k < 6; k++) { if (n >= max) return (int)total; out[n++] = q[k]; }
    }
    return (int)total;
}
arc_texture arc_terrain_splat_map(arc_terrain t) { ter_rec *T = ter_get(t); arc_texture none = { 0 }; return T ? T->splat_tex : none; }

/* ======================================================================== */
/* Scatter: one candidate per cell of a grid 1 / sqrt(density) apart, jittered; every number a copy uses comes from a
   hash of (seed, cell, k), so a copy depends on nothing but its own cell (rebuilds keep untouched places) */
static uint32_t sct_hash(uint32_t a, uint32_t b, uint32_t c, uint32_t k) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du ^ k * 0x27D4EB2Fu;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15; return h;
}
static float sct_rnd(uint32_t seed, int ix, int iz, uint32_t k) { return (float)(sct_hash(seed, (uint32_t)ix, (uint32_t)iz, k) >> 8) / 16777216.0f; }
static void sct_build(sct_rec *S) {
    free(S->m); free(S->tint); free(S->cstart); free(S->clo); free(S->chi);
    S->m = NULL; S->tint = NULL; S->cstart = NULL; S->clo = S->chi = NULL; S->n = 0;
    ter_rec *T = &ter_pool[S->ter - 1]; if (!T->used) return;
    const arc_scatter_desc *d = &S->d;
    float cell = 1.0f / sqrtf(d->density);
    int nx = (int)ceilf(T->size_x / cell), nz = (int)ceilf(T->size_z / cell);
    if ((long long)nx * nz > 16000000LL) { arc_log("arc_scatter: density %.3g over this terrain is more than 16 million cells; lowered", d->density); cell = sqrtf(T->size_x * T->size_z / 16000000.0f); nx = (int)ceilf(T->size_x / cell); nz = (int)ceilf(T->size_z / cell); }
    float smin = d->scale_min > 0 ? d->scale_min : 1, smax = d->scale_max > 0 ? d->scale_max : smin, slope_max = d->slope_max > 0 ? d->slope_max : 35;
    float mw = d->min_weight > 0 ? d->min_weight : 0.5f, cosmax = arc_cosf(slope_max * ARC_PI / 180.0f);
    int nch = T->ncx * T->ncz, cap = 0, n = 0; arc_m4 *m = NULL; uint32_t *tn = NULL; int *ck = NULL;
    float c1[4], c2[4]; arc__unpack_rgb(d->tint ? d->tint : 0xFFFFFF, c1); arc__unpack_rgb(d->tint2 ? d->tint2 : (d->tint ? d->tint : 0xFFFFFF), c2);
    for (int iz = 0; iz < nz; iz++) for (int ix = 0; ix < nx; ix++) {
        float x = T->org.x + ((float)ix + sct_rnd(d->seed, ix, iz, 0)) * cell, z = T->org.z + ((float)iz + sct_rnd(d->seed, ix, iz, 1)) * cell;
        if (x > T->org.x + T->size_x || z > T->org.z + T->size_z) continue;
        arc_v3 nn = ter_normal(T, x, z); if (nn.y < cosmax) continue;
        float h = ter_height(T, x, z);
        if ((d->height_min != 0 || d->height_max != 0) && (h < d->height_min || h > d->height_max)) continue;
        if (d->layer >= 0) {
            if (d->layer >= T->nlayers) continue;
            float thr = arc_clampf(mw + (sct_rnd(d->seed, ix, iz, 2) - 0.5f) * 0.3f, 0.02f, 0.98f);
            if (ter_weight(T, x, z, d->layer) < thr) continue;
        }
        float s = smin + (smax - smin) * sct_rnd(d->seed, ix, iz, 3), yaw = d->fixed_yaw ? 0 : sct_rnd(d->seed, ix, iz, 4) * 2 * ARC_PI;
        arc_v3 up = arc_v3_norm(arc_v3_add(arc_v3_make(0, 1, 0), arc_v3_scale(arc_v3_sub(nn, arc_v3_make(0, 1, 0)), arc_clampf(d->align, 0, 1))));
        arc_v3 f0 = arc_v3_make(arc_sinf(yaw), 0, arc_cosf(yaw)), zf = arc_v3_norm(arc_v3_sub(f0, arc_v3_scale(up, arc_v3_dot(f0, up)))), xr = arc_v3_cross(up, zf);
        if (n == cap) { cap = cap ? cap * 2 : 1024; m = (arc_m4 *)realloc(m, sizeof(arc_m4) * (size_t)cap); tn = (uint32_t *)realloc(tn, sizeof(uint32_t) * (size_t)cap); ck = (int *)realloc(ck, sizeof(int) * (size_t)cap); }
        arc_m4 M = arc_m4_basis(arc_v3_make(x, h - d->sink * s, z), arc_v3_scale(xr, s), arc_v3_scale(up, s), arc_v3_scale(zf, s));
        float k = sct_rnd(d->seed, ix, iz, 5); float c[3]; for (int q = 0; q < 3; q++) c[q] = c1[q] + (c2[q] - c1[q]) * k;
        int cx = arc_clampi((int)((x - T->org.x) / T->dx) / T->chunk, 0, T->ncx - 1), cz = arc_clampi((int)((z - T->org.z) / T->dz) / T->chunk, 0, T->ncz - 1);
        m[n] = M; tn[n] = ((uint32_t)(c[0] * 255 + 0.5f) << 16) | ((uint32_t)(c[1] * 255 + 0.5f) << 8) | (uint32_t)(c[2] * 255 + 0.5f); ck[n] = cz * T->ncx + cx; n++;
    }
    /* group by chunk (counting sort keeps the cell order inside a chunk) */
    S->cstart = (int *)calloc((size_t)nch + 1, sizeof(int)); S->clo = (arc_v3 *)calloc((size_t)nch, sizeof(arc_v3)); S->chi = (arc_v3 *)calloc((size_t)nch, sizeof(arc_v3));
    for (int i = 0; i < n; i++) S->cstart[ck[i] + 1]++;
    for (int k = 0; k < nch; k++) S->cstart[k + 1] += S->cstart[k];
    S->m = (arc_m4 *)malloc(sizeof(arc_m4) * (size_t)(n ? n : 1)); S->tint = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)(n ? n : 1));
    int *fill = (int *)malloc(sizeof(int) * (size_t)nch); memcpy(fill, S->cstart, sizeof(int) * (size_t)nch);
    arc_v3 mlo, mhi; arc_mesh_bounds(d->mesh, &mlo, &mhi);
    float rad = arc_maxf(arc_v3_len(mlo), arc_v3_len(mhi)) * smax;
    for (int k = 0; k < nch; k++) { S->clo[k] = arc_v3_make(1e30f, 1e30f, 1e30f); S->chi[k] = arc_v3_make(-1e30f, -1e30f, -1e30f); }
    for (int i = 0; i < n; i++) {
        int at = fill[ck[i]]++; S->m[at] = m[i]; S->tint[at] = tn[i];
        arc_v3 p = arc_v3_make(m[i].m[12], m[i].m[13], m[i].m[14]); int k = ck[i];
        S->clo[k] = arc_v3_make(arc_minf(S->clo[k].x, p.x - rad), arc_minf(S->clo[k].y, p.y - rad), arc_minf(S->clo[k].z, p.z - rad));
        S->chi[k] = arc_v3_make(arc_maxf(S->chi[k].x, p.x + rad), arc_maxf(S->chi[k].y, p.y + rad), arc_maxf(S->chi[k].z, p.z + rad));
    }
    free(fill); free(m); free(tn); free(ck);
    S->n = n;
}
arc_scatter arc_scatter_create(arc_terrain t, const arc_scatter_desc *d) {
    arc_scatter none = { 0 };
    if (!ter_get(t) || !d || !d->mesh.id || !(d->density > 0)) { arc_log("arc_scatter_create: a terrain, a mesh and a density above 0"); return none; }
    int slot = -1; for (int i = 0; i < SCT_MAX; i++) if (!sct_pool[i].used) { slot = i; break; }
    if (slot < 0) { arc_log("arc_scatter_create: more than %d scatters", SCT_MAX); return none; }
    sct_rec *S = &sct_pool[slot]; memset(S, 0, sizeof *S);
    S->used = true; S->ter = t.id; S->d = *d;
    sct_build(S);
    arc_scatter s = { (uint32_t)slot + 1 }; return s;
}
void arc_scatter_destroy(arc_scatter s) {
    sct_rec *S = sct_get(s); if (!S) return;
    free(S->m); free(S->tint); free(S->cstart); free(S->clo); free(S->chi); memset(S, 0, sizeof *S);
}
void arc_scatter_rebuild(arc_scatter s) { sct_rec *S = sct_get(s); if (S) sct_build(S); }
int arc_scatter_count(arc_scatter s) { sct_rec *S = sct_get(s); return S ? S->n : 0; }
int arc_scatter_drawn(arc_scatter s) { sct_rec *S = sct_get(s); return S ? S->drawn : 0; }
arc_m4 arc_scatter_matrix(arc_scatter s, int i) { sct_rec *S = sct_get(s); return S && i >= 0 && i < S->n ? S->m[i] : arc_m4_identity(); }
void arc_scatter_draw(arc_scatter s) {
    sct_rec *S = sct_get(s); if (!S) return;
    S->drawn = 0;
    ter_rec *T = &ter_pool[S->ter - 1]; if (!T->used || !S->n) return;
    arc_v3 eye = arc__view_eye(); arc_m4 vp = arc_view_proj(); const float *v = vp.m;
    float dd = S->d.draw_distance > 0 ? S->d.draw_distance : 60, fade = dd * 0.85f;
    float pl[6][4];   /* frustum planes from the view-projection's rows */
    for (int p = 0; p < 6; p++) { int r = p >> 1; float sg = (p & 1) ? -1.0f : 1.0f; for (int c = 0; c < 4; c++) pl[p][c] = v[c * 4 + 3] + sg * v[c * 4 + r]; }
    for (int k = 0; k < T->ncx * T->ncz; k++) {
        int a = S->cstart[k], b = S->cstart[k + 1]; if (a == b) continue;
        arc_v3 lo = S->clo[k], hi = S->chi[k];
        arc_v3 q = arc_v3_make(arc_clampf(eye.x, lo.x, hi.x), arc_clampf(eye.y, lo.y, hi.y), arc_clampf(eye.z, lo.z, hi.z));
        if (arc_v3_dist(q, eye) > dd) continue;
        bool out = false;
        for (int p = 0; p < 6 && !out; p++) {   /* the box's corner furthest along the plane's normal */
            float x = pl[p][0] >= 0 ? hi.x : lo.x, y = pl[p][1] >= 0 ? hi.y : lo.y, z = pl[p][2] >= 0 ? hi.z : lo.z;
            if (pl[p][0] * x + pl[p][1] * y + pl[p][2] * z + pl[p][3] < 0) out = true;
        }
        if (out) continue;
        for (int i = a; i < b; i++) {
            const arc_m4 *M = &S->m[i]; arc_v3 p = arc_v3_make(M->m[12], M->m[13], M->m[14]); float dist = arc_v3_dist(p, eye);
            if (dist > dd) continue;
            if (dist > fade) {   /* shrink away over the last 15% */
                float f = (dd - dist) / (dd - fade); arc_m4 m2 = *M;
                for (int c = 0; c < 12; c++) m2.m[c] *= f;
                arc_draw_mesh(S->d.mesh, m2, S->tint[i]);
            } else arc_draw_mesh(S->d.mesh, *M, S->tint[i]);
            S->drawn++;
        }
    }
}
