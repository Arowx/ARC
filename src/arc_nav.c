/* ARC 0.9.2: navigation. Navmeshes built from triangles, meshes and tile grids; A* over triangles with the funnel
   algorithm for straight paths; nearest point, walkable lines, random points; agents that follow paths.
   Plane maths is in x/z with the standard orientation (cross > 0 = counter-clockwise when x is right and z is up);
   heights come from the triangles. Building allocates; queries use scratch made at build time and never allocate. */

#define NAV_MAX 16
typedef struct { int v[3]; int n[3]; int island; float area; arc_v3 c; } navm_tri;   /* n[i]: across edge v[i] -> v[i+1] */
typedef struct {
    bool used, built;
    arc_navmesh_desc d;
    arc_v3 *in; int nin, cin;               /* input triangle soup, 3 vertices a triangle */
    arc_v3 *rbox; int nrbox, crbox;         /* removal boxes, lo/hi pairs */
    arc_v3 *v; uint8_t *vborder; int nv;    /* welded vertices; vborder: on the outline of the walkable area */
    navm_tri *t; int nt, islands;
    float *cum; float total_area;           /* cumulative areas for random points */
    float gx, gz, gcell; int gw, gh; int *gstart, *gitem;   /* x/z grid of triangle lists */
    /* query scratch */
    float *g, *f; int *parent; arc_v3 *pt; int *heap; uint32_t *stamp, *closed; uint32_t gen;
    int *corridor; arc_v3 *portals;         /* 2 * (nt + 2) points */
} navm_mesh;
static navm_mesh g_nav[NAV_MAX];

static navm_mesh *navm_get(arc_navmesh h) { return h.id > 0 && h.id <= NAV_MAX && g_nav[h.id - 1].used ? &g_nav[h.id - 1] : NULL; }
static float navm_cross(arc_v3 o, arc_v3 a, arc_v3 b) { return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x); }
static float navm_d2(arc_v3 a, arc_v3 b) { float dx = a.x - b.x, dz = a.z - b.z; return sqrtf(dx * dx + dz * dz); }
static bool navm_veq(arc_v3 a, arc_v3 b) { return a.x == b.x && a.z == b.z; }

static void navm_free_built(navm_mesh *m) {
    free(m->v); free(m->vborder); free(m->t); free(m->cum); free(m->gstart); free(m->gitem);
    free(m->g); free(m->f); free(m->parent); free(m->pt); free(m->heap); free(m->stamp); free(m->closed); free(m->corridor); free(m->portals);
    m->v = NULL; m->vborder = NULL; m->t = NULL; m->cum = NULL; m->gstart = m->gitem = NULL;
    m->g = m->f = NULL; m->parent = NULL; m->pt = NULL; m->heap = NULL; m->stamp = m->closed = NULL; m->corridor = NULL; m->portals = NULL;
    m->nv = m->nt = m->islands = 0; m->built = false;
}

arc_navmesh arc_navmesh_create(const arc_navmesh_desc *desc) {
    for (int i = 0; i < NAV_MAX; i++) if (!g_nav[i].used) {
        navm_mesh *m = &g_nav[i]; memset(m, 0, sizeof *m); m->used = true;
        if (desc) m->d = *desc;
        if (m->d.max_slope <= 0) m->d.max_slope = 0.8f;
        if (m->d.weld <= 0) m->d.weld = 0.01f;
        return (arc_navmesh){ (uint32_t)i + 1 };
    }
    arc_log("arc_navmesh_create: more than %d navmeshes", NAV_MAX);
    return (arc_navmesh){ 0 };
}
void arc_navmesh_destroy(arc_navmesh h) {
    navm_mesh *m = navm_get(h); if (!m) return;
    navm_free_built(m); free(m->in); free(m->rbox); memset(m, 0, sizeof *m);
}
static void navm_push(navm_mesh *m, arc_v3 a, arc_v3 b, arc_v3 c) {
    if (m->built) { m->nin = 0; m->nrbox = 0; navm_free_built(m); }   /* adding after a build starts a new one */
    if (m->nin + 3 > m->cin) { m->cin = m->cin ? m->cin * 2 : 768; m->in = (arc_v3 *)realloc(m->in, sizeof(arc_v3) * (size_t)m->cin); }
    m->in[m->nin++] = a; m->in[m->nin++] = b; m->in[m->nin++] = c;
}
void arc_navmesh_add_triangles(arc_navmesh h, const arc_v3 *v, int count) {
    navm_mesh *m = navm_get(h); if (!m || !v) return;
    for (int i = 0; i + 2 < count; i += 3) navm_push(m, v[i], v[i + 1], v[i + 2]);
}
void arc_navmesh_add_mesh(arc_navmesh h, arc_mesh mesh, arc_m4 model) {
    navm_mesh *m = navm_get(h);
    if (!m || !mesh.id || mesh.id >= ARC__MAX_MESHES || !arc__meshes[mesh.id].used) return;
    const arc__mesh_rec *r = &arc__meshes[mesh.id];
    if (!r->v) { arc_log("arc_navmesh_add_mesh: mesh %u has no vertices yet (still loading?)", mesh.id); return; }
    int tris = r->idx ? r->icount / 3 : r->vcount / 3;
    float *a = model.m; float det = a[0] * (a[5] * a[10] - a[6] * a[9]) - a[4] * (a[1] * a[10] - a[2] * a[9]) + a[8] * (a[1] * a[6] - a[2] * a[5]);
    for (int i = 0; i < tris; i++) {
        int k[3]; for (int j = 0; j < 3; j++) k[j] = r->idx ? (int)r->idx[i * 3 + j] : i * 3 + j;
        if (det < 0) { int s = k[1]; k[1] = k[2]; k[2] = s; }   /* mirrored model: keep the winding */
        arc_v3 p[3]; for (int j = 0; j < 3; j++) p[j] = arc_m4_mul_point(model, arc_v3_make(r->v[k[j]].px, r->v[k[j]].py, r->v[k[j]].pz));
        navm_push(m, p[0], p[1], p[2]);
    }
}
void arc_navmesh_add_grid(arc_navmesh h, const arc_grid *g, arc_v3 o, float cell, bool (*walkable)(uint8_t)) {
    navm_mesh *m = navm_get(h); if (!m || !g || !g->cells || cell <= 0) return;
    for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++) {
        uint8_t id = g->cells[y * g->w + x];
        if (walkable ? !walkable(id) : id != 0) continue;
        arc_v3 A = arc_v3_make(o.x + (float)x * cell, o.y, o.z + (float)y * cell), B = arc_v3_make(A.x + cell, o.y, A.z);
        arc_v3 C = arc_v3_make(A.x + cell, o.y, A.z + cell), D = arc_v3_make(A.x, o.y, A.z + cell);
        navm_push(m, A, D, B); navm_push(m, B, D, C);   /* counter-clockwise from above: normals up */
    }
}
void arc_navmesh_remove_box(arc_navmesh h, arc_v3 lo, arc_v3 hi) {
    navm_mesh *m = navm_get(h); if (!m) return;
    if (m->built) { m->nin = 0; m->nrbox = 0; navm_free_built(m); }
    if (m->nrbox + 2 > m->crbox) { m->crbox = m->crbox ? m->crbox * 2 : 16; m->rbox = (arc_v3 *)realloc(m->rbox, sizeof(arc_v3) * (size_t)m->crbox); }
    m->rbox[m->nrbox++] = lo; m->rbox[m->nrbox++] = hi;
}

/* ---- build ------------------------------------------------------------------------------------------------------ */
typedef struct { int64_t k[3]; int i; } navm_wkey;
typedef struct { int a, b, t, e; } navm_edge;
static int navm_wcmp(const void *x, const void *y) {
    const navm_wkey *a = (const navm_wkey *)x, *b = (const navm_wkey *)y;
    for (int i = 0; i < 3; i++) if (a->k[i] != b->k[i]) return a->k[i] < b->k[i] ? -1 : 1;
    return a->i - b->i;
}
static int navm_ecmp(const void *x, const void *y) {
    const navm_edge *a = (const navm_edge *)x, *b = (const navm_edge *)y;
    if (a->a != b->a) return a->a - b->a;
    if (a->b != b->b) return a->b - b->b;
    return a->t != b->t ? a->t - b->t : a->e - b->e;
}

bool arc_navmesh_build(arc_navmesh h) {
    navm_mesh *m = navm_get(h); if (!m) return false;
    navm_free_built(m);
    float cos_max = arc_cosf(m->d.max_slope);   /* (0.9.7: ARC's own, the same on every machine) */
    int ntin = m->nin / 3;
    char *keep = (char *)calloc((size_t)ntin + 1, 1);
    int nk = 0;
    for (int i = 0; i < ntin; i++) {
        arc_v3 a = m->in[i * 3], b = m->in[i * 3 + 1], c = m->in[i * 3 + 2];
        arc_v3 n = arc_v3_cross(arc_v3_sub(b, a), arc_v3_sub(c, a)); float l = arc_v3_len(n);
        if (l < 1e-12f || n.y / l < cos_max) continue;   /* degenerate, facing down, or too steep */
        arc_v3 cen = arc_v3_scale(arc_v3_add(arc_v3_add(a, b), c), 1.0f / 3.0f); bool removed = false;
        for (int r = 0; r + 1 < m->nrbox; r += 2) {
            arc_v3 lo = m->rbox[r], hi = m->rbox[r + 1];
            if (cen.x >= lo.x && cen.x <= hi.x && cen.y >= lo.y && cen.y <= hi.y && cen.z >= lo.z && cen.z <= hi.z) { removed = true; break; }
        }
        if (!removed) { keep[i] = 1; nk++; }
    }
    if (!nk) { free(keep); return false; }
    /* weld: sort the kept vertices by their position rounded to the weld size */
    navm_wkey *wk = (navm_wkey *)malloc(sizeof(navm_wkey) * (size_t)nk * 3); int nw = 0;
    float iw = 1.0f / m->d.weld;
    for (int i = 0; i < ntin; i++) if (keep[i]) for (int j = 0; j < 3; j++) {
        arc_v3 p = m->in[i * 3 + j];
        wk[nw].k[0] = (int64_t)floorf(p.x * iw + 0.5f); wk[nw].k[1] = (int64_t)floorf(p.y * iw + 0.5f); wk[nw].k[2] = (int64_t)floorf(p.z * iw + 0.5f);
        wk[nw].i = i * 3 + j; nw++;
    }
    qsort(wk, (size_t)nw, sizeof *wk, navm_wcmp);
    int *vid = (int *)malloc(sizeof(int) * (size_t)m->nin);
    m->v = (arc_v3 *)malloc(sizeof(arc_v3) * (size_t)nw); m->nv = 0;
    for (int i = 0; i < nw; i++) {
        if (i == 0 || navm_wcmp(&(navm_wkey){ { wk[i].k[0], wk[i].k[1], wk[i].k[2] }, 0 }, &(navm_wkey){ { wk[i - 1].k[0], wk[i - 1].k[1], wk[i - 1].k[2] }, 0 }) != 0)
            m->v[m->nv++] = m->in[wk[i].i];   /* the first of each group (lowest input index) is the position */
        vid[wk[i].i] = m->nv - 1;
    }
    free(wk);
    m->t = (navm_tri *)calloc((size_t)nk, sizeof(navm_tri)); m->nt = 0;
    for (int i = 0; i < ntin; i++) if (keep[i]) {
        int a = vid[i * 3], b = vid[i * 3 + 1], c = vid[i * 3 + 2];
        if (a == b || b == c || a == c) continue;
        navm_tri *t = &m->t[m->nt]; t->v[0] = a; t->v[1] = b; t->v[2] = c; t->n[0] = t->n[1] = t->n[2] = -1;
        t->c = arc_v3_scale(arc_v3_add(arc_v3_add(m->v[a], m->v[b]), m->v[c]), 1.0f / 3.0f);
        t->area = 0.5f * fabsf(navm_cross(m->v[a], m->v[b], m->v[c]));
        if (t->area < 1e-10f) continue;
        m->nt++;
    }
    free(vid); free(keep);
    if (!m->nt) { navm_free_built(m); return false; }
    /* neighbours: edges sorted by their (smaller, larger) vertex pair; two triangles on one pair are joined */
    navm_edge *ed = (navm_edge *)malloc(sizeof(navm_edge) * (size_t)m->nt * 3);
    for (int i = 0; i < m->nt; i++) for (int e = 0; e < 3; e++) {
        int a = m->t[i].v[e], b = m->t[i].v[(e + 1) % 3];
        ed[i * 3 + e] = (navm_edge){ a < b ? a : b, a < b ? b : a, i, e };
    }
    qsort(ed, (size_t)m->nt * 3, sizeof *ed, navm_ecmp);
    for (int i = 0; i + 1 < m->nt * 3; i++) if (ed[i].a == ed[i + 1].a && ed[i].b == ed[i + 1].b) {
        if (m->t[ed[i].t].n[ed[i].e] < 0 && m->t[ed[i + 1].t].n[ed[i + 1].e] < 0) {
            m->t[ed[i].t].n[ed[i].e] = ed[i + 1].t; m->t[ed[i + 1].t].n[ed[i + 1].e] = ed[i].t;
        }
        i++;
    }
    free(ed);
    m->vborder = (uint8_t *)calloc((size_t)m->nv, 1);
    for (int i = 0; i < m->nt; i++) for (int e = 0; e < 3; e++) if (m->t[i].n[e] < 0) { m->vborder[m->t[i].v[e]] = 1; m->vborder[m->t[i].v[(e + 1) % 3]] = 1; }
    /* islands */
    int *stack = (int *)malloc(sizeof(int) * (size_t)m->nt);
    for (int i = 0; i < m->nt; i++) m->t[i].island = -1;
    m->islands = 0;
    for (int i = 0; i < m->nt; i++) if (m->t[i].island < 0) {
        int sp = 0; stack[sp++] = i; m->t[i].island = m->islands;
        while (sp) { int c = stack[--sp]; for (int e = 0; e < 3; e++) { int n = m->t[c].n[e]; if (n >= 0 && m->t[n].island < 0) { m->t[n].island = m->islands; stack[sp++] = n; } } }
        m->islands++;
    }
    free(stack);
    /* areas for random points */
    m->cum = (float *)malloc(sizeof(float) * (size_t)m->nt); m->total_area = 0;
    for (int i = 0; i < m->nt; i++) { m->total_area += m->t[i].area; m->cum[i] = m->total_area; }
    /* x/z grid of triangle lists */
    float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f;
    for (int i = 0; i < m->nv; i++) { x0 = fminf(x0, m->v[i].x); z0 = fminf(z0, m->v[i].z); x1 = fmaxf(x1, m->v[i].x); z1 = fmaxf(z1, m->v[i].z); }
    m->gcell = fmaxf(sqrtf(m->total_area / (float)m->nt) * 2.0f, 0.25f);
    m->gx = x0; m->gz = z0;
    m->gw = (int)((x1 - x0) / m->gcell) + 1; m->gh = (int)((z1 - z0) / m->gcell) + 1;
    while ((int64_t)m->gw * m->gh > 4 * (int64_t)m->nt + 1024) { m->gcell *= 1.5f; m->gw = (int)((x1 - x0) / m->gcell) + 1; m->gh = (int)((z1 - z0) / m->gcell) + 1; }
    int ncell = m->gw * m->gh;
    m->gstart = (int *)calloc((size_t)ncell + 1, sizeof(int));
    for (int pass = 0; pass < 2; pass++) {
        int *fill = pass ? (int *)calloc((size_t)ncell, sizeof(int)) : NULL;
        for (int i = 0; i < m->nt; i++) {
            float tx0 = 1e30f, tz0 = 1e30f, tx1 = -1e30f, tz1 = -1e30f;
            for (int k = 0; k < 3; k++) { arc_v3 p = m->v[m->t[i].v[k]]; tx0 = fminf(tx0, p.x); tz0 = fminf(tz0, p.z); tx1 = fmaxf(tx1, p.x); tz1 = fmaxf(tz1, p.z); }
            int cx0 = (int)((tx0 - m->gx) / m->gcell), cx1 = (int)((tx1 - m->gx) / m->gcell), cz0 = (int)((tz0 - m->gz) / m->gcell), cz1 = (int)((tz1 - m->gz) / m->gcell);
            for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) {
                int c = cz * m->gw + cx;
                if (!pass) m->gstart[c + 1]++; else m->gitem[m->gstart[c] + fill[c]++] = i;
            }
        }
        if (!pass) { for (int c = 0; c < ncell; c++) m->gstart[c + 1] += m->gstart[c]; m->gitem = (int *)malloc(sizeof(int) * (size_t)(m->gstart[ncell] + 1)); }
        free(fill);
    }
    /* scratch for queries */
    size_t nt = (size_t)m->nt;
    m->g = (float *)malloc(sizeof(float) * nt); m->f = (float *)malloc(sizeof(float) * nt); m->parent = (int *)malloc(sizeof(int) * nt);
    m->pt = (arc_v3 *)malloc(sizeof(arc_v3) * nt); m->heap = (int *)malloc(sizeof(int) * (nt * 3 + 4));
    m->stamp = (uint32_t *)calloc(nt, sizeof(uint32_t)); m->closed = (uint32_t *)calloc(nt, sizeof(uint32_t)); m->gen = 0;
    m->corridor = (int *)malloc(sizeof(int) * (nt + 2)); m->portals = (arc_v3 *)malloc(sizeof(arc_v3) * (nt + 2) * 2);
    m->built = true;
    return true;
}
int arc_navmesh_triangles(arc_navmesh h) { navm_mesh *m = navm_get(h); return m && m->built ? m->nt : 0; }
int arc_navmesh_islands(arc_navmesh h) { navm_mesh *m = navm_get(h); return m && m->built ? m->islands : 0; }

/* ---- locating points ---------------------------------------------------------------------------------------------- */
static bool navm_in_tri(const navm_mesh *m, int t, arc_v3 p, float *height) {
    arc_v3 a = m->v[m->t[t].v[0]], b = m->v[m->t[t].v[1]], c = m->v[m->t[t].v[2]];
    float d = navm_cross(a, b, c); if (fabsf(d) < 1e-12f) return false;
    float w0 = navm_cross(b, c, p) / d, w1 = navm_cross(c, a, p) / d, w2 = 1.0f - w0 - w1;
    const float eps = -1e-5f;
    if (w0 < eps || w1 < eps || w2 < eps) return false;
    if (height) *height = w0 * a.y + w1 * b.y + w2 * c.y;
    return true;
}
static float navm_tri_height(const navm_mesh *m, int t, arc_v3 p) {   /* the triangle's plane height at p's x/z */
    arc_v3 a = m->v[m->t[t].v[0]], b = m->v[m->t[t].v[1]], c = m->v[m->t[t].v[2]];
    float d = navm_cross(a, b, c); if (fabsf(d) < 1e-12f) return a.y;
    float w0 = navm_cross(b, c, p) / d, w1 = navm_cross(c, a, p) / d;
    return w0 * a.y + w1 * b.y + (1.0f - w0 - w1) * c.y;
}
/* the triangle under or over p (its x/z inside), the one whose height is closest to p.y; -1 if none */
static int navm_locate(const navm_mesh *m, arc_v3 p, float *height) {
    int cx = (int)floorf((p.x - m->gx) / m->gcell), cz = (int)floorf((p.z - m->gz) / m->gcell);
    if (cx < 0 || cz < 0 || cx >= m->gw || cz >= m->gh) return -1;
    int best = -1; float bd = 1e30f, bh = 0;
    int c = cz * m->gw + cx;
    for (int k = m->gstart[c]; k < m->gstart[c + 1]; k++) {
        int t = m->gitem[k]; float hy;
        if (navm_in_tri(m, t, p, &hy) && fabsf(hy - p.y) < bd) { bd = fabsf(hy - p.y); best = t; bh = hy; }
    }
    if (best >= 0 && height) *height = bh;
    return best;
}
static arc_v3 navm_closest_on_tri(arc_v3 p, arc_v3 a, arc_v3 b, arc_v3 c) {   /* Ericson, Real-Time Collision Detection 5.1.5 */
    arc_v3 ab = arc_v3_sub(b, a), ac = arc_v3_sub(c, a), ap = arc_v3_sub(p, a);
    float d1 = arc_v3_dot(ab, ap), d2 = arc_v3_dot(ac, ap); if (d1 <= 0 && d2 <= 0) return a;
    arc_v3 bp = arc_v3_sub(p, b); float d3 = arc_v3_dot(ab, bp), d4 = arc_v3_dot(ac, bp); if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2; if (vc <= 0 && d1 >= 0 && d3 <= 0) return arc_v3_add(a, arc_v3_scale(ab, d1 / (d1 - d3)));
    arc_v3 cp = arc_v3_sub(p, c); float d5 = arc_v3_dot(ab, cp), d6 = arc_v3_dot(ac, cp); if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6; if (vb <= 0 && d2 >= 0 && d6 <= 0) return arc_v3_add(a, arc_v3_scale(ac, d2 / (d2 - d6)));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return arc_v3_add(b, arc_v3_scale(arc_v3_sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    float den = 1.0f / (va + vb + vc); return arc_v3_add(a, arc_v3_add(arc_v3_scale(ab, vb * den), arc_v3_scale(ac, vc * den)));
}
/* nearest point on the mesh (3D distance) within max_dist (<= 0: any); returns its triangle or -1 */
static int navm_nearest(navm_mesh *m, arc_v3 p, float max_dist, arc_v3 *out) {
    float h; int t = navm_locate(m, p, &h);
    if (t >= 0 && (max_dist <= 0 || fabsf(h - p.y) <= max_dist)) {
        /* inside a triangle's x/z: still look at neighbours in case another surface is nearer in 3D */
        arc_v3 q = arc_v3_make(p.x, h, p.z);
        if (fabsf(h - p.y) < m->gcell * 0.5f) { if (out) *out = q; return t; }
    }
    int best = -1; float bd = max_dist > 0 ? max_dist : 1e30f; arc_v3 bq = p;
    int cx = (int)floorf((p.x - m->gx) / m->gcell), cz = (int)floorf((p.z - m->gz) / m->gcell);
    int maxr = m->gw + m->gh + 2;
    if (max_dist > 0) { int r = (int)(max_dist / m->gcell) + 2; if (r < maxr) maxr = r; }
    m->gen++;
    for (int r = 0; r <= maxr; r++) {
        if (best >= 0 && ((float)(r - 1)) * m->gcell > bd) break;
        for (int z = cz - r; z <= cz + r; z++) for (int x = cx - r; x <= cx + r; x++) {
            if (abs(x - cx) != r && abs(z - cz) != r) continue;   /* only the ring */
            if (x < 0 || z < 0 || x >= m->gw || z >= m->gh) continue;
            int c = z * m->gw + x;
            for (int k = m->gstart[c]; k < m->gstart[c + 1]; k++) {
                int ti = m->gitem[k]; if (m->closed[ti] == m->gen) continue; m->closed[ti] = m->gen;
                arc_v3 q = navm_closest_on_tri(p, m->v[m->t[ti].v[0]], m->v[m->t[ti].v[1]], m->v[m->t[ti].v[2]]);
                float d = arc_v3_len(arc_v3_sub(q, p));
                if (d < bd || (d == bd && ti < best)) { bd = d; best = ti; bq = q; }
            }
        }
    }
    if (best >= 0 && out) *out = bq;
    return best;
}
bool arc_navmesh_nearest(arc_navmesh h, arc_v3 p, float max_dist, arc_v3 *out) {
    navm_mesh *m = navm_get(h); if (!m || !m->built) return false;
    return navm_nearest(m, p, max_dist, out) >= 0;
}
float arc_navmesh_height(arc_navmesh h, arc_v3 p, bool *found) {
    navm_mesh *m = navm_get(h); float y = p.y; bool ok = false;
    if (m && m->built) ok = navm_locate(m, p, &y) >= 0;
    if (found) *found = ok;
    return ok ? y : p.y;
}
bool arc_navmesh_connected(arc_navmesh h, arc_v3 a, arc_v3 b) {
    navm_mesh *m = navm_get(h); if (!m || !m->built) return false;
    int ta = navm_nearest(m, a, 0, NULL), tb = navm_nearest(m, b, 0, NULL);
    return ta >= 0 && tb >= 0 && m->t[ta].island == m->t[tb].island;
}
arc_v3 arc_navmesh_random_point(arc_navmesh h, arc_rng *rng) {
    navm_mesh *m = navm_get(h); if (!m || !m->built) return arc_v3_make(0, 0, 0);
    float r = (rng ? arc_rng_float(rng) : arc_rand_float()) * m->total_area;
    int lo = 0, hi = m->nt - 1;
    while (lo < hi) { int mid = (lo + hi) / 2; if (m->cum[mid] < r) lo = mid + 1; else hi = mid; }
    float u = rng ? arc_rng_float(rng) : arc_rand_float(), v = rng ? arc_rng_float(rng) : arc_rand_float();
    if (u + v > 1) { u = 1 - u; v = 1 - v; }
    arc_v3 a = m->v[m->t[lo].v[0]], b = m->v[m->t[lo].v[1]], c = m->v[m->t[lo].v[2]];
    return arc_v3_add(a, arc_v3_add(arc_v3_scale(arc_v3_sub(b, a), u), arc_v3_scale(arc_v3_sub(c, a), v)));
}

/* ---- walkable lines ------------------------------------------------------------------------------------------------ */
/* walk from `from` (in triangle t) toward `to` in x/z. Returns true on reaching to's x/z. Else *hit is where it left
   the walkable area and (*ea, *eb) the blocking edge. *tout: the triangle it ended in. */
static bool navm_line(const navm_mesh *m, int t, arc_v3 from, arc_v3 to, arc_v3 *hit, arc_v3 *ea, arc_v3 *eb, int *tout) {
    int entry = -1; float tcur = 0;
    float dx = to.x - from.x, dz = to.z - from.z;
    for (int guard = 0; guard < m->nt + 8; guard++) {
        if (navm_in_tri(m, t, to, NULL)) { if (tout) *tout = t; return true; }
        int best_e = -1; float best_t = -1;
        for (int e = 0; e < 3; e++) {
            if (m->t[t].n[e] >= 0 && m->t[t].n[e] == entry) continue;
            arc_v3 a = m->v[m->t[t].v[e]], b = m->v[m->t[t].v[(e + 1) % 3]];
            float ex = b.x - a.x, ez = b.z - a.z, den = dx * ez - dz * ex;
            if (fabsf(den) < 1e-12f) continue;
            float ax = a.x - from.x, az = a.z - from.z;
            float tt = (ax * ez - az * ex) / den, s = (ax * dz - az * dx) / den;
            if (s < -1e-5f || s > 1 + 1e-5f || tt < tcur - 1e-5f) continue;
            if (tt > best_t) { best_t = tt; best_e = e; }
        }
        if (best_e < 0) { if (tout) *tout = t; if (hit) *hit = from; if (ea) *ea = from; if (eb) *eb = from; return false; }
        int n = m->t[t].n[best_e];
        if (n < 0) {
            arc_v3 p = arc_v3_make(from.x + dx * best_t, 0, from.z + dz * best_t); p.y = navm_tri_height(m, t, p);
            if (hit) *hit = p;
            if (ea) *ea = m->v[m->t[t].v[best_e]];
            if (eb) *eb = m->v[m->t[t].v[(best_e + 1) % 3]];
            if (tout) *tout = t;
            return false;
        }
        entry = t; t = n; tcur = best_t;
    }
    if (tout) *tout = t;
    if (hit) *hit = from;
    return false;
}
bool arc_navmesh_walkable_line(arc_navmesh h, arc_v3 from, arc_v3 to, arc_v3 *hit) {
    navm_mesh *m = navm_get(h); if (!m || !m->built) return false;
    float y; int t = navm_locate(m, from, &y);
    if (t < 0) { if (hit) *hit = from; return false; }
    from.y = y;
    return navm_line(m, t, from, to, hit, NULL, NULL, NULL);
}

/* ---- paths ---------------------------------------------------------------------------------------------------------- */
static void navm_heap_push(navm_mesh *m, int *n, int t) {
    int i = (*n)++; m->heap[i] = t;
    while (i > 0) { int p = (i - 1) / 2; int a = m->heap[p];
        if (m->f[a] < m->f[t] || (m->f[a] == m->f[t] && a < t)) break;
        m->heap[i] = a; i = p; }
    m->heap[i] = t;
}
static int navm_heap_pop(navm_mesh *m, int *n) {
    int top = m->heap[0], last = m->heap[--(*n)], i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, s = i; int sv = last;
        if (l < *n) { int a = m->heap[l]; if (m->f[a] < m->f[sv] || (m->f[a] == m->f[sv] && a < sv)) { s = l; sv = a; } }
        if (r < *n) { int a = m->heap[r]; if (m->f[a] < m->f[sv] || (m->f[a] == m->f[sv] && a < sv)) { s = r; sv = a; } }
        if (s == i) break;
        m->heap[i] = sv; i = s;
    }
    if (*n > 0) m->heap[i] = last;
    return top;
}
/* where the line from p toward goal crosses edge a-b, kept `keep` (a fraction of the edge) from its ends; the middle
   when they are parallel. Entering triangles there (not at edge middles) makes A* prefer straight corridors. */
static arc_v3 navm_cross_point(arc_v3 p, arc_v3 goal, arc_v3 a, arc_v3 b, float keep) {
    float dx = goal.x - p.x, dz = goal.z - p.z, ex = b.x - a.x, ez = b.z - a.z, den = dx * ez - dz * ex, s = 0.5f;
    if (fabsf(den) > 1e-12f) s = ((a.x - p.x) * dz - (a.z - p.z) * dx) / den;
    if (keep > 0.5f) keep = 0.5f;
    s = arc_clampf(s, keep, 1.0f - keep);
    return arc_v3_add(a, arc_v3_scale(arc_v3_sub(b, a), s));
}
static int navm_shared_edge(const navm_mesh *m, int a, int b) { for (int e = 0; e < 3; e++) if (m->t[a].n[e] == b) return e; return -1; }

/* a straight walk from a to b is clear for an agent of radius r: the centre line and lines r to each side */
static bool navm_band_clear(const navm_mesh *m, arc_v3 a, arc_v3 b, float r) {
    float dx = b.x - a.x, dz = b.z - a.z, l = sqrtf(dx * dx + dz * dz);
    if (l < 1e-6f) return true;
    for (int k = -1; k <= 1; k++) {
        if (k != 0 && r <= 0) continue;
        arc_v3 o = arc_v3_make(-dz / l * r * (float)k, 0, dx / l * r * (float)k);
        arc_v3 pa = arc_v3_add(a, o), pb = arc_v3_add(b, o);
        float y; int t = navm_locate(m, pa, &y);
        if (t < 0) return false;
        pa.y = y;
        if (!navm_line(m, t, pa, pb, NULL, NULL, NULL, NULL)) return false;
    }
    return true;
}
int arc_navmesh_path(arc_navmesh h, arc_v3 from, arc_v3 to, float radius, arc_v3 *out, int max) {
    navm_mesh *m = navm_get(h); if (!m || !m->built) return 0;
    if (radius < 0) radius = 0;
    arc_v3 s, e;
    int ts = navm_nearest(m, from, 0, &s), te = navm_nearest(m, to, 0, &e);
    if (ts < 0 || te < 0 || m->t[ts].island != m->t[te].island) return 0;
    /* A* over triangles: each triangle is entered at the middle of its entry edge */
    int nc = 0;
    if (ts != te) {
        m->gen++; int hn = 0; bool found = false;
        m->stamp[ts] = m->gen; m->g[ts] = 0; m->f[ts] = navm_d2(s, e); m->parent[ts] = -1; m->pt[ts] = s;
        navm_heap_push(m, &hn, ts);
        while (hn) {
            int c = navm_heap_pop(m, &hn);
            if (m->closed[c] == m->gen) continue;
            m->closed[c] = m->gen;
            if (c == te) { found = true; break; }
            for (int k = 0; k < 3; k++) {
                int n = m->t[c].n[k]; if (n < 0 || m->closed[n] == m->gen) continue;
                arc_v3 a = m->v[m->t[c].v[k]], b = m->v[m->t[c].v[(k + 1) % 3]];
                float el = navm_d2(a, b);
                /* too narrow for this agent: only a gap between two outline points is a real gap. 0.10.19: an edge
                   with a free end (inside the walkable area) carries on into more floor, so on a fine grid (half-unit
                   cells from add_grid) every short inner edge no longer blocks a wider agent */
                if (el < 2 * radius && m->vborder[m->t[c].v[k]] && m->vborder[m->t[c].v[(k + 1) % 3]]) continue;
                arc_v3 mid = navm_cross_point(m->pt[c], e, a, b, el > 0 ? radius / el : 0.5f);
                float g = m->g[c] + navm_d2(m->pt[c], mid);
                if (n == te) g += navm_d2(mid, e);
                if (m->stamp[n] != m->gen || g < m->g[n]) {
                    m->stamp[n] = m->gen; m->g[n] = g; m->parent[n] = c; m->pt[n] = mid;
                    m->f[n] = g + (n == te ? 0 : navm_d2(mid, e));
                    if (hn < m->nt * 3) navm_heap_push(m, &hn, n);
                }
            }
        }
        if (!found) return 0;
        for (int c = te; c >= 0; c = m->parent[c]) nc++;
        int i = nc; for (int c = te; c >= 0; c = m->parent[c]) m->corridor[--i] = c;
    } else { m->corridor[0] = ts; nc = 1; }
    /* portals (right, left as seen walking through), shrunk by radius at outline corners */
    arc_v3 *P = m->portals; int np = 0;
    P[np * 2] = s; P[np * 2 + 1] = s; np++;
    for (int i = 0; i + 1 < nc; i++) {
        int a = m->corridor[i], b = m->corridor[i + 1], k = navm_shared_edge(m, a, b);
        int ia = m->t[a].v[k], ib = m->t[a].v[(k + 1) % 3];
        arc_v3 p = m->v[ia], q = m->v[ib];
        if (navm_cross(m->t[a].c, p, q) < 0) { arc_v3 tmp = p; p = q; q = tmp; int ti = ia; ia = ib; ib = ti; }
        if (radius > 0) {
            float l = arc_v3_len(arc_v3_sub(q, p));
            if (l > 1e-6f) {
                arc_v3 d = arc_v3_scale(arc_v3_sub(q, p), fminf(radius, l * 0.95f) / l);   /* 0.10.19: never past the other end */
                if (m->vborder[ia]) p = arc_v3_add(p, d);
                if (m->vborder[ib]) q = arc_v3_sub(q, d);
            }
        }
        P[np * 2] = p; P[np * 2 + 1] = q; np++;
    }
    P[np * 2] = e; P[np * 2 + 1] = e; np++;
    /* the funnel ("simple stupid funnel algorithm", Mononen), right = P[2i], left = P[2i + 1] */
    int n = 0;
    arc_v3 last_out = s;
    if (out && max > 0) out[0] = s;
    n = 1;
    arc_v3 apex = s, right = s, left = s; int ai = 0, ri = 0, li = 0;
    for (int i = 1; i < np; i++) {
        arc_v3 pr = P[i * 2], pl = P[i * 2 + 1];
        if (navm_cross(apex, right, pr) >= 0) {   /* the new right point narrows the funnel */
            if (navm_veq(apex, right) || navm_cross(apex, left, pr) < 0) { right = pr; ri = i; }
            else {   /* it crosses the left side: the left point is a corner */
                if (!navm_veq(last_out, left)) { if (out && n < max) out[n] = left; last_out = left; n++; }
                apex = left; ai = li; right = apex; left = apex; ri = ai; li = ai; i = ai; continue;
            }
        }
        if (navm_cross(apex, left, pl) <= 0) {
            if (navm_veq(apex, left) || navm_cross(apex, right, pl) > 0) { left = pl; li = i; }
            else {
                if (!navm_veq(last_out, right)) { if (out && n < max) out[n] = right; last_out = right; n++; }
                apex = right; ai = ri; right = apex; left = apex; ri = ai; li = ai; i = ai; continue;
            }
        }
    }
    if (!navm_veq(last_out, e) || n == 1) { if (out && n < max) out[n] = e; n++; }
    /* drop corners whose neighbours see each other along a band as wide as the agent (only when the whole path
       fitted in out: the count of a cut path stays the funnel's) */
    if (out && n <= max && n > 2) {
        int w = 1;
        for (int i = 1; i < n; i++) {
            if (i < n - 1 && navm_band_clear(m, out[w - 1], out[i + 1], radius)) continue;
            out[w++] = out[i];
        }
        n = w;
    }
    return n;
}
float arc_navmesh_path_length(const arc_v3 *p, int n) { float l = 0; for (int i = 1; i < n; i++) l += arc_v3_len(arc_v3_sub(p[i], p[i - 1])); return l; }

/* ---- debug drawing ---------------------------------------------------------------------------------------------------- */
void arc_navmesh_draw(arc_navmesh h, uint32_t rgb) {
    navm_mesh *m = navm_get(h); if (!m || !m->built) return;
    uint32_t dim = ((rgb >> 1) & 0x7F7F7F);
    arc_v3 up = arc_v3_make(0, 0.03f, 0);
    for (int i = 0; i < m->nt; i++) for (int e = 0; e < 3; e++) {
        int n = m->t[i].n[e];
        if (n >= 0 && n < i) continue;
        arc_draw_line(arc_v3_add(m->v[m->t[i].v[e]], up), arc_v3_add(m->v[m->t[i].v[(e + 1) % 3]], up), n < 0 ? rgb : dim);
    }
}

/* ---- agents ---------------------------------------------------------------------------------------------------------- */
void arc_nav_agent_init(arc_nav_agent *a, arc_v3 pos, float speed, float radius) {
    memset(a, 0, sizeof *a); a->pos = pos; a->speed = speed; a->radius = radius; a->target = pos;
}
void arc_nav_agent_stop(arc_nav_agent *a) { a->moving = false; a->ncorners = a->corner = 0; a->partial = false; }
bool arc_nav_agent_goto(arc_nav_agent *a, arc_navmesh h, arc_v3 target) {
    navm_mesh *m = navm_get(h);
    a->target = target; a->arrived = false; a->failed = false; a->partial = false; a->corner = 0; a->ncorners = 0;
    if (!m || !m->built) { a->failed = true; a->moving = false; return false; }
    int n = arc_navmesh_path(h, a->pos, target, a->radius, a->corners, ARC_NAV_CORNERS);
    if (n == 0) { a->failed = true; a->moving = false; return false; }
    a->ncorners = n < ARC_NAV_CORNERS ? n : ARC_NAV_CORNERS; a->partial = n > ARC_NAV_CORNERS;
    a->pos = a->corners[0];   /* onto the mesh */
    a->corner = a->ncorners > 1 ? 1 : 0; a->moving = true;
    return true;
}
/* move from p by d over the surface, sliding along the outline; returns the new point (height on the surface) */
static arc_v3 navm_slide(const navm_mesh *m, arc_v3 p, arc_v3 d) {
    float y; int t = navm_locate(m, p, &y);
    if (t < 0) { arc_v3 q; int tn = navm_nearest((navm_mesh *)m, p, 0, &q); if (tn < 0) return p; p = q; t = tn; }
    else p.y = y;
    arc_v3 to = arc_v3_make(p.x + d.x, p.y, p.z + d.z);
    for (int iter = 0; iter < 3; iter++) {
        arc_v3 hit, ea, eb; int tend;
        if (navm_line(m, t, p, to, &hit, &ea, &eb, &tend)) { to.y = navm_tri_height(m, tend, to); return to; }
        /* stop just inside the edge, then slide the rest along it */
        arc_v3 dir = arc_v3_make(to.x - p.x, 0, to.z - p.z); float dl = sqrtf(dir.x * dir.x + dir.z * dir.z);
        arc_v3 back = dl > 1e-9f ? arc_v3_scale(dir, -1e-3f / dl) : arc_v3_make(0, 0, 0);
        arc_v3 stop = arc_v3_add(hit, back);
        arc_v3 ed = arc_v3_make(eb.x - ea.x, 0, eb.z - ea.z); float el = sqrtf(ed.x * ed.x + ed.z * ed.z);
        if (el < 1e-9f) { stop.y = navm_tri_height(m, tend, stop); return stop; }
        ed = arc_v3_scale(ed, 1.0f / el);
        arc_v3 rest = arc_v3_make(to.x - hit.x, 0, to.z - hit.z); float along = rest.x * ed.x + rest.z * ed.z;
        int ts2 = navm_locate(m, stop, NULL); if (ts2 < 0) ts2 = tend;
        p = stop; p.y = navm_tri_height(m, ts2, p); t = ts2;
        if (fabsf(along) < 1e-6f) return p;
        to = arc_v3_make(p.x + ed.x * along, p.y, p.z + ed.z * along);
    }
    return p;
}
void arc_nav_agent_update(arc_nav_agent *a, arc_navmesh h, float dt) {
    navm_mesh *m = navm_get(h); if (!m || !m->built || dt <= 0) return;
    float accel = a->accel > 0 ? a->accel : a->speed * 8;
    arc_v3 desired = arc_v3_make(0, 0, 0);
    if (a->moving && a->ncorners > 0) {
        for (;;) {
            arc_v3 c = a->corners[a->corner]; bool last = a->corner >= a->ncorners - 1;
            float dist = navm_d2(a->pos, c);
            if (!last && dist < fmaxf(a->radius * 0.5f, a->speed * dt)) { a->corner++; continue; }
            if (last && dist <= fmaxf(0.01f, fminf(a->speed * dt, a->arrive > 0 ? a->arrive * 0.05f : 0.05f))) {
                a->pos = arc_v3_make(c.x, a->pos.y, c.z); float y; if (navm_locate(m, a->pos, &y) >= 0) a->pos.y = y;
                if (a->partial) { arc_nav_agent_goto(a, h, a->target); break; }
                a->moving = false; a->arrived = true; a->vel = arc_v3_make(0, 0, 0);
                return;
            }
            float want = a->speed;
            if (last) {
                float stop = a->arrive > 0 ? a->arrive : (a->speed * a->speed) / (2 * accel);
                if (dist < stop && stop > 0) want = fmaxf(a->speed * dist / stop, fminf(a->speed, 0.5f));
            }
            if (dist > 1e-9f) desired = arc_v3_make((c.x - a->pos.x) / dist * want, 0, (c.z - a->pos.z) / dist * want);
            break;
        }
    }
    /* velocity toward the desired one, at most accel * dt of change */
    arc_v3 dv = arc_v3_make(desired.x - a->vel.x, 0, desired.z - a->vel.z); float dl = sqrtf(dv.x * dv.x + dv.z * dv.z), cap = accel * dt;
    if (dl > cap) dv = arc_v3_scale(dv, cap / dl);
    a->vel = arc_v3_make(a->vel.x + dv.x, 0, a->vel.z + dv.z);
    if (a->moving && a->corner >= a->ncorners - 1) {   /* don't overshoot the end */
        arc_v3 c = a->corners[a->ncorners - 1]; float dist = navm_d2(a->pos, c), sp = sqrtf(a->vel.x * a->vel.x + a->vel.z * a->vel.z);
        if (sp * dt > dist && sp > 0) a->vel = arc_v3_scale(a->vel, dist / (sp * dt));
    }
    if (a->vel.x == 0 && a->vel.z == 0) return;
    arc_v3 np = navm_slide(m, a->pos, arc_v3_make(a->vel.x * dt, 0, a->vel.z * dt));
    a->pos = np;
}
void arc_nav_agents_separate(arc_nav_agent *ag, int n, arc_navmesh h) {
    navm_mesh *m = navm_get(h); if (!m || !m->built) return;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
        float want = ag[i].radius + ag[j].radius; if (want <= 0) continue;
        float dx = ag[j].pos.x - ag[i].pos.x, dz = ag[j].pos.z - ag[i].pos.z, d = sqrtf(dx * dx + dz * dz);
        if (d >= want) continue;
        float push = (want - d) * 0.5f;
        if (d < 1e-6f) { dx = 1; dz = 0; d = 1; }   /* on top of each other: apart along x, the same way every run */
        arc_v3 dir = arc_v3_make(dx / d * push, 0, dz / d * push);
        /* an agent that has arrived gives way less than one still moving */
        float wi = ag[i].moving || !ag[j].moving ? 1.0f : 0.25f, wj = ag[j].moving || !ag[i].moving ? 1.0f : 0.25f;
        ag[i].pos = navm_slide(m, ag[i].pos, arc_v3_scale(dir, -wi));
        ag[j].pos = navm_slide(m, ag[j].pos, arc_v3_scale(dir, wj));
    }
}
float arc_nav_agent_remaining(const arc_nav_agent *a) {
    if (!a->moving || a->ncorners == 0) return 0;
    float l = 0; arc_v3 p = a->pos;
    for (int i = a->corner; i < a->ncorners; i++) { l += navm_d2(p, a->corners[i]); p = a->corners[i]; }
    return l;
}
void arc_nav_agent_draw(const arc_nav_agent *a, uint32_t rgb) {
    if (!a->moving) return;
    arc_v3 up = arc_v3_make(0, 0.05f, 0), p = a->pos;
    for (int i = a->corner; i < a->ncorners; i++) { arc_draw_line(arc_v3_add(p, up), arc_v3_add(a->corners[i], up), rgb); p = a->corners[i]; }
}
