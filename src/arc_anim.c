/* ARC skeletal animation: skeletons and clips (.arca), clip blending, poses, skinning matrices.
 *
 * .arca v1 (little endian):
 *   "ARCA" u32 version(1) u32 joint_count u32 clip_count
 *   f32 root[16]                       column-major; placed above the root joints (the model's normalisation)
 *   joint_count x { char name[32]; i32 parent (-1 = root; parents come before children);
 *                   f32 inverse_bind[16]; f32 t[3], r[4] (x,y,z,w), s[3] (rest pose) }
 *   clip_count  x { char name[32]; f32 fps; u32 frames; u32 flags (1 = made to loop);
 *                   frames x joint_count x { f32 t[3], r[4], s[3] } }
 * Clips are resampled at a fixed rate by tools/mesh2arcm.py, so playback is a lerp between two frames.
 * A clip lasts (frames - 1) / fps seconds; looping clips end on the pose they start with. */
#include "arc_internal.h"

#define ARC__MAX_ANIMS 64
typedef struct { float t[3], r[4], s[3]; } anim_trs;      /* 40 bytes, as in the file */
typedef struct { char name[32]; float fps, duration; int frames; bool loop; const anim_trs *keys; } anim_clip;
typedef struct {
    bool used; int joints, clips;
    arc_m4 root;
    char (*names)[32]; int *parent; arc_m4 *inv_bind; anim_trs *rest;
    anim_clip *clip;
    uint8_t *blob;                                         /* the file: clip keys point into it */
} anim_rec;
static anim_rec g_anims[ARC__MAX_ANIMS];

static anim_rec *anim_get(arc_anim a) { return a.id && a.id < ARC__MAX_ANIMS && g_anims[a.id].used ? &g_anims[a.id] : NULL; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

arc_anim arc_anim_from_memory(const uint8_t *d, size_t n) {
    arc_anim none = { 0 };
    if (!d || n < 16 + 64 || memcmp(d, "ARCA", 4)) { arc_log("not an .arca animation"); return none; }
    if (rd32(d + 4) != 1) { arc_log(".arca version %u is not supported (this ARC reads 1)", rd32(d + 4)); return none; }
    uint32_t nj = rd32(d + 8), nc = rd32(d + 12);
    if (nj == 0 || nj > ARC_MAX_JOINTS) { arc_log(".arca has %u joints (1..%d allowed)", nj, ARC_MAX_JOINTS); return none; }
    if (nc > 4096) { arc_log(".arca has too many clips"); return none; }
    size_t at = 16 + 64 + (size_t)nj * 140;
    if (at > n) { arc_log("truncated .arca"); return none; }
    /* walk the clips to check sizes before keeping anything */
    size_t walk = at;
    for (uint32_t c = 0; c < nc; c++) {
        if (walk + 44 > n) { arc_log("truncated .arca"); return none; }
        uint32_t frames = rd32(d + walk + 36);
        size_t bytes = (size_t)frames * nj * 40;
        if (frames == 0 || frames > (1u << 20) || walk + 44 + bytes > n) { arc_log("truncated .arca clip"); return none; }
        walk += 44 + bytes;
    }
    int slot = 1; while (slot < ARC__MAX_ANIMS && g_anims[slot].used) slot++;
    if (slot >= ARC__MAX_ANIMS) { arc_log("too many animations (%d)", ARC__MAX_ANIMS - 1); return none; }
    anim_rec *A = &g_anims[slot]; memset(A, 0, sizeof *A);
    A->blob = (uint8_t *)arc__xalloc(n); memcpy(A->blob, d, n);
    const uint8_t *b = A->blob;
    A->joints = (int)nj; A->clips = (int)nc;
    memcpy(A->root.m, b + 16, 64);
    A->names = (char (*)[32])arc__xalloc((size_t)nj * 32); A->parent = (int *)arc__xalloc(sizeof(int) * nj);
    A->inv_bind = (arc_m4 *)arc__xalloc(sizeof(arc_m4) * nj); A->rest = (anim_trs *)arc__xalloc(sizeof(anim_trs) * nj);
    const uint8_t *q = b + 80;
    for (uint32_t j = 0; j < nj; j++, q += 140) {
        memcpy(A->names[j], q, 32); A->names[j][31] = 0;
        int32_t par; memcpy(&par, q + 32, 4);
        A->parent[j] = (par >= 0 && (uint32_t)par < j) ? par : -1;   /* parents must come first */
        memcpy(A->inv_bind[j].m, q + 36, 64);
        memcpy(&A->rest[j], q + 100, 40);
    }
    A->clip = (anim_clip *)arc__xalloc(sizeof(anim_clip) * (nc ? nc : 1));
    at = 16 + 64 + (size_t)nj * 140;
    for (uint32_t c = 0; c < nc; c++) {
        anim_clip *C = &A->clip[c];
        memcpy(C->name, b + at, 32); C->name[31] = 0;
        memcpy(&C->fps, b + at + 32, 4); C->frames = (int)rd32(b + at + 36); C->loop = (rd32(b + at + 40) & 1) != 0;
        if (!(C->fps > 0)) C->fps = 30;
        C->duration = C->frames > 1 ? (float)(C->frames - 1) / C->fps : 0;
        C->keys = (const anim_trs *)(b + at + 44);
        at += 44 + (size_t)C->frames * nj * 40;
    }
    A->used = true;
    arc_anim r = { (uint32_t)slot }; return r;
}
arc_anim arc_anim_load(const char *path) {
    size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    arc_anim a = { 0 };
    if (d) a = arc_anim_from_memory(d, n); else arc_log("cannot load animation %s", path);
    arc_arena_pop(arc_frame(), mark);
    if (a.id) arc__hot_watch(ARC__HOT_ANIM, a.id, path);
    return a;
}
void arc_anim_destroy(arc_anim a) {
    anim_rec *A = anim_get(a); if (!A) return;
    free(A->names); free(A->parent); free(A->inv_bind); free(A->rest); free(A->clip); free(A->blob);
    memset(A, 0, sizeof *A);
}
int arc_anim_clip_count(arc_anim a) { anim_rec *A = anim_get(a); return A ? A->clips : 0; }
int arc_anim_find(arc_anim a, const char *name) {
    anim_rec *A = anim_get(a); if (!A || !name) return -1;
    for (int c = 0; c < A->clips; c++) if (!strcmp(A->clip[c].name, name)) return c;
    return -1;
}
const char *arc_anim_clip_name(arc_anim a, int c) { anim_rec *A = anim_get(a); return A && c >= 0 && c < A->clips ? A->clip[c].name : ""; }
float arc_anim_length(arc_anim a, int c) { anim_rec *A = anim_get(a); return A && c >= 0 && c < A->clips ? A->clip[c].duration : 0; }
int arc_anim_joint_count(arc_anim a) { anim_rec *A = anim_get(a); return A ? A->joints : 0; }
int arc_anim_joint(arc_anim a, const char *name) {
    anim_rec *A = anim_get(a); if (!A || !name) return -1;
    for (int j = 0; j < A->joints; j++) if (!strcmp(A->names[j], name)) return j;
    return -1;
}
const char *arc_anim_joint_name(arc_anim a, int j) { anim_rec *A = anim_get(a); return A && j >= 0 && j < A->joints ? A->names[j] : ""; }
int arc__anim_parent(arc_anim a, int j) { anim_rec *A = anim_get(a); return A && j >= 0 && j < A->joints ? A->parent[j] : -1; }   /* 0.9.8 ragdolls */

/* one clip's local transform for joint j at a time */
static void clip_sample(const anim_rec *A, const anim_clip *C, int j, float time, bool loop, anim_trs *out) {
    float t = time;
    if (C->duration <= 0) t = 0;
    else if (loop) { t = fmodf(t, C->duration); if (t < 0) t += C->duration; }
    else t = arc_clampf(t, 0, C->duration);
    float f = t * C->fps; int k = (int)floorf(f); if (k < 0) k = 0;
    if (k > C->frames - 1) k = C->frames - 1;
    int k1 = k + 1 < C->frames ? k + 1 : k; float u = f - (float)k; if (u < 0) u = 0; if (u > 1) u = 1;
    const anim_trs *a = &C->keys[(size_t)k * A->joints + j], *b = &C->keys[(size_t)k1 * A->joints + j];
    for (int i = 0; i < 3; i++) { out->t[i] = a->t[i] + (b->t[i] - a->t[i]) * u; out->s[i] = a->s[i] + (b->s[i] - a->s[i]) * u; }
    float d = a->r[0] * b->r[0] + a->r[1] * b->r[1] + a->r[2] * b->r[2] + a->r[3] * b->r[3], sg = d < 0 ? -1.0f : 1.0f, l = 0;
    for (int i = 0; i < 4; i++) { out->r[i] = a->r[i] + (b->r[i] * sg - a->r[i]) * u; l += out->r[i] * out->r[i]; }
    l = l > 1e-20f ? 1.0f / sqrtf(l) : 0; for (int i = 0; i < 4; i++) out->r[i] *= l;
    if (l == 0) out->r[3] = 1;
}
static arc_m4 trs_matrix(const anim_trs *p) {
    float x = p->r[0], y = p->r[1], z = p->r[2], w = p->r[3];
    arc_m4 m;
    m.m[0] = (1 - 2 * (y * y + z * z)) * p->s[0]; m.m[1] = (2 * (x * y + z * w)) * p->s[0]; m.m[2] = (2 * (x * z - y * w)) * p->s[0]; m.m[3] = 0;
    m.m[4] = (2 * (x * y - z * w)) * p->s[1]; m.m[5] = (1 - 2 * (x * x + z * z)) * p->s[1]; m.m[6] = (2 * (y * z + x * w)) * p->s[1]; m.m[7] = 0;
    m.m[8] = (2 * (x * z + y * w)) * p->s[2]; m.m[9] = (2 * (y * z - x * w)) * p->s[2]; m.m[10] = (1 - 2 * (x * x + y * y)) * p->s[2]; m.m[11] = 0;
    m.m[12] = p->t[0]; m.m[13] = p->t[1]; m.m[14] = p->t[2]; m.m[15] = 1;
    return m;
}
void arc_anim_pose(arc_anim a, const arc_anim_layer *layers, int count, arc_pose *out) {
    if (!out) return;
    anim_rec *A = anim_get(a);
    if (!A) { out->count = 0; return; }
    float total = 0;
    for (int i = 0; i < count; i++) if (layers[i].weight > 0 && layers[i].clip >= 0 && layers[i].clip < A->clips) total += layers[i].weight;
    out->count = A->joints;
    for (int j = 0; j < A->joints; j++) {
        anim_trs acc;
        if (total <= 0) acc = A->rest[j];
        else {
            memset(&acc, 0, sizeof acc);
            float ref[4] = { 0, 0, 0, 0 }; bool have_ref = false;
            for (int i = 0; i < count; i++) {
                const arc_anim_layer *L = &layers[i];
                if (!(L->weight > 0) || L->clip < 0 || L->clip >= A->clips) continue;
                float w = L->weight / total; anim_trs s; clip_sample(A, &A->clip[L->clip], j, L->time, L->loop, &s);
                if (!have_ref) { memcpy(ref, s.r, sizeof ref); have_ref = true; }
                float sg = (s.r[0] * ref[0] + s.r[1] * ref[1] + s.r[2] * ref[2] + s.r[3] * ref[3]) < 0 ? -w : w;   /* same hemisphere */
                for (int k = 0; k < 3; k++) { acc.t[k] += s.t[k] * w; acc.s[k] += s.s[k] * w; }
                for (int k = 0; k < 4; k++) acc.r[k] += s.r[k] * sg;
            }
            float l = sqrtf(acc.r[0] * acc.r[0] + acc.r[1] * acc.r[1] + acc.r[2] * acc.r[2] + acc.r[3] * acc.r[3]);
            if (l > 1e-12f) for (int k = 0; k < 4; k++) acc.r[k] /= l; else { acc.r[0] = acc.r[1] = acc.r[2] = 0; acc.r[3] = 1; }
        }
        arc_m4 local = trs_matrix(&acc);
        out->joint[j] = arc_m4_mul(A->parent[j] >= 0 ? out->joint[A->parent[j]] : A->root, local);
    }
}
/* skinning matrix per joint = pose joint * inverse bind, written as 3 rows of 4 */
int arc__anim_skin_rows(arc_anim a, const arc_pose *p, float *rows) {
    anim_rec *A = anim_get(a); if (!A || !p || p->count != A->joints) return 0;
    for (int j = 0; j < A->joints; j++) {
        arc_m4 s = arc_m4_mul(p->joint[j], A->inv_bind[j]);
        float *r = rows + (size_t)j * 12;
        for (int row = 0; row < 3; row++) for (int c = 0; c < 4; c++) r[row * 4 + c] = s.m[c * 4 + row];
    }
    return A->joints;
}

/* hot reload: move the record of `fresh` into `old`'s slot (handles held by the game stay valid) */
bool arc__anim_replace(arc_anim old, arc_anim fresh) {
    anim_rec *o = anim_get(old), *f = anim_get(fresh);
    if (!o || !f || o == f) return false;
    anim_rec keep = *o; *o = *f; *f = keep;
    arc_anim_destroy(fresh);
    return true;
}

/* ---- 0.9.3: animation graphs (state machines), events, two-bone IK ------------------------------------------- */
#define AG_MAX 32
#define AG_BLEND_MAX 8
typedef struct { char name[32]; int kind; } ag_param;
typedef struct { char name[32]; bool blend, loop; int clip; float speed; int param, n; int clips[AG_BLEND_MAX]; float points[AG_BLEND_MAX]; } ag_state;
typedef struct { int param; arc_anim_op op; float value; } ag_cond;
typedef struct { int from, to, nc; float fade, exit; ag_cond c[4]; } ag_trans;
typedef struct { int clip, id; float time; } ag_event;
typedef char ag_name[32];
typedef struct {
    bool used; arc_anim skel;
    int np; ag_param p[ARC_ANIM_MAX_PARAMS];
    ag_state *s; int ns, cs;
    ag_trans *t; int nt, ct;
    ag_event *e; int ne, ce;
    ag_name *en; int nen, cen;        /* event names, by id */
} ag_graph;
static ag_graph g_ag[AG_MAX];
static ag_graph *ag_get(arc_anim_graph g) { return g.id > 0 && g.id <= AG_MAX && g_ag[g.id - 1].used ? &g_ag[g.id - 1] : NULL; }
#define AG_GROW(arr, n, cap, type) do { if ((n) >= (cap)) { (cap) = (cap) ? (cap) * 2 : 8; (arr) = (type *)realloc((arr), sizeof(type) * (size_t)(cap)); } } while (0)

arc_anim_graph arc_anim_graph_create(arc_anim skeleton) {
    for (int i = 0; i < AG_MAX; i++) if (!g_ag[i].used) {
        memset(&g_ag[i], 0, sizeof g_ag[i]); g_ag[i].used = true; g_ag[i].skel = skeleton;
        return (arc_anim_graph){ (uint32_t)i + 1 };
    }
    arc_log("arc_anim_graph_create: more than %d graphs", AG_MAX);
    return (arc_anim_graph){ 0 };
}
void arc_anim_graph_destroy(arc_anim_graph h) {
    ag_graph *g = ag_get(h); if (!g) return;
    free(g->s); free(g->t); free(g->e); free(g->en); memset(g, 0, sizeof *g);
}
int arc_anim_graph_find_param(arc_anim_graph h, const char *name) {
    ag_graph *g = ag_get(h); if (!g || !name) return -1;
    for (int i = 0; i < g->np; i++) if (!strcmp(g->p[i].name, name)) return i;
    return -1;
}
int arc_anim_graph_param(arc_anim_graph h, const char *name, int kind) {
    ag_graph *g = ag_get(h); if (!g || !name) return -1;
    int i = arc_anim_graph_find_param(h, name); if (i >= 0) return i;
    if (g->np >= ARC_ANIM_MAX_PARAMS) { arc_log("arc_anim_graph_param: more than %d parameters", ARC_ANIM_MAX_PARAMS); return -1; }
    snprintf(g->p[g->np].name, 32, "%s", name); g->p[g->np].kind = kind;
    return g->np++;
}
static int ag_add_state(ag_graph *g, const char *name) {
    AG_GROW(g->s, g->ns, g->cs, ag_state);
    ag_state *s = &g->s[g->ns]; memset(s, 0, sizeof *s); snprintf(s->name, 32, "%s", name ? name : ""); s->speed = 1; s->clip = -1; s->param = -1;
    return g->ns++;
}
int arc_anim_graph_state(arc_anim_graph h, const char *name, int clip, float speed, bool loop) {
    ag_graph *g = ag_get(h); if (!g) return -1;
    if (clip < 0 || clip >= arc_anim_clip_count(g->skel)) arc_log("arc_anim_graph_state %s: no clip %d (the state shows the rest pose)", name ? name : "", clip);
    int i = ag_add_state(g, name); g->s[i].clip = clip; g->s[i].speed = speed; g->s[i].loop = loop;
    return i;
}
int arc_anim_graph_blend(arc_anim_graph h, const char *name, int param, const int *clips, const float *points, int count, bool loop) {
    ag_graph *g = ag_get(h); if (!g || !clips || !points || count < 1) return -1;
    if (count > AG_BLEND_MAX) { arc_log("arc_anim_graph_blend %s: %d clips (up to %d)", name ? name : "", count, AG_BLEND_MAX); count = AG_BLEND_MAX; }
    int i = ag_add_state(g, name); ag_state *s = &g->s[i];
    s->blend = true; s->loop = loop; s->param = param; s->n = count;
    for (int k = 0; k < count; k++) { s->clips[k] = clips[k]; s->points[k] = points[k]; }
    return i;
}
int arc_anim_graph_find_state(arc_anim_graph h, const char *name) {
    ag_graph *g = ag_get(h); if (!g || !name) return -1;
    for (int i = 0; i < g->ns; i++) if (!strcmp(g->s[i].name, name)) return i;
    return -1;
}
const char *arc_anim_graph_state_name(arc_anim_graph h, int st) { ag_graph *g = ag_get(h); return g && st >= 0 && st < g->ns ? g->s[st].name : ""; }
int arc_anim_graph_transition(arc_anim_graph h, int from, int to, float fade, float exit_time) {
    ag_graph *g = ag_get(h); if (!g || to < 0 || to >= g->ns || from < ARC_ANIM_ANY || from >= g->ns) return -1;
    AG_GROW(g->t, g->nt, g->ct, ag_trans);
    ag_trans *t = &g->t[g->nt]; memset(t, 0, sizeof *t);
    t->from = from; t->to = to; t->fade = fade > 0 ? fade : 0; t->exit = exit_time;
    return g->nt++;
}
void arc_anim_graph_when(arc_anim_graph h, int tr, int param, arc_anim_op op, float value) {
    ag_graph *g = ag_get(h); if (!g || tr < 0 || tr >= g->nt || param < 0 || param >= g->np) return;
    ag_trans *t = &g->t[tr];
    if (t->nc >= 4) { arc_log("arc_anim_graph_when: more than 4 conditions on transition %d", tr); return; }
    t->c[t->nc++] = (ag_cond){ param, op, value };
}
int arc_anim_graph_event(arc_anim_graph h, int clip, float time, const char *name) {
    ag_graph *g = ag_get(h); if (!g || !name) return -1;
    int id = -1;
    for (int i = 0; i < g->nen; i++) if (!strcmp(g->en[i], name)) { id = i; break; }
    if (id < 0) { AG_GROW(g->en, g->nen, g->cen, ag_name); snprintf(g->en[g->nen], 32, "%s", name); id = g->nen++; }
    AG_GROW(g->e, g->ne, g->ce, ag_event);
    g->e[g->ne++] = (ag_event){ clip, id, time };
    return id;
}
const char *arc_anim_graph_event_name(arc_anim_graph h, int ev) { ag_graph *g = ag_get(h); return g && ev >= 0 && ev < g->nen ? g->en[ev] : ""; }

/* ---- the animator ---- */
static float ag_clip_len(const ag_graph *g, int clip) { return arc_anim_length(g->skel, clip); }
/* a blend state's two clips and their weights at parameter value v */
static int ag_blend_pick(const ag_state *s, float v, int *ia, int *ib, float *u) {
    if (s->n == 1 || v <= s->points[0]) { *ia = *ib = 0; *u = 0; return 1; }
    if (v >= s->points[s->n - 1]) { *ia = *ib = s->n - 1; *u = 0; return 1; }
    for (int k = 0; k + 1 < s->n; k++) if (v <= s->points[k + 1]) {
        float span = s->points[k + 1] - s->points[k];
        *ia = k; *ib = k + 1; *u = span > 0 ? (v - s->points[k]) / span : 0;
        return 2;
    }
    *ia = *ib = s->n - 1; *u = 0; return 1;
}
static float ag_blend_len(const ag_graph *g, const ag_state *s, const float *param) {
    int a, b; float u; ag_blend_pick(s, s->param >= 0 ? param[s->param] : 0, &a, &b, &u);
    return ag_clip_len(g, s->clips[a]) * (1 - u) + ag_clip_len(g, s->clips[b]) * u;
}
/* the fraction of the state played (unwrapped: 2.5 = two and a half loops) */
static float ag_played(const ag_graph *g, const ag_state *s, float time) {
    if (s->blend) return time;
    float len = ag_clip_len(g, s->clip); return len > 0 ? time / len : 1.0f;
}
static void ag_advance(const ag_graph *g, const ag_state *s, const float *param, float *time, float dt) {
    if (s->blend) {
        float len = ag_blend_len(g, s, param);
        *time += len > 0 ? dt / len : 0;
        if (!s->loop && *time > 1) *time = 1;
    } else {
        *time += dt * s->speed;
        float len = ag_clip_len(g, s->clip);
        if (!s->loop && *time > len) *time = len;
        if (*time < 0 && !s->loop) *time = 0;
    }
}
static void ag_emit(arc_animator *an, int id) { if (an->nevents < ARC_ANIM_MAX_EVENTS) an->events[an->nevents++] = id; }
/* events of `clip` between clip times t0 (exclusive) and t1 (inclusive), unwrapped over loops of length len */
static void ag_events_between(const ag_graph *g, arc_animator *an, int clip, float t0, float t1, float len, bool loop) {
    if (t1 <= t0) return;
    for (int i = 0; i < g->ne; i++) {
        const ag_event *e = &g->e[i]; if (e->clip != clip) continue;
        if (!loop || len <= 0) { if (e->time > t0 && e->time <= t1) ag_emit(an, e->id); continue; }
        float k0 = floorf((t0 - e->time) / len) + 1;   /* the first loop where e->time + k * len > t0 */
        for (float k = k0; e->time + k * len <= t1 && k < k0 + 64; k++) ag_emit(an, e->id);
    }
}
static bool ag_cond_ok(const ag_cond *c, float v) {
    switch (c->op) {
    case ARC_ANIM_GT: return v > c->value;   case ARC_ANIM_GE: return v >= c->value;
    case ARC_ANIM_LT: return v < c->value;   case ARC_ANIM_LE: return v <= c->value;
    case ARC_ANIM_EQ: return v == c->value;  case ARC_ANIM_NE: return v != c->value;
    case ARC_ANIM_SET: return v != 0;
    }
    return false;
}
static void ag_go(arc_animator *an, int to, float fade) {
    an->prev = fade > 0 ? an->state : -1; an->prev_time = an->time;
    an->state = to; an->time = 0; an->state_time = 0;
    an->fade = 0; an->fade_len = fade; an->changes++;
}
void arc_animator_init(arc_animator *an, arc_anim_graph h, int start) {
    memset(an, 0, sizeof *an); an->graph = h; an->prev = -1; an->state = -1;
    ag_graph *g = ag_get(h); if (!g) return;
    an->state = start >= 0 && start < g->ns ? start : (g->ns ? 0 : -1);
}
void arc_animator_set(arc_animator *an, int p, float v) { if (p >= 0 && p < ARC_ANIM_MAX_PARAMS) an->param[p] = v; }
bool arc_animator_setf(arc_animator *an, const char *name, float v) { int p = arc_anim_graph_find_param(an->graph, name); if (p < 0) return false; an->param[p] = v; return true; }
void arc_animator_trigger(arc_animator *an, int p) { if (p >= 0 && p < ARC_ANIM_MAX_PARAMS) an->param[p] = 1; }
void arc_animator_play(arc_animator *an, int st, float fade) { ag_graph *g = ag_get(an->graph); if (g && st >= 0 && st < g->ns) ag_go(an, st, fade); }
bool arc_animator_fired(const arc_animator *an, int ev) { for (int i = 0; i < an->nevents; i++) if (an->events[i] == ev) return true; return false; }
const char *arc_animator_state_name(const arc_animator *an) { return arc_anim_graph_state_name(an->graph, an->state); }
float arc_animator_progress(const arc_animator *an) {
    ag_graph *g = ag_get(an->graph); if (!g || an->state < 0) return 0;
    const ag_state *s = &g->s[an->state]; float p = ag_played(g, s, an->time);
    return s->loop ? p - floorf(p) : arc_clampf(p, 0, 1);
}
void arc_animator_update(arc_animator *an, float dt) {
    an->nevents = 0;
    ag_graph *g = ag_get(an->graph); if (!g || an->state < 0 || an->state >= g->ns) return;
    const ag_state *s = &g->s[an->state];
    /* advance the current state and report the events it passes (a blend: those of its heavier clip) */
    float t0 = an->time;
    ag_advance(g, s, an->param, &an->time, dt);
    if (s->blend) {
        int a, b; float u; ag_blend_pick(s, s->param >= 0 ? an->param[s->param] : 0, &a, &b, &u);
        int c = s->clips[u < 0.5f ? a : b]; float len = ag_clip_len(g, c);
        ag_events_between(g, an, c, t0 * len, an->time * len, len, s->loop);
    } else ag_events_between(g, an, s->clip, t0, an->time, ag_clip_len(g, s->clip), s->loop);
    an->state_time += dt;
    if (an->prev >= 0) {
        ag_advance(g, &g->s[an->prev], an->param, &an->prev_time, dt);
        an->fade += dt; if (an->fade >= an->fade_len) an->prev = -1;
    }
    /* the first transition that passes */
    float played = ag_played(g, s, an->time);
    for (int i = 0; i < g->nt; i++) {
        const ag_trans *t = &g->t[i];
        if (!(t->from == an->state || (t->from == ARC_ANIM_ANY && t->to != an->state))) continue;
        if (t->exit >= 0 && played < t->exit) continue;
        bool ok = true;
        for (int k = 0; k < t->nc && ok; k++) ok = ag_cond_ok(&t->c[k], an->param[t->c[k].param]);
        if (!ok) continue;
        for (int k = 0; k < t->nc; k++) if (g->p[t->c[k].param].kind == ARC_ANIM_TRIGGER) an->param[t->c[k].param] = 0;
        ag_go(an, t->to, t->fade);
        break;
    }
}
/* a state's layers at a time, scaled by w; returns how many were written */
static int ag_layers(const ag_graph *g, const ag_state *s, const float *param, float time, float w, arc_anim_layer *L) {
    if (!s->blend) { L[0] = (arc_anim_layer){ s->clip, time, w, s->loop }; return 1; }
    int a, b; float u; int n = ag_blend_pick(s, s->param >= 0 ? param[s->param] : 0, &a, &b, &u);
    float ph = s->loop ? time : arc_clampf(time, 0, 1);
    L[0] = (arc_anim_layer){ s->clips[a], ph * ag_clip_len(g, s->clips[a]), w * (n == 2 ? 1 - u : 1), s->loop };
    if (n == 1) return 1;
    L[1] = (arc_anim_layer){ s->clips[b], ph * ag_clip_len(g, s->clips[b]), w * u, s->loop };
    return 2;
}
void arc_animator_pose(const arc_animator *an, arc_pose *out) {
    if (!out) return;
    ag_graph *g = ag_get(an->graph);
    if (!g || an->state < 0 || an->state >= g->ns) { arc_anim_pose(g ? g->skel : (arc_anim){ 0 }, NULL, 0, out); return; }
    arc_anim_layer L[4]; int n = 0;
    float w = an->prev >= 0 && an->fade_len > 0 ? arc_clampf(an->fade / an->fade_len, 0, 1) : 1;
    n += ag_layers(g, &g->s[an->state], an->param, an->time, w, L + n);
    if (an->prev >= 0 && w < 1) n += ag_layers(g, &g->s[an->prev], an->param, an->prev_time, 1 - w, L + n);
    arc_anim_pose(g->skel, L, n, out);
}

/* ---- two-bone IK ---- */
static void ik_rotate_subtree(const anim_rec *A, arc_pose *p, int root, arc_quat q, arc_v3 pivot) {
    arc_m4 G = arc_m4_mul(arc_m4_translate(pivot), arc_m4_mul(arc_m4_from_quat(q), arc_m4_translate(arc_v3_scale(pivot, -1))));
    bool in[ARC_MAX_JOINTS] = { false };
    for (int j = root; j < A->joints; j++) {
        in[j] = j == root || (A->parent[j] >= 0 && in[A->parent[j]]);
        if (in[j]) p->joint[j] = arc_m4_mul(G, p->joint[j]);
    }
}
static arc_v3 ik_pos(const arc_pose *p, int j) { return arc_v3_make(p->joint[j].m[12], p->joint[j].m[13], p->joint[j].m[14]); }
static float ik_angle(arc_v3 a, arc_v3 b) { return acosf(arc_clampf(arc_v3_dot(arc_v3_norm(a), arc_v3_norm(b)), -1, 1)); }
static arc_v3 ik_perp(arc_v3 v) { arc_v3 o = fabsf(v.x) < 0.9f ? arc_v3_make(1, 0, 0) : arc_v3_make(0, 1, 0); return arc_v3_norm(arc_v3_cross(v, o)); }
static arc_quat ik_partial(arc_quat q, float w) { return w >= 1 ? q : arc_quat_slerp((arc_quat){ 0, 0, 0, 1 }, q, w); }

bool arc_pose_two_bone_ik(arc_anim a, arc_pose *p, int tip, arc_v3 t, arc_v3 pole, float weight, bool keep_tip_rotation) {
    anim_rec *A = anim_get(a);
    if (!A || !p || p->count != A->joints || tip < 0 || tip >= A->joints) return false;
    int mid = A->parent[tip], up = mid >= 0 ? A->parent[mid] : -1;
    if (mid < 0 || up < 0) { arc_log("arc_pose_two_bone_ik: joint %d needs a parent and a grandparent", tip); return false; }
    arc_v3 pa = ik_pos(p, up), pb = ik_pos(p, mid), pc = ik_pos(p, tip);
    float lab = arc_v3_len(arc_v3_sub(pb, pa)), lbc = arc_v3_len(arc_v3_sub(pc, pb)), dist = arc_v3_len(arc_v3_sub(t, pa));
    bool reach = dist <= lab + lbc && dist >= fabsf(lab - lbc);
    if (weight <= 0 || lab < 1e-6f || lbc < 1e-6f) return reach;
    if (weight > 1) weight = 1;
    arc_m4 tip_before = p->joint[tip];
    const float eps = 1e-4f;
    float lat = arc_clampf(dist, fabsf(lab - lbc) + eps, lab + lbc - eps);
    /* 1. bend: the upper and middle angles that make the a-c distance lat, in the chain's plane */
    float ac_ab0 = ik_angle(arc_v3_sub(pc, pa), arc_v3_sub(pb, pa)), ba_bc0 = ik_angle(arc_v3_sub(pa, pb), arc_v3_sub(pc, pb));
    float ac_ab1 = acosf(arc_clampf((lbc * lbc - lab * lab - lat * lat) / (-2 * lab * lat), -1, 1));
    float ba_bc1 = acosf(arc_clampf((lat * lat - lab * lab - lbc * lbc) / (-2 * lab * lbc), -1, 1));
    arc_v3 axis = arc_v3_cross(arc_v3_sub(pc, pa), arc_v3_sub(pb, pa));
    if (arc_v3_len(axis) < 1e-6f) axis = arc_v3_cross(arc_v3_sub(pc, pa), arc_v3_sub(pole, pa));   /* straight limb: bend in the plane with the pole (an axis at right angles to the limb) */
    if (arc_v3_len(axis) < 1e-6f) axis = ik_perp(arc_v3_sub(pc, pa));
    axis = arc_v3_norm(axis);
    ik_rotate_subtree(A, p, up, ik_partial(arc_quat_axis_angle(axis, ac_ab1 - ac_ab0), weight), pa);
    pb = ik_pos(p, mid);
    ik_rotate_subtree(A, p, mid, ik_partial(arc_quat_axis_angle(axis, ba_bc1 - ba_bc0), weight), pb);
    /* 2. aim: turn the whole chain so the tip points at the target */
    pc = ik_pos(p, tip);
    arc_v3 ac = arc_v3_sub(pc, pa), at = arc_v3_sub(t, pa);
    if (arc_v3_len(ac) > 1e-6f && arc_v3_len(at) > 1e-6f) {
        float ang = ik_angle(ac, at);
        arc_v3 ax = arc_v3_cross(ac, at);
        if (arc_v3_len(ax) < 1e-6f) ax = ang > 1.0f ? ik_perp(ac) : arc_v3_make(0, 1, 0);
        ik_rotate_subtree(A, p, up, ik_partial(arc_quat_axis_angle(ax, ang), weight), pa);
    }
    /* 3. pole: roll the chain about the a-target line so the middle joint points toward the pole */
    arc_v3 n = arc_v3_len(at) > 1e-6f ? arc_v3_norm(at) : arc_v3_norm(arc_v3_sub(ik_pos(p, tip), pa));
    arc_v3 vb = arc_v3_sub(ik_pos(p, mid), pa), vp = arc_v3_sub(pole, pa);
    vb = arc_v3_sub(vb, arc_v3_scale(n, arc_v3_dot(vb, n))); vp = arc_v3_sub(vp, arc_v3_scale(n, arc_v3_dot(vp, n)));
    if (arc_v3_len(vb) > 1e-5f && arc_v3_len(vp) > 1e-5f) {
        float ang = ik_angle(vb, vp);
        if (arc_v3_dot(arc_v3_cross(vb, vp), n) < 0) ang = -ang;
        ik_rotate_subtree(A, p, up, ik_partial(arc_quat_axis_angle(n, ang), weight), pa);
    }
    /* 4. keep the tip's own orientation: undo the rotation the chain gave it (its joints below follow) */
    if (keep_tip_rotation) {
        arc_m4 now = p->joint[tip];
        /* rotation part only: R = now3x3 * before3x3^-1, for orthogonal frames with uniform scale = now * before^T / s^2 */
        float s2 = tip_before.m[0] * tip_before.m[0] + tip_before.m[1] * tip_before.m[1] + tip_before.m[2] * tip_before.m[2];
        if (s2 > 1e-12f) {
            arc_m4 R = arc_m4_identity();
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) {
                float v = 0; for (int k = 0; k < 3; k++) v += now.m[k * 4 + r] * tip_before.m[k * 4 + c];
                R.m[c * 4 + r] = v / s2;
            }
            arc_m4 Rinv = arc_m4_identity();   /* transpose: the inverse of a rotation */
            for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) Rinv.m[c * 4 + r] = R.m[r * 4 + c];
            arc_v3 pt = ik_pos(p, tip);
            arc_m4 G = arc_m4_mul(arc_m4_translate(pt), arc_m4_mul(Rinv, arc_m4_translate(arc_v3_scale(pt, -1))));
            bool in[ARC_MAX_JOINTS] = { false };
            for (int j = tip; j < A->joints; j++) {
                in[j] = j == tip || (A->parent[j] >= 0 && in[A->parent[j]]);
                if (in[j]) p->joint[j] = arc_m4_mul(G, p->joint[j]);
            }
        }
    }
    return reach;
}
