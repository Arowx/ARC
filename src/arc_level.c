/* ARC 0.10.8 level files (Droids' request): layers of character rows, `key = value` settings for the file and for each
 * layer, and objects (a cell, a kind and named fields) per layer, in a small text format that diffs well:
 *
 *   arclvl 1
 *   # a comment (lines starting with #, outside rows)
 *   title = "Droids"
 *   layer "Deck 1" 32 16
 *   light = 0.8
 *   |################################|
 *   |#..............................#|
 *   object 3 5 droid class=4 facing=2 note="a repair droid"
 *   end
 *
 * Rows are framed by '|' and exactly as wide as the layer (any printable character inside, '|' too). Values are bare
 * words ([A-Za-z0-9_.+-/:]) or "quoted" (\" \\ \n inside). arc_level_save writes the same text for the same level, so a
 * saved file loads and saves back byte for byte. See arc.h. */
#include "arc_internal.h"
#include <ctype.h>

typedef struct { char *key, *val; } lv_kv;
typedef struct { int n, cap; lv_kv *a; } lv_kvs;
typedef struct { int x, y; char *kind; lv_kvs f; } lv_obj;
typedef struct { char *name; int w, h; char *cells; lv_kvs set; int nobj, cobj; lv_obj *obj; } lv_layer;
struct arc_level { lv_kvs set; int nl, cl; lv_layer *L; char path[260]; bool reloaded; int hot; };

static char g_lv_err[320];
const char *arc_level_error(void) { return g_lv_err; }
static char *lv_dup(const char *s) { size_t n = strlen(s ? s : ""); char *p = (char *)arc__xalloc(n + 1); memcpy(p, s ? s : "", n + 1); return p; }

/* ---- key / value lists ---- */
static int lv_find(const lv_kvs *k, const char *key) { for (int i = 0; i < k->n; i++) if (!strcmp(k->a[i].key, key)) return i; return -1; }
static bool lv_key_ok(const char *k) {
    if (!k || !(isalpha((unsigned char)k[0]) || k[0] == '_') || strlen(k) > 63) return false;
    for (const char *p = k + 1; *p; p++) if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '.' || *p == '-')) return false;
    return true;
}
static void lv_kv_set(lv_kvs *k, const char *key, const char *val) {
    if (!lv_key_ok(key)) return;
    int i = lv_find(k, key);
    if (!val) {   /* removed (the rest keep their order) */
        if (i < 0) return;
        free(k->a[i].key); free(k->a[i].val);
        memmove(&k->a[i], &k->a[i + 1], sizeof(lv_kv) * (size_t)(k->n - i - 1)); k->n--; return;
    }
    if (i >= 0) { free(k->a[i].val); k->a[i].val = lv_dup(val); return; }
    if (k->n == k->cap) { k->cap = k->cap ? k->cap * 2 : 8; k->a = (lv_kv *)realloc(k->a, sizeof(lv_kv) * (size_t)k->cap); if (!k->a) { k->n = k->cap = 0; return; } }
    k->a[k->n].key = lv_dup(key); k->a[k->n].val = lv_dup(val); k->n++;
}
static void lv_kv_free(lv_kvs *k) { for (int i = 0; i < k->n; i++) { free(k->a[i].key); free(k->a[i].val); } free(k->a); memset(k, 0, sizeof *k); }
static void lv_kv_copy(lv_kvs *d, const lv_kvs *s) { lv_kv_free(d); for (int i = 0; i < s->n; i++) lv_kv_set(d, s->a[i].key, s->a[i].val); }
static void lv_obj_free(lv_obj *o) { free(o->kind); lv_kv_free(&o->f); memset(o, 0, sizeof *o); }
static void lv_layer_free(lv_layer *L) {
    free(L->name); free(L->cells); lv_kv_free(&L->set);
    for (int i = 0; i < L->nobj; i++) lv_obj_free(&L->obj[i]);
    free(L->obj); memset(L, 0, sizeof *L);
}
static void lv_clear(arc_level *l) {
    lv_kv_free(&l->set);
    for (int i = 0; i < l->nl; i++) lv_layer_free(&l->L[i]);
    free(l->L); l->L = NULL; l->nl = l->cl = 0;
}

/* ---- the API: levels and layers ---- */
static int g_lv_hot_n; static arc_level *g_lv_hot[64];
arc_level *arc_level_new(void) { arc_level *l = (arc_level *)arc__xalloc(sizeof(arc_level)); memset(l, 0, sizeof *l); l->hot = -1; return l; }
void arc_level_free(arc_level *l) {
    if (!l) return;
    if (l->hot >= 0 && l->hot < 64) g_lv_hot[l->hot] = NULL;
    lv_clear(l); free(l);
}
const char *arc_level_path(const arc_level *l) { return l ? l->path : ""; }
static lv_layer *lv_L(const arc_level *l, int i) { return (l && i >= 0 && i < l->nl) ? &l->L[i] : NULL; }
static const lv_kvs *lv_set_c(const arc_level *l, int layer) { if (!l) return NULL; if (layer < 0) return &l->set; lv_layer *L = lv_L(l, layer); return L ? &L->set : NULL; }
int arc_level_layer_count(const arc_level *l) { return l ? l->nl : 0; }
int arc_level_add_layer(arc_level *l, const char *name, int w, int h, char fill) {
    if (!l || w <= 0 || h <= 0 || w > 4096 || h > 4096 || l->nl >= 256) return -1;
    if (l->nl == l->cl) { l->cl = l->cl ? l->cl * 2 : 8; l->L = (lv_layer *)realloc(l->L, sizeof(lv_layer) * (size_t)l->cl); if (!l->L) { l->nl = l->cl = 0; return -1; } }
    lv_layer *L = &l->L[l->nl]; memset(L, 0, sizeof *L);
    L->name = lv_dup(name ? name : ""); L->w = w; L->h = h;
    L->cells = (char *)arc__xalloc((size_t)(w + 1) * (size_t)h + 1);
    for (int y = 0; y < h; y++) { memset(L->cells + (size_t)y * (size_t)(w + 1), fill >= 32 && fill < 127 ? fill : '.', (size_t)w); L->cells[(size_t)y * (size_t)(w + 1) + (size_t)w] = 0; }
    return l->nl++;
}
void arc_level_remove_layer(arc_level *l, int layer) {
    lv_layer *L = lv_L(l, layer); if (!L) return;
    lv_layer_free(L); memmove(&l->L[layer], &l->L[layer + 1], sizeof(lv_layer) * (size_t)(l->nl - layer - 1)); l->nl--;
}
const char *arc_level_layer_name(const arc_level *l, int layer) { lv_layer *L = lv_L(l, layer); return L ? L->name : ""; }
void arc_level_set_layer_name(arc_level *l, int layer, const char *name) { lv_layer *L = lv_L(l, layer); if (L) { free(L->name); L->name = lv_dup(name); } }
int arc_level_width(const arc_level *l, int layer) { lv_layer *L = lv_L(l, layer); return L ? L->w : 0; }
int arc_level_height(const arc_level *l, int layer) { lv_layer *L = lv_L(l, layer); return L ? L->h : 0; }
char *arc_level_cells(arc_level *l, int layer) { lv_layer *L = lv_L(l, layer); return L ? L->cells : NULL; }
char arc_level_cell(const arc_level *l, int layer, int x, int y) {
    lv_layer *L = lv_L(l, layer); if (!L || x < 0 || y < 0 || x >= L->w || y >= L->h) return 0;
    return L->cells[(size_t)y * (size_t)(L->w + 1) + (size_t)x];
}
void arc_level_set_cell(arc_level *l, int layer, int x, int y, char c) {
    lv_layer *L = lv_L(l, layer); if (!L || x < 0 || y < 0 || x >= L->w || y >= L->h || c < 32 || c > 126) return;
    L->cells[(size_t)y * (size_t)(L->w + 1) + (size_t)x] = c;
}

/* ---- settings (layer -1 = the file's own) ---- */
const char *arc_level_get(const arc_level *l, int layer, const char *key, const char *def) {
    const lv_kvs *k = lv_set_c(l, layer); int i = k && key ? lv_find(k, key) : -1; return i >= 0 ? k->a[i].val : def;
}
float arc_level_get_num(const arc_level *l, int layer, const char *key, float def) {
    const char *v = arc_level_get(l, layer, key, NULL); if (!v) return def;
    char *e = NULL; float f = strtof(v, &e); return (e && e != v) ? f : def;
}
void arc_level_set(arc_level *l, int layer, const char *key, const char *value) { lv_kvs *k = (lv_kvs *)lv_set_c(l, layer); if (k) lv_kv_set(k, key, value); }
static void lv_fmt_num(char *b, size_t n, float v) { snprintf(b, n, "%.6g", (double)v); }
void arc_level_set_num(arc_level *l, int layer, const char *key, float value) { char b[32]; lv_fmt_num(b, sizeof b, value); arc_level_set(l, layer, key, b); }
int arc_level_setting_count(const arc_level *l, int layer) { const lv_kvs *k = lv_set_c(l, layer); return k ? k->n : 0; }
const char *arc_level_setting_key(const arc_level *l, int layer, int i) { const lv_kvs *k = lv_set_c(l, layer); return (k && i >= 0 && i < k->n) ? k->a[i].key : NULL; }

/* ---- objects ---- */
static lv_obj *lv_O(const arc_level *l, int layer, int i) { lv_layer *L = lv_L(l, layer); return (L && i >= 0 && i < L->nobj) ? &L->obj[i] : NULL; }
int arc_level_object_count(const arc_level *l, int layer) { lv_layer *L = lv_L(l, layer); return L ? L->nobj : 0; }
static int lv_obj_insert(arc_level *l, int layer, int at, int x, int y, const char *kind) {
    lv_layer *L = lv_L(l, layer); if (!L || !kind || !*kind) return -1;
    if (L->nobj == L->cobj) { L->cobj = L->cobj ? L->cobj * 2 : 16; L->obj = (lv_obj *)realloc(L->obj, sizeof(lv_obj) * (size_t)L->cobj); if (!L->obj) { L->nobj = L->cobj = 0; return -1; } }
    at = arc_clampi(at, 0, L->nobj);
    memmove(&L->obj[at + 1], &L->obj[at], sizeof(lv_obj) * (size_t)(L->nobj - at));
    lv_obj *o = &L->obj[at]; memset(o, 0, sizeof *o);
    o->x = arc_clampi(x, 0, L->w - 1); o->y = arc_clampi(y, 0, L->h - 1); o->kind = lv_dup(kind); L->nobj++;
    return at;
}
int arc_level_object_add(arc_level *l, int layer, int x, int y, const char *kind) { lv_layer *L = lv_L(l, layer); return L ? lv_obj_insert(l, layer, L->nobj, x, y, kind) : -1; }
void arc_level_object_remove(arc_level *l, int layer, int i) {
    lv_layer *L = lv_L(l, layer); if (!L || i < 0 || i >= L->nobj) return;
    lv_obj_free(&L->obj[i]); memmove(&L->obj[i], &L->obj[i + 1], sizeof(lv_obj) * (size_t)(L->nobj - i - 1)); L->nobj--;
}
const char *arc_level_object_kind(const arc_level *l, int layer, int i) { lv_obj *o = lv_O(l, layer, i); return o ? o->kind : ""; }
int arc_level_object_x(const arc_level *l, int layer, int i) { lv_obj *o = lv_O(l, layer, i); return o ? o->x : -1; }
int arc_level_object_y(const arc_level *l, int layer, int i) { lv_obj *o = lv_O(l, layer, i); return o ? o->y : -1; }
void arc_level_object_move(arc_level *l, int layer, int i, int x, int y) {
    lv_obj *o = lv_O(l, layer, i); lv_layer *L = lv_L(l, layer); if (o) { o->x = arc_clampi(x, 0, L->w - 1); o->y = arc_clampi(y, 0, L->h - 1); }
}
const char *arc_level_object_get(const arc_level *l, int layer, int i, const char *key, const char *def) {
    lv_obj *o = lv_O(l, layer, i); int k = o && key ? lv_find(&o->f, key) : -1; return k >= 0 ? o->f.a[k].val : def;
}
float arc_level_object_get_num(const arc_level *l, int layer, int i, const char *key, float def) {
    const char *v = arc_level_object_get(l, layer, i, key, NULL); if (!v) return def;
    char *e = NULL; float f = strtof(v, &e); return (e && e != v) ? f : def;
}
void arc_level_object_set(arc_level *l, int layer, int i, const char *key, const char *value) { lv_obj *o = lv_O(l, layer, i); if (o) lv_kv_set(&o->f, key, value); }
void arc_level_object_set_num(arc_level *l, int layer, int i, const char *key, float value) { char b[32]; lv_fmt_num(b, sizeof b, value); arc_level_object_set(l, layer, i, key, b); }
int arc_level_object_field_count(const arc_level *l, int layer, int i) { lv_obj *o = lv_O(l, layer, i); return o ? o->f.n : 0; }
const char *arc_level_object_field_key(const arc_level *l, int layer, int i, int k) { lv_obj *o = lv_O(l, layer, i); return (o && k >= 0 && k < o->f.n) ? o->f.a[k].key : NULL; }
int arc_level_object_at(const arc_level *l, int layer, int x, int y, int from) {
    lv_layer *L = lv_L(l, layer); if (!L) return -1;
    for (int i = arc_maxi(0, from); i < L->nobj; i++) if (L->obj[i].x == x && L->obj[i].y == y) return i;
    return -1;
}

/* ---- writing ---- */
typedef struct { char *b; size_t n, cap; } lv_buf;
static void lv_put(lv_buf *w, const char *s, size_t n) {
    if (w->n + n + 1 > w->cap) { size_t c = w->cap ? w->cap * 2 : 4096; while (c < w->n + n + 1) c *= 2; char *nb = (char *)realloc(w->b, c); if (!nb) return; w->b = nb; w->cap = c; }
    memcpy(w->b + w->n, s, n); w->n += n; w->b[w->n] = 0;
}
static void lv_puts(lv_buf *w, const char *s) { lv_put(w, s, strlen(s)); }
static bool lv_bare(int c) { return isalnum(c) || c == '_' || c == '.' || c == '+' || c == '-' || c == '/' || c == ':'; }
static void lv_put_value(lv_buf *w, const char *v) {
    bool bare = *v != 0;
    for (const char *p = v; *p; p++) if (!lv_bare((unsigned char)*p)) { bare = false; break; }
    if (bare) { lv_puts(w, v); return; }
    lv_put(w, "\"", 1);
    for (const char *p = v; *p; p++) {
        if (*p == '"') lv_put(w, "\\\"", 2); else if (*p == '\\') lv_put(w, "\\\\", 2); else if (*p == '\n') lv_put(w, "\\n", 2);
        else if ((unsigned char)*p >= 32) lv_put(w, p, 1);
    }
    lv_put(w, "\"", 1);
}
static void lv_put_kvline(lv_buf *w, const lv_kvs *k) {
    for (int i = 0; i < k->n; i++) { lv_puts(w, k->a[i].key); lv_puts(w, " = "); lv_put_value(w, k->a[i].val); lv_puts(w, "\n"); }
}
static void lv_put_object(lv_buf *w, const lv_obj *o) {
    char b[48]; snprintf(b, sizeof b, "object %d %d ", o->x, o->y); lv_puts(w, b); lv_put_value(w, o->kind);
    for (int i = 0; i < o->f.n; i++) { lv_puts(w, " "); lv_puts(w, o->f.a[i].key); lv_puts(w, "="); lv_put_value(w, o->f.a[i].val); }
}
char *arc_level_text(const arc_level *l, size_t *len) {
    lv_buf w = { 0, 0, 0 };
    lv_puts(&w, "arclvl 1\n");
    if (l) {
        lv_put_kvline(&w, &l->set);
        for (int i = 0; i < l->nl; i++) {
            const lv_layer *L = &l->L[i]; char b[48];
            lv_puts(&w, "\nlayer "); lv_put_value(&w, L->name); snprintf(b, sizeof b, " %d %d\n", L->w, L->h); lv_puts(&w, b);
            lv_put_kvline(&w, &L->set);
            for (int y = 0; y < L->h; y++) { lv_puts(&w, "|"); lv_put(&w, L->cells + (size_t)y * (size_t)(L->w + 1), (size_t)L->w); lv_puts(&w, "|\n"); }
            for (int k = 0; k < L->nobj; k++) { lv_put_object(&w, &L->obj[k]); lv_puts(&w, "\n"); }
            lv_puts(&w, "end\n");
        }
    }
    if (len) *len = w.n;
    if (!w.b) { w.b = (char *)arc__xalloc(1); w.b[0] = 0; }
    return w.b;
}

/* ---- reading ---- */
typedef struct { const char *p, *e; } lv_cur;
static void lv_ws(lv_cur *c) { while (c->p < c->e && (*c->p == ' ' || *c->p == '\t')) c->p++; }
static bool lv_key_ch(int ch, bool first) { return isalpha(ch) || ch == '_' || (!first && (isdigit(ch) || ch == '.' || ch == '-')); }
/* a value (bare or quoted) into out; false = malformed */
static bool lv_value(lv_cur *c, char *out, size_t cap) {
    size_t k = 0; lv_ws(c);
    if (c->p < c->e && *c->p == '"') {
        c->p++;
        while (c->p < c->e && *c->p != '"') {
            char ch = *c->p++;
            if (ch == '\\' && c->p < c->e) { char n = *c->p++; ch = n == 'n' ? '\n' : n; }
            if (k + 1 < cap) out[k++] = ch;
        }
        if (c->p >= c->e) return false;
        c->p++;
    } else {
        while (c->p < c->e && lv_bare((unsigned char)*c->p)) { if (k + 1 < cap) out[k++] = *c->p; c->p++; }
        if (!k) return false;
    }
    out[k] = 0; return true;
}
static bool lv_key(lv_cur *c, char *out, size_t cap) {
    size_t k = 0; lv_ws(c);
    if (c->p >= c->e || !lv_key_ch((unsigned char)*c->p, true)) return false;
    while (c->p < c->e && lv_key_ch((unsigned char)*c->p, k == 0)) { if (k + 1 < cap) out[k++] = *c->p; c->p++; }
    out[k] = 0; return true;
}
static bool lv_int(lv_cur *c, int *v) {
    lv_ws(c); const char *s = c->p; bool neg = false; long n = 0;
    if (c->p < c->e && *c->p == '-') { neg = true; c->p++; }
    if (c->p >= c->e || !isdigit((unsigned char)*c->p)) { c->p = s; return false; }
    while (c->p < c->e && isdigit((unsigned char)*c->p)) { n = n * 10 + (*c->p - '0'); if (n > 1000000) n = 1000000; c->p++; }
    *v = (int)(neg ? -n : n); return true;
}
static bool lv_at_end(lv_cur *c) { lv_ws(c); return c->p >= c->e; }
/* "object X Y KIND k=v ..." after the word object: into layer (at index `at`, -1 = the end) */
static bool lv_object_line(arc_level *l, int layer, int at, lv_cur *c, const char **why) {
    int x, y; char kind[96], key[64], val[1024];
    if (!lv_int(c, &x) || !lv_int(c, &y)) { *why = "object: give X Y KIND"; return false; }
    lv_layer *L = lv_L(l, layer);
    if (!L || x < 0 || y < 0 || x >= L->w || y >= L->h) { *why = "object: the cell is outside the layer"; return false; }
    if (!lv_value(c, kind, sizeof kind)) { *why = "object: give its kind after X Y"; return false; }
    int i = lv_obj_insert(l, layer, at < 0 ? L->nobj : at, x, y, kind);
    if (i < 0) { *why = "object: out of memory"; return false; }
    while (!lv_at_end(c)) {
        if (!lv_key(c, key, sizeof key) || c->p >= c->e || *c->p != '=') { *why = "object: fields are name=value"; arc_level_object_remove(l, layer, i); return false; }
        c->p++;
        if (!lv_value(c, val, sizeof val)) { *why = "object: a field's value is missing or has an unclosed quote"; arc_level_object_remove(l, layer, i); return false; }
        lv_kv_set(&l->L[layer].obj[i].f, key, val);
    }
    return true;
}
static bool lv_parse_into(arc_level *l, const char *text, size_t len, const char *name) {
    lv_clear(l);
    const char *p = text, *e = text + len; int line = 0, cur = -1, rows = 0; bool header = false;
    const char *why = NULL;
    char key[64], val[1024];
    while (p < e) {
        const char *ls = p; while (p < e && *p != '\n') p++;
        const char *le = p; if (p < e) p++;
        if (le > ls && le[-1] == '\r') le--;
        line++;
        lv_cur c = { ls, le }; lv_ws(&c);
        if (c.p >= c.e) continue;                                   /* blank */
        if (!header) {
            if (le - c.p >= 8 && !strncmp(c.p, "arclvl ", 7)) { c.p += 7; int v = 0; if (!lv_int(&c, &v) || v != 1) { why = "this file is a newer arclvl version"; goto fail; } header = true; continue; }
            if (*c.p == '#') continue;
            why = "the first line must be \"arclvl 1\""; goto fail;
        }
        if (*c.p == '|') {                                          /* a row */
            lv_layer *L = lv_L(l, cur);
            if (!L) { why = "a row outside a layer"; goto fail; }
            if (rows >= L->h) { why = "more rows than the layer's height"; goto fail; }
            if (le - c.p != L->w + 2 || le[-1] != '|') { why = "a row must be '|', exactly the layer's width of cells, then '|'"; goto fail; }
            for (int x = 0; x < L->w; x++) { char ch = c.p[1 + x]; if ((unsigned char)ch < 32 || (unsigned char)ch > 126) { why = "a row holds a character that is not printable ASCII"; goto fail; } L->cells[(size_t)rows * (size_t)(L->w + 1) + (size_t)x] = ch; }
            rows++; continue;
        }
        if (*c.p == '#') continue;                                  /* a comment */
        if (!lv_key(&c, key, sizeof key)) { why = "expected a setting (name = value), layer, object, a row or end"; goto fail; }
        if (!strcmp(key, "layer") && !(lv_ws(&c), c.p < c.e && *c.p == '=')) {
            if (cur >= 0) { why = "layer: the previous layer has no end"; goto fail; }
            int w = 0, h = 0;
            if (!lv_value(&c, val, sizeof val) || !lv_int(&c, &w) || !lv_int(&c, &h) || !lv_at_end(&c)) { why = "layer: give a name, a width and a height"; goto fail; }
            if (w <= 0 || h <= 0 || w > 4096 || h > 4096) { why = "layer: the size must be 1..4096 x 1..4096"; goto fail; }
            cur = arc_level_add_layer(l, val, w, h, '.'); rows = 0;
            if (cur < 0) { why = "layer: too many layers"; goto fail; }
            continue;
        }
        if (!strcmp(key, "end") && lv_at_end(&c)) {
            lv_layer *L = lv_L(l, cur);
            if (!L) { why = "end without a layer"; goto fail; }
            if (rows != L->h) { why = "end: fewer rows than the layer's height"; goto fail; }
            cur = -1; continue;
        }
        if (!strcmp(key, "object") && !(lv_ws(&c), c.p < c.e && *c.p == '=')) {
            if (cur < 0) { why = "object outside a layer"; goto fail; }
            if (!lv_object_line(l, cur, -1, &c, &why)) goto fail;
            continue;
        }
        lv_ws(&c);
        if (c.p >= c.e || *c.p != '=') { why = "a setting needs '=' (name = value)"; goto fail; }
        c.p++;
        if (!lv_value(&c, val, sizeof val) || !lv_at_end(&c)) { why = "a setting's value is missing, has an unclosed quote, or has more after it"; goto fail; }
        lv_kv_set(cur >= 0 ? &l->L[cur].set : &l->set, key, val);
    }
    if (!header) { why = "empty: the first line must be \"arclvl 1\""; goto fail; }
    if (cur >= 0) { why = "the last layer has no end"; line++; goto fail; }
    g_lv_err[0] = 0;
    return true;
fail:
    snprintf(g_lv_err, sizeof g_lv_err, "%s:%d: %s", name && *name ? name : "level", line, why ? why : "malformed");
    lv_clear(l);
    return false;
}
arc_level *arc_level_parse(const char *text, size_t len, const char *name) {
    arc_level *l = arc_level_new();
    if (!lv_parse_into(l, text ? text : "", text ? len : 0, name)) { arc_level_free(l); return NULL; }
    return l;
}
static char *lv_read_file(const char *path, size_t *n) {
    size_t sz = 0; uint8_t *d = arc_file_read(path, arc_frame(), &sz);
    if (!d) return NULL;
    char *t = (char *)arc__xalloc(sz + 1); memcpy(t, d, sz); t[sz] = 0; *n = sz;
    return t;
}
static void lv_watch(arc_level *l) {
    if (l->hot < 0) { for (int i = 0; i < 64; i++) if (!g_lv_hot[i]) { g_lv_hot[i] = l; l->hot = i; if (i >= g_lv_hot_n) g_lv_hot_n = i + 1; break; } }
    if (l->hot >= 0) arc__hot_watch(ARC__HOT_LEVEL, (uint32_t)l->hot, l->path);
}
arc_level *arc_level_load(const char *path) {
    size_t n = 0; char *t = path ? lv_read_file(path, &n) : NULL;
    if (!t) { snprintf(g_lv_err, sizeof g_lv_err, "%s: cannot read the file", path ? path : "(null)"); return NULL; }
    arc_level *l = arc_level_parse(t, n, path); free(t);
    if (l) { snprintf(l->path, sizeof l->path, "%s", path); lv_watch(l); }
    return l;
}
/* in place: a layer of the same size keeps its cell memory (pointers the game holds stay good) */
static bool lv_replace(arc_level *l, arc_level *n) {
    bool same = n->nl == l->nl;
    for (int i = 0; same && i < n->nl; i++) same = n->L[i].w == l->L[i].w && n->L[i].h == l->L[i].h;
    lv_kv_copy(&l->set, &n->set);
    if (same) {
        for (int i = 0; i < n->nl; i++) {
            lv_layer *a = &l->L[i], *b = &n->L[i];
            memcpy(a->cells, b->cells, (size_t)(a->w + 1) * (size_t)a->h);
            free(a->name); a->name = b->name; b->name = NULL;
            lv_kv_copy(&a->set, &b->set);
            for (int k = 0; k < a->nobj; k++) lv_obj_free(&a->obj[k]);
            free(a->obj); a->obj = b->obj; a->nobj = b->nobj; a->cobj = b->cobj; b->obj = NULL; b->nobj = b->cobj = 0;
        }
    } else {
        for (int i = 0; i < l->nl; i++) lv_layer_free(&l->L[i]);
        free(l->L); l->L = n->L; l->nl = n->nl; l->cl = n->cl; n->L = NULL; n->nl = n->cl = 0;
    }
    arc_level_free(n);
    return same;
}
bool arc_level_reload(arc_level *l, const char *path) {
    if (!l) return false;
    const char *pp = path && *path ? path : l->path;
    size_t n = 0; char *t = *pp ? lv_read_file(pp, &n) : NULL;
    if (!t) { snprintf(g_lv_err, sizeof g_lv_err, "%s: cannot read the file", pp); return false; }
    arc_level *nl = arc_level_parse(t, n, pp); free(t);
    if (!nl) return false;
    if (pp != l->path) snprintf(l->path, sizeof l->path, "%s", pp);
    lv_replace(l, nl);
    l->reloaded = true;
    arc__editor_level_reloaded(l);
    lv_watch(l);
    return true;
}
bool arc_level_reloaded(arc_level *l) { bool r = l && l->reloaded; if (l) l->reloaded = false; return r; }
bool arc_level_save(arc_level *l, const char *path) {
    if (!l) return false;
    const char *pp = path && *path ? path : l->path;
    if (!*pp) { snprintf(g_lv_err, sizeof g_lv_err, "arc_level_save: no path"); return false; }
    size_t n = 0; char *t = arc_level_text(l, &n);
    bool ok = arc_file_write(pp, t, n); free(t);
    if (!ok) { snprintf(g_lv_err, sizeof g_lv_err, "%s: cannot write the file", pp); return false; }
    if (pp != l->path) snprintf(l->path, sizeof l->path, "%s", pp);
    lv_watch(l);   /* (its own save is not a change to reload) */
    return true;
}
bool arc__level_hot_reload(uint32_t id, const char *path) {
    if (id >= 64 || !g_lv_hot[id]) return false;
    return arc_level_reload(g_lv_hot[id], path);
}

/* ---- for the editor: one object as a line ("object X Y KIND k=v"), and back ---- */
size_t arc__level_object_line(const arc_level *l, int layer, int i, char *out, size_t cap) {
    lv_obj *o = lv_O(l, layer, i); if (!o || !cap) { if (cap) out[0] = 0; return 0; }
    lv_buf w = { 0, 0, 0 }; lv_put_object(&w, o);
    size_t n = w.n < cap - 1 ? w.n : 0;   /* too long to keep: "" (not undoable) */
    if (n) memcpy(out, w.b, n);
    out[n] = 0; free(w.b); return n;
}
int arc__level_object_insert_line(arc_level *l, int layer, int at, const char *line) {
    lv_cur c = { line, line + strlen(line) }; const char *why = NULL; char w[16];
    if (!lv_key(&c, w, sizeof w) || strcmp(w, "object")) return -1;
    lv_layer *L = lv_L(l, layer); if (!L) return -1;
    int n0 = L->nobj, idx = at < 0 || at > n0 ? n0 : at;
    if (!lv_object_line(l, layer, idx, &c, &why)) return -1;
    return idx;
}
