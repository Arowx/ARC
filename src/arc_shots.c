/* ARC 0.10.0 shots: hitscan and projectiles against the grid, the collision world, physics bodies and the game's
   own targets; tracers, impact decals, sparks, sounds and short lights. Also the sweep the camera rigs use.
   Simulation maths: plain + - * / and arc_sqrtf (deterministic everywhere). */

/* ---- sweeps: a ray or a sphere through a world (shared with arc_rig.c) ---- */
typedef struct {
    arc_cworld cw; arc_physics phys; uint32_t phys_mask;
    bool (*grid)(int x, int z, void *user); void *user;
    float cell, ox, oz;
} sweep_world;
typedef struct { bool hit; float t; arc_v3 point, normal; int kind, id, cx, cz; } sweep_hit;

static float sw_floor(float v) { float f = (float)(int)v; return f > v ? f - 1.0f : f; }
static int sw_cell(float v, float o, float cell) { return (int)sw_floor((v - o) / cell); }
static float sw_len(arc_v3 v) { return arc_sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
static arc_v3 sw_norm(arc_v3 v) { float l = sw_len(v); return l > 1e-12f ? arc_v3_make(v.x / l, v.y / l, v.z / l) : arc_v3_make(0, 0, 0); }
static void sw_take(sweep_hit *best, float t, arc_v3 o, arc_v3 d, arc_v3 n, int kind, int id) {
    if (best->hit && t >= best->t) return;
    best->hit = true; best->t = t; best->point = arc_v3_make(o.x + d.x * t, o.y + d.y * t, o.z + d.z * t);
    best->normal = n; best->kind = kind; best->id = id;
}
/* grid, ray: cell by cell (the cell it starts in never blocks) */
static void sw_grid_ray(const sweep_world *w, arc_v3 o, arc_v3 d, float maxt, sweep_hit *best) {
    float cell = w->cell > 0 ? w->cell : 1.0f;
    int x = sw_cell(o.x, w->ox, cell), z = sw_cell(o.z, w->oz, cell);
    int sx = d.x > 0 ? 1 : d.x < 0 ? -1 : 0, sz = d.z > 0 ? 1 : d.z < 0 ? -1 : 0;
    float big = 3.4e38f;
    float tdx = sx ? cell / (d.x > 0 ? d.x : -d.x) : big, tdz = sz ? cell / (d.z > 0 ? d.z : -d.z) : big;
    float bx = w->ox + (float)(x + (sx > 0)) * cell, bz = w->oz + (float)(z + (sz > 0)) * cell;
    float tmx = sx ? (bx - o.x) / d.x : big, tmz = sz ? (bz - o.z) / d.z : big;
    if (best->hit && best->t < maxt) maxt = best->t;
    for (int steps = 0; steps < 4096; steps++) {
        float t; arc_v3 n;
        if (tmx < tmz) { t = tmx; x += sx; tmx += tdx; n = arc_v3_make((float)-sx, 0, 0); }
        else { t = tmz; z += sz; tmz += tdz; n = arc_v3_make(0, 0, (float)-sz); }
        if (t > maxt || t >= big) return;
        if (w->grid(x, z, w->user)) { sw_take(best, t, o, d, n, ARC_HIT_GRID, x + z * 65536); best->cx = x; best->cz = z; return; }
    }
}
/* the nearest blocked cell touching a circle (xz) at p, skipping cells that touch the start circle; returns the
   push-out normal */
static bool sw_grid_circle(const sweep_world *w, arc_v3 p, float r, arc_v3 start, arc_v3 *n, int *hx, int *hz) {
    float cell = w->cell > 0 ? w->cell : 1.0f;
    int x0 = sw_cell(p.x - r, w->ox, cell), x1 = sw_cell(p.x + r, w->ox, cell), z0 = sw_cell(p.z - r, w->oz, cell), z1 = sw_cell(p.z + r, w->oz, cell);
    float bestd = 3.4e38f; bool any = false;
    for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
        if (!w->grid(x, z, w->user)) continue;
        float lx = w->ox + (float)x * cell, lz = w->oz + (float)z * cell, hxv = lx + cell, hzv = lz + cell;
        float qx = start.x < lx ? lx : start.x > hxv ? hxv : start.x, qz = start.z < lz ? lz : start.z > hzv ? hzv : start.z;
        if ((start.x - qx) * (start.x - qx) + (start.z - qz) * (start.z - qz) < r * r) continue;   /* touched at the start */
        float cx = p.x < lx ? lx : p.x > hxv ? hxv : p.x, cz = p.z < lz ? lz : p.z > hzv ? hzv : p.z;
        float dx = p.x - cx, dz = p.z - cz, d2 = dx * dx + dz * dz;
        if (d2 < r * r && d2 < bestd) {
            bestd = d2; any = true; *hx = x; *hz = z;
            float dl = arc_sqrtf(d2); *n = dl > 1e-6f ? arc_v3_make(dx / dl, 0, dz / dl) : arc_v3_make(0, 0, 0);
        }
    }
    return any;
}
static bool sw_world_sphere(const sweep_world *w, arc_v3 p, float r, int owner, arc_v3 start, bool start_cw, int kind_mask, arc_v3 *n, int *kind, int *id, int *cx, int *cz) {
    if ((kind_mask & 1) && w->grid && sw_grid_circle(w, p, r, start, n, cx, cz)) { *kind = ARC_HIT_GRID; *id = *cx + *cz * 65536; return true; }
    if ((kind_mask & 2) && w->cw.id && !start_cw && arc_cworld_overlap_sphere(w->cw, p, r)) {
        arc_v3 q = p; arc_cworld_push_sphere(w->cw, &q, r);
        *n = sw_norm(arc_v3_sub(q, p)); *kind = ARC_HIT_WORLD; *id = 0; return true;
    }
    if ((kind_mask & 4) && w->phys.id) {
        int bodies[16]; int nb = arc_physics_overlap_sphere(w->phys, p, r, w->phys_mask ? w->phys_mask : 0xFFFFFFFFu, bodies, 16);
        for (int i = 0; i < nb && i < 16; i++) if (bodies[i] != owner) { *n = sw_norm(arc_v3_sub(p, arc_physics_pos(w->phys, bodies[i]))); *kind = ARC_HIT_BODY; *id = bodies[i]; return true; }
    }
    return false;
}
/* the whole sweep: grid, collision world and physics (kind_mask bits 1, 2, 4), nearest first */
static sweep_hit sweep(const sweep_world *w, arc_v3 o, arc_v3 d, float maxt, float radius, int owner, int kind_mask) {
    sweep_hit best; memset(&best, 0, sizeof best);
    if (maxt <= 0) return best;
    if (radius <= 0) {
        if ((kind_mask & 1) && w->grid) sw_grid_ray(w, o, d, maxt, &best);
        if ((kind_mask & 2) && w->cw.id) { arc_hit h; if (arc_cworld_raycast(w->cw, o, d, best.hit ? best.t : maxt, &h) && h.hit) sw_take(&best, h.t, o, d, h.normal, ARC_HIT_WORLD, (int)h.user); }
        if ((kind_mask & 4) && w->phys.id) {
            arc_v3 s = o; float done = 0;
            for (int k = 0; k < 64; k++) {   /* past the owner (in small steps: a ray starting inside it may hit it again) */
                arc_physics_hit h;
                if (!arc_physics_raycast(w->phys, s, d, (best.hit ? best.t : maxt) - done, w->phys_mask ? w->phys_mask : 0xFFFFFFFFu, &h)) break;
                if (h.body != owner || owner == 0) { sw_take(&best, done + h.t, o, d, h.normal, ARC_HIT_BODY, h.body); break; }
                float adv = h.t + 0.02f; done += adv; s = arc_v3_make(s.x + d.x * adv, s.y + d.y * adv, s.z + d.z * adv);
            }
        }
        return best;
    }
    /* a sphere: march in steps of radius / 2, then halve the last step down to the contact */
    bool start_cw = w->cw.id && (kind_mask & 2) && arc_cworld_overlap_sphere(w->cw, o, radius);
    if (start_cw) { sweep_hit r = sweep(w, o, d, maxt, 0, owner, 2); if (r.hit) best = r; }   /* already touching: the ray only */
    float step = radius * 0.5f; if (step < 0.01f) step = 0.01f;
    int n = (int)(maxt / step) + 1; if (n > 4096) { n = 4096; step = maxt / 4096.0f; }
    float prev = 0;
    for (int i = 1; i <= n; i++) {
        float t = i == n ? maxt : (float)i * step;
        if (best.hit && prev >= best.t) break;
        arc_v3 p = arc_v3_make(o.x + d.x * t, o.y + d.y * t, o.z + d.z * t), nn; int kind, id, cx = 0, cz = 0;
        if (sw_world_sphere(w, p, radius, owner, o, start_cw, kind_mask, &nn, &kind, &id, &cx, &cz)) {
            float lo = prev, hi = t;
            for (int k = 0; k < 10; k++) {
                float mid = (lo + hi) * 0.5f; arc_v3 q = arc_v3_make(o.x + d.x * mid, o.y + d.y * mid, o.z + d.z * mid), n2; int k2, i2, a2 = 0, b2 = 0;
                if (sw_world_sphere(w, q, radius, owner, o, start_cw, kind_mask, &n2, &k2, &i2, &a2, &b2)) { hi = mid; nn = n2; kind = k2; id = i2; cx = a2; cz = b2; } else lo = mid;
            }
            if (nn.x == 0 && nn.y == 0 && nn.z == 0) nn = arc_v3_make(-d.x, -d.y, -d.z);
            sw_take(&best, hi, o, d, nn, kind, id); best.cx = cx; best.cz = cz;
            break;
        }
        prev = t;
    }
    return best;
}
/* ray (radius r) against a capsule a-b of radius R: the first t in [0, maxt], or -1 */
static float sw_sphere_t(arc_v3 o, arc_v3 d, arc_v3 c, float R) {
    arc_v3 m = arc_v3_sub(o, c); float b = m.x * d.x + m.y * d.y + m.z * d.z, cc = m.x * m.x + m.y * m.y + m.z * m.z - R * R;
    if (cc <= 0) return 0;
    if (b > 0) return -1;
    float disc = b * b - cc; if (disc < 0) return -1;
    return -b - arc_sqrtf(disc);
}
static float sw_capsule_t(arc_v3 o, arc_v3 d, arc_v3 a, arc_v3 b, float R) {
    arc_v3 ab = arc_v3_sub(b, a); float L2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    if (L2 < 1e-10f) return sw_sphere_t(o, d, a, R);
    float best = -1;
    {   /* the cylinder between the end caps */
        arc_v3 ao = arc_v3_sub(o, a);
        float abd = ab.x * d.x + ab.y * d.y + ab.z * d.z, abao = ab.x * ao.x + ab.y * ao.y + ab.z * ao.z, aod = ao.x * d.x + ao.y * d.y + ao.z * d.z, aoao = ao.x * ao.x + ao.y * ao.y + ao.z * ao.z;
        float A = L2 - abd * abd, B = L2 * aod - abao * abd, C = L2 * aoao - abao * abao - R * R * L2;
        if (A > 1e-10f) {
            float disc = B * B - A * C;
            if (disc >= 0) {
                float t = (-B - arc_sqrtf(disc)) / A, y = abao + t * abd;
                if (C <= 0 && abao > 0 && abao < L2) return 0;   /* starts inside */
                if (t >= 0 && y > 0 && y < L2) best = t;
            }
        }
    }
    float ta = sw_sphere_t(o, d, a, R), tb = sw_sphere_t(o, d, b, R);
    if (ta >= 0 && (best < 0 || ta < best)) best = ta;
    if (tb >= 0 && (best < 0 || tb < best)) best = tb;
    return best;
}

/* ---- shots ---- */
typedef struct {
    bool used; int id;
    arc_v3 pos, dir; float speed, left, radius; uint32_t mask; int owner; uint32_t user;
    uint32_t tracer_rgb; float tracer_width, tracer_life, size;
    uint32_t decal_tex; float decal_size; uint32_t decal_argb;
    int fx; uint32_t sound; float volume, flash; uint32_t flash_rgb;
    arc_v3 start; float travelled;
} shot_rec;
typedef struct { arc_v3 a, b; uint32_t rgb; float width, life, age; } tracer_rec;
typedef struct { arc_v3 pos; uint32_t rgb; float power, life, age; } flash_rec;
#define SHOT_TRACERS 128
#define SHOT_FLASHES 64
static struct {   /* saved: everything up to `end` */
    uint32_t magic;
    int next_id;
    shot_rec p[ARC_SHOTS_MAX];
    tracer_rec tr[SHOT_TRACERS]; int tr_next;
    flash_rec fl[SHOT_FLASHES]; int fl_next;
    arc_shot_hit hits[ARC_SHOTS_HITS]; int nhits; uint64_t hits_tick;
    int end;
} SH;
static struct {   /* not saved: set by the game */
    bool touched;
    sweep_world w;
    arc_shot_target tg[ARC_SHOTS_TARGETS]; int ntg;
    const arc_fx *fx[64]; int nfx;
    arc_mesh streak;
} SHX = { false, { { 0 }, { 0 }, 0, NULL, NULL, 1.0f, 0, 0 }, { { 0 } }, 0, { 0 }, 0, { 0 } };
#define SHOTS_MAGIC 0x31544853u   /* "SHT1" */
static size_t shots_saved_size(void) { return (size_t)((const char *)&SH.end - (const char *)&SH); }

void arc_shots_set_world(arc_cworld cw, arc_physics phys, bool (*grid_blocks)(int x, int z, void *user), void *user) {
    SHX.w.cw = cw; SHX.w.phys = phys; SHX.w.grid = grid_blocks; SHX.w.user = user;
}
void arc_shots_set_grid(float cell, float origin_x, float origin_z) { SHX.w.cell = cell > 0 ? cell : 1.0f; SHX.w.ox = origin_x; SHX.w.oz = origin_z; }
void arc_shots_targets(const arc_shot_target *t, int count) {
    if (count > ARC_SHOTS_TARGETS) count = ARC_SHOTS_TARGETS;
    if (count < 0 || !t) count = 0;
    if (count) memcpy(SHX.tg, t, sizeof(arc_shot_target) * (size_t)count);
    SHX.ntg = count;
}
int arc_shots_fx(const arc_fx *fx) {
    for (int i = 0; i < SHX.nfx; i++) if (SHX.fx[i] == fx) return i + 1;
    if (!fx || SHX.nfx >= 64) return 0;
    SHX.fx[SHX.nfx++] = fx; return SHX.nfx;
}
static void shots_touch(void) { if (!SHX.touched) { SHX.touched = true; SH.magic = SHOTS_MAGIC; if (!SH.next_id) SH.next_id = 1; } }
/* the nearest hit of a ray / sphere from o along unit d within maxt */
static bool shot_walk(arc_v3 o, arc_v3 d, float maxt, float radius, uint32_t mask, int owner, arc_shot_hit *out) {
    if (!mask) mask = 0xFFFFFFFFu;
    sweep_world w = SHX.w; w.phys_mask = mask;
    sweep_hit h = sweep(&w, o, d, maxt, radius, owner, 7);
    int tid = -1; float tt = h.hit ? h.t : maxt; arc_v3 tn = { 0, 0, 0 };
    for (int i = 0; i < SHX.ntg; i++) {
        const arc_shot_target *g = &SHX.tg[i];
        if (owner && g->id == owner) continue;
        if (!((g->layer ? g->layer : 1u) & mask)) continue;
        float t = sw_capsule_t(o, d, g->a, g->b, g->radius + radius);
        if (t >= 0 && t <= tt && (tid < 0 || t < tt)) {
            tt = t; tid = i;
            arc_v3 p = arc_v3_make(o.x + d.x * t, o.y + d.y * t, o.z + d.z * t), ab = arc_v3_sub(g->b, g->a);
            float L2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z, s = 0;
            if (L2 > 1e-10f) { s = ((p.x - g->a.x) * ab.x + (p.y - g->a.y) * ab.y + (p.z - g->a.z) * ab.z) / L2; s = s < 0 ? 0 : s > 1 ? 1 : s; }
            tn = sw_norm(arc_v3_sub(p, arc_v3_make(g->a.x + ab.x * s, g->a.y + ab.y * s, g->a.z + ab.z * s)));
            if (tn.x == 0 && tn.y == 0 && tn.z == 0) tn = arc_v3_make(-d.x, -d.y, -d.z);
        }
    }
    memset(out, 0, sizeof *out);
    if (tid >= 0) {
        out->kind = ARC_HIT_TARGET; out->id = SHX.tg[tid].id; out->dist = tt;
        out->point = arc_v3_make(o.x + d.x * tt, o.y + d.y * tt, o.z + d.z * tt); out->normal = tn;
        if (radius > 0) out->point = arc_v3_sub(out->point, arc_v3_scale(tn, radius));   /* the surface, not the sphere's centre */
        return true;
    }
    if (!h.hit) return false;
    out->kind = h.kind; out->id = h.id; out->cell_x = h.cx; out->cell_z = h.cz; out->dist = h.t; out->normal = h.normal;
    out->point = radius > 0 ? arc_v3_sub(h.point, arc_v3_scale(h.normal, radius)) : h.point;
    return true;
}
static void shot_flash(arc_v3 pos, uint32_t rgb, float power, float life) {
    flash_rec *f = &SH.fl[SH.fl_next]; SH.fl_next = (SH.fl_next + 1) % SHOT_FLASHES;
    f->pos = pos; f->rgb = rgb ? rgb : 0xFFE8C0; f->power = power; f->life = life; f->age = 0;
}
static void shot_tracer(arc_v3 a, arc_v3 b, uint32_t rgb, float width, float life) {
    tracer_rec *t = &SH.tr[SH.tr_next]; SH.tr_next = (SH.tr_next + 1) % SHOT_TRACERS;
    t->a = a; t->b = b; t->rgb = rgb; t->width = width; t->life = life > 0 ? life : 0.08f; t->age = 0;
}
static void shot_hit(const shot_rec *s, const arc_shot_hit *h0) {
    uint64_t now = arc_tick_count();
    if (now != SH.hits_tick) { SH.nhits = 0; SH.hits_tick = now; }
    arc_shot_hit h = *h0; h.shot = s->id; h.owner = s->owner; h.user = s->user; h.dist += s->travelled;
    if (SH.nhits < ARC_SHOTS_HITS) SH.hits[SH.nhits++] = h;
    /* looks */
    if (s->decal_size > 0 && h.kind != ARC_HIT_TARGET) {
        float ang = (float)(((uint32_t)s->id * 2654435761u) >> 20) * (6.2831853f / 4096.0f);
        arc_decal_add((arc_texture){ s->decal_tex }, arc_v3_add(h.point, arc_v3_scale(h.normal, 0.005f)), h.normal, s->decal_size, ang, s->decal_argb ? s->decal_argb : 0x30100C08u, 0);
    }
    if (s->fx > 0 && s->fx <= SHX.nfx) arc_fx_burst(SHX.fx[s->fx - 1], h.point, h.normal, 0);
    if (s->sound) arc_play_3d((arc_sound){ s->sound }, h.point, s->volume > 0 ? s->volume : 1.0f);
    if (s->flash > 0) shot_flash(arc_v3_add(h.point, arc_v3_scale(h.normal, 0.2f)), s->flash_rgb, s->flash, 0.12f);
}
static void shot_from_desc(shot_rec *s, const arc_shot_desc *d) {
    memset(s, 0, sizeof *s);
    s->pos = s->start = d->from; s->dir = sw_norm(d->dir); s->speed = d->speed; s->left = d->range > 0 ? d->range : 100.0f;
    s->radius = d->radius > 0 ? d->radius : 0; s->mask = d->mask; s->owner = d->owner; s->user = d->user;
    s->tracer_rgb = d->tracer_rgb; s->tracer_width = d->tracer_width; s->tracer_life = d->tracer_life; s->size = d->projectile_size > 0 ? d->projectile_size : 0.4f;
    s->decal_tex = d->impact_decal.id; s->decal_size = d->decal_size; s->decal_argb = d->decal_argb;
    s->fx = d->impact_fx; s->sound = d->impact_sound.id; s->volume = d->sound_volume; s->flash = d->flash_light; s->flash_rgb = d->flash_rgb;
}
bool arc_shot_cast(const arc_shot_desc *d, arc_shot_hit *out) {
    arc_shot_hit tmp; if (!out) out = &tmp;
    arc_v3 dir = sw_norm(d->dir); memset(out, 0, sizeof *out);
    if (dir.x == 0 && dir.y == 0 && dir.z == 0) return false;
    return shot_walk(d->from, dir, d->range > 0 ? d->range : 100.0f, d->radius, d->mask, d->owner, out);
}
int arc_shot_fire(const arc_shot_desc *d) {
    if (!d) return 0;
    shots_touch();
    shot_rec s; shot_from_desc(&s, d);
    if (s.dir.x == 0 && s.dir.y == 0 && s.dir.z == 0) return 0;
    s.id = SH.next_id++; if (SH.next_id <= 0) SH.next_id = 1;
    if (s.flash > 0) shot_flash(arc_v3_add(s.pos, arc_v3_scale(s.dir, 0.3f)), s.flash_rgb, s.flash, 0.06f);
    if (s.speed <= 0) {   /* hitscan */
        arc_shot_hit h; bool hit = shot_walk(s.pos, s.dir, s.left, s.radius, s.mask, s.owner, &h);
        arc_v3 end = hit ? h.point : arc_v3_add(s.pos, arc_v3_scale(s.dir, s.left));
        if (s.tracer_rgb) shot_tracer(s.pos, end, s.tracer_rgb, s.tracer_width, s.tracer_life);
        if (hit) shot_hit(&s, &h);
        return s.id;
    }
    for (int i = 0; i < ARC_SHOTS_MAX; i++) if (!SH.p[i].used) { s.used = true; SH.p[i] = s; return s.id; }
    return 0;   /* full */
}
void arc_shots_update(float dt) {
    if (!SHX.touched) return;
    for (int i = 0; i < ARC_SHOTS_MAX; i++) {
        shot_rec *s = &SH.p[i];
        if (!s->used) continue;
        float step = s->speed * dt; if (step > s->left) step = s->left;
        arc_shot_hit h;
        if (shot_walk(s->pos, s->dir, step, s->radius, s->mask, s->owner, &h)) {
            if (s->tracer_rgb && s->tracer_width <= 0 && s->tracer_life > 0) shot_tracer(s->pos, h.point, s->tracer_rgb, 0, s->tracer_life);
            shot_hit(s, &h); s->used = false; continue;
        }
        s->pos = arc_v3_add(s->pos, arc_v3_scale(s->dir, step)); s->left -= step; s->travelled += step;
        if (s->left <= 1e-6f) s->used = false;
    }
    for (int i = 0; i < SHOT_TRACERS; i++) if (SH.tr[i].life > 0) { SH.tr[i].age += dt; if (SH.tr[i].age >= SH.tr[i].life) SH.tr[i].life = 0; }
    for (int i = 0; i < SHOT_FLASHES; i++) if (SH.fl[i].life > 0) { SH.fl[i].age += dt; if (SH.fl[i].age >= SH.fl[i].life) SH.fl[i].life = 0; }
}
int arc_shots_hits(arc_shot_hit *out, int max) {
    if (SH.hits_tick != arc_tick_count()) return 0;
    int n = SH.nhits < max ? SH.nhits : max;
    if (out && n > 0) memcpy(out, SH.hits, sizeof(arc_shot_hit) * (size_t)n);
    return out ? n : SH.nhits;
}
int arc_shots_in_flight(void) { int n = 0; for (int i = 0; i < ARC_SHOTS_MAX; i++) n += SH.p[i].used; return n; }
static int shots_tracers_live(void) { int n = 0; for (int i = 0; i < SHOT_TRACERS; i++) n += SH.tr[i].life > 0; return n; }
void arc_shots_clear(void) { int id = SH.next_id; memset(&SH, 0, sizeof SH); SH.magic = SHOTS_MAGIC; SH.next_id = id > 0 ? id : 1; }
int arc_shots_save(void *buf, int max) {
    int n = (int)shots_saved_size();
    if (buf && max >= n) { SH.magic = SHOTS_MAGIC; memcpy(buf, &SH, (size_t)n); }
    return n;
}
bool arc_shots_load(const void *buf, int size) {
    uint32_t m = 0;
    if (!buf || size != (int)shots_saved_size()) return false;
    memcpy(&m, buf, 4); if (m != SHOTS_MAGIC) return false;
    memcpy(&SH, buf, (size_t)size); SHX.touched = true; return true;
}
/* engine state (rewinds, state_save): only once shots were used */
size_t arc__shots_state(arc_writer *w) {
    if (!SHX.touched) return w->len;
    SH.magic = SHOTS_MAGIC; arc_write(w, &SH, shots_saved_size()); return w->len;
}
bool arc__shots_state_read(arc_reader *r, bool commit) {
    uint32_t m = 0; size_t n = shots_saved_size();
    if (r->pos + 4 <= r->size) memcpy(&m, r->data + r->pos, 4);
    if (r->pos >= r->size || m != SHOTS_MAGIC) { if (commit && SHX.touched) arc_shots_clear(); return true; }
    if (r->size - r->pos < n) { r->fail = true; return false; }
    if (!commit) { r->pos += n; return true; }
    arc_read(r, &SH, n); SHX.touched = true; return true;
}
static void shot_streak(arc_v3 a, arc_v3 b, float width, uint32_t rgb, float alpha) {
    arc_v3 d = arc_v3_sub(b, a); float L = sw_len(d);
    if (L < 1e-5f) return;
    if (width <= 0) { arc_draw_line(a, b, rgb); return; }
    if (!SHX.streak.id) { SHX.streak = arc_mesh_cube(0xFFFFFF); arc_mesh_set_cast_shadow(SHX.streak, false); arc_mesh_set_name(SHX.streak, "arc_shot_streak"); }
    arc_v3 z = arc_v3_scale(d, 1.0f / L), up = (z.y > 0.9f || z.y < -0.9f) ? arc_v3_make(1, 0, 0) : arc_v3_make(0, 1, 0);
    arc_v3 x = arc_v3_norm(arc_v3_cross(up, z)), y = arc_v3_cross(z, x);
    arc_m4 m = arc_m4_identity();
    m.m[0] = x.x * width; m.m[1] = x.y * width; m.m[2] = x.z * width;
    m.m[4] = y.x * width; m.m[5] = y.y * width; m.m[6] = y.z * width;
    m.m[8] = z.x * L; m.m[9] = z.y * L; m.m[10] = z.z * L;
    m.m[12] = (a.x + b.x) * 0.5f; m.m[13] = (a.y + b.y) * 0.5f; m.m[14] = (a.z + b.z) * 0.5f;
    arc_mesh_style st; memset(&st, 0, sizeof st);
    st.tint = rgb; st.emissive = 1.0f; st.additive = true; st.transparency = 1.0f - alpha;
    arc_draw_mesh_styled(SHX.streak, m, &st);
}
void arc_shots_draw(void) {
    if (!SHX.touched) return;
    for (int i = 0; i < SHOT_TRACERS; i++) {
        const tracer_rec *t = &SH.tr[i]; if (t->life <= 0) continue;
        float a = 1.0f - t->age / t->life; shot_streak(t->a, t->b, t->width, t->rgb, a < 0 ? 0 : a);
    }
    for (int i = 0; i < ARC_SHOTS_MAX; i++) {
        const shot_rec *s = &SH.p[i]; if (!s->used) continue;
        float len = s->size < s->travelled + 0.001f ? s->size : s->travelled + 0.001f;
        uint32_t rgb = s->tracer_rgb ? s->tracer_rgb : 0xFFD080;
        shot_streak(arc_v3_sub(s->pos, arc_v3_scale(s->dir, len)), s->pos, s->tracer_width > 0 ? s->tracer_width : (s->radius > 0 ? s->radius * 2.0f : 0.05f), rgb, 1.0f);
        if (s->flash > 0) arc_light_point(s->pos, s->flash_rgb ? s->flash_rgb : rgb, s->flash, 4.0f);
    }
    for (int i = 0; i < SHOT_FLASHES; i++) {
        const flash_rec *f = &SH.fl[i]; if (f->life <= 0) continue;
        arc_light_point(f->pos, f->rgb, f->power * (1.0f - f->age / f->life), 5.0f);
    }
}
void arc__shots_json(arc_json *j) {   /* in `frame` */
    if (!SHX.touched) return;
    arc_json_obj_k(j, "shots");
    arc_json_int_k(j, "projectiles", arc_shots_in_flight()); arc_json_int_k(j, "tracers", shots_tracers_live());
    arc_json_arr_k(j, "tracer_lines");
    int k = 0;
    for (int i = 0; i < SHOT_TRACERS && k < 16; i++) {
        const tracer_rec *t = &SH.tr[i]; if (t->life <= 0) continue; k++;
        arc_json_arr(j); arc_json_num(j, t->a.x); arc_json_num(j, t->a.y); arc_json_num(j, t->a.z); arc_json_num(j, t->b.x); arc_json_num(j, t->b.y); arc_json_num(j, t->b.z); arc_json_end(j);
    }
    arc_json_end(j);
    arc_json_int_k(j, "decals", arc_decal_count());
    arc_json_end(j);
}
