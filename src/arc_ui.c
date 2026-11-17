/* ARC UI: immediate-mode widgets called from tick(). A click changes game state on the tick it happens (so
 * replays and agents see the same thing); the widgets' drawing is recorded during the tick and drawn on top of
 * the next frame by the engine, in normal and immediate mode alike. Mouse, touch, keyboard and gamepad. */
#include "arc_internal.h"
#include <ctype.h>

#define UI_MAX_CMDS    4096
#define UI_TEXT_POOL   65536
#define UI_MAX_WIDGETS 512
#define UI_MAX_PANELS  32

enum { UC_RECT, UC_LINES, UC_TEXT, UC_NINE, UC_RICH };
typedef struct { uint8_t kind; float x, y, w, h; uint32_t color; int text; } ui_cmd;   /* text: w = scale (0 = the theme's); UC_NINE: text = skin part */
typedef struct { arc_texture t; float src, dst; } ui_skin;
typedef struct { char label[64]; const char *kind; float x, y, w, h; float value; bool focused; char text[64]; } ui_widget_info;
typedef struct { uint32_t id; float dx, dy; bool used; } ui_panel_rec;
/* 0.7 */
#define UI_GHOST_CMDS 256
#define UI_GHOST_TEXT 4096
#define UI_CAPTIONS 16
typedef struct { uint32_t id, seen; float age; int first, last; float cx, cy; } ui_panel_anim;   /* first/last: cmds of the tick `seen` */
typedef struct { uint32_t id, seen; float hover, press; } ui_widget_anim;
typedef struct { float age, cx, cy; int n, ntext; ui_cmd c[UI_GHOST_CMDS]; char text[UI_GHOST_TEXT]; } ui_ghost;   /* a closing panel */
typedef struct { char text[192]; float seconds, age; uint32_t rgb; } ui_caption;

static struct {
    arc_ui_theme th; bool th_set;
    arc_font font; float font_scale;
    ui_cmd cmds[UI_MAX_CMDS]; int ncmd;
    char text[UI_TEXT_POOL]; int ntext;
    /* this tick's layout */
    bool in_panel; float px, py, pw, cx, cy, row_h; int bg_cmd; uint32_t panel_id;
    int cols, col; float col_y0, col_maxh;
    int id_dup[UI_MAX_WIDGETS]; uint32_t id_seen[UI_MAX_WIDGETS]; int nseen;
    /* interaction */
    uint32_t hot, active, kb_focus; float drag_off_x, drag_off_y; uint32_t drag_panel;
    int focus_index, focus_count, focus_count_last; bool nav; uint32_t nav_id;
    arc_v2 last_mouse; bool mouse_over_ui, mouse_over_last;
    /* for agents */
    ui_widget_info info[UI_MAX_WIDGETS]; int ninfo, ninfo_last; ui_widget_info info_last[UI_MAX_WIDGETS];
    ui_panel_rec panels[UI_MAX_PANELS];
    bool used_this_tick;
    bool disabled, dim;
    ui_skin skin[ARC_UI_SKIN_COUNT];   /* arc_ui_enabled(false) / the widget being drawn now is disabled */
    /* 0.7: scale, layout navigation, back, rich text */
    float scale; bool nav_on, back, rich; char focused_now[64], focused_done[64];
    int hold_dir, stick_dir, lr; float hold_t;
    float fr[UI_MAX_WIDGETS][4]; uint8_t fr_lr[UI_MAX_WIDGETS]; int nfr;   /* this tick's focusable rects (focus order) */
    float fr_last[UI_MAX_WIDGETS][4]; uint8_t fr_lr_last[UI_MAX_WIDGETS]; int nfr_last;
    /* 0.7: animation */
    uint32_t serial;
    ui_panel_anim pan[UI_MAX_PANELS]; ui_widget_anim wan[128];
    ui_cmd prev_cmds[UI_MAX_CMDS]; int nprev; char prev_text[UI_TEXT_POOL];
    ui_ghost ghost[4];
    /* 0.7: captions and screen transitions */
    ui_caption cap[UI_CAPTIONS]; int ncap; float cap_scale; uint32_t cap_bg;
    int tr_kind; float tr_secs, tr_t; uint32_t tr_rgb; bool tr_on, tr_covered;
} U;

/* 0.10.1 layout state (not part of saved UI state) */
static struct { float ref_w, ref_h, smin, smax; bool anchored; float scale_save, shrink, ds_full; uint32_t anchored_id;
                struct { uint32_t id; float h; } last[32]; } UL;
static const arc_ui_theme UI_DEFAULT = {
    .scale = 2, .padding = 6, .spacing = 4,
    .panel = 0xD0181C24, .title = 0xFF2C3E58, .widget = 0xFF2A303C, .hover = 0xFF3A4458, .active = 0xFF4E6A96,
    .accent = 0xFF6FA8FF, .text = 0xFFE8ECF4, .text_dim = 0xFF98A0B0, .focus = 0xFFFFD060,
};
void arc_ui_set_theme(const arc_ui_theme *t) { if (t) { U.th = *t; U.th_set = true; } else U.th_set = false; }
arc_ui_theme arc_ui_get_theme(void) { return U.th_set ? U.th : UI_DEFAULT; }
void arc_ui_set_font(arc_font f, float scale) { U.font = f; U.font_scale = scale > 0 ? scale : 1; }
void arc_ui_set_skin(int part, arc_texture t, float src_border, float dst_border) {
    if (part < 0 || part >= ARC_UI_SKIN_COUNT) return;
    U.skin[part].t = t; U.skin[part].src = src_border; U.skin[part].dst = dst_border;
}
static int skin_of(int part) {   /* the skin a part draws with, or -1 (hover falls back to widget, active to hover) */
    for (int p = part; p >= 0; p = p == ARC_UI_SKIN_ACTIVE ? ARC_UI_SKIN_HOVER : p == ARC_UI_SKIN_HOVER ? ARC_UI_SKIN_WIDGET : -1)
        if (U.skin[p].t.id) return p;
    return -1;
}
static const arc_ui_theme *th(void) { return U.th_set ? &U.th : &UI_DEFAULT; }
static float uis(void) { return U.scale > 0 ? U.scale : 1.0f; }   /* 0.7 UI scale (1 = none: multiplying by 1 changes nothing) */
static float ds(void) { return arc_dpi_scale() * uis(); }
static float pad(void) { return th()->padding * ds(); }
static float text_h(void) { return U.font.id ? (float)arc_font_size(U.font) * U.font_scale * uis() : 8.0f * th()->scale * ds(); }

/* ---- recording ---- */
static bool g_rec_raw;   /* 0.10.2: re-recording window-space commands (no viewport offset) */
static void rec(int kind, float x, float y, float w, float h, uint32_t color, const char *s) {
    if (U.ncmd >= UI_MAX_CMDS) return;
    int vx, vy; if (!g_rec_raw && arc__viewport_cur(&vx, &vy, NULL, NULL)) { x += (float)vx; y += (float)vy; }   /* 0.10.2: drawn over the window later */
    ui_cmd *c = &U.cmds[U.ncmd++]; c->kind = (uint8_t)kind; c->x = x; c->y = y; c->w = w; c->h = h; c->color = color; c->text = -1;
    if (s) {
        int n = (int)strlen(s);
        if (U.ntext + n + 1 <= UI_TEXT_POOL) { memcpy(U.text + U.ntext, s, (size_t)n + 1); c->text = U.ntext; U.ntext += n + 1; }
        else U.ncmd--;
    }
}
static void rect(float x, float y, float w, float h, uint32_t c) { rec(UC_RECT, x, y, w, h, c, NULL); }
static void box(int part, float x, float y, float w, float h, uint32_t c) {   /* a skinned part, else its colour */
    int s = skin_of(part);
    if (s < 0) { rect(x, y, w, h, c); return; }
    rec(UC_NINE, x, y, w, h, U.dim ? arc_rgba(0xFFFFFF, 0.5f) : 0xFFFFFF, NULL);
    if (U.ncmd > 0 && U.cmds[U.ncmd - 1].kind == UC_NINE) U.cmds[U.ncmd - 1].text = s;
}
static float base_scale(void) { return U.font.id ? U.font_scale * uis() : th()->scale * ds(); }
static bool is_rich(const char *s) { return U.rich && strchr(s, '[') != NULL; }
static float width_at(const char *s, float sc) {
    if (is_rich(s)) return arc_rich_width(U.font, sc, s);
    return U.font.id ? arc_font_width(U.font, sc, 0, s) : arc_text_width(sc, s);
}
static float height_at(float sc) { return U.font.id ? (float)arc_font_size(U.font) * sc : 8.0f * sc; }
/* text in a box maxw wide: shrunk (down to 60%) if it doesn't fit, then cut short with "..." (at the start with
   keep_end, so the end being typed stays in view) */
static void text_fit_ex(float x, float y, float maxw, uint32_t c, const char *str, bool center, bool keep_end) {
    float fs = base_scale(), w = width_at(str, fs), sc = fs;
    char buf[256], plain[256]; const char *show = str;
    if (w > maxw && maxw > 0) {
        sc = fs * maxw / w;
        if (sc < fs * 0.6f && is_rich(str)) { arc_rich_strip(str, plain, sizeof plain); str = plain; }   /* too long even shrunk: plain, cut short */
        if (sc < fs * 0.6f) {
            sc = fs * 0.6f;
            size_t n = strlen(str), keep = 0; float dots = width_at("...", sc);
            if (keep_end) {
                size_t from = n;
                for (size_t i = n; i-- > 0;) {
                    if (((unsigned char)str[i] & 0xC0) == 0x80) continue;
                    if (n - i >= sizeof buf - 4 || width_at(str + i, sc) + dots > maxw) break;
                    from = i;
                }
                memcpy(buf, "...", 3); memcpy(buf + 3, str + from, n - from + 1); show = buf;
                goto fitted;
            }
            for (size_t i = 1; i <= n && i < sizeof buf - 4; i++) {
                if (((unsigned char)str[i] & 0xC0) == 0x80) continue;   /* whole UTF-8 characters only */
                memcpy(buf, str, i); buf[i] = 0;
                if (width_at(buf, sc) + dots > maxw) break;
                keep = i;
            }
            memcpy(buf, str, keep); memcpy(buf + keep, "...", 4); show = buf;
        }
    fitted:
        y += (height_at(fs) - height_at(sc)) * 0.5f;
        w = width_at(show, sc);
    }
    if (center) x += (maxw - w) * 0.5f;
    rec(is_rich(show) ? UC_RICH : UC_TEXT, x, y, sc == fs ? 0 : sc, 0, U.dim ? th()->text_dim : c, show);
}
static void text_fit(float x, float y, float maxw, uint32_t c, const char *str, bool center) { text_fit_ex(x, y, maxw, c, str, center, false); }
/* the visible part of a label: "Play##menu" shows "Play" */
static const char *visible(const char *label, char *buf, size_t cap) {
    const char *h = strstr(label, "##"); size_t n = h ? (size_t)(h - label) : strlen(label);
    if (n >= cap) n = cap - 1;
    memcpy(buf, label, n); buf[n] = 0; return buf;
}
static uint32_t hash_str(uint32_t h, const char *s) { while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }
static uint32_t widget_id(const char *label) {
    uint32_t h = hash_str(U.panel_id ^ 2166136261u, label);
    for (int i = 0; i < U.nseen; i++) if (U.id_seen[i] == h) { U.id_dup[i]++; h = h * 31u + (uint32_t)U.id_dup[i]; break; }   /* same label twice */
    if (U.nseen < UI_MAX_WIDGETS) { U.id_seen[U.nseen] = h; U.id_dup[U.nseen++] = 0; }
    return h ? h : 1;
}

/* engine hooks */
static void nav_begin(void);
static void ux_tick_begin(void);
void arc__ui_tick_begin(void) {
    if (th()->anim > 0) {   /* keep last tick's drawing: a panel that stops being drawn fades out from it */
        memcpy(U.prev_cmds, U.cmds, sizeof(ui_cmd) * (size_t)U.ncmd); U.nprev = U.ncmd;
        memcpy(U.prev_text, U.text, (size_t)U.ntext);
    }
    U.serial++;
    if (UL.ref_w > 0 && UL.ref_h > 0) {   /* 0.10.1 arc_ui_scale_auto: follows the window */
        float s = arc_minf((float)arc_width() / UL.ref_w, (float)arc_height() / UL.ref_h);
        s = arc_clampf(s, UL.smin, UL.smax);
        if (U.font.id && !arc_font_is_sdf(U.font)) {   /* whole-pixel text sizes for a bitmap font */
            float unit = (float)arc_font_size(U.font) * U.font_scale; float px = floorf(unit * s + 0.5f);
            if (px >= 1) s = arc_clampf(px / unit, UL.smin, UL.smax);
        }
        U.scale = s;
    }
    memcpy(U.fr_last, U.fr, sizeof U.fr[0] * (size_t)U.nfr); memcpy(U.fr_lr_last, U.fr_lr, (size_t)U.nfr); U.nfr_last = U.nfr; U.nfr = 0;
    U.back = !U.kb_focus && (arc_key_pressed(ARC_KEY_ESCAPE) || arc_key_pressed(ARC_PAD_B));
    U.lr = 0;
    ux_tick_begin();
    U.ncmd = 0; U.ntext = 0; U.nseen = 0;
    memcpy(U.info_last, U.info, sizeof(ui_widget_info) * (size_t)U.ninfo); U.ninfo_last = U.ninfo; U.ninfo = 0;
    U.focus_count_last = U.focus_count; U.focus_count = 0;
    U.mouse_over_last = U.mouse_over_ui; U.mouse_over_ui = false;
    U.used_this_tick = false; U.disabled = U.dim = false;
    arc_v2 m = arc_mouse();
    if (fabsf(m.x - U.last_mouse.x) + fabsf(m.y - U.last_mouse.y) > 0.5f) U.nav = false;   /* the mouse moved: pointer mode */
    U.last_mouse = m;
    if (U.nav_on) nav_begin();
    else if (U.focus_count_last > 0) {
        bool up = arc_key_pressed(ARC_KEY_UP) || arc_key_pressed(ARC_PAD_UP), down = arc_key_pressed(ARC_KEY_DOWN) || arc_key_pressed(ARC_PAD_DOWN) || arc_key_pressed(ARC_KEY_TAB);
        if ((up || down) && !U.kb_focus) {
            if (!U.nav) { U.nav = true; if (U.focus_index >= U.focus_count_last) U.focus_index = 0; }
            else U.focus_index = (U.focus_index + (down ? 1 : U.focus_count_last - 1)) % U.focus_count_last;
        }
    }
}
uint64_t arc__ui_test_cmd_hash(void) {          /* self-test: what the UI will draw (commands and their text) */
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < U.ncmd; i++) {
        const ui_cmd *c = &U.cmds[i]; float f[4] = { c->x, c->y, c->w, c->h };
        const uint8_t *b = (const uint8_t *)f; for (size_t k = 0; k < sizeof f; k++) { h ^= b[k]; h *= 1099511628211ull; }
        h ^= c->kind; h *= 1099511628211ull; h ^= c->color; h *= 1099511628211ull;
        if (c->text >= 0 && c->kind != UC_NINE) for (const char *t = U.text + c->text; *t; t++) { h ^= (uint8_t)*t; h *= 1099511628211ull; }
    }
    return h ^ (uint64_t)U.ncmd;
}
void arc__ui_render(void) {
    if (!U.ncmd) return;
    arc_blend b = arc_get_blend(); arc_set_blend(ARC_BLEND_ALPHA);
    for (int i = 0; i < U.ncmd; i++) {
        const ui_cmd *c = &U.cmds[i];
        if (c->kind == UC_RECT) arc_draw_rect(c->x, c->y, c->w, c->h, c->color);
        else if (c->kind == UC_LINES) arc_draw_rect_lines(c->x, c->y, c->w, c->h, c->color);
        else if (c->kind == UC_NINE) { const ui_skin *s = &U.skin[c->text]; arc_draw_nine(s->t, c->x, c->y, c->w, c->h, s->src, s->dst * ds(), c->color); }
        else if (c->text >= 0 && c->kind == UC_RICH) arc_rich_draw(U.font, c->x, c->y, c->w > 0 ? c->w : base_scale(), c->color, 0, U.text + c->text);
        else if (c->text >= 0) {
            if (U.font.id) arc_font_draw_ex(U.font, c->x, c->y, c->w > 0 ? c->w : U.font_scale * uis(), 0, c->color, U.text + c->text);
            else arc_draw_text(c->x, c->y, c->w > 0 ? c->w : th()->scale * ds(), c->color, U.text + c->text);
        }
    }
    arc_set_blend(b);
}
bool arc_ui_wants_mouse(void) { return U.mouse_over_last || U.mouse_over_ui || U.active != 0; }
bool arc_ui_wants_keyboard(void) { return U.kb_focus != 0 || (U.nav && (!U.nav_on || U.focus_count_last > 0)); }

/* ---- layout ---- */
static ui_panel_rec *panel_rec(uint32_t id) {
    for (int i = 0; i < UI_MAX_PANELS; i++) if (U.panels[i].used && U.panels[i].id == id) return &U.panels[i];
    for (int i = 0; i < UI_MAX_PANELS; i++) if (!U.panels[i].used) { U.panels[i].used = true; U.panels[i].id = id; U.panels[i].dx = U.panels[i].dy = 0; return &U.panels[i]; }
    return NULL;
}
static bool mouse_in(float x, float y, float w, float h) { arc_v2 m = arc_mouse(); return m.x >= x && m.y >= y && m.x < x + w && m.y < y + h; }
static void panel_anim_end(float h);
void arc_ui_begin(const char *title, float x, float y, float w) { U.dim = false;
    if (U.in_panel) arc_ui_end();
    U.used_this_tick = true;
    U.panel_id = hash_str(2166136261u, title ? title : "");
    ui_panel_rec *pr = panel_rec(U.panel_id);
    float p = pad();
    if (pr && UL.anchored) pr->dx = pr->dy = 0;   /* 0.10.1 anchored panels stay where their anchor puts them */
    if (pr) { x += pr->dx; y += pr->dy; }
    U.in_panel = true; U.px = x; U.py = y; U.pw = w * ds(); U.cols = 1; U.col = 0; U.col_maxh = 0;
    U.bg_cmd = U.ncmd < UI_MAX_CMDS ? U.ncmd : -1; box(ARC_UI_SKIN_PANEL, x, y, U.pw, 0, th()->panel);
    U.cy = y + p;
    if (title && *title) {
        char vis[128]; visible(title, vis, sizeof vis);
        float h = text_h() + p * 2;
        box(ARC_UI_SKIN_TITLE, x, y, U.pw, h, th()->title);
        text_fit(x + p, y + p, U.pw - 2 * p, th()->text, vis, false);
        if (pr && !UL.anchored) {   /* drag the title bar to move the panel (kept until the program ends) */
            uint32_t tid = U.panel_id ^ 0x7177u;
            if (arc_key_pressed(ARC_MOUSE_LEFT) && mouse_in(x, y, U.pw, h)) { U.drag_panel = tid; arc_v2 m = arc_mouse(); U.drag_off_x = m.x - pr->dx; U.drag_off_y = m.y - pr->dy; }
            if (U.drag_panel == tid && arc_key_down(ARC_MOUSE_LEFT)) { arc_v2 m = arc_mouse(); pr->dx = m.x - U.drag_off_x; pr->dy = m.y - U.drag_off_y; }
        }
        U.cy = y + h + p;
    }
    U.cx = x + p; U.col_y0 = U.cy;
}
void arc_ui_end(void) {
    if (!U.in_panel) return;
    if (U.col) { U.cy = U.col_y0 + U.col_maxh + th()->spacing * ds(); U.col = 0; }
    float h = U.cy - U.py + pad() - th()->spacing * ds();
    if (U.bg_cmd >= 0 && U.bg_cmd < U.ncmd) U.cmds[U.bg_cmd].h = h;
    if (mouse_in(U.px, U.py, U.pw, h)) U.mouse_over_ui = true;
    U.in_panel = false;
    if (th()->anim > 0) panel_anim_end(h);
    if (UL.anchored) {   /* 0.10.1: remember its height (at full size) for the anchor and the shrink next tick */
        int slot = -1;
        for (int i = 0; i < 32 && slot < 0; i++) if (UL.last[i].id == UL.anchored_id) slot = i;
        for (int i = 0; i < 32 && slot < 0; i++) if (!UL.last[i].id) slot = i;
        if (slot < 0) slot = (int)(UL.anchored_id % 32u);
        UL.last[slot].id = UL.anchored_id; UL.last[slot].h = h / ((UL.shrink > 0 ? UL.shrink : 1.0f) * (UL.ds_full > 0 ? UL.ds_full : 1.0f));   /* in UI units */
        U.scale = UL.scale_save; UL.anchored = false;
    }
}
void arc_ui_columns(int n) {
    if (U.col) { U.cy = U.col_y0 + U.col_maxh + th()->spacing * ds(); U.col = 0; }
    U.cols = arc_clampi(n, 1, 8); U.col_maxh = 0; U.col_y0 = U.cy;
}
/* the next widget's rect */
static void next_rect(float h, float *x, float *y, float *w) {
    if (!U.in_panel) arc_ui_begin("", 10, 10, 260);
    float p = pad(), sp = th()->spacing * ds(), inner = U.pw - p * 2;
    float cw = (inner - sp * ((float)(U.cols - 1))) / (float)U.cols;
    if (U.col == 0) U.col_y0 = U.cy;
    *x = U.px + p + (float)U.col * (cw + sp); *y = U.col_y0; *w = cw;
    U.col_maxh = arc_maxf(U.col_maxh, h);
    if (++U.col >= U.cols) { U.cy = U.col_y0 + U.col_maxh + sp; U.col = 0; U.col_maxh = 0; }
}
static float row_h(void) { return text_h() + pad() * 2; }

/* focus and pointer state of one widget */
typedef struct { uint32_t id; bool hover, pressed, released_on, focused, activate; int index; } wstate;
static wstate interact(const char *label, float x, float y, float w, float h, bool focusable) {
    wstate s; memset(&s, 0, sizeof s); s.index = -1;
    s.id = widget_id(label);
    s.hover = mouse_in(x, y, w, h);
    if (s.hover) U.mouse_over_ui = true;
    U.dim = U.disabled;
    if (U.disabled) { s.hover = false; if (U.active == s.id) U.active = 0; if (U.kb_focus == s.id) U.kb_focus = 0; return s; }   /* shown, not usable */
    if (s.hover) U.hot = s.id;
    if (s.hover && arc_key_pressed(ARC_MOUSE_LEFT) && !U.drag_panel) { U.active = s.id; s.pressed = true; if (U.kb_focus && U.kb_focus != s.id) U.kb_focus = 0; }
    if (U.active == s.id && arc_key_released(ARC_MOUSE_LEFT) && s.hover) s.released_on = true;
    if (focusable) {
        int idx = U.focus_count++; s.index = idx;
        if (idx < UI_MAX_WIDGETS) { U.fr[idx][0] = x; U.fr[idx][1] = y; U.fr[idx][2] = w; U.fr[idx][3] = h; U.fr_lr[idx] = 0; U.nfr = idx + 1; }
        if (U.nav && idx == U.focus_index) {
            s.focused = true; U.nav_id = s.id;
            s.activate = arc_key_pressed(ARC_KEY_ENTER) || arc_key_pressed(ARC_KEY_SPACE) || arc_key_pressed(ARC_PAD_A) || arc_key_pressed(ARC_KEY_KP_ENTER);
        }
    }
    return s;
}
static void record_info(const char *label, const char *kind, float x, float y, float w, float h, float value, bool focused, const char *txt) {
    if (U.ninfo >= UI_MAX_WIDGETS) return;
    ui_widget_info *i = &U.info[U.ninfo++];
    visible(label, i->label, sizeof i->label); i->kind = kind;
    if (U.rich && strchr(i->label, '[')) { char t[sizeof i->label]; arc_rich_strip(i->label, t, sizeof t); memcpy(i->label, t, sizeof t); }
    if (focused) memcpy(U.focused_now, i->label, sizeof U.focused_now);
    i->x = x; i->y = y; i->w = w; i->h = h; i->value = value; i->focused = focused;
    snprintf(i->text, sizeof i->text, "%s", txt ? txt : "");
}
static uint32_t lerp_argb(uint32_t a, uint32_t b, float t) {
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) { float u = (float)((a >> sh) & 255), v = (float)((b >> sh) & 255); r |= (uint32_t)(u + (v - u) * t + 0.5f) << sh; }
    return r;
}
static ui_widget_anim *widget_anim(uint32_t id) {
    int old = 0;
    for (int i = 0; i < 128; i++) { if (U.wan[i].id == id) return &U.wan[i]; if (U.wan[i].seen < U.wan[old].seen) old = i; }
    memset(&U.wan[old], 0, sizeof U.wan[old]); U.wan[old].id = id; return &U.wan[old];
}
static float approach(float v, float target, float step) { return v < target ? arc_minf(target, v + step) : arc_maxf(target, v - step); }
static void frame(float x, float y, float w, float h, const wstate *s, bool on) {
    const arc_ui_theme *t = th();
    uint32_t c = on ? t->active : (U.active == s->id || s->hover) ? t->hover : t->widget;
    if (t->anim > 0) {   /* 0.7: hover fades in and out; pressing sinks the widget a little */
        ui_widget_anim *a = widget_anim(s->id);
        if (a->seen != U.serial) {
            a->hover = approach(a->hover, (U.active == s->id || s->hover) ? 1.0f : 0.0f, arc_dt() / t->anim);
            a->press = approach(a->press, U.active == s->id && arc_key_down(ARC_MOUSE_LEFT) ? 1.0f : 0.0f, arc_dt() / (t->anim * 0.5f));
            a->seen = U.serial;
        }
        if (!on) c = lerp_argb(t->widget, t->hover, arc_ease_out_quad(a->hover));
        float in = arc_ease_out_quad(a->press) * arc_minf(w, h) * 0.08f;
        x += in; y += in; w -= 2 * in; h -= 2 * in;
    }
    if (U.dim) c = (c & 0xFFFFFFu) | ((((c >> 24) ? (c >> 24) : 255) / 2) << 24);   /* disabled: half see-through */
    int part = on ? ARC_UI_SKIN_ACTIVE : (U.active == s->id || s->hover) ? ARC_UI_SKIN_HOVER : ARC_UI_SKIN_WIDGET;
    if (skin_of(part) >= 0) box(part, x, y, w, h, c); else rect(x, y, w, h, c);
    if (s->focused) rec(UC_LINES, x - 1, y - 1, w + 2, h + 2, t->focus, NULL);
}

/* ---- widgets ---- */
void arc_ui_label(const char *s) {
    float x, y, w; float h = text_h() + pad() * 0.5f;
    next_rect(h, &x, &y, &w);
    U.dim = U.disabled;
    char vis[256]; text_fit(x, y + pad() * 0.25f, w, th()->text, visible(s, vis, sizeof vis), false);
    record_info(s, "label", x, y, w, h, 0, false, NULL);
}
/* 0.8.2: a paragraph wrapped to the panel's content width (the column's, with arc_ui_columns), in the UI's font and
   scale; as tall as it needs. Plain text (no rich markup, no "##" ids). */
typedef struct { float x, y, lh; uint32_t c; } ui_wrap;
static void ui_wrap_emit(void *p, const char *s, int n) {
    ui_wrap *w = (ui_wrap *)p; char line[512]; if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
    memcpy(line, s, (size_t)n); line[n] = 0;
    if (n) rec(UC_TEXT, w->x, w->y, 0, 0, w->c, line);
    w->y += w->lh;
}
static float ui_wrap_measure(const void *c, const char *s) { (void)c; return width_at(s, base_scale()); }
static void ui_wrap_count(void *u, const char *s, int n) { (void)u; (void)s; (void)n; }
void arc_ui_text_wrapped(const char *text) {
    if (!U.in_panel) arc_ui_begin("", 10, 10, 260);
    float p = pad(), sp = th()->spacing * ds(), inner = U.pw - p * 2;
    float cw = (inner - sp * ((float)(U.cols - 1))) / (float)U.cols;
    bool rich = U.rich; U.rich = false;   /* measured and drawn plain */
    float lh = U.font.id ? height_at(base_scale()) : height_at(base_scale()) * 1.25f;
    int lines = arc__wrap(text, cw, ui_wrap_measure, NULL, ui_wrap_count, NULL);
    float h = (lines > 0 ? (float)(lines - 1) * lh + height_at(base_scale()) : 0) + pad() * 0.5f;
    float x, y, w; next_rect(h, &x, &y, &w);
    U.dim = U.disabled;
    ui_wrap uw = { x, y + pad() * 0.25f, lh, U.dim ? th()->text_dim : th()->text };
    arc__wrap(text, w, ui_wrap_measure, NULL, ui_wrap_emit, &uw);
    U.rich = rich;
    record_info(text, "text", x, y, w, h, 0, false, NULL);
}
void arc_ui_labelf(const char *fmt, ...) {
    char buf[256]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    arc_ui_label(buf);
}
void arc_ui_separator(void) {
    float x, y, w; next_rect(pad(), &x, &y, &w);
    rect(x, y + pad() * 0.5f - ds() * 0.5f, w, ds(), th()->text_dim);
}
void arc_ui_enabled(bool on) { U.disabled = !on; }
bool arc_ui_button_ex(const char *label, bool enabled) {
    bool was = U.disabled; U.disabled = was || !enabled;
    float x, y, w, h = row_h(); next_rect(h, &x, &y, &w);
    wstate s = interact(label, x, y, w, h, true);
    frame(x, y, w, h, &s, false);
    char vis[128]; visible(label, vis, sizeof vis);
    text_fit(x + pad(), y + pad(), w - 2 * pad(), th()->text, vis, true);
    bool hit = !U.disabled && (s.released_on || s.activate);
    record_info(label, U.disabled ? "button(disabled)" : "button", x, y, w, h, hit, s.focused, NULL);
    U.disabled = U.dim = was;
    return hit;
}
bool arc_ui_button(const char *label) { return arc_ui_button_ex(label, true); }
bool arc_ui_toggle(const char *label, bool *v) {
    float x, y, w, h = row_h(); next_rect(h, &x, &y, &w);
    wstate s = interact(label, x, y, w, h, true);
    bool changed = false;
    if (v && (s.released_on || s.activate)) { *v = !*v; changed = true; }
    frame(x, y, w, h, &s, false);
    float b = text_h();
    rect(x + pad(), y + pad(), b, b, th()->panel);
    if (v && *v) rect(x + pad() + b * 0.2f, y + pad() + b * 0.2f, b * 0.6f, b * 0.6f, th()->accent);
    char vis[128]; text_fit(x + pad() * 2 + b, y + pad(), w - pad() * 3 - b, th()->text, visible(label, vis, sizeof vis), false);
    record_info(label, "toggle", x, y, w, h, (float)(v && *v ? 1 : 0), s.focused, NULL);
    return changed;
}
static bool slider_core(const char *label, float *v, float lo, float hi, float step, const char *shown) {
    float x, y, w, h = row_h(); next_rect(h, &x, &y, &w);
    wstate s = interact(label, x, y, w, h, true);
    bool changed = false;
    if (v && hi > lo) {
        float old = *v;
        if (U.active == s.id && arc_key_down(ARC_MOUSE_LEFT)) {
            float t = arc_clampf((arc_mouse().x - x) / w, 0, 1);
            *v = lo + (hi - lo) * t;
            if (step > 0) *v = lo + roundf((*v - lo) / step) * step;
        }
        if (s.focused) {
            float k = step > 0 ? step : (hi - lo) / 20.0f;
            if (U.nav_on) *v += k * (float)U.lr;
            else {
                if (arc_key_pressed(ARC_KEY_LEFT) || arc_key_pressed(ARC_PAD_LEFT)) *v -= k;
                if (arc_key_pressed(ARC_KEY_RIGHT) || arc_key_pressed(ARC_PAD_RIGHT)) *v += k;
            }
        }
        if (s.index >= 0 && s.index < UI_MAX_WIDGETS) U.fr_lr[s.index] = 1;   /* LEFT/RIGHT belong to it */
        *v = arc_clampf(*v, lo, hi);
        changed = *v != old;
    }
    frame(x, y, w, h, &s, false);
    float t = v && hi > lo ? (*v - lo) / (hi - lo) : 0;
    if (skin_of(ARC_UI_SKIN_WIDGET) >= 0) {   /* skinned: a see-through accent bar inside the frame */
        float i = h * 0.2f, fw = (w - 2 * i) * t;
        rect(x + i, y + i, fw, h - 2 * i, (th()->accent & 0xFFFFFFu) | 0x70000000u);
        rect(x + i + fw - ds(), y + i, ds() * 2, h - 2 * i, th()->accent);
    } else {
        rect(x, y, w * t, h, th()->active);
        rect(x + w * t - ds(), y, ds() * 2, h, th()->accent);
    }
    char vis[128], buf[192]; snprintf(buf, sizeof buf, "%s %s", visible(label, vis, sizeof vis), shown);
    text_fit(x + pad(), y + pad(), w - 2 * pad(), th()->text, buf, false);
    record_info(label, "slider", x, y, w, h, v ? *v : 0, s.focused, NULL);
    return changed;
}
bool arc_ui_slider(const char *label, float *v, float lo, float hi) {
    char shown[32]; snprintf(shown, sizeof shown, "%.3g", v ? *v : 0);
    return slider_core(label, v, lo, hi, 0, shown);
}
bool arc_ui_slider_int(const char *label, int *v, int lo, int hi) {
    if (!v) return false;
    float f = (float)*v; char shown[32]; snprintf(shown, sizeof shown, "%d", *v);
    bool c = slider_core(label, &f, (float)lo, (float)hi, 1, shown);
    int nv = arc_clampi((int)lroundf(f), lo, hi); c = nv != *v; *v = nv; return c;
}
bool arc_ui_select(const char *label, int *index, const char *const *items, int count) {
    float x, y, w, h = row_h(); next_rect(h, &x, &y, &w);
    wstate s = interact(label, x, y, w, h, true);
    bool changed = false;
    if (index && count > 0) {
        int old = *index;
        if (s.released_on) *index = arc_mouse().x < x + w * 0.5f ? *index - 1 : *index + 1;
        if (s.focused) {
            if (U.nav_on) *index += U.lr + (s.activate ? 1 : 0);
            else {
                if (arc_key_pressed(ARC_KEY_LEFT) || arc_key_pressed(ARC_PAD_LEFT)) (*index)--;
                if (arc_key_pressed(ARC_KEY_RIGHT) || arc_key_pressed(ARC_PAD_RIGHT) || s.activate) (*index)++;
            }
        }
        if (s.index >= 0 && s.index < UI_MAX_WIDGETS) U.fr_lr[s.index] = 1;
        *index = ((*index % count) + count) % count;
        changed = *index != old;
    }
    frame(x, y, w, h, &s, false);
    char vis[128], buf[256]; visible(label, vis, sizeof vis);
    const char *item = index && count > 0 ? items[*index] : "";
    snprintf(buf, sizeof buf, "%s%s< %s >", vis, *vis ? " " : "", item);
    text_fit(x + pad(), y + pad(), w - 2 * pad(), th()->text, buf, false);
    record_info(label, "select", x, y, w, h, index ? (float)*index : 0, s.focused, item);
    return changed;
}
bool arc_ui_list(const char *label, int *selected, const char *const *items, int count) {
    bool changed = false;
    char vis[128]; visible(label, vis, sizeof vis);
    if (*vis) arc_ui_label(vis);
    for (int i = 0; i < count; i++) {
        char id[160]; snprintf(id, sizeof id, "%s##%s.%d", items[i], label, i);
        float x, y, w, h = row_h(); next_rect(h, &x, &y, &w);
        wstate s = interact(id, x, y, w, h, true);
        bool on = selected && *selected == i;
        if (selected && (s.released_on || s.activate) && *selected != i) { *selected = i; changed = true; on = true; }
        frame(x, y, w, h, &s, on);
        text_fit(x + pad(), y + pad(), w - 2 * pad(), on ? th()->text : th()->text_dim, items[i], false);
        record_info(id, "item", x, y, w, h, on, s.focused, NULL);
    }
    return changed;
}
bool arc_ui_text(const char *label, char *buf, int cap) {
    float x, y, w, h = row_h(); next_rect(h, &x, &y, &w);
    wstate s = interact(label, x, y, w, h, true);
    if (s.pressed || s.activate) U.kb_focus = s.id;
    bool editing = U.kb_focus == s.id, done = false;
    if (editing && buf && cap > 1) {
        const char *in = arc_text_input();
        size_t n = strlen(buf), add = strlen(in);
        if (add && n + add < (size_t)cap) { memcpy(buf + n, in, add + 1); n += add; }
        if (arc_key_pressed(ARC_KEY_BACKSPACE) && n > 0) { n--; while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--; buf[n] = 0; }
        if (arc_key_pressed(ARC_KEY_ENTER) || arc_key_pressed(ARC_KEY_KP_ENTER) || arc_key_pressed(ARC_PAD_A)) { done = !s.activate; if (done) U.kb_focus = 0; }
        if (arc_key_pressed(ARC_KEY_ESCAPE)) U.kb_focus = 0;
        if (arc_key_pressed(ARC_MOUSE_LEFT) && !s.hover) U.kb_focus = 0;
    }
    frame(x, y, w, h, &s, editing);
    char vis[128], shown[320]; visible(label, vis, sizeof vis);
    bool blink = editing && (arc_tick_count() / 30) % 2 == 0;
    snprintf(shown, sizeof shown, "%s%s%s%s", vis, *vis ? ": " : "", buf ? buf : "", blink ? "_" : editing ? " " : "");
    text_fit_ex(x + pad(), y + pad(), w - 2 * pad(), th()->text, shown, false, editing);
    record_info(label, "text", x, y, w, h, editing, s.focused, buf);
    return done;
}
void arc_ui_progress(float fraction, const char *label) {
    float x, y, w, h = row_h(); next_rect(h, &x, &y, &w);
    float f = arc_clampf(fraction, 0, 1);
    if (skin_of(ARC_UI_SKIN_WIDGET) >= 0) { float i = h * 0.2f; box(ARC_UI_SKIN_WIDGET, x, y, w, h, th()->widget); rect(x + i, y + i, (w - 2 * i) * f, h - 2 * i, (th()->accent & 0xFFFFFFu) | 0xA0000000u); }
    else { rect(x, y, w, h, th()->widget); rect(x, y, w * f, h, th()->accent); }
    U.dim = U.disabled;
    if (label && *label) { char vis[128]; visible(label, vis, sizeof vis); text_fit(x + pad(), y + pad(), w - 2 * pad(), th()->text, vis, true); }
    record_info(label ? label : "", "progress", x, y, w, h, f, false, NULL);
}

/* every tunable variable as a slider, in its own movable panel */
int arc__var_info(int i, const char **name, float **ptr, float *initial);
void arc_ui_inspector(float x, float y) {
    bool reopen = U.in_panel; (void)reopen;
    arc_ui_begin("Tunables##arc", x, y, 340);
    const char *name; float *ptr, init; int n = 0;
    for (int i = 0; arc__var_info(i, &name, &ptr, &init); i++, n++) {
        float lo = init < 0 ? init * 3 : 0, hi = init > 0 ? init * 3 : init < 0 ? 0 : 1;
        if (*ptr < lo) lo = *ptr;
        if (*ptr > hi) hi = *ptr;
        arc_ui_slider(name, ptr, lo, hi);
    }
    if (!n) arc_ui_label("(no arc_var tunables)");
    arc_ui_end();
}

/* agents: the widgets of the last tick, and clicking one by label */
void arc__ui_json(arc_json *j, const char *key) {   /* 0.9.0: the widget list as a JSON array (for `ui` and `frame`); key NULL = unnamed */
    if (key) arc_json_arr_k(j, key); else arc_json_arr(j);
    for (int i = 0; i < U.ninfo_last; i++) {
        const ui_widget_info *w = &U.info_last[i];
        arc_json_obj(j);
        arc_json_str_k(j, "label", w->label); arc_json_str_k(j, "kind", w->kind);
        arc_json_num_k(j, "x", w->x); arc_json_num_k(j, "y", w->y); arc_json_num_k(j, "w", w->w); arc_json_num_k(j, "h", w->h);
        arc_json_num_k(j, "value", w->value);
        if (w->text[0]) arc_json_str_k(j, "text", w->text);
        if (w->focused) arc_json_bool_k(j, "focused", true);
        arc_json_end(j);
    }
    arc_json_end(j);
}
void arc__ui_print(void) {
    arc_json j; memset(&j, 0, sizeof j);
    arc_json_arr(&j);
    for (int i = 0; i < U.ninfo_last; i++) {
        const ui_widget_info *w = &U.info_last[i];
        arc_json_obj(&j);
        arc_json_str_k(&j, "label", w->label); arc_json_str_k(&j, "kind", w->kind);
        arc_json_num_k(&j, "x", w->x); arc_json_num_k(&j, "y", w->y); arc_json_num_k(&j, "w", w->w); arc_json_num_k(&j, "h", w->h);
        arc_json_num_k(&j, "value", w->value);
        if (w->text[0]) arc_json_str_k(&j, "text", w->text);
        if (w->focused) arc_json_bool_k(&j, "focused", true);
        arc_json_end(&j);
    }
    arc_json_end(&j);
    printf("ARC ui %s\n", j.buf ? j.buf : "[]"); fflush(stdout); free(j.buf);
}
static bool label_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    return !*a && !*b;
}
bool arc__ui_find(const char *label, float *cx, float *cy) {
    const ui_widget_info *src = U.ninfo_last ? U.info_last : U.info; int n = U.ninfo_last ? U.ninfo_last : U.ninfo;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            const ui_widget_info *w = &src[i];
            if (!strcmp(w->kind, "label") || !strcmp(w->kind, "progress")) continue;
            if (pass == 0 ? label_eq(w->label, label) : (strstr(w->label, label) != NULL)) {
                *cx = w->x + w->w * 0.5f; *cy = w->y + w->h * 0.5f;
                if (!strcmp(w->kind, "select")) *cx = w->x + w->w * 0.75f;   /* the right half steps forward */
                return true;
            }
        }
    return false;
}
/* self-test: the text drawn that starts with prefix, its scale and colour */
bool arc__ui_find_text(const char *prefix, char *out, int cap, float *scale, uint32_t *color) {
    for (int i = 0; i < U.ncmd; i++) {
        const ui_cmd *c = &U.cmds[i];
        if ((c->kind != UC_TEXT && c->kind != UC_RICH) || c->text < 0 || strncmp(U.text + c->text, prefix, strlen(prefix))) continue;
        snprintf(out, (size_t)cap, "%s", U.text + c->text);
        *scale = c->w > 0 ? c->w : base_scale(); *color = c->color;
        return true;
    }
    return false;
}
bool arc__ui_used(void) { return U.used_this_tick; }
static void ux_tick_end(void);
void arc__ui_tick_end(void) {
    if (U.in_panel) arc_ui_end();
    ux_tick_end();
    memcpy(U.focused_done, U.focused_now, sizeof U.focused_done); U.focused_now[0] = 0;
    if (!arc_key_down(ARC_MOUSE_LEFT)) { U.active = 0; U.drag_panel = 0; } if (U.focus_count && U.focus_index >= U.focus_count) U.focus_index = U.focus_count - 1; }

/* state snapshots: what the UI remembers between ticks (focus, pressed widget, panel positions) */
typedef struct { uint32_t hot, active, kb_focus, drag_panel, nav_id; float drag_off_x, drag_off_y; int32_t focus_index, focus_count; uint8_t nav; arc_v2 last_mouse; ui_panel_rec panels[UI_MAX_PANELS]; } ui_saved;
size_t arc__ui_state(arc_writer *w) {
    ui_saved u; memset(&u, 0, sizeof u);
    u.hot = U.hot; u.active = U.active; u.kb_focus = U.kb_focus; u.drag_panel = U.drag_panel; u.nav_id = U.nav_id;
    u.drag_off_x = U.drag_off_x; u.drag_off_y = U.drag_off_y; u.focus_index = U.focus_index; u.focus_count = U.focus_count;
    u.nav = U.nav; u.last_mouse = U.last_mouse; memcpy(u.panels, U.panels, sizeof u.panels);
    arc_write(w, &u, sizeof u);
    /* 0.7 block (older state files end before it): navigation, captions, transition */
    uint32_t magic = 0x37304355u, n = (uint32_t)U.nfr; int32_t iv[3] = { U.hold_dir, U.stick_dir, U.tr_kind };
    float fv[5] = { U.hold_t, U.tr_secs, U.tr_t, 0, 0 }; uint8_t bv[2] = { U.tr_on, U.tr_covered }; int32_t ncap = U.ncap;
    arc_write(w, &magic, 4); arc_write(w, iv, sizeof iv); arc_write(w, fv, sizeof fv); arc_write(w, bv, sizeof bv); arc_write(w, &U.tr_rgb, 4);
    arc_write(w, &n, 4); arc_write(w, U.fr, sizeof U.fr[0] * n); arc_write(w, U.fr_lr, n);
    arc_write(w, &ncap, 4); arc_write(w, U.cap, sizeof U.cap[0] * (size_t)U.ncap);
    /* 0.8.1 block (only with panel animation on): the animation clock, the panels' and widgets' animation, the closing
       panels still fading, and the last tick's drawing (a panel that is not drawn next tick fades out from it). Without
       it a seek or a rollback would start from the future's drawing (a ghost of a menu that is not there) */
    if (th()->anim > 0 || U.ghost[0].n || U.ghost[1].n || U.ghost[2].n || U.ghost[3].n) {
        uint32_t m2 = 0x31414955u, serial = U.serial, nc = (uint32_t)U.ncmd, nt = (uint32_t)U.ntext; uint8_t ng = 0;
        for (int g = 0; g < 4; g++) ng += U.ghost[g].n > 0;
        arc_write(w, &m2, 4); arc_write(w, &serial, 4); arc_write(w, U.pan, sizeof U.pan); arc_write(w, U.wan, sizeof U.wan);
        arc_write(w, &nc, 4); arc_write(w, U.cmds, sizeof(ui_cmd) * nc); arc_write(w, &nt, 4); arc_write(w, U.text, nt);
        arc_write(w, &ng, 1);
        for (int g = 0; g < 4; g++) {
            const ui_ghost *gh = &U.ghost[g]; if (!gh->n) continue;
            int32_t hv[2] = { gh->n, gh->ntext }; float fv2[3] = { gh->age, gh->cx, gh->cy };
            arc_write(w, hv, sizeof hv); arc_write(w, fv2, sizeof fv2); arc_write(w, gh->c, sizeof(ui_cmd) * (size_t)gh->n); arc_write(w, gh->text, (size_t)gh->ntext);
        }
    }
    return w->len;
}
bool arc__ui_state_read(arc_reader *r, bool commit) {   /* commit false: only check it */
    ui_saved u;
    if (!arc_read(r, &u, sizeof u)) return false;
    bool v07 = r->pos + 4 <= r->size;   /* a state from before 0.7 ends here */
    uint32_t magic = 0, n = 0, rgb = 0; int32_t iv[3] = { 0, 0, 0 }, ncap = 0; float fv[5] = { 0 }; uint8_t bv[2] = { 0, 0 };
    static float fr[UI_MAX_WIDGETS][4]; static uint8_t lr[UI_MAX_WIDGETS]; static ui_caption cap[UI_CAPTIONS];
    if (v07) {
        if (!arc_read(r, &magic, 4) || magic != 0x37304355u) return false;
        if (!arc_read(r, iv, sizeof iv) || !arc_read(r, fv, sizeof fv) || !arc_read(r, bv, sizeof bv) || !arc_read(r, &rgb, 4) || !arc_read(r, &n, 4) || n > UI_MAX_WIDGETS) return false;
        if (!arc_read(r, fr, sizeof fr[0] * n) || !arc_read(r, lr, n) || !arc_read(r, &ncap, 4) || ncap < 0 || ncap > UI_CAPTIONS) return false;
        if (!arc_read(r, cap, sizeof cap[0] * (size_t)ncap)) return false;
    }
    /* 0.8.1 animation block (see arc__ui_state); none in older states or with animation off: start the animation afresh */
    static ui_panel_anim pan[UI_MAX_PANELS]; static ui_widget_anim wan[128]; static ui_cmd cmds[UI_MAX_CMDS]; static char text[UI_TEXT_POOL]; static ui_ghost gho[4];
    uint32_t m2 = 0, serial = 0, nc = 0, nt = 0; uint8_t ng = 0; bool has_anim = false;
    if (r->pos + 4 <= r->size) memcpy(&m2, r->data + r->pos, 4);
    if (m2 == 0x31414955u) {
        has_anim = true; r->pos += 4;
        if (!arc_read(r, &serial, 4) || !arc_read(r, pan, sizeof pan) || !arc_read(r, wan, sizeof wan) || !arc_read(r, &nc, 4) || nc > UI_MAX_CMDS) return false;
        if (!arc_read(r, cmds, sizeof(ui_cmd) * nc) || !arc_read(r, &nt, 4) || nt > UI_TEXT_POOL || !arc_read(r, text, nt) || !arc_read(r, &ng, 1) || ng > 4) return false;
        memset(gho, 0, sizeof gho);
        for (int g = 0; g < ng; g++) {
            int32_t hv[2]; float fv2[3];
            if (!arc_read(r, hv, sizeof hv) || hv[0] <= 0 || hv[0] > UI_GHOST_CMDS || hv[1] < 0 || hv[1] > UI_GHOST_TEXT || !arc_read(r, fv2, sizeof fv2)) { r->fail = true; return false; }
            gho[g].n = hv[0]; gho[g].ntext = hv[1]; gho[g].age = fv2[0]; gho[g].cx = fv2[1]; gho[g].cy = fv2[2];
            if (!arc_read(r, gho[g].c, sizeof(ui_cmd) * (size_t)hv[0]) || !arc_read(r, gho[g].text, (size_t)hv[1])) return false;
        }
    }
    if (!commit) return true;
    if (has_anim) {
        U.serial = serial; memcpy(U.pan, pan, sizeof pan); memcpy(U.wan, wan, sizeof wan); memcpy(U.ghost, gho, sizeof gho);
        memcpy(U.cmds, cmds, sizeof(ui_cmd) * nc); U.ncmd = (int)nc; memcpy(U.text, text, nt); U.ntext = (int)nt;
    } else {                                       /* nothing animating: no ghost of a panel from another time */
        memset(U.pan, 0, sizeof U.pan); memset(U.wan, 0, sizeof U.wan); for (int g = 0; g < 4; g++) U.ghost[g].n = 0;
    }
    U.hot = u.hot; U.active = u.active; U.kb_focus = u.kb_focus; U.drag_panel = u.drag_panel; U.nav_id = u.nav_id;
    U.drag_off_x = u.drag_off_x; U.drag_off_y = u.drag_off_y; U.focus_index = u.focus_index; U.focus_count = u.focus_count;
    U.nav = u.nav != 0; U.last_mouse = u.last_mouse; memcpy(U.panels, u.panels, sizeof u.panels);
    U.hold_dir = iv[0]; U.stick_dir = iv[1]; U.tr_kind = iv[2]; U.hold_t = fv[0]; U.tr_secs = fv[1]; U.tr_t = fv[2];
    U.tr_on = bv[0] != 0; U.tr_covered = bv[1] != 0; U.tr_rgb = rgb;
    memcpy(U.fr, fr, sizeof fr[0] * n); memcpy(U.fr_lr, lr, n); U.nfr = (int)n;
    memcpy(U.cap, cap, sizeof cap[0] * (size_t)ncap); U.ncap = ncap;
    return true;
}

/* the standard sound-settings sliders (inside a panel the game opens) */
float arc__master_volume(void);
bool arc_ui_sound_settings(void) {
    static const char *const names[ARC_BUS_COUNT] = { "Effects##arcvol", "Music##arcvol", "Interface##arcvol", "Voices##arcvol" };
    static const int order[ARC_BUS_COUNT] = { ARC_BUS_MUSIC, ARC_BUS_SFX, ARC_BUS_UI, ARC_BUS_VOICE };
    bool changed = false;
    float m = arc__master_volume();
    if (arc_ui_slider("Master##arcvol", &m, 0, 1)) { arc_set_master_volume(m); changed = true; }
    for (int i = 0; i < ARC_BUS_COUNT; i++) {
        arc_bus b = (arc_bus)order[i]; float v = arc_bus_volume(b);
        if (arc_ui_slider(names[b], &v, 0, (float)(v > 1 ? 2 : 1)))   /* a bus boosted past 1 keeps its level */ { arc_set_bus_volume(b, v); changed = true; }
    }
    return changed;
}

/* ---- 0.7: layout navigation, back, scale, rich text ---- */
void  arc_ui_set_nav(bool on) { if (on && !U.nav_on) U.nav = true; U.nav_on = on; }
bool  arc_ui_back_pressed(void) { return U.back; }
const char *arc_ui_focused(void) { return U.focused_done; }
void  arc_ui_set_scale(float s) { U.scale = s > 0 ? s : 1; }
float arc_ui_scale(void) { return uis(); }
void  arc_ui_set_rich(bool on) { U.rich = on; }
/* ---- 0.10.1 layout: automatic scale, safe area, anchored panels, orientation ---- */
float arc_ui_scale_auto(float ref_w, float ref_h, float min_scale, float max_scale) {
    UL.ref_w = ref_w; UL.ref_h = ref_h; UL.smin = min_scale > 0 ? min_scale : 0.25f; UL.smax = max_scale > UL.smin ? max_scale : UL.smin;
    if (ref_w <= 0 || ref_h <= 0) return uis();
    float s = arc_clampf(arc_minf((float)arc_width() / ref_w, (float)arc_height() / ref_h), UL.smin, UL.smax);
    if (U.font.id && !arc_font_is_sdf(U.font)) { float unit = (float)arc_font_size(U.font) * U.font_scale, px = floorf(unit * s + 0.5f); if (px >= 1) s = arc_clampf(px / unit, UL.smin, UL.smax); }
    U.scale = s; return s;
}
float arc__safe_insets[4];   /* left, top, right, bottom in pixels: the platform (web: env(safe-area-inset-*)) or --safe-area */
arc_rectf arc_safe_area(void) {
    float W = (float)arc_width(), H = (float)arc_height(), in[4]; memcpy(in, arc__safe_insets, sizeof in);
    int vx, vy, vw, vh, ww, wh;
    if (arc__viewport_cur(&vx, &vy, &vw, &vh)) {   /* 0.10.2: the part of the window's insets that covers this viewport */
        arc__window_size(&ww, &wh);
        in[0] = arc_maxf(0.0f, in[0] - (float)vx); in[1] = arc_maxf(0.0f, in[1] - (float)vy);
        in[2] = arc_maxf(0.0f, in[2] - (float)(ww - vx - vw)); in[3] = arc_maxf(0.0f, in[3] - (float)(wh - vy - vh));
    }
    arc_rectf r; r.x = in[0]; r.y = in[1];
    r.w = W - in[0] - in[2]; r.h = H - in[1] - in[3];
    if (r.w < 1) { r.x = 0; r.w = W; }
    if (r.h < 1) { r.y = 0; r.h = H; }
    return r;
}
int arc_orientation(void) { return arc_height() > arc_width() ? ARC_PORTRAIT : ARC_LANDSCAPE; }
void arc_ui_begin_anchored(const char *title, int anchor, float ox, float oy, float w, float h) {
    if (U.in_panel) arc_ui_end();
    arc_rectf sa = arc_safe_area();
    uint32_t id = hash_str(2166136261u, title ? title : "");
    float d = ds(), wpx = w * d, hpx = h > 0 ? h * d : 0;
    if (h <= 0) for (int i = 0; i < 32; i++) if (UL.last[i].id == id) hpx = UL.last[i].h * d;
    float shrink = 1.0f;
    if (wpx > sa.w && wpx > 0) shrink = sa.w / wpx;
    if (hpx * shrink > sa.h && hpx > 0) shrink = sa.h / hpx;
    if (shrink < 0.25f) shrink = 0.25f;
    wpx *= shrink; hpx *= shrink; ox *= d; oy *= d;
    float x = (anchor & ARC_ANCHOR_LEFT) ? sa.x + ox : (anchor & ARC_ANCHOR_RIGHT) ? sa.x + sa.w - wpx - ox : sa.x + (sa.w - wpx) * 0.5f + ox;
    float y = (anchor & ARC_ANCHOR_TOP) ? sa.y + oy : (anchor & ARC_ANCHOR_BOTTOM) ? sa.y + sa.h - hpx - oy : sa.y + (sa.h - hpx) * 0.5f + oy;
    x = arc_clampf(x, sa.x, arc_maxf(sa.x, sa.x + sa.w - wpx)); y = arc_clampf(y, sa.y, arc_maxf(sa.y, sa.y + sa.h - hpx));
    UL.scale_save = U.scale; UL.shrink = shrink; UL.anchored_id = id; UL.ds_full = d;
    if (shrink < 1.0f) U.scale = uis() * shrink;
    UL.anchored = true;
    arc_ui_begin(title, floorf(x + 0.5f), floorf(y + 0.5f), wpx / ds());
}
/* 0.10.6: the card's text is in the UI's font and scale (the first line, up to '\n', as a title at twice the size),
   wrapped to the window's width; a title word wider than the window shrinks the title */
typedef struct { float sc, cx, y, lh; uint32_t c; int n; bool draw; } card_wrap;
static float card_measure(const void *p, const char *s) { return width_at(s, ((const card_wrap *)p)->sc); }
static void card_emit(void *p, const char *s, int n) {
    card_wrap *w = (card_wrap *)p; char line[512]; if (n > (int)sizeof line - 1) n = (int)sizeof line - 1;
    memcpy(line, s, (size_t)n); line[n] = 0; w->n++;
    if (w->draw && n) {
        float lw = width_at(line, w->sc), x = floorf(w->cx - lw * 0.5f + 0.5f), y = floorf(w->y + 0.5f);
        rec(UC_TEXT, x, y, w->sc, 0, w->c, line);
        record_info(line, "text", x, y, lw, height_at(w->sc), 0, false, NULL);   /* ui and ui_fit see the card's lines */
    }
    w->y += w->lh;
}
static float card_snap(float sc) {   /* whole-pixel sizes for a bitmap font */
    if (U.font.id && !arc_font_is_sdf(U.font)) { float unit = (float)arc_font_size(U.font), px = floorf(unit * sc + 0.5f); if (px >= 1) return px / unit; }
    return sc;
}
bool arc_ui_require_landscape(const char *text) {
    if (arc_orientation() != ARC_PORTRAIT) return false;
    if (U.in_panel) arc_ui_end();
    float W = (float)arc_width(), H = (float)arc_height(), maxw = W * 0.88f;
    rect(0, 0, W, H, 0xF0101418);
    const char *t = text && *text ? text : "TURN SIDEWAYS";
    char title[256], body[1024]; const char *nl = strchr(t, '\n');
    size_t tn = nl ? (size_t)(nl - t) : strlen(t); if (tn > sizeof title - 1) tn = sizeof title - 1;
    memcpy(title, t, tn); title[tn] = 0;
    snprintf(body, sizeof body, "%s", nl ? nl + 1 : "");
    bool rich = U.rich; U.rich = false;   /* measured and drawn plain */
    float bs = card_snap(base_scale() * 1.25f), ts = base_scale() * 2.0f;
    {   /* the title's widest word fits the width */
        float widest = 0; char word[256]; const char *p = title;
        while (*p) { while (*p == ' ') p++; int n = 0; while (p[n] && p[n] != ' ' && n < 255) n++; memcpy(word, p, (size_t)n); word[n] = 0; widest = arc_maxf(widest, width_at(word, ts)); p += n; }
        if (widest > maxw && widest > 0) ts *= maxw / widest;
        ts = card_snap(arc_maxf(ts, bs));
    }
    float gap = U.font.id ? 1.15f : 1.25f;
    card_wrap tw = { ts, W * 0.5f, 0, height_at(ts) * gap, th()->text, 0, false }, bw = { bs, W * 0.5f, 0, height_at(bs) * gap, th()->text_dim, 0, false };
    arc__wrap(title, maxw, card_measure, &tw, card_emit, &tw);
    if (body[0]) arc__wrap(body, maxw, card_measure, &bw, card_emit, &bw);
    float ph = W * 0.18f * 0.55f, pgap = height_at(ts) * 0.75f;
    float th_ = (float)tw.n * tw.lh, bh = body[0] ? (float)bw.n * bw.lh + height_at(bs) * 0.5f : 0;
    float top = arc_maxf(0.0f, (H - (ph + pgap + th_ + bh)) * 0.5f);
    {   /* a phone turning: an outline box on its side */
        float pw = W * 0.18f, bx = (W - pw) * 0.5f, by = top, l = arc_maxf(2.0f, floorf(ds() * 2.0f + 0.5f));
        rect(bx, by, pw, l, th()->accent); rect(bx, by + ph - l, pw, l, th()->accent); rect(bx, by, l, ph, th()->accent); rect(bx + pw - l, by, l, ph, th()->accent);
    }
    U.ninfo = 0;   /* the card covers the UI drawn before it: only its own lines are left to see or click */
    tw.draw = true; tw.n = 0; tw.y = top + ph + pgap;
    arc__wrap(title, maxw, card_measure, &tw, card_emit, &tw);
    if (body[0]) { bw.draw = true; bw.n = 0; bw.y = tw.y + height_at(bs) * 0.5f; arc__wrap(body, maxw, card_measure, &bw, card_emit, &bw); }
    U.rich = rich;
    U.mouse_over_ui = true;
    return true;
}
/* ---- 0.10.1 `ui_fit WxH ...`: one tick at each window size; widgets outside the window or overlapping ---- */
static struct { bool on; int n, i, ow, oh, problems; int w[16], h[16]; } UF;
bool arc__ui_fit_start(int argc, const char **argv) {
    memset(&UF, 0, sizeof UF);
    for (int k = 1; k < argc && UF.n < 16; k++) { int w, h; if (sscanf(argv[k], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { UF.w[UF.n] = w; UF.h[UF.n] = h; UF.n++; } }
    if (!UF.n) { printf("ARC error ui_fit: give sizes, e.g. ui_fit 1280x720 800x600\n"); fflush(stdout); return false; }
    UF.on = true; UF.ow = arc_width(); UF.oh = arc_height();
    return true;
}
int arc__ui_fit_ticks(void) { return UF.on ? UF.n : 0; }
void arc__ui_fit_before_tick(void) { if (UF.on && UF.i < UF.n) arc__set_size(UF.w[UF.i], UF.h[UF.i]); }
void arc__ui_fit_after_tick(void) {
    if (!UF.on || UF.i >= UF.n) return;
    float W = (float)UF.w[UF.i], H = (float)UF.h[UF.i]; int outside = 0, overlaps = 0; arc_rectf sa = arc_safe_area();
    for (int a = 0; a < U.ninfo; a++) {
        const ui_widget_info *p = &U.info[a];
        if (p->x < -0.5f || p->y < -0.5f || p->x + p->w > W + 0.5f || p->y + p->h > H + 0.5f) {
            outside++; printf("ARC error ui_fit %dx%d: \"%s\" (%s) is outside the window at %.0f,%.0f %.0fx%.0f\n", UF.w[UF.i], UF.h[UF.i], p->label, p->kind, p->x, p->y, p->w, p->h);
        } else if (p->x < sa.x - 0.5f || p->y < sa.y - 0.5f || p->x + p->w > sa.x + sa.w + 0.5f || p->y + p->h > sa.y + sa.h + 0.5f) {
            outside++; printf("ARC error ui_fit %dx%d: \"%s\" (%s) is outside the safe area at %.0f,%.0f %.0fx%.0f\n", UF.w[UF.i], UF.h[UF.i], p->label, p->kind, p->x, p->y, p->w, p->h);
        }
        for (int b = a + 1; b < U.ninfo; b++) {
            const ui_widget_info *q = &U.info[b];
            float ix = arc_minf(p->x + p->w, q->x + q->w) - arc_maxf(p->x, q->x), iy = arc_minf(p->y + p->h, q->y + q->h) - arc_maxf(p->y, q->y);
            if (ix > 1.0f && iy > 1.0f) { overlaps++; if (overlaps <= 8) printf("ARC error ui_fit %dx%d: \"%s\" overlaps \"%s\"\n", UF.w[UF.i], UF.h[UF.i], p->label, q->label); }
        }
    }
    printf("ARC ui_fit %dx%d widgets=%d outside=%d overlaps=%d\n", UF.w[UF.i], UF.h[UF.i], U.ninfo, outside, overlaps);
    UF.problems += outside + overlaps;
    if (++UF.i >= UF.n) {
        arc__set_size(UF.ow, UF.oh); UF.on = false;
        if (UF.problems == 0) printf("ARC ok ui_fit %d sizes\n", UF.n); else printf("ARC error ui_fit: %d problems in %d sizes\n", UF.problems, UF.n);
    }
    fflush(stdout);
}
/* the focusable widget nearest the one at `from` in direction dir (1 up, 2 down, 3 left, 4 right), by last tick's layout */
static int nav_nearest(int from, int dir) {
    const float *c = U.fr_last[from];
    int best = -1; float best_score = 1e30f;
    for (int i = 0; i < U.nfr_last; i++) {
        if (i == from) continue;
        const float *r = U.fr_last[i];   /* along: the gap between the facing edges; the widget must lie wholly that way */
        float along = dir == 1 ? c[1] - (r[1] + r[3]) : dir == 2 ? r[1] - (c[1] + c[3]) : dir == 3 ? c[0] - (r[0] + r[2]) : r[0] - (c[0] + c[2]);
        if (along < -1.0f) continue;
        float gap = dir <= 2 ? arc_maxf(0, arc_maxf(r[0] - (c[0] + c[2]), c[0] - (r[0] + r[2])))    /* sideways distance between the rects */
                             : arc_maxf(0, arc_maxf(r[1] - (c[1] + c[3]), c[1] - (r[1] + r[3])));
        float score = along + gap * 2.0f;
        if (score < best_score - 0.01f) { best_score = score; best = i; }
    }
    return best;
}
static void nav_begin(void) {
    int dir = 0;
    if (arc_key_pressed(ARC_KEY_UP) || arc_key_pressed(ARC_PAD_UP)) dir = 1;
    else if (arc_key_pressed(ARC_KEY_DOWN) || arc_key_pressed(ARC_PAD_DOWN)) dir = 2;
    else if (arc_key_pressed(ARC_KEY_LEFT) || arc_key_pressed(ARC_PAD_LEFT)) dir = 3;
    else if (arc_key_pressed(ARC_KEY_RIGHT) || arc_key_pressed(ARC_PAD_RIGHT)) dir = 4;
    else if (arc_key_pressed(ARC_KEY_TAB)) dir = arc_key_down(ARC_KEY_SHIFT) ? 5 : 6;   /* layout order back / on */
    float lx = arc_pad_axis(ARC_AXIS_LX), ly = arc_pad_axis(ARC_AXIS_LY);
    int sd = ly < -0.5f ? 1 : ly > 0.5f ? 2 : lx < -0.5f ? 3 : lx > 0.5f ? 4 : 0;
    if (!dir && sd && sd != U.stick_dir) dir = sd;
    U.stick_dir = sd;
    int held = (arc_key_down(ARC_KEY_UP) || arc_key_down(ARC_PAD_UP)) ? 1 : (arc_key_down(ARC_KEY_DOWN) || arc_key_down(ARC_PAD_DOWN)) ? 2
             : (arc_key_down(ARC_KEY_LEFT) || arc_key_down(ARC_PAD_LEFT)) ? 3 : (arc_key_down(ARC_KEY_RIGHT) || arc_key_down(ARC_PAD_RIGHT)) ? 4 : sd;
    if (dir) { U.hold_dir = dir; U.hold_t = 0; }
    else if (held && held == U.hold_dir) {   /* held: repeats after 0.4 s, then 10 a second */
        U.hold_t += arc_dt();
        if (U.hold_t >= 0.4f) { dir = held; U.hold_t -= 0.1f; }
    } else U.hold_dir = 0;
    int n = U.focus_count_last;
    if (!dir || U.kb_focus || n <= 0) return;
    if (!U.nav) { U.nav = true; if (U.focus_index >= n) U.focus_index = 0; return; }   /* the first press shows the ring */
    int at = arc_clampi(U.focus_index, 0, n - 1);
    if ((dir == 3 || dir == 4) && at < U.nfr_last && U.fr_lr_last[at]) { U.lr = dir == 3 ? -1 : 1; return; }   /* a slider's or select's */
    int to = -1;
    if (dir == 5) to = (at + n - 1) % n;
    else if (dir == 6) to = (at + 1) % n;
    else if (at < U.nfr_last) to = nav_nearest(at, dir);
    if (to < 0) to = dir == 2 ? 0 : dir == 1 ? n - 1 : at;   /* off the top or bottom: wrap */
    U.focus_index = to;
}

/* ---- 0.7: panel and widget animation ---- */
static void cmds_xform(ui_cmd *c, int n, float cx, float cy, float s, float alpha) {
    for (int i = 0; i < n; i++, c++) {
        if (s != 1.0f) {
            c->x = cx + (c->x - cx) * s; c->y = cy + (c->y - cy) * s;
            if (c->kind == UC_TEXT || c->kind == UC_RICH) c->w = (c->w > 0 ? c->w : base_scale()) * s;
            else { c->w *= s; c->h *= s; }
        }
        if (alpha < 1.0f) { uint32_t a = c->color >> 24; c->color = arc_rgba(c->color, (a ? (float)a / 255.0f : 1.0f) * alpha); }
    }
}
static ui_panel_anim *panel_anim(uint32_t id) {
    int old = 0;
    for (int i = 0; i < UI_MAX_PANELS; i++) { if (U.pan[i].id == id && U.pan[i].seen) return &U.pan[i]; if (U.pan[i].seen < U.pan[old].seen) old = i; }
    memset(&U.pan[old], 0, sizeof U.pan[old]); U.pan[old].id = id; return &U.pan[old];
}
static void panel_anim_end(float h) {
    if (U.bg_cmd < 0) return;
    ui_panel_anim *a = panel_anim(U.panel_id);
    if (a->seen != U.serial) {
        if (a->seen + 1 != U.serial) a->age = 0;   /* not drawn last tick: opening */
        a->age += arc_dt(); a->seen = U.serial;
    }
    a->first = U.bg_cmd; a->last = U.ncmd; a->cx = U.px + U.pw * 0.5f; a->cy = U.py + h * 0.5f;
    float p = arc_minf(1, a->age / th()->anim);
    if (p < 1) cmds_xform(U.cmds + a->first, a->last - a->first, a->cx, a->cy, 0.92f + 0.08f * arc_ease_out_back(p), arc_ease_out_cubic(p));
}
static void ghosts_tick(void) {
    float an = th()->anim;
    if (an <= 0) { for (int g = 0; g < 4; g++) U.ghost[g].n = 0; return; }
    for (int i = 0; i < UI_MAX_PANELS; i++) {   /* drawn last tick, not this one: it closes */
        ui_panel_anim *a = &U.pan[i];
        if (!a->seen || a->seen + 1 != U.serial || a->last <= a->first || a->last > U.nprev) continue;
        int n = a->last - a->first, g = 0, ntext = 0;
        for (int k = 1; k < 4; k++) if (U.ghost[k].n == 0 || (U.ghost[g].n && U.ghost[k].age > U.ghost[g].age)) g = k;
        ui_ghost *gh = &U.ghost[g];
        if (n > UI_GHOST_CMDS) continue;
        for (int k = 0; k < n; k++) {
            ui_cmd c = U.prev_cmds[a->first + k];
            if (c.text >= 0 && c.kind != UC_NINE) {
                int len = (int)strlen(U.prev_text + c.text);
                if (ntext + len + 1 > UI_GHOST_TEXT) { n = k; break; }
                memcpy(gh->text + ntext, U.prev_text + c.text, (size_t)len + 1); c.text = ntext; ntext += len + 1;
            }
            gh->c[k] = c;
        }
        gh->n = n; gh->ntext = ntext; gh->age = 0; gh->cx = a->cx; gh->cy = a->cy;
    }
    for (int g = 0; g < 4; g++) {
        ui_ghost *gh = &U.ghost[g];
        if (!gh->n) continue;
        gh->age += arc_dt();
        float p = 1 - gh->age / an;
        if (p <= 0) { gh->n = 0; continue; }
        int first = U.ncmd; g_rec_raw = true;
        for (int k = 0; k < gh->n && U.ncmd < UI_MAX_CMDS; k++) {
            const ui_cmd *c = &gh->c[k];
            int before = U.ncmd;
            rec(c->kind, c->x, c->y, c->w, c->h, c->color, c->text >= 0 && c->kind != UC_NINE ? gh->text + c->text : NULL);
            if (c->kind == UC_NINE && U.ncmd > before) U.cmds[U.ncmd - 1].text = c->text;
        }
        g_rec_raw = false;
        cmds_xform(U.cmds + first, U.ncmd - first, gh->cx, gh->cy, 0.92f + 0.08f * p, arc_ease_in_quad(p));
    }
}

/* ---- 0.7: screen transitions and captions (drawn through the UI overlay, on top) ---- */
static float tr_cover(void) {
    if (!U.tr_on || U.tr_secs <= 0) return 0;
    float t = U.tr_t / U.tr_secs;
    return t <= 1 ? arc_ease_inout_quad(t) : 1 - arc_ease_inout_quad(t - 1);
}
void arc_transition(int kind, float seconds, uint32_t rgb) {
    if (seconds <= 0) { U.tr_on = false; U.tr_covered = false; return; }
    U.tr_kind = kind; U.tr_secs = seconds; U.tr_t = 0; U.tr_rgb = rgb & 0xFFFFFFu; U.tr_on = true; U.tr_covered = false;
}
bool  arc_transition_covered(void) { return U.tr_covered; }
bool  arc_transition_active(void) { return U.tr_on; }
float arc_transition_amount(void) { return tr_cover(); }
void arc_caption(const char *text, float seconds, uint32_t rgb) {
    if (!text || U.ncap >= UI_CAPTIONS) return;
    ui_caption *c = &U.cap[U.ncap++]; snprintf(c->text, sizeof c->text, "%s", text);
    c->seconds = seconds > 0 ? seconds : 2; c->age = 0; c->rgb = rgb ? rgb : 0xFFFFFF;
}
void arc_caption_clear(void) { U.ncap = 0; }
const char *arc_caption_current(void) { return U.ncap ? U.cap[0].text : ""; }
void arc_caption_style(float scale, uint32_t backdrop_argb) { U.cap_scale = scale; U.cap_bg = backdrop_argb; }
static void ux_tick_begin(void) {
    U.tr_covered = false;
    if (U.tr_on) {   /* cover, one tick fully covered, uncover */
        float old = U.tr_t; U.tr_t += arc_dt();
        if (old < U.tr_secs && U.tr_t >= U.tr_secs) { U.tr_t = U.tr_secs; U.tr_covered = true; }
        if (U.tr_t >= U.tr_secs * 2) U.tr_on = false;
    }
    if (U.ncap) {
        U.cap[0].age += arc_dt();
        if (U.cap[0].age >= U.cap[0].seconds - 1e-4f) { memmove(U.cap, U.cap + 1, sizeof U.cap[0] * (size_t)(U.ncap - 1)); U.ncap--; }
    }
}
static void ux_tick_end(void) {
    bool dim = U.dim; U.dim = false;
    ghosts_tick();
    float W = (float)arc_width(), H = (float)arc_height();
    if (U.tr_on) {
        float a = tr_cover(); bool covering = U.tr_t <= U.tr_secs;
        if (U.tr_kind == ARC_TRANSITION_WIPE_LEFT) { if (covering) rect(0, 0, W * a, H, U.tr_rgb); else rect(W * (1 - a), 0, W * a, H, U.tr_rgb); }
        else if (U.tr_kind == ARC_TRANSITION_WIPE_DOWN) { if (covering) rect(0, 0, W, H * a, U.tr_rgb); else rect(0, H * (1 - a), W, H * a, U.tr_rgb); }
        else if (a > 0) rect(0, 0, W, H, arc_rgba(U.tr_rgb, a));
    }
    if (U.ncap) {
        const ui_caption *c = &U.cap[0];
        float sc = base_scale() * (U.cap_scale > 0 ? U.cap_scale : 1), p = pad();
        float tw = arc_rich_width(U.font, sc, c->text), tht = arc_rich_height(U.font, sc, c->text);
        float bw = tw + p * 4, bh = tht + p * 2, bx = (W - bw) * 0.5f, by = H - bh - 24.0f * ds();
        rect(bx, by, bw, bh, U.cap_bg ? U.cap_bg : 0xB0000000u);
        rec(UC_RICH, bx + p * 2, by + p, sc, 0, 0xFF000000u | c->rgb, c->text);
    }
    U.dim = dim;
}
