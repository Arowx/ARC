/* ARC 0.10.1 review helpers (MechBattleArc's request): arc_camera_fit, contact sheets (arc_sheet_add + the `sheet`
   command), `shot FILE crop`. */

static void fit_basis(const arc_camera *base, arc_v3 *dir, arc_v3 *right, arc_v3 *up) {
    arc_v3 d = arc_v3_sub(base->target, base->eye);
    *dir = arc_v3_len(d) > 1e-8f ? arc_v3_norm(d) : arc_v3_make(0, 0, -1);
    arc_v3 u = (base->up.x != 0 || base->up.y != 0 || base->up.z != 0) ? base->up : arc_v3_make(0, 1, 0);
    arc_v3 r = arc_v3_cross(*dir, u);
    if (arc_v3_len(r) < 1e-6f) r = arc_v3_cross(*dir, arc_v3_make(1, 0, 0));
    *right = arc_v3_norm(r); *up = arc_v3_cross(*right, *dir);
}
/* the largest projected half-extent on one axis with the view shifted (s) so that it is centred */
static float fit_axis(const float *a, const float *z, int n, float D, float t, float span, float *shift) {
    float lo = -span, hi = span;
    for (int it = 0; it < 60; it++) {
        float s = (lo + hi) * 0.5f, mx = -1e30f, mn = 1e30f;
        for (int i = 0; i < n; i++) { float g = (a[i] - s) / ((D + z[i]) * t); if (g > mx) mx = g; if (g < mn) mn = g; }
        if (mx + mn > 0) lo = s; else hi = s;
    }
    *shift = (lo + hi) * 0.5f;
    float mx = -1e30f; for (int i = 0; i < n; i++) { float g = (a[i] - *shift) / ((D + z[i]) * t); if (g < 0) g = -g; if (g > mx) mx = g; }
    return mx;
}
arc_camera arc_camera_fit(arc_camera base, arc_v3 lo, arc_v3 hi, float aspect, float margin) {
    arc_camera c = base;
    arc_v3 dir, right, up; fit_basis(&base, &dir, &right, &up);
    if (aspect <= 0) aspect = 1;
    float fill = 1.0f - 2.0f * margin; if (fill < 0.05f) fill = 0.05f;
    arc_v3 ctr = arc_v3_scale(arc_v3_add(lo, hi), 0.5f);
    float xs[8], ys[8], zs[8], R = 0.5f * arc_v3_len(arc_v3_sub(hi, lo));
    if (R < 1e-6f) R = 1e-6f;
    for (int i = 0; i < 8; i++) {
        arc_v3 p = arc_v3_make((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z), q = arc_v3_sub(p, ctr);
        xs[i] = arc_v3_dot(q, right); ys[i] = arc_v3_dot(q, up); zs[i] = arc_v3_dot(q, dir);
    }
    if (base.fov_y <= 0) {   /* orthographic: centre the projected box, size the view to it */
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
        for (int i = 0; i < 8; i++) { x0 = arc_minf(x0, xs[i]); x1 = arc_maxf(x1, xs[i]); y0 = arc_minf(y0, ys[i]); y1 = arc_maxf(y1, ys[i]); }
        float hgt = arc_maxf(y1 - y0, (x1 - x0) / aspect) / fill, D = 2.0f * R + 1.0f;
        c.eye = arc_v3_add(arc_v3_add(ctr, arc_v3_scale(right, (x0 + x1) * 0.5f)), arc_v3_add(arc_v3_scale(up, (y0 + y1) * 0.5f), arc_v3_scale(dir, -D)));
        c.target = arc_v3_add(c.eye, arc_v3_scale(dir, D)); c.up = up; c.ortho_height = hgt > 1e-6f ? hgt : 1e-6f;
        if (c.znear <= 0) c.znear = 0.1f;
        if (c.zfar < 2.0f * D) c.zfar = 2.0f * D;
        return c;
    }
    float ty = tanf(base.fov_y * 0.5f), tx = ty * aspect, zmin = 1e30f, zmax = -1e30f;
    for (int i = 0; i < 8; i++) { zmin = arc_minf(zmin, zs[i]); zmax = arc_maxf(zmax, zs[i]); }
    float sx = 0, sy = 0, dlo = -zmin + R * 1e-4f, dhi = dlo + R;
    for (int it = 0; it < 60; it++) { float ax = fit_axis(xs, zs, 8, dhi, tx, 4.0f * R, &sx), ay = fit_axis(ys, zs, 8, dhi, ty, 4.0f * R, &sy); if (arc_maxf(ax, ay) <= fill) break; dhi = dlo + (dhi - dlo) * 2.0f; }
    for (int it = 0; it < 60; it++) {
        float m = (dlo + dhi) * 0.5f, ax = fit_axis(xs, zs, 8, m, tx, 4.0f * R, &sx), ay = fit_axis(ys, zs, 8, m, ty, 4.0f * R, &sy);
        if (arc_maxf(ax, ay) > fill) dlo = m; else dhi = m;
    }
    float D = dhi; fit_axis(xs, zs, 8, D, tx, 4.0f * R, &sx); fit_axis(ys, zs, 8, D, ty, 4.0f * R, &sy);
    c.eye = arc_v3_add(arc_v3_add(ctr, arc_v3_scale(dir, -D)), arc_v3_add(arc_v3_scale(right, sx), arc_v3_scale(up, sy)));
    c.target = arc_v3_add(c.eye, arc_v3_scale(dir, D)); c.up = up;
    float zn = 0.5f * (D + zmin), bz = base.znear > 0 ? base.znear : 0.1f;
    c.znear = zn > 0.001f ? arc_minf(bz, zn) : 0.001f;
    c.zfar = arc_maxf(base.zfar > 0 ? base.zfar : 500.0f, 2.0f * (D + zmax));
    return c;
}

/* ---- contact sheets ---- */
#define SHEET_MAX 64
static struct { arc_mesh mesh; arc_m4 model; arc_mesh_style st; bool has_st, has_pose; arc_pose pose; char label[64]; } SH_items[SHEET_MAX];
static int SH_n; static bool SH_collect;
void arc_sheet_add(arc_mesh mesh, arc_m4 model, const arc_mesh_style *style, const char *label) {
    if (!SH_collect || SH_n >= SHEET_MAX || !mesh.id) return;
    int k = SH_n++; memset(&SH_items[k], 0, sizeof SH_items[k]);
    SH_items[k].mesh = mesh; SH_items[k].model = model;
    if (style) { SH_items[k].st = *style; SH_items[k].has_st = true; if (style->pose) { SH_items[k].pose = *style->pose; SH_items[k].has_pose = true; } }
    snprintf(SH_items[k].label, sizeof SH_items[k].label, "%s", label ? label : "");
}
int arc_sheet_count(void) { return SH_n; }
static void sheet_box(int k, arc_v3 *lo, arc_v3 *hi) {   /* the model's box in world space (its bind-pose box, posed meshes too) */
    arc_v3 a, b; arc_mesh_bounds(SH_items[k].mesh, &a, &b);
    *lo = arc_v3_make(1e30f, 1e30f, 1e30f); *hi = arc_v3_make(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; i++) {
        arc_v3 p = arc_v3_make((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z), q = arc_m4_mul_point(SH_items[k].model, p);
        *lo = arc_v3_make(arc_minf(lo->x, q.x), arc_minf(lo->y, q.y), arc_minf(lo->z, q.z)); *hi = arc_v3_make(arc_maxf(hi->x, q.x), arc_maxf(hi->y, q.y), arc_maxf(hi->z, q.z));
    }
}
static void sheet_draw_cell(int k, arc_texture tgt, int cw, int chh, uint32_t bg, float margin, int lh, bool *on_screen) {
    arc_target_begin(tgt, true, bg | 0xFF000000u);
    arc_camera base = { { 0.45f, 0.35f, 1.0f }, { 0, 0, 0 }, { 0, 1, 0 }, 0.6f, 0, 0.1f, 500 };
    arc_v3 lo, hi; sheet_box(k, &lo, &hi);
    arc_camera cam = arc_camera_fit(base, lo, hi, (float)cw / (float)chh, margin);
    arc_set_camera(&cam);
    arc_set_light(arc_v3_make(0.4f, 1.0f, 0.6f), 0xFFFFFF, 0x505862); arc_set_fog(0, 0, 0);
    arc_mesh_style st = SH_items[k].has_st ? SH_items[k].st : (arc_mesh_style){ 0 };
    if (SH_items[k].has_pose) st.pose = &SH_items[k].pose;
    arc_draw_mesh_styled(SH_items[k].mesh, SH_items[k].model, &st);
    arc_target_end();
    if (on_screen) {   /* the fitted box inside the cell */
        arc_m4 vp = arc_camera_view_proj(&cam, (float)cw, (float)chh); bool in = true;
        for (int i = 0; i < 8; i++) {
            arc_v3 p = arc_v3_make((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
            arc_v4 q = arc_m4_mul_v4(vp, (arc_v4){ p.x, p.y, p.z, 1 });
            in = in && q.w > 0 && fabsf(q.x / q.w) <= 1.001f && fabsf(q.y / q.w) <= 1.001f;
        }
        *on_screen = in;
    }
    arc_draw_texture_ex(tgt, 0, 0, (float)cw, (float)chh, 0, 0, (float)cw, (float)chh, 0, 0, 0, 0xFFFFFFFF);
    if (lh > 0) arc_draw_rect(0, (float)chh, (float)cw, (float)lh, bg | 0xFF000000u);
    if (lh > 0) arc_draw_text_ex((float)cw * 0.5f, (float)chh + 4, 2, 0xFFFFFF, ARC_TEXT_CENTER | ARC_TEXT_OUTLINE, SH_items[k].label);
}
/* sheet FILE.png [COLS] [cell WxH] [bg RRGGBB | transparent] [margin M] */
static bool sheet_render_frame(void (*fn)(void *), void *u) {   /* one frame drawn only by fn */
    arc__review_state(true);   /* 0.10.6: whatever the game set (HUD hidden, post, fog, world mask, ...) */
    arc__gfx_begin(); int ph = arc__phase; arc__phase = ARC__PHASE_DRAW; fn(u); arc__phase = ph; arc__gfx_end(true);
    arc__review_state(false); return true;
}
typedef struct { int k; arc_texture tgt; int cw, ch, lh; uint32_t bg; float margin; bool on; } sheet_job;
static void sheet_job_fn(void *u) { sheet_job *j = (sheet_job *)u; sheet_draw_cell(j->k, j->tgt, j->cw, j->ch, j->bg, j->margin, j->lh, &j->on); }
void arc__sheet_command(int argc, const char **argv, bool (*render_now_fn)(void)) {
    if (argc < 2) { printf("ARC error sheet: use 'sheet FILE.png [COLS] [cell WxH] [bg RRGGBB|transparent] [margin M]'\n"); fflush(stdout); return; }
    const char *file = argv[1]; int cols = 0, cw = 256, ch = 256; uint32_t bg = 0x2A2E36; bool transparent = false; float margin = 0.08f;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "cell") && i + 1 < argc) { sscanf(argv[++i], "%dx%d", &cw, &ch); }
        else if (!strcmp(argv[i], "bg") && i + 1 < argc) { const char *v = argv[++i]; if (!strcmp(v, "transparent")) transparent = true; else bg = (uint32_t)strtoul(v[0] == '#' ? v + 1 : v, NULL, 16) & 0xFFFFFF; }
        else if (!strcmp(argv[i], "transparent")) transparent = true;
        else if (!strcmp(argv[i], "margin") && i + 1 < argc) margin = (float)atof(argv[++i]);
        else cols = atoi(argv[i]);
    }
    SH_n = 0; SH_collect = true; render_now_fn(); SH_collect = false;   /* the game's draw registers this frame's items */
    if (!SH_n) { printf("ARC error sheet: nothing registered (call arc_sheet_add in draw)\n"); fflush(stdout); return; }
    int lh = 24, W = arc_width(), H = arc_height();
    if (cw > W) cw = W;
    if (ch + lh > H) ch = H - lh;
    if (cw < 16 || ch < 16) { printf("ARC error sheet: the window is too small for a cell\n"); fflush(stdout); return; }
    if (cols <= 0) { cols = 1; while (cols * cols < SH_n) cols++; }
    int rows = (SH_n + cols - 1) / cols, SW = cols * cw, SHh = rows * (ch + lh);
    uint8_t *img = (uint8_t *)calloc((size_t)SW * SHh * 4, 1);
    if (img && !transparent) for (size_t i = 0; i < (size_t)SW * SHh; i++) {   /* 0.10.6: empty cells in the background colour */
        img[i * 4] = (uint8_t)(bg >> 16); img[i * 4 + 1] = (uint8_t)(bg >> 8); img[i * 4 + 2] = (uint8_t)bg; img[i * 4 + 3] = 255;
    }
    arc_texture tgt = arc_target_create(cw, ch, true);
    arc_json j; memset(&j, 0, sizeof j); arc_json_obj(&j); arc_json_str_k(&j, "file", file); arc_json_int_k(&j, "cols", cols); arc_json_int_k(&j, "rows", rows);
    arc_json_arr_k(&j, "cells");
    int ok_cells = 0;
    for (int k = 0; k < SH_n && img && tgt.id; k++) {
        int cx = (k % cols) * cw, cy = (k / cols) * (ch + lh);
        uint8_t *pass[2] = { NULL, NULL }; int pw = 0, ph = 0; sheet_job job = { k, tgt, cw, ch, lh, 0, margin, false };
        for (int p = 0; p < (transparent ? 2 : 1); p++) {
            job.bg = transparent ? (p ? 0xFFFFFF : 0x000000) : bg;
            if (!sheet_render_frame(sheet_job_fn, &job)) break;
            pass[p] = arc__gfx_read_frame(&pw, &ph);
        }
        int ink = 0, lab = 0;   /* 0.10.6: pixels unlike the background in the picture and in the label row */
        if (pass[0] && pw >= cw && ph >= ch + lh) {
            uint32_t b0 = transparent ? 0x000000u : bg;
            for (int y = 0; y < ch + lh; y++) for (int x = 0; x < cw; x++) {
                const uint8_t *a = pass[0] + ((size_t)y * pw + x) * 4;
                int dd = abs(a[0] - (int)((b0 >> 16) & 0xFF)) + abs(a[1] - (int)((b0 >> 8) & 0xFF)) + abs(a[2] - (int)(b0 & 0xFF));
                if (dd > 24) { if (y < ch) ink++; else lab++; }
            }
            for (int y = 0; y < ch + lh; y++) for (int x = 0; x < cw; x++) {
                const uint8_t *a = pass[0] + ((size_t)y * pw + x) * 4; uint8_t *d = img + ((size_t)(cy + y) * SW + cx + x) * 4;
                if (transparent && pass[1] && y < ch) {   /* alpha from the black and white backgrounds */
                    const uint8_t *b = pass[1] + ((size_t)y * pw + x) * 4;
                    int diff = ((b[0] - a[0]) + (b[1] - a[1]) + (b[2] - a[2])) / 3; int al = 255 - (diff < 0 ? 0 : diff > 255 ? 255 : diff);
                    for (int c = 0; c < 3; c++) d[c] = al ? (uint8_t)arc_clampi(a[c] * 255 / al, 0, 255) : 0;
                    d[3] = (uint8_t)al;
                } else if (transparent && y >= ch) {   /* the label row: white text with a black outline; alpha from its brightness above black */
                    int v = (a[0] + a[1] + a[2]) / 3; d[0] = d[1] = d[2] = (uint8_t)(v > 40 ? 255 : 0); d[3] = (uint8_t)(v > 40 ? 255 : (v > 0 ? 200 : 0));
                } else { d[0] = a[0]; d[1] = a[1]; d[2] = a[2]; d[3] = 255; }
            }
            ok_cells++;
        }
        free(pass[0]); free(pass[1]);
        arc_json_obj(&j); arc_json_str_k(&j, "label", SH_items[k].label); arc_json_int_k(&j, "x", cx); arc_json_int_k(&j, "y", cy); arc_json_int_k(&j, "w", cw); arc_json_int_k(&j, "h", ch);
        arc_json_bool_k(&j, "on_screen", job.on);
        arc_json_num_k(&j, "filled", (float)ink / (float)(cw * ch)); arc_json_num_k(&j, "label_ink", (float)lab / (float)(cw * lh)); arc_json_arr_k(&j, "label_at"); arc_json_num(&j, (float)cx + (float)cw * 0.5f); arc_json_num(&j, (float)(cy + ch + 4)); arc_json_end(&j);
        arc_json_end(&j);
    }
    arc_json_end(&j); arc_json_end(&j);
    size_t fl = strlen(file); bool png = fl > 4 && (!strcmp(file + fl - 4, ".png") || !strcmp(file + fl - 4, ".PNG"));
    bool saved = img && ok_cells == SH_n && (png ? arc_image_save_png(file, img, SW, SHh) : arc__image_write(file, img, SW, SHh));   /* (PNG keeps the alpha) */
    printf("ARC sheet %s\n", j.buf ? j.buf : "{}"); free(j.buf);
    if (saved) printf("ARC ok sheet %s %d cells %dx%d\n", file, SH_n, SW, SHh); else printf("ARC error sheet: could not draw or write %s\n", file);
    fflush(stdout);
    free(img); if (tgt.id) arc_texture_destroy(tgt);
}
/* `shot FILE crop`: the frame trimmed to what differs from its corner colour, plus a margin */
bool arc__crop_write(const char *path, const uint8_t *px, int w, int h, int margin) {
    const uint8_t *bgp = px; int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        const uint8_t *p = px + ((size_t)y * w + x) * 4;
        if (abs(p[0] - bgp[0]) > 8 || abs(p[1] - bgp[1]) > 8 || abs(p[2] - bgp[2]) > 8) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    }
    if (x1 < 0) { x0 = 0; y0 = 0; x1 = w - 1; y1 = h - 1; }
    x0 = arc_maxi(0, x0 - margin); y0 = arc_maxi(0, y0 - margin); x1 = arc_mini(w - 1, x1 + margin); y1 = arc_mini(h - 1, y1 + margin);
    int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    uint8_t *o = (uint8_t *)arc__xalloc((size_t)cw * ch * 4);
    for (int y = 0; y < ch; y++) memcpy(o + (size_t)y * cw * 4, px + ((size_t)(y0 + y) * w + x0) * 4, (size_t)cw * 4);
    bool ok = arc__image_write(path, o, cw, ch); free(o);
    printf(ok ? "ARC ok shot %s crop %dx%d at %d,%d\n" : "ARC error shot %s\n", path, cw, ch, x0, y0); fflush(stdout);
    return ok;
}
