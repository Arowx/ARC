/* ARC core: math, RNG, memory, input, vars, JSON, profiling, files, the main loop and the agent interface. */
#include "arc_internal.h"
#include <ctype.h>
#include <errno.h>

/* ======================================================================== */
/* Math                                                                     */
/* ======================================================================== */

arc_m4 arc_m4_inverse(arc_m4 mm) {
    const float *m = mm.m; float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    arc_m4 r; if (fabsf(det) < 1e-20f) return arc_m4_identity();
    det = 1.0f / det; for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}
arc_m4 arc_m4_from_quat(arc_quat q) {
    arc_m4 r = arc_m4_identity();
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz); r.m[2] = 2 * (xz - wy);
    r.m[4] = 2 * (xy - wz); r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
    r.m[8] = 2 * (xz + wy); r.m[9] = 2 * (yz - wx); r.m[10] = 1 - 2 * (xx + yy);
    return r;
}
arc_quat arc_quat_mul(arc_quat a, arc_quat b) {
    arc_quat r = { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                   a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
    return r;
}
arc_quat arc_quat_from_to(arc_v3 from, arc_v3 to) {
    arc_v3 a = arc_v3_norm(from), b = arc_v3_norm(to);
    float d = arc_v3_dot(a, b);
    if (d < -0.999999f) {                                   /* opposite: half a turn about any perpendicular axis */
        arc_v3 ax = arc_v3_cross(arc_v3_make(1, 0, 0), a);
        if (arc_v3_len(ax) < 1e-6f) ax = arc_v3_cross(arc_v3_make(0, 1, 0), a);
        return arc_quat_axis_angle(ax, ARC_PI);
    }
    arc_v3 c = arc_v3_cross(a, b); float w = 1.0f + d, n = sqrtf(c.x * c.x + c.y * c.y + c.z * c.z + w * w);
    arc_quat q = { c.x / n, c.y / n, c.z / n, w / n }; return q;
}
arc_m4 arc_m4_chain(int count, ...) {
    va_list ap; va_start(ap, count);
    arc_m4 r = arc_m4_identity();
    for (int i = 0; i < count; i++) r = arc_m4_mul(r, va_arg(ap, arc_m4));
    va_end(ap); return r;
}
arc_quat arc_quat_slerp(arc_quat a, arc_quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { d = -d; b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; }
    float wa, wb;
    if (d > 0.9995f) { wa = 1 - t; wb = t; }
    else { float th = acosf(d), s = sinf(th); wa = sinf((1 - t) * th) / s; wb = sinf(t * th) / s; }
    arc_quat r = { a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb };
    float l = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w); r.x /= l; r.y /= l; r.z /= l; r.w /= l;
    return r;
}

/* ======================================================================== */
/* RNG (PCG32)                                                              */
/* ======================================================================== */

void arc_rng_seed(arc_rng *r, uint64_t seed, uint64_t stream) {
    r->state = 0; r->inc = (stream << 1u) | 1u; arc_rng_u32(r); r->state += seed; arc_rng_u32(r);
}
uint32_t arc_rng_u32(arc_rng *r) {
    uint64_t old = r->state; r->state = old * 6364136223846793005ULL + r->inc;
    uint32_t x = (uint32_t)(((old >> 18u) ^ old) >> 27u), rot = (uint32_t)(old >> 59u);
    return (x >> rot) | (x << ((32u - rot) & 31));   /* (32 - rot) & 31, not -rot: MSVC warns about minus on unsigned */
}
int arc_rng_range(arc_rng *r, int lo, int hi) {
    if (hi <= lo) return lo;
    uint32_t span = (uint32_t)(hi - lo) + 1u;
    return lo + (int)(arc_rng_u32(r) % span);
}
float arc_rng_float(arc_rng *r) { return (arc_rng_u32(r) >> 8) * (1.0f / 16777216.0f); }
float arc_rng_rangef(arc_rng *r, float lo, float hi) { return lo + (hi - lo) * arc_rng_float(r); }

/* 0.9.0 hints: one-time `ARC hint` lines that name a likely mistake and its fix (see AGENTS.md "Hints") */
int arc__phase;   /* ARC__PHASE_*: which game callback is running */
static struct { char key[48]; int n; } g_hint_calls[32];   /* 0.10.9: how often each hint was asked for (for the self-test) */
int arc__hint_count(const char *key) { for (int i = 0; i < 32; i++) if (g_hint_calls[i].key[0] && !strcmp(g_hint_calls[i].key, key)) return g_hint_calls[i].n; return 0; }
void arc__hint(const char *key, const char *fmt, ...) {
    for (int i = 0; i < 32; i++) {
        if (!g_hint_calls[i].key[0]) { snprintf(g_hint_calls[i].key, sizeof g_hint_calls[i].key, "%s", key); g_hint_calls[i].n = 1; break; }
        if (!strcmp(g_hint_calls[i].key, key)) { g_hint_calls[i].n++; break; }
    }
    static char seen[64][48]; static int n;
    for (int i = 0; i < n; i++) if (!strcmp(seen[i], key)) return;
    if (n < 64) { strncpy(seen[n], key, 47); seen[n][47] = 0; n++; }
    char line[1024]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    printf("ARC hint %s\n", line); fflush(stdout);
}
static void rand_in_draw(void) { if (arc__phase == ARC__PHASE_DRAW) arc__hint("rand-draw", "arc_rand_* called in draw(): the game's random sequence now depends on the frame rate and replays/multiplayer drift. Use an arc_rng of your own for visual randomness, or move this to tick()."); }
static arc_rng g_rng; static uint64_t g_seed = 1;
uint32_t arc_rand_u32(void) { rand_in_draw(); return arc_rng_u32(&g_rng); }
int      arc_rand_range(int lo, int hi) { rand_in_draw(); return arc_rng_range(&g_rng, lo, hi); }
float    arc_rand_float(void) { rand_in_draw(); return arc_rng_float(&g_rng); }
float    arc_rand_rangef(float lo, float hi) { rand_in_draw(); return arc_rng_rangef(&g_rng, lo, hi); }
uint64_t arc_seed(void) { return g_seed; }
/* 0.8.4 value noise: an integer hash per lattice point (ARC's dissolve hash, seed as the third coordinate) */
static uint32_t noise_hash(int x, int y, uint32_t seed) {
    uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ (seed * 83492791u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return h;
}
float arc_noise_value2(float x, float z, uint32_t seed) {
    float fx = floorf(x), fz = floorf(z); int ix = (int)fx, iz = (int)fz;
    float u = x - fx, v = z - fz; u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
    const float k = 1.0f / 16777215.0f;
    float a = (float)(noise_hash(ix, iz, seed) >> 8) * k, b = (float)(noise_hash(ix + 1, iz, seed) >> 8) * k;
    float c = (float)(noise_hash(ix, iz + 1, seed) >> 8) * k, d = (float)(noise_hash(ix + 1, iz + 1, seed) >> 8) * k;
    float ab = a + (b - a) * u, cd = c + (d - c) * u;
    return ab + (cd - ab) * v;
}
float arc_noise_fbm2(float x, float z, int octaves, uint32_t seed) {
    if (octaves <= 0) octaves = 5;
    if (octaves > 16) octaves = 16;
    float sum = 0, amp = 1, total = 0, f = 1;
    for (int i = 0; i < octaves; i++) {
        sum += amp * arc_noise_value2(x * f, z * f, seed + (uint32_t)i * 1013904223u);
        total += amp; amp *= 0.5f; f *= 2.0f;
    }
    return sum / total;
}

/* ======================================================================== */
/* Memory                                                                   */
/* ======================================================================== */

void *arc__xalloc(size_t size) {
    void *p = calloc(1, size);
    if (!p) { fprintf(stderr, "ARC fatal out of memory (%zu bytes)\n", size); exit(3); }
    return p;
}
void arc_arena_init(arc_arena *a, void *mem, size_t cap) { a->base = (uint8_t *)mem; a->used = 0; a->cap = cap; a->peak = 0; }
void *arc_arena_alloc(arc_arena *a, size_t size, size_t align) {
    if (!align) align = 16;
    size_t at = (a->used + (align - 1)) & ~(align - 1);
    if (at + size > a->cap) return NULL;
    a->used = at + size; if (a->used > a->peak) a->peak = a->used;
    void *p = a->base + at; memset(p, 0, size); return p;
}
static arc_arena g_perm, g_frame;
arc_arena *arc_perm(void) { return &g_perm; }
arc_arena *arc_frame(void) { return &g_frame; }
char *arc_fmt(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    char *s = (char *)arc_arena_alloc(&g_frame, (size_t)n + 1, 1);
    if (!s) return (char *)"";
    va_start(ap, fmt); vsnprintf(s, (size_t)n + 1, fmt, ap); va_end(ap);
    return s;
}

void arc_pool_init(arc_pool *p, arc_arena *a, int item_size, int cap) {
    memset(p, 0, sizeof *p);
    if (cap > (1 << 20) - 1) cap = (1 << 20) - 1;
    p->item_size = item_size; p->cap = cap;
    p->items = (uint8_t *)arc_arena_alloc(a, (size_t)item_size * (size_t)cap, 16);
    p->gen = (uint16_t *)arc_arena_alloc(a, sizeof(uint16_t) * (size_t)cap, 2);
    p->alive = (uint8_t *)arc_arena_alloc(a, (size_t)cap, 1);
    p->free_list = (uint32_t *)arc_arena_alloc(a, sizeof(uint32_t) * (size_t)cap, 4);
    if (!p->items || !p->gen || !p->alive || !p->free_list) { arc_log("pool alloc failed (cap %d): enlarge perm_mb", cap); p->cap = 0; return; }
    for (int i = 0; i < cap; i++) p->free_list[i] = (uint32_t)(cap - 1 - i);
    p->free_top = cap;
}
arc_handle arc_pool_handle_at(const arc_pool *p, int i) { arc_handle h = { ((uint32_t)(p->gen[i] & 0xFFF) << 20) | (uint32_t)(i + 1) }; return h; }
/* state snapshots: byte writer/reader that keep counting past the end (so save_state can report the size it needs) */
void arc_write(arc_writer *w, const void *p, size_t n) {
    if (w->buf && w->len + n <= w->cap && p) memcpy(w->buf + w->len, p, n);
    w->len += n;
}
bool arc_read(arc_reader *r, void *p, size_t n) {
    if (r->fail || r->pos > r->size || n > r->size - r->pos) { r->fail = true; if (p) memset(p, 0, n); return false; }
    if (p) memcpy(p, r->data + r->pos, n);
    r->pos += n; return true;
}
void arc_write_pool(arc_writer *w, const arc_pool *p) {
    int32_t h[3] = { p->cap, p->count, p->free_top };
    arc_write(w, h, sizeof h);
    arc_write(w, p->items, (size_t)p->item_size * (size_t)p->cap);
    arc_write(w, p->gen, sizeof(uint16_t) * (size_t)p->cap);
    arc_write(w, p->alive, (size_t)p->cap);
    arc_write(w, p->free_list, sizeof(uint32_t) * (size_t)p->cap);
}
bool arc_read_pool(arc_reader *r, arc_pool *p) {
    int32_t h[3];
    if (!arc_read(r, h, sizeof h) || h[0] != p->cap) { r->fail = true; return false; }
    if (h[2] < 0 || h[2] > p->cap || h[1] != p->cap - h[2]) { r->fail = true; return false; }
    /* read into scratch first: a bad file must not leave the pool half-written */
    size_t isz = (size_t)p->item_size * (size_t)p->cap, n = isz + (sizeof(uint16_t) + 1 + sizeof(uint32_t)) * (size_t)p->cap;
    if (r->pos > r->size || n > r->size - r->pos) { r->fail = true; return false; }
    const uint8_t *q = r->data + r->pos;
    const uint8_t *alive = q + isz + sizeof(uint16_t) * (size_t)p->cap; const uint8_t *fl = alive + p->cap;
    int dead = 0;
    for (int i = 0; i < p->cap; i++) if (!alive[i]) dead++;
    if (dead != h[2]) { r->fail = true; return false; }
    for (int k = 0; k < h[2]; k++) { uint32_t v; memcpy(&v, fl + (size_t)k * 4, 4); if (v >= (uint32_t)p->cap || alive[v]) { r->fail = true; return false; } }
    p->count = h[1]; p->free_top = h[2];
    arc_read(r, p->items, isz);
    arc_read(r, p->gen, sizeof(uint16_t) * (size_t)p->cap);
    arc_read(r, p->alive, (size_t)p->cap);
    arc_read(r, p->free_list, sizeof(uint32_t) * (size_t)p->cap);
    return !r->fail;
}
arc_handle arc_pool_add(arc_pool *p) {
    arc_handle none = { 0 };
    if (p->free_top == 0) return none;
    int i = (int)p->free_list[--p->free_top];
    p->alive[i] = 1; p->count++;
    memset(arc_pool_at(p, i), 0, (size_t)p->item_size);
    return arc_pool_handle_at(p, i);
}
static int pool_index(const arc_pool *p, arc_handle h) {
    int i = (int)(h.v & 0xFFFFF) - 1;
    if (i < 0 || i >= p->cap || !p->alive[i] || (p->gen[i] & 0xFFF) != (h.v >> 20)) return -1;
    return i;
}
void arc_pool_remove(arc_pool *p, arc_handle h) {
    int i = pool_index(p, h); if (i < 0) return;
    p->alive[i] = 0; p->gen[i]++; p->count--; p->free_list[p->free_top++] = (uint32_t)i;
}
void *arc_pool_get(arc_pool *p, arc_handle h) { int i = pool_index(p, h); return i < 0 ? NULL : arc_pool_at(p, i); }

/* ======================================================================== */
/* Files                                                                    */
/* ======================================================================== */

/* ---- hot reload: files loaded from disk are watched (desktop, --hot or arc_app.hot_reload) ---- */
#include <sys/stat.h>
typedef struct { int kind; uint32_t id; char path[260]; long long mtime; long long size; } hot_rec;
static hot_rec *g_hot; static int g_hot_count, g_hot_cap; static bool g_hot_on; static double g_hot_next;
static bool g_hot_from_pack;
static bool file_stamp(const char *path, long long *mtime, long long *size) {
#if defined(_WIN32)
    struct _stat64 st; if (_stat64(path, &st)) return false;
#else
    struct stat st; if (stat(path, &st)) return false;
#endif
    *mtime = (long long)st.st_mtime; *size = (long long)st.st_size; return true;
}
void arc__hot_watch(int kind, uint32_t id, const char *path) {
    if (!g_hot_on || g_hot_from_pack || !path) return;
    for (int i = 0; i < g_hot_count; i++) if (g_hot[i].kind == kind && g_hot[i].id == id) { snprintf(g_hot[i].path, sizeof g_hot[i].path, "%s", path); file_stamp(path, &g_hot[i].mtime, &g_hot[i].size); return; }
    if (g_hot_count == g_hot_cap) { g_hot_cap = g_hot_cap ? g_hot_cap * 2 : 64; g_hot = (hot_rec *)realloc(g_hot, sizeof(hot_rec) * (size_t)g_hot_cap); if (!g_hot) { g_hot_count = g_hot_cap = 0; return; } }
    hot_rec *h = &g_hot[g_hot_count++]; h->kind = kind; h->id = id; snprintf(h->path, sizeof h->path, "%s", path);
    if (!file_stamp(path, &h->mtime, &h->size)) h->mtime = h->size = -1;
}
void arc__hot_watch_ex(int kind, uint32_t id, const char *path, bool from_pack) {
    bool was = g_hot_from_pack; g_hot_from_pack = from_pack; arc__hot_watch(kind, id, path); g_hot_from_pack = was;
}
static int hot_check(bool force) {
    int n = 0;
    for (int i = 0; i < g_hot_count; i++) {
        hot_rec *h = &g_hot[i]; long long mt, sz;
        if (!file_stamp(h->path, &mt, &sz)) continue;
        if (!force && mt == h->mtime && sz == h->size) continue;
        bool ok = false;
        if (h->kind == ARC__HOT_TEXTURE) ok = arc__texture_reload((arc_texture){ h->id }, h->path);
        else if (h->kind == ARC__HOT_MESH) ok = arc__mesh_reload((arc_mesh){ h->id }, h->path);
        else if (h->kind == ARC__HOT_SOUND) ok = arc__sound_reload((arc_sound){ h->id }, h->path);
        else if (h->kind == ARC__HOT_LEVEL) ok = arc__level_hot_reload(h->id, h->path);   /* 0.10.8 */
        else if (h->kind == ARC__HOT_ANIM) {
            g_hot_on = false; arc_anim f = arc_anim_load(h->path); g_hot_on = true;
            ok = f.id && arc__anim_replace((arc_anim){ h->id }, f);
        }
        if (ok) { h->mtime = mt; h->size = sz; n++; printf("ARC ok reload %s\n", h->path); }
        else if (force || mt != h->mtime) printf("ARC error reload %s (kept the old one; will retry when it changes)\n", h->path);
        if (!ok) { h->mtime = mt; h->size = sz; }
        fflush(stdout);
    }
    return n;
}

/* ---- asset packs (.arcpak): many files in one, zlib-compressed where it helps; file loads look in them first ----
   "ARCP" u32 version(1) u32 count u32 dir_bytes, then per file { u32 offset, u32 stored, u32 size, u8 method
   (0 stored, 1 zlib), u8 name_len, name }, then the data. Made by tools/arcpak.py. */
#define ARC__MAX_PACKS 32                  /* 0.8.6: 8 -> 32 (a web build may fetch a pack per folder) */
typedef struct { uint8_t *data; size_t size; uint32_t count; const uint8_t *dir; char name[256]; } pack_rec;
static pack_rec g_packs[ARC__MAX_PACKS]; static int g_pack_count;
static const char *pack_norm(const char *path, char *buf, size_t cap) {
    const char *app = arc_app_dir(); size_t al = strlen(app);
    if (al > 1 && !strncmp(path, app, al)) path += al;           /* "<exe folder>/assets/x" -> "assets/x" */
    size_t k = 0;
    for (const char *p = path; *p && k + 1 < cap; p++) {
        char c = *p == '\\' ? '/' : *p;
        if (c == '/' && k > 0 && buf[k - 1] == '/') continue;
        buf[k++] = c;
    }
    buf[k] = 0;
    while (buf[0] == '.' && buf[1] == '/') memmove(buf, buf + 2, strlen(buf + 2) + 1);
    return buf;
}
static bool pack_parse(pack_rec *p) {
    if (p->size < 16 || memcmp(p->data, "ARCP", 4)) return false;
    uint32_t ver, count, dirb; memcpy(&ver, p->data + 4, 4); memcpy(&count, p->data + 8, 4); memcpy(&dirb, p->data + 12, 4);
    if (ver != 1 || (size_t)dirb > p->size - 16 || count > dirb / 14) return false;
    const uint8_t *e = p->data + 16, *end = e + dirb;
    for (uint32_t i = 0; i < count; i++) {                         /* validate the directory once */
        if ((size_t)(end - e) < 14 || (size_t)(end - e) < 14u + e[13]) return false;
        uint32_t off, st, sz; memcpy(&off, e, 4); memcpy(&st, e + 4, 4); memcpy(&sz, e + 8, 4);
        if ((size_t)off > p->size || (size_t)st > p->size - off) return false;
        if (e[12] > 1 || (e[12] == 0 && sz != st) || sz > (1u << 30)) return false;
        e += 14 + e[13];
    }
    p->count = count; p->dir = p->data + 16; return true;
}
bool arc_pack_mount_memory(const uint8_t *data, size_t size, const char *name) {
    if (g_pack_count >= ARC__MAX_PACKS || !data) return false;
    pack_rec *p = &g_packs[g_pack_count];
    p->data = (uint8_t *)arc__xalloc(size); memcpy(p->data, data, size); p->size = size;
    snprintf(p->name, sizeof p->name, "%s", name ? name : "(memory)");
    if (!pack_parse(p)) { free(p->data); memset(p, 0, sizeof *p); arc_log("%s is not an .arcpak file", name ? name : "pack"); return false; }
    ARC__BARRIER(); g_pack_count++;           /* (loader threads may be reading the packs already mounted) */
    return true;
}
/* 0.8.6: a downloaded pack, mounted without another copy (takes the malloc'd bytes; frees them if it can't) */
bool arc__pack_mount_owned(uint8_t *data, size_t size, const char *name) {
    if (!data) return false;
    if (g_pack_count >= ARC__MAX_PACKS) { free(data); arc_log("too many packs (%d): %s", ARC__MAX_PACKS, name ? name : "pack"); return false; }
    pack_rec *p = &g_packs[g_pack_count];
    p->data = data; p->size = size;
    snprintf(p->name, sizeof p->name, "%s", name ? name : "(memory)");
    if (!pack_parse(p)) { free(p->data); memset(p, 0, sizeof *p); arc_log("%s is not an .arcpak file", name ? name : "pack"); return false; }
    ARC__BARRIER(); g_pack_count++;
    return true;
}
static bool g_pack_reading;
bool arc_pack_mount(const char *path) {
    g_pack_reading = true;                                        /* the pack itself comes from disk */
    size_t n = 0, mark = arc_arena_mark(arc_frame());
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    g_pack_reading = false;
    if (!d) { arc_log("cannot open pack %s", path); return false; }
    bool ok = arc_pack_mount_memory(d, n, path);
    arc_arena_pop(arc_frame(), mark);
    if (ok) arc_log("mounted %s (%u files)", path, g_packs[g_pack_count - 1].count);
    return ok;
}
static uint8_t *pack_read(const char *path, arc_arena *a, size_t *size_out, bool *found) {
    char nb[512]; pack_norm(path, nb, sizeof nb); size_t nl = strlen(nb);
    for (int pi = g_pack_count - 1; pi >= 0; pi--) {                /* newest pack first */
        const pack_rec *p = &g_packs[pi]; const uint8_t *e = p->dir;
        for (uint32_t i = 0; i < p->count; i++, e += 14 + e[13]) {
            if (e[13] != nl || memcmp(e + 14, nb, nl)) continue;
            uint32_t off, st, sz; memcpy(&off, e, 4); memcpy(&st, e + 4, 4); memcpy(&sz, e + 8, 4);
            *found = true;
            uint8_t *buf = a ? (uint8_t *)arc_arena_alloc(a, (size_t)sz + 1, 16) : (uint8_t *)malloc((size_t)sz + 1);   /* a NULL: the loader's malloc */
            if (!buf) return NULL;
            if (e[12] == 0) memcpy(buf, p->data + off, sz);
            else {
                size_t got = 0; uint8_t *raw = arc__inflate(p->data + off, st, sz, &got);
                if (!raw || got != sz) { free(raw); if (!a) free(buf); else arc_log("pack %s: %s is damaged", p->name, nb); return NULL; }
                memcpy(buf, raw, sz); free(raw);
            }
            buf[sz] = 0; if (size_out) *size_out = sz;
            return buf;
        }
    }
    return NULL;
}
bool arc_file_exists(const char *path) {
    bool found = false; char nb[512]; pack_norm(path, nb, sizeof nb); size_t nl = strlen(nb);
    for (int pi = 0; pi < g_pack_count && !found; pi++) {
        const uint8_t *e = g_packs[pi].dir;
        for (uint32_t i = 0; i < g_packs[pi].count; i++, e += 14 + e[13]) if (e[13] == nl && !memcmp(e + 14, nb, nl)) { found = true; break; }
    }
    if (found) return true;
    FILE *f = fopen(path, "rb"); if (f) fclose(f);
    return f != NULL;
}
/* 0.7.1 for the loader's threads: no arena, no globals written */
uint8_t *arc__file_read_alloc(const char *path, size_t *size_out, bool *from_pack) {
    *from_pack = false;
    if (g_pack_count) { bool found = false; uint8_t *d = pack_read(path, NULL, size_out, &found); if (found) { *from_pack = true; return d; } }
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f); fclose(f);
    buf[got] = 0; if (size_out) *size_out = got;
    return buf;
}
long long arc__file_size(const char *path, bool *from_pack) {
    char nb[512]; pack_norm(path, nb, sizeof nb); size_t nl = strlen(nb);
    *from_pack = false;
    for (int pi = g_pack_count - 1; pi >= 0; pi--) {
        const uint8_t *e = g_packs[pi].dir;
        for (uint32_t i = 0; i < g_packs[pi].count; i++, e += 14 + e[13])
            if (e[13] == nl && !memcmp(e + 14, nb, nl)) { uint32_t sz; memcpy(&sz, e + 8, 4); *from_pack = true; return (long long)sz; }
    }
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fclose(f);
    return n < 0 ? -1 : (long long)n;
}
uint8_t *arc_file_read(const char *path, arc_arena *a, size_t *size_out) {
    g_hot_from_pack = false;
    if (g_pack_count && !g_pack_reading) { bool found = false; uint8_t *d = pack_read(path, a, size_out, &found); if (found) { g_hot_from_pack = true; return d; } }
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)arc_arena_alloc(a, (size_t)n + 1, 16);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f); fclose(f);
    buf[got] = 0; if (size_out) *size_out = got;
    return buf;
}
bool arc_file_write(const char *path, const void *data, size_t size) {
    FILE *f = fopen(path, "wb"); if (!f) return false;
    bool ok = fwrite(data, 1, size, f) == size; fclose(f); return ok;
}


/* ======================================================================== */
/* JSON writer                                                              */
/* ======================================================================== */

struct arc_json { char *buf; size_t len, cap; int depth; uint8_t first[64]; char kind[64]; };
char *arc__json_capture(void (*fn)(arc_json *j));   /* (self-test: one object written by fn; free the result) */
static void jput(arc_json *j, const char *s, size_t n) {
    if (j->len + n + 1 > j->cap) {
        size_t nc = j->cap ? j->cap * 2 : 4096; while (nc < j->len + n + 1) nc *= 2;
        char *nb = (char *)realloc(j->buf, nc); if (!nb) return; j->buf = nb; j->cap = nc;
    }
    memcpy(j->buf + j->len, s, n); j->len += n; j->buf[j->len] = 0;
}
static void jputs(arc_json *j, const char *s) { jput(j, s, strlen(s)); }
static void jsep(arc_json *j) { if (j->depth > 0) { if (!j->first[j->depth]) jput(j, ",", 1); j->first[j->depth] = 0; } }
static void jstr(arc_json *j, const char *s) {
    jput(j, "\"", 1);
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s; char e[8];
        if (c == '"' || c == '\\') { e[0] = '\\'; e[1] = (char)c; jput(j, e, 2); }
        else if (c == '\n') jput(j, "\\n", 2);
        else if (c < 0x20) { snprintf(e, sizeof e, "\\u%04x", c); jputs(j, e); }
        else jput(j, (const char *)&c, 1);
    }
    jput(j, "\"", 1);
}
static void jkey(arc_json *j, const char *k) { jsep(j); jstr(j, k); jput(j, ":", 1); }
static void jopen(arc_json *j, char c) { jput(j, &c, 1); if (j->depth < 63) j->depth++; j->first[j->depth] = 1; j->kind[j->depth] = c; }
void arc_json_obj(arc_json *j) { jsep(j); jopen(j, '{'); }
void arc_json_arr(arc_json *j) { jsep(j); jopen(j, '['); }
void arc_json_obj_k(arc_json *j, const char *k) { jkey(j, k); jopen(j, '{'); }
void arc_json_arr_k(arc_json *j, const char *k) { jkey(j, k); jopen(j, '['); }
void arc_json_end(arc_json *j) {
    if (j->depth <= 0) return;
    char close = j->kind[j->depth] == '[' ? ']' : '}';
    jput(j, &close, 1); j->depth--;
}
static void jnum(arc_json *j, double v) {
    char b[48];
    if (v != v || v > 1e300 || v < -1e300) snprintf(b, sizeof b, "null");
    else if (v == (double)(long long)v && fabs(v) < 1e15) snprintf(b, sizeof b, "%lld", (long long)v);
    else snprintf(b, sizeof b, "%.6g", v);
    jputs(j, b);
}
void arc_json_num(arc_json *j, double v) { jsep(j); jnum(j, v); }
void arc_json_int(arc_json *j, long long v) { jsep(j); char b[32]; snprintf(b, sizeof b, "%lld", v); jputs(j, b); }
void arc_json_str(arc_json *j, const char *s) { jsep(j); jstr(j, s); }
void arc_json_bool(arc_json *j, bool b) { jsep(j); jputs(j, b ? "true" : "false"); }
void arc_json_num_k(arc_json *j, const char *k, double v) { jkey(j, k); jnum(j, v); }
void arc_json_int_k(arc_json *j, const char *k, long long v) { jkey(j, k); char b[32]; snprintf(b, sizeof b, "%lld", v); jputs(j, b); }
void arc_json_str_k(arc_json *j, const char *k, const char *s) { jkey(j, k); jstr(j, s); }
void arc_json_bool_k(arc_json *j, const char *k, bool b) { jkey(j, k); jputs(j, b ? "true" : "false"); }
void arc_json_v3_k(arc_json *j, const char *k, arc_v3 v) { arc_json_arr_k(j, k); arc_json_num(j, v.x); arc_json_num(j, v.y); arc_json_num(j, v.z); arc_json_end(j); }

/* ======================================================================== */
/* App state                                                                */
/* ======================================================================== */

static struct {
    arc_app app;
    const arc__platform *plat;
    int argc; char **argv;
    bool headless, quit, started;
    int width, height;
    int tick_hz; float dt;
    uint64_t ticks;
    double acc, last_time, start_time;
    uint64_t max_ticks;           /* --frames */
    bool render_all;              /* headless: render every frame */
    int draw_last;                /* 0.9.8 --draw-last N: headless, draw (targets too) only the last N ticks before an end or a shot */
    float tick_ms;
    int last_w, last_h; bool resized;
} G;
bool arc__quit_requested;
/* 0.8.1 rollback matches (arc_rollback.c) */
int  arc__rb_mode(void);              /* 0 none, 1 a live networked match (paced by the clock), 2 synctest */
int  arc__rb_before_tick(void);       /* before a new tick: 0 run it, 1 skip this tick (time sync), 2 hold (waiting) */
void arc__rb_new_tick(void);          /* after the tick's input snapshot: pack the local input */
void arc__rb_after_tick(void);
void arc__rb_tick_begin(void);        /* the top of every simulated tick (live, re-simulated, replayed) */
bool arc__rb_event(const char *name, const char *body);   /* true: held until its tick is confirmed */
bool arc__rb_live_match(void);        /* a live match runs (going back in time is not possible then) */
int  arc__rb_fps_line(char *out, size_t n);
void arc__rb_startup(void);

float    arc_dt(void) { return G.dt; }
uint64_t arc_tick_count(void) { return G.ticks; }
double   arc_time(void) { return (double)G.ticks * (double)G.dt; }
double   arc_real_time(void) { return G.plat ? G.plat->time() - G.start_time : 0.0; }
int      arc_width(void) { int w; return arc__viewport_cur(NULL, NULL, &w, NULL) ? w : G.width; }   /* (0.10.2: a viewport's own size inside it) */
int      arc_height(void) { int h; return arc__viewport_cur(NULL, NULL, NULL, &h) ? h : G.height; }
void     arc__window_size(int *w, int *h) { *w = G.width; *h = G.height; }
float    arc_aspect(void) { int w = arc_width(), h = arc_height(); return h > 0 ? (float)w / (float)h : 1.0f; }
float    arc__dpi = 1.0f;   /* set by the web platform with arc_app.hidpi */
int      arc__msaa = 0;
int      arc__depth_mode = 0;   /* 0.7.2 ARC_DEPTH_* */
float    arc_dpi_scale(void) { return arc__dpi > 0 ? arc__dpi : 1.0f; }
bool     arc_headless(void) { return G.headless; }
void     arc_quit(void) { G.quit = true; }
int      arc_argc(void) { return G.argc; }
const char *arc_argv(int i) { return (i >= 0 && i < G.argc) ? G.argv[i] : NULL; }
void arc__set_size(int w, int h) { if (w > 0 && h > 0) { G.width = w; G.height = h; } }
bool arc_window_resized(void) { return G.resized; }
void arc_window_set_icon(const uint8_t *rgba, int w, int h) { if (G.plat && G.plat->set_icon && rgba && w > 0 && h > 0) G.plat->set_icon(rgba, w, h); }
void arc_window_toggle_fullscreen(void) { if (G.plat && G.plat->toggle_fullscreen) G.plat->toggle_fullscreen(); }
bool arc_window_fullscreen(void) { return G.plat && G.plat->fullscreen && G.plat->fullscreen(); }

/* ---- saves ---- */
#if defined(_WIN32)
#include <direct.h>
#define ARC__MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define ARC__MKDIR(p) mkdir(p, 0755)
#endif
static char g_save_dir[1024];
static const char *save_dir(void) {
    if (g_save_dir[0]) return g_save_dir;
    const char *forced = arc_arg_value("save-dir");
    if (forced) snprintf(g_save_dir, sizeof g_save_dir, "%s", forced);
    else if (G.headless) snprintf(g_save_dir, sizeof g_save_dir, "arc_saves");
    else {
        const char *name = G.app.save_folder ? G.app.save_folder : G.app.title ? G.app.title : "arc_game";
#if defined(_WIN32)
        const char *base = getenv("APPDATA");
        snprintf(g_save_dir, sizeof g_save_dir, "%s\\%s", base ? base : ".", name);
#else
        const char *x = getenv("XDG_DATA_HOME"), *h = getenv("HOME");
        if (x && *x) snprintf(g_save_dir, sizeof g_save_dir, "%s", x);
        else { snprintf(g_save_dir, sizeof g_save_dir, "%s/.local", h ? h : "."); ARC__MKDIR(g_save_dir); snprintf(g_save_dir, sizeof g_save_dir, "%s/.local/share", h ? h : "."); }
        ARC__MKDIR(g_save_dir);
        size_t n = strlen(g_save_dir); snprintf(g_save_dir + n, sizeof g_save_dir - n, "/%s", name);
#endif
    }
    ARC__MKDIR(g_save_dir);
    return g_save_dir;
}
const char *arc__save_dir(void) { return save_dir(); }   /* the backends' caches (D3D11 shaders) */
static bool save_key_ok(const char *key) {   /* a plain file name: no folders, no "..", not empty */
    if (!key || !*key || strlen(key) > 200 || !strcmp(key, ".") || !strcmp(key, "..")) return false;
    for (const char *p = key; *p; p++) if (*p == '/' || *p == '\\' || *p == ':' || (unsigned char)*p < 32) return false;
    return true;
}
/* Recordings capture what arc_load returned (input.rec.saves), so a replay starts from the same saved state
   (settings, progress, a player profile) even after the real saves have changed; replays never write saves. */
static FILE *g_saves_rec;                                  /* recording: every arc_load result, in order */
static uint8_t *g_saves_play; static size_t g_saves_play_n; static bool g_saves_replay;
/* 0.8.1: where the next recorded entry of each key is read from. Kept in ARC's snapshots (seek, rewind, the timeline,
   rollback), so ticks run again read the same entries again. A recording also keeps a copy of what it wrote: a live
   game that goes back over ticks it recorded reads them from there, never the disk again (nor records them twice). */
#define SAVES_KEYS 32
static struct { char key[64]; uint64_t pos; } g_saves_cur[SAVES_KEYS]; static int g_saves_ncur;
static uint8_t *g_saves_mem; static size_t g_saves_mem_n, g_saves_mem_cap;
static uint64_t *saves_cursor(const char *key) {
    for (int i = 0; i < g_saves_ncur; i++) if (!strcmp(g_saves_cur[i].key, key)) return &g_saves_cur[i].pos;
    if (g_saves_ncur == SAVES_KEYS || strlen(key) >= sizeof g_saves_cur[0].key) return NULL;
    snprintf(g_saves_cur[g_saves_ncur].key, sizeof g_saves_cur[0].key, "%s", key); g_saves_cur[g_saves_ncur].pos = 0;
    return &g_saves_cur[g_saves_ncur++].pos;
}
static void saves_rec_put(const char *key, const uint8_t *d, size_t n, bool found) {
    if (!g_saves_rec) return;
    uint16_t kl = (uint16_t)strlen(key); uint32_t sz = found ? (uint32_t)n : 0xFFFFFFFFu;
    fwrite(&kl, 2, 1, g_saves_rec); fwrite(key, 1, kl, g_saves_rec); fwrite(&sz, 4, 1, g_saves_rec);
    if (found && n) fwrite(d, 1, n, g_saves_rec);
    fflush(g_saves_rec);
    size_t need = 2 + kl + 4 + (found ? n : 0);
    if (g_saves_mem_n + need > g_saves_mem_cap) {
        size_t nc = g_saves_mem_cap ? g_saves_mem_cap : 4096; while (nc < g_saves_mem_n + need) nc *= 2;
        uint8_t *m = (uint8_t *)realloc(g_saves_mem, nc); if (!m) return;
        g_saves_mem = m; g_saves_mem_cap = nc;
    }
    uint8_t *p = g_saves_mem + g_saves_mem_n;
    memcpy(p, &kl, 2); memcpy(p + 2, key, kl); memcpy(p + 2 + kl, &sz, 4); if (found && n) memcpy(p + 6 + kl, d, n);
    g_saves_mem_n += need;
    uint64_t *cur = saves_cursor(key); if (cur) *cur = g_saves_mem_n;      /* read already: it was this tick's */
}
/* the next entry of this key from a recorded stream (the replay's, or this recording's copy); false: none */
static bool saves_play_get(const uint8_t *buf, size_t bn, const char *key, arc_arena *a, size_t *size_out, uint8_t **res) {
    uint64_t *cur = saves_cursor(key); if (!cur) return false;
    size_t pos = (size_t)*cur, kl = strlen(key);
    while (pos + 6 <= bn) {
        uint16_t l; memcpy(&l, buf + pos, 2);
        if (pos + 2 + l + 4 > bn) break;
        uint32_t sz; memcpy(&sz, buf + pos + 2 + l, 4);
        size_t data = pos + 2 + l + 4, next = data + (sz == 0xFFFFFFFFu ? 0 : sz);
        if (next > bn) break;
        if (l == kl && !memcmp(buf + pos + 2, key, kl)) {
            *cur = next;                                         /* the next load of this key gets the next entry */
            if (sz == 0xFFFFFFFFu) { *res = NULL; return true; }
            uint8_t *out = a ? (uint8_t *)arc_arena_alloc(a, sz + 1, 1) : (uint8_t *)malloc(sz + 1);
            if (!out) return false;
            memcpy(out, buf + data, sz); out[sz] = 0; if (size_out) *size_out = sz;
            *res = out; return true;
        }
        pos = next;
    }
    return false;
}
static bool saves_going_over_past(void);
/* ARC's snapshot block for the read positions ("SAV1"; snapshots from before 0.8.1 have none: positions are kept) */
#define SAVES_STATE_MAGIC 0x31564153u
size_t arc__saves_state(arc_writer *w) {
    if (!g_saves_replay && !g_saves_rec) return w->len;          /* (written with no keys too: then every position goes back to 0) */
    uint32_t magic = SAVES_STATE_MAGIC, n = (uint32_t)g_saves_ncur;
    arc_write(w, &magic, 4); arc_write(w, &n, 4);
    for (int i = 0; i < g_saves_ncur; i++) { arc_write(w, g_saves_cur[i].key, sizeof g_saves_cur[i].key); arc_write(w, &g_saves_cur[i].pos, 8); }
    return w->len;
}
bool arc__saves_state_read(arc_reader *r, bool commit) {
    if (r->pos >= r->size) return true;
    uint32_t magic = 0, n = 0;
    if (r->size - r->pos >= 4 && !memcmp(r->data + r->pos, "SHT1", 4)) return true;   /* 0.10.0: no loads' block, the shots' follows */
    if (!arc_read(r, &magic, 4) || magic != SAVES_STATE_MAGIC || !arc_read(r, &n, 4) || n > SAVES_KEYS) { r->fail = true; return false; }
    static struct { char key[64]; uint64_t pos; } tmp[SAVES_KEYS];
    for (uint32_t i = 0; i < n; i++) { if (!arc_read(r, tmp[i].key, sizeof tmp[i].key) || !arc_read(r, &tmp[i].pos, 8)) return false; tmp[i].key[sizeof tmp[i].key - 1] = 0; }
    if (!commit) return true;
    for (int i = 0; i < g_saves_ncur; i++) g_saves_cur[i].pos = 0;   /* keys first read after the snapshot: from the start again */
    for (uint32_t i = 0; i < n; i++) { uint64_t *c = saves_cursor(tmp[i].key); if (c) *c = tmp[i].pos; }
    return true;
}
void arc__test_saves_stream(const uint8_t *data, size_t n, FILE *rec) {   /* self-test: a replay's stream (data) or a recording (rec) */
    g_saves_play = (uint8_t *)(uintptr_t)data; g_saves_play_n = n; g_saves_replay = data != NULL;
    g_saves_rec = rec; g_saves_mem_n = 0; g_saves_ncur = 0;
}
bool arc_save(const char *key, const void *data, size_t size) {
    if (!save_key_ok(key)) { arc_log("bad save key '%s'", key ? key : ""); return false; }
    if (g_saves_replay) return true;                       /* a replay never overwrites the player's saves */
    if (G.plat && G.plat->save && !G.headless) return G.plat->save(key, data, size);
    const char *path = arc_fmt("%s/%s", save_dir(), key), *tmp = arc_fmt("%s.tmp", path);
    static bool said;   /* (once, until a save works again) */
    if (!arc_file_write(tmp, data, size)) {   /* 0.10.12: say so (a wrong --save-dir used to fail without a word) */
        if (said) return false;
        said = true;
        printf("ARC error save '%s' failed: cannot write %s (is the save folder %s there and writable? --save-dir)\n", key, tmp, save_dir()); fflush(stdout);
        return false;
    }
    remove(path);                                   /* Windows will not rename over a file */
    if (rename(tmp, path) != 0) { printf("ARC error save '%s' failed: cannot rename %s to %s\n", key, tmp, path); fflush(stdout); return false; }
    said = false;
    return true;
}
static uint8_t *load_real(const char *key, arc_arena *a, size_t *size_out) {
    if (G.plat && G.plat->load && !G.headless) return G.plat->load(key, a, size_out);
    return arc_file_read(arc_fmt("%s/%s", save_dir(), key), a, size_out);
}
uint8_t *arc_load(const char *key, arc_arena *a, size_t *size_out) {
    if (!save_key_ok(key)) return NULL;
    uint8_t *r = NULL;
    if (g_saves_replay && saves_play_get(g_saves_play, g_saves_play_n, key, a, size_out, &r)) return r;
    if (g_saves_rec && saves_going_over_past() && saves_play_get(g_saves_mem, g_saves_mem_n, key, a, size_out, &r)) return r;   /* 0.8.1 */
    size_t n = 0; r = load_real(key, a, &n);
    if (size_out) *size_out = n;
    saves_rec_put(key, r, n, r != NULL);
    return r;
}
bool arc_save_remove(const char *key) {
    if (!save_key_ok(key)) return false;
    if (G.plat && G.plat->save_remove && !G.headless) return G.plat->save_remove(key);
    return remove(arc_fmt("%s/%s", save_dir(), key)) == 0;
}

/* ---- the executable's folder ---- */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif
const char *arc_app_dir(void) {
    static char dir[1024];
    if (dir[0]) return dir;
#if defined(__EMSCRIPTEN__)
    snprintf(dir, sizeof dir, "/");
#else
#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, dir, (DWORD)sizeof dir - 1); if (n >= sizeof dir - 1) n = 0; dir[n] = 0;
#else
    ssize_t n = readlink("/proc/self/exe", dir, sizeof dir - 1); if (n < 0) n = 0; dir[n] = 0;
#endif
    char *slash = NULL; for (char *p = dir; *p; p++) { if (*p == '\\') *p = '/'; if (*p == '/') slash = p; }   /* '/' everywhere: Windows takes it too */
    if (slash) slash[1] = 0; else snprintf(dir, sizeof dir, "./");
#endif
    return dir;
}

double arc__plat_time(void) { return G.plat ? G.plat->time() : 0.0; }

void arc_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char line[1024]; vsnprintf(line, sizeof line, fmt, ap);
    fputs("ARC log ", stdout); fputs(line, stdout); fputc('\n', stdout); fflush(stdout);
    va_end(ap);
    void arc__session_log(const char *line); arc__session_log(line);
}
/* Flags start with '-' or "--"; a value follows as "--name=value" or as the next argument. Arguments that are
   values (the "help" in "--panel help") are never taken for flags. A next argument starting with '-' is a value
   only when it is a negative number ("--mission -1"). */
static bool arg_is_value(const char *a) { return a[0] != '-' || (a[1] >= '0' && a[1] <= '9') || (a[1] == '.' && a[2] >= '0' && a[2] <= '9'); }
const char *arc_arg_value(const char *name) {
    size_t n = strlen(name);
    for (int i = 1; i < G.argc; i++) {
        const char *a = G.argv[i];
        if (arg_is_value(a)) continue;
        while (*a == '-') a++;
        if (strncmp(a, name, n) == 0) {
            if (a[n] == '=') return a + n + 1;
            if (a[n] == 0 && i + 1 < G.argc && arg_is_value(G.argv[i + 1])) return G.argv[i + 1];
        }
    }
    return NULL;
}
bool arc_arg_flag(const char *name) {
    for (int i = 1; i < G.argc; i++) {
        const char *a = G.argv[i];
        if (arg_is_value(a)) continue;
        while (*a == '-') a++;
        if (!strcmp(a, name)) return true;
    }
    return false;
}

/* ======================================================================== */
/* Input                                                                    */
/* ======================================================================== */

static arc__input_snap g_raw, g_cur, g_prev, g_virtual;   /* raw from platform, virtual from scripts */
static uint8_t g_latch[ARC_KEY_COUNT / 8];                /* keys that went down since the last tick */
static char g_text[sizeof g_raw.text], g_vtext[sizeof g_raw.text];   /* typed since the last tick (platform, scripts) */
static uint8_t g_tap_ticks[ARC_KEY_COUNT];
static bool g_mouse_virtual;
static float g_raw_wheel;

static inline bool snap_get(const arc__input_snap *s, int k) { return (s->keys[k >> 3] >> (k & 7)) & 1; }
static inline void snap_set(arc__input_snap *s, int k, bool d) { if (d) s->keys[k >> 3] |= (uint8_t)(1 << (k & 7)); else s->keys[k >> 3] &= (uint8_t)~(1 << (k & 7)); }
void arc__input_key(arc_key k, bool down) {
    if (k <= 0 || k >= ARC_KEY_COUNT) return;
    snap_set(&g_raw, k, down);
    if (down) g_latch[k >> 3] |= (uint8_t)(1 << (k & 7));
}
static void text_append(char *buf, size_t cap, uint32_t cp) {
    char u[5] = { 0 }; int n = 0;
    if (cp < 0x80) u[n++] = (char)cp;
    else if (cp < 0x800) { u[n++] = (char)(0xC0 | cp >> 6); u[n++] = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { u[n++] = (char)(0xE0 | cp >> 12); u[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[n++] = (char)(0x80 | (cp & 0x3F)); }
    else { u[n++] = (char)(0xF0 | cp >> 18); u[n++] = (char)(0x80 | ((cp >> 12) & 0x3F)); u[n++] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[n++] = (char)(0x80 | (cp & 0x3F)); }
    size_t len = strlen(buf); if (len + (size_t)n < cap) memcpy(buf + len, u, (size_t)n + 1);
}
void arc__input_char(uint32_t cp) { if (cp >= 32 && cp != 127 && cp < 0x110000) text_append(g_text, sizeof g_text, cp); }
void arc__input_axis(arc_axis a, float v) { if (a >= 0 && a < ARC_AXIS_COUNT) g_raw.axes[a] = arc_clampf(v, -1, 1); }
void arc__input_pad(bool on) { if (on) g_raw.flags |= ARC__IN_PAD; else { g_raw.flags &= (uint8_t)~ARC__IN_PAD; memset(g_raw.axes, 0, sizeof g_raw.axes); for (int k = ARC_PAD_A; k <= ARC_PAD_GUIDE; k++) snap_set(&g_raw, k, false); } }
/* 0.10.5 pads by slot: the platforms' state (g_pads) and scripts' (g_vpads, the `pad` command) */
static struct { bool on; uint32_t btn, latch; float ax[ARC_AXIS_COUNT]; } g_pads[ARC_MAX_PADS], g_vpads[ARC_MAX_PADS];
void arc__input_pad_slot(int slot, bool on) {
    if (slot < 0 || slot >= ARC_MAX_PADS) return;
    if (!on) { g_pads[slot].btn = 0; memset(g_pads[slot].ax, 0, sizeof g_pads[slot].ax); }
    g_pads[slot].on = on;
}
void arc__input_pad_button(int slot, arc_key k, bool down) {
    if (slot < 0 || slot >= ARC_MAX_PADS || k < ARC_PAD_A || k > ARC_PAD_GUIDE) return;
    uint32_t bit = 1u << (k - ARC_PAD_A);
    if (down) { g_pads[slot].btn |= bit; g_pads[slot].latch |= bit; } else g_pads[slot].btn &= ~bit;
}
void arc__input_pad_axis(int slot, arc_axis a, float v) { if (slot >= 0 && slot < ARC_MAX_PADS && a >= 0 && a < ARC_AXIS_COUNT) g_pads[slot].ax[a] = arc_clampf(v, -1, 1); }
/* a tick's pads: each slot (platform + scripts), then the old single-pad view (ARC_PAD_* keys, axes) from the first */
static void snap_pads(arc__input_snap *s) {
    s->padmask = 0;
    for (int p = 0; p < ARC_MAX_PADS; p++) {
        bool on = g_pads[p].on || g_vpads[p].on;
        s->pbtn[p] = on ? (g_pads[p].btn | g_pads[p].latch | g_vpads[p].btn) : 0; g_pads[p].latch = 0;
        for (int a = 0; a < ARC_AXIS_COUNT; a++) s->pax[p][a] = !on ? 0.0f : g_vpads[p].ax[a] != 0 ? g_vpads[p].ax[a] : g_pads[p].ax[a];
        if (on) s->padmask |= 1u << p;
    }
    int f = -1; for (int p = 0; p < ARC_MAX_PADS && f < 0; p++) if (s->padmask >> p & 1) f = p;
    /* (the old view's own script input, `press PAD_A` / `axis LX`, counts as the first pad's, or pad 0's) */
    int fv = f >= 0 ? f : 0; bool legacy = (s->flags & ARC__IN_PAD) != 0;
    for (int k = ARC_PAD_A; k <= ARC_PAD_GUIDE; k++) if (snap_get(s, k)) { s->pbtn[fv] |= 1u << (k - ARC_PAD_A); legacy = true; }
    for (int a = 0; a < ARC_AXIS_COUNT; a++) if (s->axes[a] != 0) { s->pax[fv][a] = s->axes[a]; legacy = true; }
    if (legacy) { s->padmask |= 1u << fv; f = fv; }
    if (f >= 0) {
        for (int k = ARC_PAD_A; k <= ARC_PAD_GUIDE; k++) if (s->pbtn[f] >> (k - ARC_PAD_A) & 1) snap_set(s, k, true);
        memcpy(s->axes, s->pax[f], sizeof s->axes); s->flags |= ARC__IN_PAD;
    }
}
void arc__input_touch_mode(bool on) { if (on) g_raw.flags |= ARC__IN_TOUCH; else g_raw.flags &= (uint8_t)~ARC__IN_TOUCH; }
/* ---- 0.10.11 multi-touch. Platforms report each finger by their own id; here it gets a small one (the lowest free,
   not used by a finger of the last tick either, so a lifted finger's id is never re-pressed in the same tick's list).
   A finger lifted before any tick saw it stays for one tick (as a key latch does). ---- */
static struct { long long pid; bool seen, up; } g_rt[ARC_TOUCH_MAX];
static int16_t touch_px(float v) { return (int16_t)arc_clampf(floorf(v + 0.5f), -32768.0f, 32767.0f); }
static int touch_free_id(const arc__input_snap *a, const arc__input_snap *b) {
    for (int id = 0; id < 255; id++) {
        bool used = false;
        for (int i = 0; i < a->tcount && !used; i++) used = a->touch[i].id == id;
        for (int i = 0; b && i < b->tcount && !used; i++) used = b->touch[i].id == id;
        if (!used) return id;
    }
    return 255;
}
static void touch_remove(arc__input_snap *s, int i, bool raw) {
    for (int k = i; k + 1 < s->tcount; k++) { s->touch[k] = s->touch[k + 1]; if (raw) g_rt[k] = g_rt[k + 1]; }
    s->tcount--;
}
void arc__input_touch(long long finger, int phase, float x, float y) {
    int at = -1; for (int i = 0; i < g_raw.tcount; i++) if (g_rt[i].pid == finger && !g_rt[i].up) { at = i; break; }
    if (phase == 0 && at < 0) {
        if (g_raw.tcount >= ARC_TOUCH_MAX) return;
        uint8_t id = (uint8_t)touch_free_id(&g_raw, &g_cur);
        int i = g_raw.tcount++;
        g_raw.touch[i].id = id; g_raw.touch[i].pad = 0;
        g_raw.touch[i].x = g_raw.touch[i].sx = touch_px(x); g_raw.touch[i].y = g_raw.touch[i].sy = touch_px(y);
        g_rt[i].pid = finger; g_rt[i].seen = false; g_rt[i].up = false;
        g_raw.flags |= ARC__IN_TOUCH;
        return;
    }
    if (at < 0) return;
    g_raw.touch[at].x = touch_px(x); g_raw.touch[at].y = touch_px(y);
    if (phase == 2) { if (g_rt[at].seen) touch_remove(&g_raw, at, true); else g_rt[at].up = true; }
}
static void touch_raw_seen(void) {   /* after a tick took the raw list: fingers lifted meanwhile go now */
    for (int i = 0; i < g_raw.tcount; i++) g_rt[i].seen = true;
    for (int i = g_raw.tcount - 1; i >= 0; i--) if (g_rt[i].up) touch_remove(&g_raw, i, true);
}
static int g_vfirst = -1;   /* the scripted finger driving the mouse */
/* the view the game reads: this tick's fingers (pressed when new), then last tick's fingers now gone (released) */
static arc_touch g_tv[ARC_TOUCH_MAX * 2]; static int g_tvn; static arc__input_snap g_tv_cur, g_tv_prev; static bool g_tv_ok;
static void touch_view(void) {
    if (g_tv_ok && g_tv_cur.tcount == g_cur.tcount && g_tv_prev.tcount == g_prev.tcount &&
        !memcmp(g_tv_cur.touch, g_cur.touch, sizeof g_cur.touch) && !memcmp(g_tv_prev.touch, g_prev.touch, sizeof g_prev.touch)) return;
    g_tv_ok = true; g_tv_cur.tcount = g_cur.tcount; g_tv_prev.tcount = g_prev.tcount;
    memcpy(g_tv_cur.touch, g_cur.touch, sizeof g_cur.touch); memcpy(g_tv_prev.touch, g_prev.touch, sizeof g_prev.touch);
    g_tvn = 0;
    for (int i = 0; i < g_cur.tcount && i < ARC_TOUCH_MAX; i++) {
        arc_touch *t = &g_tv[g_tvn++]; memset(t, 0, sizeof *t);
        t->id = g_cur.touch[i].id; t->x = g_cur.touch[i].x; t->y = g_cur.touch[i].y; t->start_x = g_cur.touch[i].sx; t->start_y = g_cur.touch[i].sy;
        t->pressed = true; for (int k = 0; k < g_prev.tcount && k < ARC_TOUCH_MAX; k++) if (g_prev.touch[k].id == g_cur.touch[i].id) t->pressed = false;
    }
    for (int k = 0; k < g_prev.tcount && k < ARC_TOUCH_MAX; k++) {
        bool still = false; for (int i = 0; i < g_cur.tcount && i < ARC_TOUCH_MAX; i++) still = still || g_cur.touch[i].id == g_prev.touch[k].id;
        if (still) continue;
        arc_touch *t = &g_tv[g_tvn++]; memset(t, 0, sizeof *t);
        t->id = g_prev.touch[k].id; t->x = g_prev.touch[k].x; t->y = g_prev.touch[k].y; t->start_x = g_prev.touch[k].sx; t->start_y = g_prev.touch[k].sy;
        t->released = true;
    }
}
int arc_touch_count(void) { touch_view(); return g_tvn; }
const arc_touch *arc_touch_get(int i) { touch_view(); return i >= 0 && i < g_tvn ? &g_tv[i] : NULL; }
const arc_touch *arc_touch_find(int id) { touch_view(); for (int i = 0; i < g_tvn; i++) if (g_tv[i].id == id) return &g_tv[i]; return NULL; }
void arc__input_mouse(float x, float y) { g_raw.mx = x; g_raw.my = y; }
void arc__input_mouse_rel(float dx, float dy) { g_raw.mx += dx; g_raw.my += dy; }
/* 0.6.7 mouse look. Headless (no platform support): captured as asked, and `mouse X Y` moves it, so tests run */
static bool g_capture;
void arc_mouse_capture(bool on) {
    if (on == g_capture) return;
    g_capture = on;
    if (G.plat && G.plat->mouse_capture) G.plat->mouse_capture(on);
}
bool arc_mouse_captured(void) { return g_capture && (!G.plat || !G.plat->mouse_captured || G.plat->mouse_captured()); }
void arc__input_wheel(float steps) { g_raw_wheel += steps; }
void arc__input_clear(void) { memset(g_raw.keys, 0, sizeof g_raw.keys); memset(g_latch, 0, sizeof g_latch); }
/* self-tests: make the platform's input now the current tick's (as a tick would; not recorded), so a test can drive
   the UI hooks (arc__ui_tick_begin/end) through several pretend ticks without running the main loop */
static void touch_raw_seen(void);
void arc__input_test_step(void) {
    g_prev = g_cur; arc__input_snap s = g_raw; touch_raw_seen();
    for (int i = 0; i < (int)sizeof s.keys; i++) { s.keys[i] |= g_latch[i]; g_latch[i] = 0; }
    memset(s.text, 0, sizeof s.text);
    size_t tl = strlen(g_text); if (tl >= sizeof s.text) tl = sizeof s.text - 1;   /* 0.8.5: text typed with arc__input_char, as a real frame does */
    memcpy(s.text, g_text, tl); s.text[tl] = 0; g_text[0] = 0;
    snap_pads(&s); s.wheel = g_raw_wheel; g_raw_wheel = 0;   /* 0.10.17: wheel steps too */
    g_cur = s;
}

bool   arc_key_down(arc_key k) { return k > 0 && k < ARC_KEY_COUNT && snap_get(&g_cur, k); }
bool   arc_key_pressed(arc_key k) { return arc_key_down(k) && !snap_get(&g_prev, k); }
bool   arc_key_released(arc_key k) { return k > 0 && k < ARC_KEY_COUNT && !snap_get(&g_cur, k) && snap_get(&g_prev, k); }
arc_v2 arc_mouse(void) {   /* (0.10.2: in a viewport's pixels inside it) */
    int vx, vy; if (arc__viewport_cur(&vx, &vy, NULL, NULL)) return arc_v2_make(g_cur.mx - (float)vx, g_cur.my - (float)vy);
    return arc_v2_make(g_cur.mx, g_cur.my);
}
arc_v2 arc_mouse_delta(void) { return arc_v2_make(g_cur.mx - g_prev.mx, g_cur.my - g_prev.my); }
float  arc_mouse_wheel(void) { return g_cur.wheel; }
float  arc_pad_axis(arc_axis a) { return a >= 0 && a < ARC_AXIS_COUNT ? g_cur.axes[a] : 0.0f; }
bool   arc_pad_connected(void) { return (g_cur.flags & ARC__IN_PAD) != 0; }
int    arc_pad_count(void) { int n = 0; for (int p = 0; p < ARC_MAX_PADS; p++) n += (int)(g_cur.padmask >> p & 1); return n; }
bool   arc_pad_is_connected(int p) { return p >= 0 && p < ARC_MAX_PADS && (g_cur.padmask >> p & 1); }
static bool pad_bit(const arc__input_snap *s, int p, arc_key k) { return p >= 0 && p < ARC_MAX_PADS && k >= ARC_PAD_A && k <= ARC_PAD_GUIDE && (s->pbtn[p] >> (k - ARC_PAD_A) & 1); }
bool   arc_pad_down(int p, arc_key k) { return pad_bit(&g_cur, p, k); }
bool   arc_pad_pressed(int p, arc_key k) { return pad_bit(&g_cur, p, k) && !pad_bit(&g_prev, p, k); }
bool   arc_pad_released(int p, arc_key k) { return !pad_bit(&g_cur, p, k) && pad_bit(&g_prev, p, k); }
float  arc_pad_axis_of(int p, arc_axis a) { return p >= 0 && p < ARC_MAX_PADS && a >= 0 && a < ARC_AXIS_COUNT ? g_cur.pax[p][a] : 0.0f; }
int    arc_pad_first(void) { for (int p = 0; p < ARC_MAX_PADS; p++) if (g_cur.padmask >> p & 1) return p; return -1; }
int    arc_pad_any_pressed(arc_key *button) {
    for (int p = 0; p < ARC_MAX_PADS; p++) for (int k = ARC_PAD_A; k <= ARC_PAD_GUIDE; k++)
        if (arc_pad_pressed(p, (arc_key)k)) { if (button) *button = (arc_key)k; return p; }
    return -1;
}
bool   arc_touch_mode(void) { return (g_cur.flags & ARC__IN_TOUCH) != 0; }
const char *arc_text_input(void) { return g_cur.text; }
arc_key arc_key_any_pressed(void) {
    for (int k = 1; k < ARC_MOUSE_LEFT; k++) if (arc_key_pressed((arc_key)k)) return (arc_key)k;
    return ARC_KEY_NONE;
}

static const struct { const char *name; arc_key key; } k_names[] = {
    { "SPACE", ARC_KEY_SPACE }, { "ESCAPE", ARC_KEY_ESCAPE }, { "ESC", ARC_KEY_ESCAPE }, { "ENTER", ARC_KEY_ENTER }, { "RETURN", ARC_KEY_ENTER },
    { "TAB", ARC_KEY_TAB }, { "BACKSPACE", ARC_KEY_BACKSPACE }, { "LEFT", ARC_KEY_LEFT }, { "RIGHT", ARC_KEY_RIGHT },
    { "UP", ARC_KEY_UP }, { "DOWN", ARC_KEY_DOWN }, { "SHIFT", ARC_KEY_SHIFT }, { "CTRL", ARC_KEY_CTRL }, { "ALT", ARC_KEY_ALT },
    { "F1", ARC_KEY_F1 }, { "F2", ARC_KEY_F2 }, { "F3", ARC_KEY_F3 }, { "F4", ARC_KEY_F4 }, { "F5", ARC_KEY_F5 }, { "F6", ARC_KEY_F6 },
    { "F7", ARC_KEY_F7 }, { "F8", ARC_KEY_F8 }, { "F9", ARC_KEY_F9 }, { "F10", ARC_KEY_F10 }, { "F11", ARC_KEY_F11 }, { "F12", ARC_KEY_F12 },
    { "GRAVE", ARC_KEY_GRAVE }, { "MOUSE_LEFT", ARC_MOUSE_LEFT }, { "MOUSE_RIGHT", ARC_MOUSE_RIGHT }, { "MOUSE_MIDDLE", ARC_MOUSE_MIDDLE },
    { "MINUS", ARC_KEY_MINUS }, { "EQUAL", ARC_KEY_EQUAL }, { "LBRACKET", ARC_KEY_LBRACKET }, { "RBRACKET", ARC_KEY_RBRACKET },
    { "BACKSLASH", ARC_KEY_BACKSLASH }, { "SEMICOLON", ARC_KEY_SEMICOLON }, { "APOSTROPHE", ARC_KEY_APOSTROPHE }, { "COMMA", ARC_KEY_COMMA },
    { "PERIOD", ARC_KEY_PERIOD }, { "SLASH", ARC_KEY_SLASH }, { "INSERT", ARC_KEY_INSERT }, { "DELETE", ARC_KEY_DELETE },
    { "HOME", ARC_KEY_HOME }, { "END", ARC_KEY_END }, { "PAGEUP", ARC_KEY_PAGEUP }, { "PAGEDOWN", ARC_KEY_PAGEDOWN }, { "KP_ENTER", ARC_KEY_KP_ENTER },
    { "PAD_A", ARC_PAD_A }, { "PAD_B", ARC_PAD_B }, { "PAD_X", ARC_PAD_X }, { "PAD_Y", ARC_PAD_Y }, { "PAD_LB", ARC_PAD_LB }, { "PAD_RB", ARC_PAD_RB },
    { "PAD_LT", ARC_PAD_LT }, { "PAD_RT", ARC_PAD_RT }, { "PAD_BACK", ARC_PAD_BACK }, { "PAD_START", ARC_PAD_START },
    { "PAD_LSTICK", ARC_PAD_LSTICK }, { "PAD_RSTICK", ARC_PAD_RSTICK }, { "PAD_UP", ARC_PAD_UP }, { "PAD_DOWN", ARC_PAD_DOWN },
    { "PAD_LEFT", ARC_PAD_LEFT }, { "PAD_RIGHT", ARC_PAD_RIGHT }, { "PAD_GUIDE", ARC_PAD_GUIDE },
};
static const char *k_axis_names[ARC_AXIS_COUNT] = { "LX", "LY", "RX", "RY", "LT", "RT" };
arc_key arc_key_from_name(const char *name) {
    if (!name || !*name) return ARC_KEY_NONE;
    if (!name[1]) { int c = toupper((unsigned char)name[0]); if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return (arc_key)c; if (c == ' ') return ARC_KEY_SPACE; }
    char up[32]; size_t i = 0; for (; name[i] && i < sizeof up - 1; i++) up[i] = (char)toupper((unsigned char)name[i]); up[i] = 0;
    for (int n = 0; n < ARC__COUNTOF(k_names); n++) if (!strcmp(up, k_names[n].name)) return k_names[n].key;
    return ARC_KEY_NONE;
}
const char *arc_key_name(arc_key k) {
    static char one[2];
    if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) { one[0] = (char)k; one[1] = 0; return one; }
    for (int n = 0; n < ARC__COUNTOF(k_names); n++) if (k_names[n].key == k) return k_names[n].name;
    return "?";
}

/* ======================================================================== */
/* Vars                                                                     */
/* ======================================================================== */

typedef struct { char name[48]; float *ptr; float initial; } var_rec;
static var_rec g_vars[256]; static int g_var_count;
static float *g_pending_sets_ptr[256];
static struct { char name[48]; float value; } g_presets[64]; static int g_preset_count;   /* --set name=value before init */

int arc__var_info(int i, const char **name, float **ptr, float *initial) {
    if (i < 0 || i >= g_var_count) return 0;
    *name = g_vars[i].name; *ptr = g_vars[i].ptr; *initial = g_vars[i].initial; return 1;
}
static bool g_inspector, g_dev_flag;
/* ---- FPS overlay: frame rate, timings, draw counts and a frame-time graph, drawn on top by the engine ---- */
#define FPS_HIST 600   /* 10 s at 60 fps; the graph shows the last 120 */
static bool g_fps_on; static float g_fps_hist[FPS_HIST]; static int g_fps_at, g_fps_n;
void arc_show_fps(bool on) { g_fps_on = on; }
bool arc_fps_shown(void) { return g_fps_on; }
static int g_fps_skip = 5;   /* start-up frames (loading, first uploads) aren't counted */
static void fps_sample(float ms) { if (g_fps_skip > 0) { g_fps_skip--; return; } g_fps_hist[g_fps_at] = ms; g_fps_at = (g_fps_at + 1) % FPS_HIST; if (g_fps_n < FPS_HIST) g_fps_n++; }
static int fps_cmp(const void *a, const void *b) { float x = *(const float *)a, y = *(const float *)b; return (x > y) - (x < y); }
void arc__fps_test_sample(float ms) { g_fps_skip = 0; fps_sample(ms); }   /* self-test */
arc_frame_times arc_get_frame_times(void) {
    arc_frame_times t; memset(&t, 0, sizeof t);
    int n = g_fps_n; if (!n) return t;
    static float s[FPS_HIST]; double sum = 0;
    for (int i = 0; i < n; i++) { s[i] = g_fps_hist[(g_fps_at - n + i + FPS_HIST) % FPS_HIST]; sum += s[i]; }
    for (int i = arc_maxi(0, n - 60); i < n; i++) t.max_1s = arc_maxf(t.max_1s, s[i]);   /* the last second, before sorting */
    qsort(s, (size_t)n, sizeof s[0], fps_cmp);
    #define PCT(p) s[arc_mini(n - 1, (int)((p) * (float)(n - 1) + 0.5f))]
    t.avg = (float)(sum / n); t.p50 = PCT(0.50f); t.p95 = PCT(0.95f); t.p99 = PCT(0.99f); t.max = s[n - 1];
    #undef PCT
    t.low1_fps = t.p99 > 0 ? 1000.0f / t.p99 : 0; t.samples = n;
    return t;
}
/* profiler zones (see Profiling below) */
typedef struct { const char *name; double start, sum, last; int calls, last_calls; } prof_rec;
static prof_rec g_prof[64]; static int g_prof_count;
/* ---- 0.7.1 frame budget: where each frame's time goes (display only) ---- */
static struct { float target; double tick, audio, present; arc_frame_budget avg; bool primed; } BG;
volatile float arc__audio_mix_ms;   /* the mixer's time since the last frame (written by the audio thread; display only) */
void arc_set_frame_budget(float ms) { BG.target = ms > 0 ? ms : 0; }
static float prof_last_ms(const char *name) { for (int i = 0; i < g_prof_count; i++) if (!strcmp(g_prof[i].name, name)) return (float)(g_prof[i].last * 1000.0); return 0; }
static bool prof_engine(const char *n) { return !strcmp(n, "draw") || !strcmp(n, "gfx.sort") || !strcmp(n, "gfx.submit"); }
static void budget_frame(float frame_ms) {   /* after prof_roll: this frame's parts, smoothed */
    float part[8] = { (float)BG.tick, prof_last_ms("draw"), prof_last_ms("gfx.sort"), prof_last_ms("gfx.submit"), (float)BG.present, (float)BG.audio + arc__audio_mix_ms, 0, frame_ms };
    arc__audio_mix_ms = 0;
    float used = 0; for (int i = 0; i < 6; i++) used += part[i];
    part[6] = arc_maxf(frame_ms - used, 0);
    float *dst[8] = { &BG.avg.tick_ms, &BG.avg.draw_ms, &BG.avg.sort_ms, &BG.avg.submit_ms, &BG.avg.present_ms, &BG.avg.audio_ms, &BG.avg.other_ms, &BG.avg.frame_ms };
    float k = BG.primed ? 0.1f : 1.0f;
    for (int i = 0; i < 8; i++) *dst[i] += (part[i] - *dst[i]) * k;
    float zm[64]; int zn = 0; const char *zname[64];   /* the game's zones, largest first, smoothed by name */
    for (int i = 0; i < g_prof_count; i++) if (!prof_engine(g_prof[i].name)) { zname[zn] = g_prof[i].name; zm[zn] = (float)(g_prof[i].last * 1000.0); zn++; }
    for (int i = 0; i < zn; i++) {
        float prev = zm[i]; for (int j = 0; j < BG.avg.zone_count; j++) if (!strcmp(BG.avg.zone_name[j], zname[i])) { prev = BG.avg.zone_ms[j]; break; }
        zm[i] = prev + (zm[i] - prev) * k;
    }
    for (int i = 1; i < zn; i++) for (int j = i; j > 0 && zm[j] > zm[j - 1]; j--) { float t = zm[j]; zm[j] = zm[j - 1]; zm[j - 1] = t; const char *q = zname[j]; zname[j] = zname[j - 1]; zname[j - 1] = q; }
    BG.avg.zone_count = arc_mini(zn, 8);
    for (int i = 0; i < BG.avg.zone_count; i++) { BG.avg.zone_name[i] = zname[i]; BG.avg.zone_ms[i] = zm[i]; }
    BG.primed = true;
}
arc_frame_budget arc_get_frame_budget(void) {
    arc_frame_budget b = BG.avg;
    b.budget_ms = BG.target > 0 ? BG.target : 1000.0f / 60.0f;
    b.over = b.frame_ms > b.budget_ms;
    return b;
}
static uint32_t fps_col(float ms) { return ms <= 1000.0f / 59.0f ? 0x60FF60 : ms <= 1000.0f / 29.0f ? 0xFFD040 : 0xFF5050; }   /* 60+ green, 30+ amber, else red */
static void fps_render(void) {
    if (!g_fps_on) return;
    arc_gfx_stats st = arc_stats();
    arc_frame_times ft = arc_get_frame_times();
    float k = arc_dpi_scale(), x = 8 * k, y = 8 * k, w = 240 * k, gh = 48 * k, ts = 1.5f * k, lh = 12 * ts;
    double ms = st.avg_frame_ms > 0 ? st.avg_frame_ms : st.frame_ms;
    char rbl[96]; int rbn = arc__rb_fps_line(rbl, sizeof rbl);   /* 0.8.1: a rollback match adds a line */
    arc_blend b = arc_get_blend(); arc_set_blend(ARC_BLEND_ALPHA);
    arc_draw_rect(x, y, w, lh * (float)(6 + rbn) + gh + 10 * k, arc_rgba(0x000000, 0.65f));
    float ty = y + 4 * k, tx = x + 4 * k;
    arc_draw_textf(tx, ty, ts, fps_col((float)ms), "%.0f FPS  %.2f ms", ms > 0 ? 1000.0 / ms : 0.0, ms); ty += lh;
    arc_draw_textf(tx, ty, ts, fps_col(ft.p99), "1%% low %.0f  max %.1f ms", ft.low1_fps, ft.max); ty += lh;
    arc_draw_textf(tx, ty, ts, 0xE0E0E0, "p50 %.1f p95 %.1f p99 %.1f", ft.p50, ft.p95, ft.p99); ty += lh;
    arc_draw_textf(tx, ty, ts, 0xE0E0E0, "cpu %.2f  tick %.2f ms", st.cpu_ms, st.tick_ms); ty += lh;
    arc_draw_textf(tx, ty, ts, 0xE0E0E0, "%d draws  %d inst", st.draw_calls, st.instances); ty += lh;
    arc_draw_textf(tx, ty, ts, 0xA0A0A0, "%d tris  %s", st.triangles, st.backend ? st.backend : "?"); ty += lh;
    if (rbn) { arc_draw_text(tx, ty, ts, 0x80D0FF, rbl); ty += lh; }
    /* graph: the last 120 frames. Top = 33.3 ms, or the slowest frame shown (to the next 16.7 ms) so spikes fit */
    float top = 33.3f;
    for (int i = 0; i < 120 && i < g_fps_n; i++) top = arc_maxf(top, g_fps_hist[(g_fps_at - 1 - i + FPS_HIST) % FPS_HIST]);
    top = ceilf(top / 16.667f) * 16.667f;
    float gy = ty + 2 * k, bw = w / 120.0f, scale = gh / top;
    for (float line = 16.667f; line < top - 0.1f; line += 16.667f)   /* 60 fps, 30 fps, 20 fps ... */
        arc_draw_rect(x, gy + gh - line * scale, w, k, arc_rgba(line < 17 ? 0x60FF60 : line < 34 ? 0xFFD040 : 0xFF5050, 0.35f));
    for (int i = 0; i < 120; i++) {
        int back = 119 - i; if (back >= g_fps_n) continue;
        float v = g_fps_hist[(g_fps_at - 1 - back + FPS_HIST) % FPS_HIST], h = arc_minf(v * scale, gh);
        if (h > 0) arc_draw_rect(x + (float)i * bw, gy + gh - h, arc_maxf(bw - 0.5f, 1), h, fps_col(v));
    }
    if (ft.p99 > 0) arc_draw_rect(x, gy + gh - arc_minf(ft.p99 * scale, gh), w, k, arc_rgba(0xFFFFFF, 0.8f));   /* p99 of 10 s */
    arc_draw_textf(x + w - arc_text_width(ts * 0.7f, arc_fmt("%.0f ms", top)) - 3 * k, gy + 1 * k, ts * 0.7f, 0xA0A0A0, "%.0f ms", top);
    /* 0.7.1 budget: each part as a bar, full width = the budget; red = this part alone is over it, amber = the frame is
       over and this is its biggest part */
    arc_frame_budget fb = arc_get_frame_budget();
    const char *pn[7] = { "tick", "draw", "sort", "submit", "present", "audio", "other" };
    float pv[7] = { fb.tick_ms, fb.draw_ms, fb.sort_ms, fb.submit_ms, fb.present_ms, fb.audio_ms, fb.other_ms };
    int big = 0; for (int i = 1; i < 7; i++) if (pv[i] > pv[big]) big = i;
    int rows = 8 + fb.zone_count; float bs = ts * 0.75f, bl = 10 * bs, by = gy + gh + 4 * k, lw = 7 * 8 * bs + 4 * k, bx = x + lw, bwid = w - lw - 46 * bs;
    arc_draw_rect(x, by - 2 * k, w, bl * (float)rows + 6 * k, arc_rgba(0x000000, 0.65f));
    arc_draw_textf(tx, by, bs, fb.over ? 0xFF5050 : 0x60FF60, "budget %.1f ms: frame %.2f (%.0f%%)", fb.budget_ms, fb.frame_ms, 100.0f * fb.frame_ms / fb.budget_ms); by += bl;
    for (int i = 0; i < 7 + fb.zone_count; i++) {
        bool zone = i >= 7; float v = zone ? fb.zone_ms[i - 7] : pv[i];
        uint32_t col = v > fb.budget_ms ? 0xFF5050 : (!zone && fb.over && i == big) ? 0xFFD040 : zone ? 0xA080FF : 0x40C0E0;
        arc_draw_text(tx, by, bs, 0xC0C0C0, zone ? arc_fmt("%.7s", fb.zone_name[i - 7]) : pn[i]);
        arc_draw_rect(bx, by, bwid, bs * 7, arc_rgba(0xFFFFFF, 0.12f));
        arc_draw_rect(bx, by, bwid * arc_clampf(v / fb.budget_ms, 0, 1), bs * 7, col);
        arc_draw_textf(bx + bwid + 3 * k, by, bs, col, "%.2f", v);
        by += bl;
    }
    arc_set_blend(b);
}
float *arc_var(const char *name, float *ptr, float initial) {
    *ptr = initial;
    for (int i = 0; i < g_preset_count; i++) if (!strcmp(g_presets[i].name, name)) *ptr = g_presets[i].value;
    for (int i = 0; i < g_var_count; i++) if (!strcmp(g_vars[i].name, name)) { g_vars[i].ptr = ptr; return ptr; }
    if (g_var_count < ARC__COUNTOF(g_vars)) {
        var_rec *v = &g_vars[g_var_count++];
        snprintf(v->name, sizeof v->name, "%s", name); v->ptr = ptr; v->initial = initial;
    }
    (void)g_pending_sets_ptr;
    return ptr;
}
bool arc_var_set(const char *name, float value) {
    for (int i = 0; i < g_var_count; i++) if (!strcmp(g_vars[i].name, name)) { *g_vars[i].ptr = value; return true; }
    return false;
}
float arc_var_get(const char *name) {
    for (int i = 0; i < g_var_count; i++) if (!strcmp(g_vars[i].name, name)) return *g_vars[i].ptr;
    return 0.0f;
}

/* ======================================================================== */
/* Profiling                                                                */
/* ======================================================================== */

int arc_prof_begin(const char *name) {
    int i = 0;
    for (; i < g_prof_count; i++) if (g_prof[i].name == name || !strcmp(g_prof[i].name, name)) break;
    if (i == g_prof_count) { if (g_prof_count >= 64) return -1; g_prof[i].name = name; g_prof_count++; }
    g_prof[i].start = arc__plat_time();
    return i;
}
void arc_prof_end(int id) {
    if (id < 0) return;
    g_prof[id].sum += arc__plat_time() - g_prof[id].start; g_prof[id].calls++;
}
static void prof_roll(void) {
    for (int i = 0; i < g_prof_count; i++) { g_prof[i].last = g_prof[i].sum; g_prof[i].last_calls = g_prof[i].calls; g_prof[i].sum = 0; g_prof[i].calls = 0; }
}

/* ======================================================================== */
/* Replay recording                                                         */
/* ======================================================================== */

/* File: "ARCR" u32 version u64 seed u32 tick_hz, then records { u32 tick; snap } whenever input
 * changes, then { u32 0xFFFFFFFF; u32 total_ticks }. Version 3 snap = arc__input_snap (keys, mouse, wheel,
 * gamepad axes, flags, typed text, and 0.10.5 every pad). Version 2 (one pad) is read too: its snaps are the start of
 * a version 3 one, and its pad becomes pad 0. */
#define ARC__REC_VERSION 4u
#define ARC__SNAP_V2 offsetof(arc__input_snap, padmask)
#define ARC__SNAP_V3 offsetof(arc__input_snap, tcount)   /* 0.10.11: version 4 adds the touches */
static FILE *g_rec_file;
static arc__input_snap g_rec_last; static bool g_rec_any;
static uint8_t *g_replay; static size_t g_replay_size, g_replay_pos; static bool g_replaying;
static uint32_t g_replay_total; static arc__input_snap g_replay_snap;
static uint64_t g_replay_len;      /* 0.7.2: the replay's length in ticks, read at open (for the timeline) */
static uint64_t g_rec_next;        /* 0.7.2: ticks written to the recording so far (going back never re-records them) */

/* 0.10.5 an older replay as the current version: every snap widened. Version 2: its one pad becomes pad 0; version 3
   (0.10.11): no touches. The header keeps its version. */
uint8_t *arc__replay_widen(const uint8_t *d, size_t size, size_t *out) {
    uint32_t ver = 2; if (size >= 8) memcpy(&ver, d + 4, 4);
    size_t old = ver == 3u ? ARC__SNAP_V3 : ARC__SNAP_V2;
    size_t cap = 20 + (size / (4 + old) + 2) * (4 + sizeof(arc__input_snap)) + 8, o = 20, pos = 20;
    uint8_t *nb = (uint8_t *)arc__xalloc(cap); memcpy(nb, d, 20);
    while (pos + 4 <= size) {
        uint32_t t; memcpy(&t, d + pos, 4);
        if (t == 0xFFFFFFFFu) { size_t k = pos + 8 <= size ? 8 : 4; memcpy(nb + o, d + pos, k); o += k; break; }
        if (pos + 4 + old > size) break;
        arc__input_snap sn; memset(&sn, 0, sizeof sn); memcpy(&sn, d + pos + 4, old);
        if (ver == 2u && (sn.flags & ARC__IN_PAD)) {
            sn.padmask = 1; memcpy(sn.pax[0], sn.axes, sizeof sn.axes);
            for (int k = ARC_PAD_A; k <= ARC_PAD_GUIDE; k++) if (snap_get(&sn, k)) sn.pbtn[0] |= 1u << (k - ARC_PAD_A);
        }
        memcpy(nb + o, &t, 4); memcpy(nb + o + 4, &sn, sizeof sn); o += 4 + sizeof sn; pos += 4 + old;
    }
    *out = o; return nb;
}
static bool replay_open(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    g_replay = (uint8_t *)arc__xalloc((size_t)n + 1); g_replay_size = fread(g_replay, 1, (size_t)n, f); fclose(f);
    if (g_replay_size < 20 || memcmp(g_replay, "ARCR", 4)) return false;
    uint32_t ver; memcpy(&ver, g_replay + 4, 4);
    if (ver != ARC__REC_VERSION && ver != 2u && ver != 3u) { printf("ARC error replay %s is version %u; this ARC reads versions 2 to %u\n", path, ver, ARC__REC_VERSION); return false; }
    if (ver == 2u || ver == 3u) { size_t n2 = 0; uint8_t *nb = arc__replay_widen(g_replay, g_replay_size, &n2); free(g_replay); g_replay = nb; g_replay_size = n2; }   /* 0.10.5 */
    memcpy(&g_seed, g_replay + 8, 8);
    uint32_t hz; memcpy(&hz, g_replay + 16, 4); if (hz) G.tick_hz = (int)hz;
    g_replay_pos = 20; g_replaying = true; g_replay_total = 0xFFFFFFFFu;
    for (size_t pos = 20; pos + 4 <= g_replay_size;) {       /* its length: the end record, or the last input + 1 */
        uint32_t t; memcpy(&t, g_replay + pos, 4);
        if (t == 0xFFFFFFFFu) { if (pos + 8 <= g_replay_size) { memcpy(&t, g_replay + pos + 4, 4); g_replay_len = t; } break; }
        if (pos + 4 + sizeof(arc__input_snap) > g_replay_size) break;
        g_replay_len = (uint64_t)t + 1; pos += 4 + sizeof(arc__input_snap);
    }
    return true;
}
static void replay_next(uint64_t tick, arc__input_snap *out) {
    for (;;) {
        if (g_replay_pos + 4 > g_replay_size) break;
        uint32_t t; memcpy(&t, g_replay + g_replay_pos, 4);
        if (t == 0xFFFFFFFFu) { if (g_replay_pos + 8 <= g_replay_size) memcpy(&g_replay_total, g_replay + g_replay_pos + 4, 4); break; }
        if (t > tick) break;
        if (g_replay_pos + 4 + sizeof(arc__input_snap) > g_replay_size) break;
        memcpy(&g_replay_snap, g_replay + g_replay_pos + 4, sizeof(arc__input_snap));
        g_replay_pos += 4 + sizeof(arc__input_snap);
    }
    *out = g_replay_snap;
}
static void rec_write(uint64_t tick, const arc__input_snap *s) {
    if (!g_rec_file) return;
    if (tick < g_rec_next) return;                        /* 0.7.2: an earlier tick replayed after going back */
    g_rec_next = tick + 1;
    if (g_rec_any && !memcmp(s, &g_rec_last, sizeof *s)) return;
    uint32_t t = (uint32_t)tick; fwrite(&t, 4, 1, g_rec_file); fwrite(s, sizeof *s, 1, g_rec_file);
    g_rec_last = *s; g_rec_any = true;
    fflush(g_rec_file);                                   /* a crash still leaves a replay up to the last input */
}
static void rec_close(void) {
    if (!g_rec_file) return;
    uint32_t end = 0xFFFFFFFFu, total = (uint32_t)(G.ticks > g_rec_next ? G.ticks : g_rec_next);
    fwrite(&end, 4, 1, g_rec_file); fwrite(&total, 4, 1, g_rec_file); fclose(g_rec_file); g_rec_file = NULL;
    if (g_saves_rec) { fclose(g_saves_rec); g_saves_rec = NULL; }
}

/* ======================================================================== */
/* Play sessions: every real play recorded for bug reports and balance      */
/* ======================================================================== */
/* sessions/<YYYYMMDD-HHMMSS>/ : input.rec (+ .saves) replays the session exactly; events.jsonl has the game's
   arc_event lines; log.txt the arc_log lines; mark_N.jpg screenshots from F8; summary.json at exit. */
#include <time.h>
static void write_dump(arc_json *j);
void arc__net_rec_open(const char *rec_path);   /* 0.8: input.rec.net beside a recording */
static struct { bool on; char dir[600]; FILE *ev, *log; int marks, events; double start; float toast; char toast_text[96]; } SES;
static bool g_resim, g_in_past;   /* re-simulating silently / a live game replaying its own past after going back (0.7.2) */
static void tl_add_mark(uint64_t tick, const char *name, bool bookmark);
static bool saves_going_over_past(void) { return g_resim || g_in_past; }
static void json_escape_into(FILE *f, const char *s) {
    for (; *s; s++) { unsigned char c = (unsigned char)*s; if (c == '"' || c == '\\') fprintf(f, "\\%c", c); else if (c < 32) fprintf(f, "\\u%04x", c); else fputc(c, f); }
}
static int g_events_emitted;                      /* (self-test: lines printed) */
int arc__test_events_emitted(void) { return g_events_emitted; }
static void event_emit(uint64_t tick, const char *name, const char *body, bool to_file) {
    size_t n = strlen(body);
    g_events_emitted++;
    double secs = (double)tick / (G.tick_hz > 0 ? G.tick_hz : 60);
    printf("ARC event {\"tick\":%llu,\"t\":%.2f,\"ev\":\"%s\"%s%s}\n", (unsigned long long)tick, secs, name, n ? "," : "", body); fflush(stdout);
    tl_add_mark(tick, name, !strcmp(name, "bookmark"));
    if (!SES.ev || !to_file) return;
    fprintf(SES.ev, "{\"tick\":%llu,\"t\":%.2f,\"ev\":\"", (unsigned long long)tick, secs);
    json_escape_into(SES.ev, name);
    fprintf(SES.ev, "\"%s%s}\n", n ? "," : "", body); fflush(SES.ev);
    SES.events++;
}
void arc_event(const char *name, const char *fields, ...) {
    char body[2048] = ""; size_t n = 0;
    if (fields && *fields) { va_list ap; va_start(ap, fields); int k = vsnprintf(body, sizeof body, fields, ap); va_end(ap); n = k < 0 ? 0 : (size_t)k; if (n >= sizeof body) n = sizeof body - 1; }
    body[n] = 0;
    if (arc__rb_event(name, body)) return;                /* 0.8.1: a rollback match logs a tick's events once it is confirmed */
    event_emit(G.ticks, name, body, !g_resim && !g_in_past);   /* (the past is already in the file) */
}
const char *arc_session_dir(void) { return SES.on ? SES.dir : NULL; }
void arc_bookmark(const char *note) {
    int k = ++SES.marks;
    if (SES.on && !g_resim && !g_in_past) arc_screenshot(arc_fmt("%s/mark_%02d.jpg", SES.dir, k));
    if (note && *note) arc_event("bookmark", "\"n\":%d,\"note\":\"%s\"", k, note); else arc_event("bookmark", "\"n\":%d", k);
    snprintf(SES.toast_text, sizeof SES.toast_text, SES.on ? "Bookmark %d saved: tell the developer what happened here" : "Bookmark %d", k);
    SES.toast = 3.0f;
}
void arc__session_log(const char *line) { if (SES.log) { fprintf(SES.log, "%llu %s\n", (unsigned long long)G.ticks, line); fflush(SES.log); } }
static void session_start(void) {
    const char *base = arc_arg_value("session-dir"); if (!base) base = "sessions";
    time_t now = time(NULL); struct tm *lt = localtime(&now);
    char stamp[32]; strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", lt);
    ARC__MKDIR(base);
    snprintf(SES.dir, sizeof SES.dir, "%s/%s", base, stamp);
    if (ARC__MKDIR(SES.dir) != 0) { for (int i = 2; i < 100; i++) { snprintf(SES.dir, sizeof SES.dir, "%s/%s-%d", base, stamp, i); if (ARC__MKDIR(SES.dir) == 0) break; } }
    SES.ev = fopen(arc_fmt("%s/events.jsonl", SES.dir), "wb");
    if (!SES.ev) { fprintf(stderr, "ARC log sessions: cannot write to %s (recording off)\n", SES.dir); return; }
    SES.log = fopen(arc_fmt("%s/log.txt", SES.dir), "wb");
    if (!g_rec_file) {
        g_rec_file = fopen(arc_fmt("%s/input.rec", SES.dir), "wb");
        if (g_rec_file) { uint32_t ver = ARC__REC_VERSION, hz = (uint32_t)G.tick_hz; fwrite("ARCR", 1, 4, g_rec_file); fwrite(&ver, 4, 1, g_rec_file); fwrite(&g_seed, 8, 1, g_rec_file); fwrite(&hz, 4, 1, g_rec_file); }
        g_saves_rec = fopen(arc_fmt("%s/input.rec.saves", SES.dir), "wb");
        arc__net_rec_open(arc_fmt("%s/input.rec", SES.dir));
    }
    SES.on = true; SES.start = -1;   /* set once the platform has started */
    printf("ARC session %s\n", SES.dir); fflush(stdout);
    arc_event("session_start", "\"game\":\"%s\",\"engine\":\"%s\",\"seed\":%llu,\"started\":\"%s\"", G.app.title ? G.app.title : "", ARC_VERSION, (unsigned long long)g_seed, stamp);
}
static void session_end(void) {
    if (!SES.on) return;
    arc_frame_times ft = arc_get_frame_times();
    double played = G.plat && SES.start >= 0 ? G.plat->time() - SES.start : 0;
    arc_event("session_end", "\"ticks\":%llu,\"seconds\":%.1f,\"bookmarks\":%d", (unsigned long long)G.ticks, played, SES.marks);
    FILE *f = fopen(arc_fmt("%s/summary.json", SES.dir), "wb");
    if (f) {
        arc_json j; memset(&j, 0, sizeof j); write_dump(&j);
        fprintf(f, "{\"game\":\""); json_escape_into(f, G.app.title ? G.app.title : ""); fprintf(f, "\",\"engine\":\"%s\",\"seed\":%llu,\"ticks\":%llu,\"seconds_played\":%.1f,"
                   "\"events\":%d,\"bookmarks\":%d,\"replay\":\"input.rec\",\"frame_p50_ms\":%.2f,\"frame_p99_ms\":%.2f,\"frame_max_ms\":%.2f,\"final_dump\":%s}\n",
                ARC_VERSION, (unsigned long long)g_seed, (unsigned long long)G.ticks, played, SES.events + 1, SES.marks, ft.p50, ft.p99, ft.max, j.buf ? j.buf : "{}");
        fclose(f); free(j.buf);
    }
    if (SES.ev) fclose(SES.ev);
    if (SES.log) fclose(SES.log);
    SES.ev = SES.log = NULL; SES.on = false;
}
static void toast_render(void) {
    if (SES.toast <= 0) return;
    float k = arc_dpi_scale(), ts = 1.5f * k, w = arc_text_width(ts, SES.toast_text) + 16 * k;
    arc_blend b = arc_get_blend(); arc_set_blend(ARC_BLEND_ALPHA);
    float x = (float)arc_width() * 0.5f - w * 0.5f, y = (float)arc_height() - 60 * k;
    arc_draw_rect(x, y, w, 12 * ts + 8 * k, arc_rgba(0x000000, 0.7f * arc_minf(1.0f, SES.toast)));
    arc_draw_text(x + 8 * k, y + 4 * k, ts, 0xFFE070, SES.toast_text);
    arc_set_blend(b);
}

/* ======================================================================== */
/* Agent interface: commands from --script files, --cmd strings and --stdin */
/* ======================================================================== */

/* 0.8 multiplayer (arc_net.c) */
void arc__net_frame(void); void arc__net_tick_begin(void); bool arc__net_waiting(void); void arc__net_agent(int argc, const char **argv, const char *line);
void arc__net_dump_json(arc_json *j); void arc__net_startup(void); void arc__net_shutdown(void);
void arc__net_replay_open(const char *replay_path);
typedef struct { char **lines; int count, pos; } cmd_queue;
static cmd_queue g_cmds;
static bool g_stdin_mode;
void arc__audio_init(int rate); void arc__audio_mix(float *out, int frames);   /* 0.10.13 --audio-sim */
extern bool arc__audio_test_mode;
static bool g_audio_sim; static double g_audio_sim_acc;
static uint64_t g_wait_until;            /* script is paused until this tick */
/* 0.10.10: a window build with --stdin reading a pipe (tools/arc_mcp.py) never blocks on it: the window keeps drawing
   and answering the OS while ARC waits for a command, and ticks run only when asked (step / wait, or `live run`) */
static bool g_live, g_live_run, g_live_ready, g_live_eof;
static float g_live_speed = 1.0f;
#if defined(_WIN32)
static bool live_stdin_ok(void) { DWORD n; return PeekNamedPipe(GetStdHandle(STD_INPUT_HANDLE), NULL, 0, NULL, &n, NULL) != 0; }
static int live_stdin_read(char *buf, int cap) {   /* bytes read, 0 none yet, -1 closed */
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE); DWORD avail = 0, got = 0;
    if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) return -1;
    if (!avail) return 0;
    if (!ReadFile(h, buf, avail < (DWORD)cap ? avail : (DWORD)cap, &got, NULL)) return -1;
    return (int)got;
}
#elif !defined(__EMSCRIPTEN__)
#include <poll.h>
static bool live_stdin_ok(void) { return true; }
static int live_stdin_read(char *buf, int cap) {
    struct pollfd pf = { 0, POLLIN, 0 };
    if (poll(&pf, 1, 0) <= 0) return 0;
    ssize_t n = read(0, buf, (size_t)cap);
    return n > 0 ? (int)n : -1;
}
#else
static bool live_stdin_ok(void) { return false; }
static int live_stdin_read(char *buf, int cap) { (void)buf; (void)cap; return -1; }
#endif
/* one whole line from stdin if there is one (without the newline); false if none yet; sets g_live_eof when closed */
static bool live_stdin_line(char *out, size_t cap) {
    static char pend[32768]; static size_t np;
    for (;;) {
        char *nl = (char *)memchr(pend, '\n', np);
        if (nl) {
            size_t len = (size_t)(nl - pend), k = len < cap - 1 ? len : cap - 1;
            memcpy(out, pend, k); out[k] = 0;
            memmove(pend, nl + 1, np - len - 1); np -= len + 1;
            return true;
        }
        if (np == sizeof pend) { memcpy(out, pend, cap - 1); out[cap - 1] = 0; np = 0; return true; }   /* an over-long line in pieces */
        int n = live_stdin_read(pend + np, (int)(sizeof pend - np));
        if (n < 0) {
            if (np) { size_t k = np < cap - 1 ? np : cap - 1; memcpy(out, pend, k); out[k] = 0; np = 0; return true; }
            g_live_eof = true; return false;
        }
        if (n == 0) return false;
        np += (size_t)n;
    }
}
/* 0.9.8 --shot-at 40,80,120 (or 40=start.png,80=mid.png): screenshots after those ticks, drawn only then */
#define ARC__SHOT_AT 64
static uint64_t g_shot_at[ARC__SHOT_AT]; static char g_shot_at_name[ARC__SHOT_AT][256]; static int g_shot_at_n, g_shot_at_next;
static bool g_shot_at_quit;              /* no --frames: quit after the last one */
static void shot_at_parse(const char *list) {
    const char *p = list;
    while (*p && g_shot_at_n < ARC__SHOT_AT) {
        char *end; unsigned long long t = strtoull(p, &end, 10);
        if (end == p) { fprintf(stderr, "ARC error --shot-at wants ticks like 40,80,120 or 40=start.png,80\n"); return; }
        const char *pat = arc_arg_value("shot-name"); if (!pat) pat = "shot_%d.png";
        char name[256] = { 0 };
        if (*end == '=') { const char *q = end + 1; size_t n = 0; while (q[n] && q[n] != ',' && n < 255) n++; memcpy(name, q, n); name[n] = 0; end = (char *)q + n; }
        else snprintf(name, sizeof name, pat, (int)t);
        int k = g_shot_at_n++;   /* kept in order of tick */
        while (k > 0 && g_shot_at[k - 1] > t) { g_shot_at[k] = g_shot_at[k - 1]; memcpy(g_shot_at_name[k], g_shot_at_name[k - 1], 256); k--; }
        g_shot_at[k] = t; memcpy(g_shot_at_name[k], name, 256);
        p = end; while (*p == ',' || *p == ' ') p++;
    }
}
/* headless with --draw-last: is this tick close enough to the end of the run or to a --shot-at shot to be drawn? */
static bool draw_wanted_now(void) {
    if (!G.headless || G.draw_last <= 0) return true;
    uint64_t n = (uint64_t)G.draw_last, t = G.ticks;
    if (G.max_ticks && t + n >= G.max_ticks) return true;
    if (g_shot_at_next < g_shot_at_n && t + n > g_shot_at[g_shot_at_next]) return true;
    return false;
}
static char g_shot_path[64][512]; static int g_shot_count;
void arc_screenshot_after_frame(const char *path) { if (g_shot_count < 64 && path) snprintf(g_shot_path[g_shot_count++], 512, "%s", path); }

static void cmd_push_multi(const char *text) {          /* split on newlines and ';' (whole: run_command checks the length) */
    for (const char *p = text, *start = text;; p++) {
        if (*p == '\n' || *p == ';' || *p == 0) {
            const char *a = start, *b = p;
            while (a < b && isspace((unsigned char)*a)) a++;
            while (b > a && isspace((unsigned char)b[-1])) b--;
            if (b > a && *a != '#') {
                size_t n = (size_t)(b - a); char *c = (char *)arc__xalloc(n + 1); memcpy(c, a, n); c[n] = 0;
                if (g_cmds.count % 64 == 0) g_cmds.lines = (char **)realloc(g_cmds.lines, sizeof(char *) * (size_t)(g_cmds.count + 64));
                g_cmds.lines[g_cmds.count++] = c;
            }
            if (!*p) break;
            start = p + 1;
        }
    }
}

/* push agent commands from outside the core (the web build exposes this to JavaScript) */
void arc__agent_push(const char *text) { cmd_push_multi(text); }

char *arc__json_capture(void (*fn)(arc_json *j)) { arc_json j; memset(&j, 0, sizeof j); arc_json_obj(&j); fn(&j); arc_json_end(&j); return j.buf; }
static void write_dump(arc_json *j) {
    arc_json_obj(j);
    arc_json_str_k(j, "engine", "ARC " ARC_VERSION);
    arc_json_int_k(j, "tick", (long long)G.ticks);
    arc_json_num_k(j, "time", arc_time());
    arc_json_int_k(j, "seed", (long long)g_seed);
    arc_json_int_k(j, "width", G.width); arc_json_int_k(j, "height", G.height);
    if (g_cur.padmask) {   /* 0.10.5 the connected pads: their buttons held and sticks */
        arc_json_arr_k(j, "pads");
        for (int p = 0; p < ARC_MAX_PADS; p++) if (g_cur.padmask >> p & 1) {
            arc_json_obj(j); arc_json_int_k(j, "pad", p);
            char b[160]; size_t n = 0; b[0] = 0;
            for (int k = ARC_PAD_A; k <= ARC_PAD_GUIDE; k++) if (g_cur.pbtn[p] >> (k - ARC_PAD_A) & 1) n += (size_t)snprintf(b + n, sizeof b - n, "%s%s", n ? " " : "", arc_key_name((arc_key)k) + 4);
            arc_json_str_k(j, "down", b); arc_json_num_k(j, "lx", g_cur.pax[p][0]); arc_json_num_k(j, "ly", g_cur.pax[p][1]);
            arc_json_end(j);
        }
        arc_json_end(j);
    }
    {   /* 0.10.11 the fingers this tick */
        int n = arc_touch_count(), down = 0;
        for (int i = 0; i < n; i++) down += !arc_touch_get(i)->released;
        arc_json_obj_k(j, "input");
        arc_json_int_k(j, "touches", down); arc_json_bool_k(j, "touch_mode", arc_touch_mode());
        arc_json_arr_k(j, "touch");
        for (int i = 0; i < n; i++) {
            const arc_touch *t = arc_touch_get(i);
            arc_json_obj(j); arc_json_int_k(j, "id", t->id); arc_json_num_k(j, "x", t->x); arc_json_num_k(j, "y", t->y);
            arc_json_num_k(j, "start_x", t->start_x); arc_json_num_k(j, "start_y", t->start_y);
            arc_json_bool_k(j, "pressed", t->pressed); arc_json_bool_k(j, "released", t->released);
            arc_json_end(j);
        }
        arc_json_end(j);
        arc_json_end(j);
    }
    arc_gfx_stats st = arc_stats();
    arc_json_obj_k(j, "stats");
    arc_json_str_k(j, "backend", st.backend);
    arc_json_int_k(j, "draw_calls", st.draw_calls); arc_json_int_k(j, "instances", st.instances);
    arc_json_int_k(j, "dropped", st.dropped); arc_json_int_k(j, "dropped_total", arc__gfx_dropped_total());
    arc_json_int_k(j, "triangles", st.triangles); arc_json_num_k(j, "frame_ms", st.frame_ms);
    arc_json_int_k(j, "vertices_2d", st.vertices_2d); arc_json_int_k(j, "vertices_2d_max", st.vertices_2d_max);   /* 0.10.13 */
    arc_json_num_k(j, "cpu_ms", st.cpu_ms); arc_json_num_k(j, "tick_ms", st.tick_ms);
    { arc_frame_times ft = arc_get_frame_times();
      arc_json_num_k(j, "frame_p50_ms", ft.p50); arc_json_num_k(j, "frame_p95_ms", ft.p95); arc_json_num_k(j, "frame_p99_ms", ft.p99);
      arc_json_num_k(j, "frame_max_ms", ft.max); arc_json_num_k(j, "low1_fps", ft.low1_fps); }
    arc_json_int_k(j, "perm_used", (long long)g_perm.used); arc_json_int_k(j, "frame_peak", (long long)g_frame.peak);
    arc_json_int_k(j, "particles", arc_particle_count());
    if (st.culled || st.cells_visible || st.static_draws || st.lod_instances[0] || st.lod_instances[1] || st.lod_instances[2] || st.lod_instances[3]) {   /* 0.7.1 */
        arc_json_int_k(j, "culled", st.culled); arc_json_int_k(j, "cells_visible", st.cells_visible); arc_json_int_k(j, "static_draws", st.static_draws);
        arc_json_arr_k(j, "lod"); for (int i = 0; i < 4; i++) arc_json_int(j, st.lod_instances[i]); arc_json_end(j);
    }
    arc_json_end(j);
    {   arc_frame_budget fb = arc_get_frame_budget();   /* 0.7.1 */
        arc_json_obj_k(j, "budget");
        arc_json_num_k(j, "target_ms", fb.budget_ms); arc_json_num_k(j, "frame_ms", fb.frame_ms); arc_json_bool_k(j, "over", fb.over);
        arc_json_num_k(j, "tick_ms", fb.tick_ms); arc_json_num_k(j, "draw_ms", fb.draw_ms); arc_json_num_k(j, "sort_ms", fb.sort_ms);
        arc_json_num_k(j, "submit_ms", fb.submit_ms); arc_json_num_k(j, "present_ms", fb.present_ms); arc_json_num_k(j, "audio_ms", fb.audio_ms);
        arc_json_num_k(j, "other_ms", fb.other_ms);
        if (fb.zone_count) { arc_json_obj_k(j, "zones"); for (int i = 0; i < fb.zone_count; i++) arc_json_num_k(j, fb.zone_name[i], fb.zone_ms[i]); arc_json_end(j); }
        arc_json_end(j);
    }
    if (g_prof_count) {
        arc_json_obj_k(j, "prof_ms");
        for (int i = 0; i < g_prof_count; i++) arc_json_num_k(j, g_prof[i].name, g_prof[i].last * 1000.0);
        arc_json_end(j);
    }
    if (g_var_count) {
        arc_json_obj_k(j, "vars");
        for (int i = 0; i < g_var_count; i++) arc_json_num_k(j, g_vars[i].name, *g_vars[i].ptr);
        arc_json_end(j);
    }
    arc__editor_dump_json(j);                       /* 0.7.2: only once a game has begun an editor */
    arc__net_dump_json(j);                          /* 0.8: only once the net layer is used */
    arc__brain_dump_json(j); arc__senses_dump_json(j);   /* 0.10.0: only once watched */
    if (G.app.dump) { arc_json_obj_k(j, "game"); G.app.dump(j); arc_json_end(j); }
    arc_json_end(j);
}
static void do_dump(const char *path) {
    if (!G.app.dump) arc__hint("no-dump", "the game has no dump() callback, so `dump` shows only engine state. Add .dump = dump to arc_app and write positions, counts, score and phase with arc_json_*_k: tests and agents can only check what the dump holds.");
    arc_json j; memset(&j, 0, sizeof j);
    write_dump(&j);
    if (path && *path) {
        if (arc_file_write(path, j.buf, j.len)) printf("ARC ok dump %s\n", path);
        else printf("ARC error dump cannot write %s\n", path);
    } else printf("ARC dump %s\n", j.buf ? j.buf : "{}");
    fflush(stdout); free(j.buf);
}
static void print_help(void) {
    puts("ARC help commands (one per line or separated by ';'):\n"
         "ARC help   step N | wait N        advance N ticks (default 1)\n"
         "ARC help   press K | release K     hold / let go of a key (A..Z 0..9 SPACE LEFT UP ESC MOUSE_LEFT ...)\n"
         "ARC help   tap K [N]               hold K for N ticks (default 1) then release\n"
         "ARC help   mouse X Y               move the mouse (pixels); click [K] taps MOUSE_LEFT\n"
         "ARC help   wheel N                 turn the mouse wheel N steps next tick (+ up, - down)\n"
         "ARC help   tap PAD_A | axis LX 0.5  gamepad buttons and sticks (LX LY RX RY LT RT; axis LX 0 lets go)\n"
         "ARC help   type TEXT               typed text (arc_text_input) next tick;  touch on|off  finger mode\n"
         "ARC help   touch down ID X Y / touch move ID X Y / touch up ID   fingers (multi-touch)\n"
         "ARC help   shot FILE.png           draw now and save a screenshot\n"
         "ARC help   dump [FILE.json]        engine + game state as JSON (stdout if no file)\n"
         "ARC help   reset / arc_reset       back to the state right after init (tick 0, vars and --set values); a game's own reset command wins, arc_reset is always ARC's\n"
         "ARC help   vars | set NAME V | get NAME   tunable variables\n"
         "ARC help   stats                   timings and draw counts, the frame budget\n"
         "ARC help   cells [on|off|draw]     visible cells (arc_cell_add) / culling on or off / wireframe overlay\n"
         "ARC help   lod [bias X|force N]    level-of-detail counts / quality bias / always level N (-1 = auto)\n"
         "ARC help   ui | ui_click LABEL      list the UI widgets of the last tick / click one (then step 2)\n"
         "ARC help   ui_fit WxH [WxH ...]    one tick at each window size: widgets outside the window / safe area or overlapping\n"
         "ARC help   sheet FILE.png [COLS] [cell WxH] [bg RRGGBB|transparent]   a contact sheet of the meshes arc_sheet_add registers\n"
         "ARC help   shot FILE.png crop      a screenshot trimmed to what differs from its corner colour\n"
         "ARC help   frame [FILE.json]       what the frame shows: meshes (names, screen boxes), texts, sprites, rects, lights, UI; render targets too\n"
         "ARC help   inspector [on|off]      the tunables panel (also the ` key with --dev)\n"
         "ARC help   fps [on|off]            the FPS overlay (also --fps, or F3 with --dev)\n"
         "ARC help   reload                  reload every asset loaded from disk (with --hot they reload when saved)\n"
         "ARC help   gif FILE.gif N [EVERY] [MAXW]   record N frames (every 2 ticks, at most 480 px wide)\n"
         "ARC help   shots PREFIX N [EVERY]  PNG frames PREFIX_00000.png ...\n"
         "ARC help   compare REF.png [TOL%]  draw now and compare (creates REF if missing; writes REF_diff.png)\n"
         "ARC help   seek T | rewind N       go to tick T / N ticks back (back needs save_state/load_state)\n"
         "ARC help   state_save [NAME] | state_load [NAME] | state_check   game state snapshots\n"
         "ARC help   editor on|off|paint X Y CH|fill X0 Y0 X1 Y1 CH|flood X Y CH|line ..|undo|redo|save|load|dump|info   tile editor\n"
         "ARC help   timeline on|off|seek T|step [N]|back [N]|play|pause|speed X|marks|info   replay timeline (F9)\n"
         "ARC help   net host|join ADDR|find|games|leave|status|players|ready|start|cmd TEXT|free TEXT|end_turn|log|sync|wait ...   multiplayer\n"
         "ARC help   net mode rollback|delay N|rollback N|input HEX|stop_at T|stats|synctest N   real-time rollback matches (0.8.1)\n"
         "ARC help   echo TEXT | quit | help\n"
         "ARC help   anything else is passed to the game's command() callback");
    fflush(stdout);
}

/* ---- input history and state snapshots: seek / rewind / state_save / state_load ----
   Every tick's input is kept in memory. Games that implement save_state/load_state also get a snapshot every
   so often, so `seek N` can go backwards: load the nearest earlier snapshot and re-simulate (silently) with the
   recorded inputs. Re-simulation is exact because ticks are deterministic. */
static arc__input_snap *g_hist; static uint64_t g_hist_len, g_hist_cap;
static void hist_set(uint64_t tick, const arc__input_snap *s) {
    if (tick >= (1u << 22)) return;                    /* ~19 hours at 60 Hz */
    if (tick >= g_hist_cap) {
        uint64_t cap = g_hist_cap ? g_hist_cap * 2 : 4096; while (cap <= tick) cap *= 2;
        arc__input_snap *n = (arc__input_snap *)realloc(g_hist, sizeof(arc__input_snap) * (size_t)cap);
        if (!n) return;
        g_hist = n; g_hist_cap = cap;
    }
    g_hist[tick] = *s; g_hist_len = tick + 1;
}
typedef struct { uint64_t tick; uint8_t *data; size_t size; } state_blob;
#define MAX_AUTO_STATES 128
static state_blob g_auto[MAX_AUTO_STATES]; static int g_auto_count; static uint64_t g_auto_every = 60;
static struct { char name[64]; state_blob b; } g_named[16];
static uint8_t *g_state_buf; static size_t g_state_cap;
/* engine-owned state that ticks change: particles (and their random stream), emitters, UI focus */
size_t arc__fx_state(arc_writer *w); bool arc__fx_state_read(arc_reader *r, bool commit);
size_t arc__ui_state(arc_writer *w); bool arc__ui_state_read(arc_reader *r, bool commit);
size_t arc__decal_state(arc_writer *w); bool arc__decal_state_read(arc_reader *r, bool commit);   /* 0.7: only once decals are used */
size_t arc__net_state(arc_writer *w); bool arc__net_state_read(arc_reader *r, bool commit);       /* 0.8: only once the net layer is used */
static size_t arc__engine_state_write(uint8_t *buf, size_t cap) {   /* (0.8.1: then the replayed loads' read positions) */
    arc_writer w = { buf, cap, 0 }; arc__fx_state(&w); arc__ui_state(&w); arc__decal_state(&w); arc__editor_state(&w); arc__net_state(&w); arc__saves_state(&w); arc__shots_state(&w); return w.len;
}
static bool arc__engine_state_read(const uint8_t *buf, size_t n, bool commit) {   /* commit false: only check it */
    arc_reader r = { buf, n, 0, false };
    return arc__fx_state_read(&r, commit) && arc__ui_state_read(&r, commit) && arc__decal_state_read(&r, commit) && arc__editor_state_read(&r, commit) &&
           arc__net_state_read(&r, commit) && arc__saves_state_read(&r, commit) && arc__shots_state_read(&r, commit) && !r.fail;
}
/* the whole state (header, game, engine) into *data, growing it when it is too small (0.8.1: rollback snapshots reuse
   their buffers); returns the size, 0 if the game has no save_state or it failed */
static size_t state_write(uint8_t **data, size_t *dcap) {
    if (!G.app.save_state) return 0;
    if (!g_state_cap) { g_state_cap = 1 << 20; g_state_buf = (uint8_t *)arc__xalloc(g_state_cap); }
    size_t n = G.app.save_state(g_state_buf, g_state_cap);
    if (n > g_state_cap) {
        free(g_state_buf); g_state_cap = n + (n >> 2); g_state_buf = (uint8_t *)arc__xalloc(g_state_cap);
        n = G.app.save_state(g_state_buf, g_state_cap);
        if (n > g_state_cap) return 0;
    }
    size_t head = 4 + 4 + 8 + sizeof(arc_rng) + sizeof(arc__input_snap) + 4 + sizeof(float) * (size_t)g_var_count + 8;
    size_t eng = arc__engine_state_write(NULL, 0), total = head + n + eng;
    if (*dcap < total) {
        size_t nc = total + (total >> 3) + 256; uint8_t *nd = (uint8_t *)realloc(*data, nc);
        if (!nd) return 0;
        *data = nd; *dcap = nc;
    }
    uint8_t *p = *data;
    uint32_t ver = 1, nv = (uint32_t)g_var_count; uint64_t gs = n;
    memcpy(p, "ARCS", 4); p += 4; memcpy(p, &ver, 4); p += 4; memcpy(p, &G.ticks, 8); p += 8;
    memcpy(p, &g_rng, sizeof g_rng); p += sizeof g_rng; memcpy(p, &g_cur, sizeof g_cur); p += sizeof g_cur;
    memcpy(p, &nv, 4); p += 4;
    for (int i = 0; i < g_var_count; i++) { memcpy(p, g_vars[i].ptr, sizeof(float)); p += sizeof(float); }
    memcpy(p, &gs, 8); p += 8; memcpy(p, g_state_buf, n); p += n;
    arc__engine_state_write(p, eng);
    return total;
}
static bool state_capture(state_blob *out) {
    uint8_t *d = NULL; size_t cap = 0, n = state_write(&d, &cap);
    if (!n) { free(d); return false; }
    out->tick = G.ticks; out->data = d; out->size = n;
    return true;
}
static bool state_restore(const state_blob *b) {
    size_t head = 4 + 4 + 8 + sizeof(arc_rng) + sizeof(arc__input_snap) + 4 + sizeof(float) * (size_t)g_var_count + 8;
    if (!G.app.load_state || !b->data || b->size < head || memcmp(b->data, "ARCS", 4)) return false;
    uint32_t ver; memcpy(&ver, b->data + 4, 4); if (ver != 1) return false;
    const uint8_t *p = b->data + 8; uint32_t nv; uint64_t gs, tick;
    memcpy(&tick, p, 8); p += 8;
    arc_rng rng; memcpy(&rng, p, sizeof rng); p += sizeof rng;
    arc__input_snap cur; memcpy(&cur, p, sizeof cur); p += sizeof cur;
    memcpy(&nv, p, 4); p += 4;
    if ((int)nv != g_var_count) return false;
    const uint8_t *vars = p; p += sizeof(float) * (size_t)g_var_count;
    memcpy(&gs, p, 8); p += 8;
    size_t off = (size_t)(p - b->data);
    if (gs > (uint64_t)(b->size - off)) return false;
    const uint8_t *eng = p + gs; size_t engn = b->size - off - (size_t)gs;
    if (!arc__engine_state_read(eng, engn, false)) return false;   /* particles, emitters, UI focus: checked first */
    if (!G.app.load_state(p, (size_t)gs)) return false;
    arc__engine_state_read(eng, engn, true);
    for (int i = 0; i < g_var_count; i++) memcpy(g_vars[i].ptr, vars + sizeof(float) * (size_t)i, sizeof(float));   /* only once the game accepted it */
    G.ticks = tick; g_rng = rng; g_cur = g_prev = cur;
    return true;
}
static void auto_states_drop_after(uint64_t tick) {
    int k = 0;
    for (int i = 0; i < g_auto_count; i++) { if (g_auto[i].tick <= tick) g_auto[k++] = g_auto[i]; else free(g_auto[i].data); }
    g_auto_count = k;
}
static void auto_state_maybe(void) {
    if (!G.app.save_state || !G.app.load_state || G.ticks % g_auto_every) return;
    if (arc__rb_live_match()) return;                  /* 0.8.1: a live rollback match's ticks may still be predictions */
    if (g_auto_count && g_auto[g_auto_count - 1].tick >= G.ticks) return;
    if (g_auto_count == MAX_AUTO_STATES) {             /* full: keep every other one and space them out twice as far */
        int k = 0;
        for (int i = 0; i < g_auto_count; i++) { if (i % 2 == 0) g_auto[k++] = g_auto[i]; else free(g_auto[i].data); }
        g_auto_count = k; g_auto_every *= 2;
        if (G.ticks % g_auto_every) return;
    }
    if (state_capture(&g_auto[g_auto_count])) g_auto_count++;
}
static void sim_tick_body(bool imm);
/* 0.9.8 `reset`: the state right after init (captured then, for games with save_state / load_state), or init run again
   on a fresh seed and vars (games without them: what init loads, it loads again). Vars go back to their defaults and
   --set values, virtual input is released, the timeline's history starts again. */
static state_blob g_reset0;
static void do_reset(void) {
    bool ok = false, how_init = false;
    if (g_reset0.data) ok = state_restore(&g_reset0);
    if (!ok && G.app.init) {
        arc_rng_seed(&g_rng, g_seed, 54u);
        for (int i = 0; i < g_var_count; i++) { float v = g_vars[i].initial; for (int k = 0; k < g_preset_count; k++) if (!strcmp(g_presets[k].name, g_vars[i].name)) v = g_presets[k].value; *g_vars[i].ptr = v; }
        G.ticks = 0;
        arc__gfx_begin(); arc__phase = ARC__PHASE_INIT; G.app.init(); arc__phase = 0; arc__gfx_end_offscreen();
        ok = true; how_init = true;
    }
    memset(&g_virtual, 0, sizeof g_virtual); memset(g_tap_ticks, 0, sizeof g_tap_ticks); g_mouse_virtual = false; g_vfirst = -1;
    memset(&g_cur, 0, sizeof g_cur); memset(&g_prev, 0, sizeof g_prev);
    g_wait_until = 0; g_hist_len = 0; auto_states_drop_after(0); G.acc = 0;
    arc_popups_clear();   /* 0.10.14: no popup from the last case floats into the next */
    if (ok) printf("ARC ok reset tick=%llu (%s)\n", (unsigned long long)G.ticks, how_init ? "init again" : "the state after init");
    else printf("ARC error reset: the game has no init to run again\n");
    fflush(stdout);
}
/* re-simulate silently from the current tick to `target` with the recorded inputs */
static void resim_to(uint64_t target) {
    bool imm = G.app.immediate;
    g_resim = true; arc__audio_silent = true;
    while (G.ticks < target && G.ticks < g_hist_len) {
        g_prev = g_cur; g_cur = g_hist[G.ticks];
        if (imm) { arc__gfx_begin(); arc__frame_record(true); }
        sim_tick_body(imm);
        if (imm) { arc__frame_keep(); arc__gfx_end_offscreen(); }
        G.ticks++;
        auto_state_maybe();
    }
    g_resim = false; arc__audio_silent = false;
}
/* 0.8.1 rollback: ARC's whole state into a reused buffer, back again, and one silent re-simulated tick */
static size_t rb_eng_capture(uint8_t **data, size_t *cap) { return state_write(data, cap); }
static bool rb_eng_restore(const uint8_t *data, size_t size) {
    state_blob b; b.tick = 0; b.data = (uint8_t *)(uintptr_t)data; b.size = size;
    return state_restore(&b);
}
static void rb_eng_resim_tick(void) {          /* (g_resim is set by the caller) */
    bool imm = G.app.immediate;
    g_prev = g_cur; if (G.ticks < g_hist_len) g_cur = g_hist[G.ticks];
    if (imm) { arc__gfx_begin(); arc__frame_record(true); }
    sim_tick_body(imm);
    if (imm) { arc__frame_keep(); arc__gfx_end_offscreen(); }
    G.ticks++;
}
static bool seek_to(uint64_t target, char *err, size_t errn) {
    if (target >= G.ticks) { g_wait_until = target; return true; }
    if (!G.app.save_state || !G.app.load_state) { snprintf(err, errn, "going back needs the game's save_state/load_state callbacks"); return false; }
    if (arc__rb_live_match()) { snprintf(err, errn, "going back is not possible during a live rollback match (replay its recording)"); return false; }
    /* 0.7.2: going back while recording is allowed: the recorded input is replayed up to the newest tick (see build_snapshot) */
    int best = -1;
    for (int i = 0; i < g_auto_count; i++) if (g_auto[i].tick <= target) best = i;
    if (best < 0 || !state_restore(&g_auto[best])) { snprintf(err, errn, "no snapshot at or before tick %llu", (unsigned long long)target); return false; }
    auto_states_drop_after(g_auto[best].tick);
    resim_to(target);
    return G.ticks == target;
}
static void state_free(state_blob *b) { free(b->data); memset(b, 0, sizeof *b); }

/* ======================================================================== */
/* 0.7.2 replay timeline (scrubber): display and seeking only               */
/* ======================================================================== */
/* The bar reads the platform's raw input once a frame (not the ticks' input), so using it never changes what a tick
   sees: seeking goes through the snapshots and the silent re-simulation above, which are exact. */
typedef struct { uint64_t tick; uint8_t bookmark; char name[31]; } tl_mark;
#define TL_MAX_MARKS 4096
static struct {
    bool on, paused, used, drag, lmb_prev, ticking;
    float speed;                                   /* 0 = 1x */
    uint8_t keys_prev[ARC_KEY_COUNT / 8];
    tl_mark *marks; int nmarks;
    char msg[160]; double msg_until;
    uint64_t drag_last;
} TL;
static void build_snapshot(void);
static float tl_speed(void) { return TL.speed > 0 ? TL.speed : 1.0f; }
static uint64_t tl_length(void) {
    uint64_t n = g_hist_len > G.ticks ? g_hist_len : G.ticks;
    if (g_replaying && g_replay_len > n) n = g_replay_len;
    return n;
}
static void tl_add_mark(uint64_t tick, const char *name, bool bookmark) {
    if (!TL.marks) TL.marks = (tl_mark *)arc__xalloc(sizeof(tl_mark) * TL_MAX_MARKS);
    int at = TL.nmarks;                                           /* kept in tick order (new ones usually go last: cheap) */
    while (at > 0 && TL.marks[at - 1].tick > tick) at--;
    for (int i = at - 1; i >= 0 && TL.marks[i].tick == tick; i--) if (!strncmp(TL.marks[i].name, name, sizeof TL.marks[i].name - 1)) return;   /* seen already */
    if (TL.nmarks >= TL_MAX_MARKS) return;
    memmove(&TL.marks[at + 1], &TL.marks[at], sizeof(tl_mark) * (size_t)(TL.nmarks - at));
    tl_mark *m = &TL.marks[at]; TL.nmarks++;
    m->tick = tick; m->bookmark = bookmark ? 1 : 0; snprintf(m->name, sizeof m->name, "%s", name);
}
/* the markers of a play session: events.jsonl beside the replay ({"tick":N,...,"ev":"name",...} per line) */
static int tl_load_events(const char *replay_path) {
    char path[1024]; snprintf(path, sizeof path, "%s", replay_path);
    char *slash = NULL; for (char *p = path; *p; p++) if (*p == '/' || *p == '\\') slash = p;
    if (slash) snprintf(slash + 1, sizeof path - (size_t)(slash + 1 - path), "events.jsonl"); else snprintf(path, sizeof path, "events.jsonl");
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    char line[4096]; int n = 0;
    while (fgets(line, sizeof line, f)) {
        const char *t = strstr(line, "\"tick\":"), *e = strstr(line, "\"ev\":\"");
        if (!t || !e) continue;
        uint64_t tick = strtoull(t + 7, NULL, 10); e += 6;
        char name[31]; size_t k = 0; while (e[k] && e[k] != '"' && k < sizeof name - 1) { name[k] = e[k]; k++; } name[k] = 0;
        if (!strcmp(name, "session_start") || !strcmp(name, "session_end")) continue;
        tl_add_mark(tick, name, !strcmp(name, "bookmark")); n++;
    }
    fclose(f); return n;
}
static void tl_message(const char *m) { snprintf(TL.msg, sizeof TL.msg, "%s", m); TL.msg_until = arc_real_time() + 3.0; }
/* forward without drawing, from the replay or the inputs already played */
static void tl_forward(uint64_t target) {
    bool imm = G.app.immediate;
    g_resim = true; arc__audio_silent = true;
    while (G.ticks < target) {
        if (!g_replaying && G.ticks >= g_hist_len) break;
        build_snapshot();
        if (imm) arc__gfx_begin();
        sim_tick_body(imm);
        if (imm) arc__gfx_end_offscreen();
        G.ticks++;
        auto_state_maybe();
    }
    g_resim = false; arc__audio_silent = false;
}
bool arc_timeline_seek(uint64_t t) {
    if (TL.ticking) { printf("ARC error timeline seek: not from inside a tick\n"); fflush(stdout); return false; }
    TL.used = true;
    uint64_t len = tl_length(); if (t > len) t = len;
    if (t == G.ticks) return true;
    if (t > G.ticks) { tl_forward(t); return G.ticks == t; }
    char err[160] = "";
    if (!seek_to(t, err, sizeof err)) { tl_message(err); printf("ARC error timeline %s\n", err); fflush(stdout); return false; }
    return true;
}
void   arc_timeline_show(bool on) { TL.on = on; if (on) TL.used = true; }
bool   arc_timeline_shown(void) { return TL.on; }
void   arc_timeline_set_paused(bool p) { TL.paused = p; }
bool   arc_timeline_paused(void) { return TL.paused; }
void   arc_timeline_set_speed(float x) { TL.speed = arc_clampf(x, 0.25f, 8.0f); }
uint64_t arc_timeline_length(void) { return tl_length(); }
int    arc_timeline_mark_count(void) { return TL.nmarks; }
bool   arc_timeline_mark(int i, uint64_t *tick, const char **name, bool *bookmark) {
    if (i < 0 || i >= TL.nmarks) return false;
    if (tick) *tick = TL.marks[i].tick;
    if (name) *name = TL.marks[i].name;
    if (bookmark) *bookmark = TL.marks[i].bookmark != 0;
    return true;
}
/* layout: buttons |< < play > >| speed, then the track */
typedef struct { float x, y, w, h, k, ts, bw, by, bh, tx0, tx1, tky, tkh; } tl_geom;
static tl_geom tl_layout(void) {
    tl_geom g; g.k = arc_dpi_scale(); g.w = (float)arc_width();
    g.ts = g.w < 640 ? 1.0f : 1.5f * g.k;
    g.h = 8.0f * g.ts * 2.0f + 26.0f * g.k; g.x = 0; g.y = (float)arc_height() - g.h;
    g.bw = 8.0f * g.ts * 3.0f; g.bh = 8.0f * g.ts + 12.0f * g.k; g.by = g.y + g.h - g.bh - 5.0f * g.k;
    g.tx0 = 6.0f * g.k + 6.0f * (g.bw + 3.0f * g.k) + 8.0f * g.k; g.tx1 = g.w - 12.0f * g.k;
    if (g.tx1 < g.tx0 + 10) g.tx1 = g.tx0 + 10;
    g.tkh = 12.0f * g.k; g.tky = g.by + (g.bh - g.tkh) * 0.5f;
    return g;
}
static float tl_button_x(const tl_geom *g, int i) { return 6.0f * g->k + (float)i * (g->bw + 3.0f * g->k); }
static bool tl_over_bar(float mx, float my) { if (!TL.on) return false; tl_geom g = tl_layout(); return my >= g.y && mx >= 0 && mx < g.w; }
static uint64_t tl_tick_at(const tl_geom *g, float mx) {
    float f = arc_clampf((mx - g->tx0) / (g->tx1 - g->tx0), 0, 1);
    return (uint64_t)(f * (float)tl_length() + 0.5f);
}
static void tl_step(int dir) {
    TL.paused = true;
    if (dir > 0) arc_timeline_seek(G.ticks + 1);
    else if (G.ticks > 0) arc_timeline_seek(G.ticks - 1);
}
/* once a frame on desktop: the bar's keys and mouse, from the platform's raw input */
static void tl_frame(void) {
    if (G.headless) return;
    uint8_t cur[ARC_KEY_COUNT / 8];
    for (int i = 0; i < (int)sizeof cur; i++) cur[i] = (uint8_t)(g_raw.keys[i] | g_latch[i]);
#define TL_DOWN(k) (((cur[(k) >> 3] >> ((k) & 7)) & 1) != 0)
#define TL_EDGE(k) (TL_DOWN(k) && !((TL.keys_prev[(k) >> 3] >> ((k) & 7)) & 1))
#define TL_EAT(k) (g_latch[(k) >> 3] &= (uint8_t)~(1 << ((k) & 7)))
    if ((G.app.dev_keys || g_dev_flag || g_replaying) && TL_EDGE(ARC_KEY_F9)) { TL.on = !TL.on; if (TL.on) TL.used = true; }
    bool lmb = TL_DOWN(ARC_MOUSE_LEFT), press = lmb && !TL.lmb_prev;
    if (TL.on) {
        if (g_replaying || TL.paused) {           /* a live game keeps its keys while it plays */
            static const arc_key ks[] = { ARC_KEY_SPACE, ARC_KEY_LEFT, ARC_KEY_RIGHT, ARC_KEY_COMMA, ARC_KEY_PERIOD, ARC_KEY_MINUS, ARC_KEY_EQUAL, ARC_KEY_HOME, ARC_KEY_END };
            if (TL_EDGE(ARC_KEY_SPACE)) TL.paused = !TL.paused;
            if (TL_EDGE(ARC_KEY_LEFT) || TL_EDGE(ARC_KEY_COMMA)) tl_step(-1);
            if (TL_EDGE(ARC_KEY_RIGHT) || TL_EDGE(ARC_KEY_PERIOD)) tl_step(1);
            if (TL_EDGE(ARC_KEY_MINUS)) arc_timeline_set_speed(tl_speed() * 0.5f);
            if (TL_EDGE(ARC_KEY_EQUAL)) arc_timeline_set_speed(tl_speed() * 2.0f);
            if (TL_EDGE(ARC_KEY_HOME)) arc_timeline_seek(0);
            if (TL_EDGE(ARC_KEY_END)) arc_timeline_seek(tl_length());
            if (!g_replaying) for (int i = 0; i < ARC__COUNTOF(ks); i++) TL_EAT(ks[i]);
        }
        tl_geom g = tl_layout(); float mx = g_raw.mx, my = g_raw.my;
        if (press && my >= g.by && my < g.by + g.bh) {
            for (int i = 0; i < 6; i++) {
                float bx = tl_button_x(&g, i);
                if (mx < bx || mx >= bx + g.bw) continue;
                if (i == 0) arc_timeline_seek(0);
                else if (i == 1) tl_step(-1);
                else if (i == 2) TL.paused = !TL.paused;
                else if (i == 3) tl_step(1);
                else if (i == 4) arc_timeline_seek(tl_length());
                else { float sp = tl_speed() * 2.0f; arc_timeline_set_speed(sp > 8.0f ? 0.25f : sp); }
            }
            if (mx >= g.tx0 - 4 * g.k && mx <= g.tx1 + 4 * g.k) { TL.drag = true; TL.drag_last = (uint64_t)-1; }
        }
        if (TL.drag && lmb) { uint64_t t = tl_tick_at(&g, mx); if (t != TL.drag_last) { TL.drag_last = t; arc_timeline_seek(t); } }
        if (!lmb) TL.drag = false;
        if (press && my >= g.y) TL_EAT(ARC_MOUSE_LEFT);     /* a click on the bar is not the game's */
    }
    memcpy(TL.keys_prev, cur, sizeof cur); TL.lmb_prev = lmb;
    if (g_replaying) memset(g_latch, 0, sizeof g_latch);   /* a replay's ticks never read the live keys: nothing else clears them */
#undef TL_DOWN
#undef TL_EDGE
#undef TL_EAT
}
static void tl_render(void) {
    if (!TL.on) return;
    arc_blend b = arc_get_blend(); arc_set_blend(ARC_BLEND_ALPHA);
    tl_geom g = tl_layout(); uint64_t len = tl_length();
    arc_draw_rect(g.x, g.y, g.w, g.h, arc_rgba(0x0C0F14, 0.88f));
    double hz = G.tick_hz > 0 ? (double)G.tick_hz : 60.0;
    float sp = tl_speed();
    arc_draw_textf(6 * g.k, g.y + 5 * g.k, g.ts, 0xE8ECF4, "tick %llu / %llu   %.2f s / %.2f s   %gx   %s%s", (unsigned long long)G.ticks, (unsigned long long)len,
                   (double)G.ticks / hz, (double)len / hz, sp, TL.paused ? "PAUSED" : "PLAYING", g_replaying ? "  REPLAY" : g_rec_file ? "  RECORDING" : "");
    if (TL.msg[0] && arc_real_time() < TL.msg_until) arc_draw_text(g.w * 0.5f, g.y + 4 * g.k, g.ts, 0xFF7060, TL.msg);
    const char *lab[6] = { "|<<", "<|", TL.paused ? ">" : "||", "|>", ">>|", "" };
    char spd[16]; snprintf(spd, sizeof spd, "%gx", sp); lab[5] = spd;
    for (int i = 0; i < 6; i++) {
        float bx = tl_button_x(&g, i), sc = g.ts; while (sc > 1 && arc_text_width(sc, lab[i]) > g.bw - 4) sc -= 0.5f;
        arc_draw_rect(bx, g.by, g.bw, g.bh, (i == 2 && !TL.paused) ? 0x4E6A96 : 0x2A303C);
        arc_draw_text(bx + (g.bw - arc_text_width(sc, lab[i])) * 0.5f, g.by + (g.bh - 8 * sc) * 0.5f, sc, 0xE8ECF4, lab[i]);
    }
    float tw = g.tx1 - g.tx0, f = len ? (float)((double)G.ticks / (double)len) : 0.0f;
    arc_draw_rect(g.tx0, g.tky, tw, g.tkh, 0x2A303C);
    arc_draw_rect(g.tx0, g.tky, tw * arc_clampf(f, 0, 1), g.tkh, 0x4E6A96);
    float mx = g_raw.mx, my = g_raw.my; int hover = -1;
    for (int i = 0; i < TL.nmarks && len; i++) {
        float x = g.tx0 + tw * (float)((double)TL.marks[i].tick / (double)len);
        bool bm = TL.marks[i].bookmark != 0;
        arc_draw_rect(x - g.k, g.tky - (bm ? 6 : 3) * g.k, 2 * g.k, g.tkh + (bm ? 12 : 6) * g.k, bm ? 0xFFD040 : 0x40C8E0);
        if (fabsf(mx - x) <= 3 * g.k && my >= g.y) hover = i;
    }
    float px = g.tx0 + tw * arc_clampf(f, 0, 1);
    arc_draw_rect(px - 1.5f * g.k, g.tky - 5 * g.k, 3 * g.k, g.tkh + 10 * g.k, 0xFFFFFF);
    if (hover >= 0) {
        const char *t = arc_fmt("%s @%llu", TL.marks[hover].name, (unsigned long long)TL.marks[hover].tick);
        float w = arc_text_width(g.ts, t) + 8 * g.k, x = arc_clampf(mx - w * 0.5f, 0, g.w - w), y = g.y - 8 * g.ts - 10 * g.k;
        arc_draw_rect(x, y, w, 8 * g.ts + 6 * g.k, arc_rgba(0x000000, 0.8f));
        arc_draw_text(x + 4 * g.k, y + 3 * g.k, g.ts, TL.marks[hover].bookmark ? 0xFFD040 : 0x40C8E0, t);
    }
    arc_set_blend(b);
}
static void tl_command(int argc, const char **argv) {
    const char *sub = argc > 1 ? argv[1] : "";
    if (!*sub || !strcmp(sub, "on") || !strcmp(sub, "off")) { arc_timeline_show(*sub ? !strcmp(sub, "on") : !TL.on); printf("ARC ok timeline %s\n", TL.on ? "on" : "off"); }
    else if (!strcmp(sub, "seek") && argc >= 3) {
        long long t = atoll(argv[2]);
        if (arc_timeline_seek((uint64_t)(t < 0 ? 0 : t))) printf("ARC ok timeline seek tick=%llu\n", (unsigned long long)G.ticks);
    }
    else if (!strcmp(sub, "step") || !strcmp(sub, "back")) {
        long long n = argc >= 3 ? atoll(argv[2]) : 1; if (n < 0) n = 0;
        uint64_t t = !strcmp(sub, "step") ? G.ticks + (uint64_t)n : ((uint64_t)n > G.ticks ? 0 : G.ticks - (uint64_t)n);
        if (arc_timeline_seek(t)) printf("ARC ok timeline %s tick=%llu\n", sub, (unsigned long long)G.ticks);
    }
    else if (!strcmp(sub, "play") || !strcmp(sub, "pause")) { TL.paused = !strcmp(sub, "pause"); printf("ARC ok timeline %s\n", sub); }
    else if (!strcmp(sub, "speed") && argc >= 3) { arc_timeline_set_speed((float)atof(argv[2])); printf("ARC ok timeline speed %g\n", tl_speed()); }
    else if (!strcmp(sub, "marks")) {
        for (int i = 0; i < TL.nmarks; i++) printf("ARC mark tick=%llu kind=%s name=%s\n", (unsigned long long)TL.marks[i].tick, TL.marks[i].bookmark ? "bookmark" : "event", TL.marks[i].name);
        printf("ARC ok timeline marks %d\n", TL.nmarks);
    }
    else if (!strcmp(sub, "info")) printf("ARC timeline on=%d tick=%llu length=%llu paused=%d speed=%g marks=%d replay=%d snapshots=%d\n", TL.on, (unsigned long long)G.ticks,
                                          (unsigned long long)tl_length(), TL.paused, tl_speed(), TL.nmarks, g_replaying, g_auto_count);
    else printf("ARC error timeline: on|off | seek T | step [N] | back [N] | play | pause | speed X | marks | info\n");
    fflush(stdout);
}
bool arc__dev_keys(void) { return G.app.dev_keys || g_dev_flag; }
bool arc__sim_replaying(void) { return g_resim || g_in_past || g_replaying; }

/* ---- capture: GIF animations, PNG sequences, screenshot comparison ---- */
typedef struct { int mode; int left, every, index, maxw; char path[512]; arc__gif gif; } capture;   /* mode 1 gif, 2 png sequence */
static capture g_caps[2];   /* a GIF and a PNG sequence can record at the same time */
static uint8_t *downscale(const uint8_t *px, int w, int h, int k, int *ow, int *oh) {
    if (k <= 1) { *ow = w; *oh = h; uint8_t *c = (uint8_t *)arc__xalloc((size_t)w * h * 4); memcpy(c, px, (size_t)w * h * 4); return c; }
    int W = w / k, H = h / k; uint8_t *o = (uint8_t *)arc__xalloc((size_t)W * H * 4);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int acc[4] = { 0, 0, 0, 0 };
        for (int yy = 0; yy < k; yy++) for (int xx = 0; xx < k; xx++) { const uint8_t *p = px + ((size_t)(y * k + yy) * w + (x * k + xx)) * 4; for (int c = 0; c < 4; c++) acc[c] += p[c]; }
        for (int c = 0; c < 4; c++) o[((size_t)y * W + x) * 4 + c] = (uint8_t)(acc[c] / (k * k));
    }
    *ow = W; *oh = H; return o;
}
static bool render_now(void);
static bool g_cap_rendered;
static void capture_one(capture *cp, bool *imm_open);
static void capture_after_tick(bool *imm_open) { g_cap_rendered = false; capture_one(&g_caps[0], imm_open); capture_one(&g_caps[1], imm_open); }
static void capture_one(capture *cp, bool *imm_open) {
#define g_cap (*cp)
    if (!g_cap.mode || g_resim || (G.ticks % (uint64_t)g_cap.every)) return;
    if (!g_cap_rendered) {
        if (G.app.immediate && *imm_open) { arc__gfx_end(true); *imm_open = false; }
        else if (!render_now()) return;
        g_cap_rendered = true;
    }
    int w, h; uint8_t *px = arc__gfx_read_frame(&w, &h);
    if (!px) { printf("ARC error capture cannot read the frame\n"); fflush(stdout); g_cap.mode = 0; return; }
    if (g_cap.mode == 1) {
        int k = g_cap.maxw > 0 ? (w + g_cap.maxw - 1) / g_cap.maxw : 1, ow, oh;
        uint8_t *sm = downscale(px, w, h, k, &ow, &oh);
        if (g_cap.index == 0 && !arc__gif_begin(&g_cap.gif, g_cap.path, ow, oh, (double)g_cap.every * G.dt * 100.0)) {
            printf("ARC error gif cannot write %s\n", g_cap.path); fflush(stdout); g_cap.mode = 0; free(sm); free(px); return;
        }
        if (ow == g_cap.gif.w && oh == g_cap.gif.h) arc__gif_frame(&g_cap.gif, sm);   /* (a resized window keeps the first size) */
        free(sm);
    } else {
        char path[600]; snprintf(path, sizeof path, "%s_%05d.png", g_cap.path, g_cap.index);
        if (!arc__image_write(path, px, w, h)) { printf("ARC error shots cannot write %s\n", path); fflush(stdout); }
    }
    free(px);
    g_cap.index++;
    if (--g_cap.left <= 0) {
        if (g_cap.mode == 1) { bool ok = arc__gif_end(&g_cap.gif); printf(ok ? "ARC ok gif %s %d frames\n" : "ARC error gif %s\n", g_cap.path, g_cap.index); }
        else printf("ARC ok shots %s_*.png %d frames\n", g_cap.path, g_cap.index);
        fflush(stdout); g_cap.mode = 0;
    }
#undef g_cap
}
/* render now and compare with a reference PNG (made from this frame if it doesn't exist yet) */
static void compare_shot(const char *ref, float tol_pct) {
    if (!render_now()) { printf("ARC error compare nothing to draw\n"); fflush(stdout); return; }
    int w, h; uint8_t *px = arc__gfx_read_frame(&w, &h);
    if (!px) { printf("ARC error compare cannot read the frame\n"); fflush(stdout); return; }
    FILE *f = fopen(ref, "rb");
    if (!f) {
        bool ok = arc__png_write(ref, px, w, h);
        printf(ok ? "ARC ok compare %s created\n" : "ARC error compare cannot write %s\n", ref); fflush(stdout); free(px); return;
    }
    fclose(f);
    int rw, rh; uint8_t *rp = arc_image_load(ref, &rw, &rh);
    if (!rp || rw != w || rh != h) { printf("ARC error compare %s is %dx%d, the frame is %dx%d\n", ref, rp ? rw : 0, rp ? rh : 0, w, h); fflush(stdout); free(rp); free(px); return; }
    long bad = 0;
    uint8_t *diff = (uint8_t *)arc__xalloc((size_t)w * h * 4);
    for (long i = 0; i < (long)w * h; i++) {
        const uint8_t *a = px + i * 4, *b = rp + i * 4; int m = 0;
        for (int c = 0; c < 3; c++) { int d = abs(a[c] - b[c]); if (d > m) m = d; }
        uint8_t *o = diff + i * 4;
        if (m > 24) { bad++; o[0] = 255; o[1] = o[2] = 0; } else { o[0] = o[1] = o[2] = (uint8_t)((a[0] + a[1] + a[2]) / 9); }
        o[3] = 255;
    }
    double pct = 100.0 * (double)bad / ((double)w * h);
    char dpath[600]; snprintf(dpath, sizeof dpath, "%s", ref);
    char *dot = strrchr(dpath, '.'); if (dot) *dot = 0;
    strncat(dpath, "_diff.png", sizeof dpath - strlen(dpath) - 1);
    if (pct <= tol_pct) printf("ARC ok compare %s %.4f%%\n", ref, pct);
    else { arc__png_write(dpath, diff, w, h); printf("ARC error compare %s %.4f%% of pixels differ (limit %.4f%%), see %s\n", ref, pct, tol_pct, dpath); }
    fflush(stdout); free(diff); free(rp); free(px);
}

/* draw a frame now (between ticks) for a screenshot; immediate-mode games show the last frame they built */
static bool render_now(void) {
    if (!G.started) return false;
    if (G.app.immediate) return arc__gfx_render_last();
    if (!G.app.draw) return false;
    arc__gfx_begin();
    int pd = arc_prof_begin("draw");
    arc__frame_alpha = G.headless ? 1.0f : (float)(G.acc / G.dt);
    arc__phase = ARC__PHASE_DRAW; G.app.draw(arc__frame_alpha); arc__phase = 0;
    arc__viewport_close();   /* 0.10.2 */
    arc__scene_after_draw();
    arc__popups_render();   /* 0.10.14: after the game's 3D, before ARC's UI */
    arc__ui_render(); if (G.app.draw_top) G.app.draw_top(arc__frame_alpha); arc__editor_render(); tl_render(); fps_render(); toast_render();
    arc_prof_end(pd);
    arc__gfx_end(true);
    return true;
}
/* returns true if the script should stop executing for now (a wait started) */
/* a command is at most ARC__CMD_WORDS words and ARC__CMD_CHARS characters; a longer one is refused with an
   "ARC error" line rather than run cut short (0.6.6, from MechBattleArc: a cut command looks like a rules bug) */
#define ARC__CMD_WORDS 64
#define ARC__CMD_CHARS 4096
static bool run_command(const char *line) {
    char buf[ARC__CMD_CHARS];
    size_t len = strlen(line);
    if (len >= sizeof buf) { printf("ARC error command is longer than %d characters, not run: %.40s...\n", ARC__CMD_CHARS - 1, line); fflush(stdout); return false; }
    memcpy(buf, line, len + 1);
    const char *argv[ARC__CMD_WORDS]; int argc = 0;
    /* words split at spaces and tabs; 0.10.9: "double quotes" keep spaces inside one word (the quotes are dropped:
       editor rename "Cargo Hold", editor object add 3 3 droid note="a repair droid") */
    for (char *r = buf, *w = buf; *r;) {
        while (*r == ' ' || *r == '\t') r++;
        if (!*r) break;
        if (argc == ARC__CMD_WORDS) { printf("ARC error command has more than %d words, not run: %s ...\n", ARC__CMD_WORDS, argv[0]); fflush(stdout); return false; }
        char *word = w; bool q = false;
        for (; *r && (q || (*r != ' ' && *r != '\t')); r++) { if (*r == '"') q = !q; else *w++ = *r; }
        if (*r) r++;
        *w++ = 0; argv[argc++] = word;
    }
    if (!argc) return false;
    const char *c = argv[0];
    if (!strcmp(c, "step") || !strcmp(c, "wait")) {
        int n = argc > 1 ? atoi(argv[1]) : 1; if (n < 0) n = 0;
        g_wait_until = G.ticks + (uint64_t)n; return n > 0;
    }
    if (!strcmp(c, "press") || !strcmp(c, "release") || !strcmp(c, "tap") || !strcmp(c, "click")) {
        arc_key k = !strcmp(c, "click") ? (argc > 1 ? arc_key_from_name(arc_fmt("MOUSE_%s", argv[1])) : ARC_MOUSE_LEFT) : arc_key_from_name(argc > 1 ? argv[1] : "");
        if (!k) {   /* not a key: the game may have a command with this name (games should still avoid these names) */
            if (G.app.command && G.app.command(c, argc - 1, argv + 1)) { printf("ARC ok %s\n", c); fflush(stdout); return false; }
            printf("ARC error %s: unknown key '%s' (%s is an ARC key command)\n", c, argc > 1 ? argv[1] : "", c); fflush(stdout); return false;
        }
        if (!strcmp(c, "press")) snap_set(&g_virtual, k, true);
        else if (!strcmp(c, "release")) { snap_set(&g_virtual, k, false); g_tap_ticks[k] = 0; }
        else { int n = (!strcmp(c, "tap") && argc > 2) ? atoi(argv[2]) : 1; snap_set(&g_virtual, k, true); g_tap_ticks[k] = (uint8_t)arc_clampi(n, 1, 255); }
        return false;
    }
    if (!strcmp(c, "axis") && argc >= 3) {          /* axis LX 0.5  (0 hands the axis back to the real pad) */
        for (int a = 0; a < ARC_AXIS_COUNT; a++) if (!strcmp(argv[1], k_axis_names[a]) || !strcmp(argv[1], arc_fmt("PAD_%s", k_axis_names[a]))) {
            g_virtual.axes[a] = arc_clampf((float)atof(argv[2]), -1, 1); g_virtual.flags |= ARC__IN_PAD; return false;
        }
        printf("ARC error unknown axis '%s' (LX LY RX RY LT RT)\n", argv[1]); fflush(stdout); return false;
    }
    if (!strcmp(c, "pad") && argc >= 3) {           /* 0.10.5 pad N connect|disconnect | press|release BUTTON | axis LX 0.5 */
        int p = atoi(argv[1]); const char *sub = argv[2];
        if (p < 0 || p >= ARC_MAX_PADS) { printf("ARC error pad: 0..%d\n", ARC_MAX_PADS - 1); fflush(stdout); return false; }
        if (!strcmp(sub, "connect")) g_vpads[p].on = true;
        else if (!strcmp(sub, "disconnect")) memset(&g_vpads[p], 0, sizeof g_vpads[p]);
        else if ((!strcmp(sub, "press") || !strcmp(sub, "release")) && argc >= 4) {
            arc_key k = arc_key_from_name(argv[3]); if (k < ARC_PAD_A || k > ARC_PAD_GUIDE) k = arc_key_from_name(arc_fmt("PAD_%s", argv[3]));
            if (k < ARC_PAD_A || k > ARC_PAD_GUIDE) { printf("ARC error pad: unknown button '%s' (A B X Y LB RB LT RT BACK START LSTICK RSTICK UP DOWN LEFT RIGHT GUIDE)\n", argv[3]); fflush(stdout); return false; }
            g_vpads[p].on = true; if (sub[0] == 'p') g_vpads[p].btn |= 1u << (k - ARC_PAD_A); else g_vpads[p].btn &= ~(1u << (k - ARC_PAD_A));
        } else if (!strcmp(sub, "axis") && argc >= 5) {
            int a = -1; for (int i = 0; i < ARC_AXIS_COUNT; i++) if (!strcmp(argv[3], k_axis_names[i])) a = i;
            if (a < 0) { printf("ARC error pad: unknown axis '%s' (LX LY RX RY LT RT)\n", argv[3]); fflush(stdout); return false; }
            g_vpads[p].on = true; g_vpads[p].ax[a] = arc_clampf((float)atof(argv[4]), -1, 1);
        } else { printf("ARC error pad N connect|disconnect|press B|release B|axis A V\n"); fflush(stdout); return false; }
        return false;
    }
    if (!strcmp(c, "type")) {                         /* type some text  (arrives as arc_text_input next tick) */
        const char *t = strchr(line, ' '); if (t) { while (*t == ' ') t++; for (const char *q = t; *q;) text_append(g_vtext, sizeof g_vtext, arc_utf8_next(&q)); }
        return false;
    }
    if (!strcmp(c, "touch") && argc >= 2) {          /* touch on|off: report the pointer as a finger */
        const char *a = argv[1];
        if (!strcmp(a, "down") || !strcmp(a, "move") || !strcmp(a, "up")) {   /* 0.10.11: touch down|move ID X Y, touch up ID */
            if (argc < 3 || (strcmp(a, "up") && argc < 5)) { printf("ARC error touch: use 'touch down ID X Y', 'touch move ID X Y' or 'touch up ID'\n"); fflush(stdout); return false; }
            int id = arc_clampi(atoi(argv[2]), 0, 254); float x = argc >= 5 ? (float)atof(argv[3]) : 0, y = argc >= 5 ? (float)atof(argv[4]) : 0;
            int at = -1; for (int i = 0; i < g_virtual.tcount; i++) if (g_virtual.touch[i].id == id) at = i;
            if (!strcmp(a, "up")) {
                if (at < 0) { printf("ARC error touch up %d: no such finger down\n", id); fflush(stdout); return false; }
                touch_remove(&g_virtual, at, false);
                if (id == g_vfirst) { snap_set(&g_virtual, ARC_MOUSE_LEFT, false); g_vfirst = -1; }
                return false;
            }
            if (at < 0 && !strcmp(a, "move")) { printf("ARC error touch move %d: no such finger down\n", id); fflush(stdout); return false; }
            if (at < 0) {
                if (g_virtual.tcount >= ARC_TOUCH_MAX) { printf("ARC error touch down: %d fingers already\n", ARC_TOUCH_MAX); fflush(stdout); return false; }
                at = g_virtual.tcount++; g_virtual.touch[at].id = (uint8_t)id; g_virtual.touch[at].pad = 0;
                g_virtual.touch[at].sx = touch_px(x); g_virtual.touch[at].sy = touch_px(y);
                g_virtual.flags |= ARC__IN_TOUCH;
                if (g_vfirst < 0) { g_vfirst = id; snap_set(&g_virtual, ARC_MOUSE_LEFT, true); }   /* the first finger is the mouse */
            }
            g_virtual.touch[at].x = touch_px(x); g_virtual.touch[at].y = touch_px(y);
            if (id == g_vfirst) { g_virtual.mx = x; g_virtual.my = y; g_mouse_virtual = true; }
            return false;
        }
        if (!strcmp(a, "on")) g_virtual.flags |= ARC__IN_TOUCH; else g_virtual.flags &= (uint8_t)~ARC__IN_TOUCH;
        return false;
    }
    if (!strcmp(c, "ui")) { arc__ui_print(); return false; }
    if (!strcmp(c, "frame")) {   /* 0.9.0: what the frame shows, as text (see AGENTS.md) */
        /* immediate mode: the last tick recorded its frame as it drew it (0.10.9); else record while drawing it now */
        if (!G.app.immediate) arc__frame_record(true);
        bool ok = render_now(); arc_json j; memset(&j, 0, sizeof j);
        if (ok) arc__frame_json(&j);
        if (G.app.immediate) arc__frame_keep(); else arc__frame_record(false);
        if (!ok) printf("ARC error frame nothing to draw\n");
        else if (argc > 1) { if (arc_file_write(argv[1], j.buf, j.len)) printf("ARC ok frame %s\n", argv[1]); else printf("ARC error frame cannot write %s\n", argv[1]); }
        else printf("ARC frame %s\n", j.buf ? j.buf : "{}");
        fflush(stdout); free(j.buf); return false;
    }
    if (!strcmp(c, "reload")) {                       /* reload every watched asset now (needs --hot) */
        if (!g_hot_on) { printf("ARC error reload needs --hot (or arc_app.hot_reload)\n"); fflush(stdout); return false; }
        int n = hot_check(true); printf("ARC ok reload %d files\n", n); fflush(stdout); return false;
    }
    if (!strcmp(c, "gif") || !strcmp(c, "shots")) {   /* gif FILE.gif N [EVERY] [MAXW]  |  shots PREFIX N [EVERY] */
        if (argc < 3) { printf("ARC error %s needs a file and a frame count\n", c); fflush(stdout); return false; }
        capture *cp = &g_caps[strcmp(c, "gif") ? 1 : 0];
        if (cp->mode == 1 && cp->gif.f) arc__gif_end(&cp->gif);
        memset(cp, 0, sizeof *cp);
        cp->mode = !strcmp(c, "gif") ? 1 : 2; snprintf(cp->path, sizeof cp->path, "%s", argv[1]);
        cp->left = arc_maxi(1, atoi(argv[2])); cp->every = argc > 3 ? arc_maxi(1, atoi(argv[3])) : (cp->mode == 1 ? 2 : 1);
        cp->maxw = argc > 4 ? atoi(argv[4]) : 480;
        printf("ARC ok %s recording %d frames every %d ticks\n", c, cp->left, cp->every); fflush(stdout);
        return false;
    }
    if (!strcmp(c, "compare")) {                       /* compare REF.png [TOLERANCE%] */
        if (argc < 2) { printf("ARC error compare needs a reference PNG\n"); fflush(stdout); return false; }
        compare_shot(argv[1], argc > 2 ? (float)atof(argv[2]) : 0.1f); return false;
    }
    if (!strcmp(c, "seek") || !strcmp(c, "rewind")) {  /* seek TICK | rewind N */
        if (argc < 2) { printf("ARC error %s needs a number\n", c); fflush(stdout); return false; }
        long long n = atoll(argv[1]);
        uint64_t target = !strcmp(c, "seek") ? (uint64_t)(n < 0 ? 0 : n) : (n >= (long long)G.ticks ? 0 : G.ticks - (uint64_t)n);
        char err[160] = "";
        bool fwd = target > G.ticks;
        if (!seek_to(target, err, sizeof err)) { printf("ARC error %s %s\n", c, err); fflush(stdout); return false; }
        if (!fwd) { printf("ARC ok %s tick=%llu\n", c, (unsigned long long)G.ticks); fflush(stdout); }
        return fwd;
    }
    if (!strcmp(c, "state_save") || !strcmp(c, "state_load")) {   /* NAME (in memory) or a path with '.' or '/' (a file) */
        const char *name = argc > 1 ? argv[1] : "quick";
        bool file = strchr(name, '.') || strchr(name, '/') || strchr(name, '\\');
        bool save = !strcmp(c, "state_save");
        int slot = -1;
        for (int i = 0; i < 16 && !file; i++) if (!strcmp(g_named[i].name, name) || (save && slot < 0 && !g_named[i].name[0])) { slot = i; if (!strcmp(g_named[i].name, name)) break; }
        bool ok = false;
        if (save) {
            state_blob b;
            if (state_capture(&b)) {
                if (file) { ok = arc_file_write(name, b.data, b.size); state_free(&b); }
                else if (slot >= 0) { state_free(&g_named[slot].b); g_named[slot].b = b; snprintf(g_named[slot].name, 64, "%s", name); ok = true; }
                else state_free(&b);
            }
        } else {
            state_blob b; memset(&b, 0, sizeof b);
            if (file) { size_t n = 0; uint8_t *d = arc_file_read(name, arc_frame(), &n); if (d) { b.data = d; b.size = n; } }
            else if (slot >= 0 && g_named[slot].name[0]) b = g_named[slot].b;
            ok = b.data && state_restore(&b);
            if (ok) auto_states_drop_after(G.ticks);
        }
        if (!G.app.save_state) printf("ARC error %s the game has no save_state/load_state callbacks\n", c);
        else printf(ok ? "ARC ok %s %s tick=%llu\n" : "ARC error %s %s\n", c, name, (unsigned long long)G.ticks);
        fflush(stdout); return false;
    }
    if (!strcmp(c, "state_check")) {                   /* save, load, and check the game's dump didn't change */
        state_blob b; arc_json j1, j2; memset(&j1, 0, sizeof j1); memset(&j2, 0, sizeof j2);
        if (!G.app.save_state || !G.app.load_state || !state_capture(&b)) { printf("ARC error state_check the game has no working save_state/load_state\n"); fflush(stdout); return false; }
        if (G.app.dump) { arc_json_obj(&j1); G.app.dump(&j1); arc_json_end(&j1); }
        bool ok = state_restore(&b);
        if (G.app.dump) { arc_json_obj(&j2); G.app.dump(&j2); arc_json_end(&j2); }
        bool same = (!j1.buf && !j2.buf) || (j1.buf && j2.buf && !strcmp(j1.buf, j2.buf));
        if (ok && same) printf("ARC ok state_check %zu bytes, dump unchanged\n", b.size);
        else if (!ok) printf("ARC error state_check load_state failed\n");
        else { size_t k = 0; while (j1.buf[k] && j1.buf[k] == j2.buf[k]) k++; printf("ARC error state_check the dump changed after load at char %zu: ...%.60s\n", k, j2.buf + (k > 20 ? k - 20 : 0)); }
        fflush(stdout); free(j1.buf); free(j2.buf); state_free(&b); return false;
    }
    if (!strcmp(c, "hud")) {
        bool hide = argc > 1 ? (!strcmp(argv[1], "off") || !strcmp(argv[1], "0")) : !arc_hud_hidden();
        arc_set_hud_hidden(hide); printf("ARC ok hud %s\n", hide ? "off" : "on"); fflush(stdout); return false;
    }
    if (!strcmp(c, "cells") || !strcmp(c, "lod")) { arc__scene_command(argc, argv); return false; }
    if (!strcmp(c, "editor")) { arc__editor_command(argc, argv); return false; }                 /* 0.7.2 */
    if (!strcmp(c, "net")) { arc__net_agent(argc, argv, line); return arc__net_waiting(); }       /* 0.8 */
    if (!strcmp(c, "timeline") || !strcmp(c, "scrub")) { tl_command(argc, argv); return false; }
    if (!strcmp(c, "camera")) { void arc__camera_command(int argc, const char **argv); arc__camera_command(argc, argv); return false; }
    if (!strcmp(c, "brain")) { arc__brain_command(argc, argv); return false; }                  /* 0.10.0 */
    if (!strcmp(c, "cam")) {                                                                     /* 0.10.0 camera rigs; a game's own `cam` wins */
        if (G.app.command && G.app.command(c, argc - 1, argv + 1)) return false;
        void arc__cam_command(int argc, const char **argv); arc__cam_command(argc, argv); return false;
    }
    if (!strcmp(c, "fps")) { g_fps_on = argc > 1 ? !strcmp(argv[1], "on") || !strcmp(argv[1], "1") : !g_fps_on; printf("ARC ok fps %s\n", g_fps_on ? "on" : "off"); fflush(stdout); return false; }
    if (!strcmp(c, "inspector")) { g_inspector = argc > 1 ? !strcmp(argv[1], "on") || !strcmp(argv[1], "1") : !g_inspector; printf("ARC ok inspector %s\n", g_inspector ? "on" : "off"); fflush(stdout); return false; }
    if (!strcmp(c, "ui_click") && argc >= 2) {      /* click a widget by its label (as `ui` lists them) */
        char label[256] = ""; for (int i = 1; i < argc; i++) { if (i > 1) strncat(label, " ", sizeof label - strlen(label) - 1); strncat(label, argv[i], sizeof label - strlen(label) - 1); }
        float x, y;
        if (!arc__ui_find(label, &x, &y)) { printf("ARC error ui_click no widget \"%s\"\n", label); fflush(stdout); return false; }
        g_virtual.mx = x; g_virtual.my = y; g_mouse_virtual = true;
        snap_set(&g_virtual, ARC_MOUSE_LEFT, true); g_tap_ticks[ARC_MOUSE_LEFT] = 1;
        printf("ARC ok ui_click %s %.0f %.0f\n", label, x, y); fflush(stdout); return false;
    }
    if (!strcmp(c, "wheel") && argc >= 2) { g_raw_wheel += (float)atof(argv[1]); return false; }   /* 0.10.17: wheel steps next tick (+ = up) */
    if (!strcmp(c, "mouse") && argc >= 3) { g_virtual.mx = (float)atof(argv[1]); g_virtual.my = (float)atof(argv[2]); g_mouse_virtual = true; return false; }
    if (!strcmp(c, "ui_fit")) { if (!arc__ui_fit_start(argc, argv)) return false; g_wait_until = G.ticks + (uint64_t)arc__ui_fit_ticks(); return true; }   /* 0.10.1 */
    if (!strcmp(c, "sheet")) { void arc__sheet_command(int, const char **, bool (*)(void)); arc__sheet_command(argc, argv, render_now); return false; }   /* 0.10.1 */
    if (!strcmp(c, "shot") && argc > 2 && !strcmp(argv[2], "crop")) {   /* 0.10.1: trimmed to what differs from the corner colour */
        bool arc__crop_write(const char *, const uint8_t *, int, int, int);
        int w = 0, h = 0; uint8_t *px = render_now() ? arc__gfx_read_frame(&w, &h) : NULL;
        if (!px) { printf("ARC error shot %s\n", argv[1]); fflush(stdout); return false; }
        arc__crop_write(argv[1], px, w, h, argc > 3 ? atoi(argv[3]) : 8); free(px); return false;
    }
    if (!strcmp(c, "shot")) {   /* drawn and saved now, so it shows the state at this point of the script */
        const char *path = argc > 1 ? argv[1] : arc_fmt("shot_%06llu.png", (unsigned long long)G.ticks);
        bool ok = render_now() && arc_screenshot(path);
        printf(ok ? "ARC ok shot %s\n" : "ARC error shot %s\n", path); fflush(stdout);
        return false;
    }
    if (!strcmp(c, "dump")) { do_dump(argc > 1 ? argv[1] : NULL); return false; }
    if (!strcmp(c, "reset") || !strcmp(c, "arc_reset")) {   /* 0.9.8 (MechBattleArc): back to tick 0 for the next scenario */
        if (c[0] == 'r' && G.app.command && G.app.command(c, argc - 1, argv + 1)) { printf("ARC ok %s\n", c); fflush(stdout); return false; }   /* the game's own reset wins; arc_reset is always ARC's */
        do_reset(); return false;
    }
    if (!strcmp(c, "vars")) {
        for (int i = 0; i < g_var_count; i++) printf("ARC var %s %g (default %g)\n", g_vars[i].name, *g_vars[i].ptr, g_vars[i].initial);
        printf("ARC ok vars %d\n", g_var_count); fflush(stdout); return false;
    }
    if (!strcmp(c, "set") && argc >= 3) {
        bool ok = arc_var_set(argv[1], (float)atof(argv[2]));
        if (!ok && G.app.command && G.app.command(c, argc - 1, argv + 1)) {   /* 0.10.12: the game's own `set` gets what ARC's can't use */
            arc__hint("command-clash-set", "command \"set\" is ARC's (tunables: arc_var); the game's `set %s` only runs because no tunable has that name - rename the game's command so it always runs", argv[1]);
            printf("ARC ok set %s (the game's command)\n", argv[1]); fflush(stdout); return false;
        }
        printf(ok ? "ARC ok set %s %s\n" : "ARC error no var %s\n", argv[1], argv[2]); fflush(stdout); return false;
    }
    if (!strcmp(c, "get") && argc >= 2) { printf("ARC var %s %g\n", argv[1], arc_var_get(argv[1])); fflush(stdout); return false; }
    if (!strcmp(c, "stats")) {
        arc_gfx_stats s = arc_stats();
        printf("ARC stats tick=%llu backend=%s draw_calls=%d instances=%d triangles=%d frame_ms=%.3f avg_frame_ms=%.3f cpu_ms=%.3f tick_ms=%.3f dropped=%d dropped_total=%d\n",
               (unsigned long long)G.ticks, s.backend, s.draw_calls, s.instances, s.triangles, s.frame_ms, s.avg_frame_ms, s.cpu_ms, s.tick_ms, s.dropped, arc__gfx_dropped_total());
        arc_frame_times ft = arc_get_frame_times();
        printf("ARC frames last=%d p50=%.3f p95=%.3f p99=%.3f max=%.3f max_1s=%.3f low1_fps=%.1f\n", ft.samples, ft.p50, ft.p95, ft.p99, ft.max, ft.max_1s, ft.low1_fps);
        for (int i = 0; i < g_prof_count; i++) printf("ARC prof %s %.3f ms (%d calls)\n", g_prof[i].name, g_prof[i].last * 1000.0, g_prof[i].last_calls);
        printf("ARC scene culled=%d lod=%d,%d,%d,%d cells_visible=%d static_draws=%d\n", s.culled, s.lod_instances[0], s.lod_instances[1], s.lod_instances[2], s.lod_instances[3], s.cells_visible, s.static_draws);
        arc_frame_budget fb = arc_get_frame_budget();
        printf("ARC budget target=%.2f frame=%.3f over=%d tick=%.3f draw=%.3f sort=%.3f submit=%.3f present=%.3f audio=%.3f other=%.3f\n", fb.budget_ms, fb.frame_ms, fb.over ? 1 : 0,
               fb.tick_ms, fb.draw_ms, fb.sort_ms, fb.submit_ms, fb.present_ms, fb.audio_ms, fb.other_ms);
        for (int i = 0; i < fb.zone_count; i++) printf("ARC budget_zone %s %.3f ms (%.0f%%)\n", fb.zone_name[i], fb.zone_ms[i], 100.0f * fb.zone_ms[i] / fb.budget_ms);
        fflush(stdout); return false;
    }
    if (!strcmp(c, "post") && argc >= 3 && !strcmp(argv[1], "nan")) {   /* 0.10.12: post nan on|off */
        arc__post_nan = !strcmp(argv[2], "on");
        printf("ARC ok post nan %s (non-finite scene pixels %s; frame.post.nan_pixels counts them)\n", arc__post_nan ? "on" : "off", arc__post_nan ? "show magenta" : "show black"); fflush(stdout);
        return false;
    }
    if (!strcmp(c, "live")) {                        /* 0.10.10: window builds driven through --stdin (tools/arc_mcp.py) */
        const char *a = argc >= 2 ? argv[1] : "info";
        if (!strcmp(a, "run")) g_live_run = true;
        else if (!strcmp(a, "hold")) g_live_run = false;
        else if (!strcmp(a, "speed") && argc >= 3) g_live_speed = arc_clampf((float)atof(argv[2]), 0.25f, 64.0f);
        printf("ARC live window=%d run=%d speed=%g tick=%llu\n", g_live ? 1 : 0, g_live_run ? 1 : 0, (double)g_live_speed, (unsigned long long)G.ticks); fflush(stdout);
        return false;
    }
    if (!strcmp(c, "echo") || !strcmp(c, "log")) { printf("ARC echo %s\n", line + strlen(c) + (line[strlen(c)] ? 1 : 0)); fflush(stdout); return false; }
    if (!strcmp(c, "quit") || !strcmp(c, "exit")) { G.quit = true; return true; }
    if (!strcmp(c, "help")) { print_help(); return false; }
    if (G.app.command && G.app.command(c, argc - 1, argv + 1)) { printf("ARC ok %s\n", c); fflush(stdout); return false; }
    printf("ARC error unknown command '%s' (try help)\n", c); fflush(stdout);
    return false;
}

/* run queued commands until one asks to wait; in --stdin mode, read more lines when idle */
static void agent_before_tick(void) {
    if (G.ticks < g_wait_until) return;
    if (arc__net_waiting()) return;                 /* 0.8: net wait / net sync */
    for (;;) {
        if (g_cmds.pos < g_cmds.count) {
            const char *line = g_cmds.lines[g_cmds.pos++];
            if (run_command(line)) return;
            if (G.quit) return;
            continue;
        }
        if (!g_stdin_mode) return;
        if (g_live) {                                /* 0.10.10: never block; say ready once per wait */
            static char lline[16384];
            if (!live_stdin_line(lline, sizeof lline)) {
                if (g_live_eof) { G.quit = true; return; }
                if (!g_live_ready) { printf("ARC ready tick=%llu\n", (unsigned long long)G.ticks); fflush(stdout); g_live_ready = true; }
                return;
            }
            g_live_ready = false;
            cmd_push_multi(lline);
            continue;
        }
        printf("ARC ready tick=%llu\n", (unsigned long long)G.ticks); fflush(stdout);
        static char line[16384];   /* a longer line arrives in pieces */
        if (!fgets(line, sizeof line, stdin)) { G.quit = true; return; }
        cmd_push_multi(line);
    }
}

/* ======================================================================== */
/* Main loop                                                                */
/* ======================================================================== */

static void build_snapshot(void) {
    g_prev = g_cur;
    arc__input_snap s; bool from_hist = false;
    g_in_past = false;
    if (g_replaying && G.ticks < g_hist_len) { s = g_hist[G.ticks]; from_hist = true; }   /* after a seek back: the inputs already read */
    else if (g_replaying) replay_next(G.ticks, &s);
    else if (G.ticks < g_hist_len && (g_rec_file || TL.on || TL.used || g_resim)) {
        /* 0.7.2: a live game gone back (recording, or with the timeline): replay what was played up to the newest tick */
        s = g_hist[G.ticks]; from_hist = true; g_in_past = true;
        memset(g_latch, 0, sizeof g_latch); g_text[0] = g_vtext[0] = 0; g_raw_wheel = 0;
    } else {
        s = g_raw;
        /* a key pressed and released between ticks is still down for this one tick */
        for (int i = 0; i < (int)sizeof s.keys; i++) { s.keys[i] |= g_virtual.keys[i] | g_latch[i]; g_latch[i] = 0; }
        if (g_mouse_virtual) { s.mx = g_virtual.mx; s.my = g_virtual.my; }
        for (int a = 0; a < ARC_AXIS_COUNT; a++) if (g_virtual.axes[a] != 0) s.axes[a] = g_virtual.axes[a];
        s.flags |= g_virtual.flags;
        touch_raw_seen();   /* 0.10.11 */
        if (g_virtual.tcount) { s.tcount = g_virtual.tcount; memcpy(s.touch, g_virtual.touch, sizeof s.touch); }
        snap_pads(&s);   /* 0.10.5 */
        s.wheel = g_raw_wheel; g_raw_wheel = 0;
        memset(s.text, 0, sizeof s.text);
        size_t tl = strlen(g_text), vl = strlen(g_vtext);              /* both fit: each buffer is the same size */
        if (tl + vl >= sizeof s.text) vl = sizeof s.text - 1 - tl;
        memcpy(s.text, g_text, tl); memcpy(s.text + tl, g_vtext, vl); s.text[tl + vl] = 0; g_text[0] = g_vtext[0] = 0;
        if (TL.on && tl_over_bar(s.mx, s.my)) {     /* 0.7.2: the timeline bar takes clicks over it */
            snap_set(&s, ARC_MOUSE_LEFT, false); snap_set(&s, ARC_MOUSE_RIGHT, false); snap_set(&s, ARC_MOUSE_MIDDLE, false); s.wheel = 0;
        }
    }
    rec_write(G.ticks, &s);
    if (!from_hist) hist_set(G.ticks, &s);   /* (inputs replayed from the history stay as they are: the newest tick is kept) */
    g_cur = s;
    G.resized = G.last_w && (G.width != G.last_w || G.height != G.last_h);
    G.last_w = G.width; G.last_h = G.height;
}
static void after_tick(void) {
    for (int k = 0; k < ARC_KEY_COUNT; k++)
        if (g_tap_ticks[k] && --g_tap_ticks[k] == 0) snap_set(&g_virtual, k, false);
}

static void usage(void) {
    puts("ARC options:\n"
         "  --headless            no window; software renderer; runs as fast as possible\n"
         "  --hot                 reload textures, meshes, animations and sounds when their files change\n"
         "  --dev                 developer keys: ` inspector, F3 FPS overlay, F2 tile editor, F9 timeline\n"
         "  --fps                 show the FPS overlay (frame rate, timings, draw calls, frame-time graph)\n"
         "  --timeline            show the replay timeline (F9 with --dev or in a replay)\n"
         "  --frames N            quit after N ticks\n"
         "  --script FILE         run agent commands from a file (see `help`)\n"
         "  --cmd \"a; b; c\"       run agent commands given inline\n"
         "  --stdin               read agent commands from stdin (REPL; prints 'ARC ready' when waiting)\n"
         "  --seed N              seed for arc_rand_*\n"
         "  --save-dir DIR        where arc_save writes (headless default: ./arc_saves)\n"
         "  --set name=value      preset a tunable var (repeatable)\n"
         "  --record FILE         record input for replay;  --replay FILE  play it back\n"
         "  --size WxH            window/render size;  --tick-hz N  simulation rate\n"
         "  --safe-area L,T,R,B   pretend the screen has notches (pixels): arc_safe_area, anchored UI, ui_fit\n"
         "  --render-all          headless: render every frame (for benchmarking the renderer)\n"
         "  --draw-last N         headless: draw only the last N ticks before the end (--frames) or a --shot-at shot\n"
         "  --shot-at T1,T2,...   screenshots after those ticks (T=name.png to name one; --shot-name shot_%d.png)\n"
         "  --no-assets           loaded images keep their size but not their pixels, sounds their length but not\n"
         "                        their samples: lighter headless test runs (pictures are flat colours)\n"
         "  --dump-at-exit FILE   write a JSON state dump when the program exits\n"
         "  --no-compressed-textures  decode .arct / compressed textures on the CPU (as GPUs without them do)\n"
         "  --load-threads N      background loading threads (desktop; default CPUs - 1, 0 = main thread only)\n"
         "  --load-items N        queued loads: N files finish per frame (headless too: see the loading screen)\n"
         "  --help                this text");
}

static bool app_owns(const char *flag) {   /* the game said it handles --flag itself */
    const char *p = G.app.own_flags; size_t n = strlen(flag);
    while (p && *p) {
        while (*p == ' ') p++;
        size_t m = strcspn(p, " ");
        if (m == n && !strncmp(p, flag, n)) return true;
        p += m;
    }
    return false;
}
/* ---- memory that survives a code reload: keep the game's state here in hot-reload builds ---- */
typedef struct { char name[64]; void *mem; size_t size; } persist_rec;
static persist_rec g_persist[64]; static int g_persist_count;
void *arc_persist(const char *name, size_t size) {
    for (int i = 0; i < g_persist_count; i++)
        if (!strcmp(g_persist[i].name, name)) {
            if (g_persist[i].size != size) arc_log("arc_persist(\"%s\"): size changed from %zu to %zu; the old contents are kept up to the smaller size", name, g_persist[i].size, size);
            if (size > g_persist[i].size) { void *m = calloc(1, size); memcpy(m, g_persist[i].mem, g_persist[i].size); g_persist[i].mem = m; g_persist[i].size = size; }
            return g_persist[i].mem;
        }
    if (g_persist_count >= ARC__COUNTOF(g_persist)) { arc_log("arc_persist: too many blocks"); return NULL; }
    persist_rec *r = &g_persist[g_persist_count++];
    snprintf(r->name, sizeof r->name, "%s", name); r->mem = calloc(1, size ? size : 1); r->size = size;
    if (!r->mem) { arc_log("arc_persist: out of memory"); abort(); }
    return r->mem;
}

#if defined(ARC_HOT_HOST)
/* ---- hot-reload host: the game is a DLL / shared library, reloaded when it is rebuilt ---- */
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif
typedef arc_app (*arc_main_fn)(int, char **);
static struct { char path[1100]; long long mtime, size, seen_mtime, seen_size; int gen; double next; } g_lib;
static arc_main_fn hot_lib_load(void) {
    char live[1200]; snprintf(live, sizeof live, "%s.live%d", g_lib.path, g_lib.gen++);
    FILE *in = fopen(g_lib.path, "rb"), *out = in ? fopen(live, "wb") : NULL;   /* load a copy: the compiler can overwrite the original */
    if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return NULL; }
    char buf[65536]; size_t k; while ((k = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, k, out);
    fclose(in); fclose(out);
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(live); if (!h) return NULL;
    return (arc_main_fn)(void (*)(void))GetProcAddress(h, "arc_main");
#else
    void *h = dlopen(live, RTLD_NOW | RTLD_LOCAL); if (!h) { printf("ARC error reload %s\n", dlerror()); fflush(stdout); return NULL; }
    arc_main_fn f; void *sym = dlsym(h, "arc_main"); memcpy(&f, &sym, sizeof f); return f;
#endif
}
static arc_app hot_host_main(int argc, char **argv) {
    const char *v = arc_arg_value("game");
#if defined(_WIN32)
    const char *def = "game.dll";
#else
    const char *def = "libgame.so";
#endif
    if (v) snprintf(g_lib.path, sizeof g_lib.path, "%s", v); else snprintf(g_lib.path, sizeof g_lib.path, "%s%s", arc_app_dir(), def);   /* (no arenas yet: plain buffers) */
    file_stamp(g_lib.path, &g_lib.mtime, &g_lib.size); g_lib.seen_mtime = g_lib.mtime; g_lib.seen_size = g_lib.size;
    for (int i = 0; i < 64; i++) { char old[1200]; snprintf(old, sizeof old, "%s.live%d", g_lib.path, i); remove(old); }   /* copies left by an earlier run */
    arc_main_fn f = hot_lib_load();
    if (!f) { printf("ARC error cannot load the game library %s (give --game PATH)\n", g_lib.path); fflush(stdout); exit(1); }
    return f(argc, argv);
}
/* the library changed (and has stopped changing): load the new code, keep everything else */
static void hot_code_check(double now) {
    if (now < g_lib.next) return;
    g_lib.next = now + 0.25;
    long long mt, sz;
    if (!file_stamp(g_lib.path, &mt, &sz) || (mt == g_lib.mtime && sz == g_lib.size)) return;
    if (mt != g_lib.seen_mtime || sz != g_lib.seen_size) { g_lib.seen_mtime = mt; g_lib.seen_size = sz; return; }   /* still being written */
    g_lib.mtime = mt; g_lib.size = sz;
    arc_main_fn f = hot_lib_load();
    if (!f) { printf("ARC error reload code: cannot load %s\n", g_lib.path); fflush(stdout); return; }
    arc_app n = f(G.argc, G.argv);
    G.app.tick = n.tick; G.app.draw = n.draw; G.app.dump = n.dump; G.app.shutdown = n.shutdown; G.app.command = n.command;
    G.app.save_state = n.save_state; G.app.load_state = n.load_state; G.app.reloaded = n.reloaded; G.app.init = n.init;
    G.app.net_event = n.net_event; G.app.net_state = n.net_state; G.app.draw_loading = n.draw_loading;   /* 0.8 (the old code is gone) */
    G.app.net_input = n.net_input;                                                                          /* 0.8.1 */
    if (G.app.reloaded) G.app.reloaded();
    printf("ARC ok reload code %s\n", g_lib.path); fflush(stdout);
}
#define ARC__APP_MAIN hot_host_main
#else
#define ARC__APP_MAIN arc_main
#endif

int arc__startup(int argc, char **argv, const arc__platform *plat) {
    memset(&G, 0, sizeof G);
    G.argc = argc; G.argv = argv;
    if (arc_arg_flag("help") || arc_arg_flag("h")) { usage(); exit(0); }
    G.app = ARC__APP_MAIN(argc, argv);
    if (!G.app.width) G.app.width = 1280;
    if (!G.app.height) G.app.height = 720;
    if (!G.app.tick_hz) G.app.tick_hz = 60;
    if (!G.app.title) G.app.title = "ARC";
    if (!G.app.perm_mb) G.app.perm_mb = 64;
    if (!G.app.frame_mb) G.app.frame_mb = 16;
    G.tick_hz = G.app.tick_hz;
    const char *v;
    if ((v = arc_arg_value("tick-hz"))) G.tick_hz = arc_clampi(atoi(v), 1, 1000);
    if (!app_owns("seed") && (v = arc_arg_value("seed"))) g_seed = strtoull(v, NULL, 10);
    if (!app_owns("frames") && (v = arc_arg_value("frames"))) G.max_ticks = strtoull(v, NULL, 10);
    if (!app_owns("size") && (v = arc_arg_value("size"))) { int w, h; if (sscanf(v, "%dx%d", &w, &h) == 2) { G.app.width = w; G.app.height = h; } }
    if ((v = arc_arg_value("safe-area"))) { float s4[4] = { 0, 0, 0, 0 }; if (sscanf(v, "%f,%f,%f,%f", &s4[0], &s4[1], &s4[2], &s4[3]) == 4) memcpy(arc__safe_insets, s4, sizeof s4); }   /* 0.10.1 */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i]; while (*a == '-') a++;
        const char *val = NULL;
        if (!strncmp(a, "set=", 4)) val = a + 4; else if (!strcmp(a, "set") && i + 1 < argc) val = argv[++i];
        if (val && g_preset_count < 64) {
            const char *eq = strchr(val, '='); if (!eq) continue;
            size_t n = (size_t)(eq - val); if (n > 47) n = 47;
            memcpy(g_presets[g_preset_count].name, val, n); g_presets[g_preset_count].name[n] = 0;
            g_presets[g_preset_count++].value = (float)atof(eq + 1);
        }
    }
    if ((v = arc_arg_value("replay")) && !replay_open(v)) { fprintf(stderr, "ARC error cannot read replay %s\n", v); return 2; }
    const char *replay_path = v;
    G.dt = 1.0f / (float)G.tick_hz;
    arc_rng_seed(&g_rng, g_seed, 54u);
    G.headless = arc_arg_flag("headless") || plat == &arc__platform_headless;
    G.render_all = arc_arg_flag("render-all");
    if ((v = arc_arg_value("draw-last"))) G.draw_last = arc_maxi(1, atoi(v));
    if ((v = arc_arg_value("shot-at"))) { shot_at_parse(v); g_shot_at_quit = !G.max_ticks; }
    if (G.headless) plat = &arc__platform_headless;
    G.plat = plat;
    G.width = G.app.width; G.height = G.app.height;

    arc_arena_init(&g_perm, arc__xalloc(G.app.perm_mb << 20), G.app.perm_mb << 20);
    arc_arena_init(&g_frame, arc__xalloc(G.app.frame_mb << 20), G.app.frame_mb << 20);
    if (replay_path) {   /* what arc_load returned when it was recorded, so the replay starts from the same saves */
        size_t n = 0; uint8_t *sv = arc_file_read(arc_fmt("%s.saves", replay_path), &g_perm, &n);
        g_saves_replay = true; g_saves_play = sv; g_saves_play_n = sv ? n : 0;
    }

    if ((v = arc_arg_value("script"))) {
        size_t n; uint8_t *txt = arc_file_read(v, &g_frame, &n);
        if (!txt) { fprintf(stderr, "ARC error cannot read script %s\n", v); return 2; }
        cmd_push_multi((const char *)txt); arc_arena_reset(&g_frame);
    }
    if ((v = arc_arg_value("cmd"))) cmd_push_multi(v);
    g_stdin_mode = arc_arg_flag("stdin");
    g_live = g_stdin_mode && !G.headless && live_stdin_ok();   /* 0.10.10 */
    if ((v = arc_arg_value("record"))) {
        g_rec_file = fopen(v, "wb");
        if (g_rec_file) { uint32_t ver = ARC__REC_VERSION, hz = (uint32_t)G.tick_hz; fwrite("ARCR", 1, 4, g_rec_file); fwrite(&ver, 4, 1, g_rec_file); fwrite(&g_seed, 8, 1, g_rec_file); fwrite(&hz, 4, 1, g_rec_file); }
        g_saves_rec = fopen(arc_fmt("%s.saves", v), "wb");
        arc__net_rec_open(v);
    }
#if !defined(__EMSCRIPTEN__)
    /* sessions: the game asks for them (arc_app.sessions) and a person is playing, or --session forces them */
    if (arc_arg_flag("session") || (G.app.sessions && !G.headless && !g_replaying && !arc_arg_flag("no-session") && !arc_arg_value("cmd") && !arc_arg_value("script")))
        session_start();
#else
    (void)session_start; /* sessions write files: not on the web */
#endif

    arc__msaa = G.app.msaa;
    if (G.app.max_2d_vertices > 0) arc__max_v2 = G.app.max_2d_vertices;   /* 0.10.13 */
    if (G.headless && arc_arg_flag("audio-sim")) { g_audio_sim = true; arc__audio_test_mode = true; arc__audio_init(44100); }   /* 0.10.13 */
    if ((v = arc_arg_value("msaa"))) arc__msaa = atoi(v);
    arc__depth_mode = G.app.depth_mode >= ARC_DEPTH_STANDARD && G.app.depth_mode <= ARC_DEPTH_REVERSED_INFINITE ? G.app.depth_mode : ARC_DEPTH_STANDARD;
    if (arc_arg_flag("reversed-z")) arc__depth_mode = ARC_DEPTH_REVERSED;
    if ((v = arc_arg_value("depth-mode")))
        arc__depth_mode = !strcmp(v, "reversed") || !strcmp(v, "1") ? ARC_DEPTH_REVERSED : !strcmp(v, "infinite") || !strcmp(v, "2") ? ARC_DEPTH_REVERSED_INFINITE : ARC_DEPTH_STANDARD;
    if (!plat->init(&G.app, G.width, G.height)) { fprintf(stderr, "ARC error platform %s failed to start\n", plat->name); return 1; }
    if (G.app.clear_color) arc__gfx_set_clear(G.app.clear_color);
    G.start_time = G.last_time = plat->time();
    if (SES.on) SES.start = G.start_time;
    printf("ARC start %s platform=%s backend=%s size=%dx%d tick_hz=%d seed=%llu%s\n", G.app.title, plat->name,
           arc__gfx ? arc__gfx->name : "none", G.width, G.height, G.tick_hz, (unsigned long long)g_seed, g_replaying ? " replay" : "");
    fflush(stdout);
    /* init may draw into render targets (baking art at load): those passes run; screen drawing is dropped */
#if !defined(__EMSCRIPTEN__)
    g_hot_on = G.app.hot_reload || arc_arg_flag("hot");
#endif
    g_dev_flag = arc_arg_flag("dev");
    if (arc_arg_flag("fps")) g_fps_on = true;
    if (arc_arg_flag("no-hud")) arc_set_hud_hidden(true);
    if (arc_arg_flag("timeline")) arc_timeline_show(true);                       /* 0.7.2 */
    if (replay_path) tl_load_events(replay_path);
    arc__net_startup();                                                          /* 0.8 */
    if (replay_path) arc__net_replay_open(replay_path);
    {   /* an assets.arcpak next to the program (or in the working folder) is mounted automatically */
        const char *ap = arc_fmt("%sassets.arcpak", arc_app_dir()); FILE *pf = fopen(ap, "rb");
        if (!pf) { ap = "assets.arcpak"; pf = fopen(ap, "rb"); }
        if (pf) { fclose(pf); arc_pack_mount(ap); }
    }
    if (G.app.init) { arc__gfx_begin(); arc__phase = ARC__PHASE_INIT; G.app.init(); arc__phase = 0; arc__gfx_end_offscreen(); }
    G.started = true;
    if (G.app.save_state && G.app.load_state) state_capture(&g_reset0);         /* 0.9.8: for `reset` */
    arc__rb_startup();                                                           /* 0.8.1: --net-synctest */
    auto_state_maybe();
    return 0;
}

/* one tick of game logic (input is already in g_cur) */
/* 0.10.13 --audio-sim: a headless run mixes its sound into nothing, one tick's worth a tick, so music positions,
   voices and loop sections advance exactly as when playing (tests of music; otherwise headless runs have no mixer) */
static void audio_sim_tick(void) {
    static float scratch[2 * 4096];
    g_audio_sim_acc += 44100.0 / (double)(G.tick_hz > 0 ? G.tick_hz : 60);
    int n = (int)g_audio_sim_acc; g_audio_sim_acc -= n;
    while (n > 0) { int k = n < 4096 ? n : 4096; arc__audio_mix(scratch, k); n -= k; }
}
static void sim_tick_body(bool imm) {
    if (imm) arc__frame_alpha = 1.0f;
    arc__ui_tick_begin();
    if ((G.app.dev_keys || g_dev_flag) && arc_key_pressed(ARC_KEY_GRAVE)) g_inspector = !g_inspector;
    if ((G.app.dev_keys || g_dev_flag) && arc_key_pressed(ARC_KEY_F3)) g_fps_on = !g_fps_on;
    if ((SES.on || G.app.sessions) && arc_key_pressed(ARC_KEY_F8) && !g_resim && !g_in_past) arc_bookmark(NULL);   /* (the past's bookmarks are made already) */
    if (SES.toast > 0) SES.toast -= G.dt;
    TL.ticking = true;
    arc__rb_tick_begin();                            /* 0.8.1: a rollback match's tick (its START at match tick 0) */
    arc__net_tick_begin();                           /* 0.8: sequenced commands that have arrived, in order, before the game's tick */
    bool hold = arc__editor_tick();                  /* 0.7.2: an open editor pauses the game (and takes the input first) */
    arc__ui_fit_before_tick();    /* 0.10.1: `ui_fit` sizes */
    if (G.app.tick && !hold) { arc__phase = ARC__PHASE_TICK; G.app.tick(); arc__phase = 0; }
    if (g_audio_sim && !g_resim) audio_sim_tick();   /* 0.10.13 */
    arc__viewport_close();   /* 0.10.2: one left open */
    if (!hold) arc__rig_tick();   /* 0.10.0: the `cam rig` override moves with the ticks */
    arc__ui_fit_after_tick();     /* 0.10.1 */
    TL.ticking = false;
    if (g_inspector) arc_ui_inspector((float)(arc_width()) - 360.0f * arc_dpi_scale(), 10);
    arc__ui_tick_end();
    if (imm) { arc__popups_render(); arc__ui_render(); if (G.app.draw_top) G.app.draw_top(arc__frame_alpha); arc__editor_render(); tl_render(); fps_render(); toast_render(); }
    arc__fx_tick(G.dt);
    arc__popups_tick(G.dt);   /* 0.10.14 */
    double ta = arc__plat_time();
    arc__audio_tick();
    BG.audio += (arc__plat_time() - ta) * 1000.0;
}
bool arc__frame(void) {
    double now = G.plat->time(), real_dt = now - G.last_time; G.last_time = now;
    if (g_hot_on && g_hot_count && now >= g_hot_next) { g_hot_next = now + 0.5; hot_check(false); }
#if defined(ARC_HOT_HOST)
    hot_code_check(now);
#endif
    double cpu0 = now;
    BG.tick = BG.audio = BG.present = 0;
    arc_arena_reset(&g_frame);
    G.plat->poll();
    if (arc__quit_requested) G.quit = true;
    arc__net_frame();                               /* 0.8: sockets, once a frame */
    if (arc__load_frame()) {             /* 0.7.1: a group is loading: no ticks, the loading screen instead of draw() */
        G.acc = 0;
        if (!G.headless || G.render_all) {
            arc__gfx_begin();
            float pr = arc_load_progress((arc_load_group){ 0 });
            if (G.app.draw_loading) G.app.draw_loading(pr); else arc__draw_loading(pr);
            fps_render();
            arc__gfx_end(true);
            if (!G.headless) G.plat->present();
            prof_roll();
        }
        if (!G.headless) {               /* at most 60 loading frames a second: the CPU is better spent loading */
            double spent = G.plat->time() - now;
            if (spent < 1.0 / 60.0) G.plat->sleep(1.0 / 60.0 - spent);
        }
        double end = G.plat->time();
        arc__stats_frame_ms((float)(real_dt * 1000.0), (float)((end - cpu0) * 1000.0));
        if (!G.headless) fps_sample((float)(real_dt * 1000.0));
        return !G.quit;
    }

    tl_frame();                                     /* 0.7.2: the timeline's keys and mouse (desktop) */
    int ticks_to_run;
    bool rb_paced = arc__rb_mode() == 1;            /* 0.8.1: a live rollback match runs on the clock, headless too */
    bool tl_hold = TL.on && TL.paused && !G.headless && !g_stdin_mode && !rb_paced;
    if (g_live && !rb_paced) {                      /* 0.10.10: a window driven through a pipe: draw always, tick when asked */
        agent_before_tick();
        if (g_live_run || G.ticks < g_wait_until) {
            if (real_dt > 0.25) real_dt = 0.25;
            G.acc += real_dt * (double)g_live_speed;
            ticks_to_run = (int)(G.acc / G.dt);
            G.acc -= ticks_to_run * (double)G.dt;
            int cap = arc_maxi(8, (int)(8.0f * g_live_speed + 0.5f));
            if (ticks_to_run > cap) ticks_to_run = cap;
            if (!g_live_run && G.ticks < g_wait_until && (uint64_t)ticks_to_run > g_wait_until - G.ticks) ticks_to_run = (int)(g_wait_until - G.ticks);
        } else { G.acc = 0; ticks_to_run = 0; }
    }
    else if ((G.headless || g_stdin_mode) && !rb_paced) ticks_to_run = 1;
    else if (tl_hold) {                             /* paused: no ticks, but scripted commands still run */
        G.acc = G.dt; ticks_to_run = 0;
        agent_before_tick();
        if (G.ticks < g_wait_until) ticks_to_run = 1;
    } else {
        if (real_dt > 0.25) real_dt = 0.25;
        double sp = TL.on ? (double)tl_speed() : 1.0;
        int cap = TL.on ? arc_maxi(8, (int)(8.0 * sp + 0.5)) : 8;
        G.acc += real_dt * sp;
        ticks_to_run = (int)(G.acc / G.dt);
        G.acc -= ticks_to_run * (double)G.dt;
        if (ticks_to_run > cap) ticks_to_run = cap;
    }
    bool imm = G.app.immediate, imm_open = false;   /* immediate mode: the last tick's frame is still open */
    if (imm && !G.headless && ticks_to_run == 0) {     /* nothing new to show: wait for the next tick */
        double wait = (double)G.dt - G.acc - 0.001;
        if (wait > 0) G.plat->sleep(wait);
        double end = G.plat->time(); arc__stats_frame_ms((float)(real_dt * 1000.0), (float)((end - cpu0) * 1000.0));
        return !G.quit;
    }
    for (int t = 0; t < ticks_to_run && !G.quit; t++) {
        agent_before_tick();
        if (G.quit) break;
        if (G.max_ticks && G.ticks >= G.max_ticks) { G.quit = true; break; }
        if (g_replaying && g_replay_total != 0xFFFFFFFFu && G.ticks >= g_replay_total && !G.max_ticks) {
            if ((TL.on || TL.used) && !G.headless) { TL.paused = true; break; }   /* 0.7.2: a replay looked at with the timeline stays open at its end */
            G.quit = true; break;
        }
        bool rb = arc__rb_mode() != 0;
        if (rb) {                                   /* 0.8.1: corrections first; then run, skip (time sync) or wait */
            int go = arc__rb_before_tick();
            if (go == 1) continue;
            if (go == 2) { G.acc = 0; break; }
        }
        build_snapshot();
        if (rb) arc__rb_new_tick();
        if (imm) { if (imm_open) { if (draw_wanted_now()) arc__gfx_end_offscreen(); else arc__gfx_end(false); } arc__gfx_begin(); imm_open = true; arc__frame_record(true); }   /* 0.10.9: recorded for `frame` */
        double t0 = G.plat->time();
        sim_tick_body(imm);
        if (imm) arc__frame_keep();
        G.tick_ms = (float)((G.plat->time() - t0) * 1000.0);
        BG.tick += G.tick_ms;
        arc__gfx_count_tick_ms(G.tick_ms);
        G.ticks++;
        after_tick();
        auto_state_maybe();
        if (rb) arc__rb_after_tick();
        capture_after_tick(&imm_open);
        if (g_shot_at_next < g_shot_at_n && !g_resim && G.ticks >= g_shot_at[g_shot_at_next]) {   /* 0.9.8 --shot-at */
            bool drawn = (imm && imm_open) ? (arc__gfx_end(true), imm_open = false, true) : render_now();
            while (g_shot_at_next < g_shot_at_n && G.ticks >= g_shot_at[g_shot_at_next]) {
                const char *path = g_shot_at_name[g_shot_at_next++];
                bool ok = drawn && arc_screenshot(path);
                printf(ok ? "ARC ok shot %s (tick %llu)\n" : "ARC error shot %s\n", path, (unsigned long long)G.ticks); fflush(stdout);
            }
            if (g_shot_at_quit && g_shot_at_next >= g_shot_at_n) G.quit = true;
        }
    }
    if (rb_paced && G.headless && arc__rb_mode() == 1) {   /* nothing drawn or presented: wait for the next tick instead of spinning */
        double wait = (double)G.dt - G.acc;
        if (wait > 0.0005) G.plat->sleep(wait < 0.002 ? wait : 0.002);
    }
    /* a quit command may still have screenshots queued before it */
    bool want_render = !G.headless || (G.render_all && draw_wanted_now()) || g_shot_count > 0;
    if (imm && imm_open && !want_render) { if (draw_wanted_now()) arc__gfx_end_offscreen(); else arc__gfx_end(false); }
    if (want_render && (G.app.draw || imm)) {
        if (imm && imm_open) arc__gfx_end(true);
        else if (imm) arc__gfx_render_last();     /* no tick ran (a quit, say): show what the last one built */
        else {
            arc__gfx_begin();
            int pd = arc_prof_begin("draw");
            arc__frame_alpha = G.headless ? 1.0f : (float)(G.acc / G.dt);
            arc__phase = ARC__PHASE_DRAW; G.app.draw(arc__frame_alpha); arc__phase = 0;
            arc__viewport_close();   /* 0.10.2 */
            arc__scene_after_draw();
    arc__popups_render();   /* 0.10.14: after the game's 3D, before ARC's UI */
            arc__ui_render(); if (G.app.draw_top) G.app.draw_top(arc__frame_alpha); arc__editor_render(); tl_render(); fps_render(); toast_render();
            arc_prof_end(pd);
            arc__gfx_end(true);
        }
        for (int i = 0; i < g_shot_count; i++) {
            bool ok = arc_screenshot(g_shot_path[i]);
            printf(ok ? "ARC ok shot %s\n" : "ARC error shot %s\n", g_shot_path[i]); fflush(stdout);
        }
        g_shot_count = 0;
        if (!G.headless) { double tp = G.plat->time(); G.plat->present(); BG.present = (G.plat->time() - tp) * 1000.0; }
    }
    if (ticks_to_run > 0 || want_render) prof_roll();   /* prof_ms = the last frame that did work */
    double end = G.plat->time();
    arc__stats_frame_ms((float)(real_dt * 1000.0), (float)((end - cpu0) * 1000.0));
    if (want_render) { fps_sample((float)(real_dt * 1000.0)); budget_frame((float)(real_dt * 1000.0)); }
    return !G.quit;
}

void arc__shutdown(void) {
    const char *v = arc_arg_value("dump-at-exit");
    if (v) do_dump(v);
    session_end();
    rec_close();
    arc__net_shutdown();
    if (G.app.shutdown) G.app.shutdown();
    arc__load_shutdown();
#if defined(ARC_HOT_HOST) && !defined(_WIN32)
    for (int i = 0; i < g_lib.gen; i++) remove(arc_fmt("%s.live%d", g_lib.path, i));
#endif
    arc__fx_shutdown(); arc__bill_shutdown();
    arc__gfx_shutdown();
    if (G.plat && G.plat->shutdown) G.plat->shutdown();
    printf("ARC exit tick=%llu\n", (unsigned long long)G.ticks); fflush(stdout);
}

const arc__threads *arc__load_threads(void) { return G.plat ? G.plat->threads : NULL; }

/* 0.7.2 self-test only: run a pretend game inside the engine for a while (seek, rewind and the timeline need the main
   loop's machinery), then put everything back. arc__test_sandbox(&app) enters, arc__test_sandbox(NULL) leaves. */
static struct { bool on; arc_app app; uint64_t ticks, wait; arc_rng rng; arc__input_snap cur, prev, virt; } SBX;
static void sbx_clear(void) {
    g_hist_len = 0; for (int i = 0; i < g_auto_count; i++) free(g_auto[i].data);
    g_auto_count = 0; g_auto_every = 60;
    TL.on = TL.paused = TL.used = TL.drag = false; TL.nmarks = 0; TL.speed = 0;
}
void arc__test_sandbox(const arc_app *app) {
    if (app && !SBX.on) {
        SBX.on = true; SBX.app = G.app; SBX.ticks = G.ticks; SBX.wait = g_wait_until; SBX.rng = g_rng; SBX.cur = g_cur; SBX.prev = g_prev; SBX.virt = g_virtual;
        G.app.tick = app->tick; G.app.dump = app->dump; G.app.save_state = app->save_state; G.app.load_state = app->load_state;
        G.app.dev_keys = app->dev_keys; G.app.immediate = false; G.app.command = app->command;
        G.app.net_event = app->net_event; G.app.net_state = app->net_state;   /* 0.8 */
        G.app.net_input = app->net_input;                                       /* 0.8.1 */
        G.ticks = 0; g_wait_until = 0; sbx_clear(); auto_state_maybe();   /* as at startup: a snapshot of tick 0 */
    } else if (!app && SBX.on) {
        SBX.on = false; G.app = SBX.app; G.ticks = SBX.ticks; g_wait_until = SBX.wait; g_rng = SBX.rng; g_cur = SBX.cur; g_prev = SBX.prev; g_virtual = SBX.virt;
        memset(g_latch, 0, sizeof g_latch); sbx_clear();
    }
}
void arc__test_ticks(int n) {
    for (int i = 0; i < n; i++) {
        bool rb = arc__rb_mode() != 0;
        if (rb) { int go = arc__rb_before_tick(); if (go) continue; }
        build_snapshot(); if (rb) arc__rb_new_tick();
        sim_tick_body(false); G.ticks++; after_tick(); auto_state_maybe();
        if (rb) arc__rb_after_tick();
    }
}
void arc__test_command(const char *line) { run_command(line); }
void arc__test_timeline_frame(void) { bool h = G.headless; G.headless = false; tl_frame(); G.headless = h; }   /* the bar's desktop input */
int arc__run(int argc, char **argv, const arc__platform *plat) {
    int rc = arc__startup(argc, argv, plat);
    if (rc) return rc;
    while (arc__frame()) {}
    arc__shutdown();
    return 0;
}

/* ======================================================================== */
/* Headless platform (any OS): no window, software renderer                 */
/* ======================================================================== */

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static double hl_time(void) { static LARGE_INTEGER f; LARGE_INTEGER c; if (!f.QuadPart) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart; }
static void hl_sleep(double s) { Sleep((DWORD)(s * 1000.0)); }
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
static double hl_time(void) { return emscripten_get_now() / 1000.0; }
static void hl_sleep(double s) { (void)s; }
#else
#include <time.h>
static double hl_time(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }
static void hl_sleep(double s) { struct timespec ts = { (time_t)s, (long)((s - (double)(time_t)s) * 1e9) }; nanosleep(&ts, NULL); }
#endif
static bool hl_init(const arc_app *app, int w, int h) { (void)app; return arc__gfx_init(&arc__backend_soft, w, h); }
static void hl_poll(void) {}
static void hl_present(void) {}
static void hl_shutdown(void) {}
const arc__platform arc__platform_headless = { "headless", hl_init, hl_poll, hl_present, hl_shutdown, hl_time, hl_sleep, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
