/* ARC collision: ray tests, a static collision world (triangles in a BVH) and a capsule character controller.
 * Pure CPU code with a fixed order of operations, so results are the same on every platform and usable in tick(). */
#include "arc_internal.h"

/* ---- primitive ray tests ---- */
static void hit_set(arc_hit *h, float t, arc_v3 o, arc_v3 d, arc_v3 n, int tri, uint32_t user) {
    if (!h) return;
    h->hit = true; h->t = t; h->point = arc_v3_madd(o, d, t); h->normal = n; h->tri = tri; h->user = user;
}
bool arc_ray_sphere(arc_v3 o, arc_v3 d, arc_v3 c, float r, float max_t, arc_hit *h) {
    arc_v3 m = arc_v3_sub(o, c);
    float a = arc_v3_dot(d, d), b = arc_v3_dot(m, d), cc = arc_v3_dot(m, m) - r * r;
    if (a < 1e-12f) return false;
    if (cc > 0 && b > 0) return false;
    float disc = b * b - a * cc; if (disc < 0) return false;
    float t = (-b - sqrtf(disc)) / a; if (t < 0) t = 0;   /* inside: hit at the start */
    if (t > max_t) return false;
    arc_v3 p = arc_v3_madd(o, d, t);
    hit_set(h, t, o, d, arc_v3_norm(arc_v3_sub(p, c)), -1, 0);
    return true;
}
bool arc_ray_box(arc_v3 o, arc_v3 d, arc_v3 lo, arc_v3 hi, float max_t, arc_hit *h) {
    float tmin = 0, tmax = max_t; int axis = -1; float sign = 0;
    const float *O = &o.x, *D = &d.x, *L = &lo.x, *H = &hi.x;
    for (int i = 0; i < 3; i++) {
        if (fabsf(D[i]) < 1e-12f) { if (O[i] < L[i] || O[i] > H[i]) return false; continue; }
        float inv = 1.0f / D[i], t0 = (L[i] - O[i]) * inv, t1 = (H[i] - O[i]) * inv, s = -1;
        if (t0 > t1) { float k = t0; t0 = t1; t1 = k; s = 1; }
        if (t0 > tmin) { tmin = t0; axis = i; sign = s; }
        if (t1 < tmax) tmax = t1;
        if (tmin > tmax) return false;
    }
    arc_v3 n = arc_v3_make(0, 0, 0);
    if (axis >= 0) (&n.x)[axis] = sign;
    else n = arc_v3_scale(arc_v3_norm(d), -1);   /* started inside */
    hit_set(h, tmin, o, d, n, -1, 0);
    return true;
}
/* Moller-Trumbore, both sides */
static bool ray_tri(arc_v3 o, arc_v3 d, arc_v3 a, arc_v3 b, arc_v3 c, float max_t, float *t_out) {
    arc_v3 e1 = arc_v3_sub(b, a), e2 = arc_v3_sub(c, a), p = arc_v3_cross(d, e2);
    float det = arc_v3_dot(e1, p);
    if (fabsf(det) < 1e-12f) return false;
    float inv = 1.0f / det; arc_v3 s = arc_v3_sub(o, a);
    float u = arc_v3_dot(s, p) * inv; if (u < 0 || u > 1) return false;
    arc_v3 q = arc_v3_cross(s, e1);
    float v = arc_v3_dot(d, q) * inv; if (v < 0 || u + v > 1) return false;
    float t = arc_v3_dot(e2, q) * inv;
    if (t < 0 || t > max_t) return false;
    *t_out = t; return true;
}
static arc_v3 tri_normal_facing(arc_v3 a, arc_v3 b, arc_v3 c, arc_v3 d) {
    arc_v3 n = arc_v3_norm(arc_v3_cross(arc_v3_sub(b, a), arc_v3_sub(c, a)));
    return arc_v3_dot(n, d) > 0 ? arc_v3_scale(n, -1) : n;
}
bool arc_ray_triangle(arc_v3 o, arc_v3 d, arc_v3 a, arc_v3 b, arc_v3 c, float max_t, arc_hit *h) {
    float t; if (!ray_tri(o, d, a, b, c, max_t, &t)) return false;
    hit_set(h, t, o, d, tri_normal_facing(a, b, c, d), 0, 0);
    return true;
}
/* any mesh, brute force over its triangles (picking a few objects; use a world for level geometry) */
bool arc_ray_mesh(arc_mesh m, arc_m4 model, arc_v3 o, arc_v3 d, float max_t, arc_hit *h) {
    if (!m.id || m.id >= ARC__MAX_MESHES || !arc__meshes[m.id].used) return false;
    const arc__mesh_rec *r = &arc__meshes[m.id];
    /* quick reject on the transformed bounds' sphere */
    arc_v3 cen = arc_m4_mul_point(model, arc_v3_scale(arc_v3_add(r->lo, r->hi), 0.5f));
    float sx = arc_v3_len(arc_v3_make(model.m[0], model.m[1], model.m[2])), sy = arc_v3_len(arc_v3_make(model.m[4], model.m[5], model.m[6])),
          sz = arc_v3_len(arc_v3_make(model.m[8], model.m[9], model.m[10]));
    float rad = 0.5f * arc_v3_len(arc_v3_sub(r->hi, r->lo)) * arc_maxf(sx, arc_maxf(sy, sz));
    if (!arc_ray_sphere(o, d, cen, rad, max_t, NULL)) return false;
    float best = max_t; int bt = -1; arc_v3 ba = { 0, 0, 0 }, bb = ba, bc = ba;
    int tris = r->idx ? r->icount / 3 : r->vcount / 3;
    for (int i = 0; i < tris; i++) {
        int i0 = r->idx ? (int)r->idx[i * 3] : i * 3, i1 = r->idx ? (int)r->idx[i * 3 + 1] : i * 3 + 1, i2 = r->idx ? (int)r->idx[i * 3 + 2] : i * 3 + 2;
        arc_v3 a = arc_m4_mul_point(model, arc_v3_make(r->v[i0].px, r->v[i0].py, r->v[i0].pz));
        arc_v3 b = arc_m4_mul_point(model, arc_v3_make(r->v[i1].px, r->v[i1].py, r->v[i1].pz));
        arc_v3 c = arc_m4_mul_point(model, arc_v3_make(r->v[i2].px, r->v[i2].py, r->v[i2].pz));
        float t; if (ray_tri(o, d, a, b, c, best, &t) && (bt < 0 || t < best)) { best = t; bt = i; ba = a; bb = b; bc = c; }
    }
    if (bt < 0) return false;
    hit_set(h, best, o, d, tri_normal_facing(ba, bb, bc, d), bt, 0);
    return true;
}

/* ---- closest points ---- */
static arc_v3 closest_on_tri(arc_v3 p, arc_v3 a, arc_v3 b, arc_v3 c) {   /* Ericson, Real-Time Collision Detection 5.1.5 */
    arc_v3 ab = arc_v3_sub(b, a), ac = arc_v3_sub(c, a), ap = arc_v3_sub(p, a);
    float d1 = arc_v3_dot(ab, ap), d2 = arc_v3_dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    arc_v3 bp = arc_v3_sub(p, b); float d3 = arc_v3_dot(ab, bp), d4 = arc_v3_dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return arc_v3_madd(a, ab, d1 / (d1 - d3));
    arc_v3 cp = arc_v3_sub(p, c); float d5 = arc_v3_dot(ab, cp), d6 = arc_v3_dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return arc_v3_madd(a, ac, d2 / (d2 - d6));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return arc_v3_madd(b, arc_v3_sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)));
    float den = 1.0f / (va + vb + vc), v = vb * den, w = vc * den;
    return arc_v3_add(a, arc_v3_add(arc_v3_scale(ab, v), arc_v3_scale(ac, w)));
}
static void seg_seg(arc_v3 p1, arc_v3 q1, arc_v3 p2, arc_v3 q2, arc_v3 *c1, arc_v3 *c2) {   /* Ericson 5.1.9 */
    arc_v3 d1 = arc_v3_sub(q1, p1), d2 = arc_v3_sub(q2, p2), r = arc_v3_sub(p1, p2);
    float a = arc_v3_dot(d1, d1), e = arc_v3_dot(d2, d2), f = arc_v3_dot(d2, r), s, t;
    if (a <= 1e-12f && e <= 1e-12f) { *c1 = p1; *c2 = p2; return; }
    if (a <= 1e-12f) { s = 0; t = arc_clampf(f / e, 0, 1); }
    else {
        float c = arc_v3_dot(d1, r);
        if (e <= 1e-12f) { t = 0; s = arc_clampf(-c / a, 0, 1); }
        else {
            float b = arc_v3_dot(d1, d2), den = a * e - b * b;
            s = den != 0 ? arc_clampf((b * f - c * e) / den, 0, 1) : 0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = arc_clampf(-c / a, 0, 1); }
            else if (t > 1) { t = 1; s = arc_clampf((b - c) / a, 0, 1); }
        }
    }
    *c1 = arc_v3_madd(p1, d1, s); *c2 = arc_v3_madd(p2, d2, t);
}
/* closest points between segment p-q and triangle abc; returns squared distance */
static float seg_tri(arc_v3 p, arc_v3 q, arc_v3 a, arc_v3 b, arc_v3 c, arc_v3 *on_seg, arc_v3 *on_tri) {
    arc_v3 d = arc_v3_sub(q, p); float t;
    *on_seg = p; *on_tri = a;
    if (ray_tri(p, d, a, b, c, 1.0f, &t)) { *on_seg = *on_tri = arc_v3_madd(p, d, t); return 0; }
    float best = 1e30f; arc_v3 s1, s2;
    arc_v3 cp = closest_on_tri(p, a, b, c); float dd = arc_v3_dot(arc_v3_sub(p, cp), arc_v3_sub(p, cp));
    if (dd < best) { best = dd; *on_seg = p; *on_tri = cp; }
    cp = closest_on_tri(q, a, b, c); dd = arc_v3_dot(arc_v3_sub(q, cp), arc_v3_sub(q, cp));
    if (dd < best) { best = dd; *on_seg = q; *on_tri = cp; }
    const arc_v3 E[3][2] = { { a, b }, { b, c }, { c, a } };
    for (int i = 0; i < 3; i++) {
        seg_seg(p, q, E[i][0], E[i][1], &s1, &s2);
        dd = arc_v3_dot(arc_v3_sub(s1, s2), arc_v3_sub(s1, s2));
        if (dd < best) { best = dd; *on_seg = s1; *on_tri = s2; }
    }
    return best;
}

/* ---- collision world: triangles in a bounding volume hierarchy ---- */
typedef struct { arc_v3 a, b, c, n; uint32_t user; } cw_tri;
typedef struct { arc_v3 lo, hi; int left, count; } cw_node;   /* leaf: count > 0, tris [left, left+count) */
typedef struct {
    bool used, dirty; uint16_t gen;
    cw_tri *tris; int tri_count, tri_cap;
    cw_node *nodes; int node_count;
    int *order;
} cw_world;
#define ARC__MAX_CWORLDS 64
static cw_world g_cw[ARC__MAX_CWORLDS];

static cw_world *cw_get(arc_cworld w) {
    uint32_t i = w.id & 0xFFFFu, g = w.id >> 16;
    if (!i || i > ARC__MAX_CWORLDS) return NULL;
    cw_world *c = &g_cw[i - 1];
    return c->used && c->gen == g ? c : NULL;
}
arc_cworld arc_cworld_create(void) {
    arc_cworld r = { 0 };
    for (int i = 0; i < ARC__MAX_CWORLDS; i++) if (!g_cw[i].used) {
        uint16_t gen = (uint16_t)(g_cw[i].gen + 1); if (!gen) gen = 1;
        memset(&g_cw[i], 0, sizeof g_cw[i]); g_cw[i].used = true; g_cw[i].gen = gen;
        r.id = (uint32_t)(i + 1) | ((uint32_t)gen << 16); return r;
    }
    arc_log("collision: more than %d worlds", ARC__MAX_CWORLDS);
    return r;
}
void arc_cworld_clear(arc_cworld w) { cw_world *c = cw_get(w); if (c) { c->tri_count = 0; c->dirty = true; } }
void arc_cworld_destroy(arc_cworld w) {
    cw_world *c = cw_get(w); if (!c) return;
    free(c->tris); free(c->nodes); free(c->order);
    uint16_t gen = c->gen; memset(c, 0, sizeof *c); c->gen = gen;
}
int arc_cworld_triangles(arc_cworld w) { cw_world *c = cw_get(w); return c ? c->tri_count : 0; }
static void cw_push(cw_world *c, arc_v3 a, arc_v3 b, arc_v3 d, uint32_t user) {
    arc_v3 n = arc_v3_cross(arc_v3_sub(b, a), arc_v3_sub(d, a));
    if (arc_v3_dot(n, n) < 1e-14f) return;   /* degenerate */
    if (c->tri_count == c->tri_cap) { c->tri_cap = c->tri_cap ? c->tri_cap * 2 : 256; c->tris = (cw_tri *)realloc(c->tris, sizeof(cw_tri) * (size_t)c->tri_cap); if (!c->tris) { arc_log("collision: out of memory"); abort(); } }
    cw_tri *t = &c->tris[c->tri_count++]; t->a = a; t->b = b; t->c = d; t->n = arc_v3_norm(n); t->user = user;
    c->dirty = true;
}
void arc_cworld_add_triangles(arc_cworld w, const arc_v3 *v, int count, uint32_t user) {
    cw_world *c = cw_get(w); if (!c || !v) return;
    for (int i = 0; i + 2 < count; i += 3) cw_push(c, v[i], v[i + 1], v[i + 2], user);
}
void arc_cworld_add_mesh(arc_cworld w, arc_mesh m, arc_m4 model, uint32_t user) {
    cw_world *c = cw_get(w);
    if (!c || !m.id || m.id >= ARC__MAX_MESHES || !arc__meshes[m.id].used) return;
    const arc__mesh_rec *r = &arc__meshes[m.id];
    int tris = r->idx ? r->icount / 3 : r->vcount / 3;
    float *a = model.m; float det = a[0] * (a[5] * a[10] - a[6] * a[9]) - a[4] * (a[1] * a[10] - a[2] * a[9]) + a[8] * (a[1] * a[6] - a[2] * a[5]);
    for (int i = 0; i < tris; i++) {
        int k[3]; for (int j = 0; j < 3; j++) k[j] = r->idx ? (int)r->idx[i * 3 + j] : i * 3 + j;
        if (det < 0) { int s = k[1]; k[1] = k[2]; k[2] = s; }   /* mirrored: keep normals pointing out */
        arc_v3 p[3]; for (int j = 0; j < 3; j++) p[j] = arc_m4_mul_point(model, arc_v3_make(r->v[k[j]].px, r->v[k[j]].py, r->v[k[j]].pz));
        cw_push(c, p[0], p[1], p[2], user);
    }
}
void arc_cworld_add_box(arc_cworld w, arc_v3 lo, arc_v3 hi, uint32_t user) {
    cw_world *c = cw_get(w); if (!c) return;
    arc_v3 P[8]; for (int i = 0; i < 8; i++) P[i] = arc_v3_make(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
    static const int F[6][4] = { { 0, 4, 6, 2 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 2, 3, 1 }, { 4, 5, 7, 6 } };   /* -x +x -y +y -z +z, outward CCW */
    for (int f = 0; f < 6; f++) { cw_push(c, P[F[f][0]], P[F[f][1]], P[F[f][2]], user); cw_push(c, P[F[f][0]], P[F[f][2]], P[F[f][3]], user); }
}

/* build: deterministic merge sort of triangle indices by centroid along the longest axis, split at the median */
static cw_world *g_sort_world; static int g_sort_axis;
static float tri_cen(const cw_world *c, int i, int ax) { const cw_tri *t = &c->tris[i]; return (&t->a.x)[ax] + (&t->b.x)[ax] + (&t->c.x)[ax]; }
static void msort(int *a, int *tmp, int n) {
    if (n < 2) return;
    int h = n / 2; msort(a, tmp, h); msort(a + h, tmp, n - h);
    int i = 0, j = h, k = 0;
    while (i < h && j < n) {
        float ci = tri_cen(g_sort_world, a[i], g_sort_axis), cj = tri_cen(g_sort_world, a[j], g_sort_axis);
        tmp[k++] = (ci < cj || (ci == cj && a[i] < a[j])) ? a[i++] : a[j++];
    }
    while (i < h) tmp[k++] = a[i++];
    while (j < n) tmp[k++] = a[j++];
    memcpy(a, tmp, sizeof(int) * (size_t)n);
}
static void tri_bounds(const cw_tri *t, arc_v3 *lo, arc_v3 *hi) {
    lo->x = arc_minf(t->a.x, arc_minf(t->b.x, t->c.x)); lo->y = arc_minf(t->a.y, arc_minf(t->b.y, t->c.y)); lo->z = arc_minf(t->a.z, arc_minf(t->b.z, t->c.z));
    hi->x = arc_maxf(t->a.x, arc_maxf(t->b.x, t->c.x)); hi->y = arc_maxf(t->a.y, arc_maxf(t->b.y, t->c.y)); hi->z = arc_maxf(t->a.z, arc_maxf(t->b.z, t->c.z));
}
static int build_node(cw_world *c, int first, int n, int *tmp) {
    int id = c->node_count++;
    cw_node *nd = &c->nodes[id];
    arc_v3 lo = { 1e30f, 1e30f, 1e30f }, hi = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < n; i++) {
        arc_v3 a, b; tri_bounds(&c->tris[c->order[first + i]], &a, &b);
        lo.x = arc_minf(lo.x, a.x); lo.y = arc_minf(lo.y, a.y); lo.z = arc_minf(lo.z, a.z);
        hi.x = arc_maxf(hi.x, b.x); hi.y = arc_maxf(hi.y, b.y); hi.z = arc_maxf(hi.z, b.z);
    }
    nd->lo = lo; nd->hi = hi;
    if (n <= 4) { nd->left = first; nd->count = n; return id; }
    arc_v3 ext = arc_v3_sub(hi, lo);
    g_sort_world = c; g_sort_axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
    msort(c->order + first, tmp, n);
    int h = n / 2;
    nd->count = 0;
    int l = build_node(c, first, h, tmp);
    int r = build_node(c, first + h, n - h, tmp);
    c->nodes[id].left = l;             /* inner node: left child index, and count = -(right child index) */
    c->nodes[id].count = -r;
    return id;
}
static void cw_build(cw_world *c) {
    if (!c->dirty) return;
    c->dirty = false;
    free(c->nodes); free(c->order); c->nodes = NULL; c->order = NULL; c->node_count = 0;
    if (!c->tri_count) return;
    c->order = (int *)arc__xalloc(sizeof(int) * (size_t)c->tri_count);
    c->nodes = (cw_node *)arc__xalloc(sizeof(cw_node) * (size_t)(c->tri_count * 2 + 1));
    int *tmp = (int *)arc__xalloc(sizeof(int) * (size_t)c->tri_count);
    for (int i = 0; i < c->tri_count; i++) c->order[i] = i;
    build_node(c, 0, c->tri_count, tmp);
    free(tmp);
}
static bool ray_aabb(arc_v3 o, arc_v3 inv, arc_v3 lo, arc_v3 hi, float max_t) {
    float t0 = 0, t1 = max_t;
    const float *O = &o.x, *I = &inv.x, *L = &lo.x, *H = &hi.x;
    for (int i = 0; i < 3; i++) {
        float a = (L[i] - O[i]) * I[i], b = (H[i] - O[i]) * I[i];
        if (a > b) { float k = a; a = b; b = k; }
        if (a > t0) t0 = a;
        if (b < t1) t1 = b;
        if (t0 > t1) return false;
    }
    return true;
}
bool arc_cworld_raycast(arc_cworld w, arc_v3 o, arc_v3 d, float max_t, arc_hit *h) {
    cw_world *c = cw_get(w); if (!c) return false;
    cw_build(c); if (!c->node_count) return false;
    arc_v3 inv = arc_v3_make(fabsf(d.x) > 1e-12f ? 1.0f / d.x : 1e30f, fabsf(d.y) > 1e-12f ? 1.0f / d.y : 1e30f, fabsf(d.z) > 1e-12f ? 1.0f / d.z : 1e30f);
    int stack[128], sp = 0; stack[sp++] = 0;
    float best = max_t; int bt = -1;
    while (sp) {
        const cw_node *nd = &c->nodes[stack[--sp]];
        if (!ray_aabb(o, inv, nd->lo, nd->hi, best)) continue;
        if (nd->count > 0) {
            for (int i = 0; i < nd->count; i++) {
                int ti = c->order[nd->left + i]; const cw_tri *t = &c->tris[ti]; float tt;
                if (ray_tri(o, d, t->a, t->b, t->c, best, &tt) && (bt < 0 || tt < best || (tt == best && ti < bt))) { best = tt; bt = ti; }
            }
        } else if (sp < 126) { stack[sp++] = -nd->count; stack[sp++] = nd->left; }
    }
    if (bt < 0) return false;
    const cw_tri *t = &c->tris[bt];
    hit_set(h, best, o, d, arc_v3_dot(t->n, d) > 0 ? arc_v3_scale(t->n, -1) : t->n, bt, t->user);
    return true;
}
/* every triangle whose box touches [lo, hi]: callback order is fixed (tree order) */
static int cw_query(cw_world *c, arc_v3 lo, arc_v3 hi, int *out, int max) {
    cw_build(c); if (!c->node_count) return 0;
    int stack[128], sp = 0, n = 0; stack[sp++] = 0;
    while (sp) {
        const cw_node *nd = &c->nodes[stack[--sp]];
        if (nd->hi.x < lo.x || nd->lo.x > hi.x || nd->hi.y < lo.y || nd->lo.y > hi.y || nd->hi.z < lo.z || nd->lo.z > hi.z) continue;
        if (nd->count > 0) { for (int i = 0; i < nd->count && n < max; i++) out[n++] = c->order[nd->left + i]; }
        else if (sp < 126) { stack[sp++] = -nd->count; stack[sp++] = nd->left; }
    }
    return n;
}
/* push a sphere out of the world; returns how many triangles it touched */
int arc_cworld_push_sphere(arc_cworld w, arc_v3 *center, float r) {
    cw_world *c = cw_get(w); if (!c || !center) return 0;
    static int ids[1024]; int touched = 0;
    for (int iter = 0; iter < 4; iter++) {
        arc_v3 p = *center, e = arc_v3_make(r, r, r);
        int n = cw_query(c, arc_v3_sub(p, e), arc_v3_add(p, e), ids, 1024);
        bool moved = false;
        for (int i = 0; i < n; i++) {
            const cw_tri *t = &c->tris[ids[i]];
            arc_v3 q = closest_on_tri(*center, t->a, t->b, t->c), dv = arc_v3_sub(*center, q);
            float dd = arc_v3_dot(dv, dv);
            if (dd >= r * r) continue;
            float dist = sqrtf(dd);
            arc_v3 nrm = dist > 1e-6f ? arc_v3_scale(dv, 1.0f / dist) : t->n;
            *center = arc_v3_madd(*center, nrm, r - dist);
            moved = true; if (iter == 0) touched++;
        }
        if (!moved) break;
    }
    return touched;
}
bool arc_cworld_overlap_sphere(arc_cworld w, arc_v3 center, float r) {
    cw_world *c = cw_get(w); if (!c) return false;
    static int ids[1024];
    arc_v3 e = arc_v3_make(r, r, r);
    int n = cw_query(c, arc_v3_sub(center, e), arc_v3_add(center, e), ids, 1024);
    for (int i = 0; i < n; i++) {
        const cw_tri *t = &c->tris[ids[i]];
        arc_v3 q = closest_on_tri(center, t->a, t->b, t->c), dv = arc_v3_sub(center, q);
        if (arc_v3_dot(dv, dv) < r * r) return true;
    }
    return false;
}

/* ---- capsule character controller ----
   The capsule stands on c->pos (its lowest point). Each move: gravity, then the motion in sub-steps no longer than
   half the radius, pushing out of every triangle the capsule overlaps. Floors (normal within max_slope of up) push
   straight up, so the character doesn't slide down slopes; low edges (below step_height) lift it up stairs; walls
   push sideways and remove the velocity into them, so it slides along. */
/* cos without libm (whose last bits differ between C libraries): the floor/wall decision must be the same everywhere */
static float det_cos(float x) {
    x = fabsf(x); while (x > ARC_PI) x = fabsf(x - ARC_TAU);
    float x2 = x * x, r = 1.0f, t = 1.0f;
    for (int k = 1; k <= 10; k++) { t *= -x2 / (float)((2 * k - 1) * (2 * k)); r += t; }
    return r;
}
static void char_resolve(cw_world *c, arc_character *ch, bool *ground, arc_v3 *gn, bool was_ground) {
    static int ids[1024];
    float r = ch->radius, cosmax = det_cos(ch->max_slope);
    for (int iter = 0; iter < 6; iter++) {
        arc_v3 p0 = arc_v3_make(ch->pos.x, ch->pos.y + r, ch->pos.z), p1 = arc_v3_make(ch->pos.x, ch->pos.y + arc_maxf(r, ch->height - r), ch->pos.z);
        arc_v3 lo = arc_v3_make(p0.x - r, p0.y - r - 0.01f, p0.z - r), hi = arc_v3_make(p1.x + r, p1.y + r, p1.z + r);
        int n = cw_query(c, lo, hi, ids, 1024);
        bool moved = false;
        for (int i = 0; i < n; i++) {
            const cw_tri *t = &c->tris[ids[i]];
            arc_v3 ps = p0, pt = p0;
            float dd = seg_tri(p0, p1, t->a, t->b, t->c, &ps, &pt);
            if (dd >= r * r) continue;
            float dist = sqrtf(dd), pen = r - dist;
            arc_v3 nrm = dist > 1e-6f ? arc_v3_scale(arc_v3_sub(ps, pt), 1.0f / dist) : t->n;
            if (nrm.y >= cosmax) {                             /* floor: straight up */
                ch->pos.y += pen / arc_maxf(nrm.y, 0.2f);
                *ground = true; *gn = nrm;
                if (ch->velocity.y < 0) ch->velocity.y = 0;
            } else if (was_ground && pt.y - ch->pos.y < ch->step_height && nrm.y > -0.1f && pt.y > ch->pos.y) {   /* a step: climb it */
                ch->pos.y += arc_minf(pen / arc_maxf(nrm.y, 0.25f), ch->step_height);
                *ground = true; *gn = arc_v3_make(0, 1, 0);
                if (ch->velocity.y < 0) ch->velocity.y = 0;
            } else {                                           /* wall or ceiling: push out, slide along */
                ch->pos = arc_v3_madd(ch->pos, nrm, pen);
                float vn = arc_v3_dot(ch->velocity, nrm);
                if (vn < 0) ch->velocity = arc_v3_madd(ch->velocity, nrm, -vn);
                if (nrm.y < -0.5f) ch->hit_ceiling = true; else ch->hit_wall = true;
            }
            moved = true;
            p0 = arc_v3_make(ch->pos.x, ch->pos.y + r, ch->pos.z); p1 = arc_v3_make(ch->pos.x, ch->pos.y + arc_maxf(r, ch->height - r), ch->pos.z);
        }
        if (!moved) break;
    }
}
void arc_character_move(arc_cworld w, arc_character *ch, float dt) {
    cw_world *c = cw_get(w); if (!c || !ch) return;
    if (ch->radius <= 0) ch->radius = 0.3f;
    if (ch->height < ch->radius * 2) ch->height = ch->radius * 2;
    if (ch->max_slope <= 0) ch->max_slope = 0.8f;          /* about 46 degrees */
    if (ch->step_height < 0) ch->step_height = 0;
    bool was_ground = ch->on_ground;
    ch->velocity.y -= ch->gravity * dt;
    ch->hit_wall = ch->hit_ceiling = false;
    arc_v3 motion = arc_v3_scale(ch->velocity, dt);
    float len = arc_v3_len(motion);
    int steps = (int)ceilf(len / (ch->radius * 0.5f)); if (steps < 1) steps = 1; if (steps > 64) steps = 64;
    bool ground = false; arc_v3 gn = arc_v3_make(0, 1, 0);
    for (int s = 0; s < steps; s++) {
        ch->pos = arc_v3_add(ch->pos, arc_v3_scale(arc_v3_scale(ch->velocity, dt), 1.0f / (float)steps));
        char_resolve(c, ch, &ground, &gn, was_ground || ground);
    }
    /* stick to the ground going down slopes and steps: probe below the feet */
    if (!ground && was_ground && ch->velocity.y <= 0 && ch->snap_distance >= 0) {
        float probe = ch->snap_distance > 0 ? ch->snap_distance : ch->step_height + 0.05f;
        arc_character t = *ch; t.pos.y -= probe;
        bool g2 = false; arc_v3 n2 = gn;
        char_resolve(c, &t, &g2, &n2, true);
        if (g2 && t.pos.y < ch->pos.y) { ch->pos.y = t.pos.y; ground = true; gn = n2; }
    }
    ch->on_ground = ground; ch->ground_normal = ground ? gn : arc_v3_make(0, 0, 0);
}
