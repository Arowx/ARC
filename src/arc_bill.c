/* ARC billboards: camera-facing textured quads and text in the 3D world (depth tested, so walls hide them).
   Drawn as unlit see-through mesh instances, sorted back to front with the other see-through things.
   Text is drawn into a small texture on the CPU (built-in font or an arc_font) and cached by its contents. */

uint32_t arc__fx_quad(uint32_t tex);
arc_mesh arc__quad_mesh(arc_texture t);

/* world units per screen pixel at a point (perspective: grows with distance; orthographic: constant) */
static float bb_world_per_pixel(arc_v3 p) {
    arc_v4 c = arc_m4_mul_v4(R.view_proj, (arc_v4){ p.x, p.y, p.z, 1 });
    float h = (float)arc_target_height(), m5 = R.proj.m[5];
    if (h <= 0 || fabsf(m5) < 1e-8f) return 0;
    return 2.0f * fabsf(c.w) / (fabsf(m5) * h);
}
static void bb_draw(uint32_t mesh, arc_v3 pos, float w, float h, uint32_t tint, float alpha, float glow, int flags) {
    if (!R.in_frame || !mesh || w <= 0 || h <= 0) return;
    arc_v3 F = arc__camera_axis(2), ax, ay;
    if (flags & ARC_BB_UPRIGHT) {   /* turns only around the vertical: stays upright when the camera looks down */
        arc_v3 f = arc_v3_make(F.x, 0, F.z);
        if (arc_v3_len(f) < 1e-4f) f = arc_v3_make(-arc__camera_axis(1).x, 0, -arc__camera_axis(1).z);
        if (arc_v3_len(f) < 1e-4f) f = arc_v3_make(0, 0, 1);
        f = arc_v3_norm(f);
        ax = arc_v3_norm(arc_v3_cross(arc_v3_make(0, 1, 0), f)); ay = arc_v3_make(0, 1, 0); F = f;
    } else { ax = arc__camera_axis(0); ay = arc__camera_axis(1); }
    if (flags & ARC_BB_PIXELS) { float k = bb_world_per_pixel(pos); w *= k; h *= k; }
    if (flags & ARC_BB_BOTTOM) pos = arc_v3_add(pos, arc_v3_scale(ay, h * 0.5f));
    arc__draw_unlit(mesh, arc_m4_basis(pos, arc_v3_scale(ax, w), arc_v3_scale(ay, h), F), tint & 0xFFFFFF, glow, (flags & ARC_BB_ADDITIVE) != 0, alpha);
}
void arc_draw_billboard_ex(arc_texture tex, arc_v3 pos, float w, float h, uint32_t tint, float alpha, float glow, int flags) {
    if (tex.id && (tex.id >= ARC__MAX_TEXTURES || !arc__textures[tex.id].used)) return;
    bb_draw(arc__fx_quad(tex.id), pos, w, h, tint, alpha, glow, flags);
}
void arc_draw_billboard(arc_texture tex, arc_v3 pos, float w, float h, uint32_t tint, int flags) {
    arc_draw_billboard_ex(tex, pos, w, h, tint, 1, 0, flags);
}

/* ---- text ---- */
#define BB_TEXT_CACHE 128
static struct {
    uint64_t key[BB_TEXT_CACHE]; arc_texture tex[BB_TEXT_CACHE]; uint32_t mesh[BB_TEXT_CACHE];
    int w[BB_TEXT_CACHE], h[BB_TEXT_CACHE], line_h[BB_TEXT_CACHE]; uint64_t used[BB_TEXT_CACHE];
    uint64_t frame;
} BB;
static uint64_t bb_hash(uint32_t font, uint32_t rgb, uint32_t bg, const char *s) {
    uint64_t h = 1469598103934665603ull;
    uint32_t parts[3] = { font, rgb, bg };
    for (int i = 0; i < 12; i++) { h ^= ((const uint8_t *)parts)[i]; h *= 1099511628211ull; }
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 1099511628211ull; }
    return h ? h : 1;
}
static void bb_px(uint8_t *img, int w, int x, int y, uint32_t rgb, float a) {   /* "over" in straight alpha */
    uint8_t *p = img + ((size_t)y * w + x) * 4;
    float da = p[3] / 255.0f, oa = a + da * (1 - a);
    if (oa <= 0) return;
    float c[3] = { (float)((rgb >> 16) & 255), (float)((rgb >> 8) & 255), (float)(rgb & 255) };
    for (int i = 0; i < 3; i++) p[i] = (uint8_t)((c[i] * a + p[i] * da * (1 - a)) / oa + 0.5f);
    p[3] = (uint8_t)(oa * 255.0f + 0.5f);
}
/* lay out the text (img NULL: only measure). The block is out_w x out_h; glyphs may reach above it by *out_top
   pixels (and the height covers the lowest descender). Writes are clipped to the iw x ih image. */
static void bb_text_run(arc_font f, const char *s, uint8_t *img, int iw, int ih, int ox, int oy, uint32_t rgb, int *out_w, int *out_h, int *line_h, int *out_top) {
    arc__font_rec *fr = f.id ? font_rec(f) : NULL;
    const arc__tex_rec *atlas = fr ? &arc__textures[fr->tex] : NULL;
    int lh = fr ? fr->size : 8, x = 0, y = 0, best = 0, top = 0, bottom = 0;
    for (uint32_t cp; (cp = arc_utf8_next(&s)) != 0;) {
        if (cp == '\n') { best = arc_maxi(best, x); x = 0; y += lh; continue; }
        if (!fr) {   /* built-in 8x8 font */
            int g = (cp >= 32 && cp < 128) ? (int)cp - 32 : '?' - 32;
            if (img) for (int r = 0; r < 8; r++) for (int c = 0; c < 8; c++)
                if (((arc__font8x8[g][r] >> (7 - c)) & 1) && ox + x + c < iw && oy + y + r < ih) bb_px(img, iw, ox + x + c, oy + y + r, rgb, 1);
            x += 8; continue;
        }
        const arc__glyph *g = glyph_or_fallback(fr, cp); if (!g) continue;
        if (cp != ' ' && g->h > 0) { top = arc_mini(top, y + (int)floorf(g->oy)); bottom = arc_maxi(bottom, y + (int)ceilf(g->oy) + g->h); }
        if (img && atlas && atlas->rgba && cp != ' ')
            for (int r = 0; r < g->h; r++) for (int c = 0; c < g->w; c++) {
                int px = ox + x + (int)g->ox + c, py = oy + y + (int)g->oy + r;
                const uint8_t *src = atlas->rgba + ((size_t)(g->y + r) * atlas->w + (g->x + c)) * 4;
                if (src[3] && px >= 0 && py >= 0 && px < iw && py < ih) {
                    uint32_t col = (uint32_t)(((src[0] * ((rgb >> 16) & 255)) / 255) << 16 | ((src[1] * ((rgb >> 8) & 255)) / 255) << 8 | (src[2] * (rgb & 255)) / 255);
                    bb_px(img, iw, px, py, col, src[3] / 255.0f);
                }
            }
        x += (int)ceilf((float)(g->adv ? g->adv : g->w));
    }
    best = arc_maxi(best, x);
    if (out_w) *out_w = best;
    if (out_h) *out_h = arc_maxi(y + lh, bottom);
    if (line_h) *line_h = lh;
    if (out_top) *out_top = -top;
}
static int bb_text_entry(arc_font f, uint32_t rgb, uint32_t bg, const char *text) {
    uint64_t key = bb_hash(f.id, rgb & 0xFFFFFF, bg, text);
    int slot = -1, lru = 0;
    for (int i = 0; i < BB_TEXT_CACHE; i++) {
        if (BB.key[i] == key && BB.tex[i].id && arc__textures[BB.tex[i].id].used && arc__meshes[BB.mesh[i]].used) { BB.used[i] = BB.frame; return i; }
        if (!BB.key[i]) { if (slot < 0) slot = i; }
        else if (BB.used[i] < BB.used[lru] || !BB.key[lru]) lru = i;
    }
    if (slot < 0) {
        if (BB.used[lru] == BB.frame) {   /* everything is on screen this frame: don't pull a texture from under it */
            static bool warned; if (!warned) { warned = true; arc_log("billboard text: more than %d different texts in one frame", BB_TEXT_CACHE); }
            return -1;
        }
        slot = lru;
        arc_mesh_destroy((arc_mesh){ BB.mesh[slot] }); arc_texture_destroy(BB.tex[slot]);
        BB.key[slot] = 0; BB.tex[slot].id = 0; BB.mesh[slot] = 0;
    }
    int tw, th, lh, above; bb_text_run(f, text, NULL, 0, 0, 0, 0, rgb, &tw, &th, &lh, &above);
    int pad = bg >> 24 ? arc_maxi(2, lh / 4) : 1;   /* a margin keeps filtering from bleeding at the edges */
    int w = tw + pad * 2, h = th + above + pad * 2;
    if (w > 4096 || h > 4096 || tw <= 0) return -1;
    uint8_t *img = (uint8_t *)calloc((size_t)w * h, 4);
    if (!img) return -1;
    if (bg >> 24) {   /* the bubble: a filled box with its corner pixels cut */
        float a = (float)(bg >> 24) / 255.0f;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            bool corner = (x == 0 || x == w - 1) && (y == 0 || y == h - 1);
            if (!corner) bb_px(img, w, x, y, bg & 0xFFFFFF, a);
        }
    } else for (size_t i = 0; i < (size_t)w * h; i++) { img[i * 4] = (uint8_t)(rgb >> 16); img[i * 4 + 1] = (uint8_t)(rgb >> 8); img[i * 4 + 2] = (uint8_t)rgb; }   /* clear edges keep the text colour */
    bb_text_run(f, text, img, w, h, pad, pad + above, rgb, NULL, NULL, NULL, NULL);
    arc_texture t = arc_texture_create_ex(w, h, img, f.id ? ARC_TEX_SMOOTH | ARC_TEX_MIPMAPS : ARC_TEX_MIPMAPS);
    free(img);
    if (!t.id) return -1;
    arc_mesh m = arc__quad_mesh(t);
    if (!m.id) { arc_texture_destroy(t); return -1; }
    BB.key[slot] = key; BB.tex[slot] = t; BB.mesh[slot] = m.id; BB.w[slot] = w; BB.h[slot] = h; BB.line_h[slot] = lh; BB.used[slot] = BB.frame;
    return slot;
}
void arc_draw_billboard_text_font(arc_font f, arc_v3 pos, float height, uint32_t rgb, uint32_t bg_argb, int flags, const char *text) {
    if (!R.in_frame || !text || !*text || height <= 0) return;
    if (f.id && !font_rec(f)) f.id = 0;
    int e = bb_text_entry(f, rgb, bg_argb, text);
    if (e < 0) return;
    float k = height / (float)BB.line_h[e];   /* world units (or pixels) per texture pixel */
    bb_draw(BB.mesh[e], pos, (float)BB.w[e] * k, (float)BB.h[e] * k, 0xFFFFFF, 1, 0, flags);
}
void arc_draw_billboard_text(arc_v3 pos, float height, uint32_t rgb, uint32_t bg_argb, int flags, const char *text) {
    arc_draw_billboard_text_font((arc_font){ 0 }, pos, height, rgb, bg_argb, flags, text);
}
void arc__bill_frame(void) { BB.frame++; }
void arc__bill_shutdown(void) { memset(&BB, 0, sizeof BB); }
