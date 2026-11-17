/* ARC 0.9.4: deterministic rigid-body physics.
   Bodies (sphere, box, capsule; dynamic, static, kinematic) against each other and static ground (planes, triangles).
   Broadphase: sort-and-sweep along x. Narrowphase: closed forms for sphere/capsule pairs, SAT with face clipping for
   box-box, closest points for the rest. Solver: sequential impulses (friction then normal), accumulated and clamped,
   warm-started from the last step's contacts matched by position, Baumgarte position correction with a small slop.
   Determinism: only + - * / and sqrt (no trig), fixed iteration counts, everything in index order, a stable sort. */

#define PH_MAX_WORLDS 8
#define PH_MAX_PTS 4
#define PH_SLOP 0.005f
#define PH_BAUMGARTE 0.2f
#define PH_SLEEP_TIME 0.5f
#define PH_SLEEP_LIN 0.05f       /* m/s */
#define PH_SLEEP_ANG 0.08f       /* rad/s */
/* speculative contacts: pairs closer than this also make contacts (with a negative depth: the gap) so a step can only
   close the gap, not pass it. Set for each pair from the two speeds (0 for overlap queries). */
static float ph_margin = 0, ph_dt_now = 1.0f / 60;

typedef struct {
    bool used;
    uint8_t type, shape; bool awake, lock_rot;
    arc_v3 size, pos; arc_quat q;
    arc_v3 vel, w;
    float inv_mass; arc_v3 inv_i;          /* inverse inertia about the body's own axes */
    float friction, restitution, lin_damp, ang_damp, rolling;
    uint32_t layer, mask, user;
    float sleep_t;
    arc_v3 force, torque;                   /* for the next step */
    bool has_target; arc_v3 target_pos; arc_quat target_q;
    arc_v3 lo, hi;                          /* bounds (for the broadphase) */
} ph_body;
typedef struct { arc_v3 rA, rB; float pen, jn, jt1, jt2; float mn, mt1, mt2, bias; float pending; } ph_point;   /* pending: an approach speed to bounce from when it touches */
typedef struct {
    int a, b;                               /* b = 0: the static ground; then tri/plane says which (key) */
    int sub;                                /* -1 plane index -(i+1), or triangle index, for the ground; 0 between bodies */
    arc_v3 n;                               /* from a to b */
    int np; ph_point p[PH_MAX_PTS];
    arc_v3 t1, t2; float friction, restitution;
    float roll_limit_k; arc_v3 jr;          /* rolling resistance: limit = k * (sum of normal impulses); accumulated angular impulse */
    bool fresh;                             /* not touching in the step before (for events) */
    bool touching;                          /* in contact this step (depth at least -1 mm) */
    float jpre;                             /* impulse spent closing the gap before touching (added to the impact event) */
    int ord;                                /* 0.9.7 its place before sorting: ties keep it, whatever the C library's qsort */
} ph_manifold;
/* 0.9.7 joints: a point (3 rows, solved as one 3 x 3 block), a hinge's axis (2 angular rows), its limits (a lower and an
   upper row, speculative: the gap may close this step, no further) and motor (1 row, impulse capped by torque x dt).
   Accumulated impulses are warm-started; everything is + - * / and sqrt (the angle's atan2 included) */
typedef struct {
    bool used, collide, limit, motor; uint8_t type;
    int a, b;                               /* b = 0: the world */
    arc_v3 la, lb, axa, axb;                /* the pivot and the axis in each body's frame */
    arc_quat rel0;                          /* conj(qA) qB when added: angle 0 */
    float lower, upper, mspeed, mtorque;
    arc_v3 jp, ja; float jlo, jhi, jm;      /* accumulated impulses (point, angular perpendicular to the axis, limits, motor) */
    /* this step */
    arc_v3 rA, rB, cp; float kp[9]; arc_v3 u1, u2, ax; float k2[4], b1, b2, kax, angle;
    /* 0.9.8 ball joints: a cone (the child's axis within `cone` radians of a centre fixed on the base) and twist limits
       (lower / upper: the child's turn about its own axis), both speculative rows like the hinge's limits */
    float cone; arc_v3 cna;                 /* the cone's half angle and its centre in A's frame */
    float jcone, jtlo, jthi;                /* accumulated impulses */
    arc_v3 sc, tw; float ksc, ktw, swing, twist;   /* this step */
} ph_joint;
typedef struct { arc_v3 n; float d; } ph_plane;
typedef struct { arc_v3 a, b, c, n; } ph_tri;
typedef struct {
    bool used;
    arc_v3 gravity; int iterations; bool no_sleep;
    int max_bodies; ph_body *b;             /* b[0] unused: body numbers start at 1 */
    int nbodies_hint;
    ph_plane *planes; int nplanes, cplanes;
    ph_tri *tris; int ntris, ctris;
    float gx, gz, gcell; int gw, gh; int *gstart, *gitem; bool grid_dirty;   /* x/z grid over the triangles */
    ph_manifold *m, *old; int nm, nold, cm; /* this step's contacts and the last step's */
    arc_physics_event *ev; int nev, cev;
    int *order; int *pairs; int npairs, cpairs;
    ph_joint *j; int nj, cj;                /* 0.9.7 joint slots (j[0] unused: joint numbers start at 1) */
} ph_world;
static ph_world g_ph[PH_MAX_WORLDS];
static ph_world *ph_get(arc_physics h) { return h.id > 0 && h.id <= PH_MAX_WORLDS && g_ph[h.id - 1].used ? &g_ph[h.id - 1] : NULL; }
static ph_body *ph_body_get(ph_world *w, int i) { return w && i > 0 && i < w->max_bodies && w->b[i].used ? &w->b[i] : NULL; }

/* ---- small maths (no trig) ---- */
static arc_v3 ph_v3(float x, float y, float z) { return arc_v3_make(x, y, z); }
static arc_v3 ph_vadd(arc_v3 a, arc_v3 b) { return ph_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static arc_v3 ph_vsub(arc_v3 a, arc_v3 b) { return ph_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static arc_v3 ph_vmul(arc_v3 a, float s) { return ph_v3(a.x * s, a.y * s, a.z * s); }
static float ph_vdot(arc_v3 a, arc_v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static arc_v3 ph_vcross(arc_v3 a, arc_v3 b) { return ph_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static float ph_vlen(arc_v3 a) { return sqrtf(ph_vdot(a, a)); }
static arc_v3 ph_vnorm(arc_v3 a) { float l = ph_vlen(a); return l > 1e-12f ? ph_vmul(a, 1.0f / l) : ph_v3(0, 1, 0); }
static arc_quat ph_qnorm(arc_quat q) { float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); if (l < 1e-12f) return (arc_quat){ 0, 0, 0, 1 }; float i = 1.0f / l; return (arc_quat){ q.x * i, q.y * i, q.z * i, q.w * i }; }
static void ph_qmat(arc_quat q, arc_v3 ax[3]) {   /* the body's axes (columns of its rotation) */
    float x = q.x, y = q.y, z = q.z, w = q.w;
    ax[0] = ph_v3(1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w));
    ax[1] = ph_v3(2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w));
    ax[2] = ph_v3(2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y));
}
static arc_v3 ph_to_world(const arc_v3 ax[3], arc_v3 l) { return ph_vadd(ph_vadd(ph_vmul(ax[0], l.x), ph_vmul(ax[1], l.y)), ph_vmul(ax[2], l.z)); }
static arc_v3 ph_to_local(const arc_v3 ax[3], arc_v3 v) { return ph_v3(ph_vdot(ax[0], v), ph_vdot(ax[1], v), ph_vdot(ax[2], v)); }
/* world inverse inertia times v: R diag(inv_i) R^T v */
static arc_v3 ph_inv_i_mul(const ph_body *b, arc_v3 v) {
    if (b->inv_mass == 0 || b->lock_rot) return ph_v3(0, 0, 0);
    arc_v3 ax[3]; ph_qmat(b->q, ax);
    arc_v3 l = ph_to_local(ax, v); l = ph_v3(l.x * b->inv_i.x, l.y * b->inv_i.y, l.z * b->inv_i.z);
    return ph_to_world(ax, l);
}
static void ph_tangents(arc_v3 n, arc_v3 *t1, arc_v3 *t2) {
    *t1 = fabsf(n.x) < 0.57735f ? ph_vnorm(ph_v3(0, n.z, -n.y)) : ph_vnorm(ph_v3(n.y, -n.x, 0));
    *t2 = ph_vcross(n, *t1);
}
static void ph_capsule_seg(const ph_body *b, arc_v3 *p0, arc_v3 *p1) {
    arc_v3 ax[3]; ph_qmat(b->q, ax);
    arc_v3 h = ph_vmul(ax[1], b->size.y); *p0 = ph_vsub(b->pos, h); *p1 = ph_vadd(b->pos, h);
}
static void ph_body_bounds(ph_body *b) {
    arc_v3 e;
    if (b->shape == ARC_SHAPE_SPHERE) e = ph_v3(b->size.x, b->size.x, b->size.x);
    else if (b->shape == ARC_SHAPE_CAPSULE) {
        arc_v3 ax[3]; ph_qmat(b->q, ax); float r = b->size.x, h = b->size.y;
        e = ph_v3(fabsf(ax[1].x) * h + r, fabsf(ax[1].y) * h + r, fabsf(ax[1].z) * h + r);
    } else {
        arc_v3 ax[3]; ph_qmat(b->q, ax); arc_v3 s = b->size;
        e = ph_v3(fabsf(ax[0].x) * s.x + fabsf(ax[1].x) * s.y + fabsf(ax[2].x) * s.z,
               fabsf(ax[0].y) * s.x + fabsf(ax[1].y) * s.y + fabsf(ax[2].y) * s.z,
               fabsf(ax[0].z) * s.x + fabsf(ax[1].z) * s.y + fabsf(ax[2].z) * s.z);
    }
    float m = 0.02f + (b->type == ARC_BODY_STATIC ? 0 : (ph_vlen(b->vel) + ph_vlen(b->w) * fmaxf(b->size.x, fmaxf(b->size.y, b->size.z))) * ph_dt_now);   /* found before touching */
    b->lo = ph_vsub(b->pos, ph_vadd(e, ph_v3(m, m, m))); b->hi = ph_vadd(b->pos, ph_vadd(e, ph_v3(m, m, m)));
}

/* 0.9.7 atan2 from + - * / only (|error| < 1e-5 rad), so hinge angles are the same on every machine */
static float ph_atan2(float y, float x) {
    float ax = fabsf(x), ay = fabsf(y), mx = fmaxf(ax, ay), mn = fminf(ax, ay);
    if (mx == 0) return 0;
    float a = mn / mx, s2 = a * a, r = ((-0.0464964749f * s2 + 0.15931422f) * s2 - 0.327622764f) * s2 * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = 3.14159274f - r;
    return y < 0 ? -r : r;
}
static arc_quat ph_qconj(arc_quat q) { return (arc_quat){ -q.x, -q.y, -q.z, q.w }; }
/* ---- world ---- */
arc_physics arc_physics_create(const arc_physics_desc *d) {
    for (int i = 0; i < PH_MAX_WORLDS; i++) if (!g_ph[i].used) {
        ph_world *w = &g_ph[i]; memset(w, 0, sizeof *w); w->used = true;
        arc_physics_desc dd = d ? *d : (arc_physics_desc){ 0 };
        w->gravity = dd.no_gravity ? ph_v3(0, 0, 0) : (dd.gravity.x == 0 && dd.gravity.y == 0 && dd.gravity.z == 0 ? ph_v3(0, -9.81f, 0) : dd.gravity);
        w->iterations = dd.iterations > 0 ? dd.iterations : 10;
        w->no_sleep = dd.no_sleep;
        w->max_bodies = (dd.max_bodies > 0 ? dd.max_bodies : 1024) + 1;
        w->b = (ph_body *)calloc((size_t)w->max_bodies, sizeof(ph_body));
        w->order = (int *)malloc(sizeof(int) * (size_t)w->max_bodies);
        return (arc_physics){ (uint32_t)i + 1 };
    }
    arc_log("arc_physics_create: more than %d worlds", PH_MAX_WORLDS);
    return (arc_physics){ 0 };
}
void arc_physics_destroy(arc_physics h) {
    ph_world *w = ph_get(h); if (!w) return;
    free(w->b); free(w->planes); free(w->tris); free(w->gstart); free(w->gitem); free(w->m); free(w->old); free(w->ev); free(w->order); free(w->pairs); free(w->j);
    memset(w, 0, sizeof *w);
}
int arc_physics_add(arc_physics h, const arc_body_desc *d) {
    ph_world *w = ph_get(h); if (!w || !d) return 0;
    int i = 1; while (i < w->max_bodies && w->b[i].used) i++;
    if (i >= w->max_bodies) { arc_log("arc_physics_add: the world is full (%d bodies)", w->max_bodies - 1); return 0; }
    ph_body *b = &w->b[i]; memset(b, 0, sizeof *b);
    b->used = true; b->type = (uint8_t)d->type; b->shape = (uint8_t)d->shape; b->awake = true; b->lock_rot = d->lock_rotation;
    b->size = d->size;
    if (b->shape == ARC_SHAPE_SPHERE) b->size.x = fmaxf(b->size.x, 1e-3f);
    if (b->shape == ARC_SHAPE_CAPSULE) { b->size.x = fmaxf(b->size.x, 1e-3f); b->size.y = fmaxf(b->size.y, 0); }
    if (b->shape == ARC_SHAPE_BOX) b->size = ph_v3(fmaxf(b->size.x, 1e-3f), fmaxf(b->size.y, 1e-3f), fmaxf(b->size.z, 1e-3f));
    b->pos = d->pos;
    b->q = (d->rot.x == 0 && d->rot.y == 0 && d->rot.z == 0 && d->rot.w == 0) ? (arc_quat){ 0, 0, 0, 1 } : ph_qnorm(d->rot);
    b->vel = d->vel; b->w = d->ang_vel;
    b->friction = d->friction > 0 ? d->friction : 0.5f; b->restitution = d->restitution;
    b->lin_damp = d->linear_damping > 0 ? d->linear_damping : 0.02f; b->ang_damp = d->angular_damping > 0 ? d->angular_damping : 0.05f;
    b->rolling = d->rolling_friction < 0 ? 0 : d->rolling_friction > 0 ? d->rolling_friction : 0.04f;
    b->layer = d->layer ? d->layer : 1; b->mask = d->mask ? d->mask : 0xFFFFFFFFu; b->user = d->user;
    if (b->type == ARC_BODY_DYNAMIC) {
        float r = b->size.x, vol, m; arc_v3 I;
        if (b->shape == ARC_SHAPE_SPHERE) { vol = 4.18879f * r * r * r; }
        else if (b->shape == ARC_SHAPE_BOX) { vol = 8 * b->size.x * b->size.y * b->size.z; }
        else { vol = 4.18879f * r * r * r + 3.14159265f * r * r * 2 * b->size.y; }
        m = d->mass > 0 ? d->mass : vol * 1000;
        if (b->shape == ARC_SHAPE_SPHERE) { float k = 0.4f * m * r * r; I = ph_v3(k, k, k); }
        else if (b->shape == ARC_SHAPE_BOX) {
            float x = 2 * b->size.x, y = 2 * b->size.y, z = 2 * b->size.z;
            I = ph_v3(m * (y * y + z * z) / 12, m * (x * x + z * z) / 12, m * (x * x + y * y) / 12);
        } else {   /* a cylinder of length 2h plus a sphere, a fair approximation */
            float hh = 2 * b->size.y, iy = 0.5f * m * r * r, ix = m * (3 * r * r + hh * hh) / 12 + 0.4f * m * r * r * 0.5f;
            I = ph_v3(ix, iy, ix);
        }
        b->inv_mass = 1.0f / m; b->inv_i = ph_v3(1.0f / I.x, 1.0f / I.y, 1.0f / I.z);
    }
    ph_body_bounds(b);
    return i;
}
void arc_physics_remove(arc_physics h, int i) {
    ph_world *w = ph_get(h); ph_body *b = ph_body_get(w, i); if (!b) return;
    memset(b, 0, sizeof *b);
    for (int k = 0; k < w->nold; k++) if (w->old[k].a == i || w->old[k].b == i) w->old[k].np = 0;   /* forget its contacts */
    for (int k = 1; k < w->nj; k++) if (w->j[k].used && (w->j[k].a == i || w->j[k].b == i)) memset(&w->j[k], 0, sizeof w->j[k]);   /* and its joints */
}
void arc_physics_add_plane(arc_physics h, arc_v3 n, float d) {
    ph_world *w = ph_get(h); if (!w) return;
    if (w->nplanes >= w->cplanes) { w->cplanes = w->cplanes ? w->cplanes * 2 : 4; w->planes = (ph_plane *)realloc(w->planes, sizeof(ph_plane) * (size_t)w->cplanes); }
    float l = ph_vlen(n); if (l < 1e-9f) return;
    w->planes[w->nplanes++] = (ph_plane){ ph_vmul(n, 1.0f / l), d / l };
}
void arc_physics_add_triangles(arc_physics h, const arc_v3 *v, int count) {
    ph_world *w = ph_get(h); if (!w || !v) return;
    for (int i = 0; i + 2 < count; i += 3) {
        arc_v3 n = ph_vcross(ph_vsub(v[i + 1], v[i]), ph_vsub(v[i + 2], v[i])); float l = ph_vlen(n);
        if (l < 1e-12f) continue;
        if (w->ntris >= w->ctris) { w->ctris = w->ctris ? w->ctris * 2 : 256; w->tris = (ph_tri *)realloc(w->tris, sizeof(ph_tri) * (size_t)w->ctris); }
        w->tris[w->ntris++] = (ph_tri){ v[i], v[i + 1], v[i + 2], ph_vmul(n, 1.0f / l) };
    }
    w->grid_dirty = true;
}
void arc_physics_add_mesh(arc_physics h, arc_mesh mesh, arc_m4 model) {
    ph_world *w = ph_get(h);
    if (!w || !mesh.id || mesh.id >= ARC__MAX_MESHES || !arc__meshes[mesh.id].used || !arc__meshes[mesh.id].v) return;
    const arc__mesh_rec *r = &arc__meshes[mesh.id];
    int tris = r->idx ? r->icount / 3 : r->vcount / 3;
    float *a = model.m; float det = a[0] * (a[5] * a[10] - a[6] * a[9]) - a[4] * (a[1] * a[10] - a[2] * a[9]) + a[8] * (a[1] * a[6] - a[2] * a[5]);
    for (int i = 0; i < tris; i++) {
        int k[3]; for (int j = 0; j < 3; j++) k[j] = r->idx ? (int)r->idx[i * 3 + j] : i * 3 + j;
        if (det < 0) { int s = k[1]; k[1] = k[2]; k[2] = s; }
        arc_v3 p[3]; for (int j = 0; j < 3; j++) p[j] = arc_m4_mul_point(model, ph_v3(r->v[k[j]].px, r->v[k[j]].py, r->v[k[j]].pz));
        arc_physics_add_triangles(h, p, 3);
    }
}
static void ph_build_grid(ph_world *w) {
    free(w->gstart); free(w->gitem); w->gstart = w->gitem = NULL; w->grid_dirty = false;
    if (!w->ntris) return;
    float x0 = 1e30f, z0 = 1e30f, x1 = -1e30f, z1 = -1e30f, area = 0;
    for (int i = 0; i < w->ntris; i++) {
        ph_tri *t = &w->tris[i];
        x0 = fminf(x0, fminf(t->a.x, fminf(t->b.x, t->c.x))); x1 = fmaxf(x1, fmaxf(t->a.x, fmaxf(t->b.x, t->c.x)));
        z0 = fminf(z0, fminf(t->a.z, fminf(t->b.z, t->c.z))); z1 = fmaxf(z1, fmaxf(t->a.z, fmaxf(t->b.z, t->c.z)));
        area += 0.5f * ph_vlen(ph_vcross(ph_vsub(t->b, t->a), ph_vsub(t->c, t->a)));
    }
    w->gcell = fmaxf(sqrtf(area / (float)w->ntris) * 2, 0.5f);
    w->gx = x0; w->gz = z0;
    for (;;) { w->gw = (int)((x1 - x0) / w->gcell) + 1; w->gh = (int)((z1 - z0) / w->gcell) + 1; if ((int64_t)w->gw * w->gh <= 4 * (int64_t)w->ntris + 1024) break; w->gcell *= 1.5f; }
    int nc = w->gw * w->gh;
    w->gstart = (int *)calloc((size_t)nc + 1, sizeof(int));
    for (int pass = 0; pass < 2; pass++) {
        int *fill = pass ? (int *)calloc((size_t)nc, sizeof(int)) : NULL;
        for (int i = 0; i < w->ntris; i++) {
            ph_tri *t = &w->tris[i];
            int cx0 = (int)((fminf(t->a.x, fminf(t->b.x, t->c.x)) - w->gx) / w->gcell), cx1 = (int)((fmaxf(t->a.x, fmaxf(t->b.x, t->c.x)) - w->gx) / w->gcell);
            int cz0 = (int)((fminf(t->a.z, fminf(t->b.z, t->c.z)) - w->gz) / w->gcell), cz1 = (int)((fmaxf(t->a.z, fmaxf(t->b.z, t->c.z)) - w->gz) / w->gcell);
            for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) { int c = cz * w->gw + cx; if (!pass) w->gstart[c + 1]++; else w->gitem[w->gstart[c] + fill[c]++] = i; }
        }
        if (!pass) { for (int c = 0; c < nc; c++) w->gstart[c + 1] += w->gstart[c]; w->gitem = (int *)malloc(sizeof(int) * (size_t)(w->gstart[nc] + 1)); }
        free(fill);
    }
}

/* ---- contact generation ---- */
typedef struct { arc_v3 n; int np; arc_v3 p[8]; float pen[8]; } ph_contact;   /* n from a to b; p = a point between the surfaces */
static void ph_cadd(ph_contact *c, arc_v3 p, float pen) { if (c->np < 8) { c->p[c->np] = p; c->pen[c->np] = pen; c->np++; } }
static arc_v3 ph_closest_on_seg(arc_v3 p, arc_v3 a, arc_v3 b) {
    arc_v3 ab = ph_vsub(b, a); float l2 = ph_vdot(ab, ab); if (l2 < 1e-12f) return a;
    float t = arc_clampf(ph_vdot(ph_vsub(p, a), ab) / l2, 0, 1); return ph_vadd(a, ph_vmul(ab, t));
}
static void ph_closest_seg_seg(arc_v3 p1, arc_v3 q1, arc_v3 p2, arc_v3 q2, arc_v3 *c1, arc_v3 *c2) {   /* Ericson 5.1.9 */
    arc_v3 d1 = ph_vsub(q1, p1), d2 = ph_vsub(q2, p2), r = ph_vsub(p1, p2);
    float a = ph_vdot(d1, d1), e = ph_vdot(d2, d2), f = ph_vdot(d2, r), s, t;
    if (a <= 1e-12f && e <= 1e-12f) { *c1 = p1; *c2 = p2; return; }
    if (a <= 1e-12f) { s = 0; t = arc_clampf(f / e, 0, 1); }
    else {
        float c = ph_vdot(d1, r);
        if (e <= 1e-12f) { t = 0; s = arc_clampf(-c / a, 0, 1); }
        else {
            float b = ph_vdot(d1, d2), den = a * e - b * b;
            s = den > 1e-12f ? arc_clampf((b * f - c * e) / den, 0, 1) : 0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = arc_clampf(-c / a, 0, 1); } else if (t > 1) { t = 1; s = arc_clampf((b - c) / a, 0, 1); }
        }
    }
    *c1 = ph_vadd(p1, ph_vmul(d1, s)); *c2 = ph_vadd(p2, ph_vmul(d2, t));
}
static arc_v3 ph_closest_on_tri(arc_v3 p, arc_v3 a, arc_v3 b, arc_v3 c) {   /* Ericson 5.1.5 */
    arc_v3 ab = ph_vsub(b, a), ac = ph_vsub(c, a), ap = ph_vsub(p, a);
    float d1 = ph_vdot(ab, ap), d2 = ph_vdot(ac, ap); if (d1 <= 0 && d2 <= 0) return a;
    arc_v3 bp = ph_vsub(p, b); float d3 = ph_vdot(ab, bp), d4 = ph_vdot(ac, bp); if (d3 >= 0 && d4 <= d3) return b;
    float vc = d1 * d4 - d3 * d2; if (vc <= 0 && d1 >= 0 && d3 <= 0) return ph_vadd(a, ph_vmul(ab, d1 / (d1 - d3)));
    arc_v3 cp = ph_vsub(p, c); float d5 = ph_vdot(ab, cp), d6 = ph_vdot(ac, cp); if (d6 >= 0 && d5 <= d6) return c;
    float vb = d5 * d2 - d1 * d6; if (vb <= 0 && d2 >= 0 && d6 <= 0) return ph_vadd(a, ph_vmul(ac, d2 / (d2 - d6)));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return ph_vadd(b, ph_vmul(ph_vsub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    float den = 1.0f / (va + vb + vc); return ph_vadd(a, ph_vadd(ph_vmul(ab, vb * den), ph_vmul(ac, vc * den)));
}
/* sphere at pa (radius ra) against sphere at pb: contact from a to b */
static bool ph_sphere_sphere(arc_v3 pa, float ra, arc_v3 pb, float rb, ph_contact *c) {
    arc_v3 d = ph_vsub(pb, pa); float l = ph_vlen(d), r = ra + rb;
    if (l >= r + ph_margin) return false;
    c->n = l > 1e-9f ? ph_vmul(d, 1.0f / l) : ph_v3(0, 1, 0);
    ph_cadd(c, ph_vadd(pa, ph_vmul(c->n, ra - (r - l) * 0.5f)), r - l);
    return true;
}
/* the closest point on box b (centre, axes, half sizes) to p; *inside when p is inside */
static arc_v3 ph_box_closest(arc_v3 bc, const arc_v3 ax[3], arc_v3 h, arc_v3 p, bool *inside) {
    arc_v3 l = ph_to_local(ax, ph_vsub(p, bc));
    *inside = fabsf(l.x) <= h.x && fabsf(l.y) <= h.y && fabsf(l.z) <= h.z;
    arc_v3 q = ph_v3(arc_clampf(l.x, -h.x, h.x), arc_clampf(l.y, -h.y, h.y), arc_clampf(l.z, -h.z, h.z));
    return ph_vadd(bc, ph_to_world(ax, q));
}
/* sphere (pa, r) against a box: contact from the sphere to the box */
static bool ph_sphere_box(arc_v3 pa, float r, arc_v3 bc, const arc_v3 ax[3], arc_v3 h, ph_contact *c) {
    bool in; arc_v3 q = ph_box_closest(bc, ax, h, pa, &in);
    if (!in) {
        arc_v3 d = ph_vsub(q, pa); float l = ph_vlen(d); if (l >= r + ph_margin) return false;
        c->n = l > 1e-9f ? ph_vmul(d, 1.0f / l) : ph_v3(0, -1, 0);
        ph_cadd(c, ph_vadd(pa, ph_vmul(c->n, r - (r - l) * 0.5f)), r - l);
        return true;
    }
    /* the centre inside: out through the nearest face */
    arc_v3 l = ph_to_local(ax, ph_vsub(pa, bc)); float best = 1e30f; int k = 0; float sg = 1;
    float dd[3] = { h.x - fabsf(l.x), h.y - fabsf(l.y), h.z - fabsf(l.z) }, ll[3] = { l.x, l.y, l.z };
    for (int i = 0; i < 3; i++) if (dd[i] < best) { best = dd[i]; k = i; sg = ll[i] < 0 ? -1.0f : 1.0f; }
    c->n = ph_vmul(ax[k], -sg);   /* from the sphere toward the box's inside */
    ph_cadd(c, pa, best + r);
    return true;
}
/* box-box: SAT over 15 axes, face clipping or an edge pair. Contact from a to b. */
static int ph_clip_poly(const arc_v3 *in, int n, arc_v3 pn, float pd, arc_v3 *out) {   /* keep dot(pn, p) <= pd */
    int m = 0;
    for (int i = 0; i < n; i++) {
        arc_v3 a = in[i], b = in[(i + 1) % n]; float da = ph_vdot(pn, a) - pd, db = ph_vdot(pn, b) - pd;
        if (da <= 0) out[m++] = a;
        if ((da < 0) != (db < 0) && da != db) { float t = da / (da - db); out[m++] = ph_vadd(a, ph_vmul(ph_vsub(b, a), t)); }
    }
    return m;
}
static bool ph_box_box(arc_v3 ca, const arc_v3 A[3], arc_v3 ha, arc_v3 cb, const arc_v3 B[3], arc_v3 hb, ph_contact *c) {
    arc_v3 d = ph_vsub(cb, ca); float a[3] = { ha.x, ha.y, ha.z }, b[3] = { hb.x, hb.y, hb.z };
    float best = 1e30f; int kind = -1, ia = 0, ib = 0; arc_v3 bn = ph_v3(0, 1, 0);
    for (int k = 0; k < 15; k++) {
        arc_v3 L; int i = 0, j = 0;
        if (k < 3) L = A[k]; else if (k < 6) L = B[k - 3];
        else { i = (k - 6) / 3; j = (k - 6) % 3; L = ph_vcross(A[i], B[j]); float l = ph_vlen(L); if (l < 1e-5f) continue; L = ph_vmul(L, 1.0f / l); }
        float ra = a[0] * fabsf(ph_vdot(A[0], L)) + a[1] * fabsf(ph_vdot(A[1], L)) + a[2] * fabsf(ph_vdot(A[2], L));
        float rb = b[0] * fabsf(ph_vdot(B[0], L)) + b[1] * fabsf(ph_vdot(B[1], L)) + b[2] * fabsf(ph_vdot(B[2], L));
        float dist = ph_vdot(d, L), pen = ra + rb - fabsf(dist);
        if (pen < -ph_margin) return false;
        float score = k < 6 ? pen : pen * 1.05f + 0.001f;   /* prefer faces: steadier contacts */
        if (score < best) { best = score; kind = k; ia = k < 3 ? k : i; ib = k < 3 ? 0 : (k < 6 ? k - 3 : j); bn = dist < 0 ? ph_vmul(L, -1) : L; }
    }
    if (kind < 0) return false;
    c->n = bn;
    if (kind < 6) {
        /* reference face on R, incident face on I; n points from R to I */
        bool ref_a = kind < 3;
        const arc_v3 *R = ref_a ? A : B, *I = ref_a ? B : A; arc_v3 cr = ref_a ? ca : cb, ci = ref_a ? cb : ca;
        const float *hr = ref_a ? a : b, *hi = ref_a ? b : a; int ri = ref_a ? ia : ib;
        arc_v3 n = ref_a ? bn : ph_vmul(bn, -1);
        arc_v3 rn = ph_vdot(R[ri], n) > 0 ? R[ri] : ph_vmul(R[ri], -1);
        arc_v3 rc = ph_vadd(cr, ph_vmul(rn, hr[ri]));
        int ii = 0; float most = -1;
        for (int j = 0; j < 3; j++) { float v = fabsf(ph_vdot(I[j], n)); if (v > most) { most = v; ii = j; } }
        arc_v3 in_n = ph_vdot(I[ii], n) > 0 ? ph_vmul(I[ii], -1) : I[ii];
        arc_v3 ic = ph_vadd(ci, ph_vmul(in_n, hi[ii]));
        int u = (ii + 1) % 3, v = (ii + 2) % 3;
        arc_v3 eu = ph_vmul(I[u], hi[u]), ev = ph_vmul(I[v], hi[v]);
        arc_v3 poly[16], tmp[16];
        poly[0] = ph_vadd(ph_vadd(ic, eu), ev); poly[1] = ph_vadd(ph_vsub(ic, eu), ev); poly[2] = ph_vsub(ph_vsub(ic, eu), ev); poly[3] = ph_vsub(ph_vadd(ic, eu), ev);
        int np = 4;
        int ru = (ri + 1) % 3, rv = (ri + 2) % 3;
        np = ph_clip_poly(poly, np, R[ru], ph_vdot(R[ru], cr) + hr[ru], tmp);
        np = ph_clip_poly(tmp, np, ph_vmul(R[ru], -1), -ph_vdot(R[ru], cr) + hr[ru], poly);
        np = ph_clip_poly(poly, np, R[rv], ph_vdot(R[rv], cr) + hr[rv], tmp);
        np = ph_clip_poly(tmp, np, ph_vmul(R[rv], -1), -ph_vdot(R[rv], cr) + hr[rv], poly);
        for (int k = 0; k < np; k++) {
            float s = ph_vdot(ph_vsub(poly[k], rc), rn);
            if (s <= ph_margin) ph_cadd(c, ph_vsub(poly[k], ph_vmul(rn, s * 0.5f)), -s);
        }
        if (c->np == 0) return false;
    } else {
        /* edge-edge: the edges of A and B closest along n */
        arc_v3 pa = ca, pb = cb;
        for (int k = 0; k < 3; k++) {
            if (k != ia) pa = ph_vadd(pa, ph_vmul(A[k], a[k] * (ph_vdot(A[k], bn) > 0 ? 1.0f : -1.0f)));
            if (k != ib) pb = ph_vadd(pb, ph_vmul(B[k], b[k] * (ph_vdot(B[k], bn) < 0 ? 1.0f : -1.0f)));
        }
        arc_v3 c1, c2;
        ph_closest_seg_seg(ph_vsub(pa, ph_vmul(A[ia], a[ia])), ph_vadd(pa, ph_vmul(A[ia], a[ia])), ph_vsub(pb, ph_vmul(B[ib], b[ib])), ph_vadd(pb, ph_vmul(B[ib], b[ib])), &c1, &c2);
        ph_cadd(c, ph_vmul(ph_vadd(c1, c2), 0.5f), ph_vdot(ph_vsub(c1, c2), bn));   /* c1 is past c2 along n: the depth (negative = a gap) */
    }
    return true;
}
/* capsule segment vs box: the segment point nearest the box, by alternating closest points (convex: converges) */
static bool ph_seg_box(arc_v3 p0, arc_v3 p1, float r, arc_v3 bc, const arc_v3 ax[3], arc_v3 h, ph_contact *c) {
    bool any = false;
    ph_contact e0 = { 0 }, e1 = { 0 };
    bool t0 = ph_sphere_box(p0, r, bc, ax, h, &e0), t1 = ph_sphere_box(p1, r, bc, ax, h, &e1);
    if (t0) { c->n = e0.n; ph_cadd(c, e0.p[0], e0.pen[0]); any = true; }
    if (t1) { if (!any) c->n = e1.n; else c->n = ph_vnorm(ph_vadd(c->n, e1.n)); ph_cadd(c, e1.p[0], e1.pen[0]); any = true; }
    if (any) return true;
    arc_v3 s = ph_closest_on_seg(bc, p0, p1);
    for (int it = 0; it < 6; it++) { bool in; arc_v3 q = ph_box_closest(bc, ax, h, s, &in); if (in) break; s = ph_closest_on_seg(q, p0, p1); }
    ph_contact m = { 0 };
    if (!ph_sphere_box(s, r, bc, ax, h, &m)) return false;
    c->n = m.n; ph_cadd(c, m.p[0], m.pen[0]);
    return true;
}
/* shape vs plane (n, d): contacts from the shape to the plane (n points out of the plane, so the contact normal is -n) */
static bool ph_shape_plane(const ph_body *s, arc_v3 pn, float pd, ph_contact *c) {
    c->n = ph_vmul(pn, -1);
    if (s->shape == ARC_SHAPE_SPHERE) {
        float dist = ph_vdot(pn, s->pos) - pd, pen = s->size.x - dist;
        if (pen <= -ph_margin) return false;
        ph_cadd(c, ph_vsub(s->pos, ph_vmul(pn, dist + pen * 0.5f - pen)), pen); c->p[c->np - 1] = ph_vsub(s->pos, ph_vmul(pn, s->size.x - pen * 0.5f));
        return true;
    }
    if (s->shape == ARC_SHAPE_CAPSULE) {
        arc_v3 e[2]; ph_capsule_seg(s, &e[0], &e[1]);
        for (int i = 0; i < 2; i++) { float pen = s->size.x - (ph_vdot(pn, e[i]) - pd); if (pen > -ph_margin) ph_cadd(c, ph_vsub(e[i], ph_vmul(pn, s->size.x - pen * 0.5f)), pen); }
        return c->np > 0;
    }
    arc_v3 ax[3]; ph_qmat(s->q, ax);
    for (int i = 0; i < 8; i++) {
        arc_v3 p = ph_vadd(s->pos, ph_to_world(ax, ph_v3(i & 1 ? s->size.x : -s->size.x, i & 2 ? s->size.y : -s->size.y, i & 4 ? s->size.z : -s->size.z)));
        float pen = pd - ph_vdot(pn, p);
        if (pen > -ph_margin) ph_cadd(c, ph_vadd(p, ph_vmul(pn, pen * 0.5f)), pen);
    }
    return c->np > 0;
}
/* shape vs triangle (one-sided: bodies stay on its front) */
static bool ph_shape_tri(const ph_body *s, const ph_tri *t, ph_contact *c) {
    if (s->shape == ARC_SHAPE_SPHERE || s->shape == ARC_SHAPE_CAPSULE) {
        float r = s->size.x;
        arc_v3 pts[2]; int n = 1; if (s->shape == ARC_SHAPE_SPHERE) pts[0] = s->pos; else { ph_capsule_seg(s, &pts[0], &pts[1]); n = 2; }
        bool any = false;
        for (int i = 0; i < n; i++) {
            arc_v3 q = ph_closest_on_tri(pts[i], t->a, t->b, t->c), d = ph_vsub(q, pts[i]); float l = ph_vlen(d);
            if (l >= r + ph_margin || ph_vdot(ph_vsub(pts[i], t->a), t->n) < -r) continue;   /* behind the triangle: not ours */
            arc_v3 n2 = l > 1e-6f ? ph_vmul(d, 1.0f / l) : ph_vmul(t->n, -1);
            if (ph_vdot(n2, t->n) > 0) continue;
            if (!any) c->n = n2; else c->n = ph_vnorm(ph_vadd(c->n, n2));
            ph_cadd(c, ph_vadd(pts[i], ph_vmul(n2, r - (r - l) * 0.5f)), r - l); any = true;
        }
        if (!any && s->shape == ARC_SHAPE_CAPSULE) {   /* the middle of the capsule across a triangle edge */
            arc_v3 ed[3][2] = { { t->a, t->b }, { t->b, t->c }, { t->c, t->a } };
            float bd = 1e30f; arc_v3 bq = ph_v3(0, 0, 0), bs = ph_v3(0, 0, 0);
            for (int e = 0; e < 3; e++) { arc_v3 c1, c2; ph_closest_seg_seg(pts[0], pts[1], ed[e][0], ed[e][1], &c1, &c2); float l = ph_vlen(ph_vsub(c2, c1)); if (l < bd) { bd = l; bq = c2; bs = c1; } }
            if (bd < r + ph_margin && bd > 1e-6f) { c->n = ph_vmul(ph_vsub(bq, bs), 1.0f / bd); if (ph_vdot(c->n, t->n) < 0) { ph_cadd(c, ph_vadd(bs, ph_vmul(c->n, r - (r - bd) * 0.5f)), r - bd); any = true; } }
        }
        return any;
    }
    /* box: its corners under the triangle's plane and over the triangle, and the triangle's corners inside the box */
    arc_v3 ax[3]; ph_qmat(s->q, ax);
    c->n = ph_vmul(t->n, -1);
    for (int i = 0; i < 8; i++) {
        arc_v3 p = ph_vadd(s->pos, ph_to_world(ax, ph_v3(i & 1 ? s->size.x : -s->size.x, i & 2 ? s->size.y : -s->size.y, i & 4 ? s->size.z : -s->size.z)));
        float pen = ph_vdot(ph_vsub(t->a, p), t->n);
        if (pen <= -ph_margin || pen > 2 * fmaxf(s->size.x, fmaxf(s->size.y, s->size.z))) continue;
        arc_v3 q = ph_vadd(p, ph_vmul(t->n, pen));
        arc_v3 cq = ph_closest_on_tri(q, t->a, t->b, t->c);
        if (ph_vlen(ph_vsub(cq, q)) > 1e-4f) continue;
        ph_cadd(c, ph_vadd(p, ph_vmul(t->n, pen * 0.5f)), pen);
    }
    const arc_v3 tv[3] = { t->a, t->b, t->c };
    for (int i = 0; i < 3; i++) {
        arc_v3 l = ph_to_local(ax, ph_vsub(tv[i], s->pos));
        float dx = s->size.x - fabsf(l.x), dy = s->size.y - fabsf(l.y), dz = s->size.z - fabsf(l.z);
        if (dx <= 0 || dy <= 0 || dz <= 0) continue;
        float pen = ph_vdot(ph_vsub(tv[i], s->pos), t->n);   /* how far the corner pokes up into the box, measured along n */
        float extent = fabsf(ph_vdot(ax[0], t->n)) * s->size.x + fabsf(ph_vdot(ax[1], t->n)) * s->size.y + fabsf(ph_vdot(ax[2], t->n)) * s->size.z;
        pen += extent; if (pen <= 0) continue;
        ph_cadd(c, tv[i], fminf(pen, fminf(dx, fminf(dy, dz)) + 1e-3f));
    }
    return c->np > 0;
}
/* body vs body; contact from a to b */
static bool ph_collide(const ph_body *A, const ph_body *B, ph_contact *c) {
    c->np = 0;
    int sa = A->shape, sb = B->shape;
    if (sa > sb) {   /* order the pair, then flip the result */
        bool r = ph_collide(B, A, c);
        if (r) c->n = ph_vmul(c->n, -1);
        return r;
    }
    arc_v3 axa[3], axb[3]; ph_qmat(A->q, axa); ph_qmat(B->q, axb);
    if (sa == ARC_SHAPE_SPHERE && sb == ARC_SHAPE_SPHERE) return ph_sphere_sphere(A->pos, A->size.x, B->pos, B->size.x, c);
    if (sa == ARC_SHAPE_SPHERE && sb == ARC_SHAPE_BOX) return ph_sphere_box(A->pos, A->size.x, B->pos, axb, B->size, c);
    if (sa == ARC_SHAPE_SPHERE && sb == ARC_SHAPE_CAPSULE) {
        arc_v3 p0, p1; ph_capsule_seg(B, &p0, &p1);
        return ph_sphere_sphere(A->pos, A->size.x, ph_closest_on_seg(A->pos, p0, p1), B->size.x, c);
    }
    if (sa == ARC_SHAPE_BOX && sb == ARC_SHAPE_BOX) return ph_box_box(A->pos, axa, A->size, B->pos, axb, B->size, c);
    if (sa == ARC_SHAPE_BOX && sb == ARC_SHAPE_CAPSULE) {
        arc_v3 p0, p1; ph_capsule_seg(B, &p0, &p1);
        bool r = ph_seg_box(p0, p1, B->size.x, A->pos, axa, A->size, c);   /* from the capsule to the box */
        if (r) c->n = ph_vmul(c->n, -1);
        return r;
    }
    if (sa == ARC_SHAPE_CAPSULE && sb == ARC_SHAPE_CAPSULE) {
        arc_v3 a0, a1, b0, b1, c1, c2; ph_capsule_seg(A, &a0, &a1); ph_capsule_seg(B, &b0, &b1);
        /* nearly parallel and overlapping: two contacts (the ends of the overlap) so they rest side by side */
        ph_closest_seg_seg(a0, a1, b0, b1, &c1, &c2);
        bool r = ph_sphere_sphere(c1, A->size.x, c2, B->size.x, c);
        if (r) {
            arc_v3 da = ph_vnorm(ph_vsub(a1, a0)), db = ph_vnorm(ph_vsub(b1, b0));
            if (fabsf(ph_vdot(da, db)) > 0.99f && A->size.y > 0 && B->size.y > 0) {
                ph_contact e = { 0 };
                arc_v3 q0 = ph_closest_on_seg(a0, b0, b1), q1 = ph_closest_on_seg(a1, b0, b1);
                if (ph_sphere_sphere(a0, A->size.x, q0, B->size.x, &e)) { c->np = 0; ph_cadd(c, e.p[0], e.pen[0]); }
                e.np = 0;
                if (ph_sphere_sphere(a1, A->size.x, q1, B->size.x, &e)) ph_cadd(c, e.p[0], e.pen[0]);
                if (c->np == 0) return false;
            }
        }
        return r;
    }
    return false;
}

/* ---- the step ---- */
static void ph_ensure_m(ph_world *w, int n) {
    if (n <= w->cm) return;
    int c = w->cm ? w->cm : 64; while (c < n) c *= 2;
    w->m = (ph_manifold *)realloc(w->m, sizeof(ph_manifold) * (size_t)c);
    w->old = (ph_manifold *)realloc(w->old, sizeof(ph_manifold) * (size_t)c);
    w->cm = c;
}
static int ph_key_cmp(const ph_manifold *x, int a, int b, int sub) {
    if (x->a != a) return x->a < a ? -1 : 1;
    if (x->b != b) return x->b < b ? -1 : 1;
    if (x->sub != sub) return x->sub < sub ? -1 : 1;
    return 0;
}
static const ph_manifold *ph_find_old(const ph_world *w, int a, int b, int sub) {   /* old is sorted by key */
    int lo = 0, hi = w->nold - 1;
    while (lo <= hi) { int mid = (lo + hi) / 2; int c = ph_key_cmp(&w->old[mid], a, b, sub); if (!c) return &w->old[mid]; if (c < 0) lo = mid + 1; else hi = mid - 1; }
    return NULL;
}
static bool ph_active(const ph_body *b) {   /* moving this step: an awake dynamic body, or a kinematic one being moved */
    if (b->type == ARC_BODY_DYNAMIC) return b->awake;
    if (b->type == ARC_BODY_KINEMATIC) return b->vel.x != 0 || b->vel.y != 0 || b->vel.z != 0 || b->w.x != 0 || b->w.y != 0 || b->w.z != 0;
    return false;
}
static bool ph_can(const ph_body *a, const ph_body *b) {
    if ((a->layer & b->mask) == 0 || (b->layer & a->mask) == 0) return false;
    if (a->type != ARC_BODY_DYNAMIC && b->type != ARC_BODY_DYNAMIC) return false;
    return ph_active(a) || ph_active(b);
}
/* make a manifold from contact points (keeps at most 4: the deepest, then the most spread) */
static void ph_add_manifold(ph_world *w, int a, int b, int sub, const ph_contact *c) {
    if (c->np == 0) return;
    ph_ensure_m(w, w->nm + 1);
    ph_manifold *m = &w->m[w->nm++]; memset(m, 0, sizeof *m);
    m->a = a; m->b = b; m->sub = sub; m->n = ph_vnorm(c->n);
    int pick[4], np = 0;
    if (c->np <= 4) { for (int i = 0; i < c->np; i++) pick[np++] = i; }
    else {
        int d = 0; for (int i = 1; i < c->np; i++) if (c->pen[i] > c->pen[d]) d = i;
        pick[np++] = d;
        int f = d; float fd = -1; for (int i = 0; i < c->np; i++) { float l = ph_vlen(ph_vsub(c->p[i], c->p[d])); if (l > fd) { fd = l; f = i; } }
        pick[np++] = f;
        int g = -1; float ga = -1; for (int i = 0; i < c->np; i++) { if (i == d || i == f) continue; float ar = ph_vlen(ph_vcross(ph_vsub(c->p[f], c->p[d]), ph_vsub(c->p[i], c->p[d]))); if (ar > ga) { ga = ar; g = i; } }
        if (g >= 0) pick[np++] = g;
        int k = -1; float ka = -1;
        for (int i = 0; i < c->np; i++) {
            if (i == d || i == f || i == g) continue;
            /* farthest from the triangle d-f-g's plane... in practice: from its centroid */
            arc_v3 cen = ph_vmul(ph_vadd(ph_vadd(c->p[d], c->p[f]), c->p[g]), 1.0f / 3);
            float l = ph_vlen(ph_vsub(c->p[i], cen)); if (l > ka) { ka = l; k = i; }
        }
        if (k >= 0) pick[np++] = k;
    }
    const ph_body *A = &w->b[a], *B = b ? &w->b[b] : NULL;
    const ph_manifold *old = ph_find_old(w, a, b, sub);
    m->fresh = old == NULL || !old->touching;
    m->jpre = old && !old->touching ? old->jpre : 0;
    for (int i = 0; i < np; i++) {
        ph_point *p = &m->p[m->np++];
        arc_v3 pt = c->p[pick[i]];
        p->rA = ph_vsub(pt, A->pos); p->rB = B ? ph_vsub(pt, B->pos) : pt; p->pen = c->pen[pick[i]];
        if (old) {   /* warm start: the old point nearest this one, if close enough */
            float bd = 0.05f * 0.05f; int bi = -1;
            for (int k = 0; k < old->np; k++) { arc_v3 op = ph_vadd(A->pos, old->p[k].rA); arc_v3 dd = ph_vsub(op, pt); float d2 = ph_vdot(dd, dd); if (d2 < bd) { bd = d2; bi = k; } }
            if (bi >= 0) { p->jn = old->p[bi].jn; p->jt1 = old->p[bi].jt1; p->jt2 = old->p[bi].jt2; p->pending = old->p[bi].pending; }
        }
    }
    float fa = A->friction, fb = B ? B->friction : 0.6f;
    m->friction = sqrtf(fa * fb);
    m->restitution = fmaxf(A->restitution, B ? B->restitution : 0);
    /* rolling resistance: the round body's radius times its coefficient (moment arm x coefficient x normal force) */
    float k = 0;
    if (A->shape != ARC_SHAPE_BOX) k = fmaxf(k, A->rolling * A->size.x);
    if (B && B->shape != ARC_SHAPE_BOX) k = fmaxf(k, B->rolling * B->size.x);
    m->roll_limit_k = k; m->jr = ph_v3(0, 0, 0);
}
static int ph_pair_cmp(const void *x, const void *y) {
    const int *a = (const int *)x, *b = (const int *)y;
    if (a[0] != b[0]) return a[0] - b[0];
    return a[1] - b[1];
}
static ph_world *g_sort_w;
static int ph_order_cmp(const void *x, const void *y) {
    int a = *(const int *)x, b = *(const int *)y;
    float la = g_sort_w->b[a].lo.x, lb = g_sort_w->b[b].lo.x;
    if (la != lb) return la < lb ? -1 : 1;
    return a - b;
}
static float ph_speed_at(const ph_body *b) { return ph_vlen(b->vel) + ph_vlen(b->w) * fmaxf(b->size.x, fmaxf(b->size.y, b->size.z)); }
static float ph_pair_margin(const ph_body *a, const ph_body *b) {
    float v = ph_speed_at(a) + (b ? ph_speed_at(b) : 0);
    return fminf(0.01f + v * ph_dt_now, 1.0f);
}
static bool ph_joined(const ph_world *w, int a, int b) {
    for (int k = 1; k < w->nj; k++) { const ph_joint *J = &w->j[k]; if (J->used && !J->collide && ((J->a == a && J->b == b) || (J->a == b && J->b == a))) return true; }
    return false;
}
static void ph_find_contacts(ph_world *w) {
    w->nm = 0;
    /* body pairs: sort-and-sweep on x */
    int n = 0;
    for (int i = 1; i < w->max_bodies; i++) if (w->b[i].used) { ph_body_bounds(&w->b[i]); w->order[n++] = i; }
    g_sort_w = w; qsort(w->order, (size_t)n, sizeof(int), ph_order_cmp);
    w->npairs = 0;
    for (int i = 0; i < n; i++) {
        ph_body *a = &w->b[w->order[i]];
        for (int j = i + 1; j < n; j++) {
            ph_body *b = &w->b[w->order[j]];
            if (b->lo.x > a->hi.x) break;
            if (b->lo.y > a->hi.y || b->hi.y < a->lo.y || b->lo.z > a->hi.z || b->hi.z < a->lo.z) continue;
            if (!ph_can(a, b)) continue;
            if (ph_joined(w, w->order[i], w->order[j])) continue;   /* 0.9.7 joined bodies pass through each other unless collide */
            if (w->npairs + 1 > w->cpairs) { w->cpairs = w->cpairs ? w->cpairs * 2 : 256; w->pairs = (int *)realloc(w->pairs, sizeof(int) * 2 * (size_t)w->cpairs); }
            int ia = w->order[i], ib = w->order[j];
            w->pairs[w->npairs * 2] = ia < ib ? ia : ib; w->pairs[w->npairs * 2 + 1] = ia < ib ? ib : ia; w->npairs++;
        }
    }
    qsort(w->pairs, (size_t)w->npairs, sizeof(int) * 2, ph_pair_cmp);   /* index order, whatever the sweep found first */
    for (int k = 0; k < w->npairs; k++) {
        int ia = w->pairs[k * 2], ib = w->pairs[k * 2 + 1];
        ph_contact c = { 0 };
        ph_margin = ph_pair_margin(&w->b[ia], &w->b[ib]);
        if (ph_collide(&w->b[ia], &w->b[ib], &c)) ph_add_manifold(w, ia, ib, 0, &c);
    }
    /* each awake dynamic body against the static ground */
    if (w->grid_dirty) ph_build_grid(w);
    for (int i = 1; i < w->max_bodies; i++) {
        ph_body *b = &w->b[i];
        if (!b->used || b->type != ARC_BODY_DYNAMIC || !b->awake) continue;
        ph_margin = ph_pair_margin(b, NULL);
        for (int p = 0; p < w->nplanes; p++) {
            ph_contact c = { 0 };
            if (ph_shape_plane(b, w->planes[p].n, w->planes[p].d, &c)) ph_add_manifold(w, i, 0, -(p + 1), &c);
        }
        if (w->ntris && w->gstart) {
            int cx0 = (int)floorf((b->lo.x - w->gx) / w->gcell), cx1 = (int)floorf((b->hi.x - w->gx) / w->gcell);
            int cz0 = (int)floorf((b->lo.z - w->gz) / w->gcell), cz1 = (int)floorf((b->hi.z - w->gz) / w->gcell);
            if (cx0 < 0) cx0 = 0;
            if (cz0 < 0) cz0 = 0;
            if (cx1 >= w->gw) cx1 = w->gw - 1;
            if (cz1 >= w->gh) cz1 = w->gh - 1;
            /* triangles under several cells: take each once, in index order (collect, sort, unique) */
            int cand[256], nc = 0;
            for (int cz = cz0; cz <= cz1; cz++) for (int cx = cx0; cx <= cx1; cx++) {
                int c = cz * w->gw + cx;
                for (int q = w->gstart[c]; q < w->gstart[c + 1] && nc < 256; q++) cand[nc++] = w->gitem[q];
            }
            for (int x = 1; x < nc; x++) { int v = cand[x], y = x - 1; while (y >= 0 && cand[y] > v) { cand[y + 1] = cand[y]; y--; } cand[y + 1] = v; }
            for (int q = 0; q < nc; q++) {
                if (q && cand[q] == cand[q - 1]) continue;
                const ph_tri *t = &w->tris[cand[q]];
                float tlo = fminf(t->a.y, fminf(t->b.y, t->c.y)), thi = fmaxf(t->a.y, fmaxf(t->b.y, t->c.y));
                if (thi < b->lo.y || tlo > b->hi.y) continue;
                ph_contact c = { 0 };
                if (ph_shape_tri(b, t, &c)) ph_add_manifold(w, i, 0, cand[q], &c);
            }
        }
    }
}
static void ph_prepare(ph_world *w, ph_manifold *m, float dt) {
    ph_body *A = &w->b[m->a], *B = m->b ? &w->b[m->b] : NULL;
    ph_tangents(m->n, &m->t1, &m->t2);
    for (int i = 0; i < m->np; i++) {
        ph_point *p = &m->p[i];
        arc_v3 n = m->n;
        float ima = A->type == ARC_BODY_DYNAMIC ? A->inv_mass : 0, imb = B && B->type == ARC_BODY_DYNAMIC ? B->inv_mass : 0;
        #define PH_EFF(dir) (ima + imb + ph_vdot(ph_vcross(ph_inv_i_mul(A, ph_vcross(p->rA, dir)), p->rA), dir) + (B ? ph_vdot(ph_vcross(ph_inv_i_mul(B, ph_vcross(p->rB, dir)), p->rB), dir) : 0))
        float kn = PH_EFF(n), k1 = PH_EFF(m->t1), k2 = PH_EFF(m->t2);
        #undef PH_EFF
        p->mn = kn > 0 ? 1.0f / kn : 0; p->mt1 = k1 > 0 ? 1.0f / k1 : 0; p->mt2 = k2 > 0 ? 1.0f / k2 : 0;
        arc_v3 va = ph_vadd(A->vel, ph_vcross(A->w, p->rA)), vb = B ? ph_vadd(B->vel, ph_vcross(B->w, p->rB)) : ph_v3(0, 0, 0);
        float vn = ph_vdot(ph_vsub(vb, va), n);
        if (p->pen >= 0) {   /* touching: push out the depth past the slop; bounce if hitting fast (now or when it arrived) */
            p->bias = PH_BAUMGARTE / dt * fmaxf(p->pen - PH_SLOP, 0);
            float hit = fminf(vn, p->pending);
            if (hit < -1.0f && m->restitution > 0) p->bias = fmaxf(p->bias, -m->restitution * hit);
            p->pending = 0;
        } else {             /* a gap: close at most the gap this step (it arrives at the surface); bounce next step */
            p->bias = p->pen / dt;
            p->pending = -vn * dt > -p->pen ? vn : 0;
        }
    }
}
static void ph_apply(ph_body *A, ph_body *B, arc_v3 rA, arc_v3 rB, arc_v3 j) {   /* impulse j on B, -j on A */
    if (A->type == ARC_BODY_DYNAMIC) { A->vel = ph_vsub(A->vel, ph_vmul(j, A->inv_mass)); A->w = ph_vsub(A->w, ph_inv_i_mul(A, ph_vcross(rA, j))); }
    if (B && B->type == ARC_BODY_DYNAMIC) { B->vel = ph_vadd(B->vel, ph_vmul(j, B->inv_mass)); B->w = ph_vadd(B->w, ph_inv_i_mul(B, ph_vcross(rB, j))); }
}
static void ph_warm(ph_world *w, ph_manifold *m) {
    ph_body *A = &w->b[m->a], *B = m->b ? &w->b[m->b] : NULL;
    for (int i = 0; i < m->np; i++) {
        ph_point *p = &m->p[i];
        ph_apply(A, B, p->rA, p->rB, ph_vadd(ph_vmul(m->n, p->jn), ph_vadd(ph_vmul(m->t1, p->jt1), ph_vmul(m->t2, p->jt2))));
    }
}
static void ph_solve(ph_world *w, ph_manifold *m) {
    ph_body *A = &w->b[m->a], *B = m->b ? &w->b[m->b] : NULL;
    for (int i = 0; i < m->np; i++) {
        ph_point *p = &m->p[i];
        #define PH_RELV() ph_vsub(B ? ph_vadd(B->vel, ph_vcross(B->w, p->rB)) : ph_v3(0, 0, 0), ph_vadd(A->vel, ph_vcross(A->w, p->rA)))
        /* friction, limited by the normal impulse */
        float lim = m->friction * p->jn;
        arc_v3 dv = PH_RELV();
        float j1 = -ph_vdot(dv, m->t1) * p->mt1, n1 = arc_clampf(p->jt1 + j1, -lim, lim); j1 = n1 - p->jt1; p->jt1 = n1;
        float j2 = -ph_vdot(dv, m->t2) * p->mt2, n2 = arc_clampf(p->jt2 + j2, -lim, lim); j2 = n2 - p->jt2; p->jt2 = n2;
        ph_apply(A, B, p->rA, p->rB, ph_vadd(ph_vmul(m->t1, j1), ph_vmul(m->t2, j2)));
        /* normal: no pulling, push out */
        dv = PH_RELV();
        float jn = (-ph_vdot(dv, m->n) + p->bias) * p->mn, nn = fmaxf(p->jn + jn, 0); jn = nn - p->jn; p->jn = nn;
        ph_apply(A, B, p->rA, p->rB, ph_vmul(m->n, jn));
        #undef PH_RELV
    }
    if (m->roll_limit_k > 0) {   /* rolling resistance: an angular impulse against the relative spin, limited by the contact force */
        float jn_sum = 0; for (int i = 0; i < m->np; i++) jn_sum += m->p[i].jn;
        arc_v3 wr = ph_vsub(B ? B->w : ph_v3(0, 0, 0), A->w); float l = ph_vlen(wr);
        if (l > 1e-9f && jn_sum > 0) {
            arc_v3 d = ph_vmul(wr, 1.0f / l);
            float kk = ph_vdot(d, ph_inv_i_mul(A, d)) + (B ? ph_vdot(d, ph_inv_i_mul(B, d)) : 0);
            if (kk > 1e-12f) {
                arc_v3 nj = ph_vadd(m->jr, ph_vmul(d, -l / kk));
                float lim = m->roll_limit_k * jn_sum, nl = ph_vlen(nj);
                if (nl > lim) nj = ph_vmul(nj, lim / nl);
                arc_v3 dj = ph_vsub(nj, m->jr); m->jr = nj;
                if (A->type == ARC_BODY_DYNAMIC) A->w = ph_vsub(A->w, ph_inv_i_mul(A, dj));
                if (B && B->type == ARC_BODY_DYNAMIC) B->w = ph_vadd(B->w, ph_inv_i_mul(B, dj));
            }
        }
    }
}
static int ph_m_cmp(const void *x, const void *y) {
    const ph_manifold *a = (const ph_manifold *)x, *b = (const ph_manifold *)y;
    int c = ph_key_cmp(a, b->a, b->b, b->sub);
    return c ? c : (a->ord > b->ord) - (a->ord < b->ord);   /* (0.9.7: equal keys in a fixed order on every machine) */
}
/* ---- 0.9.7 joints ---- */
static ph_body ph_wbody = { .used = true, .type = ARC_BODY_STATIC, .q = { 0, 0, 0, 1 } };   /* the world end of a joint */
static ph_body *ph_jbody(ph_world *w, int i) { return i ? &w->b[i] : &ph_wbody; }
static float ph_jangle(const ph_joint *J, arc_quat qa, arc_quat qb) {   /* the hinge's turn since it was added (A's frame) */
    arc_quat d = arc_quat_mul(arc_quat_mul(ph_qconj(qa), qb), ph_qconj(J->rel0));
    if (d.w < 0) d = (arc_quat){ -d.x, -d.y, -d.z, -d.w };
    return 2 * ph_atan2(d.x * J->axa.x + d.y * J->axa.y + d.z * J->axa.z, d.w);
}
static arc_v3 ph_kmul(const float *k, arc_v3 v) { return ph_v3(k[0] * v.x + k[3] * v.y + k[6] * v.z, k[1] * v.x + k[4] * v.y + k[7] * v.z, k[2] * v.x + k[5] * v.y + k[8] * v.z); }
static void ph_jprepare(ph_world *w, ph_joint *J, float dt) {
    ph_body *A = ph_jbody(w, J->a), *B = ph_jbody(w, J->b);
    arc_v3 axA[3], axB[3]; ph_qmat(A->q, axA); ph_qmat(B->q, axB);
    J->rA = ph_to_world(axA, J->la); J->rB = ph_to_world(axB, J->lb);
    float ima = A->type == ARC_BODY_DYNAMIC ? A->inv_mass : 0, imb = B->type == ARC_BODY_DYNAMIC ? B->inv_mass : 0;
    /* point: K v = (ima + imb) v + (IA^-1 (rA x v)) x rA + (IB^-1 (rB x v)) x rB; inverted by cofactors */
    float K[9];
    for (int c = 0; c < 3; c++) {
        arc_v3 e = ph_v3(c == 0 ? 1.0f : 0.0f, c == 1 ? 1.0f : 0.0f, c == 2 ? 1.0f : 0.0f);
        arc_v3 col = ph_vadd(ph_vmul(e, ima + imb), ph_vadd(ph_vcross(ph_inv_i_mul(A, ph_vcross(J->rA, e)), J->rA), ph_vcross(ph_inv_i_mul(B, ph_vcross(J->rB, e)), J->rB)));
        K[c * 3] = col.x; K[c * 3 + 1] = col.y; K[c * 3 + 2] = col.z;
    }
    float c00 = K[4] * K[8] - K[7] * K[5], c01 = K[7] * K[2] - K[1] * K[8], c02 = K[1] * K[5] - K[4] * K[2];
    float det = K[0] * c00 + K[3] * c01 + K[6] * c02;
    if (fabsf(det) > 1e-20f) {
        float id = 1.0f / det;
        J->kp[0] = c00 * id; J->kp[1] = c01 * id; J->kp[2] = c02 * id;
        J->kp[3] = (K[6] * K[5] - K[3] * K[8]) * id; J->kp[4] = (K[0] * K[8] - K[6] * K[2]) * id; J->kp[5] = (K[3] * K[2] - K[0] * K[5]) * id;
        J->kp[6] = (K[3] * K[7] - K[6] * K[4]) * id; J->kp[7] = (K[6] * K[1] - K[0] * K[7]) * id; J->kp[8] = (K[0] * K[4] - K[3] * K[1]) * id;
    } else memset(J->kp, 0, sizeof J->kp);
    J->cp = ph_vmul(ph_vsub(ph_vadd(B->pos, J->rB), ph_vadd(A->pos, J->rA)), PH_BAUMGARTE / dt);
    if (J->type == ARC_JOINT_BALL) {   /* 0.9.8 cone and twist */
        #define PH_KB(x) (ph_vdot(x, ph_vadd(ph_inv_i_mul(A, x), ph_inv_i_mul(B, x))))
        arc_v3 aB = ph_vnorm(ph_to_world(axB, J->axb));
        if (J->cone > 0) {
            arc_v3 cA = ph_vnorm(ph_to_world(axA, J->cna)), cr = ph_vcross(cA, aB);
            float sl = ph_vlen(cr), co = ph_vdot(cA, aB);
            J->swing = ph_atan2(sl, co);
            if (sl > 1e-6f) J->sc = ph_vmul(cr, 1.0f / sl); else { arc_v3 t2; ph_tangents(cA, &J->sc, &t2); }
            float k = PH_KB(J->sc); J->ksc = k > 1e-20f ? 1.0f / k : 0;
        } else { J->jcone = 0; J->ksc = 0; }
        if (J->limit) {
            J->tw = aB; float k = PH_KB(aB); J->ktw = k > 1e-20f ? 1.0f / k : 0;
            J->twist = ph_jangle(J, A->q, B->q);
        } else { J->jtlo = J->jthi = 0; J->ktw = 0; }
        #undef PH_KB
        return;
    }
    /* the axis: B's axis kept on A's (two rows across it) */
    arc_v3 aA = ph_vnorm(ph_to_world(axA, J->axa)), aB = ph_vnorm(ph_to_world(axB, J->axb)), e = ph_vcross(aA, aB);
    J->ax = aA; ph_tangents(aA, &J->u1, &J->u2);
    #define PH_KA(x, y) (ph_vdot(x, ph_vadd(ph_inv_i_mul(A, y), ph_inv_i_mul(B, y))))
    float k11 = PH_KA(J->u1, J->u1), k12 = PH_KA(J->u1, J->u2), k22 = PH_KA(J->u2, J->u2), d2 = k11 * k22 - k12 * k12;
    if (fabsf(d2) > 1e-20f) { float id = 1.0f / d2; J->k2[0] = k22 * id; J->k2[1] = -k12 * id; J->k2[2] = -k12 * id; J->k2[3] = k11 * id; } else memset(J->k2, 0, sizeof J->k2);
    float ka = PH_KA(aA, aA); J->kax = ka > 1e-20f ? 1.0f / ka : 0;
    #undef PH_KA
    J->b1 = PH_BAUMGARTE / dt * ph_vdot(e, J->u1); J->b2 = PH_BAUMGARTE / dt * ph_vdot(e, J->u2);
    J->angle = ph_jangle(J, A->q, B->q);
    if (!J->limit) { J->jlo = J->jhi = 0; }
    if (!J->motor) J->jm = 0;
}
static void ph_jang(ph_body *A, ph_body *B, arc_v3 L) {   /* angular impulse L on B, -L on A */
    if (A->type == ARC_BODY_DYNAMIC) A->w = ph_vsub(A->w, ph_inv_i_mul(A, L));
    if (B->type == ARC_BODY_DYNAMIC) B->w = ph_vadd(B->w, ph_inv_i_mul(B, L));
}
static void ph_jwarm(ph_world *w, ph_joint *J) {
    ph_body *A = ph_jbody(w, J->a), *B = ph_jbody(w, J->b);
    ph_apply(A, B, J->rA, J->rB, J->jp);
    if (J->type == ARC_JOINT_HINGE) {
        J->ja = ph_vadd(ph_vmul(J->u1, ph_vdot(J->ja, J->u1)), ph_vmul(J->u2, ph_vdot(J->ja, J->u2)));   /* onto this step's rows */
        ph_jang(A, B, ph_vadd(J->ja, ph_vmul(J->ax, J->jlo - J->jhi + J->jm)));
    } else {
        J->ja = ph_v3(0, 0, 0);
        if (J->jcone != 0 || J->jtlo != 0 || J->jthi != 0) ph_jang(A, B, ph_vadd(ph_vmul(J->sc, -J->jcone), ph_vmul(J->tw, J->jtlo - J->jthi)));
    }
}
static void ph_jsolve(ph_world *w, ph_joint *J, float dt) {
    ph_body *A = ph_jbody(w, J->a), *B = ph_jbody(w, J->b);
    if (J->type == ARC_JOINT_HINGE) {
        if (J->motor && J->kax > 0) {   /* drive the relative spin about the axis toward the speed */
            float wr = ph_vdot(ph_vsub(B->w, A->w), J->ax), lim = J->mtorque * dt, nm = arc_clampf(J->jm + J->kax * (J->mspeed - wr), -lim, lim);
            float d = nm - J->jm; J->jm = nm; ph_jang(A, B, ph_vmul(J->ax, d));
        }
        if (J->limit && J->kax > 0) {   /* lower then upper; a gap may close this step, no more */
            float c = J->angle - J->lower, bias = c > 0 ? c / dt : PH_BAUMGARTE * c / dt;
            float wr = ph_vdot(ph_vsub(B->w, A->w), J->ax), nl = fmaxf(J->jlo - J->kax * (wr + bias), 0), d = nl - J->jlo; J->jlo = nl;
            ph_jang(A, B, ph_vmul(J->ax, d));
            c = J->upper - J->angle; bias = c > 0 ? c / dt : PH_BAUMGARTE * c / dt;
            wr = ph_vdot(ph_vsub(B->w, A->w), J->ax); float nh = fmaxf(J->jhi - J->kax * (-wr + bias), 0); d = nh - J->jhi; J->jhi = nh;
            ph_jang(A, B, ph_vmul(J->ax, -d));
        }
        arc_v3 wr = ph_vsub(B->w, A->w);   /* the two rows across the axis */
        float r1 = -(ph_vdot(wr, J->u1) + J->b1), r2 = -(ph_vdot(wr, J->u2) + J->b2);
        float l1 = J->k2[0] * r1 + J->k2[1] * r2, l2 = J->k2[2] * r1 + J->k2[3] * r2;
        arc_v3 L = ph_vadd(ph_vmul(J->u1, l1), ph_vmul(J->u2, l2)); J->ja = ph_vadd(J->ja, L); ph_jang(A, B, L);
    } else {
        if (J->cone > 0 && J->ksc > 0) {   /* the swing may close to the cone this step, no further */
            float c = J->cone - J->swing, bias = c > 0 ? c / dt : PH_BAUMGARTE * c / dt;
            float wr = ph_vdot(ph_vsub(B->w, A->w), J->sc), nh = fmaxf(J->jcone - J->ksc * (-wr + bias), 0), d = nh - J->jcone; J->jcone = nh;
            ph_jang(A, B, ph_vmul(J->sc, -d));
        }
        if (J->limit && J->ktw > 0) {
            float c = J->twist - J->lower, bias = c > 0 ? c / dt : PH_BAUMGARTE * c / dt;
            float wr = ph_vdot(ph_vsub(B->w, A->w), J->tw), nl = fmaxf(J->jtlo - J->ktw * (wr + bias), 0), d = nl - J->jtlo; J->jtlo = nl;
            ph_jang(A, B, ph_vmul(J->tw, d));
            c = J->upper - J->twist; bias = c > 0 ? c / dt : PH_BAUMGARTE * c / dt;
            wr = ph_vdot(ph_vsub(B->w, A->w), J->tw); float nh = fmaxf(J->jthi - J->ktw * (-wr + bias), 0); d = nh - J->jthi; J->jthi = nh;
            ph_jang(A, B, ph_vmul(J->tw, -d));
        }
    }
    /* the point: last, so it holds best */
    arc_v3 dv = ph_vsub(ph_vadd(B->vel, ph_vcross(B->w, J->rB)), ph_vadd(A->vel, ph_vcross(A->w, J->rA)));
    arc_v3 lam = ph_kmul(J->kp, ph_vsub(ph_vmul(dv, -1), J->cp));
    J->jp = ph_vadd(J->jp, lam); ph_apply(A, B, J->rA, J->rB, lam);
}
static void ph_jwake(ph_world *w, const ph_joint *J) {   /* both ends awake (the world end is no body) */
    if (J->a) { w->b[J->a].awake = true; w->b[J->a].sleep_t = 0; }
    if (J->b) { w->b[J->b].awake = true; w->b[J->b].sleep_t = 0; }
}
int arc_physics_add_joint(arc_physics h, const arc_joint_desc *d) {
    ph_world *w = ph_get(h); if (!w || !d) return 0;
    ph_body *A = ph_body_get(w, d->a), *B = d->b ? ph_body_get(w, d->b) : &ph_wbody;
    if (!A || !B || d->a == d->b) { arc_log("arc_physics_add_joint: body a must exist, b must exist or be 0 (the world), and differ"); return 0; }
    if (d->type != ARC_JOINT_HINGE && d->type != ARC_JOINT_BALL) { arc_log("arc_physics_add_joint: type ARC_JOINT_HINGE or ARC_JOINT_BALL"); return 0; }
    int k = 1; while (k < w->nj && w->j[k].used) k++;
    if (k >= w->cj) { w->cj = w->cj ? w->cj * 2 : 16; w->j = (ph_joint *)realloc(w->j, sizeof(ph_joint) * (size_t)w->cj); memset(w->j + w->nj, 0, sizeof(ph_joint) * (size_t)(w->cj - w->nj)); }
    if (k >= w->nj) w->nj = k + 1;
    ph_joint *J = &w->j[k]; memset(J, 0, sizeof *J);
    J->used = true; J->type = (uint8_t)d->type; J->collide = d->collide;
    J->a = d->b; J->b = d->a; { ph_body *t = A; A = B; B = t; }   /* inside: A is the base (desc b, maybe the world), so the angle and motor are a's turn relative to b */
    arc_v3 axA[3], axB[3]; ph_qmat(A->q, axA); ph_qmat(B->q, axB);
    J->la = ph_to_local(axA, ph_vsub(d->anchor, A->pos)); J->lb = ph_to_local(axB, ph_vsub(d->anchor, B->pos));
    arc_v3 axis = ph_vlen(d->axis) > 1e-9f ? ph_vnorm(d->axis) : ph_v3(0, 1, 0);
    J->axa = ph_to_local(axA, axis); J->axb = ph_to_local(axB, axis);
    J->rel0 = arc_quat_mul(ph_qconj(A->q), B->q);
    if (J->type == ARC_JOINT_BALL && d->cone > 0) {
        arc_v3 ca = ph_vlen(d->cone_axis) > 1e-9f ? ph_vnorm(d->cone_axis) : axis;
        J->cone = fminf(d->cone, 3.1f); J->cna = ph_to_local(axA, ca);
    }
    J->limit = d->limit; J->lower = d->lower; J->upper = d->upper;
    J->motor = d->motor; J->mspeed = d->motor_speed; J->mtorque = fmaxf(d->motor_torque, 0);
    if (J->limit && J->lower > J->upper) { float t = J->lower; J->lower = J->upper; J->upper = t; }
    ph_jwake(w, J);
    return k;
}
static ph_joint *ph_joint_get(ph_world *w, int k) { return w && k > 0 && k < w->nj && w->j[k].used ? &w->j[k] : NULL; }
void arc_physics_remove_joint(arc_physics h, int k) {
    ph_world *w = ph_get(h); ph_joint *J = ph_joint_get(w, k); if (!J) return;
    ph_jwake(w, J);
    memset(J, 0, sizeof *J);
}
float arc_physics_joint_angle(arc_physics h, int k) {   /* hinge: its turn; ball (0.9.8): the twist about its axis */
    ph_world *w = ph_get(h); ph_joint *J = ph_joint_get(w, k); if (!J) return 0;
    return ph_jangle(J, ph_jbody(w, J->a)->q, ph_jbody(w, J->b)->q);
}
float arc_physics_joint_swing(arc_physics h, int k) {   /* 0.9.8 ball: the angle between a's axis and the cone's centre */
    ph_world *w = ph_get(h); ph_joint *J = ph_joint_get(w, k); if (!J || J->type != ARC_JOINT_BALL) return 0;
    ph_body *A = ph_jbody(w, J->a), *B = ph_jbody(w, J->b); arc_v3 axA[3], axB[3]; ph_qmat(A->q, axA); ph_qmat(B->q, axB);
    arc_v3 c = ph_vnorm(ph_to_world(axA, J->cone > 0 ? J->cna : J->axa)), aB = ph_vnorm(ph_to_world(axB, J->axb));
    return ph_atan2(ph_vlen(ph_vcross(c, aB)), ph_vdot(c, aB));
}
void arc_physics_joint_motor(arc_physics h, int k, float speed, float max_torque) {
    ph_world *w = ph_get(h); ph_joint *J = ph_joint_get(w, k); if (!J) return;
    J->motor = max_torque > 0; J->mspeed = speed; J->mtorque = fmaxf(max_torque, 0);
    ph_jwake(w, J);
}
void arc_physics_joint_limits(arc_physics h, int k, bool on, float lower, float upper) {
    ph_world *w = ph_get(h); ph_joint *J = ph_joint_get(w, k); if (!J) return;
    J->limit = on; J->lower = fminf(lower, upper); J->upper = fmaxf(lower, upper);
    ph_jwake(w, J);
}
float arc_physics_joint_impulse(arc_physics h, int k) { ph_world *w = ph_get(h); ph_joint *J = ph_joint_get(w, k); return J ? ph_vlen(J->jp) : 0; }
int arc_physics_joint_count(arc_physics h) { ph_world *w = ph_get(h); int n = 0; if (w) for (int k = 1; k < w->nj; k++) n += w->j[k].used; return n; }
void arc_physics_step(arc_physics h, float dt) {
    ph_world *w = ph_get(h); if (!w || dt <= 0) return;
    w->nev = 0;
    /* kinematic targets become velocities for this step */
    for (int i = 1; i < w->max_bodies; i++) {
        ph_body *b = &w->b[i]; if (!b->used || b->type != ARC_BODY_KINEMATIC) continue;
        if (!b->has_target) { b->vel = ph_v3(0, 0, 0); b->w = ph_v3(0, 0, 0); continue; }
        b->vel = ph_vmul(ph_vsub(b->target_pos, b->pos), 1.0f / dt);
        arc_quat dq = arc_quat_mul(b->target_q, (arc_quat){ -b->q.x, -b->q.y, -b->q.z, b->q.w });
        if (dq.w < 0) dq = (arc_quat){ -dq.x, -dq.y, -dq.z, -dq.w };
        b->w = ph_vmul(ph_v3(dq.x, dq.y, dq.z), 2.0f / dt);   /* small-angle: w = 2 * axis * sin(angle/2) / dt */
    }
    /* forces and gravity into velocities */
    for (int i = 1; i < w->max_bodies; i++) {
        ph_body *b = &w->b[i]; if (!b->used || b->type != ARC_BODY_DYNAMIC || !b->awake) continue;
        b->vel = ph_vadd(b->vel, ph_vmul(ph_vadd(w->gravity, ph_vmul(b->force, b->inv_mass)), dt));
        b->w = ph_vadd(b->w, ph_vmul(ph_inv_i_mul(b, b->torque), dt));
        b->vel = ph_vmul(b->vel, 1.0f / (1.0f + dt * b->lin_damp));
        b->w = ph_vmul(b->w, 1.0f / (1.0f + dt * b->ang_damp));
        if (b->lock_rot) b->w = ph_v3(0, 0, 0);
        b->force = ph_v3(0, 0, 0); b->torque = ph_v3(0, 0, 0);
    }
    ph_dt_now = dt;
    ph_find_contacts(w);
    /* wake sleeping bodies touched by moving ones */
    for (int k = 0; k < w->nm; k++) {
        ph_body *A = &w->b[w->m[k].a], *B = w->m[k].b ? &w->b[w->m[k].b] : NULL;
        if (!B) continue;
        for (int side = 0; side < 2; side++) {
            ph_body *s = side ? B : A, *o = side ? A : B;
            if (s->type != ARC_BODY_DYNAMIC || s->awake || !ph_active(o)) continue;
            if (o->type == ARC_BODY_KINEMATIC || ph_vlen(o->vel) > PH_SLEEP_LIN || ph_vlen(o->w) > PH_SLEEP_ANG) { s->awake = true; s->sleep_t = 0; }
        }
    }
    /* 0.9.7 joints: a joined pair wakes together; joints to sleeping (or still) ends are left out */
    for (int k = 1; k < w->nj; k++) {
        ph_joint *J = &w->j[k]; if (!J->used) continue;
        ph_body *A = ph_jbody(w, J->a), *B = ph_jbody(w, J->b);
        bool ma = ph_active(A), mb = ph_active(B);
        if (ma && B->type == ARC_BODY_DYNAMIC && !B->awake) { B->awake = true; B->sleep_t = 0; }
        if (mb && A->type == ARC_BODY_DYNAMIC && !A->awake) { A->awake = true; A->sleep_t = 0; }
    }
    int nja = 0;
    for (int k = 1; k < w->nj; k++) { ph_joint *J = &w->j[k]; if (J->used && (ph_active(ph_jbody(w, J->a)) || ph_active(ph_jbody(w, J->b)))) { ph_jprepare(w, J, dt); nja++; } }
    for (int k = 0; k < w->nm; k++) ph_prepare(w, &w->m[k], dt);
    for (int k = 1; k < w->nj && nja; k++) { ph_joint *J = &w->j[k]; if (J->used && (ph_active(ph_jbody(w, J->a)) || ph_active(ph_jbody(w, J->b)))) ph_jwarm(w, J); }
    for (int k = 0; k < w->nm; k++) ph_warm(w, &w->m[k]);
    for (int it = 0; it < w->iterations; it++) {
        for (int k = 1; k < w->nj && nja; k++) { ph_joint *J = &w->j[k]; if (J->used && (ph_active(ph_jbody(w, J->a)) || ph_active(ph_jbody(w, J->b)))) ph_jsolve(w, J, dt); }
        for (int k = 0; k < w->nm; k++) ph_solve(w, &w->m[k]);
    }
    /* positions */
    for (int i = 1; i < w->max_bodies; i++) {
        ph_body *b = &w->b[i]; if (!b->used || b->type == ARC_BODY_STATIC || !b->awake) continue;
        b->pos = ph_vadd(b->pos, ph_vmul(b->vel, dt));
        if (b->type == ARC_BODY_KINEMATIC && b->has_target) { b->pos = b->target_pos; b->q = ph_qnorm(b->target_q); b->has_target = false; continue; }
        if (!b->lock_rot) {
            arc_quat wq = { b->w.x, b->w.y, b->w.z, 0 }, d = arc_quat_mul(wq, b->q);
            b->q = ph_qnorm((arc_quat){ b->q.x + d.x * 0.5f * dt, b->q.y + d.y * 0.5f * dt, b->q.z + d.z * 0.5f * dt, b->q.w + d.w * 0.5f * dt });
        }
    }
    /* events: pairs that just started touching, with the impulse that stopped them */
    for (int k = 0; k < w->nm; k++) {
        ph_manifold *m = &w->m[k];
        float jsum = 0, pmax = -1e30f; for (int i = 0; i < m->np; i++) { jsum += m->p[i].jn; pmax = fmaxf(pmax, m->p[i].pen); }
        m->touching = pmax >= -0.001f;
        if (!m->touching) { m->jpre += jsum; continue; }
        if (!m->fresh) continue;
        jsum += m->jpre;
        float J = jsum; arc_v3 pt = ph_v3(0, 0, 0);
        for (int i = 0; i < m->np; i++) pt = ph_vadd(pt, ph_vadd(w->b[m->a].pos, m->p[i].rA));
        if (m->np) pt = ph_vmul(pt, 1.0f / (float)m->np);
        bool dup = false;   /* one event per body pair (the ground counts once) */
        for (int e = 0; e < w->nev; e++) if (w->ev[e].a == (m->b ? m->a : 0) && w->ev[e].b == (m->b ? m->b : m->a)) { w->ev[e].impulse += J; dup = true; break; }
        if (dup) continue;
        if (w->nev >= w->cev) { w->cev = w->cev ? w->cev * 2 : 32; w->ev = (arc_physics_event *)realloc(w->ev, sizeof(arc_physics_event) * (size_t)w->cev); }
        /* a < b with 0 = ground: (0, body) for the ground */
        w->ev[w->nev++] = m->b ? (arc_physics_event){ m->a, m->b, pt, m->n, J } : (arc_physics_event){ 0, m->a, pt, ph_vmul(m->n, -1), J };
    }
    /* sleep: a body that has been still long enough sleeps, unless something it touches is still moving */
    if (!w->no_sleep) {
        for (int i = 1; i < w->max_bodies; i++) {
            ph_body *b = &w->b[i]; if (!b->used || b->type != ARC_BODY_DYNAMIC || !b->awake) continue;
            if (ph_vlen(b->vel) < PH_SLEEP_LIN && ph_vlen(b->w) < PH_SLEEP_ANG) b->sleep_t += dt; else b->sleep_t = 0;
        }
        /* a moving neighbour keeps a body awake (spread through contacts a few times: whole stacks sleep together) */
        for (int pass = 0; pass < 4; pass++) for (int k = 0; k < w->nm; k++) {
            ph_body *A = &w->b[w->m[k].a], *B = w->m[k].b ? &w->b[w->m[k].b] : NULL;
            if (!B || B->type != ARC_BODY_DYNAMIC || A->type != ARC_BODY_DYNAMIC) continue;
            float t = fminf(A->sleep_t, B->sleep_t); A->sleep_t = B->sleep_t = t;
        }
        for (int pass = 0; pass < 4; pass++) for (int k = 1; k < w->nj; k++) {   /* 0.9.7 and through joints */
            ph_joint *J = &w->j[k]; if (!J->used || !J->a || !J->b) continue;
            ph_body *A = &w->b[J->a], *B = &w->b[J->b];
            if (A->type != ARC_BODY_DYNAMIC || B->type != ARC_BODY_DYNAMIC) continue;
            float t = fminf(A->sleep_t, B->sleep_t); A->sleep_t = B->sleep_t = t;
        }
        for (int i = 1; i < w->max_bodies; i++) {
            ph_body *b = &w->b[i]; if (!b->used || b->type != ARC_BODY_DYNAMIC || !b->awake) continue;
            if (b->sleep_t >= PH_SLEEP_TIME) { b->awake = false; b->vel = ph_v3(0, 0, 0); b->w = ph_v3(0, 0, 0); }
        }
    }
    /* keep this step's contacts (sorted) for the next step's warm start; sleeping bodies keep theirs */
    for (int k = 0; k < w->nold; k++) {
        ph_manifold *o = &w->old[k]; if (o->np == 0) continue;
        bool sa = o->a && !w->b[o->a].awake, sb = !o->b || !w->b[o->b].awake;
        if (sa && sb) { ph_ensure_m(w, w->nm + 1); w->m[w->nm++] = *o; w->m[w->nm - 1].fresh = false; }   /* asleep together: still touching */
    }
    for (int k = 0; k < w->nm; k++) w->m[k].ord = k;
    qsort(w->m, (size_t)w->nm, sizeof(ph_manifold), ph_m_cmp);
    int u = 0; for (int k = 0; k < w->nm; k++) if (!u || ph_key_cmp(&w->m[u - 1], w->m[k].a, w->m[k].b, w->m[k].sub) != 0) w->m[u++] = w->m[k];
    w->nm = u;
    ph_manifold *t = w->old; w->old = w->m; w->m = t; w->nold = w->nm; w->nm = 0;
}

/* ---- access ---- */
arc_v3 arc_physics_pos(arc_physics h, int i) { ph_body *b = ph_body_get(ph_get(h), i); return b ? b->pos : ph_v3(0, 0, 0); }
arc_quat arc_physics_rot(arc_physics h, int i) { ph_body *b = ph_body_get(ph_get(h), i); return b ? b->q : (arc_quat){ 0, 0, 0, 1 }; }
arc_m4 arc_physics_matrix(arc_physics h, int i) {
    ph_body *b = ph_body_get(ph_get(h), i); if (!b) return arc_m4_identity();
    arc_m4 m = arc_m4_from_quat(b->q); m.m[12] = b->pos.x; m.m[13] = b->pos.y; m.m[14] = b->pos.z; return m;
}
arc_v3 arc_physics_vel(arc_physics h, int i) { ph_body *b = ph_body_get(ph_get(h), i); return b ? b->vel : ph_v3(0, 0, 0); }
arc_v3 arc_physics_ang_vel(arc_physics h, int i) { ph_body *b = ph_body_get(ph_get(h), i); return b ? b->w : ph_v3(0, 0, 0); }
bool arc_physics_sleeping(arc_physics h, int i) { ph_body *b = ph_body_get(ph_get(h), i); return b && !b->awake; }
uint32_t arc_physics_user(arc_physics h, int i) { ph_body *b = ph_body_get(ph_get(h), i); return b ? b->user : 0; }
int arc_physics_body_count(arc_physics h) { ph_world *w = ph_get(h); int n = 0; if (w) for (int i = 1; i < w->max_bodies; i++) n += w->b[i].used; return n; }
void arc_physics_wake(arc_physics h, int i) { ph_body *b = ph_body_get(ph_get(h), i); if (b) { b->awake = true; b->sleep_t = 0; } }
void arc_physics_set_pos(arc_physics h, int i, arc_v3 pos, arc_quat rot) {
    ph_body *b = ph_body_get(ph_get(h), i); if (!b) return;
    b->pos = pos; b->q = (rot.x == 0 && rot.y == 0 && rot.z == 0 && rot.w == 0) ? b->q : ph_qnorm(rot); b->awake = true; b->sleep_t = 0; ph_body_bounds(b);
}
void arc_physics_set_vel(arc_physics h, int i, arc_v3 vel, arc_v3 ang) { ph_body *b = ph_body_get(ph_get(h), i); if (!b) return; b->vel = vel; b->w = b->lock_rot ? ph_v3(0, 0, 0) : ang; b->awake = true; b->sleep_t = 0; }
void arc_physics_impulse(arc_physics h, int i, arc_v3 j, arc_v3 at) {
    ph_body *b = ph_body_get(ph_get(h), i); if (!b || b->type != ARC_BODY_DYNAMIC) return;
    b->vel = ph_vadd(b->vel, ph_vmul(j, b->inv_mass)); b->w = ph_vadd(b->w, ph_inv_i_mul(b, ph_vcross(ph_vsub(at, b->pos), j))); b->awake = true; b->sleep_t = 0;
}
void arc_physics_force(arc_physics h, int i, arc_v3 f, arc_v3 at) {
    ph_body *b = ph_body_get(ph_get(h), i); if (!b || b->type != ARC_BODY_DYNAMIC) return;
    b->force = ph_vadd(b->force, f); b->torque = ph_vadd(b->torque, ph_vcross(ph_vsub(at, b->pos), f)); b->awake = true; b->sleep_t = 0;
}
void arc_physics_move_kinematic(arc_physics h, int i, arc_v3 pos, arc_quat rot) {
    ph_body *b = ph_body_get(ph_get(h), i); if (!b || b->type != ARC_BODY_KINEMATIC) return;
    b->has_target = true; b->target_pos = pos; b->target_q = (rot.x == 0 && rot.y == 0 && rot.z == 0 && rot.w == 0) ? b->q : ph_qnorm(rot);
}

/* ---- queries ---- */
static bool ph_ray_body(const ph_body *b, arc_v3 o, arc_v3 d, float max_t, float *t, arc_v3 *n) {
    arc_hit hh;
    if (b->shape == ARC_SHAPE_SPHERE) {
        if (!arc_ray_sphere(o, d, b->pos, b->size.x, max_t, &hh)) return false;
        *t = hh.t; *n = hh.normal; return true;
    }
    if (b->shape == ARC_SHAPE_BOX) {   /* in the box's own frame */
        arc_v3 ax[3]; ph_qmat(b->q, ax);
        arc_v3 lo = ph_to_local(ax, ph_vsub(o, b->pos)), ld = ph_to_local(ax, d);
        if (!arc_ray_box(lo, ld, ph_vmul(b->size, -1), b->size, max_t, &hh)) return false;
        *t = hh.t; *n = ph_to_world(ax, hh.normal); return true;
    }
    /* capsule: the nearest of two end spheres and the cylinder between (sampled exactly enough by spheres along it) */
    arc_v3 p0, p1; ph_capsule_seg(b, &p0, &p1);
    float best = max_t; bool hit = false; arc_v3 bn = ph_v3(0, 1, 0);
    int steps = 2 + (int)(b->size.y * 2 / fmaxf(b->size.x * 0.25f, 1e-3f)); if (steps > 64) steps = 64;
    for (int k = 0; k <= steps; k++) {
        arc_v3 c = ph_vadd(p0, ph_vmul(ph_vsub(p1, p0), (float)k / (float)steps));
        if (arc_ray_sphere(o, d, c, b->size.x, best, &hh) && hh.t < best) { best = hh.t; bn = hh.normal; hit = true; }
    }
    if (hit) { *t = best; *n = bn; }
    return hit;
}
bool arc_physics_raycast(arc_physics h, arc_v3 o, arc_v3 d, float max_t, uint32_t mask, arc_physics_hit *hit) {
    ph_world *w = ph_get(h); if (!w) return false;
    if (!mask) mask = 0xFFFFFFFFu;
    float best = max_t; int bb = -1; arc_v3 bn = ph_v3(0, 1, 0);
    for (int i = 1; i < w->max_bodies; i++) {
        ph_body *b = &w->b[i]; if (!b->used || !(b->layer & mask)) continue;
        float t; arc_v3 n;
        if (ph_ray_body(b, o, d, best, &t, &n) && t < best) { best = t; bb = i; bn = n; }
    }
    for (int p = 0; p < w->nplanes; p++) {
        float dn = ph_vdot(w->planes[p].n, d); if (dn >= 0) continue;
        float t = (w->planes[p].d - ph_vdot(w->planes[p].n, o)) / dn;
        if (t >= 0 && t < best) { best = t; bb = 0; bn = w->planes[p].n; }
    }
    for (int k = 0; k < w->ntris; k++) {
        arc_hit hh; const ph_tri *tr = &w->tris[k];
        if (ph_vdot(tr->n, d) < 0 && arc_ray_triangle(o, d, tr->a, tr->b, tr->c, best, &hh) && hh.t < best) { best = hh.t; bb = 0; bn = tr->n; }
    }
    if (bb < 0) return false;
    if (hit) { hit->body = bb; hit->t = best; hit->point = ph_vadd(o, ph_vmul(d, best)); hit->normal = bn; }
    return true;
}
int arc_physics_overlap_sphere(arc_physics h, arc_v3 c, float r, uint32_t mask, int *out, int max) {
    ph_world *w = ph_get(h); if (!w) return 0;
    if (!mask) mask = 0xFFFFFFFFu;
    int n = 0;
    ph_margin = 0;
    ph_body probe; memset(&probe, 0, sizeof probe); probe.shape = ARC_SHAPE_SPHERE; probe.size = ph_v3(r, r, r); probe.pos = c; probe.q = (arc_quat){ 0, 0, 0, 1 };
    for (int i = 1; i < w->max_bodies; i++) {
        ph_body *b = &w->b[i]; if (!b->used || !(b->layer & mask)) continue;
        ph_contact ct = { 0 };
        if (ph_collide(&probe, b, &ct)) { if (out && n < max) out[n] = i; n++; }
    }
    return n;
}
int arc_physics_events(arc_physics h, arc_physics_event *out, int max) {
    ph_world *w = ph_get(h); if (!w) return 0;
    for (int i = 0; i < w->nev && i < max && out; i++) out[i] = w->ev[i];
    return w->nev;
}

/* ---- state ---- */
#define PH_STATE_MAGIC 0x50485333u   /* "PHS3" (0.9.7: joints after the contacts; 0.9.8: ball joint cone and twist) */
size_t arc_physics_save(arc_physics h, void *buf, size_t cap) {
    ph_world *w = ph_get(h); if (!w) return 0;
    size_t need = 16 + sizeof(ph_body) * (size_t)w->max_bodies + sizeof(ph_manifold) * (size_t)w->nold + sizeof(ph_joint) * (size_t)w->nj;
    if (buf && cap >= need) {
        uint8_t *p = (uint8_t *)buf; uint32_t hd[4] = { PH_STATE_MAGIC, (uint32_t)w->max_bodies, (uint32_t)w->nold, (uint32_t)w->nj };
        memcpy(p, hd, 16); p += 16;
        memcpy(p, w->b, sizeof(ph_body) * (size_t)w->max_bodies); p += sizeof(ph_body) * (size_t)w->max_bodies;
        if (w->nold) memcpy(p, w->old, sizeof(ph_manifold) * (size_t)w->nold);
        p += sizeof(ph_manifold) * (size_t)w->nold;
        if (w->nj) memcpy(p, w->j, sizeof(ph_joint) * (size_t)w->nj);
    }
    return need;
}
bool arc_physics_load(arc_physics h, const void *buf, size_t size) {
    ph_world *w = ph_get(h); if (!w || !buf || size < 16) return false;
    uint32_t hd[4]; memcpy(hd, buf, 16);
    if (hd[0] != PH_STATE_MAGIC || (int)hd[1] != w->max_bodies) return false;
    size_t need = 16 + sizeof(ph_body) * (size_t)w->max_bodies + sizeof(ph_manifold) * (size_t)hd[2] + sizeof(ph_joint) * (size_t)hd[3];
    if (size != need) return false;
    const uint8_t *p = (const uint8_t *)buf + 16;
    memcpy(w->b, p, sizeof(ph_body) * (size_t)w->max_bodies); p += sizeof(ph_body) * (size_t)w->max_bodies;
    ph_ensure_m(w, (int)hd[2] + 1);
    w->nold = (int)hd[2]; if (w->nold) memcpy(w->old, p, sizeof(ph_manifold) * (size_t)w->nold);
    p += sizeof(ph_manifold) * (size_t)w->nold;
    if ((int)hd[3] > w->cj) { w->cj = (int)hd[3]; w->j = (ph_joint *)realloc(w->j, sizeof(ph_joint) * (size_t)w->cj); }
    w->nj = (int)hd[3]; if (w->nj) memcpy(w->j, p, sizeof(ph_joint) * (size_t)w->nj);
    w->nm = 0; w->nev = 0;
    return true;
}

/* ---- debug drawing ---- */
void arc_physics_draw(arc_physics h, uint32_t rgb) {
    ph_world *w = ph_get(h); if (!w) return;
    uint32_t dim = (rgb >> 1) & 0x7F7F7F;
    for (int i = 1; i < w->max_bodies; i++) {
        ph_body *b = &w->b[i]; if (!b->used) continue;
        uint32_t c = b->awake || b->type != ARC_BODY_DYNAMIC ? rgb : dim;
        arc_v3 ax[3]; ph_qmat(b->q, ax);
        if (b->shape == ARC_SHAPE_BOX) {
            arc_v3 P[8]; for (int k = 0; k < 8; k++) P[k] = ph_vadd(b->pos, ph_to_world(ax, ph_v3(k & 1 ? b->size.x : -b->size.x, k & 2 ? b->size.y : -b->size.y, k & 4 ? b->size.z : -b->size.z)));
            static const int E[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
            for (int e = 0; e < 12; e++) arc_draw_line(P[E[e][0]], P[E[e][1]], c);
        } else {
            arc_v3 p0 = b->pos, p1 = b->pos; if (b->shape == ARC_SHAPE_CAPSULE) ph_capsule_seg(b, &p0, &p1);
            float r = b->size.x;
            /* three rings at each end (polygons from a fixed table: no trig at run time) */
            static const float C16[17][2] = { { 1, 0 }, { 0.92388f, 0.38268f }, { 0.70711f, 0.70711f }, { 0.38268f, 0.92388f }, { 0, 1 }, { -0.38268f, 0.92388f }, { -0.70711f, 0.70711f }, { -0.92388f, 0.38268f }, { -1, 0 },
                                              { -0.92388f, -0.38268f }, { -0.70711f, -0.70711f }, { -0.38268f, -0.92388f }, { 0, -1 }, { 0.38268f, -0.92388f }, { 0.70711f, -0.70711f }, { 0.92388f, -0.38268f }, { 1, 0 } };
            for (int e = 0; e < 2; e++) {
                arc_v3 cc = e ? p1 : p0;
                for (int ring = 0; ring < 3; ring++) {
                    arc_v3 u = ax[(ring + 1) % 3], v = ax[(ring + 2) % 3];
                    for (int k = 0; k < 16; k++) arc_draw_line(ph_vadd(cc, ph_vadd(ph_vmul(u, C16[k][0] * r), ph_vmul(v, C16[k][1] * r))), ph_vadd(cc, ph_vadd(ph_vmul(u, C16[k + 1][0] * r), ph_vmul(v, C16[k + 1][1] * r))), c);
                }
                if (b->shape != ARC_SHAPE_CAPSULE) break;
            }
            if (b->shape == ARC_SHAPE_CAPSULE) for (int k = 0; k < 4; k++) { arc_v3 o = k == 0 ? ax[0] : k == 1 ? ph_vmul(ax[0], -1) : k == 2 ? ax[2] : ph_vmul(ax[2], -1); arc_draw_line(ph_vadd(p0, ph_vmul(o, r)), ph_vadd(p1, ph_vmul(o, r)), c); }
        }
    }
    for (int k = 1; k < w->nj; k++) {   /* 0.9.7 joints: the pivot (a small cross) and a hinge's axis, lines to each body's centre */
        ph_joint *J = &w->j[k]; if (!J->used) continue;
        ph_body *A = ph_jbody(w, J->a), *B = ph_jbody(w, J->b);
        arc_v3 axB[3]; ph_qmat(B->q, axB); arc_v3 p = ph_vadd(B->pos, ph_to_world(axB, J->lb));
        uint32_t c = 0xFFD040; float s = 0.08f;
        arc_draw_line(ph_vsub(p, ph_v3(s, 0, 0)), ph_vadd(p, ph_v3(s, 0, 0)), c); arc_draw_line(ph_vsub(p, ph_v3(0, s, 0)), ph_vadd(p, ph_v3(0, s, 0)), c); arc_draw_line(ph_vsub(p, ph_v3(0, 0, s)), ph_vadd(p, ph_v3(0, 0, s)), c);
        if (J->type == ARC_JOINT_HINGE) { arc_v3 a = ph_vmul(ph_vnorm(ph_to_world(axB, J->axb)), 0.4f); arc_draw_line(ph_vsub(p, a), ph_vadd(p, a), 0xFF8020); }
        if (J->type == ARC_JOINT_BALL && J->cone > 0) {   /* 0.9.8: the cone's centre and its rim (8 spokes), on the base */
            arc_v3 axA[3]; ph_qmat(A->q, axA); arc_v3 cA = ph_vnorm(ph_to_world(axA, J->cna)), t1, t2; ph_tangents(cA, &t1, &t2);
            float L = 0.25f, cs = arc_cosf(J->cone), sn = arc_sinf(J->cone);
            arc_draw_line(p, ph_vadd(p, ph_vmul(cA, L)), 0xFF8020);
            for (int i = 0; i < 8; i++) { float an = (float)i * 0.785398f; arc_v3 r = ph_vadd(ph_vmul(t1, arc_cosf(an)), ph_vmul(t2, arc_sinf(an)));
                arc_draw_line(p, ph_vadd(p, ph_vmul(ph_vadd(ph_vmul(cA, cs), ph_vmul(r, sn)), L)), 0x905020); }
        }
        arc_draw_line(p, B->pos, 0x806820); if (J->a) arc_draw_line(p, A->pos, 0x806820);
    }
}

/* ---- 0.9.8 ragdolls ----------------------------------------------------------------------------------------------
   Eleven capsules (pelvis, chest, head, upper and lower arms, thighs and shins) on joints with human limits: ball
   joints with cones and twist at the waist, neck, shoulders and hips, hinges at the elbows and knees. Built from 17
   key points, which come from a skeleton's pose (joints found by their names: mixamo, Unreal and Blender styles) or
   from a height (arc_ragdoll_humanoid). Everything is + - * / and sqrt, so ragdolls stay deterministic. */
enum { RG_HIPS, RG_SPINE, RG_NECK, RG_HEAD, RG_TOP, RG_SHL, RG_ELBL, RG_WRL, RG_SHR, RG_ELBR, RG_WRR,
       RG_HIPL, RG_KNEEL, RG_ANKL, RG_HIPR, RG_KNEER, RG_ANKR, RG_KP };
static const float rg_mass[ARC_RAG_PARTS] = { 0.15f, 0.28f, 0.08f, 0.03f, 0.025f, 0.03f, 0.025f, 0.11f, 0.06f, 0.11f, 0.06f };
static const float rg_rad[ARC_RAG_PARTS] = { 0.065f, 0.07f, 0.06f, 0.028f, 0.025f, 0.028f, 0.025f, 0.042f, 0.032f, 0.042f, 0.032f };
static const int rg_parent[ARC_RAG_PARTS] = { -1, ARC_RAG_PELVIS, ARC_RAG_CHEST, ARC_RAG_CHEST, ARC_RAG_UPPER_ARM_L, ARC_RAG_CHEST,
                                              ARC_RAG_UPPER_ARM_R, ARC_RAG_PELVIS, ARC_RAG_THIGH_L, ARC_RAG_PELVIS, ARC_RAG_THIGH_R };
static arc_quat rg_quat_basis(arc_v3 x, arc_v3 y, arc_v3 z) {   /* columns x y z (orthonormal) to a quaternion, sqrt only */
    float tr = x.x + y.y + z.z; arc_quat q;
    if (tr > 0) { float s = sqrtf(tr + 1.0f) * 2; q = (arc_quat){ (y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, 0.25f * s }; }
    else if (x.x > y.y && x.x > z.z) { float s = sqrtf(1.0f + x.x - y.y - z.z) * 2; q = (arc_quat){ 0.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s }; }
    else if (y.y > z.z) { float s = sqrtf(1.0f + y.y - x.x - z.z) * 2; q = (arc_quat){ (y.x + x.y) / s, 0.25f * s, (z.y + y.z) / s, (z.x - x.z) / s }; }
    else { float s = sqrtf(1.0f + z.z - x.x - y.y) * 2; q = (arc_quat){ (z.x + x.z) / s, (z.y + y.z) / s, 0.25f * s, (x.y - y.x) / s }; }
    float l = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); return (arc_quat){ q.x / l, q.y / l, q.z / l, q.w / l };
}
/* the part's segment: from p0 to p1 (the capsule runs along it) */
static void rg_segment(const arc_v3 *k, int part, arc_v3 *p0, arc_v3 *p1) {
    switch (part) {
    case ARC_RAG_PELVIS: *p0 = k[RG_HIPL]; *p1 = k[RG_HIPR]; break;   /* across the hips */
    case ARC_RAG_CHEST: *p0 = k[RG_SPINE]; *p1 = k[RG_NECK]; break;
    case ARC_RAG_HEAD: *p0 = k[RG_HEAD]; *p1 = k[RG_TOP]; break;
    case ARC_RAG_UPPER_ARM_L: *p0 = k[RG_SHL]; *p1 = k[RG_ELBL]; break;
    case ARC_RAG_FOREARM_L: *p0 = k[RG_ELBL]; *p1 = ph_vadd(k[RG_WRL], ph_vmul(ph_vsub(k[RG_WRL], k[RG_ELBL]), 0.35f)); break;   /* the hand too */
    case ARC_RAG_UPPER_ARM_R: *p0 = k[RG_SHR]; *p1 = k[RG_ELBR]; break;
    case ARC_RAG_FOREARM_R: *p0 = k[RG_ELBR]; *p1 = ph_vadd(k[RG_WRR], ph_vmul(ph_vsub(k[RG_WRR], k[RG_ELBR]), 0.35f)); break;
    case ARC_RAG_THIGH_L: *p0 = k[RG_HIPL]; *p1 = k[RG_KNEEL]; break;
    case ARC_RAG_SHIN_L: *p0 = k[RG_KNEEL]; *p1 = ph_vadd(k[RG_ANKL], ph_vmul(ph_vsub(k[RG_ANKL], k[RG_KNEEL]), 0.12f)); break;
    case ARC_RAG_THIGH_R: *p0 = k[RG_HIPR]; *p1 = k[RG_KNEER]; break;
    default: *p0 = k[RG_KNEER]; *p1 = ph_vadd(k[RG_ANKR], ph_vmul(ph_vsub(k[RG_ANKR], k[RG_KNEER]), 0.12f)); break;
    }
}
static float rg_bend(arc_v3 ud, arc_v3 fd, arc_v3 *axis, arc_v3 fallback) {   /* a hinge's axis and its bend now (radians) */
    arc_v3 c = ph_vcross(ud, fd); float s = ph_vlen(c);
    if (s > 0.2f) { *axis = ph_vmul(c, 1.0f / s); return ph_atan2(s, ph_vdot(ud, fd)); }
    *axis = ph_vnorm(fallback); return ph_atan2(ph_vdot(c, *axis), ph_vdot(ud, fd));
}
static bool rg_build(arc_physics h, const arc_v3 *k, const arc_v3 *kprev, float prev_dt, const arc_ragdoll_desc *dd, arc_ragdoll *r) {
    ph_world *w = ph_get(h); if (!w || !r) return false;
    arc_ragdoll_desc d = dd ? *dd : (arc_ragdoll_desc){ 0 };
    float mass = d.mass > 0 ? d.mass : 70, rs = d.radius_scale > 0 ? d.radius_scale : 1, fr = d.friction > 0 ? d.friction : 0.7f;
    uint32_t layer = d.layer ? d.layer : 0x40000000u, mask = d.mask ? d.mask : ~layer;
    arc_v3 up = ph_vnorm(ph_vsub(k[RG_NECK], k[RG_HIPS]));
    arc_v3 right = ph_vsub(k[RG_HIPR], k[RG_HIPL]); right = ph_vsub(right, ph_vmul(up, ph_vdot(right, up)));
    if (ph_vlen(right) < 1e-4f) { right = ph_vsub(k[RG_SHR], k[RG_SHL]); right = ph_vsub(right, ph_vmul(up, ph_vdot(right, up))); }
    right = ph_vnorm(right); arc_v3 fwd = ph_vcross(up, right);
    float H = ph_vlen(ph_vsub(k[RG_TOP], k[RG_ANKL])); if (H < 0.05f) { arc_log("arc_ragdoll: the key points are too close together"); return false; }
    int nb = 0; for (int i = 1; i < w->max_bodies; i++) nb += !w->b[i].used;
    if (nb < ARC_RAG_PARTS) { arc_log("arc_ragdoll: the physics world needs %d free bodies (has %d)", ARC_RAG_PARTS, nb); return false; }
    memset(r->body, 0, sizeof r->body); memset(r->joint, 0, sizeof r->joint);
    for (int p = 0; p < ARC_RAG_PARTS; p++) {
        arc_v3 p0, p1; rg_segment(k, p, &p0, &p1);
        arc_v3 ax = ph_vsub(p1, p0); float L = ph_vlen(ax); arc_v3 yd = L > 1e-5f ? ph_vmul(ax, 1.0f / L) : up;
        float rad = rg_rad[p] * H * rs, half = fmaxf(L * 0.5f - rad * 0.5f, 0.01f);
        if (p == ARC_RAG_HEAD) half = fmaxf(L * 0.5f - rad, 0.01f);
        arc_v3 xd = ph_vsub(p == ARC_RAG_PELVIS ? fwd : right, ph_vmul(yd, ph_vdot(p == ARC_RAG_PELVIS ? fwd : right, yd)));
        if (ph_vlen(xd) < 1e-3f) xd = ph_vsub(fwd, ph_vmul(yd, ph_vdot(fwd, yd)));
        xd = ph_vnorm(xd); arc_v3 zd = ph_vcross(xd, yd);
        arc_v3 c = ph_vmul(ph_vadd(p0, p1), 0.5f), v = d.vel;
        if (kprev && prev_dt > 0) { arc_v3 q0, q1; rg_segment(kprev, p, &q0, &q1); v = ph_vadd(v, ph_vmul(ph_vsub(c, ph_vmul(ph_vadd(q0, q1), 0.5f)), 1.0f / prev_dt)); }
        arc_body_desc bd = { .type = ARC_BODY_DYNAMIC, .shape = ARC_SHAPE_CAPSULE, .size = { rad, half, 0 }, .pos = c, .rot = rg_quat_basis(xd, yd, zd),
                             .vel = v, .mass = mass * rg_mass[p] / 0.96f, .friction = fr, .linear_damping = 0.05f, .angular_damping = 0.4f,
                             .rolling_friction = 0.2f, .layer = layer, .mask = mask, .user = d.user };
        r->body[p] = arc_physics_add(h, &bd); r->radius[p] = rad; r->half[p] = half;
    }
    /* joints: child a, parent b; ball axes are the child's own direction, cones centred where the limb rests */
    #define RG_DIR(p) (ph_vnorm(ph_vsub(segp1[p], segp0[p])))
    arc_v3 segp0[ARC_RAG_PARTS], segp1[ARC_RAG_PARTS]; for (int p = 0; p < ARC_RAG_PARTS; p++) rg_segment(k, p, &segp0[p], &segp1[p]);
    for (int p = 1; p < ARC_RAG_PARTS; p++) {
        arc_joint_desc jd = { .type = ARC_JOINT_BALL, .a = r->body[p], .b = r->body[rg_parent[p]], .anchor = segp0[p], .axis = RG_DIR(p) };
        float side = (p == ARC_RAG_UPPER_ARM_L || p == ARC_RAG_THIGH_L) ? -1.0f : 1.0f;
        switch (p) {
        case ARC_RAG_CHEST: jd.cone = 0.55f; jd.cone_axis = up; jd.limit = true; jd.lower = -0.5f; jd.upper = 0.5f; break;
        case ARC_RAG_HEAD: jd.anchor = k[RG_NECK]; jd.cone = 0.7f; jd.cone_axis = up; jd.limit = true; jd.lower = -0.9f; jd.upper = 0.9f; break;
        case ARC_RAG_UPPER_ARM_L: case ARC_RAG_UPPER_ARM_R:
            jd.cone = 1.55f; jd.cone_axis = ph_vadd(ph_vadd(ph_vmul(right, 0.7f * side), ph_vmul(up, -0.55f)), ph_vmul(fwd, 0.35f));
            jd.limit = true; jd.lower = -1.2f; jd.upper = 1.2f; break;
        case ARC_RAG_THIGH_L: case ARC_RAG_THIGH_R:
            jd.cone = 1.1f; jd.cone_axis = ph_vadd(ph_vadd(ph_vmul(up, -1), ph_vmul(fwd, 0.5f)), ph_vmul(right, 0.15f * side));
            jd.limit = true; jd.lower = -0.5f; jd.upper = 0.5f; break;
        default: {   /* elbows and knees: hinges that bend one way, up to about 150 degrees from straight */
            arc_v3 ud = RG_DIR(rg_parent[p]), fd = RG_DIR(p), axis;
            bool knee = p == ARC_RAG_SHIN_L || p == ARC_RAG_SHIN_R;
            float bend = rg_bend(ud, fd, &axis, knee ? ph_vcross(fwd, ud) : ph_vcross(ud, fwd));
            jd.type = ARC_JOINT_HINGE; jd.axis = axis; jd.limit = true; jd.lower = -bend; jd.upper = 2.6f - bend; break; }
        }
        r->joint[p] = arc_physics_add_joint(h, &jd);
    }
    #undef RG_DIR
    r->joints = 0;
    return true;
}
static int rg_find(arc_anim a, int n, int kind, int side) {   /* the first joint of a kind (and side: 0 any, 1 left, 2 right) */
    for (int j = 0; j < n; j++) {
        char nm[64]; const char *src = arc_anim_joint_name(a, j); const char *colon = strrchr(src, ':'); if (colon) src = colon + 1;
        size_t L = 0; for (; src[L] && L < 63; L++) nm[L] = (char)(src[L] >= 'A' && src[L] <= 'Z' ? src[L] + 32 : src[L]); nm[L] = 0;
        bool left = strstr(nm, "left") || !strncmp(nm, "l_", 2) || !strncmp(nm, "l.", 2) || (L > 2 && (!strcmp(nm + L - 2, "_l") || !strcmp(nm + L - 2, ".l")));
        bool rightn = strstr(nm, "right") || !strncmp(nm, "r_", 2) || !strncmp(nm, "r.", 2) || (L > 2 && (!strcmp(nm + L - 2, "_r") || !strcmp(nm + L - 2, ".r")));
        if ((side == 1 && !left) || (side == 2 && !rightn)) continue;
        bool fore = strstr(nm, "forearm") || strstr(nm, "lowerarm") || strstr(nm, "elbow");
        bool thigh = strstr(nm, "upleg") || strstr(nm, "thigh") || strstr(nm, "upperleg");
        bool ok = false;
        switch (kind) {
        case RG_HIPS: ok = strstr(nm, "hips") || strstr(nm, "pelvis"); break;
        case RG_SPINE: ok = strstr(nm, "spine") || strstr(nm, "chest"); break;
        case RG_NECK: ok = strstr(nm, "neck") != NULL; break;
        case RG_HEAD: ok = strstr(nm, "head") && !strstr(nm, "end") && !strstr(nm, "top"); break;
        case RG_TOP: ok = strstr(nm, "head") && (strstr(nm, "end") || strstr(nm, "top")); break;
        case RG_SHL: ok = strstr(nm, "arm") && !fore && !strstr(nm, "twist"); break;
        case RG_ELBL: ok = fore && !strstr(nm, "twist"); break;
        case RG_WRL: ok = strstr(nm, "hand") || strstr(nm, "wrist"); break;
        case RG_HIPL: ok = thigh; break;
        case RG_KNEEL: ok = !thigh && (strstr(nm, "leg") || strstr(nm, "calf") || strstr(nm, "shin") || strstr(nm, "knee")); break;
        case RG_ANKL: ok = strstr(nm, "foot") || strstr(nm, "ankle"); break;
        }
        if (ok) return j;
    }
    return -1;
}
static bool rg_keys(arc_anim a, const arc_pose *pose, arc_m4 model, arc_v3 *k, int *src, bool quiet) {
    int n = arc_anim_joint_count(a); if (n <= 0 || !pose) return false;
    static const int kinds[RG_KP] = { RG_HIPS, RG_SPINE, RG_NECK, RG_HEAD, RG_TOP, RG_SHL, RG_ELBL, RG_WRL, RG_SHL, RG_ELBL, RG_WRL, RG_HIPL, RG_KNEEL, RG_ANKL, RG_HIPL, RG_KNEEL, RG_ANKL };
    static const int sides[RG_KP] = { 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 1, 1, 1, 2, 2, 2 };
    static const char *what[RG_KP] = { "hips", "spine", "neck", "head", "head end", "left arm", "left forearm", "left hand", "right arm", "right forearm", "right hand",
                                       "left thigh", "left shin", "left foot", "right thigh", "right shin", "right foot" };
    for (int i = 0; i < RG_KP; i++) {
        src[i] = rg_find(a, n, kinds[i], sides[i]);
        if (src[i] < 0 && i == RG_NECK) src[i] = src[RG_HEAD];
        if (src[i] < 0 && i != RG_TOP) { if (!quiet) arc_log("arc_ragdoll_from_pose: no %s joint in the skeleton (names like mixamo's Hips, Spine, Neck, Head, LeftArm, LeftForeArm, LeftHand, LeftUpLeg, LeftLeg, LeftFoot)", what[i]); return false; }
        if (src[i] >= 0) k[i] = arc_m4_mul_point(model, arc_pose_joint_pos(pose, src[i]));
    }
    if (src[RG_TOP] < 0) k[RG_TOP] = ph_vadd(k[RG_HEAD], ph_vmul(ph_vsub(k[RG_HEAD], k[RG_NECK]), 1.6f));   /* no head end joint: guess the crown */
    return true;
}
bool arc_ragdoll_from_pose(arc_physics h, arc_anim a, const arc_pose *pose, arc_m4 model, const arc_ragdoll_desc *d, arc_ragdoll *r) {
    arc_v3 k[RG_KP], kp[RG_KP]; int src[RG_KP], s2[RG_KP];
    if (!r || !rg_keys(a, pose, model, k, src, false)) return false;
    bool prev = d && d->prev && d->prev_dt > 0 && rg_keys(a, d->prev, model, kp, s2, true);
    if (!rg_build(h, k, prev ? kp : NULL, prev ? d->prev_dt : 0, d, r)) return false;
    /* which part moves each skeleton joint: a part's own start joint, else its parent's part (the root: the pelvis) */
    static const int start_kp[ARC_RAG_PARTS] = { RG_HIPS, RG_SPINE, RG_HEAD, RG_SHL, RG_ELBL, RG_SHR, RG_ELBR, RG_HIPL, RG_KNEEL, RG_HIPR, RG_KNEER };
    int n = arc_anim_joint_count(a); if (n > ARC_MAX_JOINTS) n = ARC_MAX_JOINTS;
    r->joints = n;
    for (int j = 0; j < n; j++) {
        int own = -1; for (int p = 0; p < ARC_RAG_PARTS; p++) if (src[start_kp[p]] == j) own = p;
        int par = arc__anim_parent(a, j);
        r->owner[j] = (int8_t)(own >= 0 ? own : par >= 0 ? r->owner[par] : ARC_RAG_PELVIS);
        arc_m4 b0 = arc_physics_matrix(h, r->body[r->owner[j]]);
        r->off[j] = arc_m4_mul(arc_m4_inverse(b0), arc_m4_mul(model, pose->joint[j]));
    }
    return true;
}
bool arc_ragdoll_humanoid(arc_physics h, arc_v3 feet, float yaw, float height, const arc_ragdoll_desc *d, arc_ragdoll *r) {
    if (!r || height <= 0.1f) return false;
    arc_v3 f = { arc_sinf(yaw), 0, arc_cosf(yaw) }, rt = { -f.z, 0, f.x }, u = { 0, 1, 0 };   /* forward, the character's right, up */
    static const float P[RG_KP][3] = {   /* (right, up, forward) in heights */
        { 0, 0.53f, 0 }, { 0, 0.62f, 0 }, { 0, 0.82f, 0 }, { 0, 0.87f, 0.01f }, { 0, 1.0f, 0.01f },
        { -0.10f, 0.80f, 0 }, { -0.12f, 0.63f, 0.01f }, { -0.12f, 0.47f, 0.03f }, { 0.10f, 0.80f, 0 }, { 0.12f, 0.63f, 0.01f }, { 0.12f, 0.47f, 0.03f },
        { -0.055f, 0.52f, 0 }, { -0.06f, 0.28f, 0.01f }, { -0.06f, 0.05f, 0 }, { 0.055f, 0.52f, 0 }, { 0.06f, 0.28f, 0.01f }, { 0.06f, 0.05f, 0 } };
    arc_v3 k[RG_KP];
    for (int i = 0; i < RG_KP; i++) k[i] = ph_vadd(feet, ph_vmul(ph_vadd(ph_vadd(ph_vmul(rt, P[i][0]), ph_vmul(u, P[i][1])), ph_vmul(f, P[i][2])), height));
    return rg_build(h, k, NULL, 0, d, r);
}
void arc_ragdoll_pose(arc_physics h, const arc_ragdoll *r, arc_m4 model, arc_pose *out) {
    if (!r || !out) return;
    arc_m4 inv = arc_m4_inverse(model);
    out->count = r->joints;
    for (int j = 0; j < r->joints; j++) out->joint[j] = arc_m4_mul(inv, arc_m4_mul(arc_physics_matrix(h, r->body[r->owner[j]]), r->off[j]));
}
void arc_ragdoll_remove(arc_physics h, arc_ragdoll *r) {
    if (!r) return;
    for (int p = 0; p < ARC_RAG_PARTS; p++) if (r->body[p]) arc_physics_remove(h, r->body[p]);   /* its joints go with the bodies */
    memset(r->body, 0, sizeof r->body); memset(r->joint, 0, sizeof r->joint);
}
void arc_ragdoll_draw(arc_physics h, const arc_ragdoll *r, arc_mesh capsule, uint32_t tint) {   /* each part as a scaled mesh (a unit cylinder or capsule) */
    if (!r) return;
    for (int p = 0; p < ARC_RAG_PARTS; p++) if (r->body[p]) {
        float rr = r->radius[p] * 2, l = (r->half[p] + r->radius[p]) * 2;
        arc_draw_mesh(capsule, arc_m4_mul(arc_physics_matrix(h, r->body[p]), arc_m4_scale(arc_v3_make(rr, l, rr))), tint);
    }
}
