/* ARC 0.9.9 ground markers (MechBattleArc's request): tile highlights, rings, discs, arrows, chevrons and diamonds drawn
   on the ground in one call each. Each marker is built on the CPU (fill triangles subdivided so they follow uneven
   ground, edges as quads a set number of screen pixels wide, dashes, pulse) into pooled dynamic meshes: consecutive
   markers in one pass share a mesh and a draw. They are unlit, see-through, depth-tested without writing depth and
   pulled a little toward the camera, so they never z-fight the ground; `on_top` adds a faint copy where something
   covers them (units standing on them). Labels are 2D text centred above them, so `frame.texts` lists them, and the
   markers themselves are listed in `frame.markers`. Names start with mk_ (one translation unit). */
#include "arc_internal.h"

#define MK_POOL 64
#define MK_CAP 24576                      /* vertices per pooled mesh (triangle list) */
typedef struct { uint32_t mesh; } mk_slot;
static mk_slot mk_pool[MK_POOL]; static int mk_used; static uint64_t mk_frame = (uint64_t)-1;
static arc_vertex mk_v[2][MK_CAP]; static int mk_n[2];       /* [0] normal, [1] the faint copy shown through things */
static int mk_pass = -1; static arc_v3 mk_centre[2]; static int mk_count[2];
/* frame recording */
typedef struct { int shape; float pos[3], size; uint32_t rgb, tgt; char label[48]; } mk_rec;
static mk_rec mk_recs[256]; static int mk_nrec; static uint64_t mk_rec_frame = (uint64_t)-1;
int arc__gfx_pass_index(void);
void arc__draw_marker_mesh(uint32_t mesh, int depth, arc_v3 centre);
bool arc__frame_recording(void);
arc_v3 arc__camera_axis(int i);
extern uint64_t arc__gfx_frame;

static uint32_t mk_mesh(void) {
    if (mk_frame != arc__gfx_frame) { mk_frame = arc__gfx_frame; mk_used = 0; }
    if (mk_used >= MK_POOL) return 0;
    mk_slot *s = &mk_pool[mk_used++];
    if (!s->mesh) {
        static arc_vertex zero[MK_CAP]; static uint32_t idx[MK_CAP];
        for (int i = 0; i < MK_CAP; i++) idx[i] = (uint32_t)i;
        arc_mesh m = arc_mesh_create(zero, MK_CAP, idx, MK_CAP);
        arc_mesh_set_name(m, "marker");
        s->mesh = m.id;
    }
    return s->mesh;
}
/* the batch so far goes into this pass as one draw per kind */
void arc__markers_flush(void) {
    for (int k = 0; k < 2; k++) {
        if (!mk_n[k]) continue;
        uint32_t id = mk_mesh();
        if (id) {
            memset(mk_v[k] + mk_n[k], 0, sizeof(arc_vertex) * (size_t)(MK_CAP - mk_n[k]));   /* the rest: degenerate */
            arc_mesh m = { id }; arc_mesh_update(m, mk_v[k], MK_CAP);
            arc_v3 c = arc_v3_scale(mk_centre[k], 1.0f / (float)arc_maxi(mk_count[k], 1));
            arc__draw_marker_mesh(id, k ? ARC_DEPTH_HIDDEN : ARC_DEPTH_TEST_ONLY, c);
        }
        mk_n[k] = 0; mk_count[k] = 0; mk_centre[k] = arc_v3_make(0, 0, 0);
    }
    mk_pass = -1;
}

typedef struct { const arc_marker *m; float lift; } mk_ground;
static float mk_y(const mk_ground *g, float x, float z) {
    const arc_marker *m = g->m; float y = m->y;
    if (m->height) y = m->height(m->height_ud, x, z);
    else if (m->terrain.id) y = arc_terrain_height(m->terrain, x, z);
    return y + g->lift;
}
static void mk_vert(int k, arc_v3 p, uint32_t rgb, float alpha) {
    if (mk_n[k] >= MK_CAP) return;
    arc_vertex *v = &mk_v[k][mk_n[k]++];
    memset(v, 0, sizeof *v);
    v->px = p.x; v->py = p.y; v->pz = p.z; v->ny = 1;
    v->color = (arc_vcol(rgb) & 0x00FFFFFFu) | ((uint32_t)(arc_clampf(alpha, 0, 1) * 255.0f + 0.5f) << 24);
}
/* a ground triangle given in x, z: split until its edges are short, so it follows the ground */
static void mk_tri(int k, const mk_ground *g, float ax, float az, float bx, float bz, float cx, float cz, uint32_t rgb, float alpha, int depth) {
    float lab = (ax - bx) * (ax - bx) + (az - bz) * (az - bz), lbc = (bx - cx) * (bx - cx) + (bz - cz) * (bz - cz), lca = (cx - ax) * (cx - ax) + (cz - az) * (cz - az);
    float mx = arc_maxf(lab, arc_maxf(lbc, lca));
    if (depth < 6 && mx > 0.35f * 0.35f && (g->m->height || g->m->terrain.id)) {
        if (mx == lab) { float px = (ax + bx) * 0.5f, pz = (az + bz) * 0.5f; mk_tri(k, g, ax, az, px, pz, cx, cz, rgb, alpha, depth + 1); mk_tri(k, g, px, pz, bx, bz, cx, cz, rgb, alpha, depth + 1); }
        else if (mx == lbc) { float px = (bx + cx) * 0.5f, pz = (bz + cz) * 0.5f; mk_tri(k, g, ax, az, bx, bz, px, pz, rgb, alpha, depth + 1); mk_tri(k, g, ax, az, px, pz, cx, cz, rgb, alpha, depth + 1); }
        else { float px = (cx + ax) * 0.5f, pz = (cz + az) * 0.5f; mk_tri(k, g, ax, az, bx, bz, px, pz, rgb, alpha, depth + 1); mk_tri(k, g, px, pz, bx, bz, cx, cz, rgb, alpha, depth + 1); }
        return;
    }
    if ((bx - ax) * (cz - az) - (bz - az) * (cx - ax) > 0) { float t = bx; bx = cx; cx = t; t = bz; bz = cz; cz = t; }   /* face up (+y) */
    mk_vert(k, arc_v3_make(ax, mk_y(g, ax, az), az), rgb, alpha);
    mk_vert(k, arc_v3_make(bx, mk_y(g, bx, bz), bz), rgb, alpha);
    mk_vert(k, arc_v3_make(cx, mk_y(g, cx, cz), cz), rgb, alpha);
}
/* an edge along a closed (or open) outline, `w` world units wide, centred on it; dashes of `dash` on, `dash` off */
static void mk_edge(int k, const mk_ground *g, const float *pts, int n, bool closed, float w, float dash, uint32_t rgb, float alpha) {
    float run = 0, h = w * 0.5f;
    int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; i++) {
        float ax = pts[2 * i], az = pts[2 * i + 1], bx = pts[2 * ((i + 1) % n)], bz = pts[2 * ((i + 1) % n) + 1];
        float dx = bx - ax, dz = bz - az, len = sqrtf(dx * dx + dz * dz); if (len < 1e-6f) continue;
        float nx = -dz / len * h, nz = dx / len * h;
        int pieces = (int)ceilf(len / 0.3f); if (pieces < 1) pieces = 1;
        for (int p = 0; p < pieces; p++) {
            float t0 = (float)p / (float)pieces, t1 = (float)(p + 1) / (float)pieces;
            if (dash > 0) {   /* clip the piece to the "on" parts of the dash pattern */
                float s0 = run + t0 * len, s1 = run + t1 * len, mid = (s0 + s1) * 0.5f;
                if (fmodf(mid, 2 * dash) >= dash) continue;
            }
            float x0 = ax + dx * t0, z0 = az + dz * t0, x1 = ax + dx * t1, z1 = az + dz * t1;
            mk_tri(k, g, x0 - nx, z0 - nz, x1 - nx, z1 - nz, x1 + nx, z1 + nz, rgb, alpha, 6);
            mk_tri(k, g, x0 - nx, z0 - nz, x1 + nx, z1 + nz, x0 + nx, z0 + nz, rgb, alpha, 6);
        }
        run += len;
    }
}
static void mk_fan(int k, const mk_ground *g, float cx, float cz, const float *pts, int n, uint32_t rgb, float alpha) {
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; mk_tri(k, g, cx, cz, pts[2 * i], pts[2 * i + 1], pts[2 * j], pts[2 * j + 1], rgb, alpha, 0); }
}
/* one copy of the marker into batch k (0 normal, 1 the faint see-through copy) */
static void mk_build(int k, const arc_marker *m, const mk_ground *g, float fill_a, float edge_a, float edge_w) {
    float s = m->size > 0 ? m->size : 1.0f, c = arc_cosf(m->yaw), sn = arc_sinf(m->yaw);
    float pts[2 * 72], in[2 * 72]; int n = 0;
    #define MK_PUT(arr, lx, lz) do { float LX = (lx), LZ = (lz); arr[2 * n] = m->x + LX * c + LZ * sn; arr[2 * n + 1] = m->z - LX * sn + LZ * c;   /* as arc_m4_rotate_y */ } while (0)
    switch (m->shape) {
    case ARC_MARK_RING: case ARC_MARK_DISC: {
        float ro = s * 0.5f, ri = m->shape == ARC_MARK_RING ? ro * (m->inner > 0 ? arc_minf(m->inner, 0.98f) : 0.8f) : 0;
        int seg = ro > 2 ? 64 : 40;
        for (n = 0; n < seg; n++) { float a = (float)n * 6.2831853f / (float)seg; MK_PUT(pts, arc_cosf(a) * ro, arc_sinf(a) * ro); MK_PUT(in, arc_cosf(a) * ri, arc_sinf(a) * ri); }
        if (fill_a > 0) {
            if (m->shape == ARC_MARK_DISC) mk_fan(k, g, m->x, m->z, pts, n, m->fill_rgb, fill_a);
            else for (int i = 0; i < n; i++) { int j = (i + 1) % n;
                mk_tri(k, g, in[2 * i], in[2 * i + 1], pts[2 * i], pts[2 * i + 1], pts[2 * j], pts[2 * j + 1], m->fill_rgb, fill_a, 0);
                mk_tri(k, g, in[2 * i], in[2 * i + 1], pts[2 * j], pts[2 * j + 1], in[2 * j], in[2 * j + 1], m->fill_rgb, fill_a, 0); }
        }
        if (edge_w > 0) { mk_edge(k, g, pts, n, true, edge_w, m->dash, m->edge_rgb, edge_a); if (ri > 0) mk_edge(k, g, in, n, true, edge_w, m->dash, m->edge_rgb, edge_a); }
        break; }
    case ARC_MARK_ARROW: {
        float dx = m->x2 - m->x, dz = m->z2 - m->z, len = sqrtf(dx * dx + dz * dz); if (len < 1e-4f) break;
        float w = (m->size > 0 ? m->size : 0.25f) * 0.5f, head = arc_minf(m->head > 0 ? m->head : w * 5, len), ux = dx / len, uz = dz / len, px = -uz, pz = ux;
        float bx = m->x2 - ux * head, bz = m->z2 - uz * head, hw = w * 2.2f;
        float P[14] = { m->x + px * w, m->z + pz * w, bx + px * w, bz + pz * w, bx + px * hw, bz + pz * hw, m->x2, m->z2,
                        bx - px * hw, bz - pz * hw, bx - px * w, bz - pz * w, m->x - px * w, m->z - pz * w };
        if (fill_a > 0) {
            mk_tri(k, g, P[0], P[1], P[2], P[3], P[10], P[11], m->fill_rgb, fill_a, 0); mk_tri(k, g, P[0], P[1], P[10], P[11], P[12], P[13], m->fill_rgb, fill_a, 0);
            mk_tri(k, g, P[4], P[5], P[6], P[7], P[8], P[9], m->fill_rgb, fill_a, 0);
        }
        if (edge_w > 0) mk_edge(k, g, P, 7, true, edge_w, m->dash, m->edge_rgb, edge_a);
        break; }
    case ARC_MARK_CHEVRON: {   /* a V pointing along the yaw (+x when yaw is 0), arms `size` wide */
        float h = s * 0.5f, t = s * 0.22f;
        float Q[6][2] = { { h * 0.6f, 0 }, { -h * 0.4f, h }, { -h * 0.4f - t * 1.4f, h }, { h * 0.6f - t * 1.4f, 0 }, { -h * 0.4f - t * 1.4f, -h }, { -h * 0.4f, -h } };
        for (n = 0; n < 6; n++) MK_PUT(pts, Q[n][0], Q[n][1]);
        if (fill_a > 0) {
            mk_tri(k, g, pts[0], pts[1], pts[2], pts[3], pts[4], pts[5], m->fill_rgb, fill_a, 0); mk_tri(k, g, pts[0], pts[1], pts[4], pts[5], pts[6], pts[7], m->fill_rgb, fill_a, 0);
            mk_tri(k, g, pts[0], pts[1], pts[6], pts[7], pts[8], pts[9], m->fill_rgb, fill_a, 0); mk_tri(k, g, pts[0], pts[1], pts[8], pts[9], pts[10], pts[11], m->fill_rgb, fill_a, 0);
        }
        if (edge_w > 0) mk_edge(k, g, pts, 6, true, edge_w, m->dash, m->edge_rgb, edge_a);
        break; }
    case ARC_MARK_DIAMOND: {
        float h = s * 0.5f;
        MK_PUT(pts, h, 0); n++; MK_PUT(pts, 0, h); n++; MK_PUT(pts, -h, 0); n++; MK_PUT(pts, 0, -h); n++;
        if (fill_a > 0) mk_fan(k, g, m->x, m->z, pts, n, m->fill_rgb, fill_a);
        if (edge_w > 0) mk_edge(k, g, pts, n, true, edge_w, m->dash, m->edge_rgb, edge_a);
        break; }
    default: {   /* ARC_MARK_TILE: a square `size` wide, corners rounded by `inner` (a share of the side, up to 0.5) */
        float h = s * 0.5f, r = arc_clampf(m->inner, 0, 0.5f) * s;
        if (r <= 0.001f) { MK_PUT(pts, h, h); n++; MK_PUT(pts, -h, h); n++; MK_PUT(pts, -h, -h); n++; MK_PUT(pts, h, -h); n++; }
        else {
            static const float cxs[4] = { 1, -1, -1, 1 }, czs[4] = { 1, 1, -1, -1 };
            for (int q = 0; q < 4; q++) for (int i = 0; i <= 6; i++) {
                float a = (float)(q * 6 + i) * (1.5707963f / 6.0f);
                MK_PUT(pts, cxs[q] * (h - r) + arc_cosf(a) * r, czs[q] * (h - r) + arc_sinf(a) * r); n++;
            }
        }
        if (fill_a > 0) mk_fan(k, g, m->x, m->z, pts, n, m->fill_rgb, fill_a);
        if (edge_w > 0) mk_edge(k, g, pts, n, true, edge_w, m->dash, m->edge_rgb, edge_a);
        break; }
    }
    #undef MK_PUT
}
void arc_draw_marker(const arc_marker *m) {
    if (!m) return;
    int pass = arc__gfx_pass_index(); if (pass < 0) return;
    if (pass != mk_pass) { arc__markers_flush(); mk_pass = pass; }
    mk_ground g = { m, m->lift > 0 ? m->lift : 0.02f };
    arc_v3 centre = arc_v3_make(m->shape == ARC_MARK_ARROW ? (m->x + m->x2) * 0.5f : m->x, 0, m->shape == ARC_MARK_ARROW ? (m->z + m->z2) * 0.5f : m->z);
    centre.y = mk_y(&g, centre.x, centre.z);
    float pulse = 1;
    if (m->pulse > 0) { float t = (float)arc_tick_count() * arc_dt(); pulse = 1.0f - arc_clampf(m->pulse_amount, 0, 1) * (0.5f + 0.5f * arc_sinf(t * m->pulse * 6.2831853f)); }
    float fill_a = m->fill_alpha * pulse, edge_a = (m->edge_alpha > 0 ? m->edge_alpha : 1.0f) * (m->pulse > 0 ? 0.5f + 0.5f * pulse : 1.0f), edge_w = 0;
    if (m->edge_width > 0) {   /* screen pixels to world units at the marker's centre */
        arc_v2 a, b; arc_v3 r = arc__camera_axis(0);
        if (arc_world_to_screen(centre, &a) && arc_world_to_screen(arc_v3_add(centre, r), &b)) {
            float ppu = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
            if (ppu > 1e-4f) edge_w = m->edge_width / ppu;
        }
    }
    int before = mk_n[0];
    mk_build(0, m, &g, fill_a, edge_a, edge_w);
    if (mk_n[0] > before) { mk_centre[0] = arc_v3_add(mk_centre[0], centre); mk_count[0]++; }
    if (m->on_top) { int b1 = mk_n[1]; mk_build(1, m, &g, fill_a * 0.35f, edge_a * 0.45f, edge_w); if (mk_n[1] > b1) { mk_centre[1] = arc_v3_add(mk_centre[1], centre); mk_count[1]++; } }
    if (mk_n[0] > MK_CAP - 4096 || mk_n[1] > MK_CAP - 4096) arc__markers_flush();
    if (m->label && *m->label) {
        float lh = m->label_height > 0 ? m->label_height : 0.8f; arc_v2 sp;
        if (arc_world_to_screen(arc_v3_add(centre, arc_v3_make(0, lh, 0)), &sp))
            arc_draw_text_ex(floorf(sp.x + 0.5f), floorf(sp.y + 0.5f), m->label_scale > 0 ? m->label_scale : 2, m->label_rgb ? m->label_rgb : (m->edge_rgb ? m->edge_rgb : 0xFFFFFF),
                             ARC_TEXT_CENTER | ARC_TEXT_OUTLINE, m->label);
    }
    if (arc__frame_recording()) {
        if (mk_rec_frame != arc__gfx_frame) { mk_rec_frame = arc__gfx_frame; mk_nrec = 0; }
        if (mk_nrec < 256) {
            mk_rec *r = &mk_recs[mk_nrec++]; r->shape = m->shape; r->pos[0] = centre.x; r->pos[1] = centre.y; r->pos[2] = centre.z; r->size = m->size;
            r->rgb = m->fill_alpha > 0 ? m->fill_rgb : m->edge_rgb;
            snprintf(r->label, sizeof r->label, "%s", m->label ? m->label : "");
            r->tgt = arc__frame_target();   /* 0.10.7 */
        }
    }
}
void arc__markers_json(arc_json *j) {
    static const char *names[] = { "tile", "ring", "disc", "arrow", "chevron", "diamond" };
    arc_json_arr_k(j, "markers");
    if (mk_rec_frame == arc__gfx_frame || mk_rec_frame + 1 == arc__gfx_frame)
        for (int i = 0; i < mk_nrec; i++) {
            const mk_rec *r = &mk_recs[i]; char hx[8]; snprintf(hx, sizeof hx, "%06X", r->rgb & 0xFFFFFF);
            arc_json_obj(j); arc_json_str_k(j, "shape", names[arc_clampi(r->shape, 0, 5)]);
            arc_json_arr_k(j, "pos"); for (int q = 0; q < 3; q++) arc_json_num(j, r->pos[q]); arc_json_end(j);
            arc_json_num_k(j, "size", r->size); arc_json_str_k(j, "rgb", hx);
            if (r->label[0]) arc_json_str_k(j, "label", r->label);
            if (r->tgt) { char nm[40]; arc__frame_target_name(r->tgt, nm, sizeof nm); arc_json_str_k(j, "target", nm); }
            arc_json_end(j);
        }
    arc_json_end(j);
}
