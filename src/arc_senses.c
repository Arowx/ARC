/* ARC 0.10.0 senses: sight with a view cone and build-up, hearing with muffling, memory, alerts, search points.
   Plain data in the game's state; deterministic maths (arc_sqrtf, arc_cosf). */

void arc_senses_init(arc_senses *s) { if (s) memset(s, 0, sizeof *s); }
static void senses_clear_mem(arc_senses_agent *a) { for (int k = 0; k < ARC_SENSES_TARGETS; k++) { memset(&a->mem[k], 0, sizeof a->mem[k]); a->mem[k].target = -1; } }
int arc_senses_add(arc_senses *s, arc_v3 pos, arc_v3 facing) {
    if (!s) return -1;
    for (int i = 0; i < ARC_SENSES_AGENTS; i++) if (!s->agent[i].used) {
        arc_senses_agent *a = &s->agent[i]; memset(a, 0, sizeof *a);
        a->used = true; a->pos = pos; a->facing = facing; a->eye_height = 1.0f;
        a->half_angle = 0.8f; a->range = 10.0f; a->build_up = 0.5f; a->forget = 10.0f; a->hearing = 1.0f; a->muffle = 0.5f;
        senses_clear_mem(a);
        if (i + 1 > s->agent_count) s->agent_count = i + 1;
        return i;
    }
    return -1;
}
void arc_senses_remove(arc_senses *s, int i) {
    if (!s || i < 0 || i >= ARC_SENSES_AGENTS) return;
    memset(&s->agent[i], 0, sizeof s->agent[i]);
    while (s->agent_count > 0 && !s->agent[s->agent_count - 1].used) s->agent_count--;
}
void arc_senses_set(arc_senses *s, int i, arc_v3 pos, arc_v3 facing) {
    if (!s || i < 0 || i >= ARC_SENSES_AGENTS || !s->agent[i].used) return;
    s->agent[i].pos = pos; s->agent[i].facing = facing;
}
void arc_senses_noise(arc_senses *s, arc_v3 pos, float radius, int kind, float loudness, int source) {
    if (!s || s->noise_count >= ARC_SENSES_NOISES) return;
    int n = s->noise_count++;
    s->noise[n].pos = pos; s->noise[n].radius = radius; s->noise[n].loudness = loudness > 0 ? loudness : 1.0f; s->noise[n].kind = kind; s->noise[n].source = source;
}
static arc_senses_memory *senses_slot(arc_senses_agent *a, int target, bool make) {
    for (int k = 0; k < ARC_SENSES_TARGETS; k++) if (a->mem[k].target == target) return &a->mem[k];
    if (!make) return NULL;
    int pick = -1;
    for (int k = 0; k < ARC_SENSES_TARGETS && pick < 0; k++) if (a->mem[k].target < 0) pick = k;
    if (pick < 0) for (int k = 0; k < ARC_SENSES_TARGETS; k++) if (!a->mem[k].seen_now && (pick < 0 || a->mem[k].when < a->mem[pick].when)) pick = k;   /* the oldest news */
    if (pick < 0) return NULL;
    arc_senses_memory *m = &a->mem[pick]; memset(m, 0, sizeof *m); m->target = target; return m;
}
static float sn_len(arc_v3 v) { return arc_sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }
void arc_senses_update(arc_senses *s, float dt, const arc_senses_target *targets, int count, arc_senses_los_fn los, void *user) {
    if (!s) return;
    s->time += dt;
    for (int i = 0; i < s->agent_count; i++) {
        arc_senses_agent *a = &s->agent[i];
        if (!a->used) continue;
        arc_v3 eye = arc_v3_make(a->pos.x, a->pos.y + a->eye_height, a->pos.z);
        float fl = sn_len(a->facing); arc_v3 f = fl > 1e-6f ? arc_v3_scale(a->facing, 1.0f / fl) : arc_v3_make(0, 0, 0);
        float cosh = arc_cosf(a->half_angle);
        for (int t = 0; t < count && targets; t++) {
            const arc_senses_target *g = &targets[t];
            if (a->team && g->team == a->team) continue;
            int np = g->npoints < 1 ? 1 : g->npoints > ARC_SENSES_POINTS ? ARC_SENSES_POINTS : g->npoints;
            arc_v3 to = arc_v3_sub(g->points[0], eye); float dist = sn_len(to);
            bool in_view = dist <= a->range && (fl <= 1e-6f || dist < 1e-6f || (to.x * f.x + to.y * f.y + to.z * f.z) >= cosh * dist);
            float frac = 0;
            if (in_view) { int clear = 0; for (int p = 0; p < np; p++) clear += !los || los(eye, g->points[p], user); frac = (float)clear / (float)np; }
            arc_senses_memory *m = senses_slot(a, g->id, frac > 0);
            if (!m) continue;
            if (frac > 0) { m->awareness = a->build_up <= 0 ? 1.0f : m->awareness + dt * frac / a->build_up; if (m->how == ARC_KNOW_NONE) m->pos = g->points[0]; }   /* (a glimpse: where) */
            else m->awareness = a->build_up <= 0 ? 0.0f : m->awareness - dt / a->build_up;
            if (m->awareness > 1) m->awareness = 1;
            if (m->awareness < 0) m->awareness = 0;
            m->seen_now = frac > 0 && m->awareness >= 1.0f;
            if (m->seen_now) { m->pos = g->points[0]; m->when = s->time; m->how = ARC_KNOW_SEEN; }
        }
        /* targets that left the list are out of sight too */
        for (int k = 0; k < ARC_SENSES_TARGETS; k++) {
            arc_senses_memory *m = &a->mem[k]; if (m->target < 0) continue;
            bool listed = false; for (int t = 0; t < count && targets && !listed; t++) listed = targets[t].id == m->target;
            if (!listed) { m->seen_now = false; m->awareness = a->build_up <= 0 ? 0.0f : m->awareness - dt / a->build_up; if (m->awareness < 0) m->awareness = 0; }
        }
        /* noises */
        for (int n = 0; n < s->noise_count; n++) {
            float r = s->noise[n].radius * s->noise[n].loudness * a->hearing;
            arc_v3 d = arc_v3_sub(s->noise[n].pos, eye); float dist = sn_len(d);
            if (dist > r) continue;
            if (los && !los(s->noise[n].pos, eye, user)) { r *= a->muffle; if (dist > r) continue; }
            a->heard_noise = true; a->noise_pos = s->noise[n].pos; a->noise_kind = s->noise[n].kind; a->noise_when = s->time;
            if (s->noise[n].source >= 0) {
                arc_senses_memory *m = senses_slot(a, s->noise[n].source, true);
                if (m && !m->seen_now) { m->pos = s->noise[n].pos; m->when = s->time; m->how = ARC_KNOW_HEARD; }
            }
        }
        /* forgetting */
        for (int k = 0; k < ARC_SENSES_TARGETS; k++) {
            arc_senses_memory *m = &a->mem[k]; if (m->target < 0 || m->seen_now || m->awareness > 0) continue;
            if (m->how == ARC_KNOW_NONE || s->time - m->when > a->forget) { memset(m, 0, sizeof *m); m->target = -1; }
        }
    }
    s->noise_count = 0;
}
const arc_senses_memory *arc_senses_knows(const arc_senses *s, int i, int target) {
    if (!s || i < 0 || i >= ARC_SENSES_AGENTS || !s->agent[i].used) return NULL;
    for (int k = 0; k < ARC_SENSES_TARGETS; k++) if (s->agent[i].mem[k].target == target && s->agent[i].mem[k].how != ARC_KNOW_NONE) return &s->agent[i].mem[k];
    return NULL;
}
void arc_senses_forget(arc_senses *s, int i, int target) {
    if (!s || i < 0 || i >= ARC_SENSES_AGENTS) return;
    for (int k = 0; k < ARC_SENSES_TARGETS; k++) if (s->agent[i].mem[k].target == target) { memset(&s->agent[i].mem[k], 0, sizeof s->agent[i].mem[k]); s->agent[i].mem[k].target = -1; }
}
int arc_senses_alert(arc_senses *s, int i, float radius) {
    if (!s || i < 0 || i >= ARC_SENSES_AGENTS || !s->agent[i].used) return 0;
    const arc_senses_agent *a = &s->agent[i]; int told = 0;
    for (int j = 0; j < s->agent_count; j++) {
        arc_senses_agent *b = &s->agent[j];
        if (j == i || !b->used || sn_len(arc_v3_sub(b->pos, a->pos)) > radius) continue;
        bool any = false;
        for (int k = 0; k < ARC_SENSES_TARGETS; k++) {
            const arc_senses_memory *m = &a->mem[k]; if (m->target < 0 || m->how == ARC_KNOW_NONE) continue;
            arc_senses_memory *bm = senses_slot(b, m->target, true);
            if (!bm || bm->seen_now || (bm->how != ARC_KNOW_NONE && bm->when >= m->when)) continue;
            bm->pos = m->pos; bm->when = m->when; bm->how = ARC_KNOW_TOLD; any = true;
        }
        told += any;
    }
    return told;
}
int arc_senses_search_points(arc_navmesh nm, arc_v3 center, float radius, int n, arc_v3 *out) {
    if (!out || n <= 0 || !nm.id) return 0;
    int k = 0; arc_v3 q;
    if (arc_navmesh_nearest(nm, center, radius > 0 ? radius : 1.0f, &q)) out[k++] = q;
    for (int i = 1; k < n && i < n * 4; i++) {
        float ang = (float)i * 2.39996323f, r = radius * arc_sqrtf((float)i / (float)(n * 2));
        if (r > radius) r = radius;
        arc_v3 p = arc_v3_make(center.x + arc_cosf(ang) * r, center.y, center.z + arc_sinf(ang) * r);
        if (!arc_navmesh_nearest(nm, p, radius * 0.5f + 0.5f, &q)) continue;
        bool dup = false;
        for (int j = 0; j < k && !dup; j++) dup = sn_len(arc_v3_sub(out[j], q)) < radius * 0.15f;
        if (!dup) out[k++] = q;
    }
    return k;
}
bool arc_senses_los_cworld(arc_v3 from, arc_v3 to, void *cw) {
    if (!cw) return true;
    arc_v3 d = arc_v3_sub(to, from); float L = sn_len(d); if (L < 1e-6f) return true;
    arc_hit h; return !(arc_cworld_raycast(*(arc_cworld *)cw, from, arc_v3_scale(d, 1.0f / L), L - 0.01f, &h) && h.hit);
}
bool arc_senses_los_physics(arc_v3 from, arc_v3 to, void *phys) {
    if (!phys) return true;
    arc_v3 d = arc_v3_sub(to, from); float L = sn_len(d); if (L < 0.06f) return true;
    arc_physics_hit h; return !arc_physics_raycast(*(arc_physics *)phys, from, arc_v3_scale(d, 1.0f / L), L - 0.05f, 0xFFFFFFFFu, &h);
}
void arc_senses_draw(const arc_senses *s) {
    if (!s) return;
    for (int i = 0; i < s->agent_count; i++) {
        const arc_senses_agent *a = &s->agent[i]; if (!a->used) continue;
        arc_v3 eye = arc_v3_make(a->pos.x, a->pos.y + a->eye_height, a->pos.z);
        bool alarmed = false; for (int k = 0; k < ARC_SENSES_TARGETS; k++) alarmed |= a->mem[k].seen_now;
        uint32_t rgb = alarmed ? 0xFF4030 : 0x60C0FF;
        float yaw = atan2f(a->facing.x, a->facing.z), prev_x = 0, prev_z = 0;
        for (int k = 0; k <= 8; k++) {
            float ang = yaw - a->half_angle + 2.0f * a->half_angle * (float)k / 8.0f;
            float x = eye.x + sinf(ang) * a->range, z = eye.z + cosf(ang) * a->range;
            if (k == 0 || k == 8) arc_draw_line(eye, arc_v3_make(x, eye.y, z), rgb);
            if (k > 0) arc_draw_line(arc_v3_make(prev_x, eye.y, prev_z), arc_v3_make(x, eye.y, z), rgb);
            prev_x = x; prev_z = z;
        }
        for (int k = 0; k < ARC_SENSES_TARGETS; k++) {
            const arc_senses_memory *m = &a->mem[k]; if (m->target < 0 || m->how == ARC_KNOW_NONE) continue;
            uint32_t c = m->seen_now ? 0xFF4030 : m->how == ARC_KNOW_HEARD ? 0xFFC040 : m->how == ARC_KNOW_TOLD ? 0xC080FF : 0xFF9080;
            arc_draw_line(eye, m->pos, c);
            arc_draw_line(arc_v3_make(m->pos.x - 0.3f, m->pos.y, m->pos.z), arc_v3_make(m->pos.x + 0.3f, m->pos.y, m->pos.z), c);
            arc_draw_line(arc_v3_make(m->pos.x, m->pos.y, m->pos.z - 0.3f), arc_v3_make(m->pos.x, m->pos.y, m->pos.z + 0.3f), c);
        }
        if (a->heard_noise && s->time - a->noise_when < 1.0f) arc_draw_line(eye, a->noise_pos, 0xFFE060);
    }
}
static struct { const arc_senses *s; char name[32]; } SNW[4];
void arc_senses_watch(const arc_senses *s, const char *name) {
    const char *nm = name ? name : "senses";
    for (int k = 0; k < 4; k++) if (SNW[k].s && !strcmp(SNW[k].name, nm)) { SNW[k].s = s; if (!s) SNW[k].name[0] = 0; return; }
    if (!s) return;
    for (int k = 0; k < 4; k++) if (!SNW[k].s) { SNW[k].s = s; snprintf(SNW[k].name, sizeof SNW[k].name, "%s", nm); return; }
}
void arc__senses_dump_json(arc_json *j) {
    bool any = false; for (int k = 0; k < 4; k++) any |= SNW[k].s != NULL;
    if (!any) return;
    static const char *HOW[] = { "none", "seen", "heard", "told" };
    arc_json_obj_k(j, "senses");
    for (int w = 0; w < 4; w++) {
        const arc_senses *s = SNW[w].s; if (!s) continue;
        arc_json_arr_k(j, SNW[w].name);
        for (int i = 0; i < s->agent_count; i++) {
            const arc_senses_agent *a = &s->agent[i]; if (!a->used) continue;
            arc_json_obj(j); arc_json_int_k(j, "agent", i);
            arc_json_arr_k(j, "knows");
            for (int k = 0; k < ARC_SENSES_TARGETS; k++) {
                const arc_senses_memory *m = &a->mem[k]; if (m->target < 0) continue;
                arc_json_obj(j); arc_json_int_k(j, "target", m->target); arc_json_str_k(j, "how", HOW[m->how & 3]);
                arc_json_num_k(j, "awareness", m->awareness); arc_json_bool_k(j, "seen_now", m->seen_now);
                arc_json_arr_k(j, "pos"); arc_json_num(j, m->pos.x); arc_json_num(j, m->pos.y); arc_json_num(j, m->pos.z); arc_json_end(j);
                arc_json_num_k(j, "age", s->time - m->when); arc_json_end(j);
            }
            arc_json_end(j);
            if (a->heard_noise) arc_json_num_k(j, "noise_age", s->time - a->noise_when);
            arc_json_end(j);
        }
        arc_json_end(j);
    }
    arc_json_end(j);
}
