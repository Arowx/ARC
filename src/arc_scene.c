/* ARC 0.7.1 scene speed-ups: view tests, cells and portals, levels of detail, static batches.
   Included right after arc_gfx.c in the unity build: it reads that file's R (the current camera), draw_mesh_full and
   its per-frame counters (g_culled, g_lod_n, g_static_draws, g_cam_version). Nothing here runs unless a game uses it. */

/* ======================================================================== */
/* View tests                                                               */
/* ======================================================================== */

/* the six clip planes of a view-projection (GL clip space: -w <= x, y, z <= w), normalised: a.x*p + d >= 0 inside */
static void frustum_planes(const float *m, float pl[6][4]) {
    float r[4][4];
    for (int i = 0; i < 4; i++) for (int c = 0; c < 4; c++) r[i][c] = m[c * 4 + i];   /* rows of the matrix */
    for (int k = 0; k < 3; k++)
        for (int s = 0; s < 2; s++) {
            float *p = pl[k * 2 + s], sg = s ? -1.0f : 1.0f;
            for (int c = 0; c < 4; c++) p[c] = r[3][c] + sg * r[k][c];
            float l = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
            if (l > 1e-20f) for (int c = 0; c < 4; c++) p[c] /= l;
        }
}
static bool planes_sphere(const float pl[6][4], arc_v3 c, float r) {
    for (int i = 0; i < 6; i++) if (pl[i][0] * c.x + pl[i][1] * c.y + pl[i][2] * c.z + pl[i][3] < -r) return false;
    return true;
}
static bool planes_aabb(const float pl[6][4], arc_v3 lo, arc_v3 hi) {
    for (int i = 0; i < 6; i++) {   /* the box corner furthest along the plane's normal */
        float x = pl[i][0] >= 0 ? hi.x : lo.x, y = pl[i][1] >= 0 ? hi.y : lo.y, z = pl[i][2] >= 0 ? hi.z : lo.z;
        if (pl[i][0] * x + pl[i][1] * y + pl[i][2] * z + pl[i][3] < 0) return false;
    }
    return true;
}
static float g_planes[6][4]; static uint32_t g_planes_ver;
static void planes_update(void) { if (g_planes_ver != g_cam_version) { frustum_planes(R.view_proj.m, g_planes); g_planes_ver = g_cam_version; } }
bool arc_sphere_visible(arc_v3 c, float r) { planes_update(); return planes_sphere(g_planes, c, r); }
bool arc_aabb_visible(arc_v3 lo, arc_v3 hi) { planes_update(); return planes_aabb(g_planes, lo, hi); }

/* ======================================================================== */
/* Cells and portals                                                        */
/* ======================================================================== */

#define ARC__MAX_CELL_BOXES 4096
#define ARC__MAX_PORTALS    2048
#define ARC__CELL_WORDS     (ARC_MAX_CELLS / 32)
#define ARC__PORTAL_DEPTH   32      /* portals deep a view may be followed */
#define ARC__PORTAL_STEPS   8192    /* portal crossings per visibility walk (bounded work on any graph) */
typedef struct { int cell; arc_v3 lo, hi; } cell_box;
typedef struct { int a, b; arc_v3 lo, hi; bool open; } cell_portal;
static struct {
    cell_box *box; int nbox;
    cell_portal *por; int npor;
    uint32_t exists[ARC__CELL_WORDS];           /* ids with at least one box */
    uint32_t version;                           /* bumped by every change */
    bool culling_off, debug;
    /* adjacency: portals of each cell, rebuilt when the version changes */
    int *adj_head, *adj_next, *adj_portal; uint32_t adj_ver;
    /* the current camera's answer */
    uint32_t vis[ARC__CELL_WORDS]; int vis_count, start; uint32_t cache_cam, cache_ver;
} CL;
static bool cell_id_ok(int id) { return id > 0 && id < ARC_MAX_CELLS; }
static bool bit_get(const uint32_t *b, int i) { return (b[i >> 5] >> (i & 31)) & 1u; }
static void bit_set(uint32_t *b, int i) { b[i >> 5] |= 1u << (i & 31); }
static arc_v3 v3_min(arc_v3 a, arc_v3 b) { return arc_v3_make(arc_minf(a.x, b.x), arc_minf(a.y, b.y), arc_minf(a.z, b.z)); }
static arc_v3 v3_max(arc_v3 a, arc_v3 b) { return arc_v3_make(arc_maxf(a.x, b.x), arc_maxf(a.y, b.y), arc_maxf(a.z, b.z)); }

void arc_cell_add(int id, arc_v3 lo, arc_v3 hi) {
    if (!cell_id_ok(id)) { arc_log("arc_cell_add: id %d is outside 1..%d", id, ARC_MAX_CELLS - 1); return; }
    if (!CL.box) CL.box = (cell_box *)arc__xalloc(sizeof(cell_box) * ARC__MAX_CELL_BOXES);
    if (CL.nbox >= ARC__MAX_CELL_BOXES) { arc_log("arc_cell_add: more than %d cell boxes", ARC__MAX_CELL_BOXES); return; }
    cell_box *b = &CL.box[CL.nbox++]; b->cell = id; b->lo = v3_min(lo, hi); b->hi = v3_max(lo, hi);
    bit_set(CL.exists, id); CL.version++;
}
int arc_portal_add(int a, int b, arc_v3 lo, arc_v3 hi) {
    if (!cell_id_ok(a) || !cell_id_ok(b) || a == b) { arc_log("arc_portal_add: cells %d and %d", a, b); return 0; }
    if (!CL.por) CL.por = (cell_portal *)arc__xalloc(sizeof(cell_portal) * ARC__MAX_PORTALS);
    if (CL.npor >= ARC__MAX_PORTALS) { arc_log("arc_portal_add: more than %d portals", ARC__MAX_PORTALS); return 0; }
    cell_portal *p = &CL.por[CL.npor++]; p->a = a; p->b = b; p->lo = v3_min(lo, hi); p->hi = v3_max(lo, hi); p->open = true;
    CL.version++;
    return CL.npor;
}
void arc_portal_set_open(int portal, bool open) {
    if (portal < 1 || portal > CL.npor || CL.por[portal - 1].open == open) return;
    CL.por[portal - 1].open = open; CL.version++;
}
bool arc_portal_is_open(int portal) { return portal >= 1 && portal <= CL.npor && CL.por[portal - 1].open; }
void arc_cells_clear(void) { CL.nbox = CL.npor = 0; memset(CL.exists, 0, sizeof CL.exists); CL.version++; R.stats.cells_visible = 0; }
static bool box_has(const cell_box *b, arc_v3 p) { return p.x >= b->lo.x && p.x <= b->hi.x && p.y >= b->lo.y && p.y <= b->hi.y && p.z >= b->lo.z && p.z <= b->hi.z; }
int arc_cell_at(arc_v3 p) {
    int best = 0; float bv = 0;
    for (int i = 0; i < CL.nbox; i++) {
        const cell_box *b = &CL.box[i];
        if (!box_has(b, p)) continue;
        float v = (b->hi.x - b->lo.x) * (b->hi.y - b->lo.y) * (b->hi.z - b->lo.z);
        if (!best || v < bv) { best = b->cell; bv = v; }   /* the smallest: rooms inside rooms */
    }
    return best;
}
static void adj_build(void) {
    if (CL.adj_ver == CL.version && CL.adj_head) return;
    if (!CL.adj_head) {
        CL.adj_head = (int *)arc__xalloc(sizeof(int) * ARC_MAX_CELLS);
        CL.adj_next = (int *)arc__xalloc(sizeof(int) * ARC__MAX_PORTALS * 2); CL.adj_portal = (int *)arc__xalloc(sizeof(int) * ARC__MAX_PORTALS * 2);
    }
    for (int i = 0; i < ARC_MAX_CELLS; i++) CL.adj_head[i] = -1;
    int n = 0;
    for (int i = CL.npor - 1; i >= 0; i--)   /* backwards, so each list runs in the order portals were added */
        for (int s = 1; s >= 0; s--) {
            int c = s ? CL.por[i].b : CL.por[i].a;
            CL.adj_portal[n] = i; CL.adj_next[n] = CL.adj_head[c]; CL.adj_head[c] = n; n++;
        }
    CL.adj_ver = CL.version;
}
/* a portal's rectangle on screen (normalised device x, y): its box's corners in front of the near plane, plus where its
   edges cross that plane; false if all of it is behind */
static bool portal_rect(const float *vp, arc_v3 lo, arc_v3 hi, float r[4]) {
    arc_m4 m; memcpy(m.m, vp, sizeof m.m);
    arc_v4 c[8]; float d[8];
    for (int i = 0; i < 8; i++) {
        arc_v4 p = { i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z, 1.0f };
        c[i] = arc_m4_mul_v4(m, p); d[i] = c[i].z + c[i].w;   /* >= 0: in front of the near plane */
    }
    r[0] = r[1] = 1e30f; r[2] = r[3] = -1e30f; int n = 0;
    #define ADDP(q) do { float iw = 1.0f / arc_maxf((q).w, 1e-9f), x = (q).x * iw, y = (q).y * iw; \
        r[0] = arc_minf(r[0], x); r[1] = arc_minf(r[1], y); r[2] = arc_maxf(r[2], x); r[3] = arc_maxf(r[3], y); n++; } while (0)
    for (int i = 0; i < 8; i++) if (d[i] >= 0) ADDP(c[i]);
    if (n == 8) return true;
    if (n == 0) return false;
    static const int E[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (int e = 0; e < 12; e++) {
        int i = E[e][0], j = E[e][1];
        if ((d[i] >= 0) == (d[j] >= 0)) continue;
        float t = d[i] / (d[i] - d[j]);
        arc_v4 q = { c[i].x + (c[j].x - c[i].x) * t, c[i].y + (c[j].y - c[i].y) * t, c[i].z + (c[j].z - c[i].z) * t, c[i].w + (c[j].w - c[i].w) * t };
        ADDP(q);
    }
    #undef ADDP
    return true;
}
/* which cells a view sees: from the eye's cell through open portals, the view narrowed to each portal's rectangle
   (classic portal culling); an eye outside every cell sees each cell with a box in view. Returns the count. */
static int cells_walk(const float *vp, arc_v3 eye, uint32_t *bits, int *start_out) {
    memset(bits, 0, sizeof(uint32_t) * ARC__CELL_WORDS);
    int start = arc_cell_at(eye), count = 0;
    if (start_out) *start_out = start;
    if (!start) {
        float pl[6][4]; frustum_planes(vp, pl);
        for (int i = 0; i < CL.nbox; i++) {
            const cell_box *b = &CL.box[i];
            if (!bit_get(bits, b->cell) && planes_aabb(pl, b->lo, b->hi)) { bit_set(bits, b->cell); count++; }
        }
        return count;
    }
    adj_build();
    /* depth-first over (cell, rectangle); a cell already entered with a rectangle holding this one adds nothing */
    typedef struct { int cell, via, depth; float r[4]; } walk;
    static walk stack[256];
    enum { SEEN = 4 };
    static float seen[ARC_MAX_CELLS][SEEN][4]; static uint8_t nseen[ARC_MAX_CELLS];
    memset(nseen, 0, sizeof nseen);
    int sp = 0, steps = 0;
    stack[sp++] = (walk){ start, -1, 0, { -1, -1, 1, 1 } };
    while (sp > 0) {
        walk w = stack[--sp];
        if (!bit_get(bits, w.cell)) { bit_set(bits, w.cell); count++; }
        bool covered = false;
        for (int k = 0; k < nseen[w.cell]; k++) {
            const float *s = seen[w.cell][k];
            if (w.r[0] >= s[0] && w.r[1] >= s[1] && w.r[2] <= s[2] && w.r[3] <= s[3]) { covered = true; break; }
        }
        if (covered) continue;
        { int k = nseen[w.cell] < SEEN ? nseen[w.cell]++ : SEEN - 1; memcpy(seen[w.cell][k], w.r, sizeof w.r); }
        if (w.depth >= ARC__PORTAL_DEPTH) continue;
        for (int e = CL.adj_head[w.cell]; e >= 0; e = CL.adj_next[e]) {
            int pi = CL.adj_portal[e]; const cell_portal *p = &CL.por[pi];
            if (!p->open || pi == w.via) continue;
            if (++steps > ARC__PORTAL_STEPS) return count;
            float r[4];
            if (!portal_rect(vp, p->lo, p->hi, r)) continue;
            r[0] = arc_maxf(r[0], w.r[0]); r[1] = arc_maxf(r[1], w.r[1]); r[2] = arc_minf(r[2], w.r[2]); r[3] = arc_minf(r[3], w.r[3]);
            if (r[0] >= r[2] || r[1] >= r[3]) continue;
            if (sp >= (int)(sizeof stack / sizeof stack[0])) continue;
            walk nw; nw.cell = p->a == w.cell ? p->b : p->a; nw.via = pi; nw.depth = w.depth + 1; memcpy(nw.r, r, sizeof r);
            stack[sp++] = nw;
        }
    }
    return count;
}
static void cells_current(void) {
    if (CL.cache_cam == g_cam_version && CL.cache_ver == CL.version) return;
    CL.vis_count = cells_walk(R.view_proj.m, R.eye, CL.vis, &CL.start);
    CL.cache_cam = g_cam_version; CL.cache_ver = CL.version;
    R.stats.cells_visible = CL.vis_count;
}
bool arc_cell_visible(int id) {
    if (!cell_id_ok(id) || !bit_get(CL.exists, id)) return true;   /* no such cell: nothing to hide it */
    cells_current();
    return bit_get(CL.vis, id);
}
int arc_cells_visible_count(void) { if (!CL.nbox) return 0; cells_current(); return CL.vis_count; }
int arc_cells_compute(const arc_camera *c, float vw, float vh, uint32_t *bits) {
    if (!c || !bits) return 0;
    arc_m4 vp = arc_camera_view_proj(c, vw, vh);
    return cells_walk(vp.m, c->eye, bits, NULL);
}
static bool scene_cell_drawable(int cell) { return CL.culling_off || arc_cell_visible(cell); }
void arc_set_draw_cell(int id) { arc__draw_cell = cell_id_ok(id) ? id : 0; }
int  arc_get_draw_cell(void) { return arc__draw_cell; }
void arc_cells_set_culling(bool on) { CL.culling_off = !on; }
void arc_cells_debug_draw(void) {
    if (!CL.nbox) return;
    cells_current();
    for (int i = 0; i < CL.nbox; i++) {
        const cell_box *b = &CL.box[i]; arc_v3 in = arc_v3_make(0.02f, 0.02f, 0.02f);   /* inset: neighbours' walls stay apart */
        arc_draw_box_lines(arc_v3_add(b->lo, in), arc_v3_sub(b->hi, in), bit_get(CL.vis, b->cell) ? 0x40FF60 : 0x606060);
    }
    for (int i = 0; i < CL.npor; i++) arc_draw_box_lines(CL.por[i].lo, CL.por[i].hi, CL.por[i].open ? 0xFFE040 : 0xFF3030);
}
/* the engine's overlay (`cells draw`), after the game's draw() */
void arc__scene_after_draw(void) { if (CL.debug) { int c = arc__draw_cell; arc__draw_cell = 0; arc_cells_debug_draw(); arc__draw_cell = c; } }

/* ======================================================================== */
/* Levels of detail                                                         */
/* ======================================================================== */

typedef struct { uint32_t mesh[4]; float thr[4]; uint8_t by_dist[4]; } lod_rec;   /* [1..3] used */
static lod_rec *g_lod; static float g_lod_bias = 1.0f; static int g_lod_force = -1;
static void lod_set(arc_mesh base, int level, arc_mesh lod, float thr, bool by_dist) {
    if (!mesh_ok(base) || level < 1 || level > 3) { arc_log("arc_mesh_set_lod: base mesh %u level %d (levels are 1..3)", base.id, level); return; }
    if (lod.id && (!mesh_ok(lod) || lod.id == base.id)) { arc_log("arc_mesh_set_lod: lod mesh %u is not usable", lod.id); return; }
    if (!g_lod) g_lod = (lod_rec *)calloc(ARC__MAX_MESHES, sizeof(lod_rec));
    if (!g_lod) return;
    lod_rec *L = &g_lod[base.id];
    L->mesh[level] = lod.id; L->thr[level] = thr; L->by_dist[level] = by_dist ? 1 : 0;
    if (lod.id) {
        arc__mesh_rec *b = &arc__meshes[base.id], *l = &arc__meshes[lod.id];
        if (!l->texture && b->texture) { l->texture = b->texture; l->owns_texture = false; }   /* borrowed, never owned */
        if (!l->has_mat && b->has_mat) { l->has_mat = true; l->mat_owns = 0; memcpy(l->mat_tex, b->mat_tex, sizeof l->mat_tex); memcpy(l->mat, b->mat, sizeof l->mat); l->detail_tex = b->detail_tex; l->mask_tex = b->mask_tex; memcpy(l->mat2, b->mat2, sizeof l->mat2); }
        l->reflect = b->reflect; l->unreflected = b->unreflected;   /* 0.8.7 */
        g_lod_any = true;
    }
}
void arc_mesh_set_lod(arc_mesh base, int level, arc_mesh lod, float screen_size) { lod_set(base, level, lod, screen_size, false); }
void arc_mesh_set_lod_distance(arc_mesh base, int level, arc_mesh lod, float distance) { lod_set(base, level, lod, distance, true); }
void arc_set_lod_bias(float bias) { g_lod_bias = bias > 0 ? bias : 1.0f; }
void arc_set_lod_force(int level) { g_lod_force = level < 0 ? -1 : arc_mini(level, 3); }
static void scene_lod_forget(uint32_t id) {
    if (!g_lod) return;
    memset(&g_lod[id], 0, sizeof g_lod[id]);
    for (int i = 1; i < ARC__MAX_MESHES; i++) for (int k = 1; k < 4; k++) if (g_lod[i].mesh[k] == id) g_lod[i].mesh[k] = 0;
}
/* the level for one instance: its bounding sphere's size on screen (or distance) against each level's threshold */
static int lod_level(uint32_t base, const float *mm) {
    const lod_rec *L = &g_lod[base];
    if (!L->mesh[1] && !L->mesh[2] && !L->mesh[3]) return -1;
    if (g_lod_force >= 0) { for (int k = g_lod_force; k > 0; k--) if (L->mesh[k] && mesh_ok((arc_mesh){ L->mesh[k] })) return k; return 0; }
    const arc__mesh_rec *M = &arc__meshes[base];
    arc_m4 m; memcpy(m.m, mm, sizeof m.m);
    arc_v3 c = arc_m4_mul_point(m, arc_v3_scale(arc_v3_add(M->lo, M->hi), 0.5f));
    float sx = arc_v3_len(arc_v3_make(mm[0], mm[1], mm[2])), sy = arc_v3_len(arc_v3_make(mm[4], mm[5], mm[6])), sz = arc_v3_len(arc_v3_make(mm[8], mm[9], mm[10]));
    float r = arc_v3_len(arc_v3_sub(M->hi, M->lo)) * 0.5f * arc_maxf(sx, arc_maxf(sy, sz));
    float d = arc_v3_dist(c, R.eye), p5 = R.proj.m[5];
    float cov = (R.view_persp ? (d > 1e-6f ? r * p5 / d : 1e30f) : r * p5) * g_lod_bias, dist = d / g_lod_bias;
    int lvl = 0;
    for (int k = 1; k < 4; k++)
        if (L->mesh[k] && (L->by_dist[k] ? dist > L->thr[k] : cov < L->thr[k]) && mesh_ok((arc_mesh){ L->mesh[k] })) lvl = k;
    return lvl;
}
static uint32_t scene_lod_pick(uint32_t mesh, const float *model) {
    int lvl = lod_level(mesh, model);
    if (lvl < 0) return mesh;
    g_lod_n[lvl]++;
    return lvl ? g_lod[mesh].mesh[lvl] : mesh;
}
int arc_mesh_lod_level(arc_mesh base, arc_m4 model) {
    if (!mesh_ok(base) || !g_lod) return 0;
    int l = lod_level(base.id, model.m); return l < 0 ? 0 : l;
}

/* ======================================================================== */
/* Static batches                                                           */
/* ======================================================================== */

#define ARC__MAX_STATICS 64
#define ARC__STATIC_MAX_VERTS (1 << 20)   /* a merged mesh is split before it grows past this */
typedef struct { uint32_t mesh; float model[16]; uint32_t tint; int cell; int cx, cz; } st_item;
typedef struct { uint32_t mesh; int cell; arc_v3 lo, hi; } st_out;
typedef struct { bool used; st_item *items; int n, cap; st_out *out; int nout; float chunk; } st_rec;
static st_rec g_static[ARC__MAX_STATICS];
static st_rec *static_rec(arc_static s) { return s.id && s.id < ARC__MAX_STATICS && g_static[s.id].used ? &g_static[s.id] : NULL; }
arc_static arc_static_create(void) {
    arc_static r = { 0 };
    for (uint32_t i = 1; i < ARC__MAX_STATICS; i++) if (!g_static[i].used) { memset(&g_static[i], 0, sizeof g_static[i]); g_static[i].used = true; r.id = i; return r; }
    arc_log("arc_static_create: more than %d static batches", ARC__MAX_STATICS - 1);
    return r;
}
static arc_v3 mesh_world_centre(const arc__mesh_rec *M, const float *mm) {
    arc_m4 m; memcpy(m.m, mm, sizeof m.m);
    return arc_m4_mul_point(m, arc_v3_scale(arc_v3_add(M->lo, M->hi), 0.5f));
}
void arc_static_add_cell(arc_static s, arc_mesh m, arc_m4 model, uint32_t tint, int cell) {
    st_rec *S = static_rec(s); if (!S || !mesh_ok(m)) return;
    if (arc__meshes[m.id].skin) { arc_log("arc_static_add: mesh %u is skinned; draw it on its own", m.id); return; }
    if (S->n == S->cap) { S->cap = S->cap ? S->cap * 2 : 256; S->items = (st_item *)realloc(S->items, sizeof(st_item) * (size_t)S->cap); }
    st_item *it = &S->items[S->n++];
    it->mesh = m.id; memcpy(it->model, model.m, sizeof it->model); it->tint = tint;
    it->cell = cell < 0 ? arc_cell_at(mesh_world_centre(&arc__meshes[m.id], model.m)) : cell_id_ok(cell) ? cell : 0;
    it->cx = it->cz = 0;
}
void arc_static_add(arc_static s, arc_mesh m, arc_m4 model, uint32_t tint) { arc_static_add_cell(s, m, model, tint, 0); }
void arc_static_set_chunk(arc_static s, float size) { st_rec *S = static_rec(s); if (S) S->chunk = size > 0 ? size : 0; }
static void static_free_out(st_rec *S) {
    for (int i = 0; i < S->nout; i++) arc_mesh_destroy((arc_mesh){ S->out[i].mesh });
    free(S->out); S->out = NULL; S->nout = 0;
}
/* the merge key: everything that must match for two draws to share one mesh */
typedef struct { uint32_t tex; int cell, cx, cz; uint8_t dbl, noshadow, has_mat, unrefl; uint32_t mat_tex[3]; float mat[12]; uint32_t detail_tex, mask_tex; float mat2[16]; float reflect; } st_key;
static void static_key(const st_item *it, st_key *k) {
    const arc__mesh_rec *M = &arc__meshes[it->mesh];
    memset(k, 0, sizeof *k);
    k->tex = M->texture; k->cell = it->cell; k->cx = it->cx; k->cz = it->cz;
    k->dbl = M->double_sided; k->noshadow = M->no_shadow; k->has_mat = M->has_mat; k->unrefl = M->unreflected; k->reflect = M->reflect;
    if (M->has_mat) { memcpy(k->mat_tex, M->mat_tex, sizeof k->mat_tex); memcpy(k->mat, M->mat, sizeof k->mat); k->detail_tex = M->detail_tex; k->mask_tex = M->mask_tex; memcpy(k->mat2, M->mat2, sizeof k->mat2); }
}
static st_rec *g_sort_rec;
static int static_cmp(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b; st_key ka, kb;
    static_key(&g_sort_rec->items[ia], &ka); static_key(&g_sort_rec->items[ib], &kb);
    int c = memcmp(&ka, &kb, sizeof ka);
    return c ? c : (ia > ib) - (ia < ib);   /* ties: in the order added (a stable, deterministic result) */
}
static void static_emit(st_rec *S, const int *order, int i0, int i1, int nv, int ni) {
    arc_vertex *v = (arc_vertex *)arc__xalloc(sizeof(arc_vertex) * (size_t)nv);
    uint32_t *ix = (uint32_t *)arc__xalloc(sizeof(uint32_t) * (size_t)ni);
    int vn = 0, in = 0;
    for (int k = i0; k < i1; k++) {
        const st_item *it = &S->items[order[k]]; const arc__mesh_rec *M = &arc__meshes[it->mesh];
        const float *a = it->model; arc_m4 m; memcpy(m.m, a, sizeof m.m);
        float det = a[0] * (a[5] * a[10] - a[6] * a[9]) - a[4] * (a[1] * a[10] - a[2] * a[9]) + a[8] * (a[1] * a[6] - a[2] * a[5]);
        uint32_t tr = (it->tint >> 16) & 0xFF, tg = (it->tint >> 8) & 0xFF, tb = it->tint & 0xFF;
        for (int i = 0; i < M->vcount; i++) {
            arc_vertex o = M->v[i];
            arc_v3 p = arc_m4_mul_point(m, arc_v3_make(o.px, o.py, o.pz));
            arc_v3 n = arc_v3_norm(arc_m4_mul_dir(m, arc_v3_make(o.nx, o.ny, o.nz)));   /* as the renderers turn normals */
            o.px = p.x; o.py = p.y; o.pz = p.z; o.nx = n.x; o.ny = n.y; o.nz = n.z;
            uint32_t c = o.color;   /* bytes R, G, B, A: the tint multiplies R, G, B */
            uint32_t r = ((c & 0xFF) * tr + 127) / 255, g = (((c >> 8) & 0xFF) * tg + 127) / 255, b = (((c >> 16) & 0xFF) * tb + 127) / 255;
            o.color = (c & 0xFF000000u) | (b << 16) | (g << 8) | r;
            v[vn + i] = o;
        }
        for (int t = 0; t + 2 < M->icount; t += 3) {   /* mirrored: turn each triangle round so it still faces out */
            ix[in++] = (uint32_t)vn + M->idx[t];
            ix[in++] = (uint32_t)vn + M->idx[det < 0 ? t + 2 : t + 1];
            ix[in++] = (uint32_t)vn + M->idx[det < 0 ? t + 1 : t + 2];
        }
        vn += M->vcount;
    }
    arc_mesh mm = arc_mesh_create(v, vn, ix, in);
    free(v); free(ix);
    if (!mm.id) return;
    const arc__mesh_rec *src = &arc__meshes[S->items[order[i0]].mesh]; arc__mesh_rec *dst = &arc__meshes[mm.id];
    dst->texture = src->texture; dst->owns_texture = false;   /* shared with the source meshes, never owned */
    dst->double_sided = src->double_sided; dst->no_shadow = src->no_shadow; dst->reflect = src->reflect; dst->unreflected = src->unreflected;
    if (src->has_mat) { dst->has_mat = true; dst->mat_owns = 0; memcpy(dst->mat_tex, src->mat_tex, sizeof dst->mat_tex); memcpy(dst->mat, src->mat, sizeof dst->mat); dst->detail_tex = src->detail_tex; dst->mask_tex = src->mask_tex; memcpy(dst->mat2, src->mat2, sizeof dst->mat2); }
    S->out = (st_out *)realloc(S->out, sizeof(st_out) * (size_t)(S->nout + 1));
    st_out *o = &S->out[S->nout++]; o->mesh = mm.id; o->cell = S->items[order[i0]].cell; o->lo = dst->lo; o->hi = dst->hi;
}
bool arc_static_build(arc_static s) {
    st_rec *S = static_rec(s); if (!S || !R.ready) return false;
    static_free_out(S);
    int n = 0, *order = (int *)arc__xalloc(sizeof(int) * (size_t)(S->n ? S->n : 1));
    for (int i = 0; i < S->n; i++) {
        st_item *it = &S->items[i];
        if (!mesh_ok((arc_mesh){ it->mesh }) || arc__meshes[it->mesh].skin) continue;   /* destroyed since it was added */
        if (S->chunk > 0) {
            arc_v3 c = mesh_world_centre(&arc__meshes[it->mesh], it->model);
            it->cx = (int)floorf(c.x / S->chunk); it->cz = (int)floorf(c.z / S->chunk);
        } else it->cx = it->cz = 0;
        order[n++] = i;
    }
    g_sort_rec = S; qsort(order, (size_t)n, sizeof(int), static_cmp); g_sort_rec = NULL;
    for (int i0 = 0; i0 < n;) {
        st_key k0; static_key(&S->items[order[i0]], &k0);
        int i1 = i0, nv = 0, ni = 0;
        while (i1 < n) {
            st_key k; static_key(&S->items[order[i1]], &k);
            const arc__mesh_rec *M = &arc__meshes[S->items[order[i1]].mesh];
            if (memcmp(&k, &k0, sizeof k)) break;
            if (i1 > i0 && nv + M->vcount > ARC__STATIC_MAX_VERTS) break;
            nv += M->vcount; ni += M->icount - M->icount % 3; i1++;
        }
        static_emit(S, order, i0, i1, nv, ni);
        i0 = i1;
    }
    free(order);
    return true;
}
void arc_static_draw(arc_static s) {
    st_rec *S = static_rec(s); if (!S || !S->nout || !R.in_frame) return;
    int saved = arc__draw_cell;
    bool frustum = !(R.sh_radius > 0 && R.sh_strength > 0);   /* with shadows, a chunk out of view may still cast into it */
    if (frustum) planes_update();
    arc_m4 id = arc_m4_identity();
    for (int i = 0; i < S->nout; i++) {
        const st_out *o = &S->out[i];
        if (frustum && !planes_aabb(g_planes, o->lo, o->hi)) { g_culled++; continue; }
        arc__draw_cell = o->cell;
        if (draw_mesh_full((arc_mesh){ o->mesh }, id, 0xFFFFFF, 1, 0, 0, 0, 0, 1, -1)) g_static_draws++;
    }
    arc__draw_cell = saved;
}
void arc_static_clear(arc_static s) { st_rec *S = static_rec(s); if (S) S->n = 0; }
void arc_static_destroy(arc_static s) {
    st_rec *S = static_rec(s); if (!S) return;
    static_free_out(S); free(S->items); memset(S, 0, sizeof *S);
}
int arc_static_mesh_count(arc_static s) { st_rec *S = static_rec(s); return S ? S->nout : 0; }
int arc_static_item_count(arc_static s) { st_rec *S = static_rec(s); return S ? S->n : 0; }

/* ======================================================================== */
/* Agent commands: cells, lod                                               */
/* ======================================================================== */

bool arc__scene_command(int argc, const char **argv) {
    const char *c = argv[0];
    if (!strcmp(c, "cells")) {
        if (argc > 1 && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"))) { CL.culling_off = !strcmp(argv[1], "off"); printf("ARC ok cells culling %s\n", CL.culling_off ? "off" : "on"); }
        else if (argc > 1 && !strcmp(argv[1], "draw")) { CL.debug = argc > 2 ? !strcmp(argv[2], "on") : !CL.debug; printf("ARC ok cells draw %s\n", CL.debug ? "on" : "off"); }
        else {
            int open = 0; for (int i = 0; i < CL.npor; i++) open += CL.por[i].open;
            if (CL.nbox) cells_current();
            printf("ARC cells boxes=%d portals=%d open=%d culling=%s camera_cell=%d visible=%d ids=", CL.nbox, CL.npor, open, CL.culling_off ? "off" : "on", CL.nbox ? CL.start : 0, CL.nbox ? CL.vis_count : 0);
            int first = 1;
            if (CL.nbox) for (int i = 1; i < ARC_MAX_CELLS; i++) if (bit_get(CL.vis, i)) { printf(first ? "%d" : ",%d", i); first = 0; }
            if (first) printf("-");
            printf("\n");
        }
        fflush(stdout); return true;
    }
    if (!strcmp(c, "lod")) {
        if (argc > 2 && !strcmp(argv[1], "bias")) arc_set_lod_bias((float)atof(argv[2]));
        else if (argc > 2 && !strcmp(argv[1], "force")) arc_set_lod_force(atoi(argv[2]));
        arc_gfx_stats st = arc_stats();
        printf("ARC lod bias=%g force=%d instances=%d,%d,%d,%d\n", g_lod_bias, g_lod_force, st.lod_instances[0], st.lod_instances[1], st.lod_instances[2], st.lod_instances[3]);
        fflush(stdout); return true;
    }
    return false;
}
