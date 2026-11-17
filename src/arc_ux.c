/* ARC UX helpers (0.7): easing curves, colour-blind palette and remaps, rich-text markup, string tables.
 * Captions and screen transitions live in arc_ui.c (they draw through the UI overlay). */
#include "arc_internal.h"
#include <ctype.h>

/* ---- easing ---- */
static float e01(float t) { return arc_clampf(t, 0, 1); }
float arc_ease_in_quad(float t) { t = e01(t); return t * t; }
float arc_ease_out_quad(float t) { t = e01(t); return 1 - (1 - t) * (1 - t); }
float arc_ease_inout_quad(float t) { t = e01(t); return t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); }
float arc_ease_in_cubic(float t) { t = e01(t); return t * t * t; }
float arc_ease_out_cubic(float t) { t = e01(t); float u = 1 - t; return 1 - u * u * u; }
float arc_ease_inout_cubic(float t) { t = e01(t); float u = 1 - t; return t < 0.5f ? 4 * t * t * t : 1 - 4 * u * u * u; }
#define EASE_C1 1.70158f
#define EASE_C2 (EASE_C1 * 1.525f)
float arc_ease_in_back(float t) { t = e01(t); return t * t * ((EASE_C1 + 1) * t - EASE_C1); }
float arc_ease_out_back(float t) { t = e01(t); float u = t - 1; return 1 + u * u * ((EASE_C1 + 1) * u + EASE_C1); }
float arc_ease_inout_back(float t) {
    t = e01(t);
    if (t < 0.5f) { float a = 2 * t; return a * a * ((EASE_C2 + 1) * a - EASE_C2) * 0.5f; }
    float a = 2 * t - 2; return (a * a * ((EASE_C2 + 1) * a + EASE_C2) + 2) * 0.5f;
}
float arc_ease_out_elastic(float t) {
    t = e01(t);
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    return powf(2, -10 * t) * sinf((t * 10 - 0.75f) * (2 * ARC_PI / 3)) + 1;
}
float arc_ease_out_bounce(float t) {
    t = e01(t); const float n = 7.5625f, d = 2.75f;
    if (t < 1 / d) return n * t * t;
    if (t < 2 / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
    if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
    t -= 2.625f / d; return n * t * t + 0.984375f;
}
float arc_ease_inout_sine(float t) { t = e01(t); return t <= 0 ? 0 : t >= 1 ? 1 : -(cosf(ARC_PI * t) - 1) * 0.5f; }
float arc_ease(arc_ease_kind k, float t) {
    switch (k) {
    case ARC_EASE_IN_QUAD: return arc_ease_in_quad(t);
    case ARC_EASE_OUT_QUAD: return arc_ease_out_quad(t);
    case ARC_EASE_INOUT_QUAD: return arc_ease_inout_quad(t);
    case ARC_EASE_IN_CUBIC: return arc_ease_in_cubic(t);
    case ARC_EASE_OUT_CUBIC: return arc_ease_out_cubic(t);
    case ARC_EASE_INOUT_CUBIC: return arc_ease_inout_cubic(t);
    case ARC_EASE_IN_BACK: return arc_ease_in_back(t);
    case ARC_EASE_OUT_BACK: return arc_ease_out_back(t);
    case ARC_EASE_INOUT_BACK: return arc_ease_inout_back(t);
    case ARC_EASE_OUT_ELASTIC: return arc_ease_out_elastic(t);
    case ARC_EASE_OUT_BOUNCE: return arc_ease_out_bounce(t);
    case ARC_EASE_INOUT_SINE: return arc_ease_inout_sine(t);
    default: return e01(t);
    }
}
float arc_tween(float from, float to, float t, arc_ease_kind kind) { return from + (to - from) * arc_ease(kind, t); }

/* ---- colour blindness ---- */
uint32_t arc_palette_cb(int i) {
    static const uint32_t okabe_ito[8] = { 0xE69F00, 0x56B4E9, 0x009E73, 0xF0E442, 0x0072B2, 0xD55E00, 0xCC79A7, 0x000000 };
    return okabe_ito[((i % 8) + 8) % 8];
}
static const float CVD_M[3][9] = {   /* Machado, Oliveira & Fernandes 2009, severity 1.0, linear RGB */
    { 0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f, 1.051998f },
    { 0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f, 0.968881f },
    { 1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f, 0.303900f },
};
static float s2l(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static float l2s(float c) { c = arc_clampf(c, 0, 1); return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1 / 2.4f) - 0.055f; }
static void cvd_sim3(const float in[3], float out[3], int mode) {   /* sRGB 0..1 in and out */
    if (mode < ARC_CVD_PROTAN || mode > ARC_CVD_TRITAN) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; return; }
    const float *m = CVD_M[mode - 1]; float l[3] = { s2l(in[0]), s2l(in[1]), s2l(in[2]) };
    for (int i = 0; i < 3; i++) out[i] = l2s(m[i * 3] * l[0] + m[i * 3 + 1] * l[1] + m[i * 3 + 2] * l[2]);
}
static void cvd_dalt3(float c[3], int mode) {   /* Fidaner et al.: shift the error into channels the viewer sees */
    if (mode < ARC_CVD_PROTAN || mode > ARC_CVD_TRITAN) return;
    float s[3]; cvd_sim3(c, s, mode);
    float e0 = c[0] - s[0], e1 = c[1] - s[1], e2 = c[2] - s[2];
    if (mode == ARC_CVD_TRITAN) { c[0] += 0.7f * e2; c[1] += 0.7f * e2; }   /* blue-yellow loss: into red and green */
    else { c[1] += 0.7f * e0 + e1; c[2] += 0.7f * e0 + e2; }               /* red-green loss: into green and blue */
    for (int i = 0; i < 3; i++) c[i] = arc_clampf(c[i], 0, 1);
}
static void unpack3(uint32_t rgb, float c[3]) { c[0] = (float)((rgb >> 16) & 255) / 255.0f; c[1] = (float)((rgb >> 8) & 255) / 255.0f; c[2] = (float)(rgb & 255) / 255.0f; }
static uint32_t pack3(const float c[3], uint32_t keep) {
    uint32_t r = (uint32_t)(arc_clampf(c[0], 0, 1) * 255.0f + 0.5f), g = (uint32_t)(arc_clampf(c[1], 0, 1) * 255.0f + 0.5f), b = (uint32_t)(arc_clampf(c[2], 0, 1) * 255.0f + 0.5f);
    return (keep & 0xFF000000u) | r << 16 | g << 8 | b;
}
uint32_t arc_cvd_simulate(uint32_t rgb, int mode) {
    if (mode == ARC_CVD_NONE) return rgb;
    float c[3], o[3]; unpack3(rgb, c); cvd_sim3(c, o, mode); return pack3(o, rgb);
}
uint32_t arc_cvd_daltonize(uint32_t rgb, int mode) {
    if (mode == ARC_CVD_NONE) return rgb;
    float c[3]; unpack3(rgb, c); cvd_dalt3(c, mode); return pack3(c, rgb);
}
static int g_cvd_mode;
void arc_set_cvd(int mode) { g_cvd_mode = mode >= ARC_CVD_NONE && mode <= ARC_CVD_TRITAN ? mode : ARC_CVD_NONE; }
int  arc_get_cvd(void) { return g_cvd_mode; }
uint32_t arc_cvd(uint32_t rgb) { return g_cvd_mode ? arc_cvd_daltonize(rgb, g_cvd_mode) : rgb; }
static void cvd_lut_fn(float rgb[3], void *user) { cvd_dalt3(rgb, (int)(intptr_t)user); }
arc_texture arc_cvd_lut(int mode) { return arc_lut_build(cvd_lut_fn, (void *)(intptr_t)mode, 16); }

/* ---- rich text ---- */
#define RICH_ICONS 64
static struct { char name[32]; arc_texture t; float sx, sy, sw, sh; } g_icons[RICH_ICONS];
static int g_nicons;
void arc_rich_icon(const char *name, arc_texture t, float sx, float sy, float sw, float sh) {
    if (!name || !*name) return;
    int i = 0; while (i < g_nicons && strcmp(g_icons[i].name, name)) i++;
    if (i == g_nicons) { if (g_nicons >= RICH_ICONS) { arc_log("rich text: too many icons (%d)", RICH_ICONS); return; } g_nicons++; }
    snprintf(g_icons[i].name, sizeof g_icons[i].name, "%s", name);
    if (sw <= 0 || sh <= 0) { int w = 0, h = 0; arc_texture_size(t, &w, &h); sx = sy = 0; sw = (float)w; sh = (float)h; }
    g_icons[i].t = t; g_icons[i].sx = sx; g_icons[i].sy = sy; g_icons[i].sw = sw; g_icons[i].sh = sh;
}
static int icon_find(const char *name, size_t n) {
    for (int i = 0; i < g_nicons; i++) if (strlen(g_icons[i].name) == n && !memcmp(g_icons[i].name, name, n)) return i;
    return -1;
}
static int hexval(const char *s, size_t n, uint32_t *out) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return 0;
        v = v << 4 | (uint32_t)d;
    }
    *out = v; return 1;
}
/* one tag at s ('['): its kind and argument. 0 = not a tag (draw the '[' as text) */
enum { RTAG_NONE, RTAG_BRACKET, RTAG_COLOR, RTAG_END_COLOR, RTAG_ICON, RTAG_KEY };
typedef struct { int kind; size_t len; const char *arg; size_t argn; uint32_t color; int icon; } rich_tag;
static rich_tag rich_parse(const char *s) {
    rich_tag t; memset(&t, 0, sizeof t); t.icon = -1;
    if (s[1] == '[') { t.kind = RTAG_BRACKET; t.len = 2; return t; }
    const char *e = strchr(s, ']');
    if (!e || e - s > 48) return t;
    t.len = (size_t)(e - s) + 1;
    if (!strncmp(s, "[/c]", 4)) { t.kind = RTAG_END_COLOR; return t; }
    if (!strncmp(s, "[c=", 3)) {
        size_t n = (size_t)(e - (s + 3));
        if ((n == 6 || n == 8) && hexval(s + 3, n, &t.color)) { if (n == 6) t.color |= 0xFF000000u; t.kind = RTAG_COLOR; }
        return t;
    }
    if (!strncmp(s, "[icon=", 6)) { t.arg = s + 6; t.argn = (size_t)(e - t.arg); t.icon = icon_find(t.arg, t.argn); if (t.icon >= 0) t.kind = RTAG_ICON; return t; }
    if (!strncmp(s, "[key=", 5)) { t.arg = s + 5; t.argn = (size_t)(e - t.arg); if (t.argn > 0 && t.argn < 24) t.kind = RTAG_KEY; return t; }
    return t;
}
/* the keycap's label and colours: PAD_A.. as coloured buttons, other keys as a pale key */
static void key_style(const char *arg, size_t n, char *label, size_t cap, uint32_t *body, uint32_t *ink, bool *pad) {
    char up[24]; size_t k = n < sizeof up - 1 ? n : sizeof up - 1;
    for (size_t i = 0; i < k; i++) up[i] = (char)toupper((unsigned char)arg[i]);
    up[k] = 0;
    *body = 0xFFD8DCE4; *ink = 0xFF202430; *pad = false;
    static const struct { const char *name; const char *label; uint32_t c; } pads[] = {
        { "PAD_A", "A", 0xFF3CB043 }, { "PAD_B", "B", 0xFFE03C31 }, { "PAD_X", "X", 0xFF2D7FF9 }, { "PAD_Y", "Y", 0xFFF5C400 },
        { "PAD_LB", "LB", 0xFF505868 }, { "PAD_RB", "RB", 0xFF505868 }, { "PAD_LT", "LT", 0xFF505868 }, { "PAD_RT", "RT", 0xFF505868 },
        { "PAD_START", "START", 0xFF505868 }, { "PAD_BACK", "BACK", 0xFF505868 },
        { "PAD_UP", "^", 0xFF505868 }, { "PAD_DOWN", "v", 0xFF505868 }, { "PAD_LEFT", "<", 0xFF505868 }, { "PAD_RIGHT", ">", 0xFF505868 },
    };
    for (size_t i = 0; i < sizeof pads / sizeof pads[0]; i++)
        if (!strcmp(up, pads[i].name)) { snprintf(label, cap, "%s", pads[i].label); *body = pads[i].c; *ink = 0xFFFFFFFF; *pad = true; return; }
    if (!strcmp(up, "ESCAPE")) snprintf(label, cap, "ESC");
    else if (!strcmp(up, "LEFT")) snprintf(label, cap, "<");
    else if (!strcmp(up, "RIGHT")) snprintf(label, cap, ">");
    else if (!strcmp(up, "UP")) snprintf(label, cap, "^");
    else if (!strcmp(up, "DOWN")) snprintf(label, cap, "v");
    else snprintf(label, cap, "%s", up);
}
typedef struct { arc_font f; float scale, lh, adv; bool font; } rich_ctx;
static float run_w(const rich_ctx *c, const char *s) { return c->font ? arc_font_width(c->f, c->scale, 0, s) : arc_text_width(c->scale, s); }
static void run_draw(const rich_ctx *c, float x, float y, uint32_t col, const char *s) {
    if (c->font) arc_font_draw_ex(c->f, x, y, c->scale, 0, col, s); else arc_draw_text(x, y, c->scale, col, s);
}
static float cap_label_scale(const rich_ctx *c) { return c->scale * 0.75f; }
static float key_w(const rich_ctx *c, const char *label) {
    float tw = c->font ? arc_font_width(c->f, cap_label_scale(c), 0, label) : arc_text_width(cap_label_scale(c), label);
    float h = c->lh, m = arc_maxf(1.0f, h * 0.0625f);
    return arc_maxf(h, tw + h * 0.5f) + 2 * m;   /* the cap plus a margin each side */
}
/* walk the text: measure (draw false) or draw it. mode 0 = everything, 1 = only text runs in `dark` (shadows). */
static float rich_walk(const rich_ctx *c, float x, float y, uint32_t base, const char *text, bool draw, int mode, uint32_t dark, int *lines) {
    char run[512]; size_t rn = 0;
    uint32_t stack[8]; int sp = 0; uint32_t col = base;
    float cx = x, cy = y, best = 0; int nl = 1;
    const char *p = text;
#define RICH_FLUSH() do { if (rn) { run[rn] = 0; if (draw) run_draw(c, cx, cy, mode ? dark : col, run); cx += run_w(c, run); rn = 0; } } while (0)
    while (*p) {
        if (*p == '\n') { RICH_FLUSH(); best = arc_maxf(best, cx - x); cx = x; cy += c->adv; nl++; p++; continue; }
        if (*p == '[') {
            rich_tag t = rich_parse(p);
            if (t.kind != RTAG_NONE) {
                if (t.kind == RTAG_BRACKET) { if (rn < sizeof run - 1) run[rn++] = '['; p += t.len; continue; }
                RICH_FLUSH();
                if (t.kind == RTAG_COLOR) { if (sp < 8) stack[sp++] = col; uint32_t a = (t.color >> 24) == 0xFF ? (base & 0xFF000000u) : (t.color & 0xFF000000u); col = a | (t.color & 0xFFFFFFu); }
                else if (t.kind == RTAG_END_COLOR) col = sp > 0 ? stack[--sp] : base;
                else if (t.kind == RTAG_ICON) {
                    float isx = g_icons[t.icon].sx, isy = g_icons[t.icon].sy, isw = g_icons[t.icon].sw, ish = g_icons[t.icon].sh;
                    float w = c->lh * (ish > 0 ? isw / ish : 1);
                    if (draw && !mode) arc_draw_texture_ex(g_icons[t.icon].t, isx, isy, isw, ish, cx, cy, w, c->lh, 0, 0, 0, (base & 0xFF000000u) | 0xFFFFFFu);
                    cx += w;
                } else if (t.kind == RTAG_KEY) {
                    char label[24]; uint32_t body, ink; bool pad; key_style(t.arg, t.argn, label, sizeof label, &body, &ink, &pad);
                    float w = key_w(c, label), h = c->lh, m = arc_maxf(1.0f, h * 0.0625f);
                    if (draw && !mode) {
                        float bx = cx + m, bw = w - 2 * m, edge = arc_maxf(1.0f, h * 0.125f);
                        uint32_t a = base & 0xFF000000u; if (!a) a = 0xFF000000u;
                        if (pad) arc_draw_rect(bx, cy, bw, h, (body & 0xFFFFFFu) | a);
                        else { arc_draw_rect(bx, cy, bw, h, 0x808890u | a); arc_draw_rect(bx, cy, bw, h - edge, (body & 0xFFFFFFu) | a); }
                        float ls = cap_label_scale(c), lw = c->font ? arc_font_width(c->f, ls, 0, label) : arc_text_width(ls, label);
                        float lh = c->font ? (float)arc_font_size(c->f) * ls : 8.0f * ls;
                        float lx = bx + (bw - lw) * 0.5f, ly = cy + (h - (pad ? 0 : edge) - lh) * 0.5f;
                        if (c->font) arc_font_draw_ex(c->f, lx, ly, ls, 0, (ink & 0xFFFFFFu) | a, label); else arc_draw_text(lx, ly, ls, (ink & 0xFFFFFFu) | a, label);
                    }
                    cx += w;
                }
                p += t.len; continue;
            }
        }
        if (rn >= sizeof run - 1) RICH_FLUSH();
        run[rn++] = *p++;
    }
    RICH_FLUSH();
#undef RICH_FLUSH
    if (lines) *lines = nl;
    return arc_maxf(best, cx - x);
}
static rich_ctx rich_make(arc_font f, float scale) {
    rich_ctx c; c.f = f; c.scale = scale; c.font = f.id && arc_font_size(f) > 0;
    c.lh = c.font ? (float)arc_font_size(f) * scale : 8.0f * scale;
    c.adv = c.font ? c.lh : c.lh * 1.25f;
    return c;
}
float arc_rich_width(arc_font f, float scale, const char *text) {
    if (!text) return 0;
    rich_ctx c = rich_make(f, scale); return rich_walk(&c, 0, 0, 0, text, false, 0, 0, NULL);
}
float arc_rich_height(arc_font f, float scale, const char *text) {
    if (!text) return 0;
    rich_ctx c = rich_make(f, scale); int n = 1; for (const char *p = text; *p; p++) if (*p == '\n') n++;
    return c.lh + (float)(n - 1) * c.adv;
}
float arc_rich_draw(arc_font f, float x, float y, float scale, uint32_t rgb, int flags, const char *text) {
    if (!text) return 0;
    rich_ctx c = rich_make(f, scale);
    float w = rich_walk(&c, 0, 0, 0, text, false, 0, 0, NULL), h = arc_rich_height(f, scale, text), o = arc_maxf(1.0f, scale);
    if (flags & ARC_TEXT_CENTER) x -= w * 0.5f; else if (flags & ARC_TEXT_RIGHT) x -= w;
    if (flags & ARC_TEXT_MIDDLE) y -= h * 0.5f; else if (flags & ARC_TEXT_BOTTOM) y -= h;
    uint32_t a = rgb >> 24; uint32_t d = arc_rgba(0x000000, 0.7f * (a ? (float)a / 255.0f : 1.0f));
    if (g_fr.on && !g_fr.depth) { char plain[512]; arc_rich_strip(text, plain, sizeof plain); fr_text(x, y, w, h, rgb, plain); }   /* 0.9.0 frame description: the text without markup */
    g_fr.depth++;
    if (flags & ARC_TEXT_OUTLINE) for (int j = -1; j <= 1; j++) for (int i = -1; i <= 1; i++) if (i || j) rich_walk(&c, x + (float)i * o, y + (float)j * o, rgb, text, true, 1, d, NULL);
    if (flags & ARC_TEXT_SHADOW) rich_walk(&c, x + o, y + o, rgb, text, true, 1, d, NULL);
    rich_walk(&c, x, y, rgb, text, true, 0, 0, NULL);
    g_fr.depth--;
    return w;
}
size_t arc_rich_strip(const char *text, char *out, size_t cap) {
    size_t n = 0;
    if (!out || !cap) return 0;
    for (const char *p = text ? text : ""; *p;) {
        if (*p == '[') {
            rich_tag t = rich_parse(p);
            if (t.kind == RTAG_BRACKET) { if (n + 1 < cap) out[n++] = '['; p += t.len; continue; }
            if (t.kind == RTAG_KEY) { for (size_t i = 0; i < t.argn && n + 1 < cap; i++) out[n++] = t.arg[i]; p += t.len; continue; }
            if (t.kind != RTAG_NONE) { p += t.len; continue; }
        }
        if (n + 1 < cap) out[n++] = *p;
        p++;
    }
    out[n] = 0; return n;
}

/* ---- string tables ---- */
#define TR_LANGS 16
typedef struct { const char *key, *val; uint32_t hash; } tr_entry;
typedef struct { char name[16]; tr_entry *e; int count, cap; int *table; int tcap; char **blobs; int nblobs; } tr_lang;
static tr_lang g_tr[TR_LANGS]; static int g_ntr; static int g_tr_active = -1, g_tr_fallback = -1;
static char g_tr_active_name[16], g_tr_fallback_name[16];
static uint32_t tr_hash(const char *s, size_t n) { uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; } return h ? h : 1; }
static int tr_find_lang(const char *lang) { for (int i = 0; i < g_ntr; i++) if (!strcmp(g_tr[i].name, lang)) return i; return -1; }
static void tr_resolve(void) {
    g_tr_active = g_tr_active_name[0] ? tr_find_lang(g_tr_active_name) : -1;
    g_tr_fallback = g_tr_fallback_name[0] ? tr_find_lang(g_tr_fallback_name) : (g_ntr ? 0 : -1);
}
static int tr_lookup(const tr_lang *L, const char *key, size_t n, uint32_t h) {
    if (!L->tcap) return -1;
    for (int i = (int)(h & (uint32_t)(L->tcap - 1)), k = 0; k < L->tcap; k++, i = (i + 1) & (L->tcap - 1)) {
        int e = L->table[i];
        if (e < 0) return -1;
        if (L->e[e].hash == h && !strncmp(L->e[e].key, key, n) && L->e[e].key[n] == 0) return e;
    }
    return -1;
}
static void tr_rehash(tr_lang *L) {
    int need = 16; while (need < L->count * 2 + 2) need *= 2;
    if (need != L->tcap) { free(L->table); L->table = (int *)arc__xalloc(sizeof(int) * (size_t)need); L->tcap = need; }
    for (int i = 0; i < L->tcap; i++) L->table[i] = -1;
    for (int e = 0; e < L->count; e++) {
        int i = (int)(L->e[e].hash & (uint32_t)(L->tcap - 1));
        while (L->table[i] >= 0) i = (i + 1) & (L->tcap - 1);
        L->table[i] = e;
    }
}
bool arc_tr_load_memory(const char *lang, const char *text, size_t size) {
    if (!lang || !*lang || !text) return false;
    int li = tr_find_lang(lang);
    if (li < 0) {
        if (g_ntr >= TR_LANGS) { arc_log("tr: too many languages (%d)", TR_LANGS); return false; }
        li = g_ntr++; memset(&g_tr[li], 0, sizeof g_tr[li]); snprintf(g_tr[li].name, sizeof g_tr[li].name, "%s", lang);
    }
    tr_lang *L = &g_tr[li];
    char *blob = (char *)arc__xalloc(size + 1);   /* keys and texts, unescaped in place; kept until arc_tr_clear */
    memcpy(blob, text, size); blob[size] = 0;
    char **nb = (char **)realloc(L->blobs, sizeof(char *) * (size_t)(L->nblobs + 1)); if (!nb) { free(blob); return false; }
    L->blobs = nb; L->blobs[L->nblobs++] = blob;
    char *p = blob, *end = blob + size;
    if (size >= 3 && (uint8_t)p[0] == 0xEF && (uint8_t)p[1] == 0xBB && (uint8_t)p[2] == 0xBF) p += 3;
    int added = 0;
    while (p < end) {
        char *line = p; while (p < end && *p != '\n') p++;
        char *le = p; if (p < end) *p++ = 0;
        if (le > line && le[-1] == '\r') *--le = 0;
        while (*line == ' ' || *line == '\t') line++;
        if (!*line || *line == '#') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        char *ke = eq; while (ke > line && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
        *ke = 0;
        char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
        char *ve = v + strlen(v); while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) *--ve = 0;
        char *w = v;   /* unescape \n, \t, \\ */
        for (char *r = v; *r; r++) {
            if (*r == '\\' && r[1]) { r++; *w++ = *r == 'n' ? '\n' : *r == 't' ? '\t' : *r; }
            else *w++ = *r;
        }
        *w = 0;
        if (!*line) continue;
        uint32_t h = tr_hash(line, strlen(line));
        int e = tr_lookup(L, line, strlen(line), h);
        if (e >= 0) { L->e[e].val = v; added++; continue; }
        if (L->count >= L->cap) {
            int nc = L->cap ? L->cap * 2 : 64; tr_entry *ne = (tr_entry *)realloc(L->e, sizeof(tr_entry) * (size_t)nc);
            if (!ne) break;
            L->e = ne; L->cap = nc;
        }
        L->e[L->count].key = line; L->e[L->count].val = v; L->e[L->count].hash = h; L->count++; added++;
        if (L->count * 2 + 2 > L->tcap) tr_rehash(L);
        else { int i = (int)(h & (uint32_t)(L->tcap - 1)); while (L->table[i] >= 0) i = (i + 1) & (L->tcap - 1); L->table[i] = L->count - 1; }
    }
    tr_resolve();
    return added > 0;
}
bool arc_tr_load(const char *lang, const char *path) {
    size_t mark = arc_arena_mark(arc_frame()), n = 0;
    uint8_t *d = arc_file_read(path, arc_frame(), &n);
    bool ok = d && arc_tr_load_memory(lang, (const char *)d, n);
    if (!d) arc_log("tr: cannot load %s", path);
    arc_arena_pop(arc_frame(), mark);
    return ok;
}
void arc_tr_set_language(const char *lang) { snprintf(g_tr_active_name, sizeof g_tr_active_name, "%s", lang ? lang : ""); tr_resolve(); }
const char *arc_tr_language(void) { return g_tr_active_name[0] ? g_tr_active_name : g_tr_fallback >= 0 ? g_tr[g_tr_fallback].name : ""; }
void arc_tr_set_fallback(const char *lang) { snprintf(g_tr_fallback_name, sizeof g_tr_fallback_name, "%s", lang ? lang : ""); tr_resolve(); }
static const char *tr_get(int li, const char *key, size_t n, uint32_t h) {
    if (li < 0 || li >= g_ntr) return NULL;
    int e = tr_lookup(&g_tr[li], key, n, h); return e >= 0 ? g_tr[li].e[e].val : NULL;
}
const char *arc_tr(const char *key) {
    if (!key) return "";
    size_t n = strlen(key); uint32_t h = tr_hash(key, n);
    const char *v = tr_get(g_tr_active, key, n, h);
    if (!v) v = tr_get(g_tr_fallback, key, n, h);
    return v ? v : key;
}
char *arc_trf(const char *key, ...) {
    const char *fmt = arc_tr(key);
    va_list ap; va_start(ap, key); int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (n < 0) n = 0;
    char *out = (char *)arc_arena_alloc(arc_frame(), (size_t)n + 1, 1);
    if (!out) return (char *)"";
    va_start(ap, key); vsnprintf(out, (size_t)n + 1, fmt, ap); va_end(ap);
    return out;
}
bool arc_tr_has(const char *lang, const char *key) {
    if (!key) return false;
    size_t n = strlen(key); return tr_get(lang ? tr_find_lang(lang) : g_tr_active, key, n, tr_hash(key, n)) != NULL;
}
int arc_tr_languages(const char **names, int max) {
    if (!names) return g_ntr;
    int n = 0; for (int i = 0; i < g_ntr && n < max; i++) names[n++] = g_tr[i].name;
    return n;
}
void arc_tr_clear(void) {
    for (int i = 0; i < g_ntr; i++) {
        for (int b = 0; b < g_tr[i].nblobs; b++) free(g_tr[i].blobs[b]);
        free(g_tr[i].blobs); free(g_tr[i].e); free(g_tr[i].table);
    }
    memset(g_tr, 0, sizeof g_tr); g_ntr = 0; tr_resolve();
}
