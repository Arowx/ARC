/* ARC particles: effect descriptions, bursts and emitters, simulated in tick, drawn as instanced billboards.
 * Particles live outside the game state: their own random generator (seeded from --seed) keeps replays exact
 * without touching arc_rand_*. Drawing reuses the see-through mesh pass (sorted, no depth writes), unlit. */
#include "arc_internal.h"

#define ARC__MAX_FX_KINDS  256
#define ARC__MAX_QUAD_TEX  1024   /* camera-facing quads: one mesh per texture (particles and billboards) */
#define ARC__MAX_PARTICLES (1 << 16)
#define ARC__MAX_EMITTERS  1024
#define ARC__MAX_FX_EVENTS 4096   /* sub-emitter bursts per tick */
typedef struct { int kind, count; arc_v3 pos, dir; } fx_event_rec;

typedef struct { arc_v3 p, pp, v; float age, life, rot, spin, size_k; uint16_t kind, frame0; } fx_particle;   /* frame0: flipbook start */
typedef struct { bool used, active; uint16_t gen, kind; arc_v3 pos, prev, dir; float acc, rate_scale; bool moved; } fx_emitter;

static struct {
    bool ready;
    arc_fx kinds[ARC__MAX_FX_KINDS]; uint32_t kind_mesh[ARC__MAX_FX_KINDS]; int kind_slot[ARC__MAX_FX_KINDS], kind_count;
    int16_t kind_sub[ARC__MAX_FX_KINDS][2];   /* on_death / on_bounce resolved to kinds (0 = not yet, else kind + 1) */
    uint32_t sheet_mesh[ARC__MAX_FX_KINDS];   /* flipbook kinds: their own quad covering one frame */
    fx_particle *p; int count;
    fx_emitter em[ARC__MAX_EMITTERS];
    arc_rng rng;
    arc_texture dot; uint32_t tex_mesh_tex[ARC__MAX_QUAD_TEX], tex_mesh[ARC__MAX_QUAD_TEX]; uint64_t tex_mesh_used[ARC__MAX_QUAD_TEX]; int tex_mesh_count;
    int spawned_total;
    fx_event_rec ev[ARC__MAX_FX_EVENTS]; int nev;
} FX;
float arc__frame_alpha = 1.0f;   /* set by the core before draw(): blend between the last two ticks */

static void fx_init(void) {
    if (FX.ready) return;
    FX.p = (fx_particle *)arc__xalloc(sizeof(fx_particle) * ARC__MAX_PARTICLES);
    arc_rng_seed(&FX.rng, arc_seed(), 0xF00Du);
    FX.ready = true;
}
void arc__fx_reset(void) {   /* new seed / state load: drop all particles */
    if (!FX.ready) return;
    FX.count = 0; arc_rng_seed(&FX.rng, arc_seed(), 0xF00Du);
}
void arc__fx_shutdown(void) { free(FX.p); memset(&FX, 0, sizeof FX); }

/* effect descriptions are copied into a small table (same description -> same slot) */
static arc_fx fx_defaults(const arc_fx *in) {
    arc_fx f = *in;
    if (f.life <= 0) f.life = 1.0f;
    if (f.size <= 0) f.size = 0.2f;
    if (f.size_end <= 0) f.size_end = f.size;
    if (!f.color) f.color = 0xFFFFFF;
    if (!f.color_end) f.color_end = f.color;
    if (f.alpha <= 0) f.alpha = 1.0f;
    if (f.burst <= 0) f.burst = 16;
    return f;
}
static int fx_kind(const arc_fx *fx) {
    fx_init();
    arc_fx f = fx_defaults(fx);
    for (int i = 0; i < FX.kind_count; i++) if (!memcmp(&FX.kinds[i], &f, sizeof f)) return i;
    if (FX.kind_count >= ARC__MAX_FX_KINDS) { static bool warned; if (!warned) { warned = true; arc_log("particles: more than %d different effects; reusing the last", ARC__MAX_FX_KINDS); } return FX.kind_count - 1; }
    int k = FX.kind_count++;
    FX.kinds[k] = f; FX.kind_mesh[k] = 0; FX.kind_sub[k][0] = FX.kind_sub[k][1] = 0; FX.sheet_mesh[k] = 0;
    /* sub-emitters: resolved now, while the game's pointers are surely valid (an effect naming itself finds itself) */
    if (f.on_death) FX.kind_sub[k][0] = (int16_t)(fx_kind(f.on_death) + 1);
    if (f.on_bounce) FX.kind_sub[k][1] = (int16_t)(fx_kind(f.on_bounce) + 1);
    return k;
}

static arc_v3 any_perp(arc_v3 d) { return fabsf(d.y) < 0.9f ? arc_v3_norm(arc_v3_cross(d, arc_v3_make(0, 1, 0))) : arc_v3_norm(arc_v3_cross(d, arc_v3_make(1, 0, 0))); }
static float rnd(float lo, float hi) { return arc_rng_rangef(&FX.rng, lo, hi); }
static void spawn(int kind, arc_v3 pos, arc_v3 dir) {
    if (FX.count >= ARC__MAX_PARTICLES) return;
    const arc_fx *f = &FX.kinds[kind];
    fx_particle *q = &FX.p[FX.count++];
    arc_v3 d = arc_v3_len(dir) > 1e-6f ? arc_v3_norm(dir) : arc_v3_make(0, 1, 0);
    /* direction in a cone of half-angle `spread` around d (uniform over the cap) */
    float spread = arc_clampf(f->spread, 0, ARC_PI);
    float cz = rnd(cosf(spread), 1.0f), sz = sqrtf(arc_maxf(0, 1 - cz * cz)), ph = rnd(0, ARC_TAU);
    arc_v3 a = any_perp(d), b = arc_v3_cross(d, a);
    arc_v3 dirn = arc_v3_add(arc_v3_scale(d, cz), arc_v3_add(arc_v3_scale(a, sz * cosf(ph)), arc_v3_scale(b, sz * sinf(ph))));
    float speed = f->speed + rnd(-f->speed_var, f->speed_var);
    arc_v3 off = arc_v3_make(0, 0, 0);
    if (f->radius > 0) {   /* a point inside a sphere */
        for (int tries = 0; tries < 8; tries++) {
            off = arc_v3_make(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
            if (arc_v3_dot(off, off) <= 1) break;
        }
        off = arc_v3_scale(off, f->radius);
    }
    q->p = q->pp = arc_v3_add(pos, off);
    q->v = arc_v3_add(arc_v3_scale(dirn, speed), f->inherit);
    q->age = 0; q->life = arc_maxf(0.01f, f->life + rnd(-f->life_var, f->life_var));
    q->rot = f->spin_var > 0 || f->spin != 0 ? rnd(0, ARC_TAU) : 0;
    q->spin = f->spin + rnd(-f->spin_var, f->spin_var);
    q->size_k = 1.0f + rnd(-f->size_var, f->size_var);
    q->kind = (uint16_t)kind;
    int nf = arc_maxi(f->frames_x, 1) * arc_maxi(f->frames_y, 1);
    q->frame0 = (uint16_t)(f->frame_random && nf > 1 ? arc_rng_range(&FX.rng, 0, nf - 1) : 0);
    FX.spawned_total++;
}

void arc_fx_burst(const arc_fx *fx, arc_v3 pos, arc_v3 dir, int count) {
    if (!fx) return;
    int k = fx_kind(fx);
    if (count <= 0) count = FX.kinds[k].burst;
    for (int i = 0; i < count; i++) spawn(k, pos, dir);
}

static fx_emitter *em_get(arc_emitter e) {
    uint32_t i = (e.id & 0xFFFFu), g = e.id >> 16;
    if (!i || i > ARC__MAX_EMITTERS) return NULL;
    fx_emitter *m = &FX.em[i - 1];
    return m->used && m->gen == g ? m : NULL;
}
arc_emitter arc_emitter_create(const arc_fx *fx, arc_v3 pos, arc_v3 dir) {
    arc_emitter r = { 0 };
    if (!fx) return r;
    int k = fx_kind(fx);
    for (int i = 0; i < ARC__MAX_EMITTERS; i++) {
        fx_emitter *m = &FX.em[i];
        if (m->used) continue;
        uint16_t gen = (uint16_t)(m->gen + 1); if (!gen) gen = 1;
        memset(m, 0, sizeof *m);
        m->used = m->active = true; m->gen = gen; m->kind = (uint16_t)k;
        m->pos = m->prev = pos; m->dir = dir; m->rate_scale = 1;
        r.id = (uint32_t)(i + 1) | ((uint32_t)gen << 16);
        return r;
    }
    arc_log("particles: more than %d emitters", ARC__MAX_EMITTERS);
    return r;
}
void arc_emitter_move(arc_emitter e, arc_v3 pos, arc_v3 dir) { fx_emitter *m = em_get(e); if (m) { m->pos = pos; m->dir = dir; } }
void arc_emitter_set_rate(arc_emitter e, float scale) { fx_emitter *m = em_get(e); if (m) m->rate_scale = arc_maxf(0, scale); }
void arc_emitter_set_active(arc_emitter e, bool on) { fx_emitter *m = em_get(e); if (m) { m->active = on; if (!on) m->acc = 0; } }
void arc_emitter_destroy(arc_emitter e) { fx_emitter *m = em_get(e); if (m) m->used = false; }
bool arc_emitter_alive(arc_emitter e) { return em_get(e) != NULL; }
int  arc_particle_count(void) { return FX.ready ? FX.count : 0; }
void arc_particles_clear(void) { if (FX.ready) FX.count = 0; }

/* sub-emitter events (deaths, bounces) gathered during the move, fired after it */
static void fx_event(int kind, int which, arc_v3 pos, arc_v3 dir) {
    const arc_fx *f = &FX.kinds[kind];
    if (!FX.kind_sub[kind][which]) {   /* resolve the sub-effect to a kind once */
        const arc_fx *sub = which ? f->on_bounce : f->on_death;
        FX.kind_sub[kind][which] = (int16_t)(fx_kind(sub) + 1);
        f = &FX.kinds[kind];
    }
    if (FX.nev >= ARC__MAX_FX_EVENTS) return;
    fx_event_rec *ev = &FX.ev[FX.nev++];
    ev->kind = FX.kind_sub[kind][which] - 1; ev->count = which ? f->on_bounce_count : f->on_death_count; ev->pos = pos; ev->dir = dir;
}
/* the particle moved into the game's solid: undo the move along each axis that went in and bounce on it.
   false = it ends here (bounce 0) */
static bool fx_hit(fx_particle *q, const arc_fx *f) {
    if (f->bounce <= 0) return false;
    arc_v3 o = q->pp, n = q->p, nv = arc_v3_make(0, 0, 0);
    bool hx = f->solid(n.x, o.y, o.z, f->solid_user), hy = f->solid(o.x, n.y, o.z, f->solid_user), hz = f->solid(o.x, o.y, n.z, f->solid_user);
    if (!hx && !hy && !hz) hx = hy = hz = true;   /* only the diagonal goes in: a corner, bounce back on all */
    float speed = 0;
    if (hx) { q->p.x = o.x; speed = arc_maxf(speed, fabsf(q->v.x)); nv.x = q->v.x > 0 ? -1.0f : 1.0f; q->v.x = -q->v.x * f->bounce; } else q->v.x *= 0.8f;
    if (hy) { q->p.y = o.y; speed = arc_maxf(speed, fabsf(q->v.y)); nv.y = q->v.y > 0 ? -1.0f : 1.0f; q->v.y = -q->v.y * f->bounce; } else q->v.y *= 0.8f;
    if (hz) { q->p.z = o.z; speed = arc_maxf(speed, fabsf(q->v.z)); nv.z = q->v.z > 0 ? -1.0f : 1.0f; q->v.z = -q->v.z * f->bounce; } else q->v.z *= 0.8f;
    if (f->on_bounce && speed > 1.0f) fx_event(q->kind, 1, q->p, arc_v3_norm(nv));
    return true;
}

/* once per tick, after the game's tick() */
void arc__fx_tick(float dt) {
    if (!FX.ready) return;
    for (int i = 0; i < ARC__MAX_EMITTERS; i++) {
        fx_emitter *m = &FX.em[i];
        if (!m->used) continue;
        if (m->active) {
            const arc_fx *f = &FX.kinds[m->kind];
            m->acc += f->rate * m->rate_scale * dt;
            int n = (int)m->acc; m->acc -= (float)n;
            for (int s = 0; s < n; s++)   /* spread along the path moved this tick: smooth trails */
                spawn(m->kind, arc_v3_lerp(m->prev, m->pos, (float)(s + 1) / (float)n), m->dir);
        }
        m->prev = m->pos;
    }
    FX.nev = 0;
    for (int i = 0; i < FX.count;) {
        fx_particle *q = &FX.p[i];
        const arc_fx *f = &FX.kinds[q->kind];
        q->age += dt;
        if (q->age >= q->life) { if (f->on_death) fx_event(q->kind, 0, q->p, q->v); FX.p[i] = FX.p[--FX.count]; continue; }
        q->pp = q->p;
        q->v = arc_v3_madd(q->v, f->gravity, dt);
        if (f->drag > 0) q->v = arc_v3_scale(q->v, 1.0f / (1.0f + f->drag * dt));
        q->p = arc_v3_madd(q->p, q->v, dt);
        if (f->bounce > 0 && q->p.y < f->floor_y && q->v.y < 0) {
            if (f->on_bounce && q->v.y < -1.0f) fx_event(q->kind, 1, arc_v3_make(q->p.x, f->floor_y, q->p.z), arc_v3_make(0, 1, 0));
            q->p.y = f->floor_y; q->v.y = -q->v.y * f->bounce; q->v.x *= 0.8f; q->v.z *= 0.8f;
        }
        if (f->solid && f->solid(q->p.x, q->p.y, q->p.z, f->solid_user) && !fx_hit(q, f)) {
            if (f->on_death) fx_event(q->kind, 0, q->p, q->v);
            FX.p[i] = FX.p[--FX.count]; continue;
        }
        q->rot += q->spin * dt;
        i++;
    }
    for (int e = 0; e < FX.nev; e++) {   /* sub-emitters, after the move so none of their particles moves this tick */
        const fx_event_rec *ev = &FX.ev[e];
        int n = ev->count > 0 ? ev->count : FX.kinds[ev->kind].burst;
        for (int k = 0; k < n; k++) spawn(ev->kind, ev->pos, ev->dir);
    }
}

/* ---- drawing ---- */
extern uint64_t arc__gfx_frame;
static arc_mesh quad_mesh_uv(arc_texture t, float su, float sv) {   /* uv 0..su across, 0..sv down */
    arc_vertex v[4]; memset(v, 0, sizeof v);
    const float P[4][2] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
    for (int i = 0; i < 4; i++) {
        v[i].px = P[i][0]; v[i].py = P[i][1]; v[i].nz = 1; v[i].color = 0xFFFFFFFFu;
        v[i].u = (P[i][0] + 0.5f) * su; v[i].v = (0.5f - P[i][1]) * sv;
    }
    uint32_t idx[6] = { 0, 1, 2, 0, 2, 3 };
    arc_mesh m = arc_mesh_create(v, 4, idx, 6);
    arc_mesh_set_double_sided(m, true);
    arc_mesh_set_texture(m, t);
    return m;
}
arc_mesh arc__quad_mesh(arc_texture t) { return quad_mesh_uv(t, 1, 1); }
/* a camera-facing quad mesh for a texture (0 = the soft round dot), one per texture */
uint32_t arc__fx_quad(uint32_t tex) {
    if (!tex) {
        if (!FX.dot.id || !arc__textures[FX.dot.id].used) {   /* soft round dot: smooth falloff to the edge */
            enum { S = 64 }; static uint8_t px[S * S * 4];
            for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
                float dx = ((float)x + 0.5f) / S * 2 - 1, dy = ((float)y + 0.5f) / S * 2 - 1, r = sqrtf(dx * dx + dy * dy);
                float a = arc_clampf(1.0f - r, 0, 1); a = a * a * (3 - 2 * a);
                uint8_t *o = &px[(y * S + x) * 4]; o[0] = o[1] = o[2] = 255; o[3] = (uint8_t)(a * 255.0f + 0.5f);
            }
            FX.dot = arc_texture_create_ex(S, S, px, ARC_TEX_SMOOTH | ARC_TEX_MIPMAPS);
        }
        tex = FX.dot.id;
    }
    for (int i = 0; i < FX.tex_mesh_count; i++)
        if (FX.tex_mesh_tex[i] == tex && arc__meshes[FX.tex_mesh[i]].used && arc__meshes[FX.tex_mesh[i]].texture == tex) { FX.tex_mesh_used[i] = arc__gfx_frame; return FX.tex_mesh[i]; }
    /* a free slot, else the one used longest ago (never one drawn with this frame: its instances are queued) */
    int slot = FX.tex_mesh_count < ARC__MAX_QUAD_TEX ? FX.tex_mesh_count : -1;
    if (slot < 0)
        for (int i = 0; i < ARC__MAX_QUAD_TEX; i++)
            if (FX.tex_mesh_used[i] != arc__gfx_frame && (slot < 0 || FX.tex_mesh_used[i] < FX.tex_mesh_used[slot])) slot = i;
    if (slot < 0) { static bool warned; if (!warned) { warned = true; arc_log("billboards: more than %d different textures in one frame", ARC__MAX_QUAD_TEX); } return 0; }
    if (slot < FX.tex_mesh_count) arc_mesh_destroy((arc_mesh){ FX.tex_mesh[slot] }); else FX.tex_mesh_count++;
    arc_mesh m = arc__quad_mesh((arc_texture){ tex });
    FX.tex_mesh_tex[slot] = tex; FX.tex_mesh[slot] = m.id; FX.tex_mesh_used[slot] = arc__gfx_frame;
    return m.id;
}
static uint32_t kind_mesh(int k) {
    uint32_t want = FX.kinds[k].texture.id ? FX.kinds[k].texture.id : FX.dot.id, id = FX.kind_mesh[k];   /* the cached quad may have been recycled */
    if (id && arc__meshes[id].used && want && arc__meshes[id].texture == want) {
        int sl = FX.kind_slot[k];
        if (sl < FX.tex_mesh_count && FX.tex_mesh[sl] == id) FX.tex_mesh_used[sl] = arc__gfx_frame;
        return id;
    }
    id = arc__fx_quad(FX.kinds[k].texture.id);
    for (int i = 0; i < FX.tex_mesh_count; i++) if (FX.tex_mesh[i] == id) FX.kind_slot[k] = i;
    return FX.kind_mesh[k] = id;
}
/* flipbook kinds: a quad of their own covering one frame; the frame is picked by the instance's uv offset */
static uint32_t sheet_mesh(int k) {
    const arc_fx *f = &FX.kinds[k];
    if (!f->texture.id) arc__fx_quad(0);   /* the dot texture exists */
    uint32_t want = f->texture.id ? f->texture.id : FX.dot.id, id = FX.sheet_mesh[k];
    if (id && arc__meshes[id].used && arc__meshes[id].texture == want) return id;
    arc_mesh m = quad_mesh_uv((arc_texture){ want }, 1.0f / (float)arc_maxi(f->frames_x, 1), 1.0f / (float)arc_maxi(f->frames_y, 1));
    return FX.sheet_mesh[k] = m.id;
}
static float lerp_smooth_alpha(const arc_fx *f, float t) {
    float a = arc_lerpf(f->alpha, f->alpha_end, t);
    if (f->fade_in > 0 && t < f->fade_in) a *= t / f->fade_in;
    return a;
}
static uint32_t lerp_rgb(uint32_t a, uint32_t b, float t) {
    int r = (int)arc_lerpf((float)((a >> 16) & 255), (float)((b >> 16) & 255), t);
    int g = (int)arc_lerpf((float)((a >> 8) & 255), (float)((b >> 8) & 255), t);
    int c = (int)arc_lerpf((float)(a & 255), (float)(b & 255), t);
    return (uint32_t)(r << 16 | g << 8 | c);
}
/* arc_gfx.c: push one unlit see-through instance (_fx: with a uv offset and a soft-particle distance) */
void arc__draw_unlit(uint32_t mesh, arc_m4 model, uint32_t tint, float glow, bool additive, float alpha);
void arc__draw_unlit_fx(uint32_t mesh, arc_m4 model, uint32_t tint, float glow, bool additive, float alpha, float u, float v, float soft);
arc_v3 arc__camera_axis(int i);   /* 0 right, 1 up, 2 toward the viewer */

void arc_draw_particles(void) {
    if (!FX.ready || !FX.count) return;
    arc_v3 R0 = arc__camera_axis(0), U0 = arc__camera_axis(1), F = arc__camera_axis(2);
    float al = arc_clampf(arc__frame_alpha, 0, 1);
    for (int i = 0; i < FX.count; i++) {
        const fx_particle *q = &FX.p[i];
        const arc_fx *f = &FX.kinds[q->kind];
        float t = arc_clampf((q->age - (1.0f - al) * arc_dt()) / q->life, 0, 1);
        arc_v3 pos = arc_v3_lerp(q->pp, q->p, al);
        float size = arc_lerpf(f->size, f->size_end, t) * q->size_k;
        float alpha = lerp_smooth_alpha(f, t);
        if (size <= 0 || alpha <= 0.002f) continue;
        arc_v3 ax, ay;
        if (f->stretch > 0) {   /* sparks: long side along the velocity as seen on screen */
            arc_v3 v = arc_v3_sub(q->v, arc_v3_scale(F, arc_v3_dot(q->v, F)));
            float sp = arc_v3_len(v);
            if (sp > 1e-4f) {
                arc_v3 dx = arc_v3_scale(v, 1.0f / sp);
                ax = arc_v3_scale(dx, size + sp * f->stretch);
                ay = arc_v3_scale(arc_v3_cross(F, dx), size);
            } else { ax = arc_v3_scale(R0, size); ay = arc_v3_scale(U0, size); }
        } else {
            float c = cosf(q->rot), s = sinf(q->rot);
            ax = arc_v3_scale(arc_v3_add(arc_v3_scale(R0, c), arc_v3_scale(U0, s)), size);
            ay = arc_v3_scale(arc_v3_sub(arc_v3_scale(U0, c), arc_v3_scale(R0, s)), size);
        }
        int fx_ = arc_maxi(f->frames_x, 1), fy_ = arc_maxi(f->frames_y, 1);
        if (fx_ * fy_ > 1 || f->soft > 0) {
            float u = 0, v = 0; uint32_t mesh = kind_mesh(q->kind);
            if (fx_ * fy_ > 1) {   /* flipbook: frame = age * fps (looping), or t across the sheet once */
                int n = fx_ * fy_, fr = f->fps > 0 ? (int)floorf(arc_maxf(0, q->age - (1.0f - al) * arc_dt()) * f->fps) % n : arc_mini((int)(t * (float)n), n - 1);
                fr = (fr + q->frame0) % n;
                u = (float)(fr % fx_) / (float)fx_; v = (float)(fr / fx_) / (float)fy_; mesh = sheet_mesh(q->kind);
            }
            arc__draw_unlit_fx(mesh, arc_m4_basis(pos, ax, ay, F), lerp_rgb(f->color, f->color_end, t), f->glow, f->additive, alpha, u, v, f->soft);
            continue;
        }
        arc__draw_unlit(kind_mesh(q->kind), arc_m4_basis(pos, ax, ay, F), lerp_rgb(f->color, f->color_end, t), f->glow, f->additive, alpha);
    }
}


/* ---- trails (0.7): the game's ring of points; drawn through a pool of ribbon meshes, one per trail per frame ---- */
static float trail_life(const arc_trail *t) { return t->life > 0 ? t->life : 0.5f; }
static void trail_age(arc_trail *t) {
    if (t->count < 0 || t->count > ARC_TRAIL_MAX || t->first < 0 || t->first >= ARC_TRAIL_MAX) { t->first = t->count = 0; }
    float dt = arc_dt(), life = trail_life(t);
    for (int i = 0; i < t->count; i++) t->age[(t->first + i) % ARC_TRAIL_MAX] += dt;
    while (t->count > 0 && t->age[t->first] >= life) { t->first = (t->first + 1) % ARC_TRAIL_MAX; t->count--; }
}
void arc_trail_age(arc_trail *t) { if (t) trail_age(t); }
void arc_trail_clear(arc_trail *t) { if (t) t->first = t->count = 0; }
void arc_trail_push(arc_trail *t, arc_v3 pos) {
    if (!t) return;
    trail_age(t);
    if (t->count > 0 && t->min_dist > 0) {
        int last = (t->first + t->count - 1) % ARC_TRAIL_MAX;
        if (arc_v3_dist(t->p[last], pos) < t->min_dist) { t->p[last] = pos; t->age[last] = 0; return; }
    }
    if (t->count == ARC_TRAIL_MAX) { t->first = (t->first + 1) % ARC_TRAIL_MAX; t->count--; }   /* full: drop the oldest */
    int k = (t->first + t->count) % ARC_TRAIL_MAX;
    t->p[k] = pos; t->age[k] = 0; t->count++;
}
#define TRAIL_POOL 256
static struct { uint32_t mesh[TRAIL_POOL]; uint64_t frame; int used; arc_texture edge; } TR;
static uint32_t trail_rgba(uint32_t rgb, float a) {   /* vertex colour bytes R,G,B,A */
    uint32_t al = (uint32_t)(arc_clampf(a, 0, 1) * 255.0f + 0.5f);
    return (al << 24) | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}
void arc_trail_draw(const arc_trail *t, const arc_trail_style *st) {
    if (!t || t->count < 2 || t->count > ARC_TRAIL_MAX || t->first < 0 || t->first >= ARC_TRAIL_MAX) return;
    arc_trail_style z; if (!st) { memset(&z, 0, sizeof z); st = &z; }
    if (TR.frame != arc__gfx_frame) { TR.frame = arc__gfx_frame; TR.used = 0; }
    if (TR.used >= TRAIL_POOL) { static bool warned; if (!warned) { warned = true; arc_log("trails: more than %d in one frame", TRAIL_POOL); } return; }
    arc_texture tex = st->texture;
    if (!tex.id || tex.id >= ARC__MAX_TEXTURES || !arc__textures[tex.id].used) {   /* default: soft across, solid along */
        if (!TR.edge.id || !arc__textures[TR.edge.id].used) {
            enum { EW = 4, EH = 32 }; static uint8_t px[EW * EH * 4];
            for (int y = 0; y < EH; y++) for (int x = 0; x < EW; x++) {
                float d = fabsf(((float)y + 0.5f) / EH * 2 - 1), a = arc_clampf((1.0f - d) * 2.0f, 0, 1); a = a * a * (3 - 2 * a);
                uint8_t *o = &px[(y * EW + x) * 4]; o[0] = o[1] = o[2] = 255; o[3] = (uint8_t)(a * 255.0f + 0.5f);
            }
            TR.edge = arc_texture_create_ex(EW, EH, px, ARC_TEX_SMOOTH);
        }
        tex = TR.edge;
    }
    int slot = TR.used++;
    if (!TR.mesh[slot] || !arc__meshes[TR.mesh[slot]].used) {   /* a strip of ARC_TRAIL_MAX pairs, made once */
        static arc_vertex v0[ARC_TRAIL_MAX * 2]; static uint32_t idx[(ARC_TRAIL_MAX - 1) * 6];
        memset(v0, 0, sizeof v0);
        for (int i = 0; i < ARC_TRAIL_MAX - 1; i++) {
            uint32_t a = (uint32_t)i * 2; uint32_t *o = idx + i * 6;
            o[0] = a; o[1] = a + 1; o[2] = a + 3; o[3] = a; o[4] = a + 3; o[5] = a + 2;
        }
        arc_mesh m = arc_mesh_create(v0, ARC_TRAIL_MAX * 2, idx, (ARC_TRAIL_MAX - 1) * 6);
        if (!m.id) { TR.used--; return; }
        arc_mesh_set_double_sided(m, true);
        TR.mesh[slot] = m.id;
    }
    arc_mesh m = { TR.mesh[slot] };
    if (arc__meshes[m.id].texture != tex.id) arc_mesh_set_texture(m, tex);
    float life = trail_life(t), w0 = st->width > 0 ? st->width : 0.2f, w1 = st->width_end > 0 ? st->width_end : w0;
    uint32_t c0 = st->color ? st->color : 0xFFFFFF, c1 = st->color_end ? st->color_end : c0;
    float a0 = st->alpha > 0 ? st->alpha : 1.0f, a1 = st->alpha_end;
    arc_v3 F = arc__camera_axis(2);
    int n = t->count, newest = (t->first + n - 1) % ARC_TRAIL_MAX;
    arc_v3 origin = t->p[newest];
    static arc_vertex v[ARC_TRAIL_MAX * 2];
    for (int j = 0; j < n; j++) {   /* j = 0 the head (newest) */
        int k = (t->first + n - 1 - j) % ARC_TRAIL_MAX;
        int kp = (t->first + n - 1 - arc_maxi(j - 1, 0)) % ARC_TRAIL_MAX, kn = (t->first + n - 1 - arc_mini(j + 1, n - 1)) % ARC_TRAIL_MAX;
        arc_v3 d = arc_v3_sub(t->p[kp], t->p[kn]), side = arc_v3_cross(d, F);
        float sl = arc_v3_len(side), u = arc_clampf(t->age[k] / life, 0, 1);
        side = sl > 1e-8f ? arc_v3_scale(side, 0.5f * arc_lerpf(w0, w1, u) / sl) : arc_v3_make(0, 0, 0);
        uint32_t col = trail_rgba(lerp_rgb(c0, c1, u), arc_lerpf(a0, a1, u));
        arc_v3 p = arc_v3_sub(t->p[k], origin);
        arc_vertex *L = &v[j * 2], *Rv = &v[j * 2 + 1];
        memset(L, 0, sizeof *L * 2);
        L->px = p.x + side.x; L->py = p.y + side.y; L->pz = p.z + side.z; L->nx = F.x; L->ny = F.y; L->nz = F.z; L->color = col; L->u = u; L->v = 0;
        Rv->px = p.x - side.x; Rv->py = p.y - side.y; Rv->pz = p.z - side.z; Rv->nx = F.x; Rv->ny = F.y; Rv->nz = F.z; Rv->color = col; Rv->u = u; Rv->v = 1;
    }
    for (int j = n; j < ARC_TRAIL_MAX; j++) { v[j * 2] = v[n * 2 - 2]; v[j * 2 + 1] = v[n * 2 - 1]; }   /* unused: folded onto the tail */
    arc_mesh_update(m, v, ARC_TRAIL_MAX * 2);
    arc__draw_unlit(m.id, arc_m4_translate(origin), 0xFFFFFF, st->glow, st->additive, 1);
}

/* ---- decals (0.7): a fixed pool of flat quads on surfaces, drawn lit, depth-test-only and pulled forward ---- */
arc_v3 arc__camera_eye(void);   /* arc_gfx.c */
typedef struct { bool used; uint32_t tex; arc_v3 pos, n; float w, h, angle, life; uint32_t argb; int flags; uint64_t born, seq; } fx_decal;
static struct { fx_decal d[ARC_MAX_DECALS]; uint64_t seq; float fade0, fade1; bool touched; } DC;
static void decal_expire(void) {
    uint64_t now = arc_tick_count(); float dt = arc_dt();
    for (int i = 0; i < ARC_MAX_DECALS; i++) {
        fx_decal *d = &DC.d[i];
        if (d->used && d->life > 0 && (float)(now - d->born) * dt >= d->life) d->used = false;
    }
}
void arc_decal_add_ex(arc_texture tex, arc_v3 pos, arc_v3 normal, float w, float h, float angle, uint32_t argb, float life, int flags) {
    if (w <= 0 || h <= 0 || arc_v3_len(normal) < 1e-6f) return;
    DC.touched = true;
    decal_expire();
    int slot = -1;
    for (int i = 0; i < ARC_MAX_DECALS && slot < 0; i++) if (!DC.d[i].used) slot = i;
    if (slot < 0) { slot = 0; for (int i = 1; i < ARC_MAX_DECALS; i++) if (DC.d[i].seq < DC.d[slot].seq) slot = i; }   /* the oldest */
    fx_decal *d = &DC.d[slot];
    d->used = true; d->tex = tex.id; d->pos = pos; d->n = arc_v3_norm(normal); d->w = w; d->h = h; d->angle = angle;
    d->argb = argb; d->life = life > 0 ? life : 0; d->flags = flags; d->born = arc_tick_count(); d->seq = ++DC.seq;
}
void arc_decal_add(arc_texture tex, arc_v3 pos, arc_v3 normal, float size, float angle, uint32_t argb, float life) {
    arc_decal_add_ex(tex, pos, normal, size, size, angle, argb, life, 0);
}
/* ---- 0.9.9 image operations (see arc.h) ---- */
static uint8_t img_u8(float v) { v = v * 255.0f + 0.5f; return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }
static float img_vnoise_wrap(float x, float y, int px, int py, uint32_t seed) {   /* value noise whose lattice repeats every px, py */
    float fx = floorf(x), fy = floorf(y); int ix = (int)fx, iy = (int)fy; float u = x - fx, v = y - fy;
    u = u * u * (3 - 2 * u); v = v * v * (3 - 2 * v);
    int x0 = ((ix % px) + px) % px, x1 = (x0 + 1) % px, y0 = ((iy % py) + py) % py, y1 = (y0 + 1) % py;
    const float k = 1.0f / 16777215.0f;
    float a = (float)(noise_hash(x0, y0, seed) >> 8) * k, b = (float)(noise_hash(x1, y0, seed) >> 8) * k;
    float c = (float)(noise_hash(x0, y1, seed) >> 8) * k, d = (float)(noise_hash(x1, y1, seed) >> 8) * k;
    float ab = a + (b - a) * u, cd = c + (d - c) * u; return ab + (cd - ab) * v;
}
static float img_fbm(float u, float v, float cells, int oct, uint32_t seed) {   /* u, v in 0..1 of the image */
    int p = cells < 1 ? 1 : (int)(cells + 0.5f); float sum = 0, amp = 1, tot = 0;
    for (int i = 0; i < oct; i++) {
        sum += amp * img_vnoise_wrap(u * (float)p, v * (float)p, p, p, seed + (uint32_t)i * 1013904223u);
        tot += amp; amp *= 0.5f; p *= 2;
    }
    return sum / tot;
}
void arc_image_multiply(uint8_t *rgba, int w, int h, uint32_t rgb, float amount) {
    if (!rgba || w <= 0 || h <= 0) return;
    float m[4]; arc__unpack_rgb(rgb, m); amount = arc_clampf(amount, 0, 1);
    for (size_t i = 0; i < (size_t)w * h; i++) for (int k = 0; k < 3; k++) {
        float c = rgba[i * 4 + k] / 255.0f; rgba[i * 4 + k] = img_u8(c + (c * m[k] - c) * amount);
    }
}
void arc_image_overlay(uint8_t *rgba, int w, int h, const uint8_t *src, int sw, int sh, int mode, float opacity, bool tile) {
    if (!rgba || !src || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    opacity = arc_clampf(opacity, 0, 1);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float b[4];
        if (tile) { const uint8_t *q = src + ((size_t)(y % sh) * sw + (size_t)(x % sw)) * 4; for (int k = 0; k < 4; k++) b[k] = q[k] / 255.0f; }
        else {   /* bilinear, pixel centres matched, edges clamped */
            float sx = ((float)x + 0.5f) * (float)sw / (float)w - 0.5f, sy = ((float)y + 0.5f) * (float)sh / (float)h - 0.5f;
            float fx = floorf(sx), fy = floorf(sy), tx = sx - fx, ty = sy - fy;
            int x0 = arc_clampi((int)fx, 0, sw - 1), x1 = arc_clampi((int)fx + 1, 0, sw - 1), y0 = arc_clampi((int)fy, 0, sh - 1), y1 = arc_clampi((int)fy + 1, 0, sh - 1);
            const uint8_t *q00 = src + ((size_t)y0 * sw + x0) * 4, *q10 = src + ((size_t)y0 * sw + x1) * 4, *q01 = src + ((size_t)y1 * sw + x0) * 4, *q11 = src + ((size_t)y1 * sw + x1) * 4;
            for (int k = 0; k < 4; k++) { float t0 = q00[k] + (q10[k] - q00[k]) * tx, t1 = q01[k] + (q11[k] - q01[k]) * tx; b[k] = (t0 + (t1 - t0) * ty) / 255.0f; }
        }
        float wgt = b[3] * opacity; uint8_t *d = rgba + ((size_t)y * w + x) * 4;
        if (wgt <= 0) continue;
        for (int k = 0; k < 3; k++) {
            float a = d[k] / 255.0f, f;
            switch (mode) {
            case ARC_IMAGE_MULTIPLY: f = a * b[k]; break;
            case ARC_IMAGE_SCREEN:   f = 1 - (1 - a) * (1 - b[k]); break;
            case ARC_IMAGE_OVERLAY:  f = a < 0.5f ? 2 * a * b[k] : 1 - 2 * (1 - a) * (1 - b[k]); break;
            case ARC_IMAGE_ADD:      f = a + b[k]; break;
            default:                 f = b[k]; break;
            }
            d[k] = img_u8(a + (f - a) * wgt);
        }
    }
}
void arc_image_noise(uint8_t *rgba, int w, int h, float cells, int octaves, float amount, uint32_t seed) {
    if (!rgba || w <= 0 || h <= 0) return;
    if (octaves <= 0) octaves = 4;
    if (octaves > 12) octaves = 12;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float n = img_fbm(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h, cells, octaves, seed), s = 1 + amount * (2 * n - 1);
        uint8_t *d = rgba + ((size_t)y * w + x) * 4; for (int k = 0; k < 3; k++) d[k] = img_u8(d[k] / 255.0f * s);
    }
}
void arc_image_char(uint8_t *rgba, int w, int h, float amount, uint32_t seed) {
    if (!rgba || w <= 0 || h <= 0) return;
    float c = arc_clampf(amount, 0, 1);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        float u = ((float)x + 0.5f) / (float)w, v = ((float)y + 0.5f) / (float)h;
        float n1 = img_fbm(u, v, 5, 4, seed), t = arc_clampf((n1 - 0.35f) / 0.35f, 0, 1), soot = t * t * (3 - 2 * t);
        float n2 = img_fbm(u, v, 24, 2, seed + 31), e = arc_clampf(fabsf(n2 - 0.5f) / 0.02f, 0, 1), sc = 1 - e * e * (3 - 2 * e);
        uint8_t *d = rgba + ((size_t)y * w + x) * 4;
        for (int k = 0; k < 3; k++) d[k] = img_u8(d[k] / 255.0f * (1 - 0.8f * c) * (1 - 0.6f * c * soot) + 0.55f * sc * c * (1 - soot));
    }
}
/* ---- 0.8.4 grime overlays (see arc_breakup in arc.h; tools/grime.py is the same thing offline) ---- */
uint8_t *arc_image_breakup(const uint8_t *rgba, int w, int h, const arc_breakup *bp) {
    if (!rgba || w <= 0 || h <= 0) return NULL;
    arc_breakup b; if (bp) b = *bp; else memset(&b, 0, sizeof b);
    int size = b.size > 0 ? b.size : 512; if (size > 8192) size = 8192;
    float crop = b.crop > 0 ? arc_minf(b.crop, 1) : 1, thr = b.threshold > 0 ? b.threshold : 0.45f, soft = b.softness > 0 ? b.softness : 0.12f;
    float cells = b.mask_cells > 0 ? b.mask_cells : 4, edge = b.edge == 0 ? 0.35f : b.edge, dark = b.darken > 0 ? b.darken : 0.6f;
    int oct = b.octaves > 0 ? b.octaves : 5, mn = w < h ? w : h, cs = (int)((float)mn * crop + 0.5f); if (cs < 1) cs = 1;
    int cx0 = w > cs ? (int)(noise_hash(17, 29, b.seed) % (uint32_t)(w - cs + 1)) : 0, cy0 = h > cs ? (int)(noise_hash(31, 7, b.seed) % (uint32_t)(h - cs + 1)) : 0;
    uint8_t *out = (uint8_t *)malloc((size_t)size * size * 4); float *lum = (float *)malloc(sizeof(float) * (size_t)size * size);
    if (!out || !lum) { free(out); free(lum); return NULL; }
    double sum = 0, sum2 = 0; float sc = (float)cs / (float)size;
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {   /* bilinear from the crop */
        float sx = arc_clampf(((float)x + 0.5f) * sc - 0.5f, 0, (float)(cs - 1)), sy = arc_clampf(((float)y + 0.5f) * sc - 0.5f, 0, (float)(cs - 1));
        int x0 = (int)sx, y0 = (int)sy, x1 = x0 + 1 < cs ? x0 + 1 : x0, y1 = y0 + 1 < cs ? y0 + 1 : y0; float fx = sx - (float)x0, fy = sy - (float)y0;
        const uint8_t *p00 = rgba + ((size_t)(cy0 + y0) * w + cx0 + x0) * 4, *p10 = rgba + ((size_t)(cy0 + y0) * w + cx0 + x1) * 4;
        const uint8_t *p01 = rgba + ((size_t)(cy0 + y1) * w + cx0 + x0) * 4, *p11 = rgba + ((size_t)(cy0 + y1) * w + cx0 + x1) * 4;
        uint8_t *o = out + ((size_t)y * size + x) * 4; float c[3];
        for (int k = 0; k < 3; k++) {
            float t = (float)p00[k] + ((float)p10[k] - (float)p00[k]) * fx, u = (float)p01[k] + ((float)p11[k] - (float)p01[k]) * fx;
            c[k] = t + (u - t) * fy; o[k] = (uint8_t)arc_clampf(c[k] + 0.5f, 0, 255);
        }
        float l = 0.3f * c[0] + 0.59f * c[1] + 0.11f * c[2]; lum[(size_t)y * size + x] = l; sum += l; sum2 += (double)l * l;
    }
    double n = (double)size * size, mean = sum / n, var = sum2 / n - mean * mean, sd = var > 0 ? sqrt(var) : 0;
    float half = (float)size * 0.5f;
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        uint8_t *o = out + ((size_t)y * size + x) * 4; float l = lum[(size_t)y * size + x];
        float amount = sd > 0 ? arc_clampf((float)(fabs((double)l - mean) / (2 * sd)), 0, 1) : 0;
        if ((double)l < mean) for (int k = 0; k < 3; k++) o[k] = (uint8_t)((float)o[k] * dark + 0.5f);
        float nx = ((float)x + 0.5f) / (float)size * cells, ny = ((float)y + 0.5f) / (float)size * cells;
        float m = arc_clampf((arc_noise_fbm2(nx, ny, oct, b.seed * 7u + 1u) - thr) / soft, 0, 1);
        if (b.second > 0) m = arc_maxf(m, arc_minf(b.second, 1) * arc_clampf((arc_noise_fbm2(nx, ny, oct, b.seed * 7u + 2u) - thr) / soft, 0, 1));
        float fade = 1;
        if (edge > 0) {
            float dx = ((float)x + 0.5f - half) / half, dy = ((float)y + 0.5f - half) / half, r = sqrtf(dx * dx + dy * dy), e0 = 1 - arc_minf(edge, 1);
            float t = arc_clampf((r - e0) / (1 - e0), 0, 1); fade = 1 - t * t * (3 - 2 * t);
        }
        o[3] = (uint8_t)(amount * m * fade * 255.0f + 0.5f);
    }
    free(lum); return out;
}
arc_texture arc_texture_breakup(const char *path, const arc_breakup *b) {
    int w = 0, h = 0; uint8_t *src = arc_image_load(path, &w, &h); arc_texture t = { 0 };
    if (!src) return t;
    uint8_t *px = arc_image_breakup(src, w, h, b); free(src);
    if (!px) return t;
    int size = b && b->size > 0 ? (b->size > 8192 ? 8192 : b->size) : 512;
    t = arc_texture_create_ex(size, size, px, ARC_TEX_MESH); free(px); return t;
}
/* ---- 0.8.4 stamps (see arc_stamps in arc.h) ---- */
static float stamp_r(int i, int j, uint32_t seed, int k, int t) {   /* 0..1, one per (cell, stamp, property) */
    return (float)(noise_hash(i, j, seed ^ ((uint32_t)(k * 977 + t + 1) * 2654435761u)) >> 8) * (1.0f / 16777216.0f);
}
static void stamps_grid(const arc_stamps *s, arc_v3 origin, arc_v3 us, arc_v3 vs, int i0, int i1, int j0, int j1, int rel,
                        float (*density)(void *, int, int), void *ud, uint32_t tint) {
    if (!s) return;
    float lu = arc_v3_len(us), lv = arc_v3_len(vs); if (lu < 1e-6f || lv < 1e-6f) return;
    arc_v3 U = arc_v3_scale(us, 1.0f / lu), V = arc_v3_scale(vs, 1.0f / lv), n = arc_v3_cross(vs, us);
    if (arc_v3_len(n) < 1e-9f) return;
    n = arc_v3_norm(n);
    float cellw = (lu + lv) * 0.5f;
    float smin = s->size_min > 0 ? s->size_min : 1.2f * cellw, smax = s->size_max > 0 ? s->size_max : (s->size_min > 0 ? s->size_min : 2.2f * cellw);
    if (smax < smin) smax = smin;
    float per = s->per_cell > 0 ? s->per_cell : 1, strength = s->strength > 0 ? arc_minf(s->strength, 1) : 1, gap = s->layer_gap > 0 ? s->layer_gap : 0.001f;
    int whole = (int)per; float frac = per - (float)whole; int count = s->count > 0 ? (s->count > 8 ? 8 : s->count) : 0;
    uint32_t mesh[8]; int nm = count ? count : 1;
    for (int t = 0; t < nm; t++) {
        uint32_t tex = count ? s->tex[t].id : 0;
        mesh[t] = (tex && (tex >= ARC__MAX_TEXTURES || !arc__textures[tex].used)) ? 0 : arc__fx_quad(tex);
    }
    int cell = arc__draw_cell; arc__draw_cell = 0;   /* like decals: never culled by cell */
    arc_mesh_style st; memset(&st, 0, sizeof st);
    st.tint = tint ? tint : 0xFFFFFF; st.depth = ARC_DEPTH_TEST_ONLY; st.depth_pull = 0.002f; st.layer = ARC_LAYER_TRANSPARENT;
    for (int j = j0; j < j1; j++) for (int i = i0; i < i1; i++) {
        float d = density ? density(ud, i - rel * i0, j - rel * j0) : 1.0f;
        float op = strength * arc_clampf(d, 0, 1);
        if (op <= 0.002f) continue;
        int nst = whole + (frac > 0 && stamp_r(i, j, s->seed, 0, 99) < frac ? 1 : 0);
        arc_v3 c = arc_v3_add(origin, arc_v3_add(arc_v3_scale(us, (float)i + 0.5f), arc_v3_scale(vs, (float)j + 0.5f)));
        for (int k = 0; k < nst; k++) {
            int var = count ? (int)(stamp_r(i, j, s->seed, k, 0) * (float)count) : 0; if (var >= nm) var = nm - 1;
            if (!mesh[var]) continue;
            float ang = stamp_r(i, j, s->seed, k, 1) * 2.0f * ARC_PI * arc_clampf(s->rotate, 0, 1), ca = cosf(ang), sa = sinf(ang);
            float size = smin + (smax - smin) * stamp_r(i, j, s->seed, k, 2);
            float ox = (stamp_r(i, j, s->seed, k, 3) * 2 - 1) * s->jitter, oy = (stamp_r(i, j, s->seed, k, 4) * 2 - 1) * s->jitter;
            arc_v3 p = arc_v3_add(c, arc_v3_add(arc_v3_add(arc_v3_scale(U, ox), arc_v3_scale(V, oy)), arc_v3_scale(n, gap * (float)(k + 1))));
            arc_v3 ax = arc_v3_add(arc_v3_scale(U, ca), arc_v3_scale(V, sa)), ay = arc_v3_cross(n, ax);
            st.transparency = arc_maxf(1.0f - op, 1e-4f); st.order = -100 + var;   /* one run (one draw) per texture */
            arc_draw_mesh_styled((arc_mesh){ mesh[var] }, arc_m4_basis(p, arc_v3_scale(ax, size), arc_v3_scale(ay, size), n), &st);
        }
    }
    arc__draw_cell = cell;
}
void arc_draw_stamps(const arc_stamps *s, float x0, float z0, float x1, float z1, float y,
                     float (*density)(void *ud, int cx, int cz), void *ud, uint32_t tint) {
    if (!s) return;
    float c = s->cell > 0 ? s->cell : 1;
    int i0 = (int)floorf(x0 / c), i1 = (int)ceilf(x1 / c), j0 = (int)floorf(z0 / c), j1 = (int)ceilf(z1 / c);
    stamps_grid(s, arc_v3_make(0, y, 0), arc_v3_make(c, 0, 0), arc_v3_make(0, 0, c), i0, i1, j0, j1, 0, density, ud, tint);
}
void arc_draw_stamps_on(const arc_stamps *s, arc_v3 origin, arc_v3 u_step, arc_v3 v_step, int cells_u, int cells_v,
                        float (*density)(void *ud, int cu, int cv), void *ud, uint32_t tint) {
    stamps_grid(s, origin, u_step, v_step, 0, cells_u, 0, cells_v, 1, density, ud, tint);
}
void arc_decal_fade_distance(float start, float end) { DC.fade0 = start; DC.fade1 = end; }
int  arc_decal_count(void) { int n = 0; uint64_t now = arc_tick_count(); for (int i = 0; i < ARC_MAX_DECALS; i++) { const fx_decal *d = &DC.d[i]; if (d->used && !(d->life > 0 && (float)(now - d->born) * arc_dt() >= d->life)) n++; } return n; }
void arc_decals_clear(void) { memset(DC.d, 0, sizeof DC.d); }
void arc_draw_decals(void) {
    float al = arc_clampf(arc__frame_alpha, 0, 1); uint64_t now = arc_tick_count();
    arc_v3 eye = arc__camera_eye();
    int cell = arc__draw_cell; arc__draw_cell = 0;   /* 0.7.1: decals are never culled by cell */
    for (int i = 0; i < ARC_MAX_DECALS; i++) {
        const fx_decal *d = &DC.d[i];
        if (!d->used) continue;
        if (d->tex && (d->tex >= ARC__MAX_TEXTURES || !arc__textures[d->tex].used)) continue;
        float a = (d->argb >> 24) ? (float)(d->argb >> 24) / 255.0f : 1.0f;
        if (d->life > 0) {   /* fades over the last quarter of its life */
            float age = ((float)(now - d->born) - (1.0f - al)) * arc_dt();
            a *= arc_clampf((d->life - age) / (d->life * 0.25f), 0, 1);
        }
        if (DC.fade1 > DC.fade0 && DC.fade1 > 0) a *= 1.0f - arc_clampf((arc_v3_dist(d->pos, eye) - DC.fade0) / (DC.fade1 - DC.fade0), 0, 1);
        if (a <= 0.002f) continue;
        arc_v3 t0 = any_perp(d->n), t1 = arc_v3_cross(d->n, t0);
        float c = cosf(d->angle), s = sinf(d->angle);
        arc_v3 ax = arc_v3_add(arc_v3_scale(t0, c), arc_v3_scale(t1, s)), ay = arc_v3_sub(arc_v3_scale(t1, c), arc_v3_scale(t0, s));
        uint32_t mesh = arc__fx_quad(d->tex);
        if (!mesh) continue;
        arc_mesh_style st; memset(&st, 0, sizeof st);
        st.tint = d->argb & 0xFFFFFF ? d->argb & 0xFFFFFF : 0x010101;
        st.additive = (d->flags & ARC_DECAL_ADDITIVE) != 0;
        st.transparency = st.additive ? 1.0f - a : arc_maxf(1.0f - a, 1e-4f);   /* always see-through: soft edges blend */
        st.depth = ARC_DEPTH_TEST_ONLY; st.depth_pull = 0.002f;
        st.layer = ARC_LAYER_TRANSPARENT; st.order = -1;   /* before other see-through things: they sit on surfaces */
        arc_draw_mesh_styled((arc_mesh){ mesh }, arc_m4_basis(d->pos, arc_v3_scale(ax, d->w), arc_v3_scale(ay, d->h), d->n), &st);
    }
    arc__draw_cell = cell;
}

/* state snapshots (seek / rewind / state_load): particles, their random stream and the emitters */
size_t arc__fx_state(arc_writer *w) {
    int32_t n = FX.ready ? FX.count : 0;
    arc_write(w, &n, 4);
    if (n) arc_write(w, FX.p, sizeof(fx_particle) * (size_t)n);
    arc_write(w, &FX.rng, sizeof FX.rng);
    arc_write(w, FX.em, sizeof FX.em);
    return w->len;
}
bool arc__fx_state_read(arc_reader *r, bool commit) {
    int32_t n = 0;
    if (!arc_read(r, &n, 4) || n < 0 || n > ARC__MAX_PARTICLES) { r->fail = true; return false; }
    size_t pb = sizeof(fx_particle) * (size_t)n;
    if (r->pos > r->size || pb + sizeof(arc_rng) + sizeof FX.em > r->size - r->pos) { r->fail = true; return false; }
    const uint8_t *q = r->data + r->pos;
    for (int i = 0; i < n; i++) { fx_particle t; memcpy(&t, q + sizeof t * (size_t)i, sizeof t); if (t.kind >= FX.kind_count) { r->fail = true; return false; } }
    for (int i = 0; i < ARC__MAX_EMITTERS; i++) {
        fx_emitter e; memcpy(&e, q + pb + sizeof(arc_rng) + sizeof e * (size_t)i, sizeof e);
        if (e.used && e.kind >= FX.kind_count) { r->fail = true; return false; }
    }
    if (!commit) { r->pos += pb + sizeof(arc_rng) + sizeof FX.em; return true; }
    fx_init();
    memcpy(FX.p, q, pb); FX.count = n;
    memcpy(&FX.rng, q + pb, sizeof(arc_rng));
    memcpy(FX.em, q + pb + sizeof(arc_rng), sizeof FX.em);
    r->pos += pb + sizeof(arc_rng) + sizeof FX.em;
    return true;
}

/* decals: written after the UI state only once the game has used them (older states simply have none) */
size_t arc__decal_state(arc_writer *w) {
    if (!DC.touched) return w->len;
    uint32_t magic = 0x314C4344u;   /* "DCL1" */
    arc_write(w, &magic, 4); arc_write(w, &DC.seq, sizeof DC.seq); arc_write(w, DC.d, sizeof DC.d);
    return w->len;
}
bool arc__decal_state_read(arc_reader *r, bool commit) {
    if (r->pos >= r->size) { if (commit && DC.touched) memset(DC.d, 0, sizeof DC.d); return true; }
    uint32_t magic = 0;
    if (r->size - r->pos >= 4) memcpy(&magic, r->data + r->pos, 4);
    if (magic != 0x314C4344u) { if (commit && DC.touched) memset(DC.d, 0, sizeof DC.d); return true; }   /* 0.7.2: another block (the editor's) follows */
    if (r->size - r->pos < 4 + sizeof DC.seq + sizeof DC.d) { r->fail = true; return false; }
    if (!commit) { r->pos += 4 + sizeof DC.seq + sizeof DC.d; return true; }
    r->pos += 4; arc_read(r, &DC.seq, sizeof DC.seq); arc_read(r, DC.d, sizeof DC.d); DC.touched = true;
    return true;
}

/* ---- 0.10.14 juice: floating text (score popups, "+1 UP", damage numbers). Looks only: ARC ages and draws them
   after the game's draw(), so they never touch the game's state; they show in `frame` like any text. ---- */
#define ARC__MAX_POPUPS 64
typedef struct { bool on, screen; float age, life, scale; arc_v3 p; uint32_t rgb; char text[32]; } fx_popup;   /* screen: p.x, p.y are pixels */
static fx_popup g_popup[ARC__MAX_POPUPS];
static void popup_add(arc_v3 pos, bool screen, uint32_t rgb, float scale, const char *text);
void arc_popup_text(arc_v3 pos, uint32_t rgb, float scale, const char *text) { popup_add(pos, false, rgb, scale, text); }
/* 0.10.14: the same for 2D games: (x, y) in screen pixels; it rises 60 pixels */
void arc_popup_text_2d(float x, float y, uint32_t rgb, float scale, const char *text) { popup_add(arc_v3_make(x, y, 0), true, rgb, scale, text); }
static void popup_add(arc_v3 pos, bool screen, uint32_t rgb, float scale, const char *text) {
    if (!text || !*text) return;
    if (scale <= 0) scale = 2.0f;
    int stacked = 0, slot = -1;
    for (int i = 0; i < ARC__MAX_POPUPS; i++) {
        fx_popup *q = &g_popup[i];
        if (!q->on) { if (slot < 0) slot = i; continue; }
        if (q->age < 0.45f && q->screen == screen && (screen ? fabsf(q->p.x - pos.x) < 60 && fabsf(q->p.y - pos.y) < 40
                                                            : fabsf(q->p.x - pos.x) < 1.5f && fabsf(q->p.z - pos.z) < 1.5f)) stacked++;
    }
    if (slot < 0) { slot = 0; for (int i = 1; i < ARC__MAX_POPUPS; i++) if (g_popup[i].age > g_popup[slot].age) slot = i; }   /* full: the oldest goes */
    fx_popup *q = &g_popup[slot];
    q->on = true; q->screen = screen; q->age = 0; q->life = 0.85f; q->scale = scale; q->rgb = rgb;
    q->p = screen ? arc_v3_make(pos.x, pos.y - 9.0f * scale * (float)stacked, 0)     /* a chain of them stacks into a column */
                  : arc_v3_make(pos.x, pos.y + 0.55f * (float)stacked, pos.z);
    size_t n = strlen(text); if (n > sizeof q->text - 1) n = sizeof q->text - 1;
    memcpy(q->text, text, n); q->text[n] = 0;
}
void arc_popups_clear(void) { memset(g_popup, 0, sizeof g_popup); }
int  arc_popup_count(void) { int n = 0; for (int i = 0; i < ARC__MAX_POPUPS; i++) n += g_popup[i].on; return n; }
void arc__popups_tick(float dt) {
    for (int i = 0; i < ARC__MAX_POPUPS; i++) if (g_popup[i].on && (g_popup[i].age += dt) >= g_popup[i].life) g_popup[i].on = false;
}
void arc__popups_render(void) {
    for (int i = 0; i < ARC__MAX_POPUPS; i++) if (g_popup[i].on) {
        const fx_popup *q = &g_popup[i]; float t = q->age / q->life; arc_v2 s;
        if (q->screen) s = arc_v2_make(q->p.x, q->p.y - t * 60.0f);
        else if (!arc_world_to_screen(arc_v3_make(q->p.x, q->p.y + t * 1.2f, q->p.z), &s)) continue;
        float sc = q->scale * (0.5f + 0.5f * arc_ease_out_back(arc_minf(1.0f, t * 5.0f)));      /* pops up */
        if (t > 0.8f) sc *= (1.0f - t) / 0.2f;                                                     /* and shrinks away */
        if (sc > 0.2f) arc_draw_text_ex(s.x, s.y, sc, q->rgb, ARC_TEXT_CENTER | ARC_TEXT_MIDDLE | ARC_TEXT_OUTLINE, q->text);
    }
}
