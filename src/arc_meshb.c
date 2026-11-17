/* ARC mesh builder: procedural shapes without hand-written winding or normals.
 * Triangles are counter-clockwise seen from outside; normals are flat (one per face) unless the mesh is finished
 * smooth, which averages the normals of faces that meet at a position within the given angle. */
#include "arc_internal.h"

struct arc_mesh_builder {
    arc_vertex *v; int n, cap;
    uint32_t color;                  /* vertex colour (memory order R,G,B,A) */
    arc_m4 xf; bool has_xf;
};

arc_mesh_builder *arc_mb_begin(void) {
    arc_mesh_builder *b = (arc_mesh_builder *)arc__xalloc(sizeof *b);
    memset(b, 0, sizeof *b); b->color = 0xFFFFFFFFu; b->xf = arc_m4_identity();
    return b;
}
void arc_mb_color(arc_mesh_builder *b, uint32_t rgb) { if (b) b->color = arc__to_abgr(rgb); }
void arc_mb_transform(arc_mesh_builder *b, arc_m4 m) { if (b) { b->xf = m; b->has_xf = true; } }
void arc_mb_reset_transform(arc_mesh_builder *b) { if (b) { b->xf = arc_m4_identity(); b->has_xf = false; } }
int  arc_mb_vertex_count(const arc_mesh_builder *b) { return b ? b->n : 0; }

static void mb_push(arc_mesh_builder *b, arc_v3 p, arc_v3 nrm, float u, float v) {
    if (b->n == b->cap) { b->cap = b->cap ? b->cap * 2 : 256; b->v = (arc_vertex *)realloc(b->v, sizeof(arc_vertex) * (size_t)b->cap); if (!b->v) { arc_log("mesh builder: out of memory"); abort(); } }
    arc_vertex *o = &b->v[b->n++];
    o->px = p.x; o->py = p.y; o->pz = p.z; o->nx = nrm.x; o->ny = nrm.y; o->nz = nrm.z; o->color = b->color; o->u = u; o->v = v;
}
/* one triangle with uvs, transformed; degenerate ones are dropped */
static void mb_tri_uv(arc_mesh_builder *b, arc_v3 a, arc_v3 c, arc_v3 d, const float uv[6]) {
    if (b->has_xf) { a = arc_m4_mul_point(b->xf, a); c = arc_m4_mul_point(b->xf, c); d = arc_m4_mul_point(b->xf, d); }
    arc_v3 n = arc_v3_cross(arc_v3_sub(c, a), arc_v3_sub(d, a));
    float l = arc_v3_len(n); if (l < 1e-12f) return;
    n = arc_v3_scale(n, 1.0f / l);
    if (b->has_xf) {   /* a mirroring transform flips the winding: keep triangles facing out */
        const float *m = b->xf.m;
        float det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9]) + m[8] * (m[1] * m[6] - m[2] * m[5]);
        if (det < 0) { arc_v3 t = c; c = d; d = t; n = arc_v3_scale(n, -1);
                       mb_push(b, a, n, uv[0], uv[1]); mb_push(b, c, n, uv[4], uv[5]); mb_push(b, d, n, uv[2], uv[3]); return; }
    }
    mb_push(b, a, n, uv[0], uv[1]); mb_push(b, c, n, uv[2], uv[3]); mb_push(b, d, n, uv[4], uv[5]);
}
void arc_mb_tri(arc_mesh_builder *b, arc_v3 a, arc_v3 c, arc_v3 d) {
    static const float uv[6] = { 0, 1, 1, 1, 0.5f, 0 };
    if (b) mb_tri_uv(b, a, c, d, uv);
}
/* a b c d counter-clockwise seen from the front; uv (0,1) (1,1) (1,0) (0,0) */
void arc_mb_quad(arc_mesh_builder *b, arc_v3 a, arc_v3 c, arc_v3 d, arc_v3 e) {
    if (!b) return;
    static const float t1[6] = { 0, 1, 1, 1, 1, 0 }, t2[6] = { 0, 1, 1, 0, 0, 0 };
    mb_tri_uv(b, a, c, d, t1); mb_tri_uv(b, a, d, e, t2);
}
void arc_mb_box(arc_mesh_builder *b, arc_v3 lo, arc_v3 hi) {
    if (!b) return;
    arc_v3 P[8]; for (int i = 0; i < 8; i++) P[i] = arc_v3_make(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
    static const int F[6][4] = { { 0, 4, 6, 2 }, { 5, 1, 3, 7 }, { 0, 1, 5, 4 }, { 6, 7, 3, 2 }, { 1, 0, 2, 3 }, { 4, 5, 7, 6 } };   /* -x +x -y +y -z +z */
    for (int f = 0; f < 6; f++) arc_mb_quad(b, P[F[f][0]], P[F[f][1]], P[F[f][2]], P[F[f][3]]);
}
/* spin a profile of (radius, height) points round the Y axis. Points go upward for an outward-facing surface.
   segments around; closed bottom/top caps are added where the profile starts/ends off the axis if caps is set. */
void arc_mb_lathe(arc_mesh_builder *b, const arc_v2 *profile, int count, int segments, bool caps) {
    if (!b || !profile || count < 2) return;
    if (segments < 3) segments = 3;
    for (int s = 0; s < segments; s++) {
        float a0 = ARC_TAU * (float)s / (float)segments, a1 = ARC_TAU * ((float)(s + 1)) / (float)segments;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1), u0 = (float)s / (float)segments, u1 = (float)(s + 1) / (float)segments;
        for (int i = 0; i + 1 < count; i++) {
            arc_v2 p = profile[i], q = profile[i + 1];
            float v0 = 1.0f - (float)i / ((float)(count - 1)), v1 = 1.0f - (float)(i + 1) / ((float)(count - 1));
            arc_v3 A = arc_v3_make(p.x * s0, p.y, p.x * c0), B = arc_v3_make(p.x * s1, p.y, p.x * c1);
            arc_v3 C = arc_v3_make(q.x * s1, q.y, q.x * c1), D = arc_v3_make(q.x * s0, q.y, q.x * c0);
            float t1[6] = { u0, v0, u1, v0, u1, v1 }, t2[6] = { u0, v0, u1, v1, u0, v1 };
            mb_tri_uv(b, A, B, C, t1); mb_tri_uv(b, A, C, D, t2);
        }
        if (caps) {
            arc_v2 f = profile[0], l = profile[count - 1];
            static const float tc[6] = { 0.5f, 0.5f, 0, 0, 1, 0 };
            if (f.x > 1e-6f) mb_tri_uv(b, arc_v3_make(0, f.y, 0), arc_v3_make(f.x * s1, f.y, f.x * c1), arc_v3_make(f.x * s0, f.y, f.x * c0), tc);
            if (l.x > 1e-6f) mb_tri_uv(b, arc_v3_make(0, l.y, 0), arc_v3_make(l.x * s0, l.y, l.x * c0), arc_v3_make(l.x * s1, l.y, l.x * c1), tc);
        }
    }
}
/* a flat outline (x, z points; either direction) pulled up from y0 to y1, with caps (convex or star-shaped outlines
   cap correctly: they are fanned from the centre) */
void arc_mb_extrude(arc_mesh_builder *b, const arc_v2 *outline, int count, float y0, float y1, bool caps) {
    if (!b || !outline || count < 3) return;
    float area = 0; arc_v2 c = { 0, 0 };
    for (int i = 0; i < count; i++) { arc_v2 p = outline[i], q = outline[(i + 1) % count]; area += p.x * q.y - q.x * p.y; c.x += p.x; c.y += p.y; }
    c.x /= (float)count; c.y /= (float)count;
    bool ccw = area < 0;   /* counter-clockwise seen from above (+Y), with z toward the viewer */
    float per = 0, at = 0; for (int i = 0; i < count; i++) per += arc_v2_len(arc_v2_sub(outline[(i + 1) % count], outline[i]));
    for (int k = 0; k < count; k++) {
        int i = ccw ? k : count - 1 - k, j = ccw ? (k + 1) % count : (count - 2 - k + count) % count;
        arc_v2 p = outline[i], q = outline[j];
        float u0 = per > 0 ? at / per : 0; at += arc_v2_len(arc_v2_sub(q, p)); float u1 = per > 0 ? at / per : 1;
        arc_v3 A = arc_v3_make(p.x, y0, p.y), B = arc_v3_make(q.x, y0, q.y), C = arc_v3_make(q.x, y1, q.y), D = arc_v3_make(p.x, y1, p.y);
        float t1[6] = { u0, 1, u1, 1, u1, 0 }, t2[6] = { u0, 1, u1, 0, u0, 0 };
        mb_tri_uv(b, A, B, C, t1); mb_tri_uv(b, A, C, D, t2);
        if (caps) {
            static const float tc[6] = { 0.5f, 0.5f, 0, 0, 1, 0 };
            mb_tri_uv(b, arc_v3_make(c.x, y1, c.y), D, C, tc);
            mb_tri_uv(b, arc_v3_make(c.x, y0, c.y), B, A, tc);
        }
    }
}
static const arc_vertex *mb_sort_v;
static long long mb_q(float f) { return (long long)floorf(f * 1e4f + 0.5f); }   /* positions within 0.1 mm are the same point */
static int mb_cmp(const void *x, const void *y) {
    const arc_vertex *a = &mb_sort_v[*(const int *)x], *c = &mb_sort_v[*(const int *)y];
    long long d;
    if ((d = mb_q(a->px) - mb_q(c->px)) != 0) return d < 0 ? -1 : 1;
    if ((d = mb_q(a->py) - mb_q(c->py)) != 0) return d < 0 ? -1 : 1;
    if ((d = mb_q(a->pz) - mb_q(c->pz)) != 0) return d < 0 ? -1 : 1;
    return 0;
}
/* make the mesh. smooth_degrees 0 = flat; otherwise faces meeting at a point within that angle share a normal.
   ARC_MB_OUTWARD: flip any triangle facing the shape's centre (for hand-placed triangles of a closed solid). */
arc_mesh arc_mb_finish(arc_mesh_builder *b, float smooth_degrees, int flags) {
    arc_mesh m = { 0 };
    if (!b) return m;
    if (b->n >= 3) {
        if (flags & ARC_MB_OUTWARD) {
            arc_v3 c = { 0, 0, 0 }; for (int i = 0; i < b->n; i++) c = arc_v3_add(c, arc_v3_make(b->v[i].px, b->v[i].py, b->v[i].pz));
            c = arc_v3_scale(c, 1.0f / (float)b->n);
            for (int t = 0; t + 2 < b->n; t += 3) {
                arc_vertex *v = &b->v[t];
                arc_v3 mid = arc_v3_make((v[0].px + v[1].px + v[2].px) / 3, (v[0].py + v[1].py + v[2].py) / 3, (v[0].pz + v[1].pz + v[2].pz) / 3);
                if (arc_v3_dot(arc_v3_make(v[0].nx, v[0].ny, v[0].nz), arc_v3_sub(mid, c)) < 0) {
                    arc_vertex k = v[1]; v[1] = v[2]; v[2] = k;
                    for (int j = 0; j < 3; j++) { v[j].nx = -v[j].nx; v[j].ny = -v[j].ny; v[j].nz = -v[j].nz; }
                }
            }
        }
        if (smooth_degrees > 0) {   /* average face normals where positions coincide: sort by position, then per group */
            float cosmax = cosf(arc_clampf(smooth_degrees, 0, 180) * ARC_PI / 180.0f);
            int n = b->n; mb_sort_v = b->v;
            int *ord = (int *)arc__xalloc(sizeof(int) * (size_t)n); arc_v3 *face = (arc_v3 *)arc__xalloc(sizeof(arc_v3) * (size_t)n), *out = (arc_v3 *)arc__xalloc(sizeof(arc_v3) * (size_t)n);
            float *wgt = (float *)arc__xalloc(sizeof(float) * (size_t)n);   /* each face counts by its corner angle there */
            for (int i = 0; i < n; i++) {
                ord[i] = i; face[i] = arc_v3_make(b->v[i].nx, b->v[i].ny, b->v[i].nz);
                const arc_vertex *t = &b->v[i - i % 3], *p0 = &b->v[i], *p1 = &t[(i % 3 + 1) % 3], *p2 = &t[(i % 3 + 2) % 3];
                arc_v3 e1 = arc_v3_make(p1->px - p0->px, p1->py - p0->py, p1->pz - p0->pz), e2 = arc_v3_make(p2->px - p0->px, p2->py - p0->py, p2->pz - p0->pz);
                float l = arc_v3_len(e1) * arc_v3_len(e2);
                wgt[i] = l > 1e-12f ? acosf(arc_clampf(arc_v3_dot(e1, e2) / l, -1, 1)) : 0;
            }
            qsort(ord, (size_t)n, sizeof(int), mb_cmp);
            for (int g = 0; g < n;) {
                int e = g + 1; while (e < n && !mb_cmp(&ord[g], &ord[e])) e++;
                for (int a = g; a < e; a++) {
                    arc_v3 acc = { 0, 0, 0 }; int i = ord[a];
                    for (int c = g; c < e; c++) if (arc_v3_dot(face[i], face[ord[c]]) >= cosmax - 1e-6f) acc = arc_v3_add(acc, arc_v3_scale(face[ord[c]], wgt[ord[c]]));
                    out[i] = arc_v3_len(acc) > 1e-9f ? arc_v3_norm(acc) : face[i];
                }
                g = e;
            }
            for (int i = 0; i < n; i++) { b->v[i].nx = out[i].x; b->v[i].ny = out[i].y; b->v[i].nz = out[i].z; }
            free(ord); free(face); free(out); free(wgt);
        }
        m = arc_mesh_create(b->v, b->n, NULL, 0);
    }
    free(b->v); free(b);
    return m;
}
